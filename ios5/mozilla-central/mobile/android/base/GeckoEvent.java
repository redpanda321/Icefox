/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import org.mozilla.gecko.gfx.DisplayPortMetrics;
import org.mozilla.gecko.gfx.IntSize;
import org.mozilla.gecko.gfx.ViewportMetrics;
import android.os.*;
import android.app.*;
import android.view.*;
import android.content.*;
import android.graphics.*;
import android.widget.*;
import android.hardware.*;
import android.location.*;
import android.util.FloatMath;
import android.util.DisplayMetrics;
import android.graphics.PointF;
import android.text.format.Time;
import android.os.SystemClock;
import java.lang.Math;
import java.lang.System;
import java.nio.ByteBuffer;

import android.util.Log;

/* We're not allowed to hold on to most events given to us
 * so we save the parts of the events we want to use in GeckoEvent.
 * Fields have different meanings depending on the event type.
 */

/* This class is referenced by Robocop via reflection; use care when 
 * modifying the signature.
 */
public class GeckoEvent {
    private static final String LOGTAG = "GeckoEvent";

    private static final int INVALID = -1;
    private static final int NATIVE_POKE = 0;
    private static final int KEY_EVENT = 1;
    private static final int MOTION_EVENT = 2;
    private static final int SENSOR_EVENT = 3;
    private static final int UNUSED1_EVENT = 4;
    private static final int LOCATION_EVENT = 5;
    private static final int IME_EVENT = 6;
    private static final int DRAW = 7;
    private static final int SIZE_CHANGED = 8;
    private static final int ACTIVITY_STOPPING = 9;
    private static final int ACTIVITY_PAUSING = 10;
    private static final int ACTIVITY_SHUTDOWN = 11;
    private static final int LOAD_URI = 12;
    private static final int SURFACE_CREATED = 13;
    private static final int SURFACE_DESTROYED = 14;
    private static final int GECKO_EVENT_SYNC = 15;
    private static final int ACTIVITY_START = 17;
    private static final int BROADCAST = 19;
    private static final int VIEWPORT = 20;
    private static final int VISITED = 21;
    private static final int NETWORK_CHANGED = 22;
    private static final int UNUSED3_EVENT = 23;
    private static final int ACTIVITY_RESUMING = 24;
    private static final int SCREENSHOT = 25;
    private static final int UNUSED2_EVENT = 26;
    private static final int SCREENORIENTATION_CHANGED = 27;
    private static final int COMPOSITOR_PAUSE = 28;
    private static final int COMPOSITOR_RESUME = 29;
    private static final int PAINT_LISTEN_START_EVENT = 30;

    public static final int IME_COMPOSITION_END = 0;
    public static final int IME_COMPOSITION_BEGIN = 1;
    public static final int IME_SET_TEXT = 2;
    public static final int IME_GET_TEXT = 3;
    public static final int IME_DELETE_TEXT = 4;
    public static final int IME_SET_SELECTION = 5;
    public static final int IME_GET_SELECTION = 6;
    public static final int IME_ADD_RANGE = 7;

    public static final int IME_RANGE_CARETPOSITION = 1;
    public static final int IME_RANGE_RAWINPUT = 2;
    public static final int IME_RANGE_SELECTEDRAWTEXT = 3;
    public static final int IME_RANGE_CONVERTEDTEXT = 4;
    public static final int IME_RANGE_SELECTEDCONVERTEDTEXT = 5;

    public static final int IME_RANGE_UNDERLINE = 1;
    public static final int IME_RANGE_FORECOLOR = 2;
    public static final int IME_RANGE_BACKCOLOR = 4;

    final public int mType;
    public int mAction;
    public long mTime;
    public Point[] mPoints;
    public int[] mPointIndicies;
    public int mPointerIndex; // index of the point that has changed
    public float[] mOrientations;
    public float[] mPressures;
    public Point[] mPointRadii;
    public Rect mRect;
    public double mX, mY, mZ;

    public int mMetaState, mFlags;
    public int mKeyCode, mUnicodeChar;
    public int mRepeatCount;
    public int mOffset, mCount;
    public String mCharacters, mCharactersExtra;
    public int mRangeType, mRangeStyles;
    public int mRangeForeColor, mRangeBackColor;
    public Location mLocation;
    public Address  mAddress;

    public double mBandwidth;
    public boolean mCanBeMetered;

    public int mNativeWindow;

    public short mScreenOrientation;

    public ByteBuffer mBuffer;

    private GeckoEvent(int evType) {
        mType = evType;
    }

    public static GeckoEvent createPauseEvent(boolean isApplicationInBackground) {
        GeckoEvent event = new GeckoEvent(ACTIVITY_PAUSING);
        event.mFlags = isApplicationInBackground ? 0 : 1;
        return event;
    }

