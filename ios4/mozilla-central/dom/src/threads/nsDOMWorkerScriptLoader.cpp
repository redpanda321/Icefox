/* -*- Mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 40 -*- */
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
 * The Original Code is worker threads.
 *
 * The Initial Developer of the Original Code is
 *   Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ben Turner <bent.mozilla@gmail.com> (Original Author)
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

#include "nsDOMWorkerScriptLoader.h"

// Interfaces
#include "nsIChannel.h"
#include "nsIContentPolicy.h"
#include "nsIHttpChannel.h"
#include "nsIIOService.h"
#include "nsIRequest.h"
#include "nsIScriptSecurityManager.h"
#include "nsIStreamLoader.h"

// Other includes
#include "nsAutoLock.h"
#include "nsContentErrors.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsISupportsPrimitives.h"
#include "nsNetError.h"
#include "nsNetUtil.h"
#include "nsScriptLoader.h"
#include "nsThreadUtils.h"
#include "pratom.h"
#include "nsDocShellCID.h"
#include "nsIChannelPolicy.h"
#include "nsChannelPolicy.h"
#include "nsIContentSecurityPolicy.h"

// DOMWorker includes
#include "nsDOMWorkerPool.h"
#include "nsDOMWorkerSecurityManager.h"
#include "nsDOMThreadService.h"
#include "nsDOMWorkerTimeout.h"

#define LOG(_args) PR_LOG(gDOMThreadsLog, PR_LOG_DEBUG, _args)

nsDOMWorkerScriptLoader::nsDOMWorkerScriptLoader(nsDOMWorker* aWorker)
: nsDOMWorkerFeature(aWorker),
  mTarget(nsnull),
  mScriptCount(0),
  mCanceled(PR_FALSE),
  mForWorker(PR_FALSE)
{
  // Created on worker thread.
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(aWorker, "Null worker!");
}

NS_IMPL_ISUPPORTS_INHERITED2(nsDOMWorkerScriptLoader, nsDOMWorkerFeature,
                                                      nsIRunnable,
                                                      nsIStreamLoaderObserver)

nsresult
nsDOMWorkerScriptLoader::LoadScripts(JSContext* aCx,
                                     const nsTArray<nsString>& aURLs,
                                     PRBool aForWorker)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(aCx, "Null context!");

  mTarget = NS_GetCurrentThread();
  NS_ASSERTION(mTarget, "This should never be null!");

  if (mCanceled) {
    return NS_ERROR_ABORT;
  }

  mForWorker = aForWorker;

  mScriptCount = aURLs.Length();
  if (!mScriptCount) {
    return NS_ERROR_INVALID_ARG;
  }

  // Do all the memory work for these arrays now rather than checking for
  // failures all along the way.
  PRBool success = mLoadInfos.SetCapacity(mScriptCount);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  // Need one runnable per script and then an extra for the finished
  // notification.
  success = mPendingRunnables.SetCapacity(mScriptCount + 1);
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  for (PRUint32 index = 0; index < mScriptCount; index++) {
    ScriptLoadInfo* newInfo = mLoadInfos.AppendElement();
    NS_ASSERTION(newInfo, "Shouldn't fail if SetCapacity succeeded above!");

    newInfo->url.Assign(aURLs[index]);
    if (newInfo->url.IsEmpty()) {
      return NS_ERROR_INVALID_ARG;
    }

    success = newInfo->scriptObj.Hold(aCx);
    NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);
  }

  // Don't want timeouts, etc., from queuing up while we're waiting on the
  // network or compiling.
  AutoSuspendWorkerEvents aswe(this);

  nsresult rv = DoRunLoop(aCx);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Verify that all scripts downloaded and compiled.
  rv = VerifyScripts(aCx);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ExecuteScripts(aCx);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

nsresult
nsDOMWorkerScriptLoader::LoadScript(JSContext* aCx,
                                    const nsString& aURL,
                                    PRBool aForWorker)
{
  nsAutoTArray<nsString, 1> url;
  url.AppendElement(aURL);

  return LoadScripts(aCx, url, aForWorker);
}

nsresult
nsDOMWorkerScriptLoader::DoRunLoop(JSContext* aCx)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  volatile PRBool done = PR_FALSE;
  mDoneRunnable = new ScriptLoaderDone(this, &done);
  NS_ENSURE_TRUE(mDoneRunnable, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = NS_DispatchToMainThread(this);
  NS_ENSURE_SUCCESS(rv, rv);

  while (!(done || mCanceled)) {
    JSAutoSuspendRequest asr(aCx);
    NS_ProcessNextEvent(mTarget);
  }

  return mCanceled ? NS_ERROR_ABORT : NS_OK;
}

