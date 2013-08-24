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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   David Hyatt <hyatt@netscape.com>
 *   Daniel Glazman <glazman@netscape.com>
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation
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

/*
 * representation of CSS style rules (selectors+declaration), CSS
 * selectors, and DOM objects for style rules, selectors, and
 * declarations
 */

#include "nsCOMPtr.h"
#include "nsCSSRule.h"
#include "nsICSSStyleRule.h"
#include "nsICSSGroupRule.h"
#include "mozilla/css/Declaration.h"
#include "nsCSSStyleSheet.h"
#include "mozilla/css/Loader.h"
#include "nsIURL.h"
#include "nsIDocument.h"
#include "nsIDeviceContext.h"
#include "nsIAtom.h"
#include "nsCRT.h"
#include "nsString.h"
#include "nsStyleConsts.h"
#include "nsStyleUtil.h"
#include "nsIFontMetrics.h"
#include "nsIDOMCSSStyleSheet.h"
#include "nsICSSStyleRuleDOMWrapper.h"
#include "nsIDOMCSSStyleDeclaration.h"
#include "nsDOMCSSDeclaration.h"
#include "nsINameSpaceManager.h"
#include "nsXMLNameSpaceMap.h"
#include "nsILookAndFeel.h"
#include "nsRuleNode.h"
#include "nsUnicharUtils.h"
#include "nsCSSPseudoElements.h"
#include "nsIPrincipal.h"
#include "nsComponentManagerUtils.h"
#include "nsCSSPseudoClasses.h"
#include "nsCSSAnonBoxes.h"
#include "nsTArray.h"

#include "nsContentUtils.h"
#include "nsContentErrors.h"
#include "mozAutoDocUpdate.h"

#include "prlog.h"

namespace css = mozilla::css;

#define NS_IF_CLONE(member_)                                                  \
  PR_BEGIN_MACRO                                                              \
    if (member_) {                                                            \
      result->member_ = member_->Clone();                                     \
      if (!result->member_) {                                                 \
        delete result;                                                        \
        return nsnull;                                                        \
      }                                                                       \
    }                                                                         \
  PR_END_MACRO

#define NS_IF_DELETE(ptr)                                                     \
  PR_BEGIN_MACRO                                                              \
    delete ptr;                                                               \
    ptr = nsnull;                                                             \
  PR_END_MACRO

/* ************************************************************************** */

nsAtomList::nsAtomList(nsIAtom* aAtom)
  : mAtom(aAtom),
    mNext(nsnull)
{
  MOZ_COUNT_CTOR(nsAtomList);
}

nsAtomList::nsAtomList(const nsString& aAtomValue)
  : mAtom(nsnull),
    mNext(nsnull)
{
  MOZ_COUNT_CTOR(nsAtomList);
  mAtom = do_GetAtom(aAtomValue);
}

nsAtomList*
nsAtomList::Clone(PRBool aDeep) const
{
  nsAtomList *result = new nsAtomList(mAtom);
  if (!result)
    return nsnull;

  if (aDeep)
    NS_CSS_CLONE_LIST_MEMBER(nsAtomList, this, mNext, result, (PR_FALSE));
  return result;
}

nsAtomList::~nsAtomList(void)
{
  MOZ_COUNT_DTOR(nsAtomList);
  NS_CSS_DELETE_LIST_MEMBER(nsAtomList, this, mNext);
}

nsPseudoClassList::nsPseudoClassList(nsIAtom* aAtom,
                                     nsCSSPseudoClasses::Type aType)
  : mAtom(aAtom),
    mType(aType),
    mNext(nsnull)
{
  NS_ASSERTION(!nsCSSPseudoClasses::HasStringArg(aAtom) &&
               !nsCSSPseudoClasses::HasNthPairArg(aAtom),
               "unexpected pseudo-class");
  MOZ_COUNT_CTOR(nsPseudoClassList);
  u.mMemory = nsnull;
}

nsPseudoClassList::nsPseudoClassList(nsIAtom* aAtom,
                                     nsCSSPseudoClasses::Type aType,
                                     const PRUnichar* aString)
  : mAtom(aAtom),
    mType(aType),
    mNext(nsnull)
{
  NS_ASSERTION(nsCSSPseudoClasses::HasStringArg(aAtom),
               "unexpected pseudo-class");
  NS_ASSERTION(aString, "string expected");
  MOZ_COUNT_CTOR(nsPseudoClassList);
  u.mString = NS_strdup(aString);
}

nsPseudoClassList::nsPseudoClassList(nsIAtom* aAtom,
                                     nsCSSPseudoClasses::Type aType,
                                     const PRInt32* aIntPair)
  : mAtom(aAtom),
    mType(aType),
    mNext(nsnull)
{
  NS_ASSERTION(nsCSSPseudoClasses::HasNthPairArg(aAtom),
               "unexpected pseudo-class");
  NS_ASSERTION(aIntPair, "integer pair expected");
  MOZ_COUNT_CTOR(nsPseudoClassList);
  u.mNumbers =
    static_cast<PRInt32*>(nsMemory::Clone(aIntPair, sizeof(PRInt32) * 2));
}

// adopts aSelectorList
nsPseudoClassList::nsPseudoClassList(nsIAtom* aAtom,
                                     nsCSSPseudoClasses::Type aType,
                                     nsCSSSelectorList* aSelectorList)
  : mAtom(aAtom),
    mType(aType),
    mNext(nsnull)
{
  NS_ASSERTION(nsCSSPseudoClasses::HasSelectorListArg(aAtom),
               "unexpected pseudo-class");
  NS_ASSERTION(aSelectorList, "selector list expected");
  MOZ_COUNT_CTOR(nsPseudoClassList);
  u.mSelectors = aSelectorList;
}

nsPseudoClassList*
nsPseudoClassList::Clone(PRBool aDeep) const
{
  nsPseudoClassList *result;
  if (!u.mMemory) {
    result = new nsPseudoClassList(mAtom, mType);
  } else if (nsCSSPseudoClasses::HasStringArg(mAtom)) {
    result = new nsPseudoClassList(mAtom, mType, u.mString);
  } else if (nsCSSPseudoClasses::HasNthPairArg(mAtom)) {
    result = new nsPseudoClassList(mAtom, mType, u.mNumbers);
  } else {
    NS_ASSERTION(nsCSSPseudoClasses::HasSelectorListArg(mAtom),
                 "unexpected pseudo-class");
    // This constructor adopts its selector list argument.
    result = new nsPseudoClassList(mAtom, mType, u.mSelectors->Clone());
  }

  if (aDeep)
    NS_CSS_CLONE_LIST_MEMBER(nsPseudoClassList, this, mNext, result,
                             (PR_FALSE));

  return result;
}

nsPseudoClassList::~nsPseudoClassList(void)
{
  MOZ_COUNT_DTOR(nsPseudoClassList);
  if (nsCSSPseudoClasses::HasSelectorListArg(mAtom)) {
    delete u.mSelectors;
  } else if (u.mMemory) {
    NS_Free(u.mMemory);
  }
  NS_CSS_DELETE_LIST_MEMBER(nsPseudoClassList, this, mNext);
}

