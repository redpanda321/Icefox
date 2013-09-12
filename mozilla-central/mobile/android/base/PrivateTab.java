/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

public class PrivateTab extends Tab {
    public PrivateTab(int id, String url, boolean external, int parentId, String title) {
        super(id, url, external, parentId, title);
    }

    @Override
    protected void addHistory(final String uri) {}

    @Override
    protected void updateHistory(final String uri, final String title) {}

    @Override
    protected void saveThumbnailToDB() {}

    @Override
    public boolean isPrivate() {
        return true;
    }
}
