/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// Eric Vaughan
// Netscape Communications
//
// See documentation in associated header file
//

#include "nsImageBoxFrame.h"
#include "nsGkAtoms.h"
#include "nsStyleContext.h"
#include "nsStyleConsts.h"
#include "nsCOMPtr.h"
#include "nsPresContext.h"
#include "nsBoxLayoutState.h"

#include "nsHTMLParts.h"
#include "nsString.h"
#include "nsLeafFrame.h"
#include "nsIPresShell.h"
#include "nsIDocument.h"
#include "nsImageMap.h"
#include "nsILinkHandler.h"
#include "nsIURL.h"
#include "nsILoadGroup.h"
#include "nsContainerFrame.h"
#include "prprf.h"
#include "nsCSSRendering.h"
#include "nsIDOMHTMLImageElement.h"
#include "nsINameSpaceManager.h"
#include "nsTextFragment.h"
#include "nsIDOMHTMLMapElement.h"
#include "nsTransform2D.h"
#include "nsITheme.h"
#include "nsIImageLoadingContent.h"

#include "nsIServiceManager.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsThreadUtils.h"
#include "nsGUIEvent.h"
#include "nsEventDispatcher.h"
#include "nsDisplayList.h"

#include "nsContentUtils.h"

#define ONLOAD_CALLED_TOO_EARLY 1

class nsImageBoxFrameEvent : public nsRunnable
{
public:
  nsImageBoxFrameEvent(nsIContent *content, PRUint32 message)
    : mContent(content), mMessage(message) {}

  NS_IMETHOD Run();

private:
  nsCOMPtr<nsIContent> mContent;
  PRUint32 mMessage;
};

NS_IMETHODIMP
nsImageBoxFrameEvent::Run()
{
  nsIPresShell *pres_shell = mContent->OwnerDoc()->GetShell();
  if (!pres_shell) {
    return NS_OK;
  }

  nsRefPtr<nsPresContext> pres_context = pres_shell->GetPresContext();
  if (!pres_context) {
    return NS_OK;
  }

  nsEventStatus status = nsEventStatus_eIgnore;
  nsEvent event(true, mMessage);

  event.flags |= NS_EVENT_FLAG_CANT_BUBBLE;
  nsEventDispatcher::Dispatch(mContent, pres_context, &event, nsnull, &status);
  return NS_OK;
}

// Fire off an event that'll asynchronously call the image elements
// onload handler once handled. This is needed since the image library
// can't decide if it wants to call it's observer methods
// synchronously or asynchronously. If an image is loaded from the
// cache the notifications come back synchronously, but if the image
// is loaded from the netswork the notifications come back
// asynchronously.

void
FireImageDOMEvent(nsIContent* aContent, PRUint32 aMessage)
{
  NS_ASSERTION(aMessage == NS_LOAD || aMessage == NS_LOAD_ERROR,
               "invalid message");

  nsCOMPtr<nsIRunnable> event = new nsImageBoxFrameEvent(aContent, aMessage);
  if (NS_FAILED(NS_DispatchToCurrentThread(event)))
    NS_WARNING("failed to dispatch image event");
}

//
// NS_NewImageBoxFrame
//
// Creates a new image frame and returns it
//
nsIFrame*
NS_NewImageBoxFrame (nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsImageBoxFrame (aPresShell, aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsImageBoxFrame)

NS_IMETHODIMP
nsImageBoxFrame::AttributeChanged(PRInt32 aNameSpaceID,
                                  nsIAtom* aAttribute,
                                  PRInt32 aModType)
{
  nsresult rv = nsLeafBoxFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                                 aModType);

  if (aAttribute == nsGkAtoms::src) {
    UpdateImage();
    PresContext()->PresShell()->
      FrameNeedsReflow(this, nsIPresShell::eStyleChange, NS_FRAME_IS_DIRTY);
  }
  else if (aAttribute == nsGkAtoms::validate)
    UpdateLoadFlags();

  return rv;
}

nsImageBoxFrame::nsImageBoxFrame(nsIPresShell* aShell, nsStyleContext* aContext):
  nsLeafBoxFrame(aShell, aContext),
  mIntrinsicSize(0,0),
  mRequestRegistered(false),
  mLoadFlags(nsIRequest::LOAD_NORMAL),
  mUseSrcAttr(false),
  mSuppressStyleCheck(false)
{
  MarkIntrinsicWidthsDirty();
}

