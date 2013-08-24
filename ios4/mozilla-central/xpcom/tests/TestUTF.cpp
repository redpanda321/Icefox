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
 * The Original Code is string testing code.
 *
 * The Initial Developer of the Original Code is
 * mozilla.org.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include <stdio.h>
#include <stdlib.h>
#include "nsString.h"
#include "nsStringBuffer.h"
#include "nsReadableUtils.h"
#include "nsCRTGlue.h"
#include "UTFStrings.h"
#include "nsCRT.h"

namespace TestUTF {

PRBool
test_valid()
{
  for (unsigned int i = 0; i < NS_ARRAY_LENGTH(ValidStrings); ++i) {
    nsDependentCString str8(ValidStrings[i].m8);
    nsDependentString str16(ValidStrings[i].m16);

    if (!NS_ConvertUTF16toUTF8(str16).Equals(str8))
      return PR_FALSE;

    if (!NS_ConvertUTF8toUTF16(str8).Equals(str16))
      return PR_FALSE;

    nsCString tmp8("string ");
    AppendUTF16toUTF8(str16, tmp8);
    if (!tmp8.Equals(NS_LITERAL_CSTRING("string ") + str8))
      return PR_FALSE;

    nsString tmp16(NS_LITERAL_STRING("string "));
    AppendUTF8toUTF16(str8, tmp16);
    if (!tmp16.Equals(NS_LITERAL_STRING("string ") + str16))
      return PR_FALSE;

    if (CompareUTF8toUTF16(str8, str16) != 0)
      return PR_FALSE;
  }
  
  return PR_TRUE;
}

PRBool
test_invalid16()
{
  for (unsigned int i = 0; i < NS_ARRAY_LENGTH(Invalid16Strings); ++i) {
    nsDependentString str16(Invalid16Strings[i].m16);
    nsDependentCString str8(Invalid16Strings[i].m8);

    if (!NS_ConvertUTF16toUTF8(str16).Equals(str8))
      return PR_FALSE;

    nsCString tmp8("string ");
    AppendUTF16toUTF8(str16, tmp8);
    if (!tmp8.Equals(NS_LITERAL_CSTRING("string ") + str8))
      return PR_FALSE;

    if (CompareUTF8toUTF16(str8, str16) != 0)
      return PR_FALSE;
  }
  
  return PR_TRUE;
}

PRBool
test_invalid8()
{
  for (unsigned int i = 0; i < NS_ARRAY_LENGTH(Invalid8Strings); ++i) {
    nsDependentString str16(Invalid8Strings[i].m16);
    nsDependentCString str8(Invalid8Strings[i].m8);

    if (!NS_ConvertUTF8toUTF16(str8).Equals(str16))
      return PR_FALSE;

    nsString tmp16(NS_LITERAL_STRING("string "));
    AppendUTF8toUTF16(str8, tmp16);
    if (!tmp16.Equals(NS_LITERAL_STRING("string ") + str16))
      return PR_FALSE;

    if (CompareUTF8toUTF16(str8, str16) != 0)
      return PR_FALSE;
  }
  
  return PR_TRUE;
}

PRBool
test_malformed8()
{
// Don't run this test in debug builds as that intentionally asserts.
#ifndef DEBUG
  for (unsigned int i = 0; i < NS_ARRAY_LENGTH(Malformed8Strings); ++i) {
    nsDependentCString str8(Malformed8Strings[i]);

    if (!NS_ConvertUTF8toUTF16(str8).IsEmpty())
      return PR_FALSE;

    nsString tmp16(NS_LITERAL_STRING("string"));
    AppendUTF8toUTF16(str8, tmp16);
    if (!tmp16.Equals(NS_LITERAL_STRING("string")))
      return PR_FALSE;

    if (CompareUTF8toUTF16(str8, EmptyString()) == 0)
      return PR_FALSE;
  }
#endif
  
  return PR_TRUE;
}

PRBool
test_hashas16()
{
  for (unsigned int i = 0; i < NS_ARRAY_LENGTH(ValidStrings); ++i) {
    nsDependentCString str8(ValidStrings[i].m8);
    PRBool err;
    if (nsCRT::HashCode(ValidStrings[i].m16) !=
        nsCRT::HashCodeAsUTF16(str8.get(), str8.Length(), &err) ||
        err)
      return PR_FALSE;
  }

  for (unsigned int i = 0; i < NS_ARRAY_LENGTH(Invalid8Strings); ++i) {
    nsDependentCString str8(Invalid8Strings[i].m8);
    PRBool err;
    if (nsCRT::HashCode(Invalid8Strings[i].m16) !=
        nsCRT::HashCodeAsUTF16(str8.get(), str8.Length(), &err) ||
        err)
      return PR_FALSE;
  }

// Don't run this test in debug builds as that intentionally asserts.
#ifndef DEBUG
  for (unsigned int i = 0; i < NS_ARRAY_LENGTH(Malformed8Strings); ++i) {
    nsDependentCString str8(Malformed8Strings[i]);
    PRBool err;
    if (nsCRT::HashCodeAsUTF16(str8.get(), str8.Length(), &err) != 0 ||
        !err)
      return PR_FALSE;
  }
#endif

  return PR_TRUE;
}

typedef PRBool (*TestFunc)();

static const struct Test
  {
    const char* name;
    TestFunc    func;
  }
tests[] =
  {
    { "test_valid", test_valid },
    { "test_invalid16", test_invalid16 },
    { "test_invalid8", test_invalid8 },
    { "test_malformed8", test_malformed8 },
    { "test_hashas16", test_hashas16 },
    { nsnull, nsnull }
  };

}

using namespace TestUTF;

int main(int argc, char **argv)
  {
    int count = 1;
    if (argc > 1)
      count = atoi(argv[1]);

    while (count--)
      {
        for (const Test* t = tests; t->name != nsnull; ++t)
          {
            printf("%25s : %s\n", t->name, t->func() ? "SUCCESS" : "FAILURE <--");
          }
      }
    
    return 0;
  }
