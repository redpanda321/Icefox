/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "prdtoa.h"

#ifdef XPCOM_STRING_CONSTRUCTOR_OUT_OF_LINE
nsTSubstring_CharT::nsTSubstring_CharT( char_type *data, size_type length,
                                        uint32_t flags)
  : mData(data),
    mLength(length),
    mFlags(flags)
  {
    if (flags & F_OWNED) {
      STRING_STAT_INCREMENT(Adopt);
#ifdef NS_BUILD_REFCNT_LOGGING
      NS_LogCtor(mData, "StringAdopt", 1);
#endif
    }
  }
#endif /* XPCOM_STRING_CONSTRUCTOR_OUT_OF_LINE */

  /**
   * helper function for down-casting a nsTSubstring to a nsTFixedString.
   */
inline const nsTFixedString_CharT*
AsFixedString( const nsTSubstring_CharT* s )
  {
    return static_cast<const nsTFixedString_CharT*>(s);
  }


  /**
   * this function is called to prepare mData for writing.  the given capacity
   * indicates the required minimum storage size for mData, in sizeof(char_type)
   * increments.  this function returns true if the operation succeeds.  it also
   * returns the old data and old flags members if mData is newly allocated.
   * the old data must be released by the caller.
   */
bool
nsTSubstring_CharT::MutatePrep( size_type capacity, char_type** oldData, uint32_t* oldFlags )
  {
    // initialize to no old data
    *oldData = nullptr;
    *oldFlags = 0;

    size_type curCapacity = Capacity();

    // If |capacity > kMaxCapacity|, then our doubling algorithm may not be
    // able to allocate it.  Just bail out in cases like that.  We don't want
    // to be allocating 2GB+ strings anyway.
    PR_STATIC_ASSERT((sizeof(nsStringBuffer) & 0x1) == 0);
    const size_type kMaxCapacity =
      (size_type(-1)/2 - sizeof(nsStringBuffer)) / sizeof(char_type) - 2;
    if (capacity > kMaxCapacity) {
      // Also assert for |capacity| equal to |size_type(-1)|, since we used to
      // use that value to flag immutability.
      NS_ASSERTION(capacity != size_type(-1), "Bogus capacity");
      return false;
    }

    // |curCapacity == 0| means that the buffer is immutable or 0-sized, so we
    // need to allocate a new buffer. We cannot use the existing buffer even
    // though it might be large enough.

    if (curCapacity != 0)
      {
        if (capacity <= curCapacity) {
          mFlags &= ~F_VOIDED;  // mutation clears voided flag
          return true;
        }

        // Use doubling algorithm when forced to increase available capacity.
        size_type temp = curCapacity;
        while (temp < capacity)
          temp <<= 1;
        NS_ASSERTION(NS_MIN(temp, kMaxCapacity) >= capacity,
                     "should have hit the early return at the top");
        capacity = NS_MIN(temp, kMaxCapacity);
      }

    //
    // several cases:
    //
    //  (1) we have a shared buffer (mFlags & F_SHARED)
    //  (2) we have an owned buffer (mFlags & F_OWNED)
    //  (3) we have a fixed buffer (mFlags & F_FIXED)
    //  (4) we have a readonly buffer
    //
    // requiring that we in some cases preserve the data before creating
    // a new buffer complicates things just a bit ;-)
    //

    size_type storageSize = (capacity + 1) * sizeof(char_type);

    // case #1
    if (mFlags & F_SHARED)
      {
        nsStringBuffer* hdr = nsStringBuffer::FromData(mData);
        if (!hdr->IsReadonly())
          {
            nsStringBuffer *newHdr = nsStringBuffer::Realloc(hdr, storageSize);
            if (!newHdr)
              return false; // out-of-memory (original header left intact)

            hdr = newHdr;
            mData = (char_type*) hdr->Data();
            mFlags &= ~F_VOIDED;  // mutation clears voided flag
            return true;
          }
      }

    char_type* newData;
    uint32_t newDataFlags;

      // if we have a fixed buffer of sufficient size, then use it.  this helps
      // avoid heap allocations.
    if ((mFlags & F_CLASS_FIXED) && (capacity < AsFixedString(this)->mFixedCapacity))
      {
        newData = AsFixedString(this)->mFixedBuf;
        newDataFlags = F_TERMINATED | F_FIXED;
      }
    else
      {
        // if we reach here then, we must allocate a new buffer.  we cannot
        // make use of our F_OWNED or F_FIXED buffers because they are not
        // large enough.

        nsStringBuffer* newHdr = nsStringBuffer::Alloc(storageSize);
        if (!newHdr)
          return false; // we are still in a consistent state

        newData = (char_type*) newHdr->Data();
        newDataFlags = F_TERMINATED | F_SHARED;
      }

    // save old data and flags
    *oldData = mData;
    *oldFlags = mFlags;

    mData = newData;
    SetDataFlags(newDataFlags);

    // mLength does not change

    // though we are not necessarily terminated at the moment, now is probably
    // still the best time to set F_TERMINATED.

    return true;
  }

