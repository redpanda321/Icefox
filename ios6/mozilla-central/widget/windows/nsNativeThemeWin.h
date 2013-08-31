/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsITheme.h"
#include "nsCOMPtr.h"
#include "nsIAtom.h"
#include "nsNativeTheme.h"
#include <windows.h>

struct nsIntRect;
struct nsIntSize;

class nsNativeThemeWin : private nsNativeTheme,
                         public nsITheme {
public:
  NS_DECL_ISUPPORTS_INHERITED

  // The nsITheme interface.
  NS_IMETHOD DrawWidgetBackground(nsRenderingContext* aContext,
                                  nsIFrame* aFrame,
                                  uint8_t aWidgetType,
                                  const nsRect& aRect,
                                  const nsRect& aDirtyRect);

  NS_IMETHOD GetWidgetBorder(nsDeviceContext* aContext, 
                             nsIFrame* aFrame,
                             uint8_t aWidgetType,
                             nsIntMargin* aResult);

  virtual bool GetWidgetPadding(nsDeviceContext* aContext,
                                  nsIFrame* aFrame,
                                  uint8_t aWidgetType,
                                  nsIntMargin* aResult);

  virtual bool GetWidgetOverflow(nsDeviceContext* aContext,
                                   nsIFrame* aFrame,
                                   uint8_t aWidgetType,
                                   nsRect* aOverflowRect);

  NS_IMETHOD GetMinimumWidgetSize(nsRenderingContext* aContext, nsIFrame* aFrame,
                                  uint8_t aWidgetType,
                                  nsIntSize* aResult,
                                  bool* aIsOverridable);

  virtual Transparency GetWidgetTransparency(nsIFrame* aFrame, uint8_t aWidgetType);

  NS_IMETHOD WidgetStateChanged(nsIFrame* aFrame, uint8_t aWidgetType, 
                                nsIAtom* aAttribute, bool* aShouldRepaint);

  NS_IMETHOD ThemeChanged();

  bool ThemeSupportsWidget(nsPresContext* aPresContext, 
                             nsIFrame* aFrame,
                             uint8_t aWidgetType);

  bool WidgetIsContainer(uint8_t aWidgetType);

  bool ThemeDrawsFocusForWidget(nsPresContext* aPresContext, nsIFrame* aFrame, uint8_t aWidgetType);

  bool ThemeNeedsComboboxDropmarker();

  nsNativeThemeWin();
  virtual ~nsNativeThemeWin();

protected:
  HANDLE GetTheme(uint8_t aWidgetType);
  nsresult GetThemePartAndState(nsIFrame* aFrame, uint8_t aWidgetType,
                                int32_t& aPart, int32_t& aState);
  nsresult ClassicGetThemePartAndState(nsIFrame* aFrame, uint8_t aWidgetType,
                                   int32_t& aPart, int32_t& aState, bool& aFocused);
  nsresult ClassicDrawWidgetBackground(nsRenderingContext* aContext,
                                  nsIFrame* aFrame,
                                  uint8_t aWidgetType,
                                  const nsRect& aRect,
                                  const nsRect& aClipRect);
  nsresult ClassicGetWidgetBorder(nsDeviceContext* aContext, 
                             nsIFrame* aFrame,
                             uint8_t aWidgetType,
                             nsIntMargin* aResult);

  bool ClassicGetWidgetPadding(nsDeviceContext* aContext,
                            nsIFrame* aFrame,
                            uint8_t aWidgetType,
                            nsIntMargin* aResult);

  nsresult ClassicGetMinimumWidgetSize(nsRenderingContext* aContext, nsIFrame* aFrame,
                                  uint8_t aWidgetType,
                                  nsIntSize* aResult,
                                  bool* aIsOverridable);

  bool ClassicThemeSupportsWidget(nsPresContext* aPresContext, 
                             nsIFrame* aFrame,
                             uint8_t aWidgetType);

  void DrawCheckedRect(HDC hdc, const RECT& rc, int32_t fore, int32_t back,
                       HBRUSH defaultBack);

  uint32_t GetWidgetNativeDrawingFlags(uint8_t aWidgetType);

  int32_t StandardGetState(nsIFrame* aFrame, uint8_t aWidgetType, bool wantFocused);

  bool IsMenuActive(nsIFrame* aFrame, uint8_t aWidgetType);
};
