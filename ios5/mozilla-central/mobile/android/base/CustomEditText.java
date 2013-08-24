/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.content.Context;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.View;
import android.widget.EditText;

public class CustomEditText extends EditText {
    public CustomEditText(Context context, AttributeSet attrs) {
        super(context, attrs);
        mOnKeyPreImeListener = null;
    }

    OnKeyPreImeListener mOnKeyPreImeListener;

    public interface OnKeyPreImeListener {
        public boolean onKeyPreIme(View v, int keyCode, KeyEvent event);
    }

    public void setOnKeyPreImeListener(OnKeyPreImeListener listener) {
        mOnKeyPreImeListener = listener;
    }

    @Override
    public boolean onKeyPreIme(int keyCode, KeyEvent event) {
        if (mOnKeyPreImeListener != null)
            return mOnKeyPreImeListener.onKeyPreIme(this, keyCode, event);

        return false;
    }

    public void setOnWindowFocusChangeListener(OnWindowFocusChangeListener listener) {
        mOnWindowFocusChangeListener = listener;
    }

    OnWindowFocusChangeListener mOnWindowFocusChangeListener;

    public interface OnWindowFocusChangeListener {
        public void onWindowFocusChanged(boolean hasFocus);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (mOnWindowFocusChangeListener != null)
            mOnWindowFocusChangeListener.onWindowFocusChanged(hasFocus);
    }
}
