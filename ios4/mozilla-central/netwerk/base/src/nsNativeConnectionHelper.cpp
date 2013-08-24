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
 * The Original Code is Mozilla.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2002
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Darin Fisher <darin@netscape.com>
 *   Steve Meredith <smeredith@netscape.com>
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

#include "nsNativeConnectionHelper.h"

#if defined(MOZ_ENABLE_LIBCONIC)
#include "nsAutodialMaemo.h"
#elif defined(WINCE)
#include "nsAutodialWinCE.h"
#else
#include "nsAutodialWin.h"
#endif

#include "nsIOService.h"

//-----------------------------------------------------------------------------
// API typically invoked on the socket transport thread
//-----------------------------------------------------------------------------

PRBool
nsNativeConnectionHelper::OnConnectionFailed(const PRUnichar* hostName)
{
  // On mobile platforms, instead of relying on the link service, we
  // should ask the dialer directly.  This allows the dialer to update
  // link status more forcefully rather than passively watching link
  // status changes.
#if !defined(MOZ_ENABLE_LIBCONIC) && !defined(WINCE_WINDOWS_MOBILE)
    if (gIOService->IsLinkUp())
        return PR_FALSE;
#endif

    nsAutodial autodial;
    if (autodial.ShouldDialOnNetworkError())
        return NS_SUCCEEDED(autodial.DialDefault(hostName));

    return PR_FALSE;
}

PRBool
nsNativeConnectionHelper::IsAutodialEnabled()
{
    nsAutodial autodial;
    return autodial.Init() == NS_OK && autodial.ShouldDialOnNetworkError();
}
