/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXTFElementWrapper.h"
#include "nsIXTFElement.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsXTFInterfaceAggregator.h"
#include "nsIClassInfo.h"
#include "nsPIDOMWindow.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIDocument.h"
#include "nsGkAtoms.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "nsEventStateManager.h"
#include "nsEventListenerManager.h"
#include "nsIDOMEvent.h"
#include "nsGUIEvent.h"
#include "nsContentUtils.h"
#include "nsIXTFService.h"
#include "nsIDOMAttr.h"
#include "nsIAttribute.h"
#include "nsDOMAttributeMap.h"
#include "nsUnicharUtils.h"
#include "nsEventDispatcher.h"
#include "nsIProgrammingLanguage.h"
#include "nsIXPConnect.h"
#include "nsXTFWeakTearoff.h"
#include "mozAutoDocUpdate.h"
#include "nsFocusManager.h"

nsXTFElementWrapper::nsXTFElementWrapper(already_AddRefed<nsINodeInfo> aNodeInfo,
                                         nsIXTFElement* aXTFElement)
    : nsXTFElementWrapperBase(aNodeInfo),
      mXTFElement(aXTFElement),
      mNotificationMask(0),
      mIntrinsicState(0),
      mTmpAttrName(nsGkAtoms::_asterix) // XXX this is a hack, but names
                                            // have to have a value
{
  // We never know when we might have a class
  SetFlags(NODE_MAY_HAVE_CLASS);
}

nsXTFElementWrapper::~nsXTFElementWrapper()
{
  mXTFElement->OnDestroyed();
  mXTFElement = nsnull;
  if (mClassInfo) {
    mClassInfo->Disconnect();
    mClassInfo = nsnull;
  }
}

nsresult
nsXTFElementWrapper::Init()
{
  // pass a weak wrapper (non base object ref-counted), so that
  // our mXTFElement can safely addref/release.
  nsISupports* weakWrapper = nsnull;
  nsresult rv = NS_NewXTFWeakTearoff(NS_GET_IID(nsIXTFElementWrapper),
                                     (nsIXTFElementWrapper*)this,
                                     &weakWrapper);
  NS_ENSURE_SUCCESS(rv, rv);

  mXTFElement->OnCreated(static_cast<nsIXTFElementWrapper*>(weakWrapper));
  weakWrapper->Release();

  bool innerHandlesAttribs = false;
  GetXTFElement()->GetIsAttributeHandler(&innerHandlesAttribs);
  if (innerHandlesAttribs)
    mAttributeHandler = do_QueryInterface(GetXTFElement());
  return NS_OK;
}

//----------------------------------------------------------------------
// nsISupports implementation

NS_IMPL_ADDREF_INHERITED(nsXTFElementWrapper, nsXTFElementWrapperBase)
NS_IMPL_RELEASE_INHERITED(nsXTFElementWrapper, nsXTFElementWrapperBase)

NS_IMPL_CYCLE_COLLECTION_CLASS(nsXTFElementWrapper)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsXTFElementWrapper,
                                                  nsXTFElementWrapperBase)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mXTFElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mAttributeHandler)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMETHODIMP
