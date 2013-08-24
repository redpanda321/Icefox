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
 * The Initial Developers of the Original Code are
 * Sun Microsystems and IBM Corporation
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ginn Chen (ginn.chen@sun.com)
 *   Aaron Leventhal (aleventh@us.ibm.com)
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

#include "nsHyperTextAccessible.h"

#include "nsAccessibilityAtoms.h"
#include "nsAccessibilityService.h"
#include "nsAccUtils.h"
#include "nsTextAttrs.h"

#include "nsIClipboard.h"
#include "nsContentCID.h"
#include "nsIDOMAbstractView.h"
#include "nsIDOMCharacterData.h"
#include "nsIDOMDocument.h"
#include "nsPIDOMWindow.h"        
#include "nsIDOMDocumentView.h"
#include "nsIDOMRange.h"
#include "nsIDOMNSRange.h"
#include "nsIDOMWindowInternal.h"
#include "nsIDOMXULDocument.h"
#include "nsIEditingSession.h"
#include "nsIEditor.h"
#include "nsIFontMetrics.h"
#include "nsIFrame.h"
#include "nsFrameSelection.h"
#include "nsILineIterator.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIPlaintextEditor.h"
#include "nsIScrollableFrame.h"
#include "nsISelection2.h"
#include "nsISelectionPrivate.h"
#include "nsIServiceManager.h"
#include "nsTextFragment.h"
#include "gfxSkipChars.h"

static NS_DEFINE_IID(kRangeCID, NS_RANGE_CID);

////////////////////////////////////////////////////////////////////////////////
// nsHyperTextAccessible
////////////////////////////////////////////////////////////////////////////////

nsHyperTextAccessible::
  nsHyperTextAccessible(nsIContent *aNode, nsIWeakReference *aShell) :
  nsAccessibleWrap(aNode, aShell)
{
}

NS_IMPL_ADDREF_INHERITED(nsHyperTextAccessible, nsAccessibleWrap)
NS_IMPL_RELEASE_INHERITED(nsHyperTextAccessible, nsAccessibleWrap)

nsresult nsHyperTextAccessible::QueryInterface(REFNSIID aIID, void** aInstancePtr)
{
  *aInstancePtr = nsnull;

  if (aIID.Equals(NS_GET_IID(nsHyperTextAccessible))) {
    *aInstancePtr = static_cast<nsHyperTextAccessible*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  if (mRoleMapEntry &&
      (mRoleMapEntry->role == nsIAccessibleRole::ROLE_GRAPHIC ||
       mRoleMapEntry->role == nsIAccessibleRole::ROLE_IMAGE_MAP ||
       mRoleMapEntry->role == nsIAccessibleRole::ROLE_SLIDER ||
       mRoleMapEntry->role == nsIAccessibleRole::ROLE_PROGRESSBAR ||
       mRoleMapEntry->role == nsIAccessibleRole::ROLE_SEPARATOR)) {
    // ARIA roles that these interfaces are not appropriate for
    return nsAccessible::QueryInterface(aIID, aInstancePtr);
  }

  if (aIID.Equals(NS_GET_IID(nsIAccessibleText))) {
    *aInstancePtr = static_cast<nsIAccessibleText*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsIAccessibleHyperText))) {
    *aInstancePtr = static_cast<nsIAccessibleHyperText*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsIAccessibleEditableText))) {
    *aInstancePtr = static_cast<nsIAccessibleEditableText*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  return nsAccessible::QueryInterface(aIID, aInstancePtr);
}

nsresult
nsHyperTextAccessible::GetRoleInternal(PRUint32 *aRole)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsIAtom *tag = mContent->Tag();

  if (tag == nsAccessibilityAtoms::form) {
    *aRole = nsIAccessibleRole::ROLE_FORM;
  }
  else if (tag == nsAccessibilityAtoms::div ||
           tag == nsAccessibilityAtoms::blockquote) {
    *aRole = nsIAccessibleRole::ROLE_SECTION;
  }
  else if (tag == nsAccessibilityAtoms::h1 ||
           tag == nsAccessibilityAtoms::h2 ||
           tag == nsAccessibilityAtoms::h3 ||
           tag == nsAccessibilityAtoms::h4 ||
           tag == nsAccessibilityAtoms::h5 ||
           tag == nsAccessibilityAtoms::h6) {
    *aRole = nsIAccessibleRole::ROLE_HEADING;
  }
  else {
    nsIFrame *frame = GetFrame();
    if (frame && frame->GetType() == nsAccessibilityAtoms::blockFrame &&
        frame->GetContent()->Tag() != nsAccessibilityAtoms::input) {
      // An html:input @type="file" is the only input that is exposed as a
      // blockframe. It must be exposed as ROLE_TEXT_CONTAINER for JAWS.
      *aRole = nsIAccessibleRole::ROLE_PARAGRAPH;
    }
    else {
      *aRole = nsIAccessibleRole::ROLE_TEXT_CONTAINER; // In ATK this works
    }
  }
  return NS_OK;
}

nsresult
nsHyperTextAccessible::GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState)
{
  nsresult rv = nsAccessibleWrap::GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  if (!aExtraState)
    return NS_OK;

  nsCOMPtr<nsIEditor> editor;
  GetAssociatedEditor(getter_AddRefs(editor));
  if (editor) {
    PRUint32 flags;
    editor->GetFlags(&flags);
    if (0 == (flags & nsIPlaintextEditor::eEditorReadonlyMask)) {
      *aExtraState |= nsIAccessibleStates::EXT_STATE_EDITABLE;
    }
  }

  PRInt32 childCount;
  GetChildCount(&childCount);
  if (childCount > 0) {
    *aExtraState |= nsIAccessibleStates::EXT_STATE_SELECTABLE_TEXT;
  }

  return NS_OK;
}

// Substring must be entirely within the same text node
nsIntRect nsHyperTextAccessible::GetBoundsForString(nsIFrame *aFrame, PRUint32 aStartRenderedOffset,
                                                    PRUint32 aEndRenderedOffset)
{
  nsIntRect screenRect;
  NS_ENSURE_TRUE(aFrame, screenRect);
  if (aFrame->GetType() != nsAccessibilityAtoms::textFrame) {
    // XXX fallback for non-text frames, happens for bullets right now
    // but in the future bullets will have proper text frames
    return aFrame->GetScreenRectExternal();
  }

  PRInt32 startContentOffset, endContentOffset;
  nsresult rv = RenderedToContentOffset(aFrame, aStartRenderedOffset, &startContentOffset);
  NS_ENSURE_SUCCESS(rv, screenRect);
  rv = RenderedToContentOffset(aFrame, aEndRenderedOffset, &endContentOffset);
  NS_ENSURE_SUCCESS(rv, screenRect);

  nsIFrame *frame;
  PRInt32 startContentOffsetInFrame;
  // Get the right frame continuation -- not really a child, but a sibling of
  // the primary frame passed in
  rv = aFrame->GetChildFrameContainingOffset(startContentOffset, PR_FALSE,
                                             &startContentOffsetInFrame, &frame);
  NS_ENSURE_SUCCESS(rv, screenRect);

  nsCOMPtr<nsIPresShell> shell = GetPresShell();
  NS_ENSURE_TRUE(shell, screenRect);

  nsPresContext *context = shell->GetPresContext();

  while (frame && startContentOffset < endContentOffset) {
    // Start with this frame's screen rect, which we will 
    // shrink based on the substring we care about within it.
    // We will then add that frame to the total screenRect we
    // are returning.
    nsIntRect frameScreenRect = frame->GetScreenRectExternal();

    // Get the length of the substring in this frame that we want the bounds for
    PRInt32 startFrameTextOffset, endFrameTextOffset;
    frame->GetOffsets(startFrameTextOffset, endFrameTextOffset);
    PRInt32 frameTotalTextLength = endFrameTextOffset - startFrameTextOffset;
    PRInt32 seekLength = endContentOffset - startContentOffset;
    PRInt32 frameSubStringLength = NS_MIN(frameTotalTextLength - startContentOffsetInFrame, seekLength);

    // Add the point where the string starts to the frameScreenRect
    nsPoint frameTextStartPoint;
    rv = frame->GetPointFromOffset(startContentOffset, &frameTextStartPoint);
    NS_ENSURE_SUCCESS(rv, nsIntRect());
    frameScreenRect.x += context->AppUnitsToDevPixels(frameTextStartPoint.x);

    // Use the point for the end offset to calculate the width
    nsPoint frameTextEndPoint;
    rv = frame->GetPointFromOffset(startContentOffset + frameSubStringLength, &frameTextEndPoint);
    NS_ENSURE_SUCCESS(rv, nsIntRect());
    frameScreenRect.width = context->AppUnitsToDevPixels(frameTextEndPoint.x - frameTextStartPoint.x);

    screenRect.UnionRect(frameScreenRect, screenRect);

    // Get ready to loop back for next frame continuation
    startContentOffset += frameSubStringLength;
    startContentOffsetInFrame = 0;
    frame = frame->GetNextContinuation();
  }

  return screenRect;
}

/*
 * Gets the specified text.
 */
