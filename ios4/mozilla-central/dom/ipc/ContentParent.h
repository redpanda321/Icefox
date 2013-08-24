/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
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
 * The Original Code is Mozilla Content App.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#ifndef mozilla_dom_ContentParent_h
#define mozilla_dom_ContentParent_h

#include "base/waitable_event_watcher.h"

#include "mozilla/dom/PContentParent.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"

#include "nsIObserver.h"
#include "nsIThreadInternal.h"
#include "mozilla/Monitor.h"
#include "nsNetUtil.h"
#include "nsIPrefService.h"
#include "nsIPermissionManager.h"

namespace mozilla {

namespace ipc {
class TestShellParent;
}

namespace dom {

class TabParent;

class ContentParent : public PContentParent
                    , public nsIObserver
                    , public nsIThreadObserver
{
private:
    typedef mozilla::ipc::GeckoChildProcessHost GeckoChildProcessHost;
    typedef mozilla::ipc::TestShellParent TestShellParent;

public:
    static ContentParent* GetSingleton(PRBool aForceNew = PR_TRUE);

#if 0
    // TODO: implement this somewhere!
    static ContentParent* FreeSingleton();
#endif

    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER
    NS_DECL_NSITHREADOBSERVER

    TabParent* CreateTab(PRUint32 aChromeFlags);

    TestShellParent* CreateTestShell();
    bool DestroyTestShell(TestShellParent* aTestShell);

    void ReportChildAlreadyBlocked();
    bool RequestRunToCompletion();

    bool IsAlive();

protected:
    virtual void ActorDestroy(ActorDestroyReason why);

private:
    static ContentParent* gSingleton;

    // Hide the raw constructor methods since we don't want client code
    // using them.
    using PContentParent::SendPBrowserConstructor;
    using PContentParent::SendPTestShellConstructor;

    ContentParent();
    virtual ~ContentParent();

    virtual PBrowserParent* AllocPBrowser(const PRUint32& aChromeFlags);
    virtual bool DeallocPBrowser(PBrowserParent* frame);

    virtual PTestShellParent* AllocPTestShell();
    virtual bool DeallocPTestShell(PTestShellParent* shell);

    virtual PNeckoParent* AllocPNecko();
    virtual bool DeallocPNecko(PNeckoParent* necko);

    virtual bool RecvGetPrefType(const nsCString& prefName,
            PRInt32* retValue, nsresult* rv);

    virtual bool RecvGetBoolPref(const nsCString& prefName,
            PRBool* retValue, nsresult* rv);

    virtual bool RecvGetIntPref(const nsCString& prefName,
            PRInt32* retValue, nsresult* rv);

    virtual bool RecvGetCharPref(const nsCString& prefName,
            nsCString* retValue, nsresult* rv);

    virtual bool RecvGetPrefLocalizedString(const nsCString& prefName,
            nsString* retValue, nsresult* rv);

    virtual bool RecvPrefHasUserValue(const nsCString& prefName,
            PRBool* retValue, nsresult* rv);

    virtual bool RecvPrefIsLocked(const nsCString& prefName,
            PRBool* retValue, nsresult* rv);

    virtual bool RecvGetChildList(const nsCString& domain,
            nsTArray<nsCString>* list, nsresult* rv);

    virtual bool RecvTestPermission(const IPC::URI&  aUri,
                                    const nsCString& aType,
                                    const PRBool&    aExact,
                                    PRUint32*        retValue);

    void EnsurePrefService();
    void EnsurePermissionService();

    virtual bool RecvStartVisitedQuery(const IPC::URI& uri);

    virtual bool RecvVisitURI(const IPC::URI& uri,
                              const IPC::URI& referrer,
                              const PRUint32& flags);

    virtual bool RecvSetURITitle(const IPC::URI& uri,
                                 const nsString& title);
    
    virtual bool RecvNotifyIME(const int&, const int&);

    virtual bool RecvNotifyIMEChange(const nsString&, const PRUint32&, const int&, 
                               const int&, const int&)
;


    virtual bool RecvLoadURIExternal(const IPC::URI& uri);

    mozilla::Monitor mMonitor;

    GeckoChildProcessHost* mSubprocess;

    int mRunToCompletionDepth;
    bool mShouldCallUnblockChild;
    nsCOMPtr<nsIThreadObserver> mOldObserver;

    bool mIsAlive;
    nsCOMPtr<nsIPrefBranch> mPrefService; 
    nsCOMPtr<nsIPermissionManager> mPermissionService; 
};

} // namespace dom
} // namespace mozilla

#endif
