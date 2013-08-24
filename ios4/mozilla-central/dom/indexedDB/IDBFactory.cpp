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

#include "IDBFactory.h"

#include "nsIIDBDatabaseException.h"
#include "nsILocalFile.h"
#include "nsIScriptContext.h"

#include "mozilla/storage.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsDirectoryServiceUtils.h"
#include "nsDOMClassInfo.h"
#include "nsHashKeys.h"
#include "nsPIDOMWindow.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsXPCOMCID.h"

#include "AsyncConnectionHelper.h"
#include "DatabaseInfo.h"
#include "IDBDatabase.h"
#include "IDBKeyRange.h"
#include "LazyIdleThread.h"

#define DB_SCHEMA_VERSION 3

USING_INDEXEDDB_NAMESPACE

namespace {

const PRUint32 kDefaultThreadTimeoutMS = 30000;

struct ObjectStoreInfoMap
{
  ObjectStoreInfoMap()
  : id(LL_MININT), info(nsnull) { }

  PRInt64 id;
  ObjectStoreInfo* info;
};

class OpenDatabaseHelper : public AsyncConnectionHelper
{
public:
  OpenDatabaseHelper(IDBRequest* aRequest,
                     const nsAString& aName,
                     const nsAString& aDescription,
                     const nsACString& aASCIIOrigin,
                     LazyIdleThread* aThread)
  : AsyncConnectionHelper(static_cast<IDBDatabase*>(nsnull), aRequest),
    mName(aName), mDescription(aDescription), mASCIIOrigin(aASCIIOrigin),
    mThread(aThread), mDatabaseId(0)
  { }

  PRUint16 DoDatabaseWork(mozIStorageConnection* aConnection);
  PRUint16 GetSuccessResult(nsIWritableVariant* aResult);

private:
  PRUint16 DoDatabaseWorkInternal(mozIStorageConnection* aConnection);

  // In-params.
  nsString mName;
  nsString mDescription;
  nsCString mASCIIOrigin;
  nsRefPtr<LazyIdleThread> mThread;

  // Out-params.
  nsTArray<nsAutoPtr<ObjectStoreInfo> > mObjectStores;
  nsString mVersion;

