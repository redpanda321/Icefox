/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsSimplePageSequence_h___
#define nsSimplePageSequence_h___

#include "mozilla/Attributes.h"
#include "nsIPageSequenceFrame.h"
#include "nsContainerFrame.h"
#include "nsIPrintSettings.h"
#include "nsIPrintOptions.h"
#include "nsIDateTimeFormat.h"
#include "nsHTMLCanvasElement.h"

//-----------------------------------------------
// This class maintains all the data that 
// is used by all the page frame
// It lives while the nsSimplePageSequenceFrame lives
class nsSharedPageData {
public:
  nsSharedPageData();
  ~nsSharedPageData();

  PRUnichar * mDateTimeStr;
  nsFont *    mHeadFootFont;
  PRUnichar * mPageNumFormat;
  PRUnichar * mPageNumAndTotalsFormat;
  PRUnichar * mDocTitle;
  PRUnichar * mDocURL;

  nsSize      mReflowSize;
  nsMargin    mReflowMargin;
  // Margin for headers and footers; it defaults to 4/100 of an inch on UNIX 
  // and 0 elsewhere; I think it has to do with some inconsistency in page size
  // computations
  nsMargin    mEdgePaperMargin;

  nsCOMPtr<nsIPrintSettings> mPrintSettings;
  nsCOMPtr<nsIPrintOptions> mPrintOptions;

  nscoord      mPageContentXMost;      // xmost size from Reflow(width)
  nscoord      mPageContentSize;       // constrained size (width)
};

// Simple page sequence frame class. Used when we're in paginated mode
class nsSimplePageSequenceFrame : public nsContainerFrame,
                                  public nsIPageSequenceFrame {
public:
  friend nsIFrame* NS_NewSimplePageSequenceFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS

  // nsIFrame
  NS_IMETHOD  Reflow(nsPresContext*      aPresContext,
                     nsHTMLReflowMetrics& aDesiredSize,
                     const nsHTMLReflowState& aMaxSize,
                     nsReflowStatus&      aStatus);

  NS_IMETHOD  BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                               const nsRect&           aDirtyRect,
                               const nsDisplayListSet& aLists);

  // nsIPageSequenceFrame
  NS_IMETHOD SetPageNo(int32_t aPageNo) { return NS_OK;}
  NS_IMETHOD SetSelectionHeight(nscoord aYOffset, nscoord aHeight) MOZ_OVERRIDE { mYSelOffset = aYOffset; mSelectionHeight = aHeight; return NS_OK; }
  NS_IMETHOD SetTotalNumPages(int32_t aTotal) MOZ_OVERRIDE { mTotalPages = aTotal; return NS_OK; }
  
  // For Shrink To Fit
  NS_IMETHOD GetSTFPercent(float& aSTFPercent) MOZ_OVERRIDE;

  // Async Printing
  NS_IMETHOD StartPrint(nsPresContext*  aPresContext,
                        nsIPrintSettings* aPrintSettings,
                        PRUnichar*        aDocTitle,
                        PRUnichar*        aDocURL);
  NS_IMETHOD PrePrintNextPage(nsITimerCallback* aCallback, bool* aDone) MOZ_OVERRIDE;
  NS_IMETHOD PrintNextPage() MOZ_OVERRIDE;
  NS_IMETHOD ResetPrintCanvasList() MOZ_OVERRIDE;
  NS_IMETHOD GetCurrentPageNum(int32_t* aPageNum) MOZ_OVERRIDE;
  NS_IMETHOD GetNumPages(int32_t* aNumPages) MOZ_OVERRIDE;
  NS_IMETHOD IsDoingPrintRange(bool* aDoing) MOZ_OVERRIDE;
  NS_IMETHOD GetPrintRange(int32_t* aFromPage, int32_t* aToPage) MOZ_OVERRIDE;
  NS_IMETHOD DoPageEnd() MOZ_OVERRIDE;

  // We must allow Print Preview UI to have a background, no matter what the
  // user's settings
  virtual bool HonorPrintBackgroundSettings() { return false; }

  virtual bool HasTransformGetter() const MOZ_OVERRIDE { return true; }

  /**
   * Get the "type" of the frame
   *
   * @see nsGkAtoms::sequenceFrame
   */
  virtual nsIAtom* GetType() const;

#ifdef DEBUG
  NS_IMETHOD  GetFrameName(nsAString& aResult) const;
#endif

protected:
  nsSimplePageSequenceFrame(nsStyleContext* aContext);
  virtual ~nsSimplePageSequenceFrame();

  void SetPageNumberFormat(const char* aPropName, const char* aDefPropVal, bool aPageNumOnly);

  // SharedPageData Helper methods
  void SetDateTimeStr(PRUnichar * aDateTimeStr);
  void SetPageNumberFormat(PRUnichar * aFormatStr, bool aForPageNumOnly);

  // Sets the frame desired size to the size of the viewport, or the given
  // nscoords, whichever is larger. Print scaling is applied in this function.
  void SetDesiredSize(nsHTMLReflowMetrics& aDesiredSize,
                      const nsHTMLReflowState& aReflowState,
                      nscoord aWidth, nscoord aHeight);

  void         DetermineWhetherToPrintPage();

  nsMargin mMargin;

  // I18N date formatter service which we'll want to cache locally.
  nsCOMPtr<nsIDateTimeFormat> mDateFormatter;

  nsSize       mSize;
  nsSharedPageData* mPageData; // data shared by all the nsPageFrames

  // Asynch Printing
  nsIFrame *   mCurrentPageFrame;
  int32_t      mPageNum;
  int32_t      mTotalPages;
  int32_t      mPrintRangeType;
  int32_t      mFromPageNum;
  int32_t      mToPageNum;
  nsTArray<int32_t> mPageRanges;
  nsTArray<nsRefPtr<nsHTMLCanvasElement> > mCurrentCanvasList;

  // Selection Printing Info
  nscoord      mSelectionHeight;
  nscoord      mYSelOffset;

  // Asynch Printing
  bool mPrintThisPage;
  bool mDoingPageRange;

  bool mIsPrintingSelection;

  bool mCalledBeginPage;

  bool mCurrentCanvasListSetup;
};

#endif /* nsSimplePageSequence_h___ */

