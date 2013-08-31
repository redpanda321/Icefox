/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NSMARGIN_H
#define NSMARGIN_H

#include "nsCoord.h"
#include "nsPoint.h"
#include "gfxCore.h"
#include "mozilla/gfx/BaseMargin.h"

struct nsMargin : public mozilla::gfx::BaseMargin<nscoord, nsMargin> {
  typedef mozilla::gfx::BaseMargin<nscoord, nsMargin> Super;

  // Constructors
  nsMargin() : Super() {}
  nsMargin(const nsMargin& aMargin) : Super(aMargin) {}
  nsMargin(nscoord aLeft,  nscoord aTop, nscoord aRight, nscoord aBottom)
    : Super(aLeft, aTop, aRight, aBottom) {}
};

struct nsIntMargin : public mozilla::gfx::BaseMargin<int32_t, nsIntMargin> {
  typedef mozilla::gfx::BaseMargin<int32_t, nsIntMargin> Super;

  // Constructors
  nsIntMargin() : Super() {}
  nsIntMargin(const nsIntMargin& aMargin) : Super(aMargin) {}
  nsIntMargin(int32_t aLeft,  int32_t aTop, int32_t aRight, int32_t aBottom)
    : Super(aLeft, aTop, aRight, aBottom) {}
};

#endif /* NSMARGIN_H */
