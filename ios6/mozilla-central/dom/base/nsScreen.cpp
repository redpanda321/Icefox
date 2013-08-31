/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Hal.h"
#include "nsScreen.h"
#include "nsIDocShell.h"
#include "nsPresContext.h"
#include "nsCOMPtr.h"
#include "nsDOMClassInfoID.h"
#include "nsIDocShellTreeItem.h"
#include "nsLayoutUtils.h"
#include "nsDOMEvent.h"
#include "nsGlobalWindow.h"
#include "nsJSUtils.h"
#include "mozilla/dom/ScreenBinding.h"

using namespace mozilla;
using namespace mozilla::dom;

namespace {

bool
IsChromeType(nsIDocShell *aDocShell)
{
  nsCOMPtr<nsIDocShellTreeItem> ds = do_QueryInterface(aDocShell);
  if (!ds) {
    return false;
  }

  int32_t itemType;
  ds->GetItemType(&itemType);
  return itemType == nsIDocShellTreeItem::typeChrome;
}

} // anonymous namespace

/* static */ already_AddRefed<nsScreen>
nsScreen::Create(nsPIDOMWindow* aWindow)
{
  MOZ_ASSERT(aWindow);

  if (!aWindow->GetDocShell()) {
    return nullptr;
  }

  nsCOMPtr<nsIScriptGlobalObject> sgo =
    do_QueryInterface(static_cast<nsPIDOMWindow*>(aWindow));
  NS_ENSURE_TRUE(sgo, nullptr);

  nsRefPtr<nsScreen> screen = new nsScreen();
  screen->BindToOwner(aWindow);

  hal::RegisterScreenConfigurationObserver(screen);
  hal::ScreenConfiguration config;
  hal::GetCurrentScreenConfiguration(&config);
  screen->mOrientation = config.orientation();

  return screen.forget();
}

nsScreen::nsScreen()
  : mEventListener(nullptr)
{
  SetIsDOMBinding();
}

void
nsScreen::Reset()
{
  hal::UnlockScreenOrientation();

  if (mEventListener) {
    nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(GetOwner());
    if (target) {
      target->RemoveSystemEventListener(NS_LITERAL_STRING("mozfullscreenchange"),
                                        mEventListener, /* usecapture */ true);
    }

    mEventListener = nullptr;
  }
}

nsScreen::~nsScreen()
{
  Reset();
  hal::UnregisterScreenConfigurationObserver(this);
}


DOMCI_DATA(Screen, nsScreen)

NS_IMPL_CYCLE_COLLECTION_INHERITED_0(nsScreen, nsDOMEventTargetHelper)

