/* -*- Mode: c++; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Android code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ted Mielczarek <ted.mielczarek@gmail.com>
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

#ifndef NSWINDOW_H_
#define NSWINDOW_H_

#include "nsBaseWidget.h"
#include "gfxPoint.h"

#include "nsTArray.h"

@class UIWindow;
@class UIView;
@class ChildView;
class gfxASurface;

class nsWindow :
    public nsBaseWidget
{
    typedef nsBaseWidget Inherited;

public:
    nsWindow();
    virtual ~nsWindow();

    NS_DECL_ISUPPORTS_INHERITED

    void Redraw();

    void InitEvent(nsGUIEvent& event, nsIntPoint* aPoint = 0);

    //
    // nsIWidget
    //

    NS_IMETHOD Create(nsIWidget *aParent,
                      nsNativeWidget aNativeParent,
                      const nsIntRect &aRect,
                      EVENT_CALLBACK aHandleEventFunction,
                      nsIDeviceContext *aContext,
                      nsIAppShell *aAppShell,
                      nsIToolkit *aToolkit,
                      nsWidgetInitData *aInitData);
    NS_IMETHOD Destroy(void);
    NS_IMETHOD ConfigureChildren(const nsTArray<nsIWidget::Configuration>&);
    NS_IMETHOD SetParent(nsIWidget* aNewParent);
    virtual nsIWidget *GetParent(void);
    NS_IMETHOD Show(PRBool aState);
    NS_IMETHOD SetModal(PRBool aModal);
    NS_IMETHOD IsVisible(PRBool & aState);
    NS_IMETHOD ConstrainPosition(PRBool aAllowSlop,
                                 PRInt32 *aX,
                                 PRInt32 *aY);
    NS_IMETHOD Move(PRInt32 aX,
                    PRInt32 aY);
    NS_IMETHOD Resize(PRInt32 aWidth,
                      PRInt32 aHeight,
                      PRBool  aRepaint);
    NS_IMETHOD Resize(PRInt32 aX,
                      PRInt32 aY,
                      PRInt32 aWidth,
                      PRInt32 aHeight,
                      PRBool aRepaint);
    NS_IMETHOD SetZIndex(PRInt32 aZIndex);
    NS_IMETHOD PlaceBehind(nsTopLevelWidgetZPlacement aPlacement,
                           nsIWidget *aWidget,
                           PRBool aActivate);
    NS_IMETHOD SetSizeMode(PRInt32 aMode);
    NS_IMETHOD Enable(PRBool aState);
    NS_IMETHOD IsEnabled(PRBool *aState);
    NS_IMETHOD Invalidate(const nsIntRect &aRect,
                          PRBool aIsSynchronous);
    NS_IMETHOD Update();
    void Scroll(const nsIntPoint&,
                const nsTArray<nsIntRect>&,
                const nsTArray<nsIWidget::Configuration>&);
    NS_IMETHOD SetFocus(PRBool aRaise = PR_FALSE);
    NS_IMETHOD GetScreenBounds(nsIntRect &aRect);
    virtual nsIntPoint WidgetToScreenOffset();
    static  PRBool          ConvertStatus(nsEventStatus aStatus)
    { return aStatus == nsEventStatus_eConsumeNoDefault; }

    NS_IMETHOD DispatchEvent(nsGUIEvent *aEvent, nsEventStatus &aStatus);
    PRBool DispatchWindowEvent(nsGUIEvent &event);

    NS_IMETHOD SetForegroundColor(const nscolor &aColor) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD SetBackgroundColor(const nscolor &aColor);
    NS_IMETHOD SetCursor(nsCursor aCursor) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD SetCursor(imgIContainer* aCursor,
                         PRUint32 aHotspotX,
                         PRUint32 aHotspotY) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD SetHasTransparentBackground(PRBool aTransparent) { return NS_OK; }
    NS_IMETHOD GetHasTransparentBackground(PRBool& aTransparent) { aTransparent = PR_FALSE; return NS_OK; }
    NS_IMETHOD HideWindowChrome(PRBool aShouldHide) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD MakeFullScreen(PRBool aFullScreen) { return NS_ERROR_NOT_IMPLEMENTED; }
    virtual void* GetNativeData(PRUint32 aDataType);
    NS_IMETHOD SetTitle(const nsAString& aTitle) { return NS_OK; }
    NS_IMETHOD SetIcon(const nsAString& aIconSpec) { return NS_OK; }
    NS_IMETHOD EnableDragDrop(PRBool aEnable) { return NS_OK; }
    NS_IMETHOD CaptureMouse(PRBool aCapture) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD CaptureRollupEvents(nsIRollupListener *aListener,
                                   nsIMenuRollup *aMenuRollup,
                                   PRBool aDoCapture,
                                   PRBool aConsumeRollupEvent) { return NS_ERROR_NOT_IMPLEMENTED; }

    NS_IMETHOD GetAttention(PRInt32 aCycleCount) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD BeginResizeDrag(nsGUIEvent* aEvent, PRInt32 aHorizontal, PRInt32 aVertical) { return NS_ERROR_NOT_IMPLEMENTED; }

    NS_IMETHOD SetIMEEnabled(PRUint32 aState) {
        printf("SetIMEEnabled[%p](%d)\n", this, aState);
        return NS_ERROR_NOT_IMPLEMENTED;
    };
    NS_IMETHOD GetIMEEnabled(PRUint32* aState) {
        printf("GetIMEEnabled[%p]\n", this);
        return NS_ERROR_NOT_IMPLEMENTED;
    };

    gfxASurface* GetThebesSurface();

protected:
    void BringToFront();
    nsWindow *FindTopLevel();
    PRBool DrawTo(gfxASurface *targetSurface);
    PRBool IsTopLevel();
    nsresult GetCurrentOffset(PRUint32 &aOffset, PRUint32 &aLength);
    nsresult DeleteRange(int aOffset, int aLen);
    PRBool ReportDestroyEvent();
    PRBool ReportMoveEvent();
    PRBool ReportSizeEvent();

    void TearDownView();

    ChildView*   mNativeView;
    PRPackedBool mVisible;
    nsTArray<nsWindow*> mChildren;
    nsWindow* mParent;
    nsRefPtr<gfxASurface> mTempThebesSurface;

    void OnSizeChanged(const gfxIntSize& aSize);

    static void DumpWindows();
    static void DumpWindows(const nsTArray<nsWindow*>& wins, int indent = 0);
    static void LogWindow(nsWindow *win, int index, int indent);
};

#endif /* NSWINDOW_H_ */
