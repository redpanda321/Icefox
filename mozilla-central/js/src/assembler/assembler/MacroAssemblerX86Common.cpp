/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "assembler/wtf/Platform.h"

/* SSE checks only make sense on Intel platforms. */
#if WTF_CPU_X86 || WTF_CPU_X86_64

#include "MacroAssemblerX86Common.h"

using namespace JSC;
MacroAssemblerX86Common::SSECheckState MacroAssemblerX86Common::s_sseCheckState = NotCheckedSSE;

#endif /* WTF_CPU_X86 || WTF_CPU_X86_64 */

