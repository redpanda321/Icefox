/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Joe Hewitt <hewitt@netscape.com> (Original Author)
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

#include "mozilla/ModuleUtils.h"

#include "nsBrowserCompsCID.h"
#include "DirectoryProvider.h"

#if defined(XP_WIN)
#include "nsWindowsShellService.h"
#elif defined(MOZ_WIDGET_COCOA)
#include "nsMacShellService.h"
#elif defined(MOZ_WIDGET_GTK2)
#include "nsGNOMEShellService.h"
#endif

#if !defined(WINCE) && !defined(MOZ_WIDGET_UIKIT)
#include "nsProfileMigrator.h"
#if !defined(XP_BEOS)
#include "nsDogbertProfileMigrator.h"
#endif
#if !defined(XP_OS2)
#include "nsOperaProfileMigrator.h"
#endif
#include "nsPhoenixProfileMigrator.h"
#include "nsSeamonkeyProfileMigrator.h"
#if defined(XP_WIN) && !defined(__MINGW32__)
#include "nsIEProfileMigrator.h"
#elif defined(MOZ_WIDGET_COCOA)
#include "nsSafariProfileMigrator.h"
#include "nsOmniWebProfileMigrator.h"
#include "nsMacIEProfileMigrator.h"
#include "nsCaminoProfileMigrator.h"
#include "nsICabProfileMigrator.h"
#endif

#endif // WINCE

#include "rdf.h"
#include "nsFeedSniffer.h"
#include "AboutRedirector.h"
#include "nsIAboutModule.h"

#include "nsPrivateBrowsingServiceWrapper.h"
#include "nsNetCID.h"

using namespace mozilla::browser;

/////////////////////////////////////////////////////////////////////////////

NS_GENERIC_FACTORY_CONSTRUCTOR(DirectoryProvider)
#if defined(XP_WIN)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsWindowsShellService)
#elif defined(MOZ_WIDGET_COCOA)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsMacShellService)
#elif defined(MOZ_WIDGET_GTK2)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsGNOMEShellService, Init)
#endif


#if !defined(WINCE) && !defined(MOZ_WIDGET_UIKIT)
#if !defined(XP_BEOS)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsDogbertProfileMigrator)
#endif
#if !defined(XP_OS2)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsOperaProfileMigrator)
#endif
NS_GENERIC_FACTORY_CONSTRUCTOR(nsPhoenixProfileMigrator)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsProfileMigrator)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsSeamonkeyProfileMigrator)
#if defined(XP_WIN) && !defined(__MINGW32__)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsIEProfileMigrator)
#elif defined(MOZ_WIDGET_COCOA)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsSafariProfileMigrator)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsOmniWebProfileMigrator)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsMacIEProfileMigrator)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsCaminoProfileMigrator)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsICabProfileMigrator)
#endif

#endif

NS_GENERIC_FACTORY_CONSTRUCTOR(nsFeedSniffer)

NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsPrivateBrowsingServiceWrapper, Init)

NS_DEFINE_NAMED_CID(NS_BROWSERDIRECTORYPROVIDER_CID);
#if defined(XP_WIN)
NS_DEFINE_NAMED_CID(NS_SHELLSERVICE_CID);
#elif defined(MOZ_WIDGET_GTK2)
NS_DEFINE_NAMED_CID(NS_SHELLSERVICE_CID);
#endif
NS_DEFINE_NAMED_CID(NS_FEEDSNIFFER_CID);
NS_DEFINE_NAMED_CID(NS_BROWSER_ABOUT_REDIRECTOR_CID);
#if !defined(WINCE) && !defined(MOZ_WIDGET_UIKIT)
NS_DEFINE_NAMED_CID(NS_FIREFOX_PROFILEMIGRATOR_CID);
#if defined(XP_WIN) && !defined(__MINGW32__)
NS_DEFINE_NAMED_CID(NS_WINIEPROFILEMIGRATOR_CID);
#elif defined(MOZ_WIDGET_COCOA)
NS_DEFINE_NAMED_CID(NS_SHELLSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_SAFARIPROFILEMIGRATOR_CID);
NS_DEFINE_NAMED_CID(NS_MACIEPROFILEMIGRATOR_CID);
NS_DEFINE_NAMED_CID(NS_OMNIWEBPROFILEMIGRATOR_CID);
NS_DEFINE_NAMED_CID(NS_CAMINOPROFILEMIGRATOR_CID);
NS_DEFINE_NAMED_CID(NS_ICABPROFILEMIGRATOR_CID);
#endif
#if !defined(XP_OS2)
NS_DEFINE_NAMED_CID(NS_OPERAPROFILEMIGRATOR_CID);
#endif
#if !defined(XP_BEOS)
NS_DEFINE_NAMED_CID(NS_DOGBERTPROFILEMIGRATOR_CID);
#endif
NS_DEFINE_NAMED_CID(NS_PHOENIXPROFILEMIGRATOR_CID);
NS_DEFINE_NAMED_CID(NS_SEAMONKEYPROFILEMIGRATOR_CID);
#endif /* WINCE */
NS_DEFINE_NAMED_CID(NS_PRIVATE_BROWSING_SERVICE_WRAPPER_CID);

