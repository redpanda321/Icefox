/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Class for managing loading of a subframe (creation of the docshell,
 * handling of loads in it, recursion-checking).
 */

#ifndef nsFrameLoader_h_
#define nsFrameLoader_h_

#include "nsIDocShell.h"
#include "nsStringFwd.h"
#include "nsIFrameLoader.h"
#include "nsPoint.h"
#include "nsSize.h"
#include "nsIURI.h"
#include "nsAutoPtr.h"
#include "nsFrameMessageManager.h"
#include "Layers.h"
#include "mozilla/dom/Element.h"
#include "mozilla/Attributes.h"

class nsIURI;
class nsSubDocumentFrame;
class nsIView;
class nsIInProcessContentFrameMessageManager;
class AutoResetInShow;
class nsITabParent;

namespace mozilla {
namespace dom {
class PBrowserParent;
class TabParent;
}

namespace layout {
class RenderFrameParent;
}
}

#ifdef MOZ_WIDGET_GTK2
typedef struct _GtkWidget GtkWidget;
#endif
#ifdef MOZ_WIDGET_QT
class QX11EmbedContainer;
#endif

/**
 * Defines a target configuration for this <browser>'s content
 * document's view.  If the content document's actual view
 * doesn't match this nsIContentView, then on paints its pixels
 * are transformed to compensate for the difference.
 *
 * Used to support asynchronous re-paints of content pixels; see
 * nsIContentView.
 */
class nsContentView MOZ_FINAL : public nsIContentView
{
public:
  typedef mozilla::layers::FrameMetrics::ViewID ViewID;
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTVIEW
 
  struct ViewConfig {
    ViewConfig()
      : mScrollOffset(0, 0)
      , mXScale(1.0)
      , mYScale(1.0)
    {}

    // Default copy ctor and operator= are fine

    bool operator==(const ViewConfig& aOther) const
    {
      return (mScrollOffset == aOther.mScrollOffset &&
              mXScale == aOther.mXScale &&
              mYScale == aOther.mYScale);
    }

    // This is the scroll offset the <browser> user wishes or expects
    // its enclosed content document to have.  "Scroll offset" here
    // means the document pixel at pixel (0,0) within the CSS
    // viewport.  If the content document's actual scroll offset
    // doesn't match |mScrollOffset|, the difference is used to define
    // a translation transform when painting the content document.
    nsPoint mScrollOffset;
    // The scale at which the <browser> user wishes to paint its
    // enclosed content document.  If content-document layers have a
    // lower or higher resolution than the desired scale, then the
    // ratio is used to define a scale transform when painting the
    // content document.
    float mXScale;
    float mYScale;
  };

  nsContentView(nsFrameLoader* aFrameLoader, ViewID aScrollId,
                ViewConfig aConfig = ViewConfig())
    : mViewportSize(0, 0)
    , mContentSize(0, 0)
    , mParentScaleX(1.0)
    , mParentScaleY(1.0)
    , mFrameLoader(aFrameLoader)
    , mScrollId(aScrollId)
    , mConfig(aConfig)
  {}

  bool IsRoot() const;

  ViewID GetId() const
  {
    return mScrollId;
  }

  ViewConfig GetViewConfig() const
  {
    return mConfig;
  }

  nsSize mViewportSize;
  nsSize mContentSize;
  float mParentScaleX;
  float mParentScaleY;

  nsFrameLoader* mFrameLoader;  // WEAK

private:
  nsresult Update(const ViewConfig& aConfig);

  ViewID mScrollId;
  ViewConfig mConfig;
};


