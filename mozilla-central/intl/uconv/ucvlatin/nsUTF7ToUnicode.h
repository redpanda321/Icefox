/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsUTF7ToUnicode_h___
#define nsUTF7ToUnicode_h___

#include "nsUCSupport.h"

//----------------------------------------------------------------------
// Class nsBasicUTF7Decoder [declaration]

/**
 * Basic class for a character set converter from UTF-7 to Unicode.
 *
 * @created         03/Jun/1999
 * @author  Catalin Rotaru [CATA]
 */
class nsBasicUTF7Decoder : public nsBufferDecoderSupport
{
public:

  /**
   * Class constructor.
   */
  nsBasicUTF7Decoder(char aLastChar, char aEscChar);

protected:

  int32_t                   mEncoding;      // current encoding
  uint32_t                  mEncBits;
  int32_t                   mEncStep;
  char                      mLastChar;
  char                      mEscChar;
  bool                      mFreshBase64;

  nsresult DecodeDirect(const char * aSrc, int32_t * aSrcLength, 
      PRUnichar * aDest, int32_t * aDestLength);
  nsresult DecodeBase64(const char * aSrc, int32_t * aSrcLength, 
      PRUnichar * aDest, int32_t * aDestLength);
  uint32_t CharToValue(char aChar);

  //--------------------------------------------------------------------
  // Subclassing of nsBufferDecoderSupport class [declaration]

  NS_IMETHOD ConvertNoBuff(const char * aSrc, int32_t * aSrcLength, 
      PRUnichar * aDest, int32_t * aDestLength);
  NS_IMETHOD Reset();
};

//----------------------------------------------------------------------
// Class nsUTF7ToUnicode [declaration]

/**
 * A character set converter from Modified UTF7 to Unicode.
 *
 * @created         18/May/1999
 * @author  Catalin Rotaru [CATA]
 */
class nsUTF7ToUnicode : public nsBasicUTF7Decoder 
{
public:

  /**
   * Class constructor.
   */
  nsUTF7ToUnicode();

};

#endif /* nsUTF7ToUnicode_h___ */
