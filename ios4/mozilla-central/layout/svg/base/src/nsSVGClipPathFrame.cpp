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

#include "nsIDOMDocument.h"
#include "nsIDocument.h"
#include "nsIDOMSVGClipPathElement.h"
#include "nsSVGClipPathFrame.h"
#include "nsGkAtoms.h"
#include "nsSVGUtils.h"
#include "nsSVGEffects.h"
#include "nsSVGClipPathElement.h"
#include "gfxContext.h"
#include "nsSVGMatrix.h"

//----------------------------------------------------------------------
// Implementation

nsIFrame*
NS_NewSVGClipPathFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsSVGClipPathFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGClipPathFrame)

nsresult
nsSVGClipPathFrame::ClipPaint(nsSVGRenderState* aContext,
                              nsIFrame* aParent,
                              const gfxMatrix &aMatrix)
{
  // If the flag is set when we get here, it means this clipPath frame
  // has already been used painting the current clip, and the document
  // has a clip reference loop.
  if (mInUse) {
    NS_WARNING("Clip loop detected!");
    return NS_OK;
  }
  AutoClipPathReferencer clipRef(this);

  mClipParent = aParent;
  mClipParentMatrix = NS_NewSVGMatrix(aMatrix);

  PRBool isTrivial = IsTrivial();

  nsAutoSVGRenderMode mode(aContext,
                           isTrivial ? nsSVGRenderState::CLIP
                                     : nsSVGRenderState::CLIP_MASK);


  gfxContext *gfx = aContext->GetGfxContext();

  nsSVGClipPathFrame *clipPathFrame =
    nsSVGEffects::GetEffectProperties(this).GetClipPathFrame(nsnull);
  PRBool referencedClipIsTrivial;
  if (clipPathFrame) {
    referencedClipIsTrivial = clipPathFrame->IsTrivial();
    gfx->Save();
    if (referencedClipIsTrivial) {
      clipPathFrame->ClipPaint(aContext, aParent, aMatrix);
    } else {
      gfx->PushGroup(gfxASurface::CONTENT_ALPHA);
    }
  }

  for (nsIFrame* kid = mFrames.FirstChild(); kid;
       kid = kid->GetNextSibling()) {
    nsISVGChildFrame* SVGFrame = do_QueryFrame(kid);
    if (SVGFrame) {
      // The CTM of each frame referencing us can be different.
      SVGFrame->NotifySVGChanged(nsISVGChildFrame::SUPPRESS_INVALIDATION | 
                                 nsISVGChildFrame::TRANSFORM_CHANGED);

      PRBool isOK = PR_TRUE;
      nsSVGClipPathFrame *clipPathFrame =
        nsSVGEffects::GetEffectProperties(kid).GetClipPathFrame(&isOK);
      if (!isOK) {
        continue;
      }

      PRBool isTrivial;

      if (clipPathFrame) {
        isTrivial = clipPathFrame->IsTrivial();
        gfx->Save();
        if (isTrivial) {
          clipPathFrame->ClipPaint(aContext, aParent, aMatrix);
        } else {
          gfx->PushGroup(gfxASurface::CONTENT_ALPHA);
        }
      }

      SVGFrame->PaintSVG(aContext, nsnull);

      if (clipPathFrame) {
        if (!isTrivial) {
          gfx->PopGroupToSource();

          nsRefPtr<gfxPattern> clipMaskSurface;
          gfx->PushGroup(gfxASurface::CONTENT_ALPHA);

          clipPathFrame->ClipPaint(aContext, aParent, aMatrix);
          clipMaskSurface = gfx->PopGroup();

          if (clipMaskSurface) {
            gfx->Mask(clipMaskSurface);
          }
        }
        gfx->Restore();
      }
    }
  }

  if (clipPathFrame) {
    if (!referencedClipIsTrivial) {
      gfx->PopGroupToSource();

      nsRefPtr<gfxPattern> clipMaskSurface;
      gfx->PushGroup(gfxASurface::CONTENT_ALPHA);

      clipPathFrame->ClipPaint(aContext, aParent, aMatrix);
      clipMaskSurface = gfx->PopGroup();

      if (clipMaskSurface) {
        gfx->Mask(clipMaskSurface);
      }
    }
    gfx->Restore();
  }

  if (isTrivial) {
    gfx->Clip();
    gfx->NewPath();
  }

  return NS_OK;
}

