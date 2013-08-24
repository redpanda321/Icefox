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
 * The Original Code is the Mozilla platform.
 *
 * The Initial Developer of the Original Code is
 * Benjamin Smedberg <benjamin@smedbergs.us>.
 *
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Mozilla Foundation <http://www.mozilla.org/>. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsLayoutStatics.h"
#include "nscore.h"

#include "nsAttrValue.h"
#include "nsAutoCopyListener.h"
#include "nsColorNames.h"
#include "nsComputedDOMStyle.h"
#include "nsContentDLF.h"
#include "nsContentUtils.h"
#include "nsCSSAnonBoxes.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSKeywords.h"
#include "nsCSSParser.h"
#include "nsCSSProps.h"
#include "nsCSSPseudoClasses.h"
#include "nsCSSPseudoElements.h"
#include "nsCSSRendering.h"
#include "nsCSSScanner.h"
#include "nsDOMAttribute.h"
#include "nsDOMClassInfo.h"
#include "nsEventListenerManager.h"
#include "nsFrame.h"
#include "nsGenericElement.h"  // for nsDOMEventRTTearoff
#include "nsGlobalWindow.h"
#include "nsGkAtoms.h"
#include "nsImageFrame.h"
#include "nsLayoutStylesheetCache.h"
#include "nsNodeInfo.h"
#include "nsRange.h"
#include "nsRegion.h"
#include "nsRepeatService.h"
#include "nsFloatManager.h"
#include "nsSprocketLayout.h"
#include "nsStackLayout.h"
#include "nsStyleSet.h"
#include "nsTextControlFrame.h"
#include "nsXBLWindowKeyHandler.h"
#include "txMozillaXSLTProcessor.h"
#include "nsDOMStorage.h"
#include "nsCellMap.h"
#include "nsTextFrameTextRunCache.h"
#include "nsCCUncollectableMarker.h"
#include "nsTextFragment.h"
#include "nsCSSRuleProcessor.h"
#include "nsXMLHttpRequest.h"
#include "nsWebSocket.h"
#include "nsDOMThreadService.h"
#include "nsHTMLDNSPrefetch.h"
#include "nsHtml5Module.h"
#include "nsCrossSiteListenerProxy.h"
#include "nsFocusManager.h"
#include "nsFrameList.h"
#include "nsListControlFrame.h"
#include "nsHTMLInputElement.h"
#ifdef MOZ_SVG
#include "nsSVGUtils.h"
#endif

#ifdef MOZ_XUL
#include "nsXULPopupManager.h"
#include "nsXULContentUtils.h"
#include "nsXULElement.h"
#include "nsXULPrototypeCache.h"
#include "nsXULTooltipListener.h"

#include "inDOMView.h"
#endif

#ifdef MOZ_MATHML
#include "nsMathMLAtoms.h"
#include "nsMathMLOperators.h"
#endif

#ifdef MOZ_SVG
PRBool NS_SVGEnabled();
#endif

#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
#include "nsHTMLEditor.h"
#include "nsTextServicesDocument.h"
#endif

#ifdef MOZ_MEDIA
#include "nsMediaDecoder.h"
#include "nsHTMLMediaElement.h"
#endif

#ifdef MOZ_SYDNEYAUDIO
#include "nsAudioStream.h"
#endif

#include "nsError.h"

#include "nsCycleCollector.h"
#include "nsJSEnvironment.h"
#include "nsContentSink.h"
#include "nsFrameMessageManager.h"

extern void NS_ShutdownChainItemPool();

nsrefcnt nsLayoutStatics::sLayoutStaticRefcnt = 0;

nsresult
nsLayoutStatics::Initialize()
{
  NS_ASSERTION(sLayoutStaticRefcnt == 0,
               "nsLayoutStatics isn't zero!");

  sLayoutStaticRefcnt = 1;
  NS_LOG_ADDREF(&sLayoutStaticRefcnt, sLayoutStaticRefcnt,
                "nsLayoutStatics", 1);

  nsresult rv;

  // Register all of our atoms once
  nsCSSAnonBoxes::AddRefAtoms();
  nsCSSPseudoClasses::AddRefAtoms();
  nsCSSPseudoElements::AddRefAtoms();
  nsCSSKeywords::AddRefTable();
  nsCSSProps::AddRefTable();
  nsColorNames::AddRefTable();
  nsGkAtoms::AddRefAtoms();

  nsJSRuntime::Startup();
  rv = nsRegion::InitStatic();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsRegion");
    return rv;
  }

  rv = nsContentUtils::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsContentUtils");
    return rv;
  }

  rv = nsAttrValue::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsAttrValue");
    return rv;
  }

  rv = nsTextFragment::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsTextFragment");
    return rv;
  }

  rv = nsCellMap::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsCellMap");
    return rv;
  }

  rv = nsCSSRendering::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsCSSRendering");
    return rv;
  }

  rv = nsTextFrameTextRunCache::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize textframe textrun cache");
    return rv;
  }

  rv = nsHTMLDNSPrefetch::Initialize();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize HTML DNS prefetch");
    return rv;
  }

