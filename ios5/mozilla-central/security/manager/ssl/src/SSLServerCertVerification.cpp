/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* 
 * For connections that are not processed on the socket transport thread, we do
 * NOT use the async logic described below. Instead, we authenticate the
 * certificate on the thread that the connection's I/O happens on,
 * synchronously. This allows us to do certificate verification for blocking
 * (not non-blocking) sockets and sockets that have their I/O processed on a
 * thread other than the socket transport service thread. Also, we DO NOT
 * support blocking sockets on the socket transport service thread at all.
 *
 * During certificate authentication, we call CERT_PKIXVerifyCert or
 * CERT_VerifyCert. These functions may make zero or more HTTP requests
 * for OCSP responses, CRLs, intermediate certificates, etc. Our fetching logic
 * for these requests processes them on the socket transport service thread.
 *
 * If the connection for which we are verifying the certificate is happening
 * on the socket transport thread (the usually case, at least for HTTP), then
 * if our cert auth hook were to call the CERT_*Verify* functions directly,
 * there would be a deadlock: The CERT_*Verify* function would cause an event
 * to be asynchronously posted to the socket transport thread, and then it
 * would block the socket transport thread waiting to be notified of the HTTP
 * response. However, the HTTP request would never actually be processed
 * because the socket transport thread would be blocked and so it wouldn't be
 * able process HTTP requests. (i.e. Deadlock.)
 *
 * Consequently, when we are asked to verify a certificate on the socket
 * transport service thread, we must always call the CERT_*Verify* cert
 * functions on another thread. To accomplish this, our auth cert hook
 * dispatches a SSLServerCertVerificationJob to a pool of background threads,
 * and then immediatley return SECWouldBlock to libssl. These jobs are where
 * the CERT_*Verify* functions are actually called. 
 *
 * When our auth cert hook returns SECWouldBlock, libssl will carry on the
 * handshake while we validate the certificate. This will free up the socket
 * transport thread so that HTTP requests--in particular, the OCSP/CRL/cert
 * requests needed for cert verification as mentioned above--can be processed.
 *
 * Once the CERT_*Verify* function returns, the cert verification job
 * dispatches a SSLServerCertVerificationResult to the socket transport thread;
 * the SSLServerCertVerificationResult will notify libssl that the certificate
 * authentication is complete. Once libssl is notified that the authentication
 * is complete, it will continue the SSL handshake (if it hasn't already
 * finished) and it will begin allowing us to send/receive data on the
 * connection.
 *
 * Timeline of events (for connections managed by the socket transport service):
 *
 *    * libssl calls SSLServerCertVerificationJob::Dispatch on the socket
 *      transport thread.
 *    * SSLServerCertVerificationJob::Dispatch queues a job
 *      (instance of SSLServerCertVerificationJob) to its background thread
 *      pool and returns.
 *    * One of the background threads calls CERT_*Verify*, which may enqueue
 *      some HTTP request(s) onto the socket transport thread, and then
 *      blocks that background thread waiting for the responses and/or timeouts
 *      or errors for those requests.
 *    * Once those HTTP responses have all come back or failed, the
 *      CERT_*Verify* function returns a result indicating that the validation
 *      succeeded or failed.
 *    * If the validation succeeded, then a SSLServerCertVerificationResult
 *      event is posted to the socket transport thread, and the cert
 *      verification thread becomes free to verify other certificates.
 *    * Otherwise, a CertErrorRunnable is posted to the socket transport thread
 *      and then to the main thread (blocking both, see CertErrorRunnable) to
 *      do cert override processing and bad cert listener notification. Then
 *      the cert verification thread becomes free to verify other certificates.
 *    * After processing cert overrides, the CertErrorRunnable will dispatch a
 *      SSLServerCertVerificationResult event to the socket transport thread to
 *      notify it of the result of the override processing; then it returns,
 *      freeing up the main thread.
 *    * The SSLServerCertVerificationResult event will either wake up the 
 *      socket (using SSL_RestartHandshakeAfterServerCert) if validation
 *      succeeded or there was an error override, or it will set an error flag
 *      so that the next I/O operation on the socket will fail, causing the
 *      socket transport thread to close the connection.
 *
 * Cert override processing must happen on the main thread because it accesses
 * the nsICertOverrideService, and that service must be accessed on the main 
 * thread because some extensions (Selenium, in particular) replace it with a
 * Javascript implementation, and chrome JS must always be run on the main
 * thread.
 *
 * SSLServerCertVerificationResult must be dispatched to the socket transport
 * thread because we must only call SSL_* functions on the socket transport
 * thread since they may do I/O, because many parts of nsNSSSocketInfo (the
 * subclass of TransportSecurityInfo used when validating certificates during
 * an SSL handshake) and the PSM NSS I/O layer are not thread-safe, and because
 * we need the event to interrupt the PR_Poll that may waiting for I/O on the
 * socket for which we are validating the cert.
 */

#include "SSLServerCertVerification.h"
#include "nsIBadCertListener2.h"
#include "nsICertOverrideService.h"
#include "nsIStrictTransportSecurityService.h"
#include "nsNSSComponent.h"
#include "nsNSSCleaner.h"
#include "nsRecentBadCerts.h"
#include "nsNSSIOLayer.h"

#include "mozilla/Assertions.h"
#include "nsIThreadPool.h"
#include "nsXPCOMCIDInternal.h"
#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"
#include "PSMRunnable.h"

#include "ssl.h"
#include "secerr.h"
#include "secport.h"
#include "sslerr.h"

#ifdef PR_LOGGING
extern PRLogModuleInfo* gPIPNSSLog;
#endif

