/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
// vim:cindent:tabstop=2:expandtab:shiftwidth=2:
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* representation of a CSS style sheet */

#include "nsCSSStyleSheet.h"

#include "nsCRT.h"
#include "nsIAtom.h"
#include "nsCSSRuleProcessor.h"
#include "mozilla/css/NameSpaceRule.h"
#include "mozilla/css/GroupRule.h"
#include "mozilla/css/ImportRule.h"
#include "nsIMediaList.h"
#include "nsIDocument.h"
#include "nsPresContext.h"
#include "nsGkAtoms.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsIDOMCSSStyleSheet.h"
#include "nsICSSRuleList.h"
#include "nsIDOMMediaList.h"
#include "nsIDOMNode.h"
#include "nsError.h"
#include "nsCSSParser.h"
#include "mozilla/css/Loader.h"
#include "nsICSSLoaderObserver.h"
#include "nsINameSpaceManager.h"
#include "nsXMLNameSpaceMap.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsIScriptSecurityManager.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/css/Declaration.h"
#include "nsRuleNode.h"
#include "nsMediaFeatures.h"
#include "nsDOMClassInfoID.h"
#include "mozilla/Likely.h"

using namespace mozilla;


// -------------------------------
// Style Rule List for the DOM
//
class CSSRuleListImpl : public nsICSSRuleList
{
public:
  CSSRuleListImpl(nsCSSStyleSheet *aStyleSheet);

  NS_DECL_ISUPPORTS

  // nsIDOMCSSRuleList interface
  NS_IMETHOD    GetLength(uint32_t* aLength); 
  NS_IMETHOD    Item(uint32_t aIndex, nsIDOMCSSRule** aReturn); 

  virtual nsIDOMCSSRule* GetItemAt(uint32_t aIndex, nsresult* aResult);

  void DropReference() { mStyleSheet = nullptr; }

protected:
  virtual ~CSSRuleListImpl();

  nsCSSStyleSheet*  mStyleSheet;
};

CSSRuleListImpl::CSSRuleListImpl(nsCSSStyleSheet *aStyleSheet)
{
  // Not reference counted to avoid circular references.
  // The style sheet will tell us when its going away.
  mStyleSheet = aStyleSheet;
}

CSSRuleListImpl::~CSSRuleListImpl()
{
}

DOMCI_DATA(CSSRuleList, CSSRuleListImpl)

// QueryInterface implementation for CSSRuleList
NS_INTERFACE_MAP_BEGIN(CSSRuleListImpl)
  NS_INTERFACE_MAP_ENTRY(nsICSSRuleList)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSRuleList)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(CSSRuleList)
NS_INTERFACE_MAP_END


NS_IMPL_ADDREF(CSSRuleListImpl)
NS_IMPL_RELEASE(CSSRuleListImpl)


NS_IMETHODIMP    
CSSRuleListImpl::GetLength(uint32_t* aLength)
{
  if (nullptr != mStyleSheet) {
    int32_t count = mStyleSheet->StyleRuleCount();
    *aLength = (uint32_t)count;
  }
  else {
    *aLength = 0;
  }

  return NS_OK;
}

nsIDOMCSSRule*    
CSSRuleListImpl::GetItemAt(uint32_t aIndex, nsresult* aResult)
{
  nsresult result = NS_OK;

  if (mStyleSheet) {
    // ensure rules have correct parent
    if (mStyleSheet->EnsureUniqueInner() !=
          nsCSSStyleSheet::eUniqueInner_CloneFailed) {
      nsRefPtr<css::Rule> rule;

      result = mStyleSheet->GetStyleRuleAt(aIndex, *getter_AddRefs(rule));
      if (rule) {
        *aResult = NS_OK;
        return rule->GetDOMRule();
      }
      if (result == NS_ERROR_ILLEGAL_VALUE) {
        result = NS_OK; // per spec: "Return Value ... null if ... not a valid index."
      }
    }
  }

  *aResult = result;
  return nullptr;
}

NS_IMETHODIMP    
CSSRuleListImpl::Item(uint32_t aIndex, nsIDOMCSSRule** aReturn)
{
  nsresult rv;
  nsIDOMCSSRule* rule = GetItemAt(aIndex, &rv);
  if (!rule) {
    *aReturn = nullptr;

    return rv;
  }

  NS_ADDREF(*aReturn = rule);
  return NS_OK;
}

template <class Numeric>
int32_t DoCompare(Numeric a, Numeric b)
{
  if (a == b)
    return 0;
  if (a < b)
    return -1;
  return 1;
}

bool
nsMediaExpression::Matches(nsPresContext *aPresContext,
                           const nsCSSValue& aActualValue) const
{
  const nsCSSValue& actual = aActualValue;
  const nsCSSValue& required = mValue;

  // If we don't have the feature, the match fails.
  if (actual.GetUnit() == eCSSUnit_Null) {
    return false;
  }

  // If the expression had no value to match, the match succeeds,
  // unless the value is an integer 0 or a zero length.
  if (required.GetUnit() == eCSSUnit_Null) {
    if (actual.GetUnit() == eCSSUnit_Integer)
      return actual.GetIntValue() != 0;
    if (actual.IsLengthUnit())
      return actual.GetFloatValue() != 0;
    return true;
  }

  NS_ASSERTION(mFeature->mRangeType == nsMediaFeature::eMinMaxAllowed ||
               mRange == nsMediaExpression::eEqual, "yikes");
  int32_t cmp; // -1 (actual < required)
               //  0 (actual == required)
               //  1 (actual > required)
  switch (mFeature->mValueType) {
    case nsMediaFeature::eLength:
      {
        NS_ASSERTION(actual.IsLengthUnit(), "bad actual value");
        NS_ASSERTION(required.IsLengthUnit(), "bad required value");
        nscoord actualCoord = nsRuleNode::CalcLengthWithInitialFont(
                                aPresContext, actual);
        nscoord requiredCoord = nsRuleNode::CalcLengthWithInitialFont(
                                  aPresContext, required);
        cmp = DoCompare(actualCoord, requiredCoord);
      }
      break;
    case nsMediaFeature::eInteger:
    case nsMediaFeature::eBoolInteger:
      {
        NS_ASSERTION(actual.GetUnit() == eCSSUnit_Integer,
                     "bad actual value");
        NS_ASSERTION(required.GetUnit() == eCSSUnit_Integer,
                     "bad required value");
        NS_ASSERTION(mFeature->mValueType != nsMediaFeature::eBoolInteger ||
                     actual.GetIntValue() == 0 || actual.GetIntValue() == 1,
                     "bad actual bool integer value");
        NS_ASSERTION(mFeature->mValueType != nsMediaFeature::eBoolInteger ||
                     required.GetIntValue() == 0 || required.GetIntValue() == 1,
                     "bad required bool integer value");
        cmp = DoCompare(actual.GetIntValue(), required.GetIntValue());
      }
      break;
    case nsMediaFeature::eFloat:
      {
        NS_ASSERTION(actual.GetUnit() == eCSSUnit_Number,
                     "bad actual value");
        NS_ASSERTION(required.GetUnit() == eCSSUnit_Number,
                     "bad required value");
        cmp = DoCompare(actual.GetFloatValue(), required.GetFloatValue());
      }
      break;
    case nsMediaFeature::eIntRatio:
      {
        NS_ASSERTION(actual.GetUnit() == eCSSUnit_Array &&
                     actual.GetArrayValue()->Count() == 2 &&
                     actual.GetArrayValue()->Item(0).GetUnit() ==
                       eCSSUnit_Integer &&
                     actual.GetArrayValue()->Item(1).GetUnit() ==
                       eCSSUnit_Integer,
                     "bad actual value");
        NS_ASSERTION(required.GetUnit() == eCSSUnit_Array &&
                     required.GetArrayValue()->Count() == 2 &&
                     required.GetArrayValue()->Item(0).GetUnit() ==
                       eCSSUnit_Integer &&
                     required.GetArrayValue()->Item(1).GetUnit() ==
                       eCSSUnit_Integer,
                     "bad required value");
        // Convert to int64_t so we can multiply without worry.  Note
        // that while the spec requires that both halves of |required|
        // be positive, the numerator or denominator of |actual| might
        // be zero (e.g., when testing 'aspect-ratio' on a 0-width or
        // 0-height iframe).
        int64_t actualNum = actual.GetArrayValue()->Item(0).GetIntValue(),
                actualDen = actual.GetArrayValue()->Item(1).GetIntValue(),
                requiredNum = required.GetArrayValue()->Item(0).GetIntValue(),
                requiredDen = required.GetArrayValue()->Item(1).GetIntValue();
        cmp = DoCompare(actualNum * requiredDen, requiredNum * actualDen);
      }
      break;
    case nsMediaFeature::eResolution:
      {
        NS_ASSERTION(actual.GetUnit() == eCSSUnit_Inch ||
                     actual.GetUnit() == eCSSUnit_Pixel ||
                     actual.GetUnit() == eCSSUnit_Centimeter,
                     "bad actual value");
        NS_ASSERTION(required.GetUnit() == eCSSUnit_Inch ||
                     required.GetUnit() == eCSSUnit_Pixel ||
                     required.GetUnit() == eCSSUnit_Centimeter,
                     "bad required value");
        float actualDPI = actual.GetFloatValue();
        if (actual.GetUnit() == eCSSUnit_Centimeter) {
          actualDPI = actualDPI * 2.54f;
        } else if (actual.GetUnit() == eCSSUnit_Pixel) {
          actualDPI = actualDPI * 96.0f;
        }
        float requiredDPI = required.GetFloatValue();
        if (required.GetUnit() == eCSSUnit_Centimeter) {
          requiredDPI = requiredDPI * 2.54f;
        } else if (required.GetUnit() == eCSSUnit_Pixel) {
          requiredDPI = requiredDPI * 96.0f;
        }
        cmp = DoCompare(actualDPI, requiredDPI);
      }
      break;
    case nsMediaFeature::eEnumerated:
      {
        NS_ASSERTION(actual.GetUnit() == eCSSUnit_Enumerated,
                     "bad actual value");
        NS_ASSERTION(required.GetUnit() == eCSSUnit_Enumerated,
                     "bad required value");
        NS_ASSERTION(mFeature->mRangeType == nsMediaFeature::eMinMaxNotAllowed,
                     "bad range"); // we asserted above about mRange
        // We don't really need DoCompare, but it doesn't hurt (and
        // maybe the compiler will condense this case with eInteger).
        cmp = DoCompare(actual.GetIntValue(), required.GetIntValue());
      }
      break;
    case nsMediaFeature::eIdent:
      {
        NS_ASSERTION(actual.GetUnit() == eCSSUnit_Ident,
                     "bad actual value");
        NS_ASSERTION(required.GetUnit() == eCSSUnit_Ident,
                     "bad required value");
        NS_ASSERTION(mFeature->mRangeType == nsMediaFeature::eMinMaxNotAllowed,
                     "bad range"); 
        cmp = !(actual == required); // string comparison
      }
      break;
  }
  switch (mRange) {
    case nsMediaExpression::eMin:
      return cmp != -1;
    case nsMediaExpression::eMax:
      return cmp != 1;
    case nsMediaExpression::eEqual:
      return cmp == 0;
  }
  NS_NOTREACHED("unexpected mRange");
  return false;
}

