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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "TestHarness.h"

#include "nsRect.h"
#ifdef XP_WIN
#include <windows.h>
#endif

template <class RectType>
static PRBool
TestConstructors()
{
  // Create a rectangle
  RectType  rect1(10, 20, 30, 40);

  // Make sure the rectangle was properly initialized
  if ((rect1.x != 10) || (rect1.y != 20) ||
      (rect1.width != 30) || (rect1.height != 40)) {
    fail("[1] Make sure the rectangle was properly initialized with constructor");
    return PR_FALSE;
  }

  // Create a second rect using the copy constructor
  RectType  rect2(rect1);

  // Make sure the rectangle was properly initialized
  if ((rect2.x != rect1.x) || (rect2.y != rect1.y) ||
      (rect2.width != rect1.width) || (rect2.height != rect1.height)) {
    fail("[2] Make sure the rectangle was properly initialized with copy constructor");
    return PR_FALSE;
  }

  passed("TestConstructors");
  return PR_TRUE;
}

template <class RectType>
static PRBool
TestEqualityOperator()
{
  RectType  rect1(10, 20, 30, 40);
  RectType  rect2(rect1);

  // Test the equality operator
  if (!(rect1 == rect2)) {
    fail("[1] Test the equality operator");
    return PR_FALSE;
  }

  // Test the inequality operator
  if (rect1 != rect2) {
    fail("[2] Test the inequality operator");
    return PR_FALSE;
  }

  // Make sure that two empty rects are equal
  rect1.Empty();
  rect2.Empty();
  if (!(rect1 == rect2)) {
    fail("[3] Make sure that two empty rects are equal");
    return PR_FALSE;
  }

  passed("TestEqualityOperator");
  return PR_TRUE;
}

template <class RectType>
static PRBool
TestContainment()
{
  RectType  rect1(10, 10, 50, 50);

  // Test the point containment methods
  //

  // Basic test of a point in the middle of the rect
  if (!rect1.Contains(rect1.x + rect1.width/2, rect1.y + rect1.height/2)) {
    fail("[1] Basic test of a point in the middle of the rect");
    return PR_FALSE;
  }

  // Test against a point at the left/top edges
  if (!rect1.Contains(rect1.x, rect1.y)) {
    fail("[2] Test against a point at the left/top edges");
    return PR_FALSE;
  }

  // Test against a point at the right/bottom extents
  if (rect1.Contains(rect1.XMost(), rect1.YMost())) {
    fail("[3] Test against a point at the right/bottom extents");
    return PR_FALSE;
  }

  // Test the rect containment methods
  //
  RectType  rect2(rect1);

  // Test against a rect that's the same as rect1
  if (!rect1.Contains(rect2)) {
    fail("[4] Test against a rect that's the same as rect1");
    return PR_FALSE;
  }

  // Test against a rect whose left edge (only) is outside of rect1
  rect2.x--;
  if (rect1.Contains(rect2)) {
    fail("[5] Test against a rect whose left edge (only) is outside of rect1");
    return PR_FALSE;
  }
  rect2.x++;

  // Test against a rect whose top edge (only) is outside of rect1
  rect2.y--;
  if (rect1.Contains(rect2)) {
    fail("[6] Test against a rect whose top edge (only) is outside of rect1");
    return PR_FALSE;
  }
  rect2.y++;

  // Test against a rect whose right edge (only) is outside of rect1
  rect2.x++;
  if (rect1.Contains(rect2)) {
    fail("[7] Test against a rect whose right edge (only) is outside of rect1");
    return PR_FALSE;
  }
  rect2.x--;

  // Test against a rect whose bottom edge (only) is outside of rect1
  rect2.y++;
  if (rect1.Contains(rect2)) {
    fail("[8] Test against a rect whose bottom edge (only) is outside of rect1");
    return PR_FALSE;
  }
  rect2.y--;

  passed("TestContainment");
  return PR_TRUE;
}

