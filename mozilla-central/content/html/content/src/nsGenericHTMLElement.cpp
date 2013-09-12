/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set tw=80 expandtab softtabstop=2 ts=2 sw=2: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"
#include "mozilla/Likely.h"

#include "nscore.h"
#include "nsGenericHTMLElement.h"
#include "nsAttrValueInlines.h"
#include "nsCOMPtr.h"
#include "nsIAtom.h"
#include "nsIContentViewer.h"
#include "mozilla/css/StyleRule.h"
#include "nsIDocument.h"
#include "nsIDocumentEncoder.h"
#include "nsIDOMHTMLBodyElement.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMAttr.h"
#include "nsIDOMDocumentFragment.h"
#include "nsIDOMHTMLElement.h"
#include "nsIDOMHTMLMenuElement.h"
#include "nsIDOMElementCSSInlineStyle.h"
#include "nsIDOMWindow.h"
#include "nsIDOMDocument.h"
#include "nsEventListenerManager.h"
#include "nsMappedAttributes.h"
#include "nsHTMLStyleSheet.h"
#include "nsIHTMLDocument.h"
#include "nsILink.h"
#include "nsPIDOMWindow.h"
#include "nsIStyleRule.h"
#include "nsIURL.h"
#include "nsNetUtil.h"
#include "nsEscape.h"
#include "nsIFrame.h"
#include "nsIScrollableFrame.h"
#include "nsIView.h"
#include "nsIViewManager.h"
#include "nsIWidget.h"
#include "nsRange.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsINameSpaceManager.h"
#include "nsError.h"
#include "nsScriptLoader.h"
#include "nsRuleData.h"

#include "nsPresState.h"
#include "nsILayoutHistoryState.h"

#include "nsHTMLParts.h"
#include "nsContentUtils.h"
#include "mozilla/dom/DirectionalityUtils.h"
#include "nsString.h"
#include "nsUnicharUtils.h"
#include "nsGkAtoms.h"
#include "nsEventStateManager.h"
#include "nsIDOMEvent.h"
#include "nsDOMCSSDeclaration.h"
#include "nsITextControlFrame.h"
#include "nsIForm.h"
#include "nsIFormControl.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsHTMLFormElement.h"
#include "nsFocusManager.h"
#include "nsAttrValueOrString.h"

#include "nsMutationEvent.h"

#include "nsContentCID.h"

#include "nsDOMStringMap.h"

#include "nsIEditor.h"
#include "nsIEditorIMESupport.h"
#include "nsEventDispatcher.h"
#include "nsLayoutUtils.h"
#include "mozAutoDocUpdate.h"
#include "nsHtml5Module.h"
#include "nsITextControlElement.h"
#include "mozilla/dom/Element.h"
#include "nsHTMLFieldSetElement.h"
#include "nsHTMLMenuElement.h"
#include "nsAsyncDOMEvent.h"
#include "nsDOMMutationObserver.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/FromParser.h"
#include "mozilla/BloomFilter.h"

#include "HTMLPropertiesCollection.h"
#include "nsVariant.h"
#include "nsDOMSettableTokenList.h"
#include "nsThreadUtils.h"
#include "nsTextFragment.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/ErrorResult.h"
#include "nsHTMLDocument.h"
#include "nsDOMTouchEvent.h"
#include "nsGlobalWindow.h"

using namespace mozilla;
using namespace mozilla::dom;

class nsINodeInfo;
class nsIDOMNodeList;
class nsRuleWalker;

// XXX todo: add in missing out-of-memory checks

//----------------------------------------------------------------------

#ifdef GATHER_ELEMENT_USEAGE_STATISTICS

// static objects that have constructors are kinda bad, but we don't
// care here, this is only debugging code!

static nsHashtable sGEUS_ElementCounts;

void GEUS_ElementCreated(nsINodeInfo *aNodeInfo)
{
  nsAutoString name;
  aNodeInfo->GetName(name);

  nsStringKey key(name);

  int32_t count = (int32_t)sGEUS_ElementCounts.Get(&key);

  count++;

  sGEUS_ElementCounts.Put(&key, (void *)count);
}

bool GEUS_enum_func(nsHashKey *aKey, void *aData, void *aClosure)
{
  const PRUnichar *name_chars = ((nsStringKey *)aKey)->GetString();
  NS_ConvertUTF16toUTF8 name(name_chars);

  printf ("%s %d\n", name.get(), aData);

  return true;
}

void GEUS_DumpElementCounts()
{
  printf ("Element count statistics:\n");

  sGEUS_ElementCounts.Enumerate(GEUS_enum_func, nullptr);

  printf ("End of element count statistics:\n");
}

nsresult
nsGenericHTMLElement::Init(nsINodeInfo *aNodeInfo)
{
  GEUS_ElementCreated(aNodeInfo);

  return nsGenericHTMLElementBase::Init(aNodeInfo);
}

#endif

/**
 * nsAutoFocusEvent is used to dispatch a focus event when a
 * nsGenericHTMLFormElement is binded to the tree with the autofocus attribute
 * enabled.
 */
class nsAutoFocusEvent : public nsRunnable
{
public:
  nsAutoFocusEvent(nsGenericHTMLFormElement* aElement) : mElement(aElement) {}

  NS_IMETHOD Run() {
    nsFocusManager* fm = nsFocusManager::GetFocusManager();
    if (!fm) {
      return NS_ERROR_NULL_POINTER;
    }

    nsIDocument* document = mElement->OwnerDoc();

    nsPIDOMWindow* window = document->GetWindow();
    if (!window) {
      return NS_OK;
    }

    // Trying to found the top window (equivalent to window.top).
    nsCOMPtr<nsIDOMWindow> top;
    window->GetTop(getter_AddRefs(top));
    if (top) {
      window = static_cast<nsPIDOMWindow*>(top.get());
    }

    if (window->GetFocusedNode()) {
      return NS_OK;
    }

    nsCOMPtr<nsIDocument> topDoc = window->GetExtantDoc();
    if (topDoc && topDoc->GetReadyStateEnum() == nsIDocument::READYSTATE_COMPLETE) {
      return NS_OK;
    }

    // If something is focused in the same document, ignore autofocus.
    if (!fm->GetFocusedContent() ||
        fm->GetFocusedContent()->OwnerDoc() != document) {
      mozilla::ErrorResult rv;
      mElement->Focus(rv);
      return rv.ErrorCode();
    }

    return NS_OK;
  }
private:
  // NOTE: nsGenericHTMLFormElement is saved as a nsGenericHTMLElement
  // because AddRef/Release are ambiguous with nsGenericHTMLFormElement
  // and Focus() is declared (and defined) in nsGenericHTMLElement class.
  nsRefPtr<nsGenericHTMLElement> mElement;
};

class nsGenericHTMLElementTearoff : public nsIDOMElementCSSInlineStyle
{
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  nsGenericHTMLElementTearoff(nsGenericHTMLElement *aElement)
    : mElement(aElement)
  {
  }

  virtual ~nsGenericHTMLElementTearoff()
  {
  }

  NS_IMETHOD GetStyle(nsIDOMCSSStyleDeclaration** aStyle)
  {
    mozilla::ErrorResult rv;
    NS_IF_ADDREF(*aStyle = mElement->GetStyle(rv));
    return rv.ErrorCode();
  }

  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsGenericHTMLElementTearoff,
                                           nsIDOMElementCSSInlineStyle)

private:
  nsRefPtr<nsGenericHTMLElement> mElement;
};

NS_IMPL_CYCLE_COLLECTION_1(nsGenericHTMLElementTearoff, mElement)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsGenericHTMLElementTearoff)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsGenericHTMLElementTearoff)

NS_INTERFACE_TABLE_HEAD(nsGenericHTMLElementTearoff)
  NS_INTERFACE_TABLE_INHERITED1(nsGenericHTMLElementTearoff,
                                nsIDOMElementCSSInlineStyle)
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(nsGenericHTMLElementTearoff)
NS_INTERFACE_MAP_END_AGGREGATED(mElement)

