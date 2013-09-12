let Cc = Components.classes;
let Ci = Components.interfaces;
let Cu = Components.utils;

dump("!! remote browser loaded\n\n");

Cu.import("resource://gre/modules/Geometry.jsm");

let Util = {
  /** printf-like dump function */
  dumpf: function dumpf(str) {
    let args = arguments;
    let i = 1;
    dump(str.replace(/%s/g, function() {
      if (i >= args.length) {
        throw "dumps received too many placeholders and not enough arguments";
      }
      return args[i++].toString();
    }));
  },

  /** Like dump, but each arg is handled and there's an automatic newline */
  dumpLn: function dumpLn() {
    for (let i = 0; i < arguments.length; i++)
      dump(arguments[i] + " ");
    dump("\n");
  },

  getWindowUtils: function getWindowUtils(aWindow) {
    return aWindow.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils);
  },

  getScrollOffset: function getScrollOffset(aWindow) {
    let cwu = Util.getWindowUtils(aWindow);
    let scrollX = {};
    let scrollY = {};
    cwu.getScrollXY(false, scrollX, scrollY);
    return new Point(scrollX.value, scrollY.value);
  },

  /** Executes aFunc after other events have been processed. */
  executeSoon: function executeSoon(aFunc) {
    Services.tm.mainThread.dispatch({
      run: function() {
        aFunc();
      }
    }, Ci.nsIThread.DISPATCH_NORMAL);
  },

  getHrefForElement: function getHrefForElement(target) {
    // XXX: This is kind of a hack to work around a Gecko bug (see bug 266932)
    // We're going to walk up the DOM looking for a parent link node.
    // This shouldn't be necessary, but we're matching the existing behaviour for left click

    let link = null;
    while (target) {
      if (target instanceof Ci.nsIDOMHTMLAnchorElement || 
          target instanceof Ci.nsIDOMHTMLAreaElement ||
          target instanceof Ci.nsIDOMHTMLLinkElement) {
          if (target.hasAttribute("href"))
            link = target;
      }
      target = target.parentNode;
    }

    if (link && link.hasAttribute("href"))
      return link.href;
    else
      return null;
  },

  makeURI: function makeURI(aURL, aOriginCharset, aBaseURI) {
    return Services.io.newURI(aURL, aOriginCharset, aBaseURI);
  },

  makeURLAbsolute: function makeURLAbsolute(base, url) {
    // Note:  makeURI() will throw if url is not a valid URI
    return this.makeURI(url, null, this.makeURI(base)).spec;
  },

  isLocalScheme: function isLocalScheme(aURL) {
    return (aURL.indexOf("about:") == 0 && aURL != "about:blank") || aURL.indexOf("chrome:") == 0;
  },

  isShareableScheme: function isShareableScheme(aProtocol) {
    let dontShare = /^(chrome|about|file|javascript|resource)$/;
    return (aProtocol && !dontShare.test(aProtocol));
  },

  clamp: function(num, min, max) {
    return Math.max(min, Math.min(max, num));
  },

  /**
   * Determines whether a home page override is needed.
   * Returns:
   *  "new profile" if this is the first run with a new profile.
   *  "new version" if this is the first run with a build with a different
   *                      Gecko milestone (i.e. right after an upgrade).
   *  "none" otherwise.
   */
  needHomepageOverride: function needHomepageOverride() {
    let savedmstone = null;
    try {
      savedmstone = Services.prefs.getCharPref("browser.startup.homepage_override.mstone");
    } catch (e) {}

    if (savedmstone == "ignore")
      return "none";

   let ourmstone = "2.0b1pre";

    if (ourmstone != savedmstone) {
      Services.prefs.setCharPref("browser.startup.homepage_override.mstone", ourmstone);

      return (savedmstone ? "new version" : "new profile");
    }

    return "none";
  },

  /** Don't display anything in the urlbar for these special URIs. */
  isURLEmpty: function isURLEmpty(aURL) {
    return (!aURL || aURL == "about:blank" || aURL == "about:empty" || aURL == "about:home");
  },

  /** Recursively find all documents, including root document. */
  getAllDocuments: function getAllDocuments(doc, resultSoFar) {
    resultSoFar = resultSoFar || [doc];
    if (!doc.defaultView)
      return resultSoFar;
    let frames = doc.defaultView.frames;
    if (!frames)
      return resultSoFar;

    let i;
    let currentDoc;
    for (i = 0; i < frames.length; i++) {
      currentDoc = frames[i].document;
      resultSoFar.push(currentDoc);
      this.getAllDocuments(currentDoc, resultSoFar);
    }

    return resultSoFar;
  },

  // Put the Mozilla networking code into a state that will kick the auto-connection
  // process.
  forceOnline: function forceOnline() {
//@line 192 "/home/nebel/mozilla-ios/mobile/chrome/content/Util.js"
  },

  isPortrait: function isPortrait() {
    return (window.innerWidth < 500);
  }
};


