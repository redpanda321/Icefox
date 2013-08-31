/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */ 
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Implementation of nsIFile for "unixy" systems.
 */

#include "mozilla/Util.h"
#include "mozilla/Attributes.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <dirent.h>
#include <ctype.h>
#include <locale.h>
#if defined(VMS)
    #include <fabdef.h>
#endif

#if defined(HAVE_SYS_QUOTA_H) && defined(HAVE_LINUX_QUOTA_H)
#define USE_LINUX_QUOTACTL
#include <sys/quota.h>
#endif

#if (MOZ_PLATFORM_MAEMO == 6)
#include <QUrl>
#include <QString>
#if (MOZ_ENABLE_CONTENTACTION)
#include <contentaction/contentaction.h>
#endif
#endif

#include "xpcom-private.h"
#include "nsDirectoryServiceDefs.h"
#include "nsCRT.h"
#include "nsCOMPtr.h"
#include "nsMemory.h"
#include "nsIFile.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsLocalFile.h"
#include "nsIComponentManager.h"
#include "nsXPIDLString.h"
#include "prproces.h"
#include "nsIDirectoryEnumerator.h"
#include "nsISimpleEnumerator.h"
#include "private/pprio.h"

#ifdef MOZ_WIDGET_GTK
#include "nsIGIOService.h"
#include "nsIGnomeVFSService.h"
#endif

#ifdef MOZ_WIDGET_COCOA
#include <Carbon/Carbon.h>
#include "CocoaFileUtils.h"
#include "prmem.h"
#include "plbase64.h"

static nsresult MacErrorMapper(OSErr inErr);
#endif

#if (MOZ_PLATFORM_MAEMO == 5)
#include <glib.h>
#include <hildon-uri.h>
#include <hildon-mime.h>
#include <libosso.h>
#endif

#ifdef MOZ_WIDGET_ANDROID
#include "AndroidBridge.h"
#include "nsIMIMEService.h"
#include <linux/magic.h>
#endif

#include "nsNativeCharsetUtils.h"
#include "nsTraceRefcntImpl.h"
#include "nsHashKeys.h"

using namespace mozilla;

#define ENSURE_STAT_CACHE()                     \
    PR_BEGIN_MACRO                              \
        if (!FillStatCache())                   \
             return NSRESULT_FOR_ERRNO();       \
    PR_END_MACRO

#define CHECK_mPath()                           \
    PR_BEGIN_MACRO                              \
        if (mPath.IsEmpty())                    \
            return NS_ERROR_NOT_INITIALIZED;    \
    PR_END_MACRO

/* directory enumerator */
class
nsDirEnumeratorUnix MOZ_FINAL : public nsISimpleEnumerator,
                                public nsIDirectoryEnumerator
{
    public:
    nsDirEnumeratorUnix();

    // nsISupports interface
    NS_DECL_ISUPPORTS

    // nsISimpleEnumerator interface
    NS_DECL_NSISIMPLEENUMERATOR

    // nsIDirectoryEnumerator interface
    NS_DECL_NSIDIRECTORYENUMERATOR

    NS_IMETHOD Init(nsLocalFile *parent, bool ignored);

    private:
    ~nsDirEnumeratorUnix();

    protected:
    NS_IMETHOD GetNextEntry();

    DIR           *mDir;
    struct dirent *mEntry;
    nsCString      mParentPath;
};

nsDirEnumeratorUnix::nsDirEnumeratorUnix() :
                         mDir(nullptr), 
                         mEntry(nullptr)
{
}

nsDirEnumeratorUnix::~nsDirEnumeratorUnix()
{
    Close();
}

NS_IMPL_ISUPPORTS2(nsDirEnumeratorUnix, nsISimpleEnumerator, nsIDirectoryEnumerator)

NS_IMETHODIMP
nsDirEnumeratorUnix::Init(nsLocalFile *parent, bool resolveSymlinks /*ignored*/)
{
    nsAutoCString dirPath;
    if (NS_FAILED(parent->GetNativePath(dirPath)) ||
        dirPath.IsEmpty()) {
        return NS_ERROR_FILE_INVALID_PATH;
    }

    if (NS_FAILED(parent->GetNativePath(mParentPath)))
        return NS_ERROR_FAILURE;

    mDir = opendir(dirPath.get());
    if (!mDir)
        return NSRESULT_FOR_ERRNO();
    return GetNextEntry();
}

NS_IMETHODIMP
nsDirEnumeratorUnix::HasMoreElements(bool *result)
{
    *result = mDir && mEntry;
    if (!*result)
        Close();
    return NS_OK;
}

NS_IMETHODIMP
nsDirEnumeratorUnix::GetNext(nsISupports **_retval)
{
    nsCOMPtr<nsIFile> file;
    nsresult rv = GetNextFile(getter_AddRefs(file));
    if (NS_FAILED(rv))
        return rv;
    NS_IF_ADDREF(*_retval = file);
    return NS_OK;
}

NS_IMETHODIMP
nsDirEnumeratorUnix::GetNextEntry()
{
    do {
        errno = 0;
        mEntry = readdir(mDir);

        // end of dir or error
        if (!mEntry)
            return NSRESULT_FOR_ERRNO();

        // keep going past "." and ".."
    } while (mEntry->d_name[0] == '.'     &&
            (mEntry->d_name[1] == '\0'    ||   // .\0
            (mEntry->d_name[1] == '.'     &&
            mEntry->d_name[2] == '\0')));      // ..\0
    return NS_OK;
}

NS_IMETHODIMP
nsDirEnumeratorUnix::GetNextFile(nsIFile **_retval)
{
    nsresult rv;
    if (!mDir || !mEntry) {
        *_retval = nullptr;
        return NS_OK;
    }

    nsCOMPtr<nsIFile> file = new nsLocalFile();
    if (!file)
        return NS_ERROR_OUT_OF_MEMORY;

    if (NS_FAILED(rv = file->InitWithNativePath(mParentPath)) ||
        NS_FAILED(rv = file->AppendNative(nsDependentCString(mEntry->d_name))))
        return rv;

    *_retval = file;
    NS_ADDREF(*_retval);
    return GetNextEntry();
}

NS_IMETHODIMP 
nsDirEnumeratorUnix::Close()
{
    if (mDir) {
        closedir(mDir);
        mDir = nullptr;
    }
    return NS_OK;
}

nsLocalFile::nsLocalFile()
{
}

nsLocalFile::nsLocalFile(const nsLocalFile& other)
  : mPath(other.mPath)
{
}

#ifdef MOZ_WIDGET_COCOA
NS_IMPL_THREADSAFE_ISUPPORTS4(nsLocalFile,
                              nsILocalFileMac,
                              nsILocalFile,
                              nsIFile,
                              nsIHashable)
#else
NS_IMPL_THREADSAFE_ISUPPORTS3(nsLocalFile,
                              nsILocalFile,
                              nsIFile,
                              nsIHashable)
#endif

nsresult
nsLocalFile::nsLocalFileConstructor(nsISupports *outer, 
                                    const nsIID &aIID,
                                    void **aInstancePtr)
{
    NS_ENSURE_ARG_POINTER(aInstancePtr);
    NS_ENSURE_NO_AGGREGATION(outer);

    *aInstancePtr = nullptr;

    nsCOMPtr<nsIFile> inst = new nsLocalFile();
    if (!inst)
        return NS_ERROR_OUT_OF_MEMORY;
    return inst->QueryInterface(aIID, aInstancePtr);
}

bool 
nsLocalFile::FillStatCache() {
    if (STAT(mPath.get(), &mCachedStat) == -1) {
        // try lstat it may be a symlink
        if (LSTAT(mPath.get(), &mCachedStat) == -1) {
            return false;
        }
    }
    return true;
}