nsXTFElementWrapper::QueryInterface(REFNSIID aIID, void** aInstancePtr)
{
  NS_PRECONDITION(aInstancePtr, "null out param");

  NS_IMPL_QUERY_CYCLE_COLLECTION(nsXTFElementWrapper)
  if (aIID.Equals(NS_GET_IID(nsIClassInfo)) ||
      aIID.Equals(NS_GET_IID(nsXPCClassInfo))) {
    if (!mClassInfo) {
      mClassInfo = new nsXTFClassInfo(this);
    }
    NS_ADDREF(mClassInfo);
    *aInstancePtr = static_cast<nsIClassInfo*>(mClassInfo);
    return NS_OK;
  }
  if (aIID.Equals(NS_GET_IID(nsIXPCScriptable))) {
    if (!mClassInfo) {
      mClassInfo = new nsXTFClassInfo(this);
    }
    NS_ADDREF(mClassInfo);
    *aInstancePtr = static_cast<nsIXPCScriptable*>(mClassInfo);
    return NS_OK;
  }
  if (aIID.Equals(NS_GET_IID(nsIXTFElementWrapper))) {
    *aInstancePtr = static_cast<nsIXTFElementWrapper*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  nsresult rv = nsXTFElementWrapperBase::QueryInterface(aIID, aInstancePtr);
  if (NS_SUCCEEDED(rv)) {
    return rv;
  }

  // try to get get the interface from our wrapped element:
  nsCOMPtr<nsISupports> inner;
  QueryInterfaceInner(aIID, getter_AddRefs(inner));

  if (inner) {
    rv = NS_NewXTFInterfaceAggregator(aIID, inner,
                                      static_cast<nsIContent*>(this),
                                      aInstancePtr);

    return rv;
  }

  return NS_ERROR_NO_INTERFACE;
}

nsXPCClassInfo*
nsXTFElementWrapper::GetClassInfo()
{
  if (!mClassInfo) {
    mClassInfo = new nsXTFClassInfo(this);
  }
  return mClassInfo;
}

//----------------------------------------------------------------------
// nsIContent methods:

nsresult
nsXTFElementWrapper::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                                nsIContent* aBindingParent,
                                bool aCompileEventHandlers)
{
  // XXXbz making up random order for the notifications... Perhaps
  // this api should more closely match BindToTree/UnbindFromTree?
  nsCOMPtr<nsIDOMElement> domParent;
  if (aParent != GetParent()) {
    domParent = do_QueryInterface(aParent);
  }

  nsCOMPtr<nsIDOMDocument> domDocument;
  if (aDocument &&
      (mNotificationMask & (nsIXTFElement::NOTIFY_WILL_CHANGE_DOCUMENT |
                            nsIXTFElement::NOTIFY_DOCUMENT_CHANGED))) {
    domDocument = do_QueryInterface(aDocument);
  }

  if (domDocument &&
      (mNotificationMask & (nsIXTFElement::NOTIFY_WILL_CHANGE_DOCUMENT))) {
    GetXTFElement()->WillChangeDocument(domDocument);
  }

  if (domParent &&
      (mNotificationMask & (nsIXTFElement::NOTIFY_WILL_CHANGE_PARENT))) {
    GetXTFElement()->WillChangeParent(domParent);
  }

  nsresult rv = nsXTFElementWrapperBase::BindToTree(aDocument, aParent,
                                                    aBindingParent,
                                                    aCompileEventHandlers);

  NS_ENSURE_SUCCESS(rv, rv);

  if (mNotificationMask & nsIXTFElement::NOTIFY_PERFORM_ACCESSKEY)
    RegUnregAccessKey(true);

  if (domDocument &&
      (mNotificationMask & (nsIXTFElement::NOTIFY_DOCUMENT_CHANGED))) {
    GetXTFElement()->DocumentChanged(domDocument);
  }

  if (domParent &&
      (mNotificationMask & (nsIXTFElement::NOTIFY_PARENT_CHANGED))) {
    GetXTFElement()->ParentChanged(domParent);
  }

  return rv;  
}

void
nsXTFElementWrapper::UnbindFromTree(bool aDeep, bool aNullParent)
{
  // XXXbz making up random order for the notifications... Perhaps
  // this api should more closely match BindToTree/UnbindFromTree?

  bool inDoc = IsInDoc();
  if (inDoc &&
      (mNotificationMask & nsIXTFElement::NOTIFY_WILL_CHANGE_DOCUMENT)) {
    GetXTFElement()->WillChangeDocument(nsnull);
  }

  bool parentChanged = aNullParent && GetParent();

  if (parentChanged &&
      (mNotificationMask & nsIXTFElement::NOTIFY_WILL_CHANGE_PARENT)) {
    GetXTFElement()->WillChangeParent(nsnull);
  }

  if (mNotificationMask & nsIXTFElement::NOTIFY_PERFORM_ACCESSKEY)
    RegUnregAccessKey(false);

  nsXTFElementWrapperBase::UnbindFromTree(aDeep, aNullParent);

  if (parentChanged &&
      (mNotificationMask & nsIXTFElement::NOTIFY_PARENT_CHANGED)) {
    GetXTFElement()->ParentChanged(nsnull);
  }

  if (inDoc &&
      (mNotificationMask & nsIXTFElement::NOTIFY_DOCUMENT_CHANGED)) {
    GetXTFElement()->DocumentChanged(nsnull);
  }
}

nsresult
nsXTFElementWrapper::InsertChildAt(nsIContent* aKid, PRUint32 aIndex,
                                   bool aNotify)
{
  nsresult rv;

  nsCOMPtr<nsIDOMNode> domKid;
  if (mNotificationMask & (nsIXTFElement::NOTIFY_WILL_INSERT_CHILD |
                           nsIXTFElement::NOTIFY_CHILD_INSERTED))
    domKid = do_QueryInterface(aKid);
  
  if (mNotificationMask & nsIXTFElement::NOTIFY_WILL_INSERT_CHILD)
    GetXTFElement()->WillInsertChild(domKid, aIndex);
  rv = nsXTFElementWrapperBase::InsertChildAt(aKid, aIndex, aNotify);
  if (mNotificationMask & nsIXTFElement::NOTIFY_CHILD_INSERTED)
    GetXTFElement()->ChildInserted(domKid, aIndex);
  
  return rv;
}

