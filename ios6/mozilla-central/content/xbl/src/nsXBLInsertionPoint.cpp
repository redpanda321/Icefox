/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXBLInsertionPoint.h"
#include "nsXBLBinding.h"

nsXBLInsertionPoint::nsXBLInsertionPoint(nsIContent* aParentElement,
                                         uint32_t aIndex,
                                         nsIContent* aDefaultContent)
  : mParentElement(aParentElement),
    mIndex(aIndex),
    mDefaultContentTemplate(aDefaultContent)
{
}

nsXBLInsertionPoint::~nsXBLInsertionPoint()
{
  if (mDefaultContent) {
    nsXBLBinding::UninstallAnonymousContent(mDefaultContent->OwnerDoc(),
                                            mDefaultContent);
  }
}

NS_IMPL_CYCLE_COLLECTION_NATIVE_CLASS(nsXBLInsertionPoint)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsXBLInsertionPoint)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mElements)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDefaultContentTemplate)
  if (tmp->mDefaultContent) {
    nsXBLBinding::UninstallAnonymousContent(tmp->mDefaultContent->OwnerDoc(),
                                            tmp->mDefaultContent);
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDefaultContent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsXBLInsertionPoint)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mElements)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDefaultContentTemplate)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDefaultContent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(nsXBLInsertionPoint, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(nsXBLInsertionPoint, Release)

nsIContent*
nsXBLInsertionPoint::GetInsertionParent()
{
  return mParentElement;
}

nsIContent*
nsXBLInsertionPoint::GetDefaultContent()
{
  return mDefaultContent;
}

nsIContent*
nsXBLInsertionPoint::GetDefaultContentTemplate()
{
  return mDefaultContentTemplate;
}

nsIContent*
nsXBLInsertionPoint::ChildAt(uint32_t aIndex)
{
  return mElements.ObjectAt(aIndex);
}

bool
nsXBLInsertionPoint::Matches(nsIContent* aContent, uint32_t aIndex)
{
  return (aContent == mParentElement && mIndex != -1 && ((int32_t)aIndex) == mIndex);
}

void
nsXBLInsertionPoint::UnbindDefaultContent()
{
  if (!mDefaultContent) {
    return;
  }

  // Undo InstallAnonymousContent.
  nsXBLBinding::UninstallAnonymousContent(mDefaultContent->OwnerDoc(),
                                          mDefaultContent);
}
