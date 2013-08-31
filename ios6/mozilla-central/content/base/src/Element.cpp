/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=79: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for all element classes; this provides an implementation
 * of DOM Core's nsIDOMElement, implements nsIContent, provides
 * utility methods for subclasses, and so forth.
 */

#include "mozilla/DebugOnly.h"

#include "mozilla/dom/Element.h"

#include "nsDOMAttribute.h"
#include "nsDOMAttributeMap.h"
#include "nsIAtom.h"
#include "nsINodeInfo.h"
#include "nsIDocument.h"
#include "nsIDOMNodeList.h"
#include "nsIDOMDocument.h"
#include "nsIContentIterator.h"
#include "nsEventListenerManager.h"
#include "nsFocusManager.h"
#include "nsILinkHandler.h"
#include "nsIScriptGlobalObject.h"
#include "nsIURL.h"
#include "nsNetUtil.h"
#include "nsIFrame.h"
#include "nsIAnonymousContentCreator.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "nsStyleConsts.h"
#include "nsString.h"
#include "nsUnicharUtils.h"
#include "nsEventStateManager.h"
#include "nsIDOMEvent.h"
#include "nsDOMCID.h"
#include "nsIServiceManager.h"
#include "nsIDOMCSSStyleDeclaration.h"
#include "nsDOMCSSAttrDeclaration.h"
#include "nsINameSpaceManager.h"
#include "nsContentList.h"
#include "nsDOMTokenList.h"
#include "nsXBLPrototypeBinding.h"
#include "nsError.h"
#include "nsDOMString.h"
#include "nsIScriptSecurityManager.h"
#include "nsIDOMMutationEvent.h"
#include "nsMutationEvent.h"
#include "nsNodeUtils.h"
#include "mozilla/dom/DirectionalityUtils.h"
#include "nsDocument.h"
#include "nsAttrValueOrString.h"
#include "nsAttrValueInlines.h"
#ifdef MOZ_XUL
#include "nsXULElement.h"
#endif /* MOZ_XUL */
#include "nsFrameManager.h"
#include "nsFrameSelection.h"
#ifdef DEBUG
#include "nsRange.h"
#endif

#include "nsBindingManager.h"
#include "nsXBLBinding.h"
#include "nsPIDOMWindow.h"
#include "nsPIBoxObject.h"
#include "nsClientRect.h"
#include "nsSVGUtils.h"
#include "nsLayoutUtils.h"
#include "nsGkAtoms.h"
#include "nsContentUtils.h"
#include "nsIJSContextStack.h"

#include "nsIDOMEventListener.h"
#include "nsIWebNavigation.h"
#include "nsIBaseWindow.h"
#include "nsIWidget.h"

#include "jsapi.h"

#include "nsNodeInfoManager.h"
#include "nsICategoryManager.h"
#include "nsIDOMDocumentType.h"
#include "nsIDOMUserDataHandler.h"
#include "nsGenericHTMLElement.h"
#include "nsIEditor.h"
#include "nsIEditorIMESupport.h"
#include "nsIEditorDocShell.h"
#include "nsEventDispatcher.h"
#include "nsContentCreatorFunctions.h"
#include "nsIControllers.h"
#include "nsIView.h"
#include "nsIViewManager.h"
#include "nsIScrollableFrame.h"
#include "nsXBLInsertionPoint.h"
#include "mozilla/css/StyleRule.h" /* For nsCSSSelectorList */
#include "nsCSSRuleProcessor.h"
#include "nsRuleProcessorData.h"
#include "nsAsyncDOMEvent.h"
#include "nsTextNode.h"

#ifdef MOZ_XUL
#include "nsIXULDocument.h"
#endif /* MOZ_XUL */

#include "nsCycleCollectionParticipant.h"
#include "nsCCUncollectableMarker.h"

#include "mozAutoDocUpdate.h"

#include "nsCSSParser.h"
#include "prprf.h"
#include "nsDOMMutationObserver.h"
#include "nsSVGFeatures.h"
#include "nsWrapperCacheInlines.h"
#include "nsCycleCollector.h"
#include "xpcpublic.h"
#include "nsIScriptError.h"
#include "nsLayoutStatics.h"
#include "mozilla/Telemetry.h"

#include "mozilla/CORSMode.h"

#include "nsStyledElement.h"
#include "nsXBLService.h"

using namespace mozilla;
using namespace mozilla::dom;

nsEventStates
Element::IntrinsicState() const
{
  return IsEditable() ? NS_EVENT_STATE_MOZ_READWRITE :
                        NS_EVENT_STATE_MOZ_READONLY;
}

void
Element::NotifyStateChange(nsEventStates aStates)
{
  nsIDocument* doc = GetCurrentDoc();
  if (doc) {
    nsAutoScriptBlocker scriptBlocker;
    doc->ContentStateChanged(this, aStates);
  }
}

void
Element::UpdateLinkState(nsEventStates aState)
{
  NS_ABORT_IF_FALSE(!aState.HasAtLeastOneOfStates(~(NS_EVENT_STATE_VISITED |
                                                    NS_EVENT_STATE_UNVISITED)),
                    "Unexpected link state bits");
  mState =
    (mState & ~(NS_EVENT_STATE_VISITED | NS_EVENT_STATE_UNVISITED)) |
    aState;
}

void
Element::UpdateState(bool aNotify)
{
  nsEventStates oldState = mState;
  mState = IntrinsicState() | (oldState & ESM_MANAGED_STATES);
  if (aNotify) {
    nsEventStates changedStates = oldState ^ mState;
    if (!changedStates.IsEmpty()) {
      nsIDocument* doc = GetCurrentDoc();
      if (doc) {
        nsAutoScriptBlocker scriptBlocker;
        doc->ContentStateChanged(this, changedStates);
      }
    }
  }
}

void
nsIContent::UpdateEditableState(bool aNotify)
{
  // Guaranteed to be non-element content
  NS_ASSERTION(!IsElement(), "What happened here?");
  nsIContent *parent = GetParent();

  SetEditableFlag(parent && parent->HasFlag(NODE_IS_EDITABLE));
}

void
Element::UpdateEditableState(bool aNotify)
{
  nsIContent *parent = GetParent();

  SetEditableFlag(parent && parent->HasFlag(NODE_IS_EDITABLE));
  if (aNotify) {
    UpdateState(aNotify);
  } else {
    // Avoid calling UpdateState in this very common case, because
    // this gets called for pretty much every single element on
    // insertion into the document and UpdateState can be slow for
    // some kinds of elements even when not notifying.
    if (IsEditable()) {
      RemoveStatesSilently(NS_EVENT_STATE_MOZ_READONLY);
      AddStatesSilently(NS_EVENT_STATE_MOZ_READWRITE);
    } else {
      RemoveStatesSilently(NS_EVENT_STATE_MOZ_READWRITE);
      AddStatesSilently(NS_EVENT_STATE_MOZ_READONLY);
    }
  }
}

nsEventStates
Element::StyleStateFromLocks() const
{
  nsEventStates locks = LockedStyleStates();
  nsEventStates state = mState | locks;

  if (locks.HasState(NS_EVENT_STATE_VISITED)) {
    return state & ~NS_EVENT_STATE_UNVISITED;
  }
  if (locks.HasState(NS_EVENT_STATE_UNVISITED)) {
    return state & ~NS_EVENT_STATE_VISITED;
  }
  return state;
}

nsEventStates
Element::LockedStyleStates() const
{
  nsEventStates *locks =
    static_cast<nsEventStates*> (GetProperty(nsGkAtoms::lockedStyleStates));
  if (locks) {
    return *locks;
  }
  return nsEventStates();
}

static void
nsEventStatesPropertyDtor(void *aObject, nsIAtom *aProperty,
                          void *aPropertyValue, void *aData)
{
  nsEventStates *states = static_cast<nsEventStates*>(aPropertyValue);
  delete states;
}

void
Element::NotifyStyleStateChange(nsEventStates aStates)
{
  nsIDocument* doc = GetCurrentDoc();
  if (doc) {
    nsIPresShell *presShell = doc->GetShell();
    if (presShell) {
      nsAutoScriptBlocker scriptBlocker;
      presShell->ContentStateChanged(doc, this, aStates);
    }
  }
}

void
Element::LockStyleStates(nsEventStates aStates)
{
  nsEventStates *locks = new nsEventStates(LockedStyleStates());

  *locks |= aStates;

  if (aStates.HasState(NS_EVENT_STATE_VISITED)) {
    *locks &= ~NS_EVENT_STATE_UNVISITED;
  }
  if (aStates.HasState(NS_EVENT_STATE_UNVISITED)) {
    *locks &= ~NS_EVENT_STATE_VISITED;
  }

  SetProperty(nsGkAtoms::lockedStyleStates, locks, nsEventStatesPropertyDtor);
  SetHasLockedStyleStates();

  NotifyStyleStateChange(aStates);
}

void
Element::UnlockStyleStates(nsEventStates aStates)
{
  nsEventStates *locks = new nsEventStates(LockedStyleStates());

  *locks &= ~aStates;

  if (locks->IsEmpty()) {
    DeleteProperty(nsGkAtoms::lockedStyleStates);
    ClearHasLockedStyleStates();
    delete locks;
  }
  else {
    SetProperty(nsGkAtoms::lockedStyleStates, locks, nsEventStatesPropertyDtor);
  }

  NotifyStyleStateChange(aStates);
}

void
Element::ClearStyleStateLocks()
{
  nsEventStates locks = LockedStyleStates();

  DeleteProperty(nsGkAtoms::lockedStyleStates);
  ClearHasLockedStyleStates();

  NotifyStyleStateChange(locks);
}

bool
Element::GetBindingURL(nsIDocument *aDocument, css::URLValue **aResult)
{
  // If we have a frame the frame has already loaded the binding.  And
  // otherwise, don't do anything else here unless we're dealing with
  // XUL or an HTML element that may have a plugin-related overlay
  // (i.e. object, embed, or applet).
  bool isXULorPluginElement = (IsXUL() ||
                               IsHTML(nsGkAtoms::object) ||
                               IsHTML(nsGkAtoms::embed) ||
                               IsHTML(nsGkAtoms::applet));
  nsIPresShell *shell = aDocument->GetShell();
  if (!shell || GetPrimaryFrame() || !isXULorPluginElement) {
    *aResult = nullptr;

    return true;
  }

  // Get the computed -moz-binding directly from the style context
  nsPresContext *pctx = shell->GetPresContext();
  NS_ENSURE_TRUE(pctx, false);

  nsRefPtr<nsStyleContext> sc = pctx->StyleSet()->ResolveStyleFor(this,
                                                                  nullptr);
  NS_ENSURE_TRUE(sc, false);

  *aResult = sc->GetStyleDisplay()->mBinding;

  return true;
}