nsresult
nsDOMWorkerScriptLoader::VerifyScripts(JSContext* aCx)
{
  NS_ASSERTION(aCx, "Shouldn't be null!");

  nsresult rv = NS_OK;

  for (PRUint32 index = 0; index < mScriptCount; index++) {
    ScriptLoadInfo& loadInfo = mLoadInfos[index];
    NS_ASSERTION(loadInfo.done, "Inconsistent state!");

    if (NS_SUCCEEDED(loadInfo.result) && loadInfo.scriptObj.ToJSObject()) {
      continue;
    }

    NS_ASSERTION(!loadInfo.scriptObj.ToJSObject(), "Inconsistent state!");

    // Flag failure before worrying about whether or not to report an error.
    rv = NS_FAILED(loadInfo.result) ? loadInfo.result : NS_ERROR_FAILURE;

    // If loadInfo.result is a success code then the compiler probably reported
    // an error already. Also we don't really care about NS_BINDING_ABORTED
    // since that's the code we set when some other script had a problem and the
    // rest were canceled.
    if (NS_SUCCEEDED(loadInfo.result) || loadInfo.result == NS_BINDING_ABORTED) {
      continue;
    }

    // Ok, this is the script that caused us to fail.

    JSAutoRequest ar(aCx);

    // Only throw an error if there is no other pending exception.
    if (!JS_IsExceptionPending(aCx)) {
      const char* message;
      switch (loadInfo.result) {
        case NS_ERROR_MALFORMED_URI:
          message = "Malformed script URI: %s";
          break;
        case NS_ERROR_FILE_NOT_FOUND:
        case NS_ERROR_NOT_AVAILABLE:
          message = "Script file not found: %s";
          break;
        default:
          message = "Failed to load script: %s (nsresult = 0x%x)";
          break;
      }
      NS_ConvertUTF16toUTF8 url(loadInfo.url);
      JS_ReportError(aCx, message, url.get(), loadInfo.result);
    }
    break;
  }

  return rv;
}

nsresult
nsDOMWorkerScriptLoader::ExecuteScripts(JSContext* aCx)
{
  NS_ASSERTION(aCx, "Shouldn't be null!");

  // Now execute all the scripts.
  for (PRUint32 index = 0; index < mScriptCount; index++) {
    ScriptLoadInfo& loadInfo = mLoadInfos[index];

    JSAutoRequest ar(aCx);

    JSScript* script =
      static_cast<JSScript*>(JS_GetPrivate(aCx, loadInfo.scriptObj.ToJSObject()));
    NS_ASSERTION(script, "This shouldn't ever be null!");

    JSObject* global = mWorker->mGlobal ?
                       mWorker->mGlobal :
                       JS_GetGlobalObject(aCx);
    NS_ENSURE_STATE(global);

    // Because we may have nested calls to this function we don't want the
    // execution to automatically report errors. We let them propagate instead.
    uint32 oldOpts =
      JS_SetOptions(aCx, JS_GetOptions(aCx) | JSOPTION_DONT_REPORT_UNCAUGHT);

    jsval val;
    PRBool success = JS_ExecuteScript(aCx, global, script, &val);

    JS_SetOptions(aCx, oldOpts);

    if (!success) {
      return NS_ERROR_FAILURE;
    }
  }
  return NS_OK;
}

void
nsDOMWorkerScriptLoader::Cancel()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  NS_ASSERTION(!mCanceled, "Cancel called more than once!");
  mCanceled = PR_TRUE;

  for (PRUint32 index = 0; index < mScriptCount; index++) {
    ScriptLoadInfo& loadInfo = mLoadInfos[index];

    nsIRequest* request =
      static_cast<nsIRequest*>(loadInfo.channel.get());
    if (request) {
#ifdef DEBUG
      nsresult rv =
#endif
      request->Cancel(NS_BINDING_ABORTED);
      NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Failed to cancel channel!");
    }
  }

  nsAutoTArray<ScriptLoaderRunnable*, 10> runnables;
  {
    nsAutoLock lock(mWorker->Lock());
    runnables.AppendElements(mPendingRunnables);
    mPendingRunnables.Clear();
  }

  PRUint32 runnableCount = runnables.Length();
  for (PRUint32 index = 0; index < runnableCount; index++) {
    runnables[index]->Revoke();
  }

  // We're about to post a revoked event to the worker thread, which seems
  // silly, but we have to do this because the worker thread may be sleeping
  // waiting on its event queue.
  NotifyDone();
}

