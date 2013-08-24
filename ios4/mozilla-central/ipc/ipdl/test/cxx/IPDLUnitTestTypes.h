/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 8 -*- */
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
 * The Original Code is Mozilla IPC.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Chris Jones <jones.chris.g@gmail.com>.
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

#ifndef mozilla__ipdltest_IPDLUnitTestTypes_h
#define mozilla__ipdltest_IPDLUnitTestTypes_h

#include "mozilla/ipc/ProtocolUtils.h" // ActorDestroyReason

namespace mozilla {
namespace _ipdltest {

struct DirtyRect
{
  int x; int y; int w; int h;
};

}
}

namespace IPC {
template<>
struct ParamTraits<mozilla::_ipdltest::DirtyRect>
{
  typedef mozilla::_ipdltest::DirtyRect paramType;
  static void Write(Message* aMsg, const paramType& aParam) {
    WriteParam(aMsg, aParam.x);
    WriteParam(aMsg, aParam.y);
    WriteParam(aMsg, aParam.w);
    WriteParam(aMsg, aParam.h);
  }
  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    return (ReadParam(aMsg, aIter, &aResult->x) &&
            ReadParam(aMsg, aIter, &aResult->y) &&
            ReadParam(aMsg, aIter, &aResult->w) &&
            ReadParam(aMsg, aIter, &aResult->h));
  }
};
}


#endif // ifndef mozilla__ipdltest_IPDLUnitTestTypes_h
