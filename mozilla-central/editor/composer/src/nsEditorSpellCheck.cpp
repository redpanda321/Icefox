/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdlib.h>                     // for getenv

#include "mozilla/Attributes.h"         // for MOZ_FINAL
#include "mozilla/Preferences.h"        // for Preferences
#include "mozilla/Services.h"           // for GetXULChromeRegistryService
#include "mozilla/dom/Element.h"        // for Element
#include "mozilla/mozalloc.h"           // for operator delete, etc
#include "nsAString.h"                  // for nsAString_internal::IsEmpty, etc
#include "nsComponentManagerUtils.h"    // for do_CreateInstance
#include "nsDebug.h"                    // for NS_ENSURE_TRUE, etc
#include "nsDependentSubstring.h"       // for Substring
#include "nsEditorSpellCheck.h"
#include "nsError.h"                    // for NS_ERROR_NOT_INITIALIZED, etc
#include "nsIChromeRegistry.h"          // for nsIXULChromeRegistry
#include "nsIContent.h"                 // for nsIContent
#include "nsIContentPrefService.h"      // for nsIContentPrefService, etc
#include "nsIDOMDocument.h"             // for nsIDOMDocument
#include "nsIDOMElement.h"              // for nsIDOMElement
#include "nsIDOMRange.h"                // for nsIDOMRange
#include "nsIDocument.h"                // for nsIDocument
#include "nsIEditor.h"                  // for nsIEditor
#include "nsIHTMLEditor.h"              // for nsIHTMLEditor
#include "nsILoadContext.h"
#include "nsISelection.h"               // for nsISelection
#include "nsISpellChecker.h"            // for nsISpellChecker, etc
#include "nsISupportsBase.h"            // for nsISupports
#include "nsISupportsUtils.h"           // for NS_ADDREF
#include "nsITextServicesDocument.h"    // for nsITextServicesDocument
#include "nsITextServicesFilter.h"      // for nsITextServicesFilter
#include "nsIURI.h"                     // for nsIURI
#include "nsIVariant.h"                 // for nsIWritableVariant, etc
#include "nsLiteralString.h"            // for NS_LITERAL_STRING, etc
#include "nsMemory.h"                   // for nsMemory
#include "nsReadableUtils.h"            // for ToNewUnicode, EmptyString, etc
#include "nsServiceManagerUtils.h"      // for do_GetService
#include "nsString.h"                   // for nsAutoString, nsString, etc
#include "nsStringFwd.h"                // for nsAFlatString
#include "nsStyleUtil.h"                // for nsStyleUtil

using namespace mozilla;

class UpdateDictionnaryHolder {
  private:
    nsEditorSpellCheck* mSpellCheck;
  public:
    UpdateDictionnaryHolder(nsEditorSpellCheck* esc): mSpellCheck(esc) {
      if (mSpellCheck) {
        mSpellCheck->BeginUpdateDictionary();
      }
    }
    ~UpdateDictionnaryHolder() {
      if (mSpellCheck) {
        mSpellCheck->EndUpdateDictionary();
      }
    }
};

#define CPS_PREF_NAME NS_LITERAL_STRING("spellcheck.lang")

class LastDictionary MOZ_FINAL {
public:
  /**
   * Store current dictionary for editor document url. Use content pref
   * service.
   */
  NS_IMETHOD StoreCurrentDictionary(nsIEditor* aEditor, const nsAString& aDictionary);

  /**
   * Get last stored current dictionary for editor document url.
   */
  NS_IMETHOD FetchLastDictionary(nsIEditor* aEditor, nsAString& aDictionary);

  /**
   * Forget last current dictionary stored for editor document url.
   */
  NS_IMETHOD ClearCurrentDictionary(nsIEditor* aEditor);

  /**
   * get uri of editor's document.
   *
   */
  static nsresult GetDocumentURI(nsIEditor* aEditor, nsIURI * *aURI);
};