nsIFrame*
nsHyperTextAccessible::GetPosAndText(PRInt32& aStartOffset, PRInt32& aEndOffset,
                                     nsAString *aText, nsIFrame **aEndFrame,
                                     nsIntRect *aBoundsRect,
                                     nsAccessible **aStartAcc,
                                     nsAccessible **aEndAcc)
{
  if (aStartOffset == nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT) {
    GetCharacterCount(&aStartOffset);
  }
  if (aStartOffset == nsIAccessibleText::TEXT_OFFSET_CARET) {
    GetCaretOffset(&aStartOffset);
  }
  if (aEndOffset == nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT) {
    GetCharacterCount(&aEndOffset);
  }
  if (aEndOffset == nsIAccessibleText::TEXT_OFFSET_CARET) {
    GetCaretOffset(&aEndOffset);
  }

  PRInt32 startOffset = aStartOffset;
  PRInt32 endOffset = aEndOffset;
  // XXX this prevents text interface usage on <input type="password">
  PRBool isPassword =
    (nsAccUtils::Role(this) == nsIAccessibleRole::ROLE_PASSWORD_TEXT);

  // Clear out parameters and set up loop
  if (aText) {
    aText->Truncate();
  }
  if (endOffset < 0) {
    const PRInt32 kMaxTextLength = 32767;
    endOffset = kMaxTextLength; // Max end offset
  }
  else if (startOffset > endOffset) {
    return nsnull;
  }

  nsIFrame *startFrame = nsnull;
  if (aEndFrame) {
    *aEndFrame = nsnull;
  }
  if (aBoundsRect) {
    aBoundsRect->Empty();
  }
  if (aStartAcc)
    *aStartAcc = nsnull;
  if (aEndAcc)
    *aEndAcc = nsnull;

  nsIntRect unionRect;
  nsAccessible *lastAccessible = nsnull;

  gfxSkipChars skipChars;
  gfxSkipCharsIterator iter;

  // Loop through children and collect valid offsets, text and bounds
  // depending on what we need for out parameters.
  PRInt32 childCount = GetChildCount();
  for (PRInt32 childIdx = 0; childIdx < childCount; childIdx++) {
    nsAccessible *childAcc = mChildren[childIdx];
    lastAccessible = childAcc;

    nsIFrame *frame = childAcc->GetFrame();
    if (!frame) {
      continue;
    }
    nsIFrame *primaryFrame = frame;
    if (nsAccUtils::IsText(childAcc)) {
      // We only need info up to rendered offset -- that is what we're
      // converting to content offset
      PRInt32 substringEndOffset = -1;
      PRUint32 ourRenderedStart = 0;
      PRInt32 ourContentStart = 0;
      if (frame->GetType() == nsAccessibilityAtoms::textFrame) {
        nsresult rv = frame->GetRenderedText(nsnull, &skipChars, &iter);
        if (NS_SUCCEEDED(rv)) {
          ourRenderedStart = iter.GetSkippedOffset();
          ourContentStart = iter.GetOriginalOffset();
          substringEndOffset =
            iter.ConvertOriginalToSkipped(skipChars.GetOriginalCharCount() +
                                          ourContentStart) - ourRenderedStart;
        }
      }
      if (substringEndOffset < 0) {
        // XXX for non-textframe text like list bullets,
        // should go away after list bullet rewrite
        substringEndOffset = nsAccUtils::TextLength(childAcc);
      }
      if (startOffset < substringEndOffset) {
        // Our start is within this substring
        if (startOffset > 0 || endOffset < substringEndOffset) {
          // We don't want the whole string for this accessible
          // Get out the continuing text frame with this offset
          PRInt32 outStartLineUnused;
          PRInt32 contentOffset;
          if (frame->GetType() == nsAccessibilityAtoms::textFrame) {
            contentOffset = iter.ConvertSkippedToOriginal(startOffset) +
                            ourRenderedStart - ourContentStart;
          }
          else {
            contentOffset = startOffset;
          }
          frame->GetChildFrameContainingOffset(contentOffset, PR_TRUE,
                                               &outStartLineUnused, &frame);
          if (aEndFrame) {
            *aEndFrame = frame; // We ended in the current frame
            if (aEndAcc)
              NS_ADDREF(*aEndAcc = childAcc);
          }
          if (substringEndOffset > endOffset) {
            // Need to stop before the end of the available text
            substringEndOffset = endOffset;
          }
          aEndOffset = endOffset;
        }
        if (aText) {
          if (isPassword) {
            for (PRInt32 count = startOffset; count < substringEndOffset; count ++)
              *aText += '*'; // Show *'s only for password text
          }
          else {
            childAcc->AppendTextTo(*aText, startOffset,
                                   substringEndOffset - startOffset);
          }
        }
        if (aBoundsRect) {    // Caller wants the bounds of the text
          aBoundsRect->UnionRect(*aBoundsRect,
                                 GetBoundsForString(primaryFrame, startOffset,
                                                    substringEndOffset));
        }
        if (!startFrame) {
          startFrame = frame;
          aStartOffset = startOffset;
          if (aStartAcc)
            NS_ADDREF(*aStartAcc = childAcc);
        }
        // We already started copying in this accessible's string,
        // for the next accessible we'll start at offset 0
        startOffset = 0;
      }
      else {
        // We have not found the start position yet, get the new startOffset
        // that is relative to next accessible
        startOffset -= substringEndOffset;
      }
      // The endOffset needs to be relative to the new startOffset
      endOffset -= substringEndOffset;
    }
    else {
      // Embedded object, append marker
      // XXX Append \n for <br>'s
      if (startOffset >= 1) {
        -- startOffset;
      }
      else {
        if (endOffset > 0) {
          if (aText) {
            // XXX: should use nsIAccessible::AppendTextTo.
            if (frame->GetType() == nsAccessibilityAtoms::brFrame) {
              *aText += kForcedNewLineChar;
            } else if (nsAccUtils::MustPrune(this)) {
              *aText += kImaginaryEmbeddedObjectChar;
              // Expose imaginary embedded object character if the accessible
              // hans't children.
            } else {
              *aText += kEmbeddedObjectChar;
            }
          }
          if (aBoundsRect) {
            aBoundsRect->UnionRect(*aBoundsRect,
                                   frame->GetScreenRectExternal());
          }
        }
        if (!startFrame) {
          startFrame = frame;
          aStartOffset = 0;
          if (aStartAcc)
            NS_ADDREF(*aStartAcc = childAcc);
        }
      }
      -- endOffset;
    }
    if (endOffset <= 0 && startFrame) {
      break; // If we don't have startFrame yet, get that in next loop iteration
    }
  }

  if (aStartAcc && !*aStartAcc) {
    NS_IF_ADDREF(*aStartAcc = lastAccessible);
  }
  if (aEndFrame && !*aEndFrame) {
    *aEndFrame = startFrame;
    if (aStartAcc && aEndAcc)
      NS_IF_ADDREF(*aEndAcc = *aStartAcc);
  }

  return startFrame;
}

NS_IMETHODIMP nsHyperTextAccessible::GetText(PRInt32 aStartOffset, PRInt32 aEndOffset, nsAString &aText)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  return GetPosAndText(aStartOffset, aEndOffset, &aText) ? NS_OK : NS_ERROR_FAILURE;
}

/*
 * Gets the character count.
 */
NS_IMETHODIMP nsHyperTextAccessible::GetCharacterCount(PRInt32 *aCharacterCount)
{
  NS_ENSURE_ARG_POINTER(aCharacterCount);
  *aCharacterCount = 0;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  PRInt32 childCount = GetChildCount();
  for (PRInt32 childIdx = 0; childIdx < childCount; childIdx++) {
    nsAccessible *childAcc = mChildren[childIdx];

    PRInt32 textLength = nsAccUtils::TextLength(childAcc);
    NS_ENSURE_TRUE(textLength >= 0, nsnull);
    *aCharacterCount += textLength;
  }
  return NS_OK;
}

/*
 * Gets the specified character.
 */
NS_IMETHODIMP nsHyperTextAccessible::GetCharacterAtOffset(PRInt32 aOffset, PRUnichar *aCharacter)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsAutoString text;
  nsresult rv = GetText(aOffset, aOffset + 1, text);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (text.IsEmpty()) {
    return NS_ERROR_FAILURE;
  }
  *aCharacter = text.First();
  return NS_OK;
}

nsAccessible*
nsHyperTextAccessible::DOMPointToHypertextOffset(nsINode *aNode,
                                                 PRInt32 aNodeOffset,
                                                 PRInt32 *aHyperTextOffset,
                                                 PRBool aIsEndOffset)
{
  if (!aHyperTextOffset)
    return nsnull;
  *aHyperTextOffset = 0;

  if (!aNode)
    return nsnull;

  PRUint32 addTextOffset = 0;
  nsINode* findNode = nsnull;

  if (aNodeOffset == -1) {
    findNode = aNode;

  } else if (aNode->IsNodeOfType(nsINode::eTEXT)) {
    // For text nodes, aNodeOffset comes in as a character offset
    // Text offset will be added at the end, if we find the offset in this hypertext
    // We want the "skipped" offset into the text (rendered text without the extra whitespace)
    nsCOMPtr<nsIContent> content = do_QueryInterface(aNode);
    NS_ASSERTION(content, "No nsIContent for dom node");
    nsIFrame *frame = content->GetPrimaryFrame();
    NS_ENSURE_TRUE(frame, nsnull);
    nsresult rv = ContentToRenderedOffset(frame, aNodeOffset, &addTextOffset);
    NS_ENSURE_SUCCESS(rv, nsnull);
    // Get the child node and 
    findNode = aNode;

  } else {
    // findNode could be null if aNodeOffset == # of child nodes, which means
    // one of two things:
    // 1) we're at the end of the children, keep findNode = null, so that we get
    //    the last possible offset
    // 2) there are no children and the passed-in node is mContent, which means
    //    we're an aempty nsIAccessibleText
    // 3) there are no children, and the passed-in node is not mContent -- use
    //    parentContent for the node to find

    findNode = aNode->GetChildAt(aNodeOffset);
    if (!findNode && !aNodeOffset) {
      if (aNode == GetNode()) {
        // There are no children, which means this is an empty nsIAccessibleText, in which
        // case we can only be at hypertext offset 0
        *aHyperTextOffset = 0;
        return nsnull;
      }
      findNode = aNode; // Case #2: there are no children
    }
  }

  // Get accessible for this findNode, or if that node isn't accessible, use the
  // accessible for the next DOM node which has one (based on forward depth first search)
  nsAccessible *descendantAcc = nsnull;
  if (findNode) {
    nsCOMPtr<nsIContent> findContent(do_QueryInterface(findNode));
    if (findContent && findContent->IsHTML() &&
        findContent->NodeInfo()->Equals(nsAccessibilityAtoms::br) &&
        findContent->AttrValueIs(kNameSpaceID_None,
                                 nsAccessibilityAtoms::mozeditorbogusnode,
                                 nsAccessibilityAtoms::_true,
                                 eIgnoreCase)) {
      // This <br> is the hacky "bogus node" used when there is no text in a control
      *aHyperTextOffset = 0;
      return nsnull;
    }
    descendantAcc = GetFirstAvailableAccessible(findNode);
  }

  // From the descendant, go up and get the immediate child of this hypertext
  nsAccessible *childAccAtOffset = nsnull;
  while (descendantAcc) {
    nsAccessible *parentAcc = descendantAcc->GetParent();
    if (parentAcc == this) {
      childAccAtOffset = descendantAcc;
      break;
    }

    // This offset no longer applies because the passed-in text object is not a child
    // of the hypertext. This happens when there are nested hypertexts, e.g.
    // <div>abc<h1>def</h1>ghi</div>
    // If the passed-in DOM point was not on a direct child of the hypertext, we will
    // return the offset for that entire hypertext
    if (aIsEndOffset) {
      // Not inclusive, the indicated char comes at index before this offset
      // If the end offset is after the first character of the passed in object, use 1 for
    // addTextOffset, to put us after the embedded object char. We'll only treat the offset as
    // before the embedded object char if we end at the very beginning of the child.
    addTextOffset = addTextOffset > 0;
    }
    else {
      // Start offset, inclusive
      // Make sure the offset lands on the embedded object character in order to indicate
      // the true inner offset is inside the subtree for that link
      addTextOffset =
        (nsAccUtils::TextLength(descendantAcc) == addTextOffset) ? 1 : 0;
    }

    descendantAcc = parentAcc;
  }

  // Loop through, adding offsets until we reach childAccessible
  // If childAccessible is null we will end up adding up the entire length of
  // the hypertext, which is good -- it just means our offset node
  // came after the last accessible child's node
  PRInt32 childCount = GetChildCount();

  PRInt32 childIdx = 0;
  nsAccessible *childAcc = nsnull;
  for (; childIdx < childCount; childIdx++) {
    childAcc = mChildren[childIdx];
    if (childAcc == childAccAtOffset)
      break;

    *aHyperTextOffset += nsAccUtils::TextLength(childAcc);
  }

  if (childIdx < childCount) {
    *aHyperTextOffset += addTextOffset;
    NS_ASSERTION(childAcc == childAccAtOffset,
                 "These should be equal whenever we exit loop and childAcc != nsnull");

    if (childIdx < childCount - 1 ||
        addTextOffset < nsAccUtils::TextLength(childAccAtOffset)) {
      // If not at end of last text node, we will return the accessible we were in
      return childAccAtOffset;
    }
  }

  return nsnull;
}

