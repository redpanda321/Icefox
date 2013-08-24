/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>
#include "prbit.h"
#include "nsSupportsArray.h"
#include "nsSupportsArrayEnumerator.h"
#include "nsAString.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"

#if DEBUG_SUPPORTSARRAY
#define MAXSUPPORTS 20

class SupportsStats {
public:
  SupportsStats();
  ~SupportsStats();

};

static int sizesUsed; // number of the elements of the arrays used
static int sizesAlloced[MAXSUPPORTS]; // sizes of the allocations.  sorted
static int NumberOfSize[MAXSUPPORTS]; // number of this allocation size (1 per array)
static int AllocedOfSize[MAXSUPPORTS]; // number of this allocation size (each size for array used)
static int GrowInPlace[MAXSUPPORTS];

// these are per-allocation
static int MaxElements[3000];

// very evil
#define ADD_TO_STATS(x,size) do {int i; for (i = 0; i < sizesUsed; i++) \
                                  { \
                                    if (sizesAlloced[i] == (int)(size)) \
                                    { ((x)[i])++; break; } \
                                  } \
                                  if (i >= sizesUsed && sizesUsed < MAXSUPPORTS) \
                                  { sizesAlloced[sizesUsed] = (size); \
                                    ((x)[sizesUsed++])++; break; \
                                  } \
                                } while (0);

#define SUB_FROM_STATS(x,size) do {int i; for (i = 0; i < sizesUsed; i++) \
                                    { \
                                      if (sizesAlloced[i] == (int)(size)) \
                                      { ((x)[i])--; break; } \
                                    } \
                                  } while (0);


SupportsStats::SupportsStats()
{
  sizesUsed = 1;
  sizesAlloced[0] = 0;
}

SupportsStats::~SupportsStats()
{
  int i;
  for (i = 0; i < sizesUsed; i++)
  {
    printf("Size %d:\n",sizesAlloced[i]);
    printf("\tNumber of SupportsArrays this size (max):     %d\n",NumberOfSize[i]);
    printf("\tNumber of allocations this size (total):  %d\n",AllocedOfSize[i]);
    printf("\tNumber of GrowsInPlace this size (total): %d\n",GrowInPlace[i]);
  }
  printf("Max Size of SupportsArray:\n");
  for (i = 0; i < (int)(sizeof(MaxElements)/sizeof(MaxElements[0])); i++)
  {
    if (MaxElements[i])
      printf("\t%d: %d\n",i,MaxElements[i]);
  }
}

// Just so constructor/destructor get called
SupportsStats gSupportsStats;
#endif

nsresult
nsQueryElementAt::operator()( const nsIID& aIID, void** aResult ) const
  {
    nsresult status = mCollection
                        ? mCollection->QueryElementAt(mIndex, aIID, aResult)
                        : NS_ERROR_NULL_POINTER;

    if ( mErrorPtr )
      *mErrorPtr = status;

    return status;
  }

static const PRInt32 kGrowArrayBy = 8;
static const PRInt32 kLinearThreshold = 16 * sizeof(nsISupports *);

nsSupportsArray::nsSupportsArray()
{
  mArray = mAutoArray;
  mArraySize = kAutoArraySize;
  mCount = 0;
#if DEBUG_SUPPORTSARRAY
  mMaxCount = 0;
  mMaxSize = 0;
  ADD_TO_STATS(NumberOfSize,kAutoArraySize*sizeof(mArray[0]));
  MaxElements[0]++;
#endif
}

nsSupportsArray::~nsSupportsArray()
{
  DeleteArray();
}

