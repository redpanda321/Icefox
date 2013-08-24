/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99 ft=cpp:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "XrayWrapper.h"
#include "AccessCheck.h"
#include "FilteringWrapper.h"
#include "CrossOriginWrapper.h"
#include "WrapperFactory.h"

#include "nsINode.h"
#include "nsIDocument.h"

#include "XPCWrapper.h"
#include "xpcprivate.h"

#include "jsapi.h"

#include "mozilla/dom/BindingUtils.h"

using namespace mozilla::dom;

namespace xpc {

using namespace js;

static const uint32_t JSSLOT_WN = 0;
static const uint32_t JSSLOT_RESOLVING = 1;

static XPCWrappedNative *GetWrappedNative(JSObject *obj);

namespace XrayUtils {

JSClass HolderClass = {
    "NativePropertyHolder",
    JSCLASS_HAS_RESERVED_SLOTS(2),
    JS_PropertyStub,        JS_PropertyStub, holder_get,      holder_set,
    JS_EnumerateStub,       JS_ResolveStub,  JS_ConvertStub
};

/*
 * Xray expando handling.
 *
 * We hang expandos for Xray wrappers off a reserved slot on the target object
 * so that same-origin compartments can share expandos for a given object. We
 * have a linked list of expando objects, one per origin. The properties on these
 * objects are generally wrappers pointing back to the compartment that applied
 * them.
 *
 * The expando objects should _never_ be exposed to script. The fact that they
 * live in the target compartment is a detail of the implementation, and does
 * not imply that code in the target compartment should be allowed to inspect
 * them. They are private to the origin that placed them.
 */

enum ExpandoSlots {
    JSSLOT_EXPANDO_NEXT = 0,
    JSSLOT_EXPANDO_ORIGIN,
    JSSLOT_EXPANDO_EXCLUSIVE_GLOBAL,
    JSSLOT_EXPANDO_COUNT
};

static nsIPrincipal*
ObjectPrincipal(JSObject *obj)
{
    return GetCompartmentPrincipal(js::GetObjectCompartment(obj));
}

static nsIPrincipal*
GetExpandoObjectPrincipal(JSObject *expandoObject)
{
    JS::Value v = JS_GetReservedSlot(expandoObject, JSSLOT_EXPANDO_ORIGIN);
    return static_cast<nsIPrincipal*>(v.toPrivate());
}

static void
ExpandoObjectFinalize(JSFreeOp *fop, JSObject *obj)
{
    // Release the principal.
    nsIPrincipal *principal = GetExpandoObjectPrincipal(obj);
    NS_RELEASE(principal);
}

JSClass ExpandoObjectClass = {
    "XrayExpandoObject",
    JSCLASS_HAS_RESERVED_SLOTS(JSSLOT_EXPANDO_COUNT),
    JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, ExpandoObjectFinalize
};

bool
ExpandoObjectMatchesConsumer(JSContext *cx,
                             JSObject *expandoObject,
                             nsIPrincipal *consumerOrigin,
                             JSObject *exclusiveGlobal)
{
    MOZ_ASSERT(js::IsObjectInContextCompartment(expandoObject, cx));

    // First, compare the principals.
    nsIPrincipal *o = GetExpandoObjectPrincipal(expandoObject);
    bool equal;
    // Note that it's very important here to ignore document.domain. We
    // pull the principal for the expando object off of the first consumer
    // for a given origin, and freely share the expandos amongst multiple
    // same-origin consumers afterwards. However, this means that we have
    // no way to know whether _all_ consumers have opted in to collaboration
    // by explicitly setting document.domain. So we just mandate that expando
    // sharing is unaffected by it.
    nsresult rv = consumerOrigin->EqualsIgnoringDomain(o, &equal);
    if (NS_FAILED(rv) || !equal)
        return false;

    // Sandboxes want exclusive expando objects.
    JSObject *owner = JS_GetReservedSlot(expandoObject,
                                         JSSLOT_EXPANDO_EXCLUSIVE_GLOBAL)
                                        .toObjectOrNull();
    if (!owner && !exclusiveGlobal)
        return true;

    // The exclusive global should always be wrapped in the target's compartment.
    MOZ_ASSERT(!exclusiveGlobal || js::IsObjectInContextCompartment(exclusiveGlobal, cx));
    MOZ_ASSERT(!owner || js::IsObjectInContextCompartment(owner, cx));
    return owner == exclusiveGlobal;
}

JSObject *
LookupExpandoObject(JSContext *cx, JSObject *target, nsIPrincipal *origin,
                    JSObject *exclusiveGlobal)
{
    // The expando object lives in the compartment of the target, so all our
    // work needs to happen there.
    JSAutoEnterCompartment ac;
    if (!ac.enter(cx, target) ||
        !JS_WrapObject(cx, &exclusiveGlobal))
    {
        return NULL;
    }

    // Iterate through the chain, looking for a same-origin object.
    JSObject *head = GetExpandoChain(target);
    while (head) {
        if (ExpandoObjectMatchesConsumer(cx, head, origin, exclusiveGlobal))
            return head;
        head = JS_GetReservedSlot(head, JSSLOT_EXPANDO_NEXT).toObjectOrNull();
    }

    // Not found.
    return nsnull;
}

// Convenience method for the above.
JSObject *
LookupExpandoObject(JSContext *cx, JSObject *target, JSObject *consumer)
{
    JSObject *consumerGlobal = js::GetGlobalForObjectCrossCompartment(consumer);
    bool isSandbox = !strcmp(js::GetObjectJSClass(consumerGlobal)->name, "Sandbox");
    return LookupExpandoObject(cx, target, ObjectPrincipal(consumer),
                               isSandbox ? consumerGlobal : nsnull);
}

JSObject *
AttachExpandoObject(JSContext *cx, JSObject *target, nsIPrincipal *origin,
                    JSObject *exclusiveGlobal)
{
    // Make sure the compartments are sane.
    MOZ_ASSERT(js::IsObjectInContextCompartment(target, cx));
    MOZ_ASSERT(!exclusiveGlobal || js::IsObjectInContextCompartment(exclusiveGlobal, cx));

    // We should only be used for WNs.
    MOZ_ASSERT(IS_WN_WRAPPER(target));

    // No duplicates allowed.
    MOZ_ASSERT(!LookupExpandoObject(cx, target, origin, exclusiveGlobal));

    // Create the expando object. We parent it directly to the target object.
    JSObject *expandoObject = JS_NewObjectWithGivenProto(cx, &ExpandoObjectClass,
                                                         nsnull, target);
    if (!expandoObject)
        return nsnull;

    // AddRef and store the principal.
    NS_ADDREF(origin);
    JS_SetReservedSlot(expandoObject, JSSLOT_EXPANDO_ORIGIN, PRIVATE_TO_JSVAL(origin));

    // Note the exclusive global, if any.
    JS_SetReservedSlot(expandoObject, JSSLOT_EXPANDO_EXCLUSIVE_GLOBAL,
                       OBJECT_TO_JSVAL(exclusiveGlobal));

    // If this is our first expando object, take the opportunity to preserve
    // the wrapper. This keeps our expandos alive even if the Xray wrapper gets
    // collected.
    JSObject *chain = GetExpandoChain(target);
    if (!chain) {
        XPCWrappedNative *wn =
          static_cast<XPCWrappedNative *>(xpc_GetJSPrivate(target));
        nsRefPtr<nsXPCClassInfo> ci;
        CallQueryInterface(wn->Native(), getter_AddRefs(ci));
        if (ci)
            ci->PreserveWrapper(wn->Native());
    }

    // Insert it at the front of the chain.
    JS_SetReservedSlot(expandoObject, JSSLOT_EXPANDO_NEXT, OBJECT_TO_JSVAL(chain));
    SetExpandoChain(target, expandoObject);

    return expandoObject;
}

JSObject *
EnsureExpandoObject(JSContext *cx, JSObject *wrapper, JSObject *target)
{
    // Expando objects live in the target compartment.
    JSAutoEnterCompartment ac;
    if (!ac.enter(cx, target))
        return false;

    JSObject *expandoObject = LookupExpandoObject(cx, target, wrapper);
    if (!expandoObject) {
        // If the object is a sandbox, we don't want it to share expandos with
        // anyone else, so we tag it with the sandbox global.
        //
        // NB: We first need to check the class, _then_ wrap for the target's
        // compartment.
        JSObject *consumerGlobal = js::GetGlobalForObjectCrossCompartment(wrapper);
        bool isSandbox = !strcmp(js::GetObjectJSClass(consumerGlobal)->name, "Sandbox");
        if (!JS_WrapObject(cx, &consumerGlobal))
            return NULL;
        expandoObject = AttachExpandoObject(cx, target, ObjectPrincipal(wrapper),
                                            isSandbox ? consumerGlobal : nsnull);
    }
    return expandoObject;
}

bool
CloneExpandoChain(JSContext *cx, JSObject *dst, JSObject *src)
{
    MOZ_ASSERT(js::IsObjectInContextCompartment(dst, cx));
    MOZ_ASSERT(GetExpandoChain(dst) == nsnull);

    JSObject *oldHead = GetExpandoChain(src);
    while (oldHead) {
        JSObject *exclusive = JS_GetReservedSlot(oldHead,
                                                 JSSLOT_EXPANDO_EXCLUSIVE_GLOBAL)
                                                .toObjectOrNull();
        if (!JS_WrapObject(cx, &exclusive))
            return false;
        JSObject *newHead =
          AttachExpandoObject(cx, dst, GetExpandoObjectPrincipal(oldHead), exclusive);
        if (!JS_CopyPropertiesFrom(cx, newHead, oldHead))
            return false;
        oldHead = JS_GetReservedSlot(oldHead, JSSLOT_EXPANDO_NEXT).toObjectOrNull();
    }
    return true;
}

JSObject *
createHolder(JSContext *cx, JSObject *wrappedNative, JSObject *parent)
{
    JSObject *holder = JS_NewObjectWithGivenProto(cx, &HolderClass, nsnull, parent);
    if (!holder)
        return nsnull;

    JSObject *inner = JS_ObjectToInnerObject(cx, wrappedNative);
    XPCWrappedNative *wn = GetWrappedNative(inner);

    // A note about ownership: the holder has a direct pointer to the wrapped
    // native that we're wrapping. Normally, we'd have to AddRef the pointer
    // so that it doesn't have to be collected, but then we'd have to tell the
    // cycle collector. Fortunately for us, we know that the Xray wrapper
    // itself has a reference to the flat JS object which will hold the
    // wrapped native alive. Furthermore, the reachability of that object and
    // the associated holder are exactly the same, so we can use that for our
    // strong reference.
    JS_ASSERT(IS_WN_WRAPPER(wrappedNative) ||
              js::GetObjectClass(wrappedNative)->ext.innerObject);
    js::SetReservedSlot(holder, JSSLOT_WN, PrivateValue(wn));
    js::SetReservedSlot(holder, JSSLOT_RESOLVING, PrivateValue(NULL));
    return holder;
}

}

using namespace XrayUtils;

class XPCWrappedNativeXrayTraits
{
public:
    static bool resolveNativeProperty(JSContext *cx, JSObject *wrapper, JSObject *holder, jsid id,
                                      bool set, JSPropertyDescriptor *desc);
    static bool resolveOwnProperty(JSContext *cx, js::Wrapper &jsWrapper, JSObject *wrapper,
                                   JSObject *holder, jsid id, bool set,
                                   JSPropertyDescriptor *desc);
    static bool defineProperty(JSContext *cx, JSObject *wrapper, jsid id,
                               JSPropertyDescriptor *desc);
    static bool delete_(JSContext *cx, JSObject *wrapper, jsid id, bool *bp);
    static bool enumerateNames(JSContext *cx, JSObject *wrapper, unsigned flags,
                               JS::AutoIdVector &props);
    static JSObject* getHolderObject(JSContext *cx, JSObject *wrapper)
    {
        return getHolderObject(wrapper);
    }
    static JSObject* getInnerObject(JSObject *wrapper);

