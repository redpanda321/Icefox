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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Pierre Phaneuf <pp@ludusdesign.com>
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

#ifdef MOZ_IPC
#include "base/basictypes.h"
#endif

#include "xpcmodule.h"
#include "mozilla/ModuleUtils.h"
#include "nsLayoutStatics.h"
#include "nsContentCID.h"
#include "nsContentDLF.h"
#include "nsContentPolicyUtils.h"
#include "nsDataDocumentContentPolicy.h"
#include "nsNoDataProtocolContentPolicy.h"
#include "nsDOMCID.h"
#include "nsHTMLContentSerializer.h"
#include "nsHTMLParts.h"
#include "nsGenericHTMLElement.h"
#include "nsIComponentManager.h"
#include "nsIContentIterator.h"
#include "nsIContentSerializer.h"
#include "nsIController.h"
#include "nsIControllers.h"
#include "nsIDOMDOMImplementation.h"
#include "nsIDOMEventGroup.h"
#include "nsIDOMRange.h"
#include "nsIDocument.h"
#include "nsIDocumentEncoder.h"
#include "nsIDocumentViewer.h"
#include "nsIEventListenerManager.h"
#include "nsIFactory.h"
#include "nsFrameSelection.h"
#include "nsIFrameUtil.h"
#include "nsIFragmentContentSink.h"
#include "nsHTMLStyleSheet.h"
#include "nsIHTMLToTextSink.h"
#include "nsILayoutDebugger.h"
#include "nsINameSpaceManager.h"
#include "nsINodeInfo.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIPresShell.h"
#include "nsIRangeUtils.h"
#include "nsIScriptNameSpaceManager.h"
#include "nsISelection.h"
#include "nsIXBLService.h"
#include "nsCaret.h"
#include "nsPlainTextSerializer.h"
#include "mozSanitizingSerializer.h"
#include "nsXMLContentSerializer.h"
#include "nsXHTMLContentSerializer.h"
#include "nsRuleNode.h"
#include "nsWyciwygProtocolHandler.h"
#include "nsContentAreaDragDrop.h"
#include "nsContentList.h"
#include "nsSyncLoadService.h"
#include "nsBox.h"
#include "nsIFrameTraversal.h"
#include "nsLayoutCID.h"
#include "nsILanguageAtomService.h"
#include "nsStyleSheetService.h"
#include "nsXULPopupManager.h"
#include "nsFocusManager.h"
#include "nsIContentUtils.h"
#include "mozilla/Services.h"

#include "nsIEventListenerService.h"
#include "nsIFrameMessageManager.h"

// Transformiix stuff
#include "nsXPathEvaluator.h"
#include "txMozillaXSLTProcessor.h"
#include "txNodeSetAdaptor.h"
#include "nsXPath1Scheme.h"

#include "nsDOMParser.h"
#include "nsDOMSerializer.h"
#include "nsXMLHttpRequest.h"
#include "nsChannelPolicy.h"
#include "nsWebSocket.h"

// view stuff
#include "nsViewsCID.h"
#include "nsViewManager.h"
#include "nsContentCreatorFunctions.h"

// DOM includes
#include "nsDOMException.h"
#include "nsDOMFileReader.h"
#include "nsFormData.h"
#include "nsFileDataProtocolHandler.h"
#include "nsGlobalWindowCommands.h"
#include "nsIControllerCommandTable.h"
#include "nsJSProtocolHandler.h"
#include "nsScriptNameSpaceManager.h"
#include "nsIControllerContext.h"
#include "nsDOMScriptObjectFactory.h"
#include "nsDOMStorage.h"
#include "nsJSON.h"

// Editor stuff
#include "nsEditorCID.h"
#include "nsEditor.h"
#include "nsPlaintextEditor.h"
#include "nsEditorController.h" //CID
#include "nsIController.h"
#include "nsIControllerContext.h"
#include "nsIControllerCommandTable.h"

#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
#include "nsHTMLEditor.h"
#include "nsTextServicesDocument.h"
#include "nsTextServicesCID.h"
#endif

#include "nsScriptSecurityManager.h"
#include "nsPrincipal.h"
#include "nsSystemPrincipal.h"
#include "nsNullPrincipal.h"
#include "nsNetCID.h"
#include "nsINodeInfo.h"

#define NS_EDITORCOMMANDTABLE_CID \
{ 0x4f5e62b8, 0xd659, 0x4156, { 0x84, 0xfc, 0x2f, 0x60, 0x99, 0x40, 0x03, 0x69 }}

static NS_DEFINE_CID(kEditorCommandTableCID, NS_EDITORCOMMANDTABLE_CID);

NS_GENERIC_FACTORY_CONSTRUCTOR(nsPlaintextEditor)

// Constructor of a controller which is set up to use, internally, a
// singleton command-table pre-filled with editor commands.
static nsresult
nsEditorControllerConstructor(nsISupports *aOuter, REFNSIID aIID,
                                            void **aResult)
{
  nsresult rv;
  nsCOMPtr<nsIController> controller = do_CreateInstance("@mozilla.org/embedcomp/base-command-controller;1", &rv);
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIControllerCommandTable> editorCommandTable = do_GetService(kEditorCommandTableCID, &rv);
  if (NS_FAILED(rv)) return rv;
  
  // this guy is a singleton, so make it immutable
  editorCommandTable->MakeImmutable();
  
  nsCOMPtr<nsIControllerContext> controllerContext = do_QueryInterface(controller, &rv);
  if (NS_FAILED(rv)) return rv;
  
  rv = controllerContext->Init(editorCommandTable);
  if (NS_FAILED(rv)) return rv;
  
  return controller->QueryInterface(aIID, aResult);
}


// Constructor for a command-table pref-filled with editor commands
static nsresult
nsEditorCommandTableConstructor(nsISupports *aOuter, REFNSIID aIID,
                                            void **aResult)
{
  nsresult rv;
  nsCOMPtr<nsIControllerCommandTable> commandTable =
      do_CreateInstance(NS_CONTROLLERCOMMANDTABLE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) return rv;
  
  rv = nsEditorController::RegisterEditorCommands(commandTable);
  if (NS_FAILED(rv)) return rv;
  
  // we don't know here whether we're being created as an instance,
  // or a service, so we can't become immutable

  return commandTable->QueryInterface(aIID, aResult);
}


#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
NS_GENERIC_FACTORY_CONSTRUCTOR(nsTextServicesDocument)
#ifdef ENABLE_EDITOR_API_LOG
#include "nsHTMLEditorLog.h"
NS_GENERIC_FACTORY_CONSTRUCTOR(nsHTMLEditorLog)
#else
NS_GENERIC_FACTORY_CONSTRUCTOR(nsHTMLEditor)
#endif
#endif

#include "nsHTMLCanvasFrame.h"

#include "nsIDOMCanvasRenderingContext2D.h"
#include "nsICanvasRenderingContextWebGL.h"

class nsIDocumentLoaderFactory;

#define PRODUCT_NAME "Gecko"

#define NS_HTMLIMGELEMENT_CONTRACTID \
  "@mozilla.org/content/element/html;1?name=img"

#define NS_HTMLOPTIONELEMENT_CONTRACTID \
  "@mozilla.org/content/element/html;1?name=option"

#ifdef MOZ_MEDIA
#define NS_HTMLAUDIOELEMENT_CONTRACTID \
  "@mozilla.org/content/element/html;1?name=audio"
#endif

/* 0ddf4df8-4dbb-4133-8b79-9afb966514f5 */
#define NS_PLUGINDOCLOADERFACTORY_CID \
{ 0x0ddf4df8, 0x4dbb, 0x4133, { 0x8b, 0x79, 0x9a, 0xfb, 0x96, 0x65, 0x14, 0xf5 } }

#define NS_WINDOWCOMMANDTABLE_CID \
 { /* 0DE2FBFA-6B7F-11D7-BBBA-0003938A9D96 */        \
  0x0DE2FBFA, 0x6B7F, 0x11D7, {0xBB, 0xBA, 0x00, 0x03, 0x93, 0x8A, 0x9D, 0x96} }

static NS_DEFINE_CID(kWindowCommandTableCID, NS_WINDOWCOMMANDTABLE_CID);

#include "nsIBoxObject.h"

#ifdef MOZ_XUL
#include "inDOMView.h"
#endif /* MOZ_XUL */

#include "inDeepTreeWalker.h"
#include "inFlasher.h"
#include "inCSSValueSearch.h"
#include "inDOMUtils.h"

#ifdef MOZ_XUL
#include "nsIXULDocument.h"
#include "nsIXULPrototypeCache.h"
#include "nsIXULSortService.h"

nsresult
NS_NewXULContentBuilder(nsISupports* aOuter, REFNSIID aIID, void** aResult);

nsresult
NS_NewXULTreeBuilder(nsISupports* aOuter, REFNSIID aIID, void** aResult);
#endif

nsresult
NS_NewDOMImplementation(nsIDOMDOMImplementation**);

static void Shutdown();

#ifdef MOZ_XTF
#include "nsIXTFService.h"
#include "nsIXMLContentBuilder.h"
#endif

