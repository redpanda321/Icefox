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
 * The Original Code is the Mozilla GNOME integration code.
 *
 * The Initial Developer of the Original Code is
 * Red Hat, Inc.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Jan Horak <jhorak@redhat.com>
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

#include "nsGIOService.h"
#include "nsStringAPI.h"
#include "nsIURI.h"
#include "nsTArray.h"
#include "nsIStringEnumerator.h"
#include "nsAutoPtr.h"
#include <dlfcn.h>

#include <gio/gio.h>
#include <gtk/gtk.h>


typedef const char* (*get_commandline_t)(GAppInfo*);

char *
get_content_type_from_mime_type(const char *mimeType)
{
  GList* contentTypes = g_content_types_get_registered();
  GList* ct_ptr = contentTypes;
  char* foundContentType = NULL;

  while (ct_ptr) {
    char *mimeTypeFromContentType =  g_content_type_get_mime_type((char*)ct_ptr->data);
    if (strcmp(mimeTypeFromContentType, mimeType) == 0) {
      foundContentType = strdup((char*)ct_ptr->data);
      g_free(mimeTypeFromContentType);
      break;
    }
    g_free(mimeTypeFromContentType);
    ct_ptr = ct_ptr->next;
  }
  g_list_foreach(contentTypes, (GFunc) g_free, NULL);
  g_list_free(contentTypes);
  return foundContentType;
}

class nsGIOMimeApp : public nsIGIOMimeApp
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIGIOMIMEAPP

  nsGIOMimeApp(GAppInfo* aApp) : mApp(aApp) {}
  ~nsGIOMimeApp() { g_object_unref(mApp); }

private:
  GAppInfo *mApp;
};

NS_IMPL_ISUPPORTS1(nsGIOMimeApp, nsIGIOMimeApp)