nsresult
nsHyperTextAccessible::HypertextOffsetToDOMPoint(PRInt32 aHTOffset,
                                                 nsIDOMNode **aNode,
                                                 PRInt32 *aOffset)
{
  nsCOMPtr<nsIDOMNode> endNode;
  PRInt32 endOffset;

  return HypertextOffsetsToDOMRange(aHTOffset, aHTOffset, aNode, aOffset,
                                    getter_AddRefs(endNode), &endOffset);
}

nsresult
nsHyperTextAccessible::HypertextOffsetsToDOMRange(PRInt32 aStartHTOffset,
                                                  PRInt32 aEndHTOffset,
                                                  nsIDOMNode **aStartNode,
                                                  PRInt32 *aStartOffset,
                                                  nsIDOMNode **aEndNode,
                                                  PRInt32 *aEndOffset)
{
  NS_ENSURE_ARG_POINTER(aStartNode);
  *aStartNode = nsnull;

  NS_ENSURE_ARG_POINTER(aStartOffset);
  *aStartOffset = -1;

  NS_ENSURE_ARG_POINTER(aEndNode);
  *aEndNode = nsnull;

  NS_ENSURE_ARG_POINTER(aEndOffset);
  *aEndOffset = -1;

  // If the given offsets are 0 and associated editor is empty then return
  // collapsed range with editor root element as range container.
  if (aStartHTOffset == 0 && aEndHTOffset == 0) {
    nsCOMPtr<nsIEditor> editor;
    GetAssociatedEditor(getter_AddRefs(editor));
    if (editor) {
      PRBool isEmpty = PR_FALSE;
      editor->GetDocumentIsEmpty(&isEmpty);
      if (isEmpty) {
        nsCOMPtr<nsIDOMElement> editorRootElm;
        editor->GetRootElement(getter_AddRefs(editorRootElm));

        nsCOMPtr<nsIDOMNode> editorRoot(do_QueryInterface(editorRootElm));
        if (editorRoot) {
          *aStartOffset = *aEndOffset = 0;
          NS_ADDREF(*aStartNode = editorRoot);
          NS_ADDREF(*aEndNode = editorRoot);

          return NS_OK;
        }
      }
    }
  }

  nsRefPtr<nsAccessible> startAcc, endAcc;
  PRInt32 startOffset = aStartHTOffset, endOffset = aEndHTOffset;
  nsIFrame *startFrame = nsnull, *endFrame = nsnull;

  startFrame = GetPosAndText(startOffset, endOffset, nsnull, &endFrame, nsnull,
                             getter_AddRefs(startAcc), getter_AddRefs(endAcc));
  if (!startAcc || !endAcc)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIDOMNode> startNode, endNode;
  nsresult rv = GetDOMPointByFrameOffset(startFrame, startOffset, startAcc,
                                         getter_AddRefs(startNode),
                                         &startOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aStartHTOffset != aEndHTOffset) {
    rv = GetDOMPointByFrameOffset(endFrame, endOffset, endAcc,
                                  getter_AddRefs(endNode), &endOffset);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    endNode = startNode;
    endOffset = startOffset;
  }

  NS_ADDREF(*aStartNode = startNode);
  *aStartOffset = startOffset;

  NS_ADDREF(*aEndNode = endNode);
  *aEndOffset = endOffset;

  return NS_OK;
}

PRInt32
nsHyperTextAccessible::GetRelativeOffset(nsIPresShell *aPresShell,
                                         nsIFrame *aFromFrame,
                                         PRInt32 aFromOffset,
                                         nsAccessible *aFromAccessible,
                                         nsSelectionAmount aAmount,
                                         nsDirection aDirection,
                                         PRBool aNeedsStart)
{
  const PRBool kIsJumpLinesOk = PR_TRUE;          // okay to jump lines
  const PRBool kIsScrollViewAStop = PR_FALSE;     // do not stop at scroll views
  const PRBool kIsKeyboardSelect = PR_TRUE;       // is keyboard selection
  const PRBool kIsVisualBidi = PR_FALSE;          // use visual order for bidi text

  EWordMovementType wordMovementType = aNeedsStart ? eStartWord : eEndWord;
  if (aAmount == eSelectLine) {
    aAmount = (aDirection == eDirNext) ? eSelectEndLine : eSelectBeginLine;
  }

  // Ask layout for the new node and offset, after moving the appropriate amount
  nsPeekOffsetStruct pos;

  nsresult rv;
  PRInt32 contentOffset = aFromOffset;
  if (nsAccUtils::IsText(aFromAccessible)) {
    nsIFrame *frame = aFromAccessible->GetFrame();
    NS_ENSURE_TRUE(frame, -1);

    if (frame->GetType() == nsAccessibilityAtoms::textFrame) {
      rv = RenderedToContentOffset(frame, aFromOffset, &contentOffset);
      NS_ENSURE_SUCCESS(rv, -1);
    }
  }

  pos.SetData(aAmount, aDirection, contentOffset,
              0, kIsJumpLinesOk, kIsScrollViewAStop, kIsKeyboardSelect, kIsVisualBidi,
              wordMovementType);
  rv = aFromFrame->PeekOffset(&pos);
  if (NS_FAILED(rv)) {
    if (aDirection == eDirPrevious) {
      // Use passed-in frame as starting point in failure case for now,
      // this is a hack to deal with starting on a list bullet frame,
      // which fails in PeekOffset() because the line iterator doesn't see it.
      // XXX Need to look at our overall handling of list bullets, which are an odd case
      pos.mResultContent = aFromFrame->GetContent();
      PRInt32 endOffsetUnused;
      aFromFrame->GetOffsets(pos.mContentOffset, endOffsetUnused);
    }
    else {
      return -1;
    }
  }

  // Turn the resulting node and offset into a hyperTextOffset
  PRInt32 hyperTextOffset;
  if (!pos.mResultContent)
    return -1;

  // If finalAccessible is nsnull, then DOMPointToHypertextOffset() searched
  // through the hypertext children without finding the node/offset position.
  nsAccessible *finalAccessible =
    DOMPointToHypertextOffset(pos.mResultContent, pos.mContentOffset,
                              &hyperTextOffset, aDirection == eDirNext);

  if (!finalAccessible && aDirection == eDirPrevious) {
    // If we reached the end during search, this means we didn't find the DOM point
    // and we're actually at the start of the paragraph
    hyperTextOffset = 0;
  }  
  else if (aAmount == eSelectBeginLine) {
    nsAccessible *firstChild = mChildren.SafeElementAt(0, nsnull);
    // For line selection with needsStart, set start of line exactly to line break
    if (pos.mContentOffset == 0 && firstChild &&
        nsAccUtils::Role(firstChild) == nsIAccessibleRole::ROLE_STATICTEXT &&
        static_cast<PRInt32>(nsAccUtils::TextLength(firstChild)) == hyperTextOffset) {
      // XXX Bullet hack -- we should remove this once list bullets use anonymous content
      hyperTextOffset = 0;
    }
    if (!aNeedsStart && hyperTextOffset > 0) {
      -- hyperTextOffset;
    }
  }
  else if (aAmount == eSelectEndLine && finalAccessible) { 
    // If not at very end of hypertext, we may need change the end of line offset by 1, 
    // to make sure we are in the right place relative to the line ending
    if (nsAccUtils::Role(finalAccessible) == nsIAccessibleRole::ROLE_WHITESPACE) {  // Landed on <br> hard line break
      // if aNeedsStart, set end of line exactly 1 character past line break
      // XXX It would be cleaner if we did not have to have the hard line break check,
      // and just got the correct results from PeekOffset() for the <br> case -- the returned offset should
      // come after the new line, as it does in other cases.
      ++ hyperTextOffset;  // Get past hard line break
    }
    // We are now 1 character past the line break
    if (!aNeedsStart) {
      -- hyperTextOffset;
    }
  }

  return hyperTextOffset;
}

/*
Gets the specified text relative to aBoundaryType, which means:
BOUNDARY_CHAR             The character before/at/after the offset is returned.
BOUNDARY_WORD_START       From the word start before/at/after the offset to the next word start.
BOUNDARY_WORD_END         From the word end before/at/after the offset to the next work end.
BOUNDARY_LINE_START       From the line start before/at/after the offset to the next line start.
BOUNDARY_LINE_END         From the line end before/at/after the offset to the next line start.
*/

