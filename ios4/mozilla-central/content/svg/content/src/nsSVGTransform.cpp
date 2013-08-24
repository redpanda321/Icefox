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
 * The Original Code is the Mozilla SVG project.
 *
 * The Initial Developer of the Original Code is
 * Crocodile Clips Ltd..
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Alex Fritze <alex.fritze@crocodile-clips.com> (original author)
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

#include "nsSVGTransform.h"
#include "prdtoa.h"
#include "nsSVGMatrix.h"
#include "nsISVGValueUtils.h"
#include "nsWeakReference.h"
#include "nsSVGMatrix.h"
#include "nsTextFormatter.h"
#include "nsContentUtils.h"
#include "nsDOMError.h"

const double radPerDegree = 2.0*3.1415926535 / 360.0;

//----------------------------------------------------------------------
// Implementation

nsresult
nsSVGTransform::Create(nsIDOMSVGTransform** aResult)
{
  nsSVGTransform *pl = new nsSVGTransform();
  NS_ENSURE_TRUE(pl, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(pl);
  if (NS_FAILED(pl->Init())) {
    NS_RELEASE(pl);
    *aResult = nsnull;
    return NS_ERROR_FAILURE;
  }
  *aResult = pl;
  return NS_OK;
}


nsSVGTransform::nsSVGTransform()
    : mAngle(0.0f),
      mOriginX(0.0f),
      mOriginY(0.0f),
      mType( SVG_TRANSFORM_MATRIX )
{
}

nsSVGTransform::~nsSVGTransform()
{
  NS_REMOVE_SVGVALUE_OBSERVER(mMatrix);
}

nsresult nsSVGTransform::Init()
{
  nsresult rv = NS_NewSVGMatrix(getter_AddRefs(mMatrix));
  NS_ADD_SVGVALUE_OBSERVER(mMatrix);
  return rv;
}

//----------------------------------------------------------------------
// nsISupports methods:

NS_IMPL_ADDREF(nsSVGTransform)
NS_IMPL_RELEASE(nsSVGTransform)

DOMCI_DATA(SVGTransform, nsSVGTransform)

NS_INTERFACE_MAP_BEGIN(nsSVGTransform)
  NS_INTERFACE_MAP_ENTRY(nsISVGValue)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGTransform)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsISVGValueObserver)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGTransform)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsISVGValue)
NS_INTERFACE_MAP_END


//----------------------------------------------------------------------
// nsISVGValue methods:

NS_IMETHODIMP
nsSVGTransform::SetValueString(const nsAString& aValue)
{
  NS_NOTYETIMPLEMENTED("nsSVGTransform::SetValueString");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsSVGTransform::GetValueString(nsAString& aValue)
{
  PRUnichar buf[256];
  
  switch (mType) {
    case nsIDOMSVGTransform::SVG_TRANSFORM_TRANSLATE:
      {
        float dx, dy;
        mMatrix->GetE(&dx);
        mMatrix->GetF(&dy);
        if (dy != 0.0f)
          nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar), NS_LITERAL_STRING("translate(%g, %g)").get(), dx, dy);
        else
          nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar), NS_LITERAL_STRING("translate(%g)").get(), dx);
      }
      break;
    case nsIDOMSVGTransform::SVG_TRANSFORM_ROTATE:
      {
        if (mOriginX != 0.0f || mOriginY != 0.0f)
          nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar),
                                    NS_LITERAL_STRING("rotate(%g, %g, %g)").get(),
                                    mAngle, mOriginX, mOriginY);
        else
          nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar),
                                    NS_LITERAL_STRING("rotate(%g)").get(), mAngle);
      }
      break;        
    case nsIDOMSVGTransform::SVG_TRANSFORM_SCALE:
      {
        float sx, sy;
        mMatrix->GetA(&sx);
        mMatrix->GetD(&sy);
        if (sy != sx)
          nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar),
                                    NS_LITERAL_STRING("scale(%g, %g)").get(), sx, sy);
        else
          nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar),
                                    NS_LITERAL_STRING("scale(%g)").get(), sx);
      }
      break;
    case nsIDOMSVGTransform::SVG_TRANSFORM_SKEWX:
      {
        nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar),
                                  NS_LITERAL_STRING("skewX(%g)").get(), mAngle);
      }
      break;
    case nsIDOMSVGTransform::SVG_TRANSFORM_SKEWY:
      {
        nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar),
                                  NS_LITERAL_STRING("skewY(%g)").get(), mAngle);
      }
      break;
    case nsIDOMSVGTransform::SVG_TRANSFORM_MATRIX:
      {
        float a,b,c,d,e,f;
        mMatrix->GetA(&a);
        mMatrix->GetB(&b);
        mMatrix->GetC(&c);
        mMatrix->GetD(&d);
        mMatrix->GetE(&e);
        mMatrix->GetF(&f);
        nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar),
                                  NS_LITERAL_STRING("matrix(%g, %g, %g, %g, %g, %g)").get(),
                                  a, b, c, d, e, f);
      }
      break;
    default:
      buf[0] = '\0';
      NS_ERROR("unknown transformation type");
      break;
  }

  aValue.Assign(buf);
  
  return NS_OK;
}


