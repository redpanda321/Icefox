/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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

DOMCI_CLASS(Window)
DOMCI_CLASS(InnerWindow)
DOMCI_CLASS(Location)
DOMCI_CLASS(Navigator)
DOMCI_CLASS(Plugin)
DOMCI_CLASS(PluginArray)
DOMCI_CLASS(MimeType)
DOMCI_CLASS(MimeTypeArray)
DOMCI_CLASS(BarProp)
DOMCI_CLASS(History)
DOMCI_CLASS(Screen)
DOMCI_CLASS(DOMPrototype)
DOMCI_CLASS(DOMConstructor)

// Core classes
DOMCI_CLASS(XMLDocument)
DOMCI_CLASS(DocumentType)
DOMCI_CLASS(DOMImplementation)
DOMCI_CLASS(DOMException)
DOMCI_CLASS(DOMTokenList)
DOMCI_CLASS(DOMSettableTokenList)
DOMCI_CLASS(DocumentFragment)
DOMCI_CLASS(Element)
DOMCI_CLASS(Attr)
DOMCI_CLASS(Text)
DOMCI_CLASS(Comment)
DOMCI_CLASS(CDATASection)
DOMCI_CLASS(ProcessingInstruction)
DOMCI_CLASS(Notation)
DOMCI_CLASS(NodeList)
DOMCI_CLASS(NamedNodeMap)

// Event classes
DOMCI_CLASS(Event)
DOMCI_CLASS(MutationEvent)
DOMCI_CLASS(UIEvent)
DOMCI_CLASS(MouseEvent)
DOMCI_CLASS(MouseScrollEvent)
DOMCI_CLASS(DragEvent)
DOMCI_CLASS(KeyboardEvent)
DOMCI_CLASS(PopupBlockedEvent)
DOMCI_CLASS(OrientationEvent)

// HTML classes
DOMCI_CLASS(HTMLDocument)
DOMCI_CLASS(HTMLOptionsCollection)
DOMCI_CLASS(HTMLCollection)

// HTML element classes
DOMCI_CLASS(HTMLElement)
DOMCI_CLASS(HTMLAnchorElement)
DOMCI_CLASS(HTMLAppletElement)
DOMCI_CLASS(HTMLAreaElement)
DOMCI_CLASS(HTMLBRElement)
DOMCI_CLASS(HTMLBaseElement)
DOMCI_CLASS(HTMLBodyElement)
DOMCI_CLASS(HTMLButtonElement)
DOMCI_CLASS(HTMLDListElement)
DOMCI_CLASS(HTMLDelElement)
DOMCI_CLASS(HTMLDirectoryElement)
DOMCI_CLASS(HTMLDivElement)
DOMCI_CLASS(HTMLEmbedElement)
DOMCI_CLASS(HTMLFieldSetElement)
DOMCI_CLASS(HTMLFontElement)
DOMCI_CLASS(HTMLFormElement)
DOMCI_CLASS(HTMLFrameElement)
DOMCI_CLASS(HTMLFrameSetElement)
DOMCI_CLASS(HTMLHRElement)
DOMCI_CLASS(HTMLHeadElement)
DOMCI_CLASS(HTMLHeadingElement)
DOMCI_CLASS(HTMLHtmlElement)
DOMCI_CLASS(HTMLIFrameElement)
DOMCI_CLASS(HTMLImageElement)
DOMCI_CLASS(HTMLInputElement)
DOMCI_CLASS(HTMLInsElement)
DOMCI_CLASS(HTMLIsIndexElement)
DOMCI_CLASS(HTMLLIElement)
DOMCI_CLASS(HTMLLabelElement)
DOMCI_CLASS(HTMLLegendElement)
DOMCI_CLASS(HTMLLinkElement)
DOMCI_CLASS(HTMLMapElement)
DOMCI_CLASS(HTMLMenuElement)
DOMCI_CLASS(HTMLMetaElement)
DOMCI_CLASS(HTMLOListElement)
DOMCI_CLASS(HTMLObjectElement)
DOMCI_CLASS(HTMLOptGroupElement)
DOMCI_CLASS(HTMLOptionElement)
DOMCI_CLASS(HTMLOutputElement)
DOMCI_CLASS(HTMLParagraphElement)
DOMCI_CLASS(HTMLParamElement)
DOMCI_CLASS(HTMLPreElement)
DOMCI_CLASS(HTMLQuoteElement)
DOMCI_CLASS(HTMLScriptElement)
DOMCI_CLASS(HTMLSelectElement)
DOMCI_CLASS(HTMLSpanElement)
DOMCI_CLASS(HTMLStyleElement)
DOMCI_CLASS(HTMLTableCaptionElement)
DOMCI_CLASS(HTMLTableCellElement)
DOMCI_CLASS(HTMLTableColElement)
DOMCI_CLASS(HTMLTableElement)
DOMCI_CLASS(HTMLTableRowElement)
DOMCI_CLASS(HTMLTableSectionElement)
DOMCI_CLASS(HTMLTextAreaElement)
DOMCI_CLASS(HTMLTitleElement)
DOMCI_CLASS(HTMLUListElement)
DOMCI_CLASS(HTMLUnknownElement)
DOMCI_CLASS(ValidityState)

