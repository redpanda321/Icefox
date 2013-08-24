/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for HTML <br> elements */

#include "nsCOMPtr.h"
#include "nsFrame.h"
#include "nsHTMLParts.h"
#include "nsPresContext.h"
#include "nsLineLayout.h"
#include "nsStyleConsts.h"
#include "nsGkAtoms.h"
#include "nsRenderingContext.h"
#include "nsLayoutUtils.h"

#ifdef ACCESSIBILITY
#include "nsIServiceManager.h"
#include "nsAccessibilityService.h"
#endif

//FOR SELECTION
#include "nsIContent.h"
#include "nsFrameSelection.h"
//END INCLUDES FOR SELECTION

class BRFrame : public nsFrame {
public:
  NS_DECL_FRAMEARENA_HELPERS

  friend nsIFrame* NS_NewBRFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

  virtual ContentOffsets CalcContentOffsetsFromFramePoint(nsPoint aPoint);

  virtual bool PeekOffsetNoAmount(bool aForward, PRInt32* aOffset);
  virtual bool PeekOffsetCharacter(bool aForward, PRInt32* aOffset,
                                     bool aRespectClusters = true);
  virtual bool PeekOffsetWord(bool aForward, bool aWordSelectEatSpace, bool aIsKeyboardSelect,
                                PRInt32* aOffset, PeekWordState* aState);

  NS_IMETHOD Reflow(nsPresContext* aPresContext,
                    nsHTMLReflowMetrics& aDesiredSize,
                    const nsHTMLReflowState& aReflowState,
                    nsReflowStatus& aStatus);
  virtual void AddInlineMinWidth(nsRenderingContext *aRenderingContext,
                                 InlineMinWidthData *aData);
  virtual void AddInlinePrefWidth(nsRenderingContext *aRenderingContext,
                                  InlinePrefWidthData *aData);
  virtual nscoord GetMinWidth(nsRenderingContext *aRenderingContext);
  virtual nscoord GetPrefWidth(nsRenderingContext *aRenderingContext);
  virtual nsIAtom* GetType() const;
  virtual nscoord GetBaseline() const;

  virtual bool IsFrameOfType(PRUint32 aFlags) const
  {
    return nsFrame::IsFrameOfType(aFlags & ~(nsIFrame::eReplaced |
                                             nsIFrame::eLineParticipant));
  }

#ifdef ACCESSIBILITY
  virtual already_AddRefed<Accessible> CreateAccessible();
#endif

protected:
  BRFrame(nsStyleContext* aContext) : nsFrame(aContext) {}
  virtual ~BRFrame();

  nscoord mAscent;
};

nsIFrame*
NS_NewBRFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) BRFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(BRFrame)

BRFrame::~BRFrame()
{
}

NS_IMETHODIMP
BRFrame::Reflow(nsPresContext* aPresContext,
                nsHTMLReflowMetrics& aMetrics,
                const nsHTMLReflowState& aReflowState,
                nsReflowStatus& aStatus)
{
  DO_GLOBAL_REFLOW_COUNT("BRFrame");
  DISPLAY_REFLOW(aPresContext, this, aReflowState, aMetrics, aStatus);
  aMetrics.height = 0; // BR frames with height 0 are ignored in quirks
                       // mode by nsLineLayout::VerticalAlignFrames .
                       // However, it's not always 0.  See below.
  aMetrics.width = 0;
  aMetrics.ascent = 0;

  // Only when the BR is operating in a line-layout situation will it
  // behave like a BR.
  nsLineLayout* ll = aReflowState.mLineLayout;
  if (ll) {
    // Note that the compatibility mode check excludes AlmostStandards
    // mode, since this is the inline box model.  See bug 161691.
    if ( ll->LineIsEmpty() ||
         aPresContext->CompatibilityMode() == eCompatibility_FullStandards ) {
      // The line is logically empty; any whitespace is trimmed away.
      //
      // If this frame is going to terminate the line we know
      // that nothing else will go on the line. Therefore, in this
      // case, we provide some height for the BR frame so that it
      // creates some vertical whitespace.  It's necessary to use the
      // line-height rather than the font size because the
      // quirks-mode fix that doesn't apply the block's min
      // line-height makes this necessary to make BR cause a line
      // of the full line-height

      // We also do this in strict mode because BR should act like a
      // normal inline frame.  That line-height is used is important
      // here for cases where the line-height is less than 1.
      nsRefPtr<nsFontMetrics> fm;
      nsLayoutUtils::GetFontMetricsForFrame(this, getter_AddRefs(fm),
        nsLayoutUtils::FontSizeInflationFor(this));
      aReflowState.rendContext->SetFont(fm); // FIXME: maybe not needed?
      if (fm) {
        nscoord logicalHeight = aReflowState.CalcLineHeight();
        aMetrics.height = logicalHeight;
        aMetrics.ascent =
          nsLayoutUtils::GetCenteredFontBaseline(fm, logicalHeight);
      }
      else {
        aMetrics.ascent = aMetrics.height = 0;
      }

      // XXX temporary until I figure out a better solution; see the
      // code in nsLineLayout::VerticalAlignFrames that zaps minY/maxY
      // if the width is zero.
      // XXX This also fixes bug 10036!
      // Warning: nsTextControlFrame::CalculateSizeStandard depends on
      // the following line, see bug 228752.
      aMetrics.width = 1;
    }

    // Return our reflow status
    PRUint32 breakType = aReflowState.mStyleDisplay->mBreakType;
    if (NS_STYLE_CLEAR_NONE == breakType) {
      breakType = NS_STYLE_CLEAR_LINE;
    }

    aStatus = NS_INLINE_BREAK | NS_INLINE_BREAK_AFTER |
      NS_INLINE_MAKE_BREAK_TYPE(breakType);
    ll->SetLineEndsInBR(true);
  }
  else {
    aStatus = NS_FRAME_COMPLETE;
  }

  aMetrics.SetOverflowAreasToDesiredBounds();

  mAscent = aMetrics.ascent;

  NS_FRAME_SET_TRUNCATION(aStatus, aReflowState, aMetrics);
  return NS_OK;
}

