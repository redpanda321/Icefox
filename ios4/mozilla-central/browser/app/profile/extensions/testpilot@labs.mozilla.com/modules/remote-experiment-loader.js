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
 * The Original Code is Test Pilot.
 *
 * The Initial Developer of the Original Code is Mozilla.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Jono X <jono@mozilla.com>
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

const BASE_URL_PREF = "extensions.testpilot.indexBaseURL";
var Cuddlefish = require("cuddlefish");
var resolveUrl = require("url").resolve;
var SecurableModule = require("securable-module");
let JarStore = require("jar-code-store").JarStore;

/* Security info should look like this:
 * Security Info:
	Security state: secure
	Security description: Authenticated by Equifax
	Security error message: null

Certificate Status:
	Verification: OK
	Common name (CN) = *.mozillalabs.com
	Organisation = Mozilla Corporation
	Issuer = Equifax
	SHA1 fingerprint = E5:CD:91:97:08:E6:88:F2:A2:AE:31:3C:F9:91:8D:14:33:07:C4:EE
	Valid from 8/12/09 14:04:39
	Valid until 8/14/11 3:27:26
 */

function verifyChannelSecurity(channel) {
  // http://mdn.beonex.com/En/How_to_check_the_security_state_of_an_XMLHTTPRequest_over_SSL
  // Expect channel to have security state = secure, CN = *.mozillalabs.com,
  // Organization = "Mozilla Corporation", verification = OK.
  console.info("Verifying SSL channel security info before download...");

  try {
    if (! channel instanceof  Ci.nsIChannel) {
      console.warn("Not a channel.  This should never happen.");
      return false;
    }
    let secInfo = channel.securityInfo;

    if (secInfo instanceof Ci.nsITransportSecurityInfo) {
      secInfo.QueryInterface(Ci.nsITransportSecurityInfo);
      let secState = secInfo.securityState & Ci.nsIWebProgressListener.STATE_IS_SECURE;
      if (secState != Ci.nsIWebProgressListener.STATE_IS_SECURE) {
        console.warn("Failing security check: Security state is not secure.");
        return false;
      }
    } else {
      console.warn("Failing secuity check: No TransportSecurityInfo.");
      return false;
    }

    // check SSL certificate details
    if (secInfo instanceof Ci.nsISSLStatusProvider) {
      let cert = secInfo.QueryInterface(Ci.nsISSLStatusProvider).
	SSLStatus.QueryInterface(Ci.nsISSLStatus).serverCert;

      let verificationResult = cert.verifyForUsage(
        Ci.nsIX509Cert.CERT_USAGE_SSLServer);
      if (verificationResult != Ci.nsIX509Cert.VERIFIED_OK) {
        console.warn("Failing security check: Cert not verified OK.");
        return false;
      }
      if (cert.commonName != "*.mozillalabs.com") {
        console.warn("Failing security check: Cert not for *.mozillalabs.com");
        return false;
      }
      if (cert.organization != "Mozilla Corporation") {
        console.warn("Failing security check: Cert not for Mozilla corporation.");
        return false;
      }
    } else {
      console.warn("Failing security check: No SSL cert info.");
      return false;
    }

    // Passed everything
    console.info("Channel passed SSL security check.");
    return true;
  } catch(err) {
    console.warn("Failing security check:  Error: " + err);
    return false;
  }
}

