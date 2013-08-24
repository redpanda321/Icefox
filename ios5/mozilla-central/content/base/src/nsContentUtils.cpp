/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=78: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* A namespace class for static layout utilities. */

#include "mozilla/Util.h"

#include "jsapi.h"
#include "jsdbgapi.h"
#include "jsfriendapi.h"

#include "nsJSUtils.h"
#include "nsCOMPtr.h"
#include "nsAString.h"
#include "nsPrintfCString.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptContext.h"
#include "nsDOMCID.h"
#include "nsContentUtils.h"
#include "nsIXPConnect.h"
#include "nsIContent.h"
#include "mozilla/dom/Element.h"
#include "nsIDocument.h"
#include "nsINodeInfo.h"
#include "nsIIdleService.h"
#include "nsIDOMDocument.h"
#include "nsIDOMNodeList.h"
#include "nsIDOMNode.h"
#include "nsIIOService.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsIScriptSecurityManager.h"
#include "nsPIDOMWindow.h"
#include "nsIJSContextStack.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsParserCIID.h"
#include "nsIParser.h"
#include "nsIFragmentContentSink.h"
#include "nsIContentSink.h"
#include "nsContentList.h"
#include "nsIHTMLDocument.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsIDOMHTMLElement.h"
#include "nsIForm.h"
#include "nsIFormControl.h"
#include "nsGkAtoms.h"
#include "imgIDecoderObserver.h"
#include "imgIRequest.h"
#include "imgIContainer.h"
#include "imgILoader.h"
#include "nsDocShellCID.h"
#include "nsIImageLoadingContent.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsILoadGroup.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsContentPolicyUtils.h"
#include "nsNodeInfoManager.h"
#include "nsCRT.h"
#include "nsIDOMEvent.h"
#include "nsIDOMEventTarget.h"
#ifdef MOZ_XTF
#include "nsIXTFService.h"
static NS_DEFINE_CID(kXTFServiceCID, NS_XTFSERVICE_CID);
#endif
#include "nsIMIMEService.h"
#include "nsLWBrkCIID.h"
#include "nsILineBreaker.h"
#include "nsIWordBreaker.h"
#include "nsUnicodeProperties.h"
#include "harfbuzz/hb.h"
#include "nsIJSRuntimeService.h"
#include "nsBindingManager.h"
#include "nsIURI.h"
#include "nsIURL.h"
#include "nsICharsetConverterManager.h"
#include "nsEventListenerManager.h"
#include "nsAttrName.h"
#include "nsIDOMUserDataHandler.h"
#include "nsContentCreatorFunctions.h"
#include "nsMutationEvent.h"
#include "nsIMEStateManager.h"
#include "nsContentErrors.h"
#include "nsUnicharUtilCIID.h"
#include "nsINativeKeyBindings.h"
#include "nsIDOMNSEvent.h"
#include "nsXULPopupManager.h"
#include "nsIPermissionManager.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsNullPrincipal.h"
#include "nsIRunnable.h"
#include "nsDOMJSUtils.h"
#include "nsGenericHTMLElement.h"
#include "nsAttrValue.h"
#include "nsReferencedElement.h"
#include "nsIDragService.h"
#include "nsIChannelEventSink.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIOfflineCacheUpdate.h"
#include "nsCPrefetchService.h"
#include "nsIChromeRegistry.h"
#include "nsEventDispatcher.h"
#include "nsIDOMXULCommandEvent.h"
#include "nsDOMDataTransfer.h"
#include "nsHtml5Module.h"
#include "nsPresContext.h"
#include "nsLayoutStatics.h"
#include "nsFocusManager.h"
#include "nsTextEditorState.h"
#include "nsIPluginHost.h"
#include "nsICategoryManager.h"
#include "nsIViewManager.h"
#include "nsEventStateManager.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsParserConstants.h"
#include "nsIWebNavigation.h"
#include "nsTextFragment.h"
#include "mozilla/Selection.h"

#ifdef IBMBIDI
#include "nsIBidiKeyboard.h"
#endif
#include "nsCycleCollectionParticipant.h"

// for ReportToConsole
#include "nsIStringBundle.h"
#include "nsIScriptError.h"
#include "nsIConsoleService.h"

#include "mozAutoDocUpdate.h"
#include "imgICache.h"
#include "xpcprivate.h" // nsXPConnect
#include "nsScriptSecurityManager.h"
#include "nsIChannelPolicy.h"
#include "nsChannelPolicy.h"
#include "nsIContentSecurityPolicy.h"
#include "nsContentDLF.h"
#ifdef MOZ_MEDIA
#include "nsHTMLMediaElement.h"
#endif
#include "nsDOMTouchEvent.h"
#include "nsIContentViewer.h"
#include "nsIObjectLoadingContent.h"
#include "nsCCUncollectableMarker.h"
#include "mozilla/Base64.h"
#include "mozilla/Preferences.h"
#include "nsDOMMutationObserver.h"
#include "nsIDOMDocumentType.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsICharsetDetector.h"
#include "nsICharsetDetectionObserver.h"
#include "nsIPlatformCharset.h"
#include "nsIEditor.h"
#include "nsIEditorDocShell.h"
#include "mozilla/Attributes.h"
#include "nsIParserService.h"
#include "nsIDOMScriptObjectFactory.h"

#include "nsWrapperCacheInlines.h"

extern "C" int MOZ_XMLTranslateEntity(const char* ptr, const char* end,
                                      const char** next, PRUnichar* result);
extern "C" int MOZ_XMLCheckQName(const char* ptr, const char* end,
                                 int ns_aware, const char** colon);

using namespace mozilla::dom;
using namespace mozilla::layers;
using namespace mozilla::widget;
using namespace mozilla;

const char kLoadAsData[] = "loadAsData";

nsIDOMScriptObjectFactory *nsContentUtils::sDOMScriptObjectFactory = nsnull;
nsIXPConnect *nsContentUtils::sXPConnect;
nsIScriptSecurityManager *nsContentUtils::sSecurityManager;
nsIThreadJSContextStack *nsContentUtils::sThreadJSContextStack;
nsIParserService *nsContentUtils::sParserService = nsnull;
nsINameSpaceManager *nsContentUtils::sNameSpaceManager;
nsIIOService *nsContentUtils::sIOService;
#ifdef MOZ_XTF
nsIXTFService *nsContentUtils::sXTFService = nsnull;
#endif
imgILoader *nsContentUtils::sImgLoader;
imgICache *nsContentUtils::sImgCache;
nsIConsoleService *nsContentUtils::sConsoleService;
nsDataHashtable<nsISupportsHashKey, EventNameMapping>* nsContentUtils::sAtomEventTable = nsnull;
nsDataHashtable<nsStringHashKey, EventNameMapping>* nsContentUtils::sStringEventTable = nsnull;
nsCOMArray<nsIAtom>* nsContentUtils::sUserDefinedEvents = nsnull;
nsIStringBundleService *nsContentUtils::sStringBundleService;
nsIStringBundle *nsContentUtils::sStringBundles[PropertiesFile_COUNT];
nsIContentPolicy *nsContentUtils::sContentPolicyService;
bool nsContentUtils::sTriedToGetContentPolicy = false;
nsILineBreaker *nsContentUtils::sLineBreaker;
nsIWordBreaker *nsContentUtils::sWordBreaker;
PRUint32 nsContentUtils::sJSGCThingRootCount;
#ifdef IBMBIDI
nsIBidiKeyboard *nsContentUtils::sBidiKeyboard = nsnull;
#endif
PRUint32 nsContentUtils::sScriptBlockerCount = 0;
#ifdef DEBUG
PRUint32 nsContentUtils::sDOMNodeRemovedSuppressCount = 0;
#endif
PRUint32 nsContentUtils::sMicroTaskLevel = 0;
nsTArray< nsCOMPtr<nsIRunnable> >* nsContentUtils::sBlockedScriptRunners = nsnull;
PRUint32 nsContentUtils::sRunnersCountAtFirstBlocker = 0;
nsIInterfaceRequestor* nsContentUtils::sSameOriginChecker = nsnull;

bool nsContentUtils::sIsHandlingKeyBoardEvent = false;
bool nsContentUtils::sAllowXULXBL_for_file = false;

nsString* nsContentUtils::sShiftText = nsnull;
nsString* nsContentUtils::sControlText = nsnull;
nsString* nsContentUtils::sMetaText = nsnull;
nsString* nsContentUtils::sAltText = nsnull;
nsString* nsContentUtils::sModifierSeparator = nsnull;

bool nsContentUtils::sInitialized = false;
bool nsContentUtils::sIsFullScreenApiEnabled = false;
bool nsContentUtils::sTrustedFullScreenOnly = true;
bool nsContentUtils::sIsIdleObserverAPIEnabled = false;

PRUint32 nsContentUtils::sHandlingInputTimeout = 1000;

nsHtml5StringParser* nsContentUtils::sHTMLFragmentParser = nsnull;
nsIParser* nsContentUtils::sXMLFragmentParser = nsnull;
nsIFragmentContentSink* nsContentUtils::sXMLFragmentSink = nsnull;
bool nsContentUtils::sFragmentParsingActive = false;

namespace {

/**
 * Default values for the ViewportInfo structure.
 */
static const float    kViewportMinScale = 0.0;
static const float    kViewportMaxScale = 10.0;
static const PRUint32 kViewportMinWidth = 200;
static const PRUint32 kViewportMaxWidth = 10000;
static const PRUint32 kViewportMinHeight = 223;
static const PRUint32 kViewportMaxHeight = 10000;
static const PRInt32  kViewportDefaultScreenWidth = 980;

static const char kJSStackContractID[] = "@mozilla.org/js/xpc/ContextStack;1";
static NS_DEFINE_CID(kParserServiceCID, NS_PARSERSERVICE_CID);
static NS_DEFINE_CID(kCParserCID, NS_PARSER_CID);

static PLDHashTable sEventListenerManagersHash;

class EventListenerManagerMapEntry : public PLDHashEntryHdr
{
public:
  EventListenerManagerMapEntry(const void *aKey)
    : mKey(aKey)
  {
  }

  ~EventListenerManagerMapEntry()
  {
    NS_ASSERTION(!mListenerManager, "caller must release and disconnect ELM");
  }

private:
  const void *mKey; // must be first, to look like PLDHashEntryStub

public:
  nsRefPtr<nsEventListenerManager> mListenerManager;
};

static bool
EventListenerManagerHashInitEntry(PLDHashTable *table, PLDHashEntryHdr *entry,
                                  const void *key)
{
  // Initialize the entry with placement new
  new (entry) EventListenerManagerMapEntry(key);
  return true;
}

static void
EventListenerManagerHashClearEntry(PLDHashTable *table, PLDHashEntryHdr *entry)
{
  EventListenerManagerMapEntry *lm =
    static_cast<EventListenerManagerMapEntry *>(entry);

  // Let the EventListenerManagerMapEntry clean itself up...
  lm->~EventListenerManagerMapEntry();
}

class SameOriginChecker MOZ_FINAL : public nsIChannelEventSink,
                                    public nsIInterfaceRequestor
{
  NS_DECL_ISUPPORTS
  NS_DECL_NSICHANNELEVENTSINK
  NS_DECL_NSIINTERFACEREQUESTOR
};

class CharsetDetectionObserver MOZ_FINAL : public nsICharsetDetectionObserver
{
public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD Notify(const char *aCharset, nsDetectionConfident aConf)
  {
    mCharset = aCharset;
    return NS_OK;
  }

  const nsACString& GetResult() const
  {
    return mCharset;
  }

private:
  nsCString mCharset;
};

} // anonymous namespace

/* static */
TimeDuration
nsContentUtils::HandlingUserInputTimeout()
{
  return TimeDuration::FromMilliseconds(sHandlingInputTimeout);
}

