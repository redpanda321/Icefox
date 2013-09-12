/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAuth_h__
#define nsAuth_h__

/* types of packages */
enum pType {
     PACKAGE_TYPE_KERBEROS,
     PACKAGE_TYPE_NEGOTIATE,
     PACKAGE_TYPE_NTLM
};

#if defined(MOZ_LOGGING)
#define FORCE_PR_LOG
#endif

#include "prlog.h"

#if defined( PR_LOGGING )
//
// in order to do logging, the following environment variables need to be set:
// 
//      set NSPR_LOG_MODULES=negotiateauth:4
//      set NSPR_LOG_FILE=negotiateauth.log
//
extern PRLogModuleInfo* gNegotiateLog;

#define LOG(args) PR_LOG(gNegotiateLog, PR_LOG_DEBUG, args)
#else
#define LOG(args)
#endif

#endif /* !defined( nsAuth_h__ ) */
