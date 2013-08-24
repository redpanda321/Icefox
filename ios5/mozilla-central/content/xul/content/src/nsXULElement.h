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
#include "nsIScriptObjectOwner.h"
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
        : mName(nsGkAtoms::id),  // XXX this is a hack, but names have to have a value
          mEventHandler(nsnull)
    {
        XUL_PROTOTYPE_ATTRIBUTE_METER(gNumAttributes);
        MOZ_COUNT_CTOR(nsXULPrototypeAttribute);
    }

    ~nsXULPrototypeAttribute();

    nsAttrName mName;
    nsAttrValue mValue;
    // mEventHandler is only valid for the language ID specified in the
    // containing nsXULPrototypeElement.  We would ideally use
    // nsScriptObjectHolder, but want to avoid the extra lang ID.
    JSObject* mEventHandler;

#ifdef XUL_PROTOTYPE_ATTRIBUTE_METERING
    /**
      If enough attributes, on average, are event handlers, it pays to keep
      mEventHandler here, instead of maintaining a separate mapping in each
      nsXULElement associating those mName values with their mEventHandlers.
      Assume we don't need to keep mNameSpaceID along with mName in such an
      event-handler-only name-to-function-pointer mapping.

      Let
        minAttrSize  = sizeof(mNodeInof) + sizeof(mValue)
        mappingSize  = sizeof(mNodeInfo) + sizeof(mEventHandler)
        elemOverhead = nElems * sizeof(MappingPtr)

      Then
        nAttrs * minAttrSize + nEventHandlers * mappingSize + elemOverhead
        > nAttrs * (minAttrSize + mappingSize - sizeof(mNodeInfo))
      which simplifies to
        nEventHandlers * mappingSize + elemOverhead
        > nAttrs * (mappingSize - sizeof(mNodeInfo))
      or
        nEventHandlers + (nElems * sizeof(MappingPtr)) / mappingSize
        > nAttrs * (1 - sizeof(mNodeInfo) / mappingSize)

      If nsCOMPtr and all other pointers are the same size, this reduces to
        nEventHandlers + nElems / 2 > nAttrs / 2

      To measure how many attributes are event handlers, compile XUL source
      with XUL_PROTOTYPE_ATTRIBUTE_METERING and watch the counters below.
      Plug into the above relation -- if true, it pays to put mEventHandler
      in nsXULPrototypeAttribute rather than to keep a separate mapping.

      Recent numbers after opening four browser windows:
        nElems 3537, nAttrs 2528, nEventHandlers 1042
      giving 1042 + 3537/2 > 2528/2 or 2810 > 1264.

      As it happens, mEventHandler also makes this struct power-of-2 sized,
      8 words on most architectures, which makes for strength-reduced array
      index-to-pointer calculations.
     */
    static PRUint32   gNumElements;
    static PRUint32   gNumAttributes;
    static PRUint32   gNumEventHandlers;
    static PRUint32   gNumCacheTests;
    static PRUint32   gNumCacheHits;
    static PRUint32   gNumCacheSets;
    static PRUint32   gNumCacheFills;
#endif /* !XUL_PROTOTYPE_ATTRIBUTE_METERING */
};


/**

  A prototype content model element that holds the "primordial" values
  that have been parsed from the original XUL document. A
  'lightweight' nsXULElement may delegate its representation to this
  structure, which is shared.

 */

class nsXULPrototypeNode : public nsISupports
{
public:
    enum Type { eType_Element, eType_Script, eType_Text, eType_PI };

    Type                     mType;

    NS_DECL_CYCLE_COLLECTING_ISUPPORTS

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
    virtual PRUint32 ClassSize() = 0;
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

    NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(nsXULPrototypeNode)

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
          mAttributes(nsnull),
          mHasIdAttribute(false),
          mHasClassAttribute(false),
          mHasStyleAttribute(false),
          mHoldsScriptObject(false)
    {
    }

    virtual ~nsXULPrototypeElement()
    {
        Unlink();
    }

#ifdef NS_BUILD_REFCNT_LOGGING
    virtual const char* ClassName() { return "nsXULPrototypeElement"; }
    virtual PRUint32 ClassSize() { return sizeof(*this); }
#endif

