/* -*- Mode: C++; tab-width: 2; indent-tabs-mode:nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsParentalControlsServiceWin.h"
#include "nsString.h"
#include "nsIArray.h"
#include "nsIWidget.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIFile.h"
#include "nsILocalFileWin.h"
#include "nsArrayUtils.h"
#include "nsIXULAppInfo.h"

static const CLSID CLSID_WinParentalControls = {0xE77CC89B,0x7401,0x4C04,{0x8C,0xED,0x14,0x9D,0xB3,0x5A,0xDD,0x04}};
static const IID IID_IWinParentalControls  = {0x28B4D88B,0xE072,0x49E6,{0x80,0x4D,0x26,0xED,0xBE,0x21,0xA7,0xB9}};

NS_IMPL_ISUPPORTS1(nsParentalControlsServiceWin, nsIParentalControlsService)

static HINSTANCE gAdvAPIDLLInst = NULL;

typedef ULONG (STDMETHODCALLTYPE *MyEventWrite)(
  REGHANDLE RegHandle,
  PCEVENT_DESCRIPTOR EventDescriptor,
  ULONG UserDataCount,
  PEVENT_DATA_DESCRIPTOR UserData);

typedef ULONG (STDMETHODCALLTYPE *MyEventRegister)(
  LPCGUID ProviderId,
  PENABLECALLBACK EnableCallback,
  PVOID CallbackContext,
  PREGHANDLE RegHandle);

typedef ULONG (STDMETHODCALLTYPE *MyEventUnregister)(
  REGHANDLE RegHandle);

MyEventWrite gEventWrite = NULL;
MyEventRegister gEventRegister = NULL;
MyEventUnregister gEventUnregister = NULL;

nsParentalControlsServiceWin::nsParentalControlsServiceWin() :
  mPC(nsnull)
, mEnabled(false)
, mProvider(nsnull)
{
  HRESULT hr;
  CoInitialize(NULL);
  hr = CoCreateInstance(CLSID_WinParentalControls, NULL, CLSCTX_INPROC,
                        IID_IWinParentalControls, (void**)&mPC);
  if (FAILED(hr))
    return;

  nsRefPtr<IWPCSettings> wpcs;
  if (FAILED(mPC->GetUserSettings(NULL, getter_AddRefs(wpcs)))) {
    // Not available on this os or not enabled for this user account or we're running as admin
    mPC->Release();
    mPC = nsnull;
    return;
  }

  DWORD settings = 0;
  wpcs->GetRestrictions(&settings);
  
  if (settings) { // WPCFLAG_NO_RESTRICTION = 0
    gAdvAPIDLLInst = ::LoadLibrary("Advapi32.dll");
    if(gAdvAPIDLLInst)
    {
      gEventWrite = (MyEventWrite) GetProcAddress(gAdvAPIDLLInst, "EventWrite");
      gEventRegister = (MyEventRegister) GetProcAddress(gAdvAPIDLLInst, "EventRegister");
      gEventUnregister = (MyEventUnregister) GetProcAddress(gAdvAPIDLLInst, "EventUnregister");
    }
    mEnabled = true;
  }
}

nsParentalControlsServiceWin::~nsParentalControlsServiceWin()
{
  if (mPC)
    mPC->Release();

  if (gEventUnregister && mProvider)
    gEventUnregister(mProvider);

  if (gAdvAPIDLLInst)
    ::FreeLibrary(gAdvAPIDLLInst);
}

//------------------------------------------------------------------------

NS_IMETHODIMP
nsParentalControlsServiceWin::GetParentalControlsEnabled(bool *aResult)
{
  *aResult = false;

  if (mEnabled)
    *aResult = true;

  return NS_OK;
}

NS_IMETHODIMP
nsParentalControlsServiceWin::GetBlockFileDownloadsEnabled(bool *aResult)
{
  *aResult = false;

  if (!mEnabled)
    return NS_ERROR_NOT_AVAILABLE;

  nsRefPtr<IWPCWebSettings> wpcws;
  if (SUCCEEDED(mPC->GetWebSettings(NULL, getter_AddRefs(wpcws)))) {
    DWORD settings = 0;
    wpcws->GetSettings(&settings);
    if (settings == WPCFLAG_WEB_SETTING_DOWNLOADSBLOCKED)
      *aResult = true;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsParentalControlsServiceWin::GetLoggingEnabled(bool *aResult)
{
  *aResult = false;

  if (!mEnabled)
    return NS_ERROR_NOT_AVAILABLE;

  // Check the general purpose logging flag
  nsRefPtr<IWPCSettings> wpcs;
  if (SUCCEEDED(mPC->GetUserSettings(NULL, getter_AddRefs(wpcs)))) {
    BOOL enabled = FALSE;
    wpcs->IsLoggingRequired(&enabled);
    if (enabled)
      *aResult = true;
  }

  return NS_OK;
}

// Post a log event to the system
NS_IMETHODIMP
nsParentalControlsServiceWin::Log(PRInt16 aEntryType, bool blocked, nsIURI *aSource, nsIFile *aTarget)
{
  if (!mEnabled)
    return NS_ERROR_NOT_AVAILABLE;

  NS_ENSURE_ARG_POINTER(aSource);

  // Confirm we should be logging
  bool enabled;
  GetLoggingEnabled(&enabled);
  if (!enabled)
    return NS_ERROR_NOT_AVAILABLE;

  // Register a Vista log event provider associated with the parental controls channel.
  if (!mProvider) {
    if (!gEventRegister)
      return NS_ERROR_NOT_AVAILABLE;
    if (gEventRegister(&WPCPROV, NULL, NULL, &mProvider) != ERROR_SUCCESS)
      return NS_ERROR_OUT_OF_MEMORY;
  }

  switch(aEntryType) {
    case ePCLog_URIVisit:
      // Not needed, Vista's web content filter handles this for us
      break;
    case ePCLog_FileDownload:
      LogFileDownload(blocked, aSource, aTarget);
      break;
    default:
      break;
  }

  return NS_OK;
}

// Override a single URI
NS_IMETHODIMP
nsParentalControlsServiceWin::RequestURIOverride(nsIURI *aTarget, nsIInterfaceRequestor *aWindowContext, bool *_retval)
{
  *_retval = false;

  if (!mEnabled)
    return NS_ERROR_NOT_AVAILABLE;

  NS_ENSURE_ARG_POINTER(aTarget);

  nsCAutoString spec;
  aTarget->GetSpec(spec);
  if (spec.IsEmpty())
    return NS_ERROR_INVALID_ARG;

  HWND hWnd = nsnull;
  // If we have a native window, use its handle instead
  nsCOMPtr<nsIWidget> widget(do_GetInterface(aWindowContext));
  if (widget)
    hWnd = (HWND)widget->GetNativeData(NS_NATIVE_WINDOW);
  if (hWnd == nsnull)
    hWnd = GetDesktopWindow();

  BOOL ret;
  nsRefPtr<IWPCWebSettings> wpcws;
  if (SUCCEEDED(mPC->GetWebSettings(NULL, getter_AddRefs(wpcws)))) {
    wpcws->RequestURLOverride(hWnd, NS_ConvertUTF8toUTF16(spec).get(),
                              0, NULL, &ret);
    *_retval = ret;
  }


  return NS_OK;
}

// Override a web page
NS_IMETHODIMP
nsParentalControlsServiceWin::RequestURIOverrides(nsIArray *aTargets, nsIInterfaceRequestor *aWindowContext, bool *_retval)
{
  *_retval = false;

  if (!mEnabled)
    return NS_ERROR_NOT_AVAILABLE;

  NS_ENSURE_ARG_POINTER(aTargets);

  PRUint32 arrayLength = 0;
  aTargets->GetLength(&arrayLength);
  if (!arrayLength)
    return NS_ERROR_INVALID_ARG;

  if (arrayLength == 1) {
    nsCOMPtr<nsIURI> uri = do_QueryElementAt(aTargets, 0);
    if (!uri)
      return NS_ERROR_INVALID_ARG;
    return RequestURIOverride(uri, aWindowContext, _retval);
  }

  HWND hWnd = nsnull;
  // If we have a native window, use its handle instead
  nsCOMPtr<nsIWidget> widget(do_GetInterface(aWindowContext));
  if (widget)
    hWnd = (HWND)widget->GetNativeData(NS_NATIVE_WINDOW);
  if (hWnd == nsnull)
    hWnd = GetDesktopWindow();

  // The first entry should be the root uri
  nsCAutoString rootSpec;
  nsCOMPtr<nsIURI> rootURI = do_QueryElementAt(aTargets, 0);
  if (!rootURI)
    return NS_ERROR_INVALID_ARG;
  
  rootURI->GetSpec(rootSpec);
  if (rootSpec.IsEmpty())
    return NS_ERROR_INVALID_ARG;

  // Allocate an array of sub uri
  PRInt32 count = arrayLength - 1;
  nsAutoArrayPtr<LPCWSTR> arrUrls(new LPCWSTR[count]);
  if (!arrUrls)
    return NS_ERROR_OUT_OF_MEMORY;

  PRUint32 uriIdx = 0, idx;
  for (idx = 1; idx < arrayLength; idx++)
  {
    nsCOMPtr<nsIURI> uri = do_QueryElementAt(aTargets, idx);
    if (!uri)
      continue;

    nsCAutoString subURI;
    if (NS_FAILED(uri->GetSpec(subURI)))
      continue;

    arrUrls[uriIdx] = (LPCWSTR)UTF8ToNewUnicode(subURI); // allocation
    if (!arrUrls[uriIdx])
      continue;

    uriIdx++;
  }

  if (!uriIdx)
    return NS_ERROR_INVALID_ARG;

  BOOL ret; 
  nsRefPtr<IWPCWebSettings> wpcws;
  if (SUCCEEDED(mPC->GetWebSettings(NULL, getter_AddRefs(wpcws)))) {
    wpcws->RequestURLOverride(hWnd, NS_ConvertUTF8toUTF16(rootSpec).get(),
                             uriIdx, (LPCWSTR*)arrUrls.get(), &ret);
   *_retval = ret;
  }

  // Free up the allocated strings in our array
  for (idx = 0; idx < uriIdx; idx++)
    NS_Free((void*)arrUrls[idx]);

  return NS_OK;
}

//------------------------------------------------------------------------

// Sends a file download event to the Vista Event Log 
void
nsParentalControlsServiceWin::LogFileDownload(bool blocked, nsIURI *aSource, nsIFile *aTarget)
{
  nsCAutoString curi;

  if (!gEventWrite)
    return;

  // Note, EventDataDescCreate is a macro defined in the headers, not a function

  aSource->GetSpec(curi);
  nsAutoString uri = NS_ConvertUTF8toUTF16(curi);

  // Get the name of the currently running process
  nsCOMPtr<nsIXULAppInfo> appInfo = do_GetService("@mozilla.org/xre/app-info;1");
  nsCAutoString asciiAppName;
  if (appInfo)
    appInfo->GetName(asciiAppName);
  nsAutoString appName = NS_ConvertUTF8toUTF16(asciiAppName);

  static const WCHAR fill[] = L"";
  
  // See wpcevent.h and msdn for event formats
  EVENT_DATA_DESCRIPTOR eventData[WPC_ARGS_FILEDOWNLOADEVENT_CARGS];
  DWORD dwBlocked = blocked;

  EventDataDescCreate(&eventData[WPC_ARGS_FILEDOWNLOADEVENT_URL], (const void*)uri.get(),
                      ((ULONG)uri.Length()+1)*sizeof(WCHAR));
  EventDataDescCreate(&eventData[WPC_ARGS_FILEDOWNLOADEVENT_APPNAME], (const void*)appName.get(),
                      ((ULONG)appName.Length()+1)*sizeof(WCHAR));
  EventDataDescCreate(&eventData[WPC_ARGS_FILEDOWNLOADEVENT_VERSION], (const void*)fill, sizeof(fill));
  EventDataDescCreate(&eventData[WPC_ARGS_FILEDOWNLOADEVENT_BLOCKED], (const void*)&dwBlocked,
                      sizeof(dwBlocked));

  nsCOMPtr<nsILocalFileWin> local(do_QueryInterface(aTarget)); // May be null
  if (local) {
    nsAutoString path;
    local->GetCanonicalPath(path);
    EventDataDescCreate(&eventData[WPC_ARGS_FILEDOWNLOADEVENT_PATH], (const void*)path.get(),
                        ((ULONG)path.Length()+1)*sizeof(WCHAR));
  }
  else {
    EventDataDescCreate(&eventData[WPC_ARGS_FILEDOWNLOADEVENT_PATH], (const void*)fill, sizeof(fill));
  }

  gEventWrite(mProvider, &WPCEVENT_WEB_FILEDOWNLOAD, ARRAYSIZE(eventData), eventData);
}

