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
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Jim Mathies <jmathies@mozilla.com>
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

#ifndef __JumpListBuilder_h__
#define __JumpListBuilder_h__

#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_WIN7

#include <windows.h>

#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN7
// Needed for various com interfaces
#include <shobjidl.h>

#include "nsString.h"
#include "nsIMutableArray.h"

#include "nsIJumpListBuilder.h"
#include "nsIJumpListItem.h"
#include "JumpListItem.h"

namespace mozilla {
namespace widget {

class JumpListBuilder : public nsIJumpListBuilder
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIJUMPLISTBUILDER

  JumpListBuilder();
  virtual ~JumpListBuilder();

protected:
  static PRPackedBool sBuildingList; 

private:
  nsRefPtr<ICustomDestinationList> mJumpListMgr;
  PRUint32 mMaxItems;
  PRBool mHasCommit;

  PRBool IsSeparator(nsCOMPtr<nsIJumpListItem>& item);
  nsresult TransferIObjectArrayToIMutableArray(IObjectArray *objArray, nsIMutableArray *removedItems);

  friend class WinTaskbar;
};

} // namespace widget
} // namespace mozilla

#endif // MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_WIN7

#endif /* __JumpListBuilder_h__ */

