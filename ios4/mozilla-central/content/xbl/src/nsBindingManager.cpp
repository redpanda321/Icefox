/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=79: */
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
 *   Original Author: David W. Hyatt (hyatt@netscape.com)
 *   Alec Flett <alecf@netscape.com>
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

#include "nsCOMPtr.h"
#include "nsIXBLService.h"
#include "nsIInputStream.h"
#include "nsDoubleHashtable.h"
#include "nsIURI.h"
#include "nsIURL.h"
#include "nsIChannel.h"
#include "nsXPIDLString.h"
#include "nsIParser.h"
#include "nsParserCIID.h"
#include "nsNetUtil.h"
#include "plstr.h"
#include "nsIContent.h"
#include "nsIDOMElement.h"
#include "nsIDocument.h"
#include "mozilla/FunctionTimer.h"
#include "nsContentUtils.h"
#include "nsIPresShell.h"
#include "nsIXMLContentSink.h"
#include "nsContentCID.h"
#include "nsXMLDocument.h"
#include "nsIStreamListener.h"

#include "nsXBLBinding.h"
#include "nsXBLPrototypeBinding.h"
#include "nsXBLDocumentInfo.h"
#include "nsXBLInsertionPoint.h"

#include "nsIStyleRuleProcessor.h"
#include "nsRuleProcessorData.h"
#include "nsIWeakReference.h"

#include "jsapi.h"
#include "nsIXPConnect.h"
#include "nsDOMCID.h"
#include "nsIDOMScriptObjectFactory.h"
#include "nsIScriptGlobalObject.h"
#include "nsTHashtable.h"

#include "nsIScriptContext.h"
#include "nsBindingManager.h"

#include "nsThreadUtils.h"

// ==================================================================
// = nsAnonymousContentList 
// ==================================================================

#define NS_ANONYMOUS_CONTENT_LIST_IID \
  { 0xa29df1f8, 0xaeca, 0x4356, \
    { 0xa8, 0xc2, 0xa7, 0x24, 0xa2, 0x11, 0x73, 0xac } }

class nsAnonymousContentList : public nsINodeList
{
public:
  nsAnonymousContentList(nsInsertionPointList* aElements);
  virtual ~nsAnonymousContentList();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsAnonymousContentList, nsINodeList)
  // nsIDOMNodeList interface
  NS_DECL_NSIDOMNODELIST

  // nsINodeList interface
  virtual nsIContent* GetNodeAt(PRUint32 aIndex);
  virtual PRInt32 IndexOf(nsIContent* aContent);

  PRInt32 GetInsertionPointCount() { return mElements->Length(); }

  nsXBLInsertionPoint* GetInsertionPointAt(PRInt32 i) { return static_cast<nsXBLInsertionPoint*>(mElements->ElementAt(i)); }
  void RemoveInsertionPointAt(PRInt32 i) { mElements->RemoveElementAt(i); }
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_ANONYMOUS_CONTENT_LIST_IID)
private:
  nsInsertionPointList* mElements;
};

NS_DEFINE_STATIC_IID_ACCESSOR(nsAnonymousContentList,
                              NS_ANONYMOUS_CONTENT_LIST_IID)

nsAnonymousContentList::nsAnonymousContentList(nsInsertionPointList* aElements)
  : mElements(aElements)
{
  MOZ_COUNT_CTOR(nsAnonymousContentList);

  // We don't reference count our Anonymous reference (to avoid circular
  // references). We'll be told when the Anonymous goes away.
}

nsAnonymousContentList::~nsAnonymousContentList()
{
  MOZ_COUNT_DTOR(nsAnonymousContentList);
  delete mElements;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsAnonymousContentList)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsAnonymousContentList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsAnonymousContentList)

