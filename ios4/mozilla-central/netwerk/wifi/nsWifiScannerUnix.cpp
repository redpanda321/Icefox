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
 * The Original Code is Geolocation.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * This is a derivative of work done by Google under a BSD style License.
 * See: http://gears.googlecode.com/svn/trunk/gears/geolocation/
 *
 * Contributor(s):
 *  Doug Turner <dougt@meer.net>  (Original Author)
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

#include "iwlib.h"

#include "dlfcn.h"

#include "nsAutoPtr.h"
#include "nsWifiMonitor.h"
#include "nsWifiAccessPoint.h"

#include "nsIProxyObjectManager.h"
#include "nsServiceManagerUtils.h"
#include "nsComponentManagerUtils.h"
#include "nsIMutableArray.h"




typedef int (*iw_open_t)(void);

typedef void (*iw_enum_t)(int	skfd,
			  iw_enum_handler fn,
			  char *args[],
			  int count);

typedef  int (*iw_stats_t)(int skfd,
			   const char *ifname,
			   iwstats *stats,
			   const iwrange *range,
			   int has_range);

static int scan_wifi(int skfd, char* ifname, char* args[], int count)
{
  nsCOMArray<nsWifiAccessPoint>* accessPoints = (nsCOMArray<nsWifiAccessPoint>*) args[0];
  iw_stats_t iw_stats = (iw_stats_t) args[1];

  struct iwreq wrq;

  int result = iw_get_ext(skfd, ifname, SIOCGIWMODE, &wrq);
  if (result < 0)
    return 0;

  // We only cared about "Managed" mode.  2 is the magic number.  There isn't a #define for this.
  if (wrq.u.mode != 2)
    return 0;

  nsWifiAccessPoint* ap = new nsWifiAccessPoint();
  if (!ap)
    return 0;

  char buffer[128];
  wrq.u.essid.pointer = buffer;
  wrq.u.essid.length  = 128;
  wrq.u.essid.flags   = 0;
  result = iw_get_ext(skfd, ifname, SIOCGIWESSID, &wrq);
  if (result < 0) {
    delete ap;
    return 0;
  }

  ap->setSSID(buffer, wrq.u.essid.length);

  result = iw_get_ext(skfd, ifname, SIOCGIWAP, &wrq);
  if (result < 0) {
    delete ap;
    return 0;
  }
  ap->setMac( (const uint8*) wrq.u.ap_addr.sa_data );

  iwrange range;
  iwstats stats;
  result = (*iw_stats)(skfd, ifname, &stats, &range, 1);
  if (result < 0) {
    delete ap;
    return 0;
  }

  if(stats.qual.level > range.max_qual.level)
    ap->setSignal(stats.qual.level - 0x100);
  else
    ap->setSignal(0);

  accessPoints->AppendObject(ap);
  return 0;
}

nsresult
nsWifiMonitor::DoScan()
{
  void* iwlib_handle = dlopen("libiw.so", RTLD_NOW);
  if (!iwlib_handle) {
    iwlib_handle = dlopen("libiw.so.29", RTLD_NOW);
    if (!iwlib_handle) {
      iwlib_handle = dlopen("libiw.so.30", RTLD_NOW);
      if (!iwlib_handle) {
        LOG(("Could not load libiw\n"));
        return NS_ERROR_NOT_AVAILABLE;
      }
    }
  }
  else {
    LOG(("Loaded libiw\n"));
  }

  iw_open_t iw_open = (iw_open_t) dlsym(iwlib_handle, "iw_sockets_open");
  iw_enum_t iw_enum = (iw_enum_t) dlsym(iwlib_handle, "iw_enum_devices");
  iw_stats_t iw_stats = (iw_stats_t)dlsym(iwlib_handle, "iw_get_stats");

  if (!iw_open || !iw_enum || !iw_stats) {
    dlclose(iwlib_handle);
    LOG(("Could not load a symbol from iwlib.so\n"));
    return NS_ERROR_FAILURE;
  }

  int skfd = (*iw_open)();

  if (skfd < 0) {
    dlclose(iwlib_handle);
    return NS_ERROR_FAILURE;
  }

  nsCOMArray<nsWifiAccessPoint> lastAccessPoints;
  nsCOMArray<nsWifiAccessPoint> accessPoints;

  char* args[] = {(char*) &accessPoints, (char*) iw_stats, nsnull };

  while (mKeepGoing) {

    accessPoints.Clear();

    (*iw_enum)(skfd, &scan_wifi, args, 1);

    PRBool accessPointsChanged = !AccessPointsEqual(accessPoints, lastAccessPoints);
    nsCOMArray<nsIWifiListener> currentListeners;

    {
      nsAutoMonitor mon(mMonitor);

      for (PRUint32 i = 0; i < mListeners.Length(); i++) {
        if (!mListeners[i].mHasSentData || accessPointsChanged) {
          mListeners[i].mHasSentData = PR_TRUE;
          currentListeners.AppendObject(mListeners[i].mListener);
        }
      }
    }

    ReplaceArray(lastAccessPoints, accessPoints);

    if (currentListeners.Count() > 0)
    {
      PRUint32 resultCount = lastAccessPoints.Count();
      nsIWifiAccessPoint** result = static_cast<nsIWifiAccessPoint**> (nsMemory::Alloc(sizeof(nsIWifiAccessPoint*) * resultCount));
      if (!result) {
        dlclose(iwlib_handle);
        return NS_ERROR_OUT_OF_MEMORY;
      }

      for (PRUint32 i = 0; i < resultCount; i++)
        result[i] = lastAccessPoints[i];

      for (PRInt32 i = 0; i < currentListeners.Count(); i++) {

        LOG(("About to send data to the wifi listeners\n"));

        nsCOMPtr<nsIWifiListener> proxy;
        nsCOMPtr<nsIProxyObjectManager> proxyObjMgr = do_GetService("@mozilla.org/xpcomproxy;1");
        proxyObjMgr->GetProxyForObject(NS_PROXY_TO_MAIN_THREAD,
                                       NS_GET_IID(nsIWifiListener),
                                       currentListeners[i],
                                       NS_PROXY_SYNC | NS_PROXY_ALWAYS,
                                       getter_AddRefs(proxy));
        if (!proxy) {
          LOG(("There is no proxy available.  this should never happen\n"));
        }
        else
        {
          nsresult rv = proxy->OnChange(result, resultCount);
          LOG( ("... sent %d\n", rv));
        }
      }

      nsMemory::Free(result);
    }

    LOG(("waiting on monitor\n"));

    nsAutoMonitor mon(mMonitor);
    mon.Wait(PR_SecondsToInterval(60));
  }

  iw_sockets_close(skfd);

  return NS_OK;
}
