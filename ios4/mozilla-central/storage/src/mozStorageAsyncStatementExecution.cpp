/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
 * ***** BEGIN LICENSE BLOCK *****
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
 * Mozilla Corporation. 
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Shawn Wilsher <me@shawnwilsher.com> (Original Author)
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

#include "nsAutoPtr.h"
#include "prtime.h"

#include "sqlite3.h"

#include "mozIStorageStatementCallback.h"
#include "mozStorageBindingParams.h"
#include "mozStorageHelper.h"
#include "mozStorageResultSet.h"
#include "mozStorageRow.h"
#include "mozStorageConnection.h"
#include "mozStorageError.h"
#include "mozStoragePrivateHelpers.h"
#include "mozStorageStatementData.h"
#include "mozStorageAsyncStatementExecution.h"

namespace mozilla {
namespace storage {

/**
 * The following constants help batch rows into result sets.
 * MAX_MILLISECONDS_BETWEEN_RESULTS was chosen because any user-based task that
 * takes less than 200 milliseconds is considered to feel instantaneous to end
 * users.  MAX_ROWS_PER_RESULT was arbitrarily chosen to reduce the number of
 * dispatches to calling thread, while also providing reasonably-sized sets of
 * data for consumers.  Both of these constants are used because we assume that
 * consumers are trying to avoid blocking their execution thread for long
 * periods of time, and dispatching many small events to the calling thread will
 * end up blocking it.
 */
#define MAX_MILLISECONDS_BETWEEN_RESULTS 75
#define MAX_ROWS_PER_RESULT 15

////////////////////////////////////////////////////////////////////////////////
//// Local Classes

namespace {

typedef AsyncExecuteStatements::ExecutionState ExecutionState;

/**
 * Notifies a callback with a result set.
 */
class CallbackResultNotifier : public nsRunnable
{
public:
  CallbackResultNotifier(mozIStorageStatementCallback *aCallback,
                         mozIStorageResultSet *aResults,
                         AsyncExecuteStatements *aEventStatus) :
      mCallback(aCallback)
    , mResults(aResults)
    , mEventStatus(aEventStatus)
  {
  }

  NS_IMETHOD Run()
  {
    NS_ASSERTION(mCallback, "Trying to notify about results without a callback!");

    if (mEventStatus->shouldNotify())
      (void)mCallback->HandleResult(mResults);

    return NS_OK;
  }

private:
  mozIStorageStatementCallback *mCallback;
  nsCOMPtr<mozIStorageResultSet> mResults;
  nsRefPtr<AsyncExecuteStatements> mEventStatus;
};

/**
 * Notifies the calling thread that an error has occurred.
 */
class ErrorNotifier : public nsRunnable
{
public:
  ErrorNotifier(mozIStorageStatementCallback *aCallback,
                mozIStorageError *aErrorObj,
                AsyncExecuteStatements *aEventStatus) :
      mCallback(aCallback)
    , mErrorObj(aErrorObj)
    , mEventStatus(aEventStatus)
  {
  }

  NS_IMETHOD Run()
  {
    if (mEventStatus->shouldNotify() && mCallback)
      (void)mCallback->HandleError(mErrorObj);

    return NS_OK;
  }

private:
  mozIStorageStatementCallback *mCallback;
  nsCOMPtr<mozIStorageError> mErrorObj;
  nsRefPtr<AsyncExecuteStatements> mEventStatus;
};

/**
 * Notifies the calling thread that the statement has finished executing.  Keeps
 * the AsyncExecuteStatements instance alive long enough so that it does not
 * get destroyed on the async thread if there are no other references alive.
 */
class CompletionNotifier : public nsRunnable
{
public:
  /**
   * This takes ownership of the callback.  It is released on the thread this is
   * dispatched to (which should always be the calling thread).
   */
  CompletionNotifier(mozIStorageStatementCallback *aCallback,
                     ExecutionState aReason,
                     AsyncExecuteStatements *aKeepAsyncAlive)
    : mKeepAsyncAlive(aKeepAsyncAlive)
    , mCallback(aCallback)
    , mReason(aReason)
  {
  }