// QueryInterface implementation for nsScreen
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(nsScreen)
  NS_INTERFACE_MAP_ENTRY(nsIDOMScreen)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMScreen)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(Screen)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(nsScreen, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(nsScreen, nsDOMEventTargetHelper)

NS_IMPL_EVENT_HANDLER(nsScreen, mozorientationchange)

int32_t
nsScreen::GetPixelDepth(ErrorResult& aRv)
{
  nsDeviceContext* context = GetDeviceContext();

  if (!context) {
    aRv.Throw(NS_ERROR_FAILURE);
    return -1;
  }

  uint32_t depth;
  context->GetDepth(depth);
  return depth;
}

#define FORWARD_LONG_GETTER(_name)                                              \
  NS_IMETHODIMP                                                                 \
  nsScreen::Get ## _name(int32_t* aOut)                                         \
  {                                                                             \
    ErrorResult rv;                                                             \
    *aOut = Get ## _name(rv);                                                   \
    return rv.ErrorCode();                                                      \
  }

FORWARD_LONG_GETTER(AvailWidth)
FORWARD_LONG_GETTER(AvailHeight)
FORWARD_LONG_GETTER(Width)
FORWARD_LONG_GETTER(Height)

FORWARD_LONG_GETTER(Top)
FORWARD_LONG_GETTER(Left)
FORWARD_LONG_GETTER(AvailTop)
FORWARD_LONG_GETTER(AvailLeft)

FORWARD_LONG_GETTER(PixelDepth)
FORWARD_LONG_GETTER(ColorDepth)

nsDeviceContext*
nsScreen::GetDeviceContext()
{
  return nsLayoutUtils::GetDeviceContextForScreenInfo(GetOwner());
}

nsresult
nsScreen::GetRect(nsRect& aRect)
{
  nsDeviceContext *context = GetDeviceContext();

  if (!context) {
    return NS_ERROR_FAILURE;
  }

  context->GetRect(aRect);

  aRect.x = nsPresContext::AppUnitsToIntCSSPixels(aRect.x);
  aRect.y = nsPresContext::AppUnitsToIntCSSPixels(aRect.y);
  aRect.height = nsPresContext::AppUnitsToIntCSSPixels(aRect.height);
  aRect.width = nsPresContext::AppUnitsToIntCSSPixels(aRect.width);

  return NS_OK;
}

nsresult
nsScreen::GetAvailRect(nsRect& aRect)
{
  nsDeviceContext *context = GetDeviceContext();

  if (!context) {
    return NS_ERROR_FAILURE;
  }

  context->GetClientRect(aRect);

  aRect.x = nsPresContext::AppUnitsToIntCSSPixels(aRect.x);
  aRect.y = nsPresContext::AppUnitsToIntCSSPixels(aRect.y);
  aRect.height = nsPresContext::AppUnitsToIntCSSPixels(aRect.height);
  aRect.width = nsPresContext::AppUnitsToIntCSSPixels(aRect.width);

  return NS_OK;
}

void
nsScreen::Notify(const hal::ScreenConfiguration& aConfiguration)
{
  ScreenOrientation previousOrientation = mOrientation;
  mOrientation = aConfiguration.orientation();

  NS_ASSERTION(mOrientation == eScreenOrientation_PortraitPrimary ||
               mOrientation == eScreenOrientation_PortraitSecondary ||
               mOrientation == eScreenOrientation_LandscapePrimary ||
               mOrientation == eScreenOrientation_LandscapeSecondary,
               "Invalid orientation value passed to notify method!");

  if (mOrientation != previousOrientation) {
    DispatchTrustedEvent(NS_LITERAL_STRING("mozorientationchange"));
  }
}

void
nsScreen::GetMozOrientation(nsString& aOrientation)
{
  switch (mOrientation) {
  case eScreenOrientation_PortraitPrimary:
    aOrientation.AssignLiteral("portrait-primary");
    break;
  case eScreenOrientation_PortraitSecondary:
    aOrientation.AssignLiteral("portrait-secondary");
    break;
  case eScreenOrientation_LandscapePrimary:
    aOrientation.AssignLiteral("landscape-primary");
    break;
  case eScreenOrientation_LandscapeSecondary:
    aOrientation.AssignLiteral("landscape-secondary");
    break;
  case eScreenOrientation_None:
  default:
    MOZ_NOT_REACHED("Unacceptable mOrientation value");
  }
}

NS_IMETHODIMP
nsScreen::GetSlowMozOrientation(nsAString& aOrientation)
{
  nsString orientation;
  GetMozOrientation(orientation);
  aOrientation = orientation;
  return NS_OK;
}

nsScreen::LockPermission
nsScreen::GetLockOrientationPermission() const
{
  nsCOMPtr<nsPIDOMWindow> owner = GetOwner();
  if (!owner) {
    return LOCK_DENIED;
  }

  // Chrome can always lock the screen orientation.
  if (IsChromeType(owner->GetDocShell())) {
    return LOCK_ALLOWED;
  }

  nsCOMPtr<nsIDOMDocument> domDoc;
  owner->GetDocument(getter_AddRefs(domDoc));
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);
  if (!doc) {
    return LOCK_DENIED;
  }

  // Apps can always lock the screen orientation.
  if (doc->NodePrincipal()->GetAppStatus() >=
        nsIPrincipal::APP_STATUS_INSTALLED) {
    return LOCK_ALLOWED;
  }

  // Other content must be full-screen in order to lock orientation.
  bool fullscreen;
  domDoc->GetMozFullScreen(&fullscreen);

  return fullscreen ? FULLSCREEN_LOCK_ALLOWED : LOCK_DENIED;
}

