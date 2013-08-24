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
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998-1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Don Bragg <dbragg@netscape.com>
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

/*****************************************************************************
 * 
 * nsProcess is used to execute new processes and specify if you want to
 * wait (blocking) or continue (non-blocking).
 *
 *****************************************************************************
 */

#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsMemory.h"
#include "nsProcess.h"
#include "prtypes.h"
#include "prio.h"
#include "prenv.h"
#include "nsCRT.h"
#include "nsAutoLock.h"
#include "nsThreadUtils.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"

#include <stdlib.h>

#if defined(PROCESSMODEL_WINAPI)
#include "prmem.h"
#include "nsString.h"
#include "nsLiteralString.h"
#include "nsReadableUtils.h"
#else
#include <sys/types.h>
#include <signal.h>
#endif

#ifdef WINCE
#include <windows.h> // for MultiByteToWideChar
#include "prmem.h"
#define SHELLEXECUTEINFOW SHELLEXECUTEINFO
#define SEE_MASK_FLAG_DDEWAIT 0
#define SEE_MASK_NO_CONSOLE 0
#define ShellExecuteExW ShellExecuteEx
#endif

//-------------------------------------------------------------------//
// nsIProcess implementation
//-------------------------------------------------------------------//
NS_IMPL_THREADSAFE_ISUPPORTS2(nsProcess, nsIProcess,
                                         nsIObserver)

//Constructor
nsProcess::nsProcess()
    : mThread(nsnull),
      mLock(PR_NewLock()),
      mShutdown(PR_FALSE),
      mPid(-1),
      mObserver(nsnull),
      mWeakObserver(nsnull),
      mExitValue(-1),
      mProcess(nsnull)
{
}

//Destructor
nsProcess::~nsProcess()
{
    PR_DestroyLock(mLock);
}

NS_IMETHODIMP
nsProcess::Init(nsIFile* executable)
{
    if (mExecutable)
        return NS_ERROR_ALREADY_INITIALIZED;

    NS_ENSURE_ARG_POINTER(executable);
    PRBool isFile;

    //First make sure the file exists
    nsresult rv = executable->IsFile(&isFile);
    if (NS_FAILED(rv)) return rv;
    if (!isFile)
        return NS_ERROR_FAILURE;

    //Store the nsIFile in mExecutable
    mExecutable = executable;
    //Get the path because it is needed by the NSPR process creation
#ifdef XP_WIN 
    rv = mExecutable->GetTarget(mTargetPath);
    if (NS_FAILED(rv) || mTargetPath.IsEmpty() )
#endif
        rv = mExecutable->GetPath(mTargetPath);

    return rv;
}


#if defined(XP_WIN)
// Out param `wideCmdLine` must be PR_Freed by the caller.
static int assembleCmdLine(char *const *argv, PRUnichar **wideCmdLine,
                           UINT codePage)
{
    char *const *arg;
    char *p, *q, *cmdLine;
    int cmdLineSize;
    int numBackslashes;
    int i;
    int argNeedQuotes;

    /*
     * Find out how large the command line buffer should be.
     */
    cmdLineSize = 0;
    for (arg = argv; *arg; arg++) {
        /*
         * \ and " need to be escaped by a \.  In the worst case,
         * every character is a \ or ", so the string of length
         * may double.  If we quote an argument, that needs two ".
         * Finally, we need a space between arguments, and
         * a null byte at the end of command line.
         */
        cmdLineSize += 2 * strlen(*arg)  /* \ and " need to be escaped */
                + 2                      /* we quote every argument */
                + 1;                     /* space in between, or final null */
    }
    p = cmdLine = (char *) PR_MALLOC(cmdLineSize*sizeof(char));
    if (p == NULL) {
        return -1;
    }

    for (arg = argv; *arg; arg++) {
        /* Add a space to separates the arguments */
        if (arg != argv) {
            *p++ = ' '; 
        }
        q = *arg;
        numBackslashes = 0;
        argNeedQuotes = 0;

        /* If the argument contains white space, it needs to be quoted. */
        if (strpbrk(*arg, " \f\n\r\t\v")) {
            argNeedQuotes = 1;
        }

        if (argNeedQuotes) {
            *p++ = '"';
        }
        while (*q) {
            if (*q == '\\') {
                numBackslashes++;
                q++;
            } else if (*q == '"') {
                if (numBackslashes) {
                    /*
                     * Double the backslashes since they are followed
                     * by a quote
                     */
                    for (i = 0; i < 2 * numBackslashes; i++) {
                        *p++ = '\\';
                    }
                    numBackslashes = 0;
                }
                /* To escape the quote */
                *p++ = '\\';
                *p++ = *q++;
            } else {
                if (numBackslashes) {
                    /*
                     * Backslashes are not followed by a quote, so
                     * don't need to double the backslashes.
                     */
                    for (i = 0; i < numBackslashes; i++) {
                        *p++ = '\\';
                    }
                    numBackslashes = 0;
                }
                *p++ = *q++;
            }
        }

        /* Now we are at the end of this argument */
        if (numBackslashes) {
            /*
             * Double the backslashes if we have a quote string
             * delimiter at the end.
             */
            if (argNeedQuotes) {
                numBackslashes *= 2;
            }
            for (i = 0; i < numBackslashes; i++) {
                *p++ = '\\';
            }
        }
        if (argNeedQuotes) {
            *p++ = '"';
        }
    } 

    *p = '\0';
    PRInt32 numChars = MultiByteToWideChar(codePage, 0, cmdLine, -1, NULL, 0); 
    *wideCmdLine = (PRUnichar *) PR_MALLOC(numChars*sizeof(PRUnichar));
    MultiByteToWideChar(codePage, 0, cmdLine, -1, *wideCmdLine, numChars); 
    PR_Free(cmdLine);
    return 0;
}
#endif

