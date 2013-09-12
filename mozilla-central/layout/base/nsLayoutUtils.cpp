/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=78: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "mozilla/Util.h"

#include "nsLayoutUtils.h"
#include "nsIFormControlFrame.h"
#include "nsPresContext.h"
#include "nsIContent.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMHTMLElement.h"
#include "nsFrameList.h"
#include "nsGkAtoms.h"
#include "nsIAtom.h"
#include "nsCSSPseudoElements.h"
#include "nsCSSAnonBoxes.h"
#include "nsCSSColorUtils.h"
#include "nsIView.h"
#include "nsPlaceholderFrame.h"
#include "nsIScrollableFrame.h"
#include "nsCSSFrameConstructor.h"
#include "nsIDOMEvent.h"
#include "nsGUIEvent.h"
#include "nsDisplayList.h"
#include "nsRegion.h"
#include "nsFrameManager.h"
#include "nsBlockFrame.h"
#include "nsBidiPresUtils.h"
#include "imgIContainer.h"
#include "gfxRect.h"
#include "gfxContext.h"
#include "gfxFont.h"
#include "nsRenderingContext.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsCSSRendering.h"
#include "nsContentUtils.h"
#include "nsThemeConstants.h"
#include "nsPIDOMWindow.h"
#include "nsIBaseWindow.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIWidget.h"
#include "gfxMatrix.h"
#include "gfxPoint3D.h"
#include "gfxTypes.h"
#include "gfxUserFontSet.h"
#include "nsTArray.h"
#include "nsHTMLCanvasElement.h"
#include "nsICanvasRenderingContextInternal.h"
#include "gfxPlatform.h"
#include "nsClientRect.h"
#ifdef MOZ_MEDIA
#include "nsHTMLVideoElement.h"
#endif
#include "nsHTMLImageElement.h"
#include "imgIRequest.h"
#include "nsIImageLoadingContent.h"
#include "nsCOMPtr.h"
#include "nsCSSProps.h"
#include "nsListControlFrame.h"
#include "ImageLayers.h"
#include "mozilla/arm.h"
#include "mozilla/dom/Element.h"
#include "nsCanvasFrame.h"
#include "gfxDrawable.h"
#include "gfxUtils.h"
#include "nsDataHashtable.h"
#include "nsTextFrame.h"
#include "nsFontFaceList.h"
#include "nsFontInflationData.h"
#include "nsSVGUtils.h"
#include "nsSVGIntegrationUtils.h"
#include "nsSVGForeignObjectFrame.h"
#include "nsSVGOuterSVGFrame.h"
#include "nsStyleStructInlines.h"

#include "mozilla/dom/PBrowserChild.h"
#include "mozilla/dom/TabChild.h"
#include "mozilla/Preferences.h"

#ifdef MOZ_XUL
#include "nsXULPopupManager.h"
#endif

#include "sampler.h"
#include "nsAnimationManager.h"
#include "nsTransitionManager.h"
#include "nsViewportInfo.h"

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;
using namespace mozilla::layers;
using namespace mozilla::layout;

#define FLEXBOX_ENABLED_PREF_NAME "layout.css.flexbox.enabled"

#ifdef DEBUG
// TODO: remove, see bug 598468.
bool nsLayoutUtils::gPreventAssertInCompareTreePosition = false;
#endif // DEBUG

typedef gfxPattern::GraphicsFilter GraphicsFilter;
typedef FrameMetrics::ViewID ViewID;

/* static */ uint32_t nsLayoutUtils::sFontSizeInflationEmPerLine;
/* static */ uint32_t nsLayoutUtils::sFontSizeInflationMinTwips;
/* static */ uint32_t nsLayoutUtils::sFontSizeInflationLineThreshold;
/* static */ int32_t  nsLayoutUtils::sFontSizeInflationMappingIntercept;
/* static */ uint32_t nsLayoutUtils::sFontSizeInflationMaxRatio;
/* static */ bool nsLayoutUtils::sFontSizeInflationForceEnabled;
/* static */ bool nsLayoutUtils::sFontSizeInflationDisabledInMasterProcess;

static ViewID sScrollIdCounter = FrameMetrics::START_SCROLL_ID;

#ifdef MOZ_FLEXBOX
// These are indices into kDisplayKTable.  They'll be initialized
// the first time that FlexboxEnabledPrefChangeCallback() is invoked.
static int32_t sIndexOfFlexInDisplayTable;
static int32_t sIndexOfInlineFlexInDisplayTable;
// This tracks whether those ^^ indices have been initialized
static bool sAreFlexKeywordIndicesInitialized = false;
#endif // MOZ_FLEXBOX

typedef nsDataHashtable<nsUint64HashKey, nsIContent*> ContentMap;
static ContentMap* sContentMap = nullptr;
static ContentMap& GetContentMap() {
  if (!sContentMap) {
    sContentMap = new ContentMap();
    sContentMap->Init();
  }
  return *sContentMap;
}

// When the pref "layout.css.flexbox.enabled" changes, this function is invoked
// to let us update kDisplayKTable, to selectively disable or restore the
// entries for "flex" and "inline-flex" in that table.
#ifdef MOZ_FLEXBOX
static int
FlexboxEnabledPrefChangeCallback(const char* aPrefName, void* aClosure)
{
  MOZ_ASSERT(strncmp(aPrefName, FLEXBOX_ENABLED_PREF_NAME,
                     NS_ARRAY_LENGTH(FLEXBOX_ENABLED_PREF_NAME)) == 0,
             "We only registered this callback for a single pref, so it "
             "should only be called for that pref");

  bool isFlexboxEnabled =
    Preferences::GetBool(FLEXBOX_ENABLED_PREF_NAME, false);

  if (!sAreFlexKeywordIndicesInitialized) {
    // First run: find the position of "flex" and "inline-flex" in
    // kDisplayKTable.
    sIndexOfFlexInDisplayTable =
      nsCSSProps::FindIndexOfKeyword(eCSSKeyword_flex,
                                     nsCSSProps::kDisplayKTable);
    sIndexOfInlineFlexInDisplayTable =
      nsCSSProps::FindIndexOfKeyword(eCSSKeyword_inline_flex,
                                     nsCSSProps::kDisplayKTable);

    sAreFlexKeywordIndicesInitialized = true;
  }

  // OK -- now, stomp on or restore the "flex" entries in kDisplayKTable,
  // depending on whether the flexbox pref is enabled vs. disabled.
  if (sIndexOfFlexInDisplayTable >= 0) {
    nsCSSProps::kDisplayKTable[sIndexOfFlexInDisplayTable] =
      isFlexboxEnabled ? eCSSKeyword_flex : eCSSKeyword_UNKNOWN;
  }
  if (sIndexOfInlineFlexInDisplayTable >= 0) {
    nsCSSProps::kDisplayKTable[sIndexOfInlineFlexInDisplayTable] =
      isFlexboxEnabled ? eCSSKeyword_inline_flex : eCSSKeyword_UNKNOWN;
  }

  return 0;
}
#endif // MOZ_FLEXBOX

template <class AnimationsOrTransitions>
static AnimationsOrTransitions*
HasAnimationOrTransition(nsIContent* aContent,
                         nsIAtom* aAnimationProperty,
                         nsCSSProperty aProperty)
{
  AnimationsOrTransitions* animations =
    static_cast<AnimationsOrTransitions*>(aContent->GetProperty(aAnimationProperty));
  if (animations) {
    bool propertyMatches = animations->HasAnimationOfProperty(aProperty);
    if (propertyMatches &&
        animations->CanPerformOnCompositorThread(
          CommonElementAnimationData::CanAnimate_AllowPartial)) {
      return animations;
    }
  }

  return nullptr;
}

bool
nsLayoutUtils::HasAnimationsForCompositor(nsIContent* aContent,
                                          nsCSSProperty aProperty)
{
  if (!aContent->MayHaveAnimations())
    return false;
  if (HasAnimationOrTransition<ElementAnimations>
        (aContent, nsGkAtoms::animationsProperty, aProperty)) {
    return true;
  }
  return HasAnimationOrTransition<ElementTransitions>
    (aContent, nsGkAtoms::transitionsProperty, aProperty);
}

static gfxSize
GetScaleForValue(const nsStyleAnimation::Value& aValue,
                 nsIFrame* aFrame)
{
  if (!aFrame) {
    NS_WARNING("No frame.");
    return gfxSize();
  }
  if (aValue.GetUnit() != nsStyleAnimation::eUnit_Transform) {
    NS_WARNING("Expected a transform.");
    return gfxSize();
  }

  nsCSSValueList* values = aValue.GetCSSValueListValue();
  if (values->mValue.GetUnit() == eCSSUnit_None) {
    // There is an animation, but no actual transform yet.
    return gfxSize();
  }

  nsRect frameBounds = aFrame->GetRect();
  bool dontCare;
  gfx3DMatrix transform = nsStyleTransformMatrix::ReadTransforms(
                            aValue.GetCSSValueListValue(),
                            aFrame->GetStyleContext(),
                            aFrame->PresContext(), dontCare, frameBounds,
                            aFrame->PresContext()->AppUnitsPerDevPixel());

  gfxMatrix transform2d;
  bool canDraw2D = transform.CanDraw2D(&transform2d);
  if (!canDraw2D) {
    return gfxSize();
  }

  return transform2d.ScaleFactors(true);
}

gfxSize
nsLayoutUtils::GetMaximumAnimatedScale(nsIContent* aContent)
{
  gfxSize result;
  ElementAnimations* animations = HasAnimationOrTransition<ElementAnimations>
    (aContent, nsGkAtoms::animationsProperty, eCSSProperty_transform);
  if (animations) {
    for (uint32_t animIdx = animations->mAnimations.Length(); animIdx-- != 0; ) {
      ElementAnimation& anim = animations->mAnimations[animIdx];
      for (uint32_t propIdx = anim.mProperties.Length(); propIdx-- != 0; ) {
        AnimationProperty& prop = anim.mProperties[propIdx];
        if (prop.mProperty == eCSSProperty_transform) {
          for (uint32_t segIdx = prop.mSegments.Length(); segIdx-- != 0; ) {
            AnimationPropertySegment& segment = prop.mSegments[segIdx];
            gfxSize from = GetScaleForValue(segment.mFromValue,
                                            aContent->GetPrimaryFrame());
            result.width = NS_MAX<float>(result.width, from.width);
            result.height = NS_MAX<float>(result.height, from.height);
            gfxSize to = GetScaleForValue(segment.mToValue,
                                          aContent->GetPrimaryFrame());
            result.width = NS_MAX<float>(result.width, to.width);
            result.height = NS_MAX<float>(result.height, to.height);
          }
        }
      }
    }
  }
  ElementTransitions* transitions = HasAnimationOrTransition<ElementTransitions>
    (aContent, nsGkAtoms::transitionsProperty, eCSSProperty_transform);
  if (transitions) {
    for (uint32_t i = 0, i_end = transitions->mPropertyTransitions.Length();
         i < i_end; ++i)
    {
      ElementPropertyTransition &pt = transitions->mPropertyTransitions[i];
      if (pt.IsRemovedSentinel()) {
        continue;
      }

      if (pt.mProperty == eCSSProperty_transform) {
        gfxSize start = GetScaleForValue(pt.mStartValue,
                                         aContent->GetPrimaryFrame());
        result.width = NS_MAX<float>(result.width, start.width);
        result.height = NS_MAX<float>(result.height, start.height);
        gfxSize end = GetScaleForValue(pt.mEndValue,
                                       aContent->GetPrimaryFrame());
        result.width = NS_MAX<float>(result.width, end.width);
        result.height = NS_MAX<float>(result.height, end.height);
      }
    }
  }

  // If we didn't manage to find a max scale, use no scale rather than 0,0
  if (result == gfxSize()) {
    return gfxSize(1, 1);
  }

  return result;
}

bool
nsLayoutUtils::Are3DTransformsEnabled()
{
  static bool s3DTransformsEnabled;
  static bool s3DTransformPrefCached = false;

  if (!s3DTransformPrefCached) {
    s3DTransformPrefCached = true;
    Preferences::AddBoolVarCache(&s3DTransformsEnabled,
                                 "layout.3d-transforms.enabled");
  }

  return s3DTransformsEnabled;
}

bool
nsLayoutUtils::AreOpacityAnimationsEnabled()
{
  static bool sAreOpacityAnimationsEnabled;
  static bool sOpacityPrefCached = false;

  if (!sOpacityPrefCached) {
    sOpacityPrefCached = true;
    Preferences::AddBoolVarCache(&sAreOpacityAnimationsEnabled,
                                 "layers.offmainthreadcomposition.animate-opacity");
  }

  return sAreOpacityAnimationsEnabled &&
    gfxPlatform::OffMainThreadCompositingEnabled();
}

bool
nsLayoutUtils::AreTransformAnimationsEnabled()
{
  static bool sAreTransformAnimationsEnabled;
  static bool sTransformPrefCached = false;

  if (!sTransformPrefCached) {
    sTransformPrefCached = true;
    Preferences::AddBoolVarCache(&sAreTransformAnimationsEnabled,
                                 "layers.offmainthreadcomposition.animate-transform");
  }

  return sAreTransformAnimationsEnabled &&
    gfxPlatform::OffMainThreadCompositingEnabled();
}

bool
nsLayoutUtils::IsAnimationLoggingEnabled()
{
  static bool sShouldLog;
  static bool sShouldLogPrefCached;

  if (!sShouldLogPrefCached) {
    sShouldLogPrefCached = true;
    Preferences::AddBoolVarCache(&sShouldLog,
                                 "layers.offmainthreadcomposition.log-animations");
  }

  return sShouldLog;
}

bool
nsLayoutUtils::UseBackgroundNearestFiltering()
{
  static bool sUseBackgroundNearestFilteringEnabled;
  static bool sUseBackgroundNearestFilteringPrefInitialised = false;

  if (!sUseBackgroundNearestFilteringPrefInitialised) {
    sUseBackgroundNearestFilteringPrefInitialised = true;
    sUseBackgroundNearestFilteringEnabled =
      Preferences::GetBool("gfx.filter.nearest.force-enabled", false);
  }

  return sUseBackgroundNearestFilteringEnabled;
}

bool
nsLayoutUtils::GPUImageScalingEnabled()
{
  static bool sGPUImageScalingEnabled;
  static bool sGPUImageScalingPrefInitialised = false;

  if (!sGPUImageScalingPrefInitialised) {
    sGPUImageScalingPrefInitialised = true;
    sGPUImageScalingEnabled =
      Preferences::GetBool("layout.gpu-image-scaling.enabled", false);
  }

  return sGPUImageScalingEnabled;
}

void
nsLayoutUtils::UnionChildOverflow(nsIFrame* aFrame,
                                  nsOverflowAreas& aOverflowAreas)
{
  // Iterate over all children except pop-ups.
  const nsIFrame::ChildListIDs skip(nsIFrame::kPopupList |
                                    nsIFrame::kSelectPopupList);
  for (nsIFrame::ChildListIterator childLists(aFrame);
       !childLists.IsDone(); childLists.Next()) {
    if (skip.Contains(childLists.CurrentID())) {
      continue;
    }

    nsFrameList children = childLists.CurrentList();
    for (nsFrameList::Enumerator e(children); !e.AtEnd(); e.Next()) {
      nsIFrame* child = e.get();
      nsOverflowAreas childOverflow =
        child->GetOverflowAreas() + child->GetPosition();
      aOverflowAreas.UnionWith(childOverflow);
    }
  }
}

static void DestroyViewID(void* aObject, nsIAtom* aPropertyName,
                          void* aPropertyValue, void* aData)
{
  ViewID* id = static_cast<ViewID*>(aPropertyValue);
  GetContentMap().Remove(*id);
  delete id;
}

/**
 * A namespace class for static layout utilities.
 */

ViewID
nsLayoutUtils::FindIDFor(nsIContent* aContent)
{
  ViewID scrollId;

  void* scrollIdProperty = aContent->GetProperty(nsGkAtoms::RemoteId);
  if (scrollIdProperty) {
    scrollId = *static_cast<ViewID*>(scrollIdProperty);
  } else {
    scrollId = sScrollIdCounter++;
    aContent->SetProperty(nsGkAtoms::RemoteId, new ViewID(scrollId),
                          DestroyViewID);
    GetContentMap().Put(scrollId, aContent);
  }

  return scrollId;
}

nsIContent*
nsLayoutUtils::FindContentFor(ViewID aId)
{
  NS_ABORT_IF_FALSE(aId != FrameMetrics::NULL_SCROLL_ID &&
                    aId != FrameMetrics::ROOT_SCROLL_ID,
                    "Cannot find a content element in map for null or root IDs.");
  nsIContent* content;
  bool exists = GetContentMap().Get(aId, &content);

  if (exists) {
    return content;
  } else {
    return nullptr;
  }
}

bool
nsLayoutUtils::GetDisplayPort(nsIContent* aContent, nsRect *aResult)
{
  void* property = aContent->GetProperty(nsGkAtoms::DisplayPort);
  if (!property) {
    return false;
  }

  if (aResult) {
    *aResult = *static_cast<nsRect*>(property);
  }
  return true;
}

bool
nsLayoutUtils::GetCriticalDisplayPort(nsIContent* aContent, nsRect* aResult)
{
  void* property = aContent->GetProperty(nsGkAtoms::CriticalDisplayPort);
  if (!property) {
    return false;
  }

  if (aResult) {
    *aResult = *static_cast<nsRect*>(property);
  }
  return true;
}

nsIFrame*
nsLayoutUtils::GetLastContinuationWithChild(nsIFrame* aFrame)
{
  NS_PRECONDITION(aFrame, "NULL frame pointer");
  aFrame = aFrame->GetLastContinuation();
  while (!aFrame->GetFirstPrincipalChild() &&
         aFrame->GetPrevContinuation()) {
    aFrame = aFrame->GetPrevContinuation();
  }
  return aFrame;
}

/**
 * GetFirstChildFrame returns the first "real" child frame of a
 * given frame.  It will descend down into pseudo-frames (unless the
 * pseudo-frame is the :before generated frame).
 * @param aFrame the frame
 * @param aFrame the frame's content node
 */
static nsIFrame*
GetFirstChildFrame(nsIFrame*       aFrame,
                   nsIContent*     aContent)
{
  NS_PRECONDITION(aFrame, "NULL frame pointer");

  // Get the first child frame
  nsIFrame* childFrame = aFrame->GetFirstPrincipalChild();

  // If the child frame is a pseudo-frame, then return its first child.
  // Note that the frame we create for the generated content is also a
  // pseudo-frame and so don't drill down in that case
  if (childFrame &&
      childFrame->IsPseudoFrame(aContent) &&
      !childFrame->IsGeneratedContentFrame()) {
    return GetFirstChildFrame(childFrame, aContent);
  }

  return childFrame;
}

/**
 * GetLastChildFrame returns the last "real" child frame of a
 * given frame.  It will descend down into pseudo-frames (unless the
 * pseudo-frame is the :after generated frame).
 * @param aFrame the frame
 * @param aFrame the frame's content node
 */
static nsIFrame*
GetLastChildFrame(nsIFrame*       aFrame,
                  nsIContent*     aContent)
{
  NS_PRECONDITION(aFrame, "NULL frame pointer");

  // Get the last continuation frame that's a parent
  nsIFrame* lastParentContinuation =
    nsLayoutUtils::GetLastContinuationWithChild(aFrame);
  nsIFrame* lastChildFrame =
    lastParentContinuation->GetLastChild(nsIFrame::kPrincipalList);
  if (lastChildFrame) {
    // Get the frame's first continuation. This matters in case the frame has
    // been continued across multiple lines or split by BiDi resolution.
    lastChildFrame = lastChildFrame->GetFirstContinuation();

    // If the last child frame is a pseudo-frame, then return its last child.
    // Note that the frame we create for the generated content is also a
    // pseudo-frame and so don't drill down in that case
    if (lastChildFrame &&
        lastChildFrame->IsPseudoFrame(aContent) &&
        !lastChildFrame->IsGeneratedContentFrame()) {
      return GetLastChildFrame(lastChildFrame, aContent);
    }

    return lastChildFrame;
  }

  return nullptr;
}

//static
nsIFrame::ChildListID
nsLayoutUtils::GetChildListNameFor(nsIFrame* aChildFrame)
{
  nsIFrame::ChildListID id = nsIFrame::kPrincipalList;

  if (aChildFrame->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER) {
    nsIFrame* pif = aChildFrame->GetPrevInFlow();
    if (pif->GetParent() == aChildFrame->GetParent()) {
      id = nsIFrame::kExcessOverflowContainersList;
    }
    else {
      id = nsIFrame::kOverflowContainersList;
    }
  }
  // See if the frame is moved out of the flow
  else if (aChildFrame->GetStateBits() & NS_FRAME_OUT_OF_FLOW) {
    // Look at the style information to tell
    const nsStyleDisplay* disp = aChildFrame->GetStyleDisplay();

    if (NS_STYLE_POSITION_ABSOLUTE == disp->mPosition) {
      id = nsIFrame::kAbsoluteList;
    } else if (NS_STYLE_POSITION_FIXED == disp->mPosition) {
      if (nsLayoutUtils::IsReallyFixedPos(aChildFrame)) {
        id = nsIFrame::kFixedList;
      } else {
        id = nsIFrame::kAbsoluteList;
      }
#ifdef MOZ_XUL
    } else if (NS_STYLE_DISPLAY_POPUP == disp->mDisplay) {
      // Out-of-flows that are DISPLAY_POPUP must be kids of the root popup set
#ifdef DEBUG
      nsIFrame* parent = aChildFrame->GetParent();
      NS_ASSERTION(parent && parent->GetType() == nsGkAtoms::popupSetFrame,
                   "Unexpected parent");
#endif // DEBUG

      // XXX FIXME: Bug 350740
      // Return here, because the postcondition for this function actually
      // fails for this case, since the popups are not in a "real" frame list
      // in the popup set.
      return nsIFrame::kPopupList;
#endif // MOZ_XUL
    } else {
      NS_ASSERTION(aChildFrame->IsFloating(),
                   "not a floated frame");
      id = nsIFrame::kFloatList;
    }

  } else {
    nsIAtom* childType = aChildFrame->GetType();
    if (nsGkAtoms::menuPopupFrame == childType) {
      nsIFrame* parent = aChildFrame->GetParent();
      nsIFrame* firstPopup = (parent)
                             ? parent->GetFirstChild(nsIFrame::kPopupList)
                             : nullptr;
      NS_ASSERTION(!firstPopup || !firstPopup->GetNextSibling(),
                   "We assume popupList only has one child, but it has more.");
      id = firstPopup == aChildFrame
             ? nsIFrame::kPopupList
             : nsIFrame::kPrincipalList;
    } else if (nsGkAtoms::tableColGroupFrame == childType) {
      id = nsIFrame::kColGroupList;
    } else if (nsGkAtoms::tableCaptionFrame == aChildFrame->GetType()) {
      id = nsIFrame::kCaptionList;
    } else {
      id = nsIFrame::kPrincipalList;
    }
  }

#ifdef DEBUG
  // Verify that the frame is actually in that child list or in the
  // corresponding overflow list.
  nsIFrame* parent = aChildFrame->GetParent();
  bool found = parent->GetChildList(id).ContainsFrame(aChildFrame);
  if (!found) {
    if (!(aChildFrame->GetStateBits() & NS_FRAME_OUT_OF_FLOW)) {
      found = parent->GetChildList(nsIFrame::kOverflowList)
                .ContainsFrame(aChildFrame);
    }
    else if (aChildFrame->IsFloating()) {
      found = parent->GetChildList(nsIFrame::kOverflowOutOfFlowList)
                .ContainsFrame(aChildFrame);
    }
    // else it's positioned and should have been on the 'id' child list.
    NS_POSTCONDITION(found, "not in child list");
  }
#endif

  return id;
}

// static
nsIFrame*
nsLayoutUtils::GetBeforeFrame(nsIFrame* aFrame)
{
  NS_PRECONDITION(aFrame, "NULL frame pointer");
  NS_ASSERTION(!aFrame->GetPrevContinuation(),
               "aFrame must be first continuation");

  nsIFrame* firstFrame = GetFirstChildFrame(aFrame, aFrame->GetContent());

  if (firstFrame && IsGeneratedContentFor(nullptr, firstFrame,
                                          nsCSSPseudoElements::before)) {
    return firstFrame;
  }

  return nullptr;
}

// static
nsIFrame*
nsLayoutUtils::GetAfterFrame(nsIFrame* aFrame)
{
  NS_PRECONDITION(aFrame, "NULL frame pointer");

  nsIFrame* lastFrame = GetLastChildFrame(aFrame, aFrame->GetContent());

  if (lastFrame && IsGeneratedContentFor(nullptr, lastFrame,
                                         nsCSSPseudoElements::after)) {
    return lastFrame;
  }

  return nullptr;
}

// static
nsIFrame*
nsLayoutUtils::GetClosestFrameOfType(nsIFrame* aFrame, nsIAtom* aFrameType)
{
  for (nsIFrame* frame = aFrame; frame; frame = frame->GetParent()) {
    if (frame->GetType() == aFrameType) {
      return frame;
    }
  }
  return nullptr;
}

// static
nsIFrame*
nsLayoutUtils::GetStyleFrame(nsIFrame* aFrame)
{
  if (aFrame->GetType() == nsGkAtoms::tableOuterFrame) {
    nsIFrame* inner = aFrame->GetFirstPrincipalChild();
    NS_ASSERTION(inner, "Outer table must have an inner");
    return inner;
  }

  return aFrame;
}

nsIFrame*
nsLayoutUtils::GetFloatFromPlaceholder(nsIFrame* aFrame) {
  NS_ASSERTION(nsGkAtoms::placeholderFrame == aFrame->GetType(),
               "Must have a placeholder here");
  if (aFrame->GetStateBits() & PLACEHOLDER_FOR_FLOAT) {
    nsIFrame *outOfFlowFrame =
      nsPlaceholderFrame::GetRealFrameForPlaceholder(aFrame);
    NS_ASSERTION(outOfFlowFrame->IsFloating(),
                 "How did that happen?");
    return outOfFlowFrame;
  }

  return nullptr;
}

