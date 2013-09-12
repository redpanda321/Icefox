/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsBindingManager_h_
#define nsBindingManager_h_

#include "nsStubMutationObserver.h"
#include "pldhash.h"
#include "nsInterfaceHashtable.h"
#include "nsRefPtrHashtable.h"
#include "nsURIHashKey.h"
#include "nsCycleCollectionParticipant.h"
#include "nsXBLBinding.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"

struct ElementDependentRuleProcessorData;
class nsIContent;
class nsIXPConnectWrappedJS;
class nsIAtom;
class nsIDOMNodeList;
class nsIDocument;
class nsIURI;
class nsXBLDocumentInfo;
class nsIStreamListener;
class nsStyleSet;
class nsXBLBinding;
template<class E> class nsRefPtr;
typedef nsTArray<nsRefPtr<nsXBLBinding> > nsBindingList;
class nsIPrincipal;

class nsBindingManager MOZ_FINAL : public nsStubMutationObserver
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED

  nsBindingManager(nsIDocument* aDocument);
  ~nsBindingManager();

  nsXBLBinding* GetBinding(nsIContent* aContent);
  nsresult SetBinding(nsIContent* aContent, nsXBLBinding* aBinding);

  nsIContent* GetInsertionParent(nsIContent* aContent);
  nsresult SetInsertionParent(nsIContent* aContent, nsIContent* aResult);

  /**
   * Notify the binding manager that an element
   * has been removed from its document,
   * so that it can update any bindings or
   * nsIAnonymousContentCreator-created anonymous
   * content that may depend on the document.
   * @param aContent the element that's being moved
   * @param aOldDocument the old document in which the
   *   content resided.
   */
  void RemovedFromDocument(nsIContent* aContent, nsIDocument* aOldDocument)
  {
    if (aContent->HasFlag(NODE_MAY_BE_IN_BINDING_MNGR)) {
      RemovedFromDocumentInternal(aContent, aOldDocument,
                                  aContent->GetBindingParent());
    }
  }
  void RemovedFromDocumentInternal(nsIContent* aContent,
                                   nsIDocument* aOldDocument,
                                   nsIContent* aContentBindingParent);

  nsIAtom* ResolveTag(nsIContent* aContent, int32_t* aNameSpaceID);

  /**
   * Return a list of all explicit children, including any children
   * that may have been inserted via XBL insertion points.
   */
  nsresult GetContentListFor(nsIContent* aContent, nsIDOMNodeList** aResult);

  /**
   * Non-COMy version of GetContentListFor.
   */
  nsINodeList* GetContentListFor(nsIContent* aContent);

  /**
   * Set the insertion point children for the specified element.
   * The binding manager assumes ownership of aList.
   */
  nsresult SetContentListFor(nsIContent* aContent,
                             nsInsertionPointList* aList);

  /**
   * Determine whether or not the explicit child list has been altered
   * by XBL insertion points.
   */
  bool HasContentListFor(nsIContent* aContent);

  /**
   * Return the nodelist of "anonymous" kids for this node.  This might
   * actually include some of the nodes actual DOM kids, if there are
   * <children> tags directly as kids of <content>.  This will only end up
   * returning a non-null list for nodes which have a binding attached.
   */
  nsresult GetAnonymousNodesFor(nsIContent* aContent, nsIDOMNodeList** aResult);

  /**
   * Same as above, but without the XPCOM goop
   */
  nsINodeList* GetAnonymousNodesFor(nsIContent* aContent);

  /**
   * Set the anonymous child content for the specified element.
   * The binding manager assumes ownership of aList.
   */
  nsresult SetAnonymousNodesFor(nsIContent* aContent,
                                nsInsertionPointList* aList);

  /**
   * Retrieves the anonymous list of children if the element has one;
   * otherwise, retrieves the list of explicit children. N.B. that if
   * the explicit child list has not been altered by XBL insertion
   * points, then aResult will be null.
   */
  nsresult GetXBLChildNodesFor(nsIContent* aContent, nsIDOMNodeList** aResult);

  /**
   * Non-COMy version of GetXBLChildNodesFor
   */
  nsINodeList* GetXBLChildNodesFor(nsIContent* aContent);

  /**
   * Given a parent element and a child content, determine where the
   * child content should be inserted in the parent element's
   * anonymous content tree. Specifically, aChild should be inserted
   * beneath aResult at the index specified by aIndex.
   */
  // XXXbz That's false.  The aIndex doesn't seem to accurately reflect
  // anything resembling reality in terms of inserting content.  It's really
  // only used to tell apart two different insertion points with the same
  // insertion parent when managing our internal data structures.  We really
  // shouldn't be handing it out in our public API, since it's not useful to
  // anyone.
  nsIContent* GetInsertionPoint(nsIContent* aParent,
                                const nsIContent* aChild, uint32_t* aIndex);

  /**
   * Return the unfiltered insertion point for the specified parent
   * element. If other filtered insertion points exist,
   * aMultipleInsertionPoints will be set to true.
   */
  nsIContent* GetSingleInsertionPoint(nsIContent* aParent, uint32_t* aIndex,
                                      bool* aMultipleInsertionPoints);

  nsIContent* GetNestedInsertionPoint(nsIContent* aParent,
                                      const nsIContent* aChild);
  nsIContent* GetNestedSingleInsertionPoint(nsIContent* aParent,
                                            bool* aMultipleInsertionPoints);

  nsresult AddLayeredBinding(nsIContent* aContent, nsIURI* aURL,
                             nsIPrincipal* aOriginPrincipal);
  nsresult RemoveLayeredBinding(nsIContent* aContent, nsIURI* aURL);
  nsresult LoadBindingDocument(nsIDocument* aBoundDoc, nsIURI* aURL,
                               nsIPrincipal* aOriginPrincipal);

  nsresult AddToAttachedQueue(nsXBLBinding* aBinding);
  void ProcessAttachedQueue(uint32_t aSkipSize = 0);

  void ExecuteDetachedHandlers();

  nsresult PutXBLDocumentInfo(nsXBLDocumentInfo* aDocumentInfo);
  nsXBLDocumentInfo* GetXBLDocumentInfo(nsIURI* aURI);
  void RemoveXBLDocumentInfo(nsXBLDocumentInfo* aDocumentInfo);

  nsresult PutLoadingDocListener(nsIURI* aURL, nsIStreamListener* aListener);
  nsIStreamListener* GetLoadingDocListener(nsIURI* aURL);
  void RemoveLoadingDocListener(nsIURI* aURL);

  void FlushSkinBindings();

  nsresult GetBindingImplementation(nsIContent* aContent, REFNSIID aIID, void** aResult);

  // Style rule methods
  nsresult WalkRules(nsIStyleRuleProcessor::EnumFunc aFunc,
                     ElementDependentRuleProcessorData* aData,
                     bool* aCutOffInheritance);

  void WalkAllRules(nsIStyleRuleProcessor::EnumFunc aFunc,
                    ElementDependentRuleProcessorData* aData);
  /**
   * Do any processing that needs to happen as a result of a change in
   * the characteristics of the medium, and return whether this rule
   * processor's rules have changed (e.g., because of media queries).
   */
  nsresult MediumFeaturesChanged(nsPresContext* aPresContext,
                                 bool* aRulesChanged);

  void AppendAllSheets(nsTArray<nsCSSStyleSheet*>& aArray);

  NS_HIDDEN_(void) Traverse(nsIContent *aContent,
                            nsCycleCollectionTraversalCallback &cb);

  NS_DECL_CYCLE_COLLECTION_CLASS(nsBindingManager)

  // Notify the binding manager when an outermost update begins and
  // ends.  The end method can execute script.
  void BeginOutermostUpdate();
  void EndOutermostUpdate();

  // Called when the document is going away
  void DropDocumentReference();