//----------------------------------------------------------------------
// nsISVGValueObserver methods:

NS_IMETHODIMP nsSVGTransform::WillModifySVGObservable(nsISVGValue* observable,
                                                      modificationType aModType)
{
  WillModify();
  return NS_OK;
}

NS_IMETHODIMP nsSVGTransform::DidModifySVGObservable (nsISVGValue* observable,
                                                      modificationType aModType)
{
  // we become a general matrix transform if mMatrix changes
  mType = SVG_TRANSFORM_MATRIX;
  mAngle = 0.0f;
  DidModify();
  return NS_OK;
}


//----------------------------------------------------------------------
// nsIDOMSVGTransform methods:

/* readonly attribute unsigned short type; */
NS_IMETHODIMP nsSVGTransform::GetType(PRUint16 *aType)
{
  *aType = mType;
  return NS_OK;
}

/* readonly attribute nsIDOMSVGMatrix matrix; */
NS_IMETHODIMP nsSVGTransform::GetMatrix(nsIDOMSVGMatrix * *aMatrix)
{
  *aMatrix = mMatrix;
  NS_IF_ADDREF(*aMatrix);
  return NS_OK;
}

/* readonly attribute float angle; */
NS_IMETHODIMP nsSVGTransform::GetAngle(float *aAngle)
{
  *aAngle = mAngle;
  return NS_OK;
}

/* void setMatrix (in nsIDOMSVGMatrix matrix); */
NS_IMETHODIMP nsSVGTransform::SetMatrix(nsIDOMSVGMatrix *matrix)
{
  float a, b, c, d, e, f;

  if (!matrix)
    return NS_ERROR_DOM_SVG_WRONG_TYPE_ERR;

  WillModify();

  mType = SVG_TRANSFORM_MATRIX;
  mAngle = 0.0f;
  
  matrix->GetA(&a);
  matrix->GetB(&b);
  matrix->GetC(&c);
  matrix->GetD(&d);
  matrix->GetE(&e);
  matrix->GetF(&f);

  NS_REMOVE_SVGVALUE_OBSERVER(mMatrix);
  mMatrix->SetA(a);
  mMatrix->SetB(b);
  mMatrix->SetC(c);
  mMatrix->SetD(d);
  mMatrix->SetE(e);
  mMatrix->SetF(f);
  NS_ADD_SVGVALUE_OBSERVER(mMatrix);

  DidModify();
  return NS_OK;
}

/* void setTranslate (in float tx, in float ty); */
NS_IMETHODIMP nsSVGTransform::SetTranslate(float tx, float ty)
{
  NS_ENSURE_FINITE2(tx, ty, NS_ERROR_ILLEGAL_VALUE);

  WillModify();
  
  mType = SVG_TRANSFORM_TRANSLATE;
  mAngle = 0.0f;
  NS_REMOVE_SVGVALUE_OBSERVER(mMatrix);
  mMatrix->SetA(1.0f);
  mMatrix->SetB(0.0f);
  mMatrix->SetC(0.0f);
  mMatrix->SetD(1.0f);
  mMatrix->SetE(tx);
  mMatrix->SetF(ty);
  NS_ADD_SVGVALUE_OBSERVER(mMatrix);

  DidModify();
  return NS_OK;
}

