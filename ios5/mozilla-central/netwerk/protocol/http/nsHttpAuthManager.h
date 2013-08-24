/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHttpAuthManager_h__
#define nsHttpAuthManager_h__

#include "nsHttpAuthCache.h"
#include "nsIHttpAuthManager.h"

class nsHttpAuthManager : public nsIHttpAuthManager
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIHTTPAUTHMANAGER

  nsHttpAuthManager();
  virtual ~nsHttpAuthManager();
  nsresult Init();

protected:
  nsHttpAuthCache *mAuthCache;
};

#endif // nsHttpAuthManager_h__