nsAttrSelector::nsAttrSelector(PRInt32 aNameSpace, const nsString& aAttr)
  : mValue(),
    mNext(nsnull),
    mLowercaseAttr(nsnull),
    mCasedAttr(nsnull),
    mNameSpace(aNameSpace),
    mFunction(NS_ATTR_FUNC_SET),
    mCaseSensitive(1)
{
  MOZ_COUNT_CTOR(nsAttrSelector);

  nsAutoString lowercase;
  nsContentUtils::ASCIIToLower(aAttr, lowercase);
  
  mCasedAttr = do_GetAtom(aAttr);
  mLowercaseAttr = do_GetAtom(lowercase);
}

nsAttrSelector::nsAttrSelector(PRInt32 aNameSpace, const nsString& aAttr, PRUint8 aFunction, 
                               const nsString& aValue, PRBool aCaseSensitive)
  : mValue(aValue),
    mNext(nsnull),
    mLowercaseAttr(nsnull),
    mCasedAttr(nsnull),
    mNameSpace(aNameSpace),
    mFunction(aFunction),
    mCaseSensitive(aCaseSensitive)
{
  MOZ_COUNT_CTOR(nsAttrSelector);

  nsAutoString lowercase;
  nsContentUtils::ASCIIToLower(aAttr, lowercase);
  
  mCasedAttr = do_GetAtom(aAttr);
  mLowercaseAttr = do_GetAtom(lowercase);
}

nsAttrSelector::nsAttrSelector(PRInt32 aNameSpace,  nsIAtom* aLowercaseAttr,
                               nsIAtom* aCasedAttr, PRUint8 aFunction, 
                               const nsString& aValue, PRBool aCaseSensitive)
  : mValue(aValue),
    mNext(nsnull),
    mLowercaseAttr(aLowercaseAttr),
    mCasedAttr(aCasedAttr),
    mNameSpace(aNameSpace),
    mFunction(aFunction),
    mCaseSensitive(aCaseSensitive)
{
  MOZ_COUNT_CTOR(nsAttrSelector);
}

nsAttrSelector*
nsAttrSelector::Clone(PRBool aDeep) const
{
  nsAttrSelector *result =
    new nsAttrSelector(mNameSpace, mLowercaseAttr, mCasedAttr, 
                       mFunction, mValue, mCaseSensitive);

  if (aDeep)
    NS_CSS_CLONE_LIST_MEMBER(nsAttrSelector, this, mNext, result, (PR_FALSE));

  return result;
}

nsAttrSelector::~nsAttrSelector(void)
{
  MOZ_COUNT_DTOR(nsAttrSelector);

  NS_CSS_DELETE_LIST_MEMBER(nsAttrSelector, this, mNext);
}

// -- nsCSSSelector -------------------------------

nsCSSSelector::nsCSSSelector(void)
  : mLowercaseTag(nsnull),
    mCasedTag(nsnull),
    mIDList(nsnull),
    mClassList(nsnull),
    mPseudoClassList(nsnull),
    mAttrList(nsnull),
    mNegations(nsnull),
    mNext(nsnull),
    mNameSpace(kNameSpaceID_Unknown),
    mOperator(0),
    mPseudoType(nsCSSPseudoElements::ePseudo_NotPseudoElement)
{
  MOZ_COUNT_CTOR(nsCSSSelector);
  // Make sure mPseudoType can hold all nsCSSPseudoElements::Type values
  PR_STATIC_ASSERT(nsCSSPseudoElements::ePseudo_MAX < PR_INT16_MAX);
}

nsCSSSelector*
nsCSSSelector::Clone(PRBool aDeepNext, PRBool aDeepNegations) const
{
  nsCSSSelector *result = new nsCSSSelector();
  if (!result)
    return nsnull;

  result->mNameSpace = mNameSpace;
  result->mLowercaseTag = mLowercaseTag;
  result->mCasedTag = mCasedTag;
  result->mOperator = mOperator;
  result->mPseudoType = mPseudoType;
  
  NS_IF_CLONE(mIDList);
  NS_IF_CLONE(mClassList);
  NS_IF_CLONE(mPseudoClassList);
  NS_IF_CLONE(mAttrList);

  // No need to worry about multiple levels of recursion since an
  // mNegations can't have an mNext.
  NS_ASSERTION(!mNegations || !mNegations->mNext,
               "mNegations can't have non-null mNext");
  if (aDeepNegations) {
    NS_CSS_CLONE_LIST_MEMBER(nsCSSSelector, this, mNegations, result,
                             (PR_TRUE, PR_FALSE));
  }

  if (aDeepNext) {
    NS_CSS_CLONE_LIST_MEMBER(nsCSSSelector, this, mNext, result,
                             (PR_FALSE, PR_TRUE));
  }

  return result;
}

nsCSSSelector::~nsCSSSelector(void)  
{
  MOZ_COUNT_DTOR(nsCSSSelector);
  Reset();
  // No need to worry about multiple levels of recursion since an
  // mNegations can't have an mNext.
  NS_CSS_DELETE_LIST_MEMBER(nsCSSSelector, this, mNext);
}

void nsCSSSelector::Reset(void)
{
  mNameSpace = kNameSpaceID_Unknown;
  mLowercaseTag = nsnull;
  mCasedTag = nsnull;
  NS_IF_DELETE(mIDList);
  NS_IF_DELETE(mClassList);
  NS_IF_DELETE(mPseudoClassList);
  NS_IF_DELETE(mAttrList);
  // No need to worry about multiple levels of recursion since an
  // mNegations can't have an mNext.
  NS_ASSERTION(!mNegations || !mNegations->mNext,
               "mNegations can't have non-null mNext");
  NS_CSS_DELETE_LIST_MEMBER(nsCSSSelector, this, mNegations);
  mOperator = PRUnichar(0);
}

void nsCSSSelector::SetNameSpace(PRInt32 aNameSpace)
{
  mNameSpace = aNameSpace;
}

void nsCSSSelector::SetTag(const nsString& aTag)
{
  if (aTag.IsEmpty()) {
    mLowercaseTag = mCasedTag =  nsnull;
    return;
  }

  mCasedTag = do_GetAtom(aTag);
 
  nsAutoString lowercase;
  nsContentUtils::ASCIIToLower(aTag, lowercase);
  mLowercaseTag = do_GetAtom(lowercase);
}

void nsCSSSelector::AddID(const nsString& aID)
{
  if (!aID.IsEmpty()) {
    nsAtomList** list = &mIDList;
    while (nsnull != *list) {
      list = &((*list)->mNext);
    }
    *list = new nsAtomList(aID);
  }
}

void nsCSSSelector::AddClass(const nsString& aClass)
{
  if (!aClass.IsEmpty()) {
    nsAtomList** list = &mClassList;
    while (nsnull != *list) {
      list = &((*list)->mNext);
    }
    *list = new nsAtomList(aClass);
  }
}