bool nsSupportsArray::GrowArrayBy(PRInt32 aGrowBy)
{
  // We have to grow the array. Grow by kGrowArrayBy slots if we're smaller
  // than kLinearThreshold bytes, or a power of two if we're larger.
  // This is much more efficient with most memory allocators, especially
  // if it's very large, or of the allocator is binned.
  if (aGrowBy < kGrowArrayBy)
    aGrowBy = kGrowArrayBy;

  PRUint32 newCount = mArraySize + aGrowBy;  // Minimum increase
  PRUint32 newSize = sizeof(mArray[0]) * newCount;

  if (newSize >= (PRUint32) kLinearThreshold)
  {
    // newCount includes enough space for at least kGrowArrayBy new slots.
    // Select the next power-of-two size in bytes above that if newSize is
    // not a power of two.
    if (newSize & (newSize - 1))
      newSize = PR_BIT(PR_CeilingLog2(newSize));

    newCount = newSize / sizeof(mArray[0]);
  }
  // XXX This would be far more efficient in many allocators if we used
  // XXX PR_Realloc(), etc
  nsISupports** oldArray = mArray;

  mArray = new nsISupports*[newCount];
  if (!mArray) {                    // ran out of memory
    mArray = oldArray;
    return false;
  }
  mArraySize = newCount;

#if DEBUG_SUPPORTSARRAY
  if (oldArray == mArray) // can't happen without use of realloc
    ADD_TO_STATS(GrowInPlace,mCount);
  ADD_TO_STATS(AllocedOfSize,mArraySize*sizeof(mArray[0]));
  if (mArraySize > mMaxSize)
  {
    ADD_TO_STATS(NumberOfSize,mArraySize*sizeof(mArray[0]));
    if (oldArray != &(mAutoArray[0]))
      SUB_FROM_STATS(NumberOfSize,mCount*sizeof(mArray[0]));
    mMaxSize = mArraySize;
  }
#endif
  if (oldArray) {                   // need to move old data
    if (0 < mCount) {
      ::memcpy(mArray, oldArray, mCount * sizeof(nsISupports*));
    }
    if (oldArray != &(mAutoArray[0])) {
      delete[] oldArray;
    }
  }

  return true;
}

nsresult
nsSupportsArray::Create(nsISupports *aOuter, REFNSIID aIID, void **aResult)
{
  if (aOuter)
    return NS_ERROR_NO_AGGREGATION;

  nsCOMPtr<nsISupportsArray> it = new nsSupportsArray();
  if (!it)
    return NS_ERROR_OUT_OF_MEMORY;

  return it->QueryInterface(aIID, aResult);
}

NS_IMPL_THREADSAFE_ISUPPORTS3(nsSupportsArray, nsISupportsArray, nsICollection, nsISerializable)

