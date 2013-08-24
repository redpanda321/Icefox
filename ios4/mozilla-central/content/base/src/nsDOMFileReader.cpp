/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozila.org code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsDOMFileReader.h"

#include "nsContentCID.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsDOMFile.h"
#include "nsDOMError.h"
#include "nsICharsetAlias.h"
#include "nsICharsetDetector.h"
#include "nsICharsetConverterManager.h"
#include "nsIConverterInputStream.h"
#include "nsIFile.h"
#include "nsIFileStreams.h"
#include "nsIInputStream.h"
#include "nsIMIMEService.h"
#include "nsIPlatformCharset.h"
#include "nsIUnicharInputStream.h"
#include "nsIUnicodeDecoder.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"

#include "plbase64.h"
#include "prmem.h"

#include "nsLayoutCID.h"
#include "nsXPIDLString.h"
#include "nsReadableUtils.h"
#include "nsIURI.h"
#include "nsStreamUtils.h"
#include "nsXPCOM.h"
#include "nsIPrivateDOMEvent.h"
#include "nsIDOMEventListener.h"
#include "nsIJSContextStack.h"
#include "nsJSEnvironment.h"
#include "nsIScriptGlobalObject.h"
#include "nsIDOMClassInfo.h"
#include "nsIDOMFileInternal.h"
#include "nsCExternalHandlerService.h"
#include "nsIStreamConverterService.h"
#include "nsEventDispatcher.h"
#include "nsCycleCollectionParticipant.h"
#include "nsLayoutStatics.h"
#include "nsIScriptObjectPrincipal.h"

#define LOAD_STR "load"
#define ERROR_STR "error"
#define ABORT_STR "abort"
#define LOADSTART_STR "loadstart"
#define PROGRESS_STR "progress"
#define UPLOADPROGRESS_STR "uploadprogress"
#define LOADEND_STR "loadend"

#define NS_PROGRESS_EVENT_INTERVAL 50

NS_IMPL_CYCLE_COLLECTION_CLASS(nsDOMFileReader)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsDOMFileReader,
                                                  nsXHREventTarget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mOnLoadEndListener)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mFile)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mProgressNotifier)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mPrincipal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mChannel)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsDOMFileReader,
                                                nsXHREventTarget)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mOnLoadEndListener)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mFile)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mProgressNotifier)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mPrincipal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mChannel)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

DOMCI_DATA(FileReader, nsDOMFileReader)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(nsDOMFileReader)
  NS_INTERFACE_MAP_ENTRY(nsIDOMFileReader)
  NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIJSNativeInitializer)
  NS_INTERFACE_MAP_ENTRY(nsITimerCallback)
  NS_INTERFACE_MAP_ENTRY(nsICharsetDetectionObserver)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(FileReader)
NS_INTERFACE_MAP_END_INHERITING(nsXHREventTarget)

NS_IMPL_ADDREF_INHERITED(nsDOMFileReader, nsXHREventTarget)
NS_IMPL_RELEASE_INHERITED(nsDOMFileReader, nsXHREventTarget)

NS_IMETHODIMP
nsDOMFileReader::GetOnloadend(nsIDOMEventListener** aOnloadend)
{
  return GetInnerEventListener(mOnLoadEndListener, aOnloadend);
}

NS_IMETHODIMP
nsDOMFileReader::SetOnloadend(nsIDOMEventListener* aOnloadend)
{
  return RemoveAddEventListener(NS_LITERAL_STRING(LOADEND_STR),
                                mOnLoadEndListener, aOnloadend);
}

//nsICharsetDetectionObserver

NS_IMETHODIMP
nsDOMFileReader::Notify(const char *aCharset, nsDetectionConfident aConf)
{
  CopyASCIItoUTF16(aCharset, mCharset);
  return NS_OK;
}

//nsDOMFileReader constructors/initializers

