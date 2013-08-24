/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
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
 * The Original Code is Indexed Database.
 *
 * The Initial Developer of the Original Code is
 * The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ben Turner <bent.mozilla@gmail.com>
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

#include "AsyncConnectionHelper.h"

#include "nsIIDBDatabaseException.h"

#include "mozilla/storage.h"
#include "nsComponentManagerUtils.h"
#include "nsProxyRelease.h"
#include "nsThreadUtils.h"

#include "IDBEvents.h"
#include "IDBTransaction.h"
#include "TransactionThreadPool.h"

using mozilla::TimeStamp;
using mozilla::TimeDuration;

USING_INDEXEDDB_NAMESPACE

namespace {

const PRUint32 kProgressHandlerGranularity = 1000;
const PRUint32 kDefaultTimeoutMS = 30000;

NS_STACK_CLASS
class TransactionPoolEventTarget : public nsIEventTarget
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIEVENTTARGET

  TransactionPoolEventTarget(IDBTransaction* aTransaction)
  : mTransaction(aTransaction)
  { }

private:
  IDBTransaction* mTransaction;
};

} // anonymous namespace

AsyncConnectionHelper::AsyncConnectionHelper(IDBDatabase* aDatabase,
                                             IDBRequest* aRequest)
: mDatabase(aDatabase),
  mRequest(aRequest),
  mTimeoutDuration(TimeDuration::FromMilliseconds(kDefaultTimeoutMS)),
  mErrorCode(0),
  mError(PR_FALSE)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(mRequest, "Null request!");
}

AsyncConnectionHelper::AsyncConnectionHelper(IDBTransaction* aTransaction,
                                             IDBRequest* aRequest)
: mDatabase(aTransaction->mDatabase),
  mTransaction(aTransaction),
  mRequest(aRequest),
  mTimeoutDuration(TimeDuration::FromMilliseconds(kDefaultTimeoutMS)),
  mErrorCode(0),
  mError(PR_FALSE)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(mRequest, "Null request!");
}

AsyncConnectionHelper::~AsyncConnectionHelper()
{
  if (!NS_IsMainThread()) {
    NS_ASSERTION(mErrorCode == NOREPLY || !mRequest,
                 "This should only happen if NOREPLY was returned or if the "
                 "runnable already ran on the main thread!");

    IDBDatabase* database;
    mDatabase.forget(&database);

    IDBTransaction* transaction;
    mTransaction.forget(&transaction);

    IDBRequest* request;
    mRequest.forget(&request);

    nsCOMPtr<nsIThread> mainThread;
    NS_GetMainThread(getter_AddRefs(mainThread));
    NS_WARN_IF_FALSE(mainThread, "Couldn't get the main thread!");

    if (mainThread) {
      if (database) {
        NS_ProxyRelease(mainThread, static_cast<nsIIDBDatabase*>(database));
      }
      if (transaction) {
        NS_ProxyRelease(mainThread,
                        static_cast<nsIIDBTransaction*>(transaction));
      }
      if (request) {
        NS_ProxyRelease(mainThread, static_cast<nsIDOMEventTarget*>(request));
      }
    }
  }

  NS_ASSERTION(!mOldProgressHandler, "Should not have anything here!");
}

NS_IMPL_THREADSAFE_ISUPPORTS2(AsyncConnectionHelper, nsIRunnable,
                                                     mozIStorageProgressHandler)