JSObject*
Element::WrapObject(JSContext *aCx, JSObject *aScope,
                    bool *aTriedToWrap)
{
  JSObject* obj = nsINode::WrapObject(aCx, aScope, aTriedToWrap);
  if (!obj) {
    return nullptr;
  }

  nsIDocument* doc;
  if (HasFlag(NODE_FORCE_XBL_BINDINGS)) {
    doc = OwnerDoc();
  }
  else {
    doc = GetCurrentDoc();
  }

  if (!doc) {
    // There's no baseclass that cares about this call so we just
    // return here.
    return obj;
  }

  // We must ensure that the XBL Binding is installed before we hand
  // back this object.

  if (HasFlag(NODE_MAY_BE_IN_BINDING_MNGR) &&
      doc->BindingManager()->GetBinding(this)) {
    // There's already a binding for this element so nothing left to
    // be done here.

    // In theory we could call ExecuteAttachedHandler here when it's safe to
    // run script if we also removed the binding from the PAQ queue, but that
    // seems like a scary change that would mosly just add more
    // inconsistencies.
    return obj;
  }

  // Make sure the style context goes away _before_ we load the binding
  // since that can destroy the relevant presshell.
  mozilla::css::URLValue *bindingURL;
  bool ok = GetBindingURL(doc, &bindingURL);
  if (!ok) {
    dom::Throw<true>(aCx, NS_ERROR_FAILURE);
    return nullptr;
  }

  if (!bindingURL) {
    // No binding, nothing left to do here.
    return obj;
  }

  nsCOMPtr<nsIURI> uri = bindingURL->GetURI();
  nsCOMPtr<nsIPrincipal> principal = bindingURL->mOriginPrincipal;

  // We have a binding that must be installed.
  bool dummy;

  nsXBLService* xblService = nsXBLService::GetInstance();
  if (!xblService) {
    dom::Throw<true>(aCx, NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }

  nsRefPtr<nsXBLBinding> binding;
  xblService->LoadBindings(this, uri, principal, false, getter_AddRefs(binding),
                           &dummy);
  
  if (binding) {
    if (nsContentUtils::IsSafeToRunScript()) {
      binding->ExecuteAttachedHandler();
    }
    else {
      nsContentUtils::AddScriptRunner(
        NS_NewRunnableMethod(binding, &nsXBLBinding::ExecuteAttachedHandler));
    }
  }

  return obj;
}

Element*
Element::GetFirstElementChild() const
{
  uint32_t i, count = mAttrsAndChildren.ChildCount();
  for (i = 0; i < count; ++i) {
    nsIContent* child = mAttrsAndChildren.ChildAt(i);
    if (child->IsElement()) {
      return child->AsElement();
    }
  }
  
  return nullptr;
}

Element*
Element::GetLastElementChild() const
{
  uint32_t i = mAttrsAndChildren.ChildCount();
  while (i > 0) {
    nsIContent* child = mAttrsAndChildren.ChildAt(--i);
    if (child->IsElement()) {
      return child->AsElement();
    }
  }
  
  return nullptr;
}

nsDOMTokenList*
Element::GetClassList()
{
  Element::nsDOMSlots *slots = DOMSlots();

  if (!slots->mClassList) {
    nsIAtom* classAttr = GetClassAttributeName();
    if (classAttr) {
      slots->mClassList = new nsDOMTokenList(this, classAttr);
    }
  }

  return slots->mClassList;
}

void
Element::GetClassList(nsIDOMDOMTokenList** aClassList)
{
  NS_IF_ADDREF(*aClassList = GetClassList());
}

already_AddRefed<nsIHTMLCollection>
Element::GetElementsByTagName(const nsAString& aLocalName)
{
  return NS_GetContentList(this, kNameSpaceID_Unknown, aLocalName);
}

void
Element::GetElementsByTagName(const nsAString& aLocalName,
                              nsIDOMHTMLCollection** aResult)
{
  *aResult = GetElementsByTagName(aLocalName).get();
}

nsIFrame*
Element::GetStyledFrame()
{
  nsIFrame *frame = GetPrimaryFrame(Flush_Layout);
  return frame ? nsLayoutUtils::GetStyleFrame(frame) : nullptr;
}

Element*
Element::GetOffsetRect(nsRect& aRect)
{
  aRect = nsRect();

  nsIFrame* frame = GetStyledFrame();
  if (!frame) {
    return nullptr;
  }

  nsPoint origin = frame->GetPosition();
  aRect.x = nsPresContext::AppUnitsToIntCSSPixels(origin.x);
  aRect.y = nsPresContext::AppUnitsToIntCSSPixels(origin.y);

  // Get the union of all rectangles in this and continuation frames.
  // It doesn't really matter what we use as aRelativeTo here, since
  // we only care about the size. Using 'parent' might make things
  // a bit faster by speeding up the internal GetOffsetTo operations.
  nsIFrame* parent = frame->GetParent() ? frame->GetParent() : frame;
  nsRect rcFrame = nsLayoutUtils::GetAllInFlowRectsUnion(frame, parent);
  aRect.width = nsPresContext::AppUnitsToIntCSSPixels(rcFrame.width);
  aRect.height = nsPresContext::AppUnitsToIntCSSPixels(rcFrame.height);

  return nullptr;
}

nsIntSize
Element::GetPaddingRectSize()
{
  nsIFrame* frame = GetStyledFrame();
  if (!frame) {
    return nsIntSize(0, 0);
  }

  NS_ASSERTION(frame->GetParent(), "Styled frame has no parent");
  nsRect rcFrame = nsLayoutUtils::GetAllInFlowPaddingRectsUnion(frame, frame->GetParent());
  return nsIntSize(nsPresContext::AppUnitsToIntCSSPixels(rcFrame.width),
                   nsPresContext::AppUnitsToIntCSSPixels(rcFrame.height));
}

nsIScrollableFrame*
Element::GetScrollFrame(nsIFrame **aStyledFrame)
{
  // it isn't clear what to return for SVG nodes, so just return nothing
  if (IsSVG()) {
    if (aStyledFrame) {
      *aStyledFrame = nullptr;
    }
    return nullptr;
  }

  nsIFrame* frame = GetStyledFrame();

  if (aStyledFrame) {
    *aStyledFrame = frame;
  }
  if (!frame) {
    return nullptr;
  }

  // menu frames implement GetScrollTargetFrame but we don't want
  // to use it here.  Similar for comboboxes.
  if (frame->GetType() != nsGkAtoms::menuFrame &&
      frame->GetType() != nsGkAtoms::comboboxControlFrame) {
    nsIScrollableFrame *scrollFrame = frame->GetScrollTargetFrame();
    if (scrollFrame)
      return scrollFrame;
  }

  nsIDocument* doc = OwnerDoc();
  bool quirksMode = doc->GetCompatibilityMode() == eCompatibility_NavQuirks;
  Element* elementWithRootScrollInfo =
    quirksMode ? doc->GetBodyElement() : doc->GetRootElement();
  if (this == elementWithRootScrollInfo) {
    // In quirks mode, the scroll info for the body element should map to the
    // root scrollable frame.
    // In strict mode, the scroll info for the root element should map to the
    // the root scrollable frame.
    return frame->PresContext()->PresShell()->GetRootScrollFrameAsScrollable();
  }

  return nullptr;
}

void
Element::ScrollIntoView(bool aTop)
{
  nsIDocument *document = GetCurrentDoc();
  if (!document) {
    return;
  }

  // Get the presentation shell
  nsCOMPtr<nsIPresShell> presShell = document->GetShell();
  if (!presShell) {
    return;
  }

  int16_t vpercent = aTop ? nsIPresShell::SCROLL_TOP :
    nsIPresShell::SCROLL_BOTTOM;

  presShell->ScrollContentIntoView(this,
                                   nsIPresShell::ScrollAxis(
                                     vpercent,
                                     nsIPresShell::SCROLL_ALWAYS),
                                   nsIPresShell::ScrollAxis(),
                                   nsIPresShell::SCROLL_OVERFLOW_HIDDEN);
}

int32_t
Element::ScrollHeight()
{
  if (IsSVG())
    return 0;

  nsIScrollableFrame* sf = GetScrollFrame();
  if (!sf) {
    return GetPaddingRectSize().height;
  }

  nscoord height = sf->GetScrollRange().height + sf->GetScrollPortRect().height;
  return nsPresContext::AppUnitsToIntCSSPixels(height);
}

int32_t
Element::ScrollWidth()
{
  if (IsSVG())
    return 0;

  nsIScrollableFrame* sf = GetScrollFrame();
  if (!sf) {
    return GetPaddingRectSize().width;
  }

  nscoord width = sf->GetScrollRange().width + sf->GetScrollPortRect().width;
  return nsPresContext::AppUnitsToIntCSSPixels(width);
}

nsRect
Element::GetClientAreaRect()
{
  nsIFrame* styledFrame;
  nsIScrollableFrame* sf = GetScrollFrame(&styledFrame);

  if (sf) {
    return sf->GetScrollPortRect();
  }

  if (styledFrame &&
      (styledFrame->GetStyleDisplay()->mDisplay != NS_STYLE_DISPLAY_INLINE ||
       styledFrame->IsFrameOfType(nsIFrame::eReplaced))) {
    // Special case code to make client area work even when there isn't
    // a scroll view, see bug 180552, bug 227567.
    return styledFrame->GetPaddingRect() - styledFrame->GetPositionIgnoringScrolling();
  }

  // SVG nodes reach here and just return 0
  return nsRect(0, 0, 0, 0);
}

already_AddRefed<nsClientRect>
Element::GetBoundingClientRect()
{
  nsRefPtr<nsClientRect> rect = new nsClientRect();
  
  nsIFrame* frame = GetPrimaryFrame(Flush_Layout);
  if (!frame) {
    // display:none, perhaps? Return the empty rect
    return rect.forget();
  }

  nsRect r = nsLayoutUtils::GetAllInFlowRectsUnion(frame,
          nsLayoutUtils::GetContainingBlockForClientRect(frame),
          nsLayoutUtils::RECTS_ACCOUNT_FOR_TRANSFORMS);
  rect->SetLayoutRect(r);
  return rect.forget();
}

already_AddRefed<nsClientRectList>
Element::GetClientRects(ErrorResult& aError)
{
  nsRefPtr<nsClientRectList> rectList = new nsClientRectList(this);

  nsIFrame* frame = GetPrimaryFrame(Flush_Layout);
  if (!frame) {
    // display:none, perhaps? Return an empty list
    return rectList.forget();
  }

  nsLayoutUtils::RectListBuilder builder(rectList);
  nsLayoutUtils::GetAllInFlowRects(frame,
          nsLayoutUtils::GetContainingBlockForClientRect(frame), &builder,
          nsLayoutUtils::RECTS_ACCOUNT_FOR_TRANSFORMS);
  if (NS_FAILED(builder.mRV)) {
    aError.Throw(builder.mRV);
    return nullptr;
  }
  return rectList.forget();
}


//----------------------------------------------------------------------


void
Element::GetAttribute(const nsAString& aName, nsString& aReturn)
{
  const nsAttrValue* val =
    mAttrsAndChildren.GetAttr(aName,
                              IsHTML() && IsInHTMLDocument() ?
                                eIgnoreCase : eCaseMatters);
  if (val) {
    val->ToString(aReturn);
  } else {
    if (IsXUL()) {
      // XXX should be SetDOMStringToNull(aReturn);
      // See bug 232598
      aReturn.Truncate();
    } else {
      SetDOMStringToNull(aReturn);
    }
  }
}

void
Element::SetAttribute(const nsAString& aName,
                      const nsAString& aValue,
                      ErrorResult& aError)
{
  const nsAttrName* name = InternalGetExistingAttrNameFromQName(aName);

  if (!name) {
    aError = nsContentUtils::CheckQName(aName, false);
    if (aError.Failed()) {
      return;
    }

    nsCOMPtr<nsIAtom> nameAtom;
    if (IsHTML() && IsInHTMLDocument()) {
      nsAutoString lower;
      nsresult rv = nsContentUtils::ASCIIToLower(aName, lower);
      if (NS_SUCCEEDED(rv)) {
        nameAtom = do_GetAtom(lower);
      }
    }
    else {
      nameAtom = do_GetAtom(aName);
    }
    if (!nameAtom) {
      aError.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
    aError = SetAttr(kNameSpaceID_None, nameAtom, aValue, true);
    return;
  }

  aError = SetAttr(name->NamespaceID(), name->LocalName(), name->GetPrefix(),
                   aValue, true);
  return;
}

void
Element::RemoveAttribute(const nsAString& aName, ErrorResult& aError)
{
  const nsAttrName* name = InternalGetExistingAttrNameFromQName(aName);

  if (!name) {
    // If there is no canonical nsAttrName for this attribute name, then the
    // attribute does not exist and we can't get its namespace ID and
    // local name below, so we return early.
    return;
  }

  // Hold a strong reference here so that the atom or nodeinfo doesn't go
  // away during UnsetAttr. If it did UnsetAttr would be left with a
  // dangling pointer as argument without knowing it.
  nsAttrName tmp(*name);

  aError = UnsetAttr(name->NamespaceID(), name->LocalName(), true);
}

nsIDOMAttr*
Element::GetAttributeNode(const nsAString& aName)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eGetAttributeNode);

  nsDOMAttributeMap* map = GetAttributes();
  if (!map) {
    return nullptr;
  }

  return map->GetNamedItem(aName);
}