/**
 * Helper class to nsITimer that adds a little more pizazz.  Callback can be an
 * object with a notify method or a function.
 */
Util.Timeout = function(aCallback) {
  this._callback = aCallback;
  this._timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  this._type = null;
};

Util.Timeout.prototype = {
  /** Timer callback. Don't call this manually. */
  notify: function notify() {
    if (this._type == this._timer.TYPE_ONE_SHOT)
      this._type = null;

    if (this._callback.notify)
      this._callback.notify();
    else
      this._callback.apply(null);
  },

  /** Helper function for once and interval. */
  _start: function _start(aDelay, aType, aCallback) {
    if (aCallback)
      this._callback = aCallback;
    this.clear();
    this._timer.initWithCallback(this, aDelay, aType);
    this._type = aType;
    return this;
  },

  /** Do the callback once.  Cancels other timeouts on this object. */
  once: function once(aDelay, aCallback) {
    return this._start(aDelay, this._timer.TYPE_ONE_SHOT, aCallback);
  },

  /** Do the callback every aDelay msecs. Cancels other timeouts on this object. */
  interval: function interval(aDelay, aCallback) {
    return this._start(aDelay, this._timer.TYPE_REPEATING_SLACK, aCallback);
  },

  /** Clear any pending timeouts. */
  clear: function clear() {
    if (this._type !== null) {
      this._timer.cancel();
      this._type = null;
    }
    return this;
  },

  /** If there is a pending timeout, call it and cancel the timeout. */
  flush: function flush() {
    if (this._type) {
      this.notify();
      this.clear();
    }
    return this;
  },

  /** Return true iff we are waiting for a callback. */
  isPending: function isPending() {
    return !!this._type;
  }
};



