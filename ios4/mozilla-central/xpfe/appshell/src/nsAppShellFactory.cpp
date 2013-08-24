/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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
#include "nscore.h"
#include "nsIWindowMediator.h"
#include "nsAbout.h"

#include "nsIAppShellService.h"
#include "nsAppShellService.h"
#include "nsWindowMediator.h"
#include "nsChromeTreeOwner.h"
#include "nsAppShellCID.h"

NS_GENERIC_FACTORY_CONSTRUCTOR(nsAppShellService)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsAbout)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsWindowMediator, Init)

NS_DEFINE_NAMED_CID(NS_APPSHELLSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_WINDOWMEDIATOR_CID);
NS_DEFINE_NAMED_CID(NS_ABOUT_CID);

static const mozilla::Module::CIDEntry kAppShellCIDs[] = {
  { &kNS_APPSHELLSERVICE_CID, false, NULL, nsAppShellServiceConstructor },
  { &kNS_WINDOWMEDIATOR_CID, false, NULL, nsWindowMediatorConstructor },
  { &kNS_ABOUT_CID, false, NULL, nsAboutConstructor },
  { NULL }
};

static const mozilla::Module::ContractIDEntry kAppShellContracts[] = {
  { NS_APPSHELLSERVICE_CONTRACTID, &kNS_APPSHELLSERVICE_CID },
  { NS_WINDOWMEDIATOR_CONTRACTID, &kNS_WINDOWMEDIATOR_CID },
  { NS_ABOUT_MODULE_CONTRACTID_PREFIX, &kNS_ABOUT_CID },
  { NULL }
};

static nsresult
nsAppShellModuleConstructor()
{
  return nsChromeTreeOwner::InitGlobals();
}

static void
nsAppShellModuleDestructor()
{
  nsChromeTreeOwner::FreeGlobals();
}

static const mozilla::Module kAppShellModule = {
  mozilla::Module::kVersion,
  kAppShellCIDs,
  kAppShellContracts,
  NULL,
  NULL,
  nsAppShellModuleConstructor,
  nsAppShellModuleDestructor
};

NSMODULE_DEFN(appshell) = &kAppShellModule;
