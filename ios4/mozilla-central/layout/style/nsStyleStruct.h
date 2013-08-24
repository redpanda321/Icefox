/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 *   Mats Palmgren <mats.palmgren@bredband.net>
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *   Rob Arnold <robarnold@mozilla.com>
 *   Jonathon Jongsma <jonathon.jongsma@collabora.co.uk>, Collabora Ltd.
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation
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
 * structs that contain the data provided by nsStyleContext, the
 * internal API for computed style data for an element
 */

#ifndef nsStyleStruct_h___
#define nsStyleStruct_h___

#include "nsColor.h"
#include "nsCoord.h"
#include "nsMargin.h"
#include "nsRect.h"
#include "nsFont.h"
#include "nsStyleCoord.h"
#include "nsStyleConsts.h"
#include "nsChangeHint.h"
#include "nsPresContext.h"
#include "nsCOMPtr.h"
#include "nsCOMArray.h"
#include "nsTArray.h"
#include "nsIAtom.h"
#include "nsIURI.h"
#include "nsCSSValue.h"
#include "nsStyleTransformMatrix.h"
#include "nsAlgorithm.h"
#include "imgIRequest.h"
#include "gfxRect.h"

class nsIFrame;
class imgIRequest;
class imgIContainer;
struct nsCSSValueList;

// Includes nsStyleStructID.
#include "nsStyleStructFwd.h"

// Bits for each struct.
// NS_STYLE_INHERIT_BIT defined in nsStyleStructFwd.h
#define NS_STYLE_INHERIT_MASK             0x00ffffff

// Additional bits for nsStyleContext's mBits:
// See nsStyleContext::HasTextDecorations
#define NS_STYLE_HAS_TEXT_DECORATIONS     0x01000000
// See nsStyleContext::HasPseudoElementData.
#define NS_STYLE_HAS_PSEUDO_ELEMENT_DATA  0x02000000
// See nsStyleContext::RelevantLinkIsVisited
#define NS_STYLE_RELEVANT_LINK_VISITED    0x04000000
// See nsStyleContext::IsStyleIfVisited
#define NS_STYLE_IS_STYLE_IF_VISITED      0x08000000
// See nsStyleContext::GetPseudoEnum
#define NS_STYLE_CONTEXT_TYPE_MASK        0xf0000000
#define NS_STYLE_CONTEXT_TYPE_SHIFT       28

// Additional bits for nsRuleNode's mDependentBits:
#define NS_RULE_NODE_GC_MARK              0x02000000
#define NS_RULE_NODE_IS_IMPORTANT         0x08000000
#define NS_RULE_NODE_LEVEL_MASK           0xf0000000
#define NS_RULE_NODE_LEVEL_SHIFT          28

// The lifetime of these objects is managed by the presshell's arena.

// Each struct must implement a static ForceCompare() function returning a
// boolean.  Structs that can return a hint that doesn't guarantee that the
// change will be applied to all descendants must return true from
// ForceCompare(), so that we will make sure to compare those structs in
// nsStyleContext::CalcStyleDifference.

struct nsStyleFont {
  nsStyleFont(const nsFont& aFont, nsPresContext *aPresContext);
  nsStyleFont(const nsStyleFont& aStyleFont);
  nsStyleFont(nsPresContext *aPresContext);
  ~nsStyleFont(void) {
    MOZ_COUNT_DTOR(nsStyleFont);
  }

  nsChangeHint CalcDifference(const nsStyleFont& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }
  static nsChangeHint CalcFontDifference(const nsFont& aFont1, const nsFont& aFont2);

  static nscoord ZoomText(nsPresContext* aPresContext, nscoord aSize);
  static nscoord UnZoomText(nsPresContext* aPresContext, nscoord aSize);

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW;
  void Destroy(nsPresContext* aContext);

  nsFont  mFont;        // [inherited]
  nscoord mSize;        // [inherited] Our "computed size". Can be different
                        // from mFont.size which is our "actual size" and is
                        // enforced to be >= the user's preferred min-size.
                        // mFont.size should be used for display purposes
                        // while mSize is the value to return in
                        // getComputedStyle() for example.
  PRUint8 mGenericID;   // [inherited] generic CSS font family, if any;
                        // value is a kGenericFont_* constant, see nsFont.h.

#ifdef MOZ_MATHML
  // MathML scriptlevel support
  PRInt8  mScriptLevel;          // [inherited]
  // The value mSize would have had if scriptminsize had never been applied
  nscoord mScriptUnconstrainedSize;
  nscoord mScriptMinSize;        // [inherited] length
  float   mScriptSizeMultiplier; // [inherited]
#endif
};

struct nsStyleGradientStop {
  nsStyleCoord mLocation; // percent, coord, none
  nscolor mColor;
};

class nsStyleGradient {
public:
  nsStyleGradient();
  PRUint8 mShape;  // NS_STYLE_GRADIENT_SHAPE_*
  PRUint8 mSize;   // NS_STYLE_GRADIENT_SIZE_*;
                   // not used (must be FARTHEST_CORNER) for linear shape
  PRPackedBool mRepeating;

  nsStyleCoord mBgPosX; // percent, coord, none
  nsStyleCoord mBgPosY; // percent, coord, none
  nsStyleCoord mAngle;  // none, angle

  // stops are in the order specified in the stylesheet
  nsTArray<nsStyleGradientStop> mStops;

  PRBool operator==(const nsStyleGradient& aOther) const;
  PRBool operator!=(const nsStyleGradient& aOther) const {
    return !(*this == aOther);
  };

  NS_INLINE_DECL_REFCOUNTING(nsStyleGradient)

private:
  ~nsStyleGradient() {}

  // Not to be implemented
  nsStyleGradient(const nsStyleGradient& aOther);
  nsStyleGradient& operator=(const nsStyleGradient& aOther);
};

enum nsStyleImageType {
  eStyleImageType_Null,
  eStyleImageType_Image,
  eStyleImageType_Gradient,
  eStyleImageType_Element
};

/**
 * Represents a paintable image of one of the following types.
 * (1) A real image loaded from an external source.
 * (2) A CSS linear or radial gradient.
 * (3) An element within a document, or an <img>, <video>, or <canvas> element
 *     not in a document.
 * (*) Optionally a crop rect can be set to paint a partial (rectangular)
 * region of an image. (Currently, this feature is only supported with an
 * image of type (1)).
 *
 * This struct is currently used only for 'background-image', but it may be
 * used by other CSS properties such as 'border-image', 'list-style-image', and
 * 'content' in the future (bug 507052).
 */
struct nsStyleImage {
  nsStyleImage();
  ~nsStyleImage();
  nsStyleImage(const nsStyleImage& aOther);
  nsStyleImage& operator=(const nsStyleImage& aOther);

  void SetNull();
  void SetImageData(imgIRequest* aImage);
  void TrackImage(nsPresContext* aContext);
  void UntrackImage(nsPresContext* aContext);
  void SetGradientData(nsStyleGradient* aGradient);
  void SetElementId(const PRUnichar* aElementId);
  void SetCropRect(nsStyleSides* aCropRect);

  nsStyleImageType GetType() const {
    return mType;
  }
  imgIRequest* GetImageData() const {
    NS_ABORT_IF_FALSE(mType == eStyleImageType_Image, "Data is not an image!");
    NS_ABORT_IF_FALSE(mImageTracked,
                      "Should be tracking any image we're going to use!");
    return mImage;
  }
  nsStyleGradient* GetGradientData() const {
    NS_ASSERTION(mType == eStyleImageType_Gradient, "Data is not a gradient!");
    return mGradient;
  }
  const PRUnichar* GetElementId() const {
    NS_ASSERTION(mType == eStyleImageType_Element, "Data is not an element!");
    return mElementId;
  }
  nsStyleSides* GetCropRect() const {
    NS_ASSERTION(mType == eStyleImageType_Image,
                 "Only image data can have a crop rect");
    return mCropRect;
  }

  /**
   * Compute the actual crop rect in pixels, using the source image bounds.
   * The computation involves converting percentage unit to pixel unit and
   * clamping each side value to fit in the source image bounds.
   * @param aActualCropRect the computed actual crop rect.
   * @param aIsEntireImage PR_TRUE iff |aActualCropRect| is identical to the
   * source image bounds.
   * @return PR_TRUE iff |aActualCropRect| holds a meaningful value.
   */
  PRBool ComputeActualCropRect(nsIntRect& aActualCropRect,
                               PRBool* aIsEntireImage = nsnull) const;