nsresult
nsGenericHTMLElement::DOMQueryInterface(nsIDOMHTMLElement *aElement,
                                        REFNSIID aIID, void **aInstancePtr)
{
  NS_PRECONDITION(aInstancePtr, "null out param");

  nsresult rv = NS_ERROR_FAILURE;

  NS_INTERFACE_TABLE_BEGIN
    NS_INTERFACE_TABLE_ENTRY(nsIDOMHTMLElement, nsIDOMNode)
    NS_INTERFACE_TABLE_ENTRY(nsIDOMHTMLElement, nsIDOMElement)
    NS_INTERFACE_TABLE_ENTRY(nsIDOMHTMLElement, nsIDOMHTMLElement)
  NS_INTERFACE_TABLE_END_WITH_PTR(aElement)

  NS_INTERFACE_TABLE_TO_MAP_SEGUE
  NS_INTERFACE_MAP_ENTRY_TEAROFF(nsIDOMElementCSSInlineStyle,
                                 new nsGenericHTMLElementTearoff(this))
  NS_INTERFACE_MAP_END

// No closing bracket, because NS_INTERFACE_MAP_END does that for us.

nsresult
nsGenericHTMLElement::CopyInnerTo(Element* aDst)
{
  nsresult rv;
  int32_t i, count = GetAttrCount();
  for (i = 0; i < count; ++i) {
    const nsAttrName *name = mAttrsAndChildren.AttrNameAt(i);
    const nsAttrValue *value = mAttrsAndChildren.AttrAt(i);

    nsAutoString valStr;
    value->ToString(valStr);

    if (name->Equals(nsGkAtoms::style, kNameSpaceID_None) &&
        value->Type() == nsAttrValue::eCSSStyleRule) {
      // We can't just set this as a string, because that will fail
      // to reparse the string into style data until the node is
      // inserted into the document.  Clone the Rule instead.
      nsRefPtr<mozilla::css::Rule> ruleClone = value->GetCSSStyleRuleValue()->Clone();
      nsRefPtr<mozilla::css::StyleRule> styleRule = do_QueryObject(ruleClone);
      NS_ENSURE_TRUE(styleRule, NS_ERROR_UNEXPECTED);

      rv = aDst->SetInlineStyleRule(styleRule, &valStr, false);
      NS_ENSURE_SUCCESS(rv, rv);

      continue;
    }

    rv = aDst->SetAttr(name->NamespaceID(), name->LocalName(),
                       name->GetPrefix(), valStr, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

already_AddRefed<nsDOMStringMap>
nsGenericHTMLElement::Dataset()
{
  nsDOMSlots *slots = DOMSlots();

  if (!slots->mDataset) {
    // mDataset is a weak reference so assignment will not AddRef.
    // AddRef is called before returning the pointer.
    slots->mDataset = new nsDOMStringMap(this);
  }

  NS_ADDREF(slots->mDataset);
  return slots->mDataset;
}

nsresult
nsGenericHTMLElement::GetDataset(nsISupports** aDataset)
{
  *aDataset = Dataset().get();
  return NS_OK;
}

nsresult
nsGenericHTMLElement::ClearDataset()
{
  nsDOMSlots *slots = GetExistingDOMSlots();

  NS_ASSERTION(slots && slots->mDataset,
               "Slots should exist and dataset should not be null.");
  slots->mDataset = nullptr;

  return NS_OK;
}

static const nsAttrValue::EnumTable kDirTable[] = {
  { "ltr", eDir_LTR },
  { "rtl", eDir_RTL },
  { "auto", eDir_Auto },
  { 0 }
};

void
nsGenericHTMLElement::GetAccessKeyLabel(nsAString& aLabel)
{
  nsPresContext *presContext = GetPresContext();

  if (presContext) {
    nsAutoString suffix;
    GetAccessKey(suffix);
    if (!suffix.IsEmpty() && 
        presContext->EventStateManager()->GetAccessKeyLabelPrefix(aLabel)) {
      aLabel.Append(suffix);
    }
  }
}

static bool IS_TABLE_CELL(nsIAtom* frameType) {
  return nsGkAtoms::tableCellFrame == frameType ||
    nsGkAtoms::bcTableCellFrame == frameType;
}

static bool
IsOffsetParent(nsIFrame* aFrame)
{
  nsIAtom* frameType = aFrame->GetType();
  
  if (IS_TABLE_CELL(frameType) || frameType == nsGkAtoms::tableFrame) {
    // Per the IDL for Element, only td, th, and table are acceptable offsetParents
    // apart from body or positioned elements; we need to check the content type as
    // well as the frame type so we ignore anonymous tables created by an element
    // with display: table-cell with no actual table
    nsIContent* content = aFrame->GetContent();

    return content->IsHTML(nsGkAtoms::table) || content->IsHTML(nsGkAtoms::td)
      || content->IsHTML(nsGkAtoms::th);
  }
  return false;
}

Element*
nsGenericHTMLElement::GetOffsetRect(nsRect& aRect)
{
  aRect = nsRect();

  nsIFrame* frame = GetStyledFrame();
  if (!frame) {
    return nullptr;
  }

  nsIFrame* parent = frame->GetParent();
  nsPoint origin(0, 0);

  if (parent && parent->GetType() == nsGkAtoms::tableOuterFrame &&
      frame->GetType() == nsGkAtoms::tableFrame) {
    origin = parent->GetPositionIgnoringScrolling();
    parent = parent->GetParent();
  }

  nsIContent* offsetParent = nullptr;
  Element* docElement = GetCurrentDoc()->GetRootElement();
  nsIContent* content = frame->GetContent();

  if (content && (content->IsHTML(nsGkAtoms::body) || content == docElement)) {
    parent = frame;
  }
  else {
    const bool isPositioned = frame->IsPositioned();
    const bool isAbsolutelyPositioned = frame->IsAbsolutelyPositioned();
    origin += frame->GetPositionIgnoringScrolling();

    for ( ; parent ; parent = parent->GetParent()) {
      content = parent->GetContent();

      // Stop at the first ancestor that is positioned.
      if (parent->IsPositioned()) {
        offsetParent = content;
        break;
      }

      // Add the parent's origin to our own to get to the
      // right coordinate system.
      const bool isOffsetParent = !isPositioned && IsOffsetParent(parent);
      if (!isAbsolutelyPositioned && !isOffsetParent) {
        origin += parent->GetPositionIgnoringScrolling();
      }

      if (content) {
        // If we've hit the document element, break here.
        if (content == docElement) {
          break;
        }

        // Break if the ancestor frame type makes it suitable as offset parent
        // and this element is *not* positioned or if we found the body element.
        if (isOffsetParent || content->IsHTML(nsGkAtoms::body)) {
          offsetParent = content;
          break;
        }
      }
    }

    if (isAbsolutelyPositioned && !offsetParent) {
      // If this element is absolutely positioned, but we don't have
      // an offset parent it means this element is an absolutely
      // positioned child that's not nested inside another positioned
      // element, in this case the element's frame's parent is the
      // frame for the HTML element so we fail to find the body in the
      // parent chain. We want the offset parent in this case to be
      // the body, so we just get the body element from the document.

      nsCOMPtr<nsIDOMHTMLDocument> html_doc(do_QueryInterface(GetCurrentDoc()));

      if (html_doc) {
        offsetParent = static_cast<nsHTMLDocument*>(html_doc.get())->GetBody();
      }
    }
  }

  // Subtract the parent border unless it uses border-box sizing.
  if (parent &&
      parent->GetStylePosition()->mBoxSizing != NS_STYLE_BOX_SIZING_BORDER) {
    const nsStyleBorder* border = parent->GetStyleBorder();
    origin.x -= border->GetComputedBorderWidth(NS_SIDE_LEFT);
    origin.y -= border->GetComputedBorderWidth(NS_SIDE_TOP);
  }

  // XXX We should really consider subtracting out padding for
  // content-box sizing, but we should see what IE does....

  // Convert to pixels.
  aRect.x = nsPresContext::AppUnitsToIntCSSPixels(origin.x);
  aRect.y = nsPresContext::AppUnitsToIntCSSPixels(origin.y);

  // Get the union of all rectangles in this and continuation frames.
  // It doesn't really matter what we use as aRelativeTo here, since
  // we only care about the size. We just have to use something non-null.
  nsRect rcFrame = nsLayoutUtils::GetAllInFlowRectsUnion(frame, frame);
  aRect.width = nsPresContext::AppUnitsToIntCSSPixels(rcFrame.width);
  aRect.height = nsPresContext::AppUnitsToIntCSSPixels(rcFrame.height);

  return offsetParent ? offsetParent->AsElement() : nullptr;
}

// Try to keep the size of StringBuilder close to a jemalloc bucket size.
#define STRING_BUFFER_UNITS 1020

class StringBuilder
{
private:
  class Unit
  {
  public:
    Unit() : mAtom(nullptr), mType(eUnknown), mLength(0)
    {
      MOZ_COUNT_CTOR(StringBuilder::Unit);
    }
    ~Unit()
    {
      if (mType == eString || mType == eStringWithEncode) {
        delete mString;
      }
      MOZ_COUNT_DTOR(StringBuilder::Unit);
    }

    enum Type
    {
      eUnknown,
      eAtom,
      eString,
      eStringWithEncode,
      eLiteral,
      eTextFragment,
      eTextFragmentWithEncode,
    };

    union
    {
      nsIAtom*              mAtom;
      const char*           mLiteral;
      nsAutoString*         mString;
      const nsTextFragment* mTextFragment;
    };
    Type     mType;
    uint32_t mLength;
  };
public:
  StringBuilder() : mLast(this), mLength(0)
  {
    MOZ_COUNT_CTOR(StringBuilder);
  }

  ~StringBuilder()
  {
    MOZ_COUNT_DTOR(StringBuilder);
  }

  void Append(nsIAtom* aAtom)
  {
    Unit* u = AddUnit();
    u->mAtom = aAtom;
    u->mType = Unit::eAtom;
    uint32_t len = aAtom->GetLength();
    u->mLength = len;
    mLength += len;
  }

  template<int N>
  void Append(const char (&aLiteral)[N])
  {
    Unit* u = AddUnit();
    u->mLiteral = aLiteral;
    u->mType = Unit::eLiteral;
    uint32_t len = N - 1;
    u->mLength = len;
    mLength += len;
  }

  template<int N>
  void Append(char (&aLiteral)[N])
  {
    Unit* u = AddUnit();
    u->mLiteral = aLiteral;
    u->mType = Unit::eLiteral;
    uint32_t len = N - 1;
    u->mLength = len;
    mLength += len;
  }

  void Append(const nsAString& aString)
  {
    Unit* u = AddUnit();
    u->mString = new nsAutoString(aString);
    u->mType = Unit::eString;
    uint32_t len = aString.Length();
    u->mLength = len;
    mLength += len;
  }

  void Append(nsAutoString* aString)
  {
    Unit* u = AddUnit();
    u->mString = aString;
    u->mType = Unit::eString;
    uint32_t len = aString->Length();
    u->mLength = len;
    mLength += len;
  }

  void AppendWithAttrEncode(nsAutoString* aString, uint32_t aLen)
  {
    Unit* u = AddUnit();
    u->mString = aString;
    u->mType = Unit::eStringWithEncode;
    u->mLength = aLen;
    mLength += aLen;
  }

  void Append(const nsTextFragment* aTextFragment)
  {
    Unit* u = AddUnit();
    u->mTextFragment = aTextFragment;
    u->mType = Unit::eTextFragment;
    uint32_t len = aTextFragment->GetLength();
    u->mLength = len;
    mLength += len;
  }

  void AppendWithEncode(const nsTextFragment* aTextFragment, uint32_t aLen)
  {
    Unit* u = AddUnit();
    u->mTextFragment = aTextFragment;
    u->mType = Unit::eTextFragmentWithEncode;
    u->mLength = aLen;
    mLength += aLen;
  }

  bool ToString(nsAString& aOut)
  {
    if (!aOut.SetCapacity(mLength, fallible_t())) {
      return false;
    }

    for (StringBuilder* current = this; current; current = current->mNext) {
      uint32_t len = current->mUnits.Length();
      for (uint32_t i = 0; i < len; ++i) {
        Unit& u = current->mUnits[i];
        switch (u.mType) {
          case Unit::eAtom:
            aOut.Append(nsDependentAtomString(u.mAtom));
            break;
          case Unit::eString:
            aOut.Append(*(u.mString));
            break;
          case Unit::eStringWithEncode:
            EncodeAttrString(*(u.mString), aOut);
            break;
          case Unit::eLiteral:
            aOut.AppendASCII(u.mLiteral, u.mLength);
            break;
          case Unit::eTextFragment:
            u.mTextFragment->AppendTo(aOut);
            break;
          case Unit::eTextFragmentWithEncode:
            EncodeTextFragment(u.mTextFragment, aOut);
            break;
          default:
            MOZ_NOT_REACHED("Unknown unit type?");
        }
      }
    }
    return true;
  }
private:
  Unit* AddUnit()
  {
    if (mLast->mUnits.Length() == STRING_BUFFER_UNITS) {
      new StringBuilder(this);
    }
    return mLast->mUnits.AppendElement();
  }

  StringBuilder(StringBuilder* aFirst)
  : mLast(nullptr), mLength(0)
  {
    MOZ_COUNT_CTOR(StringBuilder);
    aFirst->mLast->mNext = this;
    aFirst->mLast = this;
  }

  void EncodeAttrString(const nsAutoString& aValue, nsAString& aOut)
  {
    const PRUnichar* c = aValue.BeginReading();
    const PRUnichar* end = aValue.EndReading();
    while (c < end) {
      switch (*c) {
      case '"':
        aOut.AppendLiteral("&quot;");
        break;
      case '&':
        aOut.AppendLiteral("&amp;");
        break;
      case 0x00A0:
        aOut.AppendLiteral("&nbsp;");
        break;
      default:
        aOut.Append(*c);
        break;
      }
      ++c;
    }
  }

  void EncodeTextFragment(const nsTextFragment* aValue, nsAString& aOut)
  {
    uint32_t len = aValue->GetLength();
    if (aValue->Is2b()) {
      const PRUnichar* data = aValue->Get2b();
      for (uint32_t i = 0; i < len; ++i) {
        const PRUnichar c = data[i];
        switch (c) {
          case '<':
            aOut.AppendLiteral("&lt;");
            break;
          case '>':
            aOut.AppendLiteral("&gt;");
            break;
          case '&':
            aOut.AppendLiteral("&amp;");
            break;
          case 0x00A0:
            aOut.AppendLiteral("&nbsp;");
            break;
          default:
            aOut.Append(c);
            break;
        }
      }
    } else {
      const char* data = aValue->Get1b();
      for (uint32_t i = 0; i < len; ++i) {
        const unsigned char c = data[i];
        switch (c) {
          case '<':
            aOut.AppendLiteral("&lt;");
            break;
          case '>':
            aOut.AppendLiteral("&gt;");
            break;
          case '&':
            aOut.AppendLiteral("&amp;");
            break;
          case 0x00A0:
            aOut.AppendLiteral("&nbsp;");
            break;
          default:
            aOut.Append(c);
            break;
        }
      }
    }
  }

  nsAutoTArray<Unit, STRING_BUFFER_UNITS> mUnits;
  nsAutoPtr<StringBuilder>                mNext;
  StringBuilder*                          mLast;
  // mLength is used only in the first StringBuilder object in the linked list.
  uint32_t                                mLength;
};

static void
AppendEncodedCharacters(const nsTextFragment* aText, StringBuilder& aBuilder)
{
  uint32_t extraSpaceNeeded = 0;
  uint32_t len = aText->GetLength();
  if (aText->Is2b()) {
    const PRUnichar* data = aText->Get2b();
    for (uint32_t i = 0; i < len; ++i) {
      const PRUnichar c = data[i];
      switch (c) {
        case '<':
          extraSpaceNeeded += ArrayLength("&lt;") - 2;
          break;
        case '>':
          extraSpaceNeeded += ArrayLength("&gt;") - 2;
          break;
        case '&':
          extraSpaceNeeded += ArrayLength("&amp;") - 2;
          break;
        case 0x00A0:
          extraSpaceNeeded += ArrayLength("&nbsp;") - 2;
          break;
        default:
          break;
      }
    }
  } else {
    const char* data = aText->Get1b();
    for (uint32_t i = 0; i < len; ++i) {
      const unsigned char c = data[i];
      switch (c) {
        case '<':
          extraSpaceNeeded += ArrayLength("&lt;") - 2;
          break;
        case '>':
          extraSpaceNeeded += ArrayLength("&gt;") - 2;
          break;
        case '&':
          extraSpaceNeeded += ArrayLength("&amp;") - 2;
          break;
        case 0x00A0:
          extraSpaceNeeded += ArrayLength("&nbsp;") - 2;
          break;
        default:
          break;
      }
    }
  }

  if (extraSpaceNeeded) {
    aBuilder.AppendWithEncode(aText, len + extraSpaceNeeded);
  } else {
    aBuilder.Append(aText);
  }
}

static void
AppendEncodedAttributeValue(nsAutoString* aValue, StringBuilder& aBuilder)
{
  const PRUnichar* c = aValue->BeginReading();
  const PRUnichar* end = aValue->EndReading();

  uint32_t extraSpaceNeeded = 0;
  while (c < end) {
    switch (*c) {
      case '"':
        extraSpaceNeeded += ArrayLength("&quot;") - 2;
        break;
      case '&':
        extraSpaceNeeded += ArrayLength("&amp;") - 2;
        break;
      case 0x00A0:
        extraSpaceNeeded += ArrayLength("&nbsp;") - 2;
        break;
      default:
        break;
    }
    ++c;
  }

  if (extraSpaceNeeded) {
    aBuilder.AppendWithAttrEncode(aValue, aValue->Length() + extraSpaceNeeded);
  } else {
    aBuilder.Append(aValue);
  }
}

static void
StartElement(Element* aContent, StringBuilder& aBuilder)
{
  nsIAtom* localName = aContent->Tag();
  int32_t tagNS = aContent->GetNameSpaceID();

  aBuilder.Append("<");
  if (aContent->IsHTML() || aContent->IsSVG() || aContent->IsMathML()) {
    aBuilder.Append(localName);
  } else {
    aBuilder.Append(aContent->NodeName());
  }

  int32_t count = aContent->GetAttrCount();
  for (int32_t i = count; i > 0;) {
    --i;
    const nsAttrName* name = aContent->GetAttrNameAt(i);
    int32_t attNs = name->NamespaceID();
    nsIAtom* attName = name->LocalName();

    // Filter out any attribute starting with [-|_]moz
    nsDependentAtomString attrNameStr(attName);
    if (StringBeginsWith(attrNameStr, NS_LITERAL_STRING("_moz")) ||
        StringBeginsWith(attrNameStr, NS_LITERAL_STRING("-moz"))) {
      continue;
    }

    nsAutoString* attValue = new nsAutoString();
    aContent->GetAttr(attNs, attName, *attValue);

    // Filter out special case of <br type="_moz*"> used by the editor.
    // Bug 16988.  Yuck.
    if (localName == nsGkAtoms::br && tagNS == kNameSpaceID_XHTML &&
        attName == nsGkAtoms::type && attNs == kNameSpaceID_None &&
        StringBeginsWith(*attValue, NS_LITERAL_STRING("_moz"))) {
      delete attValue;
      continue;
    }
    
    if (MOZ_LIKELY(attNs == kNameSpaceID_None) ||
        (attNs == kNameSpaceID_XMLNS &&
         attName == nsGkAtoms::xmlns)) {
      aBuilder.Append(" ");
    } else if (attNs == kNameSpaceID_XML) {
      aBuilder.Append(" xml:");
    } else if (attNs == kNameSpaceID_XMLNS) {
      aBuilder.Append(" xmlns:");
    } else if (attNs == kNameSpaceID_XLink) {
      aBuilder.Append(" xlink:");
    } else {
      nsIAtom* prefix = name->GetPrefix();
      if (prefix) {
        aBuilder.Append(" ");
        aBuilder.Append(prefix);
        aBuilder.Append(":");
      }
    }

    aBuilder.Append(attName);
    aBuilder.Append("=\"");
    AppendEncodedAttributeValue(attValue, aBuilder);
    aBuilder.Append("\"");
  }

  aBuilder.Append(">");

  /*
  // Per HTML spec we should append one \n if the first child of
  // pre/textarea/listing is a textnode and starts with a \n.
  // But because browsers haven't traditionally had that behavior,
  // we're not changing our behavior either - yet.
  if (aContent->IsHTML()) {
    if (localName == nsGkAtoms::pre || localName == nsGkAtoms::textarea ||
        localName == nsGkAtoms::listing) {
      nsIContent* fc = aContent->GetFirstChild();
      if (fc &&
          (fc->NodeType() == nsIDOMNode::TEXT_NODE ||
           fc->NodeType() == nsIDOMNode::CDATA_SECTION_NODE)) {
        const nsTextFragment* text = fc->GetText();
        if (text && text->GetLength() && text->CharAt(0) == PRUnichar('\n')) {
          aBuilder.Append("\n");
        }
      }
    }
  }*/
}

static inline bool
ShouldEscape(nsIContent* aParent)
{
  if (!aParent || !aParent->IsHTML()) {
    return true;
  }

  static const nsIAtom* nonEscapingElements[] = {
    nsGkAtoms::style, nsGkAtoms::script, nsGkAtoms::xmp,
    nsGkAtoms::iframe, nsGkAtoms::noembed, nsGkAtoms::noframes,
    nsGkAtoms::plaintext, 
    // Per the current spec noscript should be escaped in case
    // scripts are disabled or if document doesn't have
    // browsing context. However the latter seems to be a spec bug
    // and Gecko hasn't traditionally done the former.
    nsGkAtoms::noscript    
  };
  static mozilla::BloomFilter<12, nsIAtom> sFilter;
  static bool sInitialized = false;
  if (!sInitialized) {
    sInitialized = true;
    for (uint32_t i = 0; i < ArrayLength(nonEscapingElements); ++i) {
      sFilter.add(nonEscapingElements[i]);
    }
  }

  nsIAtom* tag = aParent->Tag();
  if (sFilter.mightContain(tag)) {
    for (uint32_t i = 0; i < ArrayLength(nonEscapingElements); ++i) {
      if (tag == nonEscapingElements[i]) {
        return false;
      }
    }
  }
  return true;
}

static inline bool
IsVoidTag(Element* aElement)
{
  if (!aElement->IsHTML()) {
    return false;
  }

  static const nsIAtom* voidElements[] = {
    nsGkAtoms::area, nsGkAtoms::base, nsGkAtoms::basefont,
    nsGkAtoms::bgsound, nsGkAtoms::br, nsGkAtoms::col,
    nsGkAtoms::command, nsGkAtoms::embed, nsGkAtoms::frame,
    nsGkAtoms::hr, nsGkAtoms::img, nsGkAtoms::input,
    nsGkAtoms::keygen, nsGkAtoms::link, nsGkAtoms::meta,
    nsGkAtoms::param, nsGkAtoms::source, nsGkAtoms::track,
    nsGkAtoms::wbr
  };

  static mozilla::BloomFilter<12, nsIAtom> sFilter;
  static bool sInitialized = false;
  if (!sInitialized) {
    sInitialized = true;
    for (uint32_t i = 0; i < ArrayLength(voidElements); ++i) {
      sFilter.add(voidElements[i]);
    }
  }
  
  nsIAtom* tag = aElement->Tag();
  if (sFilter.mightContain(tag)) {
    for (uint32_t i = 0; i < ArrayLength(voidElements); ++i) {
      if (tag == voidElements[i]) {
        return true;
      }
    }
  }
  return false;
}

static bool
Serialize(Element* aRoot, bool aDescendentsOnly, nsAString& aOut)
{
  nsINode* current = aDescendentsOnly ? aRoot->GetFirstChild() : aRoot;
  if (!current) {
    return true;
  }

  StringBuilder builder;
  nsIContent* next;
  while (true) {
    bool isVoid = false;
    switch (current->NodeType()) {
      case nsIDOMNode::ELEMENT_NODE: {
        Element* elem = current->AsElement();
        StartElement(elem, builder);
        isVoid = IsVoidTag(elem);
        if (!isVoid && (next = current->GetFirstChild())) {
          current = next;
          continue;
        }
        break;
      }

      case nsIDOMNode::TEXT_NODE:
      case nsIDOMNode::CDATA_SECTION_NODE: {
        const nsTextFragment* text = static_cast<nsIContent*>(current)->GetText();
        nsIContent* parent = current->GetParent();
        if (ShouldEscape(parent)) {
          AppendEncodedCharacters(text, builder);
        } else {
          builder.Append(text);
        }
        break;
      }

      case nsIDOMNode::COMMENT_NODE: {
        builder.Append("<!--");
        builder.Append(static_cast<nsIContent*>(current)->GetText());
        builder.Append("-->");
        break;
      }

      case nsIDOMNode::DOCUMENT_TYPE_NODE: {
        builder.Append("<!DOCTYPE ");
        builder.Append(current->NodeName());
        builder.Append(">");
        break;
      }

      case nsIDOMNode::PROCESSING_INSTRUCTION_NODE: {
        builder.Append("<?");
        builder.Append(current->NodeName());
        builder.Append(" ");
        builder.Append(static_cast<nsIContent*>(current)->GetText());
        builder.Append(">");
        break;
      }
    }

    while (true) {
      if (!isVoid && current->NodeType() == nsIDOMNode::ELEMENT_NODE) {
        builder.Append("</");
        nsIContent* elem = static_cast<nsIContent*>(current);
        if (elem->IsHTML() || elem->IsSVG() || elem->IsMathML()) {
          builder.Append(elem->Tag());
        } else {
          builder.Append(current->NodeName());
        }
        builder.Append(">");
      }
      isVoid = false;

      if (current == aRoot) {
        return builder.ToString(aOut);
      }

      if ((next = current->GetNextSibling())) {
        current = next;
        break;
      }

      current = current->GetParentNode();
      if (aDescendentsOnly && current == aRoot) {
        return builder.ToString(aOut);
      }
    }
  }
}

nsresult
nsGenericHTMLElement::GetMarkup(bool aIncludeSelf, nsAString& aMarkup)
{
  aMarkup.Truncate();

  nsIDocument* doc = OwnerDoc();
  if (IsInHTMLDocument()) {
    return Serialize(this, !aIncludeSelf, aMarkup) ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
  }

  nsAutoString contentType;
  doc->GetContentType(contentType);

  nsCOMPtr<nsIDocumentEncoder> docEncoder = doc->GetCachedEncoder();
  if (!docEncoder) {
    docEncoder =
      do_CreateInstance(PromiseFlatCString(
        nsDependentCString(NS_DOC_ENCODER_CONTRACTID_BASE) +
        NS_ConvertUTF16toUTF8(contentType)
      ).get());
  }
  if (!docEncoder) {
    // This could be some type for which we create a synthetic document.  Try
    // again as XML
    contentType.AssignLiteral("application/xml");
    docEncoder = do_CreateInstance(NS_DOC_ENCODER_CONTRACTID_BASE "application/xml");
  }

  NS_ENSURE_TRUE(docEncoder, NS_ERROR_FAILURE);

  uint32_t flags = nsIDocumentEncoder::OutputEncodeBasicEntities |
                   // Output DOM-standard newlines
                   nsIDocumentEncoder::OutputLFLineBreak |
                   // Don't do linebreaking that's not present in
                   // the source
                   nsIDocumentEncoder::OutputRaw |
                   // Only check for mozdirty when necessary (bug 599983)
                   nsIDocumentEncoder::OutputIgnoreMozDirty;

  if (IsEditable()) {
    nsCOMPtr<nsIEditor> editor;
    GetEditorInternal(getter_AddRefs(editor));
    if (editor && editor->OutputsMozDirty()) {
      flags &= ~nsIDocumentEncoder::OutputIgnoreMozDirty;
    }
  }

  nsresult rv = docEncoder->NativeInit(doc, contentType, flags);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aIncludeSelf) {
    docEncoder->SetNativeNode(this);
  } else {
    docEncoder->SetNativeContainerNode(this);
  }
  rv = docEncoder->EncodeToString(aMarkup);
  if (!aIncludeSelf) {
    doc->SetCachedEncoder(docEncoder.forget());
  }
  return rv;
}

void
nsGenericHTMLElement::GetInnerHTML(nsAString& aInnerHTML, ErrorResult& aError)
{
  aError = GetMarkup(false, aInnerHTML);
}

void
nsGenericHTMLElement::GetOuterHTML(nsAString& aOuterHTML, ErrorResult& aError)
{
  aError = GetMarkup(true, aOuterHTML);
}

void
nsGenericHTMLElement::FireMutationEventsForDirectParsing(nsIDocument* aDoc,
                                                         nsIContent* aDest,
                                                         int32_t aOldChildCount)
{
  // Fire mutation events. Optimize for the case when there are no listeners
  int32_t newChildCount = aDest->GetChildCount();
  if (newChildCount && nsContentUtils::
        HasMutationListeners(aDoc, NS_EVENT_BITS_MUTATION_NODEINSERTED)) {
    nsAutoTArray<nsCOMPtr<nsIContent>, 50> childNodes;
    NS_ASSERTION(newChildCount - aOldChildCount >= 0,
                 "What, some unexpected dom mutation has happened?");
    childNodes.SetCapacity(newChildCount - aOldChildCount);
    for (nsIContent* child = aDest->GetFirstChild();
         child;
         child = child->GetNextSibling()) {
      childNodes.AppendElement(child);
    }
    Element::FireNodeInserted(aDoc, aDest, childNodes);
  }
}

void
nsGenericHTMLElement::SetInnerHTML(const nsAString& aInnerHTML,
                                   ErrorResult& aError)
{
  nsIDocument* doc = OwnerDoc();

  // Batch possible DOMSubtreeModified events.
  mozAutoSubtreeModified subtree(doc, nullptr);

  FireNodeRemovedForChildren();

  // Needed when innerHTML is used in combination with contenteditable
  mozAutoDocUpdate updateBatch(doc, UPDATE_CONTENT_MODEL, true);

  // Remove childnodes.
  uint32_t childCount = GetChildCount();
  nsAutoMutationBatch mb(this, true, false);
  for (uint32_t i = 0; i < childCount; ++i) {
    RemoveChildAt(0, true);
  }
  mb.RemovalDone();

  nsAutoScriptLoaderDisabler sld(doc);

  if (doc->IsHTML()) {
    int32_t oldChildCount = GetChildCount();
    aError = nsContentUtils::ParseFragmentHTML(aInnerHTML,
                                               this,
                                               Tag(),
                                               GetNameSpaceID(),
                                               doc->GetCompatibilityMode() ==
                                                 eCompatibility_NavQuirks,
                                               true);
    mb.NodesAdded();
    // HTML5 parser has notified, but not fired mutation events.
    FireMutationEventsForDirectParsing(doc, this, oldChildCount);
  } else {
    nsCOMPtr<nsIDOMDocumentFragment> df;
    aError = nsContentUtils::CreateContextualFragment(this, aInnerHTML,
                                                      true,
                                                      getter_AddRefs(df));
    nsCOMPtr<nsINode> fragment = do_QueryInterface(df);
    if (!aError.Failed()) {
      // Suppress assertion about node removal mutation events that can't have
      // listeners anyway, because no one has had the chance to register mutation
      // listeners on the fragment that comes from the parser.
      nsAutoScriptBlockerSuppressNodeRemoved scriptBlocker;

      static_cast<nsINode*>(this)->AppendChild(*fragment, aError);
      mb.NodesAdded();
    }
  }
}

void
nsGenericHTMLElement::SetOuterHTML(const nsAString& aOuterHTML,
                                   ErrorResult& aError)
{
  nsCOMPtr<nsINode> parent = GetParentNode();
  if (!parent) {
    return;
  }

  if (parent->NodeType() == nsIDOMNode::DOCUMENT_NODE) {
    aError.Throw(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR);
    return;
  }

  if (OwnerDoc()->IsHTML()) {
    nsIAtom* localName;
    int32_t namespaceID;
    if (parent->IsElement()) {
      localName = static_cast<nsIContent*>(parent.get())->Tag();
      namespaceID = static_cast<nsIContent*>(parent.get())->GetNameSpaceID();
    } else {
      NS_ASSERTION(parent->NodeType() == nsIDOMNode::DOCUMENT_FRAGMENT_NODE,
        "How come the parent isn't a document, a fragment or an element?");
      localName = nsGkAtoms::body;
      namespaceID = kNameSpaceID_XHTML;
    }
    nsCOMPtr<nsIDOMDocumentFragment> df;
    aError = NS_NewDocumentFragment(getter_AddRefs(df),
                                    OwnerDoc()->NodeInfoManager());
    if (aError.Failed()) {
      return;
    }
    nsCOMPtr<nsIContent> fragment = do_QueryInterface(df);
    nsContentUtils::ParseFragmentHTML(aOuterHTML,
                                      fragment,
                                      localName,
                                      namespaceID,
                                      OwnerDoc()->GetCompatibilityMode() ==
                                        eCompatibility_NavQuirks,
                                      true);
    nsAutoMutationBatch mb(parent, true, false);
    parent->ReplaceChild(*fragment, *this, aError);
    return;
  }

  nsCOMPtr<nsINode> context;
  if (parent->IsElement()) {
    context = parent;
  } else {
    NS_ASSERTION(parent->NodeType() == nsIDOMNode::DOCUMENT_FRAGMENT_NODE,
      "How come the parent isn't a document, a fragment or an element?");
    nsCOMPtr<nsINodeInfo> info =
      OwnerDoc()->NodeInfoManager()->GetNodeInfo(nsGkAtoms::body,
                                                 nullptr,
                                                 kNameSpaceID_XHTML,
                                                 nsIDOMNode::ELEMENT_NODE);
    context = NS_NewHTMLBodyElement(info.forget(), FROM_PARSER_FRAGMENT);
  }

  nsCOMPtr<nsIDOMDocumentFragment> df;
  aError = nsContentUtils::CreateContextualFragment(context,
                                                    aOuterHTML,
                                                    true,
                                                    getter_AddRefs(df));
  if (aError.Failed()) {
    return;
  }
  nsCOMPtr<nsINode> fragment = do_QueryInterface(df);
  nsAutoMutationBatch mb(parent, true, false);
  parent->ReplaceChild(*fragment, *this, aError);
}

enum nsAdjacentPosition {
  eBeforeBegin,
  eAfterBegin,
  eBeforeEnd,
  eAfterEnd
};

NS_IMETHODIMP
nsGenericHTMLElement::InsertAdjacentHTML(const nsAString& aPosition,
                                         const nsAString& aText)
{
  ErrorResult rv;
  InsertAdjacentHTML(aPosition, aText, rv);
  return rv.ErrorCode();
}

void
nsGenericHTMLElement::InsertAdjacentHTML(const nsAString& aPosition,
                                         const nsAString& aText,
                                         ErrorResult& aError)
{
  nsAdjacentPosition position;
  if (aPosition.LowerCaseEqualsLiteral("beforebegin")) {
    position = eBeforeBegin;
  } else if (aPosition.LowerCaseEqualsLiteral("afterbegin")) {
    position = eAfterBegin;
  } else if (aPosition.LowerCaseEqualsLiteral("beforeend")) {
    position = eBeforeEnd;
  } else if (aPosition.LowerCaseEqualsLiteral("afterend")) {
    position = eAfterEnd;
  } else {
    aError.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return;
  }

  nsCOMPtr<nsIContent> destination;
  if (position == eBeforeBegin || position == eAfterEnd) {
    destination = GetParent();
    if (!destination) {
      aError.Throw(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR);
      return;
    }
  } else {
    destination = this;
  }

  nsIDocument* doc = OwnerDoc();

  // Needed when insertAdjacentHTML is used in combination with contenteditable
  mozAutoDocUpdate updateBatch(doc, UPDATE_CONTENT_MODEL, true);
  nsAutoScriptLoaderDisabler sld(doc);
  
  // Batch possible DOMSubtreeModified events.
  mozAutoSubtreeModified subtree(doc, nullptr);

  // Parse directly into destination if possible
  if (doc->IsHTML() && !OwnerDoc()->MayHaveDOMMutationObservers() &&
      (position == eBeforeEnd ||
       (position == eAfterEnd && !GetNextSibling()) ||
       (position == eAfterBegin && !GetFirstChild()))) {
    int32_t oldChildCount = destination->GetChildCount();
    int32_t contextNs = destination->GetNameSpaceID();
    nsIAtom* contextLocal = destination->Tag();
    if (contextLocal == nsGkAtoms::html && contextNs == kNameSpaceID_XHTML) {
      // For compat with IE6 through IE9. Willful violation of HTML5 as of
      // 2011-04-06. CreateContextualFragment does the same already.
      // Spec bug: http://www.w3.org/Bugs/Public/show_bug.cgi?id=12434
      contextLocal = nsGkAtoms::body;
    }
    aError = nsContentUtils::ParseFragmentHTML(aText,
                                               destination,
                                               contextLocal,
                                               contextNs,
                                               doc->GetCompatibilityMode() ==
                                                 eCompatibility_NavQuirks,
                                               true);
    // HTML5 parser has notified, but not fired mutation events.
    FireMutationEventsForDirectParsing(doc, destination, oldChildCount);
    return;
  }

  // couldn't parse directly
  nsCOMPtr<nsIDOMDocumentFragment> df;
  aError = nsContentUtils::CreateContextualFragment(destination,
                                                    aText,
                                                    true,
                                                    getter_AddRefs(df));
  if (aError.Failed()) {
    return;
  }

  nsCOMPtr<nsINode> fragment = do_QueryInterface(df);

  // Suppress assertion about node removal mutation events that can't have
  // listeners anyway, because no one has had the chance to register mutation
  // listeners on the fragment that comes from the parser.
  nsAutoScriptBlockerSuppressNodeRemoved scriptBlocker;

  nsAutoMutationBatch mb(destination, true, false);
  switch (position) {
    case eBeforeBegin:
      destination->InsertBefore(*fragment, this, aError);
      break;
    case eAfterBegin:
      static_cast<nsINode*>(this)->InsertBefore(*fragment, GetFirstChild(),
                                                aError);
      break;
    case eBeforeEnd:
      static_cast<nsINode*>(this)->AppendChild(*fragment, aError);
      break;
    case eAfterEnd:
      destination->InsertBefore(*fragment, GetNextSibling(), aError);
      break;
  }
}

bool
nsGenericHTMLElement::Spellcheck()
{
  // Has the state has been explicitly set?
  nsIContent* node;
  for (node = this; node; node = node->GetParent()) {
    if (node->IsHTML()) {
      static nsIContent::AttrValuesArray strings[] =
        {&nsGkAtoms::_true, &nsGkAtoms::_false, nullptr};
      switch (node->FindAttrValueIn(kNameSpaceID_None, nsGkAtoms::spellcheck,
                                    strings, eCaseMatters)) {
        case 0:                         // spellcheck = "true"
          return true;
        case 1:                         // spellcheck = "false"
          return false;
      }
    }
  }

  // Is this a chrome element?
  if (nsContentUtils::IsChromeDoc(OwnerDoc())) {
    return false;                       // Not spellchecked by default
  }

  if (IsCurrentBodyElement()) {
    nsCOMPtr<nsIHTMLDocument> doc = do_QueryInterface(GetCurrentDoc());
    return doc && doc->IsEditingOn();
  }

  // Is this element editable?
  nsCOMPtr<nsIFormControl> formControl = do_QueryInterface(this);
  if (!formControl) {
    return false;                       // Not spellchecked by default
  }

  // Is this a multiline plaintext input?
  int32_t controlType = formControl->GetType();
  if (controlType == NS_FORM_TEXTAREA) {
    return true;             // Spellchecked by default
  }

  // Is this anything other than an input text?
  // Other inputs are not spellchecked.
  if (controlType != NS_FORM_INPUT_TEXT) {
    return false;                       // Not spellchecked by default
  }

  // Does the user want input text spellchecked by default?
  // NOTE: Do not reflect a pref value of 0 back to the DOM getter.
  // The web page should not know if the user has disabled spellchecking.
  // We'll catch this in the editor itself.
  int32_t spellcheckLevel = Preferences::GetInt("layout.spellcheckDefault", 1);
  return spellcheckLevel == 2;           // "Spellcheck multi- and single-line"
}

bool
nsGenericHTMLElement::InNavQuirksMode(nsIDocument* aDoc)
{
  return aDoc && aDoc->GetCompatibilityMode() == eCompatibility_NavQuirks;
}

void
nsGenericHTMLElement::UpdateEditableState(bool aNotify)
{
  // XXX Should we do this only when in a document?
  ContentEditableTristate value = GetContentEditableValue();
  if (value != eInherit) {
    DoSetEditableFlag(!!value, aNotify);
    return;
  }

  nsStyledElement::UpdateEditableState(aNotify);
}

nsEventStates
nsGenericHTMLElement::IntrinsicState() const
{
  nsEventStates state = nsGenericHTMLElementBase::IntrinsicState();

  if (GetDirectionality() == eDir_RTL) {
    state |= NS_EVENT_STATE_RTL;
    state &= ~NS_EVENT_STATE_LTR;
  } else { // at least for HTML, directionality is exclusively LTR or RTL
    NS_ASSERTION(GetDirectionality() == eDir_LTR,
                 "HTML element's directionality must be either RTL or LTR");
    state |= NS_EVENT_STATE_LTR;
    state &= ~NS_EVENT_STATE_RTL;
  }

  return state;
}

nsresult
nsGenericHTMLElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                                 nsIContent* aBindingParent,
                                 bool aCompileEventHandlers)
{
  nsresult rv = nsGenericHTMLElementBase::BindToTree(aDocument, aParent,
                                                     aBindingParent,
                                                     aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aDocument) {
    if (HasProperties()) {
      HTMLPropertiesCollection* properties = 
        static_cast<HTMLPropertiesCollection*>(GetProperty(nsGkAtoms::microdataProperties));
      if (properties) {
        properties->SetDocument(aDocument);
      }
    }
    RegAccessKey();
    if (HasName()) {
      aDocument->
        AddToNameTable(this, GetParsedAttr(nsGkAtoms::name)->GetAtomValue());
    }
    if (HasFlag(NODE_IS_EDITABLE) && GetContentEditableValue() == eTrue) {
      nsCOMPtr<nsIHTMLDocument> htmlDocument = do_QueryInterface(aDocument);
      if (htmlDocument) {
        htmlDocument->ChangeContentEditableCount(this, +1);
      }
    }
  }

  return rv;
}

void
nsGenericHTMLElement::UnbindFromTree(bool aDeep, bool aNullParent)
{
  if (IsInDoc()) {
    UnregAccessKey();
  }
  
  if(HasProperties()) {
    HTMLPropertiesCollection* properties = 
      static_cast<HTMLPropertiesCollection*>(GetProperty(nsGkAtoms::microdataProperties));
    if (properties) {
      properties->SetDocument(nullptr);
    }
  }

  RemoveFromNameTable();

  if (GetContentEditableValue() == eTrue) {
    nsCOMPtr<nsIHTMLDocument> htmlDocument = do_QueryInterface(GetCurrentDoc());
    if (htmlDocument) {
      htmlDocument->ChangeContentEditableCount(this, -1);
    }
  }

  nsStyledElement::UnbindFromTree(aDeep, aNullParent);
}

nsHTMLFormElement*
nsGenericHTMLElement::FindAncestorForm(nsHTMLFormElement* aCurrentForm)
{
  NS_ASSERTION(!HasAttr(kNameSpaceID_None, nsGkAtoms::form),
               "FindAncestorForm should not be called if @form is set!");

  // Make sure we don't end up finding a form that's anonymous from
  // our point of view.
  nsIContent* bindingParent = GetBindingParent();

  nsIContent* content = this;
  while (content != bindingParent && content) {
    // If the current ancestor is a form, return it as our form
    if (content->IsHTML(nsGkAtoms::form)) {
#ifdef DEBUG
      if (!nsContentUtils::IsInSameAnonymousTree(this, content)) {
        // It's possible that we started unbinding at |content| or
        // some ancestor of it, and |content| and |this| used to all be
        // anonymous.  Check for this the hard way.
        for (nsIContent* child = this; child != content;
             child = child->GetParent()) {
          NS_ASSERTION(child->GetParent()->IndexOf(child) != -1,
                       "Walked too far?");
        }
      }
#endif
      return static_cast<nsHTMLFormElement*>(content);
    }

    nsIContent *prevContent = content;
    content = prevContent->GetParent();

    if (!content && aCurrentForm) {
      // We got to the root of the subtree we're in, and we're being removed
      // from the DOM (the only time we get into this method with a non-null
      // aCurrentForm).  Check whether aCurrentForm is in the same subtree.  If
      // it is, we want to return aCurrentForm, since this case means that
      // we're one of those inputs-in-a-table that have a hacked mForm pointer
      // and a subtree containing both us and the form got removed from the
      // DOM.
      if (nsContentUtils::ContentIsDescendantOf(aCurrentForm, prevContent)) {
        return aCurrentForm;
      }
    }
  }

  return nullptr;
}

bool
nsGenericHTMLElement::CheckHandleEventForAnchorsPreconditions(nsEventChainVisitor& aVisitor)
{
  NS_PRECONDITION(nsCOMPtr<nsILink>(do_QueryInterface(this)),
                  "should be called only when |this| implements |nsILink|");

  if (!aVisitor.mPresContext) {
    // We need a pres context to do link stuff. Some events (e.g. mutation
    // events) don't have one.
    // XXX: ideally, shouldn't we be able to do what we need without one?
    return false; 
  }

  //Need to check if we hit an imagemap area and if so see if we're handling
  //the event on that map or on a link farther up the tree.  If we're on a
  //link farther up, do nothing.
  nsCOMPtr<nsIContent> target = aVisitor.mPresContext->EventStateManager()->
    GetEventTargetContent(aVisitor.mEvent);

  return !target || !target->IsHTML(nsGkAtoms::area) || IsHTML(nsGkAtoms::area);
}

nsresult
nsGenericHTMLElement::PreHandleEventForAnchors(nsEventChainPreVisitor& aVisitor)
{
  nsresult rv = nsGenericHTMLElementBase::PreHandleEvent(aVisitor);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!CheckHandleEventForAnchorsPreconditions(aVisitor)) {
    return NS_OK;
  }

  return PreHandleEventForLinks(aVisitor);
}

nsresult
nsGenericHTMLElement::PostHandleEventForAnchors(nsEventChainPostVisitor& aVisitor)
{
  if (!CheckHandleEventForAnchorsPreconditions(aVisitor)) {
    return NS_OK;
  }

  return PostHandleEventForLinks(aVisitor);
}

bool
nsGenericHTMLElement::IsHTMLLink(nsIURI** aURI) const
{
  NS_PRECONDITION(aURI, "Must provide aURI out param");

  *aURI = GetHrefURIForAnchors().get();
  // We promise out param is non-null if we return true, so base rv on it
  return *aURI != nullptr;
}

already_AddRefed<nsIURI>
nsGenericHTMLElement::GetHrefURIForAnchors() const
{
  // This is used by the three nsILink implementations and
  // nsHTMLStyleElement.

  // Get href= attribute (relative URI).

  // We use the nsAttrValue's copy of the URI string to avoid copying.
  nsCOMPtr<nsIURI> uri;
  GetURIAttr(nsGkAtoms::href, nullptr, getter_AddRefs(uri));

  return uri.forget();
}

nsresult
nsGenericHTMLElement::BeforeSetAttr(int32_t aNamespaceID, nsIAtom* aName,
                                    const nsAttrValueOrString* aValue,
                                    bool aNotify)
{
  if (aNamespaceID == kNameSpaceID_None &&
      aName == nsGkAtoms::dir &&
      HasDirAuto()) {
      // setting dir on an element that currently has dir=auto
    WalkDescendantsClearAncestorDirAuto(this);
    SetHasDirAuto();
  }

  return nsGenericHTMLElementBase::BeforeSetAttr(aNamespaceID, aName,
                                                 aValue, aNotify);
}

nsresult
nsGenericHTMLElement::AfterSetAttr(int32_t aNamespaceID, nsIAtom* aName,
                                   const nsAttrValue* aValue, bool aNotify)
{
  if (aNamespaceID == kNameSpaceID_None) {
    uint32_t eventType = EventNameType_HTML;
    if (mNodeInfo->Equals(nsGkAtoms::body) ||
        mNodeInfo->Equals(nsGkAtoms::frameset)) {
      eventType |= EventNameType_HTMLBodyOrFramesetOnly;
    }
    if (nsContentUtils::IsEventAttributeName(aName, eventType) &&
        aValue) {
      NS_ABORT_IF_FALSE(aValue->Type() == nsAttrValue::eString,
        "Expected string value for script body");
      nsresult rv = SetEventHandler(aName, aValue->GetStringValue());
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else if (aNotify && aName == nsGkAtoms::spellcheck) {
      SyncEditorsOnSubtree(this);
    }
    else if (aName == nsGkAtoms::dir) {
      Directionality dir = eDir_LTR;
      if (aValue && aValue->Type() == nsAttrValue::eEnum) {
        SetHasValidDir();
        Directionality dirValue = (Directionality)aValue->GetEnumValue();
        if (dirValue == eDir_Auto) {
          SetHasDirAuto();
          ClearHasFixedDir();
        } else {
          dir = dirValue;
          SetDirectionality(dir, aNotify);
          ClearHasDirAuto();
          ClearHasDirAutoSet();
          SetHasFixedDir();
        }
      } else {
        ClearHasValidDir();
        ClearHasFixedDir();
        if (NodeInfo()->Equals(nsGkAtoms::bdi)) {
          SetHasDirAuto();
        } else {
          dir = RecomputeDirectionality(this, aNotify);
        }
      }
      SetDirectionalityOnDescendants(this, dir, aNotify);
    }
  }

  return nsGenericHTMLElementBase::AfterSetAttr(aNamespaceID, aName,
                                                aValue, aNotify);
}

nsEventListenerManager*
nsGenericHTMLElement::GetEventListenerManagerForAttr(nsIAtom* aAttrName,
                                                     bool* aDefer)
{
  // Attributes on the body and frameset tags get set on the global object
  if ((mNodeInfo->Equals(nsGkAtoms::body) ||
       mNodeInfo->Equals(nsGkAtoms::frameset)) &&
      // We only forward some event attributes from body/frameset to window
      (0
#define EVENT(name_, id_, type_, struct_) /* nothing */
#define FORWARDED_EVENT(name_, id_, type_, struct_) \
       || nsGkAtoms::on##name_ == aAttrName
#define WINDOW_EVENT FORWARDED_EVENT
#include "nsEventNameList.h"
#undef WINDOW_EVENT
#undef FORWARDED_EVENT
#undef EVENT
       )
      ) {
    nsPIDOMWindow *win;

    // If we have a document, and it has a window, add the event
    // listener on the window (the inner window). If not, proceed as
    // normal.
    // XXXbz sXBL/XBL2 issue: should we instead use GetCurrentDoc() here,
    // override BindToTree for those classes and munge event listeners there?
    nsIDocument *document = OwnerDoc();

    // FIXME (https://bugzilla.mozilla.org/show_bug.cgi?id=431767)
    // nsDocument::GetInnerWindow can return an outer window in some cases,
    // we don't want to stick an event listener on an outer window, so
    // bail if it does.  See similar code in HTMLBodyElement and
    // HTMLFramesetElement
    *aDefer = false;
    if ((win = document->GetInnerWindow()) && win->IsInnerWindow()) {
      nsCOMPtr<nsIDOMEventTarget> piTarget(do_QueryInterface(win));

      return piTarget->GetListenerManager(true);
    }

    return nullptr;
  }

  return nsGenericHTMLElementBase::GetEventListenerManagerForAttr(aAttrName,
                                                                  aDefer);
}

// FIXME (https://bugzilla.mozilla.org/show_bug.cgi?id=431767)
// nsDocument::GetInnerWindow can return an outer window in some
// cases.  We don't want to stick an event listener on an outer
// window, so bail if it does.  See also similar code in
// nsGenericHTMLElement::GetEventListenerManagerForAttr.
#define EVENT(name_, id_, type_, struct_) /* nothing; handled by nsINode */
#define FORWARDED_EVENT(name_, id_, type_, struct_)                           \
EventHandlerNonNull*                                                          \
nsGenericHTMLElement::GetOn##name_()                                          \
{                                                                             \
  if (Tag() == nsGkAtoms::body || Tag() == nsGkAtoms::frameset) {             \
    /* XXXbz note to self: add tests for this! */                             \
    nsPIDOMWindow* win = OwnerDoc()->GetInnerWindow();                        \
    if (win && win->IsInnerWindow()) {                                        \
      nsCOMPtr<nsISupports> supports = do_QueryInterface(win);                \
      nsGlobalWindow* globalWin = nsGlobalWindow::FromSupports(supports);     \
      return globalWin->GetOn##name_();                                       \
    }                                                                         \
    return nullptr;                                                           \
  }                                                                           \
                                                                              \
  return nsINode::GetOn##name_();                                             \
}                                                                             \
void                                                                          \
nsGenericHTMLElement::SetOn##name_(EventHandlerNonNull* handler,              \
                                   ErrorResult& error)                        \
{                                                                             \
  if (Tag() == nsGkAtoms::body || Tag() == nsGkAtoms::frameset) {             \
    nsPIDOMWindow* win = OwnerDoc()->GetInnerWindow();                        \
    if (!win || !win->IsInnerWindow()) {                                      \
      return;                                                                 \
    }                                                                         \
                                                                              \
    nsCOMPtr<nsISupports> supports = do_QueryInterface(win);                  \
    nsGlobalWindow* globalWin = nsGlobalWindow::FromSupports(supports);       \
    return globalWin->SetOn##name_(handler, error);                           \
  }                                                                           \
                                                                              \
  return nsINode::SetOn##name_(handler, error);                               \
}
#define ERROR_EVENT(name_, id_, type_, struct_)                               \
already_AddRefed<EventHandlerNonNull>                                         \
nsGenericHTMLElement::GetOn##name_()                                          \
{                                                                             \
  if (Tag() == nsGkAtoms::body || Tag() == nsGkAtoms::frameset) {             \
    /* XXXbz note to self: add tests for this! */                             \
    nsPIDOMWindow* win = OwnerDoc()->GetInnerWindow();                        \
    if (win && win->IsInnerWindow()) {                                        \
      nsCOMPtr<nsISupports> supports = do_QueryInterface(win);                \
      nsGlobalWindow* globalWin = nsGlobalWindow::FromSupports(supports);     \
      OnErrorEventHandlerNonNull* errorHandler = globalWin->GetOn##name_();   \
      if (errorHandler) {                                                     \
        nsRefPtr<EventHandlerNonNull> handler =                               \
          new EventHandlerNonNull(errorHandler);                              \
        return handler.forget();                                              \
      }                                                                       \
    }                                                                         \
    return nullptr;                                                           \
  }                                                                           \
                                                                              \
  nsRefPtr<EventHandlerNonNull> handler = nsINode::GetOn##name_();            \
  return handler.forget();                                                    \
}                                                                             \
void                                                                          \
nsGenericHTMLElement::SetOn##name_(EventHandlerNonNull* handler,              \
                                   ErrorResult& error)                        \
{                                                                             \
  if (Tag() == nsGkAtoms::body || Tag() == nsGkAtoms::frameset) {             \
    nsPIDOMWindow* win = OwnerDoc()->GetInnerWindow();                        \
    if (!win || !win->IsInnerWindow()) {                                      \
      return;                                                                 \
    }                                                                         \
                                                                              \
    nsCOMPtr<nsISupports> supports = do_QueryInterface(win);                  \
    nsGlobalWindow* globalWin = nsGlobalWindow::FromSupports(supports);       \
    nsRefPtr<OnErrorEventHandlerNonNull> errorHandler;                        \
    if (handler) {                                                            \
      errorHandler = new OnErrorEventHandlerNonNull(handler);                 \
    }                                                                         \
    return globalWin->SetOn##name_(errorHandler, error);                      \
  }                                                                           \
                                                                              \
  return nsINode::SetOn##name_(handler, error);                               \
}
#include "nsEventNameList.h"
#undef ERROR_EVENT
#undef FORWARDED_EVENT
#undef EVENT