// Test the method that returns a boolean result but doesn't return a
// a rectangle
template <class RectType>
static PRBool
TestIntersects()
{
  RectType  rect1(10, 10, 50, 50);
  RectType  rect2(rect1);

  // Test against a rect that's the same as rect1
  if (!rect1.Intersects(rect2)) {
    fail("[1] Test against a rect that's the same as rect1");
    return PR_FALSE;
  }

  // Test against a rect that's enclosed by rect1
  rect2.Inflate(-1, -1);
  if (!rect1.Contains(rect2) || !rect1.Intersects(rect2)) {
    fail("[2] Test against a rect that's enclosed by rect1");
    return PR_FALSE;
  }
  rect2.Inflate(1, 1);

  // Make sure inflate and deflate worked correctly
  if (rect1 != rect2) {
    fail("[3] Make sure inflate and deflate worked correctly");
    return PR_FALSE;
  }

  // Test against a rect that overlaps the left edge of rect1
  rect2.x--;
  if (!rect1.Intersects(rect2)) {
    fail("[4] Test against a rect that overlaps the left edge of rect1");
    return PR_FALSE;
  }
  rect2.x++;

  // Test against a rect that's outside of rect1 on the left
  rect2.x -= rect2.width;
  if (rect1.Intersects(rect2)) {
    fail("[5] Test against a rect that's outside of rect1 on the left");
    return PR_FALSE;
  }
  rect2.x += rect2.width;

  // Test against a rect that overlaps the top edge of rect1
  rect2.y--;
  if (!rect1.Intersects(rect2)) {
    fail("[6] Test against a rect that overlaps the top edge of rect1");
    return PR_FALSE;
  }
  rect2.y++;

  // Test against a rect that's outside of rect1 on the top
  rect2.y -= rect2.height;
  if (rect1.Intersects(rect2)) {
    fail("[7] Test against a rect that's outside of rect1 on the top");
    return PR_FALSE;
  }
  rect2.y += rect2.height;

  // Test against a rect that overlaps the right edge of rect1
  rect2.x++;
  if (!rect1.Intersects(rect2)) {
    fail("[8] Test against a rect that overlaps the right edge of rect1");
    return PR_FALSE;
  }
  rect2.x--;

  // Test against a rect that's outside of rect1 on the right
  rect2.x += rect2.width;
  if (rect1.Intersects(rect2)) {
    fail("[9] Test against a rect that's outside of rect1 on the right");
    return PR_FALSE;
  }
  rect2.x -= rect2.width;

  // Test against a rect that overlaps the bottom edge of rect1
  rect2.y++;
  if (!rect1.Intersects(rect2)) {
    fail("[10] Test against a rect that overlaps the bottom edge of rect1");
    return PR_FALSE;
  }
  rect2.y--;

  // Test against a rect that's outside of rect1 on the bottom
  rect2.y += rect2.height;
  if (rect1.Intersects(rect2)) {
    fail("[11] Test against a rect that's outside of rect1 on the bottom");
    return PR_FALSE;
  }
  rect2.y -= rect2.height;

  passed("TestIntersects");
  return PR_TRUE;
}

