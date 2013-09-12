/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This file tests the basic synchronous (deprecated) and asynchronous favicons
 * APIs, and their interactions when they are used together.
 */

function run_test() {
  run_next_test();
}

/*
 * The following few tests are asynchronous but share state. We bundle that
 * state into two arrays: `icons` and `pages`.
 * The tests are in four parts:
 *
 * 1. setup, where we add some history visits, and set an icon for one of them;
 * 2. getFaviconURLForPage, where we test synchronous retrieval;
 * 3. second_and_third, where we add icons for the remaining two pages, and test
 *    them synchronously;
 * 4. getFaviconDataForPage, which tests asynchronous retrieval.
 */
let icons = [
  {
    name: "favicon-normal32.png",
    mime: "image/png",
    data: null,
    uri:  NetUtil.newURI("file:///./favicon-normal32.png")
  },
  {
    name: "favicon-normal16.png",
    mime: "image/png",
    data: null,
    uri:  NetUtil.newURI("file:///./favicon-normal16.png")
  }
];

let pages = [
  NetUtil.newURI("http://foo.bar/"),
  NetUtil.newURI("http://bar.foo/"),
  NetUtil.newURI("http://foo.bar.moz/")
];

add_task(function test_set_and_get_favicon_setup() {
  do_log_info("Setup code for set/get favicon.");
  let [icon0, icon1] = icons;

  // 32x32 png, 344 bytes.
  icon0.data = readFileOfLength(icon0.name, 344);

  // 16x16 png, 286 bytes.
  icon1.data = readFileOfLength(icon1.name, 286);

  // Add visits to the DB.
  for each (let uri in pages) {
    yield promiseAddVisits(uri);
  }

  // Set first page icon.
  try {
    PlacesUtils.favicons.setFaviconData(icon0.uri, icon0.data, icon0.data.length,
                                        icon0.mime, Number.MAX_VALUE);
  } catch (ex) {
    do_throw("Failure setting first page icon: " + ex);
  }
  PlacesUtils.favicons.setFaviconUrlForPage(pages[0], icon0.uri);
  do_check_guid_for_uri(pages[0]);

  let favicon = PlacesUtils.favicons.getFaviconForPage(pages[0]);
  do_check_true(icon0.uri.equals(favicon));
});

add_test(function test_set_and_get_favicon_getFaviconURLForPage() {
  let [icon0] = icons;
  PlacesUtils.favicons.getFaviconURLForPage(pages[0],
    function (aURI, aDataLen, aData, aMimeType) {
      do_check_true(icon0.uri.equals(aURI));
      do_check_eq(aDataLen, 0);
      do_check_eq(aData.length, 0);
      do_check_eq(aMimeType, "");
      run_next_test();
    });
});

add_test(function test_set_and_get_favicon_second_and_third() {
  let [icon0, icon1] = icons;
  try {
    PlacesUtils.favicons.setFaviconData(icon1.uri, icon1.data, icon1.data.length,
                                        icon1.mime, Number.MAX_VALUE);
  } catch (ex) {
    do_throw("Failure setting second page icon: " + ex);
  }
  PlacesUtils.favicons.setFaviconUrlForPage(pages[1], icon1.uri);
  do_check_guid_for_uri(pages[1]);
  do_check_true(icon1.uri.equals(PlacesUtils.favicons.getFaviconForPage(pages[1])));

  // Set third page icon as the same as first page one.
  try {
    PlacesUtils.favicons.setFaviconData(icon0.uri, icon0.data, icon0.data.length,
                                        icon0.mime, Number.MAX_VALUE);
  } catch (ex) {
    do_throw("Failure setting third page icon: " + ex);
  }
  PlacesUtils.favicons.setFaviconUrlForPage(pages[2], icon0.uri);
  do_check_guid_for_uri(pages[2]);
  let page3favicon = PlacesUtils.favicons.getFaviconForPage(pages[2]);
  do_check_true(icon0.uri.equals(page3favicon));

  // Check first page icon.
  let out1MimeType = {};
  let out1Data = PlacesUtils.favicons.getFaviconData(icon0.uri, out1MimeType);
  do_check_eq(icon0.mime, out1MimeType.value);
  do_check_true(compareArrays(icon0.data, out1Data));

  // Check second page icon.
  let out2MimeType = {};
  let out2Data = PlacesUtils.favicons.getFaviconData(icon1.uri, out2MimeType);
  do_check_eq(icon1.mime, out2MimeType.value);
  do_check_true(compareArrays(icon1.data, out2Data));

  // Check third page icon.
  let out3MimeType = {};
  let out3Data = PlacesUtils.favicons.getFaviconData(page3favicon, out3MimeType);
  do_check_eq(icon0.mime, out3MimeType.value);
  do_check_true(compareArrays(icon0.data, out3Data));
  run_next_test();
});