// static
nsresult
LastDictionary::GetDocumentURI(nsIEditor* aEditor, nsIURI * *aURI)
{
  NS_ENSURE_ARG_POINTER(aEditor);
  NS_ENSURE_ARG_POINTER(aURI);

  nsCOMPtr<nsIDOMDocument> domDoc;
  aEditor->GetDocument(getter_AddRefs(domDoc));
  NS_ENSURE_TRUE(domDoc, NS_ERROR_FAILURE);

  nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);
  NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);

  nsCOMPtr<nsIURI> docUri = doc->GetDocumentURI();
  NS_ENSURE_TRUE(docUri, NS_ERROR_FAILURE);

  *aURI = docUri;
  NS_ADDREF(*aURI);
  return NS_OK;
}

static already_AddRefed<nsILoadContext>
GetLoadContext(nsIEditor* aEditor)
{
  nsCOMPtr<nsIDOMDocument> domDoc;
  aEditor->GetDocument(getter_AddRefs(domDoc));
  NS_ENSURE_TRUE(domDoc, nullptr);

  nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);
  NS_ENSURE_TRUE(doc, nullptr);

  nsCOMPtr<nsISupports> container = doc->GetContainer();
  nsCOMPtr<nsILoadContext> loadContext = do_QueryInterface(container);
  return loadContext.forget();
}