// Test the method that returns a boolean result and an intersection rect
template <class RectType>
static PRBool
TestIntersection()
{
  RectType  rect1(10, 10, 50, 50);
  RectType  rect2(rect1);
  RectType  dest;

  // Test against a rect that's the same as rect1
  if (!dest.IntersectRect(rect1, rect2) || (dest != rect1)) {
    fail("[1] Test against a rect that's the same as rect1");
    return PR_FALSE;
  }

  // Test against a rect that's enclosed by rect1
  rect2.Inflate(-1, -1);
  if (!dest.IntersectRect(rect1, rect2) || (dest != rect2)) {
    fail("[2] Test against a rect that's enclosed by rect1");
    return PR_FALSE;
  }
  rect2.Inflate(1, 1);

  // Test against a rect that overlaps the left edge of rect1
  rect2.x--;
  if (!dest.IntersectRect(rect1, rect2) ||
     (dest != RectType(rect1.x, rect1.y, rect1.width - 1, rect1.height))) {
    fail("[3] Test against a rect that overlaps the left edge of rect1");
    return PR_FALSE;
  }
  rect2.x++;

  // Test against a rect that's outside of rect1 on the left
  rect2.x -= rect2.width;
  if (dest.IntersectRect(rect1, rect2)) {
    fail("[4] Test against a rect that's outside of rect1 on the left");
    return PR_FALSE;
  }
  // Make sure an empty rect is returned
  if (!dest.IsEmpty()) {
    fail("[4] Make sure an empty rect is returned");
    return PR_FALSE;
  }
  rect2.x += rect2.width;

  // Test against a rect that overlaps the top edge of rect1
  rect2.y--;
  if (!dest.IntersectRect(rect1, rect2) ||
     (dest != RectType(rect1.x, rect1.y, rect1.width, rect1.height - 1))) {
    fail("[5] Test against a rect that overlaps the top edge of rect1");
    return PR_FALSE;
  }
  rect2.y++;

  // Test against a rect that's outside of rect1 on the top
  rect2.y -= rect2.height;
  if (dest.IntersectRect(rect1, rect2)) {
    fail("[6] Test against a rect that's outside of rect1 on the top");
    return PR_FALSE;
  }
  // Make sure an empty rect is returned
  if (!dest.IsEmpty()) {
    fail("[6] Make sure an empty rect is returned");
    return PR_FALSE;
  }
  rect2.y += rect2.height;

  // Test against a rect that overlaps the right edge of rect1
  rect2.x++;
  if (!dest.IntersectRect(rect1, rect2) ||
     (dest != RectType(rect1.x + 1, rect1.y, rect1.width - 1, rect1.height))) {
    fail("[7] Test against a rect that overlaps the right edge of rect1");
    return PR_FALSE;
  }
  rect2.x--;

  // Test against a rect that's outside of rect1 on the right
  rect2.x += rect2.width;
  if (dest.IntersectRect(rect1, rect2)) {
    fail("[8] Test against a rect that's outside of rect1 on the right");
    return PR_FALSE;
  }
  // Make sure an empty rect is returned
  if (!dest.IsEmpty()) {
    fail("[8] Make sure an empty rect is returned");
    return PR_FALSE;
  }
  rect2.x -= rect2.width;

  // Test against a rect that overlaps the bottom edge of rect1
  rect2.y++;
  if (!dest.IntersectRect(rect1, rect2) ||
     (dest != RectType(rect1.x, rect1.y + 1, rect1.width, rect1.height - 1))) {
    fail("[9] Test against a rect that overlaps the bottom edge of rect1");
    return PR_FALSE;
  }
  rect2.y--;

  // Test against a rect that's outside of rect1 on the bottom
  rect2.y += rect2.height;
  if (dest.IntersectRect(rect1, rect2)) {
    fail("[10] Test against a rect that's outside of rect1 on the bottom");
    return PR_FALSE;
  }
  // Make sure an empty rect is returned
  if (!dest.IsEmpty()) {
    fail("[10] Make sure an empty rect is returned");
    return PR_FALSE;
  }
  rect2.y -= rect2.height;

  // Test against a rect with zero width or height
  rect1.SetRect(100, 100, 100, 100);
  rect2.SetRect(150, 100, 0, 100);
  if (dest.IntersectRect(rect1, rect2) || !dest.IsEmpty()) {
    fail("[11] Intersection of rects with zero width or height should be empty");
    return PR_FALSE;
  }

  // Tests against a rect with negative width or height
  //

  // Test against a rect with negative width
  rect1.SetRect(100, 100, 100, 100);
  rect2.SetRect(100, 100, -100, 100);
  if (dest.IntersectRect(rect1, rect2) || !dest.IsEmpty()) {
    fail("[12] Intersection of rects with negative width or height should be empty");
    return PR_FALSE;
  }

  // Those two rects exactly overlap in some way...
  // but we still want to return an empty rect
  rect1.SetRect(100, 100, 100, 100);
  rect2.SetRect(200, 200, -100, -100);
  if (dest.IntersectRect(rect1, rect2) || !dest.IsEmpty()) {
    fail("[13] Intersection of rects with negative width or height should be empty");
    return PR_FALSE;
  }

  // Test against two identical rects with negative height
  rect1.SetRect(100, 100, 100, -100);
  rect2.SetRect(100, 100, 100, -100);
  if (dest.IntersectRect(rect1, rect2) || !dest.IsEmpty()) {
    fail("[14] Intersection of rects with negative width or height should be empty");
    return PR_FALSE;
  }

  passed("TestIntersection");
  return PR_TRUE;
}