void PR_CALLBACK nsProcess::Monitor(void *arg)
{
    nsRefPtr<nsProcess> process = dont_AddRef(static_cast<nsProcess*>(arg));
#if defined(PROCESSMODEL_WINAPI)
    DWORD dwRetVal;
    unsigned long exitCode = -1;

    dwRetVal = WaitForSingleObject(process->mProcess, INFINITE);
    if (dwRetVal != WAIT_FAILED) {
        if (GetExitCodeProcess(process->mProcess, &exitCode) == FALSE)
            exitCode = -1;
    }

    // Lock in case Kill or GetExitCode are called during this
    {
        nsAutoLock lock(process->mLock);
        CloseHandle(process->mProcess);
        process->mProcess = NULL;
        process->mExitValue = exitCode;
        if (process->mShutdown)
            return;
    }
#else
    PRInt32 exitCode = -1;
    if (PR_WaitProcess(process->mProcess, &exitCode) != PR_SUCCESS)
        exitCode = -1;

    // Lock in case Kill or GetExitCode are called during this
    {
        nsAutoLock lock(process->mLock);
        process->mProcess = nsnull;
        process->mExitValue = exitCode;
        if (process->mShutdown)
            return;
    }
#endif

    // If we ran a background thread for the monitor then notify on the main
    // thread
    if (NS_IsMainThread()) {
        process->ProcessComplete();
    }
    else {
        nsCOMPtr<nsIRunnable> event =
            NS_NewRunnableMethod(process, &nsProcess::ProcessComplete);
        NS_DispatchToMainThread(event);
    }
}

void nsProcess::ProcessComplete()
{
    if (mThread) {
        nsCOMPtr<nsIObserverService> os =
            mozilla::services::GetObserverService();
        if (os)
            os->RemoveObserver(this, "xpcom-shutdown");
        PR_JoinThread(mThread);
        mThread = nsnull;
    }

    const char* topic;
    if (mExitValue < 0)
        topic = "process-failed";
    else
        topic = "process-finished";

    mPid = -1;
    nsCOMPtr<nsIObserver> observer;
    if (mWeakObserver)
        observer = do_QueryReferent(mWeakObserver);
    else if (mObserver)
        observer = mObserver;
    mObserver = nsnull;
    mWeakObserver = nsnull;

    if (observer)
        observer->Observe(NS_ISUPPORTS_CAST(nsIProcess*, this), topic, nsnull);
}

// XXXldb |args| has the wrong const-ness
NS_IMETHODIMP  
nsProcess::Run(PRBool blocking, const char **args, PRUint32 count)
{
    return CopyArgsAndRunProcess(blocking, args, count, nsnull, PR_FALSE);
}

// XXXldb |args| has the wrong const-ness
NS_IMETHODIMP  
nsProcess::RunAsync(const char **args, PRUint32 count,
                    nsIObserver* observer, PRBool holdWeak)
{
    return CopyArgsAndRunProcess(PR_FALSE, args, count, observer, holdWeak);
}