NS_IMETHODIMP
nsGIOMimeApp::GetId(nsACString& aId)
{
  aId.Assign(g_app_info_get_id(mApp));
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::GetName(nsACString& aName)
{
  aName.Assign(g_app_info_get_name(mApp));
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::GetCommand(nsACString& aCommand)
{
  get_commandline_t g_app_info_get_commandline_ptr;

  void *libHandle = dlopen("libgio-2.0.so", RTLD_LAZY);
  if (!libHandle) {
    return NS_ERROR_FAILURE;
  }
  dlerror(); /* clear any existing error */
  g_app_info_get_commandline_ptr =
    (get_commandline_t) dlsym(libHandle, "g_app_info_get_commandline");
  if (dlerror() != NULL) {
    const char cmd = *g_app_info_get_commandline_ptr(mApp);
    if (!cmd) {
      dlclose(libHandle);
      return NS_ERROR_FAILURE;
    }
    aCommand.Assign(cmd);
  }
  dlclose(libHandle);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::GetExpectsURIs(PRInt32* aExpects)
{
  *aExpects = g_app_info_supports_uris(mApp);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::Launch(const nsACString& aUri)
{
  char *uri = strdup(PromiseFlatCString(aUri).get());

  if (!uri)
    return NS_ERROR_OUT_OF_MEMORY;

  GList *uris = g_list_append(NULL, uri);

  if (!uris) {
    g_free(uri);
    return NS_ERROR_OUT_OF_MEMORY;
  }
  GError *error = NULL;
  gboolean result = g_app_info_launch_uris(mApp, uris, NULL, &error);

  g_free(uri);
  g_list_free(uris);

  if (!result) {
    g_warning("Cannot launch application: %s", error->message);
    g_error_free(error);
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

class GIOUTF8StringEnumerator : public nsIUTF8StringEnumerator
{
public:
  GIOUTF8StringEnumerator() : mIndex(0) { }
  ~GIOUTF8StringEnumerator() { }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIUTF8STRINGENUMERATOR

  nsTArray<nsCString> mStrings;
  PRUint32            mIndex;
};

NS_IMPL_ISUPPORTS1(GIOUTF8StringEnumerator, nsIUTF8StringEnumerator)

NS_IMETHODIMP
GIOUTF8StringEnumerator::HasMore(PRBool* aResult)
{
  *aResult = mIndex < mStrings.Length();
  return NS_OK;
}

NS_IMETHODIMP
GIOUTF8StringEnumerator::GetNext(nsACString& aResult)
{
  if (mIndex >= mStrings.Length())
    return NS_ERROR_UNEXPECTED;

  aResult.Assign(mStrings[mIndex]);
  ++mIndex;
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::GetSupportedURISchemes(nsIUTF8StringEnumerator** aSchemes)
{
  *aSchemes = nsnull;

  nsRefPtr<GIOUTF8StringEnumerator> array = new GIOUTF8StringEnumerator();
  NS_ENSURE_TRUE(array, NS_ERROR_OUT_OF_MEMORY);

  GVfs *gvfs = g_vfs_get_default();

  if (!gvfs) {
    g_warning("Cannot get GVfs object.");
    return NS_ERROR_OUT_OF_MEMORY;
  }

  const gchar* const * uri_schemes = g_vfs_get_supported_uri_schemes(gvfs);

  while (*uri_schemes != NULL) {
    if (!array->mStrings.AppendElement(*uri_schemes)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    uri_schemes++;
  }

  NS_ADDREF(*aSchemes = array);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOMimeApp::SetAsDefaultForMimeType(nsACString const& aMimeType)
{
  char *content_type =
    get_content_type_from_mime_type(PromiseFlatCString(aMimeType).get());
  if (!content_type)
    return NS_ERROR_FAILURE;
  GError *error = NULL;
  g_app_info_set_as_default_for_type(mApp,
                                     content_type,
                                     &error);
  if (error) {
    g_warning("Cannot set application as default for MIME type (%s): %s",
              PromiseFlatCString(aMimeType).get(),
              error->message);
    g_error_free(error);
    g_free(content_type);
    return NS_ERROR_FAILURE;
  }

  g_free(content_type);
  return NS_OK;
}
/**
 * Set default application for files with given extensions
 * @param fileExts string of space separated extensions
 * @return NS_OK when application was set as default for given extensions,
 * NS_ERROR_FAILURE otherwise
 */
NS_IMETHODIMP
nsGIOMimeApp::SetAsDefaultForFileExtensions(nsACString const& fileExts)
{
  GError *error = NULL;
  char *extensions = strdup(PromiseFlatCString(fileExts).get());
  char *ext_pos = extensions;
  char *space_pos;

  while ( (space_pos = strchr(ext_pos, ' ')) || (*ext_pos != '\0') ) {
    if (space_pos) {
      *space_pos = '\0';
    }
    g_app_info_set_as_default_for_extension(mApp, ext_pos, &error);
    if (error) {
      g_warning("Cannot set application as default for extension (%s): %s",
                ext_pos,
                error->message);
      g_error_free(error);
      g_free(extensions);
      return NS_ERROR_FAILURE;
    }
    if (space_pos) {
      ext_pos = space_pos + 1;
    } else {
      *ext_pos = '\0';
    }
  }
  g_free(extensions);
  return NS_OK;
}

nsresult
nsGIOService::Init()
{
  // do nothing, gvfs/gio does not init.
  return NS_OK;
}

NS_IMPL_ISUPPORTS1(nsGIOService, nsIGIOService)

NS_IMETHODIMP
nsGIOService::GetMimeTypeFromExtension(const nsACString& aExtension,
                                             nsACString& aMimeType)
{
  nsCAutoString fileExtToUse("file.");
  fileExtToUse.Append(aExtension);

  gboolean result_uncertain;
  char *content_type = g_content_type_guess(fileExtToUse.get(),
                                            NULL,
                                            0,
                                            &result_uncertain);
  if (!content_type)
    return NS_ERROR_FAILURE;

  char *mime_type = g_content_type_get_mime_type(content_type);
  if (!mime_type) {
    g_free(content_type);
    return NS_ERROR_FAILURE;
  }

  aMimeType.Assign(mime_type);

  g_free(mime_type);
  g_free(content_type);

  return NS_OK;
}
// used in nsGNOMERegistry
// -----------------------------------------------------------------------------
NS_IMETHODIMP
nsGIOService::GetAppForMimeType(const nsACString& aMimeType,
                                nsIGIOMimeApp**   aApp)
{
  *aApp = nsnull;
  char *content_type =
    get_content_type_from_mime_type(PromiseFlatCString(aMimeType).get());
  if (!content_type)
    return NS_ERROR_FAILURE;

  GAppInfo *app_info = g_app_info_get_default_for_type(content_type, false);
  if (app_info) {
    nsGIOMimeApp *mozApp = new nsGIOMimeApp(app_info);
    NS_ENSURE_TRUE(mozApp, NS_ERROR_OUT_OF_MEMORY);
    NS_ADDREF(*aApp = mozApp);
  } else {
    g_free(content_type);
    return NS_ERROR_FAILURE;
  }
  g_free(content_type);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOService::GetDescriptionForMimeType(const nsACString& aMimeType,
                                              nsACString& aDescription)
{
  char *content_type =
    get_content_type_from_mime_type(PromiseFlatCString(aMimeType).get());
  if (!content_type)
    return NS_ERROR_FAILURE;

  char *desc = g_content_type_get_description(content_type);
  if (!desc) {
    g_free(content_type);
    return NS_ERROR_FAILURE;
  }

  aDescription.Assign(desc);
  g_free(content_type);
  g_free(desc);
  return NS_OK;
}

NS_IMETHODIMP
nsGIOService::ShowURI(nsIURI* aURI)
{
  nsCAutoString spec;
  aURI->GetSpec(spec);
  GError *error = NULL;
  if (!g_app_info_launch_default_for_uri(spec.get(), NULL, &error)) {
    g_warning("Could not launch default application for URI: %s" ,error->message);
    g_error_free(error);
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsGIOService::ShowURIForInput(const nsACString& aUri)
{
  GFile *file = g_file_new_for_commandline_arg(PromiseFlatCString(aUri).get());
  char* spec = g_file_get_uri(file);
  nsresult rv = NS_ERROR_FAILURE;
  GError *error = NULL;

  g_app_info_launch_default_for_uri(spec, NULL, &error);
  if (error) {
    g_warning("Cannot launch default application: %s", error->message);
    g_error_free(error);
  } else {
    rv = NS_OK;
  }
  g_object_unref(file);
  g_free(spec);

  return rv;
}

/**
 * Create or find already existing application info for specified command
 * and application name.
 * @param cmd command to execute
 * @param appName application name
 * @param appInfo location where created GAppInfo is stored
 * @return NS_OK when object is created, NS_ERROR_FAILURE otherwise.
 */
NS_IMETHODIMP
nsGIOService::CreateAppFromCommand(nsACString const& cmd,
                                   nsACString const& appName,
                                   nsIGIOMimeApp**   appInfo)
{
  GError *error = NULL;
  *appInfo = nsnull;

  GAppInfo *app_info = NULL, *app_info_from_list = NULL;
  GList *apps = g_app_info_get_all();
  GList *apps_p = apps;
  get_commandline_t g_app_info_get_commandline_ptr;

  void *libHandle = dlopen("libgio-2.0.so", RTLD_LAZY);
  if (!libHandle) {
    return NS_ERROR_FAILURE;
  }
  dlerror(); /* clear any existing error */
  g_app_info_get_commandline_ptr =
    (get_commandline_t) dlsym(libHandle, "g_app_info_get_commandline");
  if (dlerror() != NULL) {
    g_app_info_get_commandline_ptr = NULL;
  }

  // Try to find relevant and existing GAppInfo in all installed application
  while (apps_p) {
    app_info_from_list = (GAppInfo*) apps_p->data;
    /* This is  a silly test. It just compares app names but not
     * commands. This is due to old version of Glib/Gio. The required
     * function which allows to do a regular check of existence of desktop file
     * is possible by using function g_app_info_get_commandline. This function
     * has been introduced in Glib 2.20. */
    if (app_info_from_list && strcmp(g_app_info_get_name(app_info_from_list),
                                     PromiseFlatCString(appName).get()) == 0 )
    {
      if (g_app_info_get_commandline_ptr)
      {
        /* Following test is only possible with Glib >= 2.20.
         * Compare path only by using strncmp */
        if (strncmp(g_app_info_get_commandline_ptr(app_info_from_list),
                    PromiseFlatCString(cmd).get(),
                    strlen(PromiseFlatCString(cmd).get())) == 0)
        {
          app_info = app_info_from_list;
          break;
        } else {
          g_object_unref(app_info_from_list);
        }
      } else {
        app_info = app_info_from_list;
        break;
      }
    } else {
      g_object_unref(app_info_from_list);
    }
    apps_p = apps_p->next;
  }
  g_list_free(apps);

  if (!app_info) {
    app_info = g_app_info_create_from_commandline(PromiseFlatCString(cmd).get(),
                                                  PromiseFlatCString(appName).get(),
                                                  G_APP_INFO_CREATE_SUPPORTS_URIS,
                                                  &error);
  }

  if (!app_info) {
    g_warning("Cannot create application info from command: %s", error->message);
    g_error_free(error);
    dlclose(libHandle);
    return NS_ERROR_FAILURE;
  }
  nsGIOMimeApp *mozApp = new nsGIOMimeApp(app_info);
  NS_ENSURE_TRUE(mozApp, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(*appInfo = mozApp);
  dlclose(libHandle);
  return NS_OK;
}
