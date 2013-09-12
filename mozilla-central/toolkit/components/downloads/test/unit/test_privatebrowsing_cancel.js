/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
  Make sure that the download manager service is given a chance to cancel the
  private browisng mode transition.
**/

const Cm = Components.manager;

const kPromptServiceUUID = "{6cc9c9fe-bc0b-432b-a410-253ef8bcc699}";
const kPromptServiceContractID = "@mozilla.org/embedcomp/prompt-service;1";

// Save original prompt service factory
const kPromptServiceFactory = Cm.getClassObject(Cc[kPromptServiceContractID],
                                                Ci.nsIFactory);

let fakePromptServiceFactory = {
  createInstance: function(aOuter, aIid) {
    if (aOuter != null)
      throw Cr.NS_ERROR_NO_AGGREGATION;
    return promptService.QueryInterface(aIid);
  }
};

let promptService = {
  _buttonChoice: 0,
  _called: false,
  wasCalled: function() {
    let called = this._called;
    this._called = false;
    return called;
  },
  sayCancel: function() {
    this._buttonChoice = 1;
    this._called = false;
  },
  sayProceed: function() {
    this._buttonChoice = 0;
    this._called = false;
  },
  QueryInterface: function(aIID) {
    if (aIID.equals(Ci.nsIPromptService) ||
        aIID.equals(Ci.nsISupports)) {
      return this;
    }
    throw Cr.NS_ERROR_NO_INTERFACE;
  },
  confirmEx: function(parent, title, text, buttonFlags,
                      button0Title, button1Title, button2Title,
                      checkMsg, checkState) {
    this._called = true;
    return this._buttonChoice;
  }
};

Cm.QueryInterface(Ci.nsIComponentRegistrar)
  .registerFactory(Components.ID(kPromptServiceUUID), "Prompt Service",
                   kPromptServiceContractID, fakePromptServiceFactory);

this.__defineGetter__("dm", function() {
  delete this.dm;
  return this.dm = Cc["@mozilla.org/download-manager;1"].
                   getService(Ci.nsIDownloadManager);
});

function trigger_pb_cleanup(expected)
{
  var obs = Cc["@mozilla.org/observer-service;1"].getService(Ci.nsIObserverService);
  var cancel = Cc["@mozilla.org/supports-PRBool;1"].createInstance(Ci.nsISupportsPRBool);
  cancel.data = false;
  obs.notifyObservers(cancel, "last-pb-context-exiting", null);
  do_check_eq(expected, cancel.data);
  if (!expected)
    obs.notifyObservers(cancel, "last-pb-context-exited", null);
}

