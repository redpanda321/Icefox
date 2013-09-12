/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsGlobalWindow_h___
#define nsGlobalWindow_h___

#include "mozilla/XPCOM.h" // for TimeStamp/TimeDuration

// Local Includes
// Helper Classes
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsWeakReference.h"
#include "nsHashtable.h"
#include "nsDataHashtable.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDOMScriptObjectHolder.h"

// Interfaces Needed
#include "nsDOMWindowList.h"
#include "nsIBaseWindow.h"
#include "nsIBrowserDOMWindow.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDOMEventTarget.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIDOMJSWindow.h"
#include "nsIDOMChromeWindow.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptContext.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIScriptTimeoutHandler.h"
#include "nsITimer.h"
#include "nsIWebBrowserChrome.h"
#include "nsPIDOMWindow.h"
#include "nsIDOMModalContentWindow.h"
#include "nsIScriptSecurityManager.h"
#include "nsEventListenerManager.h"
#include "nsIDOMDocument.h"
#ifndef MOZ_DISABLE_DOMCRYPTO
#include "nsIDOMCrypto.h"
#endif
#include "nsIPrincipal.h"
#include "nsIXPCScriptable.h"
#include "nsPoint.h"
#include "nsSize.h"
#include "nsRect.h"
#include "mozFlushType.h"
#include "prclist.h"
#include "nsIDOMStorageObsolete.h"
#include "nsIDOMStorageEvent.h"
#include "nsIDOMStorageIndexedDB.h"
#include "nsIDOMOfflineResourceList.h"
#include "nsIArray.h"
#include "nsIContent.h"
#include "nsIIDBFactory.h"
#include "nsFrameMessageManager.h"
#include "mozilla/LinkedList.h"
#include "mozilla/TimeStamp.h"
#include "nsIDOMTouchEvent.h"
#include "nsIInlineEventHandlers.h"
#include "nsWrapperCacheInlines.h"
#include "nsIIdleObserver.h"
#include "nsIDOMWakeLock.h"

// JS includes
#include "jsapi.h"

#ifdef MOZ_B2G
#include "nsIDOMWindowB2G.h"
#endif // MOZ_B2G

#define DEFAULT_HOME_PAGE "www.mozilla.org"
#define PREF_BROWSER_STARTUP_HOMEPAGE "browser.startup.homepage"

// Amount of time allowed between alert/prompt/confirm before enabling
// the stop dialog checkbox.
#define DEFAULT_SUCCESSIVE_DIALOG_TIME_LIMIT 3 // 3 sec

// Maximum number of successive dialogs before we prompt users to disable
// dialogs for this window.
#define MAX_SUCCESSIVE_DIALOG_COUNT 5

// Idle fuzz time upper limit
#define MAX_IDLE_FUZZ_TIME_MS 90000

// Min idle notification time in seconds.
#define MIN_IDLE_NOTIFICATION_TIME_S 1

class nsIDOMBarProp;
class nsIDocument;
class nsPresContext;
class nsIDOMEvent;
class nsIScrollableFrame;
class nsIControllers;

class nsBarProp;
class nsLocation;
class nsScreen;
class nsHistory;
class nsPerformance;
class nsIDocShellLoadInfo;
class WindowStateHolder;
class nsGlobalWindowObserver;
class nsGlobalWindow;
class PostMessageEvent;
class nsRunnable;
class nsDOMEventTargetHelper;
class nsDOMOfflineResourceList;
class nsDOMWindowUtils;
class nsIIdleService;

#ifdef MOZ_DISABLE_DOMCRYPTO
class nsIDOMCrypto;
#endif

class nsWindowSizes;

namespace mozilla {
namespace dom {
class Navigator;
class URL;
} // namespace dom
} // namespace mozilla

extern nsresult
NS_CreateJSTimeoutHandler(nsGlobalWindow *aWindow,
                          bool *aIsInterval,
                          int32_t *aInterval,
                          nsIScriptTimeoutHandler **aRet);

/*
 * Timeout struct that holds information about each script
 * timeout.  Holds a strong reference to an nsIScriptTimeoutHandler, which
 * abstracts the language specific cruft.
 */
struct nsTimeout : mozilla::LinkedListElement<nsTimeout>
{
  nsTimeout();
  ~nsTimeout();

  NS_DECL_CYCLE_COLLECTION_LEGACY_NATIVE_CLASS(nsTimeout)

  nsrefcnt Release();
  nsrefcnt AddRef();

  nsresult InitTimer(nsTimerCallbackFunc aFunc, uint64_t delay) {
    return mTimer->InitWithFuncCallback(aFunc, this, delay,
                                        nsITimer::TYPE_ONE_SHOT);
  }

  // Window for which this timeout fires
  nsRefPtr<nsGlobalWindow> mWindow;

  // The actual timer object
  nsCOMPtr<nsITimer> mTimer;

  // True if the timeout was cleared
  bool mCleared;

  // True if this is one of the timeouts that are currently running
  bool mRunning;

  // True if this is a repeating/interval timer
  bool mIsInterval;

  // Returned as value of setTimeout()
  uint32_t mPublicId;

  // Interval in milliseconds
  uint32_t mInterval;

  // mWhen and mTimeRemaining can't be in a union, sadly, because they
  // have constructors.
  // Nominal time to run this timeout.  Use only when timeouts are not
  // suspended.
  mozilla::TimeStamp mWhen;
  // Remaining time to wait.  Used only when timeouts are suspended.
  mozilla::TimeDuration mTimeRemaining;

  // Principal with which to execute
  nsCOMPtr<nsIPrincipal> mPrincipal;

  // stack depth at which timeout is firing
  uint32_t mFiringDepth;

  // 
  uint32_t mNestingLevel;

  // The popup state at timeout creation time if not created from
  // another timeout
  PopupControlState mPopupState;

  // The language-specific information about the callback.
  nsCOMPtr<nsIScriptTimeoutHandler> mScriptHandler;

private:
  // reference count for shared usage
  nsAutoRefCnt mRefCnt;
};

