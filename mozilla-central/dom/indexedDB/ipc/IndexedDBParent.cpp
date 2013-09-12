/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "IndexedDBParent.h"

#include "nsIDOMFile.h"
#include "nsIDOMEvent.h"
#include "nsIIDBVersionChangeEvent.h"
#include "nsIJSContextStack.h"
#include "nsIXPConnect.h"

#include "mozilla/AppProcessPermissions.h"
#include "mozilla/Assertions.h"
#include "mozilla/unused.h"
#include "mozilla/Util.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/TabParent.h"
#include "mozilla/dom/ipc/Blob.h"
#include "nsContentUtils.h"

#include "AsyncConnectionHelper.h"
#include "DatabaseInfo.h"
#include "IDBDatabase.h"
#include "IDBEvents.h"
#include "IDBFactory.h"
#include "IDBIndex.h"
#include "IDBKeyRange.h"
#include "IDBObjectStore.h"
#include "IDBTransaction.h"

#define CHROME_ORIGIN "chrome"
#define PERMISSION_PREFIX "indexedDB-chrome-"
#define PERMISSION_SUFFIX_READ "-read"
#define PERMISSION_SUFFIX_WRITE "-write"

USING_INDEXEDDB_NAMESPACE

using namespace mozilla;
using namespace mozilla::dom;

/*******************************************************************************
 * AutoSetCurrentTransaction
 ******************************************************************************/

AutoSetCurrentTransaction::AutoSetCurrentTransaction(
                                                   IDBTransaction* aTransaction)
{
  MOZ_ASSERT(aTransaction);
  AsyncConnectionHelper::SetCurrentTransaction(aTransaction);
}

AutoSetCurrentTransaction::~AutoSetCurrentTransaction()
{
  AsyncConnectionHelper::SetCurrentTransaction(NULL);
}

/*******************************************************************************
 * IndexedDBParent
 ******************************************************************************/

IndexedDBParent::IndexedDBParent(ContentParent* aContentParent)
: mManagerContent(aContentParent), mManagerTab(nullptr), mDisconnected(false)
{
  MOZ_COUNT_CTOR(IndexedDBParent);
  MOZ_ASSERT(aContentParent);
}

IndexedDBParent::IndexedDBParent(TabParent* aTabParent)
: mManagerContent(nullptr), mManagerTab(aTabParent), mDisconnected(false)
{
  MOZ_COUNT_CTOR(IndexedDBParent);
  MOZ_ASSERT(aTabParent);
}

IndexedDBParent::~IndexedDBParent()
{
  MOZ_COUNT_DTOR(IndexedDBParent);
}

void
IndexedDBParent::Disconnect()
{
  MOZ_ASSERT(!mDisconnected);

  mDisconnected = true;

  const InfallibleTArray<PIndexedDBDatabaseParent*>& databases =
    ManagedPIndexedDBDatabaseParent();
  for (uint32_t i = 0; i < databases.Length(); ++i) {
    static_cast<IndexedDBDatabaseParent*>(databases[i])->Disconnect();
  }
}

bool
IndexedDBParent::CheckReadPermission(const nsAString& aDatabaseName)
{
  NS_NAMED_LITERAL_CSTRING(permission, PERMISSION_SUFFIX_READ);
  return CheckPermissionInternal(aDatabaseName, permission);
}

bool
IndexedDBParent::CheckWritePermission(const nsAString& aDatabaseName)
{
  // Write permission assumes read permission is granted as well.
  MOZ_ASSERT(CheckReadPermission(aDatabaseName));

  NS_NAMED_LITERAL_CSTRING(permission, PERMISSION_SUFFIX_WRITE);
  return CheckPermissionInternal(aDatabaseName, permission);
}

bool
IndexedDBParent::CheckPermissionInternal(const nsAString& aDatabaseName,
                                         const nsDependentCString& aPermission)
{
  MOZ_ASSERT(!mASCIIOrigin.IsEmpty());
  MOZ_ASSERT(mManagerContent || mManagerTab);

  if (mASCIIOrigin.EqualsLiteral(CHROME_ORIGIN)) {
    nsAutoCString fullPermission =
      NS_LITERAL_CSTRING(PERMISSION_PREFIX) +
      NS_ConvertUTF16toUTF8(aDatabaseName) +
      aPermission;

    if ((mManagerContent &&
         !AssertAppProcessPermission(mManagerContent, fullPermission.get())) ||
        (mManagerTab &&
         !AssertAppProcessPermission(mManagerTab, fullPermission.get()))) {
      return false;
    }
  }

  return true;
}

void
IndexedDBParent::ActorDestroy(ActorDestroyReason aWhy)
{
  // Nothing really needs to be done here...
}

bool
IndexedDBParent::RecvPIndexedDBDatabaseConstructor(
                                               PIndexedDBDatabaseParent* aActor,
                                               const nsString& aName,
                                               const uint64_t& aVersion)
{
  if (!CheckReadPermission(aName)) {
    return false;
  }

  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mFactory) {
    return true;
  }

  nsRefPtr<IDBOpenDBRequest> request;
  nsresult rv =
    mFactory->OpenCommon(aName, aVersion, false, nullptr,
                         getter_AddRefs(request));
  NS_ENSURE_SUCCESS(rv, false);

  IndexedDBDatabaseParent* actor =
    static_cast<IndexedDBDatabaseParent*>(aActor);

  rv = actor->SetOpenRequest(request);
  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

bool
IndexedDBParent::RecvPIndexedDBDeleteDatabaseRequestConstructor(
                                  PIndexedDBDeleteDatabaseRequestParent* aActor,
                                  const nsString& aName)
{
  if (!CheckWritePermission(aName)) {
    return false;
  }

  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mFactory) {
    return true;
  }

  IndexedDBDeleteDatabaseRequestParent* actor =
    static_cast<IndexedDBDeleteDatabaseRequestParent*>(aActor);

  nsRefPtr<IDBOpenDBRequest> request;

  nsresult rv =
    mFactory->OpenCommon(aName, 0, true, nullptr, getter_AddRefs(request));
  NS_ENSURE_SUCCESS(rv, false);

  rv = actor->SetOpenRequest(request);
  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

PIndexedDBDatabaseParent*
IndexedDBParent::AllocPIndexedDBDatabase(const nsString& aName,
                                         const uint64_t& aVersion)
{
  return new IndexedDBDatabaseParent();
}

bool
IndexedDBParent::DeallocPIndexedDBDatabase(PIndexedDBDatabaseParent* aActor)
{
  delete aActor;
  return true;
}

PIndexedDBDeleteDatabaseRequestParent*
IndexedDBParent::AllocPIndexedDBDeleteDatabaseRequest(const nsString& aName)
{
  return new IndexedDBDeleteDatabaseRequestParent(mFactory);
}

bool
IndexedDBParent::DeallocPIndexedDBDeleteDatabaseRequest(
                                  PIndexedDBDeleteDatabaseRequestParent* aActor)
{
  delete aActor;
  return true;
}

/*******************************************************************************
 * IndexedDBDatabaseParent
 ******************************************************************************/

IndexedDBDatabaseParent::IndexedDBDatabaseParent()
: mEventListener(ALLOW_THIS_IN_INITIALIZER_LIST(this))
{
  MOZ_COUNT_CTOR(IndexedDBDatabaseParent);
}

IndexedDBDatabaseParent::~IndexedDBDatabaseParent()
{
  MOZ_COUNT_DTOR(IndexedDBDatabaseParent);
}

