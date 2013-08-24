/* -*- Mode: C++; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _NS_DEVICECONTEXT_H_
#define _NS_DEVICECONTEXT_H_

#include "nsCOMPtr.h"
#include "nsIDeviceContextSpec.h"
#include "nsIScreenManager.h"
#include "nsIWidget.h"
#include "nsCoord.h"
#include "gfxContext.h"

class nsIAtom;
class nsFontCache;
class gfxUserFontSet;

class nsDeviceContext
{
public:
    nsDeviceContext();
    ~nsDeviceContext();

    NS_INLINE_DECL_REFCOUNTING(nsDeviceContext)

    /**
     * Initialize the device context from a widget
     * @param aWidget a widget to initialize the device context from
     * @return error status
     */
    nsresult Init(nsIWidget *aWidget);

    /**
     * Initialize the device context from a device context spec
     * @param aDevSpec the specification of the printing device
     * @return error status
     */
    nsresult InitForPrinting(nsIDeviceContextSpec *aDevSpec);

    /**
     * Create a rendering context and initialize it.  Only call this
     * method on device contexts that were initialized for printing.
     * @param aContext out parameter for new rendering context
     * @return error status
     */
    nsresult CreateRenderingContext(nsRenderingContext *&aContext);

    /**
     * Gets the number of app units in one CSS pixel; this number is global,
     * not unique to each device context.
     */
    static PRInt32 AppUnitsPerCSSPixel() { return 60; }

    /**
     * Gets the number of app units in one device pixel; this number
     * is usually a factor of AppUnitsPerCSSPixel(), although that is
     * not guaranteed.
     */
    PRUint32 AppUnitsPerDevPixel() const { return mAppUnitsPerDevPixel; }

    /**
     * Convert device pixels which is used for gfx/thebes to nearest
     * (rounded) app units
     */
    nscoord GfxUnitsToAppUnits(gfxFloat aGfxUnits) const
    { return nscoord(NS_round(aGfxUnits * AppUnitsPerDevPixel())); }

    /**
     * Convert app units to device pixels which is used for gfx/thebes.
     */
    gfxFloat AppUnitsToGfxUnits(nscoord aAppUnits) const
    { return gfxFloat(aAppUnits) / AppUnitsPerDevPixel(); }

    /**
     * Gets the number of app units in one physical inch; this is the
     * device's DPI times AppUnitsPerDevPixel().
     */
    PRInt32 AppUnitsPerPhysicalInch() const
    { return mAppUnitsPerPhysicalInch; }

    /**
     * Gets the number of app units in one CSS inch; this is
     * 96 times AppUnitsPerCSSPixel.
     */
    static PRInt32 AppUnitsPerCSSInch() { return 96 * AppUnitsPerCSSPixel(); }

    /**
     * Get the unscaled ratio of app units to dev pixels; useful if something
     * needs to be converted from to unscaled pixels
     */
    PRInt32 UnscaledAppUnitsPerDevPixel() const
    { return mAppUnitsPerDevNotScaledPixel; }

    /**
     * Get the nsFontMetrics that describe the properties of
     * an nsFont.
     * @param aFont font description to obtain metrics for
     * @param aLanguage the language of the document
     * @param aMetrics out parameter for font metrics
     * @param aUserFontSet user font set
     * @return error status
     */
    nsresult GetMetricsFor(const nsFont& aFont, nsIAtom* aLanguage,
                           gfxUserFontSet* aUserFontSet,
                           nsFontMetrics*& aMetrics);

    /**
     * Notification when a font metrics instance created for this device is
     * about to be deleted
     */
    nsresult FontMetricsDeleted(const nsFontMetrics* aFontMetrics);

    /**
     * Attempt to free up resources by flushing out any fonts no longer
     * referenced by anything other than the font cache itself.
     * @return error status
     */
    nsresult FlushFontCache();

    /**
     * Return the bit depth of the device.
     */
    nsresult GetDepth(PRUint32& aDepth);

    /**
     * Get the size of the displayable area of the output device
     * in app units.
     * @param aWidth out parameter for width
     * @param aHeight out parameter for height
     * @return error status
     */
    nsresult GetDeviceSurfaceDimensions(nscoord& aWidth, nscoord& aHeight);

    /**
     * Get the size of the content area of the output device in app
     * units.  This corresponds on a screen device, for instance, to
     * the entire screen.
     * @param aRect out parameter for full rect. Position (x,y) will
     *              be (0,0) or relative to the primary monitor if
     *              this is not the primary.
     * @return error status
     */
    nsresult GetRect(nsRect& aRect);

    /**
     * Get the size of the content area of the output device in app
     * units.  This corresponds on a screen device, for instance, to
     * the area reported by GetDeviceSurfaceDimensions, minus the
     * taskbar (Windows) or menubar (Macintosh).
     * @param aRect out parameter for client rect. Position (x,y) will
     *              be (0,0) adjusted for any upper/left non-client
     *              space if present or relative to the primary
     *              monitor if this is not the primary.
     * @return error status
     */
    nsresult GetClientRect(nsRect& aRect);

    /**
     * Inform the output device that output of a document is beginning
     * Used for print related device contexts. Must be matched 1:1 with
     * EndDocument() or AbortDocument().
     *
     * @param aTitle - title of Document
     * @param aPrintToFileName - name of file to print to, if NULL
     * then don't print to file
     * @param aStartPage - starting page number (must be greater than zero)
     * @param aEndPage - ending page number (must be less than or
     * equal to number of pages)
     *
     * @return error status
     */
    nsresult BeginDocument(PRUnichar  *aTitle,
                           PRUnichar  *aPrintToFileName,
                           PRInt32     aStartPage,
                           PRInt32     aEndPage);

    /**
     * Inform the output device that output of a document is ending.
     * Used for print related device contexts. Must be matched 1:1 with
     * BeginDocument()
     * @return error status
     */
    nsresult EndDocument();

    /**
     * Inform the output device that output of a document is being aborted.
     * Must be matched 1:1 with BeginDocument()
     * @return error status
     */
    nsresult AbortDocument();

    /**
     * Inform the output device that output of a page is beginning
     * Used for print related device contexts. Must be matched 1:1 with
     * EndPage() and within a BeginDocument()/EndDocument() pair.
     * @return error status
     */
    nsresult BeginPage();

    /**
     * Inform the output device that output of a page is ending
     * Used for print related device contexts. Must be matched 1:1 with
     * BeginPage() and within a BeginDocument()/EndDocument() pair.
     * @return error status
     */
    nsresult EndPage();

    /**
     * Check to see if the DPI has changed
     * @return whether there was actually a change in the DPI (whether
     *         AppUnitsPerDevPixel() or AppUnitsPerPhysicalInch()
     *         changed)
     */
    bool CheckDPIChange();

    /**
     * Set the pixel scaling factor: all lengths are multiplied by this factor
     * when we convert them to device pixels. Returns whether the ratio of
     * app units to dev pixels changed because of the scale factor.
     */
    bool SetPixelScale(float aScale);

    /**
     * True if this device context was created for printing.
     */
    bool IsPrinterSurface();

protected:
    void SetDPI();
    void ComputeClientRectUsingScreen(nsRect *outRect);
    void ComputeFullAreaUsingScreen(nsRect *outRect);
    void FindScreen(nsIScreen **outScreen);
    void CalcPrintingSize();
    void UpdateScaledAppUnits();

    nscoord  mWidth;
    nscoord  mHeight;
    PRUint32 mDepth;
    PRUint32  mAppUnitsPerDevPixel;
    PRInt32  mAppUnitsPerDevNotScaledPixel;
    PRInt32  mAppUnitsPerPhysicalInch;
    float    mPixelScale;
    float    mPrintingScale;

    nsFontCache*                   mFontCache;
    nsCOMPtr<nsIWidget>            mWidget;
    nsCOMPtr<nsIScreenManager>     mScreenManager;
    nsCOMPtr<nsIDeviceContextSpec> mDeviceContextSpec;
    nsRefPtr<gfxASurface>          mPrintingSurface;
};

#endif /* _NS_DEVICECONTEXT_H_ */