NS_IMETHODIMP
AsyncConnectionHelper::Run()
{
  if (NS_IsMainThread()) {
    if (mRequest->mAborted) {
      NS_ASSERTION(mRequest->mReadyState == nsIIDBRequest::DONE,
                   "Wrong state!");
      mError = true;
      mErrorCode = nsIIDBDatabaseException::UNKNOWN_ERR;
    }
    else {
      NS_ASSERTION(mRequest->mReadyState == nsIIDBRequest::LOADING,
                   "Wrong state!");
      mRequest->mReadyState = nsIIDBRequest::DONE;
    }

    // Call OnError if the database had an error or if the OnSuccess handler
    // has an error.
    if (mError || ((mErrorCode = OnSuccess(mRequest)) != OK)) {
      OnError(mRequest, mErrorCode);
    }

    if (mTransaction) {
      mTransaction->OnRequestFinished();
    }

    mDatabase = nsnull;
    mTransaction = nsnull;
    mRequest = nsnull;
    return NS_OK;
  }

  nsresult rv = NS_OK;
  nsCOMPtr<mozIStorageConnection> connection;

  if (mTransaction) {
    rv = mTransaction->GetOrCreateConnection(getter_AddRefs(connection));
    if (NS_SUCCEEDED(rv)) {
      NS_ASSERTION(connection, "This should never be null!");
    }
  }
  else if (mDatabase) {
    rv = mDatabase->GetOrCreateConnection(getter_AddRefs(connection));
    if (NS_SUCCEEDED(rv)) {
      NS_ASSERTION(connection, "This should never be null!");
    }
  }

  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "GetOrCreateConnection failed!");

  if (connection) {
    rv = connection->SetProgressHandler(kProgressHandlerGranularity, this,
                                        getter_AddRefs(mOldProgressHandler));
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "SetProgressHandler failed!");
    if (NS_SUCCEEDED(rv)) {
      mStartTime = TimeStamp::Now();
    }
  }

  if (NS_SUCCEEDED(rv)) {
    mErrorCode = DoDatabaseWork(connection);
  }
  else {
    mErrorCode = nsIIDBDatabaseException::UNKNOWN_ERR;
  }

  if (!mStartTime.IsNull()) {
    nsCOMPtr<mozIStorageProgressHandler> handler;
    rv = connection->RemoveProgressHandler(getter_AddRefs(handler));
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "RemoveProgressHandler failed!");
#ifdef DEBUG
    if (NS_SUCCEEDED(rv)) {
      nsCOMPtr<nsISupports> handlerSupports(do_QueryInterface(handler));
      nsCOMPtr<nsISupports> thisSupports =
        do_QueryInterface(static_cast<nsIRunnable*>(this));
      NS_ASSERTION(thisSupports == handlerSupports, "Mismatch!");
    }
#endif
    mStartTime = TimeStamp();
  }

  if (mErrorCode != NOREPLY) {
    mError = mErrorCode != OK;

    return NS_DispatchToMainThread(this, NS_DISPATCH_NORMAL);
  }

  return NS_OK;
}

NS_IMETHODIMP
AsyncConnectionHelper::OnProgress(mozIStorageConnection* aConnection,
                                  PRBool* _retval)
{
  TimeDuration elapsed = TimeStamp::Now() - mStartTime;
  if (elapsed >= mTimeoutDuration) {
    *_retval = PR_TRUE;
    return NS_OK;
  }

  if (mOldProgressHandler) {
    return mOldProgressHandler->OnProgress(aConnection, _retval);
  }

  *_retval = PR_FALSE;
  return NS_OK;
}

nsresult
AsyncConnectionHelper::Dispatch(nsIEventTarget* aDatabaseThread)
{
#ifdef DEBUG
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  {
    PRBool sameThread;
    nsresult rv = aDatabaseThread->IsOnCurrentThread(&sameThread);
    NS_ASSERTION(NS_SUCCEEDED(rv), "IsOnCurrentThread failed!");
    NS_ASSERTION(!sameThread, "Dispatching to main thread not supported!");
  }
#endif

  nsresult rv = Init();
  if (NS_FAILED(rv)) {
    return rv;
  }

  NS_ASSERTION(mRequest->mReadyState == nsIIDBRequest::INITIAL,
               "Wrong readyState!");
  mRequest->mReadyState = nsIIDBRequest::LOADING;

  rv = aDatabaseThread->Dispatch(this, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mTransaction) {
    mTransaction->OnNewRequest();
  }

  return NS_OK;
}