NS_IMETHODIMP
nsLocalFile::Clone(nsIFile **file)
{
    // Just copy-construct ourselves
    *file = new nsLocalFile(*this);
    if (!*file)
      return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(*file);
    
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::InitWithNativePath(const nsACString &filePath)
{
    if (filePath.Equals("~") || Substring(filePath, 0, 2).EqualsLiteral("~/")) {
        nsCOMPtr<nsIFile> homeDir;
        nsAutoCString homePath;
        if (NS_FAILED(NS_GetSpecialDirectory(NS_OS_HOME_DIR,
                                             getter_AddRefs(homeDir))) ||
            NS_FAILED(homeDir->GetNativePath(homePath))) {
            return NS_ERROR_FAILURE;
        }
 
        mPath = homePath;
        if (filePath.Length() > 2)
          mPath.Append(Substring(filePath, 1, filePath.Length() - 1));
    } else {
        if (filePath.IsEmpty() || filePath.First() != '/')
            return NS_ERROR_FILE_UNRECOGNIZED_PATH;
        mPath = filePath;
    }

    // trim off trailing slashes
    ssize_t len = mPath.Length();
    while ((len > 1) && (mPath[len - 1] == '/'))
        --len;
    mPath.SetLength(len);

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::CreateAllAncestors(uint32_t permissions)
{
    // <jband> I promise to play nice
    char *buffer = mPath.BeginWriting(),
         *slashp = buffer;

#ifdef DEBUG_NSIFILE
    fprintf(stderr, "nsIFile: before: %s\n", buffer);
#endif

    while ((slashp = strchr(slashp + 1, '/'))) {
        /*
         * Sequences of '/' are equivalent to a single '/'.
         */
        if (slashp[1] == '/')
            continue;

        /*
         * If the path has a trailing slash, don't make the last component,
         * because we'll get EEXIST in Create when we try to build the final
         * component again, and it's easier to condition the logic here than
         * there.
         */
        if (slashp[1] == '\0')
            break;

        /* Temporarily NUL-terminate here */
        *slashp = '\0';
#ifdef DEBUG_NSIFILE
        fprintf(stderr, "nsIFile: mkdir(\"%s\")\n", buffer);
#endif
        int mkdir_result = mkdir(buffer, permissions);
        int mkdir_errno  = errno;
        if (mkdir_result == -1) {
            /*
             * Always set |errno| to EEXIST if the dir already exists
             * (we have to do this here since the errno value is not consistent
             * in all cases - various reasons like different platform,
             * automounter-controlled dir, etc. can affect it (see bug 125489
             * for details)).
             */
            if (access(buffer, F_OK) == 0) {
                mkdir_errno = EEXIST;
            }
        }

        /* Put the / back before we (maybe) return */
        *slashp = '/';

        /*
         * We could get EEXIST for an existing file -- not directory --
         * with the name of one of our ancestors, but that's OK: we'll get
         * ENOTDIR when we try to make the next component in the path,
         * either here on back in Create, and error out appropriately.
         */
        if (mkdir_result == -1 && mkdir_errno != EEXIST)
            return nsresultForErrno(mkdir_errno);
    }

#ifdef DEBUG_NSIFILE
    fprintf(stderr, "nsIFile: after: %s\n", buffer);
#endif

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::OpenNSPRFileDesc(int32_t flags, int32_t mode, PRFileDesc **_retval)
{
    *_retval = PR_Open(mPath.get(), flags, mode);
    if (! *_retval)
        return NS_ErrorAccordingToNSPR();

    if (flags & DELETE_ON_CLOSE) {
        PR_Delete(mPath.get());
    }

#if defined(LINUX) && !defined(ANDROID)
    if (flags & OS_READAHEAD) {
        readahead(PR_FileDesc2NativeHandle(*_retval), 0, 0);
    }
#endif
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::OpenANSIFileDesc(const char *mode, FILE **_retval)
{
    *_retval = fopen(mPath.get(), mode);
    if (! *_retval)
        return NS_ERROR_FAILURE;

    return NS_OK;
}

static int
do_create(const char *path, int flags, mode_t mode, PRFileDesc **_retval)
{
    *_retval = PR_Open(path, flags, mode);
    return *_retval ? 0 : -1;
}

static int
do_mkdir(const char *path, int flags, mode_t mode, PRFileDesc **_retval)
{
    *_retval = nullptr;
    return mkdir(path, mode);
}

nsresult
nsLocalFile::CreateAndKeepOpen(uint32_t type, int flags,
                               uint32_t permissions, PRFileDesc **_retval)
{
    if (type != NORMAL_FILE_TYPE && type != DIRECTORY_TYPE)
        return NS_ERROR_FILE_UNKNOWN_TYPE;

    int result;
    int (*createFunc)(const char *, int, mode_t, PRFileDesc **) =
        (type == NORMAL_FILE_TYPE) ? do_create : do_mkdir;

    result = createFunc(mPath.get(), flags, permissions, _retval);
    if (result == -1 && errno == ENOENT) {
        /*
         * If we failed because of missing ancestor components, try to create
         * them and then retry the original creation.
         *
         * Ancestor directories get the same permissions as the file we're
         * creating, with the X bit set for each of (user,group,other) with
         * an R bit in the original permissions.    If you want to do anything
         * fancy like setgid or sticky bits, do it by hand.
         */
        int dirperm = permissions;
        if (permissions & S_IRUSR)
            dirperm |= S_IXUSR;
        if (permissions & S_IRGRP)
            dirperm |= S_IXGRP;
        if (permissions & S_IROTH)
            dirperm |= S_IXOTH;

#ifdef DEBUG_NSIFILE
        fprintf(stderr, "nsIFile: perm = %o, dirperm = %o\n", permissions,
                dirperm);
#endif

        if (NS_FAILED(CreateAllAncestors(dirperm)))
            return NS_ERROR_FAILURE;

#ifdef DEBUG_NSIFILE
        fprintf(stderr, "nsIFile: Create(\"%s\") again\n", mPath.get());
#endif
        result = createFunc(mPath.get(), flags, permissions, _retval);
    }
    return NSRESULT_FOR_RETURN(result);
}

NS_IMETHODIMP
nsLocalFile::Create(uint32_t type, uint32_t permissions)
{
    PRFileDesc *junk = nullptr;
    nsresult rv = CreateAndKeepOpen(type,
                                    PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE |
                                    PR_EXCL,
                                    permissions,
                                    &junk);
    if (junk)
        PR_Close(junk);
    return rv;
}

NS_IMETHODIMP
nsLocalFile::AppendNative(const nsACString &fragment)
{
    if (fragment.IsEmpty())
        return NS_OK;

    // only one component of path can be appended
    nsACString::const_iterator begin, end;
    if (FindCharInReadable('/', fragment.BeginReading(begin),
                                fragment.EndReading(end)))
        return NS_ERROR_FILE_UNRECOGNIZED_PATH;

    return AppendRelativeNativePath(fragment);
}

NS_IMETHODIMP
nsLocalFile::AppendRelativeNativePath(const nsACString &fragment)
{
    if (fragment.IsEmpty())
        return NS_OK;

    // No leading '/' 
    if (fragment.First() == '/')
        return NS_ERROR_FILE_UNRECOGNIZED_PATH;

    if (mPath.EqualsLiteral("/"))
        mPath.Append(fragment);
    else
        mPath.Append(NS_LITERAL_CSTRING("/") + fragment);

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::Normalize()
{
    char    resolved_path[PATH_MAX] = "";
    char *resolved_path_ptr = nullptr;

    resolved_path_ptr = realpath(mPath.get(), resolved_path);

    // if there is an error, the return is null.
    if (!resolved_path_ptr)
        return NSRESULT_FOR_ERRNO();

    mPath = resolved_path;
    return NS_OK;
}

void
nsLocalFile::LocateNativeLeafName(nsACString::const_iterator &begin, 
                                  nsACString::const_iterator &end)
{
    // XXX perhaps we should cache this??

    mPath.BeginReading(begin);
    mPath.EndReading(end);
    
    nsACString::const_iterator it = end;
    nsACString::const_iterator stop = begin;
    --stop;
    while (--it != stop) {
        if (*it == '/') {
            begin = ++it;
            return;
        }
    }
    // else, the entire path is the leaf name (which means this
    // isn't an absolute path... unexpected??)
}

NS_IMETHODIMP
nsLocalFile::GetNativeLeafName(nsACString &aLeafName)
{
    nsACString::const_iterator begin, end;
    LocateNativeLeafName(begin, end);
    aLeafName = Substring(begin, end);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetNativeLeafName(const nsACString &aLeafName)
{
    nsACString::const_iterator begin, end;
    LocateNativeLeafName(begin, end);
    mPath.Replace(begin.get() - mPath.get(), Distance(begin, end), aLeafName);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetNativePath(nsACString &_retval)
{
    _retval = mPath;
    return NS_OK;
}

nsresult
nsLocalFile::GetNativeTargetPathName(nsIFile *newParent, 
                                     const nsACString &newName,
                                     nsACString &_retval)
{
    nsresult rv;
    nsCOMPtr<nsIFile> oldParent;

    if (!newParent) {
        if (NS_FAILED(rv = GetParent(getter_AddRefs(oldParent))))
            return rv;
        newParent = oldParent.get();
    } else {
        // check to see if our target directory exists
        bool targetExists;
        if (NS_FAILED(rv = newParent->Exists(&targetExists)))
            return rv;

        if (!targetExists) {
            // XXX create the new directory with some permissions
            rv = newParent->Create(DIRECTORY_TYPE, 0755);
            if (NS_FAILED(rv))
                return rv;
        } else {
            // make sure that the target is actually a directory
            bool targetIsDirectory;
            if (NS_FAILED(rv = newParent->IsDirectory(&targetIsDirectory)))
                return rv;
            if (!targetIsDirectory)
                return NS_ERROR_FILE_DESTINATION_NOT_DIR;
        }
    }

    nsACString::const_iterator nameBegin, nameEnd;
    if (!newName.IsEmpty()) {
        newName.BeginReading(nameBegin);
        newName.EndReading(nameEnd);
    }
    else
        LocateNativeLeafName(nameBegin, nameEnd);

    nsAutoCString dirName;
    if (NS_FAILED(rv = newParent->GetNativePath(dirName)))
        return rv;

    _retval = dirName
            + NS_LITERAL_CSTRING("/")
            + Substring(nameBegin, nameEnd);
    return NS_OK;
}

nsresult
nsLocalFile::CopyDirectoryTo(nsIFile *newParent)
{
    nsresult rv;
    /*
     * dirCheck is used for various boolean test results such as from Equals,
     * Exists, isDir, etc.
     */
    bool dirCheck, isSymlink;
    uint32_t oldPerms;

    if (NS_FAILED(rv = IsDirectory(&dirCheck)))
        return rv;
    if (!dirCheck)
        return CopyToNative(newParent, EmptyCString());
    
    if (NS_FAILED(rv = Equals(newParent, &dirCheck)))
        return rv;
    if (dirCheck) { 
        // can't copy dir to itself
        return NS_ERROR_INVALID_ARG;
    }
    
    if (NS_FAILED(rv = newParent->Exists(&dirCheck))) 
        return rv;
    // get the dirs old permissions
    if (NS_FAILED(rv = GetPermissions(&oldPerms)))
        return rv;
    if (!dirCheck) {
        if (NS_FAILED(rv = newParent->Create(DIRECTORY_TYPE, oldPerms)))
            return rv;
    } else {    // dir exists lets try to use leaf
        nsAutoCString leafName;
        if (NS_FAILED(rv = GetNativeLeafName(leafName)))
            return rv;
        if (NS_FAILED(rv = newParent->AppendNative(leafName)))
            return rv;
        if (NS_FAILED(rv = newParent->Exists(&dirCheck)))
            return rv;
        if (dirCheck) 
            return NS_ERROR_FILE_ALREADY_EXISTS; // dest exists
        if (NS_FAILED(rv = newParent->Create(DIRECTORY_TYPE, oldPerms)))
            return rv;
    }

    nsCOMPtr<nsISimpleEnumerator> dirIterator;
    if (NS_FAILED(rv = GetDirectoryEntries(getter_AddRefs(dirIterator))))
        return rv;

    bool hasMore = false;
    while (dirIterator->HasMoreElements(&hasMore), hasMore) {
        nsCOMPtr<nsIFile> entry;
        rv = dirIterator->GetNext((nsISupports**)getter_AddRefs(entry));
        if (NS_FAILED(rv)) 
            continue;
        if (NS_FAILED(rv = entry->IsSymlink(&isSymlink)))
            return rv;
        if (NS_FAILED(rv = entry->IsDirectory(&dirCheck)))
            return rv;
        if (dirCheck && !isSymlink) {
            nsCOMPtr<nsIFile> destClone;
            rv = newParent->Clone(getter_AddRefs(destClone));
            if (NS_SUCCEEDED(rv)) {
                if (NS_FAILED(rv = entry->CopyToNative(destClone, EmptyCString()))) {
#ifdef DEBUG
                    nsresult rv2;
                    nsAutoCString pathName;
                    if (NS_FAILED(rv2 = entry->GetNativePath(pathName)))
                        return rv2;
                    printf("Operation not supported: %s\n", pathName.get());
#endif
                    if (rv == NS_ERROR_OUT_OF_MEMORY) 
                        return rv;
                    continue;
                }
            }
        } else {
            if (NS_FAILED(rv = entry->CopyToNative(newParent, EmptyCString()))) {
#ifdef DEBUG
                nsresult rv2;
                nsAutoCString pathName;
                if (NS_FAILED(rv2 = entry->GetNativePath(pathName)))
                    return rv2;
                printf("Operation not supported: %s\n", pathName.get());
#endif
                if (rv == NS_ERROR_OUT_OF_MEMORY) 
                    return rv;
                continue;
            }
        }
    }
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::CopyToNative(nsIFile *newParent, const nsACString &newName)
{
    nsresult rv;
    // check to make sure that this has been initialized properly
    CHECK_mPath();

    // we copy the parent here so 'newParent' remains immutable
    nsCOMPtr <nsIFile> workParent;
    if (newParent) {
        if (NS_FAILED(rv = newParent->Clone(getter_AddRefs(workParent))))
            return rv;
    } else {
        if (NS_FAILED(rv = GetParent(getter_AddRefs(workParent))))
            return rv;
    }
    
    // check to see if we are a directory or if we are a file
    bool isDirectory;
    if (NS_FAILED(rv = IsDirectory(&isDirectory)))
        return rv;

    nsAutoCString newPathName;
    if (isDirectory) {
        if (!newName.IsEmpty()) {
            if (NS_FAILED(rv = workParent->AppendNative(newName)))
                return rv;
        } else {
            if (NS_FAILED(rv = GetNativeLeafName(newPathName)))
                return rv;
            if (NS_FAILED(rv = workParent->AppendNative(newPathName)))
                return rv;
        }
        if (NS_FAILED(rv = CopyDirectoryTo(workParent)))
            return rv;
    } else {
        rv = GetNativeTargetPathName(workParent, newName, newPathName);
        if (NS_FAILED(rv)) 
            return rv;

#ifdef DEBUG_blizzard
        printf("nsLocalFile::CopyTo() %s -> %s\n", mPath.get(), newPathName.get());
#endif

        // actually create the file.
        nsLocalFile *newFile = new nsLocalFile();
        if (!newFile)
            return NS_ERROR_OUT_OF_MEMORY;

        nsCOMPtr<nsIFile> fileRef(newFile); // release on exit

        rv = newFile->InitWithNativePath(newPathName);
        if (NS_FAILED(rv))
            return rv;

        // get the old permissions
        uint32_t myPerms;
        GetPermissions(&myPerms);

        // Create the new file with the old file's permissions, even if write
        // permission is missing.  We can't create with write permission and
        // then change back to myPerm on all filesystems (FAT on Linux, e.g.).
        // But we can write to a read-only file on all Unix filesystems if we
        // open it successfully for writing.

        PRFileDesc *newFD;
        rv = newFile->CreateAndKeepOpen(NORMAL_FILE_TYPE,
                                        PR_WRONLY|PR_CREATE_FILE|PR_TRUNCATE,
                                        myPerms,
                                        &newFD);
        if (NS_FAILED(rv))
            return rv;

        // open the old file, too
        bool specialFile;
        if (NS_FAILED(rv = IsSpecial(&specialFile))) {
            PR_Close(newFD);
            return rv;
        }
        if (specialFile) {
#ifdef DEBUG
            printf("Operation not supported: %s\n", mPath.get());
#endif
            // make sure to clean up properly
            PR_Close(newFD);
            return NS_OK;
        }
               
        PRFileDesc *oldFD;
        rv = OpenNSPRFileDesc(PR_RDONLY, myPerms, &oldFD);
        if (NS_FAILED(rv)) {
            // make sure to clean up properly
            PR_Close(newFD);
            return rv;
        }

#ifdef DEBUG_blizzard
        int32_t totalRead = 0;
        int32_t totalWritten = 0;
#endif
        char buf[BUFSIZ];
        int32_t bytesRead;
        
        while ((bytesRead = PR_Read(oldFD, buf, BUFSIZ)) > 0) {
#ifdef DEBUG_blizzard
            totalRead += bytesRead;
#endif

            // PR_Write promises never to do a short write
            int32_t bytesWritten = PR_Write(newFD, buf, bytesRead);
            if (bytesWritten < 0) {
                bytesRead = -1;
                break;
            }
            NS_ASSERTION(bytesWritten == bytesRead, "short PR_Write?");

#ifdef DEBUG_blizzard
            totalWritten += bytesWritten;
#endif
        }

#ifdef DEBUG_blizzard
        printf("read %d bytes, wrote %d bytes\n",
                 totalRead, totalWritten);
#endif

        // close the files
        PR_Close(newFD);
        PR_Close(oldFD);

        // check for read (or write) error after cleaning up
        if (bytesRead < 0) 
            return NS_ERROR_OUT_OF_MEMORY;
    }
    return rv;
}

NS_IMETHODIMP
nsLocalFile::CopyToFollowingLinksNative(nsIFile *newParent, const nsACString &newName)
{
    return CopyToNative(newParent, newName);
}

NS_IMETHODIMP
nsLocalFile::MoveToNative(nsIFile *newParent, const nsACString &newName)
{
    nsresult rv;

    // check to make sure that this has been initialized properly
    CHECK_mPath();

    // check to make sure that we have a new parent
    nsAutoCString newPathName;
    rv = GetNativeTargetPathName(newParent, newName, newPathName);
    if (NS_FAILED(rv))
        return rv;

    // try for atomic rename, falling back to copy/delete
    if (rename(mPath.get(), newPathName.get()) < 0) {
#ifdef VMS
        if (errno == EXDEV || errno == ENXIO) {
#else
        if (errno == EXDEV) {
#endif
            rv = CopyToNative(newParent, newName);
            if (NS_SUCCEEDED(rv))
                rv = Remove(true);
        } else {
            rv = NSRESULT_FOR_ERRNO();
        }
    }

    if (NS_SUCCEEDED(rv)) {
        // Adjust this
        mPath = newPathName;
    }
    return rv;
}

NS_IMETHODIMP
nsLocalFile::Remove(bool recursive)
{
    CHECK_mPath();
    ENSURE_STAT_CACHE();

    bool isSymLink;

    nsresult rv = IsSymlink(&isSymLink);
    if (NS_FAILED(rv))
        return rv;

    if (isSymLink || !S_ISDIR(mCachedStat.st_mode))
        return NSRESULT_FOR_RETURN(unlink(mPath.get()));

    if (recursive) {
        nsDirEnumeratorUnix *dir = new nsDirEnumeratorUnix();
        if (!dir)
            return NS_ERROR_OUT_OF_MEMORY;

        nsCOMPtr<nsISimpleEnumerator> dirRef(dir); // release on exit

        rv = dir->Init(this, false);
        if (NS_FAILED(rv))
            return rv;

        bool more;
        while (dir->HasMoreElements(&more), more) {
            nsCOMPtr<nsISupports> item;
            rv = dir->GetNext(getter_AddRefs(item));
            if (NS_FAILED(rv))
                return NS_ERROR_FAILURE;

            nsCOMPtr<nsIFile> file = do_QueryInterface(item, &rv);
            if (NS_FAILED(rv))
                return NS_ERROR_FAILURE;
            rv = file->Remove(recursive);

#ifdef ANDROID
            // See bug 580434 - Bionic gives us just deleted files
            if (rv == NS_ERROR_FILE_TARGET_DOES_NOT_EXIST)
                continue;
#endif
            if (NS_FAILED(rv))
                return rv;
        }
    }

    return NSRESULT_FOR_RETURN(rmdir(mPath.get()));
}

NS_IMETHODIMP
nsLocalFile::GetLastModifiedTime(PRTime *aLastModTime)
{
    CHECK_mPath();
    NS_ENSURE_ARG(aLastModTime);

    PRFileInfo64 info;
    if (PR_GetFileInfo64(mPath.get(), &info) != PR_SUCCESS)
        return NSRESULT_FOR_ERRNO();
    PRTime modTime = info.modifyTime;
    if (modTime == 0)
        *aLastModTime = 0;
    else
        *aLastModTime = modTime / PR_USEC_PER_MSEC;

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetLastModifiedTime(PRTime aLastModTime)
{
    CHECK_mPath();

    int result;
    if (aLastModTime != 0) {
        ENSURE_STAT_CACHE();
        struct utimbuf ut;
        ut.actime = mCachedStat.st_atime;

        // convert milliseconds to seconds since the unix epoch
        ut.modtime = (time_t)(aLastModTime / PR_MSEC_PER_SEC);
        result = utime(mPath.get(), &ut);
    } else {
        result = utime(mPath.get(), nullptr);
    }
    return NSRESULT_FOR_RETURN(result);
}

NS_IMETHODIMP
nsLocalFile::GetLastModifiedTimeOfLink(PRTime *aLastModTimeOfLink)
{
    CHECK_mPath();
    NS_ENSURE_ARG(aLastModTimeOfLink);

    struct STAT sbuf;
    if (LSTAT(mPath.get(), &sbuf) == -1)
        return NSRESULT_FOR_ERRNO();
    *aLastModTimeOfLink = PRTime(sbuf.st_mtime) * PR_MSEC_PER_SEC;

    return NS_OK;
}

/*
 * utime(2) may or may not dereference symlinks, joy.
 */
NS_IMETHODIMP
nsLocalFile::SetLastModifiedTimeOfLink(PRTime aLastModTimeOfLink)
{
    return SetLastModifiedTime(aLastModTimeOfLink);
}

/*
 * Only send back permissions bits: maybe we want to send back the whole
 * mode_t to permit checks against other file types?
 */

#define NORMALIZE_PERMS(mode)    ((mode)& (S_IRWXU | S_IRWXG | S_IRWXO))

NS_IMETHODIMP
nsLocalFile::GetPermissions(uint32_t *aPermissions)
{
    NS_ENSURE_ARG(aPermissions);
    ENSURE_STAT_CACHE();
    *aPermissions = NORMALIZE_PERMS(mCachedStat.st_mode);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetPermissionsOfLink(uint32_t *aPermissionsOfLink)
{
    CHECK_mPath();
    NS_ENSURE_ARG(aPermissionsOfLink);

    struct STAT sbuf;
    if (LSTAT(mPath.get(), &sbuf) == -1)
        return NSRESULT_FOR_ERRNO();
    *aPermissionsOfLink = NORMALIZE_PERMS(sbuf.st_mode);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetPermissions(uint32_t aPermissions)
{
    CHECK_mPath();

    /*
     * Race condition here: we should use fchmod instead, there's no way to 
     * guarantee the name still refers to the same file.
     */
    if (chmod(mPath.get(), aPermissions) >= 0)
        return NS_OK;
#if defined(ANDROID) && defined(STATFS)
    // For the time being, this is restricted for use by Android, but we 
    // will figure out what to do for all platforms in bug 638503
    struct STATFS sfs;
    if (STATFS(mPath.get(), &sfs) < 0)
         return NSRESULT_FOR_ERRNO();

    // if this is a FAT file system we can't set file permissions
    if (sfs.f_type == MSDOS_SUPER_MAGIC )
        return NS_OK;
#endif
    return NSRESULT_FOR_ERRNO();
}

NS_IMETHODIMP
nsLocalFile::SetPermissionsOfLink(uint32_t aPermissions)
{
    // There isn't a consistent mechanism for doing this on UNIX platforms. We
    // might want to carefully implement this in the future though.
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsLocalFile::GetFileSize(int64_t *aFileSize)
{
    NS_ENSURE_ARG_POINTER(aFileSize);
    *aFileSize = 0;
    ENSURE_STAT_CACHE();

#if defined(VMS)
    /* Only two record formats can report correct file content size */
    if ((mCachedStat.st_fab_rfm != FAB$C_STMLF) &&
        (mCachedStat.st_fab_rfm != FAB$C_STMCR)) {
        return NS_ERROR_FAILURE;
    }
#endif

    if (!S_ISDIR(mCachedStat.st_mode)) {
        *aFileSize = (int64_t)mCachedStat.st_size;
    }
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetFileSize(int64_t aFileSize)
{
    CHECK_mPath();

#if defined(ANDROID)
    /* no truncate on bionic */
    int fd = open(mPath.get(), O_WRONLY);
    if (fd == -1)
        return NSRESULT_FOR_ERRNO();

    int ret = ftruncate(fd, (off_t)aFileSize);
    close(fd);

    if (ret == -1)
        return NSRESULT_FOR_ERRNO();
#elif defined(HAVE_TRUNCATE64)
    if (truncate64(mPath.get(), (off64_t)aFileSize) == -1)
        return NSRESULT_FOR_ERRNO();
#else
    off_t size = (off_t)aFileSize;
    if (truncate(mPath.get(), size) == -1)
        return NSRESULT_FOR_ERRNO();
#endif
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetFileSizeOfLink(int64_t *aFileSize)
{
    CHECK_mPath();
    NS_ENSURE_ARG(aFileSize);

    struct STAT sbuf;
    if (LSTAT(mPath.get(), &sbuf) == -1)
        return NSRESULT_FOR_ERRNO();

    *aFileSize = (int64_t)sbuf.st_size;
    return NS_OK;
}

#if defined(USE_LINUX_QUOTACTL)
/*
 * Searches /proc/self/mountinfo for given device (Major:Minor), 
 * returns exported name from /dev
 *
 * Fails when /proc/self/mountinfo or diven device don't exist.
 */
static bool
GetDeviceName(int deviceMajor, int deviceMinor, nsACString &deviceName)
{
    bool ret = false;
    
    const int kMountInfoLineLength = 200;
    const int kMountInfoDevPosition = 6;

    char mountinfo_line[kMountInfoLineLength];
    char device_num[kMountInfoLineLength];
    
    snprintf(device_num,kMountInfoLineLength,"%d:%d", deviceMajor, deviceMinor);
    
    FILE *f = fopen("/proc/self/mountinfo","rt");
    if(!f)
        return ret;

    // Expects /proc/self/mountinfo in format:
    // 'ID ID major:minor root mountpoint flags - type devicename flags'
    while(fgets(mountinfo_line,kMountInfoLineLength,f)) {
        char *p_dev = strstr(mountinfo_line,device_num);
    
        int i;
        for(i = 0; i < kMountInfoDevPosition && p_dev != NULL; i++) {
            p_dev = strchr(p_dev,' ');
            if(p_dev)
              p_dev++;
        }
    
        if(p_dev) {
            char *p_dev_end = strchr(p_dev,' ');
            if(p_dev_end) {
                *p_dev_end = '\0';
                deviceName.Assign(p_dev);
                ret = true;
                break;
            }
        }
    }
    
    fclose(f);
    return ret; 
}
#endif

NS_IMETHODIMP
nsLocalFile::GetDiskSpaceAvailable(int64_t *aDiskSpaceAvailable)
{
    NS_ENSURE_ARG_POINTER(aDiskSpaceAvailable);

    // These systems have the operations necessary to check disk space.

#ifdef STATFS

    // check to make sure that mPath is properly initialized
    CHECK_mPath();

    struct STATFS fs_buf;

    /* 
     * Members of the STATFS struct that you should know about:
     * F_BSIZE = block size on disk.
     * f_bavail = number of free blocks available to a non-superuser.
     * f_bfree = number of total free blocks in file system.
     */

    if (STATFS(mPath.get(), &fs_buf) < 0) {
        // The call to STATFS failed.
#ifdef DEBUG
        printf("ERROR: GetDiskSpaceAvailable: STATFS call FAILED. \n");
#endif
        return NS_ERROR_FAILURE;
    }

    *aDiskSpaceAvailable = (int64_t) fs_buf.F_BSIZE * fs_buf.f_bavail;

#ifdef DEBUG_DISK_SPACE
    printf("DiskSpaceAvailable: %lu bytes\n",
         *aDiskSpaceAvailable);
#endif

#if defined(USE_LINUX_QUOTACTL)

    if(!FillStatCache()) {
        // Return available size from statfs
        return NS_OK;
    }

    nsCString deviceName;
    if(!GetDeviceName(major(mCachedStat.st_dev), minor(mCachedStat.st_dev), deviceName)) {
        return NS_OK;
    }

    struct dqblk dq;
    if(!quotactl(QCMD(Q_GETQUOTA, USRQUOTA), deviceName.get(), getuid(), (caddr_t)&dq)
#ifdef QIF_BLIMITS
       && dq.dqb_valid & QIF_BLIMITS
#endif
       && dq.dqb_bhardlimit)
    {
        int64_t QuotaSpaceAvailable = 0;
        if (dq.dqb_bhardlimit > dq.dqb_curspace)
            QuotaSpaceAvailable = int64_t(fs_buf.F_BSIZE * (dq.dqb_bhardlimit - dq.dqb_curspace));
        if(QuotaSpaceAvailable < *aDiskSpaceAvailable) {
            *aDiskSpaceAvailable = QuotaSpaceAvailable;
        }
    }
#endif

    return NS_OK;

#else
    /*
     * This platform doesn't have statfs or statvfs.  I'm sure that there's
     * a way to check for free disk space on platforms that don't have statfs
     * (I'm SURE they have df, for example).
     *
     * Until we figure out how to do that, lets be honest and say that this
     * command isn't implemented properly for these platforms yet.
     */
#ifdef DEBUG
    printf("ERROR: GetDiskSpaceAvailable: Not implemented for plaforms without statfs.\n");
#endif
    return NS_ERROR_NOT_IMPLEMENTED;

#endif /* STATFS */

}

NS_IMETHODIMP
nsLocalFile::GetParent(nsIFile **aParent)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(aParent);
    *aParent = nullptr;

    // if '/' we are at the top of the volume, return null
    if (mPath.Equals("/"))
        return  NS_OK;
 
    // <brendan, after jband> I promise to play nice
    char *buffer   = mPath.BeginWriting(),
         *slashp   = buffer;

    // find the last significant slash in buffer
    slashp = strrchr(buffer, '/');
    NS_ASSERTION(slashp, "non-canonical path?");
    if (!slashp)
        return NS_ERROR_FILE_INVALID_PATH;

    // for the case where we are at '/'
    if (slashp == buffer)
        slashp++;

    // temporarily terminate buffer at the last significant slash
    char c = *slashp;
    *slashp = '\0';

    nsCOMPtr<nsIFile> localFile;
    nsresult rv = NS_NewNativeLocalFile(nsDependentCString(buffer), true,
                                        getter_AddRefs(localFile));

    // make buffer whole again
    *slashp = c;

    if (NS_SUCCEEDED(rv) && localFile)
        rv = CallQueryInterface(localFile, aParent);
    return rv;
}

/*
 * The results of Exists, isWritable and isReadable are not cached.
 */


NS_IMETHODIMP
nsLocalFile::Exists(bool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);

    *_retval = (access(mPath.get(), F_OK) == 0);
    return NS_OK;
}


NS_IMETHODIMP
nsLocalFile::IsWritable(bool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);

    *_retval = (access(mPath.get(), W_OK) == 0);
    if (*_retval || errno == EACCES)
        return NS_OK;
    return NSRESULT_FOR_ERRNO();
}

NS_IMETHODIMP
nsLocalFile::IsReadable(bool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);

    *_retval = (access(mPath.get(), R_OK) == 0);
    if (*_retval || errno == EACCES)
        return NS_OK;
    return NSRESULT_FOR_ERRNO();
}

NS_IMETHODIMP
nsLocalFile::IsExecutable(bool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);

    // Check extension (bug 663899). On certain platforms, the file
    // extension may cause the OS to treat it as executable regardless of
    // the execute bit, such as .jar on Mac OS X. We borrow the code from
    // nsLocalFileWin, slightly modified.

    // Don't be fooled by symlinks.
    bool symLink;
    nsresult rv = IsSymlink(&symLink);
    if (NS_FAILED(rv))
        return rv;

    nsAutoString path;
    if (symLink)
        GetTarget(path);
    else
        GetPath(path);

    int32_t dotIdx = path.RFindChar(PRUnichar('.'));
    if (dotIdx != kNotFound) {
        // Convert extension to lower case.
        PRUnichar *p = path.BeginWriting();
        for(p += dotIdx + 1; *p; p++)
            *p +=  (*p >= L'A' && *p <= L'Z') ? 'a' - 'A' : 0; 
        
        // Search for any of the set of executable extensions.
        static const char * const executableExts[] = {
            "air",         // Adobe AIR installer
            "jar"};        // java application bundle
        nsDependentSubstring ext = Substring(path, dotIdx + 1);
        for (size_t i = 0; i < ArrayLength(executableExts); i++) {
            if (ext.EqualsASCII(executableExts[i])) {
                // Found a match.  Set result and quit.
                *_retval = true;
                return NS_OK;
            }
        }
    }

    // On OS X, then query Launch Services.
#ifdef MOZ_WIDGET_COCOA
    // Certain Mac applications, such as Classic applications, which
    // run under Rosetta, might not have the +x mode bit but are still
    // considered to be executable by Launch Services (bug 646748).
    CFURLRef url;
    if (NS_FAILED(GetCFURL(&url))) {
        return NS_ERROR_FAILURE;
    }

    LSRequestedInfo theInfoRequest = kLSRequestAllInfo;
    LSItemInfoRecord theInfo;
    OSStatus result = ::LSCopyItemInfoForURL(url, theInfoRequest, &theInfo);
    ::CFRelease(url);
    if (result == noErr) {
        if ((theInfo.flags & kLSItemInfoIsApplication) != 0) {
            *_retval = true;
            return NS_OK;
        }
    }
#endif

    // Then check the execute bit.
    *_retval = (access(mPath.get(), X_OK) == 0);
#ifdef SOLARIS
    // On Solaris, access will always return 0 for root user, however
    // the file is only executable if S_IXUSR | S_IXGRP | S_IXOTH is set.
    // See bug 351950, https://bugzilla.mozilla.org/show_bug.cgi?id=351950
    if (*_retval) {
        struct STAT buf;

        *_retval = (STAT(mPath.get(), &buf) == 0);
        if (*_retval || errno == EACCES) {
            *_retval = *_retval &&
                       (buf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH ));
            return NS_OK;
        }

        return NSRESULT_FOR_ERRNO();
    }
#endif
    if (*_retval || errno == EACCES)
        return NS_OK;
    return NSRESULT_FOR_ERRNO();
}

NS_IMETHODIMP
nsLocalFile::IsDirectory(bool *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
    *_retval = false;
    ENSURE_STAT_CACHE();
    *_retval = S_ISDIR(mCachedStat.st_mode);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::IsFile(bool *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
    *_retval = false;
    ENSURE_STAT_CACHE();
    *_retval = S_ISREG(mCachedStat.st_mode);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::IsHidden(bool *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
    nsACString::const_iterator begin, end;
    LocateNativeLeafName(begin, end);
    *_retval = (*begin == '.');
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::IsSymlink(bool *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
    CHECK_mPath();

    struct STAT symStat;
    if (LSTAT(mPath.get(), &symStat) == -1)
        return NSRESULT_FOR_ERRNO();
    *_retval=S_ISLNK(symStat.st_mode);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::IsSpecial(bool *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
    ENSURE_STAT_CACHE();
    *_retval = S_ISCHR(mCachedStat.st_mode)      ||
                 S_ISBLK(mCachedStat.st_mode)    ||
#ifdef S_ISSOCK
                 S_ISSOCK(mCachedStat.st_mode)   ||
#endif
                 S_ISFIFO(mCachedStat.st_mode);

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::Equals(nsIFile *inFile, bool *_retval)
{
    NS_ENSURE_ARG(inFile);
    NS_ENSURE_ARG_POINTER(_retval);
    *_retval = false;

    nsAutoCString inPath;
    nsresult rv = inFile->GetNativePath(inPath);
    if (NS_FAILED(rv))
        return rv;

    // We don't need to worry about "/foo/" vs. "/foo" here
    // because trailing slashes are stripped on init.
    *_retval = !strcmp(inPath.get(), mPath.get());
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::Contains(nsIFile *inFile, bool recur, bool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG(inFile);
    NS_ENSURE_ARG_POINTER(_retval);

    nsAutoCString inPath;
    nsresult rv;

    if (NS_FAILED(rv = inFile->GetNativePath(inPath)))
        return rv;

    *_retval = false;

    ssize_t len = mPath.Length();
    if (strncmp(mPath.get(), inPath.get(), len) == 0) {
        // Now make sure that the |inFile|'s path has a separator at len,
        // which implies that it has more components after len.
        if (inPath[len] == '/')
            *_retval = true;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetNativeTarget(nsACString &_retval)
{
    CHECK_mPath();
    _retval.Truncate();

    struct STAT symStat;
    if (LSTAT(mPath.get(), &symStat) == -1)
        return NSRESULT_FOR_ERRNO();

    if (!S_ISLNK(symStat.st_mode))
        return NS_ERROR_FILE_INVALID_PATH;

    int32_t size = (int32_t)symStat.st_size;
    char *target = (char *)nsMemory::Alloc(size + 1);
    if (!target)
        return NS_ERROR_OUT_OF_MEMORY;

    if (readlink(mPath.get(), target, (size_t)size) < 0) {
        nsMemory::Free(target);
        return NSRESULT_FOR_ERRNO();
    }
    target[size] = '\0';

    nsresult rv = NS_OK;
    nsCOMPtr<nsIFile> self(this);
    int32_t maxLinks = 40;
    while (true) {
        if (maxLinks-- == 0) {
            rv = NS_ERROR_FILE_UNRESOLVABLE_SYMLINK;
            break;
        }

        if (target[0] != '/') {
            nsCOMPtr<nsIFile> parent;
            if (NS_FAILED(rv = self->GetParent(getter_AddRefs(parent))))
                break;
            if (NS_FAILED(rv = parent->AppendRelativeNativePath(nsDependentCString(target))))
                break;
            if (NS_FAILED(rv = parent->GetNativePath(_retval)))
                break;
            self = parent;
        } else {
            _retval = target;
        }

        const nsPromiseFlatCString &flatRetval = PromiseFlatCString(_retval);

        // Any failure in testing the current target we'll just interpret
        // as having reached our destiny.
        if (LSTAT(flatRetval.get(), &symStat) == -1)
            break;

        // And of course we're done if it isn't a symlink.
        if (!S_ISLNK(symStat.st_mode))
            break;

        int32_t newSize = (int32_t)symStat.st_size;
        if (newSize > size) {
            char *newTarget = (char *)nsMemory::Realloc(target, newSize + 1);
            if (!newTarget) {
                rv = NS_ERROR_OUT_OF_MEMORY;
                break;
            }
            target = newTarget;
            size = newSize;
        }

        int32_t linkLen = readlink(flatRetval.get(), target, size);
        if (linkLen == -1) {
            rv = NSRESULT_FOR_ERRNO();
            break;
        }
        target[linkLen] = '\0';
    }

    nsMemory::Free(target);

    if (NS_FAILED(rv))
        _retval.Truncate();
    return rv;
}

NS_IMETHODIMP
nsLocalFile::GetFollowLinks(bool *aFollowLinks)
{
    *aFollowLinks = true;
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetFollowLinks(bool aFollowLinks)
{
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetDirectoryEntries(nsISimpleEnumerator **entries)
{
    nsDirEnumeratorUnix *dir = new nsDirEnumeratorUnix();
    if (!dir)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(dir);
    nsresult rv = dir->Init(this, false);
    if (NS_FAILED(rv)) {
        *entries = nullptr;
        NS_RELEASE(dir);
    } else {
        *entries = dir; // transfer reference
    }

    return rv;
}

NS_IMETHODIMP
nsLocalFile::Load(PRLibrary **_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);

#ifdef NS_BUILD_REFCNT_LOGGING
    nsTraceRefcntImpl::SetActivityIsLegal(false);
#endif

    *_retval = PR_LoadLibrary(mPath.get());

#ifdef NS_BUILD_REFCNT_LOGGING
    nsTraceRefcntImpl::SetActivityIsLegal(true);
#endif

    if (!*_retval)
        return NS_ERROR_FAILURE;
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetPersistentDescriptor(nsACString &aPersistentDescriptor)
{
    return GetNativePath(aPersistentDescriptor);
}

NS_IMETHODIMP
nsLocalFile::SetPersistentDescriptor(const nsACString &aPersistentDescriptor)
{
#ifdef MOZ_WIDGET_COCOA
    if (aPersistentDescriptor.IsEmpty())
        return NS_ERROR_INVALID_ARG;

    // Support pathnames as user-supplied descriptors if they begin with '/'
    // or '~'.  These characters do not collide with the base64 set used for
    // encoding alias records.
    char first = aPersistentDescriptor.First();
    if (first == '/' || first == '~')
        return InitWithNativePath(aPersistentDescriptor);

    uint32_t dataSize = aPersistentDescriptor.Length();    
    char* decodedData = PL_Base64Decode(PromiseFlatCString(aPersistentDescriptor).get(), dataSize, nullptr);
    if (!decodedData) {
        NS_ERROR("SetPersistentDescriptor was given bad data");
        return NS_ERROR_FAILURE;
    }

    // Cast to an alias record and resolve.
    AliasRecord aliasHeader = *(AliasPtr)decodedData;
    int32_t aliasSize = ::GetAliasSizeFromPtr(&aliasHeader);
    if (aliasSize > ((int32_t)dataSize * 3) / 4) { // be paranoid about having too few data
        PR_Free(decodedData);
        return NS_ERROR_FAILURE;
    }

    nsresult rv = NS_OK;

    // Move the now-decoded data into the Handle.
    // The size of the decoded data is 3/4 the size of the encoded data. See plbase64.h
    Handle  newHandle = nullptr;
    if (::PtrToHand(decodedData, &newHandle, aliasSize) != noErr)
        rv = NS_ERROR_OUT_OF_MEMORY;
    PR_Free(decodedData);
    if (NS_FAILED(rv))
        return rv;

    Boolean changed;
    FSRef resolvedFSRef;
    OSErr err = ::FSResolveAlias(nullptr, (AliasHandle)newHandle, &resolvedFSRef, &changed);

    rv = MacErrorMapper(err);
    DisposeHandle(newHandle);
    if (NS_FAILED(rv))
        return rv;

    return InitWithFSRef(&resolvedFSRef);  
#else
    return InitWithNativePath(aPersistentDescriptor);
#endif
}

NS_IMETHODIMP
nsLocalFile::Reveal()
{
#ifdef MOZ_WIDGET_GTK
    nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
    nsCOMPtr<nsIGnomeVFSService> gnomevfs = do_GetService(NS_GNOMEVFSSERVICE_CONTRACTID);
    if (!giovfs && !gnomevfs)
        return NS_ERROR_FAILURE;

    bool isDirectory;
    if (NS_FAILED(IsDirectory(&isDirectory)))
        return NS_ERROR_FAILURE;

    if (isDirectory) {
        if (giovfs)
            return giovfs->ShowURIForInput(mPath);
        else 
            /* Fallback to GnomeVFS */
            return gnomevfs->ShowURIForInput(mPath);
    } else {
        nsCOMPtr<nsIFile> parentDir;
        nsAutoCString dirPath;
        if (NS_FAILED(GetParent(getter_AddRefs(parentDir))))
            return NS_ERROR_FAILURE;
        if (NS_FAILED(parentDir->GetNativePath(dirPath)))
            return NS_ERROR_FAILURE;

        if (giovfs)
            return giovfs->ShowURIForInput(dirPath);
        else 
            return gnomevfs->ShowURIForInput(dirPath);        
    }
#elif defined(MOZ_WIDGET_COCOA)
    CFURLRef url;
    if (NS_SUCCEEDED(GetCFURL(&url))) {
      nsresult rv = CocoaFileUtils::RevealFileInFinder(url);
      ::CFRelease(url);
      return rv;
    }
    return NS_ERROR_FAILURE;
#else
    return NS_ERROR_FAILURE;
#endif
}

NS_IMETHODIMP
nsLocalFile::Launch()
{
#ifdef MOZ_WIDGET_GTK
#if (MOZ_PLATFORM_MAEMO==5)
    const int32_t kHILDON_SUCCESS = 1;
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
      dbus_error_free(&err);
      return NS_ERROR_FAILURE;
    }

    if (nullptr == connection)
      return NS_ERROR_FAILURE;

    if (hildon_mime_open_file(connection, mPath.get()) != kHILDON_SUCCESS)
      return NS_ERROR_FAILURE;
    return NS_OK;
#else
    nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
    nsCOMPtr<nsIGnomeVFSService> gnomevfs = do_GetService(NS_GNOMEVFSSERVICE_CONTRACTID);
    if (giovfs) {
      return giovfs->ShowURIForInput(mPath);
    } else if (gnomevfs) {
      /* GnomeVFS fallback */
      return gnomevfs->ShowURIForInput(mPath);
    }
    
    return NS_ERROR_FAILURE;
#endif
#elif defined(MOZ_ENABLE_CONTENTACTION)
    QUrl uri = QUrl::fromLocalFile(QString::fromUtf8(mPath.get()));
    ContentAction::Action action =
      ContentAction::Action::defaultActionForFile(uri);

    if (action.isValid()) {
      action.trigger();
      return NS_OK;
    }

    return NS_ERROR_FAILURE;
#elif defined(MOZ_WIDGET_ANDROID)
    // Try to get a mimetype, if this fails just use the file uri alone
    nsresult rv;
    nsAutoCString type;
    nsCOMPtr<nsIMIMEService> mimeService(do_GetService("@mozilla.org/mime;1", &rv));
    if (NS_SUCCEEDED(rv))
        rv = mimeService->GetTypeFromFile(this, type);

    nsDependentCString fileUri = NS_LITERAL_CSTRING("file://");
    fileUri.Append(mPath);
    mozilla::AndroidBridge* bridge = mozilla::AndroidBridge::Bridge();
    return bridge->OpenUriExternal(fileUri, type) ? NS_OK : NS_ERROR_FAILURE;
#elif defined(MOZ_WIDGET_COCOA)
    CFURLRef url;
    if (NS_SUCCEEDED(GetCFURL(&url))) {
        nsresult rv = CocoaFileUtils::OpenURL(url);
        ::CFRelease(url);
        return rv;
    }
    return NS_ERROR_FAILURE;
#else
    return NS_ERROR_FAILURE;
#endif
}

nsresult
NS_NewNativeLocalFile(const nsACString &path, bool followSymlinks, nsIFile **result)
{
    nsLocalFile *file = new nsLocalFile();
    if (!file)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(file);

    file->SetFollowLinks(followSymlinks);

    if (!path.IsEmpty()) {
        nsresult rv = file->InitWithNativePath(path);
        if (NS_FAILED(rv)) {
            NS_RELEASE(file);
            return rv;
        }
    }
    *result = file;
    return NS_OK;
}

//-----------------------------------------------------------------------------
// unicode support
//-----------------------------------------------------------------------------

#define SET_UCS(func, ucsArg) \
    { \
        nsAutoCString buf; \
        nsresult rv = NS_CopyUnicodeToNative(ucsArg, buf); \
        if (NS_FAILED(rv)) \
            return rv; \
        return (func)(buf); \
    }

#define GET_UCS(func, ucsArg) \
    { \
        nsAutoCString buf; \
        nsresult rv = (func)(buf); \
        if (NS_FAILED(rv)) return rv; \
        return NS_CopyNativeToUnicode(buf, ucsArg); \
    }

#define SET_UCS_2ARGS_2(func, opaqueArg, ucsArg) \
    { \
        nsAutoCString buf; \
        nsresult rv = NS_CopyUnicodeToNative(ucsArg, buf); \
        if (NS_FAILED(rv)) \
            return rv; \
        return (func)(opaqueArg, buf); \
    }

// Unicode interface Wrapper
nsresult  
nsLocalFile::InitWithPath(const nsAString &filePath)
{
    SET_UCS(InitWithNativePath, filePath);
}
nsresult  
nsLocalFile::Append(const nsAString &node)
{
    SET_UCS(AppendNative, node);
}
nsresult  
nsLocalFile::AppendRelativePath(const nsAString &node)
{
    SET_UCS(AppendRelativeNativePath, node);
}
nsresult  
nsLocalFile::GetLeafName(nsAString &aLeafName)
{
    GET_UCS(GetNativeLeafName, aLeafName);
}
nsresult  
nsLocalFile::SetLeafName(const nsAString &aLeafName)
{
    SET_UCS(SetNativeLeafName, aLeafName);
}
nsresult  
nsLocalFile::GetPath(nsAString &_retval)
{
    return NS_CopyNativeToUnicode(mPath, _retval);
}
nsresult  
nsLocalFile::CopyTo(nsIFile *newParentDir, const nsAString &newName)
{
    SET_UCS_2ARGS_2(CopyToNative , newParentDir, newName);
}
nsresult  
nsLocalFile::CopyToFollowingLinks(nsIFile *newParentDir, const nsAString &newName)
{
    SET_UCS_2ARGS_2(CopyToFollowingLinksNative , newParentDir, newName);
}
nsresult  
nsLocalFile::MoveTo(nsIFile *newParentDir, const nsAString &newName)
{
    SET_UCS_2ARGS_2(MoveToNative, newParentDir, newName);
}
nsresult
nsLocalFile::GetTarget(nsAString &_retval)
{   
    GET_UCS(GetNativeTarget, _retval);
}

// nsIHashable

NS_IMETHODIMP
nsLocalFile::Equals(nsIHashable* aOther, bool *aResult)
{
    nsCOMPtr<nsIFile> otherFile(do_QueryInterface(aOther));
    if (!otherFile) {
        *aResult = false;
        return NS_OK;
    }

    return Equals(otherFile, aResult);
}

NS_IMETHODIMP
nsLocalFile::GetHashCode(uint32_t *aResult)
{
    *aResult = HashString(mPath);
    return NS_OK;
}

nsresult 
NS_NewLocalFile(const nsAString &path, bool followLinks, nsIFile* *result)
{
    nsAutoCString buf;
    nsresult rv = NS_CopyUnicodeToNative(path, buf);
    if (NS_FAILED(rv))
        return rv;
    return NS_NewNativeLocalFile(buf, followLinks, result);
}

//-----------------------------------------------------------------------------
// global init/shutdown
//-----------------------------------------------------------------------------

void
nsLocalFile::GlobalInit()
{
}

void
nsLocalFile::GlobalShutdown()
{
}

// nsILocalFileMac

#ifdef MOZ_WIDGET_COCOA

static nsresult MacErrorMapper(OSErr inErr)
{
  nsresult outErr;

  switch (inErr)
  {
    case noErr:
      outErr = NS_OK;
      break;

    case fnfErr:
    case afpObjectNotFound:
    case afpDirNotFound:
      outErr = NS_ERROR_FILE_NOT_FOUND;
      break;

    case dupFNErr:
    case afpObjectExists:
      outErr = NS_ERROR_FILE_ALREADY_EXISTS;
      break;

    case dskFulErr:
    case afpDiskFull:
      outErr = NS_ERROR_FILE_DISK_FULL;
      break;

    case fLckdErr:
    case afpVolLocked:
      outErr = NS_ERROR_FILE_IS_LOCKED;
      break;

    case afpAccessDenied:
      outErr = NS_ERROR_FILE_ACCESS_DENIED;
      break;

    case afpDirNotEmpty:
      outErr = NS_ERROR_FILE_DIR_NOT_EMPTY;
      break;

    // Can't find good map for some
    case bdNamErr:
      outErr = NS_ERROR_FAILURE;
      break;

    default:
      outErr = NS_ERROR_FAILURE;
      break;
  }

  return outErr;
}

static nsresult CFStringReftoUTF8(CFStringRef aInStrRef, nsACString& aOutStr)
{
  // first see if the conversion would succeed and find the length of the result
  CFIndex usedBufLen, inStrLen = ::CFStringGetLength(aInStrRef);
  CFIndex charsConverted = ::CFStringGetBytes(aInStrRef, CFRangeMake(0, inStrLen),
                                              kCFStringEncodingUTF8, 0, false,
                                              NULL, 0, &usedBufLen);
  if (charsConverted == inStrLen) {
    // all characters converted, do the actual conversion
    aOutStr.SetLength(usedBufLen);
    if (aOutStr.Length() != (unsigned int)usedBufLen)
      return NS_ERROR_OUT_OF_MEMORY;
    UInt8 *buffer = (UInt8*)aOutStr.BeginWriting();
    ::CFStringGetBytes(aInStrRef, CFRangeMake(0, inStrLen), kCFStringEncodingUTF8,
                       0, false, buffer, usedBufLen, &usedBufLen);
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsLocalFile::InitWithCFURL(CFURLRef aCFURL)
{
  UInt8 path[PATH_MAX];
  if (::CFURLGetFileSystemRepresentation(aCFURL, false, path, PATH_MAX)) {
    nsDependentCString nativePath((char*)path);
    return InitWithNativePath(nativePath);
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsLocalFile::InitWithFSRef(const FSRef *aFSRef)
{
  NS_ENSURE_ARG(aFSRef);

  CFURLRef newURLRef = ::CFURLCreateFromFSRef(kCFAllocatorDefault, aFSRef);
  if (newURLRef) {
    nsresult rv = InitWithCFURL(newURLRef);
    ::CFRelease(newURLRef);
    return rv;
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsLocalFile::GetCFURL(CFURLRef *_retval)
{
  CHECK_mPath();

  bool isDir;
  IsDirectory(&isDir);
  *_retval = ::CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                                                       (UInt8*)mPath.get(),
                                                       mPath.Length(),
                                                       isDir);

  return (*_retval ? NS_OK : NS_ERROR_FAILURE);
}

NS_IMETHODIMP
nsLocalFile::GetFSRef(FSRef *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv = NS_ERROR_FAILURE;

  CFURLRef url = NULL;
  if (NS_SUCCEEDED(GetCFURL(&url))) {
    if (::CFURLGetFSRef(url, _retval)) {
      rv = NS_OK;
    }
    ::CFRelease(url);
  }

  return rv;
}

NS_IMETHODIMP
nsLocalFile::GetFSSpec(FSSpec *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  FSRef fsRef;
  nsresult rv = GetFSRef(&fsRef);
  if (NS_SUCCEEDED(rv)) {
    OSErr err = ::FSGetCatalogInfo(&fsRef, kFSCatInfoNone, nullptr, nullptr, _retval, nullptr);
    return MacErrorMapper(err);
  }

  return rv;
}

NS_IMETHODIMP
nsLocalFile::GetFileSizeWithResFork(int64_t *aFileSizeWithResFork)
{
  NS_ENSURE_ARG_POINTER(aFileSizeWithResFork);

  FSRef fsRef;
  nsresult rv = GetFSRef(&fsRef);
  if (NS_FAILED(rv))
    return rv;

  FSCatalogInfo catalogInfo;
  OSErr err = ::FSGetCatalogInfo(&fsRef, kFSCatInfoDataSizes + kFSCatInfoRsrcSizes,
                                 &catalogInfo, nullptr, nullptr, nullptr);
  if (err != noErr)
    return MacErrorMapper(err);

  *aFileSizeWithResFork = catalogInfo.dataLogicalSize + catalogInfo.rsrcLogicalSize;
  return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetFileType(OSType *aFileType)
{
  CFURLRef url;
  if (NS_SUCCEEDED(GetCFURL(&url))) {
    nsresult rv = CocoaFileUtils::GetFileTypeCode(url, aFileType);
    ::CFRelease(url);
    return rv;
  }
  return NS_ERROR_FAILURE;
}
  
NS_IMETHODIMP
nsLocalFile::SetFileType(OSType aFileType)
{
  CFURLRef url;
  if (NS_SUCCEEDED(GetCFURL(&url))) {
    nsresult rv = CocoaFileUtils::SetFileTypeCode(url, aFileType);
    ::CFRelease(url);
    return rv;
  }
  return NS_ERROR_FAILURE;
}
  
NS_IMETHODIMP
nsLocalFile::GetFileCreator(OSType *aFileCreator)
{
  CFURLRef url;
  if (NS_SUCCEEDED(GetCFURL(&url))) {
    nsresult rv = CocoaFileUtils::GetFileCreatorCode(url, aFileCreator);
    ::CFRelease(url);
    return rv;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsLocalFile::SetFileCreator(OSType aFileCreator)
{
  CFURLRef url;
  if (NS_SUCCEEDED(GetCFURL(&url))) {
    nsresult rv = CocoaFileUtils::SetFileCreatorCode(url, aFileCreator);
    ::CFRelease(url);
    return rv;
  }
  return NS_ERROR_FAILURE;
}
  
NS_IMETHODIMP
nsLocalFile::LaunchWithDoc(nsIFile *aDocToLoad, bool aLaunchInBackground)
{    
  bool isExecutable;
  nsresult rv = IsExecutable(&isExecutable);
  if (NS_FAILED(rv))
    return rv;
  if (!isExecutable)
    return NS_ERROR_FILE_EXECUTION_FAILED;

  FSRef appFSRef, docFSRef;
  rv = GetFSRef(&appFSRef);
  if (NS_FAILED(rv))
    return rv;

  if (aDocToLoad) {
    nsCOMPtr<nsILocalFileMac> macDoc = do_QueryInterface(aDocToLoad);
    rv = macDoc->GetFSRef(&docFSRef);
    if (NS_FAILED(rv))
      return rv;
  }

  LSLaunchFlags theLaunchFlags = kLSLaunchDefaults;
  LSLaunchFSRefSpec thelaunchSpec;

  if (aLaunchInBackground)
    theLaunchFlags |= kLSLaunchDontSwitch;
  memset(&thelaunchSpec, 0, sizeof(LSLaunchFSRefSpec));

  thelaunchSpec.appRef = &appFSRef;
  if (aDocToLoad) {
    thelaunchSpec.numDocs = 1;
    thelaunchSpec.itemRefs = &docFSRef;
  }
  thelaunchSpec.launchFlags = theLaunchFlags;

  OSErr err = ::LSOpenFromRefSpec(&thelaunchSpec, NULL);
  if (err != noErr)
    return MacErrorMapper(err);

  return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::OpenDocWithApp(nsIFile *aAppToOpenWith, bool aLaunchInBackground)
{
  FSRef docFSRef;
  nsresult rv = GetFSRef(&docFSRef);
  if (NS_FAILED(rv))
    return rv;

  if (!aAppToOpenWith) {
    OSErr err = ::LSOpenFSRef(&docFSRef, NULL);
    return MacErrorMapper(err);
  }

  nsCOMPtr<nsILocalFileMac> appFileMac = do_QueryInterface(aAppToOpenWith, &rv);
  if (!appFileMac)
    return rv;

  bool isExecutable;
  rv = appFileMac->IsExecutable(&isExecutable);
  if (NS_FAILED(rv))
    return rv;
  if (!isExecutable)
    return NS_ERROR_FILE_EXECUTION_FAILED;

  FSRef appFSRef;
  rv = appFileMac->GetFSRef(&appFSRef);
  if (NS_FAILED(rv))
    return rv;

  LSLaunchFlags theLaunchFlags = kLSLaunchDefaults;
  LSLaunchFSRefSpec thelaunchSpec;

  if (aLaunchInBackground)
    theLaunchFlags |= kLSLaunchDontSwitch;
  memset(&thelaunchSpec, 0, sizeof(LSLaunchFSRefSpec));

  thelaunchSpec.appRef = &appFSRef;
  thelaunchSpec.numDocs = 1;
  thelaunchSpec.itemRefs = &docFSRef;
  thelaunchSpec.launchFlags = theLaunchFlags;

  OSErr err = ::LSOpenFromRefSpec(&thelaunchSpec, NULL);
  if (err != noErr)
    return MacErrorMapper(err);

  return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::IsPackage(bool *_retval)
{
  NS_ENSURE_ARG(_retval);
  *_retval = false;

  CFURLRef url;
  nsresult rv = GetCFURL(&url);
  if (NS_FAILED(rv))
    return rv;

  LSItemInfoRecord info;
  OSStatus status = ::LSCopyItemInfoForURL(url, kLSRequestBasicFlagsOnly, &info);

  ::CFRelease(url);

  if (status != noErr) {
    return NS_ERROR_FAILURE;
  }

  *_retval = !!(info.flags & kLSItemInfoIsPackage);

  return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetBundleDisplayName(nsAString& outBundleName)
{
  bool isPackage = false;
  nsresult rv = IsPackage(&isPackage);
  if (NS_FAILED(rv) || !isPackage)
    return NS_ERROR_FAILURE;

  nsAutoString name;
  rv = GetLeafName(name);
  if (NS_FAILED(rv))
    return rv;

  int32_t length = name.Length();
  if (Substring(name, length - 4, length).EqualsLiteral(".app")) {
    // 4 characters in ".app"
    outBundleName = Substring(name, 0, length - 4);
  }
  else {
    outBundleName = name;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetBundleIdentifier(nsACString& outBundleIdentifier)
{
  nsresult rv = NS_ERROR_FAILURE;

  CFURLRef urlRef;
  if (NS_SUCCEEDED(GetCFURL(&urlRef))) {
    CFBundleRef bundle = ::CFBundleCreate(NULL, urlRef);
    if (bundle) {
      CFStringRef bundleIdentifier = ::CFBundleGetIdentifier(bundle);
      if (bundleIdentifier)
        rv = CFStringReftoUTF8(bundleIdentifier, outBundleIdentifier);
      ::CFRelease(bundle);
    }
    ::CFRelease(urlRef);
  }

  return rv;
}

NS_IMETHODIMP
nsLocalFile::GetBundleContentsLastModifiedTime(int64_t *aLastModTime)
{
  CHECK_mPath();
  NS_ENSURE_ARG_POINTER(aLastModTime);

  bool isPackage = false;
  nsresult rv = IsPackage(&isPackage);
  if (NS_FAILED(rv) || !isPackage) {
    return GetLastModifiedTime(aLastModTime);
  }

  nsAutoCString infoPlistPath(mPath);
  infoPlistPath.AppendLiteral("/Contents/Info.plist");
  PRFileInfo64 info;
  if (PR_GetFileInfo64(infoPlistPath.get(), &info) != PR_SUCCESS) {
    return GetLastModifiedTime(aLastModTime);
  }
  int64_t modTime = int64_t(info.modifyTime);
  if (modTime == 0) {
    *aLastModTime = 0;
  } else {
    *aLastModTime = modTime / int64_t(PR_USEC_PER_MSEC);
  }

  return NS_OK;
}

NS_IMETHODIMP nsLocalFile::InitWithFile(nsIFile *aFile)
{
  NS_ENSURE_ARG(aFile);

  nsAutoCString nativePath;
  nsresult rv = aFile->GetNativePath(nativePath);
  if (NS_FAILED(rv))
    return rv;

  return InitWithNativePath(nativePath);
}

nsresult
NS_NewLocalFileWithFSRef(const FSRef* aFSRef, bool aFollowLinks, nsILocalFileMac** result)
{
  nsLocalFile* file = new nsLocalFile();
  if (file == nullptr)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(file);

  file->SetFollowLinks(aFollowLinks);

  nsresult rv = file->InitWithFSRef(aFSRef);
  if (NS_FAILED(rv)) {
    NS_RELEASE(file);
    return rv;
  }
  *result = file;
  return NS_OK;
}

nsresult
NS_NewLocalFileWithCFURL(const CFURLRef aURL, bool aFollowLinks, nsILocalFileMac** result)
{
  nsLocalFile* file = new nsLocalFile();
  if (!file)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(file);

  file->SetFollowLinks(aFollowLinks);

  nsresult rv = file->InitWithCFURL(aURL);
  if (NS_FAILED(rv)) {
    NS_RELEASE(file);
    return rv;
  }
  *result = file;
  return NS_OK;
}

#endif