    public static GeckoEvent createResumeEvent(boolean isApplicationInBackground) {
        GeckoEvent event = new GeckoEvent(ACTIVITY_RESUMING);
        event.mFlags = isApplicationInBackground ? 0 : 1;
        return event;
    }

    public static GeckoEvent createStoppingEvent(boolean isApplicationInBackground) {
        GeckoEvent event = new GeckoEvent(ACTIVITY_STOPPING);
        event.mFlags = isApplicationInBackground ? 0 : 1;
        return event;
    }

    public static GeckoEvent createStartEvent(boolean isApplicationInBackground) {
        GeckoEvent event = new GeckoEvent(ACTIVITY_START);
        event.mFlags = isApplicationInBackground ? 0 : 1;
        return event;
    }

    public static GeckoEvent createShutdownEvent() {
        return new GeckoEvent(ACTIVITY_SHUTDOWN);
    }

    public static GeckoEvent createSyncEvent() {
        return new GeckoEvent(GECKO_EVENT_SYNC);
    }

    public static GeckoEvent createKeyEvent(KeyEvent k) {
        GeckoEvent event = new GeckoEvent(KEY_EVENT);
        event.initKeyEvent(k);
        return event;
    }

    public static GeckoEvent createCompositorPauseEvent() {
        return new GeckoEvent(COMPOSITOR_PAUSE);
    }

    public static GeckoEvent createCompositorResumeEvent() {
        return new GeckoEvent(COMPOSITOR_RESUME);
    }

    private void initKeyEvent(KeyEvent k) {
        mAction = k.getAction();
        mTime = k.getEventTime();
        mMetaState = k.getMetaState();
        mFlags = k.getFlags();
        mKeyCode = k.getKeyCode();
        mUnicodeChar = k.getUnicodeChar();
        mRepeatCount = k.getRepeatCount();
        mCharacters = k.getCharacters();
    }

    public static GeckoEvent createMotionEvent(MotionEvent m) {
        GeckoEvent event = new GeckoEvent(MOTION_EVENT);
        event.initMotionEvent(m);
        return event;
    }

    private void initMotionEvent(MotionEvent m) {
        mAction = m.getAction();
        mTime = (System.currentTimeMillis() - SystemClock.elapsedRealtime()) + m.getEventTime();
        mMetaState = m.getMetaState();

        switch (mAction & MotionEvent.ACTION_MASK) {
            case MotionEvent.ACTION_CANCEL:
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_MOVE:
            case MotionEvent.ACTION_HOVER_ENTER:
            case MotionEvent.ACTION_HOVER_MOVE:
            case MotionEvent.ACTION_HOVER_EXIT: {
                mCount = m.getPointerCount();
                mPoints = new Point[mCount];
                mPointIndicies = new int[mCount];
                mOrientations = new float[mCount];
                mPressures = new float[mCount];
                mPointRadii = new Point[mCount];
                mPointerIndex = (mAction & MotionEvent.ACTION_POINTER_INDEX_MASK) >> MotionEvent.ACTION_POINTER_INDEX_SHIFT;
                for (int i = 0; i < mCount; i++) {
                    addMotionPoint(i, i, m);
                }
                break;
            }
            default: {
                mCount = 0;
                mPointerIndex = -1;
                mPoints = new Point[mCount];
                mPointIndicies = new int[mCount];
                mOrientations = new float[mCount];
                mPressures = new float[mCount];
                mPointRadii = new Point[mCount];
            }
        }
    }

    public void addMotionPoint(int index, int eventIndex, MotionEvent event) {
        try {
            PointF geckoPoint = new PointF(event.getX(eventIndex), event.getY(eventIndex));
            geckoPoint = GeckoApp.mAppContext.getLayerController().convertViewPointToLayerPoint(geckoPoint);
    
            mPoints[index] = new Point(Math.round(geckoPoint.x), Math.round(geckoPoint.y));
            mPointIndicies[index] = event.getPointerId(eventIndex);
            // getToolMajor, getToolMinor and getOrientation are API Level 9 features
            if (Build.VERSION.SDK_INT >= 9) {
                double radians = event.getOrientation(eventIndex);
                mOrientations[index] = (float) Math.toDegrees(radians);
                // w3c touchevents spec does not allow orientations == 90
                // this shifts it to -90, which will be shifted to zero below
                if (mOrientations[index] == 90)
                    mOrientations[index] = -90;
    
                // w3c touchevent radius are given by an orientation between 0 and 90
                // the radius is found by removing the orientation and measuring the x and y
                // radius of the resulting ellipse
                // for android orientations >= 0 and < 90, the major axis should correspond to
                // just reporting the y radius as the major one, and x as minor
                // however, for a radius < 0, we have to shift the orientation by adding 90, and
                // reverse which radius is major and minor
                if (mOrientations[index] < 0) {
                    mOrientations[index] += 90;
                    mPointRadii[index] = new Point((int)event.getToolMajor(eventIndex)/2,
                                                   (int)event.getToolMinor(eventIndex)/2);
                } else {
                    mPointRadii[index] = new Point((int)event.getToolMinor(eventIndex)/2,
                                                   (int)event.getToolMajor(eventIndex)/2);
                }
            } else {
                float size = event.getSize(eventIndex);
                DisplayMetrics displaymetrics = GeckoApp.mAppContext.getDisplayMetrics();
                size = size*Math.min(displaymetrics.heightPixels, displaymetrics.widthPixels);
                mPointRadii[index] = new Point((int)size,(int)size);
                mOrientations[index] = 0;
            }
            mPressures[index] = event.getPressure(eventIndex);
        } catch(Exception ex) {
            Log.e(LOGTAG, "Error creating motion point " + index, ex);
            mPointRadii[index] = new Point(0, 0);
            mPoints[index] = new Point(0, 0);
        }
    }