already_AddRefed<nsIDOMAttr>
Element::SetAttributeNode(nsIDOMAttr* aNewAttr, ErrorResult& aError)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eSetAttributeNode);

  nsDOMAttributeMap* map = GetAttributes();
  if (!map) {
    // XXX Throw?
    return nullptr;
  }

  nsCOMPtr<nsIDOMNode> returnNode;
  aError = map->SetNamedItem(aNewAttr, getter_AddRefs(returnNode));
  if (aError.Failed()) {
    return nullptr;
  }

  return static_cast<nsIDOMAttr*>(returnNode.forget().get());
}

already_AddRefed<nsIDOMAttr>
Element::RemoveAttributeNode(nsIDOMAttr* aAttribute,
                             ErrorResult& aError)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eRemoveAttributeNode);

  nsDOMAttributeMap* map = GetAttributes();
  if (!map) {
    // XXX Throw?
    return nullptr;
  }

  nsAutoString name;

  aError = aAttribute->GetName(name);
  if (aError.Failed()) {
    return nullptr;
  }

  nsCOMPtr<nsIDOMNode> node;
  aError = map->RemoveNamedItem(name, getter_AddRefs(node));
  if (aError.Failed()) {
    return nullptr;
  }

  return static_cast<nsIDOMAttr*>(node.forget().get());
}

void
Element::GetAttributeNS(const nsAString& aNamespaceURI,
                        const nsAString& aLocalName,
                        nsAString& aReturn)
{
  int32_t nsid =
    nsContentUtils::NameSpaceManager()->GetNameSpaceID(aNamespaceURI);

  if (nsid == kNameSpaceID_Unknown) {
    // Unknown namespace means no attribute.
    SetDOMStringToNull(aReturn);
    return;
  }

  nsCOMPtr<nsIAtom> name = do_GetAtom(aLocalName);
  bool hasAttr = GetAttr(nsid, name, aReturn);
  if (!hasAttr) {
    SetDOMStringToNull(aReturn);
  }
}

void
Element::SetAttributeNS(const nsAString& aNamespaceURI,
                        const nsAString& aQualifiedName,
                        const nsAString& aValue,
                        ErrorResult& aError)
{
  nsCOMPtr<nsINodeInfo> ni;
  aError =
    nsContentUtils::GetNodeInfoFromQName(aNamespaceURI, aQualifiedName,
                                         mNodeInfo->NodeInfoManager(),
                                         nsIDOMNode::ATTRIBUTE_NODE,
                                         getter_AddRefs(ni));
  if (aError.Failed()) {
    return;
  }

  aError = SetAttr(ni->NamespaceID(), ni->NameAtom(), ni->GetPrefixAtom(),
                   aValue, true);
}

void
Element::RemoveAttributeNS(const nsAString& aNamespaceURI,
                           const nsAString& aLocalName,
                           ErrorResult& aError)
{
  nsCOMPtr<nsIAtom> name = do_GetAtom(aLocalName);
  int32_t nsid =
    nsContentUtils::NameSpaceManager()->GetNameSpaceID(aNamespaceURI);

  if (nsid == kNameSpaceID_Unknown) {
    // If the namespace ID is unknown, it means there can't possibly be an
    // existing attribute. We would need a known namespace ID to pass into
    // UnsetAttr, so we return early if we don't have one.
    return;
  }

  aError = UnsetAttr(nsid, name, true);
}

nsIDOMAttr*
Element::GetAttributeNodeNS(const nsAString& aNamespaceURI,
                            const nsAString& aLocalName,
                            ErrorResult& aError)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eGetAttributeNodeNS);

  return GetAttributeNodeNSInternal(aNamespaceURI, aLocalName, aError);
}

nsIDOMAttr*
Element::GetAttributeNodeNSInternal(const nsAString& aNamespaceURI,
                                    const nsAString& aLocalName,
                                    ErrorResult& aError)
{
  nsDOMAttributeMap* map = GetAttributes();
  if (!map) {
    return nullptr;
  }

  return map->GetNamedItemNS(aNamespaceURI, aLocalName, aError);
}

already_AddRefed<nsIDOMAttr>
Element::SetAttributeNodeNS(nsIDOMAttr* aNewAttr,
                            ErrorResult& aError)
{
  OwnerDoc()->WarnOnceAbout(nsIDocument::eSetAttributeNodeNS);

  nsDOMAttributeMap* map = GetAttributes();
  if (!map) {
    return nullptr;
  }

  return map->SetNamedItemNS(aNewAttr, aError);
}

already_AddRefed<nsIHTMLCollection>
Element::GetElementsByTagNameNS(const nsAString& aNamespaceURI,
                                const nsAString& aLocalName,
                                ErrorResult& aError)
{
  int32_t nameSpaceId = kNameSpaceID_Wildcard;

  if (!aNamespaceURI.EqualsLiteral("*")) {
    aError =
      nsContentUtils::NameSpaceManager()->RegisterNameSpace(aNamespaceURI,
                                                            nameSpaceId);
    if (aError.Failed()) {
      return nullptr;
    }
  }

  NS_ASSERTION(nameSpaceId != kNameSpaceID_Unknown, "Unexpected namespace ID!");

  return NS_GetContentList(this, nameSpaceId, aLocalName);
}

nsresult
Element::GetElementsByTagNameNS(const nsAString& namespaceURI,
                                const nsAString& localName,
                                nsIDOMHTMLCollection** aResult)
{
  mozilla::ErrorResult rv;
  nsCOMPtr<nsIHTMLCollection> list =
    GetElementsByTagNameNS(namespaceURI, localName, rv);
  if (rv.Failed()) {
    return rv.ErrorCode();
  }
  list.forget(aResult);
  return NS_OK;
}

bool
Element::HasAttributeNS(const nsAString& aNamespaceURI,
                        const nsAString& aLocalName) const
{
  int32_t nsid =
    nsContentUtils::NameSpaceManager()->GetNameSpaceID(aNamespaceURI);

  if (nsid == kNameSpaceID_Unknown) {
    // Unknown namespace means no attr...
    return false;
  }

  nsCOMPtr<nsIAtom> name = do_GetAtom(aLocalName);
  return HasAttr(nsid, name);
}

static nsXBLBinding*
GetFirstBindingWithContent(nsBindingManager* aBmgr, nsIContent* aBoundElem)
{
  nsXBLBinding* binding = aBmgr->GetBinding(aBoundElem);
  while (binding) {
    if (binding->GetAnonymousContent()) {
      return binding;
    }
    binding = binding->GetBaseBinding();
  }
  
  return nullptr;
}

static nsresult
BindNodesInInsertPoints(nsXBLBinding* aBinding, nsIContent* aInsertParent,
                        nsIDocument* aDocument)
{
  NS_PRECONDITION(aBinding && aInsertParent, "Missing arguments");

  nsresult rv;
  // These should be refcounted or otherwise protectable.
  nsInsertionPointList* inserts =
    aBinding->GetExistingInsertionPointsFor(aInsertParent);
  if (inserts) {
    bool allowScripts = aBinding->AllowScripts();
#ifdef MOZ_XUL
    nsCOMPtr<nsIXULDocument> xulDoc = do_QueryInterface(aDocument);
#endif
    uint32_t i;
    for (i = 0; i < inserts->Length(); ++i) {
      nsCOMPtr<nsIContent> insertRoot =
        inserts->ElementAt(i)->GetDefaultContent();
      if (insertRoot) {
        for (nsCOMPtr<nsIContent> child = insertRoot->GetFirstChild();
             child;
             child = child->GetNextSibling()) {
          rv = child->BindToTree(aDocument, aInsertParent,
                                 aBinding->GetBoundElement(), allowScripts);
          NS_ENSURE_SUCCESS(rv, rv);

#ifdef MOZ_XUL
          if (xulDoc) {
            xulDoc->AddSubtreeToDocument(child);
          }
#endif
        }
      }
    }
  }

  return NS_OK;
}

already_AddRefed<nsIHTMLCollection>
Element::GetElementsByClassName(const nsAString& aClassNames)
{
  return nsContentUtils::GetElementsByClassName(this, aClassNames);
}

nsresult
Element::GetElementsByClassName(const nsAString& aClassNames,
                                nsIDOMHTMLCollection** aResult)
{
  *aResult = nsContentUtils::GetElementsByClassName(this, aClassNames).get();
  return NS_OK;
}