  NS_IMETHOD Run()
  {
    if (mCallback) {
      (void)mCallback->HandleCompletion(mReason);
      NS_RELEASE(mCallback);
    }

    return NS_OK;
  }

private:
  nsRefPtr<AsyncExecuteStatements> mKeepAsyncAlive;
  mozIStorageStatementCallback *mCallback;
  ExecutionState mReason;
};

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//// AsyncExecuteStatements

/* static */
nsresult
AsyncExecuteStatements::execute(StatementDataArray &aStatements,
                                Connection *aConnection,
                                mozIStorageStatementCallback *aCallback,
                                mozIStoragePendingStatement **_stmt)
{
  // Create our event to run in the background
  nsRefPtr<AsyncExecuteStatements> event =
    new AsyncExecuteStatements(aStatements, aConnection, aCallback);
  NS_ENSURE_TRUE(event, NS_ERROR_OUT_OF_MEMORY);

  // Dispatch it to the background
  nsIEventTarget *target = aConnection->getAsyncExecutionTarget();
  NS_ENSURE_TRUE(target, NS_ERROR_NOT_AVAILABLE);
  nsresult rv = target->Dispatch(event, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  // Return it as the pending statement object and track it.
  NS_ADDREF(*_stmt = event);
  return NS_OK;
}

AsyncExecuteStatements::AsyncExecuteStatements(StatementDataArray &aStatements,
                                               Connection *aConnection,
                                               mozIStorageStatementCallback *aCallback)
: mConnection(aConnection)
, mTransactionManager(nsnull)
, mCallback(aCallback)
, mCallingThread(::do_GetCurrentThread())
, mMaxWait(TimeDuration::FromMilliseconds(MAX_MILLISECONDS_BETWEEN_RESULTS))
, mIntervalStart(TimeStamp::Now())
, mState(PENDING)
, mCancelRequested(false)
, mMutex(aConnection->sharedAsyncExecutionMutex)
, mDBMutex(aConnection->sharedDBMutex)
{
  (void)mStatements.SwapElements(aStatements);
  NS_ASSERTION(mStatements.Length(), "We weren't given any statements!");
  NS_IF_ADDREF(mCallback);
}

bool
AsyncExecuteStatements::shouldNotify()
{
#ifdef DEBUG
  mMutex.AssertNotCurrentThreadOwns();

  PRBool onCallingThread = PR_FALSE;
  (void)mCallingThread->IsOnCurrentThread(&onCallingThread);
  NS_ASSERTION(onCallingThread, "runEvent not running on the calling thread!");
#endif

  // We do not need to acquire mMutex here because it can only ever be written
  // to on the calling thread, and the only thread that can call us is the
  // calling thread, so we know that our access is serialized.
  return !mCancelRequested;
}

bool
AsyncExecuteStatements::bindExecuteAndProcessStatement(StatementData &aData,
                                                       bool aLastStatement)
{
  mMutex.AssertNotCurrentThreadOwns();

  sqlite3_stmt *aStatement = nsnull;
  // This cannot fail; we are only called if it's available.
  (void)aData.getSqliteStatement(&aStatement);
  NS_ASSERTION(aStatement, "You broke the code; do not call here like that!");
  BindingParamsArray *paramsArray(aData);

  // Iterate through all of our parameters, bind them, and execute.
  bool continueProcessing = true;
  BindingParamsArray::iterator itr = paramsArray->begin();
  BindingParamsArray::iterator end = paramsArray->end();
  while (itr != end && continueProcessing) {
    // Bind the data to our statement.
    nsCOMPtr<IStorageBindingParamsInternal> bindingInternal = 
      do_QueryInterface(*itr);
    nsCOMPtr<mozIStorageError> error = bindingInternal->bind(aStatement);
    if (error) {
      // Set our error state.
      mState = ERROR;

      // And notify.
      (void)notifyError(error);
      return false;
    }

    // Advance our iterator, execute, and then process the statement.
    itr++;
    bool lastStatement = aLastStatement && itr == end;
    continueProcessing = executeAndProcessStatement(aStatement, lastStatement);

    // Always reset our statement.
    (void)::sqlite3_reset(aStatement);
  }

  return continueProcessing;
}

bool
AsyncExecuteStatements::executeAndProcessStatement(sqlite3_stmt *aStatement,
                                                   bool aLastStatement)
{
  mMutex.AssertNotCurrentThreadOwns();

  // Execute our statement
  bool hasResults;
  do {
    hasResults = executeStatement(aStatement);

    // If we had an error, bail.
    if (mState == ERROR)
      return false;

    // If we have been canceled, there is no point in going on...
    {
      MutexAutoLock lockedScope(mMutex);
      if (mCancelRequested) {
        mState = CANCELED;
        return false;
      }
    }

    // Build our result set and notify if we got anything back and have a
    // callback to notify.
    if (mCallback && hasResults &&
        NS_FAILED(buildAndNotifyResults(aStatement))) {
      // We had an error notifying, so we notify on error and stop processing.
      mState = ERROR;

      // Notify, and stop processing statements.
      (void)notifyError(mozIStorageError::ERROR,
                        "An error occurred while notifying about results");

      return false;
    }
  } while (hasResults);

#ifdef DEBUG
  // Check to make sure that this statement was smart about what it did.
  checkAndLogStatementPerformance(aStatement);
#endif

  // If we are done, we need to set our state accordingly while we still hold
  // our mutex.  We would have already returned if we were canceled or had
  // an error at this point.
  if (aLastStatement)
    mState = COMPLETED;

  return true;
}

bool
AsyncExecuteStatements::executeStatement(sqlite3_stmt *aStatement)
{
  mMutex.AssertNotCurrentThreadOwns();

  while (true) {
    // lock the sqlite mutex so sqlite3_errmsg cannot change
    SQLiteMutexAutoLock lockedScope(mDBMutex);

    int rc = stepStmt(aStatement);
    // Stop if we have no more results.
    if (rc == SQLITE_DONE)
      return false;

    // If we got results, we can return now.
    if (rc == SQLITE_ROW)
      return true;

    // Some errors are not fatal, and we can handle them and continue.
    if (rc == SQLITE_BUSY) {
      // Don't hold the lock while we call outside our module.
      SQLiteMutexAutoUnlock unlockedScope(mDBMutex);

      // Yield, and try again
      (void)::PR_Sleep(PR_INTERVAL_NO_WAIT);
      continue;
    }

    // Set an error state.
    mState = ERROR;

    // Construct the error message before giving up the mutex (which we cannot
    // hold during the call to notifyError).
    sqlite3 *db = mConnection->GetNativeConnection();
    nsCOMPtr<mozIStorageError> errorObj(new Error(rc, ::sqlite3_errmsg(db)));
    // We cannot hold the DB mutex while calling notifyError.
    SQLiteMutexAutoUnlock unlockedScope(mDBMutex);
    (void)notifyError(errorObj);

    // Finally, indicate that we should stop processing.
    return false;
  }
}

nsresult
AsyncExecuteStatements::buildAndNotifyResults(sqlite3_stmt *aStatement)
{
  NS_ASSERTION(mCallback, "Trying to dispatch results without a callback!");
  mMutex.AssertNotCurrentThreadOwns();

  // Build result object if we need it.
  if (!mResultSet)
    mResultSet = new ResultSet();
  NS_ENSURE_TRUE(mResultSet, NS_ERROR_OUT_OF_MEMORY);

  nsRefPtr<Row> row(new Row());
  NS_ENSURE_TRUE(row, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = row->initialize(aStatement);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mResultSet->add(row);
  NS_ENSURE_SUCCESS(rv, rv);

  // If we have hit our maximum number of allowed results, or if we have hit
  // the maximum amount of time we want to wait for results, notify the
  // calling thread about it.
  TimeStamp now = TimeStamp::Now();
  TimeDuration delta = now - mIntervalStart;
  if (mResultSet->rows() >= MAX_ROWS_PER_RESULT || delta > mMaxWait) {
    // Notify the caller
    rv = notifyResults();
    if (NS_FAILED(rv))
      return NS_OK; // we'll try again with the next result

    // Reset our start time
    mIntervalStart = now;
  }

  return NS_OK;
}

nsresult
AsyncExecuteStatements::notifyComplete()
{
  mMutex.AssertNotCurrentThreadOwns();
  NS_ASSERTION(mState != PENDING,
               "Still in a pending state when calling Complete!");

  // Finalize our statements before we try to commit or rollback.  If we are
  // canceling and have statements that think they have pending work, the
  // rollback will fail.
  for (PRUint32 i = 0; i < mStatements.Length(); i++)
    mStatements[i].finalize();

  // Handle our transaction, if we have one
  if (mTransactionManager) {
    if (mState == COMPLETED) {
      nsresult rv = mTransactionManager->Commit();
      if (NS_FAILED(rv)) {
        mState = ERROR;
        (void)notifyError(mozIStorageError::ERROR,
                          "Transaction failed to commit");
      }
    }
    else {
      (void)mTransactionManager->Rollback();
    }
    delete mTransactionManager;
    mTransactionManager = nsnull;
  }

  // Always generate a completion notification; it is what guarantees that our
  // destruction does not happen here on the async thread.
  nsRefPtr<CompletionNotifier> completionEvent =
    new CompletionNotifier(mCallback, mState, this);
  NS_ENSURE_TRUE(completionEvent, NS_ERROR_OUT_OF_MEMORY);

  // We no longer own mCallback (the CompletionNotifier takes ownership).
  mCallback = nsnull;

  (void)mCallingThread->Dispatch(completionEvent, NS_DISPATCH_NORMAL);

  return NS_OK;
}

nsresult
AsyncExecuteStatements::notifyError(PRInt32 aErrorCode,
                                    const char *aMessage)
{
  mMutex.AssertNotCurrentThreadOwns();
  mDBMutex.assertNotCurrentThreadOwns();

  if (!mCallback)
    return NS_OK;

  nsCOMPtr<mozIStorageError> errorObj(new Error(aErrorCode, aMessage));
  NS_ENSURE_TRUE(errorObj, NS_ERROR_OUT_OF_MEMORY);

  return notifyError(errorObj);
}

nsresult
AsyncExecuteStatements::notifyError(mozIStorageError *aError)
{
  mMutex.AssertNotCurrentThreadOwns();
  mDBMutex.assertNotCurrentThreadOwns();

  if (!mCallback)
    return NS_OK;

  nsRefPtr<ErrorNotifier> notifier =
    new ErrorNotifier(mCallback, aError, this);
  NS_ENSURE_TRUE(notifier, NS_ERROR_OUT_OF_MEMORY);

  return mCallingThread->Dispatch(notifier, NS_DISPATCH_NORMAL);
}

nsresult
AsyncExecuteStatements::notifyResults()
{
  mMutex.AssertNotCurrentThreadOwns();
  NS_ASSERTION(mCallback, "notifyResults called without a callback!");

  nsRefPtr<CallbackResultNotifier> notifier =
    new CallbackResultNotifier(mCallback, mResultSet, this);
  NS_ENSURE_TRUE(notifier, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = mCallingThread->Dispatch(notifier, NS_DISPATCH_NORMAL);
  if (NS_SUCCEEDED(rv))
    mResultSet = nsnull; // we no longer own it on success
  return rv;
}

NS_IMPL_THREADSAFE_ISUPPORTS2(
  AsyncExecuteStatements,
  nsIRunnable,
  mozIStoragePendingStatement
)

////////////////////////////////////////////////////////////////////////////////
//// mozIStoragePendingStatement

NS_IMETHODIMP
AsyncExecuteStatements::Cancel()
{
#ifdef DEBUG
  PRBool onCallingThread = PR_FALSE;
  (void)mCallingThread->IsOnCurrentThread(&onCallingThread);
  NS_ASSERTION(onCallingThread, "Not canceling from the calling thread!");
#endif

  // If we have already canceled, we have an error, but always indicate that
  // we are trying to cancel.
  NS_ENSURE_FALSE(mCancelRequested, NS_ERROR_UNEXPECTED);

  {
    MutexAutoLock lockedScope(mMutex);

    // We need to indicate that we want to try and cancel now.
    mCancelRequested = true;
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// nsIRunnable

NS_IMETHODIMP
AsyncExecuteStatements::Run()
{
  // Do not run if we have been canceled.
  {
    MutexAutoLock lockedScope(mMutex);
    if (mCancelRequested)
      mState = CANCELED;
  }
  if (mState == CANCELED)
    return notifyComplete();

  // If there is more than one statement, run it in a transaction.  We assume
  // that we have been given write statements since getting a batch of read
  // statements doesn't make a whole lot of sense.
  // Additionally, if we have only one statement and it needs a transaction, we
  // will wrap it in one.
  if (mStatements.Length() > 1 || mStatements[0].needsTransaction()) {
    // We don't error if this failed because it's not terrible if it does.
    mTransactionManager = new mozStorageTransaction(mConnection, PR_FALSE,
                                                    mozIStorageConnection::TRANSACTION_IMMEDIATE);
  }

  // Execute each statement, giving the callback results if it returns any.
  for (PRUint32 i = 0; i < mStatements.Length(); i++) {
    bool finished = (i == (mStatements.Length() - 1));

    sqlite3_stmt *stmt;
    { // lock the sqlite mutex so sqlite3_errmsg cannot change
      SQLiteMutexAutoLock lockedScope(mDBMutex);

      int rc = mStatements[i].getSqliteStatement(&stmt);
      if (rc != SQLITE_OK) {
        // Set our error state.
        mState = ERROR;

        // Build the error object; can't call notifyError with the lock held
        sqlite3 *db = mConnection->GetNativeConnection();
        nsCOMPtr<mozIStorageError> errorObj(
          new Error(rc, ::sqlite3_errmsg(db))
        );
        {
          // We cannot hold the DB mutex and call notifyError.
          SQLiteMutexAutoUnlock unlockedScope(mDBMutex);
          (void)notifyError(errorObj);
        }
        break;
      }
    }

    // If we have parameters to bind, bind them, execute, and process.
    if (mStatements[i].hasParametersToBeBound()) {
      if (!bindExecuteAndProcessStatement(mStatements[i], finished))
        break;
    }
    // Otherwise, just execute and process the statement.
    else if (!executeAndProcessStatement(stmt, finished)) {
      break;
    }
  }

  // If we still have results that we haven't notified about, take care of
  // them now.
  if (mResultSet)
    (void)notifyResults();

  // Notify about completion
  return notifyComplete();
}

} // namespace storage
} // namespace mozilla