nsresult
nsGenericHTMLElement::SetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                              nsIAtom* aPrefix, const nsAString& aValue,
                              bool aNotify)
{
  bool contentEditable = aNameSpaceID == kNameSpaceID_None &&
                           aName == nsGkAtoms::contenteditable;
  bool accessKey = aName == nsGkAtoms::accesskey && 
                     aNameSpaceID == kNameSpaceID_None;

  int32_t change = 0;
  if (contentEditable) {
    change = GetContentEditableValue() == eTrue ? -1 : 0;
    SetMayHaveContentEditableAttr();
  }

  if (accessKey) {
    UnregAccessKey();
  }

  nsresult rv = nsStyledElement::SetAttr(aNameSpaceID, aName, aPrefix, aValue,
                                         aNotify);
  NS_ENSURE_SUCCESS(rv, rv);

  if (contentEditable) {
    if (aValue.IsEmpty() || aValue.LowerCaseEqualsLiteral("true")) {
      change += 1;
    }

    ChangeEditableState(change);
  }

  if (accessKey && !aValue.IsEmpty()) {
    SetFlags(NODE_HAS_ACCESSKEY);
    RegAccessKey();
  }

  return NS_OK;
}

nsresult
nsGenericHTMLElement::UnsetAttr(int32_t aNameSpaceID, nsIAtom* aAttribute,
                                bool aNotify)
{
  bool contentEditable = false;
  int32_t contentEditableChange = 0;

  // Check for event handlers
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::name) {
      // Have to do this before clearing flag. See RemoveFromNameTable
      RemoveFromNameTable();
      ClearHasName();
    }
    else if (aAttribute == nsGkAtoms::contenteditable) {
      contentEditable = true;
      contentEditableChange = GetContentEditableValue() == eTrue ? -1 : 0;
    }
    else if (aAttribute == nsGkAtoms::accesskey) {
      // Have to unregister before clearing flag. See UnregAccessKey
      UnregAccessKey();
      UnsetFlags(NODE_HAS_ACCESSKEY);
    }
    else if (nsContentUtils::IsEventAttributeName(aAttribute,
                                                  EventNameType_HTML)) {
      nsEventListenerManager* manager = GetListenerManager(false);
      if (manager) {
        manager->RemoveEventHandler(aAttribute);
      }
    }
  }

  nsresult rv = nsGenericHTMLElementBase::UnsetAttr(aNameSpaceID, aAttribute,
                                                    aNotify);
  NS_ENSURE_SUCCESS(rv, rv);

  if (contentEditable) {
    ChangeEditableState(contentEditableChange);
  }

  return NS_OK;
}