// static
bool
nsLayoutUtils::IsGeneratedContentFor(nsIContent* aContent,
                                     nsIFrame* aFrame,
                                     nsIAtom* aPseudoElement)
{
  NS_PRECONDITION(aFrame, "Must have a frame");
  NS_PRECONDITION(aPseudoElement, "Must have a pseudo name");

  if (!aFrame->IsGeneratedContentFrame()) {
    return false;
  }
  nsIFrame* parent = aFrame->GetParent();
  NS_ASSERTION(parent, "Generated content can't be root frame");
  if (parent->IsGeneratedContentFrame()) {
    // Not the root of the generated content
    return false;
  }

  if (aContent && parent->GetContent() != aContent) {
    return false;
  }

  return (aFrame->GetContent()->Tag() == nsGkAtoms::mozgeneratedcontentbefore) ==
    (aPseudoElement == nsCSSPseudoElements::before);
}

// static
nsIFrame*
nsLayoutUtils::GetCrossDocParentFrame(const nsIFrame* aFrame,
                                      nsPoint* aExtraOffset)
{
  nsIFrame* p = aFrame->GetParent();
  if (p)
    return p;

  nsIView* v = aFrame->GetView();
  if (!v)
    return nullptr;
  v = v->GetParent(); // anonymous inner view
  if (!v)
    return nullptr;
  if (aExtraOffset) {
    *aExtraOffset += v->GetPosition();
  }
  v = v->GetParent(); // subdocumentframe's view
  return v ? v->GetFrame() : nullptr;
}

// static
bool
nsLayoutUtils::IsProperAncestorFrameCrossDoc(nsIFrame* aAncestorFrame, nsIFrame* aFrame,
                                             nsIFrame* aCommonAncestor)
{
  if (aFrame == aAncestorFrame)
    return false;
  return IsAncestorFrameCrossDoc(aAncestorFrame, aFrame, aCommonAncestor);
}

// static
bool
nsLayoutUtils::IsAncestorFrameCrossDoc(const nsIFrame* aAncestorFrame, const nsIFrame* aFrame,
                                       const nsIFrame* aCommonAncestor)
{
  for (const nsIFrame* f = aFrame; f != aCommonAncestor;
       f = GetCrossDocParentFrame(f)) {
    if (f == aAncestorFrame)
      return true;
  }
  return aCommonAncestor == aAncestorFrame;
}

// static
bool
nsLayoutUtils::IsProperAncestorFrame(nsIFrame* aAncestorFrame, nsIFrame* aFrame,
                                     nsIFrame* aCommonAncestor)
{
  if (aFrame == aAncestorFrame)
    return false;
  for (nsIFrame* f = aFrame; f != aCommonAncestor; f = f->GetParent()) {
    if (f == aAncestorFrame)
      return true;
  }
  return aCommonAncestor == aAncestorFrame;
}

// static
int32_t
nsLayoutUtils::DoCompareTreePosition(nsIContent* aContent1,
                                     nsIContent* aContent2,
                                     int32_t aIf1Ancestor,
                                     int32_t aIf2Ancestor,
                                     const nsIContent* aCommonAncestor)
{
  NS_PRECONDITION(aContent1, "aContent1 must not be null");
  NS_PRECONDITION(aContent2, "aContent2 must not be null");

  nsAutoTArray<nsINode*, 32> content1Ancestors;
  nsINode* c1;
  for (c1 = aContent1; c1 && c1 != aCommonAncestor; c1 = c1->GetParentNode()) {
    content1Ancestors.AppendElement(c1);
  }
  if (!c1 && aCommonAncestor) {
    // So, it turns out aCommonAncestor was not an ancestor of c1. Oops.
    // Never mind. We can continue as if aCommonAncestor was null.
    aCommonAncestor = nullptr;
  }

  nsAutoTArray<nsINode*, 32> content2Ancestors;
  nsINode* c2;
  for (c2 = aContent2; c2 && c2 != aCommonAncestor; c2 = c2->GetParentNode()) {
    content2Ancestors.AppendElement(c2);
  }
  if (!c2 && aCommonAncestor) {
    // So, it turns out aCommonAncestor was not an ancestor of c2.
    // We need to retry with no common ancestor hint.
    return DoCompareTreePosition(aContent1, aContent2,
                                 aIf1Ancestor, aIf2Ancestor, nullptr);
  }

  int last1 = content1Ancestors.Length() - 1;
  int last2 = content2Ancestors.Length() - 1;
  nsINode* content1Ancestor = nullptr;
  nsINode* content2Ancestor = nullptr;
  while (last1 >= 0 && last2 >= 0
         && ((content1Ancestor = content1Ancestors.ElementAt(last1)) ==
             (content2Ancestor = content2Ancestors.ElementAt(last2)))) {
    last1--;
    last2--;
  }

  if (last1 < 0) {
    if (last2 < 0) {
      NS_ASSERTION(aContent1 == aContent2, "internal error?");
      return 0;
    }
    // aContent1 is an ancestor of aContent2
    return aIf1Ancestor;
  }

  if (last2 < 0) {
    // aContent2 is an ancestor of aContent1
    return aIf2Ancestor;
  }

  // content1Ancestor != content2Ancestor, so they must be siblings with the same parent
  nsINode* parent = content1Ancestor->GetParentNode();
#ifdef DEBUG
  // TODO: remove the uglyness, see bug 598468.
  NS_ASSERTION(gPreventAssertInCompareTreePosition || parent,
               "no common ancestor at all???");
#endif // DEBUG
  if (!parent) { // different documents??
    return 0;
  }

  int32_t index1 = parent->IndexOf(content1Ancestor);
  int32_t index2 = parent->IndexOf(content2Ancestor);
  if (index1 < 0 || index2 < 0) {
    // one of them must be anonymous; we can't determine the order
    return 0;
  }

  return index1 - index2;
}

static nsIFrame* FillAncestors(nsIFrame* aFrame,
                               nsIFrame* aStopAtAncestor,
                               nsTArray<nsIFrame*>* aAncestors)
{
  while (aFrame && aFrame != aStopAtAncestor) {
    aAncestors->AppendElement(aFrame);
    aFrame = nsLayoutUtils::GetParentOrPlaceholderFor(aFrame);
  }
  return aFrame;
}

// Return true if aFrame1 is after aFrame2
static bool IsFrameAfter(nsIFrame* aFrame1, nsIFrame* aFrame2)
{
  nsIFrame* f = aFrame2;
  do {
    f = f->GetNextSibling();
    if (f == aFrame1)
      return true;
  } while (f);
  return false;
}

// static
int32_t
nsLayoutUtils::DoCompareTreePosition(nsIFrame* aFrame1,
                                     nsIFrame* aFrame2,
                                     int32_t aIf1Ancestor,
                                     int32_t aIf2Ancestor,
                                     nsIFrame* aCommonAncestor)
{
  NS_PRECONDITION(aFrame1, "aFrame1 must not be null");
  NS_PRECONDITION(aFrame2, "aFrame2 must not be null");

  nsPresContext* presContext = aFrame1->PresContext();
  if (presContext != aFrame2->PresContext()) {
    NS_ERROR("no common ancestor at all, different documents");
    return 0;
  }

  nsAutoTArray<nsIFrame*,20> frame1Ancestors;
  if (!FillAncestors(aFrame1, aCommonAncestor, &frame1Ancestors)) {
    // We reached the root of the frame tree ... if aCommonAncestor was set,
    // it is wrong
    aCommonAncestor = nullptr;
  }

  nsAutoTArray<nsIFrame*,20> frame2Ancestors;
  if (!FillAncestors(aFrame2, aCommonAncestor, &frame2Ancestors) &&
      aCommonAncestor) {
    // We reached the root of the frame tree ... aCommonAncestor was wrong.
    // Try again with no hint.
    return DoCompareTreePosition(aFrame1, aFrame2,
                                 aIf1Ancestor, aIf2Ancestor, nullptr);
  }

  int32_t last1 = int32_t(frame1Ancestors.Length()) - 1;
  int32_t last2 = int32_t(frame2Ancestors.Length()) - 1;
  while (last1 >= 0 && last2 >= 0 &&
         frame1Ancestors[last1] == frame2Ancestors[last2]) {
    last1--;
    last2--;
  }

  if (last1 < 0) {
    if (last2 < 0) {
      NS_ASSERTION(aFrame1 == aFrame2, "internal error?");
      return 0;
    }
    // aFrame1 is an ancestor of aFrame2
    return aIf1Ancestor;
  }

  if (last2 < 0) {
    // aFrame2 is an ancestor of aFrame1
    return aIf2Ancestor;
  }

  nsIFrame* ancestor1 = frame1Ancestors[last1];
  nsIFrame* ancestor2 = frame2Ancestors[last2];
  // Now we should be able to walk sibling chains to find which one is first
  if (IsFrameAfter(ancestor2, ancestor1))
    return -1;
  if (IsFrameAfter(ancestor1, ancestor2))
    return 1;
  NS_WARNING("Frames were in different child lists???");
  return 0;
}

// static
nsIFrame* nsLayoutUtils::GetLastSibling(nsIFrame* aFrame) {
  if (!aFrame) {
    return nullptr;
  }

  nsIFrame* next;
  while ((next = aFrame->GetNextSibling()) != nullptr) {
    aFrame = next;
  }
  return aFrame;
}

// static
nsIView*
nsLayoutUtils::FindSiblingViewFor(nsIView* aParentView, nsIFrame* aFrame) {
  nsIFrame* parentViewFrame = aParentView->GetFrame();
  nsIContent* parentViewContent = parentViewFrame ? parentViewFrame->GetContent() : nullptr;
  for (nsIView* insertBefore = aParentView->GetFirstChild(); insertBefore;
       insertBefore = insertBefore->GetNextSibling()) {
    nsIFrame* f = insertBefore->GetFrame();
    if (!f) {
      // this view could be some anonymous view attached to a meaningful parent
      for (nsIView* searchView = insertBefore->GetParent(); searchView;
           searchView = searchView->GetParent()) {
        f = searchView->GetFrame();
        if (f) {
          break;
        }
      }
      NS_ASSERTION(f, "Can't find a frame anywhere!");
    }
    if (!f || !aFrame->GetContent() || !f->GetContent() ||
        CompareTreePosition(aFrame->GetContent(), f->GetContent(), parentViewContent) > 0) {
      // aFrame's content is after f's content (or we just don't know),
      // so put our view before f's view
      return insertBefore;
    }
  }
  return nullptr;
}

//static
nsIScrollableFrame*
nsLayoutUtils::GetScrollableFrameFor(const nsIFrame *aScrolledFrame)
{
  nsIFrame *frame = aScrolledFrame->GetParent();
  if (!frame) {
    return nullptr;
  }
  nsIScrollableFrame *sf = do_QueryFrame(frame);
  return sf;
}

nsIFrame*
nsLayoutUtils::GetActiveScrolledRootFor(nsIFrame* aFrame,
                                        const nsIFrame* aStopAtAncestor)
{
  nsIFrame* f = aFrame;
  while (f != aStopAtAncestor) {
    if (IsPopup(f))
      break;
    nsIFrame* parent = GetCrossDocParentFrame(f);
    if (!parent)
      break;
    nsIScrollableFrame* sf = do_QueryFrame(parent);
    if (sf && sf->IsScrollingActive() && sf->GetScrolledFrame() == f)
      break;
    f = parent;
  }
  return f;
}

nsIFrame*
nsLayoutUtils::GetActiveScrolledRootFor(nsDisplayItem* aItem,
                                        nsDisplayListBuilder* aBuilder,
                                        bool* aShouldFixToViewport)
{
  nsIFrame* f = aItem->GetUnderlyingFrame();
  if (aShouldFixToViewport) {
    *aShouldFixToViewport = false;
  }
  if (!f) {
    return nullptr;
  }
  if (aItem->ShouldFixToViewport(aBuilder)) {
    if (aShouldFixToViewport) {
      *aShouldFixToViewport = true;
    }
    // Make its active scrolled root be the active scrolled root of
    // the enclosing viewport, since it shouldn't be scrolled by scrolled
    // frames in its document. InvalidateFixedBackgroundFramesFromList in
    // nsGfxScrollFrame will not repaint this item when scrolling occurs.
    nsIFrame* viewportFrame =
      nsLayoutUtils::GetClosestFrameOfType(f, nsGkAtoms::viewportFrame);
    NS_ASSERTION(viewportFrame, "no viewport???");
    return nsLayoutUtils::GetActiveScrolledRootFor(viewportFrame, aBuilder->FindReferenceFrameFor(viewportFrame));
  } else {
    return nsLayoutUtils::GetActiveScrolledRootFor(f, aItem->ReferenceFrame());
  }
}

bool
nsLayoutUtils::IsScrolledByRootContentDocumentDisplayportScrolling(const nsIFrame* aActiveScrolledRoot,
                                                                   nsDisplayListBuilder* aBuilder)
{
  nsPresContext* presContext = aActiveScrolledRoot->PresContext()->
          GetToplevelContentDocumentPresContext();
  if (!presContext)
    return false;

  nsIFrame* rootScrollFrame = presContext->GetPresShell()->GetRootScrollFrame();
  if (!rootScrollFrame || !nsLayoutUtils::GetDisplayPort(rootScrollFrame->GetContent(), nullptr))
    return false;
  return nsLayoutUtils::IsAncestorFrameCrossDoc(rootScrollFrame, aActiveScrolledRoot);
}

// static
nsIScrollableFrame*
nsLayoutUtils::GetNearestScrollableFrameForDirection(nsIFrame* aFrame,
                                                     Direction aDirection)
{
  NS_ASSERTION(aFrame, "GetNearestScrollableFrameForDirection expects a non-null frame");
  for (nsIFrame* f = aFrame; f; f = nsLayoutUtils::GetCrossDocParentFrame(f)) {
    nsIScrollableFrame* scrollableFrame = do_QueryFrame(f);
    if (scrollableFrame) {
      nsPresContext::ScrollbarStyles ss = scrollableFrame->GetScrollbarStyles();
      uint32_t directions = scrollableFrame->GetPerceivedScrollingDirections();
      if (aDirection == eVertical ?
          (ss.mVertical != NS_STYLE_OVERFLOW_HIDDEN &&
           (directions & nsIScrollableFrame::VERTICAL)) :
          (ss.mHorizontal != NS_STYLE_OVERFLOW_HIDDEN &&
           (directions & nsIScrollableFrame::HORIZONTAL)))
        return scrollableFrame;
    }
  }
  return nullptr;
}

// static
nsIScrollableFrame*
nsLayoutUtils::GetNearestScrollableFrame(nsIFrame* aFrame)
{
  NS_ASSERTION(aFrame, "GetNearestScrollableFrame expects a non-null frame");
  for (nsIFrame* f = aFrame; f; f = nsLayoutUtils::GetCrossDocParentFrame(f)) {
    nsIScrollableFrame* scrollableFrame = do_QueryFrame(f);
    if (scrollableFrame) {
      nsPresContext::ScrollbarStyles ss = scrollableFrame->GetScrollbarStyles();
      if (ss.mVertical != NS_STYLE_OVERFLOW_HIDDEN ||
          ss.mHorizontal != NS_STYLE_OVERFLOW_HIDDEN)
        return scrollableFrame;
    }
  }
  return nullptr;
}

//static
bool
nsLayoutUtils::HasPseudoStyle(nsIContent* aContent,
                              nsStyleContext* aStyleContext,
                              nsCSSPseudoElements::Type aPseudoElement,
                              nsPresContext* aPresContext)
{
  NS_PRECONDITION(aPresContext, "Must have a prescontext");

  nsRefPtr<nsStyleContext> pseudoContext;
  if (aContent) {
    pseudoContext = aPresContext->StyleSet()->
      ProbePseudoElementStyle(aContent->AsElement(), aPseudoElement,
                              aStyleContext);
  }
  return pseudoContext != nullptr;
}

nsPoint
nsLayoutUtils::GetDOMEventCoordinatesRelativeTo(nsIDOMEvent* aDOMEvent, nsIFrame* aFrame)
{
  if (!aDOMEvent)
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  nsEvent *event = aDOMEvent->GetInternalNSEvent();
  if (!event)
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  return GetEventCoordinatesRelativeTo(event, aFrame);
}

nsPoint
nsLayoutUtils::GetEventCoordinatesRelativeTo(const nsEvent* aEvent, nsIFrame* aFrame)
{
  if (!aEvent || (aEvent->eventStructType != NS_MOUSE_EVENT &&
                  aEvent->eventStructType != NS_MOUSE_SCROLL_EVENT &&
                  aEvent->eventStructType != NS_WHEEL_EVENT &&
                  aEvent->eventStructType != NS_DRAG_EVENT &&
                  aEvent->eventStructType != NS_SIMPLE_GESTURE_EVENT &&
                  aEvent->eventStructType != NS_GESTURENOTIFY_EVENT &&
                  aEvent->eventStructType != NS_TOUCH_EVENT &&
                  aEvent->eventStructType != NS_QUERY_CONTENT_EVENT))
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);

  const nsGUIEvent* GUIEvent = static_cast<const nsGUIEvent*>(aEvent);
  return GetEventCoordinatesRelativeTo(aEvent,
                                       GUIEvent->refPoint,
                                       aFrame);
}

nsPoint
nsLayoutUtils::GetEventCoordinatesRelativeTo(const nsEvent* aEvent,
                                             const nsIntPoint aPoint,
                                             nsIFrame* aFrame)
{
  if (!aFrame) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  const nsGUIEvent* GUIEvent = static_cast<const nsGUIEvent*>(aEvent);
  nsIWidget* widget = GUIEvent->widget;
  if (!widget) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  return GetEventCoordinatesRelativeTo(widget, aPoint, aFrame);
}

nsPoint
nsLayoutUtils::GetEventCoordinatesRelativeTo(nsIWidget* aWidget,
                                             const nsIntPoint aPoint,
                                             nsIFrame* aFrame)
{
  if (!aFrame || !aWidget) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  nsIView* view = aFrame->GetView();
  if (view) {
    nsIWidget* frameWidget = view->GetWidget();
    if (frameWidget && frameWidget == aWidget) {
      // Special case this cause it happens a lot.
      // This also fixes bug 664707, events in the extra-special case of select
      // dropdown popups that are transformed.
      nsPresContext* presContext = aFrame->PresContext();
      nsPoint pt(presContext->DevPixelsToAppUnits(aPoint.x),
                 presContext->DevPixelsToAppUnits(aPoint.y));
      return pt - view->ViewToWidgetOffset();
    }
  }

  /* If we walk up the frame tree and discover that any of the frames are
   * transformed, we need to do extra work to convert from the global
   * space to the local space.
   */
  nsIFrame* rootFrame = aFrame;
  bool transformFound = false;
  for (nsIFrame* f = aFrame; f; f = GetCrossDocParentFrame(f)) {
    if (f->IsTransformed()) {
      transformFound = true;
    }

    rootFrame = f;
  }

  nsIView* rootView = rootFrame->GetView();
  if (!rootView) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  nsPoint widgetToView = TranslateWidgetToView(rootFrame->PresContext(),
                                               aWidget, aPoint, rootView);

  if (widgetToView == nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE)) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  // Convert from root document app units to app units of the document aFrame
  // is in.
  int32_t rootAPD = rootFrame->PresContext()->AppUnitsPerDevPixel();
  int32_t localAPD = aFrame->PresContext()->AppUnitsPerDevPixel();
  widgetToView = widgetToView.ConvertAppUnits(rootAPD, localAPD);

  /* If we encountered a transform, we can't do simple arithmetic to figure
   * out how to convert back to aFrame's coordinates and must use the CTM.
   */
  if (transformFound) {
    return TransformRootPointToFrame(aFrame, widgetToView);
  }

  /* Otherwise, all coordinate systems are translations of one another,
   * so we can just subtract out the different.
   */
  nsPoint offset = aFrame->GetOffsetToCrossDoc(rootFrame);
  return widgetToView - offset;
}

nsIFrame*
nsLayoutUtils::GetPopupFrameForEventCoordinates(nsPresContext* aPresContext,
                                                const nsEvent* aEvent)
{
#ifdef MOZ_XUL
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (!pm) {
    return nullptr;
  }
  nsTArray<nsIFrame*> popups;
  pm->GetVisiblePopups(popups);
  uint32_t i;
  // Search from top to bottom
  for (i = 0; i < popups.Length(); i++) {
    nsIFrame* popup = popups[i];
    if (popup->PresContext()->GetRootPresContext() == aPresContext &&
        popup->GetScrollableOverflowRect().Contains(
          GetEventCoordinatesRelativeTo(aEvent, popup))) {
      return popup;
    }
  }
#endif
  return nullptr;
}

gfx3DMatrix
nsLayoutUtils::ChangeMatrixBasis(const gfxPoint3D &aOrigin,
                                 const gfx3DMatrix &aMatrix)
{
  gfx3DMatrix result = aMatrix;

  /* Translate to the origin before aMatrix */
  result.Translate(-aOrigin);

  /* Translate back into position after aMatrix */
  result.TranslatePost(aOrigin);

  return result; 
}

/**
 * Given a gfxFloat, constrains its value to be between nscoord_MIN and nscoord_MAX.
 *
 * @param aVal The value to constrain (in/out)
 */
static void ConstrainToCoordValues(gfxFloat &aVal)
{
  if (aVal <= nscoord_MIN)
    aVal = nscoord_MIN;
  else if (aVal >= nscoord_MAX)
    aVal = nscoord_MAX;
}

nsRect
nsLayoutUtils::RoundGfxRectToAppRect(const gfxRect &aRect, float aFactor)
{
  /* Get a new gfxRect whose units are app units by scaling by the specified factor. */
  gfxRect scaledRect = aRect;
  scaledRect.ScaleRoundOut(aFactor);

  /* We now need to constrain our results to the max and min values for coords. */
  ConstrainToCoordValues(scaledRect.x);
  ConstrainToCoordValues(scaledRect.y);
  ConstrainToCoordValues(scaledRect.width);
  ConstrainToCoordValues(scaledRect.height);

  /* Now typecast everything back.  This is guaranteed to be safe. */
  return nsRect(nscoord(scaledRect.X()), nscoord(scaledRect.Y()),
                nscoord(scaledRect.Width()), nscoord(scaledRect.Height()));
}


nsRegion
nsLayoutUtils::RoundedRectIntersectRect(const nsRect& aRoundedRect,
                                        const nscoord aRadii[8],
                                        const nsRect& aContainedRect)
{
  // rectFullHeight and rectFullWidth together will approximately contain
  // the total area of the frame minus the rounded corners.
  nsRect rectFullHeight = aRoundedRect;
  nscoord xDiff = NS_MAX(aRadii[NS_CORNER_TOP_LEFT_X], aRadii[NS_CORNER_BOTTOM_LEFT_X]);
  rectFullHeight.x += xDiff;
  rectFullHeight.width -= NS_MAX(aRadii[NS_CORNER_TOP_RIGHT_X],
                                 aRadii[NS_CORNER_BOTTOM_RIGHT_X]) + xDiff;
  nsRect r1;
  r1.IntersectRect(rectFullHeight, aContainedRect);

  nsRect rectFullWidth = aRoundedRect;
  nscoord yDiff = NS_MAX(aRadii[NS_CORNER_TOP_LEFT_Y], aRadii[NS_CORNER_TOP_RIGHT_Y]);
  rectFullWidth.y += yDiff;
  rectFullWidth.height -= NS_MAX(aRadii[NS_CORNER_BOTTOM_LEFT_Y],
                                 aRadii[NS_CORNER_BOTTOM_RIGHT_Y]) + yDiff;
  nsRect r2;
  r2.IntersectRect(rectFullWidth, aContainedRect);

  nsRegion result;
  result.Or(r1, r2);
  return result;
}

nsRect
nsLayoutUtils::MatrixTransformRectOut(const nsRect &aBounds,
                                      const gfx3DMatrix &aMatrix, float aFactor)
{
  nsRect outside = aBounds;
  outside.ScaleRoundOut(1/aFactor);
  gfxRect image = aMatrix.TransformBounds(gfxRect(outside.x,
                                                  outside.y,
                                                  outside.width,
                                                  outside.height));
  return RoundGfxRectToAppRect(image, aFactor);
}

nsRect
nsLayoutUtils::MatrixTransformRect(const nsRect &aBounds,
                                   const gfx3DMatrix &aMatrix, float aFactor)
{
  gfxRect image = aMatrix.TransformBounds(gfxRect(NSAppUnitsToDoublePixels(aBounds.x, aFactor),
                                                  NSAppUnitsToDoublePixels(aBounds.y, aFactor),
                                                  NSAppUnitsToDoublePixels(aBounds.width, aFactor),
                                                  NSAppUnitsToDoublePixels(aBounds.height, aFactor)));

  return RoundGfxRectToAppRect(image, aFactor);
}

nsPoint
nsLayoutUtils::MatrixTransformPoint(const nsPoint &aPoint,
                                    const gfx3DMatrix &aMatrix, float aFactor)
{
  gfxPoint image = aMatrix.Transform(gfxPoint(NSAppUnitsToFloatPixels(aPoint.x, aFactor),
                                              NSAppUnitsToFloatPixels(aPoint.y, aFactor)));
  return nsPoint(NSFloatPixelsToAppUnits(float(image.x), aFactor),
                 NSFloatPixelsToAppUnits(float(image.y), aFactor));
}

gfx3DMatrix
nsLayoutUtils::GetTransformToAncestor(nsIFrame *aFrame, const nsIFrame *aAncestor)
{
  nsIFrame* parent;
  gfx3DMatrix ctm;
  if (aFrame == aAncestor) {
    return ctm;
  }
  ctm = aFrame->GetTransformMatrix(aAncestor, &parent);
  while (parent && parent != aAncestor) {
    if (!parent->Preserves3DChildren()) {
      ctm.ProjectTo2D();
    }
    ctm = ctm * parent->GetTransformMatrix(aAncestor, &parent);
  }
  return ctm;
}