NS_INTERFACE_TABLE_HEAD(nsAnonymousContentList)
  NS_NODELIST_OFFSET_AND_INTERFACE_TABLE_BEGIN(nsAnonymousContentList)
    NS_INTERFACE_TABLE_ENTRY(nsAnonymousContentList, nsINodeList)
    NS_INTERFACE_TABLE_ENTRY(nsAnonymousContentList, nsIDOMNodeList)
    NS_INTERFACE_TABLE_ENTRY(nsAnonymousContentList, nsAnonymousContentList)
  NS_OFFSET_AND_INTERFACE_TABLE_END
  NS_OFFSET_AND_INTERFACE_TABLE_TO_MAP_SEGUE
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(NodeList)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(nsAnonymousContentList)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsAnonymousContentList)
  tmp->mElements->Clear();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsAnonymousContentList)
  {
    PRInt32 i, count = tmp->mElements->Length();
    for (i = 0; i < count; ++i) {
      NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_MEMBER(mElements->ElementAt(i),
                                                      nsXBLInsertionPoint);
    }
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMETHODIMP
nsAnonymousContentList::GetLength(PRUint32* aLength)
{
  NS_ASSERTION(aLength != nsnull, "null ptr");
  if (! aLength)
      return NS_ERROR_NULL_POINTER;

  PRInt32 cnt = mElements->Length();

  *aLength = 0;
  for (PRInt32 i = 0; i < cnt; i++)
    *aLength += static_cast<nsXBLInsertionPoint*>(mElements->ElementAt(i))->ChildCount();

  return NS_OK;
}

NS_IMETHODIMP    
nsAnonymousContentList::Item(PRUint32 aIndex, nsIDOMNode** aReturn)
{
  nsINode* item = GetNodeAt(aIndex);
  if (!item)
    return NS_ERROR_FAILURE;

  return CallQueryInterface(item, aReturn);    
}

nsIContent*
nsAnonymousContentList::GetNodeAt(PRUint32 aIndex)
{
  PRInt32 cnt = mElements->Length();
  PRUint32 pointCount = 0;

  for (PRInt32 i = 0; i < cnt; i++) {
    aIndex -= pointCount;
    
    nsXBLInsertionPoint* point = static_cast<nsXBLInsertionPoint*>(mElements->ElementAt(i));
    pointCount = point->ChildCount();

    if (aIndex < pointCount) {
      return point->ChildAt(aIndex);
    }
  }

  return nsnull;
}

PRInt32
nsAnonymousContentList::IndexOf(nsIContent* aContent)
{
  PRInt32 cnt = mElements->Length();
  PRInt32 lengthSoFar = 0;

  for (PRInt32 i = 0; i < cnt; ++i) {
    nsXBLInsertionPoint* point =
      static_cast<nsXBLInsertionPoint*>(mElements->ElementAt(i));
    PRInt32 idx = point->IndexOf(aContent);
    if (idx != -1) {
      return idx + lengthSoFar;
    }

    lengthSoFar += point->ChildCount();
  }

  // Didn't find it anywhere
  return -1;
}

//
// Generic pldhash table stuff for mapping one nsISupports to another
//
// These values are never null - a null value implies that this
// whole key should be removed (See SetOrRemoveObject)
class ObjectEntry : public PLDHashEntryHdr
{
public:

  // note that these are allocated within the PLDHashTable, but we
  // want to keep track of them anyway
  ObjectEntry() { MOZ_COUNT_CTOR(ObjectEntry); }
  ~ObjectEntry() { MOZ_COUNT_DTOR(ObjectEntry); }
  
  nsISupports* GetValue() { return mValue; }
  nsISupports* GetKey() { return mKey; }
  void SetValue(nsISupports* aValue) { mValue = aValue; }
  void SetKey(nsISupports* aKey) { mKey = aKey; }
  
private:
  nsCOMPtr<nsISupports> mKey;
  nsCOMPtr<nsISupports> mValue;
};

static void
ClearObjectEntry(PLDHashTable* table, PLDHashEntryHdr *entry)
{
  ObjectEntry* objEntry = static_cast<ObjectEntry*>(entry);
  objEntry->~ObjectEntry();
}

static PRBool
InitObjectEntry(PLDHashTable* table, PLDHashEntryHdr* entry, const void* key)
{
  new (entry) ObjectEntry;
  return PR_TRUE;
}
  


static PLDHashTableOps ObjectTableOps = {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  PL_DHashVoidPtrKeyStub,
  PL_DHashMatchEntryStub,
  PL_DHashMoveEntryStub,
  ClearObjectEntry,
  PL_DHashFinalizeStub,
  InitObjectEntry
};

// helper routine for adding a new entry
static nsresult
AddObjectEntry(PLDHashTable& table, nsISupports* aKey, nsISupports* aValue)
{
  NS_ASSERTION(aKey, "key must be non-null");
  if (!aKey) return NS_ERROR_INVALID_ARG;
  
  ObjectEntry *entry =
    static_cast<ObjectEntry*>
               (PL_DHashTableOperate(&table, aKey, PL_DHASH_ADD));

  if (!entry)
    return NS_ERROR_OUT_OF_MEMORY;

  // only add the key if the entry is new
  if (!entry->GetKey())
    entry->SetKey(aKey);

  // now attach the new entry - note that entry->mValue could possibly
  // have a value already, this will release that.
  entry->SetValue(aValue);
  
  return NS_OK;
}

// helper routine for looking up an existing entry. Note that the
// return result is NOT addreffed
static nsISupports*
LookupObject(PLDHashTable& table, nsIContent* aKey)
{
  if (aKey && aKey->HasFlag(NODE_MAY_BE_IN_BINDING_MNGR)) {
    ObjectEntry *entry =
      static_cast<ObjectEntry*>
                 (PL_DHashTableOperate(&table, aKey, PL_DHASH_LOOKUP));

    if (PL_DHASH_ENTRY_IS_BUSY(entry))
      return entry->GetValue();
  }

  return nsnull;
}

inline void
RemoveObjectEntry(PLDHashTable& table, nsISupports* aKey)
{
  PL_DHashTableOperate(&table, aKey, PL_DHASH_REMOVE);
}

static nsresult
SetOrRemoveObject(PLDHashTable& table, nsIContent* aKey, nsISupports* aValue)
{
  if (aValue) {
    // lazily create the table, but only when adding elements
    if (!table.ops &&
        !PL_DHashTableInit(&table, &ObjectTableOps, nsnull,
                           sizeof(ObjectEntry), 16)) {
      table.ops = nsnull;
      return NS_ERROR_OUT_OF_MEMORY;
    }
    aKey->SetFlags(NODE_MAY_BE_IN_BINDING_MNGR);
    return AddObjectEntry(table, aKey, aValue);
  }

  // no value, so remove the key from the table
  if (table.ops) {
    ObjectEntry* entry =
      static_cast<ObjectEntry*>
        (PL_DHashTableOperate(&table, aKey, PL_DHASH_LOOKUP));
    if (entry && PL_DHASH_ENTRY_IS_BUSY(entry)) {
      // Keep key and value alive while removing the entry.
      nsCOMPtr<nsISupports> key = entry->GetKey();
      nsCOMPtr<nsISupports> value = entry->GetValue();
      RemoveObjectEntry(table, aKey);
    }
  }
  return NS_OK;
}

// Implementation /////////////////////////////////////////////////////////////////

// Static member variable initialization

// Implement our nsISupports methods

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsBindingManager)
  tmp->mDestroyed = PR_TRUE;

  if (tmp->mBindingTable.IsInitialized())
    tmp->mBindingTable.Clear();

  if (tmp->mDocumentTable.IsInitialized())
    tmp->mDocumentTable.Clear();

  if (tmp->mLoadingDocTable.IsInitialized())
    tmp->mLoadingDocTable.Clear();

  if (tmp->mContentListTable.ops)
    PL_DHashTableFinish(&(tmp->mContentListTable));
  tmp->mContentListTable.ops = nsnull;

  if (tmp->mAnonymousNodesTable.ops)
    PL_DHashTableFinish(&(tmp->mAnonymousNodesTable));
  tmp->mAnonymousNodesTable.ops = nsnull;

  if (tmp->mInsertionParentTable.ops)
    PL_DHashTableFinish(&(tmp->mInsertionParentTable));
  tmp->mInsertionParentTable.ops = nsnull;

  if (tmp->mWrapperTable.ops)
    PL_DHashTableFinish(&(tmp->mWrapperTable));
  tmp->mWrapperTable.ops = nsnull;

  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSTARRAY(mAttachedStack)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END


static PLDHashOperator
DocumentInfoHashtableTraverser(nsIURI* key,
                               nsXBLDocumentInfo* di,
                               void* userArg)
{
  nsCycleCollectionTraversalCallback *cb = 
    static_cast<nsCycleCollectionTraversalCallback*>(userArg);
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*cb, "mDocumentTable value");
  nsCOMPtr<nsISupports> iface = do_QueryObject(di);
  cb->NoteXPCOMChild(iface);
  return PL_DHASH_NEXT;
}

static PLDHashOperator
LoadingDocHashtableTraverser(nsIURI* key,
                             nsIStreamListener* sl,
                             void* userArg)
{
  nsCycleCollectionTraversalCallback *cb = 
    static_cast<nsCycleCollectionTraversalCallback*>(userArg);
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*cb, "mLoadingDocTable value");
  cb->NoteXPCOMChild(sl);
  return PL_DHASH_NEXT;
}

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsBindingManager)
  // The hashes keyed on nsIContent are traversed from the nsIContent itself.
  if (tmp->mDocumentTable.IsInitialized())
      tmp->mDocumentTable.EnumerateRead(&DocumentInfoHashtableTraverser, &cb);
  if (tmp->mLoadingDocTable.IsInitialized())
      tmp->mLoadingDocTable.EnumerateRead(&LoadingDocHashtableTraverser, &cb);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSTARRAY_MEMBER(mAttachedStack,
                                                    nsXBLBinding)
  // No need to traverse mProcessAttachedQueueEvent, since it'll just
  // fire at some point or become revoke and drop its ref to us.
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_CLASS(nsBindingManager)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsBindingManager)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsBindingManager)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsBindingManager)

// Constructors/Destructors
nsBindingManager::nsBindingManager(nsIDocument* aDocument)
  : mProcessingAttachedStack(PR_FALSE),
    mDestroyed(PR_FALSE),
    mAttachedStackSizeOnOutermost(0),
    mDocument(aDocument)
{
  mContentListTable.ops = nsnull;
  mAnonymousNodesTable.ops = nsnull;
  mInsertionParentTable.ops = nsnull;
  mWrapperTable.ops = nsnull;
}

nsBindingManager::~nsBindingManager(void)
{
  mDestroyed = PR_TRUE;

  if (mContentListTable.ops)
    PL_DHashTableFinish(&mContentListTable);
  if (mAnonymousNodesTable.ops)
    PL_DHashTableFinish(&mAnonymousNodesTable);
  NS_ASSERTION(!mInsertionParentTable.ops || !mInsertionParentTable.entryCount,
               "Insertion parent table isn't empty!");
  if (mInsertionParentTable.ops)
    PL_DHashTableFinish(&mInsertionParentTable);
  if (mWrapperTable.ops)
    PL_DHashTableFinish(&mWrapperTable);
}