void
nsTSubstring_CharT::Finalize()
  {
    ::ReleaseData(mData, mFlags);
    // mData, mLength, and mFlags are purposefully left dangling
  }

bool
nsTSubstring_CharT::ReplacePrepInternal(index_type cutStart, size_type cutLen,
                                        size_type fragLen, size_type newLen)
  {
    char_type* oldData;
    uint32_t oldFlags;
    if (!MutatePrep(newLen, &oldData, &oldFlags))
      return false; // out-of-memory

    if (oldData)
      {
        // determine whether or not we need to copy part of the old string
        // over to the new string.

        if (cutStart > 0)
          {
            // copy prefix from old string
            char_traits::copy(mData, oldData, cutStart);
          }

        if (cutStart + cutLen < mLength)
          {
            // copy suffix from old string to new offset
            size_type from = cutStart + cutLen;
            size_type fromLen = mLength - from;
            uint32_t to = cutStart + fragLen;
            char_traits::copy(mData + to, oldData + from, fromLen);
          }

        ::ReleaseData(oldData, oldFlags);
      }
    else
      {
        // original data remains intact

        // determine whether or not we need to move part of the existing string
        // to make room for the requested hole.
        if (fragLen != cutLen && cutStart + cutLen < mLength)
          {
            uint32_t from = cutStart + cutLen;
            uint32_t fromLen = mLength - from;
            uint32_t to = cutStart + fragLen;
            char_traits::move(mData + to, mData + from, fromLen);
          }
      }

    // add null terminator (mutable mData always has room for the null-
    // terminator).
    mData[newLen] = char_type(0);
    mLength = newLen;

    return true;
  }

nsTSubstring_CharT::size_type
nsTSubstring_CharT::Capacity() const
  {
    // return 0 to indicate an immutable or 0-sized buffer

    size_type capacity;
    if (mFlags & F_SHARED)
      {
        // if the string is readonly, then we pretend that it has no capacity.
        nsStringBuffer* hdr = nsStringBuffer::FromData(mData);
        if (hdr->IsReadonly())
          capacity = 0;
        else {
          capacity = (hdr->StorageSize() / sizeof(char_type)) - 1;
        }
      }
    else if (mFlags & F_FIXED)
      {
        capacity = AsFixedString(this)->mFixedCapacity;
      }
    else if (mFlags & F_OWNED)
      {
        // we don't store the capacity of an adopted buffer because that would
        // require an additional member field.  the best we can do is base the
        // capacity on our length.  remains to be seen if this is the right
        // trade-off.
        capacity = mLength;
      }
    else
      {
        capacity = 0;
      }

    return capacity;
  }

bool
nsTSubstring_CharT::EnsureMutable( size_type newLen )
  {
    if (newLen == size_type(-1) || newLen == mLength)
      {
        if (mFlags & (F_FIXED | F_OWNED))
          return true;
        if ((mFlags & F_SHARED) && !nsStringBuffer::FromData(mData)->IsReadonly())
          return true;

        newLen = mLength;
      }
    return SetLength(newLen, fallible_t());
  }

// ---------------------------------------------------------------------------

  // This version of Assign is optimized for single-character assignment.
void
nsTSubstring_CharT::Assign( char_type c )
  {
    if (!ReplacePrep(0, mLength, 1))
      NS_RUNTIMEABORT("OOM");

    *mData = c;
  }

bool
nsTSubstring_CharT::Assign( char_type c, const fallible_t& )
  {
    if (!ReplacePrep(0, mLength, 1))
      return false;

    *mData = c;
    return true;
  }

void
nsTSubstring_CharT::Assign( const char_type* data, size_type length )
  {
    if (!Assign(data, length, fallible_t()))
      NS_RUNTIMEABORT("OOM");
  }

