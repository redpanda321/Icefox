/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageAccessible.h"

#include "nsAccUtils.h"
#include "Role.h"
#include "AccIterator.h"
#include "States.h"

#include "imgIContainer.h"
#include "imgIRequest.h"
#include "nsGenericHTMLElement.h"
#include "nsIDocument.h"
#include "nsIImageLoadingContent.h"
#include "nsILink.h"
#include "nsIPresShell.h"
#include "nsIServiceManager.h"
#include "nsIDOMHTMLImageElement.h"
#include "nsPIDOMWindow.h"

using namespace mozilla::a11y;

////////////////////////////////////////////////////////////////////////////////
// ImageAccessible
////////////////////////////////////////////////////////////////////////////////

ImageAccessible::
  ImageAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  LinkableAccessible(aContent, aDoc)
{
  mFlags |= eImageAccessible;
}

NS_IMPL_ISUPPORTS_INHERITED1(ImageAccessible, Accessible,
                             nsIAccessibleImage)

////////////////////////////////////////////////////////////////////////////////
// Accessible public

PRUint64
ImageAccessible::NativeState()
{
  // The state is a bitfield, get our inherited state, then logically OR it with
  // states::ANIMATED if this is an animated image.

  PRUint64 state = LinkableAccessible::NativeState();

  nsCOMPtr<nsIImageLoadingContent> content(do_QueryInterface(mContent));
  nsCOMPtr<imgIRequest> imageRequest;

  if (content)
    content->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                        getter_AddRefs(imageRequest));

  nsCOMPtr<imgIContainer> imgContainer;
  if (imageRequest)
    imageRequest->GetImage(getter_AddRefs(imgContainer));

  if (imgContainer) {
    bool animated;
    imgContainer->GetAnimated(&animated);
    if (animated)
      state |= states::ANIMATED;
  }

  return state;
}

nsresult
ImageAccessible::GetNameInternal(nsAString& aName)
{
  bool hasAltAttrib =
    mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::alt, aName);
  if (!aName.IsEmpty())
    return NS_OK;

  nsresult rv = Accessible::GetNameInternal(aName);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aName.IsEmpty() && hasAltAttrib) {
    // No accessible name but empty 'alt' attribute is present. If further name
    // computation algorithm doesn't provide non empty name then it means
    // an empty 'alt' attribute was used to indicate a decorative image (see
    // nsIAccessible::name attribute for details).
    return NS_OK_EMPTY_NAME;
  }

  return NS_OK;
}

role
ImageAccessible::NativeRole()
{
  return roles::GRAPHIC;
}

////////////////////////////////////////////////////////////////////////////////
// nsIAccessible

PRUint8
ImageAccessible::ActionCount()
{
  PRUint8 actionCount = LinkableAccessible::ActionCount();
  return HasLongDesc() ? actionCount + 1 : actionCount;
}

NS_IMETHODIMP
ImageAccessible::GetActionName(PRUint8 aIndex, nsAString& aName)
{
  aName.Truncate();

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (IsLongDescIndex(aIndex) && HasLongDesc()) {
    aName.AssignLiteral("showlongdesc"); 
    return NS_OK;
  }
  return LinkableAccessible::GetActionName(aIndex, aName);
}

NS_IMETHODIMP
ImageAccessible::DoAction(PRUint8 aIndex)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // Get the long description uri and open in a new window.
  if (!IsLongDescIndex(aIndex))
    return LinkableAccessible::DoAction(aIndex);

  nsCOMPtr<nsIURI> uri = GetLongDescURI();
  if (!uri)
    return NS_ERROR_INVALID_ARG;

  nsCAutoString utf8spec;
  uri->GetSpec(utf8spec);
  NS_ConvertUTF8toUTF16 spec(utf8spec);

  nsIDocument* document = mContent->OwnerDoc();
  nsCOMPtr<nsPIDOMWindow> piWindow = document->GetWindow();
  nsCOMPtr<nsIDOMWindow> win = do_QueryInterface(piWindow);
  NS_ENSURE_STATE(win);

  nsCOMPtr<nsIDOMWindow> tmp;
  return win->Open(spec, EmptyString(), EmptyString(),
                   getter_AddRefs(tmp));
}

////////////////////////////////////////////////////////////////////////////////
// nsIAccessibleImage

NS_IMETHODIMP
ImageAccessible::GetImagePosition(PRUint32 aCoordType, PRInt32* aX, PRInt32* aY)
{
  PRInt32 width, height;
  nsresult rv = GetBounds(aX, aY, &width, &height);
  if (NS_FAILED(rv))
    return rv;

  return nsAccUtils::ConvertScreenCoordsTo(aX, aY, aCoordType, this);
}

NS_IMETHODIMP
ImageAccessible::GetImageSize(PRInt32* aWidth, PRInt32* aHeight)
{
  PRInt32 x, y;
  return GetBounds(&x, &y, aWidth, aHeight);
}

// Accessible
nsresult
ImageAccessible::GetAttributesInternal(nsIPersistentProperties* aAttributes)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsresult rv = LinkableAccessible::GetAttributesInternal(aAttributes);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString src;
  mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::src, src);
  if (!src.IsEmpty())
    nsAccUtils::SetAccAttr(aAttributes, nsGkAtoms::src, src);

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// Private methods

already_AddRefed<nsIURI>
ImageAccessible::GetLongDescURI() const
{
  if (mContent->HasAttr(kNameSpaceID_None, nsGkAtoms::longdesc)) {
    nsGenericHTMLElement* element =
      nsGenericHTMLElement::FromContent(mContent);
    if (element) {
      nsCOMPtr<nsIURI> uri;
      element->GetURIAttr(nsGkAtoms::longdesc, nsnull, getter_AddRefs(uri));
      return uri.forget();
    }
  }

  DocAccessible* document = Document();
  if (document) {
    IDRefsIterator iter(document, mContent, nsGkAtoms::aria_describedby);
    while (nsIContent* target = iter.NextElem()) {
      if ((target->IsHTML(nsGkAtoms::a) || target->IsHTML(nsGkAtoms::area)) &&
          target->HasAttr(kNameSpaceID_None, nsGkAtoms::href)) {
        nsGenericHTMLElement* element =
          nsGenericHTMLElement::FromContent(target);

        nsCOMPtr<nsIURI> uri;
        element->GetURIAttr(nsGkAtoms::href, nsnull, getter_AddRefs(uri));
        return uri.forget();
      }
    }
  }

  return nsnull;
}

bool
ImageAccessible::IsLongDescIndex(PRUint8 aIndex)
{
  return aIndex == LinkableAccessible::ActionCount();
}

