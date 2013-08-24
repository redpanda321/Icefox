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
 * The Original Code is Mozilla code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *    Ted Mielczarek <ted.mielczarek@gmail.com> (original author)
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

Components.utils.import("resource://gre/modules/Services.jsm");

let EXPORTED_SYMBOLS = [
  "CrashSubmit"
];

const Cc = Components.classes;
const Ci = Components.interfaces;
const STATE_START = Ci.nsIWebProgressListener.STATE_START;
const STATE_STOP = Ci.nsIWebProgressListener.STATE_STOP;

const SUCCESS = "success";
const FAILED  = "failed";
const SUBMITTING = "submitting";

let reportURL = null;
let strings = null;
let myListener = null;

function parseKeyValuePairs(text) {
  var lines = text.split('\n');
  var data = {};
  for (let i = 0; i < lines.length; i++) {
    if (lines[i] == '')
      continue;

    // can't just .split() because the value might contain = characters
    let eq = lines[i].indexOf('=');
    if (eq != -1) {
      let [key, value] = [lines[i].substring(0, eq),
                          lines[i].substring(eq + 1)];
      if (key && value)
        data[key] = value.replace("\\n", "\n", "g").replace("\\\\", "\\", "g");
    }
  }
  return data;
}

function parseKeyValuePairsFromFile(file) {
  var fstream = Cc["@mozilla.org/network/file-input-stream;1"].
                createInstance(Ci.nsIFileInputStream);
  fstream.init(file, -1, 0, 0);
  var is = Cc["@mozilla.org/intl/converter-input-stream;1"].
           createInstance(Ci.nsIConverterInputStream);
  is.init(fstream, "UTF-8", 1024, Ci.nsIConverterInputStream.DEFAULT_REPLACEMENT_CHARACTER);
  var str = {};
  var contents = '';
  while (is.readString(4096, str) != 0) {
    contents += str.value;
  }
  is.close();
  fstream.close();
  return parseKeyValuePairs(contents);
}

function parseINIStrings(file) {
  var factory = Cc["@mozilla.org/xpcom/ini-parser-factory;1"].
                getService(Ci.nsIINIParserFactory);
  var parser = factory.createINIParser(file);
  var obj = {};
  var en = parser.getKeys("Strings");
  while (en.hasMore()) {
    var key = en.getNext();
    obj[key] = parser.getString("Strings", key);
  }
  return obj;
}

// Since we're basically re-implementing part of the crashreporter
// client here, we'll just steal the strings we need from crashreporter.ini
function getL10nStrings() {
  let dirSvc = Cc["@mozilla.org/file/directory_service;1"].
               getService(Ci.nsIProperties);
  let path = dirSvc.get("GreD", Ci.nsIFile);
  path.append("crashreporter.ini");
  if (!path.exists()) {
    // see if we're on a mac
    path = path.parent;
    path.append("crashreporter.app");
    path.append("Contents");
    path.append("MacOS");
    path.append("crashreporter.ini");
    if (!path.exists()) {
      // very bad, but I don't know how to recover
      return;
    }
  }
  let crstrings = parseINIStrings(path);
  strings = {
    'crashid': crstrings.CrashID,
    'reporturl': crstrings.CrashDetailsURL
  };

  path = dirSvc.get("XCurProcD", Ci.nsIFile);
  path.append("crashreporter-override.ini");
  if (path.exists()) {
    crstrings = parseINIStrings(path);
    if ('CrashID' in crstrings)
      strings['crashid'] = crstrings.CrashID;
    if ('CrashDetailsURL' in crstrings)
      strings['reporturl'] = crstrings.CrashDetailsURL;
  }
}

function getPendingMinidump(id) {
  let directoryService = Cc["@mozilla.org/file/directory_service;1"].
                         getService(Ci.nsIProperties);
  let pendingDir = directoryService.get("UAppData", Ci.nsIFile);
  pendingDir.append("Crash Reports");
  pendingDir.append("pending");
  let dump = pendingDir.clone();
  let extra = pendingDir.clone();
  dump.append(id + ".dmp");
  extra.append(id + ".extra");
  return [dump, extra];
}

