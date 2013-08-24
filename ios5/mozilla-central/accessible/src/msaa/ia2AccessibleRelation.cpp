/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ia2AccessibleRelation.h"

#include "Relation.h"

#include "nsIAccessibleRelation.h"
#include "nsID.h"

#include "AccessibleRelation_i.c"

using namespace mozilla::a11y;

ia2AccessibleRelation::ia2AccessibleRelation(PRUint32 aType, Relation* aRel) :
  mType(aType), mReferences(0)
{
  Accessible* target = nsnull;
  while ((target = aRel->Next()))
    mTargets.AppendElement(target);
}

// IUnknown

STDMETHODIMP
ia2AccessibleRelation::QueryInterface(REFIID iid, void** ppv)
{
  if (!ppv)
    return E_INVALIDARG;

  *ppv = NULL;

  if (IID_IAccessibleRelation == iid || IID_IUnknown == iid) {
    *ppv = static_cast<IAccessibleRelation*>(this);
    (reinterpret_cast<IUnknown*>(*ppv))->AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE
ia2AccessibleRelation::AddRef()
{
  return mReferences++;
}

ULONG STDMETHODCALLTYPE 
ia2AccessibleRelation::Release()
{
  mReferences--;
  ULONG references = mReferences;
  if (!mReferences)
    delete this;

  return references;
}

// IAccessibleRelation

STDMETHODIMP
ia2AccessibleRelation::get_relationType(BSTR *aRelationType)
{
__try {
  if (!aRelationType)
    return E_INVALIDARG;

  *aRelationType = NULL;

  switch (mType) {
    case nsIAccessibleRelation::RELATION_CONTROLLED_BY:
      *aRelationType = ::SysAllocString(IA2_RELATION_CONTROLLED_BY);
      break;
    case nsIAccessibleRelation::RELATION_CONTROLLER_FOR:
      *aRelationType = ::SysAllocString(IA2_RELATION_CONTROLLER_FOR);
      break;
    case nsIAccessibleRelation::RELATION_DESCRIBED_BY:
      *aRelationType = ::SysAllocString(IA2_RELATION_DESCRIBED_BY);
      break;
    case nsIAccessibleRelation::RELATION_DESCRIPTION_FOR:
      *aRelationType = ::SysAllocString(IA2_RELATION_DESCRIPTION_FOR);
      break;
    case nsIAccessibleRelation::RELATION_EMBEDDED_BY:
      *aRelationType = ::SysAllocString(IA2_RELATION_EMBEDDED_BY);
      break;
    case nsIAccessibleRelation::RELATION_EMBEDS:
      *aRelationType = ::SysAllocString(IA2_RELATION_EMBEDS);
      break;
    case nsIAccessibleRelation::RELATION_FLOWS_FROM:
      *aRelationType = ::SysAllocString(IA2_RELATION_FLOWS_FROM);
      break;
    case nsIAccessibleRelation::RELATION_FLOWS_TO:
      *aRelationType = ::SysAllocString(IA2_RELATION_FLOWS_TO);
      break;
    case nsIAccessibleRelation::RELATION_LABEL_FOR:
      *aRelationType = ::SysAllocString(IA2_RELATION_LABEL_FOR);
      break;
    case nsIAccessibleRelation::RELATION_LABELLED_BY:
      *aRelationType = ::SysAllocString(IA2_RELATION_LABELED_BY);
      break;
    case nsIAccessibleRelation::RELATION_MEMBER_OF:
      *aRelationType = ::SysAllocString(IA2_RELATION_MEMBER_OF);
      break;
    case nsIAccessibleRelation::RELATION_NODE_CHILD_OF:
      *aRelationType = ::SysAllocString(IA2_RELATION_NODE_CHILD_OF);
      break;
    case nsIAccessibleRelation::RELATION_PARENT_WINDOW_OF:
      *aRelationType = ::SysAllocString(IA2_RELATION_PARENT_WINDOW_OF);
      break;
    case nsIAccessibleRelation::RELATION_POPUP_FOR:
      *aRelationType = ::SysAllocString(IA2_RELATION_POPUP_FOR);
      break;
    case nsIAccessibleRelation::RELATION_SUBWINDOW_OF:
      *aRelationType = ::SysAllocString(IA2_RELATION_SUBWINDOW_OF);
      break;
    default:
      return E_FAIL;
  }

  return *aRelationType ? S_OK : E_OUTOFMEMORY;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
ia2AccessibleRelation::get_localizedRelationType(BSTR *aLocalizedRelationType)
{
__try {
  if (!aLocalizedRelationType)
    return E_INVALIDARG;

  *aLocalizedRelationType = NULL;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_NOTIMPL;
}

STDMETHODIMP
ia2AccessibleRelation::get_nTargets(long *aNTargets)
{
__try {
 if (!aNTargets)
   return E_INVALIDARG;

 *aNTargets = mTargets.Length();
  return S_OK;
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
ia2AccessibleRelation::get_target(long aTargetIndex, IUnknown **aTarget)
{
__try {
  if (aTargetIndex < 0 || aTargetIndex >= mTargets.Length() || !aTarget)
    return E_INVALIDARG;

  mTargets[aTargetIndex]->QueryNativeInterface(IID_IUnknown, (void**) aTarget);
  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
ia2AccessibleRelation::get_targets(long aMaxTargets, IUnknown **aTargets,
                                   long *aNTargets)
{
__try {
  if (!aNTargets || !aTargets)
    return E_INVALIDARG;

  *aNTargets = 0;
  PRUint32 maxTargets = mTargets.Length();
  if (maxTargets > aMaxTargets)
    maxTargets = aMaxTargets;

  for (PRUint32 idx = 0; idx < maxTargets; idx++)
    get_target(idx, aTargets + idx);

  *aNTargets = maxTargets;
  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

