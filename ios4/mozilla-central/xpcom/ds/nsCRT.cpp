/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

 
/**
 * MODULE NOTES:
 * @update  gess7/30/98
 *
 * Much as I hate to do it, we were using string compares wrong.
 * Often, programmers call functions like strcmp(s1,s2), and pass
 * one or more null strings. Rather than blow up on these, I've 
 * added quick checks to ensure that cases like this don't cause
 * us to fail.
 *
 * In general, if you pass a null into any of these string compare
 * routines, we simply return 0.
 */


#include "nsCRT.h"
#include "nsIServiceManager.h"
#include "nsCharTraits.h"
#include "prbit.h"
#include "nsUTF8Utils.h"

#define ADD_TO_HASHVAL(hashval, c) \
    hashval = PR_ROTATE_LEFT32(hashval, 4) ^ (c);

//----------------------------------------------------------------------


////////////////////////////////////////////////////////////////////////////////
// My lovely strtok routine

#define IS_DELIM(m, c)          ((m)[(c) >> 3] & (1 << ((c) & 7)))
#define SET_DELIM(m, c)         ((m)[(c) >> 3] |= (1 << ((c) & 7)))
#define DELIM_TABLE_SIZE        32

char* nsCRT::strtok(char* string, const char* delims, char* *newStr)
{
  NS_ASSERTION(string, "Unlike regular strtok, the first argument cannot be null.");

  char delimTable[DELIM_TABLE_SIZE];
  PRUint32 i;
  char* result;
  char* str = string;

  for (i = 0; i < DELIM_TABLE_SIZE; i++)
    delimTable[i] = '\0';

  for (i = 0; delims[i]; i++) {
    SET_DELIM(delimTable, static_cast<PRUint8>(delims[i]));
  }
  NS_ASSERTION(delims[i] == '\0', "too many delimiters");

  // skip to beginning
  while (*str && IS_DELIM(delimTable, static_cast<PRUint8>(*str))) {
    str++;
  }
  result = str;

  // fix up the end of the token
  while (*str) {
    if (IS_DELIM(delimTable, static_cast<PRUint8>(*str))) {
      *str++ = '\0';
      break;
    }
    str++;
  }
  *newStr = str;

  return str == result ? NULL : result;
}

////////////////////////////////////////////////////////////////////////////////

/**
 * Compare unichar string ptrs, stopping at the 1st null 
 * NOTE: If both are null, we return 0.
 * NOTE: We terminate the search upon encountering a NULL
 *
 * @update  gess 11/10/99
 * @param   s1 and s2 both point to unichar strings
 * @return  0 if they match, -1 if s1<s2; 1 if s1>s2
 */
PRInt32 nsCRT::strcmp(const PRUnichar* s1, const PRUnichar* s2) {
  if(s1 && s2) {
    for (;;) {
      PRUnichar c1 = *s1++;
      PRUnichar c2 = *s2++;
      if (c1 != c2) {
        if (c1 < c2) return -1;
        return 1;
      }
      if ((0==c1) || (0==c2)) break;
    }
  }
  else {
    if (s1)                     // s2 must have been null
      return -1;
    if (s2)                     // s1 must have been null
      return 1;
  }
  return 0;
}

/**
 * Compare unichar string ptrs, stopping at the 1st null or nth char.
 * NOTE: If either is null, we return 0.
 * NOTE: We DO NOT terminate the search upon encountering NULL's before N
 *
 * @update  gess 11/10/99
 * @param   s1 and s2 both point to unichar strings
 * @return  0 if they match, -1 if s1<s2; 1 if s1>s2
 */
PRInt32 nsCRT::strncmp(const PRUnichar* s1, const PRUnichar* s2, PRUint32 n) {
  if(s1 && s2) { 
    if(n != 0) {
      do {
        PRUnichar c1 = *s1++;
        PRUnichar c2 = *s2++;
        if (c1 != c2) {
          if (c1 < c2) return -1;
          return 1;
        }
      } while (--n != 0);
    }
  }
  return 0;
}