#ifdef MOZ_XUL
  rv = nsXULContentUtils::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsXULContentUtils");
    return rv;
  }

  inDOMView::InitAtoms();

#endif

#ifdef MOZ_MATHML
  nsMathMLOperators::AddRefTable();
#endif

#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
  nsEditProperty::RegisterAtoms();
  nsTextServicesDocument::RegisterAtoms();
#endif

#ifdef DEBUG
  nsFrame::DisplayReflowStartup();
#endif
  nsDOMAttribute::Initialize();

  rv = txMozillaXSLTProcessor::Startup();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize txMozillaXSLTProcessor");
    return rv;
  }

  rv = nsDOMStorageManager::Initialize();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsDOMStorageManager");
    return rv;
  }

  rv = nsCCUncollectableMarker::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsCCUncollectableMarker");
    return rv;
  }

  rv = nsCSSRuleProcessor::Startup();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsCSSRuleProcessor");
    return rv;
  }

#ifdef MOZ_XUL
  rv = nsXULPopupManager::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsXULPopupManager");
    return rv;
  }
#endif

  rv = nsFocusManager::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsFocusManager");
    return rv;
  }

#ifdef MOZ_SYDNEYAUDIO
  nsAudioStream::InitLibrary();
#endif

  nsContentSink::InitializeStatics();
  nsHtml5Module::InitializeStatics();
  nsIPresShell::InitializeStatics();

  nsCrossSiteListenerProxy::Startup();

  rv = nsFrameList::Init();
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not initialize nsFrameList");
    return rv;
  }

  NS_SealStaticAtomTable();

  return NS_OK;
}

void
nsLayoutStatics::Shutdown()
{
  nsFrameScriptExecutor::Shutdown();
  nsFocusManager::Shutdown();
#ifdef MOZ_XUL
  nsXULPopupManager::Shutdown();
#endif
  nsDOMStorageManager::Shutdown();
  txMozillaXSLTProcessor::Shutdown();
  nsDOMAttribute::Shutdown();
  nsDOMEventRTTearoff::Shutdown();
  nsEventListenerManager::Shutdown();
  nsComputedDOMStyle::Shutdown();
  nsCSSParser::Shutdown();
  nsCSSRuleProcessor::Shutdown();
  nsTextFrameTextRunCache::Shutdown();
  nsHTMLDNSPrefetch::Shutdown();
  nsCSSRendering::Shutdown();
#ifdef DEBUG
  nsFrame::DisplayReflowShutdown();
#endif
  nsCellMap::Shutdown();
  nsFrame::ShutdownLayerActivityTimer();

  // Release all of our atoms
  nsColorNames::ReleaseTable();
  nsCSSProps::ReleaseTable();
  nsCSSKeywords::ReleaseTable();
  nsRepeatService::Shutdown();
  nsStackLayout::Shutdown();
  nsBox::Shutdown();

#ifdef MOZ_XUL
  nsXULContentUtils::Finish();
  nsXULElement::ReleaseGlobals();
  nsXULPrototypeCache::ReleaseGlobals();
  nsSprocketLayout::Shutdown();
#endif

#ifdef MOZ_MATHML
  nsMathMLOperators::ReleaseTable();
#endif

  nsCSSFrameConstructor::ReleaseGlobals();
  nsFloatManager::Shutdown();
  nsImageFrame::ReleaseGlobals();

  nsCSSScanner::ReleaseGlobals();

  NS_IF_RELEASE(nsRuleNode::gLangService);

  nsTextFragment::Shutdown();

  nsAttrValue::Shutdown();
  nsContentUtils::Shutdown();
  nsNodeInfo::ClearCache();
  nsLayoutStylesheetCache::Shutdown();
  NS_NameSpaceManagerShutdown();

  nsJSRuntime::Shutdown();
  nsGlobalWindow::ShutDown();
  nsDOMClassInfo::ShutDown();
  nsListControlFrame::Shutdown();
  nsXBLWindowKeyHandler::ShutDown();
  nsAutoCopyListener::Shutdown();

#ifndef MOZILLA_PLAINTEXT_EDITOR_ONLY
  nsHTMLEditor::Shutdown();
  nsTextServicesDocument::Shutdown();
#endif

  nsDOMThreadService::Shutdown();

#ifdef MOZ_SYDNEYAUDIO
  nsAudioStream::ShutdownLibrary();
#endif

  nsXMLHttpRequest::ShutdownACCache();
  
  nsWebSocket::ReleaseGlobals();
  
  nsIPresShell::ReleaseStatics();

  nsHtml5Module::ReleaseStatics();

  nsRegion::ShutdownStatic();

  NS_ShutdownChainItemPool();

  nsFrameList::Shutdown();

  nsHTMLInputElement::DestroyUploadLastDir();
}