NS_IMETHODIMP
nsProcess::CopyArgsAndRunProcess(PRBool blocking, const char** args,
                                 PRUint32 count, nsIObserver* observer,
                                 PRBool holdWeak)
{
    // make sure that when we allocate we have 1 greater than the
    // count since we need to null terminate the list for the argv to
    // pass into PR_CreateProcess
    char **my_argv = NULL;
    my_argv = (char **)NS_Alloc(sizeof(char *) * (count + 2) );
    if (!my_argv) {
        return NS_ERROR_OUT_OF_MEMORY;
    }

    // copy the args
    PRUint32 i;
    for (i=0; i < count; i++) {
        my_argv[i+1] = const_cast<char*>(args[i]);
        printf("arg[%d] = %s\n", i, my_argv[i+1]);
    }
    // we need to set argv[0] to the program name.
    my_argv[0] = ToNewUTF8String(mTargetPath);
    // null terminate the array
    my_argv[count+1] = NULL;

    nsresult rv =
        RunProcess(blocking, my_argv, count, observer, holdWeak, PR_FALSE);

    NS_Free(my_argv[0]);
    NS_Free(my_argv);
    return rv;
}

// XXXldb |args| has the wrong const-ness
NS_IMETHODIMP  
nsProcess::Runw(PRBool blocking, const PRUnichar **args, PRUint32 count)
{
    return CopyArgsAndRunProcessw(blocking, args, count, nsnull, PR_FALSE);
}

// XXXldb |args| has the wrong const-ness
NS_IMETHODIMP  
nsProcess::RunwAsync(const PRUnichar **args, PRUint32 count,
                    nsIObserver* observer, PRBool holdWeak)
{
    return CopyArgsAndRunProcessw(PR_FALSE, args, count, observer, holdWeak);
}

NS_IMETHODIMP
nsProcess::CopyArgsAndRunProcessw(PRBool blocking, const PRUnichar** args,
                                  PRUint32 count, nsIObserver* observer,
                                  PRBool holdWeak)
{
    // make sure that when we allocate we have 1 greater than the
    // count since we need to null terminate the list for the argv to
    // pass into PR_CreateProcess
    char **my_argv = NULL;
    my_argv = (char **)NS_Alloc(sizeof(char *) * (count + 2) );
    if (!my_argv) {
        return NS_ERROR_OUT_OF_MEMORY;
    }

    // copy the args
    PRUint32 i;
    for (i=0; i < count; i++) {
        my_argv[i+1] = ToNewUTF8String(nsDependentString(args[i]));
    }
    // we need to set argv[0] to the program name.
    my_argv[0] = ToNewUTF8String(mTargetPath);
    // null terminate the array
    my_argv[count+1] = NULL;

    nsresult rv =
        RunProcess(blocking, my_argv, count, observer, holdWeak, PR_TRUE);

    for (i=0; i <= count; ++i) {
        NS_Free(my_argv[i]);
    }
    NS_Free(my_argv);
    return rv;
}

