/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsFontFaceList_h__
#define __nsFontFaceList_h__

#include "nsIDOMFontFaceList.h"
#include "nsIDOMFontFace.h"
#include "nsCOMPtr.h"
#include "nsInterfaceHashtable.h"
#include "nsHashKeys.h"
#include "gfxFont.h"

class gfxTextRun;
class nsIFrame;

class nsFontFaceList : public nsIDOMFontFaceList
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMFONTFACELIST

  nsFontFaceList();
  virtual ~nsFontFaceList();

  nsresult AddFontsFromTextRun(gfxTextRun* aTextRun,
                               PRUint32 aOffset, PRUint32 aLength,
                               nsIFrame* aFrame);

protected:
  nsInterfaceHashtable<nsPtrHashKey<gfxFontEntry>,nsIDOMFontFace> mFontFaces;
};

#endif // __nsFontFaceList_h__
