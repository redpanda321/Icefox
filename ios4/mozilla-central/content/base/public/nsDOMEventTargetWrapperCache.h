/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
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
 * Wellington Fernando de Macedo.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *    Wellington Fernando de Macedo <wfernandom2004@gmail.com> (original author)
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

#ifndef nsDOMEventTargetWrapperCache_h__
#define nsDOMEventTargetWrapperCache_h__

#include "nsDOMEventTargetHelper.h"
#include "nsWrapperCache.h"
#include "nsIScriptContext.h"


// Base class intended to be used for objets like XMLHttpRequest,
// EventSource and WebSocket.

class nsDOMEventTargetWrapperCache : public nsDOMEventTargetHelper,
                                     public nsWrapperCache
{
public:  
  NS_DECL_ISUPPORTS_INHERITED

  class NS_CYCLE_COLLECTION_INNERCLASS
    : public NS_CYCLE_COLLECTION_CLASSNAME(nsDOMEventTargetHelper)
  {
    NS_IMETHOD RootAndUnlinkJSObjects(void *p);
    NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED_BODY_NO_UNLINK(nsDOMEventTargetWrapperCache,
                                                            nsDOMEventTargetHelper)
    NS_IMETHOD_(void) Trace(void *p, TraceCallback cb, void *closure);
  };
  NS_CYCLE_COLLECTION_PARTICIPANT_INSTANCE
  
  void GetParentObject(nsIScriptGlobalObject **aParentObject)
  {
    if (mOwner) {
      CallQueryInterface(mOwner, aParentObject);
    }
    else {
      *aParentObject = nsnull;
    }
  }

  static nsDOMEventTargetWrapperCache* FromSupports(nsISupports* aSupports)
  {
    nsPIDOMEventTarget* target =
      static_cast<nsPIDOMEventTarget*>(aSupports);
#ifdef DEBUG
    {
      nsCOMPtr<nsPIDOMEventTarget> target_qi =
        do_QueryInterface(aSupports);

      // If this assertion fires the QI implementation for the object in
      // question doesn't use the nsPIDOMEventTarget pointer as the
      // nsISupports pointer. That must be fixed, or we'll crash...
      NS_ASSERTION(target_qi == target, "Uh, fix QI!");
    }
#endif

    return static_cast<nsDOMEventTargetWrapperCache*>(target);
  }

protected:
  nsDOMEventTargetWrapperCache() : nsDOMEventTargetHelper(), nsWrapperCache() {}
  virtual ~nsDOMEventTargetWrapperCache();
};

#endif  // nsDOMEventTargetWrapperCache_h__
