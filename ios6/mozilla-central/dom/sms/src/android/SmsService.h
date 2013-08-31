/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_sms_SmsService_h
#define mozilla_dom_sms_SmsService_h

#include "nsISmsService.h"

namespace mozilla {
namespace dom {
namespace sms {

class SmsService MOZ_FINAL : public nsISmsService
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISMSSERVICE
};

} // namespace sms
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_sms_SmsService_h
