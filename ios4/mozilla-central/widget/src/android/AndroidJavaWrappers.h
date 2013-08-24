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

#ifndef AndroidJavaWrappers_h__
#define AndroidJavaWrappers_h__

#include <jni.h>
#include <android/log.h>

#include "nsGeoPosition.h"
#include "nsPoint.h"
#include "nsRect.h"
#include "nsString.h"

//#define FORCE_ALOG 1

#ifndef ALOG
#if defined(DEBUG) || defined(FORCE_ALOG)
#define ALOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Gecko" , ## args)
#else
#define ALOG(args...)
#endif
#endif

namespace mozilla {

void InitAndroidJavaWrappers(JNIEnv *jEnv);

/*
 * Note: do not store global refs to any WrappedJavaObject;
 * these are live only during a particular JNI method, as
 * NewGlobalRef is -not- called on the jobject.
 *
 * If this is needed, WrappedJavaObject can be extended to
 * handle it.
 */

class WrappedJavaObject {
public:
    WrappedJavaObject() :
        wrapped_obj(0)
    { }

    WrappedJavaObject(jobject jobj) {
        Init(jobj);
    }

    void Init(jobject jobj) {
        wrapped_obj = jobj;
    }

    bool isNull() const {
        return wrapped_obj == 0;
    }

    jobject wrappedObject() const {
        return wrapped_obj;
    }

protected:
    jobject wrapped_obj;
};

class AndroidPoint : public WrappedJavaObject
{
public:
    static void InitPointClass(JNIEnv *jEnv);

    AndroidPoint() { }
    AndroidPoint(JNIEnv *jenv, jobject jobj) {
        Init(jenv, jobj);
    }

    void Init(JNIEnv *jenv, jobject jobj);

    int X() { return mX; }
    int Y() { return mY; }

protected:
    int mX;
    int mY;

    static jclass jPointClass;
    static jfieldID jXField;
    static jfieldID jYField;
};

class AndroidRect : public WrappedJavaObject
{
public:
    static void InitRectClass(JNIEnv *jEnv);

    AndroidRect() { }
    AndroidRect(JNIEnv *jenv, jobject jobj) {
        Init(jenv, jobj);
    }

    void Init(JNIEnv *jenv, jobject jobj);

    int Bottom() { return mBottom; }
    int Left() { return mLeft; }
    int Right() { return mRight; }
    int Top() { return mTop; }

protected:
    int mBottom;
    int mLeft;
    int mRight;
    int mTop;

    static jclass jRectClass;
    static jfieldID jBottomField;
    static jfieldID jLeftField;
    static jfieldID jRightField;
    static jfieldID jTopField;
};

class AndroidGeckoSurfaceView : public WrappedJavaObject
{
public:
    static void InitGeckoSurfaceViewClass(JNIEnv *jEnv);

    AndroidGeckoSurfaceView() { }
    AndroidGeckoSurfaceView(jobject jobj) {
        Init(jobj);
    }

    void Init(jobject jobj);

    enum {
        DRAW_ERROR = 0,
        DRAW_GLES_2 = 1
    };

    int BeginDrawing();
    jobject GetSoftwareDrawBuffer();
    void EndDrawing();
    void Draw2D(jobject buffer);

