/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"

#include <windows.h>
#include <setupapi.h>
#include "gfxWindowsPlatform.h"
#include "GfxInfo.h"
#include "GfxInfoWebGL.h"
#include "nsUnicharUtils.h"
#include "mozilla/FunctionTimer.h"
#include "prenv.h"
#include "prprf.h"
#include "GfxDriverInfo.h"
#include "mozilla/Preferences.h"
#include "nsPrintfCString.h"

#if defined(MOZ_CRASHREPORTER)
#include "nsExceptionHandler.h"
#include "nsICrashReporter.h"
#define NS_CRASHREPORTER_CONTRACTID "@mozilla.org/toolkit/crash-reporter;1"
#endif

using namespace mozilla;
using namespace mozilla::widget;

#ifdef DEBUG
NS_IMPL_ISUPPORTS_INHERITED1(GfxInfo, GfxInfoBase, nsIGfxInfoDebug)
#endif

static const PRUint32 allWindowsVersions = 0xffffffff;

#define V(a,b,c,d) GFX_DRIVER_VERSION(a,b,c,d)


GfxInfo::GfxInfo()
 :  mWindowsVersion(0),
    mHasDualGPU(false),
    mIsGPU2Active(false)
{
}

/* GetD2DEnabled and GetDwriteEnabled shouldn't be called until after gfxPlatform initialization
 * has occurred because they depend on it for information. (See bug 591561) */
nsresult
GfxInfo::GetD2DEnabled(bool *aEnabled)
{
  *aEnabled = gfxWindowsPlatform::GetPlatform()->GetRenderMode() == gfxWindowsPlatform::RENDER_DIRECT2D;
  return NS_OK;
}

nsresult
GfxInfo::GetDWriteEnabled(bool *aEnabled)
{
  *aEnabled = gfxWindowsPlatform::GetPlatform()->DWriteEnabled();
  return NS_OK;
}

nsresult
GfxInfo::GetAzureEnabled(bool *aEnabled)
{
  *aEnabled = false;

  bool d2dEnabled = 
    gfxWindowsPlatform::GetPlatform()->GetRenderMode() == gfxWindowsPlatform::RENDER_DIRECT2D;

  if (d2dEnabled) {
    bool azure = false;
    nsresult rv = mozilla::Preferences::GetBool("gfx.canvas.azure.enabled", &azure);

    if (NS_SUCCEEDED(rv) && azure) {
      *aEnabled = true;
    }
  }

  return NS_OK;
}

/* readonly attribute DOMString DWriteVersion; */
NS_IMETHODIMP
GfxInfo::GetDWriteVersion(nsAString & aDwriteVersion)
{
  gfxWindowsPlatform::GetDLLVersion(L"dwrite.dll", aDwriteVersion);
  return NS_OK;
}

#define PIXEL_STRUCT_RGB  1
#define PIXEL_STRUCT_BGR  2

