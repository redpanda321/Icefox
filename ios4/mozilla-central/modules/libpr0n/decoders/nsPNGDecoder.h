/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
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
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Stuart Parmenter <pavlov@netscape.com>
 *   Bobby Holley <bobbyholley@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#ifndef nsPNGDecoder_h__
#define nsPNGDecoder_h__

#include "Decoder.h"

#include "imgIDecoderObserver.h"
#include "gfxASurface.h"

#include "nsCOMPtr.h"

#include "png.h"

#include "qcms.h"

namespace mozilla {
namespace imagelib {
class RasterImage;

class nsPNGDecoder : public Decoder
{
public:
  nsPNGDecoder();
  virtual ~nsPNGDecoder();

  virtual nsresult InitInternal();
  virtual nsresult WriteInternal(const char* aBuffer, PRUint32 aCount);
  virtual nsresult FinishInternal();

  void CreateFrame(png_uint_32 x_offset, png_uint_32 y_offset,
                   PRInt32 width, PRInt32 height,
                   gfxASurface::gfxImageFormat format);
  void SetAnimFrameInfo();

  void EndImageFrame();
  void NotifyDone(PRBool aSuccess);

public:
  png_structp mPNG;
  png_infop mInfo;
  nsIntRect mFrameRect;
  PRUint8 *mCMSLine;
  PRUint8 *interlacebuf;
  PRUint8 *mImageData;
  qcms_profile *mInProfile;
  qcms_transform *mTransform;

  gfxASurface::gfxImageFormat format;

  // For size decodes
  PRUint8 *mHeaderBuf;
  PRUint32 mHeaderBytesRead;

  PRUint8 mChannels;
  PRPackedBool mError;
  PRPackedBool mFrameHasNoAlpha;
  PRPackedBool mFrameIsHidden;
  PRPackedBool mNotifiedDone;

  /*
   * libpng callbacks
   *
   * We put these in the class so that they can access protected members.
   */
  static void PNGAPI info_callback(png_structp png_ptr, png_infop info_ptr);
  static void PNGAPI row_callback(png_structp png_ptr, png_bytep new_row,
                                  png_uint_32 row_num, int pass);
#ifdef PNG_APNG_SUPPORTED
  static void PNGAPI frame_info_callback(png_structp png_ptr,
                                         png_uint_32 frame_num);
#endif
  static void PNGAPI end_callback(png_structp png_ptr, png_infop info_ptr);
  static void PNGAPI error_callback(png_structp png_ptr,
                                    png_const_charp error_msg);
  static void PNGAPI warning_callback(png_structp png_ptr,
                                      png_const_charp warning_msg);
};

} // namespace imagelib
} // namespace mozilla

#endif // nsPNGDecoder_h__
