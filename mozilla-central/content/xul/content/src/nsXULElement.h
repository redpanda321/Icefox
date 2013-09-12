/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*

  The base XUL element class and associates.

*/

#ifndef nsXULElement_h__
#define nsXULElement_h__

// XXX because nsEventListenerManager has broken includes
#include "nsIDOMEvent.h"
#include "nsIServiceManager.h"
#include "nsIAtom.h"
#include "nsINodeInfo.h"
#include "nsIControllers.h"
#include "nsIDOMElement.h"
#include "nsIDOMEventTarget.h"
#include "nsIDOMXULElement.h"
#include "nsIDOMXULMultSelectCntrlEl.h"
#include "nsEventListenerManager.h"
#include "nsIRDFCompositeDataSource.h"
#include "nsIRDFResource.h"
#include "nsBindingManager.h"
#include "nsIURI.h"
#include "nsIXULTemplateBuilder.h"
#include "nsIBoxObject.h"
#include "nsLayoutCID.h"
#include "nsAttrAndChildArray.h"
#include "nsGkAtoms.h"
#include "nsAutoPtr.h"
#include "nsStyledElement.h"
#include "nsDOMScriptObjectHolder.h"
#include "nsIFrameLoader.h"
#include "jspubtd.h"

class nsIDocument;
class nsString;
class nsIDocShell;

class nsIObjectInputStream;
class nsIObjectOutputStream;
class nsIScriptGlobalObjectOwner;
class nsXULPrototypeNode;
typedef nsTArray<nsRefPtr<nsXULPrototypeNode> > nsPrototypeArray;

namespace mozilla {
namespace css {
class StyleRule;
}
}

////////////////////////////////////////////////////////////////////////

#ifdef XUL_PROTOTYPE_ATTRIBUTE_METERING
#define XUL_PROTOTYPE_ATTRIBUTE_METER(counter) (nsXULPrototypeAttribute::counter++)
#else
#define XUL_PROTOTYPE_ATTRIBUTE_METER(counter) ((void) 0)
#endif


/**

  A prototype attribute for an nsXULPrototypeElement.

 */

class nsXULPrototypeAttribute
{
public:
    nsXULPrototypeAttribute()
        : mName(nsGkAtoms::id)  // XXX this is a hack, but names have to have a value
    {
        XUL_PROTOTYPE_ATTRIBUTE_METER(gNumAttributes);
        MOZ_COUNT_CTOR(nsXULPrototypeAttribute);
    }

    ~nsXULPrototypeAttribute();

    nsAttrName mName;
    nsAttrValue mValue;

#ifdef XUL_PROTOTYPE_ATTRIBUTE_METERING
    static uint32_t   gNumElements;
    static uint32_t   gNumAttributes;
    static uint32_t   gNumCacheTests;
    static uint32_t   gNumCacheHits;
    static uint32_t   gNumCacheSets;
    static uint32_t   gNumCacheFills;
#endif /* !XUL_PROTOTYPE_ATTRIBUTE_METERING */
};


/**

  A prototype content model element that holds the "primordial" values
  that have been parsed from the original XUL document.

 */

class nsXULPrototypeNode
{
public:
    enum Type { eType_Element, eType_Script, eType_Text, eType_PI };

    Type                     mType;

    virtual ~nsXULPrototypeNode() {}
    virtual nsresult Serialize(nsIObjectOutputStream* aStream,
                               nsIScriptGlobalObject* aGlobal,
                               const nsCOMArray<nsINodeInfo> *aNodeInfos) = 0;
    virtual nsresult Deserialize(nsIObjectInputStream* aStream,
                                 nsIScriptGlobalObject* aGlobal,
                                 nsIURI* aDocumentURI,
                                 const nsCOMArray<nsINodeInfo> *aNodeInfos) = 0;

#ifdef NS_BUILD_REFCNT_LOGGING
    virtual const char* ClassName() = 0;
    virtual uint32_t ClassSize() = 0;
#endif

    /**
     * The prototype document must call ReleaseSubtree when it is going
     * away.  This makes the parents through the tree stop owning their
     * children, whether or not the parent's reference count is zero.
     * Individual elements may still own individual prototypes, but
     * those prototypes no longer remember their children to allow them
     * to be constructed.
     */
    virtual void ReleaseSubtree() { }

    NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(nsXULPrototypeNode)
    NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(nsXULPrototypeNode)

protected:
    nsXULPrototypeNode(Type aType)
        : mType(aType) {}
};

class nsXULPrototypeElement : public nsXULPrototypeNode
{
public:
    nsXULPrototypeElement()
        : nsXULPrototypeNode(eType_Element),
          mNumAttributes(0),
          mHasIdAttribute(false),
          mHasClassAttribute(false),
          mHasStyleAttribute(false),
          mAttributes(nullptr)
    {
    }