PRUnichar* nsCRT::strdup(const PRUnichar* str)
{
  PRUint32 len = nsCRT::strlen(str);
  return strndup(str, len);
}

PRUnichar* nsCRT::strndup(const PRUnichar* str, PRUint32 len)
{
	nsCppSharedAllocator<PRUnichar> shared_allocator;
	PRUnichar* rslt = shared_allocator.allocate(len + 1); // add one for the null
  // PRUnichar* rslt = new PRUnichar[len + 1];

  if (rslt == NULL) return NULL;
  memcpy(rslt, str, len * sizeof(PRUnichar));
  rslt[len] = 0;
  return rslt;
}

  /**
   * |nsCRT::HashCode| is identical to |PL_HashString|, which tests
   *  (http://bugzilla.mozilla.org/showattachment.cgi?attach_id=26596)
   *  show to be the best hash among several other choices.
   *
   * We re-implement it here rather than calling it for two reasons:
   *  (1) in this interface, we also calculate the length of the
   *  string being hashed; and (2) the narrow and wide and `buffer' versions here
   *  will hash equivalent strings to the same value, e.g., "Hello" and L"Hello".
   */
PRUint32 nsCRT::HashCode(const char* str, PRUint32* resultingStrLen)
{
  PRUint32 h = 0;
  const char* s = str;

  if (!str) return h;

  unsigned char c;
  while ( (c = *s++) )
    ADD_TO_HASHVAL(h, c);

  if ( resultingStrLen )
    *resultingStrLen = (s-str)-1;
  return h;
}

PRUint32 nsCRT::HashCode(const char* start, PRUint32 length)
{
  PRUint32 h = 0;
  const char* s = start;
  const char* end = start + length;

  unsigned char c;
  while ( s < end ) {
    c = *s++;
    ADD_TO_HASHVAL(h, c);
  }

  return h;
}

PRUint32 nsCRT::HashCode(const PRUnichar* str, PRUint32* resultingStrLen)
{
  PRUint32 h = 0;
  const PRUnichar* s = str;

  if (!str) return h;

  PRUnichar c;
  while ( (c = *s++) )
    ADD_TO_HASHVAL(h, c);

  if ( resultingStrLen )
    *resultingStrLen = (s-str)-1;
  return h;
}

PRUint32 nsCRT::HashCode(const PRUnichar* start, PRUint32 length)
{
  PRUint32 h = 0;
  const PRUnichar* s = start;
  const PRUnichar* end = start + length;

  PRUnichar c;
  while ( s < end ) {
    c = *s++;
    ADD_TO_HASHVAL(h, c);
  }

  return h;
}

PRUint32 nsCRT::HashCodeAsUTF16(const char* start, PRUint32 length,
                                PRBool* err)
{
  PRUint32 h = 0;
  const char* s = start;
  const char* end = start + length;

  *err = PR_FALSE;

  while ( s < end )
    {
      PRUint32 ucs4 = UTF8CharEnumerator::NextChar(&s, end, err);
      if (*err) {
	return 0;
      }

      if (ucs4 < PLANE1_BASE) {
        ADD_TO_HASHVAL(h, ucs4);
      }
      else {
        ADD_TO_HASHVAL(h, H_SURROGATE(ucs4));
        ADD_TO_HASHVAL(h, L_SURROGATE(ucs4));
      }
    }

  return h;
}

// This should use NSPR but NSPR isn't exporting its PR_strtoll function
// Until then...
PRInt64 nsCRT::atoll(const char *str)
{
    if (!str)
        return LL_Zero();

    PRInt64 ll = LL_Zero(), digitll = LL_Zero();

    while (*str && *str >= '0' && *str <= '9') {
        LL_MUL(ll, ll, 10);
        LL_UI2L(digitll, (*str - '0'));
        LL_ADD(ll, ll, digitll);
        str++;
    }

    return ll;
}

