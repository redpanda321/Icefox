/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef IBMBIDI

#ifndef nsBidiPresUtils_h___
#define nsBidiPresUtils_h___

#include "nsTArray.h"
#include "nsIFrame.h"
#include "nsBidi.h"
#include "nsBidiUtils.h"
#include "nsCOMPtr.h"
#include "nsDataHashtable.h"
#include "nsBlockFrame.h"
#include "nsTHashtable.h"

#ifdef DrawText
#undef DrawText
#endif

struct BidiParagraphData;
struct BidiLineData;

/**
 * A structure representing some continuation state for each frame on the line,
 * used to determine the first and the last continuation frame for each
 * continuation chain on the line.
 */
struct nsFrameContinuationState : public nsVoidPtrHashKey
{
  nsFrameContinuationState(const void *aFrame) : nsVoidPtrHashKey(aFrame) {}

  /**
   * The first visual frame in the continuation chain containing this frame, or
   * nsnull if this frame is the first visual frame in the chain.
   */
  nsIFrame* mFirstVisualFrame;

  /**
   * The number of frames in the continuation chain containing this frame, if
   * this frame is the first visual frame of the chain, or 0 otherwise.
   */
  PRUint32 mFrameCount;

  /**
   * TRUE if this frame is the first visual frame of its continuation chain on
   * this line and the chain has some frames on the previous lines.
   */
  bool mHasContOnPrevLines;

  /**
   * TRUE if this frame is the first visual frame of its continuation chain on
   * this line and the chain has some frames left for next lines.
   */
  bool mHasContOnNextLines;
};

/*
 * Following type is used to pass needed hashtable to reordering methods
 */
typedef nsTHashtable<nsFrameContinuationState> nsContinuationStates;

/**
 * A structure representing a logical position which should be resolved
 * into its visual position during BiDi processing.
 */
struct nsBidiPositionResolve
{
  // [in] Logical index within string.
  PRInt32 logicalIndex;
  // [out] Visual index within string.
  // If the logical position was not found, set to kNotFound.
  PRInt32 visualIndex;
  // [out] Visual position of the character, from the left (on the X axis), in twips.
  // Eessentially, this is the X position (relative to the rendering context) where the text was drawn + the font metric of the visual string to the left of the given logical position.
  // If the logical position was not found, set to kNotFound.
  PRInt32 visualLeftTwips;
  // [out] Visual width of the character, in twips.
  // If the logical position was not found, set to kNotFound.
  PRInt32 visualWidth;
};

class nsBidiPresUtils {
public:
  nsBidiPresUtils();
  ~nsBidiPresUtils();
  
  /**
   * Interface for the processor used by ProcessText. Used by process text to
   * collect information about the width of subruns and to notify where each
   * subrun should be rendered.
   */
  class BidiProcessor {
  public:
    virtual ~BidiProcessor() { }

    /**
     * Sets the current text with the given length and the given direction.
     *
     * @remark The reason that the function gives a string instead of an index
     *  is that ProcessText copies and modifies the string passed to it, so
     *  passing an index would be impossible.
     * 
     * @param aText The string of text.
     * @param aLength The length of the string of text.
     * @param aDirection The direction of the text. The string will never have
     *  mixed direction.
     */
    virtual void SetText(const PRUnichar*   aText,
                         PRInt32            aLength,
                         nsBidiDirection    aDirection) = 0;

    /**
     * Returns the measured width of the text given in SetText. If SetText was
     * not called with valid parameters, the result of this call is undefined.
     * This call is guaranteed to only be called once between SetText calls.
     * Will be invoked before DrawText.
     */
    virtual nscoord GetWidth() = 0;

    /**
     * Draws the text given in SetText to a rendering context. If SetText was
     * not called with valid parameters, the result of this call is undefined.
     * This call is guaranteed to only be called once between SetText calls.
     * 
     * @param aXOffset The offset of the left side of the substring to be drawn
     *  from the beginning of the overall string passed to ProcessText.
     * @param aWidth The width returned by GetWidth.
     */
    virtual void DrawText(nscoord   aXOffset,
                          nscoord   aWidth) = 0;
  };

  /**
   * Make Bidi engine calculate the embedding levels of the frames that are
   * descendants of a given block frame.
   *
   * @param aBlockFrame          The block frame
   *
   *  @lina 06/18/2000
   */
  static nsresult Resolve(nsBlockFrame* aBlockFrame);
  static nsresult ResolveParagraph(nsBlockFrame* aBlockFrame,
                                   BidiParagraphData* aBpd);
  static void ResolveParagraphWithinBlock(nsBlockFrame* aBlockFrame,
                                          BidiParagraphData* aBpd);

