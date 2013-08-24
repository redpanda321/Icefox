/* -*- Mode: C++; tab-width: 3; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim:set ts=2 sts=2 sw=2 et cin:
 *
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is the Mozilla browser.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications, Inc.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Scott MacGregor <mscott@netscape.com>
 *   Dan Mosedale <dmose@mozilla.org>
 *   Jim Mathies <jmathies@mozilla.com>
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

#include "nsOSHelperAppService.h"
#include "nsISupports.h"
#include "nsString.h"
#include "nsXPIDLString.h"
#include "nsIURL.h"
#include "nsIMIMEInfo.h"
#include "nsMIMEInfoWin.h"
#include "nsMimeTypes.h"
#include "nsILocalFileWin.h"
#include "nsIProcess.h"
#include "plstr.h"
#include "nsAutoPtr.h"
#include "nsNativeCharsetUtils.h"
#include "nsIWindowsRegKey.h"

// shellapi.h is needed to build with WIN32_LEAN_AND_MEAN
#include <shellapi.h>

#define LOG(args) PR_LOG(mLog, PR_LOG_DEBUG, args)

// helper methods: forward declarations...
static nsresult GetExtensionFrom4xRegistryInfo(const nsACString& aMimeType, 
                                               nsString& aFileExtension);
static nsresult GetExtensionFromWindowsMimeDatabase(const nsACString& aMimeType,
                                                    nsString& aFileExtension);

nsOSHelperAppService::nsOSHelperAppService() : 
  nsExternalHelperAppService()
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
  , mAppAssoc(nsnull)
#endif
{
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
  CoInitialize(NULL);
  CoCreateInstance(CLSID_ApplicationAssociationRegistration, NULL, CLSCTX_INPROC,
                   IID_IApplicationAssociationRegistration, (void**)&mAppAssoc);
#endif
}

nsOSHelperAppService::~nsOSHelperAppService()
{
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
  if (mAppAssoc)
    mAppAssoc->Release();
  mAppAssoc = nsnull;
  CoUninitialize();
#endif
}

// The windows registry provides a mime database key which lists a set of mime types and corresponding "Extension" values. 
// we can use this to look up our mime type to see if there is a preferred extension for the mime type.
static nsresult GetExtensionFromWindowsMimeDatabase(const nsACString& aMimeType,
                                                    nsString& aFileExtension)
{
  nsAutoString mimeDatabaseKey;
  mimeDatabaseKey.AssignLiteral("MIME\\Database\\Content Type\\");

  AppendASCIItoUTF16(aMimeType, mimeDatabaseKey);

  nsCOMPtr<nsIWindowsRegKey> regKey = 
    do_CreateInstance("@mozilla.org/windows-registry-key;1");
  if (!regKey) 
    return NS_ERROR_NOT_AVAILABLE;

  nsresult rv = regKey->Open(nsIWindowsRegKey::ROOT_KEY_CLASSES_ROOT,
                             mimeDatabaseKey,
                             nsIWindowsRegKey::ACCESS_QUERY_VALUE);
  
  if (NS_SUCCEEDED(rv))
     regKey->ReadStringValue(NS_LITERAL_STRING("Extension"), aFileExtension);

  return NS_OK;
}

// We have a serious problem!! I have this content type and the windows registry only gives me
// helper apps based on extension. Right now, we really don't have a good place to go for 
// trying to figure out the extension for a particular mime type....One short term hack is to look
// this information in 4.x (it's stored in the windows regsitry). 
static nsresult GetExtensionFrom4xRegistryInfo(const nsACString& aMimeType,
                                               nsString& aFileExtension)
{
  nsCOMPtr<nsIWindowsRegKey> regKey = 
    do_CreateInstance("@mozilla.org/windows-registry-key;1");
  if (!regKey) 
    return NS_ERROR_NOT_AVAILABLE;

  nsresult rv = regKey->
    Open(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER,
         NS_LITERAL_STRING("Software\\Netscape\\Netscape Navigator\\Suffixes"),
         nsIWindowsRegKey::ACCESS_QUERY_VALUE);
  if (NS_FAILED(rv))
    return NS_ERROR_NOT_AVAILABLE;
   
  rv = regKey->ReadStringValue(NS_ConvertASCIItoUTF16(aMimeType),
                               aFileExtension);
  if (NS_FAILED(rv))
    return NS_OK;

  aFileExtension.Insert(PRUnichar('.'), 0);
      
  // this may be a comma separated list of extensions...just take the 
  // first one for now...

  PRInt32 pos = aFileExtension.FindChar(PRUnichar(','));
  if (pos > 0) {
    // we have a comma separated list of types...
    // truncate everything after the first comma (including the comma)
    aFileExtension.Truncate(pos); 
  }
   
  return NS_OK;
}

nsresult nsOSHelperAppService::OSProtocolHandlerExists(const char * aProtocolScheme, PRBool * aHandlerExists)
{
  // look up the protocol scheme in the windows registry....if we find a match then we have a handler for it...
  *aHandlerExists = PR_FALSE;
  if (aProtocolScheme && *aProtocolScheme)
  {
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
    // Vista: use new application association interface
    if (mAppAssoc) {
      PRUnichar * pResult = nsnull;
      NS_ConvertASCIItoUTF16 scheme(aProtocolScheme);
      // We are responsible for freeing returned strings.
      HRESULT hr = mAppAssoc->QueryCurrentDefault(scheme.get(),
                                                  AT_URLPROTOCOL, AL_EFFECTIVE,
                                                  &pResult);
      if (SUCCEEDED(hr)) {
        CoTaskMemFree(pResult);
        *aHandlerExists = PR_TRUE;
      }
      return NS_OK;
    }
#endif

    HKEY hKey;
    LONG err = ::RegOpenKeyExW(HKEY_CLASSES_ROOT,
                               NS_ConvertASCIItoUTF16(aProtocolScheme).get(),
                               0,
                               KEY_QUERY_VALUE,
                               &hKey);
    if (err == ERROR_SUCCESS)
    {
      err = ::RegQueryValueExW(hKey, L"URL Protocol", NULL, NULL, NULL, NULL);
      *aHandlerExists = (err == ERROR_SUCCESS);
      // close the key
      ::RegCloseKey(hKey);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP nsOSHelperAppService::GetApplicationDescription(const nsACString& aScheme, nsAString& _retval)
{
  nsCOMPtr<nsIWindowsRegKey> regKey = 
    do_CreateInstance("@mozilla.org/windows-registry-key;1");
  if (!regKey) 
    return NS_ERROR_NOT_AVAILABLE;

  NS_ConvertASCIItoUTF16 buf(aScheme);

#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
  // Vista: use new application association interface
  if (mAppAssoc) {
    PRUnichar * pResult = nsnull;
    // We are responsible for freeing returned strings.
    HRESULT hr = mAppAssoc->QueryCurrentDefault(buf.get(),
                                                AT_URLPROTOCOL, AL_EFFECTIVE,
                                                &pResult);
    if (SUCCEEDED(hr)) {
      nsCOMPtr<nsIFile> app;
      nsAutoString appInfo(pResult);
      CoTaskMemFree(pResult);
      if (NS_SUCCEEDED(GetDefaultAppInfo(appInfo, _retval, getter_AddRefs(app))))
        return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }
#endif

  nsCOMPtr<nsIFile> app;
  GetDefaultAppInfo(buf, _retval, getter_AddRefs(app));

  if (!_retval.Equals(buf))
    return NS_OK;

  // Fall back to full path
  buf.AppendLiteral("\\shell\\open\\command");
  nsresult rv = regKey->Open(nsIWindowsRegKey::ROOT_KEY_CLASSES_ROOT,
                             buf,
                             nsIWindowsRegKey::ACCESS_QUERY_VALUE);
  if (NS_FAILED(rv))
    return NS_ERROR_NOT_AVAILABLE;   
   
  rv = regKey->ReadStringValue(EmptyString(), _retval); 

  return NS_SUCCEEDED(rv) ? NS_OK : NS_ERROR_NOT_AVAILABLE;
}

// GetMIMEInfoFromRegistry: This function obtains the values of some of the nsIMIMEInfo
// attributes for the mimeType/extension associated with the input registry key.  The default
// entry for that key is the name of a registry key under HKEY_CLASSES_ROOT.  The default
// value for *that* key is the descriptive name of the type.  The EditFlags value is a binary
// value; the low order bit of the third byte of which indicates that the user does not need
// to be prompted.
//
// This function sets only the Description attribute of the input nsIMIMEInfo.
/* static */
nsresult nsOSHelperAppService::GetMIMEInfoFromRegistry(const nsAFlatString& fileType, nsIMIMEInfo *pInfo)
{
  nsresult rv = NS_OK;

  NS_ENSURE_ARG(pInfo);
  nsCOMPtr<nsIWindowsRegKey> regKey = 
    do_CreateInstance("@mozilla.org/windows-registry-key;1");
  if (!regKey) 
    return NS_ERROR_NOT_AVAILABLE;

  rv = regKey->Open(nsIWindowsRegKey::ROOT_KEY_CLASSES_ROOT,
                    fileType, nsIWindowsRegKey::ACCESS_QUERY_VALUE);
  if (NS_FAILED(rv))
    return NS_ERROR_FAILURE;
 
  // OK, the default value here is the description of the type.
  nsAutoString description;
  rv = regKey->ReadStringValue(EmptyString(), description);
  if (NS_SUCCEEDED(rv))
    pInfo->SetDescription(description);

  return NS_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////
// method overrides used to gather information from the windows registry for
// various mime types. 
////////////////////////////////////////////////////////////////////////////////////////////////

/// Looks up the type for the extension aExt and compares it to aType
/* static */ PRBool
nsOSHelperAppService::typeFromExtEquals(const PRUnichar* aExt, const char *aType)
{
  if (!aType)
    return PR_FALSE;
  nsAutoString fileExtToUse;
  if (aExt[0] != PRUnichar('.'))
    fileExtToUse = PRUnichar('.');

  fileExtToUse.Append(aExt);

  PRBool eq = PR_FALSE;
  nsCOMPtr<nsIWindowsRegKey> regKey = 
    do_CreateInstance("@mozilla.org/windows-registry-key;1");
  if (!regKey) 
    return eq;

  nsresult rv = regKey->Open(nsIWindowsRegKey::ROOT_KEY_CLASSES_ROOT,
                             fileExtToUse,
                             nsIWindowsRegKey::ACCESS_QUERY_VALUE);
  if (NS_FAILED(rv))
      return eq;
   
  nsAutoString type;
  rv = regKey->ReadStringValue(NS_LITERAL_STRING("Content Type"), type);
  if (NS_SUCCEEDED(rv))
     eq = type.EqualsASCII(aType);

  return eq;
}

// Strip a handler command string of its quotes and parameters.
static void CleanupHandlerPath(nsString& aPath)
{
  // Example command strings passed into this routine:

  // 1) C:\Program Files\Company\some.exe -foo -bar
  // 2) C:\Program Files\Company\some.dll
  // 3) C:\Windows\some.dll,-foo -bar
  // 4) C:\Windows\some.cpl,-foo -bar

  PRInt32 lastCommaPos = aPath.RFindChar(',');
  if (lastCommaPos != kNotFound)
    aPath.Truncate(lastCommaPos);

  aPath.AppendLiteral(" ");

  // case insensitive
  PRUint32 index = aPath.Find(".exe ", PR_TRUE);
  if (index == kNotFound)
    index = aPath.Find(".dll ", PR_TRUE);
  if (index == kNotFound)
    index = aPath.Find(".cpl ", PR_TRUE);

  if (index != kNotFound)
    aPath.Truncate(index + 4);
  aPath.Trim(" ", PR_TRUE, PR_TRUE);
}

// Strip the windows host process bootstrap executable rundll32.exe
// from a handler's command string if it exists.
static void StripRundll32(nsString& aCommandString)
{
  // Example rundll formats:
  // C:\Windows\System32\rundll32.exe "path to dll"
  // rundll32.exe "path to dll"
  // C:\Windows\System32\rundll32.exe "path to dll", var var
  // rundll32.exe "path to dll", var var

  NS_NAMED_LITERAL_STRING(rundllSegment, "rundll32.exe ");
  NS_NAMED_LITERAL_STRING(rundllSegmentShort, "rundll32 ");

  // case insensitive
  PRInt32 strLen = rundllSegment.Length();
  PRInt32 index = aCommandString.Find(rundllSegment, PR_TRUE);
  if (index == kNotFound) {
    strLen = rundllSegmentShort.Length();
    index = aCommandString.Find(rundllSegmentShort, PR_TRUE);
  }

  if (index != kNotFound) {
    PRUint32 rundllSegmentLength = index + strLen;
    aCommandString.Cut(0, rundllSegmentLength);
  }
}

// Returns the fully qualified path to an application handler based on
// a parameterized command string. Note this routine should not be used
// to launch the associated application as it strips parameters and
// rundll.exe from the string. Designed for retrieving display information
// on a particular handler.   
/* static */ PRBool nsOSHelperAppService::CleanupCmdHandlerPath(nsAString& aCommandHandler)
{
  nsAutoString handlerCommand(aCommandHandler);

  // Straight command path:
  //
  // %SystemRoot%\system32\NOTEPAD.EXE var
  // "C:\Program Files\iTunes\iTunes.exe" var var
  // C:\Program Files\iTunes\iTunes.exe var var
  //
  // Example rundll handlers:
  //
  // rundll32.exe "%ProgramFiles%\Win...ery\PhotoViewer.dll", var var
  // rundll32.exe "%ProgramFiles%\Windows Photo Gallery\PhotoViewer.dll"
  // C:\Windows\System32\rundll32.exe "path to dll", var var
  // %SystemRoot%\System32\rundll32.exe "%ProgramFiles%\Win...ery\Photo
  //    Viewer.dll", var var

  // Expand environment variables so we have full path strings.
  PRUint32 bufLength = ::ExpandEnvironmentStringsW(handlerCommand.get(),
                                                   L"", 0);
  if (bufLength == 0) // Error
    return PR_FALSE;

  nsAutoArrayPtr<PRUnichar> destination(new PRUnichar[bufLength]);
  if (!destination)
    return PR_FALSE;
  if (!::ExpandEnvironmentStringsW(handlerCommand.get(), destination,
                                   bufLength))
    return PR_FALSE;

  handlerCommand = destination;

  // Remove quotes around paths
  handlerCommand.StripChars("\"");

  // Strip windows host process bootstrap so we can get to the actual
  // handler.
  StripRundll32(handlerCommand);

  // Trim any command parameters so that we have a native path we can
  // initialize a local file with.
  CleanupHandlerPath(handlerCommand);

  aCommandHandler.Assign(handlerCommand);
  return PR_TRUE;
}

// The "real" name of a given helper app (as specified by the path to the 
// executable file held in various registry keys) is stored n the VERSIONINFO
// block in the file's resources. We need to find the path to the executable
// and then retrieve the "FileDescription" field value from the file. 
nsresult
nsOSHelperAppService::GetDefaultAppInfo(const nsAString& aAppInfo,
                                        nsAString& aDefaultDescription, 
                                        nsIFile** aDefaultApplication)
{
  nsAutoString handlerCommand;

  // If all else fails, use the file type key name, which will be 
  // something like "pngfile" for .pngs, "WMVFile" for .wmvs, etc. 
  aDefaultDescription = aAppInfo;
  *aDefaultApplication = nsnull;

  if (aAppInfo.IsEmpty())
    return NS_ERROR_FAILURE;

  // aAppInfo may be a file, file path, program id, or
  // Applications reference -
  // c:\dir\app.exe
  // Applications\appfile.exe/dll (shell\open...)
  // ProgID.progid (shell\open...)

  nsAutoString handlerKeyName(aAppInfo);

  nsCOMPtr<nsIWindowsRegKey> chkKey = 
    do_CreateInstance("@mozilla.org/windows-registry-key;1");
  if (!chkKey) 
    return NS_ERROR_FAILURE;
      
  nsresult rv = chkKey->Open(nsIWindowsRegKey::ROOT_KEY_CLASSES_ROOT,
                             handlerKeyName, 
                             nsIWindowsRegKey::ACCESS_QUERY_VALUE);
  if (NS_FAILED(rv)) {
    // It's a file system path to a handler 
    handlerCommand.Assign(aAppInfo);
  }
  else {
    handlerKeyName.AppendLiteral("\\shell\\open\\command");
    nsCOMPtr<nsIWindowsRegKey> regKey = 
      do_CreateInstance("@mozilla.org/windows-registry-key;1");
    if (!regKey) 
      return NS_ERROR_FAILURE;

    nsresult rv = regKey->Open(nsIWindowsRegKey::ROOT_KEY_CLASSES_ROOT,
                               handlerKeyName, 
                               nsIWindowsRegKey::ACCESS_QUERY_VALUE);
    if (NS_FAILED(rv))
      return NS_ERROR_FAILURE;
     
    // OK, the default value here is the description of the type.
    rv = regKey->ReadStringValue(EmptyString(), handlerCommand);
    if (NS_FAILED(rv))
      return NS_ERROR_FAILURE;
  }

  if (!CleanupCmdHandlerPath(handlerCommand))
    return NS_ERROR_FAILURE;

  // XXX FIXME: If this fails, the UI will display the full command
  // string.
  // There are some rare cases this can happen - ["url.dll" -foo]
  // for example won't resolve correctly to the system dir. The 
  // subsequent launch of the helper app will work though.
  nsCOMPtr<nsILocalFile> lf;
  NS_NewLocalFile(handlerCommand, PR_TRUE, getter_AddRefs(lf));
  if (!lf)
    return NS_ERROR_FILE_NOT_FOUND;

  nsILocalFileWin* lfw = nsnull;
  CallQueryInterface(lf, &lfw);

  if (lfw) {
    // The "FileDescription" field contains the actual name of the application.
    lfw->GetVersionInfoField("FileDescription", aDefaultDescription);
    // QI addref'ed for us.
    *aDefaultApplication = lfw;
  }

  return NS_OK;
}

already_AddRefed<nsMIMEInfoWin> nsOSHelperAppService::GetByExtension(const nsAFlatString& aFileExt, const char *aTypeHint)
{
  if (aFileExt.IsEmpty())
    return nsnull;

  // windows registry assumes your file extension is going to include the '.'.
  // so make sure it's there...
  nsAutoString fileExtToUse;
  if (aFileExt.First() != PRUnichar('.'))
    fileExtToUse = PRUnichar('.');

  fileExtToUse.Append(aFileExt);

  // Try to get an entry from the windows registry.
  nsCOMPtr<nsIWindowsRegKey> regKey = 
    do_CreateInstance("@mozilla.org/windows-registry-key;1");
  if (!regKey) 
    return nsnull; 

  nsresult rv = regKey->Open(nsIWindowsRegKey::ROOT_KEY_CLASSES_ROOT,
                             fileExtToUse,
                             nsIWindowsRegKey::ACCESS_QUERY_VALUE);
  if (NS_FAILED(rv))
    return nsnull; 

  nsCAutoString typeToUse;
  if (aTypeHint && *aTypeHint) {
    typeToUse.Assign(aTypeHint);
  }
  else {
    nsAutoString temp;
    if (NS_FAILED(regKey->ReadStringValue(NS_LITERAL_STRING("Content Type"),
                  temp)) || temp.IsEmpty()) {
      return nsnull; 
    }
    // Content-Type is always in ASCII
    LossyAppendUTF16toASCII(temp, typeToUse);
  }

  nsMIMEInfoWin* mimeInfo = new nsMIMEInfoWin(typeToUse);
  if (!mimeInfo)
    return nsnull; // out of memory

  NS_ADDREF(mimeInfo);

  // don't append the '.'
  mimeInfo->AppendExtension(NS_ConvertUTF16toUTF8(Substring(fileExtToUse, 1)));
  mimeInfo->SetPreferredAction(nsIMIMEInfo::useSystemDefault);

  nsAutoString appInfo;
  PRBool found;

#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
  // Retrieve the default application for this extension
  if (mAppAssoc) {
    // Vista: use the new application association COM interfaces
    // for resolving helpers.
    nsString assocType(fileExtToUse);
    PRUnichar * pResult = nsnull;
    HRESULT hr = mAppAssoc->QueryCurrentDefault(assocType.get(),
                                                AT_FILEEXTENSION, AL_EFFECTIVE,
                                                &pResult);
    if (SUCCEEDED(hr)) {
      found = PR_TRUE;
      appInfo.Assign(pResult);
      CoTaskMemFree(pResult);
    } 
    else {
      found = PR_FALSE;
    }
  } 
  else
#endif
  {
    found = NS_SUCCEEDED(regKey->ReadStringValue(EmptyString(), 
                                                 appInfo));
  }

  // Bug 358297 - ignore the default handler, force the user to choose app
  if (appInfo.EqualsLiteral("XPSViewer.Document"))
    found = PR_FALSE;

  if (!found) {
    NS_IF_RELEASE(mimeInfo); // we failed to really find an entry in the registry
    return nsnull;
  }

  // Get other nsIMIMEInfo fields from registry, if possible.
  nsAutoString defaultDescription;
  nsCOMPtr<nsIFile> defaultApplication;
  
  if (NS_FAILED(GetDefaultAppInfo(appInfo, defaultDescription,
                                  getter_AddRefs(defaultApplication)))) {
    NS_IF_RELEASE(mimeInfo);
    return nsnull;
  }

  mimeInfo->SetDefaultDescription(defaultDescription);
  mimeInfo->SetDefaultApplicationHandler(defaultApplication);

  // Grab the general description
  GetMIMEInfoFromRegistry(appInfo, mimeInfo);

  return mimeInfo;
}

already_AddRefed<nsIMIMEInfo> nsOSHelperAppService::GetMIMEInfoFromOS(const nsACString& aMIMEType, const nsACString& aFileExt, PRBool *aFound)
{
  *aFound = PR_TRUE;

  const nsCString& flatType = PromiseFlatCString(aMIMEType);
  const nsCString& flatExt = PromiseFlatCString(aFileExt);

  nsAutoString fileExtension;
  /* XXX The Equals is a gross hack to wallpaper over the most common Win32
   * extension issues caused by the fix for bug 116938.  See bug
   * 120327, comment 271 for why this is needed.  Not even sure we
   * want to remove this once we have fixed all this stuff to work
   * right; any info we get from the OS on this type is pretty much
   * useless....
   * We'll do extension-based lookup for this type later in this function.
   */
  if (!aMIMEType.LowerCaseEqualsLiteral(APPLICATION_OCTET_STREAM)) {
    // (1) try to use the windows mime database to see if there is a mapping to a file extension
    // (2) try to see if we have some left over 4.x registry info we can peek at...
    GetExtensionFromWindowsMimeDatabase(aMIMEType, fileExtension);
    LOG(("Windows mime database: extension '%s'\n", fileExtension.get()));
    if (fileExtension.IsEmpty()) {
      GetExtensionFrom4xRegistryInfo(aMIMEType, fileExtension);
      LOG(("4.x Registry: extension '%s'\n", fileExtension.get()));
    }
  }
  // If we found an extension for the type, do the lookup
  nsMIMEInfoWin* mi = nsnull;
  if (!fileExtension.IsEmpty())
    mi = GetByExtension(fileExtension, flatType.get()).get();
  LOG(("Extension lookup on '%s' found: 0x%p\n", fileExtension.get(), mi));

  PRBool hasDefault = PR_FALSE;
  if (mi) {
    mi->GetHasDefaultHandler(&hasDefault);
    // OK. We might have the case that |aFileExt| is a valid extension for the
    // mimetype we were given. In that case, we do want to append aFileExt
    // to the mimeinfo that we have. (E.g.: We are asked for video/mpeg and
    // .mpg, but the primary extension for video/mpeg is .mpeg. But because
    // .mpg is an extension for video/mpeg content, we want to append it)
    if (!aFileExt.IsEmpty() && typeFromExtEquals(NS_ConvertUTF8toUTF16(flatExt).get(), flatType.get())) {
      LOG(("Appending extension '%s' to mimeinfo, because its mimetype is '%s'\n",
           flatExt.get(), flatType.get()));
      PRBool extExist = PR_FALSE;
      mi->ExtensionExists(aFileExt, &extExist);
      if (!extExist)
        mi->AppendExtension(aFileExt);
    }
  }
  if (!mi || !hasDefault) {
    nsRefPtr<nsMIMEInfoWin> miByExt =
      GetByExtension(NS_ConvertUTF8toUTF16(aFileExt), flatType.get());
    LOG(("Ext. lookup for '%s' found 0x%p\n", flatExt.get(), miByExt.get()));
    if (!miByExt && mi)
      return mi;
    if (miByExt && !mi) {
      miByExt.swap(mi);
      return mi;
    }
    if (!miByExt && !mi) {
      *aFound = PR_FALSE;
      mi = new nsMIMEInfoWin(flatType);
      if (mi) {
        NS_ADDREF(mi);
        if (!aFileExt.IsEmpty())
          mi->AppendExtension(aFileExt);
      }
      
      return mi;
    }

    // if we get here, mi has no default app. copy from extension lookup.
    nsCOMPtr<nsIFile> defaultApp;
    nsAutoString desc;
    miByExt->GetDefaultDescription(desc);

    mi->SetDefaultDescription(desc);
  }
  return mi;
}

NS_IMETHODIMP
nsOSHelperAppService::GetProtocolHandlerInfoFromOS(const nsACString &aScheme,
                                                   PRBool *found,
                                                   nsIHandlerInfo **_retval)
{
  NS_ASSERTION(!aScheme.IsEmpty(), "No scheme was specified!");

  nsresult rv = OSProtocolHandlerExists(nsPromiseFlatCString(aScheme).get(),
                                        found);
  if (NS_FAILED(rv))
    return rv;

  nsMIMEInfoWin *handlerInfo =
    new nsMIMEInfoWin(aScheme, nsMIMEInfoBase::eProtocolInfo);
  NS_ENSURE_TRUE(handlerInfo, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(*_retval = handlerInfo);

  if (!*found) {
    // Code that calls this requires an object regardless if the OS has
    // something for us, so we return the empty object.
    return NS_OK;
  }

  nsAutoString desc;
  GetApplicationDescription(aScheme, desc);
  handlerInfo->SetDefaultDescription(desc);

  return NS_OK;
}