// CSS classes
DOMCI_CLASS(CSSStyleRule)
DOMCI_CLASS(CSSCharsetRule)
DOMCI_CLASS(CSSImportRule)
DOMCI_CLASS(CSSMediaRule)
DOMCI_CLASS(CSSNameSpaceRule)
DOMCI_CLASS(CSSRuleList)
DOMCI_CLASS(CSSGroupRuleRuleList)
DOMCI_CLASS(MediaList)
DOMCI_CLASS(StyleSheetList)
DOMCI_CLASS(CSSStyleSheet)
DOMCI_CLASS(CSSStyleDeclaration)
DOMCI_CLASS(ComputedCSSStyleDeclaration)
DOMCI_CLASS(ROCSSPrimitiveValue)

// Range classes
DOMCI_CLASS(Range)
DOMCI_CLASS(Selection)

// XUL classes
#ifdef MOZ_XUL
DOMCI_CLASS(XULDocument)
DOMCI_CLASS(XULElement)
DOMCI_CLASS(XULCommandDispatcher)
#endif
DOMCI_CLASS(XULControllers)
DOMCI_CLASS(BoxObject)
#ifdef MOZ_XUL
DOMCI_CLASS(TreeSelection)
DOMCI_CLASS(TreeContentView)
#endif

// Crypto classes
DOMCI_CLASS(Crypto)
DOMCI_CLASS(CRMFObject)
  
// DOM Traversal classes
DOMCI_CLASS(TreeWalker)

// Rect object used by getComputedStyle
DOMCI_CLASS(CSSRect)

// DOM Chrome Window class, almost identical to Window
DOMCI_CLASS(ChromeWindow)
DOMCI_CLASS(InnerChromeWindow)

// RGBColor object used by getComputedStyle
DOMCI_CLASS(CSSRGBColor)

DOMCI_CLASS(RangeException)

// CSSValueList object that represents an nsIDOMCSSValueList, used
// by DOM CSS
DOMCI_CLASS(CSSValueList)

// ContentList object used for various live NodeLists
DOMCI_CLASS(ContentList)
  
// Processing-instruction with target "xml-stylesheet"
DOMCI_CLASS(XMLStylesheetProcessingInstruction)
  
DOMCI_CLASS(ImageDocument)

#ifdef MOZ_XUL
DOMCI_CLASS(XULTemplateBuilder)
DOMCI_CLASS(XULTreeBuilder)
#endif

// DOMStringList object
DOMCI_CLASS(DOMStringList)

// NameList object used by the DOM
DOMCI_CLASS(NameList)

#ifdef MOZ_XUL
DOMCI_CLASS(TreeColumn)
DOMCI_CLASS(TreeColumns)
#endif

DOMCI_CLASS(CSSMozDocumentRule)

DOMCI_CLASS(BeforeUnloadEvent)

