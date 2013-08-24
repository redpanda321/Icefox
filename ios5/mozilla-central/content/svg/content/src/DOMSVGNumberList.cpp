/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsSVGElement.h"
#include "DOMSVGNumberList.h"
#include "DOMSVGNumber.h"
#include "nsDOMError.h"
#include "SVGAnimatedNumberList.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "dombindings.h"

// See the comment in this file's header.

namespace mozilla {

// local helper functions
namespace {

using mozilla::DOMSVGNumber;

void UpdateListIndicesFromIndex(nsTArray<DOMSVGNumber*>& aItemsArray,
                                PRUint32 aStartingIndex)
{
  PRUint32 length = aItemsArray.Length();

  for (PRUint32 i = aStartingIndex; i < length; ++i) {
    if (aItemsArray[i]) {
      aItemsArray[i]->UpdateListIndex(i);
    }
  }
}

} // namespace

// We could use NS_IMPL_CYCLE_COLLECTION_1, except that in Unlink() we need to
// clear our DOMSVGAnimatedNumberList's weak ref to us to be safe. (The other
// option would be to not unlink and rely on the breaking of the other edges in
// the cycle, as NS_SVG_VAL_IMPL_CYCLE_COLLECTION does.)
NS_IMPL_CYCLE_COLLECTION_CLASS(DOMSVGNumberList)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(DOMSVGNumberList)
  // No need to null check tmp - script/SMIL can't detach us from mAList
  ( tmp->IsAnimValList() ? tmp->mAList->mAnimVal : tmp->mAList->mBaseVal ) = nsnull;
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mAList)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(DOMSVGNumberList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mAList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(DOMSVGNumberList)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(DOMSVGNumberList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DOMSVGNumberList)

} // namespace mozilla
DOMCI_DATA(SVGNumberList, mozilla::DOMSVGNumberList)
namespace mozilla {

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DOMSVGNumberList)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGNumberList)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGNumberList)
NS_INTERFACE_MAP_END


JSObject*
DOMSVGNumberList::WrapObject(JSContext *cx, JSObject *scope, bool *triedToWrap)
{
  return mozilla::dom::binding::SVGNumberList::create(cx, scope, this,
                                                      triedToWrap);
}

nsIDOMSVGNumber*
DOMSVGNumberList::GetItemAt(PRUint32 aIndex)
{
  if (IsAnimValList()) {
    Element()->FlushAnimations();
  }
  if (aIndex < Length()) {
    EnsureItemAt(aIndex);
    return mItems[aIndex];
  }
  return nsnull;
}

void
DOMSVGNumberList::InternalListLengthWillChange(PRUint32 aNewLength)
{
  PRUint32 oldLength = mItems.Length();

  if (aNewLength > DOMSVGNumber::MaxListIndex()) {
    // It's safe to get out of sync with our internal list as long as we have
    // FEWER items than it does.
    aNewLength = DOMSVGNumber::MaxListIndex();
  }

  nsRefPtr<DOMSVGNumberList> kungFuDeathGrip;
  if (aNewLength < oldLength) {
    // RemovingFromList() might clear last reference to |this|.
    // Retain a temporary reference to keep from dying before returning.
    kungFuDeathGrip = this;
  }

  // If our length will decrease, notify the items that will be removed:
  for (PRUint32 i = aNewLength; i < oldLength; ++i) {
    if (mItems[i]) {
      mItems[i]->RemovingFromList();
    }
  }

  if (!mItems.SetLength(aNewLength)) {
    // We silently ignore SetLength OOM failure since being out of sync is safe
    // so long as we have *fewer* items than our internal list.
    mItems.Clear();
    return;
  }

  // If our length has increased, null out the new pointers:
  for (PRUint32 i = oldLength; i < aNewLength; ++i) {
    mItems[i] = nsnull;
  }
}

SVGNumberList&
DOMSVGNumberList::InternalList() const
{
  SVGAnimatedNumberList *alist = Element()->GetAnimatedNumberList(AttrEnum());
  return IsAnimValList() && alist->mAnimVal ? *alist->mAnimVal : alist->mBaseVal;
}

// ----------------------------------------------------------------------------
// nsIDOMSVGNumberList implementation:

