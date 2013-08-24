/* -*- Mode: C++; tab-width: 40; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 *   Mozilla Corp
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Andreas Gal <gal@uci.edu>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__i386) || defined(__amd64__)

/*
 * x87 FPU Control Word:
 *
 * 0 -> IM  Invalid Operation
 * 1 -> DM  Denormalized Operand
 * 2 -> ZM  Zero Divide
 * 3 -> OM  Overflow
 * 4 -> UM  Underflow
 * 5 -> PM  Precision
 */
#define FPU_EXCEPTION_MASK 0x3f

/*
 * x86 FPU Status Word:
 *
 * 0..5  ->      Exception flags  (see x86 FPU Control Word)
 * 6     -> SF   Stack Fault
 * 7     -> ES   Error Summary Status
 */
#define FPU_STATUS_FLAGS 0xff

/*
 * MXCSR Control and Status Register:
 *
 * 0..5  ->      Exception flags (see x86 FPU Control Word)
 * 6     -> DAZ  Denormals Are Zero
 * 7..12 ->      Exception mask (see x86 FPU Control Word)
 */
#define SSE_STATUS_FLAGS   FPU_EXCEPTION_MASK
#define SSE_EXCEPTION_MASK (FPU_EXCEPTION_MASK << 7)

#endif