    virtual ~nsXULPrototypeElement()
    {
        Unlink();
    }

#ifdef NS_BUILD_REFCNT_LOGGING
    virtual const char* ClassName() { return "nsXULPrototypeElement"; }
    virtual uint32_t ClassSize() { return sizeof(*this); }
#endif

    virtual void ReleaseSubtree()
    {
        for (int32_t i = mChildren.Length() - 1; i >= 0; i--) {
            if (mChildren[i].get())
                mChildren[i]->ReleaseSubtree();
        }
        mChildren.Clear();
        nsXULPrototypeNode::ReleaseSubtree();
    }

    virtual nsresult Serialize(nsIObjectOutputStream* aStream,
                               nsIScriptGlobalObject* aGlobal,
                               const nsCOMArray<nsINodeInfo> *aNodeInfos);
    virtual nsresult Deserialize(nsIObjectInputStream* aStream,
                                 nsIScriptGlobalObject* aGlobal,
                                 nsIURI* aDocumentURI,
                                 const nsCOMArray<nsINodeInfo> *aNodeInfos);

    nsresult SetAttrAt(uint32_t aPos, const nsAString& aValue, nsIURI* aDocumentURI);

    void Unlink();

    nsPrototypeArray         mChildren;

    nsCOMPtr<nsINodeInfo>    mNodeInfo;           // [OWNER]

    uint32_t                 mNumAttributes:29;
    uint32_t                 mHasIdAttribute:1;
    uint32_t                 mHasClassAttribute:1;
    uint32_t                 mHasStyleAttribute:1;
    nsXULPrototypeAttribute* mAttributes;         // [OWNER]
};

class nsXULDocument;

class nsXULPrototypeScript : public nsXULPrototypeNode
{
public:
    nsXULPrototypeScript(uint32_t aLineNo, uint32_t version);
    virtual ~nsXULPrototypeScript();

#ifdef NS_BUILD_REFCNT_LOGGING
    virtual const char* ClassName() { return "nsXULPrototypeScript"; }
    virtual uint32_t ClassSize() { return sizeof(*this); }
#endif

    virtual nsresult Serialize(nsIObjectOutputStream* aStream,
                               nsIScriptGlobalObject* aGlobal,
                               const nsCOMArray<nsINodeInfo> *aNodeInfos);
    nsresult SerializeOutOfLine(nsIObjectOutputStream* aStream,
                                nsIScriptGlobalObject* aGlobal);
    virtual nsresult Deserialize(nsIObjectInputStream* aStream,
                                 nsIScriptGlobalObject* aGlobal,
                                 nsIURI* aDocumentURI,
                                 const nsCOMArray<nsINodeInfo> *aNodeInfos);
    nsresult DeserializeOutOfLine(nsIObjectInputStream* aInput,
                                  nsIScriptGlobalObject* aGlobal);

    nsresult Compile(const PRUnichar* aText, int32_t aTextLength,
                     nsIURI* aURI, uint32_t aLineNo,
                     nsIDocument* aDocument,
                     nsIScriptGlobalObjectOwner* aGlobalOwner);

    void UnlinkJSObjects();

    void Set(nsScriptObjectHolder<JSScript>& aHolder)
    {
        Set(aHolder.get());
    }
    void Set(JSScript* aObject);

    struct ScriptObjectHolder
    {
        ScriptObjectHolder() : mObject(nullptr)
        {
        }
        JSScript* mObject;
    };

    nsCOMPtr<nsIURI>         mSrcURI;
    uint32_t                 mLineNo;
    bool                     mSrcLoading;
    bool                     mOutOfLine;
    nsXULDocument*           mSrcLoadWaiters;   // [OWNER] but not COMPtr
    uint32_t                 mLangVersion;
    ScriptObjectHolder       mScriptObject;
};

class nsXULPrototypeText : public nsXULPrototypeNode
{
public:
    nsXULPrototypeText()
        : nsXULPrototypeNode(eType_Text)
    {
    }

    virtual ~nsXULPrototypeText()
    {
    }

#ifdef NS_BUILD_REFCNT_LOGGING
    virtual const char* ClassName() { return "nsXULPrototypeText"; }
    virtual uint32_t ClassSize() { return sizeof(*this); }
#endif

    virtual nsresult Serialize(nsIObjectOutputStream* aStream,
                               nsIScriptGlobalObject* aGlobal,
                               const nsCOMArray<nsINodeInfo> *aNodeInfos);
    virtual nsresult Deserialize(nsIObjectInputStream* aStream,
                                 nsIScriptGlobalObject* aGlobal,
                                 nsIURI* aDocumentURI,
                                 const nsCOMArray<nsINodeInfo> *aNodeInfos);

