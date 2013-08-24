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
 * The Original Code is Mozilla SVG Project code.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsSVGElement.h"
#include "DOMSVGLengthList.h"
#include "DOMSVGLength.h"
#include "nsDOMError.h"
#include "SVGAnimatedLengthList.h"
#include "nsCOMPtr.h"

// See the comment in this file's header.

namespace mozilla {

// We could use NS_IMPL_CYCLE_COLLECTION_1, except that in Unlink() we need to
// clear our DOMSVGAnimatedLengthList's weak ref to us to be safe. (The other
// option would be to not unlink and rely on the breaking of the other edges in
// the cycle, as NS_SVG_VAL_IMPL_CYCLE_COLLECTION does.)
NS_IMPL_CYCLE_COLLECTION_CLASS(DOMSVGLengthList)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(DOMSVGLengthList)
  // No need to null check tmp - script/SMIL can't detach us from mAList
  ( tmp->IsAnimValList() ? tmp->mAList->mAnimVal : tmp->mAList->mBaseVal ) = nsnull;
NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mAList)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(DOMSVGLengthList)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mAList)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(DOMSVGLengthList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DOMSVGLengthList)

}
DOMCI_DATA(SVGLengthList, mozilla::DOMSVGLengthList)
namespace mozilla {

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DOMSVGLengthList)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGLengthList)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGLengthList)
NS_INTERFACE_MAP_END


void
DOMSVGLengthList::InternalListLengthWillChange(PRUint32 aNewLength)
{
  PRUint32 oldLength = mItems.Length();

  // If our length will decrease, notify the items that will be removed:
  for (PRUint32 i = aNewLength; i < oldLength; ++i) {
    if (mItems[i]) {
      mItems[i]->RemovingFromList();
    }
  }

  if (!mItems.SetLength(aNewLength)) { // OOM
    mItems.Clear();
    return;
  }

  // If our length has increased, null out the new pointers:
  for (PRUint32 i = oldLength; i < aNewLength; ++i) {
    mItems[i] = nsnull;
  }
}

SVGLengthList&
DOMSVGLengthList::InternalList()
{
  SVGAnimatedLengthList *alist = Element()->GetAnimatedLengthList(AttrEnum());
  return IsAnimValList() && alist->mAnimVal ? *alist->mAnimVal : alist->mBaseVal;
}

// ----------------------------------------------------------------------------
// nsIDOMSVGLengthList implementation:

