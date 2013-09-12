/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_sms_SmsCursor_h
#define mozilla_dom_sms_SmsCursor_h

#include "nsIDOMSmsCursor.h"
#include "nsCycleCollectionParticipant.h"
#include "nsCOMPtr.h"
#include "mozilla/Attributes.h"

class nsIDOMMozSmsMessage;
class nsISmsRequest;

namespace mozilla {
namespace dom {
namespace sms {

class SmsCursor MOZ_FINAL : public nsIDOMMozSmsCursor
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIDOMMOZSMSCURSOR

  NS_DECL_CYCLE_COLLECTION_CLASS(SmsCursor)

  SmsCursor();
  SmsCursor(int32_t aListId, nsISmsRequest* aRequest);

  ~SmsCursor();

  void SetMessage(nsIDOMMozSmsMessage* aMessage);

  void Disconnect();

private:
  int32_t                       mListId;
  nsCOMPtr<nsISmsRequest>       mRequest;
  nsCOMPtr<nsIDOMMozSmsMessage> mMessage;
};

inline void
SmsCursor::SetMessage(nsIDOMMozSmsMessage* aMessage)
{
  mMessage = aMessage;
}

} // namespace sms
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_sms_SmsCursor_h