#include "nsGeolocation.h"
//XXX: bug 575206
#ifndef MOZ_WIDGET_UIKIT
#if defined(XP_UNIX)    || \
    defined(_WINDOWS)   || \
    defined(machintosh) || \
    defined(android)
#include "nsAccelerometerSystem.h"
#endif
#endif
#include "nsCSPService.h"

// Transformiix
/* {0C351177-0159-4500-86B0-A219DFDE4258} */
#define TRANSFORMIIX_XPATH1_SCHEME_CID \
{ 0xc351177, 0x159, 0x4500, { 0x86, 0xb0, 0xa2, 0x19, 0xdf, 0xde, 0x42, 0x58 } }

/* 5d5d92cd-6bf8-11d9-bf4a-000a95dc234c */
#define TRANSFORMIIX_NODESET_CID \
{ 0x5d5d92cd, 0x6bf8, 0x11d9, { 0xbf, 0x4a, 0x0, 0x0a, 0x95, 0xdc, 0x23, 0x4c } }

#define TRANSFORMIIX_NODESET_CONTRACTID \
"@mozilla.org/transformiix-nodeset;1"

NS_GENERIC_FACTORY_CONSTRUCTOR(nsXPath1SchemeProcessor)

// Factory Constructor
NS_GENERIC_FACTORY_CONSTRUCTOR(txMozillaXSLTProcessor)
NS_GENERIC_AGGREGATED_CONSTRUCTOR_INIT(nsXPathEvaluator, Init)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(txNodeSetAdaptor, Init)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsDOMSerializer)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsXMLHttpRequest, Init)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsWebSocket)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsWSProtocolHandler)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsWSSProtocolHandler)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsDOMFileReader, Init)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsFormData)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsFileDataProtocolHandler)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsDOMParser)
NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(nsDOMStorageManager,
                                         nsDOMStorageManager::GetInstance)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsChannelPolicy)
//XXX bug 575206
#ifndef MOZ_WIDGET_UIKIT
#if defined(XP_UNIX)    || \
    defined(_WINDOWS)   || \
    defined(machintosh) || \
    defined(android)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsAccelerometerSystem)
#endif
#endif

//-----------------------------------------------------------------------------

// Per bug 209804, it is necessary to observe the "xpcom-shutdown" event and
// perform shutdown of the layout modules at that time instead of waiting for
// our module destructor to run.  If we do not do this, then we risk holding
// references to objects in other component libraries that have already been
// shutdown (and possibly unloaded if 60709 is ever fixed).

class LayoutShutdownObserver : public nsIObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
};

NS_IMPL_ISUPPORTS1(LayoutShutdownObserver, nsIObserver)

NS_IMETHODIMP
LayoutShutdownObserver::Observe(nsISupports *aSubject,
                                const char *aTopic,
                                const PRUnichar *someData)
{
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID))
    Shutdown();
  return NS_OK;
}

//-----------------------------------------------------------------------------

static PRBool gInitialized = PR_FALSE;

// Perform our one-time intialization for this module

// static
nsresult
Initialize()
{
  if (gInitialized) {
    NS_ERROR("Recursive layout module initialization");
    return NS_ERROR_FAILURE;
  }

  NS_ASSERTION(sizeof(PtrBits) == sizeof(void *),
               "Eeek! You'll need to adjust the size of PtrBits to the size "
               "of a pointer on your platform.");

  gInitialized = PR_TRUE;

  nsresult rv;
  rv = xpcModuleCtor();
  if (NS_FAILED(rv))
    return rv;

  rv = nsLayoutStatics::Initialize();
  if (NS_FAILED(rv)) {
    Shutdown();
    return rv;
  }

  // Add our shutdown observer.
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();

  if (observerService) {
    LayoutShutdownObserver* observer = new LayoutShutdownObserver();

    if (!observer) {
      Shutdown();

      return NS_ERROR_OUT_OF_MEMORY;
    }

    observerService->AddObserver(observer, NS_XPCOM_SHUTDOWN_OBSERVER_ID, PR_FALSE);
  } else {
    NS_WARNING("Could not get an observer service.  We will leak on shutdown.");
  }

  return NS_OK;
}

// Shutdown this module, releasing all of the module resources

// static
void
Shutdown()
{
  NS_PRECONDITION(gInitialized, "module not initialized");
  if (!gInitialized)
    return;

  gInitialized = PR_FALSE;

  nsLayoutStatics::Release();
}

#ifdef NS_DEBUG
nsresult NS_NewFrameUtil(nsIFrameUtil** aResult);
nsresult NS_NewLayoutDebugger(nsILayoutDebugger** aResult);
#endif

nsresult NS_NewBoxObject(nsIBoxObject** aResult);

#ifdef MOZ_XUL
nsresult NS_NewListBoxObject(nsIBoxObject** aResult);
nsresult NS_NewScrollBoxObject(nsIBoxObject** aResult);
nsresult NS_NewMenuBoxObject(nsIBoxObject** aResult);
nsresult NS_NewPopupBoxObject(nsIBoxObject** aResult);
nsresult NS_NewContainerBoxObject(nsIBoxObject** aResult);
nsresult NS_NewTreeBoxObject(nsIBoxObject** aResult);
#endif

nsresult NS_NewCanvasRenderingContext2D(nsIDOMCanvasRenderingContext2D** aResult);
nsresult NS_NewCanvasRenderingContextWebGL(nsICanvasRenderingContextWebGL** aResult);

nsresult NS_CreateFrameTraversal(nsIFrameTraversal** aResult);

nsresult NS_NewSelection(nsFrameSelection** aResult);
nsresult NS_NewDomSelection(nsISelection** aResult);
nsresult NS_NewDocumentViewer(nsIDocumentViewer** aResult);
nsresult NS_NewRange(nsIDOMRange** aResult);
nsresult NS_NewRangeUtils(nsIRangeUtils** aResult);
nsresult NS_NewContentIterator(nsIContentIterator** aResult);
nsresult NS_NewPreContentIterator(nsIContentIterator** aResult);
nsresult NS_NewGenRegularIterator(nsIContentIterator** aResult);
nsresult NS_NewContentSubtreeIterator(nsIContentIterator** aResult);
nsresult NS_NewGenSubtreeIterator(nsIContentIterator** aInstancePtrResult);
nsresult NS_NewContentDocumentLoaderFactory(nsIDocumentLoaderFactory** aResult);
nsresult NS_NewHTMLCopyTextEncoder(nsIDocumentEncoder** aResult);
nsresult NS_NewTextEncoder(nsIDocumentEncoder** aResult);
nsresult NS_NewXBLService(nsIXBLService** aResult);
nsresult NS_NewContentPolicy(nsIContentPolicy** aResult);
nsresult NS_NewDOMEventGroup(nsIDOMEventGroup** aResult);

nsresult NS_NewEventListenerService(nsIEventListenerService** aResult);
nsresult NS_NewGlobalMessageManager(nsIChromeFrameMessageManager** aResult);

nsresult NS_NewXULControllers(nsISupports* aOuter, REFNSIID aIID, void** aResult);

#define MAKE_CTOR(ctor_, iface_, func_)                   \
static nsresult                                           \
ctor_(nsISupports* aOuter, REFNSIID aIID, void** aResult) \
{                                                         \
  *aResult = nsnull;                                      \
  if (aOuter)                                             \
    return NS_ERROR_NO_AGGREGATION;                       \
  iface_* inst;                                           \
  nsresult rv = func_(&inst);                             \
  if (NS_SUCCEEDED(rv)) {                                 \
    rv = inst->QueryInterface(aIID, aResult);             \
    NS_RELEASE(inst);                                     \
  }                                                       \
  return rv;                                              \
}

#ifdef DEBUG
MAKE_CTOR(CreateNewFrameUtil,             nsIFrameUtil,                NS_NewFrameUtil)
MAKE_CTOR(CreateNewLayoutDebugger,        nsILayoutDebugger,           NS_NewLayoutDebugger)
#endif

MAKE_CTOR(CreateNewFrameTraversal,      nsIFrameTraversal,      NS_CreateFrameTraversal)
MAKE_CTOR(CreateNewPresShell,           nsIPresShell,           NS_NewPresShell)
MAKE_CTOR(CreateNewBoxObject,           nsIBoxObject,           NS_NewBoxObject)

#ifdef MOZ_XUL
MAKE_CTOR(CreateNewListBoxObject,       nsIBoxObject,           NS_NewListBoxObject)
MAKE_CTOR(CreateNewMenuBoxObject,       nsIBoxObject,           NS_NewMenuBoxObject)
MAKE_CTOR(CreateNewPopupBoxObject,      nsIBoxObject,           NS_NewPopupBoxObject)
MAKE_CTOR(CreateNewScrollBoxObject,     nsIBoxObject,           NS_NewScrollBoxObject)
MAKE_CTOR(CreateNewTreeBoxObject,       nsIBoxObject,           NS_NewTreeBoxObject)
MAKE_CTOR(CreateNewContainerBoxObject,  nsIBoxObject,           NS_NewContainerBoxObject)
#endif // MOZ_XUL

