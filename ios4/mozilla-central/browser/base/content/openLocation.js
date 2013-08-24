# -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
#
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
# The Original Code is Mozilla Communicator client code, released
# March 31, 1998.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 1998
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Michael Lowe <michael.lowe@bigfoot.com>
#   Blake Ross   <blaker@netscape.com>
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

var browser;
var dialog = {};
var pref = null;
try {
  pref = Components.classes["@mozilla.org/preferences-service;1"]
                   .getService(Components.interfaces.nsIPrefBranch);
} catch (ex) {
  // not critical, remain silent
}

Components.utils.import("resource:///modules/openLocationLastURL.jsm");

function onLoad()
{
  dialog.input         = document.getElementById("dialog.input");
  dialog.open          = document.documentElement.getButton("accept");
  dialog.openWhereList = document.getElementById("openWhereList");
  dialog.openTopWindow = document.getElementById("currentWindow");
  dialog.bundle        = document.getElementById("openLocationBundle");

  if ("arguments" in window && window.arguments.length >= 1)
    browser = window.arguments[0];
   
  dialog.openWhereList.selectedItem = dialog.openTopWindow;

  if (pref) {
    try {
      var useAutoFill = pref.getBoolPref("browser.urlbar.autoFill");
      if (useAutoFill)
        dialog.input.setAttribute("completedefaultindex", "true");
    } catch (ex) {}

    try {
      var value = pref.getIntPref("general.open_location.last_window_choice");
      var element = dialog.openWhereList.getElementsByAttribute("value", value)[0];
      if (element)
        dialog.openWhereList.selectedItem = element;
      dialog.input.value = gOpenLocationLastURL.value;
    }
    catch(ex) {
    }
    if (dialog.input.value)
      dialog.input.select(); // XXX should probably be done automatically
  }

  doEnabling();
}

function doEnabling()
{
    dialog.open.disabled = !dialog.input.value;
}

function open()
{
  var url;
  var postData = {};
  if (browser)
    url = browser.getShortcutOrURI(dialog.input.value, postData);
  else
    url = dialog.input.value;

  try {
    // Whichever target we use for the load, we allow third-party services to
    // fixup the URI
    switch (dialog.openWhereList.value) {
      case "0":
        browser.loadURI(url, null, postData.value, true);
        break;
      case "1":
        window.opener.delayedOpenWindow(getBrowserURL(), "all,dialog=no",
                                        url, postData.value, null, null, true);
        break;
      case "3":
        browser.delayedOpenTab(url, null, null, postData.value, true);
        break;
    }
  }
  catch(exception) {
  }

  if (pref) {
    gOpenLocationLastURL.value = dialog.input.value;
    pref.setIntPref("general.open_location.last_window_choice", dialog.openWhereList.value);
  }

  // Delay closing slightly to avoid timing bug on Linux.
  window.close();
  return false;
}

function createInstance(contractid, iidName)
{
  var iid = Components.interfaces[iidName];
  return Components.classes[contractid].createInstance(iid);
}

const nsIFilePicker = Components.interfaces.nsIFilePicker;
function onChooseFile()
{
  try {
    var fp = Components.classes["@mozilla.org/filepicker;1"].createInstance(nsIFilePicker);
    fp.init(window, dialog.bundle.getString("chooseFileDialogTitle"), nsIFilePicker.modeOpen);
    fp.appendFilters(nsIFilePicker.filterHTML | nsIFilePicker.filterText |
                     nsIFilePicker.filterAll | nsIFilePicker.filterImages | nsIFilePicker.filterXML);

    if (fp.show() == nsIFilePicker.returnOK && fp.fileURL.spec && fp.fileURL.spec.length > 0)
      dialog.input.value = fp.fileURL.spec;
  }
  catch(ex) {
  }
  doEnabling();
}