    virtual void ReleaseSubtree()
    {
        for (PRInt32 i = mChildren.Length() - 1; i >= 0; i--) {
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

    nsresult SetAttrAt(PRUint32 aPos, const nsAString& aValue, nsIURI* aDocumentURI);

    void Unlink();

    nsPrototypeArray         mChildren;

    nsCOMPtr<nsINodeInfo>    mNodeInfo;           // [OWNER]

    PRUint32                 mNumAttributes;
    nsXULPrototypeAttribute* mAttributes;         // [OWNER]
    
    bool                     mHasIdAttribute:1;
    bool                     mHasClassAttribute:1;
    bool                     mHasStyleAttribute:1;
    bool                     mHoldsScriptObject:1;

};

class nsXULDocument;

class nsXULPrototypeScript : public nsXULPrototypeNode
{
public:
    nsXULPrototypeScript(PRUint32 aLineNo, PRUint32 version);
    virtual ~nsXULPrototypeScript();

#ifdef NS_BUILD_REFCNT_LOGGING
    virtual const char* ClassName() { return "nsXULPrototypeScript"; }
    virtual PRUint32 ClassSize() { return sizeof(*this); }
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

    nsresult Compile(const PRUnichar* aText, PRInt32 aTextLength,
                     nsIURI* aURI, PRUint32 aLineNo,
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
        ScriptObjectHolder() : mObject(nsnull)
        {
        }
        JSScript* mObject;
    };

    nsCOMPtr<nsIURI>         mSrcURI;
    PRUint32                 mLineNo;
    bool                     mSrcLoading;
    bool                     mOutOfLine;
    nsXULDocument*           mSrcLoadWaiters;   // [OWNER] but not COMPtr
    PRUint32                 mLangVersion;
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
    virtual PRUint32 ClassSize() { return sizeof(*this); }
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
    virtual PRUint32 ClassSize() { return sizeof(*this); }
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

#define XUL_ELEMENT_TEMPLATE_GENERATED (1 << ELEMENT_TYPE_SPECIFIC_BITS_OFFSET)

// Make sure we have space for our bit
PR_STATIC_ASSERT(ELEMENT_TYPE_SPECIFIC_BITS_OFFSET < 32);

class nsScriptEventHandlerOwnerTearoff;

class nsXULElement : public nsStyledElement, public nsIDOMXULElement
{
public:

    /** Typesafe, non-refcounting cast from nsIContent.  Cheaper than QI. **/
    static nsXULElement* FromContent(nsIContent *aContent)
    {
        if (aContent->IsXUL())
            return static_cast<nsXULElement*>(aContent);
        return nsnull;
    }

public:
    nsXULElement(already_AddRefed<nsINodeInfo> aNodeInfo);

    static nsresult
    Create(nsXULPrototypeElement* aPrototype, nsIDocument* aDocument,
           bool aIsScriptable, mozilla::dom::Element** aResult);

    // nsISupports
    NS_DECL_ISUPPORTS_INHERITED
    NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED_NO_UNLINK(nsXULElement,
                                                       nsGenericElement)

    // nsINode
    virtual nsresult PreHandleEvent(nsEventChainPreVisitor& aVisitor);

    // nsIContent
    virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                                nsIContent* aBindingParent,
                                bool aCompileEventHandlers);
    virtual void UnbindFromTree(bool aDeep, bool aNullParent);
    virtual void RemoveChildAt(PRUint32 aIndex, bool aNotify);
    virtual bool GetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                           nsAString& aResult) const;
    virtual bool HasAttr(PRInt32 aNameSpaceID, nsIAtom* aName) const;
    virtual bool AttrValueIs(PRInt32 aNameSpaceID, nsIAtom* aName,
                               const nsAString& aValue,
                               nsCaseTreatment aCaseSensitive) const;
    virtual bool AttrValueIs(PRInt32 aNameSpaceID, nsIAtom* aName,
                               nsIAtom* aValue,
                               nsCaseTreatment aCaseSensitive) const;
    virtual PRInt32 FindAttrValueIn(PRInt32 aNameSpaceID,
                                    nsIAtom* aName,
                                    AttrValuesArray* aValues,
                                    nsCaseTreatment aCaseSensitive) const;
    virtual nsresult UnsetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                               bool aNotify);
    virtual const nsAttrName* GetAttrNameAt(PRUint32 aIndex) const;
    virtual PRUint32 GetAttrCount() const;
    virtual void DestroyContent();

#ifdef DEBUG
    virtual void List(FILE* out, PRInt32 aIndent) const;
    virtual void DumpContent(FILE* out, PRInt32 aIndent,bool aDumpAll) const
    {
    }
#endif

    virtual void PerformAccesskey(bool aKeyCausesActivation,
                                  bool aIsTrustedEvent);
    nsresult ClickWithInputSource(PRUint16 aInputSource);

