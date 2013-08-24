/*
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is the Extension Manager.
#
# The Initial Developer of the Original Code is mozilla.org
# Portions created by the Initial Developer are Copyright (C) 2008
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Dave Townsend <dtownsend@oxymoronical.com>
#   Ben Parr <bparr@bparr.com>
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****
*/

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Components.utils.import("resource://gre/modules/FileUtils.jsm");
Components.utils.import("resource://gre/modules/NetUtil.jsm");
Components.utils.import("resource://gre/modules/Services.jsm");
Components.utils.import("resource://gre/modules/AddonManager.jsm");

var EXPORTED_SYMBOLS = [ "AddonRepository" ];

const PREF_GETADDONS_CACHE_ENABLED       = "extensions.getAddons.cache.enabled";
const PREF_GETADDONS_BROWSEADDONS        = "extensions.getAddons.browseAddons";
const PREF_GETADDONS_BYIDS               = "extensions.getAddons.get.url";
const PREF_GETADDONS_BROWSERECOMMENDED   = "extensions.getAddons.recommended.browseURL";
const PREF_GETADDONS_GETRECOMMENDED      = "extensions.getAddons.recommended.url";
const PREF_GETADDONS_BROWSESEARCHRESULTS = "extensions.getAddons.search.browseURL";
const PREF_GETADDONS_GETSEARCHRESULTS    = "extensions.getAddons.search.url";

const XMLURI_PARSE_ERROR  = "http://www.mozilla.org/newlayout/xml/parsererror.xml";

const API_VERSION = "1.5";

const KEY_PROFILEDIR = "ProfD";
const FILE_DATABASE  = "addons.sqlite";
const DB_SCHEMA      = 1;

["LOG", "WARN", "ERROR"].forEach(function(aName) {
  this.__defineGetter__(aName, function() {
    Components.utils.import("resource://gre/modules/AddonLogging.jsm");

    LogManager.getLogger("addons.repository", this);
    return this[aName];
  });
}, this);


// Add-on properties parsed out of AMO results
// Note: the 'install' property is added for results from
// retrieveRecommendedAddons and searchAddons
const PROP_SINGLE = ["id", "type", "name", "version", "creator", "description",
                     "fullDescription", "developerComments", "eula", "iconURL",
                     "homepageURL", "supportURL", "contributionURL",
                     "contributionAmount", "averageRating", "reviewCount",
                     "reviewURL", "totalDownloads", "weeklyDownloads",
                     "dailyUsers", "sourceURI", "repositoryStatus", "size",
                     "updateDate"];
const PROP_MULTI = ["developers", "screenshots"]

// A map between XML keys to AddonSearchResult keys for string values
// that require no extra parsing from XML
const STRING_KEY_MAP = {
  name:               "name",
  version:            "version",
  summary:            "description",
  description:        "fullDescription",
  developer_comments: "developerComments",
  eula:               "eula",
  icon:               "iconURL",
  homepage:           "homepageURL",
  support:            "supportURL"
};

// A map between XML keys to AddonSearchResult keys for integer values
// that require no extra parsing from XML
const INTEGER_KEY_MAP = {
  total_downloads:  "totalDownloads",
  weekly_downloads: "weeklyDownloads",
  daily_users:      "dailyUsers"
};


function AddonSearchResult(aId) {
  this.id = aId;
}

AddonSearchResult.prototype = {
  /**
   * The ID of the add-on
   */
  id: null,

  /**
   * The add-on type (e.g. "extension" or "theme")
   */
  type: null,

  /**
   * The name of the add-on
   */
  name: null,

  /**
   * The version of the add-on
   */
  version: null,

  /**
   * The creator of the add-on
   */
  creator: null,

  /**
   * The developers of the add-on
   */
  developers: null,

  /**
   * A short description of the add-on
   */
  description: null,

  /**
   * The full description of the add-on
   */
  fullDescription: null,

  /**
   * The developer comments for the add-on. This includes any information
   * that may be helpful to end users that isn't necessarily applicable to
   * the add-on description (e.g. known major bugs)
   */
  developerComments: null,

  /**
   * The end-user licensing agreement (EULA) of the add-on
   */
  eula: null,

  /**
   * The url of the add-on's icon
   */
  iconURL: null,

  /**
   * An array of screenshot urls for the add-on
   */
  screenshots: null,

  /**
   * The homepage for the add-on
   */
  homepageURL: null,

  /**
   * The support URL for the add-on
   */
  supportURL: null,

  /**
   * The contribution url of the add-on
   */
  contributionURL: null,

  /**
   * The suggested contribution amount
   */
  contributionAmount: null,

  /**
   * The rating of the add-on, 0-5
   */
  averageRating: null,

  /**
   * The number of reviews for this add-on
   */
  reviewCount: null,

  /**
   * The URL to the list of reviews for this add-on
   */
  reviewURL: null,

  /**
   * The total number of times the add-on was downloaded
   */
  totalDownloads: null,

  /**
   * The number of times the add-on was downloaded the current week
   */
  weeklyDownloads: null,

  /**
   * The number of daily users for the add-on
   */
  dailyUsers: null,

  /**
   * AddonInstall object generated from the add-on XPI url
   */
  install: null,

  /**
   * nsIURI storing where this add-on was installed from
   */
  sourceURI: null,

  /**
   * The status of the add-on in the repository (e.g. 4 = "Public")
   */
  repositoryStatus: null,

  /**
   * The size of the add-on's files in bytes. For an add-on that have not yet
   * been downloaded this may be an estimated value.
   */
  size: null,

  /**
   * The Date that the add-on was most recently updated
   */
  updateDate: null,

  /**
   * True or false depending on whether the add-on is compatible with the
   * current version and platform of the application
   */
  isCompatible: true,

  /**
   * True if the add-on has a secure means of updating
   */
  providesUpdatesSecurely: true,

  /**
   * The current blocklist state of the add-on
   */
  blocklistState: Ci.nsIBlocklistService.STATE_NOT_BLOCKED,

  /**
   * True if this add-on cannot be used in the application based on version
   * compatibility, dependencies and blocklisting
   */
  appDisabled: false,

  /**
   * True if the user wants this add-on to be disabled
   */
  userDisabled: false,

  /**
   * Indicates what scope the add-on is installed in, per profile, user,
   * system or application
   */
  scope: AddonManager.SCOPE_PROFILE,

  /**
   * True if the add-on is currently functional
   */
  isActive: true,

  /**
   * A bitfield holding all of the current operations that are waiting to be
   * performed for this add-on
   */
  pendingOperations: AddonManager.PENDING_NONE,

  /**
   * A bitfield holding all the the operations that can be performed on
   * this add-on
   */
  permissions: 0,

  /**
   * Tests whether this add-on is known to be compatible with a
   * particular application and platform version.
   *
   * @param  appVersion
   *         An application version to test against
   * @param  platformVersion
   *         A platform version to test against
   * @return Boolean representing if the add-on is compatible
   */
  isCompatibleWith: function(aAppVerison, aPlatformVersion) {
    return true;
  },

  /**
   * Starts an update check for this add-on. This will perform
   * asynchronously and deliver results to the given listener.
   *
   * @param  aListener
   *         An UpdateListener for the update process
   * @param  aReason
   *         A reason code for performing the update
   * @param  aAppVersion
   *         An application version to check for updates for
   * @param  aPlatformVersion
   *         A platform version to check for updates for
   */
  findUpdates: function(aListener, aReason, aAppVersion, aPlatformVersion) {
    if ("onNoCompatibilityUpdateAvailable" in aListener)
      aListener.onNoCompatibilityUpdateAvailable(this);
    if ("onNoUpdateAvailable" in aListener)
      aListener.onNoUpdateAvailable(this);
    if ("onUpdateFinished" in aListener)
      aListener.onUpdateFinished(this);
  }
}

