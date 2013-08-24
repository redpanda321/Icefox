/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCanvasRenderingContext2DAzure_h
#define nsCanvasRenderingContext2DAzure_h

#include <vector>
#include "nsIDOMCanvasRenderingContext2D.h"
#include "nsICanvasRenderingContextInternal.h"
#include "mozilla/RefPtr.h"
#include "nsColor.h"
#include "nsHTMLCanvasElement.h"
#include "CanvasUtils.h"
#include "nsHTMLImageElement.h"
#include "nsHTMLVideoElement.h"
#include "gfxFont.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/ImageData.h"
#include "mozilla/dom/UnionTypes.h"

namespace mozilla {
namespace dom {
template<typename T> class Optional;
}
namespace gfx {
struct Rect;
class SourceSurface;
}
}

extern const mozilla::gfx::Float SIGMA_MAX;

/**
 ** nsCanvasGradientAzure
 **/
#define NS_CANVASGRADIENTAZURE_PRIVATE_IID \
    {0x28425a6a, 0x90e0, 0x4d42, {0x9c, 0x75, 0xff, 0x60, 0x09, 0xb3, 0x10, 0xa8}}
class nsCanvasGradientAzure : public nsIDOMCanvasGradient
{
public:
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_CANVASGRADIENTAZURE_PRIVATE_IID)

  enum Type
  {
    LINEAR = 0,
    RADIAL
  };

  Type GetType()
  {
    return mType;
  }


  mozilla::gfx::GradientStops *
  GetGradientStopsForTarget(mozilla::gfx::DrawTarget *aRT)
  {
    if (mStops && mStops->GetBackendType() == aRT->GetType()) {
      return mStops;
    }

    mStops = aRT->CreateGradientStops(mRawStops.Elements(), mRawStops.Length());

    return mStops;
  }

  NS_DECL_ISUPPORTS

  /* nsIDOMCanvasGradient */
  NS_IMETHOD AddColorStop(float offset, const nsAString& colorstr);

protected:
  nsCanvasGradientAzure(Type aType) : mType(aType)
  {}

  nsTArray<mozilla::gfx::GradientStop> mRawStops;
  mozilla::RefPtr<mozilla::gfx::GradientStops> mStops;
  Type mType;
  virtual ~nsCanvasGradientAzure() {}
};

/**
 ** nsCanvasPatternAzure
 **/
#define NS_CANVASPATTERNAZURE_PRIVATE_IID \
    {0xc9bacc25, 0x28da, 0x421e, {0x9a, 0x4b, 0xbb, 0xd6, 0x93, 0x05, 0x12, 0xbc}}
class nsCanvasPatternAzure MOZ_FINAL : public nsIDOMCanvasPattern
{
public:
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_CANVASPATTERNAZURE_PRIVATE_IID)

  enum RepeatMode
  {
    REPEAT,
    REPEATX,
    REPEATY,
    NOREPEAT
  };

  nsCanvasPatternAzure(mozilla::gfx::SourceSurface* aSurface,
                       RepeatMode aRepeat,
                       nsIPrincipal* principalForSecurityCheck,
                       bool forceWriteOnly,
                       bool CORSUsed)
    : mSurface(aSurface)
    , mRepeat(aRepeat)
    , mPrincipal(principalForSecurityCheck)
    , mForceWriteOnly(forceWriteOnly)
    , mCORSUsed(CORSUsed)
  {
  }

  NS_DECL_ISUPPORTS

  mozilla::RefPtr<mozilla::gfx::SourceSurface> mSurface;
  const RepeatMode mRepeat;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  const bool mForceWriteOnly;
  const bool mCORSUsed;
};

struct nsCanvasBidiProcessorAzure;
class CanvasRenderingContext2DUserDataAzure;

/**
 ** nsCanvasRenderingContext2DAzure
 **/
