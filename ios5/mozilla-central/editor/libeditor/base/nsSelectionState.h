/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __selectionstate_h__
#define __selectionstate_h__

#include "nsCOMPtr.h"
#include "nsIDOMNode.h"
#include "nsINode.h"
#include "nsTArray.h"
#include "nscore.h"
#include "prtypes.h"

class nsCycleCollectionTraversalCallback;
class nsIDOMCharacterData;
class nsIDOMRange;
class nsISelection;
class nsRange;

/***************************************************************************
 * class for recording selection info.  stores selection as collection of
 * { {startnode, startoffset} , {endnode, endoffset} } tuples.  Can't store
 * ranges since dom gravity will possibly change the ranges.
 */

// first a helper struct for saving/setting ranges
struct nsRangeStore 
{
  nsRangeStore();
  ~nsRangeStore();
  nsresult StoreRange(nsIDOMRange *aRange);
  nsresult GetRange(nsRange** outRange);

  NS_INLINE_DECL_REFCOUNTING(nsRangeStore)
        
  nsCOMPtr<nsIDOMNode> startNode;
  PRInt32              startOffset;
  nsCOMPtr<nsIDOMNode> endNode;
  PRInt32              endOffset;
  // DEBUG:   static PRInt32 n;
};

class nsSelectionState
{
  public:
      
    nsSelectionState();
    ~nsSelectionState();

    void DoTraverse(nsCycleCollectionTraversalCallback &cb);
    void DoUnlink() { MakeEmpty(); }
  
    nsresult SaveSelection(nsISelection *aSel);
    nsresult RestoreSelection(nsISelection *aSel);
    bool     IsCollapsed();
    bool     IsEqual(nsSelectionState *aSelState);
    void     MakeEmpty();
    bool     IsEmpty();
  protected:    
    nsTArray<nsRefPtr<nsRangeStore> > mArray;
    
    friend class nsRangeUpdater;
};

class nsRangeUpdater
{
  public:    
  
    nsRangeUpdater();
    ~nsRangeUpdater();
  
    void RegisterRangeItem(nsRangeStore *aRangeItem);
    void DropRangeItem(nsRangeStore *aRangeItem);
    nsresult RegisterSelectionState(nsSelectionState &aSelState);
    nsresult DropSelectionState(nsSelectionState &aSelState);
    
    // editor selection gravity routines.  Note that we can't always depend on
    // DOM Range gravity to do what we want to the "real" selection.  For instance,
    // if you move a node, that corresponds to deleting it and reinserting it.
    // DOM Range gravity will promote the selection out of the node on deletion,
    // which is not what you want if you know you are reinserting it.
    nsresult SelAdjCreateNode(nsIDOMNode *aParent, PRInt32 aPosition);
    nsresult SelAdjInsertNode(nsIDOMNode *aParent, PRInt32 aPosition);
    void     SelAdjDeleteNode(nsIDOMNode *aNode);
    nsresult SelAdjSplitNode(nsIDOMNode *aOldRightNode, PRInt32 aOffset, nsIDOMNode *aNewLeftNode);
    nsresult SelAdjJoinNodes(nsIDOMNode *aLeftNode, 
                             nsIDOMNode *aRightNode, 
                             nsIDOMNode *aParent, 
                             PRInt32 aOffset,
                             PRInt32 aOldLeftNodeLength);
    nsresult SelAdjInsertText(nsIDOMCharacterData *aTextNode, PRInt32 aOffset, const nsAString &aString);
    nsresult SelAdjDeleteText(nsIDOMCharacterData *aTextNode, PRInt32 aOffset, PRInt32 aLength);
    // the following gravity routines need will/did sandwiches, because the other gravity
    // routines will be called inside of these sandwiches, but should be ignored.
    nsresult WillReplaceContainer();
    nsresult DidReplaceContainer(nsIDOMNode *aOriginalNode, nsIDOMNode *aNewNode);
    nsresult WillRemoveContainer();
    nsresult DidRemoveContainer(nsIDOMNode *aNode, nsIDOMNode *aParent, PRInt32 aOffset, PRUint32 aNodeOrigLen);
    nsresult WillInsertContainer();
    nsresult DidInsertContainer();
    nsresult WillMoveNode();
    nsresult DidMoveNode(nsIDOMNode *aOldParent, PRInt32 aOldOffset, nsIDOMNode *aNewParent, PRInt32 aNewOffset);
  protected:    
    nsTArray<nsRefPtr<nsRangeStore> > mArray;
    bool mLock;
};


/***************************************************************************
 * helper class for using nsSelectionState.  stack based class for doing
 * preservation of dom points across editor actions
 */