    class ResolvingId
    {
    public:
        ResolvingId(JSObject *holder, jsid id);
        ~ResolvingId();

    private:
        friend class XPCWrappedNativeXrayTraits;

        jsid mId;
        JSObject *mHolder;
        ResolvingId *mPrev;
    };
    static bool isResolving(JSContext *cx, JSObject *holder, jsid id);

private:
    static JSObject* getHolderObject(JSObject *wrapper)
    {
        return &js::GetProxyExtra(wrapper, 0).toObject();
    }
    static ResolvingId* getResolvingId(JSObject *holder)
    {
        return (ResolvingId *)js::GetReservedSlot(holder, JSSLOT_RESOLVING).toPrivate();
    }
};

class ProxyXrayTraits
{
public:
    static bool resolveNativeProperty(JSContext *cx, JSObject *wrapper, JSObject *holder, jsid id,
                                      bool set, JSPropertyDescriptor *desc);
    static bool resolveOwnProperty(JSContext *cx, js::Wrapper &jsWrapper, JSObject *wrapper,
                                   JSObject *holder, jsid id, bool set,
                                   JSPropertyDescriptor *desc);
    static bool defineProperty(JSContext *cx, JSObject *wrapper, jsid id,
                               JSPropertyDescriptor *desc);
    static bool delete_(JSContext *cx, JSObject *wrapper, jsid id, bool *bp);
    static bool enumerateNames(JSContext *cx, JSObject *wrapper, unsigned flags,
                               JS::AutoIdVector &props);
    static JSObject* getHolderObject(JSContext *cx, JSObject *wrapper)
    {
        return getHolderObject(cx, wrapper, true);
    }
    static JSObject* getInnerObject(JSObject *wrapper)
    {
        return &js::GetProxyPrivate(wrapper).toObject();
    }