void nsCSSSelector::AddPseudoClass(nsIAtom* aPseudoClass,
                                   nsCSSPseudoClasses::Type aType)
{
  AddPseudoClassInternal(new nsPseudoClassList(aPseudoClass, aType));
}

void nsCSSSelector::AddPseudoClass(nsIAtom* aPseudoClass,
                                   nsCSSPseudoClasses::Type aType,
                                   const PRUnichar* aString)
{
  AddPseudoClassInternal(new nsPseudoClassList(aPseudoClass, aType, aString));
}

void nsCSSSelector::AddPseudoClass(nsIAtom* aPseudoClass,
                                   nsCSSPseudoClasses::Type aType,
                                   const PRInt32* aIntPair)
{
  AddPseudoClassInternal(new nsPseudoClassList(aPseudoClass, aType, aIntPair));
}

void nsCSSSelector::AddPseudoClass(nsIAtom* aPseudoClass,
                                   nsCSSPseudoClasses::Type aType,
                                   nsCSSSelectorList* aSelectorList)
{
  // Take ownership of nsCSSSelectorList instead of copying.
  AddPseudoClassInternal(new nsPseudoClassList(aPseudoClass, aType,
                                               aSelectorList));
}

void nsCSSSelector::AddPseudoClassInternal(nsPseudoClassList *aPseudoClass)
{
  nsPseudoClassList** list = &mPseudoClassList;
  while (nsnull != *list) {
    list = &((*list)->mNext);
  }
  *list = aPseudoClass;
}

void nsCSSSelector::AddAttribute(PRInt32 aNameSpace, const nsString& aAttr)
{
  if (!aAttr.IsEmpty()) {
    nsAttrSelector** list = &mAttrList;
    while (nsnull != *list) {
      list = &((*list)->mNext);
    }
    *list = new nsAttrSelector(aNameSpace, aAttr);
  }
}

void nsCSSSelector::AddAttribute(PRInt32 aNameSpace, const nsString& aAttr, PRUint8 aFunc, 
                                 const nsString& aValue, PRBool aCaseSensitive)
{
  if (!aAttr.IsEmpty()) {
    nsAttrSelector** list = &mAttrList;
    while (nsnull != *list) {
      list = &((*list)->mNext);
    }
    *list = new nsAttrSelector(aNameSpace, aAttr, aFunc, aValue, aCaseSensitive);
  }
}

void nsCSSSelector::SetOperator(PRUnichar aOperator)
{
  mOperator = aOperator;
}

PRInt32 nsCSSSelector::CalcWeightWithoutNegations() const
{
  PRInt32 weight = 0;

  if (nsnull != mLowercaseTag) {
    weight += 0x000001;
  }
  nsAtomList* list = mIDList;
  while (nsnull != list) {
    weight += 0x010000;
    list = list->mNext;
  }
  list = mClassList;
  while (nsnull != list) {
    weight += 0x000100;
    list = list->mNext;
  }
  // FIXME (bug 561154):  This is incorrect for :-moz-any(), which isn't
  // really a pseudo-class.  In order to handle :-moz-any() correctly,
  // we need to compute specificity after we match, based on which
  // option we matched with (and thus also need to try the
  // highest-specificity options first).
  nsPseudoClassList *plist = mPseudoClassList;
  while (nsnull != plist) {
    weight += 0x000100;
    plist = plist->mNext;
  }
  nsAttrSelector* attr = mAttrList;
  while (nsnull != attr) {
    weight += 0x000100;
    attr = attr->mNext;
  }
  return weight;
}

PRInt32 nsCSSSelector::CalcWeight() const
{
  // Loop over this selector and all its negations.
  PRInt32 weight = 0;
  for (const nsCSSSelector *n = this; n; n = n->mNegations) {
    weight += n->CalcWeightWithoutNegations();
  }
  return weight;
}

//
// Builds the textual representation of a selector. Called by DOM 2 CSS 
// StyleRule:selectorText
//
void
nsCSSSelector::ToString(nsAString& aString, nsCSSStyleSheet* aSheet,
                        PRBool aAppend) const
{
  if (!aAppend)
   aString.Truncate();

  // selectors are linked from right-to-left, so the next selector in
  // the linked list actually precedes this one in the resulting string
  nsAutoTArray<const nsCSSSelector*, 8> stack;
  for (const nsCSSSelector *s = this; s; s = s->mNext) {
    stack.AppendElement(s);
  }

  while (!stack.IsEmpty()) {
    PRUint32 index = stack.Length() - 1;
    const nsCSSSelector *s = stack.ElementAt(index);
    stack.RemoveElementAt(index);

    s->AppendToStringWithoutCombinators(aString, aSheet);

    // Append the combinator, if needed.
    if (!stack.IsEmpty()) {
      const nsCSSSelector *next = stack.ElementAt(index - 1);
      PRUnichar oper = s->mOperator;
      if (next->IsPseudoElement()) {
        NS_ASSERTION(oper == PRUnichar('>'),
                     "improperly chained pseudo element");
      } else {
        NS_ASSERTION(oper != PRUnichar(0),
                     "compound selector without combinator");

        aString.Append(PRUnichar(' '));
        if (oper != PRUnichar(' ')) {
          aString.Append(oper);
          aString.Append(PRUnichar(' '));
        }
      }
    }
  }
}

void
nsCSSSelector::AppendToStringWithoutCombinators
                   (nsAString& aString, nsCSSStyleSheet* aSheet) const
{
  AppendToStringWithoutCombinatorsOrNegations(aString, aSheet, PR_FALSE);

  for (const nsCSSSelector* negation = mNegations; negation;
       negation = negation->mNegations) {
    aString.AppendLiteral(":not(");
    negation->AppendToStringWithoutCombinatorsOrNegations(aString, aSheet,
                                                          PR_TRUE);
    aString.Append(PRUnichar(')'));
  }
}