function run_test() {
  function finishTest() {
    // Cancel Download-E
    dlF.cancel();
    dlF.remove();
    dm.cleanUp();
    dm.cleanUpPrivate();
    do_check_eq(dm.activeDownloadCount, 0);
    do_check_eq(dm.activePrivateDownloadCount, 0);

    dm.removeListener(listener);
    httpserv.stop(do_test_finished);

    // Unregister the factory so we do not leak
    Cm.QueryInterface(Ci.nsIComponentRegistrar)
      .unregisterFactory(Components.ID(kPromptServiceUUID),
                         fakePromptServiceFactory);

    // Restore the original factory
    Cm.QueryInterface(Ci.nsIComponentRegistrar)
      .registerFactory(Components.ID(kPromptServiceUUID), "Prompt Service",
                       kPromptServiceContractID, kPromptServiceFactory);
  }

  do_test_pending();
  let httpserv = new HttpServer();
  httpserv.registerDirectory("/file/", do_get_cwd());
  httpserv.registerPathHandler("/noresume", function (meta, response) {
    response.setHeader("Content-Type", "text/html", false);
    response.setHeader("Accept-Ranges", "none", false);
    response.write("foo");
  });
  httpserv.start(4444);

  let tmpDir = Cc["@mozilla.org/file/directory_service;1"].
               getService(Ci.nsIProperties).
               get("TmpD", Ci.nsIFile);

  // make sure we're starting with an empty DB
  do_check_eq(dm.activeDownloadCount, 0);

  let listener = {
    onDownloadStateChange: function(aState, aDownload)
    {
      switch (aDownload.state) {
        case dm.DOWNLOAD_QUEUED:
        case dm.DOWNLOAD_DOWNLOADING:
          if (aDownload.targetFile.equals(dlD.targetFile)) {
            // Sanity check: Download-D must not be resumable
            do_check_false(dlD.resumable);

            // Cancel the transition
            promptService.sayCancel();
            trigger_pb_cleanup(true);
            do_check_true(promptService.wasCalled());
            do_check_eq(dm.activePrivateDownloadCount, 1);

            promptService.sayProceed();
            trigger_pb_cleanup(false);
            do_check_true(promptService.wasCalled());
            do_check_eq(dm.activePrivateDownloadCount, 0);
            do_check_eq(dlD.state, dm.DOWNLOAD_CANCELED);

            // Create Download-E
            dlE = addDownload({
              isPrivate: true,
              targetFile: fileE,
              sourceURI: downloadESource,
              downloadName: downloadEName
            });

            // Wait for Download-E to start
          } else if (aDownload.targetFile.equals(dlE.targetFile)) {
            // Sanity check: Download-E must be resumable
            do_check_true(dlE.resumable);

            promptService.sayCancel();
            trigger_pb_cleanup(true);
            do_check_true(promptService.wasCalled());
            do_check_eq(dm.activePrivateDownloadCount, 1);

            promptService.sayProceed();
            trigger_pb_cleanup(false);
            do_check_true(promptService.wasCalled());
            do_check_eq(dm.activePrivateDownloadCount, 0);
            do_check_eq(dlE.state, dm.DOWNLOAD_PAUSED);

            // Create Download-F
            dlF = addDownload({
              isPrivate: false,
              targetFile: fileF,
              sourceURI: downloadFSource,
              downloadName: downloadFName
            });

            // Wait for Download-F to start
          } else if (aDownload.targetFile.equals(dlF.targetFile)) {
            // Sanity check: Download-F must not be resumable
            do_check_false(dlF.resumable);

            promptService.sayCancel();
            trigger_pb_cleanup(false);
            do_check_false(promptService.wasCalled());
            do_check_eq(dm.activeDownloadCount, 1);
            do_check_eq(dlF.state, dm.DOWNLOAD_DOWNLOADING);
            finishTest();
          }
          break;
      }
    },
    onStateChange: function(a, b, c, d, e) { },
    onProgressChange: function(a, b, c, d, e, f, g) { },
    onSecurityChange: function(a, b, c, d) { }
  };

  dm.addPrivacyAwareListener(listener);

  // properties of Download-D
  const downloadDSource = "http://localhost:4444/noresume";
  const downloadDDest = "download-file-D";
  const downloadDName = "download-D";

  // properties of Download-E
  const downloadESource = "http://localhost:4444/file/head_download_manager.js";
  const downloadEDest = "download-file-E";
  const downloadEName = "download-E";

  // properties of Download-F
  const downloadFSource = "http://localhost:4444/noresume";
  const downloadFDest = "download-file-F";
  const downloadFName = "download-F";

  // Create all target files
  let fileD = tmpDir.clone();
  fileD.append(downloadDDest);
  fileD.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0666);
  let fileE = tmpDir.clone();
  fileE.append(downloadEDest);
  fileE.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0666);
  let fileF = tmpDir.clone();
  fileF.append(downloadFDest);
  fileF.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0666);

  // Create Download-D
  let dlD = addDownload({
    isPrivate: true,
    targetFile: fileD,
    sourceURI: downloadDSource,
    downloadName: downloadDName
  });

  let dlE, dlF;

  // wait for Download-D to start
}