NS_IMETHODIMP
nsDOMWorkerScriptLoader::Run()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  // We may have been canceled already.
  if (mCanceled) {
    return NS_BINDING_ABORTED;
  }

  nsresult rv = RunInternal();
  if (NS_SUCCEEDED(rv)) {
    return rv;
  }

  // Ok, something failed beyond a normal cancel.

  // If necko is holding a ref to us then we'll end up notifying in the
  // OnStreamComplete method, not here.
  PRBool needsNotify = PR_TRUE;

  // Cancel any async channels that were already opened.
  for (PRUint32 index = 0; index < mScriptCount; index++) {
    ScriptLoadInfo& loadInfo = mLoadInfos[index];

    nsIRequest* request = static_cast<nsIRequest*>(loadInfo.channel.get());
    if (request) {
#ifdef DEBUG
      nsresult rvInner =
#endif
      request->Cancel(NS_BINDING_ABORTED);
      NS_WARN_IF_FALSE(NS_SUCCEEDED(rvInner), "Failed to cancel channel!");

      // Necko is holding a ref to us so make sure that the OnStreamComplete
      // code sends the done event.
      needsNotify = PR_FALSE;
    }
    else {
      // Make sure to set this so that the OnStreamComplete code will dispatch
      // the done event.
      loadInfo.done = PR_TRUE;
    }
  }

  if (needsNotify) {
    NotifyDone();
  }

  return rv;
}

NS_IMETHODIMP
nsDOMWorkerScriptLoader::OnStreamComplete(nsIStreamLoader* aLoader,
                                          nsISupports* aContext,
                                          nsresult aStatus,
                                          PRUint32 aStringLen,
                                          const PRUint8* aString)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  // We may have been canceled already.
  if (mCanceled) {
    return NS_BINDING_ABORTED;
  }

  nsresult rv = OnStreamCompleteInternal(aLoader, aContext, aStatus, aStringLen,
                                         aString);

  // Dispatch the done event if we've received all the data.
  for (PRUint32 index = 0; index < mScriptCount; index++) {
    if (!mLoadInfos[index].done) {
      // Some async load is still outstanding, don't notify yet.
      break;
    }

    if (index == mScriptCount - 1) {
      // All loads complete, signal the thread.
      NotifyDone();
    }
  }

  return rv;
}