void
nsCSSSelector::AppendToStringWithoutCombinatorsOrNegations
                   (nsAString& aString, nsCSSStyleSheet* aSheet,
                   PRBool aIsNegated) const
{
  nsAutoString temp;
  PRBool isPseudoElement = IsPseudoElement();

  // For non-pseudo-element selectors or for lone pseudo-elements, deal with
  // namespace prefixes.
  PRBool wroteNamespace = PR_FALSE;
  if (!isPseudoElement || !mNext) {
    // append the namespace prefix if needed
    nsXMLNameSpaceMap *sheetNS = aSheet ? aSheet->GetNameSpaceMap() : nsnull;

    // sheetNS is non-null if and only if we had an @namespace rule.  If it's
    // null, that means that the only namespaces we could have are the
    // wildcard namespace (which can be implicit in this case) and the "none"
    // namespace, which then needs to be explicitly specified.
    if (!sheetNS) {
      NS_ASSERTION(mNameSpace == kNameSpaceID_Unknown ||
                   mNameSpace == kNameSpaceID_None,
                   "How did we get this namespace?");
      if (mNameSpace == kNameSpaceID_None) {
        aString.Append(PRUnichar('|'));
        wroteNamespace = PR_TRUE;
      }
    } else if (sheetNS->FindNameSpaceID(nsnull) == mNameSpace) {
      // We have the default namespace (possibly including the wildcard
      // namespace).  Do nothing.
      NS_ASSERTION(mNameSpace == kNameSpaceID_Unknown ||
                   CanBeNamespaced(aIsNegated),
                   "How did we end up with this namespace?");
    } else if (mNameSpace == kNameSpaceID_None) {
      NS_ASSERTION(CanBeNamespaced(aIsNegated),
                   "How did we end up with this namespace?");
      aString.Append(PRUnichar('|'));
      wroteNamespace = PR_TRUE;
    } else if (mNameSpace != kNameSpaceID_Unknown) {
      NS_ASSERTION(CanBeNamespaced(aIsNegated),
                   "How did we end up with this namespace?");
      nsIAtom *prefixAtom = sheetNS->FindPrefix(mNameSpace);
      NS_ASSERTION(prefixAtom, "how'd we get a non-default namespace "
                   "without a prefix?");
      nsStyleUtil::AppendEscapedCSSIdent(nsDependentAtomString(prefixAtom),
                                         aString);
      aString.Append(PRUnichar('|'));
      wroteNamespace = PR_TRUE;
    } else {
      // A selector for an element in any namespace, while the default
      // namespace is something else.  :not() is special in that the default
      // namespace is not implied for non-type selectors, so if this is a
      // negated non-type selector we don't need to output an explicit wildcard
      // namespace here, since those default to a wildcard namespace.
      if (CanBeNamespaced(aIsNegated)) {
        aString.AppendLiteral("*|");
        wroteNamespace = PR_TRUE;
      }
    }
  }
      
  if (!mLowercaseTag) {
    // Universal selector:  avoid writing the universal selector when we
    // can avoid it, especially since we're required to avoid it for the
    // inside of :not()
    if (wroteNamespace ||
        (!mIDList && !mClassList && !mPseudoClassList && !mAttrList &&
         (aIsNegated || !mNegations))) {
      aString.Append(PRUnichar('*'));
    }
  } else {
    // Append the tag name
    nsAutoString tag;
    (isPseudoElement ? mLowercaseTag : mCasedTag)->ToString(tag);
    if (isPseudoElement) {
      if (!mNext) {
        // Lone pseudo-element selector -- toss in a wildcard type selector
        // XXXldb Why?
        aString.Append(PRUnichar('*'));
      }
      if (!nsCSSPseudoElements::IsCSS2PseudoElement(mLowercaseTag)) {
        aString.Append(PRUnichar(':'));
      }
      // This should not be escaped since (a) the pseudo-element string
      // has a ":" that can't be escaped and (b) all pseudo-elements at
      // this point are known, and therefore we know they don't need
      // escaping.
      aString.Append(tag);
    } else {
      nsStyleUtil::AppendEscapedCSSIdent(tag, aString);
    }
  }

  // Append the id, if there is one
  if (mIDList) {
    nsAtomList* list = mIDList;
    while (list != nsnull) {
      list->mAtom->ToString(temp);
      aString.Append(PRUnichar('#'));
      nsStyleUtil::AppendEscapedCSSIdent(temp, aString);
      list = list->mNext;
    }
  }

  // Append each class in the linked list
  if (mClassList) {
    nsAtomList* list = mClassList;
    while (list != nsnull) {
      list->mAtom->ToString(temp);
      aString.Append(PRUnichar('.'));
      nsStyleUtil::AppendEscapedCSSIdent(temp, aString);
      list = list->mNext;
    }
  }

  // Append each attribute selector in the linked list
  if (mAttrList) {
    nsAttrSelector* list = mAttrList;
    while (list != nsnull) {
      aString.Append(PRUnichar('['));
      // Append the namespace prefix
      if (list->mNameSpace == kNameSpaceID_Unknown) {
        aString.Append(PRUnichar('*'));
        aString.Append(PRUnichar('|'));
      } else if (list->mNameSpace != kNameSpaceID_None) {
        if (aSheet) {
          nsXMLNameSpaceMap *sheetNS = aSheet->GetNameSpaceMap();
          nsIAtom *prefixAtom = sheetNS->FindPrefix(list->mNameSpace);
          // Default namespaces don't apply to attribute selectors, so
          // we must have a useful prefix.
          NS_ASSERTION(prefixAtom,
                       "How did we end up with a namespace if the prefix "
                       "is unknown?");
          nsAutoString prefix;
          prefixAtom->ToString(prefix);
          nsStyleUtil::AppendEscapedCSSIdent(prefix, aString);
          aString.Append(PRUnichar('|'));
        }
      }
      // Append the attribute name
      list->mCasedAttr->ToString(temp);
      nsStyleUtil::AppendEscapedCSSIdent(temp, aString);

      if (list->mFunction != NS_ATTR_FUNC_SET) {
        // Append the function
        if (list->mFunction == NS_ATTR_FUNC_INCLUDES)
          aString.Append(PRUnichar('~'));
        else if (list->mFunction == NS_ATTR_FUNC_DASHMATCH)
          aString.Append(PRUnichar('|'));
        else if (list->mFunction == NS_ATTR_FUNC_BEGINSMATCH)
          aString.Append(PRUnichar('^'));
        else if (list->mFunction == NS_ATTR_FUNC_ENDSMATCH)
          aString.Append(PRUnichar('$'));
        else if (list->mFunction == NS_ATTR_FUNC_CONTAINSMATCH)
          aString.Append(PRUnichar('*'));

        aString.Append(PRUnichar('='));
      
        // Append the value
        nsStyleUtil::AppendEscapedCSSString(list->mValue, aString);
      }

      aString.Append(PRUnichar(']'));
      
      list = list->mNext;
    }
  }

  // Append each pseudo-class in the linked list
  if (isPseudoElement) {
#ifdef MOZ_XUL
    if (mPseudoClassList) {
      NS_ABORT_IF_FALSE(nsCSSAnonBoxes::IsTreePseudoElement(mLowercaseTag),
                        "must be tree pseudo-element");
      aString.Append(PRUnichar('('));
      for (nsPseudoClassList* list = mPseudoClassList; list;
           list = list->mNext) {
        list->mAtom->ToString(temp);
        nsStyleUtil::AppendEscapedCSSIdent(temp, aString);
        NS_ABORT_IF_FALSE(!list->u.mMemory, "data not expected");
        aString.Append(PRUnichar(','));
      }
      // replace the final comma with a close-paren
      aString.Replace(aString.Length() - 1, 1, PRUnichar(')'));
    }
#else
    NS_ABORT_IF_FALSE(!mPseudoClassList, "unexpected pseudo-class list");
#endif
  } else {
    for (nsPseudoClassList* list = mPseudoClassList; list; list = list->mNext) {
      list->mAtom->ToString(temp);
      // This should not be escaped since (a) the pseudo-class string
      // has a ":" that can't be escaped and (b) all pseudo-classes at
      // this point are known, and therefore we know they don't need
      // escaping.
      aString.Append(temp);
      if (list->u.mMemory) {
        aString.Append(PRUnichar('('));
        if (nsCSSPseudoClasses::HasStringArg(list->mAtom)) {
          nsStyleUtil::AppendEscapedCSSIdent(
            nsDependentString(list->u.mString), aString);
        } else if (nsCSSPseudoClasses::HasNthPairArg(list->mAtom)) {
          PRInt32 a = list->u.mNumbers[0],
                  b = list->u.mNumbers[1];
          temp.Truncate();
          if (a != 0) {
            if (a == -1) {
              temp.Append(PRUnichar('-'));
            } else if (a != 1) {
              temp.AppendInt(a);
            }
            temp.Append(PRUnichar('n'));
          }
          if (b != 0 || a == 0) {
            if (b >= 0 && a != 0) // check a != 0 for whether we printed above
              temp.Append(PRUnichar('+'));
            temp.AppendInt(b);
          }
          aString.Append(temp);
        } else {
          NS_ASSERTION(nsCSSPseudoClasses::HasSelectorListArg(list->mAtom),
                       "unexpected pseudo-class");
          nsString tmp;
          list->u.mSelectors->ToString(tmp, aSheet);
          aString.Append(tmp);
        }
        aString.Append(PRUnichar(')'));
      }
    }
  }
}

