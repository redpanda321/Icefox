/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set tw=80 expandtab softtabstop=2 ts=2 sw=2: */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsGenericHTMLFrameElement.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsContentUtils.h"
#include "mozilla/Preferences.h"
#include "nsIAppsService.h"
#include "nsServiceManagerUtils.h"
#include "nsIDOMApplicationRegistry.h"

using namespace mozilla;
using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION_CLASS(nsGenericHTMLFrameElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsGenericHTMLFrameElement,
                                                  nsGenericHTMLElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(mFrameLoader, nsIFrameLoader)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_TABLE_HEAD(nsGenericHTMLFrameElement)
  NS_INTERFACE_TABLE_INHERITED3(nsGenericHTMLFrameElement,
                                nsIFrameLoaderOwner,
                                nsIDOMMozBrowserFrame,
                                nsIMozBrowserFrame)
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(nsGenericHTMLFrameElement)
NS_INTERFACE_MAP_END_INHERITING(nsGenericHTMLElement)

NS_IMPL_INT_ATTR(nsGenericHTMLFrameElement, TabIndex, tabindex)
NS_IMPL_BOOL_ATTR(nsGenericHTMLFrameElement, Mozbrowser, mozbrowser)

nsGenericHTMLFrameElement::~nsGenericHTMLFrameElement()
{
  if (mFrameLoader) {
    mFrameLoader->Destroy();
  }
}

nsresult
nsGenericHTMLFrameElement::GetContentDocument(nsIDOMDocument** aContentDocument)
{
  NS_PRECONDITION(aContentDocument, "Null out param");
  *aContentDocument = nsnull;

  nsCOMPtr<nsIDOMWindow> win;
  GetContentWindow(getter_AddRefs(win));

  if (!win) {
    return NS_OK;
  }

  return win->GetDocument(aContentDocument);
}

nsresult
nsGenericHTMLFrameElement::GetContentWindow(nsIDOMWindow** aContentWindow)
{
  NS_PRECONDITION(aContentWindow, "Null out param");
  *aContentWindow = nsnull;

  nsresult rv = EnsureFrameLoader();
  NS_ENSURE_SUCCESS(rv, rv);

  if (!mFrameLoader) {
    return NS_OK;
  }

  bool depthTooGreat = false;
  mFrameLoader->GetDepthTooGreat(&depthTooGreat);
  if (depthTooGreat) {
    // Claim to have no contentWindow
    return NS_OK;
  }

  nsCOMPtr<nsIDocShell> doc_shell;
  mFrameLoader->GetDocShell(getter_AddRefs(doc_shell));

  nsCOMPtr<nsPIDOMWindow> win(do_GetInterface(doc_shell));

  if (!win) {
    return NS_OK;
  }

  NS_ASSERTION(win->IsOuterWindow(),
               "Uh, this window should always be an outer window!");

  return CallQueryInterface(win, aContentWindow);
}

nsresult
nsGenericHTMLFrameElement::EnsureFrameLoader()
{
  if (!GetParent() || !IsInDoc() || mFrameLoader || mFrameLoaderCreationDisallowed) {
    // If frame loader is there, we just keep it around, cached
    return NS_OK;
  }

  mFrameLoader = nsFrameLoader::Create(this, mNetworkCreated);
  if (!mFrameLoader) {
    // Strangely enough, this method doesn't actually ensure that the
    // frameloader exists.  It's more of a best-effort kind of thing.
    return NS_OK;
  }

  return NS_OK;
}

nsresult
nsGenericHTMLFrameElement::CreateRemoteFrameLoader(nsITabParent* aTabParent)
{
  MOZ_ASSERT(!mFrameLoader);
  EnsureFrameLoader();
  NS_ENSURE_STATE(mFrameLoader);
  mFrameLoader->SetRemoteBrowser(aTabParent);
  return NS_OK;
}

NS_IMETHODIMP
nsGenericHTMLFrameElement::GetFrameLoader(nsIFrameLoader **aFrameLoader)
{
  NS_IF_ADDREF(*aFrameLoader = mFrameLoader);
  return NS_OK;
}

NS_IMETHODIMP_(already_AddRefed<nsFrameLoader>)
nsGenericHTMLFrameElement::GetFrameLoader()
{
  nsRefPtr<nsFrameLoader> loader = mFrameLoader;
  return loader.forget();
}

NS_IMETHODIMP
nsGenericHTMLFrameElement::SwapFrameLoaders(nsIFrameLoaderOwner* aOtherOwner)
{
  // We don't support this yet
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
nsGenericHTMLFrameElement::LoadSrc()
{
  nsresult rv = EnsureFrameLoader();
  NS_ENSURE_SUCCESS(rv, rv);

  if (!mFrameLoader) {
    return NS_OK;
  }

  rv = mFrameLoader->LoadFrame();
#ifdef DEBUG
  if (NS_FAILED(rv)) {
    NS_WARNING("failed to load URL");
  }
#endif

  return rv;
}

nsresult
nsGenericHTMLFrameElement::BindToTree(nsIDocument* aDocument,
                                      nsIContent* aParent,
                                      nsIContent* aBindingParent,
                                      bool aCompileEventHandlers)
{
  nsresult rv = nsGenericHTMLElement::BindToTree(aDocument, aParent,
                                                 aBindingParent,
                                                 aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aDocument) {
    NS_ASSERTION(!nsContentUtils::IsSafeToRunScript(),
                 "Missing a script blocker!");
    // We're in a document now.  Kick off the frame load.
    LoadSrc();
  }

  // We're now in document and scripts may move us, so clear
  // the mNetworkCreated flag.
  mNetworkCreated = false;
  return rv;
}

void
nsGenericHTMLFrameElement::UnbindFromTree(bool aDeep, bool aNullParent)
{
  if (mFrameLoader) {
    // This iframe is being taken out of the document, destroy the
    // iframe's frame loader (doing that will tear down the window in
    // this iframe).
    // XXXbz we really want to only partially destroy the frame
    // loader... we don't want to tear down the docshell.  Food for
    // later bug.
    mFrameLoader->Destroy();
    mFrameLoader = nsnull;
  }

  nsGenericHTMLElement::UnbindFromTree(aDeep, aNullParent);
}

nsresult
nsGenericHTMLFrameElement::SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                                   nsIAtom* aPrefix, const nsAString& aValue,
                                   bool aNotify)
{
  nsresult rv = nsGenericHTMLElement::SetAttr(aNameSpaceID, aName, aPrefix,
                                              aValue, aNotify);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aNameSpaceID == kNameSpaceID_None && aName == nsGkAtoms::src) {
    // Don't propagate error here. The attribute was successfully set, that's
    // what we should reflect.
    LoadSrc();
  }

  return NS_OK;
}