nsresult
Element::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                    nsIContent* aBindingParent,
                    bool aCompileEventHandlers)
{
  NS_PRECONDITION(aParent || aDocument, "Must have document if no parent!");
  NS_PRECONDITION(HasSameOwnerDoc(NODE_FROM(aParent, aDocument)),
                  "Must have the same owner document");
  NS_PRECONDITION(!aParent || aDocument == aParent->GetCurrentDoc(),
                  "aDocument must be current doc of aParent");
  NS_PRECONDITION(!GetCurrentDoc(), "Already have a document.  Unbind first!");
  // Note that as we recurse into the kids, they'll have a non-null parent.  So
  // only assert if our parent is _changing_ while we have a parent.
  NS_PRECONDITION(!GetParent() || aParent == GetParent(),
                  "Already have a parent.  Unbind first!");
  NS_PRECONDITION(!GetBindingParent() ||
                  aBindingParent == GetBindingParent() ||
                  (!aBindingParent && aParent &&
                   aParent->GetBindingParent() == GetBindingParent()),
                  "Already have a binding parent.  Unbind first!");
  NS_PRECONDITION(!aParent || !aDocument ||
                  !aParent->HasFlag(NODE_FORCE_XBL_BINDINGS),
                  "Parent in document but flagged as forcing XBL");
  NS_PRECONDITION(aBindingParent != this,
                  "Content must not be its own binding parent");
  NS_PRECONDITION(!IsRootOfNativeAnonymousSubtree() ||
                  aBindingParent == aParent,
                  "Native anonymous content must have its parent as its "
                  "own binding parent");
  NS_PRECONDITION(aBindingParent || !aParent ||
                  aBindingParent == aParent->GetBindingParent(),
                  "We should be passed the right binding parent");

#ifdef MOZ_XUL
  // First set the binding parent
  nsXULElement* xulElem = nsXULElement::FromContent(this);
  if (xulElem) {
    xulElem->SetXULBindingParent(aBindingParent);
  }
  else 
#endif
  {
    if (aBindingParent) {
      nsDOMSlots *slots = DOMSlots();

      slots->mBindingParent = aBindingParent; // Weak, so no addref happens.
    }
  }
  NS_ASSERTION(!aBindingParent || IsRootOfNativeAnonymousSubtree() ||
               !HasFlag(NODE_IS_IN_ANONYMOUS_SUBTREE) ||
               (aParent && aParent->IsInNativeAnonymousSubtree()),
               "Trying to re-bind content from native anonymous subtree to "
               "non-native anonymous parent!");
  if (aParent) {
    if (aParent->IsInNativeAnonymousSubtree()) {
      SetFlags(NODE_IS_IN_ANONYMOUS_SUBTREE);
    }
    if (aParent->HasFlag(NODE_CHROME_ONLY_ACCESS)) {
      SetFlags(NODE_CHROME_ONLY_ACCESS);
    }
  }

  bool hadForceXBL = HasFlag(NODE_FORCE_XBL_BINDINGS);

  // Now set the parent and set the "Force attach xbl" flag if needed.
  if (aParent) {
    if (!GetParent()) {
      NS_ADDREF(aParent);
    }
    mParent = aParent;

    if (aParent->HasFlag(NODE_FORCE_XBL_BINDINGS)) {
      SetFlags(NODE_FORCE_XBL_BINDINGS);
    }
  }
  else {
    mParent = aDocument;
  }
  SetParentIsContent(aParent);

  // XXXbz sXBL/XBL2 issue!

  // Finally, set the document
  if (aDocument) {
    // Notify XBL- & nsIAnonymousContentCreator-generated
    // anonymous content that the document is changing.
    // XXXbz ordering issues here?  Probably not, since ChangeDocumentFor is
    // just pretty broken anyway....  Need to get it working.
    // XXXbz XBL doesn't handle this (asserts), and we don't really want
    // to be doing this during parsing anyway... sort this out.    
    //    aDocument->BindingManager()->ChangeDocumentFor(this, nullptr,
    //                                                   aDocument);

    // We no longer need to track the subtree pointer (and in fact we'll assert
    // if we do this any later).
    ClearSubtreeRootPointer();

    // Being added to a document.
    SetInDocument();

    // Unset this flag since we now really are in a document.
    UnsetFlags(NODE_FORCE_XBL_BINDINGS |
               // And clear the lazy frame construction bits.
               NODE_NEEDS_FRAME | NODE_DESCENDANTS_NEED_FRAMES |
               // And the restyle bits
               ELEMENT_ALL_RESTYLE_FLAGS);
  } else {
    // If we're not in the doc, update our subtree pointer.
    SetSubtreeRootPointer(aParent->SubtreeRoot());
  }

  // This has to be here, rather than in nsGenericHTMLElement::BindToTree, 
  //  because it has to happen after updating the parent pointer, but before
  //  recursively binding the kids.
  if (IsHTML()) {
    if (aParent && aParent->NodeOrAncestorHasDirAuto()) {
      SetAncestorHasDirAuto();
      // if we are binding an element to the tree that already has descendants,
      // and the parent has NodeHasDirAuto or NodeAncestorHasDirAuto, we may
      // need to reset the direction of an ancestor with dir=auto
      if (GetFirstChild()) {
        WalkAncestorsResetAutoDirection(this);
      }
    }

    if (!HasDirAuto()) {
      // if the element doesn't have dir=auto, set its directionality from
      // the dir attribute or by inheriting from its ancestors.
      RecomputeDirectionality(this, false);
    }
  }

  // If NODE_FORCE_XBL_BINDINGS was set we might have anonymous children
  // that also need to be told that they are moving.
  nsresult rv;
  if (hadForceXBL) {
    nsBindingManager* bmgr = OwnerDoc()->BindingManager();

    // First check if we have a binding...
    nsXBLBinding* contBinding =
      GetFirstBindingWithContent(bmgr, this);
    if (contBinding) {
      nsCOMPtr<nsIContent> anonRoot = contBinding->GetAnonymousContent();
      bool allowScripts = contBinding->AllowScripts();
      for (nsCOMPtr<nsIContent> child = anonRoot->GetFirstChild();
           child;
           child = child->GetNextSibling()) {
        rv = child->BindToTree(aDocument, this, this, allowScripts);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // ...then check if we have content in insertion points that are
      // direct children of the <content>
      rv = BindNodesInInsertPoints(contBinding, this, aDocument);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // ...and finally check if we're in a binding where we have content in
    // insertion points.
    if (aBindingParent) {
      nsXBLBinding* binding = bmgr->GetBinding(aBindingParent);
      if (binding) {
        rv = BindNodesInInsertPoints(binding, this, aDocument);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  }

  UpdateEditableState(false);

  // Now recurse into our kids
  for (nsIContent* child = GetFirstChild(); child;
       child = child->GetNextSibling()) {
    rv = child->BindToTree(aDocument, this, aBindingParent,
                           aCompileEventHandlers);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsNodeUtils::ParentChainChanged(this);

  if (aDocument && HasID() && !aBindingParent) {
    aDocument->AddToIdTable(this, DoGetID());
  }

  if (MayHaveStyle() && !IsXUL()) {
    // XXXbz if we already have a style attr parsed, this won't do
    // anything... need to fix that.
    // If MayHaveStyle() is true, we must be an nsStyledElement
    static_cast<nsStyledElement*>(this)->ReparseStyleAttribute(false);
  }

  if (aDocument) {
    // If we're in a document now, let our mapped attrs know what their new
    // sheet is.  This is safe to run for non-mapped-attribute elements too;
    // it'll just do a small bit of unnecessary work.  But most elements in
    // practice are mapped-attribute elements.
    nsHTMLStyleSheet* sheet = aDocument->GetAttributeStyleSheet();
    if (sheet) {
      mAttrsAndChildren.SetMappedAttrStyleSheet(sheet);
    }
  }

  // XXXbz script execution during binding can trigger some of these
  // postcondition asserts....  But we do want that, since things will
  // generally be quite broken when that happens.
  NS_POSTCONDITION(aDocument == GetCurrentDoc(), "Bound to wrong document");
  NS_POSTCONDITION(aParent == GetParent(), "Bound to wrong parent");
  NS_POSTCONDITION(aBindingParent == GetBindingParent(),
                   "Bound to wrong binding parent");

  return NS_OK;
}

class RemoveFromBindingManagerRunnable : public nsRunnable {
public:
  RemoveFromBindingManagerRunnable(nsBindingManager* aManager,
                                   Element* aElement,
                                   nsIDocument* aDoc,
                                   nsIContent* aBindingParent):
    mManager(aManager), mElement(aElement), mDoc(aDoc),
    mBindingParent(aBindingParent)
  {}

  NS_IMETHOD Run()
  {
    mManager->RemovedFromDocumentInternal(mElement, mDoc, mBindingParent);
    return NS_OK;
  }

private:
  nsRefPtr<nsBindingManager> mManager;
  nsRefPtr<Element> mElement;
  nsCOMPtr<nsIDocument> mDoc;
  nsCOMPtr<nsIContent> mBindingParent;
};

void
Element::UnbindFromTree(bool aDeep, bool aNullParent)
{
  NS_PRECONDITION(aDeep || (!GetCurrentDoc() && !GetBindingParent()),
                  "Shallow unbind won't clear document and binding parent on "
                  "kids!");

  RemoveFromIdTable();

  // Make sure to unbind this node before doing the kids
  nsIDocument *document =
    HasFlag(NODE_FORCE_XBL_BINDINGS) ? OwnerDoc() : GetCurrentDoc();

  if (aNullParent) {
    if (IsFullScreenAncestor()) {
      // The element being removed is an ancestor of the full-screen element,
      // exit full-screen state.
      nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                      "DOM", OwnerDoc(),
                                      nsContentUtils::eDOM_PROPERTIES,
                                      "RemovedFullScreenElement");
      // Fully exit full-screen.
      nsIDocument::ExitFullScreen(false);
    }
    if (HasPointerLock()) {
      nsIDocument::UnlockPointer();
    }
    if (GetParent()) {
      NS_RELEASE(mParent);
    } else {
      mParent = nullptr;
    }
    SetParentIsContent(false);
  }
  ClearInDocument();

  // Begin keeping track of our subtree root.
  SetSubtreeRootPointer(aNullParent ? this : mParent->SubtreeRoot());

  if (document) {
    // Notify XBL- & nsIAnonymousContentCreator-generated
    // anonymous content that the document is changing.
    if (HasFlag(NODE_MAY_BE_IN_BINDING_MNGR)) {
      nsContentUtils::AddScriptRunner(
        new RemoveFromBindingManagerRunnable(document->BindingManager(), this,
                                             document, GetBindingParent()));
    }

    document->ClearBoxObjectFor(this);
  }

  // Ensure that CSS transitions don't continue on an element at a
  // different place in the tree (even if reinserted before next
  // animation refresh).
  // FIXME (Bug 522599): Need a test for this.
  if (HasFlag(NODE_HAS_PROPERTIES)) {
    DeleteProperty(nsGkAtoms::transitionsOfBeforeProperty);
    DeleteProperty(nsGkAtoms::transitionsOfAfterProperty);
    DeleteProperty(nsGkAtoms::transitionsProperty);
    DeleteProperty(nsGkAtoms::animationsOfBeforeProperty);
    DeleteProperty(nsGkAtoms::animationsOfAfterProperty);
    DeleteProperty(nsGkAtoms::animationsProperty);
  }

  // Unset this since that's what the old code effectively did.
  UnsetFlags(NODE_FORCE_XBL_BINDINGS);
  
#ifdef MOZ_XUL
  nsXULElement* xulElem = nsXULElement::FromContent(this);
  if (xulElem) {
    xulElem->SetXULBindingParent(nullptr);
  }
  else
#endif
  {
    nsDOMSlots *slots = GetExistingDOMSlots();
    if (slots) {
      slots->mBindingParent = nullptr;
    }
  }

  // This has to be here, rather than in nsGenericHTMLElement::UnbindFromTree, 
  //  because it has to happen after unsetting the parent pointer, but before
  //  recursively unbinding the kids.
  if (IsHTML() && !HasDirAuto()) {
    RecomputeDirectionality(this, false);
  }

  if (aDeep) {
    // Do the kids. Don't call GetChildCount() here since that'll force
    // XUL to generate template children, which there is no need for since
    // all we're going to do is unbind them anyway.
    uint32_t i, n = mAttrsAndChildren.ChildCount();

    for (i = 0; i < n; ++i) {
      // Note that we pass false for aNullParent here, since we don't want
      // the kids to forget us.  We _do_ want them to forget their binding
      // parent, though, since this only walks non-anonymous kids.
      mAttrsAndChildren.ChildAt(i)->UnbindFromTree(true, false);
    }
  }

  nsNodeUtils::ParentChainChanged(this);
}

nsICSSDeclaration*
Element::GetSMILOverrideStyle()
{
  Element::nsDOMSlots *slots = DOMSlots();

  if (!slots->mSMILOverrideStyle) {
    slots->mSMILOverrideStyle = new nsDOMCSSAttributeDeclaration(this, true);
  }

  return slots->mSMILOverrideStyle;
}

css::StyleRule*
Element::GetSMILOverrideStyleRule()
{
  Element::nsDOMSlots *slots = GetExistingDOMSlots();
  return slots ? slots->mSMILOverrideStyleRule.get() : nullptr;
}

nsresult
Element::SetSMILOverrideStyleRule(css::StyleRule* aStyleRule,
                                           bool aNotify)
{
  Element::nsDOMSlots *slots = DOMSlots();

  slots->mSMILOverrideStyleRule = aStyleRule;

  if (aNotify) {
    nsIDocument* doc = GetCurrentDoc();
    // Only need to request a restyle if we're in a document.  (We might not
    // be in a document, if we're clearing animation effects on a target node
    // that's been detached since the previous animation sample.)
    if (doc) {
      nsCOMPtr<nsIPresShell> shell = doc->GetShell();
      if (shell) {
        shell->RestyleForAnimation(this, eRestyle_Self);
      }
    }
  }

  return NS_OK;
}

bool
Element::IsLabelable() const
{
  return false;
}

css::StyleRule*
Element::GetInlineStyleRule()
{
  return nullptr;
}

nsresult
Element::SetInlineStyleRule(css::StyleRule* aStyleRule,
                            const nsAString* aSerialized,
                            bool aNotify)
{
  NS_NOTYETIMPLEMENTED("Element::SetInlineStyleRule");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP_(bool)
Element::IsAttributeMapped(const nsIAtom* aAttribute) const
{
  return false;
}

nsChangeHint
Element::GetAttributeChangeHint(const nsIAtom* aAttribute,
                                int32_t aModType) const
{
  return nsChangeHint(0);
}

nsIAtom *
Element::GetClassAttributeName() const
{
  return nullptr;
}

bool
Element::FindAttributeDependence(const nsIAtom* aAttribute,
                                 const MappedAttributeEntry* const aMaps[],
                                 uint32_t aMapCount)
{
  for (uint32_t mapindex = 0; mapindex < aMapCount; ++mapindex) {
    for (const MappedAttributeEntry* map = aMaps[mapindex];
         map->attribute; ++map) {
      if (aAttribute == *map->attribute) {
        return true;
      }
    }
  }

  return false;
}

already_AddRefed<nsINodeInfo>
Element::GetExistingAttrNameFromQName(const nsAString& aStr) const
{
  const nsAttrName* name = InternalGetExistingAttrNameFromQName(aStr);
  if (!name) {
    return nullptr;
  }

  nsINodeInfo* nodeInfo;
  if (name->IsAtom()) {
    nodeInfo = mNodeInfo->NodeInfoManager()->
      GetNodeInfo(name->Atom(), nullptr, kNameSpaceID_None,
                  nsIDOMNode::ATTRIBUTE_NODE).get();
  }
  else {
    NS_ADDREF(nodeInfo = name->NodeInfo());
  }

  return nodeInfo;
}

NS_IMETHODIMP
Element::GetAttributes(nsIDOMNamedNodeMap** aAttributes)
{
  nsDOMSlots *slots = DOMSlots();

  if (!slots->mAttributeMap) {
    slots->mAttributeMap = new nsDOMAttributeMap(this);
  }

  NS_ADDREF(*aAttributes = slots->mAttributeMap);

  return NS_OK;
}

// static
bool
Element::ShouldBlur(nsIContent *aContent)
{
  // Determine if the current element is focused, if it is not focused
  // then we should not try to blur
  nsIDocument *document = aContent->GetDocument();
  if (!document)
    return false;

  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(document->GetWindow());
  if (!window)
    return false;

  nsCOMPtr<nsPIDOMWindow> focusedFrame;
  nsIContent* contentToBlur =
    nsFocusManager::GetFocusedDescendant(window, false, getter_AddRefs(focusedFrame));
  if (contentToBlur == aContent)
    return true;

  // if focus on this element would get redirected, then check the redirected
  // content as well when blurring.
  return (contentToBlur && nsFocusManager::GetRedirectedFocus(aContent) == contentToBlur);
}

bool
Element::IsNodeOfType(uint32_t aFlags) const
{
  return !(aFlags & ~eCONTENT);
}

/* static */
nsresult
Element::DispatchEvent(nsPresContext* aPresContext,
                       nsEvent* aEvent,
                       nsIContent* aTarget,
                       bool aFullDispatch,
                       nsEventStatus* aStatus)
{
  NS_PRECONDITION(aTarget, "Must have target");
  NS_PRECONDITION(aEvent, "Must have source event");
  NS_PRECONDITION(aStatus, "Null out param?");

  if (!aPresContext) {
    return NS_OK;
  }

  nsCOMPtr<nsIPresShell> shell = aPresContext->GetPresShell();
  if (!shell) {
    return NS_OK;
  }

  if (aFullDispatch) {
    return shell->HandleEventWithTarget(aEvent, nullptr, aTarget, aStatus);
  }

  return shell->HandleDOMEventWithTarget(aTarget, aEvent, aStatus);
}

/* static */
nsresult
Element::DispatchClickEvent(nsPresContext* aPresContext,
                            nsInputEvent* aSourceEvent,
                            nsIContent* aTarget,
                            bool aFullDispatch,
                            const widget::EventFlags* aExtraEventFlags,
                            nsEventStatus* aStatus)
{
  NS_PRECONDITION(aTarget, "Must have target");
  NS_PRECONDITION(aSourceEvent, "Must have source event");
  NS_PRECONDITION(aStatus, "Null out param?");

  nsMouseEvent event(aSourceEvent->mFlags.mIsTrusted, NS_MOUSE_CLICK,
                     aSourceEvent->widget, nsMouseEvent::eReal);
  event.refPoint = aSourceEvent->refPoint;
  uint32_t clickCount = 1;
  float pressure = 0;
  uint16_t inputSource = 0;
  if (aSourceEvent->eventStructType == NS_MOUSE_EVENT) {
    clickCount = static_cast<nsMouseEvent*>(aSourceEvent)->clickCount;
    pressure = static_cast<nsMouseEvent*>(aSourceEvent)->pressure;
    inputSource = static_cast<nsMouseEvent*>(aSourceEvent)->inputSource;
  } else if (aSourceEvent->eventStructType == NS_KEY_EVENT) {
    inputSource = nsIDOMMouseEvent::MOZ_SOURCE_KEYBOARD;
  }
  event.pressure = pressure;
  event.clickCount = clickCount;
  event.inputSource = inputSource;
  event.modifiers = aSourceEvent->modifiers;
  if (aExtraEventFlags) {
    // Be careful not to overwrite existing flags!
    event.mFlags |= *aExtraEventFlags;
  }

  return DispatchEvent(aPresContext, &event, aTarget, aFullDispatch, aStatus);
}

nsIFrame*
Element::GetPrimaryFrame(mozFlushType aType)
{
  nsIDocument* doc = GetCurrentDoc();
  if (!doc) {
    return nullptr;
  }

  // Cause a flush, so we get up-to-date frame
  // information
  doc->FlushPendingNotifications(aType);

  return GetPrimaryFrame();
}

//----------------------------------------------------------------------
nsresult
Element::LeaveLink(nsPresContext* aPresContext)
{
  nsILinkHandler *handler = aPresContext->GetLinkHandler();
  if (!handler) {
    return NS_OK;
  }

  return handler->OnLeaveLink();
}

nsresult
Element::SetEventHandler(nsIAtom* aEventName,
                         const nsAString& aValue,
                         bool aDefer)
{
  nsIDocument *ownerDoc = OwnerDoc();
  if (ownerDoc->IsLoadedAsData()) {
    // Make this a no-op rather than throwing an error to avoid
    // the error causing problems setting the attribute.
    return NS_OK;
  }

  NS_PRECONDITION(aEventName, "Must have event name!");
  bool defer = true;
  nsEventListenerManager* manager = GetEventListenerManagerForAttr(aEventName,
                                                                   &defer);
  if (!manager) {
    return NS_OK;
  }

  defer = defer && aDefer; // only defer if everyone agrees...
  manager->SetEventHandler(aEventName, aValue,
                           nsIProgrammingLanguage::JAVASCRIPT,
                           defer, !nsContentUtils::IsChromeDoc(ownerDoc));
  return NS_OK;
}


//----------------------------------------------------------------------

const nsAttrName*
Element::InternalGetExistingAttrNameFromQName(const nsAString& aStr) const
{
  return mAttrsAndChildren.GetExistingAttrNameFromQName(aStr);
}

bool
Element::MaybeCheckSameAttrVal(int32_t aNamespaceID,
                               nsIAtom* aName,
                               nsIAtom* aPrefix,
                               const nsAttrValueOrString& aValue,
                               bool aNotify,
                               nsAttrValue& aOldValue,
                               uint8_t* aModType,
                               bool* aHasListeners)
{
  bool modification = false;
  *aHasListeners = aNotify &&
    nsContentUtils::HasMutationListeners(this,
                                         NS_EVENT_BITS_MUTATION_ATTRMODIFIED,
                                         this);

  // If we have no listeners and aNotify is false, we are almost certainly
  // coming from the content sink and will almost certainly have no previous
  // value.  Even if we do, setting the value is cheap when we have no
  // listeners and don't plan to notify.  The check for aNotify here is an
  // optimization, the check for *aHasListeners is a correctness issue.
  if (*aHasListeners || aNotify) {
    nsAttrInfo info(GetAttrInfo(aNamespaceID, aName));
    if (info.mValue) {
      // Check whether the old value is the same as the new one.  Note that we
      // only need to actually _get_ the old value if we have listeners.
      if (*aHasListeners) {
        // Need to store the old value.
        //
        // If the current attribute value contains a pointer to some other data
        // structure that gets updated in the process of setting the attribute
        // we'll no longer have the old value of the attribute. Therefore, we
        // should serialize the attribute value now to keep a snapshot.
        //
        // We have to serialize the value anyway in order to create the
        // mutation event so there's no cost in doing it now.
        aOldValue.SetToSerialized(*info.mValue);
      }
      bool valueMatches = aValue.EqualsAsStrings(*info.mValue);
      if (valueMatches && aPrefix == info.mName->GetPrefix()) {
        return true;
      }
      modification = true;
    }
  }
  *aModType = modification ?
    static_cast<uint8_t>(nsIDOMMutationEvent::MODIFICATION) :
    static_cast<uint8_t>(nsIDOMMutationEvent::ADDITION);
  return false;
}

nsresult
Element::SetAttr(int32_t aNamespaceID, nsIAtom* aName,
                 nsIAtom* aPrefix, const nsAString& aValue,
                 bool aNotify)
{
  // Keep this in sync with SetParsedAttr below

  NS_ENSURE_ARG_POINTER(aName);
  NS_ASSERTION(aNamespaceID != kNameSpaceID_Unknown,
               "Don't call SetAttr with unknown namespace");

  if (!mAttrsAndChildren.CanFitMoreAttrs()) {
    return NS_ERROR_FAILURE;
  }

  uint8_t modType;
  bool hasListeners;
  nsAttrValueOrString value(aValue);
  nsAttrValue oldValue;

  if (OnlyNotifySameValueSet(aNamespaceID, aName, aPrefix, value, aNotify,
                             oldValue, &modType, &hasListeners)) {
    return NS_OK;
  }

  nsresult rv = BeforeSetAttr(aNamespaceID, aName, &value, aNotify);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aNotify) {
    nsNodeUtils::AttributeWillChange(this, aNamespaceID, aName, modType);
  }

  // Hold a script blocker while calling ParseAttribute since that can call
  // out to id-observers
  nsAutoScriptBlocker scriptBlocker;

  nsAttrValue attrValue;
  if (!ParseAttribute(aNamespaceID, aName, aValue, attrValue)) {
    attrValue.SetTo(aValue);
  }

  return SetAttrAndNotify(aNamespaceID, aName, aPrefix, oldValue,
                          attrValue, modType, hasListeners, aNotify,
                          kCallAfterSetAttr);
}

nsresult
Element::SetParsedAttr(int32_t aNamespaceID, nsIAtom* aName,
                       nsIAtom* aPrefix, nsAttrValue& aParsedValue,
                       bool aNotify)
{
  // Keep this in sync with SetAttr above

  NS_ENSURE_ARG_POINTER(aName);
  NS_ASSERTION(aNamespaceID != kNameSpaceID_Unknown,
               "Don't call SetAttr with unknown namespace");

  if (!mAttrsAndChildren.CanFitMoreAttrs()) {
    return NS_ERROR_FAILURE;
  }


  uint8_t modType;
  bool hasListeners;
  nsAttrValueOrString value(aParsedValue);
  nsAttrValue oldValue;

  if (OnlyNotifySameValueSet(aNamespaceID, aName, aPrefix, value, aNotify,
                             oldValue, &modType, &hasListeners)) {
    return NS_OK;
  }

  nsresult rv = BeforeSetAttr(aNamespaceID, aName, &value, aNotify);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aNotify) {
    nsNodeUtils::AttributeWillChange(this, aNamespaceID, aName, modType);
  }

  return SetAttrAndNotify(aNamespaceID, aName, aPrefix, oldValue,
                          aParsedValue, modType, hasListeners, aNotify,
                          kCallAfterSetAttr);
}

nsresult
Element::SetAttrAndNotify(int32_t aNamespaceID,
                          nsIAtom* aName,
                          nsIAtom* aPrefix,
                          const nsAttrValue& aOldValue,
                          nsAttrValue& aParsedValue,
                          uint8_t aModType,
                          bool aFireMutation,
                          bool aNotify,
                          bool aCallAfterSetAttr)
{
  nsresult rv;

  nsIDocument* document = GetCurrentDoc();
  mozAutoDocUpdate updateBatch(document, UPDATE_CONTENT_MODEL, aNotify);

  nsMutationGuard::DidMutate();

  // Copy aParsedValue for later use since it will be lost when we call
  // SetAndTakeMappedAttr below
  nsAttrValue aValueForAfterSetAttr;
  if (aCallAfterSetAttr) {
    aValueForAfterSetAttr.SetTo(aParsedValue);
  }

  bool hadValidDir = false;

  if (aNamespaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::dir) {
      hadValidDir = HasValidDir() || NodeInfo()->Equals(nsGkAtoms::bdi);
    }

    // XXXbz Perhaps we should push up the attribute mapping function
    // stuff to Element?
    if (!IsAttributeMapped(aName) ||
        !SetMappedAttribute(document, aName, aParsedValue, &rv)) {
      rv = mAttrsAndChildren.SetAndTakeAttr(aName, aParsedValue);
    }
  }
  else {
    nsCOMPtr<nsINodeInfo> ni;
    ni = mNodeInfo->NodeInfoManager()->GetNodeInfo(aName, aPrefix,
                                                   aNamespaceID,
                                                   nsIDOMNode::ATTRIBUTE_NODE);
    NS_ENSURE_TRUE(ni, NS_ERROR_OUT_OF_MEMORY);

    rv = mAttrsAndChildren.SetAndTakeAttr(ni, aParsedValue);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  if (document || HasFlag(NODE_FORCE_XBL_BINDINGS)) {
    nsRefPtr<nsXBLBinding> binding =
      OwnerDoc()->BindingManager()->GetBinding(this);
    if (binding) {
      binding->AttributeChanged(aName, aNamespaceID, false, aNotify);
    }
  }

  UpdateState(aNotify);

  if (aNotify) {
    nsNodeUtils::AttributeChanged(this, aNamespaceID, aName, aModType);
  }

  if (aCallAfterSetAttr) {
    rv = AfterSetAttr(aNamespaceID, aName, &aValueForAfterSetAttr, aNotify);
    NS_ENSURE_SUCCESS(rv, rv);

    if (aNamespaceID == kNameSpaceID_None && aName == nsGkAtoms::dir) {
      OnSetDirAttr(this, &aValueForAfterSetAttr, hadValidDir, aNotify);
    }
  }

  if (aFireMutation) {
    nsMutationEvent mutation(true, NS_MUTATION_ATTRMODIFIED);

    nsAutoString ns;
    nsContentUtils::NameSpaceManager()->GetNameSpaceURI(aNamespaceID, ns);
    ErrorResult rv;
    nsIDOMAttr* attrNode =
      GetAttributeNodeNSInternal(ns, nsDependentAtomString(aName), rv);
    mutation.mRelatedNode = attrNode;

    mutation.mAttrName = aName;
    nsAutoString newValue;
    GetAttr(aNamespaceID, aName, newValue);
    if (!newValue.IsEmpty()) {
      mutation.mNewAttrValue = do_GetAtom(newValue);
    }
    if (!aOldValue.IsEmptyString()) {
      mutation.mPrevAttrValue = aOldValue.GetAsAtom();
    }
    mutation.mAttrChange = aModType;

    mozAutoSubtreeModified subtree(OwnerDoc(), this);
    (new nsAsyncDOMEvent(this, mutation))->RunDOMEventWhenSafe();
  }

  return NS_OK;
}

bool
Element::ParseAttribute(int32_t aNamespaceID,
                        nsIAtom* aAttribute,
                        const nsAString& aValue,
                        nsAttrValue& aResult)
{
  return false;
}

bool
Element::SetMappedAttribute(nsIDocument* aDocument,
                            nsIAtom* aName,
                            nsAttrValue& aValue,
                            nsresult* aRetval)
{
  *aRetval = NS_OK;
  return false;
}

nsEventListenerManager*
Element::GetEventListenerManagerForAttr(nsIAtom* aAttrName,
                                        bool* aDefer)
{
  *aDefer = true;
  return GetListenerManager(true);
}

Element::nsAttrInfo
Element::GetAttrInfo(int32_t aNamespaceID, nsIAtom* aName) const
{
  NS_ASSERTION(nullptr != aName, "must have attribute name");
  NS_ASSERTION(aNamespaceID != kNameSpaceID_Unknown,
               "must have a real namespace ID!");

  int32_t index = mAttrsAndChildren.IndexOfAttr(aName, aNamespaceID);
  if (index >= 0) {
    return nsAttrInfo(mAttrsAndChildren.AttrNameAt(index),
                      mAttrsAndChildren.AttrAt(index));
  }

  return nsAttrInfo(nullptr, nullptr);
}
  

bool
Element::GetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                 nsAString& aResult) const
{
  NS_ASSERTION(nullptr != aName, "must have attribute name");
  NS_ASSERTION(aNameSpaceID != kNameSpaceID_Unknown,
               "must have a real namespace ID!");

  const nsAttrValue* val = mAttrsAndChildren.GetAttr(aName, aNameSpaceID);
  if (!val) {
    // Since we are returning a success code we'd better do
    // something about the out parameters (someone may have
    // given us a non-empty string).
    aResult.Truncate();
    
    return false;
  }

  val->ToString(aResult);

  return true;
}

bool
Element::HasAttr(int32_t aNameSpaceID, nsIAtom* aName) const
{
  NS_ASSERTION(nullptr != aName, "must have attribute name");
  NS_ASSERTION(aNameSpaceID != kNameSpaceID_Unknown,
               "must have a real namespace ID!");

  return mAttrsAndChildren.IndexOfAttr(aName, aNameSpaceID) >= 0;
}

bool
Element::AttrValueIs(int32_t aNameSpaceID,
                     nsIAtom* aName,
                     const nsAString& aValue,
                     nsCaseTreatment aCaseSensitive) const
{
  NS_ASSERTION(aName, "Must have attr name");
  NS_ASSERTION(aNameSpaceID != kNameSpaceID_Unknown, "Must have namespace");

  const nsAttrValue* val = mAttrsAndChildren.GetAttr(aName, aNameSpaceID);
  return val && val->Equals(aValue, aCaseSensitive);
}

bool
Element::AttrValueIs(int32_t aNameSpaceID,
                     nsIAtom* aName,
                     nsIAtom* aValue,
                     nsCaseTreatment aCaseSensitive) const
{
  NS_ASSERTION(aName, "Must have attr name");
  NS_ASSERTION(aNameSpaceID != kNameSpaceID_Unknown, "Must have namespace");
  NS_ASSERTION(aValue, "Null value atom");

  const nsAttrValue* val = mAttrsAndChildren.GetAttr(aName, aNameSpaceID);
  return val && val->Equals(aValue, aCaseSensitive);
}

int32_t
Element::FindAttrValueIn(int32_t aNameSpaceID,
                         nsIAtom* aName,
                         AttrValuesArray* aValues,
                         nsCaseTreatment aCaseSensitive) const
{
  NS_ASSERTION(aName, "Must have attr name");
  NS_ASSERTION(aNameSpaceID != kNameSpaceID_Unknown, "Must have namespace");
  NS_ASSERTION(aValues, "Null value array");
  
  const nsAttrValue* val = mAttrsAndChildren.GetAttr(aName, aNameSpaceID);
  if (val) {
    for (int32_t i = 0; aValues[i]; ++i) {
      if (val->Equals(*aValues[i], aCaseSensitive)) {
        return i;
      }
    }
    return ATTR_VALUE_NO_MATCH;
  }
  return ATTR_MISSING;
}

nsresult
Element::UnsetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                   bool aNotify)
{
  NS_ASSERTION(nullptr != aName, "must have attribute name");

  int32_t index = mAttrsAndChildren.IndexOfAttr(aName, aNameSpaceID);
  if (index < 0) {
    return NS_OK;
  }

  nsresult rv = BeforeSetAttr(aNameSpaceID, aName, nullptr, aNotify);
  NS_ENSURE_SUCCESS(rv, rv);

  nsIDocument *document = GetCurrentDoc();
  mozAutoDocUpdate updateBatch(document, UPDATE_CONTENT_MODEL, aNotify);

  if (aNotify) {
    nsNodeUtils::AttributeWillChange(this, aNameSpaceID, aName,
                                     nsIDOMMutationEvent::REMOVAL);
  }

  bool hasMutationListeners = aNotify &&
    nsContentUtils::HasMutationListeners(this,
                                         NS_EVENT_BITS_MUTATION_ATTRMODIFIED,
                                         this);

  // Grab the attr node if needed before we remove it from the attr map
  nsCOMPtr<nsIDOMAttr> attrNode;
  if (hasMutationListeners) {
    nsAutoString ns;
    nsContentUtils::NameSpaceManager()->GetNameSpaceURI(aNameSpaceID, ns);
    ErrorResult rv;
    attrNode = GetAttributeNodeNSInternal(ns, nsDependentAtomString(aName), rv);
  }

  // Clear binding to nsIDOMNamedNodeMap
  nsDOMSlots *slots = GetExistingDOMSlots();
  if (slots && slots->mAttributeMap) {
    slots->mAttributeMap->DropAttribute(aNameSpaceID, aName);
  }

  // The id-handling code, and in the future possibly other code, need to
  // react to unexpected attribute changes.
  nsMutationGuard::DidMutate();

  bool hadValidDir = false;

  if (aNameSpaceID == kNameSpaceID_None && aName == nsGkAtoms::dir) {
    hadValidDir = HasValidDir() || NodeInfo()->Equals(nsGkAtoms::bdi);
  }

  nsAttrValue oldValue;
  rv = mAttrsAndChildren.RemoveAttrAt(index, oldValue);
  NS_ENSURE_SUCCESS(rv, rv);

  if (document || HasFlag(NODE_FORCE_XBL_BINDINGS)) {
    nsRefPtr<nsXBLBinding> binding =
      OwnerDoc()->BindingManager()->GetBinding(this);
    if (binding) {
      binding->AttributeChanged(aName, aNameSpaceID, true, aNotify);
    }
  }

  UpdateState(aNotify);

  if (aNotify) {
    nsNodeUtils::AttributeChanged(this, aNameSpaceID, aName,
                                  nsIDOMMutationEvent::REMOVAL);
  }

  rv = AfterSetAttr(aNameSpaceID, aName, nullptr, aNotify);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aNameSpaceID == kNameSpaceID_None && aName == nsGkAtoms::dir) {
    OnSetDirAttr(this, nullptr, hadValidDir, aNotify);
  }

  if (hasMutationListeners) {
    nsCOMPtr<nsIDOMEventTarget> node = do_QueryObject(this);
    nsMutationEvent mutation(true, NS_MUTATION_ATTRMODIFIED);

    mutation.mRelatedNode = attrNode;
    mutation.mAttrName = aName;

    nsAutoString value;
    oldValue.ToString(value);
    if (!value.IsEmpty())
      mutation.mPrevAttrValue = do_GetAtom(value);
    mutation.mAttrChange = nsIDOMMutationEvent::REMOVAL;

    mozAutoSubtreeModified subtree(OwnerDoc(), this);
    (new nsAsyncDOMEvent(this, mutation))->RunDOMEventWhenSafe();
  }

  return NS_OK;
}