PRBool
nsCSSSelector::CanBeNamespaced(PRBool aIsNegated) const
{
  return !aIsNegated ||
         (!mIDList && !mClassList && !mPseudoClassList && !mAttrList);
}

// -- nsCSSSelectorList -------------------------------

nsCSSSelectorList::nsCSSSelectorList(void)
  : mSelectors(nsnull),
    mWeight(0),
    mNext(nsnull)
{
  MOZ_COUNT_CTOR(nsCSSSelectorList);
}

nsCSSSelectorList::~nsCSSSelectorList()
{
  MOZ_COUNT_DTOR(nsCSSSelectorList);
  delete mSelectors;
  NS_CSS_DELETE_LIST_MEMBER(nsCSSSelectorList, this, mNext);
}

nsCSSSelector*
nsCSSSelectorList::AddSelector(PRUnichar aOperator)
{
  nsCSSSelector* newSel = new nsCSSSelector();

  if (mSelectors) {
    NS_ASSERTION(aOperator != PRUnichar(0), "chaining without combinator");
    mSelectors->SetOperator(aOperator);
  } else {
    NS_ASSERTION(aOperator == PRUnichar(0), "combinator without chaining");
  }

  newSel->mNext = mSelectors;
  mSelectors = newSel;
  return newSel;
}

void
nsCSSSelectorList::ToString(nsAString& aResult, nsCSSStyleSheet* aSheet)
{
  aResult.Truncate();
  nsCSSSelectorList *p = this;
  for (;;) {
    p->mSelectors->ToString(aResult, aSheet, PR_TRUE);
    p = p->mNext;
    if (!p)
      break;
    aResult.AppendLiteral(", ");
  }
}

nsCSSSelectorList*
nsCSSSelectorList::Clone(PRBool aDeep) const
{
  nsCSSSelectorList *result = new nsCSSSelectorList();
  result->mWeight = mWeight;
  NS_IF_CLONE(mSelectors);

  if (aDeep) {
    NS_CSS_CLONE_LIST_MEMBER(nsCSSSelectorList, this, mNext, result,
                             (PR_FALSE));
  }
  return result;
}

// -- CSSImportantRule -------------------------------

class CSSStyleRuleImpl;

class CSSImportantRule : public nsIStyleRule {
public:
  CSSImportantRule(css::Declaration *aDeclaration);

  NS_DECL_ISUPPORTS

  // nsIStyleRule interface
  virtual void MapRuleInfoInto(nsRuleData* aRuleData);
#ifdef DEBUG
  virtual void List(FILE* out = stdout, PRInt32 aIndent = 0) const;
#endif

protected:
  virtual ~CSSImportantRule(void);

  // Not an owning reference; the CSSStyleRuleImpl that owns this
  // CSSImportantRule also owns the mDeclaration, and any rule node
  // pointing to this rule keeps that CSSStyleRuleImpl alive as well.
  css::Declaration* mDeclaration;

  friend class CSSStyleRuleImpl;
};

CSSImportantRule::CSSImportantRule(css::Declaration* aDeclaration)
  : mDeclaration(aDeclaration)
{
}

CSSImportantRule::~CSSImportantRule(void)
{
}

NS_IMPL_ISUPPORTS1(CSSImportantRule, nsIStyleRule)

/* virtual */ void
CSSImportantRule::MapRuleInfoInto(nsRuleData* aRuleData)
{
  mDeclaration->MapImportantRuleInfoInto(aRuleData);
}

#ifdef DEBUG
/* virtual */ void
CSSImportantRule::List(FILE* out, PRInt32 aIndent) const
{
  // Indent
  for (PRInt32 index = aIndent; --index >= 0; ) fputs("  ", out);

  fprintf(out, "! Important declaration=%p\n",
          static_cast<void*>(mDeclaration));
}
#endif

// --------------------------------------------------------

class DOMCSSStyleRuleImpl;

class DOMCSSDeclarationImpl : public nsDOMCSSDeclaration
{
public:
  DOMCSSDeclarationImpl(nsICSSStyleRule *aRule);
  virtual ~DOMCSSDeclarationImpl(void);

  NS_IMETHOD GetParentRule(nsIDOMCSSRule **aParent);
  void DropReference(void);
  virtual css::Declaration* GetCSSDeclaration(PRBool aAllocate);
  virtual nsresult SetCSSDeclaration(css::Declaration* aDecl);
  virtual nsresult GetCSSParsingEnvironment(nsIURI** aSheetURI,
                                            nsIURI** aBaseURI,
                                            nsIPrincipal** aSheetPrincipal,
                                            mozilla::css::Loader** aCSSLoader);
  virtual nsIDocument* DocToUpdate();

  // Override |AddRef| and |Release| for being a member of
  // |DOMCSSStyleRuleImpl|.
  NS_IMETHOD_(nsrefcnt) AddRef(void);
  NS_IMETHOD_(nsrefcnt) Release(void);

  virtual nsINode *GetParentObject()
  {
    return nsnull;
  }

  friend class DOMCSSStyleRuleImpl;

protected:
  // This reference is not reference-counted. The rule object tells us
  // when it's about to go away.
  nsICSSStyleRule *mRule;

  inline DOMCSSStyleRuleImpl* DomRule();

private:
  // NOT TO BE IMPLEMENTED
  // This object cannot be allocated on its own.  It must be a member of
  // DOMCSSStyleRuleImpl.
  void* operator new(size_t size) CPP_THROW_NEW;
};

