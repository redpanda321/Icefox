/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for all element classes as well as nsDocumentFragment.  This
 * provides an implementation of nsIDOMNode, implements nsIContent, provides
 * utility methods for subclasses, and so forth.
 */

#ifndef FragmentOrElement_h___
#define FragmentOrElement_h___

#include "nsAttrAndChildArray.h"          // member
#include "nsCOMPtr.h"                     // member
#include "nsCycleCollectionParticipant.h" // NS_DECL_CYCLE_*
#include "nsIContent.h"                   // base class
#include "nsIDOMTouchEvent.h"             // base class (nsITouchEventReceiver)
#include "nsIDOMXPathNSResolver.h"        // base class
#include "nsIInlineEventHandlers.h"       // base class
#include "nsINodeList.h"                  // base class
#include "nsIWeakReference.h"             // base class
#include "nsNodeUtils.h"                  // class member nsNodeUtils::CloneNodeImpl

class ContentUnbinder;
class nsContentList;
class nsDOMAttributeMap;
class nsDOMTokenList;
class nsIControllers;
class nsICSSDeclaration;
class nsIDocument;
class nsDOMStringMap;
class nsIDOMNamedNodeMap;
class nsIHTMLCollection;
class nsINodeInfo;
class nsIURI;

/**
 * Class that implements the nsIDOMNodeList interface (a list of children of
 * the content), by holding a reference to the content and delegating GetLength
 * and Item to its existing child list.
 * @see nsIDOMNodeList
 */
class nsChildContentList MOZ_FINAL : public nsINodeList
{
public:
  nsChildContentList(nsINode* aNode)
    : mNode(aNode)
  {
    SetIsDOMBinding();
  }

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS(nsChildContentList)

  // nsWrapperCache
  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope,
                               bool *triedToWrap);

  // nsIDOMNodeList interface
  NS_DECL_NSIDOMNODELIST

  // nsINodeList interface
  virtual int32_t IndexOf(nsIContent* aContent);
  virtual nsIContent* Item(uint32_t aIndex);

  void DropReference()
  {
    mNode = nullptr;
  }

  virtual nsINode* GetParentObject()
  {
    return mNode;
  }

private:
  // The node whose children make up the list (weak reference)
  nsINode* mNode;
};

/**
 * A tearoff class for FragmentOrElement to implement additional interfaces
 */
class nsNode3Tearoff : public nsIDOMXPathNSResolver
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_DECL_CYCLE_COLLECTION_CLASS(nsNode3Tearoff)

  NS_DECL_NSIDOMXPATHNSRESOLVER

  nsNode3Tearoff(nsINode *aNode) : mNode(aNode)
  {
  }

protected:
  virtual ~nsNode3Tearoff() {}

private:
  nsCOMPtr<nsINode> mNode;
};

/**
 * A class that implements nsIWeakReference
 */

class nsNodeWeakReference MOZ_FINAL : public nsIWeakReference
{
public:
  nsNodeWeakReference(nsINode* aNode)
    : mNode(aNode)
  {
  }

  ~nsNodeWeakReference();

  // nsISupports
  NS_DECL_ISUPPORTS

  // nsIWeakReference
  NS_DECL_NSIWEAKREFERENCE

  void NoticeNodeDestruction()
  {
    mNode = nullptr;
  }

private:
  nsINode* mNode;
};

/**
 * Tearoff to use for nodes to implement nsISupportsWeakReference
 */
class nsNodeSupportsWeakRefTearoff MOZ_FINAL : public nsISupportsWeakReference
{
public:
  nsNodeSupportsWeakRefTearoff(nsINode* aNode)
    : mNode(aNode)
  {
  }

  // nsISupports
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  // nsISupportsWeakReference
  NS_DECL_NSISUPPORTSWEAKREFERENCE

  NS_DECL_CYCLE_COLLECTION_CLASS(nsNodeSupportsWeakRefTearoff)

private:
  nsCOMPtr<nsINode> mNode;
};

// Forward declare to allow being a friend
class nsTouchEventReceiverTearoff;
class nsInlineEventHandlersTearoff;

/**
 * A generic base class for DOM elements, implementing many nsIContent,
 * nsIDOMNode and nsIDOMElement methods.
 */
namespace mozilla {
namespace dom {

class FragmentOrElement : public nsIContent
{
public:
  FragmentOrElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual ~FragmentOrElement();

