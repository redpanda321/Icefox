/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsUCConstructors.h"
#include "nsUnicodeToAdobeEuro.h"

//----------------------------------------------------------------------
// Global functions and data [declaration]

static const PRUint16 g_ufMappingTable[] = {
/* "adobeeuro.uf" was generated like this:
 * % echo '0xA4    0x20AC  #    EURO CURRENCY SIGN' | ./umaptable -uf >adobeeuro.uf
 */
#include "adobeeuro.uf"
};

nsresult
nsUnicodeToAdobeEuroConstructor(nsISupports *aOuter, REFNSIID aIID,
                                void **aResult) 
{
  return CreateTableEncoder(u1ByteCharset,
                            (uMappingTable*) &g_ufMappingTable, 1,
                            aOuter, aIID, aResult);
}