protected:
  nsIXPConnectWrappedJS* GetWrappedJS(nsIContent* aContent);
  nsresult SetWrappedJS(nsIContent* aContent, nsIXPConnectWrappedJS* aResult);

  nsINodeList* GetXBLChildNodesInternal(nsIContent* aContent,
                                        bool* aIsAnonymousContentList);
  nsINodeList* GetAnonymousNodesInternal(nsIContent* aContent,
                                         bool* aIsAnonymousContentList);

  // Called by ContentAppended and ContentInserted to handle a single child
  // insertion.  aChild must not be null.  aContainer may be null.
  // aIndexInContainer is the index of the child in the parent.  aAppend is
  // true if this child is being appended, not inserted.
  void HandleChildInsertion(nsIContent* aContainer, nsIContent* aChild,
                            uint32_t aIndexInContainer, bool aAppend);

  // For the given container under which a child is being added, given
  // insertion parent and given index of the child being inserted, find the
  // right nsXBLInsertionPoint and the right index in that insertion point to
  // insert it at.  If null is returned, aInsertionIndex might be garbage.
  // aAppend controls what should be returned as the aInsertionIndex if the
  // right index can't be found.  If true, the length of the insertion point
  // will be returned; otherwise 0 will be returned.
  nsXBLInsertionPoint* FindInsertionPointAndIndex(nsIContent* aContainer,
                                                  nsIContent* aInsertionParent,
                                                  uint32_t aIndexInContainer,
                                                  int32_t aAppend,
                                                  int32_t* aInsertionIndex);

  // Same as ProcessAttachedQueue, but also nulls out
  // mProcessAttachedQueueEvent
  void DoProcessAttachedQueue();

  // Post an event to process the attached queue.
  void PostProcessAttachedQueueEvent();

