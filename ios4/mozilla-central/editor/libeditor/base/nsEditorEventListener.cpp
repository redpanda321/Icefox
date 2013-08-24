/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=4 sw=2 et tw=78: */
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
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
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
#include "nsEditorEventListener.h"
#include "nsEditor.h"

#include "nsIDOMDOMStringList.h"
#include "nsIDOMEvent.h"
#include "nsIDOMNSEvent.h"
#include "nsIDOMDocument.h"
#include "nsPIDOMEventTarget.h"
#include "nsIDocument.h"
#include "nsIPresShell.h"
#include "nsISelection.h"
#include "nsISelectionController.h"
#include "nsIDOMKeyEvent.h"
#include "nsIDOMMouseEvent.h"
#include "nsIDOMNSUIEvent.h"
#include "nsIPrivateTextEvent.h"
#include "nsIEditorMailSupport.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsILookAndFeel.h"
#include "nsFocusManager.h"
#include "nsIEventListenerManager.h"
#include "nsIDOMEventGroup.h"

// Drag & Drop, Clipboard
#include "nsIServiceManager.h"
#include "nsIClipboard.h"
#include "nsIDragService.h"
#include "nsIDragSession.h"
#include "nsIContent.h"
#include "nsISupportsPrimitives.h"
#include "nsIDOMNSRange.h"
#include "nsEditorUtils.h"
#include "nsIDOMEventTarget.h"
#include "nsIEventStateManager.h"
#include "nsISelectionPrivate.h"
#include "nsIDOMDragEvent.h"
#include "nsIFocusManager.h"
#include "nsIDOMWindow.h"
#include "nsContentUtils.h"

nsEditorEventListener::nsEditorEventListener() :
  mEditor(nsnull), mCaretDrawn(PR_FALSE), mCommitText(PR_FALSE),
  mInTransaction(PR_FALSE)
{
}

nsEditorEventListener::~nsEditorEventListener() 
{
  if (mEditor) {
    NS_WARNING("We're not uninstalled");
    Disconnect();
  }
}

nsresult
nsEditorEventListener::Connect(nsEditor* aEditor)
{
  NS_ENSURE_ARG(aEditor);

  mEditor = aEditor;

  nsresult rv = InstallToEditor();
  if (NS_FAILED(rv)) {
    Disconnect();
  }
  return rv;
}

