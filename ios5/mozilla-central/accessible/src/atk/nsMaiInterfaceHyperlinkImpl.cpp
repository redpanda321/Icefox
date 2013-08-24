/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InterfaceInitFuncs.h"

#include "nsMaiHyperlink.h"

extern "C" {
static AtkHyperlink*
getHyperlinkCB(AtkHyperlinkImpl* aImpl)
{
  AccessibleWrap* accWrap = GetAccessibleWrap(ATK_OBJECT(aImpl));
  if (!accWrap)
    return nsnull;

  NS_ENSURE_TRUE(accWrap->IsLink(), nsnull);

  MaiHyperlink* maiHyperlink = accWrap->GetMaiHyperlink();
  NS_ENSURE_TRUE(maiHyperlink, nsnull);
  return maiHyperlink->GetAtkHyperlink();
}
}

void
hyperlinkImplInterfaceInitCB(AtkHyperlinkImplIface *aIface)
{
  NS_ASSERTION(aIface, "no interface!");
  if (NS_UNLIKELY(!aIface))
    return;

  aIface->get_hyperlink = getHyperlinkCB;
}