const nsAttrName*
Element::GetAttrNameAt(uint32_t aIndex) const
{
  return mAttrsAndChildren.GetSafeAttrNameAt(aIndex);
}

uint32_t
Element::GetAttrCount() const
{
  return mAttrsAndChildren.AttrCount();
}

#ifdef DEBUG
void
Element::ListAttributes(FILE* out) const
{
  uint32_t index, count = mAttrsAndChildren.AttrCount();
  for (index = 0; index < count; index++) {
    nsAutoString buffer;

    // name
    mAttrsAndChildren.AttrNameAt(index)->GetQualifiedName(buffer);

    // value
    buffer.AppendLiteral("=\"");
    nsAutoString value;
    mAttrsAndChildren.AttrAt(index)->ToString(value);
    for (int i = value.Length(); i >= 0; --i) {
      if (value[i] == PRUnichar('"'))
        value.Insert(PRUnichar('\\'), uint32_t(i));
    }
    buffer.Append(value);
    buffer.AppendLiteral("\"");

    fputs(" ", out);
    fputs(NS_LossyConvertUTF16toASCII(buffer).get(), out);
  }
}

void
Element::List(FILE* out, int32_t aIndent,
              const nsCString& aPrefix) const
{
  int32_t indent;
  for (indent = aIndent; --indent >= 0; ) fputs("  ", out);

  fputs(aPrefix.get(), out);

  fputs(NS_LossyConvertUTF16toASCII(mNodeInfo->QualifiedName()).get(), out);

  fprintf(out, "@%p", (void *)this);

  ListAttributes(out);

  fprintf(out, " state=[%llx]", State().GetInternalValue());
  fprintf(out, " flags=[%08x]", static_cast<unsigned int>(GetFlags()));
  if (IsCommonAncestorForRangeInSelection()) {
    nsRange::RangeHashTable* ranges =
      static_cast<nsRange::RangeHashTable*>(GetProperty(nsGkAtoms::range));
    fprintf(out, " ranges:%d", ranges ? ranges->Count() : 0);
  }
  fprintf(out, " primaryframe=%p", static_cast<void*>(GetPrimaryFrame()));
  fprintf(out, " refcount=%d<", mRefCnt.get());

  nsIContent* child = GetFirstChild();
  if (child) {
    fputs("\n", out);
    
    for (; child; child = child->GetNextSibling()) {
      child->List(out, aIndent + 1);
    }

    for (indent = aIndent; --indent >= 0; ) fputs("  ", out);
  }

  fputs(">\n", out);
  
  Element* nonConstThis = const_cast<Element*>(this);

  // XXX sXBL/XBL2 issue! Owner or current document?
  nsIDocument *document = OwnerDoc();

  // Note: not listing nsIAnonymousContentCreator-created content...

  nsBindingManager* bindingManager = document->BindingManager();
  nsCOMPtr<nsIDOMNodeList> anonymousChildren;
  bindingManager->GetAnonymousNodesFor(nonConstThis,
                                       getter_AddRefs(anonymousChildren));

  if (anonymousChildren) {
    uint32_t length;
    anonymousChildren->GetLength(&length);
    if (length > 0) {
      for (indent = aIndent; --indent >= 0; ) fputs("  ", out);
      fputs("anonymous-children<\n", out);

      for (uint32_t i = 0; i < length; ++i) {
        nsCOMPtr<nsIDOMNode> node;
        anonymousChildren->Item(i, getter_AddRefs(node));
        nsCOMPtr<nsIContent> child = do_QueryInterface(node);
        child->List(out, aIndent + 1);
      }

      for (indent = aIndent; --indent >= 0; ) fputs("  ", out);
      fputs(">\n", out);
    }
  }

  if (bindingManager->HasContentListFor(nonConstThis)) {
    nsCOMPtr<nsIDOMNodeList> contentList;
    bindingManager->GetContentListFor(nonConstThis,
                                      getter_AddRefs(contentList));

    NS_ASSERTION(contentList != nullptr, "oops, binding manager lied");
    
    uint32_t length;
    contentList->GetLength(&length);
    if (length > 0) {
      for (indent = aIndent; --indent >= 0; ) fputs("  ", out);
      fputs("content-list<\n", out);

      for (uint32_t i = 0; i < length; ++i) {
        nsCOMPtr<nsIDOMNode> node;
        contentList->Item(i, getter_AddRefs(node));
        nsCOMPtr<nsIContent> child = do_QueryInterface(node);
        child->List(out, aIndent + 1);
      }

      for (indent = aIndent; --indent >= 0; ) fputs("  ", out);
      fputs(">\n", out);
    }
  }
}