void
nsXTFElementWrapper::RemoveChildAt(PRUint32 aIndex, bool aNotify)
{
  if (mNotificationMask & nsIXTFElement::NOTIFY_WILL_REMOVE_CHILD)
    GetXTFElement()->WillRemoveChild(aIndex);
  nsXTFElementWrapperBase::RemoveChildAt(aIndex, aNotify);
  if (mNotificationMask & nsIXTFElement::NOTIFY_CHILD_REMOVED)
    GetXTFElement()->ChildRemoved(aIndex);
}

nsIAtom *
nsXTFElementWrapper::GetIDAttributeName() const
{
  // XXX:
  return nsGkAtoms::id;
}

nsresult
nsXTFElementWrapper::SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                             nsIAtom* aPrefix, const nsAString& aValue,
                             bool aNotify)
{
  nsresult rv;

  if (aNameSpaceID == kNameSpaceID_None &&
      (mNotificationMask & nsIXTFElement::NOTIFY_WILL_SET_ATTRIBUTE))
    GetXTFElement()->WillSetAttribute(aName, aValue);

  if (aNameSpaceID==kNameSpaceID_None && HandledByInner(aName)) {
    rv = mAttributeHandler->SetAttribute(aName, aValue);
    // XXX mutation events?
  }
  else { // let wrapper handle it
    rv = nsXTFElementWrapperBase::SetAttr(aNameSpaceID, aName, aPrefix, aValue, aNotify);
  }
  
  if (aNameSpaceID == kNameSpaceID_None &&
      (mNotificationMask & nsIXTFElement::NOTIFY_ATTRIBUTE_SET))
    GetXTFElement()->AttributeSet(aName, aValue);

  if (mNotificationMask & nsIXTFElement::NOTIFY_PERFORM_ACCESSKEY) {
    nsCOMPtr<nsIDOMAttr> accesskey;
    GetXTFElement()->GetAccesskeyNode(getter_AddRefs(accesskey));
    nsCOMPtr<nsIAttribute> attr(do_QueryInterface(accesskey));
    if (attr && attr->NodeInfo()->Equals(aName, aNameSpaceID))
      RegUnregAccessKey(true);
  }

  return rv;
}

bool
nsXTFElementWrapper::GetAttr(PRInt32 aNameSpaceID, nsIAtom* aName, 
                             nsAString& aResult) const
{
  if (aNameSpaceID==kNameSpaceID_None && HandledByInner(aName)) {
    // XXX we don't do namespaced attributes yet
    nsresult rv = mAttributeHandler->GetAttribute(aName, aResult);
    return NS_SUCCEEDED(rv) && !aResult.IsVoid();
  }
  else { // try wrapper
    return nsXTFElementWrapperBase::GetAttr(aNameSpaceID, aName, aResult);
  }
}

bool
nsXTFElementWrapper::HasAttr(PRInt32 aNameSpaceID, nsIAtom* aName) const
{
  if (aNameSpaceID==kNameSpaceID_None && HandledByInner(aName)) {
    bool rval = false;
    mAttributeHandler->HasAttribute(aName, &rval);
    return rval;
  }
  else { // try wrapper
    return nsXTFElementWrapperBase::HasAttr(aNameSpaceID, aName);
  }
}

bool
nsXTFElementWrapper::AttrValueIs(PRInt32 aNameSpaceID,
                                 nsIAtom* aName,
                                 const nsAString& aValue,
                                 nsCaseTreatment aCaseSensitive) const
{
  NS_ASSERTION(aName, "Must have attr name");
  NS_ASSERTION(aNameSpaceID != kNameSpaceID_Unknown, "Must have namespace");

  if (aNameSpaceID == kNameSpaceID_None && HandledByInner(aName)) {
    nsAutoString ourVal;
    if (!GetAttr(aNameSpaceID, aName, ourVal)) {
      return false;
    }
    return aCaseSensitive == eCaseMatters ?
      aValue.Equals(ourVal) :
      aValue.Equals(ourVal, nsCaseInsensitiveStringComparator());
  }

  return nsXTFElementWrapperBase::AttrValueIs(aNameSpaceID, aName, aValue,
                                              aCaseSensitive);
}