class nsCanvasRenderingContext2DAzure :
  public nsIDOMCanvasRenderingContext2D,
  public nsICanvasRenderingContextInternal,
  public nsWrapperCache
{
typedef mozilla::dom::HTMLImageElementOrHTMLCanvasElementOrHTMLVideoElement
  HTMLImageOrCanvasOrVideoElement;

public:
  nsCanvasRenderingContext2DAzure();
  virtual ~nsCanvasRenderingContext2DAzure();

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope,
                               bool *triedToWrap);

  nsHTMLCanvasElement* GetCanvas() const
  {
    return mCanvasElement;
  }

  void Save();
  void Restore();
  void Scale(double x, double y, mozilla::ErrorResult& error);
  void Rotate(double angle, mozilla::ErrorResult& error);
  void Translate(double x, double y, mozilla::ErrorResult& error);
  void Transform(double m11, double m12, double m21, double m22, double dx,
                 double dy, mozilla::ErrorResult& error);
  void SetTransform(double m11, double m12, double m21, double m22, double dx,
                    double dy, mozilla::ErrorResult& error);

  double GetGlobalAlpha()
  {
    return CurrentState().globalAlpha;
  }

  void SetGlobalAlpha(double globalAlpha)
  {
    if (mozilla::CanvasUtils::FloatValidate(globalAlpha) &&
        globalAlpha >= 0.0 && globalAlpha <= 1.0) {
      CurrentState().globalAlpha = globalAlpha;
    }
  }

  void GetGlobalCompositeOperation(nsAString& op, mozilla::ErrorResult& error);
  void SetGlobalCompositeOperation(const nsAString& op,
                                   mozilla::ErrorResult& error);
  JS::Value GetStrokeStyle(JSContext* cx, mozilla::ErrorResult& error);

  void SetStrokeStyle(JSContext* cx, JS::Value& value)
  {
    SetStyleFromJSValue(cx, value, STYLE_STROKE);
  }

  JS::Value GetFillStyle(JSContext* cx, mozilla::ErrorResult& error);

  void SetFillStyle(JSContext* cx, JS::Value& value)
  {
    SetStyleFromJSValue(cx, value, STYLE_FILL);
  }

  already_AddRefed<nsIDOMCanvasGradient>
    CreateLinearGradient(double x0, double y0, double x1, double y1,
                         mozilla::ErrorResult& aError);
  already_AddRefed<nsIDOMCanvasGradient>
    CreateRadialGradient(double x0, double y0, double r0, double x1, double y1,
                         double r1, mozilla::ErrorResult& aError);
  already_AddRefed<nsIDOMCanvasPattern>
    CreatePattern(const HTMLImageOrCanvasOrVideoElement& element,
                  const nsAString& repeat, mozilla::ErrorResult& error);

  double GetShadowOffsetX()
  {
    return CurrentState().shadowOffset.x;
  }

  void SetShadowOffsetX(double shadowOffsetX)
  {
    if (mozilla::CanvasUtils::FloatValidate(shadowOffsetX)) {
      CurrentState().shadowOffset.x = shadowOffsetX;
    }
  }

  double GetShadowOffsetY()
  {
    return CurrentState().shadowOffset.y;
  }

  void SetShadowOffsetY(double shadowOffsetY)
  {
    if (mozilla::CanvasUtils::FloatValidate(shadowOffsetY)) {
      CurrentState().shadowOffset.y = shadowOffsetY;
    }
  }

  double GetShadowBlur()
  {
    return CurrentState().shadowBlur;
  }

  void SetShadowBlur(double shadowBlur)
  {
    if (mozilla::CanvasUtils::FloatValidate(shadowBlur) && shadowBlur >= 0.0) {
      CurrentState().shadowBlur = shadowBlur;
    }
  }

  void GetShadowColor(nsAString& shadowColor)
  {
    StyleColorToString(CurrentState().shadowColor, shadowColor);
  }

  void SetShadowColor(const nsAString& shadowColor);
  void ClearRect(double x, double y, double w, double h);
  void FillRect(double x, double y, double w, double h);
  void StrokeRect(double x, double y, double w, double h);
  void BeginPath();
  void Fill();
  void Stroke();
  void Clip();
  bool IsPointInPath(double x, double y);
  void FillText(const nsAString& text, double x, double y,
                const mozilla::dom::Optional<double>& maxWidth,
                mozilla::ErrorResult& error);
  void StrokeText(const nsAString& text, double x, double y,
                  const mozilla::dom::Optional<double>& maxWidth,
                  mozilla::ErrorResult& error);
  already_AddRefed<nsIDOMTextMetrics>
    MeasureText(const nsAString& rawText, mozilla::ErrorResult& error);

  void DrawImage(const HTMLImageOrCanvasOrVideoElement& image,
                 double dx, double dy, mozilla::ErrorResult& error)
  {
    if (!mozilla::CanvasUtils::FloatValidate(dx, dy)) {
      return;
    }
    DrawImage(image, 0.0, 0.0, 0.0, 0.0, dx, dy, 0.0, 0.0, 0, error);
  }

  void DrawImage(const HTMLImageOrCanvasOrVideoElement& image,
                 double dx, double dy, double dw, double dh,
                 mozilla::ErrorResult& error)
  {
    if (!mozilla::CanvasUtils::FloatValidate(dx, dy, dw, dh)) {
      return;
    }
    DrawImage(image, 0.0, 0.0, 0.0, 0.0, dx, dy, dw, dh, 2, error);
  }

  void DrawImage(const HTMLImageOrCanvasOrVideoElement& image,
                 double sx, double sy, double sw, double sh, double dx,
                 double dy, double dw, double dh, mozilla::ErrorResult& error)
  {
    if (!mozilla::CanvasUtils::FloatValidate(sx, sy, sw, sh) ||
        !mozilla::CanvasUtils::FloatValidate(dx, dy, dw, dh)) {
      return;
    }
    DrawImage(image, sx, sy, sw, sh, dx, dy, dw, dh, 6, error);
  }

  already_AddRefed<mozilla::dom::ImageData>
    CreateImageData(JSContext* cx, double sw, double sh,
                    mozilla::ErrorResult& error);
  already_AddRefed<mozilla::dom::ImageData>
    CreateImageData(JSContext* cx, mozilla::dom::ImageData* imagedata,
                    mozilla::ErrorResult& error);
  already_AddRefed<mozilla::dom::ImageData>
    GetImageData(JSContext* cx, double sx, double sy, double sw, double sh,
                 mozilla::ErrorResult& error);
  void PutImageData(JSContext* cx, mozilla::dom::ImageData* imageData,
                    double dx, double dy, mozilla::ErrorResult& error);
  void PutImageData(JSContext* cx, mozilla::dom::ImageData* imageData,
                    double dx, double dy, double dirtyX, double dirtyY,
                    double dirtyWidth, double dirtyHeight,
                    mozilla::ErrorResult& error);

  double GetLineWidth()
  {
    return CurrentState().lineWidth;
  }

  void SetLineWidth(double width)
  {
    if (mozilla::CanvasUtils::FloatValidate(width) && width > 0.0) {
      CurrentState().lineWidth = width;
    }
  }
  void GetLineCap(nsAString& linecap);
  void SetLineCap(const nsAString& linecap);
  void GetLineJoin(nsAString& linejoin, mozilla::ErrorResult& error);
  void SetLineJoin(const nsAString& linejoin);

  double GetMiterLimit()
  {
    return CurrentState().miterLimit;
  }

  void SetMiterLimit(double miter)
  {
    if (mozilla::CanvasUtils::FloatValidate(miter) && miter > 0.0) {
      CurrentState().miterLimit = miter;
    }
  }

  void GetFont(nsAString& font)
  {
    font = GetFont();
  }

  void SetFont(const nsAString& font, mozilla::ErrorResult& error);
  void GetTextAlign(nsAString& textAlign);
  void SetTextAlign(const nsAString& textAlign);
  void GetTextBaseline(nsAString& textBaseline);
  void SetTextBaseline(const nsAString& textBaseline);

  void ClosePath()
  {
    EnsureWritablePath();

    if (mPathBuilder) {
      mPathBuilder->Close();
    } else {
      mDSPathBuilder->Close();
    }
  }

  void MoveTo(double x, double y)
  {
    if (mozilla::CanvasUtils::FloatValidate(x, y)) {
      EnsureWritablePath();

      if (mPathBuilder) {
        mPathBuilder->MoveTo(mozilla::gfx::Point(x, y));
      } else {
        mDSPathBuilder->MoveTo(mTarget->GetTransform() *
                                 mozilla::gfx::Point(x, y));
      }
    }
  }

  void LineTo(double x, double y)
  {
    if (mozilla::CanvasUtils::FloatValidate(x, y)) {
      EnsureWritablePath();
    
      LineTo(mozilla::gfx::Point(x, y));
    }
  }

  void QuadraticCurveTo(double cpx, double cpy, double x, double y)
  {
    if (mozilla::CanvasUtils::FloatValidate(cpx, cpy, x, y)) {
      EnsureWritablePath();

      if (mPathBuilder) {
        mPathBuilder->QuadraticBezierTo(mozilla::gfx::Point(cpx, cpy),
                                        mozilla::gfx::Point(x, y));
      } else {
        mozilla::gfx::Matrix transform = mTarget->GetTransform();
        mDSPathBuilder->QuadraticBezierTo(transform *
                                            mozilla::gfx::Point(cpx, cpy),
                                          transform *
                                            mozilla::gfx::Point(x, y));
      }
    }
  }

  void BezierCurveTo(double cp1x, double cp1y, double cp2x, double cp2y, double x, double y)
  {
    if (mozilla::CanvasUtils::FloatValidate(cp1x, cp1y, cp2x, cp2y, x, y)) {
      EnsureWritablePath();

      BezierTo(mozilla::gfx::Point(cp1x, cp1y),
               mozilla::gfx::Point(cp2x, cp2y),
               mozilla::gfx::Point(x, y));
    }
  }

  void ArcTo(double x1, double y1, double x2, double y2, double radius,
             mozilla::ErrorResult& error);
  void Rect(double x, double y, double w, double h);
  void Arc(double x, double y, double radius, double startAngle,
           double endAngle, bool anticlockwise, mozilla::ErrorResult& error);

  JSObject* GetMozCurrentTransform(JSContext* cx,
                                   mozilla::ErrorResult& error) const;
  void SetMozCurrentTransform(JSContext* cx, JSObject& currentTransform,
                              mozilla::ErrorResult& error);
  JSObject* GetMozCurrentTransformInverse(JSContext* cx,
                                          mozilla::ErrorResult& error) const;
  void SetMozCurrentTransformInverse(JSContext* cx, JSObject& currentTransform, 
                                     mozilla::ErrorResult& error);
  void GetFillRule(nsAString& fillRule);
  void SetFillRule(const nsAString& fillRule);
  JS::Value GetMozDash(JSContext* cx, mozilla::ErrorResult& error);
  void SetMozDash(JSContext* cx, const JS::Value& mozDash,
                  mozilla::ErrorResult& error);

  double GetMozDashOffset()
  {
    return CurrentState().dashOffset;
  }

  void SetMozDashOffset(double mozDashOffset, mozilla::ErrorResult& error);

  void GetMozTextStyle(nsAString& mozTextStyle)
  {
    GetFont(mozTextStyle);
  }

  void SetMozTextStyle(const nsAString& mozTextStyle,
                       mozilla::ErrorResult& error)
  {
    SetFont(mozTextStyle, error);
  }

  bool GetImageSmoothingEnabled()
  {
    return CurrentState().imageSmoothingEnabled;
  }

  void SetImageSmoothingEnabled(bool imageSmoothingEnabled)
  {
    if (imageSmoothingEnabled != CurrentState().imageSmoothingEnabled) {
      CurrentState().imageSmoothingEnabled = imageSmoothingEnabled;
    }
  }

  void DrawWindow(nsIDOMWindow* window, double x, double y, double w, double h,
                  const nsAString& bgColor, uint32_t flags,
                  mozilla::ErrorResult& error);
  void AsyncDrawXULElement(nsIDOMXULElement* elem, double x, double y, double w,
                           double h, const nsAString& bgColor, uint32_t flags,
                           mozilla::ErrorResult& error);

  nsresult Redraw();

  // nsICanvasRenderingContextInternal
  NS_IMETHOD SetDimensions(PRInt32 width, PRInt32 height);
  NS_IMETHOD InitializeWithSurface(nsIDocShell *shell, gfxASurface *surface, PRInt32 width, PRInt32 height)
  { return NS_ERROR_NOT_IMPLEMENTED; }

  NS_IMETHOD Render(gfxContext *ctx,
                    gfxPattern::GraphicsFilter aFilter,
                    PRUint32 aFlags = RenderFlagPremultAlpha);
  NS_IMETHOD GetInputStream(const char* aMimeType,
                            const PRUnichar* aEncoderOptions,
                            nsIInputStream **aStream);
  NS_IMETHOD GetThebesSurface(gfxASurface **surface);

  mozilla::TemporaryRef<mozilla::gfx::SourceSurface> GetSurfaceSnapshot()
  { return mTarget ? mTarget->Snapshot() : nsnull; }

  NS_IMETHOD SetIsOpaque(bool isOpaque);
  NS_IMETHOD Reset();
  already_AddRefed<CanvasLayer> GetCanvasLayer(nsDisplayListBuilder* aBuilder,
                                                CanvasLayer *aOldLayer,
                                                LayerManager *aManager);
  void MarkContextClean();
  NS_IMETHOD SetIsIPC(bool isIPC);
  // this rect is in canvas device space
  void Redraw(const mozilla::gfx::Rect &r);
  NS_IMETHOD Redraw(const gfxRect &r) { Redraw(ToRect(r)); return NS_OK; }

  // this rect is in mTarget's current user space
  void RedrawUser(const gfxRect &r);

  // nsISupports interface + CC
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS_AMBIGUOUS(nsCanvasRenderingContext2DAzure,
                                                                   nsIDOMCanvasRenderingContext2D)

  // nsIDOMCanvasRenderingContext2D interface
  NS_DECL_NSIDOMCANVASRENDERINGCONTEXT2D

  enum Style {
    STYLE_STROKE = 0,
    STYLE_FILL,
    STYLE_MAX
  };

  nsINode* GetParentObject()
  {
    return mCanvasElement;
  }

  void LineTo(const mozilla::gfx::Point& aPoint)
  {
    if (mPathBuilder) {
      mPathBuilder->LineTo(aPoint);
    } else {
      mDSPathBuilder->LineTo(mTarget->GetTransform() * aPoint);
    }
  }

  void BezierTo(const mozilla::gfx::Point& aCP1,
                const mozilla::gfx::Point& aCP2,
                const mozilla::gfx::Point& aCP3)
  {
    if (mPathBuilder) {
      mPathBuilder->BezierTo(aCP1, aCP2, aCP3);
    } else {
      mozilla::gfx::Matrix transform = mTarget->GetTransform();
      mDSPathBuilder->BezierTo(transform * aCP1,
                                transform * aCP2,
                                transform * aCP3);
    }
  }

  friend class CanvasRenderingContext2DUserDataAzure;