nsresult
nsEditorEventListener::InstallToEditor()
{
  NS_PRECONDITION(mEditor, "The caller must set mEditor");

  nsCOMPtr<nsPIDOMEventTarget> piTarget = mEditor->GetPIDOMEventTarget();
  NS_ENSURE_TRUE(piTarget, NS_ERROR_FAILURE);

  nsresult rv;

  // register the event listeners with the listener manager
  nsCOMPtr<nsIDOMEventGroup> sysGroup;
  piTarget->GetSystemEventGroup(getter_AddRefs(sysGroup));
  NS_ENSURE_STATE(sysGroup);
  nsIEventListenerManager* elmP = piTarget->GetListenerManager(PR_TRUE);
  NS_ENSURE_STATE(elmP);

  rv = elmP->AddEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                    NS_LITERAL_STRING("keypress"),
                                    NS_EVENT_FLAG_BUBBLE |
                                    NS_PRIV_EVENT_UNTRUSTED_PERMITTED,
                                    sysGroup);
  NS_ENSURE_SUCCESS(rv, rv);
  // See bug 455215, we cannot use the standard dragstart event yet
  rv = elmP->AddEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                    NS_LITERAL_STRING("draggesture"),
                                    NS_EVENT_FLAG_BUBBLE, sysGroup);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = elmP->AddEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                    NS_LITERAL_STRING("dragenter"),
                                    NS_EVENT_FLAG_BUBBLE, sysGroup);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = elmP->AddEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                    NS_LITERAL_STRING("dragover"),
                                    NS_EVENT_FLAG_BUBBLE, sysGroup);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = elmP->AddEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                    NS_LITERAL_STRING("dragleave"),
                                    NS_EVENT_FLAG_BUBBLE, sysGroup);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = elmP->AddEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                    NS_LITERAL_STRING("drop"),
                                    NS_EVENT_FLAG_BUBBLE, sysGroup);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = piTarget->AddEventListenerByIID(static_cast<nsIDOMMouseListener*>(this),
                                       NS_GET_IID(nsIDOMMouseListener));
  NS_ENSURE_SUCCESS(rv, rv);

  // Focus event doesn't bubble so adding the listener to capturing phase.
  // Make sure this works after bug 235441 gets fixed.
  rv = elmP->AddEventListenerByIID(static_cast<nsIDOMFocusListener*>(this),
                                   NS_GET_IID(nsIDOMFocusListener),
                                   NS_EVENT_FLAG_CAPTURE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = piTarget->AddEventListenerByIID(static_cast<nsIDOMTextListener*>(this),
                                       NS_GET_IID(nsIDOMTextListener));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = piTarget->AddEventListenerByIID(
    static_cast<nsIDOMCompositionListener*>(this),
    NS_GET_IID(nsIDOMCompositionListener));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void
nsEditorEventListener::Disconnect()
{
  if (!mEditor) {
    return;
  }
  UninstallFromEditor();
  mEditor = nsnull;
}

void
nsEditorEventListener::UninstallFromEditor()
{
  nsCOMPtr<nsPIDOMEventTarget> piTarget = mEditor->GetPIDOMEventTarget();
  if (!piTarget) {
    return;
  }

  nsCOMPtr<nsIEventListenerManager> elmP =
    piTarget->GetListenerManager(PR_TRUE);
  if (!elmP) {
    return;
  }
  nsCOMPtr<nsIDOMEventGroup> sysGroup;
  piTarget->GetSystemEventGroup(getter_AddRefs(sysGroup));
  if (!sysGroup) {
    return;
  }

  elmP->RemoveEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                  NS_LITERAL_STRING("keypress"),
                                  NS_EVENT_FLAG_BUBBLE, sysGroup);
  elmP->RemoveEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                  NS_LITERAL_STRING("draggesture"),
                                  NS_EVENT_FLAG_BUBBLE, sysGroup);
  elmP->RemoveEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                  NS_LITERAL_STRING("dragenter"),
                                  NS_EVENT_FLAG_BUBBLE, sysGroup);
  elmP->RemoveEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                  NS_LITERAL_STRING("dragover"),
                                  NS_EVENT_FLAG_BUBBLE, sysGroup);
  elmP->RemoveEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                  NS_LITERAL_STRING("dragleave"),
                                  NS_EVENT_FLAG_BUBBLE, sysGroup);
  elmP->RemoveEventListenerByType(static_cast<nsIDOMKeyListener*>(this),
                                  NS_LITERAL_STRING("drop"),
                                  NS_EVENT_FLAG_BUBBLE, sysGroup);

  piTarget->RemoveEventListenerByIID(static_cast<nsIDOMMouseListener*>(this),
                                     NS_GET_IID(nsIDOMMouseListener));

  elmP->RemoveEventListenerByIID(static_cast<nsIDOMFocusListener*>(this),
                                 NS_GET_IID(nsIDOMFocusListener),
                                 NS_EVENT_FLAG_CAPTURE);

  piTarget->RemoveEventListenerByIID(static_cast<nsIDOMTextListener*>(this),
                                     NS_GET_IID(nsIDOMTextListener));

  piTarget->RemoveEventListenerByIID(
    static_cast<nsIDOMCompositionListener*>(this),
    NS_GET_IID(nsIDOMCompositionListener));
}

already_AddRefed<nsIPresShell>
nsEditorEventListener::GetPresShell()
{
  NS_PRECONDITION(mEditor,
    "The caller must check whether this is connected to an editor");
  nsCOMPtr<nsIPresShell> ps;
  mEditor->GetPresShell(getter_AddRefs(ps));
  return ps.forget();
}

/**
 *  nsISupports implementation
 */

NS_IMPL_ADDREF(nsEditorEventListener)
NS_IMPL_RELEASE(nsEditorEventListener)

NS_INTERFACE_MAP_BEGIN(nsEditorEventListener)
  NS_INTERFACE_MAP_ENTRY(nsIDOMKeyListener)
  NS_INTERFACE_MAP_ENTRY(nsIDOMTextListener)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCompositionListener)
  NS_INTERFACE_MAP_ENTRY(nsIDOMMouseListener)
  NS_INTERFACE_MAP_ENTRY(nsIDOMFocusListener)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIDOMEventListener, nsIDOMKeyListener)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMKeyListener)
