/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"

#include <gtk/gtk.h>

#include "nsIFileURL.h"
#include "nsIURI.h"
#include "nsIWidget.h"
#include "nsIFile.h"
#include "nsIStringBundle.h"

#include "nsArrayEnumerator.h"
#include "nsMemory.h"
#include "nsEnumeratorUtils.h"
#include "nsNetUtil.h"
#include "nsReadableUtils.h"
#include "mozcontainer.h"

#include "nsFilePicker.h"

#if (MOZ_PLATFORM_MAEMO == 5)
#include <hildon-fm-2/hildon/hildon-file-chooser-dialog.h>
#endif

using namespace mozilla;

#define MAX_PREVIEW_SIZE 180

nsIFile *nsFilePicker::mPrevDisplayDirectory = nullptr;

// Some GObject functions expect functions for gpointer arguments.
// gpointer is void* but C++ doesn't like casting functions to void*.
template<class T> static inline gpointer
FuncToGpointer(T aFunction)
{
    return reinterpret_cast<gpointer>
        (reinterpret_cast<uintptr_t>
         // This cast just provides a warning if T is not a function.
         (reinterpret_cast<void (*)()>(aFunction)));
}

void
nsFilePicker::Shutdown()
{
  NS_IF_RELEASE(mPrevDisplayDirectory);
}

static GtkFileChooserAction
GetGtkFileChooserAction(int16_t aMode)
{
  GtkFileChooserAction action;

  switch (aMode) {
    case nsIFilePicker::modeSave:
    action = GTK_FILE_CHOOSER_ACTION_SAVE;
    break;

    case nsIFilePicker::modeGetFolder:
    action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
    break;

    case nsIFilePicker::modeOpen:
    case nsIFilePicker::modeOpenMultiple:
    action = GTK_FILE_CHOOSER_ACTION_OPEN;
    break;

    default:
    NS_WARNING("Unknown nsIFilePicker mode");
    action = GTK_FILE_CHOOSER_ACTION_OPEN;
    break;
  }

  return action;
}


static void
UpdateFilePreviewWidget(GtkFileChooser *file_chooser,
                        gpointer preview_widget_voidptr)
{
  GtkImage *preview_widget = GTK_IMAGE(preview_widget_voidptr);
  char *image_filename = gtk_file_chooser_get_preview_filename(file_chooser);

  if (!image_filename) {
    gtk_file_chooser_set_preview_widget_active(file_chooser, FALSE);
    return;
  }

  gint preview_width = 0;
  gint preview_height = 0;
  GdkPixbufFormat *preview_format = gdk_pixbuf_get_file_info(image_filename,
                                                             &preview_width,
                                                             &preview_height);
  if (!preview_format) {
    g_free(image_filename);
    gtk_file_chooser_set_preview_widget_active(file_chooser, FALSE);
    return;
  }

  GdkPixbuf *preview_pixbuf;
  // Only scale down images that are too big
  if (preview_width > MAX_PREVIEW_SIZE || preview_height > MAX_PREVIEW_SIZE) {
    preview_pixbuf = gdk_pixbuf_new_from_file_at_size(image_filename,
                                                      MAX_PREVIEW_SIZE,
                                                      MAX_PREVIEW_SIZE, NULL);
  }
  else {
    preview_pixbuf = gdk_pixbuf_new_from_file(image_filename, NULL);
  }

  g_free(image_filename);

  if (!preview_pixbuf) {
    gtk_file_chooser_set_preview_widget_active(file_chooser, FALSE);
    return;
  }

  // This is the easiest way to do center alignment without worrying about containers
  // Minimum 3px padding each side (hence the 6) just to make things nice
  gint x_padding = (MAX_PREVIEW_SIZE + 6 - gdk_pixbuf_get_width(preview_pixbuf)) / 2;
  gtk_misc_set_padding(GTK_MISC(preview_widget), x_padding, 0);

  gtk_image_set_from_pixbuf(preview_widget, preview_pixbuf);
  g_object_unref(preview_pixbuf);
  gtk_file_chooser_set_preview_widget_active(file_chooser, TRUE);
}

