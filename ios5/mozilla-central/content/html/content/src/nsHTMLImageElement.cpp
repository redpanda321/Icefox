/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"

#include "nsHTMLImageElement.h"
#include "nsIDOMEventTarget.h"
#include "nsGkAtoms.h"
#include "nsStyleConsts.h"
#include "nsPresContext.h"
#include "nsMappedAttributes.h"
#include "nsSize.h"
#include "nsIDocument.h"
#include "nsIScriptContext.h"
#include "nsIURL.h"
#include "nsIIOService.h"
#include "nsIServiceManager.h"
#include "nsNetUtil.h"
#include "nsContentUtils.h"
#include "nsIFrame.h"
#include "nsNodeInfoManager.h"
#include "nsGUIEvent.h"
#include "nsContentPolicyUtils.h"
#include "nsIDOMWindow.h"
#include "nsFocusManager.h"

#include "imgIContainer.h"
#include "imgILoader.h"
#include "imgIRequest.h"
#include "imgIDecoderObserver.h"

#include "nsILoadGroup.h"

#include "nsRuleData.h"

#include "nsIJSContextStack.h"
#include "nsIDOMHTMLMapElement.h"
#include "nsEventDispatcher.h"

#include "nsLayoutUtils.h"

using namespace mozilla;
using namespace mozilla::dom;

nsGenericHTMLElement*
NS_NewHTMLImageElement(already_AddRefed<nsINodeInfo> aNodeInfo,
                       FromParser aFromParser)
{
  /*
   * nsHTMLImageElement's will be created without a nsINodeInfo passed in
   * if someone says "var img = new Image();" in JavaScript, in a case like
   * that we request the nsINodeInfo from the document's nodeinfo list.
   */
  nsCOMPtr<nsINodeInfo> nodeInfo(aNodeInfo);
  if (!nodeInfo) {
    nsCOMPtr<nsIDocument> doc =
      do_QueryInterface(nsContentUtils::GetDocumentFromCaller());
    NS_ENSURE_TRUE(doc, nsnull);

    nodeInfo = doc->NodeInfoManager()->GetNodeInfo(nsGkAtoms::img, nsnull,
                                                   kNameSpaceID_XHTML,
                                                   nsIDOMNode::ELEMENT_NODE);
    NS_ENSURE_TRUE(nodeInfo, nsnull);
  }

  return new nsHTMLImageElement(nodeInfo.forget());
}

nsHTMLImageElement::nsHTMLImageElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : nsGenericHTMLElement(aNodeInfo)
{
  // We start out broken
  AddStatesSilently(NS_EVENT_STATE_BROKEN);
}

nsHTMLImageElement::~nsHTMLImageElement()
{
  DestroyImageLoadingContent();
}


NS_IMPL_ADDREF_INHERITED(nsHTMLImageElement, nsGenericElement)
NS_IMPL_RELEASE_INHERITED(nsHTMLImageElement, nsGenericElement)


DOMCI_NODE_DATA(HTMLImageElement, nsHTMLImageElement)

// QueryInterface implementation for nsHTMLImageElement
NS_INTERFACE_TABLE_HEAD(nsHTMLImageElement)
  NS_HTML_CONTENT_INTERFACE_TABLE5(nsHTMLImageElement,
                                   nsIDOMHTMLImageElement,
                                   nsIJSNativeInitializer,
                                   imgIDecoderObserver,
                                   nsIImageLoadingContent,
                                   imgIContainerObserver)
  NS_HTML_CONTENT_INTERFACE_TABLE_TO_MAP_SEGUE(nsHTMLImageElement,
                                               nsGenericHTMLElement)
NS_HTML_CONTENT_INTERFACE_TABLE_TAIL_CLASSINFO(HTMLImageElement)


NS_IMPL_ELEMENT_CLONE(nsHTMLImageElement)


NS_IMPL_STRING_ATTR(nsHTMLImageElement, Name, name)
NS_IMPL_STRING_ATTR(nsHTMLImageElement, Align, align)
NS_IMPL_STRING_ATTR(nsHTMLImageElement, Alt, alt)
NS_IMPL_STRING_ATTR(nsHTMLImageElement, Border, border)
NS_IMPL_INT_ATTR(nsHTMLImageElement, Hspace, hspace)
NS_IMPL_BOOL_ATTR(nsHTMLImageElement, IsMap, ismap)
NS_IMPL_URI_ATTR(nsHTMLImageElement, LongDesc, longdesc)
NS_IMPL_STRING_ATTR(nsHTMLImageElement, Lowsrc, lowsrc)
NS_IMPL_URI_ATTR(nsHTMLImageElement, Src, src)
NS_IMPL_STRING_ATTR(nsHTMLImageElement, UseMap, usemap)
NS_IMPL_INT_ATTR(nsHTMLImageElement, Vspace, vspace)