function addFormEntry(doc, form, name, value) {
  var input = doc.createElement("input");
  input.type = "hidden";
  input.name = name;
  input.value = value;
  form.appendChild(input);
}

function writeSubmittedReport(crashID, viewURL) {
  let directoryService = Cc["@mozilla.org/file/directory_service;1"].
                           getService(Ci.nsIProperties);
  let reportFile = directoryService.get("UAppData", Ci.nsIFile);
  reportFile.append("Crash Reports");
  reportFile.append("submitted");
  if (!reportFile.exists())
    reportFile.create(Ci.nsIFile.DIRECTORY_TYPE, 0700);
  reportFile.append(crashID + ".txt");
  var fstream = Cc["@mozilla.org/network/file-output-stream;1"].
                createInstance(Ci.nsIFileOutputStream);
  // open, write, truncate
  fstream.init(reportFile, -1, -1, 0);
  var os = Cc["@mozilla.org/intl/converter-output-stream;1"].
           createInstance(Ci.nsIConverterOutputStream);
  os.init(fstream, "UTF-8", 0, 0x0000);

  var data = strings.crashid.replace("%s", crashID);
  if (viewURL)
     data += "\n" + strings.reporturl.replace("%s", viewURL);

  os.writeString(data);
  os.close();
  fstream.close();
}

// the Submitter class represents an individual submission.
function Submitter(id, element, submitSuccess, submitError, noThrottle) {
  this.id = id;
  this.element = element;
  this.document = element.ownerDocument;
  this.successCallback = submitSuccess;
  this.errorCallback = submitError;
  this.noThrottle = noThrottle;
}

