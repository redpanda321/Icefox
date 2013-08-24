/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
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
 * The Original Code is C++ priority queue implementation.
 *
 * The Initial Developer of the Original Code is Brian Birtles.
 * Portions created by the Initial Developer are Copyright (C) 2009
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

#ifndef NS_TPRIORITY_QUEUE_H_
#define NS_TPRIORITY_QUEUE_H_

#include "nsTArray.h"
#include "nsDebug.h"

/**
 * A templatized priority queue data structure that uses an nsTArray to serve as
 * a binary heap. The default comparator causes this to act like a min-heap.
 * Only the LessThan method of the comparator is used.
 */
template<class T, class Compare = nsDefaultComparator<T, T> >
class nsTPriorityQueue
{
public:
  typedef typename nsTArray<T>::size_type size_type;

  /**
   * Default constructor also creates a comparator object using the default
   * constructor for type Compare.
   */
  nsTPriorityQueue() : mCompare(Compare()) { }

  /**
   * Constructor to allow a specific instance of a comparator object to be
   * used.
   */
  nsTPriorityQueue(const Compare& aComp) : mCompare(aComp) { }

  /**
   * Copy constructor
   */
  nsTPriorityQueue(const nsTPriorityQueue& aOther)
    : mElements(aOther.mElements),
      mCompare(aOther.mCompare)
  { }

  /**
   * @return True if the queue is empty or false otherwise.
   */
  PRBool IsEmpty() const
  {
    return mElements.IsEmpty();
  }

  /**
   * @return The number of elements in the queue.
   */
  size_type Length() const
  {
    return mElements.Length();
  }

  /**
   * @return The topmost element in the queue without changing the queue. This
   * is the element 'a' such that there is no other element 'b' in the queue for
   * which Compare(b, a) returns PR_TRUE. (Since this container does not check
   * for duplicate entries there may exist 'b' for which Compare(a, b) returns
   * PR_FALSE.)
   */
  const T& Top() const
  {
    NS_ABORT_IF_FALSE(!mElements.IsEmpty(), "Empty queue");
    return mElements[0];
  }

  /**
   * Adds an element to the queue
   * @param aElement The element to add
   * @return PR_TRUE on success, PR_FALSE on out of memory.
   */
  PRBool Push(const T& aElement)
  {
    T* elem = mElements.AppendElement(aElement);
    if (!elem)
      return PR_FALSE; // Out of memory

    // Sift up
    size_type i = mElements.Length() - 1;
    while (i) {
      size_type parent = (size_type)((i - 1) / 2);
      if (mCompare.LessThan(mElements[parent], mElements[i])) {
        break;
      }
      Swap(i, parent);
      i = parent;
    }

    return PR_TRUE;
  }

  /**
   * Removes and returns the top-most element from the queue.
   * @return The topmost element, that is, the element 'a' such that there is no
   * other element 'b' in the queue for which Compare(b, a) returns PR_TRUE.
   * @see Top()
   */
  T Pop()
  {
    NS_ABORT_IF_FALSE(!mElements.IsEmpty(), "Empty queue");
    T pop = mElements[0];

    // Move last to front
    mElements[0] = mElements[mElements.Length() - 1];
    mElements.TruncateLength(mElements.Length() - 1);

    // Sift down
    size_type i = 0;
    while (2*i + 1 < mElements.Length()) {
      size_type swap = i;
      size_type l_child = 2*i + 1;
      if (mCompare.LessThan(mElements[l_child], mElements[i])) {
        swap = l_child;
      }
      size_type r_child = l_child + 1;
      if (r_child < mElements.Length() &&
          mCompare.LessThan(mElements[r_child], mElements[swap])) {
        swap = r_child;
      }
      if (swap == i) {
        break;
      }
      Swap(i, swap);
      i = swap;
    }

    return pop;
  }

  /**
   * Removes all elements from the queue.
   */
  void Clear()
  {
    mElements.Clear();
  }

  /**
   * Provides readonly access to the queue elements as an array. Generally this
   * should be avoided but may be needed in some situations such as when the
   * elements contained in the queue need to be enumerated for cycle-collection.
   * @return A pointer to the first element of the array.  If the array is
   * empty, then this pointer must not be dereferenced.
   */
  const T* Elements() const
  {
    return mElements.Elements();
  }

protected:
  /**
   * Swaps the elements at the specified indices.
   */
  void Swap(size_type aIndexA, size_type aIndexB)
  {
    T temp = mElements[aIndexA];
    mElements[aIndexA] = mElements[aIndexB];
    mElements[aIndexB] = temp;
  }

  nsTArray<T> mElements;
  Compare mCompare; // Comparator object
};

#endif // NS_TPRIORITY_QUEUE_H_