void
nsMediaQueryResultCacheKey::AddExpression(const nsMediaExpression* aExpression,
                                          bool aExpressionMatches)
{
  const nsMediaFeature *feature = aExpression->mFeature;
  FeatureEntry *entry = nullptr;
  for (uint32_t i = 0; i < mFeatureCache.Length(); ++i) {
    if (mFeatureCache[i].mFeature == feature) {
      entry = &mFeatureCache[i];
      break;
    }
  }
  if (!entry) {
    entry = mFeatureCache.AppendElement();
    if (!entry) {
      return; /* out of memory */
    }
    entry->mFeature = feature;
  }

  ExpressionEntry eentry = { *aExpression, aExpressionMatches };
  entry->mExpressions.AppendElement(eentry);
}

bool
nsMediaQueryResultCacheKey::Matches(nsPresContext* aPresContext) const
{
  if (aPresContext->Medium() != mMedium) {
    return false;
  }

  for (uint32_t i = 0; i < mFeatureCache.Length(); ++i) {
    const FeatureEntry *entry = &mFeatureCache[i];
    nsCSSValue actual;
    nsresult rv =
      (entry->mFeature->mGetter)(aPresContext, entry->mFeature, actual);
    NS_ENSURE_SUCCESS(rv, false); // any better ideas?

    for (uint32_t j = 0; j < entry->mExpressions.Length(); ++j) {
      const ExpressionEntry &eentry = entry->mExpressions[j];
      if (eentry.mExpression.Matches(aPresContext, actual) !=
          eentry.mExpressionMatches) {
        return false;
      }
    }
  }

  return true;
}

void
nsMediaQuery::AppendToString(nsAString& aString) const
{
  if (mHadUnknownExpression) {
    aString.AppendLiteral("not all");
    return;
  }

  NS_ASSERTION(!mNegated || !mHasOnly, "can't have not and only");
  NS_ASSERTION(!mTypeOmitted || (!mNegated && !mHasOnly),
               "can't have not or only when type is omitted");
  if (!mTypeOmitted) {
    if (mNegated) {
      aString.AppendLiteral("not ");
    } else if (mHasOnly) {
      aString.AppendLiteral("only ");
    }
    aString.Append(nsDependentAtomString(mMediaType));
  }

  for (uint32_t i = 0, i_end = mExpressions.Length(); i < i_end; ++i) {
    if (i > 0 || !mTypeOmitted)
      aString.AppendLiteral(" and ");
    aString.AppendLiteral("(");

    const nsMediaExpression &expr = mExpressions[i];
    if (expr.mRange == nsMediaExpression::eMin) {
      aString.AppendLiteral("min-");
    } else if (expr.mRange == nsMediaExpression::eMax) {
      aString.AppendLiteral("max-");
    }

    const nsMediaFeature *feature = expr.mFeature;
    aString.Append(nsDependentAtomString(*feature->mName));

    if (expr.mValue.GetUnit() != eCSSUnit_Null) {
      aString.AppendLiteral(": ");
      switch (feature->mValueType) {
        case nsMediaFeature::eLength:
          NS_ASSERTION(expr.mValue.IsLengthUnit(), "bad unit");
          // Use 'width' as a property that takes length values
          // written in the normal way.
          expr.mValue.AppendToString(eCSSProperty_width, aString);
          break;
        case nsMediaFeature::eInteger:
        case nsMediaFeature::eBoolInteger:
          NS_ASSERTION(expr.mValue.GetUnit() == eCSSUnit_Integer,
                       "bad unit");
          // Use 'z-index' as a property that takes integer values
          // written without anything extra.
          expr.mValue.AppendToString(eCSSProperty_z_index, aString);
          break;
        case nsMediaFeature::eFloat:
          {
            NS_ASSERTION(expr.mValue.GetUnit() == eCSSUnit_Number,
                         "bad unit");
            // Use 'line-height' as a property that takes float values
            // written in the normal way.
            expr.mValue.AppendToString(eCSSProperty_line_height, aString);
          }
          break;
        case nsMediaFeature::eIntRatio:
          {
            NS_ASSERTION(expr.mValue.GetUnit() == eCSSUnit_Array,
                         "bad unit");
            nsCSSValue::Array *array = expr.mValue.GetArrayValue();
            NS_ASSERTION(array->Count() == 2, "unexpected length");
            NS_ASSERTION(array->Item(0).GetUnit() == eCSSUnit_Integer,
                         "bad unit");
            NS_ASSERTION(array->Item(1).GetUnit() == eCSSUnit_Integer,
                         "bad unit");
            array->Item(0).AppendToString(eCSSProperty_z_index, aString);
            aString.AppendLiteral("/");
            array->Item(1).AppendToString(eCSSProperty_z_index, aString);
          }
          break;
        case nsMediaFeature::eResolution:
          {
            aString.AppendFloat(expr.mValue.GetFloatValue());
            if (expr.mValue.GetUnit() == eCSSUnit_Inch) {
              aString.AppendLiteral("dpi");
            } else if (expr.mValue.GetUnit() == eCSSUnit_Pixel) {
              aString.AppendLiteral("dppx");
            } else {
              NS_ASSERTION(expr.mValue.GetUnit() == eCSSUnit_Centimeter,
                           "bad unit");
              aString.AppendLiteral("dpcm");
            }
          }
          break;
        case nsMediaFeature::eEnumerated:
          NS_ASSERTION(expr.mValue.GetUnit() == eCSSUnit_Enumerated,
                       "bad unit");
          AppendASCIItoUTF16(
              nsCSSProps::ValueToKeyword(expr.mValue.GetIntValue(),
                                         feature->mData.mKeywordTable),
              aString);
          break;
        case nsMediaFeature::eIdent:
          NS_ASSERTION(expr.mValue.GetUnit() == eCSSUnit_Ident,
                       "bad unit");
          aString.Append(expr.mValue.GetStringBufferValue());
          break;
      }
    }

    aString.AppendLiteral(")");
  }
}

nsMediaQuery*
nsMediaQuery::Clone() const
{
  nsAutoPtr<nsMediaQuery> result(new nsMediaQuery(*this));
  NS_ENSURE_TRUE(result &&
                   result->mExpressions.Length() == mExpressions.Length(),
                 nullptr);
  return result.forget();
}

bool
nsMediaQuery::Matches(nsPresContext* aPresContext,
                      nsMediaQueryResultCacheKey* aKey) const
{
  if (mHadUnknownExpression)
    return false;

  bool match =
    mMediaType == aPresContext->Medium() || mMediaType == nsGkAtoms::all;
  for (uint32_t i = 0, i_end = mExpressions.Length(); match && i < i_end; ++i) {
    const nsMediaExpression &expr = mExpressions[i];
    nsCSSValue actual;
    nsresult rv =
      (expr.mFeature->mGetter)(aPresContext, expr.mFeature, actual);
    NS_ENSURE_SUCCESS(rv, false); // any better ideas?

    match = expr.Matches(aPresContext, actual);
    if (aKey) {
      aKey->AddExpression(&expr, match);
    }
  }

  return match == !mNegated;
}

DOMCI_DATA(MediaList, nsMediaList)

NS_INTERFACE_MAP_BEGIN(nsMediaList)
  NS_INTERFACE_MAP_ENTRY(nsIDOMMediaList)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(MediaList)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(nsMediaList)
NS_IMPL_RELEASE(nsMediaList)


nsMediaList::nsMediaList()
  : mStyleSheet(nullptr)
{
}

nsMediaList::~nsMediaList()
{
}

nsresult
nsMediaList::GetText(nsAString& aMediaText)
{
  aMediaText.Truncate();

  for (int32_t i = 0, i_end = mArray.Length(); i < i_end; ++i) {
    nsMediaQuery* query = mArray[i];
    NS_ENSURE_TRUE(query, NS_ERROR_FAILURE);

    query->AppendToString(aMediaText);

    if (i + 1 < i_end) {
      aMediaText.AppendLiteral(", ");
    }
  }

  return NS_OK;
}