nsresult
nsDOMWorkerScriptLoader::RunInternal()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  // Things we need to make all this work...
  nsCOMPtr<nsIDocument> parentDoc = mWorker->Pool()->ParentDocument();
  if (!parentDoc) {
    // Must have been canceled.
    return NS_ERROR_ABORT;
  }

  nsIPrincipal* principal;
  nsIURI* baseURI;

  if (mForWorker) {
    NS_ASSERTION(mScriptCount == 1, "Bad state!");

    nsRefPtr<nsDOMWorker> parentWorker = mWorker->GetParent();
    if (parentWorker) {
      principal = parentWorker->GetPrincipal();
      NS_ENSURE_STATE(principal);

      baseURI = parentWorker->GetURI();
      NS_ENSURE_STATE(baseURI);
    }
    else {
      principal = parentDoc->NodePrincipal();
      NS_ENSURE_STATE(principal);

      baseURI = parentDoc->GetDocBaseURI();
    }
  }
  else {
    principal = mWorker->GetPrincipal();
    baseURI = mWorker->GetURI();

    NS_ASSERTION(principal && baseURI, "Should have been set already!");
  }

  // All of these can potentially be null, but that should be ok. We'll either
  // succeed without them or fail below.
  nsCOMPtr<nsILoadGroup> loadGroup(parentDoc->GetDocumentLoadGroup());
  nsCOMPtr<nsIIOService> ios(do_GetIOService());

  for (PRUint32 index = 0; index < mScriptCount; index++) {
    ScriptLoadInfo& loadInfo = mLoadInfos[index];
    nsresult& rv = loadInfo.result;

    nsCOMPtr<nsIURI>& uri = loadInfo.finalURI;
    rv = nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(uri),
                                                   loadInfo.url, parentDoc,
                                                   baseURI);
    if (NS_FAILED(rv)) {
      return rv;
    }

    nsIScriptSecurityManager* secMan = nsContentUtils::GetSecurityManager();
    NS_ENSURE_TRUE(secMan, NS_ERROR_FAILURE);

    PRInt16 shouldLoad = nsIContentPolicy::ACCEPT;
    rv = NS_CheckContentLoadPolicy(nsIContentPolicy::TYPE_SCRIPT, uri,
                                   principal, parentDoc,
                                   NS_LITERAL_CSTRING("text/javascript"),
                                   nsnull, &shouldLoad,
                                   nsContentUtils::GetContentPolicy(), secMan);
    if (NS_FAILED(rv) || NS_CP_REJECTED(shouldLoad)) {
      if (NS_FAILED(rv) || shouldLoad != nsIContentPolicy::REJECT_TYPE) {
        return NS_ERROR_CONTENT_BLOCKED;
      }
      return NS_ERROR_CONTENT_BLOCKED_SHOW_ALT;
    }

    // If this script loader is being used to make a new worker then we need to
    // do a same-origin check. Otherwise we need to clear the load with the
    // security manager.
    if (mForWorker) {
      rv = principal->CheckMayLoad(uri, PR_FALSE);
      NS_ENSURE_SUCCESS(rv, rv);

      // Set the principal and URI on the new worker.
      mWorker->SetPrincipal(principal);

      rv = mWorker->SetURI(uri);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      rv = secMan->CheckLoadURIWithPrincipal(principal, uri, 0);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // We need to know which index we're on in OnStreamComplete so we know where
    // to put the result.
    nsCOMPtr<nsISupportsPRUint32> indexSupports =
      do_CreateInstance(NS_SUPPORTS_PRUINT32_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = indexSupports->SetData(index);
    NS_ENSURE_SUCCESS(rv, rv);

    // We don't care about progress so just use the simple stream loader for
    // OnStreamComplete notification only.
    nsCOMPtr<nsIStreamLoader> loader;
    rv = NS_NewStreamLoader(getter_AddRefs(loader), this);
    NS_ENSURE_SUCCESS(rv, rv);

    // get Content Security Policy from parent document to pass into channel
    nsCOMPtr<nsIChannelPolicy> channelPolicy;
    nsCOMPtr<nsIContentSecurityPolicy> csp;
    rv = parentDoc->NodePrincipal()->GetCsp(getter_AddRefs(csp));
    NS_ENSURE_SUCCESS(rv, rv);
    if (csp) {
        channelPolicy = do_CreateInstance("@mozilla.org/nschannelpolicy;1");
        channelPolicy->SetContentSecurityPolicy(csp);
        channelPolicy->SetLoadType(nsIContentPolicy::TYPE_SCRIPT);
    }

    rv = NS_NewChannel(getter_AddRefs(loadInfo.channel),
                       uri,
                       ios,
                       loadGroup,
                       nsnull,                            // callbacks
                       nsIRequest::LOAD_NORMAL |
                       nsIChannel::LOAD_CLASSIFY_URI,     // loadFlags
                       channelPolicy);                    // CSP info
    NS_ENSURE_SUCCESS(rv, rv);

    rv = loadInfo.channel->AsyncOpen(loader, indexSupports);
    if (NS_FAILED(rv)) {
      // Null this out so we don't try to cancel it later.
      loadInfo.channel = nsnull;
      return rv;
    }
  }

  return NS_OK;
}

nsresult
nsDOMWorkerScriptLoader::OnStreamCompleteInternal(nsIStreamLoader* aLoader,
                                                  nsISupports* aContext,
                                                  nsresult aStatus,
                                                  PRUint32 aStringLen,
                                                  const PRUint8* aString)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsCOMPtr<nsISupportsPRUint32> indexSupports(do_QueryInterface(aContext));
  NS_ENSURE_TRUE(indexSupports, NS_ERROR_NO_INTERFACE);

  PRUint32 index = PR_UINT32_MAX;
  indexSupports->GetData(&index);

  if (index >= mScriptCount) {
    NS_NOTREACHED("This really can't fail or we'll hang!");
    return NS_ERROR_FAILURE;
  }

  ScriptLoadInfo& loadInfo = mLoadInfos[index];

  NS_ASSERTION(!loadInfo.done, "Got complete on the same load twice!");
  loadInfo.done = PR_TRUE;

