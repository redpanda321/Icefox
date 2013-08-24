/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for all our document implementations.
 */

#ifndef nsDocument_h___
#define nsDocument_h___

#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsCRT.h"
#include "nsIDocument.h"
#include "nsWeakReference.h"
#include "nsWeakPtr.h"
#include "nsVoidArray.h"
#include "nsTArray.h"
#include "nsIDOMXMLDocument.h"
#include "nsIDOMDocumentXBL.h"
#include "nsStubDocumentObserver.h"
#include "nsIDOMStyleSheetList.h"
#include "nsIScriptGlobalObject.h"
#include "nsIDOMEventTarget.h"
#include "nsIContent.h"
#include "nsEventListenerManager.h"
#include "nsIDOMNodeSelector.h"
#include "nsIPrincipal.h"
#include "nsIParser.h"
#include "nsBindingManager.h"
#include "nsINodeInfo.h"
#include "nsHashtable.h"
#include "nsInterfaceHashtable.h"
#include "nsIBoxObject.h"
#include "nsPIBoxObject.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIURI.h"
#include "nsScriptLoader.h"
#include "nsIRadioGroupContainer.h"
#include "nsILayoutHistoryState.h"
#include "nsIRequest.h"
#include "nsILoadGroup.h"
#include "nsTObserverArray.h"
#include "nsStubMutationObserver.h"
#include "nsIChannel.h"
#include "nsCycleCollectionParticipant.h"
#include "nsContentList.h"
#include "nsGkAtoms.h"
#include "nsIApplicationCache.h"
#include "nsIApplicationCacheContainer.h"
#include "nsStyleSet.h"
#include "pldhash.h"
#include "nsAttrAndChildArray.h"
#include "nsDOMAttributeMap.h"
#include "nsThreadUtils.h"
#include "nsIContentViewer.h"
#include "nsIDOMXPathNSResolver.h"
#include "nsIInterfaceRequestor.h"
#include "nsILoadContext.h"
#include "nsIProgressEventSink.h"
#include "nsISecurityEventSink.h"
#include "nsIChannelEventSink.h"
#include "imgIRequest.h"
#include "nsIDOMDOMImplementation.h"
#include "nsIDOMTouchEvent.h"
#include "nsIInlineEventHandlers.h"
#include "nsDataHashtable.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Attributes.h"

#define XML_DECLARATION_BITS_DECLARATION_EXISTS   (1 << 0)
#define XML_DECLARATION_BITS_ENCODING_EXISTS      (1 << 1)
#define XML_DECLARATION_BITS_STANDALONE_EXISTS    (1 << 2)
#define XML_DECLARATION_BITS_STANDALONE_YES       (1 << 3)


class nsEventListenerManager;
class nsDOMStyleSheetList;
class nsDOMStyleSheetSetList;
class nsIOutputStream;
class nsDocument;
class nsIDTD;
class nsIRadioVisitor;
class nsIFormControl;
struct nsRadioGroupStruct;
class nsOnloadBlocker;
class nsUnblockOnloadEvent;
class nsChildContentList;
class nsXMLEventsManager;
class nsHTMLStyleSheet;
class nsHTMLCSSStyleSheet;
class nsDOMNavigationTiming;
class nsWindowSizes;
class nsHtml5TreeOpExecutor;

/**
 * Right now our identifier map entries contain information for 'name'
 * and 'id' mappings of a given string. This is so that
 * nsHTMLDocument::ResolveName only has to do one hash lookup instead
 * of two. It's not clear whether this still matters for performance.
 * 
 * We also store the document.all result list here. This is mainly so that
 * when all elements with the given ID are removed and we remove
 * the ID's nsIdentifierMapEntry, the document.all result is released too.
 * Perhaps the document.all results should have their own hashtable
 * in nsHTMLDocument.
 */
class nsIdentifierMapEntry : public nsStringHashKey
{
public:
  typedef mozilla::dom::Element Element;
  
  nsIdentifierMapEntry(const nsAString& aKey) :
    nsStringHashKey(&aKey), mNameContentList(nsnull)
  {
  }
  nsIdentifierMapEntry(const nsAString *aKey) :
    nsStringHashKey(aKey), mNameContentList(nsnull)
  {
  }
  nsIdentifierMapEntry(const nsIdentifierMapEntry& aOther) :
    nsStringHashKey(&aOther.GetKey())
  {
    NS_ERROR("Should never be called");
  }
  ~nsIdentifierMapEntry();

  void SetInvalidName();
  bool IsInvalidName();
  void AddNameElement(nsIDocument* aDocument, Element* aElement);
  void RemoveNameElement(Element* aElement);
  bool IsEmpty();
  nsBaseContentList* GetNameContentList() {
    return mNameContentList;
  }

  /**
   * Returns the element if we know the element associated with this
   * id. Otherwise returns null.
   */
  Element* GetIdElement();
  /**
   * Returns the list of all elements associated with this id.
   */
  const nsSmallVoidArray* GetIdElements() const {
    return &mIdContentList;
  }
  /**
   * If this entry has a non-null image element set (using SetImageElement),
   * the image element will be returned, otherwise the same as GetIdElement().
   */
  Element* GetImageIdElement();
  /**
   * Append all the elements with this id to aElements
   */
  void AppendAllIdContent(nsCOMArray<nsIContent>* aElements);
  /**
   * This can fire ID change callbacks.
   * @return true if the content could be added, false if we failed due
   * to OOM.
   */
  bool AddIdElement(Element* aElement);
  /**
   * This can fire ID change callbacks.
   */
  void RemoveIdElement(Element* aElement);
  /**
   * Set the image element override for this ID. This will be returned by
   * GetIdElement(true) if non-null.
   */
  void SetImageElement(Element* aElement);

  bool HasContentChangeCallback() { return mChangeCallbacks != nsnull; }
  void AddContentChangeCallback(nsIDocument::IDTargetObserver aCallback,
                                void* aData, bool aForImage);
  void RemoveContentChangeCallback(nsIDocument::IDTargetObserver aCallback,
                                void* aData, bool aForImage);

  void Traverse(nsCycleCollectionTraversalCallback* aCallback);

  void SetDocAllList(nsContentList* aContentList) { mDocAllList = aContentList; }
  nsContentList* GetDocAllList() { return mDocAllList; }

  struct ChangeCallback {
    nsIDocument::IDTargetObserver mCallback;
    void* mData;
    bool mForImage;
  };

  struct ChangeCallbackEntry : public PLDHashEntryHdr {
    typedef const ChangeCallback KeyType;
    typedef const ChangeCallback* KeyTypePointer;