    nsString                 mValue;
};

class nsXULPrototypePI : public nsXULPrototypeNode
{
public:
    nsXULPrototypePI()
        : nsXULPrototypeNode(eType_PI)
    {
    }

    virtual ~nsXULPrototypePI()
    {
    }

#ifdef NS_BUILD_REFCNT_LOGGING
    virtual const char* ClassName() { return "nsXULPrototypePI"; }
    virtual uint32_t ClassSize() { return sizeof(*this); }
#endif

    virtual nsresult Serialize(nsIObjectOutputStream* aStream,
                               nsIScriptGlobalObject* aGlobal,
                               const nsCOMArray<nsINodeInfo> *aNodeInfos);
    virtual nsresult Deserialize(nsIObjectInputStream* aStream,
                                 nsIScriptGlobalObject* aGlobal,
                                 nsIURI* aDocumentURI,
                                 const nsCOMArray<nsINodeInfo> *aNodeInfos);

    nsString                 mTarget;
    nsString                 mData;
};

////////////////////////////////////////////////////////////////////////

/**

  The XUL element.

 */

#define XUL_ELEMENT_FLAG_BIT(n_) NODE_FLAG_BIT(ELEMENT_TYPE_SPECIFIC_BITS_OFFSET + (n_))

// XUL element specific bits
enum {
  XUL_ELEMENT_TEMPLATE_GENERATED =        XUL_ELEMENT_FLAG_BIT(0),
  XUL_ELEMENT_HAS_CONTENTMENU_LISTENER =  XUL_ELEMENT_FLAG_BIT(1),
  XUL_ELEMENT_HAS_POPUP_LISTENER =        XUL_ELEMENT_FLAG_BIT(2)
};

// Make sure we have space for our bits
PR_STATIC_ASSERT((ELEMENT_TYPE_SPECIFIC_BITS_OFFSET + 2) < 32);

#undef XUL_ELEMENT_FLAG_BIT

class nsScriptEventHandlerOwnerTearoff;

class nsXULElement : public nsStyledElement,
                     public nsIDOMXULElement
{
public:
    nsXULElement(already_AddRefed<nsINodeInfo> aNodeInfo);

    static nsresult
    Create(nsXULPrototypeElement* aPrototype, nsIDocument* aDocument,
           bool aIsScriptable, mozilla::dom::Element** aResult);

    NS_IMPL_FROMCONTENT(nsXULElement, kNameSpaceID_XUL)

    // nsISupports
    NS_DECL_ISUPPORTS_INHERITED
    NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED_NO_UNLINK(nsXULElement,
                                                       mozilla::dom::Element)

    // nsINode
    virtual nsresult PreHandleEvent(nsEventChainPreVisitor& aVisitor);

    // nsIContent
    virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                                nsIContent* aBindingParent,
                                bool aCompileEventHandlers);
    virtual void UnbindFromTree(bool aDeep, bool aNullParent);
    virtual void RemoveChildAt(uint32_t aIndex, bool aNotify);
    virtual void DestroyContent();

#ifdef DEBUG
    virtual void List(FILE* out, int32_t aIndent) const;
    virtual void DumpContent(FILE* out, int32_t aIndent,bool aDumpAll) const
    {
    }
#endif

    virtual void PerformAccesskey(bool aKeyCausesActivation,
                                  bool aIsTrustedEvent);
    nsresult ClickWithInputSource(uint16_t aInputSource);

    virtual nsIContent *GetBindingParent() const;
    virtual bool IsNodeOfType(uint32_t aFlags) const;
    virtual bool IsFocusable(int32_t *aTabIndex = nullptr, bool aWithMouse = false);

    NS_IMETHOD WalkContentStyleRules(nsRuleWalker* aRuleWalker);
    virtual nsChangeHint GetAttributeChangeHint(const nsIAtom* aAttribute,
                                                int32_t aModType) const;
    NS_IMETHOD_(bool) IsAttributeMapped(const nsIAtom* aAttribute) const;

    // XUL element methods
    /**
     * The template-generated flag is used to indicate that a
     * template-generated element has already had its children generated.
     */
    void SetTemplateGenerated() { SetFlags(XUL_ELEMENT_TEMPLATE_GENERATED); }
    void ClearTemplateGenerated() { UnsetFlags(XUL_ELEMENT_TEMPLATE_GENERATED); }
    bool GetTemplateGenerated() { return HasFlag(XUL_ELEMENT_TEMPLATE_GENERATED); }

    // nsIDOMNode
    NS_FORWARD_NSIDOMNODE_TO_NSINODE

    // nsIDOMElement
    NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

    // nsIDOMXULElement
    NS_DECL_NSIDOMXULELEMENT

    virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;
    virtual nsEventStates IntrinsicState() const;

    nsresult GetFrameLoader(nsIFrameLoader** aFrameLoader);
    already_AddRefed<nsFrameLoader> GetFrameLoader();
    nsresult SwapFrameLoaders(nsIFrameLoaderOwner* aOtherOwner);

    virtual void RecompileScriptEventListeners();

    // This function should ONLY be used by BindToTree implementations.
    // The function exists solely because XUL elements store the binding
    // parent as a member instead of in the slots, as Element does.
    void SetXULBindingParent(nsIContent* aBindingParent)
    {
      mBindingParent = aBindingParent;
    }

    virtual nsXPCClassInfo* GetClassInfo();

    virtual nsIDOMNode* AsDOMNode() { return this; }
