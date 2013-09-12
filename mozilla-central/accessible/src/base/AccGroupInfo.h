/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AccGroupInfo_h_
#define AccGroupInfo_h_

#include "Accessible-inl.h"
#include "nsAccUtils.h"

namespace mozilla {
namespace a11y {

/**
 * Calculate and store group information.
 */
class AccGroupInfo
{
public:
  AccGroupInfo(Accessible* aItem, mozilla::a11y::role aRole);
  ~AccGroupInfo() { MOZ_COUNT_DTOR(AccGroupInfo); }

  int32_t PosInSet() const { return mPosInSet; }
  uint32_t SetSize() const { return mSetSize; }
  Accessible* ConceptualParent() const { return mParent; }

  /**
   * Create group info.
   */
  static AccGroupInfo* CreateGroupInfo(Accessible* aAccessible)
  {
    mozilla::a11y::role role = aAccessible->Role();
    if (role != mozilla::a11y::roles::ROW &&
        role != mozilla::a11y::roles::GRID_CELL &&
        role != mozilla::a11y::roles::OUTLINEITEM &&
        role != mozilla::a11y::roles::OPTION &&
        role != mozilla::a11y::roles::LISTITEM &&
        role != mozilla::a11y::roles::MENUITEM &&
        role != mozilla::a11y::roles::COMBOBOX_OPTION &&
        role != mozilla::a11y::roles::RICH_OPTION &&
        role != mozilla::a11y::roles::CHECK_RICH_OPTION &&
        role != mozilla::a11y::roles::PARENT_MENUITEM &&
        role != mozilla::a11y::roles::CHECK_MENU_ITEM &&
        role != mozilla::a11y::roles::RADIO_MENU_ITEM &&
        role != mozilla::a11y::roles::RADIOBUTTON &&
        role != mozilla::a11y::roles::PAGETAB)
      return nullptr;

    AccGroupInfo* info = new AccGroupInfo(aAccessible, BaseRole(role));
    return info;
  }

private:
  AccGroupInfo(const AccGroupInfo&);
  AccGroupInfo& operator =(const AccGroupInfo&);

  static mozilla::a11y::role BaseRole(mozilla::a11y::role aRole)
  {
    if (aRole == mozilla::a11y::roles::CHECK_MENU_ITEM ||
        aRole == mozilla::a11y::roles::PARENT_MENUITEM ||
        aRole == mozilla::a11y::roles::RADIO_MENU_ITEM)
      return mozilla::a11y::roles::MENUITEM;

    if (aRole == mozilla::a11y::roles::CHECK_RICH_OPTION)
      return mozilla::a11y::roles::RICH_OPTION;

    return aRole;
  }

  /**
   * Return true if the given parent role is conceptual parent of the given
   * role.
   */
  static bool IsConceptualParent(mozilla::a11y::role aRole,
				 mozilla::a11y::role aParentRole);

  uint32_t mPosInSet;
  uint32_t mSetSize;
  Accessible* mParent;
};

} // namespace mozilla
} // namespace a11y

#endif
