/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
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
 * ***** END LICENSE BLOCK *****
 *
 * This Original Code has been modified by IBM Corporation. Modifications made by IBM 
 * described herein are Copyright (c) International Business Machines Corporation, 2000.
 * Modifications to Mozilla code or documentation identified per MPL Section 3.3
 *
 * Date             Modified by     Description of modification
 * 04/20/2000       IBM Corp.      OS/2 VisualAge build.
 */

/*
 * style sheet and style rule processor representing data from presentational
 * HTML attributes
 */

#include "nsHTMLStyleSheet.h"
#include "nsINameSpaceManager.h"
#include "nsIAtom.h"
#include "nsIURL.h"
#include "nsMappedAttributes.h"
#include "nsILink.h"
#include "nsIFrame.h"
#include "nsStyleContext.h"
#include "nsGkAtoms.h"
#include "nsPresContext.h"
#include "nsIEventStateManager.h"
#include "nsIDocument.h"
#include "nsIPresShell.h"
#include "nsStyleConsts.h"
#include "nsIHTMLDocument.h"
#include "nsIDOMHTMLElement.h"
#include "nsCSSAnonBoxes.h"
#include "nsRuleWalker.h"
#include "nsRuleData.h"
#include "nsContentErrors.h"
#include "nsRuleProcessorData.h"
#include "mozilla/dom/Element.h"
#include "nsCSSFrameConstructor.h"

using namespace mozilla::dom;

NS_IMPL_ISUPPORTS1(nsHTMLStyleSheet::HTMLColorRule, nsIStyleRule)

/* virtual */ void
nsHTMLStyleSheet::HTMLColorRule::MapRuleInfoInto(nsRuleData* aRuleData)
{
  if (aRuleData->mSIDs & NS_STYLE_INHERIT_BIT(Color)) {
    if (aRuleData->mColorData->mColor.GetUnit() == eCSSUnit_Null &&
        aRuleData->mPresContext->UseDocumentColors())
      aRuleData->mColorData->mColor.SetColorValue(mColor);
  }
}

#ifdef DEBUG
/* virtual */ void
nsHTMLStyleSheet::HTMLColorRule::List(FILE* out, PRInt32 aIndent) const
{
}
#endif

 
NS_IMPL_ISUPPORTS1(nsHTMLStyleSheet::GenericTableRule, nsIStyleRule)

/* virtual */ void
nsHTMLStyleSheet::GenericTableRule::MapRuleInfoInto(nsRuleData* aRuleData)
{
  // Nothing to do.
}

#ifdef DEBUG
/* virtual */ void
nsHTMLStyleSheet::GenericTableRule::List(FILE* out, PRInt32 aIndent) const
{
}
#endif

/* virtual */ void
nsHTMLStyleSheet::TableTHRule::MapRuleInfoInto(nsRuleData* aRuleData)
{
  if (aRuleData->mSIDs & NS_STYLE_INHERIT_BIT(Text)) {
    if (aRuleData->mTextData->mTextAlign.GetUnit() == eCSSUnit_Null) {
      aRuleData->mTextData->mTextAlign.
        SetIntValue(NS_STYLE_TEXT_ALIGN_MOZ_CENTER_OR_INHERIT,
                    eCSSUnit_Enumerated);
    }
  }
}

// -----------------------------------------------------------

struct MappedAttrTableEntry : public PLDHashEntryHdr {
  nsMappedAttributes *mAttributes;
};

static PLDHashNumber
MappedAttrTable_HashKey(PLDHashTable *table, const void *key)
{
  nsMappedAttributes *attributes =
    static_cast<nsMappedAttributes*>(const_cast<void*>(key));

  return attributes->HashValue();
}

static void
MappedAttrTable_ClearEntry(PLDHashTable *table, PLDHashEntryHdr *hdr)
{
  MappedAttrTableEntry *entry = static_cast<MappedAttrTableEntry*>(hdr);

  entry->mAttributes->DropStyleSheetReference();
  memset(entry, 0, sizeof(MappedAttrTableEntry));
}

static PRBool
MappedAttrTable_MatchEntry(PLDHashTable *table, const PLDHashEntryHdr *hdr,
                           const void *key)
{
  nsMappedAttributes *attributes =
    static_cast<nsMappedAttributes*>(const_cast<void*>(key));
  const MappedAttrTableEntry *entry =
    static_cast<const MappedAttrTableEntry*>(hdr);

  return attributes->Equals(entry->mAttributes);
}

static PLDHashTableOps MappedAttrTable_Ops = {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  MappedAttrTable_HashKey,
  MappedAttrTable_MatchEntry,
  PL_DHashMoveEntryStub,
  MappedAttrTable_ClearEntry,
  PL_DHashFinalizeStub,
  NULL
};

