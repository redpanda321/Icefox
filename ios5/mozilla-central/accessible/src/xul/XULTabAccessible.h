/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_a11y_XULTabAccessible_h__
#define mozilla_a11y_XULTabAccessible_h__

// NOTE: alphabetically ordered
#include "XULMenuAccessible.h"
#include "XULSelectControlAccessible.h"

namespace mozilla {
namespace a11y {

/**
 * An individual tab, xul:tab element.
 */
class XULTabAccessible : public AccessibleWrap
{
public:
  enum { eAction_Switch = 0 };

  XULTabAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // nsIAccessible
  NS_IMETHOD GetActionName(PRUint8 aIndex, nsAString& aName);
  NS_IMETHOD DoAction(PRUint8 index);

  // Accessible
  virtual a11y::role NativeRole();
  virtual PRUint64 NativeState();
  virtual PRUint64 NativeInteractiveState() const;
  virtual Relation RelationByType(PRUint32 aType);

  // ActionAccessible
  virtual PRUint8 ActionCount();
};


/**
 * A container of tab objects, xul:tabs element.
 */
class XULTabsAccessible : public XULSelectControlAccessible
{
public:
  XULTabsAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // Accessible
  virtual void Value(nsString& aValue);
  virtual nsresult GetNameInternal(nsAString& aName);
  virtual a11y::role NativeRole();

  // ActionAccessible
  virtual PRUint8 ActionCount();
};


/**
 * A container of tab panels, xul:tabpanels element.
 */
class XULDeckAccessible : public AccessibleWrap
{
public:
  XULDeckAccessible(nsIContent* aContent, DocAccessible* aDoc) :
    AccessibleWrap(aContent, aDoc)
    { mFlags |= eXULDeckAccessible; }

  // Accessible
  virtual a11y::role NativeRole();
};


/**
 * A tabpanel object, child elements of xul:tabpanels element. Note,the object
 * is created from nsAccessibilityService::GetAccessibleForDeckChildren()
 * method and we do not use nsIAccessibleProvider interface here because
 * all children of xul:tabpanels element acts as xul:tabpanel element.
 *
 * XXX: we need to move the class logic into generic class since
 * for example we do not create instance of this class for XUL textbox used as
 * a tabpanel.
 */
class XULTabpanelAccessible : public AccessibleWrap
{
public:
  XULTabpanelAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // Accessible
  virtual a11y::role NativeRole();
  virtual Relation RelationByType(PRUint32 aType);
};

} // namespace a11y
} // namespace mozilla

#endif

