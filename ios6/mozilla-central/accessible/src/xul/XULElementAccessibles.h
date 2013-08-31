/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_a11y_XULElementAccessibles_h__
#define mozilla_a11y_XULElementAccessibles_h__

#include "BaseAccessibles.h"
#include "HyperTextAccessibleWrap.h"

namespace mozilla {
namespace a11y {

/**
 * Used for XUL description and label elements.
 */
class XULLabelAccessible : public HyperTextAccessibleWrap
{
public:
  XULLabelAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // Accessible
  virtual a11y::role NativeRole();
  virtual uint64_t NativeState();
  virtual Relation RelationByType(uint32_t aRelationType);

protected:
  // Accessible
  virtual ENameValueFlag NativeName(nsString& aName) MOZ_OVERRIDE;
};

/**
 * Used for XUL tooltip element.
 */
class XULTooltipAccessible : public LeafAccessible
{

public:
  XULTooltipAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // Accessible
  virtual a11y::role NativeRole();
  virtual uint64_t NativeState();
};

class XULLinkAccessible : public HyperTextAccessibleWrap
{

public:
  XULLinkAccessible(nsIContent* aContent, DocAccessible* aDoc);

  NS_DECL_ISUPPORTS_INHERITED

  // nsIAccessible
  NS_IMETHOD GetActionName(uint8_t aIndex, nsAString& aName);
  NS_IMETHOD DoAction(uint8_t aIndex);

  // Accessible
  virtual void Value(nsString& aValue);
  virtual a11y::role NativeRole();
  virtual uint64_t NativeLinkState() const;

  // ActionAccessible
  virtual uint8_t ActionCount();

  // HyperLinkAccessible
  virtual bool IsLink();
  virtual uint32_t StartOffset();
  virtual uint32_t EndOffset();
  virtual already_AddRefed<nsIURI> AnchorURIAt(uint32_t aAnchorIndex);

protected:
  // Accessible
  virtual ENameValueFlag NativeName(nsString& aName) MOZ_OVERRIDE;

  enum { eAction_Jump = 0 };

};

} // namespace a11y
} // namespace mozilla

#endif