// XXXbz this is so ill-defined in the spec, it's not clear quite what
// it should be doing....
nsresult
nsMediaList::SetText(const nsAString& aMediaText)
{
  nsCSSParser parser;

  bool htmlMode = false;
  if (mStyleSheet) {
    nsCOMPtr<nsIDOMNode> node;
    mStyleSheet->GetOwnerNode(getter_AddRefs(node));
    htmlMode = !!node;
  }

  return parser.ParseMediaList(aMediaText, nullptr, 0,
                               this, htmlMode);
}

bool
nsMediaList::Matches(nsPresContext* aPresContext,
                     nsMediaQueryResultCacheKey* aKey)
{
  for (int32_t i = 0, i_end = mArray.Length(); i < i_end; ++i) {
    if (mArray[i]->Matches(aPresContext, aKey)) {
      return true;
    }
  }
  return mArray.IsEmpty();
}

nsresult
nsMediaList::SetStyleSheet(nsCSSStyleSheet *aSheet)
{
  NS_ASSERTION(aSheet == mStyleSheet || !aSheet || !mStyleSheet,
               "multiple style sheets competing for one media list");
  mStyleSheet = aSheet;
  return NS_OK;
}

nsresult
nsMediaList::Clone(nsMediaList** aResult)
{
  nsRefPtr<nsMediaList> result = new nsMediaList();
  if (!result || !result->mArray.AppendElements(mArray.Length()))
    return NS_ERROR_OUT_OF_MEMORY;
  for (int32_t i = 0, i_end = mArray.Length(); i < i_end; ++i) {
    if (!(result->mArray[i] = mArray[i]->Clone())) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }
  NS_ADDREF(*aResult = result);
  return NS_OK;
}

NS_IMETHODIMP
nsMediaList::GetMediaText(nsAString& aMediaText)
{
  return GetText(aMediaText);
}

// "sheet" should be an nsCSSStyleSheet and "doc" should be an
// nsCOMPtr<nsIDocument>
#define BEGIN_MEDIA_CHANGE(sheet, doc)                         \
  if (sheet) {                                                 \
    doc = sheet->GetOwningDocument();                          \
  }                                                            \
  mozAutoDocUpdate updateBatch(doc, UPDATE_STYLE, true);    \
  if (sheet) {                                                 \
    rv = sheet->WillDirty();                                   \
    NS_ENSURE_SUCCESS(rv, rv);                                 \
  }

#define END_MEDIA_CHANGE(sheet, doc)                           \
  if (sheet) {                                                 \
    sheet->DidDirty();                                         \
  }                                                            \
  /* XXXldb Pass something meaningful? */                      \
  if (doc) {                                                   \
    doc->StyleRuleChanged(sheet, nullptr, nullptr);              \
  }


NS_IMETHODIMP
nsMediaList::SetMediaText(const nsAString& aMediaText)
{
  nsresult rv = NS_OK;
  nsCOMPtr<nsIDocument> doc;

  BEGIN_MEDIA_CHANGE(mStyleSheet, doc)

  rv = SetText(aMediaText);
  if (NS_FAILED(rv))
    return rv;
  
  END_MEDIA_CHANGE(mStyleSheet, doc)

  return rv;
}
                               
NS_IMETHODIMP
nsMediaList::GetLength(uint32_t* aLength)
{
  NS_ENSURE_ARG_POINTER(aLength);

  *aLength = mArray.Length();
  return NS_OK;
}

NS_IMETHODIMP
nsMediaList::Item(uint32_t aIndex, nsAString& aReturn)
{
  int32_t index = aIndex;
  if (0 <= index && index < Count()) {
    nsMediaQuery* query = mArray[index];
    NS_ENSURE_TRUE(query, NS_ERROR_FAILURE);

    aReturn.Truncate();
    query->AppendToString(aReturn);
  } else {
    SetDOMStringToNull(aReturn);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsMediaList::DeleteMedium(const nsAString& aOldMedium)
{
  nsresult rv = NS_OK;
  nsCOMPtr<nsIDocument> doc;

  BEGIN_MEDIA_CHANGE(mStyleSheet, doc)
  
  rv = Delete(aOldMedium);
  if (NS_FAILED(rv))
    return rv;

  END_MEDIA_CHANGE(mStyleSheet, doc)
  
  return rv;
}

NS_IMETHODIMP
nsMediaList::AppendMedium(const nsAString& aNewMedium)
{
  nsresult rv = NS_OK;
  nsCOMPtr<nsIDocument> doc;

  BEGIN_MEDIA_CHANGE(mStyleSheet, doc)
  
  rv = Append(aNewMedium);
  if (NS_FAILED(rv))
    return rv;

  END_MEDIA_CHANGE(mStyleSheet, doc)
  
  return rv;
}

nsresult
nsMediaList::Delete(const nsAString& aOldMedium)
{
  if (aOldMedium.IsEmpty())
    return NS_ERROR_DOM_NOT_FOUND_ERR;

  for (int32_t i = 0, i_end = mArray.Length(); i < i_end; ++i) {
    nsMediaQuery* query = mArray[i];
    NS_ENSURE_TRUE(query, NS_ERROR_FAILURE);

    nsAutoString buf;
    query->AppendToString(buf);
    if (buf == aOldMedium) {
      mArray.RemoveElementAt(i);
      return NS_OK;
    }
  }

  return NS_ERROR_DOM_NOT_FOUND_ERR;
}

nsresult
nsMediaList::Append(const nsAString& aNewMedium)
{
  if (aNewMedium.IsEmpty())
    return NS_ERROR_DOM_NOT_FOUND_ERR;

  Delete(aNewMedium);

  nsresult rv = NS_OK;
  nsTArray<nsAutoPtr<nsMediaQuery> > buf;
#ifdef DEBUG
  bool ok = 
#endif
    mArray.SwapElements(buf);
  NS_ASSERTION(ok, "SwapElements should never fail when neither array "
                   "is an auto array");
  SetText(aNewMedium);
  if (mArray.Length() == 1) {
    nsMediaQuery *query = mArray[0].forget();
    if (!buf.AppendElement(query)) {
      delete query;
      rv = NS_ERROR_OUT_OF_MEMORY;
    }
  }
#ifdef DEBUG
  ok = 
#endif
    mArray.SwapElements(buf);
  NS_ASSERTION(ok, "SwapElements should never fail when neither array "
                   "is an auto array");
  return rv;
}

// -------------------------------
// CSS Style Sheet Inner Data Container
//


nsCSSStyleSheetInner::nsCSSStyleSheetInner(nsCSSStyleSheet* aPrimarySheet,
                                           CORSMode aCORSMode)
  : mSheets(),
    mCORSMode(aCORSMode),
    mComplete(false)
#ifdef DEBUG
    , mPrincipalSet(false)
#endif
{
  MOZ_COUNT_CTOR(nsCSSStyleSheetInner);
  mSheets.AppendElement(aPrimarySheet);

  mPrincipal = do_CreateInstance("@mozilla.org/nullprincipal;1");
  if (!mPrincipal) {
    NS_RUNTIMEABORT("OOM");
  }
}

static bool SetStyleSheetReference(css::Rule* aRule, void* aSheet)
{
  if (aRule) {
    aRule->SetStyleSheet(static_cast<nsCSSStyleSheet*>(aSheet));
  }
  return true;
}

static bool
CloneRuleInto(css::Rule* aRule, void* aArray)
{
  nsRefPtr<css::Rule> clone = aRule->Clone();
  static_cast<nsCOMArray<css::Rule>*>(aArray)->AppendObject(clone);
  return true;
}

struct ChildSheetListBuilder {
  nsRefPtr<nsCSSStyleSheet>* sheetSlot;
  nsCSSStyleSheet* parent;

  void SetParentLinks(nsCSSStyleSheet* aSheet) {
    aSheet->mParent = parent;
    aSheet->SetOwningDocument(parent->mDocument);
  }

  static void ReparentChildList(nsCSSStyleSheet* aPrimarySheet,
                                nsCSSStyleSheet* aFirstChild)
  {
    for (nsCSSStyleSheet *child = aFirstChild; child; child = child->mNext) {
      child->mParent = aPrimarySheet;
      child->SetOwningDocument(aPrimarySheet->mDocument);
    }
  }
};
  
bool
nsCSSStyleSheet::RebuildChildList(css::Rule* aRule, void* aBuilder)
{
  int32_t type = aRule->GetType();
  if (type < css::Rule::IMPORT_RULE) {
    // Keep going till we get to the import rules.
    return true;
  }

  if (type != css::Rule::IMPORT_RULE) {
    // We're past all the import rules; stop the enumeration.
    return false;
  }

  ChildSheetListBuilder* builder =
    static_cast<ChildSheetListBuilder*>(aBuilder);

  // XXXbz We really need to decomtaminate all this stuff.  Is there a reason
  // that I can't just QI to ImportRule and get an nsCSSStyleSheet
  // directly from it?
  nsCOMPtr<nsIDOMCSSImportRule> importRule(do_QueryInterface(aRule));
  NS_ASSERTION(importRule, "GetType lied");

  nsCOMPtr<nsIDOMCSSStyleSheet> childSheet;
  importRule->GetStyleSheet(getter_AddRefs(childSheet));

  // Have to do this QI to be safe, since XPConnect can fake
  // nsIDOMCSSStyleSheets
  nsRefPtr<nsCSSStyleSheet> cssSheet = do_QueryObject(childSheet);
  if (!cssSheet) {
    return true;
  }

  (*builder->sheetSlot) = cssSheet;
  builder->SetParentLinks(*builder->sheetSlot);
  builder->sheetSlot = &(*builder->sheetSlot)->mNext;
  return true;
}

size_t
nsCSSStyleSheet::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf) const
{
  size_t n = 0;
  const nsCSSStyleSheet* s = this;
  while (s) {
    n += aMallocSizeOf(s);

    // Each inner can be shared by multiple sheets.  So we only count the inner
    // if this sheet is the first one in the list of those sharing it.  As a
    // result, the first such sheet takes all the blame for the memory
    // consumption of the inner, which isn't ideal but it's better than
    // double-counting the inner.
    if (s->mInner->mSheets[0] == s) {
      n += s->mInner->SizeOfIncludingThis(aMallocSizeOf);
    }

    // Measurement of the following members may be added later if DMD finds it
    // is worthwhile:
    // - s->mTitle
    // - s->mMedia
    // - s->mRuleCollection
    // - s->mRuleProcessors
    //
    // The following members are not measured:
    // - s->mOwnerRule, because it's non-owning

    s = s->mNext;
  }
  return n;
}

nsCSSStyleSheetInner::nsCSSStyleSheetInner(nsCSSStyleSheetInner& aCopy,
                                           nsCSSStyleSheet* aPrimarySheet)
  : mSheets(),
    mSheetURI(aCopy.mSheetURI),
    mOriginalSheetURI(aCopy.mOriginalSheetURI),
    mBaseURI(aCopy.mBaseURI),
    mPrincipal(aCopy.mPrincipal),
    mCORSMode(aCopy.mCORSMode),
    mComplete(aCopy.mComplete)
#ifdef DEBUG
    , mPrincipalSet(aCopy.mPrincipalSet)
#endif
{
  MOZ_COUNT_CTOR(nsCSSStyleSheetInner);
  AddSheet(aPrimarySheet);
  aCopy.mOrderedRules.EnumerateForwards(CloneRuleInto, &mOrderedRules);
  mOrderedRules.EnumerateForwards(SetStyleSheetReference, aPrimarySheet);

  ChildSheetListBuilder builder = { &mFirstChild, aPrimarySheet };
  mOrderedRules.EnumerateForwards(nsCSSStyleSheet::RebuildChildList, &builder);

  RebuildNameSpaces();
}

nsCSSStyleSheetInner::~nsCSSStyleSheetInner()
{
  MOZ_COUNT_DTOR(nsCSSStyleSheetInner);
  mOrderedRules.EnumerateForwards(SetStyleSheetReference, nullptr);
}

nsCSSStyleSheetInner* 
nsCSSStyleSheetInner::CloneFor(nsCSSStyleSheet* aPrimarySheet)
{
  return new nsCSSStyleSheetInner(*this, aPrimarySheet);
}

void
nsCSSStyleSheetInner::AddSheet(nsCSSStyleSheet* aSheet)
{
  mSheets.AppendElement(aSheet);
}

void
nsCSSStyleSheetInner::RemoveSheet(nsCSSStyleSheet* aSheet)
{
  if (1 == mSheets.Length()) {
    NS_ASSERTION(aSheet == mSheets.ElementAt(0), "bad parent");
    delete this;
    return;
  }
  if (aSheet == mSheets.ElementAt(0)) {
    mSheets.RemoveElementAt(0);
    NS_ASSERTION(mSheets.Length(), "no parents");
    mOrderedRules.EnumerateForwards(SetStyleSheetReference,
                                    mSheets.ElementAt(0));

    ChildSheetListBuilder::ReparentChildList(mSheets[0], mFirstChild);
  }
  else {
    mSheets.RemoveElement(aSheet);
  }
}

static void
AddNamespaceRuleToMap(css::Rule* aRule, nsXMLNameSpaceMap* aMap)
{
  NS_ASSERTION(aRule->GetType() == css::Rule::NAMESPACE_RULE, "Bogus rule type");

  nsRefPtr<css::NameSpaceRule> nameSpaceRule = do_QueryObject(aRule);

  nsAutoString  urlSpec;
  nameSpaceRule->GetURLSpec(urlSpec);

  aMap->AddPrefix(nameSpaceRule->GetPrefix(), urlSpec);
}

static bool
CreateNameSpace(css::Rule* aRule, void* aNameSpacePtr)
{
  int32_t type = aRule->GetType();
  if (css::Rule::NAMESPACE_RULE == type) {
    AddNamespaceRuleToMap(aRule,
                          static_cast<nsXMLNameSpaceMap*>(aNameSpacePtr));
    return true;
  }
  // stop if not namespace, import or charset because namespace can't follow
  // anything else
  return (css::Rule::CHARSET_RULE == type || css::Rule::IMPORT_RULE == type);
}

void 
nsCSSStyleSheetInner::RebuildNameSpaces()
{
  // Just nuke our existing namespace map, if any
  if (NS_SUCCEEDED(CreateNamespaceMap())) {
    mOrderedRules.EnumerateForwards(CreateNameSpace, mNameSpaceMap);
  }
}

nsresult
nsCSSStyleSheetInner::CreateNamespaceMap()
{
  mNameSpaceMap = nsXMLNameSpaceMap::Create(false);
  NS_ENSURE_TRUE(mNameSpaceMap, NS_ERROR_OUT_OF_MEMORY);
  // Override the default namespace map behavior for the null prefix to
  // return the wildcard namespace instead of the null namespace.
  mNameSpaceMap->AddPrefix(nullptr, kNameSpaceID_Unknown);
  return NS_OK;
}

size_t
nsCSSStyleSheetInner::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf) const
{
  size_t n = aMallocSizeOf(this);
  n += mOrderedRules.SizeOfExcludingThis(css::Rule::SizeOfCOMArrayElementIncludingThis,
                                         aMallocSizeOf);
  n += mFirstChild ? mFirstChild->SizeOfIncludingThis(aMallocSizeOf) : 0;

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - mSheetURI
  // - mOriginalSheetURI
  // - mBaseURI
  // - mPrincipal
  // - mNameSpaceMap
  //
  // The following members are not measured:
  // - mSheets, because it's non-owning

  return n;
}

