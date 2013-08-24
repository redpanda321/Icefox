/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


  /**
   * NOTE:
   *
   * Try to avoid flat strings.  |PromiseFlat[C]String| will help you as a last
   * resort, and this may be necessary when dealing with legacy or OS calls,
   * but in general, requiring a null-terminated array of characters kills many
   * of the performance wins the string classes offer.  Write your own code to
   * use |nsA[C]String&|s for parameters.  Write your string proccessing
   * algorithms to exploit iterators.  If you do this, you will benefit from
   * being able to chain operations without copying or allocating and your code
   * will be significantly more efficient.  Remember, a function that takes an
   * |const nsA[C]String&| can always be passed a raw character pointer by
   * wrapping it (for free) in a |nsDependent[C]String|.  But a function that
   * takes a character pointer always has the potential to force allocation and
   * copying.
   *
   *
   * How to use it:
   *
   * A |nsPromiseFlat[C]String| doesn't necessarily own the characters it
   * promises.  You must never use it to promise characters out of a string
   * with a shorter lifespan.  The typical use will be something like this:
   *
   *   SomeOSFunction( PromiseFlatCString(aCString).get() ); // GOOD
   *
   * Here's a BAD use:
   *
   *  const char* buffer = PromiseFlatCString(aCString).get();
   *  SomeOSFunction(buffer); // BAD!! |buffer| is a dangling pointer
   *
   * The only way to make one is with the function |PromiseFlat[C]String|,
   * which produce a |const| instance.  ``What if I need to keep a promise
   * around for a little while?'' you might ask.  In that case, you can keep a
   * reference, like so
   *
   *   const nsPromiseFlatString& flat = PromiseFlatString(aString);
   *     // this reference holds the anonymous temporary alive, but remember,
   *     // it must _still_ have a lifetime shorter than that of |aString|
   *
   *  SomeOSFunction(flat.get());
   *  SomeOtherOSFunction(flat.get());
   *
   *
   * How does it work?
   *
   * A |nsPromiseFlat[C]String| is just a wrapper for another string.  If you
   * apply it to a string that happens to be flat, your promise is just a
   * dependent reference to the string's data.  If you apply it to a non-flat
   * string, then a temporary flat string is created for you, by allocating and
   * copying.  In the event that you end up assigning the result into a sharing
   * string (e.g., |nsTString|), the right thing happens.
   */

class nsTPromiseFlatString_CharT : public nsTString_CharT
  {
    public:

      typedef nsTPromiseFlatString_CharT    self_type;

    private:

      void Init( const substring_type& );

        // NOT TO BE IMPLEMENTED
      void operator=( const self_type& );

        // NOT TO BE IMPLEMENTED
      nsTPromiseFlatString_CharT();

    public:

      explicit
      nsTPromiseFlatString_CharT( const substring_type& str )
        : string_type()
        {
          Init(str);
        }

      explicit
      nsTPromiseFlatString_CharT( const substring_tuple_type& tuple )
        : string_type()
        {
          // nothing else to do here except assign the value of the tuple
          // into ourselves.
          Assign(tuple);
        }
  };

  // e.g., PromiseFlatCString(Substring(s))
inline
const nsTPromiseFlatString_CharT
TPromiseFlatString_CharT( const nsTSubstring_CharT& frag )
  {
    return nsTPromiseFlatString_CharT(frag);
  }

  // e.g., PromiseFlatCString(a + b)
inline
const nsTPromiseFlatString_CharT
TPromiseFlatString_CharT( const nsTSubstringTuple_CharT& tuple )
  {
    return nsTPromiseFlatString_CharT(tuple);
  }