nsresult nsHyperTextAccessible::GetTextHelper(EGetTextType aType, nsAccessibleTextBoundary aBoundaryType,
                                              PRInt32 aOffset, PRInt32 *aStartOffset, PRInt32 *aEndOffset,
                                              nsAString &aText)
{
  aText.Truncate();

  NS_ENSURE_ARG_POINTER(aStartOffset);
  NS_ENSURE_ARG_POINTER(aEndOffset);
  *aStartOffset = *aEndOffset = 0;

  nsCOMPtr<nsIPresShell> presShell = GetPresShell();
  if (!presShell) {
    return NS_ERROR_FAILURE;
  }

  if (aOffset == nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT) {
    GetCharacterCount(&aOffset);
  }
  if (aOffset == nsIAccessibleText::TEXT_OFFSET_CARET) {
    GetCaretOffset(&aOffset);
    if (aOffset > 0 && (aBoundaryType == BOUNDARY_LINE_START ||
                        aBoundaryType == BOUNDARY_LINE_END)) {
      // It is the same character offset when the caret is visually at the very end of a line
      // or the start of a new line. Getting text at the line should provide the line with the visual caret,
      // otherwise screen readers will announce the wrong line as the user presses up or down arrow and land
      // at the end of a line.
      nsCOMPtr<nsISelection> domSel;
      nsresult rv = GetSelections(nsISelectionController::SELECTION_NORMAL,
                                  nsnull, getter_AddRefs(domSel));
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsISelectionPrivate> privateSelection(do_QueryInterface(domSel));
      nsCOMPtr<nsFrameSelection> frameSelection;
      rv = privateSelection->GetFrameSelection(getter_AddRefs(frameSelection));
      NS_ENSURE_SUCCESS(rv, rv);

      if (frameSelection->GetHint() == nsFrameSelection::HINTLEFT) {
        -- aOffset;  // We are at the start of a line
      }
    }
  }
  else if (aOffset < 0) {
    return NS_ERROR_FAILURE;
  }

  nsSelectionAmount amount;
  PRBool needsStart = PR_FALSE;
  switch (aBoundaryType) {
    case BOUNDARY_CHAR:
      amount = eSelectCharacter;
      if (aType == eGetAt)
        aType = eGetAfter; // Avoid returning 2 characters
      break;

    case BOUNDARY_WORD_START:
      needsStart = PR_TRUE;
      amount = eSelectWord;
      break;

    case BOUNDARY_WORD_END:
      amount = eSelectWord;
      break;

    case BOUNDARY_LINE_START:
      // Newlines are considered at the end of a line. Since getting
      // the BOUNDARY_LINE_START gets the text from the line-start to the next
      // line-start, the newline is included at the end of the string.
      needsStart = PR_TRUE;
      amount = eSelectLine;
      break;

    case BOUNDARY_LINE_END:
      // Newlines are considered at the end of a line. Since getting
      // the BOUNDARY_END_START gets the text from the line-end to the next
      //line-end, the newline is included at the beginning of the string.
      amount = eSelectLine;
      break;

    case BOUNDARY_ATTRIBUTE_RANGE:
    {
      nsresult rv = GetTextAttributes(PR_FALSE, aOffset,
                                      aStartOffset, aEndOffset, nsnull);
      NS_ENSURE_SUCCESS(rv, rv);
      
      return GetText(*aStartOffset, *aEndOffset, aText);
    }

    default:  // Note, sentence support is deprecated and falls through to here
      return NS_ERROR_INVALID_ARG;
  }

  PRInt32 startOffset = aOffset + (aBoundaryType == BOUNDARY_LINE_END);  // Avoid getting the previous line
  PRInt32 endOffset = startOffset;

  // Convert offsets to frame-relative
  nsRefPtr<nsAccessible> startAcc;
  nsIFrame *startFrame = GetPosAndText(startOffset, endOffset, nsnull, nsnull,
                                       nsnull, getter_AddRefs(startAcc));

  if (!startFrame) {
    PRInt32 textLength;
    GetCharacterCount(&textLength);
    if (aBoundaryType == BOUNDARY_LINE_START && aOffset > 0 && aOffset == textLength) {
      // Asking for start of line, while on last character
      if (startAcc)
        startFrame = startAcc->GetFrame();
    }
    if (!startFrame) {
      return aOffset > textLength ? NS_ERROR_FAILURE : NS_OK;
    }
    else {
      // We're on the last continuation since we're on the last character
      startFrame = startFrame->GetLastContinuation();
    }
  }

  PRInt32 finalStartOffset, finalEndOffset;

  // If aType == eGetAt we'll change both the start and end offset from
  // the original offset
  if (aType == eGetAfter) {
    finalStartOffset = aOffset;
  }
  else {
    finalStartOffset = GetRelativeOffset(presShell, startFrame, startOffset,
                                         startAcc, amount, eDirPrevious,
                                         needsStart);
    NS_ENSURE_TRUE(finalStartOffset >= 0, NS_ERROR_FAILURE);
  }

  if (aType == eGetBefore) {
    endOffset = aOffset;
  }
  else {
    // Start moving forward from the start so that we don't get 
    // 2 words/lines if the offset occurred on whitespace boundary
    // Careful, startOffset and endOffset are passed by reference to GetPosAndText() and changed
    // For BOUNDARY_LINE_END, make sure we start of this line
    startOffset = endOffset = finalStartOffset + (aBoundaryType == BOUNDARY_LINE_END);
    nsRefPtr<nsAccessible> endAcc;
    nsIFrame *endFrame = GetPosAndText(startOffset, endOffset, nsnull, nsnull,
                                       nsnull, getter_AddRefs(endAcc));
    if (nsAccUtils::Role(endAcc) == nsIAccessibleRole::ROLE_STATICTEXT) {
      // Static text like list bullets will ruin our forward calculation,
      // since the caret cannot be in the static text. Start just after the static text.
      startOffset = endOffset = finalStartOffset +
                                (aBoundaryType == BOUNDARY_LINE_END) +
                                nsAccUtils::TextLength(endAcc);

      endFrame = GetPosAndText(startOffset, endOffset, nsnull, nsnull,
                               nsnull, getter_AddRefs(endAcc));
    }
    if (!endFrame) {
      return NS_ERROR_FAILURE;
    }
    finalEndOffset = GetRelativeOffset(presShell, endFrame, endOffset, endAcc,
                                       amount, eDirNext, needsStart);
    NS_ENSURE_TRUE(endOffset >= 0, NS_ERROR_FAILURE);
    if (finalEndOffset == aOffset) {
      if (aType == eGetAt && amount == eSelectWord) { 
        // Fix word error for the first character in word: PeekOffset() will return the previous word when 
        // aOffset points to the first character of the word, but accessibility APIs want the current word 
        // that the first character is in
        return GetTextHelper(eGetAfter, aBoundaryType, aOffset, aStartOffset, aEndOffset, aText);
      }
      PRInt32 textLength;
      GetCharacterCount(&textLength);
      if (finalEndOffset < textLength) {
        // This happens sometimes when current character at finalStartOffset 
        // is an embedded object character representing another hypertext, that
        // the AT really needs to dig into separately
        ++ finalEndOffset;
      }
    }
  }

  *aStartOffset = finalStartOffset;
  *aEndOffset = finalEndOffset;

  NS_ASSERTION((finalStartOffset < aOffset && finalEndOffset >= aOffset) || aType != eGetBefore, "Incorrect results for GetTextHelper");
  NS_ASSERTION((finalStartOffset <= aOffset && finalEndOffset > aOffset) || aType == eGetBefore, "Incorrect results for GetTextHelper");

  GetPosAndText(finalStartOffset, finalEndOffset, &aText);
  return NS_OK;
}

/**
  * nsIAccessibleText impl.
  */
NS_IMETHODIMP nsHyperTextAccessible::GetTextBeforeOffset(PRInt32 aOffset, nsAccessibleTextBoundary aBoundaryType,
                                                         PRInt32 *aStartOffset, PRInt32 *aEndOffset, nsAString & aText)
{
  return GetTextHelper(eGetBefore, aBoundaryType, aOffset, aStartOffset, aEndOffset, aText);
}

NS_IMETHODIMP nsHyperTextAccessible::GetTextAtOffset(PRInt32 aOffset, nsAccessibleTextBoundary aBoundaryType,
                                                     PRInt32 *aStartOffset, PRInt32 *aEndOffset, nsAString & aText)
{
  return GetTextHelper(eGetAt, aBoundaryType, aOffset, aStartOffset, aEndOffset, aText);
}

NS_IMETHODIMP nsHyperTextAccessible::GetTextAfterOffset(PRInt32 aOffset, nsAccessibleTextBoundary aBoundaryType,
                                                        PRInt32 *aStartOffset, PRInt32 *aEndOffset, nsAString & aText)
{
  return GetTextHelper(eGetAfter, aBoundaryType, aOffset, aStartOffset, aEndOffset, aText);
}