nsImageBoxFrame::~nsImageBoxFrame()
{
}


/* virtual */ void
nsImageBoxFrame::MarkIntrinsicWidthsDirty()
{
  SizeNeedsRecalc(mImageSize);
  nsLeafBoxFrame::MarkIntrinsicWidthsDirty();
}

void
nsImageBoxFrame::DestroyFrom(nsIFrame* aDestructRoot)
{
  if (mImageRequest) {
    nsLayoutUtils::DeregisterImageRequest(PresContext(), mImageRequest,
                                          &mRequestRegistered);

    // Release image loader first so that it's refcnt can go to zero
    mImageRequest->CancelAndForgetObserver(NS_ERROR_FAILURE);
  }

  if (mListener)
    reinterpret_cast<nsImageBoxListener*>(mListener.get())->SetFrame(nsnull); // set the frame to null so we don't send messages to a dead object.

  nsLeafBoxFrame::DestroyFrom(aDestructRoot);
}


NS_IMETHODIMP
nsImageBoxFrame::Init(nsIContent*      aContent,
                      nsIFrame*        aParent,
                      nsIFrame*        aPrevInFlow)
{
  if (!mListener) {
    nsImageBoxListener *listener = new nsImageBoxListener();
    NS_ADDREF(listener);
    listener->SetFrame(this);
    listener->QueryInterface(NS_GET_IID(imgIDecoderObserver), getter_AddRefs(mListener));
    NS_RELEASE(listener);
  }

  mSuppressStyleCheck = true;
  nsresult rv = nsLeafBoxFrame::Init(aContent, aParent, aPrevInFlow);
  mSuppressStyleCheck = false;

  UpdateLoadFlags();
  UpdateImage();

  return rv;
}

void
nsImageBoxFrame::UpdateImage()
{
  nsPresContext* presContext = PresContext();

  if (mImageRequest) {
    nsLayoutUtils::DeregisterImageRequest(presContext, mImageRequest,
                                          &mRequestRegistered);
    mImageRequest->CancelAndForgetObserver(NS_ERROR_FAILURE);
    mImageRequest = nsnull;
  }

  // get the new image src
  nsAutoString src;
  mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::src, src);
  mUseSrcAttr = !src.IsEmpty();
  if (mUseSrcAttr) {
    nsIDocument* doc = mContent->GetDocument();
    if (!doc) {
      // No need to do anything here...
      return;
    }
    nsCOMPtr<nsIURI> baseURI = mContent->GetBaseURI();
    nsCOMPtr<nsIURI> uri;
    nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(uri),
                                              src,
                                              doc,
                                              baseURI);

    if (uri && nsContentUtils::CanLoadImage(uri, mContent, doc,
                                            mContent->NodePrincipal())) {
      nsContentUtils::LoadImage(uri, doc, mContent->NodePrincipal(),
                                doc->GetDocumentURI(), mListener, mLoadFlags,
                                getter_AddRefs(mImageRequest));

      if (mImageRequest) {
        nsLayoutUtils::RegisterImageRequestIfAnimated(presContext,
                                                      mImageRequest,
                                                      &mRequestRegistered);
      }
    }
  } else {
    // Only get the list-style-image if we aren't being drawn
    // by a native theme.
    PRUint8 appearance = GetStyleDisplay()->mAppearance;
    if (!(appearance && nsBox::gTheme &&
          nsBox::gTheme->ThemeSupportsWidget(nsnull, this, appearance))) {
      // get the list-style-image
      imgIRequest *styleRequest = GetStyleList()->GetListStyleImage();
      if (styleRequest) {
        styleRequest->Clone(mListener, getter_AddRefs(mImageRequest));
      }
    }
  }

  if (!mImageRequest) {
    // We have no image, so size to 0
    mIntrinsicSize.SizeTo(0, 0);
  } else {
    // We don't want discarding or decode-on-draw for xul images.
    mImageRequest->RequestDecode();
    mImageRequest->LockImage();
  }
}

void
nsImageBoxFrame::UpdateLoadFlags()
{
  static nsIContent::AttrValuesArray strings[] =
    {&nsGkAtoms::always, &nsGkAtoms::never, nsnull};
  switch (mContent->FindAttrValueIn(kNameSpaceID_None, nsGkAtoms::validate,
                                    strings, eCaseMatters)) {
    case 0:
      mLoadFlags = nsIRequest::VALIDATE_ALWAYS;
      break;
    case 1:
      mLoadFlags = nsIRequest::VALIDATE_NEVER|nsIRequest::LOAD_FROM_CACHE;
      break;
    default:
      mLoadFlags = nsIRequest::LOAD_NORMAL;
      break;
  }
}

