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
 * The Original Code is the Mozilla SMIL module.
 *
 * The Initial Developer of the Original Code is Brian Birtles.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brian Birtles <birtles@gmail.com>
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

#include "nsSMILKeySpline.h"
#include "prtypes.h"
#include <math.h>

#define NEWTON_ITERATIONS          4
#define NEWTON_MIN_SLOPE           0.02
#define SUBDIVISION_PRECISION      0.0000001
#define SUBDIVISION_MAX_ITERATIONS 10

const double nsSMILKeySpline::kSampleStepSize =
                                        1.0 / double(kSplineTableSize - 1);

void
nsSMILKeySpline::Init(double aX1,
                      double aY1,
                      double aX2,
                      double aY2)
{
  mX1 = aX1;
  mY1 = aY1;
  mX2 = aX2;
  mY2 = aY2;

  if (mX1 != mY1 || mX2 != mY2)
    CalcSampleValues();
}

double
nsSMILKeySpline::GetSplineValue(double aX) const
{
  if (mX1 == mY1 && mX2 == mY2)
    return aX;

  return CalcBezier(GetTForX(aX), mY1, mY2);
}

void
nsSMILKeySpline::CalcSampleValues()
{
  for (PRUint32 i = 0; i < kSplineTableSize; ++i) {
    mSampleValues[i] = CalcBezier(double(i) * kSampleStepSize, mX1, mX2);
  }
}

/*static*/ double
nsSMILKeySpline::CalcBezier(double aT,
                            double aA1,
                            double aA2)
{
  // use Horner's scheme to evaluate the Bezier polynomial
  return ((A(aA1, aA2)*aT + B(aA1, aA2))*aT + C(aA1))*aT;
}

/*static*/ double
nsSMILKeySpline::GetSlope(double aT,
                          double aA1,
                          double aA2)
{
  return 3.0 * A(aA1, aA2)*aT*aT + 2.0 * B(aA1, aA2) * aT + C(aA1);
}

double
nsSMILKeySpline::GetTForX(double aX) const
{
  // Find interval where t lies
  double intervalStart = 0.0;
  const double* currentSample = &mSampleValues[1];
  const double* const lastSample = &mSampleValues[kSplineTableSize - 1];
  for (; currentSample != lastSample && *currentSample <= aX;
        ++currentSample) {
    intervalStart += kSampleStepSize;
  }
  --currentSample; // t now lies between *currentSample and *currentSample+1

  // Interpolate to provide an initial guess for t
  double dist = (aX - *currentSample) /
                (*(currentSample+1) - *currentSample);
  double guessForT = intervalStart + dist * kSampleStepSize;

  // Check the slope to see what strategy to use. If the slope is too small
  // Newton-Raphson iteration won't converge on a root so we use bisection
  // instead.
  double initialSlope = GetSlope(guessForT, mX1, mX2);
  if (initialSlope >= NEWTON_MIN_SLOPE) {
    return NewtonRaphsonIterate(aX, guessForT);
  } else if (initialSlope == 0.0) {
    return guessForT;
  } else {
    return BinarySubdivide(aX, intervalStart, intervalStart + kSampleStepSize);
  }
}

double
nsSMILKeySpline::NewtonRaphsonIterate(double aX, double aGuessT) const
{
  // Refine guess with Newton-Raphson iteration
  for (PRUint32 i = 0; i < NEWTON_ITERATIONS; ++i) {
    // We're trying to find where f(t) = aX,
    // so we're actually looking for a root for: CalcBezier(t) - aX
    double currentX = CalcBezier(aGuessT, mX1, mX2) - aX;
    double currentSlope = GetSlope(aGuessT, mX1, mX2);

    if (currentSlope == 0.0)
      return aGuessT;

    aGuessT -= currentX / currentSlope;
  }

  return aGuessT;
}

double
nsSMILKeySpline::BinarySubdivide(double aX, double aA, double aB) const
{
  double currentX;
  double currentT;
  PRUint32 i = 0;

  do
  {
    currentT = aA + (aB - aA) / 2.0;
    currentX = CalcBezier(currentT, mX1, mX2) - aX;

    if (currentX > 0.0) {
      aB = currentT;
    } else {
      aA = currentT;
    }
  } while (fabs(currentX) > SUBDIVISION_PRECISION
           && ++i < SUBDIVISION_MAX_ITERATIONS);

  return currentT;
}
