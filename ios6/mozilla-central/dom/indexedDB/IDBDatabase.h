/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_indexeddb_idbdatabase_h__
#define mozilla_dom_indexeddb_idbdatabase_h__

#include "mozilla/dom/indexedDB/IndexedDatabase.h"

#include "nsIDocument.h"
#include "nsIFileStorage.h"
#include "nsIIDBDatabase.h"
#include "nsDOMEventTargetHelper.h"
#include "mozilla/dom/indexedDB/IDBWrapperCache.h"
#include "mozilla/dom/indexedDB/FileManager.h"

class nsIScriptContext;
class nsPIDOMWindow;

namespace mozilla {
namespace dom {
class ContentParent;
}
}

BEGIN_INDEXEDDB_NAMESPACE

class AsyncConnectionHelper;
struct DatabaseInfo;
class IDBFactory;
class IDBIndex;
class IDBObjectStore;
class IDBTransaction;
class IndexedDatabaseManager;
class IndexedDBDatabaseChild;
class IndexedDBDatabaseParent;
struct ObjectStoreInfoGuts;

class IDBDatabase : public IDBWrapperCache,
                    public nsIIDBDatabase,
                    public nsIFileStorage
{
  friend class AsyncConnectionHelper;
  friend class IndexedDatabaseManager;
  friend class IndexedDBDatabaseChild;

public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIIDBDATABASE
  NS_DECL_NSIFILESTORAGE

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(IDBDatabase, IDBWrapperCache)

  static already_AddRefed<IDBDatabase>
  Create(IDBWrapperCache* aOwnerCache,
         IDBFactory* aFactory,
         already_AddRefed<DatabaseInfo> aDatabaseInfo,
         const nsACString& aASCIIOrigin,
         FileManager* aFileManager,
         mozilla::dom::ContentParent* aContentParent);

  // nsIDOMEventTarget
  virtual nsresult PostHandleEvent(nsEventChainPostVisitor& aVisitor);

  nsIAtom* Id() const
  {
    return mDatabaseId;
  }

  DatabaseInfo* Info() const
  {
    return mDatabaseInfo;
  }

  const nsString& Name() const
  {
    return mName;
  }

  const nsString& FilePath() const
  {
    return mFilePath;
  }

  already_AddRefed<nsIDocument> GetOwnerDocument()
  {
    if (!GetOwner()) {
      return nullptr;
    }

    nsCOMPtr<nsIDocument> doc =
      do_QueryInterface(GetOwner()->GetExtantDocument());
    return doc.forget();
  }

  const nsCString& Origin() const
  {
    return mASCIIOrigin;
  }

  void Invalidate();

  // Whether or not the database has been invalidated. If it has then no further
  // transactions for this database will be allowed to run. This function may be
  // called on any thread.
  bool IsInvalidated() const
  {
    return mInvalidated;
  }

  void DisconnectFromActorParent();

  void CloseInternal(bool aIsDead);

  // Whether or not the database has had Close called on it.
  bool IsClosed() const;

  void EnterSetVersionTransaction();
  void ExitSetVersionTransaction();

  // Called when a versionchange transaction is aborted to reset the
  // DatabaseInfo.
  void RevertToPreviousState();

  FileManager* Manager() const
  {
    return mFileManager;
  }

  void
  SetActor(IndexedDBDatabaseChild* aActorChild)
  {
    NS_ASSERTION(!aActorChild || !mActorChild, "Shouldn't have more than one!");
    mActorChild = aActorChild;
  }

  void
  SetActor(IndexedDBDatabaseParent* aActorParent)
  {
    NS_ASSERTION(!aActorParent || !mActorParent,
                 "Shouldn't have more than one!");
    mActorParent = aActorParent;
  }

  IndexedDBDatabaseChild*
  GetActorChild() const
  {
    return mActorChild;
  }

  IndexedDBDatabaseParent*
  GetActorParent() const
  {
    return mActorParent;
  }

  mozilla::dom::ContentParent*
  GetContentParent() const
  {
    return mContentParent;
  }

  nsresult
  CreateObjectStoreInternal(IDBTransaction* aTransaction,
                            const ObjectStoreInfoGuts& aInfo,
                            IDBObjectStore** _retval);

private:
  IDBDatabase();
  ~IDBDatabase();

  void OnUnlink();

  // The factory must be kept alive when IndexedDB is used in multiple
  // processes. If it dies then the entire actor tree will be destroyed with it
  // and the world will explode.
  nsRefPtr<IDBFactory> mFactory;

  nsRefPtr<DatabaseInfo> mDatabaseInfo;

  // Set to a copy of the existing DatabaseInfo when starting a versionchange
  // transaction.
  nsRefPtr<DatabaseInfo> mPreviousDatabaseInfo;
  nsCOMPtr<nsIAtom> mDatabaseId;
  nsString mName;
  nsString mFilePath;
  nsCString mASCIIOrigin;

  nsRefPtr<FileManager> mFileManager;

  IndexedDBDatabaseChild* mActorChild;
  IndexedDBDatabaseParent* mActorParent;

  mozilla::dom::ContentParent* mContentParent;

  bool mInvalidated;
  bool mRegistered;
  bool mClosed;
  bool mRunningVersionChange;
};

END_INDEXEDDB_NAMESPACE

#endif // mozilla_dom_indexeddb_idbdatabase_h__