NS_IMETHODIMP
nsScreen::MozLockOrientation(const JS::Value& aOrientation, JSContext* aCx,
                             bool* aReturn)
{
  if (aOrientation.isObject() && IsArrayLike(aCx, &aOrientation.toObject())) {
    JSObject* seq = &aOrientation.toObject();
    uint32_t length;
    // JS_GetArrayLength actually works on all objects
    if (!JS_GetArrayLength(aCx, seq, &length)) {
      return NS_ERROR_FAILURE;
    }

    Sequence<nsString> orientations;
    if (!orientations.SetCapacity(length)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < length; ++i) {
      JS::Value temp;
      if (!JS_GetElement(aCx, seq, i, &temp)) {
        return NS_ERROR_FAILURE;
      }

      js::RootedString jsString(aCx, JS_ValueToString(aCx, temp));
      if (!jsString) {
        return NS_ERROR_FAILURE;
      }

      nsDependentJSString str;
      if (!str.init(aCx, jsString)) {
        return NS_ERROR_FAILURE;
      }

      *orientations.AppendElement() = str;
    }

    ErrorResult rv;
    *aReturn = MozLockOrientation(orientations, rv);
    return rv.ErrorCode();
  }

  js::RootedString jsString(aCx, JS_ValueToString(aCx, aOrientation));
  if (!jsString) {
    return NS_ERROR_FAILURE;
  }

  nsDependentJSString orientation;
  if (!orientation.init(aCx, jsString)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult rv;
  *aReturn = MozLockOrientation(orientation, rv);
  return rv.ErrorCode();
}

bool
nsScreen::MozLockOrientation(const nsAString& aOrientation, ErrorResult& aRv)
{
  nsString orientation(aOrientation);
  Sequence<nsString> orientations;
  if (!orientations.AppendElement(orientation)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return false;
  }
  return MozLockOrientation(orientations, aRv);
}

bool
nsScreen::MozLockOrientation(const Sequence<nsString>& aOrientations,
                             ErrorResult& aRv)
{
  ScreenOrientation orientation = eScreenOrientation_None;

  for (uint32_t i = 0; i < aOrientations.Length(); ++i) {
    const nsString& item = aOrientations[i];

    if (item.EqualsLiteral("portrait")) {
      orientation |= eScreenOrientation_PortraitPrimary |
                     eScreenOrientation_PortraitSecondary;
    } else if (item.EqualsLiteral("portrait-primary")) {
      orientation |= eScreenOrientation_PortraitPrimary;
    } else if (item.EqualsLiteral("portrait-secondary")) {
      orientation |= eScreenOrientation_PortraitSecondary;
    } else if (item.EqualsLiteral("landscape")) {
      orientation |= eScreenOrientation_LandscapePrimary |
                     eScreenOrientation_LandscapeSecondary;
    } else if (item.EqualsLiteral("landscape-primary")) {
      orientation |= eScreenOrientation_LandscapePrimary;
    } else if (item.EqualsLiteral("landscape-secondary")) {
      orientation |= eScreenOrientation_LandscapeSecondary;
    } else {
      // If we don't recognize the token, we should just return 'false'
      // without throwing.
      return false;
    }
  }

  switch (GetLockOrientationPermission()) {
    case LOCK_DENIED:
      return false;
    case LOCK_ALLOWED:
      return hal::LockScreenOrientation(orientation);
    case FULLSCREEN_LOCK_ALLOWED: {
      // We need to register a listener so we learn when we leave full-screen
      // and when we will have to unlock the screen.
      // This needs to be done before LockScreenOrientation call to make sure
      // the locking can be unlocked.
      nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(GetOwner());
      if (!target) {
        return false;
      }

      if (!hal::LockScreenOrientation(orientation)) {
        return false;
      }

      // We are fullscreen and lock has been accepted.
      if (!mEventListener) {
        mEventListener = new FullScreenEventListener();
      }

      aRv = target->AddSystemEventListener(NS_LITERAL_STRING("mozfullscreenchange"),
                                           mEventListener, /* useCapture = */ true);
      return true;
    }
  }

  // This is only for compilers that don't understand that the previous switch
  // will always return.
  MOZ_NOT_REACHED();
  return false;
}

void
nsScreen::MozUnlockOrientation()
{
  hal::UnlockScreenOrientation();
}

NS_IMETHODIMP
nsScreen::SlowMozUnlockOrientation()
{
  MozUnlockOrientation();
  return NS_OK;
}

/* virtual */
JSObject*
nsScreen::WrapObject(JSContext* aCx, JSObject* aScope, bool* aTriedToWrap)
{
  return ScreenBinding::Wrap(aCx, aScope, this, aTriedToWrap);
}

NS_IMPL_ISUPPORTS1(nsScreen::FullScreenEventListener, nsIDOMEventListener)

NS_IMETHODIMP
nsScreen::FullScreenEventListener::HandleEvent(nsIDOMEvent* aEvent)
{
#ifdef DEBUG
  nsAutoString eventType;
  aEvent->GetType(eventType);

  MOZ_ASSERT(eventType.EqualsLiteral("mozfullscreenchange"));
#endif

  nsCOMPtr<nsIDOMEventTarget> target;
  aEvent->GetCurrentTarget(getter_AddRefs(target));

  // We have to make sure that the event we got is the event sent when
  // fullscreen is disabled because we could get one when fullscreen
  // got enabled if the lock call is done at the same moment.
  nsCOMPtr<nsIDOMWindow> window = do_QueryInterface(target);
  MOZ_ASSERT(window);

  nsCOMPtr<nsIDOMDocument> doc;
  window->GetDocument(getter_AddRefs(doc));
  // If we have no doc, we will just continue, remove the event and unlock.
  // This is an edge case were orientation lock and fullscreen is meaningless.
  if (doc) {
    bool fullscreen;
    doc->GetMozFullScreen(&fullscreen);
    if (fullscreen) {
      return NS_OK;
    }
  }

  target->RemoveSystemEventListener(NS_LITERAL_STRING("mozfullscreenchange"),
                                    this, true);

  hal::UnlockScreenOrientation();

  return NS_OK;
}
