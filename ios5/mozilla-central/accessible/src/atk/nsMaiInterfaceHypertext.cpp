/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InterfaceInitFuncs.h"

#include "HyperTextAccessible.h"
#include "nsMai.h"
#include "nsMaiHyperlink.h"

extern "C" {

static AtkHyperlink*
getLinkCB(AtkHypertext *aText, gint aLinkIndex)
{
  AccessibleWrap* accWrap = GetAccessibleWrap(ATK_OBJECT(aText));
  if (!accWrap)
    return nsnull;

  HyperTextAccessible* hyperText = accWrap->AsHyperText();
  NS_ENSURE_TRUE(hyperText, nsnull);

  Accessible* hyperLink = hyperText->GetLinkAt(aLinkIndex);
  if (!hyperLink)
    return nsnull;

  AtkObject* hyperLinkAtkObj = AccessibleWrap::GetAtkObject(hyperLink);
  AccessibleWrap* accChild = GetAccessibleWrap(hyperLinkAtkObj);
  NS_ENSURE_TRUE(accChild, nsnull);

  MaiHyperlink *maiHyperlink = accChild->GetMaiHyperlink();
  NS_ENSURE_TRUE(maiHyperlink, nsnull);
  return maiHyperlink->GetAtkHyperlink();
}

static gint
getLinkCountCB(AtkHypertext *aText)
{
  AccessibleWrap* accWrap = GetAccessibleWrap(ATK_OBJECT(aText));
  if (!accWrap)
    return -1;

  HyperTextAccessible* hyperText = accWrap->AsHyperText();
  NS_ENSURE_TRUE(hyperText, -1);

  return hyperText->GetLinkCount();
}

static gint
getLinkIndexCB(AtkHypertext *aText, gint aCharIndex)
{
  AccessibleWrap* accWrap = GetAccessibleWrap(ATK_OBJECT(aText));
  if (!accWrap)
    return -1;

  HyperTextAccessible* hyperText = accWrap->AsHyperText();
  NS_ENSURE_TRUE(hyperText, -1);

  PRInt32 index = -1;
  nsresult rv = hyperText->GetLinkIndexAtOffset(aCharIndex, &index);
  NS_ENSURE_SUCCESS(rv, -1);

  return index;
}
}

void
hypertextInterfaceInitCB(AtkHypertextIface* aIface)
{
  NS_ASSERTION(aIface, "no interface!");
  if (NS_UNLIKELY(!aIface))
    return;

  aIface->get_link = getLinkCB;
  aIface->get_n_links = getLinkCountCB;
  aIface->get_link_index = getLinkIndexCB;
}
