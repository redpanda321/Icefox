/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsReadableUtils_h___
#define nsReadableUtils_h___

  /**
   * I guess all the routines in this file are all mis-named.
   * According to our conventions, they should be |NS_xxx|.
   */

#ifndef nsAString_h___
#include "nsAString.h"
#endif
#include "nsTArray.h"

inline size_t Distance( const nsReadingIterator<PRUnichar>& start, const nsReadingIterator<PRUnichar>& end )
  {
    return end.get() - start.get();
  }
inline size_t Distance( const nsReadingIterator<char>& start, const nsReadingIterator<char>& end )
  {
    return end.get() - start.get();
  }

void LossyCopyUTF16toASCII( const nsAString& aSource, nsACString& aDest NS_OUTPARAM );
void CopyASCIItoUTF16( const nsACString& aSource, nsAString& aDest NS_OUTPARAM );

void LossyCopyUTF16toASCII( const PRUnichar* aSource, nsACString& aDest NS_OUTPARAM );
void CopyASCIItoUTF16( const char* aSource, nsAString& aDest NS_OUTPARAM );

void CopyUTF16toUTF8( const nsAString& aSource, nsACString& aDest NS_OUTPARAM );
void CopyUTF8toUTF16( const nsACString& aSource, nsAString& aDest NS_OUTPARAM );

void CopyUTF16toUTF8( const PRUnichar* aSource, nsACString& aDest NS_OUTPARAM );
void CopyUTF8toUTF16( const char* aSource, nsAString& aDest NS_OUTPARAM );

void LossyAppendUTF16toASCII( const nsAString& aSource, nsACString& aDest );
void AppendASCIItoUTF16( const nsACString& aSource, nsAString& aDest );

void LossyAppendUTF16toASCII( const PRUnichar* aSource, nsACString& aDest );
void AppendASCIItoUTF16( const char* aSource, nsAString& aDest );

void AppendUTF16toUTF8( const nsAString& aSource, nsACString& aDest );
void AppendUTF8toUTF16( const nsACString& aSource, nsAString& aDest );

void AppendUTF16toUTF8( const PRUnichar* aSource, nsACString& aDest );
void AppendUTF8toUTF16( const char* aSource, nsAString& aDest );

  /**
   * Returns a new |char| buffer containing a zero-terminated copy of |aSource|.
   *
   * Allocates and returns a new |char| buffer which you must free with |nsMemory::Free|.
   * Performs a lossy encoding conversion by chopping 16-bit wide characters down to 8-bits wide while copying |aSource| to your new buffer.
   * This conversion is not well defined; but it reproduces legacy string behavior.
   * The new buffer is zero-terminated, but that may not help you if |aSource| contains embedded nulls.
   *
   * @param aSource a 16-bit wide string
   * @return a new |char| buffer you must free with |nsMemory::Free|.
   */
char* ToNewCString( const nsAString& aSource );


  /**
   * Returns a new |char| buffer containing a zero-terminated copy of |aSource|.
   *
   * Allocates and returns a new |char| buffer which you must free with |nsMemory::Free|.
   * The new buffer is zero-terminated, but that may not help you if |aSource| contains embedded nulls.
   *
   * @param aSource an 8-bit wide string
   * @return a new |char| buffer you must free with |nsMemory::Free|.
   */
char* ToNewCString( const nsACString& aSource );

  /**
   * Returns a new |char| buffer containing a zero-terminated copy of |aSource|.
   *
   * Allocates and returns a new |char| buffer which you must free with 
   * |nsMemory::Free|.
   * Performs an encoding conversion from a UTF-16 string to a UTF-8 string
   * copying |aSource| to your new buffer.
   * The new buffer is zero-terminated, but that may not help you if |aSource| 
   * contains embedded nulls.
   *
   * @param aSource a UTF-16 string (made of PRUnichar's)
   * @param aUTF8Count the number of 8-bit units that was returned
   * @return a new |char| buffer you must free with |nsMemory::Free|.
   */

char* ToNewUTF8String( const nsAString& aSource, PRUint32 *aUTF8Count = nsnull );


  /**
   * Returns a new |PRUnichar| buffer containing a zero-terminated copy of 
   * |aSource|.
   *
   * Allocates and returns a new |PRUnichar| buffer which you must free with 
   * |nsMemory::Free|.
   * The new buffer is zero-terminated, but that may not help you if |aSource| 
   * contains embedded nulls.
   *
   * @param aSource a UTF-16 string
   * @return a new |PRUnichar| buffer you must free with |nsMemory::Free|.
   */
PRUnichar* ToNewUnicode( const nsAString& aSource );


  /**
   * Returns a new |PRUnichar| buffer containing a zero-terminated copy of |aSource|.
   *
   * Allocates and returns a new |PRUnichar| buffer which you must free with |nsMemory::Free|.
   * Performs an encoding conversion by 0-padding 8-bit wide characters up to 16-bits wide while copying |aSource| to your new buffer.
   * This conversion is not well defined; but it reproduces legacy string behavior.
   * The new buffer is zero-terminated, but that may not help you if |aSource| contains embedded nulls.
   *
   * @param aSource an 8-bit wide string (a C-string, NOT UTF-8)
   * @return a new |PRUnichar| buffer you must free with |nsMemory::Free|.
   */