  /**
   * Reorder this line using Bidi engine.
   * Update frame array, following the new visual sequence.
   * 
   * @lina 05/02/2000
   */
  static void ReorderFrames(nsIFrame*            aFirstFrameOnLine,
                            PRInt32              aNumFramesOnLine);

  /**
   * Format Unicode text, taking into account bidi capabilities
   * of the platform. The formatting includes: reordering, Arabic shaping,
   * symmetric and numeric swapping, removing control characters.
   *
   * @lina 06/18/2000 
   */
  static nsresult FormatUnicodeText(nsPresContext* aPresContext,
                                    PRUnichar*      aText,
                                    PRInt32&        aTextLength,
                                    nsCharType      aCharType,
                                    bool            aIsOddLevel);

  /**
   * Reorder plain text using the Unicode Bidi algorithm and send it to
   * a rendering context for rendering.
   *
   * @param[in] aText  the string to be rendered (in logical order)
   * @param aLength the number of characters in the string
   * @param aBaseDirection the base direction of the string
   *  NSBIDI_LTR - left-to-right string
   *  NSBIDI_RTL - right-to-left string
   * @param aPresContext the presentation context
   * @param aRenderingContext the rendering context to render to
   * @param aTextRunConstructionContext the rendering context to be used to construct the textrun (affects font hinting)
   * @param aX the x-coordinate to render the string
   * @param aY the y-coordinate to render the string
   * @param[in,out] aPosResolve array of logical positions to resolve into visual positions; can be nsnull if this functionality is not required
   * @param aPosResolveCount number of items in the aPosResolve array
   */
  static nsresult RenderText(const PRUnichar*       aText,
                             PRInt32                aLength,
                             nsBidiDirection        aBaseDirection,
                             nsPresContext*         aPresContext,
                             nsRenderingContext&    aRenderingContext,
                             nsRenderingContext&    aTextRunConstructionContext,
                             nscoord                aX,
                             nscoord                aY,
                             nsBidiPositionResolve* aPosResolve = nsnull,
                             PRInt32                aPosResolveCount = 0)
  {
    return ProcessTextForRenderingContext(aText, aLength, aBaseDirection, aPresContext, aRenderingContext,
                                          aTextRunConstructionContext, MODE_DRAW, aX, aY, aPosResolve, aPosResolveCount, nsnull);
  }
  
  static nscoord MeasureTextWidth(const PRUnichar*     aText,
                                  PRInt32              aLength,
                                  nsBidiDirection      aBaseDirection,
                                  nsPresContext*       aPresContext,
                                  nsRenderingContext&  aRenderingContext)
  {
    nscoord length;
    nsresult rv = ProcessTextForRenderingContext(aText, aLength, aBaseDirection, aPresContext,
                                                 aRenderingContext, aRenderingContext,
                                                 MODE_MEASURE, 0, 0, nsnull, 0, &length);
    return NS_SUCCEEDED(rv) ? length : 0;
  }

  /**
   * Check if a line is reordered, i.e., if the child frames are not
   * all laid out left-to-right.
   * @param aFirstFrameOnLine : first frame of the line to be tested
   * @param aNumFramesOnLine : number of frames on this line
   * @param[out] aLeftMost : leftmost frame on this line
   * @param[out] aRightMost : rightmost frame on this line
   */
  static bool CheckLineOrder(nsIFrame*  aFirstFrameOnLine,
                               PRInt32    aNumFramesOnLine,
                               nsIFrame** aLeftmost,
                               nsIFrame** aRightmost);

  /**
   * Get the frame to the right of the given frame, on the same line.
   * @param aFrame : We're looking for the frame to the right of this frame.
   *                 If null, return the leftmost frame on the line.
   * @param aFirstFrameOnLine : first frame of the line to be tested
   * @param aNumFramesOnLine : number of frames on this line
   */
  static nsIFrame* GetFrameToRightOf(const nsIFrame*  aFrame,
                                     nsIFrame*        aFirstFrameOnLine,
                                     PRInt32          aNumFramesOnLine);
    
  /**
   * Get the frame to the left of the given frame, on the same line.
   * @param aFrame : We're looking for the frame to the left of this frame.
   *                 If null, return the rightmost frame on the line.
   * @param aFirstFrameOnLine : first frame of the line to be tested
   * @param aNumFramesOnLine : number of frames on this line
   */
  static nsIFrame* GetFrameToLeftOf(const nsIFrame*  aFrame,
                                    nsIFrame*        aFirstFrameOnLine,
                                    PRInt32          aNumFramesOnLine);

