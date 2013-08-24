/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
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
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Alexander Surkov <surkov.alexander@gmail.com> (original author)
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

#include "nsWinUtils.h"

#include "nsAccessibleWrap.h"
#include "nsIWinAccessNode.h"
#include "nsArrayUtils.h"

HRESULT
nsWinUtils::ConvertToIA2Array(nsIArray *aGeckoArray, IUnknown ***aIA2Array,
                              long *aIA2ArrayLen)
{
  *aIA2Array = NULL;
  *aIA2ArrayLen = 0;

  if (!aGeckoArray)
    return S_FALSE;

  PRUint32 length = 0;
  nsresult rv = aGeckoArray->GetLength(&length);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  if (length == 0)
    return S_FALSE;

  *aIA2Array =
    static_cast<IUnknown**>(nsMemory::Alloc((length) * sizeof(IUnknown*)));
  if (!*aIA2Array)
    return E_OUTOFMEMORY;

  PRUint32 idx = 0;
  for (; idx < length; ++idx) {
    nsCOMPtr<nsIWinAccessNode> winAccessNode =
      do_QueryElementAt(aGeckoArray, idx, &rv);
    if (NS_FAILED(rv))
      break;

    void *instancePtr = NULL;
    nsresult rv = winAccessNode->QueryNativeInterface(IID_IUnknown,
                                                      &instancePtr);
    if (NS_FAILED(rv))
      break;

    (*aIA2Array)[idx] = static_cast<IUnknown*>(instancePtr);
  }

  if (NS_FAILED(rv)) {
    for (PRUint32 idx2 = 0; idx2 < idx; idx2++) {
      (*aIA2Array)[idx2]->Release();
      (*aIA2Array)[idx2] = NULL;
    }

    nsMemory::Free(*aIA2Array);
    return GetHRESULT(rv);
  }

  *aIA2ArrayLen = length;
  return S_OK;
}