void
Element::DumpContent(FILE* out, int32_t aIndent,
                     bool aDumpAll) const
{
  int32_t indent;
  for (indent = aIndent; --indent >= 0; ) fputs("  ", out);

  const nsString& buf = mNodeInfo->QualifiedName();
  fputs("<", out);
  fputs(NS_LossyConvertUTF16toASCII(buf).get(), out);

  if(aDumpAll) ListAttributes(out);

  fputs(">", out);

  if(aIndent) fputs("\n", out);

  for (nsIContent* child = GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    int32_t indent = aIndent ? aIndent + 1 : 0;
    child->DumpContent(out, indent, aDumpAll);
  }
  for (indent = aIndent; --indent >= 0; ) fputs("  ", out);
  fputs("</", out);
  fputs(NS_LossyConvertUTF16toASCII(buf).get(), out);
  fputs(">", out);

  if(aIndent) fputs("\n", out);
}
#endif

bool
Element::CheckHandleEventForLinksPrecondition(nsEventChainVisitor& aVisitor,
                                              nsIURI** aURI) const
{
  if (aVisitor.mEventStatus == nsEventStatus_eConsumeNoDefault ||
      (!aVisitor.mEvent->mFlags.mIsTrusted &&
       (aVisitor.mEvent->message != NS_MOUSE_CLICK) &&
       (aVisitor.mEvent->message != NS_KEY_PRESS) &&
       (aVisitor.mEvent->message != NS_UI_ACTIVATE)) ||
      !aVisitor.mPresContext ||
      aVisitor.mEvent->mFlags.mMultipleActionsPrevented) {
    return false;
  }

  // Make sure we actually are a link
  return IsLink(aURI);
}