  /**
   * Requests a decode on the image.
   */
  nsresult RequestDecode() const;
  /**
   * @return PR_TRUE if the item is definitely opaque --- i.e., paints every
   * pixel within its bounds opaquely, and the bounds contains at least a pixel.
   */
  PRBool IsOpaque() const;
  /**
   * @return PR_TRUE if this image is fully loaded, and its size is calculated;
   * always returns PR_TRUE if |mType| is |eStyleImageType_Gradient| or
   * |eStyleImageType_Element|.
   */
  PRBool IsComplete() const;
  /**
   * @return PR_TRUE if it is 100% confident that this image contains no pixel
   * to draw.
   */
  PRBool IsEmpty() const {
    // There are some other cases when the image will be empty, for example
    // when the crop rect is empty. However, checking the emptiness of crop
    // rect is non-trivial since each side value can be specified with
    // percentage unit, which can not be evaluated until the source image size
    // is available. Therefore, we currently postpone the evaluation of crop
    // rect until the actual rendering time --- alternatively until IsOpaque()
    // is called.
    return mType == eStyleImageType_Null;
  }

  PRBool operator==(const nsStyleImage& aOther) const;
  PRBool operator!=(const nsStyleImage& aOther) const {
    return !(*this == aOther);
  }

private:
  void DoCopy(const nsStyleImage& aOther);

  nsStyleImageType mType;
  union {
    imgIRequest* mImage;
    nsStyleGradient* mGradient;
    PRUnichar* mElementId;
  };
  // This is _currently_ used only in conjunction with eStyleImageType_Image.
  nsAutoPtr<nsStyleSides> mCropRect;
#ifdef DEBUG
  bool mImageTracked;
#endif
};

struct nsStyleColor {
  nsStyleColor(nsPresContext* aPresContext);
  nsStyleColor(const nsStyleColor& aOther);
  ~nsStyleColor(void) {
    MOZ_COUNT_DTOR(nsStyleColor);
  }

  nsChangeHint CalcDifference(const nsStyleColor& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleColor();
    aContext->FreeToShell(sizeof(nsStyleColor), this);
  }

  // Don't add ANY members to this struct!  We can achieve caching in the rule
  // tree (rather than the style tree) by letting color stay by itself! -dwh
  nscolor mColor;                 // [inherited]
};

struct nsStyleBackground {
  nsStyleBackground();
  nsStyleBackground(const nsStyleBackground& aOther);
  ~nsStyleBackground();

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext);

  nsChangeHint CalcDifference(const nsStyleBackground& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  struct Position;
  friend struct Position;
  struct Position {
    typedef union {
      nscoord mCoord; // for lengths
      float   mFloat; // for percents
    } PositionCoord;
    PositionCoord mXPosition, mYPosition;
    PRPackedBool mXIsPercent, mYIsPercent;

    // Initialize nothing
    Position() {}

    // Initialize to initial values
    void SetInitialValues();

    // True if the effective background image position described by this depends
    // on the size of the corresponding frame.
    PRBool DependsOnFrameSize() const {
      return (mXIsPercent && mXPosition.mFloat != 0.0f) ||
             (mYIsPercent && mYPosition.mFloat != 0.0f);
    }

    PRBool operator==(const Position& aOther) const {
      return mXIsPercent == aOther.mXIsPercent &&
             (mXIsPercent ? (mXPosition.mFloat == aOther.mXPosition.mFloat)
                          : (mXPosition.mCoord == aOther.mXPosition.mCoord)) &&
             mYIsPercent == aOther.mYIsPercent &&
             (mYIsPercent ? (mYPosition.mFloat == aOther.mYPosition.mFloat)
                          : (mYPosition.mCoord == aOther.mYPosition.mCoord));
    }
    PRBool operator!=(const Position& aOther) const {
      return !(*this == aOther);
    }
  };

  struct Size;
  friend struct Size;
  struct Size {
    typedef union {
      nscoord mCoord; // for lengths
      float mFloat; // for percents
    } Dimension;
    Dimension mWidth, mHeight;

    // Dimension types which might change how a layer is painted when the
    // corresponding frame's dimensions change *must* precede all dimension
    // types which are agnostic to frame size; see DependsOnFrameSize below.
    enum DimensionType {
      // If one of mWidth and mHeight is eContain or eCover, then both are.
      // Also, these two values must equal the corresponding values in
      // kBackgroundSizeKTable.
      eContain, eCover,

      ePercentage,

      eAuto,
      eLength,
      eDimensionType_COUNT
    };
    PRUint8 mWidthType, mHeightType;

    // True if the effective image size described by this depends on
    // the size of the corresponding frame.  Gradients depend on the
    // frame size when their dimensions are 'auto', images don't; both
    // types depend on the frame size when their dimensions are
    // 'contain', 'cover', or a percentage.
    // -moz-element also depends on the frame size when the dimensions
    // are 'auto' since it could be an SVG gradient or pattern which
    // behaves exactly like a CSS gradient.
    PRBool DependsOnFrameSize(nsStyleImageType aType) const {
      if (aType == eStyleImageType_Image) {
        return mWidthType <= ePercentage || mHeightType <= ePercentage;
      } else {
        NS_ABORT_IF_FALSE(aType == eStyleImageType_Gradient ||
                          aType == eStyleImageType_Element,
                          "unrecognized image type");
        return mWidthType <= eAuto || mHeightType <= eAuto;
      }
    }

    // Initialize nothing
    Size() {}

    // Initialize to initial values
    void SetInitialValues();

    PRBool operator==(const Size& aOther) const;
    PRBool operator!=(const Size& aOther) const {
      return !(*this == aOther);
    }
  };

  struct Layer;
  friend struct Layer;
  struct Layer {
    PRUint8 mAttachment;                // [reset] See nsStyleConsts.h
    PRUint8 mClip;                      // [reset] See nsStyleConsts.h
    PRUint8 mOrigin;                    // [reset] See nsStyleConsts.h
    PRUint8 mRepeat;                    // [reset] See nsStyleConsts.h
    Position mPosition;                 // [reset]
    nsStyleImage mImage;                // [reset]
    Size mSize;                         // [reset]

    // Initializes only mImage
    Layer();
    ~Layer();

    // Register/unregister images with the document. We do this only
    // after the dust has settled in ComputeBackgroundData.
    void TrackImages(nsPresContext* aContext) {
      if (mImage.GetType() == eStyleImageType_Image)
        mImage.TrackImage(aContext);
    }
    void UntrackImages(nsPresContext* aContext) {
      if (mImage.GetType() == eStyleImageType_Image)
        mImage.UntrackImage(aContext);
    }

    void SetInitialValues();

    // True if the rendering of this layer might change when the size
    // of the corresponding frame changes.  This is true for any
    // non-solid-color background whose position or size depends on
    // the frame size.
    PRBool RenderingMightDependOnFrameSize() const {
      return (!mImage.IsEmpty() &&
              (mPosition.DependsOnFrameSize() ||
               mSize.DependsOnFrameSize(mImage.GetType())));
    }

    // An equality operator that compares the images using URL-equality
    // rather than pointer-equality.
    PRBool operator==(const Layer& aOther) const;
    PRBool operator!=(const Layer& aOther) const {
      return !(*this == aOther);
    }
  };

  // The (positive) number of computed values of each property, since
  // the lengths of the lists are independent.
  PRUint32 mAttachmentCount,
           mClipCount,
           mOriginCount,
           mRepeatCount,
           mPositionCount,
           mImageCount,
           mSizeCount;
  // Layers are stored in an array, matching the top-to-bottom order in
  // which they are specified in CSS.  The number of layers to be used
  // should come from the background-image property.  We create
  // additional |Layer| objects for *any* property, not just
  // background-image.  This means that the bottommost layer that
  // callers in layout care about (which is also the one whose
  // background-clip applies to the background-color) may not be last
  // layer.  In layers below the bottom layer, properties will be
  // uninitialized unless their count, above, indicates that they are
  // present.
  nsAutoTArray<Layer, 1> mLayers;

  const Layer& BottomLayer() const { return mLayers[mImageCount - 1]; }

  #define NS_FOR_VISIBLE_BACKGROUND_LAYERS_BACK_TO_FRONT(var_, stylebg_) \
    for (PRUint32 var_ = (stylebg_)->mImageCount; var_-- != 0; )

  nscolor mBackgroundColor;       // [reset]

  // FIXME: This (now background-break in css3-background) should
  // probably move into a different struct so that everything in
  // nsStyleBackground is set by the background shorthand.
  PRUint8 mBackgroundInlinePolicy; // [reset] See nsStyleConsts.h

  // True if this background is completely transparent.
  PRBool IsTransparent() const;

  // We have to take slower codepaths for fixed background attachment,
  // but we don't want to do that when there's no image.
  // Not inline because it uses an nsCOMPtr<imgIRequest>
  // FIXME: Should be in nsStyleStructInlines.h.
  PRBool HasFixedBackground() const;
};

