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
 *   The Mozilla Foundation.
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

#include "mozilla/ipc/DocumentRendererParent.h"
#include "gfxImageSurface.h"
#include "gfxPattern.h"

using namespace mozilla::ipc;

DocumentRendererParent::DocumentRendererParent()
{}

DocumentRendererParent::~DocumentRendererParent()
{}

void DocumentRendererParent::SetCanvasContext(nsICanvasRenderingContextInternal* aCanvas,
                                              gfxContext* ctx)
{
    mCanvas = aCanvas;
    mCanvasContext = ctx;
}

void DocumentRendererParent::DrawToCanvas(PRUint32 aWidth, PRUint32 aHeight,
                                          const nsCString& aData)
{
    if (!mCanvas || !mCanvasContext)
        return;

    nsRefPtr<gfxImageSurface> surf = new gfxImageSurface(reinterpret_cast<PRUint8*>(const_cast<char*>(aData.Data())),
                                                         gfxIntSize(aWidth, aHeight),
                                                         aWidth * 4,
                                                         gfxASurface::ImageFormatARGB32);
    nsRefPtr<gfxPattern> pat = new gfxPattern(surf);

    mCanvasContext->NewPath();
    mCanvasContext->PixelSnappedRectangleAndSetPattern(gfxRect(0, 0, aWidth, aHeight), pat);
    mCanvasContext->Fill();

    // get rid of the pattern surface ref, because aData is very likely to go away shortly
    mCanvasContext->SetColor(gfxRGBA(1,1,1,1));

    gfxRect damageRect = mCanvasContext->UserToDevice(gfxRect(0, 0, aWidth, aHeight));
    mCanvas->Redraw(damageRect);
}

bool
DocumentRendererParent::Recv__delete__(const PRUint32& w, const PRUint32& h,
                                       const nsCString& data)
{
    DrawToCanvas(w, h, data);
    return true;
}
