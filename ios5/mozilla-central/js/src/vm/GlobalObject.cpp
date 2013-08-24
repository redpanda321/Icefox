/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GlobalObject.h"

#include "jscntxt.h"
#include "jsdate.h"
#include "jsexn.h"
#include "jsmath.h"
#include "json.h"
#include "jsweakmap.h"

#include "builtin/Eval.h"
#include "builtin/MapObject.h"
#include "builtin/RegExp.h"
#include "frontend/BytecodeEmitter.h"
#include "vm/GlobalObject-inl.h"

#include "jsobjinlines.h"

#include "vm/RegExpObject-inl.h"
#include "vm/RegExpStatics-inl.h"

#ifdef JS_METHODJIT
#include "methodjit/Retcon.h"
#endif

using namespace js;

JSObject *
js_InitObjectClass(JSContext *cx, JSObject *obj)
{
    JS_ASSERT(obj->isNative());

    return obj->asGlobal().getOrCreateObjectPrototype(cx);
}

JSObject *
js_InitFunctionClass(JSContext *cx, JSObject *obj)
{
    JS_ASSERT(obj->isNative());

    return obj->asGlobal().getOrCreateFunctionPrototype(cx);
}

static JSBool
ThrowTypeError(JSContext *cx, unsigned argc, Value *vp)
{
    JS_ReportErrorFlagsAndNumber(cx, JSREPORT_ERROR, js_GetErrorMessage, NULL,
                                 JSMSG_THROW_TYPE_ERROR);
    return false;
}

