/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SVGMotionSMILAnimationFunction.h"
#include "nsSMILParserUtils.h"
#include "nsSVGAngle.h"
#include "SVGMotionSMILType.h"
#include "SVGMotionSMILPathUtils.h"
#include "nsSVGPathDataParser.h"
#include "nsSVGPathElement.h" // for nsSVGPathList
#include "nsSVGMpathElement.h"

namespace mozilla {

SVGMotionSMILAnimationFunction::SVGMotionSMILAnimationFunction()
  : mRotateType(eRotateType_Explicit),
    mRotateAngle(0.0f),
    mPathSourceType(ePathSourceType_None),
    mIsPathStale(true)  // Try to initialize path on first GetValues call
{
}

void
SVGMotionSMILAnimationFunction::MarkStaleIfAttributeAffectsPath(nsIAtom* aAttribute)
{
  bool isAffected;
  if (aAttribute == nsGkAtoms::path) {
    isAffected = (mPathSourceType <= ePathSourceType_PathAttr);
  } else if (aAttribute == nsGkAtoms::values) {
    isAffected = (mPathSourceType <= ePathSourceType_ValuesAttr);
  } else if (aAttribute == nsGkAtoms::from ||
             aAttribute == nsGkAtoms::to) {
    isAffected = (mPathSourceType <= ePathSourceType_ToAttr);
  } else if (aAttribute == nsGkAtoms::by) {
    isAffected = (mPathSourceType <= ePathSourceType_ByAttr);
  } else {
    NS_NOTREACHED("Should only call this method for path-describing attrs");
    isAffected = false;
  }

  if (isAffected) {
    mIsPathStale = true;
    mHasChanged = true;
  }
}

bool
SVGMotionSMILAnimationFunction::SetAttr(nsIAtom* aAttribute,
                                        const nsAString& aValue,
                                        nsAttrValue& aResult,
                                        nsresult* aParseResult)
{
  // Handle motion-specific attrs
  if (aAttribute == nsGkAtoms::keyPoints) {
    nsresult rv = SetKeyPoints(aValue, aResult);
    if (aParseResult) {
      *aParseResult = rv;
    }
  } else if (aAttribute == nsGkAtoms::rotate) {
    nsresult rv = SetRotate(aValue, aResult);
    if (aParseResult) {
      *aParseResult = rv;
    }
  } else if (aAttribute == nsGkAtoms::path) {
    aResult.SetTo(aValue);
    if (aParseResult) {
      *aParseResult = NS_OK;
    }
    MarkStaleIfAttributeAffectsPath(aAttribute);
  } else if (aAttribute == nsGkAtoms::by ||
             aAttribute == nsGkAtoms::from ||
             aAttribute == nsGkAtoms::to ||
             aAttribute == nsGkAtoms::values) {
    MarkStaleIfAttributeAffectsPath(aAttribute);
  } else {
    // Defer to superclass method
    return nsSMILAnimationFunction::SetAttr(aAttribute, aValue,
                                            aResult, aParseResult);
  }

  return true;
}

bool
SVGMotionSMILAnimationFunction::UnsetAttr(nsIAtom* aAttribute)
{
  if (aAttribute == nsGkAtoms::keyPoints) {
    UnsetKeyPoints();
  } else if (aAttribute == nsGkAtoms::rotate) {
    UnsetRotate();
  } else if (aAttribute == nsGkAtoms::path ||
             aAttribute == nsGkAtoms::by ||
             aAttribute == nsGkAtoms::from ||
             aAttribute == nsGkAtoms::to ||
             aAttribute == nsGkAtoms::values) {
    MarkStaleIfAttributeAffectsPath(aAttribute);
  } else {
    // Defer to superclass method
    return nsSMILAnimationFunction::UnsetAttr(aAttribute);
  }

  return true;
}

nsSMILAnimationFunction::nsSMILCalcMode
SVGMotionSMILAnimationFunction::GetCalcMode() const
{
  const nsAttrValue* value = GetAttr(nsGkAtoms::calcMode);
  if (!value) {
    return CALC_PACED; // animateMotion defaults to calcMode="paced"
  }

  return nsSMILCalcMode(value->GetEnumValue());
}

//----------------------------------------------------------------------
// Helpers for GetValues

/*
 * Returns the first <mpath> child of the given element
 */

static nsSVGMpathElement*
GetFirstMpathChild(nsIContent* aElem)
{
  for (nsIContent* child = aElem->GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    if (child->IsSVG(nsGkAtoms::mpath)) {
      return static_cast<nsSVGMpathElement*>(child);
    }
  }

  return nsnull;
}

void
SVGMotionSMILAnimationFunction::
  RebuildPathAndVerticesFromBasicAttrs(const nsIContent* aContextElem)
{
  NS_ABORT_IF_FALSE(!HasAttr(nsGkAtoms::path),
                    "Should be using |path| attr if we have it");
  NS_ABORT_IF_FALSE(!mPath, "regenerating when we aleady have path");
  NS_ABORT_IF_FALSE(mPathVertices.IsEmpty(),
                    "regenerating when we already have vertices");

  if (!aContextElem->IsSVG()) {
    NS_ERROR("Uh oh, SVG animateMotion element targeting a non-SVG node");
    return;
  }

  SVGMotionSMILPathUtils::PathGenerator
    pathGenerator(static_cast<const nsSVGElement*>(aContextElem));

  bool success = false;
  if (HasAttr(nsGkAtoms::values)) {
    // Generate path based on our values array
    mPathSourceType = ePathSourceType_ValuesAttr;
    const nsAString& valuesStr = GetAttr(nsGkAtoms::values)->GetStringValue();
    SVGMotionSMILPathUtils::MotionValueParser parser(&pathGenerator,
                                                     &mPathVertices);
    success =
      NS_SUCCEEDED(nsSMILParserUtils::ParseValuesGeneric(valuesStr, parser));
  } else if (HasAttr(nsGkAtoms::to) || HasAttr(nsGkAtoms::by)) {
    // Apply 'from' value (or a dummy 0,0 'from' value)
    if (HasAttr(nsGkAtoms::from)) {
      const nsAString& fromStr = GetAttr(nsGkAtoms::from)->GetStringValue();
      success = pathGenerator.MoveToAbsolute(fromStr);
      mPathVertices.AppendElement(0.0);
    } else {
      // Create dummy 'from' value at 0,0, if we're doing by-animation.
      // (NOTE: We don't add the dummy 0-point to our list for *to-animation*,
      // because the nsSMILAnimationFunction logic for to-animation doesn't
      // expect a dummy value. It only expects one value: the final 'to' value.)
      pathGenerator.MoveToOrigin();
      if (!HasAttr(nsGkAtoms::to)) {
        mPathVertices.AppendElement(0.0);
      }
      success = true;
    }

    // Apply 'to' or 'by' value
    if (success) {
      double dist;
      if (HasAttr(nsGkAtoms::to)) {
        mPathSourceType = ePathSourceType_ToAttr;
        const nsAString& toStr = GetAttr(nsGkAtoms::to)->GetStringValue();
        success = pathGenerator.LineToAbsolute(toStr, dist);
      } else { // HasAttr(nsGkAtoms::by)
        mPathSourceType = ePathSourceType_ByAttr;
        const nsAString& byStr = GetAttr(nsGkAtoms::by)->GetStringValue();
        success = pathGenerator.LineToRelative(byStr, dist);
      }
      if (success) {
        mPathVertices.AppendElement(dist);
      }
    }
  }
  if (success) {
    mPath = pathGenerator.GetResultingPath();
  } else {
    // Parse failure. Leave path as null, and clear path-related member data.
    mPathVertices.Clear();
  }
}

void
SVGMotionSMILAnimationFunction::
  RebuildPathAndVerticesFromMpathElem(nsSVGMpathElement* aMpathElem)
{
  mPathSourceType = ePathSourceType_Mpath;

  // Use the path that's the target of our chosen <mpath> child.
  nsSVGPathElement* pathElem = aMpathElem->GetReferencedPath();
  if (pathElem) {
    const SVGPathData &path = pathElem->GetAnimPathSegList()->GetAnimValue();
    // Path data must contain of at least one path segment (if the path data
    // doesn't begin with a valid "M", then it's invalid).
    if (path.Length()) {
      bool ok =
        path.GetDistancesFromOriginToEndsOfVisibleSegments(&mPathVertices);
      if (ok && mPathVertices.Length()) {
        mPath = pathElem->GetFlattenedPath(gfxMatrix());
      }
    }
  }
}

void
SVGMotionSMILAnimationFunction::RebuildPathAndVerticesFromPathAttr()
{
  const nsAString& pathSpec = GetAttr(nsGkAtoms::path)->GetStringValue();
  mPathSourceType = ePathSourceType_PathAttr;

  // Generate gfxFlattenedPath from |path| attr
  SVGPathData path;
  nsSVGPathDataParserToInternal pathParser(&path);

  // We ignore any failure returned from Parse() since the SVG spec says to
  // accept all segments up to the first invalid token. Instead we must
  // explicitly check that the parse produces at least one path segment (if
  // the path data doesn't begin with a valid "M", then it's invalid).
  pathParser.Parse(pathSpec);
  if (!path.Length()) {
    return;
  }

  mPath = path.ToFlattenedPath(gfxMatrix());
  bool ok = path.GetDistancesFromOriginToEndsOfVisibleSegments(&mPathVertices);
  if (!ok || !mPathVertices.Length()) {
    mPath = nsnull;
  }
}

// Helper to regenerate our path representation & its list of vertices
void
SVGMotionSMILAnimationFunction::
  RebuildPathAndVertices(const nsIContent* aTargetElement)
{
  NS_ABORT_IF_FALSE(mIsPathStale, "rebuilding path when it isn't stale");

  // Clear stale data
  mPath = nsnull;
  mPathVertices.Clear();
  mPathSourceType = ePathSourceType_None;

  // Do we have a mpath child? if so, it trumps everything. Otherwise, we look
  // through our list of path-defining attributes, in order of priority.
  nsSVGMpathElement* firstMpathChild =
    GetFirstMpathChild(&mAnimationElement->AsElement());

  if (firstMpathChild) {
    RebuildPathAndVerticesFromMpathElem(firstMpathChild);
    mValueNeedsReparsingEverySample = false;
  } else if (HasAttr(nsGkAtoms::path)) {
    RebuildPathAndVerticesFromPathAttr();
    mValueNeedsReparsingEverySample = false;
  } else {
    // Get path & vertices from basic SMIL attrs: from/by/to/values

    RebuildPathAndVerticesFromBasicAttrs(aTargetElement);
    mValueNeedsReparsingEverySample = true;
  }
  mIsPathStale = false;
}

bool
SVGMotionSMILAnimationFunction::
  GenerateValuesForPathAndPoints(gfxFlattenedPath* aPath,
                                 bool aIsKeyPoints,
                                 nsTArray<double>& aPointDistances,
                                 nsTArray<nsSMILValue>& aResult)
{
  NS_ABORT_IF_FALSE(aResult.IsEmpty(), "outparam is non-empty");

  // If we're using "keyPoints" as our list of input distances, then we need
  // to de-normalize from the [0, 1] scale to the [0, totalPathLen] scale.
  double distanceMultiplier = aIsKeyPoints ? aPath->GetLength() : 1.0;
  const PRUint32 numPoints = aPointDistances.Length();
  for (PRUint32 i = 0; i < numPoints; ++i) {
    double curDist = aPointDistances[i] * distanceMultiplier;
    if (!aResult.AppendElement(
          SVGMotionSMILType::ConstructSMILValue(aPath, curDist,
                                                mRotateType, mRotateAngle))) {
      return false;
    }
  }
  return true;
}

nsresult
SVGMotionSMILAnimationFunction::GetValues(const nsISMILAttr& aSMILAttr,
                                          nsSMILValueArray& aResult)
{
  if (mIsPathStale) {
    RebuildPathAndVertices(aSMILAttr.GetTargetNode());
  }
  NS_ABORT_IF_FALSE(!mIsPathStale, "Forgot to clear 'is path stale' state");

  if (!mPath) {
    // This could be due to e.g. a parse error.
    NS_ABORT_IF_FALSE(mPathVertices.IsEmpty(), "have vertices but no path");
    return NS_ERROR_FAILURE;
  }
  NS_ABORT_IF_FALSE(!mPathVertices.IsEmpty(), "have a path but no vertices");

  // Now: Make the actual list of nsSMILValues (using keyPoints, if set)
  bool isUsingKeyPoints = !mKeyPoints.IsEmpty();
  bool success = GenerateValuesForPathAndPoints(mPath, isUsingKeyPoints,
                                                  isUsingKeyPoints ?
                                                  mKeyPoints : mPathVertices,
                                                  aResult);
  if (!success) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

void
SVGMotionSMILAnimationFunction::
  CheckValueListDependentAttrs(PRUint32 aNumValues)
{
  // Call superclass method.
  nsSMILAnimationFunction::CheckValueListDependentAttrs(aNumValues);

  // Added behavior: Do checks specific to keyPoints.
  CheckKeyPoints();
}

bool
SVGMotionSMILAnimationFunction::IsToAnimation() const
{
  // Rely on inherited method, but not if we have an <mpath> child or a |path|
  // attribute, because they'll override any 'to' attr we might have.
  // NOTE: We can't rely on mPathSourceType, because it might not have been
  // set to a useful value yet (or it might be stale).
  return !GetFirstMpathChild(&mAnimationElement->AsElement()) &&
    !HasAttr(nsGkAtoms::path) &&
    nsSMILAnimationFunction::IsToAnimation();
}

void
SVGMotionSMILAnimationFunction::CheckKeyPoints()
{
  if (!HasAttr(nsGkAtoms::keyPoints))
    return;

  // attribute is ignored for calcMode="paced" (even if it's got errors)
  if (GetCalcMode() == CALC_PACED) {
    SetKeyPointsErrorFlag(false);
  }

  if (mKeyPoints.IsEmpty()) {
    // keyPoints attr is set, but array is empty => it failed preliminary checks
    SetKeyPointsErrorFlag(true);
    return;
  }

  // Nothing else to check -- we can catch all keyPoints errors elsewhere.
  // -  Formatting & range issues will be caught in SetKeyPoints, and will
  //  result in an empty mKeyPoints array, which will drop us into the error
  //  case above.
  // -  Number-of-entries issues will be caught in CheckKeyTimes (and flagged
  //  as a problem with |keyTimes|), since we use our keyPoints entries to
  //  populate the "values" list, and that list's count gets passed to
  //  CheckKeyTimes.
}

nsresult
SVGMotionSMILAnimationFunction::SetKeyPoints(const nsAString& aKeyPoints,
                                             nsAttrValue& aResult)
{
  mKeyPoints.Clear();
  aResult.SetTo(aKeyPoints);

  nsresult rv =
    nsSMILParserUtils::ParseSemicolonDelimitedProgressList(aKeyPoints, false,
                                                           mKeyPoints);

  if (NS_SUCCEEDED(rv) && mKeyPoints.Length() < 1)
    rv = NS_ERROR_FAILURE;

  if (NS_FAILED(rv)) {
    mKeyPoints.Clear();
  }

  mHasChanged = true;

  return NS_OK;
}

void
SVGMotionSMILAnimationFunction::UnsetKeyPoints()
{
  mKeyPoints.Clear();
  SetKeyPointsErrorFlag(false);
  mHasChanged = true;
}

nsresult
SVGMotionSMILAnimationFunction::SetRotate(const nsAString& aRotate,
                                          nsAttrValue& aResult)
{
  mHasChanged = true;

  aResult.SetTo(aRotate);
  if (aRotate.EqualsLiteral("auto")) {
    mRotateType = eRotateType_Auto;
  } else if (aRotate.EqualsLiteral("auto-reverse")) {
    mRotateType = eRotateType_AutoReverse;
  } else {
    mRotateType = eRotateType_Explicit;

    // Parse numeric angle string, with the help of a temp nsSVGAngle.
    nsSVGAngle svgAngle;
    svgAngle.Init();
    nsresult rv = svgAngle.SetBaseValueString(aRotate, nsnull, false);
    if (NS_FAILED(rv)) { // Parse error
      mRotateAngle = 0.0f; // set default rotate angle
      // XXX report to console?
      return rv;
    }

    mRotateAngle = svgAngle.GetBaseValInSpecifiedUnits();

    // Convert to radian units, if we're not already in radians.
    PRUint8 angleUnit = svgAngle.GetBaseValueUnit();
    if (angleUnit != nsIDOMSVGAngle::SVG_ANGLETYPE_RAD) {
      mRotateAngle *= nsSVGAngle::GetDegreesPerUnit(angleUnit) /
        nsSVGAngle::GetDegreesPerUnit(nsIDOMSVGAngle::SVG_ANGLETYPE_RAD);
    }
  }
  return NS_OK;
}

void
SVGMotionSMILAnimationFunction::UnsetRotate()
{
  mRotateAngle = 0.0f; // default value
  mRotateType = eRotateType_Explicit;
  mHasChanged = true;
}

} // namespace mozilla