    class ResolvingId
    {
    public:
        ResolvingId(JSObject *holder, jsid id)
        {
        }
    };
    static bool isResolving(JSContext *cx, JSObject *holder, jsid id)
    {
      return false;
    }

private:
    static JSObject* getHolderObject(JSContext *cx, JSObject *wrapper,
                                     bool createHolder)
    {
        if (!js::GetProxyExtra(wrapper, 0).isUndefined())
            return &js::GetProxyExtra(wrapper, 0).toObject();

        if (!createHolder)
            return nsnull;

        return createHolderObject(cx, wrapper);
    }
    static JSObject* createHolderObject(JSContext *cx, JSObject *wrapper);
};

class DOMXrayTraits
{
public:
    static bool resolveNativeProperty(JSContext *cx, JSObject *wrapper, JSObject *holder, jsid id,
                                      bool set, JSPropertyDescriptor *desc);
    static bool resolveOwnProperty(JSContext *cx, js::Wrapper &jsWrapper, JSObject *wrapper,
                                   JSObject *holder, jsid id, bool set,
                                   JSPropertyDescriptor *desc);
    static bool defineProperty(JSContext *cx, JSObject *wrapper, jsid id,
                               JSPropertyDescriptor *desc);
    static bool delete_(JSContext *cx, JSObject *wrapper, jsid id, bool *bp);
    static bool enumerateNames(JSContext *cx, JSObject *wrapper, unsigned flags,
                               JS::AutoIdVector &props);
    static JSObject* getHolderObject(JSContext *cx, JSObject *wrapper)
    {
        return getHolderObject(cx, wrapper, true);
    }
    static JSObject* getInnerObject(JSObject *wrapper)
    {
        return &js::GetProxyPrivate(wrapper).toObject();
    }

    class ResolvingId
    {
    public:
        ResolvingId(JSObject *holder, jsid id)
        {
        }
    };
    static bool isResolving(JSContext *cx, JSObject *holder, jsid id)
    {
      return false;
    }

private:
    static JSObject* getHolderObject(JSContext *cx, JSObject *wrapper,
                                     bool createHolder)
    {
        if (!js::GetProxyExtra(wrapper, 0).isUndefined())
            return &js::GetProxyExtra(wrapper, 0).toObject();

        if (!createHolder)
            return nsnull;

        return createHolderObject(cx, wrapper);
    }
    static JSObject* createHolderObject(JSContext *cx, JSObject *wrapper);
};

static JSObject *
GetHolder(JSObject *obj)
{
    return &js::GetProxyExtra(obj, 0).toObject();
}

static XPCWrappedNative *
GetWrappedNative(JSObject *obj)
{
    MOZ_ASSERT(IS_WN_WRAPPER_OBJECT(obj));
    return static_cast<XPCWrappedNative *>(js::GetObjectPrivate(obj));
}

static XPCWrappedNative *
GetWrappedNativeFromHolder(JSObject *holder)
{
    MOZ_ASSERT(js::GetObjectJSClass(holder) == &HolderClass);
    return static_cast<XPCWrappedNative *>(js::GetReservedSlot(holder, JSSLOT_WN).toPrivate());
}

static JSObject *
GetWrappedNativeObjectFromHolder(JSObject *holder)
{
    return GetWrappedNativeFromHolder(holder)->GetFlatJSObject();
}

static inline JSObject *
FindWrapper(JSObject *wrapper)
{
    while (!js::IsWrapper(wrapper) ||
           !(Wrapper::wrapperHandler(wrapper)->flags() &
             WrapperFactory::IS_XRAY_WRAPPER_FLAG)) {
        if (js::IsWrapper(wrapper) &&
            js::GetProxyHandler(wrapper) == &sandboxProxyHandler) {
            wrapper = SandboxProxyHandler::wrappedObject(wrapper);
        } else {
            wrapper = js::GetObjectProto(wrapper);
        }
        // NB: we must eventually hit our wrapper.
    }

    return wrapper;
}

JSObject*
XPCWrappedNativeXrayTraits::getInnerObject(JSObject *wrapper)
{
    return GetWrappedNativeObjectFromHolder(getHolderObject(wrapper));
}

XPCWrappedNativeXrayTraits::ResolvingId::ResolvingId(JSObject *wrapper, jsid id)
  : mId(id),
    mHolder(getHolderObject(wrapper)),
    mPrev(getResolvingId(mHolder))
{
    js::SetReservedSlot(mHolder, JSSLOT_RESOLVING, PrivateValue(this));
}

XPCWrappedNativeXrayTraits::ResolvingId::~ResolvingId()
{
    NS_ASSERTION(getResolvingId(mHolder) == this, "unbalanced ResolvingIds");
    js::SetReservedSlot(mHolder, JSSLOT_RESOLVING, PrivateValue(mPrev));
}

bool
XPCWrappedNativeXrayTraits::isResolving(JSContext *cx, JSObject *holder,
                                        jsid id)
{
    for (ResolvingId *cur = getResolvingId(holder); cur; cur = cur->mPrev) {
        if (cur->mId == id)
            return true;
    }

    return false;
}

// Some DOM objects have shared properties that don't have an explicit
// getter/setter and rely on the class getter/setter. We install a
// class getter/setter on the holder object to trigger them.
JSBool
holder_get(JSContext *cx, JSHandleObject wrapper_, JSHandleId id, jsval *vp)
{
    JSObject *wrapper = FindWrapper(wrapper_);

    JSObject *holder = GetHolder(wrapper);

    XPCWrappedNative *wn = GetWrappedNativeFromHolder(holder);
    if (NATIVE_HAS_FLAG(wn, WantGetProperty)) {
        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, holder))
            return false;
        bool retval = true;
        nsresult rv = wn->GetScriptableCallback()->GetProperty(wn, cx, wrapper,
                                                               id, vp, &retval);
        if (NS_FAILED(rv) || !retval) {
            if (retval)
                XPCThrower::Throw(rv, cx);
            return false;
        }
    }
    return true;
}

JSBool
holder_set(JSContext *cx, JSHandleObject wrapper_, JSHandleId id, JSBool strict, jsval *vp)
{
    JSObject *wrapper = FindWrapper(wrapper_);

    JSObject *holder = GetHolder(wrapper);
    if (XPCWrappedNativeXrayTraits::isResolving(cx, holder, id)) {
        return true;
    }

    XPCWrappedNative *wn = GetWrappedNativeFromHolder(holder);
    if (NATIVE_HAS_FLAG(wn, WantSetProperty)) {
        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, holder))
            return false;
        bool retval = true;
        nsresult rv = wn->GetScriptableCallback()->SetProperty(wn, cx, wrapper,
                                                               id, vp, &retval);
        if (NS_FAILED(rv) || !retval) {
            if (retval)
                XPCThrower::Throw(rv, cx);
            return false;
        }
    }
    return true;
}