/* virtual */ void
BRFrame::AddInlineMinWidth(nsRenderingContext *aRenderingContext,
                           nsIFrame::InlineMinWidthData *aData)
{
  aData->ForceBreak(aRenderingContext);
}

/* virtual */ void
BRFrame::AddInlinePrefWidth(nsRenderingContext *aRenderingContext,
                            nsIFrame::InlinePrefWidthData *aData)
{
  aData->ForceBreak(aRenderingContext);
}

/* virtual */ nscoord
BRFrame::GetMinWidth(nsRenderingContext *aRenderingContext)
{
  nscoord result = 0;
  DISPLAY_MIN_WIDTH(this, result);
  return result;
}

/* virtual */ nscoord
BRFrame::GetPrefWidth(nsRenderingContext *aRenderingContext)
{
  nscoord result = 0;
  DISPLAY_PREF_WIDTH(this, result);
  return result;
}

nsIAtom*
BRFrame::GetType() const
{
  return nsGkAtoms::brFrame;
}

nscoord
BRFrame::GetBaseline() const
{
  return mAscent;
}

nsIFrame::ContentOffsets BRFrame::CalcContentOffsetsFromFramePoint(nsPoint aPoint)
{
  ContentOffsets offsets;
  offsets.content = mContent->GetParent();
  if (offsets.content) {
    offsets.offset = offsets.content->IndexOf(mContent);
    offsets.secondaryOffset = offsets.offset;
    offsets.associateWithNext = true;
  }
  return offsets;
}

bool
BRFrame::PeekOffsetNoAmount(bool aForward, PRInt32* aOffset)
{
  NS_ASSERTION (aOffset && *aOffset <= 1, "aOffset out of range");
  PRInt32 startOffset = *aOffset;
  // If we hit the end of a BR going backwards, go to its beginning and stay there.
  if (!aForward && startOffset != 0) {
    *aOffset = 0;
    return true;
  }
  // Otherwise, stop if we hit the beginning, continue (forward) if we hit the end.
  return (startOffset == 0);
}

bool
BRFrame::PeekOffsetCharacter(bool aForward, PRInt32* aOffset,
                             bool aRespectClusters)
{
  NS_ASSERTION (aOffset && *aOffset <= 1, "aOffset out of range");
  // Keep going. The actual line jumping will stop us.
  return false;
}

bool
BRFrame::PeekOffsetWord(bool aForward, bool aWordSelectEatSpace, bool aIsKeyboardSelect,
                        PRInt32* aOffset, PeekWordState* aState)
{
  NS_ASSERTION (aOffset && *aOffset <= 1, "aOffset out of range");
  // Keep going. The actual line jumping will stop us.
  return false;
}

#ifdef ACCESSIBILITY
already_AddRefed<Accessible>
BRFrame::CreateAccessible()
{
  nsAccessibilityService* accService = nsIPresShell::AccService();
  if (!accService) {
    return nsnull;
  }
  nsIContent *parent = mContent->GetParent();
  if (parent &&
      parent->IsRootOfNativeAnonymousSubtree() &&
      parent->GetChildCount() == 1) {
    // This <br> is the only node in a text control, therefore it is the hacky
    // "bogus node" used when there is no text in the control
    return nsnull;
  }
  return accService->CreateHTMLBRAccessible(mContent,
                                            PresContext()->PresShell());
}
#endif

