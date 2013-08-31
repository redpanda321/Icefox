/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/DOMImplementation.h"

#include "mozilla/dom/DOMImplementationBinding.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfoID.h"
#include "nsIDOMDocumentType.h"

namespace mozilla {
namespace dom {

// QueryInterface implementation for DOMImplementation
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DOMImplementation)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsIDOMDOMImplementation)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_1(DOMImplementation, mOwner)

NS_IMPL_CYCLE_COLLECTING_ADDREF(DOMImplementation)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DOMImplementation)

JSObject*
DOMImplementation::WrapObject(JSContext* aCx, JSObject* aScope,
                              bool* aTriedToWrap)
{
  return DOMImplementationBinding::Wrap(aCx, aScope, this, aTriedToWrap);
}

bool
DOMImplementation::HasFeature(const nsAString& aFeature,
                              const nsAString& aVersion)
{
  return nsContentUtils::InternalIsSupported(
           static_cast<nsIDOMDOMImplementation*>(this),
           aFeature, aVersion);
}

NS_IMETHODIMP
DOMImplementation::HasFeature(const nsAString& aFeature,
                              const nsAString& aVersion,
                              bool* aReturn)
{
  *aReturn = HasFeature(aFeature, aVersion);
  return NS_OK;
}

already_AddRefed<nsIDOMDocumentType>
DOMImplementation::CreateDocumentType(const nsAString& aQualifiedName,
                                      const nsAString& aPublicId,
                                      const nsAString& aSystemId,
                                      ErrorResult& aRv)
{
  nsCOMPtr<nsIDOMDocumentType> doctype;
  aRv = CreateDocumentType(aQualifiedName, aPublicId, aSystemId,
                           getter_AddRefs(doctype));
  return doctype.forget();
}

NS_IMETHODIMP
DOMImplementation::CreateDocumentType(const nsAString& aQualifiedName,
                                      const nsAString& aPublicId,
                                      const nsAString& aSystemId,
                                      nsIDOMDocumentType** aReturn)
{
  *aReturn = nullptr;
  NS_ENSURE_STATE(mOwner);

  nsresult rv = nsContentUtils::CheckQName(aQualifiedName);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIAtom> name = do_GetAtom(aQualifiedName);
  NS_ENSURE_TRUE(name, NS_ERROR_OUT_OF_MEMORY);

  // Indicate that there is no internal subset (not just an empty one)
  return NS_NewDOMDocumentType(aReturn, mOwner->NodeInfoManager(),
                               name, aPublicId,
                               aSystemId, NullString());
}

nsresult
DOMImplementation::CreateDocument(const nsAString& aNamespaceURI,
                                  const nsAString& aQualifiedName,
                                  nsIDOMDocumentType* aDoctype,
                                  nsIDocument** aDocument,
                                  nsIDOMDocument** aDOMDocument)
{
  *aDocument = nullptr;
  *aDOMDocument = nullptr;

  nsresult rv;
  if (!aQualifiedName.IsEmpty()) {
    const nsAFlatString& qName = PromiseFlatString(aQualifiedName);
    const PRUnichar *colon;
    rv = nsContentUtils::CheckQName(qName, true, &colon);
    NS_ENSURE_SUCCESS(rv, rv);

    if (colon &&
        (DOMStringIsNull(aNamespaceURI) ||
         (Substring(qName.get(), colon).EqualsLiteral("xml") &&
          !aNamespaceURI.EqualsLiteral("http://www.w3.org/XML/1998/namespace")))) {
      return NS_ERROR_DOM_NAMESPACE_ERR;
    }
  }
  else if (DOMStringIsNull(aQualifiedName) &&
           !DOMStringIsNull(aNamespaceURI)) {
    return NS_ERROR_DOM_NAMESPACE_ERR;
  }

  nsCOMPtr<nsIScriptGlobalObject> scriptHandlingObject =
    do_QueryReferent(mScriptObject);

  NS_ENSURE_STATE(!mScriptObject || scriptHandlingObject);

  nsCOMPtr<nsIDOMDocument> document;

  rv = NS_NewDOMDocument(getter_AddRefs(document),
                         aNamespaceURI, aQualifiedName, aDoctype,
                         mDocumentURI, mBaseURI,
                         mOwner->NodePrincipal(),
                         true, scriptHandlingObject,
                         DocumentFlavorLegacyGuess);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDocument> doc = do_QueryInterface(document);
  doc->SetReadyStateInternal(nsIDocument::READYSTATE_COMPLETE);

  doc.forget(aDocument);
  document.forget(aDOMDocument);
  return NS_OK;
}

