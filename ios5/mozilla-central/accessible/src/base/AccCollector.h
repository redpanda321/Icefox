/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AccCollector_h_
#define AccCollector_h_

#include "filters.h"

#include "nscore.h"
#include "nsTArray.h"

/**
 * Collect accessible children complying with filter function. Provides quick
 * access to accessible by index.
 */
class AccCollector
{
public:
  AccCollector(Accessible* aRoot, filters::FilterFuncPtr aFilterFunc);
  virtual ~AccCollector();

  /**
   * Return accessible count within the collection.
   */
  PRUint32 Count();

  /**
   * Return an accessible from the collection at the given index.
   */
  Accessible* GetAccessibleAt(PRUint32 aIndex);

  /**
   * Return index of the given accessible within the collection.
   */
  virtual PRInt32 GetIndexAt(Accessible* aAccessible);

protected:
  /**
   * Ensure accessible at the given index is stored and return it.
   */
  Accessible* EnsureNGetObject(PRUint32 aIndex);

  /**
   * Ensure index for the given accessible is stored and return it.
   */
  PRInt32 EnsureNGetIndex(Accessible* aAccessible);

  /**
   * Append the object to collection.
   */
  virtual void AppendObject(Accessible* aAccessible);

  filters::FilterFuncPtr mFilterFunc;
  Accessible* mRoot;
  PRUint32 mRootChildIdx;

  nsTArray<Accessible*> mObjects;

private:
  AccCollector();
  AccCollector(const AccCollector&);
  AccCollector& operator =(const AccCollector&);
};

/**
 * Collect embedded objects. Provide quick access to accessible by index and
 * vice versa.
 */
class EmbeddedObjCollector : public AccCollector
{
public:
  virtual ~EmbeddedObjCollector() { };

public:
  virtual PRInt32 GetIndexAt(Accessible* aAccessible);

protected:
  // Make sure it's used by Accessible class only.
  EmbeddedObjCollector(Accessible* aRoot) :
    AccCollector(aRoot, filters::GetEmbeddedObject) { }

  virtual void AppendObject(Accessible* aAccessible);

  friend class Accessible;
};

#endif