PRBool
nsSVGClipPathFrame::ClipHitTest(nsIFrame* aParent,
                                const gfxMatrix &aMatrix,
                                const nsPoint &aPoint)
{
  // If the flag is set when we get here, it means this clipPath frame
  // has already been used in hit testing against the current clip,
  // and the document has a clip reference loop.
  if (mInUse) {
    NS_WARNING("Clip loop detected!");
    return PR_FALSE;
  }
  AutoClipPathReferencer clipRef(this);

  mClipParent = aParent;
  mClipParentMatrix = NS_NewSVGMatrix(aMatrix);

  nsSVGClipPathFrame *clipPathFrame =
    nsSVGEffects::GetEffectProperties(this).GetClipPathFrame(nsnull);
  if (clipPathFrame && !clipPathFrame->ClipHitTest(aParent, aMatrix, aPoint))
    return PR_FALSE;

  for (nsIFrame* kid = mFrames.FirstChild(); kid;
       kid = kid->GetNextSibling()) {
    nsISVGChildFrame* SVGFrame = do_QueryFrame(kid);
    if (SVGFrame) {
      // Notify the child frame that we may be working with a
      // different transform, so it can update its covered region
      // (used to shortcut hit testing).
      SVGFrame->NotifySVGChanged(nsISVGChildFrame::TRANSFORM_CHANGED);

      if (SVGFrame->GetFrameForPoint(aPoint))
        return PR_TRUE;
    }
  }
  return PR_FALSE;
}

PRBool
nsSVGClipPathFrame::IsTrivial()
{
  // If the clip path is clipped then it's non-trivial
  if (nsSVGEffects::GetEffectProperties(this).GetClipPathFrame(nsnull))
    return PR_FALSE;

  PRBool foundChild = PR_FALSE;

  for (nsIFrame* kid = mFrames.FirstChild(); kid;
       kid = kid->GetNextSibling()) {
    nsISVGChildFrame *svgChild = do_QueryFrame(kid);
    if (svgChild) {
      // We consider a non-trivial clipPath to be one containing
      // either more than one svg child and/or a svg container
      if (foundChild || svgChild->IsDisplayContainer())
        return PR_FALSE;

      // or where the child is itself clipped
      if (nsSVGEffects::GetEffectProperties(kid).GetClipPathFrame(nsnull))
        return PR_FALSE;

      foundChild = PR_TRUE;
    }
  }
  return PR_TRUE;
}

PRBool
nsSVGClipPathFrame::IsValid()
{
  if (mInUse) {
    NS_WARNING("Clip loop detected!");
    return PR_FALSE;
  }
  AutoClipPathReferencer clipRef(this);

  PRBool isOK = PR_TRUE;
  nsSVGEffects::GetEffectProperties(this).GetClipPathFrame(&isOK);
  if (!isOK) {
    return PR_FALSE;
  }

  for (nsIFrame* kid = mFrames.FirstChild(); kid;
       kid = kid->GetNextSibling()) {

    nsIAtom *type = kid->GetType();

    if (type == nsGkAtoms::svgUseFrame) {
      for (nsIFrame* grandKid = kid->GetFirstChild(nsnull); grandKid;
           grandKid = grandKid->GetNextSibling()) {

        nsIAtom *type = grandKid->GetType();

        if (type != nsGkAtoms::svgPathGeometryFrame &&
            type != nsGkAtoms::svgTextFrame) {
          return PR_FALSE;
        }
      }
      continue;
    }
    if (type != nsGkAtoms::svgPathGeometryFrame &&
        type != nsGkAtoms::svgTextFrame) {
      return PR_FALSE;
    }
  }
  return PR_TRUE;
}

NS_IMETHODIMP
nsSVGClipPathFrame::AttributeChanged(PRInt32         aNameSpaceID,
                                     nsIAtom*        aAttribute,
                                     PRInt32         aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      aAttribute == nsGkAtoms::transform) {
    nsSVGUtils::NotifyChildrenOfSVGChange(this,
                                          nsISVGChildFrame::TRANSFORM_CHANGED);
  }

  return nsSVGClipPathFrameBase::AttributeChanged(aNameSpaceID,
                                                  aAttribute, aModType);
}

#ifdef DEBUG
NS_IMETHODIMP
nsSVGClipPathFrame::Init(nsIContent* aContent,
                         nsIFrame* aParent,
                         nsIFrame* aPrevInFlow)
{
  nsCOMPtr<nsIDOMSVGClipPathElement> clipPath = do_QueryInterface(aContent);
  NS_ASSERTION(clipPath, "Content is not an SVG clipPath!");

  return nsSVGClipPathFrameBase::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsIAtom *
nsSVGClipPathFrame::GetType() const
{
  return nsGkAtoms::svgClipPathFrame;
}

gfxMatrix
nsSVGClipPathFrame::GetCanvasTM()
{
  nsSVGClipPathElement *content = static_cast<nsSVGClipPathElement*>(mContent);

  gfxMatrix tm = content->PrependLocalTransformTo(
    nsSVGUtils::ConvertSVGMatrixToThebes(mClipParentMatrix));

  return nsSVGUtils::AdjustMatrixForUnits(tm,
                                          &content->mEnumAttributes[nsSVGClipPathElement::CLIPPATHUNITS],
                                          mClipParent);
}