/**
 * The add-on repository is a source of add-ons that can be installed. It can
 * be searched in three ways. The first takes a list of IDs and returns a
 * list of the corresponding add-ons. The second returns a list of add-ons that
 * come highly recommended. This list should change frequently. The third is to
 * search for specific search terms entered by the user. Searches are
 * asynchronous and results should be passed to the provided callback object
 * when complete. The results passed to the callback should only include add-ons
 * that are compatible with the current application and are not already
 * installed.
 */
var AddonRepository = {
  /**
   * Whether caching is currently enabled
   */
  get cacheEnabled() {
    // Act as though caching is disabled if there was an unrecoverable error
    // openning the database.
    if (!AddonDatabase.databaseOk)
      return false;

    let preference = PREF_GETADDONS_CACHE_ENABLED;
    let enabled = false;
    try {
      enabled = Services.prefs.getBoolPref(preference);
    } catch(e) {
      WARN("cacheEnabled: Couldn't get pref: " + preference);
    }

    return enabled;
  },

  // A cache of the add-ons stored in the database
  _addons: null,

  // An array of callbacks pending the retrieval of add-ons from AddonDatabase
  _pendingCallbacks: null,

  // Whether a search is currently in progress
  _searching: false,

  // XHR associated with the current request
  _request: null,

  /*
   * Addon search results callback object that contains two functions
   *
   * searchSucceeded - Called when a search has suceeded.
   *
   * @param  aAddons
   *         An array of the add-on results. In the case of searching for
   *         specific terms the ordering of results may be determined by
   *         the search provider.
   * @param  aAddonCount
   *         The length of aAddons
   * @param  aTotalResults
   *         The total results actually available in the repository
   *
   *
   * searchFailed - Called when an error occurred when performing a search.
   */
  _callback: null,

  // Maximum number of results to return
  _maxResults: null,
  
  /**
   * Initialize AddonRepository.
   */
  initialize: function() {
    Services.obs.addObserver(this, "xpcom-shutdown", false);
  },

  /**
   * Observe xpcom-shutdown notification, so we can shutdown cleanly.
   */
  observe: function (aSubject, aTopic, aData) {
    if (aTopic == "xpcom-shutdown") {
      Services.obs.removeObserver(this, "xpcom-shutdown");
      this.shutdown();
    }
  },

  /**
   * Shut down AddonRepository
   */
  shutdown: function() {
    this.cancelSearch();

    this._addons = null;
    this._pendingCallbacks = null;
    AddonDatabase.shutdown(function() {
      Services.obs.notifyObservers(null, "addon-repository-shutdown", null);
    });
  },

  /**
   * Asynchronously get a cached add-on by id. The add-on (or null if the
   * add-on is not found) is passed to the specified callback. If caching is
   * disabled, null is passed to the specified callback.
   *
   * @param  aId
   *         The id of the add-on to get
   * @param  aCallback
   *         The callback to pass the result back to
   */
  getCachedAddonByID: function(aId, aCallback) {
    if (!aId || !this.cacheEnabled) {
      aCallback(null);
      return;
    }

    let self = this;
    function getAddon(aAddons) {
      aCallback((aId in aAddons) ? aAddons[aId] : null);
    }

    if (this._addons == null) {
      if (this._pendingCallbacks == null) {
        // Data has not been retrieved from the database, so retrieve it
        this._pendingCallbacks = [];
        this._pendingCallbacks.push(getAddon);
        AddonDatabase.retrieveStoredData(function(aAddons) {
          let pendingCallbacks = self._pendingCallbacks;

          // Check if cache was shutdown or deleted before callback was called
          if (pendingCallbacks == null)
            return;

          // Callbacks may want to trigger a other caching operations that may
          // affect _addons and _pendingCallbacks, so set to final values early
          self._pendingCallbacks = null;
          self._addons = aAddons;

          pendingCallbacks.forEach(function(aCallback) aCallback(aAddons));
        });

        return;
      }

      // Data is being retrieved from the database, so wait
      this._pendingCallbacks.push(getAddon);
      return;
    }

    // Data has been retrieved, so immediately return result
    getAddon(this._addons);
  },

  /**
   * Asynchronously repopulate cache so it only contains the add-ons
   * corresponding to the specified ids. If caching is disabled,
   * the cache is completely removed.
   *
   * @param  aIds
   *         The array of add-on ids to repopulate the cache with
   * @param  aCallback
   *         The optional callback to call once complete
   */
  repopulateCache: function(aIds, aCallback) {
    let self = this;

    // Completely remove cache if caching is not enabled
    if (!this.cacheEnabled) {
      this._addons = null;
      this._pendingCallbacks = null;
      AddonDatabase.delete(aCallback);
      return;
    }

    this.getAddonsByIDs(aIds, {
      searchSucceeded: function(aAddons) {
        self._addons = {};
        aAddons.forEach(function(aAddon) { self._addons[aAddon.id] = aAddon; });
        AddonDatabase.repopulate(aAddons, aCallback);
      },
      searchFailed: function() {
        WARN("Search failed when repopulating cache");
        if (aCallback)
          aCallback();
      }
    });
  },

  /**
   * Asynchronously add add-ons to the cache corresponding to the specified
   * ids. If caching is disabled, the cache is unchanged and the callback is
   * immediatly called if it is defined.
   *
   * @param  aIds
   *         The array of add-on ids to add to the cache
   * @param  aCallback
   *         The optional callback to call once complete
   */
  cacheAddons: function(aIds, aCallback) {
    if (!this.cacheEnabled) {
      if (aCallback)
        aCallback();
      return;
    }

    let self = this;
    this.getAddonsByIDs(aIds, {
      searchSucceeded: function(aAddons) {
        aAddons.forEach(function(aAddon) { self._addons[aAddon.id] = aAddon; });
        AddonDatabase.insertAddons(aAddons, aCallback);
      },
      searchFailed: function() {
        WARN("Search failed when adding add-ons to cache");
        if (aCallback)
          aCallback();
      }
    });
  },

  /**
   * The homepage for visiting this repository. If the corresponding preference
   * is not defined, defaults to about:blank.
   */
  get homepageURL() {
    let url = this._formatURLPref(PREF_GETADDONS_BROWSEADDONS, {});
    return (url != null) ? url : "about:blank";
  },

  /**
   * Returns whether this instance is currently performing a search. New
   * searches will not be performed while this is the case.
   */
  get isSearching() {
    return this._searching;
  },

  /**
   * The url that can be visited to see recommended add-ons in this repository.
   * If the corresponding preference is not defined, defaults to about:blank.
   */
  getRecommendedURL: function() {
    let url = this._formatURLPref(PREF_GETADDONS_BROWSERECOMMENDED, {});
    return (url != null) ? url : "about:blank";
  },

  /**
   * Retrieves the url that can be visited to see search results for the given
   * terms. If the corresponding preference is not defined, defaults to
   * about:blank.
   *
   * @param  aSearchTerms
   *         Search terms used to search the repository
   */
  getSearchURL: function(aSearchTerms) {
    let url = this._formatURLPref(PREF_GETADDONS_BROWSESEARCHRESULTS, {
      TERMS : encodeURIComponent(aSearchTerms)
    });
    return (url != null) ? url : "about:blank";
  },

  /**
   * Cancels the search in progress. If there is no search in progress this
   * does nothing.
   */
  cancelSearch: function() {
    this._searching = false;
    if (this._request) {
      this._request.abort();
      this._request = null;
    }
    this._callback = null;
  },

  /**
   * Begins a search for add-ons in this repository by ID. Results will be
   * passed to the given callback.
   *
   * @param  aIDs
   *         The array of ids to search for
   * @param  aCallback
   *         The callback to pass results to
   */
  getAddonsByIDs: function(aIDs, aCallback) {
    let ids = aIDs.slice(0);
    let url = this._formatURLPref(PREF_GETADDONS_BYIDS, {
      API_VERSION : API_VERSION,
      IDS : ids.map(encodeURIComponent).join(',')
    });

    let self = this;
    function handleResults(aElements, aTotalResults) {
      // Don't use this._parseAddons() so that, for example,
      // incompatible add-ons are not filtered out
      let results = [];
      for (let i = 0; i < aElements.length && results.length < self._maxResults; i++) {
        let result = self._parseAddon(aElements[i]);
        if (result == null)
          continue;

        // Ignore add-on if it wasn't actually requested
        let idIndex = ids.indexOf(result.addon.id);
        if (idIndex == -1)
          continue;

        results.push(result);
        // Ignore this add-on from now on
        ids.splice(idIndex, 1);
      }

      // aTotalResults irrelevant
      self._reportSuccess(results, -1);
    }

    this._beginSearch(url, ids.length, aCallback, handleResults);
  },

  /**
   * Begins a search for recommended add-ons in this repository. Results will
   * be passed to the given callback.
   *
   * @param  aMaxResults
   *         The maximum number of results to return
   * @param  aCallback
   *         The callback to pass results to
   */
  retrieveRecommendedAddons: function(aMaxResults, aCallback) {
    let url = this._formatURLPref(PREF_GETADDONS_GETRECOMMENDED, {
      API_VERSION : API_VERSION,

      // Get twice as many results to account for potential filtering
      MAX_RESULTS : 2 * aMaxResults
    });

    let self = this;
    function handleResults(aElements, aTotalResults) {
      self._getLocalAddonIds(function(aLocalAddonIds) {
        // aTotalResults irrelevant
        self._parseAddons(aElements, -1, aLocalAddonIds);
      });
    }

    this._beginSearch(url, aMaxResults, aCallback, handleResults);
  },

  /**
   * Begins a search for add-ons in this repository. Results will be passed to
   * the given callback.
   *
   * @param  aSearchTerms
   *         The terms to search for
   * @param  aMaxResults
   *         The maximum number of results to return
   * @param  aCallback
   *         The callback to pass results to
   */
  searchAddons: function(aSearchTerms, aMaxResults, aCallback) {
    let url = this._formatURLPref(PREF_GETADDONS_GETSEARCHRESULTS, {
      API_VERSION : API_VERSION,
      TERMS : encodeURIComponent(aSearchTerms),

      // Get twice as many results to account for potential filtering
      MAX_RESULTS : 2 * aMaxResults
    });

    let self = this;
    function handleResults(aElements, aTotalResults) {
      self._getLocalAddonIds(function(aLocalAddonIds) {
        self._parseAddons(aElements, aTotalResults, aLocalAddonIds);
      });
    }

    this._beginSearch(url, aMaxResults, aCallback, handleResults);
  },

  // Posts results to the callback
  _reportSuccess: function(aResults, aTotalResults) {
    this._searching = false;
    this._request = null;
    // The callback may want to trigger a new search so clear references early
    let addons = [result.addon for each(result in aResults)];
    let callback = this._callback;
    this._callback = null;
    callback.searchSucceeded(addons, addons.length, aTotalResults);
  },

  // Notifies the callback of a failure
  _reportFailure: function() {
    this._searching = false;
    this._request = null;
    // The callback may want to trigger a new search so clear references early
    let callback = this._callback;
    this._callback = null;
    callback.searchFailed();
  },

  // Get descendant by unique tag name. Returns null if not unique tag name.
  _getUniqueDescendant: function(aElement, aTagName) {
    let elementsList = aElement.getElementsByTagName(aTagName);
    return (elementsList.length == 1) ? elementsList[0] : null;
  },

  // Parse out trimmed text content. Returns null if text content empty.
  _getTextContent: function(aElement) {
    let textContent = aElement.textContent.trim();
    return (textContent.length > 0) ? textContent : null;
  },

  // Parse out trimmed text content of a descendant with the specified tag name
  // Returns null if the parsing unsuccessful.
  _getDescendantTextContent: function(aElement, aTagName) {
    let descendant = this._getUniqueDescendant(aElement, aTagName);
    return (descendant != null) ? this._getTextContent(descendant) : null;
  },

  /*
   * Creates an AddonSearchResult by parsing an <addon> element
   *
   * @param  aElement
   *         The <addon> element to parse
   * @param  aSkip
   *         Object containing ids and sourceURIs of add-ons to skip.
   * @return Result object containing the parsed AddonSearchResult, xpiURL and
   *         xpiHash if the parsing was successful. Otherwise returns null.
   */
  _parseAddon: function(aElement, aSkip) {
    let skipIDs = (aSkip && aSkip.ids) ? aSkip.ids : [];
    let skipSourceURIs = (aSkip && aSkip.sourceURIs) ? aSkip.sourceURIs : [];

    let guid = this._getDescendantTextContent(aElement, "guid");
    if (guid == null || skipIDs.indexOf(guid) != -1)
      return null;

    let addon = new AddonSearchResult(guid);
    let result = {
      addon: addon,
      xpiURL: null,
      xpiHash: null
    };

    let self = this;
    for (let node = aElement.firstChild; node; node = node.nextSibling) {
      if (!(node instanceof Ci.nsIDOMElement))
        continue;

      let localName = node.localName;

      // Handle case where the wanted string value is located in text content
      if (localName in STRING_KEY_MAP) {
        addon[STRING_KEY_MAP[localName]] = this._getTextContent(node);
        continue;
      }

      // Handle case where the wanted integer value is located in text content
      if (localName in INTEGER_KEY_MAP) {
        let value = parseInt(this._getTextContent(node));
        if (value >= 0)
          addon[INTEGER_KEY_MAP[localName]] = value;
        continue;
      }

      // Handle cases that aren't as simple as grabbing the text content
      switch (localName) {
        case "type":
          // Map AMO's type id to corresponding string
          let id = parseInt(node.getAttribute("id"));
          switch (id) {
            case 1:
              addon.type = "extension";
              break;
            case 2:
              addon.type = "theme";
              break;
            default:
              WARN("Unknown type id when parsing addon: " + id);
          }
          break;
        case "authors":
          let authorNodes = node.getElementsByTagName("author");
          Array.forEach(authorNodes, function(aAuthorNode) {
            let name = self._getDescendantTextContent(aAuthorNode, "name");
            let link = self._getDescendantTextContent(aAuthorNode, "link");
            if (name == null || link == null)
              return;

            let author = new AddonManagerPrivate.AddonAuthor(name, link);
            if (addon.creator == null)
              addon.creator = author;
            else {
              if (addon.developers == null)
                addon.developers = [];

              addon.developers.push(author);
            }
          });
          break;
        case "previews":
          let previewNodes = node.getElementsByTagName("preview");
          Array.forEach(previewNodes, function(aPreviewNode) {
            let full = self._getDescendantTextContent(aPreviewNode, "full");
            if (full == null)
              return;

            let thumbnail = self._getDescendantTextContent(aPreviewNode, "thumbnail");
            let caption = self._getDescendantTextContent(aPreviewNode, "caption");
            let screenshot = new AddonManagerPrivate.AddonScreenshot(full, thumbnail, caption);

            if (addon.screenshots == null)
              addon.screenshots = [];

            if (aPreviewNode.getAttribute("primary") == 1)
              addon.screenshots.unshift(screenshot);
            else
              addon.screenshots.push(screenshot);
          });
          break;
        case "learnmore":
          addon.homepageURL = addon.homepageURL || this._getTextContent(node);
          break;
        case "contribution_data":
          let meetDevelopers = this._getDescendantTextContent(node, "meet_developers");
          let suggestedAmount = this._getDescendantTextContent(node, "suggested_amount");
          if (meetDevelopers != null && suggestedAmount != null) {
            addon.contributionURL = meetDevelopers;
            addon.contributionAmount = suggestedAmount;
          }
          break
        case "rating":
          let averageRating = parseInt(this._getTextContent(node));
          if (averageRating >= 0)
            addon.averageRating = Math.min(5, averageRating);
          break;
        case "reviews":
          let url = this._getTextContent(node);
          let num = parseInt(node.getAttribute("num"));
          if (url != null && num >= 0) {
            addon.reviewURL = url;
            addon.reviewCount = num;
          }
          break;
        case "status":
          let repositoryStatus = parseInt(node.getAttribute("id"));
          if (!isNaN(repositoryStatus))
            addon.repositoryStatus = repositoryStatus;
          break;
        case "install":
          // No os attribute means the xpi is compatible with any os
          if (node.hasAttribute("os")) {
            let os = node.getAttribute("os").trim().toLowerCase();
            // If the os is not ALL and not the current OS then ignore this xpi
            if (os != "all" && os != Services.appinfo.OS.toLowerCase())
              break;
          }

          let xpiURL = this._getTextContent(node);
          if (xpiURL == null)
            break;

          if (skipSourceURIs.indexOf(xpiURL) != -1)
            return null;

          result.xpiURL = xpiURL;
          addon.sourceURI = NetUtil.newURI(xpiURL);

          let size = parseInt(node.getAttribute("size"));
          addon.size = (size >= 0) ? size : null;

          let xpiHash = node.getAttribute("hash");
          if (xpiHash != null)
            xpiHash = xpiHash.trim();
          result.xpiHash = xpiHash ? xpiHash : null;
          break;
        case "last_updated":
          let epoch = parseInt(node.getAttribute("epoch"));
          if (!isNaN(epoch))
            addon.updateDate = new Date(1000 * epoch);
          break;
      }
    }

    return result;
  },

  _parseAddons: function(aElements, aTotalResults, aSkip) {
    let self = this;
    let results = [];
    for (let i = 0; i < aElements.length && results.length < this._maxResults; i++) {
      let element = aElements[i];

      // Ignore sandboxed add-ons
      let status = this._getUniqueDescendant(element, "status");
      // The status element has a unique id for each status type. 4 is Public.
      if (status == null || status.getAttribute("id") != 4)
        continue;

      // Ignore add-ons not compatible with this Application
      let tags = this._getUniqueDescendant(element, "compatible_applications");
      if (tags == null)
        continue;

      let applications = tags.getElementsByTagName("appID");
      let compatible = Array.some(applications, function(aAppNode) {
        if (self._getTextContent(aAppNode) != Services.appinfo.ID)
          return false;

        let parent = aAppNode.parentNode;
        let minVersion = self._getDescendantTextContent(parent, "min_version");
        let maxVersion = self._getDescendantTextContent(parent, "max_version");
        if (minVersion == null || maxVersion == null)
          return false;

        let currentVersion = Services.appinfo.version;
        return (Services.vc.compare(minVersion, currentVersion) <= 0 &&
                Services.vc.compare(currentVersion, maxVersion) <= 0);
      });

      if (!compatible)
        continue;

      // Add-on meets all requirements, so parse out data
      let result = this._parseAddon(element, aSkip);
      if (result == null)
        continue;

      // Ignore add-on missing a required attribute
      let requiredAttributes = ["id", "name", "version", "type", "creator"];
      if (requiredAttributes.some(function(aAttribute) !result.addon[aAttribute]))
        continue;

      // Add only if there was an xpi compatible with this OS
      if (!result.xpiURL)
        continue;

      results.push(result);
      // Ignore this add-on from now on by adding it to the skip array
      aSkip.ids.push(result.addon.id);
    }

    // Immediately report success if no AddonInstall instances to create
    let pendingResults = results.length;
    if (pendingResults == 0) {
      this._reportSuccess(results, aTotalResults);
      return;
    }

    // Create an AddonInstall for each result
    let self = this;
    results.forEach(function(aResult) {
      let addon = aResult.addon;
      let callback = function(aInstall) {
        addon.install = aInstall;
        pendingResults--;
        if (pendingResults == 0)
          self._reportSuccess(results, aTotalResults);
      }

      AddonManager.getInstallForURL(aResult.xpiURL, callback,
                                    "application/x-xpinstall", aResult.xpiHash,
                                    addon.name, addon.iconURL, addon.version);
    });
  },

  // Begins a new search if one isn't currently executing
  _beginSearch: function(aURI, aMaxResults, aCallback, aHandleResults) {
    if (this._searching || aURI == null || aMaxResults <= 0) {
      aCallback.searchFailed();
      return;
    }

    this._searching = true;
    this._callback = aCallback;
    this._maxResults = aMaxResults;

    this._request = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"].
                    createInstance(Ci.nsIXMLHttpRequest);
    this._request.open("GET", aURI, true);
    this._request.overrideMimeType("text/xml");

    let self = this;
    this._request.onerror = function(aEvent) { self._reportFailure(); };
    this._request.onload = function(aEvent) {
      let request = aEvent.target;
      let responseXML = request.responseXML;

      if (!responseXML || responseXML.documentElement.namespaceURI == XMLURI_PARSE_ERROR ||
          (request.status != 200 && request.status != 0)) {
        self._reportFailure();
        return;
      }

      let documentElement = responseXML.documentElement;
      let elements = documentElement.getElementsByTagName("addon");
      let totalResults = elements.length;
      let parsedTotalResults = parseInt(documentElement.getAttribute("total_results"));
      // Parsed value of total results only makes sense if >= elements.length
      if (parsedTotalResults >= totalResults)
        totalResults = parsedTotalResults;

      aHandleResults(elements, totalResults);
    };
    this._request.send(null);
  },

  // Gets the id's of local add-ons, and the sourceURI's of local installs,
  // passing the results to aCallback
  _getLocalAddonIds: function(aCallback) {
    let self = this;
    let localAddonIds = {ids: null, sourceURIs: null};

    AddonManager.getAllAddons(function(aAddons) {
      localAddonIds.ids = [a.id for each (a in aAddons)];
      if (localAddonIds.sourceURIs)
        aCallback(localAddonIds);
    });

    AddonManager.getAllInstalls(function(aInstalls) {
      localAddonIds.sourceURIs = [];
      aInstalls.forEach(function(aInstall) {
        if (aInstall.state != AddonManager.STATE_AVAILABLE)
          localAddonIds.sourceURIs.push(aInstall.sourceURI.spec);
      });

      if (localAddonIds.ids)
        aCallback(localAddonIds);
    });
  },

  // Create url from preference, returning null if preference does not exist
  _formatURLPref: function(aPreference, aSubstitutions) {
    let url = null;
    try {
      url = Services.prefs.getCharPref(aPreference);
    } catch(e) {
      WARN("_formatURLPref: Couldn't get pref: " + aPreference);
      return null;
    }

    url = url.replace(/%([A-Z_]+)%/g, function(aMatch, aKey) {
      return (aKey in aSubstitutions) ? aSubstitutions[aKey] : aMatch;
    });

    return Services.urlFormatter.formatURL(url);
  }
};
AddonRepository.initialize();

