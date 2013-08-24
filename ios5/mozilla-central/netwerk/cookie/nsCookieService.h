/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCookieService_h__
#define nsCookieService_h__

#include "nsICookieService.h"
#include "nsICookieManager.h"
#include "nsICookieManager2.h"
#include "nsIObserver.h"
#include "nsWeakReference.h"

#include "nsCookie.h"
#include "nsString.h"
#include "nsAutoPtr.h"
#include "nsHashKeys.h"
#include "nsTHashtable.h"
#include "mozIStorageStatement.h"
#include "mozIStorageAsyncStatement.h"
#include "mozIStoragePendingStatement.h"
#include "mozIStorageConnection.h"
#include "mozIStorageRow.h"
#include "mozIStorageCompletionCallback.h"
#include "mozIStorageStatementCallback.h"

class nsICookiePermission;
class nsIEffectiveTLDService;
class nsIIDNService;
class nsIPrefBranch;
class nsIObserverService;
class nsIURI;
class nsIChannel;
class nsIArray;
class mozIStorageService;
class mozIThirdPartyUtil;
class ReadCookieDBListener;

struct nsCookieAttributes;
struct nsListIter;
struct nsEnumerationData;

namespace mozilla {
namespace net {
class CookieServiceParent;
}
}

// hash entry class
class nsCookieEntry : public PLDHashEntryHdr
{
  public:
    // Hash methods
    typedef const nsCString& KeyType;
    typedef const nsCString* KeyTypePointer;
    typedef nsTArray< nsRefPtr<nsCookie> > ArrayType;
    typedef ArrayType::index_type IndexType;

    explicit
    nsCookieEntry(KeyTypePointer aBaseDomain)
     : mBaseDomain(*aBaseDomain)
    {
    }

    nsCookieEntry(const nsCookieEntry& toCopy)
    {
      // if we end up here, things will break. nsTHashtable shouldn't
      // allow this, since we set ALLOW_MEMMOVE to true.
      NS_NOTREACHED("nsCookieEntry copy constructor is forbidden!");
    }

    ~nsCookieEntry()
    {
    }

    KeyType GetKey() const
    {
      return mBaseDomain;
    }

    bool KeyEquals(KeyTypePointer aKey) const
    {
      return mBaseDomain == *aKey;
    }

    static KeyTypePointer KeyToPointer(KeyType aKey)
    {
      return &aKey;
    }

    static PLDHashNumber HashKey(KeyTypePointer aKey)
    {
      return mozilla::HashString(*aKey);
    }

    enum { ALLOW_MEMMOVE = true };

    inline ArrayType& GetCookies() { return mCookies; }

  private:
    nsCString mBaseDomain;
    ArrayType mCookies;
};

// encapsulates a (baseDomain, nsCookie) tuple for temporary storage purposes.
struct CookieDomainTuple
{
  nsCString baseDomain;
  nsRefPtr<nsCookie> cookie;
};

// encapsulates in-memory and on-disk DB states, so we can
// conveniently switch state when entering or exiting private browsing.
struct DBState
{
  DBState() : cookieCount(0), cookieOldestTime(LL_MAXINT), corruptFlag(OK)
  {
    hostTable.Init();
  }

  NS_INLINE_DECL_REFCOUNTING(DBState)

  // State of the database connection.
  enum CorruptFlag {
    OK,                   // normal
    CLOSING_FOR_REBUILD,  // corruption detected, connection closing
    REBUILDING            // close complete, rebuilding database from memory
  };

  nsTHashtable<nsCookieEntry>     hostTable;
  PRUint32                        cookieCount;
  PRInt64                         cookieOldestTime;
  nsCOMPtr<nsIFile>               cookieFile;
  nsCOMPtr<mozIStorageConnection> dbConn;
  nsCOMPtr<mozIStorageAsyncStatement> stmtInsert;
  nsCOMPtr<mozIStorageAsyncStatement> stmtDelete;
  nsCOMPtr<mozIStorageAsyncStatement> stmtUpdate;
  CorruptFlag                     corruptFlag;