NS_IMETHODIMP  
nsProcess::RunProcess(PRBool blocking, char **my_argv, PRUint32 count,
                      nsIObserver* observer, PRBool holdWeak, PRBool argsUTF8)
{
    NS_ENSURE_TRUE(mExecutable, NS_ERROR_NOT_INITIALIZED);
    NS_ENSURE_FALSE(mThread, NS_ERROR_ALREADY_INITIALIZED);

    if (observer) {
        if (holdWeak) {
            mWeakObserver = do_GetWeakReference(observer);
            if (!mWeakObserver)
                return NS_NOINTERFACE;
        }
        else {
            mObserver = observer;
        }
    }

    mExitValue = -1;
    mPid = -1;

#if defined(PROCESSMODEL_WINAPI)
    BOOL retVal;
    PRUnichar *cmdLine;

    if (count > 0 && assembleCmdLine(my_argv + 1, &cmdLine, 
                                     argsUTF8 ? CP_UTF8 : CP_ACP) == -1) {
        return NS_ERROR_FILE_EXECUTION_FAILED;    
    }

    /* The SEE_MASK_NO_CONSOLE flag is important to prevent console windows
     * from appearing. This makes behavior the same on all platforms. The flag
     * will not have any effect on non-console applications.
     */

    // The program name in my_argv[0] is always UTF-8
    PRInt32 numChars = MultiByteToWideChar(CP_UTF8, 0, my_argv[0], -1, NULL, 0); 
    PRUnichar* wideFile = (PRUnichar *) PR_MALLOC(numChars * sizeof(PRUnichar));
    MultiByteToWideChar(CP_UTF8, 0, my_argv[0], -1, wideFile, numChars); 

    SHELLEXECUTEINFOW sinfo;
    memset(&sinfo, 0, sizeof(SHELLEXECUTEINFOW));
    sinfo.cbSize = sizeof(SHELLEXECUTEINFOW);
    sinfo.hwnd   = NULL;
    sinfo.lpFile = wideFile;
    sinfo.nShow  = SW_SHOWNORMAL;
    sinfo.fMask  = SEE_MASK_FLAG_DDEWAIT |
                   SEE_MASK_NO_CONSOLE |
                   SEE_MASK_NOCLOSEPROCESS;

    if (count > 0)
        sinfo.lpParameters = cmdLine;

    retVal = ShellExecuteExW(&sinfo);
    if (!retVal) {
        return NS_ERROR_FILE_EXECUTION_FAILED;
    }

    mProcess = sinfo.hProcess;

    PR_Free(wideFile);
    if (count > 0)
        PR_Free( cmdLine );

    HMODULE kernelDLL = ::LoadLibraryW(L"kernel32.dll");
    if (kernelDLL) {
        GetProcessIdPtr getProcessId = (GetProcessIdPtr)GetProcAddress(kernelDLL, "GetProcessId");
        if (getProcessId)
            mPid = getProcessId(mProcess);
        FreeLibrary(kernelDLL);
    }

#else // Note, this must not be an #elif ...!
    
    mProcess = PR_CreateProcess(my_argv[0], my_argv, NULL, NULL);
    if (!mProcess)
        return NS_ERROR_FAILURE;

#if !defined WINCE
    struct MYProcess {
        PRUint32 pid;
    };
    MYProcess* ptrProc = (MYProcess *) mProcess;
    mPid = ptrProc->pid;
#endif
#endif

    NS_ADDREF_THIS();
    if (blocking) {
        Monitor(this);
        if (mExitValue < 0)
            return NS_ERROR_FILE_EXECUTION_FAILED;
    }
    else {
        mThread = PR_CreateThread(PR_SYSTEM_THREAD, Monitor, this,
                                  PR_PRIORITY_NORMAL, PR_LOCAL_THREAD,
                                  PR_JOINABLE_THREAD, 0);
        if (!mThread) {
            NS_RELEASE_THIS();
            return NS_ERROR_FAILURE;
        }

        // It isn't a failure if we just can't watch for shutdown
        nsCOMPtr<nsIObserverService> os =
          mozilla::services::GetObserverService();
        if (os)
            os->AddObserver(this, "xpcom-shutdown", PR_FALSE);
    }

    return NS_OK;
}

NS_IMETHODIMP nsProcess::GetIsRunning(PRBool *aIsRunning)
{
    if (mThread)
        *aIsRunning = PR_TRUE;
    else
        *aIsRunning = PR_FALSE;

    return NS_OK;
}

NS_IMETHODIMP
nsProcess::GetPid(PRUint32 *aPid)
{
    if (!mThread)
        return NS_ERROR_FAILURE;
    if (mPid < 0)
        return NS_ERROR_NOT_IMPLEMENTED;
    *aPid = mPid;
    return NS_OK;
}

NS_IMETHODIMP
nsProcess::Kill()
{
    if (!mThread)
        return NS_ERROR_FAILURE;

    {
        nsAutoLock lock(mLock);
#if defined(PROCESSMODEL_WINAPI)
        if (TerminateProcess(mProcess, NULL) == 0)
            return NS_ERROR_FAILURE;
#else
        if (!mProcess || (PR_KillProcess(mProcess) != PR_SUCCESS))
            return NS_ERROR_FAILURE;
#endif
    }

    // We must null out mThread if we want IsRunning to return false immediately
    // after this call.
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os)
        os->RemoveObserver(this, "xpcom-shutdown");
    PR_JoinThread(mThread);
    mThread = nsnull;

    return NS_OK;
}

NS_IMETHODIMP
nsProcess::GetExitValue(PRInt32 *aExitValue)
{
    nsAutoLock lock(mLock);

    *aExitValue = mExitValue;
    
    return NS_OK;
}

NS_IMETHODIMP
nsProcess::Observe(nsISupports* subject, const char* topic, const PRUnichar* data)
{
    // Shutting down, drop all references
    if (mThread) {
        nsCOMPtr<nsIObserverService> os =
          mozilla::services::GetObserverService();
        if (os)
            os->RemoveObserver(this, "xpcom-shutdown");
        mThread = nsnull;
    }

    mObserver = nsnull;
    mWeakObserver = nsnull;

    nsAutoLock lock(mLock);
    mShutdown = PR_TRUE;

    return NS_OK;
}
