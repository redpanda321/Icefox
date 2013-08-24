# -*- Mode: Java; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
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
# The Original Code is The Extension Manager.
# 
# The Initial Developer of the Original Code is Ben Goodger. 
# Portions created by the Initial Developer are Copyright (C) 2004
# the Initial Developer. All Rights Reserved.
# 
# Contributor(s):
#   Ben Goodger <ben@bengoodger.com>
#   Blair McBride <bmcbride@mozilla.com>
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

function init() {
  var addon = window.arguments[0];
  var extensionsStrings = document.getElementById("extensionsStrings");

  document.title = extensionsStrings.getFormattedString("aboutWindowTitle", [addon.name]);
  var extensionName = document.getElementById("extensionName");
  extensionName.setAttribute("value", addon.name);

  var extensionVersion = document.getElementById("extensionVersion");
  if (addon.version)
    extensionVersion.setAttribute("value", extensionsStrings.getFormattedString("aboutWindowVersionString", [addon.version]));
  else
    extensionVersion.hidden = true;

  var extensionDescription = document.getElementById("extensionDescription");
  if (addon.description)
    extensionDescription.textContent = addon.description;
  else
    extensionDescription.hidden = true;

  var extensionCreator = document.getElementById("extensionCreator");
  extensionCreator.setAttribute("value", addon.creator);

  var extensionHomepage = document.getElementById("extensionHomepage");
  var homepageURL = addon.homepageURL;
  if (homepageURL) {
    extensionHomepage.setAttribute("homepageURL", homepageURL);
    extensionHomepage.setAttribute("tooltiptext", homepageURL);
  } else {
    extensionHomepage.hidden = true;
  }

  appendToList("extensionDevelopers", "developersBox", addon.developers);
  appendToList("extensionTranslators", "translatorsBox", addon.translators);
  appendToList("extensionContributors", "contributorsBox", addon.contributors);

  var acceptButton = document.documentElement.getButton("accept");
  acceptButton.label = extensionsStrings.getString("aboutWindowCloseButton");
}

function appendToList(aHeaderId, aNodeId, aItems) {
  var header = document.getElementById(aHeaderId);
  var node = document.getElementById(aNodeId);

  if (!aItems || aItems.length == 0) {
    header.hidden = true;
    return;
  }

  for (let i = 0; i < aItems.length; i++) {
    var label = document.createElement("label");
    label.setAttribute("value", aItems[i]);
    label.setAttribute("class", "contributor");
    node.appendChild(label);
  }
}

function loadHomepage(aEvent) {
  window.close();
  openURL(aEvent.target.getAttribute("homepageURL"));
}
