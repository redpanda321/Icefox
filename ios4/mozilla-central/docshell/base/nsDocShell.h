/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=80:
 *
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
 * The Original Code is the Mozilla browser.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications, Inc.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Travis Bogard <travis@netscape.com>
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

#ifndef nsDocShell_h__
#define nsDocShell_h__

#include "nsIDOMNode.h"
#include "nsIDOMNodeList.h"
#include "nsIContentViewer.h"
#include "nsIPrefBranch.h"
#include "nsInterfaceHashtable.h"
#include "nsIScriptContext.h"
#include "nsITimer.h"

#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShellTreeNode.h"
#include "nsIBaseWindow.h"
#include "nsIScrollable.h"
#include "nsITextScroll.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIContentViewerContainer.h"
#include "nsIDeviceContext.h"

#include "nsDocLoader.h"
#include "nsIURILoader.h"
#include "nsIEditorDocShell.h"

#include "nsWeakReference.h"

// Local Includes
#include "nsDSURIContentListener.h"
#include "nsDocShellEditorData.h"

// Helper Classes
#include "nsCOMPtr.h"
#include "nsPoint.h" // mCurrent/mDefaultScrollbarPreferences
#include "nsString.h"
#include "nsAutoPtr.h"
#include "nsThreadUtils.h"

// Threshold value in ms for META refresh based redirects
#define REFRESH_REDIRECT_TIMER 15000

// Interfaces Needed
#include "nsIDocumentCharsetInfo.h"
#include "nsIDocCharset.h"
#include "nsIGlobalHistory2.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIPrompt.h"
#include "nsIRefreshURI.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptGlobalObjectOwner.h"
#include "nsISHistory.h"
#include "nsILayoutHistoryState.h"
#include "nsIStringBundle.h"
#include "nsISupportsArray.h"
#include "nsIWebNavigation.h"
#include "nsIWebPageDescriptor.h"
#include "nsIWebProgressListener.h"
#include "nsISHContainer.h"
#include "nsIDocShellLoadInfo.h"
#include "nsIDocShellHistory.h"
#include "nsIURIFixup.h"
#include "nsIWebBrowserFind.h"
#include "nsIHttpChannel.h"
#include "nsDocShellTransferableHooks.h"
#include "nsIAuthPromptProvider.h"
#include "nsISecureBrowserUI.h"
#include "nsIObserver.h"
#include "nsDocShellLoadTypes.h"
#include "nsPIDOMEventTarget.h"
#include "nsILoadContext.h"
#include "nsIWidget.h"
#include "nsIWebShellServices.h"
#include "nsILinkHandler.h"
#include "nsIClipboardCommands.h"
#include "nsICommandManager.h"
#include "nsCRT.h"

class nsDocShell;
class nsIController;
class OnLinkClickEvent;
class nsIScrollableFrame;

/* load commands were moved to nsIDocShell.h */
/* load types were moved to nsDocShellLoadTypes.h */

/* internally used ViewMode types */
enum ViewMode {
    viewNormal = 0x0,
    viewSource = 0x1
};

//*****************************************************************************
//***    nsRefreshTimer
//*****************************************************************************

class nsRefreshTimer : public nsITimerCallback
{
public:
    nsRefreshTimer();

    NS_DECL_ISUPPORTS
    NS_DECL_NSITIMERCALLBACK

    PRInt32 GetDelay() { return mDelay ;}

    nsRefPtr<nsDocShell>  mDocShell;
    nsCOMPtr<nsIURI>      mURI;
    PRInt32               mDelay;
    PRPackedBool          mRepeat;
    PRPackedBool          mMetaRefresh;
    
protected:
    virtual ~nsRefreshTimer();
};

#define NS_ERROR_DOCSHELL_REQUEST_REJECTED  NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_GENERAL,1001)

typedef enum {
    eCharsetReloadInit,
    eCharsetReloadRequested,
    eCharsetReloadStopOrigional
} eCharsetReloadState;

//*****************************************************************************
//***    nsDocShell
//*****************************************************************************