#ifdef MOZ_XUL
NS_GENERIC_FACTORY_CONSTRUCTOR(inDOMView)
#endif
NS_GENERIC_FACTORY_CONSTRUCTOR(inDeepTreeWalker)
NS_GENERIC_FACTORY_CONSTRUCTOR(inFlasher)
NS_GENERIC_FACTORY_CONSTRUCTOR(inCSSValueSearch)
NS_GENERIC_FACTORY_CONSTRUCTOR(inDOMUtils)

MAKE_CTOR(CreateNameSpaceManager,         nsINameSpaceManager,         NS_GetNameSpaceManager)
MAKE_CTOR(CreateEventListenerManager,     nsIEventListenerManager,     NS_NewEventListenerManager)
MAKE_CTOR(CreateDOMEventGroup,            nsIDOMEventGroup,            NS_NewDOMEventGroup)
MAKE_CTOR(CreateDocumentViewer,           nsIDocumentViewer,           NS_NewDocumentViewer)
MAKE_CTOR(CreateHTMLDocument,             nsIDocument,                 NS_NewHTMLDocument)
MAKE_CTOR(CreateDOMImplementation,        nsIDOMDOMImplementation,     NS_NewDOMImplementation)
MAKE_CTOR(CreateXMLDocument,              nsIDocument,                 NS_NewXMLDocument)
#ifdef MOZ_SVG
MAKE_CTOR(CreateSVGDocument,              nsIDocument,                 NS_NewSVGDocument)
#endif
MAKE_CTOR(CreateImageDocument,            nsIDocument,                 NS_NewImageDocument)
MAKE_CTOR(CreateDOMSelection,             nsISelection,                NS_NewDomSelection)
MAKE_CTOR(CreateSelection,                nsFrameSelection,            NS_NewSelection)
MAKE_CTOR(CreateRange,                    nsIDOMRange,                 NS_NewRange)
MAKE_CTOR(CreateRangeUtils,               nsIRangeUtils,               NS_NewRangeUtils)
MAKE_CTOR(CreateContentIterator,          nsIContentIterator,          NS_NewContentIterator)
MAKE_CTOR(CreatePreContentIterator,       nsIContentIterator,          NS_NewPreContentIterator)
MAKE_CTOR(CreateSubtreeIterator,          nsIContentIterator,          NS_NewContentSubtreeIterator)
// CreateHTMLImgElement, see below
// CreateHTMLOptionElement, see below
// CreateHTMLAudioElement, see below
MAKE_CTOR(CreateTextEncoder,              nsIDocumentEncoder,          NS_NewTextEncoder)
MAKE_CTOR(CreateHTMLCopyTextEncoder,      nsIDocumentEncoder,          NS_NewHTMLCopyTextEncoder)
MAKE_CTOR(CreateXMLContentSerializer,     nsIContentSerializer,        NS_NewXMLContentSerializer)
MAKE_CTOR(CreateHTMLContentSerializer,    nsIContentSerializer,        NS_NewHTMLContentSerializer)
MAKE_CTOR(CreateXHTMLContentSerializer,   nsIContentSerializer,        NS_NewXHTMLContentSerializer)
MAKE_CTOR(CreatePlainTextSerializer,      nsIContentSerializer,        NS_NewPlainTextSerializer)
MAKE_CTOR(CreateHTMLFragmentSink,         nsIFragmentContentSink,      NS_NewHTMLFragmentContentSink)
MAKE_CTOR(CreateHTMLFragmentSink2,        nsIFragmentContentSink,      NS_NewHTMLFragmentContentSink2)
MAKE_CTOR(CreateHTMLParanoidFragmentSink, nsIFragmentContentSink,      NS_NewHTMLParanoidFragmentSink)
MAKE_CTOR(CreateHTMLParanoidFragmentSink2,nsIFragmentContentSink,      NS_NewHTMLParanoidFragmentSink2)
MAKE_CTOR(CreateXMLFragmentSink,          nsIFragmentContentSink,      NS_NewXMLFragmentContentSink)
MAKE_CTOR(CreateXMLFragmentSink2,         nsIFragmentContentSink,      NS_NewXMLFragmentContentSink2)
MAKE_CTOR(CreateXHTMLParanoidFragmentSink,nsIFragmentContentSink,      NS_NewXHTMLParanoidFragmentSink)
MAKE_CTOR(CreateXHTMLParanoidFragmentSink2,nsIFragmentContentSink,     NS_NewXHTMLParanoidFragmentSink2)
MAKE_CTOR(CreateSanitizingHTMLSerializer, nsIContentSerializer,        NS_NewSanitizingHTMLSerializer)
MAKE_CTOR(CreateXBLService,               nsIXBLService,               NS_NewXBLService)
MAKE_CTOR(CreateContentPolicy,            nsIContentPolicy,            NS_NewContentPolicy)
#ifdef MOZ_XUL
MAKE_CTOR(CreateXULSortService,           nsIXULSortService,           NS_NewXULSortService)
// NS_NewXULContentBuilder
// NS_NewXULTreeBuilder
MAKE_CTOR(CreateXULDocument,              nsIXULDocument,              NS_NewXULDocument)
// NS_NewXULControllers
// NS_NewXULPrototypeCache
MAKE_CTOR(CreateXULPopupManager,      nsISupports,      NS_NewXULPopupManager)
#endif
#ifdef MOZ_XTF
MAKE_CTOR(CreateXTFService,               nsIXTFService,               NS_NewXTFService)
MAKE_CTOR(CreateXMLContentBuilder,        nsIXMLContentBuilder,        NS_NewXMLContentBuilder)
#endif
MAKE_CTOR(CreateContentDLF,               nsIDocumentLoaderFactory,    NS_NewContentDocumentLoaderFactory)
MAKE_CTOR(CreateEventListenerService,     nsIEventListenerService,     NS_NewEventListenerService)
MAKE_CTOR(CreateGlobalMessageManager,     nsIChromeFrameMessageManager,NS_NewGlobalMessageManager)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsWyciwygProtocolHandler)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsDataDocumentContentPolicy)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsNoDataProtocolContentPolicy)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsSyncLoadService)
MAKE_CTOR(CreatePluginDocument,           nsIDocument,                 NS_NewPluginDocument)
#ifdef MOZ_MEDIA
MAKE_CTOR(CreateVideoDocument,            nsIDocument,                 NS_NewVideoDocument)
#endif
MAKE_CTOR(CreateFocusManager,             nsIFocusManager,      NS_NewFocusManager)

NS_GENERIC_FACTORY_CONSTRUCTOR(nsIContentUtils)

MAKE_CTOR(CreateCanvasRenderingContext2D, nsIDOMCanvasRenderingContext2D, NS_NewCanvasRenderingContext2D)
MAKE_CTOR(CreateCanvasRenderingContextWebGL, nsICanvasRenderingContextWebGL, NS_NewCanvasRenderingContextWebGL)

NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsStyleSheetService, Init)

NS_GENERIC_FACTORY_CONSTRUCTOR(nsJSURI)

// views are not refcounted, so this is the same as
// NS_GENERIC_FACTORY_CONSTRUCTOR without the NS_ADDREF/NS_RELEASE
#define NS_GENERIC_FACTORY_CONSTRUCTOR_NOREFS(_InstanceClass)                 \
static nsresult                                                               \
_InstanceClass##Constructor(nsISupports *aOuter, REFNSIID aIID,               \
                            void **aResult)                                   \
{                                                                             \
    nsresult rv;                                                              \
                                                                              \
    *aResult = NULL;                                                          \
    if (NULL != aOuter) {                                                     \
        rv = NS_ERROR_NO_AGGREGATION;                                         \
        return rv;                                                            \
    }                                                                         \
                                                                              \
    _InstanceClass * inst = new _InstanceClass();                             \
    if (NULL == inst) {                                                       \
        rv = NS_ERROR_OUT_OF_MEMORY;                                          \
        return rv;                                                            \
    }                                                                         \
    rv = inst->QueryInterface(aIID, aResult);                                 \
                                                                              \
    return rv;                                                                \
}                                                                             \

NS_GENERIC_FACTORY_CONSTRUCTOR(nsViewManager)

static nsresult
CreateHTMLImgElement(nsISupports* aOuter, REFNSIID aIID, void** aResult)
{
  *aResult = nsnull;
  if (aOuter)
    return NS_ERROR_NO_AGGREGATION;
  // Note! NS_NewHTMLImageElement is special cased to handle a null nodeinfo
  nsCOMPtr<nsINodeInfo> ni;
  nsIContent* inst = NS_NewHTMLImageElement(ni.forget());
  nsresult rv = NS_ERROR_OUT_OF_MEMORY;
  if (inst) {
    NS_ADDREF(inst);
    rv = inst->QueryInterface(aIID, aResult);
    NS_RELEASE(inst);
  }
  return rv;
}