  // Various parts representing asynchronous read state. These are useful
  // while the background read is taking place.
  nsCOMPtr<mozIStorageConnection>       syncConn;
  nsCOMPtr<mozIStorageStatement>        stmtReadDomain;
  nsCOMPtr<mozIStoragePendingStatement> pendingRead;
  // The asynchronous read listener. This is a weak ref (storage has ownership)
  // since it may need to outlive the DBState's database connection.
  ReadCookieDBListener*                 readListener;
  // An array of (baseDomain, cookie) tuples representing data read in
  // asynchronously. This is merged into hostTable once read is complete.
  nsTArray<CookieDomainTuple>           hostArray;
  // A hashset of baseDomains read in synchronously, while the async read is
  // in flight. This is used to keep track of which data in hostArray is stale
  // when the time comes to merge.
  nsTHashtable<nsCStringHashKey>        readSet;

  // DB completion handlers.
  nsCOMPtr<mozIStorageStatementCallback>  insertListener;
  nsCOMPtr<mozIStorageStatementCallback>  updateListener;
  nsCOMPtr<mozIStorageStatementCallback>  removeListener;
  nsCOMPtr<mozIStorageCompletionCallback> closeListener;
};

// these constants represent a decision about a cookie based on user prefs.
enum CookieStatus
{
  STATUS_ACCEPTED,
  STATUS_ACCEPT_SESSION,
  STATUS_REJECTED,
  // STATUS_REJECTED_WITH_ERROR indicates the cookie should be rejected because
  // of an error (rather than something the user can control). this is used for
  // notification purposes, since we only want to notify of rejections where
  // the user can do something about it (e.g. whitelist the site).
  STATUS_REJECTED_WITH_ERROR
};

// Result codes for TryInitDB() and Read().
enum OpenDBResult
{
  RESULT_OK,
  RESULT_RETRY,
  RESULT_FAILURE
};

/******************************************************************************
 * nsCookieService:
 * class declaration
 ******************************************************************************/