let WebProgressListener = {
  _notifyFlags: [],
  _calculatedNotifyFlags: 0,

  init: function() {
    let webProgress = docShell.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIWebProgress);
    webProgress.addProgressListener(this, Ci.nsIWebProgress.NOTIFY_ALL);

    addMessageListener("WebProgress:AddProgressListener", this);
    addMessageListener("WebProgress:RemoveProgressListener", this);
  },

  onStateChange: function onStateChange(aWebProgress, aRequest, aStateFlags, aStatus) {
    let webProgress = Ci.nsIWebProgressListener;
    let notifyFlags = 0;
    if (aStateFlags & webProgress.STATE_IS_REQUEST)
      notifyFlags |= Ci.nsIWebProgress.NOTIFY_STATE_REQUEST;
    if (aStateFlags & webProgress.STATE_IS_DOCUMENT)
      notifyFlags |= Ci.nsIWebProgress.NOTIFY_STATE_DOCUMENT;
    if (aStateFlags & webProgress.STATE_IS_NETWORK)
      notifyFlags |= Ci.nsIWebProgress.NOTIFY_STATE_NETWORK;
    if (aStateFlags & webProgress.STATE_IS_WINDOW)
      notifyFlags |= Ci.nsIWebProgress.NOTIFY_STATE_WINDOW;

    let json = {
      windowId: aWebProgress.DOMWindow.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils).currentInnerWindowID,
      stateFlags: aStateFlags,
      status: aStatus,
      notifyFlags: notifyFlags
    };

    sendAsyncMessage("WebProgress:StateChange", json);
  },

  onProgressChange: function onProgressChange(aWebProgress, aRequest, aCurSelf, aMaxSelf, aCurTotal, aMaxTotal) {
    let json = {
      windowId: aWebProgress.DOMWindow.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils).currentInnerWindowID,
      curSelf: aCurSelf,
      maxSelf: aMaxSelf,
      curTotal: aCurTotal,
      maxTotal: aMaxTotal
    };

    if (this._calculatedNotifyFlags & Ci.nsIWebProgress.NOTIFY_PROGRESS)
      sendAsyncMessage("WebProgress:ProgressChange", json);
  },

  onLocationChange: function onLocationChange(aWebProgress, aRequest, aLocationURI) {
    let location = aLocationURI ? aLocationURI.spec : "";
    let json = {
      windowId: aWebProgress.DOMWindow.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils).currentInnerWindowID,
      documentURI: aWebProgress.DOMWindow.document.documentURIObject.spec,
      location: location,
      canGoBack: docShell.canGoBack,
      canGoForward: docShell.canGoForward
    };
    sendAsyncMessage("WebProgress:LocationChange", json);
  },

  onStatusChange: function onStatusChange(aWebProgress, aRequest, aStatus, aMessage) {
    let json = {
      windowId: aWebProgress.DOMWindow.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils).currentInnerWindowID,
      status: aStatus,
      message: aMessage
    };

    if (this._calculatedNotifyFlags & Ci.nsIWebProgress.NOTIFY_STATUS)
      sendAsyncMessage("WebProgress:StatusChange", json);
  },

  onSecurityChange: function onSecurityChange(aWebProgress, aRequest, aState) {
    let data = SecurityUI.getIdentityData();
    let status = {
      serverCert: data
    };

    let json = {
      windowId: aWebProgress.DOMWindow.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils).currentInnerWindowID,
      SSLStatus: status,
      state: aState
    };
    sendAsyncMessage("WebProgress:SecurityChange", json);
  },

  receiveMessage: function(aMessage) {
    switch (aMessage.name) {
      case "WebProgress:AddProgressListener":
        this._notifyFlags.push(aMessage.json.notifyFlags);
        this._calculatedNotifyFlags |= aMessage.json.notifyFlags;
        break;
      case "WebProgress.RemoveProgressListener":
        let index = this._notifyFlags.indexOf(aMessage.json.notifyFlags);
        if (index != -1) {
          this._notifyFlags.splice(index, 1);
          this._calculatedNotifyFlags = this._notifyFlags.reduce(function(a, b) {
            return a | b;
          }, 0);
        }
        break;
    }
  },

  QueryInterface: function QueryInterface(aIID) {
    if (aIID.equals(Ci.nsIWebProgressListener) ||
        aIID.equals(Ci.nsISupportsWeakReference) ||
        aIID.equals(Ci.nsISupports)) {
        return this;
    }

    throw Components.results.NS_ERROR_NO_INTERFACE;
  }
};

WebProgressListener.init();