class nsFrameLoader MOZ_FINAL : public nsIFrameLoader,
                                public nsIContentViewManager
{
  friend class AutoResetInShow;
  typedef mozilla::dom::PBrowserParent PBrowserParent;
  typedef mozilla::dom::TabParent TabParent;
  typedef mozilla::layout::RenderFrameParent RenderFrameParent;

protected:
  nsFrameLoader(mozilla::dom::Element* aOwner, bool aNetworkCreated);

public:
  ~nsFrameLoader() {
    mNeedsAsyncDestroy = true;
    if (mMessageManager) {
      mMessageManager->Disconnect();
    }
    nsFrameLoader::Destroy();
  }

  bool AsyncScrollEnabled() const
  {
    return !!(mRenderMode & RENDER_MODE_ASYNC_SCROLL);
  }

  static nsFrameLoader* Create(mozilla::dom::Element* aOwner,
                               bool aNetworkCreated);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsFrameLoader, nsIFrameLoader)
  NS_DECL_NSIFRAMELOADER
  NS_DECL_NSICONTENTVIEWMANAGER
  NS_HIDDEN_(nsresult) CheckForRecursiveLoad(nsIURI* aURI);
  nsresult ReallyStartLoading();
  void Finalize();
  nsIDocShell* GetExistingDocShell() { return mDocShell; }
  nsIDOMEventTarget* GetTabChildGlobalAsEventTarget();
  nsresult CreateStaticClone(nsIFrameLoader* aDest);

  /**
   * Called from the layout frame associated with this frame loader;
   * this notifies us to hook up with the widget and view.
   */
  bool Show(PRInt32 marginWidth, PRInt32 marginHeight,
              PRInt32 scrollbarPrefX, PRInt32 scrollbarPrefY,
              nsSubDocumentFrame* frame);

  /**
   * Called when the margin properties of the containing frame are changed.
   */
  void MarginsChanged(PRUint32 aMarginWidth, PRUint32 aMarginHeight);

  /**
   * Called from the layout frame associated with this frame loader, when
   * the frame is being torn down; this notifies us that out widget and view
   * are going away and we should unhook from them.
   */
  void Hide();

  nsresult CloneForStatic(nsIFrameLoader* aOriginal);

  // The guts of an nsIFrameLoaderOwner::SwapFrameLoader implementation.  A
  // frame loader owner needs to call this, and pass in the two references to
  // nsRefPtrs for frame loaders that need to be swapped.
  nsresult SwapWithOtherLoader(nsFrameLoader* aOther,
                               nsRefPtr<nsFrameLoader>& aFirstToSwap,
                               nsRefPtr<nsFrameLoader>& aSecondToSwap);

  // When IPC is enabled, destroy any associated child process.
  void DestroyChild();

  /**
   * Return the primary frame for our owning content, or null if it
   * can't be found.
   */
  nsIFrame* GetPrimaryFrameOfOwningContent() const
  {
    return mOwnerContent ? mOwnerContent->GetPrimaryFrame() : nsnull;
  }

  /** 
   * Return the document that owns this, or null if we don't have
   * an owner.
   */
  nsIDocument* GetOwnerDoc() const
  { return mOwnerContent ? mOwnerContent->OwnerDoc() : nsnull; }

  PBrowserParent* GetRemoteBrowser();

  /**
   * The "current" render frame is the one on which the most recent
   * remote layer-tree transaction was executed.  If no content has
   * been drawn yet, or the remote browser doesn't have any drawn
   * content for whatever reason, return NULL.  The returned render
   * frame has an associated shadow layer tree.
   *
   * Note that the returned render frame might not be a frame
   * constructed for this->GetURL().  This can happen, e.g., if the
   * <browser> was just navigated to a new URL, but hasn't painted the
   * new page yet.  A render frame for the previous page may be
   * returned.  (In-process <browser> behaves similarly, and this
   * behavior seems desirable.)
   */
  RenderFrameParent* GetCurrentRemoteFrame() const
  {
    return mCurrentRemoteFrame;
  }

  /**
   * |aFrame| can be null.  If non-null, it must be the remote frame
   * on which the most recent layer transaction completed for this's
   * <browser>.
   */
  void SetCurrentRemoteFrame(RenderFrameParent* aFrame)
  {
    mCurrentRemoteFrame = aFrame;
  }
  nsFrameMessageManager* GetFrameMessageManager() { return mMessageManager; }

  mozilla::dom::Element* GetOwnerContent() { return mOwnerContent; }
  bool ShouldClipSubdocument() { return mClipSubdocument; }

  bool ShouldClampScrollPosition() { return mClampScrollPosition; }

  /**
   * Tell this FrameLoader to use a particular remote browser.
   *
   * This will assert if mRemoteBrowser or mCurrentRemoteFrame is non-null.  In
   * practice, this means you can't have successfully run TryRemoteBrowser() on
   * this object, which means you can't have called ShowRemoteFrame() or
   * ReallyStartLoading().
   */
  void SetRemoteBrowser(nsITabParent* aTabParent);