NS_IMETHODIMP
nsSupportsArray::Read(nsIObjectInputStream *aStream)
{
  nsresult rv;

  PRUint32 newArraySize;
  rv = aStream->Read32(&newArraySize);

  if (newArraySize <= kAutoArraySize) {
    if (mArray != mAutoArray) {
      delete[] mArray;
      mArray = mAutoArray;
    }
    newArraySize = kAutoArraySize;
  }
  else {
    if (newArraySize <= mArraySize) {
      // Keep non-default-size mArray, it's more than big enough.
      newArraySize = mArraySize;
    }
    else {
      nsISupports** array = new nsISupports*[newArraySize];
      if (!array)
        return NS_ERROR_OUT_OF_MEMORY;
      if (mArray != mAutoArray)
        delete[] mArray;
      mArray = array;
    }
  }
  mArraySize = newArraySize;

  rv = aStream->Read32(&mCount);
  if (NS_FAILED(rv)) return rv;

  NS_ASSERTION(mCount <= mArraySize, "overlarge mCount!");
  if (mCount > mArraySize)
    mCount = mArraySize;

  for (PRUint32 i = 0; i < mCount; i++) {
    rv = aStream->ReadObject(true, &mArray[i]);
    if (NS_FAILED(rv)) return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSupportsArray::Write(nsIObjectOutputStream *aStream)
{
  nsresult rv;

  rv = aStream->Write32(mArraySize);
  if (NS_FAILED(rv)) return rv;

  rv = aStream->Write32(mCount);
  if (NS_FAILED(rv)) return rv;

  for (PRUint32 i = 0; i < mCount; i++) {
    rv = aStream->WriteObject(mArray[i], true);
    if (NS_FAILED(rv)) return rv;
  }

  return NS_OK;
}

void nsSupportsArray::DeleteArray(void)
{
  Clear();
  if (mArray != &(mAutoArray[0])) {
    delete[] mArray;
    mArray = mAutoArray;
    mArraySize = kAutoArraySize;
  }
}


NS_IMETHODIMP_(bool)
nsSupportsArray::Equals(const nsISupportsArray* aOther)
{
  if (aOther) {
    PRUint32 countOther;
    nsISupportsArray* other = const_cast<nsISupportsArray*>(aOther);
    nsresult rv = other->Count(&countOther);
    if (NS_FAILED( rv ))
      return false;

    if (mCount == countOther) {
      PRUint32 index = mCount;
      nsCOMPtr<nsISupports> otherElem;
      while (index--) {
        if (NS_FAILED(other->GetElementAt(index, getter_AddRefs(otherElem))))
          return false;
        if (mArray[index] != otherElem)
          return false;
      }
      return true;
    }
  }
  return false;
}

NS_IMETHODIMP_(nsISupports*)
nsSupportsArray::ElementAt(PRUint32 aIndex)
{
  if (aIndex < mCount) {
    nsISupports*  element = mArray[aIndex];
    NS_IF_ADDREF(element);
    return element;
  }
  return 0;
}

NS_IMETHODIMP_(PRInt32)
nsSupportsArray::IndexOf(const nsISupports* aPossibleElement)
{
  return IndexOfStartingAt(aPossibleElement, 0);
}

NS_IMETHODIMP_(PRInt32)
nsSupportsArray::IndexOfStartingAt(const nsISupports* aPossibleElement,
                                   PRUint32 aStartIndex)
{
  if (aStartIndex < mCount) {
    const nsISupports** start = (const nsISupports**)mArray;  // work around goofy compiler behavior
    const nsISupports** ep = (start + aStartIndex);
    const nsISupports** end = (start + mCount);
    while (ep < end) {
      if (aPossibleElement == *ep) {
        return (ep - start);
      }
      ep++;
    }
  }
  return -1;
}

NS_IMETHODIMP_(PRInt32)
nsSupportsArray::LastIndexOf(const nsISupports* aPossibleElement)
{
  if (0 < mCount) {
    const nsISupports** start = (const nsISupports**)mArray;  // work around goofy compiler behavior
    const nsISupports** ep = (start + mCount);
    while (start <= --ep) {
      if (aPossibleElement == *ep) {
        return (ep - start);
      }
    }
  }
  return -1;
}

NS_IMETHODIMP_(bool)
nsSupportsArray::InsertElementAt(nsISupports* aElement, PRUint32 aIndex)
{
  if (aIndex <= mCount) {
    if (mArraySize < (mCount + 1)) {
      // need to grow the array
      if (!GrowArrayBy(1))
        return false;
    }

    // Could be slightly more efficient if GrowArrayBy knew about the
    // split, but the difference is trivial.
    PRUint32 slide = (mCount - aIndex);
    if (0 < slide) {
      ::memmove(mArray + aIndex + 1, mArray + aIndex, slide * sizeof(nsISupports*));
    }

    mArray[aIndex] = aElement;
    NS_IF_ADDREF(aElement);
    mCount++;

#if DEBUG_SUPPORTSARRAY
    if (mCount > mMaxCount &&
        mCount < (PRInt32)(sizeof(MaxElements)/sizeof(MaxElements[0])))
    {
      MaxElements[mCount]++;
      MaxElements[mMaxCount]--;
      mMaxCount = mCount;
    }
#endif
    return true;
  }
  return false;
}

NS_IMETHODIMP_(bool)
nsSupportsArray::InsertElementsAt(nsISupportsArray* aElements, PRUint32 aIndex)
{
  if (!aElements) {
    return false;
  }
  PRUint32 countElements;
  if (NS_FAILED( aElements->Count( &countElements ) ))
    return false;

  if (aIndex <= mCount) {
    if (mArraySize < (mCount + countElements)) {
      // need to grow the array
      if (!GrowArrayBy(countElements))
        return false;
    }

    // Could be slightly more efficient if GrowArrayBy knew about the
    // split, but the difference is trivial.
    PRUint32 slide = (mCount - aIndex);
    if (0 < slide) {
      ::memmove(mArray + aIndex + countElements, mArray + aIndex,
                slide * sizeof(nsISupports*));
    }

    for (PRUint32 i = 0; i < countElements; ++i, ++mCount) {
      // use GetElementAt to copy and do AddRef for us
      if (NS_FAILED( aElements->GetElementAt( i, mArray + aIndex + i) ))
        return false;
    }

#if DEBUG_SUPPORTSARRAY
    if (mCount > mMaxCount &&
        mCount < (PRInt32)(sizeof(MaxElements)/sizeof(MaxElements[0])))
    {
      MaxElements[mCount]++;
      MaxElements[mMaxCount]--;
      mMaxCount = mCount;
    }
#endif
    return true;
  }
  return false;
}

NS_IMETHODIMP_(bool)
nsSupportsArray::ReplaceElementAt(nsISupports* aElement, PRUint32 aIndex)
{
  if (aIndex < mCount) {
    NS_IF_ADDREF(aElement);  // addref first in case it's the same object!
    NS_IF_RELEASE(mArray[aIndex]);
    mArray[aIndex] = aElement;
    return true;
  }
  return false;
}

NS_IMETHODIMP_(bool)
nsSupportsArray::RemoveElementsAt(PRUint32 aIndex, PRUint32 aCount)
{
  if (aIndex + aCount <= mCount) {
    for (PRUint32 i = 0; i < aCount; i++)
      NS_IF_RELEASE(mArray[aIndex+i]);
    mCount -= aCount;
    PRInt32 slide = (mCount - aIndex);
    if (0 < slide) {
      ::memmove(mArray + aIndex, mArray + aIndex + aCount,
                slide * sizeof(nsISupports*));
    }
    return true;
  }
  return false;
}

NS_IMETHODIMP_(bool)
nsSupportsArray::RemoveElement(const nsISupports* aElement, PRUint32 aStartIndex)
{
  PRInt32 theIndex = IndexOfStartingAt(aElement,aStartIndex);
  if (theIndex >= 0)
    return RemoveElementAt(theIndex);

  return false;
}

NS_IMETHODIMP_(bool)
nsSupportsArray::RemoveLastElement(const nsISupports* aElement)
{
  PRInt32 theIndex = LastIndexOf(aElement);
  if (theIndex >= 0)
    return RemoveElementAt(theIndex);

  return false;
}

NS_IMETHODIMP_(bool)
nsSupportsArray::MoveElement(PRInt32 aFrom, PRInt32 aTo)
{
  nsISupports *tempElement;

  if (aTo == aFrom)
    return true;

  if (aTo < 0 || aFrom < 0 ||
      (PRUint32) aTo >= mCount || (PRUint32) aFrom >= mCount)
  {
    // can't extend the array when moving an element.  Also catches mImpl = null
    return false;
  }
  tempElement = mArray[aFrom];

  if (aTo < aFrom)
  {
    // Moving one element closer to the head; the elements inbetween move down
    ::memmove(mArray + aTo + 1, mArray + aTo,
              (aFrom-aTo) * sizeof(mArray[0]));
    mArray[aTo] = tempElement;
  }
  else // already handled aFrom == aTo
  {
    // Moving one element closer to the tail; the elements inbetween move up
    ::memmove(mArray + aFrom, mArray + aFrom + 1,
              (aTo-aFrom) * sizeof(mArray[0]));
    mArray[aTo] = tempElement;
  }

  return true;
}

NS_IMETHODIMP
nsSupportsArray::Clear(void)
{
  if (0 < mCount) {
    do {
      --mCount;
      NS_IF_RELEASE(mArray[mCount]);
    } while (0 != mCount);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsSupportsArray::Compact(void)
{
#if DEBUG_SUPPORTSARRAY
  PRUint32 oldArraySize = mArraySize;
#endif
  if ((mArraySize != mCount) && (kAutoArraySize < mArraySize)) {
    nsISupports** oldArray = mArray;
    if (mCount <= kAutoArraySize) {
      mArray = mAutoArray;
      mArraySize = kAutoArraySize;
    }
    else {
      mArray = new nsISupports*[mCount];
      if (!mArray) {
        mArray = oldArray;
        return NS_OK;
      }
      mArraySize = mCount;
    }
#if DEBUG_SUPPORTSARRAY
    if (oldArray == mArray &&
        oldArray != &(mAutoArray[0])) // can't happen without use of realloc
      ADD_TO_STATS(GrowInPlace,oldArraySize);
    if (oldArray != &(mAutoArray[0]))
      ADD_TO_STATS(AllocedOfSize,mArraySize*sizeof(mArray[0]));
#endif
    ::memcpy(mArray, oldArray, mCount * sizeof(nsISupports*));
    delete[] oldArray;
  }
  return NS_OK;
}

NS_IMETHODIMP_(bool)
nsSupportsArray::SizeTo(PRInt32 aSize)
{
#if DEBUG_SUPPORTSARRAY
  PRUint32 oldArraySize = mArraySize;
#endif
  NS_ASSERTION(aSize >= 0, "negative aSize!");

  // XXX for aSize < mCount we could resize to mCount
  if (mArraySize == (PRUint32) aSize || (PRUint32) aSize < mCount)
    return true;     // nothing to do

  // switch back to autoarray if possible
  nsISupports** oldArray = mArray;
  if ((PRUint32) aSize <= kAutoArraySize) {
    mArray = mAutoArray;
    mArraySize = kAutoArraySize;
  }
  else {
    mArray = new nsISupports*[aSize];
    if (!mArray) {
      mArray = oldArray;
      return false;
    }
    mArraySize = aSize;
  }
#if DEBUG_SUPPORTSARRAY
  if (oldArray == mArray &&
      oldArray != &(mAutoArray[0])) // can't happen without use of realloc
    ADD_TO_STATS(GrowInPlace,oldArraySize);
  if (oldArray != &(mAutoArray[0]))
    ADD_TO_STATS(AllocedOfSize,mArraySize*sizeof(mArray[0]));
#endif
  ::memcpy(mArray, oldArray, mCount * sizeof(nsISupports*));
  if (oldArray != mAutoArray)
    delete[] oldArray;

  return true;
}

NS_IMETHODIMP_(bool)
nsSupportsArray::EnumerateForwards(nsISupportsArrayEnumFunc aFunc, void* aData)
{
  PRInt32 aIndex = -1;
  bool    running = true;

  while (running && (++aIndex < (PRInt32)mCount)) {
    running = (*aFunc)(mArray[aIndex], aData);
  }
  return running;
}

NS_IMETHODIMP_(bool)
nsSupportsArray::EnumerateBackwards(nsISupportsArrayEnumFunc aFunc, void* aData)
{
  PRUint32 aIndex = mCount;
  bool    running = true;

  while (running && (0 < aIndex--)) {
    running = (*aFunc)(mArray[aIndex], aData);
  }
  return running;
}

NS_IMETHODIMP
nsSupportsArray::Enumerate(nsIEnumerator* *result)
{
  nsSupportsArrayEnumerator* e = new nsSupportsArrayEnumerator(this);
  if (!e)
    return NS_ERROR_OUT_OF_MEMORY;
  *result = e;
  NS_ADDREF(e);
  return NS_OK;
}

static bool
CopyElement(nsISupports* aElement, void *aData)
{
  nsresult rv;
  nsISupportsArray* newArray = (nsISupportsArray*)aData;
  rv = newArray->AppendElement(aElement);
  return NS_SUCCEEDED(rv);
}

NS_IMETHODIMP
nsSupportsArray::Clone(nsISupportsArray* *result)
{
  nsresult rv;
  nsISupportsArray* newArray;
  rv = NS_NewISupportsArray(&newArray);
  bool ok = EnumerateForwards(CopyElement, newArray);
  if (!ok) return NS_ERROR_OUT_OF_MEMORY;
  *result = newArray;
  return NS_OK;
}

nsresult
NS_NewISupportsArray(nsISupportsArray** aInstancePtrResult)
{
  nsresult rv;
  rv = nsSupportsArray::Create(NULL, NS_GET_IID(nsISupportsArray),
                               (void**)aInstancePtrResult);
  return rv;
}

class nsArrayEnumerator MOZ_FINAL : public nsISimpleEnumerator
{
public:
    // nsISupports interface
    NS_DECL_ISUPPORTS

    // nsISimpleEnumerator interface
    NS_IMETHOD HasMoreElements(bool* aResult);
    NS_IMETHOD GetNext(nsISupports** aResult);

    // nsArrayEnumerator methods
    nsArrayEnumerator(nsISupportsArray* aValueArray);

private:
    ~nsArrayEnumerator(void);

protected:
    nsISupportsArray* mValueArray;
    PRInt32 mIndex;
};

nsArrayEnumerator::nsArrayEnumerator(nsISupportsArray* aValueArray)
    : mValueArray(aValueArray),
      mIndex(0)
{
    NS_IF_ADDREF(mValueArray);
}

nsArrayEnumerator::~nsArrayEnumerator(void)
{
    NS_IF_RELEASE(mValueArray);
}

NS_IMPL_ISUPPORTS1(nsArrayEnumerator, nsISimpleEnumerator)

NS_IMETHODIMP
nsArrayEnumerator::HasMoreElements(bool* aResult)
{
    NS_PRECONDITION(aResult != 0, "null ptr");
    if (! aResult)
        return NS_ERROR_NULL_POINTER;

    if (!mValueArray) {
        *aResult = false;
        return NS_OK;
    }

    PRUint32 cnt;
    nsresult rv = mValueArray->Count(&cnt);
    if (NS_FAILED(rv)) return rv;
    *aResult = (mIndex < (PRInt32) cnt);
    return NS_OK;
}

NS_IMETHODIMP
nsArrayEnumerator::GetNext(nsISupports** aResult)
{
    NS_PRECONDITION(aResult != 0, "null ptr");
    if (! aResult)
        return NS_ERROR_NULL_POINTER;

    if (!mValueArray) {
        *aResult = nsnull;
        return NS_OK;
    }

    PRUint32 cnt;
    nsresult rv = mValueArray->Count(&cnt);
    if (NS_FAILED(rv)) return rv;
    if (mIndex >= (PRInt32) cnt)
        return NS_ERROR_UNEXPECTED;

    *aResult = mValueArray->ElementAt(mIndex++);
    return NS_OK;
}

nsresult
NS_NewArrayEnumerator(nsISimpleEnumerator* *result,
                      nsISupportsArray* array)
{
    nsArrayEnumerator* enumer = new nsArrayEnumerator(array);
    if (enumer == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;
    *result = enumer; 
    NS_ADDREF(*result);
    return NS_OK;
}