namespace mozilla { namespace psm {

namespace {

NS_DEFINE_CID(kNSSComponentCID, NS_NSSCOMPONENT_CID);

NSSCleanupAutoPtrClass(CERTCertificate, CERT_DestroyCertificate)
NSSCleanupAutoPtrClass_WithParam(PRArenaPool, PORT_FreeArena, FalseParam, false)

// do not use a nsCOMPtr to avoid static initializer/destructor
nsIThreadPool * gCertVerificationThreadPool = nsnull;
} // unnamed namespace

// Called when the socket transport thread starts, to initialize the SSL cert
// verification thread pool. By tying the thread pool startup/shutdown directly
// to the STS thread's lifetime, we ensure that they are *always* available for
// SSL connections and that there are no races during startup and especially
// shutdown. (Previously, we have had multiple problems with races in PSM
// background threads, and the race-prevention/shutdown logic used there is
// brittle. Since this service is critical to things like downloading updates,
// we take no chances.) Also, by doing things this way, we avoid the need for
// locks, since gCertVerificationThreadPool is only ever accessed on the socket
// transport thread.
void
InitializeSSLServerCertVerificationThreads()
{
  // TODO: tuning, make parameters preferences
  // XXX: instantiate nsThreadPool directly, to make this more bulletproof.
  // Currently, the nsThreadPool.h header isn't exported for us to do so.
  nsresult rv = CallCreateInstance(NS_THREADPOOL_CONTRACTID,
                                   &gCertVerificationThreadPool);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to create SSL cert verification threads.");
    return;
  }

  (void) gCertVerificationThreadPool->SetIdleThreadLimit(5);
  (void) gCertVerificationThreadPool->SetIdleThreadTimeout(30 * 1000);
  (void) gCertVerificationThreadPool->SetThreadLimit(5);
  (void) gCertVerificationThreadPool->SetName(NS_LITERAL_CSTRING("SSL Cert"));
}

// Called when the socket transport thread finishes, to destroy the thread
// pool. Since the socket transport service has stopped processing events, it
// will not attempt any more SSL I/O operations, so it is clearly safe to shut
// down the SSL cert verification infrastructure. Also, the STS will not
// dispatch many SSL verification result events at this point, so any pending
// cert verifications will (correctly) fail at the point they are dispatched.
//
// The other shutdown race condition that is possible is a race condition with
// shutdown of the nsNSSComponent service. We use the
// nsNSSShutdownPreventionLock where needed (not here) to prevent that.
void StopSSLServerCertVerificationThreads()
{
  if (gCertVerificationThreadPool) {
    gCertVerificationThreadPool->Shutdown();
    NS_RELEASE(gCertVerificationThreadPool);
  }
}

namespace {

// Dispatched to the STS thread to notify the infoObject of the verification
// result.
//
// This will cause the PR_Poll in the STS thread to return, so things work
// correctly even if the STS thread is blocked polling (only) on the file
// descriptor that is waiting for this result.
class SSLServerCertVerificationResult : public nsRunnable
{
public:
  NS_DECL_NSIRUNNABLE

  SSLServerCertVerificationResult(TransportSecurityInfo * infoObject,
                                  PRErrorCode errorCode,
                                  SSLErrorMessageType errorMessageType = 
                                      PlainErrorMessage);

  void Dispatch();
private:
  const nsRefPtr<TransportSecurityInfo> mInfoObject;
public:
  const PRErrorCode mErrorCode;
  const SSLErrorMessageType mErrorMessageType;
};

class CertErrorRunnable : public SyncRunnableBase
{
 public:
  CertErrorRunnable(const void * fdForLogging,
                    nsIX509Cert * cert,
                    TransportSecurityInfo * infoObject,
                    PRErrorCode defaultErrorCodeToReport,
                    PRUint32 collectedErrors,
                    PRErrorCode errorCodeTrust,
                    PRErrorCode errorCodeMismatch,
                    PRErrorCode errorCodeExpired)
    : mFdForLogging(fdForLogging), mCert(cert), mInfoObject(infoObject),
      mDefaultErrorCodeToReport(defaultErrorCodeToReport),
      mCollectedErrors(collectedErrors),
      mErrorCodeTrust(errorCodeTrust),
      mErrorCodeMismatch(errorCodeMismatch),
      mErrorCodeExpired(errorCodeExpired)
  {
  }

  virtual void RunOnTargetThread();
  nsRefPtr<SSLServerCertVerificationResult> mResult; // out
private:
  SSLServerCertVerificationResult* CheckCertOverrides();
  