// -----------------------------------------------------------

nsHTMLStyleSheet::nsHTMLStyleSheet(void)
  : mDocument(nsnull)
{
  mMappedAttrTable.ops = nsnull;
}

nsresult
nsHTMLStyleSheet::Init()
{
  mTableTHRule = new TableTHRule();
  if (!mTableTHRule)
    return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}

nsHTMLStyleSheet::~nsHTMLStyleSheet()
{
  if (mMappedAttrTable.ops)
    PL_DHashTableFinish(&mMappedAttrTable);
}

NS_IMPL_ISUPPORTS2(nsHTMLStyleSheet, nsIStyleSheet, nsIStyleRuleProcessor)

static nsresult GetBodyColor(nsPresContext* aPresContext, nscolor* aColor)
{
  nsIPresShell *shell = aPresContext->PresShell();
  nsCOMPtr<nsIHTMLDocument> htmlDoc = do_QueryInterface(shell->GetDocument());
  if (!htmlDoc)
    return NS_ERROR_FAILURE;
  nsIContent* bodyContent = htmlDoc->GetBodyContentExternal();
  if (!bodyContent)
    return NS_ERROR_FAILURE;
  nsIFrame *bodyFrame = bodyContent->GetPrimaryFrame();
  if (!bodyFrame)
    return NS_ERROR_FAILURE;
  *aColor = bodyFrame->GetStyleColor()->mColor;
  return NS_OK;
}

/* virtual */ void
nsHTMLStyleSheet::RulesMatching(ElementRuleProcessorData* aData)
{
  nsRuleWalker *ruleWalker = aData->mRuleWalker;
  if (aData->mIsHTMLContent) {
    nsIAtom* tag = aData->mContentTag;

    // if we have anchor colors, check if this is an anchor with an href
    if (tag == nsGkAtoms::a) {
      if (mLinkRule || mVisitedRule || mActiveRule) {
        PRUint32 state = aData->GetContentStateForVisitedHandling(
                                  ruleWalker->VisitedHandling(),
                                  // If the node being matched is a link,
                                  // it's the relevant link.
                                  aData->IsLink());
        if (mLinkRule && (state & NS_EVENT_STATE_UNVISITED)) {
          ruleWalker->Forward(mLinkRule);
          ruleWalker->SetHaveRelevantLink();
        }
        else if (mVisitedRule && (state & NS_EVENT_STATE_VISITED)) {
          ruleWalker->Forward(mVisitedRule);
          ruleWalker->SetHaveRelevantLink();
        }

        // No need to add to the active rule if it's not a link
        if (mActiveRule && aData->IsLink() &&
            (state & NS_EVENT_STATE_ACTIVE)) {
          ruleWalker->Forward(mActiveRule);
        }
      } // end link/visited/active rules
    } // end A tag
    // add the rule to handle text-align for a <th>
    else if (tag == nsGkAtoms::th) {
      ruleWalker->Forward(mTableTHRule);
    }
    else if (tag == nsGkAtoms::table) {
      if (aData->mCompatMode == eCompatibility_NavQuirks) {
        nscolor bodyColor;
        nsresult rv =
          GetBodyColor(ruleWalker->CurrentNode()->GetPresContext(),
                       &bodyColor);
        if (NS_SUCCEEDED(rv) &&
            (!mDocumentColorRule || bodyColor != mDocumentColorRule->mColor)) {
          mDocumentColorRule = new HTMLColorRule();
          if (mDocumentColorRule) {
            mDocumentColorRule->mColor = bodyColor;
          }
        }
        if (mDocumentColorRule)
          ruleWalker->Forward(mDocumentColorRule);
      }
    }
  } // end html element

    // just get the style rules from the content
  aData->mElement->WalkContentStyleRules(ruleWalker);
}

// Test if style is dependent on content state
/* virtual */ nsRestyleHint
nsHTMLStyleSheet::HasStateDependentStyle(StateRuleProcessorData* aData)
{
  if (aData->mIsHTMLContent &&
      aData->mContentTag == nsGkAtoms::a &&
      aData->IsLink() &&
      ((mActiveRule && (aData->mStateMask & NS_EVENT_STATE_ACTIVE)) ||
       (mLinkRule && (aData->mStateMask & NS_EVENT_STATE_VISITED)) ||
       (mVisitedRule && (aData->mStateMask & NS_EVENT_STATE_VISITED)))) {
    return eRestyle_Self;
  }
  
  return nsRestyleHint(0);
}