/* readonly attribute DOMString cleartypeParameters; */
NS_IMETHODIMP
GfxInfo::GetCleartypeParameters(nsAString & aCleartypeParams)
{
  nsTArray<ClearTypeParameterInfo> clearTypeParams;

  gfxWindowsPlatform::GetPlatform()->GetCleartypeParams(clearTypeParams);
  PRUint32 d, numDisplays = clearTypeParams.Length();
  bool displayNames = (numDisplays > 1);
  bool foundData = false;
  nsString outStr;
  WCHAR valStr[256];

  for (d = 0; d < numDisplays; d++) {
    ClearTypeParameterInfo& params = clearTypeParams[d];

    if (displayNames) {
      swprintf_s(valStr, ArrayLength(valStr),
                 L"%s [ ", params.displayName.get());
      outStr.Append(valStr);
    }

    if (params.gamma >= 0) {
      foundData = true;
      swprintf_s(valStr, ArrayLength(valStr),
                 L"Gamma: %d ", params.gamma);
      outStr.Append(valStr);
    }

    if (params.pixelStructure >= 0) {
      foundData = true;
      if (params.pixelStructure == PIXEL_STRUCT_RGB ||
          params.pixelStructure == PIXEL_STRUCT_BGR)
      {
        swprintf_s(valStr, ArrayLength(valStr),
                   L"Pixel Structure: %s ",
                   (params.pixelStructure == PIXEL_STRUCT_RGB ?
                      L"RGB" : L"BGR"));
      } else {
        swprintf_s(valStr, ArrayLength(valStr),
                   L"Pixel Structure: %d ", params.pixelStructure);
      }
      outStr.Append(valStr);
    }

    if (params.clearTypeLevel >= 0) {
      foundData = true;
      swprintf_s(valStr, ArrayLength(valStr),
                 L"ClearType Level: %d ", params.clearTypeLevel);
      outStr.Append(valStr);
    }

    if (params.enhancedContrast >= 0) {
      foundData = true;
      swprintf_s(valStr, ArrayLength(valStr),
                 L"Enhanced Contrast: %d ", params.enhancedContrast);
      outStr.Append(valStr);
    }

    if (displayNames) {
      outStr.Append(L"] ");
    }
  }

  if (foundData) {
    aCleartypeParams.Assign(outStr);
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

static nsresult GetKeyValue(const WCHAR* keyLocation, const WCHAR* keyName, nsAString& destString, int type)
{
  HKEY key;
  DWORD dwcbData;
  DWORD dValue;
  DWORD resultType;
  LONG result;
  nsresult retval = NS_OK;

  result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyLocation, 0, KEY_QUERY_VALUE, &key);
  if (result != ERROR_SUCCESS) {
    return NS_ERROR_FAILURE;
  }

  switch (type) {
    case REG_DWORD: {
      // We only use this for vram size
      dwcbData = sizeof(dValue);
      result = RegQueryValueExW(key, keyName, NULL, &resultType, (LPBYTE)&dValue, &dwcbData);
      if (result == ERROR_SUCCESS && resultType == REG_DWORD) {
        dValue = dValue / 1024 / 1024;
        destString.AppendInt(PRInt32(dValue));
      } else {
        retval = NS_ERROR_FAILURE;
      }
      break;
    }
    case REG_MULTI_SZ: {
      // A chain of null-separated strings; we convert the nulls to spaces
      WCHAR wCharValue[1024];
      dwcbData = sizeof(wCharValue);

      result = RegQueryValueExW(key, keyName, NULL, &resultType, (LPBYTE)wCharValue, &dwcbData);
      if (result == ERROR_SUCCESS && resultType == REG_MULTI_SZ) {
        // This bit here could probably be cleaner.
        bool isValid = false;

        DWORD strLen = dwcbData/sizeof(wCharValue[0]);
        for (DWORD i = 0; i < strLen; i++) {
          if (wCharValue[i] == '\0') {
            if (i < strLen - 1 && wCharValue[i + 1] == '\0') {
              isValid = true;
              break;
            } else {
              wCharValue[i] = ' ';
            }
          }
        }

        // ensure wCharValue is null terminated
        wCharValue[strLen-1] = '\0';

        if (isValid)
          destString = wCharValue;

      } else {
        retval = NS_ERROR_FAILURE;
      }

      break;
    }
  }
  RegCloseKey(key);

  return retval;
}

// The driver ID is a string like PCI\VEN_15AD&DEV_0405&SUBSYS_040515AD, possibly
// followed by &REV_XXXX.  We uppercase the string, and strip the &REV_ part
// from it, if found.
static void normalizeDriverId(nsString& driverid) {
  ToUpperCase(driverid);
  PRInt32 rev = driverid.Find(NS_LITERAL_CSTRING("&REV_"));
  if (rev != -1) {
    driverid.Cut(rev, driverid.Length());
  }
}

// The device ID is a string like PCI\VEN_15AD&DEV_0405&SUBSYS_040515AD
// this function is used to extract the id's out of it
PRUint32
ParseIDFromDeviceID(const nsAString &key, const char *prefix, int length)
{
  nsAutoString id(key);
  ToUpperCase(id);
  PRInt32 start = id.Find(prefix);
  if (start != -1) {
    id.Cut(0, start + strlen(prefix));
    id.Truncate(length);
  }
  nsresult err;
  return id.ToInteger(&err, 16);
}

/* Other interesting places for info:
 *   IDXGIAdapter::GetDesc()
 *   IDirectDraw7::GetAvailableVidMem()
 *   e->GetAvailableTextureMem()
 * */

#define DEVICE_KEY_PREFIX L"\\Registry\\Machine\\"
nsresult
GfxInfo::Init()
{
  NS_TIME_FUNCTION;

  nsresult rv = GfxInfoBase::Init();

  DISPLAY_DEVICEW displayDevice;
  displayDevice.cb = sizeof(displayDevice);
  int deviceIndex = 0;

  mDeviceKeyDebug = NS_LITERAL_STRING("PrimarySearch");

  while (EnumDisplayDevicesW(NULL, deviceIndex, &displayDevice, 0)) {
    if (displayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
      mDeviceKeyDebug = NS_LITERAL_STRING("NullSearch");
      break;
    }
    deviceIndex++;
  }

  // make sure the string is NULL terminated
  if (wcsnlen(displayDevice.DeviceKey, ArrayLength(displayDevice.DeviceKey))
      == ArrayLength(displayDevice.DeviceKey)) {
    // we did not find a NULL
    return rv;
  }

  mDeviceKeyDebug = displayDevice.DeviceKey;

  /* DeviceKey is "reserved" according to MSDN so we'll be careful with it */
  /* check that DeviceKey begins with DEVICE_KEY_PREFIX */
  /* some systems have a DeviceKey starting with \REGISTRY\Machine\ so we need to compare case insenstively */
  if (_wcsnicmp(displayDevice.DeviceKey, DEVICE_KEY_PREFIX, ArrayLength(DEVICE_KEY_PREFIX)-1) != 0)
    return rv;

  // chop off DEVICE_KEY_PREFIX
  mDeviceKey = displayDevice.DeviceKey + ArrayLength(DEVICE_KEY_PREFIX)-1;

  mDeviceID = displayDevice.DeviceID;
  mDeviceString = displayDevice.DeviceString;

  /* create a device information set composed of the current display device */
  HDEVINFO devinfo = SetupDiGetClassDevsW(NULL, mDeviceID.get(), NULL,
                                          DIGCF_PRESENT | DIGCF_PROFILE | DIGCF_ALLCLASSES);

  if (devinfo != INVALID_HANDLE_VALUE) {
    HKEY key;
    LONG result;
    WCHAR value[255];
    DWORD dwcbData;
    SP_DEVINFO_DATA devinfoData;
    DWORD memberIndex = 0;

    devinfoData.cbSize = sizeof(devinfoData);
    NS_NAMED_LITERAL_STRING(driverKeyPre, "System\\CurrentControlSet\\Control\\Class\\");
    /* enumerate device information elements in the device information set */
    while (SetupDiEnumDeviceInfo(devinfo, memberIndex++, &devinfoData)) {
      /* get a string that identifies the device's driver key */
      if (SetupDiGetDeviceRegistryPropertyW(devinfo,
                                            &devinfoData,
                                            SPDRP_DRIVER,
                                            NULL,
                                            (PBYTE)value,
                                            sizeof(value),
                                            NULL)) {
        nsAutoString driverKey(driverKeyPre);
        driverKey += value;
        result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, driverKey.BeginReading(), 0, KEY_QUERY_VALUE, &key);
        if (result == ERROR_SUCCESS) {
          /* we've found the driver we're looking for */
          dwcbData = sizeof(value);
          result = RegQueryValueExW(key, L"DriverVersion", NULL, NULL, (LPBYTE)value, &dwcbData);
          if (result == ERROR_SUCCESS)
            mDriverVersion = value;
          dwcbData = sizeof(value);
          result = RegQueryValueExW(key, L"DriverDate", NULL, NULL, (LPBYTE)value, &dwcbData);
          if (result == ERROR_SUCCESS)
            mDriverDate = value;
          RegCloseKey(key); 
          break;
        }
      }
    }

    SetupDiDestroyDeviceInfoList(devinfo);
  }

  mAdapterVendorID.AppendPrintf("0x%04x", ParseIDFromDeviceID(mDeviceID, "VEN_", 4));
  mAdapterDeviceID.AppendPrintf("0x%04x", ParseIDFromDeviceID(mDeviceID, "&DEV_", 4));
  mAdapterSubsysID  = ParseIDFromDeviceID(mDeviceID,  "&SUBSYS_", 8);

  // We now check for second display adapter.

  // Device interface class for display adapters.
  CLSID GUID_DISPLAY_DEVICE_ARRIVAL;
  HRESULT hresult = CLSIDFromString(L"{1CA05180-A699-450A-9A0C-DE4FBE3DDD89}",
                               &GUID_DISPLAY_DEVICE_ARRIVAL);
  if (hresult == NOERROR) {
    devinfo = SetupDiGetClassDevsW(&GUID_DISPLAY_DEVICE_ARRIVAL, NULL, NULL,
                                   DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);

    if (devinfo != INVALID_HANDLE_VALUE) {
      HKEY key;
      LONG result;
      WCHAR value[255];
      DWORD dwcbData;
      SP_DEVINFO_DATA devinfoData;
      DWORD memberIndex = 0;
      devinfoData.cbSize = sizeof(devinfoData);

      nsAutoString adapterDriver2;
      nsAutoString deviceID2;
      nsAutoString driverVersion2;
      nsAutoString driverDate2;
      PRUint32 adapterVendorID2;
      PRUint32 adapterDeviceID2;

      NS_NAMED_LITERAL_STRING(driverKeyPre, "System\\CurrentControlSet\\Control\\Class\\");
      /* enumerate device information elements in the device information set */
      while (SetupDiEnumDeviceInfo(devinfo, memberIndex++, &devinfoData)) {
        /* get a string that identifies the device's driver key */
        if (SetupDiGetDeviceRegistryPropertyW(devinfo,
                                              &devinfoData,
                                              SPDRP_DRIVER,
                                              NULL,
                                              (PBYTE)value,
                                              sizeof(value),
                                              NULL)) {
          nsAutoString driverKey2(driverKeyPre);
          driverKey2 += value;
          result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, driverKey2.BeginReading(), 0, KEY_QUERY_VALUE, &key);
          if (result == ERROR_SUCCESS) {
            dwcbData = sizeof(value);
            result = RegQueryValueExW(key, L"MatchingDeviceId", NULL, NULL, (LPBYTE)value, &dwcbData);
            if (result != ERROR_SUCCESS) {
              continue;
            }
            deviceID2 = value;
            nsAutoString adapterVendorID2String;
            nsAutoString adapterDeviceID2String;
            adapterVendorID2 = ParseIDFromDeviceID(deviceID2, "VEN_", 4);
            adapterVendorID2String.AppendPrintf("0x%04x", adapterVendorID2);
            adapterDeviceID2 = ParseIDFromDeviceID(deviceID2, "&DEV_", 4);
            adapterDeviceID2String.AppendPrintf("0x%04x", adapterDeviceID2);
            if (mAdapterVendorID == adapterVendorID2String &&
                mAdapterDeviceID == adapterDeviceID2String) {
              RegCloseKey(key);
              continue;
            }

            // If this device is missing driver information, it is unlikely to
            // be a real display adapter.
            if (NS_FAILED(GetKeyValue(driverKey2.BeginReading(), L"InstalledDisplayDrivers",
                           adapterDriver2, REG_MULTI_SZ))) {
              RegCloseKey(key);
              continue;
            }
            dwcbData = sizeof(value);
            result = RegQueryValueExW(key, L"DriverVersion", NULL, NULL, (LPBYTE)value, &dwcbData);
            if (result != ERROR_SUCCESS) {
              RegCloseKey(key);
              continue;
            }
            driverVersion2 = value;
            dwcbData = sizeof(value);
            result = RegQueryValueExW(key, L"DriverDate", NULL, NULL, (LPBYTE)value, &dwcbData);
            if (result != ERROR_SUCCESS) {
              RegCloseKey(key);
              continue;
            }
            driverDate2 = value;
            dwcbData = sizeof(value);
            result = RegQueryValueExW(key, L"Device Description", NULL, NULL, (LPBYTE)value, &dwcbData);
            if (result != ERROR_SUCCESS) {
              dwcbData = sizeof(value);
              result = RegQueryValueExW(key, L"DriverDesc", NULL, NULL, (LPBYTE)value, &dwcbData);
            }
            RegCloseKey(key);
            if (result == ERROR_SUCCESS) {
              mHasDualGPU = true;
              mDeviceString2 = value;
              mDeviceID2 = deviceID2;
              mDeviceKey2 = driverKey2;
              mDriverVersion2 = driverVersion2;
              mDriverDate2 = driverDate2;
              mAdapterVendorID2.AppendPrintf("0x%04x", adapterVendorID2);
              mAdapterDeviceID2.AppendPrintf("0x%04x", adapterDeviceID2);
              mAdapterSubsysID2 = ParseIDFromDeviceID(mDeviceID2, "&SUBSYS_", 8);
              break;
            }
          }
        }
      }

      SetupDiDestroyDeviceInfoList(devinfo);
    }
  }

  const char *spoofedDriverVersionString = PR_GetEnv("MOZ_GFX_SPOOF_DRIVER_VERSION");
  if (spoofedDriverVersionString) {
    mDriverVersion.AssignASCII(spoofedDriverVersionString);
  }

  const char *spoofedVendor = PR_GetEnv("MOZ_GFX_SPOOF_VENDOR_ID");
  if (spoofedVendor) {
    mAdapterVendorID.AssignASCII(spoofedVendor);
  }

  mHasDriverVersionMismatch = false;
  if (mAdapterVendorID == GfxDriverInfo::GetDeviceVendor(VendorIntel)) {
    // we've had big crashers (bugs 590373 and 595364) apparently correlated
    // with bad Intel driver installations where the DriverVersion reported
    // by the registry was not the version of the DLL.
    bool is64bitApp = sizeof(void*) == 8;
    const PRUnichar *dllFileName = is64bitApp
                                 ? L"igd10umd64.dll"
                                 : L"igd10umd32.dll";
    nsString dllVersion;
    gfxWindowsPlatform::GetDLLVersion((PRUnichar*)dllFileName, dllVersion);

    PRUint64 dllNumericVersion = 0, driverNumericVersion = 0;
    ParseDriverVersion(dllVersion, &dllNumericVersion);
    ParseDriverVersion(mDriverVersion, &driverNumericVersion);

    // if GetDLLVersion fails, it gives "0.0.0.0"
    // so if GetDLLVersion failed, we get dllNumericVersion = 0
    // so this test implicitly handles the case where GetDLLVersion failed
    if (dllNumericVersion != driverNumericVersion)
      mHasDriverVersionMismatch = true;
  }

  const char *spoofedDevice = PR_GetEnv("MOZ_GFX_SPOOF_DEVICE_ID");
  if (spoofedDevice) {
    mAdapterDeviceID.AssignASCII(spoofedDevice);
  }

  const char *spoofedWindowsVersion = PR_GetEnv("MOZ_GFX_SPOOF_WINDOWS_VERSION");
  if (spoofedWindowsVersion) {
    PR_sscanf(spoofedWindowsVersion, "%x", &mWindowsVersion);
  } else {
    mWindowsVersion = gfxWindowsPlatform::WindowsOSVersion();
  }

  AddCrashReportAnnotations();

  return rv;
}

