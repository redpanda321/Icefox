/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=78: */
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
 *   Simon Fraser   <sfraser@netscape.com>
 *   Michael Judge  <mjudge@netscape.com>
 *   Charles Manske <cmanske@netscape.com>
 *   Kathleen Brade <brade@netscape.com>
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

#include "nsPIDOMWindow.h"
#include "nsIDOMWindowUtils.h"
#include "nsIDOMWindowInternal.h"
#include "nsIDOMNSHTMLDocument.h"
#include "nsIDocument.h"
#include "nsIHTMLDocument.h"
#include "nsIDOMDocument.h"
#include "nsIURI.h"
#include "nsISelectionPrivate.h"
#include "nsITransactionManager.h"

#include "nsIEditorDocShell.h"
#include "nsIDocShell.h"

#include "nsIChannel.h"
#include "nsIWebProgress.h"
#include "nsIWebNavigation.h"
#include "nsIRefreshURI.h"

#include "nsIControllers.h"
#include "nsIController.h"
#include "nsIControllerContext.h"
#include "nsICommandManager.h"
#include "nsPICommandUpdater.h"

#include "nsIPresShell.h"

#include "nsComposerCommandsUpdater.h"
#include "nsEditingSession.h"

#include "nsComponentManagerUtils.h"
#include "nsIInterfaceRequestorUtils.h"

#include "nsIContentViewer.h"
#include "nsISelectionController.h"
#include "nsIPlaintextEditor.h"
#include "nsIEditor.h"

#include "nsIDOMNSDocument.h"
#include "nsIScriptContext.h"
#include "imgIContainer.h"

#if DEBUG
//#define NOISY_DOC_LOADING  1
#endif

/*---------------------------------------------------------------------------

  nsEditingSession

----------------------------------------------------------------------------*/
nsEditingSession::nsEditingSession()
: mDoneSetup(PR_FALSE)
, mCanCreateEditor(PR_FALSE)
, mInteractive(PR_FALSE)
, mMakeWholeDocumentEditable(PR_TRUE)
, mDisabledJSAndPlugins(PR_FALSE)
, mScriptsEnabled(PR_TRUE)
, mPluginsEnabled(PR_TRUE)
, mProgressListenerRegistered(PR_FALSE)
, mImageAnimationMode(0)
, mEditorFlags(0)
, mEditorStatus(eEditorOK)
, mBaseCommandControllerId(0)
, mDocStateControllerId(0)
, mHTMLCommandControllerId(0)
{
}

/*---------------------------------------------------------------------------

  ~nsEditingSession

----------------------------------------------------------------------------*/
nsEditingSession::~nsEditingSession()
{
  // Must cancel previous timer?
  if (mLoadBlankDocTimer)
    mLoadBlankDocTimer->Cancel();
}

NS_IMPL_ISUPPORTS3(nsEditingSession, nsIEditingSession, nsIWebProgressListener, 
                   nsISupportsWeakReference)

/*---------------------------------------------------------------------------

  MakeWindowEditable

  aEditorType string, "html" "htmlsimple" "text" "textsimple"
  void makeWindowEditable(in nsIDOMWindow aWindow, in string aEditorType, 
                          in boolean aDoAfterUriLoad,
                          in boolean aMakeWholeDocumentEditable,
                          in boolean aInteractive);
----------------------------------------------------------------------------*/
#define DEFAULT_EDITOR_TYPE "html"