    private static int HalSensorAccuracyFor(int androidAccuracy) {
        switch (androidAccuracy) {
        case SensorManager.SENSOR_STATUS_UNRELIABLE:
            return GeckoHalDefines.SENSOR_ACCURACY_UNRELIABLE;
        case SensorManager.SENSOR_STATUS_ACCURACY_LOW:
            return GeckoHalDefines.SENSOR_ACCURACY_LOW;
        case SensorManager.SENSOR_STATUS_ACCURACY_MEDIUM:
            return GeckoHalDefines.SENSOR_ACCURACY_MED;
        case SensorManager.SENSOR_STATUS_ACCURACY_HIGH:
            return GeckoHalDefines.SENSOR_ACCURACY_HIGH;
        }
        return GeckoHalDefines.SENSOR_ACCURACY_UNKNOWN;
    }

    public static GeckoEvent createSensorEvent(SensorEvent s) {
        int sensor_type = s.sensor.getType();
        GeckoEvent event = null;

        switch(sensor_type) {

        case Sensor.TYPE_ACCELEROMETER:
            event = new GeckoEvent(SENSOR_EVENT);
            event.mFlags = GeckoHalDefines.SENSOR_ACCELERATION;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = s.values[0];
            event.mY = s.values[1];
            event.mZ = s.values[2];
            break;

        case 10 /* Requires API Level 9, so just use the raw value - Sensor.TYPE_LINEAR_ACCELEROMETER*/ :
            event = new GeckoEvent(SENSOR_EVENT);
            event.mFlags = GeckoHalDefines.SENSOR_LINEAR_ACCELERATION;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = s.values[0];
            event.mY = s.values[1];
            event.mZ = s.values[2];
            break;

        case Sensor.TYPE_ORIENTATION:
            event = new GeckoEvent(SENSOR_EVENT);
            event.mFlags = GeckoHalDefines.SENSOR_ORIENTATION;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = s.values[0];
            event.mY = s.values[1];
            event.mZ = s.values[2];
            break;

        case Sensor.TYPE_GYROSCOPE:
            event = new GeckoEvent(SENSOR_EVENT);
            event.mFlags = GeckoHalDefines.SENSOR_GYROSCOPE;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = Math.toDegrees(s.values[0]);
            event.mY = Math.toDegrees(s.values[1]);
            event.mZ = Math.toDegrees(s.values[2]);
            break;

        case Sensor.TYPE_PROXIMITY:
            event = new GeckoEvent(SENSOR_EVENT);
            event.mFlags = GeckoHalDefines.SENSOR_PROXIMITY;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = s.values[0];
            event.mY = 0;
            event.mZ = s.sensor.getMaximumRange();
            break;

        case Sensor.TYPE_LIGHT:
            event = new GeckoEvent(SENSOR_EVENT);
            event.mFlags = GeckoHalDefines.SENSOR_LIGHT;
            event.mMetaState = HalSensorAccuracyFor(s.accuracy);
            event.mX = s.values[0];
            break;
        }
        return event;
    }

    public static GeckoEvent createLocationEvent(Location l) {
        GeckoEvent event = new GeckoEvent(LOCATION_EVENT);
        event.mLocation = l;
        return event;
    }

    public static GeckoEvent createIMEEvent(int imeAction, int offset, int count) {
        GeckoEvent event = new GeckoEvent(IME_EVENT);
        event.mAction = imeAction;
        event.mOffset = offset;
        event.mCount = count;
        return event;
    }

    private void InitIMERange(int action, int offset, int count,
                              int rangeType, int rangeStyles,
                              int rangeForeColor, int rangeBackColor) {
        mAction = action;
        mOffset = offset;
        mCount = count;
        mRangeType = rangeType;
        mRangeStyles = rangeStyles;
        mRangeForeColor = rangeForeColor;
        mRangeBackColor = rangeBackColor;
        return;
    }
    
