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
 * The Original Code is Fennec Electrolysis.
 *
 * The Initial Developer of the Original Code is
 *   Nokia.
 * Portions created by the Initial Developer are Copyright (C) 2010
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

#ifndef mozilla_dom_DocumentRendererNativeIDChild
#define mozilla_dom_DocumentRendererNativeIDChild

#include "mozilla/ipc/PDocumentRendererNativeIDChild.h"

class nsIDOMWindow;
struct gfxMatrix;

namespace mozilla {
namespace ipc {

class DocumentRendererNativeIDChild : public PDocumentRendererNativeIDChild
{
public:
    DocumentRendererNativeIDChild();
    virtual ~DocumentRendererNativeIDChild();

    bool RenderDocument(nsIDOMWindow* window, const PRInt32& x,
                        const PRInt32& y, const PRInt32& w,
                        const PRInt32& h, const nsString& aBGColor,
                        const PRUint32& flags, const PRBool& flush,
                        const gfxMatrix& aMatrix,
                        const PRInt32& nativeID);

private:

    DISALLOW_EVIL_CONSTRUCTORS(DocumentRendererNativeIDChild);
};

}
}

#endif