PRUnichar* ToNewUnicode( const nsACString& aSource );

  /**
   * Returns a new |PRUnichar| buffer containing a zero-terminated copy
   * of |aSource|.
   *
   * Allocates and returns a new |char| buffer which you must free with
   * |nsMemory::Free|.  Performs an encoding conversion from UTF-8 to UTF-16 
   * while copying |aSource| to your new buffer.  This conversion is well defined
   * for a valid UTF-8 string.  The new buffer is zero-terminated, but that 
   * may not help you if |aSource| contains embedded nulls.
   *
   * @param aSource an 8-bit wide string, UTF-8 encoded
   * @param aUTF16Count the number of 16-bit units that was returned
   * @return a new |PRUnichar| buffer you must free with |nsMemory::Free|.
   *         (UTF-16 encoded)
   */
PRUnichar* UTF8ToNewUnicode( const nsACString& aSource, PRUint32 *aUTF16Count = nsnull );

  /**
   * Copies |aLength| 16-bit code units from the start of |aSource| to the
   * |PRUnichar| buffer |aDest|.
   *
   * After this operation |aDest| is not null terminated.
   *
   * @param aSource a UTF-16 string
   * @param aSrcOffset start offset in the source string
   * @param aDest a |PRUnichar| buffer
   * @param aLength the number of 16-bit code units to copy
   * @return pointer to destination buffer - identical to |aDest|
   */
PRUnichar* CopyUnicodeTo( const nsAString& aSource,
                                 PRUint32 aSrcOffset,
                                 PRUnichar* aDest,
                                 PRUint32 aLength );


  /**
   * Copies 16-bit characters between iterators |aSrcStart| and
   * |aSrcEnd| to the writable string |aDest|. Similar to the
   * |nsString::Mid| method.
   *
   * After this operation |aDest| is not null terminated.
   *
   * @param aSrcStart start source iterator
   * @param aSrcEnd end source iterator
   * @param aDest destination for the copy
   */
void CopyUnicodeTo( const nsAString::const_iterator& aSrcStart,
                           const nsAString::const_iterator& aSrcEnd,
                           nsAString& aDest );

  /**
   * Appends 16-bit characters between iterators |aSrcStart| and
   * |aSrcEnd| to the writable string |aDest|. 
   *
   * After this operation |aDest| is not null terminated.
   *
   * @param aSrcStart start source iterator
   * @param aSrcEnd end source iterator
   * @param aDest destination for the copy
   */
void AppendUnicodeTo( const nsAString::const_iterator& aSrcStart,
                             const nsAString::const_iterator& aSrcEnd,
                             nsAString& aDest );

  /**
   * Returns |true| if |aString| contains only ASCII characters, that is, characters in the range (0x00, 0x7F).
   *
   * @param aString a 16-bit wide string to scan
   */
bool IsASCII( const nsAString& aString );

  /**
   * Returns |true| if |aString| contains only ASCII characters, that is, characters in the range (0x00, 0x7F).
   *
   * @param aString a 8-bit wide string to scan
   */
bool IsASCII( const nsACString& aString );

  /**
   * Returns |true| if |aString| is a valid UTF-8 string.
   * XXX This is not bullet-proof and nor an all-purpose UTF-8 validator. 
   * It is mainly written to replace and roughly equivalent to
   *
   *    str.Equals(NS_ConvertUTF16toUTF8(NS_ConvertUTF8toUTF16(str)))
   *
   * (see bug 191541)
   * As such,  it does not check for non-UTF-8 7bit encodings such as 
   * ISO-2022-JP and HZ. 
   *
   * It rejects sequences with the following errors:
   *
   * byte sequences that cannot be decoded into characters according to
   *   UTF-8's rules (including cases where the input is part of a valid
   *   UTF-8 sequence but starts or ends mid-character)
   * overlong sequences (i.e., cases where a character was encoded
   *   non-canonically by using more bytes than necessary)
   * surrogate codepoints (i.e., the codepoints reserved for
       representing astral characters in UTF-16)
   * codepoints above the unicode range (i.e., outside the first 17
   *   planes; higher than U+10FFFF), in accordance with
   *   http://tools.ietf.org/html/rfc3629
   * when aRejectNonChar is true (the default), any codepoint whose low
   *   16 bits are 0xFFFE or 0xFFFF

   *
   * @param aString an 8-bit wide string to scan
   * @param aRejectNonChar a boolean to control the rejection of utf-8
   *        non characters
   */
bool IsUTF8( const nsACString& aString, bool aRejectNonChar = true );

bool ParseString(const nsACString& aAstring, char aDelimiter, 
                          nsTArray<nsCString>& aArray);

  /**
   * Converts case in place in the argument string.
   */
void ToUpperCase( nsACString& );

void ToLowerCase( nsACString& );

void ToUpperCase( nsCSubstring& );

void ToLowerCase( nsCSubstring& );

  /**
   * Converts case from string aSource to aDest.
   */