// -------------------------------
// CSS Style Sheet
//

nsCSSStyleSheet::nsCSSStyleSheet(CORSMode aCORSMode)
  : mTitle(), 
    mParent(nullptr),
    mOwnerRule(nullptr),
    mDocument(nullptr),
    mOwningNode(nullptr),
    mDisabled(false),
    mDirty(false),
    mRuleProcessors(nullptr)
{

  mInner = new nsCSSStyleSheetInner(this, aCORSMode);
}

nsCSSStyleSheet::nsCSSStyleSheet(const nsCSSStyleSheet& aCopy,
                                 nsCSSStyleSheet* aParentToUse,
                                 css::ImportRule* aOwnerRuleToUse,
                                 nsIDocument* aDocumentToUse,
                                 nsIDOMNode* aOwningNodeToUse)
  : mTitle(aCopy.mTitle),
    mParent(aParentToUse),
    mOwnerRule(aOwnerRuleToUse),
    mDocument(aDocumentToUse),
    mOwningNode(aOwningNodeToUse),
    mDisabled(aCopy.mDisabled),
    mDirty(aCopy.mDirty),
    mInner(aCopy.mInner),
    mRuleProcessors(nullptr)
{

  mInner->AddSheet(this);

  if (mDirty) { // CSSOM's been there, force full copy now
    NS_ASSERTION(mInner->mComplete, "Why have rules been accessed on an incomplete sheet?");
    // FIXME: handle failure?
    EnsureUniqueInner();
  }

  if (aCopy.mMedia) {
    // XXX This is wrong; we should be keeping @import rules and
    // sheets in sync!
    aCopy.mMedia->Clone(getter_AddRefs(mMedia));
  }
}

nsCSSStyleSheet::~nsCSSStyleSheet()
{
  for (nsCSSStyleSheet* child = mInner->mFirstChild;
       child;
       child = child->mNext) {
    // XXXbz this is a little bogus; see the XXX comment where we
    // declare mFirstChild.
    if (child->mParent == this) {
      child->mParent = nullptr;
      child->mDocument = nullptr;
    }
  }
  DropRuleCollection();
  DropMedia();
  mInner->RemoveSheet(this);
  // XXX The document reference is not reference counted and should
  // not be released. The document will let us know when it is going
  // away.
  if (mRuleProcessors) {
    NS_ASSERTION(mRuleProcessors->Length() == 0, "destructing sheet with rule processor reference");
    delete mRuleProcessors; // weak refs, should be empty here anyway
  }
}

void
nsCSSStyleSheet::DropRuleCollection()
{
  if (mRuleCollection) {
    mRuleCollection->DropReference();
    mRuleCollection = nullptr;
  }
}

void
nsCSSStyleSheet::DropMedia()
{
  if (mMedia) {
    mMedia->SetStyleSheet(nullptr);
    mMedia = nullptr;
  }
}

void
nsCSSStyleSheet::UnlinkInner()
{
  // We can only have a cycle through our inner if we have a unique inner,
  // because otherwise there are no JS wrappers for anything in the inner.
  if (mInner->mSheets.Length() != 1) {
    return;
  }

  mInner->mOrderedRules.EnumerateForwards(SetStyleSheetReference, nullptr);
  mInner->mOrderedRules.Clear();

  // Have to be a bit careful with child sheets, because we want to
  // drop their mNext pointers and null out their mParent and
  // mDocument, but don't want to work with deleted objects.  And we
  // don't want to do any addrefing in the process, just to make sure
  // we don't confuse the cycle collector (though on the face of it,
  // addref/release pairs during unlink should probably be ok).
  nsRefPtr<nsCSSStyleSheet> child;
  child.swap(mInner->mFirstChild);
  while (child) {
    MOZ_ASSERT(child->mParent == this, "We have a unique inner!");
    child->mParent = nullptr;
    child->mDocument = nullptr;
    nsRefPtr<nsCSSStyleSheet> next;
    // Null out child->mNext, but don't let it die yet
    next.swap(child->mNext);
    // Switch to looking at the old value of child->mNext next iteration
    child.swap(next);
    // "next" is now our previous value of child; it'll get released
    // as we loop around.
  }
}

