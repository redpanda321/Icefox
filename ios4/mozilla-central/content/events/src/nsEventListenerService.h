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
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Olli Pettay <Olli.Pettay@helsinki.fi> (Original Author)
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

#ifndef nsEventListenerService_h__
#define nsEventListenerService_h__
#include "nsIEventListenerService.h"
#include "nsAutoPtr.h"
#include "nsIDOMEventListener.h"
#include "nsIDOMEventTarget.h"
#include "nsString.h"
#include "nsPIDOMEventTarget.h"
#include "nsCycleCollectionParticipant.h"
#include "jsapi.h"


class nsEventListenerInfo : public nsIEventListenerInfo
{
public:
  nsEventListenerInfo(const nsAString& aType, nsIDOMEventListener* aListener,
                      PRBool aCapturing, PRBool aAllowsUntrusted,
                      PRBool aInSystemEventGroup)
  : mType(aType), mListener(aListener), mCapturing(aCapturing),
    mAllowsUntrusted(aAllowsUntrusted),
    mInSystemEventGroup(aInSystemEventGroup) {}
  virtual ~nsEventListenerInfo() {}
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(nsEventListenerInfo)
  NS_DECL_NSIEVENTLISTENERINFO
protected:
  PRBool GetJSVal(jsval* aJSVal);

  nsString                      mType;
  // nsReftPtr because that is what nsListenerStruct uses too.
  nsRefPtr<nsIDOMEventListener> mListener;
  PRPackedBool                  mCapturing;
  PRPackedBool                  mAllowsUntrusted;
  PRPackedBool                  mInSystemEventGroup;
};

class nsEventListenerService : public nsIEventListenerService
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIEVENTLISTENERSERVICE
};
#endif
