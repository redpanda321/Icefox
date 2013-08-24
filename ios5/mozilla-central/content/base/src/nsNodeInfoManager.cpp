/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * A class for handing out nodeinfos and ensuring sharing of them as needed.
 */

#include "nsNodeInfoManager.h"
#include "nsNodeInfo.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsIAtom.h"
#include "nsIDocument.h"
#include "nsIPrincipal.h"
#include "nsIURI.h"
#include "nsContentUtils.h"
#include "nsReadableUtils.h"
#include "nsGkAtoms.h"
#include "nsComponentManagerUtils.h"
#include "nsLayoutStatics.h"
#include "nsBindingManager.h"
#include "nsHashKeys.h"
#include "nsCCUncollectableMarker.h"

using namespace mozilla;

#ifdef MOZ_LOGGING
// so we can get logging even in release builds
#define FORCE_PR_LOG 1
#endif
#include "prlog.h"

#ifdef PR_LOGGING
static PRLogModuleInfo* gNodeInfoManagerLeakPRLog;
#endif

PLHashNumber
nsNodeInfoManager::GetNodeInfoInnerHashValue(const void *key)
{
  NS_ASSERTION(key, "Null key passed to nsNodeInfo::GetHashValue!");

  const nsINodeInfo::nsNodeInfoInner *node =
    reinterpret_cast<const nsINodeInfo::nsNodeInfoInner *>(key);

  if (node->mName) {
    // Ideally, we'd return node->mName->hash() here.  But that doesn't work at
    // the moment because node->mName->hash() is not the same as
    // HashString(*(node->mNameString)).  See bug 732815.
    return HashString(nsDependentAtomString(node->mName));
  }
  return HashString(*(node->mNameString));
}


PRIntn
nsNodeInfoManager::NodeInfoInnerKeyCompare(const void *key1, const void *key2)
{
  NS_ASSERTION(key1 && key2, "Null key passed to NodeInfoInnerKeyCompare!");

  const nsINodeInfo::nsNodeInfoInner *node1 =
    reinterpret_cast<const nsINodeInfo::nsNodeInfoInner *>(key1);
  const nsINodeInfo::nsNodeInfoInner *node2 =
    reinterpret_cast<const nsINodeInfo::nsNodeInfoInner *>(key2);

  if (node1->mPrefix != node2->mPrefix ||
      node1->mNamespaceID != node2->mNamespaceID ||
      node1->mNodeType != node2->mNodeType ||
      node1->mExtraName != node2->mExtraName) {
    return 0;
  }

  if (node1->mName) {
    if (node2->mName) {
      return (node1->mName == node2->mName);
    }
    return (node1->mName->Equals(*(node2->mNameString)));
  }
  if (node2->mName) {
    return (node2->mName->Equals(*(node1->mNameString)));
  }
  return (node1->mNameString->Equals(*(node2->mNameString)));
}


nsNodeInfoManager::nsNodeInfoManager()
  : mDocument(nsnull),
    mNonDocumentNodeInfos(0),
    mPrincipal(nsnull),
    mTextNodeInfo(nsnull),
    mCommentNodeInfo(nsnull),
    mDocumentNodeInfo(nsnull),
    mBindingManager(nsnull)
{
  nsLayoutStatics::AddRef();

#ifdef PR_LOGGING
  if (!gNodeInfoManagerLeakPRLog)
    gNodeInfoManagerLeakPRLog = PR_NewLogModule("NodeInfoManagerLeak");

  if (gNodeInfoManagerLeakPRLog)
    PR_LOG(gNodeInfoManagerLeakPRLog, PR_LOG_DEBUG,
           ("NODEINFOMANAGER %p created", this));
#endif

  mNodeInfoHash = PL_NewHashTable(32, GetNodeInfoInnerHashValue,
                                  NodeInfoInnerKeyCompare,
                                  PL_CompareValues, nsnull, nsnull);
}


nsNodeInfoManager::~nsNodeInfoManager()
{
  if (mNodeInfoHash)
    PL_HashTableDestroy(mNodeInfoHash);

  // Note: mPrincipal may be null here if we never got inited correctly
  NS_IF_RELEASE(mPrincipal);

  NS_IF_RELEASE(mBindingManager);

#ifdef PR_LOGGING
  if (gNodeInfoManagerLeakPRLog)
    PR_LOG(gNodeInfoManagerLeakPRLog, PR_LOG_DEBUG,
           ("NODEINFOMANAGER %p destroyed", this));
#endif

  nsLayoutStatics::Release();
}


