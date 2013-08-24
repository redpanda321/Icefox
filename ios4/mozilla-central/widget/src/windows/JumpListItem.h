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

#ifndef __JumpListItem_h__
#define __JumpListItem_h__

#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_WIN7

#include <windows.h>
#include <shobjidl.h>

#include "nsIJumpListItem.h"  // defines nsIJumpListItem
#include "nsIMIMEInfo.h" // defines nsILocalHandlerApp
#include "nsTArray.h"
#include "nsIMutableArray.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsIURI.h"
#include "nsICryptoHash.h"
#include "nsString.h"
#include "nsCycleCollectionParticipant.h"

namespace mozilla {
namespace widget {

class JumpListItem : public nsIJumpListItem
{
public:
  JumpListItem() :
   mItemType(nsIJumpListItem::JUMPLIST_ITEM_EMPTY)
  {}

  JumpListItem(PRInt32 type) :
   mItemType(type)
  {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSIJUMPLISTITEM

protected:
  short Type() { return mItemType; }
  short mItemType;
};

class JumpListSeparator : public JumpListItem, public nsIJumpListSeparator
{
public:
  JumpListSeparator() :
   JumpListItem(nsIJumpListItem::JUMPLIST_ITEM_SEPARATOR)
  {}

  NS_DECL_ISUPPORTS_INHERITED
  NS_IMETHOD GetType(PRInt16 *aType) { return JumpListItem::GetType(aType); }
  NS_IMETHOD Equals(nsIJumpListItem *item, PRBool *_retval) { return JumpListItem::Equals(item, _retval); }

  static nsresult GetSeparator(nsRefPtr<IShellLinkW>& aShellLink);
};

class JumpListLink : public JumpListItem, public nsIJumpListLink
{
public:
  JumpListLink() :
   JumpListItem(nsIJumpListItem::JUMPLIST_ITEM_LINK)
  {}

  NS_DECL_ISUPPORTS_INHERITED
  NS_IMETHOD GetType(PRInt16 *aType) { return JumpListItem::GetType(aType); }
  NS_IMETHOD Equals(nsIJumpListItem *item, PRBool *_retval);
  NS_DECL_NSIJUMPLISTLINK

  static nsresult GetShellItem(nsCOMPtr<nsIJumpListItem>& item, nsRefPtr<IShellItem2>& aShellItem);
  static nsresult GetJumpListLink(IShellItem *pItem, nsCOMPtr<nsIJumpListLink>& aLink);

protected:
  typedef HRESULT (WINAPI * SHCreateItemFromParsingNamePtr)(PCWSTR pszPath, IBindCtx *pbc, REFIID riid, void **ppv);

  nsString mUriTitle;
  nsCOMPtr<nsIURI> mURI;
  nsCOMPtr<nsICryptoHash> mCryptoHash;
  static const PRUnichar kSehllLibraryName[];
  static HMODULE sShellDll;
  static SHCreateItemFromParsingNamePtr createItemFromParsingName;

  nsresult HashURI(nsIURI *uri, nsACString& aUriHas);
};

class JumpListShortcut : public JumpListItem, public nsIJumpListShortcut
{
public:
  JumpListShortcut() :
   JumpListItem(nsIJumpListItem::JUMPLIST_ITEM_SHORTCUT)
  {}

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(JumpListShortcut, JumpListItem);
  NS_IMETHOD GetType(PRInt16 *aType) { return JumpListItem::GetType(aType); }
  NS_IMETHOD Equals(nsIJumpListItem *item, PRBool *_retval);
  NS_DECL_NSIJUMPLISTSHORTCUT

  static nsresult GetShellLink(nsCOMPtr<nsIJumpListItem>& item, nsRefPtr<IShellLinkW>& aShellLink);
  static nsresult GetJumpListShortcut(IShellLinkW *pLink, nsCOMPtr<nsIJumpListShortcut>& aShortcut);

protected:
  PRInt32 mIconIndex;
  nsCOMPtr<nsILocalHandlerApp> mHandlerApp;

  PRBool ExecutableExists(nsCOMPtr<nsILocalHandlerApp>& handlerApp);
};

} // namespace widget
} // namespace mozilla

#endif // MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_WIN7

#endif /* __JumpListItem_h__ */
