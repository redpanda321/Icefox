/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef RELATION_H_
#define RELATION_H_

#include "AccIterator.h"

namespace mozilla {
namespace a11y {

/**
 * This class is used to return Relation objects from functions.  A copy
 * constructor doesn't work here because we need to mutate the old relation to
 * have its nsAutoPtr forget what it points to.
 */
struct RelationCopyHelper
{
  RelationCopyHelper(mozilla::a11y::AccIterable* aFirstIter,
                     mozilla::a11y::AccIterable* aLastIter) :
    mFirstIter(aFirstIter), mLastIter(aLastIter) { }

  mozilla::a11y::AccIterable* mFirstIter;
  mozilla::a11y::AccIterable* mLastIter;
};

/**
 * A collection of relation targets of a certain type.  Targets are computed
 * lazily while enumerating.
 */
class Relation
{
public:
  Relation() : mFirstIter(nullptr), mLastIter(nullptr) { }

  Relation(const RelationCopyHelper aRelation) :
    mFirstIter(aRelation.mFirstIter), mLastIter(aRelation.mLastIter) { }

  Relation(mozilla::a11y::AccIterable* aIter) :
    mFirstIter(aIter), mLastIter(aIter) { }

  Relation(Accessible* aAcc) :
    mFirstIter(nullptr), mLastIter(nullptr)
    { AppendTarget(aAcc); }

  Relation(DocAccessible* aDocument, nsIContent* aContent) :
    mFirstIter(nullptr), mLastIter(nullptr)
    { AppendTarget(aDocument, aContent); }

  Relation& operator = (const RelationCopyHelper& aRH)
  {
    mFirstIter = aRH.mFirstIter;
    mLastIter = aRH.mLastIter;
    return *this;
  }

  Relation& operator = (Relation& aRelation)
  {
    mFirstIter = aRelation.mFirstIter;
    mLastIter = aRelation.mLastIter;
    return *this;
  }

  operator RelationCopyHelper()
  {
    return RelationCopyHelper(mFirstIter.forget(), mLastIter);
  }

  inline void AppendIter(mozilla::a11y::AccIterable* aIter)
  {
    if (mLastIter)
      mLastIter->mNextIter = aIter;
    else
      mFirstIter = aIter;

    mLastIter = aIter;
  }

  /**
   * Append the given accessible to the set of related accessibles.
   */
  inline void AppendTarget(Accessible* aAcc)
  {
    if (aAcc)
      AppendIter(new mozilla::a11y::SingleAccIterator(aAcc));
  }

  /**
   * Append the one accessible for this content node to the set of related
   * accessibles.
   */
  void AppendTarget(DocAccessible* aDocument, nsIContent* aContent)
  {
    if (aContent)
      AppendTarget(aDocument->GetAccessible(aContent));
  }

  /**
   * compute and return the next related accessible.
   */
  inline Accessible* Next()
  {
    Accessible* target = nullptr;

    // a trick nsAutoPtr deletes what it used to point to when assigned to
    while (mFirstIter && !(target = mFirstIter->Next()))
      mFirstIter = mFirstIter->mNextIter;

    if (!mFirstIter)
      mLastIter = nullptr;

    return target;
  }

private:
  Relation& operator = (const Relation&);

  nsAutoPtr<mozilla::a11y::AccIterable> mFirstIter;
  mozilla::a11y::AccIterable* mLastIter;
};

} // namespace a11y
} // namespace mozilla

#endif

