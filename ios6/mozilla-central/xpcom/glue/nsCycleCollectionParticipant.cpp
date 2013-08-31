/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCycleCollectionParticipant.h"
#include "nsCOMPtr.h"

#ifdef MOZILLA_INTERNAL_API
#include "nsString.h"
#else
#include "nsStringAPI.h"
#endif

void
nsScriptObjectTracer::NoteJSChild(void *aScriptThing, const char *name,
                                  void *aClosure)
{
  nsCycleCollectionTraversalCallback *cb =
    static_cast<nsCycleCollectionTraversalCallback*>(aClosure);
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*cb, name);
  cb->NoteJSChild(aScriptThing);
}

nsresult
nsXPCOMCycleCollectionParticipant::RootImpl(void *p)
{
    nsISupports *s = static_cast<nsISupports*>(p);
    NS_ADDREF(s);
    return NS_OK;
}

nsresult
nsXPCOMCycleCollectionParticipant::UnrootImpl(void *p)
{
    nsISupports *s = static_cast<nsISupports*>(p);
    NS_RELEASE(s);
    return NS_OK;
}

// We define a default trace function because some participants don't need
// to trace anything, so it is okay for them not to define one.
NS_IMETHODIMP_(void)
nsXPCOMCycleCollectionParticipant::TraceImpl(void *p, TraceCallback cb,
                                             void *closure)
{
}

bool
nsXPCOMCycleCollectionParticipant::CheckForRightISupports(nsISupports *s)
{
    nsISupports* foo;
    s->QueryInterface(NS_GET_IID(nsCycleCollectionISupports),
                      reinterpret_cast<void**>(&foo));
    return s == foo;
}

void
CycleCollectionNoteEdgeNameImpl(nsCycleCollectionTraversalCallback& aCallback,
                                const char* aName,
                                uint32_t aFlags)
{
  nsAutoCString arrayEdgeName(aName);
  if (aFlags & CycleCollectionEdgeNameArrayFlag) {
    arrayEdgeName.AppendLiteral("[i]");
  }
  aCallback.NoteNextEdgeName(arrayEdgeName.get());
}