class nsDisplayXULImage : public nsDisplayItem {
public:
  nsDisplayXULImage(nsDisplayListBuilder* aBuilder,
                    nsImageBoxFrame* aFrame) :
    nsDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayXULImage);
  }
#ifdef NS_BUILD_REFCNT_LOGGING
  virtual ~nsDisplayXULImage() {
    MOZ_COUNT_DTOR(nsDisplayXULImage);
  }
#endif

  // Doesn't handle HitTest because nsLeafBoxFrame already creates an
  // event receiver for us
  virtual void Paint(nsDisplayListBuilder* aBuilder,
                     nsRenderingContext* aCtx);
  NS_DISPLAY_DECL_NAME("XULImage", TYPE_XUL_IMAGE)
};

void nsDisplayXULImage::Paint(nsDisplayListBuilder* aBuilder,
                              nsRenderingContext* aCtx)
{
  static_cast<nsImageBoxFrame*>(mFrame)->
    PaintImage(*aCtx, mVisibleRect, ToReferenceFrame(),
               aBuilder->ShouldSyncDecodeImages()
                 ? (PRUint32) imgIContainer::FLAG_SYNC_DECODE
                 : (PRUint32) imgIContainer::FLAG_NONE);
}

NS_IMETHODIMP
nsImageBoxFrame::BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                  const nsRect&           aDirtyRect,
                                  const nsDisplayListSet& aLists)
{
  nsresult rv = nsLeafBoxFrame::BuildDisplayList(aBuilder, aDirtyRect, aLists);
  NS_ENSURE_SUCCESS(rv, rv);

  if ((0 == mRect.width) || (0 == mRect.height)) {
    // Do not render when given a zero area. This avoids some useless
    // scaling work while we wait for our image dimensions to arrive
    // asynchronously.
    return NS_OK;
  }

  if (!IsVisibleForPainting(aBuilder))
    return NS_OK;

  return aLists.Content()->AppendNewToTop(
      new (aBuilder) nsDisplayXULImage(aBuilder, this));
}

void
nsImageBoxFrame::PaintImage(nsRenderingContext& aRenderingContext,
                            const nsRect& aDirtyRect, nsPoint aPt,
                            PRUint32 aFlags)
{
  nsRect rect;
  GetClientRect(rect);

  rect += aPt;

  if (!mImageRequest)
    return;

  // don't draw if the image is not dirty
  nsRect dirty;
  if (!dirty.IntersectRect(aDirtyRect, rect))
    return;

  nsCOMPtr<imgIContainer> imgCon;
  mImageRequest->GetImage(getter_AddRefs(imgCon));

  if (imgCon) {
    bool hasSubRect = !mUseSrcAttr && (mSubRect.width > 0 || mSubRect.height > 0);
    nsLayoutUtils::DrawSingleImage(&aRenderingContext, imgCon,
        nsLayoutUtils::GetGraphicsFilterForFrame(this),
        rect, dirty, aFlags, hasSubRect ? &mSubRect : nsnull);
  }
}


//
// DidSetStyleContext
//
// When the style context changes, make sure that all of our image is up to date.
//
/* virtual */ void
nsImageBoxFrame::DidSetStyleContext(nsStyleContext* aOldStyleContext)
{
  nsLeafBoxFrame::DidSetStyleContext(aOldStyleContext);

  // Fetch our subrect.
  const nsStyleList* myList = GetStyleList();
  mSubRect = myList->mImageRegion; // before |mSuppressStyleCheck| test!

  if (mUseSrcAttr || mSuppressStyleCheck)
    return; // No more work required, since the image isn't specified by style.

  // If we're using a native theme implementation, we shouldn't draw anything.
  const nsStyleDisplay* disp = GetStyleDisplay();
  if (disp->mAppearance && nsBox::gTheme &&
      nsBox::gTheme->ThemeSupportsWidget(nsnull, this, disp->mAppearance))
    return;

  // If list-style-image changes, we have a new image.
  nsCOMPtr<nsIURI> oldURI, newURI;
  if (mImageRequest)
    mImageRequest->GetURI(getter_AddRefs(oldURI));
  if (myList->GetListStyleImage())
    myList->GetListStyleImage()->GetURI(getter_AddRefs(newURI));
  bool equal;
  if (newURI == oldURI ||   // handles null==null
      (newURI && oldURI &&
       NS_SUCCEEDED(newURI->Equals(oldURI, &equal)) && equal))
    return;

  UpdateImage();
} // DidSetStyleContext

