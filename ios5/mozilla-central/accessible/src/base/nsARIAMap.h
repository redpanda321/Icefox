/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsARIAMap_H_
#define _nsARIAMap_H_

#include "ARIAStateMap.h"
#include "mozilla/a11y/Role.h"

#include "nsIAtom.h"

class nsIContent;
class nsINode;

////////////////////////////////////////////////////////////////////////////////
// Value constants

/**
 * Used to define if role requires to expose nsIAccessibleValue.
 */
enum EValueRule
{
  /**
   * nsIAccessibleValue isn't exposed.
   */
  eNoValue,

  /**
   * nsIAccessibleValue is implemented, supports value, min and max from
   * aria-valuenow, aria-valuemin and aria-valuemax.
   */
  eHasValueMinMax
};


////////////////////////////////////////////////////////////////////////////////
// Action constants

/**
 * Used to define if the role requires to expose action.
 */
enum EActionRule
{
  eNoAction,
  eActivateAction,
  eClickAction,
  ePressAction,
  eCheckUncheckAction,
  eExpandAction,
  eJumpAction,
  eOpenCloseAction,
  eSelectAction,
  eSortAction,
  eSwitchAction
};


////////////////////////////////////////////////////////////////////////////////
// Live region constants

/**
 * Used to define if role exposes default value of aria-live attribute.
 */
enum ELiveAttrRule
{
  eNoLiveAttr,
  eOffLiveAttr,
  ePoliteLiveAttr
};


////////////////////////////////////////////////////////////////////////////////
// Role constants

/**
 * ARIA role overrides role from native markup.
 */
const bool kUseMapRole = true;

/**
 * ARIA role doesn't override the role from native markup.
 */
const bool kUseNativeRole = false;


////////////////////////////////////////////////////////////////////////////////
// ARIA attribute characteristic masks

/**
 * This mask indicates the attribute should not be exposed as an object
 * attribute via the catch-all logic in Accessible::GetAttributes.
 * This means it either isn't mean't to be exposed as an object attribute, or
 * that it should, but is already handled in other code.
 */
const PRUint8 ATTR_BYPASSOBJ  = 0x0001;

/**
 * This mask indicates the attribute is expected to have an NMTOKEN or bool value.
 * (See for example usage in Accessible::GetAttributes)
 */
const PRUint8 ATTR_VALTOKEN   = 0x0010;

/**
 * Small footprint storage of persistent aria attribute characteristics.
 */
struct nsAttributeCharacteristics
{
  nsIAtom** attributeName;
  const PRUint8 characteristics;
};


////////////////////////////////////////////////////////////////////////////////
// State map entry

/**
 * Used in nsRoleMapEntry.state if no nsIAccessibleStates are automatic for
 * a given role.
 */
#define kNoReqStates 0

enum EDefaultStateRule
{
  //eNoDefaultState,
  eUseFirstState
};

////////////////////////////////////////////////////////////////////////////////
// Role map entry

/**
 * For each ARIA role, this maps the nsIAccessible information.
 */
struct nsRoleMapEntry
{
  /**
   * Return true if matches to the given ARIA role.
   */
  bool Is(nsIAtom* aARIARole) const
    { return *roleAtom == aARIARole; }

  /**
   * Return ARIA role.
   */
  const nsDependentAtomString ARIARoleString() const
    { return nsDependentAtomString(*roleAtom); }

  // ARIA role: string representation such as "button"
  nsIAtom** roleAtom;

  // Role mapping rule: maps to this nsIAccessibleRole
  mozilla::a11y::role role;
  
  // Role rule: whether to use mapped role or native semantics
  bool roleRule;
  
  // Value mapping rule: how to compute nsIAccessible value
  EValueRule valueRule;

  // Action mapping rule, how to expose nsIAccessible action
  EActionRule actionRule;

  // 'live' and 'container-live' object attributes mapping rule: how to expose
  // these object attributes if ARIA 'live' attribute is missed.
  ELiveAttrRule liveAttRule;

  // Automatic state mapping rule: always include in nsIAccessibleStates
  PRUint64 state;   // or kNoReqStates if no nsIAccessibleStates are automatic for this role.

  // ARIA properties supported for this role
  // (in other words, the aria-foo attribute to nsIAccessibleStates mapping rules)
  // Currently you cannot have unlimited mappings, because
  // a variable sized array would not allow the use of
  // C++'s struct initialization feature.
  mozilla::a11y::aria::EStateRule attributeMap1;
  mozilla::a11y::aria::EStateRule attributeMap2;
  mozilla::a11y::aria::EStateRule attributeMap3;
};


////////////////////////////////////////////////////////////////////////////////
// ARIA map

/**
 *  These are currently initialized (hardcoded) in nsARIAMap.cpp, 
 *  and provide the mappings for WAI-ARIA roles and properties using the 
 *  structs defined in this file.
 */
struct nsARIAMap
{
  /**
   * Empty role map entry. Used by accessibility service to create an accessible
   * if the accessible can't use role of used accessible class. For example,
   * it is used for table cells that aren't contained by table.
   */
  static nsRoleMapEntry gEmptyRoleMap;

  /**
   * Map of attribute to attribute characteristics.
   */
  static nsAttributeCharacteristics gWAIUnivAttrMap[];
  static PRUint32 gWAIUnivAttrMapLength;
};

namespace mozilla {
namespace a11y {
namespace aria {

/**
 * Get the role map entry for a given DOM node. This will use the first
 * ARIA role if the role attribute provides a space delimited list of roles.
 *
 * @param aNode  [in] the DOM node to get the role map entry for
 * @return        a pointer to the role map entry for the ARIA role, or nsnull
 *                if none
 */
nsRoleMapEntry* GetRoleMap(nsINode* aNode);

/**
 * Return accessible state from ARIA universal states applied to the given
 * element.
 */
PRUint64 UniversalStatesFor(mozilla::dom::Element* aElement);

} // namespace aria
} // namespace a11y
} // namespace mozilla

#endif
