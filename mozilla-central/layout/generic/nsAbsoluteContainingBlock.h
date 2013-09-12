/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * code for managing absolutely positioned children of a rendering
 * object that is a containing block for them
 */

#ifndef nsAbsoluteContainingBlock_h___
#define nsAbsoluteContainingBlock_h___

#include "nsFrameList.h"
#include "nsHTMLReflowState.h"
#include "nsGkAtoms.h"
#include "nsContainerFrame.h"

class nsIAtom;
class nsIFrame;
class nsPresContext;

/**
 * This class contains the logic for being an absolute containing block.  This
 * class is used within viewport frames (for frames representing content with
 * fixed position) and blocks (for frames representing absolutely positioned
 * content), since each set of frames is absolutely positioned with respect to
 * its parent.
 *
 * There is no principal child list, just a named child list which contains
 * the absolutely positioned frames (kAbsoluteList or kFixedList).
 *
 * All functions include as the first argument the frame that is delegating
 * the request.
 *
 */
class nsAbsoluteContainingBlock
{
public:
  typedef nsIFrame::ChildListID ChildListID;

  nsAbsoluteContainingBlock(ChildListID aChildListID)
#ifdef DEBUG
    : mChildListID(aChildListID)
#endif
  {
    NS_ASSERTION(mChildListID == nsIFrame::kAbsoluteList ||
                 mChildListID == nsIFrame::kFixedList,
                 "should either represent position:fixed or absolute content");
  }

#ifdef DEBUG
  ChildListID GetChildListID() const { return mChildListID; }
#endif

  const nsFrameList& GetChildList() const { return mAbsoluteFrames; }
  void AppendChildList(nsTArray<nsIFrame::ChildList>* aLists,
                       ChildListID aListID) const
  {
    NS_ASSERTION(aListID == GetChildListID(), "wrong list ID");
    GetChildList().AppendIfNonempty(aLists, aListID);
  }

  nsresult SetInitialChildList(nsIFrame*       aDelegatingFrame,
                               ChildListID     aListID,
                               nsFrameList&    aChildList);
  nsresult AppendFrames(nsIFrame*      aDelegatingFrame,
                        ChildListID    aListID,
                        nsFrameList&   aFrameList);
  nsresult InsertFrames(nsIFrame*      aDelegatingFrame,
                        ChildListID    aListID,
                        nsIFrame*      aPrevFrame,
                        nsFrameList&   aFrameList);
  void RemoveFrame(nsIFrame*      aDelegatingFrame,
                   ChildListID    aListID,
                   nsIFrame*      aOldFrame);

  // Called by the delegating frame after it has done its reflow first. This
  // function will reflow any absolutely positioned child frames that need to
  // be reflowed, e.g., because the absolutely positioned child frame has
  // 'auto' for an offset, or a percentage based width or height.
  // aOverflowAreas, if non-null, is unioned with (in the local
  // coordinate space) the overflow areas of the absolutely positioned
  // children.
  // @param aForceReflow if this is false, reflow for some absolutely
  //        positioned frames may be skipped based on whether they use
  //        placeholders for positioning and on whether the containing block
  //        width or height changed.
  nsresult Reflow(nsContainerFrame*        aDelegatingFrame,
                  nsPresContext*           aPresContext,
                  const nsHTMLReflowState& aReflowState,
                  nsReflowStatus&          aReflowStatus,
                  nscoord                  aContainingBlockWidth,
                  nscoord                  aContainingBlockHeight,
                  bool                     aConstrainHeight,
                  bool                     aCBWidthChanged,
                  bool                     aCBHeightChanged,
                  nsOverflowAreas*         aOverflowAreas);


  void DestroyFrames(nsIFrame* aDelegatingFrame,
                     nsIFrame* aDestructRoot);

  bool    HasAbsoluteFrames() {return mAbsoluteFrames.NotEmpty();}

  // Mark our size-dependent absolute frames with NS_FRAME_HAS_DIRTY_CHILDREN
  // so that we'll make sure to reflow them.
  void MarkSizeDependentFramesDirty();

  // Mark all our absolute frames with NS_FRAME_IS_DIRTY
  void MarkAllFramesDirty();

protected:
  // Returns true if the position of f depends on the position of
  // its placeholder or if the position or size of f depends on a
  // containing block dimension that changed.
  bool FrameDependsOnContainer(nsIFrame* f, bool aCBWidthChanged,
                                 bool aCBHeightChanged);

  nsresult ReflowAbsoluteFrame(nsIFrame*                aDelegatingFrame,
                               nsPresContext*          aPresContext,
                               const nsHTMLReflowState& aReflowState,
                               nscoord                  aContainingBlockWidth,
                               nscoord                  aContainingBlockHeight,
                               bool                     aConstrainHeight,
                               nsIFrame*                aKidFrame,
                               nsReflowStatus&          aStatus,
                               nsOverflowAreas*         aOverflowAreas);

  // Mark our absolute frames dirty.  If aMarkAllDirty is true, all will be
  // marked with NS_FRAME_IS_DIRTY.  Otherwise, the size-dependant ones will be
  // marked with NS_FRAME_HAS_DIRTY_CHILDREN.
  void DoMarkFramesDirty(bool aMarkAllDirty);

protected:
  nsFrameList mAbsoluteFrames;  // additional named child list

#ifdef DEBUG
  ChildListID const mChildListID; // kFixedList or kAbsoluteList

  // helper routine for debug printout
  void PrettyUC(nscoord aSize,
                char*   aBuf);
#endif
};

#endif /* nsnsAbsoluteContainingBlock_h___ */