bool
nsXTFElementWrapper::AttrValueIs(PRInt32 aNameSpaceID,
                                 nsIAtom* aName,
                                 nsIAtom* aValue,
                                 nsCaseTreatment aCaseSensitive) const
{
  NS_ASSERTION(aName, "Must have attr name");
  NS_ASSERTION(aNameSpaceID != kNameSpaceID_Unknown, "Must have namespace");
  NS_ASSERTION(aValue, "Null value atom");

  if (aNameSpaceID == kNameSpaceID_None && HandledByInner(aName)) {
    nsAutoString ourVal;
    if (!GetAttr(aNameSpaceID, aName, ourVal)) {
      return false;
    }
    if (aCaseSensitive == eCaseMatters) {
      return aValue->Equals(ourVal);
    }
    nsAutoString val;
    aValue->ToString(val);
    return val.Equals(ourVal, nsCaseInsensitiveStringComparator());
  }

  return nsXTFElementWrapperBase::AttrValueIs(aNameSpaceID, aName, aValue,
                                              aCaseSensitive);
}

PRInt32
nsXTFElementWrapper::FindAttrValueIn(PRInt32 aNameSpaceID,
                                     nsIAtom* aName,
                                     AttrValuesArray* aValues,
                                     nsCaseTreatment aCaseSensitive) const
{
  NS_ASSERTION(aName, "Must have attr name");
  NS_ASSERTION(aNameSpaceID != kNameSpaceID_Unknown, "Must have namespace");
  NS_ASSERTION(aValues, "Null value array");
  
  if (aNameSpaceID == kNameSpaceID_None && HandledByInner(aName)) {
    nsAutoString ourVal;
    if (!GetAttr(aNameSpaceID, aName, ourVal)) {
      return ATTR_MISSING;
    }
    
    for (PRInt32 i = 0; aValues[i]; ++i) {
      if (aCaseSensitive == eCaseMatters) {
        if ((*aValues[i])->Equals(ourVal)) {
          return i;
        }
      } else {
        nsAutoString val;
        (*aValues[i])->ToString(val);
        if (val.Equals(ourVal, nsCaseInsensitiveStringComparator())) {
          return i;
        }
      }
    }
    return ATTR_VALUE_NO_MATCH;
  }

  return nsXTFElementWrapperBase::FindAttrValueIn(aNameSpaceID, aName, aValues,
                                                  aCaseSensitive);
}

nsresult
nsXTFElementWrapper::UnsetAttr(PRInt32 aNameSpaceID, nsIAtom* aAttr, 
                               bool aNotify)
{
  nsresult rv;

  if (aNameSpaceID == kNameSpaceID_None &&
      (mNotificationMask & nsIXTFElement::NOTIFY_WILL_REMOVE_ATTRIBUTE))
    GetXTFElement()->WillRemoveAttribute(aAttr);

  if (mNotificationMask & nsIXTFElement::NOTIFY_PERFORM_ACCESSKEY) {
    nsCOMPtr<nsIDOMAttr> accesskey;
    GetXTFElement()->GetAccesskeyNode(getter_AddRefs(accesskey));
    nsCOMPtr<nsIAttribute> attr(do_QueryInterface(accesskey));
    if (attr && attr->NodeInfo()->Equals(aAttr, aNameSpaceID))
      RegUnregAccessKey(false);
  }

  if (aNameSpaceID==kNameSpaceID_None && HandledByInner(aAttr)) {
    nsDOMSlots *slots = GetExistingDOMSlots();
    if (slots && slots->mAttributeMap) {
      slots->mAttributeMap->DropAttribute(aNameSpaceID, aAttr);
    }
    rv = mAttributeHandler->RemoveAttribute(aAttr);

    // XXX if the RemoveAttribute() call fails, we might end up having removed
    // the attribute from the attribute map even though the attribute is still
    // on the element
    // https://bugzilla.mozilla.org/show_bug.cgi?id=296205

    // XXX mutation events?
  }
  else { // try wrapper
    rv = nsXTFElementWrapperBase::UnsetAttr(aNameSpaceID, aAttr, aNotify);
  }

  if (aNameSpaceID == kNameSpaceID_None &&
      (mNotificationMask & nsIXTFElement::NOTIFY_ATTRIBUTE_REMOVED))
    GetXTFElement()->AttributeRemoved(aAttr);

  return rv;
}

