/* vim: set shiftwidth=2 tabstop=8 autoindent cindent expandtab: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsDOMAnimationEvent_h_
#define nsDOMAnimationEvent_h_

#include "nsDOMEvent.h"
#include "nsIDOMAnimationEvent.h"
#include "nsString.h"

class nsAnimationEvent;

class nsDOMAnimationEvent : public nsDOMEvent,
                            public nsIDOMAnimationEvent
{
public:
  nsDOMAnimationEvent(nsPresContext *aPresContext,
                      nsAnimationEvent *aEvent);
  ~nsDOMAnimationEvent();

  NS_DECL_ISUPPORTS_INHERITED
  NS_FORWARD_TO_NSDOMEVENT
  NS_DECL_NSIDOMANIMATIONEVENT

private:
  nsAnimationEvent* AnimationEvent() {
    NS_ABORT_IF_FALSE(mEvent->eventStructType == NS_ANIMATION_EVENT,
                      "unexpected struct type");
    return static_cast<nsAnimationEvent*>(mEvent);
  }
};

#endif /* !defined(nsDOMAnimationEvent_h_) */
