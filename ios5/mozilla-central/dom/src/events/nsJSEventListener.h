/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsJSEventListener_h__
#define nsJSEventListener_h__

#include "nsIDOMKeyEvent.h"
#include "nsIJSEventListener.h"
#include "nsIDOMEventListener.h"
#include "jsapi.h"
#include "nsCOMPtr.h"
#include "nsIAtom.h"
#include "nsIScriptContext.h"
#include "nsCycleCollectionParticipant.h"

// nsJSEventListener interface
// misnamed - JS no longer has exclusive rights over this interface!
class nsJSEventListener : public nsIJSEventListener
{
public:
  nsJSEventListener(nsIScriptContext* aContext, JSObject* aScopeObject,
                    nsISupports* aTarget, nsIAtom* aType, JSObject* aHandler);
  virtual ~nsJSEventListener();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  // nsIDOMEventListener interface
  NS_DECL_NSIDOMEVENTLISTENER

  // nsIJSEventListener
  virtual void SetHandler(JSObject *aHandler);

  virtual size_t SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf) const
  {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS(nsJSEventListener)

protected:
  bool IsBlackForCC();

  nsCOMPtr<nsIAtom> mEventName;
};

#endif //nsJSEventListener_h__

