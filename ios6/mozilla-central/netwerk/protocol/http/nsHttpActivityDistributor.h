/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHttpActivityDistributor_h__
#define nsHttpActivityDistributor_h__

#include "nsIHttpActivityObserver.h"
#include "nsCOMArray.h"
#include "mozilla/Mutex.h"

class nsHttpActivityDistributor : public nsIHttpActivityDistributor
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIHTTPACTIVITYOBSERVER
    NS_DECL_NSIHTTPACTIVITYDISTRIBUTOR

    nsHttpActivityDistributor();
    virtual ~nsHttpActivityDistributor();

protected:
    nsCOMArray<nsIHttpActivityObserver> mObservers;
    mozilla::Mutex mLock;
};

#endif // nsHttpActivityDistributor_h__