void
nsGenericHTMLElement::GetBaseTarget(nsAString& aBaseTarget) const
{
  OwnerDoc()->GetBaseTarget(aBaseTarget);
}

//----------------------------------------------------------------------

static bool
CanHaveName(nsIAtom* aTag)
{
  return aTag == nsGkAtoms::img ||
         aTag == nsGkAtoms::form ||
         aTag == nsGkAtoms::applet ||
         aTag == nsGkAtoms::embed ||
         aTag == nsGkAtoms::object;
}

bool
nsGenericHTMLElement::ParseAttribute(int32_t aNamespaceID,
                                     nsIAtom* aAttribute,
                                     const nsAString& aValue,
                                     nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::dir) {
      return aResult.ParseEnumValue(aValue, kDirTable, false);
    }
  
    if (aAttribute == nsGkAtoms::tabindex) {
      return aResult.ParseIntWithBounds(aValue, -32768, 32767);
    }

    if (aAttribute == nsGkAtoms::name) {
      // Store name as an atom.  name="" means that the element has no name,
      // not that it has an emptystring as the name.
      RemoveFromNameTable();
      if (aValue.IsEmpty()) {
        ClearHasName();
        return false;
      }

      aResult.ParseAtom(aValue);

      if (CanHaveName(Tag())) {
        SetHasName();
        AddToNameTable(aResult.GetAtomValue());
      }
      
      return true;
    }

    if (aAttribute == nsGkAtoms::contenteditable) {
      aResult.ParseAtom(aValue);
      return true;
    }

    if (aAttribute == nsGkAtoms::itemref ||
        aAttribute == nsGkAtoms::itemprop ||
        aAttribute == nsGkAtoms::itemtype) {
      aResult.ParseAtomArray(aValue);
      return true;
    }
  }

  return nsGenericHTMLElementBase::ParseAttribute(aNamespaceID, aAttribute,
                                                  aValue, aResult);
}

bool
nsGenericHTMLElement::ParseBackgroundAttribute(int32_t aNamespaceID,
                                               nsIAtom* aAttribute,
                                               const nsAString& aValue,
                                               nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None &&
      aAttribute == nsGkAtoms::background &&
      !aValue.IsEmpty()) {
    // Resolve url to an absolute url
    nsIDocument* doc = OwnerDoc();
    nsCOMPtr<nsIURI> baseURI = GetBaseURI();
    nsCOMPtr<nsIURI> uri;
    nsresult rv = nsContentUtils::NewURIWithDocumentCharset(
        getter_AddRefs(uri), aValue, doc, baseURI);
    if (NS_FAILED(rv)) {
      return false;
    }

    nsString value(aValue);
    nsRefPtr<nsStringBuffer> buffer = nsCSSValue::BufferFromString(value);
    if (MOZ_UNLIKELY(!buffer)) {
      return false;
    }

    mozilla::css::URLValue *url =
      new mozilla::css::URLValue(uri, buffer, doc->GetDocumentURI(),
                                 NodePrincipal());
    aResult.SetTo(url, &aValue);
    return true;
  }

  return false;
}

bool
nsGenericHTMLElement::IsAttributeMapped(const nsIAtom* aAttribute) const
{
  static const MappedAttributeEntry* const map[] = {
    sCommonAttributeMap
  };
  
  return FindAttributeDependence(aAttribute, map);
}

nsMapRuleToAttributesFunc
nsGenericHTMLElement::GetAttributeMappingFunction() const
{
  return &MapCommonAttributesInto;
}