class nsCookieService : public nsICookieService
                      , public nsICookieManager2
                      , public nsIObserver
                      , public nsSupportsWeakReference
{
  public:
    // nsISupports
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER
    NS_DECL_NSICOOKIESERVICE
    NS_DECL_NSICOOKIEMANAGER
    NS_DECL_NSICOOKIEMANAGER2

    nsCookieService();
    virtual ~nsCookieService();
    static nsICookieService*      GetXPCOMSingleton();
    nsresult                      Init();

  protected:
    void                          PrefChanged(nsIPrefBranch *aPrefBranch);
    void                          InitDBStates();
    OpenDBResult                  TryInitDB(bool aDeleteExistingDB);
    nsresult                      CreateTable();
    void                          CloseDBStates();
    void                          CloseDefaultDBConnection();
    void                          HandleDBClosed(DBState* aDBState);
    void                          HandleCorruptDB(DBState* aDBState);
    void                          RebuildCorruptDB(DBState* aDBState);
    OpenDBResult                  Read();
    template<class T> nsCookie*   GetCookieFromRow(T &aRow);
    void                          AsyncReadComplete();
    void                          CancelAsyncRead(bool aPurgeReadSet);
    void                          EnsureReadDomain(const nsCString &aBaseDomain);
    void                          EnsureReadComplete();
    nsresult                      NormalizeHost(nsCString &aHost);
    nsresult                      GetBaseDomain(nsIURI *aHostURI, nsCString &aBaseDomain, bool &aRequireHostMatch);
    nsresult                      GetBaseDomainFromHost(const nsACString &aHost, nsCString &aBaseDomain);
    nsresult                      GetCookieStringCommon(nsIURI *aHostURI, nsIChannel *aChannel, bool aHttpBound, char** aCookie);
    void                          GetCookieStringInternal(nsIURI *aHostURI, bool aIsForeign, bool aHttpBound, nsCString &aCookie);
    nsresult                      SetCookieStringCommon(nsIURI *aHostURI, const char *aCookieHeader, const char *aServerTime, nsIChannel *aChannel, bool aFromHttp);
    void                          SetCookieStringInternal(nsIURI *aHostURI, bool aIsForeign, nsDependentCString &aCookieHeader, const nsCString &aServerTime, bool aFromHttp);
    bool                          SetCookieInternal(nsIURI *aHostURI, const nsCString& aBaseDomain, bool aRequireHostMatch, CookieStatus aStatus, nsDependentCString &aCookieHeader, PRInt64 aServerTime, bool aFromHttp);
    void                          AddInternal(const nsCString& aBaseDomain, nsCookie *aCookie, PRInt64 aCurrentTimeInUsec, nsIURI *aHostURI, const char *aCookieHeader, bool aFromHttp);
    void                          RemoveCookieFromList(const nsListIter &aIter, mozIStorageBindingParamsArray *aParamsArray = NULL);
    void                          AddCookieToList(const nsCString& aBaseDomain, nsCookie *aCookie, DBState *aDBState, mozIStorageBindingParamsArray *aParamsArray, bool aWriteToDB = true);
    void                          UpdateCookieInList(nsCookie *aCookie, PRInt64 aLastAccessed, mozIStorageBindingParamsArray *aParamsArray);
    static bool                   GetTokenValue(nsASingleFragmentCString::const_char_iterator &aIter, nsASingleFragmentCString::const_char_iterator &aEndIter, nsDependentCSubstring &aTokenString, nsDependentCSubstring &aTokenValue, bool &aEqualsFound);
    static bool                   ParseAttributes(nsDependentCString &aCookieHeader, nsCookieAttributes &aCookie);
    bool                          RequireThirdPartyCheck();
    CookieStatus                  CheckPrefs(nsIURI *aHostURI, bool aIsForeign, const nsCString &aBaseDomain, bool aRequireHostMatch, const char *aCookieHeader);
    bool                          CheckDomain(nsCookieAttributes &aCookie, nsIURI *aHostURI, const nsCString &aBaseDomain, bool aRequireHostMatch);
    static bool                   CheckPath(nsCookieAttributes &aCookie, nsIURI *aHostURI);
    static bool                   GetExpiry(nsCookieAttributes &aCookie, PRInt64 aServerTime, PRInt64 aCurrentTime);
    void                          RemoveAllFromMemory();
    already_AddRefed<nsIArray>    PurgeCookies(PRInt64 aCurrentTimeInUsec);
    bool                          FindCookie(const nsCString& aBaseDomain, const nsAFlatCString &aHost, const nsAFlatCString &aName, const nsAFlatCString &aPath, nsListIter &aIter);
    static void                   FindStaleCookie(nsCookieEntry *aEntry, PRInt64 aCurrentTime, nsListIter &aIter);
    void                          NotifyRejected(nsIURI *aHostURI);
    void                          NotifyChanged(nsISupports *aSubject, const PRUnichar *aData);
    void                          NotifyPurged(nsICookie2* aCookie);
    already_AddRefed<nsIArray>    CreatePurgeList(nsICookie2* aCookie);

  protected:
    // cached members.
    nsCOMPtr<nsIObserverService>     mObserverService;
    nsCOMPtr<nsICookiePermission>    mPermissionService;
    nsCOMPtr<mozIThirdPartyUtil>     mThirdPartyUtil;
    nsCOMPtr<nsIEffectiveTLDService> mTLDService;
    nsCOMPtr<nsIIDNService>          mIDNService;
    nsCOMPtr<mozIStorageService>     mStorageService;

    // we have two separate DB states: one for normal browsing and one for
    // private browsing, switching between them as appropriate. this state
    // encapsulates both the in-memory table and the on-disk DB.
    // note that the private states' dbConn should always be null - we never
    // want to be dealing with the on-disk DB when in private browsing.
    DBState                      *mDBState;
    nsRefPtr<DBState>             mDefaultDBState;
    nsRefPtr<DBState>             mPrivateDBState;

    // cached prefs
    PRUint8                       mCookieBehavior; // BEHAVIOR_{ACCEPT, REJECTFOREIGN, REJECT}
    bool                          mThirdPartySession;
    PRUint16                      mMaxNumberOfCookies;
    PRUint16                      mMaxCookiesPerHost;
    PRInt64                       mCookiePurgeAge;

    // friends!
    friend PLDHashOperator purgeCookiesCallback(nsCookieEntry *aEntry, void *aArg);
    friend class DBListenerErrorHandler;
    friend class ReadCookieDBListener;
    friend class CloseCookieDBListener;

    static nsCookieService*       GetSingleton();
    friend class mozilla::net::CookieServiceParent;
};

#endif // nsCookieService_h__