static nsresult
CreateHTMLOptionElement(nsISupports* aOuter, REFNSIID aIID, void** aResult)
{
  *aResult = nsnull;
  if (aOuter)
    return NS_ERROR_NO_AGGREGATION;
  // Note! NS_NewHTMLOptionElement is special cased to handle a null nodeinfo
  nsCOMPtr<nsINodeInfo> ni;
  nsIContent* inst = NS_NewHTMLOptionElement(ni.forget());
  nsresult rv = NS_ERROR_OUT_OF_MEMORY;
  if (inst) {
    NS_ADDREF(inst);
    rv = inst->QueryInterface(aIID, aResult);
    NS_RELEASE(inst);
  }
  return rv;
}

#ifdef MOZ_MEDIA
static nsresult
CreateHTMLAudioElement(nsISupports* aOuter, REFNSIID aIID, void** aResult)
{
  *aResult = nsnull;
  if (aOuter)
    return NS_ERROR_NO_AGGREGATION;
  // Note! NS_NewHTMLAudioElement is special cased to handle a null nodeinfo
  nsCOMPtr<nsINodeInfo> ni;
  nsCOMPtr<nsIContent> inst(NS_NewHTMLAudioElement(ni.forget()));
  return inst ? inst->QueryInterface(aIID, aResult) : NS_ERROR_OUT_OF_MEMORY;
}
#endif

static nsresult
CreateWindowCommandTableConstructor(nsISupports *aOuter,
                                    REFNSIID aIID, void **aResult)
{
  nsresult rv;
  nsCOMPtr<nsIControllerCommandTable> commandTable =
      do_CreateInstance(NS_CONTROLLERCOMMANDTABLE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) return rv;

  rv = nsWindowCommandRegistration::RegisterWindowCommands(commandTable);
  if (NS_FAILED(rv)) return rv;

  return commandTable->QueryInterface(aIID, aResult);
}

static nsresult
CreateWindowControllerWithSingletonCommandTable(nsISupports *aOuter,
                                      REFNSIID aIID, void **aResult)
{
  nsresult rv;
  nsCOMPtr<nsIController> controller =
       do_CreateInstance("@mozilla.org/embedcomp/base-command-controller;1", &rv);

 if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIControllerCommandTable> windowCommandTable = do_GetService(kWindowCommandTableCID, &rv);
  if (NS_FAILED(rv)) return rv;

  // this is a singleton; make it immutable
  windowCommandTable->MakeImmutable();

  nsCOMPtr<nsIControllerContext> controllerContext = do_QueryInterface(controller, &rv);
  if (NS_FAILED(rv)) return rv;

  controllerContext->Init(windowCommandTable);
  if (NS_FAILED(rv)) return rv;

  return controller->QueryInterface(aIID, aResult);
}

NS_GENERIC_FACTORY_CONSTRUCTOR(nsDOMScriptObjectFactory)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsBaseDOMException)

#define NS_GEOLOCATION_CID \
  { 0x1E1C3FF, 0x94A, 0xD048, { 0x44, 0xB4, 0x62, 0xD2, 0x9C, 0x7B, 0x4F, 0x39 } }

NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsGeolocation, Init)

#define NS_GEOLOCATION_SERVICE_CID \
  { 0x404d02a, 0x1CA, 0xAAAB, { 0x47, 0x62, 0x94, 0x4b, 0x1b, 0xf2, 0xf7, 0xb5 } }

NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(nsGeolocationService, nsGeolocationService::GetGeolocationService)

NS_GENERIC_FACTORY_CONSTRUCTOR(CSPService)

NS_GENERIC_FACTORY_CONSTRUCTOR(nsPrincipal)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsSecurityNameSet)
NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(nsSystemPrincipal,
    nsScriptSecurityManager::SystemPrincipalSingletonConstructor)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsNullPrincipal, Init)

static nsresult
Construct_nsIScriptSecurityManager(nsISupports *aOuter, REFNSIID aIID, 
                                   void **aResult)
{
    if (!aResult)
        return NS_ERROR_NULL_POINTER;
    *aResult = nsnull;
    if (aOuter)
        return NS_ERROR_NO_AGGREGATION;
    nsScriptSecurityManager *obj = nsScriptSecurityManager::GetScriptSecurityManager();
    if (!obj) 
        return NS_ERROR_OUT_OF_MEMORY;
    if (NS_FAILED(obj->QueryInterface(aIID, aResult)))
        return NS_ERROR_FAILURE;
    return NS_OK;
}