nsIFormControlFrame*
nsGenericHTMLElement::GetFormControlFrame(bool aFlushFrames)
{
  if (aFlushFrames && IsInDoc()) {
    // Cause a flush of the frames, so we get up-to-date frame information
    GetCurrentDoc()->FlushPendingNotifications(Flush_Frames);
  }
  nsIFrame* frame = GetPrimaryFrame();
  if (frame) {
    nsIFormControlFrame* form_frame = do_QueryFrame(frame);
    if (form_frame) {
      return form_frame;
    }

    // If we have generated content, the primary frame will be a
    // wrapper frame..  out real frame will be in its child list.
    for (frame = frame->GetFirstPrincipalChild();
         frame;
         frame = frame->GetNextSibling()) {
      form_frame = do_QueryFrame(frame);
      if (form_frame) {
        return form_frame;
      }
    }
  }

  return nullptr;
}

/* static */ nsresult
nsGenericHTMLElement::GetPrimaryPresState(nsGenericHTMLElement* aContent,
                                          nsPresState** aPresState)
{
  NS_ENSURE_ARG_POINTER(aPresState);
  *aPresState = nullptr;

  nsresult result = NS_OK;

  nsAutoCString key;
  nsCOMPtr<nsILayoutHistoryState> history = GetLayoutHistoryAndKey(aContent, false, key);

  if (history) {
    // Get the pres state for this key, if it doesn't exist, create one
    result = history->GetState(key, aPresState);
    if (!*aPresState) {
      *aPresState = new nsPresState();
      result = history->AddState(key, *aPresState);
    }
  }

  return result;
}


already_AddRefed<nsILayoutHistoryState>
nsGenericHTMLElement::GetLayoutHistoryAndKey(nsGenericHTMLElement* aContent,
                                             bool aRead,
                                             nsACString& aKey)
{
  //
  // Get the pres shell
  //
  nsCOMPtr<nsIDocument> doc = aContent->GetDocument();
  if (!doc) {
    return nullptr;
  }

  //
  // Get the history (don't bother with the key if the history is not there)
  //
  nsCOMPtr<nsILayoutHistoryState> history = doc->GetLayoutHistoryState();
  if (!history) {
    return nullptr;
  }

  if (aRead && !history->HasStates()) {
    return nullptr;
  }

  //
  // Get the state key
  //
  nsresult rv = nsContentUtils::GenerateStateKey(aContent, doc, aKey);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  // If the state key is blank, this is anonymous content or for
  // whatever reason we are not supposed to save/restore state.
  if (aKey.IsEmpty()) {
    return nullptr;
  }

  // Add something unique to content so layout doesn't muck us up
  aKey += "-C";

  return history.forget();
}

bool
nsGenericHTMLElement::RestoreFormControlState(nsGenericHTMLElement* aContent,
                                              nsIFormControl* aControl)
{
  nsAutoCString key;
  nsCOMPtr<nsILayoutHistoryState> history = GetLayoutHistoryAndKey(aContent, true, key);
  if (!history) {
    return false;
  }

  nsPresState *state;
  // Get the pres state for this key
  nsresult rv = history->GetState(key, &state);
  if (NS_SUCCEEDED(rv) && state) {
    bool result = aControl->RestoreState(state);
    history->RemoveState(key);
    return result;
  }

  return false;
}

// XXX This creates a dependency between content and frames
nsPresContext*
nsGenericHTMLElement::GetPresContext()
{
  // Get the document
  nsIDocument* doc = GetDocument();
  if (doc) {
    // Get presentation shell.
    nsIPresShell *presShell = doc->GetShell();
    if (presShell) {
      return presShell->GetPresContext();
    }
  }

  return nullptr;
}

static const nsAttrValue::EnumTable kAlignTable[] = {
  { "left",      NS_STYLE_TEXT_ALIGN_LEFT },
  { "right",     NS_STYLE_TEXT_ALIGN_RIGHT },

  { "top",       NS_STYLE_VERTICAL_ALIGN_TOP },
  { "middle",    NS_STYLE_VERTICAL_ALIGN_MIDDLE_WITH_BASELINE },
  { "bottom",    NS_STYLE_VERTICAL_ALIGN_BASELINE },

  { "center",    NS_STYLE_VERTICAL_ALIGN_MIDDLE_WITH_BASELINE },
  { "baseline",  NS_STYLE_VERTICAL_ALIGN_BASELINE },

  { "texttop",   NS_STYLE_VERTICAL_ALIGN_TEXT_TOP },
  { "absmiddle", NS_STYLE_VERTICAL_ALIGN_MIDDLE },
  { "abscenter", NS_STYLE_VERTICAL_ALIGN_MIDDLE },
  { "absbottom", NS_STYLE_VERTICAL_ALIGN_BOTTOM },
  { 0 }
};

static const nsAttrValue::EnumTable kDivAlignTable[] = {
  { "left", NS_STYLE_TEXT_ALIGN_MOZ_LEFT },
  { "right", NS_STYLE_TEXT_ALIGN_MOZ_RIGHT },
  { "center", NS_STYLE_TEXT_ALIGN_MOZ_CENTER },
  { "middle", NS_STYLE_TEXT_ALIGN_MOZ_CENTER },
  { "justify", NS_STYLE_TEXT_ALIGN_JUSTIFY },
  { 0 }
};

static const nsAttrValue::EnumTable kFrameborderTable[] = {
  { "yes", NS_STYLE_FRAME_YES },
  { "no", NS_STYLE_FRAME_NO },
  { "1", NS_STYLE_FRAME_1 },
  { "0", NS_STYLE_FRAME_0 },
  { 0 }
};

static const nsAttrValue::EnumTable kScrollingTable[] = {
  { "yes", NS_STYLE_FRAME_YES },
  { "no", NS_STYLE_FRAME_NO },
  { "on", NS_STYLE_FRAME_ON },
  { "off", NS_STYLE_FRAME_OFF },
  { "scroll", NS_STYLE_FRAME_SCROLL },
  { "noscroll", NS_STYLE_FRAME_NOSCROLL },
  { "auto", NS_STYLE_FRAME_AUTO },
  { 0 }
};

static const nsAttrValue::EnumTable kTableVAlignTable[] = {
  { "top",     NS_STYLE_VERTICAL_ALIGN_TOP },
  { "middle",  NS_STYLE_VERTICAL_ALIGN_MIDDLE },
  { "bottom",  NS_STYLE_VERTICAL_ALIGN_BOTTOM },
  { "baseline",NS_STYLE_VERTICAL_ALIGN_BASELINE },
  { 0 }
};

bool
nsGenericHTMLElement::ParseAlignValue(const nsAString& aString,
                                      nsAttrValue& aResult)
{
  return aResult.ParseEnumValue(aString, kAlignTable, false);
}

//----------------------------------------

static const nsAttrValue::EnumTable kTableHAlignTable[] = {
  { "left",   NS_STYLE_TEXT_ALIGN_LEFT },
  { "right",  NS_STYLE_TEXT_ALIGN_RIGHT },
  { "center", NS_STYLE_TEXT_ALIGN_CENTER },
  { "char",   NS_STYLE_TEXT_ALIGN_CHAR },
  { "justify",NS_STYLE_TEXT_ALIGN_JUSTIFY },
  { 0 }
};

bool
nsGenericHTMLElement::ParseTableHAlignValue(const nsAString& aString,
                                            nsAttrValue& aResult)
{
  return aResult.ParseEnumValue(aString, kTableHAlignTable, false);
}

//----------------------------------------

// This table is used for td, th, tr, col, thead, tbody and tfoot.
static const nsAttrValue::EnumTable kTableCellHAlignTable[] = {
  { "left",   NS_STYLE_TEXT_ALIGN_MOZ_LEFT },
  { "right",  NS_STYLE_TEXT_ALIGN_MOZ_RIGHT },
  { "center", NS_STYLE_TEXT_ALIGN_MOZ_CENTER },
  { "char",   NS_STYLE_TEXT_ALIGN_CHAR },
  { "justify",NS_STYLE_TEXT_ALIGN_JUSTIFY },
  { "middle", NS_STYLE_TEXT_ALIGN_MOZ_CENTER },
  { "absmiddle", NS_STYLE_TEXT_ALIGN_CENTER },
  { 0 }
};

bool
nsGenericHTMLElement::ParseTableCellHAlignValue(const nsAString& aString,
                                                nsAttrValue& aResult)
{
  return aResult.ParseEnumValue(aString, kTableCellHAlignTable, false);
}

//----------------------------------------

bool
nsGenericHTMLElement::ParseTableVAlignValue(const nsAString& aString,
                                            nsAttrValue& aResult)
{
  return aResult.ParseEnumValue(aString, kTableVAlignTable, false);
}

bool
nsGenericHTMLElement::ParseDivAlignValue(const nsAString& aString,
                                         nsAttrValue& aResult)
{
  return aResult.ParseEnumValue(aString, kDivAlignTable, false);
}

bool
nsGenericHTMLElement::ParseImageAttribute(nsIAtom* aAttribute,
                                          const nsAString& aString,
                                          nsAttrValue& aResult)
{
  if ((aAttribute == nsGkAtoms::width) ||
      (aAttribute == nsGkAtoms::height)) {
    return aResult.ParseSpecialIntValue(aString);
  }
  if ((aAttribute == nsGkAtoms::hspace) ||
      (aAttribute == nsGkAtoms::vspace) ||
      (aAttribute == nsGkAtoms::border)) {
    return aResult.ParseIntWithBounds(aString, 0);
  }
  return false;
}

bool
nsGenericHTMLElement::ParseFrameborderValue(const nsAString& aString,
                                            nsAttrValue& aResult)
{
  return aResult.ParseEnumValue(aString, kFrameborderTable, false);
}

bool
nsGenericHTMLElement::ParseScrollingValue(const nsAString& aString,
                                          nsAttrValue& aResult)
{
  return aResult.ParseEnumValue(aString, kScrollingTable, false);
}

/**
 * Handle attributes common to all html elements
 */
void
nsGenericHTMLElement::MapCommonAttributesExceptHiddenInto(const nsMappedAttributes* aAttributes,
                                                          nsRuleData* aData)
{
  if (aData->mSIDs & NS_STYLE_INHERIT_BIT(UserInterface)) {
    nsCSSValue* userModify = aData->ValueForUserModify();
    if (userModify->GetUnit() == eCSSUnit_Null) {
      const nsAttrValue* value =
        aAttributes->GetAttr(nsGkAtoms::contenteditable);
      if (value) {
        if (value->Equals(nsGkAtoms::_empty, eCaseMatters) ||
            value->Equals(nsGkAtoms::_true, eIgnoreCase)) {
          userModify->SetIntValue(NS_STYLE_USER_MODIFY_READ_WRITE,
                                  eCSSUnit_Enumerated);
        }
        else if (value->Equals(nsGkAtoms::_false, eIgnoreCase)) {
            userModify->SetIntValue(NS_STYLE_USER_MODIFY_READ_ONLY,
                                    eCSSUnit_Enumerated);
        }
      }
    }
  }

  if (aData->mSIDs & NS_STYLE_INHERIT_BIT(Font)) {
    const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::lang);
    if (value && value->Type() == nsAttrValue::eString) {
      aData->ValueForLang()->SetStringValue(value->GetStringValue(),
                                            eCSSUnit_Ident);
    }
  }
}

void
nsGenericHTMLElement::MapCommonAttributesInto(const nsMappedAttributes* aAttributes,
                                              nsRuleData* aData)
{
  nsGenericHTMLElement::MapCommonAttributesExceptHiddenInto(aAttributes, aData);

  if (aData->mSIDs & NS_STYLE_INHERIT_BIT(Display)) {
    nsCSSValue* display = aData->ValueForDisplay();
    if (display->GetUnit() == eCSSUnit_Null) {
      if (aAttributes->IndexOfAttr(nsGkAtoms::hidden) >= 0) {
        display->SetIntValue(NS_STYLE_DISPLAY_NONE, eCSSUnit_Enumerated);
      }
    }
  }
}


/* static */ const nsGenericHTMLElement::MappedAttributeEntry
nsGenericHTMLElement::sCommonAttributeMap[] = {
  { &nsGkAtoms::contenteditable },
  { &nsGkAtoms::lang },
  { &nsGkAtoms::hidden },
  { nullptr }
};

/* static */ const Element::MappedAttributeEntry
nsGenericHTMLElement::sImageMarginSizeAttributeMap[] = {
  { &nsGkAtoms::width },
  { &nsGkAtoms::height },
  { &nsGkAtoms::hspace },
  { &nsGkAtoms::vspace },
  { nullptr }
};

/* static */ const Element::MappedAttributeEntry
nsGenericHTMLElement::sImageAlignAttributeMap[] = {
  { &nsGkAtoms::align },
  { nullptr }
};

/* static */ const Element::MappedAttributeEntry
nsGenericHTMLElement::sDivAlignAttributeMap[] = {
  { &nsGkAtoms::align },
  { nullptr }
};

/* static */ const Element::MappedAttributeEntry
nsGenericHTMLElement::sImageBorderAttributeMap[] = {
  { &nsGkAtoms::border },
  { nullptr }
};

/* static */ const Element::MappedAttributeEntry
nsGenericHTMLElement::sBackgroundAttributeMap[] = {
  { &nsGkAtoms::background },
  { &nsGkAtoms::bgcolor },
  { nullptr }
};

/* static */ const Element::MappedAttributeEntry
nsGenericHTMLElement::sBackgroundColorAttributeMap[] = {
  { &nsGkAtoms::bgcolor },
  { nullptr }
};

/* static */ const Element::MappedAttributeEntry
nsGenericHTMLElement::sScrollingAttributeMap[] = {
  { &nsGkAtoms::scrolling },
  { nullptr }
};

void
nsGenericHTMLElement::MapImageAlignAttributeInto(const nsMappedAttributes* aAttributes,
                                                 nsRuleData* aRuleData)
{
  if (aRuleData->mSIDs & (NS_STYLE_INHERIT_BIT(Display) |
                          NS_STYLE_INHERIT_BIT(TextReset))) {
    const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::align);
    if (value && value->Type() == nsAttrValue::eEnum) {
      int32_t align = value->GetEnumValue();
      if (aRuleData->mSIDs & NS_STYLE_INHERIT_BIT(Display)) {
        nsCSSValue* cssFloat = aRuleData->ValueForCssFloat();
        if (cssFloat->GetUnit() == eCSSUnit_Null) {
          if (align == NS_STYLE_TEXT_ALIGN_LEFT) {
            cssFloat->SetIntValue(NS_STYLE_FLOAT_LEFT, eCSSUnit_Enumerated);
          } else if (align == NS_STYLE_TEXT_ALIGN_RIGHT) {
            cssFloat->SetIntValue(NS_STYLE_FLOAT_RIGHT, eCSSUnit_Enumerated);
          }
        }
      }
      if (aRuleData->mSIDs & NS_STYLE_INHERIT_BIT(TextReset)) {
        nsCSSValue* verticalAlign = aRuleData->ValueForVerticalAlign();
        if (verticalAlign->GetUnit() == eCSSUnit_Null) {
          switch (align) {
          case NS_STYLE_TEXT_ALIGN_LEFT:
          case NS_STYLE_TEXT_ALIGN_RIGHT:
            break;
          default:
            verticalAlign->SetIntValue(align, eCSSUnit_Enumerated);
            break;
          }
        }
      }
    }
  }
}

void
nsGenericHTMLElement::MapDivAlignAttributeInto(const nsMappedAttributes* aAttributes,
                                               nsRuleData* aRuleData)
{
  if (aRuleData->mSIDs & NS_STYLE_INHERIT_BIT(Text)) {
    nsCSSValue* textAlign = aRuleData->ValueForTextAlign();
    if (textAlign->GetUnit() == eCSSUnit_Null) {
      // align: enum
      const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::align);
      if (value && value->Type() == nsAttrValue::eEnum)
        textAlign->SetIntValue(value->GetEnumValue(), eCSSUnit_Enumerated);
    }
  }
}


void
nsGenericHTMLElement::MapImageMarginAttributeInto(const nsMappedAttributes* aAttributes,
                                                  nsRuleData* aData)
{
  if (!(aData->mSIDs & NS_STYLE_INHERIT_BIT(Margin)))
    return;

  const nsAttrValue* value;

  // hspace: value
  value = aAttributes->GetAttr(nsGkAtoms::hspace);
  if (value) {
    nsCSSValue hval;
    if (value->Type() == nsAttrValue::eInteger)
      hval.SetFloatValue((float)value->GetIntegerValue(), eCSSUnit_Pixel);
    else if (value->Type() == nsAttrValue::ePercent)
      hval.SetPercentValue(value->GetPercentValue());

    if (hval.GetUnit() != eCSSUnit_Null) {
      nsCSSValue* left = aData->ValueForMarginLeftValue();
      if (left->GetUnit() == eCSSUnit_Null)
        *left = hval;
      nsCSSValue* right = aData->ValueForMarginRightValue();
      if (right->GetUnit() == eCSSUnit_Null)
        *right = hval;
    }
  }

  // vspace: value
  value = aAttributes->GetAttr(nsGkAtoms::vspace);
  if (value) {
    nsCSSValue vval;
    if (value->Type() == nsAttrValue::eInteger)
      vval.SetFloatValue((float)value->GetIntegerValue(), eCSSUnit_Pixel);
    else if (value->Type() == nsAttrValue::ePercent)
      vval.SetPercentValue(value->GetPercentValue());
  
    if (vval.GetUnit() != eCSSUnit_Null) {
      nsCSSValue* top = aData->ValueForMarginTop();
      if (top->GetUnit() == eCSSUnit_Null)
        *top = vval;
      nsCSSValue* bottom = aData->ValueForMarginBottom();
      if (bottom->GetUnit() == eCSSUnit_Null)
        *bottom = vval;
    }
  }
}

