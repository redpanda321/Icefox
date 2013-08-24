/* -*- Mode: Java; tab-width: 20; indent-tabs-mode: nil; -*-
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
 * Portions created by the Initial Developer are Copyright (C) 2009-2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
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

package org.mozilla.gecko;

import android.os.*;
import android.app.*;
import android.view.*;
import android.content.*;
import android.graphics.*;
import android.widget.*;
import android.hardware.*;
import android.location.*;

import android.util.Log;

/* We're not allowed to hold on to most events given to us
 * so we save the parts of the events we want to use in GeckoEvent.
 * Fields have different meanings depending on the event type.
 */

public class GeckoEvent {
    public static final int INVALID = -1;
    public static final int NATIVE_POKE = 0;
    public static final int KEY_EVENT = 1;
    public static final int MOTION_EVENT = 2;
    public static final int SENSOR_EVENT = 3;
    public static final int LOCATION_EVENT = 4;
    public static final int IME_EVENT = 5;
    public static final int DRAW = 6;
    public static final int SIZE_CHANGED = 7;
    public static final int ACTIVITY_STOPPING = 8;
    public static final int ACTIVITY_PAUSING = 9;
    public static final int LOAD_URI = 10;

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

    public int mType;
    public int mAction;
    public long mTime;
    public Point mP0, mP1;
    public Rect mRect;
    public float mX, mY, mZ;

    public int mMetaState, mFlags;
    public int mKeyCode, mUnicodeChar;
    public int mOffset, mCount;
    public String mCharacters;
    public int mRangeType, mRangeStyles;
    public int mRangeForeColor, mRangeBackColor;
    public Location mLocation;

    public int mNativeWindow;

    public GeckoEvent() {
        mType = NATIVE_POKE;
    }

    public GeckoEvent(int evType) {
        mType = evType;
    }

    public GeckoEvent(KeyEvent k) {
        mType = KEY_EVENT;
        mAction = k.getAction();
        mTime = k.getEventTime();
        mMetaState = k.getMetaState();
        mFlags = k.getFlags();
        mKeyCode = k.getKeyCode();
        mUnicodeChar = k.getUnicodeChar();
        mCharacters = k.getCharacters();
    }

    public GeckoEvent(MotionEvent m) {
        mType = MOTION_EVENT;
        mAction = m.getAction();
        mTime = m.getEventTime();
        mP0 = new Point((int)m.getX(0), (int)m.getY(0));
        mCount = m.getPointerCount();
        if (mCount > 1)
            mP1 = new Point((int)m.getX(1), (int)m.getY(1));
    }

    public GeckoEvent(SensorEvent s) {
        mType = SENSOR_EVENT;
        mX = s.values[0] / SensorManager.GRAVITY_EARTH;
        mY = s.values[1] / SensorManager.GRAVITY_EARTH;
        mZ = s.values[2] / SensorManager.GRAVITY_EARTH;
    }

    public GeckoEvent(Location l) {
        mType = LOCATION_EVENT;
        mLocation = l;
    }

    public GeckoEvent(int imeAction, int offset, int count) {
        mType = IME_EVENT;
        mAction = imeAction;
        mOffset = offset;
        mCount = count;
    }

    private void InitIMERange(int action, int offset, int count,
                              int rangeType, int rangeStyles,
                              int rangeForeColor, int rangeBackColor) {
        mType = IME_EVENT;
        mAction = action;
        mOffset = offset;
        mCount = count;
        mRangeType = rangeType;
        mRangeStyles = rangeStyles;
        mRangeForeColor = rangeForeColor;
        mRangeBackColor = rangeBackColor;
        return;
    }
    
    public GeckoEvent(int offset, int count,
                      int rangeType, int rangeStyles,
                      int rangeForeColor, int rangeBackColor, String text) {
        InitIMERange(IME_SET_TEXT, offset, count, rangeType, rangeStyles,
                     rangeForeColor, rangeBackColor);
        mCharacters = text;
    }

    public GeckoEvent(int offset, int count,
                      int rangeType, int rangeStyles,
                      int rangeForeColor, int rangeBackColor) {
        InitIMERange(IME_ADD_RANGE, offset, count, rangeType, rangeStyles,
                     rangeForeColor, rangeBackColor);
    }

    public GeckoEvent(int etype, Rect dirty) {
        if (etype != DRAW) {
            mType = INVALID;
            return;
        }

        mType = etype;
        mRect = dirty;
    }

    public GeckoEvent(int etype, int w, int h, int oldw, int oldh) {
        if (etype != SIZE_CHANGED) {
            mType = INVALID;
            return;
        }

        mType = etype;

        mP0 = new Point(w, h);
        mP1 = new Point(oldw, oldh);
    }

    public GeckoEvent(String uri) {
        mType = LOAD_URI;
        mCharacters = uri;
    }
}