void
nsImageBoxFrame::GetImageSize()
{
  if (mIntrinsicSize.width > 0 && mIntrinsicSize.height > 0) {
    mImageSize.width = mIntrinsicSize.width;
    mImageSize.height = mIntrinsicSize.height;
  } else {
    mImageSize.width = 0;
    mImageSize.height = 0;
  }
}

/**
 * Ok return our dimensions
 */
nsSize
nsImageBoxFrame::GetPrefSize(nsBoxLayoutState& aState)
{
  nsSize size(0,0);
  DISPLAY_PREF_SIZE(this, size);
  if (DoesNeedRecalc(mImageSize))
     GetImageSize();

  if (!mUseSrcAttr && (mSubRect.width > 0 || mSubRect.height > 0))
    size = nsSize(mSubRect.width, mSubRect.height);
  else
    size = mImageSize;

  nsSize intrinsicSize = size;

  nsMargin borderPadding(0,0,0,0);
  GetBorderAndPadding(borderPadding);
  size.width += borderPadding.LeftRight();
  size.height += borderPadding.TopBottom();

  bool widthSet, heightSet;
  nsIBox::AddCSSPrefSize(this, size, widthSet, heightSet);
  NS_ASSERTION(size.width != NS_INTRINSICSIZE && size.height != NS_INTRINSICSIZE,
               "non-intrinsic size expected");

  nsSize minSize = GetMinSize(aState);
  nsSize maxSize = GetMaxSize(aState);

  if (!widthSet && !heightSet) {
    if (minSize.width != NS_INTRINSICSIZE)
      minSize.width -= borderPadding.LeftRight();
    if (minSize.height != NS_INTRINSICSIZE)
      minSize.height -= borderPadding.TopBottom();
    if (maxSize.width != NS_INTRINSICSIZE)
      maxSize.width -= borderPadding.LeftRight();
    if (maxSize.height != NS_INTRINSICSIZE)
      maxSize.height -= borderPadding.TopBottom();

    size = nsLayoutUtils::ComputeAutoSizeWithIntrinsicDimensions(minSize.width, minSize.height,
                                                                 maxSize.width, maxSize.height,
                                                                 intrinsicSize.width, intrinsicSize.height);
    NS_ASSERTION(size.width != NS_INTRINSICSIZE && size.height != NS_INTRINSICSIZE,
                 "non-intrinsic size expected");
    size.width += borderPadding.LeftRight();
    size.height += borderPadding.TopBottom();
    return size;
  }

  if (!widthSet) {
    if (intrinsicSize.height > 0) {
      // Subtract off the border and padding from the height because the
      // content-box needs to be used to determine the ratio
      nscoord height = size.height - borderPadding.TopBottom();
      size.width = nscoord(PRInt64(height) * PRInt64(intrinsicSize.width) /
                           PRInt64(intrinsicSize.height));
    }
    else {
      size.width = intrinsicSize.width;
    }

    size.width += borderPadding.LeftRight();
  }
  else if (!heightSet) {
    if (intrinsicSize.width > 0) {
      nscoord width = size.width - borderPadding.LeftRight();
      size.height = nscoord(PRInt64(width) * PRInt64(intrinsicSize.height) /
                            PRInt64(intrinsicSize.width));
    }
    else {
      size.height = intrinsicSize.height;
    }

    size.height += borderPadding.TopBottom();
  }

  return BoundsCheck(minSize, size, maxSize);
}

nsSize
nsImageBoxFrame::GetMinSize(nsBoxLayoutState& aState)
{
  // An image can always scale down to (0,0).
  nsSize size(0,0);
  DISPLAY_MIN_SIZE(this, size);
  AddBorderAndPadding(size);
  bool widthSet, heightSet;
  nsIBox::AddCSSMinSize(aState, this, size, widthSet, heightSet);
  return size;
}

nscoord
nsImageBoxFrame::GetBoxAscent(nsBoxLayoutState& aState)
{
  return GetPrefSize(aState).height;
}

nsIAtom*
nsImageBoxFrame::GetType() const
{
  return nsGkAtoms::imageBoxFrame;
}

