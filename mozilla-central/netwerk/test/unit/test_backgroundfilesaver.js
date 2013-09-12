/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This file tests components that implement nsIBackgroundFileSaver.
 */

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;
const Cr = Components.results;

////////////////////////////////////////////////////////////////////////////////
//// Globals

Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "FileUtils",
                                  "resource://gre/modules/FileUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "NetUtil",
                                  "resource://gre/modules/NetUtil.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Promise",
                                  "resource://gre/modules/commonjs/promise/core.js");
XPCOMUtils.defineLazyModuleGetter(this, "Task",
                                  "resource://gre/modules/Task.jsm");

const BackgroundFileSaverOutputStream = Components.Constructor(
      "@mozilla.org/network/background-file-saver;1?mode=outputstream",
      "nsIBackgroundFileSaver");

const BackgroundFileSaverStreamListener = Components.Constructor(
      "@mozilla.org/network/background-file-saver;1?mode=streamlistener",
      "nsIBackgroundFileSaver");

const StringInputStream = Components.Constructor(
      "@mozilla.org/io/string-input-stream;1",
      "nsIStringInputStream",
      "setData");

const REQUEST_SUSPEND_AT = 1024 * 1024 * 4;
const TEST_DATA_SHORT = "This test string is written to the file.";
const TEST_FILE_NAME_1 = "test-backgroundfilesaver-1.txt";
const TEST_FILE_NAME_2 = "test-backgroundfilesaver-2.txt";
const TEST_FILE_NAME_3 = "test-backgroundfilesaver-3.txt";

const gTextDecoder = new TextDecoder();

// Generate a long string of data in a moderately fast way.
const TEST_256_CHARS = new Array(257).join("-");
const DESIRED_LENGTH = REQUEST_SUSPEND_AT * 2;
const TEST_DATA_LONG = new Array(1 + DESIRED_LENGTH / 256).join(TEST_256_CHARS);
do_check_eq(TEST_DATA_LONG.length, DESIRED_LENGTH);

/**
 * This function will be uplifted to the testing framework.
 *
 * @see toolkit/components/places/test/unit/head_common.js
 */
function add_task(aTaskFn) {
  function wrapperFn() {
    Task.spawn(aTaskFn)
        .then(run_next_test, do_report_unexpected_exception);
  }
  eval("add_test(function " + aTaskFn.name + "() wrapperFn());");
}

/**
 * Returns a reference to a temporary file.  If the file is then created, it
 * will be removed when tests in this file finish.
 */
function getTempFile(aLeafName) {
  let file = FileUtils.getFile("TmpD", [aLeafName]);
  do_register_cleanup(function GTF_cleanup() {
    if (file.exists()) {
      file.remove(false);
    }
  });
  return file;
}

/**
 * Ensures that the given file contents are equal to the given string.
 *
 * @param aFile
 *        nsIFile whose contents should be verified.
 * @param aExpectedContents
 *        String containing the octets that are expected in the file.
 *
 * @return {Promise}
 * @resolves When the operation completes.
 * @rejects Never.
 */
function promiseVerifyContents(aFile, aExpectedContents) {
  let deferred = Promise.defer();
  NetUtil.asyncFetch(aFile, function(aInputStream, aStatus) {
    do_check_true(Components.isSuccessCode(aStatus));
    let contents = NetUtil.readInputStreamToString(aInputStream,
                                                   aInputStream.available());
    if (contents.length <= TEST_DATA_SHORT.length * 2) {
      do_check_eq(contents, aExpectedContents);
    } else {
      // Do not print the entire content string to the test log.
      do_check_eq(contents.length, aExpectedContents.length);
      do_check_true(contents == aExpectedContents);
    }
    deferred.resolve();
  });
  return deferred.promise;
}