bool
nsTSubstring_CharT::Assign( const char_type* data, size_type length, const fallible_t& )
  {
    if (!data)
      {
        Truncate();
        return true;
      }

    if (length == size_type(-1))
      length = char_traits::length(data);

    if (IsDependentOn(data, data + length))
      {
        return Assign(string_type(data, length), fallible_t());
      }

    if (!ReplacePrep(0, mLength, length))
      return false;

    char_traits::copy(mData, data, length);
    return true;
  }

void
nsTSubstring_CharT::AssignASCII( const char* data, size_type length )
  {
    if (!AssignASCII(data, length, fallible_t()))
      NS_RUNTIMEABORT("OOM");
  }

bool
nsTSubstring_CharT::AssignASCII( const char* data, size_type length, const fallible_t& )
  {
    // A Unicode string can't depend on an ASCII string buffer,
    // so this dependence check only applies to CStrings.
#ifdef CharT_is_char
    if (IsDependentOn(data, data + length))
      {
        return Assign(string_type(data, length), fallible_t());
      }
#endif

    if (!ReplacePrep(0, mLength, length))
      return false;

    char_traits::copyASCII(mData, data, length);
    return true;
  }

void
nsTSubstring_CharT::Assign( const self_type& str )
{
  if (!Assign(str, fallible_t()))
    NS_RUNTIMEABORT("OOM");
}

bool
nsTSubstring_CharT::Assign( const self_type& str, const fallible_t& )
  {
    // |str| could be sharable.  we need to check its flags to know how to
    // deal with it.

    if (&str == this)
      return true;

    if (!str.mLength)
      {
        Truncate();
        mFlags |= str.mFlags & F_VOIDED;
        return true;
      }

    if (str.mFlags & F_SHARED)
      {
        // nice! we can avoid a string copy :-)

        // |str| should be null-terminated
        NS_ASSERTION(str.mFlags & F_TERMINATED, "shared, but not terminated");

        ::ReleaseData(mData, mFlags);

        mData = str.mData;
        mLength = str.mLength;
        SetDataFlags(F_TERMINATED | F_SHARED);

        // get an owning reference to the mData
        nsStringBuffer::FromData(mData)->AddRef();
        return true;
      }

    // else, treat this like an ordinary assignment.
    return Assign(str.Data(), str.Length(), fallible_t());
  }

void
nsTSubstring_CharT::Assign( const substring_tuple_type& tuple )
  {
    if (!Assign(tuple, fallible_t()))
      NS_RUNTIMEABORT("OOM");
  }

bool
nsTSubstring_CharT::Assign( const substring_tuple_type& tuple, const fallible_t& )
  {
    if (tuple.IsDependentOn(mData, mData + mLength))
      {
        // take advantage of sharing here...
        return Assign(string_type(tuple), fallible_t());
      }

    size_type length = tuple.Length();

    // don't use ReplacePrep here because it changes the length
    char_type* oldData;
    uint32_t oldFlags;
    if (!MutatePrep(length, &oldData, &oldFlags))
      return false;

    if (oldData)
      ::ReleaseData(oldData, oldFlags);

    tuple.WriteTo(mData, length);
    mData[length] = 0;
    mLength = length;
    return true;
  }

void
nsTSubstring_CharT::Adopt( char_type* data, size_type length )
  {
    if (data)
      {
        ::ReleaseData(mData, mFlags);

        if (length == size_type(-1))
          length = char_traits::length(data);

        mData = data;
        mLength = length;
        SetDataFlags(F_TERMINATED | F_OWNED);

        STRING_STAT_INCREMENT(Adopt);
#ifdef NS_BUILD_REFCNT_LOGGING
        // Treat this as construction of a "StringAdopt" object for leak
        // tracking purposes.        
        NS_LogCtor(mData, "StringAdopt", 1);
#endif // NS_BUILD_REFCNT_LOGGING
      }
    else
      {
        SetIsVoid(true);
      }
  }


  // This version of Replace is optimized for single-character replacement.
void
nsTSubstring_CharT::Replace( index_type cutStart, size_type cutLength, char_type c )
  {
    cutStart = NS_MIN(cutStart, Length());

    if (ReplacePrep(cutStart, cutLength, 1))
      mData[cutStart] = c;
  }


