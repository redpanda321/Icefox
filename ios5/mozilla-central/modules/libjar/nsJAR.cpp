/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <string.h>
#include "nsJARInputStream.h"
#include "nsJAR.h"
#include "nsIFile.h"
#include "nsIConsoleService.h"
#include "nsICryptoHash.h"
#include "prprf.h"
#include "mozilla/Omnijar.h"

#ifdef XP_UNIX
  #include <sys/stat.h>
#elif defined (XP_WIN) || defined(XP_OS2)
  #include <io.h>
#endif

using namespace mozilla;

//----------------------------------------------
// nsJARManifestItem declaration
//----------------------------------------------
/*
 * nsJARManifestItem contains meta-information pertaining 
 * to an individual JAR entry, taken from the 
 * META-INF/MANIFEST.MF and META-INF/ *.SF files.
 * This is security-critical information, defined here so it is not
 * accessible from anywhere else.
 */
typedef enum
{
  JAR_INVALID       = 1,
  JAR_INTERNAL      = 2,
  JAR_EXTERNAL      = 3
} JARManifestItemType;

class nsJARManifestItem
{
public:
  JARManifestItemType mType;

  // True if the second step of verification (VerifyEntry) 
  // has taken place:
  bool                entryVerified;
  
  // Not signed, valid, or failure code
  PRInt16             status;
  
  // Internal storage of digests
  nsCString           calculatedSectionDigest;
  nsCString           storedEntryDigest;

  nsJARManifestItem();
  virtual ~nsJARManifestItem();
};

//-------------------------------------------------
// nsJARManifestItem constructors and destructor
//-------------------------------------------------
nsJARManifestItem::nsJARManifestItem(): mType(JAR_INTERNAL),
                                        entryVerified(false),
                                        status(JAR_NOT_SIGNED)
{
}

nsJARManifestItem::~nsJARManifestItem()
{
}

//----------------------------------------------
// nsJAR constructor/destructor
//----------------------------------------------
static bool
DeleteManifestEntry(nsHashKey* aKey, void* aData, void* closure)
{
//-- deletes an entry in  mManifestData.
  delete (nsJARManifestItem*)aData;
  return true;
}

// The following initialization makes a guess of 10 entries per jarfile.
nsJAR::nsJAR(): mZip(new nsZipArchive()),
                mManifestData(nsnull, nsnull, DeleteManifestEntry, nsnull, 10),
                mParsedManifest(false),
                mGlobalStatus(JAR_MANIFEST_NOT_PARSED),
                mReleaseTime(PR_INTERVAL_NO_TIMEOUT), 
                mCache(nsnull), 
                mLock("nsJAR::mLock"),
                mTotalItemsInManifest(0),
                mOpened(false)
{
}

nsJAR::~nsJAR()
{
  Close();
}

NS_IMPL_THREADSAFE_QUERY_INTERFACE1(nsJAR, nsIZipReader)
NS_IMPL_THREADSAFE_ADDREF(nsJAR)

// Custom Release method works with nsZipReaderCache...
nsrefcnt nsJAR::Release(void) 
{
  nsrefcnt count; 
  NS_PRECONDITION(0 != mRefCnt, "dup release"); 
  count = NS_AtomicDecrementRefcnt(mRefCnt); 
  NS_LOG_RELEASE(this, count, "nsJAR"); 
  if (0 == count) {
    mRefCnt = 1; /* stabilize */ 
    /* enable this to find non-threadsafe destructors: */ 
    /* NS_ASSERT_OWNINGTHREAD(nsJAR); */ 
    delete this;
    return 0; 
  }
  else if (1 == count && mCache) {
#ifdef DEBUG
    nsresult rv =
#endif
      mCache->ReleaseZip(this);
    NS_ASSERTION(NS_SUCCEEDED(rv), "failed to release zip file");
  }
  return count; 
} 

//----------------------------------------------
// nsIZipReader implementation
//----------------------------------------------

NS_IMETHODIMP
nsJAR::Open(nsIFile* zipFile)
{
  NS_ENSURE_ARG_POINTER(zipFile);
  if (mOpened) return NS_ERROR_FAILURE; // Already open!

  mZipFile = zipFile;
  mOuterZipEntry.Truncate();
  mOpened = true;
  
  // The omnijar is special, it is opened early on and closed late
  // this avoids reopening it
  nsRefPtr<nsZipArchive> zip = mozilla::Omnijar::GetReader(zipFile);
  if (zip) {
    mZip = zip;
    return NS_OK;
  }
  return mZip->OpenArchive(zipFile);
}

NS_IMETHODIMP
nsJAR::OpenInner(nsIZipReader *aZipReader, const nsACString &aZipEntry)
{
  NS_ENSURE_ARG_POINTER(aZipReader);
  if (mOpened) return NS_ERROR_FAILURE; // Already open!

  bool exist;
  nsresult rv = aZipReader->HasEntry(aZipEntry, &exist);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(exist, NS_ERROR_FILE_NOT_FOUND);

  rv = aZipReader->GetFile(getter_AddRefs(mZipFile));
  NS_ENSURE_SUCCESS(rv, rv);

  mOpened = true;

  mOuterZipEntry.Assign(aZipEntry);

  nsRefPtr<nsZipHandle> handle;
  rv = nsZipHandle::Init(static_cast<nsJAR*>(aZipReader)->mZip.get(), PromiseFlatCString(aZipEntry).get(),
                         getter_AddRefs(handle));
  if (NS_FAILED(rv))
    return rv;

  return mZip->OpenArchive(handle);
}