/* readonly attribute DOMString adapterDescription; */
NS_IMETHODIMP
GfxInfo::GetAdapterDescription(nsAString & aAdapterDescription)
{
  aAdapterDescription = mDeviceString;
  return NS_OK;
}

/* readonly attribute DOMString adapterDescription2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDescription2(nsAString & aAdapterDescription)
{
  aAdapterDescription = mDeviceString2;
  return NS_OK;
}

/* readonly attribute DOMString adapterRAM; */
NS_IMETHODIMP
GfxInfo::GetAdapterRAM(nsAString & aAdapterRAM)
{
  if (NS_FAILED(GetKeyValue(mDeviceKey.BeginReading(), L"HardwareInformation.MemorySize", aAdapterRAM, REG_DWORD)))
    aAdapterRAM = L"Unknown";
  return NS_OK;
}

/* readonly attribute DOMString adapterRAM2; */
NS_IMETHODIMP
GfxInfo::GetAdapterRAM2(nsAString & aAdapterRAM)
{
  if (!mHasDualGPU) {
    aAdapterRAM.AssignLiteral("");
  } else if (NS_FAILED(GetKeyValue(mDeviceKey2.BeginReading(), L"HardwareInformation.MemorySize", aAdapterRAM, REG_DWORD))) {
    aAdapterRAM = L"Unknown";
  }
  return NS_OK;
}

