/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PorterDuff.Mode;
import android.graphics.Shader;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.StateListDrawable;
import android.util.AttributeSet;

public class ForwardButton extends ShapedButton { 
    private Path mBorderPath;
    private Paint mBorderPaint;
    private Paint mBorderPrivatePaint;

    public ForwardButton(Context context, AttributeSet attrs) {
        super(context, attrs);

        // Paint to draw the border.
        mBorderPaint = new Paint();
        mBorderPaint.setAntiAlias(true);
        mBorderPaint.setColor(0xFF000000);
        mBorderPaint.setStyle(Paint.Style.STROKE);

        mBorderPrivatePaint = new Paint(mBorderPaint);

        mBorderPath = new Path();
    }

    @Override
    protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        super.onSizeChanged(width, height, oldWidth, oldHeight);

        float borderWidth = getContext().getResources().getDimension(R.dimen.nav_button_border_width);
        mBorderPaint.setStrokeWidth(borderWidth);
        mBorderPrivatePaint.setStrokeWidth(borderWidth);

        mBorderPath.reset();
        mBorderPath.moveTo(width - borderWidth, 0);
        mBorderPath.lineTo(width - borderWidth, height);

        mBorderPaint.setShader(new LinearGradient(0, 0, 
                                                  0, height, 
                                                  0xFF898D8F, 0xFFFEFEFE,
                                                  Shader.TileMode.CLAMP));

        mBorderPrivatePaint.setShader(new LinearGradient(0, 0, 
                                                         0, height, 
                                                         0xCC06090D, 0xFF616569,
                                                         Shader.TileMode.CLAMP));
    }

    @Override
    public void draw(Canvas canvas) {
        super.draw(canvas);

        // Draw the border on top.
        canvas.drawPath(mBorderPath, isPrivateMode() ? mBorderPrivatePaint : mBorderPaint);
    }

    // The drawable is constructed as per @drawable/address_bar_nav_button.
    @Override
    public void onLightweightThemeChanged() {
        Drawable drawable = mActivity.getLightweightTheme().getDrawable(this);
        if (drawable == null)
            return;

        Resources resources = getContext().getResources();
        StateListDrawable stateList = new StateListDrawable();

        stateList.addState(new int[] { android.R.attr.state_pressed }, resources.getDrawable(R.drawable.highlight));
        stateList.addState(new int[] { R.attr.state_private }, resources.getDrawable(R.drawable.address_bar_bg_private));
        stateList.addState(new int[] {}, drawable);

        setBackgroundDrawable(stateList);
    }

    @Override
    public void onLightweightThemeReset() {
        setBackgroundResource(R.drawable.address_bar_nav_button);
    }
}