static const mozilla::Module::CIDEntry kBrowserCIDs[] = {
    { &kNS_BROWSERDIRECTORYPROVIDER_CID, false, NULL, DirectoryProviderConstructor },
#if defined(XP_WIN)
    { &kNS_SHELLSERVICE_CID, false, NULL, nsWindowsShellServiceConstructor },
#elif defined(MOZ_WIDGET_GTK2)
    { &kNS_SHELLSERVICE_CID, false, NULL, nsGNOMEShellServiceConstructor },
#endif
    { &kNS_FEEDSNIFFER_CID, false, NULL, nsFeedSnifferConstructor },
    { &kNS_BROWSER_ABOUT_REDIRECTOR_CID, false, NULL, AboutRedirector::Create },
#if !defined(WINCE) && !defined(MOZ_WIDGET_UIKIT)
    { &kNS_FIREFOX_PROFILEMIGRATOR_CID, false, NULL, nsProfileMigratorConstructor },
#if defined(XP_WIN) && !defined(__MINGW32__)
    { &kNS_WINIEPROFILEMIGRATOR_CID, false, NULL, nsIEProfileMigratorConstructor },
#elif defined(MOZ_WIDGET_COCOA)
    { &kNS_SHELLSERVICE_CID, false, NULL, nsMacShellServiceConstructor },
    { &kNS_SAFARIPROFILEMIGRATOR_CID, false, NULL, nsSafariProfileMigratorConstructor },
    { &kNS_MACIEPROFILEMIGRATOR_CID, false, NULL, nsMacIEProfileMigratorConstructor },
    { &kNS_OMNIWEBPROFILEMIGRATOR_CID, false, NULL, nsOmniWebProfileMigratorConstructor },
    { &kNS_CAMINOPROFILEMIGRATOR_CID, false, NULL, nsCaminoProfileMigratorConstructor },
    { &kNS_ICABPROFILEMIGRATOR_CID, false, NULL, nsICabProfileMigratorConstructor },
#endif
#if !defined(XP_OS2)
    { &kNS_OPERAPROFILEMIGRATOR_CID, false, NULL, nsOperaProfileMigratorConstructor },
#endif
#if !defined(XP_BEOS)
    { &kNS_DOGBERTPROFILEMIGRATOR_CID, false, NULL, nsDogbertProfileMigratorConstructor },
#endif
    { &kNS_PHOENIXPROFILEMIGRATOR_CID, false, NULL, nsPhoenixProfileMigratorConstructor },
    { &kNS_SEAMONKEYPROFILEMIGRATOR_CID, false, NULL, nsSeamonkeyProfileMigratorConstructor },
#endif /* WINCE */
    { &kNS_PRIVATE_BROWSING_SERVICE_WRAPPER_CID, false, NULL, nsPrivateBrowsingServiceWrapperConstructor },
    { NULL }
};