bool
nsLayoutUtils::GetLayerTransformForFrame(nsIFrame* aFrame,
                                         gfx3DMatrix* aTransform)
{
  // FIXME/bug 796690: we can sometimes compute a transform in these
  // cases, it just increases complexity considerably.  Punt for now.
  if (aFrame->Preserves3DChildren() || aFrame->HasTransformGetter()) {
    return false;
  }

  nsIFrame* root = nsLayoutUtils::GetDisplayRootFrame(aFrame);
  if (root->HasAnyStateBits(NS_FRAME_UPDATE_LAYER_TREE)) {
    // Content may have been invalidated, so we can't reliably compute
    // the "layer transform" in general.
    return false;
  }
  // If the caller doesn't care about the value, early-return to skip
  // overhead below.
  if (!aTransform) {
    return true;
  }

  nsDisplayListBuilder builder(root, nsDisplayListBuilder::OTHER,
                               false/*don't build caret*/);
  nsDisplayList list;  
  nsDisplayTransform* item =
    new (&builder) nsDisplayTransform(&builder, aFrame, &list);

  *aTransform =
    item->GetTransform(aFrame->PresContext()->AppUnitsPerDevPixel());
  item->~nsDisplayTransform();

  return true;
}

static gfxPoint
TransformGfxPointFromAncestor(nsIFrame *aFrame,
                              const gfxPoint &aPoint,
                              nsIFrame *aAncestor)
{
  gfx3DMatrix ctm = nsLayoutUtils::GetTransformToAncestor(aFrame, aAncestor);
  return ctm.Inverse().ProjectPoint(aPoint);
}

static gfxRect
TransformGfxRectFromAncestor(nsIFrame *aFrame,
                             const gfxRect &aRect,
                             const nsIFrame *aAncestor)
{
  gfx3DMatrix ctm = nsLayoutUtils::GetTransformToAncestor(aFrame, aAncestor);
  return ctm.Inverse().ProjectRectBounds(aRect);
}

static gfxRect
TransformGfxRectToAncestor(nsIFrame *aFrame,
                           const gfxRect &aRect,
                           const nsIFrame *aAncestor)
{
  gfx3DMatrix ctm = nsLayoutUtils::GetTransformToAncestor(aFrame, aAncestor);
  return ctm.TransformBounds(aRect);
}

nsPoint
nsLayoutUtils::TransformRootPointToFrame(nsIFrame *aFrame,
                                         const nsPoint &aPoint)
{
    float factor = aFrame->PresContext()->AppUnitsPerDevPixel();
    gfxPoint result(NSAppUnitsToFloatPixels(aPoint.x, factor),
                    NSAppUnitsToFloatPixels(aPoint.y, factor));

    result = TransformGfxPointFromAncestor(aFrame, result, nullptr);

    return nsPoint(NSFloatPixelsToAppUnits(float(result.x), factor),
                   NSFloatPixelsToAppUnits(float(result.y), factor));
}

nsRect 
nsLayoutUtils::TransformAncestorRectToFrame(nsIFrame* aFrame,
                                            const nsRect &aRect,
                                            const nsIFrame* aAncestor)
{
    float srcAppUnitsPerDevPixel = aAncestor->PresContext()->AppUnitsPerDevPixel();
    gfxRect result(NSAppUnitsToFloatPixels(aRect.x, srcAppUnitsPerDevPixel),
                   NSAppUnitsToFloatPixels(aRect.y, srcAppUnitsPerDevPixel),
                   NSAppUnitsToFloatPixels(aRect.width, srcAppUnitsPerDevPixel),
                   NSAppUnitsToFloatPixels(aRect.height, srcAppUnitsPerDevPixel));

    result = TransformGfxRectFromAncestor(aFrame, result, aAncestor);

    float destAppUnitsPerDevPixel = aFrame->PresContext()->AppUnitsPerDevPixel();
    return nsRect(NSFloatPixelsToAppUnits(float(result.x), destAppUnitsPerDevPixel),
                  NSFloatPixelsToAppUnits(float(result.y), destAppUnitsPerDevPixel),
                  NSFloatPixelsToAppUnits(float(result.width), destAppUnitsPerDevPixel),
                  NSFloatPixelsToAppUnits(float(result.height), destAppUnitsPerDevPixel));
}

nsRect
nsLayoutUtils::TransformFrameRectToAncestor(nsIFrame* aFrame,
                                            const nsRect& aRect,
                                            const nsIFrame* aAncestor)
{
  float srcAppUnitsPerDevPixel = aFrame->PresContext()->AppUnitsPerDevPixel();
  gfxRect result(NSAppUnitsToFloatPixels(aRect.x, srcAppUnitsPerDevPixel),
                 NSAppUnitsToFloatPixels(aRect.y, srcAppUnitsPerDevPixel),
                 NSAppUnitsToFloatPixels(aRect.width, srcAppUnitsPerDevPixel),
                 NSAppUnitsToFloatPixels(aRect.height, srcAppUnitsPerDevPixel));

  result = TransformGfxRectToAncestor(aFrame, result, aAncestor);

  float destAppUnitsPerDevPixel = aAncestor->PresContext()->AppUnitsPerDevPixel();
  return nsRect(NSFloatPixelsToAppUnits(float(result.x), destAppUnitsPerDevPixel),
                NSFloatPixelsToAppUnits(float(result.y), destAppUnitsPerDevPixel),
                NSFloatPixelsToAppUnits(float(result.width), destAppUnitsPerDevPixel),
                NSFloatPixelsToAppUnits(float(result.height), destAppUnitsPerDevPixel));
}

static nsIntPoint GetWidgetOffset(nsIWidget* aWidget, nsIWidget*& aRootWidget) {
  nsIntPoint offset(0, 0);
  nsIWidget* parent = aWidget->GetParent();
  while (parent) {
    nsIntRect bounds;
    aWidget->GetBounds(bounds);
    offset += bounds.TopLeft();
    aWidget = parent;
    parent = aWidget->GetParent();
  }
  aRootWidget = aWidget;
  return offset;
}

nsPoint
nsLayoutUtils::TranslateWidgetToView(nsPresContext* aPresContext,
                                     nsIWidget* aWidget, nsIntPoint aPt,
                                     nsIView* aView)
{
  nsPoint viewOffset;
  nsIWidget* viewWidget = aView->GetNearestWidget(&viewOffset);
  if (!viewWidget) {
    return nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  }

  nsIWidget* fromRoot;
  nsIntPoint fromOffset = GetWidgetOffset(aWidget, fromRoot);
  nsIWidget* toRoot;
  nsIntPoint toOffset = GetWidgetOffset(viewWidget, toRoot);

  nsIntPoint widgetPoint;
  if (fromRoot == toRoot) {
    widgetPoint = aPt + fromOffset - toOffset;
  } else {
    nsIntPoint screenPoint = aWidget->WidgetToScreenOffset();
    widgetPoint = aPt + screenPoint - viewWidget->WidgetToScreenOffset();
  }

  nsPoint widgetAppUnits(aPresContext->DevPixelsToAppUnits(widgetPoint.x),
                         aPresContext->DevPixelsToAppUnits(widgetPoint.y));
  return widgetAppUnits - viewOffset;
}

// Combine aNewBreakType with aOrigBreakType, but limit the break types
// to NS_STYLE_CLEAR_LEFT, RIGHT, LEFT_AND_RIGHT.
uint8_t
nsLayoutUtils::CombineBreakType(uint8_t aOrigBreakType,
                                uint8_t aNewBreakType)
{
  uint8_t breakType = aOrigBreakType;
  switch(breakType) {
  case NS_STYLE_CLEAR_LEFT:
    if ((NS_STYLE_CLEAR_RIGHT          == aNewBreakType) ||
        (NS_STYLE_CLEAR_LEFT_AND_RIGHT == aNewBreakType)) {
      breakType = NS_STYLE_CLEAR_LEFT_AND_RIGHT;
    }
    break;
  case NS_STYLE_CLEAR_RIGHT:
    if ((NS_STYLE_CLEAR_LEFT           == aNewBreakType) ||
        (NS_STYLE_CLEAR_LEFT_AND_RIGHT == aNewBreakType)) {
      breakType = NS_STYLE_CLEAR_LEFT_AND_RIGHT;
    }
    break;
  case NS_STYLE_CLEAR_NONE:
    if ((NS_STYLE_CLEAR_LEFT           == aNewBreakType) ||
        (NS_STYLE_CLEAR_RIGHT          == aNewBreakType) ||
        (NS_STYLE_CLEAR_LEFT_AND_RIGHT == aNewBreakType)) {
      breakType = aNewBreakType;
    }
  }
  return breakType;
}

#ifdef MOZ_DUMP_PAINTING
#include <stdio.h>

static bool gDumpEventList = false;
int gPaintCount = 0;
#endif

nsresult
nsLayoutUtils::GetRemoteContentIds(nsIFrame* aFrame,
                                   const nsRect& aTarget,
                                   nsTArray<ViewID> &aOutIDs,
                                   bool aIgnoreRootScrollFrame)
{
  nsDisplayListBuilder builder(aFrame, nsDisplayListBuilder::EVENT_DELIVERY,
                               false);
  nsDisplayList list;

  if (aIgnoreRootScrollFrame) {
    nsIFrame* rootScrollFrame =
      aFrame->PresContext()->PresShell()->GetRootScrollFrame();
    if (rootScrollFrame) {
      builder.SetIgnoreScrollFrame(rootScrollFrame);
    }
  }

  builder.EnterPresShell(aFrame, aTarget);

  nsresult rv =
    aFrame->BuildDisplayListForStackingContext(&builder, aTarget, &list);

  builder.LeavePresShell(aFrame, aTarget);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoTArray<nsIFrame*,8> outFrames;
  nsDisplayItem::HitTestState hitTestState(&aOutIDs);
  list.HitTest(&builder, aTarget, &hitTestState, &outFrames);
  list.DeleteAll();

  return NS_OK;
}

nsIFrame*
nsLayoutUtils::GetFrameForPoint(nsIFrame* aFrame, nsPoint aPt,
                                bool aShouldIgnoreSuppression,
                                bool aIgnoreRootScrollFrame)
{
  SAMPLE_LABEL("nsLayoutUtils", "GetFrameForPoint");
  nsresult rv;
  nsAutoTArray<nsIFrame*,8> outFrames;
  rv = GetFramesForArea(aFrame, nsRect(aPt, nsSize(1, 1)), outFrames,
                        aShouldIgnoreSuppression, aIgnoreRootScrollFrame);
  NS_ENSURE_SUCCESS(rv, nullptr);
  return outFrames.Length() ? outFrames.ElementAt(0) : nullptr;
}

nsresult
nsLayoutUtils::GetFramesForArea(nsIFrame* aFrame, const nsRect& aRect,
                                nsTArray<nsIFrame*> &aOutFrames,
                                bool aShouldIgnoreSuppression,
                                bool aIgnoreRootScrollFrame)
{
  SAMPLE_LABEL("nsLayoutUtils","GetFramesForArea");
  nsDisplayListBuilder builder(aFrame, nsDisplayListBuilder::EVENT_DELIVERY,
		                       false);
  nsDisplayList list;
  nsRect target(aRect);

  if (aShouldIgnoreSuppression) {
    builder.IgnorePaintSuppression();
  }

  if (aIgnoreRootScrollFrame) {
    nsIFrame* rootScrollFrame =
      aFrame->PresContext()->PresShell()->GetRootScrollFrame();
    if (rootScrollFrame) {
      builder.SetIgnoreScrollFrame(rootScrollFrame);
    }
  }

  builder.EnterPresShell(aFrame, target);

  nsresult rv =
    aFrame->BuildDisplayListForStackingContext(&builder, target, &list);

  builder.LeavePresShell(aFrame, target);
  NS_ENSURE_SUCCESS(rv, rv);

#ifdef MOZ_DUMP_PAINTING
  if (gDumpEventList) {
    fprintf(stdout, "Event handling --- (%d,%d):\n", aRect.x, aRect.y);
    nsFrame::PrintDisplayList(&builder, list);
  }
#endif

  nsDisplayItem::HitTestState hitTestState;
  list.HitTest(&builder, target, &hitTestState, &aOutFrames);
  list.DeleteAll();
  return NS_OK;
}

nsresult
nsLayoutUtils::PaintFrame(nsRenderingContext* aRenderingContext, nsIFrame* aFrame,
                          const nsRegion& aDirtyRegion, nscolor aBackstop,
                          uint32_t aFlags)
{
  SAMPLE_LABEL("nsLayoutUtils","PaintFrame");
  if (aFlags & PAINT_WIDGET_LAYERS) {
    nsIView* view = aFrame->GetView();
    if (!(view && view->GetWidget() && GetDisplayRootFrame(aFrame) == aFrame)) {
      aFlags &= ~PAINT_WIDGET_LAYERS;
      NS_ASSERTION(aRenderingContext, "need a rendering context");
    }
  }

  nsPresContext* presContext = aFrame->PresContext();
  nsIPresShell* presShell = presContext->PresShell();
  nsRootPresContext* rootPresContext = presContext->GetRootPresContext();
  if (!rootPresContext) {
    return NS_OK;
  }

  nsIFrame* rootScrollFrame = presShell->GetRootScrollFrame();
  bool usingDisplayPort = false;
  nsRect displayport;
  if (rootScrollFrame) {
    nsIContent* content = rootScrollFrame->GetContent();
    if (content) {
      usingDisplayPort = nsLayoutUtils::GetDisplayPort(content, &displayport);
    }
  }

  bool ignoreViewportScrolling = presShell->IgnoringViewportScrolling();
  nsRegion visibleRegion;
  if (aFlags & PAINT_WIDGET_LAYERS) {
    // This layer tree will be reused, so we'll need to calculate it
    // for the whole "visible" area of the window
    // 
    // |ignoreViewportScrolling| and |usingDisplayPort| are persistent
    // document-rendering state.  We rely on PresShell to flush
    // retained layers as needed when that persistent state changes.
    if (!usingDisplayPort) {
      visibleRegion = aFrame->GetVisualOverflowRectRelativeToSelf();
    } else {
      visibleRegion = displayport;
    }
  } else {
    visibleRegion = aDirtyRegion;
  }

  // If we're going to display something different from what we'd normally
  // paint in a window then we will flush out any retained layer trees before
  // *and after* we draw.
  bool willFlushRetainedLayers = (aFlags & PAINT_HIDE_CARET) != 0;

  nsDisplayListBuilder builder(aFrame, nsDisplayListBuilder::PAINTING,
		                       !(aFlags & PAINT_HIDE_CARET));
  if (usingDisplayPort) {
    builder.SetDisplayPort(displayport);
  }

  nsDisplayList list;
  if (aFlags & PAINT_IN_TRANSFORM) {
    builder.SetInTransform(true);
  }
  if (aFlags & PAINT_SYNC_DECODE_IMAGES) {
    builder.SetSyncDecodeImages(true);
  }
  if (aFlags & (PAINT_WIDGET_LAYERS | PAINT_TO_WINDOW)) {
    builder.SetPaintingToWindow(true);
  }
  if (aFlags & PAINT_IGNORE_SUPPRESSION) {
    builder.IgnorePaintSuppression();
  }
  // Windowed plugins aren't allowed in popups
  if ((aFlags & PAINT_WIDGET_LAYERS) &&
      !willFlushRetainedLayers &&
      !(aFlags & PAINT_DOCUMENT_RELATIVE) &&
      rootPresContext->NeedToComputePluginGeometryUpdates()) {
    builder.SetWillComputePluginGeometry(true);
  }
  nsRect canvasArea(nsPoint(0, 0), aFrame->GetSize());

#ifdef DEBUG
  if (ignoreViewportScrolling) {
    nsIDocument* doc = aFrame->GetContent() ?
      aFrame->GetContent()->GetCurrentDoc() : nullptr;
    NS_ASSERTION(!aFrame->GetParent() ||
                 (doc && doc->IsBeingUsedAsImage()),
                 "Only expecting ignoreViewportScrolling for root frames and "
                 "for image documents.");
  }
#endif

  if (ignoreViewportScrolling && !aFrame->GetParent()) {
    nsIFrame* rootScrollFrame = presShell->GetRootScrollFrame();
    if (rootScrollFrame) {
      nsIScrollableFrame* rootScrollableFrame =
        presShell->GetRootScrollFrameAsScrollable();
      if (aFlags & PAINT_DOCUMENT_RELATIVE) {
        // Make visibleRegion and aRenderingContext relative to the
        // scrolled frame instead of the root frame.
        nsPoint pos = rootScrollableFrame->GetScrollPosition();
        visibleRegion.MoveBy(-pos);
        if (aRenderingContext) {
          aRenderingContext->Translate(pos);
        }
      }
      builder.SetIgnoreScrollFrame(rootScrollFrame);

      nsCanvasFrame* canvasFrame =
        do_QueryFrame(rootScrollableFrame->GetScrolledFrame());
      if (canvasFrame) {
        // Use UnionRect here to ensure that areas where the scrollbars
        // were are still filled with the background color.
        canvasArea.UnionRect(canvasArea,
          canvasFrame->CanvasArea() + builder.ToReferenceFrame(canvasFrame));
      }
    }
  }
  nsresult rv;

  nsRect dirtyRect = visibleRegion.GetBounds();
  builder.EnterPresShell(aFrame, dirtyRect);
  {
  SAMPLE_LABEL("nsLayoutUtils","PaintFrame::BuildDisplayList");
  rv = aFrame->BuildDisplayListForStackingContext(&builder, dirtyRect, &list);
  }
  const bool paintAllContinuations = aFlags & PAINT_ALL_CONTINUATIONS;
  NS_ASSERTION(!paintAllContinuations || !aFrame->GetPrevContinuation(),
               "If painting all continuations, the frame must be "
               "first-continuation");

  nsIAtom* frameType = aFrame->GetType();

  if (paintAllContinuations) {
    nsIFrame* currentFrame = aFrame;
    while (NS_SUCCEEDED(rv) &&
           (currentFrame = currentFrame->GetNextContinuation()) != nullptr) {
      SAMPLE_LABEL("nsLayoutUtils","PaintFrame::ContinuationsBuildDisplayList");
      nsRect frameDirty = dirtyRect - builder.ToReferenceFrame(currentFrame);
      rv = currentFrame->BuildDisplayListForStackingContext(&builder,
                                                            frameDirty, &list);
    }
  }

  // For the viewport frame in print preview/page layout we want to paint
  // the grey background behind the page, not the canvas color.
  if (frameType == nsGkAtoms::viewportFrame && 
      nsLayoutUtils::NeedsPrintPreviewBackground(presContext)) {
    nsRect bounds = nsRect(builder.ToReferenceFrame(aFrame),
                           aFrame->GetSize());
    rv = presShell->AddPrintPreviewBackgroundItem(builder, list, aFrame, bounds);
  } else if (frameType != nsGkAtoms::pageFrame) {
    // For printing, this function is first called on an nsPageFrame, which
    // creates a display list with a PageContent item. The PageContent item's
    // paint function calls this function on the nsPageFrame's child which is
    // an nsPageContentFrame. We only want to add the canvas background color
    // item once, for the nsPageContentFrame.

    // Add the canvas background color to the bottom of the list. This
    // happens after we've built the list so that AddCanvasBackgroundColorItem
    // can monkey with the contents if necessary.
    canvasArea.IntersectRect(canvasArea, visibleRegion.GetBounds());
    rv = presShell->AddCanvasBackgroundColorItem(
           builder, list, aFrame, canvasArea, aBackstop);

    // If the passed in backstop color makes us draw something different from
    // normal, we need to flush layers.
    if ((aFlags & PAINT_WIDGET_LAYERS) && !willFlushRetainedLayers) {
      nsIView* view = aFrame->GetView();
      if (view) {
        nscolor backstop = presShell->ComputeBackstopColor(view);
        // The PresShell's canvas background color doesn't get updated until
        // EnterPresShell, so this check has to be done after that.
        nscolor canvasColor = presShell->GetCanvasBackground();
        if (NS_ComposeColors(aBackstop, canvasColor) !=
            NS_ComposeColors(backstop, canvasColor)) {
          willFlushRetainedLayers = true;
        }
      }
    }
  }

  builder.LeavePresShell(aFrame, dirtyRect);
  NS_ENSURE_SUCCESS(rv, rv);

  if (builder.GetHadToIgnorePaintSuppression()) {
    willFlushRetainedLayers = true;
  }

#ifdef MOZ_DUMP_PAINTING
  FILE* savedDumpFile = gfxUtils::sDumpPaintFile;
  if (gfxUtils::sDumpPaintList || gfxUtils::sDumpPainting) {
    if (gfxUtils::sDumpPaintingToFile) {
      nsCString string("dump-");
      string.AppendInt(gPaintCount);
      string.Append(".html");
      gfxUtils::sDumpPaintFile = fopen(string.BeginReading(), "w");
    } else {
      gfxUtils::sDumpPaintFile = stdout;
    }
    if (gfxUtils::sDumpPaintingToFile) {
      fprintf(gfxUtils::sDumpPaintFile, "<html><head><script>var array = {}; function ViewImage(index) { window.location = array[index]; }</script></head><body>");
    }
    fprintf(gfxUtils::sDumpPaintFile, "Painting --- before optimization (dirty %d,%d,%d,%d):\n",
            dirtyRect.x, dirtyRect.y, dirtyRect.width, dirtyRect.height);
    nsFrame::PrintDisplayList(&builder, list, gfxUtils::sDumpPaintFile, gfxUtils::sDumpPaintingToFile);
    if (gfxUtils::sDumpPaintingToFile) {
      fprintf(gfxUtils::sDumpPaintFile, "<script>");
    }
  }
#endif

  list.ComputeVisibilityForRoot(&builder, &visibleRegion);

  uint32_t flags = nsDisplayList::PAINT_DEFAULT;
  if (aFlags & PAINT_WIDGET_LAYERS) {
    flags |= nsDisplayList::PAINT_USE_WIDGET_LAYERS;
    if (willFlushRetainedLayers) {
      // The caller wanted to paint from retained layers, but set up
      // the paint in such a way that we can't use them.  We're going
      // to display something different from what we'd normally paint
      // in a window, so make sure we flush out any retained layer
      // trees before *and after* we draw.  Callers should be fixed to
      // not do this.
      NS_WARNING("Flushing retained layers!");
      flags |= nsDisplayList::PAINT_FLUSH_LAYERS;
    } else if (!(aFlags & PAINT_DOCUMENT_RELATIVE)) {
      nsIWidget *widget = aFrame->GetNearestWidget();
      if (widget) {
        builder.SetFinalTransparentRegion(visibleRegion);
        // If we're finished building display list items for painting of the outermost
        // pres shell, notify the widget about any toolbars we've encountered.
        widget->UpdateThemeGeometries(builder.GetThemeGeometries());
      }
    }
  }
  if (aFlags & PAINT_EXISTING_TRANSACTION) {
    flags |= nsDisplayList::PAINT_EXISTING_TRANSACTION;
  }
  if (aFlags & PAINT_NO_COMPOSITE) {
    flags |= nsDisplayList::PAINT_NO_COMPOSITE;
  }

  list.PaintRoot(&builder, aRenderingContext, flags);

#ifdef MOZ_DUMP_PAINTING
  if (gfxUtils::sDumpPaintList || gfxUtils::sDumpPainting) {
    if (gfxUtils::sDumpPaintingToFile) {
      fprintf(gfxUtils::sDumpPaintFile, "</script>");
    }
    fprintf(gfxUtils::sDumpPaintFile, "Painting --- after optimization:\n");
    nsFrame::PrintDisplayList(&builder, list, gfxUtils::sDumpPaintFile, gfxUtils::sDumpPaintingToFile);

    fprintf(gfxUtils::sDumpPaintFile, "Painting --- retained layer tree:\n");
    nsIWidget* widget = aFrame->GetNearestWidget();
    if (widget) {
      nsRefPtr<LayerManager> layerManager = widget->GetLayerManager();
      if (layerManager) {
        FrameLayerBuilder::DumpRetainedLayerTree(layerManager, gfxUtils::sDumpPaintFile,
                                                 gfxUtils::sDumpPaintingToFile);
      }
    }
    if (gfxUtils::sDumpPaintingToFile) {
      fprintf(gfxUtils::sDumpPaintFile, "</body></html>");
      fclose(gfxUtils::sDumpPaintFile);
    }
    gfxUtils::sDumpPaintFile = savedDumpFile;
    gPaintCount++;
  }
#endif

  // Update the widget's opaque region information. This sets
  // glass boundaries on Windows. Also set up plugin clip regions and bounds.
  if ((aFlags & PAINT_WIDGET_LAYERS) &&
      !willFlushRetainedLayers &&
      !(aFlags & PAINT_DOCUMENT_RELATIVE)) {
    nsIWidget *widget = aFrame->GetNearestWidget();
    if (widget) {
      nsRegion excludedRegion = builder.GetExcludedGlassRegion();
      excludedRegion.Sub(excludedRegion, visibleRegion);
      nsIntRegion windowRegion(excludedRegion.ToNearestPixels(presContext->AppUnitsPerDevPixel()));
      widget->UpdateOpaqueRegion(windowRegion);
    }
  }

  if (builder.WillComputePluginGeometry()) {
    rootPresContext->ComputePluginGeometryUpdates(aFrame, &builder, &list);
  }

  // Flush the list so we don't trigger the IsEmpty-on-destruction assertion
  list.DeleteAll();
  return NS_OK;
}

int32_t
nsLayoutUtils::GetZIndex(nsIFrame* aFrame) {
  if (!aFrame->IsPositioned() && !aFrame->IsFlexItem())
    return 0;

  const nsStylePosition* position =
    aFrame->GetStylePosition();
  if (position->mZIndex.GetUnit() == eStyleUnit_Integer)
    return position->mZIndex.GetIntValue();

  // sort the auto and 0 elements together
  return 0;
}

/**
 * Uses a binary search for find where the cursor falls in the line of text
 * It also keeps track of the part of the string that has already been measured
 * so it doesn't have to keep measuring the same text over and over
 *
 * @param "aBaseWidth" contains the width in twips of the portion
 * of the text that has already been measured, and aBaseInx contains
 * the index of the text that has already been measured.
 *
 * @param aTextWidth returns the (in twips) the length of the text that falls
 * before the cursor aIndex contains the index of the text where the cursor falls
 */
