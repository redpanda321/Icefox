/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=8:
 */
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
 * Christopher Blizzard.
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Christopher Blizzard <blizzard@mozilla.org>
 *   Benjamin Smedberg <benjamin@smedbergs.us>
 *   Miika Jarvinen <mjarvin@gmail.com> 
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
#include <QWidget>
#include <QX11Info>
#include "nsQtRemoteService.h"

#include "mozilla/ModuleUtils.h"
#include "nsIServiceManager.h"
#include "nsIAppShellService.h"

#include "nsCOMPtr.h"

/**
  Helper class which is used to receive notification about property changes
*/
class MozQRemoteEventHandlerWidget: public QWidget {
public:
  /**
    Constructor
    @param aRemoteService remote service, which is notified about atom change
  */
  MozQRemoteEventHandlerWidget(nsQtRemoteService &aRemoteService);

protected:
  /**
    Event filter, which receives all XEvents
    @return false which continues event handling
  */
  bool x11Event(XEvent *);

private:
  /**
    Service which is notified about property change
  */
  nsQtRemoteService &mRemoteService;
};

MozQRemoteEventHandlerWidget::MozQRemoteEventHandlerWidget(nsQtRemoteService &aRemoteService)
  :mRemoteService(aRemoteService)
{
}

bool
MozQRemoteEventHandlerWidget::x11Event(XEvent *aEvt)
{
  if (aEvt->type == PropertyNotify && aEvt->xproperty.state == PropertyNewValue)
    mRemoteService.PropertyNotifyEvent(aEvt);

  return false;
}

NS_IMPL_ISUPPORTS2(nsQtRemoteService,
                   nsIRemoteService,
                   nsIObserver)

nsQtRemoteService::nsQtRemoteService():
mServerWindow(0)
{
}

NS_IMETHODIMP
nsQtRemoteService::Startup(const char* aAppName, const char* aProfileName)
{
#if (MOZ_PLATFORM_MAEMO == 5)
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
  if (mServerWindow) return NS_ERROR_ALREADY_INITIALIZED;
  NS_ASSERTION(aAppName, "Don't pass a null appname!");

  XRemoteBaseStartup(aAppName,aProfileName);

  //Create window, which is not shown.
  mServerWindow = new MozQRemoteEventHandlerWidget(*this);

  HandleCommandsFor(mServerWindow->winId());
  return NS_OK;
}

NS_IMETHODIMP
nsQtRemoteService::RegisterWindow(nsIDOMWindow* aWindow)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsQtRemoteService::Shutdown()
{
  if (!mServerWindow)
    return NS_ERROR_NOT_INITIALIZED;

  delete mServerWindow;
  mServerWindow = nsnull;

  return NS_OK;
}

void
nsQtRemoteService::PropertyNotifyEvent(XEvent *aEvt)
{
  HandleNewProperty(aEvt->xproperty.window,
                    QX11Info::display(),
                    aEvt->xproperty.time,
                    aEvt->xproperty.atom,
                    0);
}

void
nsQtRemoteService::SetDesktopStartupIDOrTimestamp(const nsACString& aDesktopStartupID,
                                                  PRUint32 aTimestamp)
{
}

// {C0773E90-5799-4eff-AD03-3EBCD85624AC}
#define NS_REMOTESERVICE_CID \
  { 0xc0773e90, 0x5799, 0x4eff, { 0xad, 0x3, 0x3e, 0xbc, 0xd8, 0x56, 0x24, 0xac } }

NS_GENERIC_FACTORY_CONSTRUCTOR(nsQtRemoteService)
NS_DEFINE_NAMED_CID(NS_REMOTESERVICE_CID);

static const mozilla::Module::CIDEntry kRemoteCIDs[] = {
  { &kNS_REMOTESERVICE_CID, false, NULL, nsQtRemoteServiceConstructor },
  { NULL }
};

static const mozilla::Module::ContractIDEntry kRemoteContracts[] = {
  { "@mozilla.org/toolkit/remote-service;1", &kNS_REMOTESERVICE_CID },
  { NULL }
};

static const mozilla::Module kRemoteModule = {
  mozilla::Module::kVersion,
  kRemoteCIDs,
  kRemoteContracts
};

NSMODULE_DEFN(RemoteServiceModule) = &kRemoteModule;
