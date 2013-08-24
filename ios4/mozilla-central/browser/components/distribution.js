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
 * The Original Code is the Firefox Distribution Customizations.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Dan Mills <thunder@mozilla.com>
 *   Marco Bonardo <mak77@bonardo.net>
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

EXPORTED_SYMBOLS = [ "DistributionCustomizer" ];

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cr = Components.results;
const Cu = Components.utils;

const DISTRIBUTION_CUSTOMIZATION_COMPLETE_TOPIC =
  "distribution-customization-complete";

function DistributionCustomizer() {
  let dirSvc = Cc["@mozilla.org/file/directory_service;1"].
               getService(Ci.nsIProperties);
  let iniFile = dirSvc.get("XCurProcD", Ci.nsIFile);
  iniFile.append("distribution");
  iniFile.append("distribution.ini");
  if (iniFile.exists())
    this._iniFile = iniFile;
}

DistributionCustomizer.prototype = {
  _iniFile: null,

  get _ini() {
    let ini = Cc["@mozilla.org/xpcom/ini-parser-factory;1"].
              getService(Ci.nsIINIParserFactory).
              createINIParser(this._iniFile);
    this.__defineGetter__("_ini", function() ini);
    return this._ini;
  },

  get _locale() {
    let locale;
    try {
      locale = this._prefs.getCharPref("general.useragent.locale");
    }
    catch (e) {
      locale = "en-US";
    }
    this.__defineGetter__("_locale", function() locale);
    return this._locale;
  },

  get _bmSvc() {
    let svc = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
              getService(Ci.nsINavBookmarksService);
    this.__defineGetter__("_bmSvc", function() svc);
    return this._bmSvc;
  },

  get _annoSvc() {
    let svc = Cc["@mozilla.org/browser/annotation-service;1"].
              getService(Ci.nsIAnnotationService);
    this.__defineGetter__("_annoSvc", function() svc);
    return this._annoSvc;
  },

  get _livemarkSvc() {
    let svc = Cc["@mozilla.org/browser/livemark-service;2"].
              getService(Ci.nsILivemarkService);
    this.__defineGetter__("_livemarkSvc", function() svc);
    return this._livemarkSvc;
  },

  get _prefSvc() {
    let svc = Cc["@mozilla.org/preferences-service;1"].
              getService(Ci.nsIPrefService);
    this.__defineGetter__("_prefSvc", function() svc);
    return this._prefSvc;
  },

  get _prefs() {
    let branch = this._prefSvc.getBranch(null);
    this.__defineGetter__("_prefs", function() branch);
    return this._prefs;
  },

  get _ioSvc() {
    let svc = Cc["@mozilla.org/network/io-service;1"].
              getService(Ci.nsIIOService);
    this.__defineGetter__("_ioSvc", function() svc);
    return this._ioSvc;
  },

  _makeURI: function DIST__makeURI(spec) {
    return this._ioSvc.newURI(spec, null, null);
  },

  _parseBookmarksSection:
  function DIST_parseBookmarksSection(parentId, section) {
    let keys = [];
    for (let i in enumerate(this._ini.getKeys(section)))
      keys.push(i);
    keys.sort();

    let items = {};
    let defaultItemId = -1;
    let maxItemId = -1;

    for (let i = 0; i < keys.length; i++) {
      let m = /^item\.(\d+)\.(\w+)\.?(\w*)/.exec(keys[i]);
      if (m) {
        let [foo, iid, iprop, ilocale] = m;
        iid = parseInt(iid);

        if (ilocale)
          continue;

        if (!items[iid])
          items[iid] = {};
        if (keys.indexOf(keys[i] + "." + this._locale) >= 0) {
          items[iid][iprop] = this._ini.getString(section, keys[i] + "." +
                                                  this._locale);
        } else {
          items[iid][iprop] = this._ini.getString(section, keys[i]);
        }

        if (iprop == "type" && items[iid]["type"] == "default")
          defaultItemId = iid;

        if (maxItemId < iid)
          maxItemId = iid;
      } else {
        dump("Key did not match: " + keys[i] + "\n");
      }
    }

    let prependIndex = 0;
    for (let iid = 0; iid <= maxItemId; iid++) {
      if (!items[iid])
        continue;

      let index = this._bmSvc.DEFAULT_INDEX;
      let newId;

      switch (items[iid]["type"]) {
      case "default":
        break;

      case "folder":
        if (iid < defaultItemId)
          index = prependIndex++;

        newId = this._bmSvc.createFolder(parentId, items[iid]["title"], index);

        this._parseBookmarksSection(newId, "BookmarksFolder-" +
                                    items[iid]["folderId"]);

        if (items[iid]["description"])
          this._annoSvc.setItemAnnotation(newId,
                                          "bookmarkProperties/description",
                                          items[iid]["description"], 0,
                                          this._annoSvc.EXPIRE_NEVER);

        break;

      case "separator":
        if (iid < defaultItemId)
          index = prependIndex++;
        this._bmSvc.insertSeparator(parentId, index);
        break;

      case "livemark":
        if (iid < defaultItemId)
          index = prependIndex++;

        // Don't bother updating the livemark contents on creation.
        newId = this._livemarkSvc.
          createLivemarkFolderOnly(parentId,
                                   items[iid]["title"],
                                   this._makeURI(items[iid]["siteLink"]),
                                   this._makeURI(items[iid]["feedLink"]),
                                   index);
        break;

      case "bookmark":
      default:
        if (iid < defaultItemId)
          index = prependIndex++;

        newId = this._bmSvc.insertBookmark(parentId,
                                           this._makeURI(items[iid]["link"]),
                                           index, items[iid]["title"]);

        if (items[iid]["description"])
          this._annoSvc.setItemAnnotation(newId,
                                          "bookmarkProperties/description",
                                          items[iid]["description"], 0,
                                          this._annoSvc.EXPIRE_NEVER);

        break;
      }
    }
  },

  _customizationsApplied: false,
  applyCustomizations: function DIST_applyCustomizations() {
    this._customizationsApplied = true;
    if (!this._iniFile)
      return this._checkCustomizationComplete();

    // nsPrefService loads very early.  Reload prefs so we can set
    // distribution defaults during the prefservice:after-app-defaults
    // notification (see applyPrefDefaults below)
    this._prefSvc.QueryInterface(Ci.nsIObserver);
    this._prefSvc.observe(null, "reload-default-prefs", null);
  },

  _bookmarksApplied: false,
  applyBookmarks: function DIST_applyBookmarks() {
    this._bookmarksApplied = true;
    if (!this._iniFile)
      return this._checkCustomizationComplete();

    let sections = enumToObject(this._ini.getSections());

    // The global section, and several of its fields, is required
    // (we also check here to be consistent with applyPrefDefaults below)
    if (!sections["Global"])
      return this._checkCustomizationComplete();
    let globalPrefs = enumToObject(this._ini.getKeys("Global"));
    if (!(globalPrefs["id"] && globalPrefs["version"] && globalPrefs["about"]))
      return this._checkCustomizationComplete();

    let bmProcessedPref;
    try {
      bmProcessedPref = this._ini.getString("Global",
                                            "bookmarks.initialized.pref");
    }
    catch (e) {
      bmProcessedPref = "distribution." +
        this._ini.getString("Global", "id") + ".bookmarksProcessed";
    }

    let bmProcessed = false;
    try {
      bmProcessed = this._prefs.getBoolPref(bmProcessedPref);
    }
    catch (e) {}

    if (!bmProcessed) {
      if (sections["BookmarksMenu"])
        this._parseBookmarksSection(this._bmSvc.bookmarksMenuFolder,
                                    "BookmarksMenu");
      if (sections["BookmarksToolbar"])
        this._parseBookmarksSection(this._bmSvc.toolbarFolder,
                                    "BookmarksToolbar");
      this._prefs.setBoolPref(bmProcessedPref, true);
    }
    return this._checkCustomizationComplete();
  },

  _prefDefaultsApplied: false,
  applyPrefDefaults: function DIST_applyPrefDefaults() {
    this._prefDefaultsApplied = true;
    if (!this._iniFile)
      return this._checkCustomizationComplete();

    let sections = enumToObject(this._ini.getSections());

    // The global section, and several of its fields, is required
    if (!sections["Global"])
      return this._checkCustomizationComplete();
    let globalPrefs = enumToObject(this._ini.getKeys("Global"));
    if (!(globalPrefs["id"] && globalPrefs["version"] && globalPrefs["about"]))
      return this._checkCustomizationComplete();

    let defaults = this._prefSvc.getDefaultBranch(null);

    // Global really contains info we set as prefs.  They're only
    // separate because they are "special" (read: required)

    defaults.setCharPref("distribution.id", this._ini.getString("Global", "id"));
    defaults.setCharPref("distribution.version",
                         this._ini.getString("Global", "version"));

    let partnerAbout = Cc["@mozilla.org/supports-string;1"].
      createInstance(Ci.nsISupportsString);
    if (globalPrefs["about." + this._locale]) {
      partnerAbout.data = this._ini.getString("Global", "about." + this._locale);
    } else {
      partnerAbout.data = this._ini.getString("Global", "about");
    }
    defaults.setComplexValue("distribution.about",
                             Ci.nsISupportsString, partnerAbout);

    if (sections["Preferences"]) {
      for (let key in enumerate(this._ini.getKeys("Preferences"))) {
        try {
          let value = eval(this._ini.getString("Preferences", key));
          switch (typeof value) {
          case "boolean":
            defaults.setBoolPref(key, value);
            break;
          case "number":
            defaults.setIntPref(key, value);
            break;
          case "string":
            defaults.setCharPref(key, value);
            break;
          case "undefined":
            defaults.setCharPref(key, value);
            break;
          }
        } catch (e) { /* ignore bad prefs and move on */ }
      }
    }

    // We eval() the localizable prefs as well (even though they'll
    // always get set as a string) to keep the INI format consistent:
    // string prefs always need to be in quotes

    let localizedStr = Cc["@mozilla.org/pref-localizedstring;1"].
      createInstance(Ci.nsIPrefLocalizedString);

    if (sections["LocalizablePreferences"]) {
      for (let key in enumerate(this._ini.getKeys("LocalizablePreferences"))) {
        try {
          let value = eval(this._ini.getString("LocalizablePreferences", key));
          value = value.replace("%LOCALE%", this._locale, "g");
          localizedStr.data = "data:text/plain," + key + "=" + value;
          defaults.setComplexValue(key, Ci.nsIPrefLocalizedString, localizedStr);
        } catch (e) { /* ignore bad prefs and move on */ }
      }
    }

    if (sections["LocalizablePreferences-" + this._locale]) {
      for (let key in enumerate(this._ini.getKeys("LocalizablePreferences-" + this._locale))) {
        try {
          let value = eval(this._ini.getString("LocalizablePreferences-" + this._locale, key));
          localizedStr.data = "data:text/plain," + key + "=" + value;
          defaults.setComplexValue(key, Ci.nsIPrefLocalizedString, localizedStr);
        } catch (e) { /* ignore bad prefs and move on */ }
      }
    }

    return this._checkCustomizationComplete();
  },

  _checkCustomizationComplete: function DIST__checkCustomizationComplete() {
    let prefDefaultsApplied = this._prefDefaultsApplied || !this._iniFile;
    if (this._customizationsApplied && this._bookmarksApplied &&
        prefDefaultsApplied) {
      let os = Cc["@mozilla.org/observer-service;1"].
               getService(Ci.nsIObserverService);
      os.notifyObservers(null, DISTRIBUTION_CUSTOMIZATION_COMPLETE_TOPIC, null);
    }
  }
};

function enumerate(UTF8Enumerator) {
  while (UTF8Enumerator.hasMore())
    yield UTF8Enumerator.getNext();
}

function enumToObject(UTF8Enumerator) {
  let ret = {};
  for (let i in enumerate(UTF8Enumerator))
    ret[i] = 1;
  return ret;
}