bool
nsLayoutUtils::BinarySearchForPosition(nsRenderingContext* aRendContext,
                        const PRUnichar* aText,
                        int32_t    aBaseWidth,
                        int32_t    aBaseInx,
                        int32_t    aStartInx,
                        int32_t    aEndInx,
                        int32_t    aCursorPos,
                        int32_t&   aIndex,
                        int32_t&   aTextWidth)
{
  int32_t range = aEndInx - aStartInx;
  if ((range == 1) || (range == 2 && NS_IS_HIGH_SURROGATE(aText[aStartInx]))) {
    aIndex   = aStartInx + aBaseInx;
    aTextWidth = aRendContext->GetWidth(aText, aIndex);
    return true;
  }

  int32_t inx = aStartInx + (range / 2);

  // Make sure we don't leave a dangling low surrogate
  if (NS_IS_HIGH_SURROGATE(aText[inx-1]))
    inx++;

  int32_t textWidth = aRendContext->GetWidth(aText, inx);

  int32_t fullWidth = aBaseWidth + textWidth;
  if (fullWidth == aCursorPos) {
    aTextWidth = textWidth;
    aIndex = inx;
    return true;
  } else if (aCursorPos < fullWidth) {
    aTextWidth = aBaseWidth;
    if (BinarySearchForPosition(aRendContext, aText, aBaseWidth, aBaseInx, aStartInx, inx, aCursorPos, aIndex, aTextWidth)) {
      return true;
    }
  } else {
    aTextWidth = fullWidth;
    if (BinarySearchForPosition(aRendContext, aText, aBaseWidth, aBaseInx, inx, aEndInx, aCursorPos, aIndex, aTextWidth)) {
      return true;
    }
  }
  return false;
}

static void
AddBoxesForFrame(nsIFrame* aFrame,
                 nsLayoutUtils::BoxCallback* aCallback)
{
  nsIAtom* pseudoType = aFrame->GetStyleContext()->GetPseudo();

  if (pseudoType == nsCSSAnonBoxes::tableOuter) {
    AddBoxesForFrame(aFrame->GetFirstPrincipalChild(), aCallback);
    nsIFrame* kid = aFrame->GetFirstChild(nsIFrame::kCaptionList);
    if (kid) {
      AddBoxesForFrame(kid, aCallback);
    }
  } else if (pseudoType == nsCSSAnonBoxes::mozAnonymousBlock ||
             pseudoType == nsCSSAnonBoxes::mozAnonymousPositionedBlock ||
             pseudoType == nsCSSAnonBoxes::mozMathMLAnonymousBlock ||
             pseudoType == nsCSSAnonBoxes::mozXULAnonymousBlock) {
    for (nsIFrame* kid = aFrame->GetFirstPrincipalChild(); kid; kid = kid->GetNextSibling()) {
      AddBoxesForFrame(kid, aCallback);
    }
  } else {
    aCallback->AddBox(aFrame);
  }
}

void
nsLayoutUtils::GetAllInFlowBoxes(nsIFrame* aFrame, BoxCallback* aCallback)
{
  while (aFrame) {
    AddBoxesForFrame(aFrame, aCallback);
    aFrame = nsLayoutUtils::GetNextContinuationOrSpecialSibling(aFrame);
  }
}

struct BoxToRect : public nsLayoutUtils::BoxCallback {
  typedef nsSize (*GetRectFromFrameFun)(nsIFrame*);

  nsIFrame* mRelativeTo;
  nsLayoutUtils::RectCallback* mCallback;
  uint32_t mFlags;
  GetRectFromFrameFun mRectFromFrame;

  BoxToRect(nsIFrame* aRelativeTo, nsLayoutUtils::RectCallback* aCallback,
            uint32_t aFlags, GetRectFromFrameFun aRectFromFrame)
    : mRelativeTo(aRelativeTo), mCallback(aCallback), mFlags(aFlags),
      mRectFromFrame(aRectFromFrame) {}

  virtual void AddBox(nsIFrame* aFrame) {
    nsRect r;
    nsIFrame* outer = nsSVGUtils::GetOuterSVGFrameAndCoveredRegion(aFrame, &r);
    if (!outer) {
      outer = aFrame;
      r = nsRect(nsPoint(0, 0), mRectFromFrame(aFrame));
    }
    if (mFlags & nsLayoutUtils::RECTS_ACCOUNT_FOR_TRANSFORMS) {
      r = nsLayoutUtils::TransformFrameRectToAncestor(outer, r, mRelativeTo);
    } else {
      r += outer->GetOffsetTo(mRelativeTo);
    }
    mCallback->AddRect(r);
  }
};

static nsSize
GetFrameBorderSize(nsIFrame* aFrame)
{
  return aFrame->GetSize();
}

void
nsLayoutUtils::GetAllInFlowRects(nsIFrame* aFrame, nsIFrame* aRelativeTo,
                                 RectCallback* aCallback, uint32_t aFlags)
{
  BoxToRect converter(aRelativeTo, aCallback, aFlags, &GetFrameBorderSize);
  GetAllInFlowBoxes(aFrame, &converter);
}

static nsSize
GetFramePaddingSize(nsIFrame* aFrame)
{
  return aFrame->GetPaddingRect().Size();
}

void
nsLayoutUtils::GetAllInFlowPaddingRects(nsIFrame* aFrame, nsIFrame* aRelativeTo,
                                        RectCallback* aCallback, uint32_t aFlags)
{
  BoxToRect converter(aRelativeTo, aCallback, aFlags, &GetFramePaddingSize);
  GetAllInFlowBoxes(aFrame, &converter);
}

nsLayoutUtils::RectAccumulator::RectAccumulator() : mSeenFirstRect(false) {}

void nsLayoutUtils::RectAccumulator::AddRect(const nsRect& aRect) {
  mResultRect.UnionRect(mResultRect, aRect);
  if (!mSeenFirstRect) {
    mSeenFirstRect = true;
    mFirstRect = aRect;
  }
}

nsLayoutUtils::RectListBuilder::RectListBuilder(nsClientRectList* aList)
  : mRectList(aList), mRV(NS_OK) {}

void nsLayoutUtils::RectListBuilder::AddRect(const nsRect& aRect) {
  nsRefPtr<nsClientRect> rect = new nsClientRect();

  rect->SetLayoutRect(aRect);
  mRectList->Append(rect);
}

nsIFrame* nsLayoutUtils::GetContainingBlockForClientRect(nsIFrame* aFrame)
{
  return aFrame->PresContext()->PresShell()->GetRootFrame();
}

nsRect
nsLayoutUtils::GetAllInFlowRectsUnion(nsIFrame* aFrame, nsIFrame* aRelativeTo,
                                      uint32_t aFlags) {
  RectAccumulator accumulator;
  GetAllInFlowRects(aFrame, aRelativeTo, &accumulator, aFlags);
  return accumulator.mResultRect.IsEmpty() ? accumulator.mFirstRect
          : accumulator.mResultRect;
}

nsRect
nsLayoutUtils::GetAllInFlowPaddingRectsUnion(nsIFrame* aFrame,
                                             nsIFrame* aRelativeTo,
                                             uint32_t aFlags)
{
  RectAccumulator accumulator;
  GetAllInFlowPaddingRects(aFrame, aRelativeTo, &accumulator, aFlags);
  return accumulator.mResultRect.IsEmpty() ? accumulator.mFirstRect
          : accumulator.mResultRect;
}

nsRect
nsLayoutUtils::GetTextShadowRectsUnion(const nsRect& aTextAndDecorationsRect,
                                       nsIFrame* aFrame,
                                       uint32_t aFlags)
{
  const nsStyleText* textStyle = aFrame->GetStyleText();
  if (!textStyle->HasTextShadow(aFrame))
    return aTextAndDecorationsRect;

  nsRect resultRect = aTextAndDecorationsRect;
  int32_t A2D = aFrame->PresContext()->AppUnitsPerDevPixel();
  for (uint32_t i = 0; i < textStyle->mTextShadow->Length(); ++i) {
    nsCSSShadowItem* shadow = textStyle->mTextShadow->ShadowAt(i);
    nsMargin blur = nsContextBoxBlur::GetBlurRadiusMargin(shadow->mRadius, A2D);
    if ((aFlags & EXCLUDE_BLUR_SHADOWS) && blur != nsMargin(0, 0, 0, 0))
      continue;

    nsRect tmpRect(aTextAndDecorationsRect);

    tmpRect.MoveBy(nsPoint(shadow->mXOffset, shadow->mYOffset));
    tmpRect.Inflate(blur);

    resultRect.UnionRect(resultRect, tmpRect);
  }
  return resultRect;
}

nsresult
nsLayoutUtils::GetFontMetricsForFrame(const nsIFrame* aFrame,
                                      nsFontMetrics** aFontMetrics,
                                      float aInflation)
{
  return nsLayoutUtils::GetFontMetricsForStyleContext(aFrame->GetStyleContext(),
                                                      aFontMetrics,
                                                      aInflation);
}

nsresult
nsLayoutUtils::GetFontMetricsForStyleContext(nsStyleContext* aStyleContext,
                                             nsFontMetrics** aFontMetrics,
                                             float aInflation)
{
  // pass the user font set object into the device context to pass along to CreateFontGroup
  gfxUserFontSet* fs = aStyleContext->PresContext()->GetUserFontSet();

  nsFont font = aStyleContext->GetStyleFont()->mFont;
  // We need to not run font.size through floats when it's large since
  // doing so would be lossy.  Fortunately, in such cases, aInflation is
  // guaranteed to be 1.0f.
  if (aInflation != 1.0f) {
    font.size = NSToCoordRound(font.size * aInflation);
  }
  return aStyleContext->PresContext()->DeviceContext()->GetMetricsFor(
                  font, aStyleContext->GetStyleFont()->mLanguage,
                  fs, *aFontMetrics);
}

nsIFrame*
nsLayoutUtils::FindChildContainingDescendant(nsIFrame* aParent, nsIFrame* aDescendantFrame)
{
  nsIFrame* result = aDescendantFrame;

  while (result) {
    nsIFrame* parent = result->GetParent();
    if (parent == aParent) {
      break;
    }

    // The frame is not an immediate child of aParent so walk up another level
    result = parent;
  }

  return result;
}

nsBlockFrame*
nsLayoutUtils::GetAsBlock(nsIFrame* aFrame)
{
  nsBlockFrame* block = do_QueryFrame(aFrame);
  return block;
}

nsBlockFrame*
nsLayoutUtils::FindNearestBlockAncestor(nsIFrame* aFrame)
{
  nsIFrame* nextAncestor;
  for (nextAncestor = aFrame->GetParent(); nextAncestor;
       nextAncestor = nextAncestor->GetParent()) {
    nsBlockFrame* block = GetAsBlock(nextAncestor);
    if (block)
      return block;
  }
  return nullptr;
}

nsIFrame*
nsLayoutUtils::GetNonGeneratedAncestor(nsIFrame* aFrame)
{
  if (!(aFrame->GetStateBits() & NS_FRAME_GENERATED_CONTENT))
    return aFrame;

  nsIFrame* f = aFrame;
  do {
    f = GetParentOrPlaceholderFor(f);
  } while (f->GetStateBits() & NS_FRAME_GENERATED_CONTENT);
  return f;
}

nsIFrame*
nsLayoutUtils::GetParentOrPlaceholderFor(nsIFrame* aFrame)
{
  if ((aFrame->GetStateBits() & NS_FRAME_OUT_OF_FLOW)
      && !aFrame->GetPrevInFlow()) {
    return aFrame->PresContext()->PresShell()->FrameManager()->
      GetPlaceholderFrameFor(aFrame);
  }
  return aFrame->GetParent();
}

nsIFrame*
nsLayoutUtils::GetParentOrPlaceholderForCrossDoc(nsIFrame* aFrame)
{
  nsIFrame* f = GetParentOrPlaceholderFor(aFrame);
  if (f)
    return f;
  return GetCrossDocParentFrame(aFrame);
}

nsIFrame*
nsLayoutUtils::GetNextContinuationOrSpecialSibling(nsIFrame *aFrame)
{
  nsIFrame *result = aFrame->GetNextContinuation();
  if (result)
    return result;

  if ((aFrame->GetStateBits() & NS_FRAME_IS_SPECIAL) != 0) {
    // We only store the "special sibling" annotation with the first
    // frame in the continuation chain. Walk back to find that frame now.
    aFrame = aFrame->GetFirstContinuation();

    void* value = aFrame->Properties().Get(nsIFrame::IBSplitSpecialSibling());
    return static_cast<nsIFrame*>(value);
  }

  return nullptr;
}

nsIFrame*
nsLayoutUtils::GetFirstContinuationOrSpecialSibling(nsIFrame *aFrame)
{
  nsIFrame *result = aFrame->GetFirstContinuation();
  if (result->GetStateBits() & NS_FRAME_IS_SPECIAL) {
    while (true) {
      nsIFrame *f = static_cast<nsIFrame*>
        (result->Properties().Get(nsIFrame::IBSplitSpecialPrevSibling()));
      if (!f)
        break;
      result = f;
    }
  }

  return result;
}

bool
nsLayoutUtils::IsViewportScrollbarFrame(nsIFrame* aFrame)
{
  if (!aFrame)
    return false;

  nsIFrame* rootScrollFrame =
    aFrame->PresContext()->PresShell()->GetRootScrollFrame();
  if (!rootScrollFrame)
    return false;

  nsIScrollableFrame* rootScrollableFrame = do_QueryFrame(rootScrollFrame);
  NS_ASSERTION(rootScrollableFrame, "The root scorollable frame is null");

  if (!IsProperAncestorFrame(rootScrollFrame, aFrame))
    return false;

  nsIFrame* rootScrolledFrame = rootScrollableFrame->GetScrolledFrame();
  return !(rootScrolledFrame == aFrame ||
           IsProperAncestorFrame(rootScrolledFrame, aFrame));
}

static nscoord AddPercents(nsLayoutUtils::IntrinsicWidthType aType,
                           nscoord aCurrent, float aPercent)
{
  nscoord result = aCurrent;
  if (aPercent > 0.0f && aType == nsLayoutUtils::PREF_WIDTH) {
    // XXX Should we also consider percentages for min widths, up to a
    // limit?
    if (aPercent >= 1.0f)
      result = nscoord_MAX;
    else
      result = NSToCoordRound(float(result) / (1.0f - aPercent));
  }
  return result;
}

// Use only for widths/heights (or their min/max), since it clamps
// negative calc() results to 0.
static bool GetAbsoluteCoord(const nsStyleCoord& aStyle, nscoord& aResult)
{
  if (aStyle.IsCalcUnit()) {
    if (aStyle.CalcHasPercent()) {
      return false;
    }
    // If it has no percents, we can pass 0 for the percentage basis.
    aResult = nsRuleNode::ComputeComputedCalc(aStyle, 0);
    if (aResult < 0)
      aResult = 0;
    return true;
  }

  if (eStyleUnit_Coord != aStyle.GetUnit())
    return false;

  aResult = aStyle.GetCoordValue();
  NS_ASSERTION(aResult >= 0, "negative widths not allowed");
  return true;
}

// Only call on style coords for which GetAbsoluteCoord returned false.
static bool
GetPercentHeight(const nsStyleCoord& aStyle,
                 nsIFrame* aFrame,
                 nscoord& aResult)
{
  if (eStyleUnit_Percent != aStyle.GetUnit() &&
      !aStyle.IsCalcUnit())
    return false;

  MOZ_ASSERT(!aStyle.IsCalcUnit() || aStyle.CalcHasPercent(),
             "GetAbsoluteCoord should have handled this");

  nsIFrame *f = aFrame->GetContainingBlock();
  if (!f) {
    NS_NOTREACHED("top of frame tree not a containing block");
    return false;
  }

  const nsStylePosition *pos = f->GetStylePosition();
  nscoord h;
  if (!GetAbsoluteCoord(pos->mHeight, h) &&
      !GetPercentHeight(pos->mHeight, f, h)) {
    NS_ASSERTION(pos->mHeight.GetUnit() == eStyleUnit_Auto ||
                 pos->mHeight.HasPercent(),
                 "unknown height unit");
    nsIAtom* fType = f->GetType();
    if (fType != nsGkAtoms::viewportFrame && fType != nsGkAtoms::canvasFrame &&
        fType != nsGkAtoms::pageContentFrame) {
      // There's no basis for the percentage height, so it acts like auto.
      // Should we consider a max-height < min-height pair a basis for
      // percentage heights?  The spec is somewhat unclear, and not doing
      // so is simpler and avoids troubling discontinuities in behavior,
      // so I'll choose not to. -LDB
      return false;
    }

    NS_ASSERTION(pos->mHeight.GetUnit() == eStyleUnit_Auto,
                 "Unexpected height unit for viewport or canvas or page-content");
    // For the viewport, canvas, and page-content kids, the percentage
    // basis is just the parent height.
    h = f->GetSize().height;
    if (h == NS_UNCONSTRAINEDSIZE) {
      // We don't have a percentage basis after all
      return false;
    }
  }

  nscoord maxh;
  if (GetAbsoluteCoord(pos->mMaxHeight, maxh) ||
      GetPercentHeight(pos->mMaxHeight, f, maxh)) {
    if (maxh < h)
      h = maxh;
  } else {
    NS_ASSERTION(pos->mMaxHeight.GetUnit() == eStyleUnit_None ||
                 pos->mMaxHeight.HasPercent(),
                 "unknown max-height unit");
  }

  nscoord minh;
  if (GetAbsoluteCoord(pos->mMinHeight, minh) ||
      GetPercentHeight(pos->mMinHeight, f, minh)) {
    if (minh > h)
      h = minh;
  } else {
    NS_ASSERTION(pos->mMinHeight.HasPercent() ||
                 pos->mMinHeight.GetUnit() == eStyleUnit_Auto,
                 "unknown min-height unit");
  }

  if (aStyle.IsCalcUnit()) {
    aResult = NS_MAX(nsRuleNode::ComputeComputedCalc(aStyle, h), 0);
    return true;
  }

  aResult = NSToCoordRound(aStyle.GetPercentValue() * h);
  return true;
}

// Handles only -moz-max-content and -moz-min-content, and
// -moz-fit-content for min-width and max-width, since the others
// (-moz-fit-content for width, and -moz-available) have no effect on
// intrinsic widths.
enum eWidthProperty { PROP_WIDTH, PROP_MAX_WIDTH, PROP_MIN_WIDTH };
static bool
GetIntrinsicCoord(const nsStyleCoord& aStyle,
                  nsRenderingContext* aRenderingContext,
                  nsIFrame* aFrame,
                  eWidthProperty aProperty,
                  nscoord& aResult)
{
  NS_PRECONDITION(aProperty == PROP_WIDTH || aProperty == PROP_MAX_WIDTH ||
                  aProperty == PROP_MIN_WIDTH, "unexpected property");
  if (aStyle.GetUnit() != eStyleUnit_Enumerated)
    return false;
  int32_t val = aStyle.GetIntValue();
  NS_ASSERTION(val == NS_STYLE_WIDTH_MAX_CONTENT ||
               val == NS_STYLE_WIDTH_MIN_CONTENT ||
               val == NS_STYLE_WIDTH_FIT_CONTENT ||
               val == NS_STYLE_WIDTH_AVAILABLE,
               "unexpected enumerated value for width property");
  if (val == NS_STYLE_WIDTH_AVAILABLE)
    return false;
  if (val == NS_STYLE_WIDTH_FIT_CONTENT) {
    if (aProperty == PROP_WIDTH)
      return false; // handle like 'width: auto'
    if (aProperty == PROP_MAX_WIDTH)
      // constrain large 'width' values down to -moz-max-content
      val = NS_STYLE_WIDTH_MAX_CONTENT;
    else
      // constrain small 'width' or 'max-width' values up to -moz-min-content
      val = NS_STYLE_WIDTH_MIN_CONTENT;
  }

  NS_ASSERTION(val == NS_STYLE_WIDTH_MAX_CONTENT ||
               val == NS_STYLE_WIDTH_MIN_CONTENT,
               "should have reduced everything remaining to one of these");

  // If aFrame is a container for font size inflation, then shrink
  // wrapping inside of it should not apply font size inflation.
  AutoMaybeDisableFontInflation an(aFrame);

  if (val == NS_STYLE_WIDTH_MAX_CONTENT)
    aResult = aFrame->GetPrefWidth(aRenderingContext);
  else
    aResult = aFrame->GetMinWidth(aRenderingContext);
  return true;
}

#undef  DEBUG_INTRINSIC_WIDTH

#ifdef DEBUG_INTRINSIC_WIDTH
static int32_t gNoiseIndent = 0;
#endif