  nsCOMPtr<mozIStorageConnection> mConnection;
  nsString mDatabaseFilePath;
  PRUint32 mDatabaseId;
};

nsresult
CreateTables(mozIStorageConnection* aDBConn)
{
  NS_PRECONDITION(!NS_IsMainThread(),
                  "Creating tables on the main thread!");
  NS_PRECONDITION(aDBConn, "Passing a null database connection!");

  // Table `database`
  nsresult rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE TABLE database ("
      "name TEXT NOT NULL, "
      "description TEXT NOT NULL, "
      "version TEXT DEFAULT NULL"
    ");"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  // Table `object_store`
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE TABLE object_store ("
      "id INTEGER, "
      "name TEXT NOT NULL, "
      "key_path TEXT NOT NULL, "
      "auto_increment INTEGER NOT NULL DEFAULT 0, "
      "PRIMARY KEY (id), "
      "UNIQUE (name)"
    ");"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  // Table `object_data`
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE TABLE object_data ("
      "id INTEGER, "
      "object_store_id INTEGER NOT NULL, "
      "data TEXT NOT NULL, "
      "key_value DEFAULT NULL, " // NONE affinity
      "PRIMARY KEY (id), "
      "FOREIGN KEY (object_store_id) REFERENCES object_store(id) ON DELETE "
        "CASCADE"
    ");"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE UNIQUE INDEX key_index "
    "ON object_data (key_value, object_store_id);"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  // Table `ai_object_data`
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE TABLE ai_object_data ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
      "object_store_id INTEGER NOT NULL, "
      "data TEXT NOT NULL, "
      "FOREIGN KEY (object_store_id) REFERENCES object_store(id) ON DELETE "
        "CASCADE"
    ");"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE UNIQUE INDEX ai_key_index "
    "ON ai_object_data (id, object_store_id);"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  // Table `index`
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE TABLE object_store_index ("
      "id INTEGER, "
      "object_store_id INTEGER NOT NULL, "
      "name TEXT NOT NULL, "
      "key_path TEXT NOT NULL, "
      "unique_index INTEGER NOT NULL, "
      "object_store_autoincrement INTERGER NOT NULL, "
      "PRIMARY KEY (id), "
      "UNIQUE (object_store_id, name), "
      "FOREIGN KEY (object_store_id) REFERENCES object_store(id) ON DELETE "
        "CASCADE"
    ");"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  // Table `index_data`
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE TABLE index_data ("
      "id INTEGER, "
      "index_id INTEGER NOT NULL, "
      "object_data_id INTEGER NOT NULL, "
      "object_data_key NOT NULL, " // NONE affinity
      "value NOT NULL, "
      "PRIMARY KEY (id), "
      "FOREIGN KEY (index_id) REFERENCES object_store_index(id) ON DELETE "
        "CASCADE, "
      "FOREIGN KEY (object_data_id) REFERENCES object_data(id) ON DELETE "
        "CASCADE"
    ");"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE INDEX value_index "
    "ON index_data (index_id, value);"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  // Table `unique_index_data`
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE TABLE unique_index_data ("
      "id INTEGER, "
      "index_id INTEGER NOT NULL, "
      "object_data_id INTEGER NOT NULL, "
      "object_data_key NOT NULL, " // NONE affinity
      "value NOT NULL, "
      "PRIMARY KEY (id), "
      "UNIQUE (index_id, value), "
      "FOREIGN KEY (index_id) REFERENCES object_store_index(id) ON DELETE "
        "CASCADE "
      "FOREIGN KEY (object_data_id) REFERENCES object_data(id) ON DELETE "
        "CASCADE"
    ");"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  // Table `ai_index_data`
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE TABLE ai_index_data ("
      "id INTEGER, "
      "index_id INTEGER NOT NULL, "
      "ai_object_data_id INTEGER NOT NULL, "
      "value NOT NULL, "
      "PRIMARY KEY (id), "
      "FOREIGN KEY (index_id) REFERENCES object_store_index(id) ON DELETE "
        "CASCADE, "
      "FOREIGN KEY (ai_object_data_id) REFERENCES ai_object_data(id) ON DELETE "
        "CASCADE"
    ");"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE INDEX ai_value_index "
    "ON ai_index_data (index_id, value);"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  // Table `ai_unique_index_data`
  rv = aDBConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "CREATE TABLE ai_unique_index_data ("
      "id INTEGER, "
      "index_id INTEGER NOT NULL, "
      "ai_object_data_id INTEGER NOT NULL, "
      "value NOT NULL, "
      "PRIMARY KEY (id), "
      "UNIQUE (index_id, value), "
      "FOREIGN KEY (index_id) REFERENCES object_store_index(id) ON DELETE "
        "CASCADE, "
      "FOREIGN KEY (ai_object_data_id) REFERENCES ai_object_data(id) ON DELETE "
        "CASCADE"
    ");"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aDBConn->SetSchemaVersion(DB_SCHEMA_VERSION);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
CreateMetaData(mozIStorageConnection* aConnection,
               const nsAString& aName,
               const nsAString& aDescription)
{
  NS_PRECONDITION(!NS_IsMainThread(), "Wrong thread!");
  NS_PRECONDITION(aConnection, "Null database!");

  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = aConnection->CreateStatement(NS_LITERAL_CSTRING(
    "INSERT OR REPLACE INTO database (name, description) "
    "VALUES (:name, :description)"
  ), getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = stmt->BindStringByName(NS_LITERAL_CSTRING("name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindStringByName(NS_LITERAL_CSTRING("description"), aDescription);
  NS_ENSURE_SUCCESS(rv, rv);

  return stmt->Execute();
}

nsresult
CreateDatabaseConnection(const nsACString& aASCIIOrigin,
                         const nsAString& aName,
                         const nsAString& aDescription,
                         nsAString& aDatabaseFilePath,
                         mozIStorageConnection** aConnection)
{
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");
  NS_ASSERTION(!aASCIIOrigin.IsEmpty() && !aName.IsEmpty(), "Bad arguments!");

  aDatabaseFilePath.Truncate();

  nsCOMPtr<nsIFile> dbFile;
  nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                       getter_AddRefs(dbFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dbFile->Append(NS_LITERAL_STRING("indexedDB"));
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool exists;
  rv = dbFile->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (exists) {
    PRBool isDirectory;
    rv = dbFile->IsDirectory(&isDirectory);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(isDirectory, NS_ERROR_UNEXPECTED);
  }
  else {
    rv = dbFile->Create(nsIFile::DIRECTORY_TYPE, 0755);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsAutoString filename;
  filename.AppendInt(HashString(aASCIIOrigin));

  rv = dbFile->Append(filename);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dbFile->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (exists) {
    PRBool isDirectory;
    rv = dbFile->IsDirectory(&isDirectory);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(isDirectory, NS_ERROR_UNEXPECTED);
  }
  else {
    rv = dbFile->Create(nsIFile::DIRECTORY_TYPE, 0755);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  filename.Truncate();
  filename.AppendInt(HashString(aName));
  filename.AppendLiteral(".sqlite");

  rv = dbFile->Append(filename);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dbFile->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<mozIStorageService> ss =
    do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(ss, NS_ERROR_FAILURE);

  nsCOMPtr<mozIStorageConnection> connection;
  rv = ss->OpenDatabase(dbFile, getter_AddRefs(connection));
  if (rv == NS_ERROR_FILE_CORRUPTED) {
    // Nuke the database file.  The web services can recreate their data.
    rv = dbFile->Remove(PR_FALSE);
    NS_ENSURE_SUCCESS(rv, rv);

    exists = PR_FALSE;

    rv = ss->OpenDatabase(dbFile, getter_AddRefs(connection));
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // Check to make sure that the database schema is correct.
  PRInt32 schemaVersion;
  rv = connection->GetSchemaVersion(&schemaVersion);
  NS_ENSURE_SUCCESS(rv, rv);

  if (schemaVersion != DB_SCHEMA_VERSION) {
    if (exists) {
      // If the connection is not at the right schema version, nuke it.
      rv = dbFile->Remove(PR_FALSE);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = ss->OpenDatabase(dbFile, getter_AddRefs(connection));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    rv = CreateTables(connection);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = CreateMetaData(connection, aName, aDescription);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Check to make sure that the database schema is correct again.
  NS_ASSERTION(NS_SUCCEEDED(connection->GetSchemaVersion(&schemaVersion)) &&
               schemaVersion == DB_SCHEMA_VERSION,
               "CreateTables failed!");

  // Turn on foreign key constraints in debug builds to catch bugs!
  rv = connection->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "PRAGMA foreign_keys = ON;"
  ));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dbFile->GetPath(aDatabaseFilePath);
  NS_ENSURE_SUCCESS(rv, rv);

  connection.forget(aConnection);
  return NS_OK;
}

inline
nsresult
ValidateVariantForKey(nsIVariant* aVariant)
{
  PRUint16 type;
  nsresult rv = aVariant->GetDataType(&type);
  NS_ENSURE_SUCCESS(rv, rv);

  switch (type) {
    case nsIDataType::VTYPE_WSTRING_SIZE_IS:
    case nsIDataType::VTYPE_INT32:
    case nsIDataType::VTYPE_DOUBLE:
      break;

    default:
      return NS_ERROR_INVALID_ARG;
  }

  return NS_OK;
}

} // anonyomous namespace

// static
already_AddRefed<nsIIDBFactory>
IDBFactory::Create()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  nsCOMPtr<nsIIDBFactory> request(new IDBFactory());
  return request.forget();
}

// static
already_AddRefed<mozIStorageConnection>
IDBFactory::GetConnection(const nsAString& aDatabaseFilePath)
{
  NS_ASSERTION(StringEndsWith(aDatabaseFilePath, NS_LITERAL_STRING(".sqlite")),
               "Bad file path!");

  nsCOMPtr<nsILocalFile> dbFile(do_CreateInstance(NS_LOCAL_FILE_CONTRACTID));
  NS_ENSURE_TRUE(dbFile, nsnull);

  nsresult rv = dbFile->InitWithPath(aDatabaseFilePath);
  NS_ENSURE_SUCCESS(rv, nsnull);

  PRBool exists;
  rv = dbFile->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, nsnull);
  NS_ENSURE_TRUE(exists, nsnull);

  nsCOMPtr<mozIStorageService> ss =
    do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(ss, nsnull);

  nsCOMPtr<mozIStorageConnection> connection;
  rv = ss->OpenDatabase(dbFile, getter_AddRefs(connection));
  NS_ENSURE_SUCCESS(rv, nsnull);

#ifdef DEBUG
  {
    // Check to make sure that the database schema is correct again.
    PRInt32 schemaVersion;
    NS_ASSERTION(NS_SUCCEEDED(connection->GetSchemaVersion(&schemaVersion)) &&
                 schemaVersion == DB_SCHEMA_VERSION,
                 "Wrong schema!");
  }
#endif

  // Turn on foreign key constraints in debug builds to catch bugs!
  rv = connection->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "PRAGMA foreign_keys = ON;"
  ));
  NS_ENSURE_SUCCESS(rv, nsnull);

  return connection.forget();
}

NS_IMPL_ADDREF(IDBFactory)
NS_IMPL_RELEASE(IDBFactory)

NS_INTERFACE_MAP_BEGIN(IDBFactory)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, IDBRequest::Generator)
  NS_INTERFACE_MAP_ENTRY(nsIIDBFactory)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(IDBFactory)
NS_INTERFACE_MAP_END

DOMCI_DATA(IDBFactory, IDBFactory)

NS_IMETHODIMP
IDBFactory::Open(const nsAString& aName,
                 const nsAString& aDescription,
                 JSContext* aCx,
                 nsIIDBRequest** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  if (aName.IsEmpty()) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIPrincipal> principal;
  nsresult rv = nsContentUtils::GetSecurityManager()->
    GetSubjectPrincipal(getter_AddRefs(principal));
  NS_ENSURE_SUCCESS(rv, nsnull);

  nsCString origin;
  if (nsContentUtils::IsSystemPrincipal(principal)) {
    origin.AssignLiteral("chrome");
  }
  else {
    rv = nsContentUtils::GetASCIIOrigin(principal, origin);
    NS_ENSURE_SUCCESS(rv, nsnull);
  }

  nsIScriptContext* context = GetScriptContextFromJSContext(aCx);
  NS_ENSURE_STATE(context);

  nsCOMPtr<nsPIDOMWindow> innerWindow;

  nsCOMPtr<nsPIDOMWindow> window =
    do_QueryInterface(context->GetGlobalObject());
  if (window) {
    innerWindow = window->GetCurrentInnerWindow();
  }
  NS_ENSURE_STATE(innerWindow);

  nsRefPtr<IDBRequest> request = GenerateRequest(context, innerWindow);
  NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

  nsRefPtr<LazyIdleThread> thread(new LazyIdleThread(kDefaultThreadTimeoutMS,
                                                     nsnull));

  nsRefPtr<OpenDatabaseHelper> runnable =
    new OpenDatabaseHelper(request, aName, aDescription, origin, thread);

  rv = runnable->Dispatch(thread);
  NS_ENSURE_SUCCESS(rv, rv);

  request.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBFactory::MakeSingleKeyRange(nsIVariant* aValue,
                               nsIIDBKeyRange** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsresult rv = ValidateVariantForKey(aValue);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsRefPtr<IDBKeyRange> range =
    IDBKeyRange::Create(aValue, aValue, PRUint16(nsIIDBKeyRange::LEFT_BOUND |
                                                 nsIIDBKeyRange::RIGHT_BOUND));
  NS_ASSERTION(range, "Out of memory?");

  range.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBFactory::MakeLeftBoundKeyRange(nsIVariant* aBound,
                                  PRBool aOpen,
                                  nsIIDBKeyRange** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsresult rv = ValidateVariantForKey(aBound);
  if (NS_FAILED(rv)) {
    return rv;
  }

  PRUint16 flags = aOpen ?
                   PRUint16(nsIIDBKeyRange::LEFT_OPEN) :
                   PRUint16(nsIIDBKeyRange::LEFT_BOUND);

  nsRefPtr<IDBKeyRange> range = IDBKeyRange::Create(aBound, nsnull, flags);
  NS_ASSERTION(range, "Out of memory?");

  range.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBFactory::MakeRightBoundKeyRange(nsIVariant* aBound,
                                   PRBool aOpen,
                                   nsIIDBKeyRange** _retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsresult rv = ValidateVariantForKey(aBound);
  if (NS_FAILED(rv)) {
    return rv;
  }

  PRUint16 flags = aOpen ?
                   PRUint16(nsIIDBKeyRange::RIGHT_OPEN) :
                   PRUint16(nsIIDBKeyRange::RIGHT_BOUND);

  nsRefPtr<IDBKeyRange> range = IDBKeyRange::Create(nsnull, aBound, flags);
  NS_ASSERTION(range, "Out of memory?");

  range.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
IDBFactory::MakeBoundKeyRange(nsIVariant* aLeft,
                              nsIVariant* aRight,
                              PRBool aOpenLeft,
                              PRBool aOpenRight,
                              nsIIDBKeyRange **_retval)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");

  nsresult rv = ValidateVariantForKey(aLeft);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ValidateVariantForKey(aRight);
  if (NS_FAILED(rv)) {
    return rv;
  }

  PRUint16 flags = aOpenLeft ?
                   PRUint16(nsIIDBKeyRange::LEFT_OPEN) :
                   PRUint16(nsIIDBKeyRange::LEFT_BOUND);

  flags |= aOpenRight ?
           PRUint16(nsIIDBKeyRange::RIGHT_OPEN) :
           PRUint16(nsIIDBKeyRange::RIGHT_BOUND);

  nsRefPtr<IDBKeyRange> range = IDBKeyRange::Create(aLeft, aRight, flags);
  NS_ASSERTION(range, "Out of memory?");

  range.forget(_retval);
  return NS_OK;
}

PRUint16
OpenDatabaseHelper::DoDatabaseWork(mozIStorageConnection* aConnection)
{
  PRUint16 result = DoDatabaseWorkInternal(aConnection);
  if (result != OK) {
    mConnection = nsnull;
  }
  return result;
}

PRUint16
OpenDatabaseHelper::DoDatabaseWorkInternal(mozIStorageConnection* aConnection)
{
#ifdef DEBUG
  {
    PRBool correctThread;
    NS_ASSERTION(NS_SUCCEEDED(mThread->IsOnCurrentThread(&correctThread)) &&
                 correctThread,
                 "Running on the wrong thread!");
  }
#endif
  NS_ASSERTION(!aConnection, "Huh?!");

  nsresult rv = CreateDatabaseConnection(mASCIIOrigin, mName, mDescription,
                                         mDatabaseFilePath,
                                         getter_AddRefs(mConnection));
  NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);

  mDatabaseId = HashString(mDatabaseFilePath);
  NS_ASSERTION(mDatabaseId, "HashString gave us 0?!");

  nsAutoTArray<ObjectStoreInfoMap, 20> infoMap;

  { // Load object store names and ids.
    nsCOMPtr<mozIStorageStatement> stmt;
    rv = mConnection->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT name, id, key_path, auto_increment "
      "FROM object_store"
    ), getter_AddRefs(stmt));
    NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);

    PRBool hasResult;
    while (NS_SUCCEEDED((rv = stmt->ExecuteStep(&hasResult))) && hasResult) {
      nsAutoPtr<ObjectStoreInfo>* element =
        mObjectStores.AppendElement(new ObjectStoreInfo());
      NS_ENSURE_TRUE(element, nsIIDBDatabaseException::UNKNOWN_ERR);

      ObjectStoreInfo* const info = element->get();

      rv = stmt->GetString(0, info->name);
      NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);

      info->id = stmt->AsInt64(1);

      rv = stmt->GetString(2, info->keyPath);
      NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);

      info->autoIncrement = !!stmt->AsInt32(3);
      info->databaseId = mDatabaseId;

      ObjectStoreInfoMap* mapEntry = infoMap.AppendElement();
      if (!mapEntry) {
        NS_ERROR("Failed to add to map!");
        return nsIIDBDatabaseException::UNKNOWN_ERR;
      }
      mapEntry->id = info->id;
      mapEntry->info = info;
    }
    NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);
  }

  { // Load index information
    nsCOMPtr<mozIStorageStatement> stmt;
    rv = mConnection->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT object_store_id, id, name, key_path, unique_index, "
             "object_store_autoincrement "
      "FROM object_store_index"
    ), getter_AddRefs(stmt));
    NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);

    PRBool hasResult;
    while (NS_SUCCEEDED((rv = stmt->ExecuteStep(&hasResult))) && hasResult) {

      PRInt64 objectStoreId = stmt->AsInt64(0);

      ObjectStoreInfo* objectStoreInfo = nsnull;
      PRUint32 count = infoMap.Length();
      for (PRUint32 index = 0; index < count; index++) {
        if (infoMap[index].id == objectStoreId) {
          objectStoreInfo = infoMap[index].info;
          break;
        }
      }
      NS_ENSURE_TRUE(objectStoreInfo, nsIIDBDatabaseException::UNKNOWN_ERR);

      IndexInfo* indexInfo = objectStoreInfo->indexes.AppendElement();
      NS_ENSURE_TRUE(indexInfo, nsIIDBDatabaseException::UNKNOWN_ERR);

      indexInfo->id = stmt->AsInt64(1);

      rv = stmt->GetString(2, indexInfo->name);
      NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);

      rv = stmt->GetString(3, indexInfo->keyPath);
      NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);

      indexInfo->unique = !!stmt->AsInt32(4);
      indexInfo->autoIncrement = !!stmt->AsInt32(5);
    }
    NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);
  }