PLDHashOperator
RemoveInsertionParentCB(PLDHashTable* aTable, PLDHashEntryHdr* aEntry,
                        PRUint32 aNumber, void* aArg)
{
  return (static_cast<ObjectEntry*>(aEntry)->GetValue() ==
          static_cast<nsISupports*>(aArg)) ? PL_DHASH_REMOVE : PL_DHASH_NEXT;
}

static void
RemoveInsertionParentForNodeList(nsIDOMNodeList* aList, nsIContent* aParent)
{
  nsAnonymousContentList* list = nsnull;
  if (aList) {
    CallQueryInterface(aList, &list);
  }
  if (list) {
    PRInt32 count = list->GetInsertionPointCount();
    for (PRInt32 i = 0; i < count; ++i) {
      nsRefPtr<nsXBLInsertionPoint> currPoint = list->GetInsertionPointAt(i);
      currPoint->UnbindDefaultContent();
#ifdef DEBUG
      nsCOMPtr<nsIContent> parent = currPoint->GetInsertionParent();
      NS_ASSERTION(!parent || parent == aParent, "Wrong insertion parent!");
#endif
      currPoint->ClearInsertionParent();
    }
    NS_RELEASE(list);
  }
}

void
nsBindingManager::RemoveInsertionParent(nsIContent* aParent)
{
  RemoveInsertionParentForNodeList(GetContentListFor(aParent), aParent);

  RemoveInsertionParentForNodeList(GetAnonymousNodesFor(aParent), aParent);

  if (mInsertionParentTable.ops) {
    PL_DHashTableEnumerate(&mInsertionParentTable, RemoveInsertionParentCB,
                           static_cast<nsISupports*>(aParent));
  }
}

nsXBLBinding*
nsBindingManager::GetBinding(nsIContent* aContent)
{
  if (aContent && aContent->HasFlag(NODE_MAY_BE_IN_BINDING_MNGR) &&
      mBindingTable.IsInitialized()) {
    return mBindingTable.GetWeak(aContent);
  }

  return nsnull;
}

nsresult
nsBindingManager::SetBinding(nsIContent* aContent, nsXBLBinding* aBinding)
{
  if (!mBindingTable.IsInitialized()) {
    if (!mBindingTable.Init())
      return NS_ERROR_OUT_OF_MEMORY;
  }

  // After this point, aBinding will be the most-derived binding for aContent.
  // If we already have a binding for aContent in our table, make sure to
  // remove it from the attached stack.  Otherwise we might end up firing its
  // constructor twice (if aBinding inherits from it) or firing its constructor
  // after aContent has been deleted (if aBinding is null and the content node
  // dies before we process mAttachedStack).
  nsRefPtr<nsXBLBinding> oldBinding = GetBinding(aContent);
  if (oldBinding) {
    if (aContent->HasFlag(NODE_IS_INSERTION_PARENT)) {
      nsRefPtr<nsXBLBinding> parentBinding =
        GetBinding(aContent->GetBindingParent());
      // Clear insertion parent only if we don't have a parent binding which
      // marked content to be an insertion parent. See also ChangeDocumentFor().
      if (!parentBinding || !parentBinding->HasInsertionParent(aContent)) {
        RemoveInsertionParent(aContent);
        aContent->UnsetFlags(NODE_IS_INSERTION_PARENT);
      }
    }
    // Don't remove items here as that could mess up an executing
    // ProcessAttachedQueue
    PRUint32 index = mAttachedStack.IndexOf(oldBinding);
    if (index != mAttachedStack.NoIndex) {
      mAttachedStack[index] = nsnull;
    }
  }
  
  PRBool result = PR_TRUE;

  if (aBinding) {
    aContent->SetFlags(NODE_MAY_BE_IN_BINDING_MNGR);
    result = mBindingTable.Put(aContent, aBinding);
  } else {
    mBindingTable.Remove(aContent);

    // The death of the bindings means the death of the JS wrapper,
    // and the flushing of our explicit and anonymous insertion point
    // lists.
    SetWrappedJS(aContent, nsnull);
    SetContentListFor(aContent, nsnull);
    SetAnonymousNodesFor(aContent, nsnull);
  }

  return result ? NS_OK : NS_ERROR_FAILURE;
}

nsIContent*
nsBindingManager::GetInsertionParent(nsIContent* aContent)
{ 
  if (mInsertionParentTable.ops) {
    return static_cast<nsIContent*>
                      (LookupObject(mInsertionParentTable, aContent));
  }

  return nsnull;
}

nsresult
nsBindingManager::SetInsertionParent(nsIContent* aContent, nsIContent* aParent)
{
  NS_ASSERTION(!aParent || aParent->HasFlag(NODE_IS_INSERTION_PARENT),
               "Insertion parent should have NODE_IS_INSERTION_PARENT flag!");

  if (mDestroyed) {
    return NS_OK;
  }

  return SetOrRemoveObject(mInsertionParentTable, aContent, aParent);
}

nsIXPConnectWrappedJS*
nsBindingManager::GetWrappedJS(nsIContent* aContent)
{ 
  if (mWrapperTable.ops) {
    return static_cast<nsIXPConnectWrappedJS*>(LookupObject(mWrapperTable, aContent));
  }

  return nsnull;
}

nsresult
nsBindingManager::SetWrappedJS(nsIContent* aContent, nsIXPConnectWrappedJS* aWrappedJS)
{
  if (mDestroyed) {
    return NS_OK;
  }

  return SetOrRemoveObject(mWrapperTable, aContent, aWrappedJS);
}

void
nsBindingManager::RemovedFromDocumentInternal(nsIContent* aContent,
                                              nsIDocument* aOldDocument)
{
  NS_PRECONDITION(aOldDocument != nsnull, "no old document");

  if (mDestroyed)
    return;

  // Hold a ref to the binding so it won't die when we remove it from our
  // table.
  nsRefPtr<nsXBLBinding> binding = GetBinding(aContent);
  if (aContent->HasFlag(NODE_IS_INSERTION_PARENT)) {
    nsRefPtr<nsXBLBinding> parentBinding = GetBinding(aContent->GetBindingParent());
    if (parentBinding) {
      parentBinding->RemoveInsertionParent(aContent);
      // Clear insertion parent only if we don't have a binding which
      // marked content to be an insertion parent. See also SetBinding().
      if (!binding || !binding->HasInsertionParent(aContent)) {
        RemoveInsertionParent(aContent);
        aContent->UnsetFlags(NODE_IS_INSERTION_PARENT);
      }
    }
  }

  if (binding) {
    binding->ChangeDocument(aOldDocument, nsnull);
    SetBinding(aContent, nsnull);
  }

  // Clear out insertion parents and content lists.
  SetInsertionParent(aContent, nsnull);
  SetContentListFor(aContent, nsnull);
  SetAnonymousNodesFor(aContent, nsnull);
}

nsIAtom*
nsBindingManager::ResolveTag(nsIContent* aContent, PRInt32* aNameSpaceID)
{
  nsXBLBinding *binding = GetBinding(aContent);
  
  if (binding) {
    nsIAtom* base = binding->GetBaseTag(aNameSpaceID);

    if (base) {
      return base;
    }
  }

  *aNameSpaceID = aContent->GetNameSpaceID();
  return aContent->Tag();
}

nsresult
nsBindingManager::GetContentListFor(nsIContent* aContent, nsIDOMNodeList** aResult)
{
  NS_IF_ADDREF(*aResult = GetContentListFor(aContent));
  return NS_OK;
}