/* static */ nscoord
nsLayoutUtils::IntrinsicForContainer(nsRenderingContext *aRenderingContext,
                                     nsIFrame *aFrame,
                                     IntrinsicWidthType aType)
{
  NS_PRECONDITION(aFrame, "null frame");
  NS_PRECONDITION(aType == MIN_WIDTH || aType == PREF_WIDTH, "bad type");

#ifdef DEBUG_INTRINSIC_WIDTH
  nsFrame::IndentBy(stdout, gNoiseIndent);
  static_cast<nsFrame*>(aFrame)->ListTag(stdout);
  printf(" %s intrinsic width for container:\n",
         aType == MIN_WIDTH ? "min" : "pref");
#endif

  // If aFrame is a container for font size inflation, then shrink
  // wrapping inside of it should not apply font size inflation.
  AutoMaybeDisableFontInflation an(aFrame);

  nsIFrame::IntrinsicWidthOffsetData offsets =
    aFrame->IntrinsicWidthOffsets(aRenderingContext);

  const nsStylePosition *stylePos = aFrame->GetStylePosition();
  uint8_t boxSizing = stylePos->mBoxSizing;
  const nsStyleCoord &styleWidth = stylePos->mWidth;
  const nsStyleCoord &styleMinWidth = stylePos->mMinWidth;
  const nsStyleCoord &styleMaxWidth = stylePos->mMaxWidth;

  // We build up two values starting with the content box, and then
  // adding padding, border and margin.  The result is normally
  // |result|.  Then, when we handle 'width', 'min-width', and
  // 'max-width', we use the results we've been building in |min| as a
  // minimum, overriding 'min-width'.  This ensures two things:
  //   * that we don't let a value of 'box-sizing' specifying a width
  //     smaller than the padding/border inside the box-sizing box give
  //     a content width less than zero
  //   * that we prevent tables from becoming smaller than their
  //     intrinsic minimum width
  nscoord result = 0, min = 0;

  nscoord maxw;
  bool haveFixedMaxWidth = GetAbsoluteCoord(styleMaxWidth, maxw);
  nscoord minw;

  // Treat "min-width: auto" as 0.
  bool haveFixedMinWidth;
  if (eStyleUnit_Auto == styleMinWidth.GetUnit()) {
    // NOTE: Technically, "auto" is supposed to behave like "min-content" on
    // flex items. However, we don't need to worry about that here, because
    // flex items' min-sizes are intentionally ignored until the flex
    // container explicitly considers them during space distribution.
    minw = 0;
    haveFixedMinWidth = true;
  } else {
    haveFixedMinWidth = GetAbsoluteCoord(styleMinWidth, minw);
  }

  // If we have a specified width (or a specified 'min-width' greater
  // than the specified 'max-width', which works out to the same thing),
  // don't even bother getting the frame's intrinsic width, because in
  // this case GetAbsoluteCoord(styleWidth, w) will always succeed, so
  // we'll never need the intrinsic dimensions.
  if (styleWidth.GetUnit() == eStyleUnit_Enumerated &&
      (styleWidth.GetIntValue() == NS_STYLE_WIDTH_MAX_CONTENT ||
       styleWidth.GetIntValue() == NS_STYLE_WIDTH_MIN_CONTENT)) {
    // -moz-fit-content and -moz-available enumerated widths compute intrinsic
    // widths just like auto.
    // For -moz-max-content and -moz-min-content, we handle them like
    // specified widths, but ignore -moz-box-sizing.
    boxSizing = NS_STYLE_BOX_SIZING_CONTENT;
  } else if (!styleWidth.ConvertsToLength() &&
             !(haveFixedMinWidth && haveFixedMaxWidth && maxw <= minw)) {
#ifdef DEBUG_INTRINSIC_WIDTH
    ++gNoiseIndent;
#endif
    if (aType == MIN_WIDTH)
      result = aFrame->GetMinWidth(aRenderingContext);
    else
      result = aFrame->GetPrefWidth(aRenderingContext);
#ifdef DEBUG_INTRINSIC_WIDTH
    --gNoiseIndent;
    nsFrame::IndentBy(stdout, gNoiseIndent);
    static_cast<nsFrame*>(aFrame)->ListTag(stdout);
    printf(" %s intrinsic width from frame is %d.\n",
           aType == MIN_WIDTH ? "min" : "pref", result);
#endif

    // Handle elements with an intrinsic ratio (or size) and a specified
    // height, min-height, or max-height.
    // NOTE: We treat "min-height:auto" as "0" for the purpose of this code,
    // since that's what it means in all cases except for on flex items -- and
    // even there, we're supposed to ignore it (i.e. treat it as 0) until the
    // flex container explicitly considers it.
    const nsStyleCoord &styleHeight = stylePos->mHeight;
    const nsStyleCoord &styleMinHeight = stylePos->mMinHeight;
    const nsStyleCoord &styleMaxHeight = stylePos->mMaxHeight;

    if (styleHeight.GetUnit() != eStyleUnit_Auto ||
        !(styleMinHeight.GetUnit() == eStyleUnit_Auto || 
          (styleMinHeight.GetUnit() == eStyleUnit_Coord &&
           styleMinHeight.GetCoordValue() == 0)) ||
        styleMaxHeight.GetUnit() != eStyleUnit_None) {

      nsSize ratio = aFrame->GetIntrinsicRatio();

      if (ratio.height != 0) {
        nscoord heightTakenByBoxSizing = 0;
        switch (boxSizing) {
        case NS_STYLE_BOX_SIZING_BORDER: {
          const nsStyleBorder* styleBorder = aFrame->GetStyleBorder();
          heightTakenByBoxSizing +=
            styleBorder->GetComputedBorder().TopBottom();
          // fall through
        }
        case NS_STYLE_BOX_SIZING_PADDING: {
          const nsStylePadding* stylePadding = aFrame->GetStylePadding();
          nscoord pad;
          if (GetAbsoluteCoord(stylePadding->mPadding.GetTop(), pad) ||
              GetPercentHeight(stylePadding->mPadding.GetTop(), aFrame, pad)) {
            heightTakenByBoxSizing += pad;
          }
          if (GetAbsoluteCoord(stylePadding->mPadding.GetBottom(), pad) ||
              GetPercentHeight(stylePadding->mPadding.GetBottom(), aFrame, pad)) {
            heightTakenByBoxSizing += pad;
          }
          // fall through
        }
        case NS_STYLE_BOX_SIZING_CONTENT:
        default:
          break;
        }

        nscoord h;
        if (GetAbsoluteCoord(styleHeight, h) ||
            GetPercentHeight(styleHeight, aFrame, h)) {
          h = NS_MAX(0, h - heightTakenByBoxSizing);
          result =
            NSToCoordRound(h * (float(ratio.width) / float(ratio.height)));
        }

        if (GetAbsoluteCoord(styleMaxHeight, h) ||
            GetPercentHeight(styleMaxHeight, aFrame, h)) {
          h = NS_MAX(0, h - heightTakenByBoxSizing);
          nscoord maxWidth =
            NSToCoordRound(h * (float(ratio.width) / float(ratio.height)));
          if (maxWidth < result)
            result = maxWidth;
        }

        if (GetAbsoluteCoord(styleMinHeight, h) ||
            GetPercentHeight(styleMinHeight, aFrame, h)) {
          h = NS_MAX(0, h - heightTakenByBoxSizing);
          nscoord minWidth =
            NSToCoordRound(h * (float(ratio.width) / float(ratio.height)));
          if (minWidth > result)
            result = minWidth;
        }
      }
    }
  }

  if (aFrame->GetType() == nsGkAtoms::tableFrame) {
    // Tables can't shrink smaller than their intrinsic minimum width,
    // no matter what.
    min = aFrame->GetMinWidth(aRenderingContext);
  }

  // We also need to track what has been added on outside of the box
  // (controlled by 'box-sizing') where 'width', 'min-width' and
  // 'max-width' are applied.  We have to account for these properties
  // after getting all the offsets (margin, border, padding) because
  // percentages do not operate linearly.
  // Doing this is ok because although percentages aren't handled
  // linearly, they are handled monotonically.
  nscoord coordOutsideWidth = offsets.hPadding;
  float pctOutsideWidth = offsets.hPctPadding;

  float pctTotal = 0.0f;

  if (boxSizing == NS_STYLE_BOX_SIZING_PADDING) {
    min += coordOutsideWidth;
    result = NSCoordSaturatingAdd(result, coordOutsideWidth);
    pctTotal += pctOutsideWidth;

    coordOutsideWidth = 0;
    pctOutsideWidth = 0.0f;
  }

  coordOutsideWidth += offsets.hBorder;

  if (boxSizing == NS_STYLE_BOX_SIZING_BORDER) {
    min += coordOutsideWidth;
    result = NSCoordSaturatingAdd(result, coordOutsideWidth);
    pctTotal += pctOutsideWidth;

    coordOutsideWidth = 0;
    pctOutsideWidth = 0.0f;
  }

  coordOutsideWidth += offsets.hMargin;
  pctOutsideWidth += offsets.hPctMargin;

  min += coordOutsideWidth;
  result = NSCoordSaturatingAdd(result, coordOutsideWidth);
  pctTotal += pctOutsideWidth;

  nscoord w;
  if (GetAbsoluteCoord(styleWidth, w) ||
      GetIntrinsicCoord(styleWidth, aRenderingContext, aFrame,
                        PROP_WIDTH, w)) {
    result = AddPercents(aType, w + coordOutsideWidth, pctOutsideWidth);
  }
  else if (aType == MIN_WIDTH &&
           // The only cases of coord-percent-calc() units that
           // GetAbsoluteCoord didn't handle are percent and calc()s
           // containing percent.
           styleWidth.IsCoordPercentCalcUnit() &&
           aFrame->IsFrameOfType(nsIFrame::eReplaced)) {
    // A percentage width on replaced elements means they can shrink to 0.
    result = 0; // let |min| handle padding/border/margin
  }
  else {
    // NOTE: We could really do a lot better for percents and for some
    // cases of calc() containing percent (certainly including any where
    // the coefficient on the percent is positive and there are no max()
    // expressions).  However, doing better for percents wouldn't be
    // backwards compatible.
    result = AddPercents(aType, result, pctTotal);
  }

  if (haveFixedMaxWidth ||
      GetIntrinsicCoord(styleMaxWidth, aRenderingContext, aFrame,
                        PROP_MAX_WIDTH, maxw)) {
    maxw = AddPercents(aType, maxw + coordOutsideWidth, pctOutsideWidth);
    if (result > maxw)
      result = maxw;
  }

  if (haveFixedMinWidth ||
      GetIntrinsicCoord(styleMinWidth, aRenderingContext, aFrame,
                        PROP_MIN_WIDTH, minw)) {
    minw = AddPercents(aType, minw + coordOutsideWidth, pctOutsideWidth);
    if (result < minw)
      result = minw;
  }

  min = AddPercents(aType, min, pctTotal);
  if (result < min)
    result = min;

  const nsStyleDisplay *disp = aFrame->GetStyleDisplay();
  if (aFrame->IsThemed(disp)) {
    nsIntSize size(0, 0);
    bool canOverride = true;
    nsPresContext *presContext = aFrame->PresContext();
    presContext->GetTheme()->
      GetMinimumWidgetSize(aRenderingContext, aFrame, disp->mAppearance,
                           &size, &canOverride);

    nscoord themeWidth = presContext->DevPixelsToAppUnits(size.width);

    // GMWS() returns a border-box width
    themeWidth += offsets.hMargin;
    themeWidth = AddPercents(aType, themeWidth, offsets.hPctMargin);

    if (themeWidth > result || !canOverride)
      result = themeWidth;
  }

#ifdef DEBUG_INTRINSIC_WIDTH
  nsFrame::IndentBy(stdout, gNoiseIndent);
  static_cast<nsFrame*>(aFrame)->ListTag(stdout);
  printf(" %s intrinsic width for container is %d twips.\n",
         aType == MIN_WIDTH ? "min" : "pref", result);
#endif

  return result;
}

/* static */ nscoord
nsLayoutUtils::ComputeWidthDependentValue(
                 nscoord              aContainingBlockWidth,
                 const nsStyleCoord&  aCoord)
{
  NS_WARN_IF_FALSE(aContainingBlockWidth != NS_UNCONSTRAINEDSIZE,
                   "have unconstrained width; this should only result from "
                   "very large sizes, not attempts at intrinsic width "
                   "calculation");

  if (aCoord.IsCoordPercentCalcUnit()) {
    return nsRuleNode::ComputeCoordPercentCalc(aCoord, aContainingBlockWidth);
  }
  NS_ASSERTION(aCoord.GetUnit() == eStyleUnit_None ||
               aCoord.GetUnit() == eStyleUnit_Auto,
               "unexpected width value");
  return 0;
}

/* static */ nscoord
nsLayoutUtils::ComputeWidthValue(
                 nsRenderingContext* aRenderingContext,
                 nsIFrame*            aFrame,
                 nscoord              aContainingBlockWidth,
                 nscoord              aContentEdgeToBoxSizing,
                 nscoord              aBoxSizingToMarginEdge,
                 const nsStyleCoord&  aCoord)
{
  NS_PRECONDITION(aFrame, "non-null frame expected");
  NS_PRECONDITION(aRenderingContext, "non-null rendering context expected");
  NS_WARN_IF_FALSE(aContainingBlockWidth != NS_UNCONSTRAINEDSIZE,
                   "have unconstrained width; this should only result from "
                   "very large sizes, not attempts at intrinsic width "
                   "calculation");
  NS_PRECONDITION(aContainingBlockWidth >= 0,
                  "width less than zero");

  nscoord result;
  if (aCoord.IsCoordPercentCalcUnit()) {
    result = nsRuleNode::ComputeCoordPercentCalc(aCoord, 
                                                 aContainingBlockWidth);
    // The result of a calc() expression might be less than 0; we
    // should clamp at runtime (below).  (Percentages and coords that
    // are less than 0 have already been dropped by the parser.)
    result -= aContentEdgeToBoxSizing;
  } else {
    MOZ_ASSERT(eStyleUnit_Enumerated == aCoord.GetUnit());
    // If aFrame is a container for font size inflation, then shrink
    // wrapping inside of it should not apply font size inflation.
    AutoMaybeDisableFontInflation an(aFrame);

    int32_t val = aCoord.GetIntValue();
    switch (val) {
      case NS_STYLE_WIDTH_MAX_CONTENT:
        result = aFrame->GetPrefWidth(aRenderingContext);
        NS_ASSERTION(result >= 0, "width less than zero");
        break;
      case NS_STYLE_WIDTH_MIN_CONTENT:
        result = aFrame->GetMinWidth(aRenderingContext);
        NS_ASSERTION(result >= 0, "width less than zero");
        break;
      case NS_STYLE_WIDTH_FIT_CONTENT:
        {
          nscoord pref = aFrame->GetPrefWidth(aRenderingContext),
                   min = aFrame->GetMinWidth(aRenderingContext),
                  fill = aContainingBlockWidth -
                         (aBoxSizingToMarginEdge + aContentEdgeToBoxSizing);
          result = NS_MAX(min, NS_MIN(pref, fill));
          NS_ASSERTION(result >= 0, "width less than zero");
        }
        break;
      case NS_STYLE_WIDTH_AVAILABLE:
        result = aContainingBlockWidth -
                 (aBoxSizingToMarginEdge + aContentEdgeToBoxSizing);
    }
  }

  return NS_MAX(0, result);
}

/* static */ nscoord
nsLayoutUtils::ComputeHeightDependentValue(
                 nscoord              aContainingBlockHeight,
                 const nsStyleCoord&  aCoord)
{
  // XXXldb Some callers explicitly check aContainingBlockHeight
  // against NS_AUTOHEIGHT *and* unit against eStyleUnit_Percent or
  // calc()s containing percents before calling this function.
  // However, it would be much more likely to catch problems without
  // the unit conditions.
  // XXXldb Many callers pass a non-'auto' containing block height when
  // according to CSS2.1 they should be passing 'auto'.
  NS_PRECONDITION(NS_AUTOHEIGHT != aContainingBlockHeight ||
                  !aCoord.HasPercent(),
                  "unexpected containing block height");

  if (aCoord.IsCoordPercentCalcUnit()) {
    return nsRuleNode::ComputeCoordPercentCalc(aCoord, aContainingBlockHeight);
  }

  NS_ASSERTION(aCoord.GetUnit() == eStyleUnit_None ||
               aCoord.GetUnit() == eStyleUnit_Auto,
               "unexpected height value");
  return 0;
}

#define MULDIV(a,b,c) (nscoord(int64_t(a) * int64_t(b) / int64_t(c)))

/* static */ nsSize
nsLayoutUtils::ComputeSizeWithIntrinsicDimensions(
                   nsRenderingContext* aRenderingContext, nsIFrame* aFrame,
                   const nsIFrame::IntrinsicSize& aIntrinsicSize,
                   nsSize aIntrinsicRatio, nsSize aCBSize,
                   nsSize aMargin, nsSize aBorder, nsSize aPadding)
{
  const nsStylePosition* stylePos = aFrame->GetStylePosition();

  // If we're a flex item, we'll compute our size a bit differently.
  const nsStyleCoord* widthStyleCoord = &(stylePos->mWidth);
  const nsStyleCoord* heightStyleCoord = &(stylePos->mHeight);

  bool isFlexItem = aFrame->IsFlexItem();
  bool isHorizontalFlexItem = false;

#ifdef MOZ_FLEXBOX
  if (isFlexItem) {
    // Flex items use their "flex-basis" property in place of their main-size
    // property (e.g. "width") for sizing purposes, *unless* they have
    // "flex-basis:auto", in which case they use their main-size property after
    // all.
    uint32_t flexDirection =
      aFrame->GetParent()->GetStylePosition()->mFlexDirection;
    isHorizontalFlexItem =
      flexDirection == NS_STYLE_FLEX_DIRECTION_ROW ||
      flexDirection == NS_STYLE_FLEX_DIRECTION_ROW_REVERSE;

    // NOTE: The logic here should match the similar chunk for determining
    // widthStyleCoord and heightStyleCoord in nsFrame::ComputeSize().
    const nsStyleCoord* flexBasis = &(stylePos->mFlexBasis);
    if (flexBasis->GetUnit() != eStyleUnit_Auto) {
      if (isHorizontalFlexItem) {
        widthStyleCoord = flexBasis;
      } else {
        // One caveat for vertical flex items: We don't support enumerated
        // values (e.g. "max-content") for height properties yet. So, if our
        // computed flex-basis is an enumerated value, we'll just behave as if
        // it were "auto", which means "use the main-size property after all"
        // (which is "height", in this case).
        // NOTE: Once we support intrinsic sizing keywords for "height",
        // we should remove this check.
        if (flexBasis->GetUnit() != eStyleUnit_Enumerated) {
          heightStyleCoord = flexBasis;
        }
      }
    }
  }
#endif // MOZ_FLEXBOX


  // Handle intrinsic sizes and their interaction with
  // {min-,max-,}{width,height} according to the rules in
  // http://www.w3.org/TR/CSS21/visudet.html#min-max-widths

  // Note: throughout the following section of the function, I avoid
  // a * (b / c) because of its reduced accuracy relative to a * b / c
  // or (a * b) / c (which are equivalent).

  const bool isAutoWidth = widthStyleCoord->GetUnit() == eStyleUnit_Auto;
  const bool isAutoHeight = IsAutoHeight(*heightStyleCoord, aCBSize.height);

  nsSize boxSizingAdjust(0,0);
  switch (stylePos->mBoxSizing) {
    case NS_STYLE_BOX_SIZING_BORDER:
      boxSizingAdjust += aBorder;
      // fall through
    case NS_STYLE_BOX_SIZING_PADDING:
      boxSizingAdjust += aPadding;
  }
  nscoord boxSizingToMarginEdgeWidth =
    aMargin.width + aBorder.width + aPadding.width - boxSizingAdjust.width;

  nscoord width, minWidth, maxWidth, height, minHeight, maxHeight;

  if (!isAutoWidth) {
    width = nsLayoutUtils::ComputeWidthValue(aRenderingContext,
              aFrame, aCBSize.width, boxSizingAdjust.width,
              boxSizingToMarginEdgeWidth, *widthStyleCoord);
  }

  if (stylePos->mMaxWidth.GetUnit() != eStyleUnit_None &&
      !(isFlexItem && isHorizontalFlexItem)) {
    maxWidth = nsLayoutUtils::ComputeWidthValue(aRenderingContext,
                 aFrame, aCBSize.width, boxSizingAdjust.width,
                 boxSizingToMarginEdgeWidth, stylePos->mMaxWidth);
  } else {
    maxWidth = nscoord_MAX;
  }

  // NOTE: Flex items ignore their min & max sizing properties in their
  // flex container's main-axis.  (Those properties get applied later in
  // the flexbox algorithm.)
  if (stylePos->mMinWidth.GetUnit() != eStyleUnit_Auto &&
      !(isFlexItem && isHorizontalFlexItem)) {
    minWidth = nsLayoutUtils::ComputeWidthValue(aRenderingContext,
                 aFrame, aCBSize.width, boxSizingAdjust.width,
                 boxSizingToMarginEdgeWidth, stylePos->mMinWidth);
  } else {
    // Treat "min-width: auto" as 0.
    // NOTE: Technically, "auto" is supposed to behave like "min-content" on
    // flex items. However, we don't need to worry about that here, because
    // flex items' min-sizes are intentionally ignored until the flex
    // container explicitly considers them during space distribution.
    minWidth = 0;
  }

  if (!isAutoHeight) {
    height = nsLayoutUtils::ComputeHeightValue(aCBSize.height, 
                boxSizingAdjust.height, 
                *heightStyleCoord);
  }

  if (!IsAutoHeight(stylePos->mMaxHeight, aCBSize.height) &&
      !(isFlexItem && !isHorizontalFlexItem)) {
    maxHeight = nsLayoutUtils::ComputeHeightValue(aCBSize.height, 
                  boxSizingAdjust.height, 
                  stylePos->mMaxHeight);
  } else {
    maxHeight = nscoord_MAX;
  }

  if (!IsAutoHeight(stylePos->mMinHeight, aCBSize.height) &&
      !(isFlexItem && !isHorizontalFlexItem)) {
    minHeight = nsLayoutUtils::ComputeHeightValue(aCBSize.height, 
                  boxSizingAdjust.height,
                  stylePos->mMinHeight);
  } else {
    minHeight = 0;
  }

  // Resolve percentage intrinsic width/height as necessary:

  NS_ASSERTION(aCBSize.width != NS_UNCONSTRAINEDSIZE,
               "Our containing block must not have unconstrained width!");

  bool hasIntrinsicWidth, hasIntrinsicHeight;
  nscoord intrinsicWidth, intrinsicHeight;

  if (aIntrinsicSize.width.GetUnit() == eStyleUnit_Coord) {
    hasIntrinsicWidth = true;
    intrinsicWidth = aIntrinsicSize.width.GetCoordValue();
    if (intrinsicWidth < 0)
      intrinsicWidth = 0;
  } else {
    NS_ASSERTION(aIntrinsicSize.width.GetUnit() == eStyleUnit_None,
                 "unexpected unit");
    hasIntrinsicWidth = false;
    intrinsicWidth = 0;
  }

  if (aIntrinsicSize.height.GetUnit() == eStyleUnit_Coord) {
    hasIntrinsicHeight = true;
    intrinsicHeight = aIntrinsicSize.height.GetCoordValue();
    if (intrinsicHeight < 0)
      intrinsicHeight = 0;
  } else {
    NS_ASSERTION(aIntrinsicSize.height.GetUnit() == eStyleUnit_None,
                 "unexpected unit");
    hasIntrinsicHeight = false;
    intrinsicHeight = 0;
  }

  NS_ASSERTION(aIntrinsicRatio.width >= 0 && aIntrinsicRatio.height >= 0,
               "Intrinsic ratio has a negative component!");

  // Now calculate the used values for width and height:

  if (isAutoWidth) {
    if (isAutoHeight) {

      // 'auto' width, 'auto' height

      // Get tentative values - CSS 2.1 sections 10.3.2 and 10.6.2:

      nscoord tentWidth, tentHeight;

      if (hasIntrinsicWidth) {
        tentWidth = intrinsicWidth;
      } else if (hasIntrinsicHeight && aIntrinsicRatio.height > 0) {
        tentWidth = MULDIV(intrinsicHeight, aIntrinsicRatio.width, aIntrinsicRatio.height);
      } else if (aIntrinsicRatio.width > 0) {
        tentWidth = aCBSize.width - boxSizingToMarginEdgeWidth; // XXX scrollbar?
        if (tentWidth < 0) tentWidth = 0;
      } else {
        tentWidth = nsPresContext::CSSPixelsToAppUnits(300);
      }

      if (hasIntrinsicHeight) {
        tentHeight = intrinsicHeight;
      } else if (aIntrinsicRatio.width > 0) {
        tentHeight = MULDIV(tentWidth, aIntrinsicRatio.height, aIntrinsicRatio.width);
      } else {
        tentHeight = nsPresContext::CSSPixelsToAppUnits(150);
      }

      return ComputeAutoSizeWithIntrinsicDimensions(minWidth, minHeight,
                                                    maxWidth, maxHeight,
                                                    tentWidth, tentHeight);
    } else {

      // 'auto' width, non-'auto' height
      height = NS_CSS_MINMAX(height, minHeight, maxHeight);
      if (aIntrinsicRatio.height > 0) {
        width = MULDIV(height, aIntrinsicRatio.width, aIntrinsicRatio.height);
      } else if (hasIntrinsicWidth) {
        width = intrinsicWidth;
      } else {
        width = nsPresContext::CSSPixelsToAppUnits(300);
      }
      width = NS_CSS_MINMAX(width, minWidth, maxWidth);

    }
  } else {
    if (isAutoHeight) {

      // non-'auto' width, 'auto' height
      width = NS_CSS_MINMAX(width, minWidth, maxWidth);
      if (aIntrinsicRatio.width > 0) {
        height = MULDIV(width, aIntrinsicRatio.height, aIntrinsicRatio.width);
      } else if (hasIntrinsicHeight) {
        height = intrinsicHeight;
      } else {
        height = nsPresContext::CSSPixelsToAppUnits(150);
      }
      height = NS_CSS_MINMAX(height, minHeight, maxHeight);

    } else {

      // non-'auto' width, non-'auto' height
      width = NS_CSS_MINMAX(width, minWidth, maxWidth);
      height = NS_CSS_MINMAX(height, minHeight, maxHeight);

    }
  }

  return nsSize(width, height);
}

nsSize
nsLayoutUtils::ComputeAutoSizeWithIntrinsicDimensions(nscoord minWidth, nscoord minHeight,
                                                      nscoord maxWidth, nscoord maxHeight,
                                                      nscoord tentWidth, nscoord tentHeight)
{
  // Now apply min/max-width/height - CSS 2.1 sections 10.4 and 10.7:

  if (minWidth > maxWidth)
    maxWidth = minWidth;
  if (minHeight > maxHeight)
    maxHeight = minHeight;

  nscoord heightAtMaxWidth, heightAtMinWidth,
          widthAtMaxHeight, widthAtMinHeight;

  if (tentWidth > 0) {
    heightAtMaxWidth = MULDIV(maxWidth, tentHeight, tentWidth);
    if (heightAtMaxWidth < minHeight)
      heightAtMaxWidth = minHeight;
    heightAtMinWidth = MULDIV(minWidth, tentHeight, tentWidth);
    if (heightAtMinWidth > maxHeight)
      heightAtMinWidth = maxHeight;
  } else {
    heightAtMaxWidth = heightAtMinWidth = NS_CSS_MINMAX(tentHeight, minHeight, maxHeight);
  }

  if (tentHeight > 0) {
    widthAtMaxHeight = MULDIV(maxHeight, tentWidth, tentHeight);
    if (widthAtMaxHeight < minWidth)
      widthAtMaxHeight = minWidth;
    widthAtMinHeight = MULDIV(minHeight, tentWidth, tentHeight);
    if (widthAtMinHeight > maxWidth)
      widthAtMinHeight = maxWidth;
  } else {
    widthAtMaxHeight = widthAtMinHeight = NS_CSS_MINMAX(tentWidth, minWidth, maxWidth);
  }

  // The table at http://www.w3.org/TR/CSS21/visudet.html#min-max-widths :

  nscoord width, height;

  if (tentWidth > maxWidth) {
    if (tentHeight > maxHeight) {
      if (int64_t(maxWidth) * int64_t(tentHeight) <=
          int64_t(maxHeight) * int64_t(tentWidth)) {
        width = maxWidth;
        height = heightAtMaxWidth;
      } else {
        width = widthAtMaxHeight;
        height = maxHeight;
      }
    } else {
      // This also covers "(w > max-width) and (h < min-height)" since in
      // that case (max-width/w < 1), and with (h < min-height):
      //   max(max-width * h/w, min-height) == min-height
      width = maxWidth;
      height = heightAtMaxWidth;
    }
  } else if (tentWidth < minWidth) {
    if (tentHeight < minHeight) {
      if (int64_t(minWidth) * int64_t(tentHeight) <=
          int64_t(minHeight) * int64_t(tentWidth)) {
        width = widthAtMinHeight;
        height = minHeight;
      } else {
        width = minWidth;
        height = heightAtMinWidth;
      }
    } else {
      // This also covers "(w < min-width) and (h > max-height)" since in
      // that case (min-width/w > 1), and with (h > max-height):
      //   min(min-width * h/w, max-height) == max-height
      width = minWidth;
      height = heightAtMinWidth;
    }
  } else {
    if (tentHeight > maxHeight) {
      width = widthAtMaxHeight;
      height = maxHeight;
    } else if (tentHeight < minHeight) {
      width = widthAtMinHeight;
      height = minHeight;
    } else {
      width = tentWidth;
      height = tentHeight;
    }
  }

  return nsSize(width, height);
}

/* static */ nscoord
nsLayoutUtils::MinWidthFromInline(nsIFrame* aFrame,
                                  nsRenderingContext* aRenderingContext)
{
  NS_ASSERTION(!nsLayoutUtils::IsContainerForFontSizeInflation(aFrame),
               "should not be container for font size inflation");

  nsIFrame::InlineMinWidthData data;
  DISPLAY_MIN_WIDTH(aFrame, data.prevLines);
  aFrame->AddInlineMinWidth(aRenderingContext, &data);
  data.ForceBreak(aRenderingContext);
  return data.prevLines;
}

/* static */ nscoord
nsLayoutUtils::PrefWidthFromInline(nsIFrame* aFrame,
                                   nsRenderingContext* aRenderingContext)
{
  NS_ASSERTION(!nsLayoutUtils::IsContainerForFontSizeInflation(aFrame),
               "should not be container for font size inflation");

  nsIFrame::InlinePrefWidthData data;
  DISPLAY_PREF_WIDTH(aFrame, data.prevLines);
  aFrame->AddInlinePrefWidth(aRenderingContext, &data);
  data.ForceBreak(aRenderingContext);
  return data.prevLines;
}

static nscolor
DarkenColor(nscolor aColor)
{
  uint16_t  hue, sat, value;
  uint8_t alpha;

  // convert the RBG to HSV so we can get the lightness (which is the v)
  NS_RGB2HSV(aColor, hue, sat, value, alpha);

  // The goal here is to send white to black while letting colored
  // stuff stay colored... So we adopt the following approach.
  // Something with sat = 0 should end up with value = 0.  Something
  // with a high sat can end up with a high value and it's ok.... At
  // the same time, we don't want to make things lighter.  Do
  // something simple, since it seems to work.
  if (value > sat) {
    value = sat;
    // convert this color back into the RGB color space.
    NS_HSV2RGB(aColor, hue, sat, value, alpha);
  }
  return aColor;
}