// nsIPersistentProperties
// nsIAccessibleText::getTextAttributes(in boolean includeDefAttrs,
//                                      in long offset,
//                                      out long rangeStartOffset,
//                                      out long rangeEndOffset);
NS_IMETHODIMP
nsHyperTextAccessible::GetTextAttributes(PRBool aIncludeDefAttrs,
                                         PRInt32 aOffset,
                                         PRInt32 *aStartOffset,
                                         PRInt32 *aEndOffset,
                                         nsIPersistentProperties **aAttributes)
{
  // 1. Get each attribute and its ranges one after another.
  // 2. As we get each new attribute, we pass the current start and end offsets
  //    as in/out parameters. In other words, as attributes are collected,
  //    the attribute range itself can only stay the same or get smaller.

  NS_ENSURE_ARG_POINTER(aStartOffset);
  *aStartOffset = 0;

  NS_ENSURE_ARG_POINTER(aEndOffset);
  *aEndOffset = 0;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (aAttributes) {
    *aAttributes = nsnull;

    nsCOMPtr<nsIPersistentProperties> attributes =
      do_CreateInstance(NS_PERSISTENTPROPERTIES_CONTRACTID);
    NS_ENSURE_TRUE(attributes, NS_ERROR_OUT_OF_MEMORY);

    NS_ADDREF(*aAttributes = attributes);
  }

  nsAccessible* accAtOffset = GetChildAtOffset(aOffset);
  if (!accAtOffset) {
    // Offset 0 is correct offset when accessible has empty text. Include
    // default attributes if they were requested, otherwise return empty set.
    if (aOffset == 0) {
      if (aIncludeDefAttrs) {
        nsTextAttrsMgr textAttrsMgr(this, PR_TRUE, nsnull, -1);
        return textAttrsMgr.GetAttributes(*aAttributes);
      }
      return NS_OK;
    }
    return NS_ERROR_INVALID_ARG;
  }

  PRInt32 accAtOffsetIdx = accAtOffset->GetIndexInParent();
  PRInt32 startOffset = GetChildOffset(accAtOffsetIdx);
  PRInt32 endOffset = GetChildOffset(accAtOffsetIdx + 1);
  PRInt32 offsetInAcc = aOffset - startOffset;

  nsTextAttrsMgr textAttrsMgr(this, aIncludeDefAttrs, accAtOffset,
                              accAtOffsetIdx);
  nsresult rv = textAttrsMgr.GetAttributes(*aAttributes, &startOffset,
                                           &endOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  // Compute spelling attributes on text accessible only.
  nsIFrame *offsetFrame = accAtOffset->GetFrame();
  if (offsetFrame && offsetFrame->GetType() == nsAccessibilityAtoms::textFrame) {
    nsCOMPtr<nsIDOMNode> node = accAtOffset->GetDOMNode();

    PRInt32 nodeOffset = 0;
    nsresult rv = RenderedToContentOffset(offsetFrame, offsetInAcc,
                                          &nodeOffset);
    NS_ENSURE_SUCCESS(rv, rv);

    // Set 'misspelled' text attribute.
    rv = GetSpellTextAttribute(node, nodeOffset, &startOffset, &endOffset,
                               aAttributes ? *aAttributes : nsnull);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  *aStartOffset = startOffset;
  *aEndOffset = endOffset;
  return NS_OK;
}

// nsIPersistentProperties
// nsIAccessibleText::defaultTextAttributes
NS_IMETHODIMP
nsHyperTextAccessible::GetDefaultTextAttributes(nsIPersistentProperties **aAttributes)
{
  NS_ENSURE_ARG_POINTER(aAttributes);
  *aAttributes = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIPersistentProperties> attributes =
    do_CreateInstance(NS_PERSISTENTPROPERTIES_CONTRACTID);
  NS_ENSURE_TRUE(attributes, NS_ERROR_OUT_OF_MEMORY);

  NS_ADDREF(*aAttributes = attributes);

  nsTextAttrsMgr textAttrsMgr(this, PR_TRUE);
  return textAttrsMgr.GetAttributes(*aAttributes);
}

PRInt32
nsHyperTextAccessible::GetLevelInternal()
{
  nsIAtom *tag = mContent->Tag();
  if (tag == nsAccessibilityAtoms::h1)
    return 1;
  if (tag == nsAccessibilityAtoms::h2)
    return 2;
  if (tag == nsAccessibilityAtoms::h3)
    return 3;
  if (tag == nsAccessibilityAtoms::h4)
    return 4;
  if (tag == nsAccessibilityAtoms::h5)
    return 5;
  if (tag == nsAccessibilityAtoms::h6)
    return 6;

  return nsAccessibleWrap::GetLevelInternal();
}

nsresult
nsHyperTextAccessible::GetAttributesInternal(nsIPersistentProperties *aAttributes)
{
  nsresult rv = nsAccessibleWrap::GetAttributesInternal(aAttributes);
  NS_ENSURE_SUCCESS(rv, rv);

  // Indicate when the current object uses block-level formatting
  // via formatting: block
  // XXX: 'formatting' attribute is deprecated and will be removed in Mozilla2,
  // use 'display' attribute instead.
  nsIFrame *frame = GetFrame();
  if (frame && frame->GetType() == nsAccessibilityAtoms::blockFrame) {
    nsAutoString oldValueUnused;
    aAttributes->SetStringProperty(NS_LITERAL_CSTRING("formatting"), NS_LITERAL_STRING("block"),
                                   oldValueUnused);
  }

  if (gLastFocusedNode == GetNode()) {
    PRInt32 lineNumber = GetCaretLineNumber();
    if (lineNumber >= 1) {
      nsAutoString strLineNumber;
      strLineNumber.AppendInt(lineNumber);
      nsAccUtils::SetAccAttr(aAttributes, nsAccessibilityAtoms::lineNumber,
                             strLineNumber);
    }
  }

  return  NS_OK;
}

/*
 * Given an offset, the x, y, width, and height values are filled appropriately.
 */
NS_IMETHODIMP nsHyperTextAccessible::GetCharacterExtents(PRInt32 aOffset, PRInt32 *aX, PRInt32 *aY,
                                                         PRInt32 *aWidth, PRInt32 *aHeight,
                                                         PRUint32 aCoordType)
{
  return GetRangeExtents(aOffset, aOffset + 1, aX, aY, aWidth, aHeight, aCoordType);
}

/*
 * Given a start & end offset, the x, y, width, and height values are filled appropriately.
 */
NS_IMETHODIMP nsHyperTextAccessible::GetRangeExtents(PRInt32 aStartOffset, PRInt32 aEndOffset,
                                                     PRInt32 *aX, PRInt32 *aY,
                                                     PRInt32 *aWidth, PRInt32 *aHeight,
                                                     PRUint32 aCoordType)
{
  nsIntRect boundsRect;
  nsIFrame *endFrameUnused;
  if (!GetPosAndText(aStartOffset, aEndOffset, nsnull, &endFrameUnused, &boundsRect) ||
      boundsRect.IsEmpty()) {
    return NS_ERROR_FAILURE;
  }

  *aX = boundsRect.x;
  *aY = boundsRect.y;
  *aWidth = boundsRect.width;
  *aHeight = boundsRect.height;

  return nsAccUtils::ConvertScreenCoordsTo(aX, aY, aCoordType, this);
}

/*
 * Gets the offset of the character located at coordinates x and y. x and y are interpreted as being relative to
 * the screen or this widget's window depending on coords.
 */
NS_IMETHODIMP
nsHyperTextAccessible::GetOffsetAtPoint(PRInt32 aX, PRInt32 aY,
                                        PRUint32 aCoordType, PRInt32 *aOffset)
{
  *aOffset = -1;
  nsCOMPtr<nsIPresShell> shell = GetPresShell();
  if (!shell) {
    return NS_ERROR_FAILURE;
  }
  nsIFrame *hyperFrame = GetFrame();
  if (!hyperFrame) {
    return NS_ERROR_FAILURE;
  }
  nsIntRect frameScreenRect = hyperFrame->GetScreenRectExternal();

  nsIntPoint coords;
  nsresult rv = nsAccUtils::ConvertToScreenCoords(aX, aY, aCoordType,
                                                  this, &coords);
  NS_ENSURE_SUCCESS(rv, rv);

  // coords are currently screen coordinates, and we need to turn them into
  // frame coordinates relative to the current accessible
  if (!frameScreenRect.Contains(coords.x, coords.y)) {
    return NS_OK;   // Not found, will return -1
  }
  nsIntPoint pxInHyperText(coords.x - frameScreenRect.x,
                           coords.y - frameScreenRect.y);
  nsPresContext *context = GetPresContext();
  NS_ENSURE_TRUE(context, NS_ERROR_FAILURE);
  nsPoint pointInHyperText(context->DevPixelsToAppUnits(pxInHyperText.x),
                           context->DevPixelsToAppUnits(pxInHyperText.y));

  // Go through the frames to check if each one has the point.
  // When one does, add up the character offsets until we have a match

  // We have an point in an accessible child of this, now we need to add up the
  // offsets before it to what we already have
  PRInt32 offset = 0;
  PRInt32 childCount = GetChildCount();
  for (PRInt32 childIdx = 0; childIdx < childCount; childIdx++) {
    nsAccessible *childAcc = mChildren[childIdx];

    nsIFrame *primaryFrame = childAcc->GetFrame();
    NS_ENSURE_TRUE(primaryFrame, NS_ERROR_FAILURE);

    nsIFrame *frame = primaryFrame;
    while (frame) {
      nsIContent *content = frame->GetContent();
      NS_ENSURE_TRUE(content, NS_ERROR_FAILURE);
      nsPoint pointInFrame = pointInHyperText - frame->GetOffsetToExternal(hyperFrame);
      nsSize frameSize = frame->GetSize();
      if (pointInFrame.x < frameSize.width && pointInFrame.y < frameSize.height) {
        // Finished
        if (frame->GetType() == nsAccessibilityAtoms::textFrame) {
          nsIFrame::ContentOffsets contentOffsets = frame->GetContentOffsetsFromPointExternal(pointInFrame, PR_TRUE);
          if (contentOffsets.IsNull() || contentOffsets.content != content) {
            return NS_OK; // Not found, will return -1
          }
          PRUint32 addToOffset;
          nsresult rv = ContentToRenderedOffset(primaryFrame,
                                                contentOffsets.offset,
                                                &addToOffset);
          NS_ENSURE_SUCCESS(rv, rv);
          offset += addToOffset;
        }
        *aOffset = offset;
        return NS_OK;
      }
      frame = frame->GetNextContinuation();
    }

    offset += nsAccUtils::TextLength(childAcc);
  }

  return NS_OK; // Not found, will return -1
}


////////////////////////////////////////////////////////////////////////////////
// nsIAccessibleHyperText

NS_IMETHODIMP
nsHyperTextAccessible::GetLinkCount(PRInt32 *aLinkCount)
{
  NS_ENSURE_ARG_POINTER(aLinkCount);
  *aLinkCount = 0;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  *aLinkCount = GetLinkCount();
  return NS_OK;
}

NS_IMETHODIMP
nsHyperTextAccessible::GetLinkAt(PRInt32 aIndex, nsIAccessibleHyperLink** aLink)
{
  NS_ENSURE_ARG_POINTER(aLink);
  *aLink = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsAccessible* link = GetLinkAt(aIndex);
  if (link)
    CallQueryInterface(link, aLink);

  return NS_OK;
}

NS_IMETHODIMP
nsHyperTextAccessible::GetLinkIndex(nsIAccessibleHyperLink* aLink,
                                    PRInt32* aIndex)
{
  NS_ENSURE_ARG_POINTER(aLink);

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsRefPtr<nsAccessible> link(do_QueryObject(aLink));
  *aIndex = GetLinkIndex(link);
  return NS_OK;
}

NS_IMETHODIMP
nsHyperTextAccessible::GetLinkIndexAtOffset(PRInt32 aOffset,
                                            PRInt32* aLinkIndex)
{
  NS_ENSURE_ARG_POINTER(aLinkIndex);
  *aLinkIndex = -1; // API says this magic value means 'not found'

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  *aLinkIndex = GetLinkIndexAtOffset(aOffset);
  return NS_OK;
}

/**
  * nsIAccessibleEditableText impl.
  */
NS_IMETHODIMP nsHyperTextAccessible::SetAttributes(PRInt32 aStartPos, PRInt32 aEndPos,
                                                   nsISupports *aAttributes)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsHyperTextAccessible::SetTextContents(const nsAString &aText)
{
  PRInt32 numChars;
  GetCharacterCount(&numChars);
  if (numChars == 0 || NS_SUCCEEDED(DeleteText(0, numChars))) {
    return InsertText(aText, 0);
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsHyperTextAccessible::InsertText(const nsAString &aText, PRInt32 aPosition)
{
  nsCOMPtr<nsIEditor> editor;
  GetAssociatedEditor(getter_AddRefs(editor));

  nsCOMPtr<nsIPlaintextEditor> peditor(do_QueryInterface(editor));
  NS_ENSURE_STATE(peditor);

  nsresult rv = SetSelectionRange(aPosition, aPosition);
  NS_ENSURE_SUCCESS(rv, rv);

  return peditor->InsertText(aText);
}

NS_IMETHODIMP
nsHyperTextAccessible::CopyText(PRInt32 aStartPos, PRInt32 aEndPos)
{
  nsCOMPtr<nsIEditor> editor;
  GetAssociatedEditor(getter_AddRefs(editor));
  NS_ENSURE_STATE(editor);

  nsresult rv = SetSelectionRange(aStartPos, aEndPos);
  NS_ENSURE_SUCCESS(rv, rv);

  return editor->Copy();
}

NS_IMETHODIMP
nsHyperTextAccessible::CutText(PRInt32 aStartPos, PRInt32 aEndPos)
{
  nsCOMPtr<nsIEditor> editor;
  GetAssociatedEditor(getter_AddRefs(editor));
  NS_ENSURE_STATE(editor);

  nsresult rv = SetSelectionRange(aStartPos, aEndPos);
  NS_ENSURE_SUCCESS(rv, rv);

  return editor->Cut();
}

NS_IMETHODIMP
nsHyperTextAccessible::DeleteText(PRInt32 aStartPos, PRInt32 aEndPos)
{
  nsCOMPtr<nsIEditor> editor;
  GetAssociatedEditor(getter_AddRefs(editor));
  NS_ENSURE_STATE(editor);

  nsresult rv = SetSelectionRange(aStartPos, aEndPos);
  NS_ENSURE_SUCCESS(rv, rv);

  return editor->DeleteSelection(nsIEditor::eNone);
}

NS_IMETHODIMP
nsHyperTextAccessible::PasteText(PRInt32 aPosition)
{
  nsCOMPtr<nsIEditor> editor;
  GetAssociatedEditor(getter_AddRefs(editor));
  NS_ENSURE_STATE(editor);

  nsresult rv = SetSelectionRange(aPosition, aPosition);
  NS_ENSURE_SUCCESS(rv, rv);

  return editor->Paste(nsIClipboard::kGlobalClipboard);
}

NS_IMETHODIMP
nsHyperTextAccessible::GetAssociatedEditor(nsIEditor **aEditor)
{
  NS_ENSURE_ARG_POINTER(aEditor);
  *aEditor = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (!mContent->HasFlag(NODE_IS_EDITABLE)) {
    // If we're inside an editable container, then return that container's editor
    nsCOMPtr<nsIAccessible> ancestor, current = this;
    while (NS_SUCCEEDED(current->GetParent(getter_AddRefs(ancestor))) && ancestor) {
      nsRefPtr<nsHyperTextAccessible> ancestorTextAccessible;
      ancestor->QueryInterface(NS_GET_IID(nsHyperTextAccessible),
                               getter_AddRefs(ancestorTextAccessible));
      if (ancestorTextAccessible) {
        // Recursion will stop at container doc because it has its own impl
        // of GetAssociatedEditor()
        return ancestorTextAccessible->GetAssociatedEditor(aEditor);
      }
      current = ancestor;
    }
    return NS_OK;
  }

  nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem =
    nsCoreUtils::GetDocShellTreeItemFor(mContent);
  nsCOMPtr<nsIEditingSession> editingSession(do_GetInterface(docShellTreeItem));
  if (!editingSession)
    return NS_OK; // No editing session interface

  nsCOMPtr<nsIPresShell> shell = GetPresShell();
  NS_ENSURE_TRUE(shell, NS_ERROR_FAILURE);

  nsCOMPtr<nsIDocument> doc = shell->GetDocument();
  NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);

  nsCOMPtr<nsIEditor> editor;
  return editingSession->GetEditorForWindow(doc->GetWindow(), aEditor);
}

/**
  * =================== Caret & Selection ======================
  */

nsresult
nsHyperTextAccessible::SetSelectionRange(PRInt32 aStartPos, PRInt32 aEndPos)
{
  nsresult rv = TakeFocus();
  NS_ENSURE_SUCCESS(rv, rv);

  // Set the selection
  SetSelectionBounds(0, aStartPos, aEndPos);
  NS_ENSURE_SUCCESS(rv, rv);

  // If range 0 was successfully set, clear any additional selection 
  // ranges remaining from previous selection
  nsCOMPtr<nsISelection> domSel;
  nsCOMPtr<nsISelectionController> selCon;
  GetSelections(nsISelectionController::SELECTION_NORMAL,
                getter_AddRefs(selCon), getter_AddRefs(domSel));
  if (domSel) {
    PRInt32 numRanges;
    domSel->GetRangeCount(&numRanges);

    for (PRInt32 count = 0; count < numRanges - 1; count ++) {
      nsCOMPtr<nsIDOMRange> range;
      domSel->GetRangeAt(1, getter_AddRefs(range));
      domSel->RemoveRange(range);
    }
  }
  
  if (selCon) {
    // XXX I'm not sure this can do synchronous scrolling. If the last param is
    // set to true, this calling might flush the pending reflow. See bug 418470.
    selCon->ScrollSelectionIntoView(nsISelectionController::SELECTION_NORMAL,
      nsISelectionController::SELECTION_FOCUS_REGION, PR_FALSE);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHyperTextAccessible::SetCaretOffset(PRInt32 aCaretOffset)
{
  return SetSelectionRange(aCaretOffset, aCaretOffset);
}

/*
 * Gets the offset position of the caret (cursor).
 */
NS_IMETHODIMP
nsHyperTextAccessible::GetCaretOffset(PRInt32 *aCaretOffset)
{
  *aCaretOffset = -1;

  // No caret if the focused node is not inside this DOM node and this DOM node
  // is not inside of focused node.

  nsINode* thisNode = GetNode();
  PRBool isInsideOfFocusedNode =
    nsCoreUtils::IsAncestorOf(gLastFocusedNode, thisNode);

  if (!isInsideOfFocusedNode && thisNode != gLastFocusedNode &&
      !nsCoreUtils::IsAncestorOf(thisNode, gLastFocusedNode))
    return NS_OK;

  // Turn the focus node and offset of the selection into caret hypretext
  // offset.
  nsCOMPtr<nsISelection> domSel;
  nsresult rv = GetSelections(nsISelectionController::SELECTION_NORMAL,
                              nsnull, getter_AddRefs(domSel));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMNode> focusDOMNode;
  rv = domSel->GetFocusNode(getter_AddRefs(focusDOMNode));
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 focusOffset;
  rv = domSel->GetFocusOffset(&focusOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  // No caret if this DOM node is inside of focused node but the selection's
  // focus point is not inside of this DOM node.
  nsCOMPtr<nsINode> focusNode(do_QueryInterface(focusDOMNode));
  if (isInsideOfFocusedNode) {
    nsINode *resultNode =
      nsCoreUtils::GetDOMNodeFromDOMPoint(focusNode, focusOffset);

    if (resultNode != thisNode &&
        !nsCoreUtils::IsAncestorOf(thisNode, resultNode))
      return NS_OK;
  }

  DOMPointToHypertextOffset(focusNode, focusOffset, aCaretOffset);
  return NS_OK;
}

PRInt32 nsHyperTextAccessible::GetCaretLineNumber()
{
  // Provide the line number for the caret, relative to the
  // currently focused node. Use a 1-based index
  nsCOMPtr<nsISelection> domSel;
  GetSelections(nsISelectionController::SELECTION_NORMAL, nsnull,
                getter_AddRefs(domSel));
  nsCOMPtr<nsISelectionPrivate> privateSelection(do_QueryInterface(domSel));
  NS_ENSURE_TRUE(privateSelection, -1);
  nsCOMPtr<nsFrameSelection> frameSelection;
  privateSelection->GetFrameSelection(getter_AddRefs(frameSelection));
  NS_ENSURE_TRUE(frameSelection, -1);

  nsCOMPtr<nsIDOMNode> caretNode;
  domSel->GetFocusNode(getter_AddRefs(caretNode));
  nsCOMPtr<nsIContent> caretContent = do_QueryInterface(caretNode);
  if (!caretContent || !nsCoreUtils::IsAncestorOf(GetNode(), caretContent))
    return -1;

  PRInt32 caretOffset, returnOffsetUnused;
  domSel->GetFocusOffset(&caretOffset);
  nsFrameSelection::HINT hint = frameSelection->GetHint();
  nsIFrame *caretFrame = frameSelection->GetFrameForNodeOffset(caretContent, caretOffset,
                                                               hint, &returnOffsetUnused);
  NS_ENSURE_TRUE(caretFrame, -1);

  PRInt32 lineNumber = 1;
  nsAutoLineIterator lineIterForCaret;
  nsIContent *hyperTextContent = IsContent() ? mContent.get() : nsnull;
  while (caretFrame) {
    if (hyperTextContent == caretFrame->GetContent()) {
      return lineNumber; // Must be in a single line hyper text, there is no line iterator
    }
    nsIFrame *parentFrame = caretFrame->GetParent();
    if (!parentFrame)
      break;

    // Add lines for the sibling frames before the caret
    nsIFrame *sibling = parentFrame->GetFirstChild(nsnull);
    while (sibling && sibling != caretFrame) {
      nsAutoLineIterator lineIterForSibling = sibling->GetLineIterator();
      if (lineIterForSibling) {
        // For the frames before that grab all the lines
        PRInt32 addLines = lineIterForSibling->GetNumLines();
        lineNumber += addLines;
      }
      sibling = sibling->GetNextSibling();
    }

    // Get the line number relative to the container with lines
    if (!lineIterForCaret) {   // Add the caret line just once
      lineIterForCaret = parentFrame->GetLineIterator();
      if (lineIterForCaret) {
        // Ancestor of caret
        PRInt32 addLines = lineIterForCaret->FindLineContaining(caretFrame);
        lineNumber += addLines;
      }
    }

    caretFrame = parentFrame;
  }

  NS_NOTREACHED("DOM ancestry had this hypertext but frame ancestry didn't");
  return lineNumber;
}

nsresult
nsHyperTextAccessible::GetSelections(PRInt16 aType,
                                     nsISelectionController **aSelCon,
                                     nsISelection **aDomSel,
                                     nsCOMArray<nsIDOMRange>* aRanges)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (aSelCon) {
    *aSelCon = nsnull;
  }
  if (aDomSel) {
    *aDomSel = nsnull;
  }
  if (aRanges) {
    aRanges->Clear();
  }
  
  nsCOMPtr<nsISelection> domSel;
  nsCOMPtr<nsISelectionController> selCon;

  nsCOMPtr<nsIEditor> editor;
  GetAssociatedEditor(getter_AddRefs(editor));
  nsCOMPtr<nsIPlaintextEditor> peditor(do_QueryInterface(editor));
  if (peditor) {
    // Case 1: plain text editor
    // This is for form controls which have their own
    // selection controller separate from the document, for example
    // HTML:input, HTML:textarea, XUL:textbox, etc.
    editor->GetSelectionController(getter_AddRefs(selCon));
  }
  else {
    // Case 2: rich content subtree (can be rich editor)
    // This uses the selection controller from the entire document
    nsIFrame *frame = GetFrame();
    NS_ENSURE_TRUE(frame, NS_ERROR_FAILURE);

    // Get the selection and selection controller
    frame->GetSelectionController(GetPresContext(),
                                  getter_AddRefs(selCon));
  }
  NS_ENSURE_TRUE(selCon, NS_ERROR_FAILURE);

  selCon->GetSelection(aType, getter_AddRefs(domSel));
  NS_ENSURE_TRUE(domSel, NS_ERROR_FAILURE);

  if (aSelCon) {
    NS_ADDREF(*aSelCon = selCon);
  }
  if (aDomSel) {
    NS_ADDREF(*aDomSel = domSel);
  }

  if (aRanges) {
    nsCOMPtr<nsISelection2> selection2(do_QueryInterface(domSel));
    NS_ENSURE_TRUE(selection2, NS_ERROR_FAILURE);

    nsCOMPtr<nsINode> startNode = GetNode();
    if (peditor) {
      nsCOMPtr<nsIDOMElement> editorRoot;
      editor->GetRootElement(getter_AddRefs(editorRoot));
      startNode = do_QueryInterface(editorRoot);
    }
    NS_ENSURE_STATE(startNode);

    PRUint32 childCount = startNode->GetChildCount();
    nsCOMPtr<nsIDOMNode> startDOMNode(do_QueryInterface(startNode));
    nsresult rv = selection2->
      GetRangesForIntervalCOMArray(startDOMNode, 0, startDOMNode, childCount,
                                   PR_TRUE, aRanges);
    NS_ENSURE_SUCCESS(rv, rv);
    // Remove collapsed ranges
    PRInt32 numRanges = aRanges->Count();
    for (PRInt32 count = 0; count < numRanges; count ++) {
      PRBool isCollapsed;
      (*aRanges)[count]->GetCollapsed(&isCollapsed);
      if (isCollapsed) {
        aRanges->RemoveObjectAt(count);
        -- numRanges;
        -- count;
      }
    }
  }

  return NS_OK;
}

/*
 * Gets the number of selected regions.
 */
NS_IMETHODIMP nsHyperTextAccessible::GetSelectionCount(PRInt32 *aSelectionCount)
{
  nsCOMPtr<nsISelection> domSel;
  nsCOMArray<nsIDOMRange> ranges;
  nsresult rv = GetSelections(nsISelectionController::SELECTION_NORMAL,
                              nsnull, nsnull, &ranges);
  NS_ENSURE_SUCCESS(rv, rv);

  *aSelectionCount = ranges.Count();

  return NS_OK;
}

/*
 * Gets the start and end offset of the specified selection.
 */
NS_IMETHODIMP nsHyperTextAccessible::GetSelectionBounds(PRInt32 aSelectionNum, PRInt32 *aStartOffset, PRInt32 *aEndOffset)
{
  *aStartOffset = *aEndOffset = 0;

  nsCOMPtr<nsISelection> domSel;
  nsCOMArray<nsIDOMRange> ranges;
  nsresult rv = GetSelections(nsISelectionController::SELECTION_NORMAL,
                              nsnull, getter_AddRefs(domSel), &ranges);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 rangeCount = ranges.Count();
  if (aSelectionNum < 0 || aSelectionNum >= rangeCount)
    return NS_ERROR_INVALID_ARG;

  nsCOMPtr<nsIDOMRange> range = ranges[aSelectionNum];

  // Get start point
  nsCOMPtr<nsIDOMNode> startDOMNode;
  range->GetStartContainer(getter_AddRefs(startDOMNode));
  nsCOMPtr<nsINode> startNode(do_QueryInterface(startDOMNode));
  PRInt32 startOffset;
  range->GetStartOffset(&startOffset);

  // Get end point
  nsCOMPtr<nsIDOMNode> endDOMNode;
  range->GetEndContainer(getter_AddRefs(endDOMNode));
  nsCOMPtr<nsINode> endNode(do_QueryInterface(endDOMNode));
  PRInt32 endOffset;
  range->GetEndOffset(&endOffset);

  PRInt16 rangeCompareResult;
  rv = range->CompareBoundaryPoints(nsIDOMRange::START_TO_END, range, &rangeCompareResult);
  NS_ENSURE_SUCCESS(rv, rv);

  if (rangeCompareResult < 0) {
    // Make sure start is before end, by swapping offsets
    // This occurs when the user selects backwards in the text
    startNode.swap(endNode);
    PRInt32 tempOffset = startOffset;
    startOffset = endOffset;
    endOffset = tempOffset;
  }

  nsAccessible *startAccessible =
    DOMPointToHypertextOffset(startNode, startOffset, aStartOffset);
  if (!startAccessible) {
    *aStartOffset = 0; // Could not find start point within this hypertext, so starts before
  }

  DOMPointToHypertextOffset(endNode, endOffset, aEndOffset, PR_TRUE);
  return NS_OK;
}

/*
 * Changes the start and end offset of the specified selection.
 */
NS_IMETHODIMP
nsHyperTextAccessible::SetSelectionBounds(PRInt32 aSelectionNum,
                                          PRInt32 aStartOffset,
                                          PRInt32 aEndOffset)
{
  nsCOMPtr<nsISelection> domSel;
  nsresult rv = GetSelections(nsISelectionController::SELECTION_NORMAL,
                              nsnull, getter_AddRefs(domSel));
  NS_ENSURE_SUCCESS(rv, rv);

  // Caret is a collapsed selection
  PRBool isOnlyCaret = (aStartOffset == aEndOffset);

  PRInt32 rangeCount;
  domSel->GetRangeCount(&rangeCount);
  nsCOMPtr<nsIDOMRange> range;
  if (aSelectionNum == rangeCount) { // Add a range
    range = do_CreateInstance(kRangeCID);
    NS_ENSURE_TRUE(range, NS_ERROR_OUT_OF_MEMORY);
  }
  else if (aSelectionNum < 0 || aSelectionNum > rangeCount) {
    return NS_ERROR_INVALID_ARG;
  }
  else {
    domSel->GetRangeAt(aSelectionNum, getter_AddRefs(range));
    NS_ENSURE_TRUE(range, NS_ERROR_FAILURE);
  }

  PRInt32 startOffset, endOffset;
  nsCOMPtr<nsIDOMNode> startNode, endNode;

  rv = HypertextOffsetsToDOMRange(aStartOffset, aEndOffset,
                                  getter_AddRefs(startNode), &startOffset,
                                  getter_AddRefs(endNode), &endOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = range->SetStart(startNode, startOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = isOnlyCaret ? range->Collapse(PR_TRUE) :
                     range->SetEnd(endNode, endOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aSelectionNum == rangeCount) { // Add successfully created new range
    return domSel->AddRange(range);
  }
  return NS_OK;
}

/*
 * Adds a selection bounded by the specified offsets.
 */
NS_IMETHODIMP nsHyperTextAccessible::AddSelection(PRInt32 aStartOffset, PRInt32 aEndOffset)
{
  nsCOMPtr<nsISelection> domSel;
  nsresult rv = GetSelections(nsISelectionController::SELECTION_NORMAL,
                              nsnull, getter_AddRefs(domSel));
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 rangeCount;
  domSel->GetRangeCount(&rangeCount);

  return SetSelectionBounds(rangeCount, aStartOffset, aEndOffset);
}

/*
 * Removes the specified selection.
 */
NS_IMETHODIMP nsHyperTextAccessible::RemoveSelection(PRInt32 aSelectionNum)
{
  nsCOMPtr<nsISelection> domSel;
  nsresult rv = GetSelections(nsISelectionController::SELECTION_NORMAL,
                              nsnull, getter_AddRefs(domSel));
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 rangeCount;
  domSel->GetRangeCount(&rangeCount);
  if (aSelectionNum < 0 || aSelectionNum >= rangeCount)
    return NS_ERROR_INVALID_ARG;

  nsCOMPtr<nsIDOMRange> range;
  domSel->GetRangeAt(aSelectionNum, getter_AddRefs(range));
  return domSel->RemoveRange(range);
}

// void nsIAccessibleText::
//   scrollSubstringTo(in long startIndex, in long endIndex,
//                     in unsigned long scrollType);
NS_IMETHODIMP
nsHyperTextAccessible::ScrollSubstringTo(PRInt32 aStartIndex, PRInt32 aEndIndex,
                                         PRUint32 aScrollType)
{
  PRInt32 startOffset, endOffset;
  nsCOMPtr<nsIDOMNode> startNode, endNode;

  nsresult rv = HypertextOffsetsToDOMRange(aStartIndex, aEndIndex,
                                           getter_AddRefs(startNode),
                                           &startOffset,
                                           getter_AddRefs(endNode),
                                           &endOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  return nsCoreUtils::ScrollSubstringTo(GetFrame(), startNode, startOffset,
                                        endNode, endOffset, aScrollType);
}

// void nsIAccessibleText::
//   scrollSubstringToPoint(in long startIndex, in long endIndex,
//                          in unsigned long coordinateType,
//                          in long x, in long y);
NS_IMETHODIMP
nsHyperTextAccessible::ScrollSubstringToPoint(PRInt32 aStartIndex,
                                              PRInt32 aEndIndex,
                                              PRUint32 aCoordinateType,
                                              PRInt32 aX, PRInt32 aY)
{
  nsIFrame *frame = GetFrame();
  if (!frame)
    return NS_ERROR_FAILURE;

  nsIntPoint coords;
  nsresult rv = nsAccUtils::ConvertToScreenCoords(aX, aY, aCoordinateType,
                                                  this, &coords);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 startOffset, endOffset;
  nsCOMPtr<nsIDOMNode> startNode, endNode;

  rv = HypertextOffsetsToDOMRange(aStartIndex, aEndIndex,
                                  getter_AddRefs(startNode), &startOffset,
                                  getter_AddRefs(endNode), &endOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  nsPresContext *presContext = frame->PresContext();

  PRBool initialScrolled = PR_FALSE;
  nsIFrame *parentFrame = frame;
  while ((parentFrame = parentFrame->GetParent())) {
    nsIScrollableFrame *scrollableFrame = do_QueryFrame(parentFrame);
    if (scrollableFrame) {
      if (!initialScrolled) {
        // Scroll substring to the given point. Turn the point into percents
        // relative scrollable area to use nsCoreUtils::ScrollSubstringTo.
        nsIntRect frameRect = parentFrame->GetScreenRectExternal();
        PRInt32 devOffsetX = coords.x - frameRect.x;
        PRInt32 devOffsetY = coords.y - frameRect.y;

        nsPoint offsetPoint(presContext->DevPixelsToAppUnits(devOffsetX),
                            presContext->DevPixelsToAppUnits(devOffsetY));

        nsSize size(parentFrame->GetSize());

        // avoid divide by zero
        size.width = size.width ? size.width : 1;
        size.height = size.height ? size.height : 1;

        PRInt16 hPercent = offsetPoint.x * 100 / size.width;
        PRInt16 vPercent = offsetPoint.y * 100 / size.height;

        rv = nsCoreUtils::ScrollSubstringTo(GetFrame(), startNode, startOffset,
                                            endNode, endOffset,
                                            vPercent, hPercent);
        NS_ENSURE_SUCCESS(rv, rv);

        initialScrolled = PR_TRUE;
      } else {
        // Substring was scrolled to the given point already inside its closest
        // scrollable area. If there are nested scrollable areas then make
        // sure we scroll lower areas to the given point inside currently
        // traversed scrollable area.
        nsCoreUtils::ScrollFrameToPoint(parentFrame, frame, coords);
      }
    }
    frame = parentFrame;
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsAccessible public

void
nsHyperTextAccessible::InvalidateChildren()
{
  mOffsets.Clear();

  nsAccessibleWrap::InvalidateChildren();
}

////////////////////////////////////////////////////////////////////////////////
// nsHyperTextAccessible public static

nsresult nsHyperTextAccessible::ContentToRenderedOffset(nsIFrame *aFrame, PRInt32 aContentOffset,
                                                        PRUint32 *aRenderedOffset)
{
  if (!aFrame) {
    // Current frame not rendered -- this can happen if text is set on
    // something with display: none
    *aRenderedOffset = 0;
    return NS_OK;
  }
  NS_ASSERTION(aFrame->GetType() == nsAccessibilityAtoms::textFrame,
               "Need text frame for offset conversion");
  NS_ASSERTION(aFrame->GetPrevContinuation() == nsnull,
               "Call on primary frame only");

  gfxSkipChars skipChars;
  gfxSkipCharsIterator iter;
  // Only get info up to original offset, we know that will be larger than skipped offset
  nsresult rv = aFrame->GetRenderedText(nsnull, &skipChars, &iter, 0, aContentOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 ourRenderedStart = iter.GetSkippedOffset();
  PRInt32 ourContentStart = iter.GetOriginalOffset();

  *aRenderedOffset = iter.ConvertOriginalToSkipped(aContentOffset + ourContentStart) -
                    ourRenderedStart;

  return NS_OK;
}

nsresult nsHyperTextAccessible::RenderedToContentOffset(nsIFrame *aFrame, PRUint32 aRenderedOffset,
                                                        PRInt32 *aContentOffset)
{
  *aContentOffset = 0;
  NS_ENSURE_TRUE(aFrame, NS_ERROR_FAILURE);

  NS_ASSERTION(aFrame->GetType() == nsAccessibilityAtoms::textFrame,
               "Need text frame for offset conversion");
  NS_ASSERTION(aFrame->GetPrevContinuation() == nsnull,
               "Call on primary frame only");

  gfxSkipChars skipChars;
  gfxSkipCharsIterator iter;
  // We only need info up to skipped offset -- that is what we're converting to original offset
  nsresult rv = aFrame->GetRenderedText(nsnull, &skipChars, &iter, 0, aRenderedOffset);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 ourRenderedStart = iter.GetSkippedOffset();
  PRInt32 ourContentStart = iter.GetOriginalOffset();

  *aContentOffset = iter.ConvertSkippedToOriginal(aRenderedOffset + ourRenderedStart) - ourContentStart;

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsHyperTextAccessible public

PRInt32
nsHyperTextAccessible::GetChildOffset(PRUint32 aChildIndex,
                                      PRBool aInvalidateAfter)
{
  if (aChildIndex == 0)
    return aChildIndex;

  PRInt32 count = mOffsets.Length() - aChildIndex;
  if (count > 0) {
    if (aInvalidateAfter)
      mOffsets.RemoveElementsAt(aChildIndex, count);

    return mOffsets[aChildIndex - 1];
  }

  PRUint32 lastOffset = mOffsets.IsEmpty() ?
    0 : mOffsets[mOffsets.Length() - 1];

  EnsureChildren();
  while (mOffsets.Length() < aChildIndex) {
    nsAccessible* child = mChildren[mOffsets.Length()];
    lastOffset += nsAccUtils::TextLength(child);
    mOffsets.AppendElement(lastOffset);
  }

  return mOffsets[aChildIndex - 1];
}

PRInt32
nsHyperTextAccessible::GetChildIndexAtOffset(PRUint32 aOffset)
{
  PRUint32 lastOffset = 0;
  PRUint32 offsetCount = mOffsets.Length();
  if (offsetCount > 0) {
    lastOffset = mOffsets[offsetCount - 1];
    if (aOffset < lastOffset) {
      PRUint32 low = 0, high = offsetCount;
      while (high > low) {
        PRUint32 mid = (high + low) >> 1;
        if (mOffsets[mid] == aOffset)
          return mid < offsetCount - 1 ? mid + 1 : mid;

        if (mOffsets[mid] < aOffset)
          low = mid + 1;
        else
          high = mid;
      }
      if (high == offsetCount)
        return -1;

      return low;
    }
  }

  PRUint32 childCount = GetChildCount();
  while (mOffsets.Length() < childCount) {
    nsAccessible* child = GetChildAt(mOffsets.Length());
    lastOffset += nsAccUtils::TextLength(child);
    mOffsets.AppendElement(lastOffset);
    if (aOffset < lastOffset)
      return mOffsets.Length() - 1;
  }

  if (aOffset == lastOffset)
    return mOffsets.Length() - 1;

  return -1;
}

////////////////////////////////////////////////////////////////////////////////
// nsHyperTextAccessible protected

nsresult
nsHyperTextAccessible::GetDOMPointByFrameOffset(nsIFrame *aFrame,
                                                PRInt32 aOffset,
                                                nsIAccessible *aAccessible,
                                                nsIDOMNode **aNode,
                                                PRInt32 *aNodeOffset)
{
  NS_ENSURE_ARG(aAccessible);

  nsCOMPtr<nsIDOMNode> node;

  if (!aFrame) {
    // If the given frame is null then set offset after the DOM node of the
    // given accessible.
    nsCOMPtr<nsIAccessNode> accessNode(do_QueryInterface(aAccessible));

    nsCOMPtr<nsIDOMNode> DOMNode;
    accessNode->GetDOMNode(getter_AddRefs(DOMNode));
    nsCOMPtr<nsIContent> content(do_QueryInterface(DOMNode));
    NS_ENSURE_STATE(content);

    nsCOMPtr<nsIContent> parent(content->GetParent());
    NS_ENSURE_STATE(parent);

    *aNodeOffset = parent->IndexOf(content) + 1;
    node = do_QueryInterface(parent);

  } else if (aFrame->GetType() == nsAccessibilityAtoms::textFrame) {
    nsCOMPtr<nsIContent> content(aFrame->GetContent());
    NS_ENSURE_STATE(content);

    nsIFrame *primaryFrame = content->GetPrimaryFrame();
    nsresult rv = RenderedToContentOffset(primaryFrame, aOffset, aNodeOffset);
    NS_ENSURE_SUCCESS(rv, rv);

    node = do_QueryInterface(content);

  } else {
    nsCOMPtr<nsIContent> content(aFrame->GetContent());
    NS_ENSURE_STATE(content);

    nsCOMPtr<nsIContent> parent(content->GetParent());
    NS_ENSURE_STATE(parent);

    *aNodeOffset = parent->IndexOf(content);
    node = do_QueryInterface(parent);
  }

  NS_IF_ADDREF(*aNode = node);
  return NS_OK;
}

// nsHyperTextAccessible
nsresult
nsHyperTextAccessible::DOMRangeBoundToHypertextOffset(nsIDOMRange *aRange,
                                                      PRBool aIsStartBound,
                                                      PRBool aIsStartHTOffset,
                                                      PRInt32 *aHTOffset)
{
  nsCOMPtr<nsIDOMNode> DOMNode;
  PRInt32 nodeOffset = 0;

  nsresult rv;
  if (aIsStartBound) {
    rv = aRange->GetStartContainer(getter_AddRefs(DOMNode));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = aRange->GetStartOffset(&nodeOffset);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    rv = aRange->GetEndContainer(getter_AddRefs(DOMNode));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = aRange->GetEndOffset(&nodeOffset);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsINode> node(do_QueryInterface(DOMNode));
  nsAccessible *startAcc =
    DOMPointToHypertextOffset(node, nodeOffset, aHTOffset);

  if (aIsStartHTOffset && !startAcc)
    *aHTOffset = 0;

  return NS_OK;
}

// nsHyperTextAccessible
nsresult
nsHyperTextAccessible::GetSpellTextAttribute(nsIDOMNode *aNode,
                                             PRInt32 aNodeOffset,
                                             PRInt32 *aHTStartOffset,
                                             PRInt32 *aHTEndOffset,
                                             nsIPersistentProperties *aAttributes)
{
  nsCOMArray<nsIDOMRange> ranges;
  nsresult rv = GetSelections(nsISelectionController::SELECTION_SPELLCHECK,
                              nsnull, nsnull, &ranges);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 rangeCount = ranges.Count();
  if (!rangeCount)
    return NS_OK;

  for (PRInt32 index = 0; index < rangeCount; index++) {
    nsCOMPtr<nsIDOMRange> range = ranges[index];
    nsCOMPtr<nsIDOMNSRange> nsrange(do_QueryInterface(range));
    NS_ENSURE_STATE(nsrange);

    PRInt16 result;
    rv = nsrange->ComparePoint(aNode, aNodeOffset, &result);
    NS_ENSURE_SUCCESS(rv, rv);
    // ComparePoint checks boundary points, but we need to check that
    // text at aNodeOffset is inside the range.
    // See also bug 460690.
    if (result == 0) {
      nsCOMPtr<nsIDOMNode> end;
      rv = range->GetEndContainer(getter_AddRefs(end));
      NS_ENSURE_SUCCESS(rv, rv);
      PRInt32 endOffset;
      rv = range->GetEndOffset(&endOffset);
      NS_ENSURE_SUCCESS(rv, rv);
      if (aNode == end && aNodeOffset == endOffset) {
        result = 1;
      }
    }

    if (result == 1) { // range is before point
      PRInt32 startHTOffset = 0;
      rv = DOMRangeBoundToHypertextOffset(range, PR_FALSE, PR_TRUE,
                                          &startHTOffset);
      NS_ENSURE_SUCCESS(rv, rv);

      if (startHTOffset > *aHTStartOffset)
        *aHTStartOffset = startHTOffset;

    } else if (result == -1) { // range is after point
      PRInt32 endHTOffset = 0;
      rv = DOMRangeBoundToHypertextOffset(range, PR_TRUE, PR_FALSE,
                                          &endHTOffset);
      NS_ENSURE_SUCCESS(rv, rv);

      if (endHTOffset < *aHTEndOffset)
        *aHTEndOffset = endHTOffset;

    } else { // point is in range
      PRInt32 startHTOffset = 0;
      rv = DOMRangeBoundToHypertextOffset(range, PR_TRUE, PR_TRUE,
                                          &startHTOffset);
      NS_ENSURE_SUCCESS(rv, rv);

      PRInt32 endHTOffset = 0;
      rv = DOMRangeBoundToHypertextOffset(range, PR_FALSE, PR_FALSE,
                                          &endHTOffset);
      NS_ENSURE_SUCCESS(rv, rv);

      if (startHTOffset > *aHTStartOffset)
        *aHTStartOffset = startHTOffset;
      if (endHTOffset < *aHTEndOffset)
        *aHTEndOffset = endHTOffset;

      if (aAttributes) {
        nsAccUtils::SetAccAttr(aAttributes, nsAccessibilityAtoms::invalid,
                               NS_LITERAL_STRING("spelling"));
      }

      return NS_OK;
    }
  }

  return NS_OK;
}
