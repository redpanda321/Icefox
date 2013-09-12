/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

const Ci = Components.interfaces;

function do_check_true(cond, text) {
  // we don't have the test harness' utilities in this scope, so we need this
  // little helper. In the failure case, the exception is propagated to the
  // caller in the main run_test() function, and the test fails.
  if (!cond)
    throw "Failed check: " + text;
}

function FileComponent() {
  this.wrappedJSObject = this;
}
FileComponent.prototype =
{
  doTest: function() {
    // throw if anything goes wrong

    // find the current directory path
    var file = Components.classes["@mozilla.org/file/directory_service;1"]
               .getService(Ci.nsIProperties)
               .get("CurWorkD", Ci.nsIFile);
    file.append("xpcshell.ini");

    // should be able to construct a file
    var f1 = File(file.path);
    // with either constructor syntax
    var f2 = new File(file.path);
    // and with nsIFiles
    var f3 = File(file);
    var f4 = new File(file);

    // do some tests
    do_check_true(f1 instanceof Ci.nsIDOMFile, "Should be a DOM File");
    do_check_true(f2 instanceof Ci.nsIDOMFile, "Should be a DOM File");
    do_check_true(f3 instanceof Ci.nsIDOMFile, "Should be a DOM File");
    do_check_true(f4 instanceof Ci.nsIDOMFile, "Should be a DOM File");

    do_check_true(f1.name == "xpcshell.ini", "Should be the right file");
    do_check_true(f2.name == "xpcshell.ini", "Should be the right file");
    do_check_true(f3.name == "xpcshell.ini", "Should be the right file");
    do_check_true(f4.name == "xpcshell.ini", "Should be the right file");

    do_check_true(f1.type = "text/plain", "Should be the right type");
    do_check_true(f2.type = "text/plain", "Should be the right type");
    do_check_true(f3.type = "text/plain", "Should be the right type");
    do_check_true(f4.type = "text/plain", "Should be the right type");

    var threw = false;
    try {
      // Needs a ctor argument
      var f7 = File();
    } catch (e) {
      threw = true;
    }
    do_check_true(threw, "No ctor arguments should throw");

    var threw = false;
    try {
      // Needs a valid ctor argument
      var f7 = File(Date(132131532));
    } catch (e) {
      threw = true;
    }
    do_check_true(threw, "Passing a random object should fail");

    var threw = false
    try {
      // Directories fail
      var dir = Components.classes["@mozilla.org/file/directory_service;1"]
                          .getService(Ci.nsIProperties)
                          .get("CurWorkD", Ci.nsIFile);
      var f7 = File(dir)
    } catch (e) {
      threw = true;
    }
    do_check_true(threw, "Can't create a File object for a directory");

    return true;
  },

  // nsIClassInfo + information for XPCOM registration code in XPCOMUtils.jsm
  classDescription: "File in components scope code",
  classID: Components.ID("{da332370-91d4-464f-a730-018e14769cab}"),
  contractID: "@mozilla.org/tests/component-file;1",

  // nsIClassInfo
  implementationLanguage: Components.interfaces.nsIProgrammingLanguage.JAVASCRIPT,
  flags: 0,

  getInterfaces: function getInterfaces(aCount) {
    var interfaces = [Components.interfaces.nsIClassInfo];
    aCount.value = interfaces.length;
    return interfaces;
  },

  getHelperForLanguage: function getHelperForLanguage(aLanguage) {
    return null;
  },

  // nsISupports
  QueryInterface: XPCOMUtils.generateQI([Components.interfaces.nsIClassInfo])
};

var gComponentsArray = [FileComponent];
this.NSGetFactory = XPCOMUtils.generateNSGetFactory(gComponentsArray);
