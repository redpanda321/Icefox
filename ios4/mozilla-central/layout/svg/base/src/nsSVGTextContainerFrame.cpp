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
 * The Original Code is the Mozilla SVG project.
 *
 * The Initial Developer of the Original Code is IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2006
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

#include "nsSVGContainerFrame.h"
#include "nsSVGTextFrame.h"
#include "nsSVGUtils.h"
#include "nsSVGOuterSVGFrame.h"
#include "nsIDOMSVGTextElement.h"
#include "nsIDOMSVGAnimatedLengthList.h"
#include "nsIDOMSVGAnimatedNumberList.h"
#include "nsISVGGlyphFragmentLeaf.h"
#include "nsDOMError.h"
#include "SVGLengthList.h"

using namespace mozilla;

//----------------------------------------------------------------------
// nsQueryFrame methods

NS_QUERYFRAME_HEAD(nsSVGTextContainerFrame)
  NS_QUERYFRAME_ENTRY(nsSVGTextContainerFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsSVGDisplayContainerFrame)

NS_IMPL_FRAMEARENA_HELPERS(nsSVGTextContainerFrame)

void
nsSVGTextContainerFrame::NotifyGlyphMetricsChange()
{
  nsSVGTextFrame *textFrame = GetTextFrame();
  if (textFrame)
    textFrame->NotifyGlyphMetricsChange();
}

void
nsSVGTextContainerFrame::GetXY(SVGUserUnitList *aX, SVGUserUnitList *aY)
{
  static_cast<nsSVGElement*>(mContent)->
    GetAnimatedLengthListValues(aX, aY, nsnull);
}

void
nsSVGTextContainerFrame::GetDxDy(SVGUserUnitList *aDx, SVGUserUnitList *aDy)
{
  // SVGUserUnitList is lazy, so there's little overhead it getting the x
  // and y lists even though we ignore them.
  SVGUserUnitList xLengthList, yLengthList;
  static_cast<nsSVGElement*>(mContent)->
    GetAnimatedLengthListValues(&xLengthList, &yLengthList, aDx, aDy, nsnull);
}

already_AddRefed<nsIDOMSVGNumberList>
nsSVGTextContainerFrame::GetRotate()
{
  nsCOMPtr<nsIDOMSVGTextPositioningElement> tpElement =
    do_QueryInterface(mContent);

  if (!tpElement)
    return nsnull;

  nsCOMPtr<nsIDOMSVGAnimatedNumberList> animNumberList;
  tpElement->GetRotate(getter_AddRefs(animNumberList));
  nsIDOMSVGNumberList *retval;
  animNumberList->GetAnimVal(&retval);
  return retval;
}

//----------------------------------------------------------------------
// nsIFrame methods

NS_IMETHODIMP
nsSVGTextContainerFrame::InsertFrames(nsIAtom* aListName,
                                      nsIFrame* aPrevFrame,
                                      nsFrameList& aFrameList)
{
  nsresult rv = nsSVGDisplayContainerFrame::InsertFrames(aListName,
                                                         aPrevFrame,
                                                         aFrameList);

  NotifyGlyphMetricsChange();
  return rv;
}

NS_IMETHODIMP
nsSVGTextContainerFrame::RemoveFrame(nsIAtom *aListName, nsIFrame *aOldFrame)
{
  nsSVGTextFrame *textFrame = GetTextFrame();

  nsresult rv = nsSVGDisplayContainerFrame::RemoveFrame(aListName, aOldFrame);

  if (textFrame)
    textFrame->NotifyGlyphMetricsChange();

  return rv;
}