  const void * const mFdForLogging; // may become an invalid pointer; do not dereference
  const nsCOMPtr<nsIX509Cert> mCert;
  const nsRefPtr<TransportSecurityInfo> mInfoObject;
  const PRErrorCode mDefaultErrorCodeToReport;
  const PRUint32 mCollectedErrors;
  const PRErrorCode mErrorCodeTrust;
  const PRErrorCode mErrorCodeMismatch;
  const PRErrorCode mErrorCodeExpired;
};

SSLServerCertVerificationResult *
CertErrorRunnable::CheckCertOverrides()
{
  PR_LOG(gPIPNSSLog, PR_LOG_DEBUG, ("[%p][%p] top of CheckCertOverrides\n",
                                    mFdForLogging, this));

  if (!NS_IsMainThread()) {
    NS_ERROR("CertErrorRunnable::CheckCertOverrides called off main thread");
    return new SSLServerCertVerificationResult(mInfoObject,
                                               mDefaultErrorCodeToReport);
  }

  PRInt32 port;
  mInfoObject->GetPort(&port);

  nsCString hostWithPortString;
  hostWithPortString.AppendASCII(mInfoObject->GetHostName());
  hostWithPortString.AppendLiteral(":");
  hostWithPortString.AppendInt(port);

  PRUint32 remaining_display_errors = mCollectedErrors;

  nsresult nsrv;

  // Enforce Strict-Transport-Security for hosts that are "STS" hosts:
  // connections must be dropped when there are any certificate errors
  // (STS Spec section 7.3).
  bool strictTransportSecurityEnabled = false;
  nsCOMPtr<nsIStrictTransportSecurityService> stss
    = do_GetService(NS_STSSERVICE_CONTRACTID, &nsrv);
  if (NS_SUCCEEDED(nsrv)) {
    nsrv = stss->IsStsHost(mInfoObject->GetHostName(),
                           &strictTransportSecurityEnabled);
  }
  if (NS_FAILED(nsrv)) {
    return new SSLServerCertVerificationResult(mInfoObject,
                                               mDefaultErrorCodeToReport);
  }

  if (!strictTransportSecurityEnabled) {
    nsCOMPtr<nsICertOverrideService> overrideService =
      do_GetService(NS_CERTOVERRIDE_CONTRACTID);
    // it is fine to continue without the nsICertOverrideService

    PRUint32 overrideBits = 0;

    if (overrideService)
    {
      bool haveOverride;
      bool isTemporaryOverride; // we don't care
      nsCString hostString(mInfoObject->GetHostName());
      nsrv = overrideService->HasMatchingOverride(hostString, port,
                                                  mCert,
                                                  &overrideBits,
                                                  &isTemporaryOverride, 
                                                  &haveOverride);
      if (NS_SUCCEEDED(nsrv) && haveOverride) 
      {
       // remove the errors that are already overriden
        remaining_display_errors -= overrideBits;
      }
    }

    if (!remaining_display_errors) {
      // all errors are covered by override rules, so let's accept the cert
      PR_LOG(gPIPNSSLog, PR_LOG_DEBUG,
             ("[%p][%p] All errors covered by override rules\n",
             mFdForLogging, this));
      return new SSLServerCertVerificationResult(mInfoObject, 0);
    }
  } else {
    PR_LOG(gPIPNSSLog, PR_LOG_DEBUG,
           ("[%p][%p] Strict-Transport-Security is violated: untrusted "
            "transport layer\n", mFdForLogging, this));
  }

  PR_LOG(gPIPNSSLog, PR_LOG_DEBUG,
         ("[%p][%p] Certificate error was not overridden\n",
         mFdForLogging, this));

  // Ok, this is a full stop.
  // First, deliver the technical details of the broken SSL status.

  // Try to get a nsIBadCertListener2 implementation from the socket consumer.
  nsCOMPtr<nsISSLSocketControl> sslSocketControl = do_QueryInterface(
    NS_ISUPPORTS_CAST(nsITransportSecurityInfo*, mInfoObject));
  if (sslSocketControl) {
    nsCOMPtr<nsIInterfaceRequestor> cb;
    sslSocketControl->GetNotificationCallbacks(getter_AddRefs(cb));
    if (cb) {
      nsCOMPtr<nsIBadCertListener2> bcl = do_GetInterface(cb);
      if (bcl) {
        nsIInterfaceRequestor *csi
          = static_cast<nsIInterfaceRequestor*>(mInfoObject);
        bool suppressMessage = false; // obsolete, ignored
        nsrv = bcl->NotifyCertProblem(csi, mInfoObject->SSLStatus(),
                                      hostWithPortString, &suppressMessage);
      }
    }
  }

  nsCOMPtr<nsIRecentBadCertsService> recentBadCertsService = 
    do_GetService(NS_RECENTBADCERTS_CONTRACTID);
 
  if (recentBadCertsService) {
    NS_ConvertUTF8toUTF16 hostWithPortStringUTF16(hostWithPortString);
    recentBadCertsService->AddBadCert(hostWithPortStringUTF16,
                                      mInfoObject->SSLStatus());
  }

  // pick the error code to report by priority
  PRErrorCode errorCodeToReport = mErrorCodeTrust    ? mErrorCodeTrust
                                : mErrorCodeMismatch ? mErrorCodeMismatch
                                : mErrorCodeExpired  ? mErrorCodeExpired
                                : mDefaultErrorCodeToReport;

  return new SSLServerCertVerificationResult(mInfoObject, errorCodeToReport,
                                             OverridableCertErrorMessage);
}

void 
CertErrorRunnable::RunOnTargetThread()
{
  MOZ_ASSERT(NS_IsMainThread());

  mResult = CheckCertOverrides();
  
  MOZ_ASSERT(mResult);
}

// Returns null with the error code (PR_GetError()) set if it does not create
// the CertErrorRunnable.
CertErrorRunnable *
CreateCertErrorRunnable(PRErrorCode defaultErrorCodeToReport,
                        TransportSecurityInfo * infoObject,
                        CERTCertificate * cert,
                        const void * fdForLogging)
{
  MOZ_ASSERT(infoObject);
  MOZ_ASSERT(cert);
  
  // cert was revoked, don't do anything else
  if (defaultErrorCodeToReport == SEC_ERROR_REVOKED_CERTIFICATE) {
    PR_SetError(SEC_ERROR_REVOKED_CERTIFICATE, 0);
    return nsnull;
  }

  if (defaultErrorCodeToReport == 0) {
    NS_ERROR("No error code set during certificate validation failure.");
    PR_SetError(PR_INVALID_STATE_ERROR, 0);
    return nsnull;
  }

  nsRefPtr<nsNSSCertificate> nssCert;
  nssCert = nsNSSCertificate::Create(cert);
  if (!nssCert) {
    NS_ERROR("nsNSSCertificate::Create failed");
    PR_SetError(SEC_ERROR_NO_MEMORY, 0);
    return nsnull;
  }

  SECStatus srv;
  nsresult nsrv;

  nsCOMPtr<nsINSSComponent> inss = do_GetService(kNSSComponentCID, &nsrv);
  if (!inss) {
    NS_ERROR("do_GetService(kNSSComponentCID) failed");
    PR_SetError(defaultErrorCodeToReport, 0);
    return nsnull;
  }

  nsRefPtr<nsCERTValInParamWrapper> survivingParams;
  nsrv = inss->GetDefaultCERTValInParam(survivingParams);
  if (NS_FAILED(nsrv)) {
    NS_ERROR("GetDefaultCERTValInParam failed");
    PR_SetError(defaultErrorCodeToReport, 0);
    return nsnull;
  }
  
  PRArenaPool *log_arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
  PRArenaPoolCleanerFalseParam log_arena_cleaner(log_arena);
  if (!log_arena) {
    NS_ERROR("PORT_NewArena failed");
    return nsnull; // PORT_NewArena set error code
  }

  CERTVerifyLog *verify_log = PORT_ArenaZNew(log_arena, CERTVerifyLog);
  if (!verify_log) {
    NS_ERROR("PORT_ArenaZNew failed");
    return nsnull; // PORT_ArenaZNew set error code
  }
  CERTVerifyLogContentsCleaner verify_log_cleaner(verify_log);
  verify_log->arena = log_arena;

  if (!nsNSSComponent::globalConstFlagUsePKIXVerification) {
    srv = CERT_VerifyCertificate(CERT_GetDefaultCertDB(), cert,
                                true, certificateUsageSSLServer,
                                PR_Now(), static_cast<void*>(infoObject),
                                verify_log, NULL);
  }
  else {
    CERTValOutParam cvout[2];
    cvout[0].type = cert_po_errorLog;
    cvout[0].value.pointer.log = verify_log;
    cvout[1].type = cert_po_end;

    srv = CERT_PKIXVerifyCert(cert, certificateUsageSSLServer,
                              survivingParams->GetRawPointerForNSS(),
                              cvout, static_cast<void*>(infoObject));
  }

  // We ignore the result code of the cert verification.
  // Either it is a failure, which is expected, and we'll process the
  //                         verify log below.
  // Or it is a success, then a domain mismatch is the only 
  //                     possible failure. 

  PRErrorCode errorCodeMismatch = 0;
  PRErrorCode errorCodeTrust = 0;
  PRErrorCode errorCodeExpired = 0;

  PRUint32 collected_errors = 0;

  if (infoObject->IsCertIssuerBlacklisted()) {
    collected_errors |= nsICertOverrideService::ERROR_UNTRUSTED;
    errorCodeTrust = defaultErrorCodeToReport;
  }

  // Check the name field against the desired hostname.
  if (CERT_VerifyCertName(cert, infoObject->GetHostName()) != SECSuccess) {
    collected_errors |= nsICertOverrideService::ERROR_MISMATCH;
    errorCodeMismatch = SSL_ERROR_BAD_CERT_DOMAIN;
  }

  CERTVerifyLogNode *i_node;
  for (i_node = verify_log->head; i_node; i_node = i_node->next)
  {
    switch (i_node->error)
    {
      case SEC_ERROR_UNKNOWN_ISSUER:
      case SEC_ERROR_CA_CERT_INVALID:
      case SEC_ERROR_UNTRUSTED_ISSUER:
      case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
      case SEC_ERROR_UNTRUSTED_CERT:
      case SEC_ERROR_INADEQUATE_KEY_USAGE:
      case SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED:
        // We group all these errors as "cert not trusted"
        collected_errors |= nsICertOverrideService::ERROR_UNTRUSTED;
        if (errorCodeTrust == SECSuccess) {
          errorCodeTrust = i_node->error;
        }
        break;
      case SSL_ERROR_BAD_CERT_DOMAIN:
        collected_errors |= nsICertOverrideService::ERROR_MISMATCH;
        if (errorCodeMismatch == SECSuccess) {
          errorCodeMismatch = i_node->error;
        }
        break;
      case SEC_ERROR_EXPIRED_CERTIFICATE:
        collected_errors |= nsICertOverrideService::ERROR_TIME;
        if (errorCodeExpired == SECSuccess) {
          errorCodeExpired = i_node->error;
        }
        break;
      default:
        PR_SetError(i_node->error, 0);
        return nsnull;
    }
  }

  if (!collected_errors)
  {
    // This will happen when CERT_*Verify* only returned error(s) that are
    // not on our whitelist of overridable certificate errors.
    PR_LOG(gPIPNSSLog, PR_LOG_DEBUG, ("[%p] !collected_errors: %d\n",
           fdForLogging, static_cast<int>(defaultErrorCodeToReport)));
    PR_SetError(defaultErrorCodeToReport, 0);
    return nsnull;
  }

  infoObject->SetStatusErrorBits(*nssCert, collected_errors);

  return new CertErrorRunnable(fdForLogging, 
                               static_cast<nsIX509Cert*>(nssCert.get()),
                               infoObject, defaultErrorCodeToReport, 
                               collected_errors, errorCodeTrust, 
                               errorCodeMismatch, errorCodeExpired);
}

// When doing async cert processing, we dispatch one of these runnables to the
// socket transport service thread, which blocks the socket transport
// service thread while it waits for the inner CertErrorRunnable to execute
// CheckCertOverrides on the main thread. CheckCertOverrides must block events
// on both of these threads because it calls TransportSecurityInfo::GetInterface(), 
// which may call nsHttpConnection::GetInterface() through
// TransportSecurityInfo::mCallbacks. nsHttpConnection::GetInterface must always
// execute on the main thread, with the socket transport service thread
// blocked.
class CertErrorRunnableRunnable : public nsRunnable
{
public:
  CertErrorRunnableRunnable(CertErrorRunnable * certErrorRunnable)
    : mCertErrorRunnable(certErrorRunnable)
  {
  }
private:
  NS_IMETHOD Run()
  {
    nsresult rv = mCertErrorRunnable->DispatchToMainThreadAndWait();
    // The result must run on the socket transport thread, which we are already
    // on, so we can just run it directly, instead of dispatching it.
    if (NS_SUCCEEDED(rv)) {
      rv = mCertErrorRunnable->mResult ? mCertErrorRunnable->mResult->Run()
                                       : NS_ERROR_UNEXPECTED;
    }
    return rv;
  }
  nsRefPtr<CertErrorRunnable> mCertErrorRunnable;
};

class SSLServerCertVerificationJob : public nsRunnable
{
public:
  // Must be called only on the socket transport thread
  static SECStatus Dispatch(const void * fdForLogging,
                            TransportSecurityInfo * infoObject,
                            CERTCertificate * serverCert);
private:
  NS_DECL_NSIRUNNABLE