struct IdleObserverHolder
{
  nsCOMPtr<nsIIdleObserver> mIdleObserver;
  uint32_t mTimeInS;
  bool mPrevNotificationIdle;

  IdleObserverHolder()
    : mTimeInS(0), mPrevNotificationIdle(false)
  {
    MOZ_COUNT_CTOR(IdleObserverHolder);
  }

  IdleObserverHolder(const IdleObserverHolder& aOther)
    : mIdleObserver(aOther.mIdleObserver), mTimeInS(aOther.mTimeInS),
      mPrevNotificationIdle(aOther.mPrevNotificationIdle)
  {
    MOZ_COUNT_CTOR(IdleObserverHolder);
  }

  bool operator==(const IdleObserverHolder& aOther) const {
    return
      mIdleObserver == aOther.mIdleObserver &&
      mTimeInS == aOther.mTimeInS;
  }

  ~IdleObserverHolder()
  {
    MOZ_COUNT_DTOR(IdleObserverHolder);
  }
};

//*****************************************************************************
// nsGlobalWindow: Global Object for Scripting
//*****************************************************************************
// Beware that all scriptable interfaces implemented by
// nsGlobalWindow will be reachable from JS, if you make this class
// implement new interfaces you better know what you're
// doing. Security wise this is very sensitive code. --
// jst@netscape.com

// nsGlobalWindow inherits PRCList for maintaining a list of all inner
// windows still in memory for any given outer window. This list is
// needed to ensure that mOuterWindow doesn't end up dangling. The
// nature of PRCList means that the window itself is always in the
// list, and an outer window's list will also contain all inner window
// objects that are still in memory (and in reality all inner window
// object's lists also contain its outer and all other inner windows
// belonging to the same outer window, but that's an unimportant
// side effect of inheriting PRCList).

class nsGlobalWindow : public nsPIDOMWindow,
                       public nsIScriptGlobalObject,
                       public nsIDOMJSWindow,
                       public nsIScriptObjectPrincipal,
                       public nsIDOMEventTarget,
                       public nsIDOMStorageIndexedDB,
                       public nsSupportsWeakReference,
                       public nsIInterfaceRequestor,
                       public nsWrapperCache,
                       public PRCListStr,
                       public nsIDOMWindowPerformance,
                       public nsITouchEventReceiver,
                       public nsIInlineEventHandlers,
                       public nsIWindowCrypto
#ifdef MOZ_B2G
                     , public nsIDOMWindowB2G
