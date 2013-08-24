/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "nsXULAppAPI.h"

#include "History.h"
#include "nsNavHistory.h"
#include "nsNavBookmarks.h"
#include "nsAnnotationService.h"
#include "Helpers.h"
#include "PlaceInfo.h"
#include "VisitInfo.h"

#include "mozilla/storage.h"
#include "mozilla/dom/Link.h"
#include "nsDocShellCID.h"
#include "mozilla/Services.h"
#include "nsThreadUtils.h"
#include "nsNetUtil.h"
#include "nsIXPConnect.h"
#include "mozilla/unused.h"
#include "mozilla/Util.h"
#include "nsContentUtils.h"
#include "nsIMemoryReporter.h"
#include "mozilla/Attributes.h"

// Initial size for the cache holding visited status observers.
#define VISIT_OBSERVERS_INITIAL_CACHE_SIZE 128

using namespace mozilla::dom;
using mozilla::unused;

namespace mozilla {
namespace places {

////////////////////////////////////////////////////////////////////////////////
//// Global Defines

#define URI_VISITED "visited"
#define URI_NOT_VISITED "not visited"
#define URI_VISITED_RESOLUTION_TOPIC "visited-status-resolution"
// Observer event fired after a visit has been registered in the DB.
#define URI_VISIT_SAVED "uri-visit-saved"

#define DESTINATIONFILEURI_ANNO \
        NS_LITERAL_CSTRING("downloads/destinationFileURI")
#define DESTINATIONFILENAME_ANNO \
        NS_LITERAL_CSTRING("downloads/destinationFileName")

////////////////////////////////////////////////////////////////////////////////
//// VisitData

struct VisitData {
  VisitData()
  : placeId(0)
  , visitId(0)
  , sessionId(0)
  , hidden(true)
  , typed(false)
  , transitionType(PR_UINT32_MAX)
  , visitTime(0)
  , frecency(-1)
  , titleChanged(false)
  {
    guid.SetIsVoid(true);
    title.SetIsVoid(true);
  }

  VisitData(nsIURI* aURI,
            nsIURI* aReferrer = NULL)
  : placeId(0)
  , visitId(0)
  , sessionId(0)
  , hidden(true)
  , typed(false)
  , transitionType(PR_UINT32_MAX)
  , visitTime(0)
  , frecency(-1)
  , titleChanged(false)
  {
    (void)aURI->GetSpec(spec);
    (void)GetReversedHostname(aURI, revHost);
    if (aReferrer) {
      (void)aReferrer->GetSpec(referrerSpec);
    }
    guid.SetIsVoid(true);
    title.SetIsVoid(true);
  }

  /**
   * Sets the transition type of the visit, as well as if it was typed.
   *
   * @param aTransitionType
   *        The transition type constant to set.  Must be one of the
   *        TRANSITION_ constants on nsINavHistoryService.
   */
  void SetTransitionType(PRUint32 aTransitionType)
  {
    typed = aTransitionType == nsINavHistoryService::TRANSITION_TYPED;
    transitionType = aTransitionType;
  }

  /**
   * Determines if this refers to the same url as aOther, and updates aOther
   * with missing information if so.
   *
   * @param aOther
   *        The other place to check against.
   * @return true if this is a visit for the same place as aOther, false
   *         otherwise.
   */
  bool IsSamePlaceAs(VisitData& aOther)
  {
    if (!spec.Equals(aOther.spec)) {
      return false;
    }

    aOther.placeId = placeId;
    aOther.guid = guid;
    return true;
  }

  PRInt64 placeId;
  nsCString guid;
  PRInt64 visitId;
  PRInt64 sessionId;
  nsCString spec;
  nsString revHost;
  bool hidden;
  bool typed;
  PRUint32 transitionType;
  PRTime visitTime;
  PRInt32 frecency;

  /**
   * Stores the title.  If this is empty (IsEmpty() returns true), then the
   * title should be removed from the Place.  If the title is void (IsVoid()
   * returns true), then no title has been set on this object, and titleChanged
   * should remain false.
   */
  nsString title;

  nsCString referrerSpec;

  // TODO bug 626836 hook up hidden and typed change tracking too!
  bool titleChanged;
};

////////////////////////////////////////////////////////////////////////////////
//// Anonymous Helpers

namespace {

/**
 * Obtains an nsIURI from the "uri" property of a JSObject.
 *
 * @param aCtx
 *        The JSContext for aObject.
 * @param aObject
 *        The JSObject to get the URI from.
 * @param aProperty
 *        The name of the property to get the URI from.
 * @return the URI if it exists.
 */
already_AddRefed<nsIURI>
GetURIFromJSObject(JSContext* aCtx,
                   JSObject* aObject,
                   const char* aProperty)
{
  jsval uriVal;
  JSBool rc = JS_GetProperty(aCtx, aObject, aProperty, &uriVal);
  NS_ENSURE_TRUE(rc, nsnull);

  if (!JSVAL_IS_PRIMITIVE(uriVal)) {
    nsCOMPtr<nsIXPConnect> xpc = mozilla::services::GetXPConnect();

    nsCOMPtr<nsIXPConnectWrappedNative> wrappedObj;
    nsresult rv = xpc->GetWrappedNativeOfJSObject(aCtx, JSVAL_TO_OBJECT(uriVal),
                                                  getter_AddRefs(wrappedObj));
    NS_ENSURE_SUCCESS(rv, nsnull);
    nsCOMPtr<nsIURI> uri = do_QueryWrappedNative(wrappedObj);
    return uri.forget();
  }
  return nsnull;
}

/**
 * Obtains the specified property of a JSObject.
 *
 * @param aCtx
 *        The JSContext for aObject.
 * @param aObject
 *        The JSObject to get the string from.
 * @param aProperty
 *        The property to get the value from.
 * @param _string
 *        The string to populate with the value, or set it to void.
 */
void
GetStringFromJSObject(JSContext* aCtx,
                      JSObject* aObject,
                      const char* aProperty,
                      nsString& _string)
{
  jsval val;
  JSBool rc = JS_GetProperty(aCtx, aObject, aProperty, &val);
  if (!rc || JSVAL_IS_VOID(val) ||
      !(JSVAL_IS_NULL(val) || JSVAL_IS_STRING(val))) {
    _string.SetIsVoid(true);
    return;
  }
  // |null| in JS maps to the empty string.
  if (JSVAL_IS_NULL(val)) {
    _string.Truncate();
    return;
  }
  size_t length;
  const jschar* chars =
    JS_GetStringCharsZAndLength(aCtx, JSVAL_TO_STRING(val), &length);
  if (!chars) {
    _string.SetIsVoid(true);
    return;
  }
  _string.Assign(static_cast<const PRUnichar*>(chars), length);
}

/**
 * Obtains the specified property of a JSObject.
 *
 * @param aCtx
 *        The JSContext for aObject.
 * @param aObject
 *        The JSObject to get the int from.
 * @param aProperty
 *        The property to get the value from.
 * @param _int
 *        The integer to populate with the value on success.
 */
template <typename IntType>
nsresult
GetIntFromJSObject(JSContext* aCtx,
                   JSObject* aObject,
                   const char* aProperty,
                   IntType* _int)
{
  jsval value;
  JSBool rc = JS_GetProperty(aCtx, aObject, aProperty, &value);
  NS_ENSURE_TRUE(rc, NS_ERROR_UNEXPECTED);
  if (JSVAL_IS_VOID(value)) {
    return NS_ERROR_INVALID_ARG;
  }
  NS_ENSURE_ARG(JSVAL_IS_PRIMITIVE(value));
  NS_ENSURE_ARG(JSVAL_IS_NUMBER(value));

  double num;
  rc = JS_ValueToNumber(aCtx, value, &num);
  NS_ENSURE_TRUE(rc, NS_ERROR_UNEXPECTED);
  NS_ENSURE_ARG(IntType(num) == num);

  *_int = IntType(num);
  return NS_OK;
}

/**
 * Obtains the specified property of a JSObject.
 *
 * @pre aArray must be an Array object.
 *
 * @param aCtx
 *        The JSContext for aArray.
 * @param aArray
 *        The JSObject to get the object from.
 * @param aIndex
 *        The index to get the object from.
 * @param _object
 *        The JSObject pointer on success.
 */
nsresult
GetJSObjectFromArray(JSContext* aCtx,
                     JSObject* aArray,
                     uint32_t aIndex,
                     JSObject** _rooter)
{
  NS_PRECONDITION(JS_IsArrayObject(aCtx, aArray),
                  "Must provide an object that is an array!");

  jsval value;
  JSBool rc = JS_GetElement(aCtx, aArray, aIndex, &value);
  NS_ENSURE_TRUE(rc, NS_ERROR_UNEXPECTED);
  NS_ENSURE_ARG(!JSVAL_IS_PRIMITIVE(value));
  *_rooter = JSVAL_TO_OBJECT(value);
  return NS_OK;
}

class VisitedQuery : public AsyncStatementCallback
{
public:
  static nsresult Start(nsIURI* aURI,
                        mozIVisitedStatusCallback* aCallback=nsnull)
  {
    NS_PRECONDITION(aURI, "Null URI");

  // If we are a content process, always remote the request to the
  // parent process.
  if (XRE_GetProcessType() == GeckoProcessType_Content) {
    mozilla::dom::ContentChild* cpc =
      mozilla::dom::ContentChild::GetSingleton();
    NS_ASSERTION(cpc, "Content Protocol is NULL!");
    (void)cpc->SendStartVisitedQuery(aURI);
    return NS_OK;
  }

    nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
    NS_ENSURE_STATE(navHistory);
    if (navHistory->hasEmbedVisit(aURI)) {
      nsRefPtr<VisitedQuery> callback = new VisitedQuery(aURI, aCallback, true);
      NS_ENSURE_TRUE(callback, NS_ERROR_OUT_OF_MEMORY);
      // As per IHistory contract, we must notify asynchronously.
      nsCOMPtr<nsIRunnable> event =
        NS_NewRunnableMethod(callback, &VisitedQuery::NotifyVisitedStatus);
      NS_DispatchToMainThread(event);

      return NS_OK;
    }

    History* history = History::GetService();
    NS_ENSURE_STATE(history);
    mozIStorageAsyncStatement* stmt = history->GetIsVisitedStatement();
    NS_ENSURE_STATE(stmt);

    // Bind by index for performance.
    nsresult rv = URIBinder::Bind(stmt, 0, aURI);
    NS_ENSURE_SUCCESS(rv, rv);

    nsRefPtr<VisitedQuery> callback = new VisitedQuery(aURI, aCallback);
    NS_ENSURE_TRUE(callback, NS_ERROR_OUT_OF_MEMORY);

    nsCOMPtr<mozIStoragePendingStatement> handle;
    return stmt->ExecuteAsync(callback, getter_AddRefs(handle));
  }

