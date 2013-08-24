/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
// vim:cindent:ts=2:et:sw=2:
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Dan Rosen <dr@netscape.com>
 *   Mats Palmgren <matspal@gmail.com>
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

/*
 * construction of a frame tree that is nearly isomorphic to the content
 * tree and updating of that tree in response to dynamic changes
 */

#include "nsCSSFrameConstructor.h"
#include "nsCRT.h"
#include "nsIAtom.h"
#include "nsIURL.h"
#include "nsHashtable.h"
#include "nsIHTMLDocument.h"
#include "nsIStyleRule.h"
#include "nsIFrame.h"
#include "nsGkAtoms.h"
#include "nsPresContext.h"
#include "nsILinkHandler.h"
#include "nsIDocument.h"
#include "nsTableFrame.h"
#include "nsTableColGroupFrame.h"
#include "nsTableColFrame.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMHTMLTableColElement.h"
#include "nsIDOMHTMLTableCaptionElem.h"
#include "nsHTMLParts.h"
#include "nsIPresShell.h"
#include "nsUnicharUtils.h"
#include "nsStyleSet.h"
#include "nsIViewManager.h"
#include "nsIEventStateManager.h"
#include "nsStyleConsts.h"
#include "nsTableOuterFrame.h"
#include "nsIDOMXULElement.h"
#include "nsHTMLContainerFrame.h"
#include "nsINameSpaceManager.h"
#include "nsIDOMHTMLSelectElement.h"
#include "nsIDOMHTMLLegendElement.h"
#include "nsIComboboxControlFrame.h"
#include "nsIListControlFrame.h"
#include "nsISelectControlFrame.h"
#include "nsIDOMCharacterData.h"
#include "nsIDOMHTMLImageElement.h"
#include "nsPlaceholderFrame.h"
#include "nsTableRowGroupFrame.h"
#include "nsStyleChangeList.h"
#include "nsIFormControl.h"
#include "nsCSSAnonBoxes.h"
#include "nsIDeviceContext.h"
#include "nsTextFragment.h"
#include "nsIAnonymousContentCreator.h"
#include "nsFrameManager.h"
#include "nsLegendFrame.h"
#include "nsIContentIterator.h"
#include "nsBoxLayoutState.h"
#include "nsBindingManager.h"
#include "nsXBLBinding.h"
#include "nsITheme.h"
#include "nsContentCID.h"
#include "nsContentUtils.h"
#include "nsIScriptError.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsObjectFrame.h"
#include "nsRuleNode.h"
#include "nsIDOMMutationEvent.h"
#include "nsChildIterator.h"
#include "nsCSSRendering.h"
#include "nsISelectElement.h"
#include "nsLayoutErrors.h"
#include "nsLayoutUtils.h"
#include "nsAutoPtr.h"
#include "nsBoxFrame.h"
#include "nsIBoxLayout.h"
#include "nsImageFrame.h"
#include "nsIObjectLoadingContent.h"
#include "nsContentErrors.h"
#include "nsIPrincipal.h"
#include "nsIDOMWindowInternal.h"
#include "nsStyleUtil.h"
#include "nsBox.h"
#include "nsTArray.h"
#include "nsGenericDOMDataNode.h"
#include "mozilla/dom/Element.h"
#include "FrameLayerBuilder.h"

#ifdef MOZ_XUL
#include "nsIRootBox.h"
#include "nsIDOMXULCommandDispatcher.h"
#include "nsIDOMXULDocument.h"
#include "nsIXULDocument.h"
#endif
#ifdef ACCESSIBILITY
#include "nsIAccessibilityService.h"
#include "nsIAccessibleEvent.h"
#endif

#include "nsInlineFrame.h"
#include "nsBlockFrame.h"

#include "nsIScrollableFrame.h"

#include "nsIXBLService.h"

#undef NOISY_FIRST_LETTER

#ifdef MOZ_MATHML
#include "nsMathMLParts.h"
#endif
#ifdef MOZ_SVG
#include "nsSVGFeatures.h"
#include "nsSVGEffects.h"
#include "nsSVGUtils.h"
#include "nsSVGOuterSVGFrame.h"
#endif

#include "nsRefreshDriver.h"

using namespace mozilla;
using namespace mozilla::dom;

nsIFrame*
NS_NewHTMLCanvasFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

#if defined(MOZ_MEDIA)
nsIFrame*
NS_NewHTMLVideoFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);
#endif

#ifdef MOZ_SVG
#include "nsSVGTextContainerFrame.h"

PRBool
NS_SVGEnabled();
nsIFrame*
NS_NewSVGOuterSVGFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGInnerSVGFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGPathGeometryFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGGFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGGenericContainerFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGForeignObjectFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGAFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGGlyphFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGSwitchFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGTextFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGTSpanFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGContainerFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGUseFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
extern nsIFrame*
NS_NewSVGLinearGradientFrame(nsIPresShell *aPresShell, nsStyleContext* aContext);
extern nsIFrame*
NS_NewSVGRadialGradientFrame(nsIPresShell *aPresShell, nsStyleContext* aContext);
extern nsIFrame*
NS_NewSVGStopFrame(nsIPresShell *aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGMarkerFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
extern nsIFrame*
NS_NewSVGImageFrame(nsIPresShell *aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGClipPathFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGTextPathFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGFilterFrame(nsIPresShell *aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGPatternFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGMaskFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewSVGLeafFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
#endif

#include "nsIDocument.h"
#include "nsIDOMElement.h"
#include "nsIDOMNodeList.h"
#include "nsIDOMDocument.h"
#include "nsIDOMDocumentXBL.h"
#include "nsIScrollable.h"
#include "nsINodeInfo.h"
#include "prenv.h"
#include "nsWidgetsCID.h"
#include "nsNodeInfoManager.h"
#include "nsContentCreatorFunctions.h"
#include "nsIServiceManager.h"

// Global object maintenance
nsIXBLService * nsCSSFrameConstructor::gXBLService = nsnull;

#ifdef DEBUG
// Set the environment variable GECKO_FRAMECTOR_DEBUG_FLAGS to one or
// more of the following flags (comma separated) for handy debug
// output.
static PRBool gNoisyContentUpdates = PR_FALSE;
static PRBool gReallyNoisyContentUpdates = PR_FALSE;
static PRBool gNoisyInlineConstruction = PR_FALSE;

struct FrameCtorDebugFlags {
  const char* name;
  PRBool* on;
};

static FrameCtorDebugFlags gFlags[] = {
  { "content-updates",              &gNoisyContentUpdates },
  { "really-noisy-content-updates", &gReallyNoisyContentUpdates },
  { "noisy-inline",                 &gNoisyInlineConstruction }
};

#define NUM_DEBUG_FLAGS (sizeof(gFlags) / sizeof(gFlags[0]))
#endif


#ifdef MOZ_XUL
#include "nsMenuFrame.h"
#include "nsMenuPopupFrame.h"
#include "nsPopupSetFrame.h"
#include "nsTreeColFrame.h"
#include "nsIBoxObject.h"
#include "nsPIListBoxObject.h"
#include "nsListBoxBodyFrame.h"
#include "nsListItemFrame.h"
#include "nsXULLabelFrame.h"

//------------------------------------------------------------------

nsIFrame*
NS_NewAutoRepeatBoxFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewRootBoxFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewDocElementBoxFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewThumbFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewDeckFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewLeafBoxFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewStackFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewProgressMeterFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewImageBoxFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewTextBoxFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewGroupBoxFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewButtonBoxFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewSplitterFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewMenuPopupFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewPopupSetFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewMenuFrame (nsIPresShell* aPresShell, nsStyleContext* aContext, PRUint32 aFlags);

nsIFrame*
NS_NewMenuBarFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewTreeBodyFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

// grid
nsresult
NS_NewGridLayout2 ( nsIPresShell* aPresShell, nsIBoxLayout** aNewLayout );
nsIFrame*
NS_NewGridRowLeafFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);
nsIFrame*
NS_NewGridRowGroupFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

// end grid

nsIFrame*
NS_NewTitleBarFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewResizerFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);


#endif

nsIFrame*
NS_NewHTMLScrollFrame (nsIPresShell* aPresShell, nsStyleContext* aContext, PRBool aIsRoot);

nsIFrame*
NS_NewXULScrollFrame (nsIPresShell* aPresShell, nsStyleContext* aContext, PRBool aIsRoot);

nsIFrame*
NS_NewSliderFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewScrollbarFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);

nsIFrame*
NS_NewScrollbarButtonFrame (nsIPresShell* aPresShell, nsStyleContext* aContext);


#ifdef NOISY_FINDFRAME
static PRInt32 FFWC_totalCount=0;
static PRInt32 FFWC_doLoop=0;
static PRInt32 FFWC_doSibling=0;
static PRInt32 FFWC_recursions=0;
static PRInt32 FFWC_nextInFlows=0;
#endif

static inline nsIFrame*
GetFieldSetBlockFrame(nsIFrame* aFieldsetFrame)
{
  // Depends on the fieldset child frame order - see ConstructFieldSetFrame() below.
  nsIFrame* firstChild = aFieldsetFrame->GetFirstChild(nsnull);
  return firstChild && firstChild->GetNextSibling() ? firstChild->GetNextSibling() : firstChild;
}

#define FCDATA_DECL(_flags, _func) \
  { _flags, { (FrameCreationFunc)_func } }

//----------------------------------------------------------------------

static PRBool
IsInlineOutside(nsIFrame* aFrame)
{
  return aFrame->GetStyleDisplay()->IsInlineOutside();
}

/**
 * True if aFrame is an actual inline frame in the sense of non-replaced
 * display:inline CSS boxes.  In other words, it can be affected by {ib}
 * splitting and can contain first-letter frames.  Basically, this is either an
 * inline frame (positioned or otherwise) or an line frame (this last because
 * it can contain first-letter and because inserting blocks in the middle of it
 * needs to terminate it).
 */
static PRBool
IsInlineFrame(const nsIFrame* aFrame)
{
  return aFrame->IsFrameOfType(nsIFrame::eLineParticipant);
}

/**
 * If any children require a block parent, return the first such child.
 * Otherwise return null.
 */
static nsIContent*
AnyKidsNeedBlockParent(nsIFrame *aFrameList)
{
  for (nsIFrame *k = aFrameList; k; k = k->GetNextSibling()) {
    // Line participants, such as text and inline frames, can't be
    // directly inside a XUL box; they must be wrapped in an
    // intermediate block.
    if (k->IsFrameOfType(nsIFrame::eLineParticipant)) {
      return k->GetContent();
    }
  }
  return nsnull;
}

// Reparent a frame into a wrapper frame that is a child of its old parent.
static void
ReparentFrame(nsFrameManager* aFrameManager,
              nsIFrame* aNewParentFrame,
              nsIFrame* aFrame)
{
  aFrame->SetParent(aNewParentFrame);
  aFrameManager->ReparentStyleContext(aFrame);
  if (aFrame->GetStateBits() &
      (NS_FRAME_HAS_VIEW | NS_FRAME_HAS_CHILD_WITH_VIEW)) {
    // No need to walk up the tree, since the bits are already set
    // right on the parent of aNewParentFrame.
    NS_ASSERTION(aNewParentFrame->GetParent()->GetStateBits() &
                   NS_FRAME_HAS_CHILD_WITH_VIEW,
                 "aNewParentFrame's parent should have this bit set!");
    aNewParentFrame->AddStateBits(NS_FRAME_HAS_CHILD_WITH_VIEW);
  }
}

static void
ReparentFrames(nsFrameManager* aFrameManager,
               nsIFrame* aNewParentFrame,
               const nsFrameList& aFrameList)
{
  for (nsFrameList::Enumerator e(aFrameList); !e.AtEnd(); e.Next()) {
    ReparentFrame(aFrameManager, aNewParentFrame, e.get());
  }
}

//----------------------------------------------------------------------
//
// When inline frames get weird and have block frames in them, we
// annotate them to help us respond to incremental content changes
// more easily.

static inline PRBool
IsFrameSpecial(nsIFrame* aFrame)
{
  return (aFrame->GetStateBits() & NS_FRAME_IS_SPECIAL) != 0;
}

static nsIFrame* GetSpecialSibling(nsIFrame* aFrame)
{
  NS_PRECONDITION(IsFrameSpecial(aFrame), "Shouldn't call this");

  // We only store the "special sibling" annotation with the first
  // frame in the continuation chain. Walk back to find that frame now.
  return static_cast<nsIFrame*>
    (aFrame->GetFirstContinuation()->
       Properties().Get(nsIFrame::IBSplitSpecialSibling()));
}

static nsIFrame* GetSpecialPrevSibling(nsIFrame* aFrame)
{
  NS_PRECONDITION(IsFrameSpecial(aFrame), "Shouldn't call this");
  
  // We only store the "special sibling" annotation with the first
  // frame in the continuation chain. Walk back to find that frame now.  
  return static_cast<nsIFrame*>
    (aFrame->GetFirstContinuation()->
       Properties().Get(nsIFrame::IBSplitSpecialPrevSibling()));
}

static nsIFrame*
GetLastSpecialSibling(nsIFrame* aFrame, PRBool aReturnEmptyTrailingInline)
{
  for (nsIFrame *frame = aFrame, *next; ; frame = next) {
    next = GetSpecialSibling(frame);
    if (!next ||
        (!aReturnEmptyTrailingInline && !next->GetFirstChild(nsnull) &&
         !GetSpecialSibling(next))) {
      NS_ASSERTION(!next || !IsInlineOutside(frame),
                   "Should have a block here!");
      return frame;
    }
  }
  NS_NOTREACHED("unreachable code");
  return nsnull;
}

static void
SetFrameIsSpecial(nsIFrame* aFrame, nsIFrame* aSpecialSibling)
{
  NS_PRECONDITION(aFrame, "bad args!");

  // We should be the only continuation
  NS_ASSERTION(!aFrame->GetPrevContinuation(),
               "assigning special sibling to other than first continuation!");
  NS_ASSERTION(!aFrame->GetNextContinuation() ||
               IsFrameSpecial(aFrame->GetNextContinuation()),
               "should have no non-special continuations here");

  // Mark the frame as "special".
  aFrame->AddStateBits(NS_FRAME_IS_SPECIAL);

  if (aSpecialSibling) {
    NS_ASSERTION(!aSpecialSibling->GetPrevContinuation(),
                 "assigning something other than the first continuation as the "
                 "special sibling");

    // Store the "special sibling" (if we were given one) with the
    // first frame in the flow.
    FramePropertyTable* props = aFrame->PresContext()->PropertyTable();
    props->Set(aFrame, nsIFrame::IBSplitSpecialSibling(), aSpecialSibling);
    props->Set(aSpecialSibling, nsIFrame::IBSplitSpecialPrevSibling(), aFrame);
  }
}

static nsIFrame*
GetIBContainingBlockFor(nsIFrame* aFrame)
{
  NS_PRECONDITION(IsFrameSpecial(aFrame),
                  "GetIBContainingBlockFor() should only be called on known IB frames");

  // Get the first "normal" ancestor of the target frame.
  nsIFrame* parentFrame;
  do {
    parentFrame = aFrame->GetParent();

    if (! parentFrame) {
      NS_ERROR("no unsplit block frame in IB hierarchy");
      return aFrame;
    }

    // Note that we ignore non-special frames which have a pseudo on their
    // style context -- they're not the frames we're looking for!  In
    // particular, they may be hiding a real parent that _is_ special.
    if (!IsFrameSpecial(parentFrame) &&
        !parentFrame->GetStyleContext()->GetPseudo())
      break;

    aFrame = parentFrame;
  } while (1);
 
  // post-conditions
  NS_ASSERTION(parentFrame, "no normal ancestor found for special frame in GetIBContainingBlockFor");
  NS_ASSERTION(parentFrame != aFrame, "parentFrame is actually the child frame - bogus reslt");

  return parentFrame;
}

//----------------------------------------------------------------------

// Block/inline frame construction logic. We maintain a few invariants here:
//
// 1. Block frames contain block and inline frames.
//
// 2. Inline frames only contain inline frames. If an inline parent has a block
// child then the block child is migrated upward until it lands in a block
// parent (the inline frames containing block is where it will end up).

// After this function returns, aLink is pointing to the first link at or
// after its starting position for which the next frame is a block.  If there
// is no such link, it points to the end of the list.
static void
FindFirstBlock(nsFrameList::FrameLinkEnumerator& aLink)
{
  for ( ; !aLink.AtEnd(); aLink.Next()) {
    if (!IsInlineOutside(aLink.NextFrame())) {
      return;
    }
  }
}

// This function returns a frame link enumerator pointing to the first link in
// the list for which the next frame is not block.  If there is no such link,
// it points to the end of the list.
static nsFrameList::FrameLinkEnumerator
FindFirstNonBlock(const nsFrameList& aList)
{
  nsFrameList::FrameLinkEnumerator link(aList);
  for (; !link.AtEnd(); link.Next()) {
    if (IsInlineOutside(link.NextFrame())) {
      break;
    }
  }
  return link;
}

inline void
SetInitialSingleChild(nsIFrame* aParent, nsIFrame* aFrame)
{
  NS_PRECONDITION(!aFrame->GetNextSibling(), "Should be using a frame list");
  nsFrameList temp(aFrame, aFrame);
  aParent->SetInitialChildList(nsnull, temp);
}

// -----------------------------------------------------------

// Structure used when constructing formatting object trees.
struct nsFrameItems : public nsFrameList
{
  // Appends the frame to the end of the list
  void AddChild(nsIFrame* aChild);
};

void 
nsFrameItems::AddChild(nsIFrame* aChild)
{
  NS_PRECONDITION(aChild, "nsFrameItems::AddChild");

  // It'd be really nice if we could just AppendFrames(nsnull, aChild) here,
  // but some of our callers put frames that have different
  // parents (caption, I'm looking at you) on the same framelist, and
  // nsFrameList asserts if you try to do that.
  if (IsEmpty()) {
    SetFrames(aChild);
  }
  else {
    NS_ASSERTION(aChild != mLastChild,
                 "Same frame being added to frame list twice?");
    mLastChild->SetNextSibling(aChild);
    mLastChild = nsLayoutUtils::GetLastSibling(aChild);
  }
}

// -----------------------------------------------------------

// Structure used when constructing formatting object trees. Contains
// state information needed for absolutely positioned elements
struct nsAbsoluteItems : nsFrameItems {
  // containing block for absolutely positioned elements
  nsIFrame* containingBlock;
  
  nsAbsoluteItems(nsIFrame* aContainingBlock);
#ifdef DEBUG
  // XXXbz Does this need a debug-only assignment operator that nulls out the
  // childList in the nsAbsoluteItems we're copying?  Introducing a difference
  // between debug and non-debug behavior seems bad, so I guess not...
  ~nsAbsoluteItems() {
    NS_ASSERTION(!FirstChild(),
                 "Dangling child list.  Someone forgot to insert it?");
  }
#endif
  
  // Appends the frame to the end of the list
  void AddChild(nsIFrame* aChild);
};

nsAbsoluteItems::nsAbsoluteItems(nsIFrame* aContainingBlock)
  : containingBlock(aContainingBlock)
{
}

// Additional behavior is that it sets the frame's NS_FRAME_OUT_OF_FLOW flag
void
nsAbsoluteItems::AddChild(nsIFrame* aChild)
{
  NS_ASSERTION(aChild->PresContext()->FrameManager()->
               GetPlaceholderFrameFor(aChild),
               "Child without placeholder being added to nsAbsoluteItems?");
  aChild->AddStateBits(NS_FRAME_OUT_OF_FLOW);
  nsFrameItems::AddChild(aChild);
}

// -----------------------------------------------------------

// Structure for saving the existing state when pushing/poping containing
// blocks. The destructor restores the state to its previous state
class NS_STACK_CLASS nsFrameConstructorSaveState {
public:
  nsFrameConstructorSaveState();
  ~nsFrameConstructorSaveState();

private:
  nsAbsoluteItems* mItems;                // pointer to struct whose data we save/restore
  PRPackedBool*    mFixedPosIsAbsPos;

  nsAbsoluteItems  mSavedItems;           // copy of original data
  PRPackedBool     mSavedFixedPosIsAbsPos;

  // The name of the child list in which our frames would belong
  nsIAtom* mChildListName;
  nsFrameConstructorState* mState;

  friend class nsFrameConstructorState;
};

// Structure used to keep track of a list of bindings we need to call
// AddToAttachedQueue on.  These should be in post-order depth-first
// flattened tree traversal order.
struct PendingBinding : public PRCList
{
#ifdef NS_BUILD_REFCNT_LOGGING
  PendingBinding() {
    MOZ_COUNT_CTOR(PendingBinding);
  }
  ~PendingBinding() {
    MOZ_COUNT_DTOR(PendingBinding);
  }
#endif

  nsRefPtr<nsXBLBinding> mBinding;
};

// Structure used for maintaining state information during the
// frame construction process
class NS_STACK_CLASS nsFrameConstructorState {
public:
  nsPresContext            *mPresContext;
  nsIPresShell             *mPresShell;
  nsFrameManager           *mFrameManager;

#ifdef MOZ_XUL
  // Frames destined for the nsGkAtoms::popupList.
  nsAbsoluteItems           mPopupItems;
#endif

  // Containing block information for out-of-flow frames.
  nsAbsoluteItems           mFixedItems;
  nsAbsoluteItems           mAbsoluteItems;
  nsAbsoluteItems           mFloatedItems;

  nsCOMPtr<nsILayoutHistoryState> mFrameState;
  // These bits will be added to the state bits of any frame we construct
  // using this state.
  nsFrameState              mAdditionalStateBits;

  // When working with the -moz-transform property, we want to hook
  // the abs-pos and fixed-pos lists together, since transformed
  // elements are fixed-pos containing blocks.  This flag determines
  // whether or not we want to wire the fixed-pos and abs-pos lists
  // together.
  PRPackedBool              mFixedPosIsAbsPos;

  // A boolean to indicate whether we have a "pending" popupgroup.  That is, we
  // have already created the FrameConstructionItem for the root popupgroup but
  // we have not yet created the relevant frame.
  PRPackedBool              mHavePendingPopupgroup;

  // If true (which is the default) then call SetPrimaryFrame() as needed
  // during frame construction.  If false, don't make any SetPrimaryFrame()
  // calls.  The mSetPrimaryFrames == PR_FALSE mode is meant to be used for
  // construction of random "extra" frames for elements via normal frame
  // construction APIs (e.g. replication of things across pages in paginated
  // mode).
  PRPackedBool              mSetPrimaryFrames;

  nsCOMArray<nsIContent>    mGeneratedTextNodesWithInitializer;

  // Constructor
  // Use the passed-in history state.
  nsFrameConstructorState(nsIPresShell*          aPresShell,
                          nsIFrame*              aFixedContainingBlock,
                          nsIFrame*              aAbsoluteContainingBlock,
                          nsIFrame*              aFloatContainingBlock,
                          nsILayoutHistoryState* aHistoryState);
  // Get the history state from the pres context's pres shell.
  nsFrameConstructorState(nsIPresShell*          aPresShell,
                          nsIFrame*              aFixedContainingBlock,
                          nsIFrame*              aAbsoluteContainingBlock,
                          nsIFrame*              aFloatContainingBlock);

  ~nsFrameConstructorState();
  
  // Function to push the existing absolute containing block state and
  // create a new scope. Code that uses this function should get matching
  // logic in GetAbsoluteContainingBlock.
  void PushAbsoluteContainingBlock(nsIFrame* aNewAbsoluteContainingBlock,
                                   nsFrameConstructorSaveState& aSaveState);

  // Function to push the existing float containing block state and
  // create a new scope. Code that uses this function should get matching
  // logic in GetFloatContainingBlock.
  // Pushing a null float containing block forbids any frames from being
  // floated until a new float containing block is pushed.
  // XXX we should get rid of null float containing blocks and teach the
  // various frame classes to deal with floats instead.
  void PushFloatContainingBlock(nsIFrame* aNewFloatContainingBlock,
                                nsFrameConstructorSaveState& aSaveState);

  // Function to return the proper geometric parent for a frame with display
  // struct given by aStyleDisplay and parent's frame given by
  // aContentParentFrame.
  nsIFrame* GetGeometricParent(const nsStyleDisplay* aStyleDisplay,
                               nsIFrame* aContentParentFrame);

  /**
   * Function to add a new frame to the right frame list.  This MUST be called
   * on frames before their children have been processed if the frames might
   * conceivably be out-of-flow; otherwise cleanup in error cases won't work
   * right.  Also, this MUST be called on frames after they have been
   * initialized.
   * @param aNewFrame the frame to add
   * @param aFrameItems the list to add in-flow frames to
   * @param aContent the content pointer for aNewFrame
   * @param aStyleContext the style context resolved for aContent
   * @param aParentFrame the parent frame for the content if it were in-flow
   * @param aCanBePositioned pass false if the frame isn't allowed to be
   *        positioned
   * @param aCanBeFloated pass false if the frame isn't allowed to be
   *        floated
   * @param aIsOutOfFlowPopup pass true if the frame is an out-of-flow popup
   *        (XUL-only)
   * @throws NS_ERROR_OUT_OF_MEMORY if it happens.
   * @note If this method throws, that means that aNewFrame was not inserted
   *       into any frame lists.  Furthermore, this method will handle cleanup
   *       of aNewFrame (via calling Destroy() on it).
   */
  nsresult AddChild(nsIFrame* aNewFrame,
                    nsFrameItems& aFrameItems,
                    nsIContent* aContent,
                    nsStyleContext* aStyleContext,
                    nsIFrame* aParentFrame,
                    PRBool aCanBePositioned = PR_TRUE,
                    PRBool aCanBeFloated = PR_TRUE,
                    PRBool aIsOutOfFlowPopup = PR_FALSE,
                    PRBool aInsertAfter = PR_FALSE,
                    nsIFrame* aInsertAfterFrame = nsnull);

  /**
   * Function to return the fixed-pos element list.  Normally this will just hand back the
   * fixed-pos element list, but in case we're dealing with a transformed element that's
   * acting as an abs-pos and fixed-pos container, we'll hand back the abs-pos list.  Callers should
   * use this function if they want to get the list acting as the fixed-pos item parent.
   */
  nsAbsoluteItems& GetFixedItems()
  {
    return mFixedPosIsAbsPos ? mAbsoluteItems : mFixedItems;
  }
  const nsAbsoluteItems& GetFixedItems() const
  {
    return mFixedPosIsAbsPos ? mAbsoluteItems : mFixedItems;
  }


  /**
   * class to automatically push and pop a pending binding in the frame
   * constructor state.  See nsCSSFrameConstructor::FrameConstructionItem
   * mPendingBinding documentation.
   */
  class PendingBindingAutoPusher;
  friend class PendingBindingAutoPusher;
  class NS_STACK_CLASS PendingBindingAutoPusher {
  public:
    PendingBindingAutoPusher(nsFrameConstructorState& aState,
                             PendingBinding* aPendingBinding) :
      mState(aState),
      mPendingBinding(aState.mCurrentPendingBindingInsertionPoint)
        {
          NS_PRECONDITION(mPendingBinding, "how did that happen?");
          if (aPendingBinding) {
            aState.mCurrentPendingBindingInsertionPoint = aPendingBinding;
          }
        }

    ~PendingBindingAutoPusher()
      {
        mState.mCurrentPendingBindingInsertionPoint = mPendingBinding;
      }

  private:
    nsFrameConstructorState& mState;
    PRCList* mPendingBinding;
  };

  /**
   * Add a new pending binding to the list
   */
  void AddPendingBinding(PendingBinding* aPendingBinding) {
    PR_INSERT_BEFORE(aPendingBinding, mCurrentPendingBindingInsertionPoint);
  }

protected:
  friend class nsFrameConstructorSaveState;

  /**
   * ProcessFrameInsertions takes the frames in aFrameItems and adds them as
   * kids to the aChildListName child list of |aFrameItems.containingBlock|.
   */
  void ProcessFrameInsertions(nsAbsoluteItems& aFrameItems,
                              nsIAtom* aChildListName);

  // Our list of all pending bindings.  When we're done, we need to call
  // AddToAttachedQueue on all of them, in order.
  PRCList mPendingBindings;

  PRCList* mCurrentPendingBindingInsertionPoint;
};

nsFrameConstructorState::nsFrameConstructorState(nsIPresShell*          aPresShell,
                                                 nsIFrame*              aFixedContainingBlock,
                                                 nsIFrame*              aAbsoluteContainingBlock,
                                                 nsIFrame*              aFloatContainingBlock,
                                                 nsILayoutHistoryState* aHistoryState)
  : mPresContext(aPresShell->GetPresContext()),
    mPresShell(aPresShell),
    mFrameManager(aPresShell->FrameManager()),
#ifdef MOZ_XUL    
    mPopupItems(nsnull),
#endif
    mFixedItems(aFixedContainingBlock),
    mAbsoluteItems(aAbsoluteContainingBlock),
    mFloatedItems(aFloatContainingBlock),
    // See PushAbsoluteContaningBlock below
    mFrameState(aHistoryState),
    mAdditionalStateBits(0),
    mFixedPosIsAbsPos(aAbsoluteContainingBlock &&
                      aAbsoluteContainingBlock->GetStyleDisplay()->
                        HasTransform()),
    mHavePendingPopupgroup(PR_FALSE),
    mSetPrimaryFrames(PR_TRUE),
    mCurrentPendingBindingInsertionPoint(&mPendingBindings)
{
#ifdef MOZ_XUL
  nsIRootBox* rootBox = nsIRootBox::GetRootBox(aPresShell);
  if (rootBox) {
    mPopupItems.containingBlock = rootBox->GetPopupSetFrame();
  }
#endif
  MOZ_COUNT_CTOR(nsFrameConstructorState);
  PR_INIT_CLIST(&mPendingBindings);
}

nsFrameConstructorState::nsFrameConstructorState(nsIPresShell* aPresShell,
                                                 nsIFrame*     aFixedContainingBlock,
                                                 nsIFrame*     aAbsoluteContainingBlock,
                                                 nsIFrame*     aFloatContainingBlock)
  : mPresContext(aPresShell->GetPresContext()),
    mPresShell(aPresShell),
    mFrameManager(aPresShell->FrameManager()),
#ifdef MOZ_XUL    
    mPopupItems(nsnull),
#endif
    mFixedItems(aFixedContainingBlock),
    mAbsoluteItems(aAbsoluteContainingBlock),
    mFloatedItems(aFloatContainingBlock),
    // See PushAbsoluteContaningBlock below
    mAdditionalStateBits(0),
    mFixedPosIsAbsPos(aAbsoluteContainingBlock &&
                      aAbsoluteContainingBlock->GetStyleDisplay()->
                        HasTransform()),
    mHavePendingPopupgroup(PR_FALSE),
    mSetPrimaryFrames(PR_TRUE),
    mCurrentPendingBindingInsertionPoint(&mPendingBindings)
{
#ifdef MOZ_XUL
  nsIRootBox* rootBox = nsIRootBox::GetRootBox(aPresShell);
  if (rootBox) {
    mPopupItems.containingBlock = rootBox->GetPopupSetFrame();
  }
#endif
  MOZ_COUNT_CTOR(nsFrameConstructorState);
  mFrameState = aPresShell->GetDocument()->GetLayoutHistoryState();
  PR_INIT_CLIST(&mPendingBindings);
}

nsFrameConstructorState::~nsFrameConstructorState()
{
  // Frame order comparison functions only work properly when the placeholders
  // have been inserted into the frame tree. So for example if we have a new float
  // containing the placeholder for a new abs-pos frame, and we process the abs-pos
  // insertion first, then we won't be able to find the right place to insert in
  // in the abs-pos list. So put floats in first, because they can contain placeholders
  // for abs-pos and fixed-pos items whose containing blocks are outside the floats.
  // Then put abs-pos frames in, because they can contain placeholders for fixed-pos
  // items whose containing block is outside the abs-pos frames. 
  MOZ_COUNT_DTOR(nsFrameConstructorState);
  ProcessFrameInsertions(mFloatedItems, nsGkAtoms::floatList);
  ProcessFrameInsertions(mAbsoluteItems, nsGkAtoms::absoluteList);
  ProcessFrameInsertions(mFixedItems, nsGkAtoms::fixedList);
#ifdef MOZ_XUL
  ProcessFrameInsertions(mPopupItems, nsGkAtoms::popupList);
#endif
  for (PRInt32 i = mGeneratedTextNodesWithInitializer.Count() - 1; i >= 0; --i) {
    mGeneratedTextNodesWithInitializer[i]->
      DeleteProperty(nsGkAtoms::genConInitializerProperty);
  }
  if (!PR_CLIST_IS_EMPTY(&mPendingBindings)) {
    nsBindingManager* bindingManager = mPresShell->GetDocument()->BindingManager();
    do {
      PendingBinding* pendingBinding =
        static_cast<PendingBinding*>(PR_NEXT_LINK(&mPendingBindings));
      PR_REMOVE_LINK(pendingBinding);
      bindingManager->AddToAttachedQueue(pendingBinding->mBinding);
      delete pendingBinding;
    } while (!PR_CLIST_IS_EMPTY(&mPendingBindings));
  }
}

static nsIFrame*
AdjustAbsoluteContainingBlock(nsIFrame* aContainingBlockIn)
{
  if (!aContainingBlockIn) {
    return nsnull;
  }
  
  // Always use the container's first continuation. (Inline frames can have
  // non-fluid bidi continuations...)
  return aContainingBlockIn->GetFirstContinuation();
}

void
nsFrameConstructorState::PushAbsoluteContainingBlock(nsIFrame* aNewAbsoluteContainingBlock,
                                                     nsFrameConstructorSaveState& aSaveState)
{
  aSaveState.mItems = &mAbsoluteItems;
  aSaveState.mSavedItems = mAbsoluteItems;
  aSaveState.mChildListName = nsGkAtoms::absoluteList;
  aSaveState.mState = this;

  /* Store whether we're wiring the abs-pos and fixed-pos lists together. */
  aSaveState.mFixedPosIsAbsPos = &mFixedPosIsAbsPos;
  aSaveState.mSavedFixedPosIsAbsPos = mFixedPosIsAbsPos;

  mAbsoluteItems = 
    nsAbsoluteItems(AdjustAbsoluteContainingBlock(aNewAbsoluteContainingBlock));

  /* See if we're wiring the fixed-pos and abs-pos lists together.  This happens iff
   * we're a transformed element.
   */
  mFixedPosIsAbsPos = (aNewAbsoluteContainingBlock &&
                       aNewAbsoluteContainingBlock->GetStyleDisplay()->HasTransform());
}

void
nsFrameConstructorState::PushFloatContainingBlock(nsIFrame* aNewFloatContainingBlock,
                                                  nsFrameConstructorSaveState& aSaveState)
{
  NS_PRECONDITION(!aNewFloatContainingBlock ||
                  aNewFloatContainingBlock->IsFloatContainingBlock(),
                  "Please push a real float containing block!");
  aSaveState.mItems = &mFloatedItems;
  aSaveState.mSavedItems = mFloatedItems;
  aSaveState.mChildListName = nsGkAtoms::floatList;
  aSaveState.mState = this;
  mFloatedItems = nsAbsoluteItems(aNewFloatContainingBlock);
}

nsIFrame*
nsFrameConstructorState::GetGeometricParent(const nsStyleDisplay* aStyleDisplay,
                                            nsIFrame* aContentParentFrame)
{
  NS_PRECONDITION(aStyleDisplay, "Must have display struct!");

  // If there is no container for a fixed, absolute, or floating root
  // frame, we will ignore the positioning.  This hack is originally
  // brought to you by the letter T: tables, since other roots don't
  // even call into this code.  See bug 178855.
  //
  // XXX Disabling positioning in this case is a hack.  If one was so inclined,
  // one could support this either by (1) inserting a dummy block between the
  // table and the canvas or (2) teaching the canvas how to reflow positioned
  // elements. (1) has the usual problems when multiple frames share the same
  // content (notice all the special cases in this file dealing with inner
  // tables and outer tables which share the same content). (2) requires some
  // work and possible factoring.
  //
  // XXXbz couldn't we just force position to "static" on roots and
  // float to "none"?  That's OK per CSS 2.1, as far as I can tell.
  
  if (aStyleDisplay->IsFloating() && mFloatedItems.containingBlock) {
    NS_ASSERTION(!aStyleDisplay->IsAbsolutelyPositioned(),
                 "Absolutely positioned _and_ floating?");
    return mFloatedItems.containingBlock;
  }

  if (aStyleDisplay->mPosition == NS_STYLE_POSITION_ABSOLUTE &&
      mAbsoluteItems.containingBlock) {
    return mAbsoluteItems.containingBlock;
  }

  if (aStyleDisplay->mPosition == NS_STYLE_POSITION_FIXED &&
      GetFixedItems().containingBlock) {
    return GetFixedItems().containingBlock;
  }

  return aContentParentFrame;
}

nsresult
nsFrameConstructorState::AddChild(nsIFrame* aNewFrame,
                                  nsFrameItems& aFrameItems,
                                  nsIContent* aContent,
                                  nsStyleContext* aStyleContext,
                                  nsIFrame* aParentFrame,
                                  PRBool aCanBePositioned,
                                  PRBool aCanBeFloated,
                                  PRBool aIsOutOfFlowPopup,
                                  PRBool aInsertAfter,
                                  nsIFrame* aInsertAfterFrame)
{
  NS_PRECONDITION(!aNewFrame->GetNextSibling(), "Shouldn't happen");
  
  const nsStyleDisplay* disp = aNewFrame->GetStyleDisplay();
  
  // The comments in GetGeometricParent regarding root table frames
  // all apply here, unfortunately.

  PRBool needPlaceholder = PR_FALSE;
  nsFrameState placeholderType;
  nsFrameItems* frameItems = &aFrameItems;
#ifdef MOZ_XUL
  if (NS_UNLIKELY(aIsOutOfFlowPopup)) {
      NS_ASSERTION(aNewFrame->GetParent() == mPopupItems.containingBlock,
                   "Popup whose parent is not the popup containing block?");
      NS_ASSERTION(mPopupItems.containingBlock, "Must have a popup set frame!");
      needPlaceholder = PR_TRUE;
      frameItems = &mPopupItems;
      placeholderType = PLACEHOLDER_FOR_POPUP;
  }
  else
#endif // MOZ_XUL
  if (aCanBeFloated && disp->IsFloating() &&
      mFloatedItems.containingBlock) {
    NS_ASSERTION(aNewFrame->GetParent() == mFloatedItems.containingBlock,
                 "Float whose parent is not the float containing block?");
    needPlaceholder = PR_TRUE;
    frameItems = &mFloatedItems;
    placeholderType = PLACEHOLDER_FOR_FLOAT;
  }
  else if (aCanBePositioned) {
    if (disp->mPosition == NS_STYLE_POSITION_ABSOLUTE &&
        mAbsoluteItems.containingBlock) {
      NS_ASSERTION(aNewFrame->GetParent() == mAbsoluteItems.containingBlock,
                   "Abs pos whose parent is not the abs pos containing block?");
      needPlaceholder = PR_TRUE;
      frameItems = &mAbsoluteItems;
      placeholderType = PLACEHOLDER_FOR_ABSPOS;
    }
    if (disp->mPosition == NS_STYLE_POSITION_FIXED &&
        GetFixedItems().containingBlock) {
      NS_ASSERTION(aNewFrame->GetParent() == GetFixedItems().containingBlock,
                   "Fixed pos whose parent is not the fixed pos containing block?");
      needPlaceholder = PR_TRUE;
      frameItems = &GetFixedItems();
      placeholderType = PLACEHOLDER_FOR_FIXEDPOS;
    }
  }

  if (needPlaceholder) {
    NS_ASSERTION(frameItems != &aFrameItems,
                 "Putting frame in-flow _and_ want a placeholder?");
    nsIFrame* placeholderFrame;
    nsresult rv =
      nsCSSFrameConstructor::CreatePlaceholderFrameFor(mPresShell,
                                                       aContent,
                                                       aNewFrame,
                                                       aStyleContext,
                                                       aParentFrame,
                                                       nsnull,
                                                       placeholderType,
                                                       &placeholderFrame);
    if (NS_FAILED(rv)) {
      // Note that aNewFrame could be the top frame for a scrollframe setup,
      // hence already set as the primary frame.  So we have to clean up here.
      // But it shouldn't have any out-of-flow kids.
      // XXXbz Maybe add a utility function to assert that?
      aNewFrame->Destroy();
      return rv;
    }

    placeholderFrame->AddStateBits(mAdditionalStateBits);
    // Add the placeholder frame to the flow
    aFrameItems.AddChild(placeholderFrame);
  }
#ifdef DEBUG
  else {
    NS_ASSERTION(aNewFrame->GetParent() == aParentFrame,
                 "In-flow frame has wrong parent");
  }
#endif

  if (aInsertAfter) {
    frameItems->InsertFrame(nsnull, aInsertAfterFrame, aNewFrame);
  } else {
    frameItems->AddChild(aNewFrame);
  }
  
  return NS_OK;
}

void
nsFrameConstructorState::ProcessFrameInsertions(nsAbsoluteItems& aFrameItems,
                                                nsIAtom* aChildListName)
{
#define NS_NONXUL_LIST_TEST (&aFrameItems == &mFloatedItems &&             \
                             aChildListName == nsGkAtoms::floatList)    || \
                            (&aFrameItems == &mAbsoluteItems &&            \
                             aChildListName == nsGkAtoms::absoluteList) || \
                            (&aFrameItems == &mFixedItems &&               \
                             aChildListName == nsGkAtoms::fixedList)
#ifdef MOZ_XUL
  NS_PRECONDITION(NS_NONXUL_LIST_TEST ||
                  (&aFrameItems == &mPopupItems &&
                   aChildListName == nsGkAtoms::popupList), 
                  "Unexpected aFrameItems/aChildListName combination");
#else
  NS_PRECONDITION(NS_NONXUL_LIST_TEST,
                  "Unexpected aFrameItems/aChildListName combination");
#endif

  if (aFrameItems.IsEmpty()) {
    return;
  }
  
  nsIFrame* containingBlock = aFrameItems.containingBlock;

  NS_ASSERTION(containingBlock,
               "Child list without containing block?");
  
  // Insert the frames hanging out in aItems.  We can use SetInitialChildList()
  // if the containing block hasn't been reflowed yet (so NS_FRAME_FIRST_REFLOW
  // is set) and doesn't have any frames in the aChildListName child list yet.
  const nsFrameList& childList = containingBlock->GetChildList(aChildListName);
  nsresult rv = NS_OK;
  if (childList.IsEmpty() &&
      (containingBlock->GetStateBits() & NS_FRAME_FIRST_REFLOW)) {
    rv = containingBlock->SetInitialChildList(aChildListName, aFrameItems);
  } else {
    // Note that whether the frame construction context is doing an append or
    // not is not helpful here, since it could be appending to some frame in
    // the middle of the document, which means we're not necessarily
    // appending to the children of the containing block.
    //
    // We need to make sure the 'append to the end of document' case is fast.
    // So first test the last child of the containing block
    nsIFrame* lastChild = childList.LastChild();

    // CompareTreePosition uses placeholder hierarchy for out of flow frames,
    // so this will make out-of-flows respect the ordering of placeholders,
    // which is great because it takes care of anonymous content.
    nsIFrame* firstNewFrame = aFrameItems.FirstChild();  
    if (!lastChild ||
        nsLayoutUtils::CompareTreePosition(lastChild, firstNewFrame, containingBlock) < 0) {
      // no lastChild, or lastChild comes before the new children, so just append
      rv = containingBlock->AppendFrames(aChildListName, aFrameItems);
    } else {
      // try the other children
      nsIFrame* insertionPoint = nsnull;
      for (nsIFrame* f = childList.FirstChild(); f != lastChild;
           f = f->GetNextSibling()) {
        PRInt32 compare =
          nsLayoutUtils::CompareTreePosition(f, firstNewFrame, containingBlock);
        if (compare > 0) {
          // f comes after the new children, so stop here and insert after
          // the previous frame
          break;
        }
        insertionPoint = f;
      }
      rv = containingBlock->InsertFrames(aChildListName, insertionPoint,
                                         aFrameItems);
    }
  }

  NS_POSTCONDITION(aFrameItems.IsEmpty(), "How did that happen?");

  // XXXbz And if NS_FAILED(rv), what?  I guess we need to clean up the list
  // and deal with all the placeholders... but what if the placeholders aren't
  // in the document yet?  Could that happen?
  NS_ASSERTION(NS_SUCCEEDED(rv), "Frames getting lost!");
}


nsFrameConstructorSaveState::nsFrameConstructorSaveState()
  : mItems(nsnull),
    mFixedPosIsAbsPos(nsnull),
    mSavedItems(nsnull),
    mSavedFixedPosIsAbsPos(PR_FALSE),
    mChildListName(nsnull),
    mState(nsnull)
{
}

nsFrameConstructorSaveState::~nsFrameConstructorSaveState()
{
  // Restore the state
  if (mItems) {
    NS_ASSERTION(mState, "Can't have mItems set without having a state!");
    mState->ProcessFrameInsertions(*mItems, mChildListName);
    *mItems = mSavedItems;
#ifdef DEBUG
    // We've transferred the child list, so drop the pointer we held to it.
    // Note that this only matters for the assert in ~nsAbsoluteItems.
    mSavedItems.Clear();
#endif
  }
  if (mFixedPosIsAbsPos) {
    *mFixedPosIsAbsPos = mSavedFixedPosIsAbsPos;
  }
}

static 
PRBool IsBorderCollapse(nsIFrame* aFrame)
{
  for (nsIFrame* frame = aFrame; frame; frame = frame->GetParent()) {
    if (nsGkAtoms::tableFrame == frame->GetType()) {
      return ((nsTableFrame*)frame)->IsBorderCollapse();
    }
  }
  NS_ASSERTION(PR_FALSE, "program error");
  return PR_FALSE;
}

/**
 * Moves aFrameList from aOldParent to aNewParent.  This updates the parent
 * pointer of the frames in the list, reparents their views as needed, and sets
 * the NS_FRAME_HAS_VIEW bit on aNewParent and its ancestors as needed.  Then
 * it sets the list as the initial child list on aNewParent, unless aNewParent
 * either already has kids or has been reflowed; in that case it appends the
 * new frames.  Note that this method differs from ReparentFrames in that it
 * doesn't change the kids' style contexts.
 */
// XXXbz Since this is only used for {ib} splits, could we just copy the view
// bits from aOldParent to aNewParent and then use the
// nsFrameList::ApplySetParent?  That would still leave us doing two passes
// over the list, of course; if we really wanted to we could factor out the
// relevant part of ReparentFrameViewList, I suppose...  Or just get rid of
// views, which would make most of this function go away.
static void
MoveChildrenTo(nsPresContext* aPresContext,
               nsIFrame* aOldParent,
               nsIFrame* aNewParent,
               nsFrameList& aFrameList)
{
  PRBool sameGrandParent = aOldParent->GetParent() == aNewParent->GetParent();

  if (aNewParent->HasView() || aOldParent->HasView() || !sameGrandParent) {
    // Move the frames into the new view
    nsHTMLContainerFrame::ReparentFrameViewList(aPresContext, aFrameList,
                                                aOldParent, aNewParent);
  }

  PRBool setHasChildWithView = PR_FALSE;

  for (nsFrameList::Enumerator e(aFrameList); !e.AtEnd(); e.Next()) {
    if (!setHasChildWithView
        && (e.get()->GetStateBits() &
            (NS_FRAME_HAS_VIEW | NS_FRAME_HAS_CHILD_WITH_VIEW))) {
      setHasChildWithView = PR_TRUE;
    }

    e.get()->SetParent(aNewParent);
  }

  if (setHasChildWithView) {
    aNewParent->AddStateBits(NS_FRAME_HAS_CHILD_WITH_VIEW);
    if (!sameGrandParent) {
      for (nsIFrame* ancestor = aNewParent->GetParent();
           ancestor; ancestor = ancestor->GetParent()) {
        ancestor->AddStateBits(NS_FRAME_HAS_CHILD_WITH_VIEW);
      }
    }
  }

  if (aNewParent->GetChildList(nsnull).IsEmpty() &&
      (aNewParent->GetStateBits() & NS_FRAME_FIRST_REFLOW)) {
    aNewParent->SetInitialChildList(nsnull, aFrameList);
  } else {
    aNewParent->AppendFrames(nsnull, aFrameList);
  }
}

//----------------------------------------------------------------------

nsCSSFrameConstructor::nsCSSFrameConstructor(nsIDocument *aDocument,
                                             nsIPresShell *aPresShell)
  : mDocument(aDocument)
  , mPresShell(aPresShell)
  , mRootElementFrame(nsnull)
  , mRootElementStyleFrame(nsnull)
  , mFixedContainingBlock(nsnull)
  , mDocElementContainingBlock(nsnull)
  , mGfxScrollFrame(nsnull)
  , mPageSequenceFrame(nsnull)
  , mUpdateCount(0)
  , mQuotesDirty(PR_FALSE)
  , mCountersDirty(PR_FALSE)
  , mIsDestroyingFrameTree(PR_FALSE)
  , mRebuildAllStyleData(PR_FALSE)
  , mHasRootAbsPosContainingBlock(PR_FALSE)
  , mObservingRefreshDriver(PR_FALSE)
  , mInStyleRefresh(PR_FALSE)
  , mHoverGeneration(0)
  , mRebuildAllExtraHint(nsChangeHint(0))
  , mPendingRestyles(ELEMENT_HAS_PENDING_RESTYLE |
                     ELEMENT_IS_POTENTIAL_RESTYLE_ROOT, this)
  , mPendingAnimationRestyles(ELEMENT_HAS_PENDING_ANIMATION_RESTYLE |
                              ELEMENT_IS_POTENTIAL_ANIMATION_RESTYLE_ROOT, this)
{
  // XXXbz this should be in Init() or something!
  if (!mPendingRestyles.Init() || !mPendingAnimationRestyles.Init()) {
    // now what?
  }

#ifdef DEBUG
  static PRBool gFirstTime = PR_TRUE;
  if (gFirstTime) {
    gFirstTime = PR_FALSE;
    char* flags = PR_GetEnv("GECKO_FRAMECTOR_DEBUG_FLAGS");
    if (flags) {
      PRBool error = PR_FALSE;
      for (;;) {
        char* comma = PL_strchr(flags, ',');
        if (comma)
          *comma = '\0';

        PRBool found = PR_FALSE;
        FrameCtorDebugFlags* flag = gFlags;
        FrameCtorDebugFlags* limit = gFlags + NUM_DEBUG_FLAGS;
        while (flag < limit) {
          if (PL_strcasecmp(flag->name, flags) == 0) {
            *(flag->on) = PR_TRUE;
            printf("nsCSSFrameConstructor: setting %s debug flag on\n", flag->name);
            found = PR_TRUE;
            break;
          }
          ++flag;
        }

        if (! found)
          error = PR_TRUE;

        if (! comma)
          break;

        *comma = ',';
        flags = comma + 1;
      }

      if (error) {
        printf("Here are the available GECKO_FRAMECTOR_DEBUG_FLAGS:\n");
        FrameCtorDebugFlags* flag = gFlags;
        FrameCtorDebugFlags* limit = gFlags + NUM_DEBUG_FLAGS;
        while (flag < limit) {
          printf("  %s\n", flag->name);
          ++flag;
        }
        printf("Note: GECKO_FRAMECTOR_DEBUG_FLAGS is a comma separated list of flag\n");
        printf("names (no whitespace)\n");
      }
    }
  }
#endif
}

nsIXBLService * nsCSSFrameConstructor::GetXBLService()
{
  if (!gXBLService) {
    nsresult rv = CallGetService("@mozilla.org/xbl;1", &gXBLService);
    if (NS_FAILED(rv))
      gXBLService = nsnull;
  }
  
  return gXBLService;
}

void
nsCSSFrameConstructor::NotifyDestroyingFrame(nsIFrame* aFrame)
{
  NS_PRECONDITION(mUpdateCount != 0,
                  "Should be in an update while destroying frames");

  if (aFrame->GetStateBits() & NS_FRAME_GENERATED_CONTENT) {
    if (mQuoteList.DestroyNodesFor(aFrame))
      QuotesDirty();
  }

  if (mCounterManager.DestroyNodesFor(aFrame)) {
    // Technically we don't need to update anything if we destroyed only
    // USE nodes.  However, this is unlikely to happen in the real world
    // since USE nodes generally go along with INCREMENT nodes.
    CountersDirty();
  }
}

struct nsGenConInitializer {
  nsAutoPtr<nsGenConNode> mNode;
  nsGenConList*           mList;
  void (nsCSSFrameConstructor::*mDirtyAll)();
  
  nsGenConInitializer(nsGenConNode* aNode, nsGenConList* aList,
                      void (nsCSSFrameConstructor::*aDirtyAll)())
    : mNode(aNode), mList(aList), mDirtyAll(aDirtyAll) {}
};

static void
DestroyGenConInitializer(void*    aFrame,
                         nsIAtom* aPropertyName,
                         void*    aPropertyValue,
                         void*    aDtorData)
{
  delete static_cast<nsGenConInitializer*>(aPropertyValue);
}

already_AddRefed<nsIContent>
nsCSSFrameConstructor::CreateGenConTextNode(nsFrameConstructorState& aState,
                                            const nsString& aString,
                                            nsCOMPtr<nsIDOMCharacterData>* aText,
                                            nsGenConInitializer* aInitializer)
{
  nsCOMPtr<nsIContent> content;
  NS_NewTextNode(getter_AddRefs(content), mDocument->NodeInfoManager());
  if (!content) {
    // XXX The quotes/counters code doesn't like the text pointer
    // being null in case of dynamic changes!
    NS_ASSERTION(!aText, "this OOM case isn't handled very well");
    return nsnull;
  }
  content->SetText(aString, PR_FALSE);
  if (aText) {
    *aText = do_QueryInterface(content);
  }
  if (aInitializer) {
    content->SetProperty(nsGkAtoms::genConInitializerProperty, aInitializer,
                         DestroyGenConInitializer);
    aState.mGeneratedTextNodesWithInitializer.AppendObject(content);
  }
  return content.forget();
}

already_AddRefed<nsIContent>
nsCSSFrameConstructor::CreateGeneratedContent(nsFrameConstructorState& aState,
                                              nsIContent*     aParentContent,
                                              nsStyleContext* aStyleContext,
                                              PRUint32        aContentIndex)
{
  // Get the content value
  const nsStyleContentData &data =
    aStyleContext->GetStyleContent()->ContentAt(aContentIndex);
  nsStyleContentType type = data.mType;

  if (eStyleContentType_Image == type) {
    if (!data.mContent.mImage) {
      // CSS had something specified that couldn't be converted to an
      // image object
      return nsnull;
    }
    
    // Create an image content object and pass it the image request.
    // XXX Check if it's an image type we can handle...

    nsCOMPtr<nsINodeInfo> nodeInfo;
    nodeInfo = mDocument->NodeInfoManager()->GetNodeInfo(nsGkAtoms::mozgeneratedcontentimage, nsnull,
                                                         kNameSpaceID_XHTML);

    nsCOMPtr<nsIContent> content;
    NS_NewGenConImageContent(getter_AddRefs(content), nodeInfo.forget(),
                             data.mContent.mImage);
    return content.forget();
  }

  switch (type) {
  case eStyleContentType_String:
    return CreateGenConTextNode(aState,
                                nsDependentString(data.mContent.mString),
                                nsnull, nsnull);

  case eStyleContentType_Attr:
    {
      nsCOMPtr<nsIAtom> attrName;
      PRInt32 attrNameSpace = kNameSpaceID_None;
      nsAutoString contentString(data.mContent.mString);
      
      PRInt32 barIndex = contentString.FindChar('|'); // CSS namespace delimiter
      if (-1 != barIndex) {
        nsAutoString  nameSpaceVal;
        contentString.Left(nameSpaceVal, barIndex);
        PRInt32 error;
        attrNameSpace = nameSpaceVal.ToInteger(&error, 10);
        contentString.Cut(0, barIndex + 1);
        if (contentString.Length()) {
          if (mDocument->IsHTML() && aParentContent->IsHTML()) {
            ToLowerCase(contentString);
          }
          attrName = do_GetAtom(contentString);
        }
      }
      else {
        if (mDocument->IsHTML() && aParentContent->IsHTML()) {
          ToLowerCase(contentString);
        }
        attrName = do_GetAtom(contentString);
      }

      if (!attrName) {
        return nsnull;
      }

      nsCOMPtr<nsIContent> content;
      NS_NewAttributeContent(mDocument->NodeInfoManager(),
                             attrNameSpace, attrName, getter_AddRefs(content));
      return content.forget();
    }

  case eStyleContentType_Counter:
  case eStyleContentType_Counters:
    {
      nsCSSValue::Array* counters = data.mContent.mCounters;
      nsCounterList* counterList = mCounterManager.CounterListFor(
          nsDependentString(counters->Item(0).GetStringBufferValue()));
      if (!counterList)
        return nsnull;

      nsCounterUseNode* node =
        new nsCounterUseNode(counters, aContentIndex,
                             type == eStyleContentType_Counters);
      if (!node)
        return nsnull;

      nsGenConInitializer* initializer =
        new nsGenConInitializer(node, counterList,
                                &nsCSSFrameConstructor::CountersDirty);
      return CreateGenConTextNode(aState, EmptyString(), &node->mText,
                                  initializer);
    }

  case eStyleContentType_Image:
    NS_NOTREACHED("handled by if above");
    return nsnull;

  case eStyleContentType_OpenQuote:
  case eStyleContentType_CloseQuote:
  case eStyleContentType_NoOpenQuote:
  case eStyleContentType_NoCloseQuote:
    {
      nsQuoteNode* node =
        new nsQuoteNode(type, aContentIndex);
      if (!node)
        return nsnull;

      nsGenConInitializer* initializer =
        new nsGenConInitializer(node, &mQuoteList,
                                &nsCSSFrameConstructor::QuotesDirty);
      return CreateGenConTextNode(aState, EmptyString(), &node->mText,
                                  initializer);
    }
  
  case eStyleContentType_AltContent:
    {
      // Use the "alt" attribute; if that fails and the node is an HTML
      // <input>, try the value attribute and then fall back to some default
      // localized text we have.
      // XXX what if the 'alt' attribute is added later, how will we
      // detect that and do the right thing here?
      if (aParentContent->HasAttr(kNameSpaceID_None, nsGkAtoms::alt)) {
        nsCOMPtr<nsIContent> content;
        NS_NewAttributeContent(mDocument->NodeInfoManager(),
                               kNameSpaceID_None, nsGkAtoms::alt, getter_AddRefs(content));
        return content.forget();
      }

      if (aParentContent->IsHTML() &&
          aParentContent->NodeInfo()->Equals(nsGkAtoms::input)) {
        if (aParentContent->HasAttr(kNameSpaceID_None, nsGkAtoms::value)) {
          nsCOMPtr<nsIContent> content;
          NS_NewAttributeContent(mDocument->NodeInfoManager(),
                                 kNameSpaceID_None, nsGkAtoms::value, getter_AddRefs(content));
          return content.forget();
        }

        nsXPIDLString temp;
        nsContentUtils::GetLocalizedString(nsContentUtils::eFORMS_PROPERTIES,
                                           "Submit", temp);
        return CreateGenConTextNode(aState, temp, nsnull, nsnull);
      }

      break;
    }
  } // switch

  return nsnull;
}

/*
 * aParentFrame - the frame that should be the parent of the generated
 *   content.  This is the frame for the corresponding content node,
 *   which must not be a leaf frame.
 * 
 * Any items created are added to aItems.
 * 
 * We create an XML element (tag _moz_generated_content_before or
 * _moz_generated_content_after) representing the pseudoelement. We
 * create a DOM node for each 'content' item and make those nodes the
 * children of the XML element. Then we create a frame subtree for
 * the XML element as if it were a regular child of
 * aParentFrame/aParentContent, giving the XML element the ::before or
 * ::after style.
 */
void
nsCSSFrameConstructor::CreateGeneratedContentItem(nsFrameConstructorState& aState,
                                                  nsIFrame*        aParentFrame,
                                                  nsIContent*      aParentContent,
                                                  nsStyleContext*  aStyleContext,
                                                  nsCSSPseudoElements::Type aPseudoElement,
                                                  FrameConstructionItemList& aItems)
{
  // XXXbz is this ever true?
  if (!aParentContent->IsElement()) {
    NS_ERROR("Bogus generated content parent");
    return;
  }

  nsStyleSet *styleSet = mPresShell->StyleSet();

  // Probe for the existence of the pseudo-element
  nsRefPtr<nsStyleContext> pseudoStyleContext;
  pseudoStyleContext =
    styleSet->ProbePseudoElementStyle(aParentContent->AsElement(),
                                      aPseudoElement,
                                      aStyleContext);
  if (!pseudoStyleContext)
    return;
  // |ProbePseudoStyleFor| checked the 'display' property and the
  // |ContentCount()| of the 'content' property for us.
  nsCOMPtr<nsINodeInfo> nodeInfo;
  nsIAtom* elemName = aPseudoElement == nsCSSPseudoElements::ePseudo_before ?
    nsGkAtoms::mozgeneratedcontentbefore : nsGkAtoms::mozgeneratedcontentafter;
  nodeInfo = mDocument->NodeInfoManager()->GetNodeInfo(elemName, nsnull,
                                                       kNameSpaceID_None);
  nsCOMPtr<nsIContent> container;
  nsresult rv = NS_NewXMLElement(getter_AddRefs(container), nodeInfo.forget());
  if (NS_FAILED(rv))
    return;
  container->SetNativeAnonymous();

  rv = container->BindToTree(mDocument, aParentContent, aParentContent, PR_TRUE);
  if (NS_FAILED(rv)) {
    container->UnbindFromTree();
    return;
  }

  PRUint32 contentCount = pseudoStyleContext->GetStyleContent()->ContentCount();
  for (PRUint32 contentIndex = 0; contentIndex < contentCount; contentIndex++) {
    nsCOMPtr<nsIContent> content =
      CreateGeneratedContent(aState, aParentContent, pseudoStyleContext,
                             contentIndex);
    if (content) {
      container->AppendChildTo(content, PR_FALSE);
    }
  }

  AddFrameConstructionItemsInternal(aState, container, aParentFrame, elemName,
                                    kNameSpaceID_None, PR_TRUE,
                                    pseudoStyleContext,
                                    ITEM_IS_GENERATED_CONTENT, aItems);
}
    
/****************************************************
 **  BEGIN TABLE SECTION
 ****************************************************/

// The term pseudo frame is being used instead of anonymous frame, since anonymous
// frame has been used elsewhere to refer to frames that have generated content

static PRBool
IsTableRelated(nsIAtom* aParentType)
{
  return
    nsGkAtoms::tableOuterFrame    == aParentType ||
    nsGkAtoms::tableFrame         == aParentType ||
    nsGkAtoms::tableRowGroupFrame == aParentType ||
    nsGkAtoms::tableRowFrame      == aParentType ||
    nsGkAtoms::tableCaptionFrame  == aParentType ||
    nsGkAtoms::tableColGroupFrame == aParentType ||
    nsGkAtoms::tableColFrame      == aParentType ||
    IS_TABLE_CELL(aParentType);
}

// Return whether the given frame is a table pseudo-frame.  Note that
// cell-content and table-outer frames have pseudo-types, but are always
// created, even for non-anonymous cells and tables respectively.  So for those
// we have to examine the cell or table frame to see whether it's a pseudo
// frame.  In particular, a lone table caption will have an outer table as its
// parent, but will also trigger construction of an empty inner table, which
// will be the one we can examine to see whether the outer was a pseudo-frame.
static PRBool
IsTablePseudo(nsIFrame* aFrame)
{
  nsIAtom* pseudoType = aFrame->GetStyleContext()->GetPseudo();
  return pseudoType &&
    (pseudoType == nsCSSAnonBoxes::table ||
     pseudoType == nsCSSAnonBoxes::inlineTable ||
     pseudoType == nsCSSAnonBoxes::tableColGroup ||
     pseudoType == nsCSSAnonBoxes::tableRowGroup ||
     pseudoType == nsCSSAnonBoxes::tableRow ||
     pseudoType == nsCSSAnonBoxes::tableCell ||
     (pseudoType == nsCSSAnonBoxes::cellContent &&
      aFrame->GetParent()->GetStyleContext()->GetPseudo() ==
        nsCSSAnonBoxes::tableCell) ||
     (pseudoType == nsCSSAnonBoxes::tableOuter &&
      (aFrame->GetFirstChild(nsnull)->GetStyleContext()->GetPseudo() ==
         nsCSSAnonBoxes::table ||
       aFrame->GetFirstChild(nsnull)->GetStyleContext()->GetPseudo() ==
         nsCSSAnonBoxes::inlineTable)));
}

/* static */
nsCSSFrameConstructor::ParentType
nsCSSFrameConstructor::GetParentType(nsIAtom* aFrameType)
{
  if (aFrameType == nsGkAtoms::tableFrame) {
    return eTypeTable;
  }
  if (aFrameType == nsGkAtoms::tableRowGroupFrame) {
    return eTypeRowGroup;
  }
  if (aFrameType == nsGkAtoms::tableRowFrame) {
    return eTypeRow;
  }
  if (aFrameType == nsGkAtoms::tableColGroupFrame) {
    return eTypeColGroup;
  }

  return eTypeBlock;
}
           
static nsIFrame*
AdjustCaptionParentFrame(nsIFrame* aParentFrame) 
{
  if (nsGkAtoms::tableFrame == aParentFrame->GetType()) {
    return aParentFrame->GetParent();;
  }
  return aParentFrame;
}
 
/**
 * If the parent frame is a |tableFrame| and the child is a
 * |captionFrame|, then we want to insert the frames beneath the
 * |tableFrame|'s parent frame. Returns |PR_TRUE| if the parent frame
 * needed to be fixed up.
 */
static PRBool
GetCaptionAdjustedParent(nsIFrame*        aParentFrame,
                         const nsIFrame*  aChildFrame,
                         nsIFrame**       aAdjParentFrame)
{
  *aAdjParentFrame = aParentFrame;
  PRBool haveCaption = PR_FALSE;

  if (nsGkAtoms::tableCaptionFrame == aChildFrame->GetType()) {
    haveCaption = PR_TRUE;
    *aAdjParentFrame = AdjustCaptionParentFrame(aParentFrame);
  }
  return haveCaption;
}

void
nsCSSFrameConstructor::AdjustParentFrame(nsIFrame* &                  aParentFrame,
                                         const FrameConstructionData* aFCData,
                                         nsStyleContext*              aStyleContext)
{
  NS_PRECONDITION(aStyleContext, "Must have child's style context");
  NS_PRECONDITION(aFCData, "Must have frame construction data");

  PRBool tablePart = ((aFCData->mBits & FCDATA_IS_TABLE_PART) != 0);

  if (tablePart && aStyleContext->GetStyleDisplay()->mDisplay ==
      NS_STYLE_DISPLAY_TABLE_CAPTION) {
    aParentFrame = AdjustCaptionParentFrame(aParentFrame);
  }
}

// Pull all the captions present in aItems out  into aCaptions
static void
PullOutCaptionFrames(nsFrameItems& aItems, nsFrameItems& aCaptions)
{
  nsIFrame *child = aItems.FirstChild();
  while (child) {
    nsIFrame *nextSibling = child->GetNextSibling();
    if (nsGkAtoms::tableCaptionFrame == child->GetType()) {
      aItems.RemoveFrame(child);
      aCaptions.AddChild(child);
    }
    child = nextSibling;
  }
}


// Construct the outer, inner table frames and the children frames for the table. 
// XXX Page break frames for pseudo table frames are not constructed to avoid the risk
// associated with revising the pseudo frame mechanism. The long term solution
// of having frames handle page-break-before/after will solve the problem. 
nsresult
nsCSSFrameConstructor::ConstructTable(nsFrameConstructorState& aState,
                                      FrameConstructionItem&   aItem,
                                      nsIFrame*                aParentFrame,
                                      const nsStyleDisplay*    aDisplay,
                                      nsFrameItems&            aFrameItems,
                                      nsIFrame**               aNewFrame)
{
  NS_PRECONDITION(aDisplay->mDisplay == NS_STYLE_DISPLAY_TABLE ||
                  aDisplay->mDisplay == NS_STYLE_DISPLAY_INLINE_TABLE,
                  "Unexpected call");

  nsIContent* const content = aItem.mContent;
  nsStyleContext* const styleContext = aItem.mStyleContext;
  const PRUint32 nameSpaceID = aItem.mNameSpaceID;
  
  nsresult rv = NS_OK;

  // create the pseudo SC for the outer table as a child of the inner SC
  nsRefPtr<nsStyleContext> outerStyleContext;
  outerStyleContext = mPresShell->StyleSet()->
    ResolveAnonymousBoxStyle(nsCSSAnonBoxes::tableOuter, styleContext);

  // Create the outer table frame which holds the caption and inner table frame
  nsIFrame* newFrame;
#ifdef MOZ_MATHML
  if (kNameSpaceID_MathML == nameSpaceID)
    newFrame = NS_NewMathMLmtableOuterFrame(mPresShell, outerStyleContext);
  else
#endif
    newFrame = NS_NewTableOuterFrame(mPresShell, outerStyleContext);

  nsIFrame* geometricParent =
    aState.GetGeometricParent(outerStyleContext->GetStyleDisplay(),
                              aParentFrame);

  // Init the table outer frame and see if we need to create a view, e.g.
  // the frame is absolutely positioned  
  InitAndRestoreFrame(aState, content, geometricParent, nsnull, newFrame);  
  nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);

  // Create the inner table frame
  nsIFrame* innerFrame;
#ifdef MOZ_MATHML
  if (kNameSpaceID_MathML == nameSpaceID)
    innerFrame = NS_NewMathMLmtableFrame(mPresShell, styleContext);
  else
#endif
    innerFrame = NS_NewTableFrame(mPresShell, styleContext);
 
  InitAndRestoreFrame(aState, content, newFrame, nsnull, innerFrame);

  // Put the newly created frames into the right child list
  SetInitialSingleChild(newFrame, innerFrame);

  rv = aState.AddChild(newFrame, aFrameItems, content, styleContext,
                       aParentFrame);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (!mRootElementFrame) {
    // The frame we're constructing will be the root element frame.
    // Set mRootElementFrame before processing children.
    mRootElementFrame = newFrame;
  }

  nsFrameItems childItems;
  if (aItem.mFCData->mBits & FCDATA_USE_CHILD_ITEMS) {
    rv = ConstructFramesFromItemList(aState, aItem.mChildItems,
                                     innerFrame, childItems);
  } else {
    rv = ProcessChildren(aState, content, styleContext, innerFrame,
                         PR_TRUE, childItems, PR_FALSE, aItem.mPendingBinding);
  }
  // XXXbz what about cleaning up?
  if (NS_FAILED(rv)) return rv;

  nsFrameItems captionItems;
  PullOutCaptionFrames(childItems, captionItems);

  // Set the inner table frame's initial primary list 
  innerFrame->SetInitialChildList(nsnull, childItems);

  // Set the outer table frame's secondary childlist lists
  if (captionItems.NotEmpty()) {
    newFrame->SetInitialChildList(nsGkAtoms::captionList, captionItems);
  }

  *aNewFrame = newFrame;
  return rv;
}

nsresult
nsCSSFrameConstructor::ConstructTableRow(nsFrameConstructorState& aState,
                                         FrameConstructionItem&   aItem,
                                         nsIFrame*                aParentFrame,
                                         const nsStyleDisplay*    aDisplay,
                                         nsFrameItems&            aFrameItems,
                                         nsIFrame**               aNewFrame)
{
  NS_PRECONDITION(aDisplay->mDisplay == NS_STYLE_DISPLAY_TABLE_ROW,
                  "Unexpected call");
  nsIContent* const content = aItem.mContent;
  nsStyleContext* const styleContext = aItem.mStyleContext;
  const PRUint32 nameSpaceID = aItem.mNameSpaceID;

  nsIFrame* newFrame;
#ifdef MOZ_MATHML
  if (kNameSpaceID_MathML == nameSpaceID)
    newFrame = NS_NewMathMLmtrFrame(mPresShell, styleContext);
  else
#endif
    newFrame = NS_NewTableRowFrame(mPresShell, styleContext);

  if (NS_UNLIKELY(!newFrame)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  InitAndRestoreFrame(aState, content, aParentFrame, nsnull, newFrame);
  nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);

  nsFrameItems childItems;
  nsresult rv;
  if (aItem.mFCData->mBits & FCDATA_USE_CHILD_ITEMS) {
    rv = ConstructFramesFromItemList(aState, aItem.mChildItems, newFrame,
                                     childItems);
  } else {
    rv = ProcessChildren(aState, content, styleContext, newFrame,
                         PR_TRUE, childItems, PR_FALSE, aItem.mPendingBinding);
  }
  if (NS_FAILED(rv)) return rv;

  newFrame->SetInitialChildList(nsnull, childItems);
  aFrameItems.AddChild(newFrame);
  *aNewFrame = newFrame;

  return NS_OK;
}

nsresult
nsCSSFrameConstructor::ConstructTableCol(nsFrameConstructorState& aState,
                                         FrameConstructionItem&   aItem,
                                         nsIFrame*                aParentFrame,
                                         const nsStyleDisplay*    aStyleDisplay,
                                         nsFrameItems&            aFrameItems,
                                         nsIFrame**               aNewFrame)
{
  nsIContent* const content = aItem.mContent;
  nsStyleContext* const styleContext = aItem.mStyleContext;

  nsTableColFrame* colFrame = NS_NewTableColFrame(mPresShell, styleContext);
  if (NS_UNLIKELY(!colFrame)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  InitAndRestoreFrame(aState, content, aParentFrame, nsnull, colFrame);

  NS_ASSERTION(colFrame->GetStyleContext() == styleContext,
               "Unexpected style context");

  aFrameItems.AddChild(colFrame);
  *aNewFrame = colFrame;

  // construct additional col frames if the col frame has a span > 1
  PRInt32 span = colFrame->GetSpan();
  for (PRInt32 spanX = 1; spanX < span; spanX++) {
    nsTableColFrame* newCol = NS_NewTableColFrame(mPresShell, styleContext);
    if (NS_UNLIKELY(!newCol)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    InitAndRestoreFrame(aState, content, aParentFrame, nsnull, newCol,
                        PR_FALSE);
    aFrameItems.LastChild()->SetNextContinuation(newCol);
    newCol->SetPrevContinuation(aFrameItems.LastChild());
    aFrameItems.AddChild(newCol);
    newCol->SetColType(eColAnonymousCol);
  }

  return NS_OK;
}

nsresult
nsCSSFrameConstructor::ConstructTableCell(nsFrameConstructorState& aState,
                                          FrameConstructionItem&   aItem,
                                          nsIFrame*                aParentFrame,
                                          const nsStyleDisplay*    aDisplay,
                                          nsFrameItems&            aFrameItems,
                                          nsIFrame**               aNewFrame)
{
  NS_PRECONDITION(aDisplay->mDisplay == NS_STYLE_DISPLAY_TABLE_CELL,
                  "Unexpected call");

  nsIContent* const content = aItem.mContent;
  nsStyleContext* const styleContext = aItem.mStyleContext;
  const PRUint32 nameSpaceID = aItem.mNameSpaceID;

  PRBool borderCollapse = IsBorderCollapse(aParentFrame);
  nsIFrame* newFrame;
#ifdef MOZ_MATHML
  // <mtable> is border separate in mathml.css and the MathML code doesn't implement
  // border collapse. For those users who style <mtable> with border collapse,
  // give them the default non-MathML table frames that understand border collapse.
  // This won't break us because MathML table frames are all subclasses of the default
  // table code, and so we can freely mix <mtable> with <mtr> or <tr>, <mtd> or <td>.
  // What will happen is just that non-MathML frames won't understand MathML attributes
  // and will therefore miss the special handling that the MathML code does.
  if (kNameSpaceID_MathML == nameSpaceID && !borderCollapse)
    newFrame = NS_NewMathMLmtdFrame(mPresShell, styleContext);
  else
#endif
    // Warning: If you change this and add a wrapper frame around table cell
    // frames, make sure Bug 368554 doesn't regress!
    // See IsInAutoWidthTableCellForQuirk() in nsImageFrame.cpp.    
    newFrame = NS_NewTableCellFrame(mPresShell, styleContext, borderCollapse);

  if (NS_UNLIKELY(!newFrame)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Initialize the table cell frame
  InitAndRestoreFrame(aState, content, aParentFrame, nsnull, newFrame);
  nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
  
  // Resolve pseudo style and initialize the body cell frame
  nsRefPtr<nsStyleContext> innerPseudoStyle;
  innerPseudoStyle = mPresShell->StyleSet()->
    ResolveAnonymousBoxStyle(nsCSSAnonBoxes::cellContent, styleContext);

  // Create a block frame that will format the cell's content
  PRBool isBlock;
  nsIFrame* cellInnerFrame;
#ifdef MOZ_MATHML
  if (kNameSpaceID_MathML == nameSpaceID) {
    cellInnerFrame = NS_NewMathMLmtdInnerFrame(mPresShell, innerPseudoStyle);
    isBlock = PR_FALSE;
  }
  else
#endif
  {
    cellInnerFrame = NS_NewBlockFormattingContext(mPresShell, innerPseudoStyle);
    isBlock = PR_TRUE;
  }

  if (NS_UNLIKELY(!cellInnerFrame)) {
    newFrame->Destroy();
    return NS_ERROR_OUT_OF_MEMORY;
  }

  InitAndRestoreFrame(aState, content, newFrame, nsnull, cellInnerFrame);

  nsFrameItems childItems;
  nsresult rv;
  if (aItem.mFCData->mBits & FCDATA_USE_CHILD_ITEMS) {
    // Need to push ourselves as a float containing block.
    // XXXbz it might be nice to work on getting the parent
    // FrameConstructionItem down into ProcessChildren and just making use of
    // the push there, but that's a bit of work.
    nsFrameConstructorSaveState floatSaveState;
    if (!isBlock) { /* MathML case */
      aState.PushFloatContainingBlock(nsnull, floatSaveState);
    } else {
      aState.PushFloatContainingBlock(cellInnerFrame, floatSaveState);
    }

    rv = ConstructFramesFromItemList(aState, aItem.mChildItems, cellInnerFrame,
                                     childItems);
  } else {
    // Process the child content
    rv = ProcessChildren(aState, content, styleContext, cellInnerFrame,
                         PR_TRUE, childItems, isBlock, aItem.mPendingBinding);
  }
  
  if (NS_FAILED(rv)) {
    // Clean up
    // XXXbz kids of this stuff need to be cleaned up too!
    cellInnerFrame->Destroy();
    newFrame->Destroy();
    return rv;
  }

  cellInnerFrame->SetInitialChildList(nsnull, childItems);
  SetInitialSingleChild(newFrame, cellInnerFrame);
  aFrameItems.AddChild(newFrame);
  *aNewFrame = newFrame;

  return NS_OK;
}

static inline PRBool 
NeedFrameFor(const nsFrameConstructorState& aState,
             nsIFrame*   aParentFrame,
             nsIContent* aChildContent) 
{
  // XXX the GetContent() != aChildContent check is needed due to bug 135040.
  // Remove it once that's fixed.
  NS_PRECONDITION(!aChildContent->GetPrimaryFrame() ||
                  !aState.mSetPrimaryFrames ||
                  aChildContent->GetPrimaryFrame()->GetContent() != aChildContent,
                  "Why did we get called?");

  // don't create a whitespace frame if aParentFrame doesn't want it.
  // always create frames for children in generated content. counter(),
  // quotes, and attr() content can easily change dynamically and we don't
  // want to be reconstructing frames. It's not even clear that these
  // should be considered ignorable just because they evaluate to
  // whitespace.

  // We could handle all this in CreateNeededTablePseudos or some other place
  // after we build our frame construction items, but that would involve
  // creating frame construction items for whitespace kids of
  // eExcludesIgnorableWhitespace frames, where we know we'll be dropping them
  // all anyway, and involve an extra walk down the frame construction item
  // list.
  if (!aParentFrame->IsFrameOfType(nsIFrame::eExcludesIgnorableWhitespace) ||
      aParentFrame->IsGeneratedContentFrame() ||
      !aChildContent->IsNodeOfType(nsINode::eTEXT)) {
    return PR_TRUE;
  }

  aChildContent->SetFlags(NS_CREATE_FRAME_IF_NON_WHITESPACE |
                          NS_REFRAME_IF_WHITESPACE);
  return !aChildContent->TextIsOnlyWhitespace();
}

/***********************************************
 * END TABLE SECTION
 ***********************************************/

static PRBool CheckOverflow(nsPresContext* aPresContext,
                            const nsStyleDisplay* aDisplay)
{
  if (aDisplay->mOverflowX == NS_STYLE_OVERFLOW_VISIBLE)
    return PR_FALSE;

  if (aDisplay->mOverflowX == NS_STYLE_OVERFLOW_CLIP)
    aPresContext->SetViewportOverflowOverride(NS_STYLE_OVERFLOW_HIDDEN,
                                              NS_STYLE_OVERFLOW_HIDDEN);
  else
    aPresContext->SetViewportOverflowOverride(aDisplay->mOverflowX,
                                              aDisplay->mOverflowY);
  return PR_TRUE;
}

/**
 * This checks the root element and the HTML BODY, if any, for an "overflow" property
 * that should be applied to the viewport. If one is found then we return the
 * element that we took the overflow from (which should then be treated as
 * "overflow:visible"), and we store the overflow style in the prescontext.
 * @return if scroll was propagated from some content node, the content node it
 *         was propagated from.
 */
nsIContent*
nsCSSFrameConstructor::PropagateScrollToViewport()
{
  // Set default
  nsPresContext* presContext = mPresShell->GetPresContext();
  presContext->SetViewportOverflowOverride(NS_STYLE_OVERFLOW_AUTO,
                                           NS_STYLE_OVERFLOW_AUTO);

  // We never mess with the viewport scroll state
  // when printing or in print preview
  if (presContext->IsPaginated()) {
    return nsnull;
  }

  Element* docElement = mDocument->GetRootElement();

  // Check the style on the document root element
  nsStyleSet *styleSet = mPresShell->StyleSet();
  nsRefPtr<nsStyleContext> rootStyle;
  rootStyle = styleSet->ResolveStyleFor(docElement, nsnull);
  if (!rootStyle) {
    return nsnull;
  }
  if (CheckOverflow(presContext, rootStyle->GetStyleDisplay())) {
    // tell caller we stole the overflow style from the root element
    return docElement;
  }
  
  // Don't look in the BODY for non-HTML documents or HTML documents
  // with non-HTML roots
  // XXX this should be earlier; we shouldn't even look at the document root
  // for non-HTML documents. Fix this once we support explicit CSS styling
  // of the viewport
  // XXX what about XHTML?
  nsCOMPtr<nsIDOMHTMLDocument> htmlDoc(do_QueryInterface(mDocument));
  if (!htmlDoc || !docElement->IsHTML()) {
    return nsnull;
  }
  
  nsCOMPtr<nsIDOMHTMLElement> body;
  htmlDoc->GetBody(getter_AddRefs(body));
  nsCOMPtr<nsIContent> bodyElement = do_QueryInterface(body);
  
  if (!bodyElement ||
      !bodyElement->NodeInfo()->Equals(nsGkAtoms::body)) {
    // The body is not a <body> tag, it's a <frameset>.
    return nsnull;
  }

  nsRefPtr<nsStyleContext> bodyStyle;
  bodyStyle = styleSet->ResolveStyleFor(bodyElement->AsElement(), rootStyle);
  if (!bodyStyle) {
    return nsnull;
  }

  if (CheckOverflow(presContext, bodyStyle->GetStyleDisplay())) {
    // tell caller we stole the overflow style from the body element
    return bodyElement;
  }

  return nsnull;
}

nsresult
nsCSSFrameConstructor::ConstructDocElementFrame(Element*                 aDocElement,
                                                nsILayoutHistoryState*   aFrameState,
                                                nsIFrame**               aNewFrame)
{
  NS_PRECONDITION(mFixedContainingBlock,
                  "No viewport?  Someone forgot to call ConstructRootFrame!");
  NS_PRECONDITION(mFixedContainingBlock == mPresShell->FrameManager()->GetRootFrame(),
                  "Unexpected mFixedContainingBlock");
  NS_PRECONDITION(!mDocElementContainingBlock,
                  "Shouldn't have a doc element containing block here");

  *aNewFrame = nsnull;

  // Make sure to call PropagateScrollToViewport before
  // SetUpDocElementContainingBlock, since it sets up our scrollbar state
  // properly.
  nsIContent* propagatedScrollFrom = PropagateScrollToViewport();

  SetUpDocElementContainingBlock(aDocElement);

  NS_ASSERTION(mDocElementContainingBlock, "Should have parent by now");

  nsFrameConstructorState state(mPresShell, mFixedContainingBlock, nsnull,
                                nsnull, aFrameState);

  // XXXbz why, exactly?
  if (!mTempFrameTreeState)
    state.mPresShell->CaptureHistoryState(getter_AddRefs(mTempFrameTreeState));

  // Make sure that we'll handle restyles for this document element in
  // the future.  We need this, because the document element might
  // have stale restyle bits from a previous frame constructor for
  // this document.  Unlike in AddFrameConstructionItems, it's safe to
  // unset all element restyle flags, since we don't have any
  // siblings.
  aDocElement->UnsetFlags(ELEMENT_ALL_RESTYLE_FLAGS);

  // --------- CREATE AREA OR BOX FRAME -------
  nsRefPtr<nsStyleContext> styleContext;
  styleContext = mPresShell->StyleSet()->ResolveStyleFor(aDocElement,
                                                         nsnull);

  const nsStyleDisplay* display = styleContext->GetStyleDisplay();

  // Ensure that our XBL bindings are installed.
  if (display->mBinding) {
    // Get the XBL loader.
    nsresult rv;
    PRBool resolveStyle;
    
    nsIXBLService * xblService = GetXBLService();
    if (!xblService)
      return NS_ERROR_FAILURE;

    nsRefPtr<nsXBLBinding> binding;
    rv = xblService->LoadBindings(aDocElement, display->mBinding->mURI,
                                  display->mBinding->mOriginPrincipal,
                                  PR_FALSE, getter_AddRefs(binding),
                                  &resolveStyle);
    if (NS_FAILED(rv))
      return NS_OK; // Binding will load asynchronously.

    if (binding) {
      // For backwards compat, keep firing the root's constructor
      // after all of its kids' constructors.  So tell the binding
      // manager about it right now.
      mDocument->BindingManager()->AddToAttachedQueue(binding);
    }

    if (resolveStyle) {
      styleContext = mPresShell->StyleSet()->ResolveStyleFor(aDocElement,
                                                             nsnull);
      display = styleContext->GetStyleDisplay();
    }
  }

  // --------- IF SCROLLABLE WRAP IN SCROLLFRAME --------

#ifdef DEBUG
  NS_ASSERTION(!display->IsScrollableOverflow() || 
               state.mPresContext->IsPaginated() ||
               propagatedScrollFrom == aDocElement,
               "Scrollbars should have been propagated to the viewport");
#endif

  if (NS_UNLIKELY(display->mDisplay == NS_STYLE_DISPLAY_NONE)) {
    state.mFrameManager->SetUndisplayedContent(aDocElement, styleContext);
    return NS_OK;
  }

  nsFrameConstructorSaveState absoluteSaveState;
  if (mHasRootAbsPosContainingBlock) {
    // Push the absolute containing block now so we can absolutely position
    // the root element
    state.PushAbsoluteContainingBlock(mDocElementContainingBlock,
                                      absoluteSaveState);
  }

  nsresult rv;

  // The rules from CSS 2.1, section 9.2.4, have already been applied
  // by the style system, so we can assume that display->mDisplay is
  // either NONE, BLOCK, or TABLE.

  // contentFrame is the primary frame for the root element. *aNewFrame
  // is the frame that will be the child of the initial containing block.
  // These are usually the same frame but they can be different, in
  // particular if the root frame is positioned, in which case
  // contentFrame is the out-of-flow frame and *aNewFrame is the
  // placeholder.
  nsIFrame* contentFrame;
  PRBool processChildren = PR_FALSE;

  // Check whether we need to build a XUL box or SVG root frame
#ifdef MOZ_XUL
  if (aDocElement->IsXUL()) {
    contentFrame = NS_NewDocElementBoxFrame(mPresShell, styleContext);
    if (NS_UNLIKELY(!contentFrame)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    InitAndRestoreFrame(state, aDocElement, mDocElementContainingBlock, nsnull,
                        contentFrame);
    *aNewFrame = contentFrame;
    processChildren = PR_TRUE;
  }
  else
#endif
#ifdef MOZ_SVG
  if (aDocElement->GetNameSpaceID() == kNameSpaceID_SVG) {
    if (aDocElement->Tag() == nsGkAtoms::svg && NS_SVGEnabled()) {
      contentFrame = NS_NewSVGOuterSVGFrame(mPresShell, styleContext);
      if (NS_UNLIKELY(!contentFrame)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      InitAndRestoreFrame(state, aDocElement,
                          state.GetGeometricParent(display,
                                                   mDocElementContainingBlock),
                          nsnull, contentFrame);

      // AddChild takes care of transforming the frame tree for fixed-pos
      // or abs-pos situations
      nsFrameItems frameItems;
      rv = state.AddChild(contentFrame, frameItems, aDocElement,
                          styleContext, mDocElementContainingBlock);
      if (NS_FAILED(rv) || frameItems.IsEmpty()) {
        return rv;
      }
      *aNewFrame = frameItems.FirstChild();
      processChildren = PR_TRUE;

      // See if we need to create a view
      nsHTMLContainerFrame::CreateViewForFrame(contentFrame, PR_FALSE);
    } else {
      return NS_ERROR_FAILURE;
    }
  }
  else
#endif
  {
    PRBool docElemIsTable = (display->mDisplay == NS_STYLE_DISPLAY_TABLE);
    if (docElemIsTable) {
      // We're going to call the right function ourselves, so no need to give a
      // function to this FrameConstructionData.

      // XXXbz on the other hand, if we converted this whole function to
      // FrameConstructionData/Item, then we'd need the right function
      // here... but would probably be able to get away with less code in this
      // function in general.
      // Use a null PendingBinding, since our binding is not in fact pending.
      static const FrameConstructionData rootTableData = FCDATA_DECL(0, nsnull);
      nsRefPtr<nsStyleContext> extraRef(styleContext);
      FrameConstructionItem item(&rootTableData, aDocElement,
                                 aDocElement->Tag(), kNameSpaceID_None,
                                 nsnull, extraRef.forget(), PR_TRUE);

      nsFrameItems frameItems;
      // if the document is a table then just populate it.
      rv = ConstructTable(state, item, mDocElementContainingBlock,
                          styleContext->GetStyleDisplay(),
                          frameItems, &contentFrame);
      if (NS_FAILED(rv))
        return rv;
      if (!contentFrame || frameItems.IsEmpty())
        return NS_ERROR_FAILURE;
      *aNewFrame = frameItems.FirstChild();
      NS_ASSERTION(frameItems.OnlyChild(), "multiple root element frames");
    } else {
      contentFrame = NS_NewBlockFrame(mPresShell, styleContext,
        NS_BLOCK_FLOAT_MGR|NS_BLOCK_MARGIN_ROOT);
      if (!contentFrame)
        return NS_ERROR_OUT_OF_MEMORY;
      nsFrameItems frameItems;
      // Use a null PendingBinding, since our binding is not in fact pending.
      rv = ConstructBlock(state, display, aDocElement,
                          state.GetGeometricParent(display,
                                                   mDocElementContainingBlock),
                          mDocElementContainingBlock, styleContext,
                          &contentFrame, frameItems, display->IsPositioned(),
                          nsnull);
      if (NS_FAILED(rv) || frameItems.IsEmpty())
        return rv;
      *aNewFrame = frameItems.FirstChild();
      NS_ASSERTION(frameItems.OnlyChild(), "multiple root element frames");
    }
  }

  // set the primary frame
  aDocElement->SetPrimaryFrame(contentFrame);

  NS_ASSERTION(processChildren ? !mRootElementFrame :
                 mRootElementFrame == contentFrame,
               "unexpected mRootElementFrame");
  mRootElementFrame = contentFrame;

  // Figure out which frame has the main style for the document element,
  // assigning it to mRootElementStyleFrame.
  // Backgrounds should be propagated from that frame to the viewport.
  PRBool isChild;
  contentFrame->GetParentStyleContextFrame(state.mPresContext,
          &mRootElementStyleFrame, &isChild);
  if (!isChild) {
    mRootElementStyleFrame = mRootElementFrame;
  }

  if (processChildren) {
    // Still need to process the child content
    nsFrameItems childItems;

    NS_ASSERTION(!nsLayoutUtils::GetAsBlock(contentFrame),
                 "Only XUL and SVG frames should reach here");
    // Use a null PendingBinding, since our binding is not in fact pending.
    ProcessChildren(state, aDocElement, styleContext, contentFrame, PR_TRUE,
                    childItems, PR_FALSE, nsnull);

    // Set the initial child lists
    contentFrame->SetInitialChildList(nsnull, childItems);
  }

  SetInitialSingleChild(mDocElementContainingBlock, *aNewFrame);

  return NS_OK;
}


nsresult
nsCSSFrameConstructor::ConstructRootFrame(nsIFrame** aNewFrame)
{
  AUTO_LAYOUT_PHASE_ENTRY_POINT(mPresShell->GetPresContext(), FrameC);
  NS_PRECONDITION(aNewFrame, "null out param");

  nsStyleSet *styleSet = mPresShell->StyleSet();

  // Set up our style rule observer.
  // XXXbz wouldn't this make more sense as part of presshell init?
  {
    styleSet->SetBindingManager(mDocument->BindingManager());
  }

  // --------- BUILD VIEWPORT -----------
  nsIFrame*                 viewportFrame = nsnull;
  nsRefPtr<nsStyleContext> viewportPseudoStyle;

  viewportPseudoStyle =
    styleSet->ResolveAnonymousBoxStyle(nsCSSAnonBoxes::viewport, nsnull);

  viewportFrame = NS_NewViewportFrame(mPresShell, viewportPseudoStyle);

  // XXXbz do we _have_ to pass a null content pointer to that frame?
  // Would it really kill us to pass in the root element or something?
  // What would that break?
  viewportFrame->Init(nsnull, nsnull, nsnull);

  // Bind the viewport frame to the root view
  nsIView*        rootView;
  mPresShell->GetViewManager()->GetRootView(rootView);
  viewportFrame->SetView(rootView);

  nsContainerFrame::SyncFrameViewProperties(mPresShell->GetPresContext(), viewportFrame,
                                            viewportPseudoStyle, rootView);
  nsContainerFrame::SyncWindowProperties(mPresShell->GetPresContext(), viewportFrame,
                                         rootView);

  // The viewport is the containing block for 'fixed' elements
  mFixedContainingBlock = viewportFrame;

  *aNewFrame = viewportFrame;
  return NS_OK;
}

nsresult
nsCSSFrameConstructor::SetUpDocElementContainingBlock(nsIContent* aDocElement)
{
  NS_PRECONDITION(aDocElement, "No element?");
  NS_PRECONDITION(!aDocElement->GetParent(), "Not root content?");
  NS_PRECONDITION(aDocElement->GetCurrentDoc(), "Not in a document?");
  NS_PRECONDITION(aDocElement->GetCurrentDoc()->GetRootElement() ==
                  aDocElement, "Not the root of the document?");

  /*
    how the root frame hierarchy should look

  Galley presentation, non-XUL, with scrolling (i.e. not a frameset):
  
      ViewportFrame [fixed-cb]
        nsHTMLScrollFrame
          nsCanvasFrame [abs-cb]
            root element frame (nsBlockFrame, nsSVGOuterSVGFrame,
                                nsTableOuterFrame, nsPlaceholderFrame)

  Galley presentation, non-XUL, without scrolling (i.e. a frameset):
  
      ViewportFrame [fixed-cb]
        nsCanvasFrame [abs-cb]
          root element frame (nsBlockFrame)

  Galley presentation, XUL
  
      ViewportFrame [fixed-cb]
        nsRootBoxFrame
          root element frame (nsDocElementBoxFrame)

  Print presentation, non-XUL

      ViewportFrame
        nsSimplePageSequenceFrame
          nsPageFrame [fixed-cb]
            nsPageContentFrame
              nsCanvasFrame [abs-cb]
                root element frame (nsBlockFrame, nsSVGOuterSVGFrame,
                                    nsTableOuterFrame, nsPlaceholderFrame)

  Print-preview presentation, non-XUL

      ViewportFrame
        nsHTMLScrollFrame
          nsSimplePageSequenceFrame
            nsPageFrame [fixed-cb]
              nsPageContentFrame
                nsCanvasFrame [abs-cb]
                  root element frame (nsBlockFrame, nsSVGOuterSVGFrame,
                                      nsTableOuterFrame, nsPlaceholderFrame)

  Print/print preview of XUL is not supported.
  [fixed-cb]: the default containing block for fixed-pos content
  [abs-cb]: the default containing block for abs-pos content
 
  Meaning of nsCSSFrameConstructor fields:
    mRootElementFrame is "root element frame".  This is the primary frame for
      the root element.
    mDocElementContainingBlock is the parent of mRootElementFrame
      (i.e. nsCanvasFrame or nsRootBoxFrame)
    mFixedContainingBlock is the [fixed-cb]
    mGfxScrollFrame is the nsHTMLScrollFrame mentioned above, or null if there isn't one
    mPageSequenceFrame is the nsSimplePageSequenceFrame, or null if there isn't one
  */

  // --------- CREATE ROOT FRAME -------


  // Create the root frame. The document element's frame is a child of the
  // root frame.
  //
  // The root frame serves two purposes:
  // - reserves space for any margins needed for the document element's frame
  // - renders the document element's background. This ensures the background covers
  //   the entire canvas as specified by the CSS2 spec

  nsPresContext* presContext = mPresShell->GetPresContext();
  PRBool isPaginated = presContext->IsRootPaginatedDocument();
  nsIFrame* viewportFrame = mFixedContainingBlock;
  nsStyleContext* viewportPseudoStyle = viewportFrame->GetStyleContext();

  nsIFrame* rootFrame = nsnull;
  nsIAtom* rootPseudo;
        
  if (!isPaginated) {
#ifdef MOZ_XUL
    if (aDocElement->IsXUL())
    {
      // pass a temporary stylecontext, the correct one will be set later
      rootFrame = NS_NewRootBoxFrame(mPresShell, viewportPseudoStyle);
    } else
#endif
    {
      // pass a temporary stylecontext, the correct one will be set later
      rootFrame = NS_NewCanvasFrame(mPresShell, viewportPseudoStyle);
      mHasRootAbsPosContainingBlock = PR_TRUE;
    }

    rootPseudo = nsCSSAnonBoxes::canvas;
    mDocElementContainingBlock = rootFrame;
  } else {
    // Create a page sequence frame
    rootFrame = NS_NewSimplePageSequenceFrame(mPresShell, viewportPseudoStyle);
    mPageSequenceFrame = rootFrame;
    rootPseudo = nsCSSAnonBoxes::pageSequence;
  }


  // --------- IF SCROLLABLE WRAP IN SCROLLFRAME --------

  // If the device supports scrolling (e.g., in galley mode on the screen and
  // for print-preview, but not when printing), then create a scroll frame that
  // will act as the scrolling mechanism for the viewport. 
  // XXX Do we even need a viewport when printing to a printer?

  // As long as the docshell doesn't prohibit it, and the device supports
  // it, create a scroll frame that will act as the scolling mechanism for
  // the viewport.
  //
  // Threre are three possible values stored in the docshell:
  //  1) nsIScrollable::Scrollbar_Never = no scrollbars
  //  2) nsIScrollable::Scrollbar_Auto = scrollbars appear if needed
  //  3) nsIScrollable::Scrollbar_Always = scrollbars always
  // Only need to create a scroll frame/view for cases 2 and 3.

  PRBool isHTML = aDocElement->IsHTML();
  PRBool isXUL = PR_FALSE;

  if (!isHTML) {
    isXUL = aDocElement->IsXUL();
  }

  // Never create scrollbars for XUL documents
  PRBool isScrollable = !isXUL;

  // Never create scrollbars for frameset documents.
  if (isHTML) {
    nsCOMPtr<nsIHTMLDocument> htmlDoc = do_QueryInterface(mDocument);
    if (htmlDoc && htmlDoc->GetIsFrameset())
      isScrollable = PR_FALSE;
  }

  if (isPaginated) {
    isScrollable = presContext->HasPaginatedScrolling();
  }

  // We no longer need to do overflow propagation here. It's taken care of
  // when we construct frames for the element whose overflow might be
  // propagated
  NS_ASSERTION(!isScrollable || !isXUL,
               "XUL documents should never be scrollable - see above");

  nsIFrame* newFrame = rootFrame;
  nsRefPtr<nsStyleContext> rootPseudoStyle;
  // we must create a state because if the scrollbars are GFX it needs the 
  // state to build the scrollbar frames.
  nsFrameConstructorState state(mPresShell, nsnull, nsnull, nsnull);

  // Start off with the viewport as parent; we'll adjust it as needed.
  nsIFrame* parentFrame = viewportFrame;

  nsStyleSet* styleSet = mPresShell->StyleSet();
  // If paginated, make sure we don't put scrollbars in
  if (!isScrollable) {
    rootPseudoStyle = styleSet->ResolveAnonymousBoxStyle(rootPseudo,
                                                         viewportPseudoStyle);
  } else {
      if (rootPseudo == nsCSSAnonBoxes::canvas) {
        rootPseudo = nsCSSAnonBoxes::scrolledCanvas;
      } else {
        NS_ASSERTION(rootPseudo == nsCSSAnonBoxes::pageSequence,
                     "Unknown root pseudo");
        rootPseudo = nsCSSAnonBoxes::scrolledPageSequence;
      }

      // Build the frame. We give it the content we are wrapping which is the
      // document element, the root frame, the parent view port frame, and we
      // should get back the new frame and the scrollable view if one was
      // created.

      // resolve a context for the scrollframe
      nsRefPtr<nsStyleContext>  styleContext;
      styleContext = styleSet->ResolveAnonymousBoxStyle(nsCSSAnonBoxes::viewportScroll,
                                                        viewportPseudoStyle);

      // Note that the viewport scrollframe is always built with
      // overflow:auto style. This forces the scroll frame to create
      // anonymous content for both scrollbars. This is necessary even
      // if the HTML or BODY elements are overriding the viewport
      // scroll style to 'hidden' --- dynamic style changes might put
      // scrollbars back on the viewport and we don't want to have to
      // reframe the viewport to create the scrollbar content.
      newFrame = nsnull;
      rootPseudoStyle = BeginBuildingScrollFrame( state,
                                                  aDocElement,
                                                  styleContext,
                                                  viewportFrame,
                                                  rootPseudo,
                                                  PR_TRUE,
                                                  newFrame);
      parentFrame = newFrame;
      mGfxScrollFrame = newFrame;
  }
  
  rootFrame->SetStyleContextWithoutNotification(rootPseudoStyle);
  rootFrame->Init(aDocElement, parentFrame, nsnull);
  
  if (isScrollable) {
    FinishBuildingScrollFrame(parentFrame, rootFrame);
  }
  
  if (isPaginated) { // paginated
    // Create the first page
    // Set the initial child lists
    nsIFrame *pageFrame, *canvasFrame;
    ConstructPageFrame(mPresShell, presContext, rootFrame, nsnull,
                       pageFrame, canvasFrame);
    SetInitialSingleChild(rootFrame, pageFrame);

    // The eventual parent of the document element frame.
    // XXX should this be set for every new page (in ConstructPageFrame)?
    mDocElementContainingBlock = canvasFrame;
    mHasRootAbsPosContainingBlock = PR_TRUE;
  }

  if (viewportFrame->GetStateBits() & NS_FRAME_FIRST_REFLOW) {
    SetInitialSingleChild(viewportFrame, newFrame);
  } else {
    nsFrameList newFrameList(newFrame, newFrame);
    viewportFrame->AppendFrames(nsnull, newFrameList);
  }

  return NS_OK;
}

nsresult
nsCSSFrameConstructor::ConstructPageFrame(nsIPresShell*  aPresShell,
                                          nsPresContext* aPresContext,
                                          nsIFrame*      aParentFrame,
                                          nsIFrame*      aPrevPageFrame,
                                          nsIFrame*&     aPageFrame,
                                          nsIFrame*&     aCanvasFrame)
{
  nsStyleContext* parentStyleContext = aParentFrame->GetStyleContext();
  nsStyleSet *styleSet = aPresShell->StyleSet();

  nsRefPtr<nsStyleContext> pagePseudoStyle;
  pagePseudoStyle = styleSet->ResolveAnonymousBoxStyle(nsCSSAnonBoxes::page,
                                                       parentStyleContext);

  aPageFrame = NS_NewPageFrame(aPresShell, pagePseudoStyle);
  if (NS_UNLIKELY(!aPageFrame))
    return NS_ERROR_OUT_OF_MEMORY;

  // Initialize the page frame and force it to have a view. This makes printing of
  // the pages easier and faster.
  aPageFrame->Init(nsnull, aParentFrame, aPrevPageFrame);

  nsRefPtr<nsStyleContext> pageContentPseudoStyle;
  pageContentPseudoStyle =
    styleSet->ResolveAnonymousBoxStyle(nsCSSAnonBoxes::pageContent,
                                       pagePseudoStyle);

  nsIFrame* pageContentFrame = NS_NewPageContentFrame(aPresShell, pageContentPseudoStyle);
  if (NS_UNLIKELY(!pageContentFrame))
    return NS_ERROR_OUT_OF_MEMORY;

  // Initialize the page content frame and force it to have a view. Also make it the
  // containing block for fixed elements which are repeated on every page.
  nsIFrame* prevPageContentFrame = nsnull;
  if (aPrevPageFrame) {
    prevPageContentFrame = aPrevPageFrame->GetFirstChild(nsnull);
    NS_ASSERTION(prevPageContentFrame, "missing page content frame");
  }
  pageContentFrame->Init(nsnull, aPageFrame, prevPageContentFrame);
  SetInitialSingleChild(aPageFrame, pageContentFrame);
  mFixedContainingBlock = pageContentFrame;

  nsRefPtr<nsStyleContext> canvasPseudoStyle;
  canvasPseudoStyle = styleSet->ResolveAnonymousBoxStyle(nsCSSAnonBoxes::canvas,
                                                         pageContentPseudoStyle);

  aCanvasFrame = NS_NewCanvasFrame(aPresShell, canvasPseudoStyle);
  if (NS_UNLIKELY(!aCanvasFrame))
    return NS_ERROR_OUT_OF_MEMORY;

  nsIFrame* prevCanvasFrame = nsnull;
  if (prevPageContentFrame) {
    prevCanvasFrame = prevPageContentFrame->GetFirstChild(nsnull);
    NS_ASSERTION(prevCanvasFrame, "missing canvas frame");
  }
  aCanvasFrame->Init(nsnull, pageContentFrame, prevCanvasFrame);
  SetInitialSingleChild(pageContentFrame, aCanvasFrame);

  return NS_OK;
}

/* static */
nsresult
nsCSSFrameConstructor::CreatePlaceholderFrameFor(nsIPresShell*    aPresShell, 
                                                 nsIContent*      aContent,
                                                 nsIFrame*        aFrame,
                                                 nsStyleContext*  aStyleContext,
                                                 nsIFrame*        aParentFrame,
                                                 nsIFrame*        aPrevInFlow,
                                                 nsFrameState     aTypeBit,
                                                 nsIFrame**       aPlaceholderFrame)
{
  nsRefPtr<nsStyleContext> placeholderStyle = aPresShell->StyleSet()->
    ResolveStyleForNonElement(aStyleContext->GetParent());
  
  // The placeholder frame gets a pseudo style context
  nsPlaceholderFrame* placeholderFrame =
    (nsPlaceholderFrame*)NS_NewPlaceholderFrame(aPresShell, placeholderStyle,
                                                aTypeBit);

  if (placeholderFrame) {
    placeholderFrame->Init(aContent, aParentFrame, aPrevInFlow);
  
    // The placeholder frame has a pointer back to the out-of-flow frame
    placeholderFrame->SetOutOfFlowFrame(aFrame);
  
    aFrame->AddStateBits(NS_FRAME_OUT_OF_FLOW);

    // Add mapping from absolutely positioned frame to its placeholder frame
    aPresShell->FrameManager()->RegisterPlaceholderFrame(placeholderFrame);

    *aPlaceholderFrame = static_cast<nsIFrame*>(placeholderFrame);
    
    return NS_OK;
  }
  else {
    return NS_ERROR_OUT_OF_MEMORY;
  }
}

// Clears any lazy bits set in the range [aStartContent, aEndContent).  If
// aEndContent is null, that means to clear bits in all siblings starting with
// aStartContent.  aStartContent must not be null unless aEndContent is also
// null.  We do this so that when new children are inserted under elements whose
// frame is a leaf the new children don't cause us to try to construct frames
// for the existing children again.
static inline void
ClearLazyBits(nsIContent* aStartContent, nsIContent* aEndContent)
{
  NS_PRECONDITION(aStartContent || !aEndContent,
                  "Must have start child if we have an end child");
  for (nsIContent* cur = aStartContent; cur != aEndContent;
       cur = cur->GetNextSibling()) {
    cur->UnsetFlags(NODE_DESCENDANTS_NEED_FRAMES | NODE_NEEDS_FRAME);
  }
}

nsresult
nsCSSFrameConstructor::ConstructButtonFrame(nsFrameConstructorState& aState,
                                            FrameConstructionItem&   aItem,
                                            nsIFrame*                aParentFrame,
                                            const nsStyleDisplay*    aStyleDisplay,
                                            nsFrameItems&            aFrameItems,
                                            nsIFrame**               aNewFrame)
{
  *aNewFrame = nsnull;
  nsIFrame* buttonFrame = nsnull;
  nsIContent* const content = aItem.mContent;
  nsStyleContext* const styleContext = aItem.mStyleContext;

  if (nsGkAtoms::button == aItem.mTag) {
    buttonFrame = NS_NewHTMLButtonControlFrame(mPresShell, styleContext);
  }
  else {
    buttonFrame = NS_NewGfxButtonControlFrame(mPresShell, styleContext);
  }
  if (NS_UNLIKELY(!buttonFrame)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  // Initialize the button frame
  nsresult rv = InitAndRestoreFrame(aState, content,
                                    aState.GetGeometricParent(aStyleDisplay, aParentFrame),
                                    nsnull, buttonFrame);
  if (NS_FAILED(rv)) {
    buttonFrame->Destroy();
    return rv;
  }
  // See if we need to create a view
  nsHTMLContainerFrame::CreateViewForFrame(buttonFrame, PR_FALSE);

  nsRefPtr<nsStyleContext> innerBlockContext;
  innerBlockContext =
    mPresShell->StyleSet()->ResolveAnonymousBoxStyle(nsCSSAnonBoxes::buttonContent,
                                                     styleContext);
                                                               
  nsIFrame* blockFrame = NS_NewBlockFrame(mPresShell, innerBlockContext,
                                          NS_BLOCK_FLOAT_MGR);

  if (NS_UNLIKELY(!blockFrame)) {
    buttonFrame->Destroy();
    return NS_ERROR_OUT_OF_MEMORY;
  }
  rv = InitAndRestoreFrame(aState, content, buttonFrame, nsnull, blockFrame);
  if (NS_FAILED(rv)) {
    blockFrame->Destroy();
    buttonFrame->Destroy();
    return rv;
  }

  rv = aState.AddChild(buttonFrame, aFrameItems, content, styleContext,
                       aParentFrame);
  if (NS_FAILED(rv)) {
    blockFrame->Destroy();
    buttonFrame->Destroy();
    return rv;
  }

  PRBool isLeaf = buttonFrame->IsLeaf();
#ifdef DEBUG
  // Make sure that we're an anonymous content creator exactly when we're a
  // leaf
  nsIAnonymousContentCreator* creator = do_QueryFrame(buttonFrame);
  NS_ASSERTION(!creator == !isLeaf,
               "Should be creator exactly when we're a leaf");
#endif
  
  if (!isLeaf) { 
    // Process children
    nsFrameConstructorSaveState absoluteSaveState;
    nsFrameItems                childItems;

    if (aStyleDisplay->IsPositioned()) {
      // The area frame becomes a container for child frames that are
      // absolutely positioned
      aState.PushAbsoluteContainingBlock(blockFrame, absoluteSaveState);
    }

#ifdef DEBUG
    // Make sure that anonymous child creation will have no effect in this case
    nsIAnonymousContentCreator* creator = do_QueryFrame(blockFrame);
    NS_ASSERTION(!creator, "Shouldn't be an anonymous content creator!");
#endif

    rv = ProcessChildren(aState, content, styleContext, blockFrame, PR_TRUE,
                         childItems, aStyleDisplay->IsBlockInside(),
                         aItem.mPendingBinding);
    if (NS_FAILED(rv)) return rv;
  
    // Set the areas frame's initial child lists
    blockFrame->SetInitialChildList(nsnull, childItems);
  }

  SetInitialSingleChild(buttonFrame, blockFrame);

  if (isLeaf) {
    ClearLazyBits(content->GetFirstChild(), nsnull);

    nsFrameItems  anonymousChildItems;
    // if there are any anonymous children create frames for them.  Note that
    // we're doing this using a different parent frame from the one we pass to
    // ProcessChildren!
    CreateAnonymousFrames(aState, content, buttonFrame, aItem.mPendingBinding,
                          anonymousChildItems);
    if (anonymousChildItems.NotEmpty()) {
      // the anonymous content is already parented to the area frame
      aState.mFrameManager->AppendFrames(blockFrame, nsnull,
                                         anonymousChildItems);
    }
  }

  // our new button frame returned is the top frame. 
  *aNewFrame = buttonFrame; 

  return NS_OK;  
}

nsresult
nsCSSFrameConstructor::ConstructSelectFrame(nsFrameConstructorState& aState,
                                            FrameConstructionItem&   aItem,
                                            nsIFrame*                aParentFrame,
                                            const nsStyleDisplay*    aStyleDisplay,
                                            nsFrameItems&            aFrameItems,
                                            nsIFrame**               aNewFrame)
{
  nsresult rv = NS_OK;
  const PRInt32 kNoSizeSpecified = -1;

  nsIContent* const content = aItem.mContent;
  nsStyleContext* const styleContext = aItem.mStyleContext;

  // Construct a frame-based listbox or combobox
  nsCOMPtr<nsIDOMHTMLSelectElement> sel(do_QueryInterface(content));
  PRInt32 size = 1;
  if (sel) {
    sel->GetSize(&size); 
    PRBool multipleSelect = PR_FALSE;
    sel->GetMultiple(&multipleSelect);
     // Construct a combobox if size=1 or no size is specified and its multiple select
    if (((1 == size || 0 == size) || (kNoSizeSpecified  == size)) && (PR_FALSE == multipleSelect)) {
        // Construct a frame-based combo box.
        // The frame-based combo box is built out of three parts. A display area, a button and
        // a dropdown list. The display area and button are created through anonymous content.
        // The drop-down list's frame is created explicitly. The combobox frame shares its content
        // with the drop-down list.
      PRUint32 flags = NS_BLOCK_FLOAT_MGR;
      nsIFrame* comboboxFrame = NS_NewComboboxControlFrame(mPresShell, styleContext, flags);

      // Save the history state so we don't restore during construction
      // since the complete tree is required before we restore.
      nsILayoutHistoryState *historyState = aState.mFrameState;
      aState.mFrameState = nsnull;
      // Initialize the combobox frame
      InitAndRestoreFrame(aState, content,
                          aState.GetGeometricParent(aStyleDisplay, aParentFrame),
                          nsnull, comboboxFrame);

      nsHTMLContainerFrame::CreateViewForFrame(comboboxFrame, PR_FALSE);

      rv = aState.AddChild(comboboxFrame, aFrameItems, content, styleContext,
                           aParentFrame);
      if (NS_FAILED(rv)) {
        return rv;
      }
      
      ///////////////////////////////////////////////////////////////////
      // Combobox - Old Native Implementation
      ///////////////////////////////////////////////////////////////////
      nsIComboboxControlFrame* comboBox = do_QueryFrame(comboboxFrame);
      NS_ASSERTION(comboBox, "NS_NewComboboxControlFrame returned frame that "
                             "doesn't implement nsIComboboxControlFrame");

        // Resolve pseudo element style for the dropdown list
      nsRefPtr<nsStyleContext> listStyle;
      listStyle = mPresShell->StyleSet()->
        ResolveAnonymousBoxStyle(nsCSSAnonBoxes::dropDownList, styleContext);

        // Create a listbox
      nsIFrame* listFrame = NS_NewListControlFrame(mPresShell, listStyle);

        // Notify the listbox that it is being used as a dropdown list.
      nsIListControlFrame * listControlFrame = do_QueryFrame(listFrame);
      if (listControlFrame) {
        listControlFrame->SetComboboxFrame(comboboxFrame);
      }
         // Notify combobox that it should use the listbox as it's popup
      comboBox->SetDropDown(listFrame);

      NS_ASSERTION(!listStyle->GetStyleDisplay()->IsPositioned(),
                   "Ended up with positioned dropdown list somehow.");
      NS_ASSERTION(!listStyle->GetStyleDisplay()->IsFloating(),
                   "Ended up with floating dropdown list somehow.");
      
      // Initialize the scroll frame positioned. Note that it is NOT
      // initialized as absolutely positioned.
      nsIFrame* scrolledFrame = NS_NewSelectsAreaFrame(mPresShell, styleContext, flags);

      InitializeSelectFrame(aState, listFrame, scrolledFrame, content,
                            comboboxFrame, listStyle, PR_TRUE,
                            aItem.mPendingBinding, aFrameItems);

        // Set flag so the events go to the listFrame not child frames.
        // XXX: We should replace this with a real widget manager similar
        // to how the nsFormControlFrame works. Re-directing events is a temporary Kludge.
      NS_ASSERTION(listFrame->GetView(), "ListFrame's view is nsnull");
      //listFrame->GetView()->SetViewFlags(NS_VIEW_PUBLIC_FLAG_DONT_CHECK_CHILDREN);

      // Create display and button frames from the combobox's anonymous content.
      // The anonymous content is appended to existing anonymous content for this
      // element (the scrollbars).

      nsFrameItems childItems;
      CreateAnonymousFrames(aState, content, comboboxFrame,
                            aItem.mPendingBinding, childItems);
  
      comboboxFrame->SetInitialChildList(nsnull, childItems);

      // Initialize the additional popup child list which contains the
      // dropdown list frame.
      nsFrameItems popupItems;
      popupItems.AddChild(listFrame);
      comboboxFrame->SetInitialChildList(nsGkAtoms::selectPopupList,
                                         popupItems);

      *aNewFrame = comboboxFrame;
      aState.mFrameState = historyState;
      if (aState.mFrameState && aState.mFrameManager) {
        // Restore frame state for the entire subtree of |comboboxFrame|.
        aState.mFrameManager->RestoreFrameState(comboboxFrame,
                                                aState.mFrameState);
      }
    } else {
      ///////////////////////////////////////////////////////////////////
      // ListBox - Old Native Implementation
      ///////////////////////////////////////////////////////////////////
      nsIFrame* listFrame = NS_NewListControlFrame(mPresShell, styleContext);
      if (listFrame) {
        rv = NS_OK;
      }
      else {
        rv = NS_ERROR_OUT_OF_MEMORY;
      }

      nsIFrame* scrolledFrame = NS_NewSelectsAreaFrame(
        mPresShell, styleContext, NS_BLOCK_FLOAT_MGR);

      // ******* this code stolen from Initialze ScrollFrame ********
      // please adjust this code to use BuildScrollFrame.

      InitializeSelectFrame(aState, listFrame, scrolledFrame, content,
                            aParentFrame, styleContext, PR_FALSE,
                            aItem.mPendingBinding, aFrameItems);

      *aNewFrame = listFrame;
    }
  }
  return rv;

}

/**
 * Used to be InitializeScrollFrame but now it's only used for the select tag
 * But the select tag should really be fixed to use GFX scrollbars that can
 * be create with BuildScrollFrame.
 */
nsresult
nsCSSFrameConstructor::InitializeSelectFrame(nsFrameConstructorState& aState,
                                             nsIFrame*                scrollFrame,
                                             nsIFrame*                scrolledFrame,
                                             nsIContent*              aContent,
                                             nsIFrame*                aParentFrame,
                                             nsStyleContext*          aStyleContext,
                                             PRBool                   aBuildCombobox,
                                             PendingBinding*          aPendingBinding,
                                             nsFrameItems&            aFrameItems)
{
  const nsStyleDisplay* display = aStyleContext->GetStyleDisplay();

  // Initialize it
  nsIFrame* geometricParent = aState.GetGeometricParent(display, aParentFrame);
    
  // We don't call InitAndRestoreFrame for scrollFrame because we can only
  // restore the frame state after its parts have been created (in particular,
  // the scrollable view). So we have to split Init and Restore.

  // Initialize the frame
  scrollFrame->Init(aContent, geometricParent, nsnull);

  if (!aBuildCombobox) {
    nsresult rv = aState.AddChild(scrollFrame, aFrameItems, aContent,
                                  aStyleContext, aParentFrame);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }
      
  nsHTMLContainerFrame::CreateViewForFrame(scrollFrame, aBuildCombobox);
  if (aBuildCombobox) {
    // Give the drop-down list a popup widget
    nsIView* view = scrollFrame->GetView();
    NS_ASSERTION(view, "We asked for a view but didn't get one");
    if (view) {
      view->GetViewManager()->SetViewFloating(view, PR_TRUE);

      nsWidgetInitData widgetData;
      widgetData.mWindowType  = eWindowType_popup;
      widgetData.mBorderStyle = eBorderStyle_default;
      view->CreateWidgetForPopup(&widgetData);
    }
  }

  BuildScrollFrame(aState, aContent, aStyleContext, scrolledFrame,
                   geometricParent, scrollFrame);

  if (aState.mFrameState && aState.mFrameManager) {
    // Restore frame state for the scroll frame
    aState.mFrameManager->RestoreFrameStateFor(scrollFrame, aState.mFrameState);
  }

  // Process children
  nsFrameConstructorSaveState absoluteSaveState;
  nsFrameItems                childItems;

  if (display->IsPositioned()) {
    // The area frame becomes a container for child frames that are
    // absolutely positioned
    aState.PushAbsoluteContainingBlock(scrolledFrame, absoluteSaveState);
  }

  ProcessChildren(aState, aContent, aStyleContext, scrolledFrame, PR_FALSE,
                  childItems, PR_FALSE, aPendingBinding);

  // Set the scrolled frame's initial child lists
  scrolledFrame->SetInitialChildList(nsnull, childItems);
  return NS_OK;
}

nsresult
nsCSSFrameConstructor::ConstructFieldSetFrame(nsFrameConstructorState& aState,
                                              FrameConstructionItem&   aItem,
                                              nsIFrame*                aParentFrame,
                                              const nsStyleDisplay*    aStyleDisplay,
                                              nsFrameItems&            aFrameItems,
                                              nsIFrame**               aNewFrame)
{
  nsIContent* const content = aItem.mContent;
  nsStyleContext* const styleContext = aItem.mStyleContext;

  nsIFrame* newFrame = NS_NewFieldSetFrame(mPresShell, styleContext);
  if (NS_UNLIKELY(!newFrame)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Initialize it
  InitAndRestoreFrame(aState, content,
                      aState.GetGeometricParent(aStyleDisplay, aParentFrame),
                      nsnull, newFrame);

  // See if we need to create a view, e.g. the frame is absolutely
  // positioned
  nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);

  // Resolve style and initialize the frame
  nsRefPtr<nsStyleContext> fieldsetContentStyle;
  fieldsetContentStyle = mPresShell->StyleSet()->
    ResolveAnonymousBoxStyle(nsCSSAnonBoxes::fieldsetContent, styleContext);

  nsIFrame* blockFrame = NS_NewBlockFrame(mPresShell, fieldsetContentStyle,
                                          NS_BLOCK_FLOAT_MGR |
                                          NS_BLOCK_MARGIN_ROOT);
  InitAndRestoreFrame(aState, content, newFrame, nsnull, blockFrame);

  nsresult rv = aState.AddChild(newFrame, aFrameItems, content, styleContext,
                                aParentFrame);
  if (NS_FAILED(rv)) {
    return rv;
  }
  
  // Process children
  nsFrameConstructorSaveState absoluteSaveState;
  nsFrameItems                childItems;

  if (aStyleDisplay->IsPositioned()) {
    // The area frame becomes a container for child frames that are
    // absolutely positioned
    // XXXbz this is probably wrong, and once arbitrary frames can be absolute
    // containing blocks we should fix this..
    aState.PushAbsoluteContainingBlock(blockFrame, absoluteSaveState);
  }

  ProcessChildren(aState, content, styleContext, blockFrame, PR_TRUE,
                  childItems, PR_TRUE, aItem.mPendingBinding);

  nsFrameItems fieldsetKids;
  fieldsetKids.AddChild(blockFrame);

  for (nsFrameList::Enumerator e(childItems); !e.AtEnd(); e.Next()) {
    nsLegendFrame* legendFrame = do_QueryFrame(e.get());
    if (legendFrame) {
      // We want the legend to be the first frame in the fieldset child list.
      // That way the EventStateManager will do the right thing when tabbing
      // from a selection point within the legend (bug 236071), which is
      // used for implementing legend access keys (bug 81481).
      // GetAdjustedParentFrame() below depends on this frame order.
      childItems.RemoveFrame(legendFrame);
      // Make sure to reparent the legend so it has the fieldset as the parent.
      fieldsetKids.InsertFrame(newFrame, nsnull, legendFrame);
      break;
    }
  }

  // Set the inner frame's initial child lists
  blockFrame->SetInitialChildList(nsnull, childItems);

  // Set the outer frame's initial child list
  newFrame->SetInitialChildList(nsnull, fieldsetKids);

  // our new frame returned is the top frame which is the list frame. 
  *aNewFrame = newFrame; 

  return NS_OK;
}

static nsIFrame*
FindAncestorWithGeneratedContentPseudo(nsIFrame* aFrame)
{
  for (nsIFrame* f = aFrame->GetParent(); f; f = f->GetParent()) {
    NS_ASSERTION(f->IsGeneratedContentFrame(),
                 "should not have exited generated content");
    nsIAtom* pseudo = f->GetStyleContext()->GetPseudo();
    if (pseudo == nsCSSPseudoElements::before ||
        pseudo == nsCSSPseudoElements::after)
      return f;
  }
  return nsnull;
}

#define SIMPLE_FCDATA(_func) FCDATA_DECL(0, _func)
#define FULL_CTOR_FCDATA(_flags, _func)                     \
  { _flags | FCDATA_FUNC_IS_FULL_CTOR, { nsnull }, _func }

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindTextData(nsIFrame* aParentFrame)
{
#ifdef MOZ_SVG
  if (aParentFrame && aParentFrame->IsFrameOfType(nsIFrame::eSVG)) {
    nsIFrame *ancestorFrame =
      nsSVGUtils::GetFirstNonAAncestorFrame(aParentFrame);
    if (ancestorFrame) {
      nsSVGTextContainerFrame* metrics = do_QueryFrame(ancestorFrame);
      if (metrics) {
        static const FrameConstructionData sSVGGlyphData =
          SIMPLE_FCDATA(NS_NewSVGGlyphFrame);
        return &sSVGGlyphData;
      }
    }
    return nsnull;
  }
#endif

  static const FrameConstructionData sTextData =
    FCDATA_DECL(FCDATA_IS_LINE_PARTICIPANT, NS_NewTextFrame);
  return &sTextData;
}

nsresult
nsCSSFrameConstructor::ConstructTextFrame(const FrameConstructionData* aData,
                                          nsFrameConstructorState& aState,
                                          nsIContent*              aContent,
                                          nsIFrame*                aParentFrame,
                                          nsStyleContext*          aStyleContext,
                                          nsFrameItems&            aFrameItems)
{
  NS_PRECONDITION(aData, "Must have frame construction data");

  nsIFrame* newFrame = (*aData->mFunc.mCreationFunc)(mPresShell, aStyleContext);

  if (NS_UNLIKELY(!newFrame))
    return NS_ERROR_OUT_OF_MEMORY;

  nsresult rv = InitAndRestoreFrame(aState, aContent, aParentFrame,
                                    nsnull, newFrame);

  if (NS_FAILED(rv)) {
    newFrame->Destroy();
    return rv;
  }

  // We never need to create a view for a text frame.

  if (newFrame->IsGeneratedContentFrame()) {
    nsAutoPtr<nsGenConInitializer> initializer;
    initializer =
      static_cast<nsGenConInitializer*>(
        aContent->UnsetProperty(nsGkAtoms::genConInitializerProperty));
    if (initializer) {
      if (initializer->mNode->InitTextFrame(initializer->mList,
              FindAncestorWithGeneratedContentPseudo(newFrame), newFrame)) {
        (this->*(initializer->mDirtyAll))();
      }
      initializer->mNode.forget();
    }
  }
  
  // Add the newly constructed frame to the flow
  aFrameItems.AddChild(newFrame);

  if (aState.mSetPrimaryFrames)
    aContent->SetPrimaryFrame(newFrame);
  
  return rv;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindDataByInt(PRInt32 aInt,
                                     nsIContent* aContent,
                                     nsStyleContext* aStyleContext,
                                     const FrameConstructionDataByInt* aDataPtr,
                                     PRUint32 aDataLength)
{
  for (const FrameConstructionDataByInt *curData = aDataPtr,
         *endData = aDataPtr + aDataLength;
       curData != endData;
       ++curData) {
    if (curData->mInt == aInt) {
      const FrameConstructionData* data = &curData->mData;
      if (data->mBits & FCDATA_FUNC_IS_DATA_GETTER) {
        return data->mFunc.mDataGetter(aContent, aStyleContext);
      }

      return data;
    }
  }

  return nsnull;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindDataByTag(nsIAtom* aTag,
                                     nsIContent* aContent,
                                     nsStyleContext* aStyleContext,
                                     const FrameConstructionDataByTag* aDataPtr,
                                     PRUint32 aDataLength)
{
  for (const FrameConstructionDataByTag *curData = aDataPtr,
         *endData = aDataPtr + aDataLength;
       curData != endData;
       ++curData) {
    if (*curData->mTag == aTag) {
      const FrameConstructionData* data = &curData->mData;
      if (data->mBits & FCDATA_FUNC_IS_DATA_GETTER) {
        return data->mFunc.mDataGetter(aContent, aStyleContext);
      }

      return data;
    }
  }

  return nsnull;
}

#define SUPPRESS_FCDATA() FCDATA_DECL(FCDATA_SUPPRESS_FRAME, nsnull)
#define SIMPLE_INT_CREATE(_int, _func) { _int, SIMPLE_FCDATA(_func) }
#define SIMPLE_INT_CHAIN(_int, _func)                       \
  { _int, FCDATA_DECL(FCDATA_FUNC_IS_DATA_GETTER, _func) }
#define COMPLEX_INT_CREATE(_int, _func)         \
  { _int, FULL_CTOR_FCDATA(0, _func) }

#define SIMPLE_TAG_CREATE(_tag, _func)          \
  { &nsGkAtoms::_tag, SIMPLE_FCDATA(_func) }
#define SIMPLE_TAG_CHAIN(_tag, _func)                                   \
  { &nsGkAtoms::_tag, FCDATA_DECL(FCDATA_FUNC_IS_DATA_GETTER,  _func) }
#define COMPLEX_TAG_CREATE(_tag, _func)             \
  { &nsGkAtoms::_tag, FULL_CTOR_FCDATA(0, _func) }

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindHTMLData(nsIContent* aContent,
                                    nsIAtom* aTag,
                                    PRInt32 aNameSpaceID,
                                    nsIFrame* aParentFrame,
                                    nsStyleContext* aStyleContext)
{
  // Ignore the tag if it's not HTML content and if it doesn't extend (via XBL)
  // a valid HTML namespace.  This check must match the one in
  // ShouldHaveFirstLineStyle.
  if (aNameSpaceID != kNameSpaceID_XHTML) {
    return nsnull;
  }

  NS_ASSERTION(!aParentFrame ||
               aParentFrame->GetStyleContext()->GetPseudo() !=
                 nsCSSAnonBoxes::fieldsetContent ||
               aParentFrame->GetParent()->GetType() == nsGkAtoms::fieldSetFrame,
               "Unexpected parent for fieldset content anon box");
  if (aTag == nsGkAtoms::legend &&
      (!aParentFrame ||
       (aParentFrame->GetType() != nsGkAtoms::fieldSetFrame &&
        aParentFrame->GetStyleContext()->GetPseudo() !=
          nsCSSAnonBoxes::fieldsetContent) ||
       !aContent->GetParent() ||
       !aContent->GetParent()->IsHTML() ||
       aContent->GetParent()->Tag() != nsGkAtoms::fieldset ||
       aStyleContext->GetStyleDisplay()->IsFloating() ||
       aStyleContext->GetStyleDisplay()->IsAbsolutelyPositioned())) {
    // <legend> is only special inside fieldset, check both the frame tree
    // parent and content tree parent due to XBL issues. For floated or
    // absolutely positioned legends we want to construct by display type and
    // not do special legend stuff.
    // XXXbz it would be nice if we could just decide this based on the parent
    // tag, and hence just use a SIMPLE_TAG_CHAIN for legend below, but the
    // fact that with XBL we could end up with this legend element in some
    // totally weird insertion point makes that chancy, I think.
    return nsnull;
  }

  static const FrameConstructionDataByTag sHTMLData[] = {
    SIMPLE_TAG_CHAIN(img, nsCSSFrameConstructor::FindImgData),
    SIMPLE_TAG_CHAIN(mozgeneratedcontentimage,
                     nsCSSFrameConstructor::FindImgData),
    { &nsGkAtoms::br,
      FCDATA_DECL(FCDATA_IS_LINE_PARTICIPANT | FCDATA_IS_LINE_BREAK,
                  NS_NewBRFrame) },
    SIMPLE_TAG_CREATE(wbr, NS_NewWBRFrame),
    SIMPLE_TAG_CHAIN(input, nsCSSFrameConstructor::FindInputData),
    SIMPLE_TAG_CREATE(textarea, NS_NewTextControlFrame),
    COMPLEX_TAG_CREATE(select, &nsCSSFrameConstructor::ConstructSelectFrame),
    SIMPLE_TAG_CHAIN(object, nsCSSFrameConstructor::FindObjectData),
    SIMPLE_TAG_CHAIN(applet, nsCSSFrameConstructor::FindObjectData),
    SIMPLE_TAG_CHAIN(embed, nsCSSFrameConstructor::FindObjectData),
    COMPLEX_TAG_CREATE(fieldset,
                       &nsCSSFrameConstructor::ConstructFieldSetFrame),
    SIMPLE_TAG_CREATE(legend, NS_NewLegendFrame),
    SIMPLE_TAG_CREATE(frameset, NS_NewHTMLFramesetFrame),
    SIMPLE_TAG_CREATE(iframe, NS_NewSubDocumentFrame),
    COMPLEX_TAG_CREATE(button, &nsCSSFrameConstructor::ConstructButtonFrame),
    SIMPLE_TAG_CREATE(canvas, NS_NewHTMLCanvasFrame),
#if defined(MOZ_MEDIA)
    SIMPLE_TAG_CREATE(video, NS_NewHTMLVideoFrame),
    SIMPLE_TAG_CREATE(audio, NS_NewHTMLVideoFrame),
#endif
    SIMPLE_TAG_CREATE(isindex, NS_NewIsIndexFrame)
  };

  return FindDataByTag(aTag, aContent, aStyleContext, sHTMLData,
                       NS_ARRAY_LENGTH(sHTMLData));
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindImgData(nsIContent* aContent,
                                   nsStyleContext* aStyleContext)
{
  if (!nsImageFrame::ShouldCreateImageFrameFor(aContent, aStyleContext)) {
    return nsnull;
  }

  static const FrameConstructionData sImgData = SIMPLE_FCDATA(NS_NewImageFrame);
  return &sImgData;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindImgControlData(nsIContent* aContent,
                                          nsStyleContext* aStyleContext)
{
  if (!nsImageFrame::ShouldCreateImageFrameFor(aContent, aStyleContext)) {
    return nsnull;
  }

  static const FrameConstructionData sImgControlData =
    SIMPLE_FCDATA(NS_NewImageControlFrame);
  return &sImgControlData;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindInputData(nsIContent* aContent,
                                     nsStyleContext* aStyleContext)
{
  static const FrameConstructionDataByInt sInputData[] = {
    SIMPLE_INT_CREATE(NS_FORM_INPUT_CHECKBOX, NS_NewGfxCheckboxControlFrame),
    SIMPLE_INT_CREATE(NS_FORM_INPUT_RADIO, NS_NewGfxRadioControlFrame),
    SIMPLE_INT_CREATE(NS_FORM_INPUT_FILE, NS_NewFileControlFrame),
    SIMPLE_INT_CHAIN(NS_FORM_INPUT_IMAGE,
                     nsCSSFrameConstructor::FindImgControlData),
    SIMPLE_INT_CREATE(NS_FORM_INPUT_EMAIL, NS_NewTextControlFrame),
    SIMPLE_INT_CREATE(NS_FORM_INPUT_SEARCH, NS_NewTextControlFrame),
    SIMPLE_INT_CREATE(NS_FORM_INPUT_TEXT, NS_NewTextControlFrame),
    SIMPLE_INT_CREATE(NS_FORM_INPUT_TEL, NS_NewTextControlFrame),
    SIMPLE_INT_CREATE(NS_FORM_INPUT_URL, NS_NewTextControlFrame),
    SIMPLE_INT_CREATE(NS_FORM_INPUT_PASSWORD, NS_NewTextControlFrame),
    COMPLEX_INT_CREATE(NS_FORM_INPUT_SUBMIT,
                       &nsCSSFrameConstructor::ConstructButtonFrame),
    COMPLEX_INT_CREATE(NS_FORM_INPUT_RESET,
                       &nsCSSFrameConstructor::ConstructButtonFrame),
    COMPLEX_INT_CREATE(NS_FORM_INPUT_BUTTON,
                       &nsCSSFrameConstructor::ConstructButtonFrame)
    // Keeping hidden inputs out of here on purpose for so they get frames by
    // display (in practice, none).
  };

  nsCOMPtr<nsIFormControl> control = do_QueryInterface(aContent);
  NS_ASSERTION(control, "input doesn't implement nsIFormControl?");

  return FindDataByInt(control->GetType(), aContent, aStyleContext,
                       sInputData, NS_ARRAY_LENGTH(sInputData));
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindObjectData(nsIContent* aContent,
                                      nsStyleContext* aStyleContext)
{
  // GetDisplayedType isn't necessarily nsIObjectLoadingContent::TYPE_NULL for
  // cases when the object is broken/suppressed/etc (e.g. a broken image), but
  // we want to treat those cases as TYPE_NULL
  PRUint32 type;
  if (aContent->IntrinsicState() &
      (NS_EVENT_STATE_BROKEN | NS_EVENT_STATE_USERDISABLED |
       NS_EVENT_STATE_SUPPRESSED)) {
    type = nsIObjectLoadingContent::TYPE_NULL;
  } else {
    nsCOMPtr<nsIObjectLoadingContent> objContent(do_QueryInterface(aContent));
    NS_ASSERTION(objContent,
                 "applet, embed and object must implement "
                 "nsIObjectLoadingContent!");

    objContent->GetDisplayedType(&type);
  }

  static const FrameConstructionDataByInt sObjectData[] = {
    SIMPLE_INT_CREATE(nsIObjectLoadingContent::TYPE_LOADING,
                      NS_NewEmptyFrame),
    SIMPLE_INT_CREATE(nsIObjectLoadingContent::TYPE_PLUGIN,
                      NS_NewObjectFrame),
    SIMPLE_INT_CREATE(nsIObjectLoadingContent::TYPE_IMAGE,
                      NS_NewImageFrame),
    SIMPLE_INT_CREATE(nsIObjectLoadingContent::TYPE_DOCUMENT,
                      NS_NewSubDocumentFrame)
    // Nothing for TYPE_NULL so we'll construct frames by display there
  };

  return FindDataByInt((PRInt32)type, aContent, aStyleContext,
                       sObjectData, NS_ARRAY_LENGTH(sObjectData));
}

nsresult
nsCSSFrameConstructor::ConstructFrameFromItemInternal(FrameConstructionItem& aItem,
                                                      nsFrameConstructorState& aState,
                                                      nsIFrame* aParentFrame,
                                                      nsFrameItems& aFrameItems)
{
  const FrameConstructionData* data = aItem.mFCData;
  NS_ASSERTION(data, "Must have frame construction data");

  PRUint32 bits = data->mBits;

  NS_ASSERTION(!(bits & FCDATA_FUNC_IS_DATA_GETTER),
               "Should have dealt with this inside the data finder");

  // Some sets of bits are not compatible with each other
#define CHECK_ONLY_ONE_BIT(_bit1, _bit2)               \
  NS_ASSERTION(!(bits & _bit1) || !(bits & _bit2),     \
               "Only one of these bits should be set")
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR, FCDATA_FORCE_NULL_ABSPOS_CONTAINER);
#ifdef MOZ_MATHML
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR, FCDATA_WRAP_KIDS_IN_BLOCKS);
#endif
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR, FCDATA_MAY_NEED_SCROLLFRAME);
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR, FCDATA_IS_POPUP);
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR, FCDATA_SKIP_ABSPOS_PUSH);
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR, FCDATA_FORCE_VIEW);
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR,
                     FCDATA_DISALLOW_GENERATED_CONTENT);
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR, FCDATA_ALLOW_BLOCK_STYLES);
  CHECK_ONLY_ONE_BIT(FCDATA_MAY_NEED_SCROLLFRAME, FCDATA_FORCE_VIEW);
#undef CHECK_ONLY_ONE_BIT

  nsStyleContext* const styleContext = aItem.mStyleContext;
  const nsStyleDisplay* display = styleContext->GetStyleDisplay();

  nsIFrame* newFrame;
  nsIFrame* primaryFrame;
  if (bits & FCDATA_FUNC_IS_FULL_CTOR) {
    nsresult rv =
      (this->*(data->mFullConstructor))(aState, aItem, aParentFrame,
                                        display, aFrameItems, &newFrame);
    if (NS_FAILED(rv)) {
      return rv;
    }

    primaryFrame = newFrame;
  } else {
    nsIContent* const content = aItem.mContent;

    newFrame =
      (*data->mFunc.mCreationFunc)(mPresShell, styleContext);
    if (!newFrame) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    PRBool allowOutOfFlow = !(bits & FCDATA_DISALLOW_OUT_OF_FLOW);
    PRBool isPopup = aItem.mIsPopup;
    NS_ASSERTION(!isPopup ||
                 (aState.mPopupItems.containingBlock &&
                  aState.mPopupItems.containingBlock->GetType() ==
                    nsGkAtoms::popupSetFrame),
                 "Should have a containing block here!");

    nsIFrame* geometricParent =
      isPopup ? aState.mPopupItems.containingBlock :
      (allowOutOfFlow ? aState.GetGeometricParent(display, aParentFrame)
                      : aParentFrame);

    nsresult rv = NS_OK;

    // Must init frameToAddToList to null, since it's inout
    nsIFrame* frameToAddToList = nsnull;
    if ((bits & FCDATA_MAY_NEED_SCROLLFRAME) &&
        display->IsScrollableOverflow()) {
      BuildScrollFrame(aState, content, styleContext, newFrame,
                       geometricParent, frameToAddToList);
    } else {
      rv = InitAndRestoreFrame(aState, content, geometricParent, nsnull,
                               newFrame);
      NS_ASSERTION(NS_SUCCEEDED(rv), "InitAndRestoreFrame failed");
      // See whether we need to create a view
      nsHTMLContainerFrame::CreateViewForFrame(newFrame,
                                               (bits & FCDATA_FORCE_VIEW) != 0);
      frameToAddToList = newFrame;
    }

    // Use frameToAddToList as the primary frame.  In the non-scrollframe case
    // they're equal, but in the scrollframe case newFrame is the scrolled
    // frame, while frameToAddToList is the scrollframe (and should be the
    // primary frame).
    primaryFrame = frameToAddToList;

    rv = aState.AddChild(frameToAddToList, aFrameItems, content, styleContext,
                         aParentFrame, allowOutOfFlow, allowOutOfFlow, isPopup);
    if (NS_FAILED(rv)) {
      return rv;
    }

#ifdef MOZ_XUL
    // Icky XUL stuff, sadly

    if (aItem.mIsRootPopupgroup) {
      NS_ASSERTION(nsIRootBox::GetRootBox(mPresShell) &&
                   nsIRootBox::GetRootBox(mPresShell)->GetPopupSetFrame() ==
                     newFrame,
                   "Unexpected PopupSetFrame");
      aState.mPopupItems.containingBlock = newFrame;
      aState.mHavePendingPopupgroup = PR_FALSE;
    }
#endif /* MOZ_XUL */

    // Process the child content if requested
    nsFrameItems childItems;
    nsFrameConstructorSaveState absoluteSaveState;

    if (bits & FCDATA_FORCE_NULL_ABSPOS_CONTAINER) {
      aState.PushAbsoluteContainingBlock(nsnull, absoluteSaveState);
    } else if (!(bits & FCDATA_SKIP_ABSPOS_PUSH) && display->IsPositioned()) {
      aState.PushAbsoluteContainingBlock(newFrame, absoluteSaveState);
    }

    if (bits & FCDATA_USE_CHILD_ITEMS) {
      rv = ConstructFramesFromItemList(aState, aItem.mChildItems, newFrame,
                                       childItems);
    } else {
      // Process the child frames.
      rv = ProcessChildren(aState, content, styleContext, newFrame,
                           !(bits & FCDATA_DISALLOW_GENERATED_CONTENT),
                           childItems,
                           (bits & FCDATA_ALLOW_BLOCK_STYLES) != 0,
                           aItem.mPendingBinding);
    }

#ifdef MOZ_XUL
    // More icky XUL stuff
    if (aItem.mNameSpaceID == kNameSpaceID_XUL &&
        (aItem.mTag == nsGkAtoms::treechildren || // trees always need titletips
         content->HasAttr(kNameSpaceID_None, nsGkAtoms::tooltiptext) ||
         content->HasAttr(kNameSpaceID_None, nsGkAtoms::tooltip))) {
      nsIRootBox* rootBox = nsIRootBox::GetRootBox(mPresShell);
      if (rootBox) {
        rootBox->AddTooltipSupport(content);
      }
    }
#endif

#ifdef MOZ_MATHML
    if (NS_SUCCEEDED(rv) && (bits & FCDATA_WRAP_KIDS_IN_BLOCKS)) {
      nsFrameItems newItems;
      nsFrameItems currentBlock;
      nsIFrame* f;
      while ((f = childItems.FirstChild()) != nsnull) {
        PRBool wrapFrame = IsInlineFrame(f) || IsFrameSpecial(f);
        if (!wrapFrame) {
          rv = FlushAccumulatedBlock(aState, content, newFrame, &currentBlock, &newItems);
          if (NS_FAILED(rv))
            break;
        }

        childItems.RemoveFrame(f);
        if (wrapFrame) {
          currentBlock.AddChild(f);
        } else {
          newItems.AddChild(f);
        }
      }
      rv = FlushAccumulatedBlock(aState, content, newFrame, &currentBlock, &newItems);

      if (childItems.NotEmpty()) {
        // an error must have occurred, delete unprocessed frames
        childItems.DestroyFrames();
      }

      childItems = newItems;
    }
#endif

    // Set the frame's initial child list
    // Note that MathML depends on this being called even if
    // childItems is empty!
    newFrame->SetInitialChildList(nsnull, childItems);
  }

  NS_ASSERTION(newFrame->IsFrameOfType(nsIFrame::eLineParticipant) ==
               ((bits & FCDATA_IS_LINE_PARTICIPANT) != 0),
               "Incorrectly set FCDATA_IS_LINE_PARTICIPANT bits");

  if (aState.mSetPrimaryFrames && !(bits & FCDATA_SKIP_FRAMESET)) {
    aItem.mContent->SetPrimaryFrame(primaryFrame);
  }

  return NS_OK;
}

// after the node has been constructed and initialized create any
// anonymous content a node needs.
nsresult
nsCSSFrameConstructor::CreateAnonymousFrames(nsFrameConstructorState& aState,
                                             nsIContent*              aParent,
                                             nsIFrame*                aParentFrame,
                                             PendingBinding*          aPendingBinding,
                                             nsFrameItems&            aChildItems)
{
  nsAutoTArray<nsIContent*, 4> newAnonymousItems;
  nsresult rv = GetAnonymousContent(aParent, aParentFrame, newAnonymousItems);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 count = newAnonymousItems.Length();
  if (count == 0) {
    return NS_OK;
  }

  nsFrameConstructorState::PendingBindingAutoPusher pusher(aState,
                                                           aPendingBinding);

  nsIAnonymousContentCreator* creator = do_QueryFrame(aParentFrame);
  NS_ASSERTION(creator,
               "How can that happen if we have nodes to construct frames for?");

  for (PRUint32 i=0; i < count; i++) {
    nsIContent* content = newAnonymousItems[i];
    NS_ASSERTION(content, "null anonymous content?");

    nsIFrame* newFrame = creator->CreateFrameFor(content);
    if (newFrame) {
      NS_ASSERTION(content->GetPrimaryFrame(),
                   "Content must have a primary frame now");
      aChildItems.AddChild(newFrame);
    }
    else {
      // create the frame and attach it to our frame
      ConstructFrame(aState, content, aParentFrame, aChildItems);
    }
  }

  return NS_OK;
}

nsresult
nsCSSFrameConstructor::GetAnonymousContent(nsIContent* aParent,
                                           nsIFrame* aParentFrame,
                                           nsTArray<nsIContent*>& aContent)
{
  nsIAnonymousContentCreator* creator = do_QueryFrame(aParentFrame);
  if (!creator)
    return NS_OK;

  nsresult rv = creator->CreateAnonymousContent(aContent);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 count = aContent.Length();
  for (PRUint32 i=0; i < count; i++) {
    // get our child's content and set its parent to our content
    nsIContent* content = aContent[i];
    NS_ASSERTION(content, "null anonymous content?");

#ifdef MOZ_SVG
    // least-surprise CSS binding until we do the SVG specified
    // cascading rules for <svg:use> - bug 265894
    if (aParent &&
        aParent->NodeInfo()->Equals(nsGkAtoms::use, kNameSpaceID_SVG)) {
      content->SetFlags(NODE_IS_ANONYMOUS);
    } else
#endif
    {
      content->SetNativeAnonymous();
    }

    rv = content->BindToTree(mDocument, aParent, aParent, PR_TRUE);
    if (NS_FAILED(rv)) {
      content->UnbindFromTree();
      return rv;
    }
  }

  return NS_OK;
}

static
PRBool IsXULDisplayType(const nsStyleDisplay* aDisplay)
{
  return (aDisplay->mDisplay == NS_STYLE_DISPLAY_INLINE_BOX || 
#ifdef MOZ_XUL
          aDisplay->mDisplay == NS_STYLE_DISPLAY_INLINE_GRID || 
          aDisplay->mDisplay == NS_STYLE_DISPLAY_INLINE_STACK ||
#endif
          aDisplay->mDisplay == NS_STYLE_DISPLAY_BOX
#ifdef MOZ_XUL
          || aDisplay->mDisplay == NS_STYLE_DISPLAY_GRID ||
          aDisplay->mDisplay == NS_STYLE_DISPLAY_STACK ||
          aDisplay->mDisplay == NS_STYLE_DISPLAY_GRID_GROUP ||
          aDisplay->mDisplay == NS_STYLE_DISPLAY_GRID_LINE ||
          aDisplay->mDisplay == NS_STYLE_DISPLAY_DECK ||
          aDisplay->mDisplay == NS_STYLE_DISPLAY_POPUP ||
          aDisplay->mDisplay == NS_STYLE_DISPLAY_GROUPBOX
#endif
          );
}


// XUL frames are not allowed to be out of flow.
#define SIMPLE_XUL_FCDATA(_func)                                        \
  FCDATA_DECL(FCDATA_DISALLOW_OUT_OF_FLOW | FCDATA_SKIP_ABSPOS_PUSH,    \
              _func)
#define SCROLLABLE_XUL_FCDATA(_func)                                    \
  FCDATA_DECL(FCDATA_DISALLOW_OUT_OF_FLOW | FCDATA_SKIP_ABSPOS_PUSH |   \
              FCDATA_MAY_NEED_SCROLLFRAME, _func)
#define SIMPLE_XUL_CREATE(_tag, _func)            \
  { &nsGkAtoms::_tag, SIMPLE_XUL_FCDATA(_func) }
#define SCROLLABLE_XUL_CREATE(_tag, _func)            \
  { &nsGkAtoms::_tag, SCROLLABLE_XUL_FCDATA(_func) }
#define SIMPLE_XUL_INT_CREATE(_int, _func)      \
  { _int, SIMPLE_XUL_FCDATA(_func) }
#define SCROLLABLE_XUL_INT_CREATE(_int, _func)                          \
  { _int, SCROLLABLE_XUL_FCDATA(_func) }

static
nsIFrame* NS_NewGridBoxFrame(nsIPresShell* aPresShell,
                             nsStyleContext* aStyleContext)
{
  nsCOMPtr<nsIBoxLayout> layout;
  NS_NewGridLayout2(aPresShell, getter_AddRefs(layout));
  if (!layout) {
    return nsnull;
  }

  return NS_NewBoxFrame(aPresShell, aStyleContext, PR_FALSE, layout);
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindXULTagData(nsIContent* aContent,
                                      nsIAtom* aTag,
                                      PRInt32 aNameSpaceID,
                                      nsStyleContext* aStyleContext)
{
  if (aNameSpaceID != kNameSpaceID_XUL) {
    return nsnull;
  }

  static const FrameConstructionDataByTag sXULTagData[] = {
#ifdef MOZ_XUL
    SCROLLABLE_XUL_CREATE(button, NS_NewButtonBoxFrame),
    SCROLLABLE_XUL_CREATE(checkbox, NS_NewButtonBoxFrame),
    SCROLLABLE_XUL_CREATE(radio, NS_NewButtonBoxFrame),
    SCROLLABLE_XUL_CREATE(autorepeatbutton, NS_NewAutoRepeatBoxFrame),
    SCROLLABLE_XUL_CREATE(titlebar, NS_NewTitleBarFrame),
    SCROLLABLE_XUL_CREATE(resizer, NS_NewResizerFrame),
    SIMPLE_XUL_CREATE(image, NS_NewImageBoxFrame),
    SIMPLE_XUL_CREATE(spring, NS_NewLeafBoxFrame),
    SIMPLE_XUL_CREATE(spacer, NS_NewLeafBoxFrame),
    SIMPLE_XUL_CREATE(treechildren, NS_NewTreeBodyFrame),
    SIMPLE_XUL_CREATE(treecol, NS_NewTreeColFrame),
    SIMPLE_XUL_CREATE(text, NS_NewTextBoxFrame),
    SIMPLE_TAG_CHAIN(label, nsCSSFrameConstructor::FindXULLabelData),
    SIMPLE_TAG_CHAIN(description, nsCSSFrameConstructor::FindXULDescriptionData),
    SIMPLE_XUL_CREATE(menu, NS_NewMenuFrame),
    SIMPLE_XUL_CREATE(menubutton, NS_NewMenuFrame),
    SIMPLE_XUL_CREATE(menuitem, NS_NewMenuItemFrame),
#ifdef XP_MACOSX
    SIMPLE_TAG_CHAIN(menubar, nsCSSFrameConstructor::FindXULMenubarData),
#else
    SIMPLE_XUL_CREATE(menubar, NS_NewMenuBarFrame),
#endif /* XP_MACOSX */
    SIMPLE_TAG_CHAIN(popupgroup, nsCSSFrameConstructor::FindPopupGroupData),
    SIMPLE_XUL_CREATE(iframe, NS_NewSubDocumentFrame),
    SIMPLE_XUL_CREATE(editor, NS_NewSubDocumentFrame),
    SIMPLE_XUL_CREATE(browser, NS_NewSubDocumentFrame),
    SIMPLE_XUL_CREATE(progressmeter, NS_NewProgressMeterFrame),
    SIMPLE_XUL_CREATE(splitter, NS_NewSplitterFrame),
    SIMPLE_TAG_CHAIN(listboxbody,
                     nsCSSFrameConstructor::FindXULListBoxBodyData),
    SIMPLE_TAG_CHAIN(listitem, nsCSSFrameConstructor::FindXULListItemData),
#endif /* MOZ_XUL */
    SIMPLE_XUL_CREATE(slider, NS_NewSliderFrame),
    SIMPLE_XUL_CREATE(scrollbar, NS_NewScrollbarFrame),
    SIMPLE_XUL_CREATE(scrollbarbutton, NS_NewScrollbarButtonFrame)
};

  return FindDataByTag(aTag, aContent, aStyleContext, sXULTagData,
                       NS_ARRAY_LENGTH(sXULTagData));
}

#ifdef MOZ_XUL
/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindPopupGroupData(nsIContent* aContent,
                                          nsStyleContext* /* unused */)
{
  if (!aContent->IsRootOfNativeAnonymousSubtree()) {
    return nsnull;
  }

  static const FrameConstructionData sPopupSetData =
    SIMPLE_XUL_FCDATA(NS_NewPopupSetFrame);
  return &sPopupSetData;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData
nsCSSFrameConstructor::sXULTextBoxData = SIMPLE_XUL_FCDATA(NS_NewTextBoxFrame);

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindXULLabelData(nsIContent* aContent,
                                        nsStyleContext* /* unused */)
{
  if (aContent->HasAttr(kNameSpaceID_None, nsGkAtoms::value)) {
    return &sXULTextBoxData;
  }

  static const FrameConstructionData sLabelData =
    SIMPLE_XUL_FCDATA(NS_NewXULLabelFrame);
  return &sLabelData;
}

static nsIFrame*
NS_NewXULDescriptionFrame(nsIPresShell* aPresShell, nsStyleContext *aContext)
{
  // XXXbz do we really need to set those flags?  If the parent is not
  // a block we'll get them anyway, and if it is, do we want them?
  return NS_NewBlockFrame(aPresShell, aContext,
                          NS_BLOCK_FLOAT_MGR | NS_BLOCK_MARGIN_ROOT);
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindXULDescriptionData(nsIContent* aContent,
                                              nsStyleContext* /* unused */)
{
  if (aContent->HasAttr(kNameSpaceID_None, nsGkAtoms::value)) {
    return &sXULTextBoxData;
  }

  static const FrameConstructionData sDescriptionData =
    SIMPLE_XUL_FCDATA(NS_NewXULDescriptionFrame);
  return &sDescriptionData;
}

#ifdef XP_MACOSX
/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindXULMenubarData(nsIContent* aContent,
                                          nsStyleContext* aStyleContext)
{
  nsCOMPtr<nsISupports> container =
    aStyleContext->PresContext()->GetContainer();
  if (container) {
    nsCOMPtr<nsIDocShellTreeItem> treeItem(do_QueryInterface(container));
    if (treeItem) {
      PRInt32 type;
      treeItem->GetItemType(&type);
      if (nsIDocShellTreeItem::typeChrome == type) {
        nsCOMPtr<nsIDocShellTreeItem> parent;
        treeItem->GetParent(getter_AddRefs(parent));
        if (!parent) {
          // This is the root.  Suppress the menubar, since on Mac
          // window menus are not attached to the window.
          static const FrameConstructionData sSuppressData = SUPPRESS_FCDATA();
          return &sSuppressData;
        }
      }
    }
  }

  static const FrameConstructionData sMenubarData =
    SIMPLE_XUL_FCDATA(NS_NewMenuBarFrame);
  return &sMenubarData;
}
#endif /* XP_MACOSX */

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindXULListBoxBodyData(nsIContent* aContent,
                                              nsStyleContext* aStyleContext)
{
  if (aStyleContext->GetStyleDisplay()->mDisplay !=
        NS_STYLE_DISPLAY_GRID_GROUP) {
    return nsnull;
  }

  static const FrameConstructionData sListBoxBodyData =
    SCROLLABLE_XUL_FCDATA(NS_NewListBoxBodyFrame);
  return &sListBoxBodyData;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindXULListItemData(nsIContent* aContent,
                                           nsStyleContext* aStyleContext)
{
  if (aStyleContext->GetStyleDisplay()->mDisplay !=
        NS_STYLE_DISPLAY_GRID_LINE) {
    return nsnull;
  }

  static const FrameConstructionData sListItemData =
    SCROLLABLE_XUL_FCDATA(NS_NewListItemFrame);
  return &sListItemData;
}

#endif /* MOZ_XUL */

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindXULDisplayData(const nsStyleDisplay* aDisplay,
                                          nsIContent* aContent,
                                          nsStyleContext* aStyleContext)
{
  static const FrameConstructionDataByInt sXULDisplayData[] = {
    SCROLLABLE_XUL_INT_CREATE(NS_STYLE_DISPLAY_INLINE_BOX, NS_NewBoxFrame),
    SCROLLABLE_XUL_INT_CREATE(NS_STYLE_DISPLAY_BOX, NS_NewBoxFrame),
#ifdef MOZ_XUL
    SCROLLABLE_XUL_INT_CREATE(NS_STYLE_DISPLAY_INLINE_GRID, NS_NewGridBoxFrame),
    SCROLLABLE_XUL_INT_CREATE(NS_STYLE_DISPLAY_GRID, NS_NewGridBoxFrame),
    SCROLLABLE_XUL_INT_CREATE(NS_STYLE_DISPLAY_GRID_GROUP,
                              NS_NewGridRowGroupFrame),
    SCROLLABLE_XUL_INT_CREATE(NS_STYLE_DISPLAY_GRID_LINE,
                              NS_NewGridRowLeafFrame),
    SIMPLE_XUL_INT_CREATE(NS_STYLE_DISPLAY_DECK, NS_NewDeckFrame),
    SCROLLABLE_XUL_INT_CREATE(NS_STYLE_DISPLAY_GROUPBOX, NS_NewGroupBoxFrame),
    SCROLLABLE_XUL_INT_CREATE(NS_STYLE_DISPLAY_INLINE_STACK, NS_NewStackFrame),
    SCROLLABLE_XUL_INT_CREATE(NS_STYLE_DISPLAY_STACK, NS_NewStackFrame),
    { NS_STYLE_DISPLAY_POPUP,
      FCDATA_DECL(FCDATA_DISALLOW_OUT_OF_FLOW | FCDATA_IS_POPUP |
                  FCDATA_SKIP_ABSPOS_PUSH, NS_NewMenuPopupFrame) }
#endif /* MOZ_XUL */
  };

  // Processing by display here:
  return FindDataByInt(aDisplay->mDisplay, aContent, aStyleContext,
                       sXULDisplayData, NS_ARRAY_LENGTH(sXULDisplayData));
}

nsresult
nsCSSFrameConstructor::AddLazyChildren(nsIContent* aContent,
                                       nsLazyFrameConstructionCallback* aCallback,
                                       void* aArg, PRBool aIsSynch)
{
  nsCOMPtr<nsIRunnable> event =
    new LazyGenerateChildrenEvent(aContent, mPresShell, aCallback, aArg);
  return aIsSynch ? event->Run() :
                    NS_DispatchToCurrentThread(event);
}

already_AddRefed<nsStyleContext>
nsCSSFrameConstructor::BeginBuildingScrollFrame(nsFrameConstructorState& aState,
                                                nsIContent*              aContent,
                                                nsStyleContext*          aContentStyle,
                                                nsIFrame*                aParentFrame,
                                                nsIAtom*                 aScrolledPseudo,
                                                PRBool                   aIsRoot,
                                                nsIFrame*&               aNewFrame)
{
  nsIFrame* gfxScrollFrame = aNewFrame;

  nsFrameItems anonymousItems;

  nsRefPtr<nsStyleContext> contentStyle = aContentStyle;

  if (!gfxScrollFrame) {
    // Build a XULScrollFrame when the child is a box, otherwise an
    // HTMLScrollFrame
    // XXXbz this is the lone remaining consumer of IsXULDisplayType.
    // I wonder whether we can eliminate that somehow.
    if (IsXULDisplayType(aContentStyle->GetStyleDisplay())) {
      gfxScrollFrame = NS_NewXULScrollFrame(mPresShell, contentStyle, aIsRoot);
    } else {
      gfxScrollFrame = NS_NewHTMLScrollFrame(mPresShell, contentStyle, aIsRoot);
    }

    InitAndRestoreFrame(aState, aContent, aParentFrame, nsnull, gfxScrollFrame);

    // Create a view
    nsHTMLContainerFrame::CreateViewForFrame(gfxScrollFrame, PR_FALSE);
  }

  // if there are any anonymous children for the scroll frame, create
  // frames for them.
  // Pass a null pending binding: we don't care how constructors for any of
  // this anonymous content order with anything else.  It's never been
  // consistent anyway.
  CreateAnonymousFrames(aState, aContent, gfxScrollFrame, nsnull,
                        anonymousItems);

  aNewFrame = gfxScrollFrame;

  // we used the style that was passed in. So resolve another one.
  nsStyleSet *styleSet = mPresShell->StyleSet();
  nsStyleContext* aScrolledChildStyle =
    styleSet->ResolveAnonymousBoxStyle(aScrolledPseudo, contentStyle).get();

  if (gfxScrollFrame) {
     gfxScrollFrame->SetInitialChildList(nsnull, anonymousItems);
  }

  return aScrolledChildStyle;
}

void
nsCSSFrameConstructor::FinishBuildingScrollFrame(nsIFrame* aScrollFrame,
                                                 nsIFrame* aScrolledFrame)
{
  nsFrameList scrolled(aScrolledFrame, aScrolledFrame);
  aScrollFrame->AppendFrames(nsnull, scrolled);
}


/**
 * Called to wrap a gfx scrollframe around a frame. The hierarchy will look like this
 *
 * ------- for gfx scrollbars ------
 *
 *
 *            ScrollFrame
 *                 ^
 *                 |
 *               Frame (scrolled frame you passed in)
 *
 *
 *-----------------------------------
 * LEGEND:
 * 
 * ScrollFrame: This is a frame that manages gfx cross platform frame based scrollbars.
 *
 * @param aContent the content node of the child to wrap.
 * @param aScrolledFrame The frame of the content to wrap. This should not be
 *                    Initialized. This method will initialize it with a scrolled pseudo
 *                    and no nsIContent. The content will be attached to the scrollframe 
 *                    returned.
 * @param aContentStyle the style context that has already been resolved for the content being passed in.
 *
 * @param aParentFrame The parent to attach the scroll frame to
 *
 * @param aNewFrame The new scrollframe or gfx scrollframe that we create. It will contain the
 *                  scrolled frame you passed in. (returned)
 *                  If this is not null, we'll just use it
 * @param aScrolledContentStyle the style that was resolved for the scrolled frame. (returned)
 */
nsresult
nsCSSFrameConstructor::BuildScrollFrame(nsFrameConstructorState& aState,
                                        nsIContent*              aContent,
                                        nsStyleContext*          aContentStyle,
                                        nsIFrame*                aScrolledFrame,
                                        nsIFrame*                aParentFrame,
                                        nsIFrame*&               aNewFrame)
{
    nsRefPtr<nsStyleContext> scrolledContentStyle =
      BeginBuildingScrollFrame(aState, aContent, aContentStyle, aParentFrame,
                               nsCSSAnonBoxes::scrolledContent,
                               PR_FALSE, aNewFrame);
    
    aScrolledFrame->SetStyleContextWithoutNotification(scrolledContentStyle);
    InitAndRestoreFrame(aState, aContent, aNewFrame, nsnull, aScrolledFrame);

    FinishBuildingScrollFrame(aNewFrame, aScrolledFrame);
    return NS_OK;
}

const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindDisplayData(const nsStyleDisplay* aDisplay,
                                       nsIContent* aContent,
                                       nsStyleContext* aStyleContext)
{
  PR_STATIC_ASSERT(eParentTypeCount < (1 << (32 - FCDATA_PARENT_TYPE_OFFSET)));

  // The style system ensures that floated and positioned frames are
  // block-level.
  NS_ASSERTION(!(aDisplay->IsFloating() ||
                 aDisplay->IsAbsolutelyPositioned()) ||
               aDisplay->IsBlockOutside(),
               "Style system did not apply CSS2.1 section 9.7 fixups");

  // If this is "body", try propagating its scroll style to the viewport
  // Note that we need to do this even if the body is NOT scrollable;
  // it might have dynamically changed from scrollable to not scrollable,
  // and that might need to be propagated.
  // XXXbz is this the right place to do this?  If this code moves,
  // make this function static.
  PRBool propagatedScrollToViewport = PR_FALSE;
  if (aContent->NodeInfo()->Equals(nsGkAtoms::body) &&
      aContent->IsHTML()) {
    propagatedScrollToViewport =
      PropagateScrollToViewport() == aContent;
  }

  // If the frame is a block-level frame and is scrollable, then wrap it
  // in a scroll frame.
  // XXX Ignore tables for the time being
  // XXXbz it would be nice to combine this with the other block
  // case... Think about how do do this?
  if (aDisplay->IsBlockInside() &&
      aDisplay->IsScrollableOverflow() &&
      !propagatedScrollToViewport) {
    static const FrameConstructionData sScrollableBlockData =
      FULL_CTOR_FCDATA(0, &nsCSSFrameConstructor::ConstructScrollableBlock);
    return &sScrollableBlockData;
  }

  // Handle various non-scrollable blocks
  if (aDisplay->IsBlockInside() ||
      NS_STYLE_DISPLAY_RUN_IN == aDisplay->mDisplay ||
      NS_STYLE_DISPLAY_COMPACT == aDisplay->mDisplay) {  
    static const FrameConstructionData sNonScrollableBlockData =
      FULL_CTOR_FCDATA(0, &nsCSSFrameConstructor::ConstructNonScrollableBlock);
    return &sNonScrollableBlockData;
  }

  static const FrameConstructionDataByInt sDisplayData[] = {
    // To keep the hash table small don't add inline frames (they're
    // typically things like FONT and B), because we can quickly
    // find them if we need to.
    // XXXbz the "quickly" part is a bald-faced lie!
    { NS_STYLE_DISPLAY_INLINE,
      FULL_CTOR_FCDATA(FCDATA_IS_INLINE | FCDATA_IS_LINE_PARTICIPANT,
                       &nsCSSFrameConstructor::ConstructInline) },
    { NS_STYLE_DISPLAY_MARKER,
      FULL_CTOR_FCDATA(FCDATA_IS_INLINE | FCDATA_IS_LINE_PARTICIPANT,
                       &nsCSSFrameConstructor::ConstructInline) },
    { NS_STYLE_DISPLAY_TABLE,
      FULL_CTOR_FCDATA(0, &nsCSSFrameConstructor::ConstructTable) },
    { NS_STYLE_DISPLAY_INLINE_TABLE,
      FULL_CTOR_FCDATA(0, &nsCSSFrameConstructor::ConstructTable) },
    { NS_STYLE_DISPLAY_TABLE_CAPTION,
      FCDATA_DECL(FCDATA_IS_TABLE_PART | FCDATA_ALLOW_BLOCK_STYLES |
                  FCDATA_DISALLOW_OUT_OF_FLOW | FCDATA_SKIP_ABSPOS_PUSH |
                  FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable),
                  NS_NewTableCaptionFrame) },
    { NS_STYLE_DISPLAY_TABLE_ROW_GROUP,
      FCDATA_DECL(FCDATA_IS_TABLE_PART | FCDATA_DISALLOW_OUT_OF_FLOW |
                  FCDATA_SKIP_ABSPOS_PUSH |
                  FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable),
                  NS_NewTableRowGroupFrame) },
    { NS_STYLE_DISPLAY_TABLE_HEADER_GROUP,
      FCDATA_DECL(FCDATA_IS_TABLE_PART | FCDATA_DISALLOW_OUT_OF_FLOW |
                  FCDATA_SKIP_ABSPOS_PUSH |
                  FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable),
                  NS_NewTableRowGroupFrame) },
    { NS_STYLE_DISPLAY_TABLE_FOOTER_GROUP,
      FCDATA_DECL(FCDATA_IS_TABLE_PART | FCDATA_DISALLOW_OUT_OF_FLOW |
                  FCDATA_SKIP_ABSPOS_PUSH |
                  FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable),
                  NS_NewTableRowGroupFrame) },
    { NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP,
      FCDATA_DECL(FCDATA_IS_TABLE_PART | FCDATA_DISALLOW_OUT_OF_FLOW |
                  FCDATA_SKIP_ABSPOS_PUSH |
                  FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable),
                  NS_NewTableColGroupFrame) },
    { NS_STYLE_DISPLAY_TABLE_COLUMN,
      FULL_CTOR_FCDATA(FCDATA_IS_TABLE_PART |
                       FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeColGroup),
                       &nsCSSFrameConstructor::ConstructTableCol) },
    { NS_STYLE_DISPLAY_TABLE_ROW,
      FULL_CTOR_FCDATA(FCDATA_IS_TABLE_PART |
                       FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRowGroup),
                       &nsCSSFrameConstructor::ConstructTableRow) },
    { NS_STYLE_DISPLAY_TABLE_CELL,
      FULL_CTOR_FCDATA(FCDATA_IS_TABLE_PART |
                       FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRow),
                       &nsCSSFrameConstructor::ConstructTableCell) }
  };

  return FindDataByInt(aDisplay->mDisplay, aContent, aStyleContext,
                       sDisplayData, NS_ARRAY_LENGTH(sDisplayData));
}

nsresult
nsCSSFrameConstructor::ConstructScrollableBlock(nsFrameConstructorState& aState,
                                                FrameConstructionItem&   aItem,
                                                nsIFrame*                aParentFrame,
                                                const nsStyleDisplay*    aDisplay,
                                                nsFrameItems&            aFrameItems,
                                                nsIFrame**               aNewFrame)
{
  nsIContent* const content = aItem.mContent;
  nsStyleContext* const styleContext = aItem.mStyleContext;

  *aNewFrame = nsnull;
  nsRefPtr<nsStyleContext> scrolledContentStyle
    = BeginBuildingScrollFrame(aState, content, styleContext,
                               aState.GetGeometricParent(aDisplay, aParentFrame),
                               nsCSSAnonBoxes::scrolledContent,
                               PR_FALSE, *aNewFrame);

  // Create our block frame
  // pass a temporary stylecontext, the correct one will be set later
  nsIFrame* scrolledFrame =
    NS_NewBlockFormattingContext(mPresShell, styleContext);

  nsFrameItems blockItem;
  nsresult rv = ConstructBlock(aState,
                               scrolledContentStyle->GetStyleDisplay(), content,
                               *aNewFrame, *aNewFrame, scrolledContentStyle,
                               &scrolledFrame, blockItem, aDisplay->IsPositioned(),
                               aItem.mPendingBinding);
  if (NS_UNLIKELY(NS_FAILED(rv))) {
    // XXXbz any cleanup needed here?
    return rv;
  }

  NS_ASSERTION(blockItem.FirstChild() == scrolledFrame,
               "Scrollframe's frameItems should be exactly the scrolled frame");
  FinishBuildingScrollFrame(*aNewFrame, scrolledFrame);

  rv = aState.AddChild(*aNewFrame, aFrameItems, content, styleContext,
                       aParentFrame);
  return rv;
}

nsresult
nsCSSFrameConstructor::ConstructNonScrollableBlock(nsFrameConstructorState& aState,
                                                   FrameConstructionItem&   aItem,
                                                   nsIFrame*                aParentFrame,
                                                   const nsStyleDisplay*    aDisplay,
                                                   nsFrameItems&            aFrameItems,
                                                   nsIFrame**               aNewFrame)
{
  nsStyleContext* const styleContext = aItem.mStyleContext;

  if (aDisplay->IsAbsolutelyPositioned() ||
      aDisplay->IsFloating() ||
      NS_STYLE_DISPLAY_INLINE_BLOCK == aDisplay->mDisplay) {
    *aNewFrame = NS_NewBlockFormattingContext(mPresShell, styleContext);
  } else {
    *aNewFrame = NS_NewBlockFrame(mPresShell, styleContext);
  }

  return ConstructBlock(aState, aDisplay, aItem.mContent,
                        aState.GetGeometricParent(aDisplay, aParentFrame),
                        aParentFrame, styleContext, aNewFrame,
                        aFrameItems, aDisplay->IsPositioned(),
                        aItem.mPendingBinding);
}


nsresult 
nsCSSFrameConstructor::InitAndRestoreFrame(const nsFrameConstructorState& aState,
                                           nsIContent*              aContent,
                                           nsIFrame*                aParentFrame,
                                           nsIFrame*                aPrevInFlow,
                                           nsIFrame*                aNewFrame,
                                           PRBool                   aAllowCounters)
{
  NS_PRECONDITION(mUpdateCount != 0,
                  "Should be in an update while creating frames");
  
  nsresult rv = NS_OK;
  
  NS_ASSERTION(aNewFrame, "Null frame cannot be initialized");
  if (!aNewFrame)
    return NS_ERROR_NULL_POINTER;

  // Initialize the frame
  rv = aNewFrame->Init(aContent, aParentFrame, aPrevInFlow);
  aNewFrame->AddStateBits(aState.mAdditionalStateBits);

  if (aState.mFrameState && aState.mFrameManager) {
    // Restore frame state for just the newly created frame.
    aState.mFrameManager->RestoreFrameStateFor(aNewFrame, aState.mFrameState);
  }

  if (aAllowCounters && !aPrevInFlow &&
      mCounterManager.AddCounterResetsAndIncrements(aNewFrame)) {
    CountersDirty();
  }

  return rv;
}

already_AddRefed<nsStyleContext>
nsCSSFrameConstructor::ResolveStyleContext(nsIFrame*         aParentFrame,
                                           nsIContent*       aContent)
{
  nsStyleContext* parentStyleContext = nsnull;
  NS_ASSERTION(aContent->GetParent(), "Must have parent here");

  aParentFrame = nsFrame::CorrectStyleParentFrame(aParentFrame, nsnull);

  if (aParentFrame) {
    // Resolve the style context based on the content object and the parent
    // style context
    parentStyleContext = aParentFrame->GetStyleContext();
  } else {
    // Perhaps aParentFrame is a canvasFrame and we're replicating
    // fixed-pos frames.
    // XXX should we create a way to tell ConstructFrame which style
    // context to use, and pass it the style context for the
    // previous page's fixed-pos frame?
  }

  return ResolveStyleContext(parentStyleContext, aContent);
}

already_AddRefed<nsStyleContext>
nsCSSFrameConstructor::ResolveStyleContext(nsStyleContext* aParentStyleContext,
                                           nsIContent* aContent)
{
  nsStyleSet *styleSet = mPresShell->StyleSet();

  if (aContent->IsElement()) {
    return styleSet->ResolveStyleFor(aContent->AsElement(), aParentStyleContext);
  }

  NS_ASSERTION(aContent->IsNodeOfType(nsINode::eTEXT),
               "shouldn't waste time creating style contexts for "
               "comments and processing instructions");

  return styleSet->ResolveStyleForNonElement(aParentStyleContext);
}

// MathML Mod - RBS
#ifdef MOZ_MATHML
nsresult
nsCSSFrameConstructor::FlushAccumulatedBlock(nsFrameConstructorState& aState,
                                             nsIContent* aContent,
                                             nsIFrame* aParentFrame,
                                             nsFrameItems* aBlockItems,
                                             nsFrameItems* aNewItems)
{
  if (aBlockItems->IsEmpty()) {
    // Nothing to do
    return NS_OK;
  }

  nsStyleContext* parentContext =
    nsFrame::CorrectStyleParentFrame(aParentFrame,
                                     nsCSSAnonBoxes::mozMathMLAnonymousBlock)->GetStyleContext(); 
  nsStyleSet *styleSet = mPresShell->StyleSet();
  nsRefPtr<nsStyleContext> blockContext;
  blockContext = styleSet->
    ResolveAnonymousBoxStyle(nsCSSAnonBoxes::mozMathMLAnonymousBlock,
                             parentContext);

  // then, create a block frame that will wrap the child frames. Make it a
  // MathML frame so that Get(Absolute/Float)ContainingBlockFor know that this
  // is not a suitable block.
  nsIFrame* blockFrame = NS_NewMathMLmathBlockFrame(mPresShell, blockContext,
                          NS_BLOCK_FLOAT_MGR | NS_BLOCK_MARGIN_ROOT);
  if (NS_UNLIKELY(!blockFrame))
    return NS_ERROR_OUT_OF_MEMORY;

  InitAndRestoreFrame(aState, aContent, aParentFrame, nsnull, blockFrame);
  ReparentFrames(aState.mFrameManager, blockFrame, *aBlockItems);
  // abs-pos and floats are disabled in MathML children so we don't have to
  // worry about messing up those.
  blockFrame->SetInitialChildList(nsnull, *aBlockItems);
  NS_ASSERTION(aBlockItems->IsEmpty(), "What happened?");
  aBlockItems->Clear();
  aNewItems->AddChild(blockFrame);
  return NS_OK;
}

// Only <math> elements can be floated or positioned.  All other MathML
// should be in-flow.
#define SIMPLE_MATHML_CREATE(_tag, _func)                               \
  { &nsGkAtoms::_tag,                                                   \
      FCDATA_DECL(FCDATA_DISALLOW_OUT_OF_FLOW |                         \
                  FCDATA_FORCE_NULL_ABSPOS_CONTAINER |                  \
                  FCDATA_WRAP_KIDS_IN_BLOCKS, _func) }

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindMathMLData(nsIContent* aContent,
                                      nsIAtom* aTag,
                                      PRInt32 aNameSpaceID,
                                      nsStyleContext* aStyleContext)
{
  // Make sure that we remain confined in the MathML world
  if (aNameSpaceID != kNameSpaceID_MathML) 
    return nsnull;

  // Handle <math> specially, because it sometimes produces inlines
  if (aTag == nsGkAtoms::math) {
    // This needs to match the test in EnsureBlockDisplay in
    // nsRuleNode.cpp.  Though the behavior here for the display:table
    // case is pretty weird...
    if (aStyleContext->GetStyleDisplay()->IsBlockOutside()) {
      static const FrameConstructionData sBlockMathData =
        FCDATA_DECL(FCDATA_FORCE_NULL_ABSPOS_CONTAINER |
                    FCDATA_WRAP_KIDS_IN_BLOCKS,
                    NS_CreateNewMathMLmathBlockFrame);
      return &sBlockMathData;
    }

    static const FrameConstructionData sInlineMathData =
      FCDATA_DECL(FCDATA_FORCE_NULL_ABSPOS_CONTAINER |
                  FCDATA_IS_LINE_PARTICIPANT |
                  FCDATA_WRAP_KIDS_IN_BLOCKS,
                  NS_NewMathMLmathInlineFrame);
    return &sInlineMathData;
  }
      

  static const FrameConstructionDataByTag sMathMLData[] = {
    SIMPLE_MATHML_CREATE(mi_, NS_NewMathMLTokenFrame),
    SIMPLE_MATHML_CREATE(mn_, NS_NewMathMLTokenFrame),
    SIMPLE_MATHML_CREATE(ms_, NS_NewMathMLTokenFrame),
    SIMPLE_MATHML_CREATE(mtext_, NS_NewMathMLTokenFrame),
    SIMPLE_MATHML_CREATE(mo_, NS_NewMathMLmoFrame),
    SIMPLE_MATHML_CREATE(mfrac_, NS_NewMathMLmfracFrame),
    SIMPLE_MATHML_CREATE(msup_, NS_NewMathMLmsupFrame),
    SIMPLE_MATHML_CREATE(msub_, NS_NewMathMLmsubFrame),
    SIMPLE_MATHML_CREATE(msubsup_, NS_NewMathMLmsubsupFrame),
    SIMPLE_MATHML_CREATE(munder_, NS_NewMathMLmunderFrame),
    SIMPLE_MATHML_CREATE(mover_, NS_NewMathMLmoverFrame),
    SIMPLE_MATHML_CREATE(munderover_, NS_NewMathMLmunderoverFrame),
    SIMPLE_MATHML_CREATE(mphantom_, NS_NewMathMLmphantomFrame),
    SIMPLE_MATHML_CREATE(mpadded_, NS_NewMathMLmpaddedFrame),
    SIMPLE_MATHML_CREATE(mspace_, NS_NewMathMLmspaceFrame),
    SIMPLE_MATHML_CREATE(none, NS_NewMathMLmspaceFrame),
    SIMPLE_MATHML_CREATE(mprescripts_, NS_NewMathMLmspaceFrame),
    SIMPLE_MATHML_CREATE(mfenced_, NS_NewMathMLmfencedFrame),
    SIMPLE_MATHML_CREATE(mmultiscripts_, NS_NewMathMLmmultiscriptsFrame),
    SIMPLE_MATHML_CREATE(mstyle_, NS_NewMathMLmstyleFrame),
    SIMPLE_MATHML_CREATE(msqrt_, NS_NewMathMLmsqrtFrame),
    SIMPLE_MATHML_CREATE(mroot_, NS_NewMathMLmrootFrame),
    SIMPLE_MATHML_CREATE(maction_, NS_NewMathMLmactionFrame),
    SIMPLE_MATHML_CREATE(mrow_, NS_NewMathMLmrowFrame),
    SIMPLE_MATHML_CREATE(merror_, NS_NewMathMLmrowFrame),
    SIMPLE_MATHML_CREATE(menclose_, NS_NewMathMLmencloseFrame),
    SIMPLE_MATHML_CREATE(semantics_, NS_NewMathMLsemanticsFrame)
  };

  return FindDataByTag(aTag, aContent, aStyleContext, sMathMLData,
                       NS_ARRAY_LENGTH(sMathMLData));
}
#endif // MOZ_MATHML

#ifdef MOZ_SVG
// Only outer <svg> elements can be floated or positioned.  All other SVG
// should be in-flow.
#define SIMPLE_SVG_FCDATA(_func)                                        \
  FCDATA_DECL(FCDATA_DISALLOW_OUT_OF_FLOW |                             \
              FCDATA_SKIP_ABSPOS_PUSH |                                 \
              FCDATA_DISALLOW_GENERATED_CONTENT,  _func)
#define SIMPLE_SVG_CREATE(_tag, _func)            \
  { &nsGkAtoms::_tag, SIMPLE_SVG_FCDATA(_func) }

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindSVGData(nsIContent* aContent,
                                   nsIAtom* aTag,
                                   PRInt32 aNameSpaceID,
                                   nsIFrame* aParentFrame,
                                   nsStyleContext* aStyleContext)
{
  if (aNameSpaceID != kNameSpaceID_SVG || !NS_SVGEnabled()) {
    return nsnull;
  }

  static const FrameConstructionData sSuppressData = SUPPRESS_FCDATA();
  static const FrameConstructionData sGenericContainerData =
    SIMPLE_SVG_FCDATA(NS_NewSVGGenericContainerFrame);

  PRBool parentIsSVG = PR_FALSE;
  nsIContent* parentContent =
    aParentFrame ? aParentFrame->GetContent() : nsnull;
  // XXXbz should this really be based on the XBL-resolved tag of the parent
  // frame's content?  Should it not be based on the type of the parent frame
  // (e.g. whether it's an SVG frame)?
  if (parentContent) {
    PRInt32 parentNSID;
    nsIAtom* parentTag =
      parentContent->GetOwnerDoc()->BindingManager()->
        ResolveTag(aParentFrame->GetContent(), &parentNSID);

    // It's not clear whether the SVG spec intends to allow any SVG
    // content within svg:foreignObject at all (SVG 1.1, section
    // 23.2), but if it does, it better be svg:svg.  So given that
    // we're allowing it, treat it as a non-SVG parent.
    parentIsSVG = parentNSID == kNameSpaceID_SVG &&
                  parentTag != nsGkAtoms::foreignObject;
  }

  if ((aTag != nsGkAtoms::svg && !parentIsSVG) ||
      (aTag == nsGkAtoms::desc || aTag == nsGkAtoms::title)) {
    // Sections 5.1 and G.4 of SVG 1.1 say that SVG elements other than
    // svg:svg not contained within svg:svg are incorrect, although they
    // don't seem to specify error handling.  Ignore them, since many of
    // our frame classes can't deal.  It *may* be that the document
    // should at that point be considered in error according to F.2, but
    // it's hard to tell.
    //
    // Style mutation can't change this situation, so don't bother
    // adding to the undisplayed content map.
    //
    // We don't currently handle any UI for desc/title
    return &sSuppressData;
  }

  // We don't need frames for animation elements
  if (aContent->IsNodeOfType(nsINode::eANIMATION)) {
    return &sSuppressData;
  }

  // Reduce the number of frames we create unnecessarily. Note that this is not
  // where we select which frame in a <switch> to render! That happens in
  // nsSVGSwitchFrame::PaintSVG.
  if (!nsSVGFeatures::PassesConditionalProcessingTests(aContent)) {
    // Note that just returning is probably not right.  According
    // to the spec, <use> is allowed to use an element that fails its
    // conditional, but because we never actually create the frame when
    // a conditional fails and when we use GetReferencedFrame to find the
    // references, things don't work right.
    // XXX FIXME XXX
    return &sSuppressData;
  }

  // Special case for aTag == nsGkAtoms::svg because we don't want to
  // have to recompute parentIsSVG for it.
  if (aTag == nsGkAtoms::svg) {
    if (parentIsSVG) {
      static const FrameConstructionData sInnerSVGData =
        SIMPLE_SVG_FCDATA(NS_NewSVGInnerSVGFrame);
      return &sInnerSVGData;
    }

    static const FrameConstructionData sOuterSVGData =
      FCDATA_DECL(FCDATA_FORCE_VIEW | FCDATA_SKIP_ABSPOS_PUSH |
                  FCDATA_DISALLOW_GENERATED_CONTENT,
                  NS_NewSVGOuterSVGFrame);
    return &sOuterSVGData;
  }

  // Special cases for text/tspan/textpath, because the kind of frame
  // they get depends on the parent frame.
  if (aTag == nsGkAtoms::text) {
    NS_ASSERTION(aParentFrame, "Should have aParentFrame here");
    nsIFrame *ancestorFrame =
      nsSVGUtils::GetFirstNonAAncestorFrame(aParentFrame);
    if (ancestorFrame) {
      nsSVGTextContainerFrame* metrics = do_QueryFrame(ancestorFrame);
      // Text cannot be nested
      if (metrics) {
        return &sGenericContainerData;
      }
    }
  }
  else if (aTag == nsGkAtoms::tspan || aTag == nsGkAtoms::altGlyph) {
    NS_ASSERTION(aParentFrame, "Should have aParentFrame here");
    nsIFrame *ancestorFrame =
      nsSVGUtils::GetFirstNonAAncestorFrame(aParentFrame);
    if (ancestorFrame) {
      nsSVGTextContainerFrame* metrics = do_QueryFrame(ancestorFrame);
      if (!metrics) {
        return &sGenericContainerData;
      }
    }
  }
  else if (aTag == nsGkAtoms::textPath) {
    NS_ASSERTION(aParentFrame, "Should have aParentFrame here");
    nsIFrame *ancestorFrame =
      nsSVGUtils::GetFirstNonAAncestorFrame(aParentFrame);
    if (!ancestorFrame ||
        ancestorFrame->GetType() != nsGkAtoms::svgTextFrame) {
      return &sGenericContainerData;
    }
  }

  static const FrameConstructionDataByTag sSVGData[] = {
    SIMPLE_SVG_CREATE(g, NS_NewSVGGFrame),
    SIMPLE_SVG_CREATE(svgSwitch, NS_NewSVGSwitchFrame),
    SIMPLE_SVG_CREATE(polygon, NS_NewSVGPathGeometryFrame),
    SIMPLE_SVG_CREATE(polyline, NS_NewSVGPathGeometryFrame),
    SIMPLE_SVG_CREATE(circle, NS_NewSVGPathGeometryFrame),
    SIMPLE_SVG_CREATE(ellipse, NS_NewSVGPathGeometryFrame),
    SIMPLE_SVG_CREATE(line, NS_NewSVGPathGeometryFrame),
    SIMPLE_SVG_CREATE(rect, NS_NewSVGPathGeometryFrame),
    SIMPLE_SVG_CREATE(path, NS_NewSVGPathGeometryFrame),
    SIMPLE_SVG_CREATE(defs, NS_NewSVGContainerFrame),
    { &nsGkAtoms::foreignObject,
      FULL_CTOR_FCDATA(FCDATA_DISALLOW_OUT_OF_FLOW,
                       &nsCSSFrameConstructor::ConstructSVGForeignObjectFrame) },
    SIMPLE_SVG_CREATE(a, NS_NewSVGAFrame),
    SIMPLE_SVG_CREATE(altGlyph, NS_NewSVGTSpanFrame),
    SIMPLE_SVG_CREATE(text, NS_NewSVGTextFrame),
    SIMPLE_SVG_CREATE(tspan, NS_NewSVGTSpanFrame),
    SIMPLE_SVG_CREATE(linearGradient, NS_NewSVGLinearGradientFrame),
    SIMPLE_SVG_CREATE(radialGradient, NS_NewSVGRadialGradientFrame),
    SIMPLE_SVG_CREATE(stop, NS_NewSVGStopFrame),
    SIMPLE_SVG_CREATE(use, NS_NewSVGUseFrame),
    SIMPLE_SVG_CREATE(marker, NS_NewSVGMarkerFrame),
    SIMPLE_SVG_CREATE(image, NS_NewSVGImageFrame),
    SIMPLE_SVG_CREATE(clipPath, NS_NewSVGClipPathFrame),
    SIMPLE_SVG_CREATE(textPath, NS_NewSVGTextPathFrame),
    SIMPLE_SVG_CREATE(filter, NS_NewSVGFilterFrame),
    SIMPLE_SVG_CREATE(pattern, NS_NewSVGPatternFrame),
    SIMPLE_SVG_CREATE(mask, NS_NewSVGMaskFrame),
    SIMPLE_SVG_CREATE(feDistantLight, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(fePointLight, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feSpotLight, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feBlend, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feColorMatrix, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feFuncR, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feFuncG, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feFuncB, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feFuncA, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feComposite, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feConvolveMatrix, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feDisplacementMap, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feFlood, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feGaussianBlur, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feImage, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feMergeNode, NS_NewSVGLeafFrame),
    SIMPLE_SVG_CREATE(feMorphology, NS_NewSVGLeafFrame), 
    SIMPLE_SVG_CREATE(feOffset, NS_NewSVGLeafFrame), 
    SIMPLE_SVG_CREATE(feTile, NS_NewSVGLeafFrame), 
    SIMPLE_SVG_CREATE(feTurbulence, NS_NewSVGLeafFrame) 
  };

  const FrameConstructionData* data =
    FindDataByTag(aTag, aContent, aStyleContext, sSVGData,
                  NS_ARRAY_LENGTH(sSVGData));

  if (!data) {
    data = &sGenericContainerData;
  }

  return data;
}

nsresult
nsCSSFrameConstructor::ConstructSVGForeignObjectFrame(nsFrameConstructorState& aState,
                                                      FrameConstructionItem&   aItem,
                                                      nsIFrame* aParentFrame,
                                                      const nsStyleDisplay* aStyleDisplay,
                                                      nsFrameItems& aFrameItems,
                                                      nsIFrame** aNewFrame)
{
  nsIContent* const content = aItem.mContent;
  nsStyleContext* const styleContext = aItem.mStyleContext;

  nsIFrame* newFrame = NS_NewSVGForeignObjectFrame(mPresShell, styleContext);
  if (NS_UNLIKELY(!newFrame)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // We don't allow this frame to be out of flow
  InitAndRestoreFrame(aState, content, aParentFrame, nsnull, newFrame);
  nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);

  nsresult rv = aState.AddChild(newFrame, aFrameItems, content, styleContext,
                                aParentFrame, PR_FALSE, PR_FALSE);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsRefPtr<nsStyleContext> innerPseudoStyle;
  innerPseudoStyle = mPresShell->StyleSet()->
    ResolveAnonymousBoxStyle(nsCSSAnonBoxes::mozSVGForeignContent, styleContext);

  nsIFrame* blockFrame = NS_NewBlockFrame(mPresShell, innerPseudoStyle,
                                          NS_BLOCK_FLOAT_MGR |
                                          NS_BLOCK_MARGIN_ROOT);
  if (NS_UNLIKELY(!blockFrame)) {
    newFrame->Destroy();
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsFrameItems childItems;
  // Claim to be relatively positioned so that we end up being the
  // absolute containing block.
  rv = ConstructBlock(aState, innerPseudoStyle->GetStyleDisplay(), content,
                      newFrame, newFrame, innerPseudoStyle,
                      &blockFrame, childItems, PR_TRUE,
                      aItem.mPendingBinding);

  // Give the blockFrame a view so that GetOffsetTo works for descendants
  // of blockFrame with views...
  nsHTMLContainerFrame::CreateViewForFrame(blockFrame, PR_TRUE);

  newFrame->SetInitialChildList(nsnull, childItems);

  *aNewFrame = newFrame;

  return rv;
}

#endif // MOZ_SVG

void
nsCSSFrameConstructor::AddPageBreakItem(nsIContent* aContent,
                                        nsStyleContext* aMainStyleContext,
                                        FrameConstructionItemList& aItems)
{
  nsRefPtr<nsStyleContext> pseudoStyle;
  // Use the same parent style context that |aMainStyleContext| has, since
  // that's easier to re-resolve and it doesn't matter in practice.
  // (Getting different parents can result in framechange hints, e.g.,
  // for user-modify.)
  pseudoStyle =
    mPresShell->StyleSet()->
      ResolveAnonymousBoxStyle(nsCSSAnonBoxes::pageBreak,
                               aMainStyleContext->GetParent());

  NS_ASSERTION(pseudoStyle->GetStyleDisplay()->mDisplay ==
                 NS_STYLE_DISPLAY_BLOCK, "Unexpected display");

  static const FrameConstructionData sPageBreakData =
    FCDATA_DECL(FCDATA_SKIP_FRAMESET, NS_NewPageBreakFrame);

  // Lie about the tag and namespace so we don't trigger anything
  // interesting during frame construction.
  aItems.AppendItem(&sPageBreakData, aContent, nsCSSAnonBoxes::pageBreak,
                    kNameSpaceID_None, nsnull, pseudoStyle.forget(), PR_TRUE);
}

nsresult
nsCSSFrameConstructor::ConstructFrame(nsFrameConstructorState& aState,
                                      nsIContent*              aContent,
                                      nsIFrame*                aParentFrame,
                                      nsFrameItems&            aFrameItems)

{
  NS_PRECONDITION(nsnull != aParentFrame, "no parent frame");
  FrameConstructionItemList items;
  AddFrameConstructionItems(aState, aContent, PR_TRUE, aParentFrame, items);

  for (FCItemIterator iter(items); !iter.IsDone(); iter.Next()) {
    NS_ASSERTION(iter.item().DesiredParentType() == GetParentType(aParentFrame),
                 "This is not going to work");
    nsresult rv =
      ConstructFramesFromItem(aState, iter, aParentFrame, aFrameItems);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

void
nsCSSFrameConstructor::AddFrameConstructionItems(nsFrameConstructorState& aState,
                                                 nsIContent* aContent,
                                                 PRBool aSuppressWhiteSpaceOptimizations,
                                                 nsIFrame* aParentFrame,
                                                 FrameConstructionItemList& aItems)
{
  aContent->UnsetFlags(NODE_DESCENDANTS_NEED_FRAMES | NODE_NEEDS_FRAME);
  if (aContent->IsElement()) {
    // We can't just remove our pending restyle flags, since we may
    // have restyle-later-siblings set on us.  But we _can_ remove the
    // "is possible restyle root" flags, and need to.  Otherwise we can
    // end up with stale such flags (e.g. if we used to have a
    // display:none parent when our last restyle was posted and
    // processed and now no longer do).
    aContent->UnsetFlags(ELEMENT_ALL_RESTYLE_FLAGS &
                         ~ELEMENT_PENDING_RESTYLE_FLAGS);
  }

  // don't create a whitespace frame if aParent doesn't want it
  if (!NeedFrameFor(aState, aParentFrame, aContent)) {
    return;
  }

  // never create frames for comments or PIs
  if (aContent->IsNodeOfType(nsINode::eCOMMENT) ||
      aContent->IsNodeOfType(nsINode::ePROCESSING_INSTRUCTION))
    return;

  nsRefPtr<nsStyleContext> styleContext;
  styleContext = ResolveStyleContext(aParentFrame, aContent);

  AddFrameConstructionItemsInternal(aState, aContent, aParentFrame,
                                    aContent->Tag(), aContent->GetNameSpaceID(),
                                    aSuppressWhiteSpaceOptimizations,
                                    styleContext,
                                    ITEM_ALLOW_XBL_BASE | ITEM_ALLOW_PAGE_BREAK,
                                    aItems);
}

/**
 * Set aContent as undisplayed content with style context aStyleContext.  This
 * method enforces the invariant that all style contexts in the undisplayed
 * content map must be non-pseudo contexts and also handles unbinding
 * undisplayed generated content as needed.
 */
static void
SetAsUndisplayedContent(nsFrameManager* aFrameManager, nsIContent* aContent,
                        nsStyleContext* aStyleContext,
                        PRBool aIsGeneratedContent)
{
  if (aStyleContext->GetPseudo()) {
    if (aIsGeneratedContent) {
      aContent->UnbindFromTree();
    }
    return;
  }

  NS_ASSERTION(!aIsGeneratedContent, "Should have had pseudo type");
  aFrameManager->SetUndisplayedContent(aContent, aStyleContext);
}

void
nsCSSFrameConstructor::AddFrameConstructionItemsInternal(nsFrameConstructorState& aState,
                                                         nsIContent* aContent,
                                                         nsIFrame* aParentFrame,
                                                         nsIAtom* aTag,
                                                         PRInt32 aNameSpaceID,
                                                         PRBool aSuppressWhiteSpaceOptimizations,
                                                         nsStyleContext* aStyleContext,
                                                         PRUint32 aFlags,
                                                         FrameConstructionItemList& aItems)
{
  // The following code allows the user to specify the base tag
  // of an element using XBL.  XUL and HTML objects (like boxes, menus, etc.)
  // can then be extended arbitrarily.
  const nsStyleDisplay* display = aStyleContext->GetStyleDisplay();
  nsRefPtr<nsStyleContext> styleContext(aStyleContext);
  PendingBinding* pendingBinding = nsnull;
  if ((aFlags & ITEM_ALLOW_XBL_BASE) && display->mBinding)
  {
    // Ensure that our XBL bindings are installed.

    nsIXBLService * xblService = GetXBLService();
    if (!xblService)
      return;

    PRBool resolveStyle;

    nsAutoPtr<PendingBinding> newPendingBinding(new PendingBinding());
    if (!newPendingBinding) {
      return;
    }
    nsresult rv = xblService->LoadBindings(aContent, display->mBinding->mURI,
                                           display->mBinding->mOriginPrincipal,
                                           PR_FALSE,
                                           getter_AddRefs(newPendingBinding->mBinding),
                                           &resolveStyle);
    if (NS_FAILED(rv))
      return;

    if (newPendingBinding->mBinding) {
      pendingBinding = newPendingBinding;
      // aState takes over owning newPendingBinding
      aState.AddPendingBinding(newPendingBinding.forget());
    }

    if (resolveStyle) {
      styleContext = ResolveStyleContext(styleContext->GetParent(), aContent);
      display = styleContext->GetStyleDisplay();
      aStyleContext = styleContext;
    }

    aTag = mDocument->BindingManager()->ResolveTag(aContent, &aNameSpaceID);
  }

  PRBool isGeneratedContent = ((aFlags & ITEM_IS_GENERATED_CONTENT) != 0);

  // Pre-check for display "none" - if we find that, don't create
  // any frame at all
  if (NS_STYLE_DISPLAY_NONE == display->mDisplay) {
    SetAsUndisplayedContent(aState.mFrameManager, aContent, styleContext,
                            isGeneratedContent);
    return;
  }

  PRBool isText = aContent->IsNodeOfType(nsINode::eTEXT);
  PRBool isPopup = PR_FALSE;
  // Try to find frame construction data for this content
  const FrameConstructionData* data;
  if (isText) {
    data = FindTextData(aParentFrame);
#ifdef MOZ_SVG
    if (!data) {
      // Nothing to do here; suppressed text inside SVG
      return;
    }
#endif /* MOZ_SVG */
  } else {
#ifdef MOZ_SVG
    // Don't create frames for non-SVG element children of SVG elements.
    if (aNameSpaceID != kNameSpaceID_SVG &&
        aParentFrame &&
        aParentFrame->IsFrameOfType(nsIFrame::eSVG) &&
        !aParentFrame->IsFrameOfType(nsIFrame::eSVGForeignObject)
        ) {
      SetAsUndisplayedContent(aState.mFrameManager, aContent, styleContext,
                              isGeneratedContent);
      return;
    }
#endif /* MOZ_SVG */

    data = FindHTMLData(aContent, aTag, aNameSpaceID, aParentFrame,
                        styleContext);
    if (!data) {
      data = FindXULTagData(aContent, aTag, aNameSpaceID, styleContext);
    }
#ifdef MOZ_MATHML
    if (!data) {
      data = FindMathMLData(aContent, aTag, aNameSpaceID, styleContext);
    }
#endif
#ifdef MOZ_SVG
    if (!data) {
      data = FindSVGData(aContent, aTag, aNameSpaceID, aParentFrame,
                         styleContext);
    }
#endif /* MOZ_SVG */

    // Now check for XUL display types
    if (!data) {
      data = FindXULDisplayData(display, aContent, styleContext);
    }

    // And general display types
    if (!data) {
      data = FindDisplayData(display, aContent, styleContext);
    }

    NS_ASSERTION(data, "Should have frame construction data now");

    if (data->mBits & FCDATA_SUPPRESS_FRAME) {
      SetAsUndisplayedContent(aState.mFrameManager, aContent, styleContext,
                              isGeneratedContent);
      return;
    }

#ifdef MOZ_XUL
    if ((data->mBits & FCDATA_IS_POPUP) &&
        (!aParentFrame || // Parent is inline
         aParentFrame->GetType() != nsGkAtoms::menuFrame)) {
      if (!aState.mPopupItems.containingBlock &&
          !aState.mHavePendingPopupgroup) {
        SetAsUndisplayedContent(aState.mFrameManager, aContent, styleContext,
                                isGeneratedContent);
        return;
      }

      isPopup = PR_TRUE;
    }
#endif /* MOZ_XUL */
  }

  PRUint32 bits = data->mBits;

  // Inside colgroups, suppress everything except columns.
  if (aParentFrame &&
      aParentFrame->GetType() == nsGkAtoms::tableColGroupFrame &&
      (!(bits & FCDATA_IS_TABLE_PART) ||
       display->mDisplay != NS_STYLE_DISPLAY_TABLE_COLUMN)) {
    SetAsUndisplayedContent(aState.mFrameManager, aContent, styleContext,
                            isGeneratedContent);
    return;
  }

  PRBool canHavePageBreak =
    (aFlags & ITEM_ALLOW_PAGE_BREAK) &&
    aState.mPresContext->IsPaginated() &&
    !display->IsAbsolutelyPositioned() &&
    !(bits & FCDATA_IS_TABLE_PART);

  if (canHavePageBreak && display->mBreakBefore) {
    AddPageBreakItem(aContent, aStyleContext, aItems);
  }

  FrameConstructionItem* item =
    aItems.AppendItem(data, aContent, aTag, aNameSpaceID,
                      pendingBinding, styleContext.forget(),
                      aSuppressWhiteSpaceOptimizations);
  if (!item) {
    if (isGeneratedContent) {
      aContent->UnbindFromTree();
    }
    return;
  }

  item->mIsText = isText;
  item->mIsGeneratedContent = isGeneratedContent;
  if (isGeneratedContent) {
    NS_ADDREF(item->mContent);
  }
  item->mIsRootPopupgroup =
    aNameSpaceID == kNameSpaceID_XUL && aTag == nsGkAtoms::popupgroup &&
    aContent->IsRootOfNativeAnonymousSubtree();
  if (item->mIsRootPopupgroup) {
    aState.mHavePendingPopupgroup = PR_TRUE;
  }
  item->mIsPopup = isPopup;

  if (canHavePageBreak && display->mBreakAfter) {
    AddPageBreakItem(aContent, aStyleContext, aItems);
  }

  if (bits & FCDATA_IS_INLINE) {
    // To correctly set item->mIsAllInline we need to build up our child items
    // right now.
    BuildInlineChildItems(aState, *item);
    item->mHasInlineEnds = PR_TRUE;
    item->mIsBlock = PR_FALSE;
  } else {
    // Compute a boolean isInline which is guaranteed to be false for blocks
    // (but may also be false for some inlines).
    PRBool isInline =
      // Table-internal things are inline-outside if and only if they're kids of
      // inlines, since they'll trigger construction of inline-table
      // pseudos.
      ((bits & FCDATA_IS_TABLE_PART) &&
       (!aParentFrame || // No aParentFrame means inline
        aParentFrame->GetStyleDisplay()->mDisplay == NS_STYLE_DISPLAY_INLINE)) ||
      // Things that are inline-outside but aren't inline frames are inline
      display->IsInlineOutside() ||
      // Popups that are certainly out of flow.
      isPopup;

    // Set mIsAllInline conservatively.  It just might be that even an inline
    // that has mIsAllInline false doesn't need an {ib} split.  So this is just
    // an optimization to keep from doing too much work in cases when we can
    // show that mIsAllInline is true..
    item->mIsAllInline = item->mHasInlineEnds = isInline ||
      // Figure out whether we're guaranteed this item will be out of flow.
      // This is not a precise test, since one of our ancestor inlines might add
      // an absolute containing block (if it's relatively positioned) when there
      // wasn't such a containing block before.  But it's conservative in the
      // sense that anything that will really end up as an in-flow non-inline
      // will test false here.  In other words, if this test is true we're
      // guaranteed to be inline; if it's false we don't know what we'll end up
      // as.
      //
      // If we make this test precise, we can remove some of the code dealing
      // with the imprecision in ConstructInline and adjust the comments on
      // mIsAllInline and mIsBlock in the header.  And probably remove mIsBlock
      // altogether, since then it will always be equal to !mHasInlineEnds.
      (!(bits & FCDATA_DISALLOW_OUT_OF_FLOW) &&
       aState.GetGeometricParent(display, nsnull));

    // Set mIsBlock conservatively.  It's OK to set it false for some real
    // blocks, but not OK to set it true for things that aren't blocks.  Since
    // isOutOfFlow might be false even in cases when the frame will end up
    // out-of-flow, we can't use it here.  But we _can_ say that the frame will
    // for sure end up in-flow if it's not floated or absolutely positioned.
    item->mIsBlock =
      !isInline && !display->IsAbsolutelyPositioned() && !display->IsFloating();
  }

  if (item->mIsAllInline) {
    aItems.InlineItemAdded();
  } else if (item->mIsBlock) {
    aItems.BlockItemAdded();
  }

  // Our item should be treated as a line participant if we have the relevant
  // bit and are going to be in-flow.  Note that this really only matters if
  // our ancestor is a box or some such, so the fact that we might have an
  // inline ancestor that might become a containing block is not relevant here.
  if ((bits & FCDATA_IS_LINE_PARTICIPANT) &&
      ((bits & FCDATA_DISALLOW_OUT_OF_FLOW) ||
       !aState.GetGeometricParent(display, nsnull))) {
    item->mIsLineParticipant = PR_TRUE;
    aItems.LineParticipantItemAdded();
  }
}

static void
DestroyContent(void* aPropertyValue)
{
  nsIContent* content = static_cast<nsIContent*>(aPropertyValue);
  content->UnbindFromTree();
  NS_RELEASE(content);
}

NS_DECLARE_FRAME_PROPERTY(BeforeProperty, DestroyContent)
NS_DECLARE_FRAME_PROPERTY(AfterProperty, DestroyContent)

static const FramePropertyDescriptor*
GenConPseudoToProperty(nsIAtom* aPseudo)
{
  NS_ASSERTION(aPseudo == nsCSSPseudoElements::before ||
               aPseudo == nsCSSPseudoElements::after,
               "Bad gen-con pseudo");
  return aPseudo == nsCSSPseudoElements::before ? BeforeProperty()
      : AfterProperty();
}

/**
 * Return true if the frame construction item pointed to by aIter will
 * create a frame adjacent to a line boundary in the frame tree, and that
 * line boundary is induced by a content node adjacent to the frame's
 * content node in the content tree. The latter condition is necessary so
 * that ContentAppended/ContentInserted/ContentRemoved can easily find any
 * text nodes that were suppressed here.
 */
PRBool
nsCSSFrameConstructor::AtLineBoundary(FCItemIterator& aIter)
{
  if (aIter.item().mSuppressWhiteSpaceOptimizations) {
    return PR_FALSE;
  }

  if (aIter.AtStart()) {
    if (aIter.List()->HasLineBoundaryAtStart() &&
        !aIter.item().mContent->GetPreviousSibling())
      return PR_TRUE;
  } else {
    FCItemIterator prev = aIter;
    prev.Prev();
    if (prev.item().IsLineBoundary() &&
        !prev.item().mSuppressWhiteSpaceOptimizations &&
        aIter.item().mContent->GetPreviousSibling() == prev.item().mContent)
      return PR_TRUE;
  }

  FCItemIterator next = aIter;
  next.Next();
  if (next.IsDone()) {
    if (aIter.List()->HasLineBoundaryAtEnd() &&
        !aIter.item().mContent->GetNextSibling())
      return PR_TRUE;
  } else {
    if (next.item().IsLineBoundary() &&
        !next.item().mSuppressWhiteSpaceOptimizations &&
        aIter.item().mContent->GetNextSibling() == next.item().mContent)
      return PR_TRUE;
  }

  return PR_FALSE;
}

nsresult
nsCSSFrameConstructor::ConstructFramesFromItem(nsFrameConstructorState& aState,
                                               FCItemIterator& aIter,
                                               nsIFrame* aParentFrame,
                                               nsFrameItems& aFrameItems)
{
  nsIFrame* adjParentFrame = aParentFrame;
  FrameConstructionItem& item = aIter.item();
  nsStyleContext* styleContext = item.mStyleContext;
  AdjustParentFrame(adjParentFrame, item.mFCData, styleContext);

  if (item.mIsText) {
    // If this is collapsible whitespace next to a line boundary,
    // don't create a frame. item.IsWhitespace() also sets the
    // NS_CREATE_FRAME_IF_NON_WHITESPACE flag in the text node. (If we
    // end up creating a frame, nsTextFrame::Init will clear the flag.)
    // We don't do this for generated content, because some generated
    // text content is empty text nodes that are about to be initialized.
    // (We check mAdditionalStateBits because only the generated content
    // container's frame construction item is marked with
    // mIsGeneratedContent, and we might not have an aParentFrame.)
    // We don't do it for content that may have XBL anonymous siblings,
    // because they make it difficult to correctly create the frame
    // due to dynamic changes.
    // We don't do it for text that's not a line participant (i.e. SVG text).
    if (AtLineBoundary(aIter) &&
        !styleContext->GetStyleText()->NewlineIsSignificant() &&
        aIter.List()->ParentHasNoXBLChildren() &&
        !(aState.mAdditionalStateBits & NS_FRAME_GENERATED_CONTENT) &&
        (item.mFCData->mBits & FCDATA_IS_LINE_PARTICIPANT) &&
        item.IsWhitespace(aState))
      return NS_OK;

    return ConstructTextFrame(item.mFCData, aState, item.mContent,
                              adjParentFrame, styleContext,
                              aFrameItems);
  }

  // Start background loads during frame construction. This is just
  // a hint; the paint code will do the right thing in any case.
  {
    styleContext->GetStyleBackground();
  }

  nsFrameState savedStateBits = aState.mAdditionalStateBits;
  if (item.mIsGeneratedContent) {
    // Ensure that frames created here are all tagged with
    // NS_FRAME_GENERATED_CONTENT.
    aState.mAdditionalStateBits |= NS_FRAME_GENERATED_CONTENT;

    // Note that we're not necessarily setting this property on the primary
    // frame for the content for which this is generated content.  We might be
    // setting it on a table pseudo-frame inserted under that instead.  That's
    // OK, though; we just need to do the property set so that the content will
    // get cleaned up when the frame is destroyed.
    aParentFrame->Properties().Set(GenConPseudoToProperty(styleContext->GetPseudo()),
                                   item.mContent);

    // Now that we've passed ownership of item.mContent to the frame, unset
    // our generated content flag so we don't release or unbind it ourselves.
    item.mIsGeneratedContent = PR_FALSE;
  }

  // XXXbz maybe just inline ConstructFrameFromItemInternal here or something?
  nsresult rv = ConstructFrameFromItemInternal(item, aState, adjParentFrame,
                                               aFrameItems);

  aState.mAdditionalStateBits = savedStateBits;

  return rv;
}


inline PRBool
IsRootBoxFrame(nsIFrame *aFrame)
{
  return (aFrame->GetType() == nsGkAtoms::rootFrame);
}

nsresult
nsCSSFrameConstructor::ReconstructDocElementHierarchy()
{
  return RecreateFramesForContent(mPresShell->GetDocument()->GetRootElement(),
				  PR_FALSE);
}

nsIFrame*
nsCSSFrameConstructor::GetFrameFor(nsIContent* aContent)
{
  // Get the primary frame associated with the content
  nsIFrame* frame = aContent->GetPrimaryFrame();

  if (!frame)
    return nsnull;

  // If the content of the frame is not the desired content then this is not
  // really a frame for the desired content.
  // XXX This check is needed due to bug 135040. Remove it once that's fixed.
  if (frame->GetContent() != aContent) {
    return nsnull;
  }

  nsIFrame* insertionFrame = frame->GetContentInsertionFrame();

  NS_ASSERTION(insertionFrame == frame || !frame->IsLeaf(),
    "The insertion frame is the primary frame or the primary frame isn't a leaf");

  return insertionFrame;
}

nsIFrame*
nsCSSFrameConstructor::GetAbsoluteContainingBlock(nsIFrame* aFrame)
{
  NS_PRECONDITION(nsnull != mRootElementFrame, "no root element frame");
  
  // Starting with aFrame, look for a frame that is absolutely positioned or
  // relatively positioned
  nsIFrame* containingBlock = nsnull;
  for (nsIFrame* frame = aFrame; frame && !containingBlock;
       frame = frame->GetParent()) {
    if (frame->IsFrameOfType(nsIFrame::eMathML)) {
      // If it's mathml, bail out -- no absolute positioning out from inside
      // mathml frames.  Note that we don't make this part of the loop
      // condition because of the stuff at the end of this method...
      return nsnull;
    }
    
    // Is it positioned?
    // If it's table-related then ignore it, because for the time
    // being table-related frames are not containers for absolutely
    // positioned child frames.
    const nsStyleDisplay* disp = frame->GetStyleDisplay();

    if (disp->IsPositioned() && !IsTableRelated(frame->GetType())) {
      // Find the outermost wrapped block under this frame
      for (nsIFrame* wrappedFrame = aFrame; wrappedFrame != frame->GetParent();
           wrappedFrame = wrappedFrame->GetParent()) {
        nsIAtom* frameType = wrappedFrame->GetType();
        if (nsGkAtoms::blockFrame == frameType ||
#ifdef MOZ_XUL
            nsGkAtoms::XULLabelFrame == frameType ||
#endif
            nsGkAtoms::positionedInlineFrame == frameType) {
          containingBlock = wrappedFrame;
        } else if (nsGkAtoms::fieldSetFrame == frameType) {
          // If the positioned frame is a fieldset, use the area frame inside it.
          // We don't use GetContentInsertionFrame for fieldsets yet.
          containingBlock = GetFieldSetBlockFrame(wrappedFrame);
        }
      }

#ifdef DEBUG
      if (!containingBlock)
        NS_WARNING("Positioned frame that does not handle positioned kids; looking further up the parent chain");
#endif
    }
  }

  // If we found an absolutely positioned containing block, then use the
  // first-continuation.
  if (containingBlock)
    return AdjustAbsoluteContainingBlock(containingBlock);

  // If we didn't find it, then use the document element containing block
  return mHasRootAbsPosContainingBlock ? mDocElementContainingBlock : nsnull;
}

nsIFrame*
nsCSSFrameConstructor::GetFloatContainingBlock(nsIFrame* aFrame)
{
  // Starting with aFrame, look for a frame that is a float containing block.
  // IF we hit a mathml frame, bail out; we don't allow floating out of mathml
  // frames, because they don't seem to be able to deal.
  // The logic here needs to match the logic in ProcessChildren()
  for (nsIFrame* containingBlock = aFrame;
       containingBlock && !containingBlock->IsFrameOfType(nsIFrame::eMathML) &&
       !containingBlock->IsBoxFrame();
       containingBlock = containingBlock->GetParent()) {
    if (containingBlock->IsFloatContainingBlock()) {
      return containingBlock;
    }
  }

  // If we didn't find a containing block, then there just isn't
  // one.... return null
  return nsnull;
}

/**
 * This function will check whether aContainer has :after generated content.
 * If so, appending to it should actually insert.  The return value is the
 * parent to use for newly-appended content.  *aAfterFrame points to the :after
 * frame before which appended content should go, if there is one.
 */
static nsIFrame*
AdjustAppendParentForAfterContent(nsPresContext* aPresContext,
                                  nsIContent* aContainer,
                                  nsIFrame* aParentFrame,
                                  nsIFrame** aAfterFrame)
{
  // See if the parent has an :after pseudo-element.  Check for the presence
  // of style first, since nsLayoutUtils::GetAfterFrame is sorta expensive.
  nsStyleContext* parentStyle = aParentFrame->GetStyleContext();
  if (nsLayoutUtils::HasPseudoStyle(aContainer, parentStyle,
                                    nsCSSPseudoElements::ePseudo_after,
                                    aPresContext)) {
    nsIFrame* afterFrame = nsLayoutUtils::GetAfterFrame(aParentFrame);
    if (afterFrame) {
      *aAfterFrame = afterFrame;
      return afterFrame->GetParent();
    }
  }

  *aAfterFrame = nsnull;

  if (IsFrameSpecial(aParentFrame)) {
    // We might be in a situation where the last part of the {ib} split was
    // empty.  Since we have no ::after pseudo-element, we do in fact want to be
    // appending to that last part, so advance to it if needed.  Note that here
    // aParentFrame is the result of a GetLastSpecialSibling call, so must be
    // either the last or next to last special sibling.
    nsIFrame* trailingInline = GetSpecialSibling(aParentFrame);
    if (trailingInline) {
      aParentFrame = trailingInline;
    }

    // Always make sure to look at the last continuation of the frame
    // for the {ib} case, even if that continuation is empty.  We
    // don't do this for the non-special-frame case, since in the
    // other cases appending to the last nonempty continuation is fine
    // and in fact not doing that can confuse code that doesn't know
    // to pull kids from continuations other than its next one.
    aParentFrame = aParentFrame->GetLastContinuation();
  }

  return aParentFrame;
}

/**
 * This function will get the previous sibling to use for an append operation.
 * it takes a parent frame (must not be null) and its :after frame (may be
 * null).
 */
static nsIFrame*
FindAppendPrevSibling(nsIFrame* aParentFrame, nsIFrame* aAfterFrame)
{
  if (aAfterFrame) {
    NS_ASSERTION(aAfterFrame->GetParent() == aParentFrame, "Wrong parent");
    return aAfterFrame->GetPrevSibling();
  }

  return aParentFrame->GetLastChild(nsnull);
}

/**
 * This function will get the next sibling for a frame insert operation given
 * the parent and previous sibling.  aPrevSibling may be null.
 */
static nsIFrame*
GetInsertNextSibling(nsIFrame* aParentFrame, nsIFrame* aPrevSibling)
{
  if (aPrevSibling) {
    return aPrevSibling->GetNextSibling();
  }

  return aParentFrame->GetFirstChild(nsnull);
}

/**
 * This function is called by ContentAppended() and ContentInserted() when
 * appending flowed frames to a parent's principal child list. It handles the
 * case where the parent is the trailing inline of an {ib} split.
 */
nsresult
nsCSSFrameConstructor::AppendFrames(nsFrameConstructorState&       aState,
                                    nsIFrame*                      aParentFrame,
                                    nsFrameItems&                  aFrameList,
                                    nsIFrame*                      aPrevSibling,
                                    PRBool                         aIsRecursiveCall)
{
  NS_PRECONDITION(!IsFrameSpecial(aParentFrame) ||
                  !GetSpecialSibling(aParentFrame) ||
                  !GetSpecialSibling(aParentFrame)->GetFirstChild(nsnull),
                  "aParentFrame has a special sibling with kids?");
  NS_PRECONDITION(!aPrevSibling || aPrevSibling->GetParent() == aParentFrame,
                  "Parent and prevsibling don't match");

  nsIFrame* nextSibling = ::GetInsertNextSibling(aParentFrame, aPrevSibling);

  NS_ASSERTION(nextSibling ||
               !aParentFrame->GetNextContinuation() ||
               !aParentFrame->GetNextContinuation()->GetFirstChild(nsnull) ||
               aIsRecursiveCall,
               "aParentFrame has later continuations with kids?");
  NS_ASSERTION(nextSibling ||
               !IsFrameSpecial(aParentFrame) ||
               (IsInlineFrame(aParentFrame) &&
                !GetSpecialSibling(aParentFrame) &&
                !aParentFrame->GetNextContinuation()) ||
               aIsRecursiveCall,
               "aParentFrame is not last?");

  // If we're inserting a list of frames at the end of the trailing inline
  // of an {ib} split, we may need to create additional {ib} siblings to parent
  // them.
  if (!nextSibling && IsFrameSpecial(aParentFrame)) {
    // When we get here, our frame list might start with a block.  If it does
    // so, and aParentFrame is an inline, and it and all its previous
    // continuations have no siblings, then put the initial blocks from the
    // frame list into the previous block of the {ib} split.  Note that we
    // didn't want to stop at the block part of the split when figuring out
    // initial parent, because that could screw up float parenting; it's easier
    // to do this little fixup here instead.
    if (aFrameList.NotEmpty() && !IsInlineOutside(aFrameList.FirstChild())) {
      // See whether our trailing inline is empty
      nsIFrame* firstContinuation = aParentFrame->GetFirstContinuation();
      if (firstContinuation->GetChildList(nsnull).IsEmpty()) {
        // Our trailing inline is empty.  Collect our starting blocks from
        // aFrameList, get the right parent frame for them, and put them in.
        nsFrameList::FrameLinkEnumerator firstNonBlockEnumerator =
          FindFirstNonBlock(aFrameList);
        nsFrameList blockKids = aFrameList.ExtractHead(firstNonBlockEnumerator);
        NS_ASSERTION(blockKids.NotEmpty(), "No blocks?");

        nsIFrame* prevBlock =
          GetSpecialPrevSibling(firstContinuation)->GetLastContinuation();
        NS_ASSERTION(prevBlock, "Should have previous block here");

        MoveChildrenTo(aState.mPresContext, aParentFrame, prevBlock, blockKids);
      }
    }

    // We want to put some of the frames into this inline frame.
    nsFrameList::FrameLinkEnumerator firstBlockEnumerator(aFrameList);
    FindFirstBlock(firstBlockEnumerator);

    nsFrameList inlineKids = aFrameList.ExtractHead(firstBlockEnumerator);
    if (!inlineKids.IsEmpty()) {
      aState.mFrameManager->AppendFrames(aParentFrame, nsnull, inlineKids);
    }

    if (!aFrameList.IsEmpty()) {
      const nsStyleDisplay* parentDisplay = aParentFrame->GetStyleDisplay();
      PRBool positioned =
        parentDisplay->mPosition == NS_STYLE_POSITION_RELATIVE ||
        parentDisplay->HasTransform();
      nsFrameItems ibSiblings;
      CreateIBSiblings(aState, aParentFrame, positioned, aFrameList,
                       ibSiblings);

      // Make sure to trigger reflow of the inline that used to be our
      // last one and now isn't anymore, since its GetSkipSides() has
      // changed.
      mPresShell->FrameNeedsReflow(aParentFrame,
                                   nsIPresShell::eTreeChange,
                                   NS_FRAME_HAS_DIRTY_CHILDREN);

      // Recurse so we create new ib siblings as needed for aParentFrame's parent
      return AppendFrames(aState, aParentFrame->GetParent(), ibSiblings,
                          aParentFrame, PR_TRUE);
    }

    return NS_OK;
  }
  
  // Insert the frames after our aPrevSibling
  return aState.mFrameManager->InsertFrames(aParentFrame, nsnull, aPrevSibling,
                                            aFrameList);
}

#define UNSET_DISPLAY 255

// This gets called to see if the frames corresponding to aSibling and aContent
// should be siblings in the frame tree. Although (1) rows and cols, (2) row
// groups and col groups, (3) row groups and captions, (4) legends and content
// inside fieldsets, (5) popups and other kids of the menu are siblings from a
// content perspective, they are not considered siblings in the frame tree.
PRBool
nsCSSFrameConstructor::IsValidSibling(nsIFrame*              aSibling,
                                      nsIContent*            aContent,
                                      PRUint8&               aDisplay)
{
  nsIFrame* parentFrame = aSibling->GetParent();
  nsIAtom* parentType = nsnull;
  nsIAtom* grandparentType = nsnull;
  if (parentFrame) {
    parentType = parentFrame->GetType();
    nsIFrame* grandparentFrame = parentFrame->GetParent();
    if (grandparentFrame) {
      grandparentType = grandparentFrame->GetType();
    }
  }
    
  PRUint8 siblingDisplay = aSibling->GetStyleDisplay()->mDisplay;
  if ((NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP == siblingDisplay) ||
      (NS_STYLE_DISPLAY_TABLE_COLUMN       == siblingDisplay) ||
      (NS_STYLE_DISPLAY_TABLE_CAPTION      == siblingDisplay) ||
      (NS_STYLE_DISPLAY_TABLE_HEADER_GROUP == siblingDisplay) ||
      (NS_STYLE_DISPLAY_TABLE_ROW_GROUP    == siblingDisplay) ||
      (NS_STYLE_DISPLAY_TABLE_FOOTER_GROUP == siblingDisplay) ||
      nsGkAtoms::menuFrame == parentType) {
    // if we haven't already, construct a style context to find the display type of aContent
    if (UNSET_DISPLAY == aDisplay) {
      nsRefPtr<nsStyleContext> styleContext;
      nsIFrame* styleParent;
      PRBool providerIsChild;
      if (NS_FAILED(aSibling->
                      GetParentStyleContextFrame(aSibling->PresContext(),
                                                 &styleParent,
                                                 &providerIsChild)) ||
          !styleParent) {
        NS_NOTREACHED("Shouldn't happen");
        return PR_FALSE;
      }
      styleContext = ResolveStyleContext(styleParent, aContent);
      if (!styleContext) return PR_FALSE;
      const nsStyleDisplay* display = styleContext->GetStyleDisplay();
      aDisplay = display->mDisplay;
    }
    if (nsGkAtoms::menuFrame == parentType) {
      return
        (NS_STYLE_DISPLAY_POPUP == aDisplay) ==
        (NS_STYLE_DISPLAY_POPUP == siblingDisplay);
    }
    // To have decent performance we want to return false in cases in which
    // reordering the two siblings has no effect on display.  To ensure
    // correctness, we MUST return false in cases where the two siblings have
    // the same desired parent type and live on different display lists.
    // Specificaly, columns and column groups should only consider columns and
    // column groups as valid siblings.  Captions should only consider other
    // captions.  All other things should consider each other as valid
    // siblings.  The restriction in the |if| above on siblingDisplay is ok,
    // because for correctness the only part that really needs to happen is to
    // not consider captions, column groups, and row/header/footer groups
    // siblings of each other.  Treating a column or colgroup as a valid
    // sibling of a non-table-related frame will just mean we end up reframing.
    if ((siblingDisplay == NS_STYLE_DISPLAY_TABLE_CAPTION) !=
        (aDisplay == NS_STYLE_DISPLAY_TABLE_CAPTION)) {
      // One's a caption and the other is not.  Not valid siblings.
      return PR_FALSE;
    }

    if ((siblingDisplay == NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP ||
         siblingDisplay == NS_STYLE_DISPLAY_TABLE_COLUMN) !=
        (aDisplay == NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP ||
         aDisplay == NS_STYLE_DISPLAY_TABLE_COLUMN)) {
      // One's a column or column group and the other is not.  Not valid
      // siblings.
      return PR_FALSE;
    }

    return PR_TRUE;
  }
  else if (nsGkAtoms::fieldSetFrame == parentType ||
           (nsGkAtoms::fieldSetFrame == grandparentType &&
            nsGkAtoms::blockFrame == parentType)) {
    // Legends can be sibling of legends but not of other content in the fieldset
    nsIAtom* sibType = aSibling->GetType();
    nsCOMPtr<nsIDOMHTMLLegendElement> legendContent(do_QueryInterface(aContent));

    if ((legendContent  && (nsGkAtoms::legendFrame != sibType)) ||
        (!legendContent && (nsGkAtoms::legendFrame == sibType)))
      return PR_FALSE;
  }

  return PR_TRUE;
}

nsIFrame*
nsCSSFrameConstructor::FindFrameForContentSibling(nsIContent* aContent,
                                                  nsIContent* aTargetContent,
                                                  PRUint8& aTargetContentDisplay,
                                                  PRBool aPrevSibling)
{
  nsIFrame* sibling = aContent->GetPrimaryFrame();
  if (!sibling || sibling->GetContent() != aContent) {
    // XXX the GetContent() != aContent check is needed due to bug 135040.
    // Remove it once that's fixed.
    return nsnull;
  }

  // If the frame is out-of-flow, GetPrimaryFrame() will have returned the
  // out-of-flow frame; we want the placeholder.
  if (sibling->GetStateBits() & NS_FRAME_OUT_OF_FLOW) {
    nsIFrame* placeholderFrame = mPresShell->FrameManager()->GetPlaceholderFrameFor(sibling);
    NS_ASSERTION(placeholderFrame, "no placeholder for out-of-flow frame");
    sibling = placeholderFrame;
  }

  // The frame we have now should never be a continuation
  NS_ASSERTION(!sibling->GetPrevContinuation(), "How did that happen?");

  if (aPrevSibling) {
    // The frame may be a special frame (a split inline frame that
    // contains a block).  Get the last part of that split.
    if (IsFrameSpecial(sibling)) {
      sibling = GetLastSpecialSibling(sibling, PR_TRUE);
    }

    // The frame may have a continuation. If so, we want the last
    // non-overflow-container continuation as our previous sibling.
    sibling = sibling->GetTailContinuation();
  }

  if (aTargetContent &&
      !IsValidSibling(sibling, aTargetContent, aTargetContentDisplay)) {
    sibling = nsnull;
  }

  return sibling;
}

nsIFrame*
nsCSSFrameConstructor::FindPreviousSibling(const ChildIterator& aFirst,
                                           ChildIterator aIter,
                                           PRUint8& aTargetContentDisplay)
{
  nsIContent* child = *aIter;

  // Note: not all content objects are associated with a frame (e.g., if it's
  // `display: none') so keep looking until we find a previous frame
  while (aIter != aFirst) {
    --aIter;
    nsIFrame* prevSibling =
      FindFrameForContentSibling(*aIter, child, aTargetContentDisplay, PR_TRUE);

    if (prevSibling) {
      // Found a previous sibling, we're done!
      return prevSibling;
    }
  }

  return nsnull;
}

nsIFrame*
nsCSSFrameConstructor::FindNextSibling(ChildIterator aIter,
                                       const ChildIterator& aLast,
                                       PRUint8& aTargetContentDisplay)
{
  if (aIter == aLast) {
    // XXXbz Can happen when XBL lies to us about insertion points.  This check
    // might be able to go away once bug 474324 is fixed.
    return nsnull;
  }

  nsIContent* child = *aIter;

  while (++aIter != aLast) {
    nsIFrame* nextSibling =
      FindFrameForContentSibling(*aIter, child, aTargetContentDisplay, PR_FALSE);

    if (nextSibling) {
      // We found a next sibling, we're done!
      return nextSibling;
    }
  }

  return nsnull;
}

// For fieldsets, returns the area frame, if the child is not a legend. 
static nsIFrame*
GetAdjustedParentFrame(nsIFrame*       aParentFrame,
                       nsIAtom*        aParentFrameType,
                       nsIContent*     aChildContent)
{
  NS_PRECONDITION(nsGkAtoms::tableOuterFrame != aParentFrameType,
                  "Shouldn't be happening!");
  
  nsIFrame* newParent = nsnull;

  if (nsGkAtoms::fieldSetFrame == aParentFrameType) {
    // If the parent is a fieldSet, use the fieldSet's area frame as the
    // parent unless the new content is a legend. 
    nsCOMPtr<nsIDOMHTMLLegendElement> legendContent(do_QueryInterface(aChildContent));
    if (!legendContent) {
      newParent = GetFieldSetBlockFrame(aParentFrame);
    }
  }
  return (newParent) ? newParent : aParentFrame;
}

nsIFrame*
nsCSSFrameConstructor::GetInsertionPrevSibling(nsIFrame*& aParentFrame,
                                               nsIContent* aContainer,
                                               nsIContent* aChild,
                                               PRBool* aIsAppend,
                                               PRBool* aIsRangeInsertSafe,
                                               nsIContent* aStartSkipChild,
                                               nsIContent* aEndSkipChild)
{
  *aIsAppend = PR_FALSE;

  // Find the frame that precedes the insertion point. Walk backwards
  // from the parent frame to get the parent content, because if an
  // XBL insertion point is involved, we'll need to use _that_ to find
  // the preceding frame.

  NS_PRECONDITION(aParentFrame, "Must have parent frame to start with");
  nsIContent* container = aParentFrame->GetContent();

  ChildIterator first, last;
  ChildIterator::Init(container, &first, &last);
  ChildIterator iter(first);
  PRBool xblCase = iter.XBLInvolved() || container != aContainer;
  if (xblCase || !aChild->IsRootOfAnonymousSubtree()) {
    // The check for IsRootOfAnonymousSubtree() is because editor is
    // severely broken and calls us directly for native anonymous
    // nodes that it creates.
    if (aStartSkipChild) {
      iter.seek(aStartSkipChild);
    } else {
      iter.seek(aChild);
    }
  }
#ifdef DEBUG
  else {
    NS_WARNING("Someone passed native anonymous content directly into frame "
               "construction.  Stop doing that!");
  }
#endif

  PRUint8 childDisplay = UNSET_DISPLAY;
  nsIFrame* prevSibling = FindPreviousSibling(first, iter, childDisplay);

  // Now, find the geometric parent so that we can handle
  // continuations properly. Use the prev sibling if we have it;
  // otherwise use the next sibling.
  if (prevSibling) {
    aParentFrame = prevSibling->GetParent()->GetContentInsertionFrame();
  }
  else {
    // If there is no previous sibling, then find the frame that follows
    if (aEndSkipChild) {
      iter.seek(aEndSkipChild);
      iter--;
    }
    nsIFrame* nextSibling = FindNextSibling(iter, last, childDisplay);

    if (nextSibling) {
      aParentFrame = nextSibling->GetParent()->GetContentInsertionFrame();
    }
    else {
      // No previous or next sibling, so treat this like an appended frame.
      *aIsAppend = PR_TRUE;
      if (IsFrameSpecial(aParentFrame)) {
        // Since we're appending, we'll walk to the last anonymous frame
        // that was created for the broken inline frame.  But don't walk
        // to the trailing inline if it's empty; stop at the block.
        aParentFrame = GetLastSpecialSibling(aParentFrame, PR_FALSE);
      }
      // Get continuation that parents the last child.  This MUST be done
      // before the AdjustAppendParentForAfterContent call.
      aParentFrame = nsLayoutUtils::GetLastContinuationWithChild(aParentFrame);
      // Deal with fieldsets
      aParentFrame = ::GetAdjustedParentFrame(aParentFrame,
                                              aParentFrame->GetType(),
                                              aChild);
      nsIFrame* appendAfterFrame;
      aParentFrame =
        ::AdjustAppendParentForAfterContent(mPresShell->GetPresContext(),
                                            container, aParentFrame,
                                            &appendAfterFrame);
      prevSibling = ::FindAppendPrevSibling(aParentFrame, appendAfterFrame);
    }
  }

  *aIsRangeInsertSafe = (childDisplay == UNSET_DISPLAY);
  return prevSibling;
}

static PRBool
IsSpecialFramesetChild(nsIContent* aContent)
{
  // IMPORTANT: This must match the conditions in nsHTMLFramesetFrame::Init.
  return aContent->IsHTML() &&
    (aContent->Tag() == nsGkAtoms::frameset ||
     aContent->Tag() == nsGkAtoms::frame);
}

static void
InvalidateCanvasIfNeeded(nsIPresShell* presShell, nsIContent* node);

#ifdef MOZ_XUL

static
PRBool
IsXULListBox(nsIContent* aContainer)
{
  return (aContainer->IsXUL() && aContainer->Tag() == nsGkAtoms::listbox);
}

static
nsListBoxBodyFrame*
MaybeGetListBoxBodyFrame(nsIContent* aContainer, nsIContent* aChild)
{
  if (!aContainer)
    return nsnull;

  if (IsXULListBox(aContainer) &&
      aChild->IsXUL() && aChild->Tag() == nsGkAtoms::listitem) {
    nsCOMPtr<nsIDOMXULElement> xulElement = do_QueryInterface(aContainer);
    nsCOMPtr<nsIBoxObject> boxObject;
    xulElement->GetBoxObject(getter_AddRefs(boxObject));
    nsCOMPtr<nsPIListBoxObject> listBoxObject = do_QueryInterface(boxObject);
    if (listBoxObject) {
      return listBoxObject->GetListBoxBody(PR_FALSE);
    }
  }

  return nsnull;
}
#endif

void
nsCSSFrameConstructor::AddTextItemIfNeeded(nsFrameConstructorState& aState,
                                           nsIFrame* aParentFrame,
                                           nsIContent* aPossibleTextContent,
                                           FrameConstructionItemList& aItems)
{
  NS_PRECONDITION(aPossibleTextContent, "Must have node");
  if (!aPossibleTextContent->IsNodeOfType(nsINode::eTEXT) ||
      !aPossibleTextContent->HasFlag(NS_CREATE_FRAME_IF_NON_WHITESPACE)) {
    // Not text, or not suppressed due to being all-whitespace (if it
    // were being suppressed, it would have the
    // NS_CREATE_FRAME_IF_NON_WHITESPACE flag)
    return;
  }
  NS_ASSERTION(!aPossibleTextContent->GetPrimaryFrame(),
               "Text node has a frame and NS_CREATE_FRAME_IF_NON_WHITESPACE");
  AddFrameConstructionItems(aState, aPossibleTextContent, PR_FALSE,
                            aParentFrame, aItems);
}

void
nsCSSFrameConstructor::ReframeTextIfNeeded(nsIContent* aParentContent,
                                           nsIContent* aContent)
{
  if (!aContent->IsNodeOfType(nsINode::eTEXT) ||
      !aContent->HasFlag(NS_CREATE_FRAME_IF_NON_WHITESPACE)) {
    // Not text, or not suppressed due to being all-whitespace (if it
    // were being suppressed, it would have the
    // NS_CREATE_FRAME_IF_NON_WHITESPACE flag)
    return;
  }
  NS_ASSERTION(!aContent->GetPrimaryFrame(),
               "Text node has a frame and NS_CREATE_FRAME_IF_NON_WHITESPACE");
  ContentInserted(aParentContent, aContent, nsnull, PR_FALSE);
}

// We want to disable lazy frame construction for nodes that are under an
// editor. We use nsINode::IsEditable, but that includes inputs with type text
// and password and textareas, which are common and aren't really editable (the
// native anonymous content under them is what is actually editable) so we want
// to construct frames for those lazily.
// The logic for this check is based on
// nsGenericHTMLFormElement::UpdateEditableFormControlState and so must be kept
// in sync with that. The presence of the NODE_MAY_HAVE_CONTENT_EDITABLE_ATTR
// flag only indicates a contenteditable attribute, it doesn't indicate if it
// is true or false, so we force eager construction in some cases when the node
// is not editable, but that should be rare.
static inline PRBool
IsActuallyEditable(nsIContent* aContainer, nsIContent* aChild)
{
  return (aChild->IsEditable() &&
          (aContainer->IsEditable() ||
           aChild->HasFlag(NODE_MAY_HAVE_CONTENT_EDITABLE_ATTR)));
}

// For inserts aChild should be valid, for appends it should be null.
// Returns true if this operation can be lazy, false if not.
PRBool
nsCSSFrameConstructor::MaybeConstructLazily(Operation aOperation,
                                            nsIContent* aContainer,
                                            nsIContent* aChild)
{
  if (mPresShell->GetPresContext()->IsChrome() || !aContainer ||
      aContainer->IsInNativeAnonymousSubtree() || aContainer->IsXUL()) {
    return PR_FALSE;
  }

  if (aOperation == CONTENTINSERT) {
    if (aChild->IsRootOfAnonymousSubtree() ||
        aChild->IsXUL() || IsActuallyEditable(aContainer, aChild)) {
      return PR_FALSE;
    }
  } else { // CONTENTAPPEND
    NS_ASSERTION(aOperation == CONTENTAPPEND,
                 "operation should be either insert or append");
    for (nsIContent* child = aChild; child; child = child->GetNextSibling()) {
      NS_ASSERTION(!child->IsRootOfAnonymousSubtree(),
                   "Should be coming through the CONTENTAPPEND case");
      if (child->IsXUL() || IsActuallyEditable(aContainer, child)) {
        return PR_FALSE;
      }
    }
  }

  // We can construct lazily; just need to set suitable bits in the content
  // tree.

  // Walk up the tree setting the NODE_DESCENDANTS_NEED_FRAMES bit as we go.
  nsIContent* content = aContainer;
#ifdef DEBUG
  // If we hit a node with no primary frame, or the NODE_NEEDS_FRAME bit set
  // we want to assert, but leaf frames that process their own children and may
  // ignore anonymous children (eg framesets) make this complicated. So we set
  // these two booleans if we encounter these situations and unset them if we
  // hit a node with a leaf frame.
  PRBool noPrimaryFrame = PR_FALSE;
  PRBool needsFrameBitSet = PR_FALSE;
#endif
  while (content &&
         !content->HasFlag(NODE_DESCENDANTS_NEED_FRAMES)) {
#ifdef DEBUG
    if (content->GetPrimaryFrame() && content->GetPrimaryFrame()->IsLeaf()) {
      noPrimaryFrame = needsFrameBitSet = PR_FALSE;
    }
    if (!noPrimaryFrame && !content->GetPrimaryFrame()) {
      noPrimaryFrame = PR_TRUE;
    }
    if (!needsFrameBitSet && content->HasFlag(NODE_NEEDS_FRAME)) {
      needsFrameBitSet = PR_TRUE;
    }
#endif
    content->SetFlags(NODE_DESCENDANTS_NEED_FRAMES);
    content = content->GetFlattenedTreeParent();
  }
#ifdef DEBUG
  if (content && content->GetPrimaryFrame() &&
      content->GetPrimaryFrame()->IsLeaf()) {
    noPrimaryFrame = needsFrameBitSet = PR_FALSE;
  }
  NS_ASSERTION(!noPrimaryFrame, "Ancestors of nodes with frames to be "
    "constructed lazily should have frames");
  NS_ASSERTION(!needsFrameBitSet, "Ancestors of nodes with frames to be "
    "constructed lazily should not have NEEDS_FRAME bit set");
#endif

  // Set NODE_NEEDS_FRAME on the new nodes.
  if (aOperation == CONTENTINSERT) {
    NS_ASSERTION(!aChild->GetPrimaryFrame() ||
                 aChild->GetPrimaryFrame()->GetContent() != aChild,
                 //XXX the aChild->GetPrimaryFrame()->GetContent() != aChild
                 // check is needed due to bug 135040. Remove it once that's
                 // fixed.
                 "setting NEEDS_FRAME on a node that already has a frame?");
    aChild->SetFlags(NODE_NEEDS_FRAME);
  } else { // CONTENTAPPEND
    for (nsIContent* child = aChild; child; child = child->GetNextSibling()) {
      NS_ASSERTION(!child->GetPrimaryFrame() ||
                   child->GetPrimaryFrame()->GetContent() != child,
                   //XXX the child->GetPrimaryFrame()->GetContent() != child
                   // check is needed due to bug 135040. Remove it once that's
                   // fixed.
                   "setting NEEDS_FRAME on a node that already has a frame?");
      child->SetFlags(NODE_NEEDS_FRAME);
    }
  }

  PostRestyleEventInternal(PR_TRUE);
  return PR_TRUE;
}

void
nsCSSFrameConstructor::CreateNeededFrames(nsIContent* aContent)
{
  NS_ASSERTION(!aContent->HasFlag(NODE_NEEDS_FRAME),
    "shouldn't get here with a content node that has needs frame bit set");
  NS_ASSERTION(aContent->HasFlag(NODE_DESCENDANTS_NEED_FRAMES),
    "should only get here with a content node that has descendants needing frames");

  aContent->UnsetFlags(NODE_DESCENDANTS_NEED_FRAMES);

  // We could either descend first (on nodes that don't have NODE_NEEDS_FRAME
  // set) or issue content notifications for our kids first. In absence of
  // anything definitive either way we'll go with the latter.

  // It might be better to use GetChildArray and scan it completely first and
  // then issue all notifications. (We have to scan it completely first because
  // constructing frames can set attributes, which can change the storage of
  // child lists).

  // Scan the children of aContent to see what operations (if any) we need to
  // perform.
  PRUint32 childCount = aContent->GetChildCount();
  PRBool inRun = PR_FALSE;
  nsIContent* firstChildInRun = nsnull;
  for (PRUint32 i = 0; i < childCount; i++) {
    nsIContent* child = aContent->GetChildAt(i);
    if (child->HasFlag(NODE_NEEDS_FRAME)) {
      NS_ASSERTION(!child->GetPrimaryFrame() ||
                   child->GetPrimaryFrame()->GetContent() != child,
                   //XXX the child->GetPrimaryFrame()->GetContent() != child
                   // check is needed due to bug 135040. Remove it once that's
                   // fixed.
                   "NEEDS_FRAME set on a node that already has a frame?");
      if (!inRun) {
        inRun = PR_TRUE;
        firstChildInRun = child;
      }
    } else {
      if (inRun) {
        inRun = PR_FALSE;
        // generate a ContentRangeInserted for [startOfRun,i)
        ContentRangeInserted(aContent, firstChildInRun, child, nsnull,
                             PR_FALSE);
      }
    }
  }
  if (inRun) {
    ContentAppended(aContent, firstChildInRun, PR_FALSE);
  }

  // Now descend.
  ChildIterator iter, last;
  for (ChildIterator::Init(aContent, &iter, &last);
       iter != last;
       ++iter) {
    nsIContent* child = *iter;
    if (child->HasFlag(NODE_DESCENDANTS_NEED_FRAMES)) {
      CreateNeededFrames(child);
    }
  }
}

void nsCSSFrameConstructor::CreateNeededFrames()
{
  NS_ASSERTION(!nsContentUtils::IsSafeToRunScript(),
               "Someone forgot a script blocker");

  Element* rootElement = mDocument->GetRootElement();
  NS_ASSERTION(!rootElement || !rootElement->HasFlag(NODE_NEEDS_FRAME),
    "root element should not have frame created lazily");
  if (rootElement && rootElement->HasFlag(NODE_DESCENDANTS_NEED_FRAMES)) {
    BeginUpdate();
    CreateNeededFrames(rootElement);
    EndUpdate();
  }
}

void
nsCSSFrameConstructor::IssueSingleInsertNofications(nsIContent* aContainer,
                                                    nsIContent* aStartChild,
                                                    nsIContent* aEndChild,
                                                    PRBool aAllowLazyConstruction)
{
  for (nsIContent* child = aStartChild;
       child != aEndChild;
       child = child->GetNextSibling()) {
    if ((child->GetPrimaryFrame() ||
         mPresShell->FrameManager()->GetUndisplayedContent(child))
#ifdef MOZ_XUL
        //  Except listboxes suck, so do NOT skip anything here if
        //  we plan to notify a listbox.
        && !MaybeGetListBoxBodyFrame(aContainer, child)
#endif
        ) {
      // Already have a frame or undisplayed entry for this content; a
      // previous ContentInserted in this loop must have reconstructed
      // its insertion parent.  Skip it.
      continue;
    }
    // Call ContentInserted with this node.
    ContentInserted(aContainer, child, mTempFrameTreeState,
                    aAllowLazyConstruction);
  }
}

nsIFrame*
nsCSSFrameConstructor::GetRangeInsertionPoint(nsIContent* aContainer,
                                              nsIFrame* aParentFrame,
                                              nsIContent* aStartChild,
                                              nsIContent* aEndChild,
                                              PRBool aAllowLazyConstruction)
{
  // See if we have an XBL insertion point. If so, then that's our
  // real parent frame; if not, then the frame hasn't been built yet
  // and we just bail.
  nsIFrame* insertionPoint;
  PRBool multiple = PR_FALSE;
  GetInsertionPoint(aParentFrame, nsnull, &insertionPoint, &multiple);
  if (! insertionPoint)
    return nsnull; // Don't build the frames.
 
  PRBool hasInsertion = PR_FALSE;
  if (!multiple) {
    // XXXbz XBL2/sXBL issue
    nsIDocument* document = aStartChild->GetDocument();
    // XXXbz how would |document| be null here?
    if (document &&
        document->BindingManager()->GetInsertionParent(aStartChild)) {
      hasInsertion = PR_TRUE;
    }
  }

  if (multiple || hasInsertion) {
    // We have an insertion point.  There are some additional tests we need to do
    // in order to ensure that an append is a safe operation.
    PRUint32 childCount = 0;

    if (!multiple) {
      // We may need to make multiple ContentInserted calls instead.  A
      // reasonable heuristic to employ (in order to maintain good performance)
      // is to find out if the insertion point's content node contains any
      // explicit children.  If it does not, then it is highly likely that 
      // an append is occurring.  (Note it is not definite, and there are insane
      // cases we will not deal with by employing this heuristic, but it beats
      // always falling back to multiple ContentInserted calls).
      //
      // In the multiple insertion point case, we know we're going to need to do
      // multiple ContentInserted calls anyway.
      childCount = insertionPoint->GetContent()->GetChildCount();
    }
 
    // If we have multiple insertion points or if we have an insertion point
    // and the operation is not a true append or if the insertion point already
    // has explicit children, then we must fall back.
    if (multiple || aEndChild != nsnull || childCount > 0) {
      // Now comes the fun part.  For each inserted child, make a
      // ContentInserted call as if it had just gotten inserted and
      // let ContentInserted handle the mess.
      IssueSingleInsertNofications(aContainer, aStartChild, aEndChild,
                                   aAllowLazyConstruction);
      return nsnull;
    }
  }

  return insertionPoint;
}

PRBool
nsCSSFrameConstructor::MaybeRecreateForFrameset(nsIFrame* aParentFrame,
                                                nsIContent* aStartChild,
                                                nsIContent* aEndChild)
{
  if (aParentFrame->GetType() == nsGkAtoms::frameSetFrame) {
    // Check whether we have any kids we care about.
    for (nsIContent* cur = aStartChild;
         cur != aEndChild;
         cur = cur->GetNextSibling()) {
      if (IsSpecialFramesetChild(cur)) {
        // Just reframe the parent, since framesets are weird like that.
        RecreateFramesForContent(aParentFrame->GetContent(), PR_FALSE);
        return PR_TRUE;
      }
    }
  }
  return PR_FALSE;
}

nsresult
nsCSSFrameConstructor::ContentAppended(nsIContent*     aContainer,
                                       nsIContent*     aFirstNewContent,
                                       PRBool          aAllowLazyConstruction)
{
  AUTO_LAYOUT_PHASE_ENTRY_POINT(mPresShell->GetPresContext(), FrameC);
  NS_PRECONDITION(mUpdateCount != 0,
                  "Should be in an update while creating frames");

#ifdef DEBUG
  if (gNoisyContentUpdates) {
    printf("nsCSSFrameConstructor::ContentAppended container=%p "
           "first-child=%p lazy=%d\n",
           static_cast<void*>(aContainer), aFirstNewContent,
           aAllowLazyConstruction);
    if (gReallyNoisyContentUpdates && aContainer) {
      aContainer->List(stdout, 0);
    }
  }
#endif

#ifdef MOZ_XUL
  if (aContainer) {
    PRInt32 namespaceID;
    nsIAtom* tag =
      mDocument->BindingManager()->ResolveTag(aContainer, &namespaceID);

    // Just ignore tree tags, anyway we don't create any frames for them.
    if (tag == nsGkAtoms::treechildren ||
        tag == nsGkAtoms::treeitem ||
        tag == nsGkAtoms::treerow)
      return NS_OK;

  }
#endif // MOZ_XUL

  // Get the frame associated with the content
  nsIFrame* parentFrame = GetFrameFor(aContainer);
  if (! parentFrame)
    return NS_OK;

  if (aAllowLazyConstruction &&
      MaybeConstructLazily(CONTENTAPPEND, aContainer, aFirstNewContent)) {
    return NS_OK;
  }

  LAYOUT_PHASE_TEMP_EXIT();
  parentFrame = GetRangeInsertionPoint(aContainer, parentFrame,
                                       aFirstNewContent, nsnull,
                                       aAllowLazyConstruction);
  LAYOUT_PHASE_TEMP_REENTER();
  if (!parentFrame) {
    return NS_OK;
  }

  LAYOUT_PHASE_TEMP_EXIT();
  if (MaybeRecreateForFrameset(parentFrame, aFirstNewContent, nsnull)) {
    LAYOUT_PHASE_TEMP_REENTER();
    return NS_OK;
  }
  LAYOUT_PHASE_TEMP_REENTER();  

  if (parentFrame->IsLeaf()) {
    // Nothing to do here; we shouldn't be constructing kids of leaves
    // Clear lazy bits so we don't try to construct again.
    ClearLazyBits(aFirstNewContent, nsnull);
    return NS_OK;
  }
  
#ifdef MOZ_MATHML
  if (parentFrame->IsFrameOfType(nsIFrame::eMathML)) {
    LAYOUT_PHASE_TEMP_EXIT();
    nsresult rv = RecreateFramesForContent(parentFrame->GetContent(), PR_FALSE);
    LAYOUT_PHASE_TEMP_REENTER();
    return rv;
  }
#endif

  // If the frame we are manipulating is a ``special'' frame (that is, one
  // that's been created as a result of a block-in-inline situation) then we
  // need to append to the last special sibling, not to the frame itself.
  PRBool parentSpecial = IsFrameSpecial(parentFrame);
  if (parentSpecial) {
#ifdef DEBUG
    if (gNoisyContentUpdates) {
      printf("nsCSSFrameConstructor::ContentAppended: parentFrame=");
      nsFrame::ListTag(stdout, parentFrame);
      printf(" is special\n");
    }
#endif

    // Since we're appending, we'll walk to the last anonymous frame
    // that was created for the broken inline frame.  But don't walk
    // to the trailing inline if it's empty; stop at the block.
    parentFrame = GetLastSpecialSibling(parentFrame, PR_FALSE);
  }

  // Get continuation that parents the last child.  This MUST be done
  // before the AdjustAppendParentForAfterContent call.
  parentFrame = nsLayoutUtils::GetLastContinuationWithChild(parentFrame);

  // We should never get here with fieldsets, since they have multiple
  // insertion points.
  NS_ASSERTION(parentFrame->GetType() != nsGkAtoms::fieldSetFrame,
               "Unexpected parent");

  // Deal with possible :after generated content on the parent
  nsIFrame* parentAfterFrame;
  parentFrame =
    ::AdjustAppendParentForAfterContent(mPresShell->GetPresContext(),
                                        aContainer, parentFrame,
                                        &parentAfterFrame);
  
  // Create some new frames
  nsFrameConstructorState state(mPresShell, mFixedContainingBlock,
                                GetAbsoluteContainingBlock(parentFrame),
                                GetFloatContainingBlock(parentFrame));

  // See if the containing block has :first-letter style applied.
  PRBool haveFirstLetterStyle = PR_FALSE, haveFirstLineStyle = PR_FALSE;
  nsIFrame* containingBlock = state.mFloatedItems.containingBlock;
  if (containingBlock) {
    haveFirstLetterStyle = HasFirstLetterStyle(containingBlock);
    haveFirstLineStyle =
      ShouldHaveFirstLineStyle(containingBlock->GetContent(),
                               containingBlock->GetStyleContext());
  }

  if (haveFirstLetterStyle) {
    // Before we get going, remove the current letter frames
    RemoveLetterFrames(state.mPresContext, state.mPresShell,
                       state.mFrameManager, containingBlock);
  }

  nsIAtom* frameType = parentFrame->GetType();
  PRBool haveNoXBLChildren =
    mDocument->BindingManager()->GetXBLChildNodesFor(aContainer) == nsnull;
  FrameConstructionItemList items;
  if (aFirstNewContent->GetPreviousSibling() &&
      GetParentType(frameType) == eTypeBlock &&
      haveNoXBLChildren) {
    // If there's a text node in the normal content list just before the new
    // items, and it has no frame, make a frame construction item for it. If it
    // doesn't need a frame, ConstructFramesFromItemList below won't give it
    // one.  No need to do all this if our parent type is not block, though,
    // since WipeContainingBlock already handles that situation.
    //
    // Because we're appending, we don't need to worry about any text
    // after the appended content; there can only be XBL anonymous content
    // (text in an XBL binding is not suppressed) or generated content
    // (and bare text nodes are not generated). Native anonymous content
    // generated by frames never participates in inline layout.
    AddTextItemIfNeeded(state, parentFrame,
                        aFirstNewContent->GetPreviousSibling(), items);
  }
  for (nsIContent* child = aFirstNewContent;
       child;
       child = child->GetNextSibling()) {
    AddFrameConstructionItems(state, child, PR_FALSE, parentFrame, items);
  }

  nsIFrame* prevSibling = ::FindAppendPrevSibling(parentFrame, parentAfterFrame);

  // Perform special check for diddling around with the frames in
  // a special inline frame.
  // If we're appending before :after content, then we're not really
  // appending, so let WipeContainingBlock know that.
  LAYOUT_PHASE_TEMP_EXIT();
  if (WipeContainingBlock(state, containingBlock, parentFrame, items,
                          PR_TRUE, prevSibling)) {
    LAYOUT_PHASE_TEMP_REENTER();
    return NS_OK;
  }
  LAYOUT_PHASE_TEMP_REENTER();

  // If the parent is a block frame, and we're not in a special case
  // where frames can be moved around, determine if the list is for the
  // start or end of the block.
  if (nsLayoutUtils::GetAsBlock(parentFrame) && !haveFirstLetterStyle &&
      !haveFirstLineStyle && !parentSpecial) {
    items.SetLineBoundaryAtStart(!prevSibling ||
        !prevSibling->GetStyleDisplay()->IsInlineOutside() ||
        prevSibling->GetType() == nsGkAtoms::brFrame);
    // :after content can't be <br> so no need to check it
    items.SetLineBoundaryAtEnd(!parentAfterFrame ||
        !parentAfterFrame->GetStyleDisplay()->IsInlineOutside());
  }
  // To suppress whitespace-only text frames, we have to verify that
  // our container's DOM child list matches its flattened tree child list.
  // This is guaranteed to be true if GetXBLChildNodesFor() returns null.
  items.SetParentHasNoXBLChildren(haveNoXBLChildren);

  nsFrameItems frameItems;
  ConstructFramesFromItemList(state, items, parentFrame, frameItems);

  for (nsIContent* child = aFirstNewContent;
       child;
       child = child->GetNextSibling()) {
    // Invalidate now instead of before the WipeContainingBlock call, just in
    // case we do wipe; in that case we don't need to do this walk at all.
    // XXXbz does that matter?  Would it make more sense to save some virtual
    // GetChildAt calls instead and do this during construction of our
    // FrameConstructionItemList?
    InvalidateCanvasIfNeeded(mPresShell, child);
  }

  // if the container is a table and a caption was appended, it needs to be put
  // in the outer table frame's additional child list.
  nsFrameItems captionItems;
  if (nsGkAtoms::tableFrame == frameType) {
    // Pull out the captions.  Note that we don't want to do that as we go,
    // because processing a single caption can add a whole bunch of things to
    // the frame items due to pseudoframe processing.  So we'd have to pull
    // captions from a list anyway; might as well do that here.
    // XXXbz this is no longer true; we could pull captions directly out of the
    // FrameConstructionItemList now.
    PullOutCaptionFrames(frameItems, captionItems);
  }
  
  if (haveFirstLineStyle && parentFrame == containingBlock) {
    // It's possible that some of the new frames go into a
    // first-line frame. Look at them and see...
    AppendFirstLineFrames(state, containingBlock->GetContent(),
                          containingBlock, frameItems); 
  }

  // Notify the parent frame passing it the list of new frames
  // Append the flowed frames to the principal child list; captions
  // need special treatment
  if (captionItems.NotEmpty()) { // append the caption to the outer table
    NS_ASSERTION(nsGkAtoms::tableFrame == frameType, "how did that happen?");
    nsIFrame* outerTable = parentFrame->GetParent();
    if (outerTable) {
      state.mFrameManager->AppendFrames(outerTable, nsGkAtoms::captionList,
                                        captionItems);
    }
  }

  if (frameItems.NotEmpty()) { // append the in-flow kids
    AppendFrames(state, parentFrame, frameItems, prevSibling);
  }

  // Recover first-letter frames
  if (haveFirstLetterStyle) {
    RecoverLetterFrames(containingBlock);
  }

#ifdef DEBUG
  if (gReallyNoisyContentUpdates) {
    printf("nsCSSFrameConstructor::ContentAppended: resulting frame model:\n");
    parentFrame->List(stdout, 0);
  }
#endif

  return NS_OK;
}

#ifdef MOZ_XUL

enum content_operation
{
    CONTENT_INSERTED,
    CONTENT_REMOVED
};

// Helper function to lookup the listbox body frame and send a notification
// for insertion or removal of content
static
PRBool NotifyListBoxBody(nsPresContext*    aPresContext,
                         nsIContent*        aContainer,
                         nsIContent*        aChild,
                         // Only used for the removed notification
                         nsIContent*        aOldNextSibling,
                         nsIDocument*       aDocument,                         
                         nsIFrame*          aChildFrame,
                         content_operation  aOperation)
{
  nsListBoxBodyFrame* listBoxBodyFrame =
    MaybeGetListBoxBodyFrame(aContainer, aChild);
  if (listBoxBodyFrame) {
    if (aOperation == CONTENT_REMOVED) {
      // Except if we have an aChildFrame and its parent is not the right
      // thing, then we don't do this.  Pseudo frames are so much fun....
      if (!aChildFrame || aChildFrame->GetParent() == listBoxBodyFrame) {
        listBoxBodyFrame->OnContentRemoved(aPresContext, aContainer,
                                           aChildFrame, aOldNextSibling);
        return PR_TRUE;
      }
    } else {
      listBoxBodyFrame->OnContentInserted(aPresContext, aChild);
      return PR_TRUE;
    }
  }

  return PR_FALSE;
}
#endif // MOZ_XUL

nsresult
nsCSSFrameConstructor::ContentInserted(nsIContent*            aContainer,
                                       nsIContent*            aChild,
                                       nsILayoutHistoryState* aFrameState,
                                       PRBool                 aAllowLazyConstruction)
{
  return ContentRangeInserted(aContainer,
                              aChild,
                              aChild->GetNextSibling(),
                              aFrameState,
                              aAllowLazyConstruction);
}

// ContentRangeInserted handles creating frames for a range of nodes that
// aren't at the end of their childlist. ContentRangeInserted isn't a real
// content notification, but rather it handles regular ContentInserted calls
// for a single node as well as the lazy construction of frames for a range of
// nodes when called from CreateNeededFrames. For a range of nodes to be
// suitable to have its frames constructed all at once they must meet the same
// conditions that ContentAppended imposes (GetRangeInsertionPoint checks
// these), plus more. Namely when finding the insertion prevsibling we must not
// need to consult something specific to any one node in the range, so that the
// insertion prevsibling would be the same for each node in the range. So we
// pass the first node in the range to GetInsertionPrevSibling, and if
// IsValidSibling (the only place GetInsertionPrevSibling might look at the
// passed in node itself) needs to resolve style on the node we record this and
// return that this range needs to be split up and inserted separately. Table
// captions need extra attention as we need to determine where to insert them
// in the caption list, while skipping any nodes in the range being inserted
// (because when we treat the caption frames the other nodes have had their
// frames constructed but not yet inserted into the frame tree).
nsresult
nsCSSFrameConstructor::ContentRangeInserted(nsIContent*            aContainer,
                                            nsIContent*            aStartChild,
                                            nsIContent*            aEndChild,
                                            nsILayoutHistoryState* aFrameState,
                                            PRBool                 aAllowLazyConstruction)
{
  AUTO_LAYOUT_PHASE_ENTRY_POINT(mPresShell->GetPresContext(), FrameC);
  NS_PRECONDITION(mUpdateCount != 0,
                  "Should be in an update while creating frames");

  NS_PRECONDITION(aStartChild, "must always pass a child");

  // XXXldb Do we need to re-resolve style to handle the CSS2 + combinator and
  // the :empty pseudo-class?
#ifdef DEBUG
  if (gNoisyContentUpdates) {
    printf("nsCSSFrameConstructor::ContentRangeInserted container=%p "
           "start-child=%p end-child=%p lazy=%d\n",
           static_cast<void*>(aContainer),
           static_cast<void*>(aStartChild), static_cast<void*>(aEndChild),
           aAllowLazyConstruction);
    if (gReallyNoisyContentUpdates) {
      if (aContainer) {
        aContainer->List(stdout,0);
      } else {
        aStartChild->List(stdout, 0);
      }
    }
  }
#endif

  nsresult rv = NS_OK;

  PRBool isSingleInsert = (aStartChild->GetNextSibling() == aEndChild);
  NS_ASSERTION(isSingleInsert || !aAllowLazyConstruction,
               "range insert shouldn't be lazy");
  NS_ASSERTION(isSingleInsert || aEndChild,
               "range should not include all nodes after aStartChild");

#ifdef MOZ_XUL
  if (aContainer && IsXULListBox(aContainer)) {
    if (isSingleInsert) {
      if (NotifyListBoxBody(mPresShell->GetPresContext(), aContainer,
                            // The insert case in NotifyListBoxBody
                            // doesn't use "old next sibling".
                            aStartChild, nsnull, 
                            mDocument, nsnull, CONTENT_INSERTED)) {
        return NS_OK;
      }
    } else {
      // We don't handle a range insert to a listbox parent, issue single
      // ContertInserted calls for each node inserted.
      LAYOUT_PHASE_TEMP_EXIT();
      IssueSingleInsertNofications(aContainer, aStartChild, aEndChild,
                                   aAllowLazyConstruction);
      LAYOUT_PHASE_TEMP_REENTER();
      return NS_OK;
    }
  }
#endif // MOZ_XUL
  
  // If we have a null parent, then this must be the document element being
  // inserted, or some other child of the document in the DOM (might be a PI,
  // say).
  if (! aContainer) {
    NS_ASSERTION(isSingleInsert,
                 "root node insertion should be a single insertion");
    Element *docElement = mDocument->GetRootElement();

    if (aStartChild != docElement) {
      // Not the root element; just bail out
      return NS_OK;
    }

    NS_PRECONDITION(nsnull == mRootElementFrame,
                    "root element frame already created");

    // Create frames for the document element and its child elements
    nsIFrame*               docElementFrame;
    rv = ConstructDocElementFrame(docElement, aFrameState, &docElementFrame);

    if (NS_SUCCEEDED(rv) && docElementFrame) {
      InvalidateCanvasIfNeeded(mPresShell, aStartChild);
#ifdef DEBUG
      if (gReallyNoisyContentUpdates) {
        printf("nsCSSFrameConstructor::ContentRangeInserted: resulting frame "
               "model:\n");
        mFixedContainingBlock->List(stdout, 0);
      }
#endif
    }

    return NS_OK;
  }

  // Otherwise, we've got parent content. Find its frame.
  nsIFrame* parentFrame = GetFrameFor(aContainer);
  if (! parentFrame)
    return NS_OK;

  if (aAllowLazyConstruction &&
      MaybeConstructLazily(CONTENTINSERT, aContainer, aStartChild)) {
    return NS_OK;
  }

  if (isSingleInsert) {
    // See if we have an XBL insertion point. If so, then that's our
    // real parent frame; if not, then the frame hasn't been built yet
    // and we just bail.
    nsIFrame* insertionPoint;
    GetInsertionPoint(parentFrame, aStartChild, &insertionPoint);
    if (! insertionPoint)
      return NS_OK; // Don't build the frames.

    parentFrame = insertionPoint;
  } else {
    // Get our insertion point. If we need to issue single ContentInserted's
    // GetRangeInsertionPoint will take care of that for us.
    LAYOUT_PHASE_TEMP_EXIT();
    parentFrame = GetRangeInsertionPoint(aContainer, parentFrame,
                                         aStartChild, aEndChild,
                                         aAllowLazyConstruction);
    LAYOUT_PHASE_TEMP_REENTER();
    if (!parentFrame) {
      return NS_OK;
    }
  }

  PRBool isAppend, isRangeInsertSafe;
  nsIFrame* prevSibling =
    GetInsertionPrevSibling(parentFrame, aContainer, aStartChild,
                            &isAppend, &isRangeInsertSafe);

  // check if range insert is safe
  if (!isSingleInsert && !isRangeInsertSafe) {
    // must fall back to a single ContertInserted for each child in the range
    LAYOUT_PHASE_TEMP_EXIT();
    IssueSingleInsertNofications(aContainer, aStartChild, aEndChild,
                                 aAllowLazyConstruction);
    LAYOUT_PHASE_TEMP_REENTER();
    return NS_OK;
  }

  nsIContent* container = parentFrame->GetContent();

  nsIAtom* frameType = parentFrame->GetType();
  LAYOUT_PHASE_TEMP_EXIT();
  if (MaybeRecreateForFrameset(parentFrame, aStartChild, aEndChild)) {
    LAYOUT_PHASE_TEMP_REENTER();
    return NS_OK;
  }
  LAYOUT_PHASE_TEMP_REENTER();

  // We should only get here with fieldsets when doing a single insert, because
  // fieldsets have multiple insertion points.
  NS_ASSERTION(isSingleInsert || frameType != nsGkAtoms::fieldSetFrame,
               "Unexpected parent");
  if (frameType == nsGkAtoms::fieldSetFrame &&
      aStartChild->Tag() == nsGkAtoms::legend) {
    // Just reframe the parent, since figuring out whether this
    // should be the new legend and then handling it is too complex.
    // We could do a little better here --- check if the fieldset already
    // has a legend which occurs earlier in its child list than this node,
    // and if so, proceed. But we'd have to extend nsFieldSetFrame
    // to locate this legend in the inserted frames and extract it.
    LAYOUT_PHASE_TEMP_EXIT();
    nsresult rv = RecreateFramesForContent(parentFrame->GetContent(), PR_FALSE);
    LAYOUT_PHASE_TEMP_REENTER();
    return rv;
  }

  // Don't construct kids of leaves
  if (parentFrame->IsLeaf()) {
    // Clear lazy bits so we don't try to construct again.
    ClearLazyBits(aStartChild, aEndChild);
    return NS_OK;
  }

#ifdef MOZ_MATHML
  if (parentFrame->IsFrameOfType(nsIFrame::eMathML)) {
    LAYOUT_PHASE_TEMP_EXIT();
    nsresult rv = RecreateFramesForContent(parentFrame->GetContent(), PR_FALSE);
    LAYOUT_PHASE_TEMP_REENTER();
    return rv;
  }
#endif

  nsFrameConstructorState state(mPresShell, mFixedContainingBlock,
                                GetAbsoluteContainingBlock(parentFrame),
                                GetFloatContainingBlock(parentFrame),
                                aFrameState);


  // Recover state for the containing block - we need to know if
  // it has :first-letter or :first-line style applied to it. The
  // reason we care is that the internal structure in these cases
  // is not the normal structure and requires custom updating
  // logic.
  nsIFrame* containingBlock = state.mFloatedItems.containingBlock;
  PRBool haveFirstLetterStyle = PR_FALSE;
  PRBool haveFirstLineStyle = PR_FALSE;

  // In order to shave off some cycles, we only dig up the
  // containing block haveFirst* flags if the parent frame where
  // the insertion/append is occurring is an inline or block
  // container. For other types of containers this isn't relevant.
  const nsStyleDisplay* parentDisplay = parentFrame->GetStyleDisplay();

  // Examine the parentFrame where the insertion is taking
  // place. If it's a certain kind of container then some special
  // processing is done.
  if ((NS_STYLE_DISPLAY_BLOCK == parentDisplay->mDisplay) ||
      (NS_STYLE_DISPLAY_LIST_ITEM == parentDisplay->mDisplay) ||
      (NS_STYLE_DISPLAY_INLINE == parentDisplay->mDisplay) ||
      (NS_STYLE_DISPLAY_INLINE_BLOCK == parentDisplay->mDisplay)) {
    // Recover the special style flags for the containing block
    if (containingBlock) {
      haveFirstLetterStyle = HasFirstLetterStyle(containingBlock);
      haveFirstLineStyle =
        ShouldHaveFirstLineStyle(containingBlock->GetContent(),
                                 containingBlock->GetStyleContext());
    }

    if (haveFirstLetterStyle) {
      // If our current parentFrame is a Letter frame, use its parent as our
      // new parent hint
      if (parentFrame->GetType() == nsGkAtoms::letterFrame) {
        // If parentFrame is out of flow, then we actually want the parent of
        // the placeholder frame.
        if (parentFrame->GetStateBits() & NS_FRAME_OUT_OF_FLOW) {
          nsPlaceholderFrame* placeholderFrame =
            state.mFrameManager->GetPlaceholderFrameFor(parentFrame);
          NS_ASSERTION(placeholderFrame, "No placeholder for out-of-flow?");
          parentFrame = placeholderFrame->GetParent();
        } else {
          parentFrame = parentFrame->GetParent();
        }
      }

      // Remove the old letter frames before doing the insertion
      RemoveLetterFrames(state.mPresContext, mPresShell,
                         state.mFrameManager,
                         state.mFloatedItems.containingBlock);

      // Removing the letterframes messes around with the frame tree, removing
      // and creating frames.  We need to reget our prevsibling, parent frame,
      // etc.
      prevSibling = GetInsertionPrevSibling(parentFrame, aContainer,
                                            aStartChild, &isAppend,
                                            &isRangeInsertSafe);

      // Need check whether a range insert is still safe.
      if (!isSingleInsert && !isRangeInsertSafe) {
        // Need to recover the letter frames first.
        RecoverLetterFrames(state.mFloatedItems.containingBlock);

        // must fall back to a single ContertInserted for each child in the range
        LAYOUT_PHASE_TEMP_EXIT();
        IssueSingleInsertNofications(aContainer, aStartChild, aEndChild,
                                     aAllowLazyConstruction);
        LAYOUT_PHASE_TEMP_REENTER();
        return NS_OK;
      }

      container = parentFrame->GetContent();
      frameType = parentFrame->GetType();
    }
  }

  if (!prevSibling) {
    // We're inserting the new frames as the first child. See if the
    // parent has a :before pseudo-element
    nsIFrame* firstChild = parentFrame->GetFirstChild(nsnull);

    if (firstChild &&
        nsLayoutUtils::IsGeneratedContentFor(container, firstChild,
                                             nsCSSPseudoElements::before)) {
      // Insert the new frames after the last continuation of the :before
      prevSibling = firstChild->GetTailContinuation();
      parentFrame = prevSibling->GetParent()->GetContentInsertionFrame();
      // Don't change isAppend here; we'll can call AppendFrames as needed, and
      // the change to our prevSibling doesn't affect that.
    }
  }

  FrameConstructionItemList items;
  ParentType parentType = GetParentType(frameType);
  PRBool haveNoXBLChildren =
    mDocument->BindingManager()->GetXBLChildNodesFor(aContainer) == nsnull;
  if (aStartChild->GetPreviousSibling() &&
      parentType == eTypeBlock && haveNoXBLChildren) {
    // If there's a text node in the normal content list just before the
    // new nodes, and it has no frame, make a frame construction item for
    // it, because it might need a frame now.  No need to do this if our
    // parent type is not block, though, since WipeContainingBlock
    // already handles that sitation.
    AddTextItemIfNeeded(state, parentFrame, aStartChild->GetPreviousSibling(),
                        items);
  }

  if (isSingleInsert) {
    AddFrameConstructionItems(state, aStartChild,
                              aStartChild->IsRootOfAnonymousSubtree(),
                              parentFrame, items);
  } else {
    for (nsIContent* child = aStartChild;
         child != aEndChild;
         child = child->GetNextSibling()){
      AddFrameConstructionItems(state, child, PR_FALSE, parentFrame, items);
    }
  }

  if (aEndChild && parentType == eTypeBlock && haveNoXBLChildren) {
    // If there's a text node in the normal content list just after the
    // new nodes, and it has no frame, make a frame construction item for
    // it, because it might need a frame now.  No need to do this if our
    // parent type is not block, though, since WipeContainingBlock
    // already handles that sitation.
    AddTextItemIfNeeded(state, parentFrame, aEndChild, items);
  }

  // Perform special check for diddling around with the frames in
  // a special inline frame.
  // If we're appending before :after content, then we're not really
  // appending, so let WipeContainingBlock know that.
  LAYOUT_PHASE_TEMP_EXIT();
  if (WipeContainingBlock(state, containingBlock, parentFrame, items,
                          isAppend, prevSibling)) {
    LAYOUT_PHASE_TEMP_REENTER();
    return NS_OK;
  }
  LAYOUT_PHASE_TEMP_REENTER();

  // If the container is a table and a caption will be appended, it needs to be
  // put in the outer table frame's additional child list.
  // We make no attempt here to set flags to indicate whether the list
  // will be at the start or end of a block. It doesn't seem worthwhile.
  nsFrameItems frameItems, captionItems;
  ConstructFramesFromItemList(state, items, parentFrame, frameItems);

  if (frameItems.NotEmpty()) {
    for (nsIContent* child = aStartChild;
         child != aEndChild;
         child = child->GetNextSibling()){
      InvalidateCanvasIfNeeded(mPresShell, child);
    }

    if (nsGkAtoms::tableFrame == frameType ||
        nsGkAtoms::tableOuterFrame == frameType) {
      PullOutCaptionFrames(frameItems, captionItems);
    }
  }

  // If the parent of our current prevSibling is different from the frame we'll
  // actually use as the parent, then the calculated insertion point is now
  // invalid and as it is unknown where to insert correctly we append instead
  // (bug 341858).
  // This can affect our prevSibling and isAppend, but should not have any
  // effect on the WipeContainingBlock above, since this should only happen
  // when neither parent is a special frame and should not affect whitespace
  // handling inside table-related frames (and in fact, can only happen when
  // one of the parents is an outer table and one is an inner table or when the
  // parent is a fieldset or fieldset content frame).  So it won't affect the
  // {ib} or XUL box cases in WipeContainingBlock(), and the table pseudo
  // handling will only be affected by us maybe thinking we're not inserting
  // at the beginning, whereas we really are.  That would have made us reframe
  // unnecessarily, but that's ok.
  // XXXbz we should push our frame construction item code up higher, so we
  // know what our items are by the time we start figuring out previous
  // siblings
  if (prevSibling && frameItems.NotEmpty() &&
      frameItems.FirstChild()->GetParent() != prevSibling->GetParent()) {
#ifdef DEBUG
    nsIFrame* frame1 = frameItems.FirstChild()->GetParent();
    nsIFrame* frame2 = prevSibling->GetParent();
    NS_ASSERTION(!IsFrameSpecial(frame1) && !IsFrameSpecial(frame2),
                 "Neither should be special");
    NS_ASSERTION((frame1->GetType() == nsGkAtoms::tableFrame &&
                  frame2->GetType() == nsGkAtoms::tableOuterFrame) ||
                 (frame1->GetType() == nsGkAtoms::tableOuterFrame &&
                  frame2->GetType() == nsGkAtoms::tableFrame) ||
                 frame1->GetType() == nsGkAtoms::fieldSetFrame ||
                 (frame1->GetParent() &&
                  frame1->GetParent()->GetType() == nsGkAtoms::fieldSetFrame),
                 "Unexpected frame types");
#endif
    isAppend = PR_TRUE;
    nsIFrame* appendAfterFrame;
    parentFrame =
      ::AdjustAppendParentForAfterContent(mPresShell->GetPresContext(),
                                          container,
                                          frameItems.FirstChild()->GetParent(),
                                          &appendAfterFrame);
    prevSibling = ::FindAppendPrevSibling(parentFrame, appendAfterFrame);
  }

  if (haveFirstLineStyle && parentFrame == containingBlock) {
    // It's possible that the new frame goes into a first-line
    // frame. Look at it and see...
    if (isAppend) {
      // Use append logic when appending
      AppendFirstLineFrames(state, containingBlock->GetContent(),
                            containingBlock, frameItems); 
    }
    else {
      // Use more complicated insert logic when inserting
      // XXXbz this method is a no-op, so it's easy for the args being passed
      // here to make no sense without anyone noticing...  If it ever stops
      // being a no-op, vet them carefully!
      InsertFirstLineFrames(state, container, containingBlock, &parentFrame,
                            prevSibling, frameItems);
    }
  }
      
  // We might have captions; put them into the caption list of the
  // outer table frame.
  if (captionItems.NotEmpty()) {
    NS_ASSERTION(nsGkAtoms::tableFrame == frameType ||
                 nsGkAtoms::tableOuterFrame == frameType,
                 "parent for caption is not table?");
    // We need to determine where to put the caption items; start with the
    // the parent frame that has already been determined and get the insertion
    // prevsibling of the first caption item.
    nsIFrame* captionParent = parentFrame;
    PRBool captionIsAppend;
    nsIFrame* captionPrevSibling = nsnull;

    // aIsRangeInsertSafe is ignored on purpose because it is irrelevant here.
    PRBool ignored;
    if (isSingleInsert) {
      captionPrevSibling =
        GetInsertionPrevSibling(captionParent, aContainer, aStartChild,
                                &captionIsAppend, &ignored);
    } else {
      nsIContent* firstCaption = captionItems.FirstChild()->GetContent();
      // It is very important here that we skip the children in
      // [aStartChild,aEndChild) when looking for a
      // prevsibling.
      captionPrevSibling =
        GetInsertionPrevSibling(captionParent, aContainer, firstCaption,
                                &captionIsAppend, &ignored,
                                aStartChild, aEndChild);
    }

    nsIFrame* outerTable = nsnull;
    if (GetCaptionAdjustedParent(captionParent, captionItems.FirstChild(),
                                 &outerTable)) {
      // If the parent is not an outer table frame we will try to add frames
      // to a named child list that the parent does not honour and the frames
      // will get lost
      NS_ASSERTION(nsGkAtoms::tableOuterFrame == outerTable->GetType(),
                   "Pseudo frame construction failure; "
                   "a caption can be only a child of an outer table frame");

      // If the parent of our current prevSibling is different from the frame
      // we'll actually use as the parent, then the calculated insertion
      // point is now invalid (bug 341382).
      if (captionPrevSibling &&
          captionPrevSibling->GetParent() != outerTable) {
          captionPrevSibling = nsnull;
      }
      if (captionIsAppend) {
        state.mFrameManager->AppendFrames(outerTable, nsGkAtoms::captionList,
                                          captionItems);
      } else {
        state.mFrameManager->InsertFrames(outerTable, nsGkAtoms::captionList,
                                          captionPrevSibling, captionItems);
      }
    }
  }

  if (frameItems.NotEmpty()) {
    // Notify the parent frame
    if (isAppend) {
      AppendFrames(state, parentFrame, frameItems, prevSibling);
    } else {
      state.mFrameManager->InsertFrames(parentFrame, nsnull, prevSibling,
                                        frameItems);
    }
  }

  if (haveFirstLetterStyle) {
    // Recover the letter frames for the containing block when
    // it has first-letter style.
    RecoverLetterFrames(state.mFloatedItems.containingBlock);
  }

#ifdef DEBUG
  if (gReallyNoisyContentUpdates && parentFrame) {
    printf("nsCSSFrameConstructor::ContentRangeInserted: resulting frame model:\n");
    parentFrame->List(stdout, 0);
  }
#endif

  return NS_OK;
}

nsresult
nsCSSFrameConstructor::ContentRemoved(nsIContent* aContainer,
                                      nsIContent* aChild,
                                      nsIContent* aOldNextSibling,
                                      RemoveFlags aFlags,
                                      PRBool*     aDidReconstruct)
{
  AUTO_LAYOUT_PHASE_ENTRY_POINT(mPresShell->GetPresContext(), FrameC);
  NS_PRECONDITION(mUpdateCount != 0,
                  "Should be in an update while destroying frames");

  *aDidReconstruct = PR_FALSE;
  
  // XXXldb Do we need to re-resolve style to handle the CSS2 + combinator and
  // the :empty pseudo-class?

#ifdef DEBUG
  if (gNoisyContentUpdates) {
    printf("nsCSSFrameConstructor::ContentRemoved container=%p child=%p "
           "old-next-sibling=%p\n",
           static_cast<void*>(aContainer),
           static_cast<void*>(aChild),
           static_cast<void*>(aOldNextSibling));
    if (gReallyNoisyContentUpdates) {
      aContainer->List(stdout, 0);
    }
  }
#endif

  nsFrameManager *frameManager = mPresShell->FrameManager();
  nsPresContext *presContext = mPresShell->GetPresContext();
  nsresult                  rv = NS_OK;

  // Find the child frame that maps the content
  nsIFrame* childFrame = aChild->GetPrimaryFrame();

  if (!childFrame || childFrame->GetContent() != aChild) {
    // XXXbz the GetContent() != aChild check is needed due to bug 135040.
    // Remove it once that's fixed.
    frameManager->ClearUndisplayedContentIn(aChild, aContainer);
  }

#ifdef MOZ_XUL
  if (NotifyListBoxBody(presContext, aContainer, aChild, aOldNextSibling, 
                        mDocument, childFrame, CONTENT_REMOVED))
    return NS_OK;

#endif // MOZ_XUL

  // If we're removing the root, then make sure to remove things starting at
  // the viewport's child instead of the primary frame (which might even be
  // null if the root had an XBL binding or display:none, even though the
  // frames above it got created).  We do the adjustment after the childFrame
  // check above, because we do want to clear any undisplayed content we might
  // have for the root.  Detecting removal of a root is a little exciting; in
  // particular, having a null aContainer is necessary but NOT sufficient.  Due
  // to how we process reframes, the content node might not even be in our
  // document by now.  So explicitly check whether the viewport's first kid's
  // content node is aChild.
  PRBool isRoot = PR_FALSE;
  if (!aContainer) {
    nsIFrame* viewport = frameManager->GetRootFrame();
    if (viewport) {
      nsIFrame* firstChild = viewport->GetFirstChild(nsnull);
      if (firstChild && firstChild->GetContent() == aChild) {
        isRoot = PR_TRUE;
        childFrame = firstChild;
        NS_ASSERTION(!childFrame->GetNextSibling(), "How did that happen?");
      }
    }
  }

  if (childFrame) {
    InvalidateCanvasIfNeeded(mPresShell, aChild);
    
    // See whether we need to remove more than just childFrame
    LAYOUT_PHASE_TEMP_EXIT();
    if (MaybeRecreateContainerForFrameRemoval(childFrame, &rv)) {
      LAYOUT_PHASE_TEMP_REENTER();
      *aDidReconstruct = PR_TRUE;
      return rv;
    }
    LAYOUT_PHASE_TEMP_REENTER();

    // Get the childFrame's parent frame
    nsIFrame* parentFrame = childFrame->GetParent();
    nsIAtom* parentType = parentFrame->GetType();

    if (parentType == nsGkAtoms::frameSetFrame &&
        IsSpecialFramesetChild(aChild)) {
      // Just reframe the parent, since framesets are weird like that.
      *aDidReconstruct = PR_TRUE;
      LAYOUT_PHASE_TEMP_EXIT();
      nsresult rv = RecreateFramesForContent(parentFrame->GetContent(), PR_FALSE);
      LAYOUT_PHASE_TEMP_REENTER();
      return rv;
    }

#ifdef MOZ_MATHML
    // If we're a child of MathML, then we should reframe the MathML content.
    // If we're non-MathML, then we would be wrapped in a block so we need to
    // check our grandparent in that case.
    nsIFrame* possibleMathMLAncestor = parentType == nsGkAtoms::blockFrame ? 
         parentFrame->GetParent() : parentFrame;
    if (possibleMathMLAncestor->IsFrameOfType(nsIFrame::eMathML)) {
      *aDidReconstruct = PR_TRUE;
      LAYOUT_PHASE_TEMP_EXIT();
      nsresult rv = RecreateFramesForContent(possibleMathMLAncestor->GetContent(), PR_FALSE);
      LAYOUT_PHASE_TEMP_REENTER();
      return rv;
    }
#endif

    // Undo XUL wrapping if it's no longer needed.
    // (If we're in the XUL block-wrapping situation, parentFrame is the
    // wrapper frame.)
    nsIFrame* grandparentFrame = parentFrame->GetParent();
    if (grandparentFrame && grandparentFrame->IsBoxFrame() &&
        (grandparentFrame->GetStateBits() & NS_STATE_BOX_WRAPS_KIDS_IN_BLOCK) &&
        // check if this frame is the only one needing wrapping
        aChild == AnyKidsNeedBlockParent(parentFrame->GetFirstChild(nsnull)) &&
        !AnyKidsNeedBlockParent(childFrame->GetNextSibling())) {
      *aDidReconstruct = PR_TRUE;
      LAYOUT_PHASE_TEMP_EXIT();
      nsresult rv = RecreateFramesForContent(grandparentFrame->GetContent(), PR_TRUE);
      LAYOUT_PHASE_TEMP_REENTER();
      return rv;
    }
    
    // Examine the containing-block for the removed content and see if
    // :first-letter style applies.
    nsIFrame* inflowChild = childFrame;
    if (childFrame->GetStateBits() & NS_FRAME_OUT_OF_FLOW) {
      inflowChild = frameManager->GetPlaceholderFrameFor(childFrame);
      NS_ASSERTION(inflowChild, "No placeholder for out-of-flow?");
    }
    nsIFrame* containingBlock =
      GetFloatContainingBlock(inflowChild->GetParent());
    PRBool haveFLS = containingBlock && HasFirstLetterStyle(containingBlock);
    if (haveFLS) {
      // Trap out to special routine that handles adjusting a blocks
      // frame tree when first-letter style is present.
#ifdef NOISY_FIRST_LETTER
      printf("ContentRemoved: containingBlock=");
      nsFrame::ListTag(stdout, containingBlock);
      printf(" parentFrame=");
      nsFrame::ListTag(stdout, parentFrame);
      printf(" childFrame=");
      nsFrame::ListTag(stdout, childFrame);
      printf("\n");
#endif

      // First update the containing blocks structure by removing the
      // existing letter frames. This makes the subsequent logic
      // simpler.
      RemoveLetterFrames(presContext, mPresShell, frameManager,
                         containingBlock);

      // Recover childFrame and parentFrame
      childFrame = aChild->GetPrimaryFrame();
      if (!childFrame || childFrame->GetContent() != aChild) {
        // XXXbz the GetContent() != aChild check is needed due to bug 135040.
        // Remove it once that's fixed.
        frameManager->ClearUndisplayedContentIn(aChild, aContainer);
        return NS_OK;
      }
      parentFrame = childFrame->GetParent();
      parentType = parentFrame->GetType();

#ifdef NOISY_FIRST_LETTER
      printf("  ==> revised parentFrame=");
      nsFrame::ListTag(stdout, parentFrame);
      printf(" childFrame=");
      nsFrame::ListTag(stdout, childFrame);
      printf("\n");
#endif
    }

#ifdef DEBUG
    if (gReallyNoisyContentUpdates) {
      printf("nsCSSFrameConstructor::ContentRemoved: childFrame=");
      nsFrame::ListTag(stdout, childFrame);
      putchar('\n');
      parentFrame->List(stdout, 0);
    }
#endif


    // Notify the parent frame that it should delete the frame
    if (childFrame->GetStateBits() & NS_FRAME_OUT_OF_FLOW) {
      childFrame = frameManager->GetPlaceholderFrameFor(childFrame);
      NS_ASSERTION(childFrame, "Missing placeholder frame for out of flow.");
      parentFrame = childFrame->GetParent();
    }
    rv = frameManager->RemoveFrame(nsLayoutUtils::GetChildListNameFor(childFrame),
                                   childFrame);
    //XXXfr NS_ENSURE_SUCCESS(rv, rv) ?

    if (isRoot) {
      mRootElementFrame = nsnull;
      mRootElementStyleFrame = nsnull;
      mDocElementContainingBlock = nsnull;
      mPageSequenceFrame = nsnull;
      mGfxScrollFrame = nsnull;
      mHasRootAbsPosContainingBlock = PR_FALSE;
      mFixedContainingBlock = frameManager->GetRootFrame();
    }

    if (haveFLS && mRootElementFrame) {
      RecoverLetterFrames(containingBlock);
    }

    // If we're just reconstructing frames for the element, then the
    // following ContentInserted notification on the element will
    // take care of fixing up any adjacent text nodes.  We don't need
    // to do this if the table parent type of our parent type is not
    // eTypeBlock, though, because in that case the whitespace isn't
    // being suppressed due to us anyway.
    if (aContainer && !aChild->IsRootOfAnonymousSubtree() &&
        aFlags != REMOVE_FOR_RECONSTRUCTION &&
        GetParentType(parentType) == eTypeBlock) {
      // Adjacent whitespace-only text nodes might have been suppressed if
      // this node does not have inline ends. Create frames for them now
      // if necessary.
      // Reframe any text node just before the node being removed, if there is
      // one, and if it's not the last child or the first child. If a whitespace
      // textframe was being suppressed and it's now the last child or first
      // child then it can stay suppressed since the parent must be a block
      // and hence it's adjacent to a block end.
      // If aOldNextSibling is null, then the text node before the node being
      // removed is the last node, and we don't need to worry about it.
      if (aOldNextSibling) {
        nsIContent* prevSibling = aOldNextSibling->GetPreviousSibling();
        if (prevSibling && prevSibling->GetPreviousSibling()) {
          LAYOUT_PHASE_TEMP_EXIT();
          ReframeTextIfNeeded(aContainer, prevSibling);
          LAYOUT_PHASE_TEMP_REENTER();
        }
      }
      // Reframe any text node just after the node being removed, if there is
      // one, and if it's not the last child or the first child.
      if (aOldNextSibling && aOldNextSibling->GetNextSibling() &&
          aOldNextSibling->GetPreviousSibling()) {
        LAYOUT_PHASE_TEMP_EXIT();
        ReframeTextIfNeeded(aContainer, aOldNextSibling);
        LAYOUT_PHASE_TEMP_REENTER();
      }
    }

#ifdef DEBUG
    if (gReallyNoisyContentUpdates && parentFrame) {
      printf("nsCSSFrameConstructor::ContentRemoved: resulting frame model:\n");
      parentFrame->List(stdout, 0);
    }
#endif
  }

  return rv;
}

#ifdef DEBUG
  // To ensure that the functions below are only called within
  // |ApplyRenderingChangeToTree|.
static PRBool gInApplyRenderingChangeToTree = PR_FALSE;
#endif

static void
DoApplyRenderingChangeToTree(nsIFrame* aFrame,
                             nsIViewManager* aViewManager,
                             nsFrameManager* aFrameManager,
                             nsChangeHint aChange);

/**
 * @param aBoundsRect returns the bounds enclosing the areas covered by aFrame and its childre
 * This rect is relative to aFrame's parent
 */
static void
UpdateViewsForTree(nsIFrame* aFrame, nsIViewManager* aViewManager,
                   nsFrameManager* aFrameManager,
                   nsChangeHint aChange)
{
  NS_PRECONDITION(gInApplyRenderingChangeToTree,
                  "should only be called within ApplyRenderingChangeToTree");

  nsIView* view = aFrame->GetView();
  if (view) {
    if (aChange & nsChangeHint_SyncFrameView) {
      nsContainerFrame::SyncFrameViewProperties(aFrame->PresContext(),
                                                aFrame, nsnull, view);
    }
  }

  // now do children of frame
  PRInt32 listIndex = 0;
  nsIAtom* childList = nsnull;

  do {
    nsIFrame* child = aFrame->GetFirstChild(childList);
    while (child) {
      if (!(child->GetStateBits() & NS_FRAME_OUT_OF_FLOW)
          || (child->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER)) {
        // only do frames that don't have placeholders
        if (nsGkAtoms::placeholderFrame == child->GetType()) {
          // do the out-of-flow frame and its continuations
          nsIFrame* outOfFlowFrame =
            nsPlaceholderFrame::GetRealFrameForPlaceholder(child);
          do {
            DoApplyRenderingChangeToTree(outOfFlowFrame, aViewManager,
                                         aFrameManager, aChange);
          } while (outOfFlowFrame = outOfFlowFrame->GetNextContinuation());
        } else if (childList == nsGkAtoms::popupList) {
          DoApplyRenderingChangeToTree(child, aViewManager,
                                       aFrameManager, aChange);
        } else {  // regular frame
          if ((child->GetStateBits() & NS_FRAME_HAS_CONTAINER_LAYER) &&
              (aChange & nsChangeHint_RepaintFrame)) {
            FrameLayerBuilder::InvalidateThebesLayerContents(child,
              child->GetOverflowRectRelativeToSelf());
          }
          UpdateViewsForTree(child, aViewManager, aFrameManager, aChange);
        }
      }
      child = child->GetNextSibling();
    }
    childList = aFrame->GetAdditionalChildListName(listIndex++);
  } while (childList);
}

static void
DoApplyRenderingChangeToTree(nsIFrame* aFrame,
                             nsIViewManager* aViewManager,
                             nsFrameManager* aFrameManager,
                             nsChangeHint aChange)
{
  NS_PRECONDITION(gInApplyRenderingChangeToTree,
                  "should only be called within ApplyRenderingChangeToTree");

  for ( ; aFrame; aFrame = nsLayoutUtils::GetNextContinuationOrSpecialSibling(aFrame)) {
    // Get view if this frame has one and trigger an update. If the
    // frame doesn't have a view, find the nearest containing view
    // (adjusting r's coordinate system to reflect the nesting) and
    // update there.
    UpdateViewsForTree(aFrame, aViewManager, aFrameManager, aChange);

    // if frame has view, will already be invalidated
    if (aChange & nsChangeHint_RepaintFrame) {
      if (aFrame->IsFrameOfType(nsIFrame::eSVG)) {
#ifdef MOZ_SVG
        if (!(aFrame->GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)) {
          nsSVGOuterSVGFrame *outerSVGFrame = nsSVGUtils::GetOuterSVGFrame(aFrame);
          if (outerSVGFrame) {
            // We need this to invalidate frames when their 'filter' or 'marker'
            // property changes. XXX in theory changes to 'marker' should be
            // handled in nsSVGPathGeometryFrame::DidSetStyleContext, but for
            // some reason that's broken.
            //
            // This call is also currently the only mechanism for invalidating
            // the area covered by a <foreignObject> when 'opacity' changes on
            // it or one of its ancestors. (For 'opacity' changes on <image> or
            // a graphical element such as <path>, or on one of their
            // ancestors, this is redundant since
            // nsSVGPathGeometryFrame::DidSetStyleContext also invalidates.)
            outerSVGFrame->UpdateAndInvalidateCoveredRegion(aFrame);
          }
        }
#endif
      } else {
        aFrame->InvalidateOverflowRect();
      }
    }
    if (aChange & nsChangeHint_UpdateOpacityLayer) {
      aFrame->MarkLayersActive();
      aFrame->InvalidateLayer(aFrame->GetOverflowRectRelativeToSelf(),
                              nsDisplayItem::TYPE_OPACITY);
    }
    
    if (aChange & nsChangeHint_UpdateTransformLayer) {
      aFrame->MarkLayersActive();
      aFrame->InvalidateLayer(aFrame->GetOverflowRectRelativeToSelf(),
                              nsDisplayItem::TYPE_TRANSFORM);
    }
  }
}

static void
ApplyRenderingChangeToTree(nsPresContext* aPresContext,
                           nsIFrame* aFrame,
                           nsChangeHint aChange)
{
  nsIPresShell *shell = aPresContext->PresShell();
  if (shell->IsPaintingSuppressed()) {
    // Don't allow synchronous rendering changes when painting is turned off.
    aChange = NS_SubtractHint(aChange, nsChangeHint_RepaintFrame);
    if (!aChange) {
      return;
    }
  }

  // If the frame's background is propagated to an ancestor, walk up to
  // that ancestor.
  nsStyleContext *bgSC;
  while (!nsCSSRendering::FindBackground(aPresContext, aFrame, &bgSC)) {
    aFrame = aFrame->GetParent();
    NS_ASSERTION(aFrame, "root frame must paint");
  }

  nsIViewManager* viewManager = shell->GetViewManager();

  // Trigger rendering updates by damaging this frame and any
  // continuations of this frame.

  // XXX this needs to detect the need for a view due to an opacity change and deal with it...

  nsIViewManager::UpdateViewBatch batch(viewManager);

#ifdef DEBUG
  gInApplyRenderingChangeToTree = PR_TRUE;
#endif
  DoApplyRenderingChangeToTree(aFrame, viewManager, shell->FrameManager(),
                               aChange);
#ifdef DEBUG
  gInApplyRenderingChangeToTree = PR_FALSE;
#endif
  
  batch.EndUpdateViewBatch(NS_VMREFRESH_NO_SYNC);
}

/**
 * This method invalidates the canvas when frames are removed or added for a
 * node that might have its background propagated to the canvas, i.e., a
 * document root node or an HTML BODY which is a child of the root node.
 *
 * @param aFrame a frame for a content node about to be removed or a frame that
 *               was just created for a content node that was inserted.
 */ 
static void
InvalidateCanvasIfNeeded(nsIPresShell* presShell, nsIContent* node)
{
  NS_PRECONDITION(presShell->GetRootFrame(), "What happened here?");
  NS_PRECONDITION(presShell->GetPresContext(), "Say what?");

  //  Note that both in ContentRemoved and ContentInserted the content node
  //  will still have the right parent pointer, so looking at that is ok.
  
  nsIContent* parent = node->GetParent();
  if (parent) {
    // Has a parent; might not be what we want
    nsIContent* grandParent = parent->GetParent();
    if (grandParent) {
      // Has a grandparent, so not what we want
      return;
    }

    // Check whether it's an HTML body
    if (node->Tag() != nsGkAtoms::body ||
        !node->IsHTML()) {
      return;
    }
  }

  // At this point the node has no parent or it's an HTML <body> child of the
  // root.  We might not need to invalidate in this case (eg we might be in
  // XHTML or something), but chances are we want to.  Play it safe.
  // Invalidate the viewport.

  // Wrap this in a DEFERRED view update batch so we don't try to
  // flush out layout here

  nsIViewManager::UpdateViewBatch batch(presShell->GetViewManager());
  nsIFrame* rootFrame = presShell->GetRootFrame();
  rootFrame->InvalidateOverflowRect();
  batch.EndUpdateViewBatch(NS_VMREFRESH_DEFERRED);
}

nsresult
nsCSSFrameConstructor::StyleChangeReflow(nsIFrame* aFrame,
                                         nsChangeHint aHint)
{
  // If the frame hasn't even received an initial reflow, then don't
  // send it a style-change reflow!
  if (aFrame->GetStateBits() & NS_FRAME_FIRST_REFLOW)
    return NS_OK;

#ifdef DEBUG
  if (gNoisyContentUpdates) {
    printf("nsCSSFrameConstructor::StyleChangeReflow: aFrame=");
    nsFrame::ListTag(stdout, aFrame);
    printf("\n");
  }
#endif

  nsIPresShell::IntrinsicDirty dirtyType;
  if (aHint & nsChangeHint_ClearDescendantIntrinsics) {
    NS_ASSERTION(aHint & nsChangeHint_ClearAncestorIntrinsics,
                 "Please read the comments in nsChangeHint.h");
    dirtyType = nsIPresShell::eStyleChange;
  } else if (aHint & nsChangeHint_ClearAncestorIntrinsics) {
    dirtyType = nsIPresShell::eTreeChange;
  } else {
    dirtyType = nsIPresShell::eResize;
  }

  nsFrameState dirtyBits;
  if (aHint & nsChangeHint_NeedDirtyReflow) {
    dirtyBits = NS_FRAME_IS_DIRTY;
  } else {
    dirtyBits = NS_FRAME_HAS_DIRTY_CHILDREN;
  }

  do {
    mPresShell->FrameNeedsReflow(aFrame, dirtyType, dirtyBits);
    aFrame = nsLayoutUtils::GetNextContinuationOrSpecialSibling(aFrame);
  } while (aFrame);

  return NS_OK;
}

nsresult
nsCSSFrameConstructor::CharacterDataChanged(nsIContent* aContent,
                                            CharacterDataChangeInfo* aInfo)
{
  AUTO_LAYOUT_PHASE_ENTRY_POINT(mPresShell->GetPresContext(), FrameC);
  nsresult      rv = NS_OK;

  if ((aContent->HasFlag(NS_CREATE_FRAME_IF_NON_WHITESPACE) &&
       !aContent->TextIsOnlyWhitespace()) ||
      (aContent->HasFlag(NS_REFRAME_IF_WHITESPACE) &&
       aContent->TextIsOnlyWhitespace())) {
#ifdef DEBUG
    nsIFrame* frame = aContent->GetPrimaryFrame();
    NS_ASSERTION(!frame || !frame->IsGeneratedContentFrame(),
                 "Bit should never be set on generated content");
#endif
    LAYOUT_PHASE_TEMP_EXIT();
    nsresult rv = RecreateFramesForContent(aContent, PR_FALSE);
    LAYOUT_PHASE_TEMP_REENTER();
    return rv;
  }

  // Find the child frame
  nsIFrame* frame = aContent->GetPrimaryFrame();

  // Notify the first frame that maps the content. It will generate a reflow
  // command

  // It's possible the frame whose content changed isn't inserted into the
  // frame hierarchy yet, or that there is no frame that maps the content
  if (nsnull != frame) {
#if 0
    NS_FRAME_LOG(NS_FRAME_TRACE_CALLS,
       ("nsCSSFrameConstructor::CharacterDataChanged: content=%p[%s] subcontent=%p frame=%p",
        aContent, ContentTag(aContent, 0),
        aSubContent, frame));
#endif

    // Special check for text content that is a child of a letter frame.  If
    // this happens, we should remove the letter frame, do whatever we're
    // planning to do with this notification, then put the letter frame back.
    // Note that this is basically what RecreateFramesForContent ends up doing;
    // the reason we dont' want to call that here is that our text content
    // could be native anonymous, in which case RecreateFramesForContent would
    // completely barf on it.  And recreating the non-anonymous ancestor would
    // just lead us to come back into this notification (e.g. if quotes or
    // counters are involved), leading to a loop.
    nsIFrame* block = GetFloatContainingBlock(frame);
    PRBool haveFirstLetterStyle = PR_FALSE;
    if (block) {
      // See if the block has first-letter style applied to it.
      haveFirstLetterStyle = HasFirstLetterStyle(block);
      if (haveFirstLetterStyle) {
        RemoveLetterFrames(mPresShell->GetPresContext(), mPresShell,
                           mPresShell->FrameManager(), block);
        // Reget |frame|, since we might have killed it.
        // Do we really need to call CharacterDataChanged in this case, though?
        frame = aContent->GetPrimaryFrame();
        NS_ASSERTION(frame, "Should have frame here!");
      }
    }

    frame->CharacterDataChanged(aInfo);

    if (haveFirstLetterStyle) {
      RecoverLetterFrames(block);
    }
  }

  return rv;
}

NS_DECLARE_FRAME_PROPERTY(ChangeListProperty, nsnull)

nsresult
nsCSSFrameConstructor::ProcessRestyledFrames(nsStyleChangeList& aChangeList)
{
  NS_ASSERTION(!nsContentUtils::IsSafeToRunScript(),
               "Someone forgot a script blocker");
  PRInt32 count = aChangeList.Count();
  if (!count)
    return NS_OK;

  // Make sure to not rebuild quote or counter lists while we're
  // processing restyles
  BeginUpdate();

  nsPresContext* presContext = mPresShell->GetPresContext();
  FramePropertyTable* propTable = presContext->PropertyTable();

  // Mark frames so that we skip frames that die along the way, bug 123049.
  // A frame can be in the list multiple times with different hints. Further
  // optmization is possible if nsStyleChangeList::AppendChange could coalesce
  PRInt32 index = count;

  while (0 <= --index) {
    const nsStyleChangeData* changeData;
    aChangeList.ChangeAt(index, &changeData);
    if (changeData->mFrame) {
      propTable->Set(changeData->mFrame, ChangeListProperty(),
                     NS_INT32_TO_PTR(1));
    }
  }

  index = count;
  PRBool didInvalidate = PR_FALSE;
  PRBool didReflow = PR_FALSE;

  while (0 <= --index) {
    nsIFrame* frame;
    nsIContent* content;
    nsChangeHint hint;
    aChangeList.ChangeAt(index, frame, content, hint);

    NS_ASSERTION(!(hint & nsChangeHint_ReflowFrame) ||
                 (hint & nsChangeHint_NeedReflow),
                 "Reflow hint bits set without actually asking for a reflow");

    if (frame && frame->GetContent() != content) {
      // XXXbz this is due to image maps messing with the primary frame of
      // <area>s.  See bug 135040.  Remove this block once that's fixed.
      frame = nsnull;
      if (!(hint & nsChangeHint_ReconstructFrame)) {
        continue;
      }
    }

    // skip any frame that has been destroyed due to a ripple effect
    if (frame) {
      if (!propTable->Get(frame, ChangeListProperty()))
        continue;
    }

    if (hint & nsChangeHint_ReconstructFrame) {
      RecreateFramesForContent(content, PR_FALSE);
    } else {
      NS_ASSERTION(frame, "This shouldn't happen");
#ifdef MOZ_SVG
      if (hint & nsChangeHint_UpdateEffects) {
        nsSVGEffects::UpdateEffects(frame);
      }
#endif
      if (hint & nsChangeHint_NeedReflow) {
        StyleChangeReflow(frame, hint);
        didReflow = PR_TRUE;
      }
      if (hint & (nsChangeHint_RepaintFrame | nsChangeHint_SyncFrameView |
                  nsChangeHint_UpdateOpacityLayer | nsChangeHint_UpdateTransformLayer)) {
        ApplyRenderingChangeToTree(presContext, frame, hint);
        didInvalidate = PR_TRUE;
      }
      if (hint & nsChangeHint_UpdateCursor) {
        nsIViewManager* viewMgr = mPresShell->GetViewManager();
        if (viewMgr)
          viewMgr->SynthesizeMouseMove(PR_FALSE);
      }
    }
  }

  EndUpdate();

  if (didInvalidate && !didReflow) {
    // RepaintFrame changes can indicate changes in opacity etc which
    // can require plugin clipping to change. If we requested a reflow,
    // we don't need to do this since the reflow will do it for us.
    nsIFrame* rootFrame = mPresShell->FrameManager()->GetRootFrame();
    nsRootPresContext* rootPC = presContext->GetRootPresContext();
    if (rootPC) {
      rootPC->RequestUpdatePluginGeometry(rootFrame);
    }
  }

  // cleanup references and verify the style tree.  Note that the latter needs
  // to happen once we've processed the whole list, since until then the tree
  // is not in fact in a consistent state.
  index = count;
  while (0 <= --index) {
    const nsStyleChangeData* changeData;
    aChangeList.ChangeAt(index, &changeData);
    if (changeData->mFrame) {
      propTable->Delete(changeData->mFrame, ChangeListProperty());
    }

#ifdef DEBUG
    // reget frame from content since it may have been regenerated...
    if (changeData->mContent) {
      nsIFrame* frame = changeData->mContent->GetPrimaryFrame();
      if (frame) {
        mPresShell->FrameManager()->DebugVerifyStyleTree(frame);
      }
    } else {
      NS_WARNING("Unable to test style tree integrity -- no content node");
    }
#endif
  }

  aChangeList.Clear();
  return NS_OK;
}

void
nsCSSFrameConstructor::RestyleElement(Element        *aElement,
                                      nsIFrame       *aPrimaryFrame,
                                      nsChangeHint   aMinHint,
                                      RestyleTracker& aRestyleTracker,
                                      PRBool          aRestyleDescendants)
{
  NS_ASSERTION(aPrimaryFrame == aElement->GetPrimaryFrame(),
               "frame/content mismatch");
  if (aPrimaryFrame && aPrimaryFrame->GetContent() != aElement) {
    // XXXbz this is due to image maps messing with the primary frame pointer
    // of <area>s.  See bug 135040.  We can remove this block once that's fixed.
    aPrimaryFrame = nsnull;
  }
  NS_ASSERTION(!aPrimaryFrame || aPrimaryFrame->GetContent() == aElement,
               "frame/content mismatch");

  if (aMinHint & nsChangeHint_ReconstructFrame) {
    RecreateFramesForContent(aElement, PR_FALSE);
  } else if (aPrimaryFrame) {
    nsStyleChangeList changeList;
    mPresShell->FrameManager()->
      ComputeStyleChangeFor(aPrimaryFrame, &changeList, aMinHint,
                            aRestyleTracker, aRestyleDescendants);
    ProcessRestyledFrames(changeList);
  } else {
    // no frames, reconstruct for content
    MaybeRecreateFramesForElement(aElement);
  }
}

nsresult
nsCSSFrameConstructor::ContentStatesChanged(nsIContent* aContent1,
                                            nsIContent* aContent2,
                                            PRInt32 aStateMask) 
{
  // XXXbz it would be good if this function only took Elements, but
  // we'd have to make ESM guarantee that usefully.
  if (NS_LIKELY(aContent1 && aContent1->IsElement())) {
    DoContentStateChanged(aContent1->AsElement(), aStateMask);
  }
  if (aContent2 && aContent2->IsElement()) {
    DoContentStateChanged(aContent2->AsElement(), aStateMask);
  }
  return NS_OK;
}

void
nsCSSFrameConstructor::DoContentStateChanged(Element* aElement,
                                             PRInt32 aStateMask) 
{
  nsStyleSet *styleSet = mPresShell->StyleSet();
  nsPresContext *presContext = mPresShell->GetPresContext();
  NS_ASSERTION(styleSet, "couldn't get style set");

  nsChangeHint hint = NS_STYLE_HINT_NONE;
  // Any change to a content state that affects which frames we construct
  // must lead to a frame reconstruct here if we already have a frame.
  // Note that we never decide through non-CSS means to not create frames
  // based on content states, so if we already don't have a frame we don't
  // need to force a reframe -- if it's needed, the HasStateDependentStyle
  // call will handle things.
  nsIFrame* primaryFrame = aElement->GetPrimaryFrame();
  if (primaryFrame) {
    // If it's generated content, ignore LOADING/etc state changes on it.
    if (!primaryFrame->IsGeneratedContentFrame() &&
        (aStateMask & (NS_EVENT_STATE_BROKEN | NS_EVENT_STATE_USERDISABLED |
                       NS_EVENT_STATE_SUPPRESSED | NS_EVENT_STATE_LOADING))) {
      hint = nsChangeHint_ReconstructFrame;
    } else {
      PRUint8 app = primaryFrame->GetStyleDisplay()->mAppearance;
      if (app) {
        nsITheme *theme = presContext->GetTheme();
        if (theme && theme->ThemeSupportsWidget(presContext,
                                                primaryFrame, app)) {
          PRBool repaint = PR_FALSE;
          theme->WidgetStateChanged(primaryFrame, app, nsnull, &repaint);
          if (repaint) {
            NS_UpdateHint(hint, nsChangeHint_RepaintFrame);
          }
        }
      }
    }
  }

  nsRestyleHint rshint = 
    styleSet->HasStateDependentStyle(presContext, aElement, aStateMask);
      
  if ((aStateMask & NS_EVENT_STATE_HOVER) && rshint != 0) {
    ++mHoverGeneration;
  }

  if (aStateMask & NS_EVENT_STATE_VISITED) {
    // Exposing information to the page about whether the link is
    // visited or not isn't really something we can worry about here.
    // FIXME: We could probably do this a bit better.
    NS_UpdateHint(hint, nsChangeHint_RepaintFrame);
  }

  PostRestyleEvent(aElement, rshint, hint);
}

void
nsCSSFrameConstructor::AttributeWillChange(Element* aElement,
                                           PRInt32 aNameSpaceID,
                                           nsIAtom* aAttribute,
                                           PRInt32 aModType)
{
  nsRestyleHint rshint =
    mPresShell->StyleSet()->HasAttributeDependentStyle(mPresShell->GetPresContext(),
                                                       aElement,
                                                       aAttribute,
                                                       aModType,
                                                       PR_FALSE);
  PostRestyleEvent(aElement, rshint, NS_STYLE_HINT_NONE);
}

void
nsCSSFrameConstructor::AttributeChanged(Element* aElement,
                                        PRInt32 aNameSpaceID,
                                        nsIAtom* aAttribute,
                                        PRInt32 aModType)
{
  // Hold onto the PresShell to prevent ourselves from being destroyed.
  // XXXbz how, exactly, would this attribute change cause us to be
  // destroyed from inside this function?
  nsCOMPtr<nsIPresShell> shell = mPresShell;

  // Get the frame associated with the content which is the highest in the frame tree
  nsIFrame* primaryFrame = aElement->GetPrimaryFrame();

#if 0
  NS_FRAME_LOG(NS_FRAME_TRACE_CALLS,
     ("HTMLStyleSheet::AttributeChanged: content=%p[%s] frame=%p",
      aContent, ContentTag(aElement, 0), frame));
#endif

  // the style tag has its own interpretation based on aHint 
  nsChangeHint hint = aElement->GetAttributeChangeHint(aAttribute, aModType);

  PRBool reframe = (hint & nsChangeHint_ReconstructFrame) != 0;

#ifdef MOZ_XUL
  // The following listbox widget trap prevents offscreen listbox widget
  // content from being removed and re-inserted (which is what would
  // happen otherwise).
  if (!primaryFrame && !reframe) {
    PRInt32 namespaceID;
    nsIAtom* tag =
      mDocument->BindingManager()->ResolveTag(aElement, &namespaceID);

    if (namespaceID == kNameSpaceID_XUL &&
        (tag == nsGkAtoms::listitem ||
         tag == nsGkAtoms::listcell))
      return;
  }

  if (aAttribute == nsGkAtoms::tooltiptext ||
      aAttribute == nsGkAtoms::tooltip) 
  {
    nsIRootBox* rootBox = nsIRootBox::GetRootBox(mPresShell);
    if (rootBox) {
      if (aModType == nsIDOMMutationEvent::REMOVAL)
        rootBox->RemoveTooltipSupport(aElement);
      if (aModType == nsIDOMMutationEvent::ADDITION)
        rootBox->AddTooltipSupport(aElement);
    }
  }

#endif // MOZ_XUL

  if (primaryFrame) {
    // See if we have appearance information for a theme.
    const nsStyleDisplay* disp = primaryFrame->GetStyleDisplay();
    if (disp->mAppearance) {
      nsPresContext* presContext = mPresShell->GetPresContext();
      nsITheme *theme = presContext->GetTheme();
      if (theme && theme->ThemeSupportsWidget(presContext, primaryFrame, disp->mAppearance)) {
        PRBool repaint = PR_FALSE;
        theme->WidgetStateChanged(primaryFrame, disp->mAppearance, aAttribute, &repaint);
        if (repaint)
          NS_UpdateHint(hint, nsChangeHint_RepaintFrame);
      }
    }
   
    // let the frame deal with it now, so we don't have to deal later
    primaryFrame->AttributeChanged(aNameSpaceID, aAttribute, aModType);
    // XXXwaterson should probably check for special IB siblings
    // here, and propagate the AttributeChanged notification to
    // them, as well. Currently, inline frames don't do anything on
    // this notification, so it's not that big a deal.
  }

  // See if we can optimize away the style re-resolution -- must be called after
  // the frame's AttributeChanged() in case it does something that affects the style
  nsRestyleHint rshint =
    mPresShell->StyleSet()->HasAttributeDependentStyle(mPresShell->GetPresContext(),
                                                       aElement,
                                                       aAttribute,
                                                       aModType,
                                                       PR_TRUE);

  PostRestyleEvent(aElement, rshint, hint);
}

void
nsCSSFrameConstructor::BeginUpdate() {
  NS_ASSERTION(!nsContentUtils::IsSafeToRunScript(),
               "Someone forgot a script blocker");

  ++mUpdateCount;
}

void
nsCSSFrameConstructor::EndUpdate()
{
  if (mUpdateCount == 1) {
    // This is the end of our last update.  Before we decrement
    // mUpdateCount, recalc quotes and counters as needed.

    RecalcQuotesAndCounters();
    NS_ASSERTION(mUpdateCount == 1, "Odd update count");
  }
  --mUpdateCount;
}

void
nsCSSFrameConstructor::RecalcQuotesAndCounters()
{
  if (mQuotesDirty) {
    mQuotesDirty = PR_FALSE;
    mQuoteList.RecalcAll();
  }

  if (mCountersDirty) {
    mCountersDirty = PR_FALSE;
    mCounterManager.RecalcAll();
  }

  NS_ASSERTION(!mQuotesDirty, "Quotes updates will be lost");
  NS_ASSERTION(!mCountersDirty, "Counter updates will be lost");  
}

void
nsCSSFrameConstructor::WillDestroyFrameTree()
{
#if defined(DEBUG_dbaron_off)
  mCounterManager.Dump();
#endif

  mIsDestroyingFrameTree = PR_TRUE;

  // Prevent frame tree destruction from being O(N^2)
  mQuoteList.Clear();
  mCounterManager.Clear();

  // Remove our presshell as a style flush observer.  But leave
  // mObservingRefreshDriver true so we don't readd to it even if someone tries
  // to post restyle events on us from this point on for some reason.
  mPresShell->GetPresContext()->RefreshDriver()->
    RemoveStyleFlushObserver(mPresShell);
}

//STATIC

// XXXbz I'd really like this method to go away. Once we have inline-block and
// I can just use that for sized broken images, that can happen, maybe.
void nsCSSFrameConstructor::GetAlternateTextFor(nsIContent*    aContent,
                                                nsIAtom*       aTag,  // content object's tag
                                                nsXPIDLString& aAltText)
{
  // The "alt" attribute specifies alternate text that is rendered
  // when the image can not be displayed

  // If there's no "alt" attribute, and aContent is an input    
  // element, then use the value of the "value" attribute
  if (!aContent->GetAttr(kNameSpaceID_None, nsGkAtoms::alt, aAltText) &&
      nsGkAtoms::input == aTag) {
    // If there's no "value" attribute either, then use the localized string 
    // for "Submit" as the alternate text.
    if (!aContent->GetAttr(kNameSpaceID_None, nsGkAtoms::value, aAltText)) {
      nsContentUtils::GetLocalizedString(nsContentUtils::eFORMS_PROPERTIES,
                                         "Submit", aAltText);      
    }
  }
}

nsresult
nsCSSFrameConstructor::CreateContinuingOuterTableFrame(nsIPresShell*    aPresShell,
                                                       nsPresContext*  aPresContext,
                                                       nsIFrame*        aFrame,
                                                       nsIFrame*        aParentFrame,
                                                       nsIContent*      aContent,
                                                       nsStyleContext*  aStyleContext,
                                                       nsIFrame**       aContinuingFrame)
{
  nsIFrame* newFrame = NS_NewTableOuterFrame(aPresShell, aStyleContext);

  if (newFrame) {
    newFrame->Init(aContent, aParentFrame, aFrame);
    nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);

    // Create a continuing inner table frame, and if there's a caption then
    // replicate the caption
    nsFrameItems  newChildFrames;

    nsIFrame* childFrame = aFrame->GetFirstChild(nsnull);
    if (childFrame) {
      nsIFrame* continuingTableFrame;
      nsresult rv = CreateContinuingFrame(aPresContext, childFrame, newFrame,
                                          &continuingTableFrame);
      if (NS_FAILED(rv)) {
        newFrame->Destroy();
        *aContinuingFrame = nsnull;
        return rv;
      }
      newChildFrames.AddChild(continuingTableFrame);
      
      NS_ASSERTION(!childFrame->GetNextSibling(),"there can be only one inner table frame");
    }

    // Set the outer table's initial child list
    newFrame->SetInitialChildList(nsnull, newChildFrames);
    
    *aContinuingFrame = newFrame;
    return NS_OK;
  }
  else {
    *aContinuingFrame = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
  }
}

nsresult
nsCSSFrameConstructor::CreateContinuingTableFrame(nsIPresShell* aPresShell, 
                                                  nsPresContext*  aPresContext,
                                                  nsIFrame*        aFrame,
                                                  nsIFrame*        aParentFrame,
                                                  nsIContent*      aContent,
                                                  nsStyleContext*  aStyleContext,
                                                  nsIFrame**       aContinuingFrame)
{
  nsIFrame* newFrame = NS_NewTableFrame(aPresShell, aStyleContext);

  if (newFrame) {
    newFrame->Init(aContent, aParentFrame, aFrame);
    nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);

    // Replicate any header/footer frames
    nsFrameItems  childFrames;
    nsIFrame* childFrame = aFrame->GetFirstChild(nsnull);
    for ( ; childFrame; childFrame = childFrame->GetNextSibling()) {
      // See if it's a header/footer, possibly wrapped in a scroll frame.
      nsTableRowGroupFrame* rowGroupFrame =
        static_cast<nsTableRowGroupFrame*>(childFrame);
      // If the row group was continued, then don't replicate it.
      nsIFrame* rgNextInFlow = rowGroupFrame->GetNextInFlow();
      if (rgNextInFlow) {
        rowGroupFrame->SetRepeatable(PR_FALSE);
      }
      else if (rowGroupFrame->IsRepeatable()) {
        // Replicate the header/footer frame.
        nsTableRowGroupFrame*   headerFooterFrame;
        nsFrameItems            childItems;
        nsFrameConstructorState state(mPresShell, mFixedContainingBlock,
                                      GetAbsoluteContainingBlock(newFrame),
                                      nsnull);
        state.mSetPrimaryFrames = PR_FALSE;

        headerFooterFrame = static_cast<nsTableRowGroupFrame*>
                                       (NS_NewTableRowGroupFrame(aPresShell, rowGroupFrame->GetStyleContext()));
        nsIContent* headerFooter = rowGroupFrame->GetContent();
        headerFooterFrame->Init(headerFooter, newFrame, nsnull);
        ProcessChildren(state, headerFooter, rowGroupFrame->GetStyleContext(),
                        headerFooterFrame, PR_TRUE, childItems, PR_FALSE,
                        nsnull);
        NS_ASSERTION(state.mFloatedItems.IsEmpty(), "unexpected floated element");
        headerFooterFrame->SetInitialChildList(nsnull, childItems);
        headerFooterFrame->SetRepeatable(PR_TRUE);

        // Table specific initialization
        headerFooterFrame->InitRepeatedFrame(aPresContext, rowGroupFrame);

        // XXX Deal with absolute and fixed frames...
        childFrames.AddChild(headerFooterFrame);
      }
    }
    
    // Set the table frame's initial child list
    newFrame->SetInitialChildList(nsnull, childFrames);
    
    *aContinuingFrame = newFrame;
    return NS_OK;
  }
  else {
    *aContinuingFrame = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
  }
}

nsresult
nsCSSFrameConstructor::CreateContinuingFrame(nsPresContext* aPresContext,
                                             nsIFrame*       aFrame,
                                             nsIFrame*       aParentFrame,
                                             nsIFrame**      aContinuingFrame,
                                             PRBool          aIsFluid)
{
  nsIPresShell*              shell = aPresContext->PresShell();
  nsStyleContext*            styleContext = aFrame->GetStyleContext();
  nsIFrame*                  newFrame = nsnull;
  nsresult                   rv = NS_OK;
  nsIFrame*                  nextContinuation = aFrame->GetNextContinuation();
  nsIFrame*                  nextInFlow = aFrame->GetNextInFlow();

  // Use the frame type to determine what type of frame to create
  nsIAtom* frameType = aFrame->GetType();
  nsIContent* content = aFrame->GetContent();

  NS_ASSERTION(aFrame->GetSplittableType() != NS_FRAME_NOT_SPLITTABLE,
               "why CreateContinuingFrame for a non-splittable frame?");
  
  if (nsGkAtoms::textFrame == frameType) {
    newFrame = NS_NewContinuingTextFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
    }
    
  } else if (nsGkAtoms::inlineFrame == frameType) {
    newFrame = NS_NewInlineFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
    }
  
  } else if (nsGkAtoms::blockFrame == frameType) {
    newFrame = NS_NewBlockFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
    }
  
#ifdef MOZ_XUL
  } else if (nsGkAtoms::XULLabelFrame == frameType) {
    newFrame = NS_NewXULLabelFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
    }
#endif  
  } else if (nsGkAtoms::columnSetFrame == frameType) {
    newFrame = NS_NewColumnSetFrame(shell, styleContext, 0);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
    }
  
  } else if (nsGkAtoms::positionedInlineFrame == frameType) {
    newFrame = NS_NewPositionedInlineFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
    }

  } else if (nsGkAtoms::pageFrame == frameType) {
    nsIFrame* canvasFrame;
    rv = ConstructPageFrame(shell, aPresContext, aParentFrame, aFrame,
                            newFrame, canvasFrame);
  } else if (nsGkAtoms::tableOuterFrame == frameType) {
    rv = CreateContinuingOuterTableFrame(shell, aPresContext, aFrame, aParentFrame,
                                         content, styleContext, &newFrame);

  } else if (nsGkAtoms::tableFrame == frameType) {
    rv = CreateContinuingTableFrame(shell, aPresContext, aFrame, aParentFrame,
                                    content, styleContext, &newFrame);

  } else if (nsGkAtoms::tableRowGroupFrame == frameType) {
    newFrame = NS_NewTableRowGroupFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
    }

  } else if (nsGkAtoms::tableRowFrame == frameType) {
    newFrame = NS_NewTableRowFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);

      // Create a continuing frame for each table cell frame
      nsFrameItems  newChildList;
      nsIFrame* cellFrame = aFrame->GetFirstChild(nsnull);
      while (cellFrame) {
        // See if it's a table cell frame
        if (IS_TABLE_CELL(cellFrame->GetType())) {
          nsIFrame* continuingCellFrame;
          rv = CreateContinuingFrame(aPresContext, cellFrame, newFrame,
                                     &continuingCellFrame);
          if (NS_FAILED(rv)) {
            newChildList.DestroyFrames();
            newFrame->Destroy();
            *aContinuingFrame = nsnull;
            return NS_ERROR_OUT_OF_MEMORY;
          }
          newChildList.AddChild(continuingCellFrame);
        }
        cellFrame = cellFrame->GetNextSibling();
      }
      
      // Set the table cell's initial child list
      newFrame->SetInitialChildList(nsnull, newChildList);
    }

  } else if (IS_TABLE_CELL(frameType)) {
    // Warning: If you change this and add a wrapper frame around table cell
    // frames, make sure Bug 368554 doesn't regress!
    // See IsInAutoWidthTableCellForQuirk() in nsImageFrame.cpp.
    newFrame = NS_NewTableCellFrame(shell, styleContext, IsBorderCollapse(aParentFrame));

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);

      // Create a continuing area frame
      nsIFrame* continuingBlockFrame;
      nsIFrame* blockFrame = aFrame->GetFirstChild(nsnull);
      rv = CreateContinuingFrame(aPresContext, blockFrame, newFrame,
                                 &continuingBlockFrame);
      if (NS_FAILED(rv)) {
        newFrame->Destroy();
        *aContinuingFrame = nsnull;
        return rv;
      }

      // Set the table cell's initial child list
      SetInitialSingleChild(newFrame, continuingBlockFrame);
    }
  
  } else if (nsGkAtoms::lineFrame == frameType) {
    newFrame = NS_NewFirstLineFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
    }
  
  } else if (nsGkAtoms::letterFrame == frameType) {
    newFrame = NS_NewFirstLetterFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
    }

  } else if (nsGkAtoms::imageFrame == frameType) {
    newFrame = NS_NewImageFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
    }
  } else if (nsGkAtoms::imageControlFrame == frameType) {
    newFrame = NS_NewImageControlFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
    }    
  } else if (nsGkAtoms::placeholderFrame == frameType) {
    // create a continuing out of flow frame
    nsIFrame* oofFrame = nsPlaceholderFrame::GetRealFrameForPlaceholder(aFrame);
    nsIFrame* oofContFrame;
    rv = CreateContinuingFrame(aPresContext, oofFrame, aParentFrame, &oofContFrame);
    if (NS_FAILED(rv)) {
      *aContinuingFrame = nsnull;
      return rv;
    }
    // create a continuing placeholder frame
    rv = CreatePlaceholderFrameFor(shell, content, oofContFrame, styleContext,
                                   aParentFrame, aFrame,
                                   aFrame->GetStateBits() & PLACEHOLDER_TYPE_MASK,
                                   &newFrame);
    if (NS_FAILED(rv)) {
      oofContFrame->Destroy();
      *aContinuingFrame = nsnull;
      return rv;
    }
  } else if (nsGkAtoms::fieldSetFrame == frameType) {
    newFrame = NS_NewFieldSetFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);

      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);

      // Create a continuing area frame
      // XXXbz we really shouldn't have to do this by hand!
      nsIFrame* continuingBlockFrame;
      nsIFrame* blockFrame = GetFieldSetBlockFrame(aFrame);
      rv = CreateContinuingFrame(aPresContext, blockFrame, newFrame,
                                 &continuingBlockFrame);
      if (NS_FAILED(rv)) {
        newFrame->Destroy();
        *aContinuingFrame = nsnull;
        return rv;
      }
      // Set the fieldset's initial child list
      SetInitialSingleChild(newFrame, continuingBlockFrame);
    }
  } else if (nsGkAtoms::legendFrame == frameType) {
    newFrame = NS_NewLegendFrame(shell, styleContext);

    if (newFrame) {
      newFrame->Init(content, aParentFrame, aFrame);
      nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);
    }
  } else {
    NS_NOTREACHED("unexpected frame type");
    *aContinuingFrame = nsnull;
    return NS_ERROR_UNEXPECTED;
  }

  *aContinuingFrame = newFrame;

  if (!newFrame) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Init() set newFrame to be a fluid continuation of aFrame.
  // If we want a non-fluid continuation, we need to call SetPrevContinuation()
  // to reset NS_FRAME_IS_FLUID_CONTINUATION.
  if (!aIsFluid) {
    newFrame->SetPrevContinuation(aFrame);
  }

  // A continuation of generated content is also generated content
  if (aFrame->GetStateBits() & NS_FRAME_GENERATED_CONTENT) {
    newFrame->AddStateBits(NS_FRAME_GENERATED_CONTENT);
  }

  // A continuation of an out-of-flow is also an out-of-flow
  if (aFrame->GetStateBits() & NS_FRAME_OUT_OF_FLOW) {
    newFrame->AddStateBits(NS_FRAME_OUT_OF_FLOW);
  }

  if (nextInFlow) {
    nextInFlow->SetPrevInFlow(newFrame);
    newFrame->SetNextInFlow(nextInFlow);
  } else if (nextContinuation) {
    nextContinuation->SetPrevContinuation(newFrame);
    newFrame->SetNextContinuation(nextContinuation);
  }

  NS_POSTCONDITION(!newFrame->GetNextSibling(), "unexpected sibling");
  return NS_OK;
}

nsresult
nsCSSFrameConstructor::ReplicateFixedFrames(nsPageContentFrame* aParentFrame)
{
  // Now deal with fixed-pos things....  They should appear on all pages,
  // so we want to move over the placeholders when processing the child
  // of the pageContentFrame.

  nsIFrame* prevPageContentFrame = aParentFrame->GetPrevInFlow();
  if (!prevPageContentFrame) {
    return NS_OK;
  }
  nsIFrame* canvasFrame = aParentFrame->GetFirstChild(nsnull);
  nsIFrame* prevCanvasFrame = prevPageContentFrame->GetFirstChild(nsnull);
  if (!canvasFrame || !prevCanvasFrame) {
    // document's root element frame missing
    return NS_ERROR_UNEXPECTED;
  }

  nsFrameItems fixedPlaceholders;
  nsIFrame* firstFixed = prevPageContentFrame->GetFirstChild(nsGkAtoms::fixedList);
  if (!firstFixed) {
    return NS_OK;
  }

  // Don't allow abs-pos descendants of the fixed content to escape the content.
  // This should not normally be possible (because fixed-pos elements should
  // be absolute containers) but fixed-pos tables currently aren't abs-pos
  // containers.
  nsFrameConstructorState state(mPresShell, aParentFrame,
                                nsnull,
                                mRootElementFrame);
  state.mSetPrimaryFrames = PR_FALSE;

  // Iterate across fixed frames and replicate each whose placeholder is a
  // descendant of aFrame. (We don't want to explicitly copy placeholders that
  // are within fixed frames, because that would cause duplicates on the new
  // page - bug 389619)
  for (nsIFrame* fixed = firstFixed; fixed; fixed = fixed->GetNextSibling()) {
    nsIFrame* prevPlaceholder = mPresShell->FrameManager()->GetPlaceholderFrameFor(fixed);
    if (prevPlaceholder &&
        nsLayoutUtils::IsProperAncestorFrame(prevCanvasFrame, prevPlaceholder)) {
      nsresult rv = ConstructFrame(state, fixed->GetContent(),
                                   canvasFrame, fixedPlaceholders);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // Add the placeholders to our primary child list.
  // XXXbz this is a little screwed up, since the fixed frames will have 
  // broken auto-positioning. Oh, well.
  NS_ASSERTION(!canvasFrame->GetFirstChild(nsnull),
               "leaking frames; doc root continuation must be empty");
  canvasFrame->SetInitialChildList(nsnull, fixedPlaceholders);
  return NS_OK;
}

nsresult
nsCSSFrameConstructor::GetInsertionPoint(nsIFrame*     aParentFrame,
                                         nsIContent*   aChildContent,
                                         nsIFrame**    aInsertionPoint,
                                         PRBool*       aMultiple)
{
  // Make the insertion point be the parent frame by default, in case
  // we have to bail early.
  *aInsertionPoint = aParentFrame;

  nsIContent* container = aParentFrame->GetContent();
  if (!container)
    return NS_OK;

  nsBindingManager *bindingManager = mDocument->BindingManager();

  nsIContent* insertionElement;
  if (aChildContent) {
    // We've got an explicit insertion child. Check to see if it's
    // anonymous.
    if (aChildContent->GetBindingParent() == container) {
      // This child content is anonymous. Don't use the insertion
      // point, since that's only for the explicit kids.
      return NS_OK;
    }

    PRUint32 index;
    insertionElement = bindingManager->GetInsertionPoint(container,
                                                         aChildContent,
                                                         &index);
  }
  else {
    PRBool multiple;
    PRUint32 index;
    insertionElement = bindingManager->GetSingleInsertionPoint(container,
                                                               &index,
                                                               &multiple);
    if (multiple && aMultiple)
      *aMultiple = multiple; // Record the fact that filters are in use.
  }

  if (insertionElement) {
    nsIFrame* insertionPoint = insertionElement->GetPrimaryFrame();
    if (insertionPoint) {
      // Use the content insertion frame of the insertion point.
      insertionPoint = insertionPoint->GetContentInsertionFrame();
      if (insertionPoint && insertionPoint != aParentFrame) 
        GetInsertionPoint(insertionPoint, aChildContent, aInsertionPoint, aMultiple);
    }
    else {
      // There was no frame created yet for the insertion point.
      *aInsertionPoint = nsnull;
    }
  }

  // fieldsets have multiple insertion points.  Note that we might
  // have to look at insertionElement here...
  if (aMultiple && !*aMultiple) {
    nsIContent* content = insertionElement ? insertionElement : container;
    if (content->IsHTML() &&
        content->Tag() == nsGkAtoms::fieldset) {
      *aMultiple = PR_TRUE;
    }
  }

  return NS_OK;
}

// Capture state for the frame tree rooted at the frame associated with the
// content object, aContent
nsresult
nsCSSFrameConstructor::CaptureStateForFramesOf(nsIContent* aContent,
                                               nsILayoutHistoryState* aHistoryState)
{
  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (frame == mRootElementFrame) {
    frame = mFixedContainingBlock;
  }
  if (frame) {
    CaptureStateFor(frame, aHistoryState);
  }
  return NS_OK;
}

// Capture state for the frame tree rooted at aFrame.
nsresult
nsCSSFrameConstructor::CaptureStateFor(nsIFrame* aFrame,
                                       nsILayoutHistoryState* aHistoryState)
{
  if (aFrame && aHistoryState) {
    mPresShell->FrameManager()->CaptureFrameState(aFrame, aHistoryState);
  }
  return NS_OK;
}

nsresult
nsCSSFrameConstructor::MaybeRecreateFramesForElement(Element* aElement)
{
  nsresult result = NS_OK;
  nsFrameManager *frameManager = mPresShell->FrameManager();

  nsStyleContext *oldContext = frameManager->GetUndisplayedContent(aElement);
  if (oldContext) {
    // The parent has a frame, so try resolving a new context.
    nsRefPtr<nsStyleContext> newContext = mPresShell->StyleSet()->
      ResolveStyleFor(aElement, oldContext->GetParent());

    frameManager->ChangeUndisplayedContent(aElement, newContext);
    if (newContext->GetStyleDisplay()->mDisplay != NS_STYLE_DISPLAY_NONE) {
      result = RecreateFramesForContent(aElement, PR_FALSE);
    }
  }
  return result;
}

static nsIFrame*
FindFirstNonWhitespaceChild(nsIFrame* aParentFrame)
{
  nsIFrame* f = aParentFrame->GetFirstChild(nsnull);
  while (f && f->GetType() == nsGkAtoms::textFrame &&
         f->GetContent()->TextIsOnlyWhitespace()) {
    f = f->GetNextSibling();
  }
  return f;
}

static nsIFrame*
FindNextNonWhitespaceSibling(nsIFrame* aFrame)
{
  nsIFrame* f = aFrame;
  do {
    f = f->GetNextSibling();
  } while (f && f->GetType() == nsGkAtoms::textFrame &&
           f->GetContent()->TextIsOnlyWhitespace());
  return f;
}

PRBool
nsCSSFrameConstructor::MaybeRecreateContainerForFrameRemoval(nsIFrame* aFrame,
                                                             nsresult* aResult)
{
  NS_PRECONDITION(aFrame, "Must have a frame");
  NS_PRECONDITION(aFrame->GetParent(), "Frame shouldn't be root");
  NS_PRECONDITION(aResult, "Null out param?");
  NS_PRECONDITION(aFrame == aFrame->GetFirstContinuation(),
                  "aFrame not the result of GetPrimaryFrame()?");

  if (IsFrameSpecial(aFrame)) {
    // The removal functions can't handle removal of an {ib} split directly; we
    // need to rebuild the containing block.
#ifdef DEBUG
    if (gNoisyContentUpdates) {
      printf("nsCSSFrameConstructor::MaybeRecreateContainerForFrameRemoval: "
             "frame=");
      nsFrame::ListTag(stdout, aFrame);
      printf(" is special\n");
    }
#endif

    *aResult = ReframeContainingBlock(aFrame);
    return PR_TRUE;
  }

  if (aFrame->GetType() == nsGkAtoms::legendFrame &&
      aFrame->GetParent()->GetType() == nsGkAtoms::fieldSetFrame) {
    // When we remove the legend for a fieldset, we should reframe
    // the fieldset to ensure another legend is used, if there is one
    *aResult = RecreateFramesForContent(aFrame->GetParent()->GetContent(), PR_FALSE);
    return PR_TRUE;
  }

  // Now check for possibly needing to reconstruct due to a pseudo parent
  nsIFrame* inFlowFrame =
    (aFrame->GetStateBits() & NS_FRAME_OUT_OF_FLOW) ?
      mPresShell->FrameManager()->GetPlaceholderFrameFor(aFrame) : aFrame;
  NS_ASSERTION(inFlowFrame, "How did that happen?");
  nsIFrame* parent = inFlowFrame->GetParent();
  if (IsTablePseudo(parent)) {
    if (FindFirstNonWhitespaceChild(parent) == inFlowFrame ||
        !FindNextNonWhitespaceSibling(inFlowFrame->GetLastContinuation()) ||
        // If we're a table-column-group, then the GetFirstChild check above is
        // not going to catch cases when we're the first child.
        (inFlowFrame->GetType() == nsGkAtoms::tableColGroupFrame &&
         parent->GetFirstChild(nsGkAtoms::colGroupList) == inFlowFrame) ||
        // Similar if we're a table-caption.
        (inFlowFrame->GetType() == nsGkAtoms::tableCaptionFrame &&
         parent->GetFirstChild(nsGkAtoms::captionList) == inFlowFrame)) {
      // We're the first or last frame in the pseudo.  Need to reframe.
      // Good enough to recreate frames for |parent|'s content
      *aResult = RecreateFramesForContent(parent->GetContent(), PR_TRUE);
      return PR_TRUE;
    }
  }

  // Might need to reconstruct things if this frame's nextSibling is a table
  // pseudo, since removal of this frame might mean that this pseudo needs to
  // get merged with the frame's prevSibling.
  // XXXbz it would be really nice if we had the prevSibling here too, to check
  // whether this is in fact the case...
  nsIFrame* nextSibling =
    FindNextNonWhitespaceSibling(inFlowFrame->GetLastContinuation());
  NS_ASSERTION(!IsTablePseudo(inFlowFrame), "Shouldn't happen here");
  if (nextSibling && IsTablePseudo(nextSibling)) {
#ifdef DEBUG
    if (gNoisyContentUpdates) {
      printf("nsCSSFrameConstructor::MaybeRecreateContainerForFrameRemoval: "
             "frame=");
      nsFrame::ListTag(stdout, aFrame);
      printf(" has a table pseudo next sibling of different type\n");
    }
#endif
    // Good enough to recreate frames for aFrame's parent's content; even if
    // aFrame's parent is a table pseudo, that'll be the right content node.
    *aResult = RecreateFramesForContent(parent->GetContent(), PR_TRUE);
    return PR_TRUE;
  }

#ifdef MOZ_XUL
  if (aFrame->GetType() == nsGkAtoms::popupSetFrame) {
    nsIRootBox* rootBox = nsIRootBox::GetRootBox(mPresShell);
    if (rootBox && rootBox->GetPopupSetFrame() == aFrame) {
      *aResult = ReconstructDocElementHierarchy();
      return PR_TRUE;
    }
  }
#endif

  // We might still need to reconstruct things if the parent of inFlowFrame is
  // special, since in that case the removal of aFrame might affect the
  // splitting of its parent.
  if (!IsFrameSpecial(parent)) {
    return PR_FALSE;
  }

  // If inFlowFrame is not the only in-flow child of |parent|, then removing
  // it will change nothing about the {ib} split.
  if (inFlowFrame != parent->GetFirstChild(nsnull) ||
      inFlowFrame->GetLastContinuation()->GetNextSibling()) {
    return PR_FALSE;
  }

  // If the parent is the first or last part of the {ib} split, then
  // removing one of its kids will have no effect on the splitting.
  // Get the first continuation up front so we don't have to do it twice.
  nsIFrame* parentFirstContinuation = parent->GetFirstContinuation();
  if (!GetSpecialSibling(parentFirstContinuation) ||
      !GetSpecialPrevSibling(parentFirstContinuation)) {
    return PR_FALSE;
  }

#ifdef DEBUG
  if (gNoisyContentUpdates) {
    printf("nsCSSFrameConstructor::MaybeRecreateContainerForFrameRemoval: "
           "frame=");
    nsFrame::ListTag(stdout, parent);
    printf(" is special\n");
  }
#endif

  *aResult = ReframeContainingBlock(parent);
  return PR_TRUE;
}
 
nsresult
nsCSSFrameConstructor::RecreateFramesForContent(nsIContent* aContent,
                                                PRBool aAsyncInsert)
{
  NS_PRECONDITION(!aAsyncInsert || aContent->IsElement(),
                  "Can only insert elements async");
  // If there is no document, we don't want to recreate frames for it.  (You
  // shouldn't generally be giving this method content without a document
  // anyway).
  // Rebuilding the frame tree can have bad effects, especially if it's the
  // frame tree for chrome (see bug 157322).
  NS_ENSURE_TRUE(aContent->GetDocument(), NS_ERROR_FAILURE);

  // Is the frame `special'? If so, we need to reframe the containing
  // block *here*, rather than trying to remove and re-insert the
  // content (which would otherwise result in *two* nested reframe
  // containing block from ContentRemoved() and ContentInserted(),
  // below!).  We'd really like to optimize away one of those
  // containing block reframes, hence the code here.

  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (frame && frame->IsFrameOfType(nsIFrame::eMathML)) {
    // Reframe the topmost MathML element to prevent exponential blowup
    // (see bug 397518)
    while (PR_TRUE) {
      nsIContent* parentContent = aContent->GetParent();
      nsIFrame* parentContentFrame = parentContent->GetPrimaryFrame();
      if (!parentContentFrame || !parentContentFrame->IsFrameOfType(nsIFrame::eMathML))
        break;
      aContent = parentContent;
      frame = parentContentFrame;
    }
  }

  if (frame) {
    nsIFrame* nonGeneratedAncestor = nsLayoutUtils::GetNonGeneratedAncestor(frame);
    if (nonGeneratedAncestor->GetContent() != aContent) {
      return RecreateFramesForContent(nonGeneratedAncestor->GetContent(), aAsyncInsert);
    }

    nsIFrame* parent = frame->GetParent();
    nsIContent* parentContent = parent ? parent->GetContent() : nsnull;
    // If the parent frame is a leaf then the subsequent insert will fail to
    // create a frame, so we need to recreate the parent content. This happens
    // with native anonymous content from the editor.
    if (parent && parent->IsLeaf() && parentContent &&
        parentContent != aContent) {
      return RecreateFramesForContent(parentContent, aAsyncInsert);
    }
  }

  nsresult rv = NS_OK;

  if (frame && MaybeRecreateContainerForFrameRemoval(frame, &rv)) {
    return rv;
  }

  nsINode* containerNode = aContent->GetNodeParent();
  // XXXbz how can containerNode be null here?
  if (containerNode) {
    // Before removing the frames associated with the content object,
    // ask them to save their state onto a temporary state object.
    CaptureStateForFramesOf(aContent, mTempFrameTreeState);

    // Need the nsIContent parent, which might be null here, since we need to
    // pass it to ContentInserted and ContentRemoved.
    nsCOMPtr<nsIContent> container = aContent->GetParent();

    // Remove the frames associated with the content object.
    PRBool didReconstruct;
    rv = ContentRemoved(container, aContent,
                        aContent->IsRootOfAnonymousSubtree() ?
                          nsnull :
                          aContent->GetNextSibling(),
                        REMOVE_FOR_RECONSTRUCTION, &didReconstruct);

    if (NS_SUCCEEDED(rv) && !didReconstruct) {
      // Now, recreate the frames associated with this content object. If
      // ContentRemoved triggered reconstruction, then we don't need to do this
      // because the frames will already have been built.
      if (aAsyncInsert) {
        PostRestyleEvent(aContent->AsElement(), nsRestyleHint(0),
                         nsChangeHint_ReconstructFrame);
      } else {
        rv = ContentInserted(container, aContent, mTempFrameTreeState, PR_FALSE);
      }
    }
  }

#ifdef ACCESSIBILITY
  if (mPresShell->IsAccessibilityActive()) {
    PRUint32 changeType;
    if (frame) {
      nsIFrame *newFrame = aContent->GetPrimaryFrame();
      changeType = newFrame ? nsIAccessibilityService::FRAME_SIGNIFICANT_CHANGE :
                              nsIAccessibilityService::FRAME_HIDE;
    }
    else {
      changeType = nsIAccessibilityService::FRAME_SHOW;
    }

    // A significant enough change occurred that this part
    // of the accessible tree is no longer valid.
    nsCOMPtr<nsIAccessibilityService> accService = 
      do_GetService("@mozilla.org/accessibilityService;1");
    if (accService) {
      accService->InvalidateSubtreeFor(mPresShell, aContent, changeType);
    }
  }
#endif

  return rv;
}

//////////////////////////////////////////////////////////////////////

// Block frame construction code

already_AddRefed<nsStyleContext>
nsCSSFrameConstructor::GetFirstLetterStyle(nsIContent* aContent,
                                           nsStyleContext* aStyleContext)
{
  if (aContent) {
    return mPresShell->StyleSet()->
      ResolvePseudoElementStyle(aContent->AsElement(),
                                nsCSSPseudoElements::ePseudo_firstLetter,
                                aStyleContext);
  }
  return nsnull;
}

already_AddRefed<nsStyleContext>
nsCSSFrameConstructor::GetFirstLineStyle(nsIContent* aContent,
                                         nsStyleContext* aStyleContext)
{
  if (aContent) {
    return mPresShell->StyleSet()->
      ResolvePseudoElementStyle(aContent->AsElement(),
                                nsCSSPseudoElements::ePseudo_firstLine,
                                aStyleContext);
  }
  return nsnull;
}

// Predicate to see if a given content (block element) has
// first-letter style applied to it.
PRBool
nsCSSFrameConstructor::ShouldHaveFirstLetterStyle(nsIContent* aContent,
                                                  nsStyleContext* aStyleContext)
{
  return nsLayoutUtils::HasPseudoStyle(aContent, aStyleContext,
                                       nsCSSPseudoElements::ePseudo_firstLetter,
                                       mPresShell->GetPresContext());
}

PRBool
nsCSSFrameConstructor::HasFirstLetterStyle(nsIFrame* aBlockFrame)
{
  NS_PRECONDITION(aBlockFrame, "Need a frame");
  NS_ASSERTION(nsLayoutUtils::GetAsBlock(aBlockFrame),
               "Not a block frame?");

  return (aBlockFrame->GetStateBits() & NS_BLOCK_HAS_FIRST_LETTER_STYLE) != 0;
}

PRBool
nsCSSFrameConstructor::ShouldHaveFirstLineStyle(nsIContent* aContent,
                                                nsStyleContext* aStyleContext)
{
  PRBool hasFirstLine =
    nsLayoutUtils::HasPseudoStyle(aContent, aStyleContext,
                                  nsCSSPseudoElements::ePseudo_firstLine,
                                  mPresShell->GetPresContext());
  if (hasFirstLine) {
    // But disable for fieldsets
    PRInt32 namespaceID;
    nsIAtom* tag = mDocument->BindingManager()->ResolveTag(aContent,
                                                           &namespaceID);
    // This check must match the one in FindHTMLData.
    hasFirstLine = tag != nsGkAtoms::fieldset ||
      namespaceID != kNameSpaceID_XHTML;
  }

  return hasFirstLine;
}

void
nsCSSFrameConstructor::ShouldHaveSpecialBlockStyle(nsIContent* aContent,
                                                   nsStyleContext* aStyleContext,
                                                   PRBool* aHaveFirstLetterStyle,
                                                   PRBool* aHaveFirstLineStyle)
{
  *aHaveFirstLetterStyle =
    ShouldHaveFirstLetterStyle(aContent, aStyleContext);
  *aHaveFirstLineStyle =
    ShouldHaveFirstLineStyle(aContent, aStyleContext);
}

/* static */
const nsCSSFrameConstructor::PseudoParentData
nsCSSFrameConstructor::sPseudoParentData[eParentTypeCount] = {
  { // Cell
    FULL_CTOR_FCDATA(FCDATA_IS_TABLE_PART | FCDATA_SKIP_FRAMESET |
                     FCDATA_USE_CHILD_ITEMS |
                     FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRow),
                     &nsCSSFrameConstructor::ConstructTableCell),
    &nsCSSAnonBoxes::tableCell
  },
  { // Row
    FULL_CTOR_FCDATA(FCDATA_IS_TABLE_PART | FCDATA_SKIP_FRAMESET |
                     FCDATA_USE_CHILD_ITEMS |
                     FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRowGroup),
                     &nsCSSFrameConstructor::ConstructTableRow),
    &nsCSSAnonBoxes::tableRow
  },
  { // Row group
    FCDATA_DECL(FCDATA_IS_TABLE_PART | FCDATA_SKIP_FRAMESET |
                FCDATA_DISALLOW_OUT_OF_FLOW | FCDATA_USE_CHILD_ITEMS |
                FCDATA_SKIP_ABSPOS_PUSH |
                FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable),
                NS_NewTableRowGroupFrame),
    &nsCSSAnonBoxes::tableRowGroup
  },
  { // Column group
    FCDATA_DECL(FCDATA_IS_TABLE_PART | FCDATA_SKIP_FRAMESET |
                FCDATA_DISALLOW_OUT_OF_FLOW | FCDATA_USE_CHILD_ITEMS |
                FCDATA_SKIP_ABSPOS_PUSH |
                FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable),
                NS_NewTableColGroupFrame),
    &nsCSSAnonBoxes::tableColGroup
  },
  { // Table
    FULL_CTOR_FCDATA(FCDATA_SKIP_FRAMESET | FCDATA_USE_CHILD_ITEMS,
                     &nsCSSFrameConstructor::ConstructTable),
    &nsCSSAnonBoxes::table
  }
};

/*
 * This function works as follows: we walk through the child list (aItems) and
 * find items that cannot have aParentFrame as their parent.  We wrap
 * continuous runs of such items into a FrameConstructionItem for a frame that
 * gets them closer to their desired parents.  For example, a run of non-row
 * children of a row-group will get wrapped in a row.  When we later construct
 * the frame for this wrapper (in this case for the row), it'll be the correct
 * parent for the cells in the set of items we wrapped or we'll wrap cells
 * around everything else.  At the end of this method, aItems is guaranteed to
 * contain only items for frames that can be direct kids of aParentFrame.
 */
nsresult
nsCSSFrameConstructor::CreateNeededTablePseudos(nsFrameConstructorState& aState,
                                                FrameConstructionItemList& aItems,
                                                nsIFrame* aParentFrame)
{
  ParentType ourParentType = GetParentType(aParentFrame);
  if (aItems.AllWantParentType(ourParentType)) {
    // Nothing to do here
    return NS_OK;
  }

  FCItemIterator iter(aItems);
  do {
    if (iter.SkipItemsWantingParentType(ourParentType)) {
      // Nothing else to do here; we're finished
      return NS_OK;
    }

    // Now we're pointing to the first child that wants a different parent
    // type.

    // Now try to figure out what kids we can group together.  We can generally
    // group everything that has a different desired parent type from us.  Two
    // exceptions to this:
    // 1) If our parent type is table, we can't group columns with anything
    //    else other than whitespace.
    // 2) Whitespace that lies between two things we can group which both want
    //    a non-block parent should be dropped, even if we can't group them
    //    with each other and even if the whitespace wants a parent of
    //    ourParentType.  Ends of the list count as things that don't want a
    //    block parent (so that for example we'll drop a whitespace-only list).

    FCItemIterator endIter(iter); /* iterator to find the end of the group */
    ParentType groupingParentType = endIter.item().DesiredParentType();
    if (aItems.AllWantParentType(groupingParentType) &&
        groupingParentType != eTypeBlock) {
      // Just group them all and be done with it.  We need the check for
      // eTypeBlock here to catch the "all the items are whitespace" case
      // described above.
      endIter.SetToEnd();
    } else {
      // Locate the end of the group.

      // Keep track of the type the previous item wanted, in case we have to
      // deal with whitespace.  Start it off with ourParentType, since that's
      // the last thing |iter| would have skipped over.
      ParentType prevParentType = ourParentType;
      do {
        /* Walk an iterator past any whitespace that we might be able to drop from the list */
        FCItemIterator spaceEndIter(endIter);
        if (prevParentType != eTypeBlock &&
            !aParentFrame->IsGeneratedContentFrame() &&
            spaceEndIter.item().IsWhitespace(aState)) {
          PRBool trailingSpaces = spaceEndIter.SkipWhitespace(aState);

          // See whether we can drop the whitespace
          if (trailingSpaces ||
              spaceEndIter.item().DesiredParentType() != eTypeBlock) {
            PRBool updateStart = (iter == endIter);
            endIter.DeleteItemsTo(spaceEndIter);
            NS_ASSERTION(trailingSpaces == endIter.IsDone(), "These should match");

            if (updateStart) {
              iter = endIter;
            }

            if (trailingSpaces) {
              break; /* Found group end */
            }

            if (updateStart) {
              // Update groupingParentType, since it might have been eTypeBlock
              // just because of the whitespace.
              groupingParentType = iter.item().DesiredParentType();
            }
          }
        }

        // Now endIter points to a non-whitespace item or a non-droppable
        // whitespace item. In the latter case, if this is the end of the group
        // we'll traverse this whitespace again.  But it'll all just be quick
        // DesiredParentType() checks which will match ourParentType (that's
        // what it means that this is the group end), so it's OK.
        prevParentType = endIter.item().DesiredParentType();
        if (prevParentType == ourParentType) {
          // End the group at endIter.
          break;
        }

        if (ourParentType == eTypeTable &&
            (prevParentType == eTypeColGroup) !=
            (groupingParentType == eTypeColGroup)) {
          // Either we started with columns and now found something else, or vice
          // versa.  In any case, end the grouping.
          break;
        }

        // Include the whitespace we didn't drop (if any) in the group, since
        // this is not the end of the group.  Note that this doesn't change
        // prevParentType, since if we didn't drop the whitespace then we ended
        // at something that wants a block parent.
        endIter = spaceEndIter;

        endIter.Next();
      } while (!endIter.IsDone());
    }

    if (iter == endIter) {
      // Nothing to wrap here; just skipped some whitespace
      continue;
    }

    // Now group together all the items between iter and endIter.  The right
    // parent type to use depends on ourParentType.
    ParentType wrapperType;
    switch (ourParentType) {
      case eTypeBlock:
        wrapperType = eTypeTable;
        break;
      case eTypeRow:
        // The parent type for a cell is eTypeBlock, since that's what a cell
        // looks like to its kids.
        wrapperType = eTypeBlock;
        break;
      case eTypeRowGroup:
        wrapperType = eTypeRow;
        break;
      case eTypeTable:
        // Either colgroup or rowgroup, depending on what we're grouping.
        wrapperType = groupingParentType == eTypeColGroup ?
          eTypeColGroup : eTypeRowGroup;
        break;
      default:
        NS_NOTREACHED("Colgroups should be suppresing non-col child items");
        break;
    }

    const PseudoParentData& pseudoData = sPseudoParentData[wrapperType];
    nsIAtom* pseudoType = *pseudoData.mPseudoType;
    nsStyleContext* parentStyle = aParentFrame->GetStyleContext();
    nsIContent* parentContent = aParentFrame->GetContent();

    if (pseudoType == nsCSSAnonBoxes::table &&
        parentStyle->GetStyleDisplay()->mDisplay == NS_STYLE_DISPLAY_INLINE) {
      pseudoType = nsCSSAnonBoxes::inlineTable;
    }

    nsRefPtr<nsStyleContext> wrapperStyle =
      mPresShell->StyleSet()->ResolveAnonymousBoxStyle(pseudoType, parentStyle);
    FrameConstructionItem* newItem =
      new FrameConstructionItem(&pseudoData.mFCData,
                                // Use the content of our parent frame
                                parentContent,
                                // Lie about the tag; it doesn't matter anyway
                                pseudoType,
                                // The namespace does matter, however; it needs
                                // to match that of our first child item to
                                // match the old behavior
                                iter.item().mNameSpaceID,
                                // no pending binding
                                nsnull,
                                wrapperStyle.forget(),
                                PR_TRUE);

    if (!newItem) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // Here we're cheating a tad... technically, table-internal items should be
    // inline if aParentFrame is inline, but they'll get wrapped in an
    // inline-table in the end, so it'll all work out.  In any case, arguably
    // we don't need to maintain this state at this point... but it's better
    // to, I guess.
    newItem->mIsAllInline = newItem->mHasInlineEnds =
      newItem->mStyleContext->GetStyleDisplay()->IsInlineOutside();

    // Table pseudo frames always induce line boundaries around their
    // contents.
    newItem->mChildItems.SetLineBoundaryAtStart(PR_TRUE);
    newItem->mChildItems.SetLineBoundaryAtEnd(PR_TRUE);
    // The parent of the items in aItems is also the parent of the items
    // in mChildItems
    newItem->mChildItems.SetParentHasNoXBLChildren(
      aItems.ParentHasNoXBLChildren());

    // Eat up all items between |iter| and |endIter| and put them in our wrapper
    // Advances |iter| to point to |endIter|.
    iter.AppendItemsToList(endIter, newItem->mChildItems);

    iter.InsertItem(newItem);

    // Now |iter| points to the item that was the first one we didn't wrap;
    // loop and see whether we need to skip it or wrap it in something
    // different.
  } while (!iter.IsDone());

  return NS_OK;
}

inline nsresult
nsCSSFrameConstructor::ConstructFramesFromItemList(nsFrameConstructorState& aState,
                                                   FrameConstructionItemList& aItems,
                                                   nsIFrame* aParentFrame,
                                                   nsFrameItems& aFrameItems)
{
  nsresult rv = CreateNeededTablePseudos(aState, aItems, aParentFrame);
  NS_ENSURE_SUCCESS(rv, rv);

#ifdef DEBUG
  for (FCItemIterator iter(aItems); !iter.IsDone(); iter.Next()) {
    NS_ASSERTION(iter.item().DesiredParentType() == GetParentType(aParentFrame),
                 "Needed pseudos didn't get created; expect bad things");
  }
#endif

  for (FCItemIterator iter(aItems); !iter.IsDone(); iter.Next()) {
    rv = ConstructFramesFromItem(aState, iter, aParentFrame, aFrameItems);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  NS_ASSERTION(!aState.mHavePendingPopupgroup,
               "Should have proccessed it by now");

  return NS_OK;
}

nsresult
nsCSSFrameConstructor::ProcessChildren(nsFrameConstructorState& aState,
                                       nsIContent*              aContent,
                                       nsStyleContext*          aStyleContext,
                                       nsIFrame*                aFrame,
                                       const PRBool             aCanHaveGeneratedContent,
                                       nsFrameItems&            aFrameItems,
                                       const PRBool             aAllowBlockStyles,
                                       PendingBinding*          aPendingBinding)
{
  NS_PRECONDITION(aFrame, "Must have parent frame here");
  NS_PRECONDITION(aFrame->GetContentInsertionFrame() == aFrame,
                  "Parent frame in ProcessChildren should be its own "
                  "content insertion frame");

  // XXXbz ideally, this would do all the pushing of various
  // containing blocks as needed, so callers don't have to do it...

  PRBool haveFirstLetterStyle = PR_FALSE, haveFirstLineStyle = PR_FALSE;
  if (aAllowBlockStyles) {
    ShouldHaveSpecialBlockStyle(aContent, aStyleContext, &haveFirstLetterStyle,
                                &haveFirstLineStyle);
  }

  // The logic here needs to match the logic in GetFloatContainingBlock()
  nsFrameConstructorSaveState floatSaveState;
  if (aFrame->IsFrameOfType(nsIFrame::eMathML) ||
      aFrame->IsBoxFrame()) {
    aState.PushFloatContainingBlock(nsnull, floatSaveState);
  } else if (aFrame->IsFloatContainingBlock()) {
    aState.PushFloatContainingBlock(aFrame, floatSaveState);
  }

  nsFrameConstructorState::PendingBindingAutoPusher pusher(aState,
                                                           aPendingBinding);

  FrameConstructionItemList itemsToConstruct;
  nsresult rv = NS_OK;

  // If we have first-letter or first-line style then frames can get
  // moved around so don't set these flags.
  if (aAllowBlockStyles && !haveFirstLetterStyle && !haveFirstLineStyle) {
    itemsToConstruct.SetLineBoundaryAtStart(PR_TRUE);
    itemsToConstruct.SetLineBoundaryAtEnd(PR_TRUE);
  }

  // Create any anonymous frames we need here.  This must happen before the
  // non-anonymous children are processed to ensure that popups are never
  // constructed before the popupset.
  nsAutoTArray<nsIContent*, 4> anonymousItems;
  GetAnonymousContent(aContent, aFrame, anonymousItems);
  for (PRUint32 i = 0; i < anonymousItems.Length(); ++i) {
#ifdef DEBUG
    nsIAnonymousContentCreator* creator = do_QueryFrame(aFrame);
    NS_ASSERTION(!creator || !creator->CreateFrameFor(anonymousItems[i]),
                 "If you need to use CreateFrameFor, you need to call "
                 "CreateAnonymousFrames manually and not follow the standard "
                 "ProcessChildren() codepath for this frame");
#endif
    AddFrameConstructionItems(aState, anonymousItems[i], PR_TRUE, aFrame,
                              itemsToConstruct);
  }

  if (!aFrame->IsLeaf()) {
    // :before/:after content should have the same style context parent
    // as normal kids.
    // Note that we don't use this style context for looking up things like
    // special block styles because in some cases involving table pseudo-frames
    // it has nothing to do with the parent frame's desired behavior.
    nsStyleContext* styleContext;

    if (aCanHaveGeneratedContent) {
      styleContext =
        nsFrame::CorrectStyleParentFrame(aFrame, nsnull)->GetStyleContext();
      // Probe for generated content before
      CreateGeneratedContentItem(aState, aFrame, aContent, styleContext,
                                 nsCSSPseudoElements::ePseudo_before,
                                 itemsToConstruct);
    }

    ChildIterator iter, last;
    for (ChildIterator::Init(aContent, &iter, &last);
         iter != last;
         ++iter) {
      nsIContent* child = *iter;
      // Frame construction item construction should not post
      // restyles, so removing restyle flags here is safe.
      if (child->IsElement()) {
        child->UnsetFlags(ELEMENT_ALL_RESTYLE_FLAGS);
      }
      AddFrameConstructionItems(aState, child, iter.XBLInvolved(), aFrame,
                                itemsToConstruct);
    }
    itemsToConstruct.SetParentHasNoXBLChildren(!iter.XBLInvolved());

    if (aCanHaveGeneratedContent) {
      // Probe for generated content after
      CreateGeneratedContentItem(aState, aFrame, aContent, styleContext,
                                 nsCSSPseudoElements::ePseudo_after,
                                 itemsToConstruct);
    }
  } else {
    ClearLazyBits(aContent->GetFirstChild(), nsnull);
  }

  rv = ConstructFramesFromItemList(aState, itemsToConstruct, aFrame,
                                   aFrameItems);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(!aAllowBlockStyles || !aFrame->IsBoxFrame(),
               "can't be both block and box");

  if (haveFirstLetterStyle) {
    rv = WrapFramesInFirstLetterFrame(aContent, aFrame, aFrameItems);
  }
  if (haveFirstLineStyle) {
    rv = WrapFramesInFirstLineFrame(aState, aContent, aFrame, nsnull,
                                    aFrameItems);
  }

  // We might end up with first-line frames that change
  // AnyKidsNeedBlockParent() without changing itemsToConstruct, but that
  // should never happen for cases whan aFrame->IsBoxFrame().
  NS_ASSERTION(!haveFirstLineStyle || !aFrame->IsBoxFrame(),
               "Shouldn't have first-line style if we're a box");
  NS_ASSERTION(!aFrame->IsBoxFrame() ||
               itemsToConstruct.AnyItemsNeedBlockParent() ==
                 (AnyKidsNeedBlockParent(aFrameItems.FirstChild()) != nsnull),
               "Something went awry in our block parent calculations");

  if (aFrame->IsBoxFrame() && itemsToConstruct.AnyItemsNeedBlockParent()) {
    // XXXbz we could do this on the FrameConstructionItemList level,
    // no?  And if we cared we could look through the item list
    // instead of groveling through the framelist here..
    nsIContent *badKid = AnyKidsNeedBlockParent(aFrameItems.FirstChild());
    nsDependentAtomString parentTag(aContent->Tag()), kidTag(badKid->Tag());
    const PRUnichar* params[] = { parentTag.get(), kidTag.get() };
    nsStyleContext *frameStyleContext = aFrame->GetStyleContext();
    const nsStyleDisplay *display = frameStyleContext->GetStyleDisplay();
    const char *message =
      (display->mDisplay == NS_STYLE_DISPLAY_INLINE_BOX)
        ? "NeededToWrapXULInlineBox" : "NeededToWrapXUL";
    nsContentUtils::ReportToConsole(nsContentUtils::eXUL_PROPERTIES,
                                    message,
                                    params, NS_ARRAY_LENGTH(params),
                                    mDocument->GetDocumentURI(),
                                    EmptyString(), 0, 0, // not useful
                                    nsIScriptError::warningFlag,
                                    "FrameConstructor");

    nsRefPtr<nsStyleContext> blockSC = mPresShell->StyleSet()->
      ResolveAnonymousBoxStyle(nsCSSAnonBoxes::mozXULAnonymousBlock,
                               frameStyleContext);
    nsIFrame *blockFrame = NS_NewBlockFrame(mPresShell, blockSC);
    // We might, in theory, want to set NS_BLOCK_FLOAT_MGR and
    // NS_BLOCK_MARGIN_ROOT, but I think it's a bad idea given that
    // a real block placed here wouldn't get those set on it.

    InitAndRestoreFrame(aState, aContent, aFrame, nsnull,
                        blockFrame, PR_FALSE);

    NS_ASSERTION(!blockFrame->HasView(), "need to do view reparenting");
    ReparentFrames(aState.mFrameManager, blockFrame, aFrameItems);

    blockFrame->SetInitialChildList(nsnull, aFrameItems);
    NS_ASSERTION(aFrameItems.IsEmpty(), "How did that happen?");
    aFrameItems.Clear();
    aFrameItems.AddChild(blockFrame);

    aFrame->AddStateBits(NS_STATE_BOX_WRAPS_KIDS_IN_BLOCK);
  }

  return rv;
}

//----------------------------------------------------------------------

// Support for :first-line style

// Special routine to handle placing a list of frames into a block
// frame that has first-line style. The routine ensures that the first
// collection of inline frames end up in a first-line frame.
// NOTE: aState may have containing block information related to a
// different part of the frame tree than where the first line occurs.
// In particular aState may be set up for where ContentInserted or
// ContentAppended is inserting content, which may be some
// non-first-in-flow continuation of the block to which the first-line
// belongs. So this function needs to be careful about how it uses
// aState.
nsresult
nsCSSFrameConstructor::WrapFramesInFirstLineFrame(
  nsFrameConstructorState& aState,
  nsIContent*              aBlockContent,
  nsIFrame*                aBlockFrame,
  nsIFrame*                aLineFrame,
  nsFrameItems&            aFrameItems)
{
  nsresult rv = NS_OK;

  // Find the part of aFrameItems that we want to put in the first-line
  nsFrameList::FrameLinkEnumerator link(aFrameItems);
  while (!link.AtEnd() && IsInlineOutside(link.NextFrame())) {
    link.Next();
  }

  nsFrameList firstLineChildren = aFrameItems.ExtractHead(link);

  if (firstLineChildren.IsEmpty()) {
    // Nothing is supposed to go into the first-line; nothing to do
    return NS_OK;
  }

  if (!aLineFrame) {
    // Create line frame
    nsStyleContext* parentStyle =
      nsFrame::CorrectStyleParentFrame(aBlockFrame,
                                       nsCSSPseudoElements::firstLine)->
        GetStyleContext();
    nsRefPtr<nsStyleContext> firstLineStyle = GetFirstLineStyle(aBlockContent,
                                                                parentStyle);

    aLineFrame = NS_NewFirstLineFrame(mPresShell, firstLineStyle);

    if (aLineFrame) {
      // Initialize the line frame
      rv = InitAndRestoreFrame(aState, aBlockContent, aBlockFrame, nsnull,
                               aLineFrame);

      // The lineFrame will be the block's first child; the rest of the
      // frame list (after lastInlineFrame) will be the second and
      // subsequent children; insert lineFrame into aFrameItems.
      aFrameItems.InsertFrame(nsnull, nsnull, aLineFrame);

      NS_ASSERTION(aLineFrame->GetStyleContext() == firstLineStyle,
                   "Bogus style context on line frame");
    }
  }

  if (aLineFrame) {
    // Give the inline frames to the lineFrame <b>after</b> reparenting them
    ReparentFrames(aState.mFrameManager, aLineFrame, firstLineChildren);
    if (aLineFrame->GetChildList(nsnull).IsEmpty() &&
        (aLineFrame->GetStateBits() & NS_FRAME_FIRST_REFLOW)) {
      aLineFrame->SetInitialChildList(nsnull, firstLineChildren);
    } else {
      aState.mFrameManager->AppendFrames(aLineFrame, nsnull, firstLineChildren);
    }
  }
  else {
    rv = NS_ERROR_OUT_OF_MEMORY;
  }

  return rv;
}

// Special routine to handle appending a new frame to a block frame's
// child list. Takes care of placing the new frame into the right
// place when first-line style is present.
nsresult
nsCSSFrameConstructor::AppendFirstLineFrames(
  nsFrameConstructorState& aState,
  nsIContent*              aBlockContent,
  nsIFrame*                aBlockFrame,
  nsFrameItems&            aFrameItems)
{
  // It's possible that aBlockFrame needs to have a first-line frame
  // created because it doesn't currently have any children.
  const nsFrameList& blockKids = aBlockFrame->GetChildList(nsnull);
  if (blockKids.IsEmpty()) {
    return WrapFramesInFirstLineFrame(aState, aBlockContent,
                                      aBlockFrame, nsnull, aFrameItems);
  }

  // Examine the last block child - if it's a first-line frame then
  // appended frames need special treatment.
  nsIFrame* lastBlockKid = blockKids.LastChild();
  if (lastBlockKid->GetType() != nsGkAtoms::lineFrame) {
    // No first-line frame at the end of the list, therefore there is
    // an intervening block between any first-line frame the frames
    // we are appending. Therefore, we don't need any special
    // treatment of the appended frames.
    return NS_OK;
  }

  return WrapFramesInFirstLineFrame(aState, aBlockContent, aBlockFrame,
                                    lastBlockKid, aFrameItems);
}

// Special routine to handle inserting a new frame into a block
// frame's child list. Takes care of placing the new frame into the
// right place when first-line style is present.
nsresult
nsCSSFrameConstructor::InsertFirstLineFrames(
  nsFrameConstructorState& aState,
  nsIContent*              aContent,
  nsIFrame*                aBlockFrame,
  nsIFrame**               aParentFrame,
  nsIFrame*                aPrevSibling,
  nsFrameItems&            aFrameItems)
{
  nsresult rv = NS_OK;
  // XXXbz If you make this method actually do something, check to
  // make sure that the caller is passing what you expect.  In
  // particular, which content is aContent?  And audit the rest of
  // this code too; it makes bogus assumptions and may not build.
#if 0
  nsIFrame* parentFrame = *aParentFrame;
  nsIFrame* newFrame = aFrameItems.childList;
  PRBool isInline = IsInlineOutside(newFrame);

  if (!aPrevSibling) {
    // Insertion will become the first frame. Two cases: we either
    // already have a first-line frame or we don't.
    nsIFrame* firstBlockKid = aBlockFrame->GetFirstChild(nsnull);
    if (firstBlockKid->GetType() == nsGkAtoms::lineFrame) {
      // We already have a first-line frame
      nsIFrame* lineFrame = firstBlockKid;

      if (isInline) {
        // Easy case: the new inline frame will go into the lineFrame.
        ReparentFrame(aState.mFrameManager, lineFrame, newFrame);
        aState.mFrameManager->InsertFrames(lineFrame, nsnull, nsnull,
                                           newFrame);

        // Since the frame is going into the lineFrame, don't let it
        // go into the block too.
        aFrameItems.childList = nsnull;
        aFrameItems.lastChild = nsnull;
      }
      else {
        // Harder case: We are about to insert a block level element
        // before the first-line frame.
        // XXX need a method to steal away frames from the line-frame
      }
    }
    else {
      // We do not have a first-line frame
      if (isInline) {
        // We now need a first-line frame to contain the inline frame.
        nsIFrame* lineFrame = NS_NewFirstLineFrame(firstLineStyle);
        if (!lineFrame) {
          rv = NS_ERROR_OUT_OF_MEMORY;
        }

        if (NS_SUCCEEDED(rv)) {
          // Lookup first-line style context
          nsStyleContext* parentStyle =
            nsFrame::CorrectStyleParentFrame(aBlockFrame,
                                             nsCSSPseudoElements::firstLine)->
              GetStyleContext();
          nsRefPtr<nsStyleContext> firstLineStyle =
            GetFirstLineStyle(aContent, parentStyle);

          // Initialize the line frame
          rv = InitAndRestoreFrame(aState, aContent, aBlockFrame,
                                   nsnull, lineFrame);

          // Make sure the caller inserts the lineFrame into the
          // blocks list of children.
          aFrameItems.childList = lineFrame;
          aFrameItems.lastChild = lineFrame;

          // Give the inline frames to the lineFrame <b>after</b>
          // reparenting them
          NS_ASSERTION(lineFrame->GetStyleContext() == firstLineStyle,
                       "Bogus style context on line frame");
          ReparentFrame(aPresContext, lineFrame, newFrame);
          lineFrame->SetInitialChildList(nsnull, newFrame);
        }
      }
      else {
        // Easy case: the regular insertion logic can insert the new
        // frame because it's a block frame.
      }
    }
  }
  else {
    // Insertion will not be the first frame.
    nsIFrame* prevSiblingParent = aPrevSibling->GetParent();
    if (prevSiblingParent == aBlockFrame) {
      // Easy case: The prev-siblings parent is the block
      // frame. Therefore the prev-sibling is not currently in a
      // line-frame. Therefore the new frame which is going after it,
      // regardless of type, is not going into a line-frame.
    }
    else {
      // If the prevSiblingParent is not the block-frame then it must
      // be a line-frame (if it were a letter-frame, that logic would
      // already have adjusted the prev-sibling to be the
      // letter-frame).
      if (isInline) {
        // Easy case: the insertion can go where the caller thinks it
        // should go (which is into prevSiblingParent).
      }
      else {
        // Block elements don't end up in line-frames, therefore
        // change the insertion point to aBlockFrame. However, there
        // might be more inline elements following aPrevSibling that
        // need to be pulled out of the line-frame and become children
        // of the block.
        nsIFrame* nextSibling = aPrevSibling->GetNextSibling();
        nsIFrame* nextLineFrame = prevSiblingParent->GetNextInFlow();
        if (nextSibling || nextLineFrame) {
          // Oy. We have work to do. Create a list of the new frames
          // that are going into the block by stripping them away from
          // the line-frame(s).
          if (nextSibling) {
            nsLineFrame* lineFrame = (nsLineFrame*) prevSiblingParent;
            nsFrameList tail = lineFrame->StealFramesAfter(aPrevSibling);
            // XXX do something with 'tail'
          }

          nsLineFrame* nextLineFrame = (nsLineFrame*) lineFrame;
          for (;;) {
            nextLineFrame = nextLineFrame->GetNextInFlow();
            if (!nextLineFrame) {
              break;
            }
            nsIFrame* kids = nextLineFrame->GetFirstChild(nsnull);
          }
        }
        else {
          // We got lucky: aPrevSibling was the last inline frame in
          // the line-frame.
          ReparentFrame(aState.mFrameManager, aBlockFrame, newFrame);
          aState.mFrameManager->InsertFrames(aBlockFrame, nsnull,
                                             prevSiblingParent, newFrame);
          aFrameItems.childList = nsnull;
          aFrameItems.lastChild = nsnull;
        }
      }
    }
  }

#endif
  return rv;
}

//----------------------------------------------------------------------

// First-letter support

// Determine how many characters in the text fragment apply to the
// first letter
static PRInt32
FirstLetterCount(const nsTextFragment* aFragment)
{
  PRInt32 count = 0;
  PRInt32 firstLetterLength = 0;
  PRBool done = PR_FALSE;

  PRInt32 i, n = aFragment->GetLength();
  for (i = 0; i < n; i++) {
    PRUnichar ch = aFragment->CharAt(i);
    if (XP_IS_SPACE(ch)) {
      if (firstLetterLength) {
        done = PR_TRUE;
        break;
      }
      count++;
      continue;
    }
    // XXX I18n
    if ((ch == '\'') || (ch == '\"')) {
      if (firstLetterLength) {
        done = PR_TRUE;
        break;
      }
      // keep looping
      firstLetterLength = 1;
    }
    else {
      count++;
      done = PR_TRUE;
      break;
    }
  }

  return count;
}

static PRBool
NeedFirstLetterContinuation(nsIContent* aContent)
{
  NS_PRECONDITION(aContent, "null ptr");

  PRBool result = PR_FALSE;
  if (aContent) {
    const nsTextFragment* frag = aContent->GetText();
    if (frag) {
      PRInt32 flc = FirstLetterCount(frag);
      PRInt32 tl = frag->GetLength();
      if (flc < tl) {
        result = PR_TRUE;
      }
    }
  }
  return result;
}

static PRBool IsFirstLetterContent(nsIContent* aContent)
{
  return aContent->TextLength() &&
         !aContent->TextIsOnlyWhitespace();
}

/**
 * Create a letter frame, only make it a floating frame.
 */
void
nsCSSFrameConstructor::CreateFloatingLetterFrame(
  nsFrameConstructorState& aState,
  nsIFrame* aBlockFrame,
  nsIContent* aTextContent,
  nsIFrame* aTextFrame,
  nsIContent* aBlockContent,
  nsIFrame* aParentFrame,
  nsStyleContext* aStyleContext,
  nsFrameItems& aResult)
{
  // Create the first-letter-frame
  nsresult rv;
  nsIFrame* letterFrame;
  nsStyleSet *styleSet = mPresShell->StyleSet();

  letterFrame = NS_NewFirstLetterFrame(mPresShell, aStyleContext);
  // We don't want to use a text content for a non-text frame (because we want
  // its primary frame to be a text frame).  So use its parent for the
  // first-letter.
  nsIContent* letterContent = aTextContent->GetParent();
  nsIFrame* containingBlock = aState.GetGeometricParent(
    aStyleContext->GetStyleDisplay(), aParentFrame);
  InitAndRestoreFrame(aState, letterContent, containingBlock, nsnull,
                      letterFrame);

  // Init the text frame to refer to the letter frame. Make sure we
  // get a proper style context for it (the one passed in is for the
  // letter frame and will have the float property set on it; the text
  // frame shouldn't have that set).
  nsRefPtr<nsStyleContext> textSC;
  textSC = styleSet->ResolveStyleForNonElement(aStyleContext);
  aTextFrame->SetStyleContextWithoutNotification(textSC);
  InitAndRestoreFrame(aState, aTextContent, letterFrame, nsnull, aTextFrame);

  // And then give the text frame to the letter frame
  SetInitialSingleChild(letterFrame, aTextFrame);

  // See if we will need to continue the text frame (does it contain
  // more than just the first-letter text or not?) If it does, then we
  // create (in advance) a continuation frame for it.
  nsIFrame* nextTextFrame = nsnull;
  if (NeedFirstLetterContinuation(aTextContent)) {
    // Create continuation
    rv = CreateContinuingFrame(aState.mPresContext, aTextFrame, aParentFrame,
                               &nextTextFrame);
    if (NS_FAILED(rv)) {
      letterFrame->Destroy();
      return;
    }
    // Repair the continuations style context
    nsStyleContext* parentStyleContext = aStyleContext->GetParent();
    if (parentStyleContext) {
      nsRefPtr<nsStyleContext> newSC;
      newSC = styleSet->ResolveStyleForNonElement(parentStyleContext);
      if (newSC) {
        nextTextFrame->SetStyleContext(newSC);
      }
    }
  }

  NS_ASSERTION(aResult.IsEmpty(), "aResult should be an empty nsFrameItems!");
  // Put the new float before any of the floats in the block we're doing
  // first-letter for, that is, before any floats whose parent is
  // containingBlock.
  nsFrameList::FrameLinkEnumerator link(aState.mFloatedItems);
  while (!link.AtEnd() && link.NextFrame()->GetParent() != containingBlock) {
    link.Next();
  }

  rv = aState.AddChild(letterFrame, aResult, letterContent, aStyleContext,
                       aParentFrame, PR_FALSE, PR_TRUE, PR_FALSE, PR_TRUE,
                       link.PrevFrame());

  if (nextTextFrame) {
    if (NS_FAILED(rv)) {
      nextTextFrame->Destroy();
    } else {
      aResult.AddChild(nextTextFrame);
    }
  }
}

/**
 * Create a new letter frame for aTextFrame. The letter frame will be
 * a child of aParentFrame.
 */
nsresult
nsCSSFrameConstructor::CreateLetterFrame(nsIFrame* aBlockFrame,
                                         nsIFrame* aBlockContinuation,
                                         nsIContent* aTextContent,
                                         nsIFrame* aParentFrame,
                                         nsFrameItems& aResult)
{
  NS_PRECONDITION(aTextContent->IsNodeOfType(nsINode::eTEXT),
                  "aTextContent isn't text");
  NS_ASSERTION(nsLayoutUtils::GetAsBlock(aBlockFrame),
                 "Not a block frame?");

  // Get style context for the first-letter-frame
  nsStyleContext* parentStyleContext =
    nsFrame::CorrectStyleParentFrame(aParentFrame,
                                     nsCSSPseudoElements::firstLetter)->
      GetStyleContext();

  // Use content from containing block so that we can actually
  // find a matching style rule.
  nsIContent* blockContent = aBlockFrame->GetContent();

  // Create first-letter style rule
  nsRefPtr<nsStyleContext> sc = GetFirstLetterStyle(blockContent,
                                                    parentStyleContext);
  if (sc) {
    nsRefPtr<nsStyleContext> textSC;
    textSC = mPresShell->StyleSet()->ResolveStyleForNonElement(sc);
    
    // Create a new text frame (the original one will be discarded)
    // pass a temporary stylecontext, the correct one will be set
    // later.  Start off by unsetting the primary frame for
    // aTextContent, so it's no longer pointing to the to-be-destroyed
    // frame.
    // XXXbz it would be really nice to destroy the old frame _first_,
    // then create the new one, so we could avoid this hack.
    aTextContent->SetPrimaryFrame(nsnull);
    nsIFrame* textFrame = NS_NewTextFrame(mPresShell, textSC);

    NS_ASSERTION(aBlockContinuation == GetFloatContainingBlock(aParentFrame),
                 "Containing block is confused");
    nsFrameConstructorState state(mPresShell, mFixedContainingBlock,
                                  GetAbsoluteContainingBlock(aParentFrame),
                                  aBlockContinuation);

    // Create the right type of first-letter frame
    const nsStyleDisplay* display = sc->GetStyleDisplay();
    if (display->IsFloating()) {
      // Make a floating first-letter frame
      CreateFloatingLetterFrame(state, aBlockFrame, aTextContent, textFrame,
                                blockContent, aParentFrame, sc, aResult);
    }
    else {
      // Make an inflow first-letter frame
      nsIFrame* letterFrame = NS_NewFirstLetterFrame(mPresShell, sc);

      if (letterFrame) {
        // Initialize the first-letter-frame.  We don't want to use a text
        // content for a non-text frame (because we want its primary frame to
        // be a text frame).  So use its parent for the first-letter.
        nsIContent* letterContent = aTextContent->GetParent();
        letterFrame->Init(letterContent, aParentFrame, nsnull);

        InitAndRestoreFrame(state, aTextContent, letterFrame, nsnull,
                            textFrame);

        SetInitialSingleChild(letterFrame, textFrame);
        aResult.Clear();
        aResult.AddChild(letterFrame);
        NS_ASSERTION(!aBlockFrame->GetPrevContinuation(),
                     "should have the first continuation here");
        aBlockFrame->AddStateBits(NS_BLOCK_HAS_FIRST_LETTER_CHILD);
      }
    }
    aTextContent->SetPrimaryFrame(textFrame);
  }

  return NS_OK;
}

nsresult
nsCSSFrameConstructor::WrapFramesInFirstLetterFrame(
  nsIContent*              aBlockContent,
  nsIFrame*                aBlockFrame,
  nsFrameItems&            aBlockFrames)
{
  nsresult rv = NS_OK;

  aBlockFrame->AddStateBits(NS_BLOCK_HAS_FIRST_LETTER_STYLE);

  nsIFrame* parentFrame = nsnull;
  nsIFrame* textFrame = nsnull;
  nsIFrame* prevFrame = nsnull;
  nsFrameItems letterFrames;
  PRBool stopLooking = PR_FALSE;
  rv = WrapFramesInFirstLetterFrame(aBlockFrame, aBlockFrame, aBlockFrame,
                                    aBlockFrames.FirstChild(),
                                    &parentFrame, &textFrame, &prevFrame,
                                    letterFrames, &stopLooking);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (parentFrame) {
    if (parentFrame == aBlockFrame) {
      // Take textFrame out of the block's frame list and substitute the
      // letter frame(s) instead.
      aBlockFrames.DestroyFrame(textFrame);
      aBlockFrames.InsertFrames(nsnull, prevFrame, letterFrames);
    }
    else {
      // Take the old textFrame out of the inline parent's child list
      mPresShell->FrameManager()->RemoveFrame(nsnull, textFrame);

      // Insert in the letter frame(s)
      parentFrame->InsertFrames(nsnull, prevFrame, letterFrames);
    }
  }

  return rv;
}

nsresult
nsCSSFrameConstructor::WrapFramesInFirstLetterFrame(
  nsIFrame*                aBlockFrame,
  nsIFrame*                aBlockContinuation,
  nsIFrame*                aParentFrame,
  nsIFrame*                aParentFrameList,
  nsIFrame**               aModifiedParent,
  nsIFrame**               aTextFrame,
  nsIFrame**               aPrevFrame,
  nsFrameItems&            aLetterFrames,
  PRBool*                  aStopLooking)
{
  nsresult rv = NS_OK;

  nsIFrame* prevFrame = nsnull;
  nsIFrame* frame = aParentFrameList;

  while (frame) {
    nsIFrame* nextFrame = frame->GetNextSibling();

    nsIAtom* frameType = frame->GetType();
    if (nsGkAtoms::textFrame == frameType) {
      // Wrap up first-letter content in a letter frame
      nsIContent* textContent = frame->GetContent();
      if (IsFirstLetterContent(textContent)) {
        // Create letter frame to wrap up the text
        rv = CreateLetterFrame(aBlockFrame, aBlockContinuation, textContent,
                               aParentFrame, aLetterFrames);
        if (NS_FAILED(rv)) {
          return rv;
        }

        // Provide adjustment information for parent
        *aModifiedParent = aParentFrame;
        *aTextFrame = frame;
        *aPrevFrame = prevFrame;
        *aStopLooking = PR_TRUE;
        return NS_OK;
      }
    }
    else if (IsInlineFrame(frame) && frameType != nsGkAtoms::brFrame) {
      nsIFrame* kids = frame->GetFirstChild(nsnull);
      WrapFramesInFirstLetterFrame(aBlockFrame, aBlockContinuation, frame,
                                   kids, aModifiedParent, aTextFrame,
                                   aPrevFrame, aLetterFrames, aStopLooking);
      if (*aStopLooking) {
        return NS_OK;
      }
    }
    else {
      // This will stop us looking to create more letter frames. For
      // example, maybe the frame-type is "letterFrame" or
      // "placeholderFrame". This keeps us from creating extra letter
      // frames, and also prevents us from creating letter frames when
      // the first real content child of a block is not text (e.g. an
      // image, hr, etc.)
      *aStopLooking = PR_TRUE;
      break;
    }

    prevFrame = frame;
    frame = nextFrame;
  }

  return rv;
}

nsresult
nsCSSFrameConstructor::RemoveFloatingFirstLetterFrames(
  nsPresContext* aPresContext,
  nsIPresShell* aPresShell,
  nsFrameManager* aFrameManager,
  nsIFrame* aBlockFrame,
  PRBool* aStopLooking)
{
  // First look for the float frame that is a letter frame
  nsIFrame* floatFrame = aBlockFrame->GetFirstChild(nsGkAtoms::floatList);
  while (floatFrame) {
    // See if we found a floating letter frame
    if (nsGkAtoms::letterFrame == floatFrame->GetType()) {
      break;
    }
    floatFrame = floatFrame->GetNextSibling();
  }
  if (!floatFrame) {
    // No such frame
    return NS_OK;
  }

  // Take the text frame away from the letter frame (so it isn't
  // destroyed when we destroy the letter frame).
  nsIFrame* textFrame = floatFrame->GetFirstChild(nsnull);
  if (!textFrame) {
    return NS_OK;
  }

  // Discover the placeholder frame for the letter frame
  nsIFrame* parentFrame;
  nsPlaceholderFrame* placeholderFrame = 
    aFrameManager->GetPlaceholderFrameFor(floatFrame);

  if (!placeholderFrame) {
    // Somethings really wrong
    return NS_OK;
  }
  parentFrame = placeholderFrame->GetParent();
  if (!parentFrame) {
    // Somethings really wrong
    return NS_OK;
  }

  // Create a new text frame with the right style context that maps
  // all of the content that was previously part of the letter frame
  // (and probably continued elsewhere).
  nsStyleContext* parentSC = parentFrame->GetStyleContext();
  if (!parentSC) {
    return NS_OK;
  }
  nsIContent* textContent = textFrame->GetContent();
  if (!textContent) {
    return NS_OK;
  }
  nsRefPtr<nsStyleContext> newSC;
  newSC = aPresShell->StyleSet()->ResolveStyleForNonElement(parentSC);
  if (!newSC) {
    return NS_OK;
  }
  nsIFrame* newTextFrame = NS_NewTextFrame(aPresShell, newSC);
  if (NS_UNLIKELY(!newTextFrame)) {
    return NS_ERROR_OUT_OF_MEMORY;;
  }
  newTextFrame->Init(textContent, parentFrame, nsnull);

  // Destroy the old text frame's continuations (the old text frame
  // will be destroyed when its letter frame is destroyed).
  nsIFrame* frameToDelete = textFrame->GetLastContinuation();
  while (frameToDelete != textFrame) {
    nsIFrame* nextFrameToDelete = frameToDelete->GetPrevContinuation();
    aFrameManager->RemoveFrame(nsnull, frameToDelete);
    frameToDelete = nextFrameToDelete;
  }

  nsIFrame* prevSibling = placeholderFrame->GetPrevSibling();

  // Now that everything is set...
#ifdef NOISY_FIRST_LETTER
  printf("RemoveFloatingFirstLetterFrames: textContent=%p oldTextFrame=%p newTextFrame=%p\n",
         textContent.get(), textFrame, newTextFrame);
#endif

  // Remove placeholder frame and the float
  aFrameManager->RemoveFrame(nsnull, placeholderFrame);

  // Now that the old frames are gone, we can start pointing to our
  // new primary frame.
  textContent->SetPrimaryFrame(newTextFrame);

  // Insert text frame in its place
  nsFrameList textList(newTextFrame, newTextFrame);
  aFrameManager->InsertFrames(parentFrame, nsnull, prevSibling, textList);

  return NS_OK;
}

nsresult
nsCSSFrameConstructor::RemoveFirstLetterFrames(nsPresContext* aPresContext,
                                               nsIPresShell* aPresShell,
                                               nsFrameManager* aFrameManager,
                                               nsIFrame* aFrame,
                                               nsIFrame* aBlockFrame,
                                               PRBool* aStopLooking)
{
  nsIFrame* prevSibling = nsnull;
  nsIFrame* kid = aFrame->GetFirstChild(nsnull);

  while (kid) {
    if (nsGkAtoms::letterFrame == kid->GetType()) {
      // Bingo. Found it. First steal away the text frame.
      nsIFrame* textFrame = kid->GetFirstChild(nsnull);
      if (!textFrame) {
        break;
      }

      // Create a new textframe
      nsStyleContext* parentSC = aFrame->GetStyleContext();
      if (!parentSC) {
        break;
      }
      nsIContent* textContent = textFrame->GetContent();
      if (!textContent) {
        break;
      }
      nsRefPtr<nsStyleContext> newSC;
      newSC = aPresShell->StyleSet()->ResolveStyleForNonElement(parentSC);
      if (!newSC) {
        break;
      }
      textFrame = NS_NewTextFrame(aPresShell, newSC);
      textFrame->Init(textContent, aFrame, nsnull);

      // Next rip out the kid and replace it with the text frame
      aFrameManager->RemoveFrame(nsnull, kid);

      // Now that the old frames are gone, we can start pointing to our
      // new primary frame.
      textContent->SetPrimaryFrame(textFrame);

      // Insert text frame in its place
      nsFrameList textList(textFrame, textFrame);
      aFrameManager->InsertFrames(aFrame, nsnull, prevSibling, textList);

      *aStopLooking = PR_TRUE;
      NS_ASSERTION(!aBlockFrame->GetPrevContinuation(),
                   "should have the first continuation here");
      aBlockFrame->RemoveStateBits(NS_BLOCK_HAS_FIRST_LETTER_CHILD);
      break;
    }
    else if (IsInlineFrame(kid)) {
      // Look inside child inline frame for the letter frame
      RemoveFirstLetterFrames(aPresContext, aPresShell, aFrameManager,
                              kid, aBlockFrame, aStopLooking);
      if (*aStopLooking) {
        break;
      }
    }
    prevSibling = kid;
    kid = kid->GetNextSibling();
  }

  return NS_OK;
}

nsresult
nsCSSFrameConstructor::RemoveLetterFrames(nsPresContext* aPresContext,
                                          nsIPresShell* aPresShell,
                                          nsFrameManager* aFrameManager,
                                          nsIFrame* aBlockFrame)
{
  aBlockFrame = aBlockFrame->GetFirstContinuation();
  nsIFrame* continuation = aBlockFrame;

  PRBool stopLooking = PR_FALSE;
  nsresult rv;
  do {
    rv = RemoveFloatingFirstLetterFrames(aPresContext, aPresShell,
                                         aFrameManager,
                                         continuation, &stopLooking);
    if (NS_SUCCEEDED(rv) && !stopLooking) {
      rv = RemoveFirstLetterFrames(aPresContext, aPresShell, aFrameManager,
                                   continuation, aBlockFrame, &stopLooking);
    }
    if (stopLooking) {
      break;
    }
    continuation = continuation->GetNextContinuation();
  }  while (continuation);
  return rv;
}

// Fixup the letter frame situation for the given block
nsresult
nsCSSFrameConstructor::RecoverLetterFrames(nsIFrame* aBlockFrame)
{
  aBlockFrame = aBlockFrame->GetFirstContinuation();
  nsIFrame* continuation = aBlockFrame;

  nsIFrame* parentFrame = nsnull;
  nsIFrame* textFrame = nsnull;
  nsIFrame* prevFrame = nsnull;
  nsFrameItems letterFrames;
  PRBool stopLooking = PR_FALSE;
  nsresult rv;
  do {
    // XXX shouldn't this bit be set already (bug 408493), assert instead?
    continuation->AddStateBits(NS_BLOCK_HAS_FIRST_LETTER_STYLE);
    rv = WrapFramesInFirstLetterFrame(aBlockFrame, continuation, continuation,
                                      continuation->GetFirstChild(nsnull),
                                      &parentFrame, &textFrame, &prevFrame,
                                      letterFrames, &stopLooking);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (stopLooking) {
      break;
    }
    continuation = continuation->GetNextContinuation();
  } while (continuation);

  if (parentFrame) {
    // Take the old textFrame out of the parents child list
    mPresShell->FrameManager()->RemoveFrame(nsnull, textFrame);

    // Insert in the letter frame(s)
    parentFrame->InsertFrames(nsnull, prevFrame, letterFrames);
  }
  return rv;
}

//----------------------------------------------------------------------

// listbox Widget Routines

nsresult
nsCSSFrameConstructor::CreateListBoxContent(nsPresContext* aPresContext,
                                            nsIFrame*       aParentFrame,
                                            nsIFrame*       aPrevFrame,
                                            nsIContent*     aChild,
                                            nsIFrame**      aNewFrame,
                                            PRBool          aIsAppend,
                                            PRBool          aIsScrollbar,
                                            nsILayoutHistoryState* aFrameState)
{
#ifdef MOZ_XUL
  nsresult rv = NS_OK;

  // Construct a new frame
  if (nsnull != aParentFrame) {
    nsFrameItems            frameItems;
    nsFrameConstructorState state(mPresShell, mFixedContainingBlock,
                                  GetAbsoluteContainingBlock(aParentFrame),
                                  GetFloatContainingBlock(aParentFrame), 
                                  mTempFrameTreeState);

    nsRefPtr<nsStyleContext> styleContext;
    styleContext = ResolveStyleContext(aParentFrame, aChild);

    // Pre-check for display "none" - only if we find that, do we create
    // any frame at all
    const nsStyleDisplay* display = styleContext->GetStyleDisplay();

    if (NS_STYLE_DISPLAY_NONE == display->mDisplay) {
      *aNewFrame = nsnull;
      return NS_OK;
    }

    BeginUpdate();

    FrameConstructionItemList items;
    AddFrameConstructionItemsInternal(state, aChild, aParentFrame,
                                      aChild->Tag(), aChild->GetNameSpaceID(),
                                      PR_TRUE, styleContext,
                                      ITEM_ALLOW_XBL_BASE, items);
    ConstructFramesFromItemList(state, items, aParentFrame, frameItems);

    nsIFrame* newFrame = frameItems.FirstChild();
    *aNewFrame = newFrame;

    if (NS_SUCCEEDED(rv) && (nsnull != newFrame)) {
      // Notify the parent frame
      if (aIsAppend)
        rv = ((nsListBoxBodyFrame*)aParentFrame)->ListBoxAppendFrames(frameItems);
      else
        rv = ((nsListBoxBodyFrame*)aParentFrame)->ListBoxInsertFrames(aPrevFrame, frameItems);
    }

    EndUpdate();
  }

  return rv;
#else
  return NS_ERROR_FAILURE;
#endif
}

//----------------------------------------

nsresult
nsCSSFrameConstructor::ConstructBlock(nsFrameConstructorState& aState,
                                      const nsStyleDisplay*    aDisplay,
                                      nsIContent*              aContent,
                                      nsIFrame*                aParentFrame,
                                      nsIFrame*                aContentParentFrame,
                                      nsStyleContext*          aStyleContext,
                                      nsIFrame**               aNewFrame,
                                      nsFrameItems&            aFrameItems,
                                      PRBool                   aAbsPosContainer,
                                      PendingBinding*          aPendingBinding)
{
  // Create column wrapper if necessary
  nsIFrame* blockFrame = *aNewFrame;
  nsIFrame* parent = aParentFrame;
  nsRefPtr<nsStyleContext> blockStyle = aStyleContext;
  const nsStyleColumn* columns = aStyleContext->GetStyleColumn();

  if (columns->mColumnCount != NS_STYLE_COLUMN_COUNT_AUTO
      || columns->mColumnWidth.GetUnit() != eStyleUnit_Auto) {
    nsIFrame* columnSetFrame = nsnull;
    columnSetFrame = NS_NewColumnSetFrame(mPresShell, aStyleContext, 0);
    if (!columnSetFrame) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    InitAndRestoreFrame(aState, aContent, aParentFrame, nsnull, columnSetFrame);
    // See if we need to create a view
    nsHTMLContainerFrame::CreateViewForFrame(columnSetFrame, PR_FALSE);
    blockStyle = mPresShell->StyleSet()->
      ResolveAnonymousBoxStyle(nsCSSAnonBoxes::columnContent, aStyleContext);
    parent = columnSetFrame;
    *aNewFrame = columnSetFrame;

    SetInitialSingleChild(columnSetFrame, blockFrame);
  }

  blockFrame->SetStyleContextWithoutNotification(blockStyle);
  InitAndRestoreFrame(aState, aContent, parent, nsnull, blockFrame);

  nsresult rv = aState.AddChild(*aNewFrame, aFrameItems, aContent,
                                aStyleContext,
                                aContentParentFrame ? aContentParentFrame :
                                                      aParentFrame);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // See if we need to create a view, e.g. the frame is absolutely positioned
  nsHTMLContainerFrame::CreateViewForFrame(blockFrame, PR_FALSE);

  if (!mRootElementFrame) {
    // The frame we're constructing will be the root element frame.
    // Set mRootElementFrame before processing children.
    mRootElementFrame = *aNewFrame;
  }

  // We should make the outer frame be the absolute containing block,
  // if one is required. We have to do this because absolute
  // positioning must be computed with respect to the CSS dimensions
  // of the element, which are the dimensions of the outer block. But
  // we can't really do that because only blocks can have absolute
  // children. So use the block and try to compensate with hacks
  // in nsBlockFrame::CalculateContainingBlockSizeForAbsolutes.
  nsFrameConstructorSaveState absoluteSaveState;
  if (aAbsPosContainer) {
    //    NS_ASSERTION(aRelPos, "should have made area frame for this");
    aState.PushAbsoluteContainingBlock(blockFrame, absoluteSaveState);
  }

  // Process the child content
  nsFrameItems childItems;
  rv = ProcessChildren(aState, aContent, aStyleContext, blockFrame, PR_TRUE,
                       childItems, PR_TRUE, aPendingBinding);

  // Set the frame's initial child list
  blockFrame->SetInitialChildList(nsnull, childItems);

  return rv;
}

nsresult
nsCSSFrameConstructor::ConstructInline(nsFrameConstructorState& aState,
                                       FrameConstructionItem&   aItem,
                                       nsIFrame*                aParentFrame,
                                       const nsStyleDisplay*    aDisplay,
                                       nsFrameItems&            aFrameItems,
                                       nsIFrame**               aNewFrame)
{
  // If an inline frame has non-inline kids, then we chop up the child list
  // into runs of blocks and runs of inlines, create anonymous block frames to
  // contain the runs of blocks, inline frames with our style context for the
  // runs of inlines, and put all these frames, in order, into aFrameItems.  We
  // put the first one into *aNewFrame.  The whole setup is called an {ib}
  // split; in what follows "frames in the split" refers to the anonymous blocks
  // and inlines that contain our children.
  //
  // {ib} splits maintain the following invariants:
  // 1) All frames in the split have the NS_FRAME_IS_SPECIAL bit set.
  // 2) Each frame in the split has the nsIFrame::IBSplitSpecialSibling
  //    property pointing to the next frame in the split, except for the last
  //    one, which does not have it set.
  // 3) Each frame in the split has the nsIFrame::IBSplitSpecialPrevSibling
  //    property pointing to the previous frame in the split, except for the
  //    first one, which does not have it set.
  // 4) The first and last frame in the split are always inlines.
  //
  // An invariant that is NOT maintained is that the wrappers are actually
  // linked via GetNextSibling linkage.  A simple example is an inline
  // containing an inline that contains a block.  The three parts of the inner
  // inline end up with three different parents.
  //
  // For example, this HTML:
  // <span>
  //   <div>a</div>
  //   <span>
  //     b
  //     <div>c</div>
  //   </span>
  //   d
  //   <div>e</div>
  //   f
  //  </span>
  // Gives the following frame tree:
  //
  // Inline (outer span)
  // Block (anonymous, outer span)
  //   Block (div)
  //     Text("a")
  // Inline (outer span)
  //   Inline (inner span)
  //     Text("b")
  // Block (anonymous, outer span)
  //   Block (anonymous, inner span)
  //     Block (div)
  //       Text("c")
  // Inline (outer span)
  //   Inline (inner span)
  //   Text("d")
  // Block (anonymous, outer span)
  //   Block (div)
  //     Text("e")
  // Inline (outer span)
  //   Text("f")

  nsIContent* const content = aItem.mContent;
  nsStyleContext* const styleContext = aItem.mStyleContext;

  nsIFrame *newFrame;

  PRBool positioned =
    NS_STYLE_DISPLAY_INLINE == aDisplay->mDisplay &&
    (NS_STYLE_POSITION_RELATIVE == aDisplay->mPosition ||
     aDisplay->HasTransform());
  if (positioned) {
    newFrame = NS_NewPositionedInlineFrame(mPresShell, styleContext);
  } else {
    newFrame = NS_NewInlineFrame(mPresShell, styleContext);
  }

  // Initialize the frame
  InitAndRestoreFrame(aState, content, aParentFrame, nsnull, newFrame);

  nsFrameConstructorSaveState absoluteSaveState;  // definition cannot be inside next block
                                                  // because the object's destructor is significant
                                                  // this is part of the fix for bug 42372

  // Any inline frame might need a view (because of opacity, or fixed background)
  nsHTMLContainerFrame::CreateViewForFrame(newFrame, PR_FALSE);

  if (positioned) {                            
    // Relatively positioned frames becomes a container for child
    // frames that are positioned
    aState.PushAbsoluteContainingBlock(newFrame, absoluteSaveState);
  }

  // Process the child content
  nsFrameItems childItems;
  nsresult rv = ConstructFramesFromItemList(aState, aItem.mChildItems, newFrame,
                                            childItems);
  if (NS_FAILED(rv)) {
    // Clean up?
    return rv;
  }

  nsFrameList::FrameLinkEnumerator firstBlockEnumerator(childItems);
  if (!aItem.mIsAllInline) {
    FindFirstBlock(firstBlockEnumerator);
  }

  if (aItem.mIsAllInline || firstBlockEnumerator.AtEnd()) { 
    // This part is easy.  We either already know we have no non-inline kids,
    // or haven't found any when constructing actual frames (the latter can
    // happen only if out-of-flows that we thought had no containing block
    // acquired one when ancestor inline frames and {ib} splits got
    // constructed).  Just put all the kids into the single inline frame and
    // bail.
    newFrame->SetInitialChildList(nsnull, childItems);
    if (NS_SUCCEEDED(rv)) {
      aState.AddChild(newFrame, aFrameItems, content, styleContext, aParentFrame);
      *aNewFrame = newFrame;
    }
    return rv;
  }

  // This inline frame contains several types of children. Therefore this frame
  // has to be chopped into several pieces, as described above.

  // Grab the first inline's kids
  nsFrameList firstInlineKids = childItems.ExtractHead(firstBlockEnumerator);
  newFrame->SetInitialChildList(nsnull, firstInlineKids);

  aFrameItems.AddChild(newFrame);

  CreateIBSiblings(aState, newFrame, positioned, childItems, aFrameItems);

  *aNewFrame = newFrame;
  return NS_OK;
}

void
nsCSSFrameConstructor::CreateIBSiblings(nsFrameConstructorState& aState,
                                        nsIFrame* aInitialInline,
                                        PRBool aIsPositioned,
                                        nsFrameItems& aChildItems,
                                        nsFrameItems& aSiblings)
{
  nsIContent* content = aInitialInline->GetContent();
  nsStyleContext* styleContext = aInitialInline->GetStyleContext();
  nsIFrame* parentFrame = aInitialInline->GetParent();

  // Resolve the right style context for our anonymous blocks.
  nsRefPtr<nsStyleContext> blockSC =
    mPresShell->StyleSet()->
      ResolveAnonymousBoxStyle(aIsPositioned ?
                                 nsCSSAnonBoxes::mozAnonymousPositionedBlock :
                                 nsCSSAnonBoxes::mozAnonymousBlock,
                               styleContext);

  nsIFrame* lastNewInline = aInitialInline->GetFirstContinuation();
  do {
    // On entry to this loop aChildItems is not empty and the first frame in it
    // is block-level.
    NS_PRECONDITION(aChildItems.NotEmpty(), "Should have child items");
    NS_PRECONDITION(!IsInlineOutside(aChildItems.FirstChild()),
                    "Must have list starting with block");

    // The initial run of blocks belongs to an anonymous block that we create
    // right now. The anonymous block will be the parent of these block
    // children of the inline.
    nsIFrame* blockFrame;
    blockFrame = NS_NewBlockFrame(mPresShell, blockSC);

    InitAndRestoreFrame(aState, content, parentFrame, nsnull, blockFrame,
                        PR_FALSE);

    // Any frame could have a view
    nsHTMLContainerFrame::CreateViewForFrame(blockFrame, PR_FALSE);

    // Find the first non-block child which defines the end of our block kids
    // and the start of our next inline's kids
    nsFrameList::FrameLinkEnumerator firstNonBlock =
      FindFirstNonBlock(aChildItems);
    nsFrameList blockKids = aChildItems.ExtractHead(firstNonBlock);

    MoveChildrenTo(aState.mPresContext, aInitialInline, blockFrame, blockKids);

    SetFrameIsSpecial(lastNewInline, blockFrame);
    aSiblings.AddChild(blockFrame);

    // Now grab the initial inlines in aChildItems and put them into an inline
    // frame
    nsIFrame* inlineFrame;
    if (aIsPositioned) {
      inlineFrame = NS_NewPositionedInlineFrame(mPresShell, styleContext);
    }
    else {
      inlineFrame = NS_NewInlineFrame(mPresShell, styleContext);
    }

    InitAndRestoreFrame(aState, content, parentFrame, nsnull, inlineFrame,
                        PR_FALSE);

    // Any frame might need a view
    nsHTMLContainerFrame::CreateViewForFrame(inlineFrame, PR_FALSE);

    if (aChildItems.NotEmpty()) {
      nsFrameList::FrameLinkEnumerator firstBlock(aChildItems);
      FindFirstBlock(firstBlock);
      nsFrameList inlineKids = aChildItems.ExtractHead(firstBlock);

      MoveChildrenTo(aState.mPresContext, aInitialInline, inlineFrame,
                     inlineKids);
    }

    SetFrameIsSpecial(blockFrame, inlineFrame);
    aSiblings.AddChild(inlineFrame);
    lastNewInline = inlineFrame;
  } while (aChildItems.NotEmpty());

  SetFrameIsSpecial(lastNewInline, nsnull);
}

void
nsCSSFrameConstructor::BuildInlineChildItems(nsFrameConstructorState& aState,
                                             FrameConstructionItem& aParentItem)
{
  // XXXbz should we preallocate aParentItem.mChildItems to some sane
  // length?  Maybe even to parentContent->GetChildCount()?
  nsFrameConstructorState::PendingBindingAutoPusher
    pusher(aState, aParentItem.mPendingBinding);

  // Probe for generated content before
  nsStyleContext* const parentStyleContext = aParentItem.mStyleContext;
  nsIContent* const parentContent = aParentItem.mContent;
  CreateGeneratedContentItem(aState, nsnull, parentContent, parentStyleContext,
                             nsCSSPseudoElements::ePseudo_before,
                             aParentItem.mChildItems);

  ChildIterator iter, last;
  for (ChildIterator::Init(parentContent, &iter, &last);
       iter != last;
       ++iter) {
    // Manually check for comments/PIs, since we do't have a frame to pass to
    // AddFrameConstructionItems.  We know our parent is a non-replaced inline,
    // so there is no need to do the NeedFrameFor check.
    nsIContent* content = *iter;
    content->UnsetFlags(NODE_DESCENDANTS_NEED_FRAMES | NODE_NEEDS_FRAME);
    if (content->IsNodeOfType(nsINode::eCOMMENT) ||
        content->IsNodeOfType(nsINode::ePROCESSING_INSTRUCTION)) {
      continue;
    }
    if (content->IsElement()) {
      // See comment explaining why we need to remove the "is possible
      // restyle root" flags in AddFrameConstructionItems.  But note
      // that we can remove all restyle flags, just like in
      // ProcessChildren and for the same reason.
      content->UnsetFlags(ELEMENT_ALL_RESTYLE_FLAGS);
    }

    nsRefPtr<nsStyleContext> childContext =
      ResolveStyleContext(parentStyleContext, content);

    AddFrameConstructionItemsInternal(aState, content, nsnull, content->Tag(),
                                      content->GetNameSpaceID(),
                                      iter.XBLInvolved(), childContext,
                                      ITEM_ALLOW_XBL_BASE | ITEM_ALLOW_PAGE_BREAK,
                                      aParentItem.mChildItems);
  }

  // Probe for generated content after
  CreateGeneratedContentItem(aState, nsnull, parentContent, parentStyleContext,
                             nsCSSPseudoElements::ePseudo_after,
                             aParentItem.mChildItems);

  aParentItem.mIsAllInline = aParentItem.mChildItems.AreAllItemsInline();
}

// return whether it's ok to append (in the AppendFrames sense) to
// aParentFrame if our nextSibling is aNextSibling.  aParentFrame must
// be an {ib} special inline.
static PRBool
IsSafeToAppendToSpecialInline(nsIFrame* aParentFrame, nsIFrame* aNextSibling)
{
  NS_PRECONDITION(IsInlineFrame(aParentFrame),
                  "Must have an inline parent here");
  do {
    NS_ASSERTION(IsFrameSpecial(aParentFrame), "How is this not special?");
    if (aNextSibling || aParentFrame->GetNextContinuation() ||
        GetSpecialSibling(aParentFrame)) {
      return PR_FALSE;
    }

    aNextSibling = aParentFrame->GetNextSibling();
    aParentFrame = aParentFrame->GetParent();
  } while (IsInlineFrame(aParentFrame));

  return PR_TRUE;
}

PRBool
nsCSSFrameConstructor::WipeContainingBlock(nsFrameConstructorState& aState,
                                           nsIFrame* aContainingBlock,
                                           nsIFrame* aFrame,
                                           FrameConstructionItemList& aItems,
                                           PRBool aIsAppend,
                                           nsIFrame* aPrevSibling)
{
  if (aItems.IsEmpty()) {
    return PR_FALSE;
  }
  
  // Before we go and append the frames, we must check for several
  // special situations.

  // Situation #1 is a XUL frame that contains frames that are required
  // to be wrapped in blocks.
  if (aFrame->IsBoxFrame() &&
      !(aFrame->GetStateBits() & NS_STATE_BOX_WRAPS_KIDS_IN_BLOCK) &&
      aItems.AnyItemsNeedBlockParent()) {
    RecreateFramesForContent(aFrame->GetContent(), PR_TRUE);
    return PR_TRUE;
  }

  nsIFrame* nextSibling = ::GetInsertNextSibling(aFrame, aPrevSibling);

  // Situation #2 is a case when table pseudo-frames don't work out right
  ParentType parentType = GetParentType(aFrame);
  // If all the kids want a parent of the type that aFrame is, then we're all
  // set to go.  Indeed, there won't be any table pseudo-frames created between
  // aFrame and the kids, so those won't need to be merged with any table
  // pseudo-frames that might already be kids of aFrame.  If aFrame itself is a
  // table pseudo-frame, then all the kids in this list would have wanted a
  // frame of that type wrapping them anyway, so putting them inside it is ok.
  if (!aItems.AllWantParentType(parentType)) {
    // Don't give up yet.  If parentType is not eTypeBlock and the parent is
    // not a generated content frame, then try filtering whitespace out of the
    // list.
    if (parentType != eTypeBlock && !aFrame->IsGeneratedContentFrame()) {
      // For leading whitespace followed by a kid that wants our parent type,
      // there are four cases:
      // 1) We have a previous sibling which is not a table pseudo.  That means
      //    that previous sibling wanted a (non-block) parent of the type we're
      //    looking at.  Then the whitespace comes between two table-internal
      //    elements, so should be collapsed out.
      // 2) We have a previous sibling which is a table pseudo.  It might have
      //    kids who want this whitespace, so we need to reframe.
      // 3) We have no previous sibling and our parent frame is not a table
      //    pseudo.  That means that we'll be at the beginning of our actual
      //    non-block-type parent, and the whitespace is OK to collapse out.
      //    If something is ever inserted before us, it'll find our own parent
      //    as its parent and if it's something that would care about the
      //    whitespace it'll want a block parent, so it'll trigger a reframe at
      //    that point.
      // 4) We have no previous sibling and our parent frame is a table pseudo.
      //    Need to reframe.
      // All that is predicated on finding the correct previous sibling.  We
      // might have to walk backwards along continuations from aFrame to do so.
      //
      // It's always OK to drop whitespace between any two items that want a
      // parent of type parentType.
      //
      // For trailing whitespace preceded by a kid that wants our parent type,
      // there are four cases:
      // 1) We have a next sibling which is not a table pseudo.  That means
      //    that next sibling wanted a (non-block) parent of the type we're
      //    looking at.  Then the whitespace comes between two table-internal
      //    elements, so should be collapsed out.
      // 2) We have a next sibling which is a table pseudo.  It might have
      //    kids who want this whitespace, so we need to reframe.
      // 3) We have no next sibling and our parent frame is not a table
      //    pseudo.  That means that we'll be at the end of our actual
      //    non-block-type parent, and the whitespace is OK to collapse out.
      //    If something is ever inserted after us, it'll find our own parent
      //    as its parent and if it's something that would care about the
      //    whitespace it'll want a block parent, so it'll trigger a reframe at
      //    that point.
      // 4) We have no next sibling and our parent frame is a table pseudo.
      //    Need to reframe.
      // All that is predicated on finding the correct next sibling.  We might
      // have to walk forward along continuations from aFrame to do so.  That
      // said, in the case when nextSibling is null at this point and aIsAppend
      // is true, we know we're in case 3.  Furthermore, in that case we don't
      // even have to worry about the table pseudo situation; we know our
      // parent is not a table pseudo there.
      FCItemIterator iter(aItems);
      FCItemIterator start(iter);
      do {
        if (iter.SkipItemsWantingParentType(parentType)) {
          break;
        }

        // iter points to an item that wants a different parent.  If it's not
        // whitespace, we're done; no more point scanning the list.
        if (!iter.item().IsWhitespace(aState)) {
          break;
        }

        if (iter == start) {
          // Leading whitespace.  How to handle this depends on our
          // previous sibling and aFrame.  See the long comment above.
          nsIFrame* prevSibling = aPrevSibling;
          if (!prevSibling) {
            // Try to find one after all
            nsIFrame* parentPrevCont = aFrame->GetPrevContinuation();
            while (parentPrevCont) {
              prevSibling = parentPrevCont->GetLastChild(nsnull);
              if (prevSibling) {
                break;
              }
              parentPrevCont = parentPrevCont->GetPrevContinuation();
            }
          };
          if (prevSibling) {
            if (IsTablePseudo(prevSibling)) {
              // need to reframe
              break;
            }
          } else if (IsTablePseudo(aFrame)) {
            // need to reframe
            break;
          }
        }

        FCItemIterator spaceEndIter(iter);
        // Advance spaceEndIter past any whitespace
        PRBool trailingSpaces = spaceEndIter.SkipWhitespace(aState);

        PRBool okToDrop;
        if (trailingSpaces) {
          // Trailing whitespace.  How to handle this depeds on aIsAppend, our
          // next sibling and aFrame.  See the long comment above.
          okToDrop = aIsAppend && !nextSibling;
          if (!okToDrop) {
            if (!nextSibling) {
              // Try to find one after all
              nsIFrame* parentNextCont = aFrame->GetNextContinuation();
              while (parentNextCont) {
                nextSibling = parentNextCont->GetFirstChild(nsnull);
                if (nextSibling) {
                  break;
                }
                parentNextCont = parentNextCont->GetNextContinuation();
              }
            }

            okToDrop = (nextSibling && !IsTablePseudo(nextSibling)) ||
                       (!nextSibling && !IsTablePseudo(aFrame));
          }
#ifdef DEBUG
          else {
            NS_ASSERTION(!IsTablePseudo(aFrame), "How did that happen?");
          }
#endif
        } else {
          okToDrop = (spaceEndIter.item().DesiredParentType() == parentType);
        }

        if (okToDrop) {
          iter.DeleteItemsTo(spaceEndIter);
        } else {
          // We're done: we don't want to drop the whitespace, and it has the
          // wrong parent type.
          break;
        }

        // Now loop, since |iter| points to item right after the whitespace we
        // removed.
      } while (!iter.IsDone());
    }

    // We might be able to figure out some sort of optimizations here, but they
    // would have to depend on having a correct aPrevSibling and a correct next
    // sibling.  For example, we can probably avoid reframing if none of
    // aFrame, aPrevSibling, and next sibling are table pseudo-frames.  But it
    // doesn't seem worth it to worry about that for now, especially since we
    // in fact do not have a reliable aPrevSibling, nor any next sibling, in
    // this method.

    // aItems might have changed, so recheck the parent type thing.  In fact,
    // it might be empty, so recheck that too.
    if (aItems.IsEmpty()) {
      return PR_FALSE;
    }

    if (!aItems.AllWantParentType(parentType)) {
      // Reframing aFrame->GetContent() is good enough, since the content of
      // table pseudo-frames is the ancestor content.
      RecreateFramesForContent(aFrame->GetContent(), PR_TRUE);
      return PR_TRUE;
    }
  }

  // Now we have several cases involving {ib} splits.  Put them all in a
  // do/while with breaks to take us to the "go and reconstruct" code.
  do {
    if (IsInlineFrame(aFrame)) {
      if (aItems.AreAllItemsInline()) {
        // We can just put the kids in.
        return PR_FALSE;
      }

      if (!IsFrameSpecial(aFrame)) {
        // Need to go ahead and reconstruct.
        break;
      }

      // Now we're adding kids including some blocks to an inline part of an
      // {ib} split.  If we plan to call AppendFrames, and don't have a next
      // sibling for the new frames, and our parent is the last continuation of
      // the last part of the {ib} split, and the same is true of all our
      // ancestor inlines (they have no following continuations and they're the
      // last part of their {ib} splits and we'd be adding to the end for all
      // of them), then AppendFrames will handle things for us.  Bail out in
      // that case.
      if (aIsAppend && IsSafeToAppendToSpecialInline(aFrame, nextSibling)) {
        return PR_FALSE;
      }

      // Need to reconstruct.
      break;
    }

    // Now we know we have a block parent.  If it's not special, we're all set.
    if (!IsFrameSpecial(aFrame)) {
      return PR_FALSE;
    }

    // We're adding some kids to a block part of an {ib} split.  If all the
    // kids are blocks, we don't need to reconstruct.
    if (aItems.AreAllItemsBlock()) {
      return PR_FALSE;
    }

    // We might have some inline kids for this block.  Just reconstruct.
    break;
  } while (0);

  // If we don't have a containing block, start with aFrame and look for one.
  if (!aContainingBlock) {
    aContainingBlock = aFrame;
  }
  
  // To find the right block to reframe, just walk up the tree until we find a
  // frame that is:
  // 1)  Not part of an IB split (not special)
  // 2)  Not a pseudo-frame
  // 3)  Not an inline frame
  // We're guaranteed to find one, since nsStyleContext::ApplyStyleFixups
  // enforces that the root is display:none, display:table, or display:block.
  // Note that walking up "too far" is OK in terms of correctness, even if it
  // might be a little inefficient.  This is why we walk out of all
  // pseudo-frames -- telling which ones are or are not OK to walk out of is
  // too hard (and I suspect that we do in fact need to walk out of all of
  // them).
  while (IsFrameSpecial(aContainingBlock) || IsInlineOutside(aContainingBlock) ||
         aContainingBlock->GetStyleContext()->GetPseudo()) {
    aContainingBlock = aContainingBlock->GetParent();
    NS_ASSERTION(aContainingBlock,
                 "Must have non-inline, non-special, non-pseudo frame as root "
                 "(or child of root, for a table root)!");
  }

  // Tell parent of the containing block to reformulate the
  // entire block. This is painful and definitely not optimal
  // but it will *always* get the right answer.

  nsIContent *blockContent = aContainingBlock->GetContent();
#ifdef DEBUG
  if (gNoisyContentUpdates) {
    printf("nsCSSFrameConstructor::WipeContainingBlock: blockContent=%p\n",
           static_cast<void*>(blockContent));
  }
#endif
  RecreateFramesForContent(blockContent, PR_TRUE);
  return PR_TRUE;
}

nsresult
nsCSSFrameConstructor::ReframeContainingBlock(nsIFrame* aFrame)
{

#ifdef DEBUG
  // ReframeContainingBlock is a NASTY routine, it causes terrible performance problems
  // so I want to see when it is happening!  Unfortunately, it is happening way to often because
  // so much content on the web causes 'special' block-in-inline frame situations and we handle them
  // very poorly
  if (gNoisyContentUpdates) {
    printf("nsCSSFrameConstructor::ReframeContainingBlock frame=%p\n",
           static_cast<void*>(aFrame));
  }
#endif

  // XXXbz how exactly would we get here while isReflowing anyway?  Should this
  // whole test be ifdef DEBUG?
  if (mPresShell->IsReflowLocked()) {
    // don't ReframeContainingBlock, this will result in a crash
    // if we remove a tree that's in reflow - see bug 121368 for testcase
    NS_ERROR("Atemptted to nsCSSFrameConstructor::ReframeContainingBlock during a Reflow!!!");
    return NS_OK;
  }

  // Get the first "normal" ancestor of the target frame.
  nsIFrame* containingBlock = GetIBContainingBlockFor(aFrame);
  if (containingBlock) {
    // From here we look for the containing block in case the target
    // frame is already a block (which can happen when an inline frame
    // wraps some of its content in an anonymous block; see
    // ConstructInline)
   
    // NOTE: We used to get the FloatContainingBlock here, but it was often wrong.
    // GetIBContainingBlock works much better and provides the correct container in all cases
    // so GetFloatContainingBlock(aFrame) has been removed

    // And get the containingBlock's content
    nsCOMPtr<nsIContent> blockContent = containingBlock->GetContent();
    if (blockContent) {
#ifdef DEBUG
      if (gNoisyContentUpdates) {
        printf("  ==> blockContent=%p\n", static_cast<void*>(blockContent));
      }
#endif
      return RecreateFramesForContent(blockContent, PR_TRUE);
    }
  }

  // If we get here, we're screwed!
  return RecreateFramesForContent(mPresShell->GetDocument()->GetRootElement(),
				  PR_TRUE);
}

void
nsCSSFrameConstructor::RestyleForEmptyChange(Element* aContainer)
{
  // In some cases (:empty + E, :empty ~ E), a change if the content of
  // an element requires restyling its parent's siblings.
  nsRestyleHint hint = eRestyle_Subtree;
  nsIContent* grandparent = aContainer->GetParent();
  if (grandparent &&
      (grandparent->GetFlags() & NODE_HAS_SLOW_SELECTOR_LATER_SIBLINGS)) {
    hint = nsRestyleHint(hint | eRestyle_LaterSiblings);
  }
  PostRestyleEvent(aContainer, hint, NS_STYLE_HINT_NONE);
}

void
nsCSSFrameConstructor::RestyleForAppend(Element* aContainer,
                                        nsIContent* aFirstNewContent)
{
  NS_ASSERTION(aContainer, "must have container for append");
#ifdef DEBUG
  {
    for (nsIContent* cur = aFirstNewContent; cur; cur = cur->GetNextSibling()) {
      NS_ASSERTION(!cur->IsRootOfAnonymousSubtree(),
                   "anonymous nodes should not be in child lists");
    }
  }
#endif
  PRUint32 selectorFlags =
    aContainer->GetFlags() & (NODE_ALL_SELECTOR_FLAGS &
                              ~NODE_HAS_SLOW_SELECTOR_LATER_SIBLINGS);
  if (selectorFlags == 0)
    return;

  if (selectorFlags & NODE_HAS_EMPTY_SELECTOR) {
    // see whether we need to restyle the container
    PRBool wasEmpty = PR_TRUE; // :empty or :-moz-only-whitespace
    for (nsIContent* cur = aContainer->GetFirstChild();
         cur != aFirstNewContent;
         cur = cur->GetNextSibling()) {
      // We don't know whether we're testing :empty or :-moz-only-whitespace,
      // so be conservative and assume :-moz-only-whitespace (i.e., make
      // IsSignificantChild less likely to be true, and thus make us more
      // likely to restyle).
      if (nsStyleUtil::IsSignificantChild(cur, PR_TRUE, PR_FALSE)) {
        wasEmpty = PR_FALSE;
        break;
      }
    }
    if (wasEmpty) {
      RestyleForEmptyChange(aContainer);
      return;
    }
  }

  if (selectorFlags & NODE_HAS_SLOW_SELECTOR) {
    PostRestyleEvent(aContainer, eRestyle_Subtree, NS_STYLE_HINT_NONE);
    // Restyling the container is the most we can do here, so we're done.
    return;
  }

  if (selectorFlags & NODE_HAS_EDGE_CHILD_SELECTOR) {
    // restyle the last element child before this node
    for (nsIContent* cur = aFirstNewContent->GetPreviousSibling();
         cur;
         cur = cur->GetPreviousSibling()) {
      if (cur->IsElement()) {
        PostRestyleEvent(cur->AsElement(), eRestyle_Subtree, NS_STYLE_HINT_NONE);
        break;
      }
    }
  }
}

// Needed since we can't use PostRestyleEvent on non-elements (with
// eRestyle_LaterSiblings or nsRestyleHint(eRestyle_Subtree |
// eRestyle_LaterSiblings) as appropriate).
static void
RestyleSiblingsStartingWith(nsCSSFrameConstructor *aFrameConstructor,
                            nsIContent *aStartingSibling /* may be null */)
{
  for (nsIContent *sibling = aStartingSibling; sibling;
       sibling = sibling->GetNextSibling()) {
    if (sibling->IsElement()) {
      aFrameConstructor->
        PostRestyleEvent(sibling->AsElement(),
                         nsRestyleHint(eRestyle_Subtree | eRestyle_LaterSiblings),
                         NS_STYLE_HINT_NONE);
      break;
    }
  }
}

// Restyling for a ContentInserted or CharacterDataChanged notification.
// This could be used for ContentRemoved as well if we got the
// notification before the removal happened (and sometimes
// CharacterDataChanged is more like a removal than an addition).
// The comments are written and variables are named in terms of it being
// a ContentInserted notification.
void
nsCSSFrameConstructor::RestyleForInsertOrChange(Element* aContainer,
                                                nsIContent* aChild)
{
  NS_ASSERTION(!aChild->IsRootOfAnonymousSubtree(),
               "anonymous nodes should not be in child lists");
  PRUint32 selectorFlags =
    aContainer ? (aContainer->GetFlags() & NODE_ALL_SELECTOR_FLAGS) : 0;
  if (selectorFlags == 0)
    return;

  if (selectorFlags & NODE_HAS_EMPTY_SELECTOR) {
    // see whether we need to restyle the container
    PRBool wasEmpty = PR_TRUE; // :empty or :-moz-only-whitespace
    for (nsIContent* child = aContainer->GetFirstChild();
         child;
         child = child->GetNextSibling()) {
      if (child == aChild)
        continue;
      // We don't know whether we're testing :empty or :-moz-only-whitespace,
      // so be conservative and assume :-moz-only-whitespace (i.e., make
      // IsSignificantChild less likely to be true, and thus make us more
      // likely to restyle).
      if (nsStyleUtil::IsSignificantChild(child, PR_TRUE, PR_FALSE)) {
        wasEmpty = PR_FALSE;
        break;
      }
    }
    if (wasEmpty) {
      RestyleForEmptyChange(aContainer);
      return;
    }
  }

  if (selectorFlags & NODE_HAS_SLOW_SELECTOR) {
    PostRestyleEvent(aContainer, eRestyle_Subtree, NS_STYLE_HINT_NONE);
    // Restyling the container is the most we can do here, so we're done.
    return;
  }

  if (selectorFlags & NODE_HAS_SLOW_SELECTOR_LATER_SIBLINGS) {
    // Restyle all later siblings.
    RestyleSiblingsStartingWith(this, aChild->GetNextSibling());
  }

  if (selectorFlags & NODE_HAS_EDGE_CHILD_SELECTOR) {
    // restyle the previously-first element child if it is after this node
    PRBool passedChild = PR_FALSE;
    for (nsIContent* content = aContainer->GetFirstChild();
         content;
         content = content->GetNextSibling()) {
      if (content == aChild) {
        passedChild = PR_TRUE;
        continue;
      }
      if (content->IsElement()) {
        if (passedChild) {
          PostRestyleEvent(content->AsElement(), eRestyle_Subtree,
                           NS_STYLE_HINT_NONE);
        }
        break;
      }
    }
    // restyle the previously-last element child if it is before this node
    passedChild = PR_FALSE;
    for (nsIContent* content = aContainer->GetLastChild();
         content;
         content = content->GetPreviousSibling()) {
      if (content == aChild) {
        passedChild = PR_TRUE;
        continue;
      }
      if (content->IsElement()) {
        if (passedChild) {
          PostRestyleEvent(content->AsElement(), eRestyle_Subtree,
                           NS_STYLE_HINT_NONE);
        }
        break;
      }
    }
  }
}

void
nsCSSFrameConstructor::RestyleForRemove(Element* aContainer,
                                        nsIContent* aOldChild,
                                        nsIContent* aFollowingSibling)
{
  NS_ASSERTION(!aOldChild->IsRootOfAnonymousSubtree(),
               "anonymous nodes should not be in child lists");
  PRUint32 selectorFlags =
    aContainer ? (aContainer->GetFlags() & NODE_ALL_SELECTOR_FLAGS) : 0;
  if (selectorFlags == 0)
    return;

  if (selectorFlags & NODE_HAS_EMPTY_SELECTOR) {
    // see whether we need to restyle the container
    PRBool isEmpty = PR_TRUE; // :empty or :-moz-only-whitespace
    for (nsIContent* child = aContainer->GetFirstChild();
         child;
         child = child->GetNextSibling()) {
      // We don't know whether we're testing :empty or :-moz-only-whitespace,
      // so be conservative and assume :-moz-only-whitespace (i.e., make
      // IsSignificantChild less likely to be true, and thus make us more
      // likely to restyle).
      if (nsStyleUtil::IsSignificantChild(child, PR_TRUE, PR_FALSE)) {
        isEmpty = PR_FALSE;
        break;
      }
    }
    if (isEmpty) {
      RestyleForEmptyChange(aContainer);
      return;
    }
  }

  if (selectorFlags & NODE_HAS_SLOW_SELECTOR) {
    PostRestyleEvent(aContainer, eRestyle_Subtree, NS_STYLE_HINT_NONE);
    // Restyling the container is the most we can do here, so we're done.
    return;
  }

  if (selectorFlags & NODE_HAS_SLOW_SELECTOR_LATER_SIBLINGS) {
    // Restyle all later siblings.
    RestyleSiblingsStartingWith(this, aFollowingSibling);
  }

  if (selectorFlags & NODE_HAS_EDGE_CHILD_SELECTOR) {
    // restyle the now-first element child if it was after aOldChild
    PRBool reachedFollowingSibling = PR_FALSE;
    for (nsIContent* content = aContainer->GetFirstChild();
         content;
         content = content->GetNextSibling()) {
      if (content == aFollowingSibling) {
        reachedFollowingSibling = PR_TRUE;
        // do NOT continue here; we might want to restyle this node
      }
      if (content->IsElement()) {
        if (reachedFollowingSibling) {
          PostRestyleEvent(content->AsElement(), eRestyle_Subtree,
                           NS_STYLE_HINT_NONE);
        }
        break;
      }
    }
    // restyle the now-last element child if it was before aOldChild
    reachedFollowingSibling = (aFollowingSibling == nsnull);
    for (nsIContent* content = aContainer->GetLastChild();
         content;
         content = content->GetPreviousSibling()) {
      if (content->IsElement()) {
        if (reachedFollowingSibling) {
          PostRestyleEvent(content->AsElement(), eRestyle_Subtree, NS_STYLE_HINT_NONE);
        }
        break;
      }
      if (content == aFollowingSibling) {
        reachedFollowingSibling = PR_TRUE;
      }
    }
  }
}


void
nsCSSFrameConstructor::RebuildAllStyleData(nsChangeHint aExtraHint)
{
  NS_ASSERTION(!(aExtraHint & nsChangeHint_ReconstructFrame),
               "Should not reconstruct the root of the frame tree.  "
               "Use ReconstructDocElementHierarchy instead.");

  mRebuildAllStyleData = PR_FALSE;
  NS_UpdateHint(aExtraHint, mRebuildAllExtraHint);
  mRebuildAllExtraHint = nsChangeHint(0);

  if (!mPresShell || !mPresShell->GetRootFrame())
    return;

  // Make sure that the viewmanager will outlive the presshell
  nsIViewManager::UpdateViewBatch batch(mPresShell->GetViewManager());

  // Processing the style changes could cause a flush that propagates to
  // the parent frame and thus destroys the pres shell.
  nsCOMPtr<nsIPresShell> kungFuDeathGrip(mPresShell);

  // We may reconstruct frames below and hence process anything that is in the
  // tree. We don't want to get notified to process those items again after.
  mPresShell->GetDocument()->FlushPendingNotifications(Flush_ContentAndNotify);

  nsAutoScriptBlocker scriptBlocker;

  // Tell the style set to get the old rule tree out of the way
  // so we can recalculate while maintaining rule tree immutability
  nsresult rv = mPresShell->StyleSet()->BeginReconstruct();
  if (NS_FAILED(rv)) {
    batch.EndUpdateViewBatch(NS_VMREFRESH_NO_SYNC);
    return;
  }

  // Recalculate all of the style contexts for the document
  // Note that we can ignore the return value of ComputeStyleChangeFor
  // because we never need to reframe the root frame
  // XXX This could be made faster by not rerunning rule matching
  // (but note that nsPresShell::SetPreferenceStyleRules currently depends
  // on us re-running rule matching here
  nsStyleChangeList changeList;
  // XXX Does it matter that we're passing aExtraHint to the real root
  // frame and not the root node's primary frame?
  // Note: The restyle tracker we pass in here doesn't matter.
  mPresShell->FrameManager()->ComputeStyleChangeFor(mPresShell->GetRootFrame(),
                                                    &changeList, aExtraHint,
                                                    mPendingRestyles, PR_TRUE);
  // Process the required changes
  ProcessRestyledFrames(changeList);
  // Tell the style set it's safe to destroy the old rule tree.  We
  // must do this after the ProcessRestyledFrames call in case the
  // change list has frame reconstructs in it (since frames to be
  // reconstructed will still have their old style context pointers
  // until they are destroyed).
  mPresShell->StyleSet()->EndReconstruct();
  batch.EndUpdateViewBatch(NS_VMREFRESH_NO_SYNC);
}

void
nsCSSFrameConstructor::ProcessPendingRestyles()
{
  NS_PRECONDITION(mDocument, "No document?  Pshaw!");
  NS_PRECONDITION(!nsContentUtils::IsSafeToRunScript(),
                  "Missing a script blocker!");

  // Process non-animation restyles...
  nsPresContext *presContext = mPresShell->GetPresContext();
  NS_ABORT_IF_FALSE(!presContext->IsProcessingRestyles(),
                    "Nesting calls to ProcessPendingRestyles?");
  presContext->SetProcessingRestyles(PR_TRUE);

  mPendingRestyles.ProcessRestyles();

#ifdef DEBUG
  PRUint32 oldPendingRestyleCount = mPendingRestyles.Count();
#endif

  // ...and then process animation restyles.  This needs to happen
  // second because we need to start animations that resulted from the
  // first set of restyles (e.g., CSS transitions with negative
  // transition-delay), and because we need to immediately
  // restyle-with-animation any just-restyled elements that are
  // mid-transition (since processing the non-animation restyle ignores
  // the running transition so it can check for a new change on the same
  // property, and then posts an immediate animation style change).
  presContext->SetProcessingAnimationStyleChange(PR_TRUE);
  mPendingAnimationRestyles.ProcessRestyles();
  presContext->SetProcessingAnimationStyleChange(PR_FALSE);

  presContext->SetProcessingRestyles(PR_FALSE);
  NS_POSTCONDITION(mPendingRestyles.Count() == oldPendingRestyleCount,
                   "We should not have posted new non-animation restyles while "
                   "processing animation restyles");

  if (mRebuildAllStyleData) {
    // We probably wasted a lot of work up above, but this seems safest
    // and it should be rarely used.
    // This might add us as a refresh observer again; that's ok.
    RebuildAllStyleData(nsChangeHint(0));
  }
}

void
nsCSSFrameConstructor::PostRestyleEventCommon(Element* aElement,
                                              nsRestyleHint aRestyleHint,
                                              nsChangeHint aMinChangeHint,
                                              PRBool aForAnimation)
{
  if (NS_UNLIKELY(mPresShell->IsDestroying())) {
    return;
  }

  if (aRestyleHint == 0 && !aMinChangeHint) {
    // Nothing to do here
    return;
  }

  RestyleTracker& tracker =
    aForAnimation ? mPendingAnimationRestyles : mPendingRestyles;
  tracker.AddPendingRestyle(aElement, aRestyleHint, aMinChangeHint);

  PostRestyleEventInternal(PR_FALSE);
}
    
void
nsCSSFrameConstructor::PostRestyleEventInternal(PRBool aForLazyConstruction)
{
  // Make sure we're not in a style refresh; if we are, we still have
  // a call to ProcessPendingRestyles coming and there's no need to
  // add ourselves as a refresh observer until then.
  PRBool inRefresh = !aForLazyConstruction && mInStyleRefresh;
  if (!mObservingRefreshDriver && !inRefresh) {
    mObservingRefreshDriver = mPresShell->GetPresContext()->RefreshDriver()->
      AddStyleFlushObserver(mPresShell);
  }
}

void
nsCSSFrameConstructor::PostRebuildAllStyleDataEvent(nsChangeHint aExtraHint)
{
  NS_ASSERTION(!(aExtraHint & nsChangeHint_ReconstructFrame),
               "Should not reconstruct the root of the frame tree.  "
               "Use ReconstructDocElementHierarchy instead.");

  mRebuildAllStyleData = PR_TRUE;
  NS_UpdateHint(mRebuildAllExtraHint, aExtraHint);
  // Get a restyle event posted if necessary
  PostRestyleEventInternal(PR_FALSE);
}

NS_IMETHODIMP
nsCSSFrameConstructor::LazyGenerateChildrenEvent::Run()
{
  mPresShell->GetDocument()->FlushPendingNotifications(Flush_Layout);

  // this is hard-coded to handle only menu popup frames
  nsIFrame* frame = mPresShell->GetPresContext() ?
    mPresShell->GetPresContext()->GetPrimaryFrameFor(mContent) : nsnull;
  if (frame && frame->GetType() == nsGkAtoms::menuPopupFrame) {
    nsWeakFrame weakFrame(frame);
#ifdef MOZ_XUL
    // it is possible that the frame is different than the one that requested
    // the lazy generation, but as long as it's a popup frame that hasn't
    // generated its children yet, that's OK.
    nsMenuPopupFrame* menuPopupFrame = static_cast<nsMenuPopupFrame *>(frame);
    if (menuPopupFrame->HasGeneratedChildren()) {
      if (mCallback)
        mCallback(mContent, frame, mArg);
      
      return NS_OK;
    }     

    // indicate that the children have been generated
    menuPopupFrame->SetGeneratedChildren();
#endif

   {
      nsAutoScriptBlocker scriptBlocker;
      nsCSSFrameConstructor* fc = mPresShell->FrameConstructor();
      fc->BeginUpdate();

      nsFrameItems childItems;
      nsFrameConstructorState state(mPresShell, nsnull, nsnull, nsnull);
      // We don't have a parent frame with a pending binding constructor here,
      // so no need to worry about ordering of the kids' constructors with it.
      // Pass null for the PendingBinding.
      nsresult rv = fc->ProcessChildren(state, mContent, frame->GetStyleContext(),
                                        frame, PR_FALSE, childItems, PR_FALSE,
                                        nsnull);
      if (NS_FAILED(rv)) {
        fc->EndUpdate();
        return rv;
      }

      frame->SetInitialChildList(nsnull, childItems);

      fc->EndUpdate();
    }

    if (mCallback && weakFrame.IsAlive())
      mCallback(mContent, frame, mArg);

    // call XBL constructors after the frames are created
    mPresShell->GetDocument()->BindingManager()->ProcessAttachedQueue();
  }

  return NS_OK;
}

//////////////////////////////////////////////////////////
// nsCSSFrameConstructor::FrameConstructionItem methods //
//////////////////////////////////////////////////////////
PRBool
nsCSSFrameConstructor::
FrameConstructionItem::IsWhitespace(nsFrameConstructorState& aState) const
{
  NS_PRECONDITION(!aState.mSetPrimaryFrames ||
                  !mContent->GetPrimaryFrame(), "How did that happen?");
  if (!mIsText) {
    return PR_FALSE;
  }
  mContent->SetFlags(NS_CREATE_FRAME_IF_NON_WHITESPACE |
                     NS_REFRAME_IF_WHITESPACE);
  return mContent->TextIsOnlyWhitespace();
}

//////////////////////////////////////////////////////////////
// nsCSSFrameConstructor::FrameConstructionItemList methods //
//////////////////////////////////////////////////////////////
void
nsCSSFrameConstructor::FrameConstructionItemList::
AdjustCountsForItem(FrameConstructionItem* aItem, PRInt32 aDelta)
{
  NS_PRECONDITION(aDelta == 1 || aDelta == -1, "Unexpected delta");
  mItemCount += aDelta;
  if (aItem->mIsAllInline) {
    mInlineCount += aDelta;
  }
  if (aItem->mIsBlock) {
    mBlockCount += aDelta;
  }
  if (aItem->mIsLineParticipant) {
    mLineParticipantCount += aDelta;
  }
  mDesiredParentCounts[aItem->DesiredParentType()] += aDelta;
}

////////////////////////////////////////////////////////////////////////
// nsCSSFrameConstructor::FrameConstructionItemList::Iterator methods //
////////////////////////////////////////////////////////////////////////
inline PRBool
nsCSSFrameConstructor::FrameConstructionItemList::
Iterator::SkipItemsWantingParentType(ParentType aParentType)
{
  NS_PRECONDITION(!IsDone(), "Shouldn't be done yet");
  while (item().DesiredParentType() == aParentType) {
    Next();
    if (IsDone()) {
      return PR_TRUE;
    }
  }
  return PR_FALSE;
}

inline PRBool
nsCSSFrameConstructor::FrameConstructionItemList::
Iterator::SkipWhitespace(nsFrameConstructorState& aState)
{
  NS_PRECONDITION(!IsDone(), "Shouldn't be done yet");
  NS_PRECONDITION(item().IsWhitespace(aState), "Not pointing to whitespace?");
  do {
    Next();
    if (IsDone()) {
      return PR_TRUE;
    }
  } while (item().IsWhitespace(aState));

  return PR_FALSE;
}

void
nsCSSFrameConstructor::FrameConstructionItemList::
Iterator::AppendItemToList(FrameConstructionItemList& aTargetList)
{
  NS_ASSERTION(&aTargetList != &mList, "Unexpected call");
  NS_PRECONDITION(!IsDone(), "should not be done");

  FrameConstructionItem* item = ToItem(mCurrent);
  Next();
  PR_REMOVE_LINK(item);
  PR_APPEND_LINK(item, &aTargetList.mItems);

  mList.AdjustCountsForItem(item, -1);
  aTargetList.AdjustCountsForItem(item, 1);
}

void
nsCSSFrameConstructor::FrameConstructionItemList::
Iterator::AppendItemsToList(const Iterator& aEnd,
                            FrameConstructionItemList& aTargetList)
{
  NS_ASSERTION(&aTargetList != &mList, "Unexpected call");
  NS_PRECONDITION(mEnd == aEnd.mEnd, "end iterator for some other list?");

  if (!AtStart() || !aEnd.IsDone() || !aTargetList.IsEmpty()) {
    do {
      AppendItemToList(aTargetList);
    } while (*this != aEnd);
    return;
  }

  // move over the list of items
  PR_INSERT_AFTER(&aTargetList.mItems, &mList.mItems);
  PR_REMOVE_LINK(&mList.mItems);

  // Copy over the various counters
  aTargetList.mInlineCount = mList.mInlineCount;
  aTargetList.mBlockCount = mList.mBlockCount;
  aTargetList.mLineParticipantCount = mList.mLineParticipantCount;
  aTargetList.mItemCount = mList.mItemCount;
  memcpy(aTargetList.mDesiredParentCounts, mList.mDesiredParentCounts,
         sizeof(aTargetList.mDesiredParentCounts));

  // reset mList
  new (&mList) FrameConstructionItemList();

  // Point ourselves to aEnd, as advertised
  mCurrent = mEnd = &mList.mItems;
  NS_POSTCONDITION(*this == aEnd, "How did that happen?");
}

void
nsCSSFrameConstructor::FrameConstructionItemList::
Iterator::InsertItem(FrameConstructionItem* aItem)
{
  // Just insert the item before us.  There's no magic here.
  PR_INSERT_BEFORE(aItem, mCurrent);
  mList.AdjustCountsForItem(aItem, 1);

  NS_POSTCONDITION(PR_NEXT_LINK(aItem) == mCurrent, "How did that happen?");
}

void
nsCSSFrameConstructor::FrameConstructionItemList::
Iterator::DeleteItemsTo(const Iterator& aEnd)
{
  NS_PRECONDITION(mEnd == aEnd.mEnd, "end iterator for some other list?");
  NS_PRECONDITION(*this != aEnd, "Shouldn't be at aEnd yet");

  do {
    NS_ASSERTION(!IsDone(), "Ran off end of list?");
    FrameConstructionItem* item = ToItem(mCurrent);
    Next();
    PR_REMOVE_LINK(item);
    mList.AdjustCountsForItem(item, -1);
    delete item;
  } while (*this != aEnd);
}