  static nsIFrame* GetFirstLeaf(nsIFrame* aFrame);
    
  /**
   * Get the bidi embedding level of the given (inline) frame.
   */
  static nsBidiLevel GetFrameEmbeddingLevel(nsIFrame* aFrame);
    
  /**
   * Get the paragraph depth of the given (inline) frame.
   */
  static PRUint8 GetParagraphDepth(nsIFrame* aFrame);

  /**
   * Get the bidi base level of the given (inline) frame.
   */
  static nsBidiLevel GetFrameBaseLevel(nsIFrame* aFrame);

  enum Mode { MODE_DRAW, MODE_MEASURE };

  /**
   * Reorder plain text using the Unicode Bidi algorithm and send it to
   * a processor for rendering or measuring
   *
   * @param[in] aText  the string to be processed (in logical order)
   * @param aLength the number of characters in the string
   * @param aBaseDirection the base direction of the string
   *  NSBIDI_LTR - left-to-right string
   *  NSBIDI_RTL - right-to-left string
   * @param aPresContext the presentation context
   * @param aprocessor the bidi processor
   * @param aMode the operation to process
   *  MODE_DRAW - invokes DrawText on the processor for each substring
   *  MODE_MEASURE - does not invoke DrawText on the processor
   *  Note that the string is always measured, regardless of mode
   * @param[in,out] aPosResolve array of logical positions to resolve into
   *  visual positions; can be nsnull if this functionality is not required
   * @param aPosResolveCount number of items in the aPosResolve array
   * @param[out] aWidth Pointer to where the width will be stored (may be null)
   */
  static nsresult ProcessText(const PRUnichar*       aText,
                              PRInt32                aLength,
                              nsBidiDirection        aBaseDirection,
                              nsPresContext*         aPresContext,
                              BidiProcessor&         aprocessor,
                              Mode                   aMode,
                              nsBidiPositionResolve* aPosResolve,
                              PRInt32                aPosResolveCount,
                              nscoord*               aWidth,
                              nsBidi*                aBidiEngine);

  /**
   * Make a copy of a string, converting from logical to visual order
   *
   * @param aSource the source string
   * @param aDest the destination string
   * @param aBaseDirection the base direction of the string
   *       (NSBIDI_LTR or NSBIDI_RTL to force the base direction;
   *        NSBIDI_DEFAULT_LTR or NSBIDI_DEFAULT_RTL to let the bidi engine
   *        determine the direction from rules P2 and P3 of the bidi algorithm.
   *  @see nsBidi::GetPara
   * @param aOverride if TRUE, the text has a bidi override, according to
   *                    the direction in aDir
   */
  static void CopyLogicalToVisual(const nsAString& aSource,
                                  nsAString& aDest,
                                  nsBidiLevel aBaseDirection,
                                  bool aOverride);

private:
  static nsresult
  ProcessTextForRenderingContext(const PRUnichar*       aText,
                                 PRInt32                aLength,
                                 nsBidiDirection        aBaseDirection,
                                 nsPresContext*         aPresContext,
                                 nsRenderingContext&    aRenderingContext,
                                 nsRenderingContext&    aTextRunConstructionContext,
                                 Mode                   aMode,
                                 nscoord                aX, // DRAW only
                                 nscoord                aY, // DRAW only
                                 nsBidiPositionResolve* aPosResolve,  /* may be null */
                                 PRInt32                aPosResolveCount,
                                 nscoord*               aWidth /* may be null */);

  /**
   * Traverse the child frames of the block element and:
   *  Set up an array of the frames in logical order
   *  Create a string containing the text content of all the frames
   *  If we encounter content that requires us to split the element into more
   *  than one paragraph for bidi resolution, resolve the paragraph up to that
   *  point.
   */
  static void TraverseFrames(nsBlockFrame*              aBlockFrame,
                             nsBlockInFlowLineIterator* aLineIter,
                             nsIFrame*                  aCurrentFrame,
                             BidiParagraphData*         aBpd);
  
  /*
   * Position aFrame and it's descendants to their visual places. Also if aFrame
   * is not leaf, resize it to embrace it's children.
   *
   * @param aFrame               The frame which itself and its children are going
   *                             to be repositioned
   * @param aIsOddLevel          TRUE means the embedding level of this frame is odd
   * @param[in,out] aLeft        IN value is the starting position of aFrame(without
   *                             considering its left margin)
   *                             OUT value will be the ending position of aFrame(after
   *                             adding its right margin)
   * @param aContinuationStates  A map from nsIFrame* to nsFrameContinuationState
   */
  static void RepositionFrame(nsIFrame*              aFrame,
                              bool                   aIsOddLevel,
                              nscoord&               aLeft,
                              nsContinuationStates*  aContinuationStates);