static nsAutoCString
MakeCaseInsensitiveShellGlob(const char* aPattern) {
  // aPattern is UTF8
  nsAutoCString result;
  unsigned int len = strlen(aPattern);

  for (unsigned int i = 0; i < len; i++) {
    if (!g_ascii_isalpha(aPattern[i])) {
      // non-ASCII characters will also trigger this path, so unicode
      // is safely handled albeit case-sensitively
      result.Append(aPattern[i]);
      continue;
    }

    // add the lowercase and uppercase version of a character to a bracket
    // match, so it matches either the lowercase or uppercase char.
    result.Append('[');
    result.Append(g_ascii_tolower(aPattern[i]));
    result.Append(g_ascii_toupper(aPattern[i]));
    result.Append(']');

  }

  return result;
}

NS_IMPL_ISUPPORTS1(nsFilePicker, nsIFilePicker)

nsFilePicker::nsFilePicker()
  : mMode(nsIFilePicker::modeOpen),
    mSelectedType(0),
    mRunning(false),
    mAllowURLs(false)
{
}

nsFilePicker::~nsFilePicker()
{
}

void
ReadMultipleFiles(gpointer filename, gpointer array)
{
  nsCOMPtr<nsIFile> localfile;
  nsresult rv = NS_NewNativeLocalFile(nsDependentCString(static_cast<char*>(filename)),
                                      false,
                                      getter_AddRefs(localfile));
  if (NS_SUCCEEDED(rv)) {
    nsCOMArray<nsIFile>& files = *static_cast<nsCOMArray<nsIFile>*>(array);
    files.AppendObject(localfile);
  }

  g_free(filename);
}

void
nsFilePicker::ReadValuesFromFileChooser(GtkWidget *file_chooser)
{
  mFiles.Clear();

  if (mMode == nsIFilePicker::modeOpenMultiple) {
    mFileURL.Truncate();

    GSList *list = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(file_chooser));
    g_slist_foreach(list, ReadMultipleFiles, static_cast<gpointer>(&mFiles));
    g_slist_free(list);
  } else {
    gchar *filename = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(file_chooser));
    mFileURL.Assign(filename);
    g_free(filename);
  }

  GtkFileFilter *filter = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(file_chooser));
  GSList *filter_list = gtk_file_chooser_list_filters(GTK_FILE_CHOOSER(file_chooser));

  mSelectedType = static_cast<int16_t>(g_slist_index(filter_list, filter));
  g_slist_free(filter_list);

  // Remember last used directory.
  nsCOMPtr<nsIFile> file;
  GetFile(getter_AddRefs(file));
  if (file) {
    nsCOMPtr<nsIFile> dir;
    file->GetParent(getter_AddRefs(dir));
    if (dir) {
      dir.swap(mPrevDisplayDirectory);
    }
  }
}

void
nsFilePicker::InitNative(nsIWidget *aParent,
                         const nsAString& aTitle,
                         int16_t aMode)
{
  mParentWidget = aParent;
  mTitle.Assign(aTitle);
  mMode = aMode;
}

NS_IMETHODIMP
nsFilePicker::AppendFilters(int32_t aFilterMask)
{
  mAllowURLs = !!(aFilterMask & filterAllowURLs);
  return nsBaseFilePicker::AppendFilters(aFilterMask);
}