/* readonly attribute DOMString adapterDriver; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriver(nsAString & aAdapterDriver)
{
  if (NS_FAILED(GetKeyValue(mDeviceKey.BeginReading(), L"InstalledDisplayDrivers", aAdapterDriver, REG_MULTI_SZ)))
    aAdapterDriver = L"Unknown";
  return NS_OK;
}

/* readonly attribute DOMString adapterDriver2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriver2(nsAString & aAdapterDriver)
{
  if (!mHasDualGPU) {
    aAdapterDriver.AssignLiteral("");
  } else if (NS_FAILED(GetKeyValue(mDeviceKey2.BeginReading(), L"InstalledDisplayDrivers", aAdapterDriver, REG_MULTI_SZ))) {
    aAdapterDriver = L"Unknown";
  }
  return NS_OK;
}

/* readonly attribute DOMString adapterDriverVersion; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriverVersion(nsAString & aAdapterDriverVersion)
{
  aAdapterDriverVersion = mDriverVersion;
  return NS_OK;
}

/* readonly attribute DOMString adapterDriverDate; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriverDate(nsAString & aAdapterDriverDate)
{
  aAdapterDriverDate = mDriverDate;
  return NS_OK;
}

/* readonly attribute DOMString adapterDriverVersion2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriverVersion2(nsAString & aAdapterDriverVersion)
{
  aAdapterDriverVersion = mDriverVersion2;
  return NS_OK;
}

/* readonly attribute DOMString adapterDriverDate2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDriverDate2(nsAString & aAdapterDriverDate)
{
  aAdapterDriverDate = mDriverDate2;
  return NS_OK;
}

/* readonly attribute DOMString adapterVendorID; */
NS_IMETHODIMP
GfxInfo::GetAdapterVendorID(nsAString & aAdapterVendorID)
{
  aAdapterVendorID = mAdapterVendorID;
  return NS_OK;
}