void
nsGenericHTMLFrameElement::DestroyContent()
{
  if (mFrameLoader) {
    mFrameLoader->Destroy();
    mFrameLoader = nsnull;
  }

  nsGenericHTMLElement::DestroyContent();
}

nsresult
nsGenericHTMLFrameElement::CopyInnerTo(nsGenericElement* aDest)
{
  nsresult rv = nsGenericHTMLElement::CopyInnerTo(aDest);
  NS_ENSURE_SUCCESS(rv, rv);

  nsIDocument* doc = aDest->OwnerDoc();
  if (doc->IsStaticDocument() && mFrameLoader) {
    nsGenericHTMLFrameElement* dest =
      static_cast<nsGenericHTMLFrameElement*>(aDest);
    nsFrameLoader* fl = nsFrameLoader::Create(dest, false);
    NS_ENSURE_STATE(fl);
    dest->mFrameLoader = fl;
    static_cast<nsFrameLoader*>(mFrameLoader.get())->CreateStaticClone(fl);
  }

  return rv;
}

bool
nsGenericHTMLFrameElement::IsHTMLFocusable(bool aWithMouse,
                                           bool *aIsFocusable,
                                           PRInt32 *aTabIndex)
{
  if (nsGenericHTMLElement::IsHTMLFocusable(aWithMouse, aIsFocusable, aTabIndex)) {
    return true;
  }

  *aIsFocusable = nsContentUtils::IsSubDocumentTabbable(this);

  if (!*aIsFocusable && aTabIndex) {
    *aTabIndex = -1;
  }

  return false;
}

/**
 * Return true if this frame element really is a mozbrowser or mozapp.  (It
 * needs to have the right attributes, and its creator must have the right
 * permissions.)
 */
nsresult
nsGenericHTMLFrameElement::GetReallyIsBrowser(bool *aOut)
{
  *aOut = false;

  // Fail if browser frames are globally disabled.
  if (!Preferences::GetBool("dom.mozBrowserFramesEnabled")) {
    return NS_OK;
  }

  // Fail if this frame doesn't have the mozbrowser attribute.
  bool isBrowser = false;
  GetMozbrowser(&isBrowser);
  if (!isBrowser) {
    return NS_OK;
  }

  // Fail if the node principal isn't trusted.
  // TODO: check properly for mozApps rights when mozApps will be less hacky.
  nsIPrincipal *principal = NodePrincipal();
  nsCOMPtr<nsIURI> principalURI;
  principal->GetURI(getter_AddRefs(principalURI));
  if (!nsContentUtils::IsSystemPrincipal(principal) &&
      !nsContentUtils::URIIsChromeOrInPref(principalURI,
                                           "dom.mozBrowserFramesWhitelist")) {
    return NS_OK;
  }

  // Otherwise, succeed.
  *aOut = true;
  return NS_OK;
}

NS_IMETHODIMP
nsGenericHTMLFrameElement::GetReallyIsApp(bool *aOut)
{
  nsAutoString manifestURL;
  GetAppManifestURL(manifestURL);

  *aOut = !manifestURL.IsEmpty();
  return NS_OK;
}

NS_IMETHODIMP
nsGenericHTMLFrameElement::GetAppManifestURL(nsAString& aOut)
{
  aOut.Truncate();

  // At the moment, you can't be an app without being a browser.
  bool isBrowser = false;
  GetReallyIsBrowser(&isBrowser);
  if (!isBrowser) {
    return NS_OK;
  }

  // TODO: We surely need a permissions check here, particularly once we no
  // longer rely on the mozbrowser permission check.

  nsAutoString manifestURL;
  GetAttr(kNameSpaceID_None, nsGkAtoms::mozapp, manifestURL);
  if (manifestURL.IsEmpty()) {
    return NS_OK;
  }

  nsCOMPtr<nsIAppsService> appsService = do_GetService(APPS_SERVICE_CONTRACTID);
  NS_ENSURE_STATE(appsService);

  nsCOMPtr<mozIDOMApplication> app;
  appsService->GetAppByManifestURL(manifestURL, getter_AddRefs(app));

  if (app) {
    aOut.Assign(manifestURL);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsGenericHTMLFrameElement::DisallowCreateFrameLoader()
{
  MOZ_ASSERT(!mFrameLoader);
  MOZ_ASSERT(!mFrameLoaderCreationDisallowed);
  mFrameLoaderCreationDisallowed = true;
  return NS_OK;
}

NS_IMETHODIMP
nsGenericHTMLFrameElement::AllowCreateFrameLoader()
{
  MOZ_ASSERT(!mFrameLoader);
  MOZ_ASSERT(mFrameLoaderCreationDisallowed);
  mFrameLoaderCreationDisallowed = false;
  return NS_OK;
}
