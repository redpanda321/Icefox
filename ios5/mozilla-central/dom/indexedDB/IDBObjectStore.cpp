/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "IDBObjectStore.h"

#include "nsIJSContextStack.h"
#include "nsIOutputStream.h"

#include "jsfriendapi.h"
#include "mozilla/dom/StructuredCloneTags.h"
#include "mozilla/storage.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsDOMFile.h"
#include "nsDOMLists.h"
#include "nsEventDispatcher.h"
#include "nsJSUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "snappy/snappy.h"
#include "test_quota.h"

#include "AsyncConnectionHelper.h"
#include "FileStream.h"
#include "IDBCursor.h"
#include "IDBEvents.h"
#include "IDBFileHandle.h"
#include "IDBIndex.h"
#include "IDBKeyRange.h"
#include "IDBTransaction.h"
#include "DatabaseInfo.h"
#include "DictionaryHelpers.h"
#include "KeyPath.h"

#include "ipc/IndexedDBChild.h"
#include "ipc/IndexedDBParent.h"

#include "IndexedDatabaseInlines.h"

#define FILE_COPY_BUFFER_SIZE 32768

USING_INDEXEDDB_NAMESPACE
using namespace mozilla::dom::indexedDB::ipc;

namespace {

inline
bool
IgnoreNothing(PRUnichar c)
{
  return false;
}

class ObjectStoreHelper : public AsyncConnectionHelper
{
public:
  ObjectStoreHelper(IDBTransaction* aTransaction,
                    IDBRequest* aRequest,
                    IDBObjectStore* aObjectStore)
  : AsyncConnectionHelper(aTransaction, aRequest), mObjectStore(aObjectStore),
    mActor(nsnull)
  {
    NS_ASSERTION(aTransaction, "Null transaction!");
    NS_ASSERTION(aRequest, "Null request!");
    NS_ASSERTION(aObjectStore, "Null object store!");
  }

  virtual void ReleaseMainThreadObjects() MOZ_OVERRIDE;

  virtual nsresult Dispatch(nsIEventTarget* aDatabaseThread) MOZ_OVERRIDE;

  virtual nsresult
  PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams) = 0;

  virtual nsresult
  UnpackResponseFromParentProcess(const ResponseValue& aResponseValue) = 0;

protected:
  nsRefPtr<IDBObjectStore> mObjectStore;

private:
  IndexedDBObjectStoreRequestChild* mActor;
};

class NoRequestObjectStoreHelper : public AsyncConnectionHelper
{
public:
  NoRequestObjectStoreHelper(IDBTransaction* aTransaction,
                             IDBObjectStore* aObjectStore)
  : AsyncConnectionHelper(aTransaction, nsnull), mObjectStore(aObjectStore)
  {
    NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");
    NS_ASSERTION(aTransaction, "Null transaction!");
    NS_ASSERTION(aObjectStore, "Null object store!");
  }

  virtual void ReleaseMainThreadObjects() MOZ_OVERRIDE;

  virtual nsresult UnpackResponseFromParentProcess(
                                            const ResponseValue& aResponseValue)
                                            MOZ_OVERRIDE;

  virtual ChildProcessSendResult
  MaybeSendResponseToChildProcess(nsresult aResultCode) MOZ_OVERRIDE;

  virtual nsresult OnSuccess() MOZ_OVERRIDE;

  virtual void OnError() MOZ_OVERRIDE;

protected:
  nsRefPtr<IDBObjectStore> mObjectStore;
};

class AddHelper : public ObjectStoreHelper
{
public:
  AddHelper(IDBTransaction* aTransaction,
            IDBRequest* aRequest,
            IDBObjectStore* aObjectStore,
            StructuredCloneWriteInfo& aCloneWriteInfo,
            const Key& aKey,
            bool aOverwrite,
            nsTArray<IndexUpdateInfo>& aIndexUpdateInfo)
  : ObjectStoreHelper(aTransaction, aRequest, aObjectStore), mKey(aKey),
    mOverwrite(aOverwrite)
  {
    mCloneWriteInfo.Swap(aCloneWriteInfo);
    mIndexUpdateInfo.SwapElements(aIndexUpdateInfo);
  }

  ~AddHelper()
  {
    IDBObjectStore::ClearStructuredCloneBuffer(mCloneWriteInfo.mCloneBuffer);
  }

  virtual nsresult DoDatabaseWork(mozIStorageConnection* aConnection)
                                  MOZ_OVERRIDE;

  virtual nsresult GetSuccessResult(JSContext* aCx,
                                    jsval* aVal) MOZ_OVERRIDE;

  virtual void ReleaseMainThreadObjects() MOZ_OVERRIDE;

  virtual nsresult
  PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams) MOZ_OVERRIDE;

  virtual ChildProcessSendResult
  MaybeSendResponseToChildProcess(nsresult aResultCode) MOZ_OVERRIDE;

  virtual nsresult
  UnpackResponseFromParentProcess(const ResponseValue& aResponseValue)
                                  MOZ_OVERRIDE;

private:
  // These may change in the autoincrement case.
  StructuredCloneWriteInfo mCloneWriteInfo;
  Key mKey;
  nsTArray<IndexUpdateInfo> mIndexUpdateInfo;
  const bool mOverwrite;
};

class GetHelper : public ObjectStoreHelper
{
public:
  GetHelper(IDBTransaction* aTransaction,
            IDBRequest* aRequest,
            IDBObjectStore* aObjectStore,
            IDBKeyRange* aKeyRange)
  : ObjectStoreHelper(aTransaction, aRequest, aObjectStore),
    mKeyRange(aKeyRange)
  {
    NS_ASSERTION(aKeyRange, "Null key range!");
  }

  ~GetHelper()
  {
    IDBObjectStore::ClearStructuredCloneBuffer(mCloneReadInfo.mCloneBuffer);
  }

  virtual nsresult DoDatabaseWork(mozIStorageConnection* aConnection)
                                  MOZ_OVERRIDE;

  virtual nsresult GetSuccessResult(JSContext* aCx,
                                    jsval* aVal) MOZ_OVERRIDE;

  virtual void ReleaseMainThreadObjects() MOZ_OVERRIDE;

  virtual nsresult
  PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams) MOZ_OVERRIDE;

  virtual ChildProcessSendResult
  MaybeSendResponseToChildProcess(nsresult aResultCode) MOZ_OVERRIDE;

  virtual nsresult
  UnpackResponseFromParentProcess(const ResponseValue& aResponseValue)
                                  MOZ_OVERRIDE;

protected:
  // In-params.
  nsRefPtr<IDBKeyRange> mKeyRange;

private:
  // Out-params.
  StructuredCloneReadInfo mCloneReadInfo;
};

class DeleteHelper : public GetHelper
{
public:
  DeleteHelper(IDBTransaction* aTransaction,
               IDBRequest* aRequest,
               IDBObjectStore* aObjectStore,
               IDBKeyRange* aKeyRange)
  : GetHelper(aTransaction, aRequest, aObjectStore, aKeyRange)
  { }

  virtual nsresult DoDatabaseWork(mozIStorageConnection* aConnection)
                                  MOZ_OVERRIDE;

  virtual nsresult GetSuccessResult(JSContext* aCx,
                                    jsval* aVal) MOZ_OVERRIDE;

  virtual nsresult
  PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams) MOZ_OVERRIDE;

  virtual ChildProcessSendResult
  MaybeSendResponseToChildProcess(nsresult aResultCode) MOZ_OVERRIDE;

  virtual nsresult
  UnpackResponseFromParentProcess(const ResponseValue& aResponseValue)
                                  MOZ_OVERRIDE;
};

class ClearHelper : public ObjectStoreHelper
{
public:
  ClearHelper(IDBTransaction* aTransaction,
              IDBRequest* aRequest,
              IDBObjectStore* aObjectStore)
  : ObjectStoreHelper(aTransaction, aRequest, aObjectStore)
  { }

  virtual nsresult DoDatabaseWork(mozIStorageConnection* aConnection)
                                  MOZ_OVERRIDE;

  virtual nsresult
  PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams) MOZ_OVERRIDE;

  virtual ChildProcessSendResult
  MaybeSendResponseToChildProcess(nsresult aResultCode) MOZ_OVERRIDE;

  virtual nsresult
  UnpackResponseFromParentProcess(const ResponseValue& aResponseValue)
                                  MOZ_OVERRIDE;
};

class OpenCursorHelper : public ObjectStoreHelper
{
public:
  OpenCursorHelper(IDBTransaction* aTransaction,
                   IDBRequest* aRequest,
                   IDBObjectStore* aObjectStore,
                   IDBKeyRange* aKeyRange,
                   IDBCursor::Direction aDirection)
  : ObjectStoreHelper(aTransaction, aRequest, aObjectStore),
    mKeyRange(aKeyRange), mDirection(aDirection)
  { }

  ~OpenCursorHelper()
  {
    IDBObjectStore::ClearStructuredCloneBuffer(mCloneReadInfo.mCloneBuffer);
  }

  virtual nsresult DoDatabaseWork(mozIStorageConnection* aConnection)
                                  MOZ_OVERRIDE;

  virtual nsresult GetSuccessResult(JSContext* aCx,
                                    jsval* aVal) MOZ_OVERRIDE;

  virtual void ReleaseMainThreadObjects() MOZ_OVERRIDE;

  virtual nsresult
  PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams) MOZ_OVERRIDE;

  virtual ChildProcessSendResult
  MaybeSendResponseToChildProcess(nsresult aResultCode) MOZ_OVERRIDE;

  virtual nsresult
  UnpackResponseFromParentProcess(const ResponseValue& aResponseValue)
                                  MOZ_OVERRIDE;

private:
  nsresult EnsureCursor();

  // In-params.
  nsRefPtr<IDBKeyRange> mKeyRange;
  const IDBCursor::Direction mDirection;

  // Out-params.
  Key mKey;
  StructuredCloneReadInfo mCloneReadInfo;
  nsCString mContinueQuery;
  nsCString mContinueToQuery;
  Key mRangeKey;

  // Only used in the parent process.
  nsRefPtr<IDBCursor> mCursor;
  SerializedStructuredCloneReadInfo mSerializedCloneReadInfo;
};

class CreateIndexHelper : public NoRequestObjectStoreHelper
{
public:
  CreateIndexHelper(IDBTransaction* aTransaction, IDBIndex* aIndex)
  : NoRequestObjectStoreHelper(aTransaction, aIndex->ObjectStore()),
    mIndex(aIndex)
  {
    if (sTLSIndex == BAD_TLS_INDEX) {
      PR_NewThreadPrivateIndex(&sTLSIndex, DestroyTLSEntry);
    }

    NS_ASSERTION(sTLSIndex != BAD_TLS_INDEX,
                 "PR_NewThreadPrivateIndex failed!");
  }

  virtual nsresult DoDatabaseWork(mozIStorageConnection* aConnection)
                                  MOZ_OVERRIDE;

  virtual void ReleaseMainThreadObjects() MOZ_OVERRIDE;

private:
  nsresult InsertDataFromObjectStore(mozIStorageConnection* aConnection);

  static void DestroyTLSEntry(void* aPtr);

  static PRUintn sTLSIndex;

  // In-params.
  nsRefPtr<IDBIndex> mIndex;
};

PRUintn CreateIndexHelper::sTLSIndex = PRUintn(BAD_TLS_INDEX);

class DeleteIndexHelper : public NoRequestObjectStoreHelper
{
public:
  DeleteIndexHelper(IDBTransaction* aTransaction,
                    IDBObjectStore* aObjectStore,
                    const nsAString& aName)
  : NoRequestObjectStoreHelper(aTransaction, aObjectStore), mName(aName)
  { }

  virtual nsresult DoDatabaseWork(mozIStorageConnection* aConnection)
                                  MOZ_OVERRIDE;

private:
  // In-params
  nsString mName;
};

class GetAllHelper : public ObjectStoreHelper
{
public:
  GetAllHelper(IDBTransaction* aTransaction,
               IDBRequest* aRequest,
               IDBObjectStore* aObjectStore,
               IDBKeyRange* aKeyRange,
               const PRUint32 aLimit)
  : ObjectStoreHelper(aTransaction, aRequest, aObjectStore),
    mKeyRange(aKeyRange), mLimit(aLimit)
  { }

  ~GetAllHelper()
  {
    for (PRUint32 index = 0; index < mCloneReadInfos.Length(); index++) {
      IDBObjectStore::ClearStructuredCloneBuffer(
        mCloneReadInfos[index].mCloneBuffer);
    }
  }

  virtual nsresult DoDatabaseWork(mozIStorageConnection* aConnection)
                                  MOZ_OVERRIDE;

  virtual nsresult GetSuccessResult(JSContext* aCx,
                                    jsval* aVal) MOZ_OVERRIDE;

  virtual void ReleaseMainThreadObjects() MOZ_OVERRIDE;

  virtual nsresult
  PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams) MOZ_OVERRIDE;

  virtual ChildProcessSendResult
  MaybeSendResponseToChildProcess(nsresult aResultCode) MOZ_OVERRIDE;

  virtual nsresult
  UnpackResponseFromParentProcess(const ResponseValue& aResponseValue)
                                  MOZ_OVERRIDE;

