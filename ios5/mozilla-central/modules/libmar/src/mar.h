/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MAR_H__
#define MAR_H__

/* We use NSPR here just to import the definition of PRUint32 */
#include "prtypes.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ProductInformationBlock {
  const char *MARChannelID;
  const char *productVersion;
};

/**
 * The MAR item data structure.
 */
typedef struct MarItem_ {
  struct MarItem_ *next;  /* private field */
  PRUint32 offset;        /* offset into archive */
  PRUint32 length;        /* length of data in bytes */
  PRUint32 flags;         /* contains file mode bits */
  char name[1];           /* file path */
} MarItem;

#define TABLESIZE 256

struct MarFile_ {
  FILE *fp;
  MarItem *item_table[TABLESIZE];
};

typedef struct MarFile_ MarFile;

/**
 * Signature of callback function passed to mar_enum_items.
 * @param mar       The MAR file being visited.
 * @param item      The MAR item being visited.
 * @param data      The data parameter passed by the caller of mar_enum_items.
 * @return          A non-zero value to stop enumerating.
 */
typedef int (* MarItemCallback)(MarFile *mar, const MarItem *item, void *data);

/**
 * Open a MAR file for reading.
 * @param path      Specifies the path to the MAR file to open.  This path must
 *                  be compatible with fopen.
 * @return          NULL if an error occurs.
 */
MarFile *mar_open(const char *path);

#ifdef XP_WIN
MarFile *mar_wopen(const PRUnichar *path);
#endif

/**
 * Close a MAR file that was opened using mar_open.
 * @param mar       The MarFile object to close.
 */
void mar_close(MarFile *mar);

/**
 * Find an item in the MAR file by name.
 * @param mar       The MarFile object to query.
 * @param item      The name of the item to query.
 * @return          A const reference to a MAR item or NULL if not found.
 */
const MarItem *mar_find_item(MarFile *mar, const char *item);

/**
 * Enumerate all MAR items via callback function.
 * @param mar       The MAR file to enumerate.
 * @param callback  The function to call for each MAR item.
 * @param data      A caller specified value that is passed along to the
 *                  callback function.
 * @return          0 if the enumeration ran to completion.  Otherwise, any
 *                  non-zero return value from the callback is returned.
 */
int mar_enum_items(MarFile *mar, MarItemCallback callback, void *data);

/**
 * Read from MAR item at given offset up to bufsize bytes.
 * @param mar       The MAR file to read.
 * @param item      The MAR item to read.
 * @param offset    The byte offset relative to the start of the item.
 * @param buf       A pointer to a buffer to copy the data into.
 * @param bufsize   The length of the buffer to copy the data into.
 * @return          The number of bytes written or a negative value if an
 *                  error occurs.
 */
int mar_read(MarFile *mar, const MarItem *item, int offset, char *buf,
             int bufsize);

/**
 * Create a MAR file from a set of files.
 * @param dest      The path to the file to create.  This path must be
 *                  compatible with fopen.
 * @param numfiles  The number of files to store in the archive.
 * @param files     The list of null-terminated file paths.  Each file
 *                  path must be compatible with fopen.
 * @param infoBlock The information to store in the product information block.
 * @return          A non-zero value if an error occurs.
 */
int mar_create(const char *dest, 
               int numfiles, 
               char **files, 
               struct ProductInformationBlock *infoBlock);

/**
 * Extract a MAR file to the current working directory.
 * @param path      The path to the MAR file to extract.  This path must be
 *                  compatible with fopen.
 * @return          A non-zero value if an error occurs.
 */
int mar_extract(const char *path);

/**
 * Verifies the embedded signature for the specified mar file.
 * We do not check that the certificate was issued by any trusted authority. 
 * We assume it to be self-signed.  We do not check whether the certificate 
 * is valid for this usage.
 * 
 * @param mar            The already opened MAR file.
 * @param certData       The certificate file data.
 * @param sizeOfCertData The size of the cert data.
 * @return 0 on success
 *         a negative number if there was an error
 *         a positive number if the signature does not verify
 */
#ifdef XP_WIN
int mar_verify_signatureW(MarFile *mar, 
                          const char *certData,
                          PRUint32 sizeOfCertData);
#endif

/** 
 * Reads the product info block from the MAR file's additional block section.
 * The caller is responsible for freeing the fields in infoBlock
 * if the return is successful.
 *
 * @param infoBlock Out parameter for where to store the result to
 * @return 0 on success, -1 on failure
*/
int
mar_read_product_info_block(MarFile *mar, 
                            struct ProductInformationBlock *infoBlock);

#ifdef __cplusplus
}
#endif

#endif  /* MAR_H__ */
