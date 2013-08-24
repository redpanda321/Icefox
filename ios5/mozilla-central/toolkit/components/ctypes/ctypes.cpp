/* -*-  Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ctypes.h"
#include "jsapi.h"
#include "mozilla/ModuleUtils.h"
#include "nsMemory.h"
#include "nsString.h"
#include "nsNativeCharsetUtils.h"

#define JSCTYPES_CONTRACTID \
  "@mozilla.org/jsctypes;1"


#define JSCTYPES_CID \
{ 0xc797702, 0x1c60, 0x4051, { 0x9d, 0xd7, 0x4d, 0x74, 0x5, 0x60, 0x56, 0x42 } }

namespace mozilla {
namespace ctypes {

static char*
UnicodeToNative(JSContext *cx, const jschar *source, size_t slen)
{
  nsCAutoString native;
  nsDependentString unicode(reinterpret_cast<const PRUnichar*>(source), slen);
  nsresult rv = NS_CopyUnicodeToNative(unicode, native);
  if (NS_FAILED(rv)) {
    JS_ReportError(cx, "could not convert string to native charset");
    return NULL;
  }

  char* result = static_cast<char*>(JS_malloc(cx, native.Length() + 1));
  if (!result)
    return NULL;

  memcpy(result, native.get(), native.Length() + 1);
  return result;
}

static JSCTypesCallbacks sCallbacks = {
  UnicodeToNative
};

NS_GENERIC_FACTORY_CONSTRUCTOR(Module)

NS_IMPL_ISUPPORTS1(Module, nsIXPCScriptable)

Module::Module()
{
}

Module::~Module()
{
}

#define XPC_MAP_CLASSNAME Module
#define XPC_MAP_QUOTED_CLASSNAME "Module"
#define XPC_MAP_WANT_CALL
#define XPC_MAP_FLAGS nsIXPCScriptable::WANT_CALL
#include "xpc_map_end.h"

static JSBool
SealObjectAndPrototype(JSContext* cx, JSObject* parent, const char* name)
{
  jsval prop;
  if (!JS_GetProperty(cx, parent, name, &prop))
    return false;

  JSObject* obj = JSVAL_TO_OBJECT(prop);
  if (!JS_GetProperty(cx, obj, "prototype", &prop))
    return false;

  JSObject* prototype = JSVAL_TO_OBJECT(prop);
  return JS_FreezeObject(cx, obj) && JS_FreezeObject(cx, prototype);
}

static JSBool
InitAndSealCTypesClass(JSContext* cx, JSObject* global)
{
  // Init the ctypes object.
  if (!JS_InitCTypesClass(cx, global))
    return false;

  // Set callbacks for charset conversion and such.
  jsval ctypes;
  if (!JS_GetProperty(cx, global, "ctypes", &ctypes))
    return false;

  JS_SetCTypesCallbacks(JSVAL_TO_OBJECT(ctypes), &sCallbacks);

  // Seal up Object, Function, Array and Error and their prototypes.  (This
  // single object instance is shared amongst everyone who imports the ctypes
  // module.)
  if (!SealObjectAndPrototype(cx, global, "Object") ||
      !SealObjectAndPrototype(cx, global, "Function") ||
      !SealObjectAndPrototype(cx, global, "Array") ||
      !SealObjectAndPrototype(cx, global, "Error"))
    return false;

  // Finally, seal the global object, for good measure. (But not recursively;
  // this breaks things.)
  return JS_FreezeObject(cx, global);
}

NS_IMETHODIMP
Module::Call(nsIXPConnectWrappedNative* wrapper,
             JSContext* cx,
             JSObject* obj,
             PRUint32 argc,
             jsval* argv,
             jsval* vp,
             bool* _retval)
{
  JSObject* global = JS_GetGlobalForScopeChain(cx);
  if (!global)
    return NS_ERROR_NOT_AVAILABLE;

  *_retval = InitAndSealCTypesClass(cx, global);
  return NS_OK;
}

}
}

NS_DEFINE_NAMED_CID(JSCTYPES_CID);

static const mozilla::Module::CIDEntry kCTypesCIDs[] = {
  { &kJSCTYPES_CID, false, NULL, mozilla::ctypes::ModuleConstructor },
  { NULL }
};

static const mozilla::Module::ContractIDEntry kCTypesContracts[] = {
  { JSCTYPES_CONTRACTID, &kJSCTYPES_CID },
  { NULL }
};

static const mozilla::Module kCTypesModule = {
  mozilla::Module::kVersion,
  kCTypesCIDs,
  kCTypesContracts
};

NSMODULE_DEFN(jsctypes) = &kCTypesModule;