/* readonly attribute DOMString adapterVendorID2; */
NS_IMETHODIMP
GfxInfo::GetAdapterVendorID2(nsAString & aAdapterVendorID)
{
  aAdapterVendorID = mAdapterVendorID2;
  return NS_OK;
}

/* readonly attribute DOMString adapterDeviceID; */
NS_IMETHODIMP
GfxInfo::GetAdapterDeviceID(nsAString & aAdapterDeviceID)
{
  aAdapterDeviceID = mAdapterDeviceID;
  return NS_OK;
}

/* readonly attribute DOMString adapterDeviceID2; */
NS_IMETHODIMP
GfxInfo::GetAdapterDeviceID2(nsAString & aAdapterDeviceID)
{
  aAdapterDeviceID = mAdapterDeviceID2;
  return NS_OK;
}

/* readonly attribute boolean isGPU2Active; */
NS_IMETHODIMP
GfxInfo::GetIsGPU2Active(bool* aIsGPU2Active)
{
  *aIsGPU2Active = mIsGPU2Active;
  return NS_OK;
}

#if defined(MOZ_CRASHREPORTER)
/* Cisco's VPN software can cause corruption of the floating point state.
 * Make a note of this in our crash reports so that some weird crashes
 * make more sense */
static void
CheckForCiscoVPN() {
  LONG result;
  HKEY key;
  /* This will give false positives, but hopefully no false negatives */
  result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Cisco Systems\\VPN Client", 0, KEY_QUERY_VALUE, &key);
  if (result == ERROR_SUCCESS) {
    RegCloseKey(key);
    CrashReporter::AppendAppNotesToCrashReport(NS_LITERAL_CSTRING("Cisco VPN\n"));
  }
}
#endif