NS_IMETHODIMP
DOMSVGLengthList::GetNumberOfItems(PRUint32 *aNumberOfItems)
{
#ifdef MOZ_SMIL
  if (IsAnimValList()) {
    Element()->FlushAnimations();
  }
#endif
  *aNumberOfItems = Length();
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGLengthList::Clear()
{
  if (IsAnimValList()) {
    return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
  }

  if (Length() > 0) {
    // Notify any existing DOM items of removal *before* truncating the lists
    // so that they can find their SVGLength internal counterparts and copy
    // their values:
    mAList->InternalBaseValListWillChangeTo(SVGLengthList());

    mItems.Clear();
    InternalList().Clear();
    Element()->DidChangeLengthList(AttrEnum(), PR_TRUE);
#ifdef MOZ_SMIL
    if (mAList->IsAnimating()) {
      Element()->AnimationNeedsResample();
    }
#endif
  }
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGLengthList::Initialize(nsIDOMSVGLength *newItem,
                             nsIDOMSVGLength **_retval)
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

  nsCOMPtr<DOMSVGLength> domItem = do_QueryInterface(newItem);
  if (!domItem) {
    return NS_ERROR_DOM_SVG_WRONG_TYPE_ERR;
  }
  if (domItem->HasOwner()) {
    newItem = domItem->Copy();
  }

  Clear();
  return InsertItemBefore(newItem, 0, _retval);
}

NS_IMETHODIMP
DOMSVGLengthList::GetItem(PRUint32 index,
                          nsIDOMSVGLength **_retval)
{
#ifdef MOZ_SMIL
  if (IsAnimValList()) {
    Element()->FlushAnimations();
  }
#endif
  if (index < Length()) {
    EnsureItemAt(index);
    NS_ADDREF(*_retval = mItems[index]);
    return NS_OK;
  }
  *_retval = nsnull;
  return NS_ERROR_DOM_INDEX_SIZE_ERR;
}

NS_IMETHODIMP
DOMSVGLengthList::InsertItemBefore(nsIDOMSVGLength *newItem,
                                   PRUint32 index,
                                   nsIDOMSVGLength **_retval)
{
  *_retval = nsnull;
  if (IsAnimValList()) {
    return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
  }

  nsCOMPtr<DOMSVGLength> domItem = do_QueryInterface(newItem);
  if (!domItem) {
    return NS_ERROR_DOM_SVG_WRONG_TYPE_ERR;
  }
  index = NS_MIN(index, Length());
  SVGLength length = domItem->ToSVGLength(); // get before setting domItem
  if (domItem->HasOwner()) {
    domItem = new DOMSVGLength();
  }
  PRBool ok = !!InternalList().InsertItem(index, length);
  if (!ok) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  domItem->InsertingIntoList(this, AttrEnum(), index, IsAnimValList());
  ok = !!mItems.InsertElementAt(index, domItem.get());
  if (!ok) {
    InternalList().RemoveItem(index);
    return NS_ERROR_OUT_OF_MEMORY;
  }
  for (PRUint32 i = index + 1; i < Length(); ++i) {
    if (mItems[i]) {
      mItems[i]->UpdateListIndex(i);
    }
  }
  Element()->DidChangeLengthList(AttrEnum(), PR_TRUE);
#ifdef MOZ_SMIL
  if (mAList->IsAnimating()) {
    Element()->AnimationNeedsResample();
  }
#endif
  *_retval = domItem.forget().get();
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGLengthList::ReplaceItem(nsIDOMSVGLength *newItem,
                              PRUint32 index,
                              nsIDOMSVGLength **_retval)
{
  *_retval = nsnull;
  if (IsAnimValList()) {
    return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
  }

  nsCOMPtr<DOMSVGLength> domItem = do_QueryInterface(newItem);
  if (!domItem) {
    return NS_ERROR_DOM_SVG_WRONG_TYPE_ERR;
  }
  if (index >= Length()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }
  SVGLength length = domItem->ToSVGLength(); // get before setting domItem
  if (domItem->HasOwner()) {
    domItem = new DOMSVGLength();
  }
  if (mItems[index]) {
    // Notify any existing DOM item of removal *before* modifying the lists so
    // that the DOM item can copy the *old* value at its index:
    mItems[index]->RemovingFromList();
  }
  InternalList()[index] = length;
  domItem->InsertingIntoList(this, AttrEnum(), index, IsAnimValList());
  mItems[index] = domItem;

  Element()->DidChangeLengthList(AttrEnum(), PR_TRUE);
#ifdef MOZ_SMIL
  if (mAList->IsAnimating()) {
    Element()->AnimationNeedsResample();
  }
#endif
  NS_ADDREF(*_retval = domItem.get());
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGLengthList::RemoveItem(PRUint32 index,
                             nsIDOMSVGLength **_retval)
{
  *_retval = nsnull;
  if (IsAnimValList()) {
    return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
  }

  if (index >= Length()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }
  // We have to return the removed item, so make sure it exists:
  EnsureItemAt(index);

  // Notify the DOM item of removal *before* modifying the lists so that the
  // DOM item can copy its *old* value:
  mItems[index]->RemovingFromList();

  InternalList().RemoveItem(index);

  NS_ADDREF(*_retval = mItems[index]);
  mItems.RemoveElementAt(index);
  for (PRUint32 i = index; i < Length(); ++i) {
    if (mItems[i]) {
      mItems[i]->UpdateListIndex(i);
    }
  }
  Element()->DidChangeLengthList(AttrEnum(), PR_TRUE);
#ifdef MOZ_SMIL
  if (mAList->IsAnimating()) {
    Element()->AnimationNeedsResample();
  }
#endif
  return NS_OK;
}

NS_IMETHODIMP
DOMSVGLengthList::AppendItem(nsIDOMSVGLength *newItem,
                             nsIDOMSVGLength **_retval)
{
  return InsertItemBefore(newItem, Length(), _retval);
}

void
DOMSVGLengthList::EnsureItemAt(PRUint32 aIndex)
{
  if (!mItems[aIndex]) {
    mItems[aIndex] = new DOMSVGLength(this, AttrEnum(), aIndex, IsAnimValList());
  }
}

} // namespace mozilla