  // Must be called only on the socket transport thread
  SSLServerCertVerificationJob(const void * fdForLogging,
                               TransportSecurityInfo * infoObject, 
                               CERTCertificate * cert);
  ~SSLServerCertVerificationJob();

  const void * const mFdForLogging;
  const nsRefPtr<TransportSecurityInfo> mInfoObject;
  CERTCertificate * const mCert;
};

SSLServerCertVerificationJob::SSLServerCertVerificationJob(
    const void * fdForLogging, TransportSecurityInfo * infoObject,
    CERTCertificate * cert)
  : mFdForLogging(fdForLogging)
  , mInfoObject(infoObject)
  , mCert(CERT_DupCertificate(cert))
{
}

SSLServerCertVerificationJob::~SSLServerCertVerificationJob()
{
  CERT_DestroyCertificate(mCert);
}

SECStatus
PSM_SSL_PKIX_AuthCertificate(CERTCertificate *peerCert, void * pinarg,
                             const char * hostname)
{
    SECStatus          rv;
    
    if (!nsNSSComponent::globalConstFlagUsePKIXVerification) {
        rv = CERT_VerifyCertNow(CERT_GetDefaultCertDB(), peerCert, true,
                                certUsageSSLServer, pinarg);
    }
    else {
        nsresult nsrv;
        nsCOMPtr<nsINSSComponent> inss = do_GetService(kNSSComponentCID, &nsrv);
        if (!inss)
          return SECFailure;
        nsRefPtr<nsCERTValInParamWrapper> survivingParams;
        if (NS_FAILED(inss->GetDefaultCERTValInParam(survivingParams)))
          return SECFailure;

        CERTValOutParam cvout[1];
        cvout[0].type = cert_po_end;

        rv = CERT_PKIXVerifyCert(peerCert, certificateUsageSSLServer,
                                survivingParams->GetRawPointerForNSS(),
                                cvout, pinarg);
    }

    if (rv == SECSuccess) {
        /* cert is OK.  This is the client side of an SSL connection.
        * Now check the name field in the cert against the desired hostname.
        * NB: This is our only defense against Man-In-The-Middle (MITM) attacks!
        */
        if (hostname && hostname[0])
            rv = CERT_VerifyCertName(peerCert, hostname);
        else
            rv = SECFailure;
        if (rv != SECSuccess)
            PORT_SetError(SSL_ERROR_BAD_CERT_DOMAIN);
    }
        
    return rv;
}

struct nsSerialBinaryBlacklistEntry
{
  unsigned int len;
  const char *binary_serial;
};

// bug 642395
static struct nsSerialBinaryBlacklistEntry myUTNBlacklistEntries[] = {
  { 17, "\x00\x92\x39\xd5\x34\x8f\x40\xd1\x69\x5a\x74\x54\x70\xe1\xf2\x3f\x43" },
  { 17, "\x00\xd8\xf3\x5f\x4e\xb7\x87\x2b\x2d\xab\x06\x92\xe3\x15\x38\x2f\xb0" },
  { 16, "\x72\x03\x21\x05\xc5\x0c\x08\x57\x3d\x8e\xa5\x30\x4e\xfe\xe8\xb0" },
  { 17, "\x00\xb0\xb7\x13\x3e\xd0\x96\xf9\xb5\x6f\xae\x91\xc8\x74\xbd\x3a\xc0" },
  { 16, "\x39\x2a\x43\x4f\x0e\x07\xdf\x1f\x8a\xa3\x05\xde\x34\xe0\xc2\x29" },
  { 16, "\x3e\x75\xce\xd4\x6b\x69\x30\x21\x21\x88\x30\xae\x86\xa8\x2a\x71" },
  { 17, "\x00\xe9\x02\x8b\x95\x78\xe4\x15\xdc\x1a\x71\x0a\x2b\x88\x15\x44\x47" },
  { 17, "\x00\xd7\x55\x8f\xda\xf5\xf1\x10\x5b\xb2\x13\x28\x2b\x70\x77\x29\xa3" },
  { 16, "\x04\x7e\xcb\xe9\xfc\xa5\x5f\x7b\xd0\x9e\xae\x36\xe1\x0c\xae\x1e" },
  { 17, "\x00\xf5\xc8\x6a\xf3\x61\x62\xf1\x3a\x64\xf5\x4f\x6d\xc9\x58\x7c\x06" },
  { 0, 0 } // end marker
};

// Call this if we have already decided that a cert should be treated as INVALID,
// in order to check if we to worsen the error to REVOKED.
PRErrorCode
PSM_SSL_DigiNotarTreatAsRevoked(CERTCertificate * serverCert,
                                CERTCertList * serverCertChain)
{
  // If any involved cert was issued by DigiNotar, 
  // and serverCert was issued after 01-JUL-2011,
  // then worsen the error to revoked.
  
  PRTime cutoff = 0;
  PRStatus status = PR_ParseTimeString("01-JUL-2011 00:00", true, &cutoff);
  if (status != PR_SUCCESS) {
    NS_ASSERTION(status == PR_SUCCESS, "PR_ParseTimeString failed");
    // be safe, assume it's afterwards, keep going
  } else {
    PRTime notBefore = 0, notAfter = 0;
    if (CERT_GetCertTimes(serverCert, &notBefore, &notAfter) == SECSuccess &&
           notBefore < cutoff) {
      // no worsening for certs issued before the cutoff date
      return 0;
    }
  }
  
  for (CERTCertListNode *node = CERT_LIST_HEAD(serverCertChain);
       !CERT_LIST_END(node, serverCertChain);
       node = CERT_LIST_NEXT(node)) {
    if (node->cert->issuerName &&
        strstr(node->cert->issuerName, "CN=DigiNotar")) {
      return SEC_ERROR_REVOKED_CERTIFICATE;
    }
  }
  
  return 0;
}

// Call this only if a cert has been reported by NSS as VALID
PRErrorCode
PSM_SSL_BlacklistDigiNotar(CERTCertificate * serverCert,
                           CERTCertList * serverCertChain)
{
  bool isDigiNotarIssuedCert = false;

  for (CERTCertListNode *node = CERT_LIST_HEAD(serverCertChain);
       !CERT_LIST_END(node, serverCertChain);
       node = CERT_LIST_NEXT(node)) {
    if (!node->cert->issuerName)
      continue;

    if (strstr(node->cert->issuerName, "CN=DigiNotar")) {
      isDigiNotarIssuedCert = true;
    }
  }

  if (isDigiNotarIssuedCert) {
    // let's see if we want to worsen the error code to revoked.
    PRErrorCode revoked_code = PSM_SSL_DigiNotarTreatAsRevoked(serverCert, serverCertChain);
    return (revoked_code != 0) ? revoked_code : SEC_ERROR_UNTRUSTED_ISSUER;
  }

  return 0;
}

// This function assumes that we will only use the SPDY connection coalescing
// feature on connections where we have negotiated SPDY using NPN. If we ever
// talk SPDY without having negotiated it with SPDY, this code will give wrong
// and perhaps unsafe results.
//
// Returns SECSuccess on the initial handshake of all connections, on
// renegotiations for any connections where we did not negotiate SPDY, or on any
// SPDY connection where the server's certificate did not change.
//
// Prohibit changing the server cert only if we negotiated SPDY,
// in order to support SPDY's cross-origin connection pooling.

static SECStatus
BlockServerCertChangeForSpdy(nsNSSSocketInfo *infoObject,
                             CERTCertificate *serverCert)
{
  // Get the existing cert. If there isn't one, then there is
  // no cert change to worry about.
  nsCOMPtr<nsIX509Cert> cert;
  nsCOMPtr<nsIX509Cert2> cert2;

  nsRefPtr<nsSSLStatus> status = infoObject->SSLStatus();
  if (!status) {
    // If we didn't have a status, then this is the
    // first handshake on this connection, not a
    // renegotiation.
    return SECSuccess;
  }
  
  status->GetServerCert(getter_AddRefs(cert));
  cert2 = do_QueryInterface(cert);
  if (!cert2) {
    NS_NOTREACHED("every nsSSLStatus must have a cert"
                  "that implements nsIX509Cert2");
    PR_SetError(SEC_ERROR_LIBRARY_FAILURE, 0);
    return SECFailure;
  }

  // Filter out sockets that did not neogtiate SPDY via NPN
  nsCAutoString negotiatedNPN;
  nsresult rv = infoObject->GetNegotiatedNPN(negotiatedNPN);
  NS_ASSERTION(NS_SUCCEEDED(rv),
               "GetNegotiatedNPN() failed during renegotiation");

  if (NS_SUCCEEDED(rv) && !StringBeginsWith(negotiatedNPN,
                                            NS_LITERAL_CSTRING("spdy/")))
    return SECSuccess;

  // If GetNegotiatedNPN() failed we will assume spdy for safety's safe
  if (NS_FAILED(rv))
    PR_LOG(gPIPNSSLog, PR_LOG_DEBUG,
           ("BlockServerCertChangeForSpdy failed GetNegotiatedNPN() call."
            " Assuming spdy.\n"));

  // Check to see if the cert has actually changed
  CERTCertificate * c = cert2->GetCert();
  NS_ASSERTION(c, "very bad and hopefully impossible state");
  bool sameCert = CERT_CompareCerts(c, serverCert);
  CERT_DestroyCertificate(c);
  if (sameCert)
    return SECSuccess;

  // Report an error - changed cert is confirmed
  PR_LOG(gPIPNSSLog, PR_LOG_DEBUG,
         ("SPDY Refused to allow new cert during renegotiation\n"));
  PR_SetError(SSL_ERROR_RENEGOTIATION_NOT_ALLOWED, 0);
  return SECFailure;
}

SECStatus
AuthCertificate(TransportSecurityInfo * infoObject, CERTCertificate * cert)
{
  if (cert->serialNumber.data &&
      cert->issuerName &&
      !strcmp(cert->issuerName, 
        "CN=UTN-USERFirst-Hardware,OU=http://www.usertrust.com,O=The USERTRUST Network,L=Salt Lake City,ST=UT,C=US")) {

    unsigned char *server_cert_comparison_start = cert->serialNumber.data;
    unsigned int server_cert_comparison_len = cert->serialNumber.len;

    while (server_cert_comparison_len) {
      if (*server_cert_comparison_start != 0)
        break;

      ++server_cert_comparison_start;
      --server_cert_comparison_len;
    }

    nsSerialBinaryBlacklistEntry *walk = myUTNBlacklistEntries;
    for ( ; walk && walk->len; ++walk) {

      unsigned char *locked_cert_comparison_start = (unsigned char*)walk->binary_serial;
      unsigned int locked_cert_comparison_len = walk->len;
      
      while (locked_cert_comparison_len) {
        if (*locked_cert_comparison_start != 0)
          break;
        
        ++locked_cert_comparison_start;
        --locked_cert_comparison_len;
      }

      if (server_cert_comparison_len == locked_cert_comparison_len &&
          !memcmp(server_cert_comparison_start, locked_cert_comparison_start, locked_cert_comparison_len)) {
        PR_SetError(SEC_ERROR_REVOKED_CERTIFICATE, 0);
        return SECFailure;
      }
    }
  }

  SECStatus rv = PSM_SSL_PKIX_AuthCertificate(cert, infoObject,
                                              infoObject->GetHostName());

  // We want to remember the CA certs in the temp db, so that the application can find the
  // complete chain at any time it might need it.
  // But we keep only those CA certs in the temp db, that we didn't already know.

  nsRefPtr<nsSSLStatus> status = infoObject->SSLStatus();
  nsRefPtr<nsNSSCertificate> nsc;

  if (!status || !status->mServerCert) {
    nsc = nsNSSCertificate::Create(cert);
  }

  CERTCertList *certList = nsnull;
  certList = CERT_GetCertChainFromCert(cert, PR_Now(), certUsageSSLCA);
  if (!certList) {
    rv = SECFailure;
  } else {
    PRErrorCode blacklistErrorCode;
    if (rv == SECSuccess) { // PSM_SSL_PKIX_AuthCertificate said "valid cert"
      blacklistErrorCode = PSM_SSL_BlacklistDigiNotar(cert, certList);
    } else { // PSM_SSL_PKIX_AuthCertificate said "invalid cert"
      PRErrorCode savedErrorCode = PORT_GetError();
      // Check if we want to worsen the error code to "revoked".
      blacklistErrorCode = PSM_SSL_DigiNotarTreatAsRevoked(cert, certList);
      if (blacklistErrorCode == 0) {
        // we don't worsen the code, let's keep the original error code from NSS
        PORT_SetError(savedErrorCode);
      }
    }
      
    if (blacklistErrorCode != 0) {
      infoObject->SetCertIssuerBlacklisted();
      PORT_SetError(blacklistErrorCode);
      rv = SECFailure;
    }
  }

  if (rv == SECSuccess) {
    if (nsc) {
      bool dummyIsEV;
      nsc->GetIsExtendedValidation(&dummyIsEV); // the nsc object will cache the status
    }
    
    nsCOMPtr<nsINSSComponent> nssComponent;
      
    for (CERTCertListNode *node = CERT_LIST_HEAD(certList);
         !CERT_LIST_END(node, certList);
         node = CERT_LIST_NEXT(node)) {

      if (node->cert->slot) {
        // This cert was found on a token, no need to remember it in the temp db.
        continue;
      }

      if (node->cert->isperm) {
        // We don't need to remember certs already stored in perm db.
        continue;
      }
        
      if (node->cert == cert) {
        // We don't want to remember the server cert, 
        // the code that cares for displaying page info does this already.
        continue;
      }

      // We have found a signer cert that we want to remember.
      char* nickname = nsNSSCertificate::defaultServerNickname(node->cert);
      if (nickname && *nickname) {
        PK11SlotInfo *slot = PK11_GetInternalKeySlot();
        if (slot) {
          PK11_ImportCert(slot, node->cert, CK_INVALID_HANDLE, 
                          nickname, false);
          PK11_FreeSlot(slot);
        }
      }
      PR_FREEIF(nickname);
    }

    if (certList) {
      CERT_DestroyCertList(certList);
    }

    // The connection may get terminated, for example, if the server requires
    // a client cert. Let's provide a minimal SSLStatus
    // to the caller that contains at least the cert and its status.
    if (!status) {
      status = new nsSSLStatus();
      infoObject->SetSSLStatus(status);
    }

    if (rv == SECSuccess) {
      // Certificate verification succeeded delete any potential record
      // of certificate error bits.
      RememberCertErrorsTable::GetInstance().RememberCertHasError(infoObject,
                                                                  nsnull, rv);
    }
    else {
      // Certificate verification failed, update the status' bits.
      RememberCertErrorsTable::GetInstance().LookupCertErrorBits(
        infoObject, status);
    }

    if (status && !status->mServerCert) {
      status->mServerCert = nsc;
      PR_LOG(gPIPNSSLog, PR_LOG_DEBUG,
             ("AuthCertificate setting NEW cert %p\n", status->mServerCert.get()));
    }
  }

  return rv;
}

/*static*/ SECStatus
SSLServerCertVerificationJob::Dispatch(const void * fdForLogging,
                                       TransportSecurityInfo * infoObject,
                                       CERTCertificate * serverCert)
{
  // Runs on the socket transport thread
  if (!infoObject || !serverCert) {
    NS_ERROR("Invalid parameters for SSL server cert validation");
    PR_SetError(PR_INVALID_ARGUMENT_ERROR, 0);
    return SECFailure;
  }
  
  nsRefPtr<SSLServerCertVerificationJob> job
    = new SSLServerCertVerificationJob(fdForLogging, infoObject, serverCert);

  nsresult nrv;
  if (!gCertVerificationThreadPool) {
    nrv = NS_ERROR_NOT_INITIALIZED;
  } else {
    nrv = gCertVerificationThreadPool->Dispatch(job, NS_DISPATCH_NORMAL);
  }
  if (NS_FAILED(nrv)) {
    // We can't call SetCertVerificationResult here to change
    // mCertVerificationState because SetCertVerificationResult will call
    // libssl functions that acquire SSL locks that are already being held at
    // this point. infoObject->mCertVerificationState will be stuck at
    // waiting_for_cert_verification here, but that is OK because we already
    // have to be able to handle cases where we encounter non-cert errors while
    // in that state.
    PRErrorCode error = nrv == NS_ERROR_OUT_OF_MEMORY
                      ? SEC_ERROR_NO_MEMORY
                      : PR_INVALID_STATE_ERROR;
    PORT_SetError(error);
    return SECFailure;
  }

  PORT_SetError(PR_WOULD_BLOCK_ERROR);
  return SECWouldBlock;    
}

NS_IMETHODIMP
SSLServerCertVerificationJob::Run()
{
  // Runs on a cert verification thread

  PR_LOG(gPIPNSSLog, PR_LOG_DEBUG,
          ("[%p] SSLServerCertVerificationJob::Run\n", mInfoObject.get()));

  PRErrorCode error;

  nsNSSShutDownPreventionLock nssShutdownPrevention;
  if (mInfoObject->isAlreadyShutDown()) {
    error = SEC_ERROR_USER_CANCELLED;
  } else {
    // Reset the error code here so we can detect if AuthCertificate fails to
    // set the error code if/when it fails.
    PR_SetError(0, 0); 
    SECStatus rv = AuthCertificate(mInfoObject, mCert);
    if (rv == SECSuccess) {
      nsRefPtr<SSLServerCertVerificationResult> restart 
        = new SSLServerCertVerificationResult(mInfoObject, 0);
      restart->Dispatch();
      return NS_OK;
    }

    error = PR_GetError();
    if (error != 0) {
      nsRefPtr<CertErrorRunnable> runnable = CreateCertErrorRunnable(
              error, mInfoObject, mCert, mFdForLogging);
      if (!runnable) {
        // CreateCertErrorRunnable set a new error code
        error = PR_GetError(); 
      } else {
        // We must block the the socket transport service thread while the
        // main thread executes the CertErrorRunnable. The CertErrorRunnable
        // will dispatch the result asynchronously, so we don't have to block
        // this thread waiting for it.

        PR_LOG(gPIPNSSLog, PR_LOG_DEBUG,
                ("[%p][%p] Before dispatching CertErrorRunnable\n",
                mFdForLogging, runnable.get()));

        nsresult nrv;
        nsCOMPtr<nsIEventTarget> stsTarget
          = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &nrv);
        if (NS_SUCCEEDED(nrv)) {
          nrv = stsTarget->Dispatch(new CertErrorRunnableRunnable(runnable),
                                    NS_DISPATCH_NORMAL);
        }
        if (NS_SUCCEEDED(nrv)) {
          return NS_OK;
        }

        NS_ERROR("Failed to dispatch CertErrorRunnable");
        error = PR_INVALID_STATE_ERROR;
      }
    }
  }

