/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsError_h__
#define nsError_h__

#ifndef nscore_h___
#include "nscore.h"  /* needed for nsresult */
#endif
#include "mozilla/Attributes.h"
#include "mozilla/Likely.h"

/*
 * To add error code to your module, you need to do the following:
 *
 * 1) Add a module offset code.  Add yours to the bottom of the list
 *    right below this comment, adding 1.
 *
 * 2) In your module, define a header file which uses one of the
 *    NE_ERROR_GENERATExxxxxx macros.  Some examples below:
 *
 *    #define NS_ERROR_MYMODULE_MYERROR1 NS_ERROR_GENERATE(NS_ERROR_SEVERITY_ERROR,NS_ERROR_MODULE_MYMODULE,1)
 *    #define NS_ERROR_MYMODULE_MYERROR2 NS_ERROR_GENERATE_SUCCESS(NS_ERROR_MODULE_MYMODULE,2)
 *    #define NS_ERROR_MYMODULE_MYERROR3 NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_MYMODULE,3)
 *
 */


/**
 * @name Standard Module Offset Code. Each Module should identify a unique number
 *       and then all errors associated with that module become offsets from the
 *       base associated with that module id. There are 16 bits of code bits for
 *       each module.
 */

#define NS_ERROR_MODULE_XPCOM      1
#define NS_ERROR_MODULE_BASE       2
#define NS_ERROR_MODULE_GFX        3
#define NS_ERROR_MODULE_WIDGET     4
#define NS_ERROR_MODULE_CALENDAR   5
#define NS_ERROR_MODULE_NETWORK    6
#define NS_ERROR_MODULE_PLUGINS    7
#define NS_ERROR_MODULE_LAYOUT     8
#define NS_ERROR_MODULE_HTMLPARSER 9
#define NS_ERROR_MODULE_RDF        10
#define NS_ERROR_MODULE_UCONV      11
#define NS_ERROR_MODULE_REG        12
#define NS_ERROR_MODULE_FILES      13
#define NS_ERROR_MODULE_DOM        14
#define NS_ERROR_MODULE_IMGLIB     15
#define NS_ERROR_MODULE_MAILNEWS   16
#define NS_ERROR_MODULE_EDITOR     17
#define NS_ERROR_MODULE_XPCONNECT  18
#define NS_ERROR_MODULE_PROFILE    19
#define NS_ERROR_MODULE_LDAP       20
#define NS_ERROR_MODULE_SECURITY   21
#define NS_ERROR_MODULE_DOM_XPATH  22
/* 23 used to be NS_ERROR_MODULE_DOM_RANGE (see bug 711047) */
#define NS_ERROR_MODULE_URILOADER  24
#define NS_ERROR_MODULE_CONTENT    25
#define NS_ERROR_MODULE_PYXPCOM    26
#define NS_ERROR_MODULE_XSLT       27
#define NS_ERROR_MODULE_IPC        28
#define NS_ERROR_MODULE_SVG        29
#define NS_ERROR_MODULE_STORAGE    30
#define NS_ERROR_MODULE_SCHEMA     31
#define NS_ERROR_MODULE_DOM_FILE   32
#define NS_ERROR_MODULE_DOM_INDEXEDDB 33
#define NS_ERROR_MODULE_DOM_FILEHANDLE 34
#define NS_ERROR_MODULE_SIGNED_JAR 35

/* NS_ERROR_MODULE_GENERAL should be used by modules that do not
 * care if return code values overlap. Callers of methods that
 * return such codes should be aware that they are not
 * globally unique. Implementors should be careful about blindly
 * returning codes from other modules that might also use
 * the generic base.
 */
#define NS_ERROR_MODULE_GENERAL    51

/**
 * @name Severity Code.  This flag identifies the level of warning
 */

#define NS_ERROR_SEVERITY_SUCCESS       0
#define NS_ERROR_SEVERITY_ERROR         1

/**
 * @name Mozilla Code.  This flag separates consumers of mozilla code
 *       from the native platform
 */

#define NS_ERROR_MODULE_BASE_OFFSET 0x45

/* Helpers for defining our enum, to be undef'd later */
#define SUCCESS_OR_FAILURE(sev, module, code) \
  ((uint32_t)(sev) << 31) | \
  ((uint32_t)(module + NS_ERROR_MODULE_BASE_OFFSET) << 16) | \
  (uint32_t)(code)
#define SUCCESS(code) \
  SUCCESS_OR_FAILURE(NS_ERROR_SEVERITY_SUCCESS, MODULE, code)
#define FAILURE(code) \
  SUCCESS_OR_FAILURE(NS_ERROR_SEVERITY_ERROR, MODULE, code)

/**
 * @name Standard return values
 */

/*@{*/

