/* -*- Mode: C++; tab-width: 50; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * mozilla.org
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Seth Spitzer <sspitzer@mozilla.org> (original author)
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

#include "nsSystemInfo.h"
#include "prsystem.h"
#include "nsString.h"
#include "prprf.h"

#ifdef MOZ_WIDGET_GTK2
#include <gtk/gtk.h>
#endif

nsSystemInfo::nsSystemInfo()
{
}

nsSystemInfo::~nsSystemInfo()
{
}

nsresult
nsSystemInfo::Init()
{
    nsresult rv = nsHashPropertyBag::Init();
    NS_ENSURE_SUCCESS(rv, rv);

    static const struct {
      PRSysInfo cmd;
      const char *name;
    } items[] = {
      { PR_SI_SYSNAME, "name" },
      { PR_SI_HOSTNAME, "host" },
      { PR_SI_ARCHITECTURE, "arch" },
      { PR_SI_RELEASE, "version" }
    };

    for (PRUint32 i = 0; i < (sizeof(items) / sizeof(items[0])); i++) {
      char buf[SYS_INFO_BUFFER_LENGTH];
      if (PR_GetSystemInfo(items[i].cmd, buf, sizeof(buf)) == PR_SUCCESS) {
        rv = SetPropertyAsACString(NS_ConvertASCIItoUTF16(items[i].name),
                                   nsDependentCString(buf));
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else {
        NS_WARNING("PR_GetSystemInfo failed");
      }
    }

    // Additional informations not available through PR_GetSystemInfo.
    SetInt32Property(NS_LITERAL_STRING("pagesize"), PR_GetPageSize());
    SetInt32Property(NS_LITERAL_STRING("pageshift"), PR_GetPageShift());
    SetInt32Property(NS_LITERAL_STRING("memmapalign"), PR_GetMemMapAlignment());
    SetInt32Property(NS_LITERAL_STRING("cpucount"), PR_GetNumberOfProcessors());
    SetUint64Property(NS_LITERAL_STRING("memsize"), PR_GetPhysicalMemorySize());

#ifdef MOZ_WIDGET_GTK2
    // This must be done here because NSPR can only separate OS's when compiled, not libraries.
    char* gtkver = PR_smprintf("GTK %u.%u.%u", gtk_major_version, gtk_minor_version, gtk_micro_version);
    if (gtkver) {
      rv = SetPropertyAsACString(NS_ConvertASCIItoUTF16("secondaryLibrary"),
                                 nsDependentCString(gtkver));
      PR_smprintf_free(gtkver);
      NS_ENSURE_SUCCESS(rv, rv);
    }
#endif


#ifdef MOZ_PLATFORM_MAEMO
    char *  line = nsnull;
    size_t  len = 0;
    ssize_t read;
    FILE *fp = fopen ("/proc/component_version", "r");
    if (fp) {
      while ((read = getline(&line, &len, fp)) != -1) {
        if (line) {
          if (strstr(line, "RX-51")) {
            SetPropertyAsACString(NS_ConvertASCIItoUTF16("device"), NS_LITERAL_CSTRING("Nokia N900"));
            break;
          } else if (strstr(line, "RX-44") ||
                     strstr(line, "RX-48") ||
                     strstr(line, "RX-32") ) {
            SetPropertyAsACString(NS_ConvertASCIItoUTF16("device"), NS_LITERAL_CSTRING("Nokia N8xx"));
            break;
          }
        }
      }
      if (line)
        free(line);
      fclose(fp);
    }
#endif   
    return NS_OK;
}

void
nsSystemInfo::SetInt32Property(const nsAString &aPropertyName,
                               const PRInt32 aValue)
{
  NS_WARN_IF_FALSE(aValue > 0, "Unable to read system value");
  if (aValue > 0) {
#ifdef DEBUG
    nsresult rv =
#endif
      SetPropertyAsInt32(aPropertyName, aValue);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Unable to set property");
  }
}

void
nsSystemInfo::SetUint64Property(const nsAString &aPropertyName,
                                const PRUint64 aValue)
{
  NS_WARN_IF_FALSE(aValue > 0, "Unable to read system value");
  if (aValue > 0) {
#ifdef DEBUG
    nsresult rv =
#endif
      SetPropertyAsUint64(aPropertyName, aValue);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Unable to set property");
  }
}