// MEMBER VARIABLES
protected: 
  void RemoveInsertionParent(nsIContent* aParent);
  // A mapping from nsIContent* to the nsXBLBinding* that is
  // installed on that element.
  nsRefPtrHashtable<nsISupportsHashKey,nsXBLBinding> mBindingTable;

  // A mapping from nsIContent* to an nsAnonymousContentList*.  This
  // list contains an accurate reflection of our *explicit* children
  // (once intermingled with insertion points) in the altered DOM.
  // There is an entry for a content node in this table only if that
  // content node has some <children> kids.
  PLDHashTable mContentListTable;

  // A mapping from nsIContent* to an nsAnonymousContentList*.  This
  // list contains an accurate reflection of our *anonymous* children
  // (if and only if they are intermingled with insertion points) in
  // the altered DOM.  This table is not used if no insertion points
  // were defined directly underneath a <content> tag in a binding.
  // The NodeList from the <content> is used instead as a performance
  // optimization.  There is an entry for a content node in this table
  // only if that content node has a binding with a <content> attached
  // and this <content> contains <children> elements directly.
  PLDHashTable mAnonymousNodesTable;

  // A mapping from nsIContent* to nsIContent*.  The insertion parent
  // is our one true parent in the transformed DOM.  This gives us a
  // more-or-less O(1) way of obtaining our transformed parent.
  PLDHashTable mInsertionParentTable;

  // A mapping from nsIContent* to nsIXPWrappedJS* (an XPConnect
  // wrapper for JS objects).  For XBL bindings that implement XPIDL
  // interfaces, and that get referred to from C++, this table caches
  // the XPConnect wrapper for the binding.  By caching it, I control
  // its lifetime, and I prevent a re-wrap of the same script object
  // (in the case where multiple bindings in an XBL inheritance chain
  // both implement an XPIDL interface).
  PLDHashTable mWrapperTable;

  // A mapping from a URL (a string) to nsXBLDocumentInfo*.  This table
  // is the cache of all binding documents that have been loaded by a
  // given bound document.
  nsRefPtrHashtable<nsURIHashKey,nsXBLDocumentInfo> mDocumentTable;

  // A mapping from a URL (a string) to a nsIStreamListener. This
  // table is the currently loading binding docs.  If they're in this
  // table, they have not yet finished loading.
  nsInterfaceHashtable<nsURIHashKey,nsIStreamListener> mLoadingDocTable;

  // A queue of binding attached event handlers that are awaiting execution.
  nsBindingList mAttachedStack;
  bool mProcessingAttachedStack;
  bool mDestroyed;
  uint32_t mAttachedStackSizeOnOutermost;

  // Our posted event to process the attached queue, if any
  friend class nsRunnableMethod<nsBindingManager>;
  nsRefPtr< nsRunnableMethod<nsBindingManager> > mProcessAttachedQueueEvent;

  // Our document.  This is a weak ref; the document owns us
  nsIDocument* mDocument; 
};

#endif
