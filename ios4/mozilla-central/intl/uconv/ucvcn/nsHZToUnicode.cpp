/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
/**
 * A character set converter from HZ to Unicode.
 * 
 *
 * @created         08/Sept/1999
 * @author  Yueheng Xu, Yueheng.Xu@intel.com
 *
 * Note: in this HZ-GB-2312 converter, we accept a string composed of 7-bit HZ 
 *       encoded Chinese chars,as it is defined in RFC1843 available at 
 *       http://www.cis.ohio-state.edu/htbin/rfc/rfc1843.html
 *       and RFC1842 available at http://www.cis.ohio-state.edu/htbin/rfc/rfc1842.html.
 *        
 *       In an effort to match the similar extended capability of Microsoft Internet Explorer
 *       5.0. We also accept the 8-bit GB encoded chars mixed in a HZ string.
 *       But this should not be a recommendedd practice for HTML authors.
 *
 *       The priority of converting are as follows: first convert 8-bit GB code; then,
 *       consume HZ ESC sequences such as '~{', '~}', '~~'; then, depending on the current
 *       state ( default to ASCII state ) of the string, each 7-bit char is converted as an 
 *       ASCII, or two 7-bit chars are converted into a Chinese character.
 */



#include "nsUCvCnDll.h"
#include "nsHZToUnicode.h"
#include "gbku.h"

//----------------------------------------------------------------------
// Class nsHZToUnicode [implementation]

//----------------------------------------------------------------------
// Subclassing of nsTablesDecoderSupport class [implementation]

#define HZ_STATE_GB     1
#define HZ_STATE_ASCII  2
#define HZ_STATE_ODD_BYTE_FLAG 0x80
#define HZLEAD1 '~'
#define HZLEAD2 '{'
#define HZLEAD3 '}'
#define HZLEAD4 '\n'
#define HZ_ODD_BYTE_STATE (mHZState & (HZ_STATE_ODD_BYTE_FLAG))
#define HZ_ENCODING_STATE (mHZState & ~(HZ_STATE_ODD_BYTE_FLAG))

nsHZToUnicode::nsHZToUnicode() : nsBufferDecoderSupport(1)
{
  mHZState = HZ_STATE_ASCII;    // per HZ spec, default to ASCII state 
  mRunLength = 0;
  mOddByte = 0;
}

//Overwriting the ConvertNoBuff() in nsUCvCnSupport.cpp.
NS_IMETHODIMP nsHZToUnicode::ConvertNoBuff(
  const char* aSrc, 
  PRInt32 * aSrcLength, 
  PRUnichar *aDest, 
  PRInt32 * aDestLength)
{
  PRInt32 i=0;
  PRInt32 iSrcLength = *aSrcLength;
  PRInt32 iDestlen = 0;
  *aSrcLength=0;
  nsresult res = NS_OK;
  char oddByte = mOddByte;

  for (i=0; i<iSrcLength; i++) {
    if (iDestlen >= (*aDestLength)) {
      res = NS_OK_UDEC_MOREOUTPUT;
      break;
    }

    char srcByte = *aSrc++;
    (*aSrcLength)++;
    if (!HZ_ODD_BYTE_STATE) {
      if (srcByte & 0x80 || srcByte == HZLEAD1 || HZ_ENCODING_STATE == HZ_STATE_GB) { 
        oddByte = srcByte;
        mHZState |= HZ_STATE_ODD_BYTE_FLAG;
      } else {
        *aDest++ = CAST_CHAR_TO_UNICHAR(srcByte);
        iDestlen++;
      }
    } else {
      if (oddByte & 0x80) { // if it is a 8-bit byte
        if (UINT8_IN_RANGE(0x81, oddByte, 0xFE) &&
            UINT8_IN_RANGE(0x40, srcByte, 0xFE)) {
          // The source is a 8-bit GBCode
          *aDest++ = mUtil.GBKCharToUnicode(oddByte, srcByte);
        } else {
          *aDest++ = UCS2_NO_MAPPING;
        }
        iDestlen++;
      // otherwise, it is a 7-bit byte 
      // The source will be an ASCII or a 7-bit HZ code depending on oddByte
      } else if (oddByte == HZLEAD1) { // if it is lead by '~'
        switch (srcByte) {
          case HZLEAD2: 
            // we got a '~{'
            // we are switching to HZ state
            mHZState = HZ_STATE_GB | HZ_ODD_BYTE_STATE;
            mRunLength = 0;
            break;

          case HZLEAD3: 
            // we got a '~}'
            // we are switching to ASCII state
            mHZState = HZ_STATE_ASCII | HZ_ODD_BYTE_STATE;
            if (mRunLength == 0) {
              *aDest++ = UCS2_NO_MAPPING;
              iDestlen++;
            }
            mRunLength = 0;
            break;

          case HZLEAD1: 
            // we got a '~~', process like an ASCII, but no state change
            *aDest++ = CAST_CHAR_TO_UNICHAR(srcByte);
            iDestlen++;
            mRunLength++;
            break;

          case HZLEAD4:   
            // we got a "~\n", it means maintain double byte mode cross lines,
            // ignore the '~' itself
            //  mHZState = HZ_STATE_GB; 
            // I find that "~\n" should interpreted as line continuation
            // without mode change
            // It should not be interpreted as line continuation with double
            // byte mode on
            break;

          default:
            // undefined ESC sequence '~X' are ignored since this is an
            // illegal combination 
            *aDest++ = UCS2_NO_MAPPING;
            iDestlen++;
            break;
        }
      } else if (HZ_ENCODING_STATE == HZ_STATE_GB) {
        *aDest++ = mUtil.GBKCharToUnicode(oddByte|0x80, srcByte|0x80);
        mRunLength++;
        iDestlen++;
      } else {
        NS_NOTREACHED("2-byte sequence that we don't know how to handle");
        *aDest++ = UCS2_NO_MAPPING;
        iDestlen++;
      }
      oddByte = 0;
      mHZState &= ~HZ_STATE_ODD_BYTE_FLAG;
    }
  } // for loop
  mOddByte = HZ_ODD_BYTE_STATE ? oddByte : 0;
  *aDestLength = iDestlen;
  return res;
}