/**
 * Waits for the given saver object to complete.
 *
 * @param aSaver
 *        The saver, with the output stream or a stream listener implementation.
 * @param aOnTargetChangeFn
 *        Optional callback invoked with the target file name when it changes.
 *
 * @return {Promise}
 * @resolves When onSaveComplete is called with a success code.
 * @rejects With an exception, if onSaveComplete is called with a failure code.
 */
function promiseSaverComplete(aSaver, aOnTargetChangeFn) {
  let deferred = Promise.defer();
  aSaver.observer = {
    onTargetChange: function BFSO_onSaveComplete(aSaver, aTarget)
    {
      if (aOnTargetChangeFn) {
        aOnTargetChangeFn(aTarget);
      }
    },
    onSaveComplete: function BFSO_onSaveComplete(aSaver, aStatus)
    {
      if (Components.isSuccessCode(aStatus)) {
        deferred.resolve();
      } else {
        deferred.reject(new Components.Exception("Saver failed.", aStatus));
      }
    },
  };
  return deferred.promise;
}

/**
 * Feeds a string to a BackgroundFileSaverOutputStream.
 *
 * @param aSourceString
 *        The source data to copy.
 * @param aSaverOutputStream
 *        The BackgroundFileSaverOutputStream to feed.
 * @param aCloseWhenDone
 *        If true, the output stream will be closed when the copy finishes.
 *
 * @return {Promise}
 * @resolves When the copy completes with a success code.
 * @rejects With an exception, if the copy fails.
 */
function promiseCopyToSaver(aSourceString, aSaverOutputStream, aCloseWhenDone) {
  let deferred = Promise.defer();
  let inputStream = new StringInputStream(aSourceString, aSourceString.length);
  let copier = Cc["@mozilla.org/network/async-stream-copier;1"]
               .createInstance(Ci.nsIAsyncStreamCopier);
  copier.init(inputStream, aSaverOutputStream, null, false, true, 0x8000, true,
              aCloseWhenDone);
  copier.asyncCopy({
    onStartRequest: function () { },
    onStopRequest: function (aRequest, aContext, aStatusCode)
    {
      if (Components.isSuccessCode(aStatusCode)) {
        deferred.resolve();
      } else {
        deferred.reject(new Components.Exception(aResult));
      }
    },
  }, null);
  return deferred.promise;
}

/**
 * Feeds a string to a BackgroundFileSaverStreamListener.
 *
 * @param aSourceString
 *        The source data to copy.
 * @param aSaverStreamListener
 *        The BackgroundFileSaverStreamListener to feed.
 * @param aCloseWhenDone
 *        If true, the output stream will be closed when the copy finishes.
 *
 * @return {Promise}
 * @resolves When the operation completes with a success code.
 * @rejects With an exception, if the operation fails.
 */
function promisePumpToSaver(aSourceString, aSaverStreamListener,
                            aCloseWhenDone) {
  let deferred = Promise.defer();
  aSaverStreamListener.QueryInterface(Ci.nsIStreamListener);
  let inputStream = new StringInputStream(aSourceString, aSourceString.length);
  let pump = Cc["@mozilla.org/network/input-stream-pump;1"]
             .createInstance(Ci.nsIInputStreamPump);
  pump.init(inputStream, -1, -1, 0, 0, true);
  pump.asyncRead({
    onStartRequest: function PPTS_onStartRequest(aRequest, aContext)
    {
      aSaverStreamListener.onStartRequest(aRequest, aContext);
    },
    onStopRequest: function PPTS_onStopRequest(aRequest, aContext, aStatusCode)
    {
      aSaverStreamListener.onStopRequest(aRequest, aContext, aStatusCode);
      if (Components.isSuccessCode(aStatusCode)) {
        deferred.resolve();
      } else {
        deferred.reject(new Components.Exception(aResult));
      }
    },
    onDataAvailable: function PPTS_onDataAvailable(aRequest, aContext,
                                                   aInputStream, aOffset,
                                                   aCount)
    {
      aSaverStreamListener.onDataAvailable(aRequest, aContext, aInputStream,
                                           aOffset, aCount);
    },
  }, null);
  return deferred.promise;
}