void ToUpperCase( const nsACString& aSource, nsACString& aDest );

void ToLowerCase( const nsACString& aSource, nsACString& aDest );

  /**
   * Finds the leftmost occurrence of |aPattern|, if any in the range |aSearchStart|..|aSearchEnd|.
   *
   * Returns |true| if a match was found, and adjusts |aSearchStart| and |aSearchEnd| to
   * point to the match.  If no match was found, returns |false| and makes |aSearchStart == aSearchEnd|.
   *
   * Currently, this is equivalent to the O(m*n) implementation previously on |ns[C]String|.
   * If we need something faster, then we can implement that later.
   */

bool FindInReadable( const nsAString& aPattern, nsAString::const_iterator&, nsAString::const_iterator&, const nsStringComparator& = nsDefaultStringComparator() );
bool FindInReadable( const nsACString& aPattern, nsACString::const_iterator&, nsACString::const_iterator&, const nsCStringComparator& = nsDefaultCStringComparator() );

/* sometimes we don't care about where the string was, just that we
 * found it or not */
inline bool FindInReadable( const nsAString& aPattern, const nsAString& aSource, const nsStringComparator& compare = nsDefaultStringComparator() )
{
  nsAString::const_iterator start, end;
  aSource.BeginReading(start);
  aSource.EndReading(end);
  return FindInReadable(aPattern, start, end, compare);
}

inline bool FindInReadable( const nsACString& aPattern, const nsACString& aSource, const nsCStringComparator& compare = nsDefaultCStringComparator() )
{
  nsACString::const_iterator start, end;
  aSource.BeginReading(start);
  aSource.EndReading(end);
  return FindInReadable(aPattern, start, end, compare);
}


bool CaseInsensitiveFindInReadable( const nsACString& aPattern, nsACString::const_iterator&, nsACString::const_iterator& );

  /**
   * Finds the rightmost occurrence of |aPattern| 
   * Returns |true| if a match was found, and adjusts |aSearchStart| and |aSearchEnd| to
   * point to the match.  If no match was found, returns |false| and makes |aSearchStart == aSearchEnd|.
   *
   */
bool RFindInReadable( const nsAString& aPattern, nsAString::const_iterator&, nsAString::const_iterator&, const nsStringComparator& = nsDefaultStringComparator() );
bool RFindInReadable( const nsACString& aPattern, nsACString::const_iterator&, nsACString::const_iterator&, const nsCStringComparator& = nsDefaultCStringComparator() );

   /**
   * Finds the leftmost occurrence of |aChar|, if any in the range 
   * |aSearchStart|..|aSearchEnd|.
   *
   * Returns |true| if a match was found, and adjusts |aSearchStart| to
   * point to the match.  If no match was found, returns |false| and 
   * makes |aSearchStart == aSearchEnd|.
   */
bool FindCharInReadable( PRUnichar aChar, nsAString::const_iterator& aSearchStart, const nsAString::const_iterator& aSearchEnd );
bool FindCharInReadable( char aChar, nsACString::const_iterator& aSearchStart, const nsACString::const_iterator& aSearchEnd );

    /**
    * Finds the number of occurences of |aChar| in the string |aStr|
    */
PRUint32 CountCharInReadable( const nsAString& aStr,
                                     PRUnichar aChar );
PRUint32 CountCharInReadable( const nsACString& aStr,
                                     char aChar );

bool
StringBeginsWith( const nsAString& aSource, const nsAString& aSubstring,
                  const nsStringComparator& aComparator =
                                              nsDefaultStringComparator() );
bool
StringBeginsWith( const nsACString& aSource, const nsACString& aSubstring,
                  const nsCStringComparator& aComparator =
                                               nsDefaultCStringComparator() );
bool
StringEndsWith( const nsAString& aSource, const nsAString& aSubstring,
                const nsStringComparator& aComparator =
                                            nsDefaultStringComparator() );
bool
StringEndsWith( const nsACString& aSource, const nsACString& aSubstring,
                const nsCStringComparator& aComparator =
                                             nsDefaultCStringComparator() );

const nsAFlatString& EmptyString();
const nsAFlatCString& EmptyCString();

const nsAFlatString& NullString();
const nsAFlatCString& NullCString();

   /**
   * Compare a UTF-8 string to an UTF-16 string.
   *
   * Returns 0 if the strings are equal, -1 if aUTF8String is less
   * than aUTF16Count, and 1 in the reverse case.  In case of fatal
   * error (eg the strings are not valid UTF8 and UTF16 respectively),
   * this method will return PR_INT32_MIN.
   */
PRInt32
CompareUTF8toUTF16(const nsASingleFragmentCString& aUTF8String,
                   const nsASingleFragmentString& aUTF16String);

void
AppendUCS4ToUTF16(const PRUint32 aSource, nsAString& aDest);

template<class T>
inline bool EnsureStringLength(T& aStr, PRUint32 aLen)
{
    aStr.SetLength(aLen);
    return (aStr.Length() == aLen);
}

#endif // !defined(nsReadableUtils_h___)