const nsAttrName*
nsXTFElementWrapper::GetAttrNameAt(PRUint32 aIndex) const
{
  PRUint32 innerCount=0;
  if (mAttributeHandler) {
    mAttributeHandler->GetAttributeCount(&innerCount);
  }
  
  if (aIndex < innerCount) {
    nsCOMPtr<nsIAtom> localName;
    nsresult rv = mAttributeHandler->GetAttributeNameAt(aIndex, getter_AddRefs(localName));
    NS_ENSURE_SUCCESS(rv, nsnull);

    const_cast<nsXTFElementWrapper*>(this)->mTmpAttrName.SetTo(localName);
    return &mTmpAttrName;
  }
  else { // wrapper handles attrib
    return nsXTFElementWrapperBase::GetAttrNameAt(aIndex - innerCount);
  }
}

PRUint32
nsXTFElementWrapper::GetAttrCount() const
{
  PRUint32 innerCount = 0;
  if (mAttributeHandler) {
    mAttributeHandler->GetAttributeCount(&innerCount);
  }
  // add wrapper attribs
  return innerCount + nsXTFElementWrapperBase::GetAttrCount();
}

void
nsXTFElementWrapper::BeginAddingChildren()
{
  if (mNotificationMask & nsIXTFElement::NOTIFY_BEGIN_ADDING_CHILDREN)
    GetXTFElement()->BeginAddingChildren();
}

void
nsXTFElementWrapper::DoneAddingChildren(bool aHaveNotified)
{
  if (mNotificationMask & nsIXTFElement::NOTIFY_DONE_ADDING_CHILDREN)
    GetXTFElement()->DoneAddingChildren();
}

already_AddRefed<nsINodeInfo>
nsXTFElementWrapper::GetExistingAttrNameFromQName(const nsAString& aStr) const
{
  nsINodeInfo* nodeInfo = nsXTFElementWrapperBase::GetExistingAttrNameFromQName(aStr).get();

  // Maybe this attribute is handled by our inner element:
  if (!nodeInfo) {
    nsCOMPtr<nsIAtom> nameAtom = do_GetAtom(aStr);
    if (HandledByInner(nameAtom)) 
      nodeInfo = mNodeInfo->NodeInfoManager()->
        GetNodeInfo(nameAtom, nsnull, kNameSpaceID_None,
                    nsIDOMNode::ATTRIBUTE_NODE).get();
  }
  
  return nodeInfo;
}

nsEventStates
nsXTFElementWrapper::IntrinsicState() const
{
  nsEventStates retState = nsXTFElementWrapperBase::IntrinsicState();
  if (mIntrinsicState.HasState(NS_EVENT_STATE_MOZ_READONLY)) {
    retState &= ~NS_EVENT_STATE_MOZ_READWRITE;
  } else if (mIntrinsicState.HasState(NS_EVENT_STATE_MOZ_READWRITE)) {
    retState &= ~NS_EVENT_STATE_MOZ_READONLY;
  }

  return  retState | mIntrinsicState;
}

void
nsXTFElementWrapper::PerformAccesskey(bool aKeyCausesActivation,
                                      bool aIsTrustedEvent)
{
  if (mNotificationMask & nsIXTFElement::NOTIFY_PERFORM_ACCESSKEY) {
    nsIFocusManager* fm = nsFocusManager::GetFocusManager();
    if (fm)
      fm->SetFocus(this, nsIFocusManager::FLAG_BYKEY);

    if (aKeyCausesActivation)
      GetXTFElement()->PerformAccesskey();
  }
}

nsresult
nsXTFElementWrapper::Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const
{
  *aResult = nsnull;
  nsCOMPtr<nsIContent> it;
  nsContentUtils::GetXTFService()->CreateElement(getter_AddRefs(it),
                                                 aNodeInfo);
  if (!it)
    return NS_ERROR_OUT_OF_MEMORY;

  nsXTFElementWrapper* wrapper =
    static_cast<nsXTFElementWrapper*>(it.get());
  nsresult rv = const_cast<nsXTFElementWrapper*>(this)->CopyInnerTo(wrapper);

  if (NS_SUCCEEDED(rv)) {
    if (mAttributeHandler) {
      PRUint32 innerCount = 0;
      mAttributeHandler->GetAttributeCount(&innerCount);
      for (PRUint32 i = 0; i < innerCount; ++i) {
        nsCOMPtr<nsIAtom> attrName;
        mAttributeHandler->GetAttributeNameAt(i, getter_AddRefs(attrName));
        if (attrName) {
          nsAutoString value;
          if (NS_SUCCEEDED(mAttributeHandler->GetAttribute(attrName, value)))
            it->SetAttr(kNameSpaceID_None, attrName, value, true);
        }
      }
    }
    NS_ADDREF(*aResult = it);
  }

  // XXX CloneState should take |const nIDOMElement*|
  wrapper->CloneState(const_cast<nsXTFElementWrapper*>(this));
  return rv;
}