// Check whether we should darken text/decoration colors. We need to do this if
// background images and colors are being suppressed, because that means
// light text will not be visible against the (presumed light-colored) background.
static bool
ShouldDarkenColors(nsPresContext* aPresContext)
{
  return !aPresContext->GetBackgroundColorDraw() &&
         !aPresContext->GetBackgroundImageDraw();
}

nscolor
nsLayoutUtils::GetColor(nsIFrame* aFrame, nsCSSProperty aProperty)
{
  nscolor color = aFrame->GetVisitedDependentColor(aProperty);
  if (ShouldDarkenColors(aFrame->PresContext())) {
    color = DarkenColor(color);
  }
  return color;
}

gfxFloat
nsLayoutUtils::GetSnappedBaselineY(nsIFrame* aFrame, gfxContext* aContext,
                                   nscoord aY, nscoord aAscent)
{
  gfxFloat appUnitsPerDevUnit = aFrame->PresContext()->AppUnitsPerDevPixel();
  gfxFloat baseline = gfxFloat(aY) + aAscent;
  gfxRect putativeRect(0, baseline/appUnitsPerDevUnit, 1, 1);
  if (!aContext->UserToDevicePixelSnapped(putativeRect, true))
    return baseline;
  return aContext->DeviceToUser(putativeRect.TopLeft()).y * appUnitsPerDevUnit;
}

void
nsLayoutUtils::DrawString(const nsIFrame*      aFrame,
                          nsRenderingContext* aContext,
                          const PRUnichar*     aString,
                          int32_t              aLength,
                          nsPoint              aPoint,
                          uint8_t              aDirection)
{
#ifdef IBMBIDI
  nsresult rv = NS_ERROR_FAILURE;
  nsPresContext* presContext = aFrame->PresContext();
  if (presContext->BidiEnabled()) {
    if (aDirection == NS_STYLE_DIRECTION_INHERIT) {
      aDirection = aFrame->GetStyleVisibility()->mDirection;
    }
    nsBidiDirection direction =
      (NS_STYLE_DIRECTION_RTL == aDirection) ?
      NSBIDI_RTL : NSBIDI_LTR;
    rv = nsBidiPresUtils::RenderText(aString, aLength, direction,
                                     presContext, *aContext, *aContext,
                                     aPoint.x, aPoint.y);
  }
  if (NS_FAILED(rv))
#endif // IBMBIDI
  {
    aContext->SetTextRunRTL(false);
    aContext->DrawString(aString, aLength, aPoint.x, aPoint.y);
  }
}

nscoord
nsLayoutUtils::GetStringWidth(const nsIFrame*      aFrame,
                              nsRenderingContext* aContext,
                              const PRUnichar*     aString,
                              int32_t              aLength)
{
#ifdef IBMBIDI
  nsPresContext* presContext = aFrame->PresContext();
  if (presContext->BidiEnabled()) {
    const nsStyleVisibility* vis = aFrame->GetStyleVisibility();
    nsBidiDirection direction =
      (NS_STYLE_DIRECTION_RTL == vis->mDirection) ?
      NSBIDI_RTL : NSBIDI_LTR;
    return nsBidiPresUtils::MeasureTextWidth(aString, aLength,
                                             direction, presContext, *aContext);
  }
#endif // IBMBIDI
  aContext->SetTextRunRTL(false);
  return aContext->GetWidth(aString, aLength);
}

/* static */ void
nsLayoutUtils::PaintTextShadow(const nsIFrame* aFrame,
                               nsRenderingContext* aContext,
                               const nsRect& aTextRect,
                               const nsRect& aDirtyRect,
                               const nscolor& aForegroundColor,
                               TextShadowCallback aCallback,
                               void* aCallbackData)
{
  const nsStyleText* textStyle = aFrame->GetStyleText();
  if (!textStyle->HasTextShadow(aFrame))
    return;

  // Text shadow happens with the last value being painted at the back,
  // ie. it is painted first.
  gfxContext* aDestCtx = aContext->ThebesContext();
  for (uint32_t i = textStyle->mTextShadow->Length(); i > 0; --i) {
    nsCSSShadowItem* shadowDetails = textStyle->mTextShadow->ShadowAt(i - 1);
    nsPoint shadowOffset(shadowDetails->mXOffset,
                         shadowDetails->mYOffset);
    nscoord blurRadius = NS_MAX(shadowDetails->mRadius, 0);

    nsRect shadowRect(aTextRect);
    shadowRect.MoveBy(shadowOffset);

    nsPresContext* presCtx = aFrame->PresContext();
    nsContextBoxBlur contextBoxBlur;
    gfxContext* shadowContext = contextBoxBlur.Init(shadowRect, 0, blurRadius,
                                                    presCtx->AppUnitsPerDevPixel(),
                                                    aDestCtx, aDirtyRect, nullptr);
    if (!shadowContext)
      continue;

    nscolor shadowColor;
    if (shadowDetails->mHasColor)
      shadowColor = shadowDetails->mColor;
    else
      shadowColor = aForegroundColor;

    // Conjure an nsRenderingContext from a gfxContext for drawing the text
    // to blur.
    nsRefPtr<nsRenderingContext> renderingContext = new nsRenderingContext();
    renderingContext->Init(presCtx->DeviceContext(), shadowContext);

    aDestCtx->Save();
    aDestCtx->NewPath();
    aDestCtx->SetColor(gfxRGBA(shadowColor));

    // The callback will draw whatever we want to blur as a shadow.
    aCallback(renderingContext, shadowOffset, shadowColor, aCallbackData);

    contextBoxBlur.DoPaint();
    aDestCtx->Restore();
  }
}

/* static */ nscoord
nsLayoutUtils::GetCenteredFontBaseline(nsFontMetrics* aFontMetrics,
                                       nscoord         aLineHeight)
{
  nscoord fontAscent = aFontMetrics->MaxAscent();
  nscoord fontHeight = aFontMetrics->MaxHeight();

  nscoord leading = aLineHeight - fontHeight;
  return fontAscent + leading/2;
}


/* static */ bool
nsLayoutUtils::GetFirstLineBaseline(const nsIFrame* aFrame, nscoord* aResult)
{
  LinePosition position;
  if (!GetFirstLinePosition(aFrame, &position))
    return false;
  *aResult = position.mBaseline;
  return true;
}

/* static */ bool
nsLayoutUtils::GetFirstLinePosition(const nsIFrame* aFrame,
                                    LinePosition* aResult)
{
  const nsBlockFrame* block = nsLayoutUtils::GetAsBlock(const_cast<nsIFrame*>(aFrame));
  if (!block) {
    // For the first-line baseline we also have to check for a table, and if
    // so, use the baseline of its first row.
    nsIAtom* fType = aFrame->GetType();
    if (fType == nsGkAtoms::tableOuterFrame) {
      aResult->mTop = 0;
      aResult->mBaseline = aFrame->GetBaseline();
      // This is what we want for the list bullet caller; not sure if
      // other future callers will want the same.
      aResult->mBottom = aFrame->GetSize().height;
      return true;
    }

    // For first-line baselines, we have to consider scroll frames.
    if (fType == nsGkAtoms::scrollFrame) {
      nsIScrollableFrame *sFrame = do_QueryFrame(const_cast<nsIFrame*>(aFrame));
      if (!sFrame) {
        NS_NOTREACHED("not scroll frame");
      }
      LinePosition kidPosition;
      if (GetFirstLinePosition(sFrame->GetScrolledFrame(), &kidPosition)) {
        // Consider only the border and padding that contributes to the
        // kid's position, not the scrolling, so we get the initial
        // position.
        *aResult = kidPosition + aFrame->GetUsedBorderAndPadding().top;
        return true;
      }
      return false;
    }

    if (fType == nsGkAtoms::fieldSetFrame) {
      LinePosition kidPosition;
      nsIFrame* kid = aFrame->GetFirstPrincipalChild();
      // kid might be a legend frame here, but that's ok.
      if (GetFirstLinePosition(kid, &kidPosition)) {
        *aResult = kidPosition + (kid->GetPosition().y -
                                  kid->GetRelativeOffset().y);
        return true;
      }
      return false;
    }

    // No baseline.
    return false;
  }

  for (nsBlockFrame::const_line_iterator line = block->begin_lines(),
                                     line_end = block->end_lines();
       line != line_end; ++line) {
    if (line->IsBlock()) {
      nsIFrame *kid = line->mFirstChild;
      LinePosition kidPosition;
      if (GetFirstLinePosition(kid, &kidPosition)) {
        *aResult = kidPosition + (kid->GetPosition().y -
                                  kid->GetRelativeOffset().y);
        return true;
      }
    } else {
      // XXX Is this the right test?  We have some bogus empty lines
      // floating around, but IsEmpty is perhaps too weak.
      if (line->GetHeight() != 0 || !line->IsEmpty()) {
        nscoord top = line->mBounds.y;
        aResult->mTop = top;
        aResult->mBaseline = top + line->GetAscent();
        aResult->mBottom = top + line->GetHeight();
        return true;
      }
    }
  }
  return false;
}

/* static */ bool
nsLayoutUtils::GetLastLineBaseline(const nsIFrame* aFrame, nscoord* aResult)
{
  const nsBlockFrame* block = nsLayoutUtils::GetAsBlock(const_cast<nsIFrame*>(aFrame));
  if (!block)
    // No baseline.  (We intentionally don't descend into scroll frames.)
    return false;

  for (nsBlockFrame::const_reverse_line_iterator line = block->rbegin_lines(),
                                             line_end = block->rend_lines();
       line != line_end; ++line) {
    if (line->IsBlock()) {
      nsIFrame *kid = line->mFirstChild;
      nscoord kidBaseline;
      if (GetLastLineBaseline(kid, &kidBaseline)) {
        // Ignore relative positioning for baseline calculations
        *aResult = kidBaseline + kid->GetPosition().y -
          kid->GetRelativeOffset().y;
        return true;
      } else if (kid->GetType() == nsGkAtoms::scrollFrame) {
        // Use the bottom of the scroll frame.
        // XXX CSS2.1 really doesn't say what to do here.
        *aResult = kid->GetRect().YMost() - kid->GetRelativeOffset().y;
        return true;
      }
    } else {
      // XXX Is this the right test?  We have some bogus empty lines
      // floating around, but IsEmpty is perhaps too weak.
      if (line->GetHeight() != 0 || !line->IsEmpty()) {
        *aResult = line->mBounds.y + line->GetAscent();
        return true;
      }
    }
  }
  return false;
}

static nscoord
CalculateBlockContentBottom(nsBlockFrame* aFrame)
{
  NS_PRECONDITION(aFrame, "null ptr");

  nscoord contentBottom = 0;

  for (nsBlockFrame::line_iterator line = aFrame->begin_lines(),
                                   line_end = aFrame->end_lines();
       line != line_end; ++line) {
    if (line->IsBlock()) {
      nsIFrame* child = line->mFirstChild;
      nscoord offset = child->GetRect().y - child->GetRelativeOffset().y;
      contentBottom = NS_MAX(contentBottom,
                        nsLayoutUtils::CalculateContentBottom(child) + offset);
    }
    else {
      contentBottom = NS_MAX(contentBottom, line->mBounds.YMost());
    }
  }
  return contentBottom;
}

/* static */ nscoord
nsLayoutUtils::CalculateContentBottom(nsIFrame* aFrame)
{
  NS_PRECONDITION(aFrame, "null ptr");

  nscoord contentBottom = aFrame->GetRect().height;

  // We want scrollable overflow rather than visual because this
  // calculation is intended to affect layout.
  if (aFrame->GetScrollableOverflowRect().height > contentBottom) {
    nsIFrame::ChildListIDs skip(nsIFrame::kOverflowList |
                                nsIFrame::kExcessOverflowContainersList |
                                nsIFrame::kOverflowOutOfFlowList);
    nsBlockFrame* blockFrame = GetAsBlock(aFrame);
    if (blockFrame) {
      contentBottom =
        NS_MAX(contentBottom, CalculateBlockContentBottom(blockFrame));
      skip |= nsIFrame::kPrincipalList;
    }
    nsIFrame::ChildListIterator lists(aFrame);
    for (; !lists.IsDone(); lists.Next()) {
      if (!skip.Contains(lists.CurrentID())) {
        nsFrameList::Enumerator childFrames(lists.CurrentList()); 
        for (; !childFrames.AtEnd(); childFrames.Next()) {
          nsIFrame* child = childFrames.get();
          nscoord offset = child->GetRect().y - child->GetRelativeOffset().y;
          contentBottom = NS_MAX(contentBottom,
                                 CalculateContentBottom(child) + offset);
        }
      }
    }
  }
  return contentBottom;
}

/* static */ nsIFrame*
nsLayoutUtils::GetClosestLayer(nsIFrame* aFrame)
{
  nsIFrame* layer;
  for (layer = aFrame; layer; layer = layer->GetParent()) {
    if (layer->IsPositioned() ||
        (layer->GetParent() &&
          layer->GetParent()->GetType() == nsGkAtoms::scrollFrame))
      break;
  }
  if (layer)
    return layer;
  return aFrame->PresContext()->PresShell()->FrameManager()->GetRootFrame();
}

GraphicsFilter
nsLayoutUtils::GetGraphicsFilterForFrame(nsIFrame* aForFrame)
{
  GraphicsFilter defaultFilter = gfxPattern::FILTER_GOOD;
  nsIFrame *frame = nsCSSRendering::IsCanvasFrame(aForFrame) ?
    nsCSSRendering::FindBackgroundStyleFrame(aForFrame) : aForFrame;

  switch (frame->GetStyleSVG()->mImageRendering) {
  case NS_STYLE_IMAGE_RENDERING_OPTIMIZESPEED:
    return gfxPattern::FILTER_FAST;
  case NS_STYLE_IMAGE_RENDERING_OPTIMIZEQUALITY:
    return gfxPattern::FILTER_BEST;
  case NS_STYLE_IMAGE_RENDERING_CRISPEDGES:
    return gfxPattern::FILTER_NEAREST;
  default:
    return defaultFilter;
  }
}

/**
 * Given an image being drawn into an appunit coordinate system, and
 * a point in that coordinate system, map the point back into image
 * pixel space.
 * @param aSize the size of the image, in pixels
 * @param aDest the rectangle that the image is being mapped into
 * @param aPt a point in the same coordinate system as the rectangle
 */
static gfxPoint
MapToFloatImagePixels(const gfxSize& aSize,
                      const gfxRect& aDest, const gfxPoint& aPt)
{
  return gfxPoint(((aPt.x - aDest.X())*aSize.width)/aDest.Width(),
                  ((aPt.y - aDest.Y())*aSize.height)/aDest.Height());
}

/**
 * Given an image being drawn into an pixel-based coordinate system, and
 * a point in image space, map the point into the pixel-based coordinate
 * system.
 * @param aSize the size of the image, in pixels
 * @param aDest the rectangle that the image is being mapped into
 * @param aPt a point in image space
 */
static gfxPoint
MapToFloatUserPixels(const gfxSize& aSize,
                     const gfxRect& aDest, const gfxPoint& aPt)
{
  return gfxPoint(aPt.x*aDest.Width()/aSize.width + aDest.X(),
                  aPt.y*aDest.Height()/aSize.height + aDest.Y());
}

/* static */ gfxRect
nsLayoutUtils::RectToGfxRect(const nsRect& aRect, int32_t aAppUnitsPerDevPixel)
{
  return gfxRect(gfxFloat(aRect.x) / aAppUnitsPerDevPixel,
                 gfxFloat(aRect.y) / aAppUnitsPerDevPixel,
                 gfxFloat(aRect.width) / aAppUnitsPerDevPixel,
                 gfxFloat(aRect.height) / aAppUnitsPerDevPixel);
}

struct SnappedImageDrawingParameters {
  // A transform from either device space or user space (depending on mResetCTM)
  // to image space
  gfxMatrix mUserSpaceToImageSpace;
  // A device-space, pixel-aligned rectangle to fill
  gfxRect mFillRect;
  // A pixel rectangle in tiled image space outside of which gfx should not
  // sample (using EXTEND_PAD as necessary)
  nsIntRect mSubimage;
  // Whether there's anything to draw at all
  bool mShouldDraw;
  // true iff the CTM of the rendering context needs to be reset to the
  // identity matrix before drawing
  bool mResetCTM;

  SnappedImageDrawingParameters()
   : mShouldDraw(false)
   , mResetCTM(false)
  {}

  SnappedImageDrawingParameters(const gfxMatrix& aUserSpaceToImageSpace,
                                const gfxRect&   aFillRect,
                                const nsIntRect& aSubimage,
                                bool             aResetCTM)
   : mUserSpaceToImageSpace(aUserSpaceToImageSpace)
   , mFillRect(aFillRect)
   , mSubimage(aSubimage)
   , mShouldDraw(true)
   , mResetCTM(aResetCTM)
  {}
};

/**
 * Given a set of input parameters, compute certain output parameters
 * for drawing an image with the image snapping algorithm.
 * See https://wiki.mozilla.org/Gecko:Image_Snapping_and_Rendering
 *
 *  @see nsLayoutUtils::DrawImage() for the descriptions of input parameters
 */
static SnappedImageDrawingParameters
ComputeSnappedImageDrawingParameters(gfxContext*     aCtx,
                                     int32_t         aAppUnitsPerDevPixel,
                                     const nsRect    aDest,
                                     const nsRect    aFill,
                                     const nsPoint   aAnchor,
                                     const nsRect    aDirty,
                                     const nsIntSize aImageSize)

{
  if (aDest.IsEmpty() || aFill.IsEmpty() || !aImageSize.width || !aImageSize.height)
    return SnappedImageDrawingParameters();

  gfxRect devPixelDest =
    nsLayoutUtils::RectToGfxRect(aDest, aAppUnitsPerDevPixel);
  gfxRect devPixelFill =
    nsLayoutUtils::RectToGfxRect(aFill, aAppUnitsPerDevPixel);
  gfxRect devPixelDirty =
    nsLayoutUtils::RectToGfxRect(aDirty, aAppUnitsPerDevPixel);

  bool ignoreScale = false;
#ifdef MOZ_GFX_OPTIMIZE_MOBILE
  ignoreScale = true;
#endif
  gfxRect fill = devPixelFill;
  bool didSnap = aCtx->UserToDevicePixelSnapped(fill, ignoreScale);
  gfxMatrix currentMatrix = aCtx->CurrentMatrix();
  if (didSnap && currentMatrix.HasNonAxisAlignedTransform()) {
    // currentMatrix must have some rotation by a multiple of 90 degrees.
    didSnap = false;
    fill = devPixelFill;
  }

  gfxSize imageSize(aImageSize.width, aImageSize.height);

  // Compute the set of pixels that would be sampled by an ideal rendering
  gfxPoint subimageTopLeft =
    MapToFloatImagePixels(imageSize, devPixelDest, devPixelFill.TopLeft());
  gfxPoint subimageBottomRight =
    MapToFloatImagePixels(imageSize, devPixelDest, devPixelFill.BottomRight());
  nsIntRect intSubimage;
  intSubimage.MoveTo(NSToIntFloor(subimageTopLeft.x),
                     NSToIntFloor(subimageTopLeft.y));
  intSubimage.SizeTo(NSToIntCeil(subimageBottomRight.x) - intSubimage.x,
                     NSToIntCeil(subimageBottomRight.y) - intSubimage.y);

  // Compute the anchor point and compute final fill rect.
  // This code assumes that pixel-based devices have one pixel per
  // device unit!
  gfxPoint anchorPoint(gfxFloat(aAnchor.x)/aAppUnitsPerDevPixel,
                       gfxFloat(aAnchor.y)/aAppUnitsPerDevPixel);
  gfxPoint imageSpaceAnchorPoint =
    MapToFloatImagePixels(imageSize, devPixelDest, anchorPoint);

  if (didSnap) {
    imageSpaceAnchorPoint.Round();
    anchorPoint = imageSpaceAnchorPoint;
    anchorPoint = MapToFloatUserPixels(imageSize, devPixelDest, anchorPoint);
    anchorPoint = currentMatrix.Transform(anchorPoint);
    anchorPoint.Round();

    // This form of Transform is safe to call since non-axis-aligned
    // transforms wouldn't be snapped.
    devPixelDirty = currentMatrix.Transform(devPixelDirty);
  }

  gfxFloat scaleX = imageSize.width*aAppUnitsPerDevPixel/aDest.width;
  gfxFloat scaleY = imageSize.height*aAppUnitsPerDevPixel/aDest.height;
  if (didSnap) {
    // We'll reset aCTX to the identity matrix before drawing, so we need to
    // adjust our scales to match.
    scaleX /= currentMatrix.xx;
    scaleY /= currentMatrix.yy;
  }
  gfxFloat translateX = imageSpaceAnchorPoint.x - anchorPoint.x*scaleX;
  gfxFloat translateY = imageSpaceAnchorPoint.y - anchorPoint.y*scaleY;
  gfxMatrix transform(scaleX, 0, 0, scaleY, translateX, translateY);

  gfxRect finalFillRect = fill;
  // If the user-space-to-image-space transform is not a straight
  // translation by integers, then filtering will occur, and
  // restricting the fill rect to the dirty rect would change the values
  // computed for edge pixels, which we can't allow.
  // Also, if didSnap is false then rounding out 'devPixelDirty' might not
  // produce pixel-aligned coordinates, which would also break the values
  // computed for edge pixels.
  if (didSnap && !transform.HasNonIntegerTranslation()) {
    devPixelDirty.RoundOut();
    finalFillRect = fill.Intersect(devPixelDirty);
  }
  if (finalFillRect.IsEmpty())
    return SnappedImageDrawingParameters();

  return SnappedImageDrawingParameters(transform, finalFillRect, intSubimage,
                                       didSnap);
}


static nsresult
DrawImageInternal(nsRenderingContext* aRenderingContext,
                  imgIContainer*       aImage,
                  GraphicsFilter       aGraphicsFilter,
                  const nsRect&        aDest,
                  const nsRect&        aFill,
                  const nsPoint&       aAnchor,
                  const nsRect&        aDirty,
                  const nsIntSize&     aImageSize,
                  uint32_t             aImageFlags)
{
  if (aDest.Contains(aFill)) {
    aImageFlags |= imgIContainer::FLAG_CLAMP;
  }
  int32_t appUnitsPerDevPixel = aRenderingContext->AppUnitsPerDevPixel();
  gfxContext* ctx = aRenderingContext->ThebesContext();

  SnappedImageDrawingParameters drawingParams =
    ComputeSnappedImageDrawingParameters(ctx, appUnitsPerDevPixel, aDest, aFill,
                                         aAnchor, aDirty, aImageSize);

  if (!drawingParams.mShouldDraw)
    return NS_OK;

  gfxContextMatrixAutoSaveRestore saveMatrix(ctx);
  if (drawingParams.mResetCTM) {
    ctx->IdentityMatrix();
  }

  aImage->Draw(ctx, aGraphicsFilter, drawingParams.mUserSpaceToImageSpace,
               drawingParams.mFillRect, drawingParams.mSubimage, aImageSize,
               aImageFlags);
  return NS_OK;
}

/* static */ void
nsLayoutUtils::DrawPixelSnapped(nsRenderingContext* aRenderingContext,
                                gfxDrawable*         aDrawable,
                                GraphicsFilter       aFilter,
                                const nsRect&        aDest,
                                const nsRect&        aFill,
                                const nsPoint&       aAnchor,
                                const nsRect&        aDirty)
{
  int32_t appUnitsPerDevPixel = aRenderingContext->AppUnitsPerDevPixel();
  gfxContext* ctx = aRenderingContext->ThebesContext();
  gfxIntSize drawableSize = aDrawable->Size();
  nsIntSize imageSize(drawableSize.width, drawableSize.height);

  SnappedImageDrawingParameters drawingParams =
    ComputeSnappedImageDrawingParameters(ctx, appUnitsPerDevPixel, aDest, aFill,
                                         aAnchor, aDirty, imageSize);

  if (!drawingParams.mShouldDraw)
    return;

  gfxContextMatrixAutoSaveRestore saveMatrix(ctx);
  if (drawingParams.mResetCTM) {
    ctx->IdentityMatrix();
  }

  gfxRect sourceRect =
    drawingParams.mUserSpaceToImageSpace.Transform(drawingParams.mFillRect);
  gfxRect imageRect(0, 0, imageSize.width, imageSize.height);
  gfxRect subimage(drawingParams.mSubimage.x, drawingParams.mSubimage.y,
                   drawingParams.mSubimage.width, drawingParams.mSubimage.height);

  NS_ASSERTION(!sourceRect.Intersect(subimage).IsEmpty(),
               "We must be allowed to sample *some* source pixels!");

  gfxUtils::DrawPixelSnapped(ctx, aDrawable,
                             drawingParams.mUserSpaceToImageSpace, subimage,
                             sourceRect, imageRect, drawingParams.mFillRect,
                             gfxASurface::ImageFormatARGB32, aFilter);
}

/* static */ nsresult
nsLayoutUtils::DrawSingleUnscaledImage(nsRenderingContext* aRenderingContext,
                                       imgIContainer*       aImage,
                                       GraphicsFilter       aGraphicsFilter,
                                       const nsPoint&       aDest,
                                       const nsRect*        aDirty,
                                       uint32_t             aImageFlags,
                                       const nsRect*        aSourceArea)
{
  nsIntSize imageSize;
  aImage->GetWidth(&imageSize.width);
  aImage->GetHeight(&imageSize.height);
  NS_ENSURE_TRUE(imageSize.width > 0 && imageSize.height > 0, NS_ERROR_FAILURE);

  nscoord appUnitsPerCSSPixel = nsDeviceContext::AppUnitsPerCSSPixel();
  nsSize size(imageSize.width*appUnitsPerCSSPixel,
              imageSize.height*appUnitsPerCSSPixel);

  nsRect source;
  if (aSourceArea) {
    source = *aSourceArea;
  } else {
    source.SizeTo(size);
  }

  nsRect dest(aDest - source.TopLeft(), size);
  nsRect fill(aDest, source.Size());
  // Ensure that only a single image tile is drawn. If aSourceArea extends
  // outside the image bounds, we want to honor the aSourceArea-to-aDest
  // translation but we don't want to actually tile the image.
  fill.IntersectRect(fill, dest);
  return DrawImageInternal(aRenderingContext, aImage, aGraphicsFilter,
                           dest, fill, aDest, aDirty ? *aDirty : dest,
                           imageSize, aImageFlags);
}