NS_IMPL_CYCLE_COLLECTION_CLASS(nsNodeInfoManager)
NS_IMPL_CYCLE_COLLECTION_UNLINK_0(nsNodeInfoManager)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsNodeInfoManager)
  if (tmp->mDocument &&
      nsCCUncollectableMarker::InGeneration(cb,
                                            tmp->mDocument->GetMarkedCCGeneration())) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }
  if (tmp->mNonDocumentNodeInfos) {
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_RAWPTR(mDocument)
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_RAWPTR(mBindingManager)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsNodeInfoManager)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsNodeInfoManager)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsNodeInfoManager)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

nsresult
nsNodeInfoManager::Init(nsIDocument *aDocument)
{
  NS_ENSURE_TRUE(mNodeInfoHash, NS_ERROR_OUT_OF_MEMORY);

  NS_PRECONDITION(!mPrincipal,
                  "Being inited when we already have a principal?");
  nsresult rv = CallCreateInstance("@mozilla.org/nullprincipal;1",
                                   &mPrincipal);
  NS_ENSURE_TRUE(mPrincipal, rv);

  if (aDocument) {
    mBindingManager = new nsBindingManager(aDocument);
    NS_ENSURE_TRUE(mBindingManager, NS_ERROR_OUT_OF_MEMORY);

    NS_ADDREF(mBindingManager);
  }

  mDefaultPrincipal = mPrincipal;

  mDocument = aDocument;

#ifdef PR_LOGGING
  if (gNodeInfoManagerLeakPRLog)
    PR_LOG(gNodeInfoManagerLeakPRLog, PR_LOG_DEBUG,
           ("NODEINFOMANAGER %p Init document=%p", this, aDocument));
#endif

  return NS_OK;
}

// static
PRIntn
nsNodeInfoManager::DropNodeInfoDocument(PLHashEntry *he, PRIntn hashIndex, void *arg)
{
  static_cast<nsINodeInfo*>(he->value)->mDocument = nsnull;
  return HT_ENUMERATE_NEXT;
}

void
nsNodeInfoManager::DropDocumentReference()
{
  if (mBindingManager) {
    mBindingManager->DropDocumentReference();
  }

  // This is probably not needed anymore.
  PL_HashTableEnumerateEntries(mNodeInfoHash, DropNodeInfoDocument, nsnull);

  NS_ASSERTION(!mNonDocumentNodeInfos, "Shouldn't have non-document nodeinfos!");
  mDocument = nsnull;
}


already_AddRefed<nsINodeInfo>
nsNodeInfoManager::GetNodeInfo(nsIAtom *aName, nsIAtom *aPrefix,
                               PRInt32 aNamespaceID, PRUint16 aNodeType,
                               nsIAtom* aExtraName /* = nsnull */)
{
  CHECK_VALID_NODEINFO(aNodeType, aName, aNamespaceID, aExtraName);

  nsINodeInfo::nsNodeInfoInner tmpKey(aName, aPrefix, aNamespaceID, aNodeType,
                                      aExtraName);

  void *node = PL_HashTableLookup(mNodeInfoHash, &tmpKey);

  if (node) {
    nsINodeInfo* nodeInfo = static_cast<nsINodeInfo *>(node);

    NS_ADDREF(nodeInfo);

    return nodeInfo;
  }

  nsRefPtr<nsNodeInfo> newNodeInfo =
    nsNodeInfo::Create(aName, aPrefix, aNamespaceID, aNodeType, aExtraName,
                       this);
  NS_ENSURE_TRUE(newNodeInfo, nsnull);
  
  PLHashEntry *he;
  he = PL_HashTableAdd(mNodeInfoHash, &newNodeInfo->mInner, newNodeInfo);
  NS_ENSURE_TRUE(he, nsnull);

  // Have to do the swap thing, because already_AddRefed<nsNodeInfo>
  // doesn't cast to already_AddRefed<nsINodeInfo>
  ++mNonDocumentNodeInfos;
  if (mNonDocumentNodeInfos == 1) {
    NS_IF_ADDREF(mDocument);
  }

  nsNodeInfo *nodeInfo = nsnull;
  newNodeInfo.swap(nodeInfo);

  return nodeInfo;
}