NS_IMETHODIMP
LastDictionary::FetchLastDictionary(nsIEditor* aEditor, nsAString& aDictionary)
{
  NS_ENSURE_ARG_POINTER(aEditor);

  nsresult rv;

  nsCOMPtr<nsIURI> docUri;
  rv = GetDocumentURI(aEditor, getter_AddRefs(docUri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIContentPrefService> contentPrefService =
    do_GetService(NS_CONTENT_PREF_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(contentPrefService, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIWritableVariant> uri = do_CreateInstance(NS_VARIANT_CONTRACTID);
  NS_ENSURE_TRUE(uri, NS_ERROR_OUT_OF_MEMORY);
  uri->SetAsISupports(docUri);

  nsCOMPtr<nsILoadContext> loadContext = GetLoadContext(aEditor);
  bool hasPref;
  if (NS_SUCCEEDED(contentPrefService->HasPref(uri, CPS_PREF_NAME, loadContext, &hasPref)) && hasPref) {
    nsCOMPtr<nsIVariant> pref;
    contentPrefService->GetPref(uri, CPS_PREF_NAME, loadContext, nullptr, getter_AddRefs(pref));
    pref->GetAsAString(aDictionary);
  } else {
    aDictionary.Truncate();
  }

  return NS_OK;
}

NS_IMETHODIMP
LastDictionary::StoreCurrentDictionary(nsIEditor* aEditor, const nsAString& aDictionary)
{
  NS_ENSURE_ARG_POINTER(aEditor);

  nsresult rv;

  nsCOMPtr<nsIURI> docUri;
  rv = GetDocumentURI(aEditor, getter_AddRefs(docUri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIWritableVariant> uri = do_CreateInstance(NS_VARIANT_CONTRACTID);
  NS_ENSURE_TRUE(uri, NS_ERROR_OUT_OF_MEMORY);
  uri->SetAsISupports(docUri);

  nsCOMPtr<nsIWritableVariant> prefValue = do_CreateInstance(NS_VARIANT_CONTRACTID);
  NS_ENSURE_TRUE(prefValue, NS_ERROR_OUT_OF_MEMORY);
  prefValue->SetAsAString(aDictionary);

  nsCOMPtr<nsIContentPrefService> contentPrefService =
    do_GetService(NS_CONTENT_PREF_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(contentPrefService, NS_ERROR_NOT_INITIALIZED);

  nsCOMPtr<nsILoadContext> loadContext = GetLoadContext(aEditor);
  return contentPrefService->SetPref(uri, CPS_PREF_NAME, prefValue, loadContext);
}

NS_IMETHODIMP
LastDictionary::ClearCurrentDictionary(nsIEditor* aEditor)
{
  NS_ENSURE_ARG_POINTER(aEditor);

  nsresult rv;

  nsCOMPtr<nsIURI> docUri;
  rv = GetDocumentURI(aEditor, getter_AddRefs(docUri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIWritableVariant> uri = do_CreateInstance(NS_VARIANT_CONTRACTID);
  NS_ENSURE_TRUE(uri, NS_ERROR_OUT_OF_MEMORY);
  uri->SetAsISupports(docUri);

  nsCOMPtr<nsIContentPrefService> contentPrefService =
    do_GetService(NS_CONTENT_PREF_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(contentPrefService, NS_ERROR_NOT_INITIALIZED);

  nsCOMPtr<nsILoadContext> loadContext = GetLoadContext(aEditor);
  return contentPrefService->RemovePref(uri, CPS_PREF_NAME, loadContext);
}

LastDictionary* nsEditorSpellCheck::gDictionaryStore = nullptr;

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsEditorSpellCheck)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsEditorSpellCheck)

NS_INTERFACE_MAP_BEGIN(nsEditorSpellCheck)
  NS_INTERFACE_MAP_ENTRY(nsIEditorSpellCheck)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIEditorSpellCheck)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(nsEditorSpellCheck)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_3(nsEditorSpellCheck,
                           mEditor,
                           mSpellChecker,
                           mTxtSrvFilter)

nsEditorSpellCheck::nsEditorSpellCheck()
  : mSuggestedWordIndex(0)
  , mDictionaryIndex(0)
  , mEditor(nullptr)
  , mUpdateDictionaryRunning(false)
{
}

nsEditorSpellCheck::~nsEditorSpellCheck()
{
  // Make sure we blow the spellchecker away, just in
  // case it hasn't been destroyed already.
  mSpellChecker = nullptr;
}

// The problem is that if the spell checker does not exist, we can not tell
// which dictionaries are installed. This function works around the problem,
// allowing callers to ask if we can spell check without actually doing so (and
// enabling or disabling UI as necessary). This just creates a spellcheck
// object if needed and asks it for the dictionary list.
NS_IMETHODIMP
nsEditorSpellCheck::CanSpellCheck(bool* _retval)
{
  nsresult rv;
  nsCOMPtr<nsISpellChecker> spellChecker;
  if (! mSpellChecker) {
    spellChecker = do_CreateInstance(NS_SPELLCHECKER_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    spellChecker = mSpellChecker;
  }
  nsTArray<nsString> dictList;
  rv = spellChecker->GetDictionaryList(&dictList);
  NS_ENSURE_SUCCESS(rv, rv);

  *_retval = (dictList.Length() > 0);
  return NS_OK;
}

NS_IMETHODIMP    
nsEditorSpellCheck::InitSpellChecker(nsIEditor* aEditor, bool aEnableSelectionChecking)
{
  NS_ENSURE_TRUE(aEditor, NS_ERROR_NULL_POINTER);
  mEditor = aEditor;

  nsresult rv;

  if (!gDictionaryStore) {
    gDictionaryStore = new LastDictionary();
  }


  // We can spell check with any editor type
  nsCOMPtr<nsITextServicesDocument>tsDoc =
     do_CreateInstance("@mozilla.org/textservices/textservicesdocument;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ENSURE_TRUE(tsDoc, NS_ERROR_NULL_POINTER);

  tsDoc->SetFilter(mTxtSrvFilter);

  // Pass the editor to the text services document
  rv = tsDoc->InitWithEditor(aEditor);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aEnableSelectionChecking) {
    // Find out if the section is collapsed or not.
    // If it isn't, we want to spellcheck just the selection.

    nsCOMPtr<nsISelection> selection;

    rv = aEditor->GetSelection(getter_AddRefs(selection));
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(selection, NS_ERROR_FAILURE);

    int32_t count = 0;

    rv = selection->GetRangeCount(&count);
    NS_ENSURE_SUCCESS(rv, rv);

    if (count > 0) {
      nsCOMPtr<nsIDOMRange> range;

      rv = selection->GetRangeAt(0, getter_AddRefs(range));
      NS_ENSURE_SUCCESS(rv, rv);

      bool collapsed = false;
      rv = range->GetCollapsed(&collapsed);
      NS_ENSURE_SUCCESS(rv, rv);

      if (!collapsed) {
        // We don't want to touch the range in the selection,
        // so create a new copy of it.

        nsCOMPtr<nsIDOMRange> rangeBounds;
        rv =  range->CloneRange(getter_AddRefs(rangeBounds));
        NS_ENSURE_SUCCESS(rv, rv);
        NS_ENSURE_TRUE(rangeBounds, NS_ERROR_FAILURE);

        // Make sure the new range spans complete words.

        rv = tsDoc->ExpandRangeToWordBoundaries(rangeBounds);
        NS_ENSURE_SUCCESS(rv, rv);

        // Now tell the text services that you only want
        // to iterate over the text in this range.

        rv = tsDoc->SetExtent(rangeBounds);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  }

  mSpellChecker = do_CreateInstance(NS_SPELLCHECKER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NULL_POINTER);

  rv = mSpellChecker->SetDocument(tsDoc, true);
  NS_ENSURE_SUCCESS(rv, rv);

  // do not fail if UpdateCurrentDictionary fails because this method may
  // succeed later.
  UpdateCurrentDictionary();
  return NS_OK;
}

NS_IMETHODIMP    
nsEditorSpellCheck::GetNextMisspelledWord(PRUnichar **aNextMisspelledWord)
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  nsAutoString nextMisspelledWord;
  
  DeleteSuggestedWordList();
  // Beware! This may flush notifications via synchronous
  // ScrollSelectionIntoView.
  nsresult rv = mSpellChecker->NextMisspelledWord(nextMisspelledWord,
                                                  &mSuggestedWordList);

  *aNextMisspelledWord = ToNewUnicode(nextMisspelledWord);
  return rv;
}

NS_IMETHODIMP    
nsEditorSpellCheck::GetSuggestedWord(PRUnichar **aSuggestedWord)
{
  nsAutoString word;
  if ( mSuggestedWordIndex < int32_t(mSuggestedWordList.Length()))
  {
    *aSuggestedWord = ToNewUnicode(mSuggestedWordList[mSuggestedWordIndex]);
    mSuggestedWordIndex++;
  } else {
    // A blank string signals that there are no more strings
    *aSuggestedWord = ToNewUnicode(EmptyString());
  }
  return NS_OK;
}

NS_IMETHODIMP    
nsEditorSpellCheck::CheckCurrentWord(const PRUnichar *aSuggestedWord,
                                     bool *aIsMisspelled)
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  DeleteSuggestedWordList();
  return mSpellChecker->CheckWord(nsDependentString(aSuggestedWord),
                                  aIsMisspelled, &mSuggestedWordList);
}

NS_IMETHODIMP    
nsEditorSpellCheck::CheckCurrentWordNoSuggest(const PRUnichar *aSuggestedWord,
                                              bool *aIsMisspelled)
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  return mSpellChecker->CheckWord(nsDependentString(aSuggestedWord),
                                  aIsMisspelled, nullptr);
}

NS_IMETHODIMP    
nsEditorSpellCheck::ReplaceWord(const PRUnichar *aMisspelledWord,
                                const PRUnichar *aReplaceWord,
                                bool             allOccurrences)
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  return mSpellChecker->Replace(nsDependentString(aMisspelledWord),
                                nsDependentString(aReplaceWord), allOccurrences);
}

NS_IMETHODIMP    
nsEditorSpellCheck::IgnoreWordAllOccurrences(const PRUnichar *aWord)
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  return mSpellChecker->IgnoreAll(nsDependentString(aWord));
}

NS_IMETHODIMP    
nsEditorSpellCheck::GetPersonalDictionary()
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

   // We can spell check with any editor type
  mDictionaryList.Clear();
  mDictionaryIndex = 0;
  return mSpellChecker->GetPersonalDictionary(&mDictionaryList);
}

NS_IMETHODIMP    
nsEditorSpellCheck::GetPersonalDictionaryWord(PRUnichar **aDictionaryWord)
{
  if ( mDictionaryIndex < int32_t( mDictionaryList.Length()))
  {
    *aDictionaryWord = ToNewUnicode(mDictionaryList[mDictionaryIndex]);
    mDictionaryIndex++;
  } else {
    // A blank string signals that there are no more strings
    *aDictionaryWord = ToNewUnicode(EmptyString());
  }

  return NS_OK;
}

NS_IMETHODIMP    
nsEditorSpellCheck::AddWordToDictionary(const PRUnichar *aWord)
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  return mSpellChecker->AddWordToPersonalDictionary(nsDependentString(aWord));
}

NS_IMETHODIMP    
nsEditorSpellCheck::RemoveWordFromDictionary(const PRUnichar *aWord)
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  return mSpellChecker->RemoveWordFromPersonalDictionary(nsDependentString(aWord));
}

NS_IMETHODIMP    
nsEditorSpellCheck::GetDictionaryList(PRUnichar ***aDictionaryList, uint32_t *aCount)
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  NS_ENSURE_TRUE(aDictionaryList && aCount, NS_ERROR_NULL_POINTER);

  *aDictionaryList = 0;
  *aCount          = 0;

  nsTArray<nsString> dictList;

  nsresult rv = mSpellChecker->GetDictionaryList(&dictList);

  NS_ENSURE_SUCCESS(rv, rv);

  PRUnichar **tmpPtr = 0;

  if (dictList.Length() < 1)
  {
    // If there are no dictionaries, return an array containing
    // one element and a count of one.

    tmpPtr = (PRUnichar **)nsMemory::Alloc(sizeof(PRUnichar *));

    NS_ENSURE_TRUE(tmpPtr, NS_ERROR_OUT_OF_MEMORY);

    *tmpPtr          = 0;
    *aDictionaryList = tmpPtr;
    *aCount          = 0;

    return NS_OK;
  }

  tmpPtr = (PRUnichar **)nsMemory::Alloc(sizeof(PRUnichar *) * dictList.Length());

  NS_ENSURE_TRUE(tmpPtr, NS_ERROR_OUT_OF_MEMORY);

  *aDictionaryList = tmpPtr;
  *aCount          = dictList.Length();

  uint32_t i;

  for (i = 0; i < *aCount; i++)
  {
    tmpPtr[i] = ToNewUnicode(dictList[i]);
  }

  return rv;
}

NS_IMETHODIMP    
nsEditorSpellCheck::GetCurrentDictionary(nsAString& aDictionary)
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  return mSpellChecker->GetCurrentDictionary(aDictionary);
}