void
nsGenericHTMLElement::MapImageSizeAttributesInto(const nsMappedAttributes* aAttributes,
                                                 nsRuleData* aData)
{
  if (!(aData->mSIDs & NS_STYLE_INHERIT_BIT(Position)))
    return;

  // width: value
  nsCSSValue* width = aData->ValueForWidth();
  if (width->GetUnit() == eCSSUnit_Null) {
    const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::width);
    if (value && value->Type() == nsAttrValue::eInteger)
      width->SetFloatValue((float)value->GetIntegerValue(), eCSSUnit_Pixel);
    else if (value && value->Type() == nsAttrValue::ePercent)
      width->SetPercentValue(value->GetPercentValue());
  }

  // height: value
  nsCSSValue* height = aData->ValueForHeight();
  if (height->GetUnit() == eCSSUnit_Null) {
    const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::height);
    if (value && value->Type() == nsAttrValue::eInteger)
      height->SetFloatValue((float)value->GetIntegerValue(), eCSSUnit_Pixel); 
    else if (value && value->Type() == nsAttrValue::ePercent)
      height->SetPercentValue(value->GetPercentValue());
  }
}

void
nsGenericHTMLElement::MapImageBorderAttributeInto(const nsMappedAttributes* aAttributes,
                                                  nsRuleData* aData)
{
  if (!(aData->mSIDs & NS_STYLE_INHERIT_BIT(Border)))
    return;

  // border: pixels
  const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::border);
  if (!value)
    return;
  
  nscoord val = 0;
  if (value->Type() == nsAttrValue::eInteger)
    val = value->GetIntegerValue();

  nsCSSValue* borderLeftWidth = aData->ValueForBorderLeftWidthValue();
  if (borderLeftWidth->GetUnit() == eCSSUnit_Null)
    borderLeftWidth->SetFloatValue((float)val, eCSSUnit_Pixel);
  nsCSSValue* borderTopWidth = aData->ValueForBorderTopWidth();
  if (borderTopWidth->GetUnit() == eCSSUnit_Null)
    borderTopWidth->SetFloatValue((float)val, eCSSUnit_Pixel);
  nsCSSValue* borderRightWidth = aData->ValueForBorderRightWidthValue();
  if (borderRightWidth->GetUnit() == eCSSUnit_Null)
    borderRightWidth->SetFloatValue((float)val, eCSSUnit_Pixel);
  nsCSSValue* borderBottomWidth = aData->ValueForBorderBottomWidth();
  if (borderBottomWidth->GetUnit() == eCSSUnit_Null)
    borderBottomWidth->SetFloatValue((float)val, eCSSUnit_Pixel);

  nsCSSValue* borderLeftStyle = aData->ValueForBorderLeftStyleValue();
  if (borderLeftStyle->GetUnit() == eCSSUnit_Null)
    borderLeftStyle->SetIntValue(NS_STYLE_BORDER_STYLE_SOLID, eCSSUnit_Enumerated);
  nsCSSValue* borderTopStyle = aData->ValueForBorderTopStyle();
  if (borderTopStyle->GetUnit() == eCSSUnit_Null)
    borderTopStyle->SetIntValue(NS_STYLE_BORDER_STYLE_SOLID, eCSSUnit_Enumerated);
  nsCSSValue* borderRightStyle = aData->ValueForBorderRightStyleValue();
  if (borderRightStyle->GetUnit() == eCSSUnit_Null)
    borderRightStyle->SetIntValue(NS_STYLE_BORDER_STYLE_SOLID, eCSSUnit_Enumerated);
  nsCSSValue* borderBottomStyle = aData->ValueForBorderBottomStyle();
  if (borderBottomStyle->GetUnit() == eCSSUnit_Null)
    borderBottomStyle->SetIntValue(NS_STYLE_BORDER_STYLE_SOLID, eCSSUnit_Enumerated);

  nsCSSValue* borderLeftColor = aData->ValueForBorderLeftColorValue();
  if (borderLeftColor->GetUnit() == eCSSUnit_Null)
    borderLeftColor->SetIntValue(NS_STYLE_COLOR_MOZ_USE_TEXT_COLOR, eCSSUnit_Enumerated);
  nsCSSValue* borderTopColor = aData->ValueForBorderTopColor();
  if (borderTopColor->GetUnit() == eCSSUnit_Null)
    borderTopColor->SetIntValue(NS_STYLE_COLOR_MOZ_USE_TEXT_COLOR, eCSSUnit_Enumerated);
  nsCSSValue* borderRightColor = aData->ValueForBorderRightColorValue();
  if (borderRightColor->GetUnit() == eCSSUnit_Null)
    borderRightColor->SetIntValue(NS_STYLE_COLOR_MOZ_USE_TEXT_COLOR, eCSSUnit_Enumerated);
  nsCSSValue* borderBottomColor = aData->ValueForBorderBottomColor();
  if (borderBottomColor->GetUnit() == eCSSUnit_Null)
    borderBottomColor->SetIntValue(NS_STYLE_COLOR_MOZ_USE_TEXT_COLOR, eCSSUnit_Enumerated);
}

void
nsGenericHTMLElement::MapBackgroundInto(const nsMappedAttributes* aAttributes,
                                        nsRuleData* aData)
{
  if (!(aData->mSIDs & NS_STYLE_INHERIT_BIT(Background)))
    return;

  nsPresContext* presContext = aData->mPresContext;
  nsCSSValue* backImage = aData->ValueForBackgroundImage();
  if (backImage->GetUnit() == eCSSUnit_Null &&
      presContext->UseDocumentColors()) {
    // background
    nsAttrValue* value =
      const_cast<nsAttrValue*>(aAttributes->GetAttr(nsGkAtoms::background));
    // If the value is an image, or it is a URL and we attempted a load,
    // put it in the style tree.
    if (value) {
      if (value->Type() == nsAttrValue::eURL) {
        value->LoadImage(presContext->Document());
      }
      if (value->Type() == nsAttrValue::eImage) {
        nsCSSValueList* list = backImage->SetListValue();
        list->mValue.SetImageValue(value->GetImageValue());
      }
    }
  }
}

void
nsGenericHTMLElement::MapBGColorInto(const nsMappedAttributes* aAttributes,
                                     nsRuleData* aData)
{
  if (!(aData->mSIDs & NS_STYLE_INHERIT_BIT(Background)))
    return;

  nsCSSValue* backColor = aData->ValueForBackgroundColor();
  if (backColor->GetUnit() == eCSSUnit_Null &&
      aData->mPresContext->UseDocumentColors()) {
    const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::bgcolor);
    nscolor color;
    if (value && value->GetColorValue(color)) {
      backColor->SetColorValue(color);
    }
  }
}

void
nsGenericHTMLElement::MapBackgroundAttributesInto(const nsMappedAttributes* aAttributes,
                                                  nsRuleData* aData)
{
  MapBackgroundInto(aAttributes, aData);
  MapBGColorInto(aAttributes, aData);
}

void
nsGenericHTMLElement::MapScrollingAttributeInto(const nsMappedAttributes* aAttributes,
                                                nsRuleData* aData)
{
  if (!(aData->mSIDs & NS_STYLE_INHERIT_BIT(Display)))
    return;

  // scrolling
  nsCSSValue* overflowValues[2] = {
    aData->ValueForOverflowX(),
    aData->ValueForOverflowY(),
  };
  for (uint32_t i = 0; i < ArrayLength(overflowValues); ++i) {
    if (overflowValues[i]->GetUnit() == eCSSUnit_Null) {
      const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::scrolling);
      if (value && value->Type() == nsAttrValue::eEnum) {
        int32_t mappedValue;
        switch (value->GetEnumValue()) {
          case NS_STYLE_FRAME_ON:
          case NS_STYLE_FRAME_SCROLL:
          case NS_STYLE_FRAME_YES:
            mappedValue = NS_STYLE_OVERFLOW_SCROLL;
            break;

          case NS_STYLE_FRAME_OFF:
          case NS_STYLE_FRAME_NOSCROLL:
          case NS_STYLE_FRAME_NO:
            mappedValue = NS_STYLE_OVERFLOW_HIDDEN;
            break;
        
          case NS_STYLE_FRAME_AUTO:
            mappedValue = NS_STYLE_OVERFLOW_AUTO;
            break;

          default:
            NS_NOTREACHED("unexpected value");
            mappedValue = NS_STYLE_OVERFLOW_AUTO;
            break;
        }
        overflowValues[i]->SetIntValue(mappedValue, eCSSUnit_Enumerated);
      }
    }
  }
}

//----------------------------------------------------------------------

nsresult
nsGenericHTMLElement::SetAttrHelper(nsIAtom* aAttr, const nsAString& aValue)
{
  return SetAttr(kNameSpaceID_None, aAttr, aValue, true);
}

nsresult
nsGenericHTMLElement::SetBoolAttr(nsIAtom* aAttr, bool aValue)
{
  if (aValue) {
    return SetAttr(kNameSpaceID_None, aAttr, EmptyString(), true);
  }

  return UnsetAttr(kNameSpaceID_None, aAttr, true);
}

int32_t
nsGenericHTMLElement::GetIntAttr(nsIAtom* aAttr, int32_t aDefault) const
{
  const nsAttrValue* attrVal = mAttrsAndChildren.GetAttr(aAttr);
  if (attrVal && attrVal->Type() == nsAttrValue::eInteger) {
    return attrVal->GetIntegerValue();
  }
  return aDefault;
}

nsresult
nsGenericHTMLElement::SetIntAttr(nsIAtom* aAttr, int32_t aValue)
{
  nsAutoString value;
  value.AppendInt(aValue);

  return SetAttr(kNameSpaceID_None, aAttr, value, true);
}

nsresult
nsGenericHTMLElement::GetUnsignedIntAttr(nsIAtom* aAttr, uint32_t aDefault,
                                         uint32_t* aResult)
{
  const nsAttrValue* attrVal = mAttrsAndChildren.GetAttr(aAttr);
  if (attrVal && attrVal->Type() == nsAttrValue::eInteger) {
    *aResult = attrVal->GetIntegerValue();
  }
  else {
    *aResult = aDefault;
  }
  return NS_OK;
}

nsresult
nsGenericHTMLElement::SetUnsignedIntAttr(nsIAtom* aAttr, uint32_t aValue)
{
  nsAutoString value;
  value.AppendInt(aValue);

  return SetAttr(kNameSpaceID_None, aAttr, value, true);
}

nsresult
nsGenericHTMLElement::SetDoubleAttr(nsIAtom* aAttr, double aValue)
{
  nsAutoString value;
  value.AppendFloat(aValue);

  return SetAttr(kNameSpaceID_None, aAttr, value, true);
}

void
nsGenericHTMLElement::GetURIAttr(nsIAtom* aAttr, nsIAtom* aBaseAttr,
                                 nsAString& aResult) const
{
  nsCOMPtr<nsIURI> uri;
  bool hadAttr = GetURIAttr(aAttr, aBaseAttr, getter_AddRefs(uri));
  if (!hadAttr) {
    aResult.Truncate();
    return;
  }

  if (!uri) {
    // Just return the attr value
    GetAttr(kNameSpaceID_None, aAttr, aResult);
    return;
  }

  nsAutoCString spec;
  uri->GetSpec(spec);
  CopyUTF8toUTF16(spec, aResult);
}

bool
nsGenericHTMLElement::GetURIAttr(nsIAtom* aAttr, nsIAtom* aBaseAttr, nsIURI** aURI) const
{
  *aURI = nullptr;

  const nsAttrValue* attr = mAttrsAndChildren.GetAttr(aAttr);
  if (!attr) {
    return false;
  }

  nsCOMPtr<nsIURI> baseURI = GetBaseURI();

  if (aBaseAttr) {
    nsAutoString baseAttrValue;
    if (GetAttr(kNameSpaceID_None, aBaseAttr, baseAttrValue)) {
      nsCOMPtr<nsIURI> baseAttrURI;
      nsresult rv =
        nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(baseAttrURI),
                                                  baseAttrValue, OwnerDoc(),
                                                  baseURI);
      if (NS_FAILED(rv)) {
        return true;
      }
      baseURI.swap(baseAttrURI);
    }
  }

  // Don't care about return value.  If it fails, we still want to
  // return true, and *aURI will be null.
  nsContentUtils::NewURIWithDocumentCharset(aURI,
                                            attr->GetStringValue(),
                                            OwnerDoc(), baseURI);
  return true;
}

nsresult
nsGenericHTMLElement::GetURIListAttr(nsIAtom* aAttr, nsAString& aResult)
{
  aResult.Truncate();

  nsAutoString value;
  if (!GetAttr(kNameSpaceID_None, aAttr, value))
    return NS_OK;

  nsIDocument* doc = OwnerDoc(); 
  nsCOMPtr<nsIURI> baseURI = GetBaseURI();

  // Value contains relative URIs split on spaces (U+0020)
  const PRUnichar *start = value.BeginReading();
  const PRUnichar *end   = value.EndReading();
  const PRUnichar *iter  = start;
  for (;;) {
    if (iter < end && *iter != ' ') {
      ++iter;
    } else {  // iter is pointing at either end or a space
      while (*start == ' ' && start < iter)
        ++start;
      if (iter != start) {
        if (!aResult.IsEmpty())
          aResult.Append(PRUnichar(' '));
        const nsSubstring& uriPart = Substring(start, iter);
        nsCOMPtr<nsIURI> attrURI;
        nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(attrURI),
                                                  uriPart, doc, baseURI);
        if (attrURI) {
          nsAutoCString spec;
          attrURI->GetSpec(spec);
          AppendUTF8toUTF16(spec, aResult);
        } else {
          aResult.Append(uriPart);
        }
      }
      start = iter = iter + 1;
      if (iter >= end)
        break;
    }
  }

  return NS_OK;
}

void
nsGenericHTMLElement::GetEnumAttr(nsIAtom* aAttr,
                                  const char* aDefault,
                                  nsAString& aResult) const
{
  const nsAttrValue* attrVal = mAttrsAndChildren.GetAttr(aAttr);

  aResult.Truncate();

  if (attrVal && attrVal->Type() == nsAttrValue::eEnum) {
    attrVal->GetEnumString(aResult, true);
  } else if (aDefault) {
    AppendASCIItoUTF16(nsDependentCString(aDefault), aResult);
  }
}

nsHTMLMenuElement*
nsGenericHTMLElement::GetContextMenu() const
{
  nsAutoString value;
  GetHTMLAttr(nsGkAtoms::contextmenu, value);
  if (!value.IsEmpty()) {
    nsIDocument* doc = GetCurrentDoc();
    if (doc) {
      return nsHTMLMenuElement::FromContentOrNull(doc->GetElementById(value));
    }
  }
  return nullptr;
}

void
nsGenericHTMLElement::GetContextMenu(nsIDOMHTMLMenuElement** aContextMenu) const
{
  NS_IF_ADDREF(*aContextMenu = GetContextMenu());
}

bool
nsGenericHTMLElement::IsLabelable() const
{
  return Tag() == nsGkAtoms::progress ||
         Tag() == nsGkAtoms::meter;
}

// static
bool
nsGenericHTMLElement::PrefEnabled()
{
  // This is a bit of a hack because we don't support non-bool prefs to enable
  // properties. This function will be called every time a HTMLElement interface
  // prototype object is created. The first time this is called we set a boolean
  // pref (dom.w3c_touch_events.expose) which controls the touch event
  // properties on HTMLElement.
  static bool sDidSetPref = false;
  if (!sDidSetPref) {
    sDidSetPref = true;
    Preferences::SetBool("dom.w3c_touch_events.expose",
                         nsDOMTouchEvent::PrefEnabled());
  }
  return true;
}

//----------------------------------------------------------------------

nsGenericHTMLFormElement::nsGenericHTMLFormElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : nsGenericHTMLElement(aNodeInfo)
  , mForm(nullptr)
  , mFieldSet(nullptr)
{
  // We should add the NS_EVENT_STATE_ENABLED bit here as needed, but
  // that depends on our type, which is not initialized yet.  So we
  // have to do this in subclasses.
}

nsGenericHTMLFormElement::~nsGenericHTMLFormElement()
{
  if (mFieldSet) {
    mFieldSet->RemoveElement(this);
  }

  // Check that this element doesn't know anything about its form at this point.
  NS_ASSERTION(!mForm, "mForm should be null at this point!");
}

NS_IMPL_QUERY_INTERFACE_INHERITED1(nsGenericHTMLFormElement,
                                   nsGenericHTMLElement,
                                   nsIFormControl)

bool
nsGenericHTMLFormElement::IsNodeOfType(uint32_t aFlags) const
{
  return !(aFlags & ~(eCONTENT | eHTML_FORM_CONTROL));
}

void
nsGenericHTMLFormElement::SaveSubtreeState()
{
  SaveState();

  nsGenericHTMLElement::SaveSubtreeState();
}

void
nsGenericHTMLFormElement::SetForm(nsIDOMHTMLFormElement* aForm)
{
  NS_PRECONDITION(aForm, "Don't pass null here");
  NS_ASSERTION(!mForm,
               "We don't support switching from one non-null form to another.");

  // keep a *weak* ref to the form here
  mForm = static_cast<nsHTMLFormElement*>(aForm);
}

void
nsGenericHTMLFormElement::ClearForm(bool aRemoveFromForm)
{
  NS_ASSERTION((mForm != nullptr) == HasFlag(ADDED_TO_FORM),
               "Form control should have had flag set correctly");

  if (!mForm) {
    return;
  }
  
  if (aRemoveFromForm) {
    nsAutoString nameVal, idVal;
    GetAttr(kNameSpaceID_None, nsGkAtoms::name, nameVal);
    GetAttr(kNameSpaceID_None, nsGkAtoms::id, idVal);

    mForm->RemoveElement(this, true);

    if (!nameVal.IsEmpty()) {
      mForm->RemoveElementFromTable(this, nameVal);
    }

    if (!idVal.IsEmpty()) {
      mForm->RemoveElementFromTable(this, idVal);
    }
  }

  UnsetFlags(ADDED_TO_FORM);
  mForm = nullptr;
}

