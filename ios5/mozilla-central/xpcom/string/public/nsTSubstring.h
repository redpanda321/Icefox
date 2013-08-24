/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// IWYU pragma: private, include "nsAString.h"

#ifndef MOZILLA_INTERNAL_API
#error Cannot use internal string classes without MOZILLA_INTERNAL_API defined. Use the frozen header nsStringAPI.h instead.
#endif

  /**
   * The base for string comparators
   */
class nsTStringComparator_CharT
  {
    public:
      typedef CharT char_type;

      nsTStringComparator_CharT() {}

      virtual int operator()( const char_type*, const char_type*, PRUint32, PRUint32 ) const = 0;
  };


  /**
   * The default string comparator (case-sensitive comparision)
   */
class nsTDefaultStringComparator_CharT
    : public nsTStringComparator_CharT
  {
    public:
      typedef CharT char_type;

      nsTDefaultStringComparator_CharT() {}

      virtual int operator()( const char_type*, const char_type*, PRUint32, PRUint32 ) const;
  };

  /**
   * nsTSubstring is the most abstract class in the string hierarchy. It
   * represents a single contiguous array of characters, which may or may not
   * be null-terminated. This type is not instantiated directly.  A sub-class
   * is instantiated instead.  For example, see nsTString.
   *
   * NAMES:
   *   nsAString for wide characters
   *   nsACString for narrow characters
   *
   * Many of the accessors on nsTSubstring are inlined as an optimization.
   */