NS_IMETHODIMP    
nsEditorSpellCheck::SetCurrentDictionary(const nsAString& aDictionary)
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  nsRefPtr<nsEditorSpellCheck> kungFuDeathGrip = this;

  if (!mUpdateDictionaryRunning) {

    nsDefaultStringComparator comparator;
    nsAutoString langCode;
    int32_t dashIdx = aDictionary.FindChar('-');
    if (dashIdx != -1) {
      langCode.Assign(Substring(aDictionary, 0, dashIdx));
    } else {
      langCode.Assign(aDictionary);
    }

    if (mPreferredLang.IsEmpty() || !nsStyleUtil::DashMatchCompare(mPreferredLang, langCode, comparator)) {
      // When user sets dictionary manually, we store this value associated
      // with editor url.
      gDictionaryStore->StoreCurrentDictionary(mEditor, aDictionary);
    } else {
      // If user sets a dictionary matching (even partially), lang defined by
      // document, we consider content pref has been canceled, and we clear it.
      gDictionaryStore->ClearCurrentDictionary(mEditor);
    }

    // Also store it in as a preference. It will be used as a default value
    // when everything else fails.
    Preferences::SetString("spellchecker.dictionary", aDictionary);
  }
  return mSpellChecker->SetCurrentDictionary(aDictionary);
}