#ifdef DEBUG
  // Make sure we're seeing the channel that we expect.
  nsCOMPtr<nsIRequest> requestDebug;
  nsresult rvDebug = aLoader->GetRequest(getter_AddRefs(requestDebug));

  // When we cancel sometimes we get null here. That should be ok, but only if
  // we're canceled.
  NS_ASSERTION(NS_SUCCEEDED(rvDebug) || mCanceled, "GetRequest failed!");

  if (NS_SUCCEEDED(rvDebug)) {
    nsCOMPtr<nsIChannel> channel(do_QueryInterface(requestDebug));
    NS_ASSERTION(channel, "QI failed!");

    nsCOMPtr<nsISupports> thisChannel(do_QueryInterface(channel));
    NS_ASSERTION(thisChannel, "QI failed!");

    nsCOMPtr<nsISupports> ourChannel(do_QueryInterface(loadInfo.channel));
    NS_ASSERTION(ourChannel, "QI failed!");

    NS_ASSERTION(thisChannel == ourChannel, "Wrong channel!");
  }
#endif

  // Use an alias to keep rv and loadInfo.result in sync.
  nsresult& rv = loadInfo.result = aStatus;

  if (NS_FAILED(rv)) {
    return rv;
  }

  if (!(aStringLen && aString)) {
    return rv = NS_ERROR_UNEXPECTED;
  }

  // Make sure we're not seeing the result of a 404 or something by checking the
  // 'requestSucceeded' attribute on the http channel.
  nsCOMPtr<nsIRequest> request;
  rv = aLoader->GetRequest(getter_AddRefs(request));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(request));
  if (httpChannel) {
    PRBool requestSucceeded;
    rv = httpChannel->GetRequestSucceeded(&requestSucceeded);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!requestSucceeded) {
      return rv = NS_ERROR_NOT_AVAILABLE;
    }
  }

  nsIDocument* parentDoc = mWorker->Pool()->ParentDocument();
  if (!parentDoc) {
    NS_ASSERTION(mWorker->IsCanceled(),
                 "Null parent document when we're not canceled?!");
    return rv = NS_ERROR_FAILURE;
  }

  // Use the regular nsScriptLoader for this grunt work! Should be just fine
  // because we're running on the main thread.
  rv = nsScriptLoader::ConvertToUTF16(loadInfo.channel, aString, aStringLen,
                                      EmptyString(), parentDoc,
                                      loadInfo.scriptText);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (loadInfo.scriptText.IsEmpty()) {
    return rv = NS_ERROR_FAILURE;
  }

  nsCString filename;
  loadInfo.finalURI->GetSpec(filename);

  if (filename.IsEmpty()) {
    filename.Assign(NS_LossyConvertUTF16toASCII(loadInfo.url));
  }
  else {
    // This will help callers figure out what their script url resolved to in
    // case of errors.
    loadInfo.url.Assign(NS_ConvertUTF8toUTF16(filename));
  }

  nsRefPtr<ScriptCompiler> compiler =
    new ScriptCompiler(this, loadInfo.scriptText, filename, loadInfo.scriptObj);
  NS_ASSERTION(compiler, "Out of memory!");
  if (!compiler) {
    return rv = NS_ERROR_OUT_OF_MEMORY;
  }

  rv = mTarget->Dispatch(compiler, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  return rv;
}

void
nsDOMWorkerScriptLoader::NotifyDone()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mDoneRunnable) {
    // We've already completed, no need to cancel anything.
    return;
  }

  for (PRUint32 index = 0; index < mScriptCount; index++) {
    ScriptLoadInfo& loadInfo = mLoadInfos[index];
    // Null both of these out because they aren't threadsafe and must be
    // destroyed on this thread.
    loadInfo.channel = nsnull;
    loadInfo.finalURI = nsnull;

    if (mCanceled) {
      // Simulate a complete, yet failed, load.
      loadInfo.done = PR_TRUE;
      loadInfo.result = NS_BINDING_ABORTED;
    }
  }

#ifdef DEBUG
  nsresult rv =
#endif
  mTarget->Dispatch(mDoneRunnable, NS_DISPATCH_NORMAL);
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Couldn't dispatch done event!");

  mDoneRunnable = nsnull;
}

void
nsDOMWorkerScriptLoader::SuspendWorkerEvents()
{
  NS_ASSERTION(mWorker, "No worker yet!");
  mWorker->SuspendFeatures();
}

void
nsDOMWorkerScriptLoader::ResumeWorkerEvents()
{
  NS_ASSERTION(mWorker, "No worker yet!");
  mWorker->ResumeFeatures();
}

