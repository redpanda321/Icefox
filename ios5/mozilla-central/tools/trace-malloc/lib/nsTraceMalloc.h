/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsTraceMalloc_h___
#define nsTraceMalloc_h___

#include "mozilla/StandardInteger.h"

#include <stdio.h> /* for FILE */
#include "prtypes.h"

#ifdef XP_WIN
#define setlinebuf(stream) setvbuf((stream), (char *)NULL, _IOLBF, 0)
#endif

PR_BEGIN_EXTERN_C

/**
 * Magic "number" at start of a trace-malloc log file.  Inspired by the PNG
 * magic string, which inspired XPCOM's typelib (.xpt) file magic.  See the
 * NS_TraceMallocStartup comment (below) for magic number differences in log
 * file structure.
 */
#define NS_TRACE_MALLOC_MAGIC           "XPCOM\nTMLog08\r\n\032"
#define NS_TRACE_MALLOC_MAGIC_SIZE      16

/**
 * Trace-malloc stats, traced via the 'Z' event at the end of a log file.
 */
typedef struct nsTMStats {
    uint32_t calltree_maxstack;
    uint32_t calltree_maxdepth;
    uint32_t calltree_parents;
    uint32_t calltree_maxkids;
    uint32_t calltree_kidhits;
    uint32_t calltree_kidmisses;
    uint32_t calltree_kidsteps;
    uint32_t callsite_recurrences;
    uint32_t backtrace_calls;
    uint32_t backtrace_failures;
    uint32_t btmalloc_failures;
    uint32_t dladdr_failures;
    uint32_t malloc_calls;
    uint32_t malloc_failures;
    uint32_t calloc_calls;
    uint32_t calloc_failures;
    uint32_t realloc_calls;
    uint32_t realloc_failures;
    uint32_t free_calls;
    uint32_t null_free_calls;
} nsTMStats;

#define NS_TMSTATS_STATIC_INITIALIZER {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}

/**
 * Call NS_TraceMallocStartup with a valid file descriptor to enable logging
 * of compressed malloc traces, including callsite chains.  Integers may be
 * unsigned serial numbers, sizes, or offsets, and require at most 32 bits.
 * They're encoded as follows:
 *   0-127                  0xxxxxxx (binary, one byte)
 *   128-16383              10xxxxxx xxxxxxxx
 *   16384-0x1fffff         110xxxxx xxxxxxxx xxxxxxxx
 *   0x200000-0xfffffff     1110xxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *   0x10000000-0xffffffff  11110000 xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 * Strings are NUL-terminated ASCII.
 *
 * Event Operands (magic TMLog01)
 *   'L' library serial, shared object filename string
 *   'N' method serial, library serial, demangled name string
 *   'S' site serial, parent serial, method serial, calling pc offset
 *   'M' site serial, malloc size
 *   'C' site serial, calloc size
 *   'R' site serial, realloc oldsize, realloc size
 *   'F' site serial, free size
 *
 * Event Operands (magic TMLog02)
 *   'Z' serialized struct tmstats (20 unsigned integers),
 *       maxkids parent callsite serial,
 *       maxstack top callsite serial
 *
 * Event Operands (magic TMLog03)
 *   'T' seconds, microseconds, caption
 *
 * Event Operands (magic TMLog04)
 *   'R' site serial, realloc size, old site serial, realloc oldsize
 *
 * Event Operands (magic TMLog05)
 *   'M' site serial, address, malloc size
 *   'C' site serial, address, calloc size
 *   'R' site serial, address, realloc size, old site serial,
 *         old address, old size
 *   'F' site serial, address, free size
 *
 * Event Operands (magic TMLog06)
 *   'M' site serial, interval time (end), address, malloc size
 *   'C' site serial, interval time (end), address, calloc size
 *   'R' site serial, interval time (end), address, realloc size,
 *         old site serial, old address, old size
 *   'F' site serial, interval time (end), address, free size
 *
 * Event Operands (magic TMLog07)
 *   'M' site serial, interval time (start), duration, address, malloc size
 *   'C' site serial, interval time (start), duration, address, calloc size
 *   'R' site serial, interval time (start), duration, address, realloc size,
 *         old site serial, old address, old size
 *   'F' site serial, interval time (start), duration, address, free size
 *
 * Event Operands (magic TMLog08)
 *   'G' filename serial, source filename string.
 *   'N' method serial, library serial, source filename serial,
 *         source file linenumber, demangled name string
 *
 * See tools/trace-malloc/bloatblame.c for an example log-file reader.
 */
