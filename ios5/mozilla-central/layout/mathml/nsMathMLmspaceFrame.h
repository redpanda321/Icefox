/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMathMLmspaceFrame_h___
#define nsMathMLmspaceFrame_h___

#include "nsCOMPtr.h"
#include "nsMathMLContainerFrame.h"

//
// <mspace> -- space
//

class nsMathMLmspaceFrame : public nsMathMLContainerFrame {
public:
  NS_DECL_FRAMEARENA_HELPERS

  friend nsIFrame* NS_NewMathMLmspaceFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

  NS_IMETHOD
  TransmitAutomaticData() {
    // The REC defines the following elements to be space-like:
    // * an mtext, mspace, maligngroup, or malignmark element;
    mPresentationData.flags |= NS_MATHML_SPACE_LIKE;
    return NS_OK;
  }

  virtual bool IsLeaf() const;

  NS_IMETHOD
  Reflow(nsPresContext*          aPresContext,
         nsHTMLReflowMetrics&     aDesiredSize,
         const nsHTMLReflowState& aReflowState,
         nsReflowStatus&          aStatus);
  
protected:
  nsMathMLmspaceFrame(nsStyleContext* aContext) : nsMathMLContainerFrame(aContext) {}
  virtual ~nsMathMLmspaceFrame();
  
  virtual PRIntn GetSkipSides() const { return 0; }

private:
  nscoord mWidth;
  nscoord mHeight;
  nscoord mDepth;

  // helper method to initialize our member data
  void 
  ProcessAttributes(nsPresContext* aPresContext);
};

#endif /* nsMathMLmspaceFrame_h___ */
