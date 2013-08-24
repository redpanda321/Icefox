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
 * The Original Code is Oracle Corporation code.
 *
 * The Initial Developer of the Original Code is
 *  Oracle Corporation
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir.vukicevic@oracle.com>
 *   Brett Wilson <brettw@gmail.com>
 *   Shawn Wilsher <me@shawnwilsher.com>
 *   Drew Willcoxon <adw@mozilla.com>
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

#include "mozStorageService.h"
#include "mozStorageConnection.h"
#include "prinit.h"
#include "nsAutoPtr.h"
#include "nsCollationCID.h"
#include "nsEmbedCID.h"
#include "nsThreadUtils.h"
#include "mozStoragePrivateHelpers.h"
#include "nsILocale.h"
#include "nsILocaleService.h"
#include "nsIXPConnect.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"

#include "sqlite3.h"

#include "nsIPromptService.h"
#include "nsIMemoryReporter.h"

#include "mozilla/FunctionTimer.h"

namespace mozilla {
namespace storage {

////////////////////////////////////////////////////////////////////////////////
//// Memory Reporting

static PRInt64
GetStorageSQLitePageCacheMemoryUsed(void *)
{
  int current, high;
  int rc = ::sqlite3_status(SQLITE_STATUS_PAGECACHE_OVERFLOW, &current, &high,
                            0);
  return rc == SQLITE_OK ? current : 0;
}

static PRInt64
GetStorageSQLiteOtherMemoryUsed(void *)
{
  int pageCacheCurrent, pageCacheHigh;
  int rc = ::sqlite3_status(SQLITE_STATUS_PAGECACHE_OVERFLOW, &pageCacheCurrent,
                            &pageCacheHigh, 0);
  return rc == SQLITE_OK ? ::sqlite3_memory_used() - pageCacheCurrent : 0;
}

NS_MEMORY_REPORTER_IMPLEMENT(StorageSQLitePageCacheMemoryUsed,
                             "storage/sqlite/pagecache",
                             "Memory in use by SQLite for the page cache",
                             GetStorageSQLitePageCacheMemoryUsed,
                             nsnull)

NS_MEMORY_REPORTER_IMPLEMENT(StorageSQLiteOtherMemoryUsed,
                             "storage/sqlite/other",
                             "Memory in use by SQLite for other various reasons",
                             GetStorageSQLiteOtherMemoryUsed,
                             nsnull)

////////////////////////////////////////////////////////////////////////////////
//// Helpers

class ServiceMainThreadInitializer : public nsRunnable
{
public:
  ServiceMainThreadInitializer(nsIObserver *aObserver,
                               nsIXPConnect **aXPConnectPtr)
  : mObserver(aObserver)
  , mXPConnectPtr(aXPConnectPtr)
  {
  }

