/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import org.mozilla.gecko.AwesomeBar.ContextMenuSubject;
import org.mozilla.gecko.db.BrowserDB.URLColumns;

import android.content.ContentResolver;
import android.content.Context;
import android.content.res.Resources;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.text.TextUtils;
import android.view.ContextMenu;
import android.view.ContextMenu.ContextMenuInfo;
import android.view.LayoutInflater;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.widget.ImageView;
import android.widget.TabHost.TabContentFactory;
import android.widget.TextView;

abstract public class AwesomeBarTab {
    abstract public String getTag();
    abstract public int getTitleStringId();
    abstract public void destroy();
    abstract public boolean   onBackPressed();
    abstract public ContextMenuSubject getSubject(ContextMenu menu, View view, ContextMenuInfo menuInfo);
    abstract public View getView();

    protected View mView = null;
    protected View.OnTouchListener mListListener;
    private AwesomeBarTabs.OnUrlOpenListener mListener;
    private LayoutInflater mInflater = null;
    private ContentResolver mContentResolver = null;
    private Resources mResources;
    // FIXME: This value should probably come from a prefs key
    public static final int MAX_RESULTS = 100;
    protected Context mContext = null;
    private static int sFaviconSmallSize = -1;
    private static int sFaviconLargeSize = -1;

    public AwesomeBarTab(Context context) {
        mContext = context;
        if (sFaviconSmallSize < 0) {
            sFaviconSmallSize = Math.round(mContext.getResources().getDimension(R.dimen.awesomebar_row_favicon_size_small));
        }
        if (sFaviconLargeSize < 0) {
            sFaviconLargeSize = Math.round(mContext.getResources().getDimension(R.dimen.awesomebar_row_favicon_size_large));
        }
    }

    public void setListTouchListener(View.OnTouchListener listener) {
        mListListener = listener;
        if (mView != null)
            mView.setOnTouchListener(mListListener);
    }

    protected class AwesomeEntryViewHolder {
        public TextView titleView;
        public TextView urlView;
        public ImageView faviconView;
        public ImageView bookmarkIconView;
    }

    protected LayoutInflater getInflater() {
        if (mInflater == null) {
            mInflater = (LayoutInflater) mContext.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        }
        return mInflater;
    }

    protected AwesomeBarTabs.OnUrlOpenListener getUrlListener() {
        return mListener;
    }

    protected void setUrlListener(AwesomeBarTabs.OnUrlOpenListener listener) {
        mListener = listener;
    }

    protected ContentResolver getContentResolver() {
        if (mContentResolver == null) {
            mContentResolver = mContext.getContentResolver();
        }
        return mContentResolver;
    }

    protected Resources getResources() {
        if (mResources == null) {
            mResources = mContext.getResources();
        }
        return mResources;
    }

    protected void updateFavicon(ImageView faviconView, Cursor cursor) {
        byte[] b = cursor.getBlob(cursor.getColumnIndexOrThrow(URLColumns.FAVICON));
        if (b == null) {
            faviconView.setImageDrawable(null);
        } else {
            Bitmap bitmap = BitmapFactory.decodeByteArray(b, 0, b.length);
            updateFavicon(faviconView, bitmap);
        }
    }

    protected void updateFavicon(ImageView faviconView, Bitmap bitmap) {
        if (bitmap == null) {
            faviconView.setImageDrawable(null);
        } else if (bitmap.getWidth() > 16 || bitmap.getHeight() > 16) {
            // If the icon is larger than 16px, scale it to sFaviconLargeSize and hide the background
            bitmap = Bitmap.createScaledBitmap(bitmap, sFaviconLargeSize, sFaviconLargeSize, false);
            faviconView.setImageBitmap(bitmap);
            faviconView.setBackgroundResource(0);
        } else {
            // If the icon is 16px or smaller, don't scale it up to full size
            bitmap = Bitmap.createScaledBitmap(bitmap, sFaviconSmallSize, sFaviconSmallSize, false);
            faviconView.setImageBitmap(bitmap);
            faviconView.setBackgroundResource(R.drawable.awesomebar_row_favicon_bg);
        }
    }

    protected void updateTitle(TextView titleView, Cursor cursor) {
        int titleIndex = cursor.getColumnIndexOrThrow(URLColumns.TITLE);
        String title = cursor.getString(titleIndex);

        // Use the URL instead of an empty title for consistency with the normal URL
        // bar view - this is the equivalent of getDisplayTitle() in Tab.java
        if (TextUtils.isEmpty(title)) {
            int urlIndex = cursor.getColumnIndexOrThrow(URLColumns.URL);
            title = cursor.getString(urlIndex);
        }

        titleView.setText(title);
    }

    protected void updateUrl(TextView urlView, Cursor cursor) {
        int urlIndex = cursor.getColumnIndexOrThrow(URLColumns.URL);
        String url = cursor.getString(urlIndex);

        urlView.setText(url);
    }

    protected boolean hideSoftInput(View view) {
        InputMethodManager imm =
                (InputMethodManager) mContext.getSystemService(Context.INPUT_METHOD_SERVICE);

        return imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
    }
}