nsresult
IndexedDBDatabaseParent::SetOpenRequest(IDBOpenDBRequest* aRequest)
{
  MOZ_ASSERT(aRequest);
  MOZ_ASSERT(!mOpenRequest);

  nsIDOMEventTarget* target = static_cast<nsIDOMEventTarget*>(aRequest);

  nsresult rv = target->AddEventListener(NS_LITERAL_STRING(SUCCESS_EVT_STR),
                                         mEventListener, false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = target->AddEventListener(NS_LITERAL_STRING(ERROR_EVT_STR),
                                mEventListener, false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = target->AddEventListener(NS_LITERAL_STRING(BLOCKED_EVT_STR),
                                mEventListener, false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = target->AddEventListener(NS_LITERAL_STRING(UPGRADENEEDED_EVT_STR),
                                mEventListener, false);
  NS_ENSURE_SUCCESS(rv, rv);

  mOpenRequest = aRequest;
  return NS_OK;
}

nsresult
IndexedDBDatabaseParent::HandleEvent(nsIDOMEvent* aEvent)
{
  MOZ_ASSERT(aEvent);

  if (IsDisconnected()) {
    // We're shutting down, ignore this event.
    return NS_OK;
  }

  nsString type;
  nsresult rv = aEvent->GetType(type);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMEventTarget> target;
  rv = aEvent->GetTarget(getter_AddRefs(target));
  NS_ENSURE_SUCCESS(rv, rv);

  if (mDatabase &&
      SameCOMIdentity(target, NS_ISUPPORTS_CAST(nsIDOMEventTarget*,
                                                mDatabase))) {
    rv = HandleDatabaseEvent(aEvent, type);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  if (mOpenRequest &&
      SameCOMIdentity(target, NS_ISUPPORTS_CAST(nsIDOMEventTarget*,
                                                mOpenRequest))) {
    rv = HandleRequestEvent(aEvent, type);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  MOZ_NOT_REACHED("Unexpected message!");
  return NS_ERROR_UNEXPECTED;
}

void
IndexedDBDatabaseParent::Disconnect()
{
  if (mDatabase) {
    mDatabase->DisconnectFromActorParent();
  }
}

bool
IndexedDBDatabaseParent::CheckWritePermission(const nsAString& aDatabaseName)
{
  IndexedDBParent* manager = static_cast<IndexedDBParent*>(Manager());
  MOZ_ASSERT(manager);

  return manager->CheckWritePermission(aDatabaseName);
}

void
IndexedDBDatabaseParent::Invalidate()
{
  MOZ_ASSERT(mDatabase);

  if (!IsDisconnected()) {
    mozilla::unused << SendInvalidate();
  }
}

nsresult
IndexedDBDatabaseParent::HandleRequestEvent(nsIDOMEvent* aEvent,
                                            const nsAString& aType)
{
  MOZ_ASSERT(mOpenRequest);
  MOZ_ASSERT(!IsDisconnected());

  nsresult rv;

  if (aType.EqualsLiteral(ERROR_EVT_STR)) {
    nsRefPtr<IDBOpenDBRequest> request;
    mOpenRequest.swap(request);

    rv = request->GetErrorCode();
    MOZ_ASSERT(NS_FAILED(rv));

    if (!SendError(rv)) {
      return NS_ERROR_FAILURE;
    }

    rv = aEvent->PreventDefault();
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  if (aType.EqualsLiteral(BLOCKED_EVT_STR)) {
    MOZ_ASSERT(!mDatabase);

    nsCOMPtr<nsIIDBVersionChangeEvent> changeEvent = do_QueryInterface(aEvent);
    NS_ENSURE_TRUE(changeEvent, NS_ERROR_FAILURE);

    uint64_t oldVersion;
    rv = changeEvent->GetOldVersion(&oldVersion);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!SendBlocked(oldVersion)) {
      return NS_ERROR_FAILURE;
    }

    return NS_OK;
  }

  jsval result;
  rv = mOpenRequest->GetResult(&result);
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_ASSERT(!JSVAL_IS_PRIMITIVE(result));

  nsIXPConnect* xpc = nsContentUtils::XPConnect();
  MOZ_ASSERT(xpc);

  JSContext* cx =  nsContentUtils::ThreadJSContextStack()->GetSafeJSContext();
  MOZ_ASSERT(cx);

  nsCOMPtr<nsIXPConnectWrappedNative> wrapper;
  rv = xpc->GetWrappedNativeOfJSObject(cx, JSVAL_TO_OBJECT(result),
                                       getter_AddRefs(wrapper));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIIDBDatabase> database;
  if (!wrapper || !(database = do_QueryInterface(wrapper->Native()))) {
    NS_WARNING("Didn't get the object we expected!");
    return NS_ERROR_FAILURE;
  }

  IDBDatabase* databaseConcrete = static_cast<IDBDatabase*>(database.get());

  DatabaseInfo* dbInfo = databaseConcrete->Info();
  MOZ_ASSERT(dbInfo);

  nsAutoTArray<nsString, 20> objectStoreNames;
  if (!dbInfo->GetObjectStoreNames(objectStoreNames)) {
    MOZ_NOT_REACHED("This should never fail!");
  }

  InfallibleTArray<ObjectStoreInfoGuts> objectStoreInfos;
  if (!objectStoreNames.IsEmpty()) {
    uint32_t length = objectStoreNames.Length();

    objectStoreInfos.SetCapacity(length);

    for (uint32_t i = 0; i < length; i++) {
      ObjectStoreInfo* osInfo = dbInfo->GetObjectStore(objectStoreNames[i]);
      MOZ_ASSERT(osInfo);

      objectStoreInfos.AppendElement(*osInfo);
    }
  }

  if (aType.EqualsLiteral(SUCCESS_EVT_STR)) {
    nsRefPtr<IDBOpenDBRequest> request;
    mOpenRequest.swap(request);

    nsIDOMEventTarget* target =
      static_cast<nsIDOMEventTarget*>(databaseConcrete);

#ifdef DEBUG
    {
      nsresult rvDEBUG =
        target->AddEventListener(NS_LITERAL_STRING(ERROR_EVT_STR),
                                 mEventListener, false);
      NS_WARN_IF_FALSE(NS_SUCCEEDED(rvDEBUG), "Failed to add error listener!");
    }
#endif

    NS_NAMED_LITERAL_STRING(versionChange, VERSIONCHANGE_EVT_STR);
    rv = target->AddEventListener(versionChange, mEventListener, false);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!SendSuccess(*dbInfo, objectStoreInfos)) {
      return NS_ERROR_FAILURE;
    }

    MOZ_ASSERT(!mDatabase || mDatabase == databaseConcrete);

    if (!mDatabase) {
      databaseConcrete->SetActor(this);
      mDatabase = databaseConcrete;
    }

    return NS_OK;
  }

  if (aType.EqualsLiteral(UPGRADENEEDED_EVT_STR)) {
    MOZ_ASSERT(!mDatabase);

    IDBTransaction* transaction =
      AsyncConnectionHelper::GetCurrentTransaction();
    MOZ_ASSERT(transaction);

    if (!CheckWritePermission(databaseConcrete->Name())) {
      // If we get here then the child process is either dead or in the process
      // of being killed. Abort the transaction now to prevent any changes to
      // the database.
      if (NS_FAILED(transaction->Abort())) {
        NS_WARNING("Failed to abort transaction!");
      }
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIIDBVersionChangeEvent> changeEvent = do_QueryInterface(aEvent);
    NS_ENSURE_TRUE(changeEvent, NS_ERROR_FAILURE);

    uint64_t oldVersion;
    rv = changeEvent->GetOldVersion(&oldVersion);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoPtr<IndexedDBVersionChangeTransactionParent> actor(
      new IndexedDBVersionChangeTransactionParent());

    rv = actor->SetTransaction(transaction);
    NS_ENSURE_SUCCESS(rv, rv);

    VersionChangeTransactionParams versionChangeParams;
    versionChangeParams.dbInfo() = *dbInfo;
    versionChangeParams.osInfo() = objectStoreInfos;
    versionChangeParams.oldVersion() = oldVersion;

    if (!SendPIndexedDBTransactionConstructor(actor.forget(),
                                              versionChangeParams)) {
      return NS_ERROR_FAILURE;
    }

    databaseConcrete->SetActor(this);
    mDatabase = databaseConcrete;

    return NS_OK;
  }

  MOZ_NOT_REACHED("Unexpected message type!");
  return NS_ERROR_UNEXPECTED;
}

nsresult
IndexedDBDatabaseParent::HandleDatabaseEvent(nsIDOMEvent* aEvent,
                                             const nsAString& aType)
{
  MOZ_ASSERT(mDatabase);
  MOZ_ASSERT(!aType.EqualsLiteral(ERROR_EVT_STR),
             "Should never get error events in the parent process!");
  MOZ_ASSERT(!IsDisconnected());

  nsresult rv;

  if (aType.EqualsLiteral(VERSIONCHANGE_EVT_STR)) {
    JSContext* cx = nsContentUtils::GetSafeJSContext();
    NS_ENSURE_TRUE(cx, NS_ERROR_FAILURE);

    nsCOMPtr<nsIIDBVersionChangeEvent> changeEvent = do_QueryInterface(aEvent);
    NS_ENSURE_TRUE(changeEvent, NS_ERROR_FAILURE);

    uint64_t oldVersion;
    rv = changeEvent->GetOldVersion(&oldVersion);
    NS_ENSURE_SUCCESS(rv, rv);

    JS::Value newVersionVal;
    rv = changeEvent->GetNewVersion(cx, &newVersionVal);
    NS_ENSURE_SUCCESS(rv, rv);

    uint64_t newVersion;
    if (newVersionVal.isNull()) {
      newVersion = 0;
    }
    else {
      MOZ_ASSERT(newVersionVal.isNumber());
      newVersion = static_cast<uint64_t>(newVersionVal.toNumber());
    }

    if (!SendVersionChange(oldVersion, newVersion)) {
      return NS_ERROR_FAILURE;
    }

    return NS_OK;
  }

  MOZ_NOT_REACHED("Unexpected message type!");
  return NS_ERROR_UNEXPECTED;
}

void
IndexedDBDatabaseParent::ActorDestroy(ActorDestroyReason aWhy)
{
  if (mDatabase) {
    mDatabase->SetActor(static_cast<IndexedDBDatabaseParent*>(NULL));
    mDatabase->Invalidate();
  }
}

bool
IndexedDBDatabaseParent::RecvClose(const bool& aUnlinked)
{
  MOZ_ASSERT(mDatabase);

  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  mDatabase->CloseInternal(aUnlinked);
  return true;
}

bool
IndexedDBDatabaseParent::RecvPIndexedDBTransactionConstructor(
                                            PIndexedDBTransactionParent* aActor,
                                            const TransactionParams& aParams)
{
  MOZ_ASSERT(aParams.type() ==
             TransactionParams::TNormalTransactionParams);
  MOZ_ASSERT(!mOpenRequest);

  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mDatabase) {
    return true;
  }

  IndexedDBTransactionParent* actor =
    static_cast<IndexedDBTransactionParent*>(aActor);

  const NormalTransactionParams& params = aParams.get_NormalTransactionParams();

  if (params.mode() != IDBTransaction::READ_ONLY &&
      !CheckWritePermission(mDatabase->Name())) {
    return false;
  }

  if (mDatabase->IsClosed()) {
    // If the window was navigated then we won't be able to do anything here.
    return true;
  }

  nsTArray<nsString> storesToOpen;
  storesToOpen.AppendElements(params.names());

  nsRefPtr<IDBTransaction> transaction =
    IDBTransaction::Create(mDatabase, storesToOpen, params.mode(), false);
  NS_ENSURE_TRUE(transaction, false);

  nsresult rv = actor->SetTransaction(transaction);
  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

PIndexedDBTransactionParent*
IndexedDBDatabaseParent::AllocPIndexedDBTransaction(
                                               const TransactionParams& aParams)
{
  MOZ_ASSERT(aParams.type() ==
             TransactionParams::TNormalTransactionParams);
  return new IndexedDBTransactionParent();
}

bool
IndexedDBDatabaseParent::DeallocPIndexedDBTransaction(
                                            PIndexedDBTransactionParent* aActor)
{
  delete aActor;
  return true;
}

/*******************************************************************************
 * IndexedDBTransactionParent
 ******************************************************************************/

IndexedDBTransactionParent::IndexedDBTransactionParent()
: mEventListener(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
  mArtificialRequestCount(false)
{
  MOZ_COUNT_CTOR(IndexedDBTransactionParent);
}

IndexedDBTransactionParent::~IndexedDBTransactionParent()
{
  MOZ_COUNT_DTOR(IndexedDBTransactionParent);
}

nsresult
IndexedDBTransactionParent::SetTransaction(IDBTransaction* aTransaction)
{
  MOZ_ASSERT(aTransaction);
  MOZ_ASSERT(!mTransaction);

  nsIDOMEventTarget* target = static_cast<nsIDOMEventTarget*>(aTransaction);

  NS_NAMED_LITERAL_STRING(complete, COMPLETE_EVT_STR);
  nsresult rv = target->AddEventListener(complete, mEventListener, false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = target->AddEventListener(NS_LITERAL_STRING(ABORT_EVT_STR),
                                mEventListener, false);
  NS_ENSURE_SUCCESS(rv, rv);

  aTransaction->OnNewRequest();
  mArtificialRequestCount = true;

  aTransaction->SetActor(this);

  mTransaction = aTransaction;
  return NS_OK;
}

nsresult
IndexedDBTransactionParent::HandleEvent(nsIDOMEvent* aEvent)
{
  MOZ_ASSERT(aEvent);

  if (IsDisconnected()) {
    // We're shutting down, ignore this event.
    return NS_OK;
  }

  nsString type;
  nsresult rv = aEvent->GetType(type);
  NS_ENSURE_SUCCESS(rv, rv);

  CompleteParams params;

  if (type.EqualsLiteral(COMPLETE_EVT_STR)) {
    params = CompleteResult();
  }
  else if (type.EqualsLiteral(ABORT_EVT_STR)) {
#ifdef DEBUG
    {
      nsCOMPtr<nsIDOMEventTarget> target;
      if (NS_FAILED(aEvent->GetTarget(getter_AddRefs(target)))) {
        NS_WARNING("Failed to get target!");
      }
      else {
        MOZ_ASSERT(SameCOMIdentity(target, NS_ISUPPORTS_CAST(nsIDOMEventTarget*,
                                                             mTransaction)));
      }
    }
#endif
    params = AbortResult(mTransaction->GetAbortCode());
  }
  else {
    NS_WARNING("Unknown message type!");
    return NS_ERROR_UNEXPECTED;
  }

  if (!SendComplete(params)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void
IndexedDBTransactionParent::ActorDestroy(ActorDestroyReason aWhy)
{
  if (mTransaction) {
    if (mArtificialRequestCount) {
      // The transaction never completed and now the child side is dead. Abort
      // here to be safe.
      mTransaction->Abort();

      mTransaction->OnRequestFinished();
#ifdef DEBUG
      mArtificialRequestCount = false;
#endif
    }
    mTransaction->SetActor(static_cast<IndexedDBTransactionParent*>(NULL));
  }
}

bool
IndexedDBTransactionParent::RecvAbort(const nsresult& aAbortCode)
{
  MOZ_ASSERT(mTransaction);

  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  mTransaction->Abort(aAbortCode);
  return true;
}

bool
IndexedDBTransactionParent::RecvAllRequestsFinished()
{
  MOZ_ASSERT(mTransaction);
  MOZ_ASSERT(mArtificialRequestCount);

  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  mTransaction->OnRequestFinished();
  mArtificialRequestCount = false;

  return true;
}

bool
IndexedDBTransactionParent::RecvDeleteObjectStore(const nsString& aName)
{
  MOZ_NOT_REACHED("Should be overridden, don't call me!");
  return false;
}

bool
IndexedDBTransactionParent::RecvPIndexedDBObjectStoreConstructor(
                                    PIndexedDBObjectStoreParent* aActor,
                                    const ObjectStoreConstructorParams& aParams)
{
  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mTransaction) {
    return true;
  }

  IndexedDBObjectStoreParent* actor =
    static_cast<IndexedDBObjectStoreParent*>(aActor);

  if (aParams.type() ==
      ObjectStoreConstructorParams::TGetObjectStoreParams) {
    const GetObjectStoreParams& params = aParams.get_GetObjectStoreParams();
    const nsString& name = params.name();

    nsRefPtr<IDBObjectStore> objectStore;

    {
      AutoSetCurrentTransaction asct(mTransaction);

      nsresult rv =
        mTransaction->ObjectStoreInternal(name, getter_AddRefs(objectStore));
      NS_ENSURE_SUCCESS(rv, false);

      actor->SetObjectStore(objectStore);
    }

    objectStore->SetActor(actor);
    return true;
  }

  if (aParams.type() ==
      ObjectStoreConstructorParams::TCreateObjectStoreParams) {
    MOZ_NOT_REACHED("Should be overridden, don't call me!");
    return false;
  }

  MOZ_NOT_REACHED("Unknown param type!");
  return false;
}

PIndexedDBObjectStoreParent*
IndexedDBTransactionParent::AllocPIndexedDBObjectStore(
                                    const ObjectStoreConstructorParams& aParams)
{
  return new IndexedDBObjectStoreParent();
}

bool
IndexedDBTransactionParent::DeallocPIndexedDBObjectStore(
                                            PIndexedDBObjectStoreParent* aActor)
{
  delete aActor;
  return true;
}

/*******************************************************************************
 * IndexedDBVersionChangeTransactionParent
 ******************************************************************************/

IndexedDBVersionChangeTransactionParent::
  IndexedDBVersionChangeTransactionParent()
{
  MOZ_COUNT_CTOR(IndexedDBVersionChangeTransactionParent);
}

IndexedDBVersionChangeTransactionParent::
  ~IndexedDBVersionChangeTransactionParent()
{
  MOZ_COUNT_DTOR(IndexedDBVersionChangeTransactionParent);
}

bool
IndexedDBVersionChangeTransactionParent::RecvDeleteObjectStore(
                                                          const nsString& aName)
{
  MOZ_ASSERT(!mTransaction ||
             mTransaction->GetMode() == IDBTransaction::VERSION_CHANGE);

  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mTransaction) {
    return true;
  }

  if (mTransaction->Database()->IsInvalidated()) {
    // If we've invalidated this database in the parent then we should bail out
    // now to avoid logic problems that could force-kill the child.
    return true;
  }

  IDBDatabase* db = mTransaction->Database();
  MOZ_ASSERT(db);

  nsresult rv;

  {
    AutoSetCurrentTransaction asct(mTransaction);

    rv = db->DeleteObjectStore(aName);
  }

  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

bool
IndexedDBVersionChangeTransactionParent::RecvPIndexedDBObjectStoreConstructor(
                                    PIndexedDBObjectStoreParent* aActor,
                                    const ObjectStoreConstructorParams& aParams)
{
  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mTransaction) {
    return true;
  }

  if (mTransaction->Database()->IsInvalidated()) {
    // If we've invalidated this database in the parent then we should bail out
    // now to avoid logic problems that could force-kill the child.
    return true;
  }

  IndexedDBObjectStoreParent* actor =
    static_cast<IndexedDBObjectStoreParent*>(aActor);

  if (aParams.type() ==
      ObjectStoreConstructorParams::TCreateObjectStoreParams) {
    MOZ_ASSERT(mTransaction->GetMode() == IDBTransaction::VERSION_CHANGE);

    const CreateObjectStoreParams& params =
      aParams.get_CreateObjectStoreParams();

    const ObjectStoreInfoGuts& info = params.info();

    IDBDatabase* db = mTransaction->Database();
    MOZ_ASSERT(db);

    nsRefPtr<IDBObjectStore> objectStore;

    nsresult rv;

    {
      AutoSetCurrentTransaction asct(mTransaction);

      rv = db->CreateObjectStoreInternal(mTransaction, info,
                                         getter_AddRefs(objectStore));
    }

    NS_ENSURE_SUCCESS(rv, false);

    actor->SetObjectStore(objectStore);
    objectStore->SetActor(actor);
    return true;
  }

  return
    IndexedDBTransactionParent::RecvPIndexedDBObjectStoreConstructor(aActor,
                                                                     aParams);
}

PIndexedDBObjectStoreParent*
IndexedDBVersionChangeTransactionParent::AllocPIndexedDBObjectStore(
                                    const ObjectStoreConstructorParams& aParams)
{
  if (aParams.type() ==
      ObjectStoreConstructorParams::TCreateObjectStoreParams ||
      mTransaction->GetMode() == IDBTransaction::VERSION_CHANGE) {
    return new IndexedDBVersionChangeObjectStoreParent();
  }

  return IndexedDBTransactionParent::AllocPIndexedDBObjectStore(aParams);
}

/*******************************************************************************
 * IndexedDBCursorParent
 ******************************************************************************/

IndexedDBCursorParent::IndexedDBCursorParent(IDBCursor* aCursor)
: mCursor(aCursor)
{
  MOZ_COUNT_CTOR(IndexedDBCursorParent);
  MOZ_ASSERT(aCursor);
  aCursor->SetActor(this);
}

IndexedDBCursorParent::~IndexedDBCursorParent()
{
  MOZ_COUNT_DTOR(IndexedDBCursorParent);
}

bool
IndexedDBCursorParent::IsDisconnected() const
{
  MOZ_ASSERT(mCursor);
  return mCursor->Transaction()->GetActorParent()->IsDisconnected();
}

void
IndexedDBCursorParent::ActorDestroy(ActorDestroyReason aWhy)
{
  MOZ_ASSERT(mCursor);
  mCursor->SetActor(static_cast<IndexedDBCursorParent*>(NULL));
}

bool
IndexedDBCursorParent::RecvPIndexedDBRequestConstructor(
                                             PIndexedDBRequestParent* aActor,
                                             const CursorRequestParams& aParams)
{
  MOZ_ASSERT(mCursor);

  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  IndexedDBCursorRequestParent* actor =
    static_cast<IndexedDBCursorRequestParent*>(aActor);

  if (mCursor->Transaction()->Database()->IsInvalidated()) {
    // If we've invalidated this database in the parent then we should bail out
    // now to avoid logic problems that could force-kill the child.
    return actor->Send__delete__(actor, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  switch (aParams.type()) {
    case CursorRequestParams::TContinueParams:
      return actor->Continue(aParams.get_ContinueParams());

    default:
      MOZ_NOT_REACHED("Unknown type!");
      return false;
  }

  MOZ_NOT_REACHED("Should never get here!");
  return false;
}

PIndexedDBRequestParent*
IndexedDBCursorParent::AllocPIndexedDBRequest(
                                             const CursorRequestParams& aParams)
{
  MOZ_ASSERT(mCursor);
  return new IndexedDBCursorRequestParent(mCursor, aParams.type());
}

bool
IndexedDBCursorParent::DeallocPIndexedDBRequest(PIndexedDBRequestParent* aActor)
{
  delete aActor;
  return true;
}

/*******************************************************************************
 * IndexedDBObjectStoreParent
 ******************************************************************************/

IndexedDBObjectStoreParent::IndexedDBObjectStoreParent()
{
  MOZ_COUNT_CTOR(IndexedDBObjectStoreParent);
}

IndexedDBObjectStoreParent::~IndexedDBObjectStoreParent()
{
  MOZ_COUNT_DTOR(IndexedDBObjectStoreParent);
}

void
IndexedDBObjectStoreParent::SetObjectStore(IDBObjectStore* aObjectStore)
{
  // Sadly can't assert aObjectStore here...
  MOZ_ASSERT(!mObjectStore);

  mObjectStore = aObjectStore;
}

void
IndexedDBObjectStoreParent::ActorDestroy(ActorDestroyReason aWhy)
{
  if (mObjectStore) {
    mObjectStore->SetActor(static_cast<IndexedDBObjectStoreParent*>(NULL));
  }
}

bool
IndexedDBObjectStoreParent::RecvDeleteIndex(const nsString& aName)
{
  MOZ_NOT_REACHED("Should be overridden, don't call me!");
  return false;
}

bool
IndexedDBObjectStoreParent::RecvPIndexedDBRequestConstructor(
                                        PIndexedDBRequestParent* aActor,
                                        const ObjectStoreRequestParams& aParams)
{
  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mObjectStore) {
    return true;
  }

  IndexedDBObjectStoreRequestParent* actor =
    static_cast<IndexedDBObjectStoreRequestParent*>(aActor);

  if (mObjectStore->Transaction()->Database()->IsInvalidated()) {
    // If we've invalidated this database in the parent then we should bail out
    // now to avoid logic problems that could force-kill the child.
    return actor->Send__delete__(actor, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  switch (aParams.type()) {
    case ObjectStoreRequestParams::TGetParams:
      return actor->Get(aParams.get_GetParams());

    case ObjectStoreRequestParams::TGetAllParams:
      return actor->GetAll(aParams.get_GetAllParams());

    case ObjectStoreRequestParams::TAddParams:
      return actor->Add(aParams.get_AddParams());

    case ObjectStoreRequestParams::TPutParams:
      return actor->Put(aParams.get_PutParams());

    case ObjectStoreRequestParams::TDeleteParams:
      return actor->Delete(aParams.get_DeleteParams());

    case ObjectStoreRequestParams::TClearParams:
      return actor->Clear(aParams.get_ClearParams());

    case ObjectStoreRequestParams::TCountParams:
      return actor->Count(aParams.get_CountParams());

    case ObjectStoreRequestParams::TOpenCursorParams:
      return actor->OpenCursor(aParams.get_OpenCursorParams());

    default:
      MOZ_NOT_REACHED("Unknown type!");
      return false;
  }

  MOZ_NOT_REACHED("Should never get here!");
  return false;
}

bool
IndexedDBObjectStoreParent::RecvPIndexedDBIndexConstructor(
                                          PIndexedDBIndexParent* aActor,
                                          const IndexConstructorParams& aParams)
{
  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mObjectStore) {
    return true;
  }

  IndexedDBIndexParent* actor = static_cast<IndexedDBIndexParent*>(aActor);

  if (aParams.type() == IndexConstructorParams::TGetIndexParams) {
    const GetIndexParams& params = aParams.get_GetIndexParams();
    const nsString& name = params.name();

    nsRefPtr<IDBIndex> index;

    {
      AutoSetCurrentTransaction asct(mObjectStore->Transaction());

      nsresult rv = mObjectStore->IndexInternal(name, getter_AddRefs(index));
      NS_ENSURE_SUCCESS(rv, false);

      actor->SetIndex(index);
    }

    index->SetActor(actor);
    return true;
  }

  if (aParams.type() == IndexConstructorParams::TCreateIndexParams) {
    MOZ_NOT_REACHED("Should be overridden, don't call me!");
    return false;
  }

  MOZ_NOT_REACHED("Unknown param type!");
  return false;
}

PIndexedDBRequestParent*
IndexedDBObjectStoreParent::AllocPIndexedDBRequest(
                                        const ObjectStoreRequestParams& aParams)
{
  return new IndexedDBObjectStoreRequestParent(mObjectStore, aParams.type());
}

bool
IndexedDBObjectStoreParent::DeallocPIndexedDBRequest(
                                                PIndexedDBRequestParent* aActor)
{
  delete aActor;
  return true;
}

PIndexedDBIndexParent*
IndexedDBObjectStoreParent::AllocPIndexedDBIndex(
                                          const IndexConstructorParams& aParams)
{
  return new IndexedDBIndexParent();
}

bool
IndexedDBObjectStoreParent::DeallocPIndexedDBIndex(
                                                  PIndexedDBIndexParent* aActor)
{
  delete aActor;
  return true;
}

PIndexedDBCursorParent*
IndexedDBObjectStoreParent::AllocPIndexedDBCursor(
                              const ObjectStoreCursorConstructorParams& aParams)
{
  MOZ_NOT_REACHED("Caller is supposed to manually construct a cursor!");
  return NULL;
}

bool
IndexedDBObjectStoreParent::DeallocPIndexedDBCursor(
                                                 PIndexedDBCursorParent* aActor)
{
  delete aActor;
  return true;
}

/*******************************************************************************
 * IndexedDBVersionChangeObjectStoreParent
 ******************************************************************************/

IndexedDBVersionChangeObjectStoreParent::
  IndexedDBVersionChangeObjectStoreParent()
{
  MOZ_COUNT_CTOR(IndexedDBVersionChangeObjectStoreParent);
}

IndexedDBVersionChangeObjectStoreParent::
  ~IndexedDBVersionChangeObjectStoreParent()
{
  MOZ_COUNT_DTOR(IndexedDBVersionChangeObjectStoreParent);
}

bool
IndexedDBVersionChangeObjectStoreParent::RecvDeleteIndex(const nsString& aName)
{
  MOZ_ASSERT(!mObjectStore ||
             mObjectStore->Transaction()->GetMode() ==
             IDBTransaction::VERSION_CHANGE);

  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mObjectStore) {
    return true;
  }

  if (mObjectStore->Transaction()->Database()->IsInvalidated()) {
    // If we've invalidated this database in the parent then we should bail out
    // now to avoid logic problems that could force-kill the child.
    return true;
  }

  nsresult rv;

  {
    AutoSetCurrentTransaction asct(mObjectStore->Transaction());

    rv = mObjectStore->DeleteIndex(aName);
  }

  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

bool
IndexedDBVersionChangeObjectStoreParent::RecvPIndexedDBIndexConstructor(
                                          PIndexedDBIndexParent* aActor,
                                          const IndexConstructorParams& aParams)
{
  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mObjectStore) {
    return true;
  }

  if (mObjectStore->Transaction()->Database()->IsInvalidated()) {
    // If we've invalidated this database in the parent then we should bail out
    // now to avoid logic problems that could force-kill the child.
    return true;
  }

  IndexedDBIndexParent* actor = static_cast<IndexedDBIndexParent*>(aActor);

  if (aParams.type() == IndexConstructorParams::TCreateIndexParams) {
    MOZ_ASSERT(mObjectStore->Transaction()->GetMode() ==
               IDBTransaction::VERSION_CHANGE);

    const CreateIndexParams& params = aParams.get_CreateIndexParams();
    const IndexInfo& info = params.info();

    nsRefPtr<IDBIndex> index;

    nsresult rv;

    {
      AutoSetCurrentTransaction asct(mObjectStore->Transaction());

      rv = mObjectStore->CreateIndexInternal(info, getter_AddRefs(index));
    }

    NS_ENSURE_SUCCESS(rv, false);

    actor->SetIndex(index);
    index->SetActor(actor);
    return true;
  }

  return IndexedDBObjectStoreParent::RecvPIndexedDBIndexConstructor(aActor,
                                                                    aParams);
}

/*******************************************************************************
 * IndexedDBIndexParent
 ******************************************************************************/

IndexedDBIndexParent::IndexedDBIndexParent()
{
  MOZ_COUNT_CTOR(IndexedDBIndexParent);
}

IndexedDBIndexParent::~IndexedDBIndexParent()
{
  MOZ_COUNT_DTOR(IndexedDBIndexParent);
}

void
IndexedDBIndexParent::SetIndex(IDBIndex* aIndex)
{
  MOZ_ASSERT(aIndex);
  MOZ_ASSERT(!mIndex);

  mIndex = aIndex;
}

void
IndexedDBIndexParent::ActorDestroy(ActorDestroyReason aWhy)
{
  if (mIndex) {
    mIndex->SetActor(static_cast<IndexedDBIndexParent*>(NULL));
  }
}

bool
IndexedDBIndexParent::RecvPIndexedDBRequestConstructor(
                                              PIndexedDBRequestParent* aActor,
                                              const IndexRequestParams& aParams)
{
  if (IsDisconnected()) {
    // We're shutting down, ignore this request.
    return true;
  }

  if (!mIndex) {
    return true;
  }

  IndexedDBIndexRequestParent* actor =
    static_cast<IndexedDBIndexRequestParent*>(aActor);

  if (mIndex->ObjectStore()->Transaction()->Database()->IsInvalidated()) {
    // If we've invalidated this database in the parent then we should bail out
    // now to avoid logic problems that could force-kill the child.
    return actor->Send__delete__(actor, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  switch (aParams.type()) {
    case IndexRequestParams::TGetParams:
      return actor->Get(aParams.get_GetParams());

    case IndexRequestParams::TGetKeyParams:
      return actor->GetKey(aParams.get_GetKeyParams());

    case IndexRequestParams::TGetAllParams:
      return actor->GetAll(aParams.get_GetAllParams());

    case IndexRequestParams::TGetAllKeysParams:
      return actor->GetAllKeys(aParams.get_GetAllKeysParams());

    case IndexRequestParams::TCountParams:
      return actor->Count(aParams.get_CountParams());

    case IndexRequestParams::TOpenCursorParams:
      return actor->OpenCursor(aParams.get_OpenCursorParams());

    case IndexRequestParams::TOpenKeyCursorParams:
      return actor->OpenKeyCursor(aParams.get_OpenKeyCursorParams());

    default:
      MOZ_NOT_REACHED("Unknown type!");
      return false;
  }

  MOZ_NOT_REACHED("Should never get here!");
  return false;
}

PIndexedDBRequestParent*
IndexedDBIndexParent::AllocPIndexedDBRequest(const IndexRequestParams& aParams)
{
  return new IndexedDBIndexRequestParent(mIndex, aParams.type());
}

bool
IndexedDBIndexParent::DeallocPIndexedDBRequest(PIndexedDBRequestParent* aActor)
{
  delete aActor;
  return true;
}

PIndexedDBCursorParent*
IndexedDBIndexParent::AllocPIndexedDBCursor(
                                    const IndexCursorConstructorParams& aParams)
{
  MOZ_NOT_REACHED("Caller is supposed to manually construct a cursor!");
  return NULL;
}

bool
IndexedDBIndexParent::DeallocPIndexedDBCursor(PIndexedDBCursorParent* aActor)
{
  delete aActor;
  return true;
}

/*******************************************************************************
 * IndexedDBRequestParentBase
 ******************************************************************************/

IndexedDBRequestParentBase::IndexedDBRequestParentBase()
{
  MOZ_COUNT_CTOR(IndexedDBRequestParentBase);
}

IndexedDBRequestParentBase::~IndexedDBRequestParentBase()
{
  MOZ_COUNT_DTOR(IndexedDBRequestParentBase);
}

void
IndexedDBRequestParentBase::ActorDestroy(ActorDestroyReason aWhy)
{
  if (mRequest) {
    mRequest->SetActor(NULL);
  }
}

/*******************************************************************************
 * IndexedDBObjectStoreRequestParent
 ******************************************************************************/

IndexedDBObjectStoreRequestParent::IndexedDBObjectStoreRequestParent(
                                                   IDBObjectStore* aObjectStore,
                                                   RequestType aRequestType)
: mObjectStore(aObjectStore), mRequestType(aRequestType)
{
  MOZ_COUNT_CTOR(IndexedDBObjectStoreRequestParent);
  // Sadly can't assert aObjectStore here...
  MOZ_ASSERT(aRequestType > ParamsUnionType::T__None &&
             aRequestType <= ParamsUnionType::T__Last);
}

IndexedDBObjectStoreRequestParent::~IndexedDBObjectStoreRequestParent()
{
  MOZ_COUNT_DTOR(IndexedDBObjectStoreRequestParent);
}

void
IndexedDBObjectStoreRequestParent::ConvertBlobActors(
                                  const InfallibleTArray<PBlobParent*>& aActors,
                                  nsTArray<nsCOMPtr<nsIDOMBlob> >& aBlobs)
{
  MOZ_ASSERT(aBlobs.IsEmpty());
  MOZ_ASSERT(mObjectStore);

  if (!aActors.IsEmpty()) {
    // Walk the chain to get to ContentParent.
    MOZ_ASSERT(mObjectStore->Transaction()->Database()->GetContentParent());

    uint32_t length = aActors.Length();
    aBlobs.SetCapacity(length);

    for (uint32_t index = 0; index < length; index++) {
      BlobParent* actor = static_cast<BlobParent*>(aActors[index]);
      aBlobs.AppendElement(actor->GetBlob());
    }
  }
}

bool
IndexedDBObjectStoreRequestParent::IsDisconnected()
{
  MOZ_ASSERT(mObjectStore);
  MOZ_ASSERT(mObjectStore->GetActorParent());
  return mObjectStore->GetActorParent()->IsDisconnected();
}

bool
IndexedDBObjectStoreRequestParent::Get(const GetParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TGetParams);
  MOZ_ASSERT(mObjectStore);

  nsRefPtr<IDBRequest> request;

  nsRefPtr<IDBKeyRange> keyRange =
    IDBKeyRange::FromSerializedKeyRange(aParams.keyRange());
  MOZ_ASSERT(keyRange);

  {
    AutoSetCurrentTransaction asct(mObjectStore->Transaction());

    nsresult rv = mObjectStore->GetInternal(keyRange, nullptr,
                                            getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);

  return true;
}

bool
IndexedDBObjectStoreRequestParent::GetAll(const GetAllParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TGetAllParams);
  MOZ_ASSERT(mObjectStore);

  nsRefPtr<IDBRequest> request;

  const ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange keyRangeUnion =
    aParams.optionalKeyRange();

  nsRefPtr<IDBKeyRange> keyRange;

  switch (keyRangeUnion.type()) {
    case ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange::TKeyRange:
      keyRange =
        IDBKeyRange::FromSerializedKeyRange(keyRangeUnion.get_KeyRange());
      break;

    case ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange::Tvoid_t:
      break;

    default:
      MOZ_NOT_REACHED("Unknown param type!");
      return false;
  }

  {
    AutoSetCurrentTransaction asct(mObjectStore->Transaction());

    nsresult rv = mObjectStore->GetAllInternal(keyRange, aParams.limit(),
                                               nullptr,
                                               getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBObjectStoreRequestParent::Add(const AddParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TAddParams);
  MOZ_ASSERT(mObjectStore);

  ipc::AddPutParams params = aParams.commonParams();

  nsTArray<nsCOMPtr<nsIDOMBlob> > blobs;
  ConvertBlobActors(params.blobsParent(), blobs);

  nsRefPtr<IDBRequest> request;

  {
    AutoSetCurrentTransaction asct(mObjectStore->Transaction());

    nsresult rv =
      mObjectStore->AddOrPutInternal(params.cloneInfo(), params.key(),
                                     params.indexUpdateInfos(), blobs, false,
                                     getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBObjectStoreRequestParent::Put(const PutParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TPutParams);
  MOZ_ASSERT(mObjectStore);

  ipc::AddPutParams params = aParams.commonParams();

  nsTArray<nsCOMPtr<nsIDOMBlob> > blobs;
  ConvertBlobActors(params.blobsParent(), blobs);

  nsRefPtr<IDBRequest> request;

  {
    AutoSetCurrentTransaction asct(mObjectStore->Transaction());

    nsresult rv =
      mObjectStore->AddOrPutInternal(params.cloneInfo(), params.key(),
                                     params.indexUpdateInfos(), blobs, true,
                                     getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBObjectStoreRequestParent::Delete(const DeleteParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TDeleteParams);
  MOZ_ASSERT(mObjectStore);

  nsRefPtr<IDBRequest> request;

  nsRefPtr<IDBKeyRange> keyRange =
    IDBKeyRange::FromSerializedKeyRange(aParams.keyRange());
  MOZ_ASSERT(keyRange);

  {
    AutoSetCurrentTransaction asct(mObjectStore->Transaction());

    nsresult rv =
      mObjectStore->DeleteInternal(keyRange, nullptr, getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBObjectStoreRequestParent::Clear(const ClearParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TClearParams);
  MOZ_ASSERT(mObjectStore);

  nsRefPtr<IDBRequest> request;

  {
    AutoSetCurrentTransaction asct(mObjectStore->Transaction());

    nsresult rv = mObjectStore->ClearInternal(nullptr, getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBObjectStoreRequestParent::Count(const CountParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TCountParams);
  MOZ_ASSERT(mObjectStore);

  const ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange keyRangeUnion =
    aParams.optionalKeyRange();

  nsRefPtr<IDBKeyRange> keyRange;

  switch (keyRangeUnion.type()) {
    case ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange::TKeyRange:
      keyRange =
        IDBKeyRange::FromSerializedKeyRange(keyRangeUnion.get_KeyRange());
      break;

    case ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange::Tvoid_t:
      break;

    default:
      MOZ_NOT_REACHED("Unknown param type!");
      return false;
  }

  nsRefPtr<IDBRequest> request;

  {
    AutoSetCurrentTransaction asct(mObjectStore->Transaction());

    nsresult rv =
      mObjectStore->CountInternal(keyRange, nullptr, getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBObjectStoreRequestParent::OpenCursor(const OpenCursorParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TOpenCursorParams);
  MOZ_ASSERT(mObjectStore);

  const ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange keyRangeUnion =
    aParams.optionalKeyRange();

  nsRefPtr<IDBKeyRange> keyRange;

  switch (keyRangeUnion.type()) {
    case ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange::TKeyRange:
      keyRange =
        IDBKeyRange::FromSerializedKeyRange(keyRangeUnion.get_KeyRange());
      break;

    case ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange::Tvoid_t:
      break;

    default:
      MOZ_NOT_REACHED("Unknown param type!");
      return false;
  }

  size_t direction = static_cast<size_t>(aParams.direction());

  nsRefPtr<IDBRequest> request;

  {
    AutoSetCurrentTransaction asct(mObjectStore->Transaction());

    nsresult rv =
      mObjectStore->OpenCursorInternal(keyRange, direction, nullptr,
                                       getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

/*******************************************************************************
 * IndexedDBIndexRequestParent
 ******************************************************************************/

IndexedDBIndexRequestParent::IndexedDBIndexRequestParent(
                                                       IDBIndex* aIndex,
                                                       RequestType aRequestType)
: mIndex(aIndex), mRequestType(aRequestType)
{
  MOZ_COUNT_CTOR(IndexedDBIndexRequestParent);
  // Sadly can't assert aIndex here...
  MOZ_ASSERT(aRequestType > ParamsUnionType::T__None &&
             aRequestType <= ParamsUnionType::T__Last);
}

IndexedDBIndexRequestParent::~IndexedDBIndexRequestParent()
{
  MOZ_COUNT_DTOR(IndexedDBIndexRequestParent);
}

bool
IndexedDBIndexRequestParent::IsDisconnected()
{
  MOZ_ASSERT(mIndex);
  MOZ_ASSERT(mIndex->GetActorParent());
  return mIndex->GetActorParent()->IsDisconnected();
}

bool
IndexedDBIndexRequestParent::Get(const GetParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TGetParams);
  MOZ_ASSERT(mIndex);

  nsRefPtr<IDBRequest> request;

  nsRefPtr<IDBKeyRange> keyRange =
    IDBKeyRange::FromSerializedKeyRange(aParams.keyRange());
  MOZ_ASSERT(keyRange);

  {
    AutoSetCurrentTransaction asct(mIndex->ObjectStore()->Transaction());

    nsresult rv = mIndex->GetInternal(keyRange, nullptr,
                                      getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBIndexRequestParent::GetKey(const GetKeyParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TGetKeyParams);
  MOZ_ASSERT(mIndex);

  nsRefPtr<IDBRequest> request;

  nsRefPtr<IDBKeyRange> keyRange =
    IDBKeyRange::FromSerializedKeyRange(aParams.keyRange());
  MOZ_ASSERT(keyRange);

  {
    AutoSetCurrentTransaction asct(mIndex->ObjectStore()->Transaction());

    nsresult rv = mIndex->GetKeyInternal(keyRange, nullptr,
                                         getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBIndexRequestParent::GetAll(const GetAllParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TGetAllParams);
  MOZ_ASSERT(mIndex);

  nsRefPtr<IDBRequest> request;

  const ipc::FIXME_Bug_521898_index::OptionalKeyRange keyRangeUnion =
    aParams.optionalKeyRange();

  nsRefPtr<IDBKeyRange> keyRange;

  switch (keyRangeUnion.type()) {
    case ipc::FIXME_Bug_521898_index::OptionalKeyRange::TKeyRange:
      keyRange =
        IDBKeyRange::FromSerializedKeyRange(keyRangeUnion.get_KeyRange());
      break;

    case ipc::FIXME_Bug_521898_index::OptionalKeyRange::Tvoid_t:
      break;

    default:
      MOZ_NOT_REACHED("Unknown param type!");
      return false;
  }

  {
    AutoSetCurrentTransaction asct(mIndex->ObjectStore()->Transaction());

    nsresult rv = mIndex->GetAllInternal(keyRange, aParams.limit(), nullptr,
                                         getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBIndexRequestParent::GetAllKeys(const GetAllKeysParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TGetAllKeysParams);
  MOZ_ASSERT(mIndex);

  nsRefPtr<IDBRequest> request;

  const ipc::FIXME_Bug_521898_index::OptionalKeyRange keyRangeUnion =
    aParams.optionalKeyRange();

  nsRefPtr<IDBKeyRange> keyRange;

  switch (keyRangeUnion.type()) {
    case ipc::FIXME_Bug_521898_index::OptionalKeyRange::TKeyRange:
      keyRange =
        IDBKeyRange::FromSerializedKeyRange(keyRangeUnion.get_KeyRange());
      break;

    case ipc::FIXME_Bug_521898_index::OptionalKeyRange::Tvoid_t:
      break;

    default:
      MOZ_NOT_REACHED("Unknown param type!");
      return false;
  }

  {
    AutoSetCurrentTransaction asct(mIndex->ObjectStore()->Transaction());

    nsresult rv = mIndex->GetAllKeysInternal(keyRange, aParams.limit(), nullptr,
                                             getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBIndexRequestParent::Count(const CountParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TCountParams);
  MOZ_ASSERT(mIndex);

  const ipc::FIXME_Bug_521898_index::OptionalKeyRange keyRangeUnion =
    aParams.optionalKeyRange();

  nsRefPtr<IDBKeyRange> keyRange;

  switch (keyRangeUnion.type()) {
    case ipc::FIXME_Bug_521898_index::OptionalKeyRange::TKeyRange:
      keyRange =
        IDBKeyRange::FromSerializedKeyRange(keyRangeUnion.get_KeyRange());
      break;

    case ipc::FIXME_Bug_521898_index::OptionalKeyRange::Tvoid_t:
      break;

    default:
      MOZ_NOT_REACHED("Unknown param type!");
      return false;
  }

  nsRefPtr<IDBRequest> request;

  {
    AutoSetCurrentTransaction asct(mIndex->ObjectStore()->Transaction());

    nsresult rv = mIndex->CountInternal(keyRange, nullptr,
                                        getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBIndexRequestParent::OpenCursor(const OpenCursorParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TOpenCursorParams);
  MOZ_ASSERT(mIndex);

  const ipc::FIXME_Bug_521898_index::OptionalKeyRange keyRangeUnion =
    aParams.optionalKeyRange();

  nsRefPtr<IDBKeyRange> keyRange;

  switch (keyRangeUnion.type()) {
    case ipc::FIXME_Bug_521898_index::OptionalKeyRange::TKeyRange:
      keyRange =
        IDBKeyRange::FromSerializedKeyRange(keyRangeUnion.get_KeyRange());
      break;

    case ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange::Tvoid_t:
      break;

    default:
      MOZ_NOT_REACHED("Unknown param type!");
      return false;
  }

  size_t direction = static_cast<size_t>(aParams.direction());

  nsRefPtr<IDBRequest> request;

  {
    AutoSetCurrentTransaction asct(mIndex->ObjectStore()->Transaction());

    nsresult rv =
      mIndex->OpenCursorInternal(keyRange, direction, nullptr,
                                 getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

bool
IndexedDBIndexRequestParent::OpenKeyCursor(const OpenKeyCursorParams& aParams)
{
  MOZ_ASSERT(mRequestType == ParamsUnionType::TOpenKeyCursorParams);
  MOZ_ASSERT(mIndex);

  const ipc::FIXME_Bug_521898_index::OptionalKeyRange keyRangeUnion =
    aParams.optionalKeyRange();

  nsRefPtr<IDBKeyRange> keyRange;

  switch (keyRangeUnion.type()) {
    case ipc::FIXME_Bug_521898_index::OptionalKeyRange::TKeyRange:
      keyRange =
        IDBKeyRange::FromSerializedKeyRange(keyRangeUnion.get_KeyRange());
      break;

    case ipc::FIXME_Bug_521898_objectstore::OptionalKeyRange::Tvoid_t:
      break;

    default:
      MOZ_NOT_REACHED("Unknown param type!");
      return false;
  }

  size_t direction = static_cast<size_t>(aParams.direction());

  nsRefPtr<IDBRequest> request;

  {
    AutoSetCurrentTransaction asct(mIndex->ObjectStore()->Transaction());

    nsresult rv =
      mIndex->OpenKeyCursorInternal(keyRange, direction, nullptr,
                                    getter_AddRefs(request));
    NS_ENSURE_SUCCESS(rv, false);
  }

  request->SetActor(this);
  mRequest.swap(request);
  return true;
}

/*******************************************************************************
 * IndexedDBCursorRequestParent
 ******************************************************************************/

IndexedDBCursorRequestParent::IndexedDBCursorRequestParent(
                                                       IDBCursor* aCursor,
                                                       RequestType aRequestType)
: mCursor(aCursor), mRequestType(aRequestType)
{
  MOZ_COUNT_CTOR(IndexedDBCursorRequestParent);
  MOZ_ASSERT(aCursor);
  MOZ_ASSERT(aRequestType > ParamsUnionType::T__None &&
             aRequestType <= ParamsUnionType::T__Last);
}

IndexedDBCursorRequestParent::~IndexedDBCursorRequestParent()
{
  MOZ_COUNT_DTOR(IndexedDBCursorRequestParent);
}

bool
IndexedDBCursorRequestParent::IsDisconnected()
{
  MOZ_ASSERT(mCursor);
  MOZ_ASSERT(mCursor->GetActorParent());
  return mCursor->GetActorParent()->IsDisconnected();
}

bool
IndexedDBCursorRequestParent::Continue(const ContinueParams& aParams)
{
  MOZ_ASSERT(mCursor);
  MOZ_ASSERT(mRequestType == ParamsUnionType::TContinueParams);

  {
    AutoSetCurrentTransaction asct(mCursor->Transaction());

    nsresult rv = mCursor->ContinueInternal(aParams.key(), aParams.count());
    NS_ENSURE_SUCCESS(rv, false);
  }

  mRequest = mCursor->Request();
  MOZ_ASSERT(mRequest);

  mRequest->SetActor(this);
  return true;
}

/*******************************************************************************
 * IndexedDBDeleteDatabaseRequestParent
 ******************************************************************************/

IndexedDBDeleteDatabaseRequestParent::IndexedDBDeleteDatabaseRequestParent(
                                                           IDBFactory* aFactory)
: mEventListener(ALLOW_THIS_IN_INITIALIZER_LIST(this)), mFactory(aFactory)
{
  MOZ_COUNT_CTOR(IndexedDBDeleteDatabaseRequestParent);
  MOZ_ASSERT(aFactory);
}

IndexedDBDeleteDatabaseRequestParent::~IndexedDBDeleteDatabaseRequestParent()
{
  MOZ_COUNT_DTOR(IndexedDBDeleteDatabaseRequestParent);
}

nsresult
IndexedDBDeleteDatabaseRequestParent::HandleEvent(nsIDOMEvent* aEvent)
{
  MOZ_ASSERT(aEvent);

  if (IsDisconnected()) {
    // We're shutting down, ignore this event.
    return NS_OK;
  }

  nsString type;
  nsresult rv = aEvent->GetType(type);
  NS_ENSURE_SUCCESS(rv, rv);

  if (type.EqualsASCII(BLOCKED_EVT_STR)) {
    nsCOMPtr<nsIIDBVersionChangeEvent> event = do_QueryInterface(aEvent);
    MOZ_ASSERT(event);

    uint64_t currentVersion;
    rv = event->GetOldVersion(&currentVersion);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!SendBlocked(currentVersion)) {
      return NS_ERROR_FAILURE;
    }

    return NS_OK;
  }

#ifdef DEBUG
  if (type.EqualsASCII(SUCCESS_EVT_STR)) {
    MOZ_ASSERT(NS_SUCCEEDED(mOpenRequest->GetErrorCode()));
  }
  else {
    MOZ_ASSERT(type.EqualsASCII(ERROR_EVT_STR));
    MOZ_ASSERT(NS_FAILED(mOpenRequest->GetErrorCode()));
  }
#endif

  if (!Send__delete__(this, mOpenRequest->GetErrorCode())) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult
IndexedDBDeleteDatabaseRequestParent::SetOpenRequest(
                                                 IDBOpenDBRequest* aOpenRequest)
{
  MOZ_ASSERT(aOpenRequest);
  MOZ_ASSERT(!mOpenRequest);

  nsIDOMEventTarget* target = static_cast<nsIDOMEventTarget*>(aOpenRequest);

  nsresult rv = target->AddEventListener(NS_LITERAL_STRING(SUCCESS_EVT_STR),
                                         mEventListener, false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = target->AddEventListener(NS_LITERAL_STRING(ERROR_EVT_STR),
                                mEventListener, false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = target->AddEventListener(NS_LITERAL_STRING(BLOCKED_EVT_STR),
                                mEventListener, false);
  NS_ENSURE_SUCCESS(rv, rv);

  mOpenRequest = aOpenRequest;
  return NS_OK;
}

/*******************************************************************************
 * WeakEventListener
 ******************************************************************************/

 NS_IMPL_ISUPPORTS1(WeakEventListenerBase, nsIDOMEventListener)

 NS_IMETHODIMP
 WeakEventListenerBase::HandleEvent(nsIDOMEvent* aEvent)
{
  MOZ_NOT_REACHED("This must be overridden!");
  return NS_ERROR_FAILURE;
}