void
nsTSubstring_CharT::Replace( index_type cutStart, size_type cutLength, const char_type* data, size_type length )
  {
      // unfortunately, some callers pass null :-(
    if (!data)
      {
        length = 0;
      }
    else
      {
        if (length == size_type(-1))
          length = char_traits::length(data);

        if (IsDependentOn(data, data + length))
          {
            nsTAutoString_CharT temp(data, length);
            Replace(cutStart, cutLength, temp);
            return;
          }
      }

    cutStart = NS_MIN(cutStart, Length());

    if (ReplacePrep(cutStart, cutLength, length) && length > 0)
      char_traits::copy(mData + cutStart, data, length);
  }

void
nsTSubstring_CharT::ReplaceASCII( index_type cutStart, size_type cutLength, const char* data, size_type length )
  {
    if (length == size_type(-1))
      length = strlen(data);
    
    // A Unicode string can't depend on an ASCII string buffer,
    // so this dependence check only applies to CStrings.
#ifdef CharT_is_char
    if (IsDependentOn(data, data + length))
      {
        nsTAutoString_CharT temp(data, length);
        Replace(cutStart, cutLength, temp);
        return;
      }
#endif

    cutStart = NS_MIN(cutStart, Length());

    if (ReplacePrep(cutStart, cutLength, length) && length > 0)
      char_traits::copyASCII(mData + cutStart, data, length);
  }

void
nsTSubstring_CharT::Replace( index_type cutStart, size_type cutLength, const substring_tuple_type& tuple )
  {
    if (tuple.IsDependentOn(mData, mData + mLength))
      {
        nsTAutoString_CharT temp(tuple);
        Replace(cutStart, cutLength, temp);
        return;
      }

    size_type length = tuple.Length();

    cutStart = NS_MIN(cutStart, Length());

    if (ReplacePrep(cutStart, cutLength, length) && length > 0)
      tuple.WriteTo(mData + cutStart, length);
  }

void
nsTSubstring_CharT::SetCapacity( size_type capacity )
  {
    if (!SetCapacity(capacity, fallible_t()))
      NS_RUNTIMEABORT("OOM");
  }

bool
nsTSubstring_CharT::SetCapacity( size_type capacity, const fallible_t& )
  {
    // capacity does not include room for the terminating null char

    // if our capacity is reduced to zero, then free our buffer.
    if (capacity == 0)
      {
        ::ReleaseData(mData, mFlags);
        mData = char_traits::sEmptyBuffer;
        mLength = 0;
        SetDataFlags(F_TERMINATED);
        return true;
      }

    char_type* oldData;
    uint32_t oldFlags;
    if (!MutatePrep(capacity, &oldData, &oldFlags))
      return false; // out-of-memory

    // compute new string length
    size_type newLen = NS_MIN(mLength, capacity);

    if (oldData)
      {
        // preserve old data
        if (mLength > 0)
          char_traits::copy(mData, oldData, newLen);

        ::ReleaseData(oldData, oldFlags);
      }

    // adjust mLength if our buffer shrunk down in size
    if (newLen < mLength)
      mLength = newLen;

    // always null-terminate here, even if the buffer got longer.  this is
    // for backwards compat with the old string implementation.
    mData[capacity] = char_type(0);

    return true;
  }

void
nsTSubstring_CharT::SetLength( size_type length )
  {
    SetCapacity(length);
    mLength = length;
  }

bool
nsTSubstring_CharT::SetLength( size_type length, const fallible_t& )
  {
    if (!SetCapacity(length, fallible_t()))
      return false;

    mLength = length;
    return true;
  }

void
nsTSubstring_CharT::SetIsVoid( bool val )
  {
    if (val)
      {
        Truncate();
        mFlags |= F_VOIDED;
      }
    else
      {
        mFlags &= ~F_VOIDED;
      }
  }

bool
nsTSubstring_CharT::Equals( const self_type& str ) const
  {
    return mLength == str.mLength && char_traits::compare(mData, str.mData, mLength) == 0;
  }

bool
nsTSubstring_CharT::Equals( const self_type& str, const comparator_type& comp ) const
  {
    return mLength == str.mLength && comp(mData, str.mData, mLength, str.mLength) == 0;
  }

bool
nsTSubstring_CharT::Equals( const char_type* data ) const
  {
    // unfortunately, some callers pass null :-(
    if (!data)
      {
        NS_NOTREACHED("null data pointer");
        return mLength == 0;
      }

    // XXX avoid length calculation?
    size_type length = char_traits::length(data);
    return mLength == length && char_traits::compare(mData, data, mLength) == 0;
  }

