/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDOMDataContainerEvent_h___
#define nsDOMDataContainerEvent_h___

#include "nsIDOMDataContainerEvent.h"
#include "nsDOMEvent.h"
#include "nsInterfaceHashtable.h"

class nsDOMDataContainerEvent : public nsDOMEvent,
                                public nsIDOMDataContainerEvent
{
public:
  nsDOMDataContainerEvent(nsPresContext* aPresContext, nsEvent* aEvent);

  NS_DECL_ISUPPORTS_INHERITED

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsDOMDataContainerEvent, nsDOMEvent)

  NS_FORWARD_TO_NSDOMEVENT

  NS_DECL_NSIDOMDATACONTAINEREVENT

private:
  static PLDHashOperator
    TraverseEntry(const nsAString& aKey, nsIVariant *aDataItem, void* aUserArg);

  nsInterfaceHashtable<nsStringHashKey, nsIVariant> mData;
};

#endif

