/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsUsageArrayHelper.h"

#include "nsCOMPtr.h"
#include "nsIDateTimeFormat.h"
#include "nsDateTimeFormatCID.h"
#include "nsComponentManagerUtils.h"
#include "nsReadableUtils.h"
#include "nsNSSCertificate.h"

#include "nspr.h"
#include "secerr.h"

using namespace mozilla;

static NS_DEFINE_CID(kNSSComponentCID, NS_NSSCOMPONENT_CID);

nsUsageArrayHelper::nsUsageArrayHelper(CERTCertificate *aCert)
:mCert(aCert)
{
  nsNSSShutDownPreventionLock locker;
  defaultcertdb = CERT_GetDefaultCertDB();
  nssComponent = do_GetService(kNSSComponentCID, &m_rv);
}

void
nsUsageArrayHelper::check(const char *suffix,
                        SECCertificateUsage aCertUsage,
                        uint32_t &aCounter,
                        PRUnichar **outUsages)
{
  if (!aCertUsage) return;
  nsAutoCString typestr;
  switch (aCertUsage) {
  case certificateUsageSSLClient:
    typestr = "VerifySSLClient";
    break;
  case certificateUsageSSLServer:
    typestr = "VerifySSLServer";
    break;
  case certificateUsageSSLServerWithStepUp:
    typestr = "VerifySSLStepUp";
    break;
  case certificateUsageEmailSigner:
    typestr = "VerifyEmailSigner";
    break;
  case certificateUsageEmailRecipient:
    typestr = "VerifyEmailRecip";
    break;
  case certificateUsageObjectSigner:
    typestr = "VerifyObjSign";
    break;
  case certificateUsageProtectedObjectSigner:
    typestr = "VerifyProtectObjSign";
    break;
  case certificateUsageUserCertImport:
    typestr = "VerifyUserImport";
    break;
  case certificateUsageSSLCA:
    typestr = "VerifySSLCA";
    break;
  case certificateUsageVerifyCA:
    typestr = "VerifyCAVerifier";
    break;
  case certificateUsageStatusResponder:
    typestr = "VerifyStatusResponder";
    break;
  case certificateUsageAnyCA:
    typestr = "VerifyAnyCA";
    break;
  default:
    break;
  }
  if (!typestr.IsEmpty()) {
    typestr.Append(suffix);
    nsAutoString verifyDesc;
    m_rv = nssComponent->GetPIPNSSBundleString(typestr.get(), verifyDesc);
    if (NS_SUCCEEDED(m_rv)) {
      outUsages[aCounter++] = ToNewUnicode(verifyDesc);
    }
  }
}

void
nsUsageArrayHelper::verifyFailed(uint32_t *_verified, int err)
{
  switch (err) {
  /* For these cases, verify only failed for the particular usage */
  case SEC_ERROR_INADEQUATE_KEY_USAGE:
  case SEC_ERROR_INADEQUATE_CERT_TYPE:
    *_verified = nsNSSCertificate::USAGE_NOT_ALLOWED; break;
  /* These are the cases that have individual error messages */
  case SEC_ERROR_REVOKED_CERTIFICATE:
    *_verified = nsNSSCertificate::CERT_REVOKED; break;
  case SEC_ERROR_EXPIRED_CERTIFICATE:
    *_verified = nsNSSCertificate::CERT_EXPIRED; break;
  case SEC_ERROR_UNTRUSTED_CERT:
    *_verified = nsNSSCertificate::CERT_NOT_TRUSTED; break;
  case SEC_ERROR_UNTRUSTED_ISSUER:
    *_verified = nsNSSCertificate::ISSUER_NOT_TRUSTED; break;
  case SEC_ERROR_UNKNOWN_ISSUER:
    *_verified = nsNSSCertificate::ISSUER_UNKNOWN; break;
  case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
    // XXX are there other error for this?
    *_verified = nsNSSCertificate::INVALID_CA; break;
  case SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED:
    *_verified = nsNSSCertificate::SIGNATURE_ALGORITHM_DISABLED; break;
  case SEC_ERROR_CERT_USAGES_INVALID: // XXX what is this?
  // there are some OCSP errors from PSM 1.x to add here
  case SECSuccess:
    // this means, no verification result has ever been received
  default:
    *_verified = nsNSSCertificate::NOT_VERIFIED_UNKNOWN; break;
  }
}