class NS_STACK_CLASS nsAutoTrackDOMPoint
{
  private:
    nsRangeUpdater &mRU;
    nsCOMPtr<nsIDOMNode> *mNode;
    PRInt32 *mOffset;
    nsRefPtr<nsRangeStore> mRangeItem;
  public:
    nsAutoTrackDOMPoint(nsRangeUpdater &aRangeUpdater, nsCOMPtr<nsIDOMNode> *aNode, PRInt32 *aOffset) :
    mRU(aRangeUpdater)
    ,mNode(aNode)
    ,mOffset(aOffset)
    {
      mRangeItem = new nsRangeStore();
      mRangeItem->startNode = *mNode;
      mRangeItem->endNode = *mNode;
      mRangeItem->startOffset = *mOffset;
      mRangeItem->endOffset = *mOffset;
      mRU.RegisterRangeItem(mRangeItem);
    }
    
    ~nsAutoTrackDOMPoint()
    {
      mRU.DropRangeItem(mRangeItem);
      *mNode  = mRangeItem->startNode;
      *mOffset = mRangeItem->startOffset;
    }
};



/***************************************************************************
 * another helper class for nsSelectionState.  stack based class for doing
 * Will/DidReplaceContainer()
 */

class NS_STACK_CLASS nsAutoReplaceContainerSelNotify
{
  private:
    nsRangeUpdater &mRU;
    nsIDOMNode *mOriginalNode;
    nsIDOMNode *mNewNode;

  public:
    nsAutoReplaceContainerSelNotify(nsRangeUpdater &aRangeUpdater, nsIDOMNode *aOriginalNode, nsIDOMNode *aNewNode) :
    mRU(aRangeUpdater)
    ,mOriginalNode(aOriginalNode)
    ,mNewNode(aNewNode)
    {
      mRU.WillReplaceContainer();
    }
    
    ~nsAutoReplaceContainerSelNotify()
    {
      mRU.DidReplaceContainer(mOriginalNode, mNewNode);
    }
};


/***************************************************************************
 * another helper class for nsSelectionState.  stack based class for doing
 * Will/DidRemoveContainer()
 */

class NS_STACK_CLASS nsAutoRemoveContainerSelNotify
{
  private:
    nsRangeUpdater &mRU;
    nsIDOMNode *mNode;
    nsIDOMNode *mParent;
    PRInt32    mOffset;
    PRUint32   mNodeOrigLen;

  public:
    nsAutoRemoveContainerSelNotify(nsRangeUpdater& aRangeUpdater,
                                   nsINode* aNode,
                                   nsINode* aParent,
                                   PRInt32 aOffset,
                                   PRUint32 aNodeOrigLen)
      : mRU(aRangeUpdater)
      , mNode(aNode->AsDOMNode())
      , mParent(aParent->AsDOMNode())
      , mOffset(aOffset)
      , mNodeOrigLen(aNodeOrigLen)
    {
      mRU.WillRemoveContainer();
    }
    
    ~nsAutoRemoveContainerSelNotify()
    {
      mRU.DidRemoveContainer(mNode, mParent, mOffset, mNodeOrigLen);
    }
};

/***************************************************************************
 * another helper class for nsSelectionState.  stack based class for doing
 * Will/DidInsertContainer()
 */

class NS_STACK_CLASS nsAutoInsertContainerSelNotify
{
  private:
    nsRangeUpdater &mRU;

  public:
    nsAutoInsertContainerSelNotify(nsRangeUpdater &aRangeUpdater) :
    mRU(aRangeUpdater)
    {
      mRU.WillInsertContainer();
    }
    
    ~nsAutoInsertContainerSelNotify()
    {
      mRU.DidInsertContainer();
    }
};


/***************************************************************************
 * another helper class for nsSelectionState.  stack based class for doing
 * Will/DidMoveNode()
 */

class NS_STACK_CLASS nsAutoMoveNodeSelNotify
{
  private:
    nsRangeUpdater &mRU;
    nsIDOMNode *mOldParent;
    nsIDOMNode *mNewParent;
    PRInt32    mOldOffset;
    PRInt32    mNewOffset;

  public:
    nsAutoMoveNodeSelNotify(nsRangeUpdater &aRangeUpdater, 
                            nsIDOMNode *aOldParent, 
                            PRInt32 aOldOffset, 
                            nsIDOMNode *aNewParent, 
                            PRInt32 aNewOffset) :
    mRU(aRangeUpdater)
    ,mOldParent(aOldParent)
    ,mNewParent(aNewParent)
    ,mOldOffset(aOldOffset)
    ,mNewOffset(aNewOffset)
    {
      mRU.WillMoveNode();
    }
    
    ~nsAutoMoveNodeSelNotify()
    {
      mRU.DidMoveNode(mOldParent, mOldOffset, mNewParent, mNewOffset);
    }
};

#endif