NS_IMETHODIMP
DOMSVGNumberList::GetNumberOfItems(PRUint32 *aNumberOfItems)
{
  if (IsAnimValList()) {
    Element()->FlushAnimations();
  }
  *aNumberOfItems = Length();
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGNumberList::Clear()
{
  if (IsAnimValList()) {
    return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
  }

  if (Length() > 0) {
    nsAttrValue emptyOrOldValue = Element()->WillChangeNumberList(AttrEnum());
    // Notify any existing DOM items of removal *before* truncating the lists
    // so that they can find their SVGNumber internal counterparts and copy
    // their values. This also notifies the animVal list:
    mAList->InternalBaseValListWillChangeTo(SVGNumberList());

    mItems.Clear();
    InternalList().Clear();
    Element()->DidChangeNumberList(AttrEnum(), emptyOrOldValue);
    if (mAList->IsAnimating()) {
      Element()->AnimationNeedsResample();
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGNumberList::Initialize(nsIDOMSVGNumber *newItem,
                             nsIDOMSVGNumber **_retval)
{
  *_retval = nsnull;
  if (IsAnimValList()) {
    return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
  }

  // If newItem is already in a list we should insert a clone of newItem, and
  // for consistency, this should happen even if *this* is the list that
  // newItem is currently in. Note that in the case of newItem being in this
  // list, the Clear() call before the InsertItemBefore() call would remove it
  // from this list, and so the InsertItemBefore() call would not insert a
  // clone of newItem, it would actually insert newItem. To prevent that from
  // happening we have to do the clone here, if necessary.

  nsCOMPtr<DOMSVGNumber> domItem = do_QueryInterface(newItem);
  if (!domItem) {
    return NS_ERROR_DOM_SVG_WRONG_TYPE_ERR;
  }
  if (domItem->HasOwner()) {
    newItem = domItem->Clone();
  }

  Clear();
  return InsertItemBefore(newItem, 0, _retval);
}

NS_IMETHODIMP
DOMSVGNumberList::GetItem(PRUint32 index,
                          nsIDOMSVGNumber **_retval)
{
  *_retval = GetItemAt(index);
  if (!*_retval) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }
  NS_ADDREF(*_retval);
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGNumberList::InsertItemBefore(nsIDOMSVGNumber *newItem,
                                   PRUint32 index,
                                   nsIDOMSVGNumber **_retval)
{
  *_retval = nsnull;
  if (IsAnimValList()) {
    return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
  }

  index = NS_MIN(index, Length());
  if (index >= DOMSVGNumber::MaxListIndex()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  nsCOMPtr<DOMSVGNumber> domItem = do_QueryInterface(newItem);
  if (!domItem) {
    return NS_ERROR_DOM_SVG_WRONG_TYPE_ERR;
  }
  if (domItem->HasOwner()) {
    domItem = domItem->Clone(); // must do this before changing anything!
  }

  // Ensure we have enough memory so we can avoid complex error handling below:
  if (!mItems.SetCapacity(mItems.Length() + 1) ||
      !InternalList().SetCapacity(InternalList().Length() + 1)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsAttrValue emptyOrOldValue = Element()->WillChangeNumberList(AttrEnum());
  // Now that we know we're inserting, keep animVal list in sync as necessary.
  MaybeInsertNullInAnimValListAt(index);

  InternalList().InsertItem(index, domItem->ToSVGNumber());
  mItems.InsertElementAt(index, domItem.get());

  // This MUST come after the insertion into InternalList(), or else under the
  // insertion into InternalList() the values read from domItem would be bad
  // data from InternalList() itself!:
  domItem->InsertingIntoList(this, AttrEnum(), index, IsAnimValList());

  UpdateListIndicesFromIndex(mItems, index + 1);

  Element()->DidChangeNumberList(AttrEnum(), emptyOrOldValue);
  if (mAList->IsAnimating()) {
    Element()->AnimationNeedsResample();
  }
  *_retval = domItem.forget().get();
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGNumberList::ReplaceItem(nsIDOMSVGNumber *newItem,
                              PRUint32 index,
                              nsIDOMSVGNumber **_retval)
{
  *_retval = nsnull;
  if (IsAnimValList()) {
    return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
  }

  nsCOMPtr<DOMSVGNumber> domItem = do_QueryInterface(newItem);
  if (!domItem) {
    return NS_ERROR_DOM_SVG_WRONG_TYPE_ERR;
  }
  if (index >= Length()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }
  if (domItem->HasOwner()) {
    domItem = domItem->Clone(); // must do this before changing anything!
  }

  nsAttrValue emptyOrOldValue = Element()->WillChangeNumberList(AttrEnum());
  if (mItems[index]) {
    // Notify any existing DOM item of removal *before* modifying the lists so
    // that the DOM item can copy the *old* value at its index:
    mItems[index]->RemovingFromList();
  }

  InternalList()[index] = domItem->ToSVGNumber();
  mItems[index] = domItem;

  // This MUST come after the ToSVGPoint() call, otherwise that call
  // would end up reading bad data from InternalList()!
  domItem->InsertingIntoList(this, AttrEnum(), index, IsAnimValList());

  Element()->DidChangeNumberList(AttrEnum(), emptyOrOldValue);
  if (mAList->IsAnimating()) {
    Element()->AnimationNeedsResample();
  }
  NS_ADDREF(*_retval = domItem.get());
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGNumberList::RemoveItem(PRUint32 index,
                             nsIDOMSVGNumber **_retval)
{
  *_retval = nsnull;
  if (IsAnimValList()) {
    return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
  }

  if (index >= Length()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  // Now that we know we're removing, keep animVal list in sync as necessary.
  // Do this *before* touching InternalList() so the removed item can get its
  // internal value.
  MaybeRemoveItemFromAnimValListAt(index);

  // We have to return the removed item, so make sure it exists:
  EnsureItemAt(index);

  nsAttrValue emptyOrOldValue = Element()->WillChangeNumberList(AttrEnum());
  // Notify the DOM item of removal *before* modifying the lists so that the
  // DOM item can copy its *old* value:
  mItems[index]->RemovingFromList();
  NS_ADDREF(*_retval = mItems[index]);

  InternalList().RemoveItem(index);
  mItems.RemoveElementAt(index);

  UpdateListIndicesFromIndex(mItems, index);

  Element()->DidChangeNumberList(AttrEnum(), emptyOrOldValue);
  if (mAList->IsAnimating()) {
    Element()->AnimationNeedsResample();
  }
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGNumberList::AppendItem(nsIDOMSVGNumber *newItem,
                             nsIDOMSVGNumber **_retval)
{
  return InsertItemBefore(newItem, Length(), _retval);
}

NS_IMETHODIMP
DOMSVGNumberList::GetLength(PRUint32 *aNumberOfItems)
{
  return GetNumberOfItems(aNumberOfItems);
}

void
DOMSVGNumberList::EnsureItemAt(PRUint32 aIndex)
{
  if (!mItems[aIndex]) {
    mItems[aIndex] = new DOMSVGNumber(this, AttrEnum(), aIndex, IsAnimValList());
  }
}

void
DOMSVGNumberList::MaybeInsertNullInAnimValListAt(PRUint32 aIndex)
{
  NS_ABORT_IF_FALSE(!IsAnimValList(), "call from baseVal to animVal");

  DOMSVGNumberList* animVal = mAList->mAnimVal;

  if (!animVal || mAList->IsAnimating()) {
    // No animVal list wrapper, or animVal not a clone of baseVal
    return;
  }

  NS_ABORT_IF_FALSE(animVal->mItems.Length() == mItems.Length(),
                    "animVal list not in sync!");

  animVal->mItems.InsertElementAt(aIndex, static_cast<DOMSVGNumber*>(nsnull));

  UpdateListIndicesFromIndex(animVal->mItems, aIndex + 1);
}

void
DOMSVGNumberList::MaybeRemoveItemFromAnimValListAt(PRUint32 aIndex)
{
  NS_ABORT_IF_FALSE(!IsAnimValList(), "call from baseVal to animVal");

  // This needs to be a strong reference; otherwise, the RemovingFromList call
  // below might drop the last reference to animVal before we're done with it.
  nsRefPtr<DOMSVGNumberList> animVal = mAList->mAnimVal;

  if (!animVal || mAList->IsAnimating()) {
    // No animVal list wrapper, or animVal not a clone of baseVal
    return;
  }

  NS_ABORT_IF_FALSE(animVal->mItems.Length() == mItems.Length(),
                    "animVal list not in sync!");

  if (animVal->mItems[aIndex]) {
    animVal->mItems[aIndex]->RemovingFromList();
  }
  animVal->mItems.RemoveElementAt(aIndex);

  UpdateListIndicesFromIndex(animVal->mItems, aIndex);
}

} // namespace mozilla
