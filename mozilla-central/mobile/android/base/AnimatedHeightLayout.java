/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.animation.Animation;
import android.view.animation.DecelerateInterpolator;
import android.widget.RelativeLayout;

public class AnimatedHeightLayout extends RelativeLayout {
    private static final String LOGTAG = "GeckoAnimatedHeightLayout";
    private static final int ANIMATION_DURATION = 100;
    private boolean mAnimating = false;

    public AnimatedHeightLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int oldHeight = getMeasuredHeight();
        super.onMeasure(widthMeasureSpec, heightMeasureSpec);
        int newHeight = getMeasuredHeight();

        if (!mAnimating && oldHeight != 0 && oldHeight != newHeight) {
            mAnimating = true;
            setMeasuredDimension(getMeasuredWidth(), oldHeight);

            // Animate the difference of suggestion row height
            Animation anim = new HeightChangeAnimation(this, oldHeight, newHeight);
            anim.setDuration(ANIMATION_DURATION);
            anim.setInterpolator(new DecelerateInterpolator());
            anim.setAnimationListener(new Animation.AnimationListener() {
                public void onAnimationStart(Animation animation) {}
                public void onAnimationRepeat(Animation animation) {}
                public void onAnimationEnd(Animation animation) {
                    post(new Runnable() {
                        public void run() {
                            finishAnimation();
                        }
                    });
                }
            });
            startAnimation(anim);
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        finishAnimation();
    }

    private void finishAnimation() {
        if (mAnimating) {
            getLayoutParams().height = LayoutParams.WRAP_CONTENT;
            mAnimating = false;
        }
    }
}