nsresult
AsyncConnectionHelper::DispatchToTransactionPool()
{
  NS_ASSERTION(mTransaction, "Only ok to call this with a transaction!");
  TransactionPoolEventTarget target(mTransaction);
  return Dispatch(&target);
}

nsresult
AsyncConnectionHelper::Init()
{
  return NS_OK;
}

PRUint16
AsyncConnectionHelper::OnSuccess(nsIDOMEventTarget* aTarget)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsCOMPtr<nsIWritableVariant> variant =
    do_CreateInstance(NS_VARIANT_CONTRACTID);
  if (!variant) {
    NS_ERROR("Couldn't create variant!");
    return nsIIDBDatabaseException::UNKNOWN_ERR;
  }

  PRUint16 result = GetSuccessResult(variant);
  if (result != OK) {
    return result;
  }

  // Check to make sure we have a listener here before actually firing.
  nsCOMPtr<nsPIDOMEventTarget> target(do_QueryInterface(aTarget));
  if (target) {
    nsIEventListenerManager* manager = target->GetListenerManager(PR_FALSE);
    if (!manager ||
        !manager->HasListenersFor(NS_LITERAL_STRING(SUCCESS_EVT_STR))) {
      // No listeners here, skip creating and dispatching the event.
      return OK;
    }
  }

  if (NS_FAILED(variant->SetWritable(PR_FALSE))) {
    NS_ERROR("Failed to make variant readonly!");
    return nsIIDBDatabaseException::UNKNOWN_ERR;
  }

  nsCOMPtr<nsIDOMEvent> event =
    IDBSuccessEvent::Create(mRequest, variant, mTransaction);
  if (!event) {
    NS_ERROR("Failed to create event!");
    return nsIIDBDatabaseException::UNKNOWN_ERR;
  }

  PRBool dummy;
  aTarget->DispatchEvent(event, &dummy);
  return OK;
}

void
AsyncConnectionHelper::OnError(nsIDOMEventTarget* aTarget,
                               PRUint16 aErrorCode)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  // Make an error event and fire it at the target.
  nsCOMPtr<nsIDOMEvent> event(IDBErrorEvent::Create(mRequest, aErrorCode));
  if (!event) {
    NS_ERROR("Failed to create event!");
    return;
  }

  PRBool dummy;
  aTarget->DispatchEvent(event, &dummy);
}

PRUint16
AsyncConnectionHelper::GetSuccessResult(nsIWritableVariant* /* aResult */)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  // Leave the variant remain set to empty.

  return OK;
}

NS_IMETHODIMP_(nsrefcnt)
TransactionPoolEventTarget::AddRef()
{
  NS_NOTREACHED("Don't call me!");
  return 2;
}

NS_IMETHODIMP_(nsrefcnt)
TransactionPoolEventTarget::Release()
{
  NS_NOTREACHED("Don't call me!");
  return 1;
}

NS_IMPL_QUERY_INTERFACE1(TransactionPoolEventTarget, nsIEventTarget)

NS_IMETHODIMP
TransactionPoolEventTarget::Dispatch(nsIRunnable* aRunnable,
                                     PRUint32 aFlags)
{
  NS_ASSERTION(aRunnable, "Null pointer!");
  NS_ASSERTION(aFlags == NS_DISPATCH_NORMAL, "Unsupported!");

  TransactionThreadPool* pool = TransactionThreadPool::GetOrCreate();
  NS_ENSURE_TRUE(pool, NS_ERROR_FAILURE);

  return pool->Dispatch(mTransaction, aRunnable, false, nsnull);
}

NS_IMETHODIMP
TransactionPoolEventTarget::IsOnCurrentThread(PRBool* aResult)
{
  *aResult = PR_FALSE;
  return NS_OK;
}
