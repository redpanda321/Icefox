/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=79: */
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

#ifndef nsMimeTypeArray_h___
#define nsMimeTypeArray_h___

#include "nsIDOMMimeTypeArray.h"
#include "nsIDOMMimeType.h"
#include "nsString.h"
#include "nsCOMPtr.h"
#include "nsCOMArray.h"

class nsIDOMNavigator;

// NB: Due to weak references, nsNavigator has intimate knowledge of our
// members.
class nsMimeTypeArray : public nsIDOMMimeTypeArray
{
public:
  nsMimeTypeArray(nsIDOMNavigator* navigator);
  virtual ~nsMimeTypeArray();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMMIMETYPEARRAY

  nsresult Refresh();

  nsIDOMMimeType* GetItemAt(PRUint32 aIndex, nsresult* aResult);
  nsIDOMMimeType* GetNamedItem(const nsAString& aName, nsresult* aResult);

  static nsMimeTypeArray* FromSupports(nsISupports* aSupports)
  {
#ifdef DEBUG
    {
      nsCOMPtr<nsIDOMMimeTypeArray> array_qi = do_QueryInterface(aSupports);

      // If this assertion fires the QI implementation for the object in
      // question doesn't use the nsIDOMMimeTypeArray pointer as the nsISupports
      // pointer. That must be fixed, or we'll crash...
      NS_ASSERTION(array_qi == static_cast<nsIDOMMimeTypeArray*>(aSupports),
                   "Uh, fix QI!");
    }
#endif

    return static_cast<nsMimeTypeArray*>(aSupports);
  }

  void Invalidate()
  {
    // NB: This will cause GetMimeTypes to fail from now on.
    mNavigator = nsnull;
    Clear();
  }

private:
  nsresult GetMimeTypes();
  void     Clear();

protected:
  nsIDOMNavigator* mNavigator;
  // Number of mimetypes handled by plugins.
  PRUint32 mPluginMimeTypeCount;
  // mMimeTypeArray contains all mimetypes handled by plugins
  // (mPluginMimeTypeCount) and any mimetypes that we handle internally and
  // have been looked up before. The number of items in mMimeTypeArray should
  // thus always be equal to or higher than mPluginMimeTypeCount.
  nsCOMArray<nsIDOMMimeType> mMimeTypeArray;
  PRBool mInited;
};

class nsMimeType : public nsIDOMMimeType
{
public:
  nsMimeType(nsIDOMPlugin* aPlugin, nsIDOMMimeType* aMimeType);
  virtual ~nsMimeType();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMMIMETYPE

  void DetachPlugin() { mPlugin = nsnull; }

protected:
  nsIDOMPlugin* mPlugin;
  nsCOMPtr<nsIDOMMimeType> mMimeType;
};

class nsHelperMimeType : public nsIDOMMimeType
{
public:
  nsHelperMimeType(const nsAString& aType)
    : mType(aType)
  {
  }

  virtual ~nsHelperMimeType()
  {
  }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMMIMETYPE 
  
private:
  nsString mType;
};

#endif /* nsMimeTypeArray_h___ */
