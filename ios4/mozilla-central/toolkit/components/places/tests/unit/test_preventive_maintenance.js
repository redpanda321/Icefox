/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
/* ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Bug 431558 code.
 *
 * The Initial Developer of the Original Code is Mozilla Corp.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Marco Bonardo <mak77bonardo.net> (Original Author)
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

 /**
  * Test preventive maintenance
  * For every maintenance query create an uncoherent db and check that we take
  * correct fix steps, without polluting valid data.
  */

// Include PlacesDBUtils module
Components.utils.import("resource://gre/modules/PlacesDBUtils.jsm");

const FINISHED_MAINTANANCE_NOTIFICATION_TOPIC = "places-maintenance-finished";

const PLACES_STRING_BUNDLE_URI = "chrome://places/locale/places.properties";

// Get services and database connection
let os = Cc["@mozilla.org/observer-service;1"].
         getService(Ci.nsIObserverService);
let hs = Cc["@mozilla.org/browser/nav-history-service;1"].
         getService(Ci.nsINavHistoryService);
let bh = hs.QueryInterface(Ci.nsIBrowserHistory);
let bs = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
         getService(Ci.nsINavBookmarksService);
let ts = Cc["@mozilla.org/browser/tagging-service;1"].
         getService(Ci.nsITaggingService);
let as = Cc["@mozilla.org/browser/annotation-service;1"].
         getService(Ci.nsIAnnotationService);
let fs = Cc["@mozilla.org/browser/favicon-service;1"].
         getService(Ci.nsIFaviconService);
let bundle = Cc["@mozilla.org/intl/stringbundle;1"].
             getService(Ci.nsIStringBundleService).
             createBundle(PLACES_STRING_BUNDLE_URI);

let mDBConn = hs.QueryInterface(Ci.nsPIPlacesDatabase).DBConnection;

//------------------------------------------------------------------------------
// Helpers

let defaultBookmarksMaxId = 0;
function cleanDatabase() {
  mDBConn.executeSimpleSQL("DELETE FROM moz_places");
  mDBConn.executeSimpleSQL("DELETE FROM moz_historyvisits");
  mDBConn.executeSimpleSQL("DELETE FROM moz_anno_attributes");
  mDBConn.executeSimpleSQL("DELETE FROM moz_annos");
  mDBConn.executeSimpleSQL("DELETE FROM moz_items_annos");
  mDBConn.executeSimpleSQL("DELETE FROM moz_inputhistory");
  mDBConn.executeSimpleSQL("DELETE FROM moz_keywords");
  mDBConn.executeSimpleSQL("DELETE FROM moz_favicons");
  mDBConn.executeSimpleSQL("DELETE FROM moz_bookmarks WHERE id > " + defaultBookmarksMaxId);
}

function addPlace(aUrl, aFavicon) {
  let stmt = mDBConn.createStatement(
    "INSERT INTO moz_places (url, favicon_id) VALUES (:url, :favicon)");
  stmt.params["url"] = aUrl || "http://www.mozilla.org";
  stmt.params["favicon"] = aFavicon || null;
  stmt.execute();
  stmt.finalize();
  return mDBConn.lastInsertRowID;
}

function addBookmark(aPlaceId, aType, aParent, aKeywordId, aFolderType) {
  let stmt = mDBConn.createStatement(
    "INSERT INTO moz_bookmarks (fk, type, parent, keyword_id, folder_type) " +
    "VALUES (:place_id, :type, :parent, :keyword_id, :folder_type)");
  stmt.params["place_id"] = aPlaceId || null;
  stmt.params["type"] = aType || bs.TYPE_BOOKMARK;
  stmt.params["parent"] = aParent || bs.unfiledBookmarksFolder;
  stmt.params["keyword_id"] = aKeywordId || null;
  stmt.params["folder_type"] = aFolderType || null;
  stmt.execute();
  stmt.finalize();
  return mDBConn.lastInsertRowID;
}

//------------------------------------------------------------------------------
// Tests

let tests = [];
let current_test = null;

//------------------------------------------------------------------------------

