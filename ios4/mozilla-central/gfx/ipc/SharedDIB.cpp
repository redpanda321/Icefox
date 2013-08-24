/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Jim Mathies <jmathies@mozilla.com>
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

#include "SharedDIB.h"

namespace mozilla {
namespace gfx {

SharedDIB::SharedDIB() :
  mShMem(nsnull)
{
}

SharedDIB::~SharedDIB()
{
  Close();
}

nsresult
SharedDIB::Create(PRUint32 aSize)
{
  Close();

  mShMem = new base::SharedMemory();
  if (!mShMem || !mShMem->Create("", false, false, aSize))
    return NS_ERROR_OUT_OF_MEMORY;

  // Map the entire section
  if (!mShMem->Map(0))
    return NS_ERROR_FAILURE;

  return NS_OK;
}

bool
SharedDIB::IsValid()
{
  if (!mShMem)
    return false;

  return mShMem->IsHandleValid(mShMem->handle());
}

nsresult
SharedDIB::Close()
{
  if (mShMem)
    delete mShMem;

  mShMem = nsnull;

  return NS_OK;
}

nsresult
SharedDIB::Attach(Handle aHandle, PRUint32 aSize)
{
  Close();

  mShMem = new base::SharedMemory(aHandle, false);
  if(!mShMem)
    return NS_ERROR_OUT_OF_MEMORY;

  if (!mShMem->Map(aSize))
    return NS_ERROR_FAILURE;

  return NS_OK;
}

nsresult
SharedDIB::ShareToProcess(base::ProcessHandle aChildProcess, Handle *aChildHandle)
{
  if (!mShMem)
    return NS_ERROR_UNEXPECTED;

  if (!mShMem->ShareToProcess(aChildProcess, aChildHandle))
    return NS_ERROR_UNEXPECTED;

  return NS_OK;
}

} // gfx
} // mozilla