#ifdef DEBUG
NS_DEFINE_NAMED_CID(NS_FRAME_UTIL_CID);
NS_DEFINE_NAMED_CID(NS_LAYOUT_DEBUGGER_CID);
#endif
NS_DEFINE_NAMED_CID(NS_FRAMETRAVERSAL_CID);
NS_DEFINE_NAMED_CID(NS_PRESSHELL_CID);
NS_DEFINE_NAMED_CID(NS_BOXOBJECT_CID);
#ifdef MOZ_XUL
NS_DEFINE_NAMED_CID(NS_LISTBOXOBJECT_CID);
NS_DEFINE_NAMED_CID(NS_MENUBOXOBJECT_CID);
NS_DEFINE_NAMED_CID(NS_POPUPBOXOBJECT_CID);
NS_DEFINE_NAMED_CID(NS_CONTAINERBOXOBJECT_CID);
NS_DEFINE_NAMED_CID(NS_SCROLLBOXOBJECT_CID);
NS_DEFINE_NAMED_CID(NS_TREEBOXOBJECT_CID);
#endif // MOZ_XUL
#ifdef MOZ_XUL
NS_DEFINE_NAMED_CID(IN_DOMVIEW_CID);
#endif
NS_DEFINE_NAMED_CID(IN_DEEPTREEWALKER_CID);
NS_DEFINE_NAMED_CID(IN_FLASHER_CID);
NS_DEFINE_NAMED_CID(IN_CSSVALUESEARCH_CID);
NS_DEFINE_NAMED_CID(IN_DOMUTILS_CID);
NS_DEFINE_NAMED_CID(NS_NAMESPACEMANAGER_CID);
NS_DEFINE_NAMED_CID(NS_EVENTLISTENERMANAGER_CID);
NS_DEFINE_NAMED_CID(NS_DOMEVENTGROUP_CID);
NS_DEFINE_NAMED_CID(NS_DOCUMENT_VIEWER_CID);
NS_DEFINE_NAMED_CID(NS_HTMLDOCUMENT_CID);
NS_DEFINE_NAMED_CID(NS_DOM_IMPLEMENTATION_CID);
NS_DEFINE_NAMED_CID(NS_XMLDOCUMENT_CID);
#ifdef MOZ_SVG
NS_DEFINE_NAMED_CID(NS_SVGDOCUMENT_CID);
#endif
NS_DEFINE_NAMED_CID(NS_IMAGEDOCUMENT_CID);
NS_DEFINE_NAMED_CID(NS_DOMSELECTION_CID);
NS_DEFINE_NAMED_CID(NS_FRAMESELECTION_CID);
NS_DEFINE_NAMED_CID(NS_RANGE_CID);
NS_DEFINE_NAMED_CID(NS_RANGEUTILS_CID);
NS_DEFINE_NAMED_CID(NS_CONTENTITERATOR_CID);
NS_DEFINE_NAMED_CID(NS_PRECONTENTITERATOR_CID);
NS_DEFINE_NAMED_CID(NS_SUBTREEITERATOR_CID);
NS_DEFINE_NAMED_CID(NS_HTMLIMAGEELEMENT_CID);
NS_DEFINE_NAMED_CID(NS_HTMLOPTIONELEMENT_CID);
#ifdef MOZ_MEDIA
NS_DEFINE_NAMED_CID(NS_HTMLAUDIOELEMENT_CID);
#endif
NS_DEFINE_NAMED_CID(NS_CANVASRENDERINGCONTEXT2D_CID);
NS_DEFINE_NAMED_CID(NS_CANVASRENDERINGCONTEXTWEBGL_CID);
NS_DEFINE_NAMED_CID(NS_TEXT_ENCODER_CID);
NS_DEFINE_NAMED_CID(NS_HTMLCOPY_TEXT_ENCODER_CID);
NS_DEFINE_NAMED_CID(NS_XMLCONTENTSERIALIZER_CID);
NS_DEFINE_NAMED_CID(NS_XHTMLCONTENTSERIALIZER_CID);
NS_DEFINE_NAMED_CID(NS_HTMLCONTENTSERIALIZER_CID);
NS_DEFINE_NAMED_CID(NS_PLAINTEXTSERIALIZER_CID);
NS_DEFINE_NAMED_CID(NS_HTMLFRAGMENTSINK_CID);
NS_DEFINE_NAMED_CID(NS_HTMLFRAGMENTSINK2_CID);
NS_DEFINE_NAMED_CID(NS_HTMLPARANOIDFRAGMENTSINK_CID);
NS_DEFINE_NAMED_CID(NS_HTMLPARANOIDFRAGMENTSINK2_CID);
NS_DEFINE_NAMED_CID(MOZ_SANITIZINGHTMLSERIALIZER_CID);
NS_DEFINE_NAMED_CID(NS_XMLFRAGMENTSINK_CID);
NS_DEFINE_NAMED_CID(NS_XMLFRAGMENTSINK2_CID);
NS_DEFINE_NAMED_CID(NS_XHTMLPARANOIDFRAGMENTSINK_CID);
NS_DEFINE_NAMED_CID(NS_XHTMLPARANOIDFRAGMENTSINK2_CID);
NS_DEFINE_NAMED_CID(NS_XBLSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_CONTENTPOLICY_CID);
NS_DEFINE_NAMED_CID(NS_DATADOCUMENTCONTENTPOLICY_CID);
NS_DEFINE_NAMED_CID(NS_NODATAPROTOCOLCONTENTPOLICY_CID);
NS_DEFINE_NAMED_CID(NS_XULCONTROLLERS_CID);
#ifdef MOZ_XUL
NS_DEFINE_NAMED_CID(NS_XULSORTSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_XULTEMPLATEBUILDER_CID);
NS_DEFINE_NAMED_CID(NS_XULTREEBUILDER_CID);
NS_DEFINE_NAMED_CID(NS_XULPOPUPMANAGER_CID);
NS_DEFINE_NAMED_CID(NS_XULDOCUMENT_CID);
NS_DEFINE_NAMED_CID(NS_XULPROTOTYPECACHE_CID);
#endif
#ifdef MOZ_XTF
NS_DEFINE_NAMED_CID(NS_XTFSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_XMLCONTENTBUILDER_CID);
#endif
NS_DEFINE_NAMED_CID(NS_CONTENT_DOCUMENT_LOADER_FACTORY_CID);
NS_DEFINE_NAMED_CID(NS_WYCIWYGPROTOCOLHANDLER_CID);
NS_DEFINE_NAMED_CID(NS_SYNCLOADDOMSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_DOM_SCRIPT_OBJECT_FACTORY_CID);
NS_DEFINE_NAMED_CID(NS_BASE_DOM_EXCEPTION_CID);
NS_DEFINE_NAMED_CID(NS_JSPROTOCOLHANDLER_CID);
NS_DEFINE_NAMED_CID(NS_JSURI_CID);
NS_DEFINE_NAMED_CID(NS_WINDOWCOMMANDTABLE_CID);
NS_DEFINE_NAMED_CID(NS_WINDOWCONTROLLER_CID);
NS_DEFINE_NAMED_CID(NS_VIEW_MANAGER_CID);
NS_DEFINE_NAMED_CID(NS_PLUGINDOCLOADERFACTORY_CID);
NS_DEFINE_NAMED_CID(NS_PLUGINDOCUMENT_CID);
#ifdef MOZ_MEDIA
NS_DEFINE_NAMED_CID(NS_VIDEODOCUMENT_CID);
#endif
NS_DEFINE_NAMED_CID(NS_STYLESHEETSERVICE_CID);
NS_DEFINE_NAMED_CID(TRANSFORMIIX_XSLT_PROCESSOR_CID);
NS_DEFINE_NAMED_CID(TRANSFORMIIX_XPATH_EVALUATOR_CID);
NS_DEFINE_NAMED_CID(TRANSFORMIIX_XPATH1_SCHEME_CID);
NS_DEFINE_NAMED_CID(TRANSFORMIIX_NODESET_CID);
NS_DEFINE_NAMED_CID(NS_XMLSERIALIZER_CID);
NS_DEFINE_NAMED_CID(NS_FILEREADER_CID);
NS_DEFINE_NAMED_CID(NS_FORMDATA_CID);
NS_DEFINE_NAMED_CID(NS_FILEDATAPROTOCOLHANDLER_CID);
NS_DEFINE_NAMED_CID(NS_XMLHTTPREQUEST_CID);
NS_DEFINE_NAMED_CID(NS_WEBSOCKET_CID);
NS_DEFINE_NAMED_CID(NS_WSPROTOCOLHANDLER_CID);
NS_DEFINE_NAMED_CID(NS_WSSPROTOCOLHANDLER_CID);
NS_DEFINE_NAMED_CID(NS_DOMPARSER_CID);
NS_DEFINE_NAMED_CID(NS_DOMSTORAGE_CID);
NS_DEFINE_NAMED_CID(NS_DOMSTORAGE2_CID);
NS_DEFINE_NAMED_CID(NS_DOMSTORAGEMANAGER_CID);
NS_DEFINE_NAMED_CID(NS_DOMJSON_CID);
NS_DEFINE_NAMED_CID(NS_TEXTEDITOR_CID);
#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
#ifdef ENABLE_EDITOR_API_LOG
NS_DEFINE_NAMED_CID(NS_HTMLEDITOR_CID);
#else
NS_DEFINE_NAMED_CID(NS_HTMLEDITOR_CID);
#endif
#endif
NS_DEFINE_NAMED_CID(NS_EDITORCONTROLLER_CID);
NS_DEFINE_NAMED_CID(NS_EDITORCOMMANDTABLE_CID);
#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
NS_DEFINE_NAMED_CID(NS_TEXTSERVICESDOCUMENT_CID);
#endif
NS_DEFINE_NAMED_CID(NS_GEOLOCATION_SERVICE_CID);
NS_DEFINE_NAMED_CID(NS_GEOLOCATION_CID);
NS_DEFINE_NAMED_CID(NS_FOCUSMANAGER_CID);
NS_DEFINE_NAMED_CID(NS_ICONTENTUTILS_CID);
NS_DEFINE_NAMED_CID(CSPSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_EVENTLISTENERSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_GLOBALMESSAGEMANAGER_CID);
NS_DEFINE_NAMED_CID(NSCHANNELPOLICY_CID);
NS_DEFINE_NAMED_CID(NS_SCRIPTSECURITYMANAGER_CID);
NS_DEFINE_NAMED_CID(NS_PRINCIPAL_CID);
NS_DEFINE_NAMED_CID(NS_SYSTEMPRINCIPAL_CID);
NS_DEFINE_NAMED_CID(NS_NULLPRINCIPAL_CID);
NS_DEFINE_NAMED_CID(NS_SECURITYNAMESET_CID);

//XXX bug 575206
#ifndef MOZ_WIDGET_UIKIT
#if defined(XP_UNIX)    || \
    defined(_WINDOWS)   || \
    defined(machintosh) || \
    defined(android)
NS_DEFINE_NAMED_CID(NS_ACCELEROMETER_CID);
#endif
#endif