tests.push({
  name: "A.1",
  desc: "Remove unused attributes",

  _usedPageAttribute: "usedPage",
  _usedItemAttribute: "usedItem",
  _unusedAttribute: "unused",
  _placeId: null,
  _bookmarkId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // add a bookmark
    this._bookmarkId = addBookmark(this._placeId);
    // Add a used attribute and an unused one.
    let stmt = mDBConn.createStatement("INSERT INTO moz_anno_attributes (name) VALUES (:anno)");
    stmt.params['anno'] = this._usedPageAttribute;
    stmt.execute();
    stmt.reset();
    stmt.params['anno'] = this._usedItemAttribute;
    stmt.execute();
    stmt.reset();
    stmt.params['anno'] = this._unusedAttribute;
    stmt.execute();
    stmt.finalize();

    stmt = mDBConn.createStatement("INSERT INTO moz_annos (place_id, anno_attribute_id) VALUES(:place_id, (SELECT id FROM moz_anno_attributes WHERE name = :anno))");
    stmt.params['place_id'] = this._placeId;
    stmt.params['anno'] = this._usedPageAttribute;
    stmt.execute();
    stmt.finalize();
    stmt = mDBConn.createStatement("INSERT INTO moz_items_annos (item_id, anno_attribute_id) VALUES(:item_id, (SELECT id FROM moz_anno_attributes WHERE name = :anno))");
    stmt.params['item_id'] = this._bookmarkId;
    stmt.params['anno'] = this._usedItemAttribute;
    stmt.execute();
    stmt.finalize();    
  },

  check: function() {
    // Check that used attributes are still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_anno_attributes WHERE name = :anno");
    stmt.params['anno'] = this._usedPageAttribute;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params['anno'] = this._usedItemAttribute;
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check that unused attribute has been removed
    stmt.params['anno'] = this._unusedAttribute;
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "B.1",
  desc: "Remove annotations with an invalid attribute",

  _usedPageAttribute: "usedPage",
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Add a used attribute.
    let stmt = mDBConn.createStatement("INSERT INTO moz_anno_attributes (name) VALUES (:anno)");
    stmt.params['anno'] = this._usedPageAttribute;
    stmt.execute();
    stmt.finalize();
    stmt = mDBConn.createStatement("INSERT INTO moz_annos (place_id, anno_attribute_id) VALUES(:place_id, (SELECT id FROM moz_anno_attributes WHERE name = :anno))");
    stmt.params['place_id'] = this._placeId;
    stmt.params['anno'] = this._usedPageAttribute;
    stmt.execute();
    stmt.finalize();
    // Add an annotation with a nonexistent attribute
    stmt = mDBConn.createStatement("INSERT INTO moz_annos (place_id, anno_attribute_id) VALUES(:place_id, 1337)");
    stmt.params['place_id'] = this._placeId;
    stmt.execute();
    stmt.finalize();
  },

  check: function() {
    // Check that used attribute is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_anno_attributes WHERE name = :anno");
    stmt.params['anno'] = this._usedPageAttribute;
    do_check_true(stmt.executeStep());
    stmt.finalize();
    // check that annotation with valid attribute is still there
    stmt = mDBConn.createStatement("SELECT id FROM moz_annos WHERE anno_attribute_id = (SELECT id FROM moz_anno_attributes WHERE name = :anno)");
    stmt.params['anno'] = this._usedPageAttribute;
    do_check_true(stmt.executeStep());
    stmt.finalize();
    // Check that annotation with bogus attribute has been removed
    stmt = mDBConn.createStatement("SELECT id FROM moz_annos WHERE anno_attribute_id = 1337");
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "B.2",
  desc: "Remove orphan page annotations",

  _usedPageAttribute: "usedPage",
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Add a used attribute.
    let stmt = mDBConn.createStatement("INSERT INTO moz_anno_attributes (name) VALUES (:anno)");
    stmt.params['anno'] = this._usedPageAttribute;
    stmt.execute();
    stmt.finalize();
    stmt = mDBConn.createStatement("INSERT INTO moz_annos (place_id, anno_attribute_id) VALUES(:place_id, (SELECT id FROM moz_anno_attributes WHERE name = :anno))");
    stmt.params['place_id'] = this._placeId;
    stmt.params['anno'] = this._usedPageAttribute;
    stmt.execute();
    stmt.reset();
    // Add an annotation to a nonexistent page
    stmt.params['place_id'] = 1337;
    stmt.params['anno'] = this._usedPageAttribute;
    stmt.execute();
    stmt.finalize();
  },

  check: function() {
    // Check that used attribute is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_anno_attributes WHERE name = :anno");
    stmt.params['anno'] = this._usedPageAttribute;
    do_check_true(stmt.executeStep());
    stmt.finalize();
    // check that annotation with valid attribute is still there
    stmt = mDBConn.createStatement("SELECT id FROM moz_annos WHERE anno_attribute_id = (SELECT id FROM moz_anno_attributes WHERE name = :anno)");
    stmt.params['anno'] = this._usedPageAttribute;
    do_check_true(stmt.executeStep());
    stmt.finalize();
    // Check that an annotation to a nonexistent page has been removed
    stmt = mDBConn.createStatement("SELECT id FROM moz_annos WHERE place_id = 1337");
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------
tests.push({
  name: "C.1",
  desc: "fix missing Places root",

  setup: function() {
    // Sanity check: ensure that roots are intact.
    do_check_eq(bs.getFolderIdForItem(bs.placesRoot), 0);
    do_check_eq(bs.getFolderIdForItem(bs.bookmarksMenuFolder), bs.placesRoot);
    do_check_eq(bs.getFolderIdForItem(bs.tagsFolder), bs.placesRoot);
    do_check_eq(bs.getFolderIdForItem(bs.unfiledBookmarksFolder), bs.placesRoot);
    do_check_eq(bs.getFolderIdForItem(bs.toolbarFolder), bs.placesRoot);

    // Remove the root.
    mDBConn.executeSimpleSQL("DELETE FROM moz_bookmarks WHERE parent = 0");
    try {
      bs.getFolderIdForItem(bs.placesRoot);
      do_throw("Places root should not exist now!");
    } catch(e) {
      // Root has been removed so this call should throw.
    }
  },

  check: function() {
    // Ensure the roots have been correctly restored.
    do_check_eq(bs.getFolderIdForItem(bs.placesRoot), 0);
    do_check_eq(bs.getFolderIdForItem(bs.bookmarksMenuFolder), bs.placesRoot);
    do_check_eq(bs.getFolderIdForItem(bs.tagsFolder), bs.placesRoot);
    do_check_eq(bs.getFolderIdForItem(bs.unfiledBookmarksFolder), bs.placesRoot);
    do_check_eq(bs.getFolderIdForItem(bs.toolbarFolder), bs.placesRoot);
  }
});

//------------------------------------------------------------------------------
tests.push({
  name: "C.2",
  desc: "Fix roots titles",

  setup: function() {
    // Sanity check: ensure that roots titles are correct. We can use our check.
    this.check();
    // Change some roots' titles.
    bs.setItemTitle(bs.placesRoot, "bad title");
    do_check_eq(bs.getItemTitle(bs.placesRoot), "bad title");
    bs.setItemTitle(bs.unfiledBookmarksFolder, "bad title");
    do_check_eq(bs.getItemTitle(bs.unfiledBookmarksFolder), "bad title");
  },

  check: function() {
    // Ensure all roots titles are correct.
    do_check_eq(bs.getItemTitle(bs.placesRoot), "");
    do_check_eq(bs.getItemTitle(bs.bookmarksMenuFolder),
                bundle.GetStringFromName("BookmarksMenuFolderTitle"));
    do_check_eq(bs.getItemTitle(bs.tagsFolder),
                bundle.GetStringFromName("TagsFolderTitle"));
    do_check_eq(bs.getItemTitle(bs.unfiledBookmarksFolder),
                bundle.GetStringFromName("UnsortedBookmarksFolderTitle"));
    do_check_eq(bs.getItemTitle(bs.toolbarFolder),
                bundle.GetStringFromName("BookmarksToolbarFolderTitle"));
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "D.1",
  desc: "Remove items without a valid place",

  _validItemId: null,
  _invalidItemId: null,
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this.placeId = addPlace();
    // Insert a valid bookmark
    this._validItemId = addBookmark(this.placeId);
    // Insert a bookmark with an invalid place
    this._invalidItemId = addBookmark(1337);
  },

  check: function() {
    // Check that valid bookmark is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE id = :item_id");
    stmt.params["item_id"] = this._validItemId;
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check that invalid bookmark has been removed
    stmt.params["item_id"] = this._invalidItemId;
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "D.2",
  desc: "Remove items that are not uri bookmarks from tag containers",

  _tagId: null,
  _bookmarkId: null,
  _separatorId: null,
  _folderId: null,
  _dynamicContainerId: null,
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Create a tag
    this._tagId = addBookmark(null, bs.TYPE_FOLDER, bs.tagsFolder);
    // Insert a bookmark in the tag
    this._bookmarkId = addBookmark(this._placeId, bs.TYPE_BOOKMARK, this._tagId);
    // Insert a separator in the tag
    this._separatorId = addBookmark(null, bs.TYPE_SEPARATOR, this._tagId);
    // Insert a folder in the tag
    this._folderId = addBookmark(null, bs.TYPE_FOLDER, this._tagId);
    // Insert a dynamic container in the tag
    this._dynamicContainerId = addBookmark(null, bs.TYPE_DYNAMIC_CONTAINER, this._tagId);
  },

  check: function() {
    // Check that valid bookmark is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE type = :type AND parent = :parent");
    stmt.params["type"] = bs.TYPE_BOOKMARK;
    stmt.params["parent"] = this._tagId;
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check that separator is no more there
    stmt.params["type"] = bs.TYPE_SEPARATOR;
    stmt.params["parent"] = this._tagId;
    do_check_false(stmt.executeStep());
    stmt.reset();
    // Check that folder is no more there
    stmt.params["type"] = bs.TYPE_FOLDER;
    stmt.params["parent"] = this._tagId;
    do_check_false(stmt.executeStep());
    stmt.reset();
    // Check that dynamic container is no more there
    stmt.params["type"] = bs.TYPE_DYNAMIC_CONTAINER;
    stmt.params["parent"] = this._tagId;
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "D.3",
  desc: "Remove empty tags",

  _tagId: null,
  _bookmarkId: null,
  _emptyTagId: null,
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Create a tag
    this._tagId = addBookmark(null, bs.TYPE_FOLDER, bs.tagsFolder);
    // Insert a bookmark in the tag
    this._bookmarkId = addBookmark(this._placeId, bs.TYPE_BOOKMARK, this._tagId);
    // Create another tag (empty)
    this._emptyTagId = addBookmark(null, bs.TYPE_FOLDER, bs.tagsFolder);
  },

  check: function() {
    // Check that valid bookmark is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE id = :id AND type = :type AND parent = :parent");
    stmt.params["id"] = this._bookmarkId;
    stmt.params["type"] = bs.TYPE_BOOKMARK;
    stmt.params["parent"] = this._tagId;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params["id"] = this._tagId;
    stmt.params["type"] = bs.TYPE_FOLDER;
    stmt.params["parent"] = bs.tagsFolder;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params["id"] = this._emptyTagId;
    stmt.params["type"] = bs.TYPE_FOLDER;
    stmt.params["parent"] = bs.tagsFolder;
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "D.4",
  desc: "Move orphan items to unsorted folder",

  _orphanBookmarkId: null,
  _orphanSeparatorId: null,
  _orphanFolderId: null,
  _bookmarkId: null,
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Insert an orphan bookmark
    this._orphanBookmarkId = addBookmark(this._placeId, bs.TYPE_BOOKMARK, 8888);
    // Insert an orphan separator
    this._orphanSeparatorId = addBookmark(null, bs.TYPE_SEPARATOR, 8888);
    // Insert a orphan folder
    this._orphanFolderId = addBookmark(null, bs.TYPE_FOLDER, 8888);
    // Create a child of the last created folder
    this._bookmarkId = addBookmark(this._placeId, bs.TYPE_BOOKMARK, this._orphanFolderId);
  },

  check: function() {
    // Check that bookmarks are now children of a real folder (unsorted)
    let stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE id = :item_id AND parent = :parent");
    stmt.params["item_id"] = this._orphanBookmarkId;
    stmt.params["parent"] = bs.unfiledBookmarksFolder;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params["item_id"] = this._orphanSeparatorId;
    stmt.params["parent"] = bs.unfiledBookmarksFolder;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params["item_id"] = this._orphanFolderId;
    stmt.params["parent"] = bs.unfiledBookmarksFolder;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params["item_id"] = this._bookmarkId;
    stmt.params["parent"] = this._orphanFolderId;
    do_check_true(stmt.executeStep());
    stmt.finalize();    
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "D.5",
  desc: "Fix wrong keywords",

  _validKeywordItemId: null,
  _invalidKeywordItemId: null,
  _validKeywordId: 1,
  _invalidKeywordId: 8888,
  _placeId: null,

  setup: function() {
    // Insert a keyword
    let stmt = mDBConn.createStatement("INSERT INTO moz_keywords (id, keyword) VALUES(:id, :keyword)");
    stmt.params["id"] = this._validKeywordId;
    stmt.params["keyword"] = "used";
    stmt.execute();
    stmt.finalize();
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Add a bookmark using the keyword
    this._validKeywordItemId = addBookmark(this._placeId, bs.TYPE_BOOKMARK, bs.unfiledBookmarksFolder, this._validKeywordId);
    // Add a bookmark using a nonexistent keyword
    this._invalidKeywordItemId = addBookmark(this._placeId, bs.TYPE_BOOKMARK, bs.unfiledBookmarksFolder, this._invalidKeywordId);
  },

  check: function() {
    // Check that item with valid keyword is there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE id = :item_id AND keyword_id = :keyword");
    stmt.params["item_id"] = this._validKeywordItemId;
    stmt.params["keyword"] = this._validKeywordId;
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check that item with invalid keyword has been corrected
    stmt.params["item_id"] = this._invalidKeywordItemId;
    stmt.params["keyword"] = this._invalidKeywordId;
    do_check_false(stmt.executeStep());
    stmt.finalize();
    // Check that item with invalid keyword has not been removed
    stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE id = :item_id");
    stmt.params["item_id"] = this._invalidKeywordItemId;
    do_check_true(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "D.6",
  desc: "Fix wrong item types | bookmarks",

  _separatorId: null,
  _folderId: null,
  _dynamicContainerId: null,
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Add a separator with a fk
    this._separatorId = addBookmark(this._placeId, bs.TYPE_SEPARATOR);
    // Add a folder with a fk
    this._folderId = addBookmark(this._placeId, bs.TYPE_FOLDER);
    // Add a dynamic container with a fk
    this._dynamicContainerId = addBookmark(this._placeId, bs.TYPE_DYNAMIC_CONTAINER, null, null, "test");
  },

  check: function() {
    // Check that items with an fk have been converted to bookmarks
    let stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE id = :item_id AND type = :type");
    stmt.params["item_id"] = this._separatorId;
    stmt.params["type"] = bs.TYPE_BOOKMARK;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params["item_id"] = this._folderId;
    stmt.params["type"] = bs.TYPE_BOOKMARK;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params["item_id"] = this._dynamicContainerId;
    stmt.params["type"] = bs.TYPE_BOOKMARK;
    do_check_true(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "D.7",
  desc: "Fix wrong item types | bookmarks",

  _validBookmarkId: null,
  _invalidBookmarkId: null,
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Add a bookmark with a valid place id
    this._validBookmarkId = addBookmark(this._placeId, bs.TYPE_BOOKMARK);
    // Add a bookmark with a null place id
    this._invalidBookmarkId = addBookmark(null, bs.TYPE_BOOKMARK);
  },

  check: function() {
    // Check valid bookmark
    let stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE id = :item_id AND type = :type");
    stmt.params["item_id"] = this._validBookmarkId;
    stmt.params["type"] = bs.TYPE_BOOKMARK;
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check invalid bookmark has been converted to a folder
    stmt.params["item_id"] = this._invalidBookmarkId;
    stmt.params["type"] = bs.TYPE_FOLDER;
    do_check_true(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "D.8",
  desc: "Fix wrong item types | dynamic containers",

  _validDynamicContainerId: null,
  _invalidDynamicContainerId: null,

  setup: function() {
    // Add a valid dynamic container with a folder type
    this._validDynamicContainerId = addBookmark(null, bs.TYPE_DYNAMIC_CONTAINER, null, null, "test");
    // Add an invalid dynamic container without a folder type
    this._invalidDynamicContainerId = addBookmark(null, bs.TYPE_DYNAMIC_CONTAINER, null, null, null);    
  },

  check: function() {
    // Check valid dynamic container is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE id = :item_id AND type = :type");
    stmt.params["item_id"] = this._validDynamicContainerId;
    stmt.params["type"] = bs.TYPE_DYNAMIC_CONTAINER;
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check invalid dynamic container has been converted to a normal folder
    stmt.params["item_id"] = this._invalidDynamicContainerId;
    stmt.params["type"] = bs.TYPE_FOLDER;
    do_check_true(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "D.9",
  desc: "Fix wrong parents",

  _bookmarkId: null,
  _separatorId: null,
  _dynamicContainerId: null,
  _bookmarkId1: null,
  _bookmarkId2: null,
  _bookmarkId3: null,
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Insert a bookmark
    this._bookmarkId = addBookmark(this._placeId, bs.TYPE_BOOKMARK);
    // Insert a separator
    this._separatorId = addBookmark(null, bs.TYPE_SEPARATOR);
    // Insert a dynamic container
    this.dynamicContainerId = addBookmark(null, bs.TYPE_DYNAMIC_CONTAINER, null, null, "test");
    // Create 3 children of these items
    this._bookmarkId1 = addBookmark(this._placeId, bs.TYPE_BOOKMARK, this._bookmarkId);
    this._bookmarkId2 = addBookmark(this._placeId, bs.TYPE_BOOKMARK, this._separatorId);
    this._bookmarkId3 = addBookmark(this._placeId, bs.TYPE_BOOKMARK, this._dynamicContainerId);
  },

  check: function() {
    // Check that bookmarks are now children of a real folder (unsorted)
    let stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE id = :item_id AND parent = :parent");
    stmt.params["item_id"] = this._bookmarkId1;
    stmt.params["parent"] = bs.unfiledBookmarksFolder;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params["item_id"] = this._bookmarkId2;
    stmt.params["parent"] = bs.unfiledBookmarksFolder;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params["item_id"] = this._bookmarkId3;
    stmt.params["parent"] = bs.unfiledBookmarksFolder;
    do_check_true(stmt.executeStep());
    stmt.finalize();    
  }
});

//------------------------------------------------------------------------------
//XXX TODO
tests.push({
  name: "D.10",
  desc: "Recalculate positions",

  setup: function() {

  },

  check: function() {

  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "D.11",
  desc: "Remove old livemarks status items",

  _bookmarkId: null,
  _livemarkLoadingStatusId: null,
  _livemarkFailedStatusId: null,
  _placeId: null,
  _lmLoadingPlaceId: null,
  _lmFailedPlaceId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();

    // Insert a bookmark
    this._bookmarkId = addBookmark(this._placeId);
    // Add livemark status item
    this._lmLoadingPlaceId = addPlace("about:livemark-loading");
    this._lmFailedPlaceId = addPlace("about:livemark-failed");
    // Bookmark it
    this._livemarkLoadingStatusId = addBookmark(this._lmLoadingPlaceId);
    this._livemarkFailedStatusId = addBookmark(this._lmFailedPlaceId);
  },

  check: function() {
    // Check that valid bookmark is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_bookmarks WHERE id = :item_id");
    stmt.params["item_id"] = this._bookmarkId;
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check that livemark status items have been removed
    stmt.params["item_id"] = this._livemarkLoadingStatusId;
    do_check_false(stmt.executeStep());
    stmt.reset();
    stmt.params["item_id"] = this._livemarkFailedStatusId;
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "E.1",
  desc: "Remove orphan icons",

  _placeId: null,

  setup: function() {
    // Insert favicon entries
    let stmt = mDBConn.createStatement("INSERT INTO moz_favicons (id, url) VALUES(:favicon_id, :url)");
    stmt.params["favicon_id"] = 1;
    stmt.params["url"] = "http://www1.mozilla.org/favicon.ico";
    stmt.execute();
    stmt.reset();
    stmt.params["favicon_id"] = 2;
    stmt.params["url"] = "http://www2.mozilla.org/favicon.ico";
    stmt.execute();
    stmt.finalize();
    // Insert a place using the existing favicon entry
    this._placeId = addPlace("http://www.mozilla.org", 1);
  },

  check: function() {
    // Check that used icon is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_favicons WHERE id = :favicon_id");
    stmt.params["favicon_id"] = 1;
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check that unused icon has been removed
    stmt.params["favicon_id"] = 2;
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "F.1",
  desc: "Remove orphan visits",

  _placeId: null,
  _invalidPlaceId: 1337,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Add a valid visit and an invalid one
    stmt = mDBConn.createStatement("INSERT INTO moz_historyvisits(place_id) VALUES (:place_id)");
    stmt.params["place_id"] = this._placeId;
    stmt.execute();
    stmt.reset();
    stmt.params["place_id"] = this._invalidPlaceId;
    stmt.execute();
    stmt.finalize();
  },

  check: function() {
    // Check that valid visit is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_historyvisits WHERE place_id = :place_id");
    stmt.params["place_id"] = this._placeId;
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check that invalid visit has been removed
    stmt.params["place_id"] = this._invalidPlaceId;
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "G.1",
  desc: "Remove orphan input history",

  _placeId: null,
  _invalidPlaceId: 1337,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Add input history entries
    let stmt = mDBConn.createStatement("INSERT INTO moz_inputhistory (place_id, input) VALUES (:place_id, :input)");
    stmt.params["place_id"] = this._placeId;
    stmt.params["input"] = "moz";
    stmt.execute();
    stmt.reset();
    stmt.params["place_id"] = this._invalidPlaceId;
    stmt.params["input"] = "moz";
    stmt.execute();    
    stmt.finalize();
  },

  check: function() {
    // Check that inputhistory on valid place is still there
    let stmt = mDBConn.createStatement("SELECT place_id FROM moz_inputhistory WHERE place_id = :place_id");
    stmt.params["place_id"] = this._placeId;
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check that inputhistory on invalid place has gone
    stmt.params["place_id"] = this._invalidPlaceId;
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "H.1",
  desc: "Remove item annos with an invalid attribute",

  _usedItemAttribute: "usedItem",
  _bookmarkId: null,
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Insert a bookmark
    this._bookmarkId = addBookmark(this._placeId);
    // Add a used attribute.
    let stmt = mDBConn.createStatement("INSERT INTO moz_anno_attributes (name) VALUES (:anno)");
    stmt.params['anno'] = this._usedItemAttribute;
    stmt.execute();
    stmt.finalize();
    stmt = mDBConn.createStatement("INSERT INTO moz_items_annos (item_id, anno_attribute_id) VALUES(:item_id, (SELECT id FROM moz_anno_attributes WHERE name = :anno))");
    stmt.params['item_id'] = this._bookmarkId;
    stmt.params['anno'] = this._usedItemAttribute;
    stmt.execute();
    stmt.finalize();
    // Add an annotation with a nonexistent attribute
    stmt = mDBConn.createStatement("INSERT INTO moz_items_annos (item_id, anno_attribute_id) VALUES(:item_id, 1337)");
    stmt.params['item_id'] = this._bookmarkId;
    stmt.execute();
    stmt.finalize();
  },

  check: function() {
    // Check that used attribute is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_anno_attributes WHERE name = :anno");
    stmt.params['anno'] = this._usedItemAttribute;
    do_check_true(stmt.executeStep());
    stmt.finalize();
    // check that annotation with valid attribute is still there
    stmt = mDBConn.createStatement("SELECT id FROM moz_items_annos WHERE anno_attribute_id = (SELECT id FROM moz_anno_attributes WHERE name = :anno)");
    stmt.params['anno'] = this._usedItemAttribute;
    do_check_true(stmt.executeStep());
    stmt.finalize();
    // Check that annotation with bogus attribute has been removed
    stmt = mDBConn.createStatement("SELECT id FROM moz_items_annos WHERE anno_attribute_id = 1337");
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "H.2",
  desc: "Remove orphan item annotations",

  _usedItemAttribute: "usedItem",
  _bookmarkId: null,
  _invalidBookmarkId: 8888,
  _placeId: null,

  setup: function() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Insert a bookmark
    this._bookmarkId = addBookmark(this._placeId);
    // Add a used attribute.
    stmt = mDBConn.createStatement("INSERT INTO moz_anno_attributes (name) VALUES (:anno)");
    stmt.params['anno'] = this._usedItemAttribute;
    stmt.execute();
    stmt.finalize();
    stmt = mDBConn.createStatement("INSERT INTO moz_items_annos (item_id, anno_attribute_id) VALUES (:item_id, (SELECT id FROM moz_anno_attributes WHERE name = :anno))");
    stmt.params["item_id"] = this._bookmarkId;
    stmt.params["anno"] = this._usedItemAttribute;
    stmt.execute();
    stmt.reset();
    // Add an annotation to a nonexistent item
    stmt.params["item_id"] = this._invalidBookmarkId;
    stmt.params["anno"] = this._usedItemAttribute;
    stmt.execute();
    stmt.finalize();
  },

  check: function() {
    // Check that used attribute is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_anno_attributes WHERE name = :anno");
    stmt.params['anno'] = this._usedItemAttribute;
    do_check_true(stmt.executeStep());
    stmt.finalize();
    // check that annotation with valid attribute is still there
    stmt = mDBConn.createStatement("SELECT id FROM moz_items_annos WHERE anno_attribute_id = (SELECT id FROM moz_anno_attributes WHERE name = :anno)");
    stmt.params['anno'] = this._usedItemAttribute;
    do_check_true(stmt.executeStep());
    stmt.finalize();
    // Check that an annotation to a nonexistent page has been removed
    stmt = mDBConn.createStatement("SELECT id FROM moz_items_annos WHERE item_id = 8888");
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});


//------------------------------------------------------------------------------

tests.push({
  name: "I.1",
  desc: "Remove unused keywords",

  _bookmarkId: null,
  _placeId: null,

  setup: function() {
    // Insert 2 keywords
    let stmt = mDBConn.createStatement("INSERT INTO moz_keywords (id, keyword) VALUES(:id, :keyword)");
    stmt.params["id"] = 1;
    stmt.params["keyword"] = "used";
    stmt.execute();
    stmt.reset();
    stmt.params["id"] = 2;
    stmt.params["keyword"] = "unused";
    stmt.execute();
    stmt.finalize();
    // Add a place to ensure place_id = 1 is valid
    this._placeId = addPlace();
    // Insert a bookmark using the "used" keyword
    this._bookmarkId = addBookmark(this._placeId, bs.TYPE_BOOKMARK, bs.unfiledBookmarksFolder, 1);
  },

  check: function() {
    // Check that "used" keyword is still there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_keywords WHERE keyword = :keyword");
    stmt.params["keyword"] = "used";
    do_check_true(stmt.executeStep());
    stmt.reset();
    // Check that "unused" keyword has gone
    stmt.params["keyword"] = "unused";
    do_check_false(stmt.executeStep());
    stmt.finalize();
  }
});


//------------------------------------------------------------------------------

tests.push({
  name: "L.1",
  desc: "Fix wrong favicon ids",

  _validIconPlaceId: null,
  _invalidIconPlaceId: null,

  setup: function() {
    // Insert a favicon entry
    let stmt = mDBConn.createStatement("INSERT INTO moz_favicons (id, url) VALUES(1, :url)");
    stmt.params["url"] = "http://www.mozilla.org/favicon.ico";
    stmt.execute();
    stmt.finalize();
    // Insert a place using the existing favicon entry
    this._validIconPlaceId = addPlace("http://www1.mozilla.org", 1);

    // Insert a place using a nonexistent favicon entry
    this._invalidIconPlaceId = addPlace("http://www2.mozilla.org", 1337);
  },

  check: function() {
    // Check that bogus favicon is not there
    let stmt = mDBConn.createStatement("SELECT id FROM moz_places WHERE favicon_id = :favicon_id");
    stmt.params["favicon_id"] = 1337;
    do_check_false(stmt.executeStep());
    stmt.reset();
    // Check that valid favicon is still there
    stmt.params["favicon_id"] = 1;
    do_check_true(stmt.executeStep());
    stmt.finalize();
    // Check that place entries are there
    stmt = mDBConn.createStatement("SELECT id FROM moz_places WHERE id = :place_id");
    stmt.params["place_id"] = this._validIconPlaceId;
    do_check_true(stmt.executeStep());
    stmt.reset();
    stmt.params["place_id"] = this._invalidIconPlaceId;
    do_check_true(stmt.executeStep());
    stmt.finalize();
  }
});

//------------------------------------------------------------------------------
//XXX TODO
tests.push({
  name: "L.2",
  desc: "Recalculate visit_count",

  setup: function() {

  },

  check: function() {

  }
});

//------------------------------------------------------------------------------

tests.push({
  name: "Z",
  desc: "Sanity: Preventive maintenance does not touch valid items",

  _uri1: uri("http://www1.mozilla.org"),
  _uri2: uri("http://www2.mozilla.org"),
  _folderId: null,
  _bookmarkId: null,
  _separatorId: null,

  setup: function() {
    // use valid api calls to create a bunch of items
    hs.addVisit(this._uri1, Date.now() * 1000, null,
                hs.TRANSITION_TYPED, false, 0);
    hs.addVisit(this._uri2, Date.now() * 1000, null,
                hs.TRANSITION_TYPED, false, 0);

    this._folderId = bs.createFolder(bs.toolbarFolder, "testfolder",
                                     bs.DEFAULT_INDEX);
    do_check_true(this._folderId > 0);
    this._bookmarkId = bs.insertBookmark(this._folderId, this._uri1,
                                         bs.DEFAULT_INDEX, "testbookmark");
    do_check_true(this._bookmarkId > 0);
    this._separatorId = bs.insertSeparator(bs.unfiledBookmarksFolder,
                                           bs.DEFAULT_INDEX);
    do_check_true(this._separatorId > 0);
    ts.tagURI(this._uri1, ["testtag"]);
    fs.setFaviconUrlForPage(this._uri2,
                            uri("http://www2.mozilla.org/favicon.ico"));
    bs.setKeywordForBookmark(this._bookmarkId, "testkeyword");
    as.setPageAnnotation(this._uri2, "anno", "anno", 0, as.EXPIRE_NEVER);
    as.setItemAnnotation(this._bookmarkId, "anno", "anno", 0, as.EXPIRE_NEVER);
  },

  check: function() {
    // Check that all items are correct
    do_check_true(bh.isVisited(this._uri1));
    do_check_true(bh.isVisited(this._uri2));
    
    do_check_eq(bs.getBookmarkURI(this._bookmarkId).spec, this._uri1.spec);
    do_check_eq(bs.getItemIndex(this._folderId), 0);

    do_check_eq(bs.getItemType(this._folderId), bs.TYPE_FOLDER);
    do_check_eq(bs.getItemType(this._separatorId), bs.TYPE_SEPARATOR);

    do_check_eq(ts.getTagsForURI(this._uri1).length, 1);
    do_check_eq(bs.getKeywordForBookmark(this._bookmarkId), "testkeyword");
    do_check_eq(fs.getFaviconForPage(this._uri2).spec,
                "http://www2.mozilla.org/favicon.ico");
    do_check_eq(as.getPageAnnotation(this._uri2, "anno"), "anno");
    do_check_eq(as.getItemAnnotation(this._bookmarkId, "anno"), "anno");
  }
});

//------------------------------------------------------------------------------

let observer = {
  observe: function(aSubject, aTopic, aData) {
    if (aTopic == FINISHED_MAINTANANCE_NOTIFICATION_TOPIC) {
      try {current_test.check();}
      catch (ex){ do_throw(ex);}
      cleanDatabase();
      if (tests.length) {
        current_test = tests.shift();
        dump("\nExecuting test: " + current_test.name + "\n" + "*** " + current_test.desc + "\n");
        current_test.setup();
        PlacesDBUtils.maintenanceOnIdle();
      }
      else {
        os.removeObserver(this, FINISHED_MAINTANANCE_NOTIFICATION_TOPIC);
        // Sanity check: all roots should be intact
        do_check_eq(bs.getFolderIdForItem(bs.placesRoot), 0);
        do_check_eq(bs.getFolderIdForItem(bs.bookmarksMenuFolder), bs.placesRoot);
        do_check_eq(bs.getFolderIdForItem(bs.tagsFolder), bs.placesRoot);
        do_check_eq(bs.getFolderIdForItem(bs.unfiledBookmarksFolder), bs.placesRoot);
        do_check_eq(bs.getFolderIdForItem(bs.toolbarFolder), bs.placesRoot);
        do_test_finished();
      }
    }
  }
}
os.addObserver(observer, FINISHED_MAINTANANCE_NOTIFICATION_TOPIC, false);


// main
function run_test() {
  // Force initialization of the bookmarks hash. This test could cause
  // it to go out of sync due to direct queries on the database.
  hs.addVisit(uri("http://force.bookmarks.hash"), Date.now() * 1000, null,
              hs.TRANSITION_TYPED, false, 0);
  do_check_false(bs.isBookmarked(uri("http://force.bookmarks.hash")));

  // Get current bookmarks max ID for cleanup
  let stmt = mDBConn.createStatement("SELECT MAX(id) FROM moz_bookmarks");
  stmt.executeStep();
  defaultBookmarksMaxId = stmt.getInt32(0);
  stmt.finalize();
  do_check_true(defaultBookmarksMaxId > 0);

  // Let test run till completion.
  // Test will end in the observer when all tests have finished running.
  do_test_pending();

  current_test = tests.shift();
  dump("\nExecuting test: " + current_test.name + "\n" + "*** " + current_test.desc + "\n");
  current_test.setup();
  PlacesDBUtils.maintenanceOnIdle();
}
