/* -*- Mode: C++; tab-width: 40; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 *   Mozilla Corp
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *    Ted Mielczarek <ted.mielczarek@gmail.com>
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

#import <UIKit/UIScreen.h>

#include "gfxPoint.h"
#include "nsScreenManager.h"

static nsIntRect gScreenBounds;

NS_IMPL_ISUPPORTS1(nsUIKitScreen, nsIScreen)

NS_IMETHODIMP
nsUIKitScreen::GetRect(PRInt32 *outLeft, PRInt32 *outTop, PRInt32 *outWidth, PRInt32 *outHeight)
{
  CGRect rect = [[UIScreen mainScreen] bounds];

  *outLeft = rect.origin.x;
  *outTop = rect.origin.y;
  *outWidth = rect.size.width;
  *outHeight = rect.size.height;

  return NS_OK;
}


NS_IMETHODIMP
nsUIKitScreen::GetAvailRect(PRInt32 *outLeft, PRInt32 *outTop, PRInt32 *outWidth, PRInt32 *outHeight)
{
  CGRect rect = [[UIScreen mainScreen] applicationFrame];

  *outLeft = rect.origin.x;
  *outTop = rect.origin.y;
  *outWidth = rect.size.width;
  *outHeight = rect.size.height;

  return NS_OK;
}

NS_IMETHODIMP
nsUIKitScreen::GetPixelDepth(PRInt32 *aPixelDepth)
{
  // XXX: this probably isn't right, but who knows?
  *aPixelDepth = 24;
  return NS_OK;
}

NS_IMETHODIMP
nsUIKitScreen::GetColorDepth(PRInt32 *aColorDepth)
{
  return GetPixelDepth(aColorDepth);
}

NS_IMPL_ISUPPORTS1(nsScreenManager, nsIScreenManager)

nsScreenManager::nsScreenManager()
: mScreen(new nsUIKitScreen(nsnull))
{
}

nsIntRect
nsScreenManager::GetBounds()
{
    //XXX: this doesn't work right if your app hasn't finished launching.
    // the status bar isn't there yet, so you get the full screen
    CGRect rect = [[UIScreen mainScreen] applicationFrame];
    gScreenBounds.x = rect.origin.x;
    gScreenBounds.y = rect.origin.y;
    gScreenBounds.width = rect.size.width;
    gScreenBounds.height = rect.size.height;
    printf("nsScreenManager::GetBounds: %d %d %d %d\n",
           gScreenBounds.x, gScreenBounds.y, gScreenBounds.width, gScreenBounds.height);
    return gScreenBounds;
}

NS_IMETHODIMP
nsScreenManager::GetPrimaryScreen(nsIScreen **outScreen)
{
  NS_IF_ADDREF(*outScreen = mScreen.get());
  return NS_OK;
}

NS_IMETHODIMP
nsScreenManager::ScreenForRect(PRInt32 inLeft,
                               PRInt32 inTop,
                               PRInt32 inWidth,
                               PRInt32 inHeight,
                               nsIScreen **outScreen)
{
  return GetPrimaryScreen(outScreen);
}

NS_IMETHODIMP
nsScreenManager::ScreenForNativeWidget(void *aWidget, nsIScreen **outScreen)
{
  return GetPrimaryScreen(outScreen);
}

NS_IMETHODIMP
nsScreenManager::GetNumberOfScreens(PRUint32 *aNumberOfScreens)
{
  //TODO: support multiple screens
  *aNumberOfScreens = 1;
  return NS_OK;
}

