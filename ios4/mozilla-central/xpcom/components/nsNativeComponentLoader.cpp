/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Original Code is mozilla.org Code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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
 * ***** END LICENSE BLOCK *****
 * This Original Code has been modified by IBM Corporation.
 * Modifications made by IBM described herein are
 * Copyright (c) International Business Machines
 * Corporation, 2000
 *
 * Modifications to Mozilla code or documentation
 * identified per MPL Section 3.3
 *
 * Date             Modified by     Description of modification
 * 04/20/2000       IBM Corp.      Added PR_CALLBACK for Optlink use in OS2
 */

/* Allow logging in the release build */
#ifdef MOZ_LOGGING
#define FORCE_PR_LOG
#endif

#include "nsNativeComponentLoader.h"

#include "prlog.h"
#include "prinit.h"
#include "prerror.h"

#include "nsComponentManager.h"
#include "nsCRTGlue.h"
#include "nsThreadUtils.h"
#include "nsTraceRefcntImpl.h"

#include "nsILocalFile.h"
#include "nsIProxyObjectManager.h"

#ifdef XP_WIN
#include <windows.h>
#endif

#ifdef XP_MACOSX
#include <signal.h>
#endif

#ifdef VMS
#include <lib$routines.h>
#include <ssdef.h>
#endif

#if defined(DEBUG) && !defined(XP_BEOS)
#define IMPLEMENT_BREAK_AFTER_LOAD
#endif

static PRLogModuleInfo *nsNativeModuleLoaderLog =
    PR_NewLogModule("nsNativeModuleLoader");

#define LOG(level, args) PR_LOG(nsNativeModuleLoaderLog, level, args)

NS_IMPL_QUERY_INTERFACE1(nsNativeModuleLoader, 
                         mozilla::ModuleLoader)

NS_IMPL_ADDREF_USING_AGGREGATOR(nsNativeModuleLoader,
                                nsComponentManagerImpl::gComponentManager)
NS_IMPL_RELEASE_USING_AGGREGATOR(nsNativeModuleLoader,
                                 nsComponentManagerImpl::gComponentManager)