#endif // MOZ_B2G
{
public:
  typedef mozilla::TimeStamp TimeStamp;
  typedef mozilla::TimeDuration TimeDuration;
  typedef mozilla::dom::Navigator Navigator;
  typedef nsDataHashtable<nsUint64HashKey, nsGlobalWindow*> WindowByIdTable;

  // public methods
  nsPIDOMWindow* GetPrivateParent();
  // callback for close event
  void ReallyCloseWindow();

  // nsISupports
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  // nsWrapperCache
  JSObject *WrapObject(JSContext *cx, JSObject *scope, bool *triedToWrap)
  {
    NS_ASSERTION(IsOuterWindow(),
                 "Inner window supports nsWrapperCache, fix WrapObject!");
    *triedToWrap = true;
    return EnsureInnerWindow() ? GetWrapper() : nullptr;
  }

  // nsIScriptGlobalObject
  virtual nsIScriptContext *GetContext();
  virtual JSObject *GetGlobalJSObject();
  JSObject *FastGetGlobalJSObject()
  {
    return mJSObject;
  }

  virtual nsresult EnsureScriptEnvironment();

  virtual nsIScriptContext *GetScriptContext();

  virtual void OnFinalize(JSObject* aObject);
  virtual void SetScriptsEnabled(bool aEnabled, bool aFireTimeouts);

  virtual bool IsBlackForCC();

  // nsIScriptObjectPrincipal
  virtual nsIPrincipal* GetPrincipal();

  // nsIDOMWindow
  NS_DECL_NSIDOMWINDOW

#ifdef MOZ_B2G
  // nsIDOMWindowB2G
  NS_DECL_NSIDOMWINDOWB2G
#endif // MOZ_B2G

  // nsIDOMWindowPerformance
  NS_DECL_NSIDOMWINDOWPERFORMANCE

  // nsIDOMJSWindow
  NS_DECL_NSIDOMJSWINDOW

  // nsIDOMEventTarget
  NS_DECL_NSIDOMEVENTTARGET

  // nsITouchEventReceiver
  NS_DECL_NSITOUCHEVENTRECEIVER

  // nsIInlineEventHandlers
  NS_DECL_NSIINLINEEVENTHANDLERS

  // nsIWindowCrypto
  NS_DECL_NSIWINDOWCRYPTO

  // nsPIDOMWindow
  virtual NS_HIDDEN_(nsPIDOMWindow*) GetPrivateRoot();
  virtual NS_HIDDEN_(void) ActivateOrDeactivate(bool aActivate);
  virtual NS_HIDDEN_(void) SetActive(bool aActive);
  virtual NS_HIDDEN_(void) SetIsBackground(bool aIsBackground);
  virtual NS_HIDDEN_(void) SetChromeEventHandler(nsIDOMEventTarget* aChromeEventHandler);

  virtual NS_HIDDEN_(void) SetInitialPrincipalToSubject();

  virtual NS_HIDDEN_(PopupControlState) PushPopupControlState(PopupControlState state, bool aForce) const;
  virtual NS_HIDDEN_(void) PopPopupControlState(PopupControlState state) const;
  virtual NS_HIDDEN_(PopupControlState) GetPopupControlState() const;

  virtual already_AddRefed<nsISupports> SaveWindowState();
  virtual NS_HIDDEN_(nsresult) RestoreWindowState(nsISupports *aState);
  virtual NS_HIDDEN_(void) SuspendTimeouts(uint32_t aIncrease = 1,
                                           bool aFreezeChildren = true);
  virtual NS_HIDDEN_(nsresult) ResumeTimeouts(bool aThawChildren = true);
  virtual NS_HIDDEN_(uint32_t) TimeoutSuspendCount();
  virtual NS_HIDDEN_(nsresult) FireDelayedDOMEvents();
  virtual NS_HIDDEN_(bool) IsFrozen() const
  {
    return mIsFrozen;
  }

  virtual NS_HIDDEN_(bool) WouldReuseInnerWindow(nsIDocument *aNewDocument);

  virtual NS_HIDDEN_(void) SetDocShell(nsIDocShell* aDocShell);
  virtual void DetachFromDocShell();
  virtual NS_HIDDEN_(nsresult) SetNewDocument(nsIDocument *aDocument,
                                              nsISupports *aState,
                                              bool aForceReuseInnerWindow);
  void DispatchDOMWindowCreated();
  virtual NS_HIDDEN_(void) SetOpenerWindow(nsIDOMWindow* aOpener,
                                           bool aOriginalOpener);
  virtual NS_HIDDEN_(void) EnsureSizeUpToDate();

  virtual NS_HIDDEN_(nsIDOMWindow*) EnterModalState();
  virtual NS_HIDDEN_(void) LeaveModalState(nsIDOMWindow* aWindow);

  virtual NS_HIDDEN_(bool) CanClose();
  virtual NS_HIDDEN_(nsresult) ForceClose();

  virtual NS_HIDDEN_(void) MaybeUpdateTouchState();
  virtual NS_HIDDEN_(void) UpdateTouchState();
  virtual NS_HIDDEN_(bool) DispatchCustomEvent(const char *aEventName);
  virtual NS_HIDDEN_(void) RefreshCompartmentPrincipal();
  virtual NS_HIDDEN_(nsresult) SetFullScreenInternal(bool aIsFullScreen, bool aRequireTrust);

  // nsIDOMStorageIndexedDB
  NS_DECL_NSIDOMSTORAGEINDEXEDDB

  // nsIInterfaceRequestor
  NS_DECL_NSIINTERFACEREQUESTOR

  // WebIDL interface.
  uint32_t GetLength();
  already_AddRefed<nsIDOMWindow> IndexedGetter(uint32_t aIndex, bool& aFound);

  // Object Management
  nsGlobalWindow(nsGlobalWindow *aOuterWindow);

  static nsGlobalWindow *FromSupports(nsISupports *supports)
  {
    // Make sure this matches the casts we do in QueryInterface().
    return (nsGlobalWindow *)(nsIScriptGlobalObject *)supports;
  }
  static nsISupports *ToSupports(nsGlobalWindow *win)
  {
    // Make sure this matches the casts we do in QueryInterface().
    return (nsISupports *)(nsIScriptGlobalObject *)win;
  }
  static nsGlobalWindow *FromWrapper(nsIXPConnectWrappedNative *wrapper)
  {
    return FromSupports(wrapper->Native());
  }

  /**
   * Wrap nsIDOMWindow::GetTop so we can overload the inline GetTop()
   * implementation below.  (nsIDOMWindow::GetTop simply calls
   * nsIDOMWindow::GetRealTop().)
   */
  nsresult GetTop(nsIDOMWindow **aWindow)
  {
    return nsIDOMWindow::GetTop(aWindow);
  }

  inline nsGlobalWindow *GetTop()
  {
    nsCOMPtr<nsIDOMWindow> top;
    GetTop(getter_AddRefs(top));
    if (top)
      return static_cast<nsGlobalWindow *>(top.get());
    return nullptr;
  }

  inline nsGlobalWindow* GetScriptableTop()
  {
    nsCOMPtr<nsIDOMWindow> top;
    GetScriptableTop(getter_AddRefs(top));
    if (top) {
      return static_cast<nsGlobalWindow *>(top.get());
    }
    return nullptr;
  }

  // Returns true if dialogs need to be prevented from appearings for this
  // window. beingAbused returns whether dialogs are being abused.
  bool DialogsAreBlocked(bool *aBeingAbused);

  // Returns true if we've reached the state in this top level window where we
  // ask the user if further dialogs should be blocked. This method must only
  // be called on the scriptable top inner window.
  bool DialogsAreBeingAbused();

  // Ask the user if further dialogs should be blocked, if dialogs are currently
  // being abused. This is used in the cases where we have no modifiable UI to
  // show, in that case we show a separate dialog to ask this question.
  bool ConfirmDialogIfNeeded();

  // Prevent further dialogs in this (top level) window
  void PreventFurtherDialogs(bool aPermanent);

  virtual void SetHasAudioAvailableEventListeners();

  nsIScriptContext *GetContextInternal()
  {
    if (mOuterWindow) {
      return GetOuterWindowInternal()->mContext;
    }

    return mContext;
  }

  nsIScriptContext *GetScriptContextInternal(uint32_t aLangID)
  {
    NS_ASSERTION(aLangID == nsIProgrammingLanguage::JAVASCRIPT,
                 "We don't support this language ID");
    if (mOuterWindow) {
      return GetOuterWindowInternal()->mContext;
    }

    return mContext;
  }

  nsGlobalWindow *GetOuterWindowInternal()
  {
    return static_cast<nsGlobalWindow *>(GetOuterWindow());
  }

  nsGlobalWindow *GetCurrentInnerWindowInternal()
  {
    return static_cast<nsGlobalWindow *>(mInnerWindow);
  }

  nsGlobalWindow *EnsureInnerWindowInternal()
  {
    return static_cast<nsGlobalWindow *>(EnsureInnerWindow());
  }

  bool IsCreatingInnerWindow() const
  {
    return  mCreatingInnerWindow;
  }

  bool IsChromeWindow() const
  {
    return mIsChrome;
  }

  // GetScrollFrame does not flush.  Callers should do it themselves as needed,
  // depending on which info they actually want off the scrollable frame.
  nsIScrollableFrame *GetScrollFrame();

  nsresult Observe(nsISupports* aSubject, const char* aTopic,
                   const PRUnichar* aData);

  static void Init();
  static void ShutDown();
  static void CleanupCachedXBLHandlers(nsGlobalWindow* aWindow);
  static bool IsCallerChrome();
  static void CloseBlockScriptTerminationFunc(nsISupports *aRef);

  static void RunPendingTimeoutsRecursive(nsGlobalWindow *aTopWindow,
                                          nsGlobalWindow *aWindow);

  friend class WindowStateHolder;

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS_AMBIGUOUS(nsGlobalWindow,
                                                                   nsIScriptGlobalObject)

  virtual NS_HIDDEN_(JSObject*)
    GetCachedXBLPrototypeHandler(nsXBLPrototypeHandler* aKey);

  virtual NS_HIDDEN_(void)
    CacheXBLPrototypeHandler(nsXBLPrototypeHandler* aKey,
                             nsScriptObjectHolder<JSObject>& aHandler);

  virtual bool TakeFocus(bool aFocus, uint32_t aFocusMethod);
  virtual void SetReadyForFocus();
  virtual void PageHidden();
  virtual nsresult DispatchAsyncHashchange(nsIURI *aOldURI, nsIURI *aNewURI);
  virtual nsresult DispatchSyncPopState();

  virtual void EnableDeviceSensor(uint32_t aType);
  virtual void DisableDeviceSensor(uint32_t aType);

  virtual void EnableTimeChangeNotifications();
  virtual void DisableTimeChangeNotifications();

#ifdef MOZ_B2G
  virtual void EnableNetworkEvent(uint32_t aType);
  virtual void DisableNetworkEvent(uint32_t aType);
#endif // MOZ_B2G

  virtual nsresult SetArguments(nsIArray *aArguments, nsIPrincipal *aOrigin);

  static bool DOMWindowDumpEnabled();

  void MaybeForgiveSpamCount();
  bool IsClosedOrClosing() {
    return (mIsClosed ||
            mInClose ||
            mHavePendingClose ||
            mCleanedUp);
  }

  static void FirePopupBlockedEvent(nsIDOMDocument* aDoc,
                                    nsIDOMWindow *aRequestingWindow, nsIURI *aPopupURI,
                                    const nsAString &aPopupWindowName,
                                    const nsAString &aPopupWindowFeatures);

  virtual uint32_t GetSerial() {
    return mSerial;
  }

  static nsGlobalWindow* GetOuterWindowWithId(uint64_t aWindowID) {
    if (!sWindowsById) {
      return nullptr;
    }

    nsGlobalWindow* outerWindow = sWindowsById->Get(aWindowID);
    return outerWindow && !outerWindow->IsInnerWindow() ? outerWindow : nullptr;
  }

  static nsGlobalWindow* GetInnerWindowWithId(uint64_t aInnerWindowID) {
    if (!sWindowsById) {
      return nullptr;
    }

    nsGlobalWindow* innerWindow = sWindowsById->Get(aInnerWindowID);
    return innerWindow && innerWindow->IsInnerWindow() ? innerWindow : nullptr;
  }

  static bool HasIndexedDBSupport();

  static bool HasPerformanceSupport();

  static WindowByIdTable* GetWindowsTable() {
    return sWindowsById;
  }

  void SizeOfIncludingThis(nsWindowSizes* aWindowSizes) const;

  void UnmarkGrayTimers();

  void AddEventTargetObject(nsDOMEventTargetHelper* aObject);
  void RemoveEventTargetObject(nsDOMEventTargetHelper* aObject);

  void NotifyIdleObserver(IdleObserverHolder* aIdleObserverHolder,
                          bool aCallOnidle);
  nsresult HandleIdleActiveEvent();
  bool ContainsIdleObserver(nsIIdleObserver* aIdleObserver, uint32_t timeInS);
  void HandleIdleObserverCallback();

  void AllowScriptsToClose()
  {
    mAllowScriptsToClose = true;
  }

#define EVENT(name_, id_, type_, struct_)                                     \
  mozilla::dom::EventHandlerNonNull* GetOn##name_()                           \
  {                                                                           \
    nsEventListenerManager *elm = GetListenerManager(false);                  \
    return elm ? elm->GetEventHandler(nsGkAtoms::on##name_) : nullptr;        \
  }                                                                           \
  void SetOn##name_(mozilla::dom::EventHandlerNonNull* handler,               \
                    mozilla::ErrorResult& error)                              \
  {                                                                           \
    nsEventListenerManager *elm = GetListenerManager(true);                   \
    if (elm) {                                                                \
      error = elm->SetEventHandler(nsGkAtoms::on##name_, handler);            \
    } else {                                                                  \
      error.Throw(NS_ERROR_OUT_OF_MEMORY);                                    \
    }                                                                         \
  }
#define ERROR_EVENT(name_, id_, type_, struct_)                               \
  mozilla::dom::OnErrorEventHandlerNonNull* GetOn##name_()                    \
  {                                                                           \
    nsEventListenerManager *elm = GetListenerManager(false);                  \
    return elm ? elm->GetOnErrorEventHandler() : nullptr;                     \
  }                                                                           \
  void SetOn##name_(mozilla::dom::OnErrorEventHandlerNonNull* handler,        \
                    mozilla::ErrorResult& error)                              \
  {                                                                           \
    nsEventListenerManager *elm = GetListenerManager(true);                   \
    if (elm) {                                                                \
      error = elm->SetEventHandler(handler);                                  \
    } else {                                                                  \
      error.Throw(NS_ERROR_OUT_OF_MEMORY);                                    \
    }                                                                         \
  }
#define BEFOREUNLOAD_EVENT(name_, id_, type_, struct_)                        \
  mozilla::dom::BeforeUnloadEventHandlerNonNull* GetOn##name_()               \
  {                                                                           \
    nsEventListenerManager *elm = GetListenerManager(false);                  \
    return elm ? elm->GetOnBeforeUnloadEventHandler() : nullptr;              \
  }                                                                           \
  void SetOn##name_(mozilla::dom::BeforeUnloadEventHandlerNonNull* handler,   \
                    mozilla::ErrorResult& error)                              \
  {                                                                           \
    nsEventListenerManager *elm = GetListenerManager(true);                   \
    if (elm) {                                                                \
      error = elm->SetEventHandler(handler);                                  \
    } else {                                                                  \
      error.Throw(NS_ERROR_OUT_OF_MEMORY);                                    \
    }                                                                         \
  }