/* virtual */ PRBool
nsHTMLStyleSheet::HasDocumentStateDependentStyle(StateRuleProcessorData* aData)
{
  return PR_FALSE;
}

/* virtual */ nsRestyleHint
nsHTMLStyleSheet::HasAttributeDependentStyle(AttributeRuleProcessorData* aData)
{
  // Do nothing on before-change checks
  if (!aData->mAttrHasChanged) {
    return nsRestyleHint(0);
  }

  // Note: no need to worry about whether some states changed with this
  // attribute here, because we handle that under HasStateDependentStyle() as
  // needed.

  // Result is true for |href| changes on HTML links if we have link rules.
  Element *element = aData->mElement;
  if (aData->mAttribute == nsGkAtoms::href &&
      (mLinkRule || mVisitedRule || mActiveRule) &&
      element->IsHTML() &&
      aData->mContentTag == nsGkAtoms::a) {
    return eRestyle_Self;
  }

  // Don't worry about the mDocumentColorRule since it only applies
  // to descendants of body, when we're already reresolving.

  // Handle the content style rules.
  if (element->IsAttributeMapped(aData->mAttribute)) {
    // cellpadding on tables is special and requires reresolving all
    // the cells in the table
    if (aData->mAttribute == nsGkAtoms::cellpadding &&
        element->IsHTML() &&
        aData->mContentTag == nsGkAtoms::table) {
      return eRestyle_Subtree;
    }
    return eRestyle_Self;
  }

  return nsRestyleHint(0);
}

/* virtual */ PRBool
nsHTMLStyleSheet::MediumFeaturesChanged(nsPresContext* aPresContext)
{
  return PR_FALSE;
}


/* virtual */ void
nsHTMLStyleSheet::RulesMatching(PseudoElementRuleProcessorData* aData)
{
}

/* virtual */ void
nsHTMLStyleSheet::RulesMatching(AnonBoxRuleProcessorData* aData)
{
}

#ifdef MOZ_XUL
/* virtual */ void
nsHTMLStyleSheet::RulesMatching(XULTreeRuleProcessorData* aData)
{
}
#endif

  // nsIStyleSheet api
/* virtual */ nsIURI*
nsHTMLStyleSheet::GetSheetURI() const
{
  return mURL;
}

/* virtual */ nsIURI*
nsHTMLStyleSheet::GetBaseURI() const
{
  return mURL;
}

/* virtual */ void
nsHTMLStyleSheet::GetTitle(nsString& aTitle) const
{
  aTitle.Truncate();
}

/* virtual */ void
nsHTMLStyleSheet::GetType(nsString& aType) const
{
  aType.AssignLiteral("text/html");
}

/* virtual */ PRBool
nsHTMLStyleSheet::HasRules() const
{
  return PR_TRUE; // We have rules at all reasonable times
}

/* virtual */ PRBool
nsHTMLStyleSheet::IsApplicable() const
{
  return PR_TRUE;
}

/* virtual */ void
nsHTMLStyleSheet::SetEnabled(PRBool aEnabled)
{ // these can't be disabled
}

/* virtual */ PRBool
nsHTMLStyleSheet::IsComplete() const
{
  return PR_TRUE;
}

/* virtual */ void
nsHTMLStyleSheet::SetComplete()
{
}

/* virtual */ nsIStyleSheet*
nsHTMLStyleSheet::GetParentSheet() const
{
  return nsnull;
}

/* virtual */ nsIDocument*
nsHTMLStyleSheet::GetOwningDocument() const
{
  return mDocument;
}

/* virtual */ void
nsHTMLStyleSheet::SetOwningDocument(nsIDocument* aDocument)
{
  mDocument = aDocument; // not refcounted
}

nsresult
nsHTMLStyleSheet::Init(nsIURI* aURL, nsIDocument* aDocument)
{
  NS_PRECONDITION(aURL && aDocument, "null ptr");
  if (! aURL || ! aDocument)
    return NS_ERROR_NULL_POINTER;

  if (mURL || mDocument)
    return NS_ERROR_ALREADY_INITIALIZED;

  mDocument = aDocument; // not refcounted!
  mURL = aURL;
  return NS_OK;
}

void
nsHTMLStyleSheet::Reset(nsIURI* aURL)
{
  mURL = aURL;

  mLinkRule          = nsnull;
  mVisitedRule       = nsnull;
  mActiveRule        = nsnull;
  mDocumentColorRule = nsnull;

  if (mMappedAttrTable.ops) {
    PL_DHashTableFinish(&mMappedAttrTable);
    mMappedAttrTable.ops = nsnull;
  }
}