NS_IMETHODIMP
nsEditorSpellCheck::CheckCurrentDictionary()
{
  mSpellChecker->CheckCurrentDictionary();

  // Check if our current dictionary is still available.
  nsAutoString currentDictionary;
  nsresult rv = GetCurrentDictionary(currentDictionary);
  if (NS_SUCCEEDED(rv) && !currentDictionary.IsEmpty()) {
    return NS_OK;
  }

  // If our preferred current dictionary has gone, pick another one.
  nsTArray<nsString> dictList;
  rv = mSpellChecker->GetDictionaryList(&dictList);
  NS_ENSURE_SUCCESS(rv, rv);

  if (dictList.Length() > 0) {
    rv = SetCurrentDictionary(dictList[0]);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP    
nsEditorSpellCheck::UninitSpellChecker()
{
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  // Cleanup - kill the spell checker
  DeleteSuggestedWordList();
  mDictionaryList.Clear();
  mDictionaryIndex = 0;
  mSpellChecker = 0;
  return NS_OK;
}


/* void setFilter (in nsITextServicesFilter filter); */
NS_IMETHODIMP 
nsEditorSpellCheck::SetFilter(nsITextServicesFilter *filter)
{
  mTxtSrvFilter = filter;
  return NS_OK;
}

nsresult    
nsEditorSpellCheck::DeleteSuggestedWordList()
{
  mSuggestedWordList.Clear();
  mSuggestedWordIndex = 0;
  return NS_OK;
}

NS_IMETHODIMP
nsEditorSpellCheck::UpdateCurrentDictionary()
{
  nsresult rv;

  nsRefPtr<nsEditorSpellCheck> kungFuDeathGrip = this;

  UpdateDictionnaryHolder holder(this);

  // Get language with html5 algorithm
  nsCOMPtr<nsIContent> rootContent;
  nsCOMPtr<nsIHTMLEditor> htmlEditor = do_QueryInterface(mEditor);
  if (htmlEditor) {
    rootContent = htmlEditor->GetActiveEditingHost();
  } else {
    nsCOMPtr<nsIDOMElement> rootElement;
    rv = mEditor->GetRootElement(getter_AddRefs(rootElement));
    NS_ENSURE_SUCCESS(rv, rv);
    rootContent = do_QueryInterface(rootElement);
  }
  NS_ENSURE_TRUE(rootContent, NS_ERROR_FAILURE);

  mPreferredLang.Truncate();
  rootContent->GetLang(mPreferredLang);

  // Tell the spellchecker what dictionary to use:

  // First try to get dictionary from content prefs. If we have one, do not got
  // further. Use this exact dictionary.
  nsAutoString dictName;
  rv = gDictionaryStore->FetchLastDictionary(mEditor, dictName);
  if (NS_SUCCEEDED(rv) && !dictName.IsEmpty()) {
    if (NS_FAILED(SetCurrentDictionary(dictName))) { 
      // may be dictionary was uninstalled ?
      gDictionaryStore->ClearCurrentDictionary(mEditor);
    }
    return NS_OK;
  }

  if (mPreferredLang.IsEmpty()) {
    nsCOMPtr<nsIDocument> doc = rootContent->GetCurrentDoc();
    NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);
    doc->GetContentLanguage(mPreferredLang);
  }

  // Then, try to use language computed from element
  if (!mPreferredLang.IsEmpty()) {
    dictName.Assign(mPreferredLang);
  }

  // otherwise, get language from preferences
  nsAutoString preferedDict(Preferences::GetLocalizedString("spellchecker.dictionary"));
  if (dictName.IsEmpty()) {
    dictName.Assign(preferedDict);
  }

  if (dictName.IsEmpty())
  {
    // Prefs didn't give us a dictionary name, so just get the current
    // locale and use that as the default dictionary name!

    nsCOMPtr<nsIXULChromeRegistry> packageRegistry =
      mozilla::services::GetXULChromeRegistryService();

    if (packageRegistry) {
      nsAutoCString utf8DictName;
      rv = packageRegistry->GetSelectedLocale(NS_LITERAL_CSTRING("global"),
                                              utf8DictName);
      AppendUTF8toUTF16(utf8DictName, dictName);
    }
  }

  if (NS_SUCCEEDED(rv) && !dictName.IsEmpty()) {
    rv = SetCurrentDictionary(dictName);
    if (NS_FAILED(rv)) {
      // required dictionary was not available. Try to get a dictionary
      // matching at least language part of dictName: 

      nsAutoString langCode;
      int32_t dashIdx = dictName.FindChar('-');
      if (dashIdx != -1) {
        langCode.Assign(Substring(dictName, 0, dashIdx));
      } else {
        langCode.Assign(dictName);
      }

      nsDefaultStringComparator comparator;

      // try dictionary.spellchecker preference if it starts with langCode (and
      // if we haven't tried it already)
      if (!preferedDict.IsEmpty() && !dictName.Equals(preferedDict) && 
          nsStyleUtil::DashMatchCompare(preferedDict, langCode, comparator)) {
        rv = SetCurrentDictionary(preferedDict);
      }

      // Otherwise, try langCode (if we haven't tried it already)
      if (NS_FAILED(rv)) {
        if (!dictName.Equals(langCode) && !preferedDict.Equals(langCode)) {
          rv = SetCurrentDictionary(langCode);
        }
      }

      // Otherwise, try any available dictionary aa-XX
      if (NS_FAILED(rv)) {
        // loop over avaible dictionaries; if we find one with required
        // language, use it
        nsTArray<nsString> dictList;
        rv = mSpellChecker->GetDictionaryList(&dictList);
        NS_ENSURE_SUCCESS(rv, rv);
        int32_t i, count = dictList.Length();
        for (i = 0; i < count; i++) {
          nsAutoString dictStr(dictList.ElementAt(i));

          if (dictStr.Equals(dictName) ||
              dictStr.Equals(preferedDict) ||
              dictStr.Equals(langCode)) {
            // We have already tried it
            continue;
          }

          if (nsStyleUtil::DashMatchCompare(dictStr, langCode, comparator) &&
              NS_SUCCEEDED(SetCurrentDictionary(dictStr))) {
              break;
          }
        }
      }
    }
  }

  // If we have not set dictionary, and the editable element doesn't have a
  // lang attribute, we try to get a dictionary. First try LANG environment variable,
  // then en-US. If it does not work, pick the first one.
  if (mPreferredLang.IsEmpty()) {
    nsAutoString currentDictionary;
    rv = GetCurrentDictionary(currentDictionary);
    if (NS_FAILED(rv) || currentDictionary.IsEmpty()) {
      // Try to get current dictionary from environment variable LANG
      char* env_lang = getenv("LANG");
      if (env_lang != nullptr) {
        nsString lang = NS_ConvertUTF8toUTF16(env_lang);
        // Strip trailing charset if there is any
        int32_t dot_pos = lang.FindChar('.');
        if (dot_pos != -1) {
          lang = Substring(lang, 0, dot_pos - 1);
        }
        rv = SetCurrentDictionary(lang);
      }
      if (NS_FAILED(rv)) {
        rv = SetCurrentDictionary(NS_LITERAL_STRING("en-US"));
        if (NS_FAILED(rv)) {
          nsTArray<nsString> dictList;
          rv = mSpellChecker->GetDictionaryList(&dictList);
          if (NS_SUCCEEDED(rv) && dictList.Length() > 0) {
            SetCurrentDictionary(dictList[0]);
          }
        }
      }
    }
  }

  // If an error was thrown while setting the dictionary, just
  // fail silently so that the spellchecker dialog is allowed to come
  // up. The user can manually reset the language to their choice on
  // the dialog if it is wrong.

  DeleteSuggestedWordList();

  return NS_OK;
}

void 
nsEditorSpellCheck::ShutDown() {
  delete gDictionaryStore;
}
