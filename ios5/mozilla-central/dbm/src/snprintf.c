#ifndef HAVE_SNPRINTF

#include <sys/types.h>
#include <stddef.h>
#include <stdio.h>

#ifdef HAVE_SYS_CDEFS_H
#include <sys/cdefs.h>
#else
#include "cdefs.h"
#endif

#include "prtypes.h"

#include <ncompat.h>

#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

int
#ifdef __STDC__
snprintf(char *str, size_t n, const char *fmt, ...)
#else
snprintf(str, n, fmt, va_alist)
	char *str;
	size_t n;
	const char *fmt;
	va_dcl
#endif
{
	va_list ap;
#ifdef VSPRINTF_CHARSTAR
	char *rp;
#else
	int rval;
#endif
#ifdef __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
#ifdef VSPRINTF_CHARSTAR
	rp = vsprintf(str, fmt, ap);
	va_end(ap);
	return (strlen(rp));
#else
	rval = vsprintf(str, fmt, ap);
	va_end(ap);
	return (rval);
#endif
}

int
vsnprintf(str, n, fmt, ap)
	char *str;
	size_t n;
	const char *fmt;
	va_list ap;
{
#ifdef VSPRINTF_CHARSTAR
	return (strlen(vsprintf(str, fmt, ap)));
#else
	return (vsprintf(str, fmt, ap));
#endif
}

#endif /* HAVE_SNPRINTF */

/* Some compilers don't like an empty source file. */
static int dummy = 0;