/* static */ nsresult
nsLayoutUtils::DrawSingleImage(nsRenderingContext* aRenderingContext,
                               imgIContainer*       aImage,
                               GraphicsFilter       aGraphicsFilter,
                               const nsRect&        aDest,
                               const nsRect&        aDirty,
                               uint32_t             aImageFlags,
                               const nsRect*        aSourceArea)
{
  nsIntSize imageSize;
  if (aImage->GetType() == imgIContainer::TYPE_VECTOR) {
    imageSize.width  = nsPresContext::AppUnitsToIntCSSPixels(aDest.width);
    imageSize.height = nsPresContext::AppUnitsToIntCSSPixels(aDest.height);
  } else {
    aImage->GetWidth(&imageSize.width);
    aImage->GetHeight(&imageSize.height);
  }
  NS_ENSURE_TRUE(imageSize.width > 0 && imageSize.height > 0, NS_ERROR_FAILURE);

  nsRect source;
  if (aSourceArea) {
    source = *aSourceArea;
  } else {
    nscoord appUnitsPerCSSPixel = nsDeviceContext::AppUnitsPerCSSPixel();
    source.SizeTo(imageSize.width*appUnitsPerCSSPixel,
                  imageSize.height*appUnitsPerCSSPixel);
  }

  nsRect dest = nsLayoutUtils::GetWholeImageDestination(imageSize, source,
                                                        aDest);
  // Ensure that only a single image tile is drawn. If aSourceArea extends
  // outside the image bounds, we want to honor the aSourceArea-to-aDest
  // transform but we don't want to actually tile the image.
  nsRect fill;
  fill.IntersectRect(aDest, dest);
  return DrawImageInternal(aRenderingContext, aImage, aGraphicsFilter, dest, fill,
                           fill.TopLeft(), aDirty, imageSize, aImageFlags);
}

/* static */ void
nsLayoutUtils::ComputeSizeForDrawing(imgIContainer *aImage,
                                     nsIntSize&     aImageSize, /*outparam*/
                                     nsSize&        aIntrinsicRatio, /*outparam*/
                                     bool&          aGotWidth,  /*outparam*/
                                     bool&          aGotHeight  /*outparam*/)
{
  aGotWidth  = NS_SUCCEEDED(aImage->GetWidth(&aImageSize.width));
  aGotHeight = NS_SUCCEEDED(aImage->GetHeight(&aImageSize.height));

  if (aGotWidth && aGotHeight) {
    aIntrinsicRatio = nsSize(aImageSize.width, aImageSize.height);
    return;
  }

  // If we failed to get width or height, we either have a vector image and
  // should return its intrinsic ratio, or we hit an error (say, because the
  // image failed to load or couldn't be decoded) and should return zero size.
  if (nsIFrame* rootFrame = aImage->GetRootLayoutFrame()) {
    aIntrinsicRatio = rootFrame->GetIntrinsicRatio();
  } else {
    aGotWidth = aGotHeight = true;
    aImageSize = nsIntSize(0, 0);
    aIntrinsicRatio = nsSize(0, 0);
  }
}


/* static */ nsresult
nsLayoutUtils::DrawBackgroundImage(nsRenderingContext* aRenderingContext,
                                   imgIContainer*      aImage,
                                   const nsIntSize&    aImageSize,
                                   GraphicsFilter      aGraphicsFilter,
                                   const nsRect&       aDest,
                                   const nsRect&       aFill,
                                   const nsPoint&      aAnchor,
                                   const nsRect&       aDirty,
                                   uint32_t            aImageFlags)
{
  SAMPLE_LABEL("layout", "nsLayoutUtils::DrawBackgroundImage");

  if (UseBackgroundNearestFiltering()) {
    aGraphicsFilter = gfxPattern::FILTER_NEAREST;
  }

  return DrawImageInternal(aRenderingContext, aImage, aGraphicsFilter,
                           aDest, aFill, aAnchor, aDirty,
                           aImageSize, aImageFlags);
}

/* static */ nsresult
nsLayoutUtils::DrawImage(nsRenderingContext* aRenderingContext,
                         imgIContainer*       aImage,
                         GraphicsFilter       aGraphicsFilter,
                         const nsRect&        aDest,
                         const nsRect&        aFill,
                         const nsPoint&       aAnchor,
                         const nsRect&        aDirty,
                         uint32_t             aImageFlags)
{
  nsIntSize imageSize;
  nsSize imageRatio;
  bool gotHeight, gotWidth;
  ComputeSizeForDrawing(aImage, imageSize, imageRatio, gotWidth, gotHeight);

  // XXX Dimensionless images shouldn't fall back to filled-area size -- the
  //     caller should provide the image size, a la DrawBackgroundImage.
  if (gotWidth != gotHeight) {
    if (!gotWidth) {
      if (imageRatio.height != 0) {
        imageSize.width =
          NSCoordSaturatingNonnegativeMultiply(imageSize.height,
                                               float(imageRatio.width) /
                                               float(imageRatio.height));
        gotWidth = true;
      }
    } else {
      if (imageRatio.width != 0) {
        imageSize.height =
          NSCoordSaturatingNonnegativeMultiply(imageSize.width,
                                               float(imageRatio.height) /
                                               float(imageRatio.width));
        gotHeight = true;
      }
    }
  }

  if (!gotWidth) {
    imageSize.width = nsPresContext::AppUnitsToIntCSSPixels(aFill.width);
  }
  if (!gotHeight) {
    imageSize.height = nsPresContext::AppUnitsToIntCSSPixels(aFill.height);
  }

  return DrawImageInternal(aRenderingContext, aImage, aGraphicsFilter,
                           aDest, aFill, aAnchor, aDirty,
                           imageSize, aImageFlags);
}

/* static */ nsRect
nsLayoutUtils::GetWholeImageDestination(const nsIntSize& aWholeImageSize,
                                        const nsRect& aImageSourceArea,
                                        const nsRect& aDestArea)
{
  double scaleX = double(aDestArea.width)/aImageSourceArea.width;
  double scaleY = double(aDestArea.height)/aImageSourceArea.height;
  nscoord destOffsetX = NSToCoordRound(aImageSourceArea.x*scaleX);
  nscoord destOffsetY = NSToCoordRound(aImageSourceArea.y*scaleY);
  nscoord appUnitsPerCSSPixel = nsDeviceContext::AppUnitsPerCSSPixel();
  nscoord wholeSizeX = NSToCoordRound(aWholeImageSize.width*appUnitsPerCSSPixel*scaleX);
  nscoord wholeSizeY = NSToCoordRound(aWholeImageSize.height*appUnitsPerCSSPixel*scaleY);
  return nsRect(aDestArea.TopLeft() - nsPoint(destOffsetX, destOffsetY),
                nsSize(wholeSizeX, wholeSizeY));
}

static bool NonZeroStyleCoord(const nsStyleCoord& aCoord)
{
  if (aCoord.IsCoordPercentCalcUnit()) {
    // Since negative results are clamped to 0, check > 0.
    return nsRuleNode::ComputeCoordPercentCalc(aCoord, nscoord_MAX) > 0 ||
           nsRuleNode::ComputeCoordPercentCalc(aCoord, 0) > 0;
  }

  return true;
}

/* static */ bool
nsLayoutUtils::HasNonZeroCorner(const nsStyleCorners& aCorners)
{
  NS_FOR_CSS_HALF_CORNERS(corner) {
    if (NonZeroStyleCoord(aCorners.Get(corner)))
      return true;
  }
  return false;
}

// aCorner is a "full corner" value, i.e. NS_CORNER_TOP_LEFT etc
static bool IsCornerAdjacentToSide(uint8_t aCorner, css::Side aSide)
{
  PR_STATIC_ASSERT((int)NS_SIDE_TOP == NS_CORNER_TOP_LEFT);
  PR_STATIC_ASSERT((int)NS_SIDE_RIGHT == NS_CORNER_TOP_RIGHT);
  PR_STATIC_ASSERT((int)NS_SIDE_BOTTOM == NS_CORNER_BOTTOM_RIGHT);
  PR_STATIC_ASSERT((int)NS_SIDE_LEFT == NS_CORNER_BOTTOM_LEFT);
  PR_STATIC_ASSERT((int)NS_SIDE_TOP == ((NS_CORNER_TOP_RIGHT - 1)&3));
  PR_STATIC_ASSERT((int)NS_SIDE_RIGHT == ((NS_CORNER_BOTTOM_RIGHT - 1)&3));
  PR_STATIC_ASSERT((int)NS_SIDE_BOTTOM == ((NS_CORNER_BOTTOM_LEFT - 1)&3));
  PR_STATIC_ASSERT((int)NS_SIDE_LEFT == ((NS_CORNER_TOP_LEFT - 1)&3));

  return aSide == aCorner || aSide == ((aCorner - 1)&3);
}

/* static */ bool
nsLayoutUtils::HasNonZeroCornerOnSide(const nsStyleCorners& aCorners,
                                      css::Side aSide)
{
  PR_STATIC_ASSERT(NS_CORNER_TOP_LEFT_X/2 == NS_CORNER_TOP_LEFT);
  PR_STATIC_ASSERT(NS_CORNER_TOP_LEFT_Y/2 == NS_CORNER_TOP_LEFT);
  PR_STATIC_ASSERT(NS_CORNER_TOP_RIGHT_X/2 == NS_CORNER_TOP_RIGHT);
  PR_STATIC_ASSERT(NS_CORNER_TOP_RIGHT_Y/2 == NS_CORNER_TOP_RIGHT);
  PR_STATIC_ASSERT(NS_CORNER_BOTTOM_RIGHT_X/2 == NS_CORNER_BOTTOM_RIGHT);
  PR_STATIC_ASSERT(NS_CORNER_BOTTOM_RIGHT_Y/2 == NS_CORNER_BOTTOM_RIGHT);
  PR_STATIC_ASSERT(NS_CORNER_BOTTOM_LEFT_X/2 == NS_CORNER_BOTTOM_LEFT);
  PR_STATIC_ASSERT(NS_CORNER_BOTTOM_LEFT_Y/2 == NS_CORNER_BOTTOM_LEFT);

  NS_FOR_CSS_HALF_CORNERS(corner) {
    // corner is a "half corner" value, so dividing by two gives us a
    // "full corner" value.
    if (NonZeroStyleCoord(aCorners.Get(corner)) &&
        IsCornerAdjacentToSide(corner/2, aSide))
      return true;
  }
  return false;
}

/* static */ nsTransparencyMode
nsLayoutUtils::GetFrameTransparency(nsIFrame* aBackgroundFrame,
                                    nsIFrame* aCSSRootFrame) {
  if (aCSSRootFrame->GetStyleContext()->GetStyleDisplay()->mOpacity < 1.0f)
    return eTransparencyTransparent;

  if (HasNonZeroCorner(aCSSRootFrame->GetStyleContext()->GetStyleBorder()->mBorderRadius))
    return eTransparencyTransparent;

  if (aCSSRootFrame->GetStyleDisplay()->mAppearance == NS_THEME_WIN_GLASS)
    return eTransparencyGlass;

  if (aCSSRootFrame->GetStyleDisplay()->mAppearance == NS_THEME_WIN_BORDERLESS_GLASS)
    return eTransparencyBorderlessGlass;

  nsITheme::Transparency transparency;
  if (aCSSRootFrame->IsThemed(&transparency))
    return transparency == nsITheme::eTransparent
         ? eTransparencyTransparent
         : eTransparencyOpaque;

  // We need an uninitialized window to be treated as opaque because
  // doing otherwise breaks window display effects on some platforms,
  // specifically Vista. (bug 450322)
  if (aBackgroundFrame->GetType() == nsGkAtoms::viewportFrame &&
      !aBackgroundFrame->GetFirstPrincipalChild()) {
    return eTransparencyOpaque;
  }

  nsStyleContext* bgSC;
  if (!nsCSSRendering::FindBackground(aBackgroundFrame->PresContext(),
                                      aBackgroundFrame, &bgSC)) {
    return eTransparencyTransparent;
  }
  const nsStyleBackground* bg = bgSC->GetStyleBackground();
  if (NS_GET_A(bg->mBackgroundColor) < 255 ||
      // bottom layer's clip is used for the color
      bg->BottomLayer().mClip != NS_STYLE_BG_CLIP_BORDER)
    return eTransparencyTransparent;
  return eTransparencyOpaque;
}

static bool IsPopupFrame(nsIFrame* aFrame)
{
  // aFrame is a popup it's the list control frame dropdown for a combobox.
  nsIAtom* frameType = aFrame->GetType();
  if (frameType == nsGkAtoms::listControlFrame) {
    nsListControlFrame* lcf = static_cast<nsListControlFrame*>(aFrame);
    return lcf->IsInDropDownMode();
  }

  // ... or if it's a XUL menupopup frame.
  return frameType == nsGkAtoms::menuPopupFrame;
}

/* static */ bool
nsLayoutUtils::IsPopup(nsIFrame* aFrame)
{
  // Optimization: the frame can't possibly be a popup if it has no view.
  if (!aFrame->HasView()) {
    NS_ASSERTION(!IsPopupFrame(aFrame), "popup frame must have a view");
    return false;
  }
  return IsPopupFrame(aFrame);
}

/* static */ nsIFrame*
nsLayoutUtils::GetDisplayRootFrame(nsIFrame* aFrame)
{
  // We could use GetRootPresContext() here if the
  // NS_FRAME_IN_POPUP frame bit is set.
  nsIFrame* f = aFrame;
  for (;;) {
    if (!f->HasAnyStateBits(NS_FRAME_IN_POPUP)) {
      f = f->PresContext()->FrameManager()->GetRootFrame();
    } else if (IsPopup(f)) {
      return f;
    }
    nsIFrame* parent = GetCrossDocParentFrame(f);
    if (!parent)
      return f;
    f = parent;
  }
}

/* static */ uint32_t
nsLayoutUtils::GetTextRunFlagsForStyle(nsStyleContext* aStyleContext,
                                       const nsStyleFont* aStyleFont,
                                       nscoord aLetterSpacing)
{
  uint32_t result = 0;
  if (aLetterSpacing != 0) {
    result |= gfxTextRunFactory::TEXT_DISABLE_OPTIONAL_LIGATURES;
  }
  switch (aStyleContext->GetStyleSVG()->mTextRendering) {
  case NS_STYLE_TEXT_RENDERING_OPTIMIZESPEED:
    result |= gfxTextRunFactory::TEXT_OPTIMIZE_SPEED;
    break;
  case NS_STYLE_TEXT_RENDERING_AUTO:
    if (aStyleFont->mFont.size <
        aStyleContext->PresContext()->GetAutoQualityMinFontSize()) {
      result |= gfxTextRunFactory::TEXT_OPTIMIZE_SPEED;
    }
    break;
  default:
    break;
  }
  return result;
}

/* static */ void
nsLayoutUtils::GetRectDifferenceStrips(const nsRect& aR1, const nsRect& aR2,
                                       nsRect* aHStrip, nsRect* aVStrip) {
  NS_ASSERTION(aR1.TopLeft() == aR2.TopLeft(),
               "expected rects at the same position");
  nsRect unionRect(aR1.x, aR1.y, NS_MAX(aR1.width, aR2.width),
                   NS_MAX(aR1.height, aR2.height));
  nscoord VStripStart = NS_MIN(aR1.width, aR2.width);
  nscoord HStripStart = NS_MIN(aR1.height, aR2.height);
  *aVStrip = unionRect;
  aVStrip->x += VStripStart;
  aVStrip->width -= VStripStart;
  *aHStrip = unionRect;
  aHStrip->y += HStripStart;
  aHStrip->height -= HStripStart;
}

nsDeviceContext*
nsLayoutUtils::GetDeviceContextForScreenInfo(nsPIDOMWindow* aWindow)
{
  if (!aWindow) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShell> docShell = aWindow->GetDocShell();
  while (docShell) {
    // Now make sure our size is up to date.  That will mean that the device
    // context does the right thing on multi-monitor systems when we return it to
    // the caller.  It will also make sure that our prescontext has been created,
    // if we're supposed to have one.
    nsCOMPtr<nsPIDOMWindow> win = do_GetInterface(docShell);
    if (!win) {
      // No reason to go on
      return nullptr;
    }

    win->EnsureSizeUpToDate();

    nsRefPtr<nsPresContext> presContext;
    docShell->GetPresContext(getter_AddRefs(presContext));
    if (presContext) {
      nsDeviceContext* context = presContext->DeviceContext();
      if (context) {
        return context;
      }
    }

    nsCOMPtr<nsIDocShellTreeItem> curItem = do_QueryInterface(docShell);
    nsCOMPtr<nsIDocShellTreeItem> parentItem;
    curItem->GetParent(getter_AddRefs(parentItem));
    docShell = do_QueryInterface(parentItem);
  }

  return nullptr;
}

/* static */ bool
nsLayoutUtils::IsReallyFixedPos(nsIFrame* aFrame)
{
  NS_PRECONDITION(aFrame->GetParent(),
                  "IsReallyFixedPos called on frame not in tree");
  NS_PRECONDITION(aFrame->GetStyleDisplay()->mPosition ==
                    NS_STYLE_POSITION_FIXED,
                  "IsReallyFixedPos called on non-'position:fixed' frame");

  nsIAtom *parentType = aFrame->GetParent()->GetType();
  return parentType == nsGkAtoms::viewportFrame ||
         parentType == nsGkAtoms::pageContentFrame;
}

nsLayoutUtils::SurfaceFromElementResult
nsLayoutUtils::SurfaceFromElement(nsIImageLoadingContent* aElement,
                                  uint32_t aSurfaceFlags)
{
  SurfaceFromElementResult result;
  nsresult rv;

  bool forceCopy = (aSurfaceFlags & SFE_WANT_NEW_SURFACE) != 0;
  bool wantImageSurface = (aSurfaceFlags & SFE_WANT_IMAGE_SURFACE) != 0;
  bool premultAlpha = (aSurfaceFlags & SFE_NO_PREMULTIPLY_ALPHA) == 0;

  if (!premultAlpha) {
    forceCopy = true;
    wantImageSurface = true;
  }

  // Push a null JSContext on the stack so that code that runs within
  // the below code doesn't think it's being called by JS. See bug
  // 604262.
  nsCxPusher pusher;
  pusher.PushNull();

  nsCOMPtr<imgIRequest> imgRequest;
  rv = aElement->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                            getter_AddRefs(imgRequest));
  if (NS_FAILED(rv) || !imgRequest)
    return result;

  uint32_t status;
  imgRequest->GetImageStatus(&status);
  if ((status & imgIRequest::STATUS_LOAD_COMPLETE) == 0) {
    // Spec says to use GetComplete, but that only works on
    // nsIDOMHTMLImageElement, and we support all sorts of other stuff
    // here.  Do this for now pending spec clarification.
    result.mIsStillLoading = (status & imgIRequest::STATUS_ERROR) == 0;
    return result;
  }

  nsCOMPtr<nsIPrincipal> principal;
  rv = imgRequest->GetImagePrincipal(getter_AddRefs(principal));
  if (NS_FAILED(rv))
    return result;

  nsCOMPtr<imgIContainer> imgContainer;
  rv = imgRequest->GetImage(getter_AddRefs(imgContainer));
  if (NS_FAILED(rv))
    return result;

  uint32_t whichFrame = (aSurfaceFlags & SFE_WANT_FIRST_FRAME)
                        ? (uint32_t) imgIContainer::FRAME_FIRST
                        : (uint32_t) imgIContainer::FRAME_CURRENT;
  uint32_t frameFlags = imgIContainer::FLAG_SYNC_DECODE;
  if (aSurfaceFlags & SFE_NO_COLORSPACE_CONVERSION)
    frameFlags |= imgIContainer::FLAG_DECODE_NO_COLORSPACE_CONVERSION;
  if (aSurfaceFlags & SFE_NO_PREMULTIPLY_ALPHA)
    frameFlags |= imgIContainer::FLAG_DECODE_NO_PREMULTIPLY_ALPHA;
  nsRefPtr<gfxASurface> framesurf;
  rv = imgContainer->GetFrame(whichFrame,
                              frameFlags,
                              getter_AddRefs(framesurf));
  if (NS_FAILED(rv))
    return result;

  int32_t imgWidth, imgHeight;
  rv = imgContainer->GetWidth(&imgWidth);
  nsresult rv2 = imgContainer->GetHeight(&imgHeight);
  if (NS_FAILED(rv) || NS_FAILED(rv2))
    return result;

  if (wantImageSurface && framesurf->GetType() != gfxASurface::SurfaceTypeImage) {
    forceCopy = true;
  }

  nsRefPtr<gfxASurface> gfxsurf = framesurf;
  if (forceCopy) {
    if (wantImageSurface) {
      gfxsurf = new gfxImageSurface (gfxIntSize(imgWidth, imgHeight), gfxASurface::ImageFormatARGB32);
    } else {
      gfxsurf = gfxPlatform::GetPlatform()->CreateOffscreenSurface(gfxIntSize(imgWidth, imgHeight),
                                                                   gfxASurface::CONTENT_COLOR_ALPHA);
    }

    nsRefPtr<gfxContext> ctx = new gfxContext(gfxsurf);

    ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
    ctx->SetSource(framesurf);
    ctx->Paint();
  }

  int32_t corsmode;
  if (NS_SUCCEEDED(imgRequest->GetCORSMode(&corsmode))) {
    result.mCORSUsed = (corsmode != imgIRequest::CORS_NONE);
  }

  result.mSurface = gfxsurf;
  result.mSize = gfxIntSize(imgWidth, imgHeight);
  result.mPrincipal = principal.forget();
  // no images, including SVG images, can load content from another domain.
  result.mIsWriteOnly = false;
  result.mImageRequest = imgRequest.forget();

  return result;
}

nsLayoutUtils::SurfaceFromElementResult
nsLayoutUtils::SurfaceFromElement(nsHTMLImageElement *aElement,
                                  uint32_t aSurfaceFlags)
{
  return SurfaceFromElement(static_cast<nsIImageLoadingContent*>(aElement),
                            aSurfaceFlags);
}

nsLayoutUtils::SurfaceFromElementResult
nsLayoutUtils::SurfaceFromElement(nsHTMLCanvasElement* aElement,
                                  uint32_t aSurfaceFlags)
{
  SurfaceFromElementResult result;
  nsresult rv;

  bool forceCopy = (aSurfaceFlags & SFE_WANT_NEW_SURFACE) != 0;
  bool wantImageSurface = (aSurfaceFlags & SFE_WANT_IMAGE_SURFACE) != 0;
  bool premultAlpha = (aSurfaceFlags & SFE_NO_PREMULTIPLY_ALPHA) == 0;

  if (!premultAlpha) {
    forceCopy = true;
    wantImageSurface = true;
  }

  gfxIntSize size = aElement->GetSize();

  nsRefPtr<gfxASurface> surf;

  if (!forceCopy && aElement->CountContexts() == 1) {
    nsICanvasRenderingContextInternal *srcCanvas = aElement->GetContextAtIndex(0);
    rv = srcCanvas->GetThebesSurface(getter_AddRefs(surf));

    if (NS_FAILED(rv))
      surf = nullptr;
  }

  if (surf && wantImageSurface && surf->GetType() != gfxASurface::SurfaceTypeImage)
    surf = nullptr;

  if (!surf) {
    if (wantImageSurface) {
      surf = new gfxImageSurface(size, gfxASurface::ImageFormatARGB32);
    } else {
      surf = gfxPlatform::GetPlatform()->CreateOffscreenSurface(size, gfxASurface::CONTENT_COLOR_ALPHA);
    }

    nsRefPtr<gfxContext> ctx = new gfxContext(surf);
    // XXX shouldn't use the external interface, but maybe we can layerify this
    uint32_t flags = premultAlpha ? nsHTMLCanvasElement::RenderFlagPremultAlpha : 0;
    rv = aElement->RenderContextsExternal(ctx, gfxPattern::FILTER_NEAREST, flags);
    if (NS_FAILED(rv))
      return result;
  }

  // Ensure that any future changes to the canvas trigger proper invalidation,
  // in case this is being used by -moz-element()
  aElement->MarkContextClean();

  result.mSurface = surf;
  result.mSize = size;
  result.mPrincipal = aElement->NodePrincipal();
  result.mIsWriteOnly = aElement->IsWriteOnly();

  return result;
}

nsLayoutUtils::SurfaceFromElementResult
nsLayoutUtils::SurfaceFromElement(nsHTMLVideoElement* aElement,
                                  uint32_t aSurfaceFlags)
{
  SurfaceFromElementResult result;

  bool wantImageSurface = (aSurfaceFlags & SFE_WANT_IMAGE_SURFACE) != 0;
  bool premultAlpha = (aSurfaceFlags & SFE_NO_PREMULTIPLY_ALPHA) == 0;

  if (!premultAlpha) {
    wantImageSurface = true;
  }

  uint16_t readyState;
  if (NS_SUCCEEDED(aElement->GetReadyState(&readyState)) &&
      (readyState == nsIDOMHTMLMediaElement::HAVE_NOTHING ||
       readyState == nsIDOMHTMLMediaElement::HAVE_METADATA)) {
    result.mIsStillLoading = true;
    return result;
  }

  // If it doesn't have a principal, just bail
  nsCOMPtr<nsIPrincipal> principal = aElement->GetCurrentPrincipal();
  if (!principal)
    return result;

  ImageContainer *container = aElement->GetImageContainer();
  if (!container)
    return result;

  gfxIntSize size;
  nsRefPtr<gfxASurface> surf = container->GetCurrentAsSurface(&size);
  if (!surf)
    return result;

  if (wantImageSurface && surf->GetType() != gfxASurface::SurfaceTypeImage) {
    nsRefPtr<gfxImageSurface> imgSurf =
      new gfxImageSurface(size, gfxASurface::ImageFormatARGB32);

    nsRefPtr<gfxContext> ctx = new gfxContext(imgSurf);
    ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
    ctx->DrawSurface(surf, size);
    surf = imgSurf;
  }

  result.mCORSUsed = aElement->GetCORSMode() != CORS_NONE;
  result.mSurface = surf;
  result.mSize = size;
  result.mPrincipal = principal.forget();
  result.mIsWriteOnly = false;

  return result;
}