  friend class ::nsTouchEventReceiverTearoff;
  friend class ::nsInlineEventHandlersTearoff;

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_DECL_SIZEOF_EXCLUDING_THIS

  /**
   * Called during QueryInterface to give the binding manager a chance to
   * get an interface for this element.
   */
  nsresult PostQueryInterface(REFNSIID aIID, void** aInstancePtr);

  // nsINode interface methods
  virtual uint32_t GetChildCount() const;
  virtual nsIContent *GetChildAt(uint32_t aIndex) const;
  virtual nsIContent * const * GetChildArray(uint32_t* aChildCount) const;
  virtual int32_t IndexOf(const nsINode* aPossibleChild) const MOZ_OVERRIDE;
  virtual nsresult InsertChildAt(nsIContent* aKid, uint32_t aIndex,
                                 bool aNotify);
  virtual void RemoveChildAt(uint32_t aIndex, bool aNotify);
  virtual void GetTextContentInternal(nsAString& aTextContent);
  virtual void SetTextContentInternal(const nsAString& aTextContent,
                                      mozilla::ErrorResult& aError);

  // nsIContent interface methods
  virtual already_AddRefed<nsINodeList> GetChildren(uint32_t aFilter);
  virtual const nsTextFragment *GetText();
  virtual uint32_t TextLength() const;
  virtual nsresult SetText(const PRUnichar* aBuffer, uint32_t aLength,
                           bool aNotify);
  // Need to implement this here too to avoid hiding.
  nsresult SetText(const nsAString& aStr, bool aNotify)
  {
    return SetText(aStr.BeginReading(), aStr.Length(), aNotify);
  }
  virtual nsresult AppendText(const PRUnichar* aBuffer, uint32_t aLength,
                              bool aNotify);
  virtual bool TextIsOnlyWhitespace();
  virtual void AppendTextTo(nsAString& aResult);
  virtual nsIContent *GetBindingParent() const;
  virtual bool IsLink(nsIURI** aURI) const;

  virtual void DestroyContent();
  virtual void SaveSubtreeState();

  virtual const nsAttrValue* DoGetClasses() const;
  NS_IMETHOD WalkContentStyleRules(nsRuleWalker* aRuleWalker);

  nsIHTMLCollection* Children();

public:
  /**
   * If there are listeners for DOMNodeInserted event, fires the event on all
   * aNodes
   */
  static void FireNodeInserted(nsIDocument* aDoc,
                               nsINode* aParent,
                               nsTArray<nsCOMPtr<nsIContent> >& aNodes);

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS(FragmentOrElement)

  /**
   * Fire a DOMNodeRemoved mutation event for all children of this node
   */
  void FireNodeRemovedForChildren();

  virtual bool OwnedOnlyByTheDOMTree()
  {
    uint32_t rc = mRefCnt.get();
    if (GetParent()) {
      --rc;
    }
    rc -= mAttrsAndChildren.ChildCount();
    return rc == 0;
  }

  virtual bool IsPurple()
  {
    return mRefCnt.IsPurple();
  }

  virtual void RemovePurple()
  {
    mRefCnt.RemovePurple();
  }

  static void ClearContentUnbinder();
  static bool CanSkip(nsINode* aNode, bool aRemovingAllowed);
  static bool CanSkipInCC(nsINode* aNode);
  static bool CanSkipThis(nsINode* aNode);
  static void MarkNodeChildren(nsINode* aNode);
  static void InitCCCallbacks();
  static void MarkUserData(void* aObject, nsIAtom* aKey, void* aChild,
                           void *aData);
  static void MarkUserDataHandler(void* aObject, nsIAtom* aKey, void* aChild,
                                  void* aData);

protected:
  /**
   * Copy attributes and state to another element
   * @param aDest the object to copy to
   */
  nsresult CopyInnerTo(FragmentOrElement* aDest);

public:
  // Because of a bug in MS C++ compiler nsDOMSlots must be declared public,
  // otherwise nsXULElement::nsXULSlots doesn't compile.
  /**
   * There are a set of DOM- and scripting-specific instance variables
   * that may only be instantiated when a content object is accessed
   * through the DOM. Rather than burn actual slots in the content
   * objects for each of these instance variables, we put them off
   * in a side structure that's only allocated when the content is
   * accessed through the DOM.
   */
  class nsDOMSlots : public nsINode::nsSlots
  {
  public:
    nsDOMSlots();
    virtual ~nsDOMSlots();