function downloadFile(url, cb, lastModified) {
  // lastModified is a timestamp (ms since epoch); if provided, then the file
  // will not be downloaded unless it is newer than this.
  var req = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"]
              .createInstance( Ci.nsIXMLHttpRequest );
  req.open('GET', url, true);
  if (lastModified != undefined) {
    let d = new Date();
    d.setTime(lastModified);
    // example header: If-Modified-Since: Sat, 29 Oct 1994 19:43:31 GMT
    req.setRequestHeader("If-Modified-Since", d.toGMTString());
    console.info("Setting if-modified-since header to " + d.toGMTString());
  }
  //Use binary mode to download jars TODO find a better switch
  if (url.indexOf(".jar") == url.length - 4) {
    console.info("Using binary mode to download jar file.");
    req.overrideMimeType('text/plain; charset=x-user-defined');
  }
  req.onreadystatechange = function(aEvt) {
    if (req.readyState == 4) {
      if (req.status == 200) {
        // check security channel:
        if (verifyChannelSecurity(req.channel)) {
          cb(req.responseText);
        } else {
          cb(null);
        }
      } else if (req.status == 304) {
        // 304 is "Not Modified", which we can get because we send an
        // If-Modified-Since header.
        console.info("File " + url + " not modified; using cached version.");
        cb(null);
        // calling back with null lets the RemoteExperimentLoader know it should
        // keep using the old cached version of the code.
      } else {
        // Some kind of error.
        console.warn("Got a " + req.status + " error code downloading " + url);
	cb(null);
      }
    }
  };
  req.send();
}

// example contents of extensions.testpilot.experiment.codeFs:
// {'fs': {"bookmark01/experiment": "<plain-text code @ bookmarks.js>"}}
// sample code
    // example data:
    // {'experiments': [{'name': 'Bookmark Experiment',
    //                           'filename': 'bookmarks.js'}]}

exports.RemoteExperimentLoader = function(logRepo, fileGetterFunction ) {
  /* fileGetterFunction is an optional stub function for unit testing.  Pass in
   * nothing to have it use the default behavior of downloading the files from the
   * Test Pilot server.  FileGetterFunction must take (url, callback).*/
  this._init(logRepo, fileGetterFunction);
};