static const mozilla::Module::CIDEntry kLayoutCIDs[] = {
  XPCONNECT_CIDENTRIES
#ifdef DEBUG
  { &kNS_FRAME_UTIL_CID, false, NULL, CreateNewFrameUtil },
  { &kNS_LAYOUT_DEBUGGER_CID, false, NULL, CreateNewLayoutDebugger },
#endif
  { &kNS_FRAMETRAVERSAL_CID, false, NULL, CreateNewFrameTraversal },
  { &kNS_PRESSHELL_CID, false, NULL, CreateNewPresShell },
  { &kNS_BOXOBJECT_CID, false, NULL, CreateNewBoxObject },
#ifdef MOZ_XUL
  { &kNS_LISTBOXOBJECT_CID, false, NULL, CreateNewListBoxObject },
  { &kNS_MENUBOXOBJECT_CID, false, NULL, CreateNewMenuBoxObject },
  { &kNS_POPUPBOXOBJECT_CID, false, NULL, CreateNewPopupBoxObject },
  { &kNS_CONTAINERBOXOBJECT_CID, false, NULL, CreateNewContainerBoxObject },
  { &kNS_SCROLLBOXOBJECT_CID, false, NULL, CreateNewScrollBoxObject },
  { &kNS_TREEBOXOBJECT_CID, false, NULL, CreateNewTreeBoxObject },
#endif // MOZ_XUL
#ifdef MOZ_XUL
  { &kIN_DOMVIEW_CID, false, NULL, inDOMViewConstructor },
#endif
  { &kIN_DEEPTREEWALKER_CID, false, NULL, inDeepTreeWalkerConstructor },
  { &kIN_FLASHER_CID, false, NULL, inFlasherConstructor },
  { &kIN_CSSVALUESEARCH_CID, false, NULL, inCSSValueSearchConstructor },
  { &kIN_DOMUTILS_CID, false, NULL, inDOMUtilsConstructor },
  { &kNS_NAMESPACEMANAGER_CID, false, NULL, CreateNameSpaceManager },
  { &kNS_EVENTLISTENERMANAGER_CID, false, NULL, CreateEventListenerManager },
  { &kNS_DOMEVENTGROUP_CID, false, NULL, CreateDOMEventGroup },
  { &kNS_DOCUMENT_VIEWER_CID, false, NULL, CreateDocumentViewer },
  { &kNS_HTMLDOCUMENT_CID, false, NULL, CreateHTMLDocument },
  { &kNS_DOM_IMPLEMENTATION_CID, false, NULL, CreateDOMImplementation },
  { &kNS_XMLDOCUMENT_CID, false, NULL, CreateXMLDocument },
#ifdef MOZ_SVG
  { &kNS_SVGDOCUMENT_CID, false, NULL, CreateSVGDocument },
#endif
  { &kNS_IMAGEDOCUMENT_CID, false, NULL, CreateImageDocument },
  { &kNS_DOMSELECTION_CID, false, NULL, CreateDOMSelection },
  { &kNS_FRAMESELECTION_CID, false, NULL, CreateSelection },
  { &kNS_RANGE_CID, false, NULL, CreateRange },
  { &kNS_RANGEUTILS_CID, false, NULL, CreateRangeUtils },
  { &kNS_CONTENTITERATOR_CID, false, NULL, CreateContentIterator },
  { &kNS_PRECONTENTITERATOR_CID, false, NULL, CreatePreContentIterator },
  { &kNS_SUBTREEITERATOR_CID, false, NULL, CreateSubtreeIterator },
  { &kNS_HTMLIMAGEELEMENT_CID, false, NULL, CreateHTMLImgElement },
  { &kNS_HTMLOPTIONELEMENT_CID, false, NULL, CreateHTMLOptionElement },
#ifdef MOZ_MEDIA
  { &kNS_HTMLAUDIOELEMENT_CID, false, NULL, CreateHTMLAudioElement },
#endif
  { &kNS_CANVASRENDERINGCONTEXT2D_CID, false, NULL, CreateCanvasRenderingContext2D },
  { &kNS_CANVASRENDERINGCONTEXTWEBGL_CID, false, NULL, CreateCanvasRenderingContextWebGL },
  { &kNS_TEXT_ENCODER_CID, false, NULL, CreateTextEncoder },
  { &kNS_HTMLCOPY_TEXT_ENCODER_CID, false, NULL, CreateHTMLCopyTextEncoder },
  { &kNS_XMLCONTENTSERIALIZER_CID, false, NULL, CreateXMLContentSerializer },
  { &kNS_HTMLCONTENTSERIALIZER_CID, false, NULL, CreateHTMLContentSerializer },
  { &kNS_XHTMLCONTENTSERIALIZER_CID, false, NULL, CreateXHTMLContentSerializer },
  { &kNS_PLAINTEXTSERIALIZER_CID, false, NULL, CreatePlainTextSerializer },
  { &kNS_HTMLFRAGMENTSINK_CID, false, NULL, CreateHTMLFragmentSink },
  { &kNS_HTMLFRAGMENTSINK2_CID, false, NULL, CreateHTMLFragmentSink2 },
  { &kNS_HTMLPARANOIDFRAGMENTSINK_CID, false, NULL, CreateHTMLParanoidFragmentSink },
  { &kNS_HTMLPARANOIDFRAGMENTSINK2_CID, false, NULL, CreateHTMLParanoidFragmentSink2 },
  { &kMOZ_SANITIZINGHTMLSERIALIZER_CID, false, NULL, CreateSanitizingHTMLSerializer },
  { &kNS_XMLFRAGMENTSINK_CID, false, NULL, CreateXMLFragmentSink },
  { &kNS_XMLFRAGMENTSINK2_CID, false, NULL, CreateXMLFragmentSink2 },
  { &kNS_XHTMLPARANOIDFRAGMENTSINK_CID, false, NULL, CreateXHTMLParanoidFragmentSink },
  { &kNS_XHTMLPARANOIDFRAGMENTSINK2_CID, false, NULL, CreateXHTMLParanoidFragmentSink2 },
  { &kNS_XBLSERVICE_CID, false, NULL, CreateXBLService },
  { &kNS_CONTENTPOLICY_CID, false, NULL, CreateContentPolicy },
  { &kNS_DATADOCUMENTCONTENTPOLICY_CID, false, NULL, nsDataDocumentContentPolicyConstructor },
  { &kNS_NODATAPROTOCOLCONTENTPOLICY_CID, false, NULL, nsNoDataProtocolContentPolicyConstructor },
  { &kNS_XULCONTROLLERS_CID, false, NULL, NS_NewXULControllers },
#ifdef MOZ_XUL
  { &kNS_XULSORTSERVICE_CID, false, NULL, CreateXULSortService },
  { &kNS_XULTEMPLATEBUILDER_CID, false, NULL, NS_NewXULContentBuilder },
  { &kNS_XULTREEBUILDER_CID, false, NULL, NS_NewXULTreeBuilder },
  { &kNS_XULPOPUPMANAGER_CID, false, NULL, CreateXULPopupManager },
  { &kNS_XULDOCUMENT_CID, false, NULL, CreateXULDocument },
  { &kNS_XULPROTOTYPECACHE_CID, false, NULL, NS_NewXULPrototypeCache },
#endif
#ifdef MOZ_XTF
  { &kNS_XTFSERVICE_CID, false, NULL, CreateXTFService },
  { &kNS_XMLCONTENTBUILDER_CID, false, NULL, CreateXMLContentBuilder },
#endif
  { &kNS_CONTENT_DOCUMENT_LOADER_FACTORY_CID, false, NULL, CreateContentDLF },
  { &kNS_WYCIWYGPROTOCOLHANDLER_CID, false, NULL, nsWyciwygProtocolHandlerConstructor },
  { &kNS_SYNCLOADDOMSERVICE_CID, false, NULL, nsSyncLoadServiceConstructor },
  { &kNS_DOM_SCRIPT_OBJECT_FACTORY_CID, false, NULL, nsDOMScriptObjectFactoryConstructor },
  { &kNS_BASE_DOM_EXCEPTION_CID, false, NULL, nsBaseDOMExceptionConstructor },
  { &kNS_JSPROTOCOLHANDLER_CID, false, NULL, nsJSProtocolHandler::Create },
  { &kNS_JSURI_CID, false, NULL, nsJSURIConstructor },
  { &kNS_WINDOWCOMMANDTABLE_CID, false, NULL, CreateWindowCommandTableConstructor },
  { &kNS_WINDOWCONTROLLER_CID, false, NULL, CreateWindowControllerWithSingletonCommandTable },
  { &kNS_VIEW_MANAGER_CID, false, NULL, nsViewManagerConstructor },
  { &kNS_PLUGINDOCLOADERFACTORY_CID, false, NULL, CreateContentDLF },
  { &kNS_PLUGINDOCUMENT_CID, false, NULL, CreatePluginDocument },
#ifdef MOZ_MEDIA
  { &kNS_VIDEODOCUMENT_CID, false, NULL, CreateVideoDocument },
#endif
  { &kNS_STYLESHEETSERVICE_CID, false, NULL, nsStyleSheetServiceConstructor },
  { &kTRANSFORMIIX_XSLT_PROCESSOR_CID, false, NULL, txMozillaXSLTProcessorConstructor },
  { &kTRANSFORMIIX_XPATH_EVALUATOR_CID, false, NULL, nsXPathEvaluatorConstructor },
  { &kTRANSFORMIIX_XPATH1_SCHEME_CID, false, NULL, nsXPath1SchemeProcessorConstructor },
  { &kTRANSFORMIIX_NODESET_CID, false, NULL, txNodeSetAdaptorConstructor },
  { &kNS_XMLSERIALIZER_CID, false, NULL, nsDOMSerializerConstructor },
  { &kNS_FILEREADER_CID, false, NULL, nsDOMFileReaderConstructor },
  { &kNS_FORMDATA_CID, false, NULL, nsFormDataConstructor },
  { &kNS_FILEDATAPROTOCOLHANDLER_CID, false, NULL, nsFileDataProtocolHandlerConstructor },
  { &kNS_XMLHTTPREQUEST_CID, false, NULL, nsXMLHttpRequestConstructor },
  { &kNS_WEBSOCKET_CID, false, NULL, nsWebSocketConstructor },
  { &kNS_WSPROTOCOLHANDLER_CID, false, NULL, nsWSProtocolHandlerConstructor },
  { &kNS_WSSPROTOCOLHANDLER_CID, false, NULL, nsWSSProtocolHandlerConstructor },
  { &kNS_DOMPARSER_CID, false, NULL, nsDOMParserConstructor },
  { &kNS_DOMSTORAGE_CID, false, NULL, NS_NewDOMStorage },
  { &kNS_DOMSTORAGE2_CID, false, NULL, NS_NewDOMStorage2 },
  { &kNS_DOMSTORAGEMANAGER_CID, false, NULL, nsDOMStorageManagerConstructor },
  { &kNS_DOMJSON_CID, false, NULL, NS_NewJSON },
  { &kNS_TEXTEDITOR_CID, false, NULL, nsPlaintextEditorConstructor },
#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
#ifdef ENABLE_EDITOR_API_LOG
  { &kNS_HTMLEDITOR_CID, false, NULL, nsHTMLEditorLogConstructor },
#else
  { &kNS_HTMLEDITOR_CID, false, NULL, nsHTMLEditorConstructor },
#endif
#endif
  { &kNS_EDITORCONTROLLER_CID, false, NULL, nsEditorControllerConstructor },
  { &kNS_EDITORCOMMANDTABLE_CID, false, NULL, nsEditorCommandTableConstructor },
#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
  { &kNS_TEXTSERVICESDOCUMENT_CID, false, NULL, nsTextServicesDocumentConstructor },
#endif
  { &kNS_GEOLOCATION_SERVICE_CID, false, NULL, nsGeolocationServiceConstructor },
  { &kNS_GEOLOCATION_CID, false, NULL, nsGeolocationConstructor },
  { &kNS_FOCUSMANAGER_CID, false, NULL, CreateFocusManager },
  { &kNS_ICONTENTUTILS_CID, false, NULL, nsIContentUtilsConstructor },
  { &kCSPSERVICE_CID, false, NULL, CSPServiceConstructor },
  { &kNS_EVENTLISTENERSERVICE_CID, false, NULL, CreateEventListenerService },
  { &kNS_GLOBALMESSAGEMANAGER_CID, false, NULL, CreateGlobalMessageManager },
  { &kNSCHANNELPOLICY_CID, false, NULL, nsChannelPolicyConstructor },
  { &kNS_SCRIPTSECURITYMANAGER_CID, false, NULL, Construct_nsIScriptSecurityManager },
  { &kNS_PRINCIPAL_CID, false, NULL, nsPrincipalConstructor },
  { &kNS_SYSTEMPRINCIPAL_CID, false, NULL, nsSystemPrincipalConstructor },
  { &kNS_NULLPRINCIPAL_CID, false, NULL, nsNullPrincipalConstructor },
  { &kNS_SECURITYNAMESET_CID, false, NULL, nsSecurityNameSetConstructor },
//XXX bug 575206
#ifndef MOZ_WIDGET_UIKIT
#if defined(XP_UNIX)    || \
    defined(_WINDOWS)   || \
    defined(machintosh) || \
    defined(android)
  { &kNS_ACCELEROMETER_CID, false, NULL, nsAccelerometerSystemConstructor },
#endif
#endif
  { NULL }
};