nsresult
Element::PreHandleEventForLinks(nsEventChainPreVisitor& aVisitor)
{
  // Optimisation: return early if this event doesn't interest us.
  // IMPORTANT: this switch and the switch below it must be kept in sync!
  switch (aVisitor.mEvent->message) {
  case NS_MOUSE_ENTER_SYNTH:
  case NS_FOCUS_CONTENT:
  case NS_MOUSE_EXIT_SYNTH:
  case NS_BLUR_CONTENT:
    break;
  default:
    return NS_OK;
  }

  // Make sure we meet the preconditions before continuing
  nsCOMPtr<nsIURI> absURI;
  if (!CheckHandleEventForLinksPrecondition(aVisitor, getter_AddRefs(absURI))) {
    return NS_OK;
  }

  nsresult rv = NS_OK;

  // We do the status bar updates in PreHandleEvent so that the status bar gets
  // updated even if the event is consumed before we have a chance to set it.
  switch (aVisitor.mEvent->message) {
  // Set the status bar similarly for mouseover and focus
  case NS_MOUSE_ENTER_SYNTH:
    aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
    // FALL THROUGH
  case NS_FOCUS_CONTENT:
    if (aVisitor.mEvent->eventStructType != NS_FOCUS_EVENT ||
        !static_cast<nsFocusEvent*>(aVisitor.mEvent)->isRefocus) {
      nsAutoString target;
      GetLinkTarget(target);
      nsContentUtils::TriggerLink(this, aVisitor.mPresContext, absURI, target,
                                  false, true, true);
      // Make sure any ancestor links don't also TriggerLink
      aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
    }
    break;

  case NS_MOUSE_EXIT_SYNTH:
    aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
    // FALL THROUGH
  case NS_BLUR_CONTENT:
    rv = LeaveLink(aVisitor.mPresContext);
    if (NS_SUCCEEDED(rv)) {
      aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
    }
    break;

  default:
    // switch not in sync with the optimization switch earlier in this function
    NS_NOTREACHED("switch statements not in sync");
    return NS_ERROR_UNEXPECTED;
  }

  return rv;
}

