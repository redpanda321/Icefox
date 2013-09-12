/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CKMD_H
#define CKMD_H

#ifdef DEBUG
static const char CKMD_CVS_ID[] = "@(#) $RCSfile: ckmd.h,v $ $Revision: 1.4 $ $Date: 2012/04/25 14:49:28 $";
#endif /* DEBUG */

/*
 * ckmd.h
 *
 */

NSS_EXTERN NSSCKMDObject *
nssCKMDSessionObject_Create
(
  NSSCKFWToken *fwToken,
  NSSArena *arena,
  CK_ATTRIBUTE_PTR attributes,
  CK_ULONG ulCount,
  CK_RV *pError
);

NSS_EXTERN NSSCKMDFindObjects *
nssCKMDFindSessionObjects_Create
(
  NSSCKFWToken *fwToken,
  CK_ATTRIBUTE_PTR pTemplate,
  CK_ULONG ulCount,
  CK_RV *pError
);

#endif /* CKMD_H */