static const mozilla::Module::ContractIDEntry kLayoutContracts[] = {
  XPCONNECT_CONTRACTS
  { "@mozilla.org/layout/xul-boxobject;1", &kNS_BOXOBJECT_CID },
#ifdef MOZ_XUL
  { "@mozilla.org/layout/xul-boxobject-listbox;1", &kNS_LISTBOXOBJECT_CID },
  { "@mozilla.org/layout/xul-boxobject-menu;1", &kNS_MENUBOXOBJECT_CID },
  { "@mozilla.org/layout/xul-boxobject-popup;1", &kNS_POPUPBOXOBJECT_CID },
  { "@mozilla.org/layout/xul-boxobject-container;1", &kNS_CONTAINERBOXOBJECT_CID },
  { "@mozilla.org/layout/xul-boxobject-scrollbox;1", &kNS_SCROLLBOXOBJECT_CID },
  { "@mozilla.org/layout/xul-boxobject-tree;1", &kNS_TREEBOXOBJECT_CID },
#endif // MOZ_XUL
#ifdef MOZ_XUL
  { "@mozilla.org/inspector/dom-view;1", &kIN_DOMVIEW_CID },
#endif
  { "@mozilla.org/inspector/deep-tree-walker;1", &kIN_DEEPTREEWALKER_CID },
  { "@mozilla.org/inspector/flasher;1", &kIN_FLASHER_CID },
  { "@mozilla.org/inspector/search;1?type=cssvalue", &kIN_CSSVALUESEARCH_CID },
  { "@mozilla.org/inspector/dom-utils;1", &kIN_DOMUTILS_CID },
  { NS_NAMESPACEMANAGER_CONTRACTID, &kNS_NAMESPACEMANAGER_CID },
  { "@mozilla.org/xml/xml-document;1", &kNS_XMLDOCUMENT_CID },
#ifdef MOZ_SVG
  { "@mozilla.org/svg/svg-document;1", &kNS_SVGDOCUMENT_CID },
#endif
  { "@mozilla.org/content/dom-selection;1", &kNS_DOMSELECTION_CID },
  { "@mozilla.org/content/range;1", &kNS_RANGE_CID },
  { "@mozilla.org/content/range-utils;1", &kNS_RANGEUTILS_CID },
  { "@mozilla.org/content/post-content-iterator;1", &kNS_CONTENTITERATOR_CID },
  { "@mozilla.org/content/pre-content-iterator;1", &kNS_PRECONTENTITERATOR_CID },
  { "@mozilla.org/content/subtree-content-iterator;1", &kNS_SUBTREEITERATOR_CID },
  { NS_HTMLIMGELEMENT_CONTRACTID, &kNS_HTMLIMAGEELEMENT_CID },
  { NS_HTMLOPTIONELEMENT_CONTRACTID, &kNS_HTMLOPTIONELEMENT_CID },
#ifdef MOZ_MEDIA
  { NS_HTMLAUDIOELEMENT_CONTRACTID, &kNS_HTMLAUDIOELEMENT_CID },
#endif
  { "@mozilla.org/content/canvas-rendering-context;1?id=2d", &kNS_CANVASRENDERINGCONTEXT2D_CID },
  { "@mozilla.org/content/canvas-rendering-context;1?id=moz-webgl", &kNS_CANVASRENDERINGCONTEXTWEBGL_CID },
  { "@mozilla.org/content/canvas-rendering-context;1?id=experimental-webgl", &kNS_CANVASRENDERINGCONTEXTWEBGL_CID },
  { NS_DOC_ENCODER_CONTRACTID_BASE "text/xml", &kNS_TEXT_ENCODER_CID },
  { NS_DOC_ENCODER_CONTRACTID_BASE "application/xml", &kNS_TEXT_ENCODER_CID },
  { NS_DOC_ENCODER_CONTRACTID_BASE "application/xhtml+xml", &kNS_TEXT_ENCODER_CID },
#ifdef MOZ_SVG
  { NS_DOC_ENCODER_CONTRACTID_BASE "image/svg+xml", &kNS_TEXT_ENCODER_CID },
#endif
  { NS_DOC_ENCODER_CONTRACTID_BASE "text/html", &kNS_TEXT_ENCODER_CID },
  { NS_DOC_ENCODER_CONTRACTID_BASE "text/plain", &kNS_TEXT_ENCODER_CID },
  { NS_HTMLCOPY_ENCODER_CONTRACTID, &kNS_HTMLCOPY_TEXT_ENCODER_CID },
  { NS_CONTENTSERIALIZER_CONTRACTID_PREFIX "text/xml", &kNS_XMLCONTENTSERIALIZER_CID },
  { NS_CONTENTSERIALIZER_CONTRACTID_PREFIX "application/xml", &kNS_XMLCONTENTSERIALIZER_CID },
  { NS_CONTENTSERIALIZER_CONTRACTID_PREFIX "application/xhtml+xml", &kNS_XHTMLCONTENTSERIALIZER_CID },
#ifdef MOZ_SVG
  { NS_CONTENTSERIALIZER_CONTRACTID_PREFIX "image/svg+xml", &kNS_XMLCONTENTSERIALIZER_CID },
#endif
  { NS_CONTENTSERIALIZER_CONTRACTID_PREFIX "text/html", &kNS_HTMLCONTENTSERIALIZER_CID },
  { NS_CONTENTSERIALIZER_CONTRACTID_PREFIX "application/vnd.mozilla.xul+xml", &kNS_XMLCONTENTSERIALIZER_CID },
  { NS_CONTENTSERIALIZER_CONTRACTID_PREFIX "text/plain", &kNS_PLAINTEXTSERIALIZER_CID },
  { NS_PLAINTEXTSINK_CONTRACTID, &kNS_PLAINTEXTSERIALIZER_CID },
  { NS_HTMLFRAGMENTSINK_CONTRACTID, &kNS_HTMLFRAGMENTSINK_CID },
  { NS_HTMLFRAGMENTSINK2_CONTRACTID, &kNS_HTMLFRAGMENTSINK2_CID },
  { NS_HTMLPARANOIDFRAGMENTSINK_CONTRACTID, &kNS_HTMLPARANOIDFRAGMENTSINK_CID },
  { NS_HTMLPARANOIDFRAGMENTSINK2_CONTRACTID, &kNS_HTMLPARANOIDFRAGMENTSINK2_CID },
  { MOZ_SANITIZINGHTMLSERIALIZER_CONTRACTID, &kMOZ_SANITIZINGHTMLSERIALIZER_CID },
  { NS_XMLFRAGMENTSINK_CONTRACTID, &kNS_XMLFRAGMENTSINK_CID },
  { NS_XMLFRAGMENTSINK2_CONTRACTID, &kNS_XMLFRAGMENTSINK2_CID },
  { NS_XHTMLPARANOIDFRAGMENTSINK_CONTRACTID, &kNS_XHTMLPARANOIDFRAGMENTSINK_CID },
  { NS_XHTMLPARANOIDFRAGMENTSINK2_CONTRACTID, &kNS_XHTMLPARANOIDFRAGMENTSINK2_CID },
  { "@mozilla.org/xbl;1", &kNS_XBLSERVICE_CID },
  { NS_CONTENTPOLICY_CONTRACTID, &kNS_CONTENTPOLICY_CID },
  { NS_DATADOCUMENTCONTENTPOLICY_CONTRACTID, &kNS_DATADOCUMENTCONTENTPOLICY_CID },
  { NS_NODATAPROTOCOLCONTENTPOLICY_CONTRACTID, &kNS_NODATAPROTOCOLCONTENTPOLICY_CID },
  { "@mozilla.org/xul/xul-controllers;1", &kNS_XULCONTROLLERS_CID },
#ifdef MOZ_XUL
  { "@mozilla.org/xul/xul-sort-service;1", &kNS_XULSORTSERVICE_CID },
  { "@mozilla.org/xul/xul-template-builder;1", &kNS_XULTEMPLATEBUILDER_CID },
  { "@mozilla.org/xul/xul-tree-builder;1", &kNS_XULTREEBUILDER_CID },
  { "@mozilla.org/xul/xul-popup-manager;1", &kNS_XULPOPUPMANAGER_CID },
  { "@mozilla.org/xul/xul-document;1", &kNS_XULDOCUMENT_CID },
  { "@mozilla.org/xul/xul-prototype-cache;1", &kNS_XULPROTOTYPECACHE_CID },
#endif
#ifdef MOZ_XTF
  { NS_XTFSERVICE_CONTRACTID, &kNS_XTFSERVICE_CID },
  { NS_XMLCONTENTBUILDER_CONTRACTID, &kNS_XMLCONTENTBUILDER_CID },
#endif
  { CONTENT_DLF_CONTRACTID, &kNS_CONTENT_DOCUMENT_LOADER_FACTORY_CID },
  { NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "wyciwyg", &kNS_WYCIWYGPROTOCOLHANDLER_CID },
  { NS_SYNCLOADDOMSERVICE_CONTRACTID, &kNS_SYNCLOADDOMSERVICE_CID },
  { NS_JSPROTOCOLHANDLER_CONTRACTID, &kNS_JSPROTOCOLHANDLER_CID },
  { NS_WINDOWCONTROLLER_CONTRACTID, &kNS_WINDOWCONTROLLER_CID },
  { "@mozilla.org/view-manager;1", &kNS_VIEW_MANAGER_CID },
  { PLUGIN_DLF_CONTRACTID, &kNS_PLUGINDOCLOADERFACTORY_CID },
  { NS_STYLESHEETSERVICE_CONTRACTID, &kNS_STYLESHEETSERVICE_CID },
  { TRANSFORMIIX_XSLT_PROCESSOR_CONTRACTID, &kTRANSFORMIIX_XSLT_PROCESSOR_CID },
  { NS_XPATH_EVALUATOR_CONTRACTID, &kTRANSFORMIIX_XPATH_EVALUATOR_CID },
  { NS_XPOINTER_SCHEME_PROCESSOR_BASE "xpath1", &kTRANSFORMIIX_XPATH1_SCHEME_CID },
  { TRANSFORMIIX_NODESET_CONTRACTID, &kTRANSFORMIIX_NODESET_CID },
  { NS_XMLSERIALIZER_CONTRACTID, &kNS_XMLSERIALIZER_CID },
  { NS_FILEREADER_CONTRACTID, &kNS_FILEREADER_CID },
  { NS_FORMDATA_CONTRACTID, &kNS_FORMDATA_CID },
  { NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX FILEDATA_SCHEME, &kNS_FILEDATAPROTOCOLHANDLER_CID },
  { NS_XMLHTTPREQUEST_CONTRACTID, &kNS_XMLHTTPREQUEST_CID },
  { NS_WEBSOCKET_CONTRACTID, &kNS_WEBSOCKET_CID },
  { NS_WSPROTOCOLHANDLER_CONTRACTID, &kNS_WSPROTOCOLHANDLER_CID },
  { NS_WSSPROTOCOLHANDLER_CONTRACTID, &kNS_WSSPROTOCOLHANDLER_CID },
  { NS_DOMPARSER_CONTRACTID, &kNS_DOMPARSER_CID },
  { "@mozilla.org/dom/storage;1", &kNS_DOMSTORAGE_CID },
  { "@mozilla.org/dom/storage;2", &kNS_DOMSTORAGE2_CID },
  { "@mozilla.org/dom/storagemanager;1", &kNS_DOMSTORAGEMANAGER_CID },
  { "@mozilla.org/dom/json;1", &kNS_DOMJSON_CID },
  { "@mozilla.org/editor/texteditor;1", &kNS_TEXTEDITOR_CID },
#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
#ifdef ENABLE_EDITOR_API_LOG
  { "@mozilla.org/editor/htmleditor;1", &kNS_HTMLEDITOR_CID },
#else
  { "@mozilla.org/editor/htmleditor;1", &kNS_HTMLEDITOR_CID },
#endif
#endif
  { "@mozilla.org/editor/editorcontroller;1", &kNS_EDITORCONTROLLER_CID },
  { "", &kNS_EDITORCOMMANDTABLE_CID },
#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
  { "@mozilla.org/textservices/textservicesdocument;1", &kNS_TEXTSERVICESDOCUMENT_CID },
#endif
  { "@mozilla.org/geolocation/service;1", &kNS_GEOLOCATION_SERVICE_CID },
  { "@mozilla.org/geolocation;1", &kNS_GEOLOCATION_CID },
  { "@mozilla.org/focus-manager;1", &kNS_FOCUSMANAGER_CID },
  { "@mozilla.org/content/contentutils;1", &kNS_ICONTENTUTILS_CID },
  { CSPSERVICE_CONTRACTID, &kCSPSERVICE_CID },
  { NS_EVENTLISTENERSERVICE_CONTRACTID, &kNS_EVENTLISTENERSERVICE_CID },
  { NS_GLOBALMESSAGEMANAGER_CONTRACTID, &kNS_GLOBALMESSAGEMANAGER_CID },
  { NSCHANNELPOLICY_CONTRACTID, &kNSCHANNELPOLICY_CID },
  { NS_SCRIPTSECURITYMANAGER_CONTRACTID, &kNS_SCRIPTSECURITYMANAGER_CID },
  { NS_GLOBAL_CHANNELEVENTSINK_CONTRACTID, &kNS_SCRIPTSECURITYMANAGER_CID },
  { NS_PRINCIPAL_CONTRACTID, &kNS_PRINCIPAL_CID },
  { NS_SYSTEMPRINCIPAL_CONTRACTID, &kNS_SYSTEMPRINCIPAL_CID },
  { NS_NULLPRINCIPAL_CONTRACTID, &kNS_NULLPRINCIPAL_CID },
  { NS_SECURITYNAMESET_CONTRACTID, &kNS_SECURITYNAMESET_CID },
//XXX bug 575206
#ifndef MOZ_WIDGET_UIKIT
#if defined(XP_UNIX)    || \
    defined(_WINDOWS)   || \
    defined(machintosh) || \
    defined(android)
  { NS_ACCELEROMETER_CONTRACTID, &kNS_ACCELEROMETER_CID },
#endif
#endif
  { NULL }
};