nsDOMFileReader::nsDOMFileReader()
  : mFileData(nsnull),
    mDataLen(0), mDataFormat(FILE_AS_BINARY),
    mReadyState(nsIDOMFileReader::EMPTY),
    mProgressEventWasDelayed(PR_FALSE),
    mTimerIsActive(PR_FALSE),
    mReadTotal(0), mReadTransferred(0)
{
  nsLayoutStatics::AddRef();
}

nsDOMFileReader::~nsDOMFileReader()
{
  if (mListenerManager) 
    mListenerManager->Disconnect();

  FreeFileData();

  nsLayoutStatics::Release();
}

nsresult
nsDOMFileReader::Init()
{
  // Set the original mScriptContext and mPrincipal, if available.
  // Get JSContext from stack.
  nsCOMPtr<nsIJSContextStack> stack =
    do_GetService("@mozilla.org/js/xpc/ContextStack;1");

  if (!stack) {
    return NS_OK;
  }

  JSContext *cx;

  if (NS_FAILED(stack->Peek(&cx)) || !cx) {
    return NS_OK;
  }

  nsIScriptSecurityManager *secMan = nsContentUtils::GetSecurityManager();
  nsCOMPtr<nsIPrincipal> subjectPrincipal;
  if (secMan) {
    secMan->GetSubjectPrincipal(getter_AddRefs(subjectPrincipal));
  }
  NS_ENSURE_STATE(subjectPrincipal);
  mPrincipal = subjectPrincipal;

  nsIScriptContext* context = GetScriptContextFromJSContext(cx);
  if (context) {
    mScriptContext = context;
    nsCOMPtr<nsPIDOMWindow> window =
      do_QueryInterface(context->GetGlobalObject());
    if (window) {
      mOwner = window->GetCurrentInnerWindow();
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMFileReader::Initialize(nsISupports* aOwner, JSContext* cx, JSObject* obj,
                            PRUint32 argc, jsval *argv)
{
  mOwner = do_QueryInterface(aOwner);
  if (!mOwner) {
    NS_WARNING("Unexpected nsIJSNativeInitializer owner");
    return NS_OK;
  }

  // This object is bound to a |window|,
  // so reset the principal and script context.
  nsCOMPtr<nsIScriptObjectPrincipal> scriptPrincipal = do_QueryInterface(aOwner);
  NS_ENSURE_STATE(scriptPrincipal);
  mPrincipal = scriptPrincipal->GetPrincipal();
  nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(aOwner);
  NS_ENSURE_STATE(sgo);
  mScriptContext = sgo->GetContext();
  NS_ENSURE_STATE(mScriptContext);

  return NS_OK; 
}

// nsIInterfaceRequestor

NS_IMETHODIMP
nsDOMFileReader::GetInterface(const nsIID & aIID, void **aResult)
{
  return QueryInterface(aIID, aResult);
}

// nsIDOMFileReader

NS_IMETHODIMP
nsDOMFileReader::GetReadyState(PRUint16 *aReadyState)
{
  *aReadyState = mReadyState;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMFileReader::GetResult(nsAString& aResult)
{
  aResult = mResult;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMFileReader::GetError(nsIDOMFileError** aError)
{
  NS_IF_ADDREF(*aError = mError);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMFileReader::ReadAsBinaryString(nsIDOMFile* aFile)
{
  return ReadFileContent(aFile, EmptyString(), FILE_AS_BINARY);
}

NS_IMETHODIMP
nsDOMFileReader::ReadAsText(nsIDOMFile* aFile,
                            const nsAString &aCharset)
{
  return ReadFileContent(aFile, aCharset, FILE_AS_TEXT);
}

NS_IMETHODIMP
nsDOMFileReader::ReadAsDataURL(nsIDOMFile* aFile)
{
  return ReadFileContent(aFile, EmptyString(), FILE_AS_DATAURL);
}

NS_IMETHODIMP
nsDOMFileReader::Abort()
{
  if (mReadyState != nsIDOMFileReader::LOADING)
    return NS_OK;

  //Clear progress and file data
  mProgressEventWasDelayed = PR_FALSE;
  mTimerIsActive = PR_FALSE;
  if (mProgressNotifier) {
    mProgressNotifier->Cancel();
  }

  //Revert status, result and readystate attributes
  SetDOMStringToNull(mResult);
  mReadyState = nsIDOMFileReader::DONE;
  mError = new nsDOMFileError(nsIDOMFileError::ABORT_ERR);
    
  //Non-null channel indicates a read is currently active
  if (mChannel) {
    //Cancel request requires an error status
    mChannel->Cancel(NS_ERROR_FAILURE);
    mChannel = nsnull;
  }
  mFile = nsnull;

  //Clean up memory buffer
  FreeFileData();

  //Dispatch the abort event
  DispatchProgressEvent(NS_LITERAL_STRING(ABORT_STR));
  DispatchProgressEvent(NS_LITERAL_STRING(LOADEND_STR));

  mReadyState = nsIDOMFileReader::EMPTY;

  return NS_OK;
}

// nsITimerCallback
NS_IMETHODIMP
nsDOMFileReader::Notify(nsITimer* aTimer)
{
  mTimerIsActive = PR_FALSE;
  if (mProgressEventWasDelayed) {
    DispatchProgressEvent(NS_LITERAL_STRING(PROGRESS_STR));
    StartProgressEventTimer();
  }

  return NS_OK;
}

void
nsDOMFileReader::StartProgressEventTimer()
{
  if (!mProgressNotifier) {
    mProgressNotifier = do_CreateInstance(NS_TIMER_CONTRACTID);
  }
  if (mProgressNotifier) {
    mProgressEventWasDelayed = PR_FALSE;
    mTimerIsActive = PR_TRUE;
    mProgressNotifier->Cancel();
    mProgressNotifier->InitWithCallback(this, NS_PROGRESS_EVENT_INTERVAL,
                                              nsITimer::TYPE_ONE_SHOT);
  }
}

// nsIStreamListener

NS_IMETHODIMP
nsDOMFileReader::OnStartRequest(nsIRequest *request, nsISupports *ctxt)
{
  return NS_OK;
}

static
NS_METHOD
ReadFuncBinaryString(nsIInputStream* in,
                     void* closure,
                     const char* fromRawSegment,
                     PRUint32 toOffset,
                     PRUint32 count,
                     PRUint32 *writeCount)
{
  PRUnichar* dest = static_cast<PRUnichar*>(closure) + toOffset;
  PRUnichar* end = dest + count;
  const unsigned char* source = (const unsigned char*)fromRawSegment;
  while (dest != end) {
    *dest = *source;
    ++dest;
    ++source;
  }
  *writeCount = count;

  return NS_OK;
}

NS_IMETHODIMP
nsDOMFileReader::OnDataAvailable(nsIRequest *aRequest,
                                 nsISupports *aContext,
                                 nsIInputStream *aInputStream,
                                 PRUint32 aOffset,
                                 PRUint32 aCount)
{
  if (mDataFormat == FILE_AS_BINARY) {
    //Continuously update our binary string as data comes in
    NS_ASSERTION(mResult.Length() == aOffset,
                 "unexpected mResult length");
    PRUint32 oldLen = mResult.Length();
    PRUnichar *buf = nsnull;
    mResult.GetMutableData(&buf, oldLen + aCount);
    NS_ENSURE_TRUE(buf, NS_ERROR_OUT_OF_MEMORY);

    PRUint32 bytesRead = 0;
    aInputStream->ReadSegments(ReadFuncBinaryString, buf + oldLen, aCount,
                               &bytesRead);
    NS_ASSERTION(bytesRead == aCount, "failed to read data");
  }
  else {
    //Update memory buffer to reflect the contents of the file
    mFileData = (char *)PR_Realloc(mFileData, aOffset + aCount);
    NS_ENSURE_TRUE(mFileData, NS_ERROR_OUT_OF_MEMORY);

    PRUint32 bytesRead = 0;
    aInputStream->Read(mFileData + aOffset, aCount, &bytesRead);
    NS_ASSERTION(bytesRead == aCount, "failed to read data");

    mDataLen += aCount;
  }

  mReadTransferred += aCount;

  //Notify the timer is the appropriate timeframe has passed
  if (mTimerIsActive) {
    mProgressEventWasDelayed = PR_TRUE;
  }
  else {
    DispatchProgressEvent(NS_LITERAL_STRING(PROGRESS_STR));
    StartProgressEventTimer();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMFileReader::OnStopRequest(nsIRequest *aRequest,
                               nsISupports *aContext,
                               nsresult aStatus)
{
  //If we're here as a result of a call from Abort(),
  //simply ignore the request.
  if (aRequest != mChannel)
    return NS_OK;

  //Cancel the progress event timer
  mProgressEventWasDelayed = PR_FALSE;
  mTimerIsActive = PR_FALSE;
  if (mProgressNotifier) {
    mProgressNotifier->Cancel();
  }

  //FileReader must be in DONE stage after a load
  mReadyState = nsIDOMFileReader::DONE;

  //Set the status field as appropriate
  if (NS_FAILED(aStatus)) {
    FreeFileData();
    DispatchError(aStatus);
    return NS_OK;
  }

  nsresult rv = NS_OK;
  switch (mDataFormat) {
    case FILE_AS_BINARY:
      break; //Already accumulated mResult
    case FILE_AS_TEXT:
      rv = GetAsText(mCharset, mFileData, mDataLen, mResult);
      break;
    case FILE_AS_DATAURL:
      rv = GetAsDataURL(mFile, mFileData, mDataLen, mResult);
      break;
  }

  FreeFileData();

  if (NS_FAILED(rv)) {
    DispatchError(rv);
    return NS_OK;
  }

  //Dispatch load event to signify end of a successful load
  DispatchProgressEvent(NS_LITERAL_STRING(LOAD_STR));
  DispatchProgressEvent(NS_LITERAL_STRING(LOADEND_STR));

  return NS_OK;
}

// Helper methods

nsresult
nsDOMFileReader::ReadFileContent(nsIDOMFile* aFile,
                                 const nsAString &aCharset,
                                 eDataFormat aDataFormat)
{
  NS_ENSURE_TRUE(aFile, NS_ERROR_NULL_POINTER);

  //Implicit abort to clear any other activity going on
  Abort();
  mError = nsnull;
  SetDOMStringToNull(mResult);
  mReadTransferred = 0;
  mReadTotal = 0;
  mReadyState = nsIDOMFileReader::EMPTY;
  FreeFileData();

  mDataFormat = aDataFormat;
  mCharset = aCharset;

  //Obtain the nsDOMFile's underlying nsIFile
  nsresult rv;
  nsCOMPtr<nsIDOMFileInternal> domFile(do_QueryInterface(aFile));
  rv = domFile->GetInternalFile(getter_AddRefs(mFile));
  NS_ENSURE_SUCCESS(rv, rv);

  //Establish a channel with our file
  nsCOMPtr<nsIURI> uri;
  rv = NS_NewFileURI(getter_AddRefs(uri), mFile);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = NS_NewChannel(getter_AddRefs(mChannel), uri);
  NS_ENSURE_SUCCESS(rv, rv);

  //Obtain the total size of the file before reading
  mReadTotal = -1;
  mFile->GetFileSize(&mReadTotal);

  rv = mChannel->AsyncOpen(this, nsnull);
  NS_ENSURE_SUCCESS(rv, rv);

  //FileReader should be in loading state here
  mReadyState = nsIDOMFileReader::LOADING;
  DispatchProgressEvent(NS_LITERAL_STRING(LOADSTART_STR));
 
  return NS_OK;
}

void
nsDOMFileReader::DispatchError(nsresult rv)
{
  //Set the status attribute, and dispatch the error event
  switch (rv) {
    case NS_ERROR_FILE_NOT_FOUND:
      mError = new nsDOMFileError(nsIDOMFileError::NOT_FOUND_ERR);
      break;
    case NS_ERROR_FILE_ACCESS_DENIED:
      mError = new nsDOMFileError(nsIDOMFileError::SECURITY_ERR);
      break;
    default:
      mError = new nsDOMFileError(nsIDOMFileError::NOT_READABLE_ERR);
      break;
  }

  //Dispatch error event to signify load failure
  DispatchProgressEvent(NS_LITERAL_STRING(ERROR_STR));
  DispatchProgressEvent(NS_LITERAL_STRING(LOADEND_STR));
}

void
nsDOMFileReader::DispatchProgressEvent(const nsAString& aType)
{
  nsCOMPtr<nsIDOMEvent> event;
  nsresult rv = nsEventDispatcher::CreateEvent(nsnull, nsnull,
                                               NS_LITERAL_STRING("ProgressEvent"),
                                               getter_AddRefs(event));
  if (NS_FAILED(rv))
    return;

  nsCOMPtr<nsIPrivateDOMEvent> privevent(do_QueryInterface(event));

  if (!privevent)
    return;

  privevent->SetTrusted(PR_TRUE);

  nsCOMPtr<nsIDOMProgressEvent> progress = do_QueryInterface(event);

  if (!progress)
    return;

  progress->InitProgressEvent(aType, PR_FALSE, PR_FALSE, mReadTotal >= 0,
                              mReadTransferred, PR_MAX(mReadTotal, 0));

  this->DispatchDOMEvent(nsnull, event, nsnull, nsnull);
}

nsresult
nsDOMFileReader::GetAsText(const nsAString &aCharset,
                           const char *aFileData,
                           PRUint32 aDataLen,
                           nsAString& aResult)
{
  nsresult rv;
  nsCAutoString charsetGuess;
  if (!aCharset.IsEmpty()) {
    CopyUTF16toUTF8(aCharset, charsetGuess);
  } else {
    rv = GuessCharset(aFileData, aDataLen, charsetGuess);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCAutoString charset;
  nsCOMPtr<nsICharsetAlias> alias = do_GetService(NS_CHARSETALIAS_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = alias->GetPreferred(charsetGuess, charset);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ConvertStream(aFileData, aDataLen, charset.get(), aResult);

  return NS_OK;
}

nsresult
nsDOMFileReader::GetAsDataURL(nsIFile *aFile,
                              const char *aFileData,
                              PRUint32 aDataLen,
                              nsAString& aResult)
{
  aResult.AssignLiteral("data:");

  nsresult rv;
  nsCOMPtr<nsIMIMEService> mimeService =
    do_GetService(NS_MIMESERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString contentType;
  rv = mimeService->GetTypeFromFile(aFile, contentType);
  if (NS_SUCCEEDED(rv)) {
    AppendUTF8toUTF16(contentType, aResult);
  } else {
    aResult.AppendLiteral("application/octet-stream");
  }
  aResult.AppendLiteral(";base64,");

  PRUint32 totalRead = 0;
  do {
    PRUint32 numEncode = 4096;
    PRUint32 amtRemaining = aDataLen - totalRead;
    if (numEncode > amtRemaining)
      numEncode = amtRemaining;

    //Unless this is the end of the file, encode in multiples of 3
    if (numEncode > 3) {
      PRUint32 leftOver = numEncode % 3;
      numEncode -= leftOver;
    }

    //Out buffer should be at least 4/3rds the read buf, plus a terminator
    char *base64 = PL_Base64Encode(aFileData + totalRead, numEncode, nsnull);
    AppendASCIItoUTF16(nsDependentCString(base64), aResult);
    PR_Free(base64);

    totalRead += numEncode;

  } while (aDataLen > totalRead);

  return NS_OK;
}

nsresult
nsDOMFileReader::ConvertStream(const char *aFileData,
                               PRUint32 aDataLen,
                               const char *aCharset,
                               nsAString &aResult)
{
  nsresult rv;
  nsCOMPtr<nsICharsetConverterManager> charsetConverter = 
    do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIUnicodeDecoder> unicodeDecoder;
  rv = charsetConverter->GetUnicodeDecoder(aCharset, getter_AddRefs(unicodeDecoder));
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 destLength;
  rv = unicodeDecoder->GetMaxLength(aFileData, aDataLen, &destLength);
  NS_ENSURE_SUCCESS(rv, rv);

  aResult.SetLength(destLength);  //Make sure we have enough space for the conversion
  destLength = aResult.Length();

  PRInt32 srcLength = aDataLen;
  rv = unicodeDecoder->Convert(aFileData, &srcLength, aResult.BeginWriting(), &destLength);
  aResult.SetLength(destLength); //Trim down to the correct size

  return rv;
}

nsresult
nsDOMFileReader::GuessCharset(const char *aFileData,
                              PRUint32 aDataLen,
                              nsACString &aCharset)
{
  // First try the universal charset detector
  nsCOMPtr<nsICharsetDetector> detector
    = do_CreateInstance(NS_CHARSET_DETECTOR_CONTRACTID_BASE
                        "universal_charset_detector");
  if (!detector) {
    // No universal charset detector, try the default charset detector
    const nsAdoptingString& detectorName =
      nsContentUtils::GetLocalizedStringPref("intl.charset.detector");
    if (!detectorName.IsEmpty()) {
      nsCAutoString detectorContractID;
      detectorContractID.AssignLiteral(NS_CHARSET_DETECTOR_CONTRACTID_BASE);
      AppendUTF16toUTF8(detectorName, detectorContractID);
      detector = do_CreateInstance(detectorContractID.get());
    }
  }

  nsresult rv;
  if (detector) {
    mCharset.Truncate();
    detector->Init(this);

    PRBool done;

    rv = detector->DoIt(aFileData, aDataLen, &done);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = detector->Done();
    NS_ENSURE_SUCCESS(rv, rv);

    CopyUTF16toUTF8(mCharset, aCharset);
  } else {
    // no charset detector available, check the BOM
    unsigned char sniffBuf[4];
    PRUint32 numRead = (aDataLen >= sizeof(sniffBuf) ? sizeof(sniffBuf) : aDataLen);
    memcpy(sniffBuf, aFileData, numRead);

    if (numRead >= 4 &&
        sniffBuf[0] == 0x00 &&
        sniffBuf[1] == 0x00 &&
        sniffBuf[2] == 0xfe &&
        sniffBuf[3] == 0xff) {
      aCharset = "UTF-32BE";
    } else if (numRead >= 4 &&
               sniffBuf[0] == 0xff &&
               sniffBuf[1] == 0xfe &&
               sniffBuf[2] == 0x00 &&
               sniffBuf[3] == 0x00) {
      aCharset = "UTF-32LE";
    } else if (numRead >= 2 &&
               sniffBuf[0] == 0xfe &&
               sniffBuf[1] == 0xff) {
      aCharset = "UTF-16BE";
    } else if (numRead >= 2 &&
               sniffBuf[0] == 0xff &&
               sniffBuf[1] == 0xfe) {
      aCharset = "UTF-16LE";
    } else if (numRead >= 3 &&
               sniffBuf[0] == 0xef &&
               sniffBuf[1] == 0xbb &&
               sniffBuf[2] == 0xbf) {
      aCharset = "UTF-8";
    }
  }

  if (aCharset.IsEmpty()) {
    // no charset detected, default to the system charset
    nsCOMPtr<nsIPlatformCharset> platformCharset =
      do_GetService(NS_PLATFORMCHARSET_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv)) {
      rv = platformCharset->GetCharset(kPlatformCharsetSel_PlainTextInFile,
                                       aCharset);
    }
  }

  if (aCharset.IsEmpty()) {
    // no sniffed or default charset, try UTF-8
    aCharset.AssignLiteral("UTF-8");
  }

  return NS_OK;
}
