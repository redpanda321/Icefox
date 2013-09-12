/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsFrameList_h___
#define nsFrameList_h___

#include "nscore.h"
#include "nsTraceRefcnt.h"
#include <stdio.h> /* for FILE* */
#include "nsDebug.h"
#include "nsTArray.h"

class nsIFrame;
namespace mozilla {
namespace layout {
  class FrameChildList;
  enum FrameChildListID {
      // The individual concrete child lists.
      kPrincipalList                = 0x1,
      kPopupList                    = 0x2,
      kCaptionList                  = 0x4,
      kColGroupList                 = 0x8,
      kSelectPopupList              = 0x10,
      kAbsoluteList                 = 0x20,
      kFixedList                    = 0x40,
      kOverflowList                 = 0x80,
      kOverflowContainersList       = 0x100,
      kExcessOverflowContainersList = 0x200,
      kOverflowOutOfFlowList        = 0x400,
      kFloatList                    = 0x800,
      kBulletList                   = 0x1000,
      kPushedFloatsList             = 0x2000,
      // A special alias for kPrincipalList that suppress the reflow request that
      // is normally done when manipulating child lists.
      kNoReflowPrincipalList        = 0x4000
  };
}
}

// Uncomment this to enable expensive frame-list integrity checking
// #define DEBUG_FRAME_LIST

/**
 * A class for managing a list of frames.
 */
class nsFrameList {
public:
  nsFrameList() :
    mFirstChild(nullptr), mLastChild(nullptr)
  {
    MOZ_COUNT_CTOR(nsFrameList);
  }

  nsFrameList(nsIFrame* aFirstFrame, nsIFrame* aLastFrame) :
    mFirstChild(aFirstFrame), mLastChild(aLastFrame)
  {
    MOZ_COUNT_CTOR(nsFrameList);
    VerifyList();
  }

  nsFrameList(const nsFrameList& aOther) :
    mFirstChild(aOther.mFirstChild), mLastChild(aOther.mLastChild)
  {
    MOZ_COUNT_CTOR(nsFrameList);
  }

  ~nsFrameList() {
    MOZ_COUNT_DTOR(nsFrameList);
    // Don't destroy our frames here, so that we can have temporary nsFrameLists
  }

  /**
   * For each frame in this list: remove it from the list then call
   * Destroy() on it.
   */
  void DestroyFrames();

  /**
   * For each frame in this list: remove it from the list then call
   * DestroyFrom() on it.
   */
  void DestroyFramesFrom(nsIFrame* aDestructRoot);

  /**
   * For each frame in this list: remove it from the list then call
   * Destroy() on it. Finally <code>delete this</code>.
   * 
   */
  void Destroy();

  /**
   * For each frame in this list: remove it from the list then call
   * DestroyFrom() on it. Finally <code>delete this</code>.
   *
   */
  void DestroyFrom(nsIFrame* aDestructRoot);

  void Clear() { mFirstChild = mLastChild = nullptr; }

  void SetFrames(nsIFrame* aFrameList);

  void SetFrames(nsFrameList& aFrameList) {
    NS_PRECONDITION(!mFirstChild, "Losing frames");

    mFirstChild = aFrameList.FirstChild();
    mLastChild = aFrameList.LastChild();
    aFrameList.Clear();
  }

  class Slice;

  /**
   * Append aFrameList to this list.  If aParent is not null,
   * reparents the newly added frames.  Clears out aFrameList and
   * returns a list slice represening the newly-appended frames.
   */
  Slice AppendFrames(nsIFrame* aParent, nsFrameList& aFrameList) {
    return InsertFrames(aParent, LastChild(), aFrameList);
  }


  /**
   * Append aFrame to this list.  If aParent is not null,
   * reparents the newly added frame.
   */
  void AppendFrame(nsIFrame* aParent, nsIFrame* aFrame) {
    nsFrameList temp(aFrame, aFrame);
    AppendFrames(aParent, temp);
  }

  /**
   * Take aFrame out of the frame list. This also disconnects aFrame
   * from the sibling list. The frame must be non-null and present on
   * this list.
   */
  void RemoveFrame(nsIFrame* aFrame);

  /**
   * Take aFrame out of the frame list, if present. This also disconnects
   * aFrame from the sibling list. aFrame must be non-null but is not
   * required to be on the list.
   * @return true if aFrame was removed
   */
  bool RemoveFrameIfPresent(nsIFrame* aFrame);

  /**
   * Take the frames after aAfterFrame out of the frame list.  If
   * aAfterFrame is null, removes the entire list.
   * @param aAfterFrame a frame in this list, or null
   * @return the removed frames, if any
   */
  nsFrameList RemoveFramesAfter(nsIFrame* aAfterFrame);