NS_IMETHODIMP
nsJAR::GetFile(nsIFile* *result)
{
  *result = mZipFile;
  NS_IF_ADDREF(*result);
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::Close()
{
  mOpened = false;
  mParsedManifest = false;
  mManifestData.Reset();
  mGlobalStatus = JAR_MANIFEST_NOT_PARSED;
  mTotalItemsInManifest = 0;

  nsRefPtr<nsZipArchive> greOmni = mozilla::Omnijar::GetReader(mozilla::Omnijar::GRE);
  nsRefPtr<nsZipArchive> appOmni = mozilla::Omnijar::GetReader(mozilla::Omnijar::APP);

  if (mZip == greOmni || mZip == appOmni) {
    mZip = new nsZipArchive();
    return NS_OK;
  }
  return mZip->CloseArchive();
}

NS_IMETHODIMP
nsJAR::Test(const nsACString &aEntryName)
{
  return mZip->Test(aEntryName.IsEmpty()? nsnull : PromiseFlatCString(aEntryName).get());
}

NS_IMETHODIMP
nsJAR::Extract(const nsACString &aEntryName, nsIFile* outFile)
{
  // nsZipArchive and zlib are not thread safe
  // we need to use a lock to prevent bug #51267
  MutexAutoLock lock(mLock);

  nsZipItem *item = mZip->GetItem(PromiseFlatCString(aEntryName).get());
  NS_ENSURE_TRUE(item, NS_ERROR_FILE_TARGET_DOES_NOT_EXIST);

  // Remove existing file or directory so we set permissions correctly.
  // If it's a directory that already exists and contains files, throw
  // an exception and return.

  //XXX Bug 332139:
  //XXX If we guarantee that rv in the case of a non-empty directory
  //XXX is always FILE_DIR_NOT_EMPTY, we can remove
  //XXX |rv == NS_ERROR_FAILURE| - bug 322183 needs to be completely
  //XXX fixed before that can happen
  nsresult rv = outFile->Remove(false);
  if (rv == NS_ERROR_FILE_DIR_NOT_EMPTY ||
      rv == NS_ERROR_FAILURE)
    return rv;

  if (item->IsDirectory())
  {
    rv = outFile->Create(nsIFile::DIRECTORY_TYPE, item->Mode());
    //XXX Do this in nsZipArchive?  It would be nice to keep extraction
    //XXX code completely there, but that would require a way to get a
    //XXX PRDir from outFile.
  }
  else
  {
    PRFileDesc* fd;
    rv = outFile->OpenNSPRFileDesc(PR_WRONLY | PR_CREATE_FILE, item->Mode(), &fd);
    if (NS_FAILED(rv)) return rv;

    // ExtractFile also closes the fd handle and resolves the symlink if needed
    nsCAutoString path;
    rv = outFile->GetNativePath(path);
    if (NS_FAILED(rv)) return rv;

    rv = mZip->ExtractFile(item, path.get(), fd);
  }
  if (NS_FAILED(rv)) return rv;

  // nsIFile needs milliseconds, while prtime is in microseconds.
  // non-fatal if this fails, ignore errors
  outFile->SetLastModifiedTime(item->LastModTime() / PR_USEC_PER_MSEC);

  return NS_OK;
}

NS_IMETHODIMP    
nsJAR::GetEntry(const nsACString &aEntryName, nsIZipEntry* *result)
{
  nsZipItem* zipItem = mZip->GetItem(PromiseFlatCString(aEntryName).get());
  NS_ENSURE_TRUE(zipItem, NS_ERROR_FILE_TARGET_DOES_NOT_EXIST);

  nsJARItem* jarItem = new nsJARItem(zipItem);
  NS_ENSURE_TRUE(jarItem, NS_ERROR_OUT_OF_MEMORY);

  NS_ADDREF(*result = jarItem);
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::HasEntry(const nsACString &aEntryName, bool *result)
{
  *result = mZip->GetItem(PromiseFlatCString(aEntryName).get()) != nsnull;
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::FindEntries(const nsACString &aPattern, nsIUTF8StringEnumerator **result)
{
  NS_ENSURE_ARG_POINTER(result);

  nsZipFind *find;
  nsresult rv = mZip->FindInit(aPattern.IsEmpty()? nsnull : PromiseFlatCString(aPattern).get(), &find);
  NS_ENSURE_SUCCESS(rv, rv);

  nsIUTF8StringEnumerator *zipEnum = new nsJAREnumerator(find);
  if (!zipEnum) {
    delete find;
    return NS_ERROR_OUT_OF_MEMORY;
  }

  NS_ADDREF(*result = zipEnum);
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::GetInputStream(const nsACString &aFilename, nsIInputStream** result)
{
  return GetInputStreamWithSpec(EmptyCString(), aFilename, result);
}

NS_IMETHODIMP
nsJAR::GetInputStreamWithSpec(const nsACString& aJarDirSpec, 
                          const nsACString &aEntryName, nsIInputStream** result)
{
  NS_ENSURE_ARG_POINTER(result);

  // Watch out for the jar:foo.zip!/ (aDir is empty) top-level special case!
  nsZipItem *item = nsnull;
  const char *entry = PromiseFlatCString(aEntryName).get();
  if (*entry) {
    // First check if item exists in jar
    item = mZip->GetItem(entry);
    if (!item) return NS_ERROR_FILE_TARGET_DOES_NOT_EXIST;
  }
  nsJARInputStream* jis = new nsJARInputStream();
  // addref now so we can call InitFile/InitDirectory()
  NS_ENSURE_TRUE(jis, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(*result = jis);

  nsresult rv = NS_OK;
  if (!item || item->IsDirectory()) {
    rv = jis->InitDirectory(this, aJarDirSpec, entry);
  } else {
    rv = jis->InitFile(this, item);
  }
  if (NS_FAILED(rv)) {
    NS_RELEASE(*result);
  }
  return rv;
}

NS_IMETHODIMP
nsJAR::GetCertificatePrincipal(const nsACString &aFilename, nsIPrincipal** aPrincipal)
{
  //-- Parameter check
  if (!aPrincipal)
    return NS_ERROR_NULL_POINTER;
  *aPrincipal = nsnull;

  // Don't check signatures in the omnijar - this is only
  // interesting for extensions/XPIs.
  nsRefPtr<nsZipArchive> greOmni = mozilla::Omnijar::GetReader(mozilla::Omnijar::GRE);
  nsRefPtr<nsZipArchive> appOmni = mozilla::Omnijar::GetReader(mozilla::Omnijar::APP);

  if (mZip == greOmni || mZip == appOmni)
    return NS_OK;

  //-- Parse the manifest
  nsresult rv = ParseManifest();
  if (NS_FAILED(rv)) return rv;
  if (mGlobalStatus == JAR_NO_MANIFEST)
    return NS_OK;

  PRInt16 requestedStatus;
  if (!aFilename.IsEmpty())
  {
    //-- Find the item
    nsCStringKey key(aFilename);
    nsJARManifestItem* manItem = static_cast<nsJARManifestItem*>(mManifestData.Get(&key));
    if (!manItem)
      return NS_OK;
    //-- Verify the item against the manifest
    if (!manItem->entryVerified)
    {
      nsXPIDLCString entryData;
      PRUint32 entryDataLen;
      rv = LoadEntry(aFilename, getter_Copies(entryData), &entryDataLen);
      if (NS_FAILED(rv)) return rv;
      rv = VerifyEntry(manItem, entryData, entryDataLen);
      if (NS_FAILED(rv)) return rv;
    }
    requestedStatus = manItem->status;
  }
  else // User wants identity of signer w/o verifying any entries
    requestedStatus = mGlobalStatus;

  if (requestedStatus != JAR_VALID_MANIFEST)
    ReportError(aFilename, requestedStatus);
  else // Valid signature
  {
    *aPrincipal = mPrincipal;
    NS_IF_ADDREF(*aPrincipal);
  }
  return NS_OK;
}

NS_IMETHODIMP 
nsJAR::GetManifestEntriesCount(PRUint32* count)
{
  *count = mTotalItemsInManifest;
  return NS_OK;
}

nsresult
nsJAR::GetJarPath(nsACString& aResult)
{
  NS_ENSURE_ARG_POINTER(mZipFile);

  return mZipFile->GetNativePath(aResult);
}

//----------------------------------------------
// nsJAR private implementation
//----------------------------------------------
nsresult 
nsJAR::LoadEntry(const nsACString &aFilename, char** aBuf, PRUint32* aBufLen)
{
  //-- Get a stream for reading the file
  nsresult rv;
  nsCOMPtr<nsIInputStream> manifestStream;
  rv = GetInputStream(aFilename, getter_AddRefs(manifestStream));
  if (NS_FAILED(rv)) return NS_ERROR_FILE_TARGET_DOES_NOT_EXIST;
  
  //-- Read the manifest file into memory
  char* buf;
  PRUint32 len;
  rv = manifestStream->Available(&len);
  if (NS_FAILED(rv)) return rv;
  if (len == PRUint32(-1))
    return NS_ERROR_FILE_CORRUPTED; // bug 164695
  buf = (char*)malloc(len+1);
  if (!buf) return NS_ERROR_OUT_OF_MEMORY;
  PRUint32 bytesRead;
  rv = manifestStream->Read(buf, len, &bytesRead);
  if (bytesRead != len) 
    rv = NS_ERROR_FILE_CORRUPTED;
  if (NS_FAILED(rv)) {
    free(buf);
    return rv;
  }
  buf[len] = '\0'; //Null-terminate the buffer
  *aBuf = buf;
  if (aBufLen)
    *aBufLen = len;
  return NS_OK;
}


PRInt32
nsJAR::ReadLine(const char** src)
{
  //--Moves pointer to beginning of next line and returns line length
  //  not including CR/LF.
  PRInt32 length;
  char* eol = PL_strpbrk(*src, "\r\n");

  if (eol == nsnull) // Probably reached end of file before newline
  {
    length = PL_strlen(*src);
    if (length == 0) // immediate end-of-file
      *src = nsnull;
    else             // some data left on this line
      *src += length;
  }
  else
  {
    length = eol - *src;
    if (eol[0] == '\r' && eol[1] == '\n')      // CR LF, so skip 2
      *src = eol+2;
    else                                       // Either CR or LF, so skip 1
      *src = eol+1;
  }
  return length;
}

//-- The following #defines are used by ParseManifest()
//   and ParseOneFile(). The header strings are defined in the JAR specification.
#define JAR_MF 1
#define JAR_SF 2
#define JAR_MF_SEARCH_STRING "(M|/M)ETA-INF/(M|m)(ANIFEST|anifest).(MF|mf)$"
#define JAR_SF_SEARCH_STRING "(M|/M)ETA-INF/*.(SF|sf)$"
#define JAR_MF_HEADER (const char*)"Manifest-Version: 1.0"
#define JAR_SF_HEADER (const char*)"Signature-Version: 1.0"

nsresult
nsJAR::ParseManifest()
{
  //-- Verification Step 1
  if (mParsedManifest)
    return NS_OK;
  //-- (1)Manifest (MF) file
  nsCOMPtr<nsIUTF8StringEnumerator> files;
  nsresult rv = FindEntries(nsDependentCString(JAR_MF_SEARCH_STRING), getter_AddRefs(files));
  if (!files) rv = NS_ERROR_FAILURE;
  if (NS_FAILED(rv)) return rv;

  //-- Load the file into memory
  bool more;
  rv = files->HasMore(&more);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!more)
  {
    mGlobalStatus = JAR_NO_MANIFEST;
    mParsedManifest = true;
    return NS_OK;
  }

  nsCAutoString manifestFilename;
  rv = files->GetNext(manifestFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  // Check if there is more than one manifest, if so then error!
  rv = files->HasMore(&more);
  if (NS_FAILED(rv)) return rv;
  if (more)
  {
    mParsedManifest = true;
    return NS_ERROR_FILE_CORRUPTED; // More than one MF file
  }

  nsXPIDLCString manifestBuffer;
  PRUint32 manifestLen;
  rv = LoadEntry(manifestFilename, getter_Copies(manifestBuffer), &manifestLen);
  if (NS_FAILED(rv)) return rv;

  //-- Parse it
  rv = ParseOneFile(manifestBuffer, JAR_MF);
  if (NS_FAILED(rv)) return rv;

  //-- (2)Signature (SF) file
  // If there are multiple signatures, we select one.
  rv = FindEntries(nsDependentCString(JAR_SF_SEARCH_STRING), getter_AddRefs(files));
  if (!files) rv = NS_ERROR_FAILURE;
  if (NS_FAILED(rv)) return rv;
  //-- Get an SF file
  rv = files->HasMore(&more);
  if (NS_FAILED(rv)) return rv;
  if (!more)
  {
    mGlobalStatus = JAR_NO_MANIFEST;
    mParsedManifest = true;
    return NS_OK;
  }
  rv = files->GetNext(manifestFilename);
  if (NS_FAILED(rv)) return rv;

  rv = LoadEntry(manifestFilename, getter_Copies(manifestBuffer), &manifestLen);
  if (NS_FAILED(rv)) return rv;
  
  //-- Get its corresponding signature file
  nsCAutoString sigFilename(manifestFilename);
  PRInt32 extension = sigFilename.RFindChar('.') + 1;
  NS_ASSERTION(extension != 0, "Manifest Parser: Missing file extension.");
  (void)sigFilename.Cut(extension, 2);
  nsXPIDLCString sigBuffer;
  PRUint32 sigLen;
  {
    nsCAutoString tempFilename(sigFilename); tempFilename.Append("rsa", 3);
    rv = LoadEntry(tempFilename, getter_Copies(sigBuffer), &sigLen);
  }
  if (NS_FAILED(rv))
  {
    nsCAutoString tempFilename(sigFilename); tempFilename.Append("RSA", 3);
    rv = LoadEntry(tempFilename, getter_Copies(sigBuffer), &sigLen);
  }
  if (NS_FAILED(rv))
  {
    mGlobalStatus = JAR_NO_MANIFEST;
    mParsedManifest = true;
    return NS_OK;
  }

  //-- Get the signature verifier service
  nsCOMPtr<nsISignatureVerifier> verifier = 
           do_GetService(SIGNATURE_VERIFIER_CONTRACTID, &rv);
  if (NS_FAILED(rv)) // No signature verifier available
  {
    mGlobalStatus = JAR_NO_MANIFEST;
    mParsedManifest = true;
    return NS_OK;
  }

  //-- Verify that the signature file is a valid signature of the SF file
  PRInt32 verifyError;
  rv = verifier->VerifySignature(sigBuffer, sigLen, manifestBuffer, manifestLen, 
                                 &verifyError, getter_AddRefs(mPrincipal));
  if (NS_FAILED(rv)) return rv;
  if (mPrincipal && verifyError == 0)
    mGlobalStatus = JAR_VALID_MANIFEST;
  else if (verifyError == nsISignatureVerifier::VERIFY_ERROR_UNKNOWN_CA)
    mGlobalStatus = JAR_INVALID_UNKNOWN_CA;
  else
    mGlobalStatus = JAR_INVALID_SIG;

  //-- Parse the SF file. If the verification above failed, principal
  // is null, and ParseOneFile will mark the relevant entries as invalid.
  // if ParseOneFile fails, then it has no effect, and we can safely 
  // continue to the next SF file, or return. 
  ParseOneFile(manifestBuffer, JAR_SF);
  mParsedManifest = true;

  return NS_OK;
}

nsresult
nsJAR::ParseOneFile(const char* filebuf, PRInt16 aFileType)
{
  //-- Check file header
  const char* nextLineStart = filebuf;
  nsCAutoString curLine;
  PRInt32 linelen;
  linelen = ReadLine(&nextLineStart);
  curLine.Assign(filebuf, linelen);

  if ( ((aFileType == JAR_MF) && !curLine.Equals(JAR_MF_HEADER) ) ||
       ((aFileType == JAR_SF) && !curLine.Equals(JAR_SF_HEADER) ) )
     return NS_ERROR_FILE_CORRUPTED;

  //-- Skip header section
  do {
    linelen = ReadLine(&nextLineStart);
  } while (linelen > 0);

  //-- Set up parsing variables
  const char* curPos;
  const char* sectionStart = nextLineStart;

  nsJARManifestItem* curItemMF = nsnull;
  bool foundName = false;
  if (aFileType == JAR_MF)
    if (!(curItemMF = new nsJARManifestItem()))
      return NS_ERROR_OUT_OF_MEMORY;

  nsCAutoString curItemName;
  nsCAutoString storedSectionDigest;

  for(;;)
  {
    curPos = nextLineStart;
    linelen = ReadLine(&nextLineStart);
    curLine.Assign(curPos, linelen);
    if (linelen == 0) 
    // end of section (blank line or end-of-file)
    {
      if (aFileType == JAR_MF)
      {
        mTotalItemsInManifest++;
        if (curItemMF->mType != JAR_INVALID)
        { 
          //-- Did this section have a name: line?
          if(!foundName)
            curItemMF->mType = JAR_INVALID;
          else 
          {
            //-- If it's an internal item, it must correspond 
            //   to a valid jar entry
            if (curItemMF->mType == JAR_INTERNAL)
            {
              bool exists;
              nsresult rv = HasEntry(curItemName, &exists);
              if (NS_FAILED(rv) || !exists)
                curItemMF->mType = JAR_INVALID;
            }
            //-- Check for duplicates
            nsCStringKey key(curItemName);
            if (mManifestData.Exists(&key))
              curItemMF->mType = JAR_INVALID;
          }
        }

        if (curItemMF->mType == JAR_INVALID)
          delete curItemMF;
        else //-- calculate section digest
        {
          PRUint32 sectionLength = curPos - sectionStart;
          CalculateDigest(sectionStart, sectionLength,
                          curItemMF->calculatedSectionDigest);
          //-- Save item in the hashtable
          nsCStringKey itemKey(curItemName);
          mManifestData.Put(&itemKey, (void*)curItemMF);
        }
        if (nextLineStart == nsnull) // end-of-file
          break;

        sectionStart = nextLineStart;
        if (!(curItemMF = new nsJARManifestItem()))
          return NS_ERROR_OUT_OF_MEMORY;
      } // (aFileType == JAR_MF)
      else
        //-- file type is SF, compare digest with calculated 
        //   section digests from MF file.
      {
        if (foundName)
        {
          nsJARManifestItem* curItemSF;
          nsCStringKey key(curItemName);
          curItemSF = (nsJARManifestItem*)mManifestData.Get(&key);
          if(curItemSF)
          {
            NS_ASSERTION(curItemSF->status == JAR_NOT_SIGNED,
                         "SECURITY ERROR: nsJARManifestItem not correctly initialized");
            curItemSF->status = mGlobalStatus;
            if (curItemSF->status == JAR_VALID_MANIFEST)
            { // Compare digests
              if (storedSectionDigest.IsEmpty())
                curItemSF->status = JAR_NOT_SIGNED;
              else
              {
                if (!storedSectionDigest.Equals(curItemSF->calculatedSectionDigest))
                  curItemSF->status = JAR_INVALID_MANIFEST;
                curItemSF->calculatedSectionDigest.Truncate();
                storedSectionDigest.Truncate();
              }
            } // (aPrincipal != nsnull)
          } // if(curItemSF)
        } // if(foundName)

        if(nextLineStart == nsnull) // end-of-file
          break;
      } // aFileType == JAR_SF
      foundName = false;
      continue;
    } // if(linelen == 0)

    //-- Look for continuations (beginning with a space) on subsequent lines
    //   and append them to the current line.
    while(*nextLineStart == ' ')
    {
      curPos = nextLineStart;
      PRInt32 continuationLen = ReadLine(&nextLineStart) - 1;
      nsCAutoString continuation(curPos+1, continuationLen);
      curLine += continuation;
      linelen += continuationLen;
    }

    //-- Find colon in current line, this separates name from value
    PRInt32 colonPos = curLine.FindChar(':');
    if (colonPos == -1)    // No colon on line, ignore line
      continue;
    //-- Break down the line
    nsCAutoString lineName;
    curLine.Left(lineName, colonPos);
    nsCAutoString lineData;
    curLine.Mid(lineData, colonPos+2, linelen - (colonPos+2));

    //-- Lines to look for:
    // (1) Digest:
    if (lineName.LowerCaseEqualsLiteral("sha1-digest"))
    //-- This is a digest line, save the data in the appropriate place 
    {
      if(aFileType == JAR_MF)
        curItemMF->storedEntryDigest = lineData;
      else
        storedSectionDigest = lineData;
      continue;
    }
    
    // (2) Name: associates this manifest section with a file in the jar.
    if (!foundName && lineName.LowerCaseEqualsLiteral("name"))
    {
      curItemName = lineData;
      foundName = true;
      continue;
    }

    // (3) Magic: this may be an inline Javascript. 
    //     We can't do any other kind of magic.
    if (aFileType == JAR_MF && lineName.LowerCaseEqualsLiteral("magic"))
    {
      if (lineData.LowerCaseEqualsLiteral("javascript"))
        curItemMF->mType = JAR_EXTERNAL;
      else
        curItemMF->mType = JAR_INVALID;
      continue;
    }

  } // for (;;)
  return NS_OK;
} //ParseOneFile()

nsresult
nsJAR::VerifyEntry(nsJARManifestItem* aManItem, const char* aEntryData,
                   PRUint32 aLen)
{
  if (aManItem->status == JAR_VALID_MANIFEST)
  {
    if (aManItem->storedEntryDigest.IsEmpty())
      // No entry digests in manifest file. Entry is unsigned.
      aManItem->status = JAR_NOT_SIGNED;
    else
    { //-- Calculate and compare digests
      nsCString calculatedEntryDigest;
      nsresult rv = CalculateDigest(aEntryData, aLen, calculatedEntryDigest);
      if (NS_FAILED(rv)) return NS_ERROR_FAILURE;
      if (!aManItem->storedEntryDigest.Equals(calculatedEntryDigest))
        aManItem->status = JAR_INVALID_ENTRY;
      aManItem->storedEntryDigest.Truncate();
    }
  }
  aManItem->entryVerified = true;
  return NS_OK;
}

void nsJAR::ReportError(const nsACString &aFilename, PRInt16 errorCode)
{
  //-- Generate error message
  nsAutoString message; 
  message.AssignLiteral("Signature Verification Error: the signature on ");
  if (!aFilename.IsEmpty())
    AppendASCIItoUTF16(aFilename, message);
  else
    message.AppendLiteral("this .jar archive");
  message.AppendLiteral(" is invalid because ");
  switch(errorCode)
  {
  case JAR_NOT_SIGNED:
    message.AppendLiteral("the archive did not contain a valid PKCS7 signature.");
    break;
  case JAR_INVALID_SIG:
    message.AppendLiteral("the digital signature (*.RSA) file is not a valid signature of the signature instruction file (*.SF).");
    break;
  case JAR_INVALID_UNKNOWN_CA:
    message.AppendLiteral("the certificate used to sign this file has an unrecognized issuer.");
    break;
  case JAR_INVALID_MANIFEST:
    message.AppendLiteral("the signature instruction file (*.SF) does not contain a valid hash of the MANIFEST.MF file.");
    break;
  case JAR_INVALID_ENTRY:
    message.AppendLiteral("the MANIFEST.MF file does not contain a valid hash of the file being verified.");
    break;
  case JAR_NO_MANIFEST:
    message.AppendLiteral("the archive did not contain a manifest.");
    break;
  default:
    message.AppendLiteral("of an unknown problem.");
  }
  
  // Report error in JS console
  nsCOMPtr<nsIConsoleService> console(do_GetService("@mozilla.org/consoleservice;1"));
  if (console)
  {
    console->LogStringMessage(message.get());
  }
#ifdef DEBUG
  char* messageCstr = ToNewCString(message);
  if (!messageCstr) return;
  fprintf(stderr, "%s\n", messageCstr);
  nsMemory::Free(messageCstr);
#endif
}


nsresult nsJAR::CalculateDigest(const char* aInBuf, PRUint32 aLen,
                                nsCString& digest)
{
  nsresult rv;

  nsCOMPtr<nsICryptoHash> hasher = do_CreateInstance("@mozilla.org/security/hash;1", &rv);
  if (NS_FAILED(rv)) return rv;

  rv = hasher->Init(nsICryptoHash::SHA1);
  if (NS_FAILED(rv)) return rv;

  rv = hasher->Update((const PRUint8*) aInBuf, aLen);
  if (NS_FAILED(rv)) return rv;

  return hasher->Finish(true, digest);
}

NS_IMPL_THREADSAFE_ISUPPORTS1(nsJAREnumerator, nsIUTF8StringEnumerator)
  
//----------------------------------------------
// nsJAREnumerator::HasMore
//----------------------------------------------
NS_IMETHODIMP
nsJAREnumerator::HasMore(bool* aResult)
{
    // try to get the next element
    if (!mName) {
        NS_ASSERTION(mFind, "nsJAREnumerator: Missing zipFind.");
        nsresult rv = mFind->FindNext( &mName, &mNameLen );
        if (rv == NS_ERROR_FILE_TARGET_DOES_NOT_EXIST) {
            *aResult = false;                    // No more matches available
            return NS_OK;
        }
        NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);    // no error translation
    }

    *aResult = true;
    return NS_OK;
}

//----------------------------------------------
// nsJAREnumerator::GetNext
//----------------------------------------------
NS_IMETHODIMP
nsJAREnumerator::GetNext(nsACString& aResult)
{
    // check if the current item is "stale"
    if (!mName) {
        bool     bMore;
        nsresult rv = HasMore(&bMore);
        if (NS_FAILED(rv) || !bMore)
            return NS_ERROR_FAILURE; // no error translation
    }
    aResult.Assign(mName, mNameLen);
    mName = 0; // we just gave this one away
    return NS_OK;
}


NS_IMPL_THREADSAFE_ISUPPORTS1(nsJARItem, nsIZipEntry)

nsJARItem::nsJARItem(nsZipItem* aZipItem)
    : mSize(aZipItem->Size()),
      mRealsize(aZipItem->RealSize()),
      mCrc32(aZipItem->CRC32()),
      mLastModTime(aZipItem->LastModTime()),
      mCompression(aZipItem->Compression()),
      mIsDirectory(aZipItem->IsDirectory()),
      mIsSynthetic(aZipItem->isSynthetic)
{
}

//------------------------------------------
// nsJARItem::GetCompression
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetCompression(PRUint16 *aCompression)
{
    NS_ENSURE_ARG_POINTER(aCompression);

    *aCompression = mCompression;
    return NS_OK;
}

//------------------------------------------
// nsJARItem::GetSize
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetSize(PRUint32 *aSize)
{
    NS_ENSURE_ARG_POINTER(aSize);

    *aSize = mSize;
    return NS_OK;
}

//------------------------------------------
// nsJARItem::GetRealSize
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetRealSize(PRUint32 *aRealsize)
{
    NS_ENSURE_ARG_POINTER(aRealsize);

    *aRealsize = mRealsize;
    return NS_OK;
}

//------------------------------------------
// nsJARItem::GetCrc32
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetCRC32(PRUint32 *aCrc32)
{
    NS_ENSURE_ARG_POINTER(aCrc32);

    *aCrc32 = mCrc32;
    return NS_OK;
}

//------------------------------------------
// nsJARItem::GetIsDirectory
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetIsDirectory(bool *aIsDirectory)
{
    NS_ENSURE_ARG_POINTER(aIsDirectory);

    *aIsDirectory = mIsDirectory;
    return NS_OK;
}

//------------------------------------------
// nsJARItem::GetIsSynthetic
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetIsSynthetic(bool *aIsSynthetic)
{
    NS_ENSURE_ARG_POINTER(aIsSynthetic);

    *aIsSynthetic = mIsSynthetic;
    return NS_OK;
}

//------------------------------------------
// nsJARItem::GetLastModifiedTime
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetLastModifiedTime(PRTime* aLastModTime)
{
    NS_ENSURE_ARG_POINTER(aLastModTime);

    *aLastModTime = mLastModTime;
    return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsIZipReaderCache

NS_IMPL_THREADSAFE_ISUPPORTS3(nsZipReaderCache, nsIZipReaderCache, nsIObserver, nsISupportsWeakReference)

nsZipReaderCache::nsZipReaderCache()
  : mLock("nsZipReaderCache.mLock")
  , mZips(16)
#ifdef ZIP_CACHE_HIT_RATE
    ,
    mZipCacheLookups(0),
    mZipCacheHits(0),
    mZipCacheFlushes(0),
    mZipSyncMisses(0)
#endif
{
}

NS_IMETHODIMP
nsZipReaderCache::Init(PRUint32 cacheSize)
{
  mCacheSize = cacheSize; 
  
// Register as a memory pressure observer 
  nsCOMPtr<nsIObserverService> os = 
           do_GetService("@mozilla.org/observer-service;1");
  if (os)
  {
    os->AddObserver(this, "memory-pressure", true);
    os->AddObserver(this, "chrome-flush-caches", true);
    os->AddObserver(this, "flush-cache-entry", true);
  }
// ignore failure of the observer registration.

  return NS_OK;
}

static bool
DropZipReaderCache(nsHashKey *aKey, void *aData, void* closure)
{
  nsJAR* zip = (nsJAR*)aData;
  zip->SetZipReaderCache(nsnull);
  return true;
}

nsZipReaderCache::~nsZipReaderCache()
{
  mZips.Enumerate(DropZipReaderCache, nsnull);

#ifdef ZIP_CACHE_HIT_RATE
  printf("nsZipReaderCache size=%d hits=%d lookups=%d rate=%f%% flushes=%d missed %d\n",
         mCacheSize, mZipCacheHits, mZipCacheLookups, 
         (float)mZipCacheHits / mZipCacheLookups, 
         mZipCacheFlushes, mZipSyncMisses);
#endif
}

NS_IMETHODIMP
nsZipReaderCache::GetZip(nsIFile* zipFile, nsIZipReader* *result)
{
  NS_ENSURE_ARG_POINTER(zipFile);
  nsresult rv;
  nsCOMPtr<nsIZipReader> antiLockZipGrip;
  MutexAutoLock lock(mLock);

#ifdef ZIP_CACHE_HIT_RATE
  mZipCacheLookups++;
#endif

  nsCAutoString uri;
  rv = zipFile->GetNativePath(uri);
  if (NS_FAILED(rv)) return rv;

  uri.Insert(NS_LITERAL_CSTRING("file:"), 0);

  nsCStringKey key(uri);
  nsJAR* zip = static_cast<nsJAR*>(static_cast<nsIZipReader*>(mZips.Get(&key))); // AddRefs
  if (zip) {
#ifdef ZIP_CACHE_HIT_RATE
    mZipCacheHits++;
#endif
    zip->ClearReleaseTime();
  }
  else {
    zip = new nsJAR();
    if (zip == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(zip);
    zip->SetZipReaderCache(this);

    rv = zip->Open(zipFile);
    if (NS_FAILED(rv)) {
      NS_RELEASE(zip);
      return rv;
    }

#ifdef DEBUG
    bool collision =
#endif
      mZips.Put(&key, static_cast<nsIZipReader*>(zip)); // AddRefs to 2
    NS_ASSERTION(!collision, "horked");
  }
  *result = zip;
  return rv;
}

NS_IMETHODIMP
nsZipReaderCache::GetInnerZip(nsIFile* zipFile, const nsACString &entry,
                              nsIZipReader* *result)
{
  NS_ENSURE_ARG_POINTER(zipFile);

  nsCOMPtr<nsIZipReader> outerZipReader;
  nsresult rv = GetZip(zipFile, getter_AddRefs(outerZipReader));
  NS_ENSURE_SUCCESS(rv, rv);

#ifdef ZIP_CACHE_HIT_RATE
  mZipCacheLookups++;
#endif

  nsCAutoString uri;
  rv = zipFile->GetNativePath(uri);
  if (NS_FAILED(rv)) return rv;

  uri.Insert(NS_LITERAL_CSTRING("jar:"), 0);
  uri.AppendLiteral("!/");
  uri.Append(entry);

  nsCStringKey key(uri);
  nsJAR* zip = static_cast<nsJAR*>(static_cast<nsIZipReader*>(mZips.Get(&key))); // AddRefs
  if (zip) {
#ifdef ZIP_CACHE_HIT_RATE
    mZipCacheHits++;
#endif
    zip->ClearReleaseTime();
  }
  else {
    zip = new nsJAR();
    NS_ADDREF(zip);
    zip->SetZipReaderCache(this);

    rv = zip->OpenInner(outerZipReader, entry);
    if (NS_FAILED(rv)) {
      NS_RELEASE(zip);
      return rv;
    }
#ifdef DEBUG
    bool collision =
#endif
    mZips.Put(&key, static_cast<nsIZipReader*>(zip)); // AddRefs to 2
    NS_ASSERTION(!collision, "horked");
  }
  *result = zip;
  return rv;
}

static bool
FindOldestZip(nsHashKey *aKey, void *aData, void* closure)
{
  nsJAR** oldestPtr = (nsJAR**)closure;
  nsJAR* oldest = *oldestPtr;
  nsJAR* current = (nsJAR*)aData;
  PRIntervalTime currentReleaseTime = current->GetReleaseTime();
  if (currentReleaseTime != PR_INTERVAL_NO_TIMEOUT) {
    if (oldest == nsnull ||
        currentReleaseTime < oldest->GetReleaseTime()) {
      *oldestPtr = current;
    }    
  }
  return true;
}

struct ZipFindData {nsJAR* zip; bool found;}; 

static bool
FindZip(nsHashKey *aKey, void *aData, void* closure)
{
  ZipFindData* find_data = (ZipFindData*)closure;

  if (find_data->zip == (nsJAR*)aData) {
    find_data->found = true; 
    return false;
  }
  return true;
}

nsresult
nsZipReaderCache::ReleaseZip(nsJAR* zip)
{
  nsresult rv;
  MutexAutoLock lock(mLock);

  // It is possible that two thread compete for this zip. The dangerous 
  // case is where one thread Releases the zip and discovers that the ref
  // count has gone to one. Before it can call this ReleaseZip method
  // another thread calls our GetZip method. The ref count goes to two. That
  // second thread then Releases the zip and the ref count goes to one. It
  // then tries to enter this ReleaseZip method and blocks while the first
  // thread is still here. The first thread continues and remove the zip from 
  // the cache and calls its Release method sending the ref count to 0 and
  // deleting the zip. However, the second thread is still blocked at the
  // start of ReleaseZip, but the 'zip' param now hold a reference to a
  // deleted zip!
  // 
  // So, we are going to try safeguarding here by searching our hashtable while
  // locked here for the zip. We return fast if it is not found. 

  ZipFindData find_data = {zip, false};
  mZips.Enumerate(FindZip, &find_data);
  if (!find_data.found) {
#ifdef ZIP_CACHE_HIT_RATE
    mZipSyncMisses++;
#endif
    return NS_OK;
  }

  zip->SetReleaseTime();

  if (mZips.Count() <= mCacheSize)
    return NS_OK;

  nsJAR* oldest = nsnull;
  mZips.Enumerate(FindOldestZip, &oldest);
  
  // Because of the craziness above it is possible that there is no zip that
  // needs removing. 
  if (!oldest)
    return NS_OK;

#ifdef ZIP_CACHE_HIT_RATE
    mZipCacheFlushes++;
#endif

  // remove from hashtable
  nsCAutoString uri;
  rv = oldest->GetJarPath(uri);
  if (NS_FAILED(rv))
    return rv;

  if (oldest->mOuterZipEntry.IsEmpty()) {
    uri.Insert(NS_LITERAL_CSTRING("file:"), 0);
  } else {
    uri.Insert(NS_LITERAL_CSTRING("jar:"), 0);
    uri.AppendLiteral("!/");
    uri.Append(oldest->mOuterZipEntry);
  }

  nsCStringKey key(uri);
  nsRefPtr<nsJAR> removed;
  mZips.Remove(&key, (nsISupports **)removed.StartAssignment());
  NS_ASSERTION(removed, "botched");
  NS_ASSERTION(oldest == removed, "removed wrong entry");

  if (removed)
    removed->SetZipReaderCache(nsnull);

  return NS_OK;
}

static bool
FindFlushableZip(nsHashKey *aKey, void *aData, void* closure)
{
  nsHashKey** flushableKeyPtr = (nsHashKey**)closure;
  nsJAR* current = (nsJAR*)aData;
  
  if (current->GetReleaseTime() != PR_INTERVAL_NO_TIMEOUT) {
    *flushableKeyPtr = aKey;
    current->SetZipReaderCache(nsnull);
    return false;
  }
  return true;
}

NS_IMETHODIMP
nsZipReaderCache::Observe(nsISupports *aSubject,
                          const char *aTopic, 
                          const PRUnichar *aSomeData)
{
  if (strcmp(aTopic, "memory-pressure") == 0) {
    MutexAutoLock lock(mLock);
    while (true) {
      nsHashKey* flushable = nsnull;
      mZips.Enumerate(FindFlushableZip, &flushable); 
      if ( ! flushable )
        break;
#ifdef DEBUG
      bool removed =
#endif
        mZips.Remove(flushable);   // Releases
      NS_ASSERTION(removed, "botched");

#ifdef xDEBUG_jband
      printf("flushed something from the jar cache\n");
#endif
    }
  }
  else if (strcmp(aTopic, "chrome-flush-caches") == 0) {
    mZips.Enumerate(DropZipReaderCache, nsnull);
    mZips.Reset();
  }
  else if (strcmp(aTopic, "flush-cache-entry") == 0) {
    nsCOMPtr<nsIFile> file = do_QueryInterface(aSubject);
    if (!file)
      return NS_OK;

    nsCAutoString uri;
    if (NS_FAILED(file->GetNativePath(uri)))
      return NS_OK;

    uri.Insert(NS_LITERAL_CSTRING("file:"), 0);
    nsCStringKey key(uri);

    MutexAutoLock lock(mLock);    
    nsJAR* zip = static_cast<nsJAR*>(static_cast<nsIZipReader*>(mZips.Get(&key)));
    if (!zip)
      return NS_OK;

#ifdef ZIP_CACHE_HIT_RATE
    mZipCacheFlushes++;
#endif

    zip->SetZipReaderCache(nsnull);

    mZips.Remove(&key);
    NS_RELEASE(zip);
  }
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
