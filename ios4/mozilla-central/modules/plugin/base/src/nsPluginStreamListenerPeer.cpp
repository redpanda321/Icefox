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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Sean Echevarria <sean@beatnik.com>
 *   Håkan Waara <hwaara@chello.se>
 *   Josh Aas <josh@mozilla.com>
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

#include "nsPluginStreamListenerPeer.h"
#include "nsIStreamConverterService.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIFileChannel.h"
#include "nsICachingChannel.h"
#include "nsMimeTypes.h"
#include "nsISupportsPrimitives.h"
#include "nsNetCID.h"
#include "nsPluginLogging.h"
#include "nsIURI.h"
#include "nsPluginHost.h"
#include "nsIByteRangeRequest.h"
#include "nsIInputStreamTee.h"
#include "nsPrintfCString.h"

#define MAGIC_REQUEST_CONTEXT 0x01020304

// nsPluginByteRangeStreamListener

class nsPluginByteRangeStreamListener : public nsIStreamListener {
public:
  nsPluginByteRangeStreamListener(nsIWeakReference* aWeakPtr);
  virtual ~nsPluginByteRangeStreamListener();
  
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  
private:
  nsCOMPtr<nsIStreamListener> mStreamConverter;
  nsWeakPtr mWeakPtrPluginStreamListenerPeer;
  PRBool mRemoveMagicNumber;
};

NS_IMPL_ISUPPORTS1(nsPluginByteRangeStreamListener, nsIStreamListener)
nsPluginByteRangeStreamListener::nsPluginByteRangeStreamListener(nsIWeakReference* aWeakPtr)
{
  mWeakPtrPluginStreamListenerPeer = aWeakPtr;
  mRemoveMagicNumber = PR_FALSE;
}

nsPluginByteRangeStreamListener::~nsPluginByteRangeStreamListener()
{
  mStreamConverter = 0;
  mWeakPtrPluginStreamListenerPeer = 0;
}

NS_IMETHODIMP
nsPluginByteRangeStreamListener::OnStartRequest(nsIRequest *request, nsISupports *ctxt)
{
  nsresult rv;
  
  nsCOMPtr<nsIStreamListener> finalStreamListener = do_QueryReferent(mWeakPtrPluginStreamListenerPeer);
  if (!finalStreamListener)
    return NS_ERROR_FAILURE;
  
  nsCOMPtr<nsIStreamConverterService> serv = do_GetService(NS_STREAMCONVERTERSERVICE_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = serv->AsyncConvertData(MULTIPART_BYTERANGES,
                                "*/*",
                                finalStreamListener,
                                nsnull,
                                getter_AddRefs(mStreamConverter));
    if (NS_SUCCEEDED(rv)) {
      rv = mStreamConverter->OnStartRequest(request, ctxt);
      if (NS_SUCCEEDED(rv))
        return rv;
    }
  }
  mStreamConverter = 0;
  
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(request));
  if (!httpChannel) {
    return NS_ERROR_FAILURE;
  }
  
  PRUint32 responseCode = 0;
  rv = httpChannel->GetResponseStatus(&responseCode);
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }
  
  // get nsPluginStreamListenerPeer* ptr from finalStreamListener
  nsPluginStreamListenerPeer *pslp =
  reinterpret_cast<nsPluginStreamListenerPeer*>(finalStreamListener.get());
  
  if (responseCode != 200) {
    PRBool bWantsAllNetworkStreams = PR_FALSE;
    pslp->GetPluginInstance()->
    GetValueFromPlugin(NPPVpluginWantsAllNetworkStreams,
                       (void*)&bWantsAllNetworkStreams);
    if (!bWantsAllNetworkStreams){
      return NS_ERROR_FAILURE;
    }
  }
  
  // if server cannot continue with byte range (206 status) and sending us whole object (200 status)
  // reset this seekable stream & try serve it to plugin instance as a file
  mStreamConverter = finalStreamListener;
  mRemoveMagicNumber = PR_TRUE;
  
  rv = pslp->ServeStreamAsFile(request, ctxt);
  return rv;
}

NS_IMETHODIMP
nsPluginByteRangeStreamListener::OnStopRequest(nsIRequest *request, nsISupports *ctxt,
                                               nsresult status)
{
  if (!mStreamConverter)
    return NS_ERROR_FAILURE;
  
  nsCOMPtr<nsIStreamListener> finalStreamListener = do_QueryReferent(mWeakPtrPluginStreamListenerPeer);
  if (!finalStreamListener)
    return NS_ERROR_FAILURE;
  
  if (mRemoveMagicNumber) {
    // remove magic number from container
    nsCOMPtr<nsISupportsPRUint32> container = do_QueryInterface(ctxt);
    if (container) {
      PRUint32 magicNumber = 0;
      container->GetData(&magicNumber);
      if (magicNumber == MAGIC_REQUEST_CONTEXT) {
        // to allow properly finish nsPluginStreamListenerPeer->OnStopRequest()
        // set it to something that is not the magic number.
        container->SetData(0);
      }
    } else {
      NS_WARNING("Bad state of nsPluginByteRangeStreamListener");
    }
  }
  
  return mStreamConverter->OnStopRequest(request, ctxt, status);
}

// CachedFileHolder

