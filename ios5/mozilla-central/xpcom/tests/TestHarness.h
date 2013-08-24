/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Test harness for XPCOM objects, providing a scoped XPCOM initializer,
 * nsCOMPtr, nsRefPtr, do_CreateInstance, do_GetService, ns(Auto|C|)String,
 * and stdio.h/stdlib.h.
 */

#ifndef TestHarness_h__
#define TestHarness_h__

#if defined(_MSC_VER) && defined(MOZ_STATIC_JS)
/*
 * Including jsdbgapi.h may cause build break with --disable-shared-js
 * This is a workaround for bug 673616.
 */
#define STATIC_JS_API
#endif

#include "mozilla/Util.h"

#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsStringGlue.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIDirectoryService.h"
#include "nsIFile.h"
#include "nsIProperties.h"
#include "nsIObserverService.h"
#include "nsXULAppAPI.h"
#include "jsdbgapi.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static PRUint32 gFailCount = 0;

/**
 * Prints the given failure message and arguments using printf, prepending
 * "TEST-UNEXPECTED-FAIL " for the benefit of the test harness and
 * appending "\n" to eliminate having to type it at each call site.
 */
void fail(const char* msg, ...)
{
  va_list ap;

  printf("TEST-UNEXPECTED-FAIL | ");

  va_start(ap, msg);
  vprintf(msg, ap);
  va_end(ap);

  putchar('\n');
  ++gFailCount;
}

/**
 * Prints the given success message and arguments using printf, prepending
 * "TEST-PASS " for the benefit of the test harness and
 * appending "\n" to eliminate having to type it at each call site.
 */
void passed(const char* msg, ...)
{
  va_list ap;

  printf("TEST-PASS | ");

  va_start(ap, msg);
  vprintf(msg, ap);
  va_end(ap);

  putchar('\n');
}

//-----------------------------------------------------------------------------
// Code profiling
//
static const char* gCurrentProfile;

/**
 * If the build has been configured properly, start the best code profiler
 * available on this platform.
 *
 * This is NOT thread safe.
 *
 * @precondition Profiling is not started
 * @param profileName A descriptive name for this profiling run.  Every 
 *                    attempt is made to name the profile data according
 *                    to this name, but check your platform's profiler
 *                    documentation for what this means.
 * @return true if profiling was available and successfully started.
 * @see StopProfiling
 */
inline bool
StartProfiling(const char* profileName)
{
    NS_ASSERTION(profileName, "need a name for this profile");
    NS_PRECONDITION(!gCurrentProfile, "started a new profile before stopping another");

    JSBool ok = JS_StartProfiling(profileName);
    gCurrentProfile = profileName;
    return ok ? true : false;
}

/**
 * Stop the platform's profiler.  For what this means, what happens after
 * stopping, and how the profile data can be accessed, check the 
 * documentation of your platform's profiler.
 *
 * This is NOT thread safe.
 *
 * @precondition Profiling was started
 * @return true if profiling was successfully stopped.
 * @see StartProfiling
 */
inline bool
StopProfiling()
{
    NS_PRECONDITION(gCurrentProfile, "tried to stop profile before starting one");

    const char* profileName = gCurrentProfile;
    gCurrentProfile = 0;
    return JS_StopProfiling(profileName) ? true : false;
}

//-----------------------------------------------------------------------------

class ScopedLogging
{
public:
    ScopedLogging()
    {
        NS_LogInit();
    }

    ~ScopedLogging()
    {
        NS_LogTerm();
    }
};

class ScopedXPCOM : public nsIDirectoryServiceProvider2
{
  public:
    NS_DECL_ISUPPORTS

    ScopedXPCOM(const char* testName,
                nsIDirectoryServiceProvider *dirSvcProvider = NULL)
    : mDirSvcProvider(dirSvcProvider)
    {
      mTestName = testName;
      printf("Running %s tests...\n", mTestName);

      nsresult rv = NS_InitXPCOM2(&mServMgr, NULL, this);
      if (NS_FAILED(rv))
      {
        fail("NS_InitXPCOM2 returned failure code 0x%x", rv);
        mServMgr = NULL;
        return;
      }
    }