NS_IMETHODIMP
nsEditingSession::MakeWindowEditable(nsIDOMWindow *aWindow,
                                     const char *aEditorType, 
                                     PRBool aDoAfterUriLoad,
                                     PRBool aMakeWholeDocumentEditable,
                                     PRBool aInteractive)
{
  mEditorType.Truncate();
  mEditorFlags = 0;
  mWindowToBeEdited = do_GetWeakReference(aWindow);

  // disable plugins
  nsIDocShell *docShell = GetDocShellFromWindow(aWindow);
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  mInteractive = aInteractive;
  mMakeWholeDocumentEditable = aMakeWholeDocumentEditable;

  nsresult rv;
  if (!mInteractive) {
    rv = DisableJSAndPlugins(aWindow);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Always remove existing editor
  TearDownEditorOnWindow(aWindow);
  
  // Tells embedder that startup is in progress
  mEditorStatus = eEditorCreationInProgress;

  //temporary to set editor type here. we will need different classes soon.
  if (!aEditorType)
    aEditorType = DEFAULT_EDITOR_TYPE;
  mEditorType = aEditorType;

  // if all this does is setup listeners and I don't need listeners, 
  // can't this step be ignored?? (based on aDoAfterURILoad)
  rv = PrepareForEditing(aWindow);
  NS_ENSURE_SUCCESS(rv, rv);  
  
  nsCOMPtr<nsIEditorDocShell> editorDocShell;
  rv = GetEditorDocShellFromWindow(aWindow, getter_AddRefs(editorDocShell));
  NS_ENSURE_SUCCESS(rv, rv);  
  
  // set the flag on the docShell to say that it's editable
  rv = editorDocShell->MakeEditable(aDoAfterUriLoad);
  NS_ENSURE_SUCCESS(rv, rv);  

  // Setup commands common to plaintext and html editors,
  //  including the document creation observers
  // the first is an editor controller
  rv = SetupEditorCommandController("@mozilla.org/editor/editorcontroller;1",
                                    aWindow,
                                    static_cast<nsIEditingSession*>(this),
                                    &mBaseCommandControllerId);
  NS_ENSURE_SUCCESS(rv, rv);

  // The second is a controller to monitor doc state,
  // such as creation and "dirty flag"
  rv = SetupEditorCommandController("@mozilla.org/editor/editordocstatecontroller;1",
                                    aWindow,
                                    static_cast<nsIEditingSession*>(this),
                                    &mDocStateControllerId);
  NS_ENSURE_SUCCESS(rv, rv);

  // aDoAfterUriLoad can be false only when making an existing window editable
  if (!aDoAfterUriLoad)
  {
    rv = SetupEditorOnWindow(aWindow);

    // mEditorStatus is set to the error reason
    // Since this is used only when editing an existing page,
    //  it IS ok to destroy current editor
    if (NS_FAILED(rv))
      TearDownEditorOnWindow(aWindow);
  }
  return rv;
}

NS_IMETHODIMP
nsEditingSession::DisableJSAndPlugins(nsIDOMWindow *aWindow)
{
  nsIDocShell *docShell = GetDocShellFromWindow(aWindow);
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  PRBool tmp;
  nsresult rv = docShell->GetAllowJavascript(&tmp);
  NS_ENSURE_SUCCESS(rv, rv);

  mScriptsEnabled = tmp;

  rv = docShell->SetAllowJavascript(PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  // Disable plugins in this document:
  rv = docShell->GetAllowPlugins(&tmp);
  NS_ENSURE_SUCCESS(rv, rv);

  mPluginsEnabled = tmp;

  rv = docShell->SetAllowPlugins(PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  mDisabledJSAndPlugins = PR_TRUE;

  return NS_OK;
}

NS_IMETHODIMP
nsEditingSession::RestoreJSAndPlugins(nsIDOMWindow *aWindow)
{
  NS_ENSURE_TRUE(mDisabledJSAndPlugins, NS_OK);

  mDisabledJSAndPlugins = PR_FALSE;

  nsIDocShell *docShell = GetDocShellFromWindow(aWindow);
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  nsresult rv = docShell->SetAllowJavascript(mScriptsEnabled);
  NS_ENSURE_SUCCESS(rv, rv);

  // Disable plugins in this document:
  return docShell->SetAllowPlugins(mPluginsEnabled);
}

NS_IMETHODIMP
nsEditingSession::GetJsAndPluginsDisabled(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = mDisabledJSAndPlugins;
  return NS_OK;
}

/*---------------------------------------------------------------------------

  WindowIsEditable

  boolean windowIsEditable (in nsIDOMWindow aWindow);
----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::WindowIsEditable(nsIDOMWindow *aWindow, PRBool *outIsEditable)
{
  nsCOMPtr<nsIEditorDocShell> editorDocShell;
  nsresult rv = GetEditorDocShellFromWindow(aWindow,
                                            getter_AddRefs(editorDocShell));
  NS_ENSURE_SUCCESS(rv, rv);  

  return editorDocShell->GetEditable(outIsEditable);
}


// These are MIME types that are automatically parsed as "text/plain"
//   and thus we can edit them as plaintext
// Note: in older versions, we attempted to convert the mimetype of
//   the network channel for these and "text/xml" to "text/plain", 
//   but further investigation reveals that strategy doesn't work
const char* const gSupportedTextTypes[] = {
  "text/plain",
  "text/css",
  "text/rdf",
  "text/xsl",
  "text/javascript",           // obsolete type
  "text/ecmascript",           // obsolete type
  "application/javascript",
  "application/ecmascript",
  "application/x-javascript",  // obsolete type
  "text/xul",                  // obsolete type
  "application/vnd.mozilla.xul+xml",
  NULL      // IMPORTANT! Null must be at end
};

PRBool
IsSupportedTextType(const char* aMIMEType)
{
  NS_ENSURE_TRUE(aMIMEType, PR_FALSE);

  PRInt32 i = 0;
  while (gSupportedTextTypes[i])
  {
    if (strcmp(gSupportedTextTypes[i], aMIMEType) == 0)
    {
      return PR_TRUE;
    }

    i ++;
  }
  
  return PR_FALSE;
}

/*---------------------------------------------------------------------------

  SetupEditorOnWindow

  nsIEditor setupEditorOnWindow (in nsIDOMWindow aWindow);
----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::SetupEditorOnWindow(nsIDOMWindow *aWindow)
{
  mDoneSetup = PR_TRUE;

  nsresult rv;

  //MIME CHECKING
  //must get the content type
  // Note: the doc gets this from the network channel during StartPageLoad,
  //    so we don't have to get it from there ourselves
  nsCOMPtr<nsIDOMDocument> doc;
  nsCAutoString mimeCType;

  //then lets check the mime type
  if (NS_SUCCEEDED(aWindow->GetDocument(getter_AddRefs(doc))) && doc)
  {
    nsCOMPtr<nsIDOMNSDocument> nsdoc = do_QueryInterface(doc);
    if (nsdoc)
    {
      nsAutoString mimeType;
      if (NS_SUCCEEDED(nsdoc->GetContentType(mimeType)))
        AppendUTF16toUTF8(mimeType, mimeCType);

      if (IsSupportedTextType(mimeCType.get()))
      {
        mEditorType.AssignLiteral("text");
        mimeCType = "text/plain";
      }
      else if (!mimeCType.EqualsLiteral("text/html") &&
               !mimeCType.EqualsLiteral("application/xhtml+xml"))
      {
        // Neither an acceptable text or html type.
        mEditorStatus = eEditorErrorCantEditMimeType;

        // Turn editor into HTML -- we will load blank page later
        mEditorType.AssignLiteral("html");
        mimeCType.AssignLiteral("text/html");
      }
    }

    // Flush out frame construction to make sure that the subframe's
    // presshell is set up if it needs to be.
    nsCOMPtr<nsIDocument> document(do_QueryInterface(doc));
    if (document) {
      document->FlushPendingNotifications(Flush_Frames);
      if (mMakeWholeDocumentEditable) {
        document->SetEditableFlag(PR_TRUE);
      }
    }
  }
  PRBool needHTMLController = PR_FALSE;

  const char *classString = "@mozilla.org/editor/htmleditor;1";
  if (mEditorType.EqualsLiteral("textmail"))
  {
    mEditorFlags = nsIPlaintextEditor::eEditorPlaintextMask | 
                   nsIPlaintextEditor::eEditorEnableWrapHackMask | 
                   nsIPlaintextEditor::eEditorMailMask;
  }
  else if (mEditorType.EqualsLiteral("text"))
  {
    mEditorFlags = nsIPlaintextEditor::eEditorPlaintextMask | 
                   nsIPlaintextEditor::eEditorEnableWrapHackMask;
  }
  else if (mEditorType.EqualsLiteral("htmlmail"))
  {
    if (mimeCType.EqualsLiteral("text/html"))
    {
      needHTMLController = PR_TRUE;
      mEditorFlags = nsIPlaintextEditor::eEditorMailMask;
    }
    else //set the flags back to textplain.
      mEditorFlags = nsIPlaintextEditor::eEditorPlaintextMask | 
                     nsIPlaintextEditor::eEditorEnableWrapHackMask;
  }
  else // Defaulted to html
  {
    needHTMLController = PR_TRUE;
  }

  if (mInteractive) {
    mEditorFlags |= nsIPlaintextEditor::eEditorAllowInteraction;
  }

  // make the UI state maintainer
  mStateMaintainer = new nsComposerCommandsUpdater();

  // now init the state maintainer
  // This allows notification of error state
  //  even if we don't create an editor
  rv = mStateMaintainer->Init(aWindow);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mEditorStatus != eEditorCreationInProgress)
  {
    mStateMaintainer->NotifyDocumentCreated();
    return NS_ERROR_FAILURE;
  }

  // Create editor and do other things 
  //  only if we haven't found some error above,
  nsIDocShell *docShell = GetDocShellFromWindow(aWindow);
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);  

  if (!mInteractive) {
    // Disable animation of images in this document:
    nsCOMPtr<nsIDOMWindowUtils> utils(do_GetInterface(aWindow));
    NS_ENSURE_TRUE(utils, NS_ERROR_FAILURE);

    rv = utils->GetImageAnimationMode(&mImageAnimationMode);
    NS_ENSURE_SUCCESS(rv, rv);
    utils->SetImageAnimationMode(imgIContainer::kDontAnimMode);
  }

  // create and set editor
  nsCOMPtr<nsIEditorDocShell> editorDocShell = do_QueryInterface(docShell, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIEditor> editor = do_CreateInstance(classString, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  // set the editor on the docShell. The docShell now owns it.
  rv = editorDocShell->SetEditor(editor);
  NS_ENSURE_SUCCESS(rv, rv);

  // setup the HTML editor command controller
  if (needHTMLController)
  {
    // The third controller takes an nsIEditor as the context
    rv = SetupEditorCommandController("@mozilla.org/editor/htmleditorcontroller;1",
                                      aWindow, editor,
                                      &mHTMLCommandControllerId);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Set mimetype on editor
  rv = editor->SetContentsMIMEType(mimeCType.get());
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIContentViewer> contentViewer;
  rv = docShell->GetContentViewer(getter_AddRefs(contentViewer));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(contentViewer, NS_ERROR_FAILURE);

  nsCOMPtr<nsIDOMDocument> domDoc;  
  rv = contentViewer->GetDOMDocument(getter_AddRefs(domDoc));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(domDoc, NS_ERROR_FAILURE);

  // Set up as a doc state listener
  // Important! We must have this to broadcast the "obs_documentCreated" message
  rv = editor->AddDocumentStateListener(mStateMaintainer);
  NS_ENSURE_SUCCESS(rv, rv);

  // XXXbz we really shouldn't need a presShell here!
  nsCOMPtr<nsIPresShell> presShell;
  rv = docShell->GetPresShell(getter_AddRefs(presShell));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(presShell, NS_ERROR_FAILURE);

  nsCOMPtr<nsISelectionController> selCon = do_QueryInterface(presShell);
  rv = editor->Init(domDoc, presShell, nsnull /* root content */,
                    selCon, mEditorFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISelection> selection;
  editor->GetSelection(getter_AddRefs(selection));
  nsCOMPtr<nsISelectionPrivate> selPriv = do_QueryInterface(selection);
  NS_ENSURE_TRUE(selPriv, NS_ERROR_FAILURE);

  rv = selPriv->AddSelectionListener(mStateMaintainer);
  NS_ENSURE_SUCCESS(rv, rv);

  // and as a transaction listener
  nsCOMPtr<nsITransactionManager> txnMgr;
  editor->GetTransactionManager(getter_AddRefs(txnMgr));
  if (txnMgr)
    txnMgr->AddListener(mStateMaintainer);

  // Set context on all controllers to be the editor
  rv = SetEditorOnControllers(aWindow, editor);
  NS_ENSURE_SUCCESS(rv, rv);

  // Everything went fine!
  mEditorStatus = eEditorOK;

  // This will trigger documentCreation notification
  return editor->PostCreate();
}

// Removes all listeners and controllers from aWindow and aEditor.
void
nsEditingSession::RemoveListenersAndControllers(nsIDOMWindow *aWindow,
                                                nsIEditor *aEditor)
{
  if (!mStateMaintainer || !aEditor)
    return;

  // Remove all the listeners
  nsCOMPtr<nsISelection> selection;
  aEditor->GetSelection(getter_AddRefs(selection));
  nsCOMPtr<nsISelectionPrivate> selPriv = do_QueryInterface(selection);
  if (selPriv)
    selPriv->RemoveSelectionListener(mStateMaintainer);

  aEditor->RemoveDocumentStateListener(mStateMaintainer);

  nsCOMPtr<nsITransactionManager> txnMgr;
  aEditor->GetTransactionManager(getter_AddRefs(txnMgr));
  if (txnMgr)
    txnMgr->RemoveListener(mStateMaintainer);

  // Remove editor controllers from the window now that we're not
  // editing in that window any more.
  RemoveEditorControllers(aWindow);
}

/*---------------------------------------------------------------------------

  TearDownEditorOnWindow

  void tearDownEditorOnWindow (in nsIDOMWindow aWindow);
----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::TearDownEditorOnWindow(nsIDOMWindow *aWindow)
{
  NS_ENSURE_TRUE(mDoneSetup, NS_OK);

  NS_ENSURE_TRUE(aWindow, NS_ERROR_NULL_POINTER);

  nsresult rv;
  
  // Kill any existing reload timer
  if (mLoadBlankDocTimer)
  {
    mLoadBlankDocTimer->Cancel();
    mLoadBlankDocTimer = nsnull;
  }

  mDoneSetup = PR_FALSE;

  // Check if we're turning off editing (from contentEditable or designMode).
  nsCOMPtr<nsIDOMDocument> domDoc;
  aWindow->GetDocument(getter_AddRefs(domDoc));
  nsCOMPtr<nsIHTMLDocument> htmlDoc = do_QueryInterface(domDoc);
  PRBool stopEditing = htmlDoc && htmlDoc->IsEditingOn();
  if (stopEditing)
    RemoveWebProgressListener(aWindow);

  nsCOMPtr<nsIEditorDocShell> editorDocShell;
  rv = GetEditorDocShellFromWindow(aWindow, getter_AddRefs(editorDocShell));
  NS_ENSURE_SUCCESS(rv, rv);
  
  nsCOMPtr<nsIEditor> editor;
  rv = editorDocShell->GetEditor(getter_AddRefs(editor));
  NS_ENSURE_SUCCESS(rv, rv);

  if (stopEditing)
    htmlDoc->TearingDownEditor(editor);

  if (mStateMaintainer && editor)
  {
    // Null out the editor on the controllers first to prevent their weak 
    // references from pointing to a destroyed editor.
    SetEditorOnControllers(aWindow, nsnull);
  }

  // Null out the editor on the docShell to trigger PreDestroy which
  // needs to happen before document state listeners are removed below.
  editorDocShell->SetEditor(nsnull);

  RemoveListenersAndControllers(aWindow, editor);

  if (stopEditing)
  {
    // Make things the way they were before we started editing.
    RestoreJSAndPlugins(aWindow);
    RestoreAnimationMode(aWindow);

    if (mMakeWholeDocumentEditable)
    {
      nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      doc->SetEditableFlag(PR_FALSE);
    }
  }

  return rv;
}

/*---------------------------------------------------------------------------

  GetEditorForFrame

  nsIEditor getEditorForFrame (in nsIDOMWindow aWindow);
----------------------------------------------------------------------------*/
NS_IMETHODIMP 
nsEditingSession::GetEditorForWindow(nsIDOMWindow *aWindow,
                                     nsIEditor **outEditor)
{
  nsCOMPtr<nsIEditorDocShell> editorDocShell;
  nsresult rv = GetEditorDocShellFromWindow(aWindow,
                                            getter_AddRefs(editorDocShell));
  NS_ENSURE_SUCCESS(rv, rv);  
  
  return editorDocShell->GetEditor(outEditor);
}

#ifdef XP_MAC
#pragma mark -
#endif

/*---------------------------------------------------------------------------

  OnStateChange

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnStateChange(nsIWebProgress *aWebProgress,
                                nsIRequest *aRequest,
                                PRUint32 aStateFlags, nsresult aStatus)
{

#ifdef NOISY_DOC_LOADING
  nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
  if (channel)
  {
    nsCAutoString contentType;
    channel->GetContentType(contentType);
    if (!contentType.IsEmpty())
      printf(" ++++++ MIMETYPE = %s\n", contentType.get());
  }
#endif

  //
  // A Request has started...
  //
  if (aStateFlags & nsIWebProgressListener::STATE_START)
  {
#ifdef NOISY_DOC_LOADING
  {
    nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
    if (channel)
    {
      nsCOMPtr<nsIURI> uri;
      channel->GetURI(getter_AddRefs(uri));
      if (uri)
      {
        nsXPIDLCString spec;
        uri->GetSpec(spec);
        printf(" **** STATE_START: CHANNEL URI=%s, flags=%x\n",
               spec.get(), aStateFlags);
      }
    }
    else
      printf("    STATE_START: NO CHANNEL flags=%x\n", aStateFlags);
  }
#endif
    // Page level notification...
    if (aStateFlags & nsIWebProgressListener::STATE_IS_NETWORK)
    {
      nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
      StartPageLoad(channel);
#ifdef NOISY_DOC_LOADING
      printf("STATE_START & STATE_IS_NETWORK flags=%x\n", aStateFlags);
#endif
    }

    // Document level notification...
    if (aStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT &&
        !(aStateFlags & nsIWebProgressListener::STATE_RESTORING)) {
#ifdef NOISY_DOC_LOADING
      printf("STATE_START & STATE_IS_DOCUMENT flags=%x\n", aStateFlags);
#endif

      PRBool progressIsForTargetDocument =
        IsProgressForTargetDocument(aWebProgress);

      if (progressIsForTargetDocument)
      {
        nsCOMPtr<nsIDOMWindow> window;
        aWebProgress->GetDOMWindow(getter_AddRefs(window));

        nsCOMPtr<nsIDOMDocument> doc;
        window->GetDocument(getter_AddRefs(doc));

        nsCOMPtr<nsIHTMLDocument> htmlDoc(do_QueryInterface(doc));

        if (htmlDoc && htmlDoc->IsWriting())
        {
          nsCOMPtr<nsIDOMNSHTMLDocument> htmlDomDoc(do_QueryInterface(doc));
          nsAutoString designMode;

          htmlDomDoc->GetDesignMode(designMode);

          if (designMode.EqualsLiteral("on"))
          {
            // This notification is for data coming in through
            // document.open/write/close(), ignore it.

            return NS_OK;
          }
        }

        mCanCreateEditor = PR_TRUE;
        StartDocumentLoad(aWebProgress, progressIsForTargetDocument);
      }
    }
  }
  //
  // A Request is being processed
  //
  else if (aStateFlags & nsIWebProgressListener::STATE_TRANSFERRING)
  {
    if (aStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT)
    {
      // document transfer started
    }
  }
  //
  // Got a redirection
  //
  else if (aStateFlags & nsIWebProgressListener::STATE_REDIRECTING)
  {
    if (aStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT)
    {
      // got a redirect
    }
  }
  //
  // A network or document Request has finished...
  //
  else if (aStateFlags & nsIWebProgressListener::STATE_STOP)
  {

#ifdef NOISY_DOC_LOADING
  {
    nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
    if (channel)
    {
      nsCOMPtr<nsIURI> uri;
      channel->GetURI(getter_AddRefs(uri));
      if (uri)
      {
        nsXPIDLCString spec;
        uri->GetSpec(spec);
        printf(" **** STATE_STOP: CHANNEL URI=%s, flags=%x\n",
               spec.get(), aStateFlags);
      }
    }
    else
      printf("     STATE_STOP: NO CHANNEL  flags=%x\n", aStateFlags);
  }
#endif

    // Document level notification...
    if (aStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT)
    {
      nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
      EndDocumentLoad(aWebProgress, channel, aStatus,
                      IsProgressForTargetDocument(aWebProgress));
#ifdef NOISY_DOC_LOADING
      printf("STATE_STOP & STATE_IS_DOCUMENT flags=%x\n", aStateFlags);
#endif
    }

    // Page level notification...
    if (aStateFlags & nsIWebProgressListener::STATE_IS_NETWORK)
    {
      nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
      (void)EndPageLoad(aWebProgress, channel, aStatus);
#ifdef NOISY_DOC_LOADING
      printf("STATE_STOP & STATE_IS_NETWORK flags=%x\n", aStateFlags);
#endif
    }
  }

  return NS_OK;
}

/*---------------------------------------------------------------------------

  OnProgressChange

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnProgressChange(nsIWebProgress *aWebProgress,
                                   nsIRequest *aRequest,
                                   PRInt32 aCurSelfProgress,
                                   PRInt32 aMaxSelfProgress,
                                   PRInt32 aCurTotalProgress,
                                   PRInt32 aMaxTotalProgress)
{
    NS_NOTREACHED("notification excluded in AddProgressListener(...)");
    return NS_OK;
}

/*---------------------------------------------------------------------------

  OnLocationChange

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnLocationChange(nsIWebProgress *aWebProgress, 
                                   nsIRequest *aRequest, nsIURI *aURI)
{
  nsCOMPtr<nsIDOMWindow> domWindow;
  nsresult rv = aWebProgress->GetDOMWindow(getter_AddRefs(domWindow));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMDocument> domDoc;
  rv = domWindow->GetDocument(getter_AddRefs(domDoc));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);
  NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);

  doc->SetDocumentURI(aURI);

  // Notify the location-changed observer that
  //  the document URL has changed
  nsIDocShell *docShell = GetDocShellFromWindow(domWindow);
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  nsCOMPtr<nsICommandManager> commandManager = do_GetInterface(docShell);
  nsCOMPtr<nsPICommandUpdater> commandUpdater =
                                  do_QueryInterface(commandManager);
  NS_ENSURE_TRUE(commandUpdater, NS_ERROR_FAILURE);

  return commandUpdater->CommandStatusChanged("obs_documentLocationChanged");
}

/*---------------------------------------------------------------------------

  OnStatusChange

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnStatusChange(nsIWebProgress *aWebProgress,
                                 nsIRequest *aRequest,
                                 nsresult aStatus,
                                 const PRUnichar *aMessage)
{
    NS_NOTREACHED("notification excluded in AddProgressListener(...)");
    return NS_OK;
}

/*---------------------------------------------------------------------------

  OnSecurityChange

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnSecurityChange(nsIWebProgress *aWebProgress,
                                   nsIRequest *aRequest, PRUint32 state)
{
    NS_NOTREACHED("notification excluded in AddProgressListener(...)");
    return NS_OK;
}


#ifdef XP_MAC
#pragma mark -
#endif


/*---------------------------------------------------------------------------

  IsProgressForTargetDocument

  Check that this notification is for our document.
----------------------------------------------------------------------------*/

PRBool
nsEditingSession::IsProgressForTargetDocument(nsIWebProgress *aWebProgress)
{
  nsCOMPtr<nsIDOMWindow> domWindow;
  if (aWebProgress)
    aWebProgress->GetDOMWindow(getter_AddRefs(domWindow));
  nsCOMPtr<nsIDOMWindow> editedDOMWindow = do_QueryReferent(mWindowToBeEdited);

  return (domWindow && (domWindow == editedDOMWindow));
}


/*---------------------------------------------------------------------------

  GetEditorStatus

  Called during GetCommandStateParams("obs_documentCreated"...) 
  to determine if editor was created and document 
  was loaded successfully
----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::GetEditorStatus(PRUint32 *aStatus)
{
  NS_ENSURE_ARG_POINTER(aStatus);
  *aStatus = mEditorStatus;
  return NS_OK;
}

/*---------------------------------------------------------------------------

  StartDocumentLoad

  Called on start of load in a single frame
----------------------------------------------------------------------------*/
nsresult
nsEditingSession::StartDocumentLoad(nsIWebProgress *aWebProgress, 
                                    PRBool aIsToBeMadeEditable)
{
#ifdef NOISY_DOC_LOADING
  printf("======= StartDocumentLoad ========\n");
#endif

  NS_ENSURE_ARG_POINTER(aWebProgress);
  
  // If we have an editor here, then we got a reload after making the editor.
  // We need to blow it away and make a new one at the end of the load.
  nsCOMPtr<nsIDOMWindow> domWindow;
  aWebProgress->GetDOMWindow(getter_AddRefs(domWindow));
  if (domWindow)
  {
    nsIDocShell *docShell = GetDocShellFromWindow(domWindow);
    NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);
    docShell->DetachEditorFromWindow();
  }
    
  if (aIsToBeMadeEditable)
    mEditorStatus = eEditorCreationInProgress;

  return NS_OK;
}

/*---------------------------------------------------------------------------

  EndDocumentLoad

  Called on end of load in a single frame
----------------------------------------------------------------------------*/
nsresult
nsEditingSession::EndDocumentLoad(nsIWebProgress *aWebProgress,
                                  nsIChannel* aChannel, nsresult aStatus,
                                  PRBool aIsToBeMadeEditable)
{
  NS_ENSURE_ARG_POINTER(aWebProgress);
  
#ifdef NOISY_DOC_LOADING
  printf("======= EndDocumentLoad ========\n");
  printf("with status %d, ", aStatus);
  nsCOMPtr<nsIURI> uri;
  nsXPIDLCString spec;
  if (NS_SUCCEEDED(aChannel->GetURI(getter_AddRefs(uri)))) {
    uri->GetSpec(spec);
    printf(" uri %s\n", spec.get());
  }
#endif

  // We want to call the base class EndDocumentLoad,
  // but avoid some of the stuff
  // that nsDocShell does (need to refactor).
  
  // OK, time to make an editor on this document
  nsCOMPtr<nsIDOMWindow> domWindow;
  aWebProgress->GetDOMWindow(getter_AddRefs(domWindow));
  
  // Set the error state -- we will create an editor 
  // anyway and load empty doc later
  if (aIsToBeMadeEditable) {
    if (aStatus == NS_ERROR_FILE_NOT_FOUND)
      mEditorStatus = eEditorErrorFileNotFound;
  }

  nsIDocShell *docShell = GetDocShellFromWindow(domWindow);
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);       // better error handling?

  // cancel refresh from meta tags
  // we need to make sure that all pages in editor (whether editable or not)
  // can't refresh contents being edited
  nsCOMPtr<nsIRefreshURI> refreshURI = do_QueryInterface(docShell);
  if (refreshURI)
    refreshURI->CancelRefreshURITimers();

  nsCOMPtr<nsIEditorDocShell> editorDocShell = do_QueryInterface(docShell);

  nsresult rv = NS_OK;

  // did someone set the flag to make this shell editable?
  if (aIsToBeMadeEditable && mCanCreateEditor && editorDocShell)
  {
    PRBool  makeEditable;
    editorDocShell->GetEditable(&makeEditable);
  
    if (makeEditable)
    {
      // To keep pre Gecko 1.9 behavior, setup editor always when
      // mMakeWholeDocumentEditable.
      PRBool needsSetup;
      if (mMakeWholeDocumentEditable) {
        needsSetup = PR_TRUE;
      } else {
        // do we already have an editor here?
        nsCOMPtr<nsIEditor> editor;
        rv = editorDocShell->GetEditor(getter_AddRefs(editor));
        NS_ENSURE_SUCCESS(rv, rv);

        needsSetup = !editor;
      }

      if (needsSetup)
      {
        mCanCreateEditor = PR_FALSE;
        rv = SetupEditorOnWindow(domWindow);
        if (NS_FAILED(rv))
        {
          // If we had an error, setup timer to load a blank page later
          if (mLoadBlankDocTimer)
          {
            // Must cancel previous timer?
            mLoadBlankDocTimer->Cancel();
            mLoadBlankDocTimer = NULL;
          }
  
          mLoadBlankDocTimer = do_CreateInstance("@mozilla.org/timer;1", &rv);
          NS_ENSURE_SUCCESS(rv, rv);

          mEditorStatus = eEditorCreationInProgress;
          mDocShell = do_GetWeakReference(docShell);
          mLoadBlankDocTimer->InitWithFuncCallback(
                                          nsEditingSession::TimerCallback,
                                          static_cast<void*> (mDocShell.get()),
                                          10, nsITimer::TYPE_ONE_SHOT);
        }
      }
    }
  }
  return rv;
}


