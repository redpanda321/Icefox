/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAString_h___
#define nsAString_h___

#ifndef nsStringFwd_h___
#include "nsStringFwd.h"
#endif

#ifndef nsStringIterator_h___
#include "nsStringIterator.h"
#endif

// If some platform(s) can't handle our template that matches literal strings,
// then we'll disable it on those platforms.
#ifndef NS_DISABLE_LITERAL_TEMPLATE
#  if (defined(_MSC_VER) && (_MSC_VER < 1310)) || (defined(__SUNPRO_CC) && (__SUNPRO_CC < 0x560)) || (defined(__HP_aCC) && (__HP_aCC <= 012100))
#    define NS_DISABLE_LITERAL_TEMPLATE
#  endif
#endif /* !NS_DISABLE_LITERAL_TEMPLATE */

#include <string.h>
#include <stdarg.h>

#include "mozilla/fallible.h"

#define kNotFound -1

  // declare nsAString
#include "string-template-def-unichar.h"
#include "nsTSubstring.h"
#include "string-template-undef.h"

  // declare nsACString
#include "string-template-def-char.h"
#include "nsTSubstring.h"
#include "string-template-undef.h"


  /**
   * ASCII case-insensitive comparator.  (for Unicode case-insensitive
   * comparision, see nsUnicharUtils.h)
   */
class nsCaseInsensitiveCStringComparator
    : public nsCStringComparator
  {
    public:
      nsCaseInsensitiveCStringComparator() {}
      typedef char char_type;

      virtual int operator()( const char_type*, const char_type*, PRUint32, PRUint32 ) const;
  };

class nsCaseInsensitiveCStringArrayComparator
  {
    public:
      template<class A, class B>
      bool Equals(const A& a, const B& b) const {
        return a.Equals(b, nsCaseInsensitiveCStringComparator());
      }
  };

  // included here for backwards compatibility
#ifndef nsSubstringTuple_h___
#include "nsSubstringTuple.h"
#endif

#endif // !defined(nsAString_h___)