  /*
   * Initialize the continuation state(nsFrameContinuationState) to
   * (nsnull, 0) for aFrame and its descendants.
   *
   * @param aFrame               The frame which itself and its descendants will
   *                             be initialized
   * @param aContinuationStates  A map from nsIFrame* to nsFrameContinuationState
   */
  static void InitContinuationStates(nsIFrame*              aFrame,
                                     nsContinuationStates*  aContinuationStates);

  /*
   * Determine if aFrame is leftmost or rightmost, and set aIsLeftMost and
   * aIsRightMost values. Also set continuation states of aContinuationStates.
   *
   * A frame is leftmost if it's the first appearance of its continuation chain
   * on the line and the chain is on its first line if it's LTR or the chain is
   * on its last line if it's RTL.
   * A frame is rightmost if it's the last appearance of its continuation chain
   * on the line and the chain is on its first line if it's RTL or the chain is
   * on its last line if it's LTR.
   *
   * @param aContinuationStates  A map from nsIFrame* to nsFrameContinuationState
   * @param[out] aIsLeftMost     TRUE means aFrame is leftmost frame or continuation
   * @param[out] aIsRightMost    TRUE means aFrame is rightmost frame or continuation
   */
   static void IsLeftOrRightMost(nsIFrame*              aFrame,
                                 nsContinuationStates*  aContinuationStates,
                                 bool&                aIsLeftMost /* out */,
                                 bool&                aIsRightMost /* out */);

  /**
   *  Adjust frame positions following their visual order
   *
   *  @param aFirstChild the first kid
   *
   *  @lina 04/11/2000
   */
  static void RepositionInlineFrames(BidiLineData* aBld,
                                     nsIFrame* aFirstChild);
  
  /**
   * Helper method for Resolve()
   * Truncate a text frame to the end of a single-directional run and possibly
   * create a continuation frame for the remainder of its content.
   *
   * @param aFrame       the original frame
   * @param aNewFrame    [OUT] the new frame that was created
   * @param aFrameIndex  [IN/OUT] index of aFrame in mLogicalFrames
   * @param aStart       [IN] the start of the content mapped by aFrame (and 
   *                          any fluid continuations)
   * @param aEnd         [IN] the offset of the end of the single-directional
   *                          text run.
   * @see Resolve()
   * @see RemoveBidiContinuation()
   */
  static inline
  nsresult EnsureBidiContinuation(nsIFrame*       aFrame,
                                  nsIFrame**      aNewFrame,
                                  PRInt32&        aFrameIndex,
                                  PRInt32         aStart,
                                  PRInt32         aEnd);

  /**
   * Helper method for Resolve()
   * Convert one or more bidi continuation frames created in a previous reflow by
   * EnsureBidiContinuation() into fluid continuations.
   * @param aFrame       the frame whose continuations are to be removed
   * @param aFirstIndex  index of aFrame in mLogicalFrames
   * @param aLastIndex   index of the last frame to be removed
   * @param aOffset      [OUT] count of directional frames removed. Since
   *                     directional frames have control characters
   *                     corresponding to them in mBuffer, the pointers to
   *                     mBuffer in Resolve() will need to be updated after
   *                     deleting the frames.
   *
   * @see Resolve()
   * @see EnsureBidiContinuation()
   */
  static void RemoveBidiContinuation(BidiParagraphData* aBpd,
                                     nsIFrame*          aFrame,
                                     PRInt32            aFirstIndex,
                                     PRInt32            aLastIndex,
                                     PRInt32&           aOffset);
  static void CalculateCharType(nsBidi*          aBidiEngine,
                                const PRUnichar* aText,
                                PRInt32&         aOffset,
                                PRInt32          aCharTypeLimit,
                                PRInt32&         aRunLimit,
                                PRInt32&         aRunLength,
                                PRInt32&         aRunCount,
                                PRUint8&         aCharType,
                                PRUint8&         aPrevCharType);
  
  static void StripBidiControlCharacters(PRUnichar* aText,
                                         PRInt32&   aTextLength);

  static bool WriteLogicalToVisual(const PRUnichar* aSrc,
                                     PRUint32 aSrcLength,
                                     PRUnichar* aDest,
                                     nsBidiLevel aBaseDirection,
                                     nsBidi* aBidiEngine);

  static void WriteReverse(const PRUnichar* aSrc,
                           PRUint32 aSrcLength,
                           PRUnichar* aDest);
};

#endif /* nsBidiPresUtils_h___ */

#endif // IBMBIDI