void
nsCSSStyleSheet::TraverseInner(nsCycleCollectionTraversalCallback &cb)
{
  // We can only have a cycle through our inner if we have a unique inner,
  // because otherwise there are no JS wrappers for anything in the inner.
  if (mInner->mSheets.Length() != 1) {
    return;
  }

  nsRefPtr<nsCSSStyleSheet>* childSheetSlot = &mInner->mFirstChild;
  while (*childSheetSlot) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "child sheet");
    cb.NoteXPCOMChild(NS_ISUPPORTS_CAST(nsIStyleSheet*, childSheetSlot->get()));
    childSheetSlot = &(*childSheetSlot)->mNext;
  }

  const nsCOMArray<css::Rule>& rules = mInner->mOrderedRules;
  for (int32_t i = 0, count = rules.Count(); i < count; ++i) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mOrderedRules[i]");
    cb.NoteXPCOMChild(rules[i]->GetExistingDOMRule());
  }
}

DOMCI_DATA(CSSStyleSheet, nsCSSStyleSheet)

// QueryInterface implementation for nsCSSStyleSheet
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsCSSStyleSheet)
  NS_INTERFACE_MAP_ENTRY(nsIStyleSheet)
  NS_INTERFACE_MAP_ENTRY(nsIDOMStyleSheet)
  NS_INTERFACE_MAP_ENTRY(nsIDOMCSSStyleSheet)
  NS_INTERFACE_MAP_ENTRY(nsICSSLoaderObserver)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIStyleSheet)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(CSSStyleSheet)
  if (aIID.Equals(NS_GET_IID(nsCSSStyleSheet)))
    foundInterface = reinterpret_cast<nsISupports*>(this);
  else
NS_INTERFACE_MAP_END


NS_IMPL_CYCLE_COLLECTING_ADDREF(nsCSSStyleSheet)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsCSSStyleSheet)

NS_IMPL_CYCLE_COLLECTION_CLASS(nsCSSStyleSheet)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsCSSStyleSheet)
  tmp->DropMedia();
  // We do not unlink mNext; our parent will handle that.  If we
  // unlinked it here, our parent would not be able to walk its list
  // of child sheets and null out the back-references to it, if we got
  // unlinked before it does.
  tmp->DropRuleCollection();
  tmp->UnlinkInner();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsCSSStyleSheet)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMedia)
  // We do not traverse mNext; our parent will handle that.  See
  // comments in Unlink for why.
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRuleCollection)
  tmp->TraverseInner(cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END


nsresult
nsCSSStyleSheet::AddRuleProcessor(nsCSSRuleProcessor* aProcessor)
{
  if (! mRuleProcessors) {
    mRuleProcessors = new nsAutoTArray<nsCSSRuleProcessor*, 8>();
    if (!mRuleProcessors)
      return NS_ERROR_OUT_OF_MEMORY;
  }
  NS_ASSERTION(mRuleProcessors->NoIndex == mRuleProcessors->IndexOf(aProcessor),
               "processor already registered");
  mRuleProcessors->AppendElement(aProcessor); // weak ref
  return NS_OK;
}

nsresult
nsCSSStyleSheet::DropRuleProcessor(nsCSSRuleProcessor* aProcessor)
{
  if (!mRuleProcessors)
    return NS_ERROR_FAILURE;
  return mRuleProcessors->RemoveElement(aProcessor)
           ? NS_OK
           : NS_ERROR_FAILURE;
}


void
nsCSSStyleSheet::SetURIs(nsIURI* aSheetURI, nsIURI* aOriginalSheetURI,
                         nsIURI* aBaseURI)
{
  NS_PRECONDITION(aSheetURI && aBaseURI, "null ptr");

  NS_ASSERTION(mInner->mOrderedRules.Count() == 0 && !mInner->mComplete,
               "Can't call SetURL on sheets that are complete or have rules");

  mInner->mSheetURI = aSheetURI;
  mInner->mOriginalSheetURI = aOriginalSheetURI;
  mInner->mBaseURI = aBaseURI;
}

void
nsCSSStyleSheet::SetPrincipal(nsIPrincipal* aPrincipal)
{
  NS_PRECONDITION(!mInner->mPrincipalSet,
                  "Should have an inner whose principal has not yet been set");
  if (aPrincipal) {
    mInner->mPrincipal = aPrincipal;
#ifdef DEBUG
    mInner->mPrincipalSet = true;
#endif
  }
}

/* virtual */ nsIURI*
nsCSSStyleSheet::GetSheetURI() const
{
  return mInner->mSheetURI;
}

/* virtual */ nsIURI*
nsCSSStyleSheet::GetBaseURI() const
{
  return mInner->mBaseURI;
}

/* virtual */ void
nsCSSStyleSheet::GetType(nsString& aType) const
{
  aType.AssignLiteral("text/css");
}

bool
nsCSSStyleSheet::UseForPresentation(nsPresContext* aPresContext,
                                    nsMediaQueryResultCacheKey& aKey) const
{
  if (mMedia) {
    return mMedia->Matches(aPresContext, &aKey);
  }
  return true;
}


void
nsCSSStyleSheet::SetMedia(nsMediaList* aMedia)
{
  mMedia = aMedia;
}

/* virtual */ bool
nsCSSStyleSheet::HasRules() const
{
  return StyleRuleCount() != 0;
}

/* virtual */ bool
nsCSSStyleSheet::IsApplicable() const
{
  return !mDisabled && mInner->mComplete;
}

/* virtual */ void
nsCSSStyleSheet::SetEnabled(bool aEnabled)
{
  // Internal method, so callers must handle BeginUpdate/EndUpdate
  bool oldDisabled = mDisabled;
  mDisabled = !aEnabled;

  if (mInner->mComplete && oldDisabled != mDisabled) {
    ClearRuleCascades();

    if (mDocument) {
      mDocument->SetStyleSheetApplicableState(this, !mDisabled);
    }
  }
}

/* virtual */ bool
nsCSSStyleSheet::IsComplete() const
{
  return mInner->mComplete;
}

/* virtual */ void
nsCSSStyleSheet::SetComplete()
{
  NS_ASSERTION(!mDirty, "Can't set a dirty sheet complete!");
  mInner->mComplete = true;
  if (mDocument && !mDisabled) {
    // Let the document know
    mDocument->BeginUpdate(UPDATE_STYLE);
    mDocument->SetStyleSheetApplicableState(this, true);
    mDocument->EndUpdate(UPDATE_STYLE);
  }
}

/* virtual */ nsIStyleSheet*
nsCSSStyleSheet::GetParentSheet() const
{
  return mParent;
}

/* virtual */ nsIDocument*
nsCSSStyleSheet::GetOwningDocument() const
{
  return mDocument;
}

/* virtual */ void
nsCSSStyleSheet::SetOwningDocument(nsIDocument* aDocument)
{ // not ref counted
  mDocument = aDocument;
  // Now set the same document on all our child sheets....
  // XXXbz this is a little bogus; see the XXX comment where we
  // declare mFirstChild.
  for (nsCSSStyleSheet* child = mInner->mFirstChild;
       child; child = child->mNext) {
    if (child->mParent == this) {
      child->SetOwningDocument(aDocument);
    }
  }
}

uint64_t
nsCSSStyleSheet::FindOwningWindowInnerID() const
{
  uint64_t windowID = 0;
  if (mDocument) {
    windowID = mDocument->InnerWindowID();
  }

  if (windowID == 0 && mOwningNode) {
    nsCOMPtr<nsINode> node = do_QueryInterface(mOwningNode);
    windowID = node->OwnerDoc()->InnerWindowID();
  }

  if (windowID == 0 && mOwnerRule) {
    nsCOMPtr<nsIStyleSheet> sheet = static_cast<css::Rule*>(mOwnerRule)->GetStyleSheet();
    if (sheet) {
      nsRefPtr<nsCSSStyleSheet> cssSheet = do_QueryObject(sheet);
      if (cssSheet) {
        windowID = cssSheet->FindOwningWindowInnerID();
      }
    }
  }

  if (windowID == 0 && mParent) {
    windowID = mParent->FindOwningWindowInnerID();
  }

  return windowID;
}

void
nsCSSStyleSheet::AppendStyleSheet(nsCSSStyleSheet* aSheet)
{
  NS_PRECONDITION(nullptr != aSheet, "null arg");

  if (NS_SUCCEEDED(WillDirty())) {
    nsRefPtr<nsCSSStyleSheet>* tail = &mInner->mFirstChild;
    while (*tail) {
      tail = &(*tail)->mNext;
    }
    *tail = aSheet;
  
    // This is not reference counted. Our parent tells us when
    // it's going away.
    aSheet->mParent = this;
    aSheet->mDocument = mDocument;
    DidDirty();
  }
}

void
nsCSSStyleSheet::InsertStyleSheetAt(nsCSSStyleSheet* aSheet, int32_t aIndex)
{
  NS_PRECONDITION(nullptr != aSheet, "null arg");

  if (NS_SUCCEEDED(WillDirty())) {
    nsRefPtr<nsCSSStyleSheet>* tail = &mInner->mFirstChild;
    while (*tail && aIndex) {
      --aIndex;
      tail = &(*tail)->mNext;
    }
    aSheet->mNext = *tail;
    *tail = aSheet;

    // This is not reference counted. Our parent tells us when
    // it's going away.
    aSheet->mParent = this;
    aSheet->mDocument = mDocument;
    DidDirty();
  }
}

void
nsCSSStyleSheet::PrependStyleRule(css::Rule* aRule)
{
  NS_PRECONDITION(nullptr != aRule, "null arg");

  if (NS_SUCCEEDED(WillDirty())) {
    mInner->mOrderedRules.InsertObjectAt(aRule, 0);
    aRule->SetStyleSheet(this);
    DidDirty();

    if (css::Rule::NAMESPACE_RULE == aRule->GetType()) {
      // no api to prepend a namespace (ugh), release old ones and re-create them all
      mInner->RebuildNameSpaces();
    }
  }
}

void
nsCSSStyleSheet::AppendStyleRule(css::Rule* aRule)
{
  NS_PRECONDITION(nullptr != aRule, "null arg");

  if (NS_SUCCEEDED(WillDirty())) {
    mInner->mOrderedRules.AppendObject(aRule);
    aRule->SetStyleSheet(this);
    DidDirty();

    if (css::Rule::NAMESPACE_RULE == aRule->GetType()) {
#ifdef DEBUG
      nsresult rv =
#endif
        RegisterNamespaceRule(aRule);
      NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
                       "RegisterNamespaceRule returned error");
    }
  }
}

