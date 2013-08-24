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
 * The Original Code is Mozilla IPCShell.
 *
 * The Initial Developer of the Original Code is
 *   Ben Turner <bent.mozilla@gmail.com>.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "TestShellChild.h"
#include "mozilla/jsipc/ContextWrapperChild.h"

using mozilla::ipc::TestShellChild;
using mozilla::ipc::PTestShellCommandChild;
using mozilla::ipc::XPCShellEnvironment;
using mozilla::jsipc::PContextWrapperChild;
using mozilla::jsipc::ContextWrapperChild;

TestShellChild::TestShellChild()
: mXPCShell(XPCShellEnvironment::CreateEnvironment())
{
}

bool
TestShellChild::RecvExecuteCommand(const nsString& aCommand)
{
  if (mXPCShell->IsQuitting()) {
    NS_WARNING("Commands sent after quit command issued!");
    return false;
  }

  return mXPCShell->EvaluateString(aCommand);
}

PTestShellCommandChild*
TestShellChild::AllocPTestShellCommand(const nsString& aCommand)
{
  return new PTestShellCommandChild();
}

bool
TestShellChild::DeallocPTestShellCommand(PTestShellCommandChild* aCommand)
{
  delete aCommand;
  return true;
}

bool
TestShellChild::RecvPTestShellCommandConstructor(PTestShellCommandChild* aActor,
                                                 const nsString& aCommand)
{
  if (mXPCShell->IsQuitting()) {
    NS_WARNING("Commands sent after quit command issued!");
    return false;
  }

  nsString response;
  if (!mXPCShell->EvaluateString(aCommand, &response)) {
    return false;
  }

  return PTestShellCommandChild::Send__delete__(aActor, response);
}

PContextWrapperChild*
TestShellChild::AllocPContextWrapper()
{
  JSContext* cx;
  if (mXPCShell && (cx = mXPCShell->GetContext())) {
    return new ContextWrapperChild(cx);
  }
  return NULL;
}

bool
TestShellChild::DeallocPContextWrapper(PContextWrapperChild* actor)
{
  delete actor;
  return true;
}