#ifdef MOZ_SVG
// The SVG document
DOMCI_CLASS(SVGDocument)

// SVG element classes
DOMCI_CLASS(SVGAElement)
DOMCI_CLASS(SVGAltGlyphElement)
#ifdef MOZ_SMIL
DOMCI_CLASS(SVGAnimateElement)
DOMCI_CLASS(SVGAnimateTransformElement)
DOMCI_CLASS(SVGAnimateMotionElement)
DOMCI_CLASS(SVGMpathElement)
DOMCI_CLASS(SVGSetElement)
DOMCI_CLASS(TimeEvent)
#endif // MOZ_SMIL
DOMCI_CLASS(SVGCircleElement)
DOMCI_CLASS(SVGClipPathElement)
DOMCI_CLASS(SVGDefsElement)
DOMCI_CLASS(SVGDescElement)
DOMCI_CLASS(SVGEllipseElement)
DOMCI_CLASS(SVGFEBlendElement)
DOMCI_CLASS(SVGFEColorMatrixElement)
DOMCI_CLASS(SVGFEComponentTransferElement)
DOMCI_CLASS(SVGFECompositeElement)
DOMCI_CLASS(SVGFEConvolveMatrixElement)
DOMCI_CLASS(SVGFEDiffuseLightingElement)
DOMCI_CLASS(SVGFEDisplacementMapElement)
DOMCI_CLASS(SVGFEDistantLightElement)
DOMCI_CLASS(SVGFEFloodElement)
DOMCI_CLASS(SVGFEFuncAElement)
DOMCI_CLASS(SVGFEFuncBElement)
DOMCI_CLASS(SVGFEFuncGElement)
DOMCI_CLASS(SVGFEFuncRElement)
DOMCI_CLASS(SVGFEGaussianBlurElement)
DOMCI_CLASS(SVGFEImageElement)
DOMCI_CLASS(SVGFEMergeElement)
DOMCI_CLASS(SVGFEMergeNodeElement)
DOMCI_CLASS(SVGFEMorphologyElement)
DOMCI_CLASS(SVGFEOffsetElement)
DOMCI_CLASS(SVGFEPointLightElement)
DOMCI_CLASS(SVGFESpecularLightingElement)
DOMCI_CLASS(SVGFESpotLightElement)
DOMCI_CLASS(SVGFETileElement)
DOMCI_CLASS(SVGFETurbulenceElement)
DOMCI_CLASS(SVGFilterElement)
DOMCI_CLASS(SVGGElement)
DOMCI_CLASS(SVGImageElement)
DOMCI_CLASS(SVGLinearGradientElement)
DOMCI_CLASS(SVGLineElement)
DOMCI_CLASS(SVGMarkerElement)
DOMCI_CLASS(SVGMaskElement)
DOMCI_CLASS(SVGMetadataElement)
DOMCI_CLASS(SVGPathElement)
DOMCI_CLASS(SVGPatternElement)
DOMCI_CLASS(SVGPolygonElement)
DOMCI_CLASS(SVGPolylineElement)
DOMCI_CLASS(SVGRadialGradientElement)
DOMCI_CLASS(SVGRectElement)
DOMCI_CLASS(SVGScriptElement)
DOMCI_CLASS(SVGStopElement)
DOMCI_CLASS(SVGStyleElement)
DOMCI_CLASS(SVGSVGElement)
DOMCI_CLASS(SVGSwitchElement)
DOMCI_CLASS(SVGSymbolElement)
DOMCI_CLASS(SVGTextElement)
DOMCI_CLASS(SVGTextPathElement)
DOMCI_CLASS(SVGTitleElement)
DOMCI_CLASS(SVGTSpanElement)
DOMCI_CLASS(SVGUseElement)