void
nsCSSStyleSheet::ReplaceStyleRule(css::Rule* aOld, css::Rule* aNew)
{
  NS_PRECONDITION(mInner->mOrderedRules.Count() != 0, "can't have old rule");
  NS_PRECONDITION(mInner->mComplete, "No replacing in an incomplete sheet!");

  if (NS_SUCCEEDED(WillDirty())) {
    int32_t index = mInner->mOrderedRules.IndexOf(aOld);
    if (MOZ_UNLIKELY(index == -1)) {
      NS_NOTREACHED("Couldn't find old rule");
      return;
    }
    mInner->mOrderedRules.ReplaceObjectAt(aNew, index);

    aNew->SetStyleSheet(this);
    aOld->SetStyleSheet(nullptr);
    DidDirty();
    NS_ASSERTION(css::Rule::NAMESPACE_RULE != aNew->GetType(), "not yet implemented");
    NS_ASSERTION(css::Rule::NAMESPACE_RULE != aOld->GetType(), "not yet implemented");
  }
}

int32_t
nsCSSStyleSheet::StyleRuleCount() const
{
  return mInner->mOrderedRules.Count();
}

nsresult
nsCSSStyleSheet::GetStyleRuleAt(int32_t aIndex, css::Rule*& aRule) const
{
  // Important: If this function is ever made scriptable, we must add
  // a security check here. See GetCssRules below for an example.
  aRule = mInner->mOrderedRules.SafeObjectAt(aIndex);
  if (aRule) {
    NS_ADDREF(aRule);
    return NS_OK;
  }

  return NS_ERROR_ILLEGAL_VALUE;
}

int32_t
nsCSSStyleSheet::StyleSheetCount() const
{
  // XXX Far from an ideal way to do this, but the hope is that
  // it won't be done too often. If it is, we might want to 
  // consider storing the children in an array.
  int32_t count = 0;

  const nsCSSStyleSheet* child = mInner->mFirstChild;
  while (child) {
    count++;
    child = child->mNext;
  }

  return count;
}

nsCSSStyleSheet::EnsureUniqueInnerResult
nsCSSStyleSheet::EnsureUniqueInner()
{
  mDirty = true;

  NS_ABORT_IF_FALSE(mInner->mSheets.Length() != 0,
                    "unexpected number of outers");
  if (mInner->mSheets.Length() == 1) {
    return eUniqueInner_AlreadyUnique;
  }
  nsCSSStyleSheetInner* clone = mInner->CloneFor(this);
  if (!clone) {
    return eUniqueInner_CloneFailed;
  }
  mInner->RemoveSheet(this);
  mInner = clone;

  // otherwise the rule processor has pointers to the old rules
  ClearRuleCascades();

  return eUniqueInner_ClonedInner;
}

bool
nsCSSStyleSheet::AppendAllChildSheets(nsTArray<nsCSSStyleSheet*>& aArray)
{
  for (nsCSSStyleSheet* child = mInner->mFirstChild; child;
       child = child->mNext) {
    if (!aArray.AppendElement(child)) {
      return false;
    }
  }
  return true;
}

already_AddRefed<nsCSSStyleSheet>
nsCSSStyleSheet::Clone(nsCSSStyleSheet* aCloneParent,
                       css::ImportRule* aCloneOwnerRule,
                       nsIDocument* aCloneDocument,
                       nsIDOMNode* aCloneOwningNode) const
{
  nsCSSStyleSheet* clone = new nsCSSStyleSheet(*this,
                                               aCloneParent,
                                               aCloneOwnerRule,
                                               aCloneDocument,
                                               aCloneOwningNode);
  NS_IF_ADDREF(clone);
  return clone;
}

#ifdef DEBUG
static void
ListRules(const nsCOMArray<css::Rule>& aRules, FILE* aOut, int32_t aIndent)
{
  for (int32_t index = aRules.Count() - 1; index >= 0; --index) {
    aRules.ObjectAt(index)->List(aOut, aIndent);
  }
}

struct ListEnumData {
  ListEnumData(FILE* aOut, int32_t aIndent)
    : mOut(aOut),
      mIndent(aIndent)
  {
  }
  FILE*   mOut;
  int32_t mIndent;
};

/* virtual */ void
nsCSSStyleSheet::List(FILE* out, int32_t aIndent) const
{

  int32_t index;

  // Indent
  for (index = aIndent; --index >= 0; ) fputs("  ", out);

  fputs("CSS Style Sheet: ", out);
  nsAutoCString urlSpec;
  nsresult rv = mInner->mSheetURI->GetSpec(urlSpec);
  if (NS_SUCCEEDED(rv) && !urlSpec.IsEmpty()) {
    fputs(urlSpec.get(), out);
  }

  if (mMedia) {
    fputs(" media: ", out);
    nsAutoString  buffer;
    mMedia->GetText(buffer);
    fputs(NS_ConvertUTF16toUTF8(buffer).get(), out);
  }
  fputs("\n", out);

  for (const nsCSSStyleSheet*  child = mInner->mFirstChild;
       child;
       child = child->mNext) {
    child->List(out, aIndent + 1);
  }

  fputs("Rules in source order:\n", out);
  ListRules(mInner->mOrderedRules, out, aIndent);
}
#endif

void 
nsCSSStyleSheet::ClearRuleCascades()
{
  if (mRuleProcessors) {
    nsCSSRuleProcessor **iter = mRuleProcessors->Elements(),
                       **end = iter + mRuleProcessors->Length();
    for(; iter != end; ++iter) {
      (*iter)->ClearRuleCascades();
    }
  }
  if (mParent) {
    nsCSSStyleSheet* parent = (nsCSSStyleSheet*)mParent;
    parent->ClearRuleCascades();
  }
}

nsresult
nsCSSStyleSheet::WillDirty()
{
  if (!mInner->mComplete) {
    // Do nothing
    return NS_OK;
  }

  if (EnsureUniqueInner() == eUniqueInner_CloneFailed) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return NS_OK;
}

void
nsCSSStyleSheet::DidDirty()
{
  NS_ABORT_IF_FALSE(!mInner->mComplete || mDirty,
                    "caller must have called WillDirty()");
  ClearRuleCascades();
}

nsresult
nsCSSStyleSheet::SubjectSubsumesInnerPrincipal()
{
  // Get the security manager and do the subsumes check
  nsIScriptSecurityManager *securityManager =
    nsContentUtils::GetSecurityManager();

  nsCOMPtr<nsIPrincipal> subjectPrincipal;
  nsresult rv = securityManager->GetSubjectPrincipal(getter_AddRefs(subjectPrincipal));
  NS_ENSURE_SUCCESS(rv, rv);

  if (!subjectPrincipal) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  bool subsumes;
  rv = subjectPrincipal->Subsumes(mInner->mPrincipal, &subsumes);
  NS_ENSURE_SUCCESS(rv, rv);

  if (subsumes) {
    return NS_OK;
  }
  
  if (!nsContentUtils::IsCallerChrome()) {
    // Allow access only if CORS mode is not NONE
    if (GetCORSMode() == CORS_NONE) {
      return NS_ERROR_DOM_SECURITY_ERR;
    }

    // Now make sure we set the principal of our inner to the
    // subjectPrincipal.  That means we need a unique inner, of
    // course.  But we don't want to do that if we're not complete
    // yet.  Luckily, all the callers of this method throw anyway if
    // not complete, so we can just do that here too.
    if (!mInner->mComplete) {
      return NS_ERROR_DOM_INVALID_ACCESS_ERR;
    }

    rv = WillDirty();
    NS_ENSURE_SUCCESS(rv, rv);

    mInner->mPrincipal = subjectPrincipal;

    DidDirty();
  }

  return NS_OK;
}