protected:
  nsresult GetImageDataArray(JSContext* aCx, int32_t aX, int32_t aY,
                             uint32_t aWidth, uint32_t aHeight,
                             JSObject** aRetval);

  nsresult InitializeWithTarget(mozilla::gfx::DrawTarget *surface,
                                PRInt32 width, PRInt32 height);

  /**
    * The number of living nsCanvasRenderingContexts.  When this goes down to
    * 0, we free the premultiply and unpremultiply tables, if they exist.
    */
  static PRUint32 sNumLivingContexts;

  /**
    * Lookup table used to speed up GetImageData().
    */
  static PRUint8 (*sUnpremultiplyTable)[256];

  /**
    * Lookup table used to speed up PutImageData().
    */
  static PRUint8 (*sPremultiplyTable)[256];

  // Some helpers.  Doesn't modify a color on failure.
  void SetStyleFromJSValue(JSContext* cx, JS::Value& value, Style whichStyle);
  void SetStyleFromString(const nsAString& str, Style whichStyle);

  void SetStyleFromGradient(nsCanvasGradientAzure *gradient, Style whichStyle)
  {
    CurrentState().SetGradientStyle(whichStyle, gradient);
  }

  void SetStyleFromPattern(nsCanvasPatternAzure *pattern, Style whichStyle)
  {
    CurrentState().SetPatternStyle(whichStyle, pattern);
  }

  void SetStyleFromStringOrInterface(const nsAString& aStr, nsISupports *aInterface, Style aWhichStyle);
  nsISupports* GetStyleAsStringOrInterface(nsAString& aStr, CanvasMultiGetterType& aType, Style aWhichStyle);

  // Returns whether a color was successfully parsed.
  bool ParseColor(const nsAString& aString, nscolor* aColor);

  static void StyleColorToString(const nscolor& aColor, nsAString& aStr);

  /**
    * Creates the unpremultiply lookup table, if it doesn't exist.
    */
  void EnsureUnpremultiplyTable();

  /**
    * Creates the premultiply lookup table, if it doesn't exist.
    */
  void EnsurePremultiplyTable();

  /* This function ensures there is a writable pathbuilder available, this
   * pathbuilder may be working in user space or in device space or
   * device space.
   */
  void EnsureWritablePath();

  // Ensures a path in UserSpace is available.
  void EnsureUserSpacePath();

  void TransformWillUpdate();

  // Report the fillRule has changed.
  void FillRuleChanged();

  /**
    * Returns the surface format this canvas should be allocated using. Takes
    * into account mOpaque, platform requirements, etc.
    */
  mozilla::gfx::SurfaceFormat GetSurfaceFormat() const;

  void DrawImage(const HTMLImageOrCanvasOrVideoElement &imgElt,
                 double sx, double sy, double sw, double sh,
                 double dx, double dy, double dw, double dh, 
                 PRUint8 optional_argc, mozilla::ErrorResult& error);

  nsString& GetFont()
  {
    /* will initilize the value if not set, else does nothing */
    GetCurrentFontStyle();

    return CurrentState().font;
  }

  static bool
  ToHTMLImageOrCanvasOrVideoElement(nsIDOMElement* html,
                                    HTMLImageOrCanvasOrVideoElement& element)
  {
    nsCOMPtr<nsIContent> content = do_QueryInterface(html);
    if (content) {
      if (content->IsHTML(nsGkAtoms::canvas)) {
        element.SetAsHTMLCanvasElement() =
          static_cast<nsHTMLCanvasElement*>(html);
        return true;
      }
      if (content->IsHTML(nsGkAtoms::img)) {
        element.SetAsHTMLImageElement() =
          static_cast<nsHTMLImageElement*>(html);
        return true;
      }
      if (content->IsHTML(nsGkAtoms::video)) {
        element.SetAsHTMLVideoElement() =
          static_cast<nsHTMLVideoElement*>(html);
        return true;
      }
    }

    return false;
  }

  // Member vars
  PRInt32 mWidth, mHeight;

  // This is true when the canvas is valid, false otherwise, this occurs when
  // for some reason initialization of the drawtarget fails. If the canvas
  // is invalid certain behavior is expected.
  bool mValid;
  // This is true when the canvas is valid, but of zero size, this requires
  // specific behavior on some operations.
  bool mZero;

  bool mOpaque;

  // This is true when the next time our layer is retrieved we need to
  // recreate it (i.e. our backing surface changed)
  bool mResetLayer;
  // This is needed for drawing in drawAsyncXULElement
  bool mIPC;

  nsTArray<CanvasRenderingContext2DUserDataAzure*> mUserDatas;

  // If mCanvasElement is not provided, then a docshell is
  nsCOMPtr<nsIDocShell> mDocShell;

  // our drawing surfaces, contexts, and layers
  mozilla::RefPtr<mozilla::gfx::DrawTarget> mTarget;

  /**
    * Flag to avoid duplicate calls to InvalidateFrame. Set to true whenever
    * Redraw is called, reset to false when Render is called.
    */
  bool mIsEntireFrameInvalid;
  /**
    * When this is set, the first call to Redraw(gfxRect) should set
    * mIsEntireFrameInvalid since we expect it will be followed by
    * many more Redraw calls.
    */
  bool mPredictManyRedrawCalls;

  // This is stored after GetThebesSurface has been called once to avoid
  // excessive ThebesSurface initialization overhead.
  nsRefPtr<gfxASurface> mThebesSurface;

  /**
    * We also have a device space pathbuilder. The reason for this is as
    * follows, when a path is being built, but the transform changes, we
    * can no longer keep a single path in userspace, considering there's
    * several 'user spaces' now. We therefore transform the current path
    * into device space, and add all operations to this path in device
    * space.
    *
    * When then finally executing a render, the Azure drawing API expects
    * the path to be in userspace. We could then set an identity transform
    * on the DrawTarget and do all drawing in device space. This is
    * undesirable because it requires transforming patterns, gradients,
    * clips, etc. into device space and it would not work for stroking.
    * What we do instead is convert the path back to user space when it is
    * drawn, and draw it with the current transform. This makes all drawing
    * occur correctly.
    *
    * There's never both a device space path builder and a user space path
    * builder present at the same time. There is also never a path and a
    * path builder present at the same time. When writing proceeds on an
    * existing path the Path is cleared and a new builder is created.
    *
    * mPath is always in user-space.
    */
  mozilla::RefPtr<mozilla::gfx::Path> mPath;
  mozilla::RefPtr<mozilla::gfx::PathBuilder> mDSPathBuilder;
  mozilla::RefPtr<mozilla::gfx::PathBuilder> mPathBuilder;
  bool mPathTransformWillUpdate;
  mozilla::gfx::Matrix mPathToDS;

  /**
    * Number of times we've invalidated before calling redraw
    */
  PRUint32 mInvalidateCount;
  static const PRUint32 kCanvasMaxInvalidateCount = 100;

  /**
    * Returns true if a shadow should be drawn along with a
    * drawing operation.
    */
  bool NeedToDrawShadow()
  {
    const ContextState& state = CurrentState();

    // The spec says we should not draw shadows if the operator is OVER.
    // If it's over and the alpha value is zero, nothing needs to be drawn.
    return NS_GET_A(state.shadowColor) != 0 && 
      (state.shadowBlur != 0 || state.shadowOffset.x != 0 || state.shadowOffset.y != 0);
  }

  mozilla::gfx::CompositionOp UsedOperation()
  {
    if (NeedToDrawShadow()) {
      // In this case the shadow rendering will use the operator.
      return mozilla::gfx::OP_OVER;
    }

    return CurrentState().op;
  }

  /**
    * Gets the pres shell from either the canvas element or the doc shell
    */
  nsIPresShell *GetPresShell() {
    if (mCanvasElement) {
      return mCanvasElement->OwnerDoc()->GetShell();
    }
    if (mDocShell) {
      nsCOMPtr<nsIPresShell> shell;
      mDocShell->GetPresShell(getter_AddRefs(shell));
      return shell.get();
    }
    return nsnull;
  }

  // text
  enum TextAlign {
    TEXT_ALIGN_START,
    TEXT_ALIGN_END,
    TEXT_ALIGN_LEFT,
    TEXT_ALIGN_RIGHT,
    TEXT_ALIGN_CENTER
  };

  enum TextBaseline {
    TEXT_BASELINE_TOP,
    TEXT_BASELINE_HANGING,
    TEXT_BASELINE_MIDDLE,
    TEXT_BASELINE_ALPHABETIC,
    TEXT_BASELINE_IDEOGRAPHIC,
    TEXT_BASELINE_BOTTOM
  };

  gfxFontGroup *GetCurrentFontStyle();

  enum TextDrawOperation {
    TEXT_DRAW_OPERATION_FILL,
    TEXT_DRAW_OPERATION_STROKE,
    TEXT_DRAW_OPERATION_MEASURE
  };

  /*
    * Implementation of the fillText, strokeText, and measure functions with
    * the operation abstracted to a flag.
    */
  nsresult DrawOrMeasureText(const nsAString& text,
                              float x,
                              float y,
                              const mozilla::dom::Optional<double>& maxWidth,
                              TextDrawOperation op,
                              float* aWidth);

  // state stack handling
  class ContextState {
  public:
      ContextState() : textAlign(TEXT_ALIGN_START),
                       textBaseline(TEXT_BASELINE_ALPHABETIC),
                       lineWidth(1.0f),
                       miterLimit(10.0f),
                       globalAlpha(1.0f),
                       shadowBlur(0.0),
                       dashOffset(0.0f),
                       op(mozilla::gfx::OP_OVER),
                       fillRule(mozilla::gfx::FILL_WINDING),
                       lineCap(mozilla::gfx::CAP_BUTT),
                       lineJoin(mozilla::gfx::JOIN_MITER_OR_BEVEL),
                       imageSmoothingEnabled(true)
      { }

      ContextState(const ContextState& other)
          : fontGroup(other.fontGroup),
            font(other.font),
            textAlign(other.textAlign),
            textBaseline(other.textBaseline),
            shadowColor(other.shadowColor),
            transform(other.transform),
            shadowOffset(other.shadowOffset),
            lineWidth(other.lineWidth),
            miterLimit(other.miterLimit),
            globalAlpha(other.globalAlpha),
            shadowBlur(other.shadowBlur),
            dash(other.dash),
            dashOffset(other.dashOffset),
            op(other.op),
            fillRule(other.fillRule),
            lineCap(other.lineCap),
            lineJoin(other.lineJoin),
            imageSmoothingEnabled(other.imageSmoothingEnabled)
      {
          for (int i = 0; i < STYLE_MAX; i++) {
              colorStyles[i] = other.colorStyles[i];
              gradientStyles[i] = other.gradientStyles[i];
              patternStyles[i] = other.patternStyles[i];
          }
      }

      void SetColorStyle(Style whichStyle, nscolor color) {
          colorStyles[whichStyle] = color;
          gradientStyles[whichStyle] = nsnull;
          patternStyles[whichStyle] = nsnull;
      }

      void SetPatternStyle(Style whichStyle, nsCanvasPatternAzure* pat) {
          gradientStyles[whichStyle] = nsnull;
          patternStyles[whichStyle] = pat;
      }

      void SetGradientStyle(Style whichStyle, nsCanvasGradientAzure* grad) {
          gradientStyles[whichStyle] = grad;
          patternStyles[whichStyle] = nsnull;
      }

      /**
        * returns true iff the given style is a solid color.
        */
      bool StyleIsColor(Style whichStyle) const
      {
          return !(patternStyles[whichStyle] || gradientStyles[whichStyle]);
      }


      std::vector<mozilla::RefPtr<mozilla::gfx::Path> > clipsPushed;

      nsRefPtr<gfxFontGroup> fontGroup;
      nsRefPtr<nsCanvasGradientAzure> gradientStyles[STYLE_MAX];
      nsRefPtr<nsCanvasPatternAzure> patternStyles[STYLE_MAX];

      nsString font;
      TextAlign textAlign;
      TextBaseline textBaseline;

      nscolor colorStyles[STYLE_MAX];
      nscolor shadowColor;

      mozilla::gfx::Matrix transform;
      mozilla::gfx::Point shadowOffset;
      mozilla::gfx::Float lineWidth;
      mozilla::gfx::Float miterLimit;
      mozilla::gfx::Float globalAlpha;
      mozilla::gfx::Float shadowBlur;
      FallibleTArray<mozilla::gfx::Float> dash;
      mozilla::gfx::Float dashOffset;

      mozilla::gfx::CompositionOp op;
      mozilla::gfx::FillRule fillRule;
      mozilla::gfx::CapStyle lineCap;
      mozilla::gfx::JoinStyle lineJoin;

      bool imageSmoothingEnabled;
  };

  nsAutoTArray<ContextState, 3> mStyleStack;

  inline ContextState& CurrentState() {
    return mStyleStack[mStyleStack.Length() - 1];
  }

  friend class CanvasGeneralPattern;
  friend class AdjustedTarget;

  // other helpers
  void GetAppUnitsValues(PRUint32 *perDevPixel, PRUint32 *perCSSPixel) {
    // If we don't have a canvas element, we just return something generic.
    PRUint32 devPixel = 60;
    PRUint32 cssPixel = 60;

    nsIPresShell *ps = GetPresShell();
    nsPresContext *pc;

    if (!ps) goto FINISH;
    pc = ps->GetPresContext();
    if (!pc) goto FINISH;
    devPixel = pc->AppUnitsPerDevPixel();
    cssPixel = pc->AppUnitsPerCSSPixel();

  FINISH:
    if (perDevPixel)
      *perDevPixel = devPixel;
    if (perCSSPixel)
      *perCSSPixel = cssPixel;
  }

  friend struct nsCanvasBidiProcessorAzure;
};

#endif /* nsCanvasRenderingContext2DAzure_h */