    ChangeCallbackEntry(const ChangeCallback* key) :
      mKey(*key) { }
    ChangeCallbackEntry(const ChangeCallbackEntry& toCopy) :
      mKey(toCopy.mKey) { }

    KeyType GetKey() const { return mKey; }
    bool KeyEquals(KeyTypePointer aKey) const {
      return aKey->mCallback == mKey.mCallback &&
             aKey->mData == mKey.mData &&
             aKey->mForImage == mKey.mForImage;
    }

    static KeyTypePointer KeyToPointer(KeyType& aKey) { return &aKey; }
    static PLDHashNumber HashKey(KeyTypePointer aKey)
    {
      return mozilla::HashGeneric(aKey->mCallback, aKey->mData);
    }
    enum { ALLOW_MEMMOVE = true };
    
    ChangeCallback mKey;
  };

  static size_t SizeOfExcludingThis(nsIdentifierMapEntry* aEntry,
                                    nsMallocSizeOfFun aMallocSizeOf,
                                    void* aArg);

private:
  void FireChangeCallbacks(Element* aOldElement, Element* aNewElement,
                           bool aImageOnly = false);

  // empty if there are no elements with this ID.
  // The elements are stored as weak pointers.
  nsSmallVoidArray mIdContentList;
  nsRefPtr<nsBaseContentList> mNameContentList;
  nsRefPtr<nsContentList> mDocAllList;
  nsAutoPtr<nsTHashtable<ChangeCallbackEntry> > mChangeCallbacks;
  nsRefPtr<Element> mImageElement;
};

class nsDocHeaderData
{
public:
  nsDocHeaderData(nsIAtom* aField, const nsAString& aData)
    : mField(aField), mData(aData), mNext(nsnull)
  {
  }

  ~nsDocHeaderData(void)
  {
    delete mNext;
  }

  nsCOMPtr<nsIAtom> mField;
  nsString          mData;
  nsDocHeaderData*  mNext;
};

class nsDOMStyleSheetList : public nsIDOMStyleSheetList,
                            public nsStubDocumentObserver
{
public:
  nsDOMStyleSheetList(nsIDocument *aDocument);
  virtual ~nsDOMStyleSheetList();

  NS_DECL_ISUPPORTS

  NS_DECL_NSIDOMSTYLESHEETLIST

  // nsIDocumentObserver
  NS_DECL_NSIDOCUMENTOBSERVER_STYLESHEETADDED
  NS_DECL_NSIDOCUMENTOBSERVER_STYLESHEETREMOVED

  // nsIMutationObserver
  NS_DECL_NSIMUTATIONOBSERVER_NODEWILLBEDESTROYED

  nsIStyleSheet* GetItemAt(PRUint32 aIndex);

  static nsDOMStyleSheetList* FromSupports(nsISupports* aSupports)
  {
    nsIDOMStyleSheetList* list = static_cast<nsIDOMStyleSheetList*>(aSupports);
#ifdef DEBUG
    {
      nsCOMPtr<nsIDOMStyleSheetList> list_qi = do_QueryInterface(aSupports);

      // If this assertion fires the QI implementation for the object in
      // question doesn't use the nsIDOMStyleSheetList pointer as the
      // nsISupports pointer. That must be fixed, or we'll crash...
      NS_ASSERTION(list_qi == list, "Uh, fix QI!");
    }
#endif
    return static_cast<nsDOMStyleSheetList*>(list);
  }

protected:
  PRInt32       mLength;
  nsIDocument*  mDocument;
};

class nsOnloadBlocker MOZ_FINAL : public nsIRequest
{
public:
  nsOnloadBlocker() {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUEST

private:
  ~nsOnloadBlocker() {}
};

class nsExternalResourceMap
{
public:
  typedef nsIDocument::ExternalResourceLoad ExternalResourceLoad;
  nsExternalResourceMap();

  /**
   * Request an external resource document.  This does exactly what
   * nsIDocument::RequestExternalResource is documented to do.
   */
  nsIDocument* RequestResource(nsIURI* aURI,
                               nsINode* aRequestingNode,
                               nsDocument* aDisplayDocument,
                               ExternalResourceLoad** aPendingLoad);

  /**
   * Enumerate the resource documents.  See
   * nsIDocument::EnumerateExternalResources.
   */
  void EnumerateResources(nsIDocument::nsSubDocEnumFunc aCallback, void* aData);

  /**
   * Traverse ourselves for cycle-collection
   */
  void Traverse(nsCycleCollectionTraversalCallback* aCallback) const;

  /**
   * Shut ourselves down (used for cycle-collection unlink), as well
   * as for document destruction.
   */
  void Shutdown()
  {
    mPendingLoads.Clear();
    mMap.Clear();
    mHaveShutDown = true;
  }

  bool HaveShutDown() const
  {
    return mHaveShutDown;
  }

  // Needs to be public so we can traverse them sanely
  struct ExternalResource
  {
    ~ExternalResource();
    nsCOMPtr<nsIDocument> mDocument;
    nsCOMPtr<nsIContentViewer> mViewer;
    nsCOMPtr<nsILoadGroup> mLoadGroup;
  };

  // Hide all our viewers
  void HideViewers();

  // Show all our viewers
  void ShowViewers();

protected:
  class PendingLoad : public ExternalResourceLoad,
                      public nsIStreamListener
  {
  public:
    PendingLoad(nsDocument* aDisplayDocument) :
      mDisplayDocument(aDisplayDocument)
    {}

    NS_DECL_ISUPPORTS
    NS_DECL_NSISTREAMLISTENER
    NS_DECL_NSIREQUESTOBSERVER

    /**
     * Start aURI loading.  This will perform the necessary security checks and
     * so forth.
     */
    nsresult StartLoad(nsIURI* aURI, nsINode* aRequestingNode);

    /**
     * Set up an nsIContentViewer based on aRequest.  This is guaranteed to
     * put null in *aViewer and *aLoadGroup on all failures.
     */
    nsresult SetupViewer(nsIRequest* aRequest, nsIContentViewer** aViewer,
                         nsILoadGroup** aLoadGroup);

  private:
    nsRefPtr<nsDocument> mDisplayDocument;
    nsCOMPtr<nsIStreamListener> mTargetListener;
    nsCOMPtr<nsIURI> mURI;
  };
  friend class PendingLoad;

  class LoadgroupCallbacks MOZ_FINAL : public nsIInterfaceRequestor
  {
  public:
    LoadgroupCallbacks(nsIInterfaceRequestor* aOtherCallbacks)
      : mCallbacks(aOtherCallbacks)
    {}
    NS_DECL_ISUPPORTS
    NS_DECL_NSIINTERFACEREQUESTOR
  private:
    // The only reason it's safe to hold a strong ref here without leaking is
    // that the notificationCallbacks on a loadgroup aren't the docshell itself
    // but a shim that holds a weak reference to the docshell.
    nsCOMPtr<nsIInterfaceRequestor> mCallbacks;