#ifdef DEBUG
NS_IMETHODIMP
nsImageBoxFrame::GetFrameName(nsAString& aResult) const
{
  return MakeFrameName(NS_LITERAL_STRING("ImageBox"), aResult);
}
#endif


NS_IMETHODIMP nsImageBoxFrame::OnStartContainer(imgIRequest *request,
                                                imgIContainer *image)
{
  NS_ENSURE_ARG_POINTER(image);

  // Ensure the animation (if any) is started. Note: There is no
  // corresponding call to Decrement for this. This Increment will be
  // 'cleaned up' by the Request when it is destroyed, but only then.
  request->IncrementAnimationConsumers();

  nscoord w, h;
  image->GetWidth(&w);
  image->GetHeight(&h);

  mIntrinsicSize.SizeTo(nsPresContext::CSSPixelsToAppUnits(w),
                        nsPresContext::CSSPixelsToAppUnits(h));

  if (!(GetStateBits() & NS_FRAME_FIRST_REFLOW)) {
    PresContext()->PresShell()->
      FrameNeedsReflow(this, nsIPresShell::eStyleChange, NS_FRAME_IS_DIRTY);
  }

  return NS_OK;
}

NS_IMETHODIMP nsImageBoxFrame::OnStopContainer(imgIRequest *request,
                                               imgIContainer *image)
{
  nsBoxLayoutState state(PresContext());
  this->Redraw(state);

  return NS_OK;
}

NS_IMETHODIMP nsImageBoxFrame::OnStopDecode(imgIRequest *request,
                                            nsresult aStatus,
                                            const PRUnichar *statusArg)
{
  if (NS_SUCCEEDED(aStatus))
    // Fire an onload DOM event.
    FireImageDOMEvent(mContent, NS_LOAD);
  else {
    // Fire an onerror DOM event.
    mIntrinsicSize.SizeTo(0, 0);
    PresContext()->PresShell()->
      FrameNeedsReflow(this, nsIPresShell::eStyleChange, NS_FRAME_IS_DIRTY);
    FireImageDOMEvent(mContent, NS_LOAD_ERROR);
  }

  return NS_OK;
}

NS_IMETHODIMP nsImageBoxFrame::OnImageIsAnimated(imgIRequest *aRequest)
{
  // Register with our refresh driver, if we're animated.
  nsLayoutUtils::RegisterImageRequest(PresContext(), aRequest,
                                      &mRequestRegistered);

  return NS_OK;
}

NS_IMETHODIMP nsImageBoxFrame::FrameChanged(imgIRequest *aRequest,
                                            imgIContainer *aContainer,
                                            const nsIntRect *aDirtyRect)
{
  nsBoxLayoutState state(PresContext());
  this->Redraw(state);

  return NS_OK;
}

NS_IMPL_ISUPPORTS2(nsImageBoxListener, imgIDecoderObserver, imgIContainerObserver)

nsImageBoxListener::nsImageBoxListener()
{
}

nsImageBoxListener::~nsImageBoxListener()
{
}

NS_IMETHODIMP nsImageBoxListener::OnStartContainer(imgIRequest *request,
                                                   imgIContainer *image)
{
  if (!mFrame)
    return NS_OK;

  return mFrame->OnStartContainer(request, image);
}

NS_IMETHODIMP nsImageBoxListener::OnStopContainer(imgIRequest *request,
                                                  imgIContainer *image)
{
  if (!mFrame)
    return NS_OK;

  return mFrame->OnStopContainer(request, image);
}

NS_IMETHODIMP nsImageBoxListener::OnStopDecode(imgIRequest *request,
                                               nsresult status,
                                               const PRUnichar *statusArg)
{
  if (!mFrame)
    return NS_OK;

  return mFrame->OnStopDecode(request, status, statusArg);
}

NS_IMETHODIMP nsImageBoxListener::OnImageIsAnimated(imgIRequest* aRequest)
{
  if (!mFrame)
    return NS_OK;

  return mFrame->OnImageIsAnimated(aRequest);
}

NS_IMETHODIMP nsImageBoxListener::FrameChanged(imgIRequest *aRequest,
                                               imgIContainer *aContainer,
                                               const nsIntRect *aDirtyRect)
{
  if (!mFrame)
    return NS_ERROR_FAILURE;

  return mFrame->FrameChanged(aRequest, aContainer, aDirtyRect);
}