nsresult
nsNativeModuleLoader::Init()
{
    NS_ASSERTION(NS_IsMainThread(), "Startup not on main thread?");

    LOG(PR_LOG_DEBUG, ("nsNativeModuleLoader::Init()"));

    return mLibraries.Init() ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

class LoadModuleMainThreadRunnable : public nsRunnable
{
public:
    LoadModuleMainThreadRunnable(nsNativeModuleLoader* loader,
                                 nsILocalFile* file)
        : mLoader(loader)
        , mFile(file)
        , mResult(NULL)
    { }

    NS_IMETHOD Run()
    {
        mResult = mLoader->LoadModule(mFile);
        return NS_OK;
    }

    nsRefPtr<nsNativeModuleLoader> mLoader;
    nsCOMPtr<nsILocalFile> mFile;
    const mozilla::Module* mResult;
};

const mozilla::Module*
nsNativeModuleLoader::LoadModule(nsILocalFile* aFile)
{
    nsresult rv;

    if (!NS_IsMainThread()) {
        // If this call is off the main thread, synchronously proxy it
        // to the main thread.
        nsRefPtr<LoadModuleMainThreadRunnable> r = new LoadModuleMainThreadRunnable(this, aFile);
        NS_DispatchToMainThread(r, NS_DISPATCH_SYNC);
        return r->mResult;
    }

    nsCOMPtr<nsIHashable> hashedFile(do_QueryInterface(aFile));
    if (!hashedFile) {
        NS_ERROR("nsIFile is not nsIHashable");
        return NULL;
    }

    nsCAutoString filePath;
    aFile->GetNativePath(filePath);

    NativeLoadData data;

    if (mLibraries.Get(hashedFile, &data)) {
        NS_ASSERTION(data.module, "Corrupt mLibraries hash");
        LOG(PR_LOG_DEBUG,
            ("nsNativeModuleLoader::LoadModule(\"%s\") - found in cache",
             filePath.get()));
        return data.module;
    }

    // We haven't loaded this module before

    rv = aFile->Load(&data.library);

    if (NS_FAILED(rv)) {
        char errorMsg[1024] = "<unknown; can't get error from NSPR>";

        if (PR_GetErrorTextLength() < (int) sizeof(errorMsg))
            PR_GetErrorText(errorMsg);

        LOG(PR_LOG_ERROR,
            ("nsNativeModuleLoader::LoadModule(\"%s\") - load FAILED, "
             "rv: %lx, error:\n\t%s\n",
             filePath.get(), rv, errorMsg));

#ifdef DEBUG
        fprintf(stderr,
                "nsNativeModuleLoader::LoadModule(\"%s\") - load FAILED, "
                "rv: %lx, error:\n\t%s\n",
                filePath.get(), (unsigned long)rv, errorMsg);
#endif

        return NULL;
    }

#ifdef IMPLEMENT_BREAK_AFTER_LOAD
    nsCAutoString leafName;
    aFile->GetNativeLeafName(leafName);

    char *env = getenv("XPCOM_BREAK_ON_LOAD");
    char *blist;
    if (env && *env && (blist = strdup(env))) {
        char *nextTok = blist;
        while (char *token = NS_strtok(":", &nextTok)) {
            if (leafName.Find(token, PR_TRUE) != kNotFound) {
                NS_BREAK();
            }
        }

        free(blist);
    }
#endif

    void *module = PR_FindSymbol(data.library, "NSModule");
    if (module) {
        data.module = *(mozilla::Module const *const *) module;
        if (mLibraries.Put(hashedFile, data))
            return data.module;
    }
    else {
        LOG(PR_LOG_ERROR,
            ("nsNativeModuleLoader::LoadModule(\"%s\") - "
             "Symbol NSModule not found", filePath.get()));
    }

    // at some point we failed, clean up
    data.module = nsnull;
    PR_UnloadLibrary(data.library);

    return NULL;
}

const mozilla::Module*
nsNativeModuleLoader::LoadModuleFromJAR(nsILocalFile* aJARFile, const nsACString &aPath)
{
    NS_ERROR("Binary components cannot be loaded from JARs");
    return NULL;
}

PLDHashOperator
nsNativeModuleLoader::ReleaserFunc(nsIHashable* aHashedFile,
                                   NativeLoadData& aLoadData, void*)
{
    aLoadData.module = nsnull;
    return PL_DHASH_NEXT;
}

PLDHashOperator
nsNativeModuleLoader::UnloaderFunc(nsIHashable* aHashedFile,
                                   NativeLoadData& aLoadData, void*)
{
    if (PR_LOG_TEST(nsNativeModuleLoaderLog, PR_LOG_DEBUG)) {
        nsCOMPtr<nsIFile> file(do_QueryInterface(aHashedFile));

        nsCAutoString filePath;
        file->GetNativePath(filePath);

        LOG(PR_LOG_DEBUG,
            ("nsNativeModuleLoader::UnloaderFunc(\"%s\")", filePath.get()));
    }

#ifdef NS_BUILD_REFCNT_LOGGING
    nsTraceRefcntImpl::SetActivityIsLegal(PR_FALSE);
#endif

#if 0
    // XXXbsmedberg: do this as soon as the static-destructor crash(es)
    // are fixed
    PRStatus ret = PR_UnloadLibrary(aLoadData.library);
    NS_ASSERTION(ret == PR_SUCCESS, "Failed to unload library");
#endif

#ifdef NS_BUILD_REFCNT_LOGGING
    nsTraceRefcntImpl::SetActivityIsLegal(PR_TRUE);
#endif

    return PL_DHASH_REMOVE;
}

void
nsNativeModuleLoader::UnloadLibraries()
{
    NS_ASSERTION(NS_IsMainThread(), "Shutdown not on main thread?");

    mLibraries.Enumerate(ReleaserFunc, nsnull);
    mLibraries.Enumerate(UnloaderFunc, nsnull);
}