bool
nsTSubstring_CharT::Equals( const char_type* data, const comparator_type& comp ) const
  {
    // unfortunately, some callers pass null :-(
    if (!data)
      {
        NS_NOTREACHED("null data pointer");
        return mLength == 0;
      }

    // XXX avoid length calculation?
    size_type length = char_traits::length(data);
    return mLength == length && comp(mData, data, mLength, length) == 0;
  }

bool
nsTSubstring_CharT::EqualsASCII( const char* data, size_type len ) const
  {
    return mLength == len && char_traits::compareASCII(mData, data, len) == 0;
  }

bool
nsTSubstring_CharT::EqualsASCII( const char* data ) const
  {
    return char_traits::compareASCIINullTerminated(mData, mLength, data) == 0;
  }

bool
nsTSubstring_CharT::LowerCaseEqualsASCII( const char* data, size_type len ) const
  {
    return mLength == len && char_traits::compareLowerCaseToASCII(mData, data, len) == 0;
  }

bool
nsTSubstring_CharT::LowerCaseEqualsASCII( const char* data ) const
  {
    return char_traits::compareLowerCaseToASCIINullTerminated(mData, mLength, data) == 0;
  }

nsTSubstring_CharT::size_type
nsTSubstring_CharT::CountChar( char_type c ) const
  {
    const char_type *start = mData;
    const char_type *end   = mData + mLength;

    return NS_COUNT(start, end, c);
  }

int32_t
nsTSubstring_CharT::FindChar( char_type c, index_type offset ) const
  {
    if (offset < mLength)
      {
        const char_type* result = char_traits::find(mData + offset, mLength - offset, c);
        if (result)
          return result - mData;
      }
    return -1;
  }

void
nsTSubstring_CharT::StripChar( char_type aChar, int32_t aOffset )
  {
    if (mLength == 0 || aOffset >= int32_t(mLength))
      return;

    if (!EnsureMutable()) // XXX do this lazily?
      NS_RUNTIMEABORT("OOM");

    // XXX(darin): this code should defer writing until necessary.

    char_type* to   = mData + aOffset;
    char_type* from = mData + aOffset;
    char_type* end  = mData + mLength;

    while (from < end)
      {
        char_type theChar = *from++;
        if (aChar != theChar)
          *to++ = theChar;
      }
    *to = char_type(0); // add the null
    mLength = to - mData;
  }

void
nsTSubstring_CharT::StripChars( const char_type* aChars, uint32_t aOffset )
  {
    if (aOffset >= uint32_t(mLength))
      return;

    if (!EnsureMutable()) // XXX do this lazily?
      NS_RUNTIMEABORT("OOM");

    // XXX(darin): this code should defer writing until necessary.

    char_type* to   = mData + aOffset;
    char_type* from = mData + aOffset;
    char_type* end  = mData + mLength;

    while (from < end)
      {
        char_type theChar = *from++;
        const char_type* test = aChars;

        for (; *test && *test != theChar; ++test);

        if (!*test) {
          // Not stripped, copy this char.
          *to++ = theChar;
        }
      }
    *to = char_type(0); // add the null
    mLength = to - mData;
  }

int
nsTSubstring_CharT::AppendFunc(void* arg, const char* s, uint32_t len)
  {
    self_type* self = static_cast<self_type*>(arg);

    // NSPR sends us the final null terminator even though we don't want it
    if (len && s[len - 1] == '\0') {
      --len;
    }

    self->AppendASCII(s, len);

    return len;
  }

void nsTSubstring_CharT::AppendPrintf( const char* format, ...)
  {
    va_list ap;
    va_start(ap, format);
    uint32_t r = PR_vsxprintf(AppendFunc, this, format, ap);
    if (r == (uint32_t) -1)
      NS_RUNTIMEABORT("Allocation or other failure in PR_vsxprintf");
    va_end(ap);
  }

void nsTSubstring_CharT::AppendPrintf( const char* format, va_list ap )
  {
    uint32_t r = PR_vsxprintf(AppendFunc, this, format, ap);
    if (r == (uint32_t) -1)
      NS_RUNTIMEABORT("Allocation or other failure in PR_vsxprintf");
  }

/* hack to make sure we define Modified_cnvtf only once */
#ifdef CharT_is_PRUnichar
/**
 * This is a copy of |PR_cnvtf| with a bug fixed.  (The second argument
 * of PR_dtoa is 2 rather than 1.)
 *
 * XXX(darin): if this is the right thing, then why wasn't it fixed in NSPR?!?
 */