void
nsHTMLImageElement::GetItemValueText(nsAString& aValue)
{
  GetSrc(aValue);
}

void
nsHTMLImageElement::SetItemValueText(const nsAString& aValue)
{
  SetSrc(aValue);
}

// crossorigin is not "limited to only known values" per spec, so it's
// just a string attr purposes of the DOM crossOrigin property.
NS_IMPL_STRING_ATTR(nsHTMLImageElement, CrossOrigin, crossorigin)

NS_IMETHODIMP
nsHTMLImageElement::GetDraggable(bool* aDraggable)
{
  // images may be dragged unless the draggable attribute is false
  *aDraggable = !AttrValueIs(kNameSpaceID_None, nsGkAtoms::draggable,
                             nsGkAtoms::_false, eIgnoreCase);
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLImageElement::GetComplete(bool* aComplete)
{
  NS_PRECONDITION(aComplete, "Null out param!");
  *aComplete = true;

  if (!mCurrentRequest) {
    return NS_OK;
  }

  PRUint32 status;
  mCurrentRequest->GetImageStatus(&status);
  *aComplete =
    (status &
     (imgIRequest::STATUS_LOAD_COMPLETE | imgIRequest::STATUS_ERROR)) != 0;

  return NS_OK;
}

nsIntPoint
nsHTMLImageElement::GetXY()
{
  nsIntPoint point(0, 0);

  nsIFrame* frame = GetPrimaryFrame(Flush_Layout);

  if (!frame) {
    return point;
  }

  nsIFrame* layer = nsLayoutUtils::GetClosestLayer(frame->GetParent());
  nsPoint origin(frame->GetOffsetTo(layer));
  // Convert to pixels using that scale
  point.x = nsPresContext::AppUnitsToIntCSSPixels(origin.x);
  point.y = nsPresContext::AppUnitsToIntCSSPixels(origin.y);

  return point;
}

NS_IMETHODIMP
nsHTMLImageElement::GetX(PRInt32* aX)
{
  *aX = GetXY().x;

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLImageElement::GetY(PRInt32* aY)
{
  *aY = GetXY().y;

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLImageElement::GetHeight(PRUint32* aHeight)
{
  *aHeight = GetWidthHeightForImage(mCurrentRequest).height;

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLImageElement::SetHeight(PRUint32 aHeight)
{
  return nsGenericHTMLElement::SetUnsignedIntAttr(nsGkAtoms::height, aHeight);
}

NS_IMETHODIMP
nsHTMLImageElement::GetWidth(PRUint32* aWidth)
{
  *aWidth = GetWidthHeightForImage(mCurrentRequest).width;

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLImageElement::SetWidth(PRUint32 aWidth)
{
  return nsGenericHTMLElement::SetUnsignedIntAttr(nsGkAtoms::width, aWidth);
}

bool
nsHTMLImageElement::ParseAttribute(PRInt32 aNamespaceID,
                                   nsIAtom* aAttribute,
                                   const nsAString& aValue,
                                   nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::align) {
      return ParseAlignValue(aValue, aResult);
    }
    if (aAttribute == nsGkAtoms::crossorigin) {
      ParseCORSValue(aValue, aResult);
      return true;
    }
    if (ParseImageAttribute(aAttribute, aValue, aResult)) {
      return true;
    }
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aResult);
}

static void
MapAttributesIntoRule(const nsMappedAttributes* aAttributes,
                      nsRuleData* aData)
{
  nsGenericHTMLElement::MapImageAlignAttributeInto(aAttributes, aData);
  nsGenericHTMLElement::MapImageBorderAttributeInto(aAttributes, aData);
  nsGenericHTMLElement::MapImageMarginAttributeInto(aAttributes, aData);
  nsGenericHTMLElement::MapImageSizeAttributesInto(aAttributes, aData);
  nsGenericHTMLElement::MapCommonAttributesInto(aAttributes, aData);
}

nsChangeHint
nsHTMLImageElement::GetAttributeChangeHint(const nsIAtom* aAttribute,
                                           PRInt32 aModType) const
{
  nsChangeHint retval =
    nsGenericHTMLElement::GetAttributeChangeHint(aAttribute, aModType);
  if (aAttribute == nsGkAtoms::usemap ||
      aAttribute == nsGkAtoms::ismap) {
    NS_UpdateHint(retval, NS_STYLE_HINT_FRAMECHANGE);
  }
  return retval;
}

NS_IMETHODIMP_(bool)
nsHTMLImageElement::IsAttributeMapped(const nsIAtom* aAttribute) const
{
  static const MappedAttributeEntry* const map[] = {
    sCommonAttributeMap,
    sImageMarginSizeAttributeMap,
    sImageBorderAttributeMap,
    sImageAlignAttributeMap
  };

  return FindAttributeDependence(aAttribute, map);
}


nsMapRuleToAttributesFunc
nsHTMLImageElement::GetAttributeMappingFunction() const
{
  return &MapAttributesIntoRule;
}


nsresult
nsHTMLImageElement::PreHandleEvent(nsEventChainPreVisitor& aVisitor)
{
  // If we are a map and get a mouse click, don't let it be handled by
  // the Generic Element as this could cause a click event to fire
  // twice, once by the image frame for the map and once by the Anchor
  // element. (bug 39723)
  if (aVisitor.mEvent->eventStructType == NS_MOUSE_EVENT &&
      aVisitor.mEvent->message == NS_MOUSE_CLICK &&
      static_cast<nsMouseEvent*>(aVisitor.mEvent)->button ==
        nsMouseEvent::eLeftButton) {
    bool isMap = false;
    GetIsMap(&isMap);
    if (isMap) {
      aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
    }
  }
  return nsGenericHTMLElement::PreHandleEvent(aVisitor);
}

bool
nsHTMLImageElement::IsHTMLFocusable(bool aWithMouse,
                                    bool *aIsFocusable, PRInt32 *aTabIndex)
{
  PRInt32 tabIndex;
  GetTabIndex(&tabIndex);

  if (IsInDoc()) {
    nsAutoString usemap;
    GetUseMap(usemap);
    // XXXbz which document should this be using?  sXBL/XBL2 issue!  I
    // think that OwnerDoc() is right, since we don't want to
    // assume stuff about the document we're bound to.
    if (OwnerDoc()->FindImageMap(usemap)) {
      if (aTabIndex) {
        // Use tab index on individual map areas
        *aTabIndex = (sTabFocusModel & eTabFocus_linksMask)? 0 : -1;
      }
      // Image map is not focusable itself, but flag as tabbable
      // so that image map areas get walked into.
      *aIsFocusable = false;

      return false;
    }
  }

  if (aTabIndex) {
    // Can be in tab order if tabindex >=0 and form controls are tabbable.
    *aTabIndex = (sTabFocusModel & eTabFocus_formElementsMask)? tabIndex : -1;
  }

  *aIsFocusable = 
#ifdef XP_MACOSX
    (!aWithMouse || nsFocusManager::sMouseFocusesFormControl) &&
#endif
    (tabIndex >= 0 || HasAttr(kNameSpaceID_None, nsGkAtoms::tabindex));

  return false;
}

nsresult
nsHTMLImageElement::SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                            nsIAtom* aPrefix, const nsAString& aValue,
                            bool aNotify)
{
  // If we plan to call LoadImage, we want to do it first so that the
  // image load kicks off _before_ the reflow triggered by the SetAttr.  But if
  // aNotify is false, we are coming from the parser or some such place; we'll
  // get bound after all the attributes have been set, so we'll do the
  // image load from BindToTree.  Skip the LoadImage call in that case.
  if (aNotify &&
      aNameSpaceID == kNameSpaceID_None && aName == nsGkAtoms::src) {

    // Prevent setting image.src by exiting early
    if (nsContentUtils::IsImageSrcSetDisabled()) {
      return NS_OK;
    }

    // A hack to get animations to reset. See bug 594771.
    mNewRequestsWillNeedAnimationReset = true;

    // Force image loading here, so that we'll try to load the image from
    // network if it's set to be not cacheable...  If we change things so that
    // the state gets in nsGenericElement's attr-setting happen around this
    // LoadImage call, we could start passing false instead of aNotify
    // here.
    LoadImage(aValue, true, aNotify);

    mNewRequestsWillNeedAnimationReset = false;
  }
    
  return nsGenericHTMLElement::SetAttr(aNameSpaceID, aName, aPrefix, aValue,
                                       aNotify);
}

nsresult
nsHTMLImageElement::UnsetAttr(PRInt32 aNameSpaceID, nsIAtom* aAttribute,
                              bool aNotify)
{
  if (aNameSpaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::src) {
    CancelImageRequests(aNotify);
  }

  return nsGenericHTMLElement::UnsetAttr(aNameSpaceID, aAttribute, aNotify);
}

nsresult
nsHTMLImageElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                               nsIContent* aBindingParent,
                               bool aCompileEventHandlers)
{
  nsresult rv = nsGenericHTMLElement::BindToTree(aDocument, aParent,
                                                 aBindingParent,
                                                 aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  if (HasAttr(kNameSpaceID_None, nsGkAtoms::src)) {
    // FIXME: Bug 660963 it would be nice if we could just have
    // ClearBrokenState update our state and do it fast...
    ClearBrokenState();
    RemoveStatesSilently(NS_EVENT_STATE_BROKEN);
    // If loading is temporarily disabled, don't even launch MaybeLoadImage.
    // Otherwise MaybeLoadImage may run later when someone has reenabled
    // loading.
    if (LoadingEnabled()) {
      nsContentUtils::AddScriptRunner(
        NS_NewRunnableMethod(this, &nsHTMLImageElement::MaybeLoadImage));
    }
  }

  return rv;
}

void
nsHTMLImageElement::MaybeLoadImage()
{
  // Our base URI may have changed; claim that our URI changed, and the
  // nsImageLoadingContent will decide whether a new image load is warranted.
  // Note, check LoadingEnabled() after LoadImage call.
  nsAutoString uri;
  if (GetAttr(kNameSpaceID_None, nsGkAtoms::src, uri) &&
      (NS_FAILED(LoadImage(uri, false, true)) ||
       !LoadingEnabled())) {
    CancelImageRequests(true);
  }
}

nsEventStates
nsHTMLImageElement::IntrinsicState() const
{
  return nsGenericHTMLElement::IntrinsicState() |
    nsImageLoadingContent::ImageState();
}

NS_IMETHODIMP
nsHTMLImageElement::Initialize(nsISupports* aOwner, JSContext* aContext,
                               JSObject *aObj, PRUint32 argc, jsval *argv)
{
  if (argc <= 0) {
    // Nothing to do here if we don't get any arguments.

    return NS_OK;
  }

  // The first (optional) argument is the width of the image
  uint32_t width;
  JSBool ret = JS_ValueToECMAUint32(aContext, argv[0], &width);
  NS_ENSURE_TRUE(ret, NS_ERROR_INVALID_ARG);

  nsresult rv = SetIntAttr(nsGkAtoms::width, static_cast<PRInt32>(width));

  if (NS_SUCCEEDED(rv) && (argc > 1)) {
    // The second (optional) argument is the height of the image
    uint32_t height;
    ret = JS_ValueToECMAUint32(aContext, argv[1], &height);
    NS_ENSURE_TRUE(ret, NS_ERROR_INVALID_ARG);

    rv = SetIntAttr(nsGkAtoms::height, static_cast<PRInt32>(height));
  }

  return rv;
}

NS_IMETHODIMP
nsHTMLImageElement::GetNaturalHeight(PRUint32* aNaturalHeight)
{
  NS_ENSURE_ARG_POINTER(aNaturalHeight);

  *aNaturalHeight = 0;

  if (!mCurrentRequest) {
    return NS_OK;
  }
  
  nsCOMPtr<imgIContainer> image;
  mCurrentRequest->GetImage(getter_AddRefs(image));
  if (!image) {
    return NS_OK;
  }

  PRInt32 height;
  if (NS_SUCCEEDED(image->GetHeight(&height))) {
    *aNaturalHeight = height;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLImageElement::GetNaturalWidth(PRUint32* aNaturalWidth)
{
  NS_ENSURE_ARG_POINTER(aNaturalWidth);

  *aNaturalWidth = 0;

  if (!mCurrentRequest) {
    return NS_OK;
  }
  
  nsCOMPtr<imgIContainer> image;
  mCurrentRequest->GetImage(getter_AddRefs(image));
  if (!image) {
    return NS_OK;
  }

  PRInt32 width;
  if (NS_SUCCEEDED(image->GetWidth(&width))) {
    *aNaturalWidth = width;
  }
  return NS_OK;
}

nsresult
nsHTMLImageElement::CopyInnerTo(nsGenericElement* aDest)
{
  if (aDest->OwnerDoc()->IsStaticDocument()) {
    CreateStaticImageClone(static_cast<nsHTMLImageElement*>(aDest));
  }
  return nsGenericHTMLElement::CopyInnerTo(aDest);
}

CORSMode
nsHTMLImageElement::GetCORSMode()
{
  return AttrValueToCORSMode(GetParsedAttr(nsGkAtoms::crossorigin));
}