CachedFileHolder::CachedFileHolder(nsIFile* cacheFile)
: mFile(cacheFile)
{
  NS_ASSERTION(mFile, "Empty CachedFileHolder");
}

CachedFileHolder::~CachedFileHolder()
{
  mFile->Remove(PR_FALSE);
}

void
CachedFileHolder::AddRef()
{
  ++mRefCnt;
  NS_LOG_ADDREF(this, mRefCnt, "CachedFileHolder", sizeof(*this));
}

void
CachedFileHolder::Release()
{
  --mRefCnt;
  NS_LOG_RELEASE(this, mRefCnt, "CachedFileHolder");
  if (0 == mRefCnt)
    delete this;
}


NS_IMETHODIMP
nsPluginByteRangeStreamListener::OnDataAvailable(nsIRequest *request, nsISupports *ctxt,
                                                 nsIInputStream *inStr, PRUint32 sourceOffset, PRUint32 count)
{
  if (!mStreamConverter)
    return NS_ERROR_FAILURE;
  
  nsCOMPtr<nsIStreamListener> finalStreamListener = do_QueryReferent(mWeakPtrPluginStreamListenerPeer);
  if (!finalStreamListener)
    return NS_ERROR_FAILURE;
  
  return mStreamConverter->OnDataAvailable(request, ctxt, inStr, sourceOffset, count);
}

// nsPRUintKey

class nsPRUintKey : public nsHashKey {
protected:
  PRUint32 mKey;
public:
  nsPRUintKey(PRUint32 key) : mKey(key) {}
  
  PRUint32 HashCode() const {
    return mKey;
  }
  
  PRBool Equals(const nsHashKey *aKey) const {
    return mKey == ((const nsPRUintKey*)aKey)->mKey;
  }
  nsHashKey *Clone() const {
    return new nsPRUintKey(mKey);
  }
  PRUint32 GetValue() { return mKey; }
};

// nsPluginStreamListenerPeer

NS_IMPL_ISUPPORTS6(nsPluginStreamListenerPeer,
                   nsIStreamListener,
                   nsIRequestObserver,
                   nsIHttpHeaderVisitor,
                   nsISupportsWeakReference,
                   nsIPluginStreamInfo,
                   nsINPAPIPluginStreamInfo)

nsPluginStreamListenerPeer::nsPluginStreamListenerPeer()
{
  mStreamType = NP_NORMAL;
  mStartBinding = PR_FALSE;
  mAbort = PR_FALSE;
  mRequestFailed = PR_FALSE;
  
  mPendingRequests = 0;
  mHaveFiredOnStartRequest = PR_FALSE;
  mDataForwardToRequest = nsnull;
  
  mSeekable = PR_FALSE;
  mModified = 0;
  mStreamOffset = 0;
  mStreamComplete = 0;
}

nsPluginStreamListenerPeer::~nsPluginStreamListenerPeer()
{
#ifdef PLUGIN_LOGGING
  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL,
         ("nsPluginStreamListenerPeer::dtor this=%p, url=%s\n",this, mURLSpec.get()));
#endif
  
  // close FD of mFileCacheOutputStream if it's still open
  // or we won't be able to remove the cache file
  if (mFileCacheOutputStream)
    mFileCacheOutputStream = nsnull;
  
  delete mDataForwardToRequest;

  if (mPluginInstance)
    mPluginInstance->BStreamListeners()->RemoveElement(this);
}

// Called as a result of GetURL and PostURL
nsresult nsPluginStreamListenerPeer::Initialize(nsIURI *aURL,
                                                nsNPAPIPluginInstance *aInstance,
                                                nsIPluginStreamListener* aListener,
                                                PRInt32 requestCount)
{
#ifdef PLUGIN_LOGGING
  nsCAutoString urlSpec;
  if (aURL != nsnull) aURL->GetAsciiSpec(urlSpec);
  
  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL,
         ("nsPluginStreamListenerPeer::Initialize instance=%p, url=%s\n", aInstance, urlSpec.get()));
  
  PR_LogFlush();
#endif
  
  mURL = aURL;
  
  mPluginInstance = aInstance;
  mPStreamListener = aListener;
  
  mPendingRequests = requestCount;
  
  mDataForwardToRequest = new nsHashtable(16, PR_FALSE);
  if (!mDataForwardToRequest)
    return NS_ERROR_FAILURE;
  
  return NS_OK;
}

/* Called by NewEmbeddedPluginStream() - if this is called, we weren't
 * able to load the plugin, so we need to load it later once we figure
 * out the mimetype.  In order to load it later, we need the plugin
 * instance owner.
 */
nsresult nsPluginStreamListenerPeer::InitializeEmbedded(nsIURI *aURL,
                                                        nsNPAPIPluginInstance* aInstance,
                                                        nsIPluginInstanceOwner *aOwner)
{
#ifdef PLUGIN_LOGGING
  nsCAutoString urlSpec;
  aURL->GetSpec(urlSpec);
  
  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NORMAL,
         ("nsPluginStreamListenerPeer::InitializeEmbedded url=%s\n", urlSpec.get()));
  
  PR_LogFlush();