// other SVG classes
DOMCI_CLASS(SVGAngle)
DOMCI_CLASS(SVGAnimatedAngle)
DOMCI_CLASS(SVGAnimatedBoolean)
DOMCI_CLASS(SVGAnimatedEnumeration)
DOMCI_CLASS(SVGAnimatedInteger)
DOMCI_CLASS(SVGAnimatedLength)
DOMCI_CLASS(SVGAnimatedLengthList)
DOMCI_CLASS(SVGAnimatedNumber)
DOMCI_CLASS(SVGAnimatedNumberList)
DOMCI_CLASS(SVGAnimatedPreserveAspectRatio)
DOMCI_CLASS(SVGAnimatedRect)
DOMCI_CLASS(SVGAnimatedString)
DOMCI_CLASS(SVGAnimatedTransformList)
DOMCI_CLASS(SVGEvent)
DOMCI_CLASS(SVGException)
DOMCI_CLASS(SVGLength)
DOMCI_CLASS(SVGLengthList)
DOMCI_CLASS(SVGMatrix)
DOMCI_CLASS(SVGNumber)
DOMCI_CLASS(SVGNumberList)
DOMCI_CLASS(SVGPathSegArcAbs)
DOMCI_CLASS(SVGPathSegArcRel)
DOMCI_CLASS(SVGPathSegClosePath)
DOMCI_CLASS(SVGPathSegCurvetoCubicAbs)
DOMCI_CLASS(SVGPathSegCurvetoCubicRel)
DOMCI_CLASS(SVGPathSegCurvetoCubicSmoothAbs)
DOMCI_CLASS(SVGPathSegCurvetoCubicSmoothRel)
DOMCI_CLASS(SVGPathSegCurvetoQuadraticAbs)
DOMCI_CLASS(SVGPathSegCurvetoQuadraticRel)
DOMCI_CLASS(SVGPathSegCurvetoQuadraticSmoothAbs)
DOMCI_CLASS(SVGPathSegCurvetoQuadraticSmoothRel)
DOMCI_CLASS(SVGPathSegLinetoAbs)
DOMCI_CLASS(SVGPathSegLinetoHorizontalAbs)
DOMCI_CLASS(SVGPathSegLinetoHorizontalRel)
DOMCI_CLASS(SVGPathSegLinetoRel)
DOMCI_CLASS(SVGPathSegLinetoVerticalAbs)
DOMCI_CLASS(SVGPathSegLinetoVerticalRel)
DOMCI_CLASS(SVGPathSegList)
DOMCI_CLASS(SVGPathSegMovetoAbs)
DOMCI_CLASS(SVGPathSegMovetoRel)
DOMCI_CLASS(SVGPoint)
DOMCI_CLASS(SVGPointList)
DOMCI_CLASS(SVGPreserveAspectRatio)
DOMCI_CLASS(SVGRect)
DOMCI_CLASS(SVGTransform)
DOMCI_CLASS(SVGTransformList)
DOMCI_CLASS(SVGZoomEvent)
#endif // MOZ_SVG

// Canvas
DOMCI_CLASS(HTMLCanvasElement)
DOMCI_CLASS(CanvasRenderingContext2D)
DOMCI_CLASS(CanvasGradient)
DOMCI_CLASS(CanvasPattern)
DOMCI_CLASS(TextMetrics)

// SmartCard Events
DOMCI_CLASS(SmartCardEvent)
  
// PageTransition Events
DOMCI_CLASS(PageTransitionEvent)

// WindowUtils
DOMCI_CLASS(WindowUtils)

// XSLTProcessor
DOMCI_CLASS(XSLTProcessor)

// DOM Level 3 XPath objects
DOMCI_CLASS(XPathEvaluator)
DOMCI_CLASS(XPathException)
DOMCI_CLASS(XPathExpression)
DOMCI_CLASS(XPathNSResolver)
DOMCI_CLASS(XPathResult)

// WhatWG WebApps Objects
DOMCI_CLASS(StorageObsolete)
DOMCI_CLASS(Storage)
DOMCI_CLASS(StorageList)
DOMCI_CLASS(StorageItem)
DOMCI_CLASS(StorageEvent)
DOMCI_CLASS(StorageEventObsolete)

// DOMParser, XMLSerializer
DOMCI_CLASS(DOMParser)
DOMCI_CLASS(XMLSerializer)