    public static GeckoEvent createIMERangeEvent(int offset, int count,
                                                 int rangeType, int rangeStyles,
                                                 int rangeForeColor, int rangeBackColor,
                                                 String text) {
        GeckoEvent event = new GeckoEvent(IME_EVENT);
        event.InitIMERange(IME_SET_TEXT, offset, count, rangeType, rangeStyles,
                           rangeForeColor, rangeBackColor);
        event.mCharacters = text;
        return event;
    }

    public static GeckoEvent createIMERangeEvent(int offset, int count,
                                                 int rangeType, int rangeStyles,
                                                 int rangeForeColor, int rangeBackColor) {
        GeckoEvent event = new GeckoEvent(IME_EVENT);
        event.InitIMERange(IME_ADD_RANGE, offset, count, rangeType, rangeStyles,
                           rangeForeColor, rangeBackColor);
        return event;
    }

    public static GeckoEvent createDrawEvent(Rect rect) {
        GeckoEvent event = new GeckoEvent(DRAW);
        event.mRect = rect;
        return event;
    }

    public static GeckoEvent createSizeChangedEvent(int w, int h, int screenw, int screenh) {
        GeckoEvent event = new GeckoEvent(SIZE_CHANGED);
        event.mPoints = new Point[2];
        event.mPoints[0] = new Point(w, h);
        event.mPoints[1] = new Point(screenw, screenh);
        return event;
    }

    public static GeckoEvent createBroadcastEvent(String subject, String data) {
        GeckoEvent event = new GeckoEvent(BROADCAST);
        event.mCharacters = subject;
        event.mCharactersExtra = data;
        return event;
    }

    public static GeckoEvent createViewportEvent(ViewportMetrics viewport, DisplayPortMetrics displayPort) {
        GeckoEvent event = new GeckoEvent(VIEWPORT);
        event.mCharacters = "Viewport:Change";
        PointF origin = viewport.getOrigin();
        StringBuffer sb = new StringBuffer(256);
        sb.append("{ \"x\" : ").append(origin.x)
          .append(", \"y\" : ").append(origin.y)
          .append(", \"zoom\" : ").append(viewport.getZoomFactor())
          .append(", \"displayPort\" :").append(displayPort.toJSON())
          .append('}');
        event.mCharactersExtra = sb.toString();
        return event;
    }

    public static GeckoEvent createURILoadEvent(String uri) {
        GeckoEvent event = new GeckoEvent(LOAD_URI);
        event.mCharacters = uri;
        event.mCharactersExtra = "";
        return event;
    }

    public static GeckoEvent createWebappLoadEvent(String uri) {
        GeckoEvent event = new GeckoEvent(LOAD_URI);
        event.mCharacters = uri;
        event.mCharactersExtra = "-webapp";
        return event;
    }

    public static GeckoEvent createBookmarkLoadEvent(String uri) {
        GeckoEvent event = new GeckoEvent(LOAD_URI);
        event.mCharacters = uri;
        event.mCharactersExtra = "-bookmark";
        return event;
    }

    public static GeckoEvent createVisitedEvent(String data) {
        GeckoEvent event = new GeckoEvent(VISITED);
        event.mCharacters = data;
        return event;
    }

    public static GeckoEvent createNetworkEvent(double bandwidth, boolean canBeMetered) {
        GeckoEvent event = new GeckoEvent(NETWORK_CHANGED);
        event.mBandwidth = bandwidth;
        event.mCanBeMetered = canBeMetered;
        return event;
    }

    public static GeckoEvent createScreenshotEvent(int tabId, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, int bw, int bh, int token, ByteBuffer buffer) {
        GeckoEvent event = new GeckoEvent(SCREENSHOT);
        event.mPoints = new Point[5];
        event.mPoints[0] = new Point(sx, sy);
        event.mPoints[1] = new Point(sw, sh);
        event.mPoints[2] = new Point(dx, dy);
        event.mPoints[3] = new Point(dw, dh);
        event.mPoints[4] = new Point(bw, bh);
        event.mMetaState = tabId;
        event.mFlags = token;
        event.mBuffer = buffer;
        return event;
    }

    public static GeckoEvent createScreenOrientationEvent(short aScreenOrientation) {
        GeckoEvent event = new GeckoEvent(SCREENORIENTATION_CHANGED);
        event.mScreenOrientation = aScreenOrientation;
        return event;
    }

    public static GeckoEvent createStartPaintListentingEvent(int tabId) {
        GeckoEvent event = new GeckoEvent(PAINT_LISTEN_START_EVENT);
        event.mMetaState = tabId;
        return event;
    }
}
