/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is mozilla.org Code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

// Implement shared vtbl methods.

// Keep this in sync with the darwin version.

#include "xptcprivate.h"
#include "xptiprivate.h"

// The Linux/x86-64 ABI passes the first 6 integer parameters and the
// first 8 floating point parameters in registers (rdi, rsi, rdx, rcx,
// r8, r9 and xmm0-xmm7), no stack space is allocated for these by the
// caller.  The rest of the parameters are passed in the callers stack
// area.

const PRUint32 PARAM_BUFFER_COUNT   = 16;
const PRUint32 GPR_COUNT            = 6;
const PRUint32 FPR_COUNT            = 8;

// PrepareAndDispatch() is called by SharedStub() and calls the actual method.
//
// - 'args[]' contains the arguments passed on stack
// - 'gpregs[]' contains the arguments passed in integer registers
// - 'fpregs[]' contains the arguments passed in floating point registers
// 
// The parameters are mapped into an array of type 'nsXPTCMiniVariant'
// and then the method gets called.

extern "C" nsresult
PrepareAndDispatch(nsXPTCStubBase * self, PRUint32 methodIndex,
                   PRUint64 * args, PRUint64 * gpregs, double *fpregs)
{
    nsXPTCMiniVariant paramBuffer[PARAM_BUFFER_COUNT];
    nsXPTCMiniVariant* dispatchParams = NULL;
    const nsXPTMethodInfo* info;
    PRUint32 paramCount;
    PRUint32 i;
    nsresult result = NS_ERROR_FAILURE;

    NS_ASSERTION(self,"no self");

    self->mEntry->GetMethodInfo(PRUint16(methodIndex), &info);
    NS_ASSERTION(info,"no method info");
    if (!info)
        return NS_ERROR_UNEXPECTED;

    paramCount = info->GetParamCount();

    // setup variant array pointer
    if (paramCount > PARAM_BUFFER_COUNT)
        dispatchParams = new nsXPTCMiniVariant[paramCount];
    else
        dispatchParams = paramBuffer;

    NS_ASSERTION(dispatchParams,"no place for params");
    if (!dispatchParams)
        return NS_ERROR_OUT_OF_MEMORY;

    PRUint64* ap = args;
    PRUint32 nr_gpr = 1;    // skip one GPR register for 'that'
    PRUint32 nr_fpr = 0;
    PRUint64 value;

    for (i = 0; i < paramCount; i++) {
        const nsXPTParamInfo& param = info->GetParam(i);
        const nsXPTType& type = param.GetType();
        nsXPTCMiniVariant* dp = &dispatchParams[i];
	
        if (!param.IsOut() && type == nsXPTType::T_DOUBLE) {
            if (nr_fpr < FPR_COUNT)
                dp->val.d = fpregs[nr_fpr++];
            else
                dp->val.d = *(double*) ap++;
            continue;
        }
        else if (!param.IsOut() && type == nsXPTType::T_FLOAT) {
            if (nr_fpr < FPR_COUNT)
                // The value in %xmm register is already prepared to
                // be retrieved as a float. Therefore, we pass the
                // value verbatim, as a double without conversion.
                dp->val.d = *(double*) ap++;
            else
                dp->val.f = *(float*) ap++;
            continue;
        }
        else {
            if (nr_gpr < GPR_COUNT)
                value = gpregs[nr_gpr++];
            else
                value = *ap++;
        }

        if (param.IsOut() || !type.IsArithmetic()) {
            dp->val.p = (void*) value;
            continue;
        }

        switch (type) {
        case nsXPTType::T_I8:      dp->val.i8  = (PRInt8)   value; break;
        case nsXPTType::T_I16:     dp->val.i16 = (PRInt16)  value; break;
        case nsXPTType::T_I32:     dp->val.i32 = (PRInt32)  value; break;
        case nsXPTType::T_I64:     dp->val.i64 = (PRInt64)  value; break;
        case nsXPTType::T_U8:      dp->val.u8  = (PRUint8)  value; break;
        case nsXPTType::T_U16:     dp->val.u16 = (PRUint16) value; break;
        case nsXPTType::T_U32:     dp->val.u32 = (PRUint32) value; break;
        case nsXPTType::T_U64:     dp->val.u64 = (PRUint64) value; break;
        case nsXPTType::T_BOOL:    dp->val.b   = (PRBool)   value; break;
        case nsXPTType::T_CHAR:    dp->val.c   = (char)     value; break;
        case nsXPTType::T_WCHAR:   dp->val.wc  = (wchar_t)  value; break;

        default:
            NS_ERROR("bad type");
            break;
        }
    }

    result = self->mOuter->CallMethod((PRUint16) methodIndex, info, dispatchParams);

    if (dispatchParams != paramBuffer)
        delete [] dispatchParams;

    return result;
}

#define STUB_ENTRY(n)

#define SENTINEL_ENTRY(n) \
nsresult nsXPTCStubBase::Sentinel##n() \
{ \
    NS_ERROR("nsXPTCStubBase::Sentinel called"); \
    return NS_ERROR_NOT_IMPLEMENTED; \
}

#include "xptcstubsdef.inc"