nsresult
nsCSSStyleSheet::RegisterNamespaceRule(css::Rule* aRule)
{
  if (!mInner->mNameSpaceMap) {
    nsresult rv = mInner->CreateNamespaceMap();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  AddNamespaceRuleToMap(aRule, mInner->mNameSpaceMap);
  return NS_OK;
}

  // nsIDOMStyleSheet interface
NS_IMETHODIMP    
nsCSSStyleSheet::GetType(nsAString& aType)
{
  aType.AssignLiteral("text/css");
  return NS_OK;
}

NS_IMETHODIMP    
nsCSSStyleSheet::GetDisabled(bool* aDisabled)
{
  *aDisabled = mDisabled;
  return NS_OK;
}

NS_IMETHODIMP    
nsCSSStyleSheet::SetDisabled(bool aDisabled)
{
  // DOM method, so handle BeginUpdate/EndUpdate
  MOZ_AUTO_DOC_UPDATE(mDocument, UPDATE_STYLE, true);
  nsCSSStyleSheet::SetEnabled(!aDisabled);
  return NS_OK;
}

NS_IMETHODIMP
nsCSSStyleSheet::GetOwnerNode(nsIDOMNode** aOwnerNode)
{
  *aOwnerNode = mOwningNode;
  NS_IF_ADDREF(*aOwnerNode);
  return NS_OK;
}

NS_IMETHODIMP
nsCSSStyleSheet::GetParentStyleSheet(nsIDOMStyleSheet** aParentStyleSheet)
{
  NS_ENSURE_ARG_POINTER(aParentStyleSheet);

  NS_IF_ADDREF(*aParentStyleSheet = mParent);

  return NS_OK;
}

NS_IMETHODIMP
nsCSSStyleSheet::GetHref(nsAString& aHref)
{
  if (mInner->mOriginalSheetURI) {
    nsAutoCString str;
    mInner->mOriginalSheetURI->GetSpec(str);
    CopyUTF8toUTF16(str, aHref);
  } else {
    SetDOMStringToNull(aHref);
  }

  return NS_OK;
}

/* virtual */ void
nsCSSStyleSheet::GetTitle(nsString& aTitle) const
{
  aTitle = mTitle;
}

NS_IMETHODIMP
nsCSSStyleSheet::GetTitle(nsAString& aTitle)
{
  aTitle.Assign(mTitle);
  return NS_OK;
}

NS_IMETHODIMP
nsCSSStyleSheet::GetMedia(nsIDOMMediaList** aMedia)
{
  NS_ENSURE_ARG_POINTER(aMedia);
  *aMedia = nullptr;

  if (!mMedia) {
    mMedia = new nsMediaList();
    NS_ENSURE_TRUE(mMedia, NS_ERROR_OUT_OF_MEMORY);
    mMedia->SetStyleSheet(this);
  }

  *aMedia = mMedia;
  NS_ADDREF(*aMedia);

  return NS_OK;
}

NS_IMETHODIMP    
nsCSSStyleSheet::GetOwnerRule(nsIDOMCSSRule** aOwnerRule)
{
  if (mOwnerRule) {
    NS_IF_ADDREF(*aOwnerRule = mOwnerRule->GetDOMRule());
  } else {
    *aOwnerRule = nullptr;
  }
  return NS_OK;
}

NS_IMETHODIMP    
nsCSSStyleSheet::GetCssRules(nsIDOMCSSRuleList** aCssRules)
{
  // No doing this on incomplete sheets!
  if (!mInner->mComplete) {
    return NS_ERROR_DOM_INVALID_ACCESS_ERR;
  }
  
  //-- Security check: Only scripts whose principal subsumes that of the
  //   style sheet can access rule collections.
  nsresult rv = SubjectSubsumesInnerPrincipal();
  NS_ENSURE_SUCCESS(rv, rv);

  // OK, security check passed, so get the rule collection
  if (nullptr == mRuleCollection) {
    mRuleCollection = new CSSRuleListImpl(this);
  }

  NS_ADDREF(*aCssRules = mRuleCollection);

  return NS_OK;
}

NS_IMETHODIMP    
nsCSSStyleSheet::InsertRule(const nsAString& aRule, 
                            uint32_t aIndex, 
                            uint32_t* aReturn)
{
  //-- Security check: Only scripts whose principal subsumes that of the
  //   style sheet can modify rule collections.
  nsresult rv = SubjectSubsumesInnerPrincipal();
  NS_ENSURE_SUCCESS(rv, rv);

  return InsertRuleInternal(aRule, aIndex, aReturn);
}

static bool
RuleHasPendingChildSheet(css::Rule *cssRule)
{
  nsCOMPtr<nsIDOMCSSImportRule> importRule(do_QueryInterface(cssRule));
  NS_ASSERTION(importRule, "Rule which has type IMPORT_RULE and does not implement nsIDOMCSSImportRule!");
  nsCOMPtr<nsIDOMCSSStyleSheet> childSheet;
  importRule->GetStyleSheet(getter_AddRefs(childSheet));
  nsRefPtr<nsCSSStyleSheet> cssSheet = do_QueryObject(childSheet);
  return cssSheet != nullptr && !cssSheet->IsComplete();
}

nsresult
nsCSSStyleSheet::InsertRuleInternal(const nsAString& aRule, 
                                    uint32_t aIndex, 
                                    uint32_t* aReturn)
{
  // No doing this if the sheet is not complete!
  if (!mInner->mComplete) {
    return NS_ERROR_DOM_INVALID_ACCESS_ERR;
  }

  if (aRule.IsEmpty()) {
    // Nothing to do here
    return NS_OK;
  }
  
  nsresult result;
  result = WillDirty();
  if (NS_FAILED(result))
    return result;
  
  if (aIndex > uint32_t(mInner->mOrderedRules.Count()))
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  
  NS_ASSERTION(uint32_t(mInner->mOrderedRules.Count()) <= INT32_MAX,
               "Too many style rules!");

  // Hold strong ref to the CSSLoader in case the document update
  // kills the document
  nsRefPtr<css::Loader> loader;
  if (mDocument) {
    loader = mDocument->CSSLoader();
    NS_ASSERTION(loader, "Document with no CSS loader!");
  }

  nsCSSParser css(loader, this);

  mozAutoDocUpdate updateBatch(mDocument, UPDATE_STYLE, true);

  nsCOMArray<css::Rule> rules;
  result = css.ParseRule(aRule, mInner->mSheetURI, mInner->mBaseURI,
                         mInner->mPrincipal, rules);
  if (NS_FAILED(result))
    return result;

  int32_t rulecount = rules.Count();
  if (rulecount == 0) {
    // Since we know aRule was not an empty string, just throw
    return NS_ERROR_DOM_SYNTAX_ERR;
  }
  
  // Hierarchy checking.  Just check the first and last rule in the list.
  
  // check that we're not inserting before a charset rule
  css::Rule* nextRule = mInner->mOrderedRules.SafeObjectAt(aIndex);
  if (nextRule) {
    int32_t nextType = nextRule->GetType();
    if (nextType == css::Rule::CHARSET_RULE) {
      return NS_ERROR_DOM_HIERARCHY_REQUEST_ERR;
    }

    // check last rule in list
    css::Rule* lastRule = rules.ObjectAt(rulecount - 1);
    int32_t lastType = lastRule->GetType();

    if (nextType == css::Rule::IMPORT_RULE &&
        lastType != css::Rule::CHARSET_RULE &&
        lastType != css::Rule::IMPORT_RULE) {
      return NS_ERROR_DOM_HIERARCHY_REQUEST_ERR;
    }
    
    if (nextType == css::Rule::NAMESPACE_RULE &&
        lastType != css::Rule::CHARSET_RULE &&
        lastType != css::Rule::IMPORT_RULE &&
        lastType != css::Rule::NAMESPACE_RULE) {
      return NS_ERROR_DOM_HIERARCHY_REQUEST_ERR;
    } 
  }
  
  // check first rule in list
  css::Rule* firstRule = rules.ObjectAt(0);
  int32_t firstType = firstRule->GetType();
  if (aIndex != 0) {
    if (firstType == css::Rule::CHARSET_RULE) { // no inserting charset at nonzero position
      return NS_ERROR_DOM_HIERARCHY_REQUEST_ERR;
    }
  
    css::Rule* prevRule = mInner->mOrderedRules.SafeObjectAt(aIndex - 1);
    int32_t prevType = prevRule->GetType();

    if (firstType == css::Rule::IMPORT_RULE &&
        prevType != css::Rule::CHARSET_RULE &&
        prevType != css::Rule::IMPORT_RULE) {
      return NS_ERROR_DOM_HIERARCHY_REQUEST_ERR;
    }

    if (firstType == css::Rule::NAMESPACE_RULE &&
        prevType != css::Rule::CHARSET_RULE &&
        prevType != css::Rule::IMPORT_RULE &&
        prevType != css::Rule::NAMESPACE_RULE) {
      return NS_ERROR_DOM_HIERARCHY_REQUEST_ERR;
    }
  }
  
  bool insertResult = mInner->mOrderedRules.InsertObjectsAt(rules, aIndex);
  NS_ENSURE_TRUE(insertResult, NS_ERROR_OUT_OF_MEMORY);
  DidDirty();

  for (int32_t counter = 0; counter < rulecount; counter++) {
    css::Rule* cssRule = rules.ObjectAt(counter);
    cssRule->SetStyleSheet(this);

    int32_t type = cssRule->GetType();
    if (type == css::Rule::NAMESPACE_RULE) {
      // XXXbz does this screw up when inserting a namespace rule before
      // another namespace rule that binds the same prefix to a different
      // namespace?
      result = RegisterNamespaceRule(cssRule);
      NS_ENSURE_SUCCESS(result, result);
    }

    if (type == css::Rule::IMPORT_RULE && RuleHasPendingChildSheet(cssRule)) {
      // We don't notify immediately for @import rules, but rather when
      // the sheet the rule is importing is loaded (see StyleSheetLoaded)
      continue;
    }
    if (mDocument) {
      mDocument->StyleRuleAdded(this, cssRule);
    }
  }

  *aReturn = aIndex;
  return NS_OK;
}

NS_IMETHODIMP    
nsCSSStyleSheet::DeleteRule(uint32_t aIndex)
{
  nsresult result = NS_ERROR_DOM_INDEX_SIZE_ERR;
  // No doing this if the sheet is not complete!
  if (!mInner->mComplete) {
    return NS_ERROR_DOM_INVALID_ACCESS_ERR;
  }

  //-- Security check: Only scripts whose principal subsumes that of the
  //   style sheet can modify rule collections.
  nsresult rv = SubjectSubsumesInnerPrincipal();
  NS_ENSURE_SUCCESS(rv, rv);

  // XXX TBI: handle @rule types
  mozAutoDocUpdate updateBatch(mDocument, UPDATE_STYLE, true);
    
  result = WillDirty();

  if (NS_SUCCEEDED(result)) {
    if (aIndex >= uint32_t(mInner->mOrderedRules.Count()))
      return NS_ERROR_DOM_INDEX_SIZE_ERR;

    NS_ASSERTION(uint32_t(mInner->mOrderedRules.Count()) <= INT32_MAX,
                 "Too many style rules!");

    // Hold a strong ref to the rule so it doesn't die when we RemoveObjectAt
    nsRefPtr<css::Rule> rule = mInner->mOrderedRules.ObjectAt(aIndex);
    if (rule) {
      mInner->mOrderedRules.RemoveObjectAt(aIndex);
      rule->SetStyleSheet(nullptr);
      DidDirty();

      if (mDocument) {
        mDocument->StyleRuleRemoved(this, rule);
      }
    }
  }

  return result;
}

nsresult
nsCSSStyleSheet::DeleteRuleFromGroup(css::GroupRule* aGroup, uint32_t aIndex)
{
  NS_ENSURE_ARG_POINTER(aGroup);
  NS_ASSERTION(mInner->mComplete, "No deleting from an incomplete sheet!");
  nsresult result;
  nsRefPtr<css::Rule> rule = aGroup->GetStyleRuleAt(aIndex);
  NS_ENSURE_TRUE(rule, NS_ERROR_ILLEGAL_VALUE);

  // check that the rule actually belongs to this sheet!
  if (this != rule->GetStyleSheet()) {
    return NS_ERROR_INVALID_ARG;
  }

  mozAutoDocUpdate updateBatch(mDocument, UPDATE_STYLE, true);
  
  result = WillDirty();
  NS_ENSURE_SUCCESS(result, result);

  result = aGroup->DeleteStyleRuleAt(aIndex);
  NS_ENSURE_SUCCESS(result, result);
  
  rule->SetStyleSheet(nullptr);
  
  DidDirty();

  if (mDocument) {
    mDocument->StyleRuleRemoved(this, rule);
  }

  return NS_OK;
}

nsresult
nsCSSStyleSheet::InsertRuleIntoGroup(const nsAString & aRule,
                                     css::GroupRule* aGroup,
                                     uint32_t aIndex,
                                     uint32_t* _retval)
{
  nsresult result;
  NS_ASSERTION(mInner->mComplete, "No inserting into an incomplete sheet!");
  // check that the group actually belongs to this sheet!
  if (this != aGroup->GetStyleSheet()) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aRule.IsEmpty()) {
    // Nothing to do here
    return NS_OK;
  }

  // Hold strong ref to the CSSLoader in case the document update
  // kills the document
  nsRefPtr<css::Loader> loader;
  if (mDocument) {
    loader = mDocument->CSSLoader();
    NS_ASSERTION(loader, "Document with no CSS loader!");
  }

  nsCSSParser css(loader, this);

  // parse and grab the rule
  mozAutoDocUpdate updateBatch(mDocument, UPDATE_STYLE, true);

  result = WillDirty();
  NS_ENSURE_SUCCESS(result, result);

  nsCOMArray<css::Rule> rules;
  result = css.ParseRule(aRule, mInner->mSheetURI, mInner->mBaseURI,
                         mInner->mPrincipal, rules);
  NS_ENSURE_SUCCESS(result, result);

  int32_t rulecount = rules.Count();
  if (rulecount == 0) {
    // Since we know aRule was not an empty string, just throw
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  int32_t counter;
  css::Rule* rule;
  for (counter = 0; counter < rulecount; counter++) {
    rule = rules.ObjectAt(counter);
    switch (rule->GetType()) {
      case css::Rule::STYLE_RULE:
      case css::Rule::MEDIA_RULE:
      case css::Rule::FONT_FACE_RULE:
      case css::Rule::PAGE_RULE:
      case css::Rule::KEYFRAMES_RULE:
      case css::Rule::DOCUMENT_RULE:
      case css::Rule::SUPPORTS_RULE:
        // these types are OK to insert into a group
        break;
      case css::Rule::CHARSET_RULE:
      case css::Rule::IMPORT_RULE:
      case css::Rule::NAMESPACE_RULE:
        // these aren't
        return NS_ERROR_DOM_HIERARCHY_REQUEST_ERR;
      default:
        NS_NOTREACHED("unexpected rule type");
        return NS_ERROR_DOM_HIERARCHY_REQUEST_ERR;
    }
  }
  
  result = aGroup->InsertStyleRulesAt(aIndex, rules);
  NS_ENSURE_SUCCESS(result, result);
  DidDirty();
  for (counter = 0; counter < rulecount; counter++) {
    rule = rules.ObjectAt(counter);
  
    if (mDocument) {
      mDocument->StyleRuleAdded(this, rule);
    }
  }

  *_retval = aIndex;
  return NS_OK;
}

nsresult
nsCSSStyleSheet::ReplaceRuleInGroup(css::GroupRule* aGroup,
                                    css::Rule* aOld, css::Rule* aNew)
{
  nsresult result;
  NS_PRECONDITION(mInner->mComplete, "No replacing in an incomplete sheet!");
  NS_ASSERTION(this == aGroup->GetStyleSheet(), "group doesn't belong to this sheet");
  result = WillDirty();
  NS_ENSURE_SUCCESS(result, result);

  result = aGroup->ReplaceStyleRule(aOld, aNew);
  DidDirty();
  return result;
}

// nsICSSLoaderObserver implementation
NS_IMETHODIMP
nsCSSStyleSheet::StyleSheetLoaded(nsCSSStyleSheet* aSheet,
                                  bool aWasAlternate,
                                  nsresult aStatus)
{
  if (aSheet->GetParentSheet() == nullptr) {
    return NS_OK; // ignore if sheet has been detached already (see parseSheet)
  }
  NS_ASSERTION(this == aSheet->GetParentSheet(),
               "We are being notified of a sheet load for a sheet that is not our child!");

  if (mDocument && NS_SUCCEEDED(aStatus)) {
    mozAutoDocUpdate updateBatch(mDocument, UPDATE_STYLE, true);

    // XXXldb @import rules shouldn't even implement nsIStyleRule (but
    // they do)!
    mDocument->StyleRuleAdded(this, aSheet->GetOwnerRule());
  }

  return NS_OK;
}

nsresult
nsCSSStyleSheet::ParseSheet(const nsAString& aInput)
{
  // Not doing this if the sheet is not complete!
  if (!mInner->mComplete) {
    return NS_ERROR_DOM_INVALID_ACCESS_ERR;
  }

  // Hold strong ref to the CSSLoader in case the document update
  // kills the document
  nsRefPtr<css::Loader> loader;
  if (mDocument) {
    loader = mDocument->CSSLoader();
    NS_ASSERTION(loader, "Document with no CSS loader!");
  } else {
    loader = new css::Loader();
  }

  mozAutoDocUpdate updateBatch(mDocument, UPDATE_STYLE, true);

  nsresult rv = WillDirty();
  NS_ENSURE_SUCCESS(rv, rv);

  // detach existing rules (including child sheets via import rules)
  int ruleCount;
  while ((ruleCount = mInner->mOrderedRules.Count()) != 0) {
    nsRefPtr<css::Rule> rule = mInner->mOrderedRules.ObjectAt(ruleCount - 1);
    mInner->mOrderedRules.RemoveObjectAt(ruleCount - 1);
    rule->SetStyleSheet(nullptr);
    if (mDocument) {
      mDocument->StyleRuleRemoved(this, rule);
    }
  }

  // nuke child sheets list and current namespace map
  for (nsCSSStyleSheet* child = mInner->mFirstChild; child; child = child->mNext) {
    NS_ASSERTION(child->mParent == this, "Child sheet is not parented to this!");
    child->mParent = nullptr;
    child->mDocument = nullptr;
  }
  mInner->mFirstChild = nullptr;
  mInner->mNameSpaceMap = nullptr;

  // allow unsafe rules if the style sheet's principal is the system principal
  bool allowUnsafeRules = nsContentUtils::IsSystemPrincipal(mInner->mPrincipal);

  nsCSSParser parser(loader, this);
  rv = parser.ParseSheet(aInput, mInner->mSheetURI, mInner->mBaseURI,
                         mInner->mPrincipal, 1, allowUnsafeRules);
  DidDirty(); // we are always 'dirty' here since we always remove rules first
  NS_ENSURE_SUCCESS(rv, rv);

  // notify document of all new rules
  if (mDocument) {
    for (int32_t index = 0; index < mInner->mOrderedRules.Count(); ++index) {
      nsRefPtr<css::Rule> rule = mInner->mOrderedRules.ObjectAt(index);
      if (rule->GetType() == css::Rule::IMPORT_RULE &&
          RuleHasPendingChildSheet(rule)) {
        continue; // notify when loaded (see StyleSheetLoaded)
      }
      mDocument->StyleRuleAdded(this, rule);
    }
  }
  return NS_OK;
}

/* virtual */ nsIURI*
nsCSSStyleSheet::GetOriginalURI() const
{
  return mInner->mOriginalSheetURI;
}
