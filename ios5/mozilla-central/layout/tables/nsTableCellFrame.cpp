/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsTableFrame.h"
#include "nsTableColFrame.h"
#include "nsTableCellFrame.h"
#include "nsTableRowGroupFrame.h"
#include "nsTablePainter.h"
#include "nsStyleContext.h"
#include "nsStyleConsts.h"
#include "nsPresContext.h"
#include "nsRenderingContext.h"
#include "nsCSSRendering.h"
#include "nsIContent.h"
#include "nsGenericHTMLElement.h"
#include "nsHTMLParts.h"
#include "nsGkAtoms.h"
#include "nsIPresShell.h"
#include "nsCOMPtr.h"
#include "nsIDOMHTMLTableCellElement.h"
#ifdef ACCESSIBILITY
#include "nsAccessibilityService.h"
#endif
#include "nsIServiceManager.h"
#include "nsIDOMNode.h"
#include "nsINameSpaceManager.h"
#include "nsDisplayList.h"
#include "nsLayoutUtils.h"
#include "nsTextFrame.h"

//TABLECELL SELECTION
#include "nsFrameSelection.h"
#include "mozilla/LookAndFeel.h"

using namespace mozilla;


nsTableCellFrame::nsTableCellFrame(nsStyleContext* aContext) :
  nsContainerFrame(aContext)
{
  mColIndex = 0;
  mPriorAvailWidth = 0;

  SetContentEmpty(false);
  SetHasPctOverHeight(false);
}

nsTableCellFrame::~nsTableCellFrame()
{
}

NS_IMPL_FRAMEARENA_HELPERS(nsTableCellFrame)

nsTableCellFrame*
nsTableCellFrame::GetNextCell() const
{
  nsIFrame* childFrame = GetNextSibling();
  while (childFrame) {
    nsTableCellFrame *cellFrame = do_QueryFrame(childFrame);
    if (cellFrame) {
      return cellFrame;
    }
    childFrame = childFrame->GetNextSibling();
  }
  return nsnull;
}

NS_IMETHODIMP
nsTableCellFrame::Init(nsIContent*      aContent,
                       nsIFrame*        aParent,
                       nsIFrame*        aPrevInFlow)
{
  // Let the base class do its initialization
  nsresult rv = nsContainerFrame::Init(aContent, aParent, aPrevInFlow);

  if (GetStateBits() & NS_FRAME_FONT_INFLATION_CONTAINER) {
    AddStateBits(NS_FRAME_FONT_INFLATION_FLOW_ROOT);
  }

  if (aPrevInFlow) {
    // Set the column index
    nsTableCellFrame* cellFrame = (nsTableCellFrame*)aPrevInFlow;
    PRInt32           colIndex;
    cellFrame->GetColIndex(colIndex);
    SetColIndex(colIndex);
  }

  return rv;
}

// nsIPercentHeightObserver methods

void
nsTableCellFrame::NotifyPercentHeight(const nsHTMLReflowState& aReflowState)
{
  // nsHTMLReflowState ensures the mCBReflowState of blocks inside a
  // cell is the cell frame, not the inner-cell block, and that the
  // containing block of an inner table is the containing block of its
  // outer table.
  // XXXldb Given the now-stricter |NeedsToObserve|, many if not all of
  // these tests are probably unnecessary.

  // Maybe the cell reflow state; we sure if we're inside the |if|.
  const nsHTMLReflowState *cellRS = aReflowState.mCBReflowState;

  if (cellRS && cellRS->frame == this &&
      (cellRS->ComputedHeight() == NS_UNCONSTRAINEDSIZE ||
       cellRS->ComputedHeight() == 0)) { // XXXldb Why 0?
    // This is a percentage height on a frame whose percentage heights
    // are based on the height of the cell, since its containing block
    // is the inner cell frame.

    // We'll only honor the percent height if sibling-cells/ancestors
    // have specified/pct height. (Also, siblings only count for this if
    // both this cell and the sibling cell span exactly 1 row.)

    if (nsTableFrame::AncestorsHaveStyleHeight(*cellRS) ||
        (nsTableFrame::GetTableFrame(this)->GetEffectiveRowSpan(*this) == 1 &&
         (cellRS->parentReflowState->frame->GetStateBits() &
          NS_ROW_HAS_CELL_WITH_STYLE_HEIGHT))) {

      for (const nsHTMLReflowState *rs = aReflowState.parentReflowState;
           rs != cellRS;
           rs = rs->parentReflowState) {
        rs->frame->AddStateBits(NS_FRAME_CONTAINS_RELATIVE_HEIGHT);
      }

      nsTableFrame::RequestSpecialHeightReflow(*cellRS);
    }
  }
}

// The cell needs to observe its block and things inside its block but nothing below that
bool
nsTableCellFrame::NeedsToObserve(const nsHTMLReflowState& aReflowState)
{
  const nsHTMLReflowState *rs = aReflowState.parentReflowState;
  if (!rs)
    return false;
  if (rs->frame == this) {
    // We always observe the child block.  It will never send any
    // notifications, but we need this so that the observer gets
    // propagated to its kids.
    return true;
  }
  rs = rs->parentReflowState;
  if (!rs) {
    return false;
  }

  // We always need to let the percent height observer be propagated
  // from an outer table frame to an inner table frame.
  nsIAtom *fType = aReflowState.frame->GetType();
  if (fType == nsGkAtoms::tableFrame) {
    return true;
  }

  // We need the observer to be propagated to all children of the cell
  // (i.e., children of the child block) in quirks mode, but only to
  // tables in standards mode.
  return rs->frame == this &&
         (PresContext()->CompatibilityMode() == eCompatibility_NavQuirks ||
          fType == nsGkAtoms::tableOuterFrame);
}

