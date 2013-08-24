/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Doug Turner <dougt@dougt.org>
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsDOMOrientationEvent.h"
#include "nsContentUtils.h"

NS_IMPL_ADDREF_INHERITED(nsDOMOrientationEvent, nsDOMEvent)
NS_IMPL_RELEASE_INHERITED(nsDOMOrientationEvent, nsDOMEvent)

DOMCI_DATA(OrientationEvent, nsDOMOrientationEvent)

NS_INTERFACE_MAP_BEGIN(nsDOMOrientationEvent)
  NS_INTERFACE_MAP_ENTRY(nsIDOMOrientationEvent)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(OrientationEvent)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEvent)

NS_IMETHODIMP nsDOMOrientationEvent::InitOrientationEvent(const nsAString & eventTypeArg,
                                                          PRBool canBubbleArg,
                                                          PRBool cancelableArg,
                                                          double x,
                                                          double y,
                                                          double z)
{
  nsresult rv = nsDOMEvent::InitEvent(eventTypeArg, canBubbleArg, cancelableArg);
  NS_ENSURE_SUCCESS(rv, rv);

  mX = x;
  mY = y;
  mZ = z;

  return NS_OK;
}


NS_IMETHODIMP nsDOMOrientationEvent::GetX(double *aX)
{
  NS_ENSURE_ARG_POINTER(aX);

  *aX = mX;
  return NS_OK;
}

NS_IMETHODIMP nsDOMOrientationEvent::GetY(double *aY)
{
  NS_ENSURE_ARG_POINTER(aY);

  *aY = mY;
  return NS_OK;
}

NS_IMETHODIMP nsDOMOrientationEvent::GetZ(double *aZ)
{
  NS_ENSURE_ARG_POINTER(aZ);

  *aZ = mZ;
  return NS_OK;
}


nsresult NS_NewDOMOrientationEvent(nsIDOMEvent** aInstancePtrResult,
                                   nsPresContext* aPresContext,
                                   nsEvent *aEvent) 
{
  NS_ENSURE_ARG_POINTER(aInstancePtrResult);

  nsDOMOrientationEvent* it = new nsDOMOrientationEvent(aPresContext, aEvent);
  if (nsnull == it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return CallQueryInterface(it, aInstancePtrResult);
}