    // Use shims for interfaces that docshell implements directly so that we
    // don't hand out references to the docshell.  The shims should all allow
    // getInterface back on us, but other than that each one should only
    // implement one interface.
    
    // XXXbz I wish we could just derive the _allcaps thing from _i
#define DECL_SHIM(_i, _allcaps)                                              \
    class _i##Shim MOZ_FINAL : public nsIInterfaceRequestor,                 \
                               public _i                                     \
    {                                                                        \
    public:                                                                  \
      _i##Shim(nsIInterfaceRequestor* aIfreq, _i* aRealPtr)                  \
        : mIfReq(aIfreq), mRealPtr(aRealPtr)                                 \
      {                                                                      \
        NS_ASSERTION(mIfReq, "Expected non-null here");                      \
        NS_ASSERTION(mRealPtr, "Expected non-null here");                    \
      }                                                                      \
      NS_DECL_ISUPPORTS                                                      \
      NS_FORWARD_NSIINTERFACEREQUESTOR(mIfReq->);                            \
      NS_FORWARD_##_allcaps(mRealPtr->);                                     \
    private:                                                                 \
      nsCOMPtr<nsIInterfaceRequestor> mIfReq;                                \
      nsCOMPtr<_i> mRealPtr;                                                 \
    };

    DECL_SHIM(nsILoadContext, NSILOADCONTEXT)
    DECL_SHIM(nsIProgressEventSink, NSIPROGRESSEVENTSINK)
    DECL_SHIM(nsIChannelEventSink, NSICHANNELEVENTSINK)
    DECL_SHIM(nsISecurityEventSink, NSISECURITYEVENTSINK)
    DECL_SHIM(nsIApplicationCacheContainer, NSIAPPLICATIONCACHECONTAINER)
#undef DECL_SHIM
  };
  
  /**
   * Add an ExternalResource for aURI.  aViewer and aLoadGroup might be null
   * when this is called if the URI didn't result in an XML document.  This
   * function makes sure to remove the pending load for aURI, if any, from our
   * hashtable, and to notify its observers, if any.
   */
  nsresult AddExternalResource(nsIURI* aURI, nsIContentViewer* aViewer,
                               nsILoadGroup* aLoadGroup,
                               nsIDocument* aDisplayDocument);
  
