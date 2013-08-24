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
# The Original Code is The Browser Profile Migrator.
#
# The Initial Developer of the Original Code is Ben Goodger.
# Portions created by the Initial Developer are Copyright (C) 2004
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Ben Goodger <ben@bengoodger.com>
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

const kIMig = Components.interfaces.nsIBrowserProfileMigrator;
const kIPStartup = Components.interfaces.nsIProfileStartup;
const kProfileMigratorContractIDPrefix = "@mozilla.org/profile/migrator;1?app=browser&type=";

var MigrationWizard = {
  _source: "",                  // Source Profile Migrator ContractID suffix
  _itemsFlags: kIMig.ALL,       // Selected Import Data Sources (16-bit bitfield)
  _selectedProfile: null,       // Selected Profile name to import from
  _wiz: null,
  _migrator: null,
  _autoMigrate: null,
  _bookmarks: false,

  init: function ()
  {
    var os = Components.classes["@mozilla.org/observer-service;1"]
                       .getService(Components.interfaces.nsIObserverService);
    os.addObserver(this, "Migration:Started", false);
    os.addObserver(this, "Migration:ItemBeforeMigrate", false);
    os.addObserver(this, "Migration:ItemAfterMigrate", false);
    os.addObserver(this, "Migration:Ended", false);

    this._wiz = document.documentElement;

    if ("arguments" in window && window.arguments.length > 1) {
      this._source = window.arguments[0];
      this._migrator = window.arguments[1].QueryInterface(kIMig);
      this._autoMigrate = window.arguments[2].QueryInterface(kIPStartup);

      if (this._autoMigrate) {
        // Show the "nothing" option in the automigrate case to provide an
        // easily identifiable way to avoid migration and create a new profile.
        var nothing = document.getElementById("nothing");
        nothing.hidden = false;
      }
    }

    this.onImportSourcePageShow();
  },

  uninit: function ()
  {
    var os = Components.classes["@mozilla.org/observer-service;1"]
                       .getService(Components.interfaces.nsIObserverService);
    os.removeObserver(this, "Migration:Started");
    os.removeObserver(this, "Migration:ItemBeforeMigrate");
    os.removeObserver(this, "Migration:ItemAfterMigrate");
    os.removeObserver(this, "Migration:Ended");
  },

  // 1 - Import Source
  onImportSourcePageShow: function ()
  {
    // Reference to the "From File" radio button 
    var fromfile = null;

    //XXXquark This function is called before init, so check for bookmarks here
    if ("arguments" in window && window.arguments[0] == "bookmarks") {
      this._bookmarks = true;

      fromfile = document.getElementById("fromfile");
      fromfile.hidden = false;

      var importBookmarks = document.getElementById("importBookmarks");
      importBookmarks.hidden = false;

      var importAll = document.getElementById("importAll");
      importAll.hidden = true;
    }

    this._wiz.canRewind = false;

    // The migrator to select. If the "fromfile" migrator is available, use it
    // as the default in case we have no other migrators.
    var selectedMigrator = fromfile;

    // Figure out what source apps are are available to import from:
    var group = document.getElementById("importSourceGroup");
    for (var i = 0; i < group.childNodes.length; ++i) {
      var suffix = group.childNodes[i].id;
      if (suffix != "nothing" && suffix != "fromfile") {
        var contractID = kProfileMigratorContractIDPrefix + suffix;
        try {
          var migrator = Components.classes[contractID].createInstance(kIMig);
        }
        catch (e) {
          dump("*** invalid contractID =" + contractID + "\n");
          return;
        }

        if (migrator.sourceExists &&
            !(suffix == "phoenix" && !this._autoMigrate)) {
          // Save this as the first selectable item, if we don't already have
          // one, or if it is the migrator that was passed to us.
          if (!selectedMigrator || this._source == suffix)
            selectedMigrator = group.childNodes[i];
        } else {
          // Hide this option
          group.childNodes[i].hidden = true;
        }
      }
    }

    if (selectedMigrator)
      group.selectedItem = selectedMigrator;
    else {
      // We didn't find a migrator, notify the user
      document.getElementById("noSources").hidden = false;

      this._wiz.canAdvance = false;

      document.getElementById("importBookmarks").hidden = true;
      document.getElementById("importAll").hidden = true;
    }
  },
  
  onImportSourcePageAdvanced: function ()
  {
    var newSource = document.getElementById("importSourceGroup").selectedItem.id;
    
    if (newSource == "nothing" || newSource == "fromfile") {
      if(newSource == "fromfile")
        window.opener.fromFile = true;
      document.documentElement.cancel();
      return false;
    }
    
    if (!this._migrator || (newSource != this._source)) {
      // Create the migrator for the selected source.
      var contractID = kProfileMigratorContractIDPrefix + newSource;
      this._migrator = Components.classes[contractID].createInstance(kIMig);

      this._itemsFlags = kIMig.ALL;
      this._selectedProfile = null;
    }
    this._source = newSource;
      
    // check for more than one source profile
    if (this._migrator.sourceHasMultipleProfiles)
      this._wiz.currentPage.next = "selectProfile";
    else {
      if (this._autoMigrate)
        this._wiz.currentPage.next = "homePageImport";
      else if (this._bookmarks)
        this._wiz.currentPage.next = "migrating"
      else
        this._wiz.currentPage.next = "importItems";

      var sourceProfiles = this._migrator.sourceProfiles;
      if (sourceProfiles && sourceProfiles.Count() == 1) {
        var profileName = sourceProfiles.QueryElementAt(0, Components.interfaces.nsISupportsString);
        this._selectedProfile = profileName.data;
      }
      else
        this._selectedProfile = "";
    }
  },
  
  // 2 - [Profile Selection]
  onSelectProfilePageShow: function ()
  {
    // Disabling this for now, since we ask about import sources in automigration
    // too and don't want to disable the back button
    // if (this._autoMigrate)
    //   document.documentElement.getButton("back").disabled = true;
      
    var profiles = document.getElementById("profiles");
    while (profiles.hasChildNodes()) 
      profiles.removeChild(profiles.firstChild);
    
    // Note that this block is still reached even if the user chose 'From File'
    // and we canceled the dialog.  When that happens, _migrator will be null.
    if (this._migrator) {
      var sourceProfiles = this._migrator.sourceProfiles;
      var count = sourceProfiles.Count();
      for (var i = 0; i < count; ++i) {
        var item = document.createElement("radio");
        var str = sourceProfiles.QueryElementAt(i, Components.interfaces.nsISupportsString);
        item.id = str.data;
        item.setAttribute("label", str.data);
        profiles.appendChild(item);
      }
    }
    
    profiles.selectedItem = this._selectedProfile ? document.getElementById(this._selectedProfile) : profiles.firstChild;
  },
  
  onSelectProfilePageRewound: function ()
  {
    var profiles = document.getElementById("profiles");
    this._selectedProfile = profiles.selectedItem.id;
  },
  
  onSelectProfilePageAdvanced: function ()
  {
    var profiles = document.getElementById("profiles");
    this._selectedProfile = profiles.selectedItem.id;
    
    // If we're automigrating or just doing bookmarks don't show the item selection page
    if (this._autoMigrate)
      this._wiz.currentPage.next = "homePageImport";
    else if (this._bookmarks)
      this._wiz.currentPage.next = "migrating"
  },
  
  // 3 - ImportItems
  onImportItemsPageShow: function ()
  {
    var dataSources = document.getElementById("dataSources");
    while (dataSources.hasChildNodes())
      dataSources.removeChild(dataSources.firstChild);
    
    var bundle = document.getElementById("bundle");
    
    var items = this._migrator.getMigrateData(this._selectedProfile, this._autoMigrate);
    for (var i = 0; i < 16; ++i) {
      var itemID = (items >> i) & 0x1 ? Math.pow(2, i) : 0;
      if (itemID > 0) {
        var checkbox = document.createElement("checkbox");
        checkbox.id = itemID;
        checkbox.setAttribute("label", bundle.getString(itemID + "_" + this._source));
        dataSources.appendChild(checkbox);
        if (!this._itemsFlags || this._itemsFlags & itemID)
          checkbox.checked = true;
      }
    }
  },

  onImportItemsPageRewound: function ()
  {
    this._wiz.canAdvance = true;
    this.onImportItemsPageAdvanced();
  },

  onImportItemsPageAdvanced: function ()
  {
    var dataSources = document.getElementById("dataSources");
    this._itemsFlags = 0;
    for (var i = 0; i < dataSources.childNodes.length; ++i) {
      var checkbox = dataSources.childNodes[i];
      if (checkbox.localName == "checkbox" && checkbox.checked)
        this._itemsFlags |= parseInt(checkbox.id);
    }
  },
  
  onImportItemCommand: function (aEvent)
  {
    var items = document.getElementById("dataSources");
    var checkboxes = items.getElementsByTagName("checkbox");

    var oneChecked = false;
    for (var i = 0; i < checkboxes.length; ++i) {
      if (checkboxes[i].checked) {
        oneChecked = true;
        break;
      }
    }

    this._wiz.canAdvance = oneChecked;
  },

  // 4 - Home Page Selection
  onHomePageMigrationPageShow: function ()
  {
    // only want this on the first run
    if (!this._autoMigrate) {
      this._wiz.advance();
      return;
    }

    var bundle = document.getElementById("brandBundle");
    // These strings don't exist when not using official branding. If that's
    // the case, just skip this page.
    try {
      var pageTitle = bundle.getString("homePageMigrationPageTitle");
      var pageDesc = bundle.getString("homePageMigrationDescription");
      var mainStr = bundle.getString("homePageSingleStartMain");
    }
    catch (e) {
      this._wiz.advance();
      return;
    }

    document.getElementById("homePageImport").setAttribute("label", pageTitle);
    document.getElementById("homePageImportDesc").setAttribute("value", pageDesc);

    this._wiz._adjustWizardHeader();

    var singleStart = document.getElementById("homePageSingleStart");
    singleStart.setAttribute("label", mainStr);
    singleStart.setAttribute("value", "DEFAULT");

    var source = null;
    switch (this._source) {
      case "ie":
      case "macie":
        source = "sourceNameIE";
        break;
      case "opera":
        source = "sourceNameOpera";
        break;
      case "dogbert":
        source = "sourceNameDogbert";
        break;
      case "safari":
        source = "sourceNameSafari";
        break;
      case "seamonkey":
        source = "sourceNameSeamonkey";
        break;
    }

    // semi-wallpaper for crash when multiple profiles exist, since we haven't initialized mSourceProfile in places
    this._migrator.getMigrateData(this._selectedProfile, this._autoMigrate);

    var oldHomePageURL = this._migrator.sourceHomePageURL;

    if (oldHomePageURL && source) {
      var bundle2 = document.getElementById("bundle");
      var appName = bundle2.getString(source);
      var oldHomePageLabel = bundle.getFormattedString("homePageImport",
                                                       [appName]);
      var oldHomePage = document.getElementById("oldHomePage");
      oldHomePage.setAttribute("label", oldHomePageLabel);
      oldHomePage.setAttribute("value", oldHomePageURL);
      oldHomePage.removeAttribute("hidden");
    }
    else {
      // if we don't have at least two options, just advance
      this._wiz.advance();
    }
  },

  onHomePageMigrationPageAdvanced: function ()
  {
    // we might not have a selectedItem if we're in fallback mode
    try {
      var radioGroup = document.getElementById("homePageRadiogroup");

      this._newHomePage = radioGroup.selectedItem.value;
    } catch(ex) {}
  },

  // 5 - Migrating
  onMigratingPageShow: function ()
  {
    this._wiz.getButton("cancel").disabled = true;
    this._wiz.canRewind = false;
    this._wiz.canAdvance = false;
    
    // When automigrating, show all of the data that can be received from this source.
    if (this._autoMigrate)
      this._itemsFlags = this._migrator.getMigrateData(this._selectedProfile, this._autoMigrate);

    // When importing bookmarks, show only bookmarks
    if (this._bookmarks)
      this._itemsFlags = 32;

    this._listItems("migratingItems");
    setTimeout(this.onMigratingMigrate, 0, this);
  },

  onMigratingMigrate: function (aOuter)
  {
    aOuter._migrator.migrate(aOuter._itemsFlags, aOuter._autoMigrate, aOuter._selectedProfile);
  },
  
  _listItems: function (aID)
  {
    var items = document.getElementById(aID);
    while (items.hasChildNodes())
      items.removeChild(items.firstChild);
    
    var bundle = document.getElementById("bundle");
    var brandBundle = document.getElementById("brandBundle");
    var itemID;
    for (var i = 0; i < 16; ++i) {
      var itemID = (this._itemsFlags >> i) & 0x1 ? Math.pow(2, i) : 0;
      if (itemID > 0) {
        var label = document.createElement("label");
        label.id = itemID + "_migrated";
        try {
          label.setAttribute("value", bundle.getString(itemID + "_" + this._source));
          items.appendChild(label);
        }
        catch (e) {
          // if the block above throws, we've enumerated all the import data types we
          // currently support and are now just wasting time, break. 
          break;
        }
      }
    }
  },
  
  observe: function (aSubject, aTopic, aData)
  {
    switch (aTopic) {
    case "Migration:Started":
      break;
    case "Migration:ItemBeforeMigrate":
      var label = document.getElementById(aData + "_migrated");
      if (label)
        label.setAttribute("style", "font-weight: bold");
      break;
    case "Migration:ItemAfterMigrate":
      var label = document.getElementById(aData + "_migrated");
      if (label)
        label.removeAttribute("style");
      break;
    case "Migration:Ended":
      if (this._autoMigrate) {
        if (this._newHomePage) {
          try {
            // set homepage properly
            var prefSvc = Components.classes["@mozilla.org/preferences-service;1"]
                                    .getService(Components.interfaces.nsIPrefService);
            var prefBranch = prefSvc.getBranch(null);

            if (this._newHomePage == "DEFAULT") {
              try {
                prefBranch.clearUserPref("browser.startup.homepage");
              }
              catch (e) { }
            }
            else {
              var str = Components.classes["@mozilla.org/supports-string;1"]
                                .createInstance(Components.interfaces.nsISupportsString);
              str.data = this._newHomePage;
              prefBranch.setComplexValue("browser.startup.homepage",
                                         Components.interfaces.nsISupportsString,
                                         str);
            }

            var dirSvc = Components.classes["@mozilla.org/file/directory_service;1"]
                                   .getService(Components.interfaces.nsIProperties);
            var prefFile = dirSvc.get("ProfDS", Components.interfaces.nsIFile);
            prefFile.append("prefs.js");
            prefSvc.savePrefFile(prefFile);
          } catch(ex) { 
            dump(ex); 
          }
        }

        // We're done now.
        this._wiz.canAdvance = true;
        this._wiz.advance();

        setTimeout(close, 5000);
      }
      else {
        this._wiz.canAdvance = true;
        var nextButton = this._wiz.getButton("next");
        nextButton.click();
      }
      break;
    }
  },

  onDonePageShow: function ()
  {
    this._wiz.getButton("cancel").disabled = true;
    this._wiz.canRewind = false;
    this._listItems("doneItems");
  }
};
