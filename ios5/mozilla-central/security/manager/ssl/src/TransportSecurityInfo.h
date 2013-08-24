/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MOZILLA_PSM_TRANSPORTSECURITYINFO_H
#define _MOZILLA_PSM_TRANSPORTSECURITYINFO_H

#include "certt.h"
#include "mozilla/Mutex.h"
#include "nsIInterfaceRequestor.h"
#include "nsITransportSecurityInfo.h"
#include "nsSSLStatus.h"
#include "nsISSLStatusProvider.h"
#include "nsIAssociatedContentSecurity.h"
#include "nsNSSShutDown.h"
#include "nsDataHashtable.h"

namespace mozilla { namespace psm {

enum SSLErrorMessageType {
  OverridableCertErrorMessage  = 1, // for *overridable* certificate errors
  PlainErrorMessage = 2             // all other errors (or "no error")
};

class TransportSecurityInfo : public nsITransportSecurityInfo,
                              public nsIInterfaceRequestor,
                              public nsISSLStatusProvider,
                              public nsIAssociatedContentSecurity,
                              public nsISerializable,
                              public nsIClassInfo,
                              public nsNSSShutDownObject,
                              public nsOnPK11LogoutCancelObject
{
public:
  TransportSecurityInfo();
  virtual ~TransportSecurityInfo();
  
  NS_DECL_ISUPPORTS
  NS_DECL_NSITRANSPORTSECURITYINFO
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSISSLSTATUSPROVIDER
  NS_DECL_NSIASSOCIATEDCONTENTSECURITY
  NS_DECL_NSISERIALIZABLE
  NS_DECL_NSICLASSINFO

  nsresult SetSecurityState(PRUint32 aState);
  nsresult SetShortSecurityDescription(const PRUnichar *aText);

  const char * GetHostName() const {
    return mHostName.get();
  }
  nsresult GetHostName(char **aHostName);
  nsresult SetHostName(const char *aHostName);

  PRInt32 GetPort() const { return mPort; }
  nsresult GetPort(PRInt32 *aPort);
  nsresult SetPort(PRInt32 aPort);

  PRErrorCode GetErrorCode() const;
  void SetCanceled(PRErrorCode errorCode,
                   ::mozilla::psm::SSLErrorMessageType errorMessageType);
  
  /* Set SSL Status values */
  nsresult SetSSLStatus(nsSSLStatus *aSSLStatus);
  nsSSLStatus* SSLStatus() { return mSSLStatus; }
  void SetStatusErrorBits(nsIX509Cert & cert, PRUint32 collected_errors);

  bool IsCertIssuerBlacklisted() const {
    return mIsCertIssuerBlacklisted;
  }
  void SetCertIssuerBlacklisted() {
    mIsCertIssuerBlacklisted = true;
  }

private:
  mutable ::mozilla::Mutex mMutex;

protected:
  nsCOMPtr<nsIInterfaceRequestor> mCallbacks;

private:
  PRUint32 mSecurityState;
  PRInt32 mSubRequestsHighSecurity;
  PRInt32 mSubRequestsLowSecurity;
  PRInt32 mSubRequestsBrokenSecurity;
  PRInt32 mSubRequestsNoSecurity;
  nsString mShortDesc;

  PRErrorCode mErrorCode;
  ::mozilla::psm::SSLErrorMessageType mErrorMessageType;
  nsString mErrorMessageCached;
  nsresult formatErrorMessage(::mozilla::MutexAutoLock const & proofOfLock);

  PRInt32 mPort;
  nsXPIDLCString mHostName;
  PRErrorCode mIsCertIssuerBlacklisted;

  /* SSL Status */
  nsRefPtr<nsSSLStatus> mSSLStatus;

  virtual void virtualDestroyNSSReference();
  void destructorSafeDestroyNSSReference();
};

class RememberCertErrorsTable
{
private:
  RememberCertErrorsTable();

  struct CertStateBits
  {
    bool mIsDomainMismatch;
    bool mIsNotValidAtThisTime;
    bool mIsUntrusted;
  };
  nsDataHashtableMT<nsCStringHashKey, CertStateBits> mErrorHosts;

public:
  void RememberCertHasError(TransportSecurityInfo * infoobject,
                            nsSSLStatus * status,
                            SECStatus certVerificationResult);
  void LookupCertErrorBits(TransportSecurityInfo * infoObject,
                           nsSSLStatus* status);

  static nsresult Init()
  {
    sInstance = new RememberCertErrorsTable();
    if (!sInstance->mErrorHosts.IsInitialized())
      return NS_ERROR_OUT_OF_MEMORY;

    return NS_OK;
  }

  static RememberCertErrorsTable & GetInstance()
  {
    MOZ_ASSERT(sInstance);
    return *sInstance;
  }

  static void Cleanup()
  {
    delete sInstance;
    sInstance = nsnull;
  }
private:
  Mutex mMutex;

  static RememberCertErrorsTable * sInstance;
};

} } // namespace mozilla::psm

// 16786594-0296-4471-8096-8f84497ca428
#define TRANSPORTSECURITYINFO_CID \
{ 0x16786594, 0x0296, 0x4471, \
    { 0x80, 0x96, 0x8f, 0x84, 0x49, 0x7c, 0xa4, 0x28 } }

#endif /* _MOZILLA_PSM_TRANSPORTSECURITYINFO_H */
