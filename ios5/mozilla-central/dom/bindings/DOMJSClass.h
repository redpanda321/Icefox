/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_DOMJSClass_h
#define mozilla_dom_DOMJSClass_h

#include "jsapi.h"
#include "jsfriendapi.h"

#include "mozilla/dom/PrototypeList.h" // auto-generated

// We use slot 0 for holding the raw object.  This is safe for both
// globals and non-globals.
#define DOM_OBJECT_SLOT 0

// All DOM globals must have a slot at DOM_PROTOTYPE_SLOT. We have to
// start at 1 past JSCLASS_GLOBAL_SLOT_COUNT because XPConnect uses
// that one.
#define DOM_PROTOTYPE_SLOT (JSCLASS_GLOBAL_SLOT_COUNT + 1)

// We use these flag bits for the new bindings.
#define JSCLASS_DOM_GLOBAL JSCLASS_USERBIT1

namespace mozilla {
namespace dom {

typedef bool
(* ResolveProperty)(JSContext* cx, JSObject* wrapper, jsid id, bool set,
                    JSPropertyDescriptor* desc);
typedef bool
(* EnumerateProperties)(JS::AutoIdVector& props);

struct NativePropertyHooks
{
  ResolveProperty mResolveProperty;
  EnumerateProperties mEnumerateProperties;

  const NativePropertyHooks *mProtoHooks;
};

// Special JSClass for reflected DOM objects.
struct DOMJSClass
{
  // It would be nice to just inherit from JSClass, but that precludes pure
  // compile-time initialization of the form |DOMJSClass = {...};|, since C++
  // only allows brace initialization for aggregate/POD types.
  JSClass mBase;

  // A list of interfaces that this object implements, in order of decreasing
  // derivedness.
  const prototypes::ID mInterfaceChain[prototypes::id::_ID_Count];

  // We cache the VTable index of GetWrapperCache for objects that support it.
  //
  // -1 indicates that GetWrapperCache is not implemented on the underlying object.
  // XXXkhuey this is unused and needs to die.
  const int16_t mGetWrapperCacheVTableOffset;

  // We store the DOM object in a reserved slot whose index is mNativeSlot.
  // Sometimes it's an nsISupports and sometimes it's not; this class tells
  // us which it is.
  const bool mDOMObjectIsISupports;

  const NativePropertyHooks* mNativeHooks;

  static DOMJSClass* FromJSClass(JSClass* base) {
    MOZ_ASSERT(base->flags & JSCLASS_IS_DOMJSCLASS);
    return reinterpret_cast<DOMJSClass*>(base);
  }
  static const DOMJSClass* FromJSClass(const JSClass* base) {
    MOZ_ASSERT(base->flags & JSCLASS_IS_DOMJSCLASS);
    return reinterpret_cast<const DOMJSClass*>(base);
  }

  static DOMJSClass* FromJSClass(js::Class* base) {
    return FromJSClass(Jsvalify(base));
  }
  static const DOMJSClass* FromJSClass(const js::Class* base) {
    return FromJSClass(Jsvalify(base));
  }

  JSClass* ToJSClass() { return &mBase; }
};

inline bool
HasProtoOrIfaceArray(JSObject* global)
{
  MOZ_ASSERT(js::GetObjectClass(global)->flags & JSCLASS_DOM_GLOBAL);
  // This can be undefined if we GC while creating the global
  return !js::GetReservedSlot(global, DOM_PROTOTYPE_SLOT).isUndefined();
}

inline JSObject**
GetProtoOrIfaceArray(JSObject* global)
{
  MOZ_ASSERT(js::GetObjectClass(global)->flags & JSCLASS_DOM_GLOBAL);
  return static_cast<JSObject**>(
    js::GetReservedSlot(global, DOM_PROTOTYPE_SLOT).toPrivate());
}

} // namespace dom
} // namespace mozilla

#endif /* mozilla_dom_DOMJSClass_h */
