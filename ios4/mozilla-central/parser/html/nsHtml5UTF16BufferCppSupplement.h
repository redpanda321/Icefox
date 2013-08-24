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
 * The Original Code is HTML Parser C++ Translator code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Henri Sivonen <hsivonen@iki.fi>
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

#include "nsTraceRefcnt.h"

nsHtml5UTF16Buffer::nsHtml5UTF16Buffer(PRInt32 size)
  : buffer(new PRUnichar[size]),
    start(0),
    end(0),
    next(nsnull),
    key(nsnull)
{
  MOZ_COUNT_CTOR(nsHtml5UTF16Buffer);
}

nsHtml5UTF16Buffer::nsHtml5UTF16Buffer(void* key)
  : buffer(nsnull),
    start(0),
    end(0),
    next(nsnull),
    key(key)
{
  MOZ_COUNT_CTOR(nsHtml5UTF16Buffer);
}

nsHtml5UTF16Buffer::~nsHtml5UTF16Buffer()
{
  MOZ_COUNT_DTOR(nsHtml5UTF16Buffer);
  delete[] buffer;
}

// Not using macros for AddRef and Release in order to be able to refcount on
// and create on different threads.

nsrefcnt
nsHtml5UTF16Buffer::AddRef()
{
  NS_PRECONDITION(PRInt32(mRefCnt) >= 0, "Illegal refcount.");
  ++mRefCnt;
  NS_LOG_ADDREF(this, mRefCnt, "nsHtml5UTF16Buffer", sizeof(*this));
  return mRefCnt;
}

nsrefcnt
nsHtml5UTF16Buffer::Release()
{
  NS_PRECONDITION(0 != mRefCnt, "Release without AddRef.");
  --mRefCnt;
  NS_LOG_RELEASE(this, mRefCnt, "nsHtml5UTF16Buffer");
  if (mRefCnt == 0) {
    mRefCnt = 1; /* stabilize */
    delete this;
    return 0;
  }
  return mRefCnt;                              
}