nsresult
nsHTMLStyleSheet::ImplLinkColorSetter(nsRefPtr<HTMLColorRule>& aRule, nscolor aColor)
{
  if (aRule && aRule->mColor == aColor) {
    return NS_OK;
  }

  aRule = new HTMLColorRule();
  if (!aRule)
    return NS_ERROR_OUT_OF_MEMORY;

  aRule->mColor = aColor;
  // Now make sure we restyle any links that might need it.  This
  // shouldn't happen often, so just rebuilding everything is ok.
  if (mDocument && mDocument->GetShell()) {
    Element* root = mDocument->GetRootElement();
    if (root) {
      mDocument->GetShell()->FrameConstructor()->
        PostRestyleEvent(root, eRestyle_Subtree, NS_STYLE_HINT_NONE);
    }
  }
  return NS_OK;
}

nsresult
nsHTMLStyleSheet::SetLinkColor(nscolor aColor)
{
  return ImplLinkColorSetter(mLinkRule, aColor);
}


nsresult
nsHTMLStyleSheet::SetActiveLinkColor(nscolor aColor)
{
  return ImplLinkColorSetter(mActiveRule, aColor);
}

nsresult
nsHTMLStyleSheet::SetVisitedLinkColor(nscolor aColor)
{
  return ImplLinkColorSetter(mVisitedRule, aColor);
}

already_AddRefed<nsMappedAttributes>
nsHTMLStyleSheet::UniqueMappedAttributes(nsMappedAttributes* aMapped)
{
  if (!mMappedAttrTable.ops) {
    PRBool res = PL_DHashTableInit(&mMappedAttrTable, &MappedAttrTable_Ops,
                                   nsnull, sizeof(MappedAttrTableEntry), 16);
    if (!res) {
      mMappedAttrTable.ops = nsnull;
      return nsnull;
    }
  }
  MappedAttrTableEntry *entry = static_cast<MappedAttrTableEntry*>
                                           (PL_DHashTableOperate(&mMappedAttrTable, aMapped, PL_DHASH_ADD));
  if (!entry)
    return nsnull;
  if (!entry->mAttributes) {
    // We added a new entry to the hashtable, so we have a new unique set.
    entry->mAttributes = aMapped;
  }
  NS_ADDREF(entry->mAttributes); // for caller
  return entry->mAttributes;
}

void
nsHTMLStyleSheet::DropMappedAttributes(nsMappedAttributes* aMapped)
{
  NS_ENSURE_TRUE(aMapped, /**/);

  NS_ASSERTION(mMappedAttrTable.ops, "table uninitialized");
#ifdef DEBUG
  PRUint32 entryCount = mMappedAttrTable.entryCount - 1;
#endif

  PL_DHashTableOperate(&mMappedAttrTable, aMapped, PL_DHASH_REMOVE);

  NS_ASSERTION(entryCount == mMappedAttrTable.entryCount, "not removed");
}

#ifdef DEBUG
/* virtual */ void
nsHTMLStyleSheet::List(FILE* out, PRInt32 aIndent) const
{
  // Indent
  for (PRInt32 index = aIndent; --index >= 0; ) fputs("  ", out);

  fputs("HTML Style Sheet: ", out);
  nsCAutoString urlSpec;
  mURL->GetSpec(urlSpec);
  if (!urlSpec.IsEmpty()) {
    fputs(urlSpec.get(), out);
  }
  fputs("\n", out);
}
#endif

// XXX For convenience and backwards compatibility
nsresult
NS_NewHTMLStyleSheet(nsHTMLStyleSheet** aInstancePtrResult, nsIURI* aURL, 
                     nsIDocument* aDocument)
{
  nsresult rv;
  nsHTMLStyleSheet* sheet;
  if (NS_FAILED(rv = NS_NewHTMLStyleSheet(&sheet)))
    return rv;

  if (NS_FAILED(rv = sheet->Init(aURL, aDocument))) {
    NS_RELEASE(sheet);
    return rv;
  }

  *aInstancePtrResult = sheet;
  return NS_OK;
}


nsresult
NS_NewHTMLStyleSheet(nsHTMLStyleSheet** aInstancePtrResult)
{
  NS_ASSERTION(aInstancePtrResult, "null out param");

  nsHTMLStyleSheet *it = new nsHTMLStyleSheet();
  if (!it) {
    *aInstancePtrResult = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
  }

  NS_ADDREF(it);
  nsresult rv = it->Init();
  if (NS_FAILED(rv))
    NS_RELEASE(it);

  *aInstancePtrResult = it; // NS_ADDREF above, or set to null by NS_RELEASE
  return rv;
}
