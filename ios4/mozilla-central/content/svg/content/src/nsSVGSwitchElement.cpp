/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is the Mozilla SVG project.
 *
 * The Initial Developer of the Original Code is IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsSVGFeatures.h"
#include "nsSVGSwitchElement.h"
#include "nsIFrame.h"
#include "nsISVGChildFrame.h"
#include "nsSVGUtils.h"

////////////////////////////////////////////////////////////////////////
// implementation


NS_IMPL_NS_NEW_SVG_ELEMENT(Switch)


//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_CYCLE_COLLECTION_CLASS(nsSVGSwitchElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsSVGSwitchElement,
                                                  nsSVGSwitchElementBase)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mActiveChild)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsSVGSwitchElement,
                                                nsSVGSwitchElementBase)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mActiveChild)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ADDREF_INHERITED(nsSVGSwitchElement,nsSVGSwitchElementBase)
NS_IMPL_RELEASE_INHERITED(nsSVGSwitchElement,nsSVGSwitchElementBase)

DOMCI_NODE_DATA(SVGSwitchElement, nsSVGSwitchElement)

NS_INTERFACE_TABLE_HEAD_CYCLE_COLLECTION_INHERITED(nsSVGSwitchElement)
  NS_NODE_INTERFACE_TABLE4(nsSVGSwitchElement, nsIDOMNode, nsIDOMElement,
                           nsIDOMSVGElement, nsIDOMSVGSwitchElement)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGSwitchElement)
NS_INTERFACE_MAP_END_INHERITING(nsSVGSwitchElementBase)

//----------------------------------------------------------------------
// Implementation

nsSVGSwitchElement::nsSVGSwitchElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : nsSVGSwitchElementBase(aNodeInfo)
{
}

void
nsSVGSwitchElement::MaybeInvalidate()
{
  // We don't reuse UpdateActiveChild() and check if mActiveChild has changed
  // to determine if we should call nsSVGUtils::UpdateGraphic. If we did that,
  // nsSVGUtils::UpdateGraphic would not invalidate the old mActiveChild area!

  if (FindActiveChild() == mActiveChild) {
    return;
  }

  nsIFrame *frame = GetPrimaryFrame();
  if (frame) {
    nsISVGChildFrame* svgFrame = do_QueryFrame(frame);
    if (svgFrame) {
      nsSVGUtils::UpdateGraphic(svgFrame);
    }
  }
}

//----------------------------------------------------------------------
// nsIDOMNode methods


NS_IMPL_ELEMENT_CLONE_WITH_INIT(nsSVGSwitchElement)

//----------------------------------------------------------------------
// nsINode methods

nsresult
nsSVGSwitchElement::InsertChildAt(nsIContent* aKid,
                                  PRUint32 aIndex,
                                  PRBool aNotify)
{
  nsresult rv = nsSVGSwitchElementBase::InsertChildAt(aKid, aIndex, aNotify);
  if (NS_SUCCEEDED(rv)) {
    MaybeInvalidate();
  }
  return rv;
}

nsresult
nsSVGSwitchElement::RemoveChildAt(PRUint32 aIndex, PRBool aNotify, PRBool aMutationEvent)
{
  NS_ASSERTION(aMutationEvent, "Someone tried to inhibit mutations on switch child removal.");
  nsresult rv = nsSVGSwitchElementBase::RemoveChildAt(aIndex, aNotify, aMutationEvent);
  if (NS_SUCCEEDED(rv)) {
    MaybeInvalidate();
  }
  return rv;
}
 
//----------------------------------------------------------------------
// nsIContent methods

NS_IMETHODIMP_(PRBool)
nsSVGSwitchElement::IsAttributeMapped(const nsIAtom* name) const
{
  static const MappedAttributeEntry* const map[] = {
    sFEFloodMap,
    sFiltersMap,
    sFontSpecificationMap,
    sGradientStopMap,
    sLightingEffectsMap,
    sMarkersMap,
    sTextContentElementsMap,
    sViewportsMap
  };

  return FindAttributeDependence(name, map, NS_ARRAY_LENGTH(map)) ||
    nsSVGSwitchElementBase::IsAttributeMapped(name);
}

//----------------------------------------------------------------------
// Implementation Helpers:

nsIContent *
nsSVGSwitchElement::FindActiveChild() const
{
  PRBool allowReorder = AttrValueIs(kNameSpaceID_None,
                                    nsGkAtoms::allowReorder,
                                    nsGkAtoms::yes, eCaseMatters);

  const nsAdoptingString& acceptLangs =
    nsContentUtils::GetLocalizedStringPref("intl.accept_languages");

  PRUint32 count = GetChildCount();

  if (allowReorder && !acceptLangs.IsEmpty()) {
    PRInt32 bestLanguagePreferenceRank = -1;
    nsIContent *bestChild = nsnull;
    for (PRUint32 i = 0; i < count; i++) {
      nsIContent *child = GetChildAt(i);
      if (nsSVGFeatures::PassesConditionalProcessingTests(
            child, nsSVGFeatures::kIgnoreSystemLanguage)) {
        nsAutoString value;
        if (child->GetAttr(kNameSpaceID_None, nsGkAtoms::systemLanguage,
                           value)) {
          PRInt32 languagePreferenceRank =
            nsSVGFeatures::GetBestLanguagePreferenceRank(value, acceptLangs);
          switch (languagePreferenceRank) {
          case 0:
            // best possible match
            return child;
          case -1:
            // not found
            break;
          default:
            if (bestLanguagePreferenceRank == -1 ||
                languagePreferenceRank < bestLanguagePreferenceRank) {
              bestLanguagePreferenceRank = languagePreferenceRank;
              bestChild = child;
            }
            break;
          }
        } else if (!bestChild) {
          bestChild = child;
        }
      }
    }
    return bestChild;
  }

  for (PRUint32 i = 0; i < count; i++) {
    nsIContent *child = GetChildAt(i);
    if (nsSVGFeatures::PassesConditionalProcessingTests(child, &acceptLangs)) {
      return child;
    }
  }
  return nsnull;
}