#define TM_EVENT_LIBRARY        'L'
#define TM_EVENT_FILENAME       'G'
#define TM_EVENT_METHOD         'N'
#define TM_EVENT_CALLSITE       'S'
#define TM_EVENT_MALLOC         'M'
#define TM_EVENT_CALLOC         'C'
#define TM_EVENT_REALLOC        'R'
#define TM_EVENT_FREE           'F'
#define TM_EVENT_STATS          'Z'
#define TM_EVENT_TIMESTAMP      'T'

PR_EXTERN(void) NS_TraceMallocStartup(int logfd);

/**
 * Initialize malloc tracing, using the ``standard'' startup arguments.
 */
PR_EXTERN(int) NS_TraceMallocStartupArgs(int argc, char* argv[]);

/**
 * Return PR_TRUE iff |NS_TraceMallocStartup[Args]| has been successfully called.
 */
PR_EXTERN(PRBool) NS_TraceMallocHasStarted(void);

/**
 * Stop all malloc tracing, flushing any buffered events to the logfile.
 */
PR_EXTERN(void) NS_TraceMallocShutdown(void);

/**
 * Disable malloc tracing.
 */
PR_EXTERN(void) NS_TraceMallocDisable(void);

/**
 * Enable malloc tracing.
 */
PR_EXTERN(void) NS_TraceMallocEnable(void);

/**
 * Change the log file descriptor, flushing any buffered output to the old
 * fd, and writing NS_TRACE_MALLOC_MAGIC to the new file if it is zero length.
 * Return the old fd, so the caller can swap open fds.  Return -2 on failure,
 * which means malloc failure.
 */
PR_EXTERN(int) NS_TraceMallocChangeLogFD(int fd);

/**
 * Close the file descriptor fd and forget any bookkeeping associated with it.
 * Do nothing if fd is -1.
 */
PR_EXTERN(void) NS_TraceMallocCloseLogFD(int fd);

/**
 * Emit a timestamp event with the given caption to the current log file. 
 */
PR_EXTERN(void) NS_TraceMallocLogTimestamp(const char *caption);

/**
 * Walk the stack, dumping frames in standard form to ofp.  If skip is 0,
 * exclude the frames for NS_TraceStack and anything it calls to do the walk.
 * If skip is less than 0, include -skip such frames.  If skip is positive,
 * exclude that many frames leading to the call to NS_TraceStack.
 */
PR_EXTERN(void)
NS_TraceStack(int skip, FILE *ofp);

/**
 * Dump a human-readable listing of current allocations and their compressed
 * stack backtraces to the file named by pathname.  Beware this file may have
 * very long lines.
 *
 * Return -1 on error with errno set by the system, 0 on success.
 */
PR_EXTERN(int)
NS_TraceMallocDumpAllocations(const char *pathname);

/**
 * Flush all logfile buffers.
 */
PR_EXTERN(void)
NS_TraceMallocFlushLogfiles(void);

/**
 * Track all realloc and free calls operating on a given allocation.
 */
PR_EXTERN(void)
NS_TrackAllocation(void* ptr, FILE *ofp);

/* opaque type for API */
typedef struct nsTMStackTraceIDStruct *nsTMStackTraceID;

/**
 * Get an identifier for the stack trace of the current thread (to this
 * function's callsite) that can be used to print that stack trace later.
 */
PR_EXTERN(nsTMStackTraceID)
NS_TraceMallocGetStackTrace(void);

/**
 * Print the stack trace identified.
 */
PR_EXTERN(void)
NS_TraceMallocPrintStackTrace(FILE *ofp, nsTMStackTraceID id);

PR_END_EXTERN_C

#endif /* nsTraceMalloc_h___ */
