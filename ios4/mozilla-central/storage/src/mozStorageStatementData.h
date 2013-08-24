/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 sts=2 et
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
 * Portions created by the Initial Developer are Copyright (C) 2009
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

#ifndef mozStorageStatementData_h
#define mozStorageStatementData_h

#include "sqlite3.h"

#include "nsAutoPtr.h"
#include "nsTArray.h"

#include "mozStorageBindingParamsArray.h"
#include "mozIStorageBaseStatement.h"
#include "mozStorageConnection.h"
#include "StorageBaseStatementInternal.h"

struct sqlite3_stmt;

namespace mozilla {
namespace storage {

class StatementData
{
public:
  StatementData(sqlite3_stmt *aStatement,
                already_AddRefed<BindingParamsArray> aParamsArray,
                StorageBaseStatementInternal *aStatementOwner)
  : mStatement(aStatement)
  , mParamsArray(aParamsArray)
  , mStatementOwner(aStatementOwner)
  {
  }
  StatementData(const StatementData &aSource)
  : mStatement(aSource.mStatement)
  , mParamsArray(aSource.mParamsArray)
  , mStatementOwner(aSource.mStatementOwner)
  {
  }
  StatementData()
  {
  }

  /**
   * Return the sqlite statement, fetching it from the storage statement.  In
   * the case of AsyncStatements this may actually create the statement 
   */
  inline int getSqliteStatement(sqlite3_stmt **_stmt)
  {
    if (!mStatement) {
      int rc = mStatementOwner->getAsyncStatement(&mStatement);
      NS_ENSURE_TRUE(rc == SQLITE_OK, rc);
    }
    *_stmt = mStatement;
    return SQLITE_OK;
  }

  operator BindingParamsArray *() const { return mParamsArray; }

  /**
   * Provide the ability to coerce back to a sqlite3 * connection for purposes 
   * of getting an error message out of it.
   */
  operator sqlite3 *() const
  {
    return mStatementOwner->getOwner()->GetNativeConnection();
  }

  /**
   * NULLs out our sqlite3_stmt (it is held by the owner) after reseting it and
   * clear all bindings to it.  This is expected to occur on the async thread.
   *
   * We do not clear mParamsArray out because we only want to release
   * mParamsArray on the calling thread because of XPCVariant addref/release
   * thread-safety issues.  The same holds for mStatementOwner which can be
   * holding such a reference chain as well.
   */
  inline void finalize()
  {
    // In the AsyncStatement case we may never have populated mStatement if the
    // AsyncExecuteStatements got canceled or a failure occurred in constructing
    // the statement.
    if (mStatement) {
      (void)::sqlite3_reset(mStatement);
      (void)::sqlite3_clear_bindings(mStatement);
      mStatement = NULL;
    }
  }

  /**
   * Indicates if this statement has parameters to be bound before it is
   * executed.
   *
   * @return true if the statement has parameters to bind against, false
   *         otherwise.
   */
  inline bool hasParametersToBeBound() const { return !!mParamsArray; }
  /**
   * Indicates if this statement needs a transaction for execution.
   *
   * @return true if the statement needs a transaction, false otherwise.
   */
  inline bool needsTransaction() const
  {
    return mParamsArray != nsnull && mParamsArray->length() > 1;
  }

private:
  sqlite3_stmt *mStatement;
  nsRefPtr<BindingParamsArray> mParamsArray;

  /**
   * We hold onto a reference of the statement's owner so it doesn't get
   * destroyed out from under us.
   */
  nsCOMPtr<StorageBaseStatementInternal> mStatementOwner;
};

} // namespace storage
} // namespace mozilla

#endif // mozStorageStatementData_h