let SecurityUI = {
  /**
   * Helper to parse out the important parts of the SSL cert for use in constructing
   * identity UI strings
   */
  getIdentityData: function() {
    let result = {};
    let status = docShell.securityUI.QueryInterface(Ci.nsISSLStatusProvider).SSLStatus;

    if (status) {
      status = status.QueryInterface(Ci.nsISSLStatus);
      let cert = status.serverCert;

      // Human readable name of Subject
      result.subjectOrg = cert.organization;

      // SubjectName fields, broken up for individual access
      if (cert.subjectName) {
        result.subjectNameFields = {};
        cert.subjectName.split(",").forEach(function(v) {
          var field = v.split("=");
          if (field[1])
            this[field[0]] = field[1];
        }, result.subjectNameFields);

        // Call out city, state, and country specifically
        result.city = result.subjectNameFields.L;
        result.state = result.subjectNameFields.ST;
        result.country = result.subjectNameFields.C;
      }

      // Human readable name of Certificate Authority
      result.caOrg =  cert.issuerOrganization || cert.issuerCommonName;

      if (!this._overrideService)
        this._overrideService = Cc["@mozilla.org/security/certoverride;1"].getService(Ci.nsICertOverrideService);

      // Check whether this site is a security exception. XPConnect does the right
      // thing here in terms of converting _lastLocation.port from string to int, but
      // the overrideService doesn't like undefined ports, so make sure we have
      // something in the default case (bug 432241).
      result.isException = !!this._overrideService.hasMatchingOverride(content.location.hostname, (content.location.port || 443), cert, {}, {});
    }

    return result;
  },

  init: function() {
    addMessageListener("SecurityUI:Init", this);
  },

  receiveMessage: function(aMessage) {
    const SECUREBROWSERUI_CONTRACTID = "@mozilla.org/secure_browser_ui;1";
    let securityUI = Cc[SECUREBROWSERUI_CONTRACTID].createInstance(Ci.nsISecureBrowserUI);
    securityUI.init(content);
  }
};

SecurityUI.init();


let WebNavigation =  {
  _webNavigation: docShell.QueryInterface(Ci.nsIWebNavigation),

  init: function() {
    addMessageListener("WebNavigation:GoBack", this);
    addMessageListener("WebNavigation:GoForward", this);
    addMessageListener("WebNavigation:GotoIndex", this);
    addMessageListener("WebNavigation:LoadURI", this);
    addMessageListener("WebNavigation:Reload", this);
    addMessageListener("WebNavigation:Stop", this);
  },

  receiveMessage: function(message) {
    switch (message.name) {
      case "WebNavigation:GoBack":
        this.goBack(message);
        break;
      case "WebNavigation:GoForward":
        this.goForward(message);
        break;
      case "WebNavigation:GotoIndex":
        this.gotoIndex(message);
        break;
      case "WebNavigation:LoadURI":
        this.loadURI(message);
        break;
      case "WebNavigation:Reload":
        this.reload(message);
        break;
      case "WebNavigation:Stop":
        this.stop(message);
        break;
    }
  },

  goBack: function() {
    this._webNavigation.goBack();
  },

  goForward: function() {
    this._webNavigation.goForward();
  },

  gotoIndex: function(message) {
    this._webNavigation.gotoIndex(message.index);
  },

  loadURI: function(message) {
    let flags = message.json.flags || this._webNavigation.LOAD_FLAGS_NONE;
    this._webNavigation.loadURI(message.json.uri, flags, null, null, null);
  },

  reload: function(message) {
    let flags = message.json.flags || this._webNavigation.LOAD_FLAGS_NONE;
    this._webNavigation.reload(flags);
  },

  stop: function(message) {
    let flags = message.json.flags || this._webNavigation.STOP_ALL;
    this._webNavigation.stop(flags);
  }
};

WebNavigation.init();