    void Traverse(nsCycleCollectionTraversalCallback &cb, bool aIsXUL);
    void Unlink(bool aIsXUL);

    size_t SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf) const;

    /**
     * The .style attribute (an interface that forwards to the actual
     * style rules)
     * @see nsGenericHTMLElement::GetStyle
     */
    nsCOMPtr<nsICSSDeclaration> mStyle;

    /**
     * The .dataset attribute.
     * @see nsGenericHTMLElement::GetDataset
     */
    nsDOMStringMap* mDataset; // [Weak]

    /**
     * SMIL Overridde style rules (for SMIL animation of CSS properties)
     * @see nsIContent::GetSMILOverrideStyle
     */
    nsCOMPtr<nsICSSDeclaration> mSMILOverrideStyle;

    /**
     * Holds any SMIL override style rules for this element.
     */
    nsRefPtr<mozilla::css::StyleRule> mSMILOverrideStyleRule;

    /**
     * An object implementing nsIDOMNamedNodeMap for this content (attributes)
     * @see FragmentOrElement::GetAttributes
     */
    nsRefPtr<nsDOMAttributeMap> mAttributeMap;

    union {
      /**
      * The nearest enclosing content node with a binding that created us.
      * @see FragmentOrElement::GetBindingParent
      */
      nsIContent* mBindingParent;  // [Weak]

      /**
      * The controllers of the XUL Element.
      */
      nsIControllers* mControllers; // [OWNER]
    };

    /**
     * An object implementing the .children property for this element.
     */
    nsRefPtr<nsContentList> mChildrenList;

    /**
     * An object implementing the .classList property for this element.
     */
    nsRefPtr<nsDOMTokenList> mClassList;
  };

protected:
  // Override from nsINode
  virtual nsINode::nsSlots* CreateSlots();

  nsDOMSlots *DOMSlots()
  {
    return static_cast<nsDOMSlots*>(Slots());
  }

  nsDOMSlots *GetExistingDOMSlots() const
  {
    return static_cast<nsDOMSlots*>(GetExistingSlots());
  }

  friend class ::ContentUnbinder;
  /**
   * Array containing all attributes and children for this element
   */
  nsAttrAndChildArray mAttrsAndChildren;
};

} // namespace dom
} // namespace mozilla

/**
 * Tearoff class to implement nsITouchEventReceiver
 */
class nsTouchEventReceiverTearoff MOZ_FINAL : public nsITouchEventReceiver
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_FORWARD_NSITOUCHEVENTRECEIVER(mElement->)

  NS_DECL_CYCLE_COLLECTION_CLASS(nsTouchEventReceiverTearoff)

  nsTouchEventReceiverTearoff(mozilla::dom::FragmentOrElement *aElement) : mElement(aElement)
  {
  }

private:
  nsRefPtr<mozilla::dom::FragmentOrElement> mElement;
};

/**
 * Tearoff class to implement nsIInlineEventHandlers
 */
class nsInlineEventHandlersTearoff MOZ_FINAL : public nsIInlineEventHandlers
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_FORWARD_NSIINLINEEVENTHANDLERS(mElement->)

  NS_DECL_CYCLE_COLLECTION_CLASS(nsInlineEventHandlersTearoff)

  nsInlineEventHandlersTearoff(mozilla::dom::FragmentOrElement *aElement) : mElement(aElement)
  {
  }

private:
  nsRefPtr<mozilla::dom::FragmentOrElement> mElement;
};

#define NS_ELEMENT_INTERFACE_TABLE_TO_MAP_SEGUE                               \
    rv = FragmentOrElement::QueryInterface(aIID, aInstancePtr);                \
    if (NS_SUCCEEDED(rv))                                                     \
      return rv;                                                              \
                                                                              \
    NS_OFFSET_AND_INTERFACE_TABLE_TO_MAP_SEGUE

#define NS_ELEMENT_INTERFACE_MAP_END                                          \
    {                                                                         \
      return PostQueryInterface(aIID, aInstancePtr);                          \
    }                                                                         \
                                                                              \
    NS_ADDREF(foundInterface);                                                \
                                                                              \
    *aInstancePtr = foundInterface;                                           \
                                                                              \
    return NS_OK;                                                             \
  }

#endif /* FragmentOrElement_h___ */