protected:
  // In-params.
  nsRefPtr<IDBKeyRange> mKeyRange;
  const PRUint32 mLimit;

private:
  // Out-params.
  nsTArray<StructuredCloneReadInfo> mCloneReadInfos;
};

class CountHelper : public ObjectStoreHelper
{
public:
  CountHelper(IDBTransaction* aTransaction,
              IDBRequest* aRequest,
              IDBObjectStore* aObjectStore,
              IDBKeyRange* aKeyRange)
  : ObjectStoreHelper(aTransaction, aRequest, aObjectStore),
    mKeyRange(aKeyRange), mCount(0)
  { }

  virtual nsresult DoDatabaseWork(mozIStorageConnection* aConnection)
                                  MOZ_OVERRIDE;

  virtual nsresult GetSuccessResult(JSContext* aCx,
                                    jsval* aVal) MOZ_OVERRIDE;

  virtual void ReleaseMainThreadObjects() MOZ_OVERRIDE;

  virtual nsresult
  PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams) MOZ_OVERRIDE;

  virtual ChildProcessSendResult
  MaybeSendResponseToChildProcess(nsresult aResultCode) MOZ_OVERRIDE;

  virtual nsresult
  UnpackResponseFromParentProcess(const ResponseValue& aResponseValue)
                                  MOZ_OVERRIDE;

private:
  nsRefPtr<IDBKeyRange> mKeyRange;
  PRUint64 mCount;
};

NS_STACK_CLASS
class AutoRemoveIndex
{
public:
  AutoRemoveIndex(ObjectStoreInfo* aObjectStoreInfo,
                  const nsAString& aIndexName)
  : mObjectStoreInfo(aObjectStoreInfo), mIndexName(aIndexName)
  { }

  ~AutoRemoveIndex()
  {
    if (mObjectStoreInfo) {
      for (PRUint32 i = 0; i < mObjectStoreInfo->indexes.Length(); i++) {
        if (mObjectStoreInfo->indexes[i].name == mIndexName) {
          mObjectStoreInfo->indexes.RemoveElementAt(i);
          break;
        }
      }
    }
  }

  void forget()
  {
    mObjectStoreInfo = nsnull;
  }

private:
  ObjectStoreInfo* mObjectStoreInfo;
  nsString mIndexName;
};

class ThreadLocalJSRuntime
{
  JSRuntime* mRuntime;
  JSContext* mContext;
  JSObject* mGlobal;

  static JSClass sGlobalClass;
  static const unsigned sRuntimeHeapSize = 256 * 1024;

  ThreadLocalJSRuntime()
  : mRuntime(NULL), mContext(NULL), mGlobal(NULL)
  {
      MOZ_COUNT_CTOR(ThreadLocalJSRuntime);
  }

  nsresult Init()
  {
    mRuntime = JS_NewRuntime(sRuntimeHeapSize);
    NS_ENSURE_TRUE(mRuntime, NS_ERROR_OUT_OF_MEMORY);

    mContext = JS_NewContext(mRuntime, 0);
    NS_ENSURE_TRUE(mContext, NS_ERROR_OUT_OF_MEMORY);

    JSAutoRequest ar(mContext);

    mGlobal = JS_NewGlobalObject(mContext, &sGlobalClass, NULL);
    NS_ENSURE_TRUE(mGlobal, NS_ERROR_OUT_OF_MEMORY);

    JS_SetGlobalObject(mContext, mGlobal);
    return NS_OK;
  }

 public:
  static ThreadLocalJSRuntime *Create()
  {
    ThreadLocalJSRuntime *entry = new ThreadLocalJSRuntime();
    NS_ENSURE_TRUE(entry, nsnull);

    if (NS_FAILED(entry->Init())) {
      delete entry;
      return nsnull;
    }

    return entry;
  }

  JSContext *Context() const
  {
    return mContext;
  }

  ~ThreadLocalJSRuntime()
  {
    MOZ_COUNT_DTOR(ThreadLocalJSRuntime);

    if (mContext) {
      JS_DestroyContext(mContext);
    }

    if (mRuntime) {
      JS_DestroyRuntime(mRuntime);
    }
  }
};

JSClass ThreadLocalJSRuntime::sGlobalClass = {
  "IndexedDBTransactionThreadGlobal",
  JSCLASS_GLOBAL_FLAGS,
  JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
  JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub
};

inline
already_AddRefed<IDBRequest>
GenerateRequest(IDBObjectStore* aObjectStore, JSContext* aCx)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  IDBDatabase* database = aObjectStore->Transaction()->Database();
  return IDBRequest::Create(aObjectStore, database,
                            aObjectStore->Transaction(), aCx);
}

struct GetAddInfoClosure
{
  IDBObjectStore* mThis;
  StructuredCloneWriteInfo& mCloneWriteInfo;
  jsval mValue;
};

nsresult
GetAddInfoCallback(JSContext* aCx, void* aClosure)
{
  GetAddInfoClosure* data = static_cast<GetAddInfoClosure*>(aClosure);

  data->mCloneWriteInfo.mOffsetToKeyProp = 0;
  data->mCloneWriteInfo.mTransaction = data->mThis->Transaction();

  if (!IDBObjectStore::SerializeValue(aCx, data->mCloneWriteInfo,
                                      data->mValue)) {
    return NS_ERROR_DOM_DATA_CLONE_ERR;
  }

  return NS_OK;
}

} // anonymous namespace

JSClass IDBObjectStore::sDummyPropJSClass = {
  "dummy", 0,
  JS_PropertyStub,  JS_PropertyStub,
  JS_PropertyStub,  JS_StrictPropertyStub,
  JS_EnumerateStub, JS_ResolveStub,
  JS_ConvertStub
};

// static
already_AddRefed<IDBObjectStore>
IDBObjectStore::Create(IDBTransaction* aTransaction,
                       ObjectStoreInfo* aStoreInfo,
                       nsIAtom* aDatabaseId,
                       bool aCreating)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsRefPtr<IDBObjectStore> objectStore = new IDBObjectStore();

  objectStore->mTransaction = aTransaction;
  objectStore->mName = aStoreInfo->name;
  objectStore->mId = aStoreInfo->id;
  objectStore->mKeyPath = aStoreInfo->keyPath;
  objectStore->mAutoIncrement = aStoreInfo->autoIncrement;
  objectStore->mDatabaseId = aDatabaseId;
  objectStore->mInfo = aStoreInfo;

  if (!IndexedDatabaseManager::IsMainProcess()) {
    IndexedDBTransactionChild* transactionActor = aTransaction->GetActorChild();
    NS_ASSERTION(transactionActor, "Must have an actor here!");

    ObjectStoreConstructorParams params;

    if (aCreating) {
      CreateObjectStoreParams createParams;
      createParams.info() = *aStoreInfo;
      params = createParams;
    }
    else {
      GetObjectStoreParams getParams;
      getParams.name() = aStoreInfo->name;
      params = getParams;
    }

    IndexedDBObjectStoreChild* actor =
      new IndexedDBObjectStoreChild(objectStore);

    transactionActor->SendPIndexedDBObjectStoreConstructor(actor, params);
  }

  return objectStore.forget();
}

// static
nsresult
IDBObjectStore::AppendIndexUpdateInfo(
                                    PRInt64 aIndexID,
                                    const KeyPath& aKeyPath,
                                    bool aUnique,
                                    bool aMultiEntry,
                                    JSContext* aCx,
                                    jsval aVal,
                                    nsTArray<IndexUpdateInfo>& aUpdateInfoArray)
{
  nsresult rv;

  if (!aMultiEntry) {
    Key key;
    rv = aKeyPath.ExtractKey(aCx, aVal, key);

    // If an index's keypath doesn't match an object, we ignore that object.
    if (rv == NS_ERROR_DOM_INDEXEDDB_DATA_ERR || key.IsUnset()) {
      return NS_OK;
    }

    if (NS_FAILED(rv)) {
      return rv;
    }

    IndexUpdateInfo* updateInfo = aUpdateInfoArray.AppendElement();
    updateInfo->indexId = aIndexID;
    updateInfo->indexUnique = aUnique;
    updateInfo->value = key;

    return NS_OK;
  }

  JS::Value val;
  if (NS_FAILED(aKeyPath.ExtractKeyAsJSVal(aCx, aVal, &val))) {
    return NS_OK;
  }

  if (!JSVAL_IS_PRIMITIVE(val) &&
      JS_IsArrayObject(aCx, JSVAL_TO_OBJECT(val))) {
    JSObject* array = JSVAL_TO_OBJECT(val);
    uint32_t arrayLength;
    if (!JS_GetArrayLength(aCx, array, &arrayLength)) {
      return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
    }

    for (uint32_t arrayIndex = 0; arrayIndex < arrayLength; arrayIndex++) {
      jsval arrayItem;
      if (!JS_GetElement(aCx, array, arrayIndex, &arrayItem)) {
        return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
      }

      Key value;
      if (NS_FAILED(value.SetFromJSVal(aCx, arrayItem)) ||
          value.IsUnset()) {
        // Not a value we can do anything with, ignore it.
        continue;
      }

      IndexUpdateInfo* updateInfo = aUpdateInfoArray.AppendElement();
      updateInfo->indexId = aIndexID;
      updateInfo->indexUnique = aUnique;
      updateInfo->value = value;
    }
  }
  else {
    Key value;
    if (NS_FAILED(value.SetFromJSVal(aCx, val)) ||
        value.IsUnset()) {
      // Not a value we can do anything with, ignore it.
      return NS_OK;
    }

    IndexUpdateInfo* updateInfo = aUpdateInfoArray.AppendElement();
    updateInfo->indexId = aIndexID;
    updateInfo->indexUnique = aUnique;
    updateInfo->value = value;
  }

  return NS_OK;
}

