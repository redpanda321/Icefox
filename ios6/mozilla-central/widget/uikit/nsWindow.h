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
                      nsDeviceContext *aContext,
                      nsWidgetInitData *aInitData);
    NS_IMETHOD Destroy(void);
    NS_IMETHOD ConfigureChildren(const nsTArray<nsIWidget::Configuration>&);
    NS_IMETHOD SetParent(nsIWidget* aNewParent);
    virtual nsIWidget *GetParent(void);
    NS_IMETHOD Show(bool aState);
    NS_IMETHOD SetModal(bool aModal);
    virtual bool IsVisible() const;
    NS_IMETHOD ConstrainPosition(bool aAllowSlop, int32_t *aX, int32_t *aY);
    NS_IMETHOD Move(double aX, double aY);
    NS_IMETHOD Resize(double aWidth, double aHeight, bool  aRepaint);
    NS_IMETHOD Resize(double aX, double aY,
                      double aWidth, double aHeight, bool aRepaint);
    NS_IMETHOD SetZIndex(int32_t aZIndex);
    NS_IMETHOD PlaceBehind(nsTopLevelWidgetZPlacement aPlacement,
                           nsIWidget *aWidget,
                           bool aActivate);
    NS_IMETHOD SetSizeMode(int32_t aMode);
    NS_IMETHOD Enable(bool aState);
    virtual bool IsEnabled() const;
    NS_IMETHOD Invalidate(const nsIntRect &aRect);
    NS_IMETHOD Update();
    void Scroll(const nsIntPoint&,
                const nsTArray<nsIntRect>&,
                const nsTArray<nsIWidget::Configuration>&);
    NS_IMETHOD SetFocus(bool aRaise = false);
    NS_IMETHOD GetScreenBounds(nsIntRect &aRect);
    virtual nsIntPoint WidgetToScreenOffset();
    static  bool          ConvertStatus(nsEventStatus aStatus)
    { return aStatus == nsEventStatus_eConsumeNoDefault; }

    NS_IMETHOD DispatchEvent(nsGUIEvent *aEvent, nsEventStatus &aStatus);
    bool DispatchWindowEvent(nsGUIEvent &event);

    NS_IMETHOD SetForegroundColor(const nscolor &aColor) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD SetBackgroundColor(const nscolor &aColor);
    NS_IMETHOD SetCursor(nsCursor aCursor) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD SetCursor(imgIContainer* aCursor,
                         uint32_t aHotspotX,
                         uint32_t aHotspotY) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD SetHasTransparentBackground(bool aTransparent) { return NS_OK; }
    NS_IMETHOD GetHasTransparentBackground(bool& aTransparent) { aTransparent = false; return NS_OK; }
    NS_IMETHOD HideWindowChrome(bool aShouldHide) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD MakeFullScreen(bool aFullScreen) { return NS_ERROR_NOT_IMPLEMENTED; }
    virtual void* GetNativeData(uint32_t aDataType);
    NS_IMETHOD SetTitle(const nsAString& aTitle) { return NS_OK; }
    NS_IMETHOD SetIcon(const nsAString& aIconSpec) { return NS_OK; }
    NS_IMETHOD EnableDragDrop(bool aEnable) { return NS_OK; }
    NS_IMETHOD CaptureMouse(bool aCapture) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD CaptureRollupEvents(nsIRollupListener *aListener, bool aDoCapture) { return NS_ERROR_NOT_IMPLEMENTED; }

    NS_IMETHOD GetAttention(int32_t aCycleCount) { return NS_ERROR_NOT_IMPLEMENTED; }
    NS_IMETHOD BeginResizeDrag(nsGUIEvent* aEvent, int32_t aHorizontal, int32_t aVertical) { return NS_ERROR_NOT_IMPLEMENTED; }

    NS_IMETHOD_(void) SetInputContext(const InputContext& aContext,
                                      const InputContextAction& aAction)
    {
      mInputContext = aContext;
    }
    NS_IMETHOD_(InputContext) GetInputContext()
    {
      return mInputContext;
    }

    gfxASurface* GetThebesSurface();

    NS_IMETHOD         ReparentNativeWidget(nsIWidget* aNewParent);

protected:
    void BringToFront();
    nsWindow *FindTopLevel();
    bool DrawTo(gfxASurface *targetSurface);
    bool IsTopLevel();
    nsresult GetCurrentOffset(uint32_t &aOffset, uint32_t &aLength);
    nsresult DeleteRange(int aOffset, int aLen);
    void ReportMoveEvent();
    void ReportSizeEvent();

    void TearDownView();

    ChildView*   mNativeView;
    PRPackedBool mVisible;
    nsTArray<nsWindow*> mChildren;
    nsWindow* mParent;
    nsRefPtr<gfxASurface> mTempThebesSurface;

    InputContext mInputContext;

    void OnSizeChanged(const gfxIntSize& aSize);

    static void DumpWindows();
    static void DumpWindows(const nsTArray<nsWindow*>& wins, int indent = 0);
    static void LogWindow(nsWindow *win, int index, int indent);
};

#endif /* NSWINDOW_H_ */