static void 
Modified_cnvtf(char (& buf)[40], int prcsn, double fval)
{
  int decpt, sign, numdigits;
  char num[40];
  char *nump;
  char *bufp = buf;
  char *endnum;

  if (PR_dtoa(fval, 2, prcsn, &decpt, &sign, &endnum, num, sizeof(num))
      == PR_FAILURE) {
    buf[0] = '\0';
    return;
  }
  numdigits = endnum - num;
  nump = num;

  /*
   * The NSPR code had a fancy way of checking that we weren't dealing
   * with -0.0 or -NaN, but I'll just use < instead.
   * XXX Should we check !isnan(fval) as well?  Is it portable?  We
   * probably don't need to bother since NAN isn't portable.
   */
  if (sign && fval < 0.0f) {
    *bufp++ = '-';
  }

  if (decpt == 9999) {
    while ((*bufp++ = *nump++) != 0) {} /* nothing to execute */
    return;
  }

  if (decpt > (prcsn+1) || decpt < -(prcsn-1) || decpt < -5) {
    *bufp++ = *nump++;
    if (numdigits != 1) {
      *bufp++ = '.';
    }

    while (*nump != '\0') {
      *bufp++ = *nump++;
    }
    *bufp++ = 'e';
    PR_snprintf(bufp, sizeof(num) - (bufp - buf), "%+d", decpt-1);
  }
  else if (decpt >= 0) {
    if (decpt == 0) {
      *bufp++ = '0';
    }
    else {
      while (decpt--) {
        if (*nump != '\0') {
          *bufp++ = *nump++;
        }
        else {
          *bufp++ = '0';
        }
      }
    }
    if (*nump != '\0') {
      *bufp++ = '.';
      while (*nump != '\0') {
        *bufp++ = *nump++;
      }
    }
    *bufp++ = '\0';
  }
  else if (decpt < 0) {
    *bufp++ = '0';
    *bufp++ = '.';
    while (decpt++) {
      *bufp++ = '0';
    }

    while (*nump != '\0') {
      *bufp++ = *nump++;
    }
    *bufp++ = '\0';
  }
}
#endif /* CharT_is_PRUnichar */

void
nsTSubstring_CharT::DoAppendFloat( double aFloat, int digits )
{
  char buf[40];
  // Use Modified_cnvtf, which is locale-insensitive, instead of the
  // locale-sensitive PR_snprintf or sprintf(3)
  Modified_cnvtf(buf, digits, aFloat);
  AppendASCII(buf);
}

size_t
nsTSubstring_CharT::SizeOfExcludingThisMustBeUnshared(
    nsMallocSizeOfFun mallocSizeOf) const
{
  if (mFlags & F_SHARED) {
    return nsStringBuffer::FromData(mData)->
             SizeOfIncludingThisMustBeUnshared(mallocSizeOf);
  } 
  if (mFlags & F_OWNED) {
    return mallocSizeOf(mData);
  }

  // If we reach here, exactly one of the following must be true:
  // - F_VOIDED is set, and mData points to sEmptyBuffer;
  // - F_FIXED is set, and mData points to a buffer within a string
  //   object (e.g. nsAutoString);
  // - None of F_SHARED, F_OWNED, F_FIXED is set, and mData points to a buffer
  //   owned by something else.
  //
  // In all three cases, we don't measure it.
  return 0;
}

size_t
nsTSubstring_CharT::SizeOfExcludingThisIfUnshared(
    nsMallocSizeOfFun mallocSizeOf) const
{
  // This is identical to SizeOfExcludingThisMustBeUnshared except for the
  // F_SHARED case.
  if (mFlags & F_SHARED) {
    return nsStringBuffer::FromData(mData)->
             SizeOfIncludingThisIfUnshared(mallocSizeOf);
  }
  if (mFlags & F_OWNED) {
    return mallocSizeOf(mData);
  }
  return 0;
}

size_t
nsTSubstring_CharT::SizeOfIncludingThisMustBeUnshared(
    nsMallocSizeOfFun mallocSizeOf) const
{
  return mallocSizeOf(this) + SizeOfExcludingThisMustBeUnshared(mallocSizeOf);
}

size_t
nsTSubstring_CharT::SizeOfIncludingThisIfUnshared(
    nsMallocSizeOfFun mallocSizeOf) const
{
  return mallocSizeOf(this) + SizeOfExcludingThisIfUnshared(mallocSizeOf);
}