Element*
nsGenericHTMLFormElement::GetFormElement()
{
  return mForm;
}

nsresult
nsGenericHTMLFormElement::GetForm(nsIDOMHTMLFormElement** aForm)
{
  NS_ENSURE_ARG_POINTER(aForm);
  NS_IF_ADDREF(*aForm = mForm);
  return NS_OK;
}

nsIContent::IMEState
nsGenericHTMLFormElement::GetDesiredIMEState()
{
  nsCOMPtr<nsIEditor> editor = nullptr;
  nsresult rv = GetEditorInternal(getter_AddRefs(editor));
  if (NS_FAILED(rv) || !editor)
    return nsGenericHTMLElement::GetDesiredIMEState();
  nsCOMPtr<nsIEditorIMESupport> imeEditor = do_QueryInterface(editor);
  if (!imeEditor)
    return nsGenericHTMLElement::GetDesiredIMEState();
  IMEState state;
  rv = imeEditor->GetPreferredIMEState(&state);
  if (NS_FAILED(rv))
    return nsGenericHTMLElement::GetDesiredIMEState();
  return state;
}

nsresult
nsGenericHTMLFormElement::BindToTree(nsIDocument* aDocument,
                                     nsIContent* aParent,
                                     nsIContent* aBindingParent,
                                     bool aCompileEventHandlers)
{
  nsresult rv = nsGenericHTMLElement::BindToTree(aDocument, aParent,
                                                 aBindingParent,
                                                 aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  // An autofocus event has to be launched if the autofocus attribute is
  // specified and the element accept the autofocus attribute. In addition,
  // the document should not be already loaded and the "browser.autofocus"
  // preference should be 'true'.
  if (IsAutofocusable() && HasAttr(kNameSpaceID_None, nsGkAtoms::autofocus) &&
      Preferences::GetBool("browser.autofocus", true)) {
    nsCOMPtr<nsIRunnable> event = new nsAutoFocusEvent(this);
    rv = NS_DispatchToCurrentThread(event);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // If @form is set, the element *has* to be in a document, otherwise it
  // wouldn't be possible to find an element with the corresponding id.
  // If @form isn't set, the element *has* to have a parent, otherwise it
  // wouldn't be possible to find a form ancestor.
  // We should not call UpdateFormOwner if none of these conditions are
  // fulfilled.
  if (HasAttr(kNameSpaceID_None, nsGkAtoms::form) ? !!GetCurrentDoc()
                                                  : !!aParent) {
    UpdateFormOwner(true, nullptr);
  }

  // Set parent fieldset which should be used for the disabled state.
  UpdateFieldSet(false);

  return NS_OK;
}

void
nsGenericHTMLFormElement::UnbindFromTree(bool aDeep, bool aNullParent)
{
  // Save state before doing anything
  SaveState();
  
  if (mForm) {
    // Might need to unset mForm
    if (aNullParent) {
      // No more parent means no more form
      ClearForm(true);
    } else {
      // Recheck whether we should still have an mForm.
      if (HasAttr(kNameSpaceID_None, nsGkAtoms::form) ||
          !FindAncestorForm(mForm)) {
        ClearForm(true);
      } else {
        UnsetFlags(MAYBE_ORPHAN_FORM_ELEMENT);
      }
    }

    if (!mForm) {
      // Our novalidate state might have changed
      UpdateState(false);
    }
  }

  // We have to remove the form id observer if there was one.
  // We will re-add one later if needed (during bind to tree).
  if (nsContentUtils::HasNonEmptyAttr(this, kNameSpaceID_None,
                                      nsGkAtoms::form)) {
    RemoveFormIdObserver();
  }

  nsGenericHTMLElement::UnbindFromTree(aDeep, aNullParent);

  // The element might not have a fieldset anymore.
  UpdateFieldSet(false);
}

nsresult
nsGenericHTMLFormElement::BeforeSetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                                        const nsAttrValueOrString* aValue,
                                        bool aNotify)
{
  if (aNameSpaceID == kNameSpaceID_None) {
    nsAutoString tmp;

    // remove the control from the hashtable as needed

    if (mForm && (aName == nsGkAtoms::name || aName == nsGkAtoms::id)) {
      GetAttr(kNameSpaceID_None, aName, tmp);

      if (!tmp.IsEmpty()) {
        mForm->RemoveElementFromTable(this, tmp);
      }
    }

    if (mForm && aName == nsGkAtoms::type) {
      GetAttr(kNameSpaceID_None, nsGkAtoms::name, tmp);

      if (!tmp.IsEmpty()) {
        mForm->RemoveElementFromTable(this, tmp);
      }

      GetAttr(kNameSpaceID_None, nsGkAtoms::id, tmp);

      if (!tmp.IsEmpty()) {
        mForm->RemoveElementFromTable(this, tmp);
      }

      mForm->RemoveElement(this, false);

      // Removing the element from the form can make it not be the default
      // control anymore.  Go ahead and notify on that change, though we might
      // end up readding and becoming the default control again in
      // AfterSetAttr.
      // FIXME: Bug 656197
      UpdateState(aNotify);
    }

    if (aName == nsGkAtoms::form) {
      // If @form isn't set or set to the empty string, there were no observer
      // so we don't have to remove it.
      if (nsContentUtils::HasNonEmptyAttr(this, kNameSpaceID_None,
                                          nsGkAtoms::form)) {
        // The current form id observer is no longer needed.
        // A new one may be added in AfterSetAttr.
        RemoveFormIdObserver();
      }
    }
  }

  return nsGenericHTMLElement::BeforeSetAttr(aNameSpaceID, aName,
                                             aValue, aNotify);
}

nsresult
nsGenericHTMLFormElement::AfterSetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                                       const nsAttrValue* aValue, bool aNotify)
{
  if (aNameSpaceID == kNameSpaceID_None) {
    // add the control to the hashtable as needed

    if (mForm && (aName == nsGkAtoms::name || aName == nsGkAtoms::id) &&
        aValue && !aValue->IsEmptyString()) {
      NS_ABORT_IF_FALSE(aValue->Type() == nsAttrValue::eAtom,
        "Expected atom value for name/id");
      mForm->AddElementToTable(this,
        nsDependentAtomString(aValue->GetAtomValue()));
    }

    if (mForm && aName == nsGkAtoms::type) {
      nsAutoString tmp;

      GetAttr(kNameSpaceID_None, nsGkAtoms::name, tmp);

      if (!tmp.IsEmpty()) {
        mForm->AddElementToTable(this, tmp);
      }

      GetAttr(kNameSpaceID_None, nsGkAtoms::id, tmp);

      if (!tmp.IsEmpty()) {
        mForm->AddElementToTable(this, tmp);
      }

      mForm->AddElement(this, false, aNotify);

      // Adding the element to the form can make it be the default control .
      // Go ahead and notify on that change.
      // Note: no need to notify on CanBeDisabled(), since type attr
      // changes can't affect that.
      UpdateState(aNotify);
    }

    if (aName == nsGkAtoms::form) {
      // We need a new form id observer.
      nsIDocument* doc = GetCurrentDoc();
      if (doc) {
        Element* formIdElement = nullptr;
        if (aValue && !aValue->IsEmptyString()) {
          formIdElement = AddFormIdObserver();
        }

        // Because we have a new @form value (or no more @form), we have to
        // update our form owner.
        UpdateFormOwner(false, formIdElement);
      }
    }
  }

  return nsGenericHTMLElement::AfterSetAttr(aNameSpaceID, aName,
                                            aValue, aNotify);
}

nsresult
nsGenericHTMLFormElement::PreHandleEvent(nsEventChainPreVisitor& aVisitor)
{
  if (aVisitor.mEvent->mFlags.mIsTrusted) {
    switch (aVisitor.mEvent->message) {
      case NS_FOCUS_CONTENT:
      {
        // Check to see if focus has bubbled up from a form control's
        // child textfield or button.  If that's the case, don't focus
        // this parent file control -- leave focus on the child.
        nsIFormControlFrame* formControlFrame = GetFormControlFrame(true);
        if (formControlFrame &&
            aVisitor.mEvent->originalTarget == static_cast<nsINode*>(this))
          formControlFrame->SetFocus(true, true);
        break;
      }
      case NS_BLUR_CONTENT:
      {
        nsIFormControlFrame* formControlFrame = GetFormControlFrame(true);
        if (formControlFrame)
          formControlFrame->SetFocus(false, false);
        break;
      }
    }
  }

  return nsGenericHTMLElement::PreHandleEvent(aVisitor);
}

/* virtual */
bool
nsGenericHTMLFormElement::IsDisabled() const
{
  return HasAttr(kNameSpaceID_None, nsGkAtoms::disabled) ||
         (mFieldSet && mFieldSet->IsDisabled());
}

void
nsGenericHTMLFormElement::ForgetFieldSet(nsIContent* aFieldset)
{
  if (mFieldSet == aFieldset) {
    mFieldSet = nullptr;
  }
}

bool
nsGenericHTMLFormElement::CanBeDisabled() const
{
  int32_t type = GetType();
  // It's easier to test the types that _cannot_ be disabled
  return
    type != NS_FORM_LABEL &&
    type != NS_FORM_OBJECT &&
    type != NS_FORM_OUTPUT;
}

bool
nsGenericHTMLFormElement::IsHTMLFocusable(bool aWithMouse,
                                          bool* aIsFocusable,
                                          int32_t* aTabIndex)
{
  if (nsGenericHTMLElement::IsHTMLFocusable(aWithMouse, aIsFocusable, aTabIndex)) {
    return true;
  }

#ifdef XP_MACOSX
  *aIsFocusable =
    (!aWithMouse || nsFocusManager::sMouseFocusesFormControl) && *aIsFocusable;
#endif
  return false;
}

nsEventStates
nsGenericHTMLFormElement::IntrinsicState() const
{
  // If you add attribute-dependent states here, you need to add them them to
  // AfterSetAttr too.  And add them to AfterSetAttr for all subclasses that
  // implement IntrinsicState() and are affected by that attribute.
  nsEventStates state = nsGenericHTMLElement::IntrinsicState();

  if (CanBeDisabled()) {
    // :enabled/:disabled
    if (IsDisabled()) {
      state |= NS_EVENT_STATE_DISABLED;
      state &= ~NS_EVENT_STATE_ENABLED;
    } else {
      state &= ~NS_EVENT_STATE_DISABLED;
      state |= NS_EVENT_STATE_ENABLED;
    }
  }
  
  if (mForm && mForm->IsDefaultSubmitElement(this)) {
      NS_ASSERTION(IsSubmitControl(),
                   "Default submit element that isn't a submit control.");
      // We are the default submit element (:default)
      state |= NS_EVENT_STATE_DEFAULT;
  }

  // Make the text controls read-write
  if (!state.HasState(NS_EVENT_STATE_MOZ_READWRITE) &&
      IsTextControl(false)) {
    bool roState = GetBoolAttr(nsGkAtoms::readonly);

    if (!roState) {
      state |= NS_EVENT_STATE_MOZ_READWRITE;
      state &= ~NS_EVENT_STATE_MOZ_READONLY;
    }
  }

  return state;
}

nsGenericHTMLFormElement::FocusTristate
nsGenericHTMLFormElement::FocusState()
{
  // We can't be focused if we aren't in a document
  nsIDocument* doc = GetCurrentDoc();
  if (!doc)
    return eUnfocusable;

  // first see if we are disabled or not. If disabled then do nothing.
  if (IsDisabled()) {
    return eUnfocusable;
  }

  // If the window is not active, do not allow the focus to bring the
  // window to the front.  We update the focus controller, but do
  // nothing else.
  nsPIDOMWindow* win = doc->GetWindow();
  if (win) {
    nsCOMPtr<nsIDOMWindow> rootWindow = do_QueryInterface(win->GetPrivateRoot());

    nsCOMPtr<nsIFocusManager> fm = do_GetService(FOCUSMANAGER_CONTRACTID);
    if (fm && rootWindow) {
      nsCOMPtr<nsIDOMWindow> activeWindow;
      fm->GetActiveWindow(getter_AddRefs(activeWindow));
      if (activeWindow == rootWindow) {
        return eActiveWindow;
      }
    }
  }

  return eInactiveWindow;
}

Element*
nsGenericHTMLFormElement::AddFormIdObserver()
{
  NS_ASSERTION(GetCurrentDoc(), "When adding a form id observer, "
                                "we should be in a document!");

  nsAutoString formId;
  nsIDocument* doc = OwnerDoc();
  GetAttr(kNameSpaceID_None, nsGkAtoms::form, formId);
  NS_ASSERTION(!formId.IsEmpty(),
               "@form value should not be the empty string!");
  nsCOMPtr<nsIAtom> atom = do_GetAtom(formId);

  return doc->AddIDTargetObserver(atom, FormIdUpdated, this, false);
}

void
nsGenericHTMLFormElement::RemoveFormIdObserver()
{
  /**
   * We are using OwnerDoc() because we don't really care about having the
   * element actually being in the tree. If it is not and @form value changes,
   * this method will be called for nothing but removing an observer which does
   * not exist doesn't cost so much (no entry in the hash table) so having a
   * boolean for GetCurrentDoc()/GetOwnerDoc() would make everything look more
   * complex for nothing.
   */

  nsIDocument* doc = OwnerDoc();

  // At this point, we may not have a document anymore. In that case, we can't
  // remove the observer. The document did that for us.
  if (!doc) {
    return;
  }

  nsAutoString formId;
  GetAttr(kNameSpaceID_None, nsGkAtoms::form, formId);
  NS_ASSERTION(!formId.IsEmpty(),
               "@form value should not be the empty string!");
  nsCOMPtr<nsIAtom> atom = do_GetAtom(formId);

  doc->RemoveIDTargetObserver(atom, FormIdUpdated, this, false);
}


/* static */
bool
nsGenericHTMLFormElement::FormIdUpdated(Element* aOldElement,
                                        Element* aNewElement,
                                        void* aData)
{
  nsGenericHTMLFormElement* element =
    static_cast<nsGenericHTMLFormElement*>(aData);

  NS_ASSERTION(element->IsHTML(), "aData should be an HTML element");

  element->UpdateFormOwner(false, aNewElement);

  return true;
}

bool 
nsGenericHTMLFormElement::IsElementDisabledForEvents(uint32_t aMessage, 
                                                    nsIFrame* aFrame)
{
  bool disabled = IsDisabled();
  if (!disabled && aFrame) {
    const nsStyleUserInterface* uiStyle = aFrame->GetStyleUserInterface();
    disabled = uiStyle->mUserInput == NS_STYLE_USER_INPUT_NONE ||
      uiStyle->mUserInput == NS_STYLE_USER_INPUT_DISABLED;

  }
  return disabled && aMessage != NS_MOUSE_MOVE;
}

void
nsGenericHTMLFormElement::UpdateFormOwner(bool aBindToTree,
                                          Element* aFormIdElement)
{
  NS_PRECONDITION(!aBindToTree || !aFormIdElement,
                  "aFormIdElement shouldn't be set if aBindToTree is true!");

  bool needStateUpdate = false;
  if (!aBindToTree) {
    needStateUpdate = mForm && mForm->IsDefaultSubmitElement(this);
    ClearForm(true);
  }

  nsHTMLFormElement *oldForm = mForm;

  if (!mForm) {
    // If @form is set, we have to use that to find the form.
    nsAutoString formId;
    if (GetAttr(kNameSpaceID_None, nsGkAtoms::form, formId)) {
      if (!formId.IsEmpty()) {
        Element* element = nullptr;

        if (aBindToTree) {
          element = AddFormIdObserver();
        } else {
          element = aFormIdElement;
        }

        NS_ASSERTION(GetCurrentDoc(), "The element should be in a document "
                                      "when UpdateFormOwner is called!");
        NS_ASSERTION(!GetCurrentDoc() ||
                     element == GetCurrentDoc()->GetElementById(formId),
                     "element should be equals to the current element "
                     "associated with the id in @form!");

        if (element && element->IsHTML(nsGkAtoms::form)) {
          mForm = static_cast<nsHTMLFormElement*>(element);
        }
      }
     } else {
      // We now have a parent, so we may have picked up an ancestor form.  Search
      // for it.  Note that if mForm is already set we don't want to do this,
      // because that means someone (probably the content sink) has already set
      // it to the right value.  Also note that even if being bound here didn't
      // change our parent, we still need to search, since our parent chain
      // probably changed _somewhere_.
      mForm = FindAncestorForm();
    }
  }

  if (mForm && !HasFlag(ADDED_TO_FORM)) {
    // Now we need to add ourselves to the form
    nsAutoString nameVal, idVal;
    GetAttr(kNameSpaceID_None, nsGkAtoms::name, nameVal);
    GetAttr(kNameSpaceID_None, nsGkAtoms::id, idVal);

    SetFlags(ADDED_TO_FORM);

    // Notify only if we just found this mForm.
    mForm->AddElement(this, true, oldForm == nullptr);

    if (!nameVal.IsEmpty()) {
      mForm->AddElementToTable(this, nameVal);
    }

    if (!idVal.IsEmpty()) {
      mForm->AddElementToTable(this, idVal);
    }
  }

  if (mForm != oldForm || needStateUpdate) {
    UpdateState(true);
  }
}

