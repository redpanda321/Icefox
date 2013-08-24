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
 * The Initial Developer of the Original Code is
 * Scooter Morris.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Scooter Morris <scootermorris@comcast.net>
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

#ifndef __NS_SVGPATTERNFRAME_H__
#define __NS_SVGPATTERNFRAME_H__

#include "nsIDOMSVGMatrix.h"
#include "nsSVGPaintServerFrame.h"
#include "gfxMatrix.h"

class nsSVGPreserveAspectRatio;
class nsIFrame;
class nsSVGLength2;
class nsSVGElement;
class gfxContext;
class gfxASurface;

typedef nsSVGPaintServerFrame  nsSVGPatternFrameBase;

/**
 * Patterns can refer to other patterns. We create an nsSVGPaintingProperty
 * with property type nsGkAtoms::href to track the referenced pattern.
 */
class nsSVGPatternFrame : public nsSVGPatternFrameBase
{
public:
  NS_DECL_FRAMEARENA_HELPERS

  friend nsIFrame* NS_NewSVGPatternFrame(nsIPresShell* aPresShell,
                                         nsStyleContext* aContext);

  nsSVGPatternFrame(nsStyleContext* aContext);

  // nsSVGPaintServerFrame methods:
  virtual already_AddRefed<gfxPattern>
    GetPaintServerPattern(nsIFrame *aSource,
                          float aOpacity,
                          const gfxRect *aOverrideBounds);

public:
  // nsSVGContainerFrame methods:
  virtual gfxMatrix GetCanvasTM();

  // nsIFrame interface:
  virtual void DidSetStyleContext(nsStyleContext* aOldStyleContext);

  NS_IMETHOD AttributeChanged(PRInt32         aNameSpaceID,
                              nsIAtom*        aAttribute,
                              PRInt32         aModType);

#ifdef DEBUG
  NS_IMETHOD Init(nsIContent*      aContent,
                  nsIFrame*        aParent,
                  nsIFrame*        aPrevInFlow);
#endif

  /**
   * Get the "type" of the frame
   *
   * @see nsGkAtoms::svgPatternFrame
   */
  virtual nsIAtom* GetType() const;

#ifdef DEBUG
  NS_IMETHOD GetFrameName(nsAString& aResult) const
  {
    return MakeFrameName(NS_LITERAL_STRING("SVGPattern"), aResult);
  }
#endif // DEBUG

protected:
  // Internal methods for handling referenced patterns
  nsSVGPatternFrame* GetReferencedPattern();
  // Helper to look at our pattern and then along its reference chain (if any)
  // to find the first pattern with the specified attribute. Returns
  // null if there isn't one.
  nsSVGPatternElement* GetPatternWithAttr(nsIAtom *aAttrName, nsIContent *aDefault);

  //
  const nsSVGLength2 *GetX();
  const nsSVGLength2 *GetY();
  const nsSVGLength2 *GetWidth();
  const nsSVGLength2 *GetHeight();

  PRUint16 GetPatternUnits();
  PRUint16 GetPatternContentUnits();
  gfxMatrix GetPatternTransform();

  const nsSVGViewBox &GetViewBox();
  const nsSVGPreserveAspectRatio &GetPreserveAspectRatio();


  nsresult PaintPattern(gfxASurface **surface,
                        gfxMatrix *patternMatrix,
                        nsIFrame *aSource,
                        float aGraphicOpacity,
                        const gfxRect *aOverrideBounds);
  NS_IMETHOD GetPatternFirstChild(nsIFrame **kid);
  gfxRect    GetPatternRect(const gfxRect &bbox,
                            const gfxMatrix &callerCTM,
                            nsIFrame *aTarget);
  gfxMatrix  GetPatternMatrix(const gfxRect &bbox,
                              const gfxRect &callerBBox,
                              const gfxMatrix &callerCTM);
  gfxMatrix  ConstructCTM(const gfxRect &callerBBox,
                          const gfxMatrix &callerCTM,
                          nsIFrame *aTarget);
  nsresult   GetTargetGeometry(gfxMatrix *aCTM,
                               gfxRect *aBBox,
                               nsIFrame *aTarget,
                               const gfxRect *aOverrideBounds);

private:
  // this is a *temporary* reference to the frame of the element currently
  // referencing our pattern.  This must be temporary because different
  // referencing frames will all reference this one frame
  nsSVGGeometryFrame               *mSource;
  nsCOMPtr<nsIDOMSVGMatrix>         mCTM;

protected:
  // This flag is used to detect loops in xlink:href processing
  PRPackedBool                      mLoopFlag;
  PRPackedBool                      mNoHRefURI;
};

#endif