bool
XPCWrappedNativeXrayTraits::resolveNativeProperty(JSContext *cx, JSObject *wrapper,
                                                  JSObject *holder, jsid id, bool set,
                                                  JSPropertyDescriptor *desc)
{
    desc->obj = NULL;

    MOZ_ASSERT(js::GetObjectJSClass(holder) == &HolderClass);
    JSObject *wnObject = GetWrappedNativeObjectFromHolder(holder);
    XPCWrappedNative *wn = GetWrappedNative(wnObject);

    // This will do verification and the method lookup for us.
    XPCCallContext ccx(JS_CALLER, cx, wnObject, nsnull, id);

    // There are no native numeric properties, so we can shortcut here. We will not
    // find the property.
    if (!JSID_IS_STRING(id)) {
        /* Not found */
        return true;
    }

    XPCNativeInterface *iface;
    XPCNativeMember *member;
    if (ccx.GetWrapper() != wn ||
        !wn->IsValid()  ||
        !(iface = ccx.GetInterface()) ||
        !(member = ccx.GetMember())) {
        /* Not found */
        return true;
    }

    desc->obj = holder;
    desc->attrs = JSPROP_ENUMERATE;
    desc->getter = NULL;
    desc->setter = NULL;
    desc->shortid = 0;
    desc->value = JSVAL_VOID;

    jsval fval = JSVAL_VOID;
    if (member->IsConstant()) {
        if (!member->GetConstantValue(ccx, iface, &desc->value)) {
            JS_ReportError(cx, "Failed to convert constant native property to JS value");
            return false;
        }
    } else if (member->IsAttribute()) {
        // This is a getter/setter. Clone a function for it.
        if (!member->NewFunctionObject(ccx, iface, wrapper, &fval)) {
            JS_ReportError(cx, "Failed to clone function object for native getter/setter");
            return false;
        }

        desc->attrs |= JSPROP_GETTER;
        if (member->IsWritableAttribute())
            desc->attrs |= JSPROP_SETTER;

        // Make the property shared on the holder so no slot is allocated
        // for it. This avoids keeping garbage alive through that slot.
        desc->attrs |= JSPROP_SHARED;
    } else {
        // This is a method. Clone a function for it.
        if (!member->NewFunctionObject(ccx, iface, wrapper, &desc->value)) {
            JS_ReportError(cx, "Failed to clone function object for native function");
            return false;
        }

        // Without a wrapper the function would live on the prototype. Since we
        // don't have one, we have to avoid calling the scriptable helper's
        // GetProperty method for this property, so stub out the getter and
        // setter here explicitly.
        desc->getter = JS_PropertyStub;
        desc->setter = JS_StrictPropertyStub;
    }

    if (!JS_WrapValue(cx, &desc->value) || !JS_WrapValue(cx, &fval))
        return false;

    if (desc->attrs & JSPROP_GETTER)
        desc->getter = js::CastAsJSPropertyOp(JSVAL_TO_OBJECT(fval));
    if (desc->attrs & JSPROP_SETTER)
        desc->setter = js::CastAsJSStrictPropertyOp(JSVAL_TO_OBJECT(fval));

    // Define the property.
    return JS_DefinePropertyById(cx, holder, id, desc->value,
                                 desc->getter, desc->setter, desc->attrs);
}

static JSBool
wrappedJSObject_getter(JSContext *cx, JSHandleObject wrapper, JSHandleId id, jsval *vp)
{
    if (!IsWrapper(wrapper) || !WrapperFactory::IsXrayWrapper(wrapper)) {
        JS_ReportError(cx, "Unexpected object");
        return false;
    }

    *vp = OBJECT_TO_JSVAL(wrapper);

    return WrapperFactory::WaiveXrayAndWrap(cx, vp);
}

template <typename T>
static bool
Is(JSObject *wrapper)
{
    JSObject *holder = GetHolder(wrapper);
    XPCWrappedNative *wn = GetWrappedNativeFromHolder(holder);
    nsCOMPtr<T> native = do_QueryWrappedNative(wn);
    return !!native;
}

static JSBool
WrapURI(JSContext *cx, nsIURI *uri, jsval *vp)
{
    JSObject *scope = JS_GetGlobalForScopeChain(cx);
    nsresult rv =
        nsXPConnect::FastGetXPConnect()->WrapNativeToJSVal(cx, scope, uri, nsnull,
                                                           &NS_GET_IID(nsIURI), true,
                                                           vp, nsnull);
    if (NS_FAILED(rv)) {
        XPCThrower::Throw(rv, cx);
        return false;
    }
    return true;
}

static JSBool
documentURIObject_getter(JSContext *cx, JSHandleObject wrapper, JSHandleId id, jsval *vp)
{
    if (!IsWrapper(wrapper) || !WrapperFactory::IsXrayWrapper(wrapper)) {
        JS_ReportError(cx, "Unexpected object");
        return false;
    }

    JSObject *holder = GetHolder(wrapper);
    XPCWrappedNative *wn = GetWrappedNativeFromHolder(holder);
    nsCOMPtr<nsIDocument> native = do_QueryWrappedNative(wn);
    if (!native) {
        JS_ReportError(cx, "Unexpected object");
        return false;
    }

    nsCOMPtr<nsIURI> uri = native->GetDocumentURI();
    if (!uri) {
        JS_ReportOutOfMemory(cx);
        return false;
    }

    return WrapURI(cx, uri, vp);
}

static JSBool
baseURIObject_getter(JSContext *cx, JSHandleObject wrapper, JSHandleId id, jsval *vp)
{
    if (!IsWrapper(wrapper) || !WrapperFactory::IsXrayWrapper(wrapper)) {
        JS_ReportError(cx, "Unexpected object");
        return false;
    }

    JSObject *holder = GetHolder(wrapper);
    XPCWrappedNative *wn = GetWrappedNativeFromHolder(holder);
    nsCOMPtr<nsINode> native = do_QueryWrappedNative(wn);
    if (!native) {
        JS_ReportError(cx, "Unexpected object");
        return false;
    }
    nsCOMPtr<nsIURI> uri = native->GetBaseURI();
    if (!uri) {
        JS_ReportOutOfMemory(cx);
        return false;
    }

    return WrapURI(cx, uri, vp);
}

static JSBool
nodePrincipal_getter(JSContext *cx, JSHandleObject wrapper, JSHandleId id, jsval *vp)
{
    if (!IsWrapper(wrapper) || !WrapperFactory::IsXrayWrapper(wrapper)) {
        JS_ReportError(cx, "Unexpected object");
        return false;
    }

    JSObject *holder = GetHolder(wrapper);
    XPCWrappedNative *wn = GetWrappedNativeFromHolder(holder);
    nsCOMPtr<nsINode> node = do_QueryWrappedNative(wn);
    if (!node) {
        JS_ReportError(cx, "Unexpected object");
        return false;
    }

    JSObject *scope = JS_GetGlobalForScopeChain(cx);
    nsresult rv =
        nsXPConnect::FastGetXPConnect()->WrapNativeToJSVal(cx, scope, node->NodePrincipal(), nsnull,
                                                           &NS_GET_IID(nsIPrincipal), true,
                                                           vp, nsnull);
    if (NS_FAILED(rv)) {
        XPCThrower::Throw(rv, cx);
        return false;
    }
    return true;
}

static bool
ContentScriptHasUniversalXPConnect()
{
    nsIScriptSecurityManager *ssm = XPCWrapper::GetSecurityManager();
    if (ssm) {
        // Double-check that the subject principal according to CAPS is a content
        // principal rather than the system principal. If it is, this check is
        // meaningless.
        NS_ASSERTION(!AccessCheck::callerIsChrome(), "About to do a meaningless security check!");

        bool privileged;
        if (NS_SUCCEEDED(ssm->IsCapabilityEnabled("UniversalXPConnect", &privileged)) && privileged)
            return true;
    }
    return false;
}