exports.RemoteExperimentLoader.prototype = {
  _init: function(logRepo, fileGetterFunction) {
    this._logger = logRepo.getLogger("TestPilot.Loader");
    this._expLogger = logRepo.getLogger("TestPilot.RemoteCode");
    this._studyResults = [];
    this._legacyStudies = [];
    let prefs = require("preferences-service");
    this._baseUrl = prefs.get(BASE_URL_PREF, "");
    if (fileGetterFunction != undefined) {
      this._fileGetter = fileGetterFunction;
    } else {
      this._fileGetter = downloadFile;
    }
    this._logger.trace("About to instantiate preferences store.");
    this._jarStore = new JarStore();
    this._experimentFileNames = [];
    let self = this;
    this._logger.trace("About to instantiate cuddlefish loader.");
    this._refreshLoader();
    // set up the unloading
    require("unload").when( function() {
                              self._loader.unload();
                            });
    this._logger.trace("Done instantiating remoteExperimentLoader.");
  },

  _refreshLoader: function() {
    if (this._loader) {
      this._loader.unload();
    }
    /* Pass in "TestPilot.experiment" logger as the console object for
     * all remote modules loaded through cuddlefish, so they will log their
     * stuff to the same file as all other modules.  This logger is not
     * technically a console object but as long as it has .debug, .info,
     * .warn, and .error methods, it will work fine.*/

    /* Use a composite file system here, compositing codeStorage and a new
     * local file system so that securable modules loaded remotely can
     * themselves require modules in the cuddlefish lib. */
    let self = this;
    this._loader = Cuddlefish.Loader(
      {fs: new SecurableModule.CompositeFileSystem(
         [self._jarStore, Cuddlefish.parentLoader.fs]),
       console: this._expLogger
      });
  },

  getLocalizedStudyInfo: function(studiesIndex) {
    let prefs = require("preferences-service");
    let myLocale = prefs.get("general.useragent.locale", "");
    let studiesToLoad = [];
    for each (let set in studiesIndex) {
      // First try for specific locale, e.g. pt-BR for brazillian portugese
      if (set[myLocale]) {
        studiesToLoad.push(set[myLocale]);
        continue;
      }
      // If that's not there, look for the language, e.g. just "pt":
      let hyphen = myLocale.indexOf("-");
      if (hyphen > -1) {
        let lang = myLocale.slice(0, hyphen);
        if (set[lang]) {
          studiesToLoad.push(set[lang]);
          continue;
        }
      }
      // If that's not there either, look for one called "default":
      if(set["default"]) {
        studiesToLoad.push(set["default"]);
      }
      // If none of those are there, load nothing.
    }
    return studiesToLoad;
  },

  _executeFreshIndexFile: function(data, callback) {
    try {
      data = JSON.parse(data);
    } catch (e) {
      this._logger.warn("Error parsing index.json: " + e );
      callback(false);
      return;
    }

    // Cache study results and legacy studies.
    this._studyResults = data.results;
    this._legacyStudies = data.legacy;

    /* Go through each record indicated in index.json for our locale;
     * download the specified .jar file (replacing any version on disk)
     */
    let jarFiles = this.getLocalizedStudyInfo(data.new_experiments);
    let numFilesToDload = jarFiles.length;
    let self = this;

    for each (let j in jarFiles) {
      let filename = j.jarfile;
      let hash = j.hash;
      if (j.studyfile) {
        this._experimentFileNames.push(j.studyfile);
      }
      this._logger.trace("I'm gonna go try to get the code for " + filename);
      let modDate = this._jarStore.getFileModifiedDate(filename);

      this._fileGetter(resolveUrl(this._baseUrl, filename),
      function onDone(code) {
        // code will be non-null if there is actually new code to download.
        if (code) {
          self._logger.info("Downloaded jar file " + filename);
          self._jarStore.saveJarFile(filename, code, hash);
          self._logger.trace("Saved code for: " + filename);
        } else {
          self._logger.info("Nothing to download for " + filename);
        }
        numFilesToDload--;
        if (numFilesToDload == 0) {
          self._logger.trace("Calling callback.");
          callback(true);
        }
      }, modDate);
    }
  },

  _executeCachedIndexFile: function(data) {
    /* Working with a cached index file = follow its instructions except
     * don't try to download anything - just work with the jar files already
     * on disk. There's a lot of shared code between this and _executeFreshIndexFile;
     * refactor?*/
    try {
      data = JSON.parse(data);
    } catch (e) {
      this._logger.warn("Error parsing index.json: " + e );
      return false;
    }
    // Read study results and legacy studies from index.
    this._studyResults = data.results;
    this._legacyStudies = data.legacy;

    // Read names of experiment modules from index.
    let jarFiles = this.getLocalizedStudyInfo(data.new_experiments);
    for each (let j in jarFiles) {
      let filename = j.jarfile;
      let hash = j.hash;
      if (j.studyfile) {
        this._experimentFileNames.push(j.studyfile);
      }
    }
    return true;
  },

  // TODO a bad thing that can go wrong: If we have a net connection but the index file
  // has not changed, we currently don't try to download anything...

  // Another bad thing: If there's a jar download that's corrupt or unreadable or has
    // the wrong permissions or something, we need to kill it and download a new one.

  // WTF every jar file I'm downloading appears as 0 bytes with __x__x___ permissions!

  _cachedIndexNsiFile: null,
  get cachedIndexNsiFile() {
    if (!this._cachedIndexNsiFile) {
      try {
        let file = Cc["@mozilla.org/file/directory_service;1"].
                         getService(Ci.nsIProperties).
                         get("ProfD", Ci.nsIFile);
        file.append("TestPilotExperimentFiles"); // TODO this name should go in pref?
        // Make sure there's a directory with this name; delete any non-directory
        // file that's in the way.
        if (file.exists() && !file.isDirectory()) {
          file.remove(false);
        }
        if (!file.exists()) {
          file.create(Ci.nsIFile.DIRECTORY_TYPE, 0777);
        }
        file.append("index.json");
        this._cachedIndexNsiFile = file;
      } catch(e) {
        console.warn("Error creating directory for cached index file: " + e);
      }
    }
    return this._cachedIndexNsiFile;
  },

  _cacheIndexFile: function(data) {
    // write data to disk as basedir/index.json
    try {
      let file = this.cachedIndexNsiFile;
      if (file == null) {
        console.warn("Can't cache index file because directory does not exist.");
        return;
      }
      if (file.exists()) {
        file.remove(false);
      }
      file.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0666);
      // file is nsIFile, data is a string
      let foStream = Cc["@mozilla.org/network/file-output-stream;1"].
                               createInstance(Ci.nsIFileOutputStream);

      foStream.init(file, 0x02 | 0x08 | 0x20, 0666, 0);
      // write, create, truncate
      let converter = Cc["@mozilla.org/intl/converter-output-stream;1"].
                                createInstance(Ci.nsIConverterOutputStream);
      converter.init(foStream, "UTF-8", 0, 0);
      converter.writeString(data);
      converter.close(); // this closes foStream too
    } catch(e) {
      console.warn("Error cacheing index file: " + e);
    }
  },

  // https://developer.mozilla.org/en/Table_Of_Errors
  _loadCachedIndexFile: function() {
    // If basedir/index.json exists, read it and return its data
    // Otherwise, return false
    let file = this.cachedIndexNsiFile;
    if (file == null) {
      console.warn("Can't load cached index file because directory does not exist.");
      return false;
    }
    if (file.exists()) {
      try {
        let data = "";
        let fstream = Cc["@mozilla.org/network/file-input-stream;1"].
                          createInstance(Ci.nsIFileInputStream);
        let cstream = Cc["@mozilla.org/intl/converter-input-stream;1"].
                          createInstance(Ci.nsIConverterInputStream);
        fstream.init(file, -1, 0, 0);
        cstream.init(fstream, "UTF-8", 0, 0);
        let str = {};
        while (cstream.readString(4096, str) != 0) {
          data += str.value;
        }
        cstream.close(); // this closes fstream too
        return data;
      } catch(e) {
        console.warn("Error occured in reading cached index file: " + e);
        return false;
      }
    } else {
      console.warn("Trying to load cached index file but it does not exist.");
      return false;
    }
  },

  checkForUpdates: function(callback) {
    // Check for surveys and studies.  Entry point for all download and execution of
    // remote code.
    /* Callback will be called with true or false
     * to let us know whether there are any updates, so that client code can
     * restart any experiment whose code has changed. */
    let prefs = require("preferences-service");
    let indexFileName = prefs.get("extensions.testpilot.indexFileName",
                                  "index.json");
    let self = this;
    // Unload everything before checking for updates, to be sure we
    // get the newest stuff.
    this._logger.info("Unloading everything to prepare to check for updates.");
    this._refreshLoader();

    let modDate = 0;
    if (this.cachedIndexNsiFile) {
      if (this.cachedIndexNsiFile.exists()) {
        modDate = this.cachedIndexNsiFile.lastModifiedTime;
      }
    }
    let url = resolveUrl(self._baseUrl, indexFileName);
    self._fileGetter(url, function onDone(data) {
      if (data) {
        self._executeFreshIndexFile(data, callback);
        // cache index file contents so we can read them later if we can't get online.
        self._cacheIndexFile(data);
      } else {
        self._logger.info("Could not download index.json, using cached version.");
        let data = self._loadCachedIndexFile();
        if (data) {
          let success = self._executeCachedIndexFile(data);
          callback(success);
        } else {
          self._logger.warn("Could not download index.json and no cached version.");
          // TODO Should display an error message to the user in this case.
          callback(false);
        }
      }
    }, modDate);
  },

  getExperiments: function() {
    /* Load up and return all studies/surveys (not libraries)
     * already stored in codeStorage.  Returns a dict with key =
     * the module name and value = the module object. */
    this._logger.trace("GetExperiments called.");
    let remoteExperiments = {};
    for each (filename in this._experimentFileNames) {
      this._logger.debug("GetExperiments is loading " + filename);
      try {
        remoteExperiments[filename] = this._loader.require(filename);
        this._logger.info("Loaded " + filename + " OK.");
      } catch(e) {
        this._logger.warn("Error loading " + filename);
        this._logger.warn(e);
      }
    }
    return remoteExperiments;
  },

  getStudyResults: function() {
    return this._studyResults;
  },

  getLegacyStudies: function() {
    return this._legacyStudies;
  }
};

// TODO purge the pref store of anybody who has one.

// TODO Also, if user has a study id foo that is not expired yet, and
// a LegacyStudy appears with the same id, they should keep their "real"
// version of id foo and not load the LegacyStudy version.

// TODO but once the study is expired, should delete the jar for it and
// just load the LegacyStudy version.