nsDOMWorkerScriptLoader::
ScriptLoaderRunnable::ScriptLoaderRunnable(nsDOMWorkerScriptLoader* aLoader)
: mRevoked(PR_FALSE),
  mLoader(aLoader)
{
  nsAutoLock lock(aLoader->Lock());
#ifdef DEBUG
  nsDOMWorkerScriptLoader::ScriptLoaderRunnable** added =
#endif
  aLoader->mPendingRunnables.AppendElement(this);
  NS_ASSERTION(added, "This shouldn't fail because we SetCapacity earlier!");
}

nsDOMWorkerScriptLoader::
ScriptLoaderRunnable::~ScriptLoaderRunnable()
{
  if (!mRevoked) {
    nsAutoLock lock(mLoader->Lock());
#ifdef DEBUG
    PRBool removed =
#endif
    mLoader->mPendingRunnables.RemoveElement(this);
    NS_ASSERTION(removed, "Someone has changed the array!");
  }
}

NS_IMPL_THREADSAFE_ISUPPORTS1(nsDOMWorkerScriptLoader::ScriptLoaderRunnable,
                              nsIRunnable)

void
nsDOMWorkerScriptLoader::ScriptLoaderRunnable::Revoke()
{
  mRevoked = PR_TRUE;
}

nsDOMWorkerScriptLoader::
ScriptCompiler::ScriptCompiler(nsDOMWorkerScriptLoader* aLoader,
                               const nsString& aScriptText,
                               const nsCString& aFilename,
                               nsAutoJSValHolder& aScriptObj)
: ScriptLoaderRunnable(aLoader),
  mScriptText(aScriptText),
  mFilename(aFilename),
  mScriptObj(aScriptObj)
{
  NS_ASSERTION(!aScriptText.IsEmpty(), "No script to compile!");
  NS_ASSERTION(aScriptObj.IsHeld(), "Should be held!");
}

NS_IMETHODIMP
nsDOMWorkerScriptLoader::ScriptCompiler::Run()
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (mRevoked) {
    return NS_OK;
  }

  NS_ASSERTION(!mScriptObj.ToJSObject(), "Already have a script object?!");
  NS_ASSERTION(mScriptObj.IsHeld(), "Not held?!");
  NS_ASSERTION(!mScriptText.IsEmpty(), "Shouldn't have empty source here!");

  JSContext* cx = nsDOMThreadService::GetCurrentContext();
  NS_ENSURE_STATE(cx);

  JSAutoRequest ar(cx);

  JSObject* global = JS_GetGlobalObject(cx);
  NS_ENSURE_STATE(global);

  // Because we may have nested calls to this function we don't want the
  // execution to automatically report errors. We let them propagate instead.
  uint32 oldOpts =
    JS_SetOptions(cx, JS_GetOptions(cx) | JSOPTION_DONT_REPORT_UNCAUGHT);

  JSPrincipals* principal = nsDOMWorkerSecurityManager::WorkerPrincipal();

  JSScript* script =
    JS_CompileUCScriptForPrincipals(cx, global, principal,
                                    reinterpret_cast<const jschar*>
                                               (mScriptText.BeginReading()),
                                    mScriptText.Length(), mFilename.get(), 1);

  JS_SetOptions(cx, oldOpts);

  if (!script) {
    return NS_ERROR_FAILURE;
  }

  mScriptObj = JS_NewScriptObject(cx, script);
  NS_ENSURE_STATE(mScriptObj.ToJSObject());

  return NS_OK;
}

nsDOMWorkerScriptLoader::
ScriptLoaderDone::ScriptLoaderDone(nsDOMWorkerScriptLoader* aLoader,
                                   volatile PRBool* aDoneFlag)
: ScriptLoaderRunnable(aLoader),
  mDoneFlag(aDoneFlag)
{
  NS_ASSERTION(aDoneFlag && !*aDoneFlag, "Bad setup!");
}

NS_IMETHODIMP
nsDOMWorkerScriptLoader::ScriptLoaderDone::Run()
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (mRevoked) {
    return NS_OK;
  }

  *mDoneFlag = PR_TRUE;
  return NS_OK;
}

nsDOMWorkerScriptLoader::
AutoSuspendWorkerEvents::AutoSuspendWorkerEvents(nsDOMWorkerScriptLoader* aLoader)
: mLoader(aLoader)
{
  NS_ASSERTION(aLoader, "Don't hand me null!");
  aLoader->SuspendWorkerEvents();
}

nsDOMWorkerScriptLoader::
AutoSuspendWorkerEvents::~AutoSuspendWorkerEvents()
{
  mLoader->ResumeWorkerEvents();
}