    virtual nsIContent *GetBindingParent() const;
    virtual bool IsNodeOfType(PRUint32 aFlags) const;
    virtual bool IsFocusable(PRInt32 *aTabIndex = nsnull, bool aWithMouse = false);
    virtual nsIAtom* DoGetID() const;
    virtual const nsAttrValue* DoGetClasses() const;

    NS_IMETHOD WalkContentStyleRules(nsRuleWalker* aRuleWalker);
    virtual mozilla::css::StyleRule* GetInlineStyleRule();
    virtual nsChangeHint GetAttributeChangeHint(const nsIAtom* aAttribute,
                                                PRInt32 aModType) const;
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
    NS_FORWARD_NSIDOMNODE(nsGenericElement::)

    // nsIDOMElement
    NS_FORWARD_NSIDOMELEMENT(nsGenericElement::)

    // nsIDOMXULElement
    NS_DECL_NSIDOMXULELEMENT

    virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;
    virtual nsEventStates IntrinsicState() const;

    nsresult EnsureLocalStyle();

    nsresult GetFrameLoader(nsIFrameLoader** aFrameLoader);
    already_AddRefed<nsFrameLoader> GetFrameLoader();
    nsresult SwapFrameLoaders(nsIFrameLoaderOwner* aOtherOwner);

    virtual void RecompileScriptEventListeners();

    // This function should ONLY be used by BindToTree implementations.
    // The function exists solely because XUL elements store the binding
    // parent as a member instead of in the slots, as nsGenericElement does.
    void SetXULBindingParent(nsIContent* aBindingParent)
    {
      mBindingParent = aBindingParent;
    }

    /**
     * Get the attr info for the given namespace ID and attribute name.
     * The namespace ID must not be kNameSpaceID_Unknown and the name
     * must not be null.
     */
    virtual nsAttrInfo GetAttrInfo(PRInt32 aNamespaceID, nsIAtom* aName) const;

    virtual nsXPCClassInfo* GetClassInfo();

    virtual nsIDOMNode* AsDOMNode() { return this; }
protected:
    // XXX This can be removed when nsNodeUtils::CloneAndAdopt doesn't need
    //     access to mPrototype anymore.
    friend class nsNodeUtils;

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

    class nsXULSlots : public nsGenericElement::nsDOMSlots
    {
    public:
        nsXULSlots();
        virtual ~nsXULSlots();

        void Traverse(nsCycleCollectionTraversalCallback &cb);

        nsRefPtr<nsFrameLoader> mFrameLoader;
    };

    virtual nsINode::nsSlots* CreateSlots();

    nsresult LoadSrc();

    // Required fields
    nsRefPtr<nsXULPrototypeElement>     mPrototype;

    /**
     * The nearest enclosing content node with a binding
     * that created us. [Weak]
     */
    nsIContent*                         mBindingParent;

    /**
     * Abandon our prototype linkage, and copy all attributes locally
     */
    nsresult MakeHeavyweight();

    const nsAttrValue* FindLocalOrProtoAttr(PRInt32 aNameSpaceID,
                                            nsIAtom *aName) const {
        return nsXULElement::GetAttrInfo(aNameSpaceID, aName).mValue;
    }

    virtual nsresult BeforeSetAttr(PRInt32 aNamespaceID, nsIAtom* aName,
                                   const nsAttrValueOrString* aValue,
                                   bool aNotify);
    virtual nsresult AfterSetAttr(PRInt32 aNamespaceID, nsIAtom* aName,
                                  const nsAttrValue* aValue, bool aNotify);

    virtual void UpdateEditableState(bool aNotify);

    virtual bool ParseAttribute(PRInt32 aNamespaceID,
                                  nsIAtom* aAttribute,
                                  const nsAString& aValue,
                                  nsAttrValue& aResult);

    virtual nsEventListenerManager*
      GetEventListenerManagerForAttr(nsIAtom* aAttrName, bool* aDefer);
  
    /**
     * Return our prototype's attribute, if one exists.
     */
    nsXULPrototypeAttribute *FindPrototypeAttribute(PRInt32 aNameSpaceID,
                                                    nsIAtom *aName) const;
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

    const nsAttrName* InternalGetExistingAttrNameFromQName(const nsAString& aStr) const;

    void RemoveBroadcaster(const nsAString & broadcasterId);

protected:
    // Internal accessor. This shadows the 'Slots', and returns
    // appropriate value.
    nsIControllers *Controllers() {
      nsDOMSlots* slots = GetExistingDOMSlots();
      return slots ? slots->mControllers : nsnull; 
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

    friend class nsScriptEventHandlerOwnerTearoff;

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