void
GfxInfo::AddCrashReportAnnotations()
{
#if defined(MOZ_CRASHREPORTER)
  CheckForCiscoVPN();

  nsString deviceID, vendorID;
  nsCString narrowDeviceID, narrowVendorID;
  nsAutoString adapterDriverVersionString;

  GetAdapterDeviceID(deviceID);
  CopyUTF16toUTF8(deviceID, narrowDeviceID);
  GetAdapterVendorID(vendorID);
  CopyUTF16toUTF8(vendorID, narrowVendorID);
  GetAdapterDriverVersion(adapterDriverVersionString);

  CrashReporter::AnnotateCrashReport(NS_LITERAL_CSTRING("AdapterVendorID"),
                                     narrowVendorID);
  CrashReporter::AnnotateCrashReport(NS_LITERAL_CSTRING("AdapterDeviceID"),
                                     narrowDeviceID);
  
  /* Add an App Note for now so that we get the data immediately. These
   * can go away after we store the above in the socorro db */
  nsCAutoString note;
  /* AppendPrintf only supports 32 character strings, mrghh. */
  note.Append("AdapterVendorID: ");
  note.Append(narrowVendorID);
  note.Append(", AdapterDeviceID: ");
  note.Append(narrowDeviceID);
  note.AppendPrintf(", AdapterSubsysID: %08x, ", mAdapterSubsysID);
  note.Append("AdapterDriverVersion: ");
  note.Append(NS_LossyConvertUTF16toASCII(adapterDriverVersionString));

  if (vendorID == GfxDriverInfo::GetDeviceVendor(VendorAll)) {
    /* if we didn't find a valid vendorID lets append the mDeviceID string to try to find out why */
    note.Append(", ");
    LossyAppendUTF16toASCII(mDeviceID, note);
    note.Append(", ");
    LossyAppendUTF16toASCII(mDeviceKeyDebug, note);
    LossyAppendUTF16toASCII(mDeviceKeyDebug, note);
  }
  note.Append("\n");

  if (mHasDualGPU) {
    nsString deviceID2, vendorID2;
    nsAutoString adapterDriverVersionString2;
    nsCString narrowDeviceID2, narrowVendorID2;

    note.AppendLiteral("Has dual GPUs. GPU #2: ");
    GetAdapterDeviceID2(deviceID2);
    CopyUTF16toUTF8(deviceID2, narrowDeviceID2);
    GetAdapterVendorID2(vendorID2);
    CopyUTF16toUTF8(vendorID2, narrowVendorID2);
    GetAdapterDriverVersion2(adapterDriverVersionString2);
    note.Append("AdapterVendorID2: ");
    note.Append(narrowVendorID2);
    note.Append(", AdapterDeviceID2: ");
    note.Append(narrowDeviceID2);
    note.AppendPrintf(", AdapterSubsysID2: %08x, ", mAdapterSubsysID2);
    note.AppendPrintf("AdapterDriverVersion2: ");
    note.Append(NS_LossyConvertUTF16toASCII(adapterDriverVersionString2));
  }
  CrashReporter::AppendAppNotesToCrashReport(note);

#endif
}

static OperatingSystem
WindowsVersionToOperatingSystem(PRInt32 aWindowsVersion)
{
  switch(aWindowsVersion) {
    case gfxWindowsPlatform::kWindowsXP:
      return DRIVER_OS_WINDOWS_XP;
    case gfxWindowsPlatform::kWindowsServer2003:
      return DRIVER_OS_WINDOWS_SERVER_2003;
    case gfxWindowsPlatform::kWindowsVista:
      return DRIVER_OS_WINDOWS_VISTA;
    case gfxWindowsPlatform::kWindows7:
      return DRIVER_OS_WINDOWS_7;
    case gfxWindowsPlatform::kWindowsUnknown:
    default:
      return DRIVER_OS_UNKNOWN;
    };
}