#define WINDOW_ONLY_EVENT EVENT
#define TOUCH_EVENT EVENT
#include "nsEventNameList.h"
#undef TOUCH_EVENT
#undef WINDOW_ONLY_EVENT
#undef BEFOREUNLOAD_EVENT
#undef ERROR_EVENT
#undef EVENT

protected:
  // Array of idle observers that are notified of idle events.
  nsTObserverArray<IdleObserverHolder> mIdleObservers;

  // Idle timer used for function callbacks to notify idle observers.
  nsCOMPtr<nsITimer> mIdleTimer;

  // Idle fuzz time added to idle timer callbacks.
  uint32_t mIdleFuzzFactor;

  // Index in mArrayIdleObservers
  // Next idle observer to notify user idle status
  int32_t mIdleCallbackIndex;

  // If false then the topic is "active"
  // If true then the topic is "idle"
  bool mCurrentlyIdle;

  // Set to true when a fuzz time needs to be applied
  // to active notifications to the idle observer.
  bool mAddActiveEventFuzzTime;

  nsCOMPtr <nsIIdleService> mIdleService;

  nsCOMPtr <nsIDOMMozWakeLock> mWakeLock;

  static bool sIdleObserversAPIFuzzTimeDisabled;

  friend class HashchangeCallback;
  friend class nsBarProp;

  // Object Management
  virtual ~nsGlobalWindow();
  void CleanUp(bool aIgnoreModalDialog);
  void ClearControllers();
  nsresult FinalClose();

  inline void MaybeClearInnerWindow(nsGlobalWindow* aExpectedInner)
  {
    if(mInnerWindow == aExpectedInner) {
      mInnerWindow = nullptr;
    }
  }

  void FreeInnerObjects();
  JSObject *CallerGlobal();
  nsGlobalWindow *CallerInnerWindow();

  // Only to be called on an inner window.
  // aDocument must not be null.
  void InnerSetNewDocument(nsIDocument* aDocument);

  nsresult DefineArgumentsProperty(nsIArray *aArguments);

  // Get the parent, returns null if this is a toplevel window
  nsIDOMWindow* GetParentInternal();

  // popup tracking
  bool IsPopupSpamWindow()
  {
    if (IsInnerWindow() && !mOuterWindow) {
      return false;
    }

    return GetOuterWindowInternal()->mIsPopupSpam;
  }

  void SetPopupSpamWindow(bool aPopup)
  {
    if (IsInnerWindow() && !mOuterWindow) {
      NS_ERROR("SetPopupSpamWindow() called on inner window w/o an outer!");

      return;
    }

    GetOuterWindowInternal()->mIsPopupSpam = aPopup;
  }

  // Window Control Functions

  virtual nsresult
  OpenNoNavigate(const nsAString& aUrl,
                 const nsAString& aName,
                 const nsAString& aOptions,
                 nsIDOMWindow **_retval);

  /**
   * @param aUrl the URL we intend to load into the window.  If aNavigate is
   *        true, we'll actually load this URL into the window. Otherwise,
   *        aUrl is advisory; OpenInternal will not load the URL into the
   *        new window.
   *
   * @param aName the name to use for the new window
   *
   * @param aOptions the window options to use for the new window
   *
   * @param aDialog true when called from variants of OpenDialog.  If this is
   *        true, this method will skip popup blocking checks.  The aDialog
   *        argument is passed on to the window watcher.
   *
   * @param aCalledNoScript true when called via the [noscript] open()
   *        and openDialog() methods.  When this is true, we do NOT want to use
   *        the JS stack for things like caller determination.
   *
   * @param aDoJSFixups true when this is the content-accessible JS version of
   *        window opening.  When true, popups do not cause us to throw, we save
   *        the caller's principal in the new window for later consumption, and
   *        we make sure that there is a document in the newly-opened window.
   *        Note that this last will only be done if the newly-opened window is
   *        non-chrome.
   *
   * @param aNavigate true if we should navigate to the provided URL, false
   *        otherwise.  When aNavigate is false, we also skip our can-load
   *        security check, on the assumption that whoever *actually* loads this
   *        page will do their own security check.
   *
   * @param argv The arguments to pass to the new window.  The first
   *        three args, if present, will be aUrl, aName, and aOptions.  So this
   *        param only matters if there are more than 3 arguments.
   *
   * @param argc The number of arguments in argv.
   *
   * @param aExtraArgument Another way to pass arguments in.  This is mutually
   *        exclusive with the argv/argc approach.
   *
   * @param aJSCallerContext The calling script's context. This must be null
   *        when aCalledNoScript is true.
   *
   * @param aReturn [out] The window that was opened, if any.
   */
  NS_HIDDEN_(nsresult) OpenInternal(const nsAString& aUrl,
                                    const nsAString& aName,
                                    const nsAString& aOptions,
                                    bool aDialog,
                                    bool aContentModal,
                                    bool aCalledNoScript,
                                    bool aDoJSFixups,
                                    bool aNavigate,
                                    nsIArray *argv,
                                    nsISupports *aExtraArgument,
                                    nsIPrincipal *aCalleePrincipal,
                                    JSContext *aJSCallerContext,
                                    nsIDOMWindow **aReturn);

  static void CloseWindow(nsISupports* aWindow);

  // Timeout Functions
  // Language agnostic timeout function (all args passed).
  // |interval| is in milliseconds.
  nsresult SetTimeoutOrInterval(nsIScriptTimeoutHandler *aHandler,
                                int32_t interval,
                                bool aIsInterval, int32_t *aReturn);
  nsresult ClearTimeoutOrInterval(int32_t aTimerID);

  // JS specific timeout functions (JS args grabbed from context).
  nsresult SetTimeoutOrInterval(bool aIsInterval, int32_t* aReturn);
  nsresult ResetTimersForNonBackgroundWindow();

  // The timeout implementation functions.
  void RunTimeout(nsTimeout *aTimeout);
  void RunTimeout() { RunTimeout(nullptr); }
  // Return true if |aTimeout| was cleared while its handler ran.
  bool RunTimeoutHandler(nsTimeout* aTimeout, nsIScriptContext* aScx);
  // Return true if |aTimeout| needs to be reinserted into the timeout list.
  bool RescheduleTimeout(nsTimeout* aTimeout, const TimeStamp& now,
                         bool aRunningPendingTimeouts);

  void ClearAllTimeouts();
  // Insert aTimeout into the list, before all timeouts that would
  // fire after it, but no earlier than mTimeoutInsertionPoint, if any.
  void InsertTimeoutIntoList(nsTimeout *aTimeout);
  static void TimerCallback(nsITimer *aTimer, void *aClosure);

  // Helper Functions
  nsresult GetTreeOwner(nsIDocShellTreeOwner** aTreeOwner);
  nsresult GetTreeOwner(nsIBaseWindow** aTreeOwner);
  nsresult GetWebBrowserChrome(nsIWebBrowserChrome** aBrowserChrome);
  nsresult SecurityCheckURL(const char *aURL);
  nsresult BuildURIfromBase(const char *aURL,
                            nsIURI **aBuiltURI,
                            bool *aFreeSecurityPass, JSContext **aCXused);
  bool PopupWhitelisted();
  PopupControlState RevisePopupAbuseLevel(PopupControlState);
  void     FireAbuseEvents(bool aBlocked, bool aWindow,
                           const nsAString &aPopupURL,
                           const nsAString &aPopupWindowName,
                           const nsAString &aPopupWindowFeatures);
  void FireOfflineStatusEvent();

  nsresult ScheduleNextIdleObserverCallback();
  uint32_t GetFuzzTimeMS();
  nsresult ScheduleActiveTimerCallback();
  uint32_t FindInsertionIndex(IdleObserverHolder* aIdleObserver);
  virtual nsresult RegisterIdleObserver(nsIIdleObserver* aIdleObserverPtr);
  nsresult FindIndexOfElementToRemove(nsIIdleObserver* aIdleObserver,
                                      int32_t* aRemoveElementIndex);
  virtual nsresult UnregisterIdleObserver(nsIIdleObserver* aIdleObserverPtr);

  nsresult FireHashchange(const nsAString &aOldURL, const nsAString &aNewURL);

  void FlushPendingNotifications(mozFlushType aType);
  void EnsureReflowFlushAndPaint();
  nsresult CheckSecurityWidthAndHeight(int32_t* width, int32_t* height);
  nsresult CheckSecurityLeftAndTop(int32_t* left, int32_t* top);

  // Arguments to this function should have values in app units
  nsresult SetCSSViewportWidthAndHeight(nscoord width, nscoord height);
  // Arguments to this function should have values in device pixels
  nsresult SetDocShellWidthAndHeight(int32_t width, int32_t height);

  static bool CanSetProperty(const char *aPrefName);

  static void MakeScriptDialogTitle(nsAString &aOutTitle);

  bool CanMoveResizeWindows();

  bool     GetBlurSuppression();

  // If aDoFlush is true, we'll flush our own layout; otherwise we'll try to
  // just flush our parent and only flush ourselves if we think we need to.
  nsresult GetScrollXY(int32_t* aScrollX, int32_t* aScrollY,
                       bool aDoFlush);
  nsresult GetScrollMaxXY(int32_t* aScrollMaxX, int32_t* aScrollMaxY);

  nsresult GetOuterSize(nsIntSize* aSizeCSSPixels);
  nsresult SetOuterSize(int32_t aLengthCSSPixels, bool aIsWidth);
  nsRect GetInnerScreenRect();

  bool IsFrame()
  {
    return GetParentInternal() != nullptr;
  }

  // If aLookForCallerOnJSStack is true, this method will look at the JS stack
  // to determine who the caller is.  If it's false, it'll use |this| as the
  // caller.
  bool WindowExists(const nsAString& aName, bool aLookForCallerOnJSStack);

  already_AddRefed<nsIWidget> GetMainWidget();
  nsIWidget* GetNearestWidget();

  void Freeze()
  {
    NS_ASSERTION(!IsFrozen(), "Double-freezing?");
    mIsFrozen = true;
    NotifyDOMWindowFrozen(this);
  }

  void Thaw()
  {
    mIsFrozen = false;
    NotifyDOMWindowThawed(this);
  }

  bool IsInModalState();

  // Convenience functions for the many methods that need to scale
  // from device to CSS pixels or vice versa.  Note: if a presentation
  // context is not available, they will assume a 1:1 ratio.
  int32_t DevToCSSIntPixels(int32_t px);
  int32_t CSSToDevIntPixels(int32_t px);
  nsIntSize DevToCSSIntPixels(nsIntSize px);
  nsIntSize CSSToDevIntPixels(nsIntSize px);

  virtual void SetFocusedNode(nsIContent* aNode,
                              uint32_t aFocusMethod = 0,
                              bool aNeedsFocus = false);

  virtual uint32_t GetFocusMethod();

  virtual bool ShouldShowFocusRing();

  virtual void SetKeyboardIndicators(UIStateChangeType aShowAccelerators,
                                     UIStateChangeType aShowFocusRings);
  virtual void GetKeyboardIndicators(bool* aShowAccelerators,
                                     bool* aShowFocusRings);

  void UpdateCanvasFocus(bool aFocusChanged, nsIContent* aNewContent);

  already_AddRefed<nsPIWindowRoot> GetTopWindowRoot();

  static void NotifyDOMWindowDestroyed(nsGlobalWindow* aWindow);
  void NotifyWindowIDDestroyed(const char* aTopic);

  static void NotifyDOMWindowFrozen(nsGlobalWindow* aWindow);
  static void NotifyDOMWindowThawed(nsGlobalWindow* aWindow);

  void ClearStatus();

  virtual void UpdateParentTarget();

  bool GetIsTabModalPromptAllowed();

  inline int32_t DOMMinTimeoutValue() const;

  nsresult CreateOuterObject(nsGlobalWindow* aNewInner);
  nsresult SetOuterObject(JSContext* aCx, JSObject* aOuterObject);
  nsresult CloneStorageEvent(const nsAString& aType,
                             nsCOMPtr<nsIDOMStorageEvent>& aEvent);

  // Implements Get{Real,Scriptable}Top.
  nsresult GetTopImpl(nsIDOMWindow **aWindow, bool aScriptable);

  // Helper for creating performance objects.
  void CreatePerformanceObjectIfNeeded();

  // Outer windows only.
  nsDOMWindowList* GetWindowList();

  // Helper for getComputedStyle and getDefaultComputedStyle
  nsresult GetComputedStyleHelper(nsIDOMElement* aElt,
                                  const nsAString& aPseudoElt,
                                  bool aDefaultStylesOnly,
                                  nsIDOMCSSStyleDeclaration** aReturn);

  // When adding new member variables, be careful not to create cycles
  // through JavaScript.  If there is any chance that a member variable
  // could own objects that are implemented in JavaScript, then those
  // objects will keep the global object (this object) alive.  To prevent
  // these cycles, ownership of such members must be released in
  // |CleanUp| and |DetachFromDocShell|.

  // This member is also used on both inner and outer windows, but
  // for slightly different purposes. On inner windows it means the
  // inner window is held onto by session history and should not
  // change. On outer windows it means that the window is in a state
  // where we don't want to force creation of a new inner window since
  // we're in the middle of doing just that.
  bool                          mIsFrozen : 1;

  // These members are only used on outer window objects. Make sure
  // you never set any of these on an inner object!
  bool                          mFullScreen : 1;
  bool                          mIsClosed : 1;
  bool                          mInClose : 1;
  // mHavePendingClose means we've got a termination function set to
  // close us when the JS stops executing or that we have a close
  // event posted.  If this is set, just ignore window.close() calls.
  bool                          mHavePendingClose : 1;
  bool                          mHadOriginalOpener : 1;
  bool                          mIsPopupSpam : 1;

  // Indicates whether scripts are allowed to close this window.
  bool                          mBlockScriptedClosingFlag : 1;

  // Track what sorts of events we need to fire when thawed
  bool                          mFireOfflineStatusChangeEventOnThaw : 1;
  bool                          mNotifyIdleObserversIdleOnThaw : 1;
  bool                          mNotifyIdleObserversActiveOnThaw : 1;

  // Indicates whether we're in the middle of creating an initializing
  // a new inner window object.
  bool                          mCreatingInnerWindow : 1;

  // Fast way to tell if this is a chrome window (without having to QI).
  bool                          mIsChrome : 1;

  // Hack to indicate whether a chrome window needs its message manager
  // to be disconnected, since clean up code is shared in the global
  // window superclass.
  bool                          mCleanMessageManager : 1;

  // Indicates that the current document has never received a document focus
  // event.
  bool                   mNeedsFocus : 1;
  bool                   mHasFocus : 1;

  // whether to show keyboard accelerators
  bool                   mShowAccelerators : 1;

  // whether to show focus rings
  bool                   mShowFocusRings : 1;

  // when true, show focus rings for the current focused content only.
  // This will be reset when another element is focused
  bool                   mShowFocusRingForContent : 1;

  // true if tab navigation has occurred for this window. Focus rings
  // should be displayed.
  bool                   mFocusByKeyOccurred : 1;

  // whether we've sent the destroy notification for our window id
  bool                   mNotifiedIDDestroyed : 1;
  // whether scripts may close the window,
  // even if "dom.allow_scripts_to_close_windows" is false.
  bool                   mAllowScriptsToClose : 1;

  nsCOMPtr<nsIScriptContext>    mContext;
  nsWeakPtr                     mOpener;
  nsCOMPtr<nsIControllers>      mControllers;
  nsCOMPtr<nsIArray>            mArguments;
  nsCOMPtr<nsIArray>            mArgumentsLast;
  nsCOMPtr<nsIPrincipal>        mArgumentsOrigin;
  nsRefPtr<Navigator>           mNavigator;
  nsRefPtr<nsScreen>            mScreen;
  // mPerformance is only used on inner windows.
  nsRefPtr<nsPerformance>       mPerformance;
  nsRefPtr<nsDOMWindowList>     mFrames;
  nsRefPtr<nsBarProp>           mMenubar;
  nsRefPtr<nsBarProp>           mToolbar;
  nsRefPtr<nsBarProp>           mLocationbar;
  nsRefPtr<nsBarProp>           mPersonalbar;
  nsRefPtr<nsBarProp>           mStatusbar;
  nsRefPtr<nsBarProp>           mScrollbars;
  nsRefPtr<nsDOMWindowUtils>    mWindowUtils;
  nsString                      mStatus;
  nsString                      mDefaultStatus;
  // index 0->language_id 1, so index MAX-1 == language_id MAX
  nsGlobalWindowObserver*       mObserver;