/* Unfortunately, our workaround for compilers that don't support enum class
 * doesn't really work for nsresult.  We need constants like NS_OK with type
 * nsresult, but they can't be used in (e.g.) switch cases if they're objects.
 * But if we define them to be of type nsresult::Enum instead, that causes
 *   return foo ? F() : NS_ERROR_FAILURE;
 * to fail, because nsresult and nsresult::Enum are two distinct types and
 * either can be converted to the other, so it's ambiguous.  So we have to fall
 * back to a regular enum.  Fortunately, we need to support that anyway for the
 * sake of C, so it's not a big deal.
 */
#if defined(__cplusplus) && defined(MOZ_HAVE_CXX11_STRONG_ENUMS)
  typedef enum class tag_nsresult : uint32_t
#elif defined(__cplusplus) && defined(MOZ_HAVE_CXX11_ENUM_TYPE)
  /* Need underlying type for workaround of Microsoft compiler (Bug 794734) */
  typedef enum tag_nsresult : uint32_t
#else
  /* C, or no strong enums */
  typedef enum tag_nsresult
#endif
  {
    #undef ERROR
    #define ERROR(key, val) key = val
    #include "ErrorList.h"
    #undef ERROR
  } nsresult;

#if defined(__cplusplus) && defined(MOZ_HAVE_CXX11_STRONG_ENUMS)
  /* We're using enum classes, so we need #define's to put the constants in
   * global scope for compatibility with old code. */
  #include "ErrorListDefines.h"
#endif

#undef SUCCESS_OR_FAILURE
#undef SUCCESS
#undef FAILURE

/**
 * @name Standard Error Handling Macros
 * @return 0 or 1 (false/true with bool type for C++)
 */

#ifdef __cplusplus
inline uint32_t NS_FAILED_impl(nsresult _nsresult) {
  return static_cast<uint32_t>(_nsresult) & 0x80000000;
}
#define NS_FAILED(_nsresult)    ((bool)MOZ_UNLIKELY(NS_FAILED_impl(_nsresult)))
#define NS_SUCCEEDED(_nsresult) ((bool)MOZ_LIKELY(!NS_FAILED_impl(_nsresult)))
#else
#define NS_FAILED_impl(_nsresult) ((_nsresult) & 0x80000000)
#define NS_FAILED(_nsresult)    (MOZ_UNLIKELY(NS_FAILED_impl(_nsresult)))
#define NS_SUCCEEDED(_nsresult) (MOZ_LIKELY(!NS_FAILED_impl(_nsresult)))
#endif

/**
 * @name Standard Error Generating Macros
 */

#define NS_ERROR_GENERATE(sev, module, code) \
    (nsresult)(((uint32_t)(sev) << 31) | \
               ((uint32_t)(module + NS_ERROR_MODULE_BASE_OFFSET) << 16) | \
               ((uint32_t)(code)))

#define NS_ERROR_GENERATE_SUCCESS(module, code) \
  NS_ERROR_GENERATE(NS_ERROR_SEVERITY_SUCCESS, module, code)

#define NS_ERROR_GENERATE_FAILURE(module, code) \
  NS_ERROR_GENERATE(NS_ERROR_SEVERITY_ERROR, module, code)

 /*
  * This will return the nsresult corresponding to the most recent NSPR failure
  * returned by PR_GetError.
  *
  ***********************************************************************
  *      Do not depend on this function. It will be going away!
  ***********************************************************************
  */
extern nsresult
NS_ErrorAccordingToNSPR();


/**
 * @name Standard Macros for retrieving error bits
 */

#ifdef __cplusplus
inline uint16_t NS_ERROR_GET_CODE(nsresult err) {
  return uint32_t(err) & 0xffff;
}
inline uint16_t NS_ERROR_GET_MODULE(nsresult err) {
  return ((uint32_t(err) >> 16) - NS_ERROR_MODULE_BASE_OFFSET) & 0x1fff;
}
inline bool NS_ERROR_GET_SEVERITY(nsresult err) {
  return uint32_t(err) >> 31;
}
#else
#define NS_ERROR_GET_CODE(err)     ((err) & 0xffff)
#define NS_ERROR_GET_MODULE(err)   ((((err) >> 16) - NS_ERROR_MODULE_BASE_OFFSET) & 0x1fff)
#define NS_ERROR_GET_SEVERITY(err) (((err) >> 31) & 0x1)
#endif


#ifdef _MSC_VER
#pragma warning(disable: 4251) /* 'nsCOMPtr<class nsIInputStream>' needs to have dll-interface to be used by clients of class 'nsInputStream' */
#pragma warning(disable: 4275) /* non dll-interface class 'nsISupports' used as base for dll-interface class 'nsIRDFNode' */
#endif

#if defined(XP_WIN) && defined(__cplusplus)
extern bool sXPCOMHasLoadedNewDLLs;
NS_EXPORT void NS_SetHasLoadedNewDLLs();
#endif

#endif