nsresult
nsTableCellFrame::GetRowIndex(PRInt32 &aRowIndex) const
{
  nsresult result;
  nsTableRowFrame* row = static_cast<nsTableRowFrame*>(GetParent());
  if (row) {
    aRowIndex = row->GetRowIndex();
    result = NS_OK;
  }
  else {
    aRowIndex = 0;
    result = NS_ERROR_NOT_INITIALIZED;
  }
  return result;
}

nsresult
nsTableCellFrame::GetColIndex(PRInt32 &aColIndex) const
{
  if (GetPrevInFlow()) {
    return ((nsTableCellFrame*)GetFirstInFlow())->GetColIndex(aColIndex);
  }
  else {
    aColIndex = mColIndex;
    return  NS_OK;
  }
}

NS_IMETHODIMP
nsTableCellFrame::AttributeChanged(PRInt32         aNameSpaceID,
                                   nsIAtom*        aAttribute,
                                   PRInt32         aModType)
{
  // We need to recalculate in this case because of the nowrap quirk in
  // BasicTableLayoutStrategy
  if (aNameSpaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::nowrap &&
      PresContext()->CompatibilityMode() == eCompatibility_NavQuirks) {
    PresContext()->PresShell()->
      FrameNeedsReflow(this, nsIPresShell::eTreeChange, NS_FRAME_IS_DIRTY);
  }
  // let the table frame decide what to do
  nsTableFrame* tableFrame = nsTableFrame::GetTableFrame(this);
  tableFrame->AttributeChangedFor(this, mContent, aAttribute);
  return NS_OK;
}

/* virtual */ void
nsTableCellFrame::DidSetStyleContext(nsStyleContext* aOldStyleContext)
{
  if (!aOldStyleContext) //avoid this on init
    return;

  nsTableFrame* tableFrame = nsTableFrame::GetTableFrame(this);
  if (tableFrame->IsBorderCollapse() &&
      tableFrame->BCRecalcNeeded(aOldStyleContext, GetStyleContext())) {
    PRInt32 colIndex, rowIndex;
    GetColIndex(colIndex);
    GetRowIndex(rowIndex);
    // row span needs to be clamped as we do not create rows in the cellmap
    // which do not have cells originating in them
    nsIntRect damageArea(colIndex, rowIndex, GetColSpan(),
      NS_MIN(GetRowSpan(), tableFrame->GetRowCount() - rowIndex));
    tableFrame->AddBCDamageArea(damageArea);
  }
}


NS_IMETHODIMP
nsTableCellFrame::AppendFrames(ChildListID     aListID,
                               nsFrameList&    aFrameList)
{
  NS_PRECONDITION(false, "unsupported operation");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsTableCellFrame::InsertFrames(ChildListID     aListID,
                               nsIFrame*       aPrevFrame,
                               nsFrameList&    aFrameList)
{
  NS_PRECONDITION(false, "unsupported operation");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsTableCellFrame::RemoveFrame(ChildListID     aListID,
                              nsIFrame*       aOldFrame)
{
  NS_PRECONDITION(false, "unsupported operation");
  return NS_ERROR_NOT_IMPLEMENTED;
}

void nsTableCellFrame::SetColIndex(PRInt32 aColIndex)
{
  mColIndex = aColIndex;
}

/* virtual */ nsMargin
nsTableCellFrame::GetUsedMargin() const
{
  return nsMargin(0,0,0,0);
}

//ASSURE DIFFERENT COLORS for selection
inline nscolor EnsureDifferentColors(nscolor colorA, nscolor colorB)
{
    if (colorA == colorB)
    {
      nscolor res;
      res = NS_RGB(NS_GET_R(colorA) ^ 0xff,
                   NS_GET_G(colorA) ^ 0xff,
                   NS_GET_B(colorA) ^ 0xff);
      return res;
    }
    return colorA;
}

void
nsTableCellFrame::DecorateForSelection(nsRenderingContext& aRenderingContext,
                                       nsPoint aPt)
{
  NS_ASSERTION(IsSelected(), "Should only be called for selected cells");
  PRInt16 displaySelection;
  nsPresContext* presContext = PresContext();
  displaySelection = DisplaySelection(presContext);
  if (displaySelection) {
    nsRefPtr<nsFrameSelection> frameSelection =
      presContext->PresShell()->FrameSelection();

    if (frameSelection->GetTableCellSelection()) {
      nscolor       bordercolor;
      if (displaySelection == nsISelectionController::SELECTION_DISABLED) {
        bordercolor = NS_RGB(176,176,176);// disabled color
      }
      else {
        bordercolor =
          LookAndFeel::GetColor(LookAndFeel::eColorID_TextSelectBackground);
      }
      nscoord threePx = nsPresContext::CSSPixelsToAppUnits(3);
      if ((mRect.width > threePx) && (mRect.height > threePx))
      {
        //compare bordercolor to ((nsStyleColor *)myColor)->mBackgroundColor)
        bordercolor = EnsureDifferentColors(bordercolor,
                                            GetStyleBackground()->mBackgroundColor);
        nsRenderingContext::AutoPushTranslation
            translate(&aRenderingContext, aPt);
        nscoord onePixel = nsPresContext::CSSPixelsToAppUnits(1);

        aRenderingContext.SetColor(bordercolor);
        aRenderingContext.DrawLine(onePixel, 0, mRect.width, 0);
        aRenderingContext.DrawLine(0, onePixel, 0, mRect.height);
        aRenderingContext.DrawLine(onePixel, mRect.height, mRect.width, mRect.height);
        aRenderingContext.DrawLine(mRect.width, onePixel, mRect.width, mRect.height);
        //middle
        aRenderingContext.DrawRect(onePixel, onePixel, mRect.width-onePixel,
                                   mRect.height-onePixel);
        //shading
        aRenderingContext.DrawLine(2*onePixel, mRect.height-2*onePixel,
                                   mRect.width-onePixel, mRect.height- (2*onePixel));
        aRenderingContext.DrawLine(mRect.width - (2*onePixel), 2*onePixel,
                                   mRect.width - (2*onePixel), mRect.height-onePixel);
      }
    }
  }
}

void
nsTableCellFrame::PaintBackground(nsRenderingContext& aRenderingContext,
                                  const nsRect&        aDirtyRect,
                                  nsPoint              aPt,
                                  PRUint32             aFlags)
{
  nsRect rect(aPt, GetSize());
  nsCSSRendering::PaintBackground(PresContext(), aRenderingContext, this,
                                  aDirtyRect, rect, aFlags);
}

// Called by nsTablePainter
void
nsTableCellFrame::PaintCellBackground(nsRenderingContext& aRenderingContext,
                                      const nsRect& aDirtyRect, nsPoint aPt,
                                      PRUint32 aFlags)
{
  if (!GetStyleVisibility()->IsVisible())
    return;

  PaintBackground(aRenderingContext, aDirtyRect, aPt, aFlags);
}

class nsDisplayTableCellBackground : public nsDisplayTableItem {
public:
  nsDisplayTableCellBackground(nsDisplayListBuilder* aBuilder,
                               nsTableCellFrame* aFrame) :
    nsDisplayTableItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayTableCellBackground);
  }
#ifdef NS_BUILD_REFCNT_LOGGING
  virtual ~nsDisplayTableCellBackground() {
    MOZ_COUNT_DTOR(nsDisplayTableCellBackground);
  }
#endif

  virtual void HitTest(nsDisplayListBuilder* aBuilder, const nsRect& aRect,
                       HitTestState* aState, nsTArray<nsIFrame*> *aOutFrames) {
    aOutFrames->AppendElement(mFrame);
  }
  virtual void Paint(nsDisplayListBuilder* aBuilder,
                     nsRenderingContext* aCtx);
  virtual nsRect GetBounds(nsDisplayListBuilder* aBuilder, bool* aSnap);

  NS_DISPLAY_DECL_NAME("TableCellBackground", TYPE_TABLE_CELL_BACKGROUND)
};

