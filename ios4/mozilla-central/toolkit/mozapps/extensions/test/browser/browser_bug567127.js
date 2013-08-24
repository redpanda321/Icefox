/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Tests bug 567127 - Add install button to the add-ons manager


var gFilePickerFiles = [];
var gMockFilePickerFactory;
var gMockFilePickerFactoryCID;
var gManagerWindow;

function MockFilePicker() { }

MockFilePicker.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Components.interfaces.nsIFilePicker]),
  init: function(aParent, aTitle, aMode) { },
  appendFilters: function(aFilterMask) { },
  appendFilter: function(aTitle, aFilter) { },
  defaultString: "",
  defaultExtension: "",
  filterIndex: 0,
  displayDirectory: null,
  file: null,
  fileURL: null,
  get files() {
    var i = 0;
    return {
      getNext: function() gFilePickerFiles[i++],
      hasMoreElements: function() gFilePickerFiles.length > i
    };
  },
  show: function() {
    return gFilePickerFiles.length == 0 ?
           Components.interfaces.nsIFilePicker.returnCancel :
           Components.interfaces.nsIFilePicker.returnOK;
  }
};

// This listens for the next opened window and checks it is of the right url.
// opencallback is called when the new window is fully loaded
// closecallback is called when the window is closed
function WindowOpenListener(url, opencallback, closecallback) {
  this.url = url;
  this.opencallback = opencallback;
  this.closecallback = closecallback;

  var wm = Components.classes["@mozilla.org/appshell/window-mediator;1"]
                     .getService(Components.interfaces.nsIWindowMediator);
  wm.addListener(this);
}

WindowOpenListener.prototype = {
  url: null,
  opencallback: null,
  closecallback: null,
  window: null,
  domwindow: null,

  handleEvent: function(event) {
    is(this.domwindow.document.location.href, this.url, "Should have opened the correct window");

    this.domwindow.removeEventListener("load", this, false);
    // Allow any other load handlers to execute
    var self = this;
    executeSoon(function() { self.opencallback(self.domwindow); } );
  },

  onWindowTitleChange: function(window, title) {
  },

  onOpenWindow: function(window) {
    if (this.window)
      return;

    this.window = window;
    this.domwindow = window.QueryInterface(Components.interfaces.nsIInterfaceRequestor)
                           .getInterface(Components.interfaces.nsIDOMWindowInternal);
    this.domwindow.addEventListener("load", this, false);
  },

  onCloseWindow: function(window) {
    if (this.window != window)
      return;

    var wm = Components.classes["@mozilla.org/appshell/window-mediator;1"]
                       .getService(Components.interfaces.nsIWindowMediator);
    wm.removeListener(this);
    this.opencallback = null;
    this.window = null;
    this.domwindow = null;

    // Let the window close complete
    executeSoon(this.closecallback);
    this.closecallback = null;
  }
};


function test_confirmation(aWindow, aExpectedURLs) {
  var list = aWindow.document.getElementById("itemList");
  is(list.childNodes.length, aExpectedURLs.length, "Should be the right number of installs");

  aExpectedURLs.forEach(function(aURL) {
    var node = list.firstChild;
    while (node) {
      if (node.url == aURL) {
        ok(true, "Should have seen " + aURL + " in the list");
        return;
      }
      node = node.nextSibling;
    }
    ok(false, "Should have seen " + aURL + " in the list");
  });

  aWindow.document.documentElement.cancelDialog();
}


function test() {
  waitForExplicitFinish();
  
  gMockFilePickerFactoryCID = Components.ID("{4f595df2-9108-42c6-9910-0dc392a310c9}");
  gMockFilePickerFactory = XPCOMUtils._getFactory(MockFilePicker);
  var compReg = Components.manager.QueryInterface(Components.interfaces.nsIComponentRegistrar);
  compReg.registerFactory(gMockFilePickerFactoryCID,
                          "Mock FilePicker",
                          "@mozilla.org/filepicker;1",
                          gMockFilePickerFactory);

  open_manager(null, function(aWindow) {
    gManagerWindow = aWindow;
    run_next_test();
  });
}

function end_test() {
  var compReg = Components.manager.QueryInterface(Components.interfaces.nsIComponentRegistrar);
  compReg.unregisterFactory(gMockFilePickerFactoryCID,
                            gMockFilePickerFactory);
  close_manager(gManagerWindow, function() {
    finish();
  });
}


add_test(function() {
  var filePaths = [
                   get_addon_file_url("browser_bug567127_1.xpi"),
                   get_addon_file_url("browser_bug567127_2.xpi")
                  ];
  gFilePickerFiles = filePaths.map(function(aPath) aPath.file);
  
  new WindowOpenListener(INSTALL_URI, function(aWindow) {
    test_confirmation(aWindow, filePaths.map(function(aPath) aPath.spec));
  }, run_next_test);
  
  gManagerWindow.gViewController.doCommand("cmd_installFromFile");
});