void
nsGenericHTMLFormElement::UpdateFieldSet(bool aNotify)
{
  nsIContent* parent = nullptr;
  nsIContent* prev = nullptr;

  for (parent = GetParent(); parent;
       prev = parent, parent = parent->GetParent()) {
    nsHTMLFieldSetElement* fieldset =
      nsHTMLFieldSetElement::FromContent(parent);
    if (fieldset &&
        (!prev || fieldset->GetFirstLegend() != prev)) {
      if (mFieldSet == fieldset) {
        // We already have the right fieldset;
        return;
      }

      if (mFieldSet) {
        mFieldSet->RemoveElement(this);
      }
      mFieldSet = fieldset;
      fieldset->AddElement(this);

      // The disabled state may have changed
      FieldSetDisabledChanged(aNotify);
      return;
    }
  }

  // No fieldset found.
  if (mFieldSet) {
    mFieldSet->RemoveElement(this);
    mFieldSet = nullptr;
    // The disabled state may have changed
    FieldSetDisabledChanged(aNotify);
  }
}

void
nsGenericHTMLFormElement::FieldSetDisabledChanged(bool aNotify)
{
  UpdateState(aNotify);
}

bool
nsGenericHTMLFormElement::IsLabelable() const
{
  // TODO: keygen should be in that list, see bug 101019.
  // TODO: NS_FORM_INPUT_HIDDEN should be removed, see bug 597650.
  uint32_t type = GetType();
  return type & NS_FORM_INPUT_ELEMENT ||
         type & NS_FORM_BUTTON_ELEMENT ||
         // type == NS_FORM_KEYGEN ||
         type == NS_FORM_OUTPUT ||
         type == NS_FORM_SELECT ||
         type == NS_FORM_TEXTAREA;
}

//----------------------------------------------------------------------

void
nsGenericHTMLElement::Blur(mozilla::ErrorResult& aError)
{
  if (!ShouldBlur(this)) {
    return;
  }

  nsIDocument* doc = GetCurrentDoc();
  if (!doc) {
    return;
  }

  nsIDOMWindow* win = doc->GetWindow();
  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  if (win && fm) {
    aError = fm->ClearFocus(win);
  }
}

void
nsGenericHTMLElement::Focus(ErrorResult& aError)
{
  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm) {
    nsCOMPtr<nsIDOMElement> elem = do_QueryInterface(this);
    aError = fm->SetFocus(elem, 0);
  }
}

void
nsGenericHTMLElement::Click()
{
  if (HasFlag(NODE_HANDLING_CLICK))
    return;

  // Strong in case the event kills it
  nsCOMPtr<nsIDocument> doc = GetCurrentDoc();

  nsCOMPtr<nsIPresShell> shell;
  nsRefPtr<nsPresContext> context;
  if (doc) {
    shell = doc->GetShell();
    if (shell) {
      context = shell->GetPresContext();
    }
  }

  SetFlags(NODE_HANDLING_CLICK);

  // Click() is never called from native code, but it may be
  // called from chrome JS. Mark this event trusted if Click()
  // is called from chrome code.
  nsMouseEvent event(nsContentUtils::IsCallerChrome(),
                     NS_MOUSE_CLICK, nullptr, nsMouseEvent::eReal);
  event.inputSource = nsIDOMMouseEvent::MOZ_SOURCE_UNKNOWN;

  nsEventDispatcher::Dispatch(this, context, &event);

  UnsetFlags(NODE_HANDLING_CLICK);
}

bool
nsGenericHTMLElement::IsHTMLFocusable(bool aWithMouse,
                                      bool *aIsFocusable,
                                      int32_t *aTabIndex)
{
  nsIDocument *doc = GetCurrentDoc();
  if (!doc || doc->HasFlag(NODE_IS_EDITABLE)) {
    // In designMode documents we only allow focusing the document.
    if (aTabIndex) {
      *aTabIndex = -1;
    }

    *aIsFocusable = false;

    return true;
  }

  int32_t tabIndex = TabIndex();

  bool override, disabled = false;
  if (IsEditableRoot()) {
    // Editable roots should always be focusable.
    override = true;

    // Ignore the disabled attribute in editable contentEditable/designMode
    // roots.
    if (!HasAttr(kNameSpaceID_None, nsGkAtoms::tabindex)) {
      // The default value for tabindex should be 0 for editable
      // contentEditable roots.
      tabIndex = 0;
    }
  }
  else {
    override = false;

    // Just check for disabled attribute on form controls
    disabled = IsDisabled();
    if (disabled) {
      tabIndex = -1;
    }
  }

  if (aTabIndex) {
    *aTabIndex = tabIndex;
  }

  // If a tabindex is specified at all, or the default tabindex is 0, we're focusable
  *aIsFocusable = 
    (tabIndex >= 0 || (!disabled && HasAttr(kNameSpaceID_None, nsGkAtoms::tabindex)));

  return override;
}

void
nsGenericHTMLElement::RegUnRegAccessKey(bool aDoReg)
{
  // first check to see if we have an access key
  nsAutoString accessKey;
  GetAttr(kNameSpaceID_None, nsGkAtoms::accesskey, accessKey);
  if (accessKey.IsEmpty()) {
    return;
  }

  // We have an access key, so get the ESM from the pres context.
  nsPresContext *presContext = GetPresContext();

  if (presContext) {
    nsEventStateManager *esm = presContext->EventStateManager();

    // Register or unregister as appropriate.
    if (aDoReg) {
      esm->RegisterAccessKey(this, (uint32_t)accessKey.First());
    } else {
      esm->UnregisterAccessKey(this, (uint32_t)accessKey.First());
    }
  }
}

void
nsGenericHTMLElement::PerformAccesskey(bool aKeyCausesActivation,
                                       bool aIsTrustedEvent)
{
  nsPresContext *presContext = GetPresContext();
  if (!presContext)
    return;

  // It's hard to say what HTML4 wants us to do in all cases.
  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm) {
    nsCOMPtr<nsIDOMElement> elem = do_QueryInterface(this);
    fm->SetFocus(elem, nsIFocusManager::FLAG_BYKEY);
  }

  if (aKeyCausesActivation) {
    // Click on it if the users prefs indicate to do so.
    nsMouseEvent event(aIsTrustedEvent, NS_MOUSE_CLICK,
                       nullptr, nsMouseEvent::eReal);
    event.inputSource = nsIDOMMouseEvent::MOZ_SOURCE_KEYBOARD;

    nsAutoPopupStatePusher popupStatePusher(aIsTrustedEvent ?
                                            openAllowed : openAbused);

    nsEventDispatcher::Dispatch(this, presContext, &event);
  }
}

const nsAttrName*
nsGenericHTMLElement::InternalGetExistingAttrNameFromQName(const nsAString& aStr) const
{
  if (IsInHTMLDocument()) {
    nsAutoString lower;
    nsContentUtils::ASCIIToLower(aStr, lower);
    return mAttrsAndChildren.GetExistingAttrNameFromQName(lower);
  }

  return mAttrsAndChildren.GetExistingAttrNameFromQName(aStr);
}

nsresult
nsGenericHTMLElement::GetEditor(nsIEditor** aEditor)
{
  *aEditor = nullptr;

  if (!nsContentUtils::IsCallerChrome()) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  return GetEditorInternal(aEditor);
}

nsresult
nsGenericHTMLElement::GetEditorInternal(nsIEditor** aEditor)
{
  *aEditor = nullptr;

  nsCOMPtr<nsITextControlElement> textCtrl = do_QueryInterface(this);
  if (textCtrl) {
    *aEditor = textCtrl->GetTextEditor();
    NS_IF_ADDREF(*aEditor);
  }

  return NS_OK;
}

already_AddRefed<nsIEditor>
nsGenericHTMLElement::GetAssociatedEditor()
{
  // If contenteditable is ever implemented, it might need to do something different here?

  nsIEditor* editor = nullptr;
  GetEditorInternal(&editor);
  return editor;
}

bool
nsGenericHTMLElement::IsCurrentBodyElement()
{
  // TODO Bug 698498: Should this handle the case where GetBody returns a
  //                  frameset?
  nsCOMPtr<nsIDOMHTMLBodyElement> bodyElement = do_QueryInterface(this);
  if (!bodyElement) {
    return false;
  }

  nsCOMPtr<nsIDOMHTMLDocument> htmlDocument =
    do_QueryInterface(GetCurrentDoc());
  if (!htmlDocument) {
    return false;
  }

  nsCOMPtr<nsIDOMHTMLElement> htmlElement;
  htmlDocument->GetBody(getter_AddRefs(htmlElement));
  return htmlElement == bodyElement;
}

// static
void
nsGenericHTMLElement::SyncEditorsOnSubtree(nsIContent* content)
{
  /* Sync this node */
  nsGenericHTMLElement* element = FromContent(content);
  if (element) {
    nsCOMPtr<nsIEditor> editor = element->GetAssociatedEditor();
    if (editor) {
      editor->SyncRealTimeSpell();
    }
  }

  /* Sync all children */
  for (nsIContent* child = content->GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    SyncEditorsOnSubtree(child);
  }
}

void
nsGenericHTMLElement::RecompileScriptEventListeners()
{
    int32_t i, count = mAttrsAndChildren.AttrCount();
    for (i = 0; i < count; ++i) {
        const nsAttrName *name = mAttrsAndChildren.AttrNameAt(i);

        // Eventlistenener-attributes are always in the null namespace
        if (!name->IsAtom()) {
            continue;
        }

        nsIAtom *attr = name->Atom();
        if (!nsContentUtils::IsEventAttributeName(attr, EventNameType_HTML)) {
            continue;
        }

        nsAutoString value;
        GetAttr(kNameSpaceID_None, attr, value);
        SetEventHandler(attr, value, true);
    }
}

bool
nsGenericHTMLElement::IsEditableRoot() const
{
  nsIDocument *document = GetCurrentDoc();
  if (!document) {
    return false;
  }

  if (document->HasFlag(NODE_IS_EDITABLE)) {
    return false;
  }

  if (GetContentEditableValue() != eTrue) {
    return false;
  }

  nsIContent *parent = GetParent();

  return !parent || !parent->HasFlag(NODE_IS_EDITABLE);
}

static void
MakeContentDescendantsEditable(nsIContent *aContent, nsIDocument *aDocument)
{
  // If aContent is not an element, we just need to update its
  // internal editable state and don't need to notify anyone about
  // that.  For elements, we need to send a ContentStateChanged
  // notification.
  if (!aContent->IsElement()) {
    aContent->UpdateEditableState(false);
    return;
  }

  Element *element = aContent->AsElement();

  element->UpdateEditableState(true);

  for (nsIContent *child = aContent->GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    if (!child->HasAttr(kNameSpaceID_None, nsGkAtoms::contenteditable)) {
      MakeContentDescendantsEditable(child, aDocument);
    }
  }
}

void
nsGenericHTMLElement::ChangeEditableState(int32_t aChange)
{
  nsIDocument* document = GetCurrentDoc();
  if (!document) {
    return;
  }

  if (aChange != 0) {
    nsCOMPtr<nsIHTMLDocument> htmlDocument =
      do_QueryInterface(document);
    if (htmlDocument) {
      htmlDocument->ChangeContentEditableCount(this, aChange);
    }
  }

  if (document->HasFlag(NODE_IS_EDITABLE)) {
    document = nullptr;
  }

  // MakeContentDescendantsEditable is going to call ContentStateChanged for
  // this element and all descendants if editable state has changed.
  // We might as well wrap it all in one script blocker.
  nsAutoScriptBlocker scriptBlocker;
  MakeContentDescendantsEditable(this, document);
}

JS::Value
nsGenericHTMLElement::GetItemValue(JSContext* aCx, JSObject* aScope,
                                   ErrorResult& aError)
{
  if (!HasAttr(kNameSpaceID_None, nsGkAtoms::itemprop)) {
    return JS::NullValue();
  }

  if (ItemScope()) {
    JS::Value v;
    if (!mozilla::dom::WrapObject(aCx, aScope, this, &v)) {
      aError.Throw(NS_ERROR_FAILURE);
      return JS::UndefinedValue();
    }
    return v;
  }

  nsString string;
  GetItemValueText(string);
  JS::Value v;
  if (!xpc::NonVoidStringToJsval(aCx, string, &v)) {
    aError.Throw(NS_ERROR_FAILURE);
    return JS::UndefinedValue();
  }
  return v;
}

NS_IMETHODIMP
nsGenericHTMLElement::GetItemValue(nsIVariant** aValue)
{
  nsCOMPtr<nsIWritableVariant> out = new nsVariant();

  if (!HasAttr(kNameSpaceID_None, nsGkAtoms::itemprop)) {
    out->SetAsEmpty();
    out.forget(aValue);
    return NS_OK;
  }

  if (ItemScope()) {
    out->SetAsISupports(static_cast<nsISupports*>(this));
  } else {
    nsAutoString string;
    GetItemValueText(string);
    out->SetAsAString(string);
  }

  out.forget(aValue);
  return NS_OK;
}

void
nsGenericHTMLElement::SetItemValue(JSContext* aCx, JS::Value aValue,
                                   ErrorResult& aError)
{
  if (!HasAttr(kNameSpaceID_None, nsGkAtoms::itemprop) ||
      HasAttr(kNameSpaceID_None, nsGkAtoms::itemscope)) {
    aError.Throw(NS_ERROR_DOM_INVALID_ACCESS_ERR);
    return;
  }

  FakeDependentString string;
  if (!ConvertJSValueToString(aCx, aValue, &aValue, eStringify, eStringify, string)) {
    aError.Throw(NS_ERROR_UNEXPECTED);
    return;
  }
  SetItemValueText(string);
}

NS_IMETHODIMP
nsGenericHTMLElement::SetItemValue(nsIVariant* aValue)
{
  if (!HasAttr(kNameSpaceID_None, nsGkAtoms::itemprop) ||
      HasAttr(kNameSpaceID_None, nsGkAtoms::itemscope)) {
    return NS_ERROR_DOM_INVALID_ACCESS_ERR;
  }

  nsAutoString string;
  aValue->GetAsAString(string);
  SetItemValueText(string);
  return NS_OK;
}

void
nsGenericHTMLElement::GetItemValueText(nsAString& text)
{
  GetTextContentInternal(text);
}

void
nsGenericHTMLElement::SetItemValueText(const nsAString& text)
{
  mozilla::ErrorResult rv;
  SetTextContentInternal(text, rv);
}

static void
nsDOMSettableTokenListPropertyDestructor(void *aObject, nsIAtom *aProperty,
                                         void *aPropertyValue, void *aData)
{
  nsDOMSettableTokenList* list =
    static_cast<nsDOMSettableTokenList*>(aPropertyValue);
  list->DropReference();
  NS_RELEASE(list);
}

nsDOMSettableTokenList*
nsGenericHTMLElement::GetTokenList(nsIAtom* aAtom)
{
  nsDOMSettableTokenList* list = NULL;
  if (HasProperties()) {
    list = static_cast<nsDOMSettableTokenList*>(GetProperty(aAtom));
  }
  if (!list) {
    list = new nsDOMSettableTokenList(this, aAtom);
    NS_ADDREF(list);
    SetProperty(aAtom, list, nsDOMSettableTokenListPropertyDestructor);
  }                       
  return list;
}  

void
nsGenericHTMLElement::GetTokenList(nsIAtom* aAtom, nsIVariant** aResult)
{
  nsIDOMDOMSettableTokenList* itemType = GetTokenList(aAtom);
  nsCOMPtr<nsIWritableVariant> out = new nsVariant();
  out->SetAsInterface(NS_GET_IID(nsIDOMDOMSettableTokenList), itemType);
  out.forget(aResult);
}

nsresult
nsGenericHTMLElement::SetTokenList(nsIAtom* aAtom, nsIVariant* aValue)
{
  nsDOMSettableTokenList* itemType = GetTokenList(aAtom);
  nsAutoString string;
  aValue->GetAsAString(string);
  return itemType->SetValue(string);
}

static void
HTMLPropertiesCollectionDestructor(void *aObject, nsIAtom *aProperty,
                                   void *aPropertyValue, void *aData)
{
  HTMLPropertiesCollection* properties = 
    static_cast<HTMLPropertiesCollection*>(aPropertyValue);
  NS_IF_RELEASE(properties);
}

HTMLPropertiesCollection*
nsGenericHTMLElement::Properties()
{
  HTMLPropertiesCollection* properties = 
    static_cast<HTMLPropertiesCollection*>(GetProperty(nsGkAtoms::microdataProperties));
  if (!properties) {
     properties = new HTMLPropertiesCollection(this);
     NS_ADDREF(properties);
     SetProperty(nsGkAtoms::microdataProperties, properties, HTMLPropertiesCollectionDestructor);
  }
  return properties;
}

void
nsGenericHTMLElement::GetProperties(nsISupports** aProperties)
{
  NS_ADDREF(*aProperties = static_cast<nsIHTMLCollection*>(Properties()));
}

nsSize
nsGenericHTMLElement::GetWidthHeightForImage(imgIRequest *aImageRequest)
{
  nsSize size(0,0);

  nsIFrame* frame = GetPrimaryFrame(Flush_Layout);

  if (frame) {
    size = frame->GetContentRect().Size();

    size.width = nsPresContext::AppUnitsToIntCSSPixels(size.width);
    size.height = nsPresContext::AppUnitsToIntCSSPixels(size.height);
  } else {
    const nsAttrValue* value;
    nsCOMPtr<imgIContainer> image;
    if (aImageRequest) {
      aImageRequest->GetImage(getter_AddRefs(image));
    }

    if ((value = GetParsedAttr(nsGkAtoms::width)) &&
        value->Type() == nsAttrValue::eInteger) {
      size.width = value->GetIntegerValue();
    } else if (image) {
      image->GetWidth(&size.width);
    }

    if ((value = GetParsedAttr(nsGkAtoms::height)) &&
        value->Type() == nsAttrValue::eInteger) {
      size.height = value->GetIntegerValue();
    } else if (image) {
      image->GetHeight(&size.height);
    }
  }

  NS_ASSERTION(size.width >= 0, "negative width");
  NS_ASSERTION(size.height >= 0, "negative height");
  return size;
}