static const mozilla::Module::CategoryEntry kLayoutCategories[] = {
  XPCONNECT_CATEGORIES
  { JAVASCRIPT_GLOBAL_CONSTRUCTOR_CATEGORY, "Image", NS_HTMLIMGELEMENT_CONTRACTID },
  { JAVASCRIPT_GLOBAL_CONSTRUCTOR_PROTO_ALIAS_CATEGORY, "Image", "HTMLImageElement" },
  { JAVASCRIPT_GLOBAL_CONSTRUCTOR_CATEGORY, "Option", NS_HTMLOPTIONELEMENT_CONTRACTID },
  { JAVASCRIPT_GLOBAL_CONSTRUCTOR_PROTO_ALIAS_CATEGORY, "Option", "HTMLOptionElement" },
#ifdef MOZ_MEDIA
  { JAVASCRIPT_GLOBAL_CONSTRUCTOR_CATEGORY, "Audio", NS_HTMLAUDIOELEMENT_CONTRACTID },
  { JAVASCRIPT_GLOBAL_CONSTRUCTOR_PROTO_ALIAS_CATEGORY, "Audio", "HTMLAudioElement" },
#endif
  { "content-policy", NS_DATADOCUMENTCONTENTPOLICY_CONTRACTID, NS_DATADOCUMENTCONTENTPOLICY_CONTRACTID },
  { "content-policy", NS_NODATAPROTOCOLCONTENTPOLICY_CONTRACTID, NS_NODATAPROTOCOLCONTENTPOLICY_CONTRACTID },
  { "content-policy", "CSPService", CSPSERVICE_CONTRACTID },
  { "net-channel-event-sinks", "CSPService", CSPSERVICE_CONTRACTID },
  { JAVASCRIPT_GLOBAL_STATIC_NAMESET_CATEGORY, "PrivilegeManager", NS_SECURITYNAMESET_CONTRACTID },
  { "app-startup", "Script Security Manager", "service," NS_SCRIPTSECURITYMANAGER_CONTRACTID },
  CONTENTDLF_CATEGORIES
  { NULL }
};

static void
LayoutModuleDtor()
{
  xpcModuleDtor();

  nsScriptSecurityManager::Shutdown();
}

static const mozilla::Module kLayoutModule = {
  mozilla::Module::kVersion,
  kLayoutCIDs,
  kLayoutContracts,
  kLayoutCategories,
  NULL,
  Initialize,
  LayoutModuleDtor
};
  
NSMODULE_DEFN(nsLayoutModule) = &kLayoutModule;
