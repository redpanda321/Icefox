/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 sts=2 expandtab
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
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Andrew Sutherland <asutherland@asutherland.org>
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

#include "StorageBaseStatementInternal.h"

#include "nsProxyRelease.h"

#include "mozStorageBindingParamsArray.h"
#include "mozStorageStatementData.h"
#include "mozStorageAsyncStatementExecution.h"

namespace mozilla {
namespace storage {

////////////////////////////////////////////////////////////////////////////////
//// Local Classes

/**
 * Used to finalize an asynchronous statement on the background thread.
 */
class AsyncStatementFinalizer : public nsRunnable
{
public:
  /**
   * Constructor for the event.
   *
   * @param aStatement
   *        We need the AsyncStatement to be able to get at the sqlite3_stmt;
   *        we only access/create it on the async thread.
   * @param aConnection
   *        We need the connection to know what thread to release the statement
   *        on.  We release the statement on that thread since releasing the
   *        statement might end up releasing the connection too.
   */
  AsyncStatementFinalizer(StorageBaseStatementInternal *aStatement,
                          Connection *aConnection)
  : mStatement(aStatement)
  , mConnection(aConnection)
  {
  }

  NS_IMETHOD Run()
  {
    mStatement->internalAsyncFinalize();
    (void)::NS_ProxyRelease(mConnection->threadOpenedOn, mStatement);
    return NS_OK;
  }
private:
  // It is vital that this remain an nsCOMPtr for NS_ProxyRelease's benefit.
  nsCOMPtr<StorageBaseStatementInternal> mStatement;
  nsRefPtr<Connection> mConnection;
};

////////////////////////////////////////////////////////////////////////////////
//// StorageBaseStatementInternal

StorageBaseStatementInternal::StorageBaseStatementInternal()
: mAsyncStatement(NULL)
{
}

void
StorageBaseStatementInternal::asyncFinalize()
{
  nsIEventTarget *target = mDBConnection->getAsyncExecutionTarget();
  if (!target) {
    // If we cannot get the background thread, we have to assume it has been
    // shutdown (or is in the process of doing so).  As a result, we should
    // just finalize it here and now.
    internalAsyncFinalize();
  }
  else {
    nsCOMPtr<nsIRunnable> event =
      new AsyncStatementFinalizer(this, mDBConnection);

    // If the dispatching did not go as planned, finalize now.
    if (!event ||
        NS_FAILED(target->Dispatch(event, NS_DISPATCH_NORMAL))) {
      internalAsyncFinalize();
    }
  }
}

void
StorageBaseStatementInternal::internalAsyncFinalize()
{
  if (mAsyncStatement) {
    (void)::sqlite3_finalize(mAsyncStatement);
    mAsyncStatement = nsnull;
  }
}

NS_IMETHODIMP
StorageBaseStatementInternal::NewBindingParamsArray(
  mozIStorageBindingParamsArray **_array
)
{
  nsCOMPtr<mozIStorageBindingParamsArray> array = new BindingParamsArray(this);
  NS_ENSURE_TRUE(array, NS_ERROR_OUT_OF_MEMORY);

  array.forget(_array);
  return NS_OK;
}

NS_IMETHODIMP
StorageBaseStatementInternal::ExecuteAsync(
  mozIStorageStatementCallback *aCallback,
  mozIStoragePendingStatement **_stmt
)
{
  // We used to call Connection::ExecuteAsync but it takes a
  // mozIStorageBaseStatement signature because it is also a public API.  Since
  // our 'this' has no static concept of mozIStorageBaseStatement and Connection
  // would just QI it back across to a StorageBaseStatementInternal and the
  // actual logic is very simple, we now roll our own.
  nsTArray<StatementData> stmts(1);
  StatementData data;
  nsresult rv = getAsynchronousStatementData(data);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(stmts.AppendElement(data), NS_ERROR_OUT_OF_MEMORY);

  // Dispatch to the background
  return AsyncExecuteStatements::execute(stmts, mDBConnection, aCallback,
                                         _stmt);
}

NS_IMETHODIMP
StorageBaseStatementInternal::EscapeStringForLIKE(
  const nsAString &aValue,
  const PRUnichar aEscapeChar,
  nsAString &_escapedString
)
{
  const PRUnichar MATCH_ALL('%');
  const PRUnichar MATCH_ONE('_');

  _escapedString.Truncate(0);

  for (PRUint32 i = 0; i < aValue.Length(); i++) {
    if (aValue[i] == aEscapeChar || aValue[i] == MATCH_ALL ||
        aValue[i] == MATCH_ONE) {
      _escapedString += aEscapeChar;
    }
    _escapedString += aValue[i];
  }
  return NS_OK;
}

} // namespace storage
} // namespace mozilla
