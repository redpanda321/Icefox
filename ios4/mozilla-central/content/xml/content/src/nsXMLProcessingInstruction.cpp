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

#include "nsGenericElement.h"
#include "nsGkAtoms.h"
#include "nsUnicharUtils.h"
#include "nsXMLProcessingInstruction.h"
#include "nsParserUtils.h"
#include "nsContentCreatorFunctions.h"

nsresult
NS_NewXMLProcessingInstruction(nsIContent** aInstancePtrResult,
                               nsNodeInfoManager *aNodeInfoManager,
                               const nsAString& aTarget,
                               const nsAString& aData)
{
  NS_PRECONDITION(aNodeInfoManager, "Missing nodeinfo manager");

  if (aTarget.EqualsLiteral("xml-stylesheet")) {
    return NS_NewXMLStylesheetProcessingInstruction(aInstancePtrResult,
                                                    aNodeInfoManager, aData);
  }

  *aInstancePtrResult = nsnull;

  nsCOMPtr<nsINodeInfo> ni;
  ni = aNodeInfoManager->GetNodeInfo(nsGkAtoms::processingInstructionTagName,
                                     nsnull, kNameSpaceID_None);
  NS_ENSURE_TRUE(ni, NS_ERROR_OUT_OF_MEMORY);

  nsXMLProcessingInstruction *instance =
    new nsXMLProcessingInstruction(ni.forget(), aTarget, aData);
  if (!instance) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  NS_ADDREF(*aInstancePtrResult = instance);

  return NS_OK;
}

nsXMLProcessingInstruction::nsXMLProcessingInstruction(already_AddRefed<nsINodeInfo> aNodeInfo,
                                                       const nsAString& aTarget,
                                                       const nsAString& aData)
  : nsGenericDOMDataNode(aNodeInfo),
    mTarget(aTarget)
{
  SetTextInternal(0, mText.GetLength(),
                  aData.BeginReading(), aData.Length(),
                  PR_FALSE);  // Don't notify (bug 420429).
}

nsXMLProcessingInstruction::~nsXMLProcessingInstruction()
{
}


DOMCI_NODE_DATA(ProcessingInstruction, nsXMLProcessingInstruction)

// QueryInterface implementation for nsXMLProcessingInstruction
NS_INTERFACE_TABLE_HEAD(nsXMLProcessingInstruction)
  NS_NODE_OFFSET_AND_INTERFACE_TABLE_BEGIN(nsXMLProcessingInstruction)
    NS_INTERFACE_TABLE_ENTRY(nsXMLProcessingInstruction, nsIDOMNode)
    NS_INTERFACE_TABLE_ENTRY(nsXMLProcessingInstruction,
                             nsIDOMProcessingInstruction)
  NS_OFFSET_AND_INTERFACE_TABLE_END
  NS_OFFSET_AND_INTERFACE_TABLE_TO_MAP_SEGUE
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(ProcessingInstruction)
NS_INTERFACE_MAP_END_INHERITING(nsGenericDOMDataNode)


NS_IMPL_ADDREF_INHERITED(nsXMLProcessingInstruction, nsGenericDOMDataNode)
NS_IMPL_RELEASE_INHERITED(nsXMLProcessingInstruction, nsGenericDOMDataNode)


NS_IMETHODIMP
nsXMLProcessingInstruction::GetTarget(nsAString& aTarget)
{
  aTarget.Assign(mTarget);

  return NS_OK;
}

NS_IMETHODIMP
nsXMLProcessingInstruction::SetData(const nsAString& aData)
{
  return SetNodeValue(aData);
}

NS_IMETHODIMP
nsXMLProcessingInstruction::GetData(nsAString& aData)
{
  return nsGenericDOMDataNode::GetData(aData);
}

PRBool
nsXMLProcessingInstruction::GetAttrValue(nsIAtom *aName, nsAString& aValue)
{
  nsAutoString data;

  GetData(data);
  return nsParserUtils::GetQuotedAttributeValue(data, aName, aValue);
}

PRBool
nsXMLProcessingInstruction::IsNodeOfType(PRUint32 aFlags) const
{
  return !(aFlags & ~(eCONTENT | ePROCESSING_INSTRUCTION | eDATA_NODE));
}

NS_IMETHODIMP
nsXMLProcessingInstruction::GetNodeName(nsAString& aNodeName)
{
  aNodeName.Assign(mTarget);
  return NS_OK;
}

NS_IMETHODIMP
nsXMLProcessingInstruction::GetNodeValue(nsAString& aNodeValue)
{
  return nsGenericDOMDataNode::GetNodeValue(aNodeValue);
}

NS_IMETHODIMP
nsXMLProcessingInstruction::SetNodeValue(const nsAString& aNodeValue)
{
  return nsGenericDOMDataNode::SetNodeValue(aNodeValue);
}

NS_IMETHODIMP
nsXMLProcessingInstruction::GetNodeType(PRUint16* aNodeType)
{
  *aNodeType = (PRUint16)nsIDOMNode::PROCESSING_INSTRUCTION_NODE;
  return NS_OK;
}

nsGenericDOMDataNode*
nsXMLProcessingInstruction::CloneDataNode(nsINodeInfo *aNodeInfo,
                                          PRBool aCloneText) const
{
  nsAutoString data;
  nsGenericDOMDataNode::GetData(data);
  nsCOMPtr<nsINodeInfo> ni = aNodeInfo;
  return new nsXMLProcessingInstruction(ni.forget(), mTarget, data);
}

#ifdef DEBUG
void
nsXMLProcessingInstruction::List(FILE* out, PRInt32 aIndent) const
{
  PRInt32 index;
  for (index = aIndent; --index >= 0; ) fputs("  ", out);

  fprintf(out, "Processing instruction refcount=%d<", mRefCnt.get());

  nsAutoString tmp;
  ToCString(tmp, 0, mText.GetLength());
  tmp.Insert(mTarget.get(), 0);
  fputs(NS_LossyConvertUTF16toASCII(tmp).get(), out);

  fputs(">\n", out);
}

void
nsXMLProcessingInstruction::DumpContent(FILE* out, PRInt32 aIndent,
                                        PRBool aDumpAll) const
{
}
#endif