    ~ScopedXPCOM()
    {
      // If we created a profile directory, we need to remove it.
      if (mProfD) {
        nsCOMPtr<nsIObserverService> os =
          do_GetService(NS_OBSERVERSERVICE_CONTRACTID);
        MOZ_ASSERT(os);
        if (os) {
          MOZ_ALWAYS_TRUE(NS_SUCCEEDED(os->NotifyObservers(nsnull, "profile-change-net-teardown", nsnull)));
          MOZ_ALWAYS_TRUE(NS_SUCCEEDED(os->NotifyObservers(nsnull, "profile-change-teardown", nsnull)));
          MOZ_ALWAYS_TRUE(NS_SUCCEEDED(os->NotifyObservers(nsnull, "profile-before-change", nsnull)));
        }

        if (NS_FAILED(mProfD->Remove(true))) {
          NS_WARNING("Problem removing profile directory");
        }

        mProfD = nsnull;
      }

      if (mServMgr)
      {
        NS_RELEASE(mServMgr);
        nsresult rv = NS_ShutdownXPCOM(NULL);
        if (NS_FAILED(rv))
        {
          fail("XPCOM shutdown failed with code 0x%x", rv);
          exit(1);
        }
      }

      printf("Finished running %s tests.\n", mTestName);
    }

    bool failed()
    {
      return mServMgr == NULL;
    }

    already_AddRefed<nsIFile> GetProfileDirectory()
    {
      if (mProfD) {
        nsCOMPtr<nsIFile> copy = mProfD;
        return copy.forget();
      }

      // Create a unique temporary folder to use for this test.
      nsCOMPtr<nsIFile> profD;
      nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR,
                                           getter_AddRefs(profD));
      NS_ENSURE_SUCCESS(rv, nsnull);

      rv = profD->Append(NS_LITERAL_STRING("cpp-unit-profd"));
      NS_ENSURE_SUCCESS(rv, nsnull);

      rv = profD->CreateUnique(nsIFile::DIRECTORY_TYPE, 0755);
      NS_ENSURE_SUCCESS(rv, nsnull);

      mProfD = profD;
      return profD.forget();
    }

    ////////////////////////////////////////////////////////////////////////////
    //// nsIDirectoryServiceProvider

    NS_IMETHODIMP GetFile(const char *aProperty, bool *_persistent,
                          nsIFile **_result)
    {
      // If we were supplied a directory service provider, ask it first.
      if (mDirSvcProvider &&
          NS_SUCCEEDED(mDirSvcProvider->GetFile(aProperty, _persistent,
                                                _result))) {
        return NS_OK;
      }

      // Otherwise, the test harness provides some directories automatically.
      if (0 == strcmp(aProperty, NS_APP_USER_PROFILE_50_DIR) ||
          0 == strcmp(aProperty, NS_APP_USER_PROFILE_LOCAL_50_DIR) ||
          0 == strcmp(aProperty, NS_APP_PROFILE_LOCAL_DIR_STARTUP)) {
        nsCOMPtr<nsIFile> profD = GetProfileDirectory();
        NS_ENSURE_TRUE(profD, NS_ERROR_FAILURE);

        nsCOMPtr<nsIFile> clone;
        nsresult rv = profD->Clone(getter_AddRefs(clone));
        NS_ENSURE_SUCCESS(rv, rv);

        *_persistent = true;
        clone.forget(_result);
        return NS_OK;
      }

      return NS_ERROR_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    //// nsIDirectoryServiceProvider2

    NS_IMETHODIMP GetFiles(const char *aProperty, nsISimpleEnumerator **_enum)
    {
      // If we were supplied a directory service provider, ask it first.
      nsCOMPtr<nsIDirectoryServiceProvider2> provider =
        do_QueryInterface(mDirSvcProvider);
      if (provider && NS_SUCCEEDED(provider->GetFiles(aProperty, _enum))) {
        return NS_OK;
      }

     return NS_ERROR_FAILURE;
   }

  private:
    const char* mTestName;
    nsIServiceManager* mServMgr;
    nsCOMPtr<nsIDirectoryServiceProvider> mDirSvcProvider;
    nsCOMPtr<nsIFile> mProfD;
};

NS_IMPL_QUERY_INTERFACE2(
  ScopedXPCOM,
  nsIDirectoryServiceProvider,
  nsIDirectoryServiceProvider2
)

NS_IMETHODIMP_(nsrefcnt)
ScopedXPCOM::AddRef()
{
  return 2;
}

NS_IMETHODIMP_(nsrefcnt)
ScopedXPCOM::Release()
{
  return 1;
}

#endif  // TestHarness_h__