const nsTArray<GfxDriverInfo>&
GfxInfo::GetGfxDriverInfo()
{
  if (!mDriverInfo->Length()) {
    /*
     * NVIDIA entries
     */
    APPEND_TO_DRIVER_BLOCKLIST( DRIVER_OS_WINDOWS_XP,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorNVIDIA), GfxDriverInfo::allDevices,
      GfxDriverInfo::allFeatures, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
      DRIVER_LESS_THAN, V(6,14,12,5721), "257.21" );
    APPEND_TO_DRIVER_BLOCKLIST( DRIVER_OS_WINDOWS_VISTA,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorNVIDIA), GfxDriverInfo::allDevices,
      GfxDriverInfo::allFeatures, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
      DRIVER_LESS_THAN, V(8,17,12,5721), "257.21" );
    APPEND_TO_DRIVER_BLOCKLIST( DRIVER_OS_WINDOWS_7,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorNVIDIA), GfxDriverInfo::allDevices,
      GfxDriverInfo::allFeatures, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
      DRIVER_LESS_THAN, V(8,17,12,5721), "257.21" );

    /*
     * AMD/ATI entries
     */
    APPEND_TO_DRIVER_BLOCKLIST( DRIVER_OS_ALL,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorATI), GfxDriverInfo::allDevices,
      GfxDriverInfo::allFeatures, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
      DRIVER_LESS_THAN, V(8,741,0,0), "10.6" );
    APPEND_TO_DRIVER_BLOCKLIST( DRIVER_OS_ALL,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorAMD), GfxDriverInfo::allDevices,
      GfxDriverInfo::allFeatures, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,
      DRIVER_LESS_THAN, V(8,741,0,0), "10.6" );

    /* OpenGL on any ATI/AMD hardware is discouraged
     * See:
     *  bug 619773 - WebGL: Crash with blue screen : "NMI: Parity Check / Memory Parity Error"
     *  bugs 584403, 584404, 620924 - crashes in atioglxx
     *  + many complaints about incorrect rendering
     */
    APPEND_TO_DRIVER_BLOCKLIST2( DRIVER_OS_ALL,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorATI), GfxDriverInfo::allDevices,
      nsIGfxInfo::FEATURE_OPENGL_LAYERS, nsIGfxInfo::FEATURE_DISCOURAGED,
      DRIVER_LESS_THAN, GfxDriverInfo::allDriverVersions );
    APPEND_TO_DRIVER_BLOCKLIST2( DRIVER_OS_ALL,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorATI), GfxDriverInfo::allDevices,
      nsIGfxInfo::FEATURE_WEBGL_OPENGL, nsIGfxInfo::FEATURE_DISCOURAGED,
      DRIVER_LESS_THAN, GfxDriverInfo::allDriverVersions );
    APPEND_TO_DRIVER_BLOCKLIST2( DRIVER_OS_ALL,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorAMD), GfxDriverInfo::allDevices,
      nsIGfxInfo::FEATURE_OPENGL_LAYERS, nsIGfxInfo::FEATURE_DISCOURAGED,
      DRIVER_LESS_THAN, GfxDriverInfo::allDriverVersions );
    APPEND_TO_DRIVER_BLOCKLIST2( DRIVER_OS_ALL,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorAMD), GfxDriverInfo::allDevices,
      nsIGfxInfo::FEATURE_WEBGL_OPENGL, nsIGfxInfo::FEATURE_DISCOURAGED,
      DRIVER_LESS_THAN, GfxDriverInfo::allDriverVersions );

    /*
     * Intel entries
     */

    /* implement the blocklist from bug 594877
     * Block all features on any drivers before this, as there's a crash when a MS Hotfix is installed.
     * The crash itself is Direct2D-related, but for safety we block all features.
     */
    #define IMPLEMENT_INTEL_DRIVER_BLOCKLIST(winVer, devFamily, driverVer)                                                      \
      APPEND_TO_DRIVER_BLOCKLIST2( winVer,                                                                                      \
        (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorIntel), (GfxDeviceFamily*) GfxDriverInfo::GetDeviceFamily(devFamily), \
        GfxDriverInfo::allFeatures, nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION,                                                 \
        DRIVER_LESS_THAN, driverVer )

    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_XP, IntelGMA500,   V(6,14,11,1018));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_XP, IntelGMA900,   V(6,14,10,4764));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_XP, IntelGMA950,   V(6,14,10,4926));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_XP, IntelGMA3150,  V(6,14,10,5260));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_XP, IntelGMAX3000, V(6,14,10,5218));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_XP, IntelGMAX4500HD, V(6,14,10,5284));

    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_VISTA, IntelGMA500,   V(7,14,10,1006));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_VISTA, IntelGMA900,   GfxDriverInfo::allDriverVersions);
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_VISTA, IntelGMA950,   V(7,14,10,1504));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_VISTA, IntelGMA3150,  V(7,14,10,2124));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_VISTA, IntelGMAX3000, V(7,15,10,1666));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_VISTA, IntelGMAX4500HD, V(8,15,10,2202));

    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_7, IntelGMA500,   V(5,0,0,2026));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_7, IntelGMA900,   GfxDriverInfo::allDriverVersions);
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_7, IntelGMA950,   V(8,15,10,1930));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_7, IntelGMA3150,  V(8,14,10,2117));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_7, IntelGMAX3000, V(8,15,10,1930));
    IMPLEMENT_INTEL_DRIVER_BLOCKLIST(DRIVER_OS_WINDOWS_7, IntelGMAX4500HD, V(8,15,10,2202));

    /* OpenGL on any Intel hardware is discouraged */
    APPEND_TO_DRIVER_BLOCKLIST2( DRIVER_OS_ALL,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorIntel), GfxDriverInfo::allDevices,
      nsIGfxInfo::FEATURE_OPENGL_LAYERS, nsIGfxInfo::FEATURE_DISCOURAGED,
      DRIVER_LESS_THAN, GfxDriverInfo::allDriverVersions );
    APPEND_TO_DRIVER_BLOCKLIST2( DRIVER_OS_ALL,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorIntel), GfxDriverInfo::allDevices,
      nsIGfxInfo::FEATURE_WEBGL_OPENGL, nsIGfxInfo::FEATURE_DISCOURAGED,
      DRIVER_LESS_THAN, GfxDriverInfo::allDriverVersions );

    /* Disable D3D9 layers on NVIDIA 6100/6150/6200 series due to glitches
     * whilst scrolling. See bugs: 612007, 644787 & 645872.
     */
    APPEND_TO_DRIVER_BLOCKLIST2( DRIVER_OS_ALL,
      (nsAString&) GfxDriverInfo::GetDeviceVendor(VendorNVIDIA), (GfxDeviceFamily*) GfxDriverInfo::GetDeviceFamily(NvidiaBlockD3D9Layers),
      nsIGfxInfo::FEATURE_DIRECT3D_9_LAYERS, nsIGfxInfo::FEATURE_BLOCKED_DEVICE,
      DRIVER_LESS_THAN, GfxDriverInfo::allDriverVersions );
  }
  return *mDriverInfo;
}