let gStillRunning = true;

////////////////////////////////////////////////////////////////////////////////
//// Tests

function run_test()
{
  run_next_test();
}

add_task(function test_setup()
{
  // Wait 10 minutes, that is half of the external xpcshell timeout.
  do_timeout(10 * 60 * 1000, function() {
    if (gStillRunning) {
      do_throw("Test timed out.");
    }
  })
});

add_task(function test_normal()
{
  // This test demonstrates the most basic use case.
  let destFile = getTempFile(TEST_FILE_NAME_1);

  // Create the object implementing the output stream.
  let saver = new BackgroundFileSaverOutputStream();

  // Set up callbacks for completion and target file name change.
  let receivedOnTargetChange = false;
  function onTargetChange(aTarget) {
    do_check_true(destFile.equals(aTarget));
    receivedOnTargetChange = true;
  }
  let completionPromise = promiseSaverComplete(saver, onTargetChange);

  // Set the target file.
  saver.setTarget(destFile, false);

  // Write some data and close the output stream.
  yield promiseCopyToSaver(TEST_DATA_SHORT, saver, true);

  // Indicate that we are ready to finish, and wait for a successful callback.
  saver.finish(Cr.NS_OK);
  yield completionPromise;

  // Only after we receive the completion notification, we can also be sure that
  // we've received the target file name change notification before it.
  do_check_true(receivedOnTargetChange);

  // Clean up.
  destFile.remove(false);
});

add_task(function test_combinations()
{
  let initialFile = getTempFile(TEST_FILE_NAME_1);
  let renamedFile = getTempFile(TEST_FILE_NAME_2);

  // Tests various combinations of events and behaviors for both the stream
  // listener and the output stream implementations.
  for (let testFlags = 0; testFlags < 32; testFlags++) {
    let keepPartialOnFailure = !!(testFlags & 1);
    let renameAtSomePoint = !!(testFlags & 2);
    let cancelAtSomePoint = !!(testFlags & 4);
    let useStreamListener = !!(testFlags & 8);
    let useLongData = !!(testFlags & 16);

    let startTime = Date.now();
    do_print("Starting keepPartialOnFailure = " + keepPartialOnFailure +
                    ", renameAtSomePoint = " + renameAtSomePoint +
                    ", cancelAtSomePoint = " + cancelAtSomePoint +
                    ", useStreamListener = " + useStreamListener +
                    ", useLongData = " + useLongData);

    // Keep track of the current file.
    let currentFile = null;
    function onTargetChange(aTarget) {
      do_print("Target file changed to: " + aTarget.leafName);
      currentFile = aTarget;
    }

    // Create the object and register the observers.
    let saver = useStreamListener
                ? new BackgroundFileSaverStreamListener()
                : new BackgroundFileSaverOutputStream();
    let completionPromise = promiseSaverComplete(saver, onTargetChange);

    // Start feeding the first chunk of data to the saver.  In case we are using
    // the stream listener, we only write one chunk.
    let testData = useLongData ? TEST_DATA_LONG : TEST_DATA_SHORT;
    let feedPromise = useStreamListener
                      ? promisePumpToSaver(testData + testData, saver)
                      : promiseCopyToSaver(testData, saver, false);

    // Set a target output file.
    saver.setTarget(initialFile, keepPartialOnFailure);

    // Wait for the first chunk of data to be copied.
    yield feedPromise;

    if (renameAtSomePoint) {
      saver.setTarget(renamedFile, keepPartialOnFailure);
    }

    if (cancelAtSomePoint) {
      saver.finish(Cr.NS_ERROR_FAILURE);
    }

    // Feed the second chunk of data to the saver.
    if (!useStreamListener) {
      yield promiseCopyToSaver(testData, saver, true);
    }

    // Wait for completion, and ensure we succeeded or failed as expected.
    if (!cancelAtSomePoint) {
      saver.finish(Cr.NS_OK);
    }
    try {
      yield completionPromise;
      if (cancelAtSomePoint) {
        do_throw("Failure expected.");
      }
    } catch (ex if cancelAtSomePoint && ex.result == Cr.NS_ERROR_FAILURE) { }

    if (!cancelAtSomePoint) {
      // In this case, the file must exist.
      do_check_true(currentFile.exists());
      if (!cancelAtSomePoint) {
        yield promiseVerifyContents(currentFile, testData + testData);
      }
      currentFile.remove(false);

      // If the target was really renamed, the old file should not exist.
      if (renamedFile.equals(currentFile)) {
        do_check_false(initialFile.exists());
      }
    } else if (!keepPartialOnFailure) {
      // In this case, the file must not exist.
      do_check_false(initialFile.exists());
      do_check_false(renamedFile.exists());
    } else {
      // In this case, the file may or may not exist, because canceling can
      // interrupt the asynchronous operation at any point, even before the file
      // has been created for the first time.
      if (initialFile.exists()) {
        initialFile.remove(false);
      }
      if (renamedFile.exists()) {
        renamedFile.remove(false);
      }
    }

    do_print("Test case completed in " + (Date.now() - startTime) + " ms.");
  }
});