    // must have a JNI local frame when calling this,
    // and you'd better know what you're doing
    jobject GetSurfaceHolder();

protected:
    static jclass jGeckoSurfaceViewClass;
    static jmethodID jBeginDrawingMethod;
    static jmethodID jEndDrawingMethod;
    static jmethodID jDraw2DMethod;
    static jmethodID jGetSoftwareDrawBufferMethod;
    static jmethodID jGetHolderMethod;
};

class AndroidKeyEvent
{
public:
    enum {
        KEYCODE_UNKNOWN            = 0,
        KEYCODE_SOFT_LEFT          = 1,
        KEYCODE_SOFT_RIGHT         = 2,
        KEYCODE_HOME               = 3,
        KEYCODE_BACK               = 4,
        KEYCODE_CALL               = 5,
        KEYCODE_ENDCALL            = 6,
        KEYCODE_0                  = 7,
        KEYCODE_1                  = 8,
        KEYCODE_2                  = 9,
        KEYCODE_3                  = 10,
        KEYCODE_4                  = 11,
        KEYCODE_5                  = 12,
        KEYCODE_6                  = 13,
        KEYCODE_7                  = 14,
        KEYCODE_8                  = 15,
        KEYCODE_9                  = 16,
        KEYCODE_STAR               = 17,
        KEYCODE_POUND              = 18,
        KEYCODE_DPAD_UP            = 19,
        KEYCODE_DPAD_DOWN          = 20,
        KEYCODE_DPAD_LEFT          = 21,
        KEYCODE_DPAD_RIGHT         = 22,
        KEYCODE_DPAD_CENTER        = 23,
        KEYCODE_VOLUME_UP          = 24,
        KEYCODE_VOLUME_DOWN        = 25,
        KEYCODE_POWER              = 26,
        KEYCODE_CAMERA             = 27,
        KEYCODE_CLEAR              = 28,
        KEYCODE_A                  = 29,
        KEYCODE_B                  = 30,
        KEYCODE_C                  = 31,
        KEYCODE_D                  = 32,
        KEYCODE_E                  = 33,
        KEYCODE_F                  = 34,
        KEYCODE_G                  = 35,
        KEYCODE_H                  = 36,
        KEYCODE_I                  = 37,
        KEYCODE_J                  = 38,
        KEYCODE_K                  = 39,
        KEYCODE_L                  = 40,
        KEYCODE_M                  = 41,
        KEYCODE_N                  = 42,
        KEYCODE_O                  = 43,
        KEYCODE_P                  = 44,
        KEYCODE_Q                  = 45,
        KEYCODE_R                  = 46,
        KEYCODE_S                  = 47,
        KEYCODE_T                  = 48,
        KEYCODE_U                  = 49,
        KEYCODE_V                  = 50,
        KEYCODE_W                  = 51,
        KEYCODE_X                  = 52,
        KEYCODE_Y                  = 53,
        KEYCODE_Z                  = 54,
        KEYCODE_COMMA              = 55,
        KEYCODE_PERIOD             = 56,
        KEYCODE_ALT_LEFT           = 57,
        KEYCODE_ALT_RIGHT          = 58,
        KEYCODE_SHIFT_LEFT         = 59,
        KEYCODE_SHIFT_RIGHT        = 60,
        KEYCODE_TAB                = 61,
        KEYCODE_SPACE              = 62,
        KEYCODE_SYM                = 63,
        KEYCODE_EXPLORER           = 64,
        KEYCODE_ENVELOPE           = 65,
        KEYCODE_ENTER              = 66,
        KEYCODE_DEL                = 67,
        KEYCODE_GRAVE              = 68,
        KEYCODE_MINUS              = 69,
        KEYCODE_EQUALS             = 70,
        KEYCODE_LEFT_BRACKET       = 71,
        KEYCODE_RIGHT_BRACKET      = 72,
        KEYCODE_BACKSLASH          = 73,
        KEYCODE_SEMICOLON          = 74,
        KEYCODE_APOSTROPHE         = 75,
        KEYCODE_SLASH              = 76,
        KEYCODE_AT                 = 77,
        KEYCODE_NUM                = 78,
        KEYCODE_HEADSETHOOK        = 79,
        KEYCODE_FOCUS              = 80,
        KEYCODE_PLUS               = 81,
        KEYCODE_MENU               = 82,
        KEYCODE_NOTIFICATION       = 83,
        KEYCODE_SEARCH             = 84,
        KEYCODE_MEDIA_PLAY_PAUSE   = 85,
        KEYCODE_MEDIA_STOP         = 86,
        KEYCODE_MEDIA_NEXT         = 87,
        KEYCODE_MEDIA_PREVIOUS     = 88,
        KEYCODE_MEDIA_REWIND       = 89,
        KEYCODE_MEDIA_FAST_FORWARD = 90,
        KEYCODE_MUTE               = 91,
        ACTION_DOWN                = 0,
        ACTION_UP                  = 1,
        ACTION_MULTIPLE            = 2,
        META_ALT_ON                = 0x00000002,
        META_ALT_LEFT_ON           = 0x00000010,
        META_ALT_RIGHT_ON          = 0x00000020,
        META_SHIFT_ON              = 0x00000001,
        META_SHIFT_LEFT_ON         = 0x00000040,
        META_SHIFT_RIGHT_ON        = 0x00000080,
        META_SYM_ON                = 0x00000004,
        FLAG_WOKE_HERE             = 0x00000001,
        FLAG_SOFT_KEYBOARD         = 0x00000002,
        FLAG_KEEP_TOUCH_MODE       = 0x00000004,
        FLAG_FROM_SYSTEM           = 0x00000008,
        FLAG_EDITOR_ACTION         = 0x00000010,
        FLAG_CANCELED              = 0x00000020,
        FLAG_VIRTUAL_HARD_KEY      = 0x00000040,
        FLAG_LONG_PRESS            = 0x00000080,
        FLAG_CANCELED_LONG_PRESS   = 0x00000100,
        FLAG_TRACKING              = 0x00000200,
        FLAG_START_TRACKING        = 0x40000000,
        dummy_java_enum_list_end
    };
};

class AndroidMotionEvent
{
public:
    enum {
        ACTION_MASK = 0xff,
        ACTION_DOWN = 0,
        ACTION_UP = 1,
        ACTION_MOVE = 2,
        ACTION_CANCEL = 3,
        ACTION_OUTSIDE = 4,
        ACTION_POINTER_DOWN = 5,
        ACTION_POINTER_UP = 6,
        ACTION_POINTER_ID_MASK = 0xff00,
        ACTION_POINTER_ID_SHIFT = 8,
        EDGE_TOP = 0x00000001,
        EDGE_BOTTOM = 0x00000002,
        EDGE_LEFT = 0x00000004,
        EDGE_RIGHT = 0x00000008,
        SAMPLE_X = 0,
        SAMPLE_Y = 1,
        SAMPLE_PRESSURE = 2,
        SAMPLE_SIZE = 3,
        NUM_SAMPLE_DATA = 4,
        dummy_java_enum_list_end
    };
};

class AndroidLocation : public WrappedJavaObject
{
public:
    static void InitLocationClass(JNIEnv *jEnv);
    static nsGeoPosition* CreateGeoPosition(JNIEnv *jenv, jobject jobj);
    static jclass jLocationClass;
    static jmethodID jGetLatitudeMethod;
    static jmethodID jGetLongitudeMethod;
    static jmethodID jGetAltitudeMethod;
    static jmethodID jGetAccuracyMethod;
    static jmethodID jGetBearingMethod;
    static jmethodID jGetSpeedMethod;
    static jmethodID jGetTimeMethod;
};

class AndroidGeckoEvent : public WrappedJavaObject
{
public:
    static void InitGeckoEventClass(JNIEnv *jEnv);