bool
XPCWrappedNativeXrayTraits::resolveOwnProperty(JSContext *cx, js::Wrapper &jsWrapper,
                                               JSObject *wrapper, JSObject *holder, jsid id,
                                               bool set, PropertyDescriptor *desc)
{
    // Xray wrappers don't use the regular wrapper hierarchy, so we should be
    // in the wrapper's compartment here, not the wrappee.
    MOZ_ASSERT(js::IsObjectInContextCompartment(wrapper, cx));
    XPCJSRuntime* rt = nsXPConnect::GetRuntimeInstance();
    if (!WrapperFactory::IsPartiallyTransparent(wrapper) &&
        (((id == rt->GetStringID(XPCJSRuntime::IDX_BASEURIOBJECT) ||
           id == rt->GetStringID(XPCJSRuntime::IDX_NODEPRINCIPAL)) &&
          Is<nsINode>(wrapper)) ||
          (id == rt->GetStringID(XPCJSRuntime::IDX_DOCUMENTURIOBJECT) &&
          Is<nsIDocument>(wrapper))) &&
        (AccessCheck::callerIsChrome() || ContentScriptHasUniversalXPConnect())) {
        bool status;
        Wrapper::Action action = set ? Wrapper::SET : Wrapper::GET;
        desc->obj = NULL; // default value
        if (!jsWrapper.enter(cx, wrapper, id, action, &status))
            return status;

        desc->obj = wrapper;
        desc->attrs = JSPROP_ENUMERATE|JSPROP_SHARED;
        if (id == rt->GetStringID(XPCJSRuntime::IDX_BASEURIOBJECT))
            desc->getter = baseURIObject_getter;
        else if (id == rt->GetStringID(XPCJSRuntime::IDX_DOCUMENTURIOBJECT))
            desc->getter = documentURIObject_getter;
        else
            desc->getter = nodePrincipal_getter;
        desc->setter = NULL;
        desc->shortid = 0;
        desc->value = JSVAL_VOID;
        return true;
    }

    desc->obj = NULL;

    unsigned flags = (set ? JSRESOLVE_ASSIGNING : 0) | JSRESOLVE_QUALIFIED;
    JSObject *target = GetWrappedNativeObjectFromHolder(holder);
    JSObject *expando = LookupExpandoObject(cx, target, wrapper);

    // Check for expando properties first. Note that the expando object lives
    // in the target compartment.
    if (expando) {
        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, expando) ||
            !JS_GetPropertyDescriptorById(cx, expando, id, flags, desc))
        {
            return false;
        }
    }
    if (desc->obj) {
        if (!JS_WrapPropertyDescriptor(cx, desc))
            return false;
        // Pretend the property lives on the wrapper.
        desc->obj = wrapper;
        return true;
    }

    JSBool hasProp;
    if (!JS_HasPropertyById(cx, holder, id, &hasProp)) {
        return false;
    }
    if (!hasProp) {
        XPCWrappedNative *wn = GetWrappedNativeFromHolder(holder);

        // Run the resolve hook of the wrapped native.
        if (!NATIVE_HAS_FLAG(wn, WantNewResolve)) {
            return true;
        }

        bool retval = true;
        JSObject *pobj = NULL;
        nsresult rv = wn->GetScriptableInfo()->GetCallback()->NewResolve(wn, cx, wrapper, id,
                                                                         flags, &pobj, &retval);
        if (NS_FAILED(rv)) {
            if (retval)
                XPCThrower::Throw(rv, cx);
            return false;
        }

        if (!pobj) {
            return true;
        }

#ifdef DEBUG
        NS_ASSERTION(JS_HasPropertyById(cx, holder, id, &hasProp) &&
                     hasProp, "id got defined somewhere else?");
#endif
    }

    return true;
}

bool
XPCWrappedNativeXrayTraits::defineProperty(JSContext *cx, JSObject *wrapper, jsid id,
                                      PropertyDescriptor *desc)
{
    JSObject *holder = getHolderObject(wrapper);
    if (isResolving(cx, holder, id)) {
        if (!(desc->attrs & (JSPROP_GETTER | JSPROP_SETTER))) {
            if (!desc->getter)
                desc->getter = holder_get;
            if (!desc->setter)
                desc->setter = holder_set;
        }

        return JS_DefinePropertyById(cx, holder, id, desc->value, desc->getter, desc->setter,
                                     desc->attrs);
    }

    // We're placing an expando. The expando objects live in the target
    // compartment, so we need to enter it.
    JSObject *target = GetWrappedNativeObjectFromHolder(holder);
    JSAutoEnterCompartment ac;
    if (!ac.enter(cx, target))
        return false;

    // Grab the relevant expando object.
    JSObject *expandoObject = EnsureExpandoObject(cx, wrapper, target);
    if (!expandoObject)
        return false;

    // Wrap the property descriptor for the target compartment.
    PropertyDescriptor wrappedDesc = *desc;
    if (!JS_WrapPropertyDescriptor(cx, &wrappedDesc))
        return false;

    return JS_DefinePropertyById(cx, expandoObject, id, wrappedDesc.value,
                                 wrappedDesc.getter, wrappedDesc.setter,
                                 wrappedDesc.attrs);
}

bool
XPCWrappedNativeXrayTraits::delete_(JSContext *cx, JSObject *wrapper, jsid id, bool *bp)
{
    JSObject *holder = getHolderObject(wrapper);
    JSObject *target = GetWrappedNativeObjectFromHolder(holder);
    JSObject *expando = LookupExpandoObject(cx, target, wrapper);
    JSAutoEnterCompartment ac;
    JSBool b = true;
    jsval v;
    if (expando &&
        (!ac.enter(cx, expando) ||
         !JS_DeletePropertyById2(cx, expando, id, &v) ||
         !JS_ValueToBoolean(cx, v, &b)))
    {
        return false;
    }

    *bp = !!b;
    return true;
}

bool
XPCWrappedNativeXrayTraits::enumerateNames(JSContext *cx, JSObject *wrapper, unsigned flags,
                                           JS::AutoIdVector &props)
{
    JSObject *holder = getHolderObject(wrapper);

    // Enumerate expando properties first. Note that the expando object lives
    // in the target compartment.
    JSObject *target = GetWrappedNativeObjectFromHolder(holder);
    JSObject *expando = LookupExpandoObject(cx, target, wrapper);
    if (expando) {
        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, expando) ||
            !js::GetPropertyNames(cx, expando, flags, &props))
        {
            return false;
        }
    }
    if (!JS_WrapAutoIdVector(cx, props))
        return false;

    // Force all native properties to be materialized onto the wrapped native.
    JS::AutoIdVector wnProps(cx);
    {
        JSObject *wnObject = GetWrappedNativeObjectFromHolder(holder);

        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, wnObject))
            return false;
        if (!js::GetPropertyNames(cx, wnObject, flags, &wnProps))
            return false;
    }

    // Go through the properties we got and enumerate all native ones.
    for (size_t n = 0; n < wnProps.length(); ++n) {
        jsid id = wnProps[n];
        JSBool hasProp;
        if (!JS_HasPropertyById(cx, wrapper, id, &hasProp))
            return false;
        if (hasProp)
            props.append(id);
    }
    return true;
}

bool
ProxyXrayTraits::resolveNativeProperty(JSContext *cx, JSObject *wrapper, JSObject *holder,
                                       jsid id, bool set, JSPropertyDescriptor *desc)
{
    JSObject *obj = getInnerObject(wrapper);
    return js::GetProxyHandler(obj)->getPropertyDescriptor(cx, wrapper, id, set, desc);
}