// See https://bugzilla.mozilla.org/show_bug.cgi?id=271586#c43 for why
// this is hard to replace with 'currentColor'.
#define BORDER_COLOR_FOREGROUND   0x20
#define OUTLINE_COLOR_INITIAL     0x80
// FOREGROUND | INITIAL(OUTLINE)
#define BORDER_COLOR_SPECIAL      0xA0
#define BORDER_STYLE_MASK         0x1F

#define NS_SPACING_MARGIN   0
#define NS_SPACING_PADDING  1
#define NS_SPACING_BORDER   2


struct nsStyleMargin {
  nsStyleMargin(void);
  nsStyleMargin(const nsStyleMargin& aMargin);
  ~nsStyleMargin(void) {
    MOZ_COUNT_DTOR(nsStyleMargin);
  }

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW;
  void Destroy(nsPresContext* aContext);

  void RecalcData();
  nsChangeHint CalcDifference(const nsStyleMargin& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_TRUE; }

  nsStyleSides  mMargin;          // [reset] coord, percent, auto

  PRBool GetMargin(nsMargin& aMargin) const
  {
    if (mHasCachedMargin) {
      aMargin = mCachedMargin;
      return PR_TRUE;
    }
    return PR_FALSE;
  }

protected:
  PRPackedBool  mHasCachedMargin;
  nsMargin      mCachedMargin;
};


struct nsStylePadding {
  nsStylePadding(void);
  nsStylePadding(const nsStylePadding& aPadding);
  ~nsStylePadding(void) {
    MOZ_COUNT_DTOR(nsStylePadding);
  }

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW;
  void Destroy(nsPresContext* aContext);

  void RecalcData();
  nsChangeHint CalcDifference(const nsStylePadding& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_TRUE; }

  nsStyleSides  mPadding;         // [reset] coord, percent

  PRBool GetPadding(nsMargin& aPadding) const
  {
    if (mHasCachedPadding) {
      aPadding = mCachedPadding;
      return PR_TRUE;
    }
    return PR_FALSE;
  }

protected:
  PRPackedBool  mHasCachedPadding;
  nsMargin      mCachedPadding;
};

struct nsBorderColors {
  nsBorderColors* mNext;
  nscolor mColor;

  nsBorderColors() : mNext(nsnull), mColor(NS_RGB(0,0,0)) {}
  nsBorderColors(const nscolor& aColor) : mNext(nsnull), mColor(aColor) {}
  ~nsBorderColors();

  nsBorderColors* Clone() const { return Clone(PR_TRUE); }

  static PRBool Equal(const nsBorderColors* c1,
                      const nsBorderColors* c2) {
    if (c1 == c2)
      return PR_TRUE;
    while (c1 && c2) {
      if (c1->mColor != c2->mColor)
        return PR_FALSE;
      c1 = c1->mNext;
      c2 = c2->mNext;
    }
    // both should be NULL if these are equal, otherwise one
    // has more colors than another
    return !c1 && !c2;
  }

private:
  nsBorderColors* Clone(PRBool aDeep) const;
};

struct nsCSSShadowItem {
  nscoord mXOffset;
  nscoord mYOffset;
  nscoord mRadius;
  nscoord mSpread;

  nscolor      mColor;
  PRPackedBool mHasColor; // Whether mColor should be used
  PRPackedBool mInset;

  nsCSSShadowItem() : mHasColor(PR_FALSE) {
    MOZ_COUNT_CTOR(nsCSSShadowItem);
  }
  ~nsCSSShadowItem() {
    MOZ_COUNT_DTOR(nsCSSShadowItem);
  }

  PRBool operator==(const nsCSSShadowItem& aOther) {
    return (mXOffset == aOther.mXOffset &&
            mYOffset == aOther.mYOffset &&
            mRadius == aOther.mRadius &&
            mHasColor == aOther.mHasColor &&
            mSpread == aOther.mSpread &&
            mInset == aOther.mInset &&
            (!mHasColor || mColor == aOther.mColor));
  }
  PRBool operator!=(const nsCSSShadowItem& aOther) {
    return !(*this == aOther);
  }
};

class nsCSSShadowArray {
  public:
    void* operator new(size_t aBaseSize, PRUint32 aArrayLen) {
      // We can allocate both this nsCSSShadowArray and the
      // actual array in one allocation. The amount of memory to
      // allocate is equal to the class's size + the number of bytes for all
      // but the first array item (because aBaseSize includes one
      // item, see the private declarations)
      return ::operator new(aBaseSize +
                            (aArrayLen - 1) * sizeof(nsCSSShadowItem));
    }

    nsCSSShadowArray(PRUint32 aArrayLen) :
      mLength(aArrayLen)
    {
      MOZ_COUNT_CTOR(nsCSSShadowArray);
      for (PRUint32 i = 1; i < mLength; ++i) {
        // Make sure we call the constructors of each nsCSSShadowItem
        // (the first one is called for us because we declared it under private)
        new (&mArray[i]) nsCSSShadowItem();
      }
    }
    ~nsCSSShadowArray() {
      MOZ_COUNT_DTOR(nsCSSShadowArray);
      for (PRUint32 i = 1; i < mLength; ++i) {
        mArray[i].~nsCSSShadowItem();
      }
    }

    PRUint32 Length() const { return mLength; }
    nsCSSShadowItem* ShadowAt(PRUint32 i) {
      NS_ABORT_IF_FALSE(i < mLength, "Accessing too high an index in the text shadow array!");
      return &mArray[i];
    }
    const nsCSSShadowItem* ShadowAt(PRUint32 i) const {
      NS_ABORT_IF_FALSE(i < mLength, "Accessing too high an index in the text shadow array!");
      return &mArray[i];
    }

    NS_INLINE_DECL_REFCOUNTING(nsCSSShadowArray)

  private:
    PRUint32 mLength;
    nsCSSShadowItem mArray[1]; // This MUST be the last item
};

// Border widths are rounded to the nearest-below integer number of pixels,
// but values between zero and one device pixels are always rounded up to
// one device pixel.
#define NS_ROUND_BORDER_TO_PIXELS(l,tpp) \
  ((l) == 0) ? 0 : NS_MAX((tpp), (l) / (tpp) * (tpp))
// Outline offset is rounded to the nearest integer number of pixels, but values
// between zero and one device pixels are always rounded up to one device pixel.
// Note that the offset can be negative.
#define NS_ROUND_OFFSET_TO_PIXELS(l,tpp) \
  (((l) == 0) ? 0 : \
    ((l) > 0) ? NS_MAX( (tpp), ((l) + ((tpp) / 2)) / (tpp) * (tpp)) : \
                NS_MIN(-(tpp), ((l) - ((tpp) / 2)) / (tpp) * (tpp)))

// Returns if the given border style type is visible or not
static PRBool IsVisibleBorderStyle(PRUint8 aStyle)
{
  return (aStyle != NS_STYLE_BORDER_STYLE_NONE &&
          aStyle != NS_STYLE_BORDER_STYLE_HIDDEN);
}

struct nsStyleBorder {
  nsStyleBorder(nsPresContext* aContext);
  nsStyleBorder(const nsStyleBorder& aBorder);
  ~nsStyleBorder();

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW;
  void Destroy(nsPresContext* aContext);

  nsChangeHint CalcDifference(const nsStyleBorder& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }
  PRBool ImageBorderDiffers() const;

  nsStyleCorners mBorderRadius;    // [reset] coord, percent
  nsStyleSides  mBorderImageSplit; // [reset] integer, percent
  PRUint8       mFloatEdge;       // [reset] see nsStyleConsts.h
  PRUint8       mBorderImageHFill; // [reset]
  PRUint8       mBorderImageVFill; // [reset]
  nsBorderColors** mBorderColors; // [reset] multiple levels of color for a border.
  nsRefPtr<nsCSSShadowArray> mBoxShadow; // [reset] NULL for 'none'
  PRBool        mHaveBorderImageWidth; // [reset]
  nsMargin      mBorderImageWidth; // [reset]