#ifndef MOZ_DISABLE_DOMCRYPTO
  nsCOMPtr<nsIDOMCrypto>        mCrypto;
#endif
  nsCOMPtr<nsIDOMStorage>      mLocalStorage;
  nsCOMPtr<nsIDOMStorage>      mSessionStorage;

  nsCOMPtr<nsIXPConnectJSObjectHolder> mInnerWindowHolder;

  // These member variable are used only on inner windows.
  nsRefPtr<nsEventListenerManager> mListenerManager;
  // mTimeouts is generally sorted by mWhen, unless mTimeoutInsertionPoint is
  // non-null.  In that case, the dummy timeout pointed to by
  // mTimeoutInsertionPoint may have a later mWhen than some of the timeouts
  // that come after it.
  mozilla::LinkedList<nsTimeout> mTimeouts;
  // If mTimeoutInsertionPoint is non-null, insertions should happen after it.
  // This is a dummy timeout at the moment; if that ever changes, the logic in
  // ResetTimersForNonBackgroundWindow needs to change.
  nsTimeout*                    mTimeoutInsertionPoint;
  uint32_t                      mTimeoutPublicIdCounter;
  uint32_t                      mTimeoutFiringDepth;
  nsRefPtr<nsLocation>          mLocation;
  nsRefPtr<nsHistory>           mHistory;

  // These member variables are used on both inner and the outer windows.
  nsCOMPtr<nsIPrincipal> mDocumentPrincipal;
  JSObject* mJSObject;

  typedef nsCOMArray<nsIDOMStorageEvent> nsDOMStorageEventArray;
  nsDOMStorageEventArray mPendingStorageEvents;

  uint32_t mTimeoutsSuspendDepth;

  // the method that was used to focus mFocusedNode
  uint32_t mFocusMethod;

  uint32_t mSerial;