nsLayoutUtils::SurfaceFromElementResult
nsLayoutUtils::SurfaceFromElement(dom::Element* aElement,
                                  uint32_t aSurfaceFlags)
{
  // If it's a <canvas>, we may be able to just grab its internal surface
  if (nsHTMLCanvasElement* canvas =
        nsHTMLCanvasElement::FromContentOrNull(aElement)) {
    return SurfaceFromElement(canvas, aSurfaceFlags);
  }

#ifdef MOZ_MEDIA
  // Maybe it's <video>?
  if (nsHTMLVideoElement* video =
        nsHTMLVideoElement::FromContentOrNull(aElement)) {
    return SurfaceFromElement(video, aSurfaceFlags);
  }
#endif

  // Finally, check if it's a normal image
  nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(aElement);

  if (!imageLoader) {
    return SurfaceFromElementResult();
  }

  return SurfaceFromElement(imageLoader, aSurfaceFlags);
}

/* static */
nsIContent*
nsLayoutUtils::GetEditableRootContentByContentEditable(nsIDocument* aDocument)
{
  // If the document is in designMode we should return NULL.
  if (!aDocument || aDocument->HasFlag(NODE_IS_EDITABLE)) {
    return nullptr;
  }

  // contenteditable only works with HTML document.
  // Note: Use nsIDOMHTMLDocument rather than nsIHTMLDocument for getting the
  //       body node because nsIDOMHTMLDocument::GetBody() does something
  //       additional work for some cases and nsEditor uses them.
  nsCOMPtr<nsIDOMHTMLDocument> domHTMLDoc = do_QueryInterface(aDocument);
  if (!domHTMLDoc) {
    return nullptr;
  }

  Element* rootElement = aDocument->GetRootElement();
  if (rootElement && rootElement->IsEditable()) {
    return rootElement;
  }

  // If there are no editable root element, check its <body> element.
  // Note that the body element could be <frameset> element.
  nsCOMPtr<nsIDOMHTMLElement> body;
  nsresult rv = domHTMLDoc->GetBody(getter_AddRefs(body));
  nsCOMPtr<nsIContent> content = do_QueryInterface(body);
  if (NS_SUCCEEDED(rv) && content && content->IsEditable()) {
    return content;
  }
  return nullptr;
}

#ifdef DEBUG
/* static */ void
nsLayoutUtils::AssertNoDuplicateContinuations(nsIFrame* aContainer,
                                              const nsFrameList& aFrameList)
{
  for (nsIFrame* f = aFrameList.FirstChild(); f ; f = f->GetNextSibling()) {
    // Check only later continuations of f; we deal with checking the
    // earlier continuations when we hit those earlier continuations in
    // the frame list.
    for (nsIFrame *c = f; (c = c->GetNextInFlow());) {
      NS_ASSERTION(c->GetParent() != aContainer ||
                   !aFrameList.ContainsFrame(c),
                   "Two continuations of the same frame in the same "
                   "frame list");
    }
  }
}

// Is one of aFrame's ancestors a letter frame?
static bool
IsInLetterFrame(nsIFrame *aFrame)
{
  for (nsIFrame *f = aFrame->GetParent(); f; f = f->GetParent()) {
    if (f->GetType() == nsGkAtoms::letterFrame) {
      return true;
    }
  }
  return false;
}

/* static */ void
nsLayoutUtils::AssertTreeOnlyEmptyNextInFlows(nsIFrame *aSubtreeRoot)
{
  NS_ASSERTION(aSubtreeRoot->GetPrevInFlow(),
               "frame tree not empty, but caller reported complete status");

  // Also assert that text frames map no text.
  int32_t start, end;
  nsresult rv = aSubtreeRoot->GetOffsets(start, end);
  NS_ASSERTION(NS_SUCCEEDED(rv), "GetOffsets failed");
  // In some cases involving :first-letter, we'll partially unlink a
  // continuation in the middle of a continuation chain from its
  // previous and next continuations before destroying it, presumably so
  // that we don't also destroy the later continuations.  Once we've
  // done this, GetOffsets returns incorrect values.
  // For examples, see list of tests in
  // https://bugzilla.mozilla.org/show_bug.cgi?id=619021#c29
  NS_ASSERTION(start == end || IsInLetterFrame(aSubtreeRoot),
               "frame tree not empty, but caller reported complete status");

  nsIFrame::ChildListIterator lists(aSubtreeRoot);
  for (; !lists.IsDone(); lists.Next()) {
    nsFrameList::Enumerator childFrames(lists.CurrentList());
    for (; !childFrames.AtEnd(); childFrames.Next()) {
      nsLayoutUtils::AssertTreeOnlyEmptyNextInFlows(childFrames.get());
    }
  }
}
#endif

/* static */
nsresult
nsLayoutUtils::GetFontFacesForFrames(nsIFrame* aFrame,
                                     nsFontFaceList* aFontFaceList)
{
  NS_PRECONDITION(aFrame, "NULL frame pointer");

  if (aFrame->GetType() == nsGkAtoms::textFrame) {
    return GetFontFacesForText(aFrame, 0, INT32_MAX, false,
                               aFontFaceList);
  }

  while (aFrame) {
    nsIFrame::ChildListID childLists[] = { nsIFrame::kPrincipalList,
                                           nsIFrame::kPopupList };
    for (size_t i = 0; i < ArrayLength(childLists); ++i) {
      nsFrameList children(aFrame->GetChildList(childLists[i]));
      for (nsFrameList::Enumerator e(children); !e.AtEnd(); e.Next()) {
        nsIFrame* child = e.get();
        if (child->GetPrevContinuation()) {
          continue;
        }
        child = nsPlaceholderFrame::GetRealFrameFor(child);
        nsresult rv = GetFontFacesForFrames(child, aFontFaceList);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
    aFrame = GetNextContinuationOrSpecialSibling(aFrame);
  }

  return NS_OK;
}

/* static */
nsresult
nsLayoutUtils::GetFontFacesForText(nsIFrame* aFrame,
                                   int32_t aStartOffset, int32_t aEndOffset,
                                   bool aFollowContinuations,
                                   nsFontFaceList* aFontFaceList)
{
  NS_PRECONDITION(aFrame, "NULL frame pointer");

  if (aFrame->GetType() != nsGkAtoms::textFrame) {
    return NS_OK;
  }

  nsTextFrame* curr = static_cast<nsTextFrame*>(aFrame);
  do {
    int32_t fstart = NS_MAX(curr->GetContentOffset(), aStartOffset);
    int32_t fend = NS_MIN(curr->GetContentEnd(), aEndOffset);
    if (fstart >= fend) {
      continue;
    }

    // overlapping with the offset we want
    gfxSkipCharsIterator iter = curr->EnsureTextRun(nsTextFrame::eInflated);
    gfxTextRun* textRun = curr->GetTextRun(nsTextFrame::eInflated);
    NS_ENSURE_TRUE(textRun, NS_ERROR_OUT_OF_MEMORY);

    uint32_t skipStart = iter.ConvertOriginalToSkipped(fstart);
    uint32_t skipEnd = iter.ConvertOriginalToSkipped(fend);
    aFontFaceList->AddFontsFromTextRun(textRun,
                                       skipStart,
                                       skipEnd - skipStart,
                                       curr);
  } while (aFollowContinuations &&
           (curr = static_cast<nsTextFrame*>(curr->GetNextContinuation())));

  return NS_OK;
}

/* static */
size_t
nsLayoutUtils::SizeOfTextRunsForFrames(nsIFrame* aFrame,
                                       nsMallocSizeOfFun aMallocSizeOf,
                                       bool clear)
{
  NS_PRECONDITION(aFrame, "NULL frame pointer");

  size_t total = 0;

  if (aFrame->GetType() == nsGkAtoms::textFrame) {
    nsTextFrame* textFrame = static_cast<nsTextFrame*>(aFrame);
    for (uint32_t i = 0; i < 2; ++i) {
      gfxTextRun *run = textFrame->GetTextRun(
        (i != 0) ? nsTextFrame::eInflated : nsTextFrame::eNotInflated);
      if (run) {
        if (clear) {
          run->ResetSizeOfAccountingFlags();
        } else {
          total += run->MaybeSizeOfIncludingThis(aMallocSizeOf);
        }
      }
    }
    return total;
  }

  nsAutoTArray<nsIFrame::ChildList,4> childListArray;
  aFrame->GetChildLists(&childListArray);

  for (nsIFrame::ChildListArrayIterator childLists(childListArray);
       !childLists.IsDone(); childLists.Next()) {
    for (nsFrameList::Enumerator e(childLists.CurrentList());
         !e.AtEnd(); e.Next()) {
      total += SizeOfTextRunsForFrames(e.get(), aMallocSizeOf, clear);
    }
  }
  return total;
}

/* static */
void
nsLayoutUtils::Initialize()
{
  Preferences::AddUintVarCache(&sFontSizeInflationMaxRatio,
                               "font.size.inflation.maxRatio");
  Preferences::AddUintVarCache(&sFontSizeInflationEmPerLine,
                               "font.size.inflation.emPerLine");
  Preferences::AddUintVarCache(&sFontSizeInflationMinTwips,
                               "font.size.inflation.minTwips");
  Preferences::AddUintVarCache(&sFontSizeInflationLineThreshold,
                               "font.size.inflation.lineThreshold");
  Preferences::AddIntVarCache(&sFontSizeInflationMappingIntercept,
                              "font.size.inflation.mappingIntercept");
  Preferences::AddBoolVarCache(&sFontSizeInflationForceEnabled,
                               "font.size.inflation.forceEnabled");
  Preferences::AddBoolVarCache(&sFontSizeInflationDisabledInMasterProcess,
                               "font.size.inflation.disabledInMasterProcess");

#ifdef MOZ_FLEXBOX
  Preferences::RegisterCallback(FlexboxEnabledPrefChangeCallback,
                                FLEXBOX_ENABLED_PREF_NAME);
  FlexboxEnabledPrefChangeCallback(FLEXBOX_ENABLED_PREF_NAME, nullptr);
#endif // MOZ_FLEXBOX
}

/* static */
void
nsLayoutUtils::Shutdown()
{
  if (sContentMap) {
    delete sContentMap;
    sContentMap = nullptr;
  }

#ifdef MOZ_FLEXBOX
  Preferences::UnregisterCallback(FlexboxEnabledPrefChangeCallback,
                                  FLEXBOX_ENABLED_PREF_NAME);
#endif // MOZ_FLEXBOX
}

/* static */
void
nsLayoutUtils::RegisterImageRequest(nsPresContext* aPresContext,
                                    imgIRequest* aRequest,
                                    bool* aRequestRegistered)
{
  if (!aPresContext) {
    return;
  }

  if (aRequestRegistered && *aRequestRegistered) {
    // Our request is already registered with the refresh driver, so
    // no need to register it again.
    return;
  }

  if (aRequest) {
    if (!aPresContext->RefreshDriver()->AddImageRequest(aRequest)) {
      NS_WARNING("Unable to add image request");
      return;
    }

    if (aRequestRegistered) {
      *aRequestRegistered = true;
    }
  }
}

/* static */
void
nsLayoutUtils::RegisterImageRequestIfAnimated(nsPresContext* aPresContext,
                                              imgIRequest* aRequest,
                                              bool* aRequestRegistered)
{
  if (!aPresContext) {
    return;
  }

  if (aRequestRegistered && *aRequestRegistered) {
    // Our request is already registered with the refresh driver, so
    // no need to register it again.
    return;
  }

  if (aRequest) {
    nsCOMPtr<imgIContainer> image;
    if (NS_SUCCEEDED(aRequest->GetImage(getter_AddRefs(image)))) {
      // Check to verify that the image is animated. If so, then add it to the
      // list of images tracked by the refresh driver.
      bool isAnimated = false;
      nsresult rv = image->GetAnimated(&isAnimated);
      if (NS_SUCCEEDED(rv) && isAnimated) {
        if (!aPresContext->RefreshDriver()->AddImageRequest(aRequest)) {
          NS_WARNING("Unable to add image request");
          return;
        }

        if (aRequestRegistered) {
          *aRequestRegistered = true;
        }
      }
    }
  }
}

/* static */
void
nsLayoutUtils::DeregisterImageRequest(nsPresContext* aPresContext,
                                      imgIRequest* aRequest,
                                      bool* aRequestRegistered)
{
  if (!aPresContext) {
    return;
  }

  // Deregister our imgIRequest with the refresh driver to
  // complete tear-down, but only if it has been registered
  if (aRequestRegistered && !*aRequestRegistered) {
    return;
  }

  if (aRequest) {
    nsCOMPtr<imgIContainer> image;
    if (NS_SUCCEEDED(aRequest->GetImage(getter_AddRefs(image)))) {
      aPresContext->RefreshDriver()->RemoveImageRequest(aRequest);

      if (aRequestRegistered) {
        *aRequestRegistered = false;
      }
    }
  }
}

/* static */
void
nsLayoutUtils::PostRestyleEvent(Element* aElement,
                                nsRestyleHint aRestyleHint,
                                nsChangeHint aMinChangeHint)
{
  nsIDocument* doc = aElement->GetCurrentDoc();
  if (doc) {
    nsCOMPtr<nsIPresShell> presShell = doc->GetShell();
    if (presShell) {
      presShell->FrameConstructor()->PostRestyleEvent(
        aElement, aRestyleHint, aMinChangeHint);
    }
  }
}

nsSetAttrRunnable::nsSetAttrRunnable(nsIContent* aContent, nsIAtom* aAttrName,
                                     const nsAString& aValue)
  : mContent(aContent),
    mAttrName(aAttrName),
    mValue(aValue)
{
  NS_ASSERTION(aContent && aAttrName, "Missing stuff, prepare to crash");
}

nsSetAttrRunnable::nsSetAttrRunnable(nsIContent* aContent, nsIAtom* aAttrName,
                                     int32_t aValue)
  : mContent(aContent),
    mAttrName(aAttrName)
{
  NS_ASSERTION(aContent && aAttrName, "Missing stuff, prepare to crash");
  mValue.AppendInt(aValue);
}

NS_IMETHODIMP
nsSetAttrRunnable::Run()
{
  return mContent->SetAttr(kNameSpaceID_None, mAttrName, mValue, true);
}

nsUnsetAttrRunnable::nsUnsetAttrRunnable(nsIContent* aContent,
                                         nsIAtom* aAttrName)
  : mContent(aContent),
    mAttrName(aAttrName)
{
  NS_ASSERTION(aContent && aAttrName, "Missing stuff, prepare to crash");
}

NS_IMETHODIMP
nsUnsetAttrRunnable::Run()
{
  return mContent->UnsetAttr(kNameSpaceID_None, mAttrName, true);
}

nsReflowFrameRunnable::nsReflowFrameRunnable(nsIFrame* aFrame,
                          nsIPresShell::IntrinsicDirty aIntrinsicDirty,
                          nsFrameState aBitToAdd)
  : mWeakFrame(aFrame),
    mIntrinsicDirty(aIntrinsicDirty),
    mBitToAdd(aBitToAdd)
{
}

NS_IMETHODIMP
nsReflowFrameRunnable::Run()
{
  if (mWeakFrame.IsAlive()) {
    mWeakFrame->PresContext()->PresShell()->
      FrameNeedsReflow(mWeakFrame, mIntrinsicDirty, mBitToAdd);
  }
  return NS_OK;
}

/**
 * Compute the minimum font size inside of a container with the given
 * width, such that **when the user zooms the container to fill the full
 * width of the device**, the fonts satisfy our minima.
 */
static nscoord
MinimumFontSizeFor(nsPresContext* aPresContext, nscoord aContainerWidth)
{
  nsIPresShell* presShell = aPresContext->PresShell();

  uint32_t emPerLine = presShell->FontSizeInflationEmPerLine();
  uint32_t minTwips = presShell->FontSizeInflationMinTwips();
  if (emPerLine == 0 && minTwips == 0) {
    return 0;
  }

  // Clamp the container width to the device dimensions
  nscoord iFrameWidth = aPresContext->GetVisibleArea().width;
  nscoord effectiveContainerWidth = NS_MIN(iFrameWidth, aContainerWidth);

  nscoord byLine = 0, byInch = 0;
  if (emPerLine != 0) {
    byLine = effectiveContainerWidth / emPerLine;
  }
  if (minTwips != 0) {
    // REVIEW: Is this giving us app units and sizes *not* counting
    // viewport scaling?
    float deviceWidthInches =
      aPresContext->ScreenWidthInchesForFontInflation();
    byInch = NSToCoordRound(effectiveContainerWidth /
                            (deviceWidthInches * 1440 /
                             minTwips ));
  }
  return NS_MAX(byLine, byInch);
}

/* static */ float
nsLayoutUtils::FontSizeInflationInner(const nsIFrame *aFrame,
                                      nscoord aMinFontSize)
{
  // Note that line heights should be inflated by the same ratio as the
  // font size of the same text; thus we operate only on the font size
  // even when we're scaling a line height.
  nscoord styleFontSize = aFrame->GetStyleFont()->mFont.size;
  if (styleFontSize <= 0) {
    // Never scale zero font size.
    return 1.0;
  }

  if (aMinFontSize <= 0) {
    // No need to scale.
    return 1.0;
  }

  // If between this current frame and its font inflation container there is a
  // non-inline element with fixed width or height, then we should not inflate
  // fonts for this frame.
  for (const nsIFrame* f = aFrame;
       f && !IsContainerForFontSizeInflation(f);
       f = f->GetParent()) {
    nsIContent* content = f->GetContent();
    nsIAtom* fType = f->GetType();
    // Also, if there is more than one frame corresponding to a single
    // content node, we want the outermost one.
    if (!(f->GetParent() && f->GetParent()->GetContent() == content) &&
        // ignore width/height on inlines since they don't apply
        fType != nsGkAtoms::inlineFrame &&
        // ignore width on radios and checkboxes since we enlarge them and
        // they have width/height in ua.css
        fType != nsGkAtoms::formControlFrame) {
      nsStyleCoord stylePosWidth = f->GetStylePosition()->mWidth;
      nsStyleCoord stylePosHeight = f->GetStylePosition()->mHeight;
      if (stylePosWidth.GetUnit() != eStyleUnit_Auto ||
          stylePosHeight.GetUnit() != eStyleUnit_Auto) {

        return 1.0;
      }
    }
  }

  int32_t interceptParam = nsLayoutUtils::FontSizeInflationMappingIntercept();
  float maxRatio = (float)nsLayoutUtils::FontSizeInflationMaxRatio() / 100.0f;

  float ratio = float(styleFontSize) / float(aMinFontSize);
  float inflationRatio;

  // Given a minimum inflated font size m, a specified font size s, we want to
  // find the inflated font size i and then return the ratio of i to s (i/s).
  if (interceptParam >= 0) {
    // Since the mapping intercept parameter P is greater than zero, we use it
    // to determine the point where our mapping function intersects the i=s
    // line. This means that we have an equation of the form:
    //
    // i = m + s·(P/2)/(1 + P/2), if s <= (1 + P/2)·m
    // i = s, if s >= (1 + P/2)·m

    float intercept = 1 + float(interceptParam)/2.0f;
    if (ratio >= intercept) {
      // If we're already at 1+P/2 or more times the minimum, don't scale.
      return 1.0;
    }

    // The point (intercept, intercept) is where the part of the i vs. s graph
    // that's not slope 1 meets the i=s line.  (This part of the
    // graph is a line from (0, m), to that point). We calculate the
    // intersection point to be ((1+P/2)m, (1+P/2)m), where P is the
    // intercept parameter above. We then need to return i/s.
    inflationRatio = (1.0f + (ratio * (intercept - 1) / intercept)) / ratio;
  } else {
    // This is the case where P is negative. We essentially want to implement
    // the case for P=infinity here, so we make i = s + m, which means that
    // i/s = s/s + m/s = 1 + 1/ratio
    inflationRatio = 1 + 1.0f / ratio;
  }

  if (maxRatio > 1.0 && inflationRatio > maxRatio) {
    return maxRatio;
  } else {
    return inflationRatio;
  }
}

static bool
ShouldInflateFontsForContainer(const nsIFrame *aFrame)
{
  // We only want to inflate fonts for text that is in a place
  // with room to expand.  The question is what the best heuristic for
  // that is...
  // For now, we're going to use NS_FRAME_IN_CONSTRAINED_HEIGHT, which
  // indicates whether the frame is inside something with a constrained
  // height (propagating down the tree), but the propagation stops when
  // we hit overflow-y: scroll or auto.
  const nsStyleText* styleText = aFrame->GetStyleText();

  return styleText->mTextSizeAdjust != NS_STYLE_TEXT_SIZE_ADJUST_NONE &&
         !(aFrame->GetStateBits() & NS_FRAME_IN_CONSTRAINED_HEIGHT) &&
         // We also want to disable font inflation for containers that have
         // preformatted text.
         styleText->WhiteSpaceCanWrap();
}

nscoord
nsLayoutUtils::InflationMinFontSizeFor(const nsIFrame *aFrame)
{
  nsPresContext *presContext = aFrame->PresContext();
  if (!FontSizeInflationEnabled(presContext) ||
      presContext->mInflationDisabledForShrinkWrap) {
    return 0;
  }

  for (const nsIFrame *f = aFrame; f; f = f->GetParent()) {
    if (IsContainerForFontSizeInflation(f)) {
      if (!ShouldInflateFontsForContainer(f)) {
        return 0;
      }

      nsFontInflationData *data =
        nsFontInflationData::FindFontInflationDataFor(aFrame);
      // FIXME: The need to null-check here is sort of a bug, and might
      // lead to incorrect results.
      if (!data || !data->InflationEnabled()) {
        return 0;
      }

      return MinimumFontSizeFor(aFrame->PresContext(),
                                data->EffectiveWidth());
    }
  }

  NS_ABORT_IF_FALSE(false, "root should always be container");

  return 0;
}

float
nsLayoutUtils::FontSizeInflationFor(const nsIFrame *aFrame)
{
  if (!FontSizeInflationEnabled(aFrame->PresContext())) {
    return 1.0;
  }

  return FontSizeInflationInner(aFrame, InflationMinFontSizeFor(aFrame));
}

/* static */ bool
nsLayoutUtils::FontSizeInflationEnabled(nsPresContext *aPresContext)
{
  nsIPresShell* presShell = aPresContext->GetPresShell();

  if (!presShell ||
      (presShell->FontSizeInflationEmPerLine() == 0 &&
       presShell->FontSizeInflationMinTwips() == 0) ||
       aPresContext->IsChrome()) {
    return false;
  }
  // Force-enabling font inflation always trumps the heuristics here.
  if (!presShell->FontSizeInflationForceEnabled()) {
    if (TabChild* tab = GetTabChildFrom(presShell)) {
      // We're in a child process.  Cancel inflation if we're not
      // async-pan zoomed.
      if (!tab->IsAsyncPanZoomEnabled()) {
        return false;
      }
    } else if (XRE_GetProcessType() == GeckoProcessType_Default) {
      // We're in the master process.  Cancel inflation if it's been
      // explicitly disabled.
      if (presShell->FontSizeInflationDisabledInMasterProcess()) {
        return false;
      }
    }
  }

  // XXXjwir3:
  // See bug 706918, comment 23 for more information on this particular section
  // of the code. We're using "screen size" in place of the size of the content
  // area, because on mobile, these are close or equal. This will work for our
  // purposes (bug 706198), but it will need to be changed in the future to be
  // more correct when we bring the rest of the viewport code into platform.
  // We actually want the size of the content area, in the event that we don't
  // have any metadata about the width and/or height. On mobile, the screen size
  // and the size of the content area are very close, or the same value.
  // In XUL fennec, the content area is the size of the <browser> widget, but
  // in native fennec, the content area is the size of the Gecko LayerView
  // object.

  // TODO:
  // Once bug 716575 has been resolved, this code should be changed so that it
  // does the right thing on all platforms.
  nsresult rv;
  nsCOMPtr<nsIScreenManager> screenMgr =
    do_GetService("@mozilla.org/gfx/screenmanager;1", &rv);
  NS_ENSURE_SUCCESS(rv, false);

  nsCOMPtr<nsIScreen> screen;
  screenMgr->GetPrimaryScreen(getter_AddRefs(screen));
  if (screen) {
    int32_t screenLeft, screenTop, screenWidth, screenHeight;
    screen->GetRect(&screenLeft, &screenTop, &screenWidth, &screenHeight);

    nsViewportInfo vInf =
      nsContentUtils::GetViewportInfo(aPresContext->PresShell()->GetDocument(),
                                      screenWidth, screenHeight);

  if (vInf.GetDefaultZoom() >= 1.0 || vInf.IsAutoSizeEnabled()) {
      return false;
    }
  }

  return true;
}

/* static */ nsRect
nsLayoutUtils::GetBoxShadowRectForFrame(nsIFrame* aFrame, 
                                        const nsSize& aFrameSize)
{
  nsCSSShadowArray* boxShadows = aFrame->GetStyleBorder()->mBoxShadow;
  if (!boxShadows) {
    return nsRect();
  }
  
  nsRect shadows;
  int32_t A2D = aFrame->PresContext()->AppUnitsPerDevPixel();
  for (uint32_t i = 0; i < boxShadows->Length(); ++i) {
    nsRect tmpRect(nsPoint(0, 0), aFrameSize);
    nsCSSShadowItem* shadow = boxShadows->ShadowAt(i);

    // inset shadows are never painted outside the frame
    if (shadow->mInset)
      continue;

    tmpRect.MoveBy(nsPoint(shadow->mXOffset, shadow->mYOffset));
    tmpRect.Inflate(shadow->mSpread);
    tmpRect.Inflate(
      nsContextBoxBlur::GetBlurRadiusMargin(shadow->mRadius, A2D));
    shadows.UnionRect(shadows, tmpRect);
  }
  return shadows;
}

