/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NS_SMILNULLTYPE_H_
#define NS_SMILNULLTYPE_H_

#include "nsISMILType.h"

class nsSMILNullType : public nsISMILType
{
public:
  // Singleton for nsSMILValue objects to hold onto.
  static nsSMILNullType sSingleton;

protected:
  // nsISMILType Methods
  // -------------------
  virtual void Init(nsSMILValue& aValue) const {}
  virtual void Destroy(nsSMILValue& aValue) const {}
  virtual nsresult Assign(nsSMILValue& aDest, const nsSMILValue& aSrc) const;

  // The remaining methods should never be called, so although they're very
  // simple they don't need to be inline.
  virtual bool     IsEqual(const nsSMILValue& aLeft,
                           const nsSMILValue& aRight) const;
  virtual nsresult Add(nsSMILValue& aDest, const nsSMILValue& aValueToAdd,
                       uint32_t aCount) const;
  virtual nsresult ComputeDistance(const nsSMILValue& aFrom,
                                   const nsSMILValue& aTo,
                                   double& aDistance) const;
  virtual nsresult Interpolate(const nsSMILValue& aStartVal,
                               const nsSMILValue& aEndVal,
                               double aUnitDistance,
                               nsSMILValue& aResult) const;

private:
  // Private constructor & destructor: prevent instances beyond my singleton,
  // and prevent others from deleting my singleton.
  nsSMILNullType()  {}
  ~nsSMILNullType() {}
};

#endif // NS_SMILNULLTYPE_H_