NS_INTERFACE_MAP_END

/**
 *  nsIDOMEventListener implementation
 */

NS_IMETHODIMP
nsEditorEventListener::HandleEvent(nsIDOMEvent* aEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIDOMDragEvent> dragEvent = do_QueryInterface(aEvent);
  if (dragEvent) {
    nsAutoString eventType;
    aEvent->GetType(eventType);
    if (eventType.EqualsLiteral("draggesture"))
      return DragGesture(dragEvent);
    if (eventType.EqualsLiteral("dragenter"))
      return DragEnter(dragEvent);
    if (eventType.EqualsLiteral("dragover"))
      return DragOver(dragEvent);
    if (eventType.EqualsLiteral("dragleave"))
      return DragLeave(dragEvent);
    if (eventType.EqualsLiteral("drop"))
      return Drop(dragEvent);
  }
  return NS_OK;
}

/**
 * nsIDOMKeyListener implementation
 */

NS_IMETHODIMP
nsEditorEventListener::KeyDown(nsIDOMEvent* aKeyEvent)
{
  // WARNING: If you change this method, you comment out next line.
  // NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::KeyUp(nsIDOMEvent* aKeyEvent)
{
  // WARNING: If you change this method, you comment out next line.
  // NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::KeyPress(nsIDOMEvent* aKeyEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);

  if (!mEditor->IsAcceptableInputEvent(aKeyEvent)) {
    return NS_OK;
  }

  // DOM event handling happens in two passes, the client pass and the system
  // pass.  We do all of our processing in the system pass, to allow client
  // handlers the opportunity to cancel events and prevent typing in the editor.
  // If the client pass cancelled the event, defaultPrevented will be true
  // below.

  nsCOMPtr<nsIDOMNSUIEvent> UIEvent = do_QueryInterface(aKeyEvent);
  if(UIEvent) {
    PRBool defaultPrevented;
    UIEvent->GetPreventDefault(&defaultPrevented);
    if(defaultPrevented) {
      return NS_OK;
    }
  }

  nsCOMPtr<nsIDOMKeyEvent>keyEvent = do_QueryInterface(aKeyEvent);
  if (!keyEvent) {
    //non-key event passed to keypress.  bad things.
    return NS_OK;
  }

  return mEditor->HandleKeyPressEvent(keyEvent);
}

/**
 * nsIDOMMouseListener implementation
 */

NS_IMETHODIMP
nsEditorEventListener::MouseClick(nsIDOMEvent* aMouseEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIDOMMouseEvent> mouseEvent = do_QueryInterface(aMouseEvent);
  nsCOMPtr<nsIDOMNSEvent> nsevent = do_QueryInterface(aMouseEvent);
  PRBool isTrusted = PR_FALSE;
  if (!mouseEvent || !nsevent ||
      NS_FAILED(nsevent->GetIsTrusted(&isTrusted)) || !isTrusted) {
    // Non-ui or non-trusted event passed in. Bad things.
    return NS_OK;
  }

  nsresult rv;
  nsCOMPtr<nsIDOMNSUIEvent> nsuiEvent = do_QueryInterface(aMouseEvent);
  NS_ENSURE_TRUE(nsuiEvent, NS_ERROR_NULL_POINTER);

  PRBool preventDefault;
  rv = nsuiEvent->GetPreventDefault(&preventDefault);
  if (NS_FAILED(rv) || preventDefault)
  {
    // We're done if 'preventdefault' is true (see for example bug 70698).
    return rv;
  }

  // If we got a mouse down inside the editing area, we should force the 
  // IME to commit before we change the cursor position
  mEditor->ForceCompositionEnd();

  PRUint16 button = (PRUint16)-1;
  mouseEvent->GetButton(&button);
  // middle-mouse click (paste);
  if (button == 1)
  {
    nsCOMPtr<nsIPrefBranch> prefBranch =
      do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv) && prefBranch)
    {
      PRBool doMiddleMousePaste = PR_FALSE;;
      rv = prefBranch->GetBoolPref("middlemouse.paste", &doMiddleMousePaste);
      if (NS_SUCCEEDED(rv) && doMiddleMousePaste)
      {
        // Set the selection to the point under the mouse cursor:
        nsCOMPtr<nsIDOMNode> parent;
        if (NS_FAILED(nsuiEvent->GetRangeParent(getter_AddRefs(parent))))
          return NS_ERROR_NULL_POINTER;
        PRInt32 offset = 0;
        if (NS_FAILED(nsuiEvent->GetRangeOffset(&offset)))
          return NS_ERROR_NULL_POINTER;

        nsCOMPtr<nsISelection> selection;
        if (NS_SUCCEEDED(mEditor->GetSelection(getter_AddRefs(selection))))
          (void)selection->Collapse(parent, offset);

        // If the ctrl key is pressed, we'll do paste as quotation.
        // Would've used the alt key, but the kde wmgr treats alt-middle specially. 
        PRBool ctrlKey = PR_FALSE;
        mouseEvent->GetCtrlKey(&ctrlKey);

        nsCOMPtr<nsIEditorMailSupport> mailEditor;
        if (ctrlKey)
          mailEditor = do_QueryInterface(static_cast<nsIEditor*>(mEditor));

        PRInt32 clipboard;

#if defined(XP_OS2) || defined(XP_WIN32)
        clipboard = nsIClipboard::kGlobalClipboard;
#else
        clipboard = nsIClipboard::kSelectionClipboard;
#endif

        if (mailEditor)
          mailEditor->PasteAsQuotation(clipboard);
        else
          mEditor->Paste(clipboard);

        // Prevent the event from propagating up to be possibly handled
        // again by the containing window:
        mouseEvent->StopPropagation();
        mouseEvent->PreventDefault();

        // We processed the event, whether drop/paste succeeded or not
        return NS_OK;
      }
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::MouseDown(nsIDOMEvent* aMouseEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  mEditor->ForceCompositionEnd();
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::MouseUp(nsIDOMEvent* aMouseEvent)
{
  // WARNING: If you change this method, you comment out next line.
  // NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::MouseDblClick(nsIDOMEvent* aMouseEvent)
{
  // WARNING: If you change this method, you comment out next line.
  // NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::MouseOver(nsIDOMEvent* aMouseEvent)
{
  // WARNING: If you change this method, you comment out next line.
  // NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::MouseOut(nsIDOMEvent* aMouseEvent)
{
  // WARNING: If you change this method, you comment out next line.
  // NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  return NS_OK;
}

/**
 * nsIDOMTextListener implementation
 */

NS_IMETHODIMP
nsEditorEventListener::HandleText(nsIDOMEvent* aTextEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);

  if (!mEditor->IsAcceptableInputEvent(aTextEvent)) {
    return NS_OK;
  }

  nsCOMPtr<nsIPrivateTextEvent> textEvent = do_QueryInterface(aTextEvent);
  if (!textEvent) {
     //non-ui event passed in.  bad things.
     return NS_OK;
  }

  nsAutoString                      composedText;
  nsCOMPtr<nsIPrivateTextRangeList> textRangeList;

  textEvent->GetText(composedText);
  textRangeList = textEvent->GetInputRange();

  // if we are readonly or disabled, then do nothing.
  if (mEditor->IsReadonly() || mEditor->IsDisabled()) {
    return NS_OK;
  }

  return mEditor->UpdateIMEComposition(composedText, textRangeList);
}

/**
 * Drag event implementation
 */

nsresult
nsEditorEventListener::DragGesture(nsIDOMDragEvent* aDragEvent)
{
  // ...figure out if a drag should be started...
  PRBool canDrag;
  nsresult rv = mEditor->CanDrag(aDragEvent, &canDrag);
  if ( NS_SUCCEEDED(rv) && canDrag )
    rv = mEditor->DoDrag(aDragEvent);

  return rv;
}

nsresult
nsEditorEventListener::DragEnter(nsIDOMDragEvent* aDragEvent)
{
  nsCOMPtr<nsIPresShell> presShell = GetPresShell();
  NS_ENSURE_TRUE(presShell, NS_OK);

  if (!mCaret)
  {
    NS_NewCaret(getter_AddRefs(mCaret));
    if (mCaret)
    {
      mCaret->Init(presShell);
      mCaret->SetCaretReadOnly(PR_TRUE);
    }
    mCaretDrawn = PR_FALSE;
  }

  presShell->SetCaret(mCaret);

  return DragOver(aDragEvent);
}

nsresult
nsEditorEventListener::DragOver(nsIDOMDragEvent* aDragEvent)
{
  nsCOMPtr<nsIDOMNode> parent;
  nsCOMPtr<nsIDOMNSUIEvent> nsuiEvent = do_QueryInterface(aDragEvent);
  if (nsuiEvent) {
    PRBool defaultPrevented;
    nsuiEvent->GetPreventDefault(&defaultPrevented);
    if (defaultPrevented)
      return NS_OK;

    nsuiEvent->GetRangeParent(getter_AddRefs(parent));
    nsCOMPtr<nsIContent> dropParent = do_QueryInterface(parent);
    NS_ENSURE_TRUE(dropParent, NS_ERROR_FAILURE);

    if (!dropParent->IsEditable())
      return NS_OK;
  }

  PRBool canDrop = CanDrop(aDragEvent);
  if (canDrop)
  {
    aDragEvent->PreventDefault(); // consumed

    if (mCaret && nsuiEvent)
    {
      PRInt32 offset = 0;
      nsresult rv = nsuiEvent->GetRangeOffset(&offset);
      NS_ENSURE_SUCCESS(rv, rv);

      // to avoid flicker, we could track the node and offset to see if we moved
      if (mCaretDrawn)
        mCaret->EraseCaret();
      
      //mCaret->SetCaretVisible(PR_TRUE);   // make sure it's visible
      mCaret->DrawAtPosition(parent, offset);
      mCaretDrawn = PR_TRUE;
    }
  }
  else
  {
    if (mCaret && mCaretDrawn)
    {
      mCaret->EraseCaret();
      mCaretDrawn = PR_FALSE;
    } 
  }

  return NS_OK;
}

nsresult
nsEditorEventListener::DragLeave(nsIDOMDragEvent* aDragEvent)
{
  if (mCaret && mCaretDrawn)
  {
    mCaret->EraseCaret();
    mCaretDrawn = PR_FALSE;
  }

  nsCOMPtr<nsIPresShell> presShell = GetPresShell();
  if (presShell)
    presShell->RestoreCaret();

  return NS_OK;
}

nsresult
nsEditorEventListener::Drop(nsIDOMDragEvent* aMouseEvent)
{
  if (mCaret)
  {
    if (mCaretDrawn)
    {
      mCaret->EraseCaret();
      mCaretDrawn = PR_FALSE;
    }
    mCaret->SetCaretVisible(PR_FALSE);    // hide it, so that it turns off its timer

    nsCOMPtr<nsIPresShell> presShell = GetPresShell();
    if (presShell)
    {
      presShell->RestoreCaret();
    }
  }

  nsCOMPtr<nsIDOMNSUIEvent> nsuiEvent = do_QueryInterface(aMouseEvent);
  if (nsuiEvent) {
    PRBool defaultPrevented;
    nsuiEvent->GetPreventDefault(&defaultPrevented);
    if (defaultPrevented)
      return NS_OK;

    nsCOMPtr<nsIDOMNode> parent;
    nsuiEvent->GetRangeParent(getter_AddRefs(parent));
    nsCOMPtr<nsIContent> dropParent = do_QueryInterface(parent);
    NS_ENSURE_TRUE(dropParent, NS_ERROR_FAILURE);

    if (!dropParent->IsEditable())
      return NS_OK;
  }

  PRBool canDrop = CanDrop(aMouseEvent);
  if (!canDrop)
  {
    // was it because we're read-only?
    if (mEditor->IsReadonly() || mEditor->IsDisabled())
    {
      // it was decided to "eat" the event as this is the "least surprise"
      // since someone else handling it might be unintentional and the 
      // user could probably re-drag to be not over the disabled/readonly 
      // editfields if that is what is desired.
      return aMouseEvent->StopPropagation();
    }
    return NS_OK;
  }

  aMouseEvent->StopPropagation();
  aMouseEvent->PreventDefault();
  // Beware! This may flush notifications via synchronous
  // ScrollSelectionIntoView.
  return mEditor->InsertFromDrop(aMouseEvent);
}

PRBool
nsEditorEventListener::CanDrop(nsIDOMDragEvent* aEvent)
{
  // if the target doc is read-only, we can't drop
  if (mEditor->IsReadonly() || mEditor->IsDisabled()) {
    return PR_FALSE;
  }

  nsCOMPtr<nsIDOMDataTransfer> dataTransfer;
  aEvent->GetDataTransfer(getter_AddRefs(dataTransfer));
  NS_ENSURE_TRUE(dataTransfer, PR_FALSE);

  nsCOMPtr<nsIDOMDOMStringList> types;
  dataTransfer->GetTypes(getter_AddRefs(types));
  NS_ENSURE_TRUE(types, PR_FALSE);

  // Plaintext editors only support dropping text. Otherwise, HTML and files
  // can be dropped as well.
  PRBool typeSupported;
  types->Contains(NS_LITERAL_STRING(kTextMime), &typeSupported);
  if (!typeSupported) {
    types->Contains(NS_LITERAL_STRING(kMozTextInternal), &typeSupported);
    if (!typeSupported && !mEditor->IsPlaintextEditor()) {
      types->Contains(NS_LITERAL_STRING(kHTMLMime), &typeSupported);
      if (!typeSupported) {
        types->Contains(NS_LITERAL_STRING(kFileMime), &typeSupported);
      }
    }
  }

  NS_ENSURE_TRUE(typeSupported, PR_FALSE);

  nsCOMPtr<nsIDOMNSDataTransfer> dataTransferNS(do_QueryInterface(dataTransfer));
  NS_ENSURE_TRUE(dataTransferNS, PR_FALSE);

  // If there is no source node, this is probably an external drag and the
  // drop is allowed. The later checks rely on checking if the drag target
  // is the same as the drag source.
  nsCOMPtr<nsIDOMNode> sourceNode;
  dataTransferNS->GetMozSourceNode(getter_AddRefs(sourceNode));
  NS_ENSURE_TRUE(sourceNode, PR_TRUE);

  // There is a source node, so compare the source documents and this document.
  // Disallow drops on the same document.

  nsCOMPtr<nsIDOMDocument> domdoc;
  nsresult rv = mEditor->GetDocument(getter_AddRefs(domdoc));
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  nsCOMPtr<nsIDOMDocument> sourceDoc;
  rv = sourceNode->GetOwnerDocument(getter_AddRefs(sourceDoc));
  NS_ENSURE_SUCCESS(rv, PR_FALSE);
  if (domdoc == sourceDoc)      // source and dest are the same document; disallow drops within the selection
  {
    nsCOMPtr<nsISelection> selection;
    rv = mEditor->GetSelection(getter_AddRefs(selection));
    if (NS_FAILED(rv) || !selection)
      return PR_FALSE;
    
    PRBool isCollapsed;
    rv = selection->GetIsCollapsed(&isCollapsed);
    NS_ENSURE_SUCCESS(rv, PR_FALSE);
  
    // Don't bother if collapsed - can always drop
    if (!isCollapsed)
    {
      nsCOMPtr<nsIDOMNSUIEvent> nsuiEvent (do_QueryInterface(aEvent));
      NS_ENSURE_TRUE(nsuiEvent, PR_FALSE);

      nsCOMPtr<nsIDOMNode> parent;
      rv = nsuiEvent->GetRangeParent(getter_AddRefs(parent));
      if (NS_FAILED(rv) || !parent) return PR_FALSE;

      PRInt32 offset = 0;
      rv = nsuiEvent->GetRangeOffset(&offset);
      NS_ENSURE_SUCCESS(rv, PR_FALSE);

      PRInt32 rangeCount;
      rv = selection->GetRangeCount(&rangeCount);
      NS_ENSURE_SUCCESS(rv, PR_FALSE);

      for (PRInt32 i = 0; i < rangeCount; i++)
      {
        nsCOMPtr<nsIDOMRange> range;
        rv = selection->GetRangeAt(i, getter_AddRefs(range));
        nsCOMPtr<nsIDOMNSRange> nsrange(do_QueryInterface(range));
        if (NS_FAILED(rv) || !nsrange) 
          continue; //don't bail yet, iterate through them all

        PRBool inRange = PR_TRUE;
        (void)nsrange->IsPointInRange(parent, offset, &inRange);
        if (inRange)
          return PR_FALSE;  //okay, now you can bail, we are over the orginal selection
      }
    }
  }
  
  return PR_TRUE;
}

/**
 * nsIDOMCompositionListener implementation
 */

NS_IMETHODIMP
nsEditorEventListener::HandleStartComposition(nsIDOMEvent* aCompositionEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  if (!mEditor->IsAcceptableInputEvent(aCompositionEvent)) {
    return NS_OK;
  }
  return mEditor->BeginIMEComposition();
}

NS_IMETHODIMP
nsEditorEventListener::HandleEndComposition(nsIDOMEvent* aCompositionEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  if (!mEditor->IsAcceptableInputEvent(aCompositionEvent)) {
    return NS_OK;
  }
  return mEditor->EndIMEComposition();
}

/**
 * nsIDOMFocusListener implementation
 */

NS_IMETHODIMP
nsEditorEventListener::Focus(nsIDOMEvent* aEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_ARG(aEvent);

  // Don't turn on selection and caret when the editor is disabled.
  if (mEditor->IsDisabled()) {
    return NS_OK;
  }

  nsCOMPtr<nsIDOMEventTarget> target;
  aEvent->GetTarget(getter_AddRefs(target));
  nsCOMPtr<nsINode> node = do_QueryInterface(target);
  NS_ENSURE_TRUE(node, NS_ERROR_UNEXPECTED);

  // If the traget is a document node but it's not editable, we should ignore
  // it because actual focused element's event is going to come.
  if (node->IsNodeOfType(nsINode::eDOCUMENT) &&
      !node->HasFlag(NODE_IS_EDITABLE)) {
    return NS_OK;
  }

  if (node->IsNodeOfType(nsINode::eCONTENT)) {
    // XXX If the focus event target is a form control in contenteditable
    // element, perhaps, the parent HTML editor should do nothing by this
    // handler.  However, FindSelectionRoot() returns the root element of the
    // contenteditable editor.  So, the editableRoot value is invalid for
    // the plain text editor, and it will be set to the wrong limiter of
    // the selection.  However, fortunately, actual bugs are not found yet.
    nsCOMPtr<nsIContent> editableRoot = mEditor->FindSelectionRoot(node);

    // make sure that the element is really focused in case an earlier
    // listener in the chain changed the focus.
    if (editableRoot) {
      nsIFocusManager* fm = nsFocusManager::GetFocusManager();
      NS_ENSURE_TRUE(fm, NS_OK);

      nsCOMPtr<nsIDOMElement> element;
      fm->GetFocusedElement(getter_AddRefs(element));
      if (!SameCOMIdentity(element, target))
        return NS_OK;
    }
  }

  mEditor->InitializeSelection(target);
  return NS_OK;
}

NS_IMETHODIMP
nsEditorEventListener::Blur(nsIDOMEvent* aEvent)
{
  NS_ENSURE_TRUE(mEditor, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_ARG(aEvent);

  // check if something else is focused. If another element is focused, then
  // we should not change the selection.
  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  NS_ENSURE_TRUE(fm, NS_OK);

  nsCOMPtr<nsIDOMElement> element;
  fm->GetFocusedElement(getter_AddRefs(element));
  if (element)
    return NS_OK;

  // turn off selection and caret
  nsCOMPtr<nsISelectionController>selCon;
  mEditor->GetSelectionController(getter_AddRefs(selCon));
  if (selCon)
  {
    nsCOMPtr<nsISelection> selection;
    selCon->GetSelection(nsISelectionController::SELECTION_NORMAL,
                         getter_AddRefs(selection));

    nsCOMPtr<nsISelectionPrivate> selectionPrivate =
      do_QueryInterface(selection);
    if (selectionPrivate) {
      selectionPrivate->SetAncestorLimiter(nsnull);
    }

    nsCOMPtr<nsIPresShell> presShell = GetPresShell();
    if (presShell) {
      nsRefPtr<nsCaret> caret = presShell->GetCaret();
      if (caret) {
        caret->SetIgnoreUserModify(PR_TRUE);
      }
    }

    selCon->SetCaretEnabled(PR_FALSE);

    if(mEditor->IsFormWidget() || mEditor->IsPasswordEditor() ||
       mEditor->IsReadonly() || mEditor->IsDisabled() ||
       mEditor->IsInputFiltered())
    {
      selCon->SetDisplaySelection(nsISelectionController::SELECTION_HIDDEN);//hide but do NOT turn off
    }
    else
    {
      selCon->SetDisplaySelection(nsISelectionController::SELECTION_DISABLED);
    }

    selCon->RepaintSelection(nsISelectionController::SELECTION_NORMAL);
  }

  return NS_OK;
}