NS_IMETHODIMP
nsSVGTextContainerFrame::GetStartPositionOfChar(PRUint32 charnum, nsIDOMSVGPoint **_retval)
{
  *_retval = nsnull;

  if (charnum >= GetNumberOfChars()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  nsISVGGlyphFragmentNode *node = GetFirstGlyphFragmentChildNode();
  if (!node) {
    return NS_ERROR_FAILURE;
  }

  PRUint32 offset;
  nsISVGGlyphFragmentLeaf *fragment = GetGlyphFragmentAtCharNum(node, charnum, &offset);
  if (!fragment) {
    return NS_ERROR_FAILURE;
  }

  return fragment->GetStartPositionOfChar(charnum - offset, _retval);
}

NS_IMETHODIMP
nsSVGTextContainerFrame::GetEndPositionOfChar(PRUint32 charnum, nsIDOMSVGPoint **_retval)
{
  *_retval = nsnull;

  if (charnum >= GetNumberOfChars()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  nsISVGGlyphFragmentNode *node = GetFirstGlyphFragmentChildNode();
  if (!node) {
    return NS_ERROR_FAILURE;
  }

  PRUint32 offset;
  nsISVGGlyphFragmentLeaf *fragment = GetGlyphFragmentAtCharNum(node, charnum, &offset);
  if (!fragment) {
    return NS_ERROR_FAILURE;
  }

  return fragment->GetEndPositionOfChar(charnum - offset, _retval);
}

NS_IMETHODIMP
nsSVGTextContainerFrame::GetExtentOfChar(PRUint32 charnum, nsIDOMSVGRect **_retval)
{
  *_retval = nsnull;

  if (charnum >= GetNumberOfChars()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  nsISVGGlyphFragmentNode *node = GetFirstGlyphFragmentChildNode();
  if (!node) {
    return NS_ERROR_FAILURE;
  }

  PRUint32 offset;
  nsISVGGlyphFragmentLeaf *fragment = GetGlyphFragmentAtCharNum(node, charnum, &offset);
  if (!fragment) {
    return NS_ERROR_FAILURE;
  }

  return fragment->GetExtentOfChar(charnum - offset, _retval);
}

NS_IMETHODIMP
nsSVGTextContainerFrame::GetRotationOfChar(PRUint32 charnum, float *_retval)
{
  *_retval = 0.0f;

  if (charnum >= GetNumberOfChars()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  nsISVGGlyphFragmentNode *node = GetFirstGlyphFragmentChildNode();
  if (!node) {
    return NS_ERROR_FAILURE;
  }

  PRUint32 offset;
  nsISVGGlyphFragmentLeaf *fragment = GetGlyphFragmentAtCharNum(node, charnum, &offset);
  if (!fragment) {
    return NS_ERROR_FAILURE;
  }

  return fragment->GetRotationOfChar(charnum - offset, _retval);
}

PRUint32
nsSVGTextContainerFrame::GetNumberOfChars()
{
  PRUint32 nchars = 0;
  nsISVGGlyphFragmentNode* node;
  node = GetFirstGlyphFragmentChildNode();

  while (node) {
    nchars += node->GetNumberOfChars();
    node = GetNextGlyphFragmentChildNode(node);
  }

  return nchars;
}

float
nsSVGTextContainerFrame::GetComputedTextLength()
{
  float length = 0.0f;
  nsISVGGlyphFragmentNode* node = GetFirstGlyphFragmentChildNode();

  while (node) {
    length += node->GetComputedTextLength();
    node = GetNextGlyphFragmentChildNode(node);
  }

  return length;
}

float
nsSVGTextContainerFrame::GetSubStringLength(PRUint32 charnum, PRUint32 nchars)
{
  float length = 0.0f;
  nsISVGGlyphFragmentNode *node = GetFirstGlyphFragmentChildNode();

  while (node) {
    PRUint32 count = node->GetNumberOfChars();
    if (count > charnum) {
      PRUint32 fragmentChars = NS_MIN(nchars, count);
      float fragmentLength = node->GetSubStringLength(charnum, fragmentChars);
      length += fragmentLength;
      nchars -= fragmentChars;
      if (nchars == 0) break;
    }
    charnum -= NS_MIN(charnum, count);
    node = GetNextGlyphFragmentChildNode(node);
  }

  return length;
}

PRInt32
nsSVGTextContainerFrame::GetCharNumAtPosition(nsIDOMSVGPoint *point)
{
  PRInt32 index = -1;
  PRInt32 offset = 0;
  nsISVGGlyphFragmentNode *node = GetFirstGlyphFragmentChildNode();

  while (node) {
    PRUint32 count = node->GetNumberOfChars();
    if (count > 0) {
      PRInt32 charnum = node->GetCharNumAtPosition(point);
      if (charnum >= 0) {
        index = charnum + offset;
      }
      offset += count;
      // Keep going, multiple characters may match 
      // and we must return the last one
    }
    node = GetNextGlyphFragmentChildNode(node);
  }

  return index;
}

// -------------------------------------------------------------------------
// Protected functions
// -------------------------------------------------------------------------

nsISVGGlyphFragmentNode *
nsSVGTextContainerFrame::GetFirstGlyphFragmentChildNode()
{
  nsISVGGlyphFragmentNode *retval = nsnull;
  nsIFrame* kid = mFrames.FirstChild();
  while (kid) {
    retval = do_QueryFrame(kid);
    if (retval) break;
    kid = kid->GetNextSibling();
  }
  return retval;
}

nsISVGGlyphFragmentNode *
nsSVGTextContainerFrame::GetNextGlyphFragmentChildNode(nsISVGGlyphFragmentNode *node)
{
  nsISVGGlyphFragmentNode *retval = nsnull;
  nsIFrame *frame = do_QueryFrame(node);
  NS_ASSERTION(frame, "interface not implemented");
  frame = frame->GetNextSibling();
  while (frame) {
    retval = do_QueryFrame(frame);
    if (retval) break;
    frame = frame->GetNextSibling();
  }
  return retval;
}

void
nsSVGTextContainerFrame::SetWhitespaceHandling()
{
  // init children:
  nsISVGGlyphFragmentNode* node = GetFirstGlyphFragmentChildNode();
  nsISVGGlyphFragmentNode* next;

  PRUint8 whitespaceHandling = COMPRESS_WHITESPACE | TRIM_LEADING_WHITESPACE;

  for (nsIFrame *frame = this; frame != nsnull; frame = frame->GetParent()) {
    nsIContent *content = frame->GetContent();
    static nsIContent::AttrValuesArray strings[] =
      {&nsGkAtoms::preserve, &nsGkAtoms::_default, nsnull};

    PRInt32 index = content->FindAttrValueIn(kNameSpaceID_XML,
                                             nsGkAtoms::space,
                                             strings, eCaseMatters);
    if (index == 0) {
      whitespaceHandling = PRESERVE_WHITESPACE;
      break;
    }
    if (index != nsIContent::ATTR_MISSING ||
        (frame->GetStateBits() & NS_STATE_IS_OUTER_SVG))
      break;
  }

  while (node) {
    next = GetNextGlyphFragmentChildNode(node);
    if (!next && (whitespaceHandling & COMPRESS_WHITESPACE)) {
      whitespaceHandling |= TRIM_TRAILING_WHITESPACE;
    }
    node->SetWhitespaceHandling(whitespaceHandling);
    node = next;
    whitespaceHandling &= ~TRIM_LEADING_WHITESPACE;
  }
}

// -------------------------------------------------------------------------
// Private functions
// -------------------------------------------------------------------------

nsISVGGlyphFragmentLeaf *
nsSVGTextContainerFrame::GetGlyphFragmentAtCharNum(nsISVGGlyphFragmentNode* node,
                                                   PRUint32 charnum,
                                                   PRUint32 *offset)
{
  nsISVGGlyphFragmentLeaf *fragment = node->GetFirstGlyphFragment();
  *offset = 0;
  
  while (fragment) {
    PRUint32 count = fragment->GetNumberOfChars();
    if (count > charnum)
      return fragment;
    charnum -= count;
    *offset += count;
    fragment = fragment->GetNextGlyphFragment();
  }

  // not found
  return nsnull;
}

nsSVGTextFrame *
nsSVGTextContainerFrame::GetTextFrame()
{
  for (nsIFrame *frame = this; frame != nsnull; frame = frame->GetParent()) {
    if (frame->GetType() == nsGkAtoms::svgTextFrame) {
      return static_cast<nsSVGTextFrame*>(frame);
    }
  }
  return nsnull;
}
