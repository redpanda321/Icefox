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

#ifndef mozilla_dom_indexeddb_idbtransaction_h__
#define mozilla_dom_indexeddb_idbtransaction_h__

#include "mozilla/dom/indexedDB/IDBRequest.h"
#include "mozilla/dom/indexedDB/IDBDatabase.h"

#include "nsIIDBTransaction.h"
#include "nsIRunnable.h"

#include "nsDOMEventTargetHelper.h"
#include "nsCycleCollectionParticipant.h"

#include "nsAutoPtr.h"
#include "nsHashKeys.h"
#include "nsInterfaceHashtable.h"

class nsIScriptContext;
class nsIThread;
class nsPIDOMWindow;

BEGIN_INDEXEDDB_NAMESPACE

class AsyncConnectionHelper;
class CommitHelper;
struct ObjectStoreInfo;
class TransactionThreadPool;

class IDBTransaction : public nsDOMEventTargetHelper,
                       public IDBRequest::Generator,
                       public nsIIDBTransaction
{
  friend class AsyncConnectionHelper;
  friend class CommitHelper;
  friend class TransactionThreadPool;

public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIIDBTRANSACTION

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(IDBTransaction,
                                           nsDOMEventTargetHelper)

  static already_AddRefed<IDBTransaction>
  Create(JSContext* aCx,
         IDBDatabase* aDatabase,
         nsTArray<nsString>& aObjectStoreNames,
         PRUint16 aMode,
         PRUint32 aTimeout);

  void OnNewRequest();
  void OnRequestFinished();

  bool StartSavepoint();
  nsresult ReleaseSavepoint();
  void RollbackSavepoint();

  already_AddRefed<mozIStorageStatement>
  AddStatement(bool aCreate,
               bool aOverwrite,
               bool aAutoIncrement);

  already_AddRefed<mozIStorageStatement>
  RemoveStatement(bool aAutoIncrement);

  already_AddRefed<mozIStorageStatement>
  GetStatement(bool aAutoIncrement);

  already_AddRefed<mozIStorageStatement>
  IndexGetStatement(bool aUnique,
                    bool aAutoIncrement);

  already_AddRefed<mozIStorageStatement>
  IndexGetObjectStatement(bool aUnique,
                          bool aAutoIncrement);

  already_AddRefed<mozIStorageStatement>
  IndexUpdateStatement(bool aAutoIncrement,
                       bool aUnique,
                       bool aOverwrite);

  already_AddRefed<mozIStorageStatement>
  GetCachedStatement(const nsACString& aQuery);

  template<int N>
  already_AddRefed<mozIStorageStatement>
  GetCachedStatement(const char (&aQuery)[N])
  {
    nsCString query;
    query.AssignLiteral(aQuery);
    return GetCachedStatement(query);
  }

#ifdef DEBUG
  bool TransactionIsOpen() const;
  bool IsWriteAllowed() const;
#else
  bool TransactionIsOpen() const
  {
    return mReadyState == nsIIDBTransaction::INITIAL ||
           mReadyState == nsIIDBTransaction::LOADING;
  }

  bool IsWriteAllowed() const
  {
    return mMode == nsIIDBTransaction::READ_WRITE;
  }
#endif

  enum { FULL_LOCK = nsIIDBTransaction::SNAPSHOT_READ + 1 };

  nsIScriptContext* ScriptContext()
  {
    return mScriptContext;
  }

  nsPIDOMWindow* Owner()
  {
    return mOwner;
  }

private:
  IDBTransaction();
  ~IDBTransaction();

  // Only meant to be called on mStorageThread!
  nsresult GetOrCreateConnection(mozIStorageConnection** aConnection);

  nsresult CommitOrRollback();

  nsRefPtr<IDBDatabase> mDatabase;
  nsTArray<nsString> mObjectStoreNames;
  PRUint16 mReadyState;
  PRUint16 mMode;
  PRUint32 mTimeout;
  PRUint32 mPendingRequests;

  // Only touched on the main thread.
  nsRefPtr<nsDOMEventListenerWrapper> mOnCompleteListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnAbortListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnTimeoutListener;

  nsInterfaceHashtable<nsCStringHashKey, mozIStorageStatement>
    mCachedStatements;

  // Only touched on the database thread.
  nsCOMPtr<mozIStorageConnection> mConnection;

  // Only touched on the database thread.
  PRUint32 mSavepointCount;

  bool mHasInitialSavepoint;
  bool mAborted;
};

class CommitHelper : public nsIRunnable
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIRUNNABLE

  CommitHelper(IDBTransaction* aTransaction)
  : mTransaction(aTransaction),
    mAborted(!!aTransaction->mAborted),
    mHasInitialSavepoint(!!aTransaction->mHasInitialSavepoint)
  {
    mConnection.swap(aTransaction->mConnection);
  }

  template<class T>
  bool AddDoomedObject(nsCOMPtr<T>& aCOMPtr)
  {
    if (aCOMPtr) {
      if (!mDoomedObjects.AppendElement(do_QueryInterface(aCOMPtr))) {
        NS_ERROR("Out of memory!");
        return false;
      }
      aCOMPtr = nsnull;
    }
    return true;
  }

private:
  nsRefPtr<IDBTransaction> mTransaction;
  nsCOMPtr<mozIStorageConnection> mConnection;
  nsAutoTArray<nsCOMPtr<nsISupports>, 10> mDoomedObjects;
  bool mAborted;
  bool mHasInitialSavepoint;
};

END_INDEXEDDB_NAMESPACE

#endif // mozilla_dom_indexeddb_idbtransaction_h__