  nsClassHashtable<nsURIHashKey, ExternalResource> mMap;
  nsRefPtrHashtable<nsURIHashKey, PendingLoad> mPendingLoads;
  bool mHaveShutDown;
};

// Base class for our document implementations.
//
// Note that this class *implements* nsIDOMXMLDocument, but it's not
// really an nsIDOMXMLDocument. The reason for implementing
// nsIDOMXMLDocument on this class is to avoid having to duplicate all
// its inherited methods on document classes that *are*
// nsIDOMXMLDocument's. nsDocument's QI should *not* claim to support
// nsIDOMXMLDocument unless someone writes a real implementation of
// the interface.
class nsDocument : public nsIDocument,
                   public nsIDOMXMLDocument, // inherits nsIDOMDocument
                   public nsIDOMDocumentXBL,
                   public nsSupportsWeakReference,
                   public nsIScriptObjectPrincipal,
                   public nsIRadioGroupContainer,
                   public nsIApplicationCacheContainer,
                   public nsStubMutationObserver,
                   public nsIDOMDocumentTouch,
                   public nsIInlineEventHandlers,
                   public nsIObserver
{
public:
  typedef mozilla::dom::Element Element;

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_DECL_SIZEOF_EXCLUDING_THIS

  virtual void Reset(nsIChannel *aChannel, nsILoadGroup *aLoadGroup);
  virtual void ResetToURI(nsIURI *aURI, nsILoadGroup *aLoadGroup,
                          nsIPrincipal* aPrincipal);

  // StartDocumentLoad is pure virtual so that subclasses must override it.
  // The nsDocument StartDocumentLoad does some setup, but does NOT set
  // *aDocListener; this is the job of subclasses.
  virtual nsresult StartDocumentLoad(const char* aCommand,
                                     nsIChannel* aChannel,
                                     nsILoadGroup* aLoadGroup,
                                     nsISupports* aContainer,
                                     nsIStreamListener **aDocListener,
                                     bool aReset = true,
                                     nsIContentSink* aContentSink = nsnull) = 0;

  virtual void StopDocumentLoad();

  virtual void NotifyPossibleTitleChange(bool aBoundTitleElement);

  virtual void SetDocumentURI(nsIURI* aURI);

  /**
   * Set the principal responsible for this document.
   */
  virtual void SetPrincipal(nsIPrincipal *aPrincipal);

  /**
   * Get the Content-Type of this document.
   */
  // NS_IMETHOD GetContentType(nsAString& aContentType);
  // Already declared in nsIDOMDocument

  /**
   * Set the Content-Type of this document.
   */
  virtual void SetContentType(const nsAString& aContentType);

  virtual nsresult SetBaseURI(nsIURI* aURI);

  /**
   * Get/Set the base target of a link in a document.
   */
  virtual void GetBaseTarget(nsAString &aBaseTarget);

  /**
   * Return a standard name for the document's character set. This will
   * trigger a startDocumentLoad if necessary to answer the question.
   */
  virtual void SetDocumentCharacterSet(const nsACString& aCharSetID);

  /**
   * Add an observer that gets notified whenever the charset changes.
   */
  virtual nsresult AddCharSetObserver(nsIObserver* aObserver);

  /**
   * Remove a charset observer.
   */
  virtual void RemoveCharSetObserver(nsIObserver* aObserver);

  virtual Element* AddIDTargetObserver(nsIAtom* aID, IDTargetObserver aObserver,
                                       void* aData, bool aForImage);
  virtual void RemoveIDTargetObserver(nsIAtom* aID, IDTargetObserver aObserver,
                                      void* aData, bool aForImage);

  /**
   * Access HTTP header data (this may also get set from other sources, like
   * HTML META tags).
   */
  virtual void GetHeaderData(nsIAtom* aHeaderField, nsAString& aData) const;
  virtual void SetHeaderData(nsIAtom* aheaderField,
                             const nsAString& aData);

  /**
   * Create a new presentation shell that will use aContext for
   * its presentation context (presentation context's <b>must not</b> be
   * shared among multiple presentation shell's).
   */
  virtual nsresult CreateShell(nsPresContext* aContext,
                               nsIViewManager* aViewManager,
                               nsStyleSet* aStyleSet,
                               nsIPresShell** aInstancePtrResult);
  virtual void DeleteShell();

  virtual nsresult SetSubDocumentFor(Element* aContent,
                                     nsIDocument* aSubDoc);
  virtual nsIDocument* GetSubDocumentFor(nsIContent* aContent) const;
  virtual Element* FindContentForSubDocument(nsIDocument *aDocument) const;
  virtual Element* GetRootElementInternal() const;

  /**
   * Get the style sheets owned by this document.
   * These are ordered, highest priority last
   */
  virtual PRInt32 GetNumberOfStyleSheets() const;
  virtual nsIStyleSheet* GetStyleSheetAt(PRInt32 aIndex) const;
  virtual PRInt32 GetIndexOfStyleSheet(nsIStyleSheet* aSheet) const;
  virtual void AddStyleSheet(nsIStyleSheet* aSheet);
  virtual void RemoveStyleSheet(nsIStyleSheet* aSheet);

  virtual void UpdateStyleSheets(nsCOMArray<nsIStyleSheet>& aOldSheets,
                                 nsCOMArray<nsIStyleSheet>& aNewSheets);
  virtual void AddStyleSheetToStyleSets(nsIStyleSheet* aSheet);
  virtual void RemoveStyleSheetFromStyleSets(nsIStyleSheet* aSheet);

  virtual void InsertStyleSheetAt(nsIStyleSheet* aSheet, PRInt32 aIndex);
  virtual void SetStyleSheetApplicableState(nsIStyleSheet* aSheet,
                                            bool aApplicable);

  virtual PRInt32 GetNumberOfCatalogStyleSheets() const;
  virtual nsIStyleSheet* GetCatalogStyleSheetAt(PRInt32 aIndex) const;
  virtual void AddCatalogStyleSheet(nsIStyleSheet* aSheet);
  virtual void EnsureCatalogStyleSheet(const char *aStyleSheetURI);

  virtual nsIChannel* GetChannel() const {
    return mChannel;
  }

  /**
   * Get this document's inline style sheet.  May return null if there
   * isn't one
   */
  virtual nsHTMLCSSStyleSheet* GetInlineStyleSheet() const {
    return mStyleAttrStyleSheet;
  }
  
  /**
   * Set the object from which a document can get a script context.
   * This is the context within which all scripts (during document
   * creation and during event handling) will run.
   */
  virtual nsIScriptGlobalObject* GetScriptGlobalObject() const;
  virtual void SetScriptGlobalObject(nsIScriptGlobalObject* aGlobalObject);

  virtual void SetScriptHandlingObject(nsIScriptGlobalObject* aScriptObject);

  virtual nsIScriptGlobalObject* GetScopeObject();

  /**
   * Get the script loader for this document
   */
  virtual nsScriptLoader* ScriptLoader();

  /**
   * Add/Remove an element to the document's id and name hashes
   */
  virtual void AddToIdTable(Element* aElement, nsIAtom* aId);
  virtual void RemoveFromIdTable(Element* aElement, nsIAtom* aId);
  virtual void AddToNameTable(Element* aElement, nsIAtom* aName);
  virtual void RemoveFromNameTable(Element* aElement, nsIAtom* aName);

  /**
   * Add a new observer of document change notifications. Whenever
   * content is changed, appended, inserted or removed the observers are
   * informed.
   */
  virtual void AddObserver(nsIDocumentObserver* aObserver);

  /**
   * Remove an observer of document change notifications. This will
   * return false if the observer cannot be found.
   */
  virtual bool RemoveObserver(nsIDocumentObserver* aObserver);

  // Observation hooks used to propagate notifications to document
  // observers.
  virtual void BeginUpdate(nsUpdateType aUpdateType);
  virtual void EndUpdate(nsUpdateType aUpdateType);
  virtual void BeginLoad();
  virtual void EndLoad();

  virtual void SetReadyStateInternal(ReadyState rs);
  virtual ReadyState GetReadyStateEnum();

  virtual void ContentStateChanged(nsIContent* aContent,
                                   nsEventStates aStateMask);
  virtual void DocumentStatesChanged(nsEventStates aStateMask);

  virtual void StyleRuleChanged(nsIStyleSheet* aStyleSheet,
                                nsIStyleRule* aOldStyleRule,
                                nsIStyleRule* aNewStyleRule);
  virtual void StyleRuleAdded(nsIStyleSheet* aStyleSheet,
                              nsIStyleRule* aStyleRule);
  virtual void StyleRuleRemoved(nsIStyleSheet* aStyleSheet,
                                nsIStyleRule* aStyleRule);

  virtual void FlushPendingNotifications(mozFlushType aType);
  virtual void FlushExternalResources(mozFlushType aType);
  virtual void SetXMLDeclaration(const PRUnichar *aVersion,
                                 const PRUnichar *aEncoding,
                                 const PRInt32 aStandalone);
  virtual void GetXMLDeclaration(nsAString& aVersion,
                                 nsAString& aEncoding,
                                 nsAString& Standalone);
  virtual bool IsScriptEnabled();

  virtual void OnPageShow(bool aPersisted, nsIDOMEventTarget* aDispatchStartTarget);
  virtual void OnPageHide(bool aPersisted, nsIDOMEventTarget* aDispatchStartTarget);
  
  virtual void WillDispatchMutationEvent(nsINode* aTarget);
  virtual void MutationEventDispatched(nsINode* aTarget);

  // nsINode
  virtual bool IsNodeOfType(PRUint32 aFlags) const;
  virtual nsIContent *GetChildAt(PRUint32 aIndex) const;
  virtual nsIContent * const * GetChildArray(PRUint32* aChildCount) const;
  virtual PRInt32 IndexOf(nsINode* aPossibleChild) const;
  virtual PRUint32 GetChildCount() const;
  virtual nsresult InsertChildAt(nsIContent* aKid, PRUint32 aIndex,
                                 bool aNotify);
  virtual nsresult AppendChildTo(nsIContent* aKid, bool aNotify);
  virtual void RemoveChildAt(PRUint32 aIndex, bool aNotify);
  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const
  {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  // nsIRadioGroupContainer
  NS_IMETHOD WalkRadioGroup(const nsAString& aName,
                            nsIRadioVisitor* aVisitor,
                            bool aFlushContent);
  NS_IMETHOD SetCurrentRadioButton(const nsAString& aName,
                                   nsIDOMHTMLInputElement* aRadio);
  NS_IMETHOD GetCurrentRadioButton(const nsAString& aName,
                                   nsIDOMHTMLInputElement** aRadio);
  NS_IMETHOD GetNextRadioButton(const nsAString& aName,
                                const bool aPrevious,
                                nsIDOMHTMLInputElement*  aFocusedRadio,
                                nsIDOMHTMLInputElement** aRadioOut);
  NS_IMETHOD AddToRadioGroup(const nsAString& aName,
                             nsIFormControl* aRadio);
  NS_IMETHOD RemoveFromRadioGroup(const nsAString& aName,
                                  nsIFormControl* aRadio);
  virtual PRUint32 GetRequiredRadioCount(const nsAString& aName) const;
  virtual void RadioRequiredChanged(const nsAString& aName,
                                    nsIFormControl* aRadio);
  virtual bool GetValueMissingState(const nsAString& aName) const;
  virtual void SetValueMissingState(const nsAString& aName, bool aValue);

  // for radio group
  nsRadioGroupStruct* GetRadioGroup(const nsAString& aName);

  // nsIDOMNode
  NS_DECL_NSIDOMNODE

  // nsIDOMDocument
  NS_DECL_NSIDOMDOCUMENT

  // nsIDOMXMLDocument
  NS_DECL_NSIDOMXMLDOCUMENT

  // nsIDOMDocumentXBL
  NS_DECL_NSIDOMDOCUMENTXBL

  // nsIDOMEventTarget
  virtual nsresult PreHandleEvent(nsEventChainPreVisitor& aVisitor);
  virtual nsEventListenerManager*
    GetListenerManager(bool aCreateIfNotFound);

  // nsIScriptObjectPrincipal
  virtual nsIPrincipal* GetPrincipal();

  // nsIApplicationCacheContainer
  NS_DECL_NSIAPPLICATIONCACHECONTAINER

  // nsITouchEventReceiver
  NS_DECL_NSITOUCHEVENTRECEIVER

  // nsIDOMDocumentTouch
  NS_DECL_NSIDOMDOCUMENTTOUCH

  // nsIInlineEventHandlers
  NS_DECL_NSIINLINEEVENTHANDLERS

  // nsIObserver
  NS_DECL_NSIOBSERVER

  virtual nsresult Init();
  
  virtual void AddXMLEventsContent(nsIContent * aXMLEventsElement);

  virtual nsresult CreateElem(const nsAString& aName, nsIAtom *aPrefix,
                              PRInt32 aNamespaceID,
                              nsIContent **aResult);

  nsresult CreateElement(const nsAString& aTagName,
                         nsIContent** aReturn);
  nsresult CreateElementNS(const nsAString& aNamespaceURI,
                           const nsAString& aQualifiedName,
                           nsIContent** aReturn);

  nsresult CreateTextNode(const nsAString& aData, nsIContent** aReturn);

  virtual NS_HIDDEN_(nsresult) Sanitize();

  virtual NS_HIDDEN_(void) EnumerateSubDocuments(nsSubDocEnumFunc aCallback,
                                                 void *aData);

  virtual NS_HIDDEN_(bool) CanSavePresentation(nsIRequest *aNewRequest);
  virtual NS_HIDDEN_(void) Destroy();
  virtual NS_HIDDEN_(void) RemovedFromDocShell();
  virtual NS_HIDDEN_(already_AddRefed<nsILayoutHistoryState>) GetLayoutHistoryState() const;

  virtual NS_HIDDEN_(void) BlockOnload();
  virtual NS_HIDDEN_(void) UnblockOnload(bool aFireSync);

  virtual NS_HIDDEN_(void) AddStyleRelevantLink(mozilla::dom::Link* aLink);
  virtual NS_HIDDEN_(void) ForgetLink(mozilla::dom::Link* aLink);

  NS_HIDDEN_(void) ClearBoxObjectFor(nsIContent* aContent);
  NS_IMETHOD GetBoxObjectFor(nsIDOMElement* aElement, nsIBoxObject** aResult);

  virtual NS_HIDDEN_(nsresult) GetXBLChildNodesFor(nsIContent* aContent,
                                                   nsIDOMNodeList** aResult);
  virtual NS_HIDDEN_(nsresult) GetContentListFor(nsIContent* aContent,
                                                 nsIDOMNodeList** aResult);
  virtual NS_HIDDEN_(nsIContent*)
    GetAnonymousElementByAttribute(nsIContent* aElement,
                                   nsIAtom* aAttrName,
                                   const nsAString& aAttrValue) const;

  virtual NS_HIDDEN_(nsresult) ElementFromPointHelper(float aX, float aY,
                                                      bool aIgnoreRootScrollFrame,
                                                      bool aFlushLayout,
                                                      nsIDOMElement** aReturn);

  virtual NS_HIDDEN_(nsresult) NodesFromRectHelper(float aX, float aY,
                                                   float aTopSize, float aRightSize,
                                                   float aBottomSize, float aLeftSize,
                                                   bool aIgnoreRootScrollFrame,
                                                   bool aFlushLayout,
                                                   nsIDOMNodeList** aReturn);

  virtual NS_HIDDEN_(void) FlushSkinBindings();

  virtual NS_HIDDEN_(nsresult) InitializeFrameLoader(nsFrameLoader* aLoader);
  virtual NS_HIDDEN_(nsresult) FinalizeFrameLoader(nsFrameLoader* aLoader);
  virtual NS_HIDDEN_(void) TryCancelFrameLoaderInitialization(nsIDocShell* aShell);
  virtual NS_HIDDEN_(bool) FrameLoaderScheduledToBeFinalized(nsIDocShell* aShell);
  virtual NS_HIDDEN_(nsIDocument*)
    RequestExternalResource(nsIURI* aURI,
                            nsINode* aRequestingNode,
                            ExternalResourceLoad** aPendingLoad);
  virtual NS_HIDDEN_(void)
    EnumerateExternalResources(nsSubDocEnumFunc aCallback, void* aData);

  nsTArray<nsCString> mFileDataUris;

  // Returns our (lazily-initialized) animation controller.
  // If HasAnimationController is true, this is guaranteed to return non-null.
  nsSMILAnimationController* GetAnimationController();

  void SetImagesNeedAnimating(bool aAnimating);

  virtual void SuppressEventHandling(PRUint32 aIncrease);

  virtual void UnsuppressEventHandlingAndFireEvents(bool aFireEvents);
  
  void DecreaseEventSuppression() {
    --mEventsSuppressed;
    MaybeRescheduleAnimationFrameNotifications();
  }

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS_AMBIGUOUS(nsDocument,
                                                                   nsIDocument)

  void DoNotifyPossibleTitleChange();

  nsExternalResourceMap& ExternalResourceMap()
  {
    return mExternalResourceMap;
  }

  void SetLoadedAsData(bool aLoadedAsData) { mLoadedAsData = aLoadedAsData; }
  void SetLoadedAsInteractiveData(bool aLoadedAsInteractiveData)
  {
    mLoadedAsInteractiveData = aLoadedAsInteractiveData;
  }

  nsresult CloneDocHelper(nsDocument* clone) const;

  void MaybeInitializeFinalizeFrameLoaders();

  void MaybeEndOutermostXBLUpdate();

  virtual void MaybePreLoadImage(nsIURI* uri,
                                 const nsAString &aCrossOriginAttr);

  virtual void PreloadStyle(nsIURI* uri, const nsAString& charset);

  virtual nsresult LoadChromeSheetSync(nsIURI* uri, bool isAgentSheet,
                                       nsCSSStyleSheet** sheet);

  virtual nsISupports* GetCurrentContentSink();

  virtual nsEventStates GetDocumentState();

  virtual void RegisterFileDataUri(const nsACString& aUri);
  virtual void UnregisterFileDataUri(const nsACString& aUri);

  // Only BlockOnload should call this!
  void AsyncBlockOnload();

  virtual void SetScrollToRef(nsIURI *aDocumentURI);
  virtual void ScrollToRef();
  virtual void ResetScrolledToRefAlready();
  virtual void SetChangeScrollPosWhenScrollingToRef(bool aValue);

  already_AddRefed<nsContentList>
  GetElementsByTagName(const nsAString& aTagName) {
    return NS_GetContentList(this, kNameSpaceID_Unknown, aTagName);
  }
  already_AddRefed<nsContentList>
    GetElementsByTagNameNS(const nsAString& aNamespaceURI,
                           const nsAString& aLocalName);

  virtual Element *GetElementById(const nsAString& aElementId);
  virtual const nsSmallVoidArray* GetAllElementsForId(const nsAString& aElementId) const;

  virtual Element *LookupImageElement(const nsAString& aElementId);

  virtual NS_HIDDEN_(nsresult) AddImage(imgIRequest* aImage);
  virtual NS_HIDDEN_(nsresult) RemoveImage(imgIRequest* aImage);
  virtual NS_HIDDEN_(nsresult) SetImageLockingState(bool aLocked);

  // AddPlugin adds a plugin-related element to mPlugins when the element is
  // added to the tree.
  virtual nsresult AddPlugin(nsIObjectLoadingContent* aPlugin);
  // RemovePlugin removes a plugin-related element to mPlugins when the
  // element is removed from the tree.
  virtual void RemovePlugin(nsIObjectLoadingContent* aPlugin);
  // GetPlugins returns the plugin-related elements from
  // the frame and any subframes.
  virtual void GetPlugins(nsTArray<nsIObjectLoadingContent*>& aPlugins);

  virtual nsresult GetStateObject(nsIVariant** aResult);

  virtual nsDOMNavigationTiming* GetNavigationTiming() const;
  virtual nsresult SetNavigationTiming(nsDOMNavigationTiming* aTiming);

  virtual Element* FindImageMap(const nsAString& aNormalizedMapName);

  virtual void NotifyAudioAvailableListener();

  bool HasAudioAvailableListeners()
  {
    return mHasAudioAvailableListener;
  }

  virtual Element* GetFullScreenElement();
  virtual void AsyncRequestFullScreen(Element* aElement);
  virtual void RestorePreviousFullScreenState();
  virtual bool IsFullScreenDoc();
  virtual void SetApprovedForFullscreen(bool aIsApproved);

  static void ExitFullScreen();

  // This is called asynchronously by nsIDocument::AsyncRequestFullScreen()
  // to move document into full-screen mode if allowed. aWasCallerChrome
  // should be true when nsIDocument::AsyncRequestFullScreen() was called
  // by chrome code.
  void RequestFullScreen(Element* aElement, bool aWasCallerChrome);

  // Removes all elements from the full-screen stack, removing full-scren
  // styles from the top element in the stack.
  void CleanupFullscreenState();

  // Add/remove "fullscreen-approved" observer service notification listener.
  // Chrome sends us a notification when fullscreen is approved for a
  // document, with the notification subject as the document that was approved.
  // We maintain this listener while in fullscreen mode.
  nsresult AddFullscreenApprovedObserver();
  nsresult RemoveFullscreenApprovedObserver();

  // Pushes aElement onto the full-screen stack, and removes full-screen styles
  // from the former full-screen stack top, and its ancestors, and applies the
  // styles to aElement. aElement becomes the new "full-screen element".
  bool FullScreenStackPush(Element* aElement);

  // Remove the top element from the full-screen stack. Removes the full-screen
  // styles from the former top element, and applies them to the new top
  // element, if there is one.
  void FullScreenStackPop();

  // Returns the top element from the full-screen stack.
  Element* FullScreenStackTop();

  void RequestPointerLock(Element* aElement);
  bool ShouldLockPointer(Element* aElement);
  bool SetPointerLock(Element* aElement, int aCursorStyle);
  static void UnlockPointer();

  // This method may fire a DOM event; if it does so it will happen
  // synchronously.
  void UpdateVisibilityState();
  // Posts an event to call UpdateVisibilityState
  virtual void PostVisibilityUpdateEvent();

  virtual void DocSizeOfExcludingThis(nsWindowSizes* aWindowSizes) const;
  // DocSizeOfIncludingThis is inherited from nsIDocument.

  virtual nsIDOMNode* AsDOMNode() { return this; }
protected:
  friend class nsNodeUtils;

  // Returns true if a request for DOM full-screen is currently enabled in
  // this document. This returns true if there are no windowed plugins in this
  // doc tree, and if the document is visible, and if the api is not
  // disabled by pref. aIsCallerChrome must contain the return value of
  // nsContentUtils::IsCallerChrome() from the context we're checking.
  // If aLogFailure is true, an appropriate warning message is logged to the
  // console, and a "mozfullscreenerror" event is dispatched to this document.
  bool IsFullScreenEnabled(bool aIsCallerChrome, bool aLogFailure);

  /**
   * Check that aId is not empty and log a message to the console
   * service if it is.
   * @returns true if aId looks correct, false otherwise.
   */
  inline bool CheckGetElementByIdArg(const nsAString& aId)
  {
    if (aId.IsEmpty()) {
      ReportEmptyGetElementByIdArg();
      return false;
    }
    return true;
  }

  void ReportEmptyGetElementByIdArg();

  void DispatchContentLoadedEvents();

  void RetrieveRelevantHeaders(nsIChannel *aChannel);

  bool TryChannelCharset(nsIChannel *aChannel,
                         PRInt32& aCharsetSource,
                         nsACString& aCharset,
                         nsHtml5TreeOpExecutor* aExecutor);

  // Call this before the document does something that will unbind all content.
  // That will stop us from doing a lot of work as each element is removed.
  void DestroyElementMaps();

  // Refreshes the hrefs of all the links in the document.
  void RefreshLinkHrefs();

  nsIContent* GetFirstBaseNodeWithHref();
  nsresult SetFirstBaseNodeWithHref(nsIContent *node);

  // Get the first <title> element with the given IsNodeOfType type, or
  // return null if there isn't one
  nsIContent* GetTitleContent(PRUint32 aNodeType);
  // Find the first "title" element in the given IsNodeOfType type and
  // append the concatenation of its text node children to aTitle. Do
  // nothing if there is no such element.
  void GetTitleFromElement(PRUint32 aNodeType, nsAString& aTitle);

  nsresult doCreateShell(nsPresContext* aContext,
                         nsIViewManager* aViewManager, nsStyleSet* aStyleSet,
                         nsCompatibility aCompatMode,
                         nsIPresShell** aInstancePtrResult);

  nsresult ResetStylesheetsToURI(nsIURI* aURI);
  void FillStyleSet(nsStyleSet* aStyleSet);

  // Return whether all the presshells for this document are safe to flush
  bool IsSafeToFlush() const;
  
  void DispatchPageTransition(nsIDOMEventTarget* aDispatchTarget,
                              const nsAString& aType,
                              bool aPersisted);

  virtual nsPIDOMWindow *GetWindowInternal() const;
  virtual nsPIDOMWindow *GetInnerWindowInternal();
  virtual nsIScriptGlobalObject* GetScriptHandlingObjectInternal() const;
  virtual bool InternalAllowXULXBL();

#define NS_DOCUMENT_NOTIFY_OBSERVERS(func_, params_)                        \
  NS_OBSERVER_ARRAY_NOTIFY_XPCOM_OBSERVERS(mObservers, nsIDocumentObserver, \
                                           func_, params_);
  
#ifdef DEBUG
  void VerifyRootContentState();
#endif

  nsDocument(const char* aContentType);
  virtual ~nsDocument();

  void EnsureOnloadBlocker();

  nsCString mReferrer;
  nsString mLastModified;

  nsTArray<nsIObserver*> mCharSetObservers;

  PLDHashTable *mSubDocuments;

  // Array of owning references to all children
  nsAttrAndChildArray mChildren;

  // Pointer to our parser if we're currently in the process of being
  // parsed into.
  nsCOMPtr<nsIParser> mParser;

  // Weak reference to our sink for in case we no longer have a parser.  This
  // will allow us to flush out any pending stuff from the sink even if
  // EndLoad() has already happened.
  nsWeakPtr mWeakSink;

  nsCOMArray<nsIStyleSheet> mStyleSheets;
  nsCOMArray<nsIStyleSheet> mCatalogSheets;

  // Array of observers
  nsTObserverArray<nsIDocumentObserver*> mObservers;

  // If document is created for example using
  // document.implementation.createDocument(...), mScriptObject points to
  // the script global object of the original document.
  nsWeakPtr mScriptObject;

  // Weak reference to the scope object (aka the script global object)
  // that, unlike mScriptGlobalObject, is never unset once set. This
  // is a weak reference to avoid leaks due to circular references.
  nsWeakPtr mScopeObject;

  // The document which requested (and was granted) full-screen. All ancestors
  // of this document will also be full-screen.
  static nsWeakPtr sFullScreenDoc;

  // The root document of the doctree containing the document which requested
  // full-screen. This root document will also be in full-screen state, as will
  // all the descendents down to the document which requested full-screen. This
  // reference allows us to reset full-screen state on all documents when a
  // document is hidden/navigation occurs.
  static nsWeakPtr sFullScreenRootDoc;

  // Weak reference to the document which owned the pending pointer lock
  // element, at the time it requested pointer lock.
  static nsWeakPtr sPendingPointerLockDoc;

  // Weak reference to the element which requested pointer lock. This request
  // is "pending", and will be processed once the element's document has had
  // the "fullscreen" permission granted.
  static nsWeakPtr sPendingPointerLockElement;

  // Stack of full-screen elements. When we request full-screen we push the
  // full-screen element onto this stack, and when we cancel full-screen we
  // pop one off this stack, restoring the previous full-screen state
  nsTArray<nsWeakPtr> mFullScreenStack;

  nsRefPtr<nsEventListenerManager> mListenerManager;
  nsCOMPtr<nsIDOMStyleSheetList> mDOMStyleSheets;
  nsRefPtr<nsDOMStyleSheetSetList> mStyleSheetSetList;
  nsRefPtr<nsScriptLoader> mScriptLoader;
  nsDocHeaderData* mHeaderData;
  /* mIdentifierMap works as follows for IDs:
   * 1) Attribute changes affect the table immediately (removing and adding
   *    entries as needed).
   * 2) Removals from the DOM affect the table immediately
   * 3) Additions to the DOM always update existing entries for names, and add
   *    new ones for IDs.
   */
  nsTHashtable<nsIdentifierMapEntry> mIdentifierMap;

  nsClassHashtable<nsStringHashKey, nsRadioGroupStruct> mRadioGroups;

  // Recorded time of change to 'loading' state.
  mozilla::TimeStamp mLoadingTimeStamp;

  // True if the document has been detached from its content viewer.
  bool mIsGoingAway:1;
  // True if the document is being destroyed.
  bool mInDestructor:1;

  // True if this document has ever had an HTML or SVG <title> element
  // bound to it
  bool mMayHaveTitleElement:1;

  bool mHasWarnedAboutBoxObjects:1;

  bool mDelayFrameLoaderInitialization:1;

  bool mSynchronousDOMContentLoaded:1;

  // If true, we have an input encoding.  If this is false, then the
  // document was created entirely in memory
  bool mHaveInputEncoding:1;

  bool mInXBLUpdate:1;

  // This flag is only set in nsXMLDocument, for e.g. documents used in XBL. We
  // don't want animations to play in such documents, so we need to store the
  // flag here so that we can check it in nsDocument::GetAnimationController.
  bool mLoadedAsInteractiveData:1;

  // Whether we're currently holding a lock on all of our images.
  bool mLockingImages:1;

  // Whether we currently require our images to animate
  bool mAnimatingImages:1;

  // Whether some node in this document has a listener for the
  // "mozaudioavailable" event.
  bool mHasAudioAvailableListener:1;

  // Whether we're currently under a FlushPendingNotifications call to
  // our presshell.  This is used to handle flush reentry correctly.
  bool mInFlush:1;

  // Parser aborted. True if the parser of this document was forcibly
  // terminated instead of letting it finish at its own pace.
  bool mParserAborted:1;

  // Whether this document has been approved for fullscreen, either by explicit
  // approval via the fullscreen-approval UI, or because it received
  // approval because its document's host already had the "fullscreen"
  // permission granted when the document requested fullscreen.
  // 
  // Note if a document's principal doesn't have a host, the permission manager
  // can't store permissions for it, so we can only manage approval using this
  // flag.
  //
  // Note we must track this separately from the "fullscreen" permission,
  // so that pending pointer lock requests can determine whether documents
  // whose principal doesn't have a host (i.e. those which can't store
  // permissions in the permission manager) have been approved for fullscreen.
  bool mIsApprovedForFullscreen:1;

  PRUint8 mXMLDeclarationBits;

  nsInterfaceHashtable<nsPtrHashKey<nsIContent>, nsPIBoxObject> *mBoxObjectTable;

  // The channel that got passed to StartDocumentLoad(), if any
  nsCOMPtr<nsIChannel> mChannel;
  nsRefPtr<nsHTMLCSSStyleSheet> mStyleAttrStyleSheet;
  nsRefPtr<nsXMLEventsManager> mXMLEventsManager;

  // Our update nesting level
  PRUint32 mUpdateNestLevel;

  // The application cache that this document is associated with, if
  // any.  This can change during the lifetime of the document.
  nsCOMPtr<nsIApplicationCache> mApplicationCache;

  nsCOMPtr<nsIContent> mFirstBaseNodeWithHref;

  nsEventStates mDocumentState;
  nsEventStates mGotDocumentState;

  nsRefPtr<nsDOMNavigationTiming> mTiming;
private:
  friend class nsUnblockOnloadEvent;
  // This needs to stay in sync with the list in GetMozVisibilityState.
  enum VisibilityState {
    eHidden = 0,
    eVisible,
    eVisibilityStateCount
  };
  // Recomputes the visibility state but doesn't set the new value.
  VisibilityState GetVisibilityState() const;

  void PostUnblockOnloadEvent();
  void DoUnblockOnload();

  nsresult CheckFrameOptions();
  nsresult InitCSP();

  // Sets aElement to be the pending pointer lock element. Once this document's
  // node principal's URI is granted the "fullscreen" permission, the pointer
  // lock request will be processed. At any one time there can be only one
  // pending pointer lock request; calling this clears the previous pending
  // request.
  static nsresult SetPendingPointerLockRequest(Element* aElement);

  // Clears any pending pointer lock request.
  static void ClearPendingPointerLockRequest(bool aDispatchErrorEvents);

  /**
   * Find the (non-anonymous) content in this document for aFrame. It will
   * be aFrame's content node if that content is in this document and not
   * anonymous. Otherwise, when aFrame is in a subdocument, we use the frame
   * element containing the subdocument containing aFrame, and/or find the
   * nearest non-anonymous ancestor in this document.
   * Returns null if there is no such element.
   */
  nsIContent* GetContentInThisDocument(nsIFrame* aFrame) const;

  // Just like EnableStyleSheetsForSet, but doesn't check whether
  // aSheetSet is null and allows the caller to control whether to set
  // aSheetSet as the preferred set in the CSSLoader.
  void EnableStyleSheetsForSetInternal(const nsAString& aSheetSet,
                                       bool aUpdateCSSLoader);

  // Revoke any pending notifications due to mozRequestAnimationFrame calls
  void RevokeAnimationFrameNotifications();
  // Reschedule any notifications we need to handle
  // mozRequestAnimationFrame, if it's OK to do so.
  void MaybeRescheduleAnimationFrameNotifications();

  // These are not implemented and not supported.
  nsDocument(const nsDocument& aOther);
  nsDocument& operator=(const nsDocument& aOther);

  nsCOMPtr<nsISupports> mXPathEvaluatorTearoff;

  // The layout history state that should be used by nodes in this
  // document.  We only actually store a pointer to it when:
  // 1)  We have no script global object.
  // 2)  We haven't had Destroy() called on us yet.
  nsCOMPtr<nsILayoutHistoryState> mLayoutHistoryState;

  // Currently active onload blockers
  PRUint32 mOnloadBlockCount;
  // Onload blockers which haven't been activated yet
  PRUint32 mAsyncOnloadBlockCount;
  nsCOMPtr<nsIRequest> mOnloadBlocker;
  ReadyState mReadyState;

  // A hashtable of styled links keyed by address pointer.
  nsTHashtable<nsPtrHashKey<mozilla::dom::Link> > mStyledLinks;
#ifdef DEBUG
  // Indicates whether mStyledLinks was cleared or not.  This is used to track
  // state so we can provide useful assertions to consumers of ForgetLink and
  // AddStyleRelevantLink.
  bool mStyledLinksCleared;
#endif

  // Member to store out last-selected stylesheet set.
  nsString mLastStyleSheetSet;

  nsTArray<nsRefPtr<nsFrameLoader> > mInitializableFrameLoaders;
  nsTArray<nsRefPtr<nsFrameLoader> > mFinalizableFrameLoaders;
  nsRefPtr<nsRunnableMethod<nsDocument> > mFrameLoaderRunner;

  nsRevocableEventPtr<nsRunnableMethod<nsDocument, void, false> >
    mPendingTitleChangeEvent;

  nsExternalResourceMap mExternalResourceMap;

  // All images in process of being preloaded
  nsCOMArray<imgIRequest> mPreloadingImages;

  nsCOMPtr<nsIDOMDOMImplementation> mDOMImplementation;

  nsRefPtr<nsContentList> mImageMaps;

  nsCString mScrollToRef;
  PRUint8 mScrolledToRefAlready : 1;
  PRUint8 mChangeScrollPosWhenScrollingToRef : 1;

  // Tracking for images in the document.
  nsDataHashtable< nsPtrHashKey<imgIRequest>, PRUint32> mImageTracker;

  // Tracking for plugins in the document.
  nsTHashtable< nsPtrHashKey<nsIObjectLoadingContent> > mPlugins;

  VisibilityState mVisibilityState;

#ifdef DEBUG
protected:
  bool mWillReparent;
#endif
};

#define NS_DOCUMENT_INTERFACE_TABLE_BEGIN(_class)                             \
  NS_NODE_OFFSET_AND_INTERFACE_TABLE_BEGIN(_class)                            \
  NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsIDOMDocument, nsDocument)      \
  NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsIDOMEventTarget, nsDocument)   \
  NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsIDOMNode, nsDocument)

#endif /* nsDocument_h___ */
