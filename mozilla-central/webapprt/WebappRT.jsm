/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

this.EXPORTED_SYMBOLS = ["WebappRT"];

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyGetter(this, "FileUtils", function() {
  Cu.import("resource://gre/modules/FileUtils.jsm");
  return FileUtils;
});

XPCOMUtils.defineLazyGetter(this, "DOMApplicationRegistry", function() {
  Cu.import("resource://gre/modules/Webapps.jsm");
  return DOMApplicationRegistry;
});

this.WebappRT = {
  _config: null,

  get config() {
    if (this._config)
      return this._config;

    let config;
    let webappFile = FileUtils.getFile("AppRegD", ["webapp.json"]);

    let inputStream = Cc["@mozilla.org/network/file-input-stream;1"].
                      createInstance(Ci.nsIFileInputStream);
    inputStream.init(webappFile, -1, 0, 0);
    let json = Cc["@mozilla.org/dom/json;1"].createInstance(Ci.nsIJSON);
    config = json.decodeFromStream(inputStream, webappFile.fileSize);

    return this._config = config;
  },

  // This exists to support test mode, which installs webapps after startup.
  // Ideally we wouldn't have to have a setter, as tests can just delete
  // the getter and then set the property.  But the object to which they set it
  // will have a reference to its global object, so our reference to it
  // will leak that object (per bug 780674).  The setter enables us to clone
  // the new value so we don't actually retain a reference to it.
  set config(newVal) {
    this._config = JSON.parse(JSON.stringify(newVal));
  },

  get launchURI() {
    let url = Services.io.newURI(this.config.app.origin, null, null);
    if (this.config.app.manifest.launch_path) {
      url = Services.io.newURI(this.config.app.manifest.launch_path, null, url);
    }
    return url;
  }
};