  void EnsureBorderColors() {
    if (!mBorderColors) {
      mBorderColors = new nsBorderColors*[4];
      if (mBorderColors)
        for (PRInt32 i = 0; i < 4; i++)
          mBorderColors[i] = nsnull;
    }
  }

  void ClearBorderColors(mozilla::css::Side aSide) {
    if (mBorderColors && mBorderColors[aSide]) {
      delete mBorderColors[aSide];
      mBorderColors[aSide] = nsnull;
    }
  }

  // Return whether aStyle is a visible style.  Invisible styles cause
  // the relevant computed border width to be 0.
  // Note that this does *not* consider the effects of 'border-image':
  // if border-style is none, but there is a loaded border image,
  // HasVisibleStyle will be false even though there *is* a border.
  PRBool HasVisibleStyle(mozilla::css::Side aSide)
  {
    return IsVisibleBorderStyle(GetBorderStyle(aSide));
  }

  // aBorderWidth is in twips
  void SetBorderWidth(mozilla::css::Side aSide, nscoord aBorderWidth)
  {
    nscoord roundedWidth =
      NS_ROUND_BORDER_TO_PIXELS(aBorderWidth, mTwipsPerPixel);
    mBorder.side(aSide) = roundedWidth;
    if (HasVisibleStyle(aSide))
      mComputedBorder.side(aSide) = roundedWidth;
  }

  void SetBorderImageWidthOverride(mozilla::css::Side aSide, nscoord aBorderWidth)
  {
    mBorderImageWidth.side(aSide) =
      NS_ROUND_BORDER_TO_PIXELS(aBorderWidth, mTwipsPerPixel);
  }

  // Get the actual border, in twips.  (If there is no border-image
  // loaded, this is the same as GetComputedBorder.  If there is a
  // border-image loaded, it uses the border-image width overrides if
  // present, and otherwise mBorder, which is GetComputedBorder without
  // considering border-style: none.)
  const nsMargin& GetActualBorder() const;

  // Get the computed border (plus rounding).  This does consider the
  // effects of 'border-style: none', but does not consider
  // 'border-image'.
  const nsMargin& GetComputedBorder() const
  {
    return mComputedBorder;
  }

  // Get the actual border width for a particular side, in appunits.  Note that
  // this is zero if and only if there is no border to be painted for this
  // side.  That is, this value takes into account the border style and the
  // value is rounded to the nearest device pixel by NS_ROUND_BORDER_TO_PIXELS.
  nscoord GetActualBorderWidth(mozilla::css::Side aSide) const
  {
    return GetActualBorder().side(aSide);
  }

  PRUint8 GetBorderStyle(mozilla::css::Side aSide) const
  {
    NS_ASSERTION(aSide <= NS_SIDE_LEFT, "bad side");
    return (mBorderStyle[aSide] & BORDER_STYLE_MASK);
  }

  void SetBorderStyle(mozilla::css::Side aSide, PRUint8 aStyle)
  {
    NS_ASSERTION(aSide <= NS_SIDE_LEFT, "bad side");
    mBorderStyle[aSide] &= ~BORDER_STYLE_MASK;
    mBorderStyle[aSide] |= (aStyle & BORDER_STYLE_MASK);
    mComputedBorder.side(aSide) =
      (HasVisibleStyle(aSide) ? mBorder.side(aSide) : 0);
  }

  // Defined in nsStyleStructInlines.h
  inline PRBool IsBorderImageLoaded() const;
  inline nsresult RequestDecode();

  void GetBorderColor(mozilla::css::Side aSide, nscolor& aColor,
                      PRBool& aForeground) const
  {
    aForeground = PR_FALSE;
    NS_ASSERTION(aSide <= NS_SIDE_LEFT, "bad side");
    if ((mBorderStyle[aSide] & BORDER_COLOR_SPECIAL) == 0)
      aColor = mBorderColor[aSide];
    else if (mBorderStyle[aSide] & BORDER_COLOR_FOREGROUND)
      aForeground = PR_TRUE;
    else
      NS_NOTREACHED("OUTLINE_COLOR_INITIAL should not be set here");
  }

  void SetBorderColor(mozilla::css::Side aSide, nscolor aColor)
  {
    NS_ASSERTION(aSide <= NS_SIDE_LEFT, "bad side");
    mBorderColor[aSide] = aColor;
    mBorderStyle[aSide] &= ~BORDER_COLOR_SPECIAL;
  }

  // These are defined in nsStyleStructInlines.h
  inline void SetBorderImage(imgIRequest* aImage);
  inline imgIRequest* GetBorderImage() const;

  // These methods are used for the caller to caches the sub images created during
  // a border-image paint operation
  inline void SetSubImage(PRUint8 aIndex, imgIContainer* aSubImage) const;
  inline imgIContainer* GetSubImage(PRUint8 aIndex) const;

  void GetCompositeColors(PRInt32 aIndex, nsBorderColors** aColors) const
  {
    if (!mBorderColors)
      *aColors = nsnull;
    else
      *aColors = mBorderColors[aIndex];
  }

  void AppendBorderColor(PRInt32 aIndex, nscolor aColor)
  {
    NS_ASSERTION(aIndex >= 0 && aIndex <= 3, "bad side for composite border color");
    nsBorderColors* colorEntry = new nsBorderColors(aColor);
    if (!mBorderColors[aIndex])
      mBorderColors[aIndex] = colorEntry;
    else {
      nsBorderColors* last = mBorderColors[aIndex];
      while (last->mNext)
        last = last->mNext;
      last->mNext = colorEntry;
    }
    mBorderStyle[aIndex] &= ~BORDER_COLOR_SPECIAL;
  }

  void SetBorderToForeground(mozilla::css::Side aSide)
  {
    NS_ASSERTION(aSide <= NS_SIDE_LEFT, "bad side");
    mBorderStyle[aSide] &= ~BORDER_COLOR_SPECIAL;
    mBorderStyle[aSide] |= BORDER_COLOR_FOREGROUND;
  }

protected:
  // mComputedBorder holds the CSS2.1 computed border-width values.  In
  // particular, these widths take into account the border-style for the
  // relevant side and the values are rounded to the nearest device
  // pixel.  They are also rounded (which is not part of the definition
  // of computed values).  However, they do *not* take into account the
  // presence of border-image.  See GetActualBorder above for how to
  // really get the actual border.
  nsMargin      mComputedBorder;

  // mBorder holds the nscoord values for the border widths as they would be if
  // all the border-style values were visible (not hidden or none).  This
  // member exists so that when we create structs using the copy
  // constructor during style resolution the new structs will know what the
  // specified values of the border were in case they have more specific rules
  // setting the border style.  Note that this isn't quite the CSS specified
  // value, since this has had the enumerated border widths converted to
  // lengths, and all lengths converted to twips.  But it's not quite the
  // computed value either. The values are rounded to the nearest device pixel
  // We also use these values when we have a loaded border-image that
  // does not have width overrides.
  nsMargin      mBorder;

  PRUint8       mBorderStyle[4];  // [reset] See nsStyleConsts.h
  nscolor       mBorderColor[4];  // [reset] the colors to use for a simple border.  not used
                                  // if -moz-border-colors is specified
private:
  nsCOMPtr<imgIRequest> mBorderImage; // [reset]

  // Cache used by callers for border-image painting
  nsCOMArray<imgIContainer> mSubImages;

  nscoord       mTwipsPerPixel;
};