void
nsEditingSession::TimerCallback(nsITimer* aTimer, void* aClosure)
{
  nsCOMPtr<nsIDocShell> docShell = do_QueryReferent(static_cast<nsIWeakReference*> (aClosure));
  if (docShell)
  {
    nsCOMPtr<nsIWebNavigation> webNav(do_QueryInterface(docShell));
    if (webNav)
      webNav->LoadURI(NS_LITERAL_STRING("about:blank").get(),
                      0, nsnull, nsnull, nsnull);
  }
}

/*---------------------------------------------------------------------------

  StartPageLoad

  Called on start load of the entire page (incl. subframes)
----------------------------------------------------------------------------*/
nsresult
nsEditingSession::StartPageLoad(nsIChannel *aChannel)
{
#ifdef NOISY_DOC_LOADING
  printf("======= StartPageLoad ========\n");
#endif
  return NS_OK;
}

/*---------------------------------------------------------------------------

  EndPageLoad

  Called on end load of the entire page (incl. subframes)
----------------------------------------------------------------------------*/
nsresult
nsEditingSession::EndPageLoad(nsIWebProgress *aWebProgress,
                              nsIChannel* aChannel, nsresult aStatus)
{
#ifdef NOISY_DOC_LOADING
  printf("======= EndPageLoad ========\n");
  printf("  with status %d, ", aStatus);
  nsCOMPtr<nsIURI> uri;
  nsXPIDLCString spec;
  if (NS_SUCCEEDED(aChannel->GetURI(getter_AddRefs(uri)))) {
    uri->GetSpec(spec);
    printf("uri %s\n", spec.get());
  }
 
  nsCAutoString contentType;
  aChannel->GetContentType(contentType);
  if (!contentType.IsEmpty())
    printf("   flags = %d, status = %d, MIMETYPE = %s\n", 
               mEditorFlags, mEditorStatus, contentType.get());
#endif

  // Set the error state -- we will create an editor anyway 
  // and load empty doc later
  if (aStatus == NS_ERROR_FILE_NOT_FOUND)
    mEditorStatus = eEditorErrorFileNotFound;

  nsCOMPtr<nsIDOMWindow> domWindow;
  aWebProgress->GetDOMWindow(getter_AddRefs(domWindow));

  nsIDocShell *docShell = GetDocShellFromWindow(domWindow);
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  // cancel refresh from meta tags
  // we need to make sure that all pages in editor (whether editable or not)
  // can't refresh contents being edited
  nsCOMPtr<nsIRefreshURI> refreshURI = do_QueryInterface(docShell);
  if (refreshURI)
    refreshURI->CancelRefreshURITimers();

#if 0
  // Shouldn't we do this when we want to edit sub-frames?
  return MakeWindowEditable(domWindow, "html", PR_FALSE, mInteractive);
#else
  return NS_OK;
#endif
}