  NS_IMETHOD HandleResult(mozIStorageResultSet* aResults)
  {
    // If this method is called, we've gotten results, which means we have a
    // visit.
    mIsVisited = true;
    return NS_OK;
  }

  NS_IMETHOD HandleError(mozIStorageError* aError)
  {
    // mIsVisited is already set to false, and that's the assumption we will
    // make if an error occurred.
    return NS_OK;
  }

  NS_IMETHOD HandleCompletion(PRUint16 aReason)
  {
    if (aReason != mozIStorageStatementCallback::REASON_FINISHED) {
      return NS_OK;
    }

    nsresult rv = NotifyVisitedStatus();
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  nsresult NotifyVisitedStatus()
  {
    // If an external handling callback is provided, just notify through it.
    if (mCallback) {
      mCallback->IsVisited(mURI, mIsVisited);
      return NS_OK;
    }

    if (mIsVisited) {
      History* history = History::GetService();
      NS_ENSURE_STATE(history);
      history->NotifyVisited(mURI);
    }

    nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
    if (observerService) {
      nsAutoString status;
      if (mIsVisited) {
        status.AssignLiteral(URI_VISITED);
      }
      else {
        status.AssignLiteral(URI_NOT_VISITED);
      }
      (void)observerService->NotifyObservers(mURI,
                                             URI_VISITED_RESOLUTION_TOPIC,
                                             status.get());
    }

    return NS_OK;
  }

private:
  VisitedQuery(nsIURI* aURI,
               mozIVisitedStatusCallback *aCallback=nsnull,
               bool aIsVisited=false)
  : mURI(aURI)
  , mCallback(aCallback)
  , mIsVisited(aIsVisited)
  {
  }

  nsCOMPtr<nsIURI> mURI;
  nsCOMPtr<mozIVisitedStatusCallback> mCallback;
  bool mIsVisited;
};

/**
 * Notifies observers about a visit.
 */
class NotifyVisitObservers : public nsRunnable
{
public:
  NotifyVisitObservers(VisitData& aPlace,
                       VisitData& aReferrer)
  : mPlace(aPlace)
  , mReferrer(aReferrer)
  , mHistory(History::GetService())
  {
  }

  NS_IMETHOD Run()
  {
    NS_PRECONDITION(NS_IsMainThread(),
                    "This should be called on the main thread");
    // We are in the main thread, no need to lock.
    if (mHistory->IsShuttingDown()) {
      // If we are shutting down, we cannot notify the observers.
      return NS_OK;
    }

    nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
    if (!navHistory) {
      NS_WARNING("Trying to notify about a visit but cannot get the history service!");
      return NS_OK;
    }

    nsCOMPtr<nsIURI> uri;
    (void)NS_NewURI(getter_AddRefs(uri), mPlace.spec);

    // Notify nsNavHistory observers of visit, but only for certain types of
    // visits to maintain consistency with nsNavHistory::GetQueryResults.
    if (!mPlace.hidden &&
        mPlace.transitionType != nsINavHistoryService::TRANSITION_EMBED &&
        mPlace.transitionType != nsINavHistoryService::TRANSITION_FRAMED_LINK) {
      navHistory->NotifyOnVisit(uri, mPlace.visitId, mPlace.visitTime,
                                mPlace.sessionId, mReferrer.visitId,
                                mPlace.transitionType, mPlace.guid);
    }

    nsCOMPtr<nsIObserverService> obsService =
      mozilla::services::GetObserverService();
    if (obsService) {
      DebugOnly<nsresult> rv =
        obsService->NotifyObservers(uri, URI_VISIT_SAVED, nsnull);
      NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Could not notify observers");
    }

    History* history = History::GetService();
    NS_ENSURE_STATE(history);
    history->AppendToRecentlyVisitedURIs(uri);
    history->NotifyVisited(uri);

    return NS_OK;
  }
private:
  VisitData mPlace;
  VisitData mReferrer;
  nsRefPtr<History> mHistory;
};

/**
 * Notifies observers about a pages title changing.
 */
class NotifyTitleObservers : public nsRunnable
{
public:
  /**
   * Notifies observers on the main thread.
   *
   * @param aSpec
   *        The spec of the URI to notify about.
   * @param aTitle
   *        The new title to notify about.
   */
  NotifyTitleObservers(const nsCString& aSpec,
                       const nsString& aTitle,
                       const nsCString& aGUID)
  : mSpec(aSpec)
  , mTitle(aTitle)
  , mGUID(aGUID)
  {
  }

  NS_IMETHOD Run()
  {
    NS_PRECONDITION(NS_IsMainThread(),
                    "This should be called on the main thread");

    nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
    NS_ENSURE_TRUE(navHistory, NS_ERROR_OUT_OF_MEMORY);
    nsCOMPtr<nsIURI> uri;
    (void)NS_NewURI(getter_AddRefs(uri), mSpec);
    navHistory->NotifyTitleChange(uri, mTitle, mGUID);

    return NS_OK;
  }
private:
  const nsCString mSpec;
  const nsString mTitle;
  const nsCString mGUID;
};

/**
 * Notifies a callback object when a visit has been handled.
 */
class NotifyVisitInfoCallback : public nsRunnable
{
public:
  NotifyVisitInfoCallback(mozIVisitInfoCallback* aCallback,
                          const VisitData& aPlace,
                          nsresult aResult)
  : mCallback(aCallback)
  , mPlace(aPlace)
  , mResult(aResult)
  {
    NS_PRECONDITION(aCallback, "Must pass a non-null callback!");
  }

  NS_IMETHOD Run()
  {
    NS_PRECONDITION(NS_IsMainThread(),
                    "This should be called on the main thread");

    nsCOMPtr<nsIURI> referrerURI;
    if (!mPlace.referrerSpec.IsEmpty()) {
      (void)NS_NewURI(getter_AddRefs(referrerURI), mPlace.referrerSpec);
    }

    nsCOMPtr<mozIVisitInfo> visit =
      new VisitInfo(mPlace.visitId, mPlace.visitTime, mPlace.transitionType,
                    referrerURI.forget(), mPlace.sessionId);
    PlaceInfo::VisitsArray visits;
    (void)visits.AppendElement(visit);

    nsCOMPtr<nsIURI> uri;
    (void)NS_NewURI(getter_AddRefs(uri), mPlace.spec);

    // We do not notify about the frecency of the place.
    nsCOMPtr<mozIPlaceInfo> place =
      new PlaceInfo(mPlace.placeId, mPlace.guid, uri.forget(), mPlace.title,
                    -1, visits);
    if (NS_SUCCEEDED(mResult)) {
      (void)mCallback->HandleResult(place);
    }
    else {
      (void)mCallback->HandleError(mResult, place);
    }

    return NS_OK;
  }

private:
  /**
   * Callers MUST hold a strong reference to this that outlives us because we
   * may be created off of the main thread, and therefore cannot call AddRef on
   * this object (and therefore cannot hold a strong reference to it).
   */
  mozIVisitInfoCallback* mCallback;
  VisitData mPlace;
  const nsresult mResult;
};

/**
 * Notifies a callback object when the operation is complete.
 */
class NotifyCompletion : public nsRunnable
{
public:
  NotifyCompletion(mozIVisitInfoCallback* aCallback)
  : mCallback(aCallback)
  {
    NS_PRECONDITION(aCallback, "Must pass a non-null callback!");
  }