#ifdef DEBUG
  bool mSetOpenerWindowCalled;
  nsCOMPtr<nsIURI> mLastOpenedURI;
#endif

  bool mCleanedUp, mCallCleanUpAfterModalDialogCloses;

  nsCOMPtr<nsIDOMOfflineResourceList> mApplicationCache;

  nsDataHashtable<nsPtrHashKey<nsXBLPrototypeHandler>, JSObject*> mCachedXBLPrototypeHandlers;

  nsCOMPtr<nsIDocument> mSuspendedDoc;

  nsCOMPtr<nsIIDBFactory> mIndexedDB;

  // This counts the number of windows that have been opened in rapid succession
  // (i.e. within dom.successive_dialog_time_limit of each other). It is reset
  // to 0 once a dialog is opened after dom.successive_dialog_time_limit seconds
  // have elapsed without any other dialogs.
  uint32_t                      mDialogAbuseCount;

  // This holds the time when the last modal dialog was shown. If more than
  // MAX_DIALOG_LIMIT dialogs are shown within the time span defined by
  // dom.successive_dialog_time_limit, we show a checkbox or confirmation prompt
  // to allow disabling of further dialogs from this window.
  TimeStamp                     mLastDialogQuitTime;

  // This is set to true once the user has opted-in to preventing further
  // dialogs for this window. Subsequent dialogs may still open if
  // mDialogAbuseCount gets reset.
  bool                          mStopAbuseDialogs;

  // This flag gets set when dialogs should be permanently disabled for this
  // window (e.g. when we are closing the tab and therefore are guaranteed to be
  // destroying this window).
  bool                          mDialogsPermanentlyDisabled;

  nsTHashtable<nsPtrHashKey<nsDOMEventTargetHelper> > mEventTargetObjects;

  nsTArray<uint32_t> mEnabledSensors;

  friend class nsDOMScriptableHelper;
  friend class nsDOMWindowUtils;
  friend class PostMessageEvent;
  friend class nsDOMDesktopNotification;

  static WindowByIdTable* sWindowsById;
  static bool sWarnedAboutWindowInternal;
};