#ifdef XP_MAC
#pragma mark -
#endif

/*---------------------------------------------------------------------------

  GetDocShellFromWindow

  Utility method. This will always return nsnull if no docShell is found.
----------------------------------------------------------------------------*/
nsIDocShell *
nsEditingSession::GetDocShellFromWindow(nsIDOMWindow *aWindow)
{
  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(aWindow);
  NS_ENSURE_TRUE(window, nsnull);

  return window->GetDocShell();
}

/*---------------------------------------------------------------------------

  GetEditorDocShellFromWindow

  Utility method. This will always return an error if no docShell
  is returned.
----------------------------------------------------------------------------*/
nsresult
nsEditingSession::GetEditorDocShellFromWindow(nsIDOMWindow *aWindow,
                                              nsIEditorDocShell** outDocShell)
{
  nsIDocShell *docShell = GetDocShellFromWindow(aWindow);
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);
  
  return docShell->QueryInterface(NS_GET_IID(nsIEditorDocShell), 
                                  (void **)outDocShell);
}

/*---------------------------------------------------------------------------

  PrepareForEditing

  Set up this editing session for one or more editors
----------------------------------------------------------------------------*/
nsresult
nsEditingSession::PrepareForEditing(nsIDOMWindow *aWindow)
{
  if (mProgressListenerRegistered)
    return NS_OK;
    
  nsIDocShell *docShell = GetDocShellFromWindow(aWindow);
  
  // register callback
  nsCOMPtr<nsIWebProgress> webProgress = do_GetInterface(docShell);
  NS_ENSURE_TRUE(webProgress, NS_ERROR_FAILURE);

  nsresult rv =
    webProgress->AddProgressListener(this,
                                     (nsIWebProgress::NOTIFY_STATE_NETWORK  | 
                                      nsIWebProgress::NOTIFY_STATE_DOCUMENT |
                                      nsIWebProgress::NOTIFY_LOCATION));

  mProgressListenerRegistered = NS_SUCCEEDED(rv);

  return rv;
}