    AndroidGeckoEvent() { }
    AndroidGeckoEvent(int aType) {
        Init(aType);
    }
    AndroidGeckoEvent(void *window, int x1, int y1, int x2, int y2) {
        Init(window, x1, y1, x2, y2);
    }
    AndroidGeckoEvent(JNIEnv *jenv, jobject jobj) {
        Init(jenv, jobj);
    }

    void Init(JNIEnv *jenv, jobject jobj);
    void Init(int aType);
    void Init(void *window, int x1, int y1, int x2, int y2);

    int Action() { return mAction; }
    int Type() { return mType; }
    int64_t Time() { return mTime; }
    void *NativeWindow() { return mNativeWindow; }
    const nsIntPoint& P0() { return mP0; }
    const nsIntPoint& P1() { return mP1; }
    float X() { return mX; }
    float Y() { return mY; }
    float Z() { return mZ; }
    const nsIntRect& Rect() { return mRect; }
    nsAString& Characters() { return mCharacters; }
    int KeyCode() { return mKeyCode; }
    int MetaState() { return mMetaState; }
    int Flags() { return mFlags; }
    int UnicodeChar() { return mUnicodeChar; }
    int Offset() { return mOffset; }
    int Count() { return mCount; }
    int RangeType() { return mRangeType; }
    int RangeStyles() { return mRangeStyles; }
    int RangeForeColor() { return mRangeForeColor; }
    int RangeBackColor() { return mRangeBackColor; }
    nsGeoPosition* GeoPosition() { return mGeoPosition; }

protected:
    int mAction;
    int mType;
    int64_t mTime;
    void *mNativeWindow;
    nsIntPoint mP0;
    nsIntPoint mP1;
    nsIntRect mRect;
    int mFlags, mMetaState;
    int mKeyCode, mUnicodeChar;
    int mOffset, mCount;
    int mRangeType, mRangeStyles;
    int mRangeForeColor, mRangeBackColor;
    float mX, mY, mZ;
    nsString mCharacters;
    nsRefPtr<nsGeoPosition> mGeoPosition;

    void ReadP0Field(JNIEnv *jenv);
    void ReadP1Field(JNIEnv *jenv);
    void ReadRectField(JNIEnv *jenv);
    void ReadCharactersField(JNIEnv *jenv);

    static jclass jGeckoEventClass;
    static jfieldID jActionField;
    static jfieldID jTypeField;
    static jfieldID jTimeField;
    static jfieldID jP0Field;
    static jfieldID jP1Field;
    static jfieldID jXField;
    static jfieldID jYField;
    static jfieldID jZField;
    static jfieldID jRectField;
    static jfieldID jNativeWindowField;

    static jfieldID jCharactersField;
    static jfieldID jKeyCodeField;
    static jfieldID jMetaStateField;
    static jfieldID jFlagsField;
    static jfieldID jOffsetField;
    static jfieldID jCountField;
    static jfieldID jUnicodeCharField;
    static jfieldID jRangeTypeField;
    static jfieldID jRangeStylesField;
    static jfieldID jRangeForeColorField;
    static jfieldID jRangeBackColorField;
    static jfieldID jLocationField;

public:
    enum {
        NATIVE_POKE = 0,
        KEY_EVENT = 1,
        MOTION_EVENT = 2,
        SENSOR_EVENT = 3,
        LOCATION_EVENT = 4,
        IME_EVENT = 5,
        DRAW = 6,
        SIZE_CHANGED = 7,
        ACTIVITY_STOPPING = 8,
        ACTIVITY_PAUSING = 9,
        LOAD_URI = 10,
        dummy_java_enum_list_end
    };

    enum {
        IME_COMPOSITION_END = 0,
        IME_COMPOSITION_BEGIN = 1,
        IME_SET_TEXT = 2,
        IME_GET_TEXT = 3,
        IME_DELETE_TEXT = 4,
        IME_SET_SELECTION = 5,
        IME_GET_SELECTION = 6,
        IME_ADD_RANGE = 7
    };
};

class nsJNIString : public nsString
{
public:
    nsJNIString(jstring jstr);
};

}

#endif