NS_IMETHODIMP
nsFilePicker::AppendFilter(const nsAString& aTitle, const nsAString& aFilter)
{
  if (aFilter.EqualsLiteral("..apps")) {
    // No platform specific thing we can do here, really....
    return NS_OK;
  }

  nsAutoCString filter, name;
  CopyUTF16toUTF8(aFilter, filter);
  CopyUTF16toUTF8(aTitle, name);

  mFilters.AppendElement(filter);
  mFilterNames.AppendElement(name);

  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::SetDefaultString(const nsAString& aString)
{
  mDefault = aString;

  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetDefaultString(nsAString& aString)
{
  // Per API...
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsFilePicker::SetDefaultExtension(const nsAString& aExtension)
{
  mDefaultExtension = aExtension;

  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetDefaultExtension(nsAString& aExtension)
{
  aExtension = mDefaultExtension;

  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetFilterIndex(int32_t *aFilterIndex)
{
  *aFilterIndex = mSelectedType;

  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::SetFilterIndex(int32_t aFilterIndex)
{
  mSelectedType = aFilterIndex;

  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetFile(nsIFile **aFile)
{
  NS_ENSURE_ARG_POINTER(aFile);

  *aFile = nullptr;
  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetFileURL(getter_AddRefs(uri));
  if (!uri)
    return rv;

  nsCOMPtr<nsIFileURL> fileURL(do_QueryInterface(uri, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> file;
  rv = fileURL->GetFile(getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  return CallQueryInterface(file, aFile);
}

NS_IMETHODIMP
nsFilePicker::GetFileURL(nsIURI **aFileURL)
{
  *aFileURL = nullptr;
  return NS_NewURI(aFileURL, mFileURL);
}

NS_IMETHODIMP
nsFilePicker::GetFiles(nsISimpleEnumerator **aFiles)
{
  NS_ENSURE_ARG_POINTER(aFiles);

  if (mMode == nsIFilePicker::modeOpenMultiple) {
    return NS_NewArrayEnumerator(aFiles, mFiles);
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsFilePicker::Show(int16_t *aReturn)
{
  NS_ENSURE_ARG_POINTER(aReturn);

  nsresult rv = Open(nullptr);
  if (NS_FAILED(rv))
    return rv;

  while (mRunning) {
    g_main_context_iteration(nullptr, TRUE);
  }

  *aReturn = mResult;
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::Open(nsIFilePickerShownCallback *aCallback)
{
  // Can't show two dialogs concurrently with the same filepicker
  if (mRunning)
    return NS_ERROR_NOT_AVAILABLE;

  nsXPIDLCString title;
  title.Adopt(ToNewUTF8String(mTitle));

  GtkWindow *parent_widget =
    GTK_WINDOW(mParentWidget->GetNativeData(NS_NATIVE_SHELLWIDGET));

  GtkFileChooserAction action = GetGtkFileChooserAction(mMode);
  const gchar *accept_button = (action == GTK_FILE_CHOOSER_ACTION_SAVE)
                               ? GTK_STOCK_SAVE : GTK_STOCK_OPEN;
#if (MOZ_PLATFORM_MAEMO == 5)
  GtkWidget *file_chooser =
    hildon_file_chooser_dialog_new_with_properties(parent_widget,
                                                   "action", action,
                                                   "open-button-text", accept_button,
                                                   NULL);
  gtk_window_set_title(GTK_WINDOW(file_chooser), title);
#else
  GtkWidget *file_chooser =
      gtk_file_chooser_dialog_new(title, parent_widget, action,
                                  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                  accept_button, GTK_RESPONSE_ACCEPT,
                                  NULL);
  gtk_dialog_set_alternative_button_order(GTK_DIALOG(file_chooser),
                                          GTK_RESPONSE_ACCEPT,
                                          GTK_RESPONSE_CANCEL,
                                          -1);
  if (mAllowURLs) {
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(file_chooser), FALSE);
  }
#endif

  if (action == GTK_FILE_CHOOSER_ACTION_OPEN || action == GTK_FILE_CHOOSER_ACTION_SAVE) {
    GtkWidget *img_preview = gtk_image_new();
    gtk_file_chooser_set_preview_widget(GTK_FILE_CHOOSER(file_chooser), img_preview);
    g_signal_connect(file_chooser, "update-preview", G_CALLBACK(UpdateFilePreviewWidget), img_preview);
  }

  GtkWindow *window = GTK_WINDOW(file_chooser);
  gtk_window_set_modal(window, TRUE);
  if (parent_widget) {
    gtk_window_set_destroy_with_parent(window, TRUE);
    GtkWindowGroup *parentGroup = gtk_window_get_group(parent_widget);
    if (parentGroup) {
      gtk_window_group_add_window(parentGroup, window);
    }
  }

  NS_ConvertUTF16toUTF8 defaultName(mDefault);
  switch (mMode) {
    case nsIFilePicker::modeOpenMultiple:
      gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(file_chooser), TRUE);
      break;
    case nsIFilePicker::modeSave:
      gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(file_chooser),
                                        defaultName.get());
      break;
  }

  nsCOMPtr<nsIFile> defaultPath;
  if (mDisplayDirectory) {
    mDisplayDirectory->Clone(getter_AddRefs(defaultPath));
  } else if (mPrevDisplayDirectory) {
    mPrevDisplayDirectory->Clone(getter_AddRefs(defaultPath));
  }

  if (defaultPath) {
    if (!defaultName.IsEmpty() && mMode != nsIFilePicker::modeSave) {
      // Try to select the intended file. Even if it doesn't exist, GTK still switches
      // directories.
      defaultPath->AppendNative(defaultName);
      nsAutoCString path;
      defaultPath->GetNativePath(path);
      gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(file_chooser), path.get());
    } else {
      nsAutoCString directory;
      defaultPath->GetNativePath(directory);
      gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(file_chooser),
                                          directory.get());
    }
  }

  gtk_dialog_set_default_response(GTK_DIALOG(file_chooser), GTK_RESPONSE_ACCEPT);

  int32_t count = mFilters.Length();
  for (int32_t i = 0; i < count; ++i) {
    // This is fun... the GTK file picker does not accept a list of filters
    // so we need to split out each string, and add it manually.

    char **patterns = g_strsplit(mFilters[i].get(), ";", -1);
    if (!patterns) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    GtkFileFilter *filter = gtk_file_filter_new();
    for (int j = 0; patterns[j] != NULL; ++j) {
      nsAutoCString caseInsensitiveFilter = MakeCaseInsensitiveShellGlob(g_strstrip(patterns[j]));
      gtk_file_filter_add_pattern(filter, caseInsensitiveFilter.get());
    }

    g_strfreev(patterns);

    if (!mFilterNames[i].IsEmpty()) {
      // If we have a name for our filter, let's use that.
      const char *filter_name = mFilterNames[i].get();
      gtk_file_filter_set_name(filter, filter_name);
    } else {
      // If we don't have a name, let's just use the filter pattern.
      const char *filter_pattern = mFilters[i].get();
      gtk_file_filter_set_name(filter, filter_pattern);
    }

    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(file_chooser), filter);

    // Set the initially selected filter
    if (mSelectedType == i) {
      gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(file_chooser), filter);
    }
  }

  gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(file_chooser), TRUE);

  mRunning = true;
  mCallback = aCallback;
  NS_ADDREF_THIS();
  g_signal_connect(file_chooser, "response", G_CALLBACK(OnResponse), this);
  g_signal_connect(file_chooser, "destroy", G_CALLBACK(OnDestroy), this);
  gtk_widget_show(file_chooser);

  return NS_OK;
}

/* static */ void
nsFilePicker::OnResponse(GtkWidget* file_chooser, gint response_id,
                         gpointer user_data)
{
  static_cast<nsFilePicker*>(user_data)->
    Done(file_chooser, response_id);
}

/* static */ void
nsFilePicker::OnDestroy(GtkWidget* file_chooser, gpointer user_data)
{
  static_cast<nsFilePicker*>(user_data)->
    Done(file_chooser, GTK_RESPONSE_CANCEL);
}

void
nsFilePicker::Done(GtkWidget* file_chooser, gint response)
{
  mRunning = false;

  int16_t result;
  switch (response) {
    case GTK_RESPONSE_OK:
    case GTK_RESPONSE_ACCEPT:
    ReadValuesFromFileChooser(file_chooser);
    result = nsIFilePicker::returnOK;
    if (mMode == nsIFilePicker::modeSave) {
      nsCOMPtr<nsIFile> file;
      GetFile(getter_AddRefs(file));
      if (file) {
        bool exists = false;
        file->Exists(&exists);
        if (exists)
          result = nsIFilePicker::returnReplace;
      }
    }
    break;

    case GTK_RESPONSE_CANCEL:
    case GTK_RESPONSE_CLOSE:
    case GTK_RESPONSE_DELETE_EVENT:
    result = nsIFilePicker::returnCancel;
    break;

    default:
    NS_WARNING("Unexpected response");
    result = nsIFilePicker::returnCancel;
    break;
  }

  // A "response" signal won't be sent again but "destroy" will be.
  g_signal_handlers_disconnect_by_func(file_chooser,
                                       FuncToGpointer(OnDestroy), this);

  // When response_id is GTK_RESPONSE_DELETE_EVENT or when called from
  // OnDestroy, the widget would be destroyed anyway but it is fine if
  // gtk_widget_destroy is called more than once.  gtk_widget_destroy has
  // requests that any remaining references be released, but the reference
  // count will not be decremented again if GtkWindow's reference has already
  // been released.
  gtk_widget_destroy(file_chooser);

  if (mCallback) {
    mCallback->Done(result);
    mCallback = nullptr;
  } else {
    mResult = result;
  }
  NS_RELEASE_THIS();
}
