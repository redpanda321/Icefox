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
 * Portions created by the Initial Developer are Copyright (C) 2004
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

#include "nsSVGGFrame.h"
#include "nsIAnonymousContentCreator.h"
#include "nsIDOMSVGUseElement.h"
#include "nsIDOMSVGTransformable.h"
#include "nsSVGElement.h"
#include "nsSVGUseElement.h"
#include "gfxMatrix.h"

typedef nsSVGGFrame nsSVGUseFrameBase;

class nsSVGUseFrame : public nsSVGUseFrameBase,
                      public nsIAnonymousContentCreator
{
  friend nsIFrame*
  NS_NewSVGUseFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

protected:
  nsSVGUseFrame(nsStyleContext* aContext) : nsSVGUseFrameBase(aContext) {}

public:
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS

  
  // nsIFrame interface:
#ifdef DEBUG
  NS_IMETHOD Init(nsIContent*      aContent,
                  nsIFrame*        aParent,
                  nsIFrame*        aPrevInFlow);
#endif

  NS_IMETHOD  AttributeChanged(PRInt32         aNameSpaceID,
                               nsIAtom*        aAttribute,
                               PRInt32         aModType);

  virtual void DestroyFrom(nsIFrame* aDestructRoot);

  /**
   * Get the "type" of the frame
   *
   * @see nsGkAtoms::svgUseFrame
   */
  virtual nsIAtom* GetType() const;

  virtual PRBool IsLeaf() const;

#ifdef DEBUG
  NS_IMETHOD GetFrameName(nsAString& aResult) const
  {
    return MakeFrameName(NS_LITERAL_STRING("SVGUse"), aResult);
  }
#endif

  // nsIAnonymousContentCreator
  virtual nsresult CreateAnonymousContent(nsTArray<nsIContent*>& aElements);
  virtual void AppendAnonymousContentTo(nsBaseContentList& aElements);
};

//----------------------------------------------------------------------
// Implementation

nsIFrame*
NS_NewSVGUseFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsSVGUseFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGUseFrame)

nsIAtom *
nsSVGUseFrame::GetType() const
{
  return nsGkAtoms::svgUseFrame;
}

//----------------------------------------------------------------------
// nsQueryFrame methods

NS_QUERYFRAME_HEAD(nsSVGUseFrame)
  NS_QUERYFRAME_ENTRY(nsIAnonymousContentCreator)
NS_QUERYFRAME_TAIL_INHERITING(nsSVGUseFrameBase)

//----------------------------------------------------------------------
// nsIFrame methods:

#ifdef DEBUG
NS_IMETHODIMP
nsSVGUseFrame::Init(nsIContent* aContent,
                    nsIFrame* aParent,
                    nsIFrame* aPrevInFlow)
{
  nsCOMPtr<nsIDOMSVGUseElement> use = do_QueryInterface(aContent);
  NS_ASSERTION(use, "Content is not an SVG use!");

  return nsSVGUseFrameBase::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

NS_IMETHODIMP
nsSVGUseFrame::AttributeChanged(PRInt32         aNameSpaceID,
                                nsIAtom*        aAttribute,
                                PRInt32         aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::x ||
       aAttribute == nsGkAtoms::y)) {
    // make sure our cached transform matrix gets (lazily) updated
    mCanvasTM = nsnull;
    
    nsSVGUtils::NotifyChildrenOfSVGChange(this, TRANSFORM_CHANGED);
    return NS_OK;
  }

  return nsSVGUseFrameBase::AttributeChanged(aNameSpaceID,
                                             aAttribute, aModType);
}

void
nsSVGUseFrame::DestroyFrom(nsIFrame* aDestructRoot)
{
  nsRefPtr<nsSVGUseElement> use = static_cast<nsSVGUseElement*>(mContent);
  nsSVGUseFrameBase::DestroyFrom(aDestructRoot);
  use->DestroyAnonymousContent();
}

PRBool
nsSVGUseFrame::IsLeaf() const
{
  return PR_TRUE;
}


//----------------------------------------------------------------------
// nsIAnonymousContentCreator methods:

nsresult
nsSVGUseFrame::CreateAnonymousContent(nsTArray<nsIContent*>& aElements)
{
  nsSVGUseElement *use = static_cast<nsSVGUseElement*>(mContent);

  nsIContent* clone = use->CreateAnonymousContent();
  if (!clone)
    return NS_ERROR_FAILURE;
  if (!aElements.AppendElement(clone))
    return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}

void
nsSVGUseFrame::AppendAnonymousContentTo(nsBaseContentList& aElements)
{
  nsSVGUseElement *use = static_cast<nsSVGUseElement*>(mContent);
  nsIContent* clone = use->GetAnonymousContent();
  aElements.MaybeAppendElement(clone);
}