class DOMCSSStyleRuleImpl : public nsICSSStyleRuleDOMWrapper
{
public:
  DOMCSSStyleRuleImpl(nsICSSStyleRule *aRule);
  virtual ~DOMCSSStyleRuleImpl();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMCSSRULE
  NS_DECL_NSIDOMCSSSTYLERULE

  // nsICSSStyleRuleDOMWrapper
  NS_IMETHOD GetCSSStyleRule(nsICSSStyleRule **aResult);

  DOMCSSDeclarationImpl* DOMDeclaration() { return &mDOMDeclaration; }

  friend class DOMCSSDeclarationImpl;

protected:
  DOMCSSDeclarationImpl mDOMDeclaration;

  nsICSSStyleRule* Rule() {
    return mDOMDeclaration.mRule;
  }
};

DOMCSSDeclarationImpl::DOMCSSDeclarationImpl(nsICSSStyleRule *aRule)
  : mRule(aRule)
{
  MOZ_COUNT_CTOR(DOMCSSDeclarationImpl);
}

DOMCSSDeclarationImpl::~DOMCSSDeclarationImpl(void)
{
  NS_ASSERTION(!mRule, "DropReference not called.");

  MOZ_COUNT_DTOR(DOMCSSDeclarationImpl);
}

inline DOMCSSStyleRuleImpl* DOMCSSDeclarationImpl::DomRule()
{
  return reinterpret_cast<DOMCSSStyleRuleImpl*>
                         (reinterpret_cast<char*>(this) -
           offsetof(DOMCSSStyleRuleImpl, mDOMDeclaration));
}

NS_IMPL_ADDREF_USING_AGGREGATOR(DOMCSSDeclarationImpl, DomRule())
NS_IMPL_RELEASE_USING_AGGREGATOR(DOMCSSDeclarationImpl, DomRule())

void
DOMCSSDeclarationImpl::DropReference(void)
{
  mRule = nsnull;
}

css::Declaration*
DOMCSSDeclarationImpl::GetCSSDeclaration(PRBool aAllocate)
{
  if (mRule) {
    return mRule->GetDeclaration();
  } else {
    return nsnull;
  }
}

/*
 * This is a utility function.  It will only fail if it can't get a
 * parser.  This means it can return NS_OK without aURI or aCSSLoader
 * being initialized.
 */
nsresult
DOMCSSDeclarationImpl::GetCSSParsingEnvironment(nsIURI** aSheetURI,
                                                nsIURI** aBaseURI,
                                                nsIPrincipal** aSheetPrincipal,
                                                mozilla::css::Loader** aCSSLoader)
{
  // null out the out params since some of them may not get initialized below
  *aSheetURI = nsnull;
  *aBaseURI = nsnull;
  *aSheetPrincipal = nsnull;
  *aCSSLoader = nsnull;

  nsCOMPtr<nsIStyleSheet> sheet;
  if (mRule) {
    sheet = mRule->GetStyleSheet();
    if (sheet) {
      NS_IF_ADDREF(*aSheetURI = sheet->GetSheetURI());
      NS_IF_ADDREF(*aBaseURI = sheet->GetBaseURI());

      nsRefPtr<nsCSSStyleSheet> cssSheet(do_QueryObject(sheet));
      if (cssSheet) {
        NS_ADDREF(*aSheetPrincipal = cssSheet->Principal());
      }

      nsIDocument* document = sheet->GetOwningDocument();
      if (document) {
        NS_ADDREF(*aCSSLoader = document->CSSLoader());
      }
    }
  }

  nsresult result = NS_OK;
  if (!*aSheetPrincipal) {
    result = CallCreateInstance("@mozilla.org/nullprincipal;1",
                                aSheetPrincipal);
  }

  return result;
}

NS_IMETHODIMP
DOMCSSDeclarationImpl::GetParentRule(nsIDOMCSSRule **aParent)
{
  NS_ENSURE_ARG_POINTER(aParent);

  if (!mRule) {
    *aParent = nsnull;
    return NS_OK;
  }

  return mRule->GetDOMRule(aParent);
}

nsresult
DOMCSSDeclarationImpl::SetCSSDeclaration(css::Declaration* aDecl)
{
  NS_PRECONDITION(mRule,
         "can only be called when |GetCSSDeclaration| returned a declaration");

  nsCOMPtr<nsIDocument> owningDoc;
  nsCOMPtr<nsIStyleSheet> sheet = mRule->GetStyleSheet();
  if (sheet) {
    owningDoc = sheet->GetOwningDocument();
  }

  mozAutoDocUpdate updateBatch(owningDoc, UPDATE_STYLE, PR_TRUE);

  nsCOMPtr<nsICSSStyleRule> oldRule = mRule;
  mRule = oldRule->DeclarationChanged(aDecl, PR_TRUE).get();
  if (!mRule)
    return NS_ERROR_OUT_OF_MEMORY;
  nsrefcnt cnt = mRule->Release();
  if (cnt == 0) {
    NS_NOTREACHED("container didn't take ownership");
    mRule = nsnull;
    return NS_ERROR_UNEXPECTED;
  }

  if (owningDoc) {
    owningDoc->StyleRuleChanged(sheet, oldRule, mRule);
  }
  return NS_OK;
}

nsIDocument*
DOMCSSDeclarationImpl::DocToUpdate()
{
  return nsnull;
}

DOMCSSStyleRuleImpl::DOMCSSStyleRuleImpl(nsICSSStyleRule* aRule)
  : mDOMDeclaration(aRule)
{
}

DOMCSSStyleRuleImpl::~DOMCSSStyleRuleImpl()
{
}

DOMCI_DATA(CSSStyleRule, DOMCSSStyleRuleImpl)

NS_INTERFACE_MAP_BEGIN(DOMCSSStyleRuleImpl)
  NS_INTERFACE_MAP_ENTRY(nsICSSStyleRuleDOMWrapper)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSStyleRule)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSRule)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(CSSStyleRule)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(DOMCSSStyleRuleImpl)
NS_IMPL_RELEASE(DOMCSSStyleRuleImpl)

NS_IMETHODIMP    
DOMCSSStyleRuleImpl::GetType(PRUint16* aType)
{
  *aType = nsIDOMCSSRule::STYLE_RULE;
  
  return NS_OK;
}

NS_IMETHODIMP    
DOMCSSStyleRuleImpl::GetCssText(nsAString& aCssText)
{
  if (!Rule()) {
    aCssText.Truncate();
    return NS_OK;
  }
  return Rule()->GetCssText(aCssText);
}

NS_IMETHODIMP    
DOMCSSStyleRuleImpl::SetCssText(const nsAString& aCssText)
{
  if (!Rule()) {
    return NS_OK;
  }
  return Rule()->SetCssText(aCssText);
}

NS_IMETHODIMP    
DOMCSSStyleRuleImpl::GetParentStyleSheet(nsIDOMCSSStyleSheet** aSheet)
{
  if (!Rule()) {
    *aSheet = nsnull;
    return NS_OK;
  }
  nsRefPtr<nsCSSStyleSheet> sheet;
  Rule()->GetParentStyleSheet(getter_AddRefs(sheet));
  NS_IF_ADDREF(*aSheet = sheet);
  return NS_OK;
}