namespace js {

JSObject *
GlobalObject::initFunctionAndObjectClasses(JSContext *cx)
{
    Rooted<GlobalObject*> self(cx, this);

    JS_THREADSAFE_ASSERT(cx->compartment != cx->runtime->atomsCompartment);
    JS_ASSERT(isNative());

    /*
     * Calling a function from a cleared global triggers this (yeah, I know).
     * Uncomment this once bug 470510 is fixed (if that bug doesn't remove
     * isCleared entirely).
     */
    // JS_ASSERT(!isCleared());

    /* If cx has no global object, make this the global object. */
    if (!cx->globalObject)
        JS_SetGlobalObject(cx, self);

    RootedObject objectProto(cx);

    /*
     * Create |Object.prototype| first, mirroring CreateBlankProto but for the
     * prototype of the created object.
     */
    objectProto = NewObjectWithGivenProto(cx, &ObjectClass, NULL, self);
    if (!objectProto || !objectProto->setSingletonType(cx))
        return NULL;

    /*
     * The default 'new' type of Object.prototype is required by type inference
     * to have unknown properties, to simplify handling of e.g. heterogenous
     * objects in JSON and script literals.
     */
    if (!objectProto->setNewTypeUnknown(cx))
        return NULL;

    /* Create |Function.prototype| next so we can create other functions. */
    RootedFunction functionProto(cx);
    {
        JSObject *functionProto_ = NewObjectWithGivenProto(cx, &FunctionClass, objectProto, self);
        if (!functionProto_)
            return NULL;
        functionProto = functionProto_->toFunction();

        /*
         * Bizarrely, |Function.prototype| must be an interpreted function, so
         * give it the guts to be one.
         */
        JSObject *proto = js_NewFunction(cx, functionProto,
                                         NULL, 0, JSFUN_INTERPRETED, self, NULL);
        if (!proto)
            return NULL;
        JS_ASSERT(proto == functionProto);
        functionProto->flags |= JSFUN_PROTOTYPE;

        Rooted<JSScript*> script(cx, JSScript::Create(cx,
                                                      /* enclosingScope = */ NullPtr(),
                                                      /* savedCallerFun = */ false,
                                                      /* principals = */ NULL,
                                                      /* originPrincipals = */ NULL,
                                                      /* compileAndGo = */ false,
                                                      /* noScriptRval = */ true,
                                                      JSVERSION_DEFAULT,
                                                      /* staticLevel = */ 0));
        if (!script || !JSScript::fullyInitTrivial(cx, script))
            return NULL;

        functionProto->initScript(script);
        functionProto->getType(cx)->interpretedFunction = functionProto;
        script->setFunction(functionProto);

        if (!functionProto->setSingletonType(cx))
            return NULL;

        /*
         * The default 'new' type of Function.prototype is required by type
         * inference to have unknown properties, to simplify handling of e.g.
         * CloneFunctionObject.
         */
        if (!functionProto->setNewTypeUnknown(cx))
            return NULL;
    }

    /* Create the Object function now that we have a [[Prototype]] for it. */
    RootedFunction objectCtor(cx);
    {
        JSObject *ctor = NewObjectWithGivenProto(cx, &FunctionClass, functionProto, self);
        if (!ctor)
            return NULL;
        objectCtor = js_NewFunction(cx, ctor, js_Object, 1, JSFUN_CONSTRUCTOR, self,
                                    CLASS_NAME(cx, Object));
        if (!objectCtor)
            return NULL;
    }

    /*
     * Install |Object| and |Object.prototype| for the benefit of subsequent
     * code that looks for them.
     */
    self->setObjectClassDetails(objectCtor, objectProto);

    /* Create |Function| so it and |Function.prototype| can be installed. */
    RootedFunction functionCtor(cx);
    {
        // Note that ctor is rooted purely for the JS_ASSERT at the end
        RootedObject ctor(cx, NewObjectWithGivenProto(cx, &FunctionClass, functionProto, self));
        if (!ctor)
            return NULL;
        functionCtor = js_NewFunction(cx, ctor, Function, 1, JSFUN_CONSTRUCTOR, self,
                                      CLASS_NAME(cx, Function));
        if (!functionCtor)
            return NULL;
        JS_ASSERT(ctor == functionCtor);
    }

    /*
     * Install |Function| and |Function.prototype| so that we can freely create
     * functions and objects without special effort.
     */
    self->setFunctionClassDetails(functionCtor, functionProto);

    /*
     * The hard part's done: now go back and add all the properties these
     * primordial values have.
     */
    if (!LinkConstructorAndPrototype(cx, objectCtor, objectProto) ||
        !DefinePropertiesAndBrand(cx, objectProto, object_props, object_methods) ||
        !DefinePropertiesAndBrand(cx, objectCtor, NULL, object_static_methods) ||
        !LinkConstructorAndPrototype(cx, functionCtor, functionProto) ||
        !DefinePropertiesAndBrand(cx, functionProto, NULL, function_methods) ||
        !DefinePropertiesAndBrand(cx, functionCtor, NULL, NULL))
    {
        return NULL;
    }

    /* Add the global Function and Object properties now. */
    jsid objectId = NameToId(CLASS_NAME(cx, Object));
    if (!self->addDataProperty(cx, objectId, JSProto_Object + JSProto_LIMIT * 2, 0))
        return NULL;
    jsid functionId = NameToId(CLASS_NAME(cx, Function));
    if (!self->addDataProperty(cx, functionId, JSProto_Function + JSProto_LIMIT * 2, 0))
        return NULL;

    /* Heavy lifting done, but lingering tasks remain. */

    /* ES5 15.1.2.1. */
    RootedId id(cx, NameToId(cx->runtime->atomState.evalAtom));
    JSObject *evalobj = js_DefineFunction(cx, self, id, IndirectEval, 1, JSFUN_STUB_GSOPS);
    if (!evalobj)
        return NULL;
    self->setOriginalEval(evalobj);

    /* ES5 13.2.3: Construct the unique [[ThrowTypeError]] function object. */
    RootedFunction throwTypeError(cx);
    throwTypeError = js_NewFunction(cx, NULL, ThrowTypeError, 0, 0, self, NULL);
    if (!throwTypeError)
        return NULL;
    if (!throwTypeError->preventExtensions(cx))
        return NULL;
    self->setThrowTypeError(throwTypeError);

    /*
     * The global object should have |Object.prototype| as its [[Prototype]].
     * Eventually we'd like to have standard classes be there from the start,
     * and thus we would know we were always setting what had previously been a
     * null [[Prototype]], but right now some code assumes it can set the
     * [[Prototype]] before standard classes have been initialized.  For now,
     * only set the [[Prototype]] if it hasn't already been set.
     */
    if (self->shouldSplicePrototype(cx) && !self->splicePrototype(cx, objectProto))
        return NULL;

    /*
     * Notify any debuggers about the creation of the script for
     * |Function.prototype| -- after all initialization, for simplicity.
     */
    js_CallNewScriptHook(cx, functionProto->script(), functionProto);
    return functionProto;
}

GlobalObject *
GlobalObject::create(JSContext *cx, Class *clasp)
{
    JS_ASSERT(clasp->flags & JSCLASS_IS_GLOBAL);

    JSObject *obj = NewObjectWithGivenProto(cx, clasp, NULL, NULL);
    if (!obj)
        return NULL;

    Rooted<GlobalObject *> global(cx, &obj->asGlobal());

    cx->compartment->initGlobal(*global);

    if (!global->setSingletonType(cx) || !global->setVarObj(cx))
        return NULL;

    /* Construct a regexp statics object for this global object. */
    JSObject *res = RegExpStatics::create(cx, global);
    if (!res)
        return NULL;
    global->initSlot(REGEXP_STATICS, ObjectValue(*res));
    global->initFlags(0);

    return global;
}

/* static */ bool
GlobalObject::initStandardClasses(JSContext *cx, Handle<GlobalObject*> global)
{
    JSAtomState &state = cx->runtime->atomState;

    /* Define a top-level property 'undefined' with the undefined value. */
    if (!global->defineProperty(cx, state.typeAtoms[JSTYPE_VOID], UndefinedValue(),
                                JS_PropertyStub, JS_StrictPropertyStub, JSPROP_PERMANENT | JSPROP_READONLY))
    {
        return false;
    }

    if (!global->initFunctionAndObjectClasses(cx))
        return false;

    /* Initialize the rest of the standard objects and functions. */
    return js_InitArrayClass(cx, global) &&
           js_InitBooleanClass(cx, global) &&
           js_InitExceptionClasses(cx, global) &&
           js_InitMathClass(cx, global) &&
           js_InitNumberClass(cx, global) &&
           js_InitJSONClass(cx, global) &&
           js_InitRegExpClass(cx, global) &&
           js_InitStringClass(cx, global) &&
           js_InitTypedArrayClasses(cx, global) &&
#if JS_HAS_XML_SUPPORT
           (!VersionHasAllowXML(cx->findVersion()) || js_InitXMLClasses(cx, global)) &&
#endif
#if JS_HAS_GENERATORS
           js_InitIteratorClasses(cx, global) &&
#endif
           js_InitDateClass(cx, global) &&
           js_InitWeakMapClass(cx, global) &&
           js_InitProxyClass(cx, global) &&
           js_InitMapClass(cx, global) &&
           js_InitSetClass(cx, global);
}

void
GlobalObject::clear(JSContext *cx)
{
    for (int key = JSProto_Null; key < JSProto_LIMIT * 3; key++)
        setSlot(key, UndefinedValue());

    /* Clear regexp statics. */
    getRegExpStatics()->clear();

    /* Clear the runtime-codegen-enabled cache. */
    setSlot(RUNTIME_CODEGEN_ENABLED, UndefinedValue());

    /*
     * Clear the original-eval and [[ThrowTypeError]] slots, in case throwing
     * trying to execute a script for this global must reinitialize standard
     * classes.  See bug 470150.
     */
    setSlot(EVAL, UndefinedValue());
    setSlot(THROWTYPEERROR, UndefinedValue());

    /*
     * Mark global as cleared. If we try to execute any compile-and-go
     * scripts from here on, we will throw.
     */
    int32_t flags = getSlot(FLAGS).toInt32();
    flags |= FLAGS_CLEARED;
    setSlot(FLAGS, Int32Value(flags));

    /*
     * Reset the new object cache in the compartment, which assumes that
     * prototypes cached on the global object are immutable.
     */
    cx->runtime->newObjectCache.purge();

#ifdef JS_METHODJIT
    /*
     * Destroy compiled code for any scripts parented to this global. Call ICs
     * can directly call scripts which have associated JIT code, and do so
     * without checking whether the script's global has been cleared.
     */
    for (gc::CellIter i(cx->compartment, gc::FINALIZE_SCRIPT); !i.done(); i.next()) {
        JSScript *script = i.get<JSScript>();
        if (script->compileAndGo && script->hasJITInfo() && script->hasClearedGlobal()) {
            mjit::Recompiler::clearStackReferences(cx->runtime->defaultFreeOp(), script);
            mjit::ReleaseScriptCode(cx->runtime->defaultFreeOp(), script);
        }
    }
#endif
}

bool
GlobalObject::isRuntimeCodeGenEnabled(JSContext *cx)
{
    HeapSlot &v = getSlotRef(RUNTIME_CODEGEN_ENABLED);
    if (v.isUndefined()) {
        /*
         * If there are callbacks, make sure that the CSP callback is installed
         * and that it permits runtime code generation, then cache the result.
         */
        JSCSPEvalChecker allows = cx->runtime->securityCallbacks->contentSecurityPolicyAllows;
        v.set(this, RUNTIME_CODEGEN_ENABLED, BooleanValue(!allows || allows(cx)));
    }
    return !v.isFalse();
}

JSFunction *
GlobalObject::createConstructor(JSContext *cx, Native ctor, JSAtom *name, unsigned length,
                                gc::AllocKind kind)
{
    RootedObject self(cx, this);
    return js_NewFunction(cx, NULL, ctor, length, JSFUN_CONSTRUCTOR, self, name, kind);
}

static JSObject *
CreateBlankProto(JSContext *cx, Class *clasp, JSObject &proto, GlobalObject &global)
{
    JS_ASSERT(clasp != &ObjectClass);
    JS_ASSERT(clasp != &FunctionClass);

    RootedObject blankProto(cx, NewObjectWithGivenProto(cx, clasp, &proto, &global));
    if (!blankProto || !blankProto->setSingletonType(cx))
        return NULL;

    return blankProto;
}

JSObject *
GlobalObject::createBlankPrototype(JSContext *cx, Class *clasp)
{
    Rooted<GlobalObject*> self(cx, this);
    JSObject *objectProto = getOrCreateObjectPrototype(cx);
    if (!objectProto)
        return NULL;

    return CreateBlankProto(cx, clasp, *objectProto, *self.get());
}

JSObject *
GlobalObject::createBlankPrototypeInheriting(JSContext *cx, Class *clasp, JSObject &proto)
{
    return CreateBlankProto(cx, clasp, proto, *this);
}

bool
LinkConstructorAndPrototype(JSContext *cx, JSObject *ctor_, JSObject *proto_)
{
    RootedObject ctor(cx, ctor_), proto(cx, proto_);

    return ctor->defineProperty(cx, cx->runtime->atomState.classPrototypeAtom,
                                ObjectValue(*proto), JS_PropertyStub, JS_StrictPropertyStub,
                                JSPROP_PERMANENT | JSPROP_READONLY) &&
           proto->defineProperty(cx, cx->runtime->atomState.constructorAtom,
                                 ObjectValue(*ctor), JS_PropertyStub, JS_StrictPropertyStub, 0);
}

bool
DefinePropertiesAndBrand(JSContext *cx, JSObject *obj_, JSPropertySpec *ps, JSFunctionSpec *fs)
{
    RootedObject obj(cx, obj_);

    if ((ps && !JS_DefineProperties(cx, obj, ps)) || (fs && !JS_DefineFunctions(cx, obj, fs)))
        return false;
    return true;
}

void
GlobalDebuggees_finalize(FreeOp *fop, JSObject *obj)
{
    fop->delete_((GlobalObject::DebuggerVector *) obj->getPrivate());
}

static Class
GlobalDebuggees_class = {
    "GlobalDebuggee", JSCLASS_HAS_PRIVATE,
    JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, GlobalDebuggees_finalize
};

GlobalObject::DebuggerVector *
GlobalObject::getDebuggers()
{
    Value debuggers = getReservedSlot(DEBUGGERS);
    if (debuggers.isUndefined())
        return NULL;
    JS_ASSERT(debuggers.toObject().getClass() == &GlobalDebuggees_class);
    return (DebuggerVector *) debuggers.toObject().getPrivate();
}

/* static */ GlobalObject::DebuggerVector *
GlobalObject::getOrCreateDebuggers(JSContext *cx, Handle<GlobalObject*> global)
{
    assertSameCompartment(cx, global);
    DebuggerVector *debuggers = global->getDebuggers();
    if (debuggers)
        return debuggers;

    JSObject *obj = NewObjectWithGivenProto(cx, &GlobalDebuggees_class, NULL, global);
    if (!obj)
        return NULL;
    debuggers = cx->new_<DebuggerVector>();
    if (!debuggers)
        return NULL;
    obj->setPrivate(debuggers);
    global->setReservedSlot(DEBUGGERS, ObjectValue(*obj));
    return debuggers;
}

/* static */ bool
GlobalObject::addDebugger(JSContext *cx, Handle<GlobalObject*> global, Debugger *dbg)
{
    DebuggerVector *debuggers = getOrCreateDebuggers(cx, global);
    if (!debuggers)
        return false;
#ifdef DEBUG
    for (Debugger **p = debuggers->begin(); p != debuggers->end(); p++)
        JS_ASSERT(*p != dbg);
#endif
    if (debuggers->empty() && !global->compartment()->addDebuggee(cx, global))
        return false;
    if (!debuggers->append(dbg)) {
        global->compartment()->removeDebuggee(cx->runtime->defaultFreeOp(), global);
        return false;
    }
    return true;
}

} // namespace js