nsINodeList*
nsBindingManager::GetContentListFor(nsIContent* aContent)
{ 
  nsINodeList* result = nsnull;

  if (mContentListTable.ops) {
    result = static_cast<nsAnonymousContentList*>
      (LookupObject(mContentListTable, aContent));
  }

  if (!result) {
    result = aContent->GetChildNodesList();
  }

  return result;
}

nsresult
nsBindingManager::SetContentListFor(nsIContent* aContent,
                                    nsInsertionPointList* aList)
{
  if (mDestroyed) {
    return NS_OK;
  }

  nsAnonymousContentList* contentList = nsnull;
  if (aList) {
    contentList = new nsAnonymousContentList(aList);
    if (!contentList) {
      delete aList;
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  return SetOrRemoveObject(mContentListTable, aContent, contentList);
}

PRBool
nsBindingManager::HasContentListFor(nsIContent* aContent)
{
  return mContentListTable.ops && LookupObject(mContentListTable, aContent);
}

nsINodeList*
nsBindingManager::GetAnonymousNodesInternal(nsIContent* aContent,
                                            PRBool* aIsAnonymousContentList)
{ 
  nsINodeList* result = nsnull;
  if (mAnonymousNodesTable.ops) {
    result = static_cast<nsAnonymousContentList*>
                        (LookupObject(mAnonymousNodesTable, aContent));
  }

  if (!result) {
    *aIsAnonymousContentList = PR_FALSE;
    nsXBLBinding *binding = GetBinding(aContent);
    if (binding) {
      result = binding->GetAnonymousNodes();
    }
  } else
    *aIsAnonymousContentList = PR_TRUE;

  return result;
}

nsresult
nsBindingManager::GetAnonymousNodesFor(nsIContent* aContent,
                                       nsIDOMNodeList** aResult)
{
  PRBool dummy;
  NS_IF_ADDREF(*aResult = GetAnonymousNodesInternal(aContent, &dummy));
  return NS_OK;
}

nsINodeList*
nsBindingManager::GetAnonymousNodesFor(nsIContent* aContent)
{
  PRBool dummy;
  return GetAnonymousNodesInternal(aContent, &dummy);
}

nsresult
nsBindingManager::SetAnonymousNodesFor(nsIContent* aContent,
                                       nsInsertionPointList* aList)
{
  if (mDestroyed) {
    return NS_OK;
  }

  nsAnonymousContentList* contentList = nsnull;
  if (aList) {
    contentList = new nsAnonymousContentList(aList);
    if (!contentList) {
      delete aList;
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  return SetOrRemoveObject(mAnonymousNodesTable, aContent, contentList);
}

nsINodeList*
nsBindingManager::GetXBLChildNodesInternal(nsIContent* aContent,
                                           PRBool* aIsAnonymousContentList)
{
  PRUint32 length;

  // Retrieve the anonymous content that we should build.
  nsINodeList* result = GetAnonymousNodesInternal(aContent,
                                                  aIsAnonymousContentList);
  if (result) {
    result->GetLength(&length);
    if (length == 0)
      result = nsnull;
  }
    
  // We may have an altered list of children from XBL insertion points.
  // If we don't have any anonymous kids, we next check to see if we have 
  // insertion points.
  if (!result) {
    if (mContentListTable.ops) {
      result = static_cast<nsAnonymousContentList*>
                          (LookupObject(mContentListTable, aContent));
      *aIsAnonymousContentList = PR_TRUE;
    }
  }

  return result;
}

nsresult
nsBindingManager::GetXBLChildNodesFor(nsIContent* aContent, nsIDOMNodeList** aResult)
{
  NS_IF_ADDREF(*aResult = GetXBLChildNodesFor(aContent));
  return NS_OK;
}

nsINodeList*
nsBindingManager::GetXBLChildNodesFor(nsIContent* aContent)
{
  PRBool dummy;
  return GetXBLChildNodesInternal(aContent, &dummy);
}

nsIContent*
nsBindingManager::GetInsertionPoint(nsIContent* aParent,
                                    const nsIContent* aChild,
                                    PRUint32* aIndex)
{
  nsXBLBinding *binding = GetBinding(aParent);
  return binding ? binding->GetInsertionPoint(aChild, aIndex) : nsnull;
}

nsIContent*
nsBindingManager::GetSingleInsertionPoint(nsIContent* aParent,
                                          PRUint32* aIndex,
                                          PRBool* aMultipleInsertionPoints)
{
  nsXBLBinding *binding = GetBinding(aParent);
  if (binding)
    return binding->GetSingleInsertionPoint(aIndex, aMultipleInsertionPoints);

  *aMultipleInsertionPoints = PR_FALSE;
  return nsnull;
}

nsresult
nsBindingManager::AddLayeredBinding(nsIContent* aContent, nsIURI* aURL,
                                    nsIPrincipal* aOriginPrincipal)
{
  // First we need to load our binding.
  nsresult rv;
  nsCOMPtr<nsIXBLService> xblService = 
           do_GetService("@mozilla.org/xbl;1", &rv);
  if (!xblService)
    return rv;

  // Load the bindings.
  nsRefPtr<nsXBLBinding> binding;
  PRBool dummy;
  xblService->LoadBindings(aContent, aURL, aOriginPrincipal, PR_TRUE,
                           getter_AddRefs(binding), &dummy);
  if (binding) {
    AddToAttachedQueue(binding);
    ProcessAttachedQueue();
  }

  return NS_OK;
}

nsresult
nsBindingManager::RemoveLayeredBinding(nsIContent* aContent, nsIURI* aURL)
{
  // Hold a ref to the binding so it won't die when we remove it from our table
  nsRefPtr<nsXBLBinding> binding = GetBinding(aContent);
  
  if (!binding) {
    return NS_OK;
  }

  // For now we can only handle removing a binding if it's the only one
  NS_ENSURE_FALSE(binding->GetBaseBinding(), NS_ERROR_FAILURE);

  // Make sure that the binding has the URI that is requested to be removed
  if (!binding->PrototypeBinding()->CompareBindingURI(aURL)) {
    return NS_OK;
  }

  // Make sure it isn't a style binding
  if (binding->IsStyleBinding()) {
    return NS_OK;
  }

  // Hold strong ref in case removing the binding tries to close the
  // window or something.
  // XXXbz should that be ownerdoc?  Wouldn't we need a ref to the
  // currentdoc too?  What's the one that should be passed to
  // ChangeDocument?
  nsCOMPtr<nsIDocument> doc = aContent->GetOwnerDoc();
  NS_ASSERTION(doc, "No owner document?");
  
  // Finally remove the binding...
  // XXXbz this doesn't remove the implementation!  Should fix!  Until
  // then we need the explicit UnhookEventHandlers here.
  binding->UnhookEventHandlers();
  binding->ChangeDocument(doc, nsnull);
  SetBinding(aContent, nsnull);
  binding->MarkForDeath();
  
  // ...and recreate its frames. We need to do this since the frames may have
  // been removed and style may have changed due to the removal of the
  // anonymous children.
  // XXXbz this should be using the current doc (if any), not the owner doc.
  nsIPresShell *presShell = doc->GetShell();
  NS_ENSURE_TRUE(presShell, NS_ERROR_FAILURE);

  return presShell->RecreateFramesFor(aContent);;
}

nsresult
nsBindingManager::LoadBindingDocument(nsIDocument* aBoundDoc,
                                      nsIURI* aURL,
                                      nsIPrincipal* aOriginPrincipal)
{
  NS_PRECONDITION(aURL, "Must have a URI to load!");
  
  // First we need to load our binding.
  nsresult rv;
  nsCOMPtr<nsIXBLService> xblService = 
           do_GetService("@mozilla.org/xbl;1", &rv);
  if (!xblService)
    return rv;

  // Load the binding doc.
  nsRefPtr<nsXBLDocumentInfo> info;
  xblService->LoadBindingDocumentInfo(nsnull, aBoundDoc, aURL,
                                      aOriginPrincipal, PR_TRUE,
                                      getter_AddRefs(info));
  if (!info)
    return NS_ERROR_FAILURE;

  return NS_OK;
}

nsresult
nsBindingManager::AddToAttachedQueue(nsXBLBinding* aBinding)
{
  if (!mAttachedStack.AppendElement(aBinding))
    return NS_ERROR_OUT_OF_MEMORY;

  // If we're in the middle of processing our queue already, don't
  // bother posting the event.
  if (!mProcessingAttachedStack && !mProcessAttachedQueueEvent) {
    PostProcessAttachedQueueEvent();
  }

  return NS_OK;

}

void
nsBindingManager::PostProcessAttachedQueueEvent()
{
  mProcessAttachedQueueEvent =
    NS_NewRunnableMethod(this, &nsBindingManager::DoProcessAttachedQueue);
  nsresult rv = NS_DispatchToCurrentThread(mProcessAttachedQueueEvent);
  if (NS_SUCCEEDED(rv) && mDocument) {
    mDocument->BlockOnload();
  }
}

void
nsBindingManager::DoProcessAttachedQueue()
{
  if (!mProcessingAttachedStack) {
    ProcessAttachedQueue();

    NS_ASSERTION(mAttachedStack.Length() == 0,
               "Shouldn't have pending bindings!");
  
    mProcessAttachedQueueEvent = nsnull;
  } else {
    // Someone's doing event processing from inside a constructor.
    // They're evil, but we'll fight back!  Just poll on them being
    // done and repost the attached queue event.
    PostProcessAttachedQueueEvent();
  }

  // No matter what, unblock onload for the event that's fired.
  if (mDocument) {
    // Hold a strong reference while calling UnblockOnload since that might
    // run script.
    nsCOMPtr<nsIDocument> doc = mDocument;
    doc->UnblockOnload(PR_TRUE);
  }
}

void
nsBindingManager::ProcessAttachedQueue(PRUint32 aSkipSize)
{
  if (mProcessingAttachedStack || mAttachedStack.Length() <= aSkipSize)
    return;

  NS_TIME_FUNCTION;

  mProcessingAttachedStack = PR_TRUE;

  // Excute constructors. Do this from high index to low
  while (mAttachedStack.Length() > aSkipSize) {
    PRUint32 lastItem = mAttachedStack.Length() - 1;
    nsRefPtr<nsXBLBinding> binding = mAttachedStack.ElementAt(lastItem);
    mAttachedStack.RemoveElementAt(lastItem);
    if (binding) {
      binding->ExecuteAttachedHandler();
    }
  }

  // If NodeWillBeDestroyed has run we don't want to clobber
  // mProcessingAttachedStack set there.
  if (mDocument) {
    mProcessingAttachedStack = PR_FALSE;
  }

  NS_ASSERTION(mAttachedStack.Length() == aSkipSize, "How did we get here?");
  
  mAttachedStack.Compact();
}

// Keep bindings and bound elements alive while executing detached handlers.
struct BindingTableReadClosure
{
  nsCOMArray<nsIContent> mBoundElements;
  nsBindingList          mBindings;
};

static PLDHashOperator
AccumulateBindingsToDetach(nsISupports *aKey, nsXBLBinding *aBinding,
                           void* aClosure)
 {
  BindingTableReadClosure* closure =
    static_cast<BindingTableReadClosure*>(aClosure);
  if (aBinding && closure->mBindings.AppendElement(aBinding)) {
    if (!closure->mBoundElements.AppendObject(aBinding->GetBoundElement())) {
      closure->mBindings.RemoveElementAt(closure->mBindings.Length() - 1);
    }
  }
  return PL_DHASH_NEXT;
}

void
nsBindingManager::ExecuteDetachedHandlers()
{
  // Walk our hashtable of bindings.
  if (mBindingTable.IsInitialized()) {
    BindingTableReadClosure closure;
    mBindingTable.EnumerateRead(AccumulateBindingsToDetach, &closure);
    PRUint32 i, count = closure.mBindings.Length();
    for (i = 0; i < count; ++i) {
      closure.mBindings[i]->ExecuteDetachedHandler();
    }
  }
}

nsresult
nsBindingManager::PutXBLDocumentInfo(nsXBLDocumentInfo* aDocumentInfo)
{
  NS_PRECONDITION(aDocumentInfo, "Must have a non-null documentinfo!");
  
  NS_ENSURE_TRUE(mDocumentTable.IsInitialized() || mDocumentTable.Init(16),
                 NS_ERROR_OUT_OF_MEMORY);

  NS_ENSURE_TRUE(mDocumentTable.Put(aDocumentInfo->DocumentURI(),
                                    aDocumentInfo),
                 NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

void
nsBindingManager::RemoveXBLDocumentInfo(nsXBLDocumentInfo* aDocumentInfo)
{
  if (mDocumentTable.IsInitialized()) {
    mDocumentTable.Remove(aDocumentInfo->DocumentURI());
  }
}

nsXBLDocumentInfo*
nsBindingManager::GetXBLDocumentInfo(nsIURI* aURL)
{
  if (!mDocumentTable.IsInitialized())
    return nsnull;

  return mDocumentTable.GetWeak(aURL);
}

nsresult
nsBindingManager::PutLoadingDocListener(nsIURI* aURL, nsIStreamListener* aListener)
{
  NS_PRECONDITION(aListener, "Must have a non-null listener!");
  
  NS_ENSURE_TRUE(mLoadingDocTable.IsInitialized() || mLoadingDocTable.Init(16),
                 NS_ERROR_OUT_OF_MEMORY);
  
  NS_ENSURE_TRUE(mLoadingDocTable.Put(aURL, aListener),
                 NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

nsIStreamListener*
nsBindingManager::GetLoadingDocListener(nsIURI* aURL)
{
  if (!mLoadingDocTable.IsInitialized())
    return nsnull;

  return mLoadingDocTable.GetWeak(aURL);
}

void
nsBindingManager::RemoveLoadingDocListener(nsIURI* aURL)
{
  if (mLoadingDocTable.IsInitialized()) {
    mLoadingDocTable.Remove(aURL);
  }
}

static PLDHashOperator
MarkForDeath(nsISupports *aKey, nsXBLBinding *aBinding, void* aClosure)
{
  if (aBinding->MarkedForDeath())
    return PL_DHASH_NEXT; // Already marked for death.

  nsCAutoString path;
  aBinding->PrototypeBinding()->DocURI()->GetPath(path);

  if (!strncmp(path.get(), "/skin", 5))
    aBinding->MarkForDeath();
  
  return PL_DHASH_NEXT;
}

void
nsBindingManager::FlushSkinBindings()
{
  if (mBindingTable.IsInitialized())
    mBindingTable.EnumerateRead(MarkForDeath, nsnull);
}

// Used below to protect from recurring in QI calls through XPConnect.
struct AntiRecursionData {
  nsIContent* element; 
  REFNSIID iid; 
  AntiRecursionData* next;

  AntiRecursionData(nsIContent* aElement, 
                    REFNSIID aIID, 
                    AntiRecursionData* aNext)
    : element(aElement), iid(aIID), next(aNext) {}
};

nsresult
nsBindingManager::GetBindingImplementation(nsIContent* aContent, REFNSIID aIID,
                                           void** aResult)
{
  *aResult = nsnull;
  nsXBLBinding *binding = GetBinding(aContent);
  if (binding) {
    // The binding should not be asked for nsISupports
    NS_ASSERTION(!aIID.Equals(NS_GET_IID(nsISupports)), "Asking a binding for nsISupports");
    if (binding->ImplementsInterface(aIID)) {
      nsCOMPtr<nsIXPConnectWrappedJS> wrappedJS = GetWrappedJS(aContent);

      if (wrappedJS) {
        // Protect from recurring in QI calls through XPConnect. 
        // This can happen when a second binding is being resolved.
        // At that point a wrappedJS exists, but it doesn't yet know about
        // the iid we are asking for. So, without this protection, 
        // AggregatedQueryInterface would end up recurring back into itself
        // through this code. 
        //
        // With this protection, when we detect the recursion we return 
        // NS_NOINTERFACE in the inner call. The outer call will then fall 
        // through (see below) and build a new chained wrappedJS for the iid.
        //
        // We're careful to not assume that only one direct nesting can occur
        // because there is a call into JS in the middle and we can't assume 
        // that this code won't be reached by some more complex nesting path.
        //
        // NOTE: We *assume* this is single threaded, so we can use a
        // static linked list to do the check.

        static AntiRecursionData* list = nsnull;

        for (AntiRecursionData* p = list; p; p = p->next) {
          if (p->element == aContent && p->iid.Equals(aIID)) {
            *aResult = nsnull;
            return NS_NOINTERFACE;
          }
        }

        AntiRecursionData item(aContent, aIID, list);
        list = &item;

        nsresult rv = wrappedJS->AggregatedQueryInterface(aIID, aResult);
        
        list = item.next;
        
        if (*aResult)
          return rv;
        
        // No result was found, so this must be another XBL interface.
        // Fall through to create a new wrapper.
      }

      // We have never made a wrapper for this implementation.
      // Create an XPC wrapper for the script object and hand it back.

      nsIDocument* doc = aContent->GetOwnerDoc();
      if (!doc)
        return NS_NOINTERFACE;

      nsIScriptGlobalObject *global = doc->GetScriptGlobalObject();
      if (!global)
        return NS_NOINTERFACE;

      nsIScriptContext *context = global->GetContext();
      if (!context)
        return NS_NOINTERFACE;

      JSContext* jscontext = (JSContext*)context->GetNativeContext();
      if (!jscontext)
        return NS_NOINTERFACE;

      nsIXPConnect *xpConnect = nsContentUtils::XPConnect();

      nsCOMPtr<nsIXPConnectWrappedNative> wrapper;
      xpConnect->GetWrappedNativeOfNativeObject(jscontext,
                                                global->GetGlobalJSObject(),
                                                aContent,
                                                NS_GET_IID(nsISupports),
                                                getter_AddRefs(wrapper));
      NS_ENSURE_TRUE(wrapper, NS_NOINTERFACE);

      JSObject* jsobj = nsnull;

      wrapper->GetJSObject(&jsobj);
      NS_ENSURE_TRUE(jsobj, NS_NOINTERFACE);

      nsresult rv = xpConnect->WrapJSAggregatedToNative(aContent, jscontext,
                                                        jsobj, aIID, aResult);
      if (NS_FAILED(rv))
        return rv;

      // We successfully created a wrapper.  We will own this wrapper for as long as the binding remains
      // alive.  At the time the binding is cleared out of the bindingManager, we will remove the wrapper
      // from the bindingManager as well.
      nsISupports* supp = static_cast<nsISupports*>(*aResult);
      wrappedJS = do_QueryInterface(supp);
      SetWrappedJS(aContent, wrappedJS);

      return rv;
    }
  }
  
  *aResult = nsnull;
  return NS_NOINTERFACE;
}

nsresult
nsBindingManager::WalkRules(nsIStyleRuleProcessor::EnumFunc aFunc,
                            RuleProcessorData* aData,
                            PRBool* aCutOffInheritance)
{
  *aCutOffInheritance = PR_FALSE;
  
  NS_ASSERTION(aData->mElement, "How did that happen?");

  // Walk the binding scope chain, starting with the binding attached to our
  // content, up till we run out of scopes or we get cut off.
  nsIContent *content = aData->mElement;
  
  do {
    nsXBLBinding *binding = GetBinding(content);
    if (binding) {
      aData->mScopedRoot = content;
      binding->WalkRules(aFunc, aData);
      // If we're not looking at our original content, allow the binding to cut
      // off style inheritance
      if (content != aData->mElement) {
        if (!binding->InheritsStyle()) {
          // Go no further; we're not inheriting style from anything above here
          break;
        }
      }
    }

    if (content->IsRootOfNativeAnonymousSubtree()) {
      break; // Deliberately cut off style inheritance here.
    }

    content = content->GetBindingParent();
  } while (content);

  // If "content" is non-null that means we cut off inheritance at some point
  // in the loop.
  *aCutOffInheritance = (content != nsnull);

  // Null out the scoped root that we set repeatedly
  aData->mScopedRoot = nsnull;

  return NS_OK;
}

typedef nsTHashtable<nsVoidPtrHashKey> RuleProcessorSet;

static PLDHashOperator
EnumRuleProcessors(nsISupports *aKey, nsXBLBinding *aBinding, void* aClosure)
{
  RuleProcessorSet *set = static_cast<RuleProcessorSet*>(aClosure);
  for (nsXBLBinding *binding = aBinding; binding;
       binding = binding->GetBaseBinding()) {
    nsIStyleRuleProcessor *ruleProc =
      binding->PrototypeBinding()->GetRuleProcessor();
    if (ruleProc) {
      if (!set->IsInitialized() && !set->Init(16))
        return PL_DHASH_STOP;
      set->PutEntry(ruleProc);
    }
  }
  return PL_DHASH_NEXT;
}

struct WalkAllRulesData {
  nsIStyleRuleProcessor::EnumFunc mFunc;
  RuleProcessorData* mData;
};

static PLDHashOperator
EnumWalkAllRules(nsVoidPtrHashKey *aKey, void* aClosure)
{
  nsIStyleRuleProcessor *ruleProcessor =
    static_cast<nsIStyleRuleProcessor*>(const_cast<void*>(aKey->GetKey()));
  WalkAllRulesData *data = static_cast<WalkAllRulesData*>(aClosure);

  (*(data->mFunc))(ruleProcessor, data->mData);

  return PL_DHASH_NEXT;
}

void
nsBindingManager::WalkAllRules(nsIStyleRuleProcessor::EnumFunc aFunc,
                               RuleProcessorData* aData)
{
  if (!mBindingTable.IsInitialized())
    return;

  RuleProcessorSet set;
  mBindingTable.EnumerateRead(EnumRuleProcessors, &set);
  if (!set.IsInitialized())
    return;

  WalkAllRulesData data = { aFunc, aData };
  set.EnumerateEntries(EnumWalkAllRules, &data);
}

struct MediumFeaturesChangedData {
  nsPresContext *mPresContext;
  PRBool *mRulesChanged;
};

static PLDHashOperator
EnumMediumFeaturesChanged(nsVoidPtrHashKey *aKey, void* aClosure)
{
  nsIStyleRuleProcessor *ruleProcessor =
    static_cast<nsIStyleRuleProcessor*>(const_cast<void*>(aKey->GetKey()));
  MediumFeaturesChangedData *data =
    static_cast<MediumFeaturesChangedData*>(aClosure);

  PRBool thisChanged = ruleProcessor->MediumFeaturesChanged(data->mPresContext);
  *data->mRulesChanged = *data->mRulesChanged || thisChanged;

  return PL_DHASH_NEXT;
}

nsresult
nsBindingManager::MediumFeaturesChanged(nsPresContext* aPresContext,
                                        PRBool* aRulesChanged)
{
  *aRulesChanged = PR_FALSE;
  if (!mBindingTable.IsInitialized())
    return NS_OK;

  RuleProcessorSet set;
  mBindingTable.EnumerateRead(EnumRuleProcessors, &set);
  if (!set.IsInitialized())
    return NS_OK;

  MediumFeaturesChangedData data = { aPresContext, aRulesChanged };
  set.EnumerateEntries(EnumMediumFeaturesChanged, &data);
  return NS_OK;
}

static PLDHashOperator
EnumAppendAllSheets(nsISupports *aKey, nsXBLBinding *aBinding, void* aClosure)
{
  nsTArray<nsCSSStyleSheet*>* array =
    static_cast<nsTArray<nsCSSStyleSheet*>*>(aClosure);
  for (nsXBLBinding *binding = aBinding; binding;
       binding = binding->GetBaseBinding()) {
    nsXBLPrototypeResources::sheet_array_type* sheets =
      binding->PrototypeBinding()->GetStyleSheets();
    if (sheets) {
      // Copy from nsTArray<nsRefPtr<nsCSSStyleSheet> > to
      // nsTArray<nsCSSStyleSheet*>.
      array->AppendElements(*sheets);
    }
  }
  return PL_DHASH_NEXT;
}

void
nsBindingManager::AppendAllSheets(nsTArray<nsCSSStyleSheet*>& aArray)
{
  if (!mBindingTable.IsInitialized())
    return;

  mBindingTable.EnumerateRead(EnumAppendAllSheets, &aArray);
}

nsIContent*
nsBindingManager::GetNestedInsertionPoint(nsIContent* aParent,
                                          const nsIContent* aChild)
{
  // Check to see if the content is anonymous.
  if (aChild->GetBindingParent() == aParent)
    return nsnull; // It is anonymous. Don't use the insertion point, since that's only
                   // for the explicit kids.

  PRUint32 index;
  nsIContent *insertionElement = GetInsertionPoint(aParent, aChild, &index);
  if (insertionElement && insertionElement != aParent) {
    // See if we nest even further in.
    nsIContent* nestedPoint = GetNestedInsertionPoint(insertionElement, aChild);
    if (nestedPoint)
      insertionElement = nestedPoint;
  }

  return insertionElement;
}

nsIContent*
nsBindingManager::GetNestedSingleInsertionPoint(nsIContent* aParent,
                                                PRBool* aMultipleInsertionPoints)
{
  *aMultipleInsertionPoints = PR_FALSE;
  
  PRUint32 index;
  nsIContent *insertionElement =
    GetSingleInsertionPoint(aParent, &index, aMultipleInsertionPoints);
  if (*aMultipleInsertionPoints) {
    return nsnull;
  }
  if (insertionElement && insertionElement != aParent) {
    // See if we nest even further in.
    nsIContent* nestedPoint =
      GetNestedSingleInsertionPoint(insertionElement,
                                    aMultipleInsertionPoints);
    if (nestedPoint)
      insertionElement = nestedPoint;
  }

  return insertionElement;
}

nsXBLInsertionPoint*
nsBindingManager::FindInsertionPointAndIndex(nsIContent* aContainer,
                                             nsIContent* aInsertionParent,
                                             PRUint32 aIndexInContainer,
                                             PRInt32 aAppend,
                                             PRInt32* aInsertionIndex)
{
  PRBool isAnonymousContentList;
  nsINodeList* nodeList =
    GetXBLChildNodesInternal(aInsertionParent, &isAnonymousContentList);
  if (!nodeList || !isAnonymousContentList) {
    return nsnull;
  }

  // Find a non-pseudo-insertion point and just jam ourselves in.  This is
  // not 100% correct, since there might be multiple insertion points under
  // this insertion parent, and we should really be using the one that
  // matches our content...  Hack city, baby.
  nsAnonymousContentList* contentList =
    static_cast<nsAnonymousContentList*>(nodeList);

  PRInt32 count = contentList->GetInsertionPointCount();
  for (PRInt32 i = 0; i < count; i++) {
    nsXBLInsertionPoint* point = contentList->GetInsertionPointAt(i);
    if (point->GetInsertionIndex() != -1) {
      // We're real. Jam the kid in.

      // Find the right insertion spot.  Can't just insert in the insertion
      // point at aIndexInContainer since the point may contain anonymous
      // content, not all of aContainer's kids, etc.  So find the last
      // child of aContainer that comes before aIndexInContainer and is in
      // the insertion point and insert right after it.
      PRInt32 pointSize = point->ChildCount();
      for (PRInt32 parentIndex = aIndexInContainer - 1; parentIndex >= 0;
           --parentIndex) {
        nsIContent* currentSibling = aContainer->GetChildAt(parentIndex);
        for (PRInt32 pointIndex = pointSize - 1; pointIndex >= 0;
             --pointIndex) {
          if (point->ChildAt(pointIndex) == currentSibling) {
            *aInsertionIndex = pointIndex + 1;
            return point;
          }
        }
      }

      // None of our previous siblings are in here... just stick
      // ourselves in at the end of the insertion point if we're
      // appending, and at the beginning otherwise.            
      // XXXbz if we ever start doing the filter thing right, this may be no
      // good, since we may _still_ have anonymous kids in there and may need
      // to get the ordering with those right.  In fact, this is even wrong
      // without the filter thing for nested insertion points, since they might
      // contain anonymous content that needs to come after all explicit
      // kids... but we have no way to know that here easily.
      if (aAppend) {
        *aInsertionIndex = pointSize;
      } else {
        *aInsertionIndex = 0;
      }
      return point;
    }
  }

  return nsnull;  
}

void
nsBindingManager::ContentAppended(nsIDocument* aDocument,
                                  nsIContent* aContainer,
                                  nsIContent* aFirstNewContent,
                                  PRInt32     aNewIndexInContainer)
{
  if (aNewIndexInContainer != -1 &&
      (mContentListTable.ops || mAnonymousNodesTable.ops)) {
    // It's not anonymous.
    NS_ASSERTION(aNewIndexInContainer >= 0, "Bogus index");

    PRBool multiple;
    nsIContent* ins = GetNestedSingleInsertionPoint(aContainer, &multiple);

    if (multiple) {
      // Do each kid individually
      PRInt32 childCount = aContainer->GetChildCount();
      for (PRInt32 idx = aNewIndexInContainer; idx < childCount; ++idx) {
        HandleChildInsertion(aContainer, aContainer->GetChildAt(idx),
                             idx, PR_TRUE);
      }
    }
    else if (ins) {
      PRInt32 insertionIndex;
      nsXBLInsertionPoint* point =
        FindInsertionPointAndIndex(aContainer, ins, aNewIndexInContainer,
                                   PR_TRUE, &insertionIndex);
      if (point) {
        PRInt32 childCount = aContainer->GetChildCount();
        for (PRInt32 j = aNewIndexInContainer; j < childCount;
             j++, insertionIndex++) {
          nsIContent* child = aContainer->GetChildAt(j);
          point->InsertChildAt(insertionIndex, child);
          SetInsertionParent(child, ins);
        }
      }
    }
  }
}

void
nsBindingManager::ContentInserted(nsIDocument* aDocument,
                                  nsIContent* aContainer,
                                  nsIContent* aChild,
                                  PRInt32 aIndexInContainer)
{
  if (aIndexInContainer != -1 &&
      (mContentListTable.ops || mAnonymousNodesTable.ops)) {
    // It's not anonymous.
    NS_ASSERTION(aIndexInContainer >= 0, "Bogus index");
    HandleChildInsertion(aContainer, aChild, aIndexInContainer, PR_FALSE);
  }
}

static void
RemoveChildFromInsertionPoint(nsAnonymousContentList* aInsertionPointList,
                              nsIContent* aChild,
                              PRBool aRemoveFromPseudoPoints)
{
  // We need to find the insertion point that contains aChild and remove it
  // from that insertion point.  Sadly, we don't know which point it is, or
  // when we've hit it, but just trying to remove from all the pseudo or
  // non-pseudo insertion points, depending on the value of
  // aRemoveFromPseudoPoints, should work.

  // XXXbz nsXBLInsertionPoint::RemoveChild could return whether it
  // removed something.  Wouldn't that let us short-circuit the walk?
  // Or can a child be in multiple insertion points?  I wouldn't think
  // so...
  PRInt32 count = aInsertionPointList->GetInsertionPointCount();
  for (PRInt32 i = 0; i < count; i++) {
    nsXBLInsertionPoint* point =
      aInsertionPointList->GetInsertionPointAt(i);
    if ((point->GetInsertionIndex() == -1) == aRemoveFromPseudoPoints) {
      point->RemoveChild(aChild);
    }
  }
}

void
nsBindingManager::ContentRemoved(nsIDocument* aDocument,
                                 nsIContent* aContainer,
                                 nsIContent* aChild,
                                 PRInt32 aIndexInContainer,
                                 nsIContent* aPreviousSibling)
{
  if (aContainer && aIndexInContainer != -1 &&
      (mContentListTable.ops || mAnonymousNodesTable.ops)) {
    // It's not anonymous
    nsCOMPtr<nsIContent> point = GetNestedInsertionPoint(aContainer, aChild);

    if (point) {
      PRBool isAnonymousContentList;
      nsCOMPtr<nsIDOMNodeList> nodeList =
        GetXBLChildNodesInternal(point, &isAnonymousContentList);
      
      if (nodeList && isAnonymousContentList) {
        // Find a non-pseudo-insertion point and remove ourselves.
        RemoveChildFromInsertionPoint(static_cast<nsAnonymousContentList*>
                                        (static_cast<nsIDOMNodeList*>
                                                    (nodeList)),
                                      aChild,
                                      PR_FALSE);
        SetInsertionParent(aChild, nsnull);
      }

      // Also remove from the list in mContentListTable, if any.
      if (mContentListTable.ops) {
        nsCOMPtr<nsIDOMNodeList> otherNodeList =
          static_cast<nsAnonymousContentList*>
                     (LookupObject(mContentListTable, point));
        if (otherNodeList && otherNodeList != nodeList) {
          // otherNodeList is always anonymous
          RemoveChildFromInsertionPoint(static_cast<nsAnonymousContentList*>
                                        (static_cast<nsIDOMNodeList*>
                                                    (otherNodeList)),
                                        aChild,
                                        PR_FALSE);
        }
      }
    }

    // Whether the child has a nested insertion point or not, aContainer might
    // have insertion points under it.  If that's the case, we need to remove
    // aChild from the pseudo insertion point it's in.
    if (mContentListTable.ops) {
      nsAnonymousContentList* insertionPointList =
        static_cast<nsAnonymousContentList*>(LookupObject(mContentListTable,
                                                          aContainer));
      if (insertionPointList) {
        RemoveChildFromInsertionPoint(insertionPointList, aChild, PR_TRUE);
      }
    }
  }
}

void
nsBindingManager::DropDocumentReference()
{
  mDestroyed = PR_TRUE;

  // Make sure to not run any more XBL constructors
  mProcessingAttachedStack = PR_TRUE;
  if (mProcessAttachedQueueEvent) {
    mProcessAttachedQueueEvent->Revoke();
  }

  if (mContentListTable.ops)
    PL_DHashTableFinish(&(mContentListTable));
  mContentListTable.ops = nsnull;

  if (mAnonymousNodesTable.ops)
    PL_DHashTableFinish(&(mAnonymousNodesTable));
  mAnonymousNodesTable.ops = nsnull;

  if (mInsertionParentTable.ops)
    PL_DHashTableFinish(&(mInsertionParentTable));
  mInsertionParentTable.ops = nsnull;

  if (mBindingTable.IsInitialized())
    mBindingTable.Clear();

  mDocument = nsnull;
}

void
nsBindingManager::Traverse(nsIContent *aContent,
                           nsCycleCollectionTraversalCallback &cb)
{
  if (!aContent->HasFlag(NODE_MAY_BE_IN_BINDING_MNGR)) {
    return;
  }

  nsISupports *value;
  if (mInsertionParentTable.ops &&
      (value = LookupObject(mInsertionParentTable, aContent))) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[via binding manager] mInsertionParentTable key");
    cb.NoteXPCOMChild(aContent);
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[via binding manager] mInsertionParentTable value");
    cb.NoteXPCOMChild(value);
  }

  // XXXbz how exactly would NODE_MAY_BE_IN_BINDING_MNGR end up on non-elements?
  if (!aContent->IsElement()) {
    return;
  }

  nsXBLBinding *binding = GetBinding(aContent);
  if (binding) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[via binding manager] mBindingTable key");
    cb.NoteXPCOMChild(aContent);
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_PTR(binding, nsXBLBinding,
                                  "[via binding manager] mBindingTable value")
  }
  if (mContentListTable.ops &&
      (value = LookupObject(mContentListTable, aContent))) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[via binding manager] mContentListTable key");
    cb.NoteXPCOMChild(aContent);
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[via binding manager] mContentListTable value");
    cb.NoteXPCOMChild(value);
  }
  if (mAnonymousNodesTable.ops &&
      (value = LookupObject(mAnonymousNodesTable, aContent))) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[via binding manager] mAnonymousNodesTable key");
    cb.NoteXPCOMChild(aContent);
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[via binding manager] mAnonymousNodesTable value");
    cb.NoteXPCOMChild(value);
  }
  if (mWrapperTable.ops &&
      (value = LookupObject(mWrapperTable, aContent))) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[via binding manager] mWrapperTable key");
    cb.NoteXPCOMChild(aContent);
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "[via binding manager] mWrapperTable value");
    cb.NoteXPCOMChild(value);
  }
}

void
nsBindingManager::BeginOutermostUpdate()
{
  mAttachedStackSizeOnOutermost = mAttachedStack.Length();
}

void
nsBindingManager::EndOutermostUpdate()
{
  if (!mProcessingAttachedStack) {
    ProcessAttachedQueue(mAttachedStackSizeOnOutermost);
    mAttachedStackSizeOnOutermost = 0;
  }
}

void
nsBindingManager::HandleChildInsertion(nsIContent* aContainer,
                                       nsIContent* aChild,
                                       PRUint32 aIndexInContainer,
                                       PRBool aAppend)
{
  NS_PRECONDITION(aChild, "Must have child");
  NS_PRECONDITION(!aContainer ||
                  PRUint32(aContainer->IndexOf(aChild)) == aIndexInContainer,
                  "Child not at the right index?");

  nsIContent* ins = GetNestedInsertionPoint(aContainer, aChild);

  if (ins) {
    PRInt32 insertionIndex;
    nsXBLInsertionPoint* point =
      FindInsertionPointAndIndex(aContainer, ins, aIndexInContainer, aAppend,
                                 &insertionIndex);
    if (point) {
      point->InsertChildAt(insertionIndex, aChild);
      SetInsertionParent(aChild, ins);
    }
  }
}