#endif
  
  mURL = aURL;
  
  if (aInstance) {
    NS_ASSERTION(mPluginInstance == nsnull, "nsPluginStreamListenerPeer::InitializeEmbedded mPluginInstance != nsnull");
    mPluginInstance = aInstance;
  } else {
    mOwner = aOwner;
  }
  
  mPendingRequests = 1;
  
  mDataForwardToRequest = new nsHashtable(16, PR_FALSE);
  if (!mDataForwardToRequest)
    return NS_ERROR_FAILURE;
  
  return NS_OK;
}

// Called by NewFullPagePluginStream()
nsresult nsPluginStreamListenerPeer::InitializeFullPage(nsIURI* aURL, nsNPAPIPluginInstance *aInstance)
{
  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
             ("nsPluginStreamListenerPeer::InitializeFullPage instance=%p\n",aInstance));
  
  NS_ASSERTION(mPluginInstance == nsnull, "nsPluginStreamListenerPeer::InitializeFullPage mPluginInstance != nsnull");
  mPluginInstance = aInstance;
  
  mURL = aURL;
  
  mDataForwardToRequest = new nsHashtable(16, PR_FALSE);
  if (!mDataForwardToRequest)
    return NS_ERROR_FAILURE;

  mPendingRequests = 1;
  
  return NS_OK;
}

// SetupPluginCacheFile is called if we have to save the stream to disk.
// the most likely cause for this is either there is no disk cache available
// or the stream is coming from a https server.
//
// These files will be deleted when the host is destroyed.
//
// TODO? What if we fill up the the dest dir?
nsresult
nsPluginStreamListenerPeer::SetupPluginCacheFile(nsIChannel* channel)
{
  nsresult rv = NS_OK;
  
  PRBool useExistingCacheFile = PR_FALSE;
  nsRefPtr<nsPluginHost> pluginHost = dont_AddRef(nsPluginHost::GetInst());

  // Look for an existing cache file for the URI.
  nsTArray< nsRefPtr<nsNPAPIPluginInstance> > *instances = pluginHost->InstanceArray();
  for (PRUint32 i = 0; i < instances->Length(); i++) {
    // most recent streams are at the end of list
    nsTArray<nsPluginStreamListenerPeer*> *bStreamListeners = instances->ElementAt(i)->BStreamListeners();
    for (PRInt32 i = bStreamListeners->Length() - 1; i >= 0; --i) {
      nsPluginStreamListenerPeer *lp = bStreamListeners->ElementAt(i);
      if (lp && lp->mLocalCachedFileHolder) {
        useExistingCacheFile = lp->UseExistingPluginCacheFile(this);
        if (useExistingCacheFile) {
          mLocalCachedFileHolder = lp->mLocalCachedFileHolder;
          break;
        }
      }
      if (useExistingCacheFile)
        break;
    }
  }

  // Create a new cache file if one could not be found.
  if (!useExistingCacheFile) {
    nsCOMPtr<nsIFile> pluginTmp;
    rv = nsPluginHost::GetPluginTempDir(getter_AddRefs(pluginTmp));
    if (NS_FAILED(rv)) {
      return rv;
    }
    
    // Get the filename from the channel
    nsCOMPtr<nsIURI> uri;
    rv = channel->GetURI(getter_AddRefs(uri));
    if (NS_FAILED(rv)) return rv;
    
    nsCOMPtr<nsIURL> url(do_QueryInterface(uri));
    if (!url)
      return NS_ERROR_FAILURE;
    
    nsCAutoString filename;
    url->GetFileName(filename);
    if (NS_FAILED(rv))
      return rv;
    
    // Create a file to save our stream into. Should we scramble the name?
    filename.Insert(NS_LITERAL_CSTRING("plugin-"), 0);
    rv = pluginTmp->AppendNative(filename);
    if (NS_FAILED(rv))
      return rv;
    
    // Yes, make it unique.
    rv = pluginTmp->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
    if (NS_FAILED(rv))
      return rv;
    
    // create a file output stream to write to...
    nsCOMPtr<nsIOutputStream> outstream;
    rv = NS_NewLocalFileOutputStream(getter_AddRefs(mFileCacheOutputStream), pluginTmp, -1, 00600);
    if (NS_FAILED(rv))
      return rv;
    
    // save the file.
    mLocalCachedFileHolder = new CachedFileHolder(pluginTmp);
  }

  // add this listenerPeer to list of stream peers for this instance
  mPluginInstance->BStreamListeners()->AppendElement(this);

  return rv;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::OnStartRequest(nsIRequest *request,
                                           nsISupports* aContext)
{
  nsresult  rv = NS_OK;
  
  if (mHaveFiredOnStartRequest) {
    return NS_OK;
  }
  
  mHaveFiredOnStartRequest = PR_TRUE;
  
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  NS_ENSURE_TRUE(channel, NS_ERROR_FAILURE);
  
  // deal with 404 (Not Found) HTTP response,
  // just return, this causes the request to be ignored.
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel));
  if (httpChannel) {
    PRUint32 responseCode = 0;
    rv = httpChannel->GetResponseStatus(&responseCode);
    if (NS_FAILED(rv)) {
      // NPP_Notify() will be called from OnStopRequest
      // in nsNPAPIPluginStreamListener::CleanUpStream
      // return error will cancel this request
      // ...and we also need to tell the plugin that
      mRequestFailed = PR_TRUE;
      return NS_ERROR_FAILURE;
    }
    
    if (responseCode > 206) { // not normal
      PRBool bWantsAllNetworkStreams = PR_FALSE;
      mPluginInstance->GetValueFromPlugin(NPPVpluginWantsAllNetworkStreams,
                                          (void*)&bWantsAllNetworkStreams);
      if (!bWantsAllNetworkStreams) {
        mRequestFailed = PR_TRUE;
        return NS_ERROR_FAILURE;
      }
    }
  }
  
  // do a little sanity check to make sure our frame isn't gone
  // by getting the tag type and checking for an error, we can determine if
  // the frame is gone
  if (mOwner) {
    nsCOMPtr<nsIPluginTagInfo> pti = do_QueryInterface(mOwner);
    NS_ENSURE_TRUE(pti, NS_ERROR_FAILURE);
    nsPluginTagType tagType;
    if (NS_FAILED(pti->GetTagType(&tagType)))
      return NS_ERROR_FAILURE;  // something happened to our object frame, so bail!
  }
  
  // Get the notification callbacks from the channel and save it as
  // week ref we'll use it in nsPluginStreamInfo::RequestRead() when
  // we'll create channel for byte range request.
  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  channel->GetNotificationCallbacks(getter_AddRefs(callbacks));
  if (callbacks)
    mWeakPtrChannelCallbacks = do_GetWeakReference(callbacks);
  
  nsCOMPtr<nsILoadGroup> loadGroup;
  channel->GetLoadGroup(getter_AddRefs(loadGroup));
  if (loadGroup)
    mWeakPtrChannelLoadGroup = do_GetWeakReference(loadGroup);
  
  PRInt64 length;
  rv = channel->GetContentLength(&length);
  
  // it's possible for the server to not send a Content-Length.
  // we should still work in this case.
  if (NS_FAILED(rv) || length == -1) {
    // check out if this is file channel
    nsCOMPtr<nsIFileChannel> fileChannel = do_QueryInterface(channel);
    if (fileChannel) {
      // file does not exist
      mRequestFailed = PR_TRUE;
      return NS_ERROR_FAILURE;
    }
    mLength = 0;
  }
  else {
    mLength = length;
  }
  
  mRequest = request;
  
  nsCAutoString aContentType; // XXX but we already got the type above!
  rv = channel->GetContentType(aContentType);
  if (NS_FAILED(rv))
    return rv;
  
  nsCOMPtr<nsIURI> aURL;
  rv = channel->GetURI(getter_AddRefs(aURL));
  if (NS_FAILED(rv))
    return rv;
  
  aURL->GetSpec(mURLSpec);
  
  if (!aContentType.IsEmpty())
    mContentType = aContentType;
  