/*
 * nsGlobalChromeWindow inherits from nsGlobalWindow. It is the global
 * object created for a Chrome Window only.
 */
class nsGlobalChromeWindow : public nsGlobalWindow,
                             public nsIDOMChromeWindow
{
public:
  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMChromeWindow interface
  NS_DECL_NSIDOMCHROMEWINDOW

  nsGlobalChromeWindow(nsGlobalWindow *aOuterWindow)
    : nsGlobalWindow(aOuterWindow)
  {
    mIsChrome = true;
    mCleanMessageManager = true;
  }

  ~nsGlobalChromeWindow()
  {
    NS_ABORT_IF_FALSE(mCleanMessageManager,
                      "chrome windows may always disconnect the msg manager");
    if (mMessageManager) {
      static_cast<nsFrameMessageManager *>(
        mMessageManager.get())->Disconnect();
    }

    mCleanMessageManager = false;
  }

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsGlobalChromeWindow,
                                           nsGlobalWindow)

  nsCOMPtr<nsIBrowserDOMWindow> mBrowserDOMWindow;
  nsCOMPtr<nsIMessageBroadcaster> mMessageManager;
};

/*
 * nsGlobalModalWindow inherits from nsGlobalWindow. It is the global
 * object created for a modal content windows only (i.e. not modal
 * chrome dialogs).
 */
class nsGlobalModalWindow : public nsGlobalWindow,
                            public nsIDOMModalContentWindow
{
public:
  nsGlobalModalWindow(nsGlobalWindow *aOuterWindow)
    : nsGlobalWindow(aOuterWindow)
  {
    mIsModalContentWindow = true;
  }

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMMODALCONTENTWINDOW

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsGlobalModalWindow, nsGlobalWindow)

  virtual NS_HIDDEN_(nsresult) SetNewDocument(nsIDocument *aDocument,
                                              nsISupports *aState,
                                              bool aForceReuseInnerWindow);

protected:
  nsCOMPtr<nsIVariant> mReturnValue;
};

/* factory function */
inline already_AddRefed<nsGlobalWindow>
NS_NewScriptGlobalObject(bool aIsChrome, bool aIsModalContentWindow)
{
  nsRefPtr<nsGlobalWindow> global;

  if (aIsChrome) {
    global = new nsGlobalChromeWindow(nullptr);
  } else if (aIsModalContentWindow) {
    global = new nsGlobalModalWindow(nullptr);
  } else {
    global = new nsGlobalWindow(nullptr);
  }

  return global.forget();
}

#endif /* nsGlobalWindow_h___ */
