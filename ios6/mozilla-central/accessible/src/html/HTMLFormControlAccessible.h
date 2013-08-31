/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_A11Y_HTMLFormControlAccessible_H_
#define MOZILLA_A11Y_HTMLFormControlAccessible_H_

#include "FormControlAccessible.h"
#include "HyperTextAccessibleWrap.h"

namespace mozilla {
namespace a11y {

/**
 * Accessible for HTML progress element.
 */
typedef ProgressMeterAccessible<1> HTMLProgressMeterAccessible;

/**
 * Accessible for HTML input@type="checkbox".
 */
class HTMLCheckboxAccessible : public LeafAccessible
{

public:
  enum { eAction_Click = 0 };

  HTMLCheckboxAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // nsIAccessible
  NS_IMETHOD GetActionName(uint8_t aIndex, nsAString& aName);
  NS_IMETHOD DoAction(uint8_t index);

  // Accessible
  virtual mozilla::a11y::role NativeRole();
  virtual uint64_t NativeState();

  // ActionAccessible
  virtual uint8_t ActionCount();

  // Widgets
  virtual bool IsWidget() const;
};


/**
 * Accessible for HTML input@type="radio" element.
 */
class HTMLRadioButtonAccessible : public RadioButtonAccessible
{

public:
  HTMLRadioButtonAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // Accessible
  virtual uint64_t NativeState();
  virtual void GetPositionAndSizeInternal(int32_t *aPosInSet,
                                          int32_t *aSetSize);
};


/**
 * Accessible for HTML input@type="button", @type="submit", @type="image"
 * and HTML button elements.
 */
class HTMLButtonAccessible : public HyperTextAccessibleWrap
{

public:
  enum { eAction_Click = 0 };

  HTMLButtonAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // nsIAccessible
  NS_IMETHOD GetActionName(uint8_t aIndex, nsAString& aName);
  NS_IMETHOD DoAction(uint8_t index);

  // Accessible
  virtual mozilla::a11y::role NativeRole();
  virtual uint64_t State();
  virtual uint64_t NativeState();

  // ActionAccessible
  virtual uint8_t ActionCount();

  // Widgets
  virtual bool IsWidget() const;

protected:
  // Accessible
  virtual ENameValueFlag NativeName(nsString& aName) MOZ_OVERRIDE;
};


/**
 * Accessible for HTML input@type="text" element.
 */
class HTMLTextFieldAccessible : public HyperTextAccessibleWrap
{

public:
  enum { eAction_Click = 0 };

  HTMLTextFieldAccessible(nsIContent* aContent, DocAccessible* aDoc);

  NS_DECL_ISUPPORTS_INHERITED

  // nsIAccessible
  NS_IMETHOD GetActionName(uint8_t aIndex, nsAString& aName);
  NS_IMETHOD DoAction(uint8_t index);

  // HyperTextAccessible
  virtual already_AddRefed<nsIEditor> GetEditor() const;

  // Accessible
  virtual void Value(nsString& aValue);
  virtual void ApplyARIAState(uint64_t* aState) const;
  virtual mozilla::a11y::role NativeRole();
  virtual uint64_t State();
  virtual uint64_t NativeState();

  // ActionAccessible
  virtual uint8_t ActionCount();

  // Widgets
  virtual bool IsWidget() const;
  virtual Accessible* ContainerWidget() const;

protected:
  // Accessible
  virtual ENameValueFlag NativeName(nsString& aName) MOZ_OVERRIDE;
};


/**
 * Accessible for input@type="file" element.
 */
class HTMLFileInputAccessible : public HyperTextAccessibleWrap
{
public:
  HTMLFileInputAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // Accessible
  virtual mozilla::a11y::role NativeRole();
  virtual nsresult HandleAccEvent(AccEvent* aAccEvent);
};

/**
 * Accessible for HTML fieldset element.
 */
class HTMLGroupboxAccessible : public HyperTextAccessibleWrap
{
public:
  HTMLGroupboxAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // Accessible
  virtual mozilla::a11y::role NativeRole();
  virtual Relation RelationByType(uint32_t aType);

protected:
  // Accessible
  virtual ENameValueFlag NativeName(nsString& aName) MOZ_OVERRIDE;

  // HTMLGroupboxAccessible
  nsIContent* GetLegend();
};


/**
 * Accessible for HTML legend element.
 */
class HTMLLegendAccessible : public HyperTextAccessibleWrap
{
public:
  HTMLLegendAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // Accessible
  virtual mozilla::a11y::role NativeRole();
  virtual Relation RelationByType(uint32_t aType);
};

/**
 * Accessible for HTML5 figure element.
 */
class HTMLFigureAccessible : public HyperTextAccessibleWrap
{
public:
  HTMLFigureAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // Accessible
  virtual already_AddRefed<nsIPersistentProperties> NativeAttributes() MOZ_OVERRIDE;
  virtual mozilla::a11y::role NativeRole();
  virtual Relation RelationByType(uint32_t aType);

protected:
  // Accessible
  virtual ENameValueFlag NativeName(nsString& aName) MOZ_OVERRIDE;

  // HTMLLegendAccessible
  nsIContent* Caption() const;
};


/**
 * Accessible for HTML5 figcaption element.
 */
class HTMLFigcaptionAccessible : public HyperTextAccessibleWrap
{
public:
  HTMLFigcaptionAccessible(nsIContent* aContent, DocAccessible* aDoc);

  // Accessible
  virtual mozilla::a11y::role NativeRole();
  virtual Relation RelationByType(uint32_t aType);
};

} // namespace a11y
} // namespace mozilla

#endif