void nsDisplayTableCellBackground::Paint(nsDisplayListBuilder* aBuilder,
                                         nsRenderingContext* aCtx)
{
  static_cast<nsTableCellFrame*>(mFrame)->
    PaintBackground(*aCtx, mVisibleRect, ToReferenceFrame(),
                    aBuilder->GetBackgroundPaintFlags());
}

nsRect
nsDisplayTableCellBackground::GetBounds(nsDisplayListBuilder* aBuilder,
                                        bool* aSnap)
{
  // revert from nsDisplayTableItem's implementation ... cell backgrounds
  // don't overflow the cell
  return nsDisplayItem::GetBounds(aBuilder, aSnap);
}

static void
PaintTableCellSelection(nsIFrame* aFrame, nsRenderingContext* aCtx,
                        const nsRect& aRect, nsPoint aPt)
{
  static_cast<nsTableCellFrame*>(aFrame)->DecorateForSelection(*aCtx, aPt);
}

NS_IMETHODIMP
nsTableCellFrame::BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                   const nsRect&           aDirtyRect,
                                   const nsDisplayListSet& aLists)
{
  DO_GLOBAL_REFLOW_COUNT_DSP("nsTableCellFrame");
  if (IsVisibleInSelection(aBuilder)) {
    nsTableFrame* tableFrame = nsTableFrame::GetTableFrame(this);
    PRInt32 emptyCellStyle = GetContentEmpty() && !tableFrame->IsBorderCollapse() ?
                                GetStyleTableBorder()->mEmptyCells
                                : NS_STYLE_TABLE_EMPTY_CELLS_SHOW;
    // take account of 'empty-cells'
    if (GetStyleVisibility()->IsVisible() &&
        (NS_STYLE_TABLE_EMPTY_CELLS_HIDE != emptyCellStyle)) {
    
    
      bool isRoot = aBuilder->IsAtRootOfPseudoStackingContext();
      if (!isRoot) {
        nsDisplayTableItem* currentItem = aBuilder->GetCurrentTableItem();
        if (currentItem) {
          currentItem->UpdateForFrameBackground(this);
        }
      }
    
      // display outset box-shadows if we need to.
      const nsStyleBorder* borderStyle = GetStyleBorder();
      bool hasBoxShadow = !!borderStyle->mBoxShadow;
      if (hasBoxShadow) {
        nsresult rv = aLists.BorderBackground()->AppendNewToTop(
            new (aBuilder) nsDisplayBoxShadowOuter(aBuilder, this));
        NS_ENSURE_SUCCESS(rv, rv);
      }
    
      // display background if we need to.
      if (aBuilder->IsForEventDelivery() ||
          (((!tableFrame->IsBorderCollapse() || isRoot) &&
          (!GetStyleBackground()->IsTransparent() || GetStyleDisplay()->mAppearance)))) {
        // The cell background was not painted by the nsTablePainter,
        // so we need to do it. We have special background processing here
        // so we need to duplicate some code from nsFrame::DisplayBorderBackgroundOutline
        nsDisplayTableItem* item =
          new (aBuilder) nsDisplayTableCellBackground(aBuilder, this);
        nsresult rv = aLists.BorderBackground()->AppendNewToTop(item);
        NS_ENSURE_SUCCESS(rv, rv);
        item->UpdateForFrameBackground(this);
      }
    
      // display inset box-shadows if we need to.
      if (hasBoxShadow) {
        nsresult rv = aLists.BorderBackground()->AppendNewToTop(
            new (aBuilder) nsDisplayBoxShadowInner(aBuilder, this));
        NS_ENSURE_SUCCESS(rv, rv);
      }
    
      // display borders if we need to
      if (!tableFrame->IsBorderCollapse() && borderStyle->HasBorder() &&
          emptyCellStyle == NS_STYLE_TABLE_EMPTY_CELLS_SHOW) {
        nsresult rv = aLists.BorderBackground()->AppendNewToTop(new (aBuilder)
            nsDisplayBorder(aBuilder, this));
        NS_ENSURE_SUCCESS(rv, rv);
      }
    
      // and display the selection border if we need to
      if (IsSelected()) {
        nsresult rv = aLists.BorderBackground()->AppendNewToTop(new (aBuilder)
            nsDisplayGeneric(aBuilder, this, ::PaintTableCellSelection,
                             "TableCellSelection",
                             nsDisplayItem::TYPE_TABLE_CELL_SELECTION));
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
    
    // the 'empty-cells' property has no effect on 'outline'
    nsresult rv = DisplayOutline(aBuilder, aLists);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Push a null 'current table item' so that descendant tables can't
  // accidentally mess with our table
  nsAutoPushCurrentTableItem pushTableItem;
  pushTableItem.Push(aBuilder, nsnull);

  nsIFrame* kid = mFrames.FirstChild();
  NS_ASSERTION(kid && !kid->GetNextSibling(), "Table cells should have just one child");
  // The child's background will go in our BorderBackground() list.
  // This isn't a problem since it won't have a real background except for
  // event handling. We do not call BuildDisplayListForNonBlockChildren
  // because that/ would put the child's background in the Content() list
  // which isn't right (e.g., would end up on top of our child floats for
  // event handling).
  return BuildDisplayListForChild(aBuilder, kid, aDirtyRect, aLists);
}

PRIntn
nsTableCellFrame::GetSkipSides() const
{
  PRIntn skip = 0;
  if (nsnull != GetPrevInFlow()) {
    skip |= 1 << NS_SIDE_TOP;
  }
  if (nsnull != GetNextInFlow()) {
    skip |= 1 << NS_SIDE_BOTTOM;
  }
  return skip;
}

/* virtual */ nsMargin
nsTableCellFrame::GetBorderOverflow()
{
  return nsMargin(0, 0, 0, 0);
}

// Align the cell's child frame within the cell

void nsTableCellFrame::VerticallyAlignChild(nscoord aMaxAscent)
{
  /* It's the 'border-collapse' on the table that matters */
  nsMargin borderPadding = GetUsedBorderAndPadding();

  nscoord topInset = borderPadding.top;
  nscoord bottomInset = borderPadding.bottom;

  PRUint8 verticalAlignFlags = GetVerticalAlign();

  nscoord height = mRect.height;
  nsIFrame* firstKid = mFrames.FirstChild();
  NS_ASSERTION(firstKid, "Frame construction error, a table cell always has an inner cell frame");
  nsRect kidRect = firstKid->GetRect();
  nscoord childHeight = kidRect.height;

  // Vertically align the child
  nscoord kidYTop = 0;
  switch (verticalAlignFlags)
  {
    case NS_STYLE_VERTICAL_ALIGN_BASELINE:
      // Align the baselines of the child frame with the baselines of
      // other children in the same row which have 'vertical-align: baseline'
      kidYTop = topInset + aMaxAscent - GetCellBaseline();
    break;

    case NS_STYLE_VERTICAL_ALIGN_TOP:
      // Align the top of the child frame with the top of the content area,
      kidYTop = topInset;
    break;

    case NS_STYLE_VERTICAL_ALIGN_BOTTOM:
      // Align the bottom of the child frame with the bottom of the content area,
      kidYTop = height - childHeight - bottomInset;
    break;

    default:
    case NS_STYLE_VERTICAL_ALIGN_MIDDLE:
      // Align the middle of the child frame with the middle of the content area,
      kidYTop = (height - childHeight - bottomInset + topInset) / 2;
  }
  // if the content is larger than the cell height align from top
  kidYTop = NS_MAX(0, kidYTop);

  if (kidYTop != kidRect.y) {
    // Invalidate at the old position first
    firstKid->InvalidateFrameSubtree();
  }

  firstKid->SetPosition(nsPoint(kidRect.x, kidYTop));
  nsHTMLReflowMetrics desiredSize;
  desiredSize.width = mRect.width;
  desiredSize.height = mRect.height;

  nsRect overflow(nsPoint(0,0), GetSize());
  overflow.Inflate(GetBorderOverflow());
  desiredSize.mOverflowAreas.SetAllTo(overflow);
  ConsiderChildOverflow(desiredSize.mOverflowAreas, firstKid);
  FinishAndStoreOverflow(&desiredSize);
  if (kidYTop != kidRect.y) {
    // Make sure any child views are correctly positioned. We know the inner table
    // cell won't have a view
    nsContainerFrame::PositionChildViews(firstKid);

    // Invalidate new overflow rect
    firstKid->InvalidateFrameSubtree();
  }
  if (HasView()) {
    nsContainerFrame::SyncFrameViewAfterReflow(PresContext(), this,
                                               GetView(),
                                               desiredSize.VisualOverflow(), 0);
  }
}

bool
nsTableCellFrame::UpdateOverflow()
{
  nsRect bounds(nsPoint(0,0), GetSize());
  bounds.Inflate(GetBorderOverflow());
  nsOverflowAreas overflowAreas(bounds, bounds);

  nsLayoutUtils::UnionChildOverflow(this, overflowAreas);

  return FinishAndStoreOverflow(overflowAreas, GetSize());
}

// Per CSS 2.1, we map 'sub', 'super', 'text-top', 'text-bottom',
// length, percentage, and calc() values to 'baseline'.
PRUint8
nsTableCellFrame::GetVerticalAlign() const
{
  const nsStyleCoord& verticalAlign = GetStyleTextReset()->mVerticalAlign;
  if (verticalAlign.GetUnit() == eStyleUnit_Enumerated) {
    PRUint8 value = verticalAlign.GetIntValue();
    if (value == NS_STYLE_VERTICAL_ALIGN_TOP ||
        value == NS_STYLE_VERTICAL_ALIGN_MIDDLE ||
        value == NS_STYLE_VERTICAL_ALIGN_BOTTOM) {
      return value;
    }
  }
  return NS_STYLE_VERTICAL_ALIGN_BASELINE;
}

bool
nsTableCellFrame::CellHasVisibleContent(nscoord       height,
                                        nsTableFrame* tableFrame,
                                        nsIFrame*     kidFrame)
{
  // see  http://www.w3.org/TR/CSS21/tables.html#empty-cells
  if (height > 0)
    return true;
  if (tableFrame->IsBorderCollapse())
    return true;
  nsIFrame* innerFrame = kidFrame->GetFirstPrincipalChild();
  while(innerFrame) {
    nsIAtom* frameType = innerFrame->GetType();
    if (nsGkAtoms::textFrame == frameType) {
       nsTextFrame* textFrame = static_cast<nsTextFrame*>(innerFrame);
       if (textFrame->HasNoncollapsedCharacters())
         return true;
    }
    else if (nsGkAtoms::placeholderFrame != frameType) {
      return true;
    }
    else {
      nsIFrame *floatFrame = nsLayoutUtils::GetFloatFromPlaceholder(innerFrame);
      if (floatFrame)
        return true;
    }
    innerFrame = innerFrame->GetNextSibling();
  }	
  return false;
}

nscoord
nsTableCellFrame::GetCellBaseline() const
{
  // Ignore the position of the inner frame relative to the cell frame
  // since we want the position as though the inner were top-aligned.
  nsIFrame *inner = mFrames.FirstChild();
  nscoord borderPadding = GetUsedBorderAndPadding().top;
  nscoord result;
  if (nsLayoutUtils::GetFirstLineBaseline(inner, &result))
    return result + borderPadding;
  return inner->GetContentRect().YMost() - inner->GetPosition().y +
         borderPadding;
}

PRInt32 nsTableCellFrame::GetRowSpan()
{
  PRInt32 rowSpan=1;
  nsGenericHTMLElement *hc = nsGenericHTMLElement::FromContent(mContent);

  // Don't look at the content's rowspan if we're a pseudo cell
  if (hc && !GetStyleContext()->GetPseudo()) {
    const nsAttrValue* attr = hc->GetParsedAttr(nsGkAtoms::rowspan);
    // Note that we don't need to check the tag name, because only table cells
    // and table headers parse the "rowspan" attribute into an integer.
    if (attr && attr->Type() == nsAttrValue::eInteger) {
       rowSpan = attr->GetIntegerValue();
    }
  }
  return rowSpan;
}

PRInt32 nsTableCellFrame::GetColSpan()
{
  PRInt32 colSpan=1;
  nsGenericHTMLElement *hc = nsGenericHTMLElement::FromContent(mContent);

  // Don't look at the content's colspan if we're a pseudo cell
  if (hc && !GetStyleContext()->GetPseudo()) {
    const nsAttrValue* attr = hc->GetParsedAttr(nsGkAtoms::colspan);
    // Note that we don't need to check the tag name, because only table cells
    // and table headers parse the "colspan" attribute into an integer.
    if (attr && attr->Type() == nsAttrValue::eInteger) {
       colSpan = attr->GetIntegerValue();
    }
  }
  return colSpan;
}

/* virtual */ nscoord
nsTableCellFrame::GetMinWidth(nsRenderingContext *aRenderingContext)
{
  nscoord result = 0;
  DISPLAY_MIN_WIDTH(this, result);

  nsIFrame *inner = mFrames.FirstChild();
  result = nsLayoutUtils::IntrinsicForContainer(aRenderingContext, inner,
                                                    nsLayoutUtils::MIN_WIDTH);
  return result;
}

/* virtual */ nscoord
nsTableCellFrame::GetPrefWidth(nsRenderingContext *aRenderingContext)
{
  nscoord result = 0;
  DISPLAY_PREF_WIDTH(this, result);

  nsIFrame *inner = mFrames.FirstChild();
  result = nsLayoutUtils::IntrinsicForContainer(aRenderingContext, inner,
                                                nsLayoutUtils::PREF_WIDTH);
  return result;
}

/* virtual */ nsIFrame::IntrinsicWidthOffsetData
nsTableCellFrame::IntrinsicWidthOffsets(nsRenderingContext* aRenderingContext)
{
  IntrinsicWidthOffsetData result =
    nsContainerFrame::IntrinsicWidthOffsets(aRenderingContext);

  result.hMargin = 0;
  result.hPctMargin = 0;

  nsMargin border;
  GetBorderWidth(border);
  result.hBorder = border.LeftRight();

  return result;
}

#ifdef DEBUG
#define PROBABLY_TOO_LARGE 1000000
static
void DebugCheckChildSize(nsIFrame*            aChild,
                         nsHTMLReflowMetrics& aMet,
                         nsSize&              aAvailSize)
{
  if ((aMet.width < 0) || (aMet.width > PROBABLY_TOO_LARGE)) {
    printf("WARNING: cell content %p has large width %d \n",
           static_cast<void*>(aChild), PRInt32(aMet.width));
  }
}
#endif

// the computed height for the cell, which descendants use for percent height calculations
// it is the height (minus border, padding) of the cell's first in flow during its final
// reflow without an unconstrained height.
static nscoord
CalcUnpaginagedHeight(nsPresContext*        aPresContext,
                      nsTableCellFrame&     aCellFrame,
                      nsTableFrame&         aTableFrame,
                      nscoord               aVerticalBorderPadding)
{
  const nsTableCellFrame* firstCellInFlow   = (nsTableCellFrame*)aCellFrame.GetFirstInFlow();
  nsTableFrame*           firstTableInFlow  = (nsTableFrame*)aTableFrame.GetFirstInFlow();
  nsTableRowFrame*        row
    = static_cast<nsTableRowFrame*>(firstCellInFlow->GetParent());
  nsTableRowGroupFrame*   firstRGInFlow
    = static_cast<nsTableRowGroupFrame*>(row->GetParent());

  PRInt32 rowIndex;
  firstCellInFlow->GetRowIndex(rowIndex);
  PRInt32 rowSpan = aTableFrame.GetEffectiveRowSpan(*firstCellInFlow);
  nscoord cellSpacing = firstTableInFlow->GetCellSpacingX();

  nscoord computedHeight = ((rowSpan - 1) * cellSpacing) - aVerticalBorderPadding;
  PRInt32 rowX;
  for (row = firstRGInFlow->GetFirstRow(), rowX = 0; row; row = row->GetNextRow(), rowX++) {
    if (rowX > rowIndex + rowSpan - 1) {
      break;
    }
    else if (rowX >= rowIndex) {
      computedHeight += row->GetUnpaginatedHeight(aPresContext);
    }
  }
  return computedHeight;
}

NS_METHOD nsTableCellFrame::Reflow(nsPresContext*           aPresContext,
                                   nsHTMLReflowMetrics&     aDesiredSize,
                                   const nsHTMLReflowState& aReflowState,
                                   nsReflowStatus&          aStatus)
{
  DO_GLOBAL_REFLOW_COUNT("nsTableCellFrame");
  DISPLAY_REFLOW(aPresContext, this, aReflowState, aDesiredSize, aStatus);

  if (aReflowState.mFlags.mSpecialHeightReflow) {
    GetFirstInFlow()->AddStateBits(NS_TABLE_CELL_HAD_SPECIAL_REFLOW);
  }

  // see if a special height reflow needs to occur due to having a pct height
  nsTableFrame::CheckRequestSpecialHeightReflow(aReflowState);

  aStatus = NS_FRAME_COMPLETE;
  nsSize availSize(aReflowState.availableWidth, aReflowState.availableHeight);

  nsMargin borderPadding = aReflowState.mComputedPadding;
  nsMargin border;
  GetBorderWidth(border);
  borderPadding += border;

  nscoord topInset    = borderPadding.top;
  nscoord rightInset  = borderPadding.right;
  nscoord bottomInset = borderPadding.bottom;
  nscoord leftInset   = borderPadding.left;

  // reduce available space by insets, if we're in a constrained situation
  availSize.width -= leftInset + rightInset;
  if (NS_UNCONSTRAINEDSIZE != availSize.height)
    availSize.height -= topInset + bottomInset;

  // Try to reflow the child into the available space. It might not
  // fit or might need continuing.
  if (availSize.height < 0)
    availSize.height = 1;

  nsHTMLReflowMetrics kidSize(aDesiredSize.mFlags);
  kidSize.width = kidSize.height = 0;
  SetPriorAvailWidth(aReflowState.availableWidth);
  nsIFrame* firstKid = mFrames.FirstChild();
  NS_ASSERTION(firstKid, "Frame construction error, a table cell always has an inner cell frame");
  nsTableFrame* tableFrame = nsTableFrame::GetTableFrame(this);

  if (aReflowState.mFlags.mSpecialHeightReflow) {
    const_cast<nsHTMLReflowState&>(aReflowState).SetComputedHeight(mRect.height - topInset - bottomInset);
    DISPLAY_REFLOW_CHANGE();
  }
  else if (aPresContext->IsPaginated()) {
    nscoord computedUnpaginatedHeight =
      CalcUnpaginagedHeight(aPresContext, (nsTableCellFrame&)*this,
                            *tableFrame, topInset + bottomInset);
    if (computedUnpaginatedHeight > 0) {
      const_cast<nsHTMLReflowState&>(aReflowState).SetComputedHeight(computedUnpaginatedHeight);
      DISPLAY_REFLOW_CHANGE();
    }
  }
  else {
    SetHasPctOverHeight(false);
  }

  nsHTMLReflowState kidReflowState(aPresContext, aReflowState, firstKid,
                                   availSize);

  // Don't be a percent height observer if we're in the middle of
  // special-height reflow, in case we get an accidental NotifyPercentHeight()
  // call (which we shouldn't honor during special-height reflow)
  if (!aReflowState.mFlags.mSpecialHeightReflow) {
    // mPercentHeightObserver is for children of cells in quirks mode,
    // but only those than are tables in standards mode.  NeedsToObserve
    // will determine how far this is propagated to descendants.
    kidReflowState.mPercentHeightObserver = this;
  }
  // Don't propagate special height reflow state to our kids
  kidReflowState.mFlags.mSpecialHeightReflow = false;

  if (aReflowState.mFlags.mSpecialHeightReflow ||
      (GetFirstInFlow()->GetStateBits() & NS_TABLE_CELL_HAD_SPECIAL_REFLOW)) {
    // We need to force the kid to have mVResize set if we've had a
    // special reflow in the past, since the non-special reflow needs to
    // resize back to what it was without the special height reflow.
    kidReflowState.mFlags.mVResize = true;
  }

  nsPoint kidOrigin(leftInset, topInset);
  nsRect origRect = firstKid->GetRect();
  nsRect origVisualOverflow = firstKid->GetVisualOverflowRect();
  bool firstReflow = (firstKid->GetStateBits() & NS_FRAME_FIRST_REFLOW) != 0;

  ReflowChild(firstKid, aPresContext, kidSize, kidReflowState,
              kidOrigin.x, kidOrigin.y, NS_FRAME_INVALIDATE_ON_MOVE, aStatus);
  if (NS_FRAME_OVERFLOW_IS_INCOMPLETE(aStatus)) {
    // Don't pass OVERFLOW_INCOMPLETE through tables until they can actually handle it
    //XXX should paginate overflow as overflow, but not in this patch (bug 379349)
    NS_FRAME_SET_INCOMPLETE(aStatus);
    printf("Set table cell incomplete %p\n", static_cast<void*>(this));
  }

  // XXXbz is this invalidate actually needed, really?
  if (GetStateBits() & NS_FRAME_IS_DIRTY) {
    InvalidateFrameSubtree();
  }

#ifdef DEBUG
  DebugCheckChildSize(firstKid, kidSize, availSize);
#endif

  // 0 dimensioned cells need to be treated specially in Standard/NavQuirks mode
  // see testcase "emptyCells.html"
  nsIFrame* prevInFlow = GetPrevInFlow();
  bool isEmpty;
  if (prevInFlow) {
    isEmpty = static_cast<nsTableCellFrame*>(prevInFlow)->GetContentEmpty();
  } else {
    isEmpty = !CellHasVisibleContent(kidSize.height, tableFrame, firstKid);
  }
  SetContentEmpty(isEmpty);

  // Place the child
  FinishReflowChild(firstKid, aPresContext, &kidReflowState, kidSize,
                    kidOrigin.x, kidOrigin.y, 0);

  nsTableFrame::InvalidateFrame(firstKid, origRect, origVisualOverflow,
                                firstReflow);

  // first, compute the height which can be set w/o being restricted by aMaxSize.height
  nscoord cellHeight = kidSize.height;

  if (NS_UNCONSTRAINEDSIZE != cellHeight) {
    cellHeight += topInset + bottomInset;
  }

  // next determine the cell's width
  nscoord cellWidth = kidSize.width;      // at this point, we've factored in the cell's style attributes

  // factor in border and padding
  if (NS_UNCONSTRAINEDSIZE != cellWidth) {
    cellWidth += leftInset + rightInset;
  }

  // set the cell's desired size and max element size
  aDesiredSize.width   = cellWidth;
  aDesiredSize.height  = cellHeight;

  // the overflow area will be computed when the child will be vertically aligned

  if (aReflowState.mFlags.mSpecialHeightReflow) {
    if (aDesiredSize.height > mRect.height) {
      // set a bit indicating that the pct height contents exceeded
      // the height that they could honor in the pass 2 reflow
      SetHasPctOverHeight(true);
    }
    if (NS_UNCONSTRAINEDSIZE == aReflowState.availableHeight) {
      aDesiredSize.height = mRect.height;
    }
  }

  // If our parent is in initial reflow, it'll handle invalidating our
  // entire overflow rect.
  if (!(GetParent()->GetStateBits() & NS_FRAME_FIRST_REFLOW)) {
    CheckInvalidateSizeChange(aDesiredSize);
  }

  // remember the desired size for this reflow
  SetDesiredSize(aDesiredSize);

  NS_FRAME_SET_TRUNCATION(aStatus, aReflowState, aDesiredSize);
  return NS_OK;
}

/* ----- global methods ----- */

NS_QUERYFRAME_HEAD(nsTableCellFrame)
  NS_QUERYFRAME_ENTRY(nsTableCellFrame)
  NS_QUERYFRAME_ENTRY(nsITableCellLayout)
  NS_QUERYFRAME_ENTRY(nsIPercentHeightObserver)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

#ifdef ACCESSIBILITY
already_AddRefed<Accessible>
nsTableCellFrame::CreateAccessible()
{
  nsAccessibilityService* accService = nsIPresShell::AccService();
  if (accService) {
    return accService->CreateHTMLTableCellAccessible(mContent,
                                                     PresContext()->PresShell());
  }

  return nsnull;
}
#endif

/* This is primarily for editor access via nsITableLayout */
NS_IMETHODIMP
nsTableCellFrame::GetCellIndexes(PRInt32 &aRowIndex, PRInt32 &aColIndex)
{
  nsresult res = GetRowIndex(aRowIndex);
  if (NS_FAILED(res))
  {
    aColIndex = 0;
    return res;
  }
  aColIndex = mColIndex;
  return  NS_OK;
}

nsIFrame*
NS_NewTableCellFrame(nsIPresShell*   aPresShell,
                     nsStyleContext* aContext,
                     bool            aIsBorderCollapse)
{
  if (aIsBorderCollapse)
    return new (aPresShell) nsBCTableCellFrame(aContext);
  else
    return new (aPresShell) nsTableCellFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsBCTableCellFrame)

nsMargin*
nsTableCellFrame::GetBorderWidth(nsMargin&  aBorder) const
{
  aBorder = GetStyleBorder()->GetComputedBorder();
  return &aBorder;
}

nsIAtom*
nsTableCellFrame::GetType() const
{
  return nsGkAtoms::tableCellFrame;
}

#ifdef DEBUG
NS_IMETHODIMP
nsTableCellFrame::GetFrameName(nsAString& aResult) const
{
  return MakeFrameName(NS_LITERAL_STRING("TableCell"), aResult);
}
#endif

// nsBCTableCellFrame

nsBCTableCellFrame::nsBCTableCellFrame(nsStyleContext* aContext)
:nsTableCellFrame(aContext)
{
  mTopBorder = mRightBorder = mBottomBorder = mLeftBorder = 0;
}

nsBCTableCellFrame::~nsBCTableCellFrame()
{
}

nsIAtom*
nsBCTableCellFrame::GetType() const
{
  return nsGkAtoms::bcTableCellFrame;
}

/* virtual */ nsMargin
nsBCTableCellFrame::GetUsedBorder() const
{
  nsMargin result;
  GetBorderWidth(result);
  return result;
}

/* virtual */ bool
nsBCTableCellFrame::GetBorderRadii(nscoord aRadii[8]) const
{
  NS_FOR_CSS_HALF_CORNERS(corner) {
    aRadii[corner] = 0;
  }
  return false;
}

#ifdef DEBUG
NS_IMETHODIMP
nsBCTableCellFrame::GetFrameName(nsAString& aResult) const
{
  return MakeFrameName(NS_LITERAL_STRING("BCTableCell"), aResult);
}
#endif

nsMargin*
nsBCTableCellFrame::GetBorderWidth(nsMargin&  aBorder) const
{
  PRInt32 aPixelsToTwips = nsPresContext::AppUnitsPerCSSPixel();
  aBorder.top    = BC_BORDER_BOTTOM_HALF_COORD(aPixelsToTwips, mTopBorder);
  aBorder.right  = BC_BORDER_LEFT_HALF_COORD(aPixelsToTwips, mRightBorder);
  aBorder.bottom = BC_BORDER_TOP_HALF_COORD(aPixelsToTwips, mBottomBorder);
  aBorder.left   = BC_BORDER_RIGHT_HALF_COORD(aPixelsToTwips, mLeftBorder);
  return &aBorder;
}

BCPixelSize
nsBCTableCellFrame::GetBorderWidth(mozilla::css::Side aSide) const
{
  switch(aSide) {
  case NS_SIDE_TOP:
    return BC_BORDER_BOTTOM_HALF(mTopBorder);
  case NS_SIDE_RIGHT:
    return BC_BORDER_LEFT_HALF(mRightBorder);
  case NS_SIDE_BOTTOM:
    return BC_BORDER_TOP_HALF(mBottomBorder);
  default:
    return BC_BORDER_RIGHT_HALF(mLeftBorder);
  }
}

void
nsBCTableCellFrame::SetBorderWidth(mozilla::css::Side aSide,
                                   BCPixelSize aValue)
{
  switch(aSide) {
  case NS_SIDE_TOP:
    mTopBorder = aValue;
    break;
  case NS_SIDE_RIGHT:
    mRightBorder = aValue;
    break;
  case NS_SIDE_BOTTOM:
    mBottomBorder = aValue;
    break;
  default:
    mLeftBorder = aValue;
  }
}

/* virtual */ nsMargin
nsBCTableCellFrame::GetBorderOverflow()
{
  nsMargin halfBorder;
  PRInt32 p2t = nsPresContext::AppUnitsPerCSSPixel();
  halfBorder.top = BC_BORDER_TOP_HALF_COORD(p2t, mTopBorder);
  halfBorder.right = BC_BORDER_RIGHT_HALF_COORD(p2t, mRightBorder);
  halfBorder.bottom = BC_BORDER_BOTTOM_HALF_COORD(p2t, mBottomBorder);
  halfBorder.left = BC_BORDER_LEFT_HALF_COORD(p2t, mLeftBorder);
  return halfBorder;
}


void
nsBCTableCellFrame::PaintBackground(nsRenderingContext& aRenderingContext,
                                    const nsRect&        aDirtyRect,
                                    nsPoint              aPt,
                                    PRUint32             aFlags)
{
  // make border-width reflect the half of the border-collapse
  // assigned border that's inside the cell
  nsMargin borderWidth;
  GetBorderWidth(borderWidth);

  nsStyleBorder myBorder(*GetStyleBorder());
  // We're making an ephemeral stack copy here, so just copy this debug-only
  // member to prevent assertions.
#ifdef DEBUG
  myBorder.mImageTracked = GetStyleBorder()->mImageTracked;
#endif

  NS_FOR_CSS_SIDES(side) {
    myBorder.SetBorderWidth(side, borderWidth.Side(side));
  }

  nsRect rect(aPt, GetSize());
  // bypassing nsCSSRendering::PaintBackground is safe because this kind
  // of frame cannot be used for the root element
  nsCSSRendering::PaintBackgroundWithSC(PresContext(), aRenderingContext, this,
                                        aDirtyRect, rect,
                                        GetStyleContext(), myBorder,
                                        aFlags, nsnull);

#ifdef DEBUG
  myBorder.mImageTracked = false;
#endif
}