nsresult
nsNodeInfoManager::GetNodeInfo(const nsAString& aName, nsIAtom *aPrefix,
                               PRInt32 aNamespaceID, PRUint16 aNodeType,
                               nsINodeInfo** aNodeInfo)
{
#ifdef DEBUG
  {
    nsCOMPtr<nsIAtom> nameAtom = do_GetAtom(aName);
    CHECK_VALID_NODEINFO(aNodeType, nameAtom, aNamespaceID, nsnull);
  }
#endif

  nsINodeInfo::nsNodeInfoInner tmpKey(aName, aPrefix, aNamespaceID, aNodeType);

  void *node = PL_HashTableLookup(mNodeInfoHash, &tmpKey);

  if (node) {
    nsINodeInfo* nodeInfo = static_cast<nsINodeInfo *>(node);

    NS_ADDREF(*aNodeInfo = nodeInfo);

    return NS_OK;
  }

  
  nsCOMPtr<nsIAtom> nameAtom = do_GetAtom(aName);
  NS_ENSURE_TRUE(nameAtom, NS_ERROR_OUT_OF_MEMORY);

  nsRefPtr<nsNodeInfo> newNodeInfo =
      nsNodeInfo::Create(nameAtom, aPrefix, aNamespaceID, aNodeType, nsnull,
                         this);
  NS_ENSURE_TRUE(newNodeInfo, NS_ERROR_OUT_OF_MEMORY);

  PLHashEntry *he;
  he = PL_HashTableAdd(mNodeInfoHash, &newNodeInfo->mInner, newNodeInfo);
  NS_ENSURE_TRUE(he, NS_ERROR_FAILURE);

  ++mNonDocumentNodeInfos;
  if (mNonDocumentNodeInfos == 1) {
    NS_IF_ADDREF(mDocument);
  }

  newNodeInfo.forget(aNodeInfo);

  return NS_OK;
}


nsresult
nsNodeInfoManager::GetNodeInfo(const nsAString& aName, nsIAtom *aPrefix,
                               const nsAString& aNamespaceURI,
                               PRUint16 aNodeType,
                               nsINodeInfo** aNodeInfo)
{
  PRInt32 nsid = kNameSpaceID_None;

  if (!aNamespaceURI.IsEmpty()) {
    nsresult rv = nsContentUtils::NameSpaceManager()->
      RegisterNameSpace(aNamespaceURI, nsid);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return GetNodeInfo(aName, aPrefix, nsid, aNodeType, aNodeInfo);
}

already_AddRefed<nsINodeInfo>
nsNodeInfoManager::GetTextNodeInfo()
{
  if (!mTextNodeInfo) {
    mTextNodeInfo = GetNodeInfo(nsGkAtoms::textTagName, nsnull,
                                kNameSpaceID_None,
                                nsIDOMNode::TEXT_NODE, nsnull).get();
  }
  else {
    NS_ADDREF(mTextNodeInfo);
  }

  return mTextNodeInfo;
}

already_AddRefed<nsINodeInfo>
nsNodeInfoManager::GetCommentNodeInfo()
{
  if (!mCommentNodeInfo) {
    mCommentNodeInfo = GetNodeInfo(nsGkAtoms::commentTagName, nsnull,
                                   kNameSpaceID_None,
                                   nsIDOMNode::COMMENT_NODE, nsnull).get();
  }
  else {
    NS_ADDREF(mCommentNodeInfo);
  }

  return mCommentNodeInfo;
}

already_AddRefed<nsINodeInfo>
nsNodeInfoManager::GetDocumentNodeInfo()
{
  if (!mDocumentNodeInfo) {
    NS_ASSERTION(mDocument, "Should have mDocument!");
    mDocumentNodeInfo = GetNodeInfo(nsGkAtoms::documentNodeName, nsnull,
                                    kNameSpaceID_None,
                                    nsIDOMNode::DOCUMENT_NODE, nsnull).get();
    --mNonDocumentNodeInfos;
    if (!mNonDocumentNodeInfos) {
      mDocument->Release(); // Don't set mDocument to null!
    }
  }
  else {
    NS_ADDREF(mDocumentNodeInfo);
  }

  return mDocumentNodeInfo;
}

void
nsNodeInfoManager::SetDocumentPrincipal(nsIPrincipal *aPrincipal)
{
  NS_RELEASE(mPrincipal);
  if (!aPrincipal) {
    aPrincipal = mDefaultPrincipal;
  }

  NS_ASSERTION(aPrincipal, "Must have principal by this point!");
  
  NS_ADDREF(mPrincipal = aPrincipal);
}

void
nsNodeInfoManager::RemoveNodeInfo(nsNodeInfo *aNodeInfo)
{
  NS_PRECONDITION(aNodeInfo, "Trying to remove null nodeinfo from manager!");

  if (aNodeInfo == mDocumentNodeInfo) {
    mDocumentNodeInfo = nsnull;
    mDocument = nsnull;
  } else {
    if (--mNonDocumentNodeInfos == 0) {
      if (mDocument) {
        // Note, whoever calls this method should keep NodeInfoManager alive,
        // even if mDocument gets deleted.
        mDocument->Release();
      }
    }
    // Drop weak reference if needed
    if (aNodeInfo == mTextNodeInfo) {
      mTextNodeInfo = nsnull;
    }
    else if (aNodeInfo == mCommentNodeInfo) {
      mCommentNodeInfo = nsnull;
    }
  }

#ifdef DEBUG
  bool ret =
#endif
  PL_HashTableRemove(mNodeInfoHash, &aNodeInfo->mInner);

  NS_POSTCONDITION(ret, "Can't find nsINodeInfo to remove!!!");
}
