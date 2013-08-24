
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

#ifndef __NS_SVGGEOMETRYFRAME_H__
#define __NS_SVGGEOMETRYFRAME_H__

#include "nsFrame.h"
#include "gfxMatrix.h"

class nsSVGPaintServerFrame;
class gfxContext;

typedef nsFrame nsSVGGeometryFrameBase;

/* nsSVGGeometryFrame is a base class for SVG objects that directly
 * have geometry (circle, ellipse, line, polyline, polygon, path, and
 * glyph frames).  It knows how to convert the style information into
 * cairo context information and stores the fill/stroke paint
 * servers. */

class nsSVGGeometryFrame : public nsSVGGeometryFrameBase
{
protected:
  NS_DECL_FRAMEARENA_HELPERS

  nsSVGGeometryFrame(nsStyleContext *aContext) : nsSVGGeometryFrameBase(aContext) {}

public:
  // nsIFrame interface:
  NS_IMETHOD Init(nsIContent* aContent,
                  nsIFrame* aParent,
                  nsIFrame* aPrevInFlow);

  virtual PRBool IsFrameOfType(PRUint32 aFlags) const
  {
    return nsSVGGeometryFrameBase::IsFrameOfType(aFlags & ~(nsIFrame::eSVG | nsIFrame::eSVGGeometry));
  }

  // nsSVGGeometryFrame methods:
  virtual gfxMatrix GetCanvasTM() = 0;
  PRUint16 GetClipRule();
  PRBool IsClipChild(); 

  float GetStrokeWidth();

  /*
   * Set up a cairo context for filling a path
   * @return PR_FALSE to skip rendering
   */
  PRBool SetupCairoFill(gfxContext *aContext);
  /*
   * @return PR_FALSE if there is no stroke
   */
  PRBool HasStroke();
  /*
   * Set up a cairo context for measuring a stroked path
   */
  void SetupCairoStrokeGeometry(gfxContext *aContext);
  /*
   * Set up a cairo context for hit testing a stroked path
   */
  void SetupCairoStrokeHitGeometry(gfxContext *aContext);
  /*
   * Set up a cairo context for stroking a path
   * @return PR_FALSE to skip rendering
   */
  PRBool SetupCairoStroke(gfxContext *aContext);

protected:
  nsSVGPaintServerFrame *GetPaintServer(const nsStyleSVGPaint *aPaint,
                                        const FramePropertyDescriptor *aProperty);

private:
  nsresult GetStrokeDashArray(double **arr, PRUint32 *count);
  float GetStrokeDashoffset();

  /**
   * Returns the given 'fill-opacity' or 'stroke-opacity' value multiplied by
   * the value of the 'opacity' property if it's possible to avoid the expense
   * of creating and compositing an offscreen surface for 'opacity' by
   * combining 'opacity' with the 'fill-opacity'/'stroke-opacity'. If not, the
   * given 'fill-opacity'/'stroke-opacity' is returned unmodified.
   */
  float MaybeOptimizeOpacity(float aFillOrStrokeOpacity);
};

#endif // __NS_SVGGEOMETRYFRAME_H__