add_test(function test_set_and_get_favicon_getFaviconDataForPage() {
  let [icon0] = icons;
  PlacesUtils.favicons.getFaviconDataForPage(pages[0],
    function(aURI, aDataLen, aData, aMimeType) {
      do_check_true(aURI.equals(icon0.uri));
      do_check_eq(icon0.mime, icon0.mime);
      do_check_true(compareArrays(icon0.data, aData));
      do_check_eq(aDataLen, aData.length);
      run_next_test();
    });
});

add_test(function test_favicon_links() {
  let pageURI = NetUtil.newURI("http://foo.bar/");
  let faviconURI = NetUtil.newURI("file:///./favicon-normal32.png");
  do_check_eq(PlacesUtils.favicons.getFaviconImageForPage(pageURI).spec,
              PlacesUtils.favicons.getFaviconLinkForIcon(faviconURI).spec);
  run_next_test();
});

add_test(function test_failed_favicon_cache() {
  // 32x32 png, 344 bytes.
  let iconName = "favicon-normal32.png";
  let faviconURI = NetUtil.newURI("file:///./" + iconName);

  PlacesUtils.favicons.addFailedFavicon(faviconURI);
  do_check_true(PlacesUtils.favicons.isFailedFavicon(faviconURI));
  PlacesUtils.favicons.removeFailedFavicon(faviconURI);
  do_check_false(PlacesUtils.favicons.isFailedFavicon(faviconURI));
  run_next_test();
});

add_test(function test_getFaviconData_on_the_default_favicon() {
  let icon = PlacesUtils.favicons.defaultFavicon;
  let outMimeType = {};
  let outData = PlacesUtils.favicons.getFaviconData(icon, outMimeType);
  do_check_eq(outMimeType.value, "image/png");

  // Read in the icon and compare it to what the API returned above.
  let istream = NetUtil.newChannel(PlacesUtils.favicons.defaultFavicon).open();
  let expectedData = readInputStreamData(istream);
  do_check_true(compareArrays(outData, expectedData));
  run_next_test();
});

/*
 * Retrieve the GUID for a favicon URI. For now we'll do this through SQL.
 * Provide a mozIStorageStatementCallback, such as a SingleGUIDCallback.
 */
function guidForFaviconURI(iconURIString, cb) {
  let query = "SELECT guid FROM moz_favicons WHERE url = :url";
  let stmt = cb.statement = DBConn().createAsyncStatement(query);
  stmt.params.url = iconURIString;
  stmt.executeAsync(cb);
  stmt.finalize();
}

/*
 * A mozIStorageStatementCallback for single GUID results.
 * Pass in a callback function, which will be invoked on completion.
 * Ensures that only a single GUID is returned.
 */
function SingleGUIDCallback(cb) {
  this.called = 0;
  this.cb     = cb;
}
SingleGUIDCallback.prototype = {
  _guid: null,
  handleCompletion: function handleCompletion(reason) {
    do_log_info("Completed single GUID callback.");
    do_check_eq(this.called, 1);
    this.cb(this._guid);
  },
  handleError: function handleError(err) {
    do_throw(err);
  },
  handleResult: function handleResult(resultSet) {
    this.called++;
    this._guid = resultSet.getNextRow().getResultByName("guid");
    do_log_info("Retrieved GUID is " + this._guid);
    do_check_true(!!this._guid);
    do_check_valid_places_guid(this._guid);
    do_check_eq(null, resultSet.getNextRow());  // No more rows.
  }
};