struct nsStyleOutline {
  nsStyleOutline(nsPresContext* aPresContext);
  nsStyleOutline(const nsStyleOutline& aOutline);
  ~nsStyleOutline(void) {
    MOZ_COUNT_DTOR(nsStyleOutline);
  }

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleOutline();
    aContext->FreeToShell(sizeof(nsStyleOutline), this);
  }

  void RecalcData(nsPresContext* aContext);
  nsChangeHint CalcDifference(const nsStyleOutline& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  nsStyleCorners  mOutlineRadius; // [reset] coord, percent

  // Note that this is a specified value.  You can get the actual values
  // with GetOutlineWidth.  You cannot get the computed value directly.
  nsStyleCoord  mOutlineWidth;    // [reset] coord, enum (see nsStyleConsts.h)
  nscoord       mOutlineOffset;   // [reset]

  PRBool GetOutlineWidth(nscoord& aWidth) const
  {
    if (mHasCachedOutline) {
      aWidth = mCachedOutlineWidth;
      return PR_TRUE;
    }
    return PR_FALSE;
  }

  PRUint8 GetOutlineStyle(void) const
  {
    return (mOutlineStyle & BORDER_STYLE_MASK);
  }

  void SetOutlineStyle(PRUint8 aStyle)
  {
    mOutlineStyle &= ~BORDER_STYLE_MASK;
    mOutlineStyle |= (aStyle & BORDER_STYLE_MASK);
  }

  // PR_FALSE means initial value
  PRBool GetOutlineColor(nscolor& aColor) const
  {
    if ((mOutlineStyle & BORDER_COLOR_SPECIAL) == 0) {
      aColor = mOutlineColor;
      return PR_TRUE;
    }
    return PR_FALSE;
  }

  void SetOutlineColor(nscolor aColor)
  {
    mOutlineColor = aColor;
    mOutlineStyle &= ~BORDER_COLOR_SPECIAL;
  }

  void SetOutlineInitialColor()
  {
    mOutlineStyle |= OUTLINE_COLOR_INITIAL;
  }

  PRBool GetOutlineInitialColor() const
  {
    return !!(mOutlineStyle & OUTLINE_COLOR_INITIAL);
  }

protected:
  // This value is the actual value, so it's rounded to the nearest device
  // pixel.
  nscoord       mCachedOutlineWidth;

  nscolor       mOutlineColor;    // [reset]

  PRPackedBool  mHasCachedOutline;
  PRUint8       mOutlineStyle;    // [reset] See nsStyleConsts.h

  nscoord       mTwipsPerPixel;
};


struct nsStyleList {
  nsStyleList(void);
  nsStyleList(const nsStyleList& aStyleList);
  ~nsStyleList(void);

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleList();
    aContext->FreeToShell(sizeof(nsStyleList), this);
  }

  nsChangeHint CalcDifference(const nsStyleList& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  imgIRequest* GetListStyleImage() const { return mListStyleImage; }
  void SetListStyleImage(imgIRequest* aReq)
  {
    if (mListStyleImage)
      mListStyleImage->UnlockImage();
    mListStyleImage = aReq;
    if (mListStyleImage)
      mListStyleImage->LockImage();
  }

  PRUint8   mListStyleType;             // [inherited] See nsStyleConsts.h
  PRUint8   mListStylePosition;         // [inherited]
private:
  nsCOMPtr<imgIRequest> mListStyleImage; // [inherited]
  nsStyleList& operator=(const nsStyleList& aOther); // Not to be implemented
public:
  nsRect        mImageRegion;           // [inherited] the rect to use within an image
};

struct nsStylePosition {
  nsStylePosition(void);
  nsStylePosition(const nsStylePosition& aOther);
  ~nsStylePosition(void);

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStylePosition();
    aContext->FreeToShell(sizeof(nsStylePosition), this);
  }

  nsChangeHint CalcDifference(const nsStylePosition& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_TRUE; }

  nsStyleSides  mOffset;                // [reset] coord, percent, calc, auto
  nsStyleCoord  mWidth;                 // [reset] coord, percent, enum, calc, auto
  nsStyleCoord  mMinWidth;              // [reset] coord, percent, enum, calc
  nsStyleCoord  mMaxWidth;              // [reset] coord, percent, enum, calc, none
  nsStyleCoord  mHeight;                // [reset] coord, percent, calc, auto
  nsStyleCoord  mMinHeight;             // [reset] coord, percent, calc
  nsStyleCoord  mMaxHeight;             // [reset] coord, percent, calc, none
  PRUint8       mBoxSizing;             // [reset] see nsStyleConsts.h
  nsStyleCoord  mZIndex;                // [reset] integer, auto

  PRBool WidthDependsOnContainer() const
    { return WidthCoordDependsOnContainer(mWidth); }
  PRBool MinWidthDependsOnContainer() const
    { return WidthCoordDependsOnContainer(mMinWidth); }
  PRBool MaxWidthDependsOnContainer() const
    { return WidthCoordDependsOnContainer(mMaxWidth); }

  // Note that these functions count 'auto' as depending on the
  // container since that's the case for absolutely positioned elements.
  // However, some callers do not care about this case and should check
  // for it, since it is the most common case.
  // FIXME: We should probably change the assumption to be the other way
  // around.
  PRBool HeightDependsOnContainer() const
    { return HeightCoordDependsOnContainer(mHeight); }
  PRBool MinHeightDependsOnContainer() const
    { return HeightCoordDependsOnContainer(mMinHeight); }
  PRBool MaxHeightDependsOnContainer() const
    { return HeightCoordDependsOnContainer(mMaxHeight); }

  PRBool OffsetHasPercent(mozilla::css::Side aSide) const
  {
    return mOffset.Get(aSide).HasPercent();
  }

private:
  static PRBool WidthCoordDependsOnContainer(const nsStyleCoord &aCoord);
  static PRBool HeightCoordDependsOnContainer(const nsStyleCoord &aCoord)
  {
    return aCoord.GetUnit() == eStyleUnit_Auto || // CSS 2.1, 10.6.4, item (5)
           aCoord.HasPercent();
  }
};

struct nsStyleTextReset {
  nsStyleTextReset(void);
  nsStyleTextReset(const nsStyleTextReset& aOther);
  ~nsStyleTextReset(void);

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleTextReset();
    aContext->FreeToShell(sizeof(nsStyleTextReset), this);
  }

  nsChangeHint CalcDifference(const nsStyleTextReset& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  PRUint8 mTextDecoration;              // [reset] see nsStyleConsts.h
  PRUint8 mUnicodeBidi;                 // [reset] see nsStyleConsts.h

  nsStyleCoord  mVerticalAlign;         // [reset] coord, percent, enum (see nsStyleConsts.h)
};

struct nsStyleText {
  nsStyleText(void);
  nsStyleText(const nsStyleText& aOther);
  ~nsStyleText(void);

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleText();
    aContext->FreeToShell(sizeof(nsStyleText), this);
  }

  nsChangeHint CalcDifference(const nsStyleText& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  PRUint8 mTextAlign;                   // [inherited] see nsStyleConsts.h
  PRUint8 mTextTransform;               // [inherited] see nsStyleConsts.h
  PRUint8 mWhiteSpace;                  // [inherited] see nsStyleConsts.h
  PRUint8 mWordWrap;                    // [inherited] see nsStyleConsts.h
  PRInt32 mTabSize;                     // [inherited] see nsStyleConsts.h

  nsStyleCoord  mLetterSpacing;         // [inherited] coord, normal
  nsStyleCoord  mLineHeight;            // [inherited] coord, factor, normal
  nsStyleCoord  mTextIndent;            // [inherited] coord, percent
  nscoord mWordSpacing;                 // [inherited]

  nsRefPtr<nsCSSShadowArray> mTextShadow; // [inherited] NULL in case of a zero-length

  PRBool WhiteSpaceIsSignificant() const {
    return mWhiteSpace == NS_STYLE_WHITESPACE_PRE ||
           mWhiteSpace == NS_STYLE_WHITESPACE_PRE_WRAP;
  }

  PRBool NewlineIsSignificant() const {
    return mWhiteSpace == NS_STYLE_WHITESPACE_PRE ||
           mWhiteSpace == NS_STYLE_WHITESPACE_PRE_WRAP ||
           mWhiteSpace == NS_STYLE_WHITESPACE_PRE_LINE;
  }

  PRBool WhiteSpaceCanWrap() const {
    return mWhiteSpace == NS_STYLE_WHITESPACE_NORMAL ||
           mWhiteSpace == NS_STYLE_WHITESPACE_PRE_WRAP ||
           mWhiteSpace == NS_STYLE_WHITESPACE_PRE_LINE;
  }

  PRBool WordCanWrap() const {
    return WhiteSpaceCanWrap() && mWordWrap == NS_STYLE_WORDWRAP_BREAK_WORD;
  }
};

struct nsStyleVisibility {
  nsStyleVisibility(nsPresContext* aPresContext);
  nsStyleVisibility(const nsStyleVisibility& aVisibility);
  ~nsStyleVisibility() {
    MOZ_COUNT_DTOR(nsStyleVisibility);
  }

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleVisibility();
    aContext->FreeToShell(sizeof(nsStyleVisibility), this);
  }

  nsChangeHint CalcDifference(const nsStyleVisibility& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  PRUint8 mDirection;                  // [inherited] see nsStyleConsts.h NS_STYLE_DIRECTION_*
  PRUint8   mVisible;                  // [inherited]
  nsCOMPtr<nsIAtom> mLanguage;         // [inherited]
  PRUint8 mPointerEvents;              // [inherited] see nsStyleConsts.h

  PRBool IsVisible() const {
    return (mVisible == NS_STYLE_VISIBILITY_VISIBLE);
  }

  PRBool IsVisibleOrCollapsed() const {
    return ((mVisible == NS_STYLE_VISIBILITY_VISIBLE) ||
            (mVisible == NS_STYLE_VISIBILITY_COLLAPSE));
  }
};