#ifdef PLUGIN_LOGGING
  PR_LOG(nsPluginLogging::gPluginLog, PLUGIN_LOG_NOISY,
         ("nsPluginStreamListenerPeer::OnStartRequest this=%p request=%p mime=%s, url=%s\n",
          this, request, aContentType.get(), mURLSpec.get()));
  
  PR_LogFlush();
#endif
  
  NPWindow* window = nsnull;
  
  // if we don't have an nsNPAPIPluginInstance (mPluginInstance), it means
  // we weren't able to load a plugin previously because we
  // didn't have the mimetype.  Now that we do (aContentType),
  // we'll try again with SetUpPluginInstance()
  // which is called by InstantiateEmbeddedPlugin()
  // NOTE: we don't want to try again if we didn't get the MIME type this time
  
  if (!mPluginInstance && mOwner && !aContentType.IsEmpty()) {
    nsCOMPtr<nsIPluginInstance> pluginInstCOMPtr;
    mOwner->GetInstance(getter_AddRefs(pluginInstCOMPtr));
    mPluginInstance = static_cast<nsNPAPIPluginInstance*>(pluginInstCOMPtr.get());

    mOwner->GetWindow(window);
    if (!mPluginInstance && window) {
      nsRefPtr<nsPluginHost> pluginHost = dont_AddRef(nsPluginHost::GetInst());
      
      // determine if we need to try embedded again. FullPage takes a different code path
      PRInt32 mode;
      mOwner->GetMode(&mode);
      if (mode == NP_EMBED)
        rv = pluginHost->InstantiateEmbeddedPlugin(aContentType.get(), aURL, mOwner);
      else
        rv = pluginHost->SetUpPluginInstance(aContentType.get(), aURL, mOwner);
      
      if (NS_OK == rv) {
        mOwner->GetInstance(getter_AddRefs(pluginInstCOMPtr));
        mPluginInstance = static_cast<nsNPAPIPluginInstance*>(pluginInstCOMPtr.get());
        if (mPluginInstance) {
          mPluginInstance->Start();
          mOwner->CreateWidget();
          // If we've got a native window, the let the plugin know about it.
          if (window->window) {
            ((nsPluginNativeWindow*)window)->CallSetWindow(pluginInstCOMPtr);
          }
        }
      }
    }
  }
  
  // Set up the stream listener...
  rv = SetUpStreamListener(request, aURL);
  if (NS_FAILED(rv)) return rv;
  
  return rv;
}

NS_IMETHODIMP nsPluginStreamListenerPeer::OnProgress(nsIRequest *request,
                                                     nsISupports* aContext,
                                                     PRUint64 aProgress,
                                                     PRUint64 aProgressMax)
{
  nsresult rv = NS_OK;
  return rv;
}

