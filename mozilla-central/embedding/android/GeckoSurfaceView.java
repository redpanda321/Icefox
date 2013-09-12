/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.locks.*;
import java.util.concurrent.atomic.*;
import java.util.zip.*;
import java.nio.*;

import android.os.*;
import android.app.*;
import android.text.*;
import android.text.method.*;
import android.view.*;
import android.view.inputmethod.*;
import android.content.*;
import android.graphics.*;
import android.widget.*;
import android.hardware.*;
import android.location.*;
import android.graphics.drawable.*;
import android.content.res.*;

import android.util.*;

/*
 * GeckoSurfaceView implements a GL surface view,
 * similar to GLSurfaceView.  However, since we
 * already have a thread for Gecko, we don't really want
 * a separate renderer thread that GLSurfaceView provides.
 */
class GeckoSurfaceView
    extends SurfaceView
    implements SurfaceHolder.Callback, SensorEventListener, LocationListener
{
    private static final String LOG_FILE_NAME = "GeckoSurfaceView";

    public GeckoSurfaceView(Context context) {
        super(context);

        getHolder().addCallback(this);
        inputConnection = new GeckoInputConnection(this);
        setFocusable(true);
        setFocusableInTouchMode(true);
        
        DisplayMetrics metrics = new DisplayMetrics();
        GeckoApp.mAppContext.getWindowManager().
            getDefaultDisplay().getMetrics(metrics);
        mWidth = metrics.widthPixels;
        mHeight = metrics.heightPixels;
        mBufferWidth = 0;
        mBufferHeight = 0;

        mSurfaceLock = new ReentrantLock();

        mEditableFactory = Editable.Factory.getInstance();
        initEditable("");
        mIMEState = IME_STATE_DISABLED;
        mIMETypeHint = "";
        mIMEModeHint = "";
        mIMEActionHint = "";
    }

    protected void finalize() throws Throwable {
        super.finalize();
    }

    void drawSplashScreen() {
        this.drawSplashScreen(getHolder(), mWidth, mHeight);
    }

    void drawSplashScreen(SurfaceHolder holder, int width, int height) {
        // No splash screen for Honeycomb or greater
        if (Build.VERSION.SDK_INT >= 11) {
            Log.i(LOG_FILE_NAME, "skipping splash screen");
            return;
        }

        Canvas c = holder.lockCanvas();
        if (c == null) {
            Log.i(LOG_FILE_NAME, "canvas is null");
            return;
        }

        Resources res = getResources();

        File watchDir = new File(GeckoApp.sGREDir, "components");
        if (watchDir.exists() == false) {
            // Just show the simple splash screen for "new profile" startup
            c.drawColor(res.getColor(R.color.splash_background));
            Drawable drawable = res.getDrawable(R.drawable.splash);
            int w = drawable.getIntrinsicWidth();
            int h = drawable.getIntrinsicHeight();
            int x = (width - w) / 2;
            int y = (height - h) / 2 - 16;
            drawable.setBounds(x, y, x + w, y + h);
            drawable.draw(c);

            Paint p = new Paint();
            p.setTextAlign(Paint.Align.CENTER);
            p.setTextSize(32f);
            p.setAntiAlias(true);
            p.setColor(res.getColor(R.color.splash_msgfont));
            c.drawText(res.getString(R.string.splash_firstrun), width / 2, y + h + 16, p);
        } else {
            // Show the static UI for normal startup
            DisplayMetrics metrics = new DisplayMetrics();
            GeckoApp.mAppContext.getWindowManager().getDefaultDisplay().getMetrics(metrics);
    
            // Default to DENSITY_HIGH sizes
            int toolbarHeight = 80;
            int faviconOffset = 25;
            float urlHeight = 24f;
            int urlOffsetX = 80;
            int urlOffsetY = 48;
            if (metrics.densityDpi == DisplayMetrics.DENSITY_MEDIUM) {
                toolbarHeight = 53;
                faviconOffset = 10;
                urlHeight = 16f;
                urlOffsetX = 53;
                urlOffsetY = 32;
            }
    
            c.drawColor(res.getColor(R.color.splash_content));
            Drawable toolbar = res.getDrawable(Build.VERSION.SDK_INT > 8 ?
                                               R.drawable.splash_v9 :
                                               R.drawable.splash_v8);
            toolbar.setBounds(0, 0, width, toolbarHeight);
            toolbar.draw(c);
    
            // XUL/CSS always uses 32px width and height for favicon
            Drawable favicon = res.getDrawable(R.drawable.favicon32);
            favicon.setBounds(faviconOffset, faviconOffset, 32 + faviconOffset, 32 + faviconOffset);
            favicon.draw(c);
    
            if (GeckoSurfaceView.mSplashURL != "") {
                TextPaint p = new TextPaint();
                p.setTextAlign(Paint.Align.LEFT);
                p.setTextSize(urlHeight);
                p.setAntiAlias(true);
                p.setColor(res.getColor(R.color.splash_urlfont));
                String url = TextUtils.ellipsize(GeckoSurfaceView.mSplashURL, p, width - urlOffsetX * 2, TextUtils.TruncateAt.END).toString();
                c.drawText(url, urlOffsetX, urlOffsetY, p);
            }
        }
        holder.unlockCanvasAndPost(c);
    }

    /*
     * Called on main thread
     */

    public void draw(SurfaceHolder holder, ByteBuffer buffer) {
        if (buffer == null || buffer.capacity() != (mWidth * mHeight * 2))
            return;

        synchronized (mSoftwareBuffer) {
            if (buffer != mSoftwareBuffer || mSoftwareBufferCopy == null)
                return;

            Canvas c = holder.lockCanvas();
            if (c == null)
                return;
            mSoftwareBufferCopy.copyPixelsFromBuffer(buffer);
            c.drawBitmap(mSoftwareBufferCopy, 0, 0, null);
            holder.unlockCanvasAndPost(c);
        }
    }

    public void draw(SurfaceHolder holder, Bitmap bitmap) {
        if (bitmap == null ||
            bitmap.getWidth() != mWidth || bitmap.getHeight() != mHeight)
            return;

        synchronized (mSoftwareBitmap) {
            if (bitmap != mSoftwareBitmap)
                return;

            Canvas c = holder.lockCanvas();
            if (c == null)
                return;
            c.drawBitmap(bitmap, 0, 0, null);
            holder.unlockCanvasAndPost(c);
        }
    }

    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {

        // On pre-Honeycomb, force exactly one frame of the previous size
        // to render because the surface change is only seen by GLES after we
        // have swapped the back buffer (i.e. the buffer size only changes 
        // after the next swap buffer). We need to make sure Gecko's view 
        // resizes when Android's buffer resizes.
        // In Honeycomb, the buffer size changes immediately, so rendering a
        // frame of the previous size is unnecessary (and wrong).
        if (mDrawMode == DRAW_GLES_2 && 
            (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.HONEYCOMB)) {
            // When we get a surfaceChange event, we have 0 to n paint events 
            // waiting in the Gecko event queue. We will make the first
            // succeed and the abort the others.
            mDrawSingleFrame = true;
            if (!mInDrawing) { 
                // Queue at least one paint event in case none are queued.
                GeckoAppShell.scheduleRedraw();
            }
            GeckoAppShell.geckoEventSync();
            mDrawSingleFrame = false;
            mAbortDraw = false;
        }

        if (mShowingSplashScreen)
            drawSplashScreen(holder, width, height);

        mSurfaceLock.lock();

        if (mInDrawing) {
            Log.w(LOG_FILE_NAME, "surfaceChanged while mInDrawing is true!");
        }

        boolean invalidSize;

        if (width == 0 || height == 0) {
            mSoftwareBitmap = null;
            mSoftwareBuffer = null;
            mSoftwareBufferCopy = null;
            invalidSize = true;
        } else {
            invalidSize = false;
        }

        boolean doSyncDraw =
            mDrawMode == DRAW_2D &&
            !invalidSize &&
            GeckoApp.checkLaunchState(GeckoApp.LaunchState.GeckoRunning);
        mSyncDraw = doSyncDraw;

        mFormat = format;
        mWidth = width;
        mHeight = height;
        mSurfaceValid = true;

        Log.i(LOG_FILE_NAME, "surfaceChanged: fmt: " + format + " dim: " + width + " " + height);

        try {
            DisplayMetrics metrics = new DisplayMetrics();
            GeckoApp.mAppContext.getWindowManager().getDefaultDisplay().getMetrics(metrics);

            GeckoEvent e = new GeckoEvent(GeckoEvent.SIZE_CHANGED, width, height,
                                          metrics.widthPixels, metrics.heightPixels);
            GeckoAppShell.sendEventToGecko(e);
        } finally {
            mSurfaceLock.unlock();
        }

        if (doSyncDraw) {
            GeckoAppShell.scheduleRedraw();

            Object syncDrawObject = null;
            try {
                syncDrawObject = mSyncDraws.take();
            } catch (InterruptedException ie) {
                Log.e(LOG_FILE_NAME, "Threw exception while getting sync draw bitmap/buffer: ", ie);
            }
            if (syncDrawObject != null) {
                if (syncDrawObject instanceof Bitmap)
                    draw(holder, (Bitmap)syncDrawObject);
                else
                    draw(holder, (ByteBuffer)syncDrawObject);
            } else {
                Log.e("GeckoSurfaceViewJava", "Synchronised draw object is null");
            }
        } else if (!mShowingSplashScreen) {
            // Make sure a frame is drawn before we return
            // otherwise we see artifacts or a black screen
            GeckoAppShell.scheduleRedraw();
            GeckoAppShell.geckoEventSync();
        }
    }

    public void surfaceCreated(SurfaceHolder holder) {
        Log.i(LOG_FILE_NAME, "surface created");
        GeckoEvent e = new GeckoEvent(GeckoEvent.SURFACE_CREATED);
        GeckoAppShell.sendEventToGecko(e);
        if (mShowingSplashScreen)
            drawSplashScreen();
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(LOG_FILE_NAME, "surface destroyed");
        mSurfaceValid = false;
        mSoftwareBuffer = null;
        mSoftwareBufferCopy = null;
        mSoftwareBitmap = null;
        GeckoEvent e = new GeckoEvent(GeckoEvent.SURFACE_DESTROYED);
        if (mDrawMode == DRAW_GLES_2) {
            // Ensure GL cleanup occurs before we return.
            GeckoAppShell.sendEventToGeckoSync(e);
        } else {
            GeckoAppShell.sendEventToGecko(e);
        }
    }

    public Bitmap getSoftwareDrawBitmap() {
        if (mSoftwareBitmap == null ||
            mSoftwareBitmap.getHeight() != mHeight ||
            mSoftwareBitmap.getWidth() != mWidth) {
            mSoftwareBitmap = Bitmap.createBitmap(mWidth, mHeight, Bitmap.Config.RGB_565);
        }

        mDrawMode = DRAW_2D;
        return mSoftwareBitmap;
    }

    public ByteBuffer getSoftwareDrawBuffer() {
        // We store pixels in 565 format, so two bytes per pixel (explaining
        // the * 2 in the following check/allocation)
        if (mSoftwareBuffer == null ||
            mSoftwareBuffer.capacity() != (mWidth * mHeight * 2)) {
            mSoftwareBuffer = ByteBuffer.allocateDirect(mWidth * mHeight * 2);
        }

        if (mSoftwareBufferCopy == null ||
            mSoftwareBufferCopy.getHeight() != mHeight ||
            mSoftwareBufferCopy.getWidth() != mWidth) {
            mSoftwareBufferCopy = Bitmap.createBitmap(mWidth, mHeight, Bitmap.Config.RGB_565);
        }

        mDrawMode = DRAW_2D;
        return mSoftwareBuffer;
    }

    public Surface getSurface() {
        return getHolder().getSurface();
    }

    /*
     * Called on Gecko thread
     */

    public static final int DRAW_ERROR = 0;
    public static final int DRAW_GLES_2 = 1;
    public static final int DRAW_2D = 2;
    // Drawing is disable when the surface buffer
    // has changed size but we haven't yet processed the
    // resize event.
    public static final int DRAW_DISABLED = 3;

    public int beginDrawing() {
        if (mInDrawing) {
            Log.e(LOG_FILE_NAME, "Recursive beginDrawing call!");
            return DRAW_ERROR;
        }

        // Once we drawn our first frame after resize we can ignore
        // the other draw events until we handle the resize events.
        if (mAbortDraw) {
            return DRAW_DISABLED;
        }

        /* Grab the lock, which we'll hold while we're drawing.
         * It gets released in endDrawing(), and is also used in surfaceChanged
         * to make sure that we don't change our surface details while
         * we're in the middle of drawing (and especially in the middle of
         * executing beginDrawing/endDrawing).
         *
         * We might not need to hold this lock in between
         * beginDrawing/endDrawing, and might just be able to make
         * surfaceChanged, beginDrawing, and endDrawing synchronized,
         * but this way is safer for now.
         */
        mSurfaceLock.lock();

        if (!mSurfaceValid) {
            Log.e(LOG_FILE_NAME, "Surface not valid");
            mSurfaceLock.unlock();
            return DRAW_ERROR;
        }

        mInDrawing = true;
        mDrawMode = DRAW_GLES_2;
        return DRAW_GLES_2;
    }

    public void endDrawing() {
        if (!mInDrawing) {
            Log.e(LOG_FILE_NAME, "endDrawing without beginDrawing!");
            return;
        }

       if (mDrawSingleFrame)
            mAbortDraw = true;

        try {
            if (!mSurfaceValid) {
                Log.e(LOG_FILE_NAME, "endDrawing with false mSurfaceValid");
                return;
            }
        } finally {
            mInDrawing = false;

            if (!mSurfaceLock.isHeldByCurrentThread())
                Log.e(LOG_FILE_NAME, "endDrawing while mSurfaceLock not held by current thread!");

            mSurfaceLock.unlock();
        }
    }

    /* How this works:
     * Whenever we want to draw, we want to be sure that we do not lock
     * the canvas unless we're sure we can draw. Locking the canvas clears
     * the canvas to black in most cases, causing a black flash.
     * At the same time, the surface can resize/disappear at any moment
     * unless the canvas is locked.
     * Draws originate from a different thread so the surface could change
     * at any moment while we try to draw until we lock the canvas.
     *
     * Also, never try to lock the canvas while holding the surface lock
     * unless you're in SurfaceChanged, in which case the canvas was already
     * locked. Surface lock -> Canvas lock will lead to AB-BA deadlocks.
     */
    public void draw2D(Bitmap bitmap, int width, int height) {
        // mSurfaceLock ensures that we get mSyncDraw/mSoftwareBitmap/etc.
        // set correctly before determining whether we should do a sync draw
        mSurfaceLock.lock();
        try {
            if (mSyncDraw) {
                if (bitmap != mSoftwareBitmap || width != mWidth || height != mHeight)
                    return;
                mSyncDraw = false;
                try {
                    mSyncDraws.put(bitmap);
                } catch (InterruptedException ie) {
                    Log.e(LOG_FILE_NAME, "Threw exception while getting sync draws queue: ", ie);
                }
                return;
            }
        } finally {
            mSurfaceLock.unlock();
        }

        draw(getHolder(), bitmap);
    }

    public void draw2D(ByteBuffer buffer, int stride) {
        mSurfaceLock.lock();
        try {
            if (mSyncDraw) {
                if (buffer != mSoftwareBuffer || stride != (mWidth * 2))
                    return;
                mSyncDraw = false;
                try {
                    mSyncDraws.put(buffer);
                } catch (InterruptedException ie) {
                    Log.e(LOG_FILE_NAME, "Threw exception while getting sync bitmaps queue: ", ie);
                }
                return;
            }
        } finally {
            mSurfaceLock.unlock();
        }

        draw(getHolder(), buffer);
    }

    @Override
    public boolean onCheckIsTextEditor () {
        return false;
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT;
        outAttrs.imeOptions = EditorInfo.IME_ACTION_NONE;
        outAttrs.actionLabel = null;
        mKeyListener = TextKeyListener.getInstance();

        if (mIMEState == IME_STATE_PASSWORD)
            outAttrs.inputType |= InputType.TYPE_TEXT_VARIATION_PASSWORD;
        else if (mIMETypeHint.equalsIgnoreCase("url"))
            outAttrs.inputType |= InputType.TYPE_TEXT_VARIATION_URI;
        else if (mIMETypeHint.equalsIgnoreCase("email"))
            outAttrs.inputType |= InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS;
        else if (mIMETypeHint.equalsIgnoreCase("search"))
            outAttrs.imeOptions = EditorInfo.IME_ACTION_SEARCH;
        else if (mIMETypeHint.equalsIgnoreCase("tel"))
            outAttrs.inputType = InputType.TYPE_CLASS_PHONE;
        else if (mIMETypeHint.equalsIgnoreCase("number") ||
                 mIMETypeHint.equalsIgnoreCase("range"))
            outAttrs.inputType = InputType.TYPE_CLASS_NUMBER;
        else if (mIMETypeHint.equalsIgnoreCase("datetime") ||
                 mIMETypeHint.equalsIgnoreCase("datetime-local"))
            outAttrs.inputType = InputType.TYPE_CLASS_DATETIME |
                                 InputType.TYPE_DATETIME_VARIATION_NORMAL;
        else if (mIMETypeHint.equalsIgnoreCase("date"))
            outAttrs.inputType = InputType.TYPE_CLASS_DATETIME |
                                 InputType.TYPE_DATETIME_VARIATION_DATE;
        else if (mIMETypeHint.equalsIgnoreCase("time"))
            outAttrs.inputType = InputType.TYPE_CLASS_DATETIME |
                                 InputType.TYPE_DATETIME_VARIATION_TIME;
        else if (mIMEModeHint.equalsIgnoreCase("numeric"))
            outAttrs.inputType = InputType.TYPE_CLASS_NUMBER |
                                 InputType.TYPE_NUMBER_FLAG_SIGNED |
                                 InputType.TYPE_NUMBER_FLAG_DECIMAL;
        else if (mIMEModeHint.equalsIgnoreCase("digit"))
            outAttrs.inputType = InputType.TYPE_CLASS_NUMBER;
        else if (mIMEModeHint.equalsIgnoreCase("uppercase"))
            outAttrs.inputType |= InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS;
        else if (mIMEModeHint.equalsIgnoreCase("lowercase"))
            outAttrs.inputType = InputType.TYPE_CLASS_TEXT; 
        else if (mIMEModeHint.equalsIgnoreCase("titlecase"))
            outAttrs.inputType |= InputType.TYPE_TEXT_FLAG_CAP_WORDS;
        else if (mIMEModeHint.equalsIgnoreCase("autocapitalized"))
            outAttrs.inputType |= InputType.TYPE_TEXT_FLAG_CAP_SENTENCES;

        if (mIMEActionHint.equalsIgnoreCase("go"))
            outAttrs.imeOptions = EditorInfo.IME_ACTION_GO;
        else if (mIMEActionHint.equalsIgnoreCase("done"))
            outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE;
        else if (mIMEActionHint.equalsIgnoreCase("next"))
            outAttrs.imeOptions = EditorInfo.IME_ACTION_NEXT;
        else if (mIMEActionHint.equalsIgnoreCase("search"))
            outAttrs.imeOptions = EditorInfo.IME_ACTION_SEARCH;
        else if (mIMEActionHint.equalsIgnoreCase("send"))
            outAttrs.imeOptions = EditorInfo.IME_ACTION_SEND;
        else if (mIMEActionHint != null && mIMEActionHint.length() != 0)
            outAttrs.actionLabel = mIMEActionHint;

        if (mIMELandscapeFS == false)
            outAttrs.imeOptions |= EditorInfo.IME_FLAG_NO_EXTRACT_UI;

        inputConnection.reset();
        return inputConnection;
    }

    public void setEditable(String contents)
    {
        mEditable.removeSpan(inputConnection);
        mEditable.replace(0, mEditable.length(), contents);
        mEditable.setSpan(inputConnection, 0, contents.length(), Spanned.SPAN_INCLUSIVE_INCLUSIVE);
        Selection.setSelection(mEditable, contents.length());
    }

    public void initEditable(String contents)
    {
        mEditable = mEditableFactory.newEditable(contents);
        mEditable.setSpan(inputConnection, 0, contents.length(), Spanned.SPAN_INCLUSIVE_INCLUSIVE);
        Selection.setSelection(mEditable, contents.length());
    }

    // accelerometer
    public void onAccuracyChanged(Sensor sensor, int accuracy)
    {
    }

    public void onSensorChanged(SensorEvent event)
    {
        GeckoAppShell.sendEventToGecko(new GeckoEvent(event));
    }

    // geolocation
    public void onLocationChanged(Location location)
    {
        GeckoAppShell.sendEventToGecko(new GeckoEvent(location));
    }

    public void onProviderDisabled(String provider)
    {
    }

    public void onProviderEnabled(String provider)
    {
    }

    public void onStatusChanged(String provider, int status, Bundle extras)
    {
    }

    // event stuff
    public boolean onTouchEvent(MotionEvent event) {
        GeckoAppShell.sendEventToGecko(new GeckoEvent(event));
        return true;
    }

    @Override
    public boolean onKeyPreIme(int keyCode, KeyEvent event) {
        if (event.isSystem())
            return super.onKeyPreIme(keyCode, event);

        switch (event.getAction()) {
            case KeyEvent.ACTION_DOWN:
                return processKeyDown(keyCode, event, true);
            case KeyEvent.ACTION_UP:
                return processKeyUp(keyCode, event, true);
            case KeyEvent.ACTION_MULTIPLE:
                return onKeyMultiple(keyCode, event.getRepeatCount(), event);
        }
        return super.onKeyPreIme(keyCode, event);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        return processKeyDown(keyCode, event, false);
    }

    private boolean processKeyDown(int keyCode, KeyEvent event, boolean isPreIme) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BACK:
                if (event.getRepeatCount() == 0) {
                    event.startTracking();
                    return true;
                } else {
                    return false;
                }
            case KeyEvent.KEYCODE_MENU:
                if (event.getRepeatCount() == 0) {
                    event.startTracking();
                    break;
                } else if ((event.getFlags() & KeyEvent.FLAG_LONG_PRESS) != 0) {
                    break;
                }
                // Ignore repeats for KEYCODE_MENU; they confuse the widget code.
                return false;
            case KeyEvent.KEYCODE_VOLUME_UP:
            case KeyEvent.KEYCODE_VOLUME_DOWN:
            case KeyEvent.KEYCODE_SEARCH:
                return false;
            case KeyEvent.KEYCODE_DEL:
                // See comments in GeckoInputConnection.onKeyDel
                if (inputConnection != null &&
                    inputConnection.onKeyDel()) {
                    return true;
                }
                break;
            case KeyEvent.KEYCODE_ENTER:
                if ((event.getFlags() & KeyEvent.FLAG_EDITOR_ACTION) != 0 &&
                    mIMEActionHint.equalsIgnoreCase("next"))
                    event = new KeyEvent(event.getAction(), KeyEvent.KEYCODE_TAB);
                break;
            default:
                break;
        }

        if (isPreIme && mIMEState != IME_STATE_DISABLED &&
            (event.getMetaState() & KeyEvent.META_ALT_ON) == 0)
            // Let active IME process pre-IME key events
            return false;

        // KeyListener returns true if it handled the event for us.
        if (mIMEState == IME_STATE_DISABLED ||
            keyCode == KeyEvent.KEYCODE_ENTER ||
            keyCode == KeyEvent.KEYCODE_DEL ||
            (event.getFlags() & KeyEvent.FLAG_SOFT_KEYBOARD) != 0 ||
            !mKeyListener.onKeyDown(this, mEditable, keyCode, event))
            GeckoAppShell.sendEventToGecko(new GeckoEvent(event));
        return true;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        return processKeyUp(keyCode, event, false);
    }

    private boolean processKeyUp(int keyCode, KeyEvent event, boolean isPreIme) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BACK:
                if (!event.isTracking() || event.isCanceled())
                    return false;
                break;
            default:
                break;
        }

        if (isPreIme && mIMEState != IME_STATE_DISABLED &&
            (event.getMetaState() & KeyEvent.META_ALT_ON) == 0)
            // Let active IME process pre-IME key events
            return false;

        if (mIMEState == IME_STATE_DISABLED ||
            keyCode == KeyEvent.KEYCODE_ENTER ||
            keyCode == KeyEvent.KEYCODE_DEL ||
            (event.getFlags() & KeyEvent.FLAG_SOFT_KEYBOARD) != 0 ||
            !mKeyListener.onKeyUp(this, mEditable, keyCode, event))
            GeckoAppShell.sendEventToGecko(new GeckoEvent(event));
        return true;
    }

    @Override
    public boolean onKeyMultiple(int keyCode, int repeatCount, KeyEvent event) {
        GeckoAppShell.sendEventToGecko(new GeckoEvent(event));
        return true;
    }

    @Override
    public boolean onKeyLongPress(int keyCode, KeyEvent event) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BACK:
                GeckoAppShell.sendEventToGecko(new GeckoEvent(event));
                return true;
            case KeyEvent.KEYCODE_MENU:
                InputMethodManager imm = (InputMethodManager)
                    getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
                imm.toggleSoftInputFromWindow(getWindowToken(),
                                              imm.SHOW_FORCED, 0);
                return true;
            default:
                break;
        }
        return false;
    }

    // Is this surface valid for drawing into?
    boolean mSurfaceValid;

    // Are we actively between beginDrawing/endDrawing?
    boolean mInDrawing;

    // Used to finish the current buffer before changing the surface size
    boolean mDrawSingleFrame = false;
    boolean mAbortDraw = false;

    // Are we waiting for a buffer to draw in surfaceChanged?
    boolean mSyncDraw;

    // True if gecko requests a buffer
    int mDrawMode;

    static boolean mShowingSplashScreen = true;
    static String  mSplashURL = "";

    // let's not change stuff around while we're in the middle of
    // starting drawing, ending drawing, or changing surface
    // characteristics
    ReentrantLock mSurfaceLock;

    // Surface format, from surfaceChanged.  Largely
    // useless.
    int mFormat;

    // the dimensions of the surface
    int mWidth;
    int mHeight;

    // the dimensions of the buffer we're using for drawing,
    // that is the software buffer or the EGLSurface
    int mBufferWidth;
    int mBufferHeight;

    // IME stuff
    public static final int IME_STATE_DISABLED = 0;
    public static final int IME_STATE_ENABLED = 1;
    public static final int IME_STATE_PASSWORD = 2;
    public static final int IME_STATE_PLUGIN = 3;

    GeckoInputConnection inputConnection;
    KeyListener mKeyListener;
    Editable mEditable;
    Editable.Factory mEditableFactory;
    int mIMEState;
    String mIMETypeHint;
    String mIMEModeHint;
    String mIMEActionHint;
    boolean mIMELandscapeFS;

    // Software rendering
    Bitmap mSoftwareBitmap;
    ByteBuffer mSoftwareBuffer;
    Bitmap mSoftwareBufferCopy;

    final SynchronousQueue<Object> mSyncDraws = new SynchronousQueue<Object>();
}