/*---------------------------------------------------------------------------

  SetupEditorCommandController

  Create a command controller, append to controllers,
  get and return the controller ID, and set the context
----------------------------------------------------------------------------*/
nsresult
nsEditingSession::SetupEditorCommandController(
                                  const char *aControllerClassName,
                                  nsIDOMWindow *aWindow,
                                  nsISupports *aContext,
                                  PRUint32 *aControllerId)
{
  NS_ENSURE_ARG_POINTER(aControllerClassName);
  NS_ENSURE_ARG_POINTER(aWindow);
  NS_ENSURE_ARG_POINTER(aContext);
  NS_ENSURE_ARG_POINTER(aControllerId);

  nsresult rv;
  nsCOMPtr<nsIDOMWindowInternal> domWindowInt =
                                    do_QueryInterface(aWindow, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  
  nsCOMPtr<nsIControllers> controllers;      
  rv = domWindowInt->GetControllers(getter_AddRefs(controllers));
  NS_ENSURE_SUCCESS(rv, rv);

  // We only have to create each singleton controller once
  // We know this has happened once we have a controllerId value
  if (!*aControllerId)
  {
    nsresult rv;
    nsCOMPtr<nsIController> controller;
    controller = do_CreateInstance(aControllerClassName, &rv);
    NS_ENSURE_SUCCESS(rv, rv);  

    // We must insert at head of the list to be sure our
    //   controller is found before other implementations
    //   (e.g., not-implemented versions by browser)
    rv = controllers->InsertControllerAt(0, controller);
    NS_ENSURE_SUCCESS(rv, rv);  

    // Remember the ID for the controller
    rv = controllers->GetControllerId(controller, aControllerId);
    NS_ENSURE_SUCCESS(rv, rv);  
  }  

  // Set the context
  return SetContextOnControllerById(controllers, aContext, *aControllerId);
}

/*---------------------------------------------------------------------------

  SetEditorOnControllers

  Set the editor on the controller(s) for this window
----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::SetEditorOnControllers(nsIDOMWindow *aWindow,
                                         nsIEditor* aEditor)
{
  nsresult rv;
  
  // set the editor on the controller
  nsCOMPtr<nsIDOMWindowInternal> domWindowInt =
                                     do_QueryInterface(aWindow, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  
  nsCOMPtr<nsIControllers> controllers;      
  rv = domWindowInt->GetControllers(getter_AddRefs(controllers));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupports> editorAsISupports = do_QueryInterface(aEditor);
  if (mBaseCommandControllerId)
  {
    rv = SetContextOnControllerById(controllers, editorAsISupports,
                                    mBaseCommandControllerId);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mDocStateControllerId)
  {
    rv = SetContextOnControllerById(controllers, editorAsISupports,
                                    mDocStateControllerId);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mHTMLCommandControllerId)
    rv = SetContextOnControllerById(controllers, editorAsISupports,
                                    mHTMLCommandControllerId);

  return rv;
}

nsresult
nsEditingSession::SetContextOnControllerById(nsIControllers* aControllers,
                                             nsISupports* aContext,
                                             PRUint32 aID)
{
  NS_ENSURE_ARG_POINTER(aControllers);

  // aContext can be null (when destroying editor)
  nsCOMPtr<nsIController> controller;    
  aControllers->GetControllerById(aID, getter_AddRefs(controller));
  
  // ok with nil controller
  nsCOMPtr<nsIControllerContext> editorController =
                                       do_QueryInterface(controller);
  NS_ENSURE_TRUE(editorController, NS_ERROR_FAILURE);

  return editorController->SetCommandContext(aContext);
}

void
nsEditingSession::RemoveEditorControllers(nsIDOMWindow *aWindow)
{
  // Remove editor controllers from the aWindow, call when we're 
  // tearing down/detaching editor.
  nsCOMPtr<nsIDOMWindowInternal> domWindowInt(do_QueryInterface(aWindow));

  nsCOMPtr<nsIControllers> controllers;
  if (domWindowInt)
    domWindowInt->GetControllers(getter_AddRefs(controllers));

  if (controllers)
  {
    nsCOMPtr<nsIController> controller;
    if (mBaseCommandControllerId)
    {
      controllers->GetControllerById(mBaseCommandControllerId,
                                     getter_AddRefs(controller));
      if (controller)
        controllers->RemoveController(controller);
    }

    if (mDocStateControllerId)
    {
      controllers->GetControllerById(mDocStateControllerId,
                                     getter_AddRefs(controller));
      if (controller)
        controllers->RemoveController(controller);
    }

    if (mHTMLCommandControllerId)
    {
      controllers->GetControllerById(mHTMLCommandControllerId,
                                     getter_AddRefs(controller));
      if (controller)
        controllers->RemoveController(controller);
    }
  }

  // Clear IDs to trigger creation of new controllers.
  mBaseCommandControllerId = 0;
  mDocStateControllerId = 0;
  mHTMLCommandControllerId = 0;
}

void
nsEditingSession::RemoveWebProgressListener(nsIDOMWindow *aWindow)
{
  nsIDocShell *docShell = GetDocShellFromWindow(aWindow);
  nsCOMPtr<nsIWebProgress> webProgress = do_GetInterface(docShell);
  if (webProgress)
  {
    webProgress->RemoveProgressListener(this);
    mProgressListenerRegistered = PR_FALSE;
  }
}

void
nsEditingSession::RestoreAnimationMode(nsIDOMWindow *aWindow)
{
  if (!mInteractive)
  {
    nsCOMPtr<nsIDOMWindowUtils> utils(do_GetInterface(aWindow));
    if (utils)
      utils->SetImageAnimationMode(mImageAnimationMode);
  }
}

nsresult
nsEditingSession::DetachFromWindow(nsIDOMWindow* aWindow)
{
  NS_ENSURE_TRUE(mDoneSetup, NS_OK);

  NS_ASSERTION(mStateMaintainer, "mStateMaintainer should exist.");

  // Kill any existing reload timer
  if (mLoadBlankDocTimer)
  {
    mLoadBlankDocTimer->Cancel();
    mLoadBlankDocTimer = nsnull;
  }

  // Remove controllers, webprogress listener, and otherwise
  // make things the way they were before we started editing.
  RemoveEditorControllers(aWindow);
  RemoveWebProgressListener(aWindow);
  RestoreJSAndPlugins(aWindow);
  RestoreAnimationMode(aWindow);

  // Kill our weak reference to our original window, in case
  // it changes on restore, or otherwise dies.
  mWindowToBeEdited = nsnull;

  return NS_OK;
}

nsresult
nsEditingSession::ReattachToWindow(nsIDOMWindow* aWindow)
{
  NS_ENSURE_TRUE(mDoneSetup, NS_OK);

  NS_ASSERTION(mStateMaintainer, "mStateMaintainer should exist.");

  // Imitate nsEditorDocShell::MakeEditable() to reattach the
  // old editor ot the window.
  nsresult rv;

  mWindowToBeEdited = do_GetWeakReference(aWindow);

  // Disable plugins.
  if (!mInteractive)
  {
    rv = DisableJSAndPlugins(aWindow);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Tells embedder that startup is in progress.
  mEditorStatus = eEditorCreationInProgress;

  // Adds back web progress listener.
  rv = PrepareForEditing(aWindow);
  NS_ENSURE_SUCCESS(rv, rv);

  // Setup the command controllers again.
  rv = SetupEditorCommandController("@mozilla.org/editor/editorcontroller;1",
                                    aWindow,
                                    static_cast<nsIEditingSession*>(this),
                                    &mBaseCommandControllerId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = SetupEditorCommandController("@mozilla.org/editor/editordocstatecontroller;1",
                                    aWindow,
                                    static_cast<nsIEditingSession*>(this),
                                    &mDocStateControllerId);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mStateMaintainer)
    mStateMaintainer->Init(aWindow);

  // Get editor
  nsCOMPtr<nsIEditor> editor;
  rv = GetEditorForWindow(aWindow, getter_AddRefs(editor));
  NS_ENSURE_TRUE(editor, NS_ERROR_FAILURE);

  if (!mInteractive)
  {
    // Disable animation of images in this document:
    nsCOMPtr<nsIDOMWindowUtils> utils(do_GetInterface(aWindow));
    NS_ENSURE_TRUE(utils, NS_ERROR_FAILURE);

    rv = utils->GetImageAnimationMode(&mImageAnimationMode);
    NS_ENSURE_SUCCESS(rv, rv);
    utils->SetImageAnimationMode(imgIContainer::kDontAnimMode);
  }

  // The third controller takes an nsIEditor as the context
  rv = SetupEditorCommandController("@mozilla.org/editor/htmleditorcontroller;1",
                                    aWindow, editor,
                                    &mHTMLCommandControllerId);
  NS_ENSURE_SUCCESS(rv, rv);

  // Set context on all controllers to be the editor
  rv = SetEditorOnControllers(aWindow, editor);
  NS_ENSURE_SUCCESS(rv, rv);

#ifdef DEBUG
  {
    PRBool isEditable;
    rv = WindowIsEditable(aWindow, &isEditable);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ASSERTION(isEditable, "Window is not editable after reattaching editor.");
  }
#endif // DEBUG

  return NS_OK;
}
