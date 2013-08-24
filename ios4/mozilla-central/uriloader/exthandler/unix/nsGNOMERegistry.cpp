/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is the GNOME helper app implementation.
 *
 * The Initial Developer of the Original Code is
 * IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2003
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Brian Ryner <bryner@brianryner.com>  (Original Author)
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

#include "nsGNOMERegistry.h"
#include "prlink.h"
#include "prmem.h"
#include "nsString.h"
#include "nsIComponentManager.h"
#include "nsILocalFile.h"
#include "nsMIMEInfoUnix.h"
#include "nsAutoPtr.h"
#include "nsIGConfService.h"
#include "nsIGnomeVFSService.h"
#include "nsIGIOService.h"

#ifdef MOZ_WIDGET_GTK2
#include <glib.h>
#include <glib-object.h>
#endif

#ifdef MOZ_PLATFORM_MAEMO
#include <libintl.h>
#endif

/* static */ PRBool
nsGNOMERegistry::HandlerExists(const char *aProtocolScheme)
{
  nsCOMPtr<nsIGConfService> gconf = do_GetService(NS_GCONFSERVICE_CONTRACTID);
  if (!gconf)
    return PR_FALSE;

  PRBool isEnabled;
  nsCAutoString handler;
  if (NS_FAILED(gconf->GetAppForProtocol(nsDependentCString(aProtocolScheme), &isEnabled, handler)))
    return PR_FALSE;

  return isEnabled;
}

// XXX Check HandlerExists() before calling LoadURL.
//
// If there is not a registered handler for the protocol, gnome_url_show()
// falls back to using gnomevfs modules.  See bug 389632.  We don't want
// this fallback to happen as we are not sure of the safety of all gnomevfs
// modules and MIME-default applications.  (gnomevfs should be handled in
// nsGnomeVFSProtocolHandler.)

/* static */ nsresult
nsGNOMERegistry::LoadURL(nsIURI *aURL)
{
  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
  nsCOMPtr<nsIGnomeVFSService> gnomevfs = do_GetService(NS_GNOMEVFSSERVICE_CONTRACTID);
  if (giovfs) {
    return giovfs->ShowURI(aURL);
  } else if (gnomevfs) {
    /* Fallback to GnomeVFS */
    return gnomevfs->ShowURI(aURL);
  }
  return NS_ERROR_FAILURE;
}

/* static */ void
nsGNOMERegistry::GetAppDescForScheme(const nsACString& aScheme,
                                     nsAString& aDesc)
{
  nsCOMPtr<nsIGConfService> gconf = do_GetService(NS_GCONFSERVICE_CONTRACTID);
  if (!gconf)
    return;

  PRBool isEnabled;
  nsCAutoString app;
  if (NS_FAILED(gconf->GetAppForProtocol(aScheme, &isEnabled, app)))
    return;

  if (!app.IsEmpty()) {
    // Try to only provide the executable name, as it is much simpler than with the path and arguments
    PRInt32 firstSpace = app.FindChar(' ');
    if (firstSpace != kNotFound) {
      app.Truncate(firstSpace);
      PRInt32 lastSlash = app.RFindChar('/');
      if (lastSlash != kNotFound) {
        app.Cut(0, lastSlash + 1);
      }
    }

    CopyUTF8toUTF16(app, aDesc);
  }
}


/* static */ already_AddRefed<nsMIMEInfoBase>
nsGNOMERegistry::GetFromExtension(const nsACString& aFileExt)
{
  nsCAutoString mimeType;
  nsCOMPtr<nsIGnomeVFSService> gnomevfs = do_GetService(NS_GNOMEVFSSERVICE_CONTRACTID);
  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);

  if (!gnomevfs && !giovfs)
    return nsnull;

  if (giovfs) {
    // Get the MIME type from the extension, then call GetFromType to
    // fill in the MIMEInfo.
    if (NS_FAILED(giovfs->GetMimeTypeFromExtension(aFileExt, mimeType)) ||
        mimeType.EqualsLiteral("application/octet-stream"))
      return nsnull;
  } else if (gnomevfs) {
    /* Fallback to GnomeVFS */
    if (NS_FAILED(gnomevfs->GetMimeTypeFromExtension(aFileExt, mimeType)) ||
        mimeType.EqualsLiteral("application/octet-stream"))
      return nsnull;
    
  }


  return GetFromType(mimeType);
}

/* static */ already_AddRefed<nsMIMEInfoBase>
nsGNOMERegistry::GetFromType(const nsACString& aMIMEType)
{
  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
  nsCOMPtr<nsIGnomeVFSService> gnomevfs = do_GetService(NS_GNOMEVFSSERVICE_CONTRACTID);
  nsCOMPtr<nsIGIOMimeApp> gioHandlerApp;
  nsCOMPtr<nsIGnomeVFSMimeApp> gnomeHandlerApp;
  
  if (!giovfs && !gnomevfs)
    return nsnull;

  if (giovfs) {
    if (NS_FAILED(giovfs->GetAppForMimeType(aMIMEType, getter_AddRefs(gioHandlerApp))) ||
        !gioHandlerApp)
      return nsnull;

  } else {
    /* Fallback to GnomeVFS*/
    if (NS_FAILED(gnomevfs->GetAppForMimeType(aMIMEType, getter_AddRefs(gnomeHandlerApp))) ||
        !gnomeHandlerApp)
      return nsnull;
    
  }
  nsRefPtr<nsMIMEInfoUnix> mimeInfo = new nsMIMEInfoUnix(aMIMEType);
  NS_ENSURE_TRUE(mimeInfo, nsnull);

  nsCAutoString description;
  if (giovfs)
    giovfs->GetDescriptionForMimeType(aMIMEType, description);
  else
    gnomevfs->GetDescriptionForMimeType(aMIMEType, description);

  mimeInfo->SetDescription(NS_ConvertUTF8toUTF16(description));

  nsCAutoString name;
  if (giovfs)
    gioHandlerApp->GetName(name);
  else 
    gnomeHandlerApp->GetName(name);

#ifdef MOZ_PLATFORM_MAEMO
  // On Maemo/Hildon, GetName ends up calling gnome_vfs_mime_application_get_name,
  // which happens to return a non-localized message-id for the application. To
  // get the localized name for the application, we have to call dgettext with 
  // the default maemo domain-name to try and translate the string into the operating 
  // system's native language.
  const char kDefaultTextDomain [] = "maemo-af-desktop";
  nsCAutoString realName (dgettext(kDefaultTextDomain, PromiseFlatCString(name).get()));
  mimeInfo->SetDefaultDescription(NS_ConvertUTF8toUTF16(realName));
#else
  mimeInfo->SetDefaultDescription(NS_ConvertUTF8toUTF16(name));
#endif
  mimeInfo->SetPreferredAction(nsIMIMEInfo::useSystemDefault);

  nsMIMEInfoBase* retval;
  NS_ADDREF((retval = mimeInfo));
  return retval;
}
