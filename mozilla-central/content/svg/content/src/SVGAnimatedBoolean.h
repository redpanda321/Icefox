/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include "nsWrapperCache.h"
#include "nsSVGElement.h"
#include "mozilla/Attributes.h"
#include "nsSVGBoolean.h"

namespace mozilla {
namespace dom {

class SVGAnimatedBoolean MOZ_FINAL : public nsISupports,
                                     public nsWrapperCache
{
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(SVGAnimatedBoolean)

  SVGAnimatedBoolean(nsSVGBoolean* aVal, nsSVGElement *aSVGElement)
    : mVal(aVal), mSVGElement(aSVGElement)
  {
    SetIsDOMBinding();
  }
  ~SVGAnimatedBoolean();

 // WebIDL
  nsSVGElement* GetParentObject() const { return mSVGElement; }
  virtual JSObject* WrapObject(JSContext* aCx, JSObject* aScope, bool* aTriedToWrap);
  bool BaseVal() const { return mVal->GetBaseValue(); }
  void SetBaseVal(bool aValue) { mVal->SetBaseValue(aValue, mSVGElement); }
  bool AnimVal() const { mSVGElement->FlushAnimations(); return mVal->GetAnimValue(); }

protected:
  nsSVGBoolean* mVal; // kept alive because it belongs to content
  nsRefPtr<nsSVGElement> mSVGElement;
};

} //namespace dom
} //namespace mozilla