nsresult
Element::PostHandleEventForLinks(nsEventChainPostVisitor& aVisitor)
{
  // Optimisation: return early if this event doesn't interest us.
  // IMPORTANT: this switch and the switch below it must be kept in sync!
  switch (aVisitor.mEvent->message) {
  case NS_MOUSE_BUTTON_DOWN:
  case NS_MOUSE_CLICK:
  case NS_UI_ACTIVATE:
  case NS_KEY_PRESS:
    break;
  default:
    return NS_OK;
  }

  // Make sure we meet the preconditions before continuing
  nsCOMPtr<nsIURI> absURI;
  if (!CheckHandleEventForLinksPrecondition(aVisitor, getter_AddRefs(absURI))) {
    return NS_OK;
  }

  nsresult rv = NS_OK;

  switch (aVisitor.mEvent->message) {
  case NS_MOUSE_BUTTON_DOWN:
    {
      if (aVisitor.mEvent->eventStructType == NS_MOUSE_EVENT &&
          static_cast<nsMouseEvent*>(aVisitor.mEvent)->button ==
          nsMouseEvent::eLeftButton) {
        // don't make the link grab the focus if there is no link handler
        nsILinkHandler *handler = aVisitor.mPresContext->GetLinkHandler();
        nsIDocument *document = GetCurrentDoc();
        if (handler && document) {
          nsIFocusManager* fm = nsFocusManager::GetFocusManager();
          if (fm) {
            aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
            nsCOMPtr<nsIDOMElement> elem = do_QueryInterface(this);
            fm->SetFocus(elem, nsIFocusManager::FLAG_BYMOUSE |
                               nsIFocusManager::FLAG_NOSCROLL);
          }

          nsEventStateManager::SetActiveManager(
            aVisitor.mPresContext->EventStateManager(), this);
        }
      }
    }
    break;

  case NS_MOUSE_CLICK:
    if (NS_IS_MOUSE_LEFT_CLICK(aVisitor.mEvent)) {
      nsInputEvent* inputEvent = static_cast<nsInputEvent*>(aVisitor.mEvent);
      if (inputEvent->IsControl() || inputEvent->IsMeta() ||
          inputEvent->IsAlt() ||inputEvent->IsShift()) {
        break;
      }

      // The default action is simply to dispatch DOMActivate
      nsCOMPtr<nsIPresShell> shell = aVisitor.mPresContext->GetPresShell();
      if (shell) {
        // single-click
        nsEventStatus status = nsEventStatus_eIgnore;
        nsUIEvent actEvent(aVisitor.mEvent->mFlags.mIsTrusted,
                           NS_UI_ACTIVATE, 1);

        rv = shell->HandleDOMEventWithTarget(this, &actEvent, &status);
        if (NS_SUCCEEDED(rv)) {
          aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
        }
      }
    }
    break;

  case NS_UI_ACTIVATE:
    {
      if (aVisitor.mEvent->originalTarget == this) {
        nsAutoString target;
        GetLinkTarget(target);
        nsContentUtils::TriggerLink(this, aVisitor.mPresContext, absURI, target,
                                    true, true,
                                    aVisitor.mEvent->mFlags.mIsTrusted);
        aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
      }
    }
    break;

  case NS_KEY_PRESS:
    {
      if (aVisitor.mEvent->eventStructType == NS_KEY_EVENT) {
        nsKeyEvent* keyEvent = static_cast<nsKeyEvent*>(aVisitor.mEvent);
        if (keyEvent->keyCode == NS_VK_RETURN) {
          nsEventStatus status = nsEventStatus_eIgnore;
          rv = DispatchClickEvent(aVisitor.mPresContext, keyEvent, this,
                                  false, nullptr, &status);
          if (NS_SUCCEEDED(rv)) {
            aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
          }
        }
      }
    }
    break;

  default:
    // switch not in sync with the optimization switch earlier in this function
    NS_NOTREACHED("switch statements not in sync");
    return NS_ERROR_UNEXPECTED;
  }

  return rv;
}

void
Element::GetLinkTarget(nsAString& aTarget)
{
  aTarget.Truncate();
}

// NOTE: The aPresContext pointer is NOT addrefed.
// *aSelectorList might be null even if NS_OK is returned; this
// happens when all the selectors were pseudo-element selectors.
static nsresult
ParseSelectorList(nsINode* aNode,
                  const nsAString& aSelectorString,
                  nsCSSSelectorList** aSelectorList)
{
  NS_ENSURE_ARG(aNode);

  nsIDocument* doc = aNode->OwnerDoc();
  nsCSSParser parser(doc->CSSLoader());

  nsCSSSelectorList* selectorList;
  nsresult rv = parser.ParseSelectorString(aSelectorString,
                                           doc->GetDocumentURI(),
                                           0, // XXXbz get the line number!
                                           &selectorList);
  if (NS_FAILED(rv)) {
    // We hit this for syntax errors, which are quite common, so don't
    // use NS_ENSURE_SUCCESS.  (For example, jQuery has an extended set
    // of selectors, but it sees if we can parse them first.)
    return rv;
  }

  // Filter out pseudo-element selectors from selectorList
  nsCSSSelectorList** slot = &selectorList;
  do {
    nsCSSSelectorList* cur = *slot;
    if (cur->mSelectors->IsPseudoElement()) {
      *slot = cur->mNext;
      cur->mNext = nullptr;
      delete cur;
    } else {
      slot = &cur->mNext;
    }
  } while (*slot);
  *aSelectorList = selectorList;

  return NS_OK;
}


bool
Element::MozMatchesSelector(const nsAString& aSelector,
                            ErrorResult& aError)
{
  nsAutoPtr<nsCSSSelectorList> selectorList;

  aError = ParseSelectorList(this, aSelector, getter_Transfers(selectorList));

  if (aError.Failed()) {
    return false;
  }

  OwnerDoc()->FlushPendingLinkUpdates();
  TreeMatchContext matchingContext(false,
                                   nsRuleWalker::eRelevantLinkUnvisited,
                                   OwnerDoc(),
                                   TreeMatchContext::eNeverMatchVisited);
  matchingContext.SetHasSpecifiedScope();
  matchingContext.AddScopeElement(this);
  return nsCSSRuleProcessor::SelectorListMatches(this, matchingContext,
                                                 selectorList);
}

static const nsAttrValue::EnumTable kCORSAttributeTable[] = {
  // Order matters here
  // See ParseCORSValue
  { "anonymous",       CORS_ANONYMOUS       },
  { "use-credentials", CORS_USE_CREDENTIALS },
  { 0 }
};

/* static */ void
Element::ParseCORSValue(const nsAString& aValue,
                        nsAttrValue& aResult)
{
  DebugOnly<bool> success =
    aResult.ParseEnumValue(aValue, kCORSAttributeTable, false,
                           // default value is anonymous if aValue is
                           // not a value we understand
                           &kCORSAttributeTable[0]);
  MOZ_ASSERT(success);
}

/* static */ CORSMode
Element::StringToCORSMode(const nsAString& aValue)
{
  if (aValue.IsVoid()) {
    return CORS_NONE;
  }

  nsAttrValue val;
  Element::ParseCORSValue(aValue, val);
  return CORSMode(val.GetEnumValue());
}

/* static */ CORSMode
Element::AttrValueToCORSMode(const nsAttrValue* aValue)
{
  if (!aValue) {
    return CORS_NONE;
  }

  return CORSMode(aValue->GetEnumValue());
}

static const char*
GetFullScreenError(nsIDocument* aDoc)
{
  nsCOMPtr<nsPIDOMWindow> win = aDoc->GetWindow();
  if (aDoc->NodePrincipal()->GetAppStatus() >= nsIPrincipal::APP_STATUS_INSTALLED) {
    // Request is in a web app and in the same origin as the web app.
    // Don't enforce as strict security checks for web apps, the user
    // is supposed to have trust in them. However documents cross-origin
    // to the web app must still confirm to the normal security checks.
    return nullptr;
  }

  if (!nsContentUtils::IsRequestFullScreenAllowed()) {
    return "FullScreenDeniedNotInputDriven";
  }

  if (nsContentUtils::IsSitePermDeny(aDoc->NodePrincipal(), "fullscreen")) {
    return "FullScreenDeniedBlocked";
  }

  return nullptr;
}

void
Element::MozRequestFullScreen()
{
  // Only grant full-screen requests if this is called from inside a trusted
  // event handler (i.e. inside an event handler for a user initiated event).
  // This stops the full-screen from being abused similar to the popups of old,
  // and it also makes it harder for bad guys' script to go full-screen and
  // spoof the browser chrome/window and phish logins etc.
  // Note that requests for fullscreen inside a web app's origin are exempt
  // from this restriction.
  const char* error = GetFullScreenError(OwnerDoc());
  if (error) {
    nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                    "DOM", OwnerDoc(),
                                    nsContentUtils::eDOM_PROPERTIES,
                                    error);
    nsRefPtr<nsAsyncDOMEvent> e =
      new nsAsyncDOMEvent(OwnerDoc(),
                          NS_LITERAL_STRING("mozfullscreenerror"),
                          true,
                          false);
    e->PostDOMEvent();
    return;
  }

  OwnerDoc()->AsyncRequestFullScreen(this);

  return;
}