NS_IMETHODIMP    
DOMCSSStyleRuleImpl::GetParentRule(nsIDOMCSSRule** aParentRule)
{
  if (!Rule()) {
    *aParentRule = nsnull;
    return NS_OK;
  }
  nsCOMPtr<nsICSSGroupRule> rule;
  Rule()->GetParentRule(getter_AddRefs(rule));
  if (!rule) {
    *aParentRule = nsnull;
    return NS_OK;
  }
  return rule->GetDOMRule(aParentRule);
}

NS_IMETHODIMP    
DOMCSSStyleRuleImpl::GetSelectorText(nsAString& aSelectorText)
{
  if (!Rule()) {
    aSelectorText.Truncate();
    return NS_OK;
  }
  return Rule()->GetSelectorText(aSelectorText);
}

NS_IMETHODIMP    
DOMCSSStyleRuleImpl::SetSelectorText(const nsAString& aSelectorText)
{
  if (!Rule()) {
    return NS_OK;
  }
  return Rule()->SetSelectorText(aSelectorText);
}

NS_IMETHODIMP    
DOMCSSStyleRuleImpl::GetStyle(nsIDOMCSSStyleDeclaration** aStyle)
{
  *aStyle = &mDOMDeclaration;
  NS_ADDREF(*aStyle);
  return NS_OK;
}

NS_IMETHODIMP
DOMCSSStyleRuleImpl::GetCSSStyleRule(nsICSSStyleRule **aResult)
{
  *aResult = Rule();
  NS_IF_ADDREF(*aResult);
  return NS_OK;
}

// -- nsCSSStyleRule -------------------------------

class CSSStyleRuleImpl : public nsCSSRule,
                         public nsICSSStyleRule
{
public:
  CSSStyleRuleImpl(nsCSSSelectorList* aSelector,
                   css::Declaration *aDeclaration);
private:
  // for |Clone|
  CSSStyleRuleImpl(const CSSStyleRuleImpl& aCopy);
  // for |DeclarationChanged|
  CSSStyleRuleImpl(CSSStyleRuleImpl& aCopy,
                   css::Declaration *aDeclaration);
public:

  NS_DECL_ISUPPORTS_INHERITED

  virtual nsCSSSelectorList* Selector(void);

  virtual PRUint32 GetLineNumber(void) const;
  virtual void SetLineNumber(PRUint32 aLineNumber);

  virtual css::Declaration* GetDeclaration(void) const;

  virtual nsIStyleRule* GetImportantRule(void);
  virtual void RuleMatched();

  virtual already_AddRefed<nsIStyleSheet> GetStyleSheet() const;
  virtual void SetStyleSheet(nsCSSStyleSheet* aSheet);

  virtual void SetParentRule(nsICSSGroupRule* aRule);

  virtual nsresult GetCssText(nsAString& aCssText);
  virtual nsresult SetCssText(const nsAString& aCssText);
  virtual nsresult GetParentStyleSheet(nsCSSStyleSheet** aSheet);
  virtual nsresult GetParentRule(nsICSSGroupRule** aParentRule);
  virtual nsresult GetSelectorText(nsAString& aSelectorText);
  virtual nsresult SetSelectorText(const nsAString& aSelectorText);

  virtual PRInt32 GetType() const;
  virtual already_AddRefed<nsICSSRule> Clone() const;

  nsIDOMCSSRule* GetDOMRuleWeak(nsresult* aResult);

  virtual already_AddRefed<nsICSSStyleRule>
  DeclarationChanged(css::Declaration* aDecl, PRBool aHandleContainer);

  // The new mapping function.
  virtual void MapRuleInfoInto(nsRuleData* aRuleData);

#ifdef DEBUG
  virtual void List(FILE* out = stdout, PRInt32 aIndent = 0) const;
#endif

private:
  // These are not supported and are not implemented!
  CSSStyleRuleImpl& operator=(const CSSStyleRuleImpl& aCopy);

protected:
  virtual ~CSSStyleRuleImpl(void);

protected:
  nsCSSSelectorList*      mSelector; // null for style attribute
  css::Declaration*       mDeclaration;
  CSSImportantRule*       mImportantRule; // initialized by RuleMatched
  DOMCSSStyleRuleImpl*    mDOMRule;
  PRUint32                mLineNumber;
  PRPackedBool            mWasMatched;
};

CSSStyleRuleImpl::CSSStyleRuleImpl(nsCSSSelectorList* aSelector,
                                   css::Declaration* aDeclaration)
  : nsCSSRule(),
    mSelector(aSelector),
    mDeclaration(aDeclaration),
    mImportantRule(nsnull),
    mDOMRule(nsnull),
    mLineNumber(0),
    mWasMatched(PR_FALSE)
{
}

// for |Clone|
CSSStyleRuleImpl::CSSStyleRuleImpl(const CSSStyleRuleImpl& aCopy)
  : nsCSSRule(aCopy),
    mSelector(aCopy.mSelector ? aCopy.mSelector->Clone() : nsnull),
    mDeclaration(new css::Declaration(*aCopy.mDeclaration)),
    mImportantRule(nsnull),
    mDOMRule(nsnull),
    mLineNumber(aCopy.mLineNumber),
    mWasMatched(PR_FALSE)
{
  // rest is constructed lazily on existing data
}

// for |SetCSSDeclaration|
CSSStyleRuleImpl::CSSStyleRuleImpl(CSSStyleRuleImpl& aCopy,
                                   css::Declaration* aDeclaration)
  : nsCSSRule(aCopy),
    mSelector(aCopy.mSelector),
    mDeclaration(aDeclaration),
    mImportantRule(nsnull),
    mDOMRule(aCopy.mDOMRule),
    mLineNumber(aCopy.mLineNumber),
    mWasMatched(PR_FALSE)
{
  // The DOM rule is replacing |aCopy| with |this|, so transfer
  // the reverse pointer as well (and transfer ownership).
  aCopy.mDOMRule = nsnull;

  // Similarly for the selector.
  aCopy.mSelector = nsnull;

  // We are probably replacing the old declaration with |aDeclaration|
  // instead of taking ownership of the old declaration; only null out
  // aCopy.mDeclaration if we are taking ownership.
  if (mDeclaration == aCopy.mDeclaration) {
    // This should only ever happen if the declaration was modifiable.
    mDeclaration->AssertMutable();
    aCopy.mDeclaration = nsnull;
  }
}

CSSStyleRuleImpl::~CSSStyleRuleImpl(void)
{
  delete mSelector;
  delete mDeclaration;
  NS_IF_RELEASE(mImportantRule);
  if (mDOMRule) {
    mDOMRule->DOMDeclaration()->DropReference();
    NS_RELEASE(mDOMRule);
  }
}