/* void setScale (in float sx, in float sy); */
NS_IMETHODIMP nsSVGTransform::SetScale(float sx, float sy)
{
  NS_ENSURE_FINITE2(sx, sy, NS_ERROR_ILLEGAL_VALUE);

  WillModify();
  
  mType = SVG_TRANSFORM_SCALE;
  mAngle = 0.0f;
  NS_REMOVE_SVGVALUE_OBSERVER(mMatrix);
  mMatrix->SetA(sx);
  mMatrix->SetB(0.0f);
  mMatrix->SetC(0.0f);
  mMatrix->SetD(sy);
  mMatrix->SetE(0.0f);
  mMatrix->SetF(0.0f);
  NS_ADD_SVGVALUE_OBSERVER(mMatrix);

  DidModify();
  return NS_OK;
}

/* void setRotate (in float angle, in float cx, in float cy); */
NS_IMETHODIMP nsSVGTransform::SetRotate(float angle, float cx, float cy)
{
  NS_ENSURE_FINITE3(angle, cx, cy, NS_ERROR_ILLEGAL_VALUE);

  WillModify();
  
  mType = SVG_TRANSFORM_ROTATE;
  mAngle = angle;
  mOriginX = cx;
  mOriginY = cy;

  gfxMatrix matrix(1, 0, 0, 1, cx, cy);
  matrix.Rotate(angle * radPerDegree);
  matrix.Translate(gfxPoint(-cx, -cy));

  NS_REMOVE_SVGVALUE_OBSERVER(mMatrix);
  mMatrix->SetA(static_cast<float>(matrix.xx));
  mMatrix->SetB(static_cast<float>(matrix.yx));
  mMatrix->SetC(static_cast<float>(matrix.xy));
  mMatrix->SetD(static_cast<float>(matrix.yy));
  mMatrix->SetE(static_cast<float>(matrix.x0));
  mMatrix->SetF(static_cast<float>(matrix.y0));
  NS_ADD_SVGVALUE_OBSERVER(mMatrix);

  DidModify();
  return NS_OK;
}

/* void setSkewX (in float angle); */
NS_IMETHODIMP nsSVGTransform::SetSkewX(float angle)
{
  NS_ENSURE_FINITE(angle, NS_ERROR_ILLEGAL_VALUE);

  float ta = static_cast<float>(tan(angle * radPerDegree));

  NS_ENSURE_FINITE(ta, NS_ERROR_DOM_SVG_INVALID_VALUE_ERR);

  WillModify();
  
  mType = SVG_TRANSFORM_SKEWX;
  mAngle = angle;

  NS_REMOVE_SVGVALUE_OBSERVER(mMatrix);
  mMatrix->SetA(1.0f);
  mMatrix->SetB(0.0f);
  mMatrix->SetC(ta);
  mMatrix->SetD(1.0f);
  mMatrix->SetE(0.0f);
  mMatrix->SetF(0.0f);
  NS_ADD_SVGVALUE_OBSERVER(mMatrix);

  DidModify();
  return NS_OK;
}

/* void setSkewY (in float angle); */
NS_IMETHODIMP nsSVGTransform::SetSkewY(float angle)
{
  NS_ENSURE_FINITE(angle, NS_ERROR_ILLEGAL_VALUE);

  float ta = static_cast<float>(tan(angle * radPerDegree));

  NS_ENSURE_FINITE(ta, NS_ERROR_DOM_SVG_INVALID_VALUE_ERR);

  WillModify();
  
  mType = SVG_TRANSFORM_SKEWY;
  mAngle = angle;

  NS_REMOVE_SVGVALUE_OBSERVER(mMatrix);
  mMatrix->SetA(1.0f);
  mMatrix->SetB(ta);
  mMatrix->SetC(0.0f);
  mMatrix->SetD(1.0f);
  mMatrix->SetE(0.0f);
  mMatrix->SetF(0.0f);
  NS_ADD_SVGVALUE_OBSERVER(mMatrix);

  DidModify();
  return NS_OK;
}



////////////////////////////////////////////////////////////////////////
// Exported creation functions:

nsresult
NS_NewSVGTransform(nsIDOMSVGTransform** result)
{
  return nsSVGTransform::Create(result);
}