class nsDocShell : public nsDocLoader,
                   public nsIDocShell,
                   public nsIDocShellTreeItem, 
                   public nsIDocShellHistory,
                   public nsIWebNavigation,
                   public nsIBaseWindow, 
                   public nsIScrollable, 
                   public nsITextScroll, 
                   public nsIDocCharset, 
                   public nsIContentViewerContainer,
                   public nsIScriptGlobalObjectOwner,
                   public nsIRefreshURI,
                   public nsIWebProgressListener,
                   public nsIEditorDocShell,
                   public nsIWebPageDescriptor,
                   public nsIAuthPromptProvider,
                   public nsIObserver,
                   public nsILoadContext,
                   public nsIWebShellServices,
                   public nsILinkHandler,
                   public nsIClipboardCommands
{
    friend class nsDSURIContentListener;

public:
    // Object Management
    nsDocShell();

    NS_DECL_AND_IMPL_ZEROING_OPERATOR_NEW

    virtual nsresult Init();

    NS_DECL_ISUPPORTS_INHERITED

    NS_DECL_NSIDOCSHELL
    NS_DECL_NSIDOCSHELLTREEITEM
    NS_DECL_NSIDOCSHELLTREENODE
    NS_DECL_NSIDOCSHELLHISTORY
    NS_DECL_NSIWEBNAVIGATION
    NS_DECL_NSIBASEWINDOW
    NS_DECL_NSISCROLLABLE
    NS_DECL_NSITEXTSCROLL
    NS_DECL_NSIDOCCHARSET
    NS_DECL_NSIINTERFACEREQUESTOR
    NS_DECL_NSIWEBPROGRESSLISTENER
    NS_DECL_NSIREFRESHURI
    NS_DECL_NSICONTENTVIEWERCONTAINER
    NS_DECL_NSIEDITORDOCSHELL
    NS_DECL_NSIWEBPAGEDESCRIPTOR
    NS_DECL_NSIAUTHPROMPTPROVIDER
    NS_DECL_NSIOBSERVER
    NS_DECL_NSILOADCONTEXT
    NS_DECL_NSICLIPBOARDCOMMANDS
    NS_DECL_NSIWEBSHELLSERVICES

    NS_IMETHOD Stop() {
        // Need this here because otherwise nsIWebNavigation::Stop
        // overrides the docloader's Stop()
        return nsDocLoader::Stop();
    }

    // Need to implement (and forward) nsISecurityEventSink, because
    // nsIWebProgressListener has methods with identical names...
    NS_FORWARD_NSISECURITYEVENTSINK(nsDocLoader::)

    // nsILinkHandler
    NS_IMETHOD OnLinkClick(nsIContent* aContent,
        nsIURI* aURI,
        const PRUnichar* aTargetSpec,
        nsIInputStream* aPostDataStream = 0,
        nsIInputStream* aHeadersDataStream = 0);
    NS_IMETHOD OnLinkClickSync(nsIContent* aContent,
        nsIURI* aURI,
        const PRUnichar* aTargetSpec,
        nsIInputStream* aPostDataStream = 0,
        nsIInputStream* aHeadersDataStream = 0,
        nsIDocShell** aDocShell = 0,
        nsIRequest** aRequest = 0);
    NS_IMETHOD OnOverLink(nsIContent* aContent,
        nsIURI* aURI,
        const PRUnichar* aTargetSpec);
    NS_IMETHOD OnLeaveLink();

    nsDocShellInfoLoadType ConvertLoadTypeToDocShellLoadInfo(PRUint32 aLoadType);
    PRUint32 ConvertDocShellLoadInfoToLoadType(nsDocShellInfoLoadType aDocShellLoadType);

    // nsIScriptGlobalObjectOwner methods
    virtual nsIScriptGlobalObject* GetScriptGlobalObject();

    // Restores a cached presentation from history (mLSHE).
    // This method swaps out the content viewer and simulates loads for
    // subframes.  It then simulates the completion of the toplevel load.
    nsresult RestoreFromHistory();

    // Perform a URI load from a refresh timer.  This is just like the
    // ForceRefreshURI method on nsIRefreshURI, but makes sure to take
    // the timer involved out of mRefreshURIList if it's there.
    // aTimer must not be null.
    nsresult ForceRefreshURIFromTimer(nsIURI * aURI, PRInt32 aDelay,
                                      PRBool aMetaRefresh, nsITimer* aTimer);

    friend class OnLinkClickEvent;

protected:
    // Object Management
    virtual ~nsDocShell();
    virtual void DestroyChildren();

    // Content Viewer Management
    NS_IMETHOD EnsureContentViewer();
    // aPrincipal can be passed in if the caller wants.  If null is
    // passed in, the about:blank principal will end up being used.
    nsresult CreateAboutBlankContentViewer(nsIPrincipal* aPrincipal,
                                           nsIURI* aBaseURI);
    NS_IMETHOD CreateContentViewer(const char * aContentType, 
        nsIRequest * request, nsIStreamListener ** aContentHandler);
    NS_IMETHOD NewContentViewerObj(const char * aContentType, 
        nsIRequest * request, nsILoadGroup * aLoadGroup, 
        nsIStreamListener ** aContentHandler, nsIContentViewer ** aViewer);
    NS_IMETHOD SetupNewViewer(nsIContentViewer * aNewViewer);

    void SetupReferrerFromChannel(nsIChannel * aChannel);
    
    NS_IMETHOD GetEldestPresContext(nsPresContext** aPresContext);

    // Get the principal that we'll set on the channel if we're inheriting.  If
    // aConsiderCurrentDocument is true, we try to use the current document if
    // at all possible.  If that fails, we fall back on the parent document.
    // If that fails too, we force creation of a content viewer and use the
    // resulting principal.  If aConsiderCurrentDocument is false, we just look
    // at the parent.
    nsIPrincipal* GetInheritedPrincipal(PRBool aConsiderCurrentDocument);

    // True if when loading aURI into this docshell, the channel should look
    // for an appropriate application cache.
    PRBool ShouldCheckAppCache(nsIURI * aURI);

    // Actually open a channel and perform a URI load.  Note: whatever owner is
    // passed to this function will be set on the channel.  Callers who wish to
    // not have an owner on the channel should just pass null.
    virtual nsresult DoURILoad(nsIURI * aURI,
                               nsIURI * aReferrer,
                               PRBool aSendReferrer,
                               nsISupports * aOwner,
                               const char * aTypeHint,
                               nsIInputStream * aPostData,
                               nsIInputStream * aHeadersData,
                               PRBool firstParty,
                               nsIDocShell ** aDocShell,
                               nsIRequest ** aRequest,
                               PRBool aIsNewWindowTarget,
                               PRBool aBypassClassifier,
                               PRBool aForceAllowCookies);
    NS_IMETHOD AddHeadersToChannel(nsIInputStream * aHeadersData, 
                                  nsIChannel * aChannel);
    virtual nsresult DoChannelLoad(nsIChannel * aChannel,
                                   nsIURILoader * aURILoader,
                                   PRBool aBypassClassifier);

    nsresult ScrollIfAnchor(nsIURI * aURI, PRBool * aWasAnchor,
                            PRUint32 aLoadType, PRBool * aDoHashchange);

    // Tries to stringify a given variant by converting it to JSON.  This only
    // works if the variant is backed by a JSVal.
    nsresult StringifyJSValVariant(nsIVariant *aData, nsAString &aResult);

    // Returns PR_TRUE if would have called FireOnLocationChange,
    // but did not because aFireOnLocationChange was false on entry.
    // In this case it is the caller's responsibility to ensure
    // FireOnLocationChange is called.
    // In all other cases PR_FALSE is returned.
    PRBool OnLoadingSite(nsIChannel * aChannel,
                         PRBool aFireOnLocationChange,
                         PRBool aAddToGlobalHistory = PR_TRUE);

    // Returns PR_TRUE if would have called FireOnLocationChange,
    // but did not because aFireOnLocationChange was false on entry.
    // In this case it is the caller's responsibility to ensure
    // FireOnLocationChange is called.
    // In all other cases PR_FALSE is returned.
    // Either aChannel or aOwner must be null.  If aChannel is
    // present, the owner should be gotten from it.
    PRBool OnNewURI(nsIURI * aURI, nsIChannel * aChannel, nsISupports* aOwner,
                    PRUint32 aLoadType,
                    PRBool aFireOnLocationChange,
                    PRBool aAddToGlobalHistory = PR_TRUE);

    virtual void SetReferrerURI(nsIURI * aURI);

    // Session History
    virtual PRBool ShouldAddToSessionHistory(nsIURI * aURI);
    // Either aChannel or aOwner must be null.  If aChannel is
    // present, the owner should be gotten from it.
    virtual nsresult AddToSessionHistory(nsIURI * aURI, nsIChannel * aChannel,
                                         nsISupports* aOwner,
                                         nsISHEntry ** aNewEntry);
    nsresult DoAddChildSHEntry(nsISHEntry* aNewEntry, PRInt32 aChildOffset);

    NS_IMETHOD LoadHistoryEntry(nsISHEntry * aEntry, PRUint32 aLoadType);
    NS_IMETHOD PersistLayoutHistoryState();

    // Clone a session history tree for subframe navigation.
    // The tree rooted at |aSrcEntry| will be cloned into |aDestEntry|, except
    // for the entry with id |aCloneID|, which will be replaced with
    // |aReplaceEntry|.  |aSrcShell| is a (possibly null) docshell which
    // corresponds to |aSrcEntry| via its mLSHE or mOHE pointers, and will
    // have that pointer updated to point to the cloned history entry.
    static nsresult CloneAndReplace(nsISHEntry *aSrcEntry,
                                    nsDocShell *aSrcShell,
                                    PRUint32 aCloneID,
                                    nsISHEntry *aReplaceEntry,
                                    nsISHEntry **aDestEntry);

    // Child-walking callback for CloneAndReplace
    static nsresult CloneAndReplaceChild(nsISHEntry *aEntry,
                                         nsDocShell *aShell,
                                         PRInt32 aChildIndex, void *aData);

    nsresult GetRootSessionHistory(nsISHistory ** aReturn);
    nsresult GetHttpChannel(nsIChannel * aChannel, nsIHttpChannel ** aReturn);
    PRBool ShouldDiscardLayoutState(nsIHttpChannel * aChannel);

    // Determine whether this docshell corresponds to the given history entry,
    // via having a pointer to it in mOSHE or mLSHE.
    PRBool HasHistoryEntry(nsISHEntry *aEntry) const
    {
        return aEntry && (aEntry == mOSHE || aEntry == mLSHE);
    }

    // Update any pointers (mOSHE or mLSHE) to aOldEntry to point to aNewEntry
    void SwapHistoryEntries(nsISHEntry *aOldEntry, nsISHEntry *aNewEntry);

    // Call this method to swap in a new history entry to m[OL]SHE, rather than
    // setting it directly.  This completes the navigation in all docshells
    // in the case of a subframe navigation.
    void SetHistoryEntry(nsCOMPtr<nsISHEntry> *aPtr, nsISHEntry *aEntry);

    // Child-walking callback for SetHistoryEntry
    static nsresult SetChildHistoryEntry(nsISHEntry *aEntry,
                                         nsDocShell *aShell,
                                         PRInt32 aEntryIndex, void *aData);

    // Callback prototype for WalkHistoryEntries.
    // aEntry is the child history entry, aShell is its corresponding docshell,
    // aChildIndex is the child's index in its parent entry, and aData is
    // the opaque pointer passed to WalkHistoryEntries.
    typedef nsresult (*WalkHistoryEntriesFunc)(nsISHEntry *aEntry,
                                               nsDocShell *aShell,
                                               PRInt32 aChildIndex,
                                               void *aData);

    // For each child of aRootEntry, find the corresponding docshell which is
    // a child of aRootShell, and call aCallback.  The opaque pointer aData
    // is passed to the callback.
    static nsresult WalkHistoryEntries(nsISHEntry *aRootEntry,
                                       nsDocShell *aRootShell,
                                       WalkHistoryEntriesFunc aCallback,
                                       void *aData);

    // overridden from nsDocLoader, this provides more information than the
    // normal OnStateChange with flags STATE_REDIRECTING
    virtual void OnRedirectStateChange(nsIChannel* aOldChannel,
                                       nsIChannel* aNewChannel,
                                       PRUint32 aRedirectFlags,
                                       PRUint32 aStateFlags);

    /**
     * Helper function that determines if channel is an HTTP POST.
     *
     * @param aChannel
     *        The channel to test
     *
     * @return True iff channel is an HTTP post.
     */
    bool ChannelIsPost(nsIChannel* aChannel);

    /**
     * Helper function that finds the last URI and its transition flags for a
     * channel.
     *
     * This method first checks the channel's property bag to see if previous
     * info has been saved.  If not, it gives back the referrer of the channel.
     *
     * @param aChannel
     *        The channel we are transitioning to
     * @param aURI
     *        Output parameter with the previous URI, not addref'd
     * @param aChannelRedirectFlags
     *        If a redirect, output parameter with the previous redirect flags
     *        from nsIChannelEventSink
     */
    void ExtractLastVisit(nsIChannel* aChannel,
                          nsIURI** aURI,
                          PRUint32* aChannelRedirectFlags);

    /**
     * Helper function that caches a URI and a transition for saving later.
     *
     * @param aChannel
     *        Channel that will have these properties saved
     * @param aURI
     *        The URI to save for later
     * @param aChannelRedirectFlags
     *        The nsIChannelEventSink redirect flags to save for later
     */
    void SaveLastVisit(nsIChannel* aChannel,
                       nsIURI* aURI,
                       PRUint32 aChannelRedirectFlags);

    /**
     * Helper function for adding a URI visit using IHistory.  If IHistory is
     * not available, the method tries nsIGlobalHistory2.
     *
     * The IHistory API maintains chains of visits, tracking both HTTP referrers
     * and redirects for a user session. VisitURI requires the current URI and
     * the previous URI in the chain.
     *
     * Visits can be saved either during a redirect or when the request has
     * reached its final destination.  The previous URI in the visit may be
     * from another redirect or it may be the referrer.
     *
     * @pre aURI is not null.
     *
     * @param aURI
     *        The URI that was just visited
     * @param aReferrerURI
     *        The referrer URI of this request
     * @param aPreviousURI
     *        The previous URI of this visit (may be the same as aReferrerURI)
     * @param aChannelRedirectFlags
     *        For redirects, the redirect flags from nsIChannelEventSink
     *        (0 otherwise)
     */
    void AddURIVisit(nsIURI* aURI,
                     nsIURI* aReferrerURI,
                     nsIURI* aPreviousURI,
                     PRUint32 aChannelRedirectFlags);

    // Helper Routines
    nsresult   ConfirmRepost(PRBool * aRepost);
    NS_IMETHOD GetPromptAndStringBundle(nsIPrompt ** aPrompt,
        nsIStringBundle ** aStringBundle);
    NS_IMETHOD GetChildOffset(nsIDOMNode * aChild, nsIDOMNode * aParent,
        PRInt32 * aOffset);
    nsIScrollableFrame* GetRootScrollFrame();
    NS_IMETHOD EnsureScriptEnvironment();
    NS_IMETHOD EnsureEditorData();
    nsresult   EnsureTransferableHookData();
    NS_IMETHOD EnsureFind();
    nsresult   RefreshURIFromQueue();
    NS_IMETHOD DisplayLoadError(nsresult aError, nsIURI *aURI,
                                const PRUnichar *aURL,
                                nsIChannel* aFailedChannel = nsnull);
    NS_IMETHOD LoadErrorPage(nsIURI *aURI, const PRUnichar *aURL,
                             const char *aErrorPage,
                             const PRUnichar *aErrorType,
                             const PRUnichar *aDescription,
                             const char *aCSSClass,
                             nsIChannel* aFailedChannel);
    PRBool IsNavigationAllowed(PRBool aDisplayPrintErrorDialog = PR_TRUE);
    PRBool IsPrintingOrPP(PRBool aDisplayErrorDialog = PR_TRUE);

    nsresult SetBaseUrlForWyciwyg(nsIContentViewer * aContentViewer);

    static  inline  PRUint32
    PRTimeToSeconds(PRTime t_usec)
    {
      PRTime usec_per_sec;
      PRUint32 t_sec;
      LL_I2L(usec_per_sec, PR_USEC_PER_SEC);
      LL_DIV(t_usec, t_usec, usec_per_sec);
      LL_L2I(t_sec, t_usec);
      return t_sec;
    }

    PRBool IsFrame();

    //
    // Helper method that is called when a new document (including any
    // sub-documents - ie. frames) has been completely loaded.
    //
    virtual nsresult EndPageLoad(nsIWebProgress * aProgress,
                                 nsIChannel * aChannel,
                                 nsresult aResult);

    // Sets the current document's pending state object to the given SHEntry's
    // state object.  The pending state object is eventually given to the page
    // in the PopState event.
    nsresult SetDocPendingStateObj(nsISHEntry *shEntry);

    nsresult CheckLoadingPermissions();

    // Security checks to prevent frameset spoofing.  See comments at
    // implementation sites.
    static PRBool CanAccessItem(nsIDocShellTreeItem* aTargetItem,
                                nsIDocShellTreeItem* aAccessingItem,
                                PRBool aConsiderOpener = PR_TRUE);
    static PRBool ValidateOrigin(nsIDocShellTreeItem* aOriginTreeItem,
                                 nsIDocShellTreeItem* aTargetTreeItem);

    // Returns PR_TRUE if would have called FireOnLocationChange,
    // but did not because aFireOnLocationChange was false on entry.
    // In this case it is the caller's responsibility to ensure
    // FireOnLocationChange is called.
    // In all other cases PR_FALSE is returned.
    PRBool SetCurrentURI(nsIURI *aURI, nsIRequest *aRequest,
                         PRBool aFireOnLocationChange);

    // The following methods deal with saving and restoring content viewers
    // in session history.

    // mContentViewer points to the current content viewer associated with
    // this docshell.  When loading a new document, the content viewer is
    // either destroyed or stored into a session history entry.  To make sure
    // that destruction happens in a controlled fashion, a given content viewer
    // is always owned in exactly one of these ways:
    //   1) The content viewer is active and owned by a docshell's
    //      mContentViewer.
    //   2) The content viewer is still being displayed while we begin loading
    //      a new document.  The content viewer is owned by the _new_
    //      content viewer's mPreviousViewer, and has a pointer to the
    //      nsISHEntry where it will eventually be stored.  The content viewer
    //      has been close()d by the docshell, which detaches the document from
    //      the window object.
    //   3) The content viewer is cached in session history.  The nsISHEntry
    //      has the only owning reference to the content viewer.  The viewer
    //      has released its nsISHEntry pointer to prevent circular ownership.
    //
    // When restoring a content viewer from session history, open() is called
    // to reattach the document to the window object.  The content viewer is
    // then placed into mContentViewer and removed from the history entry.
    // (mContentViewer is put into session history as described above, if
    // applicable).

    // Determines whether we can safely cache the current mContentViewer in
    // session history.  This checks a number of factors such as cache policy,
    // pending requests, and unload handlers.
    // |aLoadType| should be the load type that will replace the current
    // presentation.  |aNewRequest| should be the request for the document to
    // be loaded in place of the current document, or null if such a request
    // has not been created yet. |aNewDocument| should be the document that will
    // replace the current document.
    PRBool CanSavePresentation(PRUint32 aLoadType,
                               nsIRequest *aNewRequest,
                               nsIDocument *aNewDocument);

    // Captures the state of the supporting elements of the presentation
    // (the "window" object, docshell tree, meta-refresh loads, and security
    // state) and stores them on |mOSHE|.
    nsresult CaptureState();

    // Begin the toplevel restore process for |aSHEntry|.
    // This simulates a channel open, and defers the real work until
    // RestoreFromHistory is called from a PLEvent.
    nsresult RestorePresentation(nsISHEntry *aSHEntry, PRBool *aRestoring);

    // Call BeginRestore(nsnull, PR_FALSE) for each child of this shell.
    nsresult BeginRestoreChildren();

    // Method to get our current position and size without flushing
    void DoGetPositionAndSize(PRInt32 * x, PRInt32 * y, PRInt32 * cx,
                              PRInt32 * cy);
    
    // Check whether aURI should inherit our security context
    static nsresult URIInheritsSecurityContext(nsIURI* aURI, PRBool* aResult);

    // Check whether aURI is a URI_IS_LOCAL_FILE or not
    static PRBool URIIsLocalFile(nsIURI *aURI);

    // Check whether aURI is about:blank
    static PRBool IsAboutBlank(nsIURI* aURI);

    // Call this when a URI load is handed to us (via OnLinkClick or
    // InternalLoad).  This makes sure that we're not inside unload, or that if
    // we are it's still OK to load this URI.
    PRBool IsOKToLoadURI(nsIURI* aURI);
    
    void ReattachEditorToWindow(nsISHEntry *aSHEntry);

    nsresult GetSessionStorageForURI(nsIURI* aURI,
                                     const nsSubstring& aDocumentURI,
                                     PRBool create,
                                     nsIDOMStorage** aStorage);

    // helpers for executing commands
    nsresult GetControllerForCommand(const char *inCommand,
                                     nsIController** outController);
    nsresult IsCommandEnabled(const char * inCommand, PRBool* outEnabled);
    nsresult DoCommand(const char * inCommand);
    nsresult EnsureCommandHandler();

    nsIChannel* GetCurrentDocChannel();
protected:
    // Override the parent setter from nsDocLoader
    virtual nsresult SetDocLoaderParent(nsDocLoader * aLoader);

    void ClearFrameHistory(nsISHEntry* aEntry);

    // Event type dispatched by RestorePresentation
    class RestorePresentationEvent : public nsRunnable {
    public:
        NS_DECL_NSIRUNNABLE
        RestorePresentationEvent(nsDocShell *ds) : mDocShell(ds) {}
        void Revoke() { mDocShell = nsnull; }
    private:
        nsRefPtr<nsDocShell> mDocShell;
    };

    // hash of session storages, keyed by domain
    nsInterfaceHashtable<nsCStringHashKey, nsIDOMStorage> mStorages;

    // Dimensions of the docshell
    nsIntRect                  mBounds;
    nsString                   mName;
    nsString                   mTitle;

    /**
     * Content-Type Hint of the most-recently initiated load. Used for
     * session history entries.
     */
    nsCString                  mContentTypeHint;
    nsIntPoint                 mDefaultScrollbarPref; // persistent across doc loads

    nsCOMPtr<nsISupportsArray> mRefreshURIList;
    nsCOMPtr<nsISupportsArray> mSavedRefreshURIList;
    nsRefPtr<nsDSURIContentListener> mContentListener;
    nsCOMPtr<nsIContentViewer> mContentViewer;
    nsCOMPtr<nsIDocumentCharsetInfo> mDocumentCharsetInfo;
    nsCOMPtr<nsIWidget>        mParentWidget;
    nsCOMPtr<nsIPrefBranch>    mPrefs;

    // mCurrentURI should be marked immutable on set if possible.
    nsCOMPtr<nsIURI>           mCurrentURI;
    nsCOMPtr<nsIURI>           mReferrerURI;
    nsCOMPtr<nsIScriptGlobalObject> mScriptGlobal;
    nsCOMPtr<nsISHistory>      mSessionHistory;
    nsCOMPtr<nsIGlobalHistory2> mGlobalHistory;
    nsCOMPtr<nsIWebBrowserFind> mFind;
    nsCOMPtr<nsICommandManager> mCommandManager;
    // Reference to the SHEntry for this docshell until the page is destroyed.
    // Somebody give me better name
    nsCOMPtr<nsISHEntry>       mOSHE;
    // Reference to the SHEntry for this docshell until the page is loaded
    // Somebody give me better name
    nsCOMPtr<nsISHEntry>       mLSHE;

    // Holds a weak pointer to a RestorePresentationEvent object if any that
    // holds a weak pointer back to us.  We use this pointer to possibly revoke
    // the event whenever necessary.
    nsRevocableEventPtr<RestorePresentationEvent> mRestorePresentationEvent;

    // Editor data, if this document is designMode or contentEditable.
    nsAutoPtr<nsDocShellEditorData> mEditorData;

    // Transferable hooks/callbacks
    nsCOMPtr<nsIClipboardDragDropHookList> mTransferableHookData;

    // Secure browser UI object
    nsCOMPtr<nsISecureBrowserUI> mSecurityUI;

    // The URI we're currently loading.  This is only relevant during the
    // firing of a pagehide/unload.  The caller of FirePageHideNotification()
    // is responsible for setting it and unsetting it.  It may be null if the
    // pagehide/unload is happening for some reason other than just loading a
    // new URI.
    nsCOMPtr<nsIURI>           mLoadingURI;

    // Set in LoadErrorPage from the method argument and used later
    // in CreateContentViewer. We have to delay an shistory entry creation
    // for which these objects are needed.
    nsCOMPtr<nsIURI>           mFailedURI;
    nsCOMPtr<nsIChannel>       mFailedChannel;
    PRUint32                   mFailedLoadType;

    // WEAK REFERENCES BELOW HERE.
    // Note these are intentionally not addrefd.  Doing so will create a cycle.
    // For that reasons don't use nsCOMPtr.

    nsIDocShellTreeOwner *     mTreeOwner; // Weak Reference
    nsPIDOMEventTarget *       mChromeEventHandler; //Weak Reference

    eCharsetReloadState        mCharsetReloadState;

    // Offset in the parent's child list.
    // -1 if the docshell is added dynamically to the parent shell.
    PRUint32                   mChildOffset;
    PRUint32                   mBusyFlags;
    PRUint32                   mAppType;
    PRUint32                   mLoadType;

    PRInt32                    mMarginWidth;
    PRInt32                    mMarginHeight;

    // This can either be a content docshell or a chrome docshell.  After
    // Create() is called, the type is not expected to change.
    PRInt32                    mItemType;

    // Index into the SHTransaction list, indicating the previous and current
    // transaction at the time that this DocShell begins to load
    PRInt32                    mPreviousTransIndex;
    PRInt32                    mLoadedTransIndex;

    PRPackedBool               mAllowSubframes;
    PRPackedBool               mAllowPlugins;
    PRPackedBool               mAllowJavascript;
    PRPackedBool               mAllowMetaRedirects;
    PRPackedBool               mAllowImages;
    PRPackedBool               mAllowDNSPrefetch;
    PRPackedBool               mCreatingDocument; // (should be) debugging only
    PRPackedBool               mUseErrorPages;
    PRPackedBool               mObserveErrorPages;
    PRPackedBool               mAllowAuth;
    PRPackedBool               mAllowKeywordFixup;
    PRPackedBool               mIsOffScreenBrowser;
    PRPackedBool               mIsActive;

    // This boolean is set to true right before we fire pagehide and generally
    // unset when we embed a new content viewer.  While it's true no navigation
    // is allowed in this docshell.
    PRPackedBool               mFiredUnloadEvent;

    // this flag is for bug #21358. a docshell may load many urls
    // which don't result in new documents being created (i.e. a new
    // content viewer) we want to make sure we don't call a on load
    // event more than once for a given content viewer.
    PRPackedBool               mEODForCurrentDocument;
    PRPackedBool               mURIResultedInDocument;

    PRPackedBool               mIsBeingDestroyed;

    PRPackedBool               mIsExecutingOnLoadHandler;

    // Indicates that a DocShell in this "docshell tree" is printing
    PRPackedBool               mIsPrintingOrPP;

    // Indicates to CreateContentViewer() that it is safe to cache the old
    // presentation of the page, and to SetupNewViewer() that the old viewer
    // should be passed a SHEntry to save itself into.
    PRPackedBool               mSavingOldViewer;

    // @see nsIDocShellHistory::createdDynamically
    PRPackedBool               mDynamicallyCreated;
#ifdef DEBUG
    PRPackedBool               mInEnsureScriptEnv;
#endif
    PRUint64                   mHistoryID;

    static nsIURIFixup *sURIFixup;

#ifdef DEBUG
private:
    // We're counting the number of |nsDocShells| to help find leaks
    static unsigned long gNumberOfDocShells;
#endif /* DEBUG */

public:
    class InterfaceRequestorProxy : public nsIInterfaceRequestor {
    public:
        InterfaceRequestorProxy(nsIInterfaceRequestor* p);
        virtual ~InterfaceRequestorProxy();
        NS_DECL_ISUPPORTS
        NS_DECL_NSIINTERFACEREQUESTOR
 
    protected:
        InterfaceRequestorProxy() {}
        nsWeakPtr mWeakPtr;
    };
};

#endif /* nsDocShell_h__ */