static const mozilla::Module::ContractIDEntry kBrowserContracts[] = {
    { NS_BROWSERDIRECTORYPROVIDER_CONTRACTID, &kNS_BROWSERDIRECTORYPROVIDER_CID },
#if defined(XP_WIN)
    { NS_SHELLSERVICE_CONTRACTID, &kNS_SHELLSERVICE_CID },
#elif defined(MOZ_WIDGET_GTK2)
    { NS_SHELLSERVICE_CONTRACTID, &kNS_SHELLSERVICE_CID },
#endif
    { NS_FEEDSNIFFER_CONTRACTID, &kNS_FEEDSNIFFER_CID },
#ifdef MOZ_SAFE_BROWSING
    { NS_ABOUT_MODULE_CONTRACTID_PREFIX "blocked", &kNS_BROWSER_ABOUT_REDIRECTOR_CID },
#endif
    { NS_ABOUT_MODULE_CONTRACTID_PREFIX "certerror", &kNS_BROWSER_ABOUT_REDIRECTOR_CID },
    { NS_ABOUT_MODULE_CONTRACTID_PREFIX "feeds", &kNS_BROWSER_ABOUT_REDIRECTOR_CID },
    { NS_ABOUT_MODULE_CONTRACTID_PREFIX "privatebrowsing", &kNS_BROWSER_ABOUT_REDIRECTOR_CID },
    { NS_ABOUT_MODULE_CONTRACTID_PREFIX "rights", &kNS_BROWSER_ABOUT_REDIRECTOR_CID },
    { NS_ABOUT_MODULE_CONTRACTID_PREFIX "robots", &kNS_BROWSER_ABOUT_REDIRECTOR_CID },
    { NS_ABOUT_MODULE_CONTRACTID_PREFIX "sessionrestore", &kNS_BROWSER_ABOUT_REDIRECTOR_CID },
#ifdef MOZ_SERVICES_SYNC
    { NS_ABOUT_MODULE_CONTRACTID_PREFIX "sync-tabs", &kNS_BROWSER_ABOUT_REDIRECTOR_CID },
#endif
    { NS_ABOUT_MODULE_CONTRACTID_PREFIX "home", &kNS_BROWSER_ABOUT_REDIRECTOR_CID },
#if !defined(WINCE) && !defined(MOZ_WIDGET_UIKIT)
    { NS_PROFILEMIGRATOR_CONTRACTID, &kNS_FIREFOX_PROFILEMIGRATOR_CID },
#if defined(XP_WIN) && !defined(__MINGW32__)
    { NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "ie", &kNS_WINIEPROFILEMIGRATOR_CID },
#elif defined(MOZ_WIDGET_COCOA)
    { NS_SHELLSERVICE_CONTRACTID, &kNS_SHELLSERVICE_CID },
    { NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "safari", &kNS_SAFARIPROFILEMIGRATOR_CID },
    { NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "macie", &kNS_MACIEPROFILEMIGRATOR_CID },
    { NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "omniweb", &kNS_OMNIWEBPROFILEMIGRATOR_CID },
    { NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "camino", &kNS_CAMINOPROFILEMIGRATOR_CID },
    { NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "icab", &kNS_ICABPROFILEMIGRATOR_CID },
#endif
#if !defined(XP_OS2)
    { NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "opera", &kNS_OPERAPROFILEMIGRATOR_CID },
#endif
#if !defined(XP_BEOS)
    { NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "dogbert", &kNS_DOGBERTPROFILEMIGRATOR_CID },
#endif
    { NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "phoenix", &kNS_PHOENIXPROFILEMIGRATOR_CID },
    { NS_BROWSERPROFILEMIGRATOR_CONTRACTID_PREFIX "seamonkey", &kNS_SEAMONKEYPROFILEMIGRATOR_CID },
#endif /* WINCE || UIKit */
    { NS_PRIVATE_BROWSING_SERVICE_CONTRACTID, &kNS_PRIVATE_BROWSING_SERVICE_WRAPPER_CID },
    { NULL }
};

static const mozilla::Module::CategoryEntry kBrowserCategories[] = {
    { XPCOM_DIRECTORY_PROVIDER_CATEGORY, "browser-directory-provider", NS_BROWSERDIRECTORYPROVIDER_CONTRACTID },
    { NS_CONTENT_SNIFFER_CATEGORY, "Feed Sniffer", NS_FEEDSNIFFER_CONTRACTID },
    { NULL }
};

static const mozilla::Module kBrowserModule = {
    mozilla::Module::kVersion,
    kBrowserCIDs,
    kBrowserContracts,
    kBrowserCategories
};

NSMODULE_DEFN(nsBrowserCompsModule) = &kBrowserModule;