function insertToolbarBookmark(uri, title) {
  PlacesUtils.bookmarks.insertBookmark(
    PlacesUtils.toolbarFolderId,
    uri,
    PlacesUtils.bookmarks.DEFAULT_INDEX,
    title
  );
}

add_test(function test_insert_synchronous_mints_guid() {
  do_log_info("Test that synchronously inserting a favicon results in a " +
              "record with a new GUID.");

  let testURI     = "http://test.com/sync/";
  let testIconURI = "http://test.com/favicon.ico";
  let pageURI     = NetUtil.newURI(testURI);

  // No icon to start with.
  checkFaviconMissingForPage(pageURI, function () {
    // Add a page with a bookmark.
    insertToolbarBookmark(pageURI, "Test page");

    // Set a favicon for the page.
    PlacesUtils.favicons.setFaviconUrlForPage(
      pageURI, NetUtil.newURI(testIconURI)
    );

    // Check that the URI has been set correctly.
    do_check_eq(PlacesUtils.favicons.getFaviconForPage(pageURI).spec,
                testIconURI);

    guidForFaviconURI(testIconURI, new SingleGUIDCallback(run_next_test));
  });
});

add_test(function test_insert_asynchronous_mints_guid() {
  do_log_info("Test that asynchronously inserting a favicon results in a " +
              "record with a new GUID.");

  let testURI = "http://test.com/async/";
  let iconURI = NetUtil.newURI(do_get_file("favicon-normal32.png"));
  let pageURI = NetUtil.newURI(testURI);

  // No icon to start with.
  checkFaviconMissingForPage(pageURI, function () {
    // Add a page with a bookmark.
    insertToolbarBookmark(pageURI, "Other test page");

    // Set a favicon for the page.
    do_log_info("Asynchronously setting page favicon.");
    PlacesUtils.favicons.setAndFetchFaviconForPage(
      pageURI, iconURI, false,
      PlacesUtils.favicons.FAVICON_LOAD_NON_PRIVATE,
      function AMG_faviconDataCallback(uri, len, data, mimeType) {
        do_check_true(iconURI.equals(uri));

        // Make sure there's a valid GUID.
        guidForFaviconURI(iconURI.spec, new SingleGUIDCallback(run_next_test));
      });
  });
});

add_test(function test_insert_asynchronous_update_preserves_guid() {
  do_log_info("Test that asynchronously inserting an existing favicon leaves " +
              "the GUID unchanged.");

  let testURI = "http://test.com/async/";
  let iconURI = NetUtil.newURI(do_get_file("favicon-normal32.png"));
  let pageURI = NetUtil.newURI(testURI);

  guidForFaviconURI(iconURI.spec, new SingleGUIDCallback(function (guid) {
    // Set a favicon for the page... again.
    do_log_info("Asynchronously re-setting page favicon.");
    PlacesUtils.favicons.setAndFetchFaviconForPage(
      pageURI, iconURI, true,
      PlacesUtils.favicons.FAVICON_LOAD_NON_PRIVATE,
      function UPG_faviconDataCallback(uri, len, data, mimeType) {
        do_check_true(iconURI.equals(uri));

        // Make sure there's a valid GUID.
        guidForFaviconURI(iconURI.spec,
          new SingleGUIDCallback(function (again) {
            do_check_eq(guid, again);
            run_next_test();
          }));
      });
  }));
});

add_test(function test_setFaviconURLForPage_nonexistingPage_throws() {
  try {
    PlacesUtils.favicons.setFaviconUrlForPage(
      NetUtil.newURI("http://nonexisting.moz.org"), icons[0].uri);
    do_throw("Setting an icon for a nonexisting page should throw")
  } catch (ex if ex.result == Cr.NS_ERROR_NOT_AVAILABLE) {}
  run_next_test();
});

/*
 * TODO: need a test for async write that modifies a GUID for a favicon.
 * This will come later, when there's an API that actually does that!
 */