  /**
   * Take the first frame (if any) out of the frame list.
   * @return the first child, or nullptr if the list is empty
   */
  nsIFrame* RemoveFirstChild();

  /**
   * Take aFrame out of the frame list and then destroy it.
   * The frame must be non-null and present on this list.
   */
  void DestroyFrame(nsIFrame* aFrame);

  /**
   * If aFrame is present on this list then take it out of the list and
   * then destroy it. The frame must be non-null.
   * @return true if the frame was found
   */
  bool DestroyFrameIfPresent(nsIFrame* aFrame);

  /**
   * Insert aFrame right after aPrevSibling, or prepend it to this
   * list if aPrevSibling is null. If aParent is not null, also
   * reparents newly-added frame. Note that this method always
   * sets the frame's nextSibling pointer.
   */
  void InsertFrame(nsIFrame* aParent, nsIFrame* aPrevSibling,
                   nsIFrame* aFrame) {
    nsFrameList temp(aFrame, aFrame);
    InsertFrames(aParent, aPrevSibling, temp);
  }


  /**
   * Inserts aFrameList into this list after aPrevSibling (at the beginning if
   * aPrevSibling is null).  If aParent is not null, reparents the newly added
   * frames.  Clears out aFrameList and returns a list slice representing the
   * newly-inserted frames.
   */
  Slice InsertFrames(nsIFrame* aParent, nsIFrame* aPrevSibling,
                     nsFrameList& aFrameList);

  class FrameLinkEnumerator;

  /**
   * Split this frame list such that all the frames before the link pointed to
   * by aLink end up in the returned list, while the remaining frames stay in
   * this list.  After this call, aLink points to the beginning of this list.
   */
  nsFrameList ExtractHead(FrameLinkEnumerator& aLink);

  /**
   * Split this frame list such that all the frames coming after the link
   * pointed to by aLink end up in the returned list, while the frames before
   * that link stay in this list.  After this call, aLink is at end.
   */
  nsFrameList ExtractTail(FrameLinkEnumerator& aLink);

  nsIFrame* FirstChild() const {
    return mFirstChild;
  }

  nsIFrame* LastChild() const {
    return mLastChild;
  }

  nsIFrame* FrameAt(int32_t aIndex) const;
  int32_t IndexOf(nsIFrame* aFrame) const;

  bool IsEmpty() const {
    return nullptr == mFirstChild;
  }

  bool NotEmpty() const {
    return nullptr != mFirstChild;
  }

  bool ContainsFrame(const nsIFrame* aFrame) const;

  int32_t GetLength() const;

  /**
   * If this frame list has only one frame, return that frame.
   * Otherwise, return null.
   */
  nsIFrame* OnlyChild() const {
    if (FirstChild() == LastChild()) {
      return FirstChild();
    }
    return nullptr;
  }

  /**
   * Call SetParent(aParent) for each frame in this list.
   * @param aParent the new parent frame, must be non-null
   */
  void ApplySetParent(nsIFrame* aParent) const;

  /**
   * If this frame list is non-empty then append it to aLists as the
   * aListID child list.
   * (this method is implemented in FrameChildList.h for dependency reasons)
   */
  inline void AppendIfNonempty(nsTArray<mozilla::layout::FrameChildList>* aLists,
                               mozilla::layout::FrameChildListID aListID) const;

#ifdef IBMBIDI
  /**
   * Return the frame before this frame in visual order (after Bidi reordering).
   * If aFrame is null, return the last frame in visual order.
   */
  nsIFrame* GetPrevVisualFor(nsIFrame* aFrame) const;

  /**
   * Return the frame after this frame in visual order (after Bidi reordering).
   * If aFrame is null, return the first frame in visual order.
   */
  nsIFrame* GetNextVisualFor(nsIFrame* aFrame) const;
#endif // IBMBIDI

#ifdef DEBUG
  void List(FILE* out) const;
#endif

  static void Init();
  static void Shutdown() { delete sEmptyList; }
  static const nsFrameList& EmptyList() { return *sEmptyList; }

  class Enumerator;

  /**
   * A class representing a slice of a frame list.
   */
  class Slice {
    friend class Enumerator;

  public:
    // Implicit on purpose, so that we can easily create enumerators from
    // nsFrameList via this impicit constructor.
    Slice(const nsFrameList& aList) :
#ifdef DEBUG
      mList(aList),
#endif
      mStart(aList.FirstChild()),
      mEnd(nullptr)
    {}