bool
ProxyXrayTraits::resolveOwnProperty(JSContext *cx, js::Wrapper &jsWrapper, JSObject *wrapper,
                                    JSObject *holder, jsid id, bool set, PropertyDescriptor *desc)
{
    JSObject *obj = getInnerObject(wrapper);
    bool ok = js::GetProxyHandler(obj)->getOwnPropertyDescriptor(cx, wrapper, id, set, desc);
    if (ok) {
        // The 'not found' property descriptor has obj == NULL.
        if (desc->obj)
            desc->obj = wrapper;
    }

    // Own properties don't get cached on the holder. Just return.
    return ok;
}

bool
ProxyXrayTraits::defineProperty(JSContext *cx, JSObject *wrapper, jsid id,
                                PropertyDescriptor *desc)
{
    JSObject *holder = getHolderObject(cx, wrapper);
    if (!holder)
        return false;

    return JS_DefinePropertyById(cx, holder, id, desc->value, desc->getter, desc->setter,
                                 desc->attrs);
}

bool
ProxyXrayTraits::delete_(JSContext *cx, JSObject *wrapper, jsid id, bool *bp)
{
    JSObject *obj = getInnerObject(wrapper);
    if (!js::GetProxyHandler(obj)->delete_(cx, wrapper, id, bp))
        return false;

    JSObject *holder;
    if (*bp && (holder = getHolderObject(cx, wrapper, false)))
        JS_DeletePropertyById(cx, holder, id);

    return true;
}

bool
ProxyXrayTraits::enumerateNames(JSContext *cx, JSObject *wrapper, unsigned flags,
                                JS::AutoIdVector &props)
{
    JSObject *obj = getInnerObject(wrapper);
    if (flags & (JSITER_OWNONLY | JSITER_HIDDEN))
        return js::GetProxyHandler(obj)->getOwnPropertyNames(cx, wrapper, props);

    return js::GetProxyHandler(obj)->enumerate(cx, wrapper, props);
}

// The 'holder' here isn't actually of [[Class]] HolderClass like those used by
// XPCWrappedNativeXrayTraits. Instead, it's a funny hybrid of the 'expando' and
// 'holder' properties. However, we store it in the same slot. Exercise caution.
JSObject*
ProxyXrayTraits::createHolderObject(JSContext *cx, JSObject *wrapper)
{
    JSObject *obj = JS_NewObjectWithGivenProto(cx, nsnull, nsnull,
                                               JS_GetGlobalForObject(cx, wrapper));
    if (!obj)
        return nsnull;
    js::SetProxyExtra(wrapper, 0, ObjectValue(*obj));
    return obj;
}

bool
DOMXrayTraits::resolveNativeProperty(JSContext *cx, JSObject *wrapper, JSObject *holder, jsid id,
                                     bool set, JSPropertyDescriptor *desc)
{
    JSObject *obj = getInnerObject(wrapper);
    const NativePropertyHooks *nativeHooks =
        DOMJSClass::FromJSClass(JS_GetClass(obj))->mNativeHooks;

    do {
        if (nativeHooks->mResolveProperty(cx, wrapper, id, set, desc) &&
            desc->obj) {
            NS_ASSERTION(desc->obj == wrapper, "What did we resolve this on?");
            return true;
        }
    } while ((nativeHooks = nativeHooks->mProtoHooks));

    return true;
}

bool
DOMXrayTraits::resolveOwnProperty(JSContext *cx, js::Wrapper &jsWrapper, JSObject *wrapper,
                                  JSObject *holder, jsid id, bool set, JSPropertyDescriptor *desc)
{
    return true;
}

bool
DOMXrayTraits::defineProperty(JSContext *cx, JSObject *wrapper, jsid id, PropertyDescriptor *desc)
{
    JSObject *holder = getHolderObject(cx, wrapper);
    if (!holder)
        return false;

    return JS_DefinePropertyById(cx, holder, id, desc->value, desc->getter, desc->setter,
                                 desc->attrs);
}

bool
DOMXrayTraits::delete_(JSContext *cx, JSObject *wrapper, jsid id, bool *bp)
{
    JSObject *holder;
    if ((holder = getHolderObject(cx, wrapper, false)))
        JS_DeletePropertyById(cx, holder, id);

    return true;
}

bool
DOMXrayTraits::enumerateNames(JSContext *cx, JSObject *wrapper, unsigned flags,
                              JS::AutoIdVector &props)
{
    if (flags & (JSITER_OWNONLY | JSITER_HIDDEN))
        // Probably need to return expandos on the Xray here!
        return true;

    JSObject *obj = getInnerObject(wrapper);
    const NativePropertyHooks *nativeHooks =
        DOMJSClass::FromJSClass(JS_GetClass(obj))->mNativeHooks;

    do {
        if (!nativeHooks->mEnumerateProperties(props)) {
            return false;
        }
    } while ((nativeHooks = nativeHooks->mProtoHooks));

    return true;
}

JSObject*
DOMXrayTraits::createHolderObject(JSContext *cx, JSObject *wrapper)
{
    JSObject *obj = JS_NewObjectWithGivenProto(cx, nsnull, nsnull,
                                               JS_GetGlobalForObject(cx, wrapper));
    if (!obj)
        return nsnull;
    js::SetProxyExtra(wrapper, 0, ObjectValue(*obj));
    return obj;
}

template <typename Base, typename Traits>
XrayWrapper<Base, Traits>::XrayWrapper(unsigned flags)
  : Base(flags | WrapperFactory::IS_XRAY_WRAPPER_FLAG)
{
}

template <typename Base, typename Traits>
XrayWrapper<Base, Traits>::~XrayWrapper()
{
}

namespace XrayUtils {

bool
IsTransparent(JSContext *cx, JSObject *wrapper)
{
    if (WrapperFactory::HasWaiveXrayFlag(wrapper))
        return true;

    if (!WrapperFactory::IsPartiallyTransparent(wrapper))
        return false;

    // Redirect access straight to the wrapper if UniversalXPConnect is enabled.
    // We don't need to check for system principal here, because only content
    // scripts have Partially Transparent wrappers.
    return ContentScriptHasUniversalXPConnect();
}

JSObject *
GetNativePropertiesObject(JSContext *cx, JSObject *wrapper)
{
    NS_ASSERTION(js::IsWrapper(wrapper) && WrapperFactory::IsXrayWrapper(wrapper),
                 "bad object passed in");

    JSObject *holder = GetHolder(wrapper);
    NS_ASSERTION(holder, "uninitialized wrapper being used?");
    return holder;
}

}