struct nsTimingFunction {
  explicit nsTimingFunction(PRInt32 aTimingFunctionType
                              = NS_STYLE_TRANSITION_TIMING_FUNCTION_EASE)
  {
    AssignFromKeyword(aTimingFunctionType);
  }

  nsTimingFunction(float x1, float y1, float x2, float y2)
    : mX1(x1)
    , mY1(y1)
    , mX2(x2)
    , mY2(y2)
  {}

  float mX1;
  float mY1;
  float mX2;
  float mY2;

  PRBool operator==(const nsTimingFunction& aOther) const
  {
    return !(*this != aOther);
  }

  PRBool operator!=(const nsTimingFunction& aOther) const
  {
    return mX1 != aOther.mX1 || mY1 != aOther.mY1 ||
           mX2 != aOther.mX2 || mY2 != aOther.mY2;
  }

private:
  void AssignFromKeyword(PRInt32 aTimingFunctionType);
};

struct nsTransition {
  nsTransition() { /* leaves uninitialized; see also SetInitialValues */ }
  explicit nsTransition(const nsTransition& aCopy);

  void SetInitialValues();

  // Delay and Duration are in milliseconds

  nsTimingFunction& GetTimingFunction() { return mTimingFunction; }
  const nsTimingFunction& GetTimingFunction() const { return mTimingFunction; }
  float GetDelay() const { return mDelay; }
  float GetDuration() const { return mDuration; }
  nsCSSProperty GetProperty() const { return mProperty; }
  nsIAtom* GetUnknownProperty() const { return mUnknownProperty; }

  void SetTimingFunction(const nsTimingFunction& aTimingFunction)
    { mTimingFunction = aTimingFunction; }
  void SetDelay(float aDelay) { mDelay = aDelay; }
  void SetDuration(float aDuration) { mDuration = aDuration; }
  void SetProperty(nsCSSProperty aProperty)
    {
      NS_ASSERTION(aProperty != eCSSProperty_UNKNOWN, "invalid property");
      mProperty = aProperty;
    }
  void SetUnknownProperty(const nsAString& aUnknownProperty);
  void CopyPropertyFrom(const nsTransition& aOther)
    {
      mProperty = aOther.mProperty;
      mUnknownProperty = aOther.mUnknownProperty;
    }

private:
  nsTimingFunction mTimingFunction;
  float mDuration;
  float mDelay;
  nsCSSProperty mProperty;
  nsCOMPtr<nsIAtom> mUnknownProperty; // used when mProperty is
                                      // eCSSProperty_UNKNOWN
};

struct nsStyleDisplay {
  nsStyleDisplay();
  nsStyleDisplay(const nsStyleDisplay& aOther);
  ~nsStyleDisplay() {
    MOZ_COUNT_DTOR(nsStyleDisplay);
  }

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleDisplay();
    aContext->FreeToShell(sizeof(nsStyleDisplay), this);
  }

  nsChangeHint CalcDifference(const nsStyleDisplay& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_TRUE; }

  // We guarantee that if mBinding is non-null, so are mBinding->mURI and
  // mBinding->mOriginPrincipal.
  nsRefPtr<nsCSSValue::URL> mBinding;    // [reset]
  nsRect    mClip;              // [reset] offsets from upper-left border edge
  float   mOpacity;             // [reset]
  PRUint8 mDisplay;             // [reset] see nsStyleConsts.h NS_STYLE_DISPLAY_*
  PRUint8 mOriginalDisplay;     // [reset] saved mDisplay for position:absolute/fixed
  PRUint8 mAppearance;          // [reset]
  PRUint8 mPosition;            // [reset] see nsStyleConsts.h
  PRUint8 mFloats;              // [reset] see nsStyleConsts.h NS_STYLE_FLOAT_*
  PRUint8 mBreakType;           // [reset] see nsStyleConsts.h NS_STYLE_CLEAR_*
  PRPackedBool mBreakBefore;    // [reset]
  PRPackedBool mBreakAfter;     // [reset]
  PRUint8 mOverflowX;           // [reset] see nsStyleConsts.h
  PRUint8 mOverflowY;           // [reset] see nsStyleConsts.h
  PRUint8 mResize;              // [reset] see nsStyleConsts.h
  PRUint8   mClipFlags;         // [reset] see nsStyleConsts.h

  // mSpecifiedTransform is the list of transform functions as
  // specified, or null to indicate there is no transform.  (inherit or
  // initial are replaced by an actual list of transform functions, or
  // null, as appropriate.) (owned by the style rule)
  const nsCSSValueList *mSpecifiedTransform; // [reset]
  nsStyleTransformMatrix mTransform; // [reset] The stored transform matrix
  nsStyleCoord mTransformOrigin[2]; // [reset] percent, coord.

  nsAutoTArray<nsTransition, 1> mTransitions; // [reset]
  // The number of elements in mTransitions that are not from repeating
  // a list due to another property being longer.
  PRUint32 mTransitionTimingFunctionCount,
           mTransitionDurationCount,
           mTransitionDelayCount,
           mTransitionPropertyCount;

  PRBool IsBlockInside() const {
    return NS_STYLE_DISPLAY_BLOCK == mDisplay ||
           NS_STYLE_DISPLAY_LIST_ITEM == mDisplay ||
           NS_STYLE_DISPLAY_INLINE_BLOCK == mDisplay;
    // Should TABLE_CELL and TABLE_CAPTION go here?  They have
    // block frames nested inside of them.
    // (But please audit all callers before changing.)
  }

  PRBool IsBlockOutside() const {
    return NS_STYLE_DISPLAY_BLOCK == mDisplay ||
           NS_STYLE_DISPLAY_LIST_ITEM == mDisplay ||
           NS_STYLE_DISPLAY_TABLE == mDisplay;
  }

  PRBool IsInlineOutside() const {
    return NS_STYLE_DISPLAY_INLINE == mDisplay ||
           NS_STYLE_DISPLAY_INLINE_BLOCK == mDisplay ||
           NS_STYLE_DISPLAY_INLINE_TABLE == mDisplay ||
           NS_STYLE_DISPLAY_INLINE_BOX == mDisplay ||
           NS_STYLE_DISPLAY_INLINE_GRID == mDisplay ||
           NS_STYLE_DISPLAY_INLINE_STACK == mDisplay;
  }

  PRBool IsFloating() const {
    return NS_STYLE_FLOAT_NONE != mFloats;
  }

  PRBool IsAbsolutelyPositioned() const {return (NS_STYLE_POSITION_ABSOLUTE == mPosition) ||
                                                (NS_STYLE_POSITION_FIXED == mPosition);}

  /* Returns true if we're positioned or there's a transform in effect. */
  PRBool IsPositioned() const {
    return IsAbsolutelyPositioned() ||
      NS_STYLE_POSITION_RELATIVE == mPosition || HasTransform();
  }

  PRBool IsScrollableOverflow() const {
    // mOverflowX and mOverflowY always match when one of them is
    // NS_STYLE_OVERFLOW_VISIBLE or NS_STYLE_OVERFLOW_CLIP.
    return mOverflowX != NS_STYLE_OVERFLOW_VISIBLE &&
           mOverflowX != NS_STYLE_OVERFLOW_CLIP;
  }

  // For table elements that don't support scroll frame creation, we
  // support 'overflow: hidden' to mean 'overflow: -moz-hidden-unscrollable'.
  PRBool IsTableClip() const {
    return mOverflowX == NS_STYLE_OVERFLOW_CLIP ||
           (mOverflowX == NS_STYLE_OVERFLOW_HIDDEN &&
            mOverflowY == NS_STYLE_OVERFLOW_HIDDEN);
  }

  /* Returns whether the element has the -moz-transform property. */
  PRBool HasTransform() const {
    return mSpecifiedTransform != nsnull;
  }
};

struct nsStyleTable {
  nsStyleTable(void);
  nsStyleTable(const nsStyleTable& aOther);
  ~nsStyleTable(void);

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleTable();
    aContext->FreeToShell(sizeof(nsStyleTable), this);
  }

  nsChangeHint CalcDifference(const nsStyleTable& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  PRUint8       mLayoutStrategy;// [reset] see nsStyleConsts.h NS_STYLE_TABLE_LAYOUT_*
  PRUint8       mFrame;         // [reset] see nsStyleConsts.h NS_STYLE_TABLE_FRAME_*
  PRUint8       mRules;         // [reset] see nsStyleConsts.h NS_STYLE_TABLE_RULES_*
  PRInt32       mCols;          // [reset] an integer if set, or see nsStyleConsts.h NS_STYLE_TABLE_COLS_*
  PRInt32       mSpan;          // [reset] the number of columns spanned by a colgroup or col
};

