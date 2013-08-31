/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;

this.EXPORTED_SYMBOLS = [ ];

Cu.import("resource:///modules/devtools/gcli.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "LayoutHelpers",
                                  "resource:///modules/devtools/LayoutHelpers.jsm");

// String used as an indication to generate default file name in the following
// format: "Screen Shot yyyy-mm-dd at HH.MM.SS.png"
const FILENAME_DEFAULT_VALUE = " ";

/**
 * 'screenshot' command
 */
gcli.addCommand({
  name: "screenshot",
  description: gcli.lookup("screenshotDesc"),
  manual: gcli.lookup("screenshotManual"),
  returnType: "html",
  params: [
    {
      name: "filename",
      type: "string",
      defaultValue: FILENAME_DEFAULT_VALUE,
      description: gcli.lookup("screenshotFilenameDesc"),
      manual: gcli.lookup("screenshotFilenameManual")
    },
    {
      group: gcli.lookup("screenshotGroupOptions"),
      params: [
        {
          name: "clipboard",
          type: "boolean",
          description: gcli.lookup("screenshotClipboardDesc"),
          manual: gcli.lookup("screenshotClipboardManual")
        },
        {
          name: "chrome",
          type: "boolean",
          description: gcli.lookup("screenshotChromeDesc"),
          manual: gcli.lookup("screenshotChromeManual")
        },
        {
          name: "delay",
          type: { name: "number", min: 0 },
          defaultValue: 0,
          description: gcli.lookup("screenshotDelayDesc"),
          manual: gcli.lookup("screenshotDelayManual")
        },
        {
          name: "fullpage",
          type: "boolean",
          description: gcli.lookup("screenshotFullPageDesc"),
          manual: gcli.lookup("screenshotFullPageManual")
        },
        {
          name: "selector",
          type: "node",
          defaultValue: null,
          description: gcli.lookup("inspectNodeDesc"),
          manual: gcli.lookup("inspectNodeManual")
        }
      ]
    }
  ],
  exec: function Command_screenshot(args, context) {
    if (args.chrome && args.selector) {
      // Node screenshot with chrome option does not work as inteded
      // Refer https://bugzilla.mozilla.org/show_bug.cgi?id=659268#c7
      // throwing for now.
      throw new Error(gcli.lookup("screenshotSelectorChromeConflict"));
    }
    var document = args.chrome? context.environment.chromeDocument
                              : context.environment.contentDocument;
    if (args.delay > 0) {
      var promise = context.createPromise();
      document.defaultView.setTimeout(function Command_screenshotDelay() {
        let reply = this.grabScreen(document, args.filename, args.clipboard,
                                    args.fullpage);
        promise.resolve(reply);
      }.bind(this), args.delay * 1000);
      return promise;
    }
    else {
      return this.grabScreen(document, args.filename, args.clipboard,
                             args.fullpage, args.selector);
    }
  },
  grabScreen:
  function Command_screenshotGrabScreen(document, filename, clipboard,
                                        fullpage, node) {
    let window = document.defaultView;
    let canvas = document.createElementNS("http://www.w3.org/1999/xhtml", "canvas");
    let left = 0;
    let top = 0;
    let width;
    let height;
    let div = document.createElementNS("http://www.w3.org/1999/xhtml", "div");

    if (!fullpage) {
      if (!node) {
        left = window.scrollX;
        top = window.scrollY;
        width = window.innerWidth;
        height = window.innerHeight;
      } else {
        let rect = LayoutHelpers.getRect(node, window);
        top = rect.top;
        left = rect.left;
        width = rect.width;
        height = rect.height;
      }
    } else {
      width = window.innerWidth + window.scrollMaxX;
      height = window.innerHeight + window.scrollMaxY;
    }
    canvas.width = width;
    canvas.height = height;

    let ctx = canvas.getContext("2d");
    ctx.drawWindow(window, left, top, width, height, "#fff");
    let data = canvas.toDataURL("image/png", "");

    let loadContext = document.defaultView
                              .QueryInterface(Ci.nsIInterfaceRequestor)
                              .getInterface(Ci.nsIWebNavigation)
                              .QueryInterface(Ci.nsILoadContext);

    try {
      if (clipboard) {
        let io = Cc["@mozilla.org/network/io-service;1"]
                   .getService(Ci.nsIIOService);
        let channel = io.newChannel(data, null, null);
        let input = channel.open();
        let imgTools = Cc["@mozilla.org/image/tools;1"]
                         .getService(Ci.imgITools);

        let container = {};
        imgTools.decodeImageData(input, channel.contentType, container);

        let wrapped = Cc["@mozilla.org/supports-interface-pointer;1"]
                        .createInstance(Ci.nsISupportsInterfacePointer);
        wrapped.data = container.value;

        let trans = Cc["@mozilla.org/widget/transferable;1"]
                      .createInstance(Ci.nsITransferable);
        trans.init(loadContext);
        trans.addDataFlavor(channel.contentType);
        trans.setTransferData(channel.contentType, wrapped, -1);

        let clipid = Ci.nsIClipboard;
        let clip = Cc["@mozilla.org/widget/clipboard;1"].getService(clipid);
        clip.setData(trans, null, clipid.kGlobalClipboard);
        div.textContent = gcli.lookup("screenshotCopied");
        return div;
      }
    }
    catch (ex) {
      div.textContent = gcli.lookup("screenshotErrorCopying");
      return div;
    }

    let file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsILocalFile);

    // Create a name for the file if not present
    if (filename == FILENAME_DEFAULT_VALUE) {
      let date = new Date();
      let dateString = date.getFullYear() + "-" + (date.getMonth() + 1) +
                       "-" + date.getDate();
      dateString = dateString.split("-").map(function(part) {
        if (part.length == 1) {
          part = "0" + part;
        }
        return part;
      }).join("-");
      let timeString = date.toTimeString().replace(/:/g, ".").split(" ")[0];
      filename = gcli.lookupFormat("screenshotGeneratedFilename",
                                   [dateString, timeString]) + ".png";
    }
    // Check there is a .png extension to filename
    else if (!filename.match(/.png$/i)) {
      filename += ".png";
    }

    // If the filename is relative, tack it onto the download directory
    if (!filename.match(/[\\\/]/)) {
      let downloadMgr = Cc["@mozilla.org/download-manager;1"]
                          .getService(Ci.nsIDownloadManager);
      let tempfile = downloadMgr.userDownloadsDirectory;
      tempfile.append(filename);
      filename = tempfile.path;
    }

    try {
      file.initWithPath(filename);
    } catch (ex) {
      div.textContent = gcli.lookup("screenshotErrorSavingToFile") + " " + filename;
      return div;
    }

    let ioService = Cc["@mozilla.org/network/io-service;1"]
                      .getService(Ci.nsIIOService);

    let Persist = Ci.nsIWebBrowserPersist;
    let persist = Cc["@mozilla.org/embedding/browser/nsWebBrowserPersist;1"]
                    .createInstance(Persist);
    persist.persistFlags = Persist.PERSIST_FLAGS_REPLACE_EXISTING_FILES |
                           Persist.PERSIST_FLAGS_AUTODETECT_APPLY_CONVERSION;

    let source = ioService.newURI(data, "UTF8", null);
    persist.saveURI(source, null, null, null, null, file, loadContext);

    div.textContent = gcli.lookup("screenshotSavedToFile") + " \"" + filename +
                      "\"";
    div.addEventListener("click", function openFile() {
      div.removeEventListener("click", openFile);
      file.reveal();
    });
    div.style.cursor = "pointer";
    let image = document.createElement("div");
    let previewHeight = parseInt(256*height/width);
    image.setAttribute("style",
                       "width:256px; height:" + previewHeight + "px;" +
                       "max-height: 256px;" +
                       "background-image: url('" + data + "');" +
                       "background-size: 256px " + previewHeight + "px;" +
                       "margin: 4px; display: block");
    div.appendChild(image);
    return div;
  }
});