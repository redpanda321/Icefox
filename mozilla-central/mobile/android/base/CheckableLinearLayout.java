package org.mozilla.gecko;

import android.content.Context;
import android.util.AttributeSet;
import android.widget.CheckBox;
import android.widget.Checkable;
import android.widget.LinearLayout;


public class CheckableLinearLayout extends LinearLayout implements Checkable {

    private CheckBox mCheckBox;

    public CheckableLinearLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public boolean isChecked() {
        return mCheckBox != null ? mCheckBox.isChecked() : false;
    }

    public void setChecked(boolean isChecked) {
        if (mCheckBox != null)
            mCheckBox.setChecked(isChecked);
    }

    public void toggle() {
        if (mCheckBox != null)
            mCheckBox.toggle();
    }

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        mCheckBox = (CheckBox) findViewById(R.id.checkbox);
        mCheckBox.setClickable(false);
    }
}


