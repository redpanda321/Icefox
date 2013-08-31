/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of DOM Core's nsIDOMDocumentType node.
 */

#ifndef nsDOMDocumentType_h
#define nsDOMDocumentType_h

#include "nsCOMPtr.h"
#include "nsIDOMDocumentType.h"
#include "nsIContent.h"
#include "nsGenericDOMDataNode.h"
#include "nsString.h"

// XXX DocumentType is currently implemented by inheriting the generic
// CharacterData object, even though DocumentType is not character
// data. This is done simply for convenience and should be changed if
// this restricts what should be done for character data.

class nsDOMDocumentTypeForward : public nsGenericDOMDataNode,
                                 public nsIDOMDocumentType
{
public:
  nsDOMDocumentTypeForward(already_AddRefed<nsINodeInfo> aNodeInfo)
    : nsGenericDOMDataNode(aNodeInfo)
  {
  }

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE
};

class nsDOMDocumentType : public nsDOMDocumentTypeForward
{
public:
  nsDOMDocumentType(already_AddRefed<nsINodeInfo> aNodeInfo,
                    const nsAString& aPublicId,
                    const nsAString& aSystemId,
                    const nsAString& aInternalSubset);

  virtual ~nsDOMDocumentType();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  // Forwarded by base class

  // nsIDOMDocumentType
  NS_DECL_NSIDOMDOCUMENTTYPE

  // nsINode
  virtual bool IsNodeOfType(uint32_t aFlags) const;
  virtual void GetNodeValueInternal(nsAString& aNodeValue)
  {
    SetDOMStringToNull(aNodeValue);
  }
  virtual void SetNodeValueInternal(const nsAString& aNodeValue,
                                    mozilla::ErrorResult& aError)
  {
  }

  // nsIContent overrides
  virtual const nsTextFragment* GetText();

  virtual nsGenericDOMDataNode* CloneDataNode(nsINodeInfo *aNodeInfo,
                                              bool aCloneText) const;

  virtual nsXPCClassInfo* GetClassInfo();

  virtual nsIDOMNode* AsDOMNode() { return this; }
protected:
  nsString mPublicId;
  nsString mSystemId;
  nsString mInternalSubset;
};

nsresult
NS_NewDOMDocumentType(nsIDOMDocumentType** aDocType,
                      nsNodeInfoManager* aNodeInfoManager,
                      nsIAtom *aName,
                      const nsAString& aPublicId,
                      const nsAString& aSystemId,
                      const nsAString& aInternalSubset);

#endif // nsDOMDocumentType_h