static JSBool
XrayToString(JSContext *cx, unsigned argc, jsval *vp)
{
    JSObject *wrapper = JS_THIS_OBJECT(cx, vp);
    if (!wrapper)
        return false;
    if (!IsWrapper(wrapper) || !WrapperFactory::IsXrayWrapper(wrapper)) {
        JS_ReportError(cx, "XrayToString called on an incompatible object");
        return false;
    }

    nsAutoString result(NS_LITERAL_STRING("[object XrayWrapper "));
    JSObject *obj = &js::GetProxyPrivate(wrapper).toObject();
    if (mozilla::dom::binding::instanceIsProxy(obj)) {
        JSString *wrapperStr = js::GetProxyHandler(wrapper)->obj_toString(cx, wrapper);
        size_t length;
        const jschar* chars = JS_GetStringCharsAndLength(cx, wrapperStr, &length);
        if (!chars) {
            JS_ReportOutOfMemory(cx);
            return false;
        }
        result.Append(chars, length);
    } else if (IsDOMClass(JS_GetClass(obj))) {
        result.AppendLiteral("[Object ");
        result.AppendASCII(JS_GetClass(obj)->name);
        result.Append(']');
    } else {
        JSObject *holder = GetHolder(wrapper);
        XPCWrappedNative *wn = GetWrappedNativeFromHolder(holder);
        JSObject *wrappednative = wn->GetFlatJSObject();

        XPCCallContext ccx(JS_CALLER, cx, wrappednative);
        char *wrapperStr = wn->ToString(ccx);
        if (!wrapperStr) {
            JS_ReportOutOfMemory(cx);
            return false;
        }
        result.AppendASCII(wrapperStr);
        JS_smprintf_free(wrapperStr);
    }

    result.Append(']');

    JSString *str = JS_NewUCStringCopyN(cx, reinterpret_cast<const jschar *>(result.get()),
                                        result.Length());
    if (!str)
        return false;

    *vp = STRING_TO_JSVAL(str);
    return true;
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::getPropertyDescriptor(JSContext *cx, JSObject *wrapper, jsid id,
                                                 bool set, js::PropertyDescriptor *desc)
{
    JSObject *holder = Traits::getHolderObject(cx, wrapper);
    if (Traits::isResolving(cx, holder, id)) {
        desc->obj = NULL;
        return true;
    }

    bool status;
    Wrapper::Action action = set ? Wrapper::SET : Wrapper::GET;
    desc->obj = NULL; // default value
    if (!this->enter(cx, wrapper, id, action, &status))
        return status;

    typename Traits::ResolvingId resolving(wrapper, id);

    // Redirect access straight to the wrapper if we should be transparent.
    if (XrayUtils::IsTransparent(cx, wrapper)) {
        JSObject *obj = Traits::getInnerObject(wrapper);
        {
            JSAutoEnterCompartment ac;
            if (!ac.enter(cx, obj))
                return false;

            if (!JS_GetPropertyDescriptorById(cx, obj, id,
                                              (set ? JSRESOLVE_ASSIGNING : 0) | JSRESOLVE_QUALIFIED,
                                              desc)) {
                return false;
            }
        }

        if (desc->obj)
            desc->obj = wrapper;
        return JS_WrapPropertyDescriptor(cx, desc);
    }

    if (!holder)
        return false;

    // Partially transparent wrappers (which used to be known as XOWs) don't
    // have a .wrappedJSObject property.
    XPCJSRuntime* rt = nsXPConnect::GetRuntimeInstance();
    if (!WrapperFactory::IsPartiallyTransparent(wrapper) &&
        id == rt->GetStringID(XPCJSRuntime::IDX_WRAPPED_JSOBJECT)) {
        bool status;
        Wrapper::Action action = set ? Wrapper::SET : Wrapper::GET;
        desc->obj = NULL; // default value
        if (!this->enter(cx, wrapper, id, action, &status))
            return status;

        desc->obj = wrapper;
        desc->attrs = JSPROP_ENUMERATE|JSPROP_SHARED;
        desc->getter = wrappedJSObject_getter;
        desc->setter = NULL;
        desc->shortid = 0;
        desc->value = JSVAL_VOID;
        return true;
    }

    if (!Traits::resolveOwnProperty(cx, *this, wrapper, holder, id, set, desc))
        return false;

    if (desc->obj)
        return true;

    if (!JS_GetPropertyDescriptorById(cx, holder, id, JSRESOLVE_QUALIFIED, desc))
        return false;
    if (desc->obj) {
        desc->obj = wrapper;
        return true;
    }

    // Nothing in the cache. Call through, and cache the result.
    if (!Traits::resolveNativeProperty(cx, wrapper, holder, id, set, desc))
        return false;

    if (!desc->obj) {
        if (id != nsXPConnect::GetRuntimeInstance()->GetStringID(XPCJSRuntime::IDX_TO_STRING))
            return true;

        JSFunction *toString = JS_NewFunction(cx, XrayToString, 0, 0, holder, "toString");
        if (!toString)
            return false;

        desc->attrs = 0;
        desc->getter = NULL;
        desc->setter = NULL;
        desc->shortid = 0;
        desc->value.setObject(*JS_GetFunctionObject(toString));
    }

    desc->obj = wrapper;

    unsigned flags = (set ? JSRESOLVE_ASSIGNING : 0) | JSRESOLVE_QUALIFIED;
    return JS_DefinePropertyById(cx, holder, id, desc->value, desc->getter, desc->setter,
                                 desc->attrs) &&
           JS_GetPropertyDescriptorById(cx, holder, id, flags, desc);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::getOwnPropertyDescriptor(JSContext *cx, JSObject *wrapper, jsid id,
                                                    bool set, PropertyDescriptor *desc)
{
    JSObject *holder = Traits::getHolderObject(cx, wrapper);
    if (Traits::isResolving(cx, holder, id)) {
        desc->obj = NULL;
        return true;
    }

    bool status;
    Wrapper::Action action = set ? Wrapper::SET : Wrapper::GET;
    desc->obj = NULL; // default value
    if (!this->enter(cx, wrapper, id, action, &status))
        return status;

    typename Traits::ResolvingId resolving(wrapper, id);

    // NB: Nothing we do here acts on the wrapped native itself, so we don't
    // enter our policy.
    // Redirect access straight to the wrapper if we should be transparent.
    if (XrayUtils::IsTransparent(cx, wrapper)) {
        JSObject *obj = Traits::getInnerObject(wrapper);
        {
            JSAutoEnterCompartment ac;
            if (!ac.enter(cx, obj))
                return false;

            if (!JS_GetPropertyDescriptorById(cx, obj, id,
                                              (set ? JSRESOLVE_ASSIGNING : 0) | JSRESOLVE_QUALIFIED,
                                              desc)) {
                return false;
            }
        }

        desc->obj = (desc->obj == obj) ? wrapper : nsnull;
        return JS_WrapPropertyDescriptor(cx, desc);
    }

    if (!Traits::resolveOwnProperty(cx, *this, wrapper, holder, id, set, desc))
        return false;

    if (desc->obj)
        return true;

    unsigned flags = (set ? JSRESOLVE_ASSIGNING : 0) | JSRESOLVE_QUALIFIED;
    if (!JS_GetPropertyDescriptorById(cx, holder, id, flags, desc))
        return false;

    // Pretend we found the property on the wrapper, not the holder.
    if (desc->obj)
        desc->obj = wrapper;

    return true;
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::defineProperty(JSContext *cx, JSObject *wrapper, jsid id,
                                          js::PropertyDescriptor *desc)
{
    // If shadowing is forbidden, see if the id corresponds to an underlying
    // native property.
    if (WrapperFactory::IsShadowingForbidden(wrapper)) {
        JSObject *holder = Traits::getHolderObject(cx, wrapper);
        js::PropertyDescriptor nativeProp;
        if (!Traits::resolveNativeProperty(cx, wrapper, holder, id, false, &nativeProp))
            return false;
        if (nativeProp.obj) {
            JS_ReportError(cx, "Permission denied to shadow native property");
            return false;
        }
    }

    // Redirect access straight to the wrapper if we should be transparent.
    if (XrayUtils::IsTransparent(cx, wrapper)) {
        JSObject *obj = Traits::getInnerObject(wrapper);
        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, obj))
            return false;

        if (!JS_WrapPropertyDescriptor(cx, desc))
            return false;

        return JS_DefinePropertyById(cx, obj, id, desc->value, desc->getter, desc->setter,
                                     desc->attrs);
    }

    PropertyDescriptor existing_desc;
    if (!getOwnPropertyDescriptor(cx, wrapper, id, true, &existing_desc))
        return false;

    if (existing_desc.obj && (existing_desc.attrs & JSPROP_PERMANENT))
        return true; // silently ignore attempt to overwrite native property

    return Traits::defineProperty(cx, wrapper, id, desc);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::getOwnPropertyNames(JSContext *cx, JSObject *wrapper,
                                               JS::AutoIdVector &props)
{
    return enumerate(cx, wrapper, JSITER_OWNONLY | JSITER_HIDDEN, props);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::delete_(JSContext *cx, JSObject *wrapper, jsid id, bool *bp)
{
    // Redirect access straight to the wrapper if we should be transparent.
    if (XrayUtils::IsTransparent(cx, wrapper)) {
        JSObject *obj = Traits::getInnerObject(wrapper);

        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, obj))
            return false;

        JSBool b;
        jsval v;
        if (!JS_DeletePropertyById2(cx, obj, id, &v) || !JS_ValueToBoolean(cx, v, &b))
            return false;
        *bp = !!b;
        return true;
    }

    return Traits::delete_(cx, wrapper, id, bp);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::enumerate(JSContext *cx, JSObject *wrapper, unsigned flags,
                                     JS::AutoIdVector &props)
{
    // Redirect access straight to the wrapper if we should be transparent.
    if (XrayUtils::IsTransparent(cx, wrapper)) {
        JSObject *obj = Traits::getInnerObject(wrapper);
        JSAutoEnterCompartment ac;
        if (!ac.enter(cx, obj))
            return false;

        return js::GetPropertyNames(cx, obj, flags, &props);
    }

    if (WrapperFactory::IsPartiallyTransparent(wrapper)) {
        JS_ReportError(cx, "Not allowed to enumerate cross origin objects");
        return false;
    }

    return Traits::enumerateNames(cx, wrapper, flags, props);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::enumerate(JSContext *cx, JSObject *wrapper, JS::AutoIdVector &props)
{
    return enumerate(cx, wrapper, 0, props);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::get(JSContext *cx, JSObject *wrapper, JSObject *receiver, jsid id,
                               js::Value *vp)
{
    // Skip our Base if it isn't already ProxyHandler.
    // NB: None of the functions we call are prepared for the receiver not
    // being the wrapper, so ignore the receiver here.
    return BaseProxyHandler::get(cx, wrapper, wrapper, id, vp);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::set(JSContext *cx, JSObject *wrapper, JSObject *receiver, jsid id,
                               bool strict, js::Value *vp)
{
    // Skip our Base if it isn't already BaseProxyHandler.
    // NB: None of the functions we call are prepared for the receiver not
    // being the wrapper, so ignore the receiver here.
    return BaseProxyHandler::set(cx, wrapper, wrapper, id, strict, vp);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::has(JSContext *cx, JSObject *wrapper, jsid id, bool *bp)
{
    // Skip our Base if it isn't already ProxyHandler.
    return BaseProxyHandler::has(cx, wrapper, id, bp);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::hasOwn(JSContext *cx, JSObject *wrapper, jsid id, bool *bp)
{
    // Skip our Base if it isn't already ProxyHandler.
    return BaseProxyHandler::hasOwn(cx, wrapper, id, bp);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::keys(JSContext *cx, JSObject *wrapper, JS::AutoIdVector &props)
{
    // Skip our Base if it isn't already ProxyHandler.
    return BaseProxyHandler::keys(cx, wrapper, props);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::iterate(JSContext *cx, JSObject *wrapper, unsigned flags,
                                   js::Value *vp)
{
    // Skip our Base if it isn't already ProxyHandler.
    return BaseProxyHandler::iterate(cx, wrapper, flags, vp);
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::call(JSContext *cx, JSObject *wrapper, unsigned argc, js::Value *vp)
{
    JSObject *holder = GetHolder(wrapper);
    XPCWrappedNative *wn = GetWrappedNativeFromHolder(holder);

    // Run the resolve hook of the wrapped native.
    if (NATIVE_HAS_FLAG(wn, WantCall)) {
        XPCCallContext ccx(JS_CALLER, cx, wrapper, nsnull, JSID_VOID, argc,
                           vp + 2, vp);
        if (!ccx.IsValid())
            return false;
        bool ok = true;
        nsresult rv = wn->GetScriptableInfo()->GetCallback()->Call(wn, cx, wrapper,
                                                                   argc, vp + 2, vp, &ok);
        if (NS_FAILED(rv)) {
            if (ok)
                XPCThrower::Throw(rv, cx);
            return false;
        }
    }

    return true;
}

template <typename Base, typename Traits>
bool
XrayWrapper<Base, Traits>::construct(JSContext *cx, JSObject *wrapper, unsigned argc,
                                     js::Value *argv, js::Value *rval)
{
    JSObject *holder = GetHolder(wrapper);
    XPCWrappedNative *wn = GetWrappedNativeFromHolder(holder);

    // Run the resolve hook of the wrapped native.
    if (NATIVE_HAS_FLAG(wn, WantConstruct)) {
        XPCCallContext ccx(JS_CALLER, cx, wrapper, nsnull, JSID_VOID, argc, argv, rval);
        if (!ccx.IsValid())
            return false;
        bool ok = true;
        nsresult rv = wn->GetScriptableInfo()->GetCallback()->Construct(wn, cx, wrapper,
                                                                        argc, argv, rval, &ok);
        if (NS_FAILED(rv)) {
            if (ok)
                XPCThrower::Throw(rv, cx);
            return false;
        }
    }

    return true;
}


#define XRAY XrayWrapper<CrossCompartmentSecurityWrapper, XPCWrappedNativeXrayTraits >
template <> XRAY XRAY::singleton(0);
template class XRAY;
#undef XRAY

#define XRAY XrayWrapper<SameCompartmentSecurityWrapper, XPCWrappedNativeXrayTraits >
template <> XRAY XRAY::singleton(0);
template class XRAY;
#undef XRAY

#define XRAY XrayWrapper<CrossCompartmentWrapper, XPCWrappedNativeXrayTraits >
template <> XRAY XRAY::singleton(0);
template class XRAY;
#undef XRAY

#define XRAY XrayWrapper<CrossCompartmentWrapper, ProxyXrayTraits >
template <> XRAY XRAY::singleton(0);
template class XRAY;
#undef XRAY

#define XRAY XrayWrapper<CrossCompartmentWrapper, DOMXrayTraits >
template <> XRAY XRAY::singleton(0);
template class XRAY;
#undef XRAY

}
