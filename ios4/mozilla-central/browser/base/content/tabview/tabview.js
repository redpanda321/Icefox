const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/tabview/AllTabs.jsm");
Cu.import("resource://gre/modules/tabview/groups.jsm");
Cu.import("resource://gre/modules/tabview/utils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyGetter(this, "gWindow", function() {
  return window.QueryInterface(Ci.nsIInterfaceRequestor).
    getInterface(Ci.nsIWebNavigation).
    QueryInterface(Ci.nsIDocShell).
    chromeEventHandler.ownerDocument.defaultView;
});

XPCOMUtils.defineLazyGetter(this, "gBrowser", function() gWindow.gBrowser);

XPCOMUtils.defineLazyGetter(this, "gTabViewDeck", function() {
  return gWindow.document.getElementById("tab-view-deck");
});

XPCOMUtils.defineLazyGetter(this, "gTabViewFrame", function() {
  return gWindow.document.getElementById("tab-view");
});

# NB: Certain files need to evaluate before others

#include iq.js
#include storage.js
#include items.js
#include groupitems.js
#include tabitems.js
#include drag.js
#include trench.js
#include infoitems.js
#include ui.js