    Slice(const nsFrameList& aList, nsIFrame* aStart, nsIFrame* aEnd) :
#ifdef DEBUG
      mList(aList),
#endif
      mStart(aStart),
      mEnd(aEnd)
    {}

    Slice(const Slice& aOther) :
#ifdef DEBUG
      mList(aOther.mList),
#endif
      mStart(aOther.mStart),
      mEnd(aOther.mEnd)
    {}

  private:
#ifdef DEBUG
    const nsFrameList& mList;
#endif
    nsIFrame* const mStart; // our starting frame
    const nsIFrame* const mEnd; // The first frame that is NOT in the slice.
                                // May be null.
  };

  class Enumerator {
  public:
    Enumerator(const Slice& aSlice) :
#ifdef DEBUG
      mSlice(aSlice),
#endif
      mFrame(aSlice.mStart),
      mEnd(aSlice.mEnd)
    {}

    Enumerator(const Enumerator& aOther) :
#ifdef DEBUG
      mSlice(aOther.mSlice),
#endif
      mFrame(aOther.mFrame),
      mEnd(aOther.mEnd)
    {}

    bool AtEnd() const {
      // Can't just check mEnd, because some table code goes and destroys the
      // tail of the frame list (including mEnd!) while iterating over the
      // frame list.
      return !mFrame || mFrame == mEnd;
    }

    /* Next() needs to know about nsIFrame, and nsIFrame will need to
       know about nsFrameList methods, so in order to inline this put
       the implementation in nsIFrame.h */
    inline void Next();

    /**
     * Get the current frame we're pointing to.  Do not call this on an
     * iterator that is at end!
     */
    nsIFrame* get() const {
      NS_PRECONDITION(!AtEnd(), "Enumerator is at end");
      return mFrame;
    }

    /**
     * Get an enumerator that is just like this one, but not limited in terms of
     * the part of the list it will traverse.
     */
    Enumerator GetUnlimitedEnumerator() const {
      return Enumerator(*this, nullptr);
    }

#ifdef DEBUG
    const nsFrameList& List() const { return mSlice.mList; }
#endif

  protected:
    Enumerator(const Enumerator& aOther, const nsIFrame* const aNewEnd):
#ifdef DEBUG
      mSlice(aOther.mSlice),
#endif
      mFrame(aOther.mFrame),
      mEnd(aNewEnd)
    {}

#ifdef DEBUG
    /* Has to be an object, not a reference, since the slice could
       well be a temporary constructed from an nsFrameList */
    const Slice mSlice;
#endif
    nsIFrame* mFrame; // our current frame.
    const nsIFrame* const mEnd; // The first frame we should NOT enumerate.
                                // May be null.
  };

  /**
   * A class that can be used to enumerate links between frames.  When created
   * from an nsFrameList, it points to the "link" immediately before the first
   * frame.  It can then be advanced until it points to the "link" immediately
   * after the last frame.  At any position, PrevFrame() and NextFrame() are
   * the frames before and after the given link.  This means PrevFrame() is
   * null when the enumerator is at the beginning of the list and NextFrame()
   * is null when it's AtEnd().
   */
  class FrameLinkEnumerator : private Enumerator {
  public:
    friend class nsFrameList;

    FrameLinkEnumerator(const nsFrameList& aList) :
      Enumerator(aList),
      mPrev(nullptr)
    {}

    FrameLinkEnumerator(const FrameLinkEnumerator& aOther) :
      Enumerator(aOther),
      mPrev(aOther.mPrev)
    {}

    /* This constructor needs to know about nsIFrame, and nsIFrame will need to
       know about nsFrameList methods, so in order to inline this put
       the implementation in nsIFrame.h */
    inline FrameLinkEnumerator(const nsFrameList& aList, nsIFrame* aPrevFrame);

    void operator=(const FrameLinkEnumerator& aOther) {
      NS_PRECONDITION(&List() == &aOther.List(), "Different lists?");
      mFrame = aOther.mFrame;
      mPrev = aOther.mPrev;
    }

    void Next() {
      mPrev = mFrame;
      Enumerator::Next();
    }

    bool AtEnd() const { return Enumerator::AtEnd(); }

    nsIFrame* PrevFrame() const { return mPrev; }
    nsIFrame* NextFrame() const { return mFrame; }

  protected:
    nsIFrame* mPrev;
  };

private:
#ifdef DEBUG_FRAME_LIST
  void VerifyList() const;
#else
  void VerifyList() const {}
#endif

  static const nsFrameList* sEmptyList;

protected:
  nsIFrame* mFirstChild;
  nsIFrame* mLastChild;
};

#endif /* nsFrameList_h___ */
