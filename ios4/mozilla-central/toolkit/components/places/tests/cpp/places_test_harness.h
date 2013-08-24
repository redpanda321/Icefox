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
 * The Original Code is places test code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
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

#include "TestHarness.h"
#include "nsMemory.h"
#include "nsThreadUtils.h"
#include "nsNetUtil.h"
#include "nsDocShellCID.h"

#include "nsToolkitCompsCID.h"
#include "nsINavHistoryService.h"
#include "nsIObserverService.h"
#include "mozilla/IHistory.h"
#include "mozIStorageConnection.h"
#include "mozIStorageStatement.h"
#include "nsPIPlacesDatabase.h"

using namespace mozilla;

static size_t gTotalTests = 0;
static size_t gPassedTests = 0;

#define do_check_true(aCondition) \
  PR_BEGIN_MACRO \
    gTotalTests++; \
    if (aCondition) { \
      gPassedTests++; \
    } else { \
      fail("Expected true, got false at %s:%d!", __FILE__, __LINE__); \
    } \
  PR_END_MACRO

#define do_check_false(aCondition) \
  PR_BEGIN_MACRO \
    gTotalTests++; \
    if (!aCondition) { \
      gPassedTests++; \
    } else { \
      fail("Expected false, got true at %s:%d!", __FILE__, __LINE__); \
    } \
  PR_END_MACRO

#define do_check_success(aResult) \
  do_check_true(NS_SUCCEEDED(aResult))

#define do_check_eq(aFirst, aSecond) \
  do_check_true(aFirst == aSecond)

struct Test
{
  void (*func)(void);
  const char* const name;
};
#define TEST(aName) \
  {aName, #aName}

/**
 * Runs the next text.
 */
void run_next_test();

/**
 * To be used around asynchronous work.
 */
void do_test_pending();
void do_test_finished();

/**
 * Adds a URI to the database.
 *
 * @param aURI
 *        The URI to add to the database.
 */
void
addURI(nsIURI* aURI)
{
  nsCOMPtr<nsINavHistoryService> hist =
    do_GetService(NS_NAVHISTORYSERVICE_CONTRACTID);

  PRInt64 id;
  nsresult rv = hist->AddVisit(aURI, PR_Now(), nsnull,
                               nsINavHistoryService::TRANSITION_LINK, PR_FALSE,
                               0, &id);
  do_check_success(rv);
}

struct PlaceRecord
{
  PRInt64 id;
  PRInt32 hidden;
  PRInt32 typed;
  PRInt32 visitCount;
};

struct VisitRecord
{
  PRInt64 id;
  PRInt64 lastVisitId;
  PRInt32 transitionType;
};

already_AddRefed<IHistory>
do_get_IHistory()
{
  nsCOMPtr<IHistory> history = do_GetService(NS_IHISTORY_CONTRACTID);
  do_check_true(history);
  return history.forget();
}

already_AddRefed<nsINavHistoryService>
do_get_NavHistory()
{
  nsCOMPtr<nsINavHistoryService> serv =
    do_GetService(NS_NAVHISTORYSERVICE_CONTRACTID);
  do_check_true(serv);
  return serv.forget();
}

already_AddRefed<mozIStorageConnection>
do_get_db()
{
  nsCOMPtr<nsINavHistoryService> history = do_get_NavHistory();
  nsCOMPtr<nsPIPlacesDatabase> database = do_QueryInterface(history);
  do_check_true(database);

  mozIStorageConnection* dbConn;
  nsresult rv = database->GetDBConnection(&dbConn);
  do_check_success(rv);
  return dbConn;
}

/**
 * Get the place record from the database.
 *
 * @param aURI The unique URI of the place we are looking up
 * @param result Out parameter where the result is stored
 */
void
do_get_place(nsIURI* aURI, PlaceRecord& result)
{
  nsCOMPtr<mozIStorageConnection> dbConn = do_get_db();
  nsCOMPtr<mozIStorageStatement> stmt;

  nsCString spec;
  nsresult rv = aURI->GetSpec(spec);
  do_check_success(rv);

  rv = dbConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT id, hidden, typed, visit_count FROM moz_places_temp "
    "WHERE url=?1 "
    "UNION ALL "
    "SELECT id, hidden, typed, visit_count FROM moz_places "
    "WHERE url=?1 "
    "LIMIT 1"
  ), getter_AddRefs(stmt));
  do_check_success(rv);

  rv = stmt->BindUTF8StringParameter(0, spec);
  do_check_success(rv);

  PRBool hasResults;
  rv = stmt->ExecuteStep(&hasResults);
  do_check_true(hasResults);
  do_check_success(rv);

  rv = stmt->GetInt64(0, &result.id);
  do_check_success(rv);
  rv = stmt->GetInt32(1, &result.hidden);
  do_check_success(rv);
  rv = stmt->GetInt32(2, &result.typed);
  do_check_success(rv);
  rv = stmt->GetInt32(3, &result.visitCount);
  do_check_success(rv);
}

/**
 * Gets the most recent visit to a place.
 *
 * @param placeID ID from the moz_places table
 * @param result Out parameter where visit is stored
 */
void
do_get_lastVisit(PRInt64 placeId, VisitRecord& result)
{
  nsCOMPtr<mozIStorageConnection> dbConn = do_get_db();
  nsCOMPtr<mozIStorageStatement> stmt;

  nsresult rv = dbConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT id, from_visit, visit_type FROM moz_historyvisits_temp "
    "WHERE place_id=?1 "
    "UNION ALL "
    "SELECT id, from_visit, visit_type FROM moz_historyvisits "
    "WHERE place_id=?1 "
    "LIMIT 1"
  ), getter_AddRefs(stmt));
  do_check_success(rv);

  rv = stmt->BindInt64Parameter(0, placeId);
  do_check_success(rv);

  PRBool hasResults;
  rv = stmt->ExecuteStep(&hasResults);
  do_check_true(hasResults);
  do_check_success(rv);

  rv = stmt->GetInt64(0, &result.id);
  do_check_success(rv);
  rv = stmt->GetInt64(1, &result.lastVisitId);
  do_check_success(rv);
  rv = stmt->GetInt32(2, &result.transitionType);
  do_check_success(rv);
}