already_AddRefed<nsIDocument>
DOMImplementation::CreateDocument(const nsAString& aNamespaceURI,
                                  const nsAString& aQualifiedName,
                                  nsIDOMDocumentType* aDoctype,
                                  ErrorResult& aRv)
{
  nsCOMPtr<nsIDocument> document;
  nsCOMPtr<nsIDOMDocument> domDocument;
  aRv = CreateDocument(aNamespaceURI, aQualifiedName, aDoctype,
                       getter_AddRefs(document), getter_AddRefs(domDocument));
  return document.forget();
}

NS_IMETHODIMP
DOMImplementation::CreateDocument(const nsAString& aNamespaceURI,
                                  const nsAString& aQualifiedName,
                                  nsIDOMDocumentType* aDoctype,
                                  nsIDOMDocument** aReturn)
{
  nsCOMPtr<nsIDocument> document;
  return CreateDocument(aNamespaceURI, aQualifiedName, aDoctype,
                        getter_AddRefs(document), aReturn);
}

nsresult
DOMImplementation::CreateHTMLDocument(const nsAString& aTitle,
                                      nsIDocument** aDocument,
                                      nsIDOMDocument** aDOMDocument)
{
  *aDocument = nullptr;
  *aDOMDocument = nullptr;

  NS_ENSURE_STATE(mOwner);

  nsCOMPtr<nsIDOMDocumentType> doctype;
  // Indicate that there is no internal subset (not just an empty one)
  nsresult rv = NS_NewDOMDocumentType(getter_AddRefs(doctype),
                                      mOwner->NodeInfoManager(),
                                      nsGkAtoms::html, // aName
                                      EmptyString(), // aPublicId
                                      EmptyString(), // aSystemId
                                      NullString()); // aInternalSubset
  NS_ENSURE_SUCCESS(rv, rv);


  nsCOMPtr<nsIScriptGlobalObject> scriptHandlingObject =
    do_QueryReferent(mScriptObject);

  NS_ENSURE_STATE(!mScriptObject || scriptHandlingObject);

  nsCOMPtr<nsIDOMDocument> document;
  rv = NS_NewDOMDocument(getter_AddRefs(document),
                         EmptyString(), EmptyString(),
                         doctype, mDocumentURI, mBaseURI,
                         mOwner->NodePrincipal(),
                         true, scriptHandlingObject,
                         DocumentFlavorLegacyGuess);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(document);

  nsCOMPtr<nsIContent> root;
  rv = doc->CreateElem(NS_LITERAL_STRING("html"), NULL, kNameSpaceID_XHTML,
                       getter_AddRefs(root));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = doc->AppendChildTo(root, false);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIContent> head;
  rv = doc->CreateElem(NS_LITERAL_STRING("head"), NULL, kNameSpaceID_XHTML,
                       getter_AddRefs(head));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = root->AppendChildTo(head, false);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIContent> title;
  rv = doc->CreateElem(NS_LITERAL_STRING("title"), NULL, kNameSpaceID_XHTML,
                       getter_AddRefs(title));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = head->AppendChildTo(title, false);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIContent> titleText;
  rv = NS_NewTextNode(getter_AddRefs(titleText), doc->NodeInfoManager());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = titleText->SetText(aTitle, false);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = title->AppendChildTo(titleText, false);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIContent> body;
  rv = doc->CreateElem(NS_LITERAL_STRING("body"), NULL, kNameSpaceID_XHTML,
                       getter_AddRefs(body));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = root->AppendChildTo(body, false);
  NS_ENSURE_SUCCESS(rv, rv);

  doc->SetReadyStateInternal(nsIDocument::READYSTATE_COMPLETE);

  doc.forget(aDocument);
  document.forget(aDOMDocument);
  return NS_OK;
}

already_AddRefed<nsIDocument>
DOMImplementation::CreateHTMLDocument(const nsAString& aTitle,
                                      ErrorResult& aRv)
{
  nsCOMPtr<nsIDocument> document;
  nsCOMPtr<nsIDOMDocument> domDocument;
  aRv = CreateHTMLDocument(aTitle, getter_AddRefs(document),
                           getter_AddRefs(domDocument));
  return document.forget();
}

NS_IMETHODIMP
DOMImplementation::CreateHTMLDocument(const nsAString& aTitle,
                                      nsIDOMDocument** aReturn)
{
  nsCOMPtr<nsIDocument> document;
  return CreateHTMLDocument(aTitle, getter_AddRefs(document), aReturn);
}

} // namespace dom
} // namespace mozilla