class nsTSubstring_CharT
  {
    public:
      typedef mozilla::fallible_t                 fallible_t;

      typedef CharT                               char_type;

      typedef nsCharTraits<char_type>             char_traits;
      typedef char_traits::incompatible_char_type incompatible_char_type;

      typedef nsTSubstring_CharT                  self_type;
      typedef self_type                           abstract_string_type;
      typedef self_type                           base_string_type;

      typedef self_type                           substring_type;
      typedef nsTSubstringTuple_CharT             substring_tuple_type;
      typedef nsTString_CharT                     string_type;

      typedef nsReadingIterator<char_type>        const_iterator;
      typedef nsWritingIterator<char_type>        iterator;

      typedef nsTStringComparator_CharT           comparator_type;

      typedef char_type*                          char_iterator;
      typedef const char_type*                    const_char_iterator;

      typedef PRUint32                            size_type;
      typedef PRUint32                            index_type;

    public:

        // this acts like a virtual destructor
      ~nsTSubstring_CharT() { Finalize(); }

        /**
         * reading iterators
         */

      const_char_iterator BeginReading() const { return mData; }
      const_char_iterator EndReading() const { return mData + mLength; }

        /**
         * deprecated reading iterators
         */

      const_iterator& BeginReading( const_iterator& iter ) const
        {
          iter.mStart = mData;
          iter.mEnd = mData + mLength;
          iter.mPosition = iter.mStart;
          return iter;
        }

      const_iterator& EndReading( const_iterator& iter ) const
        {
          iter.mStart = mData;
          iter.mEnd = mData + mLength;
          iter.mPosition = iter.mEnd;
          return iter;
        }

      const_char_iterator& BeginReading( const_char_iterator& iter ) const
        {
          return iter = mData;
        }

      const_char_iterator& EndReading( const_char_iterator& iter ) const
        {
          return iter = mData + mLength;
        }


        /**
         * writing iterators
         */
      
      char_iterator BeginWriting()
        {
          if (!EnsureMutable())
            NS_RUNTIMEABORT("OOM");

          return mData;
        }

      char_iterator BeginWriting( const fallible_t& )
        {
          return EnsureMutable() ? mData : char_iterator(0);
        }

      char_iterator EndWriting()
        {
          if (!EnsureMutable())
            NS_RUNTIMEABORT("OOM");

          return mData + mLength;
        }

      char_iterator EndWriting( const fallible_t& )
        {
          return EnsureMutable() ? (mData + mLength) : char_iterator(0);
        }

      char_iterator& BeginWriting( char_iterator& iter )
        {
          return iter = BeginWriting();
        }

      char_iterator& BeginWriting( char_iterator& iter, const fallible_t& )
        {
          return iter = BeginWriting(fallible_t());
        }

      char_iterator& EndWriting( char_iterator& iter )
        {
          return iter = EndWriting();
        }

      char_iterator& EndWriting( char_iterator& iter, const fallible_t& )
        {
          return iter = EndWriting(fallible_t());
        }

        /**
         * deprecated writing iterators
         */
      
      iterator& BeginWriting( iterator& iter )
        {
          char_type *data = BeginWriting();
          iter.mStart = data;
          iter.mEnd = data + mLength;
          iter.mPosition = iter.mStart;
          return iter;
        }

      iterator& EndWriting( iterator& iter )
        {
          char_type *data = BeginWriting();
          iter.mStart = data;
          iter.mEnd = data + mLength;
          iter.mPosition = iter.mEnd;
          return iter;
        }

        /**
         * accessors
         */

        // returns pointer to string data (not necessarily null-terminated)
      const char_type *Data() const
        {
          return mData;
        }

      size_type Length() const
        {
          return mLength;
        }

      bool IsEmpty() const
        {
          return mLength == 0;
        }

      bool IsVoid() const
        {
          return (mFlags & F_VOIDED) != 0;
        }

      bool IsTerminated() const
        {
          return (mFlags & F_TERMINATED) != 0;
        }

      char_type CharAt( index_type i ) const
        {
          NS_ASSERTION(i < mLength, "index exceeds allowable range");
          return mData[i];
        }

      char_type operator[]( index_type i ) const
        {
          return CharAt(i);
        }

      char_type First() const
        {
          NS_ASSERTION(mLength > 0, "|First()| called on an empty string");
          return mData[0];
        }

      inline
      char_type Last() const
        {
          NS_ASSERTION(mLength > 0, "|Last()| called on an empty string");
          return mData[mLength - 1];
        }

      size_type NS_FASTCALL CountChar( char_type ) const;
      PRInt32 NS_FASTCALL FindChar( char_type, index_type offset = 0 ) const;


        /**
         * equality
         */

      bool NS_FASTCALL Equals( const self_type& ) const;
      bool NS_FASTCALL Equals( const self_type&, const comparator_type& ) const;

      bool NS_FASTCALL Equals( const char_type* data ) const;
      bool NS_FASTCALL Equals( const char_type* data, const comparator_type& comp ) const;

        /**
         * An efficient comparison with ASCII that can be used even
         * for wide strings. Call this version when you know the
         * length of 'data'.
         */
      bool NS_FASTCALL EqualsASCII( const char* data, size_type len ) const;
        /**
         * An efficient comparison with ASCII that can be used even
         * for wide strings. Call this version when 'data' is
         * null-terminated.
         */
      bool NS_FASTCALL EqualsASCII( const char* data ) const;

    // EqualsLiteral must ONLY be applied to an actual literal string.
    // Do not attempt to use it with a regular char* pointer, or with a char
    // array variable.
    // The template trick to acquire the array length at compile time without
    // using a macro is due to Corey Kosak, with much thanks.
#ifdef NS_DISABLE_LITERAL_TEMPLATE
      inline bool EqualsLiteral( const char* str ) const
        {
          return EqualsASCII(str);
        }
#else
      template<int N>
      inline bool EqualsLiteral( const char (&str)[N] ) const
        {
          return EqualsASCII(str, N-1);
        }
      template<int N>
      inline bool EqualsLiteral( char (&str)[N] ) const
        {
          const char* s = str;
          return EqualsASCII(s, N-1);
        }
#endif

    // The LowerCaseEquals methods compare the lower case version of
    // this string to some ASCII/Literal string. The ASCII string is
    // *not* lowercased for you. If you compare to an ASCII or literal
    // string that contains an uppercase character, it is guaranteed to
    // return false. We will throw assertions too.
      bool NS_FASTCALL LowerCaseEqualsASCII( const char* data, size_type len ) const;
      bool NS_FASTCALL LowerCaseEqualsASCII( const char* data ) const;

    // LowerCaseEqualsLiteral must ONLY be applied to an actual
    // literal string.  Do not attempt to use it with a regular char*
    // pointer, or with a char array variable. Use
    // LowerCaseEqualsASCII for them.
#ifdef NS_DISABLE_LITERAL_TEMPLATE
      inline bool LowerCaseEqualsLiteral( const char* str ) const
        {
          return LowerCaseEqualsASCII(str);
        }
#else
      template<int N>
      inline bool LowerCaseEqualsLiteral( const char (&str)[N] ) const
        {
          return LowerCaseEqualsASCII(str, N-1);
        }
      template<int N>
      inline bool LowerCaseEqualsLiteral( char (&str)[N] ) const
        {
          const char* s = str;
          return LowerCaseEqualsASCII(s, N-1);
        }
#endif

        /**
         * assignment
         */

      void NS_FASTCALL Assign( char_type c );
      bool NS_FASTCALL Assign( char_type c, const fallible_t& ) NS_WARN_UNUSED_RESULT;

      void NS_FASTCALL
        Assign( const char_type* data, size_type length = size_type(-1) );
      bool NS_FASTCALL Assign( const char_type* data, size_type length, const fallible_t& ) NS_WARN_UNUSED_RESULT;

      void NS_FASTCALL Assign( const self_type& );
      bool NS_FASTCALL Assign( const self_type&, const fallible_t& ) NS_WARN_UNUSED_RESULT;

      void NS_FASTCALL Assign( const substring_tuple_type& );
      bool NS_FASTCALL Assign( const substring_tuple_type&, const fallible_t& ) NS_WARN_UNUSED_RESULT;

      void NS_FASTCALL AssignASCII( const char* data, size_type length );
      bool NS_FASTCALL AssignASCII( const char* data, size_type length, const fallible_t& ) NS_WARN_UNUSED_RESULT;

      void NS_FASTCALL AssignASCII( const char* data )
        {
          AssignASCII(data, strlen(data));
        }
      bool NS_FASTCALL AssignASCII( const char* data, const fallible_t& ) NS_WARN_UNUSED_RESULT
        {
          return AssignASCII(data, strlen(data), fallible_t());
        }

    // AssignLiteral must ONLY be applied to an actual literal string.
    // Do not attempt to use it with a regular char* pointer, or with a char
    // array variable. Use AssignASCII for those.
    // There are not fallible version of these methods because they only really
    // apply to small allocations that we wouldn't want to check anyway.
#ifdef NS_DISABLE_LITERAL_TEMPLATE
      void AssignLiteral( const char* str )
                  { AssignASCII(str); }
#else
      template<int N>
      void AssignLiteral( const char (&str)[N] )
                  { AssignASCII(str, N-1); }
      template<int N>
      void AssignLiteral( char (&str)[N] )
                  { AssignASCII(str, N-1); }
#endif

      self_type& operator=( char_type c )                                                       { Assign(c);        return *this; }
      self_type& operator=( const char_type* data )                                             { Assign(data);     return *this; }
      self_type& operator=( const self_type& str )                                              { Assign(str);      return *this; }
      self_type& operator=( const substring_tuple_type& tuple )                                 { Assign(tuple);    return *this; }

      void NS_FASTCALL Adopt( char_type* data, size_type length = size_type(-1) );


        /**
         * buffer manipulation
         */

      void NS_FASTCALL Replace( index_type cutStart, size_type cutLength, char_type c );
      void NS_FASTCALL Replace( index_type cutStart, size_type cutLength, const char_type* data, size_type length = size_type(-1) );
      void Replace( index_type cutStart, size_type cutLength, const self_type& str )      { Replace(cutStart, cutLength, str.Data(), str.Length()); }
      void NS_FASTCALL Replace( index_type cutStart, size_type cutLength, const substring_tuple_type& tuple );

      void NS_FASTCALL ReplaceASCII( index_type cutStart, size_type cutLength, const char* data, size_type length = size_type(-1) );

      void Append( char_type c )                                                                 { Replace(mLength, 0, c); }
      void Append( const char_type* data, size_type length = size_type(-1) )                     { Replace(mLength, 0, data, length); }
      void Append( const self_type& str )                                                        { Replace(mLength, 0, str); }
      void Append( const substring_tuple_type& tuple )                                           { Replace(mLength, 0, tuple); }

      void AppendASCII( const char* data, size_type length = size_type(-1) )                     { ReplaceASCII(mLength, 0, data, length); }

      /**
       * Append a formatted string to the current string. Uses the format
       * codes documented in prprf.h
       */
      void AppendPrintf( const char* format, ... );
      void AppendInt( PRInt32 aInteger )
                 { AppendPrintf( "%d", aInteger ); }
      void AppendInt( PRInt32 aInteger, int aRadix )
        {
          const char *fmt = aRadix == 10 ? "%d" : aRadix == 8 ? "%o" : "%x";
          AppendPrintf( fmt, aInteger );
        }
      void AppendInt( PRUint32 aInteger )
                 { AppendPrintf( "%u", aInteger ); }
      void AppendInt( PRUint32 aInteger, int aRadix )
        {
          const char *fmt = aRadix == 10 ? "%u" : aRadix == 8 ? "%o" : "%x";
          AppendPrintf( fmt, aInteger );
        }
      void AppendInt( PRInt64 aInteger )
                 { AppendPrintf( "%lld", aInteger ); }
      void AppendInt( PRInt64 aInteger, int aRadix )
        {
          const char *fmt = aRadix == 10 ? "%lld" : aRadix == 8 ? "%llo" : "%llx";
          AppendPrintf( fmt, aInteger );
        }
      void AppendInt( PRUint64 aInteger )
                 { AppendPrintf( "%llu", aInteger ); }
      void AppendInt( PRUint64 aInteger, int aRadix )
        {
          const char *fmt = aRadix == 10 ? "%llu" : aRadix == 8 ? "%llo" : "%llx";
          AppendPrintf( fmt, aInteger );
        }

      /**
       * Append the given float to this string 
       */
      void AppendFloat( float aFloat )
                      { DoAppendFloat(aFloat, 6); }
      void AppendFloat( double aFloat )
                      { DoAppendFloat(aFloat, 15); }
  private:
      void NS_FASTCALL DoAppendFloat( double aFloat, int digits );
  public:

    // AppendLiteral must ONLY be applied to an actual literal string.
    // Do not attempt to use it with a regular char* pointer, or with a char
    // array variable. Use AppendASCII for those.
#ifdef NS_DISABLE_LITERAL_TEMPLATE
      void AppendLiteral( const char* str )
                  { AppendASCII(str); }
#else
      template<int N>
      void AppendLiteral( const char (&str)[N] )
                  { AppendASCII(str, N-1); }
      template<int N>
      void AppendLiteral( char (&str)[N] )
                  { AppendASCII(str, N-1); }
#endif

      self_type& operator+=( char_type c )                                                       { Append(c);        return *this; }
      self_type& operator+=( const char_type* data )                                             { Append(data);     return *this; }
      self_type& operator+=( const self_type& str )                                              { Append(str);      return *this; }
      self_type& operator+=( const substring_tuple_type& tuple )                                 { Append(tuple);    return *this; }

      void Insert( char_type c, index_type pos )                                                 { Replace(pos, 0, c); }
      void Insert( const char_type* data, index_type pos, size_type length = size_type(-1) )     { Replace(pos, 0, data, length); }
      void Insert( const self_type& str, index_type pos )                                        { Replace(pos, 0, str); }
      void Insert( const substring_tuple_type& tuple, index_type pos )                           { Replace(pos, 0, tuple); }

      void Cut( index_type cutStart, size_type cutLength )                                       { Replace(cutStart, cutLength, char_traits::sEmptyBuffer, 0); }


        /**
         * buffer sizing
         */

        /**
         * Attempts to set the capacity to the given size, without affecting
         * the length of the string. Also ensures that the buffer is mutable.
         */
      void NS_FASTCALL SetCapacity( size_type newCapacity );
      bool NS_FASTCALL SetCapacity( size_type newCapacity, const fallible_t& ) NS_WARN_UNUSED_RESULT;

      void NS_FASTCALL SetLength( size_type newLength );
      bool NS_FASTCALL SetLength( size_type newLength, const fallible_t& ) NS_WARN_UNUSED_RESULT;

      void Truncate( size_type newLength = 0 )
        {
          NS_ASSERTION(newLength <= mLength, "Truncate cannot make string longer");
          SetLength(newLength);
        }


        /**
         * buffer access
         */


        /**
         * Get a const pointer to the string's internal buffer.  The caller
         * MUST NOT modify the characters at the returned address.
         *
         * @returns The length of the buffer in characters.
         */
      inline size_type GetData( const char_type** data ) const
        {
          *data = mData;
          return mLength;
        }
        
        /**
         * Get a pointer to the string's internal buffer, optionally resizing
         * the buffer first.  If size_type(-1) is passed for newLen, then the
         * current length of the string is used.  The caller MAY modify the
         * characters at the returned address (up to but not exceeding the
         * length of the string).
         *
         * @returns The length of the buffer in characters or 0 if unable to
         * satisfy the request due to low-memory conditions.
         */
      size_type GetMutableData( char_type** data, size_type newLen = size_type(-1) )
        {
          if (!EnsureMutable(newLen))
            NS_RUNTIMEABORT("OOM");

          *data = mData;
          return mLength;
        }

      size_type GetMutableData( char_type** data, size_type newLen, const fallible_t& )
        {
          if (!EnsureMutable(newLen))
            {
              *data = nsnull;
              return 0;
            }

          *data = mData;
          return mLength;
        }


        /**
         * string data is never null, but can be marked void.  if true, the
         * string will be truncated.  @see nsTSubstring::IsVoid
         */

      void NS_FASTCALL SetIsVoid( bool );

        /**
         *  This method is used to remove all occurrences of aChar from this
         * string.
         *  
         *  @param  aChar -- char to be stripped
         *  @param  aOffset -- where in this string to start stripping chars
         */
         
      void StripChar( char_type aChar, PRInt32 aOffset=0 );

        /**
         *  This method is used to remove all occurrences of aChars from this
         * string.
         *
         *  @param  aChars -- chars to be stripped
         *  @param  aOffset -- where in this string to start stripping chars
         */

      void StripChars( const char_type* aChars, PRUint32 aOffset=0 );

        /**
         * If the string uses a shared buffer, this method
         * clears the pointer without releasing the buffer.
         */
      void ForgetSharedBuffer()
      {
        if (mFlags & nsSubstring::F_SHARED)
          {
            mData = char_traits::sEmptyBuffer;
            mLength = 0;
            mFlags = F_TERMINATED;
          }
      }

    public:

        /**
         * this is public to support automatic conversion of tuple to string
         * base type, which helps avoid converting to nsTAString.
         */
      nsTSubstring_CharT(const substring_tuple_type& tuple)
        : mData(nsnull),
          mLength(0),
          mFlags(F_NONE)
        {
          Assign(tuple);
        }

        /**
         * allows for direct initialization of a nsTSubstring object. 
         *
         * NOTE: this constructor is declared public _only_ for convenience
         * inside the string implementation.
         */
        // XXXbz or can I just include nscore.h and use NS_BUILD_REFCNT_LOGGING?
#if defined(DEBUG) || defined(FORCE_BUILD_REFCNT_LOGGING)
#define XPCOM_STRING_CONSTRUCTOR_OUT_OF_LINE
      nsTSubstring_CharT( char_type *data, size_type length, PRUint32 flags );
#else
#undef XPCOM_STRING_CONSTRUCTOR_OUT_OF_LINE
      nsTSubstring_CharT( char_type *data, size_type length, PRUint32 flags )
        : mData(data),
          mLength(length),
          mFlags(flags) {}
#endif /* DEBUG || FORCE_BUILD_REFCNT_LOGGING */

      size_t SizeOfExcludingThisMustBeUnshared(nsMallocSizeOfFun mallocSizeOf)
        const;
      size_t SizeOfIncludingThisMustBeUnshared(nsMallocSizeOfFun mallocSizeOf)
        const;

      size_t SizeOfExcludingThisIfUnshared(nsMallocSizeOfFun mallocSizeOf)
        const;
      size_t SizeOfIncludingThisIfUnshared(nsMallocSizeOfFun mallocSizeOf)
        const;

    protected:

      friend class nsTObsoleteAStringThunk_CharT;
      friend class nsTSubstringTuple_CharT;

      // XXX GCC 3.4 needs this :-(
      friend class nsTPromiseFlatString_CharT;

      char_type*  mData;
      size_type   mLength;
      PRUint32    mFlags;

        // default initialization 
      nsTSubstring_CharT()
        : mData(char_traits::sEmptyBuffer),
          mLength(0),
          mFlags(F_TERMINATED) {}

        // version of constructor that leaves mData and mLength uninitialized
      explicit
      nsTSubstring_CharT( PRUint32 flags )
        : mFlags(flags) {}

        // copy-constructor, constructs as dependent on given object
        // (NOTE: this is for internal use only)
      nsTSubstring_CharT( const self_type& str )
        : mData(str.mData),
          mLength(str.mLength),
          mFlags(str.mFlags & (F_TERMINATED | F_VOIDED)) {}

        /**
         * this function releases mData and does not change the value of
         * any of its member variables.  in other words, this function acts
         * like a destructor.
         */
      void NS_FASTCALL Finalize();

        /**
         * this function prepares mData to be mutated.
         *
         * @param capacity     specifies the required capacity of mData  
         * @param old_data     returns null or the old value of mData
         * @param old_flags    returns 0 or the old value of mFlags
         *
         * if mData is already mutable and of sufficient capacity, then this
         * function will return immediately.  otherwise, it will either resize
         * mData or allocate a new shared buffer.  if it needs to allocate a
         * new buffer, then it will return the old buffer and the corresponding
         * flags.  this allows the caller to decide when to free the old data.
         *
         * this function returns false if is unable to allocate sufficient
         * memory.
         *
         * XXX we should expose a way for subclasses to free old_data.
         */
      bool NS_FASTCALL MutatePrep( size_type capacity, char_type** old_data, PRUint32* old_flags );

        /**
         * this function prepares a section of mData to be modified.  if
         * necessary, this function will reallocate mData and possibly move
         * existing data to open up the specified section.
         *
         * @param cutStart     specifies the starting offset of the section
         * @param cutLength    specifies the length of the section to be replaced
         * @param newLength    specifies the length of the new section
         *
         * for example, suppose mData contains the string "abcdef" then
         * 
         *   ReplacePrep(2, 3, 4);
         *
         * would cause mData to look like "ab____f" where the characters
         * indicated by '_' have an unspecified value and can be freely
         * modified.  this function will null-terminate mData upon return.
         * 
         * this function returns false if is unable to allocate sufficient
         * memory.
         */
      bool ReplacePrep(index_type cutStart, size_type cutLength,
                       size_type newLength) NS_WARN_UNUSED_RESULT
      {
        cutLength = NS_MIN(cutLength, mLength - cutStart);
        PRUint32 newTotalLen = mLength - cutLength + newLength;
        if (cutStart == mLength && Capacity() > newTotalLen) {
          mFlags &= ~F_VOIDED;
          mData[newTotalLen] = char_type(0);
          mLength = newTotalLen;
          return true;
        }
        return ReplacePrepInternal(cutStart, cutLength, newLength, newTotalLen);
      }

      bool NS_FASTCALL ReplacePrepInternal(index_type cutStart,
                                           size_type cutLength,
                                           size_type newFragLength,
                                           size_type newTotalLength)
        NS_WARN_UNUSED_RESULT;

        /**
         * returns the number of writable storage units starting at mData.
         * the value does not include space for the null-terminator character.
         *
         * NOTE: this function returns 0 if mData is immutable (or the buffer
         *       is 0-sized).
         */
      size_type NS_FASTCALL Capacity() const;

        /**
         * this helper function can be called prior to directly manipulating
         * the contents of mData.  see, for example, BeginWriting.
         */
      bool NS_FASTCALL EnsureMutable( size_type newLen = size_type(-1) ) NS_WARN_UNUSED_RESULT;

        /**
         * returns true if this string overlaps with the given string fragment.
         */
      bool IsDependentOn( const char_type *start, const char_type *end ) const
        {
          /**
           * if it _isn't_ the case that one fragment starts after the other ends,
           * or ends before the other starts, then, they conflict:
           * 
           *   !(f2.begin >= f1.end || f2.end <= f1.begin)
           * 
           * Simplified, that gives us:
           */
          return ( start < (mData + mLength) && end > mData );
        }

        /**
         * this helper function stores the specified dataFlags in mFlags
         */
      void SetDataFlags(PRUint32 dataFlags)
        {
          NS_ASSERTION((dataFlags & 0xFFFF0000) == 0, "bad flags");
          mFlags = dataFlags | (mFlags & 0xFFFF0000);
        }

      static PRIntn AppendFunc( void* arg, const char* s, PRUint32 len);
      void AppendPrintf( const char* format, va_list ap );

    public:

      // mFlags is a bitwise combination of the following flags.  the meaning
      // and interpretation of these flags is an implementation detail.
      // 
      // NOTE: these flags are declared public _only_ for convenience inside
      // the string implementation.
      
      enum
        {
          F_NONE         = 0,       // no flags

          // data flags are in the lower 16-bits
          F_TERMINATED   = 1 << 0,  // IsTerminated returns true
          F_VOIDED       = 1 << 1,  // IsVoid returns true
          F_SHARED       = 1 << 2,  // mData points to a heap-allocated, shared buffer
          F_OWNED        = 1 << 3,  // mData points to a heap-allocated, raw buffer
          F_FIXED        = 1 << 4,  // mData points to a fixed-size writable, dependent buffer

          // class flags are in the upper 16-bits
          F_CLASS_FIXED  = 1 << 16   // indicates that |this| is of type nsTFixedString
        };

      //
      // Some terminology:
      //
      //   "dependent buffer"    A dependent buffer is one that the string class
      //                         does not own.  The string class relies on some
      //                         external code to ensure the lifetime of the
      //                         dependent buffer.
      //
      //   "shared buffer"       A shared buffer is one that the string class
      //                         allocates.  When it allocates a shared string
      //                         buffer, it allocates some additional space at
      //                         the beginning of the buffer for additional 
      //                         fields, including a reference count and a 
      //                         buffer length.  See nsStringHeader.
      //                         
      //   "adopted buffer"      An adopted buffer is a raw string buffer
      //                         allocated on the heap (using nsMemory::Alloc)
      //                         of which the string class subsumes ownership.
      //
      // Some comments about the string flags:
      //
      //   F_SHARED, F_OWNED, and F_FIXED are all mutually exlusive.  They
      //   indicate the allocation type of mData.  If none of these flags
      //   are set, then the string buffer is dependent.
      //
      //   F_SHARED, F_OWNED, or F_FIXED imply F_TERMINATED.  This is because
      //   the string classes always allocate null-terminated buffers, and
      //   non-terminated substrings are always dependent.
      //
      //   F_VOIDED implies F_TERMINATED, and moreover it implies that mData
      //   points to char_traits::sEmptyBuffer.  Therefore, F_VOIDED is
      //   mutually exclusive with F_SHARED, F_OWNED, and F_FIXED.
      //
  };

