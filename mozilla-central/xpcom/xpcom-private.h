/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* The following defines are only used by the xpcom implementation */

#ifndef _XPCOM_PRIVATE_H_
#define _XPCOM_PRIVATE_H_

/* Define if getpagesize() is available */
#define HAVE_GETPAGESIZE 1

/* Define if iconv() is available */
/* #undef HAVE_ICONV */

/* Define if iconv() supports const input */
/* #undef HAVE_ICONV_WITH_CONST_INPUT */

/* Define if mbrtowc() is available */
#define HAVE_MBRTOWC 1

/* Define if wcrtomb() is available */
#define HAVE_WCRTOMB 1

/* Define if statvfs64() is available */
/* #undef HAVE_STATVFS64 */

/* Define if statvfs() is available */
#define HAVE_STATVFS 1

/* Define if statfs64() is available */
#define HAVE_STATFS64 1

/* Define if statfs() is available */
#define HAVE_STATFS 1

/* Define if <sys/statvfs.h> is present */
#define HAVE_SYS_STATVFS_H 1

/* Define if <sys/statfs.h> is present */
/* #undef HAVE_SYS_STATFS_H */

/* Define if <sys/vfs.h> is present */
/* #undef HAVE_SYS_VFS_H */

/* Define if <sys/mount.h> is present */
#define HAVE_SYS_MOUNT_H 1

#endif /* _XPCOM_PRIVATE_H_ */