private:

  void SetOwnerContent(mozilla::dom::Element* aContent);

  bool ShouldUseRemoteProcess();

  /**
   * Is this a frameloader for a bona fide <iframe mozbrowser> or
   * <iframe mozapp>?  (I.e., does the frame return true for
   * nsIMozBrowserFrame::GetReallyIsBrowser()?)
   */
  bool OwnerIsBrowserFrame();

  /**
   * Is this a frameloader for a bona fide <iframe mozapp>?  (I.e., does the
   * frame return true for nsIMozBrowserFrame::GetReallyIsApp()?)
   */
  bool OwnerIsAppFrame();

  /**
   * Get our owning element's app manifest URL, or return the empty string if
   * our owning element doesn't have an app manifest URL.
   */
  void GetOwnerAppManifestURL(nsAString& aOut);

  /**
   * If we are an IPC frame, set mRemoteFrame. Otherwise, create and
   * initialize mDocShell.
   */
  nsresult MaybeCreateDocShell();
  nsresult EnsureMessageManager();
  NS_HIDDEN_(void) GetURL(nsString& aURL);

  // Properly retrieves documentSize of any subdocument type.
  NS_HIDDEN_(nsIntSize) GetSubDocumentSize(const nsIFrame *aIFrame);
  nsresult GetWindowDimensions(nsRect& aRect);

  // Updates the subdocument position and size. This gets called only
  // when we have our own in-process DocShell.
  NS_HIDDEN_(nsresult) UpdateBaseWindowPositionAndSize(nsIFrame *aIFrame);
  nsresult CheckURILoad(nsIURI* aURI);
  void FireErrorEvent();
  nsresult ReallyStartLoadingInternal();

  // Return true if remote browser created; nothing else to do
  bool TryRemoteBrowser();

  // Tell the remote browser that it's now "virtually visible"
  bool ShowRemoteFrame(const nsIntSize& size);

  nsCOMPtr<nsIDocShell> mDocShell;
  nsCOMPtr<nsIURI> mURIToLoad;
  mozilla::dom::Element* mOwnerContent; // WEAK
public:
  // public because a callback needs these.
  nsRefPtr<nsFrameMessageManager> mMessageManager;
  nsCOMPtr<nsIInProcessContentFrameMessageManager> mChildMessageManager;
private:
  bool mDepthTooGreat : 1;
  bool mIsTopLevelContent : 1;
  bool mDestroyCalled : 1;
  bool mNeedsAsyncDestroy : 1;
  bool mInSwap : 1;
  bool mInShow : 1;
  bool mHideCalled : 1;
  // True when the object is created for an element which the parser has
  // created using NS_FROM_PARSER_NETWORK flag. If the element is modified,
  // it may lose the flag.
  bool mNetworkCreated : 1;

  bool mDelayRemoteDialogs : 1;
  bool mRemoteBrowserShown : 1;
  bool mRemoteFrame : 1;
  bool mClipSubdocument : 1;
  bool mClampScrollPosition : 1;
  bool mRemoteBrowserInitialized : 1;

  // XXX leaking
  nsCOMPtr<nsIObserver> mChildHost;
  RenderFrameParent* mCurrentRemoteFrame;
  TabParent* mRemoteBrowser;

  // See nsIFrameLoader.idl.  Short story, if !(mRenderMode &
  // RENDER_MODE_ASYNC_SCROLL), all the fields below are ignored in
  // favor of what content tells.
  PRUint32 mRenderMode;

  // See nsIFrameLoader.idl. EVENT_MODE_NORMAL_DISPATCH automatically
  // forwards some input events to out-of-process content.
  PRUint32 mEventMode;
};

#endif
