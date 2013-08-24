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
 * The Original Code is mozilla.org code.
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

#ifndef nsCopySupport_h__
#define nsCopySupport_h__

#include "nscore.h"
#include "nsINode.h"

class nsISelection;
class nsIDocument;
class nsIImageLoadingContent;
class nsIContent;
class nsITransferable;
class nsACString;
class nsAString;
class nsIPresShell;

class nsCopySupport
{
  // class of static helper functions for copy support
  public:
    static nsresult HTMLCopy(nsISelection *aSel, nsIDocument *aDoc, PRInt16 aClipboardID);
    static nsresult DoHooks(nsIDocument *aDoc, nsITransferable *aTrans,
                            PRBool *aDoPutOnClipboard);
    static nsresult IsPlainTextContext(nsISelection *aSel, nsIDocument *aDoc, PRBool *aIsPlainTextContext);

    // Get the selection, or entire document, in the format specified by the mime type
    // (text/html or text/plain). If aSel is non-null, use it, otherwise get the entire
    // doc.
    static nsresult GetContents(const nsACString& aMimeType, PRUint32 aFlags, nsISelection *aSel, nsIDocument *aDoc, nsAString& outdata);
    
    static nsresult ImageCopy(nsIImageLoadingContent* aImageElement,
                              PRInt32 aCopyFlags);

    // Get the selection as a transferable. Similar to HTMLCopy except does
    // not deal with the clipboard.
    static nsresult GetTransferableForSelection(nsISelection* aSelection,
                                                nsIDocument* aDocument,
                                                nsITransferable** aTransferable);

    // Same as GetTransferableForSelection, but doesn't skip invisible content.
    static nsresult GetTransferableForNode(nsINode* aNode,
                                           nsIDocument* aDoc,
                                           nsITransferable** aTransferable);
    /**
     * Retrieve the selection for the given document. If the current focus
     * within the document has its own selection, aSelection will be set to it
     * and this focused content node returned. Otherwise, aSelection will be
     * set to the document's selection and null will be returned.
     */
    static nsIContent* GetSelectionForCopy(nsIDocument* aDocument,
                                           nsISelection** aSelection);

    /**
     * Returns true if a copy operation is currently permitted based on the
     * current focus and selection within the specified document.
     */
    static PRBool CanCopy(nsIDocument* aDocument);

    /**
     * Fires a cut, copy or paste event, on the given presshell, depending
     * on the value of aType, which should be either NS_CUT, NS_COPY or
     * NS_PASTE, and perform the default copy action if the event was not
     * cancelled.
     *
     * If aSelection is specified, then this selection is used as the target
     * of the operation. Otherwise, GetSelectionForCopy is used to retrieve
     * the current selection.
     *
     * This will fire a cut, copy or paste event at the node at the start
     * point of the selection. If a cut or copy event is not cancelled, the
     * selection is copied to the clipboard and true is returned. Paste events
     * have no default behaviour but true will be returned. It is expected
     * that the caller will execute any needed default paste behaviour. Also,
     * note that this method only copies text to the clipboard, the caller is
     * responsible for removing the content during a cut operation if true is
     * returned.
     *
     * If the event is cancelled or an error occurs, false will be returned.
     */
    static PRBool FireClipboardEvent(PRInt32 aType,
                                     nsIPresShell* aPresShell,
                                     nsISelection* aSelection);
};

#endif