var AddonDatabase = {
  // true if the database connection has been opened
  initialized: false,
  // false if there was an unrecoverable error openning the database
  databaseOk: true,
  // A cache of statements that are used and need to be finalized on shutdown
  statementCache: {},

  // The statements used by the database
  statements: {
    getAllAddons: "SELECT internal_id, id, type, name, version, " +
                  "creator, creatorURL, description, fullDescription, " +
                  "developerComments, eula, iconURL, homepageURL, supportURL, " +
                  "contributionURL, contributionAmount, averageRating, " +
                  "reviewCount, reviewURL, totalDownloads, weeklyDownloads, " +
                  "dailyUsers, sourceURI, repositoryStatus, size, updateDate " +
                  "FROM addon",

    getAllDevelopers: "SELECT addon_internal_id, name, url FROM developer " +
                      "ORDER BY addon_internal_id, num",

    getAllScreenshots: "SELECT addon_internal_id, url, thumbnailURL, caption " +
                       "FROM screenshot ORDER BY addon_internal_id, num",

    insertAddon: "INSERT INTO addon VALUES (NULL, :id, :type, :name, :version, " +
                 ":creator, :creatorURL, :description, :fullDescription, " +
                 ":developerComments, :eula, :iconURL, :homepageURL, :supportURL, " +
                 ":contributionURL, :contributionAmount, :averageRating, " +
                 ":reviewCount, :reviewURL, :totalDownloads, :weeklyDownloads, " +
                 ":dailyUsers, :sourceURI, :repositoryStatus, :size, :updateDate)",

    insertDeveloper:  "INSERT INTO developer VALUES (:addon_internal_id, " +
                      ":num, :name, :url)",

    insertScreenshot: "INSERT INTO screenshot VALUES (:addon_internal_id, " +
                      ":num, :url, :thumbnailURL, :caption)",

    emptyAddon:       "DELETE FROM addon"
  },

  /**
   * A helper function to log an SQL error.
   *
   * @param  aError
   *         The storage error code associated with the error
   * @param  aErrorString
   *         An error message
   */
  logSQLError: function AD_logSQLError(aError, aErrorString) {
    ERROR("SQL error " + aError + ": " + aErrorString);
  },

  /**
   * A helper function to log any errors that occur during async statements.
   *
   * @param  aError
   *         A mozIStorageError to log
   */
  asyncErrorLogger: function AD_asyncErrorLogger(aError) {
    ERROR("Async SQL error " + aError.result + ": " + aError.message);
  },

  /**
   * Synchronously opens a new connection to the database file.
   *
   * @param  aSecondAttempt
   *         Whether this is a second attempt to open the database
   * @return the mozIStorageConnection for the database
   */
  openConnection: function AD_openConnection(aSecondAttempt) {
    this.initialized = true;
    delete this.connection;

    let dbfile = FileUtils.getFile(KEY_PROFILEDIR, [FILE_DATABASE], true);
    let dbMissing = !dbfile.exists();

    try {
      this.connection = Services.storage.openUnsharedDatabase(dbfile);
    } catch (e) {
      this.initialized = false;
      ERROR("Failed to open database: " + e);
      if (aSecondAttempt || dbMissing) {
        this.databaseOk = false;
        throw e;
      }

      LOG("Deleting database, and attempting openConnection again");
      dbfile.remove(false);
      return this.openConnection(true);
    }

    this.connection.executeSimpleSQL("PRAGMA locking_mode = EXCLUSIVE");
    if (dbMissing || this.connection.schemaVersion == 0)
      this._createSchema();

    return this.connection;
  },

  /**
   * A lazy getter for the database connection.
   */
  get connection() {
    return this.openConnection();
  },

  /**
   * Asynchronously shuts down the database connection and releases all
   * cached objects
   *
   * @param  aCallback
   *         An optional callback to call once complete
   */
  shutdown: function AD_shutdown(aCallback) {
    this.databaseOk = true;
    if (!this.initialized) {
      if (aCallback)
        aCallback();
      return;
    }

    this.initialized = false;

    for each (let stmt in this.statementCache)
      stmt.finalize();
    this.statementCache = {};

    if (this.connection.transactionInProgress) {
      ERROR("Outstanding transaction, rolling back.");
      this.connection.rollbackTransaction();
    }

    let connection = this.connection;
    delete this.connection;

    // Re-create the connection smart getter to allow the database to be
    // re-loaded during testing.
    this.__defineGetter__("connection", function() {
      return this.openConnection();
    });

    connection.asyncClose(aCallback);
  },

  /**
   * Asynchronously deletes the database, shutting down the connection
   * first if initialized
   *
   * @param  aCallback
   *         An optional callback to call once complete
   */
  delete: function AD_delete(aCallback) {
    this.shutdown(function() {
      let dbfile = FileUtils.getFile(KEY_PROFILEDIR, [FILE_DATABASE], true);
      if (dbfile.exists())
        dbfile.remove(false);

      if (aCallback)
        aCallback();
    });
  },

  /**
   * Gets a cached statement or creates a new statement if it doesn't already
   * exist.
   *
   * @param  aKey
   *         A unique key to reference the statement
   * @return a mozIStorageStatement for the SQL corresponding to the unique key
   */
  getStatement: function AD_getStatement(aKey) {
    if (aKey in this.statementCache)
      return this.statementCache[aKey];

    let sql = this.statements[aKey];
    try {
      return this.statementCache[aKey] = this.connection.createStatement(sql);
    } catch (e) {
      ERROR("Error creating statement " + aKey + " (" + aSql + ")");
      throw e;
    }
  },

  /**
   * Asynchronously retrieve all add-ons from the database, and pass it
   * to the specified callback
   *
   * @param  aCallback
   *         The callback to pass the add-ons back to
   */
  retrieveStoredData: function AD_retrieveStoredData(aCallback) {
    let self = this;
    let addons = {};

    // Retrieve all data from the addon table
    function getAllAddons() {
      self.getStatement("getAllAddons").executeAsync({
        handleResult: function(aResults) {
          let row = null;
          while (row = aResults.getNextRow()) {
            let internal_id = row.getResultByName("internal_id");
            addons[internal_id] = self._makeAddonFromAsyncRow(row);
          }
        },

        handleError: self.asyncErrorLogger,

        handleCompletion: function(aReason) {
          if (aReason != Ci.mozIStorageStatementCallback.REASON_FINISHED) {
            ERROR("Error retrieving add-ons from database. Returning empty results");
            aCallback({});
            return;
          }

          getAllDevelopers();
        }
      });
    }

    // Retrieve all data from the developer table
    function getAllDevelopers() {
      self.getStatement("getAllDevelopers").executeAsync({
        handleResult: function(aResults) {
          let row = null;
          while (row = aResults.getNextRow()) {
            let addon_internal_id = row.getResultByName("addon_internal_id");
            if (!(addon_internal_id in addons)) {
              WARN("Found a developer not linked to an add-on in database");
              continue;
            }

            let addon = addons[addon_internal_id];
            if (!addon.developers)
              addon.developers = [];

            addon.developers.push(self._makeDeveloperFromAsyncRow(row));
          }
        },

        handleError: self.asyncErrorLogger,

        handleCompletion: function(aReason) {
          if (aReason != Ci.mozIStorageStatementCallback.REASON_FINISHED) {
            ERROR("Error retrieving developers from database. Returning empty results");
            aCallback({});
            return;
          }

          getAllScreenshots();
        }
      });
    }

    // Retrieve all data from the screenshot table
    function getAllScreenshots() {
      self.getStatement("getAllScreenshots").executeAsync({
        handleResult: function(aResults) {
          let row = null;
          while (row = aResults.getNextRow()) {
            let addon_internal_id = row.getResultByName("addon_internal_id");
            if (!(addon_internal_id in addons)) {
              WARN("Found a screenshot not linked to an add-on in database");
              continue;
            }

            let addon = addons[addon_internal_id];
            if (!addon.screenshots)
              addon.screenshots = [];
            addon.screenshots.push(self._makeScreenshotFromAsyncRow(row));
          }
        },

        handleError: self.asyncErrorLogger,

        handleCompletion: function(aReason) {
          if (aReason != Ci.mozIStorageStatementCallback.REASON_FINISHED) {
            ERROR("Error retrieving screenshots from database. Returning empty results");
            aCallback({});
            return;
          }

          let returnedAddons = {};
          for each (addon in addons)
            returnedAddons[addon.id] = addon;
          aCallback(returnedAddons);
        }
      });
    }

    // Begin asynchronous process
    getAllAddons();
  },

  /**
   * Asynchronously repopulates the database so it only contains the
   * specified add-ons
   *
   * @param  aAddons
   *         The array of add-ons to repopulate the database with
   * @param  aCallback
   *         An optional callback to call once complete
   */
  repopulate: function AD_repopulate(aAddons, aCallback) {
    let self = this;

    // Completely empty the database
    let stmts = [this.getStatement("emptyAddon")];

    this.connection.executeAsync(stmts, stmts.length, {
      handleResult: function() {},
      handleError: self.asyncErrorLogger,

      handleCompletion: function(aReason) {
        if (aReason != Ci.mozIStorageStatementCallback.REASON_FINISHED)
          ERROR("Error emptying database. Attempting to continue repopulating database");

        // Insert the specified add-ons
        self.insertAddons(aAddons, aCallback);
      }
    });
  },

  /**
   * Asynchronously inserts an array of add-ons into the database
   *
   * @param  aAddons
   *         The array of add-ons to insert
   * @param  aCallback
   *         An optional callback to call once complete
   */
  insertAddons: function AD_insertAddons(aAddons, aCallback) {
    let self = this;
    let currentAddon = -1;

    // Chain insertions
    function insertNextAddon() {
      if (++currentAddon == aAddons.length) {
        if (aCallback)
          aCallback();
        return;
      }

      self._insertAddon(aAddons[currentAddon], insertNextAddon);
    }

    insertNextAddon();
  },

  /**
   * Inserts an individual add-on into the database. If the add-on already
   * exists in the database (by id), then the specified add-on will not be
   * inserted.
   *
   * @param  aAddon
   *         The add-on to insert into the database
   * @param  aCallback
   *         The callback to call once complete
   */
  _insertAddon: function AD__insertAddon(aAddon, aCallback) {
    let self = this;
    let internal_id = null;
    this.connection.beginTransaction();

    // Simultaneously insert the developers and screenshots of the add-on
    function insertDevelopersAndScreenshots() {
      let stmts = [];

      // Initialize statement and parameters for inserting an array
      function initializeArrayInsert(aStatementKey, aArray, aAddParams) {
        if (!aArray || aArray.length == 0)
          return;

        let stmt = self.getStatement(aStatementKey);
        let params = stmt.newBindingParamsArray();
        aArray.forEach(function(aElement, aIndex) {
          aAddParams(params, internal_id, aElement, aIndex);
        });

        stmt.bindParameters(params);
        stmts.push(stmt);
      }

      // Initialize statements to insert developers and screenshots
      initializeArrayInsert("insertDeveloper", aAddon.developers,
                            self._addDeveloperParams);
      initializeArrayInsert("insertScreenshot", aAddon.screenshots,
                            self._addScreenshotParams);

      // Immediately call callback if nothing to insert
      if (stmts.length == 0) {
        self.connection.commitTransaction();
        aCallback();
        return;
      }

      self.connection.executeAsync(stmts, stmts.length, {
        handleResult: function() {},
        handleError: self.asyncErrorLogger,
        handleCompletion: function(aReason) {
          if (aReason != Ci.mozIStorageStatementCallback.REASON_FINISHED) {
            ERROR("Error inserting developers and screenshots into database. Attempting to continue");
            self.connection.rollbackTransaction();
          }
          else {
            self.connection.commitTransaction();
          }

          aCallback();
        }
      });
    }

    // Insert add-on into database
    this._makeAddonStatement(aAddon).executeAsync({
      handleResult: function() {},
      handleError: self.asyncErrorLogger,

      handleCompletion: function(aReason) {
        if (aReason != Ci.mozIStorageStatementCallback.REASON_FINISHED) {
          ERROR("Error inserting add-ons into database. Attempting to continue.");
          self.connection.rollbackTransaction();
          aCallback();
          return;
        }

        internal_id = self.connection.lastInsertRowID;
        insertDevelopersAndScreenshots();
      }
    });
  },

  /**
   * Make an asynchronous statement that will insert the specified add-on
   *
   * @param  aAddon
   *         The add-on to make the statement for
   * @return The asynchronous mozIStorageStatement
   */
  _makeAddonStatement: function AD__makeAddonStatement(aAddon) {
    let stmt = this.getStatement("insertAddon");
    let params = stmt.params;

    PROP_SINGLE.forEach(function(aProperty) {
      switch (aProperty) {
        case "sourceURI":
          params.sourceURI = aAddon.sourceURI ? aAddon.sourceURI.spec : null;
          break;
        case "creator":
          params.creator =  aAddon.creator ? aAddon.creator.name : null;
          params.creatorURL =  aAddon.creator ? aAddon.creator.url : null;
          break;
        case "updateDate":
          params.updateDate = aAddon.updateDate ? aAddon.updateDate.getTime() : null;
          break;
        default:
          params[aProperty] = aAddon[aProperty];
      }
    });

    return stmt;
  },

  /**
   * Add developer parameters to the specified mozIStorageBindingParamsArray
   *
   * @param  aParams
   *         The mozIStorageBindingParamsArray to add the parameters to
   * @param  aInternalID
   *         The internal_id of the add-on that this developer is for
   * @param  aDeveloper
   *         The developer to make the parameters from
   * @param  aIndex
   *         The index of this developer
   * @return The asynchronous mozIStorageStatement
   */
  _addDeveloperParams: function AD__addDeveloperParams(aParams, aInternalID,
                                                       aDeveloper, aIndex) {
    let bp = aParams.newBindingParams();
    bp.bindByName("addon_internal_id", aInternalID);
    bp.bindByName("num", aIndex);
    bp.bindByName("name", aDeveloper.name);
    bp.bindByName("url", aDeveloper.url);
    aParams.addParams(bp);
  },

  /**
   * Add screenshot parameters to the specified mozIStorageBindingParamsArray
   *
   * @param  aParams
   *         The mozIStorageBindingParamsArray to add the parameters to
   * @param  aInternalID
   *         The internal_id of the add-on that this screenshot is for
   * @param  aScreenshot
   *         The screenshot to make the parameters from
   * @param  aIndex
   *         The index of this screenshot
   */
  _addScreenshotParams: function AD__addScreenshotParams(aParams, aInternalID,
                                                         aScreenshot, aIndex) {
    let bp = aParams.newBindingParams();
    bp.bindByName("addon_internal_id", aInternalID);
    bp.bindByName("num", aIndex);
    bp.bindByName("url", aScreenshot.url);
    bp.bindByName("thumbnailURL", aScreenshot.thumbnailURL);
    bp.bindByName("caption", aScreenshot.caption);
    aParams.addParams(bp);
  },

  /**
   * Make add-on from an asynchronous row
   * Note: This add-on will be lacking both developers and screenshots
   *
   * @param  aRow
   *         The asynchronous row to use
   * @return The created add-on
   */
  _makeAddonFromAsyncRow: function AD__makeAddonFromAsyncRow(aRow) {
    let addon = {};

    PROP_SINGLE.forEach(function(aProperty) {
      let value = aRow.getResultByName(aProperty);

      switch (aProperty) {
        case "sourceURI":
          addon.sourceURI = value ? NetUtil.newURI(value) : null;
          break;
        case "creator":
          let creatorURL = aRow.getResultByName("creatorURL");
          if (value || creatorURL)
            addon.creator = new AddonManagerPrivate.AddonAuthor(value, creatorURL);
          else
            addon.creator = null;
          break;
        case "updateDate":
          addon.updateDate = value ? new Date(value) : null;
          break;
        default:
          addon[aProperty] = value;
      }
    });

    return addon;
  },

  /**
   * Make a developer from an asynchronous row
   *
   * @param  aRow
   *         The asynchronous row to use
   * @return The created developer
   */
  _makeDeveloperFromAsyncRow: function AD__makeDeveloperFromAsyncRow(aRow) {
    let name = aRow.getResultByName("name");
    let url = aRow.getResultByName("url")
    return new AddonManagerPrivate.AddonAuthor(name, url);
  },

  /**
   * Make a screenshot from an asynchronous row
   *
   * @param  aRow
   *         The asynchronous row to use
   * @return The created screenshot
   */
  _makeScreenshotFromAsyncRow: function AD__makeScreenshotFromAsyncRow(aRow) {
    let url = aRow.getResultByName("url");
    let thumbnailURL = aRow.getResultByName("thumbnailURL");
    let caption =aRow.getResultByName("caption");
    return new AddonManagerPrivate.AddonScreenshot(url, thumbnailURL, caption);
  },

  /**
   * Synchronously creates the schema in the database.
   */
  _createSchema: function AD__createSchema() {
    LOG("Creating database schema");
    this.connection.beginTransaction();

    // Any errors in here should rollback
    try {
      this.connection.createTable("addon",
                                  "internal_id INTEGER PRIMARY KEY AUTOINCREMENT, " +
                                  "id TEXT UNIQUE, " +
                                  "type TEXT, " +
                                  "name TEXT, " +
                                  "version TEXT, " +
                                  "creator TEXT, " +
                                  "creatorURL TEXT, " +
                                  "description TEXT, " +
                                  "fullDescription TEXT, " +
                                  "developerComments TEXT, " +
                                  "eula TEXT, " +
                                  "iconURL TEXT, " +
                                  "homepageURL TEXT, " +
                                  "supportURL TEXT, " +
                                  "contributionURL TEXT, " +
                                  "contributionAmount TEXT, " +
                                  "averageRating INTEGER, " +
                                  "reviewCount INTEGER, " +
                                  "reviewURL TEXT, " +
                                  "totalDownloads INTEGER, " +
                                  "weeklyDownloads INTEGER, " +
                                  "dailyUsers INTEGER, " +
                                  "sourceURI TEXT, " +
                                  "repositoryStatus INTEGER, " +
                                  "size INTEGER, " +
                                  "updateDate INTEGER");

      this.connection.createTable("developer",
                                  "addon_internal_id INTEGER, " +
                                  "num INTEGER, " +
                                  "name TEXT, " +
                                  "url TEXT, " +
                                  "PRIMARY KEY (addon_internal_id, num)");

      this.connection.createTable("screenshot",
                                  "addon_internal_id INTEGER, " +
                                  "num INTEGER, " +
                                  "url TEXT, " +
                                  "thumbnailURL TEXT, " +
                                  "caption TEXT, " +
                                  "PRIMARY KEY (addon_internal_id, num)");

      this.connection.executeSimpleSQL("CREATE TRIGGER delete_addon AFTER DELETE " +
        "ON addon BEGIN " +
        "DELETE FROM developer WHERE addon_internal_id=old.internal_id; " +
        "DELETE FROM screenshot WHERE addon_internal_id=old.internal_id; " +
        "END");

      this.connection.schemaVersion = DB_SCHEMA;
      this.connection.commitTransaction();
    } catch (e) {
      ERROR("Failed to create database schema");
      this.logSQLError(this.connection.lastError, this.connection.lastErrorString);
      this.connection.rollbackTransaction();
      throw e;
    }
  }
};