// static
nsresult
nsContentUtils::Init()
{
  if (sInitialized) {
    NS_WARNING("Init() called twice");

    return NS_OK;
  }

  nsresult rv = NS_GetNameSpaceManager(&sNameSpaceManager);
  NS_ENSURE_SUCCESS(rv, rv);

  nsXPConnect* xpconnect = nsXPConnect::GetXPConnect();
  NS_ENSURE_TRUE(xpconnect, NS_ERROR_FAILURE);

  sXPConnect = xpconnect;
  sThreadJSContextStack = xpconnect;

  sSecurityManager = nsScriptSecurityManager::GetScriptSecurityManager();
  if(!sSecurityManager)
    return NS_ERROR_FAILURE;
  NS_ADDREF(sSecurityManager);

  rv = CallGetService(NS_IOSERVICE_CONTRACTID, &sIOService);
  if (NS_FAILED(rv)) {
    // This makes life easier, but we can live without it.

    sIOService = nsnull;
  }

  rv = CallGetService(NS_LBRK_CONTRACTID, &sLineBreaker);
  NS_ENSURE_SUCCESS(rv, rv);
  
  rv = CallGetService(NS_WBRK_CONTRACTID, &sWordBreaker);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!InitializeEventTable())
    return NS_ERROR_FAILURE;

  if (!sEventListenerManagersHash.ops) {
    static PLDHashTableOps hash_table_ops =
    {
      PL_DHashAllocTable,
      PL_DHashFreeTable,
      PL_DHashVoidPtrKeyStub,
      PL_DHashMatchEntryStub,
      PL_DHashMoveEntryStub,
      EventListenerManagerHashClearEntry,
      PL_DHashFinalizeStub,
      EventListenerManagerHashInitEntry
    };

    if (!PL_DHashTableInit(&sEventListenerManagersHash, &hash_table_ops,
                           nsnull, sizeof(EventListenerManagerMapEntry), 16)) {
      sEventListenerManagersHash.ops = nsnull;

      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  sBlockedScriptRunners = new nsTArray< nsCOMPtr<nsIRunnable> >;

  Preferences::AddBoolVarCache(&sAllowXULXBL_for_file,
                               "dom.allow_XUL_XBL_for_file");

  Preferences::AddBoolVarCache(&sIsFullScreenApiEnabled,
                               "full-screen-api.enabled");

  Preferences::AddBoolVarCache(&sTrustedFullScreenOnly,
                               "full-screen-api.allow-trusted-requests-only");

  sIsIdleObserverAPIEnabled = Preferences::GetBool("dom.idle-observers-api.enabled", true);

  Preferences::AddUintVarCache(&sHandlingInputTimeout,
                               "dom.event.handling-user-input-time-limit",
                               1000);

  nsGenericElement::InitCCCallbacks();

  sInitialized = true;

  return NS_OK;
}

void
nsContentUtils::GetShiftText(nsAString& text)
{
  if (!sShiftText)
    InitializeModifierStrings();
  text.Assign(*sShiftText);
}

void
nsContentUtils::GetControlText(nsAString& text)
{
  if (!sControlText)
    InitializeModifierStrings();
  text.Assign(*sControlText);
}

void
nsContentUtils::GetMetaText(nsAString& text)
{
  if (!sMetaText)
    InitializeModifierStrings();
  text.Assign(*sMetaText);
}

void
nsContentUtils::GetAltText(nsAString& text)
{
  if (!sAltText)
    InitializeModifierStrings();
  text.Assign(*sAltText);
}

void
nsContentUtils::GetModifierSeparatorText(nsAString& text)
{
  if (!sModifierSeparator)
    InitializeModifierStrings();
  text.Assign(*sModifierSeparator);
}

void
nsContentUtils::InitializeModifierStrings()
{
  //load the display strings for the keyboard accelerators
  nsCOMPtr<nsIStringBundleService> bundleService =
    mozilla::services::GetStringBundleService();
  nsCOMPtr<nsIStringBundle> bundle;
  DebugOnly<nsresult> rv = NS_OK;
  if (bundleService) {
    rv = bundleService->CreateBundle( "chrome://global-platform/locale/platformKeys.properties",
                                      getter_AddRefs(bundle));
  }
  
  NS_ASSERTION(NS_SUCCEEDED(rv) && bundle, "chrome://global/locale/platformKeys.properties could not be loaded");
  nsXPIDLString shiftModifier;
  nsXPIDLString metaModifier;
  nsXPIDLString altModifier;
  nsXPIDLString controlModifier;
  nsXPIDLString modifierSeparator;
  if (bundle) {
    //macs use symbols for each modifier key, so fetch each from the bundle, which also covers i18n
    bundle->GetStringFromName(NS_LITERAL_STRING("VK_SHIFT").get(), getter_Copies(shiftModifier));
    bundle->GetStringFromName(NS_LITERAL_STRING("VK_META").get(), getter_Copies(metaModifier));
    bundle->GetStringFromName(NS_LITERAL_STRING("VK_ALT").get(), getter_Copies(altModifier));
    bundle->GetStringFromName(NS_LITERAL_STRING("VK_CONTROL").get(), getter_Copies(controlModifier));
    bundle->GetStringFromName(NS_LITERAL_STRING("MODIFIER_SEPARATOR").get(), getter_Copies(modifierSeparator));
  }
  //if any of these don't exist, we get  an empty string
  sShiftText = new nsString(shiftModifier);
  sMetaText = new nsString(metaModifier);
  sAltText = new nsString(altModifier);
  sControlText = new nsString(controlModifier);
  sModifierSeparator = new nsString(modifierSeparator);  
}

bool nsContentUtils::sImgLoaderInitialized;

void
nsContentUtils::InitImgLoader()
{
  sImgLoaderInitialized = true;

  // Ignore failure and just don't load images
  nsresult rv = CallGetService("@mozilla.org/image/loader;1", &sImgLoader);
  if (NS_FAILED(rv)) {
    // no image loading for us.  Oh, well.
    sImgLoader = nsnull;
    sImgCache = nsnull;
  } else {
    if (NS_FAILED(CallGetService("@mozilla.org/image/cache;1", &sImgCache )))
      sImgCache = nsnull;
  }
}

bool
nsContentUtils::InitializeEventTable() {
  NS_ASSERTION(!sAtomEventTable, "EventTable already initialized!");
  NS_ASSERTION(!sStringEventTable, "EventTable already initialized!");

  static const EventNameMapping eventArray[] = {
#define EVENT(name_,  _id, _type, _struct)          \
    { nsGkAtoms::on##name_, _id, _type, _struct },
#define WINDOW_ONLY_EVENT EVENT
#define NON_IDL_EVENT EVENT
#include "nsEventNameList.h"
#undef WINDOW_ONLY_EVENT
#undef EVENT
    { nsnull }
  };

  sAtomEventTable = new nsDataHashtable<nsISupportsHashKey, EventNameMapping>;
  sStringEventTable = new nsDataHashtable<nsStringHashKey, EventNameMapping>;
  sUserDefinedEvents = new nsCOMArray<nsIAtom>(64);
  sAtomEventTable->Init(int(ArrayLength(eventArray) / 0.75) + 1);
  sStringEventTable->Init(int(ArrayLength(eventArray) / 0.75) + 1);

  // Subtract one from the length because of the trailing null
  for (PRUint32 i = 0; i < ArrayLength(eventArray) - 1; ++i) {
    sAtomEventTable->Put(eventArray[i].mAtom, eventArray[i]);
    sStringEventTable->Put(Substring(nsDependentAtomString(eventArray[i].mAtom), 2),
                           eventArray[i]);
  }

  return true;
}

void
nsContentUtils::InitializeTouchEventTable()
{
  static bool sEventTableInitialized = false;
  if (!sEventTableInitialized && sAtomEventTable && sStringEventTable) {
    sEventTableInitialized = true;
    static const EventNameMapping touchEventArray[] = {
#define EVENT(name_,  _id, _type, _struct)
#define TOUCH_EVENT(name_,  _id, _type, _struct)      \
      { nsGkAtoms::on##name_, _id, _type, _struct },
#include "nsEventNameList.h"
#undef TOUCH_EVENT
#undef EVENT
      { nsnull }
    };
    // Subtract one from the length because of the trailing null
    for (PRUint32 i = 0; i < ArrayLength(touchEventArray) - 1; ++i) {
      sAtomEventTable->Put(touchEventArray[i].mAtom, touchEventArray[i]);
      sStringEventTable->Put(Substring(nsDependentAtomString(touchEventArray[i].mAtom), 2),
                             touchEventArray[i]);
    }
  }
}

static bool
Is8bit(const nsAString& aString)
{
  static const PRUnichar EIGHT_BIT = PRUnichar(~0x00FF);

  nsAString::const_iterator done_reading;
  aString.EndReading(done_reading);

  // for each chunk of |aString|...
  PRUint32 fragmentLength = 0;
  nsAString::const_iterator iter;
  for (aString.BeginReading(iter); iter != done_reading;
       iter.advance(PRInt32(fragmentLength))) {
    fragmentLength = PRUint32(iter.size_forward());
    const PRUnichar* c = iter.get();
    const PRUnichar* fragmentEnd = c + fragmentLength;

    // for each character in this chunk...
    while (c < fragmentEnd) {
      if (*c++ & EIGHT_BIT) {
        return false;
      }
    }
  }

  return true;
}

nsresult
nsContentUtils::Btoa(const nsAString& aBinaryData,
                     nsAString& aAsciiBase64String)
{
  if (!Is8bit(aBinaryData)) {
    aAsciiBase64String.Truncate();
    return NS_ERROR_DOM_INVALID_CHARACTER_ERR;
  }

  return Base64Encode(aBinaryData, aAsciiBase64String);
}

nsresult
nsContentUtils::Atob(const nsAString& aAsciiBase64String,
                     nsAString& aBinaryData)
{
  if (!Is8bit(aAsciiBase64String)) {
    aBinaryData.Truncate();
    return NS_ERROR_DOM_INVALID_CHARACTER_ERR;
  }

  nsresult rv = Base64Decode(aAsciiBase64String, aBinaryData);
  if (NS_FAILED(rv) && rv == NS_ERROR_INVALID_ARG) {
    return NS_ERROR_DOM_INVALID_CHARACTER_ERR;
  }
  return rv;
}

bool
nsContentUtils::IsAutocompleteEnabled(nsIDOMHTMLInputElement* aInput)
{
  NS_PRECONDITION(aInput, "aInput should not be null!");

  nsAutoString autocomplete;
  aInput->GetAutocomplete(autocomplete);

  if (autocomplete.IsEmpty()) {
    nsCOMPtr<nsIDOMHTMLFormElement> form;
    aInput->GetForm(getter_AddRefs(form));
    if (!form) {
      return true;
    }

    form->GetAutocomplete(autocomplete);
  }

  return autocomplete.EqualsLiteral("on");
}

bool
nsContentUtils::URIIsChromeOrInPref(nsIURI *aURI, const char *aPref)
{
  if (!aURI) {
    return false;
  }

  nsCAutoString scheme;
  aURI->GetScheme(scheme);
  if (scheme.EqualsLiteral("chrome")) {
    return true;
  }

  nsCAutoString prePathUTF8;
  aURI->GetPrePath(prePathUTF8);
  NS_ConvertUTF8toUTF16 prePath(prePathUTF8);

  const nsAdoptingString& whitelist = Preferences::GetString(aPref);

  // This tokenizer also strips off whitespace around tokens, as desired.
  nsCharSeparatedTokenizer tokenizer(whitelist, ',',
    nsCharSeparatedTokenizerTemplate<>::SEPARATOR_OPTIONAL);

  while (tokenizer.hasMoreTokens()) {
    const nsSubstring& whitelistItem = tokenizer.nextToken();

    if (whitelistItem.Equals(prePath, nsCaseInsensitiveStringComparator())) {
      return true;
    }
  }

  return false;
}

#define SKIP_WHITESPACE(iter, end_iter, end_res)                 \
  while ((iter) != (end_iter) && nsCRT::IsAsciiSpace(*(iter))) { \
    ++(iter);                                                    \
  }                                                              \
  if ((iter) == (end_iter)) {                                    \
    return (end_res);                                            \
  }

#define SKIP_ATTR_NAME(iter, end_iter)                            \
  while ((iter) != (end_iter) && !nsCRT::IsAsciiSpace(*(iter)) && \
         *(iter) != '=') {                                        \
    ++(iter);                                                     \
  }

bool
nsContentUtils::GetPseudoAttributeValue(const nsString& aSource, nsIAtom *aName,
                                        nsAString& aValue)
{
  aValue.Truncate();

  const PRUnichar *start = aSource.get();
  const PRUnichar *end = start + aSource.Length();
  const PRUnichar *iter;

  while (start != end) {
    SKIP_WHITESPACE(start, end, false)
    iter = start;
    SKIP_ATTR_NAME(iter, end)

    if (start == iter) {
      return false;
    }

    // Remember the attr name.
    const nsDependentSubstring & attrName = Substring(start, iter);

    // Now check whether this is a valid name="value" pair.
    start = iter;
    SKIP_WHITESPACE(start, end, false)
    if (*start != '=') {
      // No '=', so this is not a name="value" pair.  We don't know
      // what it is, and we have no way to handle it.
      return false;
    }

    // Have to skip the value.
    ++start;
    SKIP_WHITESPACE(start, end, false)
    PRUnichar q = *start;
    if (q != kQuote && q != kApostrophe) {
      // Not a valid quoted value, so bail.
      return false;
    }

    ++start;  // Point to the first char of the value.
    iter = start;

    while (iter != end && *iter != q) {
      ++iter;
    }

    if (iter == end) {
      // Oops, unterminated quoted string.
      return false;
    }

    // At this point attrName holds the name of the "attribute" and
    // the value is between start and iter.

    if (aName->Equals(attrName)) {
      // We'll accumulate as many characters as possible (until we hit either
      // the end of the string or the beginning of an entity). Chunks will be
      // delimited by start and chunkEnd.
      const PRUnichar *chunkEnd = start;
      while (chunkEnd != iter) {
        if (*chunkEnd == kLessThan) {
          aValue.Truncate();

          return false;
        }

        if (*chunkEnd == kAmpersand) {
          aValue.Append(start, chunkEnd - start);

          // Point to first character after the ampersand.
          ++chunkEnd;

          const PRUnichar *afterEntity = nsnull;
          PRUnichar result[2];
          PRUint32 count =
            MOZ_XMLTranslateEntity(reinterpret_cast<const char*>(chunkEnd),
                                  reinterpret_cast<const char*>(iter),
                                  reinterpret_cast<const char**>(&afterEntity),
                                  result);
          if (count == 0) {
            aValue.Truncate();

            return false;
          }

          aValue.Append(result, count);

          // Advance to after the entity and begin a new chunk.
          start = chunkEnd = afterEntity;
        }
        else {
          ++chunkEnd;
        }
      }

      // Append remainder.
      aValue.Append(start, iter - start);

      return true;
    }

    // Resume scanning after the end of the attribute value (past the quote
    // char).
    start = iter + 1;
  }

  return false;
}

bool
nsContentUtils::IsJavaScriptLanguage(const nsString& aName, PRUint32 *aFlags)
{
  JSVersion version = JSVERSION_UNKNOWN;

  if (aName.LowerCaseEqualsLiteral("javascript") ||
      aName.LowerCaseEqualsLiteral("livescript") ||
      aName.LowerCaseEqualsLiteral("mocha")) {
    version = JSVERSION_DEFAULT;
  } else if (aName.LowerCaseEqualsLiteral("javascript1.0")) {
    version = JSVERSION_1_0;
  } else if (aName.LowerCaseEqualsLiteral("javascript1.1")) {
    version = JSVERSION_1_1;
  } else if (aName.LowerCaseEqualsLiteral("javascript1.2")) {
    version = JSVERSION_1_2;
  } else if (aName.LowerCaseEqualsLiteral("javascript1.3")) {
    version = JSVERSION_1_3;
  } else if (aName.LowerCaseEqualsLiteral("javascript1.4")) {
    version = JSVERSION_1_4;
  } else if (aName.LowerCaseEqualsLiteral("javascript1.5")) {
    version = JSVERSION_1_5;
  } else if (aName.LowerCaseEqualsLiteral("javascript1.6")) {
    version = JSVERSION_1_6;
  } else if (aName.LowerCaseEqualsLiteral("javascript1.7")) {
    version = JSVERSION_1_7;
  } else if (aName.LowerCaseEqualsLiteral("javascript1.8")) {
    version = JSVERSION_1_8;
  }

  if (version == JSVERSION_UNKNOWN) {
    return false;
  }
  *aFlags = version;
  return true;
}

void
nsContentUtils::SplitMimeType(const nsAString& aValue, nsString& aType,
                              nsString& aParams)
{
  aType.Truncate();
  aParams.Truncate();
  PRInt32 semiIndex = aValue.FindChar(PRUnichar(';'));
  if (-1 != semiIndex) {
    aType = Substring(aValue, 0, semiIndex);
    aParams = Substring(aValue, semiIndex + 1,
                       aValue.Length() - (semiIndex + 1));
    aParams.StripWhitespace();
  }
  else {
    aType = aValue;
  }
  aType.StripWhitespace();
}

nsresult 
nsContentUtils::IsUserIdle(PRUint32 aRequestedIdleTimeInMS, bool* aUserIsIdle)
{
  nsresult rv;
  nsCOMPtr<nsIIdleService> idleService = 
    do_GetService("@mozilla.org/widget/idleservice;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
    
  PRUint32 idleTimeInMS;
  rv = idleService->GetIdleTime(&idleTimeInMS);
  NS_ENSURE_SUCCESS(rv, rv);

  *aUserIsIdle = idleTimeInMS >= aRequestedIdleTimeInMS;
  return NS_OK;
}

/**
 * Access a cached parser service. Don't addref. We need only one
 * reference to it and this class has that one.
 */
/* static */
nsIParserService*
nsContentUtils::GetParserService()
{
  // XXX: This isn't accessed from several threads, is it?
  if (!sParserService) {
    // Lock, recheck sCachedParserService and aquire if this should be
    // safe for multiple threads.
    nsresult rv = CallGetService(kParserServiceCID, &sParserService);
    if (NS_FAILED(rv)) {
      sParserService = nsnull;
    }
  }

  return sParserService;
}

#ifdef MOZ_XTF
nsIXTFService*
nsContentUtils::GetXTFService()
{
  if (!sXTFService) {
    nsresult rv = CallGetService(kXTFServiceCID, &sXTFService);
    if (NS_FAILED(rv)) {
      sXTFService = nsnull;
    }
  }

  return sXTFService;
}
#endif

#ifdef IBMBIDI
nsIBidiKeyboard*
nsContentUtils::GetBidiKeyboard()
{
  if (!sBidiKeyboard) {
    nsresult rv = CallGetService("@mozilla.org/widget/bidikeyboard;1", &sBidiKeyboard);
    if (NS_FAILED(rv)) {
      sBidiKeyboard = nsnull;
    }
  }
  return sBidiKeyboard;
}
#endif

template <class OutputIterator>
struct NormalizeNewlinesCharTraits {
  public:
    typedef typename OutputIterator::value_type value_type;

  public:
    NormalizeNewlinesCharTraits(OutputIterator& aIterator) : mIterator(aIterator) { }
    void writechar(typename OutputIterator::value_type aChar) {
      *mIterator++ = aChar;
    }

  private:
    OutputIterator mIterator;
};

#ifdef HAVE_CPP_PARTIAL_SPECIALIZATION

template <class CharT>
struct NormalizeNewlinesCharTraits<CharT*> {
  public:
    typedef CharT value_type;

  public:
    NormalizeNewlinesCharTraits(CharT* aCharPtr) : mCharPtr(aCharPtr) { }
    void writechar(CharT aChar) {
      *mCharPtr++ = aChar;
    }

  private:
    CharT* mCharPtr;
};

#else

template <>
struct NormalizeNewlinesCharTraits<char*> {
  public:
    typedef char value_type;

  public:
    NormalizeNewlinesCharTraits(char* aCharPtr) : mCharPtr(aCharPtr) { }
    void writechar(char aChar) {
      *mCharPtr++ = aChar;
    }

  private:
    char* mCharPtr;
};

template <>
struct NormalizeNewlinesCharTraits<PRUnichar*> {
  public:
    typedef PRUnichar value_type;

  public:
    NormalizeNewlinesCharTraits(PRUnichar* aCharPtr) : mCharPtr(aCharPtr) { }
    void writechar(PRUnichar aChar) {
      *mCharPtr++ = aChar;
    }

  private:
    PRUnichar* mCharPtr;
};

#endif

template <class OutputIterator>
class CopyNormalizeNewlines
{
  public:
    typedef typename OutputIterator::value_type value_type;

  public:
    CopyNormalizeNewlines(OutputIterator* aDestination,
                          bool aLastCharCR=false) :
      mLastCharCR(aLastCharCR),
      mDestination(aDestination),
      mWritten(0)
    { }

    PRUint32 GetCharsWritten() {
      return mWritten;
    }

    bool IsLastCharCR() {
      return mLastCharCR;
    }

    void write(const typename OutputIterator::value_type* aSource, PRUint32 aSourceLength) {

      const typename OutputIterator::value_type* done_writing = aSource + aSourceLength;

      // If the last source buffer ended with a CR...
      if (mLastCharCR) {
        // ..and if the next one is a LF, then skip it since
        // we've already written out a newline
        if (aSourceLength && (*aSource == value_type('\n'))) {
          ++aSource;
        }
        mLastCharCR = false;
      }

      PRUint32 num_written = 0;
      while ( aSource < done_writing ) {
        if (*aSource == value_type('\r')) {
          mDestination->writechar('\n');
          ++aSource;
          // If we've reached the end of the buffer, record
          // that we wrote out a CR
          if (aSource == done_writing) {
            mLastCharCR = true;
          }
          // If the next character is a LF, skip it
          else if (*aSource == value_type('\n')) {
            ++aSource;
          }
        }
        else {
          mDestination->writechar(*aSource++);
        }
        ++num_written;
      }

      mWritten += num_written;
    }

  private:
    bool mLastCharCR;
    OutputIterator* mDestination;
    PRUint32 mWritten;
};

// static
PRUint32
nsContentUtils::CopyNewlineNormalizedUnicodeTo(const nsAString& aSource,
                                               PRUint32 aSrcOffset,
                                               PRUnichar* aDest,
                                               PRUint32 aLength,
                                               bool& aLastCharCR)
{
  typedef NormalizeNewlinesCharTraits<PRUnichar*> sink_traits;

  sink_traits dest_traits(aDest);
  CopyNormalizeNewlines<sink_traits> normalizer(&dest_traits,aLastCharCR);
  nsReadingIterator<PRUnichar> fromBegin, fromEnd;
  copy_string(aSource.BeginReading(fromBegin).advance( PRInt32(aSrcOffset) ),
              aSource.BeginReading(fromEnd).advance( PRInt32(aSrcOffset+aLength) ),
              normalizer);
  aLastCharCR = normalizer.IsLastCharCR();
  return normalizer.GetCharsWritten();
}

// static
PRUint32
nsContentUtils::CopyNewlineNormalizedUnicodeTo(nsReadingIterator<PRUnichar>& aSrcStart, const nsReadingIterator<PRUnichar>& aSrcEnd, nsAString& aDest)
{
  typedef nsWritingIterator<PRUnichar> WritingIterator;
  typedef NormalizeNewlinesCharTraits<WritingIterator> sink_traits;

  WritingIterator iter;
  aDest.BeginWriting(iter);
  sink_traits dest_traits(iter);
  CopyNormalizeNewlines<sink_traits> normalizer(&dest_traits);
  copy_string(aSrcStart, aSrcEnd, normalizer);
  return normalizer.GetCharsWritten();
}

/**
 * This is used to determine whether a character is in one of the punctuation
 * mark classes which CSS says should be part of the first-letter.
 * See http://www.w3.org/TR/CSS2/selector.html#first-letter and
 *     http://www.w3.org/TR/selectors/#first-letter
 */

// static
bool
nsContentUtils::IsFirstLetterPunctuation(PRUint32 aChar)
{
  PRUint8 cat = mozilla::unicode::GetGeneralCategory(aChar);

  return (cat == HB_UNICODE_GENERAL_CATEGORY_OPEN_PUNCTUATION ||     // Ps
          cat == HB_UNICODE_GENERAL_CATEGORY_CLOSE_PUNCTUATION ||    // Pe
          cat == HB_UNICODE_GENERAL_CATEGORY_INITIAL_PUNCTUATION ||  // Pi
          cat == HB_UNICODE_GENERAL_CATEGORY_FINAL_PUNCTUATION ||    // Pf
          cat == HB_UNICODE_GENERAL_CATEGORY_OTHER_PUNCTUATION);     // Po
}

// static
bool
nsContentUtils::IsFirstLetterPunctuationAt(const nsTextFragment* aFrag, PRUint32 aOffset)
{
  PRUnichar h = aFrag->CharAt(aOffset);
  if (!IS_SURROGATE(h)) {
    return IsFirstLetterPunctuation(h);
  }
  if (NS_IS_HIGH_SURROGATE(h) && aOffset + 1 < aFrag->GetLength()) {
    PRUnichar l = aFrag->CharAt(aOffset + 1);
    if (NS_IS_LOW_SURROGATE(l)) {
      return IsFirstLetterPunctuation(SURROGATE_TO_UCS4(h, l));
    }
  }
  return false;
}

// static
bool nsContentUtils::IsAlphanumeric(PRUint32 aChar)
{
  nsIUGenCategory::nsUGenCategory cat = mozilla::unicode::GetGenCategory(aChar);

  return (cat == nsIUGenCategory::kLetter || cat == nsIUGenCategory::kNumber);
}
 
// static
bool nsContentUtils::IsAlphanumericAt(const nsTextFragment* aFrag, PRUint32 aOffset)
{
  PRUnichar h = aFrag->CharAt(aOffset);
  if (!IS_SURROGATE(h)) {
    return IsAlphanumeric(h);
  }
  if (NS_IS_HIGH_SURROGATE(h) && aOffset + 1 < aFrag->GetLength()) {
    PRUnichar l = aFrag->CharAt(aOffset + 1);
    if (NS_IS_LOW_SURROGATE(l)) {
      return IsAlphanumeric(SURROGATE_TO_UCS4(h, l));
    }
  }
  return false;
}

/* static */
bool
nsContentUtils::IsHTMLWhitespace(PRUnichar aChar)
{
  return aChar == PRUnichar(0x0009) ||
         aChar == PRUnichar(0x000A) ||
         aChar == PRUnichar(0x000C) ||
         aChar == PRUnichar(0x000D) ||
         aChar == PRUnichar(0x0020);
}

/* static */
bool
nsContentUtils::IsHTMLBlock(nsIAtom* aLocalName)
{
  return
    (aLocalName == nsGkAtoms::address) ||
    (aLocalName == nsGkAtoms::article) ||
    (aLocalName == nsGkAtoms::aside) ||
    (aLocalName == nsGkAtoms::blockquote) ||
    (aLocalName == nsGkAtoms::center) ||
    (aLocalName == nsGkAtoms::dir) ||
    (aLocalName == nsGkAtoms::div) ||
    (aLocalName == nsGkAtoms::dl) || // XXX why not dt and dd?
    (aLocalName == nsGkAtoms::fieldset) ||
    (aLocalName == nsGkAtoms::figure) || // XXX shouldn't figcaption be on this list
    (aLocalName == nsGkAtoms::footer) ||
    (aLocalName == nsGkAtoms::form) ||
    (aLocalName == nsGkAtoms::h1) ||
    (aLocalName == nsGkAtoms::h2) ||
    (aLocalName == nsGkAtoms::h3) ||
    (aLocalName == nsGkAtoms::h4) ||
    (aLocalName == nsGkAtoms::h5) ||
    (aLocalName == nsGkAtoms::h6) ||
    (aLocalName == nsGkAtoms::header) ||
    (aLocalName == nsGkAtoms::hgroup) ||
    (aLocalName == nsGkAtoms::hr) ||
    (aLocalName == nsGkAtoms::li) ||
    (aLocalName == nsGkAtoms::listing) ||
    (aLocalName == nsGkAtoms::menu) ||
    (aLocalName == nsGkAtoms::multicol) || // XXX get rid of this one?
    (aLocalName == nsGkAtoms::nav) ||
    (aLocalName == nsGkAtoms::ol) ||
    (aLocalName == nsGkAtoms::p) ||
    (aLocalName == nsGkAtoms::pre) ||
    (aLocalName == nsGkAtoms::section) ||
    (aLocalName == nsGkAtoms::table) ||
    (aLocalName == nsGkAtoms::ul) ||
    (aLocalName == nsGkAtoms::xmp);
}

/* static */
bool
nsContentUtils::IsHTMLVoid(nsIAtom* aLocalName)
{
  return
    (aLocalName == nsGkAtoms::area) ||
    (aLocalName == nsGkAtoms::base) ||
    (aLocalName == nsGkAtoms::basefont) ||
    (aLocalName == nsGkAtoms::bgsound) ||
    (aLocalName == nsGkAtoms::br) ||
    (aLocalName == nsGkAtoms::col) ||
    (aLocalName == nsGkAtoms::command) ||
    (aLocalName == nsGkAtoms::embed) ||
    (aLocalName == nsGkAtoms::frame) ||
    (aLocalName == nsGkAtoms::hr) ||
    (aLocalName == nsGkAtoms::img) ||
    (aLocalName == nsGkAtoms::input) ||
    (aLocalName == nsGkAtoms::keygen) ||
    (aLocalName == nsGkAtoms::link) ||
    (aLocalName == nsGkAtoms::meta) ||
    (aLocalName == nsGkAtoms::param) ||
#ifdef MOZ_MEDIA
    (aLocalName == nsGkAtoms::source) ||
#endif
    (aLocalName == nsGkAtoms::track) ||
    (aLocalName == nsGkAtoms::wbr);
}

/* static */
bool
nsContentUtils::ParseIntMarginValue(const nsAString& aString, nsIntMargin& result)
{
  nsAutoString marginStr(aString);
  marginStr.CompressWhitespace(true, true);
  if (marginStr.IsEmpty()) {
    return false;
  }

  PRInt32 start = 0, end = 0;
  for (int count = 0; count < 4; count++) {
    if ((PRUint32)end >= marginStr.Length())
      return false;

    // top, right, bottom, left
    if (count < 3)
      end = Substring(marginStr, start).FindChar(',');
    else
      end = Substring(marginStr, start).Length();

    if (end <= 0)
      return false;

    PRInt32 ec, val = 
      nsString(Substring(marginStr, start, end)).ToInteger(&ec);
    if (NS_FAILED(ec))
      return false;

    switch(count) {
      case 0:
        result.top = val;
      break;
      case 1:
        result.right = val;
      break;
      case 2:
        result.bottom = val;
      break;
      case 3:
        result.left = val;
      break;
    }
    start += end + 1;
  }
  return true;
}

// static
PRInt32
nsContentUtils::ParseLegacyFontSize(const nsAString& aValue)
{
  nsAString::const_iterator iter, end;
  aValue.BeginReading(iter);
  aValue.EndReading(end);

  while (iter != end && nsContentUtils::IsHTMLWhitespace(*iter)) {
    ++iter;
  }

  if (iter == end) {
    return 0;
  }

  bool relative = false;
  bool negate = false;
  if (*iter == PRUnichar('-')) {
    relative = true;
    negate = true;
    ++iter;
  } else if (*iter == PRUnichar('+')) {
    relative = true;
    ++iter;
  }

  if (*iter < PRUnichar('0') || *iter > PRUnichar('9')) {
    return 0;
  }

  // We don't have to worry about overflow, since we can bail out as soon as
  // we're bigger than 7.
  PRInt32 value = 0;
  while (iter != end && *iter >= PRUnichar('0') && *iter <= PRUnichar('9')) {
    value = 10*value + (*iter - PRUnichar('0'));
    if (value >= 7) {
      break;
    }
    ++iter;
  }

  if (relative) {
    if (negate) {
      value = 3 - value;
    } else {
      value = 3 + value;
    }
  }

  return clamped(value, 1, 7);
}

/* static */
void
nsContentUtils::GetOfflineAppManifest(nsIDocument *aDocument, nsIURI **aURI)
{
  Element* docElement = aDocument->GetRootElement();
  if (!docElement) {
    return;
  }

  nsAutoString manifestSpec;
  docElement->GetAttr(kNameSpaceID_None, nsGkAtoms::manifest, manifestSpec);

  // Manifest URIs can't have fragment identifiers.
  if (manifestSpec.IsEmpty() ||
      manifestSpec.FindChar('#') != kNotFound) {
    return;
  }

  nsContentUtils::NewURIWithDocumentCharset(aURI, manifestSpec,
                                            aDocument,
                                            aDocument->GetDocBaseURI());
}

/* static */
bool
nsContentUtils::OfflineAppAllowed(nsIURI *aURI)
{
  nsCOMPtr<nsIOfflineCacheUpdateService> updateService =
    do_GetService(NS_OFFLINECACHEUPDATESERVICE_CONTRACTID);
  if (!updateService) {
    return false;
  }

  bool allowed;
  nsresult rv =
    updateService->OfflineAppAllowedForURI(aURI,
                                           Preferences::GetRootBranch(),
                                           &allowed);
  return NS_SUCCEEDED(rv) && allowed;
}

/* static */
bool
nsContentUtils::OfflineAppAllowed(nsIPrincipal *aPrincipal)
{
  nsCOMPtr<nsIOfflineCacheUpdateService> updateService =
    do_GetService(NS_OFFLINECACHEUPDATESERVICE_CONTRACTID);
  if (!updateService) {
    return false;
  }

  bool allowed;
  nsresult rv = updateService->OfflineAppAllowed(aPrincipal,
                                                 Preferences::GetRootBranch(),
                                                 &allowed);
  return NS_SUCCEEDED(rv) && allowed;
}

// static
void
nsContentUtils::Shutdown()
{
  sInitialized = false;

  NS_IF_RELEASE(sContentPolicyService);
  sTriedToGetContentPolicy = false;
  PRUint32 i;
  for (i = 0; i < PropertiesFile_COUNT; ++i)
    NS_IF_RELEASE(sStringBundles[i]);

  NS_IF_RELEASE(sStringBundleService);
  NS_IF_RELEASE(sConsoleService);
  NS_IF_RELEASE(sDOMScriptObjectFactory);
  sXPConnect = nsnull;
  sThreadJSContextStack = nsnull;
  NS_IF_RELEASE(sSecurityManager);
  NS_IF_RELEASE(sNameSpaceManager);
  NS_IF_RELEASE(sParserService);
  NS_IF_RELEASE(sIOService);
  NS_IF_RELEASE(sLineBreaker);
  NS_IF_RELEASE(sWordBreaker);
#ifdef MOZ_XTF
  NS_IF_RELEASE(sXTFService);
#endif
  NS_IF_RELEASE(sImgLoader);
  NS_IF_RELEASE(sImgCache);
#ifdef IBMBIDI
  NS_IF_RELEASE(sBidiKeyboard);
#endif

  delete sAtomEventTable;
  sAtomEventTable = nsnull;
  delete sStringEventTable;
  sStringEventTable = nsnull;
  delete sUserDefinedEvents;
  sUserDefinedEvents = nsnull;

  if (sEventListenerManagersHash.ops) {
    NS_ASSERTION(sEventListenerManagersHash.entryCount == 0,
                 "Event listener manager hash not empty at shutdown!");

    // See comment above.

    // However, we have to handle this table differently.  If it still
    // has entries, we want to leak it too, so that we can keep it alive
    // in case any elements are destroyed.  Because if they are, we need
    // their event listener managers to be destroyed too, or otherwise
    // it could leave dangling references in DOMClassInfo's preserved
    // wrapper table.

    if (sEventListenerManagersHash.entryCount == 0) {
      PL_DHashTableFinish(&sEventListenerManagersHash);
      sEventListenerManagersHash.ops = nsnull;
    }
  }

  NS_ASSERTION(!sBlockedScriptRunners ||
               sBlockedScriptRunners->Length() == 0,
               "How'd this happen?");
  delete sBlockedScriptRunners;
  sBlockedScriptRunners = nsnull;

  delete sShiftText;
  sShiftText = nsnull;
  delete sControlText;  
  sControlText = nsnull;
  delete sMetaText;  
  sMetaText = nsnull;
  delete sAltText;  
  sAltText = nsnull;
  delete sModifierSeparator;
  sModifierSeparator = nsnull;

  NS_IF_RELEASE(sSameOriginChecker);
  
  nsTextEditorState::ShutDown();
}

// static
bool
nsContentUtils::CallerHasUniversalXPConnect()
{
  bool hasCap;
  if (NS_FAILED(sSecurityManager->IsCapabilityEnabled("UniversalXPConnect",
                                                      &hasCap)))
    return false;
  return hasCap;
}

/**
 * Checks whether two nodes come from the same origin. aTrustedNode is
 * considered 'safe' in that a user can operate on it and that it isn't
 * a js-object that implements nsIDOMNode.
 * Never call this function with the first node provided by script, it
 * must always be known to be a 'real' node!
 */
// static
nsresult
nsContentUtils::CheckSameOrigin(nsINode *aTrustedNode,
                                nsIDOMNode *aUnTrustedNode)
{
  MOZ_ASSERT(aTrustedNode);

  // Make sure it's a real node.
  nsCOMPtr<nsINode> unTrustedNode = do_QueryInterface(aUnTrustedNode);
  NS_ENSURE_TRUE(unTrustedNode, NS_ERROR_UNEXPECTED);
  return CheckSameOrigin(aTrustedNode, unTrustedNode);
}

nsresult
nsContentUtils::CheckSameOrigin(nsINode* aTrustedNode,
                                nsINode* unTrustedNode)
{
  MOZ_ASSERT(aTrustedNode);
  MOZ_ASSERT(unTrustedNode);

  bool isSystem = false;
  nsresult rv = sSecurityManager->SubjectPrincipalIsSystem(&isSystem);
  NS_ENSURE_SUCCESS(rv, rv);

  if (isSystem) {
    // we're running as system, grant access to the node.

    return NS_OK;
  }

  /*
   * Get hold of each node's principal
   */

  nsIPrincipal* trustedPrincipal = aTrustedNode->NodePrincipal();
  nsIPrincipal* unTrustedPrincipal = unTrustedNode->NodePrincipal();

  if (trustedPrincipal == unTrustedPrincipal) {
    return NS_OK;
  }

  bool equal;
  // XXXbz should we actually have a Subsumes() check here instead?  Or perhaps
  // a separate method for that, with callers using one or the other?
  if (NS_FAILED(trustedPrincipal->Equals(unTrustedPrincipal, &equal)) ||
      !equal) {
    return NS_ERROR_DOM_PROP_ACCESS_DENIED;
  }

  return NS_OK;
}

// static
bool
nsContentUtils::CanCallerAccess(nsIPrincipal* aSubjectPrincipal,
                                nsIPrincipal* aPrincipal)
{
  bool subsumes;
  nsresult rv = aSubjectPrincipal->Subsumes(aPrincipal, &subsumes);
  NS_ENSURE_SUCCESS(rv, false);

  if (subsumes) {
    return true;
  }

  // The subject doesn't subsume aPrincipal. Allow access only if the subject
  // has UniversalXPConnect.
  return CallerHasUniversalXPConnect();
}

// static
bool
nsContentUtils::CanCallerAccess(nsIDOMNode *aNode)
{
  // XXXbz why not check the IsCapabilityEnabled thing up front, and not bother
  // with the system principal games?  But really, there should be a simpler
  // API here, dammit.
  nsCOMPtr<nsIPrincipal> subjectPrincipal;
  nsresult rv = sSecurityManager->GetSubjectPrincipal(getter_AddRefs(subjectPrincipal));
  NS_ENSURE_SUCCESS(rv, false);

  if (!subjectPrincipal) {
    // we're running as system, grant access to the node.

    return true;
  }

  nsCOMPtr<nsINode> node = do_QueryInterface(aNode);
  NS_ENSURE_TRUE(node, false);

  return CanCallerAccess(subjectPrincipal, node->NodePrincipal());
}

// static
bool
nsContentUtils::CanCallerAccess(nsPIDOMWindow* aWindow)
{
  // XXXbz why not check the IsCapabilityEnabled thing up front, and not bother
  // with the system principal games?  But really, there should be a simpler
  // API here, dammit.
  nsCOMPtr<nsIPrincipal> subjectPrincipal;
  nsresult rv = sSecurityManager->GetSubjectPrincipal(getter_AddRefs(subjectPrincipal));
  NS_ENSURE_SUCCESS(rv, false);

  if (!subjectPrincipal) {
    // we're running as system, grant access to the node.

    return true;
  }

  nsCOMPtr<nsIScriptObjectPrincipal> scriptObject =
    do_QueryInterface(aWindow->IsOuterWindow() ?
                      aWindow->GetCurrentInnerWindow() : aWindow);
  NS_ENSURE_TRUE(scriptObject, false);

  return CanCallerAccess(subjectPrincipal, scriptObject->GetPrincipal());
}

//static
bool
nsContentUtils::InProlog(nsINode *aNode)
{
  NS_PRECONDITION(aNode, "missing node to nsContentUtils::InProlog");

  nsINode* parent = aNode->GetNodeParent();
  if (!parent || !parent->IsNodeOfType(nsINode::eDOCUMENT)) {
    return false;
  }

  nsIDocument* doc = static_cast<nsIDocument*>(parent);
  nsIContent* root = doc->GetRootElement();

  return !root || doc->IndexOf(aNode) < doc->IndexOf(root);
}

JSContext *
nsContentUtils::GetContextFromDocument(nsIDocument *aDocument)
{
  nsIScriptGlobalObject *sgo = aDocument->GetScopeObject();
  if (!sgo) {
    // No script global, no context.
    return nsnull;
  }

  nsIScriptContext *scx = sgo->GetContext();
  if (!scx) {
    // No context left in the scope...
    return nsnull;
  }

  return scx->GetNativeContext();
}

//static
void
nsContentUtils::TraceSafeJSContext(JSTracer* aTrc)
{
  if (!sThreadJSContextStack) {
    return;
  }
  JSContext* cx = sThreadJSContextStack->GetSafeJSContext();
  if (!cx) {
    return;
  }
  if (JSObject* global = JS_GetGlobalObject(cx)) {
    JS_CALL_OBJECT_TRACER(aTrc, global, "safe context");
  }
}

nsresult
nsContentUtils::ReparentContentWrappersInScope(JSContext *cx,
                                               nsIScriptGlobalObject *aOldScope,
                                               nsIScriptGlobalObject *aNewScope)
{
  JSObject *oldScopeObj = aOldScope->GetGlobalJSObject();
  JSObject *newScopeObj = aNewScope->GetGlobalJSObject();

  if (!newScopeObj || !oldScopeObj) {
    // We can't really do anything without the JSObjects.

    return NS_ERROR_NOT_AVAILABLE;
  }

  return sXPConnect->MoveWrappers(cx, oldScopeObj, newScopeObj);
}

nsPIDOMWindow *
nsContentUtils::GetWindowFromCaller()
{
  JSContext *cx = nsnull;
  sThreadJSContextStack->Peek(&cx);

  if (cx) {
    nsCOMPtr<nsPIDOMWindow> win =
      do_QueryInterface(nsJSUtils::GetDynamicScriptGlobal(cx));
    return win;
  }

  return nsnull;
}

nsIDOMDocument *
nsContentUtils::GetDocumentFromCaller()
{
  JSContext *cx = nsnull;
  JSObject *obj = nsnull;
  sXPConnect->GetCaller(&cx, &obj);
  NS_ASSERTION(cx && obj, "Caller ensures something is running");

  JSAutoEnterCompartment ac;
  if (!ac.enter(cx, obj)) {
    return nsnull;
  }

  nsCOMPtr<nsPIDOMWindow> win =
    do_QueryInterface(nsJSUtils::GetStaticScriptGlobal(cx, obj));
  if (!win) {
    return nsnull;
  }

  return win->GetExtantDocument();
}

nsIDOMDocument *
nsContentUtils::GetDocumentFromContext()
{
  JSContext *cx = nsnull;
  sThreadJSContextStack->Peek(&cx);

  if (cx) {
    nsIScriptGlobalObject *sgo = nsJSUtils::GetDynamicScriptGlobal(cx);

    if (sgo) {
      nsCOMPtr<nsPIDOMWindow> pwin = do_QueryInterface(sgo);
      if (pwin) {
        return pwin->GetExtantDocument();
      }
    }
  }

  return nsnull;
}

bool
nsContentUtils::IsCallerChrome()
{
  bool is_caller_chrome = false;
  nsresult rv = sSecurityManager->SubjectPrincipalIsSystem(&is_caller_chrome);
  if (NS_FAILED(rv)) {
    return false;
  }

  return is_caller_chrome;
}

bool
nsContentUtils::IsCallerTrustedForRead()
{
  return CallerHasUniversalXPConnect();
}

bool
nsContentUtils::IsCallerTrustedForWrite()
{
  return CallerHasUniversalXPConnect();
}

bool
nsContentUtils::IsImageSrcSetDisabled()
{
  return Preferences::GetBool("dom.disable_image_src_set") &&
         !IsCallerChrome();
}

// static
nsINode*
nsContentUtils::GetCrossDocParentNode(nsINode* aChild)
{
  NS_PRECONDITION(aChild, "The child is null!");

  nsINode* parent = aChild->GetNodeParent();
  if (parent || !aChild->IsNodeOfType(nsINode::eDOCUMENT))
    return parent;

  nsIDocument* doc = static_cast<nsIDocument*>(aChild);
  nsIDocument* parentDoc = doc->GetParentDocument();
  return parentDoc ? parentDoc->FindContentForSubDocument(doc) : nsnull;
}

// static
bool
nsContentUtils::ContentIsDescendantOf(const nsINode* aPossibleDescendant,
                                      const nsINode* aPossibleAncestor)
{
  NS_PRECONDITION(aPossibleDescendant, "The possible descendant is null!");
  NS_PRECONDITION(aPossibleAncestor, "The possible ancestor is null!");

  do {
    if (aPossibleDescendant == aPossibleAncestor)
      return true;
    aPossibleDescendant = aPossibleDescendant->GetNodeParent();
  } while (aPossibleDescendant);

  return false;
}

// static
bool
nsContentUtils::ContentIsCrossDocDescendantOf(nsINode* aPossibleDescendant,
                                              nsINode* aPossibleAncestor)
{
  NS_PRECONDITION(aPossibleDescendant, "The possible descendant is null!");
  NS_PRECONDITION(aPossibleAncestor, "The possible ancestor is null!");

  do {
    if (aPossibleDescendant == aPossibleAncestor)
      return true;
    aPossibleDescendant = GetCrossDocParentNode(aPossibleDescendant);
  } while (aPossibleDescendant);

  return false;
}


// static
nsresult
nsContentUtils::GetAncestors(nsINode* aNode,
                             nsTArray<nsINode*>& aArray)
{
  while (aNode) {
    aArray.AppendElement(aNode);
    aNode = aNode->GetNodeParent();
  }
  return NS_OK;
}

// static
nsresult
nsContentUtils::GetAncestorsAndOffsets(nsIDOMNode* aNode,
                                       PRInt32 aOffset,
                                       nsTArray<nsIContent*>* aAncestorNodes,
                                       nsTArray<PRInt32>* aAncestorOffsets)
{
  NS_ENSURE_ARG_POINTER(aNode);

  nsCOMPtr<nsIContent> content(do_QueryInterface(aNode));

  if (!content) {
    return NS_ERROR_FAILURE;
  }

  if (!aAncestorNodes->IsEmpty()) {
    NS_WARNING("aAncestorNodes is not empty");
    aAncestorNodes->Clear();
  }

  if (!aAncestorOffsets->IsEmpty()) {
    NS_WARNING("aAncestorOffsets is not empty");
    aAncestorOffsets->Clear();
  }

  // insert the node itself
  aAncestorNodes->AppendElement(content.get());
  aAncestorOffsets->AppendElement(aOffset);

  // insert all the ancestors
  nsIContent* child = content;
  nsIContent* parent = child->GetParent();
  while (parent) {
    aAncestorNodes->AppendElement(parent);
    aAncestorOffsets->AppendElement(parent->IndexOf(child));
    child = parent;
    parent = parent->GetParent();
  }

  return NS_OK;
}

// static
nsresult
nsContentUtils::GetCommonAncestor(nsIDOMNode *aNode,
                                  nsIDOMNode *aOther,
                                  nsIDOMNode** aCommonAncestor)
{
  *aCommonAncestor = nsnull;

  nsCOMPtr<nsINode> node1 = do_QueryInterface(aNode);
  nsCOMPtr<nsINode> node2 = do_QueryInterface(aOther);

  NS_ENSURE_TRUE(node1 && node2, NS_ERROR_UNEXPECTED);

  nsINode* common = GetCommonAncestor(node1, node2);
  NS_ENSURE_TRUE(common, NS_ERROR_NOT_AVAILABLE);

  return CallQueryInterface(common, aCommonAncestor);
}

// static
nsINode*
nsContentUtils::GetCommonAncestor(nsINode* aNode1,
                                  nsINode* aNode2)
{
  if (aNode1 == aNode2) {
    return aNode1;
  }

  // Build the chain of parents
  nsAutoTArray<nsINode*, 30> parents1, parents2;
  do {
    parents1.AppendElement(aNode1);
    aNode1 = aNode1->GetNodeParent();
  } while (aNode1);
  do {
    parents2.AppendElement(aNode2);
    aNode2 = aNode2->GetNodeParent();
  } while (aNode2);

  // Find where the parent chain differs
  PRUint32 pos1 = parents1.Length();
  PRUint32 pos2 = parents2.Length();
  nsINode* parent = nsnull;
  PRUint32 len;
  for (len = NS_MIN(pos1, pos2); len > 0; --len) {
    nsINode* child1 = parents1.ElementAt(--pos1);
    nsINode* child2 = parents2.ElementAt(--pos2);
    if (child1 != child2) {
      break;
    }
    parent = child1;
  }

  return parent;
}

/* static */
PRInt32
nsContentUtils::ComparePoints(nsINode* aParent1, PRInt32 aOffset1,
                              nsINode* aParent2, PRInt32 aOffset2,
                              bool* aDisconnected)
{
  if (aParent1 == aParent2) {
    return aOffset1 < aOffset2 ? -1 :
           aOffset1 > aOffset2 ? 1 :
           0;
  }

  nsAutoTArray<nsINode*, 32> parents1, parents2;
  nsINode* node1 = aParent1;
  nsINode* node2 = aParent2;
  do {
    parents1.AppendElement(node1);
    node1 = node1->GetNodeParent();
  } while (node1);
  do {
    parents2.AppendElement(node2);
    node2 = node2->GetNodeParent();
  } while (node2);

  PRUint32 pos1 = parents1.Length() - 1;
  PRUint32 pos2 = parents2.Length() - 1;
  
  bool disconnected = parents1.ElementAt(pos1) != parents2.ElementAt(pos2);
  if (aDisconnected) {
    *aDisconnected = disconnected;
  }
  if (disconnected) {
    NS_ASSERTION(aDisconnected, "unexpected disconnected nodes");
    return 1;
  }

  // Find where the parent chains differ
  nsINode* parent = parents1.ElementAt(pos1);
  PRUint32 len;
  for (len = NS_MIN(pos1, pos2); len > 0; --len) {
    nsINode* child1 = parents1.ElementAt(--pos1);
    nsINode* child2 = parents2.ElementAt(--pos2);
    if (child1 != child2) {
      return parent->IndexOf(child1) < parent->IndexOf(child2) ? -1 : 1;
    }
    parent = child1;
  }

  
  // The parent chains never differed, so one of the nodes is an ancestor of
  // the other

  NS_ASSERTION(!pos1 || !pos2,
               "should have run out of parent chain for one of the nodes");

  if (!pos1) {
    nsINode* child2 = parents2.ElementAt(--pos2);
    return aOffset1 <= parent->IndexOf(child2) ? -1 : 1;
  }

  nsINode* child1 = parents1.ElementAt(--pos1);
  return parent->IndexOf(child1) < aOffset2 ? -1 : 1;
}

/* static */
PRInt32
nsContentUtils::ComparePoints(nsIDOMNode* aParent1, PRInt32 aOffset1,
                              nsIDOMNode* aParent2, PRInt32 aOffset2,
                              bool* aDisconnected)
{
  nsCOMPtr<nsINode> parent1 = do_QueryInterface(aParent1);
  nsCOMPtr<nsINode> parent2 = do_QueryInterface(aParent2);
  NS_ENSURE_TRUE(parent1 && parent2, -1);
  return ComparePoints(parent1, aOffset1, parent2, aOffset2);
}

inline bool
IsCharInSet(const char* aSet,
            const PRUnichar aChar)
{
  PRUnichar ch;
  while ((ch = *aSet)) {
    if (aChar == PRUnichar(ch)) {
      return true;
    }
    ++aSet;
  }
  return false;
}

/**
 * This method strips leading/trailing chars, in given set, from string.
 */

// static
const nsDependentSubstring
nsContentUtils::TrimCharsInSet(const char* aSet,
                               const nsAString& aValue)
{
  nsAString::const_iterator valueCurrent, valueEnd;

  aValue.BeginReading(valueCurrent);
  aValue.EndReading(valueEnd);

  // Skip characters in the beginning
  while (valueCurrent != valueEnd) {
    if (!IsCharInSet(aSet, *valueCurrent)) {
      break;
    }
    ++valueCurrent;
  }

  if (valueCurrent != valueEnd) {
    for (;;) {
      --valueEnd;
      if (!IsCharInSet(aSet, *valueEnd)) {
        break;
      }
    }
    ++valueEnd; // Step beyond the last character we want in the value.
  }

  // valueEnd should point to the char after the last to copy
  return Substring(valueCurrent, valueEnd);
}

/**
 * This method strips leading and trailing whitespace from a string.
 */

// static
template<bool IsWhitespace(PRUnichar)>
const nsDependentSubstring
nsContentUtils::TrimWhitespace(const nsAString& aStr, bool aTrimTrailing)
{
  nsAString::const_iterator start, end;

  aStr.BeginReading(start);
  aStr.EndReading(end);

  // Skip whitespace characters in the beginning
  while (start != end && IsWhitespace(*start)) {
    ++start;
  }

  if (aTrimTrailing) {
    // Skip whitespace characters in the end.
    while (end != start) {
      --end;

      if (!IsWhitespace(*end)) {
        // Step back to the last non-whitespace character.
        ++end;

        break;
      }
    }
  }

  // Return a substring for the string w/o leading and/or trailing
  // whitespace

  return Substring(start, end);
}

// Declaring the templates we are going to use avoid linking issues without
// inlining the method. Considering there is not so much spaces checking
// methods we can consider this to be better than inlining.
template
const nsDependentSubstring
nsContentUtils::TrimWhitespace<nsCRT::IsAsciiSpace>(const nsAString&, bool);
template
const nsDependentSubstring
nsContentUtils::TrimWhitespace<nsContentUtils::IsHTMLWhitespace>(const nsAString&, bool);

static inline void KeyAppendSep(nsACString& aKey)
{
  if (!aKey.IsEmpty()) {
    aKey.Append('>');
  }
}

static inline void KeyAppendString(const nsAString& aString, nsACString& aKey)
{
  KeyAppendSep(aKey);

  // Could escape separator here if collisions happen.  > is not a legal char
  // for a name or type attribute, so we should be safe avoiding that extra work.

  AppendUTF16toUTF8(aString, aKey);
}

static inline void KeyAppendString(const nsACString& aString, nsACString& aKey)
{
  KeyAppendSep(aKey);

  // Could escape separator here if collisions happen.  > is not a legal char
  // for a name or type attribute, so we should be safe avoiding that extra work.

  aKey.Append(aString);
}

static inline void KeyAppendInt(PRInt32 aInt, nsACString& aKey)
{
  KeyAppendSep(aKey);

  aKey.Append(nsPrintfCString("%d", aInt));
}

static inline void KeyAppendAtom(nsIAtom* aAtom, nsACString& aKey)
{
  NS_PRECONDITION(aAtom, "KeyAppendAtom: aAtom can not be null!\n");

  KeyAppendString(nsAtomCString(aAtom), aKey);
}

static inline bool IsAutocompleteOff(const nsIContent* aElement)
{
  return aElement->AttrValueIs(kNameSpaceID_None, nsGkAtoms::autocomplete,
                               NS_LITERAL_STRING("off"), eIgnoreCase);
}

/*static*/ nsresult
nsContentUtils::GenerateStateKey(nsIContent* aContent,
                                 const nsIDocument* aDocument,
                                 nsIStatefulFrame::SpecialStateID aID,
                                 nsACString& aKey)
{
  aKey.Truncate();

  PRUint32 partID = aDocument ? aDocument->GetPartID() : 0;

  // SpecialStateID case - e.g. scrollbars around the content window
  // The key in this case is a special state id
  if (nsIStatefulFrame::eNoID != aID) {
    KeyAppendInt(partID, aKey);  // first append a partID
    KeyAppendInt(aID, aKey);
    return NS_OK;
  }

  // We must have content if we're not using a special state id
  NS_ENSURE_TRUE(aContent, NS_ERROR_FAILURE);

  // Don't capture state for anonymous content
  if (aContent->IsInAnonymousSubtree()) {
    return NS_OK;
  }

  if (IsAutocompleteOff(aContent)) {
    return NS_OK;
  }

  nsCOMPtr<nsIHTMLDocument> htmlDocument(do_QueryInterface(aContent->GetCurrentDoc()));

  KeyAppendInt(partID, aKey);  // first append a partID
  // Make sure we can't possibly collide with an nsIStatefulFrame
  // special id of some sort
  KeyAppendInt(nsIStatefulFrame::eNoID, aKey);
  bool generatedUniqueKey = false;

  if (htmlDocument) {
    // Flush our content model so it'll be up to date
    // If this becomes unnecessary and the following line is removed,
    // please also remove the corresponding flush operation from
    // nsHtml5TreeBuilderCppSupplement.h. (Look for "See bug 497861." there.)
    aContent->GetCurrentDoc()->FlushPendingNotifications(Flush_Content);

    nsContentList *htmlForms = htmlDocument->GetForms();
    nsContentList *htmlFormControls = htmlDocument->GetFormControls();

    NS_ENSURE_TRUE(htmlForms && htmlFormControls, NS_ERROR_OUT_OF_MEMORY);

    // If we have a form control and can calculate form information, use that
    // as the key - it is more reliable than just recording position in the
    // DOM.
    // XXXbz Is it, really?  We have bugs on this, I think...
    // Important to have a unique key, and tag/type/name may not be.
    //
    // If the control has a form, the format of the key is:
    // f>type>IndOfFormInDoc>IndOfControlInForm>FormName>name
    // else:
    // d>type>IndOfControlInDoc>name
    //
    // XXX We don't need to use index if name is there
    // XXXbz We don't?  Why not?  I don't follow.
    //
    nsCOMPtr<nsIFormControl> control(do_QueryInterface(aContent));
    if (control && htmlFormControls && htmlForms) {

      // Append the control type
      KeyAppendInt(control->GetType(), aKey);

      // If in a form, add form name / index of form / index in form
      PRInt32 index = -1;
      Element *formElement = control->GetFormElement();
      if (formElement) {
        if (IsAutocompleteOff(formElement)) {
          aKey.Truncate();
          return NS_OK;
        }

        KeyAppendString(NS_LITERAL_CSTRING("f"), aKey);

        // Append the index of the form in the document
        index = htmlForms->IndexOf(formElement, false);
        if (index <= -1) {
          //
          // XXX HACK this uses some state that was dumped into the document
          // specifically to fix bug 138892.  What we are trying to do is *guess*
          // which form this control's state is found in, with the highly likely
          // guess that the highest form parsed so far is the one.
          // This code should not be on trunk, only branch.
          //
          index = htmlDocument->GetNumFormsSynchronous() - 1;
        }
        if (index > -1) {
          KeyAppendInt(index, aKey);

          // Append the index of the control in the form
          nsCOMPtr<nsIForm> form(do_QueryInterface(formElement));
          index = form->IndexOfControl(control);

          if (index > -1) {
            KeyAppendInt(index, aKey);
            generatedUniqueKey = true;
          }
        }

        // Append the form name
        nsAutoString formName;
        formElement->GetAttr(kNameSpaceID_None, nsGkAtoms::name, formName);
        KeyAppendString(formName, aKey);

      } else {

        KeyAppendString(NS_LITERAL_CSTRING("d"), aKey);

        // If not in a form, add index of control in document
        // Less desirable than indexing by form info.

        // Hash by index of control in doc (we are not in a form)
        // These are important as they are unique, and type/name may not be.

        // We have to flush sink notifications at this point to make
        // sure that htmlFormControls is up to date.
        index = htmlFormControls->IndexOf(aContent, true);
        if (index > -1) {
          KeyAppendInt(index, aKey);
          generatedUniqueKey = true;
        }
      }

      // Append the control name
      nsAutoString name;
      aContent->GetAttr(kNameSpaceID_None, nsGkAtoms::name, name);
      KeyAppendString(name, aKey);
    }
  }

  if (!generatedUniqueKey) {
    // Either we didn't have a form control or we aren't in an HTML document so
    // we can't figure out form info.  Append the tag name if it's an element
    // to avoid restoring state for one type of element on another type.
    if (aContent->IsElement()) {
      KeyAppendString(nsDependentAtomString(aContent->Tag()), aKey);
    }
    else {
      // Append a character that is not "d" or "f" to disambiguate from
      // the case when we were a form control in an HTML document.
      KeyAppendString(NS_LITERAL_CSTRING("o"), aKey);
    }

    // Now start at aContent and append the indices of it and all its ancestors
    // in their containers.  That should at least pin down its position in the
    // DOM...
    nsINode* parent = aContent->GetNodeParent();
    nsINode* content = aContent;
    while (parent) {
      KeyAppendInt(parent->IndexOf(content), aKey);
      content = parent;
      parent = content->GetNodeParent();
    }
  }

  return NS_OK;
}

// static
nsresult
nsContentUtils::NewURIWithDocumentCharset(nsIURI** aResult,
                                          const nsAString& aSpec,
                                          nsIDocument* aDocument,
                                          nsIURI* aBaseURI)
{
  return NS_NewURI(aResult, aSpec,
                   aDocument ? aDocument->GetDocumentCharacterSet().get() : nsnull,
                   aBaseURI, sIOService);
}

// static
bool
nsContentUtils::BelongsInForm(nsIContent *aForm,
                              nsIContent *aContent)
{
  NS_PRECONDITION(aForm, "Must have a form");
  NS_PRECONDITION(aContent, "Must have a content node");

  if (aForm == aContent) {
    // A form does not belong inside itself, so we return false here

    return false;
  }

  nsIContent* content = aContent->GetParent();

  while (content) {
    if (content == aForm) {
      // aContent is contained within the form so we return true.

      return true;
    }

    if (content->Tag() == nsGkAtoms::form &&
        content->IsHTML()) {
      // The child is contained within a form, but not the right form
      // so we ignore it.

      return false;
    }

    content = content->GetParent();
  }

  if (aForm->GetChildCount() > 0) {
    // The form is a container but aContent wasn't inside the form,
    // return false

    return false;
  }

  // The form is a leaf and aContent wasn't inside any other form so
  // we check whether the content comes after the form.  If it does,
  // return true.  If it does not, then it couldn't have been inside
  // the form in the HTML.
  if (PositionIsBefore(aForm, aContent)) {
    // We could be in this form!
    // In the future, we may want to get document.forms, look at the
    // form after aForm, and if aContent is after that form after
    // aForm return false here....
    return true;
  }

  return false;
}

// static
nsresult
nsContentUtils::CheckQName(const nsAString& aQualifiedName,
                           bool aNamespaceAware,
                           const PRUnichar** aColon)
{
  const char* colon = nsnull;
  const PRUnichar* begin = aQualifiedName.BeginReading();
  const PRUnichar* end = aQualifiedName.EndReading();
  
  int result = MOZ_XMLCheckQName(reinterpret_cast<const char*>(begin),
                                 reinterpret_cast<const char*>(end),
                                 aNamespaceAware, &colon);

  if (!result) {
    if (aColon) {
      *aColon = reinterpret_cast<const PRUnichar*>(colon);
    }

    return NS_OK;
  }

  // MOZ_EXPAT_EMPTY_QNAME || MOZ_EXPAT_INVALID_CHARACTER
  if (result == (1 << 0) || result == (1 << 1)) {
    return NS_ERROR_DOM_INVALID_CHARACTER_ERR;
  }

  return NS_ERROR_DOM_NAMESPACE_ERR;
}

//static
nsresult
nsContentUtils::SplitQName(const nsIContent* aNamespaceResolver,
                           const nsAFlatString& aQName,
                           PRInt32 *aNamespace, nsIAtom **aLocalName)
{
  const PRUnichar* colon;
  nsresult rv = nsContentUtils::CheckQName(aQName, true, &colon);
  NS_ENSURE_SUCCESS(rv, rv);

  if (colon) {
    const PRUnichar* end;
    aQName.EndReading(end);
    nsAutoString nameSpace;
    rv = aNamespaceResolver->LookupNamespaceURIInternal(Substring(aQName.get(),
                                                                  colon),
                                                        nameSpace);
    NS_ENSURE_SUCCESS(rv, rv);

    *aNamespace = NameSpaceManager()->GetNameSpaceID(nameSpace);
    if (*aNamespace == kNameSpaceID_Unknown)
      return NS_ERROR_FAILURE;

    *aLocalName = NS_NewAtom(Substring(colon + 1, end));
  }
  else {
    *aNamespace = kNameSpaceID_None;
    *aLocalName = NS_NewAtom(aQName);
  }
  NS_ENSURE_TRUE(aLocalName, NS_ERROR_OUT_OF_MEMORY);
  return NS_OK;
}

// static
nsresult
nsContentUtils::GetNodeInfoFromQName(const nsAString& aNamespaceURI,
                                     const nsAString& aQualifiedName,
                                     nsNodeInfoManager* aNodeInfoManager,
                                     PRUint16 aNodeType,
                                     nsINodeInfo** aNodeInfo)
{
  const nsAFlatString& qName = PromiseFlatString(aQualifiedName);
  const PRUnichar* colon;
  nsresult rv = nsContentUtils::CheckQName(qName, true, &colon);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 nsID;
  sNameSpaceManager->RegisterNameSpace(aNamespaceURI, nsID);
  if (colon) {
    const PRUnichar* end;
    qName.EndReading(end);

    nsCOMPtr<nsIAtom> prefix = do_GetAtom(Substring(qName.get(), colon));

    rv = aNodeInfoManager->GetNodeInfo(Substring(colon + 1, end), prefix,
                                       nsID, aNodeType, aNodeInfo);
  }
  else {
    rv = aNodeInfoManager->GetNodeInfo(aQualifiedName, nsnull, nsID,
                                       aNodeType, aNodeInfo);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  return nsContentUtils::IsValidNodeName((*aNodeInfo)->NameAtom(),
                                         (*aNodeInfo)->GetPrefixAtom(),
                                         (*aNodeInfo)->NamespaceID()) ?
         NS_OK : NS_ERROR_DOM_NAMESPACE_ERR;
}

// static
void
nsContentUtils::SplitExpatName(const PRUnichar *aExpatName, nsIAtom **aPrefix,
                               nsIAtom **aLocalName, PRInt32* aNameSpaceID)
{
  /**
   *  Expat can send the following:
   *    localName
   *    namespaceURI<separator>localName
   *    namespaceURI<separator>localName<separator>prefix
   *
   *  and we use 0xFFFF for the <separator>.
   *
   */

  const PRUnichar *uriEnd = nsnull;
  const PRUnichar *nameEnd = nsnull;
  const PRUnichar *pos;
  for (pos = aExpatName; *pos; ++pos) {
    if (*pos == 0xFFFF) {
      if (uriEnd) {
        nameEnd = pos;
      }
      else {
        uriEnd = pos;
      }
    }
  }

  const PRUnichar *nameStart;
  if (uriEnd) {
    if (sNameSpaceManager) {
      sNameSpaceManager->RegisterNameSpace(nsDependentSubstring(aExpatName,
                                                                uriEnd),
                                           *aNameSpaceID);
    }
    else {
      *aNameSpaceID = kNameSpaceID_Unknown;
    }

    nameStart = (uriEnd + 1);
    if (nameEnd)  {
      const PRUnichar *prefixStart = nameEnd + 1;
      *aPrefix = NS_NewAtom(Substring(prefixStart, pos));
    }
    else {
      nameEnd = pos;
      *aPrefix = nsnull;
    }
  }
  else {
    *aNameSpaceID = kNameSpaceID_None;
    nameStart = aExpatName;
    nameEnd = pos;
    *aPrefix = nsnull;
  }
  *aLocalName = NS_NewAtom(Substring(nameStart, nameEnd));
}

// static
nsPresContext*
nsContentUtils::GetContextForContent(const nsIContent* aContent)
{
  nsIDocument* doc = aContent->GetCurrentDoc();
  if (doc) {
    nsIPresShell *presShell = doc->GetShell();
    if (presShell) {
      return presShell->GetPresContext();
    }
  }
  return nsnull;
}

// static
bool
nsContentUtils::CanLoadImage(nsIURI* aURI, nsISupports* aContext,
                             nsIDocument* aLoadingDocument,
                             nsIPrincipal* aLoadingPrincipal,
                             PRInt16* aImageBlockingStatus)
{
  NS_PRECONDITION(aURI, "Must have a URI");
  NS_PRECONDITION(aLoadingDocument, "Must have a document");
  NS_PRECONDITION(aLoadingPrincipal, "Must have a loading principal");

  nsresult rv;

  PRUint32 appType = nsIDocShell::APP_TYPE_UNKNOWN;

  {
    nsCOMPtr<nsISupports> container = aLoadingDocument->GetContainer();
    nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem =
      do_QueryInterface(container);

    if (docShellTreeItem) {
      nsCOMPtr<nsIDocShellTreeItem> root;
      docShellTreeItem->GetRootTreeItem(getter_AddRefs(root));

      nsCOMPtr<nsIDocShell> docShell(do_QueryInterface(root));

      if (!docShell || NS_FAILED(docShell->GetAppType(&appType))) {
        appType = nsIDocShell::APP_TYPE_UNKNOWN;
      }
    }
  }

  if (appType != nsIDocShell::APP_TYPE_EDITOR) {
    // Editor apps get special treatment here, editors can load images
    // from anywhere.  This allows editor to insert images from file://
    // into documents that are being edited.
    rv = sSecurityManager->
      CheckLoadURIWithPrincipal(aLoadingPrincipal, aURI,
                                nsIScriptSecurityManager::ALLOW_CHROME);
    if (NS_FAILED(rv)) {
      if (aImageBlockingStatus) {
        // Reject the request itself, not all requests to the relevant
        // server...
        *aImageBlockingStatus = nsIContentPolicy::REJECT_REQUEST;
      }
      return false;
    }
  }

  PRInt16 decision = nsIContentPolicy::ACCEPT;

  rv = NS_CheckContentLoadPolicy(nsIContentPolicy::TYPE_IMAGE,
                                 aURI,
                                 aLoadingPrincipal,
                                 aContext,
                                 EmptyCString(), //mime guess
                                 nsnull,         //extra
                                 &decision,
                                 GetContentPolicy(),
                                 sSecurityManager);

  if (aImageBlockingStatus) {
    *aImageBlockingStatus =
      NS_FAILED(rv) ? nsIContentPolicy::REJECT_REQUEST : decision;
  }
  return NS_FAILED(rv) ? false : NS_CP_ACCEPTED(decision);
}

// static
bool
nsContentUtils::IsImageInCache(nsIURI* aURI)
{
    if (!sImgLoaderInitialized)
        InitImgLoader();

    if (!sImgCache) return false;

    // If something unexpected happened we return false, otherwise if props
    // is set, the image is cached and we return true
    nsCOMPtr<nsIProperties> props;
    nsresult rv = sImgCache->FindEntryProperties(aURI, getter_AddRefs(props));
    return (NS_SUCCEEDED(rv) && props);
}

// static
nsresult
nsContentUtils::LoadImage(nsIURI* aURI, nsIDocument* aLoadingDocument,
                          nsIPrincipal* aLoadingPrincipal, nsIURI* aReferrer,
                          imgIDecoderObserver* aObserver, PRInt32 aLoadFlags,
                          imgIRequest** aRequest)
{
  NS_PRECONDITION(aURI, "Must have a URI");
  NS_PRECONDITION(aLoadingDocument, "Must have a document");
  NS_PRECONDITION(aLoadingPrincipal, "Must have a principal");
  NS_PRECONDITION(aRequest, "Null out param");

  imgILoader* imgLoader = GetImgLoader();
  if (!imgLoader) {
    // nothing we can do here
    return NS_OK;
  }

  nsCOMPtr<nsILoadGroup> loadGroup = aLoadingDocument->GetDocumentLoadGroup();
  NS_ASSERTION(loadGroup, "Could not get loadgroup; onload may fire too early");

  nsIURI *documentURI = aLoadingDocument->GetDocumentURI();

  // check for a Content Security Policy to pass down to the channel that
  // will get created to load the image
  nsCOMPtr<nsIChannelPolicy> channelPolicy;
  nsCOMPtr<nsIContentSecurityPolicy> csp;
  if (aLoadingPrincipal) {
    nsresult rv = aLoadingPrincipal->GetCsp(getter_AddRefs(csp));
    NS_ENSURE_SUCCESS(rv, rv);
    if (csp) {
      channelPolicy = do_CreateInstance("@mozilla.org/nschannelpolicy;1");
      channelPolicy->SetContentSecurityPolicy(csp);
      channelPolicy->SetLoadType(nsIContentPolicy::TYPE_IMAGE);
    }
  }
    
  // Make the URI immutable so people won't change it under us
  NS_TryToSetImmutable(aURI);

  // XXXbz using "documentURI" for the initialDocumentURI is not quite
  // right, but the best we can do here...
  return imgLoader->LoadImage(aURI,                 /* uri to load */
                              documentURI,          /* initialDocumentURI */
                              aReferrer,            /* referrer */
                              aLoadingPrincipal,    /* loading principal */
                              loadGroup,            /* loadgroup */
                              aObserver,            /* imgIDecoderObserver */
                              aLoadingDocument,     /* uniquification key */
                              aLoadFlags,           /* load flags */
                              nsnull,               /* cache key */
                              nsnull,               /* existing request*/
                              channelPolicy,        /* CSP info */
                              aRequest);
}

// static
already_AddRefed<imgIContainer>
nsContentUtils::GetImageFromContent(nsIImageLoadingContent* aContent,
                                    imgIRequest **aRequest)
{
  if (aRequest) {
    *aRequest = nsnull;
  }

  NS_ENSURE_TRUE(aContent, nsnull);

  nsCOMPtr<imgIRequest> imgRequest;
  aContent->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                      getter_AddRefs(imgRequest));
  if (!imgRequest) {
    return nsnull;
  }

  nsCOMPtr<imgIContainer> imgContainer;
  imgRequest->GetImage(getter_AddRefs(imgContainer));

  if (!imgContainer) {
    return nsnull;
  }

  if (aRequest) {
    imgRequest.swap(*aRequest);
  }

  return imgContainer.forget();
}

//static
already_AddRefed<imgIRequest>
nsContentUtils::GetStaticRequest(imgIRequest* aRequest)
{
  NS_ENSURE_TRUE(aRequest, nsnull);
  nsCOMPtr<imgIRequest> retval;
  aRequest->GetStaticRequest(getter_AddRefs(retval));
  return retval.forget();
}

// static
bool
nsContentUtils::ContentIsDraggable(nsIContent* aContent)
{
  nsCOMPtr<nsIDOMHTMLElement> htmlElement = do_QueryInterface(aContent);
  if (htmlElement) {
    bool draggable = false;
    htmlElement->GetDraggable(&draggable);
    if (draggable)
      return true;

    if (aContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::draggable,
                              nsGkAtoms::_false, eIgnoreCase))
      return false;
  }

  // special handling for content area image and link dragging
  return IsDraggableImage(aContent) || IsDraggableLink(aContent);
}

// static
bool
nsContentUtils::IsDraggableImage(nsIContent* aContent)
{
  NS_PRECONDITION(aContent, "Must have content node to test");

  nsCOMPtr<nsIImageLoadingContent> imageContent(do_QueryInterface(aContent));
  if (!imageContent) {
    return false;
  }

  nsCOMPtr<imgIRequest> imgRequest;
  imageContent->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                           getter_AddRefs(imgRequest));

  // XXXbz It may be draggable even if the request resulted in an error.  Why?
  // Not sure; that's what the old nsContentAreaDragDrop/nsFrame code did.
  return imgRequest != nsnull;
}

// static
bool
nsContentUtils::IsDraggableLink(const nsIContent* aContent) {
  nsCOMPtr<nsIURI> absURI;
  return aContent->IsLink(getter_AddRefs(absURI));
}

static bool
TestSitePerm(nsIPrincipal* aPrincipal, const char* aType, PRUint32 aPerm)
{
  if (nsContentUtils::IsSystemPrincipal(aPrincipal)) {
    // System principal is always allowed and never denied permission.
    return aPerm == nsIPermissionManager::ALLOW_ACTION;
  }

  nsCOMPtr<nsIURI> uri;
  if (NS_FAILED(!aPrincipal ||
      aPrincipal->GetURI(getter_AddRefs(uri))) ||
      !uri) {
    // We always deny (i.e. don't allow) the permission if we don't
    // have a principal or we don't know the URI.
    return aPerm != nsIPermissionManager::ALLOW_ACTION;
  }

  nsCOMPtr<nsIPermissionManager> permMgr =
    do_GetService("@mozilla.org/permissionmanager;1");
  NS_ENSURE_TRUE(permMgr, false);

  PRUint32 perm;
  nsresult rv = permMgr->TestPermission(uri, aType, &perm);
  NS_ENSURE_SUCCESS(rv, false);
  
  return perm == aPerm;
}

bool
nsContentUtils::IsSitePermAllow(nsIPrincipal* aPrincipal, const char* aType)
{
  return TestSitePerm(aPrincipal, aType, nsIPermissionManager::ALLOW_ACTION);
}

bool
nsContentUtils::IsSitePermDeny(nsIPrincipal* aPrincipal, const char* aType)
{
  return TestSitePerm(aPrincipal, aType, nsIPermissionManager::DENY_ACTION);
}

static const char *gEventNames[] = {"event"};
static const char *gSVGEventNames[] = {"evt"};
// for b/w compat, the first name to onerror is still 'event', even though it
// is actually the error message.  (pre this code, the other 2 were not avail.)
// XXXmarkh - a quick lxr shows no affected code - should we correct this?
static const char *gOnErrorNames[] = {"event", "source", "lineno"};

// static
void
nsContentUtils::GetEventArgNames(PRInt32 aNameSpaceID,
                                 nsIAtom *aEventName,
                                 PRUint32 *aArgCount,
                                 const char*** aArgArray)
{
#define SET_EVENT_ARG_NAMES(names) \
    *aArgCount = sizeof(names)/sizeof(names[0]); \
    *aArgArray = names;

  // nsJSEventListener is what does the arg magic for onerror, and it does
  // not seem to take the namespace into account.  So we let onerror in all
  // namespaces get the 3 arg names.
  if (aEventName == nsGkAtoms::onerror) {
    SET_EVENT_ARG_NAMES(gOnErrorNames);
  } else if (aNameSpaceID == kNameSpaceID_SVG) {
    SET_EVENT_ARG_NAMES(gSVGEventNames);
  } else {
    SET_EVENT_ARG_NAMES(gEventNames);
  }
}

nsCxPusher::nsCxPusher()
    : mScriptIsRunning(false),
      mPushedSomething(false)
{
}

nsCxPusher::~nsCxPusher()
{
  Pop();
}

static bool
IsContextOnStack(nsIJSContextStack *aStack, JSContext *aContext)
{
  JSContext *ctx = nsnull;
  aStack->Peek(&ctx);
  if (!ctx)
    return false;
  if (ctx == aContext)
    return true;

  nsCOMPtr<nsIJSContextStackIterator>
    iterator(do_CreateInstance("@mozilla.org/js/xpc/ContextStackIterator;1"));
  NS_ENSURE_TRUE(iterator, false);

  nsresult rv = iterator->Reset(aStack);
  NS_ENSURE_SUCCESS(rv, false);

  bool done;
  while (NS_SUCCEEDED(iterator->Done(&done)) && !done) {
    rv = iterator->Prev(&ctx);
    NS_ASSERTION(NS_SUCCEEDED(rv), "Broken iterator implementation");

    if (!ctx) {
      continue;
    }

    if (nsJSUtils::GetDynamicScriptContext(ctx) && ctx == aContext)
      return true;
  }

  return false;
}

bool
nsCxPusher::Push(nsIDOMEventTarget *aCurrentTarget)
{
  if (mPushedSomething) {
    NS_ERROR("Whaaa! No double pushing with nsCxPusher::Push()!");

    return false;
  }

  NS_ENSURE_TRUE(aCurrentTarget, false);
  nsresult rv;
  nsIScriptContext* scx =
    aCurrentTarget->GetContextForEventHandlers(&rv);
  NS_ENSURE_SUCCESS(rv, false);

  if (!scx) {
    // The target may have a special JS context for event handlers.
    JSContext* cx = aCurrentTarget->GetJSContextForEventHandlers();
    if (cx) {
      DoPush(cx);
    }

    // Nothing to do here, I guess.  Have to return true so that event firing
    // will still work correctly even if there is no associated JSContext
    return true;
  }

  JSContext* cx = nsnull;

  if (scx) {
    cx = scx->GetNativeContext();
    // Bad, no JSContext from script context!
    NS_ENSURE_TRUE(cx, false);
  }

  // If there's no native context in the script context it must be
  // in the process or being torn down. We don't want to notify the
  // script context about scripts having been evaluated in such a
  // case, calling with a null cx is fine in that case.
  return Push(cx);
}

bool
nsCxPusher::RePush(nsIDOMEventTarget *aCurrentTarget)
{
  if (!mPushedSomething) {
    return Push(aCurrentTarget);
  }

  if (aCurrentTarget) {
    nsresult rv;
    nsIScriptContext* scx =
      aCurrentTarget->GetContextForEventHandlers(&rv);
    if (NS_FAILED(rv)) {
      Pop();
      return false;
    }

    // If we have the same script context and native context is still
    // alive, no need to Pop/Push.
    if (scx && scx == mScx &&
        scx->GetNativeContext()) {
      return true;
    }
  }

  Pop();
  return Push(aCurrentTarget);
}

bool
nsCxPusher::Push(JSContext *cx, bool aRequiresScriptContext)
{
  if (mPushedSomething) {
    NS_ERROR("Whaaa! No double pushing with nsCxPusher::Push()!");

    return false;
  }

  if (!cx) {
    return false;
  }

  // Hold a strong ref to the nsIScriptContext, just in case
  // XXXbz do we really need to?  If we don't get one of these in Pop(), is
  // that really a problem?  Or do we need to do this to effectively root |cx|?
  mScx = GetScriptContextFromJSContext(cx);
  if (!mScx && aRequiresScriptContext) {
    // Should probably return false. See bug 416916.
    return true;
  }

  return DoPush(cx);
}

bool
nsCxPusher::DoPush(JSContext* cx)
{
  nsIThreadJSContextStack* stack = nsContentUtils::ThreadJSContextStack();
  if (!stack) {
    return true;
  }

  if (cx && IsContextOnStack(stack, cx)) {
    // If the context is on the stack, that means that a script
    // is running at the moment in the context.
    mScriptIsRunning = true;
  }

  if (NS_FAILED(stack->Push(cx))) {
    mScriptIsRunning = false;
    mScx = nsnull;
    return false;
  }

  mPushedSomething = true;
#ifdef DEBUG
  mPushedContext = cx;
#endif
  return true;
}

bool
nsCxPusher::PushNull()
{
  return DoPush(nsnull);
}

void
nsCxPusher::Pop()
{
  nsIThreadJSContextStack* stack = nsContentUtils::ThreadJSContextStack();
  if (!mPushedSomething || !stack) {
    mScx = nsnull;
    mPushedSomething = false;

    NS_ASSERTION(!mScriptIsRunning, "Huh, this can't be happening, "
                 "mScriptIsRunning can't be set here!");

    return;
  }

  JSContext *unused;
  stack->Pop(&unused);

  NS_ASSERTION(unused == mPushedContext, "Unexpected context popped");

  if (!mScriptIsRunning && mScx) {
    // No JS is running in the context, but executing the event handler might have
    // caused some JS to run. Tell the script context that it's done.

    mScx->ScriptEvaluated(true);
  }

  mScx = nsnull;
  mScriptIsRunning = false;
  mPushedSomething = false;
}

static const char gPropertiesFiles[nsContentUtils::PropertiesFile_COUNT][56] = {
  // Must line up with the enum values in |PropertiesFile| enum.
  "chrome://global/locale/css.properties",
  "chrome://global/locale/xbl.properties",
  "chrome://global/locale/xul.properties",
  "chrome://global/locale/layout_errors.properties",
  "chrome://global/locale/layout/HtmlForm.properties",
  "chrome://global/locale/printing.properties",
  "chrome://global/locale/dom/dom.properties",
  "chrome://global/locale/layout/htmlparser.properties",
  "chrome://global/locale/svg/svg.properties",
  "chrome://branding/locale/brand.properties",
  "chrome://global/locale/commonDialogs.properties"
};

/* static */ nsresult
nsContentUtils::EnsureStringBundle(PropertiesFile aFile)
{
  if (!sStringBundles[aFile]) {
    if (!sStringBundleService) {
      nsresult rv =
        CallGetService(NS_STRINGBUNDLE_CONTRACTID, &sStringBundleService);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    nsIStringBundle *bundle;
    nsresult rv =
      sStringBundleService->CreateBundle(gPropertiesFiles[aFile], &bundle);
    NS_ENSURE_SUCCESS(rv, rv);
    sStringBundles[aFile] = bundle; // transfer ownership
  }
  return NS_OK;
}

/* static */
nsresult nsContentUtils::GetLocalizedString(PropertiesFile aFile,
                                            const char* aKey,
                                            nsXPIDLString& aResult)
{
  nsresult rv = EnsureStringBundle(aFile);
  NS_ENSURE_SUCCESS(rv, rv);
  nsIStringBundle *bundle = sStringBundles[aFile];

  return bundle->GetStringFromName(NS_ConvertASCIItoUTF16(aKey).get(),
                                   getter_Copies(aResult));
}

/* static */
nsresult nsContentUtils::FormatLocalizedString(PropertiesFile aFile,
                                               const char* aKey,
                                               const PRUnichar **aParams,
                                               PRUint32 aParamsLength,
                                               nsXPIDLString& aResult)
{
  nsresult rv = EnsureStringBundle(aFile);
  NS_ENSURE_SUCCESS(rv, rv);
  nsIStringBundle *bundle = sStringBundles[aFile];

  return bundle->FormatStringFromName(NS_ConvertASCIItoUTF16(aKey).get(),
                                      aParams, aParamsLength,
                                      getter_Copies(aResult));
}

/* static */ nsresult
nsContentUtils::ReportToConsole(PRUint32 aErrorFlags,
                                const char *aCategory,
                                nsIDocument* aDocument,
                                PropertiesFile aFile,
                                const char *aMessageName,
                                const PRUnichar **aParams,
                                PRUint32 aParamsLength,
                                nsIURI* aURI,
                                const nsAFlatString& aSourceLine,
                                PRUint32 aLineNumber,
                                PRUint32 aColumnNumber)
{
  NS_ASSERTION((aParams && aParamsLength) || (!aParams && !aParamsLength),
               "Supply either both parameters and their number or no"
               "parameters and 0.");

  PRUint64 innerWindowID = 0;
  if (aDocument) {
    if (!aURI) {
      aURI = aDocument->GetDocumentURI();
    }
    innerWindowID = aDocument->InnerWindowID();
  }

  nsresult rv;
  if (!sConsoleService) { // only need to bother null-checking here
    rv = CallGetService(NS_CONSOLESERVICE_CONTRACTID, &sConsoleService);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsXPIDLString errorText;
  if (aParams) {
    rv = FormatLocalizedString(aFile, aMessageName, aParams, aParamsLength,
                               errorText);
  }
  else {
    rv = GetLocalizedString(aFile, aMessageName, errorText);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString spec;
  if (!aLineNumber) {
    JSContext *cx = nsnull;
    sThreadJSContextStack->Peek(&cx);
    if (cx) {
      const char* filename;
      PRUint32 lineno;
      if (nsJSUtils::GetCallingLocation(cx, &filename, &lineno)) {
        spec = filename;
        aLineNumber = lineno;
      }
    }
  }
  if (spec.IsEmpty() && aURI)
    aURI->GetSpec(spec);

  nsCOMPtr<nsIScriptError> errorObject =
      do_CreateInstance(NS_SCRIPTERROR_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = errorObject->InitWithWindowID(errorText.get(),
                                     NS_ConvertUTF8toUTF16(spec).get(), // file name
                                     aSourceLine.get(),
                                     aLineNumber, aColumnNumber,
                                     aErrorFlags, aCategory,
                                     innerWindowID);
  NS_ENSURE_SUCCESS(rv, rv);

  return sConsoleService->LogMessage(errorObject);
}

bool
nsContentUtils::IsChromeDoc(nsIDocument *aDocument)
{
  if (!aDocument) {
    return false;
  }
  
  nsCOMPtr<nsIPrincipal> systemPrincipal;
  sSecurityManager->GetSystemPrincipal(getter_AddRefs(systemPrincipal));

  return aDocument->NodePrincipal() == systemPrincipal;
}

bool
nsContentUtils::IsChildOfSameType(nsIDocument* aDoc)
{
  nsCOMPtr<nsISupports> container = aDoc->GetContainer();
  nsCOMPtr<nsIDocShellTreeItem> docShellAsItem(do_QueryInterface(container));
  nsCOMPtr<nsIDocShellTreeItem> sameTypeParent;
  if (docShellAsItem) {
    docShellAsItem->GetSameTypeParent(getter_AddRefs(sameTypeParent));
  }
  return sameTypeParent != nsnull;
}

bool
nsContentUtils::GetWrapperSafeScriptFilename(nsIDocument *aDocument,
                                             nsIURI *aURI,
                                             nsACString& aScriptURI)
{
  bool scriptFileNameModified = false;
  aURI->GetSpec(aScriptURI);

  if (IsChromeDoc(aDocument)) {
    nsCOMPtr<nsIChromeRegistry> chromeReg =
      mozilla::services::GetChromeRegistryService();

    if (!chromeReg) {
      // If we're running w/o a chrome registry we won't modify any
      // script file names.

      return scriptFileNameModified;
    }

    bool docWrappersEnabled =
      chromeReg->WrappersEnabled(aDocument->GetDocumentURI());

    bool uriWrappersEnabled = chromeReg->WrappersEnabled(aURI);

    nsIURI *docURI = aDocument->GetDocumentURI();

    if (docURI && docWrappersEnabled && !uriWrappersEnabled) {
      // aURI is a script from a URL that doesn't get wrapper
      // automation. aDocument is a chrome document that does get
      // wrapper automation. Prepend the chrome document's URI
      // followed by the string " -> " to the URI of the script we're
      // loading here so that script in that URI gets the same wrapper
      // automation that the chrome document expects.
      nsCAutoString spec;
      docURI->GetSpec(spec);
      spec.AppendASCII(" -> ");
      spec.Append(aScriptURI);

      aScriptURI = spec;

      scriptFileNameModified = true;
    }
  }

  return scriptFileNameModified;
}

// static
bool
nsContentUtils::IsInChromeDocshell(nsIDocument *aDocument)
{
  if (!aDocument) {
    return false;
  }

  if (aDocument->GetDisplayDocument()) {
    return IsInChromeDocshell(aDocument->GetDisplayDocument());
  }

  nsCOMPtr<nsISupports> docContainer = aDocument->GetContainer();
  nsCOMPtr<nsIDocShellTreeItem> docShell(do_QueryInterface(docContainer));
  PRInt32 itemType = nsIDocShellTreeItem::typeContent;
  if (docShell) {
    docShell->GetItemType(&itemType);
  }

  return itemType == nsIDocShellTreeItem::typeChrome;
}

// static
nsIContentPolicy*
nsContentUtils::GetContentPolicy()
{
  if (!sTriedToGetContentPolicy) {
    CallGetService(NS_CONTENTPOLICY_CONTRACTID, &sContentPolicyService);
    // It's OK to not have a content policy service
    sTriedToGetContentPolicy = true;
  }

  return sContentPolicyService;
}

// static
bool
nsContentUtils::IsEventAttributeName(nsIAtom* aName, PRInt32 aType)
{
  const PRUnichar* name = aName->GetUTF16String();
  if (name[0] != 'o' || name[1] != 'n')
    return false;

  EventNameMapping mapping;
  return (sAtomEventTable->Get(aName, &mapping) && mapping.mType & aType);
}

// static
PRUint32
nsContentUtils::GetEventId(nsIAtom* aName)
{
  EventNameMapping mapping;
  if (sAtomEventTable->Get(aName, &mapping))
    return mapping.mId;

  return NS_USER_DEFINED_EVENT;
}

// static
PRUint32
nsContentUtils::GetEventCategory(const nsAString& aName)
{
  EventNameMapping mapping;
  if (sStringEventTable->Get(aName, &mapping))
    return mapping.mStructType;

  return NS_EVENT;
}

nsIAtom*
nsContentUtils::GetEventIdAndAtom(const nsAString& aName,
                                  PRUint32 aEventStruct,
                                  PRUint32* aEventID)
{
  EventNameMapping mapping;
  if (sStringEventTable->Get(aName, &mapping)) {
    *aEventID =
      mapping.mStructType == aEventStruct ? mapping.mId : NS_USER_DEFINED_EVENT;
    return mapping.mAtom;
  }

  // If we have cached lots of user defined event names, clear some of them.
  if (sUserDefinedEvents->Count() > 127) {
    while (sUserDefinedEvents->Count() > 64) {
      nsIAtom* first = sUserDefinedEvents->ObjectAt(0);
      sStringEventTable->Remove(Substring(nsDependentAtomString(first), 2));
      sUserDefinedEvents->RemoveObjectAt(0);
    }
  }

  *aEventID = NS_USER_DEFINED_EVENT;
  nsCOMPtr<nsIAtom> atom = do_GetAtom(NS_LITERAL_STRING("on") + aName);
  sUserDefinedEvents->AppendObject(atom);
  mapping.mAtom = atom;
  mapping.mId = NS_USER_DEFINED_EVENT;
  mapping.mType = EventNameType_None;
  mapping.mStructType = NS_EVENT_NULL;
  sStringEventTable->Put(aName, mapping);
  return mapping.mAtom;
}

static
nsresult GetEventAndTarget(nsIDocument* aDoc, nsISupports* aTarget,
                           const nsAString& aEventName,
                           bool aCanBubble, bool aCancelable,
                           bool aTrusted, nsIDOMEvent** aEvent,
                           nsIDOMEventTarget** aTargetOut)
{
  nsCOMPtr<nsIDOMDocument> domDoc = do_QueryInterface(aDoc);
  nsCOMPtr<nsIDOMEventTarget> target(do_QueryInterface(aTarget));
  NS_ENSURE_TRUE(domDoc && target, NS_ERROR_INVALID_ARG);

  nsCOMPtr<nsIDOMEvent> event;
  nsresult rv =
    domDoc->CreateEvent(NS_LITERAL_STRING("Events"), getter_AddRefs(event));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = event->InitEvent(aEventName, aCanBubble, aCancelable);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = event->SetTrusted(aTrusted);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = event->SetTarget(target);
  NS_ENSURE_SUCCESS(rv, rv);

  event.forget(aEvent);
  target.forget(aTargetOut);
  return NS_OK;
}

// static
nsresult
nsContentUtils::DispatchTrustedEvent(nsIDocument* aDoc, nsISupports* aTarget,
                                     const nsAString& aEventName,
                                     bool aCanBubble, bool aCancelable,
                                     bool *aDefaultAction)
{
  return DispatchEvent(aDoc, aTarget, aEventName, aCanBubble, aCancelable,
                       true, aDefaultAction);
}

// static
nsresult
nsContentUtils::DispatchUntrustedEvent(nsIDocument* aDoc, nsISupports* aTarget,
                                       const nsAString& aEventName,
                                       bool aCanBubble, bool aCancelable,
                                       bool *aDefaultAction)
{
  return DispatchEvent(aDoc, aTarget, aEventName, aCanBubble, aCancelable,
                       false, aDefaultAction);
}

// static
nsresult
nsContentUtils::DispatchEvent(nsIDocument* aDoc, nsISupports* aTarget,
                              const nsAString& aEventName,
                              bool aCanBubble, bool aCancelable,
                              bool aTrusted, bool *aDefaultAction)
{
  nsCOMPtr<nsIDOMEvent> event;
  nsCOMPtr<nsIDOMEventTarget> target;
  nsresult rv = GetEventAndTarget(aDoc, aTarget, aEventName, aCanBubble,
                                  aCancelable, aTrusted, getter_AddRefs(event),
                                  getter_AddRefs(target));
  NS_ENSURE_SUCCESS(rv, rv);

  bool dummy;
  return target->DispatchEvent(event, aDefaultAction ? aDefaultAction : &dummy);
}

nsresult
nsContentUtils::DispatchChromeEvent(nsIDocument *aDoc,
                                    nsISupports *aTarget,
                                    const nsAString& aEventName,
                                    bool aCanBubble, bool aCancelable,
                                    bool *aDefaultAction)
{

  nsCOMPtr<nsIDOMEvent> event;
  nsCOMPtr<nsIDOMEventTarget> target;
  nsresult rv = GetEventAndTarget(aDoc, aTarget, aEventName, aCanBubble,
                                  aCancelable, true, getter_AddRefs(event),
                                  getter_AddRefs(target));
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(aDoc, "GetEventAndTarget lied?");
  if (!aDoc->GetWindow())
    return NS_ERROR_INVALID_ARG;

  nsIDOMEventTarget* piTarget = aDoc->GetWindow()->GetParentTarget();
  if (!piTarget)
    return NS_ERROR_INVALID_ARG;

  nsEventStatus status = nsEventStatus_eIgnore;
  rv = piTarget->DispatchDOMEvent(nsnull, event, nsnull, &status);
  if (aDefaultAction) {
    *aDefaultAction = (status != nsEventStatus_eConsumeNoDefault);
  }
  return rv;
}

/* static */
Element*
nsContentUtils::MatchElementId(nsIContent *aContent, const nsIAtom* aId)
{
  for (nsIContent* cur = aContent;
       cur;
       cur = cur->GetNextNode(aContent)) {
    if (aId == cur->GetID()) {
      return cur->AsElement();
    }
  }

  return nsnull;
}

/* static */
Element *
nsContentUtils::MatchElementId(nsIContent *aContent, const nsAString& aId)
{
  NS_PRECONDITION(!aId.IsEmpty(), "Will match random elements");
  
  // ID attrs are generally stored as atoms, so just atomize this up front
  nsCOMPtr<nsIAtom> id(do_GetAtom(aId));
  if (!id) {
    // OOM, so just bail
    return nsnull;
  }

  return MatchElementId(aContent, id);
}

// Convert the string from the given charset to Unicode.
/* static */
nsresult
nsContentUtils::ConvertStringFromCharset(const nsACString& aCharset,
                                         const nsACString& aInput,
                                         nsAString& aOutput)
{
  if (aCharset.IsEmpty()) {
    // Treat the string as UTF8
    CopyUTF8toUTF16(aInput, aOutput);
    return NS_OK;
  }

  nsresult rv;
  nsCOMPtr<nsICharsetConverterManager> ccm =
    do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &rv);
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsIUnicodeDecoder> decoder;
  rv = ccm->GetUnicodeDecoder(PromiseFlatCString(aCharset).get(),
                              getter_AddRefs(decoder));
  if (NS_FAILED(rv))
    return rv;

  nsPromiseFlatCString flatInput(aInput);
  PRInt32 srcLen = flatInput.Length();
  PRInt32 dstLen;
  rv = decoder->GetMaxLength(flatInput.get(), srcLen, &dstLen);
  if (NS_FAILED(rv))
    return rv;

  PRUnichar *ustr = (PRUnichar *)nsMemory::Alloc((dstLen + 1) *
                                                 sizeof(PRUnichar));
  if (!ustr)
    return NS_ERROR_OUT_OF_MEMORY;

  rv = decoder->Convert(flatInput.get(), &srcLen, ustr, &dstLen);
  if (NS_SUCCEEDED(rv)) {
    ustr[dstLen] = 0;
    aOutput.Assign(ustr, dstLen);
  }

  nsMemory::Free(ustr);
  return rv;
}

/* static */
bool
nsContentUtils::CheckForBOM(const unsigned char* aBuffer, PRUint32 aLength,
                            nsACString& aCharset, bool *bigEndian)
{
  bool found = true;
  aCharset.Truncate();
  if (aLength >= 3 &&
      aBuffer[0] == 0xEF &&
      aBuffer[1] == 0xBB &&
      aBuffer[2] == 0xBF) {
    aCharset = "UTF-8";
  }
  else if (aLength >= 2 &&
           aBuffer[0] == 0xFE && aBuffer[1] == 0xFF) {
    aCharset = "UTF-16";
    if (bigEndian)
      *bigEndian = true;
  }
  else if (aLength >= 2 &&
           aBuffer[0] == 0xFF && aBuffer[1] == 0xFE) {
    aCharset = "UTF-16";
    if (bigEndian)
      *bigEndian = false;
  } else {
    found = false;
  }

  return found;
}

NS_IMPL_ISUPPORTS1(CharsetDetectionObserver,
                   nsICharsetDetectionObserver)

/* static */
nsresult
nsContentUtils::GuessCharset(const char *aData, PRUint32 aDataLen,
                             nsACString &aCharset)
{
  // First try the universal charset detector
  nsCOMPtr<nsICharsetDetector> detector =
    do_CreateInstance(NS_CHARSET_DETECTOR_CONTRACTID_BASE
                      "universal_charset_detector");
  if (!detector) {
    // No universal charset detector, try the default charset detector
    const nsAdoptingCString& detectorName =
      Preferences::GetLocalizedCString("intl.charset.detector");
    if (!detectorName.IsEmpty()) {
      nsCAutoString detectorContractID;
      detectorContractID.AssignLiteral(NS_CHARSET_DETECTOR_CONTRACTID_BASE);
      detectorContractID += detectorName;
      detector = do_CreateInstance(detectorContractID.get());
    }
  }

  nsresult rv;

  // The charset detector doesn't work for empty (null) aData. Testing
  // aDataLen instead of aData so that we catch potential errors.
  if (detector && aDataLen) {
    nsRefPtr<CharsetDetectionObserver> observer =
      new CharsetDetectionObserver();

    rv = detector->Init(observer);
    NS_ENSURE_SUCCESS(rv, rv);

    bool dummy;
    rv = detector->DoIt(aData, aDataLen, &dummy);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = detector->Done();
    NS_ENSURE_SUCCESS(rv, rv);

    aCharset = observer->GetResult();
  } else {
    // no charset detector available, check the BOM
    unsigned char sniffBuf[3];
    PRUint32 numRead =
      (aDataLen >= sizeof(sniffBuf) ? sizeof(sniffBuf) : aDataLen);
    memcpy(sniffBuf, aData, numRead);

    bool bigEndian;
    if (CheckForBOM(sniffBuf, numRead, aCharset, &bigEndian) &&
        aCharset.EqualsLiteral("UTF-16")) {
      if (bigEndian) {
        aCharset.AppendLiteral("BE");
      }
      else {
        aCharset.AppendLiteral("LE");
      }
    }
  }

  if (aCharset.IsEmpty()) {
    // no charset detected, default to the system charset
    nsCOMPtr<nsIPlatformCharset> platformCharset =
      do_GetService(NS_PLATFORMCHARSET_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv)) {
      rv = platformCharset->GetCharset(kPlatformCharsetSel_PlainTextInFile,
                                       aCharset);
      if (NS_FAILED(rv)) {
        NS_WARNING("Failed to get the system charset!");
      }
    }
  }

  if (aCharset.IsEmpty()) {
    // no sniffed or default charset, assume UTF-8
    aCharset.AssignLiteral("UTF-8");
  }

  return NS_OK;
}

/* static */
void
nsContentUtils::RegisterShutdownObserver(nsIObserver* aObserver)
{
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  if (observerService) {
    observerService->AddObserver(aObserver, 
                                 NS_XPCOM_SHUTDOWN_OBSERVER_ID, 
                                 false);
  }
}

/* static */
void
nsContentUtils::UnregisterShutdownObserver(nsIObserver* aObserver)
{
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  if (observerService) {
    observerService->RemoveObserver(aObserver, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
  }
}

/* static */
bool
nsContentUtils::HasNonEmptyAttr(const nsIContent* aContent, PRInt32 aNameSpaceID,
                                nsIAtom* aName)
{
  static nsIContent::AttrValuesArray strings[] = {&nsGkAtoms::_empty, nsnull};
  return aContent->FindAttrValueIn(aNameSpaceID, aName, strings, eCaseMatters)
    == nsIContent::ATTR_VALUE_NO_MATCH;
}

/* static */
bool
nsContentUtils::HasMutationListeners(nsINode* aNode,
                                     PRUint32 aType,
                                     nsINode* aTargetForSubtreeModified)
{
  nsIDocument* doc = aNode->OwnerDoc();

  // global object will be null for documents that don't have windows.
  nsPIDOMWindow* window = doc->GetInnerWindow();
  // This relies on nsEventListenerManager::AddEventListener, which sets
  // all mutation bits when there is a listener for DOMSubtreeModified event.
  if (window && !window->HasMutationListeners(aType)) {
    return false;
  }

  if (aNode->IsNodeOfType(nsINode::eCONTENT) &&
      static_cast<nsIContent*>(aNode)->IsInNativeAnonymousSubtree()) {
    return false;
  }

  doc->MayDispatchMutationEvent(aTargetForSubtreeModified);

  // If we have a window, we can check it for mutation listeners now.
  if (aNode->IsInDoc()) {
    nsCOMPtr<nsIDOMEventTarget> piTarget(do_QueryInterface(window));
    if (piTarget) {
      nsEventListenerManager* manager = piTarget->GetListenerManager(false);
      if (manager && manager->HasMutationListeners()) {
        return true;
      }
    }
  }

  // If we have a window, we know a mutation listener is registered, but it
  // might not be in our chain.  If we don't have a window, we might have a
  // mutation listener.  Check quickly to see.
  while (aNode) {
    nsEventListenerManager* manager = aNode->GetListenerManager(false);
    if (manager && manager->HasMutationListeners()) {
      return true;
    }

    if (aNode->IsNodeOfType(nsINode::eCONTENT)) {
      nsIContent* content = static_cast<nsIContent*>(aNode);
      nsIContent* insertionParent =
        doc->BindingManager()->GetInsertionParent(content);
      if (insertionParent) {
        aNode = insertionParent;
        continue;
      }
    }
    aNode = aNode->GetNodeParent();
  }

  return false;
}

/* static */
bool
nsContentUtils::HasMutationListeners(nsIDocument* aDocument,
                                     PRUint32 aType)
{
  nsPIDOMWindow* window = aDocument ?
    aDocument->GetInnerWindow() : nsnull;

  // This relies on nsEventListenerManager::AddEventListener, which sets
  // all mutation bits when there is a listener for DOMSubtreeModified event.
  return !window || window->HasMutationListeners(aType);
}

void
nsContentUtils::MaybeFireNodeRemoved(nsINode* aChild, nsINode* aParent,
                                     nsIDocument* aOwnerDoc)
{
  NS_PRECONDITION(aChild, "Missing child");
  NS_PRECONDITION(aChild->GetNodeParent() == aParent, "Wrong parent");
  NS_PRECONDITION(aChild->OwnerDoc() == aOwnerDoc, "Wrong owner-doc");

  // This checks that IsSafeToRunScript is true since we don't want to fire
  // events when that is false. We can't rely on nsEventDispatcher to assert
  // this in this situation since most of the time there are no mutation
  // event listeners, in which case we won't even attempt to dispatch events.
  // However this also allows for two exceptions. First off, we don't assert
  // if the mutation happens to native anonymous content since we never fire
  // mutation events on such content anyway.
  // Second, we don't assert if sDOMNodeRemovedSuppressCount is true since
  // that is a know case when we'd normally fire a mutation event, but can't
  // make that safe and so we suppress it at this time. Ideally this should
  // go away eventually.
  NS_ASSERTION((aChild->IsNodeOfType(nsINode::eCONTENT) &&
               static_cast<nsIContent*>(aChild)->
                 IsInNativeAnonymousSubtree()) ||
               IsSafeToRunScript() ||
               sDOMNodeRemovedSuppressCount,
               "Want to fire DOMNodeRemoved event, but it's not safe");

  // Having an explicit check here since it's an easy mistake to fall into,
  // and there might be existing code with problems. We'd rather be safe
  // than fire DOMNodeRemoved in all corner cases. We also rely on it for
  // nsAutoScriptBlockerSuppressNodeRemoved.
  if (!IsSafeToRunScript()) {
    return;
  }

  if (HasMutationListeners(aChild,
        NS_EVENT_BITS_MUTATION_NODEREMOVED, aParent)) {
    nsMutationEvent mutation(true, NS_MUTATION_NODEREMOVED);
    mutation.mRelatedNode = do_QueryInterface(aParent);

    mozAutoSubtreeModified subtree(aOwnerDoc, aParent);
    nsEventDispatcher::Dispatch(aChild, nsnull, &mutation);
  }
}

PLDHashOperator
ListenerEnumerator(PLDHashTable* aTable, PLDHashEntryHdr* aEntry,
                   PRUint32 aNumber, void* aArg)
{
  EventListenerManagerMapEntry* entry =
    static_cast<EventListenerManagerMapEntry*>(aEntry);
  if (entry) {
    nsINode* n = static_cast<nsINode*>(entry->mListenerManager->GetTarget());
    if (n && n->IsInDoc() &&
        nsCCUncollectableMarker::InGeneration(n->OwnerDoc()->GetMarkedCCGeneration())) {
      entry->mListenerManager->UnmarkGrayJSListeners();
    }
  }
  return PL_DHASH_NEXT;
}

void
nsContentUtils::UnmarkGrayJSListenersInCCGenerationDocuments(PRUint32 aGeneration)
{
  if (sEventListenerManagersHash.ops) {
    PL_DHashTableEnumerate(&sEventListenerManagersHash, ListenerEnumerator,
                           &aGeneration);
  }
}

/* static */
void
nsContentUtils::TraverseListenerManager(nsINode *aNode,
                                        nsCycleCollectionTraversalCallback &cb)
{
  if (!sEventListenerManagersHash.ops) {
    // We're already shut down, just return.
    return;
  }

  EventListenerManagerMapEntry *entry =
    static_cast<EventListenerManagerMapEntry *>
               (PL_DHashTableOperate(&sEventListenerManagersHash, aNode,
                                        PL_DHASH_LOOKUP));
  if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_PTR(entry->mListenerManager,
                                                 nsEventListenerManager,
                                  "[via hash] mListenerManager")
  }
}

nsEventListenerManager*
nsContentUtils::GetListenerManager(nsINode *aNode,
                                   bool aCreateIfNotFound)
{
  if (!aCreateIfNotFound && !aNode->HasFlag(NODE_HAS_LISTENERMANAGER)) {
    return nsnull;
  }
  
  if (!sEventListenerManagersHash.ops) {
    // We're already shut down, don't bother creating an event listener
    // manager.

    return nsnull;
  }

  if (!aCreateIfNotFound) {
    EventListenerManagerMapEntry *entry =
      static_cast<EventListenerManagerMapEntry *>
                 (PL_DHashTableOperate(&sEventListenerManagersHash, aNode,
                                          PL_DHASH_LOOKUP));
    if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
      return entry->mListenerManager;
    }
    return nsnull;
  }

  EventListenerManagerMapEntry *entry =
    static_cast<EventListenerManagerMapEntry *>
               (PL_DHashTableOperate(&sEventListenerManagersHash, aNode,
                                        PL_DHASH_ADD));

  if (!entry) {
    return nsnull;
  }

  if (!entry->mListenerManager) {
    entry->mListenerManager = new nsEventListenerManager(aNode);

    aNode->SetFlags(NODE_HAS_LISTENERMANAGER);
  }

  return entry->mListenerManager;
}

/* static */
void
nsContentUtils::RemoveListenerManager(nsINode *aNode)
{
  if (sEventListenerManagersHash.ops) {
    EventListenerManagerMapEntry *entry =
      static_cast<EventListenerManagerMapEntry *>
                 (PL_DHashTableOperate(&sEventListenerManagersHash, aNode,
                                          PL_DHASH_LOOKUP));
    if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
      nsRefPtr<nsEventListenerManager> listenerManager;
      listenerManager.swap(entry->mListenerManager);
      // Remove the entry and *then* do operations that could cause further
      // modification of sEventListenerManagersHash.  See bug 334177.
      PL_DHashTableRawRemove(&sEventListenerManagersHash, entry);
      if (listenerManager) {
        listenerManager->Disconnect();
      }
    }
  }
}

/* static */
bool
nsContentUtils::IsValidNodeName(nsIAtom *aLocalName, nsIAtom *aPrefix,
                                PRInt32 aNamespaceID)
{
  if (aNamespaceID == kNameSpaceID_Unknown) {
    return false;
  }

  if (!aPrefix) {
    // If the prefix is null, then either the QName must be xmlns or the
    // namespace must not be XMLNS.
    return (aLocalName == nsGkAtoms::xmlns) ==
           (aNamespaceID == kNameSpaceID_XMLNS);
  }

  // If the prefix is non-null then the namespace must not be null.
  if (aNamespaceID == kNameSpaceID_None) {
    return false;
  }

  // If the namespace is the XMLNS namespace then the prefix must be xmlns,
  // but the localname must not be xmlns.
  if (aNamespaceID == kNameSpaceID_XMLNS) {
    return aPrefix == nsGkAtoms::xmlns && aLocalName != nsGkAtoms::xmlns;
  }

  // If the namespace is not the XMLNS namespace then the prefix must not be
  // xmlns.
  // If the namespace is the XML namespace then the prefix can be anything.
  // If the namespace is not the XML namespace then the prefix must not be xml.
  return aPrefix != nsGkAtoms::xmlns &&
         (aNamespaceID == kNameSpaceID_XML || aPrefix != nsGkAtoms::xml);
}

/* static */
nsresult
nsContentUtils::CreateContextualFragment(nsINode* aContextNode,
                                         const nsAString& aFragment,
                                         bool aPreventScriptExecution,
                                         nsIDOMDocumentFragment** aReturn)
{
  *aReturn = nsnull;
  NS_ENSURE_ARG(aContextNode);

  // If we don't have a document here, we can't get the right security context
  // for compiling event handlers... so just bail out.
  nsCOMPtr<nsIDocument> document = aContextNode->OwnerDoc();
  bool isHTML = document->IsHTML();
#ifdef DEBUG
  nsCOMPtr<nsIHTMLDocument> htmlDoc = do_QueryInterface(document);
  NS_ASSERTION(!isHTML || htmlDoc, "Should have HTMLDocument here!");
#endif

  if (isHTML) {
    nsCOMPtr<nsIDOMDocumentFragment> frag;
    NS_NewDocumentFragment(getter_AddRefs(frag), document->NodeInfoManager());
    
    nsCOMPtr<nsIContent> contextAsContent = do_QueryInterface(aContextNode);
    if (contextAsContent && !contextAsContent->IsElement()) {
      contextAsContent = contextAsContent->GetParent();
      if (contextAsContent && !contextAsContent->IsElement()) {
        // can this even happen?
        contextAsContent = nsnull;
      }
    }
    
    nsresult rv;
    nsCOMPtr<nsIContent> fragment = do_QueryInterface(frag);
    if (contextAsContent && !contextAsContent->IsHTML(nsGkAtoms::html)) {
      rv = ParseFragmentHTML(aFragment,
                             fragment,
                             contextAsContent->Tag(),
                             contextAsContent->GetNameSpaceID(),
                             (document->GetCompatibilityMode() ==
                               eCompatibility_NavQuirks),
                             aPreventScriptExecution);
    } else {
      rv = ParseFragmentHTML(aFragment,
                             fragment,
                             nsGkAtoms::body,
                             kNameSpaceID_XHTML,
                             (document->GetCompatibilityMode() ==
                               eCompatibility_NavQuirks),
                             aPreventScriptExecution);
    }

    frag.forget(aReturn);
    return rv;
  }

  nsAutoTArray<nsString, 32> tagStack;
  nsAutoString uriStr, nameStr;
  nsCOMPtr<nsIContent> content = do_QueryInterface(aContextNode);
  // just in case we have a text node
  if (content && !content->IsElement())
    content = content->GetParent();

  while (content && content->IsElement()) {
    nsString& tagName = *tagStack.AppendElement();
    NS_ENSURE_TRUE(&tagName, NS_ERROR_OUT_OF_MEMORY);

    tagName = content->NodeInfo()->QualifiedName();

    // see if we need to add xmlns declarations
    PRUint32 count = content->GetAttrCount();
    bool setDefaultNamespace = false;
    if (count > 0) {
      PRUint32 index;

      for (index = 0; index < count; index++) {
        const nsAttrName* name = content->GetAttrNameAt(index);
        if (name->NamespaceEquals(kNameSpaceID_XMLNS)) {
          content->GetAttr(kNameSpaceID_XMLNS, name->LocalName(), uriStr);

          // really want something like nsXMLContentSerializer::SerializeAttr
          tagName.Append(NS_LITERAL_STRING(" xmlns")); // space important
          if (name->GetPrefix()) {
            tagName.Append(PRUnichar(':'));
            name->LocalName()->ToString(nameStr);
            tagName.Append(nameStr);
          } else {
            setDefaultNamespace = true;
          }
          tagName.Append(NS_LITERAL_STRING("=\"") + uriStr +
            NS_LITERAL_STRING("\""));
        }
      }
    }

    if (!setDefaultNamespace) {
      nsINodeInfo* info = content->NodeInfo();
      if (!info->GetPrefixAtom() &&
          info->NamespaceID() != kNameSpaceID_None) {
        // We have no namespace prefix, but have a namespace ID.  Push
        // default namespace attr in, so that our kids will be in our
        // namespace.
        info->GetNamespaceURI(uriStr);
        tagName.Append(NS_LITERAL_STRING(" xmlns=\"") + uriStr +
                       NS_LITERAL_STRING("\""));
      }
    }

    content = content->GetParent();
  }

  return ParseFragmentXML(aFragment,
                          document,
                          tagStack,
                          aPreventScriptExecution,
                          aReturn);
}

/* static */
void
nsContentUtils::DropFragmentParsers()
{
  NS_IF_RELEASE(sHTMLFragmentParser);
  NS_IF_RELEASE(sXMLFragmentParser);
  NS_IF_RELEASE(sXMLFragmentSink);
}

/* static */
void
nsContentUtils::XPCOMShutdown()
{
  nsContentUtils::DropFragmentParsers();
}

/* static */
nsresult
nsContentUtils::ParseFragmentHTML(const nsAString& aSourceBuffer,
                                  nsIContent* aTargetNode,
                                  nsIAtom* aContextLocalName,
                                  PRInt32 aContextNamespace,
                                  bool aQuirks,
                                  bool aPreventScriptExecution)
{
  if (nsContentUtils::sFragmentParsingActive) {
    NS_NOTREACHED("Re-entrant fragment parsing attempted.");
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }
  mozilla::AutoRestore<bool> guard(nsContentUtils::sFragmentParsingActive);
  nsContentUtils::sFragmentParsingActive = true;
  if (!sHTMLFragmentParser) {
    NS_ADDREF(sHTMLFragmentParser = new nsHtml5StringParser());
    // Now sHTMLFragmentParser owns the object
  }
  nsresult rv =
    sHTMLFragmentParser->ParseFragment(aSourceBuffer,
                                       aTargetNode,
                                       aContextLocalName,
                                       aContextNamespace,
                                       aQuirks,
                                       aPreventScriptExecution);
  return rv;
}

/* static */
nsresult
nsContentUtils::ParseDocumentHTML(const nsAString& aSourceBuffer,
                                  nsIDocument* aTargetDocument,
                                  bool aScriptingEnabledForNoscriptParsing)
{
  if (nsContentUtils::sFragmentParsingActive) {
    NS_NOTREACHED("Re-entrant fragment parsing attempted.");
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }
  mozilla::AutoRestore<bool> guard(nsContentUtils::sFragmentParsingActive);
  nsContentUtils::sFragmentParsingActive = true;
  if (!sHTMLFragmentParser) {
    NS_ADDREF(sHTMLFragmentParser = new nsHtml5StringParser());
    // Now sHTMLFragmentParser owns the object
  }
  nsresult rv =
    sHTMLFragmentParser->ParseDocument(aSourceBuffer,
                                       aTargetDocument,
                                       aScriptingEnabledForNoscriptParsing);
  return rv;
}

/* static */
nsresult
nsContentUtils::ParseFragmentXML(const nsAString& aSourceBuffer,
                                 nsIDocument* aDocument,
                                 nsTArray<nsString>& aTagStack,
                                 bool aPreventScriptExecution,
                                 nsIDOMDocumentFragment** aReturn)
{
  if (nsContentUtils::sFragmentParsingActive) {
    NS_NOTREACHED("Re-entrant fragment parsing attempted.");
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }
  mozilla::AutoRestore<bool> guard(nsContentUtils::sFragmentParsingActive);
  nsContentUtils::sFragmentParsingActive = true;
  if (!sXMLFragmentParser) {
    nsCOMPtr<nsIParser> parser = do_CreateInstance(kCParserCID);
    parser.forget(&sXMLFragmentParser);
    // sXMLFragmentParser now owns the parser
  }
  if (!sXMLFragmentSink) {
    NS_NewXMLFragmentContentSink(&sXMLFragmentSink);
    // sXMLFragmentSink now owns the sink
  }
  nsCOMPtr<nsIContentSink> contentsink = do_QueryInterface(sXMLFragmentSink);
  NS_ABORT_IF_FALSE(contentsink, "Sink doesn't QI to nsIContentSink!");
  sXMLFragmentParser->SetContentSink(contentsink);

  sXMLFragmentSink->SetTargetDocument(aDocument);
  sXMLFragmentSink->SetPreventScriptExecution(aPreventScriptExecution);

  nsresult rv =
    sXMLFragmentParser->ParseFragment(aSourceBuffer,
                                      aTagStack);
  if (NS_FAILED(rv)) {
    // Drop the fragment parser and sink that might be in an inconsistent state
    NS_IF_RELEASE(sXMLFragmentParser);
    NS_IF_RELEASE(sXMLFragmentSink);
    return rv;
  }

  rv = sXMLFragmentSink->FinishFragmentParsing(aReturn);

  sXMLFragmentParser->Reset();

  return rv;
}

/* static */
nsresult
nsContentUtils::ConvertToPlainText(const nsAString& aSourceBuffer,
                                   nsAString& aResultBuffer,
                                   PRUint32 aFlags,
                                   PRUint32 aWrapCol)
{
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "about:blank");
  nsCOMPtr<nsIPrincipal> principal =
    do_CreateInstance(NS_NULLPRINCIPAL_CONTRACTID);
  nsCOMPtr<nsIDOMDocument> domDocument;
  nsresult rv = nsContentUtils::CreateDocument(EmptyString(),
                                               EmptyString(),
                                               nsnull,
                                               uri,
                                               uri,
                                               principal,
                                               nsnull,
                                               DocumentFlavorHTML,
                                               getter_AddRefs(domDocument));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDocument> document = do_QueryInterface(domDocument);
  rv = nsContentUtils::ParseDocumentHTML(aSourceBuffer, document,
    !(aFlags & nsIDocumentEncoder::OutputNoScriptContent));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDocumentEncoder> encoder = do_CreateInstance(
    "@mozilla.org/layout/documentEncoder;1?type=text/plain");

  rv = encoder->Init(domDocument, NS_LITERAL_STRING("text/plain"), aFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  encoder->SetWrapColumn(aWrapCol);

  return encoder->EncodeToString(aResultBuffer);
}

/* static */
nsresult
nsContentUtils::CreateDocument(const nsAString& aNamespaceURI, 
                               const nsAString& aQualifiedName, 
                               nsIDOMDocumentType* aDoctype,
                               nsIURI* aDocumentURI, nsIURI* aBaseURI,
                               nsIPrincipal* aPrincipal,
                               nsIScriptGlobalObject* aEventObject,
                               DocumentFlavor aFlavor,
                               nsIDOMDocument** aResult)
{
  return NS_NewDOMDocument(aResult, aNamespaceURI, aQualifiedName,
                           aDoctype, aDocumentURI, aBaseURI, aPrincipal,
                           true, aEventObject, aFlavor);
}

/* static */
nsresult
nsContentUtils::SetNodeTextContent(nsIContent* aContent,
                                   const nsAString& aValue,
                                   bool aTryReuse)
{
  // Fire DOMNodeRemoved mutation events before we do anything else.
  nsCOMPtr<nsIContent> owningContent;

  // Batch possible DOMSubtreeModified events.
  mozAutoSubtreeModified subtree(nsnull, nsnull);

  // Scope firing mutation events so that we don't carry any state that
  // might be stale
  {
    // We're relying on mozAutoSubtreeModified to keep a strong reference if
    // needed.
    nsIDocument* doc = aContent->OwnerDoc();

    // Optimize the common case of there being no observers
    if (HasMutationListeners(doc, NS_EVENT_BITS_MUTATION_NODEREMOVED)) {
      subtree.UpdateTarget(doc, nsnull);
      owningContent = aContent;
      nsCOMPtr<nsINode> child;
      bool skipFirst = aTryReuse;
      for (child = aContent->GetFirstChild();
           child && child->GetNodeParent() == aContent;
           child = child->GetNextSibling()) {
        if (skipFirst && child->IsNodeOfType(nsINode::eTEXT)) {
          skipFirst = false;
          continue;
        }
        nsContentUtils::MaybeFireNodeRemoved(child, aContent, doc);
      }
    }
  }

  // Might as well stick a batch around this since we're performing several
  // mutations.
  mozAutoDocUpdate updateBatch(aContent->GetCurrentDoc(),
    UPDATE_CONTENT_MODEL, true);
  nsAutoMutationBatch mb;

  PRUint32 childCount = aContent->GetChildCount();

  if (aTryReuse && !aValue.IsEmpty()) {
    PRUint32 removeIndex = 0;

    for (PRUint32 i = 0; i < childCount; ++i) {
      nsIContent* child = aContent->GetChildAt(removeIndex);
      if (removeIndex == 0 && child && child->IsNodeOfType(nsINode::eTEXT)) {
        nsresult rv = child->SetText(aValue, true);
        NS_ENSURE_SUCCESS(rv, rv);

        removeIndex = 1;
      }
      else {
        aContent->RemoveChildAt(removeIndex, true);
      }
    }

    if (removeIndex == 1) {
      return NS_OK;
    }
  }
  else {
    mb.Init(aContent, true, false);
    for (PRUint32 i = 0; i < childCount; ++i) {
      aContent->RemoveChildAt(0, true);
    }
  }
  mb.RemovalDone();

  if (aValue.IsEmpty()) {
    return NS_OK;
  }

  nsCOMPtr<nsIContent> textContent;
  nsresult rv = NS_NewTextNode(getter_AddRefs(textContent),
                               aContent->NodeInfo()->NodeInfoManager());
  NS_ENSURE_SUCCESS(rv, rv);

  textContent->SetText(aValue, true);

  rv = aContent->AppendChildTo(textContent, true);
  mb.NodesAdded();
  return rv;
}

static void AppendNodeTextContentsRecurse(nsINode* aNode, nsAString& aResult)
{
  for (nsIContent* child = aNode->GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    if (child->IsElement()) {
      AppendNodeTextContentsRecurse(child, aResult);
    }
    else if (child->IsNodeOfType(nsINode::eTEXT)) {
      child->AppendTextTo(aResult);
    }
  }
}

/* static */
void
nsContentUtils::AppendNodeTextContent(nsINode* aNode, bool aDeep,
                                      nsAString& aResult)
{
  if (aNode->IsNodeOfType(nsINode::eTEXT)) {
    static_cast<nsIContent*>(aNode)->AppendTextTo(aResult);
  }
  else if (aDeep) {
    AppendNodeTextContentsRecurse(aNode, aResult);
  }
  else {
    for (nsIContent* child = aNode->GetFirstChild();
         child;
         child = child->GetNextSibling()) {
      if (child->IsNodeOfType(nsINode::eTEXT)) {
        child->AppendTextTo(aResult);
      }
    }
  }
}

bool
nsContentUtils::HasNonEmptyTextContent(nsINode* aNode)
{
  for (nsIContent* child = aNode->GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    if (child->IsNodeOfType(nsINode::eTEXT) &&
        child->TextLength() > 0) {
      return true;
    }
  }

  return false;
}

/* static */
bool
nsContentUtils::IsInSameAnonymousTree(const nsINode* aNode,
                                      const nsIContent* aContent)
{
  NS_PRECONDITION(aNode,
                  "Must have a node to work with");
  NS_PRECONDITION(aContent,
                  "Must have a content to work with");
  
  if (!aNode->IsNodeOfType(nsINode::eCONTENT)) {
    /**
     * The root isn't an nsIContent, so it's a document or attribute.  The only
     * nodes in the same anonymous subtree as it will have a null
     * bindingParent.
     *
     * XXXbz strictly speaking, that's not true for attribute nodes.
     */
    return aContent->GetBindingParent() == nsnull;
  }

  return static_cast<const nsIContent*>(aNode)->GetBindingParent() ==
         aContent->GetBindingParent();
 
}

class AnonymousContentDestroyer : public nsRunnable {
public:
  AnonymousContentDestroyer(nsCOMPtr<nsIContent>* aContent) {
    mContent.swap(*aContent);
    mParent = mContent->GetParent();
    mDoc = mContent->OwnerDoc();
  }
  NS_IMETHOD Run() {
    mContent->UnbindFromTree();
    return NS_OK;
  }
private:
  nsCOMPtr<nsIContent> mContent;
  // Hold strong refs to the parent content and document so that they
  // don't die unexpectedly
  nsCOMPtr<nsIDocument> mDoc;
  nsCOMPtr<nsIContent> mParent;
};

/* static */
void
nsContentUtils::DestroyAnonymousContent(nsCOMPtr<nsIContent>* aContent)
{
  if (*aContent) {
    AddScriptRunner(new AnonymousContentDestroyer(aContent));
  }
}

/* static */
nsresult
nsContentUtils::HoldJSObjects(void* aScriptObjectHolder,
                              nsScriptObjectTracer* aTracer)
{
  NS_ENSURE_TRUE(sXPConnect, NS_ERROR_UNEXPECTED);

  nsresult rv = sXPConnect->AddJSHolder(aScriptObjectHolder, aTracer);
  NS_ENSURE_SUCCESS(rv, rv);

  if (sJSGCThingRootCount++ == 0) {
    nsLayoutStatics::AddRef();
  }
  NS_LOG_ADDREF(sXPConnect, sJSGCThingRootCount, "HoldJSObjects",
                sizeof(void*));

  return NS_OK;
}

/* static */
nsresult
nsContentUtils::DropJSObjects(void* aScriptObjectHolder)
{
  NS_LOG_RELEASE(sXPConnect, sJSGCThingRootCount - 1, "HoldJSObjects");
  nsresult rv = sXPConnect->RemoveJSHolder(aScriptObjectHolder);
  if (--sJSGCThingRootCount == 0) {
    nsLayoutStatics::Release();
  }
  return rv;
}

#ifdef DEBUG
/* static */
bool
nsContentUtils::AreJSObjectsHeld(void* aScriptHolder)
{
  bool isHeld = false;
  sXPConnect->TestJSHolder(aScriptHolder, &isHeld);
  return isHeld;
}
#endif

/* static */
void
nsContentUtils::NotifyInstalledMenuKeyboardListener(bool aInstalling)
{
  nsIMEStateManager::OnInstalledMenuKeyboardListener(aInstalling);
}

static bool SchemeIs(nsIURI* aURI, const char* aScheme)
{
  nsCOMPtr<nsIURI> baseURI = NS_GetInnermostURI(aURI);
  NS_ENSURE_TRUE(baseURI, false);

  bool isScheme = false;
  return NS_SUCCEEDED(baseURI->SchemeIs(aScheme, &isScheme)) && isScheme;
}

/* static */
nsresult
nsContentUtils::CheckSecurityBeforeLoad(nsIURI* aURIToLoad,
                                        nsIPrincipal* aLoadingPrincipal,
                                        PRUint32 aCheckLoadFlags,
                                        bool aAllowData,
                                        PRUint32 aContentPolicyType,
                                        nsISupports* aContext,
                                        const nsACString& aMimeGuess,
                                        nsISupports* aExtra)
{
  NS_PRECONDITION(aLoadingPrincipal, "Must have a loading principal here");

  bool isSystemPrin = false;
  if (NS_SUCCEEDED(sSecurityManager->IsSystemPrincipal(aLoadingPrincipal,
                                                       &isSystemPrin)) &&
      isSystemPrin) {
    return NS_OK;
  }
  
  // XXXbz do we want to fast-path skin stylesheets loading XBL here somehow?
  // CheckLoadURIWithPrincipal
  nsresult rv = sSecurityManager->
    CheckLoadURIWithPrincipal(aLoadingPrincipal, aURIToLoad, aCheckLoadFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  // Content Policy
  PRInt16 shouldLoad = nsIContentPolicy::ACCEPT;
  rv = NS_CheckContentLoadPolicy(aContentPolicyType,
                                 aURIToLoad,
                                 aLoadingPrincipal,
                                 aContext,
                                 aMimeGuess,
                                 aExtra,
                                 &shouldLoad,
                                 GetContentPolicy(),
                                 sSecurityManager);
  NS_ENSURE_SUCCESS(rv, rv);
  if (NS_CP_REJECTED(shouldLoad)) {
    return NS_ERROR_CONTENT_BLOCKED;
  }

  // Same Origin
  if ((aAllowData && SchemeIs(aURIToLoad, "data")) ||
      ((aCheckLoadFlags & nsIScriptSecurityManager::ALLOW_CHROME) &&
       SchemeIs(aURIToLoad, "chrome"))) {
    return NS_OK;
  }

  return aLoadingPrincipal->CheckMayLoad(aURIToLoad, true);
}

bool
nsContentUtils::IsSystemPrincipal(nsIPrincipal* aPrincipal)
{
  bool isSystem;
  nsresult rv = sSecurityManager->IsSystemPrincipal(aPrincipal, &isSystem);
  return NS_SUCCEEDED(rv) && isSystem;
}

bool
nsContentUtils::CombineResourcePrincipals(nsCOMPtr<nsIPrincipal>* aResourcePrincipal,
                                          nsIPrincipal* aExtraPrincipal)
{
  if (!aExtraPrincipal) {
    return false;
  }
  if (!*aResourcePrincipal) {
    *aResourcePrincipal = aExtraPrincipal;
    return true;
  }
  if (*aResourcePrincipal == aExtraPrincipal) {
    return false;
  }
  bool subsumes;
  if (NS_SUCCEEDED((*aResourcePrincipal)->Subsumes(aExtraPrincipal, &subsumes)) &&
      subsumes) {
    return false;
  }
  sSecurityManager->GetSystemPrincipal(getter_AddRefs(*aResourcePrincipal));
  return true;
}

/* static */
void
nsContentUtils::TriggerLink(nsIContent *aContent, nsPresContext *aPresContext,
                            nsIURI *aLinkURI, const nsString &aTargetSpec,
                            bool aClick, bool aIsUserTriggered,
                            bool aIsTrusted)
{
  NS_ASSERTION(aPresContext, "Need a nsPresContext");
  NS_PRECONDITION(aLinkURI, "No link URI");

  if (aContent->IsEditable()) {
    return;
  }

  nsILinkHandler *handler = aPresContext->GetLinkHandler();
  if (!handler) {
    return;
  }

  if (!aClick) {
    handler->OnOverLink(aContent, aLinkURI, aTargetSpec.get());

    return;
  }

  // Check that this page is allowed to load this URI.
  nsresult proceed = NS_OK;

  if (sSecurityManager) {
    PRUint32 flag =
      aIsUserTriggered ?
      (PRUint32)nsIScriptSecurityManager::STANDARD :
      (PRUint32)nsIScriptSecurityManager::LOAD_IS_AUTOMATIC_DOCUMENT_REPLACEMENT;
    proceed =
      sSecurityManager->CheckLoadURIWithPrincipal(aContent->NodePrincipal(),
                                                  aLinkURI, flag);
  }

  // Only pass off the click event if the script security manager says it's ok.
  if (NS_SUCCEEDED(proceed)) {
    handler->OnLinkClick(aContent, aLinkURI, aTargetSpec.get(), nsnull, nsnull,
                         aIsTrusted);
  }
}

/* static */
void
nsContentUtils::GetLinkLocation(Element* aElement, nsString& aLocationString)
{
  nsCOMPtr<nsIURI> hrefURI = aElement->GetHrefURI();
  if (hrefURI) {
    nsCAutoString specUTF8;
    nsresult rv = hrefURI->GetSpec(specUTF8);
    if (NS_SUCCEEDED(rv))
      CopyUTF8toUTF16(specUTF8, aLocationString);
  }
}

/* static */
nsIWidget*
nsContentUtils::GetTopLevelWidget(nsIWidget* aWidget)
{
  if (!aWidget)
    return nsnull;

  return aWidget->GetTopLevelWidget();
}

/* static */
const nsDependentString
nsContentUtils::GetLocalizedEllipsis()
{
  static PRUnichar sBuf[4] = { 0, 0, 0, 0 };
  if (!sBuf[0]) {
    nsAdoptingString tmp = Preferences::GetLocalizedString("intl.ellipsis");
    PRUint32 len = NS_MIN(PRUint32(tmp.Length()),
                          PRUint32(ArrayLength(sBuf) - 1));
    CopyUnicodeTo(tmp, 0, sBuf, len);
    if (!sBuf[0])
      sBuf[0] = PRUnichar(0x2026);
  }
  return nsDependentString(sBuf);
}

//static
nsEvent*
nsContentUtils::GetNativeEvent(nsIDOMEvent* aDOMEvent)
{
  return aDOMEvent ? aDOMEvent->GetInternalNSEvent() : nsnull;
}

//static
bool
nsContentUtils::DOMEventToNativeKeyEvent(nsIDOMKeyEvent* aKeyEvent,
                                         nsNativeKeyEvent* aNativeEvent,
                                         bool aGetCharCode)
{
  nsCOMPtr<nsIDOMNSEvent> nsevent = do_QueryInterface(aKeyEvent);
  bool defaultPrevented;
  nsevent->GetPreventDefault(&defaultPrevented);
  if (defaultPrevented)
    return false;

  bool trusted = false;
  nsevent->GetIsTrusted(&trusted);
  if (!trusted)
    return false;

  if (aGetCharCode) {
    aKeyEvent->GetCharCode(&aNativeEvent->charCode);
  } else {
    aNativeEvent->charCode = 0;
  }
  aKeyEvent->GetKeyCode(&aNativeEvent->keyCode);
  aKeyEvent->GetAltKey(&aNativeEvent->altKey);
  aKeyEvent->GetCtrlKey(&aNativeEvent->ctrlKey);
  aKeyEvent->GetShiftKey(&aNativeEvent->shiftKey);
  aKeyEvent->GetMetaKey(&aNativeEvent->metaKey);

  aNativeEvent->nativeEvent = GetNativeEvent(aKeyEvent);

  return true;
}

static bool
HasASCIIDigit(const nsTArray<nsShortcutCandidate>& aCandidates)
{
  for (PRUint32 i = 0; i < aCandidates.Length(); ++i) {
    PRUint32 ch = aCandidates[i].mCharCode;
    if (ch >= '0' && ch <= '9')
      return true;
  }
  return false;
}

static bool
CharsCaseInsensitiveEqual(PRUint32 aChar1, PRUint32 aChar2)
{
  return aChar1 == aChar2 ||
         (IS_IN_BMP(aChar1) && IS_IN_BMP(aChar2) &&
          ToLowerCase(PRUnichar(aChar1)) == ToLowerCase(PRUnichar(aChar2)));
}

static bool
IsCaseChangeableChar(PRUint32 aChar)
{
  return IS_IN_BMP(aChar) &&
         ToLowerCase(PRUnichar(aChar)) != ToUpperCase(PRUnichar(aChar));
}

/* static */
void
nsContentUtils::GetAccelKeyCandidates(nsIDOMKeyEvent* aDOMKeyEvent,
                  nsTArray<nsShortcutCandidate>& aCandidates)
{
  NS_PRECONDITION(aCandidates.IsEmpty(), "aCandidates must be empty");

  nsAutoString eventType;
  aDOMKeyEvent->GetType(eventType);
  // Don't process if aDOMKeyEvent is not a keypress event.
  if (!eventType.EqualsLiteral("keypress"))
    return;

  nsKeyEvent* nativeKeyEvent =
    static_cast<nsKeyEvent*>(GetNativeEvent(aDOMKeyEvent));
  if (nativeKeyEvent) {
    NS_ASSERTION(nativeKeyEvent->eventStructType == NS_KEY_EVENT,
                 "wrong type of native event");
    // nsShortcutCandidate::mCharCode is a candidate charCode.
    // nsShoftcutCandidate::mIgnoreShift means the mCharCode should be tried to
    // execute a command with/without shift key state. If this is TRUE, the
    // shifted key state should be ignored. Otherwise, don't ignore the state.
    // the priority of the charCodes are (shift key is not pressed):
    //   0: charCode/false,
    //   1: unshiftedCharCodes[0]/false, 2: unshiftedCharCodes[1]/false...
    // the priority of the charCodes are (shift key is pressed):
    //   0: charCode/false,
    //   1: shiftedCharCodes[0]/false, 2: shiftedCharCodes[0]/true,
    //   3: shiftedCharCodes[1]/false, 4: shiftedCharCodes[1]/true...
    if (nativeKeyEvent->charCode) {
      nsShortcutCandidate key(nativeKeyEvent->charCode, false);
      aCandidates.AppendElement(key);
    }

    PRUint32 len = nativeKeyEvent->alternativeCharCodes.Length();
    if (!nativeKeyEvent->IsShift()) {
      for (PRUint32 i = 0; i < len; ++i) {
        PRUint32 ch =
          nativeKeyEvent->alternativeCharCodes[i].mUnshiftedCharCode;
        if (!ch || ch == nativeKeyEvent->charCode)
          continue;

        nsShortcutCandidate key(ch, false);
        aCandidates.AppendElement(key);
      }
      // If unshiftedCharCodes doesn't have numeric but shiftedCharCode has it,
      // this keyboard layout is AZERTY or similar layout, probably.
      // In this case, Accel+[0-9] should be accessible without shift key.
      // However, the priority should be lowest.
      if (!HasASCIIDigit(aCandidates)) {
        for (PRUint32 i = 0; i < len; ++i) {
          PRUint32 ch =
            nativeKeyEvent->alternativeCharCodes[i].mShiftedCharCode;
          if (ch >= '0' && ch <= '9') {
            nsShortcutCandidate key(ch, false);
            aCandidates.AppendElement(key);
            break;
          }
        }
      }
    } else {
      for (PRUint32 i = 0; i < len; ++i) {
        PRUint32 ch = nativeKeyEvent->alternativeCharCodes[i].mShiftedCharCode;
        if (!ch)
          continue;

        if (ch != nativeKeyEvent->charCode) {
          nsShortcutCandidate key(ch, false);
          aCandidates.AppendElement(key);
        }

        // If the char is an alphabet, the shift key state should not be
        // ignored. E.g., Ctrl+Shift+C should not execute Ctrl+C.

        // And checking the charCode is same as unshiftedCharCode too.
        // E.g., for Ctrl+Shift+(Plus of Numpad) should not run Ctrl+Plus.
        PRUint32 unshiftCh =
          nativeKeyEvent->alternativeCharCodes[i].mUnshiftedCharCode;
        if (CharsCaseInsensitiveEqual(ch, unshiftCh))
          continue;

        // On the Hebrew keyboard layout on Windows, the unshifted char is a
        // localized character but the shifted char is a Latin alphabet,
        // then, we should not execute without the shift state. See bug 433192.
        if (IsCaseChangeableChar(ch))
          continue;

        // Setting the alternative charCode candidates for retry without shift
        // key state only when the shift key is pressed.
        nsShortcutCandidate key(ch, true);
        aCandidates.AppendElement(key);
      }
    }
  } else {
    PRUint32 charCode;
    aDOMKeyEvent->GetCharCode(&charCode);
    if (charCode) {
      nsShortcutCandidate key(charCode, false);
      aCandidates.AppendElement(key);
    }
  }
}

/* static */
void
nsContentUtils::GetAccessKeyCandidates(nsKeyEvent* aNativeKeyEvent,
                                       nsTArray<PRUint32>& aCandidates)
{
  NS_PRECONDITION(aCandidates.IsEmpty(), "aCandidates must be empty");

  // return the lower cased charCode candidates for access keys.
  // the priority of the charCodes are:
  //   0: charCode, 1: unshiftedCharCodes[0], 2: shiftedCharCodes[0]
  //   3: unshiftedCharCodes[1], 4: shiftedCharCodes[1],...
  if (aNativeKeyEvent->charCode) {
    PRUint32 ch = aNativeKeyEvent->charCode;
    if (IS_IN_BMP(ch))
      ch = ToLowerCase(PRUnichar(ch));
    aCandidates.AppendElement(ch);
  }
  for (PRUint32 i = 0;
       i < aNativeKeyEvent->alternativeCharCodes.Length(); ++i) {
    PRUint32 ch[2] =
      { aNativeKeyEvent->alternativeCharCodes[i].mUnshiftedCharCode,
        aNativeKeyEvent->alternativeCharCodes[i].mShiftedCharCode };
    for (PRUint32 j = 0; j < 2; ++j) {
      if (!ch[j])
        continue;
      if (IS_IN_BMP(ch[j]))
        ch[j] = ToLowerCase(PRUnichar(ch[j]));
      // Don't append the charCode that was already appended.
      if (aCandidates.IndexOf(ch[j]) == aCandidates.NoIndex)
        aCandidates.AppendElement(ch[j]);
    }
  }
  return;
}

/* static */
void
nsContentUtils::AddScriptBlocker()
{
  if (!sScriptBlockerCount) {
    NS_ASSERTION(sRunnersCountAtFirstBlocker == 0,
                 "Should not already have a count");
    sRunnersCountAtFirstBlocker = sBlockedScriptRunners->Length();
  }
  ++sScriptBlockerCount;
}

/* static */
void
nsContentUtils::RemoveScriptBlocker()
{
  NS_ASSERTION(sScriptBlockerCount != 0, "Negative script blockers");
  --sScriptBlockerCount;
  if (sScriptBlockerCount) {
    return;
  }

  PRUint32 firstBlocker = sRunnersCountAtFirstBlocker;
  PRUint32 lastBlocker = sBlockedScriptRunners->Length();
  PRUint32 originalFirstBlocker = firstBlocker;
  PRUint32 blockersCount = lastBlocker - firstBlocker;
  sRunnersCountAtFirstBlocker = 0;
  NS_ASSERTION(firstBlocker <= lastBlocker,
               "bad sRunnersCountAtFirstBlocker");

  while (firstBlocker < lastBlocker) {
    nsCOMPtr<nsIRunnable> runnable = (*sBlockedScriptRunners)[firstBlocker];
    ++firstBlocker;

    runnable->Run();
    NS_ASSERTION(sRunnersCountAtFirstBlocker == 0,
                 "Bad count");
    NS_ASSERTION(!sScriptBlockerCount, "This is really bad");
  }
  sBlockedScriptRunners->RemoveElementsAt(originalFirstBlocker, blockersCount);
}

/* static */
bool
nsContentUtils::AddScriptRunner(nsIRunnable* aRunnable)
{
  if (!aRunnable) {
    return false;
  }

  if (sScriptBlockerCount) {
    return sBlockedScriptRunners->AppendElement(aRunnable) != nsnull;
  }
  
  nsCOMPtr<nsIRunnable> run = aRunnable;
  run->Run();

  return true;
}

void
nsContentUtils::LeaveMicroTask()
{
  if (--sMicroTaskLevel == 0) {
    nsDOMMutationObserver::HandleMutations();
  }
}

/* 
 * Helper function for nsContentUtils::ProcessViewportInfo.
 *
 * Handles a single key=value pair. If it corresponds to a valid viewport
 * attribute, add it to the document header data. No validation is done on the
 * value itself (this is done at display time).
 */
static void ProcessViewportToken(nsIDocument *aDocument, 
                                 const nsAString &token) {

  /* Iterators. */
  nsAString::const_iterator tip, tail, end;
  token.BeginReading(tip);
  tail = tip;
  token.EndReading(end);

  /* Move tip to the '='. */
  while ((tip != end) && (*tip != '='))
    ++tip;

  /* If we didn't find an '=', punt. */
  if (tip == end)
    return;

  /* Extract the key and value. */
  const nsAString &key =
    nsContentUtils::TrimWhitespace<nsCRT::IsAsciiSpace>(Substring(tail, tip),
                                                        true);
  const nsAString &value =
    nsContentUtils::TrimWhitespace<nsCRT::IsAsciiSpace>(Substring(++tip, end),
                                                        true);

  /* Check for known keys. If we find a match, insert the appropriate
   * information into the document header. */
  nsCOMPtr<nsIAtom> key_atom = do_GetAtom(key);
  if (key_atom == nsGkAtoms::height)
    aDocument->SetHeaderData(nsGkAtoms::viewport_height, value);
  else if (key_atom == nsGkAtoms::width)
    aDocument->SetHeaderData(nsGkAtoms::viewport_width, value);
  else if (key_atom == nsGkAtoms::initial_scale)
    aDocument->SetHeaderData(nsGkAtoms::viewport_initial_scale, value);
  else if (key_atom == nsGkAtoms::minimum_scale)
    aDocument->SetHeaderData(nsGkAtoms::viewport_minimum_scale, value);
  else if (key_atom == nsGkAtoms::maximum_scale)
    aDocument->SetHeaderData(nsGkAtoms::viewport_maximum_scale, value);
  else if (key_atom == nsGkAtoms::user_scalable)
    aDocument->SetHeaderData(nsGkAtoms::viewport_user_scalable, value);
}

#define IS_SEPARATOR(c) ((c == '=') || (c == ',') || (c == ';') || \
                         (c == '\t') || (c == '\n') || (c == '\r'))

/* static */
ViewportInfo
nsContentUtils::GetViewportInfo(nsIDocument *aDocument)
{
  ViewportInfo ret;
  ret.defaultZoom = 1.0;
  ret.autoSize = true;
  ret.allowZoom = true;
  ret.autoScale = true;

  // If the docType specifies that we are on a site optimized for mobile,
  // then we want to return specially crafted defaults for the viewport info.
  nsCOMPtr<nsIDOMDocument>
    domDoc(do_QueryInterface(aDocument));

  nsCOMPtr<nsIDOMDocumentType> docType;
  nsresult rv = domDoc->GetDoctype(getter_AddRefs(docType));
  if (NS_SUCCEEDED(rv) && docType) {
    nsAutoString docId;
    rv = docType->GetPublicId(docId);
    if (NS_SUCCEEDED(rv)) {
      if ((docId.Find("WAP") != -1) ||
          (docId.Find("Mobile") != -1) ||
          (docId.Find("WML") != -1))
      {
        return ret;
      }
    }
  }

  if (aDocument->IsXUL()) {
    ret.autoScale = false;
    return ret;
  }

  nsAutoString handheldFriendly;
  aDocument->GetHeaderData(nsGkAtoms::handheldFriendly, handheldFriendly);

  if (handheldFriendly.EqualsLiteral("true")) {
    return ret;
  }

  PRInt32 errorCode;

  nsAutoString minScaleStr;
  aDocument->GetHeaderData(nsGkAtoms::minimum_scale, minScaleStr);

  float scaleMinFloat = minScaleStr.ToFloat(&errorCode);

  if (errorCode) {
    scaleMinFloat = kViewportMinScale;
  }

  scaleMinFloat = NS_MIN(scaleMinFloat, kViewportMaxScale);
  scaleMinFloat = NS_MAX(scaleMinFloat, kViewportMinScale);

  nsAutoString maxScaleStr;
  aDocument->GetHeaderData(nsGkAtoms::maximum_scale, maxScaleStr);

  // We define a special error code variable for the scale and max scale,
  // because they are used later (see the width calculations).
  PRInt32 scaleMaxErrorCode;
  float scaleMaxFloat = maxScaleStr.ToFloat(&scaleMaxErrorCode);

  if (scaleMaxErrorCode) {
    scaleMaxFloat = kViewportMaxScale;
  }

  scaleMaxFloat = NS_MIN(scaleMaxFloat, kViewportMaxScale);
  scaleMaxFloat = NS_MAX(scaleMaxFloat, kViewportMinScale);

  nsAutoString scaleStr;
  aDocument->GetHeaderData(nsGkAtoms::viewport_initial_scale, scaleStr);

  PRInt32 scaleErrorCode;
  float scaleFloat = scaleStr.ToFloat(&scaleErrorCode);
  scaleFloat = NS_MIN(scaleFloat, scaleMaxFloat);
  scaleFloat = NS_MAX(scaleFloat, scaleMinFloat);

  nsAutoString widthStr, heightStr;

  aDocument->GetHeaderData(nsGkAtoms::viewport_height, heightStr);
  aDocument->GetHeaderData(nsGkAtoms::viewport_width, widthStr);

  bool autoSize = false;

  if (widthStr.EqualsLiteral("device-width")) {
    autoSize = true;
  }

  if (widthStr.IsEmpty() &&
     (heightStr.EqualsLiteral("device-height") ||
          scaleFloat == 1.0))
  {
    autoSize = true;
  }

  // XXXjwir3:
  // See bug 706918, comment 23 for more information on this particular section
  // of the code. We're using "screen size" in place of the size of the content
  // area, because on mobile, these are close or equal. This will work for our
  // purposes (bug 706198), but it will need to be changed in the future to be
  // more correct when we bring the rest of the viewport code into platform.
  // We actually want the size of the content area, in the event that we don't
  // have any metadata about the width and/or height. On mobile, the screen size
  // and the size of the content area are very close, or the same value.
  // In XUL fennec, the content area is the size of the <browser> widget, but
  // in native fennec, the content area is the size of the Gecko LayerView
  // object.

  // TODO:
  // Once bug 716575 has been resolved, this code should be changed so that it
  // does the right thing on all platforms.
  nsresult result;
  PRInt32 screenLeft, screenTop, screenWidth, screenHeight;
  nsCOMPtr<nsIScreenManager> screenMgr =
    do_GetService("@mozilla.org/gfx/screenmanager;1", &result);

  nsCOMPtr<nsIScreen> screen;
  screenMgr->GetPrimaryScreen(getter_AddRefs(screen));
  screen->GetRect(&screenLeft, &screenTop, &screenWidth, &screenHeight);

  PRUint32 width = widthStr.ToInteger(&errorCode);
  if (errorCode) {
    if (autoSize) {
      width = screenWidth;
    } else {
      width = Preferences::GetInt("browser.viewport.desktopWidth", 0);
    }
  }

  width = NS_MIN(width, kViewportMaxWidth);
  width = NS_MAX(width, kViewportMinWidth);

  // Also recalculate the default zoom, if it wasn't specified in the metadata,
  // and the width is specified.
  if (scaleStr.IsEmpty() && !widthStr.IsEmpty()) {
    scaleFloat = NS_MAX(scaleFloat, (float)(screenWidth/width));
  }

  PRUint32 height = heightStr.ToInteger(&errorCode);

  if (errorCode) {
    height = width * ((float)screenHeight / screenWidth);
  }

  // If height was provided by the user, but width wasn't, then we should
  // calculate the width.
  if (widthStr.IsEmpty() && !heightStr.IsEmpty()) {
    width = (PRUint32) ((height * screenWidth) / screenHeight);
  }

  height = NS_MIN(height, kViewportMaxHeight);
  height = NS_MAX(height, kViewportMinHeight);

  // We need to perform a conversion, but only if the initial or maximum
  // scale were set explicitly by the user.
  if (!scaleStr.IsEmpty() && !scaleErrorCode) {
    width = NS_MAX(width, (PRUint32)(screenWidth / scaleFloat));
    height = NS_MAX(height, (PRUint32)(screenHeight / scaleFloat));
  } else if (!maxScaleStr.IsEmpty() && !scaleMaxErrorCode) {
    width = NS_MAX(width, (PRUint32)(screenWidth / scaleMaxFloat));
    height = NS_MAX(height, (PRUint32)(screenHeight / scaleMaxFloat));
  }

  bool allowZoom = true;
  nsAutoString userScalable;
  aDocument->GetHeaderData(nsGkAtoms::viewport_user_scalable, userScalable);

  if ((userScalable.EqualsLiteral("0")) ||
      (userScalable.EqualsLiteral("no")) ||
      (userScalable.EqualsLiteral("false"))) {
    allowZoom = false;
  }

  ret.allowZoom = allowZoom;
  ret.width = width;
  ret.height = height;
  ret.defaultZoom = scaleFloat;
  ret.minZoom = scaleMinFloat;
  ret.maxZoom = scaleMaxFloat;
  ret.autoSize = autoSize;
  return ret;
}

/* static */
nsresult
nsContentUtils::ProcessViewportInfo(nsIDocument *aDocument,
                                    const nsAString &viewportInfo) {

  /* We never fail. */
  nsresult rv = NS_OK;

  /* Iterators. */
  nsAString::const_iterator tip, tail, end;
  viewportInfo.BeginReading(tip);
  tail = tip;
  viewportInfo.EndReading(end);

  /* Read the tip to the first non-separator character. */
  while ((tip != end) && (IS_SEPARATOR(*tip) || nsCRT::IsAsciiSpace(*tip)))
    ++tip;

  /* Read through and find tokens separated by separators. */
  while (tip != end) {

    /* Synchronize tip and tail. */
    tail = tip;

    /* Advance tip past non-separator characters. */
    while ((tip != end) && !IS_SEPARATOR(*tip))
      ++tip;

    /* Allow white spaces that surround the '=' character */
    if ((tip != end) && (*tip == '=')) {
      ++tip;

      while ((tip != end) && nsCRT::IsAsciiSpace(*tip))
        ++tip;

      while ((tip != end) && !(IS_SEPARATOR(*tip) || nsCRT::IsAsciiSpace(*tip)))
        ++tip;
    }

    /* Our token consists of the characters between tail and tip. */
    ProcessViewportToken(aDocument, Substring(tail, tip));

    /* Skip separators. */
    while ((tip != end) && (IS_SEPARATOR(*tip) || nsCRT::IsAsciiSpace(*tip)))
      ++tip;
  }

  return rv;

}

#undef IS_SEPARATOR

/* static */
void
nsContentUtils::HidePopupsInDocument(nsIDocument* aDocument)
{
#ifdef MOZ_XUL
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (pm && aDocument) {
    nsCOMPtr<nsISupports> container = aDocument->GetContainer();
    nsCOMPtr<nsIDocShellTreeItem> docShellToHide = do_QueryInterface(container);
    if (docShellToHide)
      pm->HidePopupsInDocShell(docShellToHide);
  }
#endif
}

/* static */
already_AddRefed<nsIDragSession>
nsContentUtils::GetDragSession()
{
  nsIDragSession* dragSession = nsnull;
  nsCOMPtr<nsIDragService> dragService =
    do_GetService("@mozilla.org/widget/dragservice;1");
  if (dragService)
    dragService->GetCurrentSession(&dragSession);
  return dragSession;
}

/* static */
nsresult
nsContentUtils::SetDataTransferInEvent(nsDragEvent* aDragEvent)
{
  if (aDragEvent->dataTransfer || !NS_IS_TRUSTED_EVENT(aDragEvent))
    return NS_OK;

  // For draggesture and dragstart events, the data transfer object is
  // created before the event fires, so it should already be set. For other
  // drag events, get the object from the drag session.
  NS_ASSERTION(aDragEvent->message != NS_DRAGDROP_GESTURE &&
               aDragEvent->message != NS_DRAGDROP_START,
               "draggesture event created without a dataTransfer");

  nsCOMPtr<nsIDragSession> dragSession = GetDragSession();
  NS_ENSURE_TRUE(dragSession, NS_OK); // no drag in progress

  nsCOMPtr<nsIDOMDataTransfer> initialDataTransfer;
  dragSession->GetDataTransfer(getter_AddRefs(initialDataTransfer));
  if (!initialDataTransfer) {
    // A dataTransfer won't exist when a drag was started by some other
    // means, for instance calling the drag service directly, or a drag
    // from another application. In either case, a new dataTransfer should
    // be created that reflects the data.
    initialDataTransfer =
      new nsDOMDataTransfer(aDragEvent->message);
    NS_ENSURE_TRUE(initialDataTransfer, NS_ERROR_OUT_OF_MEMORY);

    // now set it in the drag session so we don't need to create it again
    dragSession->SetDataTransfer(initialDataTransfer);
  }

  bool isCrossDomainSubFrameDrop = false;
  if (aDragEvent->message == NS_DRAGDROP_DROP ||
      aDragEvent->message == NS_DRAGDROP_DRAGDROP) {
    isCrossDomainSubFrameDrop = CheckForSubFrameDrop(dragSession, aDragEvent);
  }

  // each event should use a clone of the original dataTransfer.
  initialDataTransfer->Clone(aDragEvent->message, aDragEvent->userCancelled,
                             isCrossDomainSubFrameDrop,
                             getter_AddRefs(aDragEvent->dataTransfer));
  NS_ENSURE_TRUE(aDragEvent->dataTransfer, NS_ERROR_OUT_OF_MEMORY);

  // for the dragenter and dragover events, initialize the drop effect
  // from the drop action, which platform specific widget code sets before
  // the event is fired based on the keyboard state.
  if (aDragEvent->message == NS_DRAGDROP_ENTER ||
      aDragEvent->message == NS_DRAGDROP_OVER) {
    PRUint32 action, effectAllowed;
    dragSession->GetDragAction(&action);
    aDragEvent->dataTransfer->GetEffectAllowedInt(&effectAllowed);
    aDragEvent->dataTransfer->SetDropEffectInt(FilterDropEffect(action, effectAllowed));
  }
  else if (aDragEvent->message == NS_DRAGDROP_DROP ||
           aDragEvent->message == NS_DRAGDROP_DRAGDROP ||
           aDragEvent->message == NS_DRAGDROP_END) {
    // For the drop and dragend events, set the drop effect based on the
    // last value that the dropEffect had. This will have been set in
    // nsEventStateManager::PostHandleEvent for the last dragenter or
    // dragover event.
    PRUint32 dropEffect;
    initialDataTransfer->GetDropEffectInt(&dropEffect);
    aDragEvent->dataTransfer->SetDropEffectInt(dropEffect);
  }

  return NS_OK;
}

/* static */
PRUint32
nsContentUtils::FilterDropEffect(PRUint32 aAction, PRUint32 aEffectAllowed)
{
  // It is possible for the drag action to include more than one action, but
  // the widget code which sets the action from the keyboard state should only
  // be including one. If multiple actions were set, we just consider them in
  //  the following order:
  //   copy, link, move
  if (aAction & nsIDragService::DRAGDROP_ACTION_COPY)
    aAction = nsIDragService::DRAGDROP_ACTION_COPY;
  else if (aAction & nsIDragService::DRAGDROP_ACTION_LINK)
    aAction = nsIDragService::DRAGDROP_ACTION_LINK;
  else if (aAction & nsIDragService::DRAGDROP_ACTION_MOVE)
    aAction = nsIDragService::DRAGDROP_ACTION_MOVE;

  // Filter the action based on the effectAllowed. If the effectAllowed
  // doesn't include the action, then that action cannot be done, so adjust
  // the action to something that is allowed. For a copy, adjust to move or
  // link. For a move, adjust to copy or link. For a link, adjust to move or
  // link. Otherwise, use none.
  if (aAction & aEffectAllowed ||
      aEffectAllowed == nsIDragService::DRAGDROP_ACTION_UNINITIALIZED)
    return aAction;
  if (aEffectAllowed & nsIDragService::DRAGDROP_ACTION_MOVE)
    return nsIDragService::DRAGDROP_ACTION_MOVE;
  if (aEffectAllowed & nsIDragService::DRAGDROP_ACTION_COPY)
    return nsIDragService::DRAGDROP_ACTION_COPY;
  if (aEffectAllowed & nsIDragService::DRAGDROP_ACTION_LINK)
    return nsIDragService::DRAGDROP_ACTION_LINK;
  return nsIDragService::DRAGDROP_ACTION_NONE;
}

/* static */
bool
nsContentUtils::CheckForSubFrameDrop(nsIDragSession* aDragSession, nsDragEvent* aDropEvent)
{
  nsCOMPtr<nsIContent> target = do_QueryInterface(aDropEvent->originalTarget);
  if (!target && !target->OwnerDoc()) {
    return true;
  }
  
  nsIDocument* targetDoc = target->OwnerDoc();
  nsCOMPtr<nsIWebNavigation> twebnav = do_GetInterface(targetDoc->GetWindow());
  nsCOMPtr<nsIDocShellTreeItem> tdsti = do_QueryInterface(twebnav);
  if (!tdsti) {
    return true;
  }

  PRInt32 type = -1;
  if (NS_FAILED(tdsti->GetItemType(&type))) {
    return true;
  }

  // Always allow dropping onto chrome shells.
  if (type == nsIDocShellTreeItem::typeChrome) {
    return false;
  }

  // If there is no source node, then this is a drag from another
  // application, which should be allowed.
  nsCOMPtr<nsIDOMDocument> sourceDocument;
  aDragSession->GetSourceDocument(getter_AddRefs(sourceDocument));
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(sourceDocument);
  if (doc) {
    // Get each successive parent of the source document and compare it to
    // the drop document. If they match, then this is a drag from a child frame.
    do {
      doc = doc->GetParentDocument();
      if (doc == targetDoc) {
        // The drag is from a child frame.
        return true;
      }
    } while (doc);
  }

  return false;
}

/* static */
bool
nsContentUtils::URIIsLocalFile(nsIURI *aURI)
{
  bool isFile;
  nsCOMPtr<nsINetUtil> util = do_QueryInterface(sIOService);

  // Important: we do NOT test the entire URI chain here!
  return util && NS_SUCCEEDED(util->ProtocolHasFlags(aURI,
                                nsIProtocolHandler::URI_IS_LOCAL_FILE,
                                &isFile)) &&
         isFile;
}

nsresult
nsContentUtils::SplitURIAtHash(nsIURI *aURI,
                               nsACString &aBeforeHash,
                               nsACString &aAfterHash)
{
  // See bug 225910 for why we can't do this using nsIURL.

  aBeforeHash.Truncate();
  aAfterHash.Truncate();

  NS_ENSURE_ARG_POINTER(aURI);

  nsCAutoString spec;
  nsresult rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 index = spec.FindChar('#');
  if (index == -1) {
    index = spec.Length();
  }

  aBeforeHash.Assign(Substring(spec, 0, index));
  aAfterHash.Assign(Substring(spec, index));
  return NS_OK;
}

/* static */
nsIScriptContext*
nsContentUtils::GetContextForEventHandlers(nsINode* aNode,
                                           nsresult* aRv)
{
  *aRv = NS_OK;
  bool hasHadScriptObject = true;
  nsIScriptGlobalObject* sgo =
    aNode->OwnerDoc()->GetScriptHandlingObject(hasHadScriptObject);
  // It is bad if the document doesn't have event handling context,
  // but it used to have one.
  if (!sgo && hasHadScriptObject) {
    *aRv = NS_ERROR_UNEXPECTED;
    return nsnull;
  }

  if (sgo) {
    nsIScriptContext* scx = sgo->GetContext();
    // Bad, no context from script global object!
    if (!scx) {
      *aRv = NS_ERROR_UNEXPECTED;
      return nsnull;
    }
    return scx;
  }

  return nsnull;
}

/* static */
JSContext *
nsContentUtils::GetCurrentJSContext()
{
  JSContext *cx = nsnull;

  sThreadJSContextStack->Peek(&cx);

  return cx;
}

/* static */
JSContext *
nsContentUtils::GetSafeJSContext()
{
  return sThreadJSContextStack->GetSafeJSContext();
}

/* static */
nsresult
nsContentUtils::ASCIIToLower(nsAString& aStr)
{
  PRUnichar* iter = aStr.BeginWriting();
  PRUnichar* end = aStr.EndWriting();
  if (NS_UNLIKELY(!iter || !end)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  while (iter != end) {
    PRUnichar c = *iter;
    if (c >= 'A' && c <= 'Z') {
      *iter = c + ('a' - 'A');
    }
    ++iter;
  }
  return NS_OK;
}

/* static */
nsresult
nsContentUtils::ASCIIToLower(const nsAString& aSource, nsAString& aDest)
{
  PRUint32 len = aSource.Length();
  aDest.SetLength(len);
  if (aDest.Length() == len) {
    PRUnichar* dest = aDest.BeginWriting();
    if (NS_UNLIKELY(!dest)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    const PRUnichar* iter = aSource.BeginReading();
    const PRUnichar* end = aSource.EndReading();
    while (iter != end) {
      PRUnichar c = *iter;
      *dest = (c >= 'A' && c <= 'Z') ?
         c + ('a' - 'A') : c;
      ++iter;
      ++dest;
    }
    return NS_OK;
  }
  return NS_ERROR_OUT_OF_MEMORY;
}

/* static */
nsresult
nsContentUtils::ASCIIToUpper(nsAString& aStr)
{
  PRUnichar* iter = aStr.BeginWriting();
  PRUnichar* end = aStr.EndWriting();
  if (NS_UNLIKELY(!iter || !end)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  while (iter != end) {
    PRUnichar c = *iter;
    if (c >= 'a' && c <= 'z') {
      *iter = c + ('A' - 'a');
    }
    ++iter;
  }
  return NS_OK;
}

/* static */
nsresult
nsContentUtils::ASCIIToUpper(const nsAString& aSource, nsAString& aDest)
{
  PRUint32 len = aSource.Length();
  aDest.SetLength(len);
  if (aDest.Length() == len) {
    PRUnichar* dest = aDest.BeginWriting();
    if (NS_UNLIKELY(!dest)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    const PRUnichar* iter = aSource.BeginReading();
    const PRUnichar* end = aSource.EndReading();
    while (iter != end) {
      PRUnichar c = *iter;
      *dest = (c >= 'a' && c <= 'z') ?
         c + ('A' - 'a') : c;
      ++iter;
      ++dest;
    }
    return NS_OK;
  }
  return NS_ERROR_OUT_OF_MEMORY;
}

/* static */
bool
nsContentUtils::EqualsIgnoreASCIICase(const nsAString& aStr1,
                                      const nsAString& aStr2)
{
  PRUint32 len = aStr1.Length();
  if (len != aStr2.Length()) {
    return false;
  }

  const PRUnichar* str1 = aStr1.BeginReading();
  const PRUnichar* str2 = aStr2.BeginReading();
  const PRUnichar* end = str1 + len;

  while (str1 < end) {
    PRUnichar c1 = *str1++;
    PRUnichar c2 = *str2++;

    // First check if any bits other than the 0x0020 differs
    if ((c1 ^ c2) & 0xffdf) {
      return false;
    }

    // We know they can only differ in the 0x0020 bit.
    // Likely the two chars are the same, so check that first
    if (c1 != c2) {
      // They do differ, but since it's only in the 0x0020 bit, check if it's
      // the same ascii char, but just differing in case
      PRUnichar c1Upper = c1 & 0xffdf;
      if (!('A' <= c1Upper && c1Upper <= 'Z')) {
        return false;
      }
    }
  }

  return true;
}

/* static */
bool
nsContentUtils::EqualsLiteralIgnoreASCIICase(const nsAString& aStr1,
                                             const char* aStr2,
                                             const PRUint32 len)
{
  if (aStr1.Length() != len) {
    return false;
  }
  
  const PRUnichar* str1 = aStr1.BeginReading();
  const char*      str2 = aStr2;
  const PRUnichar* end = str1 + len;
  
  while (str1 < end) {
    PRUnichar c1 = *str1++;
    PRUnichar c2 = *str2++;

    // First check if any bits other than the 0x0020 differs
    if ((c1 ^ c2) & 0xffdf) {
      return false;
    }
    
    // We know they can only differ in the 0x0020 bit.
    // Likely the two chars are the same, so check that first
    if (c1 != c2) {
      // They do differ, but since it's only in the 0x0020 bit, check if it's
      // the same ascii char, but just differing in case
      PRUnichar c1Upper = c1 & 0xffdf;
      if (!('A' <= c1Upper && c1Upper <= 'Z')) {
        return false;
      }
    }
  }
  
  return true;
}

/* static */
nsIInterfaceRequestor*
nsContentUtils::GetSameOriginChecker()
{
  if (!sSameOriginChecker) {
    sSameOriginChecker = new SameOriginChecker();
    NS_IF_ADDREF(sSameOriginChecker);
  }
  return sSameOriginChecker;
}

/* static */
nsresult
nsContentUtils::CheckSameOrigin(nsIChannel *aOldChannel, nsIChannel *aNewChannel)
{
  if (!nsContentUtils::GetSecurityManager())
    return NS_ERROR_NOT_AVAILABLE;

  nsCOMPtr<nsIPrincipal> oldPrincipal;
  nsContentUtils::GetSecurityManager()->
    GetChannelPrincipal(aOldChannel, getter_AddRefs(oldPrincipal));

  nsCOMPtr<nsIURI> newURI;
  aNewChannel->GetURI(getter_AddRefs(newURI));
  nsCOMPtr<nsIURI> newOriginalURI;
  aNewChannel->GetOriginalURI(getter_AddRefs(newOriginalURI));

  NS_ENSURE_STATE(oldPrincipal && newURI && newOriginalURI);

  nsresult rv = oldPrincipal->CheckMayLoad(newURI, false);
  if (NS_SUCCEEDED(rv) && newOriginalURI != newURI) {
    rv = oldPrincipal->CheckMayLoad(newOriginalURI, false);
  }

  return rv;
}

NS_IMPL_ISUPPORTS2(SameOriginChecker,
                   nsIChannelEventSink,
                   nsIInterfaceRequestor)

NS_IMETHODIMP
SameOriginChecker::AsyncOnChannelRedirect(nsIChannel *aOldChannel,
                                          nsIChannel *aNewChannel,
                                          PRUint32 aFlags,
                                          nsIAsyncVerifyRedirectCallback *cb)
{
  NS_PRECONDITION(aNewChannel, "Redirecting to null channel?");

  nsresult rv = nsContentUtils::CheckSameOrigin(aOldChannel, aNewChannel);
  if (NS_SUCCEEDED(rv)) {
    cb->OnRedirectVerifyCallback(NS_OK);
  }

  return rv;
}

NS_IMETHODIMP
SameOriginChecker::GetInterface(const nsIID & aIID, void **aResult)
{
  return QueryInterface(aIID, aResult);
}

/* static */
nsresult
nsContentUtils::GetASCIIOrigin(nsIPrincipal* aPrincipal, nsCString& aOrigin)
{
  NS_PRECONDITION(aPrincipal, "missing principal");

  aOrigin.Truncate();

  nsCOMPtr<nsIURI> uri;
  nsresult rv = aPrincipal->GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  if (uri) {
    return GetASCIIOrigin(uri, aOrigin);
  }

  aOrigin.AssignLiteral("null");

  return NS_OK;
}

/* static */
nsresult
nsContentUtils::GetASCIIOrigin(nsIURI* aURI, nsCString& aOrigin)
{
  NS_PRECONDITION(aURI, "missing uri");

  aOrigin.Truncate();

  nsCOMPtr<nsIURI> uri = NS_GetInnermostURI(aURI);
  NS_ENSURE_TRUE(uri, NS_ERROR_UNEXPECTED);

  nsCString host;
  nsresult rv = uri->GetAsciiHost(host);

  if (NS_SUCCEEDED(rv) && !host.IsEmpty()) {
    nsCString scheme;
    rv = uri->GetScheme(scheme);
    NS_ENSURE_SUCCESS(rv, rv);

    PRInt32 port = -1;
    uri->GetPort(&port);
    if (port != -1 && port == NS_GetDefaultPort(scheme.get()))
      port = -1;

    nsCString hostPort;
    rv = NS_GenerateHostPort(host, port, hostPort);
    NS_ENSURE_SUCCESS(rv, rv);

    aOrigin = scheme + NS_LITERAL_CSTRING("://") + hostPort;
  }
  else {
    aOrigin.AssignLiteral("null");
  }

  return NS_OK;
}

/* static */
nsresult
nsContentUtils::GetUTFOrigin(nsIPrincipal* aPrincipal, nsString& aOrigin)
{
  NS_PRECONDITION(aPrincipal, "missing principal");

  aOrigin.Truncate();

  nsCOMPtr<nsIURI> uri;
  nsresult rv = aPrincipal->GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  if (uri) {
    return GetUTFOrigin(uri, aOrigin);
  }

  aOrigin.AssignLiteral("null");

  return NS_OK;
}

/* static */
nsresult
nsContentUtils::GetUTFOrigin(nsIURI* aURI, nsString& aOrigin)
{
  NS_PRECONDITION(aURI, "missing uri");

  aOrigin.Truncate();

  nsCOMPtr<nsIURI> uri = NS_GetInnermostURI(aURI);
  NS_ENSURE_TRUE(uri, NS_ERROR_UNEXPECTED);

  nsCString host;
  nsresult rv = uri->GetHost(host);

  if (NS_SUCCEEDED(rv) && !host.IsEmpty()) {
    nsCString scheme;
    rv = uri->GetScheme(scheme);
    NS_ENSURE_SUCCESS(rv, rv);

    PRInt32 port = -1;
    uri->GetPort(&port);
    if (port != -1 && port == NS_GetDefaultPort(scheme.get()))
      port = -1;

    nsCString hostPort;
    rv = NS_GenerateHostPort(host, port, hostPort);
    NS_ENSURE_SUCCESS(rv, rv);

    aOrigin = NS_ConvertUTF8toUTF16(
      scheme + NS_LITERAL_CSTRING("://") + hostPort);
  }
  else {
    aOrigin.AssignLiteral("null");
  }
  
  return NS_OK;
}

/* static */
already_AddRefed<nsIDocument>
nsContentUtils::GetDocumentFromScriptContext(nsIScriptContext *aScriptContext)
{
  if (!aScriptContext)
    return nsnull;

  nsCOMPtr<nsIDOMWindow> window =
    do_QueryInterface(aScriptContext->GetGlobalObject());
  nsIDocument *doc = nsnull;
  if (window) {
    nsCOMPtr<nsIDOMDocument> domdoc;
    window->GetDocument(getter_AddRefs(domdoc));
    if (domdoc) {
      CallQueryInterface(domdoc, &doc);
    }
  }
  return doc;
}

/* static */
bool
nsContentUtils::CheckMayLoad(nsIPrincipal* aPrincipal, nsIChannel* aChannel)
{
  nsCOMPtr<nsIURI> channelURI;
  nsresult rv = NS_GetFinalChannelURI(aChannel, getter_AddRefs(channelURI));
  NS_ENSURE_SUCCESS(rv, false);

  return NS_SUCCEEDED(aPrincipal->CheckMayLoad(channelURI, false));
}

nsContentTypeParser::nsContentTypeParser(const nsAString& aString)
  : mString(aString), mService(nsnull)
{
  CallGetService("@mozilla.org/network/mime-hdrparam;1", &mService);
}

nsContentTypeParser::~nsContentTypeParser()
{
  NS_IF_RELEASE(mService);
}

nsresult
nsContentTypeParser::GetParameter(const char* aParameterName, nsAString& aResult)
{
  NS_ENSURE_TRUE(mService, NS_ERROR_FAILURE);
  return mService->GetParameter(mString, aParameterName,
                                EmptyCString(), false, nsnull,
                                aResult);
}

/* static */

// If you change this code, change also AllowedToAct() in
// XPCSystemOnlyWrapper.cpp!
bool
nsContentUtils::CanAccessNativeAnon()
{
  JSContext* cx = nsnull;
  sThreadJSContextStack->Peek(&cx);
  if (!cx) {
    return true;
  }
  JSStackFrame* fp;
  nsIPrincipal* principal =
    sSecurityManager->GetCxSubjectPrincipalAndFrame(cx, &fp);
  NS_ENSURE_TRUE(principal, false);

  JSScript *script = nsnull;
  if (!fp) {
    if (!JS_DescribeScriptedCaller(cx, &script, nsnull)) {
      // No code at all is running. So we must be arriving here as the result
      // of C++ code asking us to do something. Allow access.
      return true;
    }
  } else if (JS_IsScriptFrame(cx, fp)) {
    script = JS_GetFrameScript(cx, fp);
  }

  bool privileged;
  if (NS_SUCCEEDED(sSecurityManager->IsSystemPrincipal(principal, &privileged)) &&
      privileged) {
    // Chrome things are allowed to touch us.
    return true;
  }

  // XXX HACK EWW! Allow chrome://global/ access to these things, even
  // if they've been cloned into less privileged contexts.
  static const char prefix[] = "chrome://global/";
  const char *filename;
  if (script &&
      (filename = JS_GetScriptFilename(cx, script)) &&
      !strncmp(filename, prefix, ArrayLength(prefix) - 1)) {
    return true;
  }

  // Before we throw, check for UniversalXPConnect.
  nsresult rv = sSecurityManager->IsCapabilityEnabled("UniversalXPConnect", &privileged);
  if (NS_SUCCEEDED(rv) && privileged) {
    return true;
  }

  return false;
}

/* static */ nsresult
nsContentUtils::DispatchXULCommand(nsIContent* aTarget,
                                   bool aTrusted,
                                   nsIDOMEvent* aSourceEvent,
                                   nsIPresShell* aShell,
                                   bool aCtrl,
                                   bool aAlt,
                                   bool aShift,
                                   bool aMeta)
{
  NS_ENSURE_STATE(aTarget);
  nsIDocument* doc = aTarget->OwnerDoc();
  nsCOMPtr<nsIDOMDocument> domDoc = do_QueryInterface(doc);
  NS_ENSURE_STATE(domDoc);
  nsCOMPtr<nsIDOMEvent> event;
  domDoc->CreateEvent(NS_LITERAL_STRING("xulcommandevent"),
                      getter_AddRefs(event));
  nsCOMPtr<nsIDOMXULCommandEvent> xulCommand = do_QueryInterface(event);
  nsresult rv = xulCommand->InitCommandEvent(NS_LITERAL_STRING("command"),
                                             true, true, doc->GetWindow(),
                                             0, aCtrl, aAlt, aShift, aMeta,
                                             aSourceEvent);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aShell) {
    nsEventStatus status = nsEventStatus_eIgnore;
    nsCOMPtr<nsIPresShell> kungFuDeathGrip = aShell;
    return aShell->HandleDOMEventWithTarget(aTarget, event, &status);
  }

  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(aTarget);
  NS_ENSURE_STATE(target);
  bool dummy;
  return target->DispatchEvent(event, &dummy);
}

// static
nsresult
nsContentUtils::WrapNative(JSContext *cx, JSObject *scope, nsISupports *native,
                           nsWrapperCache *cache, const nsIID* aIID, jsval *vp,
                           nsIXPConnectJSObjectHolder **aHolder,
                           bool aAllowWrapping)
{
  if (!native) {
    NS_ASSERTION(!aHolder || !*aHolder, "*aHolder should be null!");

    *vp = JSVAL_NULL;

    return NS_OK;
  }

  JSObject *wrapper = xpc_FastGetCachedWrapper(cache, scope, vp);
  if (wrapper) {
    return NS_OK;
  }

  NS_ENSURE_TRUE(sXPConnect && sThreadJSContextStack, NS_ERROR_UNEXPECTED);

  // Keep sXPConnect and sThreadJSContextStack alive. If we're on the main
  // thread then this can be done simply and cheaply by adding a reference to
  // nsLayoutStatics. If we're not on the main thread then we need to add a
  // more expensive reference sXPConnect directly. We have to use manual
  // AddRef and Release calls so don't early-exit from this function after we've
  // added the reference!
  bool isMainThread = NS_IsMainThread();

  if (isMainThread) {
    nsLayoutStatics::AddRef();
  }
  else {
    sXPConnect->AddRef();
  }

  JSContext *topJSContext;
  nsresult rv = sThreadJSContextStack->Peek(&topJSContext);
  if (NS_SUCCEEDED(rv)) {
    bool push = topJSContext != cx;
    if (push) {
      rv = sThreadJSContextStack->Push(cx);
    }
    if (NS_SUCCEEDED(rv)) {
      rv = sXPConnect->WrapNativeToJSVal(cx, scope, native, cache, aIID,
                                         aAllowWrapping, vp, aHolder);
      if (push) {
        sThreadJSContextStack->Pop(nsnull);
      }
    }
  }

  if (isMainThread) {
    nsLayoutStatics::Release();
  }
  else {
    sXPConnect->Release();
  }

  return rv;
}

nsresult
nsContentUtils::CreateArrayBuffer(JSContext *aCx, const nsACString& aData,
                                  JSObject** aResult)
{
  if (!aCx) {
    return NS_ERROR_FAILURE;
  }

  PRInt32 dataLen = aData.Length();
  *aResult = JS_NewArrayBuffer(aCx, dataLen);
  if (!*aResult) {
    return NS_ERROR_FAILURE;
  }

  if (dataLen > 0) {
    NS_ASSERTION(JS_IsArrayBufferObject(*aResult, aCx), "What happened?");
    memcpy(JS_GetArrayBufferData(*aResult, aCx), aData.BeginReading(), dataLen);
  }

  return NS_OK;
}

void
nsContentUtils::StripNullChars(const nsAString& aInStr, nsAString& aOutStr)
{
  // In common cases where we don't have nulls in the
  // string we can simple simply bypass the checking code.
  PRInt32 firstNullPos = aInStr.FindChar('\0');
  if (firstNullPos == kNotFound) {
    aOutStr.Assign(aInStr);
    return;
  }

  aOutStr.SetCapacity(aInStr.Length() - 1);
  nsAString::const_iterator start, end;
  aInStr.BeginReading(start);
  aInStr.EndReading(end);
  while (start != end) {
    if (*start != '\0')
      aOutStr.Append(*start);
    ++start;
  }
}

struct ClassMatchingInfo {
  nsAttrValue::AtomArray mClasses;
  nsCaseTreatment mCaseTreatment;
};

static bool
MatchClassNames(nsIContent* aContent, PRInt32 aNamespaceID, nsIAtom* aAtom,
                void* aData)
{
  // We can't match if there are no class names
  const nsAttrValue* classAttr = aContent->GetClasses();
  if (!classAttr) {
    return false;
  }
  
  // need to match *all* of the classes
  ClassMatchingInfo* info = static_cast<ClassMatchingInfo*>(aData);
  PRUint32 length = info->mClasses.Length();
  if (!length) {
    // If we actually had no classes, don't match.
    return false;
  }
  PRUint32 i;
  for (i = 0; i < length; ++i) {
    if (!classAttr->Contains(info->mClasses[i],
                             info->mCaseTreatment)) {
      return false;
    }
  }
  
  return true;
}

static void
DestroyClassNameArray(void* aData)
{
  ClassMatchingInfo* info = static_cast<ClassMatchingInfo*>(aData);
  delete info;
}

static void*
AllocClassMatchingInfo(nsINode* aRootNode,
                       const nsString* aClasses)
{
  nsAttrValue attrValue;
  attrValue.ParseAtomArray(*aClasses);
  // nsAttrValue::Equals is sensitive to order, so we'll send an array
  ClassMatchingInfo* info = new ClassMatchingInfo;
  NS_ENSURE_TRUE(info, nsnull);

  if (attrValue.Type() == nsAttrValue::eAtomArray) {
    info->mClasses.SwapElements(*(attrValue.GetAtomArrayValue()));
  } else if (attrValue.Type() == nsAttrValue::eAtom) {
    info->mClasses.AppendElement(attrValue.GetAtomValue());
  }

  info->mCaseTreatment =
    aRootNode->OwnerDoc()->GetCompatibilityMode() == eCompatibility_NavQuirks ?
    eIgnoreCase : eCaseMatters;
  return info;
}

// static

nsresult
nsContentUtils::GetElementsByClassName(nsINode* aRootNode,
                                       const nsAString& aClasses,
                                       nsIDOMNodeList** aReturn)
{
  NS_PRECONDITION(aRootNode, "Must have root node");
  
  nsContentList* elements =
    NS_GetFuncStringContentList(aRootNode, MatchClassNames,
                                DestroyClassNameArray,
                                AllocClassMatchingInfo,
                                aClasses).get();
  NS_ENSURE_TRUE(elements, NS_ERROR_OUT_OF_MEMORY);

  // Transfer ownership
  *aReturn = elements;

  return NS_OK;
}

#ifdef DEBUG
class DebugWrapperTraversalCallback : public nsCycleCollectionTraversalCallback
{
public:
  DebugWrapperTraversalCallback(void* aWrapper) : mFound(false),
                                                  mWrapper(aWrapper)
  {
    mFlags = WANT_ALL_TRACES;
  }

  NS_IMETHOD_(void) DescribeRefCountedNode(nsrefcnt refCount,
                                           size_t objSz,
                                           const char *objName)
  {
  }
  NS_IMETHOD_(void) DescribeGCedNode(bool isMarked,
                                     size_t objSz,
                                     const char *objName)
  {
  }

  NS_IMETHOD_(void) NoteXPCOMRoot(nsISupports *root)
  {
  }
  NS_IMETHOD_(void) NoteJSRoot(void* root)
  {
  }
  NS_IMETHOD_(void) NoteNativeRoot(void* root,
                                   nsCycleCollectionParticipant* helper)
  {
  }

  NS_IMETHOD_(void) NoteJSChild(void* child)
  {
    if (child == mWrapper) {
      mFound = true;
    }
  }
  NS_IMETHOD_(void) NoteXPCOMChild(nsISupports *child)
  {
  }
  NS_IMETHOD_(void) NoteNativeChild(void* child,
                                    nsCycleCollectionParticipant* helper)
  {
  }

  NS_IMETHOD_(void) NoteNextEdgeName(const char* name)
  {
  }

  NS_IMETHOD_(void) NoteWeakMapping(void* map, void* key, void* val)
  {
  }

  bool mFound;

private:
  void* mWrapper;
};

static void
DebugWrapperTraceCallback(void *p, const char *name, void *closure)
{
  DebugWrapperTraversalCallback* callback =
    static_cast<DebugWrapperTraversalCallback*>(closure);
  callback->NoteJSChild(p);
}

// static
void
nsContentUtils::CheckCCWrapperTraversal(nsISupports* aScriptObjectHolder,
                                        nsWrapperCache* aCache)
{
  JSObject* wrapper = aCache->GetWrapper();
  if (!wrapper) {
    return;
  }

  nsXPCOMCycleCollectionParticipant* participant;
  CallQueryInterface(aScriptObjectHolder, &participant);

  DebugWrapperTraversalCallback callback(wrapper);

  participant->Traverse(aScriptObjectHolder, callback);
  NS_ASSERTION(callback.mFound,
               "Cycle collection participant didn't traverse to preserved "
               "wrapper! This will probably crash.");

  callback.mFound = false;
  participant->Trace(aScriptObjectHolder, DebugWrapperTraceCallback, &callback);
  NS_ASSERTION(callback.mFound,
               "Cycle collection participant didn't trace preserved wrapper! "
               "This will probably crash.");
}
#endif

// static
bool
nsContentUtils::IsFocusedContent(const nsIContent* aContent)
{
  nsFocusManager* fm = nsFocusManager::GetFocusManager();

  return fm && fm->GetFocusedContent() == aContent;
}

bool
nsContentUtils::IsSubDocumentTabbable(nsIContent* aContent)
{
  nsIDocument* doc = aContent->GetCurrentDoc();
  if (!doc) {
    return false;
  }

  // If the subdocument lives in another process, the frame is
  // tabbable.
  if (nsEventStateManager::IsRemoteTarget(aContent)) {
    return true;
  }

  // XXXbz should this use OwnerDoc() for GetSubDocumentFor?
  // sXBL/XBL2 issue!
  nsIDocument* subDoc = doc->GetSubDocumentFor(aContent);
  if (!subDoc) {
    return false;
  }

  nsCOMPtr<nsISupports> container = subDoc->GetContainer();
  nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(container);
  if (!docShell) {
    return false;
  }

  nsCOMPtr<nsIContentViewer> contentViewer;
  docShell->GetContentViewer(getter_AddRefs(contentViewer));
  if (!contentViewer) {
    return false;
  }

  nsCOMPtr<nsIContentViewer> zombieViewer;
  contentViewer->GetPreviousViewer(getter_AddRefs(zombieViewer));

  // If there are 2 viewers for the current docshell, that
  // means the current document is a zombie document.
  // Only navigate into the subdocument if it's not a zombie.
  return !zombieViewer;
}

void
nsContentUtils::FlushLayoutForTree(nsIDOMWindow* aWindow)
{
    nsCOMPtr<nsPIDOMWindow> piWin = do_QueryInterface(aWindow);
    if (!piWin)
        return;

    // Note that because FlushPendingNotifications flushes parents, this
    // is O(N^2) in docshell tree depth.  However, the docshell tree is
    // usually pretty shallow.

    nsCOMPtr<nsIDOMDocument> domDoc;
    aWindow->GetDocument(getter_AddRefs(domDoc));
    nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);
    if (doc) {
        doc->FlushPendingNotifications(Flush_Layout);
    }

    nsCOMPtr<nsIDocShellTreeNode> node =
        do_QueryInterface(piWin->GetDocShell());
    if (node) {
        PRInt32 i = 0, i_end;
        node->GetChildCount(&i_end);
        for (; i < i_end; ++i) {
            nsCOMPtr<nsIDocShellTreeItem> item;
            node->GetChildAt(i, getter_AddRefs(item));
            nsCOMPtr<nsIDOMWindow> win = do_GetInterface(item);
            if (win) {
                FlushLayoutForTree(win);
            }
        }
    }
}

void nsContentUtils::RemoveNewlines(nsString &aString)
{
  // strip CR/LF and null
  static const char badChars[] = {'\r', '\n', 0};
  aString.StripChars(badChars);
}

void
nsContentUtils::PlatformToDOMLineBreaks(nsString &aString)
{
  if (aString.FindChar(PRUnichar('\r')) != -1) {
    // Windows linebreaks: Map CRLF to LF:
    aString.ReplaceSubstring(NS_LITERAL_STRING("\r\n").get(),
                             NS_LITERAL_STRING("\n").get());

    // Mac linebreaks: Map any remaining CR to LF:
    aString.ReplaceSubstring(NS_LITERAL_STRING("\r").get(),
                             NS_LITERAL_STRING("\n").get());
  }
}

nsIWidget *
nsContentUtils::WidgetForDocument(nsIDocument *aDoc)
{
  nsIDocument* doc = aDoc;
  nsIDocument* displayDoc = doc->GetDisplayDocument();
  if (displayDoc) {
    doc = displayDoc;
  }

  nsIPresShell* shell = doc->GetShell();
  nsCOMPtr<nsISupports> container = doc->GetContainer();
  nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem = do_QueryInterface(container);
  while (!shell && docShellTreeItem) {
    // We may be in a display:none subdocument, or we may not have a presshell
    // created yet.
    // Walk the docshell tree to find the nearest container that has a presshell,
    // and find the root widget from that.
    nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(docShellTreeItem);
    nsCOMPtr<nsIPresShell> presShell;
    docShell->GetPresShell(getter_AddRefs(presShell));
    if (presShell) {
      shell = presShell;
    } else {
      nsCOMPtr<nsIDocShellTreeItem> parent;
      docShellTreeItem->GetParent(getter_AddRefs(parent));
      docShellTreeItem = parent;
    }
  }

  if (shell) {
    nsIViewManager* VM = shell->GetViewManager();
    if (VM) {
      nsIView* rootView = VM->GetRootView();
      if (rootView) {
        nsIView* displayRoot = nsIViewManager::GetDisplayRootFor(rootView);
        if (displayRoot) {
          return displayRoot->GetNearestWidget(nsnull);
        }
      }
    }
  }

  return nsnull;
}

static already_AddRefed<LayerManager>
LayerManagerForDocumentInternal(nsIDocument *aDoc, bool aRequirePersistent,
                                bool* aAllowRetaining)
{
  nsIWidget *widget = nsContentUtils::WidgetForDocument(aDoc);
  if (widget) {
    nsRefPtr<LayerManager> manager =
      widget->GetLayerManager(aRequirePersistent ? nsIWidget::LAYER_MANAGER_PERSISTENT : 
                              nsIWidget::LAYER_MANAGER_CURRENT,
                              aAllowRetaining);
    return manager.forget();
  }

  return nsnull;
}

already_AddRefed<LayerManager>
nsContentUtils::LayerManagerForDocument(nsIDocument *aDoc, bool *aAllowRetaining)
{
  return LayerManagerForDocumentInternal(aDoc, false, aAllowRetaining);
}

already_AddRefed<LayerManager>
nsContentUtils::PersistentLayerManagerForDocument(nsIDocument *aDoc, bool *aAllowRetaining)
{
  return LayerManagerForDocumentInternal(aDoc, true, aAllowRetaining);
}

bool
nsContentUtils::AllowXULXBLForPrincipal(nsIPrincipal* aPrincipal)
{
  if (IsSystemPrincipal(aPrincipal)) {
    return true;
  }
  
  nsCOMPtr<nsIURI> princURI;
  aPrincipal->GetURI(getter_AddRefs(princURI));
  
  return princURI &&
         ((sAllowXULXBL_for_file && SchemeIs(princURI, "file")) ||
          IsSitePermAllow(aPrincipal, "allowXULXBL"));
}

already_AddRefed<nsIDocumentLoaderFactory>
nsContentUtils::FindInternalContentViewer(const char* aType,
                                          ContentViewerType* aLoaderType)
{
  if (aLoaderType) {
    *aLoaderType = TYPE_UNSUPPORTED;
  }

  // one helper factory, please
  nsCOMPtr<nsICategoryManager> catMan(do_GetService(NS_CATEGORYMANAGER_CONTRACTID));
  if (!catMan)
    return NULL;

  nsCOMPtr<nsIDocumentLoaderFactory> docFactory;

  nsXPIDLCString contractID;
  nsresult rv = catMan->GetCategoryEntry("Gecko-Content-Viewers", aType, getter_Copies(contractID));
  if (NS_SUCCEEDED(rv)) {
    docFactory = do_GetService(contractID);
    if (docFactory && aLoaderType) {
      if (contractID.EqualsLiteral(CONTENT_DLF_CONTRACTID))
        *aLoaderType = TYPE_CONTENT;
      else if (contractID.EqualsLiteral(PLUGIN_DLF_CONTRACTID))
        *aLoaderType = TYPE_PLUGIN;
      else
      *aLoaderType = TYPE_UNKNOWN;
    }
    return docFactory.forget();
  }

#ifdef MOZ_MEDIA
#ifdef MOZ_OGG
  if (nsHTMLMediaElement::IsOggEnabled()) {
    for (unsigned int i = 0; i < ArrayLength(nsHTMLMediaElement::gOggTypes); ++i) {
      const char* type = nsHTMLMediaElement::gOggTypes[i];
      if (!strcmp(aType, type)) {
        docFactory = do_GetService("@mozilla.org/content/document-loader-factory;1");
        if (docFactory && aLoaderType) {
          *aLoaderType = TYPE_CONTENT;
        }
        return docFactory.forget();
      }
    }
  }
#endif

#ifdef MOZ_WEBM
  if (nsHTMLMediaElement::IsWebMEnabled()) {
    for (unsigned int i = 0; i < ArrayLength(nsHTMLMediaElement::gWebMTypes); ++i) {
      const char* type = nsHTMLMediaElement::gWebMTypes[i];
      if (!strcmp(aType, type)) {
        docFactory = do_GetService("@mozilla.org/content/document-loader-factory;1");
        if (docFactory && aLoaderType) {
          *aLoaderType = TYPE_CONTENT;
        }
        return docFactory.forget();
      }
    }
  }
#endif

#ifdef MOZ_GSTREAMER
  if (nsHTMLMediaElement::IsH264Enabled()) {
    for (unsigned int i = 0; i < ArrayLength(nsHTMLMediaElement::gH264Types); ++i) {
      const char* type = nsHTMLMediaElement::gH264Types[i];
      if (!strcmp(aType, type)) {
        docFactory = do_GetService("@mozilla.org/content/document-loader-factory;1");
        if (docFactory && aLoaderType) {
          *aLoaderType = TYPE_CONTENT;
        }
        return docFactory.forget();
      }
    }
  }
#endif
#endif // MOZ_MEDIA

  return NULL;
}

// static
bool
nsContentUtils::IsPatternMatching(nsAString& aValue, nsAString& aPattern,
                                  nsIDocument* aDocument)
{
  NS_ASSERTION(aDocument, "aDocument should be a valid pointer (not null)");
  NS_ENSURE_TRUE(aDocument->GetScriptGlobalObject(), true);

  JSContext* ctx = (JSContext*) aDocument->GetScriptGlobalObject()->
                                  GetContext()->GetNativeContext();
  NS_ENSURE_TRUE(ctx, true);

  JSAutoRequest ar(ctx);

  // The pattern has to match the entire value.
  aPattern.Insert(NS_LITERAL_STRING("^(?:"), 0);
  aPattern.Append(NS_LITERAL_STRING(")$"));

  JSObject* re = JS_NewUCRegExpObjectNoStatics(ctx, reinterpret_cast<jschar*>
                                                 (aPattern.BeginWriting()),
                                                aPattern.Length(), 0);
  NS_ENSURE_TRUE(re, true);

  jsval rval = JSVAL_NULL;
  size_t idx = 0;
  JSBool res;

  res = JS_ExecuteRegExpNoStatics(ctx, re, reinterpret_cast<jschar*>
                                    (aValue.BeginWriting()),
                                  aValue.Length(), &idx, JS_TRUE, &rval);

  return res == JS_FALSE || rval != JSVAL_NULL;
}

// static
nsresult
nsContentUtils::URIInheritsSecurityContext(nsIURI *aURI, bool *aResult)
{
  // Note: about:blank URIs do NOT inherit the security context from the
  // current document, which is what this function tests for...
  return NS_URIChainHasFlags(aURI,
                             nsIProtocolHandler::URI_INHERITS_SECURITY_CONTEXT,
                             aResult);
}

// static
bool
nsContentUtils::SetUpChannelOwner(nsIPrincipal* aLoadingPrincipal,
                                  nsIChannel* aChannel,
                                  nsIURI* aURI,
                                  bool aSetUpForAboutBlank)
{
  //
  // Set the owner of the channel, but only for channels that can't
  // provide their own security context.
  //
  // XXX: It seems wrong that the owner is ignored - even if one is
  //      supplied) unless the URI is javascript or data or about:blank.
  // XXX: If this is ever changed, check all callers for what owners
  //      they're passing in.  In particular, see the code and
  //      comments in nsDocShell::LoadURI where we fall back on
  //      inheriting the owner if called from chrome.  That would be
  //      very wrong if this code changed anything but channels that
  //      can't provide their own security context!
  //
  //      (Currently chrome URIs set the owner when they are created!
  //      So setting a NULL owner would be bad!)
  //
  bool inherit;
  // We expect URIInheritsSecurityContext to return success for an
  // about:blank URI, so don't call NS_IsAboutBlank() if this call fails.
  // This condition needs to match the one in nsDocShell::InternalLoad where
  // we're checking for things that will use the owner.
  if (NS_SUCCEEDED(URIInheritsSecurityContext(aURI, &inherit)) &&
      (inherit || (aSetUpForAboutBlank && NS_IsAboutBlank(aURI)))) {
    aChannel->SetOwner(aLoadingPrincipal);
    return true;
  }

  //
  // file: uri special-casing
  //
  // If this is a file: load opened from another file: then it may need
  // to inherit the owner from the referrer so they can script each other.
  // If we don't set the owner explicitly then each file: gets an owner
  // based on its own codebase later.
  //
  if (URIIsLocalFile(aURI) && aLoadingPrincipal &&
      NS_SUCCEEDED(aLoadingPrincipal->CheckMayLoad(aURI, false)) &&
      // One more check here.  CheckMayLoad will always return true for the
      // system principal, but we do NOT want to inherit in that case.
      !IsSystemPrincipal(aLoadingPrincipal)) {
    aChannel->SetOwner(aLoadingPrincipal);
    return true;
  }

  return false;
}

bool
nsContentUtils::IsFullScreenApiEnabled()
{
  return sIsFullScreenApiEnabled;
}

bool
nsContentUtils::IsRequestFullScreenAllowed()
{
  return !sTrustedFullScreenOnly ||
         nsEventStateManager::IsHandlingUserInput() ||
         IsCallerChrome();
}

/* static */
bool
nsContentUtils::HaveEqualPrincipals(nsIDocument* aDoc1, nsIDocument* aDoc2)
{
  if (!aDoc1 || !aDoc2) {
    return false;
  }
  bool principalsEqual = false;
  aDoc1->NodePrincipal()->Equals(aDoc2->NodePrincipal(), &principalsEqual);
  return principalsEqual;
}

static void
CheckForWindowedPlugins(nsIContent* aContent, void* aResult)
{
  if (!aContent->IsInDoc()) {
    return;
  }
  nsCOMPtr<nsIObjectLoadingContent> olc(do_QueryInterface(aContent));
  if (!olc) {
    return;
  }
  nsRefPtr<nsNPAPIPluginInstance> plugin;
  olc->GetPluginInstance(getter_AddRefs(plugin));
  if (!plugin) {
    return;
  }
  bool isWindowless = false;
  nsresult res = plugin->IsWindowless(&isWindowless);
  if (NS_SUCCEEDED(res) && !isWindowless) {
    *static_cast<bool*>(aResult) = true;
  }
}

static bool
DocTreeContainsWindowedPlugins(nsIDocument* aDoc, void* aResult)
{
  if (!nsContentUtils::IsChromeDoc(aDoc)) {
    aDoc->EnumerateFreezableElements(CheckForWindowedPlugins, aResult);
  }
  if (*static_cast<bool*>(aResult)) {
    // Return false to stop iteration, we found a windowed plugin.
    return false;
  }
  aDoc->EnumerateSubDocuments(DocTreeContainsWindowedPlugins, aResult);
  // Return false to stop iteration if we found a windowed plugin in
  // the sub documents.
  return !*static_cast<bool*>(aResult);
}

/* static */
bool
nsContentUtils::HasPluginWithUncontrolledEventDispatch(nsIDocument* aDoc)
{
#ifdef XP_MACOSX
  // We control dispatch to all mac plugins.
  return false;
#endif
  bool result = false;
  
  // Find the top of the document's branch, the child of the chrome document.
  nsIDocument* doc = aDoc;
  nsIDocument* parent = nsnull;
  while (doc && (parent = doc->GetParentDocument()) && !IsChromeDoc(parent)) {
    doc = parent;
  }

  DocTreeContainsWindowedPlugins(doc, &result);
  return result;
}

/* static */
bool
nsContentUtils::HasPluginWithUncontrolledEventDispatch(nsIContent* aContent)
{
#ifdef XP_MACOSX
  // We control dispatch to all mac plugins.
  return false;
#endif
  bool result = false;
  CheckForWindowedPlugins(aContent, &result);
  return result;
}

/* static */
nsIDocument*
nsContentUtils::GetRootDocument(nsIDocument* aDoc)
{
  if (!aDoc) {
    return nsnull;
  }
  nsIDocument* doc = aDoc;
  while (doc->GetParentDocument()) {
    doc = doc->GetParentDocument();
  }
  return doc;
}

// static
void
nsContentUtils::ReleaseWrapper(nsISupports* aScriptObjectHolder,
                               nsWrapperCache* aCache)
{
  if (aCache->PreservingWrapper()) {
    // PreserveWrapper puts new DOM bindings in the JS holders hash, but they
    // can also be in the DOM expando hash, so we need to try to remove them
    // from both here.
    JSObject* obj = aCache->GetWrapperPreserveColor();
    if (aCache->IsDOMBinding() && obj) {
      JSCompartment *compartment = js::GetObjectCompartment(obj);
      xpc::CompartmentPrivate *priv =
        static_cast<xpc::CompartmentPrivate *>(JS_GetCompartmentPrivate(compartment));
      priv->RemoveDOMExpandoObject(obj);
    }
    DropJSObjects(aScriptObjectHolder);

    aCache->SetPreservingWrapper(false);
  }
}

// static
void
nsContentUtils::TraceWrapper(nsWrapperCache* aCache, TraceCallback aCallback,
                             void *aClosure)
{
  if (aCache->PreservingWrapper()) {
    JSObject *wrapper = aCache->GetWrapperPreserveColor();
    if (wrapper) {
      aCallback(wrapper, "Preserved wrapper", aClosure);
    }
  }
}

nsresult
nsContentUtils::JSArrayToAtomArray(JSContext* aCx, const JS::Value& aJSArray,
                                   nsCOMArray<nsIAtom>& aRetVal)
{
  JSAutoRequest ar(aCx);
  if (!aJSArray.isObject()) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  
  JSObject* obj = &aJSArray.toObject();
  JSAutoEnterCompartment ac;
  if (!ac.enter(aCx, obj)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  
  PRUint32 length;
  if (!JS_IsArrayObject(aCx, obj) || !JS_GetArrayLength(aCx, obj, &length)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  JSString* str = nsnull;
  JS::Anchor<JSString *> deleteProtector(str);
  for (PRUint32 i = 0; i < length; ++i) {
    jsval v;
    if (!JS_GetElement(aCx, obj, i, &v) ||
        !(str = JS_ValueToString(aCx, v))) {
      return NS_ERROR_ILLEGAL_VALUE;
    }

    nsDependentJSString depStr;
    if (!depStr.init(aCx, str)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    nsCOMPtr<nsIAtom> a = do_GetAtom(depStr);
    if (!a) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    aRetVal.AppendObject(a);
  }
  return NS_OK;
}

// static
nsresult
nsContentUtils::IsOnPrefWhitelist(nsPIDOMWindow* aWindow,
                                  const char* aPrefURL, bool* aAllowed)
{
  // Make sure we're dealing with an inner window.
  nsPIDOMWindow* innerWindow = aWindow->IsInnerWindow() ?
    aWindow :
    aWindow->GetCurrentInnerWindow();
  NS_ENSURE_TRUE(innerWindow, NS_ERROR_FAILURE);

  // Make sure we're being called from a window that we have permission to
  // access.
  if (!nsContentUtils::CanCallerAccess(innerWindow)) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  // Need the document in order to make security decisions.
  nsCOMPtr<nsIDocument> document =
    do_QueryInterface(innerWindow->GetExtantDocument());
  NS_ENSURE_TRUE(document, NS_NOINTERFACE);

  // Do security checks. We assume that chrome is always allowed.
  if (nsContentUtils::IsSystemPrincipal(document->NodePrincipal())) {
    *aAllowed = true;
    return NS_OK;    
  }

  // We also allow a comma seperated list of pages specified by
  // preferences.  
  nsCOMPtr<nsIURI> originalURI;
  nsresult rv =
    document->NodePrincipal()->GetURI(getter_AddRefs(originalURI));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> documentURI;
  rv = originalURI->Clone(getter_AddRefs(documentURI));
  NS_ENSURE_SUCCESS(rv, rv);

  // Strip the query string (if there is one) before comparing.
  nsCOMPtr<nsIURL> documentURL = do_QueryInterface(documentURI);
  if (documentURL) {
    rv = documentURL->SetQuery(EmptyCString());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  bool allowed = false;

  // The pref may not exist but in that case we deny access just as we do if
  // the url doesn't match.
  nsCString whitelist;
  if (NS_SUCCEEDED(Preferences::GetCString(aPrefURL,
                                           &whitelist))) {
    nsCOMPtr<nsIIOService> ios = do_GetIOService();
    NS_ENSURE_TRUE(ios, NS_ERROR_FAILURE);

    nsCCharSeparatedTokenizer tokenizer(whitelist, ',');
    while (tokenizer.hasMoreTokens()) {
      nsCOMPtr<nsIURI> uri;
      if (NS_SUCCEEDED(NS_NewURI(getter_AddRefs(uri), tokenizer.nextToken(),
                                 nsnull, nsnull, ios))) {
        rv = documentURI->EqualsExceptRef(uri, &allowed);
        NS_ENSURE_SUCCESS(rv, rv);

        if (allowed) {
          break;
        }
      }
    }
  }
  *aAllowed = allowed;
  return NS_OK;
}

// static
void
nsContentUtils::GetSelectionInTextControl(Selection* aSelection,
                                          Element* aRoot,
                                          PRInt32& aOutStartOffset,
                                          PRInt32& aOutEndOffset)
{
  MOZ_ASSERT(aSelection && aRoot);

  if (!aSelection->GetRangeCount()) {
    // Nothing selected
    aOutStartOffset = aOutEndOffset = 0;
    return;
  }

  nsCOMPtr<nsINode> anchorNode = aSelection->GetAnchorNode();
  PRInt32 anchorOffset = aSelection->GetAnchorOffset();
  nsCOMPtr<nsINode> focusNode = aSelection->GetFocusNode();
  PRInt32 focusOffset = aSelection->GetFocusOffset();

  // We have at most two children, consisting of an optional text node followed
  // by an optional <br>.
  NS_ASSERTION(aRoot->GetChildCount() <= 2, "Unexpected children");
  nsCOMPtr<nsIContent> firstChild = aRoot->GetFirstChild();
#ifdef DEBUG
  nsCOMPtr<nsIContent> lastChild = aRoot->GetLastChild();
  NS_ASSERTION(anchorNode == aRoot || anchorNode == firstChild ||
               anchorNode == lastChild, "Unexpected anchorNode");
  NS_ASSERTION(focusNode == aRoot || focusNode == firstChild ||
               focusNode == lastChild, "Unexpected focusNode");
#endif
  if (!firstChild || !firstChild->IsNodeOfType(nsINode::eTEXT)) {
    // No text node, so everything is 0
    anchorOffset = focusOffset = 0;
  } else {
    // First child is text.  If the anchor/focus is already in the text node,
    // or the start of the root node, no change needed.  If it's in the root
    // node but not the start, or in the trailing <br>, we need to set the
    // offset to the end.
    if ((anchorNode == aRoot && anchorOffset != 0) ||
        (anchorNode != aRoot && anchorNode != firstChild)) {
      anchorOffset = firstChild->Length();
    }
    if ((focusNode == aRoot && focusOffset != 0) ||
        (focusNode != aRoot && focusNode != firstChild)) {
      focusOffset = firstChild->Length();
    }
  }

  // Make sure aOutStartOffset <= aOutEndOffset.
  aOutStartOffset = NS_MIN(anchorOffset, focusOffset);
  aOutEndOffset = NS_MAX(anchorOffset, focusOffset);
}

nsIEditor*
nsContentUtils::GetHTMLEditor(nsPresContext* aPresContext)
{
  nsCOMPtr<nsISupports> container = aPresContext->GetContainer();
  nsCOMPtr<nsIEditorDocShell> editorDocShell(do_QueryInterface(container));
  bool isEditable;
  if (!editorDocShell ||
      NS_FAILED(editorDocShell->GetEditable(&isEditable)) || !isEditable)
    return nsnull;

  nsCOMPtr<nsIEditor> editor;
  editorDocShell->GetEditor(getter_AddRefs(editor));
  return editor;
}
