/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_mozalloc_abort_h
#define mozilla_mozalloc_abort_h

#include "mozilla/Attributes.h"

#if defined(MOZALLOC_EXPORT)
// do nothing: it's been defined to __declspec(dllexport) by
// mozalloc*.cpp on platforms where that's required
#elif defined(XP_WIN) || (defined(XP_OS2) && defined(__declspec))
#  define MOZALLOC_EXPORT __declspec(dllimport)
#elif defined(HAVE_VISIBILITY_ATTRIBUTE)
/* Make sure symbols are still exported even if we're wrapped in a
 * |visibility push(hidden)| blanket. */
#  define MOZALLOC_EXPORT __attribute__ ((visibility ("default")))
#else
#  define MOZALLOC_EXPORT
#endif

/**
 * Terminate this process in such a way that breakpad is triggered, if
 * at all possible.
 */
MOZ_NORETURN MOZALLOC_EXPORT void mozalloc_abort(const char* const msg);


#endif  /* ifndef mozilla_mozalloc_abort_h */
