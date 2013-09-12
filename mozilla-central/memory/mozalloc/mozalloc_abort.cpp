/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if defined(XP_WIN) || defined(XP_OS2)
#  define MOZALLOC_EXPORT __declspec(dllexport)
#endif

#include "mozilla/Assertions.h"

#include <stdio.h>

#include "mozilla/mozalloc_abort.h"

void
mozalloc_abort(const char* const msg)
{
    fputs(msg, stderr);
    fputs("\n", stderr);
    MOZ_CRASH();
}

#if defined(XP_UNIX)
// Define abort() here, so that it is used instead of the system abort(). This
// lets us control the behavior when aborting, in order to get better results
// on *NIX platforms. See mozalloc_abort for details.
void abort(void)
{
    mozalloc_abort("Redirecting call to abort() to mozalloc_abort\n");
}
#endif