//----------------------------------------------------------------------
// nsIDOMElement methods:

NS_IMETHODIMP
nsXTFElementWrapper::GetAttribute(const nsAString& aName, nsAString& aReturn)
{
  const nsAttrName* name = InternalGetExistingAttrNameFromQName(aName);
  if (name) {
    GetAttr(name->NamespaceID(), name->LocalName(), aReturn);
    return NS_OK;
  }

  // Maybe this attribute is handled by our inner element:
  if (mAttributeHandler) {
    nsresult rv = nsContentUtils::CheckQName(aName, false);
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<nsIAtom> nameAtom = do_GetAtom(aName);
    if (HandledByInner(nameAtom)) {
      GetAttr(kNameSpaceID_None, nameAtom, aReturn);
      return NS_OK;
    }
  }
  
  SetDOMStringToNull(aReturn);
  return NS_OK;
}

NS_IMETHODIMP
nsXTFElementWrapper::RemoveAttribute(const nsAString& aName)
{
  const nsAttrName* name = InternalGetExistingAttrNameFromQName(aName);

  if (name) {
    nsAttrName tmp(*name);
    return UnsetAttr(name->NamespaceID(), name->LocalName(), true);
  }

  // Maybe this attribute is handled by our inner element:
  if (mAttributeHandler) {
    nsCOMPtr<nsIAtom> nameAtom = do_GetAtom(aName);
    return UnsetAttr(kNameSpaceID_None, nameAtom, true);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsXTFElementWrapper::HasAttribute(const nsAString& aName, bool* aReturn)
{
  const nsAttrName* name = InternalGetExistingAttrNameFromQName(aName);
  if (name) {
    *aReturn = true;
    return NS_OK;
  }
  
  // Maybe this attribute is handled by our inner element:
  if (mAttributeHandler) {
    nsCOMPtr<nsIAtom> nameAtom = do_GetAtom(aName);
    *aReturn = HasAttr(kNameSpaceID_None, nameAtom);
    return NS_OK;
  }

  *aReturn = false;
  return NS_OK;
}


//----------------------------------------------------------------------
// nsIClassInfo implementation

/* void getInterfaces (out PRUint32 count, [array, size_is (count), retval] out nsIIDPtr array); */
NS_IMETHODIMP 
nsXTFElementWrapper::GetInterfaces(PRUint32* aCount, nsIID*** aArray)
{
  *aArray = nsnull;
  *aCount = 0;
  PRUint32 baseCount = 0;
  nsIID** baseArray = nsnull;
  PRUint32 xtfCount = 0;
  nsIID** xtfArray = nsnull;

  nsCOMPtr<nsIClassInfo> baseCi = GetBaseXPCClassInfo();
  if (baseCi) {
    baseCi->GetInterfaces(&baseCount, &baseArray);
  }

  GetXTFElement()->GetScriptingInterfaces(&xtfCount, &xtfArray);
  if (!xtfCount) {
    *aCount = baseCount;
    *aArray = baseArray;
    return NS_OK;
  } else if (!baseCount) {
    *aCount = xtfCount;
    *aArray = xtfArray;
    return NS_OK;
  }

  PRUint32 count = baseCount + xtfCount;
  nsIID** iids = static_cast<nsIID**>
                            (nsMemory::Alloc(count * sizeof(nsIID*)));
  NS_ENSURE_TRUE(iids, NS_ERROR_OUT_OF_MEMORY);

  PRUint32 i = 0;
  for (; i < baseCount; ++i) {
    iids[i] = static_cast<nsIID*>
                         (nsMemory::Clone(baseArray[i], sizeof(nsIID)));
    if (!iids[i]) {
      NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(baseCount, baseArray);
      NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(xtfCount, xtfArray);
      NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(i, iids);
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  for (; i < count; ++i) {
    iids[i] = static_cast<nsIID*>
                         (nsMemory::Clone(xtfArray[i - baseCount], sizeof(nsIID)));
    if (!iids[i]) {
      NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(baseCount, baseArray);
      NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(xtfCount, xtfArray);
      NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(i, iids);
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(baseCount, baseArray);
  NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(xtfCount, xtfArray);
  *aArray = iids;
  *aCount = count;

  return NS_OK;
}

/* nsISupports getHelperForLanguage (in PRUint32 language); */
NS_IMETHODIMP 
nsXTFElementWrapper::GetHelperForLanguage(PRUint32 language,
                                          nsISupports** aHelper)
{
  *aHelper = nsnull;
  nsCOMPtr<nsIClassInfo> ci = GetBaseXPCClassInfo();
  return
    ci ? ci->GetHelperForLanguage(language, aHelper) : NS_ERROR_NOT_AVAILABLE;
}

/* readonly attribute string contractID; */
NS_IMETHODIMP 
nsXTFElementWrapper::GetContractID(char * *aContractID)
{
  *aContractID = nsnull;
  return NS_OK;
}

/* readonly attribute string classDescription; */
NS_IMETHODIMP 
nsXTFElementWrapper::GetClassDescription(char * *aClassDescription)
{
  *aClassDescription = nsnull;
  return NS_OK;
}

/* readonly attribute nsCIDPtr classID; */
NS_IMETHODIMP 
nsXTFElementWrapper::GetClassID(nsCID * *aClassID)
{
  *aClassID = nsnull;
  return NS_OK;
}

/* readonly attribute PRUint32 implementationLanguage; */
NS_IMETHODIMP 
nsXTFElementWrapper::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
  *aImplementationLanguage = nsIProgrammingLanguage::UNKNOWN;
  return NS_OK;
}

/* readonly attribute PRUint32 flags; */
NS_IMETHODIMP 
nsXTFElementWrapper::GetFlags(PRUint32 *aFlags)
{
  *aFlags = nsIClassInfo::DOM_OBJECT;
  return NS_OK;
}

/* [notxpcom] readonly attribute nsCID classIDNoAlloc; */
NS_IMETHODIMP 
nsXTFElementWrapper::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
  return NS_ERROR_NOT_AVAILABLE;
}

//----------------------------------------------------------------------
// nsIXTFElementWrapper implementation:

/* readonly attribute nsIDOMElement elementNode; */
NS_IMETHODIMP
nsXTFElementWrapper::GetElementNode(nsIDOMElement * *aElementNode)
{
  *aElementNode = (nsIDOMElement*)this;
  NS_ADDREF(*aElementNode);
  return NS_OK;
}

/* readonly attribute nsIDOMElement documentFrameElement; */
NS_IMETHODIMP
nsXTFElementWrapper::GetDocumentFrameElement(nsIDOMElement * *aDocumentFrameElement)
{
  *aDocumentFrameElement = nsnull;
  
  nsIDocument *doc = GetCurrentDoc();
  if (!doc) {
    NS_WARNING("no document");
    return NS_OK;
  }
  nsCOMPtr<nsISupports> container = doc->GetContainer();
  if (!container) {
    NS_ERROR("no docshell");
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsPIDOMWindow> pidomwin = do_GetInterface(container);
  if (!pidomwin) {
    NS_ERROR("no nsPIDOMWindow interface on docshell");
    return NS_ERROR_FAILURE;
  }
  *aDocumentFrameElement = pidomwin->GetFrameElementInternal();
  NS_IF_ADDREF(*aDocumentFrameElement);
  return NS_OK;
}

/* attribute unsigned long notificationMask; */
NS_IMETHODIMP
nsXTFElementWrapper::GetNotificationMask(PRUint32 *aNotificationMask)
{
  *aNotificationMask = mNotificationMask;
  return NS_OK;
}
NS_IMETHODIMP
nsXTFElementWrapper::SetNotificationMask(PRUint32 aNotificationMask)
{
  mNotificationMask = aNotificationMask;
  return NS_OK;
}

//----------------------------------------------------------------------
// implementation helpers:
bool
nsXTFElementWrapper::QueryInterfaceInner(REFNSIID aIID, void** result)
{
  // We must ensure that the inner element has a distinct xpconnect
  // identity, so we mustn't aggregate nsIXPConnectWrappedJS:
  if (aIID.Equals(NS_GET_IID(nsIXPConnectWrappedJS))) return false;

  GetXTFElement()->QueryInterface(aIID, result);
  return (*result!=nsnull);
}

bool
nsXTFElementWrapper::HandledByInner(nsIAtom *attr) const
{
  bool retval = false;
  if (mAttributeHandler)
    mAttributeHandler->HandlesAttribute(attr, &retval);
  return retval;
}

nsresult
nsXTFElementWrapper::PostHandleEvent(nsEventChainPostVisitor& aVisitor)
{
  nsresult rv = NS_OK;
  if (aVisitor.mEventStatus == nsEventStatus_eConsumeNoDefault ||
      !(mNotificationMask & nsIXTFElement::NOTIFY_HANDLE_DEFAULT)) {
    return rv;
  }

  if (!aVisitor.mDOMEvent) {
    // We haven't made a DOMEvent yet.  Force making one now.
    if (NS_FAILED(rv = nsEventDispatcher::CreateEvent(aVisitor.mPresContext,
                                                      aVisitor.mEvent,
                                                      EmptyString(),
                                                      &aVisitor.mDOMEvent)))
      return rv;
  }
  if (!aVisitor.mDOMEvent)
    return NS_ERROR_FAILURE;
  
  bool defaultHandled = false;
  nsIXTFElement* xtfElement = GetXTFElement();
  if (xtfElement)
    rv = xtfElement->HandleDefault(aVisitor.mDOMEvent, &defaultHandled);
  if (defaultHandled)
    aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
  return rv;
}

NS_IMETHODIMP
nsXTFElementWrapper::SetIntrinsicState(nsEventStates::InternalType aNewState)
{
  nsIDocument *doc = GetCurrentDoc();
  nsEventStates newStates(aNewState);
  nsEventStates bits = mIntrinsicState ^ newStates;

  if (!doc || bits.IsEmpty())
    return NS_OK;

  NS_WARN_IF_FALSE(!newStates.HasAllStates(NS_EVENT_STATE_MOZ_READONLY |
                                           NS_EVENT_STATE_MOZ_READWRITE),
                   "Both READONLY and READWRITE are being set.  Yikes!!!");

  mIntrinsicState = newStates;
  UpdateState(true);

  return NS_OK;
}

nsIAtom *
nsXTFElementWrapper::GetClassAttributeName() const
{
  return mClassAttributeName;
}

const nsAttrValue*
nsXTFElementWrapper::DoGetClasses() const
{
  const nsAttrValue* val = nsnull;
  nsIAtom* clazzAttr = GetClassAttributeName();
  if (clazzAttr) {
    val = mAttrsAndChildren.GetAttr(clazzAttr);
    // This is possibly the first time we need any classes.
    if (val && val->Type() == nsAttrValue::eString) {
      nsAutoString value;
      val->ToString(value);
      nsAttrValue newValue;
      newValue.ParseAtomArray(value);
      const_cast<nsAttrAndChildArray*>(&mAttrsAndChildren)->
        SetAndTakeAttr(clazzAttr, newValue);
    }
  }
  return val;
}

nsresult
nsXTFElementWrapper::SetClassAttributeName(nsIAtom* aName)
{
  // The class attribute name can be set only once
  if (mClassAttributeName || !aName)
    return NS_ERROR_FAILURE;
  
  mClassAttributeName = aName;
  return NS_OK;
}

void
nsXTFElementWrapper::RegUnregAccessKey(bool aDoReg)
{
  nsIDocument* doc = GetCurrentDoc();
  if (!doc)
    return;

  // Get presentation shell 0
  nsIPresShell *presShell = doc->GetShell();
  if (!presShell)
    return;

  nsPresContext *presContext = presShell->GetPresContext();
  if (!presContext)
    return;

  nsEventStateManager *esm = presContext->EventStateManager();
  if (!esm)
    return;

  // Register or unregister as appropriate.
  nsCOMPtr<nsIDOMAttr> accesskeyNode;
  GetXTFElement()->GetAccesskeyNode(getter_AddRefs(accesskeyNode));
  if (!accesskeyNode)
    return;

  nsAutoString accessKey;
  accesskeyNode->GetValue(accessKey);

  if (aDoReg && !accessKey.IsEmpty())
    esm->RegisterAccessKey(this, (PRUint32)accessKey.First());
  else
    esm->UnregisterAccessKey(this, (PRUint32)accessKey.First());
}

nsresult
NS_NewXTFElementWrapper(nsIXTFElement* aXTFElement,
                        already_AddRefed<nsINodeInfo> aNodeInfo,
                        nsIContent** aResult)
{
  *aResult = nsnull;
   NS_ENSURE_ARG(aXTFElement);

  nsXTFElementWrapper* result = new nsXTFElementWrapper(aNodeInfo, aXTFElement);
  if (!result) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  NS_ADDREF(result);

  nsresult rv = result->Init();
  if (NS_FAILED(rv)) {
    NS_RELEASE(result);
    return rv;
  }

  *aResult = result;
  return NS_OK;
}

NS_IMPL_ISUPPORTS3(nsXTFClassInfo,
                   nsIClassInfo,
                   nsXPCClassInfo,
                   nsIXPCScriptable)