nsresult
nsUsageArrayHelper::GetUsagesArray(const char *suffix,
                      bool localOnly,
                      uint32_t outArraySize,
                      uint32_t *_verified,
                      uint32_t *_count,
                      PRUnichar **outUsages)
{
  nsNSSShutDownPreventionLock locker;
  if (NS_FAILED(m_rv))
    return m_rv;

  if (outArraySize < max_returned_out_array_size)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsINSSComponent> nssComponent;

  if (!nsNSSComponent::globalConstFlagUsePKIXVerification && localOnly) {
    nsresult rv;
    nssComponent = do_GetService(kNSSComponentCID, &rv);
    if (NS_FAILED(rv))
      return rv;
    
    if (nssComponent) {
      nssComponent->SkipOcsp();
    }
  }
  
  uint32_t &count = *_count;
  count = 0;
  SECCertificateUsage usages = 0;
  int err = 0;
  
if (!nsNSSComponent::globalConstFlagUsePKIXVerification) {
  // CERT_VerifyCertificateNow returns SECFailure unless the certificate is
  // valid for all the given usages. Hoewver, we are only looking for the list
  // of usages for which the cert *is* valid.
  (void)
  CERT_VerifyCertificateNow(defaultcertdb, mCert, true,
			    certificateUsageSSLClient |
			    certificateUsageSSLServer |
			    certificateUsageSSLServerWithStepUp |
			    certificateUsageEmailSigner |
			    certificateUsageEmailRecipient |
			    certificateUsageObjectSigner |
			    certificateUsageSSLCA |
			    certificateUsageStatusResponder,
			    nullptr, &usages);
  err = PR_GetError();
}
else {
  nsresult nsrv;
  nsCOMPtr<nsINSSComponent> inss = do_GetService(kNSSComponentCID, &nsrv);
  if (!inss)
    return nsrv;
  RefPtr<nsCERTValInParamWrapper> survivingParams;
  if (localOnly)
    nsrv = inss->GetDefaultCERTValInParamLocalOnly(survivingParams);
  else
    nsrv = inss->GetDefaultCERTValInParam(survivingParams);
  
  if (NS_FAILED(nsrv))
    return nsrv;

  CERTValOutParam cvout[2];
  cvout[0].type = cert_po_usages;
  cvout[0].value.scalar.usages = 0;
  cvout[1].type = cert_po_end;
  
  CERT_PKIXVerifyCert(mCert, certificateUsageCheckAllUsages,
                      survivingParams->GetRawPointerForNSS(),
                      cvout, nullptr);
  err = PR_GetError();
  usages = cvout[0].value.scalar.usages;
}

  // The following list of checks must be < max_returned_out_array_size
  
  check(suffix, usages & certificateUsageSSLClient, count, outUsages);
  check(suffix, usages & certificateUsageSSLServer, count, outUsages);
  check(suffix, usages & certificateUsageSSLServerWithStepUp, count, outUsages);
  check(suffix, usages & certificateUsageEmailSigner, count, outUsages);
  check(suffix, usages & certificateUsageEmailRecipient, count, outUsages);
  check(suffix, usages & certificateUsageObjectSigner, count, outUsages);
#if 0
  check(suffix, usages & certificateUsageProtectedObjectSigner, count, outUsages);
  check(suffix, usages & certificateUsageUserCertImport, count, outUsages);
#endif
  check(suffix, usages & certificateUsageSSLCA, count, outUsages);
#if 0
  check(suffix, usages & certificateUsageVerifyCA, count, outUsages);
#endif
  check(suffix, usages & certificateUsageStatusResponder, count, outUsages);
#if 0
  check(suffix, usages & certificateUsageAnyCA, count, outUsages);
#endif

  if (!nsNSSComponent::globalConstFlagUsePKIXVerification && localOnly && nssComponent) {
    nssComponent->SkipOcspOff();
  }

  if (count == 0) {
    verifyFailed(_verified, err);
  } else {
    *_verified = nsNSSCertificate::VERIFIED_OK;
  }
  return NS_OK;
}