template <class RectType>
static PRBool
TestUnion()
{
  RectType  rect1;
  RectType  rect2(10, 10, 50, 50);
  RectType  dest;

  // Check the case where the receiver is an empty rect
  rect1.Empty();
  if (!dest.UnionRect(rect1, rect2) || (dest != rect2)) {
    fail("[1] Check the case where the receiver is an empty rect");
    return PR_FALSE;
  }

  // Check the case where the source rect is an empty rect
  rect1 = rect2;
  rect2.Empty();
  if (!dest.UnionRect(rect1, rect2) || (dest != rect1)) {
    fail("[2] Check the case where the source rect is an empty rect");
    return PR_FALSE;
  }

  // Test the case where both rects are empty
  rect1.Empty();
  rect2.Empty();
  if (dest.UnionRect(rect1, rect2)) {
    fail("[3] Test the case where both rects are empty");
    return PR_FALSE;
  }

  // Test union case where the two rects don't overlap at all
  rect1.SetRect(10, 10, 50, 50);
  rect2.SetRect(100, 100, 50, 50);
  if (!dest.UnionRect(rect1, rect2) ||
     (dest != RectType(rect1.x, rect1.y, rect2.XMost() - rect1.x, rect2.YMost() - rect1.y))) {
    fail("[4] Test union case where the two rects don't overlap at all");
    return PR_FALSE;
  }

  // Test union case where the two rects overlap
  rect1.SetRect(30, 30, 50, 50);
  rect2.SetRect(10, 10, 50, 50);
  if (!dest.UnionRect(rect1, rect2) ||
      (dest != RectType(rect2.x, rect2.y, rect1.XMost() - rect2.x, rect1.YMost() - rect2.y))) {
    fail("[5] Test union case where the two rects overlap");
    return PR_FALSE;
  }

  passed("TestUnion");
  return PR_TRUE;
}

int main(int argc, char** argv)
{
  ScopedXPCOM xpcom("TestRect");
  if (xpcom.failed())
    return -1;

  int rv = 0;

  //-----------------------
  // Test nsRect
  //
  printf("===== nsRect tests =====\n");

  if (!TestConstructors<nsRect>())
    rv = -1;

  if (!TestEqualityOperator<nsRect>())
    rv = -1;

  if (!TestContainment<nsRect>())
    rv = -1;

  if (!TestIntersects<nsRect>())
    rv = -1;

  if (!TestIntersection<nsRect>())
    rv = -1;

  if (!TestUnion<nsRect>())
    rv = -1;

  //-----------------------
  // Test nsIntRect
  //
  printf("===== nsIntRect tests =====\n");
 
  if (!TestConstructors<nsIntRect>())
    rv = -1;

  if (!TestEqualityOperator<nsIntRect>())
    rv = -1;

  if (!TestContainment<nsIntRect>())
    rv = -1;

  if (!TestIntersects<nsIntRect>())
    rv = -1;

  if (!TestIntersection<nsIntRect>())
    rv = -1;

  if (!TestUnion<nsIntRect>())
    rv = -1;

  return rv;
}