// QueryInterface implementation for CSSStyleRuleImpl
NS_INTERFACE_MAP_BEGIN(CSSStyleRuleImpl)
  NS_INTERFACE_MAP_ENTRY(nsICSSStyleRule)
  NS_INTERFACE_MAP_ENTRY(nsICSSRule)
  NS_INTERFACE_MAP_ENTRY(nsIStyleRule)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsICSSStyleRule)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF_INHERITED(CSSStyleRuleImpl, nsCSSRule)
NS_IMPL_RELEASE_INHERITED(CSSStyleRuleImpl, nsCSSRule)

nsCSSSelectorList* CSSStyleRuleImpl::Selector(void)
{
  return mSelector;
}

PRUint32 CSSStyleRuleImpl::GetLineNumber(void) const
{
  return mLineNumber;
}

void CSSStyleRuleImpl::SetLineNumber(PRUint32 aLineNumber)
{
  mLineNumber = aLineNumber;
}

css::Declaration* CSSStyleRuleImpl::GetDeclaration(void) const
{
  return mDeclaration;
}

nsIStyleRule* CSSStyleRuleImpl::GetImportantRule(void)
{
  return mImportantRule;
}

/* virtual */ void
CSSStyleRuleImpl::RuleMatched()
{
  if (!mWasMatched) {
    NS_ABORT_IF_FALSE(!mImportantRule, "should not have important rule yet");

    mWasMatched = PR_TRUE;
    mDeclaration->SetImmutable();
    if (mDeclaration->HasImportantData()) {
      NS_ADDREF(mImportantRule = new CSSImportantRule(mDeclaration));
    }
  }
}

/* virtual */ already_AddRefed<nsIStyleSheet>
CSSStyleRuleImpl::GetStyleSheet() const
{
// XXX What about inner, etc.
  return nsCSSRule::GetStyleSheet();
}

/* virtual */ void
CSSStyleRuleImpl::SetStyleSheet(nsCSSStyleSheet* aSheet)
{
  nsCSSRule::SetStyleSheet(aSheet);
}

/* virtual */ void
CSSStyleRuleImpl::SetParentRule(nsICSSGroupRule* aRule)
{
  nsCSSRule::SetParentRule(aRule);
}

/* virtual */ PRInt32
CSSStyleRuleImpl::GetType() const
{
  return nsICSSRule::STYLE_RULE;
}

/* virtual */ already_AddRefed<nsICSSRule>
CSSStyleRuleImpl::Clone() const
{
  nsCOMPtr<nsICSSRule> clone = new CSSStyleRuleImpl(*this);
  return clone.forget();
}

nsIDOMCSSRule*
CSSStyleRuleImpl::GetDOMRuleWeak(nsresult *aResult)
{
  *aResult = NS_OK;
  if (!mSheet) {
    // inline style rules aren't supposed to have a DOM rule object, only
    // a declaration.
    return nsnull;
  }
  if (!mDOMRule) {
    mDOMRule = new DOMCSSStyleRuleImpl(this);
    if (!mDOMRule) {
      *aResult = NS_ERROR_OUT_OF_MEMORY;
      return nsnull;
    }
    NS_ADDREF(mDOMRule);
  }
  return mDOMRule;
}

/* virtual */ already_AddRefed<nsICSSStyleRule>
CSSStyleRuleImpl::DeclarationChanged(css::Declaration* aDecl,
                                     PRBool aHandleContainer)
{
  CSSStyleRuleImpl* clone = new CSSStyleRuleImpl(*this, aDecl);
  if (!clone) {
    return nsnull;
  }

  NS_ADDREF(clone); // for return

  if (aHandleContainer) {
    NS_ASSERTION(mSheet, "rule must be in a sheet");
    if (mParentRule) {
      mSheet->ReplaceRuleInGroup(mParentRule, this, clone);
    } else {
      mSheet->ReplaceStyleRule(this, clone);
    }
  }

  return clone;
}

/* virtual */ void
CSSStyleRuleImpl::MapRuleInfoInto(nsRuleData* aRuleData)
{
  NS_ABORT_IF_FALSE(mWasMatched,
                    "somebody forgot to call nsICSSStyleRule::RuleMatched");
  mDeclaration->MapNormalRuleInfoInto(aRuleData);
}

#ifdef DEBUG
/* virtual */ void
CSSStyleRuleImpl::List(FILE* out, PRInt32 aIndent) const
{
  // Indent
  for (PRInt32 index = aIndent; --index >= 0; ) fputs("  ", out);

  nsAutoString buffer;
  if (mSelector)
    mSelector->ToString(buffer, mSheet);

  buffer.AppendLiteral(" ");
  fputs(NS_LossyConvertUTF16toASCII(buffer).get(), out);
  if (nsnull != mDeclaration) {
    mDeclaration->List(out);
  }
  else {
    fputs("{ null declaration }", out);
  }
  fputs("\n", out);
}
#endif

/* virtual */ nsresult
CSSStyleRuleImpl::GetCssText(nsAString& aCssText)
{
  if (mSelector) {
    mSelector->ToString(aCssText, mSheet);
    aCssText.Append(PRUnichar(' '));
  }
  aCssText.Append(PRUnichar('{'));
  aCssText.Append(PRUnichar(' '));
  if (mDeclaration)
  {
    nsAutoString   tempString;
    mDeclaration->ToString( tempString );
    aCssText.Append( tempString );
  }
  aCssText.Append(PRUnichar(' '));
  aCssText.Append(PRUnichar('}'));
  return NS_OK;
}

/* virtual */ nsresult    
CSSStyleRuleImpl::SetCssText(const nsAString& aCssText)
{
  // XXX TBI - need to re-parse rule & declaration
  return NS_OK;
}

/* virtual */ nsresult    
CSSStyleRuleImpl::GetParentStyleSheet(nsCSSStyleSheet** aSheet)
{
  *aSheet = mSheet;
  NS_IF_ADDREF(*aSheet);
  return NS_OK;
}

/* virtual */ nsresult    
CSSStyleRuleImpl::GetParentRule(nsICSSGroupRule** aParentRule)
{
  *aParentRule = mParentRule;
  NS_IF_ADDREF(*aParentRule);
  return NS_OK;
}

/* virtual */ nsresult    
CSSStyleRuleImpl::GetSelectorText(nsAString& aSelectorText)
{
  if (mSelector)
    mSelector->ToString(aSelectorText, mSheet);
  else
    aSelectorText.Truncate();
  return NS_OK;
}

/* virtual */ nsresult    
CSSStyleRuleImpl::SetSelectorText(const nsAString& aSelectorText)
{
  // XXX TBI - get a parser and re-parse the selectors, 
  // XXX then need to re-compute the cascade
  // XXX and dirty sheet
  return NS_OK;
}

already_AddRefed<nsICSSStyleRule>
NS_NewCSSStyleRule(nsCSSSelectorList* aSelector,
                   css::Declaration* aDeclaration)
{
  NS_PRECONDITION(aDeclaration, "must have a declaration");
  CSSStyleRuleImpl *it = new CSSStyleRuleImpl(aSelector, aDeclaration);
  NS_ADDREF(it);
  return it;
}