  NS_IMETHOD Run()
  {
    NS_PRECONDITION(NS_IsMainThread(), "Must be running on the main thread!");

    // NOTE:  All code that can only run on the main thread and needs to be run
    //        during initialization should be placed here.  During the off-
    //        chance that storage is initialized on a background thread, this
    //        will ensure everything that isn't threadsafe is initialized in
    //        the right place.

    // Register for xpcom-shutdown so we can cleanup after ourselves.  The
    // observer service can only be used on the main thread.
    nsCOMPtr<nsIObserverService> os =
      mozilla::services::GetObserverService();
    NS_ENSURE_TRUE(os, NS_ERROR_FAILURE);
    nsresult rv = os->AddObserver(mObserver, "xpcom-shutdown", PR_FALSE);
    NS_ENSURE_SUCCESS(rv, rv);

    // We cache XPConnect for our language helpers.  XPConnect can only be
    // used on the main thread.
    (void)CallGetService(nsIXPConnect::GetCID(), mXPConnectPtr);

    // Register our SQLite memory reporters.  Registration can only happen on
    // the main thread (otherwise you'll get cryptic crashes).
    NS_RegisterMemoryReporter(new NS_MEMORY_REPORTER_NAME(StorageSQLitePageCacheMemoryUsed));
    NS_RegisterMemoryReporter(new NS_MEMORY_REPORTER_NAME(StorageSQLiteOtherMemoryUsed));

    return NS_OK;
  }

private:
  nsIObserver *mObserver;
  nsIXPConnect **mXPConnectPtr;
};

////////////////////////////////////////////////////////////////////////////////
//// Service

NS_IMPL_THREADSAFE_ISUPPORTS2(
  Service,
  mozIStorageService,
  nsIObserver
)

Service *Service::gService = nsnull;

Service *
Service::getSingleton()
{
  if (gService) {
    NS_ADDREF(gService);
    return gService;
  }

  // Ensure that we are using the same version of SQLite that we compiled with
  // or newer.  Our configure check ensures we are using a new enough version
  // at compile time.
  if (SQLITE_VERSION_NUMBER > ::sqlite3_libversion_number()) {
    nsCOMPtr<nsIPromptService> ps(do_GetService(NS_PROMPTSERVICE_CONTRACTID));
    if (ps) {
      nsAutoString title, message;
      title.AppendASCII("SQLite Version Error");
      message.AppendASCII("The application has been updated, but your version "
                          "of SQLite is too old and the application cannot "
                          "run.");
      (void)ps->Alert(nsnull, title.get(), message.get());
    }
    ::PR_Abort();
  }

  gService = new Service();
  if (gService) {
    NS_ADDREF(gService);
    if (NS_FAILED(gService->initialize()))
      NS_RELEASE(gService);
  }

  return gService;
}

nsIXPConnect *Service::sXPConnect = nsnull;

already_AddRefed<nsIXPConnect>
Service::getXPConnect()
{
  NS_PRECONDITION(NS_IsMainThread(),
                  "Must only get XPConnect on the main thread!");
  NS_PRECONDITION(gService,
                  "Can not get XPConnect without an instance of our service!");

  // If we've been shutdown, sXPConnect will be null.  To prevent leaks, we do
  // not cache the service after this point.
  nsCOMPtr<nsIXPConnect> xpc(sXPConnect);
  if (!xpc)
    xpc = do_GetService(nsIXPConnect::GetCID());
  NS_ASSERTION(xpc, "Could not get XPConnect!");
  return xpc.forget();
}

Service::Service()
: mMutex("Service::mMutex")
{
}

Service::~Service()
{
  // Shutdown the sqlite3 API.  Warn if shutdown did not turn out okay, but
  // there is nothing actionable we can do in that case.
  int rc = ::sqlite3_shutdown();
  if (rc != SQLITE_OK)
    NS_WARNING("sqlite3 did not shutdown cleanly.");

  bool shutdownObserved = !sXPConnect;
  NS_ASSERTION(shutdownObserved, "Shutdown was not observed!");

  gService = nsnull;
}

void
Service::shutdown()
{
  NS_IF_RELEASE(sXPConnect);
}

nsresult
Service::initialize()
{
  NS_TIME_FUNCTION;

  int rc;

  // Explicitly initialize sqlite3.  Although this is implicitly called by
  // various sqlite3 functions (and the sqlite3_open calls in our case),
  // the documentation suggests calling this directly.  So we do.
  rc = ::sqlite3_initialize();
  if (rc != SQLITE_OK)
    return convertResultCode(rc);

  // Run the things that need to run on the main thread there.
  nsCOMPtr<nsIRunnable> event =
    new ServiceMainThreadInitializer(this, &sXPConnect);
  if (event && ::NS_IsMainThread()) {
    (void)event->Run();
  }
  else {
    (void)::NS_DispatchToMainThread(event);
  }

  return NS_OK;
}

int
Service::localeCompareStrings(const nsAString &aStr1,
                              const nsAString &aStr2,
                              PRInt32 aComparisonStrength)
{
  // The implementation of nsICollation.CompareString() is platform-dependent.
  // On Linux it's not thread-safe.  It may not be on Windows and OS X either,
  // but it's more difficult to tell.  We therefore synchronize this method.
  MutexAutoLock mutex(mMutex);

  nsICollation *coll = getLocaleCollation();
  if (!coll) {
    NS_ERROR("Storage service has no collation");
    return 0;
  }

  PRInt32 res;
  nsresult rv = coll->CompareString(aComparisonStrength, aStr1, aStr2, &res);
  if (NS_FAILED(rv)) {
    NS_ERROR("Collation compare string failed");
    return 0;
  }

  return res;
}

nsICollation *
Service::getLocaleCollation()
{
  mMutex.AssertCurrentThreadOwns();

  if (mLocaleCollation)
    return mLocaleCollation;

  nsCOMPtr<nsILocaleService> svc(do_GetService(NS_LOCALESERVICE_CONTRACTID));
  if (!svc) {
    NS_WARNING("Could not get locale service");
    return nsnull;
  }

  nsCOMPtr<nsILocale> appLocale;
  nsresult rv = svc->GetApplicationLocale(getter_AddRefs(appLocale));
  if (NS_FAILED(rv)) {
    NS_WARNING("Could not get application locale");
    return nsnull;
  }

  nsCOMPtr<nsICollationFactory> collFact =
    do_CreateInstance(NS_COLLATIONFACTORY_CONTRACTID);
  if (!collFact) {
    NS_WARNING("Could not create collation factory");
    return nsnull;
  }

  rv = collFact->CreateCollation(appLocale, getter_AddRefs(mLocaleCollation));
  if (NS_FAILED(rv)) {
    NS_WARNING("Could not create collation");
    return nsnull;
  }

  return mLocaleCollation;
}

////////////////////////////////////////////////////////////////////////////////
//// mozIStorageService

#ifndef NS_APP_STORAGE_50_FILE
#define NS_APP_STORAGE_50_FILE "UStor"
#endif

NS_IMETHODIMP
Service::OpenSpecialDatabase(const char *aStorageKey,
                             mozIStorageConnection **_connection)
{
  nsresult rv;

  nsCOMPtr<nsIFile> storageFile;
  if (::strcmp(aStorageKey, "memory") == 0) {
    // just fall through with NULL storageFile, this will cause the storage
    // connection to use a memory DB.
  }
  else if (::strcmp(aStorageKey, "profile") == 0) {

    rv = NS_GetSpecialDirectory(NS_APP_STORAGE_50_FILE,
                                getter_AddRefs(storageFile));
    NS_ENSURE_SUCCESS(rv, rv);

    nsString filename;
    storageFile->GetPath(filename);
    nsCString filename8 = NS_ConvertUTF16toUTF8(filename.get());
    // fall through to DB initialization
  }
  else {
    return NS_ERROR_INVALID_ARG;
  }

  Connection *msc = new Connection(this, SQLITE_OPEN_READWRITE);
  NS_ENSURE_TRUE(msc, NS_ERROR_OUT_OF_MEMORY);

  rv = msc->initialize(storageFile);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_connection = msc);
  return NS_OK;
}

NS_IMETHODIMP
Service::OpenDatabase(nsIFile *aDatabaseFile,
                      mozIStorageConnection **_connection)
{
  NS_ENSURE_ARG(aDatabaseFile);

#ifdef NS_FUNCTION_TIMER
  nsCString leafname;
  (void)aDatabaseFile->GetNativeLeafName(leafname);
  NS_TIME_FUNCTION_FMT("mozIStorageService::OpenDatabase(%s)", leafname.get());
#endif

  // Always ensure that SQLITE_OPEN_CREATE is passed in for compatibility
  // reasons.
  int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_SHAREDCACHE |
              SQLITE_OPEN_CREATE;
  nsRefPtr<Connection> msc = new Connection(this, flags);
  NS_ENSURE_TRUE(msc, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = msc->initialize(aDatabaseFile);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_connection = msc);
  return NS_OK;
}

NS_IMETHODIMP
Service::OpenUnsharedDatabase(nsIFile *aDatabaseFile,
                              mozIStorageConnection **_connection)
{
#ifdef NS_FUNCTION_TIMER
  nsCString leafname;
  (void)aDatabaseFile->GetNativeLeafName(leafname);
  NS_TIME_FUNCTION_FMT("mozIStorageService::OpenUnsharedDatabase(%s)",
                       leafname.get());
#endif

  // Always ensure that SQLITE_OPEN_CREATE is passed in for compatibility
  // reasons.
  int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_PRIVATECACHE |
              SQLITE_OPEN_CREATE;
  nsRefPtr<Connection> msc = new Connection(this, flags);
  NS_ENSURE_TRUE(msc, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = msc->initialize(aDatabaseFile);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_connection = msc);
  return NS_OK;
}

NS_IMETHODIMP
Service::BackupDatabaseFile(nsIFile *aDBFile,
                            const nsAString &aBackupFileName,
                            nsIFile *aBackupParentDirectory,
                            nsIFile **backup)
{
  nsresult rv;
  nsCOMPtr<nsIFile> parentDir = aBackupParentDirectory;
  if (!parentDir) {
    // This argument is optional, and defaults to the same parent directory
    // as the current file.
    rv = aDBFile->GetParent(getter_AddRefs(parentDir));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsIFile> backupDB;
  rv = parentDir->Clone(getter_AddRefs(backupDB));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = backupDB->Append(aBackupFileName);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = backupDB->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString fileName;
  rv = backupDB->GetLeafName(fileName);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = backupDB->Remove(PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  backupDB.forget(backup);

  return aDBFile->CopyTo(parentDir, fileName);
}

////////////////////////////////////////////////////////////////////////////////
//// nsIObserver

NS_IMETHODIMP
Service::Observe(nsISupports *, const char *aTopic, const PRUnichar *)
{
  if (strcmp(aTopic, "xpcom-shutdown") == 0)
    shutdown();
  return NS_OK;
}

} // namespace storage
} // namespace mozilla
