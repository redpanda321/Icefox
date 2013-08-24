/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "inLayoutUtils.h"

#include "nsIDocument.h"
#include "nsIContent.h"
#include "nsIContentViewer.h"
#include "nsPIDOMWindow.h"
#include "nsIDocShell.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"

///////////////////////////////////////////////////////////////////////////////

nsIDOMWindow*
inLayoutUtils::GetWindowFor(nsIDOMNode* aNode)
{
  nsCOMPtr<nsIDOMDocument> doc1;
  aNode->GetOwnerDocument(getter_AddRefs(doc1));
  return GetWindowFor(doc1.get());
}

nsIDOMWindow*
inLayoutUtils::GetWindowFor(nsIDOMDocument* aDoc)
{
  nsCOMPtr<nsIDOMWindow> window;
  aDoc->GetDefaultView(getter_AddRefs(window));
  return window;
}

nsIPresShell* 
inLayoutUtils::GetPresShellFor(nsISupports* aThing)
{
  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(aThing);

  nsCOMPtr<nsIPresShell> presShell;
  window->GetDocShell()->GetPresShell(getter_AddRefs(presShell));

  return presShell;
}

/*static*/
nsIFrame*
inLayoutUtils::GetFrameFor(nsIDOMElement* aElement)
{
  nsCOMPtr<nsIContent> content = do_QueryInterface(aElement);
  return content->GetPrimaryFrame();
}

nsEventStateManager*
inLayoutUtils::GetEventStateManagerFor(nsIDOMElement *aElement)
{
  NS_PRECONDITION(aElement, "Passing in a null element is bad");

  nsCOMPtr<nsIDOMDocument> domDoc;
  aElement->GetOwnerDocument(getter_AddRefs(domDoc));
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);

  if (!doc) {
    NS_WARNING("Could not get an nsIDocument!");
    return nsnull;
  }

  nsIPresShell *shell = doc->GetShell();
  if (!shell)
    return nsnull;

  return shell->GetPresContext()->EventStateManager();
}

nsBindingManager* 
inLayoutUtils::GetBindingManagerFor(nsIDOMNode* aNode)
{
  nsCOMPtr<nsIDOMDocument> domdoc;
  aNode->GetOwnerDocument(getter_AddRefs(domdoc));
  if (domdoc) {
    nsCOMPtr<nsIDocument> doc = do_QueryInterface(domdoc);
    return doc->BindingManager();
  }
  
  return nsnull;
}

nsIDOMDocument*
inLayoutUtils::GetSubDocumentFor(nsIDOMNode* aNode)
{
  nsCOMPtr<nsIContent> content = do_QueryInterface(aNode);
  if (content) {
    nsCOMPtr<nsIDocument> doc = content->GetDocument();
    if (doc) {
      nsCOMPtr<nsIDOMDocument> domdoc(do_QueryInterface(doc->GetSubDocumentFor(content)));

      return domdoc;
    }
  }
  
  return nsnull;
}

nsIDOMNode*
inLayoutUtils::GetContainerFor(nsIDOMDocument* aDoc)
{
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(aDoc);
  if (!doc) return nsnull;

  nsPIDOMWindow *pwin = doc->GetWindow();
  if (!pwin) return nsnull;

  return pwin->GetFrameElementInternal();
}