Submitter.prototype = {
  submitSuccess: function Submitter_submitSuccess(ret)
  {
    if (!ret.CrashID) {
      this.notifyStatus(FAILED);
      this.cleanup();
      return;
    }

    // Write out the details file to submitted/
    writeSubmittedReport(ret.CrashID, ret.ViewURL);

    // Delete from pending dir
    try {
      this.dump.remove(false);
      this.extra.remove(false);
    }
    catch (ex) {
      // report an error? not much the user can do here.
    }

    this.notifyStatus(SUCCESS, ret);
    this.cleanup();
  },

  cleanup: function Submitter_cleanup() {
    // drop some references just to be nice
    this.element = null;
    this.document = null;
    this.successCallback = null;
    this.errorCallback = null;
    this.iframe = null;
    this.dump = null;
    this.extra = null;
    // remove this object from the list of active submissions
    let idx = CrashSubmit._activeSubmissions.indexOf(this);
    if (idx != -1)
      CrashSubmit._activeSubmissions.splice(idx, 1);
  },

  submitForm: function Submitter_submitForm()
  {
    let reportData = parseKeyValuePairsFromFile(this.extra);
    let form = this.iframe.contentDocument.forms[0];
    if ('ServerURL' in reportData) {
      form.action = reportData.ServerURL;
      delete reportData.ServerURL;
    }
    else {
      return false;
    }
    // add the other data
    for (let [name, value] in Iterator(reportData)) {
      addFormEntry(this.iframe.contentDocument, form, name, value);
    }
    if (this.noThrottle) {
      // tell the server not to throttle this, since it was manually submitted
      addFormEntry(this.iframe.contentDocument, form, "Throttleable", "0");
    }
    // add the minidump
    this.iframe.contentDocument.getElementById('minidump').value
      = this.dump.path;
    this.iframe.docShell.QueryInterface(Ci.nsIWebProgress);
    this.iframe.docShell.addProgressListener(this, Ci.nsIWebProgress.NOTIFY_STATE_DOCUMENT);
    form.submit();
    return true;
  },

  // web progress listener
  QueryInterface: function(aIID)
  {
    if (aIID.equals(Ci.nsIWebProgressListener) ||
        aIID.equals(Ci.nsISupportsWeakReference) ||
        aIID.equals(Ci.nsISupports))
      return this;
    throw Components.results.NS_NOINTERFACE;
  },

  onStateChange: function(aWebProgress, aRequest, aFlag, aStatus)
  {
    if(aFlag & STATE_STOP) {
      this.iframe.docShell.QueryInterface(Ci.nsIWebProgress);
      this.iframe.docShell.removeProgressListener(this);

      // check general request status first
      if (!Components.isSuccessCode(aStatus)) {
        this.element.removeChild(this.iframe);
        this.notifyStatus(FAILED);
        this.cleanup();
        return 0;
      }
      // check HTTP status
      if (aRequest instanceof Ci.nsIHttpChannel &&
          aRequest.responseStatus != 200) {
        this.element.removeChild(this.iframe);
        this.notifyStatus(FAILED);
        this.cleanup();
        return 0;
      }

      var ret = parseKeyValuePairs(this.iframe.contentDocument.documentElement.textContent);
      this.element.removeChild(this.iframe);
      this.submitSuccess(ret);
    }
    return 0;
  },

  onLocationChange: function(aProgress, aRequest, aURI) {return 0;},
  onProgressChange: function() {return 0;},
  onStatusChange: function() {return 0;},
  onSecurityChange: function() {return 0;},

  notifyStatus: function Submitter_notify(status, ret)
  {
    let propBag = Cc["@mozilla.org/hash-property-bag;1"].
                  createInstance(Ci.nsIWritablePropertyBag2);
    propBag.setPropertyAsAString("minidumpID", this.id);
    if (status == SUCCESS) {
      propBag.setPropertyAsAString("serverCrashID", ret.CrashID);
    }

    Services.obs.notifyObservers(propBag, "crash-report-status", status);

    switch (status) {
      case SUCCESS:
        if (this.successCallback)
          this.successCallback(this.id, ret);
        break;
      case FAILED:
        if (this.errorCallback)
          this.errorCallback(this.id);
        break;
      default:
        // no callbacks invoked.
    }
  },

  submit: function Submitter_submit()
  {
    let [dump, extra] = getPendingMinidump(this.id);
    if (!dump.exists() || !extra.exists()) {
      this.notifyStatus(FAILED);
      this.cleanup();
      return false;
    }

    this.notifyStatus(SUBMITTING);

    this.dump = dump;
    this.extra = extra;
    let iframe = this.document.createElementNS("http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul", "iframe");
    iframe.setAttribute("type", "content");
    iframe.style.width = 0;
    iframe.style.minWidth = 0;

    let self = this;
    function loadHandler() {
      if (iframe.contentWindow.location == "about:blank")
        return;
      iframe.removeEventListener("load", loadHandler, true);
      if (!self.submitForm()) {
        this.notifyStatus(FAILED);
        self.cleanup();
      }
    }

    iframe.addEventListener("load", loadHandler, true);
    this.element.appendChild(iframe);
    this.iframe = iframe;
    iframe.webNavigation.loadURI("chrome://global/content/crash-submit-form.xhtml", 0, null, null, null);
    return true;
  }
};

//===================================
// External API goes here
let CrashSubmit = {
  /**
   * Submit the crash report named id.dmp from the "pending" directory.
   *
   * @param id
   *        Filename (minus .dmp extension) of the minidump to submit.
   * @param element
   *        A DOM element to which an iframe can be appended as a child,
   *        used for form submission.
   * @param submitSuccess
   *        A function that will be called if the report is submitted
   *        successfully with two parameters: the id that was passed
   *        to this function, and an object containing the key/value
   *        data returned from the server in its properties.
   * @param submitError
   *        A function that will be called with one parameter if the
   *        report fails to submit: the id that was passed to this
   *        function.
   * @param noThrottle
   *        If true, this crash report should be submitted with
   *        an extra parameter of "Throttleable=0" indicating that
   *        it should be processed right away. This should be set
   *        when the report is being submitted and the user expects
   *        to see the results immediately.
   *
   * @return true if the submission began successfully, or false if
   *         it failed for some reason. (If the dump file does not
   *         exist, for example.)
   */
  submit: function CrashSubmit_submit(id, element, submitSuccess, submitError,
                                      noThrottle)
  {
    let submitter = new Submitter(id,
                                  element,
                                  submitSuccess,
                                  submitError,
                                  noThrottle);
    CrashSubmit._activeSubmissions.push(submitter);
    return submitter.submit();
  },

  // List of currently active submit objects
  _activeSubmissions: []
};

// Run this when first loaded
getL10nStrings();