protected:

    // This can be removed if EnsureContentsGenerated dies.
    friend class nsNSElementTearoff;

    // Implementation methods
    nsresult EnsureContentsGenerated(void) const;

    nsresult ExecuteOnBroadcastHandler(nsIDOMElement* anElement, const nsAString& attrName);

    static nsresult
    ExecuteJSCode(nsIDOMElement* anElement, nsEvent* aEvent);

    // Helper routine that crawls a parent chain looking for a tree element.
    NS_IMETHOD GetParentTree(nsIDOMXULMultiSelectControlElement** aTreeElement);

    nsresult AddPopupListener(nsIAtom* aName);

    class nsXULSlots : public mozilla::dom::Element::nsDOMSlots
    {
    public:
        nsXULSlots();
        virtual ~nsXULSlots();

        void Traverse(nsCycleCollectionTraversalCallback &cb);

        nsRefPtr<nsFrameLoader> mFrameLoader;
    };

    virtual nsINode::nsSlots* CreateSlots();

    nsresult LoadSrc();

    /**
     * The nearest enclosing content node with a binding
     * that created us. [Weak]
     */
    nsIContent*                         mBindingParent;

    /**
     * Abandon our prototype linkage, and copy all attributes locally
     */
    nsresult MakeHeavyweight(nsXULPrototypeElement* aPrototype);

    virtual nsresult BeforeSetAttr(int32_t aNamespaceID, nsIAtom* aName,
                                   const nsAttrValueOrString* aValue,
                                   bool aNotify);
    virtual nsresult AfterSetAttr(int32_t aNamespaceID, nsIAtom* aName,
                                  const nsAttrValue* aValue, bool aNotify);

    virtual void UpdateEditableState(bool aNotify);

    virtual bool ParseAttribute(int32_t aNamespaceID,
                                  nsIAtom* aAttribute,
                                  const nsAString& aValue,
                                  nsAttrValue& aResult);

    virtual nsEventListenerManager*
      GetEventListenerManagerForAttr(nsIAtom* aAttrName, bool* aDefer);
  
    /**
     * Add a listener for the specified attribute, if appropriate.
     */
    void AddListenerFor(const nsAttrName& aName,
                        bool aCompileEventHandlers);
    void MaybeAddPopupListener(nsIAtom* aLocalName);

    nsIWidget* GetWindowWidget();

    // attribute setters for widget
    nsresult HideWindowChrome(bool aShouldHide);
    void SetChromeMargins(const nsAttrValue* aValue);
    void ResetChromeMargins();
    void SetTitlebarColor(nscolor aColor, bool aActive);

    void SetDrawsInTitlebar(bool aState);

    void RemoveBroadcaster(const nsAString & broadcasterId);

protected:
    // Internal accessor. This shadows the 'Slots', and returns
    // appropriate value.
    nsIControllers *Controllers() {
      nsDOMSlots* slots = GetExistingDOMSlots();
      return slots ? slots->mControllers : nullptr; 
    }

    void UnregisterAccessKey(const nsAString& aOldValue);
    bool BoolAttrIsTrue(nsIAtom* aName);

    friend nsresult
    NS_NewXULElement(nsIContent** aResult, nsINodeInfo *aNodeInfo);
    friend void
    NS_TrustedNewXULElement(nsIContent** aResult, nsINodeInfo *aNodeInfo);

    static already_AddRefed<nsXULElement>
    Create(nsXULPrototypeElement* aPrototype, nsINodeInfo *aNodeInfo,
           bool aIsScriptable);

    bool IsReadWriteTextElement() const
    {
        const nsIAtom* tag = Tag();
        return
            GetNameSpaceID() == kNameSpaceID_XUL &&
            (tag == nsGkAtoms::textbox || tag == nsGkAtoms::textarea) &&
            !HasAttr(kNameSpaceID_None, nsGkAtoms::readonly);
    }
};

#endif // nsXULElement_h__