int NS_FASTCALL Compare( const nsTSubstring_CharT::base_string_type& lhs, const nsTSubstring_CharT::base_string_type& rhs, const nsTStringComparator_CharT& = nsTDefaultStringComparator_CharT() );


inline
bool operator!=( const nsTSubstring_CharT::base_string_type& lhs, const nsTSubstring_CharT::base_string_type& rhs )
  {
    return !lhs.Equals(rhs);
  }

inline
bool operator< ( const nsTSubstring_CharT::base_string_type& lhs, const nsTSubstring_CharT::base_string_type& rhs )
  {
    return Compare(lhs, rhs)< 0;
  }

inline
bool operator<=( const nsTSubstring_CharT::base_string_type& lhs, const nsTSubstring_CharT::base_string_type& rhs )
  {
    return Compare(lhs, rhs)<=0;
  }

inline
bool operator==( const nsTSubstring_CharT::base_string_type& lhs, const nsTSubstring_CharT::base_string_type& rhs )
  {
    return lhs.Equals(rhs);
  }

inline
bool operator>=( const nsTSubstring_CharT::base_string_type& lhs, const nsTSubstring_CharT::base_string_type& rhs )
  {
    return Compare(lhs, rhs)>=0;
  }

inline
bool operator> ( const nsTSubstring_CharT::base_string_type& lhs, const nsTSubstring_CharT::base_string_type& rhs )
  {
    return Compare(lhs, rhs)> 0;
  }