struct nsStyleTableBorder {
  nsStyleTableBorder(nsPresContext* aContext);
  nsStyleTableBorder(const nsStyleTableBorder& aOther);
  ~nsStyleTableBorder(void);

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleTableBorder();
    aContext->FreeToShell(sizeof(nsStyleTableBorder), this);
  }

  nsChangeHint CalcDifference(const nsStyleTableBorder& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  nscoord       mBorderSpacingX;// [inherited]
  nscoord       mBorderSpacingY;// [inherited]
  PRUint8       mBorderCollapse;// [inherited]
  PRUint8       mCaptionSide;   // [inherited]
  PRUint8       mEmptyCells;    // [inherited]
};

enum nsStyleContentType {
  eStyleContentType_String        = 1,
  eStyleContentType_Image         = 10,
  eStyleContentType_Attr          = 20,
  eStyleContentType_Counter       = 30,
  eStyleContentType_Counters      = 31,
  eStyleContentType_OpenQuote     = 40,
  eStyleContentType_CloseQuote    = 41,
  eStyleContentType_NoOpenQuote   = 42,
  eStyleContentType_NoCloseQuote  = 43,
  eStyleContentType_AltContent    = 50
};

struct nsStyleContentData {
  nsStyleContentType  mType;
  union {
    PRUnichar *mString;
    imgIRequest *mImage;
    nsCSSValue::Array* mCounters;
  } mContent;
#ifdef DEBUG
  bool mImageTracked;
#endif

  nsStyleContentData()
    : mType(nsStyleContentType(0))
#ifdef DEBUG
    , mImageTracked(false)
#endif
  { mContent.mString = nsnull; }

  ~nsStyleContentData();
  nsStyleContentData& operator=(const nsStyleContentData& aOther);
  PRBool operator==(const nsStyleContentData& aOther) const;

  PRBool operator!=(const nsStyleContentData& aOther) const {
    return !(*this == aOther);
  }

  void TrackImage(nsPresContext* aContext);
  void UntrackImage(nsPresContext* aContext);

  void SetImage(imgIRequest* aRequest)
  {
    NS_ABORT_IF_FALSE(!mImageTracked,
                      "Setting a new image without untracking the old one!");
    NS_ABORT_IF_FALSE(mType == eStyleContentType_Image, "Wrong type!");
    NS_IF_ADDREF(mContent.mImage = aRequest);
  }
private:
  nsStyleContentData(const nsStyleContentData&); // not to be implemented
};

struct nsStyleCounterData {
  nsString  mCounter;
  PRInt32   mValue;
};


#define DELETE_ARRAY_IF(array)  if (array) { delete[] array; array = nsnull; }