NS_IMETHODIMP nsPluginStreamListenerPeer::OnStatus(nsIRequest *request,
                                                   nsISupports* aContext,
                                                   nsresult aStatus,
                                                   const PRUnichar* aStatusArg)
{
  return NS_OK;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetContentType(char** result)
{
  *result = const_cast<char*>(mContentType.get());
  return NS_OK;
}


NS_IMETHODIMP
nsPluginStreamListenerPeer::IsSeekable(PRBool* result)
{
  *result = mSeekable;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetLength(PRUint32* result)
{
  *result = mLength;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetLastModified(PRUint32* result)
{
  *result = mModified;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetURL(const char** result)
{
  *result = mURLSpec.get();
  return NS_OK;
}

void
nsPluginStreamListenerPeer::MakeByteRangeString(NPByteRange* aRangeList, nsACString &rangeRequest,
                                                PRInt32 *numRequests)
{
  rangeRequest.Truncate();
  *numRequests  = 0;
  //the string should look like this: bytes=500-700,601-999
  if (!aRangeList)
    return;
  
  PRInt32 requestCnt = 0;
  nsCAutoString string("bytes=");
  
  for (NPByteRange * range = aRangeList; range != nsnull; range = range->next) {
    // XXX zero length?
    if (!range->length)
      continue;
    
    // XXX needs to be fixed for negative offsets
    string.AppendInt(range->offset);
    string.Append("-");
    string.AppendInt(range->offset + range->length - 1);
    if (range->next)
      string += ",";
    
    requestCnt++;
  }
  
  // get rid of possible trailing comma
  string.Trim(",", PR_FALSE);
  
  rangeRequest = string;
  *numRequests  = requestCnt;
  return;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::RequestRead(NPByteRange* rangeList)
{
  nsCAutoString rangeString;
  PRInt32 numRequests;
  
  MakeByteRangeString(rangeList, rangeString, &numRequests);
  
  if (numRequests == 0)
    return NS_ERROR_FAILURE;
  
  nsresult rv = NS_OK;
  
  nsCOMPtr<nsIInterfaceRequestor> callbacks = do_QueryReferent(mWeakPtrChannelCallbacks);
  nsCOMPtr<nsILoadGroup> loadGroup = do_QueryReferent(mWeakPtrChannelLoadGroup);
  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannel(getter_AddRefs(channel), mURL, nsnull, loadGroup, callbacks);
  if (NS_FAILED(rv))
    return rv;
  
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel));
  if (!httpChannel)
    return NS_ERROR_FAILURE;
  
  httpChannel->SetRequestHeader(NS_LITERAL_CSTRING("Range"), rangeString, PR_FALSE);
  
  mAbort = PR_TRUE; // instruct old stream listener to cancel
  // the request on the next ODA.
  
  nsCOMPtr<nsIStreamListener> converter;
  
  if (numRequests == 1) {
    converter = this;
    // set current stream offset equal to the first offset in the range list
    // it will work for single byte range request
    // for multy range we'll reset it in ODA
    SetStreamOffset(rangeList->offset);
  } else {
    nsWeakPtr weakpeer =
    do_GetWeakReference(static_cast<nsISupportsWeakReference*>(this));
    nsPluginByteRangeStreamListener *brrListener =
    new nsPluginByteRangeStreamListener(weakpeer);
    if (brrListener)
      converter = brrListener;
    else
      return NS_ERROR_OUT_OF_MEMORY;
  }
  
  mPendingRequests += numRequests;
  
  nsCOMPtr<nsISupportsPRUint32> container = do_CreateInstance(NS_SUPPORTS_PRUINT32_CONTRACTID, &rv);
  if (NS_FAILED(rv))
    return rv;
  rv = container->SetData(MAGIC_REQUEST_CONTEXT);
  if (NS_FAILED(rv))
    return rv;
  
  return channel->AsyncOpen(converter, container);
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::GetStreamOffset(PRInt32* result)
{
  *result = mStreamOffset;
  return NS_OK;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::SetStreamOffset(PRInt32 value)
{
  mStreamOffset = value;
  return NS_OK;
}

nsresult nsPluginStreamListenerPeer::ServeStreamAsFile(nsIRequest *request,
                                                       nsISupports* aContext)
{
  if (!mPluginInstance)
    return NS_ERROR_FAILURE;
  
  // mPluginInstance->Stop calls mPStreamListener->CleanUpStream(), so stream will be properly clean up
  mPluginInstance->Stop();
  mPluginInstance->Start();
  nsCOMPtr<nsIPluginInstanceOwner> owner;
  mPluginInstance->GetOwner(getter_AddRefs(owner));
  if (owner) {
    NPWindow* window = nsnull;
    owner->GetWindow(window);
#if defined(MOZ_WIDGET_GTK2) || defined(MOZ_WIDGET_QT)
    // Should call GetPluginPort() here.
    // This part is copied from nsPluginInstanceOwner::GetPluginPort(). 
    nsCOMPtr<nsIWidget> widget;
    ((nsPluginNativeWindow*)window)->GetPluginWidget(getter_AddRefs(widget));
    if (widget) {
      window->window = widget->GetNativeData(NS_NATIVE_PLUGIN_PORT);
    }
#endif
    if (window->window) {
      nsCOMPtr<nsIPluginInstance> pluginInstCOMPtr = mPluginInstance.get();
      ((nsPluginNativeWindow*)window)->CallSetWindow(pluginInstCOMPtr);
    }
  }
  
  mSeekable = PR_FALSE;
  mPStreamListener->OnStartBinding(this);
  mStreamOffset = 0;
  
  // force the plugin to use stream as file
  mStreamType = NP_ASFILE;
  
  // then check it out if browser cache is not available
  nsCOMPtr<nsICachingChannel> cacheChannel = do_QueryInterface(request);
  if (!(cacheChannel && (NS_SUCCEEDED(cacheChannel->SetCacheAsFile(PR_TRUE))))) {
    nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
    if (channel) {
      SetupPluginCacheFile(channel);
    }
  }
  
  // unset mPendingRequests
  mPendingRequests = 0;
  
  return NS_OK;
}

PRBool
nsPluginStreamListenerPeer::UseExistingPluginCacheFile(nsPluginStreamListenerPeer* psi)
{
  
  NS_ENSURE_ARG_POINTER(psi);
  
  if (psi->mLength == mLength &&
      psi->mModified == mModified &&
      mStreamComplete &&
      mURLSpec.Equals(psi->mURLSpec))
  {
    return PR_TRUE;
  }
  return PR_FALSE;
}

NS_IMETHODIMP nsPluginStreamListenerPeer::OnDataAvailable(nsIRequest *request,
                                                          nsISupports* aContext,
                                                          nsIInputStream *aIStream,
                                                          PRUint32 sourceOffset,
                                                          PRUint32 aLength)
{
  if (mRequestFailed)
    return NS_ERROR_FAILURE;
  
  if (mAbort) {
    PRUint32 magicNumber = 0;  // set it to something that is not the magic number.
    nsCOMPtr<nsISupportsPRUint32> container = do_QueryInterface(aContext);
    if (container)
      container->GetData(&magicNumber);
    
    if (magicNumber != MAGIC_REQUEST_CONTEXT) {
      // this is not one of our range requests
      mAbort = PR_FALSE;
      return NS_BINDING_ABORTED;
    }
  }
  
  nsresult rv = NS_OK;
  
  if (!mPStreamListener)
    return NS_ERROR_FAILURE;
  
  mRequest = request;
  
  const char * url = nsnull;
  GetURL(&url);
  
  PLUGIN_LOG(PLUGIN_LOG_NOISY,
             ("nsPluginStreamListenerPeer::OnDataAvailable this=%p request=%p, offset=%d, length=%d, url=%s\n",
              this, request, sourceOffset, aLength, url ? url : "no url set"));
  
  // if the plugin has requested an AsFileOnly stream, then don't
  // call OnDataAvailable
  if (mStreamType != NP_ASFILEONLY) {
    // get the absolute offset of the request, if one exists.
    nsCOMPtr<nsIByteRangeRequest> brr = do_QueryInterface(request);
    if (brr) {
      if (!mDataForwardToRequest)
        return NS_ERROR_FAILURE;
      
      PRInt64 absoluteOffset64 = LL_ZERO;
      brr->GetStartRange(&absoluteOffset64);
      
      // XXX handle 64-bit for real
      PRInt32 absoluteOffset = (PRInt32)nsInt64(absoluteOffset64);
      
      // we need to track how much data we have forwarded to the
      // plugin.
      
      // FIXME: http://bugzilla.mozilla.org/show_bug.cgi?id=240130
      //
      // Why couldn't this be tracked on the plugin info, and not in a
      // *hash table*?
      nsPRUintKey key(absoluteOffset);
      PRInt32 amtForwardToPlugin =
      NS_PTR_TO_INT32(mDataForwardToRequest->Get(&key));
      mDataForwardToRequest->Put(&key, NS_INT32_TO_PTR(amtForwardToPlugin + aLength));
      
      SetStreamOffset(absoluteOffset + amtForwardToPlugin);
    }
    
    nsCOMPtr<nsIInputStream> stream = aIStream;
    
    // if we are caching the file ourselves to disk, we want to 'tee' off
    // the data as the plugin read from the stream.  We do this by the magic
    // of an input stream tee.
    
    if (mFileCacheOutputStream) {
      rv = NS_NewInputStreamTee(getter_AddRefs(stream), aIStream, mFileCacheOutputStream);
      if (NS_FAILED(rv))
        return rv;
    }
    
    rv =  mPStreamListener->OnDataAvailable(this,
                                            stream,
                                            aLength);
    
    // if a plugin returns an error, the peer must kill the stream
    //   else the stream and PluginStreamListener leak
    if (NS_FAILED(rv))
      request->Cancel(rv);
  }
  else
  {
    // if we don't read from the stream, OnStopRequest will never be called
    char* buffer = new char[aLength];
    PRUint32 amountRead, amountWrote = 0;
    rv = aIStream->Read(buffer, aLength, &amountRead);
    
    // if we are caching this to disk ourselves, lets write the bytes out.
    if (mFileCacheOutputStream) {
      while (amountWrote < amountRead && NS_SUCCEEDED(rv)) {
        rv = mFileCacheOutputStream->Write(buffer, amountRead, &amountWrote);
      }
    }
    delete [] buffer;
  }
  return rv;
}

NS_IMETHODIMP nsPluginStreamListenerPeer::OnStopRequest(nsIRequest *request,
                                                        nsISupports* aContext,
                                                        nsresult aStatus)
{
  nsresult rv = NS_OK;
  
  PLUGIN_LOG(PLUGIN_LOG_NOISY,
             ("nsPluginStreamListenerPeer::OnStopRequest this=%p aStatus=%d request=%p\n",
              this, aStatus, request));
  
  // for ByteRangeRequest we're just updating the mDataForwardToRequest hash and return.
  nsCOMPtr<nsIByteRangeRequest> brr = do_QueryInterface(request);
  if (brr) {
    PRInt64 absoluteOffset64 = LL_ZERO;
    brr->GetStartRange(&absoluteOffset64);
    // XXX support 64-bit offsets
    PRInt32 absoluteOffset = (PRInt32)nsInt64(absoluteOffset64);
    
    nsPRUintKey key(absoluteOffset);
    
    // remove the request from our data forwarding count hash.
    mDataForwardToRequest->Remove(&key);
    
    
    PLUGIN_LOG(PLUGIN_LOG_NOISY,
               ("                          ::OnStopRequest for ByteRangeRequest Started=%d\n",
                absoluteOffset));
  } else {
    // if this is not byte range request and
    // if we are writting the stream to disk ourselves,
    // close & tear it down here
    mFileCacheOutputStream = nsnull;
  }
  
  // if we still have pending stuff to do, lets not close the plugin socket.
  if (--mPendingRequests > 0)
    return NS_OK;
  
  // we keep our connections around...
  nsCOMPtr<nsISupportsPRUint32> container = do_QueryInterface(aContext);
  if (container) {
    PRUint32 magicNumber = 0;  // set it to something that is not the magic number.
    container->GetData(&magicNumber);
    if (magicNumber == MAGIC_REQUEST_CONTEXT) {
      // this is one of our range requests
      return NS_OK;
    }
  }
  
  if (!mPStreamListener)
    return NS_ERROR_FAILURE;
  
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  if (!channel)
    return NS_ERROR_FAILURE;
  // Set the content type to ensure we don't pass null to the plugin
  nsCAutoString aContentType;
  rv = channel->GetContentType(aContentType);
  if (NS_FAILED(rv) && !mRequestFailed)
    return rv;
  
  if (!aContentType.IsEmpty())
    mContentType = aContentType;
  
  // set error status if stream failed so we notify the plugin
  if (mRequestFailed)
    aStatus = NS_ERROR_FAILURE;
  
  if (NS_FAILED(aStatus)) {
    // on error status cleanup the stream
    // and return w/o OnFileAvailable()
    mPStreamListener->OnStopBinding(this, aStatus);
    return NS_OK;
  }
  
  // call OnFileAvailable if plugin requests stream type StreamType_AsFile or StreamType_AsFileOnly
  if (mStreamType >= NP_ASFILE) {
    nsCOMPtr<nsIFile> localFile;
    if (mLocalCachedFileHolder)
      localFile = mLocalCachedFileHolder->file();
    else {
      nsCOMPtr<nsICachingChannel> cacheChannel = do_QueryInterface(request);
      if (cacheChannel) {
        cacheChannel->GetCacheFile(getter_AddRefs(localFile));
      } else {
        // see if it is a file channel.
        nsCOMPtr<nsIFileChannel> fileChannel = do_QueryInterface(request);
        if (fileChannel) {
          fileChannel->GetFile(getter_AddRefs(localFile));
        }
      }
    }
    
    if (localFile) {
      OnFileAvailable(localFile);
    }
  }
  
  if (mStartBinding) {
    // On start binding has been called
    mPStreamListener->OnStopBinding(this, aStatus);
  } else {
    // OnStartBinding hasn't been called, so complete the action.
    mPStreamListener->OnStartBinding(this);
    mPStreamListener->OnStopBinding(this, aStatus);
  }
  
  if (NS_SUCCEEDED(aStatus)) {
    mStreamComplete = PR_TRUE;
  }
  mRequest = NULL;
  
  return NS_OK;
}

nsresult nsPluginStreamListenerPeer::SetUpStreamListener(nsIRequest *request,
                                                         nsIURI* aURL)
{
  nsresult rv = NS_OK;
  
  // If we don't yet have a stream listener, we need to get
  // one from the plugin.
  // NOTE: this should only happen when a stream was NOT created
  // with GetURL or PostURL (i.e. it's the initial stream we
  // send to the plugin as determined by the SRC or DATA attribute)
  if (!mPStreamListener && mPluginInstance)
    rv = mPluginInstance->NewStreamToPlugin(getter_AddRefs(mPStreamListener));
  
  if (NS_FAILED(rv))
    return rv;
  
  if (!mPStreamListener)
    return NS_ERROR_NULL_POINTER;
  
  PRBool useLocalCache = PR_FALSE;
  
  // get httpChannel to retrieve some info we need for nsIPluginStreamInfo setup
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(channel);
  
  /*
   * Assumption
   * By the time nsPluginStreamListenerPeer::OnDataAvailable() gets
   * called, all the headers have been read.
   */
  if (httpChannel) {
    // Reassemble the HTTP response status line and provide it to our
    // listener.  Would be nice if we could get the raw status line,
    // but nsIHttpChannel doesn't currently provide that.
    nsCOMPtr<nsIHTTPHeaderListener> listener =
    do_QueryInterface(mPStreamListener);
    if (listener) {
      // Status code: required; the status line isn't useful without it.
      PRUint32 statusNum;
      if (NS_SUCCEEDED(httpChannel->GetResponseStatus(&statusNum)) &&
          statusNum < 1000) {
        // HTTP version: provide if available.  Defaults to empty string.
        nsCString ver;
        nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal =
        do_QueryInterface(channel);
        if (httpChannelInternal) {
          PRUint32 major, minor;
          if (NS_SUCCEEDED(httpChannelInternal->GetResponseVersion(&major,
                                                                   &minor))) {
            ver = nsPrintfCString("/%lu.%lu", major, minor);
          }
        }
        
        // Status text: provide if available.  Defaults to "OK".
        nsCString statusText;
        if (NS_FAILED(httpChannel->GetResponseStatusText(statusText))) {
          statusText = "OK";
        }
        
        // Assemble everything and pass to listener.
        nsPrintfCString status(100, "HTTP%s %lu %s", ver.get(), statusNum,
                               statusText.get());
        listener->StatusLine(status.get());
      }
    }
    
    // Also provide all HTTP response headers to our listener.
    httpChannel->VisitResponseHeaders(this);
    
    mSeekable = PR_FALSE;
    // first we look for a content-encoding header. If we find one, we tell the
    // plugin that stream is not seekable, because the plugin always sees
    // uncompressed data, so it can't make meaningful range requests on a
    // compressed entity.  Also, we force the plugin to use
    // nsPluginStreamType_AsFile stream type and we have to save decompressed
    // file into local plugin cache, because necko cache contains original
    // compressed file.
    nsCAutoString contentEncoding;
    if (NS_SUCCEEDED(httpChannel->GetResponseHeader(NS_LITERAL_CSTRING("Content-Encoding"),
                                                    contentEncoding))) {
      useLocalCache = PR_TRUE;
    } else {
      // set seekability (seekable if the stream has a known length and if the
      // http server accepts byte ranges).
      PRUint32 length;
      GetLength(&length);
      if (length) {
        nsCAutoString range;
        if (NS_SUCCEEDED(httpChannel->GetResponseHeader(NS_LITERAL_CSTRING("accept-ranges"), range)) &&
            range.Equals(NS_LITERAL_CSTRING("bytes"), nsCaseInsensitiveCStringComparator())) {
          mSeekable = PR_TRUE;
        }
      }
    }
    
    // we require a content len
    // get Last-Modified header for plugin info
    nsCAutoString lastModified;
    if (NS_SUCCEEDED(httpChannel->GetResponseHeader(NS_LITERAL_CSTRING("last-modified"), lastModified)) &&
        !lastModified.IsEmpty()) {
      PRTime time64;
      PR_ParseTimeString(lastModified.get(), PR_TRUE, &time64);  //convert string time to integer time
      
      // Convert PRTime to unix-style time_t, i.e. seconds since the epoch
      double fpTime;
      LL_L2D(fpTime, time64);
      mModified = (PRUint32)(fpTime * 1e-6 + 0.5);
    }
  }
  
  rv = mPStreamListener->OnStartBinding(this);
  
  mStartBinding = PR_TRUE;
  
  if (NS_FAILED(rv))
    return rv;
  
  mPStreamListener->GetStreamType(&mStreamType);
  
  if (!useLocalCache && mStreamType >= NP_ASFILE) {
    // check it out if this is not a file channel.
    nsCOMPtr<nsIFileChannel> fileChannel = do_QueryInterface(request);
    if (!fileChannel) {
      // and browser cache is not available
      nsCOMPtr<nsICachingChannel> cacheChannel = do_QueryInterface(request);
      if (!(cacheChannel && (NS_SUCCEEDED(cacheChannel->SetCacheAsFile(PR_TRUE))))) {
        useLocalCache = PR_TRUE;
      }
    }
  }
  
  if (useLocalCache) {
    SetupPluginCacheFile(channel);
  }
  
  return NS_OK;
}

nsresult
nsPluginStreamListenerPeer::OnFileAvailable(nsIFile* aFile)
{
  nsresult rv;
  if (!mPStreamListener)
    return NS_ERROR_FAILURE;
  
  nsCAutoString path;
  rv = aFile->GetNativePath(path);
  if (NS_FAILED(rv)) return rv;
  
  if (path.IsEmpty()) {
    NS_WARNING("empty path");
    return NS_OK;
  }
  
  rv = mPStreamListener->OnFileAvailable(this, path.get());
  return rv;
}

NS_IMETHODIMP
nsPluginStreamListenerPeer::VisitHeader(const nsACString &header, const nsACString &value)
{
  nsCOMPtr<nsIHTTPHeaderListener> listener = do_QueryInterface(mPStreamListener);
  if (!listener)
    return NS_ERROR_FAILURE;
  
  return listener->NewResponseHeader(PromiseFlatCString(header).get(),
                                     PromiseFlatCString(value).get());
}
