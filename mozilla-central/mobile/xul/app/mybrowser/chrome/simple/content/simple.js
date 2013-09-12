function go() {
    //var urlbar = document.getElementById("urlbar");
    var browser = document.getElementById("browser");

    //Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService).offline = false;
    //browser.loadURI("about:mozilla", null, null);
}

function onload() {
    dump("onload\n\n");
//    var browser = document.getElementById("browser");
    //browser.zoomController = new ZoomController(browser);
//    browser.mouseController = new MouseController(browser);
    dump("loadFrameScript1\n\n");
    
//browser.QueryInterface(Components.interfaces.nsIFrameLoaderOwner).frameLoader.messageManager.loadFrameScript("chrome://simple/content/browser.js", true);
    dump("loadFrameScript2\n\n");

    //go();
}