// XMLHttpRequest
DOMCI_CLASS(XMLHttpProgressEvent)
DOMCI_CLASS(XMLHttpRequest)

DOMCI_CLASS(ClientRect)
DOMCI_CLASS(ClientRectList)

#ifdef MOZ_SVG
DOMCI_CLASS(SVGForeignObjectElement)
#endif

DOMCI_CLASS(XULCommandEvent)
DOMCI_CLASS(CommandEvent)
DOMCI_CLASS(OfflineResourceList)

DOMCI_CLASS(FileList)
DOMCI_CLASS(File)
DOMCI_CLASS(FileException)
DOMCI_CLASS(FileError)
DOMCI_CLASS(FileReader)

// DOM modal content window class, almost identical to Window
DOMCI_CLASS(ModalContentWindow)
DOMCI_CLASS(InnerModalContentWindow)

// Data Events
DOMCI_CLASS(DataContainerEvent)

// event used for cross-domain message-passing and for server-sent events in
// HTML5
DOMCI_CLASS(MessageEvent)

// Geolocation
DOMCI_CLASS(GeoGeolocation)
DOMCI_CLASS(GeoPosition)
DOMCI_CLASS(GeoPositionCoords)
DOMCI_CLASS(GeoPositionError)

// @font-face in CSS
DOMCI_CLASS(CSSFontFaceRule)
DOMCI_CLASS(CSSFontFaceStyleDecl)

#if defined(MOZ_MEDIA)
// WhatWG Video Element
DOMCI_CLASS(HTMLVideoElement)
DOMCI_CLASS(HTMLSourceElement)
DOMCI_CLASS(MediaError)
DOMCI_CLASS(HTMLAudioElement)
DOMCI_CLASS(TimeRanges)
#endif

DOMCI_CLASS(ProgressEvent)

DOMCI_CLASS(XMLHttpRequestUpload)

// DOM Traversal NodeIterator class
DOMCI_CLASS(NodeIterator)

DOMCI_CLASS(DataTransfer)

DOMCI_CLASS(NotifyPaintEvent)

DOMCI_CLASS(NotifyAudioAvailableEvent)

DOMCI_CLASS(SimpleGestureEvent)

DOMCI_CLASS(MozTouchEvent)

#ifdef MOZ_MATHML
DOMCI_CLASS(MathMLElement)
#endif

DOMCI_CLASS(Worker)
DOMCI_CLASS(ChromeWorker)

// WebGL
DOMCI_CLASS(CanvasRenderingContextWebGL)
DOMCI_CLASS(WebGLBuffer)
DOMCI_CLASS(WebGLTexture)
DOMCI_CLASS(WebGLProgram)
DOMCI_CLASS(WebGLShader)
DOMCI_CLASS(WebGLFramebuffer)
DOMCI_CLASS(WebGLRenderbuffer)
DOMCI_CLASS(WebGLUniformLocation)
DOMCI_CLASS(WebGLActiveInfo)

// WebGL Buffers
DOMCI_CLASS(PaintRequest)
DOMCI_CLASS(PaintRequestList)

DOMCI_CLASS(ScrollAreaEvent)
DOMCI_CLASS(PopStateEvent)

DOMCI_CLASS(EventListenerInfo)

DOMCI_CLASS(TransitionEvent)
DOMCI_CLASS(ContentFrameMessageManager)

DOMCI_CLASS(FormData)

// WebSocket
DOMCI_CLASS(WebSocket)
DOMCI_CLASS(CloseEvent)

DOMCI_CLASS(IDBFactory)
DOMCI_CLASS(IDBRequest)
DOMCI_CLASS(IDBDatabase)
DOMCI_CLASS(IDBErrorEvent)
DOMCI_CLASS(IDBSuccessEvent)
DOMCI_CLASS(IDBTransactionEvent)
DOMCI_CLASS(IDBObjectStore)
DOMCI_CLASS(IDBTransaction)
DOMCI_CLASS(IDBCursor)
DOMCI_CLASS(IDBKeyRange)
DOMCI_CLASS(IDBIndex)
