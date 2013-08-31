/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import org.mozilla.gecko.db.BrowserDB;
import org.mozilla.gecko.gfx.Layer;
import org.mozilla.gecko.util.GeckoAsyncTask;

import org.json.JSONException;
import org.json.JSONObject;

import android.content.ContentResolver;
import android.database.ContentObserver;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Build;
import android.text.TextUtils;
import android.util.Log;
import android.view.View;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class Tab {
    private static final String LOGTAG = "GeckoTab";

    private static Pattern sColorPattern;
    private final int mId;
    private long mLastUsed;
    private String mUrl;
    private String mTitle;
    private Bitmap mFavicon;
    private String mFaviconUrl;
    private int mFaviconSize;
    private JSONObject mIdentityData;
    private boolean mReaderEnabled;
    private BitmapDrawable mThumbnail;
    private int mHistoryIndex;
    private int mHistorySize;
    private int mParentId;
    private boolean mExternal;
    private boolean mBookmark;
    private boolean mReadingListItem;
    private long mFaviconLoadId;
    private String mDocumentURI;
    private String mContentType;
    private boolean mHasTouchListeners;
    private ZoomConstraints mZoomConstraints;
    private ArrayList<View> mPluginViews;
    private HashMap<Object, Layer> mPluginLayers;
    private ContentResolver mContentResolver;
    private ContentObserver mContentObserver;
    private int mBackgroundColor = Color.WHITE;
    private int mState;
    private Bitmap mThumbnailBitmap;
    private boolean mDesktopMode;
    private boolean mEnteringReaderMode;

    public static final int STATE_DELAYED = 0;
    public static final int STATE_LOADING = 1;
    public static final int STATE_SUCCESS = 2;
    public static final int STATE_ERROR = 3;

    public Tab(int id, String url, boolean external, int parentId, String title) {
        mId = id;
        mLastUsed = 0;
        mUrl = url;
        mExternal = external;
        mParentId = parentId;
        mTitle = title == null ? "" : title;
        mFavicon = null;
        mFaviconUrl = null;
        mFaviconSize = 0;
        mIdentityData = null;
        mReaderEnabled = false;
        mEnteringReaderMode = false;
        mThumbnail = null;
        mHistoryIndex = -1;
        mHistorySize = 0;
        mBookmark = false;
        mReadingListItem = false;
        mFaviconLoadId = 0;
        mDocumentURI = "";
        mContentType = "";
        mZoomConstraints = new ZoomConstraints(false);
        mPluginViews = new ArrayList<View>();
        mPluginLayers = new HashMap<Object, Layer>();
        mState = GeckoApp.shouldShowProgress(url) ? STATE_SUCCESS : STATE_LOADING;
        mContentResolver = Tabs.getInstance().getContentResolver();
        mContentObserver = new ContentObserver(null) {
            public void onChange(boolean selfChange) {
                updateBookmark();
            }
        };
        BrowserDB.registerBookmarkObserver(mContentResolver, mContentObserver);
    }

    public void onDestroy() {
        BrowserDB.unregisterContentObserver(mContentResolver, mContentObserver);
        Tabs.getInstance().notifyListeners(this, Tabs.TabEvents.CLOSED);
    }

    public int getId() {
        return mId;
    }

    public synchronized void onChange() {
        mLastUsed = System.currentTimeMillis();
    }

    public synchronized long getLastUsed() {
        return mLastUsed;
    }

    public int getParentId() {
        return mParentId;
    }

    // may be null if user-entered query hasn't yet been resolved to a URI
    public synchronized String getURL() {
        return mUrl;
    }

    // mTitle should never be null, but it may be an empty string
    public synchronized String getTitle() {
        return mTitle;
    }

    public String getDisplayTitle() {
        if (mTitle != null && mTitle.length() > 0) {
            return mTitle;
        }

        return mUrl;
    }

    public Bitmap getFavicon() {
        return mFavicon;
    }

    public Drawable getThumbnail() {
        return mThumbnail;
    }

    public Bitmap getThumbnailBitmap(int width, int height) {
        if (mThumbnailBitmap != null) {
            // Bug 787318 - Honeycomb has a bug with bitmap caching, we can't
            // reuse the bitmap there.
            boolean honeycomb = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB
                              && Build.VERSION.SDK_INT <= Build.VERSION_CODES.HONEYCOMB_MR2);
            boolean sizeChange = mThumbnailBitmap.getWidth() != width
                              || mThumbnailBitmap.getHeight() != height;
            if (honeycomb || sizeChange) {
                mThumbnailBitmap.recycle();
                mThumbnailBitmap = null;
            }
        }

        if (mThumbnailBitmap == null) {
            mThumbnailBitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565);
        }

        return mThumbnailBitmap;
    }

    public void updateThumbnail(final Bitmap b) {
        GeckoAppShell.getHandler().post(new Runnable() {
            public void run() {
                if (b != null) {
                    try {
                        mThumbnail = new BitmapDrawable(b);
                        if (mState == Tab.STATE_SUCCESS)
                            saveThumbnailToDB();
                    } catch (OutOfMemoryError oom) {
                        Log.w(LOGTAG, "Unable to create/scale bitmap.", oom);
                        mThumbnail = null;
                    }
                } else {
                    mThumbnail = null;
                }

                Tabs.getInstance().notifyListeners(Tab.this, Tabs.TabEvents.THUMBNAIL);
            }
        });
    }

    public synchronized String getFaviconURL() {
        return mFaviconUrl;
    }

    public String getSecurityMode() {
        try {
            return mIdentityData.getString("mode");
        } catch (Exception e) {
            // If mIdentityData is null, or we get a JSONException
            return SiteIdentityPopup.UNKNOWN;
        }
    }

    public JSONObject getIdentityData() {
        return mIdentityData;
    }

    public boolean getReaderEnabled() {
        return mReaderEnabled;
    }

    public boolean isBookmark() {
        return mBookmark;
    }

    public boolean isReadingListItem() {
        return mReadingListItem;
    }

    public boolean isExternal() {
        return mExternal;
    }

    public synchronized void updateURL(String url) {
        if (url != null && url.length() > 0) {
            mUrl = url;
            updateBookmark();
        }
    }

    public void setDocumentURI(String documentURI) {
        mDocumentURI = documentURI;
    }

    public String getDocumentURI() {
        return mDocumentURI;
    }

    public void setContentType(String contentType) {
        mContentType = (contentType == null) ? "" : contentType;
    }

    public String getContentType() {
        return mContentType;
    }

    public synchronized void updateTitle(String title) {
        // Keep the title unchanged while entering reader mode
        if (mEnteringReaderMode)
            return;

        mTitle = (title == null ? "" : title);

        if (mUrl != null)
            updateHistory(mUrl, mTitle);

        Tabs.getInstance().notifyListeners(this, Tabs.TabEvents.TITLE);
    }

    protected void addHistory(final String uri) {
        GeckoAppShell.getHandler().post(new Runnable() {
            public void run() {
                GlobalHistory.getInstance().add(uri);
            }
        });
    }

    protected void updateHistory(final String uri, final String title) {
        GeckoAppShell.getHandler().post(new Runnable() {
            public void run() {
                GlobalHistory.getInstance().update(uri, title);
            }
        });
    }

    public void setState(int state) {
        mState = state;

        if (mState != Tab.STATE_LOADING)
            mEnteringReaderMode = false;
    }

    public int getState() {
        return mState;
    }

    public void setZoomConstraints(ZoomConstraints constraints) {
        mZoomConstraints = constraints;
    }

    public ZoomConstraints getZoomConstraints() {
        return mZoomConstraints;
    }

    public void setHasTouchListeners(boolean aValue) {
        mHasTouchListeners = aValue;
    }

    public boolean getHasTouchListeners() {
        return mHasTouchListeners;
    }

    public void setFaviconLoadId(long faviconLoadId) {
        mFaviconLoadId = faviconLoadId;
    }

    public long getFaviconLoadId() {
        return mFaviconLoadId;
    }

    public void updateFavicon(Bitmap favicon) {
        mFavicon = favicon;
    }

    public synchronized void updateFaviconURL(String faviconUrl, int size) {
        // If we already have an "any" sized icon, don't update the icon.
        if (mFaviconSize == -1)
            return;

        // Only update the favicon if it's bigger than the current favicon.
        // We use -1 to represent icons with sizes="any".
        if (size == -1 || size >= mFaviconSize) {
            mFaviconUrl = faviconUrl;
            mFaviconSize = size;
        }
    }

    public synchronized void clearFavicon() {
        // Keep the favicon unchanged while entering reader mode
        if (mEnteringReaderMode)
            return;

        mFavicon = null;
        mFaviconUrl = null;
        mFaviconSize = 0;
    }

    public void updateIdentityData(JSONObject identityData) {
        mIdentityData = identityData;
    }

    public void setReaderEnabled(boolean readerEnabled) {
        mReaderEnabled = readerEnabled;
        Tabs.getInstance().notifyListeners(this, Tabs.TabEvents.MENU_UPDATED);
    }

    private void updateBookmark() {
        GeckoAppShell.getHandler().post(new Runnable() {
            public void run() {
                final String url = getURL();
                if (url == null)
                    return;

                if (url.equals(getURL())) {
                    mBookmark = BrowserDB.isBookmark(mContentResolver, url);
                    mReadingListItem = BrowserDB.isReadingListItem(mContentResolver, url);
                }

                Tabs.getInstance().notifyListeners(Tab.this, Tabs.TabEvents.MENU_UPDATED);
            }
        });
    }

    public void addBookmark() {
        GeckoAppShell.getHandler().post(new Runnable() {
            public void run() {
                String url = getURL();
                if (url == null)
                    return;

                BrowserDB.addBookmark(mContentResolver, mTitle, url);
            }
        });
    }

    public void removeBookmark() {
        GeckoAppShell.getHandler().post(new Runnable() {
            public void run() {
                String url = getURL();
                if (url == null)
                    return;

                BrowserDB.removeBookmarksWithURL(mContentResolver, url);
            }
        });
    }

    public void addToReadingList() {
        if (!mReaderEnabled)
            return;

        GeckoEvent e = GeckoEvent.createBroadcastEvent("Reader:Add", String.valueOf(getId()));
        GeckoAppShell.sendEventToGecko(e);
    }

    public void readerMode() {
        if (!mReaderEnabled)
            return;

        mEnteringReaderMode = true;
        Tabs.getInstance().loadUrl(ReaderModeUtils.getAboutReaderForUrl(getURL(), mId, mReadingListItem));
    }

    public boolean isEnteringReaderMode() {
        return mEnteringReaderMode;
    }

    public void doReload() {
        GeckoEvent e = GeckoEvent.createBroadcastEvent("Session:Reload", "");
        GeckoAppShell.sendEventToGecko(e);
    }

    // Our version of nsSHistory::GetCanGoBack
    public boolean canDoBack() {
        return mHistoryIndex > 0;
    }

    public boolean doBack() {
        if (!canDoBack())
            return false;

        GeckoEvent e = GeckoEvent.createBroadcastEvent("Session:Back", "");
        GeckoAppShell.sendEventToGecko(e);
        return true;
    }

    public void doStop() {
        GeckoEvent e = GeckoEvent.createBroadcastEvent("Session:Stop", "");
        GeckoAppShell.sendEventToGecko(e);
    }

    // Our version of nsSHistory::GetCanGoForward
    public boolean canDoForward() {
        return mHistoryIndex < mHistorySize - 1;
    }

    public boolean doForward() {
        if (!canDoForward())
            return false;

        GeckoEvent e = GeckoEvent.createBroadcastEvent("Session:Forward", "");
        GeckoAppShell.sendEventToGecko(e);
        return true;
    }

    void handleSessionHistoryMessage(String event, JSONObject message) throws JSONException {
        if (event.equals("New")) {
            final String url = message.getString("url");
            mHistoryIndex++;
            mHistorySize = mHistoryIndex + 1;
        } else if (event.equals("Back")) {
            if (!canDoBack()) {
                Log.w(LOGTAG, "Received unexpected back notification");
                return;
            }
            mHistoryIndex--;
        } else if (event.equals("Forward")) {
            if (!canDoForward()) {
                Log.w(LOGTAG, "Received unexpected forward notification");
                return;
            }
            mHistoryIndex++;
        } else if (event.equals("Goto")) {
            int index = message.getInt("index");
            if (index < 0 || index >= mHistorySize) {
                Log.w(LOGTAG, "Received unexpected history-goto notification");
                return;
            }
            mHistoryIndex = index;
        } else if (event.equals("Purge")) {
            int numEntries = message.getInt("numEntries");
            if (numEntries > mHistorySize) {
                Log.w(LOGTAG, "Received unexpectedly large number of history entries to purge");
                mHistoryIndex = -1;
                mHistorySize = 0;
                return;
            }

            mHistorySize -= numEntries;
            mHistoryIndex -= numEntries;

            // If we weren't at the last history entry, mHistoryIndex may have become too small
            if (mHistoryIndex < -1)
                 mHistoryIndex = -1;
        }
    }

    void handleLocationChange(JSONObject message) throws JSONException {
        final String uri = message.getString("uri");
        mEnteringReaderMode = ReaderModeUtils.isEnteringReaderMode(mUrl, uri);
        updateURL(uri);

        setDocumentURI(message.getString("documentURI"));
        if (message.getBoolean("sameDocument")) {
            // We can get a location change event for the same document with an anchor tag
            return;
        }

        setContentType(message.getString("contentType"));
        clearFavicon();
        updateTitle(null);
        updateIdentityData(null);
        setReaderEnabled(false);
        setZoomConstraints(new ZoomConstraints(true));
        setHasTouchListeners(false);
        setBackgroundColor(Color.WHITE);

        Tabs.getInstance().notifyListeners(this, Tabs.TabEvents.LOCATION_CHANGE, uri);
    }

    protected void saveThumbnailToDB() {
        try {
            String url = getURL();
            if (url == null)
                return;

            BrowserDB.updateThumbnailForUrl(mContentResolver, url, mThumbnail);
        } catch (Exception e) {
            // ignore
        }
    }

    public void addPluginView(View view) {
        mPluginViews.add(view);
    }

    public void removePluginView(View view) {
        mPluginViews.remove(view);
    }

    public View[] getPluginViews() {
        return mPluginViews.toArray(new View[mPluginViews.size()]);
    }

    public void addPluginLayer(Object surfaceOrView, Layer layer) {
        synchronized(mPluginLayers) {
            mPluginLayers.put(surfaceOrView, layer);
        }
    }

    public Layer getPluginLayer(Object surfaceOrView) {
        synchronized(mPluginLayers) {
            return mPluginLayers.get(surfaceOrView);
        }
    }

    public Collection<Layer> getPluginLayers() {
        synchronized(mPluginLayers) {
            return new ArrayList<Layer>(mPluginLayers.values());
        }
    }

    public Layer removePluginLayer(Object surfaceOrView) {
        synchronized(mPluginLayers) {
            return mPluginLayers.remove(surfaceOrView);
        }
    }

    public int getBackgroundColor() {
        return mBackgroundColor;
    }

    /** Sets a new color for the background. */
    public void setBackgroundColor(int color) {
        mBackgroundColor = color;
    }

    /** Parses and sets a new color for the background. */
    public void setBackgroundColor(String newColor) {
        setBackgroundColor(parseColorFromGecko(newColor));
    }

    // Parses a color from an RGB triple of the form "rgb([0-9]+, [0-9]+, [0-9]+)". If the color
    // cannot be parsed, returns white.
    private static int parseColorFromGecko(String string) {
        if (sColorPattern == null) {
            sColorPattern = Pattern.compile("rgb\\((\\d+),\\s*(\\d+),\\s*(\\d+)\\)");
        }

        Matcher matcher = sColorPattern.matcher(string);
        if (!matcher.matches()) {
            return Color.WHITE;
        }

        int r = Integer.parseInt(matcher.group(1));
        int g = Integer.parseInt(matcher.group(2));
        int b = Integer.parseInt(matcher.group(3));
        return Color.rgb(r, g, b);
    }

    public void setDesktopMode(boolean enabled) {
        mDesktopMode = enabled;
    }

    public boolean getDesktopMode() {
        return mDesktopMode;
    }

    public boolean isPrivate() {
        return false;
    }
}