nsresult
GfxInfo::GetFeatureStatusImpl(PRInt32 aFeature,
                              PRInt32 *aStatus, 
                              nsAString & aSuggestedDriverVersion, 
                              const nsTArray<GfxDriverInfo>& aDriverInfo,
                              OperatingSystem* aOS /* = nsnull */)
{
  NS_ENSURE_ARG_POINTER(aStatus);
  aSuggestedDriverVersion.SetIsVoid(true);
  OperatingSystem os = WindowsVersionToOperatingSystem(mWindowsVersion);
  *aStatus = nsIGfxInfo::FEATURE_STATUS_UNKNOWN;
  if (aOS)
    *aOS = os;

  // Don't evaluate special cases if we're checking the downloaded blocklist.
  if (!aDriverInfo.Length()) {
    nsAutoString adapterVendorID;
    nsAutoString adapterDeviceID;
    nsAutoString adapterDriverVersionString;
    if (NS_FAILED(GetAdapterVendorID(adapterVendorID)) ||
        NS_FAILED(GetAdapterDeviceID(adapterDeviceID)) ||
        NS_FAILED(GetAdapterDriverVersion(adapterDriverVersionString)))
    {
      return NS_ERROR_FAILURE;
    }

    if (!adapterVendorID.Equals(GfxDriverInfo::GetDeviceVendor(VendorIntel), nsCaseInsensitiveStringComparator()) &&
        !adapterVendorID.Equals(GfxDriverInfo::GetDeviceVendor(VendorNVIDIA), nsCaseInsensitiveStringComparator()) &&
        !adapterVendorID.Equals(GfxDriverInfo::GetDeviceVendor(VendorAMD), nsCaseInsensitiveStringComparator()) &&
        !adapterVendorID.Equals(GfxDriverInfo::GetDeviceVendor(VendorATI), nsCaseInsensitiveStringComparator()) &&
        // FIXME - these special hex values are currently used in xpcshell tests introduced by
        // bug 625160 patch 8/8. Maybe these tests need to be adjusted now that we're only whitelisting
        // intel/ati/nvidia.
        !adapterVendorID.LowerCaseEqualsLiteral("0xabcd") &&
        !adapterVendorID.LowerCaseEqualsLiteral("0xdcba") &&
        !adapterVendorID.LowerCaseEqualsLiteral("0xabab") &&
        !adapterVendorID.LowerCaseEqualsLiteral("0xdcdc"))
    {
      *aStatus = FEATURE_BLOCKED_DEVICE;
      return NS_OK;
    }

    PRUint64 driverVersion;
    if (!ParseDriverVersion(adapterDriverVersionString, &driverVersion)) {
      return NS_ERROR_FAILURE;
    }

    // special-case the WinXP test slaves: they have out-of-date drivers, but we still want to
    // whitelist them, actually we do know that this combination of device and driver version
    // works well.
    if (mWindowsVersion == gfxWindowsPlatform::kWindowsXP &&
        adapterVendorID.Equals(GfxDriverInfo::GetDeviceVendor(VendorNVIDIA), nsCaseInsensitiveStringComparator()) &&
        adapterDeviceID.LowerCaseEqualsLiteral("0x0861") && // GeForce 9400
        driverVersion == V(6,14,11,7756))
    {
      *aStatus = FEATURE_NO_INFO;
      return NS_OK;
    }

    // ANGLE currently uses D3D10 <-> D3D9 interop, which crashes on Optimus
    // machines.
    if (aFeature == FEATURE_WEBGL_ANGLE &&
        gfxWindowsPlatform::IsOptimus())
    {
      *aStatus = FEATURE_BLOCKED_DEVICE;
      return NS_OK;
    }

    // Windows Server 2003 should be just like Windows XP for present purpose, but still has a different version number.
    // OTOH Windows Server 2008 R1 and R2 already have the same version numbers as Vista and Seven respectively
    if (os == DRIVER_OS_WINDOWS_SERVER_2003)
      os = DRIVER_OS_WINDOWS_XP;

    if (mHasDriverVersionMismatch) {
      if (aFeature == nsIGfxInfo::FEATURE_DIRECT3D_10_LAYERS ||
          aFeature == nsIGfxInfo::FEATURE_DIRECT3D_10_1_LAYERS ||
          aFeature == nsIGfxInfo::FEATURE_DIRECT2D)
      {
        *aStatus = nsIGfxInfo::FEATURE_BLOCKED_DRIVER_VERSION;
        return NS_OK;
      }
    }
  }

  return GfxInfoBase::GetFeatureStatusImpl(aFeature, aStatus, aSuggestedDriverVersion, aDriverInfo, &os);
}

#ifdef DEBUG

// Implement nsIGfxInfoDebug

/* void spoofVendorID (in DOMString aVendorID); */
NS_IMETHODIMP GfxInfo::SpoofVendorID(const nsAString & aVendorID)
{
  mAdapterVendorID = aVendorID;
  return NS_OK;
}

/* void spoofDeviceID (in unsigned long aDeviceID); */
NS_IMETHODIMP GfxInfo::SpoofDeviceID(const nsAString & aDeviceID)
{
  mAdapterDeviceID = aDeviceID;
  return NS_OK;
}

/* void spoofDriverVersion (in DOMString aDriverVersion); */
NS_IMETHODIMP GfxInfo::SpoofDriverVersion(const nsAString & aDriverVersion)
{
  mDriverVersion = aDriverVersion;
  return NS_OK;
}

/* void spoofOSVersion (in unsigned long aVersion); */
NS_IMETHODIMP GfxInfo::SpoofOSVersion(PRUint32 aVersion)
{
  mWindowsVersion = aVersion;
  return NS_OK;
}

#endif