let DOMEvents =  {
  init: function() {
    addEventListener("DOMContentLoaded", this, false);
    addEventListener("DOMTitleChanged", this, false);
    addEventListener("DOMLinkAdded", this, false);
    addEventListener("DOMWillOpenModalDialog", this, false);
    addEventListener("DOMModalDialogClosed", this, true);
    addEventListener("DOMWindowClose", this, false);
    addEventListener("DOMPopupBlocked", this, false);
    addEventListener("pageshow", this, false);
    addEventListener("pagehide", this, false);
  },

  handleEvent: function(aEvent) {
    let document = content.document;
    switch (aEvent.type) {
      case "DOMContentLoaded":
        if (document.documentURIObject.spec == "about:blank")
          return;

        sendAsyncMessage("DOMContentLoaded", { });
        break;

      case "pageshow":
      case "pagehide": {
        let util = aEvent.target.defaultView.QueryInterface(Ci.nsIInterfaceRequestor)
                                            .getInterface(Ci.nsIDOMWindowUtils);

        let json = {
          windowId: util.outerWindowID,
          persisted: aEvent.persisted
        };

        sendAsyncMessage(aEvent.type, json);
        break;
      }

      case "DOMPopupBlocked": {
        let util = aEvent.requestingWindow.QueryInterface(Ci.nsIInterfaceRequestor)
                                          .getInterface(Ci.nsIDOMWindowUtils);
        let json = {
          windowId: util.outerWindowID,
          popupWindowURI: {
            spec: aEvent.popupWindowURI.spec,
            charset: aEvent.popupWindowURI.originCharset
          },
          popupWindowFeatures: aEvent.popupWindowFeatures,
          popupWindowName: aEvent.popupWindowName
        };

        sendAsyncMessage("DOMPopupBlocked", json);
        break;
      }

      case "DOMTitleChanged":
        sendAsyncMessage("DOMTitleChanged", { title: document.title });
        break;

      case "DOMLinkAdded":
        let target = aEvent.originalTarget;
        if (!target.href || target.disabled)
          return;

        let json = {
          windowId: target.ownerDocument.defaultView.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils).currentInnerWindowID,
          href: target.href,
          charset: document.characterSet,
          title: target.title,
          rel: target.rel,
          type: target.type
        };

        sendAsyncMessage("DOMLinkAdded", json);
        break;

      case "DOMWillOpenModalDialog":
      case "DOMModalDialogClosed":
      case "DOMWindowClose":
        let retvals = sendSyncMessage(aEvent.type, { });
        for (rv in retvals) {
          if (rv.preventDefault) {
            aEvent.preventDefault();
            break;
          }
        }
        break;
    }
  }
};

DOMEvents.init();

let ContentScroll =  {
  init: function() {
    addMessageListener("Content:ScrollTo", this);
    addMessageListener("Content:ScrollBy", this);
    addMessageListener("Content:SetResolution", this);
    addMessageListener("Content:SetCacheViewport", this);
    addMessageListener("Content:SetCssViewportSize", this);

    addEventListener("scroll", this, false);
    addEventListener("MozScrolledAreaChanged", this, false);
  },

  receiveMessage: function(aMessage) {
    var json = aMessage.json;
    switch (aMessage.name) {
      case "Content:ScrollTo":
        content.scrollTo(json.x, json.y);
        break;

      case "Content:ScrollBy":
        content.scrollBy(json.x, json.y);
        break;

      case "Content:SetCacheViewport": {
        var displayport = new Rect(json.x, json.y, json.w, json.h);
        dump("SetCacheViewport1\n");
        if (displayport.isEmpty())
          break;
        dump("SetCacheViewport2\n");

        var cwu = Util.getWindowUtils(content);
        cwu.setResolution(json.zoomLevel, json.zoomLevel);
        cwu.setDisplayPort(displayport.x, displayport.y, displayport.width, displayport.height);
        break;
      }

      case "Content:SetCssViewportSize": {
        let cwu = Util.getWindowUtils(content);
        cwu.setCSSViewport(json.width, json.height);
        break;
      }
    }
  },

  handleEvent: function(aEvent) {
    switch (aEvent.type) {
      case "scroll":
        Util.dumpLn("XXX stub");
        break;

      case "MozScrolledAreaChanged": {
        let doc = aEvent.originalTarget;
        let win = doc.defaultView;
        // XXX need to make some things in Util as its own module!
        let scrollOffset = Util.getScrollOffset(win);
        if (win.parent != win) // We are only interested in root scroll pane changes
          return;

        // Adjust width and height from the incoming event properties so that we
        // ignore changes to width and height contributed by growth in page
        // quadrants other than x > 0 && y > 0.
        let x = aEvent.x + scrollOffset.x;
        let y = aEvent.y + scrollOffset.y;
        let width = aEvent.width + (x < 0 ? x : 0);
        let height = aEvent.height + (y < 0 ? y : 0);
        dump(">>>>>>>>>>>>>>>>>>>>>>>>> MozScrolledAreaChanged\n");

        sendAsyncMessage("MozScrolledAreaChanged", {
          width: width,
          height: height,
        });

        break;
      }
    }
  }
};

ContentScroll.init();