struct nsStyleQuotes {
  nsStyleQuotes();
  nsStyleQuotes(const nsStyleQuotes& aQuotes);
  ~nsStyleQuotes();

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleQuotes();
    aContext->FreeToShell(sizeof(nsStyleQuotes), this);
  }

  void SetInitial();
  void CopyFrom(const nsStyleQuotes& aSource);

  nsChangeHint CalcDifference(const nsStyleQuotes& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  PRUint32  QuotesCount(void) const { return mQuotesCount; } // [inherited]

  const nsString* OpenQuoteAt(PRUint32 aIndex) const
  {
    NS_ASSERTION(aIndex < mQuotesCount, "out of range");
    return mQuotes + (aIndex * 2);
  }
  const nsString* CloseQuoteAt(PRUint32 aIndex) const
  {
    NS_ASSERTION(aIndex < mQuotesCount, "out of range");
    return mQuotes + (aIndex * 2 + 1);
  }
  nsresult  GetQuotesAt(PRUint32 aIndex, nsString& aOpen, nsString& aClose) const {
    if (aIndex < mQuotesCount) {
      aIndex *= 2;
      aOpen = mQuotes[aIndex];
      aClose = mQuotes[++aIndex];
      return NS_OK;
    }
    return NS_ERROR_ILLEGAL_VALUE;
  }

  nsresult  AllocateQuotes(PRUint32 aCount) {
    if (aCount != mQuotesCount) {
      DELETE_ARRAY_IF(mQuotes);
      if (aCount) {
        mQuotes = new nsString[aCount * 2];
        if (! mQuotes) {
          mQuotesCount = 0;
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }
      mQuotesCount = aCount;
    }
    return NS_OK;
  }

  nsresult  SetQuotesAt(PRUint32 aIndex, const nsString& aOpen, const nsString& aClose) {
    if (aIndex < mQuotesCount) {
      aIndex *= 2;
      mQuotes[aIndex] = aOpen;
      mQuotes[++aIndex] = aClose;
      return NS_OK;
    }
    return NS_ERROR_ILLEGAL_VALUE;
  }

protected:
  PRUint32            mQuotesCount;
  nsString*           mQuotes;
};

struct nsStyleContent {
  nsStyleContent(void);
  nsStyleContent(const nsStyleContent& aContent);
  ~nsStyleContent(void);

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext);

  nsChangeHint CalcDifference(const nsStyleContent& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  PRUint32  ContentCount(void) const  { return mContentCount; } // [reset]

  const nsStyleContentData& ContentAt(PRUint32 aIndex) const {
    NS_ASSERTION(aIndex < mContentCount, "out of range");
    return mContents[aIndex];
  }

  nsStyleContentData& ContentAt(PRUint32 aIndex) {
    NS_ASSERTION(aIndex < mContentCount, "out of range");
    return mContents[aIndex];
  }

  nsresult AllocateContents(PRUint32 aCount);

  PRUint32  CounterIncrementCount(void) const { return mIncrementCount; }  // [reset]
  const nsStyleCounterData* GetCounterIncrementAt(PRUint32 aIndex) const {
    NS_ASSERTION(aIndex < mIncrementCount, "out of range");
    return &mIncrements[aIndex];
  }

  nsresult  AllocateCounterIncrements(PRUint32 aCount) {
    if (aCount != mIncrementCount) {
      DELETE_ARRAY_IF(mIncrements);
      if (aCount) {
        mIncrements = new nsStyleCounterData[aCount];
        if (! mIncrements) {
          mIncrementCount = 0;
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }
      mIncrementCount = aCount;
    }
    return NS_OK;
  }

  nsresult  SetCounterIncrementAt(PRUint32 aIndex, const nsString& aCounter, PRInt32 aIncrement) {
    if (aIndex < mIncrementCount) {
      mIncrements[aIndex].mCounter = aCounter;
      mIncrements[aIndex].mValue = aIncrement;
      return NS_OK;
    }
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint32  CounterResetCount(void) const { return mResetCount; }  // [reset]
  const nsStyleCounterData* GetCounterResetAt(PRUint32 aIndex) const {
    NS_ASSERTION(aIndex < mResetCount, "out of range");
    return &mResets[aIndex];
  }

  nsresult  AllocateCounterResets(PRUint32 aCount) {
    if (aCount != mResetCount) {
      DELETE_ARRAY_IF(mResets);
      if (aCount) {
        mResets = new nsStyleCounterData[aCount];
        if (! mResets) {
          mResetCount = 0;
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }
      mResetCount = aCount;
    }
    return NS_OK;
  }

  nsresult  SetCounterResetAt(PRUint32 aIndex, const nsString& aCounter, PRInt32 aValue) {
    if (aIndex < mResetCount) {
      mResets[aIndex].mCounter = aCounter;
      mResets[aIndex].mValue = aValue;
      return NS_OK;
    }
    return NS_ERROR_ILLEGAL_VALUE;
  }

  nsStyleCoord  mMarkerOffset;  // [reset] coord, auto

protected:
  nsStyleContentData* mContents;
  nsStyleCounterData* mIncrements;
  nsStyleCounterData* mResets;

  PRUint32            mContentCount;
  PRUint32            mIncrementCount;
  PRUint32            mResetCount;
};

struct nsStyleUIReset {
  nsStyleUIReset(void);
  nsStyleUIReset(const nsStyleUIReset& aOther);
  ~nsStyleUIReset(void);

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleUIReset();
    aContext->FreeToShell(sizeof(nsStyleUIReset), this);
  }

  nsChangeHint CalcDifference(const nsStyleUIReset& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  PRUint8   mUserSelect;      // [reset] (selection-style)
  PRUint8   mForceBrokenImageIcon; // [reset]  (0 if not forcing, otherwise forcing)
  PRUint8   mIMEMode;         // [reset]
  PRUint8   mWindowShadow;    // [reset]
};

struct nsCursorImage {
  PRBool mHaveHotspot;
  float mHotspotX, mHotspotY;

  nsCursorImage();
  nsCursorImage(const nsCursorImage& aOther);
  ~nsCursorImage();

  nsCursorImage& operator=(const nsCursorImage& aOther);
  /*
   * We hide mImage and force access through the getter and setter so that we
   * can lock the images we use. Cursor images are likely to be small, so we
   * don't care about discarding them. See bug 512260.
   * */
  void SetImage(imgIRequest *aImage) {
    if (mImage)
      mImage->UnlockImage();
    mImage = aImage;
    if (mImage)
      mImage->LockImage();
  }
  imgIRequest* GetImage() const {
    return mImage;
  }

private:
  nsCOMPtr<imgIRequest> mImage;
};

struct nsStyleUserInterface {
  nsStyleUserInterface(void);
  nsStyleUserInterface(const nsStyleUserInterface& aOther);
  ~nsStyleUserInterface(void);

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleUserInterface();
    aContext->FreeToShell(sizeof(nsStyleUserInterface), this);
  }

  nsChangeHint CalcDifference(const nsStyleUserInterface& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  PRUint8   mUserInput;       // [inherited]
  PRUint8   mUserModify;      // [inherited] (modify-content)
  PRUint8   mUserFocus;       // [inherited] (auto-select)

  PRUint8   mCursor;          // [inherited] See nsStyleConsts.h

  PRUint32 mCursorArrayLength;
  nsCursorImage *mCursorArray;// [inherited] The specified URL values
                              //   and coordinates.  Takes precedence over
                              //   mCursor.  Zero-length array is represented
                              //   by null pointer.

  // Does not free mCursorArray; the caller is responsible for calling
  // |delete [] mCursorArray| first if it is needed.
  void CopyCursorArrayFrom(const nsStyleUserInterface& aSource);
};

struct nsStyleXUL {
  nsStyleXUL();
  nsStyleXUL(const nsStyleXUL& aSource);
  ~nsStyleXUL();

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleXUL();
    aContext->FreeToShell(sizeof(nsStyleXUL), this);
  }

  nsChangeHint CalcDifference(const nsStyleXUL& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  float         mBoxFlex;               // [reset] see nsStyleConsts.h
  PRUint32      mBoxOrdinal;            // [reset] see nsStyleConsts.h
  PRUint8       mBoxAlign;              // [reset] see nsStyleConsts.h
  PRUint8       mBoxDirection;          // [reset] see nsStyleConsts.h
  PRUint8       mBoxOrient;             // [reset] see nsStyleConsts.h
  PRUint8       mBoxPack;               // [reset] see nsStyleConsts.h
  PRPackedBool  mStretchStack;          // [reset] see nsStyleConsts.h
};

struct nsStyleColumn {
  nsStyleColumn(nsPresContext* aPresContext);
  nsStyleColumn(const nsStyleColumn& aSource);
  ~nsStyleColumn();

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleColumn();
    aContext->FreeToShell(sizeof(nsStyleColumn), this);
  }

  nsChangeHint CalcDifference(const nsStyleColumn& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  PRUint32     mColumnCount; // [reset] see nsStyleConsts.h
  nsStyleCoord mColumnWidth; // [reset] coord, auto
  nsStyleCoord mColumnGap;   // [reset] coord, normal

  nscolor      mColumnRuleColor;  // [reset]
  PRUint8      mColumnRuleStyle;  // [reset]
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=271586#c43 for why
  // this is hard to replace with 'currentColor'.
  PRPackedBool mColumnRuleColorIsForeground;

  void SetColumnRuleWidth(nscoord aWidth) {
    mColumnRuleWidth = NS_ROUND_BORDER_TO_PIXELS(aWidth, mTwipsPerPixel);
  }

  nscoord GetComputedColumnRuleWidth() const {
    return (IsVisibleBorderStyle(mColumnRuleStyle) ? mColumnRuleWidth : 0);
  }

protected:
  nscoord mColumnRuleWidth;  // [reset] coord
  nscoord mTwipsPerPixel;
};

enum nsStyleSVGPaintType {
  eStyleSVGPaintType_None = 1,
  eStyleSVGPaintType_Color,
  eStyleSVGPaintType_Server
};

struct nsStyleSVGPaint
{
  union {
    nscolor mColor;
    nsIURI *mPaintServer;
  } mPaint;
  nsStyleSVGPaintType mType;
  nscolor mFallbackColor;

  nsStyleSVGPaint() : mType(nsStyleSVGPaintType(0)) { mPaint.mPaintServer = nsnull; }
  ~nsStyleSVGPaint();
  void SetType(nsStyleSVGPaintType aType);
  nsStyleSVGPaint& operator=(const nsStyleSVGPaint& aOther);
  PRBool operator==(const nsStyleSVGPaint& aOther) const;

  PRBool operator!=(const nsStyleSVGPaint& aOther) const {
    return !(*this == aOther);
  }
};

struct nsStyleSVG {
  nsStyleSVG();
  nsStyleSVG(const nsStyleSVG& aSource);
  ~nsStyleSVG();

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleSVG();
    aContext->FreeToShell(sizeof(nsStyleSVG), this);
  }

  nsChangeHint CalcDifference(const nsStyleSVG& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  nsStyleSVGPaint  mFill;             // [inherited]
  nsStyleSVGPaint  mStroke;           // [inherited]
  nsCOMPtr<nsIURI> mMarkerEnd;        // [inherited]
  nsCOMPtr<nsIURI> mMarkerMid;        // [inherited]
  nsCOMPtr<nsIURI> mMarkerStart;      // [inherited]
  nsStyleCoord    *mStrokeDasharray;  // [inherited] coord, percent, factor

  nsStyleCoord     mStrokeDashoffset; // [inherited] coord, percent, factor
  nsStyleCoord     mStrokeWidth;      // [inherited] coord, percent, factor

  float            mFillOpacity;      // [inherited]
  float            mStrokeMiterlimit; // [inherited]
  float            mStrokeOpacity;    // [inherited]

  PRUint32         mStrokeDasharrayLength;
  PRUint8          mClipRule;         // [inherited]
  PRUint8          mColorInterpolation; // [inherited] see nsStyleConsts.h
  PRUint8          mColorInterpolationFilters; // [inherited] see nsStyleConsts.h
  PRUint8          mFillRule;         // [inherited] see nsStyleConsts.h
  PRUint8          mImageRendering;   // [inherited] see nsStyleConsts.h
  PRUint8          mShapeRendering;   // [inherited] see nsStyleConsts.h
  PRUint8          mStrokeLinecap;    // [inherited] see nsStyleConsts.h
  PRUint8          mStrokeLinejoin;   // [inherited] see nsStyleConsts.h
  PRUint8          mTextAnchor;       // [inherited] see nsStyleConsts.h
  PRUint8          mTextRendering;    // [inherited] see nsStyleConsts.h
};

struct nsStyleSVGReset {
  nsStyleSVGReset();
  nsStyleSVGReset(const nsStyleSVGReset& aSource);
  ~nsStyleSVGReset();

  void* operator new(size_t sz, nsPresContext* aContext) CPP_THROW_NEW {
    return aContext->AllocateFromShell(sz);
  }
  void Destroy(nsPresContext* aContext) {
    this->~nsStyleSVGReset();
    aContext->FreeToShell(sizeof(nsStyleSVGReset), this);
  }

  nsChangeHint CalcDifference(const nsStyleSVGReset& aOther) const;
#ifdef DEBUG
  static nsChangeHint MaxDifference();
#endif
  static PRBool ForceCompare() { return PR_FALSE; }

  nsCOMPtr<nsIURI> mClipPath;         // [reset]
  nsCOMPtr<nsIURI> mFilter;           // [reset]
  nsCOMPtr<nsIURI> mMask;             // [reset]
  nscolor          mStopColor;        // [reset]
  nscolor          mFloodColor;       // [reset]
  nscolor          mLightingColor;    // [reset]

  float            mStopOpacity;      // [reset]
  float            mFloodOpacity;     // [reset]

  PRUint8          mDominantBaseline; // [reset] see nsStyleConsts.h
};

#endif /* nsStyleStruct_h___ */