// static
nsresult
IDBObjectStore::UpdateIndexes(IDBTransaction* aTransaction,
                              PRInt64 aObjectStoreId,
                              const Key& aObjectStoreKey,
                              bool aOverwrite,
                              PRInt64 aObjectDataId,
                              const nsTArray<IndexUpdateInfo>& aUpdateInfoArray)
{
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv;

  NS_ASSERTION(aObjectDataId != LL_MININT, "Bad objectData id!");

  NS_NAMED_LITERAL_CSTRING(objectDataId, "object_data_id");

  if (aOverwrite) {
    stmt = aTransaction->GetCachedStatement(
      "DELETE FROM unique_index_data "
      "WHERE object_data_id = :object_data_id; "
      "DELETE FROM index_data "
      "WHERE object_data_id = :object_data_id");
    NS_ENSURE_TRUE(stmt, NS_ERROR_FAILURE);

    mozStorageStatementScoper scoper(stmt);

    rv = stmt->BindInt64ByName(objectDataId, aObjectDataId);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = stmt->Execute();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  PRUint32 infoCount = aUpdateInfoArray.Length();
  for (PRUint32 i = 0; i < infoCount; i++) {
    const IndexUpdateInfo& updateInfo = aUpdateInfoArray[i];

    // Insert new values.

    stmt = updateInfo.indexUnique ?
      aTransaction->GetCachedStatement(
        "INSERT INTO unique_index_data "
          "(index_id, object_data_id, object_data_key, value) "
        "VALUES (:index_id, :object_data_id, :object_data_key, :value)") :
      aTransaction->GetCachedStatement(
        "INSERT OR IGNORE INTO index_data ("
          "index_id, object_data_id, object_data_key, value) "
        "VALUES (:index_id, :object_data_id, :object_data_key, :value)");

    NS_ENSURE_TRUE(stmt, NS_ERROR_FAILURE);

    mozStorageStatementScoper scoper4(stmt);

    rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("index_id"),
                               updateInfo.indexId);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = stmt->BindInt64ByName(objectDataId, aObjectDataId);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = aObjectStoreKey.BindToStatement(stmt,
                                         NS_LITERAL_CSTRING("object_data_key"));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = updateInfo.value.BindToStatement(stmt, NS_LITERAL_CSTRING("value"));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = stmt->Execute();
    if (rv == NS_ERROR_STORAGE_CONSTRAINT && updateInfo.indexUnique) {
      // If we're inserting multiple entries for the same unique index, then
      // we might have failed to insert due to colliding with another entry for
      // the same index in which case we should ignore it.
      
      for (PRInt32 j = (PRInt32)i - 1;
           j >= 0 && aUpdateInfoArray[j].indexId == updateInfo.indexId;
           --j) {
        if (updateInfo.value == aUpdateInfoArray[j].value) {
          // We found a key with the same value for the same index. So we
          // must have had a collision with a value we just inserted.
          rv = NS_OK;
          break;
        }
      }
    }

    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  return NS_OK;
}

// static
nsresult
IDBObjectStore::GetStructuredCloneReadInfoFromStatement(
                                           mozIStorageStatement* aStatement,
                                           PRUint32 aDataIndex,
                                           PRUint32 aFileIdsIndex,
                                           IDBDatabase* aDatabase,
                                           StructuredCloneReadInfo& aInfo)
{
#ifdef DEBUG
  {
    PRInt32 type;
    NS_ASSERTION(NS_SUCCEEDED(aStatement->GetTypeOfIndex(aDataIndex, &type)) &&
                 type == mozIStorageStatement::VALUE_TYPE_BLOB,
                 "Bad value type!");
  }
#endif

  const PRUint8* blobData;
  PRUint32 blobDataLength;
  nsresult rv = aStatement->GetSharedBlob(aDataIndex, &blobDataLength,
                                          &blobData);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  const char* compressed = reinterpret_cast<const char*>(blobData);
  size_t compressedLength = size_t(blobDataLength);

  size_t uncompressedLength;
  if (!snappy::GetUncompressedLength(compressed, compressedLength,
                                     &uncompressedLength)) {
    NS_WARNING("Snappy can't determine uncompressed length!");
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  nsAutoArrayPtr<char> uncompressed(new char[uncompressedLength]);

  if (!snappy::RawUncompress(compressed, compressedLength,
                             uncompressed.get())) {
    NS_WARNING("Snappy can't determine uncompressed length!");
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  JSAutoStructuredCloneBuffer& buffer = aInfo.mCloneBuffer;
  if (!buffer.copy(reinterpret_cast<const uint64_t *>(uncompressed.get()),
                   uncompressedLength)) {
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  bool isNull;
  rv = aStatement->GetIsNull(aFileIdsIndex, &isNull);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (!isNull) {
    nsString ids;
    rv = aStatement->GetString(aFileIdsIndex, ids);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    nsAutoTArray<PRInt64, 10> array;
    rv = ConvertFileIdsToArray(ids, array);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    FileManager* fileManager = aDatabase->Manager();

    for (PRUint32 i = 0; i < array.Length(); i++) {
      const PRInt64& id = array[i];

      nsRefPtr<FileInfo> fileInfo = fileManager->GetFileInfo(id);
      NS_ASSERTION(fileInfo, "Null file info!");

      aInfo.mFileInfos.AppendElement(fileInfo);
    }
  }

  aInfo.mDatabase = aDatabase;

  return NS_OK;
}

// static
void
IDBObjectStore::ClearStructuredCloneBuffer(JSAutoStructuredCloneBuffer& aBuffer)
{
  if (aBuffer.data()) {
    aBuffer.clear();
  }
}

// static
bool
IDBObjectStore::DeserializeValue(JSContext* aCx,
                                 StructuredCloneReadInfo& aCloneReadInfo,
                                 jsval* aValue)
{
  NS_ASSERTION(NS_IsMainThread(),
               "Should only be deserializing on the main thread!");
  NS_ASSERTION(aCx, "A JSContext is required!");

  JSAutoStructuredCloneBuffer& buffer = aCloneReadInfo.mCloneBuffer;

  if (!buffer.data()) {
    *aValue = JSVAL_VOID;
    return true;
  }

  JSAutoRequest ar(aCx);

  JSStructuredCloneCallbacks callbacks = {
    IDBObjectStore::StructuredCloneReadCallback,
    nsnull,
    nsnull
  };

  return buffer.read(aCx, aValue, &callbacks, &aCloneReadInfo);
}

// static
bool
IDBObjectStore::SerializeValue(JSContext* aCx,
                               StructuredCloneWriteInfo& aCloneWriteInfo,
                               jsval aValue)
{
  NS_ASSERTION(NS_IsMainThread(),
               "Should only be serializing on the main thread!");
  NS_ASSERTION(aCx, "A JSContext is required!");

  JSAutoRequest ar(aCx);

  JSStructuredCloneCallbacks callbacks = {
    nsnull,
    StructuredCloneWriteCallback,
    nsnull
  };

  JSAutoStructuredCloneBuffer& buffer = aCloneWriteInfo.mCloneBuffer;

  return buffer.write(aCx, aValue, &callbacks, &aCloneWriteInfo);
}

static inline PRUint32
SwapBytes(PRUint32 u)
{
#ifdef IS_BIG_ENDIAN
  return ((u & 0x000000ffU) << 24) |                                          
         ((u & 0x0000ff00U) << 8) |                                           
         ((u & 0x00ff0000U) >> 8) |                                           
         ((u & 0xff000000U) >> 24);
#else
  return u;
#endif
}

static inline double
SwapBytes(PRUint64 u)
{
#ifdef IS_BIG_ENDIAN
  return ((u & 0x00000000000000ffLLU) << 56) |
         ((u & 0x000000000000ff00LLU) << 40) |
         ((u & 0x0000000000ff0000LLU) << 24) |
         ((u & 0x00000000ff000000LLU) << 8) |
         ((u & 0x000000ff00000000LLU) >> 8) |
         ((u & 0x0000ff0000000000LLU) >> 24) |
         ((u & 0x00ff000000000000LLU) >> 40) |
         ((u & 0xff00000000000000LLU) >> 56);
#else
  return double(u);
#endif
}

static inline bool
StructuredCloneReadString(JSStructuredCloneReader* aReader,
                          nsCString& aString)
{
  PRUint32 length;
  if (!JS_ReadBytes(aReader, &length, sizeof(PRUint32))) {
    NS_WARNING("Failed to read length!");
    return false;
  }
  length = SwapBytes(length);

  if (!EnsureStringLength(aString, length)) {
    NS_WARNING("Out of memory?");
    return false;
  }
  char* buffer = aString.BeginWriting();

  if (!JS_ReadBytes(aReader, buffer, length)) {
    NS_WARNING("Failed to read type!");
    return false;
  }

  return true;
}

JSObject*
IDBObjectStore::StructuredCloneReadCallback(JSContext* aCx,
                                            JSStructuredCloneReader* aReader,
                                            uint32_t aTag,
                                            uint32_t aData,
                                            void* aClosure)
{
  if (aTag == SCTAG_DOM_FILEHANDLE || aTag == SCTAG_DOM_BLOB ||
      aTag == SCTAG_DOM_FILE) {
    StructuredCloneReadInfo* cloneReadInfo =
      reinterpret_cast<StructuredCloneReadInfo*>(aClosure);

    if (aData >= cloneReadInfo->mFileInfos.Length()) {
      NS_ERROR("Bad blob index!");
      return nsnull;
    }

    nsRefPtr<FileInfo> fileInfo = cloneReadInfo->mFileInfos[aData];
    IDBDatabase* database = cloneReadInfo->mDatabase;

    if (aTag == SCTAG_DOM_FILEHANDLE) {
      nsCString type;
      if (!StructuredCloneReadString(aReader, type)) {
        return nsnull;
      }
      NS_ConvertUTF8toUTF16 convType(type);

      nsCString name;
      if (!StructuredCloneReadString(aReader, name)) {
        return nsnull;
      }
      NS_ConvertUTF8toUTF16 convName(name);

      nsRefPtr<IDBFileHandle> fileHandle = IDBFileHandle::Create(database,
        convName, convType, fileInfo.forget());

      jsval wrappedFileHandle;
      nsresult rv =
        nsContentUtils::WrapNative(aCx, JS_GetGlobalForScopeChain(aCx),
                                   static_cast<nsIDOMFileHandle*>(fileHandle),
                                   &NS_GET_IID(nsIDOMFileHandle),
                                   &wrappedFileHandle);
      if (NS_FAILED(rv)) {
        NS_WARNING("Failed to wrap native!");
        return nsnull;
      }

      return JSVAL_TO_OBJECT(wrappedFileHandle);
    }

    FileManager* fileManager = database->Manager();

    nsCOMPtr<nsIFile> directory = fileManager->GetDirectory();
    if (!directory) {
      NS_WARNING("Failed to get directory!");
      return nsnull;
    }

    nsCOMPtr<nsIFile> nativeFile =
      fileManager->GetFileForId(directory, fileInfo->Id());
    if (!nativeFile) {
      NS_WARNING("Failed to get file!");
      return nsnull;
    }

    PRUint64 size;
    if (!JS_ReadBytes(aReader, &size, sizeof(PRUint64))) {
      NS_WARNING("Failed to read size!");
      return nsnull;
    }
    size = SwapBytes(size);

    nsCString type;
    if (!StructuredCloneReadString(aReader, type)) {
      return nsnull;
    }
    NS_ConvertUTF8toUTF16 convType(type);

    if (aTag == SCTAG_DOM_BLOB) {
      nsCOMPtr<nsIDOMBlob> blob = new nsDOMFileFile(convType, size,
                                                    nativeFile, fileInfo);

      jsval wrappedBlob;
      nsresult rv =
        nsContentUtils::WrapNative(aCx, JS_GetGlobalForScopeChain(aCx), blob,
                                   &NS_GET_IID(nsIDOMBlob), &wrappedBlob);
      if (NS_FAILED(rv)) {
        NS_WARNING("Failed to wrap native!");
        return nsnull;
      }

      return JSVAL_TO_OBJECT(wrappedBlob);
    }

    nsCString name;
    if (!StructuredCloneReadString(aReader, name)) {
      return nsnull;
    }
    NS_ConvertUTF8toUTF16 convName(name);

    nsCOMPtr<nsIDOMFile> file = new nsDOMFileFile(convName, convType, size,
                                                  nativeFile, fileInfo);

    jsval wrappedFile;
    nsresult rv =
      nsContentUtils::WrapNative(aCx, JS_GetGlobalForScopeChain(aCx), file,
                                 &NS_GET_IID(nsIDOMFile), &wrappedFile);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to wrap native!");
      return nsnull;
    }

    return JSVAL_TO_OBJECT(wrappedFile);
  }

  const JSStructuredCloneCallbacks* runtimeCallbacks =
    js::GetContextStructuredCloneCallbacks(aCx);

  if (runtimeCallbacks) {
    return runtimeCallbacks->read(aCx, aReader, aTag, aData, nsnull);
  }

  return nsnull;
}

JSBool
IDBObjectStore::StructuredCloneWriteCallback(JSContext* aCx,
                                             JSStructuredCloneWriter* aWriter,
                                             JSObject* aObj,
                                             void* aClosure)
{
  StructuredCloneWriteInfo* cloneWriteInfo =
    reinterpret_cast<StructuredCloneWriteInfo*>(aClosure);

  if (JS_GetClass(aObj) == &sDummyPropJSClass) {
    NS_ASSERTION(cloneWriteInfo->mOffsetToKeyProp == 0,
                 "We should not have been here before!");
    cloneWriteInfo->mOffsetToKeyProp = js_GetSCOffset(aWriter);

    PRUint64 value = 0;
    return JS_WriteBytes(aWriter, &value, sizeof(value));
  }

  nsCOMPtr<nsIXPConnectWrappedNative> wrappedNative;
  nsContentUtils::XPConnect()->
    GetWrappedNativeOfJSObject(aCx, aObj, getter_AddRefs(wrappedNative));

  if (wrappedNative) {
    nsISupports* supports = wrappedNative->Native();

    IDBTransaction* transaction = cloneWriteInfo->mTransaction;
    FileManager* fileManager = transaction->Database()->Manager();

    nsCOMPtr<nsIDOMBlob> blob = do_QueryInterface(supports);
    if (blob) {
      // Check if it is a blob created from this db or the blob was already
      // stored in this db

      nsRefPtr<FileInfo> fileInfo = transaction->GetFileInfo(blob);
      nsCOMPtr<nsIInputStream> inputStream;

      if (!fileInfo) {
        fileInfo = blob->GetFileInfo(fileManager);
      }

      if (!fileInfo) {
        fileInfo = fileManager->GetNewFileInfo();
        if (!fileInfo) {
          NS_WARNING("Failed to get new file info!");
          return false;
        }

        if (NS_FAILED(blob->GetInternalStream(getter_AddRefs(inputStream)))) {
          NS_WARNING("Failed to get internal steam!");
          return false;
        }

        transaction->AddFileInfo(blob, fileInfo);
      }

      PRUint64 size;
      if (NS_FAILED(blob->GetSize(&size))) {
        NS_WARNING("Failed to get size!");
        return false;
      }
      size = SwapBytes(size);

      nsString type;
      if (NS_FAILED(blob->GetType(type))) {
        NS_WARNING("Failed to get type!");
        return false;
      }
      NS_ConvertUTF16toUTF8 convType(type);
      PRUint32 convTypeLength = SwapBytes(convType.Length());

      nsCOMPtr<nsIDOMFile> file = do_QueryInterface(blob);

      if (!JS_WriteUint32Pair(aWriter, file ? SCTAG_DOM_FILE : SCTAG_DOM_BLOB,
                              cloneWriteInfo->mFiles.Length()) ||
          !JS_WriteBytes(aWriter, &size, sizeof(PRUint64)) ||
          !JS_WriteBytes(aWriter, &convTypeLength, sizeof(PRUint32)) ||
          !JS_WriteBytes(aWriter, convType.get(), convType.Length())) {
        return false;
      }

      if (file) {
        nsString name;
        if (NS_FAILED(file->GetName(name))) {
          NS_WARNING("Failed to get name!");
          return false;
        }
        NS_ConvertUTF16toUTF8 convName(name);
        PRUint32 convNameLength = SwapBytes(convName.Length());

        if (!JS_WriteBytes(aWriter, &convNameLength, sizeof(PRUint32)) ||
            !JS_WriteBytes(aWriter, convName.get(), convName.Length())) {
          return false;
        }
      }

      StructuredCloneFile* cloneFile = cloneWriteInfo->mFiles.AppendElement();
      cloneFile->mFile = blob.forget();
      cloneFile->mFileInfo = fileInfo.forget();
      cloneFile->mInputStream = inputStream.forget();

      return true;
    }

    nsCOMPtr<nsIDOMFileHandle> fileHandle = do_QueryInterface(supports);
    if (fileHandle) {
      nsRefPtr<FileInfo> fileInfo = fileHandle->GetFileInfo();

      // Throw when trying to store non IDB file handles or IDB file handles
      // across databases.
      if (!fileInfo || fileInfo->Manager() != fileManager) {
        return false;
      }

      nsString type;
      if (NS_FAILED(fileHandle->GetType(type))) {
        NS_WARNING("Failed to get type!");
        return false;
      }
      NS_ConvertUTF16toUTF8 convType(type);
      PRUint32 convTypeLength = SwapBytes(convType.Length());

      nsString name;
      if (NS_FAILED(fileHandle->GetName(name))) {
        NS_WARNING("Failed to get name!");
        return false;
      }
      NS_ConvertUTF16toUTF8 convName(name);
      PRUint32 convNameLength = SwapBytes(convName.Length());

      if (!JS_WriteUint32Pair(aWriter, SCTAG_DOM_FILEHANDLE,
                              cloneWriteInfo->mFiles.Length()) ||
          !JS_WriteBytes(aWriter, &convTypeLength, sizeof(PRUint32)) ||
          !JS_WriteBytes(aWriter, convType.get(), convType.Length()) ||
          !JS_WriteBytes(aWriter, &convNameLength, sizeof(PRUint32)) ||
          !JS_WriteBytes(aWriter, convName.get(), convName.Length())) {
        return false;
      }

      StructuredCloneFile* file = cloneWriteInfo->mFiles.AppendElement();
      file->mFileInfo = fileInfo.forget();

      return true;
    }
  }

  // try using the runtime callbacks
  const JSStructuredCloneCallbacks* runtimeCallbacks =
    js::GetContextStructuredCloneCallbacks(aCx);
  if (runtimeCallbacks) {
    return runtimeCallbacks->write(aCx, aWriter, aObj, nsnull);
  }

  return false;
}

nsresult
IDBObjectStore::ConvertFileIdsToArray(const nsAString& aFileIds,
                                      nsTArray<PRInt64>& aResult)
{
  nsCharSeparatedTokenizerTemplate<IgnoreNothing> tokenizer(aFileIds, ' ');

  while (tokenizer.hasMoreTokens()) {
    nsString token(tokenizer.nextToken());

    NS_ASSERTION(!token.IsEmpty(), "Should be a valid id!");

    nsresult rv;
    PRInt32 id = token.ToInteger(&rv);
    NS_ENSURE_SUCCESS(rv, rv);
    
    PRInt64* element = aResult.AppendElement();
    *element = id;
  }

  return NS_OK;
}

IDBObjectStore::IDBObjectStore()
: mId(LL_MININT),
  mKeyPath(0),
  mCachedKeyPath(JSVAL_VOID),
  mRooted(false),
  mAutoIncrement(false),
  mActorChild(nsnull),
  mActorParent(nsnull)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
}

IDBObjectStore::~IDBObjectStore()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(!mActorParent, "Actor parent owns us, how can we be dying?!");
  if (mActorChild) {
    NS_ASSERTION(!IndexedDatabaseManager::IsMainProcess(), "Wrong process!");
    mActorChild->Send__delete__(mActorChild);
    NS_ASSERTION(!mActorChild, "Should have cleared in Send__delete__!");
  }

  if (mRooted) {
    NS_DROP_JS_OBJECTS(this, IDBObjectStore);
  }
}

nsresult
IDBObjectStore::GetAddInfo(JSContext* aCx,
                           jsval aValue,
                           jsval aKeyVal,
                           StructuredCloneWriteInfo& aCloneWriteInfo,
                           Key& aKey,
                           nsTArray<IndexUpdateInfo>& aUpdateInfoArray)
{
  nsresult rv;

  // Return DATA_ERR if a key was passed in and this objectStore uses inline
  // keys.
  if (!JSVAL_IS_VOID(aKeyVal) && HasValidKeyPath()) {
    return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  JSAutoRequest ar(aCx);

  if (!HasValidKeyPath()) {
    // Out-of-line keys must be passed in.
    rv = aKey.SetFromJSVal(aCx, aKeyVal);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }
  else if (!mAutoIncrement) {
    rv = GetKeyPath().ExtractKey(aCx, aValue, aKey);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  // Return DATA_ERR if no key was specified this isn't an autoIncrement
  // objectStore.
  if (aKey.IsUnset() && !mAutoIncrement) {
    return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  // Figure out indexes and the index values to update here.
  PRUint32 count = mInfo->indexes.Length();
  aUpdateInfoArray.SetCapacity(count); // Pretty good estimate
  for (PRUint32 indexesIndex = 0; indexesIndex < count; indexesIndex++) {
    const IndexInfo& indexInfo = mInfo->indexes[indexesIndex];

    rv = AppendIndexUpdateInfo(indexInfo.id, indexInfo.keyPath,
                               indexInfo.unique, indexInfo.multiEntry, aCx,
                               aValue, aUpdateInfoArray);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  GetAddInfoClosure data = {this, aCloneWriteInfo, aValue};

  if (mAutoIncrement && HasValidKeyPath()) {
    NS_ASSERTION(aKey.IsUnset(), "Shouldn't have gotten the key yet!");

    rv = GetKeyPath().ExtractOrCreateKey(aCx, aValue, aKey,
                                         &GetAddInfoCallback, &data);
  }
  else {
    rv = GetAddInfoCallback(aCx, &data);
  }

  return rv;
}

nsresult
IDBObjectStore::AddOrPut(const jsval& aValue,
                         const jsval& aKey,
                         JSContext* aCx,
                         PRUint8 aOptionalArgCount,
                         bool aOverwrite,
                         IDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!IsWriteAllowed()) {
    return NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR;
  }

  jsval keyval = (aOptionalArgCount >= 1) ? aKey : JSVAL_VOID;

  StructuredCloneWriteInfo cloneWriteInfo;
  Key key;
  nsTArray<IndexUpdateInfo> updateInfo;

  nsresult rv = GetAddInfo(aCx, aValue, keyval, cloneWriteInfo, key,
                           updateInfo);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this, aCx);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<AddHelper> helper =
    new AddHelper(mTransaction, request, this, cloneWriteInfo, key,
                  aOverwrite, updateInfo);

  rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

nsresult
IDBObjectStore::AddOrPutInternal(
                      const SerializedStructuredCloneWriteInfo& aCloneWriteInfo,
                      const Key& aKey,
                      const InfallibleTArray<IndexUpdateInfo>& aUpdateInfoArray,
                      bool aOverwrite,
                      IDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!IsWriteAllowed()) {
    return NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this, nsnull);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  StructuredCloneWriteInfo cloneWriteInfo;
  if (!cloneWriteInfo.SetFromSerialized(aCloneWriteInfo)) {
    NS_WARNING("Failed to copy structured clone buffer!");
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  Key key(aKey);

  nsTArray<IndexUpdateInfo> updateInfo(aUpdateInfoArray);

  nsRefPtr<AddHelper> helper =
    new AddHelper(mTransaction, request, this, cloneWriteInfo, key, aOverwrite,
                  updateInfo);

  nsresult rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

nsresult
IDBObjectStore::GetInternal(IDBKeyRange* aKeyRange,
                            JSContext* aCx,
                            IDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(aKeyRange, "Null pointer!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this, aCx);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<GetHelper> helper =
    new GetHelper(mTransaction, request, this, aKeyRange);

  nsresult rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

nsresult
IDBObjectStore::GetAllInternal(IDBKeyRange* aKeyRange,
                               PRUint32 aLimit,
                               JSContext* aCx,
                               IDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this, aCx);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<GetAllHelper> helper =
    new GetAllHelper(mTransaction, request, this, aKeyRange, aLimit);

  nsresult rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

nsresult
IDBObjectStore::DeleteInternal(IDBKeyRange* aKeyRange,
                               JSContext* aCx,
                               IDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(aKeyRange, "Null key range!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!IsWriteAllowed()) {
    return NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this, aCx);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<DeleteHelper> helper =
    new DeleteHelper(mTransaction, request, this, aKeyRange);

  nsresult rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

nsresult
IDBObjectStore::ClearInternal(JSContext* aCx,
                              IDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!IsWriteAllowed()) {
    return NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this, aCx);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<ClearHelper> helper(new ClearHelper(mTransaction, request, this));

  nsresult rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

nsresult
IDBObjectStore::CountInternal(IDBKeyRange* aKeyRange,
                              JSContext* aCx,
                              IDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  nsRefPtr<IDBRequest> request = GenerateRequest(this, aCx);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<CountHelper> helper =
    new CountHelper(mTransaction, request, this, aKeyRange);
  nsresult rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

nsresult
IDBObjectStore::OpenCursorInternal(IDBKeyRange* aKeyRange,
                                   size_t aDirection,
                                   JSContext* aCx,
                                   IDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  IDBCursor::Direction direction =
    static_cast<IDBCursor::Direction>(aDirection);

  nsRefPtr<IDBRequest> request = GenerateRequest(this, aCx);
  NS_ENSURE_TRUE(request, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsRefPtr<OpenCursorHelper> helper =
    new OpenCursorHelper(mTransaction, request, this, aKeyRange, direction);

  nsresult rv = helper->DispatchToTransactionPool();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  request.forget(_retval);
  return NS_OK;
}

nsresult
IDBObjectStore::OpenCursorFromChildProcess(
                            IDBRequest* aRequest,
                            size_t aDirection,
                            const Key& aKey,
                            const SerializedStructuredCloneReadInfo& aCloneInfo,
                            IDBCursor** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION((!aCloneInfo.dataLength && !aCloneInfo.data) ||
               (aCloneInfo.dataLength && aCloneInfo.data),
               "Inconsistent clone info!");

  IDBCursor::Direction direction =
    static_cast<IDBCursor::Direction>(aDirection);

  StructuredCloneReadInfo cloneInfo;

  if (!cloneInfo.SetFromSerialized(aCloneInfo)) {
    NS_WARNING("Failed to copy clone buffer!");
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  nsRefPtr<IDBCursor> cursor =
    IDBCursor::Create(aRequest, mTransaction, this, direction, Key(),
                      EmptyCString(), EmptyCString(), aKey, cloneInfo);
  NS_ENSURE_TRUE(cursor, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ASSERTION(!cloneInfo.mCloneBuffer.data(), "Should have swapped!");

  cursor.forget(_retval);
  return NS_OK;
}

void
IDBObjectStore::SetInfo(ObjectStoreInfo* aInfo)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread");
  NS_ASSERTION(aInfo != mInfo, "This is nonsense");

  mInfo = aInfo;
}

nsresult
IDBObjectStore::CreateIndexInternal(const IndexInfo& aInfo,
                                    IDBIndex** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  IndexInfo* indexInfo = mInfo->indexes.AppendElement();

  indexInfo->name = aInfo.name;
  indexInfo->id = aInfo.id;
  indexInfo->keyPath = aInfo.keyPath;
  indexInfo->unique = aInfo.unique;
  indexInfo->multiEntry = aInfo.multiEntry;

  // Don't leave this in the list if we fail below!
  AutoRemoveIndex autoRemove(mInfo, aInfo.name);

  nsRefPtr<IDBIndex> index = IDBIndex::Create(this, indexInfo, true);

  mCreatedIndexes.AppendElement(index);

  if (IndexedDatabaseManager::IsMainProcess()) {
    nsRefPtr<CreateIndexHelper> helper =
      new CreateIndexHelper(mTransaction, index);

    nsresult rv = helper->DispatchToTransactionPool();
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  autoRemove.forget();

  index.forget(_retval);
  return NS_OK;
}

nsresult
IDBObjectStore::IndexInternal(const nsAString& aName,
                              IDBIndex** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (mTransaction->IsFinished()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  IndexInfo* indexInfo = nsnull;
  PRUint32 indexCount = mInfo->indexes.Length();
  for (PRUint32 index = 0; index < indexCount; index++) {
    if (mInfo->indexes[index].name == aName) {
      indexInfo = &(mInfo->indexes[index]);
      break;
    }
  }

  if (!indexInfo) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_FOUND_ERR;
  }

  nsRefPtr<IDBIndex> retval;
  for (PRUint32 i = 0; i < mCreatedIndexes.Length(); i++) {
    nsRefPtr<IDBIndex>& index = mCreatedIndexes[i];
    if (index->Name() == aName) {
      retval = index;
      break;
    }
  }

  if (!retval) {
    retval = IDBIndex::Create(this, indexInfo, false);
    NS_ENSURE_TRUE(retval, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    if (!mCreatedIndexes.AppendElement(retval)) {
      NS_WARNING("Out of memory!");
      return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
    }
  }

  retval.forget(_retval);
  return NS_OK;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(IDBObjectStore)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(IDBObjectStore)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JSVAL_MEMBER_CALLBACK(mCachedKeyPath)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(IDBObjectStore)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(mTransaction,
                                                       nsIDOMEventTarget)

  for (PRUint32 i = 0; i < tmp->mCreatedIndexes.Length(); i++) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mCreatedIndexes[i]");
    cb.NoteXPCOMChild(static_cast<nsIIDBIndex*>(tmp->mCreatedIndexes[i].get()));
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(IDBObjectStore)
  // Don't unlink mTransaction!

  tmp->mCreatedIndexes.Clear();

  tmp->mCachedKeyPath = JSVAL_VOID;

  if (tmp->mRooted) {
    NS_DROP_JS_OBJECTS(tmp, IDBObjectStore);
    tmp->mRooted = false;
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(IDBObjectStore)
  NS_INTERFACE_MAP_ENTRY(nsIIDBObjectStore)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(IDBObjectStore)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(IDBObjectStore)
NS_IMPL_CYCLE_COLLECTING_RELEASE(IDBObjectStore)

DOMCI_DATA(IDBObjectStore, IDBObjectStore)

NS_IMETHODIMP
IDBObjectStore::GetName(nsAString& aName)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  aName.Assign(mName);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::GetKeyPath(JSContext* aCx,
                           jsval* aVal)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!JSVAL_IS_VOID(mCachedKeyPath)) {
    *aVal = mCachedKeyPath;
    return NS_OK;
  }

  nsresult rv = GetKeyPath().ToJSVal(aCx, &mCachedKeyPath);
  NS_ENSURE_SUCCESS(rv, rv);

  if (JSVAL_IS_GCTHING(mCachedKeyPath)) {
    NS_HOLD_JS_OBJECTS(this, IDBObjectStore);
    mRooted = true;
  }

  *aVal = mCachedKeyPath;
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::GetTransaction(nsIIDBTransaction** aTransaction)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsCOMPtr<nsIIDBTransaction> transaction(mTransaction);
  transaction.forget(aTransaction);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::GetAutoIncrement(bool* aAutoIncrement)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  *aAutoIncrement = mAutoIncrement;
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::GetIndexNames(nsIDOMDOMStringList** aIndexNames)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsRefPtr<nsDOMStringList> list(new nsDOMStringList());

  nsAutoTArray<nsString, 10> names;
  PRUint32 count = mInfo->indexes.Length();
  names.SetCapacity(count);

  for (PRUint32 index = 0; index < count; index++) {
    names.InsertElementSorted(mInfo->indexes[index].name);
  }

  for (PRUint32 index = 0; index < count; index++) {
    NS_ENSURE_TRUE(list->Add(names[index]),
                   NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  list.forget(aIndexNames);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Get(const jsval& aKey,
                    JSContext* aCx,
                    nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  nsRefPtr<IDBKeyRange> keyRange;
  nsresult rv = IDBKeyRange::FromJSVal(aCx, aKey, getter_AddRefs(keyRange));
  NS_ENSURE_SUCCESS(rv, rv);

  if (!keyRange) {
    // Must specify a key or keyRange for get().
    return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  nsRefPtr<IDBRequest> request;
  rv = GetInternal(keyRange, aCx, getter_AddRefs(request));
  if (NS_FAILED(rv)) {
    return rv;
  }

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::GetAll(const jsval& aKey,
                       PRUint32 aLimit,
                       JSContext* aCx,
                       PRUint8 aOptionalArgCount,
                       nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  nsresult rv;

  nsRefPtr<IDBKeyRange> keyRange;
  if (aOptionalArgCount) {
    rv = IDBKeyRange::FromJSVal(aCx, aKey, getter_AddRefs(keyRange));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aOptionalArgCount < 2 || aLimit == 0) {
    aLimit = PR_UINT32_MAX;
  }

  nsRefPtr<IDBRequest> request;
  rv = GetAllInternal(keyRange, aLimit, aCx, getter_AddRefs(request));
  if (NS_FAILED(rv)) {
    return rv;
  }

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Add(const jsval& aValue,
                    const jsval& aKey,
                    JSContext* aCx,
                    PRUint8 aOptionalArgCount,
                    nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsRefPtr<IDBRequest> request;
  nsresult rv = AddOrPut(aValue, aKey, aCx, aOptionalArgCount, false,
                         getter_AddRefs(request));
  if (NS_FAILED(rv)) {
    return rv;
  }

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Put(const jsval& aValue,
                    const jsval& aKey,
                    JSContext* aCx,
                    PRUint8 aOptionalArgCount,
                    nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsRefPtr<IDBRequest> request;
  nsresult rv = AddOrPut(aValue, aKey, aCx, aOptionalArgCount, true,
                         getter_AddRefs(request));
  if (NS_FAILED(rv)) {
    return rv;
  }

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Delete(const jsval& aKey,
                       JSContext* aCx,
                       nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  if (!IsWriteAllowed()) {
    return NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR;
  }

  nsRefPtr<IDBKeyRange> keyRange;
  nsresult rv = IDBKeyRange::FromJSVal(aCx, aKey, getter_AddRefs(keyRange));
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (!keyRange) {
    // Must specify a key or keyRange for delete().
    return NS_ERROR_DOM_INDEXEDDB_DATA_ERR;
  }

  nsRefPtr<IDBRequest> request;
  rv = DeleteInternal(keyRange, aCx, getter_AddRefs(request));
  if (NS_FAILED(rv)) {
    return rv;
  }

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Clear(JSContext* aCx, nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsRefPtr<IDBRequest> request;
  nsresult rv = ClearInternal(aCx, getter_AddRefs(request));
  if (NS_FAILED(rv)) {
    return rv;
  }

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::OpenCursor(const jsval& aKey,
                           const nsAString& aDirection,
                           JSContext* aCx,
                           PRUint8 aOptionalArgCount,
                           nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  nsresult rv;

  IDBCursor::Direction direction = IDBCursor::NEXT;

  nsRefPtr<IDBKeyRange> keyRange;
  if (aOptionalArgCount) {
    rv = IDBKeyRange::FromJSVal(aCx, aKey, getter_AddRefs(keyRange));
    NS_ENSURE_SUCCESS(rv, rv);

    if (aOptionalArgCount >= 2) {
      rv = IDBCursor::ParseDirection(aDirection, &direction);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  size_t argDirection = static_cast<size_t>(direction);

  nsRefPtr<IDBRequest> request;
  rv = OpenCursorInternal(keyRange, argDirection, aCx,
                          getter_AddRefs(request));
  if (NS_FAILED(rv)) {
    return rv;
  }

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::CreateIndex(const nsAString& aName,
                            const jsval& aKeyPath,
                            const jsval& aOptions,
                            JSContext* aCx,
                            nsIIDBIndex** _retval)
{
  NS_PRECONDITION(NS_IsMainThread(), "Wrong thread!");

  KeyPath keyPath(0);
  if (NS_FAILED(KeyPath::Parse(aCx, aKeyPath, &keyPath)) ||
      !keyPath.IsValid()) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  // Check name and current mode
  IDBTransaction* transaction = AsyncConnectionHelper::GetCurrentTransaction();

  if (!transaction ||
      transaction != mTransaction ||
      mTransaction->GetMode() != IDBTransaction::VERSION_CHANGE) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR;
  }

  bool found = false;
  PRUint32 indexCount = mInfo->indexes.Length();
  for (PRUint32 index = 0; index < indexCount; index++) {
    if (mInfo->indexes[index].name == aName) {
      found = true;
      break;
    }
  }

  if (found) {
    return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
  }

  NS_ASSERTION(mTransaction->IsOpen(), "Impossible!");

#ifdef DEBUG
  for (PRUint32 index = 0; index < mCreatedIndexes.Length(); index++) {
    if (mCreatedIndexes[index]->Name() == aName) {
      NS_ERROR("Already created this one!");
    }
  }
#endif

  nsresult rv;
  mozilla::dom::IDBIndexParameters params;

  // Get optional arguments.
  if (!JSVAL_IS_VOID(aOptions) && !JSVAL_IS_NULL(aOptions)) {
    rv = params.Init(aCx, &aOptions);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  if (params.multiEntry && keyPath.IsArray()) {
    return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }

  DatabaseInfo* databaseInfo = mTransaction->DBInfo();

  IndexInfo info;

  info.name = aName;
  info.id = databaseInfo->nextIndexId++;
  info.keyPath = keyPath;
  info.unique = params.unique;
  info.multiEntry = params.multiEntry;

  nsRefPtr<IDBIndex> index;
  rv = CreateIndexInternal(info, getter_AddRefs(index));
  if (NS_FAILED(rv)) {
    return rv;
  }

  index.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Index(const nsAString& aName,
                      nsIIDBIndex** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsRefPtr<IDBIndex> index;
  nsresult rv = IndexInternal(aName, getter_AddRefs(index));
  if (NS_FAILED(rv)) {
    return rv;
  }

  index.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::DeleteIndex(const nsAString& aName)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  IDBTransaction* transaction = AsyncConnectionHelper::GetCurrentTransaction();

  if (!transaction ||
      transaction != mTransaction ||
      mTransaction->GetMode() != IDBTransaction::VERSION_CHANGE) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR;
  }

  NS_ASSERTION(mTransaction->IsOpen(), "Impossible!");

  PRUint32 index = 0;
  for (; index < mInfo->indexes.Length(); index++) {
    if (mInfo->indexes[index].name == aName) {
      break;
    }
  }

  if (index == mInfo->indexes.Length()) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_FOUND_ERR;
  }

  nsresult rv;

  if (IndexedDatabaseManager::IsMainProcess()) {
    nsRefPtr<DeleteIndexHelper> helper =
      new DeleteIndexHelper(mTransaction, this, aName);

    rv = helper->DispatchToTransactionPool();
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }
  else {
    NS_ASSERTION(mActorChild, "Must have an actor here!");

    mActorChild->SendDeleteIndex(nsString(aName));
  }

  mInfo->indexes.RemoveElementAt(index);

  for (PRUint32 i = 0; i < mCreatedIndexes.Length(); i++) {
    if (mCreatedIndexes[i]->Name() == aName) {
      mCreatedIndexes.RemoveElementAt(i);
      break;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
IDBObjectStore::Count(const jsval& aKey,
                      JSContext* aCx,
                      PRUint8 aOptionalArgCount,
                      nsIIDBRequest** _retval)
{
  if (!mTransaction->IsOpen()) {
    return NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR;
  }

  nsresult rv;

  nsRefPtr<IDBKeyRange> keyRange;
  if (aOptionalArgCount) {
    rv = IDBKeyRange::FromJSVal(aCx, aKey, getter_AddRefs(keyRange));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsRefPtr<IDBRequest> request;
  rv = CountInternal(keyRange, aCx, getter_AddRefs(request));
  if (NS_FAILED(rv)) {
    return rv;
  }

  request.forget(_retval);
  return NS_OK;
}

inline nsresult
CopyData(nsIInputStream* aInputStream, nsIOutputStream* aOutputStream)
{
  nsresult rv;

  do {
    char copyBuffer[FILE_COPY_BUFFER_SIZE];

    PRUint32 numRead;
    rv = aInputStream->Read(copyBuffer, FILE_COPY_BUFFER_SIZE, &numRead);
    NS_ENSURE_SUCCESS(rv, rv);

    if (numRead <= 0) {
      break;
    }

    PRUint32 numWrite;
    rv = aOutputStream->Write(copyBuffer, numRead, &numWrite);
    NS_ENSURE_SUCCESS(rv, rv);

    NS_ENSURE_TRUE(numWrite == numRead, NS_ERROR_FAILURE);
  } while (true);

  rv = aOutputStream->Flush();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void
ObjectStoreHelper::ReleaseMainThreadObjects()
{
  mObjectStore = nsnull;
  AsyncConnectionHelper::ReleaseMainThreadObjects();
}

nsresult
ObjectStoreHelper::Dispatch(nsIEventTarget* aDatabaseThread)
{
  if (IndexedDatabaseManager::IsMainProcess()) {
    return AsyncConnectionHelper::Dispatch(aDatabaseThread);
  }

  IndexedDBObjectStoreChild* objectStoreActor = mObjectStore->GetActorChild();
  NS_ASSERTION(objectStoreActor, "Must have an actor here!");

  ObjectStoreRequestParams params;
  nsresult rv = PackArgumentsForParentProcess(params);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NoDispatchEventTarget target;
  rv = AsyncConnectionHelper::Dispatch(&target);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mActor =
    new IndexedDBObjectStoreRequestChild(this, mObjectStore, params.type());
  objectStoreActor->SendPIndexedDBRequestConstructor(mActor, params);

  return NS_OK;
}

void
NoRequestObjectStoreHelper::ReleaseMainThreadObjects()
{
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");
  mObjectStore = nsnull;
  AsyncConnectionHelper::ReleaseMainThreadObjects();
}

nsresult
NoRequestObjectStoreHelper::UnpackResponseFromParentProcess(
                                            const ResponseValue& aResponseValue)
{
  NS_NOTREACHED("Should never get here!");
  return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
}

HelperBase::ChildProcessSendResult
NoRequestObjectStoreHelper::MaybeSendResponseToChildProcess(
                                                           nsresult aResultCode)
{
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");
  return Success_NotSent;
}

nsresult
NoRequestObjectStoreHelper::OnSuccess()
{
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");
  return NS_OK;
}

void
NoRequestObjectStoreHelper::OnError()
{
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");
  mTransaction->Abort(GetResultCode());
}

nsresult
AddHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_ASSERTION(aConnection, "Passed a null connection!");

  nsresult rv;
  bool keyUnset = mKey.IsUnset();
  PRInt64 osid = mObjectStore->Id();
  const KeyPath& keyPath = mObjectStore->GetKeyPath();

  // The "|| keyUnset" here is mostly a debugging tool. If a key isn't
  // specified we should never have a collision and so it shouldn't matter
  // if we allow overwrite or not. By not allowing overwrite we raise
  // detectable errors rather than corrupting data
  nsCOMPtr<mozIStorageStatement> stmt = !mOverwrite || keyUnset ?
    mTransaction->GetCachedStatement(
      "INSERT INTO object_data (object_store_id, key_value, data, file_ids) "
      "VALUES (:osid, :key_value, :data, :file_ids)") :
    mTransaction->GetCachedStatement(
      "INSERT OR REPLACE INTO object_data (object_store_id, key_value, data, "
                                          "file_ids) "
      "VALUES (:osid, :key_value, :data, :file_ids)");
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"), osid);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ASSERTION(!keyUnset || mObjectStore->IsAutoIncrement(),
               "Should have key unless autoincrement");

  PRInt64 autoIncrementNum = 0;

  if (mObjectStore->IsAutoIncrement()) {
    if (keyUnset) {
      autoIncrementNum = mObjectStore->Info()->nextAutoIncrementId;
      if (autoIncrementNum > (1LL << 53)) {
        return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
      }
      mKey.SetFromInteger(autoIncrementNum);
    }
    else if (mKey.IsFloat() &&
             mKey.ToFloat() >= mObjectStore->Info()->nextAutoIncrementId) {
      autoIncrementNum = floor(mKey.ToFloat());
    }

    if (keyUnset && keyPath.IsValid()) {
      // Special case where someone put an object into an autoIncrement'ing
      // objectStore with no key in its keyPath set. We needed to figure out
      // which row id we would get above before we could set that properly.

      // This is a duplicate of the js engine's byte munging here
      union {
        double d;
        PRUint64 u;
      } pun;
    
      pun.d = SwapBytes(static_cast<PRUint64>(autoIncrementNum));

      JSAutoStructuredCloneBuffer& buffer = mCloneWriteInfo.mCloneBuffer;
      PRUint64 offsetToKeyProp = mCloneWriteInfo.mOffsetToKeyProp;

      memcpy((char*)buffer.data() + offsetToKeyProp, &pun.u, sizeof(PRUint64));
    }
  }

  mKey.BindToStatement(stmt, NS_LITERAL_CSTRING("key_value"));

  // Compress the bytes before adding into the database.
  const char* uncompressed =
    reinterpret_cast<const char*>(mCloneWriteInfo.mCloneBuffer.data());
  size_t uncompressedLength = mCloneWriteInfo.mCloneBuffer.nbytes();

  size_t compressedLength = snappy::MaxCompressedLength(uncompressedLength);
  // This will hold our compressed data until the end of the method. The
  // BindBlobByName function will copy it.
  nsAutoArrayPtr<char> compressed(new char[compressedLength]);

  snappy::RawCompress(uncompressed, uncompressedLength, compressed.get(),
                      &compressedLength);

  const PRUint8* dataBuffer =
    reinterpret_cast<const PRUint8*>(compressed.get());
  size_t dataBufferLength = compressedLength;

  rv = stmt->BindBlobByName(NS_LITERAL_CSTRING("data"), dataBuffer,
                            dataBufferLength);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  // Handle blobs
  nsRefPtr<FileManager> fileManager = mDatabase->Manager();
  nsCOMPtr<nsIFile> directory = fileManager->GetDirectory();
  NS_ENSURE_TRUE(directory, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsAutoString fileIds;

  PRUint32 length = mCloneWriteInfo.mFiles.Length();
  for (PRUint32 index = 0; index < length; index++) {
    StructuredCloneFile& cloneFile = mCloneWriteInfo.mFiles[index];

    FileInfo* fileInfo = cloneFile.mFileInfo;
    nsIInputStream* inputStream = cloneFile.mInputStream;

    PRInt64 id = fileInfo->Id();
    if (inputStream) {
      // Copy it
      nsCOMPtr<nsIFile> nativeFile =
        fileManager->GetFileForId(directory, id);
      NS_ENSURE_TRUE(nativeFile, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      nsRefPtr<FileStream> outputStream = new FileStream();
      rv = outputStream->Init(nativeFile, NS_LITERAL_STRING("wb"), 0);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      rv = CopyData(inputStream, outputStream);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

      cloneFile.mFile->AddFileInfo(fileInfo);
    }

    if (index) {
      fileIds.Append(NS_LITERAL_STRING(" "));
    }
    fileIds.AppendInt(id);
  }

  if (fileIds.IsEmpty()) {
    rv = stmt->BindNullByName(NS_LITERAL_CSTRING("file_ids"));
  }
  else {
    rv = stmt->BindStringByName(NS_LITERAL_CSTRING("file_ids"), fileIds);
  }
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->Execute();
  if (rv == NS_ERROR_STORAGE_CONSTRAINT) {
    NS_ASSERTION(!keyUnset, "Generated key had a collision!?");
    return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
  }
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  PRInt64 objectDataId;
  rv = aConnection->GetLastInsertRowID(&objectDataId);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  // Update our indexes if needed.
  if (mOverwrite || !mIndexUpdateInfo.IsEmpty()) {
    rv = IDBObjectStore::UpdateIndexes(mTransaction, osid, mKey, mOverwrite,
                                       objectDataId, mIndexUpdateInfo);
    if (rv == NS_ERROR_STORAGE_CONSTRAINT) {
      return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  if (autoIncrementNum) {
    mObjectStore->Info()->nextAutoIncrementId = autoIncrementNum + 1;
  }

  return NS_OK;
}

nsresult
AddHelper::GetSuccessResult(JSContext* aCx,
                            jsval* aVal)
{
  NS_ASSERTION(!mKey.IsUnset(), "Badness!");

  mCloneWriteInfo.mCloneBuffer.clear();

  return mKey.ToJSVal(aCx, aVal);
}

void
AddHelper::ReleaseMainThreadObjects()
{
  IDBObjectStore::ClearStructuredCloneBuffer(mCloneWriteInfo.mCloneBuffer);
  ObjectStoreHelper::ReleaseMainThreadObjects();
}

nsresult
AddHelper::PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams)
{
  AddPutParams commonParams;
  commonParams.cloneInfo() = mCloneWriteInfo;
  commonParams.key() = mKey;
  commonParams.indexUpdateInfos().AppendElements(mIndexUpdateInfo);

  if (mOverwrite) {
    PutParams putParams;
    putParams.commonParams() = commonParams;
    aParams = putParams;
  }
  else {
    AddParams addParams;
    addParams.commonParams() = commonParams;
    aParams = addParams;
  }

  return NS_OK;
}

HelperBase::ChildProcessSendResult
AddHelper::MaybeSendResponseToChildProcess(nsresult aResultCode)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");

  IndexedDBRequestParentBase* actor = mRequest->GetActorParent();
  if (!actor) {
    return Success_NotSent;
  }

  ResponseValue response;
  if (NS_FAILED(aResultCode)) {
    response =  aResultCode;
  }
  else if (mOverwrite) {
    PutResponse putResponse;
    putResponse.key() = mKey;
    response = putResponse;
  }
  else {
    AddResponse addResponse;
    addResponse.key() = mKey;
    response = addResponse;
  }

  if (!actor->Send__delete__(actor, response)) {
    return Error;
  }

  return Success_Sent;
}

nsresult
AddHelper::UnpackResponseFromParentProcess(const ResponseValue& aResponseValue)
{
  NS_ASSERTION(aResponseValue.type() == ResponseValue::TAddResponse ||
               aResponseValue.type() == ResponseValue::TPutResponse,
               "Bad response type!");

  mKey = mOverwrite ?
         aResponseValue.get_PutResponse().key() :
         aResponseValue.get_AddResponse().key();

  return NS_OK;
}

nsresult
GetHelper::DoDatabaseWork(mozIStorageConnection* /* aConnection */)
{
  NS_ASSERTION(mKeyRange, "Must have a key range here!");

  nsCString keyRangeClause;
  mKeyRange->GetBindingClause(NS_LITERAL_CSTRING("key_value"), keyRangeClause);

  NS_ASSERTION(!keyRangeClause.IsEmpty(), "Huh?!");

  nsCString query = NS_LITERAL_CSTRING("SELECT data, file_ids FROM object_data "
                                       "WHERE object_store_id = :osid") +
                    keyRangeClause + NS_LITERAL_CSTRING(" LIMIT 1");

  nsCOMPtr<mozIStorageStatement> stmt = mTransaction->GetCachedStatement(query);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv =
    stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"), mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = mKeyRange->BindToStatement(stmt);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (hasResult) {
    rv = IDBObjectStore::GetStructuredCloneReadInfoFromStatement(stmt, 0, 1,
      mDatabase, mCloneReadInfo);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
GetHelper::GetSuccessResult(JSContext* aCx,
                            jsval* aVal)
{
  bool result = IDBObjectStore::DeserializeValue(aCx, mCloneReadInfo, aVal);

  mCloneReadInfo.mCloneBuffer.clear();

  NS_ENSURE_TRUE(result, NS_ERROR_DOM_DATA_CLONE_ERR);
  return NS_OK;
}

void
GetHelper::ReleaseMainThreadObjects()
{
  mKeyRange = nsnull;
  IDBObjectStore::ClearStructuredCloneBuffer(mCloneReadInfo.mCloneBuffer);
  ObjectStoreHelper::ReleaseMainThreadObjects();
}

nsresult
GetHelper::PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams)
{
  NS_ASSERTION(mKeyRange, "This should never be null!");

  FIXME_Bug_521898_objectstore::GetParams params;

  mKeyRange->ToSerializedKeyRange(params.keyRange());

  aParams = params;
  return NS_OK;
}

HelperBase::ChildProcessSendResult
GetHelper::MaybeSendResponseToChildProcess(nsresult aResultCode)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");

  IndexedDBRequestParentBase* actor = mRequest->GetActorParent();
  if (!actor) {
    return Success_NotSent;
  }

  if (!mCloneReadInfo.mFileInfos.IsEmpty()) {
    NS_WARNING("No support for transferring blobs across processes yet!");
    return Error;
  }

  ResponseValue response;
  if (NS_FAILED(aResultCode)) {
    response = aResultCode;
  }
  else {
    SerializedStructuredCloneReadInfo readInfo;
    readInfo = mCloneReadInfo;
    GetResponse getResponse = readInfo;
    response = getResponse;
  }

  if (!actor->Send__delete__(actor, response)) {
    return Error;
  }

  return Success_Sent;
}

nsresult
GetHelper::UnpackResponseFromParentProcess(const ResponseValue& aResponseValue)
{
  NS_ASSERTION(aResponseValue.type() == ResponseValue::TGetResponse,
               "Bad response type!");

  const SerializedStructuredCloneReadInfo& cloneInfo =
    aResponseValue.get_GetResponse().cloneInfo();

  NS_ASSERTION((!cloneInfo.dataLength && !cloneInfo.data) ||
               (cloneInfo.dataLength && cloneInfo.data),
               "Inconsistent clone info!");

  if (!mCloneReadInfo.SetFromSerialized(cloneInfo)) {
    NS_WARNING("Failed to copy clone buffer!");
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  return NS_OK;
}

nsresult
DeleteHelper::DoDatabaseWork(mozIStorageConnection* /*aConnection */)
{
  NS_ASSERTION(mKeyRange, "Must have a key range here!");

  nsCString keyRangeClause;
  mKeyRange->GetBindingClause(NS_LITERAL_CSTRING("key_value"), keyRangeClause);

  NS_ASSERTION(!keyRangeClause.IsEmpty(), "Huh?!");

  nsCString query = NS_LITERAL_CSTRING("DELETE FROM object_data "
                                       "WHERE object_store_id = :osid") +
                    keyRangeClause;

  nsCOMPtr<mozIStorageStatement> stmt = mTransaction->GetCachedStatement(query);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                                      mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = mKeyRange->BindToStatement(stmt);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  return NS_OK;
}

nsresult
DeleteHelper::GetSuccessResult(JSContext* aCx,
                               jsval* aVal)
{
  *aVal = JSVAL_VOID;
  return NS_OK;
}

nsresult
DeleteHelper::PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams)
{
  NS_ASSERTION(mKeyRange, "This should never be null!");

  DeleteParams params;

  mKeyRange->ToSerializedKeyRange(params.keyRange());

  aParams = params;
  return NS_OK;
}

HelperBase::ChildProcessSendResult
DeleteHelper::MaybeSendResponseToChildProcess(nsresult aResultCode)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");

  IndexedDBRequestParentBase* actor = mRequest->GetActorParent();
  if (!actor) {
    return Success_NotSent;
  }

  ResponseValue response;
  if (NS_FAILED(aResultCode)) {
    response = aResultCode;
  }
  else {
    response = DeleteResponse();
  }

  if (!actor->Send__delete__(actor, response)) {
    return Error;
  }

  return Success_Sent;
}

nsresult
DeleteHelper::UnpackResponseFromParentProcess(
                                            const ResponseValue& aResponseValue)
{
  NS_ASSERTION(aResponseValue.type() == ResponseValue::TDeleteResponse,
               "Bad response type!");

  return NS_OK;
}

nsresult
ClearHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_PRECONDITION(aConnection, "Passed a null connection!");

  nsCOMPtr<mozIStorageStatement> stmt =
    mTransaction->GetCachedStatement(
      NS_LITERAL_CSTRING("DELETE FROM object_data "
                         "WHERE object_store_id = :osid"));
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                                      mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  return NS_OK;
}

nsresult
ClearHelper::PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams)
{
  aParams = ClearParams();
  return NS_OK;
}

HelperBase::ChildProcessSendResult
ClearHelper::MaybeSendResponseToChildProcess(nsresult aResultCode)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");

  IndexedDBRequestParentBase* actor = mRequest->GetActorParent();
  if (!actor) {
    return Success_NotSent;
  }

  ResponseValue response;
  if (NS_FAILED(aResultCode)) {
    response = aResultCode;
  }
  else {
    response = ClearResponse();
  }

  if (!actor->Send__delete__(actor, response)) {
    return Error;
  }

  return Success_Sent;
}

nsresult
ClearHelper::UnpackResponseFromParentProcess(
                                            const ResponseValue& aResponseValue)
{
  NS_ASSERTION(aResponseValue.type() == ResponseValue::TClearResponse,
               "Bad response type!");

  return NS_OK;
}

nsresult
OpenCursorHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_NAMED_LITERAL_CSTRING(keyValue, "key_value");

  nsCString keyRangeClause;
  if (mKeyRange) {
    mKeyRange->GetBindingClause(keyValue, keyRangeClause);
  }

  nsCAutoString directionClause;
  switch (mDirection) {
    case IDBCursor::NEXT:
    case IDBCursor::NEXT_UNIQUE:
      directionClause.AssignLiteral(" ORDER BY key_value ASC");
      break;

    case IDBCursor::PREV:
    case IDBCursor::PREV_UNIQUE:
      directionClause.AssignLiteral(" ORDER BY key_value DESC");
      break;

    default:
      NS_NOTREACHED("Unknown direction type!");
  }

  nsCString firstQuery = NS_LITERAL_CSTRING("SELECT key_value, data, file_ids "
                                            "FROM object_data "
                                            "WHERE object_store_id = :id") +
                         keyRangeClause + directionClause +
                         NS_LITERAL_CSTRING(" LIMIT 1");

  nsCOMPtr<mozIStorageStatement> stmt =
    mTransaction->GetCachedStatement(firstQuery);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("id"),
                                      mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (mKeyRange) {
    rv = mKeyRange->BindToStatement(stmt);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (!hasResult) {
    mKey.Unset();
    return NS_OK;
  }

  rv = mKey.SetFromStatement(stmt, 0);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = IDBObjectStore::GetStructuredCloneReadInfoFromStatement(stmt, 1, 2,
    mDatabase, mCloneReadInfo);
  NS_ENSURE_SUCCESS(rv, rv);

  // Now we need to make the query to get the next match.
  keyRangeClause.Truncate();
  nsCAutoString continueToKeyRangeClause;

  NS_NAMED_LITERAL_CSTRING(currentKey, "current_key");
  NS_NAMED_LITERAL_CSTRING(rangeKey, "range_key");

  switch (mDirection) {
    case IDBCursor::NEXT:
    case IDBCursor::NEXT_UNIQUE:
      AppendConditionClause(keyValue, currentKey, false, false,
                            keyRangeClause);
      AppendConditionClause(keyValue, currentKey, false, true,
                            continueToKeyRangeClause);
      if (mKeyRange && !mKeyRange->Upper().IsUnset()) {
        AppendConditionClause(keyValue, rangeKey, true,
                              !mKeyRange->IsUpperOpen(), keyRangeClause);
        AppendConditionClause(keyValue, rangeKey, true,
                              !mKeyRange->IsUpperOpen(),
                              continueToKeyRangeClause);
        mRangeKey = mKeyRange->Upper();
      }
      break;

    case IDBCursor::PREV:
    case IDBCursor::PREV_UNIQUE:
      AppendConditionClause(keyValue, currentKey, true, false, keyRangeClause);
      AppendConditionClause(keyValue, currentKey, true, true,
                           continueToKeyRangeClause);
      if (mKeyRange && !mKeyRange->Lower().IsUnset()) {
        AppendConditionClause(keyValue, rangeKey, false,
                              !mKeyRange->IsLowerOpen(), keyRangeClause);
        AppendConditionClause(keyValue, rangeKey, false,
                              !mKeyRange->IsLowerOpen(),
                              continueToKeyRangeClause);
        mRangeKey = mKeyRange->Lower();
      }
      break;

    default:
      NS_NOTREACHED("Unknown direction type!");
  }

  NS_NAMED_LITERAL_CSTRING(queryStart, "SELECT key_value, data, file_ids "
                                       "FROM object_data "
                                       "WHERE object_store_id = :id");

  mContinueQuery = queryStart + keyRangeClause + directionClause +
                   NS_LITERAL_CSTRING(" LIMIT ");

  mContinueToQuery = queryStart + continueToKeyRangeClause + directionClause +
                     NS_LITERAL_CSTRING(" LIMIT ");

  return NS_OK;
}

nsresult
OpenCursorHelper::EnsureCursor()
{
  if (mCursor || mKey.IsUnset()) {
    return NS_OK;
  }

  mSerializedCloneReadInfo = mCloneReadInfo;

  NS_ASSERTION(mSerializedCloneReadInfo.data &&
               mSerializedCloneReadInfo.dataLength,
               "Shouldn't be possible!");

  nsRefPtr<IDBCursor> cursor =
    IDBCursor::Create(mRequest, mTransaction, mObjectStore, mDirection,
                      mRangeKey, mContinueQuery, mContinueToQuery, mKey,
                      mCloneReadInfo);
  NS_ENSURE_TRUE(cursor, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ASSERTION(!mCloneReadInfo.mCloneBuffer.data(), "Should have swapped!");

  mCursor.swap(cursor);
  return NS_OK;
}

nsresult
OpenCursorHelper::GetSuccessResult(JSContext* aCx,
                                   jsval* aVal)
{
  nsresult rv = EnsureCursor();
  NS_ENSURE_SUCCESS(rv, rv);

  if (mCursor) {
    rv = WrapNative(aCx, mCursor, aVal);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }
  else {
    *aVal = JSVAL_VOID;
  }

  return NS_OK;
}

void
OpenCursorHelper::ReleaseMainThreadObjects()
{
  mKeyRange = nsnull;
  IDBObjectStore::ClearStructuredCloneBuffer(mCloneReadInfo.mCloneBuffer);

  mCursor = nsnull;

  // These don't need to be released on the main thread but they're only valid
  // as long as mCursor is set.
  mSerializedCloneReadInfo.data = nsnull;
  mSerializedCloneReadInfo.dataLength = 0;

  ObjectStoreHelper::ReleaseMainThreadObjects();
}

nsresult
OpenCursorHelper::PackArgumentsForParentProcess(
                                              ObjectStoreRequestParams& aParams)
{
  FIXME_Bug_521898_objectstore::OpenCursorParams params;

  if (mKeyRange) {
    FIXME_Bug_521898_objectstore::KeyRange keyRange;
    mKeyRange->ToSerializedKeyRange(keyRange);
    params.optionalKeyRange() = keyRange;
  }
  else {
    params.optionalKeyRange() = mozilla::void_t();
  }

  params.direction() = mDirection;

  aParams = params;
  return NS_OK;
}

HelperBase::ChildProcessSendResult
OpenCursorHelper::MaybeSendResponseToChildProcess(nsresult aResultCode)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");

  IndexedDBRequestParentBase* actor = mRequest->GetActorParent();
  if (!actor) {
    return Success_NotSent;
  }

  if (!mCloneReadInfo.mFileInfos.IsEmpty()) {
    NS_WARNING("No support for transferring blobs across processes yet!");
    return Error;
  }

  NS_ASSERTION(!mCursor, "Shouldn't have this yet!");

  if (NS_SUCCEEDED(aResultCode)) {
    nsresult rv = EnsureCursor();
    if (NS_FAILED(rv)) {
      NS_WARNING("EnsureCursor failed!");
      aResultCode = rv;
    }
  }

  ResponseValue response;
  if (NS_FAILED(aResultCode)) {
    response = aResultCode;
  }
  else {
    OpenCursorResponse openCursorResponse;

    if (!mCursor) {
      openCursorResponse = mozilla::void_t();
    }
    else {
      IndexedDBObjectStoreParent* objectStoreActor =
        mObjectStore->GetActorParent();
      NS_ASSERTION(objectStoreActor, "Must have an actor here!");

      IndexedDBRequestParentBase* requestActor = mRequest->GetActorParent();
      NS_ASSERTION(requestActor, "Must have an actor here!");

      NS_ASSERTION(mSerializedCloneReadInfo.data &&
                   mSerializedCloneReadInfo.dataLength,
                   "Shouldn't be possible!");

      ObjectStoreCursorConstructorParams params;
      params.requestParent() = requestActor;
      params.direction() = mDirection;
      params.key() = mKey;
      params.cloneInfo() = mSerializedCloneReadInfo;

      IndexedDBCursorParent* cursorActor = new IndexedDBCursorParent(mCursor);

      if (!objectStoreActor->SendPIndexedDBCursorConstructor(cursorActor,
                                                             params)) {
        return Error;
      }

      openCursorResponse = cursorActor;
    }

    response = openCursorResponse;
  }

  if (!actor->Send__delete__(actor, response)) {
    return Error;
  }

  return Success_Sent;
}

nsresult
OpenCursorHelper::UnpackResponseFromParentProcess(
                                            const ResponseValue& aResponseValue)
{
  NS_ASSERTION(aResponseValue.type() == ResponseValue::TOpenCursorResponse,
               "Bad response type!");
  NS_ASSERTION(aResponseValue.get_OpenCursorResponse().type() ==
               OpenCursorResponse::Tvoid_t ||
               aResponseValue.get_OpenCursorResponse().type() ==
               OpenCursorResponse::TPIndexedDBCursorChild,
               "Bad response union type!");
  NS_ASSERTION(!mCursor, "Shouldn't have this yet!");

  const OpenCursorResponse& response =
    aResponseValue.get_OpenCursorResponse();

  switch (response.type()) {
    case OpenCursorResponse::Tvoid_t:
      break;

    case OpenCursorResponse::TPIndexedDBCursorChild: {
      IndexedDBCursorChild* actor =
        static_cast<IndexedDBCursorChild*>(
          response.get_PIndexedDBCursorChild());

      mCursor = actor->ForgetStrongCursor();
      NS_ASSERTION(mCursor, "This should never be null!");

    } break;

    default:
      NS_NOTREACHED("Unknown response union type!");
      return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  return NS_OK;
}

nsresult
CreateIndexHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  // Insert the data into the database.
  nsCOMPtr<mozIStorageStatement> stmt =
    mTransaction->GetCachedStatement(
    "INSERT INTO object_store_index (id, name, key_path, unique_index, "
      "multientry, object_store_id) "
    "VALUES (:id, :name, :key_path, :unique, :multientry, :osid)"
  );
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("id"),
                                      mIndex->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->BindStringByName(NS_LITERAL_CSTRING("name"), mIndex->Name());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  nsAutoString keyPathSerialization;
  mIndex->GetKeyPath().SerializeToString(keyPathSerialization);
  rv = stmt->BindStringByName(NS_LITERAL_CSTRING("key_path"),
                              keyPathSerialization);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->BindInt32ByName(NS_LITERAL_CSTRING("unique"),
                             mIndex->IsUnique() ? 1 : 0);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->BindInt32ByName(NS_LITERAL_CSTRING("multientry"),
                             mIndex->IsMultiEntry() ? 1 : 0);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                             mIndex->ObjectStore()->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (NS_FAILED(stmt->Execute())) {
    return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
  }

#ifdef DEBUG
  {
    PRInt64 id;
    aConnection->GetLastInsertRowID(&id);
    NS_ASSERTION(mIndex->Id() == id, "Bad index id!");
  }
#endif

  // Now we need to populate the index with data from the object store.
  rv = InsertDataFromObjectStore(aConnection);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

void
CreateIndexHelper::ReleaseMainThreadObjects()
{
  mIndex = nsnull;
  NoRequestObjectStoreHelper::ReleaseMainThreadObjects();
}

nsresult
CreateIndexHelper::InsertDataFromObjectStore(mozIStorageConnection* aConnection)
{
  nsCOMPtr<mozIStorageStatement> stmt =
    mTransaction->GetCachedStatement(
      NS_LITERAL_CSTRING("SELECT id, data, file_ids, key_value FROM "
                         "object_data WHERE object_store_id = :osid"));
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                                      mIndex->ObjectStore()->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  NS_ENSURE_TRUE(sTLSIndex != BAD_TLS_INDEX,
                 NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  if (!hasResult) {
    // Bail early if we have no data to avoid creating the below runtime
    return NS_OK;
  }

  ThreadLocalJSRuntime* tlsEntry =
    reinterpret_cast<ThreadLocalJSRuntime*>(PR_GetThreadPrivate(sTLSIndex));

  if (!tlsEntry) {
    tlsEntry = ThreadLocalJSRuntime::Create();
    NS_ENSURE_TRUE(tlsEntry, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    PR_SetThreadPrivate(sTLSIndex, tlsEntry);
  }

  JSContext* cx = tlsEntry->Context();
  JSAutoRequest ar(cx);

  do {
    StructuredCloneReadInfo cloneReadInfo;
    rv = IDBObjectStore::GetStructuredCloneReadInfoFromStatement(stmt, 1, 2,
      mDatabase, cloneReadInfo);
    NS_ENSURE_SUCCESS(rv, rv);

    JSAutoStructuredCloneBuffer& buffer = cloneReadInfo.mCloneBuffer;

    JSStructuredCloneCallbacks callbacks = {
      IDBObjectStore::StructuredCloneReadCallback,
      nsnull,
      nsnull
    };

    jsval clone;
    if (!buffer.read(cx, &clone, &callbacks, &cloneReadInfo)) {
      NS_WARNING("Failed to deserialize structured clone data!");
      return NS_ERROR_DOM_DATA_CLONE_ERR;
    }

    nsTArray<IndexUpdateInfo> updateInfo;
    rv = IDBObjectStore::AppendIndexUpdateInfo(mIndex->Id(),
                                               mIndex->GetKeyPath(),
                                               mIndex->IsUnique(),
                                               mIndex->IsMultiEntry(),
                                               tlsEntry->Context(),
                                               clone, updateInfo);
    NS_ENSURE_SUCCESS(rv, rv);

    PRInt64 objectDataID = stmt->AsInt64(0);

    Key key;
    rv = key.SetFromStatement(stmt, 3);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = IDBObjectStore::UpdateIndexes(mTransaction, mIndex->Id(),
                                       key, false, objectDataID, updateInfo);
    NS_ENSURE_SUCCESS(rv, rv);

  } while (NS_SUCCEEDED(rv = stmt->ExecuteStep(&hasResult)) && hasResult);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  return NS_OK;
}

void
CreateIndexHelper::DestroyTLSEntry(void* aPtr)
{
  delete reinterpret_cast<ThreadLocalJSRuntime *>(aPtr);
}

nsresult
DeleteIndexHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_PRECONDITION(!NS_IsMainThread(), "Wrong thread!");

  nsCOMPtr<mozIStorageStatement> stmt =
    mTransaction->GetCachedStatement(
      "DELETE FROM object_store_index "
      "WHERE name = :name "
    );
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindStringByName(NS_LITERAL_CSTRING("name"), mName);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (NS_FAILED(stmt->Execute())) {
    return NS_ERROR_DOM_INDEXEDDB_NOT_FOUND_ERR;
  }

  return NS_OK;
}

nsresult
GetAllHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_NAMED_LITERAL_CSTRING(lowerKeyName, "lower_key");
  NS_NAMED_LITERAL_CSTRING(upperKeyName, "upper_key");

  nsCAutoString keyRangeClause;
  if (mKeyRange) {
    if (!mKeyRange->Lower().IsUnset()) {
      keyRangeClause = NS_LITERAL_CSTRING(" AND key_value");
      if (mKeyRange->IsLowerOpen()) {
        keyRangeClause.AppendLiteral(" > :");
      }
      else {
        keyRangeClause.AppendLiteral(" >= :");
      }
      keyRangeClause.Append(lowerKeyName);
    }

    if (!mKeyRange->Upper().IsUnset()) {
      keyRangeClause += NS_LITERAL_CSTRING(" AND key_value");
      if (mKeyRange->IsUpperOpen()) {
        keyRangeClause.AppendLiteral(" < :");
      }
      else {
        keyRangeClause.AppendLiteral(" <= :");
      }
      keyRangeClause.Append(upperKeyName);
    }
  }

  nsCAutoString limitClause;
  if (mLimit != PR_UINT32_MAX) {
    limitClause.AssignLiteral(" LIMIT ");
    limitClause.AppendInt(mLimit);
  }

  nsCString query = NS_LITERAL_CSTRING("SELECT data, file_ids FROM object_data "
                                       "WHERE object_store_id = :osid") +
                    keyRangeClause +
                    NS_LITERAL_CSTRING(" ORDER BY key_value ASC") +
                    limitClause;

  mCloneReadInfos.SetCapacity(50);

  nsCOMPtr<mozIStorageStatement> stmt = mTransaction->GetCachedStatement(query);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                                      mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (mKeyRange) {
    if (!mKeyRange->Lower().IsUnset()) {
      rv = mKeyRange->Lower().BindToStatement(stmt, lowerKeyName);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    if (!mKeyRange->Upper().IsUnset()) {
      rv = mKeyRange->Upper().BindToStatement(stmt, upperKeyName);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  bool hasResult;
  while (NS_SUCCEEDED((rv = stmt->ExecuteStep(&hasResult))) && hasResult) {
    if (mCloneReadInfos.Capacity() == mCloneReadInfos.Length()) {
      if (!mCloneReadInfos.SetCapacity(mCloneReadInfos.Capacity() * 2)) {
        NS_ERROR("Out of memory!");
        return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
      }
    }

    StructuredCloneReadInfo* readInfo = mCloneReadInfos.AppendElement();
    NS_ASSERTION(readInfo, "Shouldn't fail if SetCapacity succeeded!");

    rv = IDBObjectStore::GetStructuredCloneReadInfoFromStatement(stmt, 0, 1,
      mDatabase, *readInfo);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  return NS_OK;
}

nsresult
GetAllHelper::GetSuccessResult(JSContext* aCx,
                               jsval* aVal)
{
  NS_ASSERTION(mCloneReadInfos.Length() <= mLimit, "Too many results!");

  nsresult rv = ConvertCloneReadInfosToArray(aCx, mCloneReadInfos, aVal);

  for (PRUint32 index = 0; index < mCloneReadInfos.Length(); index++) {
    mCloneReadInfos[index].mCloneBuffer.clear();
  }

  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

void
GetAllHelper::ReleaseMainThreadObjects()
{
  mKeyRange = nsnull;
  for (PRUint32 index = 0; index < mCloneReadInfos.Length(); index++) {
    IDBObjectStore::ClearStructuredCloneBuffer(
      mCloneReadInfos[index].mCloneBuffer);
  }
  ObjectStoreHelper::ReleaseMainThreadObjects();
}

nsresult
GetAllHelper::PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams)
{
  FIXME_Bug_521898_objectstore::GetAllParams params;

  if (mKeyRange) {
    FIXME_Bug_521898_objectstore::KeyRange keyRange;
    mKeyRange->ToSerializedKeyRange(keyRange);
    params.optionalKeyRange() = keyRange;
  }
  else {
    params.optionalKeyRange() = mozilla::void_t();
  }

  params.limit() = mLimit;

  aParams = params;
  return NS_OK;
}

HelperBase::ChildProcessSendResult
GetAllHelper::MaybeSendResponseToChildProcess(nsresult aResultCode)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");

  IndexedDBRequestParentBase* actor = mRequest->GetActorParent();
  if (!actor) {
    return Success_NotSent;
  }

  for (PRUint32 index = 0; index < mCloneReadInfos.Length(); index++) {
    if (!mCloneReadInfos[index].mFileInfos.IsEmpty()) {
      NS_WARNING("No support for transferring blobs across processes yet!");
      return Error;
    }
  }

  ResponseValue response;
  if (NS_FAILED(aResultCode)) {
    response = aResultCode;
  }
  else {
    GetAllResponse getAllResponse;

    InfallibleTArray<SerializedStructuredCloneReadInfo>& infos =
      getAllResponse.cloneInfos();

    infos.SetCapacity(mCloneReadInfos.Length());

    for (PRUint32 index = 0; index < mCloneReadInfos.Length(); index++) {
      SerializedStructuredCloneReadInfo* info = infos.AppendElement();
      *info = mCloneReadInfos[index];
    }

    getAllResponse = infos;
    response = getAllResponse;
  }

  if (!actor->Send__delete__(actor, response)) {
    return Error;
  }

  return Success_Sent;
}

nsresult
GetAllHelper::UnpackResponseFromParentProcess(
                                            const ResponseValue& aResponseValue)
{
  NS_ASSERTION(aResponseValue.type() == ResponseValue::TGetAllResponse,
               "Bad response type!");

  const InfallibleTArray<SerializedStructuredCloneReadInfo>& cloneInfos =
    aResponseValue.get_GetAllResponse().cloneInfos();

  mCloneReadInfos.SetCapacity(cloneInfos.Length());

  for (PRUint32 index = 0; index < cloneInfos.Length(); index++) {
    const SerializedStructuredCloneReadInfo srcInfo = cloneInfos[index];

    StructuredCloneReadInfo* destInfo = mCloneReadInfos.AppendElement();
    if (!destInfo->SetFromSerialized(srcInfo)) {
      NS_WARNING("Failed to copy clone buffer!");
      return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
    }
  }

  return NS_OK;
}

nsresult
CountHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  NS_NAMED_LITERAL_CSTRING(lowerKeyName, "lower_key");
  NS_NAMED_LITERAL_CSTRING(upperKeyName, "upper_key");

  nsCAutoString keyRangeClause;
  if (mKeyRange) {
    if (!mKeyRange->Lower().IsUnset()) {
      keyRangeClause = NS_LITERAL_CSTRING(" AND key_value");
      if (mKeyRange->IsLowerOpen()) {
        keyRangeClause.AppendLiteral(" > :");
      }
      else {
        keyRangeClause.AppendLiteral(" >= :");
      }
      keyRangeClause.Append(lowerKeyName);
    }

    if (!mKeyRange->Upper().IsUnset()) {
      keyRangeClause += NS_LITERAL_CSTRING(" AND key_value");
      if (mKeyRange->IsUpperOpen()) {
        keyRangeClause.AppendLiteral(" < :");
      }
      else {
        keyRangeClause.AppendLiteral(" <= :");
      }
      keyRangeClause.Append(upperKeyName);
    }
  }

  nsCString query = NS_LITERAL_CSTRING("SELECT count(*) FROM object_data "
                                       "WHERE object_store_id = :osid") +
                    keyRangeClause;

  nsCOMPtr<mozIStorageStatement> stmt = mTransaction->GetCachedStatement(query);
  NS_ENSURE_TRUE(stmt, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("osid"),
                                      mObjectStore->Id());
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  if (mKeyRange) {
    if (!mKeyRange->Lower().IsUnset()) {
      rv = mKeyRange->Lower().BindToStatement(stmt, lowerKeyName);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    if (!mKeyRange->Upper().IsUnset()) {
      rv = mKeyRange->Upper().BindToStatement(stmt, upperKeyName);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  NS_ENSURE_TRUE(hasResult, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  mCount = stmt->AsInt64(0);
  return NS_OK;
}

nsresult
CountHelper::GetSuccessResult(JSContext* aCx,
                              jsval* aVal)
{
  if (!JS_NewNumberValue(aCx, static_cast<double>(mCount), aVal)) {
    NS_WARNING("Failed to make number value!");
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  return NS_OK;
}

void
CountHelper::ReleaseMainThreadObjects()
{
  mKeyRange = nsnull;
  ObjectStoreHelper::ReleaseMainThreadObjects();
}

nsresult
CountHelper::PackArgumentsForParentProcess(ObjectStoreRequestParams& aParams)
{
  FIXME_Bug_521898_objectstore::CountParams params;

  if (mKeyRange) {
    FIXME_Bug_521898_objectstore::KeyRange keyRange;
    mKeyRange->ToSerializedKeyRange(keyRange);
    params.optionalKeyRange() = keyRange;
  }
  else {
    params.optionalKeyRange() = mozilla::void_t();
  }

  aParams = params;
  return NS_OK;
}

HelperBase::ChildProcessSendResult
CountHelper::MaybeSendResponseToChildProcess(nsresult aResultCode)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(IndexedDatabaseManager::IsMainProcess(), "Wrong process!");

  IndexedDBRequestParentBase* actor = mRequest->GetActorParent();
  if (!actor) {
    return Success_NotSent;
  }

  ResponseValue response;
  if (NS_FAILED(aResultCode)) {
    response = aResultCode;
  }
  else {
    CountResponse countResponse = mCount;
    response = countResponse;
  }

  if (!actor->Send__delete__(actor, response)) {
    return Error;
  }

  return Success_Sent;
}

nsresult
CountHelper::UnpackResponseFromParentProcess(
                                            const ResponseValue& aResponseValue)
{
  NS_ASSERTION(aResponseValue.type() == ResponseValue::TCountResponse,
               "Bad response type!");

  mCount = aResponseValue.get_CountResponse().count();
  return NS_OK;
}