  { // Load version information.
    nsCOMPtr<mozIStorageStatement> stmt;
    rv = mConnection->CreateStatement(NS_LITERAL_CSTRING(
      "SELECT version "
      "FROM database"
    ), getter_AddRefs(stmt));
    NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);

    PRBool hasResult;
    rv = stmt->ExecuteStep(&hasResult);
    NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);
    NS_ENSURE_TRUE(hasResult, nsIIDBDatabaseException::UNKNOWN_ERR);

    rv = stmt->GetString(0, mVersion);
    NS_ENSURE_SUCCESS(rv, nsIIDBDatabaseException::UNKNOWN_ERR);
    if (mVersion.IsVoid()) {
      mVersion.Assign(EmptyString());
    }
  }

  return OK;
}

PRUint16
OpenDatabaseHelper::GetSuccessResult(nsIWritableVariant* aResult)
{
  NS_ASSERTION(mConnection, "Should have a connection!");

  DatabaseInfo* dbInfo;
  if (DatabaseInfo::Get(mDatabaseId, &dbInfo)) {
    NS_ASSERTION(dbInfo->referenceCount, "Bad reference count!");
    ++dbInfo->referenceCount;

#ifdef DEBUG
    {
      NS_ASSERTION(dbInfo->name == mName &&
                   dbInfo->description == mDescription &&
                   dbInfo->version == mVersion &&
                   dbInfo->id == mDatabaseId &&
                   dbInfo->filePath == mDatabaseFilePath,
                   "Metadata mismatch!");

      PRUint32 objectStoreCount = mObjectStores.Length();
      for (PRUint32 index = 0; index < objectStoreCount; index++) {
        nsAutoPtr<ObjectStoreInfo>& info = mObjectStores[index];
        NS_ASSERTION(info->databaseId == mDatabaseId, "Huh?!");

        ObjectStoreInfo* otherInfo;
        NS_ASSERTION(ObjectStoreInfo::Get(mDatabaseId, info->name, &otherInfo),
                     "ObjectStore not known!");

        NS_ASSERTION(info->name == otherInfo->name &&
                     info->id == otherInfo->id &&
                     info->keyPath == otherInfo->keyPath &&
                     info->autoIncrement == otherInfo->autoIncrement &&
                     info->databaseId == otherInfo->databaseId,
                     "Metadata mismatch!");
        NS_ASSERTION(dbInfo->ContainsStoreName(info->name),
                     "Object store names out of date!");
        NS_ASSERTION(info->indexes.Length() == otherInfo->indexes.Length(),
                     "Bad index length!");

        PRUint32 indexCount = info->indexes.Length();
        for (PRUint32 indexIndex = 0; indexIndex < indexCount; indexIndex++) {
          const IndexInfo& indexInfo = info->indexes[indexIndex];
          const IndexInfo& otherIndexInfo = otherInfo->indexes[indexIndex];
          NS_ASSERTION(indexInfo.id == otherIndexInfo.id,
                       "Bad index id!");
          NS_ASSERTION(indexInfo.name == otherIndexInfo.name,
                       "Bad index name!");
          NS_ASSERTION(indexInfo.keyPath == otherIndexInfo.keyPath,
                       "Bad index keyPath!");
          NS_ASSERTION(indexInfo.unique == otherIndexInfo.unique,
                       "Bad index unique value!");
          NS_ASSERTION(indexInfo.autoIncrement == otherIndexInfo.autoIncrement,
                       "Bad index autoIncrement value!");
        }
      }
    }
#endif

  }
  else {
    nsAutoPtr<DatabaseInfo> newInfo(new DatabaseInfo());

    newInfo->name = mName;
    newInfo->description = mDescription;
    newInfo->version = mVersion;
    newInfo->id = mDatabaseId;
    newInfo->filePath = mDatabaseFilePath;
    newInfo->referenceCount = 1;

    if (!DatabaseInfo::Put(newInfo)) {
      NS_ERROR("Failed to add to hash!");
      return nsIIDBDatabaseException::UNKNOWN_ERR;
    }

    dbInfo = newInfo.forget();

    PRUint32 objectStoreCount = mObjectStores.Length();
    for (PRUint32 index = 0; index < objectStoreCount; index++) {
      nsAutoPtr<ObjectStoreInfo>& info = mObjectStores[index];
      NS_ASSERTION(info->databaseId == mDatabaseId, "Huh?!");
  
      if (!ObjectStoreInfo::Put(info)) {
        NS_ERROR("Failed to add to hash!");
        return nsIIDBDatabaseException::UNKNOWN_ERR;
      }

      info.forget();
    }
  }

  nsRefPtr<IDBDatabase> db = IDBDatabase::Create(dbInfo, mThread, mConnection);
  NS_ASSERTION(db, "This can't fail!");

  NS_ASSERTION(!mConnection, "Should have swapped out!");

  aResult->SetAsISupports(static_cast<IDBRequest::Generator*>(db));
  return OK;
}
