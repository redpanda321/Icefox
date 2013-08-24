/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsSVGPathGeometryElement.h"

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ADDREF_INHERITED(nsSVGPathGeometryElement, nsSVGPathGeometryElementBase)
NS_IMPL_RELEASE_INHERITED(nsSVGPathGeometryElement, nsSVGPathGeometryElementBase)

NS_INTERFACE_MAP_BEGIN(nsSVGPathGeometryElement)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGTests)
NS_INTERFACE_MAP_END_INHERITING(nsSVGPathGeometryElementBase)

//----------------------------------------------------------------------
// Implementation

nsSVGPathGeometryElement::nsSVGPathGeometryElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : nsSVGPathGeometryElementBase(aNodeInfo)
{
}

bool
nsSVGPathGeometryElement::AttributeDefinesGeometry(const nsIAtom *aName)
{
  // Check for nsSVGLength2 attribute
  LengthAttributesInfo info = GetLengthInfo();
  for (PRUint32 i = 0; i < info.mLengthCount; i++) {
    if (aName == *info.mLengthInfo[i].mName) {
      return true;
    }
  }

  return false;
}

bool
nsSVGPathGeometryElement::IsMarkable()
{
  return false;
}

void
nsSVGPathGeometryElement::GetMarkPoints(nsTArray<nsSVGMark> *aMarks)
{
}

already_AddRefed<gfxFlattenedPath>
nsSVGPathGeometryElement::GetFlattenedPath(const gfxMatrix &aMatrix)
{
  return nsnull;
}
