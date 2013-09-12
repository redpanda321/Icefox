/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Global defines needed by xpcom clients */

#ifndef _XPCOM_CONFIG_H_
#define _XPCOM_CONFIG_H_

/* Define this to throw() if the compiler complains about 
 * constructors returning NULL
 */
#define CPP_THROW_NEW throw()

/* Define if the c++ compiler supports a 2-byte wchar_t */
/* #undef HAVE_CPP_2BYTE_WCHAR_T */

/* Define if the c++ compiler can resolve ambiguity with |using| */
#define HAVE_CPP_AMBIGUITY_RESOLVING_USING 1

/* Define if the c++ compiler supports char16_t */
#define HAVE_CPP_CHAR16_T 1

/* Define if a dyanmic_cast to void* gives the most derived object */
/* #undef HAVE_CPP_DYNAMIC_CAST_TO_VOID_PTR */

/* Define if the c++ compiler supports partial template specialization */
#define HAVE_CPP_PARTIAL_SPECIALIZATION 1

/* Define if the c++ compiler has trouble comparing a constant
 * reference to a templatized class to zero
 */
/* #undef HAVE_CPP_TROUBLE_COMPARING_TO_ZERO */

/* Define if the c++ compiler requires implementations of 
 * unused virtual methods
 */
#define NEED_CPP_UNUSED_IMPLEMENTATIONS 1

/* Define to either <new> or <new.h> */
#define NEW_H <new>

/* Define to either __attribute__((malloc)) or nothing */
#define NS_ATTR_MALLOC __attribute__((malloc))

/* Define to either __attribute__((warn_unused_result)) or nothing */
#define NS_WARN_UNUSED_RESULT __attribute__((warn_unused_result))

#endif /* _XPCOM_CONFIG_H_ */