  NS_IMETHOD Run()
  {
    if (NS_IsMainThread()) {
      (void)mCallback->HandleCompletion();
    }
    else {
      (void)NS_DispatchToMainThread(this);

      // Also dispatch an event to release the reference to the callback after
      // we have run.
      nsCOMPtr<nsIThread> mainThread = do_GetMainThread();
      (void)NS_ProxyRelease(mainThread, mCallback, true);
    }
    return NS_OK;
  }

private:
  /**
   * Callers MUST hold a strong reference to this because we may be created
   * off of the main thread, and therefore cannot call AddRef on this object
   * (and therefore cannot hold a strong reference to it). If invoked from a
   * background thread, NotifyCompletion will release the reference to this.
   */
  mozIVisitInfoCallback* mCallback;
};

/**
 * Checks to see if we can add aURI to history, and dispatches an error to
 * aCallback (if provided) if we cannot.
 *
 * @param aURI
 *        The URI to check.
 * @param [optional] aGUID
 *        The guid of the URI to check.  This is passed back to the callback.
 * @param [optional] aCallback
 *        The callback to notify if the URI cannot be added to history.
 * @return true if the URI can be added to history, false otherwise.
 */
bool
CanAddURI(nsIURI* aURI,
          const nsCString& aGUID = EmptyCString(),
          mozIVisitInfoCallback* aCallback = NULL)
{
  nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(navHistory, false);

  bool canAdd;
  nsresult rv = navHistory->CanAddURI(aURI, &canAdd);
  if (NS_SUCCEEDED(rv) && canAdd) {
    return true;
  };

  // We cannot add the URI.  Notify the callback, if we were given one.
  if (aCallback) {
    // NotifyVisitInfoCallback does not hold a strong reference to the callback, so we
    // have to manage it by AddRefing now and then releasing it after the event
    // has run.
    NS_ADDREF(aCallback);

    VisitData place(aURI);
    place.guid = aGUID;
    nsCOMPtr<nsIRunnable> event =
      new NotifyVisitInfoCallback(aCallback, place, NS_ERROR_INVALID_ARG);
    (void)NS_DispatchToMainThread(event);

    // Also dispatch an event to release our reference to the callback after
    // NotifyVisitInfoCallback has run.
    nsCOMPtr<nsIThread> mainThread = do_GetMainThread();
    (void)NS_ProxyRelease(mainThread, aCallback, true);
  }

  return false;
}

/**
 * Adds a visit to the database.
 */
class InsertVisitedURIs : public nsRunnable
{
public:
  /**
   * Adds a visit to the database asynchronously.
   *
   * @param aConnection
   *        The database connection to use for these operations.
   * @param aPlaces
   *        The locations to record visits.
   * @param [optional] aCallback
   *        The callback to notify about the visit.
   */
  static nsresult Start(mozIStorageConnection* aConnection,
                        nsTArray<VisitData>& aPlaces,
                        mozIVisitInfoCallback* aCallback = NULL)
  {
    NS_PRECONDITION(NS_IsMainThread(),
                    "This should be called on the main thread");
    NS_PRECONDITION(aPlaces.Length() > 0, "Must pass a non-empty array!");

    nsRefPtr<InsertVisitedURIs> event =
      new InsertVisitedURIs(aConnection, aPlaces, aCallback);

    // Get the target thread, and then start the work!
    nsCOMPtr<nsIEventTarget> target = do_GetInterface(aConnection);
    NS_ENSURE_TRUE(target, NS_ERROR_UNEXPECTED);
    nsresult rv = target->Dispatch(event, NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  NS_IMETHOD Run()
  {
    NS_PRECONDITION(!NS_IsMainThread(),
                    "This should not be called on the main thread");

    // Prevent the main thread from shutting down while this is running.
    MutexAutoLock lockedScope(mHistory->GetShutdownMutex());
    if(mHistory->IsShuttingDown()) {
      // If we were already shutting down, we cannot insert the URIs.
      return NS_OK;
    }

    mozStorageTransaction transaction(mDBConn, false,
                                      mozIStorageConnection::TRANSACTION_IMMEDIATE);

    VisitData* lastPlace = NULL;
    for (nsTArray<VisitData>::size_type i = 0; i < mPlaces.Length(); i++) {
      VisitData& place = mPlaces.ElementAt(i);
      VisitData& referrer = mReferrers.ElementAt(i);

      // We can avoid a database lookup if it's the same place as the last
      // visit we added.
      bool known = (lastPlace && lastPlace->IsSamePlaceAs(place)) ||
                   mHistory->FetchPageInfo(place);

      FetchReferrerInfo(referrer, place);

      nsresult rv = DoDatabaseInserts(known, place, referrer);
      if (mCallback) {
        nsCOMPtr<nsIRunnable> event =
          new NotifyVisitInfoCallback(mCallback, place, rv);
        nsresult rv2 = NS_DispatchToMainThread(event);
        NS_ENSURE_SUCCESS(rv2, rv2);
      }
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIRunnable> event = new NotifyVisitObservers(place, referrer);
      rv = NS_DispatchToMainThread(event);
      NS_ENSURE_SUCCESS(rv, rv);

      // Notify about title change if needed.
      if ((!known && !place.title.IsVoid()) || place.titleChanged) {
        event = new NotifyTitleObservers(place.spec, place.title, place.guid);
        rv = NS_DispatchToMainThread(event);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      lastPlace = &mPlaces.ElementAt(i);
    }

    nsresult rv = transaction.Commit();
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }
private:
  InsertVisitedURIs(mozIStorageConnection* aConnection,
                    nsTArray<VisitData>& aPlaces,
                    mozIVisitInfoCallback* aCallback)
  : mDBConn(aConnection)
  , mCallback(aCallback)
  , mHistory(History::GetService())
  {
    NS_PRECONDITION(NS_IsMainThread(),
                    "This should be called on the main thread");

    (void)mPlaces.SwapElements(aPlaces);
    (void)mReferrers.SetLength(mPlaces.Length());

    nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
    NS_ABORT_IF_FALSE(navHistory, "Could not get nsNavHistory?!");

    for (nsTArray<VisitData>::size_type i = 0; i < mPlaces.Length(); i++) {
      mReferrers[i].spec = mPlaces[i].referrerSpec;

      // If we are inserting a place into an empty mPlaces array, we need to
      // check to make sure we do not store a bogus session id that is higher
      // than the current maximum session id.
      if (i == 0) {
        PRInt64 newSessionId = navHistory->GetNewSessionID();
        if (mPlaces[0].sessionId > newSessionId) {
          mPlaces[0].sessionId = newSessionId;
        }
      }

      // Speculatively get a new session id for our visit if the current session
      // id is non-valid or if it is larger than the current largest session id.
      // While it is true that we will use the session id from the referrer if
      // the visit was "recent" enough, we cannot call this method off of the
      // main thread, so we have to consume an id now.
      if (mPlaces[i].sessionId <= 0 ||
          (i > 0 && mPlaces[i].sessionId >= mPlaces[0].sessionId)) {
        mPlaces[i].sessionId = navHistory->GetNewSessionID();
      }

#ifdef DEBUG
      nsCOMPtr<nsIURI> uri;
      (void)NS_NewURI(getter_AddRefs(uri), mPlaces[i].spec);
      NS_ASSERTION(CanAddURI(uri),
                   "Passed a VisitData with a URI we cannot add to history!");
#endif
    }

    // We AddRef on the main thread, and release it when we are destroyed.
    NS_IF_ADDREF(mCallback);
  }

  virtual ~InsertVisitedURIs()
  {
    if (mCallback) {
      nsCOMPtr<nsIThread> mainThread = do_GetMainThread();
      (void)NS_ProxyRelease(mainThread, mCallback, true);
    }
  }

  /**
   * Inserts or updates the entry in moz_places for this visit, adds the visit,
   * and updates the frecency of the place.
   *
   * @param aKnown
   *        True if we already have an entry for this place in moz_places, false
   *        otherwise.
   * @param aPlace
   *        The place we are adding a visit for.
   * @param aReferrer
   *        The referrer for aPlace.
   */
  nsresult DoDatabaseInserts(bool aKnown,
                             VisitData& aPlace,
                             VisitData& aReferrer)
  {
    NS_PRECONDITION(!NS_IsMainThread(),
                    "This should not be called on the main thread");

    // If the page was in moz_places, we need to update the entry.
    nsresult rv;
    if (aKnown) {
      rv = mHistory->UpdatePlace(aPlace);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    // Otherwise, the page was not in moz_places, so now we have to add it.
    else {
      rv = mHistory->InsertPlace(aPlace);
      NS_ENSURE_SUCCESS(rv, rv);

      // We need the place id and guid of the page we just inserted when we
      // have a callback or when the GUID isn't known.  No point in doing the
      // disk I/O if we do not need it.
      if (mCallback || aPlace.guid.IsEmpty()) {
        bool exists = mHistory->FetchPageInfo(aPlace);
        if (!exists) {
          NS_NOTREACHED("should have an entry in moz_places");
        }
      }
    }

    rv = AddVisit(aPlace, aReferrer);
    NS_ENSURE_SUCCESS(rv, rv);

    // TODO (bug 623969) we shouldn't update this after each visit, but
    // rather only for each unique place to save disk I/O.
    rv = UpdateFrecency(aPlace);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  /**
   * Loads visit information about the page into _place.
   *
   * @param _place
   *        The VisitData for the place we need to know visit information about.
   * @param [optional] aThresholdStart
   *        The timestamp of a new visit (not represented by _place) used to
   *        determine if the page was recently visited or not.
   * @return true if the page was recently (determined with aThresholdStart)
   *         visited, false otherwise.
   */
  bool FetchVisitInfo(VisitData& _place,
                      PRTime aThresholdStart = 0)
  {
    NS_PRECONDITION(!_place.spec.IsEmpty(), "must have a non-empty spec!");

    nsCOMPtr<mozIStorageStatement> stmt;
    // If we have a visitTime, we want information on that specific visit.
    if (_place.visitTime) {
      stmt = mHistory->GetStatement(
        "SELECT id, session, visit_date "
        "FROM moz_historyvisits "
        "WHERE place_id = (SELECT id FROM moz_places WHERE url = :page_url) "
        "AND visit_date = :visit_date "
      );
      NS_ENSURE_TRUE(stmt, false);

      mozStorageStatementScoper scoper(stmt);
      nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("visit_date"),
                                          _place.visitTime);
      NS_ENSURE_SUCCESS(rv, rv);

      scoper.Abandon();
    }
    // Otherwise, we want information about the most recent visit.
    else {
      stmt = mHistory->GetStatement(
        "SELECT id, session, visit_date "
        "FROM moz_historyvisits "
        "WHERE place_id = (SELECT id FROM moz_places WHERE url = :page_url) "
        "ORDER BY visit_date DESC "
      );
      NS_ENSURE_TRUE(stmt, false);
    }
    mozStorageStatementScoper scoper(stmt);

    nsresult rv = URIBinder::Bind(stmt, NS_LITERAL_CSTRING("page_url"),
                                  _place.spec);
    NS_ENSURE_SUCCESS(rv, false);

    bool hasResult;
    rv = stmt->ExecuteStep(&hasResult);
    NS_ENSURE_SUCCESS(rv, false);
    if (!hasResult) {
      return false;
    }

    rv = stmt->GetInt64(0, &_place.visitId);
    NS_ENSURE_SUCCESS(rv, false);
    rv = stmt->GetInt64(1, &_place.sessionId);
    NS_ENSURE_SUCCESS(rv, false);
    rv = stmt->GetInt64(2, &_place.visitTime);
    NS_ENSURE_SUCCESS(rv, false);

    // If we have been given a visit threshold start time, go ahead and
    // calculate if we have been recently visited.
    if (aThresholdStart &&
        aThresholdStart - _place.visitTime <= RECENT_EVENT_THRESHOLD) {
      return true;
    }

    return false;
  }

  /**
   * Fetches information about a referrer and sets the session id for aPlace if
   * it was a recent visit or not.
   *
   * @param aReferrer
   *        The VisitData for the referrer.  This will be populated with
   *        FetchVisitInfo.
   * @param aPlace
   *        The VisitData for the visit we will eventually add.
   *
   */
  void FetchReferrerInfo(VisitData& aReferrer,
                         VisitData& aPlace)
  {
    if (aReferrer.spec.IsEmpty()) {
      return;
    }

    // If we had a referrer, we want to know about its last visit to put this
    // new visit into the same session.
    bool recentVisit = FetchVisitInfo(aReferrer, aPlace.visitTime);
    // At this point, we know the referrer's session id, which this new visit
    // should also share.
    if (recentVisit) {
      aPlace.sessionId = aReferrer.sessionId;
    }
    // However, if it isn't recent enough, we don't care to log anything about
    // the referrer and we'll start a new session.
    else {
      // We must change both the place and referrer to indicate that we will
      // not be using the referrer's data. This behavior has test coverage, so
      // if this invariant changes, we'll know.
      aPlace.referrerSpec.Truncate();
      aReferrer.visitId = 0;
    }
  }

  /**
   * Adds a visit for _place and updates it with the right visit id.
   *
   * @param _place
   *        The VisitData for the place we need to know visit information about.
   * @param aReferrer
   *        A reference to the referrer's visit data.
   */
  nsresult AddVisit(VisitData& _place,
                    const VisitData& aReferrer)
  {
    nsresult rv;
    nsCOMPtr<mozIStorageStatement> stmt;
    if (_place.placeId) {
      stmt = mHistory->GetStatement(
        "INSERT INTO moz_historyvisits "
          "(from_visit, place_id, visit_date, visit_type, session) "
        "VALUES (:from_visit, :page_id, :visit_date, :visit_type, :session) "
      );
      NS_ENSURE_STATE(stmt);
      rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("page_id"), _place.placeId);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      stmt = mHistory->GetStatement(
        "INSERT INTO moz_historyvisits "
          "(from_visit, place_id, visit_date, visit_type, session) "
        "VALUES (:from_visit, (SELECT id FROM moz_places WHERE url = :page_url), :visit_date, :visit_type, :session) "
      );
      NS_ENSURE_STATE(stmt);
      rv = URIBinder::Bind(stmt, NS_LITERAL_CSTRING("page_url"), _place.spec);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("from_visit"),
                               aReferrer.visitId);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("visit_date"),
                               _place.visitTime);
    NS_ENSURE_SUCCESS(rv, rv);
    PRUint32 transitionType = _place.transitionType;
    NS_ASSERTION(transitionType >= nsINavHistoryService::TRANSITION_LINK &&
                 transitionType <= nsINavHistoryService::TRANSITION_FRAMED_LINK,
                 "Invalid transition type!");
    rv = stmt->BindInt32ByName(NS_LITERAL_CSTRING("visit_type"),
                               transitionType);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("session"),
                               _place.sessionId);
    NS_ENSURE_SUCCESS(rv, rv);

    mozStorageStatementScoper scoper(stmt);
    rv = stmt->Execute();
    NS_ENSURE_SUCCESS(rv, rv);

    // Now that it should be in the database, we need to obtain the id of the
    // visit we just added.
    (void)FetchVisitInfo(_place);

    return NS_OK;
  }

  /**
   * Updates the frecency, and possibly the hidden-ness of aPlace.
   *
   * @param aPlace
   *        The VisitData for the place we want to update.
   */
  nsresult UpdateFrecency(const VisitData& aPlace)
  {
    // Don't update frecency if the page should not appear in autocomplete.
    if (aPlace.frecency == 0) {
      return NS_OK;
    }

    nsresult rv;
    { // First, set our frecency to the proper value.
      nsCOMPtr<mozIStorageStatement> stmt;
      if (aPlace.placeId) {
        stmt = mHistory->GetStatement(
          "UPDATE moz_places "
          "SET frecency = CALCULATE_FRECENCY(:page_id) "
          "WHERE id = :page_id"
        );
        NS_ENSURE_STATE(stmt);
        rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("page_id"), aPlace.placeId);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else {
        stmt = mHistory->GetStatement(
          "UPDATE moz_places "
          "SET frecency = CALCULATE_FRECENCY(id) "
          "WHERE url = :page_url"
        );
        NS_ENSURE_STATE(stmt);
        rv = URIBinder::Bind(stmt, NS_LITERAL_CSTRING("page_url"), aPlace.spec);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      mozStorageStatementScoper scoper(stmt);

      rv = stmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (!aPlace.hidden) {
      // Mark the page as not hidden if the frecency is now nonzero.
      nsCOMPtr<mozIStorageStatement> stmt;
      if (aPlace.placeId) {
        stmt = mHistory->GetStatement(
          "UPDATE moz_places "
          "SET hidden = 0 "
          "WHERE id = :page_id AND frecency <> 0"
        );
        NS_ENSURE_STATE(stmt);
        rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("page_id"), aPlace.placeId);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else {
        stmt = mHistory->GetStatement(
          "UPDATE moz_places "
          "SET hidden = 0 "
          "WHERE url = :page_url AND frecency <> 0"
        );
        NS_ENSURE_STATE(stmt);
        rv = URIBinder::Bind(stmt, NS_LITERAL_CSTRING("page_url"), aPlace.spec);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      mozStorageStatementScoper scoper(stmt);
      rv = stmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
  }

  mozIStorageConnection* mDBConn;

  nsTArray<VisitData> mPlaces;
  nsTArray<VisitData> mReferrers;

  /**
   * We own a strong reference to this, but in an indirect way.  We call AddRef
   * in our constructor, which happens on the main thread, and proxy the relase
   * of the object to the main thread in our destructor.
   */
  mozIVisitInfoCallback* mCallback;

  /**
   * Strong reference to the History object because we do not want it to
   * disappear out from under us.
   */
  nsRefPtr<History> mHistory;
};

/**
 * Sets the page title for a page in moz_places (if necessary).
 */
class SetPageTitle : public nsRunnable
{
public:
  /**
   * Sets a pages title in the database asynchronously.
   *
   * @param aConnection
   *        The database connection to use for this operation.
   * @param aURI
   *        The URI to set the page title on.
   * @param aTitle
   *        The title to set for the page, if the page exists.
   */
  static nsresult Start(mozIStorageConnection* aConnection,
                        nsIURI* aURI,
                        const nsAString& aTitle)
  {
    NS_PRECONDITION(NS_IsMainThread(),
                    "This should be called on the main thread");
    NS_PRECONDITION(aURI, "Must pass a non-null URI object!");

    nsCString spec;
    nsresult rv = aURI->GetSpec(spec);
    NS_ENSURE_SUCCESS(rv, rv);

    nsRefPtr<SetPageTitle> event = new SetPageTitle(spec, aTitle);

    // Get the target thread, and then start the work!
    nsCOMPtr<nsIEventTarget> target = do_GetInterface(aConnection);
    NS_ENSURE_TRUE(target, NS_ERROR_UNEXPECTED);
    rv = target->Dispatch(event, NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  NS_IMETHOD Run()
  {
    NS_PRECONDITION(!NS_IsMainThread(),
                    "This should not be called on the main thread");

    // First, see if the page exists in the database (we'll need its id later).
    bool exists = mHistory->FetchPageInfo(mPlace);
    if (!exists || !mPlace.titleChanged) {
      // We have no record of this page, or we have no title change, so there
      // is no need to do any further work.
      return NS_OK;
    }

    NS_ASSERTION(mPlace.placeId > 0,
                 "We somehow have an invalid place id here!");

    // Now we can update our database record.
    nsCOMPtr<mozIStorageStatement> stmt =
      mHistory->GetStatement(
        "UPDATE moz_places "
        "SET title = :page_title "
        "WHERE id = :page_id "
      );
    NS_ENSURE_STATE(stmt);

    {
      mozStorageStatementScoper scoper(stmt);
      nsresult rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("page_id"),
                                          mPlace.placeId);
      NS_ENSURE_SUCCESS(rv, rv);
      // Empty strings should clear the title, just like
      // nsNavHistory::SetPageTitle.
      if (mPlace.title.IsEmpty()) {
        rv = stmt->BindNullByName(NS_LITERAL_CSTRING("page_title"));
      }
      else {
        rv = stmt->BindStringByName(NS_LITERAL_CSTRING("page_title"),
                                    StringHead(mPlace.title, TITLE_LENGTH_MAX));
      }
      NS_ENSURE_SUCCESS(rv, rv);
      rv = stmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    nsCOMPtr<nsIRunnable> event =
      new NotifyTitleObservers(mPlace.spec, mPlace.title, mPlace.guid);
    nsresult rv = NS_DispatchToMainThread(event);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

private:
  SetPageTitle(const nsCString& aSpec,
               const nsAString& aTitle)
  : mHistory(History::GetService())
  {
    mPlace.spec = aSpec;
    mPlace.title = aTitle;
  }

  VisitData mPlace;

  /**
   * Strong reference to the History object because we do not want it to
   * disappear out from under us.
   */
  nsRefPtr<History> mHistory;
};

/**
 * Adds download-specific annotations to a download page.
 */
class SetDownloadAnnotations MOZ_FINAL : public mozIVisitInfoCallback
{
public:
  NS_DECL_ISUPPORTS

  SetDownloadAnnotations(nsIURI* aDestination)
  : mDestination(aDestination)
  , mHistory(History::GetService())
  {
    MOZ_ASSERT(mDestination);
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD HandleError(nsresult aResultCode, mozIPlaceInfo *aPlaceInfo)
  {
    // Just don't add the annotations in case the visit isn't added.
    return NS_OK;
  }

  NS_IMETHOD HandleResult(mozIPlaceInfo *aPlaceInfo)
  {
    // Exit silently if the download destination is not a local file.
    nsCOMPtr<nsIFileURL> destinationFileURL = do_QueryInterface(mDestination);
    if (!destinationFileURL) {
      return NS_OK;
    }

    nsCOMPtr<nsIURI> source;
    nsresult rv = aPlaceInfo->GetUri(getter_AddRefs(source));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIFile> destinationFile;
    rv = destinationFileURL->GetFile(getter_AddRefs(destinationFile));
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString destinationFileName;
    rv = destinationFile->GetLeafName(destinationFileName);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCAutoString destinationURISpec;
    rv = destinationFileURL->GetSpec(destinationURISpec);
    NS_ENSURE_SUCCESS(rv, rv);

    // Use annotations for storing the additional download metadata.
    nsAnnotationService* annosvc = nsAnnotationService::GetAnnotationService();
    NS_ENSURE_TRUE(annosvc, NS_ERROR_OUT_OF_MEMORY);

    rv = annosvc->SetPageAnnotationString(
      source,
      DESTINATIONFILEURI_ANNO,
      NS_ConvertUTF8toUTF16(destinationURISpec),
      0,
      nsIAnnotationService::EXPIRE_WITH_HISTORY
    );
    NS_ENSURE_SUCCESS(rv, rv);

    rv = annosvc->SetPageAnnotationString(
      source,
      DESTINATIONFILENAME_ANNO,
      destinationFileName,
      0,
      nsIAnnotationService::EXPIRE_WITH_HISTORY
    );
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString title;
    rv = aPlaceInfo->GetTitle(title);
    NS_ENSURE_SUCCESS(rv, rv);

    // In case we are downloading a file that does not correspond to a web
    // page for which the title is present, we populate the otherwise empty
    // history title with the name of the destination file, to allow it to be
    // visible and searchable in history results.
    if (title.IsEmpty()) {
      rv = mHistory->SetURITitle(source, destinationFileName);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
  }

  NS_IMETHOD HandleCompletion()
  {
    return NS_OK;
  }

private:
  nsCOMPtr<nsIURI> mDestination;

  /**
   * Strong reference to the History object because we do not want it to
   * disappear out from under us.
   */
  nsRefPtr<History> mHistory;
};
NS_IMPL_ISUPPORTS1(
  SetDownloadAnnotations,
  mozIVisitInfoCallback
)

/**
 * Stores an embed visit, and notifies observers.
 *
 * @param aPlace
 *        The VisitData of the visit to store as an embed visit.
 * @param [optional] aCallback
 *        The mozIVisitInfoCallback to notify, if provided.
 */
void
StoreAndNotifyEmbedVisit(VisitData& aPlace,
                         mozIVisitInfoCallback* aCallback = NULL)
{
  NS_PRECONDITION(aPlace.transitionType == nsINavHistoryService::TRANSITION_EMBED,
                  "Must only pass TRANSITION_EMBED visits to this!");
  NS_PRECONDITION(NS_IsMainThread(), "Must be called on the main thread!");

  nsCOMPtr<nsIURI> uri;
  (void)NS_NewURI(getter_AddRefs(uri), aPlace.spec);

  nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
  if (!navHistory || !uri) {
    return;
  }

  navHistory->registerEmbedVisit(uri, aPlace.visitTime);

  if (aCallback) {
    // NotifyVisitInfoCallback does not hold a strong reference to the callback,
    // so we have to manage it by AddRefing now and then releasing it after the
    // event has run.
    NS_ADDREF(aCallback);
    nsCOMPtr<nsIRunnable> event =
      new NotifyVisitInfoCallback(aCallback, aPlace, NS_OK);
    (void)NS_DispatchToMainThread(event);

    // Also dispatch an event to release our reference to the callback after
    // NotifyVisitInfoCallback has run.
    nsCOMPtr<nsIThread> mainThread = do_GetMainThread();
    (void)NS_ProxyRelease(mainThread, aCallback, true);
  }

  VisitData noReferrer;
  nsCOMPtr<nsIRunnable> event = new NotifyVisitObservers(aPlace, noReferrer);
  (void)NS_DispatchToMainThread(event);
}

NS_MEMORY_REPORTER_MALLOC_SIZEOF_FUN(HistoryLinksHashtableMallocSizeOf,
                                     "history-links-hashtable")

PRInt64 GetHistoryObserversSize()
{
  History* history = History::GetService();
  return history ?
         history->SizeOfIncludingThis(HistoryLinksHashtableMallocSizeOf) : 0;
}

NS_MEMORY_REPORTER_IMPLEMENT(HistoryService,
  "explicit/history-links-hashtable",
  KIND_HEAP,
  UNITS_BYTES,
  GetHistoryObserversSize,
  "Memory used by the hashtable of observers Places uses to notify objects of "
  "changes to links' visited state.")

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//// History

History* History::gService = NULL;

History::History()
  : mShuttingDown(false)
  , mShutdownMutex("History::mShutdownMutex")
  , mRecentlyVisitedURIsNextIndex(0)
{
  NS_ASSERTION(!gService, "Ruh-roh!  This service has already been created!");
  gService = this;

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  NS_WARN_IF_FALSE(os, "Observer service was not found!");
  if (os) {
    (void)os->AddObserver(this, TOPIC_PLACES_SHUTDOWN, false);
  }

  NS_RegisterMemoryReporter(new NS_MEMORY_REPORTER_NAME(HistoryService));
}

History::~History()
{
  gService = NULL;

#ifdef DEBUG
  if (mObservers.IsInitialized()) {
    NS_ASSERTION(mObservers.Count() == 0,
                 "Not all Links were removed before we disappear!");
  }
#endif
}

void
History::NotifyVisited(nsIURI* aURI)
{
  NS_ASSERTION(aURI, "Ruh-roh!  A NULL URI was passed to us!");

  nsAutoScriptBlocker scriptBlocker;

  if (XRE_GetProcessType() == GeckoProcessType_Default) {
    nsTArray<ContentParent*> cplist;
    ContentParent::GetAll(cplist);
    for (PRUint32 i = 0; i < cplist.Length(); ++i) {
      unused << cplist[i]->SendNotifyVisited(aURI);
    }
  }

  // If the hash table has not been initialized, then we have nothing to notify
  // about.
  if (!mObservers.IsInitialized()) {
    return;
  }

  // Additionally, if we have no observers for this URI, we have nothing to
  // notify about.
  KeyClass* key = mObservers.GetEntry(aURI);
  if (!key) {
    return;
  }

  // Update status of each Link node.
  {
    // RemoveEntry will destroy the array, this iterator should not survive it.
    ObserverArray::ForwardIterator iter(key->array);
    while (iter.HasMore()) {
      Link* link = iter.GetNext();
      link->SetLinkState(eLinkState_Visited);
      // Verify that the observers hash doesn't mutate while looping through
      // the links associated with this URI.
      NS_ABORT_IF_FALSE(key == mObservers.GetEntry(aURI),
                        "The URIs hash mutated!");
    }
  }

  // All the registered nodes can now be removed for this URI.
  mObservers.RemoveEntry(aURI);
}

mozIStorageAsyncStatement*
History::GetIsVisitedStatement()
{
  if (mIsVisitedStatement) {
    return mIsVisitedStatement;
  }

  // If we don't yet have a database connection, go ahead and clone it now.
  if (!mReadOnlyDBConn) {
    mozIStorageConnection* dbConn = GetDBConn();
    NS_ENSURE_TRUE(dbConn, nsnull);

    (void)dbConn->Clone(true, getter_AddRefs(mReadOnlyDBConn));
    NS_ENSURE_TRUE(mReadOnlyDBConn, nsnull);
  }

  // Now we can create our cached statement.
  nsresult rv = mReadOnlyDBConn->CreateAsyncStatement(NS_LITERAL_CSTRING(
    "SELECT 1 "
    "FROM moz_places h "
    "WHERE url = ?1 "
      "AND last_visit_date NOTNULL "
  ),  getter_AddRefs(mIsVisitedStatement));
  NS_ENSURE_SUCCESS(rv, nsnull);
  return mIsVisitedStatement;
}

nsresult
History::InsertPlace(const VisitData& aPlace)
{
  NS_PRECONDITION(aPlace.placeId == 0, "should not have a valid place id!");
  NS_PRECONDITION(!NS_IsMainThread(), "must be called off of the main thread!");

  nsCOMPtr<mozIStorageStatement> stmt = GetStatement(
      "INSERT INTO moz_places "
        "(url, title, rev_host, hidden, typed, frecency, guid) "
      "VALUES (:url, :title, :rev_host, :hidden, :typed, :frecency, :guid) "
    );
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindStringByName(NS_LITERAL_CSTRING("rev_host"),
                                       aPlace.revHost);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = URIBinder::Bind(stmt, NS_LITERAL_CSTRING("url"), aPlace.spec);
  NS_ENSURE_SUCCESS(rv, rv);
  // Empty strings should have no title, just like nsNavHistory::SetPageTitle.
  if (aPlace.title.IsEmpty()) {
    rv = stmt->BindNullByName(NS_LITERAL_CSTRING("title"));
  }
  else {
    rv = stmt->BindStringByName(NS_LITERAL_CSTRING("title"),
                                StringHead(aPlace.title, TITLE_LENGTH_MAX));
  }
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt32ByName(NS_LITERAL_CSTRING("typed"), aPlace.typed);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt32ByName(NS_LITERAL_CSTRING("frecency"), aPlace.frecency);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt32ByName(NS_LITERAL_CSTRING("hidden"), aPlace.hidden);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCAutoString guid(aPlace.guid);
  if (aPlace.guid.IsVoid()) {
    rv = GenerateGUID(guid);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  rv = stmt->BindUTF8StringByName(NS_LITERAL_CSTRING("guid"), guid);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
History::UpdatePlace(const VisitData& aPlace)
{
  NS_PRECONDITION(!NS_IsMainThread(), "must be called off of the main thread!");
  NS_PRECONDITION(aPlace.placeId > 0, "must have a valid place id!");
  NS_PRECONDITION(!aPlace.guid.IsVoid(), "must have a guid!");

  nsCOMPtr<mozIStorageStatement> stmt = GetStatement(
      "UPDATE moz_places "
      "SET title = :title, "
          "hidden = :hidden, "
          "typed = :typed, "
          "guid = :guid "
      "WHERE id = :page_id "
    );
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);

  nsresult rv;
  // Empty strings should clear the title, just like nsNavHistory::SetPageTitle.
  if (aPlace.title.IsEmpty()) {
    rv = stmt->BindNullByName(NS_LITERAL_CSTRING("title"));
  }
  else {
    rv = stmt->BindStringByName(NS_LITERAL_CSTRING("title"),
                                StringHead(aPlace.title, TITLE_LENGTH_MAX));
  }
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt32ByName(NS_LITERAL_CSTRING("typed"), aPlace.typed);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt32ByName(NS_LITERAL_CSTRING("hidden"), aPlace.hidden);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindUTF8StringByName(NS_LITERAL_CSTRING("guid"), aPlace.guid);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("page_id"),
                             aPlace.placeId);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

bool
History::FetchPageInfo(VisitData& _place)
{
  NS_PRECONDITION(!_place.spec.IsEmpty(), "must have a non-empty spec!");
  NS_PRECONDITION(!NS_IsMainThread(), "must be called off of the main thread!");

  nsCOMPtr<mozIStorageStatement> stmt = GetStatement(
      "SELECT id, title, hidden, typed, guid "
      "FROM moz_places "
      "WHERE url = :page_url "
    );
  NS_ENSURE_TRUE(stmt, false);
  mozStorageStatementScoper scoper(stmt);

  nsresult rv = URIBinder::Bind(stmt, NS_LITERAL_CSTRING("page_url"),
                                _place.spec);
  NS_ENSURE_SUCCESS(rv, false);

  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, false);
  if (!hasResult) {
    return false;
  }

  rv = stmt->GetInt64(0, &_place.placeId);
  NS_ENSURE_SUCCESS(rv, false);

  nsAutoString title;
  rv = stmt->GetString(1, title);
  NS_ENSURE_SUCCESS(rv, true);

  // If the title we were given was void, that means we did not bother to set
  // it to anything.  As a result, ignore the fact that we may have changed the
  // title (because we don't want to, that would be empty), and set the title
  // to what is currently stored in the datbase.
  if (_place.title.IsVoid()) {
    _place.title = title;
  }
  // Otherwise, just indicate if the title has changed.
  else {
    _place.titleChanged = !(_place.title.Equals(title) ||
                            (_place.title.IsEmpty() && title.IsVoid()));
  }

  if (_place.hidden) {
    // If this transition was hidden, it is possible that others were not.
    // Any one visible transition makes this location visible. If database
    // has location as visible, reflect that in our data structure.
    PRInt32 hidden;
    rv = stmt->GetInt32(2, &hidden);
    _place.hidden = !!hidden;
    NS_ENSURE_SUCCESS(rv, true);
  }

  if (!_place.typed) {
    // If this transition wasn't typed, others might have been. If database
    // has location as typed, reflect that in our data structure.
    PRInt32 typed;
    rv = stmt->GetInt32(3, &typed);
    _place.typed = !!typed;
    NS_ENSURE_SUCCESS(rv, true);
  }

  if (_place.guid.IsVoid()) {
    rv = stmt->GetUTF8String(4, _place.guid);
    NS_ENSURE_SUCCESS(rv, true);
  }

  return true;
}

/* static */ size_t
History::SizeOfEntryExcludingThis(KeyClass* aEntry, nsMallocSizeOfFun aMallocSizeOf, void *)
{
  return aEntry->array.SizeOfExcludingThis(aMallocSizeOf);
}

size_t
History::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOfThis)
{
  return aMallocSizeOfThis(this) +
         mObservers.SizeOfExcludingThis(SizeOfEntryExcludingThis, aMallocSizeOfThis);
}

/* static */
History*
History::GetService()
{
  if (gService) {
    return gService;
  }

  nsCOMPtr<IHistory> service(do_GetService(NS_IHISTORY_CONTRACTID));
  NS_ABORT_IF_FALSE(service, "Cannot obtain IHistory service!");
  NS_ASSERTION(gService, "Our constructor was not run?!");

  return gService;
}

/* static */
History*
History::GetSingleton()
{
  if (!gService) {
    gService = new History();
    NS_ENSURE_TRUE(gService, nsnull);
  }

  NS_ADDREF(gService);
  return gService;
}

mozIStorageConnection*
History::GetDBConn()
{
  if (!mDB) {
    mDB = Database::GetDatabase();
    NS_ENSURE_TRUE(mDB, nsnull);
  }
  return mDB->MainConn();
}

void
History::Shutdown()
{
  MOZ_ASSERT(NS_IsMainThread());

  // Prevent other threads from scheduling uses of the DB while we mark
  // ourselves as shutting down.
  MutexAutoLock lockedScope(mShutdownMutex);
  MOZ_ASSERT(!mShuttingDown && "Shutdown was called more than once!");

  mShuttingDown = true;

  if (mReadOnlyDBConn) {
    if (mIsVisitedStatement) {
      (void)mIsVisitedStatement->Finalize();
    }
    (void)mReadOnlyDBConn->AsyncClose(nsnull);
  }
}

void
History::AppendToRecentlyVisitedURIs(nsIURI* aURI) {
  if (mRecentlyVisitedURIs.Length() < RECENTLY_VISITED_URI_SIZE) {
    // Append a new element while the array is not full.
    mRecentlyVisitedURIs.AppendElement(aURI);
  } else {
    // Otherwise, replace the oldest member.
    mRecentlyVisitedURIsNextIndex %= RECENTLY_VISITED_URI_SIZE;
    mRecentlyVisitedURIs.ElementAt(mRecentlyVisitedURIsNextIndex) = aURI;
    mRecentlyVisitedURIsNextIndex++;
  }
}

inline bool
History::IsRecentlyVisitedURI(nsIURI* aURI) {
  bool equals = false;
  RecentlyVisitedArray::index_type i;
  for (i = 0; i < mRecentlyVisitedURIs.Length() && !equals; ++i) {
    aURI->Equals(mRecentlyVisitedURIs.ElementAt(i), &equals);
  }
  return equals;
}

////////////////////////////////////////////////////////////////////////////////
//// IHistory

NS_IMETHODIMP
History::VisitURI(nsIURI* aURI,
                  nsIURI* aLastVisitedURI,
                  PRUint32 aFlags)
{
  NS_PRECONDITION(aURI, "URI should not be NULL.");
  if (mShuttingDown) {
    return NS_OK;
  }

  if (XRE_GetProcessType() == GeckoProcessType_Content) {
    mozilla::dom::ContentChild* cpc =
      mozilla::dom::ContentChild::GetSingleton();
    NS_ASSERTION(cpc, "Content Protocol is NULL!");
    (void)cpc->SendVisitURI(aURI, aLastVisitedURI, aFlags);
    return NS_OK;
  } 

  nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(navHistory, NS_ERROR_OUT_OF_MEMORY);

  // Silently return if URI is something we shouldn't add to DB.
  bool canAdd;
  nsresult rv = navHistory->CanAddURI(aURI, &canAdd);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!canAdd) {
    return NS_OK;
  }

  if (aLastVisitedURI) {
    bool same;
    rv = aURI->Equals(aLastVisitedURI, &same);
    NS_ENSURE_SUCCESS(rv, rv);
    if (same && IsRecentlyVisitedURI(aURI)) {
      // Do not save refresh visits if we have visited this URI recently.
      return NS_OK;
    }
  }

  nsTArray<VisitData> placeArray(1);
  NS_ENSURE_TRUE(placeArray.AppendElement(VisitData(aURI, aLastVisitedURI)),
                 NS_ERROR_OUT_OF_MEMORY);
  VisitData& place = placeArray.ElementAt(0);
  NS_ENSURE_FALSE(place.spec.IsEmpty(), NS_ERROR_INVALID_ARG);

  place.visitTime = PR_Now();

  // Assigns a type to the edge in the visit linked list. Each type will be
  // considered differently when weighting the frecency of a location.
  PRUint32 recentFlags = navHistory->GetRecentFlags(aURI);
  bool isFollowedLink = recentFlags & nsNavHistory::RECENT_ACTIVATED;

  // Embed visits should never be added to the database, and the same is valid
  // for redirects across frames.
  // For the above reasoning non-toplevel transitions are handled at first.
  // if the visit is toplevel or a non-toplevel followed link, then it can be
  // handled as usual and stored on disk.

  PRUint32 transitionType = nsINavHistoryService::TRANSITION_LINK;

  if (!(aFlags & IHistory::TOP_LEVEL) && !isFollowedLink) {
    // A frame redirected to a new site without user interaction.
    transitionType = nsINavHistoryService::TRANSITION_EMBED;
  }
  else if (aFlags & IHistory::REDIRECT_TEMPORARY) {
    transitionType = nsINavHistoryService::TRANSITION_REDIRECT_TEMPORARY;
  }
  else if (aFlags & IHistory::REDIRECT_PERMANENT) {
    transitionType = nsINavHistoryService::TRANSITION_REDIRECT_PERMANENT;
  }
  else if (recentFlags & nsNavHistory::RECENT_TYPED) {
    transitionType = nsINavHistoryService::TRANSITION_TYPED;
  }
  else if (recentFlags & nsNavHistory::RECENT_BOOKMARKED) {
    transitionType = nsINavHistoryService::TRANSITION_BOOKMARK;
  }
  else if (!(aFlags & IHistory::TOP_LEVEL) && isFollowedLink) {
    // User activated a link in a frame.
    transitionType = nsINavHistoryService::TRANSITION_FRAMED_LINK;
  }

  place.SetTransitionType(transitionType);
  place.hidden = GetHiddenState(aFlags & IHistory::REDIRECT_SOURCE,
                                transitionType);

  // Error pages should never be autocompleted.
  if (aFlags & IHistory::UNRECOVERABLE_ERROR) {
    place.frecency = 0;
  }

  // EMBED visits are session-persistent and should not go through the database.
  // They exist only to keep track of isVisited status during the session.
  if (place.transitionType == nsINavHistoryService::TRANSITION_EMBED) {
    StoreAndNotifyEmbedVisit(place);
  }
  else {
    mozIStorageConnection* dbConn = GetDBConn();
    NS_ENSURE_STATE(dbConn);

    rv = InsertVisitedURIs::Start(dbConn, placeArray);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Finally, notify that we've been visited.
  nsCOMPtr<nsIObserverService> obsService =
    mozilla::services::GetObserverService();
  if (obsService) {
    obsService->NotifyObservers(aURI, NS_LINK_VISITED_EVENT_TOPIC, nsnull);
  }

  return NS_OK;
}

NS_IMETHODIMP
History::RegisterVisitedCallback(nsIURI* aURI,
                                 Link* aLink)
{
  NS_ASSERTION(aURI, "Must pass a non-null URI!");
  if (XRE_GetProcessType() == GeckoProcessType_Content) {
    NS_PRECONDITION(aLink, "Must pass a non-null Link!");
  }

  // First, ensure that our hash table is setup.
  if (!mObservers.IsInitialized()) {
    mObservers.Init(VISIT_OBSERVERS_INITIAL_CACHE_SIZE);
  }

  // Obtain our array of observers for this URI.
#ifdef DEBUG
  bool keyAlreadyExists = !!mObservers.GetEntry(aURI);
#endif
  KeyClass* key = mObservers.PutEntry(aURI);
  NS_ENSURE_TRUE(key, NS_ERROR_OUT_OF_MEMORY);
  ObserverArray& observers = key->array;

  if (observers.IsEmpty()) {
    NS_ASSERTION(!keyAlreadyExists,
                 "An empty key was kept around in our hashtable!");

    // We are the first Link node to ask about this URI, or there are no pending
    // Links wanting to know about this URI.  Therefore, we should query the
    // database now.
    nsresult rv = VisitedQuery::Start(aURI);

    // In IPC builds, we are passed a NULL Link from
    // ContentParent::RecvStartVisitedQuery.  Since we won't be adding a NULL
    // entry to our list of observers, and the code after this point assumes
    // that aLink is non-NULL, we will need to return now.
    if (NS_FAILED(rv) || !aLink) {
      // Remove our array from the hashtable so we don't keep it around.
      mObservers.RemoveEntry(aURI);
      return rv;
    }
  }
  // In IPC builds, we are passed a NULL Link from
  // ContentParent::RecvStartVisitedQuery.  All of our code after this point
  // assumes aLink is non-NULL, so we have to return now.
  else if (!aLink) {
    NS_ASSERTION(XRE_GetProcessType() == GeckoProcessType_Default,
                 "We should only ever get a null Link in the default process!");
    return NS_OK;
  }

  // Sanity check that Links are not registered more than once for a given URI.
  // This will not catch a case where it is registered for two different URIs.
  NS_ASSERTION(!observers.Contains(aLink),
               "Already tracking this Link object!");

  // Start tracking our Link.
  if (!observers.AppendElement(aLink)) {
    // Curses - unregister and return failure.
    (void)UnregisterVisitedCallback(aURI, aLink);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

NS_IMETHODIMP
History::UnregisterVisitedCallback(nsIURI* aURI,
                                   Link* aLink)
{
  NS_ASSERTION(aURI, "Must pass a non-null URI!");
  NS_ASSERTION(aLink, "Must pass a non-null Link object!");

  // Get the array, and remove the item from it.
  KeyClass* key = mObservers.GetEntry(aURI);
  if (!key) {
    NS_ERROR("Trying to unregister for a URI that wasn't registered!");
    return NS_ERROR_UNEXPECTED;
  }
  ObserverArray& observers = key->array;
  if (!observers.RemoveElement(aLink)) {
    NS_ERROR("Trying to unregister a node that wasn't registered!");
    return NS_ERROR_UNEXPECTED;
  }

  // If the array is now empty, we should remove it from the hashtable.
  if (observers.IsEmpty()) {
    mObservers.RemoveEntry(aURI);
  }

  return NS_OK;
}

NS_IMETHODIMP
History::SetURITitle(nsIURI* aURI, const nsAString& aTitle)
{
  NS_PRECONDITION(aURI, "Must pass a non-null URI!");
  if (mShuttingDown) {
    return NS_OK;
  }

  if (XRE_GetProcessType() == GeckoProcessType_Content) {
    mozilla::dom::ContentChild * cpc = 
      mozilla::dom::ContentChild::GetSingleton();
    NS_ASSERTION(cpc, "Content Protocol is NULL!");
    (void)cpc->SendSetURITitle(aURI, nsString(aTitle));
    return NS_OK;
  } 

  nsNavHistory* navHistory = nsNavHistory::GetHistoryService();

  // At first, it seems like nav history should always be available here, no
  // matter what.
  //
  // nsNavHistory fails to register as a service if there is no profile in
  // place (for instance, if user is choosing a profile).
  //
  // Maybe the correct thing to do is to not register this service if no
  // profile has been selected?
  //
  NS_ENSURE_TRUE(navHistory, NS_ERROR_FAILURE);

  bool canAdd;
  nsresult rv = navHistory->CanAddURI(aURI, &canAdd);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!canAdd) {
    return NS_OK;
  }

  // Embed visits don't have a database entry, thus don't set a title on them.
  if (navHistory->hasEmbedVisit(aURI)) {
    return NS_OK;
  }

  mozIStorageConnection* dbConn = GetDBConn();
  NS_ENSURE_STATE(dbConn);

  rv = SetPageTitle::Start(dbConn, aURI, aTitle);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// nsIDownloadHistory

NS_IMETHODIMP
History::AddDownload(nsIURI* aSource, nsIURI* aReferrer,
                     PRTime aStartTime, nsIURI* aDestination)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG(aSource);

  if (mShuttingDown) {
    return NS_OK;
  }

  if (XRE_GetProcessType() == GeckoProcessType_Content) {
    NS_ERROR("Cannot add downloads to history from content process!");
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(navHistory, NS_ERROR_OUT_OF_MEMORY);

  // Silently return if URI is something we shouldn't add to DB.
  bool canAdd;
  nsresult rv = navHistory->CanAddURI(aSource, &canAdd);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!canAdd) {
    return NS_OK;
  }

  nsTArray<VisitData> placeArray(1);
  NS_ENSURE_TRUE(placeArray.AppendElement(VisitData(aSource, aReferrer)),
                 NS_ERROR_OUT_OF_MEMORY);
  VisitData& place = placeArray.ElementAt(0);
  NS_ENSURE_FALSE(place.spec.IsEmpty(), NS_ERROR_INVALID_ARG);

  place.visitTime = aStartTime;
  place.SetTransitionType(nsINavHistoryService::TRANSITION_DOWNLOAD);
  place.hidden = false;

  mozIStorageConnection* dbConn = GetDBConn();
  NS_ENSURE_STATE(dbConn);

  nsCOMPtr<mozIVisitInfoCallback> callback = aDestination
                                  ? new SetDownloadAnnotations(aDestination)
                                  : nsnull;

  rv = InsertVisitedURIs::Start(dbConn, placeArray, callback);
  NS_ENSURE_SUCCESS(rv, rv);

  // Finally, notify that we've been visited.
  nsCOMPtr<nsIObserverService> obsService =
    mozilla::services::GetObserverService();
  if (obsService) {
    obsService->NotifyObservers(aSource, NS_LINK_VISITED_EVENT_TOPIC, nsnull);
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// mozIAsyncHistory

NS_IMETHODIMP
History::UpdatePlaces(const jsval& aPlaceInfos,
                      mozIVisitInfoCallback* aCallback,
                      JSContext* aCtx)
{
  NS_ENSURE_TRUE(NS_IsMainThread(), NS_ERROR_UNEXPECTED);
  NS_ENSURE_TRUE(!JSVAL_IS_PRIMITIVE(aPlaceInfos), NS_ERROR_INVALID_ARG);

  uint32_t infosLength = 1;
  JSObject* infos;
  if (JS_IsArrayObject(aCtx, JSVAL_TO_OBJECT(aPlaceInfos))) {
    infos = JSVAL_TO_OBJECT(aPlaceInfos);
    (void)JS_GetArrayLength(aCtx, infos, &infosLength);
    NS_ENSURE_ARG(infosLength > 0);
  }
  else {
    // Build a temporary array to store this one item so the code below can
    // just loop.
    infos = JS_NewArrayObject(aCtx, 0, NULL);
    NS_ENSURE_TRUE(infos, NS_ERROR_OUT_OF_MEMORY);

    JSBool rc = JS_DefineElement(aCtx, infos, 0, aPlaceInfos, NULL, NULL, 0);
    NS_ENSURE_TRUE(rc, NS_ERROR_UNEXPECTED);
  }

  nsTArray<VisitData> visitData;
  for (uint32_t i = 0; i < infosLength; i++) {
    JSObject* info;
    nsresult rv = GetJSObjectFromArray(aCtx, infos, i, &info);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIURI> uri = GetURIFromJSObject(aCtx, info, "uri");
    nsCString guid;
    {
      nsString fatGUID;
      GetStringFromJSObject(aCtx, info, "guid", fatGUID);
      if (fatGUID.IsVoid()) {
        guid.SetIsVoid(true);
      }
      else {
        guid = NS_ConvertUTF16toUTF8(fatGUID);
      }
    }

    // Make sure that any uri we are given can be added to history, and if not,
    // skip it (CanAddURI will notify our callback for us).
    if (uri && !CanAddURI(uri, guid, aCallback)) {
      continue;
    }

    // We must have at least one of uri or guid.
    NS_ENSURE_ARG(uri || !guid.IsVoid());

    // If we were given a guid, make sure it is valid.
    bool isValidGUID = IsValidGUID(guid);
    NS_ENSURE_ARG(guid.IsVoid() || isValidGUID);

    nsString title;
    GetStringFromJSObject(aCtx, info, "title", title);

    JSObject* visits = NULL;
    {
      jsval visitsVal;
      JSBool rc = JS_GetProperty(aCtx, info, "visits", &visitsVal);
      NS_ENSURE_TRUE(rc, NS_ERROR_UNEXPECTED);
      if (!JSVAL_IS_PRIMITIVE(visitsVal)) {
        visits = JSVAL_TO_OBJECT(visitsVal);
        NS_ENSURE_ARG(JS_IsArrayObject(aCtx, visits));
      }
    }
    NS_ENSURE_ARG(visits);

    uint32_t visitsLength = 0;
    if (visits) {
      (void)JS_GetArrayLength(aCtx, visits, &visitsLength);
    }
    NS_ENSURE_ARG(visitsLength > 0);

    // Check each visit, and build our array of VisitData objects.
    visitData.SetCapacity(visitData.Length() + visitsLength);
    for (uint32_t j = 0; j < visitsLength; j++) {
      JSObject* visit;
      rv = GetJSObjectFromArray(aCtx, visits, j, &visit);
      NS_ENSURE_SUCCESS(rv, rv);

      VisitData& data = *visitData.AppendElement(VisitData(uri));
      data.title = title;
      data.guid = guid;

      // We must have a date and a transaction type!
      rv = GetIntFromJSObject(aCtx, visit, "visitDate", &data.visitTime);
      NS_ENSURE_SUCCESS(rv, rv);
      PRUint32 transitionType = 0;
      rv = GetIntFromJSObject(aCtx, visit, "transitionType", &transitionType);
      NS_ENSURE_SUCCESS(rv, rv);
      NS_ENSURE_ARG_RANGE(transitionType,
                          nsINavHistoryService::TRANSITION_LINK,
                          nsINavHistoryService::TRANSITION_FRAMED_LINK);
      data.SetTransitionType(transitionType);
      data.hidden = GetHiddenState(false, transitionType);

      // If the visit is an embed visit, we do not actually add it to the
      // database.
      if (transitionType == nsINavHistoryService::TRANSITION_EMBED) {
        StoreAndNotifyEmbedVisit(data, aCallback);
        visitData.RemoveElementAt(visitData.Length() - 1);
        continue;
      }

      // The session id is optional.
      rv = GetIntFromJSObject(aCtx, visit, "sessionId", &data.sessionId);
      if (rv == NS_ERROR_INVALID_ARG) {
        data.sessionId = 0;
      }
      else {
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // The referrer is optional.
      nsCOMPtr<nsIURI> referrer = GetURIFromJSObject(aCtx, visit,
                                                     "referrerURI");
      if (referrer) {
        (void)referrer->GetSpec(data.referrerSpec);
      }
    }
  }

  mozIStorageConnection* dbConn = GetDBConn();
  NS_ENSURE_STATE(dbConn);

  // It is possible that all of the visits we were passed were dissallowed by
  // CanAddURI, which isn't an error.  If we have no visits to add, however,
  // we should not call InsertVisitedURIs::Start.
  if (visitData.Length()) {
    nsresult rv = InsertVisitedURIs::Start(dbConn, visitData, aCallback);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Be sure to notify that all of our operations are complete.  This
  // is dispatched to the background thread first and redirected to the
  // main thread from there to make sure that all database notifications
  // and all embed or canAddURI notifications have finished.
  if (aCallback) {
    // NotifyCompletion does not hold a strong reference to the callback,
    // so we have to manage it by AddRefing now. NotifyCompletion will
    // release it for us once it has dispatched the callback to the main
    // thread.
    NS_ADDREF(aCallback);

    nsCOMPtr<nsIEventTarget> backgroundThread = do_GetInterface(dbConn);
    NS_ENSURE_TRUE(backgroundThread, NS_ERROR_UNEXPECTED);
    nsCOMPtr<nsIRunnable> event = new NotifyCompletion(aCallback);
    (void)backgroundThread->Dispatch(event, NS_DISPATCH_NORMAL);
  }

  return NS_OK;
}

NS_IMETHODIMP
History::IsURIVisited(nsIURI* aURI,
                      mozIVisitedStatusCallback* aCallback)
{
  NS_ENSURE_STATE(NS_IsMainThread());
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG(aCallback);

  nsresult rv = VisitedQuery::Start(aURI, aCallback);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// nsIObserver

NS_IMETHODIMP
History::Observe(nsISupports* aSubject, const char* aTopic,
                 const PRUnichar* aData)
{
  if (strcmp(aTopic, TOPIC_PLACES_SHUTDOWN) == 0) {
    Shutdown();

    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
      (void)os->RemoveObserver(this, TOPIC_PLACES_SHUTDOWN);
    }
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// nsISupports

NS_IMPL_THREADSAFE_ISUPPORTS4(
  History
, IHistory
, nsIDownloadHistory
, mozIAsyncHistory
, nsIObserver
)

} // namespace places
} // namespace mozilla
