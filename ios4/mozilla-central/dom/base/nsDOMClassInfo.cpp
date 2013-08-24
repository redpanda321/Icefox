/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=78: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2000
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Johnny Stenback <jst@netscape.com> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

// JavaScript includes
#include "jsapi.h"
#include "jsprvtd.h"    // we are using private JS typedefs...
#include "jscntxt.h"
#include "jsobj.h"
#include "jsdbgapi.h"

#include "nscore.h"
#include "nsDOMClassInfo.h"
#include "nsCRT.h"
#include "nsCRTGlue.h"
#include "nsIServiceManager.h"
#include "nsICategoryManager.h"
#include "nsIComponentRegistrar.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsIXPConnect.h"
#include "nsIJSContextStack.h"
#include "nsIXPCSecurityManager.h"
#include "nsIStringBundle.h"
#include "nsIConsoleService.h"
#include "nsIScriptError.h"
#include "nsXPIDLString.h"
#include "nsReadableUtils.h"
#include "nsUnicharUtils.h"
#include "xptcall.h"
#include "prprf.h"
#include "nsTArray.h"
#include "nsCSSValue.h"
#include "nsIRunnable.h"
#include "nsThreadUtils.h"
#include "nsDOMEventTargetWrapperCache.h"

// General helper includes
#include "nsGlobalWindow.h"
#include "nsIContent.h"
#include "nsIAttribute.h"
#include "nsIDocument.h"
#include "nsIDOMDocument.h"
#include "nsIDOM3Document.h"
#include "nsIDOMXMLDocument.h"
#include "nsIDOMNSDocument.h"
#include "nsIDOMEvent.h"
#include "nsIDOMNSEvent.h"
#include "nsIDOMKeyEvent.h"
#include "nsIDOMEventListener.h"
#include "nsIDOMPopStateEvent.h"
#include "nsContentUtils.h"
#include "nsDOMWindowUtils.h"

// Window scriptable helper includes
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShellTreeNode.h"
#include "nsIScriptExternalNameSet.h"
#include "nsJSUtils.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsScriptNameSpaceManager.h"
#include "nsIScriptObjectOwner.h"
#include "nsIJSNativeInitializer.h"
#include "nsJSEnvironment.h"

// DOM base includes
#include "nsIDOMPluginArray.h"
#include "nsIDOMPlugin.h"
#include "nsIDOMMimeTypeArray.h"
#include "nsIDOMMimeType.h"
#include "nsIDOMLocation.h"
#include "nsIDOMWindowInternal.h"
#include "nsPIDOMWindow.h"
#include "nsIDOMJSWindow.h"
#include "nsIDOMWindowCollection.h"
#include "nsIDOMHistory.h"
#include "nsIDOMMediaList.h"
#include "nsIDOMChromeWindow.h"
#include "nsIDOMConstructor.h"
#include "nsClientRect.h"

// DOM core includes
#include "nsDOMError.h"
#include "nsIDOMDOMException.h"
#include "nsIDOMNode.h"
#include "nsIDOM3Node.h"
#include "nsIDOM3Attr.h"
#include "nsIDOMNodeList.h"
#include "nsIDOMNamedNodeMap.h"
#include "nsIDOMDOMStringList.h"
#include "nsIDOMDOMTokenList.h"
#include "nsIDOMDOMSettableTokenList.h"
#include "nsIDOMNameList.h"
#include "nsIDOMNSElement.h"

// HTMLFormElement helper includes
#include "nsIForm.h"
#include "nsIFormControl.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsIDOMHTMLCollection.h"
#include "nsIHTMLCollection.h"
#include "nsHTMLDocument.h"

// Constraint Validation API helper includes
#include "nsIDOMValidityState.h"

// HTMLSelectElement helper includes
#include "nsIDOMHTMLSelectElement.h"

// HTMLEmbed/ObjectElement helper includes
#include "nsIPluginInstance.h"
#include "nsIObjectFrame.h"
#include "nsIObjectLoadingContent.h"
#include "nsIPluginHost.h"

// Oh, did I mention that I hate Microsoft for doing this to me?
#ifdef XP_WIN
#undef GetClassName
#endif

// HTMLOptionsCollection includes
#include "nsIDOMHTMLOptionElement.h"
#include "nsIDOMHTMLOptionsCollection.h"
#include "nsIDOMNSHTMLOptionCollectn.h"

// ContentList includes
#include "nsContentList.h"
#include "nsGenericElement.h"

// Event related includes
#include "nsIEventListenerManager.h"
#include "nsIDOMEventTarget.h"
#include "nsIDOMNSEventTarget.h"

// CSS related includes
#include "nsIDOMStyleSheet.h"
#include "nsIDOMStyleSheetList.h"
#include "nsIDOMCSSStyleDeclaration.h"
#include "nsIDOMCSSRule.h"
#include "nsICSSRule.h"
#include "nsICSSRuleList.h"
#include "nsIDOMRect.h"
#include "nsIDOMRGBColor.h"
#include "nsIDOMNSRGBAColor.h"
#include "nsDOMCSSAttrDeclaration.h"

// XBL related includes.
#include "nsIXBLService.h"
#include "nsXBLBinding.h"
#include "nsBindingManager.h"
#include "nsIFrame.h"
#include "nsIPresShell.h"
#include "nsIDOMViewCSS.h"
#include "nsIDOMElement.h"
#include "nsIDOMCSSStyleDeclaration.h"
#include "nsStyleSet.h"
#include "nsStyleContext.h"
#include "nsAutoPtr.h"
#include "nsMemory.h"

// Tranformiix
#include "nsIDOMXPathEvaluator.h"
#include "nsIXSLTProcessor.h"
#include "nsIXSLTProcessorObsolete.h"
#include "nsIXSLTProcessorPrivate.h"

#include "nsIDOMLSProgressEvent.h"
#include "nsIDOMParser.h"
#include "nsIDOMSerializer.h"
#include "nsXMLHttpRequest.h"
#include "nsWebSocket.h"
#include "nsIDOMCloseEvent.h"

// includes needed for the prototype chain interfaces
#include "nsIDOMNavigator.h"
#include "nsIDOMBarProp.h"
#include "nsIDOMScreen.h"
#include "nsIDOMDocumentType.h"
#include "nsIDOMDOMImplementation.h"
#include "nsIDOMDocumentFragment.h"
#include "nsIDOMDocumentEvent.h"
#include "nsDOMAttribute.h"
#include "nsIDOMText.h"
#include "nsIDOM3Text.h"
#include "nsIDOMComment.h"
#include "nsIDOMCDATASection.h"
#include "nsIDOMProcessingInstruction.h"
#include "nsIDOMNotation.h"
#include "nsIDOMNSEvent.h"
#include "nsIDOMDataContainerEvent.h"
#include "nsIDOMKeyEvent.h"
#include "nsIDOMMouseEvent.h"
#include "nsIDOMMouseScrollEvent.h"
#include "nsIDOMDragEvent.h"
#include "nsIDOMCommandEvent.h"
#include "nsIDOMPopupBlockedEvent.h"
#include "nsIDOMBeforeUnloadEvent.h"
#include "nsIDOMMutationEvent.h"
#include "nsIDOMSmartCardEvent.h"
#include "nsIDOMXULCommandEvent.h"
#include "nsIDOMPageTransitionEvent.h"
#include "nsIDOMMessageEvent.h"
#include "nsPaintRequest.h"
#include "nsIDOMNotifyPaintEvent.h"
#include "nsIDOMNotifyAudioAvailableEvent.h"
#include "nsIDOMScrollAreaEvent.h"
#include "nsIDOMTransitionEvent.h"
#include "nsIDOMNSDocumentStyle.h"
#include "nsIDOMDocumentRange.h"
#include "nsIDOMDocumentTraversal.h"
#include "nsIDOMDocumentXBL.h"
#include "nsIDOMDocumentView.h"
#include "nsIDOMElementCSSInlineStyle.h"
#include "nsIDOMLinkStyle.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMNSHTMLDocument.h"
#include "nsIDOMHTMLElement.h"
#include "nsIDOMNSHTMLElement.h"
#include "nsIDOMHTMLAnchorElement.h"
#include "nsIDOMHTMLAppletElement.h"
#include "nsIDOMHTMLAreaElement.h"
#include "nsIDOMHTMLBRElement.h"
#include "nsIDOMHTMLBaseElement.h"
#include "nsIDOMHTMLBodyElement.h"
#include "nsIDOMHTMLButtonElement.h"
#include "nsIDOMHTMLCanvasElement.h"
#include "nsIDOMHTMLDListElement.h"
#include "nsIDOMHTMLDirectoryElement.h"
#include "nsIDOMHTMLDivElement.h"
#include "nsIDOMHTMLEmbedElement.h"
#include "nsIDOMHTMLFieldSetElement.h"
#include "nsIDOMHTMLFontElement.h"
#include "nsIDOMNSHTMLFormElement.h"
#include "nsIDOMHTMLFrameElement.h"
#include "nsIDOMHTMLFrameSetElement.h"
#include "nsIDOMNSHTMLFrameElement.h"
#include "nsIDOMHTMLHRElement.h"
#include "nsIDOMNSHTMLHRElement.h"
#include "nsIDOMHTMLHeadElement.h"
#include "nsIDOMHTMLHeadingElement.h"
#include "nsIDOMHTMLHtmlElement.h"
#include "nsIDOMHTMLIFrameElement.h"
#include "nsIDOMHTMLImageElement.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsIDOMHTMLIsIndexElement.h"
#include "nsIDOMHTMLLIElement.h"
#include "nsIDOMHTMLLabelElement.h"
#include "nsIDOMHTMLLegendElement.h"
#include "nsIDOMHTMLLinkElement.h"
#include "nsIDOMHTMLMapElement.h"
#include "nsIDOMHTMLMenuElement.h"
#include "nsIDOMHTMLMetaElement.h"
#include "nsIDOMHTMLModElement.h"
#include "nsIDOMHTMLOListElement.h"
#include "nsIDOMHTMLObjectElement.h"
#include "nsIDOMHTMLOptGroupElement.h"
#include "nsIDOMHTMLOutputElement.h"
#include "nsIDOMHTMLParagraphElement.h"
#include "nsIDOMHTMLParamElement.h"
#include "nsIDOMHTMLPreElement.h"
#include "nsIDOMHTMLQuoteElement.h"
#include "nsIDOMHTMLScriptElement.h"
#include "nsIDOMHTMLStyleElement.h"
#include "nsIDOMHTMLTableCaptionElem.h"
#include "nsIDOMHTMLTableCellElement.h"
#include "nsIDOMHTMLTableColElement.h"
#include "nsIDOMHTMLTableElement.h"
#include "nsIDOMHTMLTableRowElement.h"
#include "nsIDOMHTMLTableSectionElem.h"
#include "nsIDOMHTMLTextAreaElement.h"
#include "nsIDOMNSHTMLTextAreaElement.h"
#include "nsIDOMHTMLTitleElement.h"
#include "nsIDOMHTMLUListElement.h"
#include "nsIDOMMediaError.h"
#include "nsIDOMTimeRanges.h"
#include "nsIDOMHTMLSourceElement.h"
#include "nsIDOMHTMLVideoElement.h"
#include "nsIDOMHTMLAudioElement.h"
#include "nsIDOMProgressEvent.h"
#include "nsIDOMNSUIEvent.h"
#include "nsIDOMCSS2Properties.h"
#include "nsIDOMCSSCharsetRule.h"
#include "nsIDOMCSSImportRule.h"
#include "nsIDOMCSSMediaRule.h"
#include "nsIDOMCSSFontFaceRule.h"
#include "nsIDOMCSSMozDocumentRule.h"
#include "nsIDOMCSSPrimitiveValue.h"
#include "nsIDOMCSSStyleRule.h"
#include "nsIDOMCSSStyleSheet.h"
#include "nsDOMCSSValueList.h"
#include "nsIDOMOrientationEvent.h"
#include "nsIDOMRange.h"
#include "nsIDOMNSRange.h"
#include "nsIDOMRangeException.h"
#include "nsIDOMNodeIterator.h"
#include "nsIDOMTreeWalker.h"
#include "nsIDOMXULDocument.h"
#include "nsIDOMXULElement.h"
#include "nsIDOMXULCommandDispatcher.h"
#include "nsIDOMCrypto.h"
#include "nsIDOMCRMFObject.h"
#include "nsIControllers.h"
#include "nsISelection.h"
#include "nsISelection3.h"
#include "nsIBoxObject.h"
#ifdef MOZ_XUL
#include "nsITreeSelection.h"
#include "nsITreeContentView.h"
#include "nsITreeView.h"
#include "nsIXULTemplateBuilder.h"
#include "nsTreeColumns.h"
#endif
#include "nsIDOMXPathException.h"
#include "nsIDOMXPathExpression.h"
#include "nsIDOMNSXPathExpression.h"
#include "nsIDOMXPathNSResolver.h"
#include "nsIDOMXPathResult.h"

#ifdef MOZ_SVG
#include "nsIDOMGetSVGDocument.h"
#include "nsIDOMSVGAElement.h"
#include "nsIDOMSVGAltGlyphElement.h"
#include "nsIDOMSVGAngle.h"
#include "nsIDOMSVGAnimatedAngle.h"
#include "nsIDOMSVGAnimatedBoolean.h"
#include "nsIDOMSVGAnimatedEnum.h"
#include "nsIDOMSVGAnimatedInteger.h"
#include "nsIDOMSVGAnimatedLength.h"
#include "nsIDOMSVGAnimatedLengthList.h"
#include "nsIDOMSVGAnimatedNumber.h"
#include "nsIDOMSVGAnimatedNumberList.h"
#include "nsIDOMSVGAnimatedPathData.h"
#include "nsIDOMSVGAnimatedPoints.h"
#include "nsIDOMSVGAnimPresAspRatio.h"
#include "nsIDOMSVGAnimatedRect.h"
#include "nsIDOMSVGAnimatedString.h"
#ifdef MOZ_SMIL
#include "nsIDOMSVGAnimateElement.h"
#include "nsIDOMSVGAnimateTransformElement.h"
#include "nsIDOMSVGAnimateMotionElement.h"
#include "nsIDOMSVGMpathElement.h"
#include "nsIDOMSVGSetElement.h"
#include "nsIDOMSVGAnimationElement.h"
#include "nsIDOMElementTimeControl.h"
#include "nsIDOMTimeEvent.h"
#endif // MOZ_SMIL
#include "nsIDOMSVGAnimTransformList.h"
#include "nsIDOMSVGCircleElement.h"
#include "nsIDOMSVGClipPathElement.h"
#include "nsIDOMSVGDefsElement.h"
#include "nsIDOMSVGDescElement.h"
#include "nsIDOMSVGDocument.h"
#include "nsIDOMSVGElement.h"
#include "nsIDOMSVGEllipseElement.h"
#include "nsIDOMSVGEvent.h"
#include "nsIDOMSVGException.h"
#include "nsIDOMSVGFilterElement.h"
#include "nsIDOMSVGFilters.h"
#include "nsIDOMSVGFitToViewBox.h"
#include "nsIDOMSVGForeignObjectElem.h"
#include "nsIDOMSVGGElement.h"
#include "nsIDOMSVGGradientElement.h"
#include "nsIDOMSVGImageElement.h"
#include "nsIDOMSVGLength.h"
#include "nsIDOMSVGLengthList.h"
#include "nsIDOMSVGLineElement.h"
#include "nsIDOMSVGLocatable.h"
#include "nsIDOMSVGMarkerElement.h"
#include "nsIDOMSVGMaskElement.h"
#include "nsIDOMSVGMatrix.h"
#include "nsIDOMSVGMetadataElement.h"
#include "nsIDOMSVGNumber.h"
#include "nsIDOMSVGNumberList.h"
#include "nsIDOMSVGPathElement.h"
#include "nsIDOMSVGPathSeg.h"
#include "nsIDOMSVGPathSegList.h"
#include "nsIDOMSVGPatternElement.h"
#include "nsIDOMSVGPoint.h"
#include "nsIDOMSVGPointList.h"
#include "nsIDOMSVGPolygonElement.h"
#include "nsIDOMSVGPolylineElement.h"
#include "nsIDOMSVGPresAspectRatio.h"
#include "nsIDOMSVGRect.h"
#include "nsIDOMSVGRectElement.h"
#include "nsIDOMSVGScriptElement.h"
#include "nsIDOMSVGStopElement.h"
#include "nsIDOMSVGStylable.h"
#include "nsIDOMSVGStyleElement.h"
#include "nsIDOMSVGSVGElement.h"
#include "nsIDOMSVGSwitchElement.h"
#include "nsIDOMSVGSymbolElement.h"
#include "nsIDOMSVGTextElement.h"
#include "nsIDOMSVGTextPathElement.h"
#include "nsIDOMSVGTitleElement.h"
#include "nsIDOMSVGTransform.h"
#include "nsIDOMSVGTransformable.h"
#include "nsIDOMSVGTransformList.h"
#include "nsIDOMSVGTSpanElement.h"
#include "nsIDOMSVGURIReference.h"
#include "nsIDOMSVGUseElement.h"
#include "nsIDOMSVGUnitTypes.h"
#include "nsIDOMSVGZoomAndPan.h"
#include "nsIDOMSVGZoomEvent.h"
#endif // MOZ_SVG

#include "nsIDOMCanvasRenderingContext2D.h"
#include "nsICanvasRenderingContextWebGL.h"

#include "nsIImageDocument.h"

// Storage includes
#include "nsDOMStorage.h"

// Drag and drop
#include "nsIDOMDataTransfer.h"

// Geolocation
#include "nsIDOMGeoGeolocation.h"
#include "nsIDOMGeoPosition.h"
#include "nsIDOMGeoPositionCoords.h"
#include "nsIDOMGeoPositionError.h"

// Workers
#include "nsDOMWorker.h"

#include "nsDOMFile.h"
#include "nsDOMFileReader.h"
#include "nsIDOMFileException.h"
#include "nsIDOMFileError.h"
#include "nsIDOMFormData.h"

// Simple gestures include
#include "nsIDOMSimpleGestureEvent.h"

#include "nsIDOMNSMouseEvent.h"

#include "nsIDOMMozTouchEvent.h"

#include "nsIEventListenerService.h"
#include "nsIFrameMessageManager.h"
#include "mozilla/dom/Element.h"
#include "nsHTMLSelectElement.h"
#include "nsHTMLLegendElement.h"

using namespace mozilla::dom;

#include "mozilla/dom/indexedDB/IDBFactory.h"
#include "mozilla/dom/indexedDB/IDBRequest.h"
#include "mozilla/dom/indexedDB/IDBDatabase.h"
#include "mozilla/dom/indexedDB/IDBEvents.h"
#include "mozilla/dom/indexedDB/IDBObjectStore.h"
#include "mozilla/dom/indexedDB/IDBTransaction.h"
#include "mozilla/dom/indexedDB/IDBCursor.h"
#include "mozilla/dom/indexedDB/IDBKeyRange.h"
#include "mozilla/dom/indexedDB/IDBIndex.h"

static NS_DEFINE_CID(kDOMSOF_CID, NS_DOM_SCRIPT_OBJECT_FACTORY_CID);

static const char kDOMStringBundleURL[] =
  "chrome://global/locale/dom/dom.properties";

// NOTE: DEFAULT_SCRIPTABLE_FLAGS and DOM_DEFAULT_SCRIPTABLE_FLAGS
//       are defined in nsIDOMClassInfo.h.

#define COMMON_WINDOW_SCRIPTABLE_FLAGS                                        \
 (nsIXPCScriptable::WANT_GETPROPERTY |                                        \
  nsIXPCScriptable::WANT_SETPROPERTY |                                        \
  nsIXPCScriptable::WANT_PRECREATE |                                          \
  nsIXPCScriptable::WANT_ADDPROPERTY |                                        \
  nsIXPCScriptable::WANT_DELPROPERTY |                                        \
  nsIXPCScriptable::WANT_FINALIZE |                                           \
  nsIXPCScriptable::WANT_EQUALITY |                                           \
  nsIXPCScriptable::DONT_ENUM_QUERY_INTERFACE)

#define INNER_WINDOW_SCRIPTABLE_FLAGS                                         \
 (COMMON_WINDOW_SCRIPTABLE_FLAGS |                                            \
  nsIXPCScriptable::WANT_OUTER_OBJECT)                                        \

#define OUTER_WINDOW_SCRIPTABLE_FLAGS                                         \
 (COMMON_WINDOW_SCRIPTABLE_FLAGS |                                            \
  nsIXPCScriptable::WANT_INNER_OBJECT |                                       \
  nsIXPCScriptable::WANT_NEWENUMERATE)

#define NODE_SCRIPTABLE_FLAGS                                                 \
 ((DOM_DEFAULT_SCRIPTABLE_FLAGS |                                             \
   nsIXPCScriptable::WANT_GETPROPERTY |                                       \
   nsIXPCScriptable::WANT_ADDPROPERTY |                                       \
   nsIXPCScriptable::WANT_SETPROPERTY) &                                      \
  ~nsIXPCScriptable::USE_JSSTUB_FOR_ADDPROPERTY)

// We need to let JavaScript QI elements to interfaces that are not in
// the classinfo since XBL can be used to dynamically implement new
// unknown interfaces on elements, accessibility relies on this being
// possible.

#define ELEMENT_SCRIPTABLE_FLAGS                                              \
  ((NODE_SCRIPTABLE_FLAGS & ~nsIXPCScriptable::CLASSINFO_INTERFACES_ONLY) |   \
   nsIXPCScriptable::WANT_POSTCREATE |                                        \
   nsIXPCScriptable::WANT_ENUMERATE)

#define EXTERNAL_OBJ_SCRIPTABLE_FLAGS                                         \
  ((ELEMENT_SCRIPTABLE_FLAGS &                                                \
    ~nsIXPCScriptable::USE_JSSTUB_FOR_SETPROPERTY) |                          \
   nsIXPCScriptable::WANT_POSTCREATE |                                        \
   nsIXPCScriptable::WANT_GETPROPERTY |                                       \
   nsIXPCScriptable::WANT_SETPROPERTY |                                       \
   nsIXPCScriptable::WANT_CALL)

#define DOCUMENT_SCRIPTABLE_FLAGS                                             \
  (NODE_SCRIPTABLE_FLAGS |                                                    \
   nsIXPCScriptable::WANT_POSTCREATE |                                        \
   nsIXPCScriptable::WANT_ADDPROPERTY |                                       \
   nsIXPCScriptable::WANT_GETPROPERTY |                                       \
   nsIXPCScriptable::WANT_ENUMERATE)

#define ARRAY_SCRIPTABLE_FLAGS                                                \
  (DOM_DEFAULT_SCRIPTABLE_FLAGS       |                                       \
   nsIXPCScriptable::WANT_GETPROPERTY |                                       \
   nsIXPCScriptable::WANT_ENUMERATE)

#define EVENTTARGET_SCRIPTABLE_FLAGS                                          \
  (DOM_DEFAULT_SCRIPTABLE_FLAGS       |                                       \
   nsIXPCScriptable::WANT_ADDPROPERTY)

#define DOMCLASSINFO_STANDARD_FLAGS                                           \
  (nsIClassInfo::MAIN_THREAD_ONLY | nsIClassInfo::DOM_OBJECT)


#ifdef NS_DEBUG
#define NS_DEFINE_CLASSINFO_DATA_DEBUG(_class)                                \
    eDOMClassInfo_##_class##_id,
#else
#define NS_DEFINE_CLASSINFO_DATA_DEBUG(_class)                                \
  // nothing
#endif

DOMCI_DATA(Crypto, void)
DOMCI_DATA(CRMFObject, void)
DOMCI_DATA(SmartCardEvent, void)
DOMCI_DATA(ContentFrameMessageManager, void)

DOMCI_DATA(DOMPrototype, void)
DOMCI_DATA(DOMConstructor, void)

DOMCI_DATA(Worker, void)
DOMCI_DATA(ChromeWorker, void)

DOMCI_DATA(Notation, void)

#define NS_DEFINE_CLASSINFO_DATA_WITH_NAME(_class, _name, _helper,            \
                                           _flags)                            \
  { #_name,                                                                   \
    nsnull,                                                                   \
    { _helper::doCreate },                                                    \
    nsnull,                                                                   \
    nsnull,                                                                   \
    nsnull,                                                                   \
    _flags,                                                                   \
    PR_TRUE,                                                                  \
    0,                                                                        \
    PR_FALSE,                                                                 \
    NS_DEFINE_CLASSINFO_DATA_DEBUG(_class)                                    \
  },

#define NS_DEFINE_CHROME_ONLY_CLASSINFO_DATA_WITH_NAME(_class, _name,         \
                                                       _helper, _flags)       \
  { #_name,                                                                   \
    nsnull,                                                                   \
    { _helper::doCreate },                                                    \
    nsnull,                                                                   \
    nsnull,                                                                   \
    nsnull,                                                                   \
    _flags,                                                                   \
    PR_TRUE,                                                                  \
    0,                                                                        \
    PR_TRUE,                                                                  \
    NS_DEFINE_CLASSINFO_DATA_DEBUG(_class)                                    \
  },

#define NS_DEFINE_CLASSINFO_DATA(_class, _helper, _flags)                     \
  NS_DEFINE_CLASSINFO_DATA_WITH_NAME(_class, _class, _helper, _flags)

#define NS_DEFINE_CHROME_ONLY_CLASSINFO_DATA(_class, _helper, _flags)          \
  NS_DEFINE_CHROME_ONLY_CLASSINFO_DATA_WITH_NAME(_class, _class, _helper, \
                                                 _flags)

// This list of NS_DEFINE_CLASSINFO_DATA macros is what gives the DOM
// classes their correct behavior when used through XPConnect. The
// arguments that are passed to NS_DEFINE_CLASSINFO_DATA are
//
// 1. Class name as it should appear in JavaScript, this name is also
//    used to find the id of the class in nsDOMClassInfo
//    (i.e. e<classname>_id)
// 2. Scriptable helper class
// 3. nsIClassInfo/nsIXPCScriptable flags (i.e. for GetScriptableFlags)

static nsDOMClassInfoData sClassInfoData[] = {
  // Base classes

  // The Window class lets you QI into interfaces that are not in the
  // flattened set (i.e. nsIXPCScriptable::CLASSINFO_INTERFACES_ONLY
  // is not set), because of this make sure all scriptable interfaces
  // that are implemented by nsGlobalWindow can securely be exposed
  // to JS.


  NS_DEFINE_CLASSINFO_DATA(Window, nsOuterWindowSH,
                           DEFAULT_SCRIPTABLE_FLAGS |
                           OUTER_WINDOW_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(InnerWindow, nsInnerWindowSH,
                           DEFAULT_SCRIPTABLE_FLAGS |
                           INNER_WINDOW_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(Location, nsLocationSH,
                           (DOM_DEFAULT_SCRIPTABLE_FLAGS &
                            ~nsIXPCScriptable::ALLOW_PROP_MODS_TO_PROTOTYPE))

  NS_DEFINE_CLASSINFO_DATA(Navigator, nsNavigatorSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS |
                           nsIXPCScriptable::WANT_PRECREATE)
  NS_DEFINE_CLASSINFO_DATA(Plugin, nsPluginSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(PluginArray, nsPluginArraySH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(MimeType, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(MimeTypeArray, nsMimeTypeArraySH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(BarProp, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(History, nsHistorySH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(Screen, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(DOMPrototype, nsDOMConstructorSH,
                           DOM_BASE_SCRIPTABLE_FLAGS |
                           nsIXPCScriptable::WANT_HASINSTANCE |
                           nsIXPCScriptable::DONT_ENUM_QUERY_INTERFACE)
  NS_DEFINE_CLASSINFO_DATA(DOMConstructor, nsDOMConstructorSH,
                           DOM_BASE_SCRIPTABLE_FLAGS |
                           nsIXPCScriptable::WANT_HASINSTANCE |
                           nsIXPCScriptable::WANT_CALL |
                           nsIXPCScriptable::WANT_CONSTRUCT |
                           nsIXPCScriptable::DONT_ENUM_QUERY_INTERFACE)

  // Core classes
  NS_DEFINE_CLASSINFO_DATA(XMLDocument, nsDocumentSH,
                           DOCUMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(DocumentType, nsNodeSH,
                           NODE_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(DOMImplementation, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(DOMException, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(DOMTokenList, nsDOMTokenListSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(DOMSettableTokenList, nsDOMTokenListSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(DocumentFragment, nsNodeSH, NODE_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(Element, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(Attr, nsAttributeSH,
                           NODE_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(Text, nsNodeSH,
                           NODE_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(Comment, nsNodeSH,
                           NODE_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CDATASection, nsNodeSH, NODE_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(ProcessingInstruction, nsNodeSH,
                           NODE_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(Notation, nsNodeSH, NODE_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(NodeList, nsNodeListSH, ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(NamedNodeMap, nsNamedNodeMapSH,
                           ARRAY_SCRIPTABLE_FLAGS)

  // Misc Core related classes

  // Event
  NS_DEFINE_CLASSINFO_DATA(Event, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(MutationEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(UIEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(MouseEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(MouseScrollEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(DragEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(KeyboardEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(PopupBlockedEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(OrientationEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  // Misc HTML classes
  NS_DEFINE_CLASSINFO_DATA(HTMLDocument, nsHTMLDocumentSH,
                           DOCUMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLOptionsCollection,
                           nsHTMLOptionsCollectionSH,
                           ARRAY_SCRIPTABLE_FLAGS |
                           nsIXPCScriptable::WANT_SETPROPERTY)
  NS_DEFINE_CLASSINFO_DATA(HTMLCollection,
                           nsHTMLCollectionSH,
                           ARRAY_SCRIPTABLE_FLAGS)

  // HTML element classes
  NS_DEFINE_CLASSINFO_DATA(HTMLElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLAnchorElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLAppletElement, nsHTMLPluginObjElementSH,
                           EXTERNAL_OBJ_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLAreaElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLBRElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLBaseElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLBodyElement, nsHTMLBodyElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLButtonElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLDListElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLDelElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLDirectoryElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLDivElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLEmbedElement, nsHTMLPluginObjElementSH,
                           EXTERNAL_OBJ_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLFieldSetElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLFontElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLFormElement, nsHTMLFormElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS |
                           nsIXPCScriptable::WANT_GETPROPERTY |
                           nsIXPCScriptable::WANT_NEWENUMERATE)
  NS_DEFINE_CLASSINFO_DATA(HTMLFrameElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLFrameSetElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLHRElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLHeadElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLHeadingElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLHtmlElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLIFrameElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLImageElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLInputElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLInsElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLIsIndexElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLLIElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLLabelElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLLegendElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLLinkElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLMapElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLMenuElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLMetaElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLOListElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLObjectElement, nsHTMLPluginObjElementSH,
                           EXTERNAL_OBJ_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLOptGroupElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLOptionElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLOutputElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLParagraphElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLParamElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLPreElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLQuoteElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLScriptElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLSelectElement, nsHTMLSelectElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS |
                           nsIXPCScriptable::WANT_GETPROPERTY)
  NS_DEFINE_CLASSINFO_DATA(HTMLSpanElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLStyleElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLTableCaptionElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLTableCellElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLTableColElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLTableElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLTableRowElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLTableSectionElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLTextAreaElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLTitleElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLUListElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLUnknownElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)

  // Constraint Validation API classes
  NS_DEFINE_CLASSINFO_DATA(ValidityState, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  // CSS classes
  NS_DEFINE_CLASSINFO_DATA(CSSStyleRule, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CSSCharsetRule, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CSSImportRule, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CSSMediaRule, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CSSNameSpaceRule, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CSSRuleList, nsCSSRuleListSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CSSGroupRuleRuleList, nsCSSRuleListSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(MediaList, nsMediaListSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(StyleSheetList, nsStyleSheetListSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CSSStyleSheet, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CSSStyleDeclaration, nsCSSStyleDeclSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(ComputedCSSStyleDeclaration, nsCSSStyleDeclSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(ROCSSPrimitiveValue, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  // Range classes
  NS_DEFINE_CLASSINFO_DATA(Range, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(Selection, nsDOMGenericSH,
                           DEFAULT_SCRIPTABLE_FLAGS)

  // XUL classes
#ifdef MOZ_XUL
  NS_DEFINE_CLASSINFO_DATA(XULDocument, nsDocumentSH,
                           DOCUMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(XULElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(XULCommandDispatcher, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
#endif
  NS_DEFINE_CLASSINFO_DATA(XULControllers, nsNonDOMObjectSH,
                           DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(BoxObject, nsDOMGenericSH,
                           DEFAULT_SCRIPTABLE_FLAGS)
#ifdef MOZ_XUL
  NS_DEFINE_CLASSINFO_DATA(TreeSelection, nsDOMGenericSH,
                           DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(TreeContentView, nsDOMGenericSH,
                           DEFAULT_SCRIPTABLE_FLAGS)
#endif

  // Crypto classes
  NS_DEFINE_CLASSINFO_DATA(Crypto, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CRMFObject, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  // DOM Traversal classes
  NS_DEFINE_CLASSINFO_DATA(TreeWalker, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  // We are now trying to preserve binary compat in classinfo.  No
  // more putting things in those categories up there.  New entries
  // are to be added to the end of the list
  NS_DEFINE_CLASSINFO_DATA(CSSRect, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  // DOM Chrome Window class.
  NS_DEFINE_CLASSINFO_DATA(ChromeWindow, nsOuterWindowSH,
                           DEFAULT_SCRIPTABLE_FLAGS |
                           OUTER_WINDOW_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(InnerChromeWindow, nsInnerWindowSH,
                           DEFAULT_SCRIPTABLE_FLAGS |
                           INNER_WINDOW_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(CSSRGBColor, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(RangeException, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(CSSValueList, nsCSSValueListSH,
                           ARRAY_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA_WITH_NAME(ContentList, HTMLCollection,
                                     nsContentListSH, ARRAY_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(XMLStylesheetProcessingInstruction, nsNodeSH,
                           NODE_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(ImageDocument, nsHTMLDocumentSH,
                           DOCUMENT_SCRIPTABLE_FLAGS)

#ifdef MOZ_XUL
  NS_DEFINE_CLASSINFO_DATA(XULTemplateBuilder, nsDOMGenericSH,
                           DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(XULTreeBuilder, nsDOMGenericSH,
                           DEFAULT_SCRIPTABLE_FLAGS)
#endif

  NS_DEFINE_CLASSINFO_DATA(DOMStringList, nsStringListSH,
                           ARRAY_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(NameList, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

#ifdef MOZ_XUL
  NS_DEFINE_CLASSINFO_DATA(TreeColumn, nsDOMGenericSH,
                           DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(TreeColumns, nsTreeColumnsSH,
                           ARRAY_SCRIPTABLE_FLAGS)
#endif

  NS_DEFINE_CLASSINFO_DATA(CSSMozDocumentRule, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(BeforeUnloadEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

#ifdef MOZ_SVG
  // SVG document
  NS_DEFINE_CLASSINFO_DATA(SVGDocument, nsDocumentSH,
                           DOCUMENT_SCRIPTABLE_FLAGS)

  // SVG element classes
  NS_DEFINE_CLASSINFO_DATA(SVGAElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAltGlyphElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
#ifdef MOZ_SMIL
  NS_DEFINE_CLASSINFO_DATA(SVGAnimateElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimateTransformElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimateMotionElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGMpathElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGSetElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(TimeEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
#endif // MOZ_SMIL
  NS_DEFINE_CLASSINFO_DATA(SVGCircleElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGClipPathElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGDefsElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGDescElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGEllipseElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEBlendElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEColorMatrixElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEComponentTransferElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFECompositeElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEConvolveMatrixElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEDiffuseLightingElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEDisplacementMapElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEDistantLightElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEFloodElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEFuncAElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEFuncBElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEFuncGElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEFuncRElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEGaussianBlurElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEImageElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEMergeElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEMergeNodeElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEMorphologyElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEOffsetElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFEPointLightElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFESpecularLightingElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFESpotLightElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFETileElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFETurbulenceElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGFilterElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGGElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGImageElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGLinearGradientElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGLineElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGMarkerElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGMaskElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGMetadataElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPatternElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPolygonElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPolylineElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGRadialGradientElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGRectElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGScriptElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGStopElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGStyleElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGSVGElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGSwitchElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGSymbolElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGTextElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGTextPathElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGTitleElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGTSpanElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGUseElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)

  // other SVG classes
  NS_DEFINE_CLASSINFO_DATA(SVGAngle, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedAngle, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedBoolean, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedEnumeration, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedInteger, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedLength, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedLengthList, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedNumber, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)    
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedNumberList, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)    
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedPreserveAspectRatio, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedRect, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedString, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGAnimatedTransformList, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGException, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGLength, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGLengthList, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGMatrix, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGNumber, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGNumberList, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)    
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegArcAbs, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegArcRel, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegClosePath, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegCurvetoCubicAbs, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegCurvetoCubicRel, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegCurvetoCubicSmoothAbs, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegCurvetoCubicSmoothRel, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegCurvetoQuadraticAbs, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegCurvetoQuadraticRel, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegCurvetoQuadraticSmoothAbs, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegCurvetoQuadraticSmoothRel, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegLinetoAbs, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegLinetoHorizontalAbs, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegLinetoHorizontalRel, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegLinetoRel, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegLinetoVerticalAbs, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegLinetoVerticalRel, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegList, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegMovetoAbs, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPathSegMovetoRel, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPoint, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPointList, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGPreserveAspectRatio, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGRect, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGTransform, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGTransformList, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(SVGZoomEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
#endif // MOZ_SVG

  NS_DEFINE_CLASSINFO_DATA(HTMLCanvasElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CanvasRenderingContext2D, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CanvasGradient, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CanvasPattern, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(TextMetrics, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(SmartCardEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(PageTransitionEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(WindowUtils, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(XSLTProcessor, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(XPathEvaluator, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(XPathException, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(XPathExpression, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(XPathNSResolver, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(XPathResult, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  // WhatWG Storage

  // mrbkap says we don't need WANT_ADDPROPERTY on Storage objects
  // since a call to addProperty() is always followed by a call to
  // setProperty(), except in the case when a getter or setter is set
  // for a property. But we don't care about getters or setters here.
  NS_DEFINE_CLASSINFO_DATA(StorageObsolete, nsStorageSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS |
                           nsIXPCScriptable::WANT_NEWRESOLVE |
                           nsIXPCScriptable::WANT_GETPROPERTY |
                           nsIXPCScriptable::WANT_SETPROPERTY |
                           nsIXPCScriptable::WANT_DELPROPERTY |
                           nsIXPCScriptable::DONT_ENUM_STATIC_PROPS |
                           nsIXPCScriptable::WANT_NEWENUMERATE)

  NS_DEFINE_CLASSINFO_DATA(Storage, nsStorage2SH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS |
                           nsIXPCScriptable::WANT_NEWRESOLVE |
                           nsIXPCScriptable::WANT_GETPROPERTY |
                           nsIXPCScriptable::WANT_SETPROPERTY |
                           nsIXPCScriptable::WANT_DELPROPERTY |
                           nsIXPCScriptable::DONT_ENUM_STATIC_PROPS |
                           nsIXPCScriptable::WANT_NEWENUMERATE)
  NS_DEFINE_CLASSINFO_DATA(StorageList, nsStorageListSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(StorageItem, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(StorageEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(StorageEventObsolete, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(DOMParser, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(XMLSerializer, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(XMLHttpProgressEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(XMLHttpRequest, nsEventTargetSH,
                           EVENTTARGET_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(ClientRect, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(ClientRectList, nsClientRectListSH,
                           ARRAY_SCRIPTABLE_FLAGS)

#ifdef MOZ_SVG
  NS_DEFINE_CLASSINFO_DATA(SVGForeignObjectElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
#endif

  NS_DEFINE_CLASSINFO_DATA(XULCommandEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(CommandEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(OfflineResourceList, nsOfflineResourceListSH,
                           ARRAY_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(FileList, nsFileListSH,
                           ARRAY_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(File, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(FileException, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(FileError, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(FileReader, nsEventTargetSH,
                           EVENTTARGET_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(ModalContentWindow, nsOuterWindowSH,
                           DEFAULT_SCRIPTABLE_FLAGS |
                           OUTER_WINDOW_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(InnerModalContentWindow, nsInnerWindowSH,
                           DEFAULT_SCRIPTABLE_FLAGS |
                           INNER_WINDOW_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(DataContainerEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(MessageEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(GeoGeolocation, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  
  NS_DEFINE_CLASSINFO_DATA(GeoPosition, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS) 
  
  NS_DEFINE_CLASSINFO_DATA(GeoPositionCoords, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(GeoPositionError, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  
  NS_DEFINE_CLASSINFO_DATA(CSSFontFaceRule, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(CSSFontFaceStyleDecl, nsCSSStyleDeclSH,
                           ARRAY_SCRIPTABLE_FLAGS)

#if defined(MOZ_MEDIA)
  NS_DEFINE_CLASSINFO_DATA(HTMLVideoElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLSourceElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(MediaError, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(HTMLAudioElement, nsElementSH,
                           ELEMENT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(TimeRanges, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
#endif

  NS_DEFINE_CLASSINFO_DATA(ProgressEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(XMLHttpRequestUpload, nsEventTargetSH,
                           EVENTTARGET_SCRIPTABLE_FLAGS)

  // DOM Traversal NodeIterator class  
  NS_DEFINE_CLASSINFO_DATA(NodeIterator, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  // data transfer for drag and drop
  NS_DEFINE_CLASSINFO_DATA(DataTransfer, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(NotifyPaintEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(NotifyAudioAvailableEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(SimpleGestureEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(MozTouchEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
#ifdef MOZ_MATHML
  NS_DEFINE_CLASSINFO_DATA_WITH_NAME(MathMLElement, Element, nsElementSH,
                                     ELEMENT_SCRIPTABLE_FLAGS)
#endif

  NS_DEFINE_CLASSINFO_DATA(Worker, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CHROME_ONLY_CLASSINFO_DATA(ChromeWorker, nsDOMGenericSH,
                                       DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(CanvasRenderingContextWebGL, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(WebGLBuffer, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(WebGLTexture, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(WebGLProgram, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(WebGLShader, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(WebGLFramebuffer, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(WebGLRenderbuffer, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(WebGLUniformLocation, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(WebGLActiveInfo, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(PaintRequest, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(PaintRequestList, nsPaintRequestListSH,
                           ARRAY_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(ScrollAreaEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(PopStateEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(EventListenerInfo, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(TransitionEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(ContentFrameMessageManager, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(FormData, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(WebSocket, nsEventTargetSH,
                           EVENTTARGET_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(CloseEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)

  NS_DEFINE_CLASSINFO_DATA(IDBFactory, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(IDBRequest, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(IDBDatabase, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(IDBErrorEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(IDBSuccessEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(IDBTransactionEvent, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(IDBObjectStore, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(IDBTransaction, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(IDBCursor, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(IDBKeyRange, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
  NS_DEFINE_CLASSINFO_DATA(IDBIndex, nsDOMGenericSH,
                           DOM_DEFAULT_SCRIPTABLE_FLAGS)
};

// Objects that should be constructable through |new Name();|
struct nsContractIDMapData
{
  PRInt32 mDOMClassInfoID;
  const char *mContractID;
};

#define NS_DEFINE_CONSTRUCTOR_DATA(_class, _contract_id)                      \
  { eDOMClassInfo_##_class##_id, _contract_id },

static const nsContractIDMapData kConstructorMap[] =
{
  NS_DEFINE_CONSTRUCTOR_DATA(DOMParser, NS_DOMPARSER_CONTRACTID)
  NS_DEFINE_CONSTRUCTOR_DATA(FileReader, NS_FILEREADER_CONTRACTID)
  NS_DEFINE_CONSTRUCTOR_DATA(FormData, NS_FORMDATA_CONTRACTID)
  NS_DEFINE_CONSTRUCTOR_DATA(XMLSerializer, NS_XMLSERIALIZER_CONTRACTID)
  NS_DEFINE_CONSTRUCTOR_DATA(XMLHttpRequest, NS_XMLHTTPREQUEST_CONTRACTID)
  NS_DEFINE_CONSTRUCTOR_DATA(WebSocket, NS_WEBSOCKET_CONTRACTID)
  NS_DEFINE_CONSTRUCTOR_DATA(XPathEvaluator, NS_XPATH_EVALUATOR_CONTRACTID)
  NS_DEFINE_CONSTRUCTOR_DATA(XSLTProcessor,
                             "@mozilla.org/document-transformer;1?type=xslt")
};

struct nsConstructorFuncMapData
{
  PRInt32 mDOMClassInfoID;
  nsDOMConstructorFunc mConstructorFunc;
};

#define NS_DEFINE_CONSTRUCTOR_FUNC_DATA(_class, _func)                        \
  { eDOMClassInfo_##_class##_id, _func },

static const nsConstructorFuncMapData kConstructorFuncMap[] =
{
  NS_DEFINE_CONSTRUCTOR_FUNC_DATA(Worker, nsDOMWorker::NewWorker)
  NS_DEFINE_CONSTRUCTOR_FUNC_DATA(ChromeWorker, nsDOMWorker::NewChromeWorker)
};

nsIXPConnect *nsDOMClassInfo::sXPConnect = nsnull;
nsIScriptSecurityManager *nsDOMClassInfo::sSecMan = nsnull;
PRBool nsDOMClassInfo::sIsInitialized = PR_FALSE;
PRBool nsDOMClassInfo::sDisableDocumentAllSupport = PR_FALSE;
PRBool nsDOMClassInfo::sDisableGlobalScopePollutionSupport = PR_FALSE;


jsid nsDOMClassInfo::sTop_id             = JSID_VOID;
jsid nsDOMClassInfo::sParent_id          = JSID_VOID;
jsid nsDOMClassInfo::sScrollbars_id      = JSID_VOID;
jsid nsDOMClassInfo::sLocation_id        = JSID_VOID;
jsid nsDOMClassInfo::sConstructor_id     = JSID_VOID;
jsid nsDOMClassInfo::s_content_id        = JSID_VOID;
jsid nsDOMClassInfo::sContent_id         = JSID_VOID;
jsid nsDOMClassInfo::sMenubar_id         = JSID_VOID;
jsid nsDOMClassInfo::sToolbar_id         = JSID_VOID;
jsid nsDOMClassInfo::sLocationbar_id     = JSID_VOID;
jsid nsDOMClassInfo::sPersonalbar_id     = JSID_VOID;
jsid nsDOMClassInfo::sStatusbar_id       = JSID_VOID;
jsid nsDOMClassInfo::sDialogArguments_id = JSID_VOID;
jsid nsDOMClassInfo::sControllers_id     = JSID_VOID;
jsid nsDOMClassInfo::sLength_id          = JSID_VOID;
jsid nsDOMClassInfo::sInnerHeight_id     = JSID_VOID;
jsid nsDOMClassInfo::sInnerWidth_id      = JSID_VOID;
jsid nsDOMClassInfo::sOuterHeight_id     = JSID_VOID;
jsid nsDOMClassInfo::sOuterWidth_id      = JSID_VOID;
jsid nsDOMClassInfo::sScreenX_id         = JSID_VOID;
jsid nsDOMClassInfo::sScreenY_id         = JSID_VOID;
jsid nsDOMClassInfo::sStatus_id          = JSID_VOID;
jsid nsDOMClassInfo::sName_id            = JSID_VOID;
jsid nsDOMClassInfo::sOnmousedown_id     = JSID_VOID;
jsid nsDOMClassInfo::sOnmouseup_id       = JSID_VOID;
jsid nsDOMClassInfo::sOnclick_id         = JSID_VOID;
jsid nsDOMClassInfo::sOndblclick_id      = JSID_VOID;
jsid nsDOMClassInfo::sOncontextmenu_id   = JSID_VOID;
jsid nsDOMClassInfo::sOnmouseover_id     = JSID_VOID;
jsid nsDOMClassInfo::sOnmouseout_id      = JSID_VOID;
jsid nsDOMClassInfo::sOnkeydown_id       = JSID_VOID;
jsid nsDOMClassInfo::sOnkeyup_id         = JSID_VOID;
jsid nsDOMClassInfo::sOnkeypress_id      = JSID_VOID;
jsid nsDOMClassInfo::sOnmousemove_id     = JSID_VOID;
jsid nsDOMClassInfo::sOnfocus_id         = JSID_VOID;
jsid nsDOMClassInfo::sOnblur_id          = JSID_VOID;
jsid nsDOMClassInfo::sOnsubmit_id        = JSID_VOID;
jsid nsDOMClassInfo::sOnreset_id         = JSID_VOID;
jsid nsDOMClassInfo::sOnchange_id        = JSID_VOID;
jsid nsDOMClassInfo::sOnselect_id        = JSID_VOID;
jsid nsDOMClassInfo::sOnload_id          = JSID_VOID;
jsid nsDOMClassInfo::sOnpopstate_id      = JSID_VOID;
jsid nsDOMClassInfo::sOnbeforeunload_id  = JSID_VOID;
jsid nsDOMClassInfo::sOnunload_id        = JSID_VOID;
jsid nsDOMClassInfo::sOnhashchange_id    = JSID_VOID;
jsid nsDOMClassInfo::sOnreadystatechange_id = JSID_VOID;
jsid nsDOMClassInfo::sOnpageshow_id      = JSID_VOID;
jsid nsDOMClassInfo::sOnpagehide_id      = JSID_VOID;
jsid nsDOMClassInfo::sOnabort_id         = JSID_VOID;
jsid nsDOMClassInfo::sOnerror_id         = JSID_VOID;
jsid nsDOMClassInfo::sOnpaint_id         = JSID_VOID;
jsid nsDOMClassInfo::sOnresize_id        = JSID_VOID;
jsid nsDOMClassInfo::sOnscroll_id        = JSID_VOID;
jsid nsDOMClassInfo::sOndrag_id          = JSID_VOID;
jsid nsDOMClassInfo::sOndragend_id       = JSID_VOID;
jsid nsDOMClassInfo::sOndragenter_id     = JSID_VOID;
jsid nsDOMClassInfo::sOndragleave_id     = JSID_VOID;
jsid nsDOMClassInfo::sOndragover_id      = JSID_VOID;
jsid nsDOMClassInfo::sOndragstart_id     = JSID_VOID;
jsid nsDOMClassInfo::sOndrop_id          = JSID_VOID;
jsid nsDOMClassInfo::sScrollX_id         = JSID_VOID;
jsid nsDOMClassInfo::sScrollY_id         = JSID_VOID;
jsid nsDOMClassInfo::sScrollMaxX_id      = JSID_VOID;
jsid nsDOMClassInfo::sScrollMaxY_id      = JSID_VOID;
jsid nsDOMClassInfo::sOpen_id            = JSID_VOID;
jsid nsDOMClassInfo::sItem_id            = JSID_VOID;
jsid nsDOMClassInfo::sNamedItem_id       = JSID_VOID;
jsid nsDOMClassInfo::sEnumerate_id       = JSID_VOID;
jsid nsDOMClassInfo::sNavigator_id       = JSID_VOID;
jsid nsDOMClassInfo::sDocument_id        = JSID_VOID;
jsid nsDOMClassInfo::sWindow_id          = JSID_VOID;
jsid nsDOMClassInfo::sFrames_id          = JSID_VOID;
jsid nsDOMClassInfo::sSelf_id            = JSID_VOID;
jsid nsDOMClassInfo::sOpener_id          = JSID_VOID;
jsid nsDOMClassInfo::sAll_id             = JSID_VOID;
jsid nsDOMClassInfo::sTags_id            = JSID_VOID;
jsid nsDOMClassInfo::sAddEventListener_id= JSID_VOID;
jsid nsDOMClassInfo::sBaseURIObject_id   = JSID_VOID;
jsid nsDOMClassInfo::sNodePrincipal_id   = JSID_VOID;
jsid nsDOMClassInfo::sDocumentURIObject_id=JSID_VOID;
jsid nsDOMClassInfo::sOncopy_id          = JSID_VOID;
jsid nsDOMClassInfo::sOncut_id           = JSID_VOID;
jsid nsDOMClassInfo::sOnpaste_id         = JSID_VOID;
jsid nsDOMClassInfo::sJava_id            = JSID_VOID;
jsid nsDOMClassInfo::sPackages_id        = JSID_VOID;
jsid nsDOMClassInfo::sOnloadstart_id     = JSID_VOID;
jsid nsDOMClassInfo::sOnprogress_id      = JSID_VOID;
jsid nsDOMClassInfo::sOnsuspend_id       = JSID_VOID;
jsid nsDOMClassInfo::sOnemptied_id       = JSID_VOID;
jsid nsDOMClassInfo::sOnstalled_id       = JSID_VOID;
jsid nsDOMClassInfo::sOnplay_id          = JSID_VOID;
jsid nsDOMClassInfo::sOnpause_id         = JSID_VOID;
jsid nsDOMClassInfo::sOnloadedmetadata_id= JSID_VOID;
jsid nsDOMClassInfo::sOnloadeddata_id    = JSID_VOID;
jsid nsDOMClassInfo::sOnwaiting_id       = JSID_VOID;
jsid nsDOMClassInfo::sOnplaying_id       = JSID_VOID;
jsid nsDOMClassInfo::sOncanplay_id       = JSID_VOID;
jsid nsDOMClassInfo::sOncanplaythrough_id= JSID_VOID;
jsid nsDOMClassInfo::sOnseeking_id       = JSID_VOID;
jsid nsDOMClassInfo::sOnseeked_id        = JSID_VOID;
jsid nsDOMClassInfo::sOntimeupdate_id    = JSID_VOID;
jsid nsDOMClassInfo::sOnended_id         = JSID_VOID;
jsid nsDOMClassInfo::sOnratechange_id    = JSID_VOID;
jsid nsDOMClassInfo::sOndurationchange_id= JSID_VOID;
jsid nsDOMClassInfo::sOnvolumechange_id  = JSID_VOID;

static const JSClass *sObjectClass = nsnull;
JSPropertyOp nsDOMClassInfo::sXPCNativeWrapperGetPropertyOp = nsnull;
JSPropertyOp nsDOMClassInfo::sXrayWrapperPropertyHolderGetPropertyOp = nsnull;

/**
 * Set our JSClass pointer for the Object class
 */
static void
FindObjectClass(JSObject* aGlobalObject)
{
  NS_ASSERTION(!sObjectClass,
               "Double set of sObjectClass");
  JSObject *obj, *proto = aGlobalObject;
  do {
    obj = proto;
    proto = obj->getProto();
  } while (proto);

  sObjectClass = obj->getJSClass();
}

static void
PrintWarningOnConsole(JSContext *cx, const char *stringBundleProperty)
{
  nsCOMPtr<nsIStringBundleService> stringService =
    mozilla::services::GetStringBundleService();
  if (!stringService) {
    return;
  }

  nsCOMPtr<nsIStringBundle> bundle;
  stringService->CreateBundle(kDOMStringBundleURL, getter_AddRefs(bundle));
  if (!bundle) {
    return;
  }

  nsXPIDLString msg;
  bundle->GetStringFromName(NS_ConvertASCIItoUTF16(stringBundleProperty).get(),
                            getter_Copies(msg));

  if (msg.IsEmpty()) {
    NS_ERROR("Failed to get strings from dom.properties!");
    return;
  }

  nsCOMPtr<nsIConsoleService> consoleService
    (do_GetService("@mozilla.org/consoleservice;1"));
  if (!consoleService) {
    return;
  }

  nsCOMPtr<nsIScriptError> scriptError =
    do_CreateInstance(NS_SCRIPTERROR_CONTRACTID);
  if (!scriptError) {
    return;
  }

  JSStackFrame *fp, *iterator = nsnull;
  fp = ::JS_FrameIterator(cx, &iterator);
  PRUint32 lineno = 0;
  nsAutoString sourcefile;
  if (fp) {
    JSScript* script = ::JS_GetFrameScript(cx, fp);
    if (script) {
      const char* filename = ::JS_GetScriptFilename(cx, script);
      if (filename) {
        CopyUTF8toUTF16(nsDependentCString(filename), sourcefile);
      }
      jsbytecode* pc = ::JS_GetFramePC(cx, fp);
      if (pc) {
        lineno = ::JS_PCToLineNumber(cx, script, pc);
      }
    }
  }
  nsresult rv = scriptError->Init(msg.get(),
                                  sourcefile.get(),
                                  EmptyString().get(),
                                  lineno,
                                  0, // column for error is not available
                                  nsIScriptError::warningFlag,
                                  "DOM:HTML");
  if (NS_SUCCEEDED(rv)){
    consoleService->LogMessage(scriptError);
  }
}

static inline JSString *
IdToString(JSContext *cx, jsid id)
{
  if (JSID_IS_STRING(id))
    return JSID_TO_STRING(id);
  jsval idval;
  if (!::JS_IdToValue(cx, id, &idval))
    return nsnull;
  return JS_ValueToString(cx, idval);
}

// static

nsISupports *
nsDOMClassInfo::GetNative(nsIXPConnectWrappedNative *wrapper, JSObject *obj)
{
  return wrapper ? wrapper->Native() : static_cast<nsISupports*>(obj->getPrivate());
}

nsresult
nsDOMClassInfo::DefineStaticJSVals(JSContext *cx)
{
#define SET_JSID_TO_STRING(_id, _cx, _str)                                    \
  if (JSString *str = ::JS_InternString(_cx, _str))                           \
      _id = INTERNED_STRING_TO_JSID(str);                                     \
  else                                                                        \
      return NS_ERROR_OUT_OF_MEMORY;

  JSAutoRequest ar(cx);

  SET_JSID_TO_STRING(sTop_id,             cx, "top");
  SET_JSID_TO_STRING(sParent_id,          cx, "parent");
  SET_JSID_TO_STRING(sScrollbars_id,      cx, "scrollbars");
  SET_JSID_TO_STRING(sLocation_id,        cx, "location");
  SET_JSID_TO_STRING(sConstructor_id,     cx, "constructor");
  SET_JSID_TO_STRING(s_content_id,        cx, "_content");
  SET_JSID_TO_STRING(sContent_id,         cx, "content");
  SET_JSID_TO_STRING(sMenubar_id,         cx, "menubar");
  SET_JSID_TO_STRING(sToolbar_id,         cx, "toolbar");
  SET_JSID_TO_STRING(sLocationbar_id,     cx, "locationbar");
  SET_JSID_TO_STRING(sPersonalbar_id,     cx, "personalbar");
  SET_JSID_TO_STRING(sStatusbar_id,       cx, "statusbar");
  SET_JSID_TO_STRING(sDialogArguments_id, cx, "dialogArguments");
  SET_JSID_TO_STRING(sControllers_id,     cx, "controllers");
  SET_JSID_TO_STRING(sLength_id,          cx, "length");
  SET_JSID_TO_STRING(sInnerHeight_id,     cx, "innerHeight");
  SET_JSID_TO_STRING(sInnerWidth_id,      cx, "innerWidth");
  SET_JSID_TO_STRING(sOuterHeight_id,     cx, "outerHeight");
  SET_JSID_TO_STRING(sOuterWidth_id,      cx, "outerWidth");
  SET_JSID_TO_STRING(sScreenX_id,         cx, "screenX");
  SET_JSID_TO_STRING(sScreenY_id,         cx, "screenY");
  SET_JSID_TO_STRING(sStatus_id,          cx, "status");
  SET_JSID_TO_STRING(sName_id,            cx, "name");
  SET_JSID_TO_STRING(sOnmousedown_id,     cx, "onmousedown");
  SET_JSID_TO_STRING(sOnmouseup_id,       cx, "onmouseup");
  SET_JSID_TO_STRING(sOnclick_id,         cx, "onclick");
  SET_JSID_TO_STRING(sOndblclick_id,      cx, "ondblclick");
  SET_JSID_TO_STRING(sOncontextmenu_id,   cx, "oncontextmenu");
  SET_JSID_TO_STRING(sOnmouseover_id,     cx, "onmouseover");
  SET_JSID_TO_STRING(sOnmouseout_id,      cx, "onmouseout");
  SET_JSID_TO_STRING(sOnkeydown_id,       cx, "onkeydown");
  SET_JSID_TO_STRING(sOnkeyup_id,         cx, "onkeyup");
  SET_JSID_TO_STRING(sOnkeypress_id,      cx, "onkeypress");
  SET_JSID_TO_STRING(sOnmousemove_id,     cx, "onmousemove");
  SET_JSID_TO_STRING(sOnfocus_id,         cx, "onfocus");
  SET_JSID_TO_STRING(sOnblur_id,          cx, "onblur");
  SET_JSID_TO_STRING(sOnsubmit_id,        cx, "onsubmit");
  SET_JSID_TO_STRING(sOnreset_id,         cx, "onreset");
  SET_JSID_TO_STRING(sOnchange_id,        cx, "onchange");
  SET_JSID_TO_STRING(sOnselect_id,        cx, "onselect");
  SET_JSID_TO_STRING(sOnload_id,          cx, "onload");
  SET_JSID_TO_STRING(sOnpopstate_id,      cx, "onpopstate");
  SET_JSID_TO_STRING(sOnbeforeunload_id,  cx, "onbeforeunload");
  SET_JSID_TO_STRING(sOnunload_id,        cx, "onunload");
  SET_JSID_TO_STRING(sOnhashchange_id,    cx, "onhashchange");
  SET_JSID_TO_STRING(sOnreadystatechange_id, cx, "onreadystatechange");
  SET_JSID_TO_STRING(sOnpageshow_id,      cx, "onpageshow");
  SET_JSID_TO_STRING(sOnpagehide_id,      cx, "onpagehide");
  SET_JSID_TO_STRING(sOnabort_id,         cx, "onabort");
  SET_JSID_TO_STRING(sOnerror_id,         cx, "onerror");
  SET_JSID_TO_STRING(sOnpaint_id,         cx, "onpaint");
  SET_JSID_TO_STRING(sOnresize_id,        cx, "onresize");
  SET_JSID_TO_STRING(sOnscroll_id,        cx, "onscroll");
  SET_JSID_TO_STRING(sOndrag_id,          cx, "ondrag");
  SET_JSID_TO_STRING(sOndragend_id,       cx, "ondragend");
  SET_JSID_TO_STRING(sOndragenter_id,     cx, "ondragenter");
  SET_JSID_TO_STRING(sOndragleave_id,     cx, "ondragleave");
  SET_JSID_TO_STRING(sOndragover_id,      cx, "ondragover");
  SET_JSID_TO_STRING(sOndragstart_id,     cx, "ondragstart");
  SET_JSID_TO_STRING(sOndrop_id,          cx, "ondrop");
  SET_JSID_TO_STRING(sScrollX_id,         cx, "scrollX");
  SET_JSID_TO_STRING(sScrollY_id,         cx, "scrollY");
  SET_JSID_TO_STRING(sScrollMaxX_id,      cx, "scrollMaxX");
  SET_JSID_TO_STRING(sScrollMaxY_id,      cx, "scrollMaxY");
  SET_JSID_TO_STRING(sOpen_id,            cx, "open");
  SET_JSID_TO_STRING(sItem_id,            cx, "item");
  SET_JSID_TO_STRING(sNamedItem_id,       cx, "namedItem");
  SET_JSID_TO_STRING(sEnumerate_id,       cx, "enumerateProperties");
  SET_JSID_TO_STRING(sNavigator_id,       cx, "navigator");
  SET_JSID_TO_STRING(sDocument_id,        cx, "document");
  SET_JSID_TO_STRING(sWindow_id,          cx, "window");
  SET_JSID_TO_STRING(sFrames_id,          cx, "frames");
  SET_JSID_TO_STRING(sSelf_id,            cx, "self");
  SET_JSID_TO_STRING(sOpener_id,          cx, "opener");
  SET_JSID_TO_STRING(sAll_id,             cx, "all");
  SET_JSID_TO_STRING(sTags_id,            cx, "tags");
  SET_JSID_TO_STRING(sAddEventListener_id,cx, "addEventListener");
  SET_JSID_TO_STRING(sBaseURIObject_id,   cx, "baseURIObject");
  SET_JSID_TO_STRING(sNodePrincipal_id,   cx, "nodePrincipal");
  SET_JSID_TO_STRING(sDocumentURIObject_id,cx,"documentURIObject");
  SET_JSID_TO_STRING(sOncopy_id,          cx, "oncopy");
  SET_JSID_TO_STRING(sOncut_id,           cx, "oncut");
  SET_JSID_TO_STRING(sOnpaste_id,         cx, "onpaste");
  SET_JSID_TO_STRING(sJava_id,            cx, "java");
  SET_JSID_TO_STRING(sPackages_id,        cx, "Packages");
#ifdef MOZ_MEDIA
  SET_JSID_TO_STRING(sOnloadstart_id,     cx, "onloadstart");
  SET_JSID_TO_STRING(sOnprogress_id,      cx, "onprogress");
  SET_JSID_TO_STRING(sOnsuspend_id,       cx, "onsuspend");
  SET_JSID_TO_STRING(sOnemptied_id,       cx, "onemptied");
  SET_JSID_TO_STRING(sOnstalled_id,       cx, "onstalled");
  SET_JSID_TO_STRING(sOnplay_id,          cx, "onplay");
  SET_JSID_TO_STRING(sOnpause_id,         cx, "onpause");
  SET_JSID_TO_STRING(sOnloadedmetadata_id,cx, "onloadedmetadata");
  SET_JSID_TO_STRING(sOnloadeddata_id,    cx, "onloadeddata");
  SET_JSID_TO_STRING(sOnwaiting_id,       cx, "onwaiting");
  SET_JSID_TO_STRING(sOnplaying_id,       cx, "onplaying");
  SET_JSID_TO_STRING(sOncanplay_id,       cx, "oncanplay");
  SET_JSID_TO_STRING(sOncanplaythrough_id,cx, "oncanplaythrough");
  SET_JSID_TO_STRING(sOnseeking_id,       cx, "onseeking");
  SET_JSID_TO_STRING(sOnseeked_id,        cx, "onseeked");
  SET_JSID_TO_STRING(sOntimeupdate_id,    cx, "ontimeupdate");
  SET_JSID_TO_STRING(sOnended_id,         cx, "onended");
  SET_JSID_TO_STRING(sOnratechange_id,    cx, "onratechange");
  SET_JSID_TO_STRING(sOndurationchange_id,cx, "ondurationchange");
  SET_JSID_TO_STRING(sOnvolumechange_id,  cx, "onvolumechange");
#endif // MOZ_MEDIA

  return NS_OK;
}

static nsresult
CreateExceptionFromResult(JSContext *cx, nsresult aResult)
{
  nsCOMPtr<nsIExceptionService> xs =
    do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID);
  if (!xs) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIExceptionManager> xm;
  nsresult rv = xs->GetCurrentExceptionManager(getter_AddRefs(xm));
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIException> exception;
  rv = xm->GetExceptionFromProvider(aResult, 0, getter_AddRefs(exception));
  if (NS_FAILED(rv) || !exception) {
    return NS_ERROR_FAILURE;
  }

  jsval jv;
  nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
  rv = nsDOMClassInfo::WrapNative(cx, ::JS_GetGlobalObject(cx), exception,
                                  &NS_GET_IID(nsIException), PR_FALSE, &jv,
                                  getter_AddRefs(holder));
  if (NS_FAILED(rv) || JSVAL_IS_NULL(jv)) {
    return NS_ERROR_FAILURE;
  }

  JS_SetPendingException(cx, jv);
  return NS_OK;
}

// static
nsresult
nsDOMClassInfo::ThrowJSException(JSContext *cx, nsresult aResult)
{
  JSAutoRequest ar(cx);

  if (NS_SUCCEEDED(CreateExceptionFromResult(cx, aResult))) {
    return NS_OK;
  }

  // XXX This probably wants to be localized, but that can fail in ways that
  // are hard to report correctly.
  JSString *str =
    JS_NewStringCopyZ(cx, "An error occurred throwing an exception");
  if (!str) {
    // JS_NewStringCopyZ reported the error for us.
    return NS_OK; 
  }
  JS_SetPendingException(cx, STRING_TO_JSVAL(str));
  return NS_OK;
}

// static
PRBool
nsDOMClassInfo::ObjectIsNativeWrapper(JSContext* cx, JSObject* obj)
{
#ifdef DEBUG
  {
    nsIScriptContext *scx = GetScriptContextFromJSContext(cx);

    NS_PRECONDITION(!scx || !scx->IsContextInitialized() ||
                    sXPCNativeWrapperGetPropertyOp,
                    "Must know what the XPCNativeWrapper class GetProperty op is!");
  }
#endif

  JSPropertyOp op = obj->getJSClass()->getProperty;
  return !!op && (op == sXPCNativeWrapperGetPropertyOp ||
                  op == sXrayWrapperPropertyHolderGetPropertyOp);
}

nsDOMClassInfo::nsDOMClassInfo(nsDOMClassInfoData* aData) : mData(aData)
{
}

nsDOMClassInfo::~nsDOMClassInfo()
{
  if (IS_EXTERNAL(mData->mCachedClassInfo)) {
    // Some compilers don't like delete'ing a const nsDOMClassInfo*
    nsDOMClassInfoData* data = const_cast<nsDOMClassInfoData*>(mData);
    delete static_cast<nsExternalDOMClassInfoData*>(data);
  }
}

NS_IMPL_ADDREF(nsDOMClassInfo)
NS_IMPL_RELEASE(nsDOMClassInfo)

NS_INTERFACE_MAP_BEGIN(nsDOMClassInfo)
  if (aIID.Equals(NS_GET_IID(nsXPCClassInfo)))
    foundInterface = static_cast<nsIClassInfo*>(
                                    static_cast<nsXPCClassInfo*>(this));
  else
  NS_INTERFACE_MAP_ENTRY(nsIXPCScriptable)
  NS_INTERFACE_MAP_ENTRY(nsIClassInfo)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIClassInfo)
NS_INTERFACE_MAP_END


static JSClass sDOMConstructorProtoClass = {
  "DOM Constructor.prototype", 0,
  JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
  JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, nsnull
};


static const char *
CutPrefix(const char *aName) {
  static const char prefix_nsIDOM[] = "nsIDOM";
  static const char prefix_nsI[]    = "nsI";

  if (strncmp(aName, prefix_nsIDOM, sizeof(prefix_nsIDOM) - 1) == 0) {
    return aName + sizeof(prefix_nsIDOM) - 1;
  }

  if (strncmp(aName, prefix_nsI, sizeof(prefix_nsI) - 1) == 0) {
    return aName + sizeof(prefix_nsI) - 1;
  }

  return aName;
}

// static
nsresult
nsDOMClassInfo::RegisterClassName(PRInt32 aClassInfoID)
{
  nsScriptNameSpaceManager *nameSpaceManager = nsJSRuntime::GetNameSpaceManager();
  NS_ENSURE_TRUE(nameSpaceManager, NS_ERROR_NOT_INITIALIZED);

  nameSpaceManager->RegisterClassName(sClassInfoData[aClassInfoID].mName,
                                      aClassInfoID,
                                      sClassInfoData[aClassInfoID].mChromeOnly,
                                      &sClassInfoData[aClassInfoID].mNameUTF16);

  return NS_OK;
}

// static
nsresult
nsDOMClassInfo::RegisterClassProtos(PRInt32 aClassInfoID)
{
  nsScriptNameSpaceManager *nameSpaceManager = nsJSRuntime::GetNameSpaceManager();
  NS_ENSURE_TRUE(nameSpaceManager, NS_ERROR_NOT_INITIALIZED);
  PRBool found_old;

  const nsIID *primary_iid = sClassInfoData[aClassInfoID].mProtoChainInterface;

  if (!primary_iid || primary_iid == &NS_GET_IID(nsISupports)) {
    return NS_OK;
  }

  nsCOMPtr<nsIInterfaceInfoManager>
    iim(do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID));
  NS_ENSURE_TRUE(iim, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIInterfaceInfo> if_info;
  PRBool first = PR_TRUE;

  iim->GetInfoForIID(primary_iid, getter_AddRefs(if_info));

  while (if_info) {
    const nsIID *iid = nsnull;

    if_info->GetIIDShared(&iid);
    NS_ENSURE_TRUE(iid, NS_ERROR_UNEXPECTED);

    if (iid->Equals(NS_GET_IID(nsISupports))) {
      break;
    }

    const char *name = nsnull;
    if_info->GetNameShared(&name);
    NS_ENSURE_TRUE(name, NS_ERROR_UNEXPECTED);

    nameSpaceManager->RegisterClassProto(CutPrefix(name), iid, &found_old);

    if (first) {
      first = PR_FALSE;
    } else if (found_old) {
      break;
    }

    nsCOMPtr<nsIInterfaceInfo> tmp(if_info);
    tmp->GetParent(getter_AddRefs(if_info));
  }

  return NS_OK;
}

// static
nsresult
nsDOMClassInfo::RegisterExternalClasses()
{
  nsScriptNameSpaceManager *nameSpaceManager = nsJSRuntime::GetNameSpaceManager();
  NS_ENSURE_TRUE(nameSpaceManager, NS_ERROR_NOT_INITIALIZED);

  nsCOMPtr<nsIComponentRegistrar> registrar;
  nsresult rv = NS_GetComponentRegistrar(getter_AddRefs(registrar));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsICategoryManager> cm =
    do_GetService(NS_CATEGORYMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISimpleEnumerator> e;
  rv = cm->EnumerateCategory(JAVASCRIPT_DOM_CLASS, getter_AddRefs(e));
  NS_ENSURE_SUCCESS(rv, rv);

  nsXPIDLCString contractId;
  nsCAutoString categoryEntry;
  nsCOMPtr<nsISupports> entry;

  while (NS_SUCCEEDED(e->GetNext(getter_AddRefs(entry)))) {
    nsCOMPtr<nsISupportsCString> category(do_QueryInterface(entry));

    if (!category) {
      NS_WARNING("Category entry not an nsISupportsCString!");
      continue;
    }

    rv = category->GetData(categoryEntry);

    cm->GetCategoryEntry(JAVASCRIPT_DOM_CLASS, categoryEntry.get(),
                         getter_Copies(contractId));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCID *cid;
    rv = registrar->ContractIDToCID(contractId, &cid);
    if (NS_FAILED(rv)) {
      NS_WARNING("Bad contract id registered with the script namespace manager");
      continue;
    }

    rv = nameSpaceManager->RegisterExternalClassName(categoryEntry.get(), *cid);
    nsMemory::Free(cid);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return nameSpaceManager->RegisterExternalInterfaces(PR_TRUE);
}

// static
inline nsresult
nsDOMClassInfo::WrapNativeParent(JSContext *cx, JSObject *scope,
                                 nsISupports *native,
                                 nsWrapperCache *nativeWrapperCache,
                                 JSObject **parentObj)
{
  // In the common case, |native| is a wrapper cache with an existing wrapper
#ifdef DEBUG
  nsWrapperCache* cache = nsnull;
  CallQueryInterface(native, &cache);
  NS_PRECONDITION(nativeWrapperCache &&
                  cache == nativeWrapperCache, "What happened here?");
#endif
  
  JSObject* obj = nativeWrapperCache->GetWrapper();
  if (obj) {
#ifdef DEBUG
    jsval debugVal;
    nsresult rv = WrapNative(cx, scope, native, nativeWrapperCache, PR_FALSE,
                             &debugVal);
    NS_ASSERTION(NS_SUCCEEDED(rv) && JSVAL_TO_OBJECT(debugVal) == obj,
                 "Unexpected object in nsWrapperCache");
#endif
    *parentObj = obj;
    return NS_OK;
  }

  jsval v;
  nsresult rv = WrapNative(cx, scope, native, nativeWrapperCache, PR_FALSE, &v);
  NS_ENSURE_SUCCESS(rv, rv);
  *parentObj = JSVAL_TO_OBJECT(v);
  return NS_OK;
}

#define _DOM_CLASSINFO_MAP_BEGIN(_class, _ifptr, _has_class_if)               \
  {                                                                           \
    nsDOMClassInfoData &d = sClassInfoData[eDOMClassInfo_##_class##_id];      \
    d.mProtoChainInterface = _ifptr;                                          \
    d.mHasClassInterface = _has_class_if;                                     \
    d.mInterfacesBitmap = kDOMClassInfo_##_class##_interfaces;                \
    static const nsIID *interface_list[] = {

#define DOM_CLASSINFO_MAP_BEGIN(_class, _interface)                           \
  _DOM_CLASSINFO_MAP_BEGIN(_class, &NS_GET_IID(_interface), PR_TRUE)

#define DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(_class, _interface)               \
  _DOM_CLASSINFO_MAP_BEGIN(_class, &NS_GET_IID(_interface), PR_FALSE)

#define DOM_CLASSINFO_MAP_ENTRY(_if)                                          \
      &NS_GET_IID(_if),

#define DOM_CLASSINFO_MAP_END                                                 \
      nsnull                                                                  \
    };                                                                        \
                                                                              \
    d.mInterfaces = interface_list;                                           \
  }

#define DOM_CLASSINFO_DOCUMENT_MAP_ENTRIES                                    \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSDocument)                                 \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocumentEvent)                              \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocumentStyle)                              \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSDocumentStyle)                            \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocumentView)                               \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocumentRange)                              \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocumentTraversal)                          \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocumentXBL)                                \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)                              \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)                                \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Document)                                  \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)                                      \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXPathEvaluator)                             \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNodeSelector)


#define DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES                                \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSHTMLElement)                              \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMElementCSSInlineStyle)                      \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)                              \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)                                \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)                                      \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSElement)                                  \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNodeSelector)

#define DOM_CLASSINFO_EVENT_MAP_ENTRIES                                       \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEvent)                                      \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEvent)                                    \

#define DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES                                    \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMUIEvent)                                    \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSUIEvent)                                  \
    DOM_CLASSINFO_EVENT_MAP_ENTRIES

nsresult
nsDOMClassInfo::Init()
{
  /* Errors that can trigger early returns are done first,
     otherwise nsDOMClassInfo is left in a half inited state. */
  NS_ASSERTION(sizeof(PtrBits) == sizeof(void*),
               "BAD! You'll need to adjust the size of PtrBits to the size "
               "of a pointer on your platform.");

  NS_ENSURE_TRUE(!sIsInitialized, NS_ERROR_ALREADY_INITIALIZED);

  nsScriptNameSpaceManager *nameSpaceManager = nsJSRuntime::GetNameSpaceManager();
  NS_ENSURE_TRUE(nameSpaceManager, NS_ERROR_NOT_INITIALIZED);

  nsresult rv = CallGetService(nsIXPConnect::GetCID(), &sXPConnect);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIXPCFunctionThisTranslator> old;

  nsCOMPtr<nsIXPCFunctionThisTranslator> elt = new nsEventListenerThisTranslator();
  NS_ENSURE_TRUE(elt, NS_ERROR_OUT_OF_MEMORY);

  sXPConnect->SetFunctionThisTranslator(NS_GET_IID(nsIDOMEventListener),
                                        elt, getter_AddRefs(old));

  nsCOMPtr<nsIScriptSecurityManager> sm =
    do_GetService("@mozilla.org/scriptsecuritymanager;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  sSecMan = sm;
  NS_ADDREF(sSecMan);

  nsCOMPtr<nsIThreadJSContextStack> stack =
    do_GetService("@mozilla.org/js/xpc/ContextStack;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  JSContext *cx = nsnull;

  rv = stack->GetSafeJSContext(&cx);
  NS_ENSURE_SUCCESS(rv, rv);

  DOM_CLASSINFO_MAP_BEGIN(Window, nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMJSWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindowInternal)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMViewCSS)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMAbstractView)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageWindow)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(InnerWindow, nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMJSWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindowInternal)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMViewCSS)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMAbstractView)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageWindow)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(WindowUtils, nsIDOMWindowUtils)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindowUtils)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Location, nsIDOMLocation)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMLocation)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Navigator, nsIDOMNavigator)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNavigator)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNavigatorGeolocation)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMClientInformation)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Plugin, nsIDOMPlugin)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMPlugin)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(PluginArray, nsIDOMPluginArray)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMPluginArray)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(MimeType, nsIDOMMimeType)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMimeType)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(MimeTypeArray, nsIDOMMimeTypeArray)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMimeTypeArray)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(BarProp, nsIDOMBarProp)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMBarProp)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(History, nsIDOMHistory)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHistory)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Screen, nsIDOMScreen)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMScreen)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(DOMPrototype, nsIDOMDOMConstructor)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDOMConstructor)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(DOMConstructor, nsIDOMDOMConstructor)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDOMConstructor)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XMLDocument, nsIDOMXMLDocument)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocument)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXMLDocument)
    DOM_CLASSINFO_DOCUMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(DocumentType, nsIDOMDocumentType)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocumentType)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(DOMImplementation, nsIDOMDOMImplementation)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDOMImplementation)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(DOMException, nsIDOMDOMException)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDOMException)
    DOM_CLASSINFO_MAP_ENTRY(nsIException)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(DOMTokenList, nsIDOMDOMTokenList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDOMTokenList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(DOMSettableTokenList, nsIDOMDOMSettableTokenList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDOMSettableTokenList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(DocumentFragment, nsIDOMDocumentFragment)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocumentFragment)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNodeSelector)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Element, nsIDOMElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNodeSelector)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Attr, nsIDOMAttr)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMAttr)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Attr)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Text, nsIDOMText)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMText)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Text)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Comment, nsIDOMComment)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMComment)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CDATASection, nsIDOMCDATASection)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCDATASection)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Text)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(ProcessingInstruction, nsIDOMProcessingInstruction)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMProcessingInstruction)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Notation, nsIDOMNotation)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNotation)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(NodeList, nsIDOMNodeList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNodeList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(NamedNodeMap, nsIDOMNamedNodeMap)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNamedNodeMap)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Event, nsIDOMEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(PopupBlockedEvent, nsIDOMPopupBlockedEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMPopupBlockedEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(OrientationEvent, nsIDOMOrientationEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMOrientationEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SmartCardEvent, nsIDOMSmartCardEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSmartCardEvent)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(PageTransitionEvent, nsIDOMPageTransitionEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMPageTransitionEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(MutationEvent, nsIDOMMutationEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMutationEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(UIEvent, nsIDOMUIEvent)
    DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END
  
  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(KeyboardEvent, nsIDOMKeyEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMKeyEvent)
    DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(MouseEvent, nsIDOMMouseEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMouseEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSMouseEvent)
    DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(MouseScrollEvent, nsIDOMMouseScrollEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMouseScrollEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMouseEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSMouseEvent)
    DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(DragEvent, nsIDOMDragEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDragEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMouseEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSMouseEvent)
    DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(PopStateEvent, nsIDOMPopStateEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMPopStateEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLDocument, nsIDOMHTMLDocument)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLDocument)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSHTMLDocument)
    DOM_CLASSINFO_DOCUMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLOptionsCollection, nsIDOMHTMLOptionsCollection)
    // Order is significant.  nsIDOMHTMLOptionsCollection.length shadows
    // nsIDOMHTMLCollection.length, which is readonly.
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLOptionsCollection)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSHTMLOptionCollection)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLCollection)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLCollection, nsIDOMHTMLCollection)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLCollection)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLElement, nsIDOMHTMLElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSHTMLElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLAnchorElement, nsIDOMHTMLAnchorElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLAnchorElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLAppletElement, nsIDOMHTMLAppletElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLAppletElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLAreaElement, nsIDOMHTMLAreaElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLAreaElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLBRElement, nsIDOMHTMLBRElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLBRElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLBaseElement, nsIDOMHTMLBaseElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLBaseElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLBodyElement, nsIDOMHTMLBodyElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLBodyElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLButtonElement, nsIDOMHTMLButtonElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLButtonElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLDListElement, nsIDOMHTMLDListElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLDListElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(HTMLDelElement, nsIDOMHTMLElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLModElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLDirectoryElement, nsIDOMHTMLDirectoryElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLDirectoryElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLDivElement, nsIDOMHTMLDivElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLDivElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLEmbedElement, nsIDOMHTMLEmbedElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLEmbedElement)
#ifdef MOZ_SVG
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMGetSVGDocument)
#endif
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLFieldSetElement, nsIDOMHTMLFieldSetElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLFieldSetElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLFontElement, nsIDOMHTMLFontElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLFontElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLFormElement, nsIDOMHTMLFormElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLFormElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSHTMLFormElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLFrameElement, nsIDOMHTMLFrameElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLFrameElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSHTMLFrameElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLFrameSetElement, nsIDOMHTMLFrameSetElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLFrameSetElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLHRElement, nsIDOMHTMLHRElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLHRElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSHTMLHRElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLHeadElement, nsIDOMHTMLHeadElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLHeadElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLHeadingElement, nsIDOMHTMLHeadingElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLHeadingElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLHtmlElement, nsIDOMHTMLHtmlElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLHtmlElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLIFrameElement, nsIDOMHTMLIFrameElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLIFrameElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSHTMLFrameElement)
#ifdef MOZ_SVG
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMGetSVGDocument)
#endif
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLImageElement, nsIDOMHTMLImageElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLImageElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLInputElement, nsIDOMHTMLInputElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLInputElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(HTMLInsElement, nsIDOMHTMLElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLModElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLIsIndexElement, nsIDOMHTMLIsIndexElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLIsIndexElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLLIElement, nsIDOMHTMLLIElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLLIElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLLabelElement, nsIDOMHTMLLabelElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLLabelElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLLegendElement, nsIDOMHTMLLegendElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLLegendElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLLinkElement, nsIDOMHTMLLinkElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLLinkElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMLinkStyle)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLMapElement, nsIDOMHTMLMapElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLMapElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLMenuElement, nsIDOMHTMLMenuElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLMenuElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLMetaElement, nsIDOMHTMLMetaElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLMetaElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLOListElement, nsIDOMHTMLOListElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLOListElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLObjectElement, nsIDOMHTMLObjectElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLObjectElement)
#ifdef MOZ_SVG
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMGetSVGDocument)
#endif
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLOptGroupElement, nsIDOMHTMLOptGroupElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLOptGroupElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLOptionElement, nsIDOMHTMLOptionElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLOptionElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLOutputElement, nsIDOMHTMLOutputElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLOutputElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLParagraphElement, nsIDOMHTMLParagraphElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLParagraphElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLParamElement, nsIDOMHTMLParamElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLParamElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLPreElement, nsIDOMHTMLPreElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLPreElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLQuoteElement, nsIDOMHTMLQuoteElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLQuoteElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLScriptElement, nsIDOMHTMLScriptElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLScriptElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLSelectElement, nsIDOMHTMLSelectElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLSelectElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(HTMLSpanElement, nsIDOMHTMLElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLStyleElement, nsIDOMHTMLStyleElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLStyleElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMLinkStyle)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLTableCaptionElement,
                          nsIDOMHTMLTableCaptionElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLTableCaptionElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLTableCellElement, nsIDOMHTMLTableCellElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLTableCellElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLTableColElement, nsIDOMHTMLTableColElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLTableColElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLTableElement, nsIDOMHTMLTableElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLTableElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLTableRowElement, nsIDOMHTMLTableRowElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLTableRowElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLTableSectionElement,
                          nsIDOMHTMLTableSectionElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLTableSectionElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLTextAreaElement, nsIDOMHTMLTextAreaElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLTextAreaElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSHTMLTextAreaElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLTitleElement, nsIDOMHTMLTitleElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLTitleElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLUListElement, nsIDOMHTMLUListElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLUListElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(HTMLUnknownElement, nsIDOMHTMLElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(ValidityState, nsIDOMValidityState)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMValidityState)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CSSStyleRule, nsIDOMCSSStyleRule)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSStyleRule)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CSSCharsetRule, nsIDOMCSSCharsetRule)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSCharsetRule)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CSSImportRule, nsIDOMCSSImportRule)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSImportRule)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CSSMediaRule, nsIDOMCSSMediaRule)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSMediaRule)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(CSSNameSpaceRule, nsIDOMCSSRule)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSRule)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CSSRuleList, nsIDOMCSSRuleList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSRuleList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(CSSGroupRuleRuleList, nsIDOMCSSRuleList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSRuleList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(MediaList, nsIDOMMediaList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMediaList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(StyleSheetList, nsIDOMStyleSheetList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStyleSheetList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CSSStyleSheet, nsIDOMCSSStyleSheet)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSStyleSheet)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CSSStyleDeclaration, nsIDOMCSSStyleDeclaration)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSStyleDeclaration)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSS2Properties)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(ComputedCSSStyleDeclaration,
                                      nsIDOMCSSStyleDeclaration)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSStyleDeclaration)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSS2Properties)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(ROCSSPrimitiveValue,
                                      nsIDOMCSSPrimitiveValue)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSPrimitiveValue)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CSSValueList, nsIDOMCSSValueList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSValueList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(CSSRect, nsIDOMRect)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMRect)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(CSSRGBColor, nsIDOMRGBColor)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMRGBColor)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSRGBAColor)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Range, nsIDOMRange)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMRange)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSRange)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(NodeIterator, nsIDOMNodeIterator)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNodeIterator)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(TreeWalker, nsIDOMTreeWalker)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMTreeWalker)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Selection, nsISelection)
    DOM_CLASSINFO_MAP_ENTRY(nsISelection)
    DOM_CLASSINFO_MAP_ENTRY(nsISelection3)
  DOM_CLASSINFO_MAP_END

#ifdef MOZ_XUL
  DOM_CLASSINFO_MAP_BEGIN(XULDocument, nsIDOMXULDocument)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocument)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXULDocument)
    DOM_CLASSINFO_DOCUMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XULElement, nsIDOMXULElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXULElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMElementCSSInlineStyle)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNodeSelector)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XULCommandDispatcher, nsIDOMXULCommandDispatcher)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXULCommandDispatcher)
  DOM_CLASSINFO_MAP_END
#endif

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(XULControllers, nsIControllers)
    DOM_CLASSINFO_MAP_ENTRY(nsIControllers)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(BoxObject, nsIBoxObject)
    DOM_CLASSINFO_MAP_ENTRY(nsIBoxObject)
  DOM_CLASSINFO_MAP_END

#ifdef MOZ_XUL
  DOM_CLASSINFO_MAP_BEGIN(TreeSelection, nsITreeSelection)
    DOM_CLASSINFO_MAP_ENTRY(nsITreeSelection)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(TreeContentView, nsITreeContentView)
    DOM_CLASSINFO_MAP_ENTRY(nsITreeContentView)
    DOM_CLASSINFO_MAP_ENTRY(nsITreeView)
  DOM_CLASSINFO_MAP_END
#endif

  DOM_CLASSINFO_MAP_BEGIN(Crypto, nsIDOMCrypto)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCrypto)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CRMFObject, nsIDOMCRMFObject)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCRMFObject)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(XMLStylesheetProcessingInstruction, nsIDOMProcessingInstruction)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMProcessingInstruction)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMLinkStyle)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(ChromeWindow, nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMJSWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindowInternal)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMChromeWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMViewCSS)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMAbstractView)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(InnerChromeWindow, nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMJSWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindowInternal)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMChromeWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMViewCSS)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMAbstractView)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(RangeException, nsIDOMRangeException)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMRangeException)
    DOM_CLASSINFO_MAP_ENTRY(nsIException)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(ContentList, nsIDOMHTMLCollection)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNodeList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLCollection)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(ImageDocument, nsIImageDocument)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLDocument)
    DOM_CLASSINFO_MAP_ENTRY(nsIImageDocument)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSHTMLDocument)
    DOM_CLASSINFO_DOCUMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

#ifdef MOZ_XUL
  DOM_CLASSINFO_MAP_BEGIN(XULTemplateBuilder, nsIXULTemplateBuilder)
    DOM_CLASSINFO_MAP_ENTRY(nsIXULTemplateBuilder)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XULTreeBuilder, nsIXULTreeBuilder)
    DOM_CLASSINFO_MAP_ENTRY(nsIXULTreeBuilder)
    DOM_CLASSINFO_MAP_ENTRY(nsITreeView)
  DOM_CLASSINFO_MAP_END
#endif

  DOM_CLASSINFO_MAP_BEGIN(DOMStringList, nsIDOMDOMStringList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDOMStringList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(NameList, nsIDOMNameList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNameList)
  DOM_CLASSINFO_MAP_END

#ifdef MOZ_XUL
  DOM_CLASSINFO_MAP_BEGIN(TreeColumn, nsITreeColumn)
    DOM_CLASSINFO_MAP_ENTRY(nsITreeColumn)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(TreeColumns, nsITreeColumns)
    DOM_CLASSINFO_MAP_ENTRY(nsITreeColumns)
  DOM_CLASSINFO_MAP_END
#endif

  DOM_CLASSINFO_MAP_BEGIN(CSSMozDocumentRule, nsIDOMCSSMozDocumentRule)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSMozDocumentRule)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(BeforeUnloadEvent, nsIDOMBeforeUnloadEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMBeforeUnloadEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

#ifdef MOZ_SVG
#define DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES    \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget) \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)   \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGElement)    \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSElement)     \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)         \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNodeSelector)

#define DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGLocatable)       \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTransformable)   \
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)        \
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES

  // XXX - the proto chain stuff is sort of hackish, because of the MI in
  // the SVG interfaces. I doubt that extending the proto on one interface
  // works properly on an element which inherits off multiple interfaces.
  // Tough luck. - bbaetz

  // The SVG document

  DOM_CLASSINFO_MAP_BEGIN(SVGDocument, nsIDOMSVGDocument)
    // Order is significant.  nsIDOMDocument.title shadows
    // nsIDOMSVGDocument.title, which is readonly.
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDocument)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGDocument)
    DOM_CLASSINFO_DOCUMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  // SVG element classes

  DOM_CLASSINFO_MAP_BEGIN(SVGAElement, nsIDOMSVGAElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAElement)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAltGlyphElement, nsIDOMSVGAltGlyphElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTextPositioningElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTextContentElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

#ifdef MOZ_SMIL
  DOM_CLASSINFO_MAP_BEGIN(SVGAnimateElement, nsIDOMSVGAnimateElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimationElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimateElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMElementTimeControl)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimateTransformElement,
                          nsIDOMSVGAnimateTransformElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimationElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimateTransformElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMElementTimeControl)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimateMotionElement,
                          nsIDOMSVGAnimateMotionElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimationElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimateMotionElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMElementTimeControl)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGSetElement,
                          nsIDOMSVGSetElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimationElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGSetElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMElementTimeControl)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGMpathElement, nsIDOMSVGMpathElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(TimeEvent, nsIDOMTimeEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMTimeEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END
#endif // MOZ_SMIL

  DOM_CLASSINFO_MAP_BEGIN(SVGCircleElement, nsIDOMSVGCircleElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGCircleElement)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGClipPathElement, nsIDOMSVGClipPathElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGClipPathElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGLocatable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTransformable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGUnitTypes)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGDefsElement, nsIDOMSVGDefsElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGDefsElement)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGDescElement, nsIDOMSVGDescElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGDescElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGEllipseElement, nsIDOMSVGEllipseElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGEllipseElement)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEBlendElement, nsIDOMSVGFEBlendElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEBlendElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEColorMatrixElement, nsIDOMSVGFEColorMatrixElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEColorMatrixElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEComponentTransferElement, nsIDOMSVGFEComponentTransferElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEComponentTransferElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFECompositeElement, nsIDOMSVGFECompositeElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFECompositeElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEConvolveMatrixElement, nsIDOMSVGFEConvolveMatrixElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEConvolveMatrixElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEDiffuseLightingElement, nsIDOMSVGFEDiffuseLightingElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEDiffuseLightingElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEDisplacementMapElement, nsIDOMSVGFEDisplacementMapElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEDisplacementMapElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEDistantLightElement, nsIDOMSVGFEDistantLightElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEDistantLightElement)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEFloodElement, nsIDOMSVGFEFloodElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEFloodElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEFuncAElement, nsIDOMSVGFEFuncAElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEFuncAElement)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEFuncBElement, nsIDOMSVGFEFuncBElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEFuncBElement)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEFuncGElement, nsIDOMSVGFEFuncGElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEFuncGElement)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEFuncRElement, nsIDOMSVGFEFuncRElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEFuncRElement)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEGaussianBlurElement, nsIDOMSVGFEGaussianBlurElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEGaussianBlurElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEImageElement, nsIDOMSVGFEImageElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEImageElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEMergeElement, nsIDOMSVGFEMergeElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEMergeElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEMorphologyElement, nsIDOMSVGFEMorphologyElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEMorphologyElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEMergeNodeElement, nsIDOMSVGFEMergeNodeElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEMergeNodeElement)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEOffsetElement, nsIDOMSVGFEOffsetElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEOffsetElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFEPointLightElement, nsIDOMSVGFEPointLightElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFEPointLightElement)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFESpecularLightingElement, nsIDOMSVGFESpecularLightingElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFESpecularLightingElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFESpotLightElement, nsIDOMSVGFESpotLightElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFESpotLightElement)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFETileElement, nsIDOMSVGFETileElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFETileElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFETurbulenceElement, nsIDOMSVGFETurbulenceElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFETurbulenceElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterPrimitiveStandardAttributes)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGFilterElement, nsIDOMSVGFilterElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFilterElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGUnitTypes)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGGElement, nsIDOMSVGGElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGGElement)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGImageElement, nsIDOMSVGImageElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGImageElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGLinearGradientElement, nsIDOMSVGLinearGradientElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGGradientElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGLinearGradientElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGUnitTypes)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGLineElement, nsIDOMSVGLineElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGLineElement)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGMarkerElement, nsIDOMSVGMarkerElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGMarkerElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFitToViewBox)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGMaskElement, nsIDOMSVGMaskElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGMaskElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGUnitTypes)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGMetadataElement, nsIDOMSVGMetadataElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGMetadataElement)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathElement, nsIDOMSVGPathElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedPathData)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPatternElement, nsIDOMSVGPatternElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPatternElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFitToViewBox)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGUnitTypes)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPolygonElement, nsIDOMSVGPolygonElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPolygonElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedPoints)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPolylineElement, nsIDOMSVGPolylineElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPolylineElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedPoints)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGRadialGradientElement, nsIDOMSVGRadialGradientElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGGradientElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGRadialGradientElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGUnitTypes)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGRectElement, nsIDOMSVGRectElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGRectElement)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGScriptElement, nsIDOMSVGScriptElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGScriptElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGStopElement, nsIDOMSVGStopElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStopElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END
  
  DOM_CLASSINFO_MAP_BEGIN(SVGStyleElement, nsIDOMSVGStyleElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStyleElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMLinkStyle)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGSVGElement, nsIDOMSVGSVGElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGSVGElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFitToViewBox)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGLocatable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGZoomAndPan)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGSwitchElement, nsIDOMSVGSwitchElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGSwitchElement)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGSymbolElement, nsIDOMSVGSymbolElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGSymbolElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGFitToViewBox)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGTextElement, nsIDOMSVGTextElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTextPositioningElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTextContentElement)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGTextPathElement, nsIDOMSVGTextPathElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTextContentElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGTitleElement, nsIDOMSVGTitleElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTitleElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGTSpanElement, nsIDOMSVGTSpanElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTextPositioningElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTextContentElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGStylable)
    DOM_CLASSINFO_SVG_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGUseElement, nsIDOMSVGUseElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGUseElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGURIReference)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  // other SVG classes

  DOM_CLASSINFO_MAP_BEGIN(SVGAngle, nsIDOMSVGAngle)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAngle)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedAngle, nsIDOMSVGAnimatedAngle)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedAngle)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedBoolean, nsIDOMSVGAnimatedBoolean)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedBoolean)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedEnumeration, nsIDOMSVGAnimatedEnumeration)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedEnumeration)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedInteger, nsIDOMSVGAnimatedInteger)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedInteger)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedLength, nsIDOMSVGAnimatedLength)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedLength)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedLengthList, nsIDOMSVGAnimatedLengthList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedLengthList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedNumber, nsIDOMSVGAnimatedNumber)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedNumber)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedNumberList, nsIDOMSVGAnimatedNumberList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedNumberList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedPreserveAspectRatio, nsIDOMSVGAnimatedPreserveAspectRatio)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedPreserveAspectRatio)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedRect, nsIDOMSVGAnimatedRect)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedRect)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedString, nsIDOMSVGAnimatedString)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedString)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGAnimatedTransformList, nsIDOMSVGAnimatedTransformList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGAnimatedTransformList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGEvent, nsIDOMSVGEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGException, nsIDOMSVGException)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGException)
    DOM_CLASSINFO_MAP_ENTRY(nsIException)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGLength, nsIDOMSVGLength)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGLength)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGLengthList, nsIDOMSVGLengthList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGLengthList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGMatrix, nsIDOMSVGMatrix)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGMatrix)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGNumber, nsIDOMSVGNumber)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGNumber)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGNumberList, nsIDOMSVGNumberList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGNumberList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegArcAbs, nsIDOMSVGPathSegArcAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegArcAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegArcRel, nsIDOMSVGPathSegArcRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegArcRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegClosePath, nsIDOMSVGPathSegClosePath)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegClosePath)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegCurvetoCubicAbs, nsIDOMSVGPathSegCurvetoCubicAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegCurvetoCubicAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegCurvetoCubicRel, nsIDOMSVGPathSegCurvetoCubicRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegCurvetoCubicRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegCurvetoCubicSmoothAbs, nsIDOMSVGPathSegCurvetoCubicSmoothAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegCurvetoCubicSmoothAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegCurvetoCubicSmoothRel, nsIDOMSVGPathSegCurvetoCubicSmoothRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegCurvetoCubicSmoothRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegCurvetoQuadraticAbs, nsIDOMSVGPathSegCurvetoQuadraticAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegCurvetoQuadraticAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegCurvetoQuadraticRel, nsIDOMSVGPathSegCurvetoQuadraticRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegCurvetoQuadraticRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegCurvetoQuadraticSmoothAbs, nsIDOMSVGPathSegCurvetoQuadraticSmoothAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegCurvetoQuadraticSmoothAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegCurvetoQuadraticSmoothRel, nsIDOMSVGPathSegCurvetoQuadraticSmoothRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegCurvetoQuadraticSmoothRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegLinetoAbs, nsIDOMSVGPathSegLinetoAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegLinetoAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegLinetoHorizontalAbs, nsIDOMSVGPathSegLinetoHorizontalAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegLinetoHorizontalAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegLinetoHorizontalRel, nsIDOMSVGPathSegLinetoHorizontalRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegLinetoHorizontalRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegLinetoRel, nsIDOMSVGPathSegLinetoRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegLinetoRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegLinetoVerticalAbs, nsIDOMSVGPathSegLinetoVerticalAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegLinetoVerticalAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegLinetoVerticalRel, nsIDOMSVGPathSegLinetoVerticalRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegLinetoVerticalRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegList, nsIDOMSVGPathSegList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegMovetoAbs, nsIDOMSVGPathSegMovetoAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegMovetoAbs)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPathSegMovetoRel, nsIDOMSVGPathSegMovetoRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSegMovetoRel)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPathSeg)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPoint, nsIDOMSVGPoint)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPoint)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPointList, nsIDOMSVGPointList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPointList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGPreserveAspectRatio, nsIDOMSVGPreserveAspectRatio)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGPreserveAspectRatio)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGRect, nsIDOMSVGRect)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGRect)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGTransform, nsIDOMSVGTransform)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTransform)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGTransformList, nsIDOMSVGTransformList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGTransformList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SVGZoomEvent, nsIDOMSVGZoomEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGZoomEvent)
    DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END
#endif // MOZ_SVG

  DOM_CLASSINFO_MAP_BEGIN(HTMLCanvasElement, nsIDOMHTMLCanvasElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLCanvasElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CanvasRenderingContext2D, nsIDOMCanvasRenderingContext2D)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCanvasRenderingContext2D)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CanvasGradient, nsIDOMCanvasGradient)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCanvasGradient)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CanvasPattern, nsIDOMCanvasPattern)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCanvasPattern)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(TextMetrics, nsIDOMTextMetrics)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMTextMetrics)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XSLTProcessor, nsIXSLTProcessor)
    DOM_CLASSINFO_MAP_ENTRY(nsIXSLTProcessor)
    DOM_CLASSINFO_MAP_ENTRY(nsIXSLTProcessorObsolete) // XXX DEPRECATED
    DOM_CLASSINFO_MAP_ENTRY(nsIXSLTProcessorPrivate)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XPathEvaluator, nsIDOMXPathEvaluator)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXPathEvaluator)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XPathException, nsIDOMXPathException)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXPathException)
    DOM_CLASSINFO_MAP_ENTRY(nsIException)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XPathExpression, nsIDOMXPathExpression)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXPathExpression)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSXPathExpression)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XPathNSResolver, nsIDOMXPathNSResolver)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXPathNSResolver)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XPathResult, nsIDOMXPathResult)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXPathResult)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(StorageObsolete, nsIDOMStorageObsolete)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageObsolete)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(Storage, nsIDOMStorage)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorage)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(StorageList, nsIDOMStorageList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(StorageItem, nsIDOMStorageItem)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageItem)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMToString)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(StorageEvent, nsIDOMStorageEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageEvent)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(StorageEventObsolete, nsIDOMStorageEventObsolete)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageEventObsolete)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(DOMParser, nsIDOMParser)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMParser)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMParserJS)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(XMLSerializer, nsIDOMSerializer)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSerializer)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XMLHttpRequest, nsIXMLHttpRequest)
    DOM_CLASSINFO_MAP_ENTRY(nsIXMLHttpRequest)
    DOM_CLASSINFO_MAP_ENTRY(nsIJSXMLHttpRequest)
    DOM_CLASSINFO_MAP_ENTRY(nsIXMLHttpRequestEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIInterfaceRequestor)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(XMLHttpProgressEvent, nsIDOMEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMLSProgressEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMProgressEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

#ifdef MOZ_SVG
  DOM_CLASSINFO_MAP_BEGIN(SVGForeignObjectElement, nsIDOMSVGForeignObjectElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSVGForeignObjectElement)
    DOM_CLASSINFO_SVG_GRAPHIC_ELEMENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END
#endif

  DOM_CLASSINFO_MAP_BEGIN(XULCommandEvent, nsIDOMXULCommandEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMXULCommandEvent)
    DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CommandEvent, nsIDOMCommandEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCommandEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(OfflineResourceList, nsIDOMOfflineResourceList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMOfflineResourceList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(ClientRect, nsIDOMClientRect)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMClientRect)
   DOM_CLASSINFO_MAP_END
 
  DOM_CLASSINFO_MAP_BEGIN(ClientRectList, nsIDOMClientRectList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMClientRectList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(FileList, nsIDOMFileList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMFileList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(FileError, nsIDOMFileError)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMFileError)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(File, nsIDOMFile)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMFile)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(FileException, nsIDOMFileException)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMFileException)
    DOM_CLASSINFO_MAP_ENTRY(nsIException)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(FileReader, nsIDOMFileReader)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMFileReader)
    DOM_CLASSINFO_MAP_ENTRY(nsIXMLHttpRequestEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIInterfaceRequestor)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(ModalContentWindow, nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMJSWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindowInternal)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMViewCSS)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMAbstractView)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMModalContentWindow)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(InnerModalContentWindow, nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMJSWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMWindowInternal)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMViewCSS)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMAbstractView)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMStorageWindow)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMModalContentWindow)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(DataContainerEvent, nsIDOMDataContainerEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDataContainerEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(MessageEvent, nsIDOMMessageEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMessageEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(GeoGeolocation, nsIDOMGeoGeolocation)
     DOM_CLASSINFO_MAP_ENTRY(nsIDOMGeoGeolocation)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(GeoPosition, nsIDOMGeoPosition)
     DOM_CLASSINFO_MAP_ENTRY(nsIDOMGeoPosition)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(GeoPositionCoords, nsIDOMGeoPositionCoords)
     DOM_CLASSINFO_MAP_ENTRY(nsIDOMGeoPositionCoords)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(GeoPositionError, nsIDOMGeoPositionError)
     DOM_CLASSINFO_MAP_ENTRY(nsIDOMGeoPositionError)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CSSFontFaceRule, nsIDOMCSSFontFaceRule)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSFontFaceRule)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(CSSFontFaceStyleDecl,
                                      nsIDOMCSSStyleDeclaration)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCSSStyleDeclaration)
  DOM_CLASSINFO_MAP_END

#if defined (MOZ_MEDIA)
  DOM_CLASSINFO_MAP_BEGIN(HTMLVideoElement, nsIDOMHTMLVideoElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLVideoElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLSourceElement, nsIDOMHTMLSourceElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLSourceElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(MediaError, nsIDOMMediaError)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMediaError)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(HTMLAudioElement, nsIDOMHTMLAudioElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMHTMLAudioElement)
    DOM_CLASSINFO_GENERIC_HTML_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(TimeRanges, nsIDOMTimeRanges)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMTimeRanges)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END  
#endif

  DOM_CLASSINFO_MAP_BEGIN(ProgressEvent, nsIDOMProgressEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMProgressEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(XMLHttpRequestUpload, nsIXMLHttpRequestUpload)
    DOM_CLASSINFO_MAP_ENTRY(nsIXMLHttpRequestEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIXMLHttpRequestUpload)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(DataTransfer, nsIDOMDataTransfer)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMDataTransfer)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSDataTransfer)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(NotifyPaintEvent, nsIDOMNotifyPaintEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNotifyPaintEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(NotifyAudioAvailableEvent, nsIDOMNotifyAudioAvailableEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNotifyAudioAvailableEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(SimpleGestureEvent, nsIDOMSimpleGestureEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMSimpleGestureEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMouseEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSMouseEvent)
    DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(MozTouchEvent, nsIDOMMozTouchEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMozTouchEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMMouseEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSMouseEvent)
    DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

#ifdef MOZ_MATHML
  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(MathMLElement, nsIDOMElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSElement)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOM3Node)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNodeSelector)
  DOM_CLASSINFO_MAP_END
#endif

  DOM_CLASSINFO_MAP_BEGIN(Worker, nsIWorker)
    DOM_CLASSINFO_MAP_ENTRY(nsIWorker)
    DOM_CLASSINFO_MAP_ENTRY(nsIAbstractWorker)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(ChromeWorker, nsIChromeWorker)
    DOM_CLASSINFO_MAP_ENTRY(nsIChromeWorker)
    DOM_CLASSINFO_MAP_ENTRY(nsIWorker)
    DOM_CLASSINFO_MAP_ENTRY(nsIAbstractWorker)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CanvasRenderingContextWebGL, nsICanvasRenderingContextWebGL)
    DOM_CLASSINFO_MAP_ENTRY(nsICanvasRenderingContextWebGL)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(WebGLBuffer, nsIWebGLBuffer)
    DOM_CLASSINFO_MAP_ENTRY(nsIWebGLBuffer)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(WebGLTexture, nsIWebGLTexture)
    DOM_CLASSINFO_MAP_ENTRY(nsIWebGLTexture)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(WebGLProgram, nsIWebGLProgram)
    DOM_CLASSINFO_MAP_ENTRY(nsIWebGLProgram)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(WebGLShader, nsIWebGLShader)
    DOM_CLASSINFO_MAP_ENTRY(nsIWebGLShader)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(WebGLFramebuffer, nsIWebGLFramebuffer)
    DOM_CLASSINFO_MAP_ENTRY(nsIWebGLFramebuffer)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(WebGLRenderbuffer, nsIWebGLRenderbuffer)
     DOM_CLASSINFO_MAP_ENTRY(nsIWebGLRenderbuffer)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(WebGLUniformLocation, nsIWebGLUniformLocation)
    DOM_CLASSINFO_MAP_ENTRY(nsIWebGLUniformLocation)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(WebGLActiveInfo, nsIWebGLActiveInfo)
    DOM_CLASSINFO_MAP_ENTRY(nsIWebGLActiveInfo)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(PaintRequest, nsIDOMPaintRequest)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMPaintRequest)
   DOM_CLASSINFO_MAP_END
 
  DOM_CLASSINFO_MAP_BEGIN(PaintRequestList, nsIDOMPaintRequestList)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMPaintRequestList)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(ScrollAreaEvent, nsIDOMScrollAreaEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMScrollAreaEvent)
    DOM_CLASSINFO_UI_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(EventListenerInfo, nsIEventListenerInfo)
    DOM_CLASSINFO_MAP_ENTRY(nsIEventListenerInfo)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(TransitionEvent, nsIDOMTransitionEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMTransitionEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN_NO_CLASS_IF(ContentFrameMessageManager, nsIContentFrameMessageManager)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIFrameMessageManager)
    DOM_CLASSINFO_MAP_ENTRY(nsIContentFrameMessageManager)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(FormData, nsIDOMFormData)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMFormData)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(WebSocket, nsIWebSocket)
    DOM_CLASSINFO_MAP_ENTRY(nsIWebSocket)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(CloseEvent, nsIDOMCloseEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMCloseEvent)
    DOM_CLASSINFO_EVENT_MAP_ENTRIES
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBFactory, nsIIDBFactory)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBFactory)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBRequest, nsIIDBRequest)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBRequest)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBDatabase, nsIIDBDatabase)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBDatabase)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBErrorEvent, nsIIDBErrorEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBErrorEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEvent)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBSuccessEvent, nsIIDBSuccessEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBSuccessEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEvent)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBTransactionEvent, nsIIDBTransactionEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBTransactionEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBSuccessEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBEvent)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEvent)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBObjectStore, nsIIDBObjectStore)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBObjectStore)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBTransaction, nsIIDBTransaction)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBTransaction)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMNSEventTarget)
    DOM_CLASSINFO_MAP_ENTRY(nsIDOMEventTarget)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBCursor, nsIIDBCursor)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBCursor)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBKeyRange, nsIIDBKeyRange)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBKeyRange)
  DOM_CLASSINFO_MAP_END

  DOM_CLASSINFO_MAP_BEGIN(IDBIndex, nsIIDBIndex)
    DOM_CLASSINFO_MAP_ENTRY(nsIIDBIndex)
  DOM_CLASSINFO_MAP_END

#ifdef NS_DEBUG
  {
    PRUint32 i = NS_ARRAY_LENGTH(sClassInfoData);

    if (i != eDOMClassInfoIDCount) {
      NS_ERROR("The number of items in sClassInfoData doesn't match the "
               "number of nsIDOMClassInfo ID's, this is bad! Fix it!");

      return NS_ERROR_NOT_INITIALIZED;
    }

    for (i = 0; i < eDOMClassInfoIDCount; i++) {
      if (!sClassInfoData[i].u.mConstructorFptr ||
          sClassInfoData[i].mDebugID != i) {
        NS_ERROR("Class info data out of sync, you forgot to update "
                 "nsDOMClassInfo.h and nsDOMClassInfo.cpp! Fix this, "
                 "mozilla will not work without this fixed!");

        return NS_ERROR_NOT_INITIALIZED;
      }
    }

    for (i = 0; i < eDOMClassInfoIDCount; i++) {
      if (!sClassInfoData[i].mInterfaces) {
        NS_ERROR("Class info data without an interface list! Fix this, "
                 "mozilla will not work without this fixed!");

        return NS_ERROR_NOT_INITIALIZED;
      }
    }
  }
#endif

  // Initialize static JSString's
  DefineStaticJSVals(cx);

  PRInt32 i;

  for (i = 0; i < eDOMClassInfoIDCount; ++i) {
    RegisterClassName(i);
  }

  for (i = 0; i < eDOMClassInfoIDCount; ++i) {
    RegisterClassProtos(i);
  }

  RegisterExternalClasses();

  sDisableDocumentAllSupport =
    nsContentUtils::GetBoolPref("browser.dom.document.all.disabled");

  sDisableGlobalScopePollutionSupport =
    nsContentUtils::GetBoolPref("browser.dom.global_scope_pollution.disabled");

  sIsInitialized = PR_TRUE;

  return NS_OK;
}

// static
PRInt32
nsDOMClassInfo::GetArrayIndexFromId(JSContext *cx, jsid id, PRBool *aIsNumber)
{
  if (aIsNumber) {
    *aIsNumber = PR_FALSE;
  }

  jsint i;
  if (JSID_IS_INT(id)) {
      i = JSID_TO_INT(id);
  } else {
      JSAutoRequest ar(cx);

      jsval idval;
      jsdouble array_index;
      if (!::JS_IdToValue(cx, id, &idval) ||
          !::JS_ValueToNumber(cx, idval, &array_index) ||
          !::JS_DoubleIsInt32(array_index, &i)) {
        return -1;
      }
  }

  if (aIsNumber) {
    *aIsNumber = PR_TRUE;
  }

  return i;
}

NS_IMETHODIMP
nsDOMClassInfo::GetInterfaces(PRUint32 *aCount, nsIID ***aArray)
{
  PRUint32 count = 0;

  while (mData->mInterfaces[count]) {
    count++;
  }

  *aCount = count;

  if (!count) {
    *aArray = nsnull;

    return NS_OK;
  }

  *aArray = static_cast<nsIID **>(nsMemory::Alloc(count * sizeof(nsIID *)));
  NS_ENSURE_TRUE(*aArray, NS_ERROR_OUT_OF_MEMORY);

  PRUint32 i;
  for (i = 0; i < count; i++) {
    nsIID *iid = static_cast<nsIID *>(nsMemory::Clone(mData->mInterfaces[i],
                                                         sizeof(nsIID)));

    if (!iid) {
      NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(i, *aArray);

      return NS_ERROR_OUT_OF_MEMORY;
    }

    *((*aArray) + i) = iid;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::GetHelperForLanguage(PRUint32 language, nsISupports **_retval)
{
  if (language == nsIProgrammingLanguage::JAVASCRIPT) {
    *_retval = static_cast<nsIXPCScriptable *>(this);

    NS_ADDREF(*_retval);
  } else {
    *_retval = nsnull;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::GetContractID(char **aContractID)
{
  *aContractID = nsnull;

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::GetClassDescription(char **aClassDescription)
{
  return GetClassName(aClassDescription);
}

NS_IMETHODIMP
nsDOMClassInfo::GetClassID(nsCID **aClassID)
{
  *aClassID = nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::GetClassIDNoAlloc(nsCID *aClassID)
{
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsDOMClassInfo::GetImplementationLanguage(PRUint32 *aImplLanguage)
{
  *aImplLanguage = nsIProgrammingLanguage::CPLUSPLUS;

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::GetFlags(PRUint32 *aFlags)
{
  *aFlags = DOMCLASSINFO_STANDARD_FLAGS;

  return NS_OK;
}

// nsIXPCScriptable

NS_IMETHODIMP
nsDOMClassInfo::GetClassName(char **aClassName)
{
  *aClassName = NS_strdup(mData->mName);

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::GetScriptableFlags(PRUint32 *aFlags)
{
  *aFlags = mData->mScriptableFlags;

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::PreCreate(nsISupports *nativeObj, JSContext *cx,
                          JSObject *globalObj, JSObject **parentObj)
{
  *parentObj = globalObj;

  nsCOMPtr<nsPIDOMWindow> piwin = do_QueryWrapper(cx, globalObj);

  if (!piwin) {
    return NS_OK;
  }

  if (piwin->IsOuterWindow()) {
    *parentObj = ((nsGlobalWindow *)piwin.get())->
      GetCurrentInnerWindowInternal()->GetGlobalJSObject();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::Create(nsIXPConnectWrappedNative *wrapper,
                       JSContext *cx, JSObject *obj)
{
  NS_WARNING("nsDOMClassInfo::Create Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::PostCreate(nsIXPConnectWrappedNative *wrapper,
                           JSContext *cx, JSObject *obj)
{
  NS_WARNING("nsDOMClassInfo::PostCreate Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::AddProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                            JSObject *obj, jsid id, jsval *vp,
                            PRBool *_retval)
{
  NS_WARNING("nsDOMClassInfo::AddProperty Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::DelProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                            JSObject *obj, jsid id, jsval *vp,
                            PRBool *_retval)
{
  NS_WARNING("nsDOMClassInfo::DelProperty Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::GetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                            JSObject *obj, jsid id, jsval *vp,
                            PRBool *_retval)
{
  NS_WARNING("nsDOMClassInfo::GetProperty Don't call me!");

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::SetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                            JSObject *obj, jsid id, jsval *vp,
                            PRBool *_retval)
{
  NS_WARNING("nsDOMClassInfo::SetProperty Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::Enumerate(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                          JSObject *obj, PRBool *_retval)
{
#ifdef DEBUG
  if (!sSecMan) {
    NS_ERROR("No security manager!!!");
    return NS_OK;
  }

  // Ask the security manager if it's OK to enumerate
  nsresult rv =
    sSecMan->CheckPropertyAccess(cx, obj, mData->mName, sEnumerate_id,
                                 nsIXPCSecurityManager::ACCESS_GET_PROPERTY);

  NS_ASSERTION(NS_SUCCEEDED(rv),
               "XOWs should have stopped us from getting here!!!");
#endif

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::NewEnumerate(nsIXPConnectWrappedNative *wrapper,
                             JSContext *cx, JSObject *obj, PRUint32 enum_op,
                             jsval *statep, jsid *idp, PRBool *_retval)
{
  NS_WARNING("nsDOMClassInfo::NewEnumerate Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

nsresult
nsDOMClassInfo::ResolveConstructor(JSContext *cx, JSObject *obj,
                                   JSObject **objp)
{
  JSObject *global = ::JS_GetGlobalForObject(cx, obj);

  jsval val;
  JSAutoRequest ar(cx);
  if (!::JS_LookupProperty(cx, global, mData->mName, &val)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (!JSVAL_IS_PRIMITIVE(val)) {
    // If val is not an (non-null) object there either is no
    // constructor for this class, or someone messed with
    // window.classname, just fall through and let the JS engine
    // return the Object constructor.

    if (!::JS_DefinePropertyById(cx, obj, sConstructor_id, val, nsnull, nsnull,
                                 JSPROP_ENUMERATE)) {
      return NS_ERROR_UNEXPECTED;
    }

    *objp = obj;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::NewResolve(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                           JSObject *obj, jsid id, PRUint32 flags,
                           JSObject **objp, PRBool *_retval)
{
  if (id == sConstructor_id && !(flags & JSRESOLVE_ASSIGNING)) {
    return ResolveConstructor(cx, obj, objp);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::Convert(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                        JSObject *obj, PRUint32 type, jsval *vp,
                        PRBool *_retval)
{
  NS_WARNING("nsDOMClassInfo::Convert Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::Finalize(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                         JSObject *obj)
{
  NS_WARNING("nsDOMClassInfo::Finalize Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::CheckAccess(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                            JSObject *obj, jsid id, PRUint32 mode,
                            jsval *vp, PRBool *_retval)
{
  PRUint32 mode_type = mode & JSACC_TYPEMASK;

  if ((mode_type == JSACC_WATCH ||
       mode_type == JSACC_PROTO ||
       mode_type == JSACC_PARENT) &&
      sSecMan) {

    nsresult rv;
    JSObject *real_obj;
    if (wrapper) {
      rv = wrapper->GetJSObject(&real_obj);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      real_obj = obj;
    }

    rv =
      sSecMan->CheckPropertyAccess(cx, real_obj, mData->mName, id,
                                   nsIXPCSecurityManager::ACCESS_GET_PROPERTY);

    if (NS_FAILED(rv)) {
      // Let XPConnect know that the access was not granted.
      *_retval = PR_FALSE;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMClassInfo::Call(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                     JSObject *obj, PRUint32 argc, jsval *argv, jsval *vp,
                     PRBool *_retval)
{
  NS_WARNING("nsDOMClassInfo::Call Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::Construct(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                          JSObject *obj, PRUint32 argc, jsval *argv,
                          jsval *vp, PRBool *_retval)
{
  NS_WARNING("nsDOMClassInfo::Construct Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::HasInstance(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                            JSObject *obj, const jsval &val, PRBool *bp,
                            PRBool *_retval)
{
  NS_WARNING("nsDOMClassInfo::HasInstance Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::Trace(nsIXPConnectWrappedNative *wrapper, JSTracer *trc,
                      JSObject *obj)
{
  NS_WARNING("nsDOMClassInfo::Trace Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::Equality(nsIXPConnectWrappedNative *wrapper, JSContext * cx,
                         JSObject * obj, const jsval &val, PRBool *bp)
{
  NS_WARNING("nsDOMClassInfo::Equality Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::OuterObject(nsIXPConnectWrappedNative *wrapper, JSContext * cx,
                            JSObject * obj, JSObject * *_retval)
{
  NS_WARNING("nsDOMClassInfo::OuterObject Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsDOMClassInfo::InnerObject(nsIXPConnectWrappedNative *wrapper, JSContext * cx,
                            JSObject * obj, JSObject * *_retval)
{
  NS_WARNING("nsDOMClassInfo::InnerObject Don't call me!");

  return NS_ERROR_UNEXPECTED;
}

static nsresult
GetExternalClassInfo(nsScriptNameSpaceManager *aNameSpaceManager,
                     const nsString &aName,
                     const nsGlobalNameStruct *aStruct,
                     const nsGlobalNameStruct **aResult)
{
  NS_ASSERTION(aStruct->mType ==
                 nsGlobalNameStruct::eTypeExternalClassInfoCreator,
               "Wrong type!");

  nsresult rv;
  nsCOMPtr<nsIDOMCIExtension> creator(do_CreateInstance(aStruct->mCID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMScriptObjectFactory> sof(do_GetService(kDOMSOF_CID));
  NS_ENSURE_TRUE(sof, NS_ERROR_FAILURE);

  rv = creator->RegisterDOMCI(NS_ConvertUTF16toUTF8(aName).get(), sof);
  NS_ENSURE_SUCCESS(rv, rv);

  const nsGlobalNameStruct *name_struct;
  rv = aNameSpaceManager->LookupName(aName, &name_struct);
  if (NS_SUCCEEDED(rv) && name_struct &&
      name_struct->mType == nsGlobalNameStruct::eTypeExternalClassInfo) {
    *aResult = name_struct;
  }
  else {
    NS_ERROR("Couldn't get the DOM ClassInfo data.");

    *aResult = nsnull;
  }

  return NS_OK;
}


static nsresult
ResolvePrototype(nsIXPConnect *aXPConnect, nsGlobalWindow *aWin, JSContext *cx,
                 JSObject *obj, const PRUnichar *name,
                 const nsDOMClassInfoData *ci_data,
                 const nsGlobalNameStruct *name_struct,
                 nsScriptNameSpaceManager *nameSpaceManager,
                 JSObject *dot_prototype, PRBool install, PRBool *did_resolve);


NS_IMETHODIMP
nsDOMClassInfo::PostCreatePrototype(JSContext * cx, JSObject * proto)
{
  PRUint32 flags = (mData->mScriptableFlags & DONT_ENUM_STATIC_PROPS)
                   ? 0
                   : JSPROP_ENUMERATE;

  PRUint32 count = 0;
  while (mData->mInterfaces[count]) {
    count++;
  }

  if (!sXPConnect->DefineDOMQuickStubs(cx, proto, flags,
                                       count, mData->mInterfaces)) {
    JS_ClearPendingException(cx);
  }

  // This is called before any other location that requires
  // sObjectClass, so compute it here. We assume that nobody has had a
  // chance to monkey around with proto's prototype chain before this.
  if (!sObjectClass) {
    FindObjectClass(proto);
    NS_ASSERTION(sObjectClass && !strcmp(sObjectClass->name, "Object"),
                 "Incorrect object class!");
  }

  NS_ASSERTION(::JS_GetPrototype(cx, proto) &&
               JS_GET_CLASS(cx, ::JS_GetPrototype(cx, proto)) == sObjectClass,
               "Hmm, somebody did something evil?");

#ifdef DEBUG
  if (mData->mHasClassInterface && mData->mProtoChainInterface &&
      mData->mProtoChainInterface != &NS_GET_IID(nsISupports)) {
    nsCOMPtr<nsIInterfaceInfoManager>
      iim(do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID));

    if (iim) {
      nsCOMPtr<nsIInterfaceInfo> if_info;
      iim->GetInfoForIID(mData->mProtoChainInterface,
                         getter_AddRefs(if_info));

      if (if_info) {
        nsXPIDLCString name;
        if_info->GetName(getter_Copies(name));

        // Allow for inner/non-inner mismatch.
        static const char inner[] = "Inner";
        const char *dataname = mData->mName;
        if (!strncmp(dataname, "Inner", sizeof(inner) - 1)) {
          dataname += sizeof(inner) - 1;
        }

        NS_ASSERTION(nsCRT::strcmp(CutPrefix(name), dataname) == 0,
                     "Class name and proto chain interface name mismatch!");
      }
    }
  }
#endif

  // Make prototype delegation work correctly. Consider if a site sets
  // HTMLElement.prototype.foopy = function () { ... } Now, calling
  // document.body.foopy() needs to ensure that looking up foopy on
  // document.body's prototype will find the right function.
  JSObject *global = ::JS_GetGlobalForObject(cx, proto);

  // Only do this if the global object is a window.
  // XXX Is there a better way to check this?
  nsISupports *globalNative = XPConnect()->GetNativeOfWrapper(cx, global);
  nsCOMPtr<nsPIDOMWindow> piwin = do_QueryInterface(globalNative);
  if(!piwin) {
    return NS_OK;
  }

  nsGlobalWindow *win = nsGlobalWindow::FromSupports(globalNative);
  if (win->IsClosedOrClosing()) {
    return NS_OK;
  }
  if (win->IsOuterWindow()) {
    // XXXjst: Do security checks here when we remove the security
    // checks on the inner window.

    win = win->GetCurrentInnerWindowInternal();

    if (!win || !(global = win->GetGlobalJSObject()) ||
        win->IsClosedOrClosing()) {
      return NS_OK;
    }
  }

  // Don't overwrite a property set by content.
  JSBool found;
  if (!::JS_AlreadyHasOwnUCProperty(cx, global, reinterpret_cast<const jschar*>(mData->mNameUTF16),
                                    nsCRT::strlen(mData->mNameUTF16), &found)) {
    return NS_ERROR_FAILURE;
  }

  nsScriptNameSpaceManager *nameSpaceManager =
    nsJSRuntime::GetNameSpaceManager();
  NS_ENSURE_TRUE(nameSpaceManager, NS_OK);

  PRBool unused;
  return ResolvePrototype(sXPConnect, win, cx, global, mData->mNameUTF16,
                          mData, nsnull, nameSpaceManager, proto, !found,
                          &unused);
}

// static
nsIClassInfo *
NS_GetDOMClassInfoInstance(nsDOMClassInfoID aID)
{
  if (aID >= eDOMClassInfoIDCount) {
    NS_ERROR("Bad ID!");

    return nsnull;
  }

  if (!nsDOMClassInfo::sIsInitialized) {
    nsresult rv = nsDOMClassInfo::Init();

    NS_ENSURE_SUCCESS(rv, nsnull);
  }

  if (!sClassInfoData[aID].mCachedClassInfo) {
    nsDOMClassInfoData& data = sClassInfoData[aID];

    data.mCachedClassInfo = data.u.mConstructorFptr(&data);
    NS_ENSURE_TRUE(data.mCachedClassInfo, nsnull);

    NS_ADDREF(data.mCachedClassInfo);
  }

  NS_ASSERTION(!IS_EXTERNAL(sClassInfoData[aID].mCachedClassInfo),
               "This is bad, internal class marked as external!");

  return sClassInfoData[aID].mCachedClassInfo;
}

// static
nsIClassInfo *
nsDOMClassInfo::GetClassInfoInstance(nsDOMClassInfoData* aData)
{
  NS_ASSERTION(IS_EXTERNAL(aData->mCachedClassInfo)
               || !aData->mCachedClassInfo,
               "This is bad, external class marked as internal!");

  if (!aData->mCachedClassInfo) {
    if (aData->u.mExternalConstructorFptr) {
      aData->mCachedClassInfo =
        aData->u.mExternalConstructorFptr(aData->mName);
    } else {
      aData->mCachedClassInfo = nsDOMGenericSH::doCreate(aData);
    }
    NS_ENSURE_TRUE(aData->mCachedClassInfo, nsnull);

    NS_ADDREF(aData->mCachedClassInfo);
    aData->mCachedClassInfo = MARK_EXTERNAL(aData->mCachedClassInfo);
  }

  return GET_CLEAN_CI_PTR(aData->mCachedClassInfo);
}


// static
void
nsDOMClassInfo::ShutDown()
{
  if (sClassInfoData[0].u.mConstructorFptr) {
    PRUint32 i;

    for (i = 0; i < eDOMClassInfoIDCount; i++) {
      NS_IF_RELEASE(sClassInfoData[i].mCachedClassInfo);
    }
  }

  sTop_id             = JSID_VOID;
  sParent_id          = JSID_VOID;
  sScrollbars_id      = JSID_VOID;
  sLocation_id        = JSID_VOID;
  sConstructor_id     = JSID_VOID;
  s_content_id        = JSID_VOID;
  sContent_id         = JSID_VOID;
  sMenubar_id         = JSID_VOID;
  sToolbar_id         = JSID_VOID;
  sLocationbar_id     = JSID_VOID;
  sPersonalbar_id     = JSID_VOID;
  sStatusbar_id       = JSID_VOID;
  sDialogArguments_id = JSID_VOID;
  sControllers_id     = JSID_VOID;
  sLength_id          = JSID_VOID;
  sInnerHeight_id     = JSID_VOID;
  sInnerWidth_id      = JSID_VOID;
  sOuterHeight_id     = JSID_VOID;
  sOuterWidth_id      = JSID_VOID;
  sScreenX_id         = JSID_VOID;
  sScreenY_id         = JSID_VOID;
  sStatus_id          = JSID_VOID;
  sName_id            = JSID_VOID;
  sOnmousedown_id     = JSID_VOID;
  sOnmouseup_id       = JSID_VOID;
  sOnclick_id         = JSID_VOID;
  sOndblclick_id      = JSID_VOID;
  sOncontextmenu_id   = JSID_VOID;
  sOnmouseover_id     = JSID_VOID;
  sOnmouseout_id      = JSID_VOID;
  sOnkeydown_id       = JSID_VOID;
  sOnkeyup_id         = JSID_VOID;
  sOnkeypress_id      = JSID_VOID;
  sOnmousemove_id     = JSID_VOID;
  sOnfocus_id         = JSID_VOID;
  sOnblur_id          = JSID_VOID;
  sOnsubmit_id        = JSID_VOID;
  sOnreset_id         = JSID_VOID;
  sOnchange_id        = JSID_VOID;
  sOnselect_id        = JSID_VOID;
  sOnload_id          = JSID_VOID;
  sOnbeforeunload_id  = JSID_VOID;
  sOnunload_id        = JSID_VOID;
  sOnhashchange_id    = JSID_VOID;
  sOnreadystatechange_id = JSID_VOID;
  sOnpageshow_id      = JSID_VOID;
  sOnpagehide_id      = JSID_VOID;
  sOnabort_id         = JSID_VOID;
  sOnerror_id         = JSID_VOID;
  sOnpaint_id         = JSID_VOID;
  sOnresize_id        = JSID_VOID;
  sOnscroll_id        = JSID_VOID;
  sOndrag_id          = JSID_VOID;
  sOndragend_id       = JSID_VOID;
  sOndragenter_id     = JSID_VOID;
  sOndragleave_id     = JSID_VOID;
  sOndragover_id      = JSID_VOID;
  sOndragstart_id     = JSID_VOID;
  sOndrop_id          = JSID_VOID;
  sScrollX_id         = JSID_VOID;
  sScrollY_id         = JSID_VOID;
  sScrollMaxX_id      = JSID_VOID;
  sScrollMaxY_id      = JSID_VOID;
  sOpen_id            = JSID_VOID;
  sItem_id            = JSID_VOID;
  sEnumerate_id       = JSID_VOID;
  sNavigator_id       = JSID_VOID;
  sDocument_id        = JSID_VOID;
  sWindow_id          = JSID_VOID;
  sFrames_id          = JSID_VOID;
  sSelf_id            = JSID_VOID;
  sOpener_id          = JSID_VOID;
  sAll_id             = JSID_VOID;
  sTags_id            = JSID_VOID;
  sAddEventListener_id= JSID_VOID;
  sBaseURIObject_id   = JSID_VOID;
  sNodePrincipal_id   = JSID_VOID;
  sDocumentURIObject_id=JSID_VOID;
  sOncopy_id          = JSID_VOID;
  sOncut_id           = JSID_VOID;
  sOnpaste_id         = JSID_VOID;
  sJava_id            = JSID_VOID;
  sPackages_id        = JSID_VOID;
  sOnloadstart_id     = JSID_VOID;
  sOnprogress_id      = JSID_VOID;
  sOnsuspend_id       = JSID_VOID;
  sOnemptied_id       = JSID_VOID;
  sOnstalled_id       = JSID_VOID;
  sOnplay_id          = JSID_VOID;
  sOnpause_id         = JSID_VOID;
  sOnloadedmetadata_id= JSID_VOID;
  sOnloadeddata_id    = JSID_VOID;
  sOnwaiting_id       = JSID_VOID;
  sOnplaying_id       = JSID_VOID;
  sOncanplay_id       = JSID_VOID;
  sOncanplaythrough_id= JSID_VOID;
  sOnseeking_id       = JSID_VOID;
  sOnseeked_id        = JSID_VOID;
  sOntimeupdate_id    = JSID_VOID;
  sOnended_id         = JSID_VOID;
  sOnratechange_id    = JSID_VOID;
  sOndurationchange_id= JSID_VOID;
  sOnvolumechange_id  = JSID_VOID;

  NS_IF_RELEASE(sXPConnect);
  NS_IF_RELEASE(sSecMan);
  sIsInitialized = PR_FALSE;
}

// Window helper

NS_IMETHODIMP
nsInnerWindowSH::PreCreate(nsISupports *nativeObj, JSContext *cx,
                           JSObject *globalObj, JSObject **parentObj)
{
  // Normally ::PreCreate() is used to give XPConnect the parent
  // object for the object that's being wrapped, this parent object is
  // set as the parent of the wrapper and it's also used to find the
  // right scope for the object being wrapped. Now, in the case of the
  // global object the wrapper shouldn't have a parent but we supply
  // one here anyway (the global object itself) and this will be used
  // by XPConnect only to find the right scope, once the scope is
  // found XPConnect will find the existing wrapper (which always
  // exists since it's created on window construction), since an
  // existing wrapper is found the parent we supply here is ignored
  // after the wrapper is found.

  nsCOMPtr<nsIScriptGlobalObject> sgo(do_QueryInterface(nativeObj));
  NS_ASSERTION(sgo, "nativeObj not a global object!");

  nsGlobalWindow *win = nsGlobalWindow::FromSupports(nativeObj);
  JSObject *winObj = win->FastGetGlobalJSObject();
  if (!winObj) {
    NS_ASSERTION(win->GetOuterWindowInternal()->IsCreatingInnerWindow(),
                 "should have a JS object by this point");
    return NS_OK;
  }

  *parentObj = winObj;
  return NS_OK;
}

NS_IMETHODIMP
nsOuterWindowSH::PreCreate(nsISupports *nativeObj, JSContext *cx,
                           JSObject *globalObj, JSObject **parentObj)
{
  nsCOMPtr<nsIScriptGlobalObject> sgo(do_QueryInterface(nativeObj));
  NS_ASSERTION(sgo, "nativeObj not a global object!");

  nsGlobalWindow *win = nsGlobalWindow::FromSupports(nativeObj);
  if (!win->EnsureInnerWindow()) {
    return NS_ERROR_FAILURE;
  }

  *parentObj = win->GetCurrentInnerWindowInternal()->FastGetGlobalJSObject();
  return win->IsChromeWindow() ? NS_OK : NS_SUCCESS_NEEDS_XOW;
}


// This JS class piggybacks on nsHTMLDocumentSH::ReleaseDocument()...

static JSClass sGlobalScopePolluterClass = {
  "Global Scope Polluter",
  JSCLASS_HAS_PRIVATE | JSCLASS_PRIVATE_IS_NSISUPPORTS | JSCLASS_NEW_RESOLVE,
  nsCommonWindowSH::SecurityCheckOnSetProp,
  nsCommonWindowSH::SecurityCheckOnSetProp,
  nsCommonWindowSH::GlobalScopePolluterGetProperty,
  nsCommonWindowSH::SecurityCheckOnSetProp,
  JS_EnumerateStub,
  (JSResolveOp)nsCommonWindowSH::GlobalScopePolluterNewResolve,
  JS_ConvertStub,
  nsHTMLDocumentSH::ReleaseDocument
};


// static
JSBool
nsCommonWindowSH::GlobalScopePolluterGetProperty(JSContext *cx, JSObject *obj,
                                                 jsid id, jsval *vp)
{
  // Someone is accessing a element by referencing its name/id in the
  // global scope, do a security check to make sure that's ok.

  nsresult rv =
    sSecMan->CheckPropertyAccess(cx, ::JS_GetGlobalForObject(cx, obj),
                                 "Window", id,
                                 nsIXPCSecurityManager::ACCESS_GET_PROPERTY);

  if (NS_FAILED(rv)) {
    // The security check failed. The security manager set a JS
    // exception for us.

    return JS_FALSE;
  }

  // Print a warning on the console so developers have a chance to
  // catch and fix these mistakes.
  PrintWarningOnConsole(cx, "GlobalScopeElementReference");

  return JS_TRUE;
}

// static
JSBool
nsCommonWindowSH::SecurityCheckOnSetProp(JSContext *cx, JSObject *obj, jsid id,
                                         jsval *vp)
{
  // Someone is accessing a element by referencing its name/id in the
  // global scope, do a security check to make sure that's ok.

  nsresult rv =
    sSecMan->CheckPropertyAccess(cx, ::JS_GetGlobalForObject(cx, obj),
                                 "Window", id,
                                 nsIXPCSecurityManager::ACCESS_SET_PROPERTY);

  // If !NS_SUCCEEDED(rv) the security check failed. The security
  // manager set a JS exception for us.
  return NS_SUCCEEDED(rv);
}

static nsHTMLDocument*
GetDocument(JSContext *cx, JSObject *obj)
{
  return static_cast<nsHTMLDocument*>(
    static_cast<nsIHTMLDocument*>(::JS_GetPrivate(cx, obj)));
}

// static
JSBool
nsCommonWindowSH::GlobalScopePolluterNewResolve(JSContext *cx, JSObject *obj,
                                                jsid id, uintN flags,
                                          JSObject **objp)
{
  if (flags & (JSRESOLVE_ASSIGNING | JSRESOLVE_DECLARING |
               JSRESOLVE_CLASSNAME | JSRESOLVE_QUALIFIED) ||
      !JSID_IS_STRING(id)) {
    // Nothing to do here if we're either assigning or declaring,
    // resolving a class name, doing a qualified resolve, or
    // resolving a number.

    return JS_TRUE;
  }

  nsHTMLDocument *document = GetDocument(cx, obj);

  if (!document ||
      document->GetCompatibilityMode() != eCompatibility_NavQuirks) {
    // If we don't have a document, or if the document is not in
    // quirks mode, return early.

    return JS_TRUE;
  }

  JSObject *proto = ::JS_GetPrototype(cx, obj);
  JSString *jsstr = JSID_TO_STRING(id);
  JSBool hasProp;

  if (!proto || !::JS_HasPropertyById(cx, proto, id, &hasProp) ||
      hasProp) {
    // No prototype, or the property exists on the prototype. Do
    // nothing.

    return JS_TRUE;
  }

  nsDependentJSString str(jsstr);
  nsCOMPtr<nsISupports> result;
  nsWrapperCache *cache;
  {
    Element *element = document->GetElementById(str);
    result = element;
    cache = element;
  }

  if (!result) {
    document->ResolveName(str, nsnull, getter_AddRefs(result), &cache);
  }

  if (result) {
    jsval v;
    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    nsresult rv = WrapNative(cx, obj, result, cache, PR_TRUE, &v,
                             getter_AddRefs(holder));
    NS_ENSURE_SUCCESS(rv, JS_FALSE);

    if (!::JS_DefinePropertyById(cx, obj, id, v, nsnull, nsnull, 0)) {
      nsDOMClassInfo::ThrowJSException(cx, NS_ERROR_UNEXPECTED);

      return JS_FALSE;
    }

    *objp = obj;
  }

  return JS_TRUE;
}

// static
void
nsCommonWindowSH::InvalidateGlobalScopePolluter(JSContext *cx, JSObject *obj)
{
  JSObject *proto;

  JSAutoRequest ar(cx);

  while ((proto = ::JS_GetPrototype(cx, obj))) {
    if (JS_GET_CLASS(cx, proto) == &sGlobalScopePolluterClass) {
      nsIHTMLDocument *doc = (nsIHTMLDocument *)::JS_GetPrivate(cx, proto);

      NS_IF_RELEASE(doc);

      ::JS_SetPrivate(cx, proto, nsnull);

      // Pull the global scope polluter out of the prototype chain so
      // that it can be freed.
      ::JS_SetPrototype(cx, obj, ::JS_GetPrototype(cx, proto));

      break;
    }

    obj = proto;
  }
}

// static
nsresult
nsCommonWindowSH::InstallGlobalScopePolluter(JSContext *cx, JSObject *obj,
                                             nsIHTMLDocument *doc)
{
  // If global scope pollution is disabled, or if our document is not
  // a HTML document, do nothing
  if (sDisableGlobalScopePollutionSupport || !doc) {
    return NS_OK;
  }

  JSAutoRequest ar(cx);

  JSObject *gsp = ::JS_NewObject(cx, &sGlobalScopePolluterClass, nsnull, obj);
  if (!gsp) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  JSObject *o = obj, *proto;

  // Find the place in the prototype chain where we want this global
  // scope polluter (right before Object.prototype).

  while ((proto = ::JS_GetPrototype(cx, o))) {
    if (JS_GET_CLASS(cx, proto) == sObjectClass) {
      // Set the global scope polluters prototype to Object.prototype
      if (!::JS_SetPrototype(cx, gsp, proto)) {
        return NS_ERROR_UNEXPECTED;
      }

      break;
    }

    o = proto;
  }

  // And then set the prototype of the object whose prototype was
  // Object.prototype to be the global scope polluter.
  if (!::JS_SetPrototype(cx, o, gsp)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (!::JS_SetPrivate(cx, gsp, doc)) {
    return NS_ERROR_UNEXPECTED;
  }

  // The global scope polluter will release doc on destruction (or
  // invalidation).
  NS_ADDREF(doc);

  return NS_OK;
}

static
already_AddRefed<nsIDOMWindow>
GetChildFrame(nsGlobalWindow *win, jsid id)
{
  nsCOMPtr<nsIDOMWindowCollection> frames;
  win->GetFrames(getter_AddRefs(frames));

  nsIDOMWindow *frame = nsnull;

  if (frames) {
    frames->Item(JSID_TO_INT(id), &frame);
  }

  return frame;
}

NS_IMETHODIMP
nsCommonWindowSH::GetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                              JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  nsGlobalWindow *win = nsGlobalWindow::FromWrapper(wrapper);

  JSAutoRequest ar(cx);

#ifdef DEBUG_SH_FORWARDING
  {
    JSString *jsstr = ::JS_ValueToString(cx, id);
    if (jsstr) {
      nsDependentJSString str(jsstr);

      if (win->IsInnerWindow()) {
#ifdef DEBUG_PRINT_INNER
        printf("Property '%s' get on inner window %p\n",
              NS_ConvertUTF16toUTF8(str).get(), (void *)win);
#endif
      } else {
        printf("Property '%s' get on outer window %p\n",
              NS_ConvertUTF16toUTF8(str).get(), (void *)win);
      }
    }
  }
#endif

  JSObject *realObj;
  wrapper->GetJSObject(&realObj);
  if (win->IsOuterWindow() && realObj == obj) {
    // XXXjst: Do security checks here when we remove the security
    // checks on the inner window.

    nsGlobalWindow *innerWin = win->GetCurrentInnerWindowInternal();

    JSObject *innerObj;
    if (innerWin && (innerObj = innerWin->GetGlobalJSObject())) {
#ifdef DEBUG_SH_FORWARDING
      printf(" --- Forwarding get to inner window %p\n", (void *)innerWin);
#endif

      // Forward the get to the inner object
      if (JSID_IS_STRING(id)) {
        *_retval = ::JS_GetPropertyById(cx, innerObj, id, vp);
      } else if (JSID_IS_INT(id)) {
        *_retval = ::JS_GetElement(cx, innerObj, JSID_TO_INT(id), vp);
      } else {
        NS_ERROR("Write me!");

        return NS_ERROR_NOT_IMPLEMENTED;
      }

      return NS_OK;
    }
  }

  // The order in which things are done in this method are a bit
  // whacky, that's because this method is *extremely* performace
  // critical. Don't touch this unless you know what you're doing.

  if (JSID_IS_INT(id)) {
    // If we're accessing a numeric property we'll treat that as if
    // window.frames[n] is accessed (since window.frames === window),
    // if window.frames[n] is a child frame, wrap the frame and return
    // it without doing a security check.

    nsCOMPtr<nsIDOMWindow> frame = GetChildFrame(win, id);
    nsresult rv = NS_OK;

    if (frame) {
      // A numeric property accessed and the numeric property is a
      // child frame, wrap the child frame without doing a security
      // check and return.

      nsGlobalWindow *frameWin = (nsGlobalWindow *)frame.get();
      NS_ASSERTION(frameWin->IsOuterWindow(), "GetChildFrame gave us an inner?");

      frameWin->EnsureInnerWindow();

      nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
      rv = WrapNative(cx, frameWin->GetGlobalJSObject(), frame,
                      &NS_GET_IID(nsIDOMWindow), PR_TRUE, vp,
                      getter_AddRefs(holder));

      if (NS_SUCCEEDED(rv) && !win->IsChromeWindow()) {
        JSObject *scopeobj = JS_GetScopeChain(cx);
        if (!scopeobj) {
          *_retval = JS_FALSE;
          return NS_ERROR_FAILURE;
        }

        rv = sXPConnect->GetXOWForObject(cx, scopeobj, JSVAL_TO_OBJECT(*vp),
                                         vp);
      }
    }

    return NS_FAILED(rv) ? rv : NS_SUCCESS_I_DID_SOMETHING;
  }

  if (JSID_IS_STRING(id) && !JSVAL_IS_PRIMITIVE(*vp) &&
      ::JS_TypeOfValue(cx, *vp) != JSTYPE_FUNCTION) {
    // A named property accessed which could have been resolved to a
    // child frame in nsCommonWindowSH::NewResolve() (*vp will tell us if
    // that's the case). If *vp is a window object (i.e. a child
    // frame), return without doing a security check.
    //
    // Calling GetWrappedNativeOfJSObject() is not all that cheap, so
    // only do that if the JSClass name is one that is likely to be a
    // window object.

    const char *name = JS_GET_CLASS(cx, JSVAL_TO_OBJECT(*vp))->name;

    // The list of Window class names here need to be kept in sync
    // with the actual class names! The class name
    // XPCCrossOriginWrapper needs to be handled here too as XOWs
    // define child frame names with a XOW as the value, and thus
    // we'll need to get through here with XOWs class name too.
    if ((*name == 'W' && strcmp(name, "Window") == 0) ||
        (*name == 'C' && strcmp(name, "ChromeWindow") == 0) ||
        (*name == 'M' && strcmp(name, "ModalContentWindow") == 0) ||
        (*name == 'I' &&
         (strcmp(name, "InnerWindow") == 0 ||
          strcmp(name, "InnerChromeWindow") == 0 ||
          strcmp(name, "InnerModalContentWindow") == 0)) ||
        (*name == 'X' && strcmp(name, "XPCCrossOriginWrapper") == 0)) {
      nsCOMPtr<nsIDOMWindow> window = do_QueryWrapper(cx, JSVAL_TO_OBJECT(*vp));

      if (window) {
        // Yup, *vp is a window object, return early (*vp is already
        // the window, so no need to wrap it again).

        return NS_SUCCESS_I_DID_SOMETHING;
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCommonWindowSH::SetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                              JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  nsGlobalWindow *win = nsGlobalWindow::FromWrapper(wrapper);

#ifdef DEBUG_SH_FORWARDING
  {
    nsDependentJSString str(::JS_ValueToString(cx, id));

    if (win->IsInnerWindow()) {
#ifdef DEBUG_PRINT_INNER
      printf("Property '%s' set on inner window %p\n",
             NS_ConvertUTF16toUTF8(str).get(), (void *)win);
#endif
    } else {
      printf("Property '%s' set on outer window %p\n",
             NS_ConvertUTF16toUTF8(str).get(), (void *)win);
    }
  }
#endif

  JSObject *realObj;
  wrapper->GetJSObject(&realObj);
  if (win->IsOuterWindow() && obj == realObj) {
    // XXXjst: Do security checks here when we remove the security
    // checks on the inner window.

    nsGlobalWindow *innerWin = win->GetCurrentInnerWindowInternal();

    JSObject *innerObj;
    if (innerWin && (innerObj = innerWin->GetGlobalJSObject())) {
#ifdef DEBUG_SH_FORWARDING
      printf(" --- Forwarding set to inner window %p\n", (void *)innerWin);
#endif

      // Forward the set to the inner object
      if (JSID_IS_STRING(id)) {
        *_retval = ::JS_SetPropertyById(cx, innerObj, id, vp);
      } else if (JSID_IS_INT(id)) {
        *_retval = ::JS_SetElement(cx, innerObj, JSID_TO_INT(id), vp);
      } else {
        NS_ERROR("Write me!");

        return NS_ERROR_NOT_IMPLEMENTED;
      }

      return NS_OK;
    }
  }

  if (id == sLocation_id) {
    JSAutoRequest ar(cx);

    JSString *val = ::JS_ValueToString(cx, *vp);
    NS_ENSURE_TRUE(val, NS_ERROR_UNEXPECTED);

    nsCOMPtr<nsIDOMWindowInternal> window(do_QueryWrappedNative(wrapper));
    NS_ENSURE_TRUE(window, NS_ERROR_UNEXPECTED);

    nsCOMPtr<nsIDOMLocation> location;
    nsresult rv = window->GetLocation(getter_AddRefs(location));
    NS_ENSURE_TRUE(NS_SUCCEEDED(rv) && location, rv);

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    rv = WrapNative(cx, obj, location, &NS_GET_IID(nsIDOMLocation), PR_TRUE,
                    vp, getter_AddRefs(holder));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = location->SetHref(nsDependentJSString(val));

    return NS_FAILED(rv) ? rv : NS_SUCCESS_I_DID_SOMETHING;
  }

  return nsEventReceiverSH::SetProperty(wrapper, cx, obj, id, vp, _retval);
}

NS_IMETHODIMP
nsOuterWindowSH::AddProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                             JSObject *obj, jsid id, jsval *vp,
                             PRBool *_retval)
{
  nsGlobalWindow *win = nsGlobalWindow::FromWrapper(wrapper);

  JSObject *realObj;
  wrapper->GetJSObject(&realObj);
  if (obj == realObj) {
    nsGlobalWindow *innerWin = win->GetCurrentInnerWindowInternal();

    JSObject *innerObj;
    if (innerWin && (innerObj = innerWin->GetGlobalJSObject())) {
      if (sResolving) {
        return NS_OK;
      }

#ifdef DEBUG_SH_FORWARDING
      printf(" --- Forwarding add to inner window %p\n", (void *)innerWin);
#endif

      JSPropertyDescriptor desc;
      if (!JS_GetPropertyDescriptorById(cx, obj, id,
                                        JSRESOLVE_QUALIFIED, &desc)) {
        *_retval = JS_FALSE;
        return NS_OK;
      }

      // Forward the add to the inner object
      *_retval = JS_DefinePropertyById(cx, innerObj, id, *vp,
                                       desc.getter, desc.setter,
                                       desc.attrs | JSPROP_ENUMERATE);
      return NS_OK;
    }
  }

  return nsEventReceiverSH::AddProperty(wrapper, cx, obj, id, vp, _retval);
}

NS_IMETHODIMP
nsCommonWindowSH::DelProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                              JSObject *obj, jsid id, jsval *vp,
                              PRBool *_retval)
{
  nsGlobalWindow *win = nsGlobalWindow::FromWrapper(wrapper);

#ifdef DEBUG_SH_FORWARDING
  {
    nsDependentJSString str(::JS_ValueToString(cx, id));

    if (win->IsInnerWindow()) {
#ifdef DEBUG_PRINT_INNER
      printf("Property '%s' del on inner window %p\n",
             NS_ConvertUTF16toUTF8(str).get(), (void *)win);
#endif
    } else {
      printf("Property '%s' del on outer window %p\n",
             NS_ConvertUTF16toUTF8(str).get(), (void *)win);
    }
  }
#endif

  if (win->IsOuterWindow() && !ObjectIsNativeWrapper(cx, obj)) {
    // XXXjst: Do security checks here when we remove the security
    // checks on the inner window.

    nsGlobalWindow *innerWin = win->GetCurrentInnerWindowInternal();

    JSObject *innerObj;
    if (innerWin && (innerObj = innerWin->GetGlobalJSObject())) {
#ifdef DEBUG_SH_FORWARDING
      printf(" --- Forwarding del to inner window %p\n", (void *)innerWin);
#endif

      // Forward the del to the inner object
      *_retval = JS_DeletePropertyById(cx, innerObj, id);

      return NS_OK;
    }
  }

  // Notify any XOWs on our outer window.

  nsGlobalWindow *outerWin = win->GetOuterWindowInternal();
  if (outerWin) {
    nsCOMPtr<nsIXPConnectWrappedNative> wn;
    nsIXPConnect *xpc = nsContentUtils::XPConnect();
    nsresult rv =
      xpc->GetWrappedNativeOfJSObject(cx, outerWin->GetGlobalJSObject(),
                                      getter_AddRefs(wn));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = xpc->UpdateXOWs(cx, wn, nsIXPConnect::XPC_XOW_CLEARSCOPE);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

static const char*
FindConstructorContractID(const nsDOMClassInfoData *aDOMClassInfoData)
{
  PRUint32 i;
  for (i = 0; i < NS_ARRAY_LENGTH(kConstructorMap); ++i) {
    if (&sClassInfoData[kConstructorMap[i].mDOMClassInfoID] ==
        aDOMClassInfoData) {
      return kConstructorMap[i].mContractID;
    }
  }
  return nsnull;
}

static nsDOMConstructorFunc
FindConstructorFunc(const nsDOMClassInfoData *aDOMClassInfoData)
{
  for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(kConstructorFuncMap); ++i) {
    if (&sClassInfoData[kConstructorFuncMap[i].mDOMClassInfoID] ==
        aDOMClassInfoData) {
      return kConstructorFuncMap[i].mConstructorFunc;
    }
  }
  return nsnull;
}

static nsresult
BaseStubConstructor(nsIWeakReference* aWeakOwner,
                    const nsGlobalNameStruct *name_struct, JSContext *cx,
                    JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  nsresult rv;
  nsCOMPtr<nsISupports> native;
  if (name_struct->mType == nsGlobalNameStruct::eTypeClassConstructor) {
    const nsDOMClassInfoData* ci_data =
      &sClassInfoData[name_struct->mDOMClassInfoID];
    const char *contractid = FindConstructorContractID(ci_data);
    if (contractid) {
      native = do_CreateInstance(contractid, &rv);
    }
    else {
      nsDOMConstructorFunc func = FindConstructorFunc(ci_data);
      if (func) {
        rv = func(getter_AddRefs(native));
      }
      else {
        rv = NS_ERROR_NOT_AVAILABLE;
      }
    }
  } else if (name_struct->mType == nsGlobalNameStruct::eTypeExternalConstructor) {
    native = do_CreateInstance(name_struct->mCID, &rv);
  } else if (name_struct->mType == nsGlobalNameStruct::eTypeExternalConstructorAlias) {
    native = do_CreateInstance(name_struct->mAlias->mCID, &rv);
  } else {
    native = do_CreateInstance(*name_struct->mData->mConstructorCID, &rv);
  }
  if (NS_FAILED(rv)) {
    NS_ERROR("Failed to create the object");
    return rv;
  }

  nsCOMPtr<nsIJSNativeInitializer> initializer(do_QueryInterface(native));
  if (initializer) {
    // Initialize object using the current inner window, but only if
    // the caller can access it.
    nsCOMPtr<nsPIDOMWindow> owner = do_QueryReferent(aWeakOwner);
    nsPIDOMWindow* outerWindow = owner ? owner->GetOuterWindow() : nsnull;
    nsPIDOMWindow* currentInner =
      outerWindow ? outerWindow->GetCurrentInnerWindow() : nsnull;
    if (!currentInner ||
        (owner != currentInner &&
         !nsContentUtils::CanCallerAccess(currentInner))) {
      return NS_ERROR_DOM_SECURITY_ERR;
    }

    rv = initializer->Initialize(currentInner, cx, obj, argc, argv);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  nsCOMPtr<nsIScriptObjectOwner> owner(do_QueryInterface(native));
  if (owner) {
    nsIScriptContext *context = nsJSUtils::GetStaticScriptContext(cx, obj);
    if (!context) {
      return NS_ERROR_UNEXPECTED;
    }

    JSObject* new_obj;
    rv = owner->GetScriptObject(context, (void**)&new_obj);

    if (NS_SUCCEEDED(rv)) {
      *rval = OBJECT_TO_JSVAL(new_obj);
    }

    return rv;
  }

  rv = nsDOMGenericSH::WrapNative(cx, obj, native, PR_TRUE, rval);

  return rv;
}

static nsresult
DefineInterfaceConstants(JSContext *cx, JSObject *obj, const nsIID *aIID)
{
  nsCOMPtr<nsIInterfaceInfoManager>
    iim(do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID));
  NS_ENSURE_TRUE(iim, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIInterfaceInfo> if_info;

  nsresult rv = iim->GetInfoForIID(aIID, getter_AddRefs(if_info));
  NS_ENSURE_TRUE(NS_SUCCEEDED(rv) && if_info, rv);

  PRUint16 constant_count;

  if_info->GetConstantCount(&constant_count);

  if (!constant_count) {
    return NS_OK;
  }

  nsCOMPtr<nsIInterfaceInfo> parent_if_info;

  rv = if_info->GetParent(getter_AddRefs(parent_if_info));
  NS_ENSURE_TRUE(NS_SUCCEEDED(rv) && parent_if_info, rv);

  PRUint16 parent_constant_count, i;
  parent_if_info->GetConstantCount(&parent_constant_count);

  for (i = parent_constant_count; i < constant_count; i++) {
    const nsXPTConstant *c = nsnull;

    rv = if_info->GetConstant(i, &c);
    NS_ENSURE_TRUE(NS_SUCCEEDED(rv) && c, rv);

    PRUint16 type = c->GetType().TagPart();

    jsval v;
    switch (type) {
      case nsXPTType::T_I8:
      case nsXPTType::T_U8:
      {
        v = INT_TO_JSVAL(c->GetValue()->val.u8);
        break;
      }
      case nsXPTType::T_I16:
      case nsXPTType::T_U16:
      {
        v = INT_TO_JSVAL(c->GetValue()->val.u16);
        break;
      }
      case nsXPTType::T_I32:
      {
        if (!JS_NewNumberValue(cx, c->GetValue()->val.i32, &v)) {
          return NS_ERROR_UNEXPECTED;
        }
        break;
      }
      case nsXPTType::T_U32:
      {
        if (!JS_NewNumberValue(cx, c->GetValue()->val.u32, &v)) {
          return NS_ERROR_UNEXPECTED;
        }
        break;
      }
      default:
      {
#ifdef NS_DEBUG
        NS_ERROR("Non-numeric constant found in interface.");
#endif
        continue;
      }
    }

    if (!::JS_DefineProperty(cx, obj, c->GetName(), v, nsnull, nsnull,
                             JSPROP_ENUMERATE)) {
      return NS_ERROR_UNEXPECTED;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLBodyElementSH::NewResolve(nsIXPConnectWrappedNative *wrapper, JSContext
                                *cx, JSObject *obj, jsid id, PRUint32 flags,
                                JSObject **objp, PRBool *_retval)
{
  if (id == sOnhashchange_id) {
    // Special handling so |"onhashchange" in document.body| returns true.
    if (!JS_DefinePropertyById(cx, obj, id, JSVAL_VOID,
                               nsnull, nsnull, JSPROP_ENUMERATE)) {
      *_retval = PR_FALSE;
      return NS_ERROR_FAILURE;
    }

    *objp = obj;
    return NS_OK;
  }

  return nsElementSH::NewResolve(wrapper, cx, obj, id, flags, objp, _retval);
}

NS_IMETHODIMP
nsHTMLBodyElementSH::GetProperty(nsIXPConnectWrappedNative *wrapper,
                                 JSContext *cx, JSObject *obj, jsid id,
                                 jsval *vp, PRBool *_retval)
{
  if (id == sOnhashchange_id) {
    // Forward the request to the Window.
    if (!JS_GetPropertyById(cx, JS_GetGlobalForObject(cx, obj), id, vp)) {
      *_retval = PR_FALSE;
      return NS_ERROR_FAILURE;
    }

    return NS_OK;
  }

  return nsElementSH::GetProperty(wrapper, cx, obj, id, vp, _retval);
}

NS_IMETHODIMP
nsHTMLBodyElementSH::SetProperty(nsIXPConnectWrappedNative *wrapper,
                                 JSContext *cx, JSObject *obj,
                                 jsid id, jsval *vp, PRBool *_retval)
{
  if (id == sOnhashchange_id) {
    // Forward the request to the Window.
    if (!JS_SetPropertyById(cx, JS_GetGlobalForObject(cx, obj), id, vp)) {
      *_retval = PR_FALSE;
      return NS_ERROR_FAILURE;
    }

    return NS_OK;
  }

  return nsElementSH::SetProperty(wrapper, cx, obj, id, vp, _retval);
}

class nsDOMConstructor : public nsIDOMDOMConstructor
{
protected:
  nsDOMConstructor(const PRUnichar* aName,
                   PRBool aIsConstructable,
                   nsPIDOMWindow* aOwner)
    : mClassName(aName),
      mConstructable(aIsConstructable),
      mWeakOwner(do_GetWeakReference(aOwner))
  {
  }

public:

  static nsresult Create(const PRUnichar* aName,
                         const nsDOMClassInfoData* aData,
                         const nsGlobalNameStruct* aNameStruct,
                         nsPIDOMWindow* aOwner,
                         nsDOMConstructor** aResult);

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMDOMCONSTRUCTOR

  nsresult Construct(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                     JSObject *obj, PRUint32 argc, jsval *argv,
                     jsval *vp, PRBool *_retval);

  nsresult HasInstance(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                       JSObject *obj, const jsval &val, PRBool *bp,
                       PRBool *_retval);

  nsresult Install(JSContext *cx, JSObject *target, jsval thisAsVal)
  {
    JSBool ok =
      ::JS_DefineUCProperty(cx, target,
                            reinterpret_cast<const jschar *>(mClassName),
                            nsCRT::strlen(mClassName), thisAsVal, nsnull,
                            nsnull, JSPROP_PERMANENT);

    return ok ? NS_OK : NS_ERROR_UNEXPECTED;
  }

private:
  const nsGlobalNameStruct *GetNameStruct()
  {
    if (!mClassName) {
      NS_ERROR("Can't get name");
      return nsnull;
    }

    const nsGlobalNameStruct *nameStruct;
#ifdef DEBUG
    nsresult rv =
#endif
      GetNameStruct(nsDependentString(mClassName), &nameStruct);

    NS_ASSERTION(NS_FAILED(rv) || nameStruct, "Name isn't in hash.");

    return nameStruct;
  }

  static nsresult GetNameStruct(const nsAString& aName,
                                const nsGlobalNameStruct **aNameStruct)
  {
    *aNameStruct = nsnull;

    nsScriptNameSpaceManager *nameSpaceManager = nsJSRuntime::GetNameSpaceManager();
    if (!nameSpaceManager) {
      NS_ERROR("Can't get namespace manager.");
      return NS_ERROR_UNEXPECTED;
    }

    nameSpaceManager->LookupName(aName, aNameStruct);

    // Return NS_OK here, aName just isn't a DOM class but nothing failed.
    return NS_OK;
  }

  static PRBool IsConstructable(const nsDOMClassInfoData *aData)
  {
    if (IS_EXTERNAL(aData->mCachedClassInfo)) {
      const nsExternalDOMClassInfoData* data =
        static_cast<const nsExternalDOMClassInfoData*>(aData);
      return data->mConstructorCID != nsnull;
    }

    return FindConstructorContractID(aData) || FindConstructorFunc(aData);
  }
  static PRBool IsConstructable(const nsGlobalNameStruct *aNameStruct)
  {
    return
      (aNameStruct->mType == nsGlobalNameStruct::eTypeClassConstructor &&
       IsConstructable(&sClassInfoData[aNameStruct->mDOMClassInfoID])) ||
      (aNameStruct->mType == nsGlobalNameStruct::eTypeExternalClassInfo &&
       IsConstructable(aNameStruct->mData)) ||
      aNameStruct->mType == nsGlobalNameStruct::eTypeExternalConstructor ||
      aNameStruct->mType == nsGlobalNameStruct::eTypeExternalConstructorAlias;
  }

  const PRUnichar*   mClassName;
  const PRPackedBool mConstructable;
  nsWeakPtr          mWeakOwner;
};

//static
nsresult
nsDOMConstructor::Create(const PRUnichar* aName,
                         const nsDOMClassInfoData* aData,
                         const nsGlobalNameStruct* aNameStruct,
                         nsPIDOMWindow* aOwner,
                         nsDOMConstructor** aResult)
{
  *aResult = nsnull;
  // Prevent creating a constructor if aOwner is inner window which doesn't have
  // an outer window. If the outer window doesn't have an inner window or the
  // caller can't access the outer window's current inner window then try to use
  // the owner (so long as it is, in fact, an inner window). If that doesn't
  // work then prevent creation also.
  nsPIDOMWindow* outerWindow = aOwner->GetOuterWindow();
  nsPIDOMWindow* currentInner =
    outerWindow ? outerWindow->GetCurrentInnerWindow() : aOwner;
  if (!outerWindow ||
      (aOwner != currentInner &&
       !nsContentUtils::CanCallerAccess(currentInner) &&
       !(currentInner = aOwner)->IsInnerWindow())) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  PRBool constructable = aNameStruct ?
                         IsConstructable(aNameStruct) :
                         IsConstructable(aData);

  *aResult = new nsDOMConstructor(aName, constructable, currentInner);
  NS_ENSURE_TRUE(*aResult, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(*aResult);
  return NS_OK;
}

NS_IMPL_ADDREF(nsDOMConstructor)
NS_IMPL_RELEASE(nsDOMConstructor)
NS_INTERFACE_MAP_BEGIN(nsDOMConstructor)
  NS_INTERFACE_MAP_ENTRY(nsIDOMDOMConstructor)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  if (aIID.Equals(NS_GET_IID(nsIClassInfo))) {
#ifdef DEBUG
    {
      const nsGlobalNameStruct *name_struct = GetNameStruct();
      NS_ASSERTION(!name_struct ||
                   mConstructable == IsConstructable(name_struct),
                   "Can't change constructability dynamically!");
    }
#endif
    foundInterface =
      NS_GetDOMClassInfoInstance(mConstructable ?
                                 eDOMClassInfo_DOMConstructor_id :
                                 eDOMClassInfo_DOMPrototype_id);
    if (!foundInterface) {
      *aInstancePtr = nsnull;
      return NS_ERROR_OUT_OF_MEMORY;
    }
  } else
NS_INTERFACE_MAP_END

nsresult
nsDOMConstructor::Construct(nsIXPConnectWrappedNative *wrapper, JSContext * cx,
                            JSObject * obj, PRUint32 argc, jsval * argv,
                            jsval * vp, PRBool *_retval)
{
  JSObject* class_obj = JSVAL_TO_OBJECT(argv[-2]);
  if (!class_obj) {
    NS_ERROR("nsDOMConstructor::Construct couldn't get constructor object.");
    return NS_ERROR_UNEXPECTED;
  }

  const nsGlobalNameStruct *name_struct = GetNameStruct();
  NS_ENSURE_TRUE(name_struct, NS_ERROR_FAILURE);

  if (!IsConstructable(name_struct)) {
    // ignore return value, we return JS_FALSE anyway
    return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }

  return BaseStubConstructor(mWeakOwner, name_struct, cx, obj, argc, argv, vp);
}

nsresult
nsDOMConstructor::HasInstance(nsIXPConnectWrappedNative *wrapper,
                              JSContext * cx, JSObject * obj,
                              const jsval &v, PRBool *bp, PRBool *_retval)

{
  // No need to look these up in the hash.
  if (JSVAL_IS_PRIMITIVE(v)) {
    return NS_OK;
  }

  JSObject *dom_obj = JSVAL_TO_OBJECT(v);
  NS_ASSERTION(dom_obj, "nsDOMConstructor::HasInstance couldn't get object");

  // This might not be the right object, if XPCNativeWrapping
  // happened.  Get the wrapped native for this object, then get its
  // JS object.
  JSObject *wrapped_obj;
  nsresult rv = nsContentUtils::XPConnect()->GetJSObjectOfWrapper(cx, dom_obj,
                                                                  &wrapped_obj);
  if (NS_SUCCEEDED(rv)) {
    dom_obj = wrapped_obj;
  }

  JSClass *dom_class = JS_GET_CLASS(cx, dom_obj);
  if (!dom_class) {
    NS_ERROR("nsDOMConstructor::HasInstance can't get class.");
    return NS_ERROR_UNEXPECTED;
  }

  const nsGlobalNameStruct *name_struct;
  rv = GetNameStruct(NS_ConvertASCIItoUTF16(dom_class->name), &name_struct);
  if (!name_struct) {
    return rv;
  }

  if (name_struct->mType != nsGlobalNameStruct::eTypeClassConstructor &&
      name_struct->mType != nsGlobalNameStruct::eTypeExternalClassInfo &&
      name_struct->mType != nsGlobalNameStruct::eTypeExternalConstructorAlias) {
    // Doesn't have DOM interfaces.
    return NS_OK;
  }

  const nsGlobalNameStruct *class_name_struct = GetNameStruct();
  NS_ENSURE_TRUE(class_name_struct, NS_ERROR_FAILURE);

  if (name_struct == class_name_struct) {
    *bp = JS_TRUE;

    return NS_OK;
  }

  nsScriptNameSpaceManager *nameSpaceManager = nsJSRuntime::GetNameSpaceManager();
  NS_ASSERTION(nameSpaceManager, "Can't get namespace manager?");

  const nsIID *class_iid;
  if (class_name_struct->mType == nsGlobalNameStruct::eTypeInterface ||
      class_name_struct->mType == nsGlobalNameStruct::eTypeClassProto) {
    class_iid = &class_name_struct->mIID;
  } else if (class_name_struct->mType == nsGlobalNameStruct::eTypeClassConstructor) {
    class_iid =
      sClassInfoData[class_name_struct->mDOMClassInfoID].mProtoChainInterface;
  } else if (class_name_struct->mType == nsGlobalNameStruct::eTypeExternalClassInfo) {
    class_iid = class_name_struct->mData->mProtoChainInterface;
  } else if (class_name_struct->mType == nsGlobalNameStruct::eTypeExternalConstructorAlias) {
    const nsGlobalNameStruct* alias_struct =
      nameSpaceManager->GetConstructorProto(class_name_struct);
    if (!alias_struct) {
      NS_ERROR("Couldn't get constructor prototype.");
      return NS_ERROR_UNEXPECTED;
    }

    if (alias_struct->mType == nsGlobalNameStruct::eTypeClassConstructor) {
      class_iid =
        sClassInfoData[alias_struct->mDOMClassInfoID].mProtoChainInterface;
    } else if (alias_struct->mType == nsGlobalNameStruct::eTypeExternalClassInfo) {
      class_iid = alias_struct->mData->mProtoChainInterface;
    } else {
      NS_ERROR("Expected eTypeClassConstructor or eTypeExternalClassInfo.");
      return NS_ERROR_UNEXPECTED;
    }
  } else {
    *bp = JS_FALSE;

    return NS_OK;
  }

  if (name_struct->mType == nsGlobalNameStruct::eTypeExternalConstructorAlias) {
    name_struct = nameSpaceManager->GetConstructorProto(name_struct);
    if (!name_struct) {
      NS_ERROR("Couldn't get constructor prototype.");
      return NS_ERROR_UNEXPECTED;
    }
  }

  NS_ASSERTION(name_struct->mType == nsGlobalNameStruct::eTypeClassConstructor ||
               name_struct->mType == nsGlobalNameStruct::eTypeExternalClassInfo,
               "The constructor was set up with a struct of the wrong type.");

  const nsDOMClassInfoData *ci_data = nsnull;
  if (name_struct->mType == nsGlobalNameStruct::eTypeClassConstructor &&
      name_struct->mDOMClassInfoID >= 0) {
    ci_data = &sClassInfoData[name_struct->mDOMClassInfoID];
  } else if (name_struct->mType == nsGlobalNameStruct::eTypeExternalClassInfo) {
    ci_data = name_struct->mData;
  }

  nsCOMPtr<nsIInterfaceInfoManager>
    iim(do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID));
  if (!iim) {
    NS_ERROR("nsDOMConstructor::HasInstance can't get interface info mgr.");
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIInterfaceInfo> if_info;
  PRUint32 count = 0;
  const nsIID* class_interface;
  while ((class_interface = ci_data->mInterfaces[count++])) {
    if (class_iid->Equals(*class_interface)) {
      *bp = JS_TRUE;

      return NS_OK;
    }

    iim->GetInfoForIID(class_interface, getter_AddRefs(if_info));
    if (!if_info) {
      NS_ERROR("nsDOMConstructor::HasInstance can't get interface info.");
      return NS_ERROR_UNEXPECTED;
    }

    if_info->HasAncestor(class_iid, bp);

    if (*bp) {
      return NS_OK;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMConstructor::ToString(nsAString &aResult)
{
  aResult.AssignLiteral("[object ");
  aResult.Append(mClassName);
  aResult.Append(PRUnichar(']'));

  return NS_OK;
}


static nsresult
GetXPCProto(nsIXPConnect *aXPConnect, JSContext *cx, nsGlobalWindow *aWin,
            const nsGlobalNameStruct *aNameStruct,
            nsIXPConnectJSObjectHolder **aProto)
{
  NS_ASSERTION(aNameStruct->mType ==
                 nsGlobalNameStruct::eTypeClassConstructor ||
               aNameStruct->mType == nsGlobalNameStruct::eTypeExternalClassInfo,
               "Wrong type!");

  nsCOMPtr<nsIClassInfo> ci;
  if (aNameStruct->mType == nsGlobalNameStruct::eTypeClassConstructor) {
    PRInt32 id = aNameStruct->mDOMClassInfoID;
    NS_ABORT_IF_FALSE(id >= 0, "Negative DOM classinfo?!?");

    nsDOMClassInfoID ci_id = (nsDOMClassInfoID)id;

    ci = NS_GetDOMClassInfoInstance(ci_id);

    // In most cases we want to find the wrapped native prototype in
    // aWin's scope and use that prototype for
    // ClassName.prototype. But in the case where we're setting up
    // "Window.prototype" or "ChromeWindow.prototype" we want to do
    // the look up in aWin's outer window's scope since the inner
    // window's wrapped native prototype comes from the outer
    // window's scope.
    if (ci_id == eDOMClassInfo_Window_id ||
        ci_id == eDOMClassInfo_ModalContentWindow_id ||
        ci_id == eDOMClassInfo_ChromeWindow_id) {
      nsGlobalWindow *scopeWindow = aWin->GetOuterWindowInternal();

      if (scopeWindow) {
        aWin = scopeWindow;
      }
    }
  }
  else {
    ci = nsDOMClassInfo::GetClassInfoInstance(aNameStruct->mData);
  }
  NS_ENSURE_TRUE(ci, NS_ERROR_UNEXPECTED);

  return aXPConnect->GetWrappedNativePrototype(cx, aWin->GetGlobalJSObject(),
                                               ci, aProto);
}

// Either ci_data must be non-null or name_struct must be non-null and of type
// eTypeClassProto.
static nsresult
ResolvePrototype(nsIXPConnect *aXPConnect, nsGlobalWindow *aWin, JSContext *cx,
                 JSObject *obj, const PRUnichar *name,
                 const nsDOMClassInfoData *ci_data,
                 const nsGlobalNameStruct *name_struct,
                 nsScriptNameSpaceManager *nameSpaceManager,
                 JSObject *dot_prototype, PRBool install, PRBool *did_resolve)
{
  NS_ASSERTION(ci_data ||
               (name_struct &&
                name_struct->mType == nsGlobalNameStruct::eTypeClassProto),
               "Wrong type or missing ci_data!");

  nsRefPtr<nsDOMConstructor> constructor;
  nsresult rv = nsDOMConstructor::Create(name, ci_data, name_struct, aWin,
                                         getter_AddRefs(constructor));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
  jsval v;

  rv = nsDOMClassInfo::WrapNative(cx, obj, constructor,
                                  &NS_GET_IID(nsIDOMDOMConstructor),
                                  PR_FALSE, &v, getter_AddRefs(holder));
  NS_ENSURE_SUCCESS(rv, rv);

  if (install) {
    rv = constructor->Install(cx, obj, v);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  JSObject *class_obj;
  holder->GetJSObject(&class_obj);
  NS_ASSERTION(class_obj, "The return value lied");

  const nsIID *primary_iid = &NS_GET_IID(nsISupports);

  if (!ci_data) {
    primary_iid = &name_struct->mIID;
  }
  else if (ci_data->mProtoChainInterface) {
    primary_iid = ci_data->mProtoChainInterface;
  }

  nsCOMPtr<nsIInterfaceInfo> if_info;
  nsCOMPtr<nsIInterfaceInfo> parent;
  const char *class_parent_name = nsnull;

  if (!primary_iid->Equals(NS_GET_IID(nsISupports))) {
    rv = DefineInterfaceConstants(cx, class_obj, primary_iid);
    NS_ENSURE_SUCCESS(rv, rv);

    // Special case for |Node|, which needs constants from Node3
    // too for forwards compatibility.
    if (primary_iid->Equals(NS_GET_IID(nsIDOMNode))) {
      rv = DefineInterfaceConstants(cx, class_obj,
                                    &NS_GET_IID(nsIDOM3Node));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Special case for |Event|, Event needs constants from NSEvent
    // too for backwards compatibility.
    if (primary_iid->Equals(NS_GET_IID(nsIDOMEvent))) {
      rv = DefineInterfaceConstants(cx, class_obj,
                                    &NS_GET_IID(nsIDOMNSEvent));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    nsCOMPtr<nsIInterfaceInfoManager>
      iim(do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID));
    NS_ENSURE_TRUE(iim, NS_ERROR_NOT_AVAILABLE);

    iim->GetInfoForIID(primary_iid, getter_AddRefs(if_info));
    NS_ENSURE_TRUE(if_info, NS_ERROR_UNEXPECTED);

    const nsIID *iid = nsnull;

    if (ci_data && !ci_data->mHasClassInterface) {
      if_info->GetIIDShared(&iid);
    } else {
      if_info->GetParent(getter_AddRefs(parent));
      NS_ENSURE_TRUE(parent, NS_ERROR_UNEXPECTED);

      parent->GetIIDShared(&iid);
    }

    if (iid) {
      if (!iid->Equals(NS_GET_IID(nsISupports))) {
        if (ci_data && !ci_data->mHasClassInterface) {
          // If the class doesn't have a class interface the primary
          // interface is the interface that should be
          // constructor.prototype.__proto__.

          if_info->GetNameShared(&class_parent_name);
        } else {
          // If the class does have a class interface (or there's no
          // real class for this name) then the parent of the
          // primary interface is what we want on
          // constructor.prototype.__proto__.

          NS_ASSERTION(parent, "Whoa, this is bad, null parent here!");

          parent->GetNameShared(&class_parent_name);
        }
      }
    }
  }

  JSObject *proto = nsnull;

  if (class_parent_name) {
    jsval val;

    if (!::JS_LookupProperty(cx, obj, CutPrefix(class_parent_name), &val)) {
      return NS_ERROR_UNEXPECTED;
    }

    JSObject *tmp = JSVAL_IS_OBJECT(val) ? JSVAL_TO_OBJECT(val) : nsnull;

    if (tmp) {
      if (!::JS_LookupProperty(cx, tmp, "prototype", &val)) {
        return NS_ERROR_UNEXPECTED;
      }

      if (JSVAL_IS_OBJECT(val)) {
        proto = JSVAL_TO_OBJECT(val);
      }
    }
  }

  if (dot_prototype) {
    JSObject *xpc_proto_proto = ::JS_GetPrototype(cx, dot_prototype);

    if (proto &&
        (!xpc_proto_proto ||
         JS_GET_CLASS(cx, xpc_proto_proto) == sObjectClass)) {
      if (!::JS_SetPrototype(cx, dot_prototype, proto)) {
        return NS_ERROR_UNEXPECTED;
      }
    }
  } else {
    dot_prototype = ::JS_NewObject(cx, &sDOMConstructorProtoClass, proto,
                                   obj);
    NS_ENSURE_TRUE(dot_prototype, NS_ERROR_OUT_OF_MEMORY);
  }

  v = OBJECT_TO_JSVAL(dot_prototype);

  // Per ECMA, the prototype property is {DontEnum, DontDelete, ReadOnly}
  if (!::JS_DefineProperty(cx, class_obj, "prototype", v, nsnull, nsnull,
                           JSPROP_PERMANENT | JSPROP_READONLY)) {
    return NS_ERROR_UNEXPECTED;
  }

  *did_resolve = PR_TRUE;

  return NS_OK;
}


// static
nsresult
nsCommonWindowSH::GlobalResolve(nsGlobalWindow *aWin, JSContext *cx,
                                JSObject *obj, JSString *str, PRBool *did_resolve)
{
  *did_resolve = PR_FALSE;

  nsScriptNameSpaceManager *nameSpaceManager = nsJSRuntime::GetNameSpaceManager();
  NS_ENSURE_TRUE(nameSpaceManager, NS_ERROR_NOT_INITIALIZED);

  nsDependentJSString name(str);

  const nsGlobalNameStruct *name_struct = nsnull;
  const PRUnichar *class_name = nsnull;

  nameSpaceManager->LookupName(name, &name_struct, &class_name);

  if (!name_struct) {
    return NS_OK;
  }

  NS_ENSURE_TRUE(class_name, NS_ERROR_UNEXPECTED);

  nsresult rv = NS_OK;

  if (name_struct->mType == nsGlobalNameStruct::eTypeExternalClassInfoCreator) {
    rv = GetExternalClassInfo(nameSpaceManager, name, name_struct,
                              &name_struct);
    if (NS_FAILED(rv) || !name_struct) {
      return rv;
    }
  }

  if (name_struct->mType == nsGlobalNameStruct::eTypeInterface) {
    // We're resolving a name of a DOM interface for which there is no
    // direct DOM class, create a constructor object...

    nsRefPtr<nsDOMConstructor> constructor;
    rv = nsDOMConstructor::Create(class_name,
                                  nsnull,
                                  name_struct,
                                  static_cast<nsPIDOMWindow*>(aWin),
                                  getter_AddRefs(constructor));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    jsval v;
    rv = WrapNative(cx, obj, constructor, &NS_GET_IID(nsIDOMDOMConstructor),
                    PR_FALSE, &v, getter_AddRefs(holder));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = constructor->Install(cx, obj, v);
    NS_ENSURE_SUCCESS(rv, rv);

    JSObject *class_obj;
    holder->GetJSObject(&class_obj);
    NS_ASSERTION(class_obj, "The return value lied");

    // ... and define the constants from the DOM interface on that
    // constructor object.

    rv = DefineInterfaceConstants(cx, class_obj, &name_struct->mIID);
    NS_ENSURE_SUCCESS(rv, rv);

    *did_resolve = PR_TRUE;

    return NS_OK;
  }

  if (name_struct->mType == nsGlobalNameStruct::eTypeClassConstructor ||
      name_struct->mType == nsGlobalNameStruct::eTypeExternalClassInfo) {
    // Don't expose chrome only constructors to content windows.
    if (name_struct->mChromeOnly &&
        !nsContentUtils::IsSystemPrincipal(aWin->GetPrincipal())) {
      return NS_OK;
    }

    // Create the XPConnect prototype for our classinfo, PostCreateProto will
    // set up the prototype chain.
    nsCOMPtr<nsIXPConnectJSObjectHolder> proto_holder;
    rv = GetXPCProto(sXPConnect, cx, aWin, name_struct,
                     getter_AddRefs(proto_holder));

    if (NS_SUCCEEDED(rv) && obj != aWin->GetGlobalJSObject()) {
      JSObject* dot_prototype;
      rv = proto_holder->GetJSObject(&dot_prototype);
      NS_ENSURE_SUCCESS(rv, rv);

      const nsDOMClassInfoData *ci_data;
      if (name_struct->mType == nsGlobalNameStruct::eTypeClassConstructor) {
        ci_data = &sClassInfoData[name_struct->mDOMClassInfoID];
      } else {
        ci_data = name_struct->mData;
      }

      return ResolvePrototype(sXPConnect, aWin, cx, obj, class_name, ci_data,
                              name_struct, nameSpaceManager, dot_prototype,
                              PR_TRUE, did_resolve);
    }

    *did_resolve = NS_SUCCEEDED(rv);

    return rv;
  }

  if (name_struct->mType == nsGlobalNameStruct::eTypeClassProto) {
    // We don't have a XPConnect prototype object, let ResolvePrototype create
    // one.
    return ResolvePrototype(sXPConnect, aWin, cx, obj, class_name, nsnull,
                            name_struct, nameSpaceManager, nsnull, PR_TRUE,
                            did_resolve);
  }

  if (name_struct->mType == nsGlobalNameStruct::eTypeExternalConstructorAlias) {
    const nsGlobalNameStruct *alias_struct =
      nameSpaceManager->GetConstructorProto(name_struct);
    NS_ENSURE_TRUE(alias_struct, NS_ERROR_UNEXPECTED);

    // We need to use the XPConnect prototype for the DOM class that this
    // constructor is an alias for (for example for Image we need the prototype
    // for HTMLImageElement).
    nsCOMPtr<nsIXPConnectJSObjectHolder> proto_holder;
    rv = GetXPCProto(sXPConnect, cx, aWin, alias_struct,
                     getter_AddRefs(proto_holder));
    NS_ENSURE_SUCCESS(rv, rv);

    JSObject* dot_prototype;
    rv = proto_holder->GetJSObject(&dot_prototype);
    NS_ENSURE_SUCCESS(rv, rv);

    const nsDOMClassInfoData *ci_data;
    if (alias_struct->mType == nsGlobalNameStruct::eTypeClassConstructor) {
      ci_data = &sClassInfoData[alias_struct->mDOMClassInfoID];
    } else if (alias_struct->mType == nsGlobalNameStruct::eTypeExternalClassInfo) {
      ci_data = alias_struct->mData;
    } else {
      return NS_ERROR_UNEXPECTED;
    }

    return ResolvePrototype(sXPConnect, aWin, cx, obj, class_name, ci_data,
                            name_struct, nameSpaceManager, nsnull, PR_TRUE,
                            did_resolve);
  }

  if (name_struct->mType == nsGlobalNameStruct::eTypeExternalConstructor) {
    nsRefPtr<nsDOMConstructor> constructor;
    rv = nsDOMConstructor::Create(class_name, nsnull, name_struct,
                                  static_cast<nsPIDOMWindow*>(aWin),
                                  getter_AddRefs(constructor));
    NS_ENSURE_SUCCESS(rv, rv);

    jsval val;
    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    rv = WrapNative(cx, obj, constructor, &NS_GET_IID(nsIDOMDOMConstructor),
                    PR_FALSE, &val, getter_AddRefs(holder));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = constructor->Install(cx, obj, val);
    NS_ENSURE_SUCCESS(rv, rv);

    JSObject* class_obj;
    holder->GetJSObject(&class_obj);
    NS_ASSERTION(class_obj, "Why didn't we get a JSObject?");

    *did_resolve = PR_TRUE;

    return NS_OK;
  }

  if (name_struct->mType == nsGlobalNameStruct::eTypeProperty) {
    if (name_struct->mChromeOnly && !nsContentUtils::IsCallerChrome())
      return NS_OK;

    nsCOMPtr<nsISupports> native(do_CreateInstance(name_struct->mCID, &rv));
    NS_ENSURE_SUCCESS(rv, rv);

    jsval prop_val; // Property value.

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    nsCOMPtr<nsIScriptObjectOwner> owner(do_QueryInterface(native));
    if (owner) {
      nsIScriptContext *context = nsJSUtils::GetStaticScriptContext(cx, obj);
      NS_ENSURE_TRUE(context, NS_ERROR_UNEXPECTED);

      JSObject *prop_obj = nsnull;
      rv = owner->GetScriptObject(context, (void**)&prop_obj);
      NS_ENSURE_TRUE(prop_obj, NS_ERROR_UNEXPECTED);

      prop_val = OBJECT_TO_JSVAL(prop_obj);
    } else {
      JSObject *scope;

      if (aWin->IsOuterWindow()) {
        nsGlobalWindow *inner = aWin->GetCurrentInnerWindowInternal();
        NS_ENSURE_TRUE(inner, NS_ERROR_UNEXPECTED);

        scope = inner->GetGlobalJSObject();
      } else {
        scope = aWin->GetGlobalJSObject();
      }

      rv = WrapNative(cx, scope, native, PR_TRUE, &prop_val,
                      getter_AddRefs(holder));
    }

    NS_ENSURE_SUCCESS(rv, rv);

    JSBool ok = ::JS_DefineUCProperty(cx, obj, ::JS_GetStringChars(str),
                                      ::JS_GetStringLength(str),
                                      prop_val, nsnull, nsnull,
                                      JSPROP_ENUMERATE);

    *did_resolve = PR_TRUE;

    return ok ? NS_OK : NS_ERROR_FAILURE;
  }

  if (name_struct->mType == nsGlobalNameStruct::eTypeDynamicNameSet) {
    nsCOMPtr<nsIScriptExternalNameSet> nameset =
      do_CreateInstance(name_struct->mCID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsIScriptContext *context = aWin->GetContext();
    NS_ENSURE_TRUE(context, NS_ERROR_UNEXPECTED);

    rv = nameset->InitializeNameSet(context);

    *did_resolve = PR_TRUE;
  }

  return rv;
}

// Native code for window._content getter, this simply maps
// window._content to window.content for backwards compatibility only.
static JSBool
ContentWindowGetter(JSContext *cx, JSObject *obj, uintN argc, jsval *argv,
                    jsval *rval)
{
  return ::JS_GetProperty(cx, obj, "content", rval);
}

PRBool
nsOuterWindowSH::sResolving = PR_FALSE;

NS_IMETHODIMP
nsOuterWindowSH::NewResolve(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                            JSObject *obj, jsid id, PRUint32 flags,
                            JSObject **objp, PRBool *_retval)
{
  nsGlobalWindow *win = nsGlobalWindow::FromWrapper(wrapper);

  // Note, we won't forward resolve of the location property to the
  // inner window, we need to deal with that one for the outer too
  // since we've got special security protection code for that
  // property.  Also note that we want to enter this block even for
  // native wrappers, so that we'll ensure an inner window to wrap
  // against for the result of whatever we're getting.
  if (id != sLocation_id) {
    // XXXjst: Do security checks here when we remove the security
    // checks on the inner window.

    nsGlobalWindow *innerWin = win->GetCurrentInnerWindowInternal();

    if ((!innerWin || !innerWin->GetExtantDocument()) &&
        !win->IsCreatingInnerWindow()) {
      // We're resolving a property on an outer window for which there
      // is no inner window yet, and we're not in the midst of
      // creating the inner window or in the middle of initializing
      // XPConnect classes on it. If the context is already
      // initialized, force creation of a new inner window. This will
      // create a synthetic about:blank document, and an inner window
      // which may be reused by the actual document being loaded into
      // this outer window. This way properties defined on the window
      // before the document load started will be visible to the
      // document once it's loaded, assuming same origin etc.
      nsIScriptContext *scx = win->GetContextInternal();

      if (scx && scx->IsContextInitialized()) {
        // Grab the new inner window.
        innerWin = win->EnsureInnerWindowInternal();

        if (!innerWin) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }
    }

    JSObject *innerObj;
    JSObject *realObj;
    wrapper->GetJSObject(&realObj);
    if (realObj == obj &&
        innerWin && (innerObj = innerWin->GetGlobalJSObject())) {
#ifdef DEBUG_SH_FORWARDING
      printf(" --- Forwarding resolve to inner window %p\n", (void *)innerWin);
#endif

      JSPropertyDescriptor desc;

      *_retval = JS_GetPropertyDescriptorById(cx, innerObj, id, flags, &desc);

      if (*_retval && desc.obj) {
#ifdef DEBUG_SH_FORWARDING
        printf(" --- Resolve on inner window found property.\n");
#endif

        // The JS engine assumes that the object that we return in objp is on
        // our prototype chain. As a result, for an assignment, it wants to
        // shadow the property by defining one on our object (unless the
        // property has a setter). This confuses our code and, for fast
        // expandos, we end up overriding the fast expando with a slow one. So
        // detect when we're about to get a new property from the JS engine
        // that would shadow a fast expando and define it ourselves, sending
        // ourselves a signal via sResolving that we are doing this. Note that
        // we only care about fast expandos on the innerObj itself, things
        // found further up the prototype chain need to fend for themselves.
        if ((flags & JSRESOLVE_ASSIGNING) &&
            !(desc.attrs & (JSPROP_GETTER | JSPROP_SETTER)) &&
            desc.obj == innerObj) {
          PRBool oldResolving = sResolving;
          sResolving = PR_TRUE;

          *_retval = JS_DefinePropertyById(cx, obj, id, JSVAL_VOID,
                                           nsnull, nsnull,
                                           desc.attrs & JSPROP_ENUMERATE);

          sResolving = oldResolving;
          if (!*_retval) {
            return NS_OK;
          }
        }

        *objp = desc.obj;
      }

      return NS_OK;
    }
  }

  return nsCommonWindowSH::NewResolve(wrapper, cx, obj, id, flags, objp,
                                      _retval);
}

NS_IMETHODIMP
nsCommonWindowSH::NewResolve(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                             JSObject *obj, jsid id, PRUint32 flags,
                             JSObject **objp, PRBool *_retval)
{
  nsGlobalWindow *win = nsGlobalWindow::FromWrapper(wrapper);

  if (!JSID_IS_STRING(id)) {
    if (JSID_IS_INT(id) && !(flags & JSRESOLVE_ASSIGNING)) {
      // If we're resolving a numeric property, treat that as if
      // window.frames[n] is resolved (since window.frames ===
      // window), if window.frames[n] is a child frame, define a
      // property for this index.

      nsCOMPtr<nsIDOMWindow> frame = GetChildFrame(win, id);

      if (frame) {
        // A numeric property accessed and the numeric property is a
        // child frame. Define a property for this index.

        *_retval = ::JS_DefineElement(cx, obj, JSID_TO_INT(id), JSVAL_VOID,
                                      nsnull, nsnull, JSPROP_SHARED);

        if (*_retval) {
          *objp = obj;
        }
      }
    }

    return NS_OK;
  }

  nsIScriptContext *my_context = win->GetContextInternal();

  nsresult rv = NS_OK;

  // Resolve standard classes on my_context's JSContext (or on cx,
  // if we don't have a my_context yet), in case the two contexts
  // have different origins.  We want lazy standard class
  // initialization to behave as if it were done eagerly, on each
  // window's own context (not on some other window-caller's
  // context).

  JSBool did_resolve = JS_FALSE;
  JSContext *my_cx;

  if (!my_context) {
    my_cx = cx;
  } else {
    my_cx = (JSContext *)my_context->GetNativeContext();
  }

  JSBool ok = JS_TRUE;
  jsval exn = JSVAL_VOID;
  if (win->IsInnerWindow()) {
    JSAutoRequest transfer(my_cx);

    JSObject *realObj;
    wrapper->GetJSObject(&realObj);

    // Don't resolve standard classes on XPCNativeWrapper etc, only
    // resolve them if we're resolving on the real global object.
    ok = obj == realObj ?
         ::JS_ResolveStandardClass(my_cx, obj, id, &did_resolve) :
         JS_TRUE;

    if (!ok) {
      // Trust the JS engine (or the script security manager) to set
      // the exception in the JS engine.

      if (!JS_GetPendingException(my_cx, &exn)) {
        return NS_ERROR_UNEXPECTED;
      }

      // Return NS_OK to avoid stomping over the exception that was passed
      // down from the ResolveStandardClass call.
      // Note that the order of the JS_ClearPendingException and
      // JS_SetPendingException is important in the case that my_cx == cx.

      JS_ClearPendingException(my_cx);
    }
  }

  if (!ok) {
    JS_SetPendingException(cx, exn);
    *_retval = JS_FALSE;
    return NS_OK;
  }

  if (did_resolve) {
    *objp = obj;

    return NS_OK;
  }

  if (!(flags & JSRESOLVE_ASSIGNING)) {
    // We want this code to be before the child frame lookup code
    // below so that a child frame named 'constructor' doesn't
    // shadow the window's constructor property.
    if (id == sConstructor_id) {
      return ResolveConstructor(cx, obj, objp);
    }
  }

  if (!my_context || !my_context->IsContextInitialized()) {
    // The context is not yet initialized so there's nothing we can do
    // here yet.

    return NS_OK;
  }


  // Hmm, we do an awful lot of QIs here; maybe we should add a
  // method on an interface that would let us just call into the
  // window code directly...

  JSString *str = JSID_TO_STRING(id);

  // Don't resolve named frames on native wrappers
  if (!ObjectIsNativeWrapper(cx, obj)) {
    nsCOMPtr<nsIDocShellTreeNode> dsn(do_QueryInterface(win->GetDocShell()));

    PRInt32 count = 0;

    if (dsn) {
      dsn->GetChildCount(&count);
    }

    if (count > 0) {
      nsCOMPtr<nsIDocShellTreeItem> child;

      const jschar *chars = ::JS_GetStringChars(str);

      dsn->FindChildWithName(reinterpret_cast<const PRUnichar*>(chars),
                             PR_FALSE, PR_TRUE, nsnull, nsnull,
                             getter_AddRefs(child));

      nsCOMPtr<nsIDOMWindow> child_win(do_GetInterface(child));

      if (child_win) {
        // We found a subframe of the right name, define the property
        // on the wrapper so that ::NewResolve() doesn't get called
        // again for this property name.

        JSObject *wrapperObj;
        wrapper->GetJSObject(&wrapperObj);

        jsval v;
        nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
        rv = WrapNative(cx, wrapperObj, child_win,
                        &NS_GET_IID(nsIDOMWindowInternal), PR_TRUE, &v,
                        getter_AddRefs(holder));
        NS_ENSURE_SUCCESS(rv, rv);

#ifdef DEBUG
        if (!win->IsChromeWindow()) {
          NS_ASSERTION(JSVAL_IS_OBJECT(v) &&
                       !strcmp(JSVAL_TO_OBJECT(v)->getClass()->name,
                               "XPCCrossOriginWrapper"),
                       "Didn't wrap a window!");
        }
#endif

        JSAutoRequest ar(cx);

        PRBool ok = ::JS_DefinePropertyById(cx, obj, id, v, nsnull, nsnull, 0);
        if (!ok) {
          return NS_ERROR_FAILURE;
        }

        *objp = obj;

        return NS_OK;
      }
    }
  }

  // It is not worth calling GlobalResolve() if we are resolving
  // for assignment, since only read-write properties get dealt
  // with there.
  if (!(flags & JSRESOLVE_ASSIGNING)) {
    JSAutoRequest ar(cx);

    // Call GlobalResolve() after we call FindChildWithName() so
    // that named child frames will override external properties
    // which have been registered with the script namespace manager.

    JSBool did_resolve = JS_FALSE;
    rv = GlobalResolve(win, cx, obj, str, &did_resolve);
    NS_ENSURE_SUCCESS(rv, rv);

    if (did_resolve) {
      // GlobalResolve() resolved something, so we're done here.
      *objp = obj;

      return NS_OK;
    }
  }

  if (id == s_content_id) {
    // Map window._content to window.content for backwards
    // compatibility, this should spit out an message on the JS
    // console.

    JSObject *windowObj = win->GetGlobalJSObject();

    JSAutoRequest ar(cx);

    JSFunction *fun = ::JS_NewFunction(cx, ContentWindowGetter, 0, 0,
                                       windowObj, "_content");
    if (!fun) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    JSObject *funObj = ::JS_GetFunctionObject(fun);

    nsAutoGCRoot root(&funObj, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!::JS_DefinePropertyById(cx, windowObj, id, JSVAL_VOID,
                                 JS_DATA_TO_FUNC_PTR(JSPropertyOp, funObj),
                                 nsnull,
                                 JSPROP_ENUMERATE | JSPROP_GETTER |
                                 JSPROP_SHARED)) {
      return NS_ERROR_FAILURE;
    }

    *objp = obj;

    return NS_OK;
  }

  if (id == sLocation_id) {
    // This must be done even if we're just getting the value of
    // window.location (i.e. no checking flags & JSRESOLVE_ASSIGNING
    // here) since we must define window.location to prevent the
    // getter from being overriden (for security reasons).

    // Note: Because we explicitly don't forward to the inner window
    // above, we have to ensure here that our window has a current
    // inner window so that the location object we return will work.

    if (win->IsOuterWindow()) {
      win->EnsureInnerWindow();
    }

    nsCOMPtr<nsIDOMLocation> location;
    rv = win->GetLocation(getter_AddRefs(location));
    NS_ENSURE_SUCCESS(rv, rv);

    // Make sure we wrap the location object in the inner window's
    // scope if we've got an inner window.
    JSObject *scope = nsnull;
    if (win->IsOuterWindow()) {
      nsGlobalWindow *innerWin = win->GetCurrentInnerWindowInternal();

      if (innerWin) {
        scope = innerWin->GetGlobalJSObject();
      }
    }

    if (!scope) {
      wrapper->GetJSObject(&scope);
    }

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    jsval v;
    rv = WrapNative(cx, scope, location, &NS_GET_IID(nsIDOMLocation), PR_TRUE,
                    &v, getter_AddRefs(holder));
    NS_ENSURE_SUCCESS(rv, rv);

#ifdef DEBUG
    if (!win->IsChromeWindow()) {
          NS_ASSERTION(JSVAL_IS_OBJECT(v) &&
                       !strcmp(JSVAL_TO_OBJECT(v)->getClass()->name,
                               "XPCCrossOriginWrapper"),
                       "Didn't wrap a location object!");
    }
#endif

    JSAutoRequest ar(cx);

    JSBool ok = ::JS_DefinePropertyById(cx, obj, id, v, nsnull, nsnull,
                                        JSPROP_PERMANENT | JSPROP_ENUMERATE);

    if (!ok) {
      return NS_ERROR_FAILURE;
    }

    *objp = obj;

    return NS_OK;
  }

  if (id == sOnhashchange_id) {
    // Special handling so |"onhashchange" in window| returns true
    if (!JS_DefinePropertyById(cx, obj, id, JSVAL_VOID,
                                nsnull, nsnull, JSPROP_ENUMERATE)) {
      *_retval = PR_FALSE;
      return NS_ERROR_FAILURE;
    }

    *objp = obj;
    return NS_OK;
  }

  if (flags & JSRESOLVE_ASSIGNING) {
    if (IsReadonlyReplaceable(id) ||
        (!(flags & JSRESOLVE_QUALIFIED) && IsWritableReplaceable(id))) {
      // A readonly "replaceable" property is being set, or a
      // readwrite "replaceable" property is being set w/o being
      // fully qualified. Define the property on obj with the value
      // undefined to override the predefined property. This is done
      // for compatibility with other browsers.
      JSAutoRequest ar(cx);

      if (!::JS_DefinePropertyById(cx, obj, id, JSVAL_VOID, JS_PropertyStub,
                                   JS_PropertyStub, JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
      *objp = obj;

      return NS_OK;
    }
  } else {
    if (id == sNavigator_id) {
      nsCOMPtr<nsIDOMNavigator> navigator;
      rv = win->GetNavigator(getter_AddRefs(navigator));
      NS_ENSURE_SUCCESS(rv, rv);

      jsval v;
      nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
      rv = WrapNative(cx, obj, navigator, &NS_GET_IID(nsIDOMNavigator), PR_TRUE,
                      &v, getter_AddRefs(holder));
      NS_ENSURE_SUCCESS(rv, rv);

      JSAutoRequest ar(cx);

      if (!::JS_DefinePropertyById(cx, obj, id, v, nsnull, nsnull,
                                   JSPROP_READONLY | JSPROP_PERMANENT |
                                   JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
      *objp = obj;

      return NS_OK;
    }

    if (id == sDocument_id) {
      nsCOMPtr<nsIDOMDocument> document;
      rv = win->GetDocument(getter_AddRefs(document));
      NS_ENSURE_SUCCESS(rv, rv);

      // FIXME Ideally we'd have an nsIDocument here and get nsWrapperCache
      //       from it.
      jsval v;
      nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
      rv = WrapNative(cx, obj, document, &NS_GET_IID(nsIDOMDocument), PR_FALSE,
                      &v, getter_AddRefs(holder));
      NS_ENSURE_SUCCESS(rv, rv);

      // The PostCreate hook for the document will handle defining the
      // property
      *objp = obj;

      if (ObjectIsNativeWrapper(cx, obj)) {
        // Unless our object is a native wrapper, in which case we have to
        // define it ourselves.

        *_retval = JS_DefineProperty(cx, obj, "document", v, NULL, NULL,
                                     JSPROP_READONLY | JSPROP_ENUMERATE);
        if (!*_retval) {
          return NS_ERROR_UNEXPECTED;
        }
      }

      return NS_OK;
    }

    if (id == sWindow_id) {
      // window should *always* be the outer window object.
      nsGlobalWindow *oldWin = win;
      win = win->GetOuterWindowInternal();
      NS_ENSURE_TRUE(win, NS_ERROR_NOT_AVAILABLE);

      JSAutoRequest ar(cx);

      jsval winVal = OBJECT_TO_JSVAL(win->GetGlobalJSObject());
      if (!win->IsChromeWindow()) {
        JSObject *scope;
        nsGlobalWindow *innerWin;
        if (oldWin->IsInnerWindow()) {
          scope = oldWin->GetGlobalJSObject();
        } else if ((innerWin = oldWin->GetCurrentInnerWindowInternal())) {
          scope = innerWin->GetGlobalJSObject();
        } else {
          NS_ERROR("I don't know what scope to use!");
          scope = oldWin->GetGlobalJSObject();
        }

        rv = sXPConnect->GetXOWForObject(cx, scope, JSVAL_TO_OBJECT(winVal),
                                         &winVal);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      PRBool ok =
        ::JS_DefinePropertyById(cx, obj, id, winVal, JS_PropertyStub, JS_PropertyStub,
                                JSPROP_READONLY | JSPROP_ENUMERATE);

      if (!ok) {
        return NS_ERROR_FAILURE;
      }
      *objp = obj;

      return NS_OK;
    }

    if (id == sJava_id || id == sPackages_id) {
      static PRBool isResolvingJavaProperties;

      if (!isResolvingJavaProperties) {
        isResolvingJavaProperties = PR_TRUE;

        // Tell the window to initialize the Java properties. The
        // window needs to do this as we need to do this only once,
        // and detecting that reliably from here is hard.

        win->InitJavaProperties(); 

        PRBool hasProp;
        PRBool ok = ::JS_HasProperty(cx, obj, ::JS_GetStringBytes(str),
                                     &hasProp);

        isResolvingJavaProperties = PR_FALSE;

        if (!ok) {
          return NS_ERROR_FAILURE;
        }

        if (hasProp) {
          *objp = obj;

          return NS_OK;
        }
      }
    } else if (id == sDialogArguments_id && win->IsModalContentWindow()) {
      nsCOMPtr<nsIArray> args;
      ((nsGlobalModalWindow *)win)->GetDialogArguments(getter_AddRefs(args));

      nsIScriptContext *script_cx = win->GetContext();
      if (script_cx) {
        JSAutoSuspendRequest asr(cx);

        // Make nsJSContext::SetProperty()'s magic argument array
        // handling happen.
        rv = script_cx->SetProperty(obj, "dialogArguments", args);
        NS_ENSURE_SUCCESS(rv, rv);

        *objp = obj;
      }

      return NS_OK;
    }
  }

  JSObject *oldobj = *objp;
  rv = nsEventReceiverSH::NewResolve(wrapper, cx, obj, id, flags, objp,
                                     _retval);

  if (NS_FAILED(rv) || *objp != oldobj) {
    // Something went wrong, or the property got resolved. Return.
    return rv;
  }

  // Make a fast expando if we're assigning to (not declaring or
  // binding a name) a new undefined property that's not already
  // defined on our prototype chain. This way we can access this
  // expando w/o ever getting back into XPConnect.
  if ((flags & JSRESOLVE_ASSIGNING) && !(flags & JSRESOLVE_WITH) &&
      win->IsInnerWindow()) {
    JSObject *realObj;
    wrapper->GetJSObject(&realObj);

    if (obj == realObj) {
      JSObject *proto = obj->getProto();
      if (proto) {
        JSObject *pobj = NULL;
        jsval val;

        if (!::JS_LookupPropertyWithFlagsById(cx, proto, id, flags,
                                              &pobj, &val)) {
          *_retval = JS_FALSE;

          return NS_OK;
        }

        if (pobj) {
          // A property was found on the prototype chain.
          *objp = pobj;
          return NS_OK;
        }
      }

      // Define a fast expando, the key here is to use JS_PropertyStub
      // as the getter/setter, which makes us stay out of XPConnect
      // when using this property.
      //
      // We don't need to worry about property attributes here as we
      // know here we're dealing with an undefined property set, so
      // we're not declaring readonly or permanent properties.
      //
      // Since we always create the undeclared property here without given a
      // chance for the interpreter to report applicable strict mode warnings,
      // we must take care to check those warnings here.
      JSString *str = JSID_TO_STRING(id);
      if ((!(flags & JSRESOLVE_QUALIFIED) &&
           !js_CheckUndeclaredVarAssignment(cx, str)) ||
          !::JS_DefinePropertyById(cx, obj, id, JSVAL_VOID, JS_PropertyStub,
                                   JS_PropertyStub, JSPROP_ENUMERATE)) {
        *_retval = JS_FALSE;

        return NS_OK;
      }

      *objp = obj;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsOuterWindowSH::NewEnumerate(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                              JSObject *obj, PRUint32 enum_op, jsval *statep,
                              jsid *idp, PRBool *_retval)
{
  switch ((JSIterateOp)enum_op) {
    /* FIXME bug 576449: non-enumerable property support */
    case JSENUMERATE_INIT_ALL:
    case JSENUMERATE_INIT:
    {
#ifdef DEBUG
      // First, do the security check that nsDOMClassInfo does to see
      // if we need to do any work at all.
      nsDOMClassInfo::Enumerate(wrapper, cx, obj, _retval);
      if (!*_retval) {
        NS_ERROR("security wrappers failed us");
        return NS_OK;
      }
#endif

      // The security check passed, let's see if we need to get the inner
      // window's JS object or if we can just start enumerating.
      nsGlobalWindow *win =
        nsGlobalWindow::FromWrapper(wrapper)->GetCurrentInnerWindowInternal();
      JSObject *enumobj = win->FastGetGlobalJSObject();

      // Great, we have the js object, now let's enumerate it.
      JSObject *iterator = JS_NewPropertyIterator(cx, enumobj);
      if (!iterator) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      *statep = OBJECT_TO_JSVAL(iterator);
      if (idp) {
        // Note: With these property iterators, we can't tell ahead of time how
        // many properties we're going to be iterating over.
        *idp = INT_TO_JSID(0);
      }
      break;
    }
    case JSENUMERATE_NEXT:
    {
      JSObject *iterator = (JSObject*)JSVAL_TO_OBJECT(*statep);
      if (!JS_NextProperty(cx, iterator, idp)) {
        return NS_ERROR_UNEXPECTED;
      }

      if (*idp != JSID_VOID) {
        break;
      }

      // Fall through.
    }
    case JSENUMERATE_DESTROY:
      // Let GC at our iterator object.
      *statep = JSVAL_NULL;
      break;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCommonWindowSH::Finalize(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                           JSObject *obj)
{
  nsCOMPtr<nsIScriptGlobalObject> sgo(do_QueryWrappedNative(wrapper));
  NS_ENSURE_TRUE(sgo, NS_ERROR_UNEXPECTED);

  sgo->OnFinalize(nsIProgrammingLanguage::JAVASCRIPT, obj);

  return NS_OK;
}

NS_IMETHODIMP
nsCommonWindowSH::Equality(nsIXPConnectWrappedNative *wrapper, JSContext * cx,
                           JSObject * obj, const jsval &val, PRBool *bp)
{
  *bp = PR_FALSE;

  if (JSVAL_IS_PRIMITIVE(val)) {
    return NS_OK;
  }

  nsCOMPtr<nsIXPConnectWrappedNative> other_wrapper;
  nsContentUtils::XPConnect()->
    GetWrappedNativeOfJSObject(cx, JSVAL_TO_OBJECT(val),
                               getter_AddRefs(other_wrapper));
  if (!other_wrapper) {
    // Not equal.

    return NS_OK;
  }

  nsGlobalWindow *win = nsGlobalWindow::FromWrapper(wrapper);

  NS_ASSERTION(win->IsOuterWindow(),
               "Inner window detected in Equality hook!");

  nsCOMPtr<nsPIDOMWindow> other = do_QueryWrappedNative(other_wrapper);

  if (other) {
    NS_ASSERTION(other->IsOuterWindow(),
                 "Inner window detected in Equality hook!");

    *bp = win->GetOuterWindow() == other->GetOuterWindow();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsInnerWindowSH::OuterObject(nsIXPConnectWrappedNative *wrapper, JSContext * cx,
                             JSObject * obj, JSObject * *_retval)
{
  nsGlobalWindow *origWin = nsGlobalWindow::FromWrapper(wrapper);
  nsGlobalWindow *win = origWin->GetOuterWindowInternal();

  if (!win) {
    // If we no longer have an outer window. No code should ever be
    // running on a window w/o an outer, which means this hook should
    // never be called when we have no outer. But just in case, return
    // null to prevent leaking an inner window to code in a different
    // window.
    *_retval = nsnull;
    return NS_ERROR_UNEXPECTED;
  }

  JSObject *winObj = win->FastGetGlobalJSObject();
  if (!winObj) {
    NS_ASSERTION(origWin->IsOuterWindow(), "What window is this?");
    *_retval = obj;
    return NS_OK;
  }

  *_retval = winObj;
  return NS_OK;
}

NS_IMETHODIMP
nsOuterWindowSH::InnerObject(nsIXPConnectWrappedNative *wrapper, JSContext * cx,
                             JSObject * obj, JSObject * *_retval)
{
  nsGlobalWindow *win = nsGlobalWindow::FromWrapper(wrapper);

  if (win->IsFrozen()) {
    // Return the inner window, or the outer if we're dealing with a
    // frozen outer.

    *_retval = obj;
  } else {
    // Try to find the current inner window.

    nsGlobalWindow *inner = win->GetCurrentInnerWindowInternal();
    if (!inner) {
      // Yikes! No inner window! Instead of leaking the outer window into the
      // scope chain, let's return an error.
      NS_ERROR("using an outer that doesn't have an inner?");
      *_retval = nsnull;

      return NS_ERROR_UNEXPECTED;
    }

    *_retval = inner->FastGetGlobalJSObject();
  }

  return NS_OK;
}

// DOM Location helper

NS_IMETHODIMP
nsLocationSH::CheckAccess(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                          JSObject *obj, jsid id, PRUint32 mode,
                          jsval *vp, PRBool *_retval)
{
  if ((mode & JSACC_TYPEMASK) == JSACC_PROTO && (mode & JSACC_WRITE)) {
    // No setting location.__proto__, ever!

    // Let XPConnect know that the access was not granted.
    *_retval = PR_FALSE;

    return NS_ERROR_DOM_SECURITY_ERR;
  }

  return nsDOMGenericSH::CheckAccess(wrapper, cx, obj, id, mode, vp, _retval);
}

NS_IMETHODIMP
nsLocationSH::PreCreate(nsISupports *nativeObj, JSContext *cx,
                        JSObject *globalObj, JSObject **parentObj)
{
  // window.location can be held onto by both evil pages that want to track the
  // user's progress on the web and bookmarklets that want to use the location
  // object. Parent it to the outer window so that access checks do the Right
  // Thing.
  *parentObj = globalObj;

  nsCOMPtr<nsIDOMLocation> safeLoc(do_QueryInterface(nativeObj));
  if (!safeLoc) {
    // Oops, this wasn't really a location object. This can happen if someone
    // tries to use our scriptable helper as a real object and tries to wrap
    // it, see bug 319296
    return NS_OK;
  }

  nsLocation *loc = (nsLocation *)safeLoc.get();
  nsIDocShell *ds = loc->GetDocShell();
  if (!ds) {
    NS_WARNING("Refusing to create a location in the wrong scope");
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIScriptGlobalObject> sgo = do_GetInterface(ds);

  if (sgo) {
    JSObject *global = sgo->GetGlobalJSObject();

    if (global) {
      *parentObj = global;
    }
  }

  return NS_OK;
}

// DOM Navigator helper
nsresult
nsNavigatorSH::PreCreate(nsISupports *nativeObj, JSContext *cx,
                         JSObject *globalObj, JSObject **parentObj)
{
  // window.navigator is persisted across document transitions if
  // we're loading a page from the same origin. Because of that we
  // need to parent the navigator wrapper at the outer window to avoid
  // holding on to the inner window where the navigator was initially
  // created too long.
  *parentObj = globalObj;

  nsCOMPtr<nsIDOMNavigator> safeNav(do_QueryInterface(nativeObj));
  if (!safeNav) {
    // Oops, this wasn't really a navigator object. This can happen if someone
    // tries to use our scriptable helper as a real object and tries to wrap
    // it, see bug 319296.
    return NS_OK;
  }

  nsNavigator *nav = (nsNavigator *)safeNav.get();
  nsIDocShell *ds = nav->GetDocShell();
  if (!ds) {
    NS_WARNING("Refusing to create a navigator in the wrong scope");
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIScriptGlobalObject> sgo = do_GetInterface(ds);

  if (sgo) {
    JSObject *global = sgo->GetGlobalJSObject();

    if (global) {
      *parentObj = global;
    }
  }

  return NS_OK;
}

// DOM Node helper

PRBool
nsNodeSH::IsCapabilityEnabled(const char* aCapability)
{
  PRBool enabled;
  return sSecMan &&
    NS_SUCCEEDED(sSecMan->IsCapabilityEnabled(aCapability, &enabled)) &&
    enabled;
}

nsresult
nsNodeSH::DefineVoidProp(JSContext* cx, JSObject* obj, jsid id,
                         JSObject** objp)
{
  NS_ASSERTION(JSID_IS_STRING(id), "id must be a string");

  // We want this to be as invisible to content script as possible.  So
  // don't enumerate this, and set is as JSPROP_SHARED so it won't get
  // cached on the object.
  JSBool ok = ::JS_DefinePropertyById(cx, obj, id, JSVAL_VOID,
                                      nsnull, nsnull, JSPROP_SHARED);

  if (!ok) {
    return NS_ERROR_FAILURE;
  }

  *objp = obj;
  return NS_OK;
}

NS_IMETHODIMP
nsNodeSH::PreCreate(nsISupports *nativeObj, JSContext *cx, JSObject *globalObj,
                    JSObject **parentObj)
{
  nsINode *node = static_cast<nsINode*>(nativeObj);
  
#ifdef DEBUG
  {
    nsCOMPtr<nsINode> node_qi(do_QueryInterface(nativeObj));

    // If this assertion fires the QI implementation for the object in
    // question doesn't use the nsINode pointer as the nsISupports
    // pointer. That must be fixed, or we'll crash...
    NS_ASSERTION(node_qi == node, "Uh, fix QI!");
  }
#endif

  // Make sure that we get the owner document of the content node, in case
  // we're in document teardown.  If we are, it's important to *not* use
  // globalObj as the nodes parent since that would give the node the
  // principal of globalObj (i.e. the principal of the document that's being
  // loaded) and not the principal of the document that's being unloaded.
  // See http://bugzilla.mozilla.org/show_bug.cgi?id=227417
  nsIDocument* doc = node->GetOwnerDoc();

  if (!doc) {
    // No document reachable from nativeObj, use the global object
    // that was passed to this method.

    *parentObj = globalObj;

    return node->IsInNativeAnonymousSubtree() ?
      NS_SUCCESS_CHROME_ACCESS_ONLY : NS_OK;
  }

  // If we have a document, make sure one of these is true
  // (1) it has a script handling object,
  // (2) has had one, or has been marked to have had one,
  // (3) we are running a privileged script.
  // Event handling is possible only if (1). If (2) event handling is prevented.
  // If document has never had a script handling object,
  // untrusted scripts (3) shouldn't touch it!
  PRBool hasHadScriptHandlingObject = PR_FALSE;
  NS_ENSURE_STATE(doc->GetScriptHandlingObject(hasHadScriptHandlingObject) ||
                  hasHadScriptHandlingObject ||
                  IsPrivilegedScript());

  nsINode *native_parent;

  PRBool nodeIsElement = node->IsElement();
  if (nodeIsElement && node->AsElement()->IsXUL()) {
    // For XUL elements, use the parent, if any.
    native_parent = node->GetParent();

    if (!native_parent) {
      native_parent = doc;
    }
  } else if (!node->IsNodeOfType(nsINode::eDOCUMENT)) {
    NS_ASSERTION(node->IsNodeOfType(nsINode::eCONTENT) ||
                 node->IsNodeOfType(nsINode::eATTRIBUTE),
                 "Unexpected node type");
                 
    // For attributes and non-XUL content, use the document as scope parent.
    native_parent = doc;

    // But for HTML form controls, use the form as scope parent.
    if (nodeIsElement) {
      if (node->IsNodeOfType(nsINode::eHTML_FORM_CONTROL)) {
        nsCOMPtr<nsIFormControl> form_control(do_QueryInterface(node));

        if (form_control) {
          Element *form = form_control->GetFormElement();

          if (form) {
            // Found a form, use it.
            native_parent = form;
          }
        }
      }
      else {
        // Legend isn't an HTML form control but should have its fieldset form
        // as scope parent at least for backward compatibility.
        nsHTMLLegendElement *legend =
          nsHTMLLegendElement::FromContent(node->AsElement());
        if (legend) {
          Element *form = legend->GetFormElement();

          if (form) {
            native_parent = form;
          }
        }
      }
    }
  } else {
    // We're called for a document object; set the parent to be the
    // document's global object, if there is one

    // Get the scope object from the document.
    nsISupports *scope = doc->GetScopeObject();

    if (scope) {
        jsval v;
        nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
        nsresult rv = WrapNative(cx, globalObj, scope, PR_FALSE, &v,
                                 getter_AddRefs(holder));
        NS_ENSURE_SUCCESS(rv, rv);

        holder->GetJSObject(parentObj);
    }
    else {
      // No global object reachable from this document, use the
      // global object that was passed to this method.

      *parentObj = globalObj;
    }

    // No slim wrappers for a document's scope object.
    return node->IsInNativeAnonymousSubtree() ?
      NS_SUCCESS_CHROME_ACCESS_ONLY : NS_OK;
  }

  // XXXjst: Maybe we need to find the global to use from the
  // nsIScriptGlobalObject that's reachable from the node we're about
  // to wrap here? But that's not always reachable, let's use
  // globalObj for now...

  nsresult rv = WrapNativeParent(cx, globalObj, native_parent, native_parent,
                                 parentObj);
  NS_ENSURE_SUCCESS(rv, rv);

  return node->IsInNativeAnonymousSubtree() ?
    NS_SUCCESS_CHROME_ACCESS_ONLY : NS_SUCCESS_ALLOW_SLIM_WRAPPERS;
}

NS_IMETHODIMP
nsNodeSH::AddProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                      JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  nsNodeSH::PreserveWrapper(GetNative(wrapper, obj));
  return nsEventReceiverSH::AddProperty(wrapper, cx, obj, id, vp, _retval);
}

NS_IMETHODIMP
nsNodeSH::NewResolve(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                     JSObject *obj, jsid id, PRUint32 flags,
                     JSObject **objp, PRBool *_retval)
{
  if ((id == sBaseURIObject_id || id == sNodePrincipal_id) &&
      IsPrivilegedScript()) {
    return DefineVoidProp(cx, obj, id, objp);
  }

  if (id == sOnload_id || id == sOnerror_id) {
    // Make sure that this node can't go away while waiting for a
    // network load that could fire an event handler.
    nsNodeSH::PreserveWrapper(GetNative(wrapper, obj));
  }

  return nsEventReceiverSH::NewResolve(wrapper, cx, obj, id, flags, objp,
                                       _retval);
}

NS_IMETHODIMP
nsNodeSH::GetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                      JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  if (id == sBaseURIObject_id && IsPrivilegedScript()) {
    // I wish GetBaseURI lived on nsINode
    nsCOMPtr<nsIURI> uri;
    nsCOMPtr<nsIContent> content = do_QueryWrappedNative(wrapper, obj);
    if (content) {
      uri = content->GetBaseURI();
      NS_ENSURE_TRUE(uri, NS_ERROR_OUT_OF_MEMORY);
    } else {
      nsCOMPtr<nsIDocument> doc = do_QueryWrappedNative(wrapper, obj);
      NS_ENSURE_TRUE(doc, NS_ERROR_UNEXPECTED);

      uri = doc->GetBaseURI();
      NS_ENSURE_TRUE(uri, NS_ERROR_NOT_AVAILABLE);
    }

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    nsresult rv = WrapNative(cx, obj, uri, &NS_GET_IID(nsIURI), PR_TRUE, vp,
                             getter_AddRefs(holder));
    return NS_FAILED(rv) ? rv : NS_SUCCESS_I_DID_SOMETHING;
  }

  if (id == sNodePrincipal_id && IsPrivilegedScript()) {
    nsCOMPtr<nsINode> node = do_QueryWrappedNative(wrapper, obj);
    NS_ENSURE_TRUE(node, NS_ERROR_UNEXPECTED);

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    nsresult rv = WrapNative(cx, obj, node->NodePrincipal(),
                             &NS_GET_IID(nsIPrincipal), PR_TRUE, vp,
                             getter_AddRefs(holder));
    return NS_FAILED(rv) ? rv : NS_SUCCESS_I_DID_SOMETHING;
  }    

  // Note: none of our ancestors want GetProperty
  return NS_OK;
}

NS_IMETHODIMP
nsNodeSH::SetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                      JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  if ((id == sBaseURIObject_id || id == sNodePrincipal_id) &&
      IsPrivilegedScript()) {
    // We don't want privileged script that can read this property to set it,
    // but _do_ want to allow everyone else to set a value they can then read.
    //
    // XXXbz Is there a better error we could use here?
    return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }

  return nsEventReceiverSH::SetProperty(wrapper, cx, obj, id, vp,_retval);
}

NS_IMETHODIMP
nsNodeSH::GetFlags(PRUint32 *aFlags)
{
  *aFlags = DOMCLASSINFO_STANDARD_FLAGS | nsIClassInfo::CONTENT_NODE;

  return NS_OK;
}

void
nsNodeSH::PreserveWrapper(nsISupports *aNative)
{
  nsINode *node = static_cast<nsINode*>(aNative);
  nsContentUtils::PreserveWrapper(aNative, node);
}

// EventReceiver helper

// static
PRBool
nsEventReceiverSH::ReallyIsEventName(jsid id, jschar aFirstChar)
{
  // I wonder if this is faster than using a hash...

  switch (aFirstChar) {
  case 'a' :
    return id == sOnabort_id;
  case 'b' :
    return (id == sOnbeforeunload_id ||
            id == sOnblur_id);
  case 'c' :
    return (id == sOnchange_id       ||
            id == sOnclick_id        ||
            id == sOncontextmenu_id  ||
            id == sOncopy_id         ||
            id == sOncut_id          ||
            id == sOncanplay_id      ||
            id == sOncanplaythrough_id);
  case 'd' :
    return (id == sOndblclick_id     || 
            id == sOndrag_id         ||
            id == sOndragend_id      ||
            id == sOndragenter_id    ||
            id == sOndragleave_id    ||
            id == sOndragover_id     ||
            id == sOndragstart_id    ||
            id == sOndrop_id         ||
            id == sOndurationchange_id);
  case 'e' :
    return (id == sOnerror_id ||
            id == sOnemptied_id ||
            id == sOnended_id);
  case 'f' :
    return id == sOnfocus_id;
  case 'h' :
    return id == sOnhashchange_id;
  case 'k' :
    return (id == sOnkeydown_id      ||
            id == sOnkeypress_id     ||
            id == sOnkeyup_id);
  case 'l' :
    return (id == sOnload_id           ||
            id == sOnloadeddata_id     ||
            id == sOnloadedmetadata_id ||
            id == sOnloadstart_id);
  case 'm' :
    return (id == sOnmousemove_id    ||
            id == sOnmouseout_id     ||
            id == sOnmouseover_id    ||
            id == sOnmouseup_id      ||
            id == sOnmousedown_id);
  case 'p' :
    return (id == sOnpaint_id        ||
            id == sOnpageshow_id     ||
            id == sOnpagehide_id     ||
            id == sOnpaste_id        ||
            id == sOnpopstate_id     ||
            id == sOnpause_id        ||
            id == sOnplay_id         ||
            id == sOnplaying_id      ||
            id == sOnprogress_id);
  case 'r' :
    return (id == sOnreadystatechange_id ||
            id == sOnreset_id            ||
            id == sOnresize_id           ||
            id == sOnratechange_id);
  case 's' :
    return (id == sOnscroll_id       ||
            id == sOnselect_id       ||
            id == sOnsubmit_id       ||
            id == sOnseeked_id       ||
            id == sOnseeking_id      ||
            id == sOnstalled_id      ||
            id == sOnsuspend_id);
  case 't':
    return id == sOntimeupdate_id;
  case 'u' :
    return id == sOnunload_id;
  case 'v':
    return id == sOnvolumechange_id;
  case 'w':
    return id == sOnwaiting_id;
  }

  return PR_FALSE;
}

nsresult
nsEventReceiverSH::RegisterCompileHandler(nsIXPConnectWrappedNative *wrapper,
                                          JSContext *cx, JSObject *obj,
                                          jsid id, PRBool compile,
                                          PRBool remove,
                                          PRBool *did_define)
{
  NS_PRECONDITION(!compile || !remove,
                  "Can't both compile and remove at the same time");
  *did_define = PR_FALSE;

  if (!IsEventName(id)) {
    return NS_OK;
  }

  if (ObjectIsNativeWrapper(cx, obj)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsIScriptContext *script_cx = nsJSUtils::GetStaticScriptContext(cx, obj);
  NS_ENSURE_TRUE(script_cx, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsPIDOMEventTarget> piTarget =
    do_QueryWrappedNative(wrapper, obj);
  if (!piTarget) {
    // Doesn't do events
    NS_WARNING("Doesn't QI to nsPIDOMEventTarget?");
    return NS_OK;
  }
  
  nsIEventListenerManager* manager = piTarget->GetListenerManager(PR_TRUE);
  NS_ENSURE_TRUE(manager, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIAtom> atom(do_GetAtom(nsDependentJSString(id)));
  NS_ENSURE_TRUE(atom, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv;

  JSObject *scope = ::JS_GetGlobalForObject(cx, obj);

  if (compile) {
    rv = manager->CompileScriptEventListener(script_cx, scope, piTarget, atom,
                                             did_define);
  } else if (remove) {
    rv = manager->RemoveScriptEventListener(atom);
  } else {
    rv = manager->RegisterScriptEventListener(script_cx, scope, piTarget,
                                              atom);
  }

  return NS_FAILED(rv) ? rv : NS_SUCCESS_I_DID_SOMETHING;
}

NS_IMETHODIMP
nsEventReceiverSH::NewResolve(nsIXPConnectWrappedNative *wrapper,
                              JSContext *cx, JSObject *obj, jsid id,
                              PRUint32 flags, JSObject **objp, PRBool *_retval)
{
  if (!JSID_IS_STRING(id)) {
    return NS_OK;
  }

  if (flags & JSRESOLVE_ASSIGNING) {
    if (!IsEventName(id)) {
      // Bail out.  We don't care about this assignment.
      return NS_OK;
    }

    // If we're assigning to an on* property, just resolve to null for
    // now; the assignment will then set the right value. Only do this
    // in the case where the property isn't already defined on the
    // object's prototype chain though.
    JSAutoRequest ar(cx);

    JSObject *proto = ::JS_GetPrototype(cx, obj);
    PRBool ok = PR_TRUE, hasProp = PR_FALSE;
    if (!proto || ((ok = ::JS_HasPropertyById(cx, proto, id, &hasProp)) &&
                   !hasProp)) {
      // Make sure the flags here match those in
      // nsJSContext::BindCompiledEventHandler
      if (!::JS_DefinePropertyById(cx, obj, id, JSVAL_NULL, nsnull, nsnull,
                                   JSPROP_ENUMERATE | JSPROP_PERMANENT)) {
        return NS_ERROR_FAILURE;
      }

      *objp = obj;
      return NS_OK;
    }

    return ok ? NS_OK : NS_ERROR_FAILURE;
  }

  if (id == sAddEventListener_id) {
    return NS_OK;
  }

  PRBool did_define = PR_FALSE;
  nsresult rv = RegisterCompileHandler(wrapper, cx, obj, id, PR_TRUE, PR_FALSE,
                                       &did_define);
  NS_ENSURE_SUCCESS(rv, rv);

  if (did_define) {
    *objp = obj;
  }

  return nsDOMGenericSH::NewResolve(wrapper, cx, obj, id, flags, objp,
                                    _retval);
}

NS_IMETHODIMP
nsEventReceiverSH::SetProperty(nsIXPConnectWrappedNative *wrapper,
                               JSContext *cx, JSObject *obj, jsid id,
                               jsval *vp, PRBool *_retval)
{
  JSAutoRequest ar(cx);

  if ((::JS_TypeOfValue(cx, *vp) != JSTYPE_FUNCTION && !JSVAL_IS_NULL(*vp)) ||
      !JSID_IS_STRING(id) || id == sAddEventListener_id) {
    return NS_OK;
  }

  PRBool did_compile; // Ignored here.

  return RegisterCompileHandler(wrapper, cx, obj, id, PR_FALSE,
                                JSVAL_IS_NULL(*vp), &did_compile);
}

NS_IMETHODIMP
nsEventReceiverSH::AddProperty(nsIXPConnectWrappedNative *wrapper,
                               JSContext *cx, JSObject *obj, jsid id,
                               jsval *vp, PRBool *_retval)
{
  return nsEventReceiverSH::SetProperty(wrapper, cx, obj, id, vp, _retval);
}

// EventTarget helper

NS_IMETHODIMP
nsEventTargetSH::PreCreate(nsISupports *nativeObj, JSContext *cx,
                           JSObject *globalObj, JSObject **parentObj)
{
  nsDOMEventTargetWrapperCache *target =
    nsDOMEventTargetWrapperCache::FromSupports(nativeObj);

  nsCOMPtr<nsIScriptGlobalObject> native_parent;
  target->GetParentObject(getter_AddRefs(native_parent));

  *parentObj = native_parent ? native_parent->GetGlobalJSObject() : globalObj;

  return NS_OK;
}

NS_IMETHODIMP
nsEventTargetSH::AddProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                             JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  if (id == sAddEventListener_id) {
    return NS_OK;
  }

  nsEventTargetSH::PreserveWrapper(GetNative(wrapper, obj));

  return NS_OK;
}

void
nsEventTargetSH::PreserveWrapper(nsISupports *aNative)
{
  nsDOMEventTargetWrapperCache *target =
    nsDOMEventTargetWrapperCache::FromSupports(aNative);
  nsContentUtils::PreserveWrapper(aNative, target);
}


// Element helper

static PRBool
GetBindingURL(Element *aElement, nsIDocument *aDocument,
              nsCSSValue::URL **aResult)
{
  // If we have a frame the frame has already loaded the binding.  And
  // otherwise, don't do anything else here unless we're dealing with
  // XUL.
  nsIPresShell *shell = aDocument->GetShell();
  if (!shell || aElement->GetPrimaryFrame() || !aElement->IsXUL()) {
    *aResult = nsnull;

    return PR_TRUE;
  }

  // Get the computed -moz-binding directly from the style context
  nsPresContext *pctx = shell->GetPresContext();
  NS_ENSURE_TRUE(pctx, PR_FALSE);

  nsRefPtr<nsStyleContext> sc = pctx->StyleSet()->ResolveStyleFor(aElement,
                                                                  nsnull);
  NS_ENSURE_TRUE(sc, PR_FALSE);

  *aResult = sc->GetStyleDisplay()->mBinding;

  return PR_TRUE;
}

NS_IMETHODIMP
nsElementSH::PreCreate(nsISupports *nativeObj, JSContext *cx,
                       JSObject *globalObj, JSObject **parentObj)
{
  nsresult rv = nsNodeSH::PreCreate(nativeObj, cx, globalObj, parentObj);
  NS_ENSURE_SUCCESS(rv, rv);

  Element *element = static_cast<Element*>(nativeObj);

#ifdef DEBUG
  {
    nsCOMPtr<nsIContent> content_qi(do_QueryInterface(nativeObj));

    // If this assertion fires the QI implementation for the object in
    // question doesn't use the nsIContent pointer as the nsISupports
    // pointer. That must be fixed, or we'll crash...
    NS_ASSERTION(content_qi == element, "Uh, fix QI!");
  }
#endif

  nsIDocument *doc = element->HasFlag(NODE_FORCE_XBL_BINDINGS) ?
                     element->GetOwnerDoc() :
                     element->GetCurrentDoc();

  if (!doc) {
    return rv;
  }

  if (element->HasFlag(NODE_MAY_BE_IN_BINDING_MNGR) &&
      doc->BindingManager()->GetBinding(element)) {
    // Don't allow slim wrappers.
    return rv == NS_SUCCESS_ALLOW_SLIM_WRAPPERS ? NS_OK : rv;
  }

  nsCSSValue::URL *bindingURL;
  PRBool ok = GetBindingURL(element, doc, &bindingURL);
  NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

  // Only allow slim wrappers if there's no binding.
  if (!bindingURL) {
    return rv;
  }

  element->SetFlags(NODE_ATTACH_BINDING_ON_POSTCREATE);

  return rv == NS_SUCCESS_ALLOW_SLIM_WRAPPERS ? NS_OK : rv;
}

NS_IMETHODIMP
nsElementSH::PostCreate(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                        JSObject *obj)
{
  Element *element = static_cast<Element*>(wrapper->Native());

#ifdef DEBUG
  {
    nsCOMPtr<nsIContent> content_qi(do_QueryWrappedNative(wrapper));

    // If this assertion fires the QI implementation for the object in
    // question doesn't use the nsIContent pointer as the nsISupports
    // pointer. That must be fixed, or we'll crash...
    NS_ASSERTION(content_qi == element, "Uh, fix QI!");
  }
#endif

  nsIDocument* doc;
  if (element->HasFlag(NODE_FORCE_XBL_BINDINGS)) {
    doc = element->GetOwnerDoc();
  }
  else {
    doc = element->GetCurrentDoc();
  }

  if (!doc) {
    // There's no baseclass that cares about this call so we just
    // return here.

    return NS_OK;
  }

  // We must ensure that the XBL Binding is installed before we hand
  // back this object.

  if (!element->HasFlag(NODE_ATTACH_BINDING_ON_POSTCREATE)) {
    // There's already a binding for this element so nothing left to
    // be done here.

    // In theory we could call ExecuteAttachedHandler here when it's safe to
    // run script if we also removed the binding from the PAQ queue, but that
    // seems like a scary change that would mosly just add more
    // inconsistencies.

    return NS_OK;
  }

  element->UnsetFlags(NODE_ATTACH_BINDING_ON_POSTCREATE);

  // Make sure the style context goes away _before_ we load the binding
  // since that can destroy the relevant presshell.
  nsCSSValue::URL *bindingURL;
  PRBool ok = GetBindingURL(element, doc, &bindingURL);
  NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

  if (!bindingURL) {
    // No binding, nothing left to do here.
    return NS_OK;
  }

  nsCOMPtr<nsIURI> uri = bindingURL->mURI;
  nsCOMPtr<nsIPrincipal> principal = bindingURL->mOriginPrincipal;

  // We have a binding that must be installed.
  PRBool dummy;

  nsCOMPtr<nsIXBLService> xblService(do_GetService("@mozilla.org/xbl;1"));
  NS_ENSURE_TRUE(xblService, NS_ERROR_NOT_AVAILABLE);

  nsRefPtr<nsXBLBinding> binding;
  xblService->LoadBindings(element, uri, principal, PR_FALSE,
                           getter_AddRefs(binding), &dummy);
  
  if (binding) {
    if (nsContentUtils::IsSafeToRunScript()) {
      binding->ExecuteAttachedHandler();
    }
    else {
      nsContentUtils::AddScriptRunner(
        NS_NewRunnableMethod(binding, &nsXBLBinding::ExecuteAttachedHandler));
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsElementSH::Enumerate(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                       JSObject *obj, PRBool *_retval)
{
  // Make sure to not call the superclass here!
  nsCOMPtr<nsIContent> content(do_QueryWrappedNative(wrapper, obj));
  NS_ENSURE_TRUE(content, NS_ERROR_UNEXPECTED);

  nsIDocument* doc = content->GetOwnerDoc();
  if (!doc) {
    // Nothing else to do here
    return NS_OK;
  }

  nsRefPtr<nsXBLBinding> binding = doc->BindingManager()->GetBinding(content);
  if (!binding) {
    // Nothing else to do here
    return NS_OK;
  }

  *_retval = binding->ResolveAllFields(cx, obj);
  
  return NS_OK;
}
  

// Generic array scriptable helper.

NS_IMETHODIMP
nsGenericArraySH::NewResolve(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                             JSObject *obj, jsid id, PRUint32 flags,
                             JSObject **objp, PRBool *_retval)
{
  if (id == sLength_id) {
    // Bail early; this isn't something we're interested in
    return NS_OK;
  }
  
  PRBool is_number = PR_FALSE;
  PRInt32 n = GetArrayIndexFromId(cx, id, &is_number);

  if (is_number && n >= 0) {
    // XXX The following is a cheap optimization to avoid hitting xpconnect to
    // get the length. We may want to consider asking our concrete
    // implementation for the length, and falling back onto the GetProperty if
    // it doesn't provide one.

    PRUint32 length;
    nsresult rv = GetLength(wrapper, cx, obj, &length);
    NS_ENSURE_SUCCESS(rv, rv);

    if ((PRUint32)n < length) {
      *_retval = ::JS_DefineElement(cx, obj, n, JSVAL_VOID, nsnull, nsnull,
                                    JSPROP_ENUMERATE | JSPROP_SHARED);
      *objp = obj;
    }
  }

  return NS_OK;
}

nsresult
nsGenericArraySH::GetLength(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                             JSObject *obj, PRUint32 *length)
{
  *length = 0;

  jsval lenval;
  if (!JS_GetProperty(cx, obj, "length", &lenval)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (!JSVAL_IS_INT(lenval)) {
    // This can apparently happen with some sparse array impls falling back
    // onto this code.
    return NS_OK;
  }

  PRInt32 slen = JSVAL_TO_INT(lenval);
  if (slen < 0) {
    return NS_OK;
  }

  *length = (PRUint32)slen;

  return NS_OK;
}

NS_IMETHODIMP
nsGenericArraySH::Enumerate(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                            JSObject *obj, PRBool *_retval)
{
  // Recursion protection in case someone tries to be smart and call
  // the enumerate hook from a user defined .length getter, or
  // somesuch.

  static PRBool sCurrentlyEnumerating;

  if (sCurrentlyEnumerating) {
    // Don't recurse to death.
    return NS_OK;
  }

  sCurrentlyEnumerating = PR_TRUE;

  jsval len_val;
  JSAutoRequest ar(cx);
  JSBool ok = ::JS_GetProperty(cx, obj, "length", &len_val);

  if (ok && JSVAL_IS_INT(len_val)) {
    PRInt32 length = JSVAL_TO_INT(len_val);

    for (PRInt32 i = 0; ok && i < length; ++i) {
      ok = ::JS_DefineElement(cx, obj, i, JSVAL_VOID, nsnull, nsnull,
                              JSPROP_ENUMERATE | JSPROP_SHARED);
    }
  }

  sCurrentlyEnumerating = PR_FALSE;

  return ok ? NS_OK : NS_ERROR_UNEXPECTED;
}

// Array scriptable helper

NS_IMETHODIMP
nsArraySH::GetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                       JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  PRBool is_number = PR_FALSE;
  PRInt32 n = GetArrayIndexFromId(cx, id, &is_number);

  nsresult rv = NS_OK;

  if (is_number) {
    if (n < 0) {
      return NS_ERROR_DOM_INDEX_SIZE_ERR;
    }

    // Make sure rv == NS_OK here, so GetItemAt implementations that never fail
    // don't have to set rv.
    rv = NS_OK;
    nsWrapperCache *cache = nsnull;
    nsISupports* array_item =
      GetItemAt(GetNative(wrapper, obj), n, &cache, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    if (array_item) {
      rv = WrapNative(cx, obj, array_item, cache, PR_TRUE, vp);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = NS_SUCCESS_I_DID_SOMETHING;
    }
  }

  return rv;
}


// NodeList scriptable helper

nsresult
nsNodeListSH::PreCreate(nsISupports *nativeObj, JSContext *cx,
                        JSObject *globalObj, JSObject **parentObj)
{
  nsWrapperCache* cache = nsnull;
  CallQueryInterface(nativeObj, &cache);
  if (!cache) {
    return nsDOMClassInfo::PreCreate(nativeObj, cx, globalObj, parentObj);
  }

  // nsChildContentList is the only class that uses nsNodeListSH and has a
  // cached wrapper.
  nsChildContentList *list = nsChildContentList::FromSupports(nativeObj);
  nsINode *native_parent = list->GetParentObject();
  if (!native_parent) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv =
    WrapNativeParent(cx, globalObj, native_parent, native_parent, parentObj);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_SUCCESS_ALLOW_SLIM_WRAPPERS;
}

nsresult
nsNodeListSH::GetLength(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                        JSObject *obj, PRUint32 *length)
{
  nsINodeList* list = static_cast<nsINodeList*>(GetNative(wrapper, obj));
#ifdef DEBUG
  {
    nsCOMPtr<nsINodeList> list_qi = do_QueryWrappedNative(wrapper, obj);

    // If this assertion fires the QI implementation for the object in
    // question doesn't use the nsINodeList pointer as the nsISupports
    // pointer. That must be fixed, or we'll crash...
    NS_ASSERTION(list_qi == list, "Uh, fix QI!");
  }
#endif

  return list->GetLength(length);
}

nsISupports*
nsNodeListSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                        nsWrapperCache **aCache, nsresult *aResult)
{
  nsINodeList* list = static_cast<nsINodeList*>(aNative);
#ifdef DEBUG
  {
    nsCOMPtr<nsINodeList> list_qi = do_QueryInterface(aNative);

    // If this assertion fires the QI implementation for the object in
    // question doesn't use the nsINodeList pointer as the nsISupports
    // pointer. That must be fixed, or we'll crash...
    NS_ASSERTION(list_qi == list, "Uh, fix QI!");
  }
#endif

  nsINode *node;
  *aCache = node = list->GetNodeAt(aIndex);
  return node;
}


// StringList scriptable helper

nsresult
nsStringListSH::GetStringAt(nsISupports *aNative, PRInt32 aIndex,
                            nsAString& aResult)
{
  nsCOMPtr<nsIDOMDOMStringList> list(do_QueryInterface(aNative));
  NS_ENSURE_TRUE(list, NS_ERROR_UNEXPECTED);

  return list->Item(aIndex, aResult);
}


// DOMTokenList scriptable helper

nsresult
nsDOMTokenListSH::GetStringAt(nsISupports *aNative, PRInt32 aIndex,
                              nsAString& aResult)
{
  nsCOMPtr<nsIDOMDOMTokenList> list(do_QueryInterface(aNative));
  NS_ENSURE_TRUE(list, NS_ERROR_UNEXPECTED);

  return list->Item(aIndex, aResult);
}


// Named Array helper

NS_IMETHODIMP
nsNamedArraySH::GetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                            JSObject *obj, jsid id, jsval *vp,
                            PRBool *_retval)
{
  if (JSID_IS_STRING(id) && !ObjectIsNativeWrapper(cx, obj)) {
    nsresult rv = NS_OK;
    nsWrapperCache *cache = nsnull;
    nsISupports* item = GetNamedItem(GetNative(wrapper, obj),
                                     nsDependentJSString(id), &cache, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    if (item) {
      rv = WrapNative(cx, obj, item, cache, PR_TRUE, vp);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = NS_SUCCESS_I_DID_SOMETHING;
    }

    // Don't fall through to nsArraySH::GetProperty() here
    return rv;
  }

  return nsArraySH::GetProperty(wrapper, cx, obj, id, vp, _retval);
}


// NamedNodeMap helper

nsISupports*
nsNamedNodeMapSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                            nsWrapperCache **aCache, nsresult *aResult)
{
  nsDOMAttributeMap* map = nsDOMAttributeMap::FromSupports(aNative);

  nsINode *attr;
  *aCache = attr = map->GetItemAt(aIndex, aResult);
  return attr;
}

nsISupports*
nsNamedNodeMapSH::GetNamedItem(nsISupports *aNative, const nsAString& aName,
                               nsWrapperCache **aCache, nsresult *aResult)
{
  nsDOMAttributeMap* map = nsDOMAttributeMap::FromSupports(aNative);

  nsINode *attr;
  *aCache = attr = map->GetNamedItem(aName, aResult);
  return attr;
}


// HTMLCollection helper

nsresult
nsHTMLCollectionSH::GetLength(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                              JSObject *obj, PRUint32 *length)
{
  nsIHTMLCollection* collection =
    static_cast<nsIHTMLCollection*>(GetNative(wrapper, obj));
#ifdef DEBUG
  {
    nsCOMPtr<nsIHTMLCollection> collection_qi =
      do_QueryWrappedNative(wrapper, obj);

    // If this assertion fires the QI implementation for the object in
    // question doesn't use the nsIHTMLCollection pointer as the nsISupports
    // pointer. That must be fixed, or we'll crash...
    NS_ASSERTION(collection_qi == collection, "Uh, fix QI!");
  }
#endif

  return collection->GetLength(length);
}

nsISupports*
nsHTMLCollectionSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                              nsWrapperCache **aCache, nsresult *aResult)
{
  nsIHTMLCollection* collection = static_cast<nsIHTMLCollection*>(aNative);
#ifdef DEBUG
  {
    nsCOMPtr<nsIHTMLCollection> collection_qi = do_QueryInterface(aNative);

    // If this assertion fires the QI implementation for the object in
    // question doesn't use the nsIHTMLCollection pointer as the nsISupports
    // pointer. That must be fixed, or we'll crash...
    NS_ASSERTION(collection_qi == collection, "Uh, fix QI!");
  }
#endif

  nsINode *item;
  *aCache = item = collection->GetNodeAt(aIndex, aResult);
  return item;
}

nsISupports*
nsHTMLCollectionSH::GetNamedItem(nsISupports *aNative,
                                 const nsAString& aName,
                                 nsWrapperCache **aCache,
                                 nsresult *aResult)
{
  nsIHTMLCollection* collection = static_cast<nsIHTMLCollection*>(aNative);
#ifdef DEBUG
  {
    nsCOMPtr<nsIHTMLCollection> collection_qi = do_QueryInterface(aNative);

    // If this assertion fires the QI implementation for the object in
    // question doesn't use the nsIHTMLCollection pointer as the nsISupports
    // pointer. That must be fixed, or we'll crash...
    NS_ASSERTION(collection_qi == collection, "Uh, fix QI!");
  }
#endif

  return collection->GetNamedItem(aName, aCache, aResult);
}


// ContentList helper
nsresult
nsContentListSH::PreCreate(nsISupports *nativeObj, JSContext *cx,
                           JSObject *globalObj, JSObject **parentObj)
{
  nsContentList *contentList = nsContentList::FromSupports(nativeObj);
  nsINode *native_parent = contentList->GetParentObject();

  if (!native_parent) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv =
    WrapNativeParent(cx, globalObj, native_parent, native_parent, parentObj);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_SUCCESS_ALLOW_SLIM_WRAPPERS;
}

nsresult
nsContentListSH::GetLength(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                           JSObject *obj, PRUint32 *length)
{
  nsContentList *list =
    nsContentList::FromSupports(GetNative(wrapper, obj));

  return list->GetLength(length);
}

nsISupports*
nsContentListSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                           nsWrapperCache **aCache, nsresult *aResult)
{
  nsContentList *list = nsContentList::FromSupports(aNative);

  nsIContent *item;
  *aCache = item = list->GetNodeAt(aIndex, aResult);
  return item;
}

nsISupports*
nsContentListSH::GetNamedItem(nsISupports *aNative, const nsAString& aName,
                              nsWrapperCache **aCache, nsresult *aResult)
{
  nsContentList *list = nsContentList::FromSupports(aNative);

  return list->GetNamedItem(aName, aCache, aResult);
}

NS_IMETHODIMP
nsDocumentSH::NewResolve(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                         JSObject *obj, jsid id, PRUint32 flags,
                         JSObject **objp, PRBool *_retval)
{
  nsresult rv;

  if (id == sLocation_id) {
    // Define the location property on the document object itself so
    // that we can intercept getting and setting of document.location.

    nsCOMPtr<nsIDOMNSDocument> doc(do_QueryWrappedNative(wrapper, obj));
    NS_ENSURE_TRUE(doc, NS_ERROR_UNEXPECTED);

    nsCOMPtr<nsIDOMLocation> location;
    rv = doc->GetLocation(getter_AddRefs(location));
    NS_ENSURE_SUCCESS(rv, rv);

    jsval v;

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    rv = WrapNative(cx, obj, location, &NS_GET_IID(nsIDOMLocation), PR_TRUE,
                    &v, getter_AddRefs(holder));
    NS_ENSURE_SUCCESS(rv, rv);

    JSAutoRequest ar(cx);

    JSBool ok = ::JS_DefinePropertyById(cx, obj, id, v, nsnull, nsnull,
                                        JSPROP_PERMANENT | JSPROP_ENUMERATE);

    if (!ok) {
      return NS_ERROR_FAILURE;
    }

    *objp = obj;

    return NS_OK;
  }

  if (id == sDocumentURIObject_id && IsPrivilegedScript()) {
    return DefineVoidProp(cx, obj, id, objp);
  } 

  return nsNodeSH::NewResolve(wrapper, cx, obj, id, flags, objp, _retval);
}

NS_IMETHODIMP
nsDocumentSH::GetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                          JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  if (id == sDocumentURIObject_id && IsPrivilegedScript()) {
    nsCOMPtr<nsIDocument> doc = do_QueryWrappedNative(wrapper);
    NS_ENSURE_TRUE(doc, NS_ERROR_UNEXPECTED);

    nsIURI* uri = doc->GetDocumentURI();
    NS_ENSURE_TRUE(uri, NS_ERROR_NOT_AVAILABLE);

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    nsresult rv = WrapNative(cx, obj, uri, &NS_GET_IID(nsIURI), PR_TRUE, vp,
                             getter_AddRefs(holder));

    return NS_FAILED(rv) ? rv : NS_SUCCESS_I_DID_SOMETHING;
  }

  return nsNodeSH::GetProperty(wrapper, cx, obj, id, vp, _retval);
}

NS_IMETHODIMP
nsDocumentSH::SetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                          JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  if (id == sLocation_id) {
    nsCOMPtr<nsIDOMNSDocument> doc(do_QueryWrappedNative(wrapper));
    NS_ENSURE_TRUE(doc, NS_ERROR_UNEXPECTED);

    nsCOMPtr<nsIDOMLocation> location;

    nsresult rv = doc->GetLocation(getter_AddRefs(location));
    NS_ENSURE_SUCCESS(rv, rv);

    if (location) {
      JSAutoRequest ar(cx);

      JSString *val = ::JS_ValueToString(cx, *vp);
      NS_ENSURE_TRUE(val, NS_ERROR_UNEXPECTED);

      rv = location->SetHref(nsDependentJSString(val));
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
      rv = WrapNative(cx, obj, location, &NS_GET_IID(nsIDOMLocation), PR_TRUE,
                      vp, getter_AddRefs(holder));
      return NS_FAILED(rv) ? rv : NS_SUCCESS_I_DID_SOMETHING;
    }
  }

  if (id == sDocumentURIObject_id && IsPrivilegedScript()) {
    // We don't want privileged script that can read this property to set it,
    // but _do_ want to allow everyone else to set a value they can then read.
    //
    // XXXbz Is there a better error we could use here?
    return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }
  
  return nsNodeSH::SetProperty(wrapper, cx, obj, id, vp, _retval);
}

NS_IMETHODIMP
nsDocumentSH::GetFlags(PRUint32* aFlags)
{
  *aFlags = DOMCLASSINFO_STANDARD_FLAGS;

  return NS_OK;
}

NS_IMETHODIMP
nsDocumentSH::PostCreate(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                         JSObject *obj)
{
  // If this is the current document for the window that's the script global
  // object of this document, then define this document object on the window.
  // That will make sure that the document is referenced (via window.document)
  // and prevent it from going away in GC.
  nsCOMPtr<nsIDocument> doc = do_QueryWrappedNative(wrapper);
  if (!doc) {
    return NS_ERROR_UNEXPECTED;
  }

  nsIScriptGlobalObject *sgo = doc->GetScriptGlobalObject();
  nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface(sgo);
  if (!win) {
    // No window, nothing else to do here
    return NS_OK;
  }

  nsIDOMDocument* currentDoc = win->GetExtantDocument();

  if (SameCOMIdentity(doc, currentDoc)) {
    jsval winVal;

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    nsresult rv = WrapNative(cx, obj, win, &NS_GET_IID(nsIDOMWindow), PR_FALSE,
                             &winVal, getter_AddRefs(holder));
    NS_ENSURE_SUCCESS(rv, rv);

    NS_NAMED_LITERAL_STRING(doc_str, "document");

    if (!::JS_DefineUCProperty(cx, JSVAL_TO_OBJECT(winVal),
                               reinterpret_cast<const jschar *>
                                               (doc_str.get()),
                               doc_str.Length(), OBJECT_TO_JSVAL(obj),
                               JS_PropertyStub, JS_PropertyStub,
                               JSPROP_READONLY | JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }
  }
  return NS_OK;
}

// HTMLDocument helper

static nsresult
ResolveImpl(JSContext *cx, nsIXPConnectWrappedNative *wrapper, jsid id,
            nsISupports **result, nsWrapperCache **aCache)
{
  nsHTMLDocument *doc =
    static_cast<nsHTMLDocument*>(static_cast<nsINode*>(wrapper->Native()));

  // 'id' is not always a string, it can be a number since document.1
  // should map to <input name="1">. Thus we can't use
  // JSVAL_TO_STRING() here.
  JSString *str = IdToString(cx, id);
  NS_ENSURE_TRUE(str, NS_ERROR_UNEXPECTED);

  return doc->ResolveName(nsDependentJSString(str), nsnull, result, aCache);
}

// static
JSBool
nsHTMLDocumentSH::DocumentOpen(JSContext *cx, JSObject *obj, uintN argc,
                               jsval *argv, jsval *rval)
{
  if (argc > 2) {
    JSObject *global = ::JS_GetGlobalForObject(cx, obj);

    // DOM0 quirk that makes document.open() call window.open() if
    // called with 3 or more arguments.

    return ::JS_CallFunctionName(cx, global, "open", argc, argv, rval);
  }

  nsCOMPtr<nsISupports> native = do_QueryWrapper(cx, obj);
  if (!native) {
    nsDOMClassInfo::ThrowJSException(cx, NS_ERROR_FAILURE);

    return JS_FALSE;
  }

  nsCOMPtr<nsIDOMNSHTMLDocument> doc = do_QueryInterface(native);
  NS_ENSURE_TRUE(doc, JS_FALSE);

  nsCAutoString contentType("text/html");
  if (argc > 0) {
    JSString* jsstr = JS_ValueToString(cx, argv[0]);
    if (!jsstr) {
      nsDOMClassInfo::ThrowJSException(cx, NS_ERROR_OUT_OF_MEMORY);
      return JS_FALSE;
    }
    nsAutoString type;
    type.Assign(nsDependentJSString(jsstr));
    ToLowerCase(type);
    nsCAutoString actualType, dummy;
    NS_ParseContentType(NS_ConvertUTF16toUTF8(type), actualType, dummy);
    if (!actualType.EqualsLiteral("text/html") &&
        !type.EqualsLiteral("replace")) {
      contentType = "text/plain";
    }
  }
  
  PRBool replace = PR_FALSE;
  if (argc > 1) {
    JSString* jsstr = JS_ValueToString(cx, argv[1]);
    if (!jsstr) {
      nsDOMClassInfo::ThrowJSException(cx, NS_ERROR_OUT_OF_MEMORY);
      return JS_FALSE;
    }

    replace = NS_LITERAL_STRING("replace").
      Equals(reinterpret_cast<const PRUnichar*>
                             (::JS_GetStringChars(jsstr)));
  }

  nsCOMPtr<nsIDOMDocument> retval;
  nsresult rv = doc->Open(contentType, replace, getter_AddRefs(retval));
  if (NS_FAILED(rv)) {
    nsDOMClassInfo::ThrowJSException(cx, rv);

    return JS_FALSE;
  }

  nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
  rv = WrapNative(cx, obj, retval, PR_FALSE, rval,
                  getter_AddRefs(holder));
  NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to wrap native!");

  return NS_SUCCEEDED(rv);
}


static JSClass sHTMLDocumentAllClass = {
  "HTML document.all class",
  JSCLASS_HAS_PRIVATE | JSCLASS_PRIVATE_IS_NSISUPPORTS | JSCLASS_NEW_RESOLVE |
  JSCLASS_HAS_RESERVED_SLOTS(1),
  JS_PropertyStub, JS_PropertyStub, nsHTMLDocumentSH::DocumentAllGetProperty,
  JS_PropertyStub, JS_EnumerateStub,
  (JSResolveOp)nsHTMLDocumentSH::DocumentAllNewResolve, JS_ConvertStub,
  nsHTMLDocumentSH::ReleaseDocument, nsnull, nsnull,
  nsHTMLDocumentSH::CallToGetPropMapper
};


static JSClass sHTMLDocumentAllHelperClass = {
  "HTML document.all helper class", JSCLASS_HAS_PRIVATE | JSCLASS_NEW_RESOLVE,
  JS_PropertyStub, JS_PropertyStub,
  nsHTMLDocumentSH::DocumentAllHelperGetProperty,
  JS_PropertyStub, JS_EnumerateStub,
  (JSResolveOp)nsHTMLDocumentSH::DocumentAllHelperNewResolve, JS_ConvertStub,
  nsnull
};


static JSClass sHTMLDocumentAllTagsClass = {
  "HTML document.all.tags class",
  JSCLASS_HAS_PRIVATE | JSCLASS_NEW_RESOLVE | JSCLASS_PRIVATE_IS_NSISUPPORTS,
  JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
  JS_EnumerateStub, (JSResolveOp)nsHTMLDocumentSH::DocumentAllTagsNewResolve,
  JS_ConvertStub, nsHTMLDocumentSH::ReleaseDocument, nsnull, nsnull,
  nsHTMLDocumentSH::CallToGetPropMapper
};

// static
JSBool
nsHTMLDocumentSH::GetDocumentAllNodeList(JSContext *cx, JSObject *obj,
                                         nsDocument *domdoc,
                                         nsContentList **nodeList)
{
  // The document.all object is a mix of the node list returned by
  // document.getElementsByTagName("*") and a map of elements in the
  // document exposed by their id and/or name. To make access to the
  // node list part (i.e. access to elements by index) not walk the
  // document each time, we create a nsContentList and hold on to it
  // in a reserved slot (0) on the document.all JSObject.
  jsval collection;
  nsresult rv = NS_OK;

  if (!JS_GetReservedSlot(cx, obj, 0, &collection)) {
    return JS_FALSE;
  }

  if (!JSVAL_IS_PRIMITIVE(collection)) {
    // We already have a node list in our reserved slot, use it.

    nsISupports *native =
      sXPConnect->GetNativeOfWrapper(cx, JSVAL_TO_OBJECT(collection));
    if (native) {
      NS_ADDREF(*nodeList = nsContentList::FromSupports(native));
    }
    else {
      rv = NS_ERROR_FAILURE;
    }
  } else {
    // No node list for this document.all yet, create one...

    nsRefPtr<nsContentList> list =
      domdoc->GetElementsByTagName(NS_LITERAL_STRING("*"));
    if (!list) {
      rv |= NS_ERROR_OUT_OF_MEMORY;
    }

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
    rv |= nsDOMClassInfo::WrapNative(cx, obj, static_cast<nsINodeList*>(list),
                                     list, PR_FALSE, &collection,
                                     getter_AddRefs(holder));

    list.forget(nodeList);

    // ... and store it in our reserved slot.
    if (!JS_SetReservedSlot(cx, obj, 0, collection)) {
      return JS_FALSE;
    }
  }

  if (NS_FAILED(rv)) {
    nsDOMClassInfo::ThrowJSException(cx, rv);

    return JS_FALSE;
  }

  return *nodeList != nsnull;
}

JSBool
nsHTMLDocumentSH::DocumentAllGetProperty(JSContext *cx, JSObject *obj,
                                         jsid id, jsval *vp)
{
  // document.all.item and .namedItem get their value in the
  // newResolve hook, so nothing to do for those properties here. And
  // we need to return early to prevent <div id="item"> from shadowing
  // document.all.item(), etc.
  if (id == sItem_id || id == sNamedItem_id) {
    return JS_TRUE;
  }

  while (obj->getJSClass() != &sHTMLDocumentAllClass) {
    obj = obj->getProto();

    if (!obj) {
      NS_ERROR("The JS engine lies!");

      return JS_TRUE;
    }
  }

  nsHTMLDocument *doc = GetDocument(cx, obj);
  nsISupports *result;
  nsWrapperCache *cache;
  nsresult rv = NS_OK;

  if (JSID_IS_STRING(id)) {
    if (id == sLength_id) {
      // Map document.all.length to the length of the collection
      // document.getElementsByTagName("*"), and make sure <div
      // id="length"> doesn't shadow document.all.length.

      nsRefPtr<nsContentList> nodeList;
      if (!GetDocumentAllNodeList(cx, obj, doc, getter_AddRefs(nodeList))) {
        return JS_FALSE;
      }

      PRUint32 length;
      rv = nodeList->GetLength(&length);

      if (NS_FAILED(rv)) {
        nsDOMClassInfo::ThrowJSException(cx, rv);

        return JS_FALSE;
      }

      *vp = INT_TO_JSVAL(length);

      return JS_TRUE;
    } else if (id != sTags_id) {
      // For all other strings, look for an element by id or name.

      nsDependentJSString str(id);

      result = doc->GetDocumentAllResult(str, &cache, &rv);

      if (NS_FAILED(rv)) {
        nsDOMClassInfo::ThrowJSException(cx, rv);

        return JS_FALSE;
      }
    }
    else {
      result = nsnull;
    }
  } else if (JSID_IS_INT(id) && JSID_TO_INT(id) >= 0) {
    // Map document.all[n] (where n is a number) to the n:th item in
    // the document.all node list.

    nsRefPtr<nsContentList> nodeList;
    if (!GetDocumentAllNodeList(cx, obj, doc, getter_AddRefs(nodeList))) {
      return JS_FALSE;
    }

    nsIContent *node = nodeList->GetNodeAt(JSID_TO_INT(id));

    result = node;
    cache = node;
  }

  if (result) {
    rv = WrapNative(cx, obj, result, cache, PR_TRUE, vp);
    if (NS_FAILED(rv)) {
      nsDOMClassInfo::ThrowJSException(cx, rv);

      return JS_FALSE;
    }
  } else {
    *vp = JSVAL_VOID;
  }

  return JS_TRUE;
}

JSBool
nsHTMLDocumentSH::DocumentAllNewResolve(JSContext *cx, JSObject *obj, jsid id,
                                        uintN flags, JSObject **objp)
{
  if (flags & JSRESOLVE_ASSIGNING) {
    // Nothing to do here if we're assigning

    return JS_TRUE;
  }

  jsval v = JSVAL_VOID;

  if (id == sItem_id || id == sNamedItem_id) {
    // Define the item() or namedItem() method.

    JSFunction *fnc =
      ::JS_DefineFunction(cx, obj, ::JS_GetStringBytes(JSID_TO_STRING(id)),
                          CallToGetPropMapper, 0, JSPROP_ENUMERATE);

    *objp = obj;

    return fnc != nsnull;
  }

  if (id == sLength_id) {
    // document.all.length. Any jsval other than undefined would do
    // here, all we need is to get into the code below that defines
    // this propery on obj, the rest happens in
    // DocumentAllGetProperty().

    v = JSVAL_ONE;
  } else if (id == sTags_id) {
    nsHTMLDocument *doc = GetDocument(cx, obj);

    JSObject *tags = ::JS_NewObject(cx, &sHTMLDocumentAllTagsClass, nsnull,
                                    ::JS_GetGlobalForObject(cx, obj));
    if (!tags) {
      return JS_FALSE;
    }

    if (!::JS_SetPrivate(cx, tags, doc)) {
      return JS_FALSE;
    }

    // The "tags" JSObject now also owns doc.
    NS_ADDREF(doc);

    v = OBJECT_TO_JSVAL(tags);
  } else {
    if (!DocumentAllGetProperty(cx, obj, id, &v)) {
      return JS_FALSE;
    }
  }

  JSBool ok = JS_TRUE;

  if (v != JSVAL_VOID) {
    if (JSID_IS_STRING(id)) {
      ok = ::JS_DefinePropertyById(cx, obj, id, v, nsnull, nsnull, 0);
    } else {
      ok = ::JS_DefineElement(cx, obj, JSID_TO_INT(id), v, nsnull, nsnull, 0);
    }

    *objp = obj;
  }

  return ok;
}

// Finalize hook used by document related JS objects, but also by
// sGlobalScopePolluterClass!

void
nsHTMLDocumentSH::ReleaseDocument(JSContext *cx, JSObject *obj)
{
  nsIHTMLDocument *doc = (nsIHTMLDocument *)::JS_GetPrivate(cx, obj);

  NS_IF_RELEASE(doc);
}

JSBool
nsHTMLDocumentSH::CallToGetPropMapper(JSContext *cx, JSObject *obj, uintN argc,
                                      jsval *argv, jsval *rval)
{
  // Handle document.all("foo") style access to document.all.

  if (argc != 1) {
    // XXX: Should throw NS_ERROR_XPC_NOT_ENOUGH_ARGS for argc < 1,
    // and create a new NS_ERROR_XPC_TOO_MANY_ARGS for argc > 1? IE
    // accepts nothing other than one arg.
    nsDOMClassInfo::ThrowJSException(cx, NS_ERROR_INVALID_ARG);

    return JS_FALSE;
  }

  // Convert all types to string.
  JSString *str = ::JS_ValueToString(cx, argv[0]);
  if (!str) {
    return JS_FALSE;
  }

  JSObject *self;

  if (::JS_TypeOfValue(cx, argv[-2]) == JSTYPE_FUNCTION) {
    // If argv[-2] is a function, we're called through
    // document.all.item() or something similar. In such a case, self
    // is passed as obj.

    self = obj;
  } else {
    // In other cases (i.e. document.all("foo")), self is passed as
    // argv[-2].

    self = JSVAL_TO_OBJECT(argv[-2]);
  }

  return ::JS_GetUCProperty(cx, self, ::JS_GetStringChars(str),
                            ::JS_GetStringLength(str), rval);
}


static inline JSObject *
GetDocumentAllHelper(JSContext *cx, JSObject *obj)
{
  while (obj && JS_GET_CLASS(cx, obj) != &sHTMLDocumentAllHelperClass) {
    obj = ::JS_GetPrototype(cx, obj);
  }

  return obj;
}

static inline void *
FlagsToPrivate(PRUint32 flags)
{
  JS_ASSERT((flags & (1 << 31)) == 0);
  return (void *)(flags << 1);
}

static inline PRUint32
PrivateToFlags(void *priv)
{
  JS_ASSERT(size_t(priv) <= PR_UINT32_MAX && (size_t(priv) & 1) == 0);
  return (PRUint32)(size_t(priv) >> 1);
}

JSBool
nsHTMLDocumentSH::DocumentAllHelperGetProperty(JSContext *cx, JSObject *obj,
                                               jsid id, jsval *vp)
{
  if (id != nsDOMClassInfo::sAll_id) {
    return JS_TRUE;
  }

  JSObject *helper = GetDocumentAllHelper(cx, obj);

  if (!helper) {
    NS_ERROR("Uh, how'd we get here?");

    // Let scripts continue, if we somehow did get here...

    return JS_TRUE;
  }

  PRUint32 flags = PrivateToFlags(::JS_GetPrivate(cx, helper));

  if (flags & JSRESOLVE_DETECTING || !(flags & JSRESOLVE_QUALIFIED)) {
    // document.all is either being detected, e.g. if (document.all),
    // or it was not being resolved with a qualified name. Claim that
    // document.all is undefined.

    *vp = JSVAL_VOID;
  } else {
    // document.all is not being detected, and it resolved with a
    // qualified name. Expose the document.all collection.

    if (!JSVAL_IS_OBJECT(*vp)) {
      // First time through, create the collection, and set the
      // document as its private nsISupports data.
      nsresult rv;
      nsCOMPtr<nsIHTMLDocument> doc = do_QueryWrapper(cx, obj, &rv);
      if (NS_FAILED(rv)) {
        nsDOMClassInfo::ThrowJSException(cx, rv);

        return JS_FALSE;
      }

      JSObject *all = ::JS_NewObject(cx, &sHTMLDocumentAllClass, nsnull,
                                     ::JS_GetGlobalForObject(cx, obj));
      if (!all) {
        return JS_FALSE;
      }

      // Let the JSObject take over ownership of doc.
      if (!::JS_SetPrivate(cx, all, doc)) {
        return JS_FALSE;
      }

      doc.forget();

      *vp = OBJECT_TO_JSVAL(all);
    }
  }

  return JS_TRUE;
}

JSBool
nsHTMLDocumentSH::DocumentAllHelperNewResolve(JSContext *cx, JSObject *obj,
                                              jsid id, uintN flags,
                                              JSObject **objp)
{
  if (id == nsDOMClassInfo::sAll_id) {
    // document.all is resolved for the first time. Define it.
    JSObject *helper = GetDocumentAllHelper(cx, obj);

    if (helper) {
      if (!::JS_DefineProperty(cx, helper, "all", JSVAL_VOID, nsnull, nsnull,
                               JSPROP_ENUMERATE)) {
        return JS_FALSE;
      }

      *objp = helper;
    }
  }

  return JS_TRUE;
}


JSBool
nsHTMLDocumentSH::DocumentAllTagsNewResolve(JSContext *cx, JSObject *obj,
                                            jsid id, uintN flags,
                                            JSObject **objp)
{
  if (JSID_IS_STRING(id)) {
    nsDocument *doc = GetDocument(cx, obj);

    JSString *str = JSID_TO_STRING(id);

    JSObject *proto = ::JS_GetPrototype(cx, obj);
    if (NS_UNLIKELY(!proto)) {
      return JS_TRUE;
    }

    JSBool found;
    if (!::JS_HasPropertyById(cx, proto, id, &found)) {
      return JS_FALSE;
    }

    if (found) {
      return JS_TRUE;
    }

    nsRefPtr<nsContentList> tags =
      doc->GetElementsByTagName(nsDependentJSString(str));

    if (tags) {
      jsval v;
      nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
      nsresult rv = nsDOMClassInfo::WrapNative(cx, obj,
                                               static_cast<nsINodeList*>(tags),
                                               tags, PR_TRUE, &v,
                                               getter_AddRefs(holder));
      if (NS_FAILED(rv)) {
        nsDOMClassInfo::ThrowJSException(cx, rv);

        return JS_FALSE;
      }

      if (!::JS_DefinePropertyById(cx, obj, id, v, nsnull, nsnull, 0)) {
        return JS_FALSE;
      }

      *objp = obj;
    }
  }

  return JS_TRUE;
}


NS_IMETHODIMP
nsHTMLDocumentSH::NewResolve(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                             JSObject *obj, jsid id, PRUint32 flags,
                             JSObject **objp, PRBool *_retval)
{
  // nsDocumentSH::NewResolve() does a security check that we'd kinda
  // want to do here too before doing anything else. But given that we
  // only define dynamic properties here before the call to
  // nsDocumentSH::NewResolve() we're ok, since once those properties
  // are accessed, we'll do the necessary security check.

  if (!(flags & JSRESOLVE_ASSIGNING)) {
    // For native wrappers, do not resolve random names on document

    JSAutoRequest ar(cx);

    if (!ObjectIsNativeWrapper(cx, obj)) {
      nsCOMPtr<nsISupports> result;
      nsWrapperCache *cache;
      nsresult rv = ResolveImpl(cx, wrapper, id, getter_AddRefs(result),
                                &cache);
      NS_ENSURE_SUCCESS(rv, rv);

      if (result) {
        JSBool ok = *_retval =
          ::JS_DefinePropertyById(cx, obj, id, JSVAL_VOID, nsnull, nsnull, 0);
        *objp = obj;

        return ok ? NS_OK : NS_ERROR_FAILURE;
      }
    }

    if (id == sOpen_id) {
      JSString *str = JSID_TO_STRING(id);
      JSFunction *fnc =
        ::JS_DefineFunction(cx, obj, ::JS_GetStringBytes(str),
                            DocumentOpen, 0, JSPROP_ENUMERATE);

      *objp = obj;

      return fnc ? NS_OK : NS_ERROR_UNEXPECTED;
    }

    if (id == sAll_id && !sDisableDocumentAllSupport &&
        !ObjectIsNativeWrapper(cx, obj)) {
      nsIDocument *doc = static_cast<nsIDocument*>(wrapper->Native());

      if (doc->GetCompatibilityMode() == eCompatibility_NavQuirks) {
        JSObject *helper =
          GetDocumentAllHelper(cx, ::JS_GetPrototype(cx, obj));

        JSObject *proto = ::JS_GetPrototype(cx, helper ? helper : obj);

        // Check if the property all is defined on obj's (or helper's
        // if obj doesn't exist) prototype, if it is, don't expose our
        // document.all helper.

        JSBool hasAll = JS_FALSE;
        if (proto && !JS_HasProperty(cx, proto, "all", &hasAll)) {
          return NS_ERROR_UNEXPECTED;
        }

        if (hasAll && helper) {
          // Our helper's prototype now has an "all" property, remove
          // the helper out of the prototype chain to prevent
          // shadowing of the now defined "all" property.
          JSObject *tmp = obj, *tmpProto;

          while ((tmpProto = ::JS_GetPrototype(cx, tmp)) != helper) {
            tmp = tmpProto;
          }

          ::JS_SetPrototype(cx, tmp, proto);
        }

        // If we don't already have a helper, and we're resolving
        // document.all qualified, and we're *not* detecting
        // document.all, e.g. if (document.all), and "all" isn't
        // already defined on our prototype, create a helper.
        if (!helper && flags & JSRESOLVE_QUALIFIED &&
            !(flags & JSRESOLVE_DETECTING) && !hasAll) {
          // Print a warning so developers can stop using document.all
          PrintWarningOnConsole(cx, "DocumentAllUsed");

          helper = ::JS_NewObject(cx, &sHTMLDocumentAllHelperClass,
                                  ::JS_GetPrototype(cx, obj),
                                  ::JS_GetGlobalForObject(cx, obj));

          if (!helper) {
            return NS_ERROR_OUT_OF_MEMORY;
          }

          // Insert the helper into our prototype chain. helper's prototype
          // is already obj's current prototype.
          if (!::JS_SetPrototype(cx, obj, helper)) {
            nsDOMClassInfo::ThrowJSException(cx, NS_ERROR_UNEXPECTED);

            return NS_ERROR_UNEXPECTED;
          }
        }

        // If we have (or just created) a helper, pass the resolve flags
        // to the helper as its private data.
        if (helper && !::JS_SetPrivate(cx, helper, FlagsToPrivate(flags))) {
          nsDOMClassInfo::ThrowJSException(cx, NS_ERROR_UNEXPECTED);

          return NS_ERROR_UNEXPECTED;
        }
      }

      return NS_OK;
    }
  }

  return nsDocumentSH::NewResolve(wrapper, cx, obj, id, flags, objp, _retval);
}

NS_IMETHODIMP
nsHTMLDocumentSH::GetProperty(nsIXPConnectWrappedNative *wrapper,
                              JSContext *cx, JSObject *obj, jsid id,
                              jsval *vp, PRBool *_retval)
{
  // For native wrappers, do not get random names on document
  if (!ObjectIsNativeWrapper(cx, obj)) {
    nsCOMPtr<nsISupports> result;

    JSAutoRequest ar(cx);

    nsWrapperCache *cache;
    nsresult rv = ResolveImpl(cx, wrapper, id, getter_AddRefs(result), &cache);
    NS_ENSURE_SUCCESS(rv, rv);

    if (result) {
      rv = WrapNative(cx, obj, result, cache, PR_TRUE, vp);
      if (NS_SUCCEEDED(rv)) {
        rv = NS_SUCCESS_I_DID_SOMETHING;
      }
      return rv;
    }
  }

  return nsDocumentSH::GetProperty(wrapper, cx, obj, id, vp, _retval);
}

// HTMLFormElement helper

// static
nsresult
nsHTMLFormElementSH::FindNamedItem(nsIForm *aForm, JSString *str,
                                   nsISupports **aResult,
                                   nsWrapperCache **aCache)
{
  nsDependentJSString name(str);

  *aResult = aForm->ResolveName(name).get();
  // FIXME Get the wrapper cache from nsIForm::ResolveName
  *aCache = nsnull;

  if (!*aResult) {
    nsCOMPtr<nsIContent> content(do_QueryInterface(aForm));
    nsCOMPtr<nsIDOMHTMLFormElement> form_element(do_QueryInterface(aForm));

    nsCOMPtr<nsIHTMLDocument> html_doc =
      do_QueryInterface(content->GetDocument());

    if (html_doc && form_element) {
      html_doc->ResolveName(name, form_element, aResult, aCache);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElementSH::NewResolve(nsIXPConnectWrappedNative *wrapper,
                                JSContext *cx, JSObject *obj, jsid id,
                                PRUint32 flags, JSObject **objp,
                                PRBool *_retval)
{
  // For native wrappers, do not resolve random names on form
  if ((!(JSRESOLVE_ASSIGNING & flags)) && JSID_IS_STRING(id) &&
      !ObjectIsNativeWrapper(cx, obj)) {
    nsCOMPtr<nsIForm> form(do_QueryWrappedNative(wrapper, obj));
    nsCOMPtr<nsISupports> result;
    nsWrapperCache *cache;

    JSString *str = JSID_TO_STRING(id);
    FindNamedItem(form, str, getter_AddRefs(result), &cache);

    if (result) {
      JSAutoRequest ar(cx);
      *_retval = ::JS_DefinePropertyById(cx, obj, id, JSVAL_VOID, nsnull,
                                         nsnull, JSPROP_ENUMERATE);

      *objp = obj;

      return *_retval ? NS_OK : NS_ERROR_FAILURE;
    }
  }

  return nsElementSH::NewResolve(wrapper, cx, obj, id, flags, objp, _retval);
}


NS_IMETHODIMP
nsHTMLFormElementSH::GetProperty(nsIXPConnectWrappedNative *wrapper,
                                 JSContext *cx, JSObject *obj, jsid id,
                                 jsval *vp, PRBool *_retval)
{
  nsCOMPtr<nsIForm> form(do_QueryWrappedNative(wrapper, obj));

  if (JSID_IS_STRING(id)) {
    // For native wrappers, do not get random names on form
    if (!ObjectIsNativeWrapper(cx, obj)) {
      nsCOMPtr<nsISupports> result;
      nsWrapperCache *cache;

      JSString *str = JSID_TO_STRING(id);
      FindNamedItem(form, str, getter_AddRefs(result), &cache);

      if (result) {
        // Wrap result, result can be either an element or a list of
        // elements
        nsresult rv = WrapNative(cx, obj, result, cache, PR_TRUE, vp);
        return NS_FAILED(rv) ? rv : NS_SUCCESS_I_DID_SOMETHING;
      }
    }
  } else {
    PRInt32 n = GetArrayIndexFromId(cx, id);

    if (n >= 0) {
      nsIFormControl* control = form->GetElementAt(n);

      if (control) {
        Element *element =
          static_cast<nsGenericHTMLFormElement*>(form->GetElementAt(n));
        nsresult rv = WrapNative(cx, obj, element, element, PR_TRUE, vp);
        return NS_FAILED(rv) ? rv : NS_SUCCESS_I_DID_SOMETHING;
      }
    }
  }

  return nsElementSH::GetProperty(wrapper, cx, obj, id, vp, _retval);;
}

NS_IMETHODIMP
nsHTMLFormElementSH::NewEnumerate(nsIXPConnectWrappedNative *wrapper,
                                  JSContext *cx, JSObject *obj,
                                  PRUint32 enum_op, jsval *statep,
                                  jsid *idp, PRBool *_retval)
{
  switch (enum_op) {
  case JSENUMERATE_INIT:
  case JSENUMERATE_INIT_ALL:
    {
      nsCOMPtr<nsIForm> form(do_QueryWrappedNative(wrapper, obj));

      if (!form) {
        *statep = JSVAL_NULL;
        return NS_ERROR_UNEXPECTED;
      }

      *statep = INT_TO_JSVAL(0);

      if (idp) {
        PRUint32 count = form->GetElementCount();

        *idp = INT_TO_JSID(count);
      }

      break;
    }
  case JSENUMERATE_NEXT:
    {
      nsCOMPtr<nsIForm> form(do_QueryWrappedNative(wrapper, obj));
      NS_ENSURE_TRUE(form, NS_ERROR_FAILURE);

      PRInt32 index = (PRInt32)JSVAL_TO_INT(*statep);

      PRUint32 count = form->GetElementCount();

      if ((PRUint32)index < count) {
        nsIFormControl* controlNode = form->GetElementAt(index);
        NS_ENSURE_TRUE(controlNode, NS_ERROR_FAILURE);

        nsCOMPtr<nsIDOMElement> domElement = do_QueryInterface(controlNode);
        NS_ENSURE_TRUE(domElement, NS_ERROR_FAILURE);

        nsAutoString attr;
        domElement->GetAttribute(NS_LITERAL_STRING("name"), attr);
        if (attr.IsEmpty()) {
          // If name is not there, use index instead
          attr.AppendInt(index);
        }

        JSAutoRequest ar(cx);

        JSString *jsname =
          JS_NewUCStringCopyN(cx, reinterpret_cast<const jschar *>
                                                  (attr.get()),
                              attr.Length());
        NS_ENSURE_TRUE(jsname, NS_ERROR_OUT_OF_MEMORY);

        JS_ValueToId(cx, STRING_TO_JSVAL(jsname), idp);

        *statep = INT_TO_JSVAL(++index);
      } else {
        *statep = JSVAL_NULL;
      }

      break;
    }
  case JSENUMERATE_DESTROY:
    *statep = JSVAL_NULL;

    break;
  }

  return NS_OK;
}


// HTMLSelectElement helper

NS_IMETHODIMP
nsHTMLSelectElementSH::GetProperty(nsIXPConnectWrappedNative *wrapper,
                                   JSContext *cx, JSObject *obj, jsid id,
                                   jsval *vp, PRBool *_retval)
{
  PRInt32 n = GetArrayIndexFromId(cx, id);

  nsresult rv = NS_OK;
  if (n >= 0) {
    nsHTMLSelectElement *s =
      nsHTMLSelectElement::FromSupports(GetNative(wrapper, obj));

    nsHTMLOptionCollection *options = s->GetOptions();

    if (options) {
      nsISupports *node = options->GetNodeAt(n, &rv);

      rv = WrapNative(cx, obj, node, &NS_GET_IID(nsIDOMNode), PR_TRUE, vp);
      if (NS_SUCCEEDED(rv)) {
        rv = NS_SUCCESS_I_DID_SOMETHING;
      }
      return rv;
    }
  }

  return nsElementSH::GetProperty(wrapper, cx, obj, id, vp, _retval);;
}

// static
nsresult
nsHTMLSelectElementSH::SetOption(JSContext *cx, jsval *vp, PRUint32 aIndex,
                                 nsIDOMNSHTMLOptionCollection *aOptCollection)
{
  JSAutoRequest ar(cx);

  // vp must refer to an object
  if (!JSVAL_IS_OBJECT(*vp) && !::JS_ConvertValue(cx, *vp, JSTYPE_OBJECT, vp)) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIDOMHTMLOptionElement> new_option;

  if (!JSVAL_IS_NULL(*vp)) {
    new_option = do_QueryWrapper(cx, JSVAL_TO_OBJECT(*vp));
    if (!new_option) {
      // Someone is trying to set an option to a non-option object.

      return NS_ERROR_UNEXPECTED;
    }
  }

  return aOptCollection->SetOption(aIndex, new_option);
}

NS_IMETHODIMP
nsHTMLSelectElementSH::SetProperty(nsIXPConnectWrappedNative *wrapper,
                                   JSContext *cx, JSObject *obj, jsid id,
                                   jsval *vp, PRBool *_retval)
{
  PRInt32 n = GetArrayIndexFromId(cx, id);

  if (n >= 0) {
    nsCOMPtr<nsIDOMHTMLSelectElement> select =
      do_QueryWrappedNative(wrapper, obj);
    NS_ENSURE_TRUE(select, NS_ERROR_UNEXPECTED);

    nsCOMPtr<nsIDOMHTMLOptionsCollection> options;
    select->GetOptions(getter_AddRefs(options));

    nsCOMPtr<nsIDOMNSHTMLOptionCollection> oc(do_QueryInterface(options));
    NS_ENSURE_TRUE(oc, NS_ERROR_UNEXPECTED);

    nsresult rv = SetOption(cx, vp, n, oc);
    return NS_FAILED(rv) ? rv : NS_SUCCESS_I_DID_SOMETHING;
  }

  return nsElementSH::SetProperty(wrapper, cx, obj, id, vp, _retval);
}


// HTMLObject/EmbedElement helper

// static
nsresult
nsHTMLPluginObjElementSH::GetPluginInstanceIfSafe(nsIXPConnectWrappedNative *wrapper,
                                                  JSObject *obj,
                                                  nsIPluginInstance **_result)
{
  *_result = nsnull;

  nsCOMPtr<nsIContent> content(do_QueryWrappedNative(wrapper, obj));
  NS_ENSURE_TRUE(content, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIObjectLoadingContent> objlc(do_QueryInterface(content));
  NS_ASSERTION(objlc, "Object nodes must implement nsIObjectLoadingContent");

  // If it's not safe to run script we'll only return the instance if it
  // exists.
  if (!nsContentUtils::IsSafeToRunScript()) {
    return objlc->GetPluginInstance(_result);
  }

  // Make sure that there is a plugin
  return objlc->EnsureInstantiation(_result);
}

// Check if proto is already in obj's prototype chain.

static PRBool
IsObjInProtoChain(JSContext *cx, JSObject *obj, JSObject *proto)
{
  JSObject *o = obj;

  JSAutoRequest ar(cx);

  while (o) {
    JSObject *p = ::JS_GetPrototype(cx, o);

    if (p == proto) {
      return PR_TRUE;
    }

    o = p;
  }

  return PR_FALSE;
}

class nsPluginProtoChainInstallRunner : public nsIRunnable
{
public:
  NS_DECL_ISUPPORTS

  nsPluginProtoChainInstallRunner(nsIXPConnectWrappedNative* wrapper,
                                  nsIScriptContext* scriptContext)
    : mWrapper(wrapper),
      mContext(scriptContext)
  {
  }

  NS_IMETHOD Run()
  {
    JSContext* cx = nsnull;
    if (mContext) {
      cx = (JSContext*)mContext->GetNativeContext();
    } else {
      nsCOMPtr<nsIThreadJSContextStack> stack =
        do_GetService("@mozilla.org/js/xpc/ContextStack;1");
      NS_ENSURE_TRUE(stack, NS_OK);

      stack->GetSafeJSContext(&cx);
      NS_ENSURE_TRUE(cx, NS_OK);
    }

    JSObject* obj = nsnull;
    mWrapper->GetJSObject(&obj);
    NS_ASSERTION(obj, "Should never be null");
    nsHTMLPluginObjElementSH::SetupProtoChain(mWrapper, cx, obj);
    return NS_OK;
  }

private:
  nsCOMPtr<nsIXPConnectWrappedNative> mWrapper;
  nsCOMPtr<nsIScriptContext> mContext;
};

NS_IMPL_ISUPPORTS1(nsPluginProtoChainInstallRunner, nsIRunnable)

// static
nsresult
nsHTMLPluginObjElementSH::SetupProtoChain(nsIXPConnectWrappedNative *wrapper,
                                          JSContext *cx,
                                          JSObject *obj)
{
  NS_ASSERTION(nsContentUtils::IsSafeToRunScript(),
               "Shouldn't have gotten in here");

  nsCxPusher cxPusher;
  if (!cxPusher.Push(cx)) {
    return NS_OK;
  }

  nsCOMPtr<nsIPluginInstance> pi;
  nsresult rv = GetPluginInstanceIfSafe(wrapper, obj, getter_AddRefs(pi));
  NS_ENSURE_SUCCESS(rv, rv);

  if (!pi) {
    // No plugin around for this object.

    return NS_OK;
  }

  JSObject *pi_obj = nsnull; // XPConnect-wrapped peer object, when we get it.
  JSObject *pi_proto = nsnull; // 'pi.__proto__'

  rv = GetPluginJSObject(cx, obj, pi, &pi_obj, &pi_proto);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!pi_obj) {
    // Didn't get a plugin instance JSObject, nothing we can do then.

    return NS_OK;
  }

  if (IsObjInProtoChain(cx, obj, pi_obj)) {
    // We must have re-entered ::PostCreate() from nsObjectFrame()
    // (through the EnsureInstantiation() call in
    // GetPluginInstanceIfSafe()), this means that we've already done what
    // we're about to do in this function so we can just return here.

    return NS_OK;
  }


  // If we got an xpconnect-wrapped plugin object, set obj's
  // prototype's prototype to the scriptable plugin.

  JSObject *my_proto = nsnull;

  // Get 'this.__proto__'
  rv = wrapper->GetJSObjectPrototype(&my_proto);
  NS_ENSURE_SUCCESS(rv, rv);

  JSAutoRequest ar(cx);

  // Set 'this.__proto__' to pi
  if (!::JS_SetPrototype(cx, obj, pi_obj)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (pi_proto && JS_GET_CLASS(cx, pi_proto) != sObjectClass) {
    // The plugin wrapper has a proto that's not Object.prototype, set
    // 'pi.__proto__.__proto__' to the original 'this.__proto__'
    if (pi_proto != my_proto && !::JS_SetPrototype(cx, pi_proto, my_proto)) {
      return NS_ERROR_UNEXPECTED;
    }
  } else {
    // 'pi' didn't have a prototype, or pi's proto was
    // 'Object.prototype' (i.e. pi is an NPRuntime wrapped JS object)
    // set 'pi.__proto__' to the original 'this.__proto__'
    if (!::JS_SetPrototype(cx, pi_obj, my_proto)) {
      return NS_ERROR_UNEXPECTED;
    }
  }

  // Before this proto dance the objects involved looked like this:
  //
  // this.__proto__.__proto__
  //   ^      ^         ^
  //   |      |         |__ Object.prototype
  //   |      |
  //   |      |__ xpc embed wrapper proto (shared)
  //   |
  //   |__ xpc wrapped native embed node
  //
  // pi.__proto__
  // ^      ^
  // |      |__ Object.prototype
  // |
  // |__ Plugin NPRuntime JS object wrapper
  //
  // Now, after the above prototype setup the prototype chain should
  // look like this:
  //
  // this.__proto__.__proto__.__proto__
  //   ^      ^         ^         ^
  //   |      |         |         |__ Object.prototype
  //   |      |         |
  //   |      |         |__ xpc embed wrapper proto (shared)
  //   |      |
  //   |      |__ Plugin NPRuntime JS object wrapper
  //   |
  //   |__ xpc wrapped native embed node
  //

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLPluginObjElementSH::PreCreate(nsISupports *nativeObj, JSContext *cx,
                                    JSObject *globalObj, JSObject **parentObj)
{
  nsresult rv = nsElementSH::PreCreate(nativeObj, cx, globalObj, parentObj);

  // For now we don't support slim wrappers for plugins.
  return rv == NS_SUCCESS_ALLOW_SLIM_WRAPPERS ? NS_OK : rv;
}

NS_IMETHODIMP
nsHTMLPluginObjElementSH::PostCreate(nsIXPConnectWrappedNative *wrapper,
                                     JSContext *cx, JSObject *obj)
{
  if (nsContentUtils::IsSafeToRunScript()) {
#ifdef DEBUG
    nsresult rv =
#endif
      SetupProtoChain(wrapper, cx, obj);

    // If SetupProtoChain failed then we're in real trouble. We're about to fail
    // PostCreate but it's more than likely that we handed our (now invalid)
    // wrapper to someone already. Bug 429442 is an example of the kind of crash
    // that can result from such a situation. We'll return NS_OK for the time
    // being and hope for the best.
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "SetupProtoChain failed!");
    return NS_OK;
  }

  // This may be null if the JS context is not a DOM context. That's ok, we'll
  // use the safe context from XPConnect in the runnable.
  nsCOMPtr<nsIScriptContext> scriptContext = GetScriptContextFromJSContext(cx);

  nsRefPtr<nsPluginProtoChainInstallRunner> runner =
    new nsPluginProtoChainInstallRunner(wrapper, scriptContext);
  nsContentUtils::AddScriptRunner(runner);

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLPluginObjElementSH::GetProperty(nsIXPConnectWrappedNative *wrapper,
                                      JSContext *cx, JSObject *obj, jsid id,
                                      jsval *vp, PRBool *_retval)
{
  JSAutoRequest ar(cx);

  JSObject *pi_obj = ::JS_GetPrototype(cx, obj);
  if (NS_UNLIKELY(!pi_obj)) {
    return NS_OK;
  }

  JSBool found = PR_FALSE;

  if (!ObjectIsNativeWrapper(cx, obj)) {
    if (JSID_IS_STRING(id)) {
      *_retval = ::JS_HasPropertyById(cx, pi_obj, id, &found);
    } else {
      *_retval = ::JS_HasElement(cx, pi_obj, JSID_TO_INT(id), &found);
    }

    if (!*_retval) {
      return NS_ERROR_UNEXPECTED;
    }
  }

  if (found) {
    if (JSID_IS_STRING(id)) {
      *_retval = ::JS_GetPropertyById(cx, pi_obj, id, vp);
    } else {
      *_retval = ::JS_GetElement(cx, pi_obj, JSID_TO_INT(id), vp);
    }

    return *_retval ? NS_SUCCESS_I_DID_SOMETHING : NS_ERROR_FAILURE;
  }

  return nsElementSH::GetProperty(wrapper, cx, obj, id, vp, _retval);
}

NS_IMETHODIMP
nsHTMLPluginObjElementSH::SetProperty(nsIXPConnectWrappedNative *wrapper,
                                      JSContext *cx, JSObject *obj, jsid id,
                                      jsval *vp, PRBool *_retval)
{
  JSAutoRequest ar(cx);

  JSObject *pi_obj = ::JS_GetPrototype(cx, obj);
  if (NS_UNLIKELY(!pi_obj)) {
    return NS_OK;
  }

  JSBool found = PR_FALSE;

  if (!ObjectIsNativeWrapper(cx, obj)) {
    if (JSID_IS_STRING(id)) {
      *_retval = ::JS_HasPropertyById(cx, pi_obj, id, &found);
    } else {
      *_retval = ::JS_HasElement(cx, pi_obj, JSID_TO_INT(id), &found);
    }

    if (!*_retval) {
      return NS_ERROR_UNEXPECTED;
    }
  }

  if (found) {
    if (JSID_IS_STRING(id)) {
      *_retval = ::JS_SetPropertyById(cx, pi_obj, id, vp);
    } else {
      *_retval = ::JS_SetElement(cx, pi_obj, JSID_TO_INT(id), vp);
    }

    return *_retval ? NS_SUCCESS_I_DID_SOMETHING : NS_ERROR_FAILURE;
  }

  return nsElementSH::SetProperty(wrapper, cx, obj, id, vp, _retval);
}

NS_IMETHODIMP
nsHTMLPluginObjElementSH::Call(nsIXPConnectWrappedNative *wrapper,
                               JSContext *cx, JSObject *obj, PRUint32 argc,
                               jsval *argv, jsval *vp, PRBool *_retval)
{
  nsCOMPtr<nsIPluginInstance> pi;
  nsresult rv = GetPluginInstanceIfSafe(wrapper, obj, getter_AddRefs(pi));
  NS_ENSURE_SUCCESS(rv, rv);

  // If obj is a native wrapper, or if there's no plugin around for
  // this object, throw.
  if (ObjectIsNativeWrapper(cx, obj) || !pi) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  JSObject *pi_obj = nsnull;
  JSObject *pi_proto = nsnull;

  rv = GetPluginJSObject(cx, obj, pi, &pi_obj, &pi_proto);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!pi) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // XPConnect passes us the XPConnect wrapper JSObject as obj, and
  // not the 'this' parameter that the JS engine passes in. Pass in
  // the real this parameter from JS (argv[-1]) here.
  JSAutoRequest ar(cx);
  *_retval = ::JS_CallFunctionValue(cx, JSVAL_TO_OBJECT(argv[-1]),
                                    OBJECT_TO_JSVAL(pi_obj), argc, argv, vp);

  return NS_OK;
}


nsresult
nsHTMLPluginObjElementSH::GetPluginJSObject(JSContext *cx, JSObject *obj,
                                            nsIPluginInstance *plugin_inst,
                                            JSObject **plugin_obj,
                                            JSObject **plugin_proto)
{
  *plugin_obj = nsnull;
  *plugin_proto = nsnull;

  JSAutoRequest ar(cx);

  if (plugin_inst) {
    plugin_inst->GetJSObject(cx, plugin_obj);
    if (*plugin_obj) {
      *plugin_proto = ::JS_GetPrototype(cx, *plugin_obj);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLPluginObjElementSH::NewResolve(nsIXPConnectWrappedNative *wrapper,
                                     JSContext *cx, JSObject *obj, jsid id,
                                     PRUint32 flags, JSObject **objp,
                                     PRBool *_retval)
{
  // Make sure the plugin instance is loaded and instantiated, if
  // possible.

  nsCOMPtr<nsIPluginInstance> pi;
  nsresult rv = GetPluginInstanceIfSafe(wrapper, obj, getter_AddRefs(pi));
  NS_ENSURE_SUCCESS(rv, rv);

  return nsElementSH::NewResolve(wrapper, cx, obj, id, flags, objp,
                                 _retval);
}
 
// HTMLOptionsCollection helper

NS_IMETHODIMP
nsHTMLOptionsCollectionSH::SetProperty(nsIXPConnectWrappedNative *wrapper,
                                       JSContext *cx, JSObject *obj, jsid id,
                                       jsval *vp, PRBool *_retval)
{
  PRInt32 n = GetArrayIndexFromId(cx, id);

  if (n < 0) {
    return NS_OK;
  }

  nsCOMPtr<nsIDOMNSHTMLOptionCollection> oc =
    do_QueryWrappedNative(wrapper, obj);
  NS_ENSURE_TRUE(oc, NS_ERROR_UNEXPECTED);

  nsresult rv = nsHTMLSelectElementSH::SetOption(cx, vp, n, oc);
  if (NS_SUCCEEDED(rv)) {
    rv = NS_SUCCESS_I_DID_SOMETHING;
  }
  return rv;
}


// Plugin helper

nsISupports*
nsPluginSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                      nsWrapperCache **aCache, nsresult *aResult)
{
  nsPluginElement* plugin = nsPluginElement::FromSupports(aNative);

  return plugin->GetItemAt(aIndex, aResult);
}

nsISupports*
nsPluginSH::GetNamedItem(nsISupports *aNative, const nsAString& aName,
                         nsWrapperCache **aCache, nsresult *aResult)
{
  nsPluginElement* plugin = nsPluginElement::FromSupports(aNative);

  return plugin->GetNamedItem(aName, aResult);
}


// PluginArray helper

nsISupports*
nsPluginArraySH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                           nsWrapperCache **aCache, nsresult *aResult)
{
  nsPluginArray* array = nsPluginArray::FromSupports(aNative);

  return array->GetItemAt(aIndex, aResult);
}

nsISupports*
nsPluginArraySH::GetNamedItem(nsISupports *aNative, const nsAString& aName,
                              nsWrapperCache **aCache, nsresult *aResult)
{
  nsPluginArray* array = nsPluginArray::FromSupports(aNative);

  return array->GetNamedItem(aName, aResult);
}


// MimeTypeArray helper

nsISupports*
nsMimeTypeArraySH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                             nsWrapperCache **aCache, nsresult *aResult)
{
  nsMimeTypeArray* array = nsMimeTypeArray::FromSupports(aNative);

  return array->GetItemAt(aIndex, aResult);
}

nsISupports*
nsMimeTypeArraySH::GetNamedItem(nsISupports *aNative, const nsAString& aName,
                                nsWrapperCache **aCache, nsresult *aResult)
{
  nsMimeTypeArray* array = nsMimeTypeArray::FromSupports(aNative);

  return array->GetNamedItem(aName, aResult);
}


// StringArray helper

NS_IMETHODIMP
nsStringArraySH::GetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                             JSObject *obj, jsid id, jsval *vp,
                             PRBool *_retval)
{
  PRBool is_number = PR_FALSE;
  PRInt32 n = GetArrayIndexFromId(cx, id, &is_number);

  if (!is_number) {
    return NS_OK;
  }

  nsAutoString val;

  nsresult rv = GetStringAt(GetNative(wrapper, obj), n, val);
  NS_ENSURE_SUCCESS(rv, rv);

  // XXX: Null strings?

  JSAutoRequest ar(cx);

  JSString *str =
    ::JS_NewUCStringCopyN(cx, reinterpret_cast<const jschar *>(val.get()),
                          val.Length());
  NS_ENSURE_TRUE(str, NS_ERROR_OUT_OF_MEMORY);

  *vp = STRING_TO_JSVAL(str);

  return NS_SUCCESS_I_DID_SOMETHING;
}


// History helper

NS_IMETHODIMP
nsHistorySH::GetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                         JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  PRBool is_number = PR_FALSE;
  GetArrayIndexFromId(cx, id, &is_number);

  if (!is_number) {
    return NS_OK;
  }

  nsresult rv =
    sSecMan->CheckPropertyAccess(cx, obj, mData->mName, sItem_id,
                                 nsIXPCSecurityManager::ACCESS_CALL_METHOD);

  if (NS_FAILED(rv)) {
    // Let XPConnect know that the access was not granted.
    *_retval = PR_FALSE;

    return NS_OK;
  }

  // sec check

  return nsStringArraySH::GetProperty(wrapper, cx, obj, id, vp, _retval);
}

nsresult
nsHistorySH::GetStringAt(nsISupports *aNative, PRInt32 aIndex,
                         nsAString& aResult)
{
  if (aIndex < 0) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  nsCOMPtr<nsIDOMHistory> history(do_QueryInterface(aNative));

  return history->Item(aIndex, aResult);
}


// MediaList helper

nsresult
nsMediaListSH::GetStringAt(nsISupports *aNative, PRInt32 aIndex,
                           nsAString& aResult)
{
  if (aIndex < 0) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  nsCOMPtr<nsIDOMMediaList> media_list(do_QueryInterface(aNative));

  return media_list->Item(PRUint32(aIndex), aResult);
}


// StyleSheetList helper

nsISupports*
nsStyleSheetListSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                              nsWrapperCache **aCache, nsresult *rv)
{
  nsDOMStyleSheetList* list = nsDOMStyleSheetList::FromSupports(aNative);

  return list->GetItemAt(aIndex);
}


// CSSValueList helper

nsISupports*
nsCSSValueListSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                            nsWrapperCache **aCache, nsresult *aResult)
{
  nsDOMCSSValueList* list = nsDOMCSSValueList::FromSupports(aNative);

  return list->GetItemAt(aIndex);
}


// CSSStyleDeclaration helper

NS_IMETHODIMP
nsCSSStyleDeclSH::PreCreate(nsISupports *nativeObj, JSContext *cx,
                            JSObject *globalObj, JSObject **parentObj)
{
  nsWrapperCache* cache = nsnull;
  CallQueryInterface(nativeObj, &cache);
  if (!cache) {
    return nsDOMClassInfo::PreCreate(nativeObj, cx, globalObj, parentObj);
  }

  nsICSSDeclaration *declaration = static_cast<nsICSSDeclaration*>(nativeObj);
  nsINode *native_parent = declaration->GetParentObject();
  if (!native_parent) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv =
    WrapNativeParent(cx, globalObj, native_parent, native_parent, parentObj);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_SUCCESS_ALLOW_SLIM_WRAPPERS;
}

nsresult
nsCSSStyleDeclSH::GetStringAt(nsISupports *aNative, PRInt32 aIndex,
                              nsAString& aResult)
{
  if (aIndex < 0) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  nsCOMPtr<nsIDOMCSSStyleDeclaration> style_decl(do_QueryInterface(aNative));

  return style_decl->Item(PRUint32(aIndex), aResult);
}


// CSSRuleList scriptable helper

nsISupports*
nsCSSRuleListSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                           nsWrapperCache **aCache, nsresult *aResult)
{
  nsICSSRuleList* list = static_cast<nsICSSRuleList*>(aNative);
#ifdef DEBUG
  {
    nsCOMPtr<nsICSSRuleList> list_qi = do_QueryInterface(aNative);

    // If this assertion fires the QI implementation for the object in
    // question doesn't use the nsICSSRuleList pointer as the nsISupports
    // pointer. That must be fixed, or we'll crash...
    NS_ASSERTION(list_qi == list, "Uh, fix QI!");
  }
#endif

  return list->GetItemAt(aIndex, aResult);
}

// ClientRectList scriptable helper

nsISupports*
nsClientRectListSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                              nsWrapperCache **aCache, nsresult *aResult)
{
  nsClientRectList* list = nsClientRectList::FromSupports(aNative);

  return list->GetItemAt(aIndex);
}

// PaintRequestList scriptable helper

nsISupports*
nsPaintRequestListSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                                nsWrapperCache **aCache, nsresult *aResult)
{
  nsPaintRequestList* list = nsPaintRequestList::FromSupports(aNative);

  return list->GetItemAt(aIndex);
}

#ifdef MOZ_XUL
// TreeColumns helper

nsISupports*
nsTreeColumnsSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                           nsWrapperCache **aCache, nsresult *aResult)
{
  nsTreeColumns* columns = nsTreeColumns::FromSupports(aNative);

  return columns->GetColumnAt(aIndex);
}

nsISupports*
nsTreeColumnsSH::GetNamedItem(nsISupports *aNative,
                              const nsAString& aName,
                              nsWrapperCache **aCache,
                              nsresult *aResult)
{
  nsTreeColumns* columns = nsTreeColumns::FromSupports(aNative);

  return columns->GetNamedColumn(aName);
}
#endif


// Storage scriptable helper

// One reason we need a newResolve hook is that in order for
// enumeration of storage object keys to work the keys we're
// enumerating need to exist on the storage object for the JS engine
// to find them.

NS_IMETHODIMP
nsStorageSH::NewResolve(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                        JSObject *obj, jsid id, PRUint32 flags,
                        JSObject **objp, PRBool *_retval)
{
  JSObject *realObj;
  wrapper->GetJSObject(&realObj);

  // First check to see if the property is defined on our prototype.

  JSObject *proto = ::JS_GetPrototype(cx, realObj);
  JSBool hasProp;

  if (proto &&
      (::JS_HasPropertyById(cx, proto, id, &hasProp) &&
       hasProp)) {
    // We found the property we're resolving on the prototype,
    // nothing left to do here then.

    return NS_OK;
  }

  // We're resolving property that doesn't exist on the prototype,
  // check if the key exists in the storage object.

  nsCOMPtr<nsIDOMStorageObsolete> storage(do_QueryWrappedNative(wrapper));

  JSString *jsstr = IdToString(cx, id);
  if (!jsstr)
    return JS_FALSE;

  // GetItem() will return null if the caller can't access the session
  // storage item.
  nsCOMPtr<nsIDOMStorageItem> item;
  nsresult rv = storage->GetItem(nsDependentJSString(jsstr),
                                 getter_AddRefs(item));
  NS_ENSURE_SUCCESS(rv, rv);

  if (item) {
    if (!::JS_DefinePropertyById(cx, realObj, id, JSVAL_VOID, nsnull, nsnull,
                                 JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }

    *objp = realObj;
  }

  return NS_OK;
}

nsISupports*
nsStorageSH::GetNamedItem(nsISupports *aNative, const nsAString& aName,
                          nsWrapperCache **aCache, nsresult *aResult)
{
  nsDOMStorage* storage = nsDOMStorage::FromSupports(aNative);

  return storage->GetNamedItem(aName, aResult);
}

NS_IMETHODIMP
nsStorageSH::SetProperty(nsIXPConnectWrappedNative *wrapper,
                         JSContext *cx, JSObject *obj, jsid id,
                         jsval *vp, PRBool *_retval)
{
  nsCOMPtr<nsIDOMStorageObsolete> storage(do_QueryWrappedNative(wrapper));
  NS_ENSURE_TRUE(storage, NS_ERROR_UNEXPECTED);

  JSString *key = IdToString(cx, id);
  NS_ENSURE_TRUE(key, NS_ERROR_UNEXPECTED);

  JSString *value = ::JS_ValueToString(cx, *vp);
  NS_ENSURE_TRUE(value, NS_ERROR_UNEXPECTED);

  nsresult rv = storage->SetItem(nsDependentJSString(key),
                                 nsDependentJSString(value));
  if (NS_SUCCEEDED(rv)) {
    rv = NS_SUCCESS_I_DID_SOMETHING;
  }

  return rv;
}

NS_IMETHODIMP
nsStorageSH::DelProperty(nsIXPConnectWrappedNative *wrapper,
                         JSContext *cx, JSObject *obj, jsid id,
                         jsval *vp, PRBool *_retval)
{
  nsCOMPtr<nsIDOMStorageObsolete> storage(do_QueryWrappedNative(wrapper));
  NS_ENSURE_TRUE(storage, NS_ERROR_UNEXPECTED);

  JSString *key = IdToString(cx, id);
  NS_ENSURE_TRUE(key, NS_ERROR_UNEXPECTED);

  nsresult rv = storage->RemoveItem(nsDependentJSString(key));
  if (NS_SUCCEEDED(rv)) {
    rv = NS_SUCCESS_I_DID_SOMETHING;
  }

  return rv;
}


NS_IMETHODIMP
nsStorageSH::NewEnumerate(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                          JSObject *obj, PRUint32 enum_op, jsval *statep,
                          jsid *idp, PRBool *_retval)
{
  nsTArray<nsString> *keys =
    (nsTArray<nsString> *)JSVAL_TO_PRIVATE(*statep);

  switch (enum_op) {
    case JSENUMERATE_INIT:
    case JSENUMERATE_INIT_ALL:
    {
      nsCOMPtr<nsPIDOMStorage> storage(do_QueryWrappedNative(wrapper));

      // XXXndeakin need to free the keys afterwards
      keys = storage->GetKeys();
      NS_ENSURE_TRUE(keys, NS_ERROR_OUT_OF_MEMORY);

      *statep = PRIVATE_TO_JSVAL(keys);

      if (idp) {
        *idp = INT_TO_JSID(keys->Length());
      }
      break;
    }
    case JSENUMERATE_NEXT:
      if (keys->Length() != 0) {
        nsString& key = keys->ElementAt(0);
        JSString *str =
          JS_NewUCStringCopyN(cx, reinterpret_cast<const jschar *>
                                                  (key.get()),
                              key.Length());
        NS_ENSURE_TRUE(str, NS_ERROR_OUT_OF_MEMORY);

        JS_ValueToId(cx, STRING_TO_JSVAL(str), idp);

        keys->RemoveElementAt(0);

        break;
      }

      // Fall through
    case JSENUMERATE_DESTROY:
      delete keys;

      *statep = JSVAL_NULL;

      break;
    default:
      NS_NOTREACHED("Bad call from the JS engine");

      return NS_ERROR_FAILURE;
  }

  return NS_OK;
}


// Storage2SH

// One reason we need a newResolve hook is that in order for
// enumeration of storage object keys to work the keys we're
// enumerating need to exist on the storage object for the JS engine
// to find them.

NS_IMETHODIMP
nsStorage2SH::NewResolve(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                         JSObject *obj, jsid id, PRUint32 flags,
                         JSObject **objp, PRBool *_retval)
{
  JSObject *realObj;
  wrapper->GetJSObject(&realObj);

  // First check to see if the property is defined on our prototype,
  // after converting id to a string if it's an integer.

  JSString *jsstr = IdToString(cx, id);
  if (!jsstr) {
    return JS_FALSE;
  }

  JSObject *proto = ::JS_GetPrototype(cx, realObj);
  JSBool hasProp;

  if (proto &&
      (::JS_HasPropertyById(cx, proto, id, &hasProp) &&
       hasProp)) {
    // We found the property we're resolving on the prototype,
    // nothing left to do here then.

    return NS_OK;
  }

  // We're resolving property that doesn't exist on the prototype,
  // check if the key exists in the storage object.

  nsCOMPtr<nsIDOMStorage> storage(do_QueryWrappedNative(wrapper));

  // GetItem() will return null if the caller can't access the session
  // storage item.
  nsAutoString data;
  nsresult rv = storage->GetItem(nsDependentJSString(jsstr), data);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!DOMStringIsNull(data)) {
    if (!::JS_DefinePropertyById(cx, realObj, id, JSVAL_VOID, nsnull,
                                 nsnull, JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }

    *objp = realObj;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsStorage2SH::GetProperty(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                          JSObject *obj, jsid id, jsval *vp, PRBool *_retval)
{
  nsCOMPtr<nsIDOMStorage> storage(do_QueryWrappedNative(wrapper));
  NS_ENSURE_TRUE(storage, NS_ERROR_UNEXPECTED);

  nsAutoString val;
  nsresult rv = NS_OK;

  if (JSID_IS_STRING(id)) {
    // For native wrappers, do not get random names on storage objects.
    if (ObjectIsNativeWrapper(cx, obj)) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    rv = storage->GetItem(nsDependentJSString(id), val);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    PRInt32 n = GetArrayIndexFromId(cx, id);
    NS_ENSURE_TRUE(n >= 0, NS_ERROR_NOT_AVAILABLE);

    rv = storage->Key(n, val);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  JSAutoRequest ar(cx);

  if (DOMStringIsNull(val)) {
      *vp = JSVAL_NULL;
  }
  else {
      JSString *str =
        ::JS_NewUCStringCopyN(cx, reinterpret_cast<const jschar *>(val.get()),
                              val.Length());
      NS_ENSURE_TRUE(str, NS_ERROR_OUT_OF_MEMORY);

      *vp = STRING_TO_JSVAL(str);
  }

  return NS_SUCCESS_I_DID_SOMETHING;
}

NS_IMETHODIMP
nsStorage2SH::SetProperty(nsIXPConnectWrappedNative *wrapper,
                          JSContext *cx, JSObject *obj, jsid id,
                          jsval *vp, PRBool *_retval)
{
  nsCOMPtr<nsIDOMStorage> storage(do_QueryWrappedNative(wrapper));
  NS_ENSURE_TRUE(storage, NS_ERROR_UNEXPECTED);

  JSString *key = IdToString(cx, id);
  NS_ENSURE_TRUE(key, NS_ERROR_UNEXPECTED);

  JSString *value = ::JS_ValueToString(cx, *vp);
  NS_ENSURE_TRUE(value, NS_ERROR_UNEXPECTED);

  nsresult rv = storage->SetItem(nsDependentJSString(key),
                                 nsDependentJSString(value));
  if (NS_SUCCEEDED(rv)) {
    rv = NS_SUCCESS_I_DID_SOMETHING;
  }

  return rv;
}

NS_IMETHODIMP
nsStorage2SH::DelProperty(nsIXPConnectWrappedNative *wrapper,
                          JSContext *cx, JSObject *obj, jsid id,
                          jsval *vp, PRBool *_retval)
{
  nsCOMPtr<nsIDOMStorage> storage(do_QueryWrappedNative(wrapper));
  NS_ENSURE_TRUE(storage, NS_ERROR_UNEXPECTED);

  JSString *key = IdToString(cx, id);
  NS_ENSURE_TRUE(key, NS_ERROR_UNEXPECTED);

  nsresult rv = storage->RemoveItem(nsDependentJSString(key));
  if (NS_SUCCEEDED(rv)) {
    rv = NS_SUCCESS_I_DID_SOMETHING;
  }

  return rv;
}


NS_IMETHODIMP
nsStorage2SH::NewEnumerate(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                           JSObject *obj, PRUint32 enum_op, jsval *statep,
                           jsid *idp, PRBool *_retval)
{
  nsTArray<nsString> *keys =
    (nsTArray<nsString> *)JSVAL_TO_PRIVATE(*statep);

  switch (enum_op) {
    case JSENUMERATE_INIT:
    case JSENUMERATE_INIT_ALL:
    {
      nsCOMPtr<nsPIDOMStorage> storage(do_QueryWrappedNative(wrapper));

      // XXXndeakin need to free the keys afterwards
      keys = storage->GetKeys();
      NS_ENSURE_TRUE(keys, NS_ERROR_OUT_OF_MEMORY);

      *statep = PRIVATE_TO_JSVAL(keys);

      if (idp) {
        *idp = INT_TO_JSID(keys->Length());
      }
      break;
    }
    case JSENUMERATE_NEXT:
      if (keys->Length() != 0) {
        nsString& key = keys->ElementAt(0);
        JSString *str =
          JS_NewUCStringCopyN(cx, reinterpret_cast<const jschar *>
                                                  (key.get()),
                              key.Length());
        NS_ENSURE_TRUE(str, NS_ERROR_OUT_OF_MEMORY);

        JS_ValueToId(cx, STRING_TO_JSVAL(str), idp);

        keys->RemoveElementAt(0);

        break;
      }

      // Fall through
    case JSENUMERATE_DESTROY:
      delete keys;

      *statep = JSVAL_NULL;

      break;
    default:
      NS_NOTREACHED("Bad call from the JS engine");

      return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

// StorageList scriptable helper

nsISupports*
nsStorageListSH::GetNamedItem(nsISupports *aNative, const nsAString& aName,
                              nsWrapperCache **aCache, nsresult *aResult)
{
  nsDOMStorageList* storagelist = static_cast<nsDOMStorageList*>(aNative);

  return storagelist->GetNamedItem(aName, aResult);
}


// nsIDOMEventListener::HandleEvent() 'this' converter helper

NS_INTERFACE_MAP_BEGIN(nsEventListenerThisTranslator)
  NS_INTERFACE_MAP_ENTRY(nsIXPCFunctionThisTranslator)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END


NS_IMPL_ADDREF(nsEventListenerThisTranslator)
NS_IMPL_RELEASE(nsEventListenerThisTranslator)


NS_IMETHODIMP
nsEventListenerThisTranslator::TranslateThis(nsISupports *aInitialThis,
                                             nsIInterfaceInfo *aInterfaceInfo,
                                             PRUint16 aMethodIndex,
                                             PRBool *aHideFirstParamFromJS,
                                             nsIID * *aIIDOfResult,
                                             nsISupports **_retval)
{
  *aHideFirstParamFromJS = PR_FALSE;
  *aIIDOfResult = nsnull;

  nsCOMPtr<nsIDOMEvent> event(do_QueryInterface(aInitialThis));
  NS_ENSURE_TRUE(event, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIDOMEventTarget> target;
  event->GetCurrentTarget(getter_AddRefs(target));

  *_retval = target.forget().get();

  return NS_OK;
}

NS_IMETHODIMP
nsDOMConstructorSH::Call(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                         JSObject *obj, PRUint32 argc, jsval *argv, jsval *vp,
                         PRBool *_retval)
{
  nsDOMConstructor *wrapped =
    static_cast<nsDOMConstructor *>(wrapper->Native());

#ifdef DEBUG
  {
    nsCOMPtr<nsIDOMDOMConstructor> is_constructor =
      do_QueryWrappedNative(wrapper);
    NS_ASSERTION(is_constructor, "How did we not get a constructor?");
  }
#endif

  return wrapped->Construct(wrapper, cx, obj, argc, argv, vp, _retval);
}

NS_IMETHODIMP
nsDOMConstructorSH::Construct(nsIXPConnectWrappedNative *wrapper, JSContext *cx,
                              JSObject *obj, PRUint32 argc, jsval *argv,
                              jsval *vp, PRBool *_retval)
{
  nsDOMConstructor *wrapped =
    static_cast<nsDOMConstructor *>(wrapper->Native());

#ifdef DEBUG
  {
    nsCOMPtr<nsIDOMDOMConstructor> is_constructor =
      do_QueryWrappedNative(wrapper);
    NS_ASSERTION(is_constructor, "How did we not get a constructor?");
  }
#endif

  return wrapped->Construct(wrapper, cx, obj, argc, argv, vp, _retval);
}

NS_IMETHODIMP
nsDOMConstructorSH::HasInstance(nsIXPConnectWrappedNative *wrapper,
                                JSContext *cx, JSObject *obj, const jsval &val,
                                PRBool *bp, PRBool *_retval)
{
  nsDOMConstructor *wrapped =
    static_cast<nsDOMConstructor *>(wrapper->Native());

#ifdef DEBUG
  {
    nsCOMPtr<nsIDOMDOMConstructor> is_constructor =
      do_QueryWrappedNative(wrapper);
    NS_ASSERTION(is_constructor, "How did we not get a constructor?");
  }
#endif

  return wrapped->HasInstance(wrapper, cx, obj, val, bp, _retval);
}

NS_IMETHODIMP
nsNonDOMObjectSH::GetFlags(PRUint32 *aFlags)
{
  // This is NOT a DOM Object.  Use this helper class for cases when you need
  // to do something like implement nsISecurityCheckedComponent in a meaningful
  // way.
  *aFlags = nsIClassInfo::MAIN_THREAD_ONLY;
  return NS_OK;
}

NS_IMETHODIMP
nsAttributeSH::GetFlags(PRUint32 *aFlags)
{
  // Just like nsNodeSH, but without CONTENT_NODE
  *aFlags = DOMCLASSINFO_STANDARD_FLAGS;

  return NS_OK;
}

// nsOfflineResourceListSH
nsresult
nsOfflineResourceListSH::GetStringAt(nsISupports *aNative, PRInt32 aIndex,
                                     nsAString& aResult)
{
  nsCOMPtr<nsIDOMOfflineResourceList> list(do_QueryInterface(aNative));
  NS_ENSURE_TRUE(list, NS_ERROR_UNEXPECTED);

  return list->MozItem(aIndex, aResult);
}

// nsFileListSH
nsISupports*
nsFileListSH::GetItemAt(nsISupports *aNative, PRUint32 aIndex,
                        nsWrapperCache **aCache, nsresult *aResult)
{
  nsDOMFileList* list = nsDOMFileList::FromSupports(aNative);

  return list->GetItemAt(aIndex);
}