  if (error == 0) {
    NS_NOTREACHED("no error set during certificate validation failure");
    error = PR_INVALID_STATE_ERROR;
  }

  nsRefPtr<SSLServerCertVerificationResult> failure
    = new SSLServerCertVerificationResult(mInfoObject, error);
  failure->Dispatch();
  return NS_OK;
}

} // unnamed namespace

// Extracts whatever information we need out of fd (using SSL_*) and passes it
// to SSLServerCertVerificationJob::Dispatch. SSLServerCertVerificationJob should
// never do anything with fd except logging.
SECStatus
AuthCertificateHook(void *arg, PRFileDesc *fd, PRBool checkSig, PRBool isServer)
{
  // Runs on the socket transport thread

  PR_LOG(gPIPNSSLog, PR_LOG_DEBUG,
         ("[%p] starting AuthCertificateHook\n", fd));

  // Modern libssl always passes PR_TRUE for checkSig, and we have no means of
  // doing verification without checking signatures.
  NS_ASSERTION(checkSig, "AuthCertificateHook: checkSig unexpectedly false");

  // PSM never causes libssl to call this function with PR_TRUE for isServer,
  // and many things in PSM assume that we are a client.
  NS_ASSERTION(!isServer, "AuthCertificateHook: isServer unexpectedly true");

  nsNSSSocketInfo *socketInfo = static_cast<nsNSSSocketInfo*>(arg);
  
  if (socketInfo) {
    // This is the first callback during full handshakes.
    socketInfo->SetFirstServerHelloReceived();
  }

  CERTCertificate *serverCert = SSL_PeerCertificate(fd);
  CERTCertificateCleaner serverCertCleaner(serverCert);

  if (!checkSig || isServer || !socketInfo || !serverCert) {
      PR_SetError(PR_INVALID_STATE_ERROR, 0);
      return SECFailure;
  }
      
  if (BlockServerCertChangeForSpdy(socketInfo, serverCert) != SECSuccess)
    return SECFailure;

  bool onSTSThread;
  nsresult nrv;
  nsCOMPtr<nsIEventTarget> sts
    = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &nrv);
  if (NS_SUCCEEDED(nrv)) {
    nrv = sts->IsOnCurrentThread(&onSTSThread);
  }

  if (NS_FAILED(nrv)) {
    NS_ERROR("Could not get STS service or IsOnCurrentThread failed");
    PR_SetError(PR_UNKNOWN_ERROR, 0);
    return SECFailure;
  }
  
  if (onSTSThread) {
    // We *must* do certificate verification on a background thread because
    // we need the socket transport thread to be free for our OCSP requests,
    // and we *want* to do certificate verification on a background thread
    // because of the performance benefits of doing so.
    socketInfo->SetCertVerificationWaiting();
    SECStatus rv = SSLServerCertVerificationJob::Dispatch(
                        static_cast<const void *>(fd), socketInfo, serverCert);
    return rv;
  }
  
  // We can't do certificate verification on a background thread, because the
  // thread doing the network I/O may not interrupt its network I/O on receipt
  // of our SSLServerCertVerificationResult event, and/or it might not even be
  // a non-blocking socket.
  SECStatus rv = AuthCertificate(socketInfo, serverCert);
  if (rv == SECSuccess) {
    return SECSuccess;
  }

  PRErrorCode error = PR_GetError();
  if (error != 0) {
    nsRefPtr<CertErrorRunnable> runnable = CreateCertErrorRunnable(
                    error, socketInfo, serverCert,
                    static_cast<const void *>(fd));
    if (!runnable) {
      // CreateCertErrorRunnable sets a new error code when it fails
      error = PR_GetError();
    } else {
      // We have to return SECSuccess or SECFailure based on the result of the
      // override processing, so we must block this thread waiting for it. The
      // CertErrorRunnable will NOT dispatch the result at all, since we passed
      // false for CreateCertErrorRunnable's async parameter
      nrv = runnable->DispatchToMainThreadAndWait();
      if (NS_FAILED(nrv)) {
        NS_ERROR("Failed to dispatch CertErrorRunnable");
        PR_SetError(PR_INVALID_STATE_ERROR, 0);
        return SECFailure;
      }

      if (!runnable->mResult) {
        NS_ERROR("CertErrorRunnable did not set result");
        PR_SetError(PR_INVALID_STATE_ERROR, 0);
        return SECFailure;
      }

      if (runnable->mResult->mErrorCode == 0) {
        return SECSuccess; // cert error override occurred.
      }

      // We must call SetCanceled here to set the error message type
      // in case it isn't PlainErrorMessage, which is what we would
      // default to if we just called
      // PR_SetError(runnable->mResult->mErrorCode, 0) and returned
      // SECFailure without doing this.
      socketInfo->SetCanceled(runnable->mResult->mErrorCode,
                              runnable->mResult->mErrorMessageType);
      error = runnable->mResult->mErrorCode;
    }
  }

  if (error == 0) {
    NS_ERROR("error code not set");
    error = PR_UNKNOWN_ERROR;
  }

  PR_SetError(error, 0);
  return SECFailure;
}

SSLServerCertVerificationResult::SSLServerCertVerificationResult(
        TransportSecurityInfo * infoObject, PRErrorCode errorCode,
        SSLErrorMessageType errorMessageType)
  : mInfoObject(infoObject)
  , mErrorCode(errorCode)
  , mErrorMessageType(errorMessageType)
{
}

void
SSLServerCertVerificationResult::Dispatch()
{
  nsresult rv;
  nsCOMPtr<nsIEventTarget> stsTarget
    = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  NS_ASSERTION(stsTarget,
               "Failed to get socket transport service event target");
  rv = stsTarget->Dispatch(this, NS_DISPATCH_NORMAL);
  NS_ASSERTION(NS_SUCCEEDED(rv), 
               "Failed to dispatch SSLServerCertVerificationResult");
}

NS_IMETHODIMP
SSLServerCertVerificationResult::Run()
{
  // TODO: Assert that we're on the socket transport thread
  // XXX: This cast will be removed by the next patch
  ((nsNSSSocketInfo *) mInfoObject.get())
    ->SetCertVerificationResult(mErrorCode, mErrorMessageType);
  return NS_OK;
}

} } // namespace mozilla::psm