add_task(function test_setTarget_after_close_stream()
{
  // This test checks the case where we close the output stream before we call
  // the setTarget method.  All the data should be buffered and written anyway.
  let destFile = getTempFile(TEST_FILE_NAME_1);

  // Test the case where the file does not already exists first, then the case
  // where the file already exists.
  for (let i = 0; i < 2; i++) {
    let saver = new BackgroundFileSaverOutputStream();
    let completionPromise = promiseSaverComplete(saver);
  
    // Copy some data to the output stream of the file saver.  This data must
    // be shorter than the internal component's pipe buffer for the test to
    // succeed, because otherwise the test would block waiting for the write to
    // complete.
    yield promiseCopyToSaver(TEST_DATA_SHORT, saver, true);
  
    // Set the target file and wait for the output to finish.
    saver.setTarget(destFile, false);
    saver.finish(Cr.NS_OK);
    yield completionPromise;
  
    // Verify results.
    yield promiseVerifyContents(destFile, TEST_DATA_SHORT);
  }

  // Clean up.
  destFile.remove(false);
});

add_task(function test_setTarget_multiple()
{
  // This test checks multiple renames of the target file.
  let destFile = getTempFile(TEST_FILE_NAME_1);
  let saver = new BackgroundFileSaverOutputStream();
  let completionPromise = promiseSaverComplete(saver);

  // Rename both before and after the stream is closed.
  saver.setTarget(getTempFile(TEST_FILE_NAME_2), false);
  saver.setTarget(getTempFile(TEST_FILE_NAME_3), false);
  yield promiseCopyToSaver(TEST_DATA_SHORT, saver, true);
  saver.setTarget(getTempFile(TEST_FILE_NAME_2), false);
  saver.setTarget(destFile, false);

  // Wait for all the operations to complete.
  saver.finish(Cr.NS_OK);
  yield completionPromise;

  // Verify results and clean up.
  do_check_false(getTempFile(TEST_FILE_NAME_2).exists());
  do_check_false(getTempFile(TEST_FILE_NAME_3).exists());
  yield promiseVerifyContents(destFile, TEST_DATA_SHORT);
  destFile.remove(false);
});

add_task(function test_finish_only()
{
  // This test checks creating the object and doing nothing.
  let destFile = getTempFile(TEST_FILE_NAME_1);
  let saver = new BackgroundFileSaverOutputStream();
  function onTargetChange(aTarget) {
    do_throw("Should not receive the onTargetChange notification.");
  }
  let completionPromise = promiseSaverComplete(saver, onTargetChange);
  saver.finish(Cr.NS_OK);
  yield completionPromise;
});

add_task(function test_teardown()
{
  gStillRunning = false;
});
