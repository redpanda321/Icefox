/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDOMCommandEvent_h__
#define nsDOMCommandEvent_h__

#include "nsIDOMCommandEvent.h"
#include "nsDOMEvent.h"

class nsDOMCommandEvent : public nsDOMEvent,
                          public nsIDOMCommandEvent
{
public:
  nsDOMCommandEvent(nsPresContext* aPresContext,
                    nsCommandEvent* aEvent);
  virtual ~nsDOMCommandEvent();

  NS_DECL_ISUPPORTS_INHERITED

  NS_DECL_NSIDOMCOMMANDEVENT

  // Forward to base class
  NS_FORWARD_TO_NSDOMEVENT
};

#endif // nsDOMCommandEvent_h__
