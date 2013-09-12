/* -*- Mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file is used for both about:memory and about:compartments.
//
//
// about:memory will by default show information about the browser's current
// memory usage, but you can direct it to load information from a file by
// providing a file= query string.  For example,
//
//     about:memory?file=/foo/bar
//     about:memory?verbose&file=/foo/bar%26baz
//
// The order of "verbose" and "file=" isn't significant, and neither "verbose"
// nor "file=" is case-sensitive.  We'll URI-unescape the contents of the
// "file=" argument, and obviously the filename is case-sensitive iff you're on
// a case-sensitive filesystem.  If you specify more than one "file=" argument,
// only the first one is used.
//
// about:compartments doesn't support the "verbose" or "file=" parameters and
// will ignore them if they're provided.

"use strict";

//---------------------------------------------------------------------------
// Code shared by about:memory and about:compartments
//---------------------------------------------------------------------------

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

const KIND_NONHEAP           = Ci.nsIMemoryReporter.KIND_NONHEAP;
const KIND_HEAP              = Ci.nsIMemoryReporter.KIND_HEAP;
const KIND_OTHER             = Ci.nsIMemoryReporter.KIND_OTHER;
const UNITS_BYTES            = Ci.nsIMemoryReporter.UNITS_BYTES;
const UNITS_COUNT            = Ci.nsIMemoryReporter.UNITS_COUNT;
const UNITS_COUNT_CUMULATIVE = Ci.nsIMemoryReporter.UNITS_COUNT_CUMULATIVE;
const UNITS_PERCENTAGE       = Ci.nsIMemoryReporter.UNITS_PERCENTAGE;

let gMgr = Cc["@mozilla.org/memory-reporter-manager;1"].
           getService(Ci.nsIMemoryReporterManager);

let gUnnamedProcessStr = "Main Process";

// Because about:memory and about:compartments are non-standard URLs,
// location.search is undefined, so we have to use location.href here.
// The toLowerCase() calls ensure that addresses like "ABOUT:MEMORY" work.
let gVerbose;
{
  let split = document.location.href.split('?');
  document.title = split[0].toLowerCase();

  gVerbose = false;
  if (split.length == 2) {
    let searchSplit = split[1].split('&');
    for (let i = 0; i < searchSplit.length; i++) {
      if (searchSplit[i].toLowerCase() == 'verbose') {
        gVerbose = true;
      }
    }
  }
}

let gChildMemoryListener = undefined;

// This is a useful function and an efficient way to implement it.
String.prototype.startsWith =
  function(s) { return this.lastIndexOf(s, 0) === 0; }

//---------------------------------------------------------------------------

// Forward slashes in URLs in paths are represented with backslashes to avoid
// being mistaken for path separators.  Paths/names where this hasn't been
// undone are prefixed with "unsafe"; the rest are prefixed with "safe".
function flipBackslashes(aUnsafeStr)
{
  // Save memory by only doing the replacement if it's necessary.
  return (aUnsafeStr.indexOf('\\') === -1)
         ? aUnsafeStr
         : aUnsafeStr.replace(/\\/g, '/');
}

const gAssertionFailureMsgPrefix = "aboutMemory.js assertion failed: ";

// This is used for things that should never fail, and indicate a defect in
// this file if they do.
function assert(aCond, aMsg)
{
  if (!aCond) {
    reportAssertionFailure(aMsg)
    throw(gAssertionFailureMsgPrefix + aMsg);
  }
}

// This is used for malformed input from memory reporters.
function assertInput(aCond, aMsg)
{
  if (!aCond) {
    throw "Invalid memory report(s): " + aMsg;
  }
}

function handleException(ex)
{
  let str = ex.toString();
  if (str.startsWith(gAssertionFailureMsgPrefix)) {
    throw ex;     // Argh, assertion failure within this file!  Give up.
  } else {
    badInput(ex); // File or memory reporter problem.  Print a message.
  }
}

function reportAssertionFailure(aMsg)
{
  let debug = Cc["@mozilla.org/xpcom/debug;1"].getService(Ci.nsIDebug2);
  if (debug.isDebugBuild) {
    debug.assertion(aMsg, "false", "aboutMemory.js", 0);
  }
}

function debug(x)
{
  let section = appendElement(document.body, 'div', 'section');
  appendElementWithText(section, "div", "debug", JSON.stringify(x));
}

function badInput(x)
{
  let section = appendElement(document.body, 'div', 'section');
  appendElementWithText(section, "div", "badInputWarning", x);
}

//---------------------------------------------------------------------------

function addChildObserversAndUpdate(aUpdateFn)
{
  let os = Cc["@mozilla.org/observer-service;1"].
      getService(Ci.nsIObserverService);
  os.notifyObservers(null, "child-memory-reporter-request", null);

  gChildMemoryListener = aUpdateFn;
  os.addObserver(gChildMemoryListener, "child-memory-reporter-update", false);

  gChildMemoryListener();
}

function onLoad()
{
  if (document.title === "about:memory") {
    onLoadAboutMemory();
  } else if (document.title === "about:compartments") {
    onLoadAboutCompartments();
  } else {
    assert(false, "Unknown location: " + document.title);
  }
}

function onUnload()
{
  // We need to check if the observer has been added before removing; in some
  // circumstances (e.g. reloading the page quickly) it might not have because
  // onLoadAbout{Memory,Compartments} might not fire.
  if (gChildMemoryListener) {
    let os = Cc["@mozilla.org/observer-service;1"].
        getService(Ci.nsIObserverService);
    os.removeObserver(gChildMemoryListener, "child-memory-reporter-update");
  }
}

//---------------------------------------------------------------------------

/**
 * Iterates over each reporter and multi-reporter.
 *
 * @param aIgnoreSingle
 *        Function that indicates if we should skip a single reporter, based
 *        on its path.
 * @param aIgnoreMulti
 *        Function that indicates if we should skip a multi-reporter, based on
 *        its name.
 * @param aHandleReport
 *        The function that's called for each report.
 */
function processMemoryReporters(aIgnoreSingle, aIgnoreMulti, aHandleReport)
{
  // Process each memory reporter with aHandleReport.
  //
  // - Note that copying rOrig.amount (which calls a C++ function under the
  //   IDL covers) to r._amount for every reporter now means that the
  //   results as consistent as possible -- measurements are made all at
  //   once before most of the memory required to generate this page is
  //   allocated.
  //
  // - After this point we never use the original memory report again.

  let e = gMgr.enumerateReporters();
  while (e.hasMoreElements()) {
    let rOrig = e.getNext().QueryInterface(Ci.nsIMemoryReporter);
    let unsafePath = rOrig.path;
    if (!aIgnoreSingle(unsafePath)) {
      aHandleReport(rOrig.process, unsafePath, rOrig.kind, rOrig.units,
                    rOrig.amount, rOrig.description);
    }
  }

  let e = gMgr.enumerateMultiReporters();
  while (e.hasMoreElements()) {
    let mr = e.getNext().QueryInterface(Ci.nsIMemoryMultiReporter);
    if (!aIgnoreMulti(mr.name)) {
      mr.collectReports(aHandleReport, null);
    }
  }
}

/**
 * Iterates over each report.
 *
 * @param aReports
 *        Array of reports, read from a file or the clipboard.
 * @param aIgnoreSingle
 *        Function that indicates if we should skip a single reporter, based
 *        on its path.
 * @param aHandleReport
 *        The function that's called for each report.
 */
function processMemoryReportsFromFile(aReports, aIgnoreSingle, aHandleReport)
{
  // Process each memory reporter with aHandleReport.

  for (let i = 0; i < aReports.length; i++) {
    let r = aReports[i];
    if (!aIgnoreSingle(r.path)) {
      aHandleReport(r.process, r.path, r.kind, r.units, r.amount,
                    r.description);
    }
  }
}

//---------------------------------------------------------------------------

function clearBody()
{
  let oldBody = document.body;
  let body = oldBody.cloneNode(false);
  oldBody.parentNode.replaceChild(body, oldBody);
  body.classList.add(gVerbose ? 'verbose' : 'non-verbose');
  return body
}

function appendTextNode(aP, aText)
{
  let e = document.createTextNode(aText);
  aP.appendChild(e);
  return e;
}

function appendElement(aP, aTagName, aClassName)
{
  let e = document.createElement(aTagName);
  if (aClassName) {
    e.className = aClassName;
  }
  aP.appendChild(e);
  return e;
}

function appendElementWithText(aP, aTagName, aClassName, aText)
{
  let e = appendElement(aP, aTagName, aClassName);
  // Setting textContent clobbers existing children, but there are none.  More
  // importantly, it avoids creating a JS-land object for the node, saving
  // memory.
  e.textContent = aText;
  return e;
}

//---------------------------------------------------------------------------
// Code specific to about:memory
//---------------------------------------------------------------------------

const kTreeDescriptions = {
  'explicit' :
"This tree covers explicit memory allocations by the application, both at the \
operating system level (via calls to functions such as VirtualAlloc, \
vm_allocate, and mmap), and at the heap allocation level (via functions such \
as malloc, calloc, realloc, memalign, operator new, and operator new[]).\
\n\n\
It excludes memory that is mapped implicitly such as code and data segments, \
and thread stacks.  It also excludes heap memory that has been freed by the \
application but is still being held onto by the heap allocator. \
\n\n\
It is not guaranteed to cover every explicit allocation, but it does cover \
most (including the entire heap), and therefore it is the single best number \
to focus on when trying to reduce memory usage.",

  'rss':
"This tree shows how much space in physical memory each of the process's \
mappings is currently using (the mapping's 'resident set size', or 'RSS'). \
This is a good measure of the 'cost' of the mapping, although it does not \
take into account the fact that shared libraries may be mapped by multiple \
processes but appear only once in physical memory. \
\n\n\
Note that the 'rss' value here might not equal the value for 'resident' \
under 'Other Measurements' because the two measurements are not taken at \
exactly the same time.",

  'pss':
"This tree shows how much space in physical memory can be 'blamed' on this \
process.  For each mapping, its 'proportional set size' (PSS) is the \
mapping's resident size divided by the number of processes which use the \
mapping.  So if a mapping is private to this process, its PSS should equal \
its RSS.  But if a mapping is shared between three processes, its PSS in each \
of the processes would be 1/3 its RSS.",

  'size':
"This tree shows how much virtual addres space each of the process's mappings \
takes up (a.k.a. the mapping's 'vsize').  A mapping may have a large size but use \
only a small amount of physical memory; the resident set size of a mapping is \
a better measure of the mapping's 'cost'. \
\n\n\
Note that the 'size' value here might not equal the value for 'vsize' under \
'Other Measurements' because the two measurements are not taken at exactly \
the same time.",

  'swap':
"This tree shows how much space in the swap file each of the process's \
mappings is currently using. Mappings which are not in the swap file (i.e., \
nodes which would have a value of 0 in this tree) are omitted."
};

const kSectionNames = {
  'explicit': 'Explicit Allocations',
  'rss':      'Resident Set Size (RSS) Breakdown',
  'pss':      'Proportional Set Size (PSS) Breakdown',
  'size':     'Virtual Size Breakdown',
  'swap':     'Swap Breakdown',
  'other':    'Other Measurements'
};

const kSmapsTreeNames    = ['rss',  'pss',  'size',  'swap' ];
const kSmapsTreePrefixes = ['rss/', 'pss/', 'size/', 'swap/'];

function isExplicitPath(aUnsafePath)
{
  return aUnsafePath.startsWith("explicit/");
}

function isSmapsPath(aUnsafePath)
{
  for (let i = 0; i < kSmapsTreePrefixes.length; i++) {
    if (aUnsafePath.startsWith(kSmapsTreePrefixes[i])) {
      return true;
    }
  }
  return false;
}

//---------------------------------------------------------------------------

function onLoadAboutMemory()
{
  // Check location.href to see if we're loading from a file.
  let search = location.href.split('?')[1];
  if (search) {
    let searchSplit = search.split('&');
    for (let i = 0; i < searchSplit.length; i++) {
      if (searchSplit[i].toLowerCase().startsWith('file=')) {
        let filename = searchSplit[i].substring('file='.length);
        let file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
        file.initWithPath(decodeURIComponent(filename));
        updateAboutMemoryFromFile(file);
        return;
      }
    }
  }

  addChildObserversAndUpdate(updateAboutMemory);
}

function doGlobalGC()
{
  Cu.forceGC();
  let os = Cc["@mozilla.org/observer-service;1"]
            .getService(Ci.nsIObserverService);
  os.notifyObservers(null, "child-gc-request", null);
  updateAboutMemory();
}

function doCC()
{
  window.QueryInterface(Ci.nsIInterfaceRequestor)
        .getInterface(Ci.nsIDOMWindowUtils)
        .cycleCollect();
  let os = Cc["@mozilla.org/observer-service;1"]
            .getService(Ci.nsIObserverService);
  os.notifyObservers(null, "child-cc-request", null);
  updateAboutMemory();
}

//---------------------------------------------------------------------------

/**
 * Top-level function that does the work of generating the page from the memory
 * reporters.
 */
function updateAboutMemory()
{
  // First, clear the page contents.  Necessary because updateAboutMemory()
  // might be called more than once due to the "child-memory-reporter-update"
  // observer.
  let body = clearBody();

  try {
    // Process the reports from the memory reporters.
    let process = function(aIgnoreSingle, aIgnoreMulti, aHandleReport) {
      processMemoryReporters(aIgnoreSingle, aIgnoreMulti, aHandleReport);
    }
    appendAboutMemoryMain(body, process, gMgr.hasMozMallocUsableSize,
                          /* forceShowSmaps = */ false);

  } catch (ex) {
    handleException(ex);

  } finally {
    appendAboutMemoryFooter(body);
  }
}

// Increment this if the JSON format changes.
var gCurrentFileFormatVersion = 1;

/**
 * Populate about:memory using the data in the given JSON string.
 *
 * @param aJSONString
 *        A string containing JSON data conforming to the schema used by
 *        nsIMemoryReporterManager::dumpReports.
 */
function updateAboutMemoryFromJSONString(aJSONString)
{
  let body = clearBody();

  try {
    let json = JSON.parse(aJSONString);
    assertInput(json.version === gCurrentFileFormatVersion,
                "data version number missing or doesn't match");
    assertInput(json.hasMozMallocUsableSize !== undefined,
                "missing 'hasMozMallocUsableSize' property");
    assertInput(json.reports && json.reports instanceof Array,
                "missing or non-array 'reports' property");
    let process = function(aIgnoreSingle, aIgnoreMulti, aHandleReport) {
      processMemoryReportsFromFile(json.reports, aIgnoreSingle,
                                   aHandleReport);
    }
    appendAboutMemoryMain(body, process, json.hasMozMallocUsableSize,
                          /* forceShowSmaps = */ true);
  } catch (ex) {
    handleException(ex);
  } finally {
    appendAboutMemoryFooter(body);
  }
}

/**
 * Like updateAboutMemory(), but gets its data from a file instead of the
 * memory reporters.
 *
 * @param aFile
 *        The File or nsILocalFile being read from.
 *
 *        The expected format of the file's contents is described in the
 *        comment describing nsIMemoryReporterManager::dumpReports.
 */
function updateAboutMemoryFromFile(aFile)
{
  // Note: reader.onload is called asynchronously, once FileReader.readAsText()
  // completes.  Therefore its exception handling has to be distinct from that
  // surrounding the |reader.readAsText(aFile)| call.

  try {
    // Convert nsILocalFile to a File object, if necessary.
    let file = aFile;
    if (aFile instanceof Ci.nsILocalFile) {
      file = new File(aFile);
    }

    let reader = new FileReader();
    reader.onerror = function(aEvent) { throw "FileReader.onerror"; };
    reader.onabort = function(aEvent) { throw "FileReader.onabort"; };
    reader.onload = function(aEvent) {
      updateAboutMemoryFromJSONString(aEvent.target.result);
    };
    reader.readAsText(file);

  } catch (ex) {
    let body = clearBody();
    handleException(ex);
    appendAboutMemoryFooter(body);
  }
}

/**
 * Like updateAboutMemoryFromFile(), but gets its data from the clipboard
 * instead of a file.
 */
function updateAboutMemoryFromClipboard()
{
  // Get the clipboard's contents.
  let cb = Cc["@mozilla.org/widget/clipboard;1"]
             .getService(Components.interfaces.nsIClipboard);
  let transferable = Cc["@mozilla.org/widget/transferable;1"]
                       .createInstance(Ci.nsITransferable);
  let loadContext = window.QueryInterface(Ci.nsIInterfaceRequestor)
                          .getInterface(Ci.nsIWebNavigation)
                          .QueryInterface(Ci.nsILoadContext);
  transferable.init(loadContext);
  transferable.addDataFlavor('text/unicode');
  cb.getData(transferable, Ci.nsIClipboard.kGlobalClipboard);

  var cbData = {};
  try {
    transferable.getTransferData('text/unicode', cbData,
                                 /* out dataLen (ignored) */ {});
    let cbString = cbData.value.QueryInterface(Ci.nsISupportsString).data;

    // Success!  Now use the string to generate about:memory.
    updateAboutMemoryFromJSONString(cbString);
  } catch (ex) {
    let body = clearBody();
    handleException(ex);
    appendAboutMemoryFooter(body);
  }
}

/**
 * Processes reports (whether from reporters or from a file) and append the
 * main part of the page.
 *
 * @param aBody
 *        The DOM body element.
 * @param aProcess
 *        Function that extracts the memory reports from the reporters or from
 *        file.
 * @param aHasMozMallocUsableSize
 *        Boolean indicating if moz_malloc_usable_size works.
 * @param aForceShowSmaps
 *        True if we should show the smaps memory reporters even if we're not
 *        in verbose mode.
 */
function appendAboutMemoryMain(aBody, aProcess, aHasMozMallocUsableSize,
                               aForceShowSmaps)
{
  let treesByProcess = {}, degeneratesByProcess = {}, heapTotalByProcess = {};
  getTreesByProcess(aProcess, treesByProcess, degeneratesByProcess,
                    heapTotalByProcess, aForceShowSmaps);

  // Sort our list of processes.  Always start with the main process, then sort
  // by resident size (descending).  Processes with no resident reporter go at
  // the end of the list.
  let processes = Object.keys(treesByProcess);
  processes.sort(function(a, b) {
    assert(a != b, "Elements of Object.keys() should be unique, but " +
                   "saw duplicate " + a + " elem.");

    if (a == gUnnamedProcessStr) {
      return -1;
    }

    if (b == gUnnamedProcessStr) {
      return 1;
    }

    let nodeA = degeneratesByProcess[a]['resident'];
    let nodeB = degeneratesByProcess[b]['resident'];

    if (nodeA && nodeB) {
      return TreeNode.compareAmounts(nodeA, nodeB);
    }

    if (nodeA) {
      return -1;
    }

    if (nodeB) {
      return 1;
    }

    return 0;
  });

  // Generate output for each process.
  for (let i = 0; i < processes.length; i++) {
    let process = processes[i];
    let section = appendElement(aBody, 'div', 'section');

    appendProcessAboutMemoryElements(section, process,
                                     treesByProcess[process],
                                     degeneratesByProcess[process],
                                     heapTotalByProcess[process],
                                     aHasMozMallocUsableSize);
  }
}

/**
 * Appends the page footer.
 *
 * @param aBody
 *        The DOM body element.
 */
function appendAboutMemoryFooter(aBody)
{
  let section = appendElement(aBody, 'div', 'footer');

  // Memory-related actions.
  const UpDesc = "Re-measure.";
  const GCDesc = "Do a global garbage collection.";
  const CCDesc = "Do a cycle collection.";
  const MPDesc = "Send three \"heap-minimize\" notifications in a " +
                 "row.  Each notification triggers a global garbage " +
                 "collection followed by a cycle collection, and causes the " +
                 "process to reduce memory usage in other ways, e.g. by " +
                 "flushing various caches.";
  const RdDesc = "Read memory report data from a file.";
  const CbDesc = "Read memory report data from the clipboard.";

  function appendButton(aP, aTitle, aOnClick, aText, aId)
  {
    let b = appendElementWithText(aP, "button", "", aText);
    b.title = aTitle;
    b.onclick = aOnClick
    if (aId) {
      b.id = aId;
    }
  }

  let div1 = appendElement(section, "div");

  // The "Update" button has an id so it can be clicked in a test.
  appendButton(div1, UpDesc, updateAboutMemory, "Update", "updateButton");
  appendButton(div1, GCDesc, doGlobalGC,        "GC");
  appendButton(div1, CCDesc, doCC,              "CC");
  appendButton(div1, MPDesc,
               function() { gMgr.minimizeMemoryUsage(updateAboutMemory); },
               "Minimize memory usage");

  // The standard file input element is ugly.  So we hide it, and add a button
  // that when clicked invokes the input element.
  let input = appendElementWithText(div1, "input", "hidden", "input text");
  input.type = "file";
  input.id = "fileInput";   // has an id so it can be invoked by a test
  input.addEventListener("change", function() {
    let file = this.files[0];
    updateAboutMemoryFromFile(file);
  }); 
  appendButton(div1, RdDesc, function() { input.click() },
               "Read reports from a file", "readReportsFromFileButton");

  appendButton(div1, CbDesc, updateAboutMemoryFromClipboard,
               "Read reports from clipboard", "readReportsFromClipboardButton");

  let div2 = appendElement(section, "div");
  if (gVerbose) {
    let a = appendElementWithText(div2, "a", "option", "Less verbose");
    a.href = "about:memory";
  } else {
    let a = appendElementWithText(div2, "a", "option", "More verbose");
    a.href = "about:memory?verbose";
  }

  let div3 = appendElement(section, "div");
  let a = appendElementWithText(div3, "a", "option",
                                "Troubleshooting information");
  a.href = "about:support";

  let legendText1 = "Click on a non-leaf node in a tree to expand ('++') " +
                    "or collapse ('--') its children.";
  let legendText2 = "Hover the pointer over the name of a memory report " +
                    "to see a description of what it measures.";

  appendElementWithText(section, "div", "legend", legendText1);
  appendElementWithText(section, "div", "legend hiddenOnMobile", legendText2);
}

//---------------------------------------------------------------------------

// This regexp matches sentences and sentence fragments, i.e. strings that
// start with a capital letter and ends with a '.'.  (The final sentence may be
// in parentheses, so a ')' might appear after the '.'.)
const gSentenceRegExp = /^[A-Z].*\.\)?$/m;

/**
 * This function reads all the memory reports, and puts that data in structures
 * that will be used to generate the page.
 *
 * @param aProcessMemoryReports
 *        Function that extracts the memory reports from the reporters or from
 *        file.
 * @param aTreesByProcess
 *        Table of non-degenerate trees, indexed by process, which this
 *        function appends to.
 * @param aDegeneratesByProcess
 *        Table of degenerate trees, indexed by process, which this function
 *        appends to.
 * @param aHeapTotalByProcess
 *        Table of heap total counts, indexed by process, which this function
 *        appends to.
 * @param aForceShowSmaps
 *        True if we should show the smaps memory reporters even if we're not
 *        in verbose mode.
 */
function getTreesByProcess(aProcessMemoryReports, aTreesByProcess,
                           aDegeneratesByProcess, aHeapTotalByProcess,
                           aForceShowSmaps)
{
  // Ignore the "smaps" multi-reporter in non-verbose mode unless we're reading
  // from a file or the clipboard, and ignore the "compartments" and
  // "ghost-windows" multi-reporters all the time.  (Note that reports from
  // these multi-reporters can reach here as single reports if they were in the
  // child process.)

  function ignoreSingle(aUnsafePath)
  {
    return (isSmapsPath(aUnsafePath) && !gVerbose && !aForceShowSmaps) ||
           aUnsafePath.startsWith("compartments/") ||
           aUnsafePath.startsWith("ghost-windows/");
  }

  function ignoreMulti(aMRName)
  {
    return (aMRName === "smaps" && !gVerbose && !aForceShowSmaps) ||
            aMRName === "compartments" ||
            aMRName === "ghost-windows";
  }

  function handleReport(aProcess, aUnsafePath, aKind, aUnits, aAmount,
                        aDescription)
  {
    if (isExplicitPath(aUnsafePath)) {
      assertInput(aKind === KIND_HEAP || aKind === KIND_NONHEAP, "bad explicit kind");
      assertInput(aUnits === UNITS_BYTES, "bad explicit units");
      assertInput(gSentenceRegExp.test(aDescription),
                  "non-sentence explicit description");

    } else if (isSmapsPath(aUnsafePath)) {
      assertInput(aKind === KIND_NONHEAP, "bad smaps kind");
      assertInput(aUnits === UNITS_BYTES, "bad smaps units");
      assertInput(aDescription !== "", "empty smaps description");

    } else {
      assertInput(gSentenceRegExp.test(aDescription),
                  "non-sentence other description");
    }

    let process = aProcess === "" ? gUnnamedProcessStr : aProcess;
    let unsafeNames = aUnsafePath.split('/');
    let unsafeName0 = unsafeNames[0];
    let isDegenerate = unsafeNames.length === 1;

    // Get the appropriate trees table (non-degenerate or degenerate) for the
    // process, creating it if necessary.
    let t;
    let thingsByProcess =
      isDegenerate ? aDegeneratesByProcess : aTreesByProcess;
    let things = thingsByProcess[process];
    if (!thingsByProcess[process]) {
      things = thingsByProcess[process] = {};
    }

    // Get the root node, creating it if necessary.
    t = things[unsafeName0];
    if (!t) {
      t = things[unsafeName0] =
        new TreeNode(unsafeName0, aUnits, isDegenerate);
    }

    if (!isDegenerate) {
      // Add any missing nodes in the tree implied by aUnsafePath, and fill in
      // the properties that we can with a top-down traversal.
      for (let i = 1; i < unsafeNames.length; i++) {
        let unsafeName = unsafeNames[i];
        let u = t.findKid(unsafeName);
        if (!u) {
          u = new TreeNode(unsafeName, aUnits, isDegenerate);
          if (!t._kids) {
            t._kids = [];
          }
          t._kids.push(u);
        }
        t = u;
      }

      // Update the heap total if necessary.
      if (unsafeName0 === "explicit" && aKind == KIND_HEAP) {
        if (!aHeapTotalByProcess[process]) {
          aHeapTotalByProcess[process] = 0;
        }
        aHeapTotalByProcess[process] += aAmount;
      }
    }

    if (t._amount) {
      // Duplicate!  Sum the values and mark it as a dup.
      t._amount += aAmount;
      t._nMerged = t._nMerged ? t._nMerged + 1 : 2;
    } else {
      // New leaf node.  Fill in extra details node from the report.
      t._amount = aAmount;
      t._description = aDescription;
    }
  }

  aProcessMemoryReports(ignoreSingle, ignoreMulti, handleReport);
}

//---------------------------------------------------------------------------

// There are two kinds of TreeNode.
// - Leaf TreeNodes correspond to reports.
// - Non-leaf TreeNodes are just scaffolding nodes for the tree;  their values
//   are derived from their children.
// Some trees are "degenerate", i.e. they contain a single node, i.e. they
// correspond to a report whose path has no '/' separators.
function TreeNode(aUnsafeName, aUnits, aIsDegenerate)
{
  this._units = aUnits;
  this._unsafeName = aUnsafeName;
  if (aIsDegenerate) {
    this._isDegenerate = true;
  }

  // Leaf TreeNodes have these properties added immediately after construction:
  // - _amount
  // - _description
  // - _nMerged (only defined if > 1)
  //
  // Non-leaf TreeNodes have these properties added later:
  // - _kids
  // - _amount
  // - _description
  // - _hideKids (only defined if true)
}

TreeNode.prototype = {
  findKid: function(aUnsafeName) {
    if (this._kids) {
      for (let i = 0; i < this._kids.length; i++) {
        if (this._kids[i]._unsafeName === aUnsafeName) {
          return this._kids[i];
        }
      }
    }
    return undefined;
  },

  toString: function() {
    switch (this._units) {
      case UNITS_BYTES:            return formatBytes(this._amount);
      case UNITS_COUNT:
      case UNITS_COUNT_CUMULATIVE: return formatInt(this._amount);
      case UNITS_PERCENTAGE:       return formatPercentage(this._amount);
      default:
        assertInput(false, "bad units in TreeNode.toString");
    }
  }
};

TreeNode.compareAmounts = function(a, b) {
  return b._amount - a._amount;
};

TreeNode.compareUnsafeNames = function(a, b) {
  return a._unsafeName < b._unsafeName ? -1 :
         a._unsafeName > b._unsafeName ?  1 :
         0;
};


/**
 * Fill in the remaining properties for the specified tree in a bottom-up
 * fashion.
 *
 * @param aRoot
 *        The tree root.
 */
function fillInTree(aRoot)
{
  // Fill in the remaining properties bottom-up.
  function fillInNonLeafNodes(aT)
  {
    if (!aT._kids) {
      // Leaf node.  Has already been filled in.

    } else if (aT._kids.length === 1 && aT != aRoot) {
      // Non-root, non-leaf node with one child.  Merge the child with the node
      // to avoid redundant entries.
      let kid = aT._kids[0];
      let kidBytes = fillInNonLeafNodes(kid);
      aT._unsafeName += '/' + kid._unsafeName;
      if (kid._kids) {
        aT._kids = kid._kids;
      } else {
        delete aT._kids;
      }
      aT._amount = kid._amount;
      aT._description = kid._description;
      if (kid._nMerged !== undefined) {
        aT._nMerged = kid._nMerged
      }
      assert(!aT._hideKids && !kid._hideKids, "_hideKids set when merging");

    } else {
      // Non-leaf node with multiple children.  Derive its _amount and
      // _description entirely from its children.
      let kidsBytes = 0;
      for (let i = 0; i < aT._kids.length; i++) {
        kidsBytes += fillInNonLeafNodes(aT._kids[i]);
      }
      assert(aT._amount === undefined, "_amount already set for non-leaf node");
      aT._amount = kidsBytes;
      aT._description = "The sum of all entries below this one.";
    }
    return aT._amount;
  }

  // cannotMerge is set because don't want to merge into a tree's root node.
  fillInNonLeafNodes(aRoot);
}

/**
 * Compute the "heap-unclassified" value and insert it into the "explicit"
 * tree.
 *
 * @param aT
 *        The "explicit" tree.
 * @param aHeapAllocatedNode
 *        The "heap-allocated" tree node.
 * @param aHeapTotal
 *        The sum of all explicit HEAP reporters for this process.
 * @return A boolean indicating if "heap-allocated" is known for the process.
 */
function addHeapUnclassifiedNode(aT, aHeapAllocatedNode, aHeapTotal)
{
  if (aHeapAllocatedNode === undefined)
    return false;

  assert(aHeapAllocatedNode._isDegenerate, "heap-allocated is not degenerate");
  let heapAllocatedBytes = aHeapAllocatedNode._amount;
  let heapUnclassifiedT = new TreeNode("heap-unclassified", UNITS_BYTES);
  heapUnclassifiedT._amount = heapAllocatedBytes - aHeapTotal;
  heapUnclassifiedT._description =
      "Memory not classified by a more specific reporter. This includes " +
      "slop bytes due to internal fragmentation in the heap allocator " +
      "(caused when the allocator rounds up request sizes).";
  aT._kids.push(heapUnclassifiedT);
  aT._amount += heapUnclassifiedT._amount;
  return true;
}

/**
 * Sort all kid nodes from largest to smallest, and insert aggregate nodes
 * where appropriate.
 *
 * @param aTotalBytes
 *        The size of the tree's root node.
 * @param aT
 *        The tree.
 */
function sortTreeAndInsertAggregateNodes(aTotalBytes, aT)
{
  const kSignificanceThresholdPerc = 1;

  function isInsignificant(aT)
  {
    return !gVerbose &&
           (100 * aT._amount / aTotalBytes) < kSignificanceThresholdPerc;
  }

  if (!aT._kids) {
    return;
  }

  aT._kids.sort(TreeNode.compareAmounts);

  // If the first child is insignificant, they all are, and there's no point
  // creating an aggregate node that lacks siblings.  Just set the parent's
  // _hideKids property and process all children.
  if (isInsignificant(aT._kids[0])) {
    aT._hideKids = true;
    for (let i = 0; i < aT._kids.length; i++) {
      sortTreeAndInsertAggregateNodes(aTotalBytes, aT._kids[i]);
    }
    return;
  }

  // Look at all children except the last one.
  let i;
  for (i = 0; i < aT._kids.length - 1; i++) {
    if (isInsignificant(aT._kids[i])) {
      // This child is below the significance threshold.  If there are other
      // (smaller) children remaining, move them under an aggregate node.
      let i0 = i;
      let nAgg = aT._kids.length - i0;
      // Create an aggregate node.  Inherit units from the parent;  everything
      // in the tree should have the same units anyway (we test this later).
      let aggT = new TreeNode("(" + nAgg + " tiny)", aT._units);
      aggT._kids = [];
      let aggBytes = 0;
      for ( ; i < aT._kids.length; i++) {
        aggBytes += aT._kids[i]._amount;
        aggT._kids.push(aT._kids[i]);
      }
      aggT._hideKids = true;
      aggT._amount = aggBytes;
      aggT._description =
        nAgg + " sub-trees that are below the " + kSignificanceThresholdPerc +
        "% significance threshold.";
      aT._kids.splice(i0, nAgg, aggT);
      aT._kids.sort(TreeNode.compareAmounts);

      // Process the moved children.
      for (i = 0; i < aggT._kids.length; i++) {
        sortTreeAndInsertAggregateNodes(aTotalBytes, aggT._kids[i]);
      }
      return;
    }

    sortTreeAndInsertAggregateNodes(aTotalBytes, aT._kids[i]);
  }

  // The first n-1 children were significant.  Don't consider if the last child
  // is significant;  there's no point creating an aggregate node that only has
  // one child.  Just process it.
  sortTreeAndInsertAggregateNodes(aTotalBytes, aT._kids[i]);
}

// Global variable indicating if we've seen any invalid values for this
// process;  it holds the unsafePaths of any such reports.  It is reset for
// each new process.
let gUnsafePathsWithInvalidValuesForThisProcess = [];

function appendWarningElements(aP, aHasKnownHeapAllocated,
                               aHasMozMallocUsableSize)
{
  if (!aHasKnownHeapAllocated && !aHasMozMallocUsableSize) {
    appendElementWithText(aP, "p", "",
      "WARNING: the 'heap-allocated' memory reporter and the " +
      "moz_malloc_usable_size() function do not work for this platform " +
      "and/or configuration.  This means that 'heap-unclassified' is not " +
      "shown and the 'explicit' tree shows much less memory than it should.\n\n");

  } else if (!aHasKnownHeapAllocated) {
    appendElementWithText(aP, "p", "",
      "WARNING: the 'heap-allocated' memory reporter does not work for this " +
      "platform and/or configuration. This means that 'heap-unclassified' " +
      "is not shown and the 'explicit' tree shows less memory than it should.\n\n");

  } else if (!aHasMozMallocUsableSize) {
    appendElementWithText(aP, "p", "",
      "WARNING: the moz_malloc_usable_size() function does not work for " +
      "this platform and/or configuration.  This means that much of the " +
      "heap-allocated memory is not measured by individual memory reporters " +
      "and so will fall under 'heap-unclassified'.\n\n");
  }

  if (gUnsafePathsWithInvalidValuesForThisProcess.length > 0) {
    let div = appendElement(aP, "div");
    appendElementWithText(div, "p", "",
      "WARNING: the following values are negative or unreasonably large.\n");

    let ul = appendElement(div, "ul");
    for (let i = 0;
         i < gUnsafePathsWithInvalidValuesForThisProcess.length;
         i++)
    {
      appendTextNode(ul, " ");
      appendElementWithText(ul, "li", "",
        flipBackslashes(gUnsafePathsWithInvalidValuesForThisProcess[i]) + "\n");
    }

    appendElementWithText(div, "p", "",
      "This indicates a defect in one or more memory reporters.  The " +
      "invalid values are highlighted.\n\n");
    gUnsafePathsWithInvalidValuesForThisProcess = [];  // reset for the next process
  }
}

/**
 * Appends the about:memory elements for a single process.
 *
 * @param aP
 *        The parent DOM node.
 * @param aProcess
 *        The name of the process.
 * @param aTrees
 *        The table of non-degenerate trees for this process.
 * @param aDegenerates
 *        The table of degenerate trees for this process.
 * @param aHasMozMallocUsableSize
 *        Boolean indicating if moz_malloc_usable_size works.
 * @return The generated text.
 */
function appendProcessAboutMemoryElements(aP, aProcess, aTrees, aDegenerates,
                                          aHeapTotal, aHasMozMallocUsableSize)
{
  appendElementWithText(aP, "h1", "", aProcess + "\n\n");

  // We'll fill this in later.
  let warningsDiv = appendElement(aP, "div", "accuracyWarning");

  // The explicit tree.
  let hasKnownHeapAllocated;
  {
    let treeName = "explicit";
    let t = aTrees[treeName];
    assertInput(t, "no explicit reports");
    fillInTree(t);
    hasKnownHeapAllocated =
      aDegenerates &&
      addHeapUnclassifiedNode(t, aDegenerates["heap-allocated"], aHeapTotal);
    sortTreeAndInsertAggregateNodes(t._amount, t);
    t._description = kTreeDescriptions[treeName];
    let pre = appendSectionHeader(aP, kSectionNames[treeName]);
    appendTreeElements(pre, t, aProcess, "");
    appendTextNode(aP, "\n");  // gives nice spacing when we cut and paste
    delete aTrees[treeName];
  }

  // The smaps trees, which are only present in aTrees in verbose mode or when
  // we're reading from a file or the clipboard.
  kSmapsTreeNames.forEach(function(aTreeName) {
    // |t| will be undefined if we don't have any reports for the given
    // unsafePath.
    let t = aTrees[aTreeName];
    if (t) {
      fillInTree(t);
      sortTreeAndInsertAggregateNodes(t._amount, t);
      t._description = kTreeDescriptions[aTreeName];
      t._hideKids = true;   // smaps trees are always initially collapsed
      let pre = appendSectionHeader(aP, kSectionNames[aTreeName]);
      appendTreeElements(pre, t, aProcess, "");
      appendTextNode(aP, "\n");  // gives nice spacing when we cut and paste
      delete aTrees[aTreeName];
    }
  });

  // Fill in and sort all the non-degenerate other trees.
  let otherTrees = [];
  for (let unsafeName in aTrees) {
    let t = aTrees[unsafeName];
    assert(!t._isDegenerate, "tree is degenerate");
    fillInTree(t);
    sortTreeAndInsertAggregateNodes(t._amount, t);
    otherTrees.push(t);
  }
  otherTrees.sort(TreeNode.compareUnsafeNames);

  // Get the length of the longest root value among the degenerate other trees,
  // and sort them as well.
  let otherDegenerates = [];
  let maxStringLength = 0;
  for (let unsafeName in aDegenerates) {
    let t = aDegenerates[unsafeName];
    assert(t._isDegenerate, "tree is not degenerate");
    let length = t.toString().length;
    if (length > maxStringLength) {
      maxStringLength = length;
    }
    otherDegenerates.push(t);
  }
  otherDegenerates.sort(TreeNode.compareUnsafeNames);

  // Now generate the elements, putting non-degenerate trees first. 
  let pre = appendSectionHeader(aP, kSectionNames['other']);
  for (let i = 0; i < otherTrees.length; i++) {
    let t = otherTrees[i];
    appendTreeElements(pre, t, aProcess, "");
    appendTextNode(pre, "\n");  // blank lines after non-degenerate trees
  }
  for (let i = 0; i < otherDegenerates.length; i++) {
    let t = otherDegenerates[i];
    let padText = pad("", maxStringLength - t.toString().length, ' ');
    appendTreeElements(pre, t, aProcess, padText);
  }
  appendTextNode(aP, "\n");  // gives nice spacing when we cut and paste

  // Add any warnings about inaccuracies due to platform limitations.
  // These must be computed after generating all the text.  The newlines give
  // nice spacing if we cut+paste into a text buffer.
  appendWarningElements(warningsDiv, hasKnownHeapAllocated,
                        aHasMozMallocUsableSize);
}

/**
 * Determines if a number has a negative sign when converted to a string.
 * Works even for -0.
 *
 * @param aN
 *        The number.
 * @return A boolean.
 */
function hasNegativeSign(aN)
{
  if (aN === 0) {                   // this succeeds for 0 and -0
    return 1 / aN === -Infinity;    // this succeeds for -0
  }
  return aN < 0;
}

/**
 * Formats an int as a human-readable string.
 *
 * @param aN
 *        The integer to format.
 * @param aExtra
 *        An extra string to tack onto the end.
 * @return A human-readable string representing the int.
 *
 * Note: building an array of chars and converting that to a string with
 * Array.join at the end is more memory efficient than using string
 * concatenation.  See bug 722972 for details.
 */
function formatInt(aN, aExtra)
{
  let neg = false;
  if (hasNegativeSign(aN)) {
    neg = true;
    aN = -aN;
  }
  let s = [];
  while (true) {
    let k = aN % 1000;
    aN = Math.floor(aN / 1000);
    if (aN > 0) {
      if (k < 10) {
        s.unshift(",00", k);
      } else if (k < 100) {
        s.unshift(",0", k);
      } else {
        s.unshift(",", k);
      }
    } else {
      s.unshift(k);
      break;
    }
  }
  if (neg) {
    s.unshift("-");
  }
  if (aExtra) {
    s.push(aExtra);
  }
  return s.join("");
}

/**
 * Converts a byte count to an appropriate string representation.
 *
 * @param aBytes
 *        The byte count.
 * @return The string representation.
 */
function formatBytes(aBytes)
{
  let unit = gVerbose ? " B" : " MB";

  let s;
  if (gVerbose) {
    s = formatInt(aBytes, unit);
  } else {
    let mbytes = (aBytes / (1024 * 1024)).toFixed(2);
    let a = String(mbytes).split(".");
    // If the argument to formatInt() is -0, it will print the negative sign.
    s = formatInt(Number(a[0])) + "." + a[1] + unit;
  }
  return s;
}

/**
 * Converts a percentage to an appropriate string representation.
 *
 * @param aPerc100x
 *        The percentage, multiplied by 100 (see nsIMemoryReporter).
 * @return The string representation
 */
function formatPercentage(aPerc100x)
{
  return (aPerc100x / 100).toFixed(2) + "%";
}

/**
 * Right-justifies a string in a field of a given width, padding as necessary.
 *
 * @param aS
 *        The string.
 * @param aN
 *        The field width.
 * @param aC
 *        The char used to pad.
 * @return The string representation.
 */
function pad(aS, aN, aC)
{
  let padding = "";
  let n2 = aN - aS.length;
  for (let i = 0; i < n2; i++) {
    padding += aC;
  }
  return padding + aS;
}

// There's a subset of the Unicode "light" box-drawing chars that is widely
// implemented in terminals, and this code sticks to that subset to maximize
// the chance that cutting and pasting about:memory output to a terminal will
// work correctly.
const kHorizontal                   = "\u2500",
      kVertical                     = "\u2502",
      kUpAndRight                   = "\u2514",
      kUpAndRight_Right_Right       = "\u2514\u2500\u2500",
      kVerticalAndRight             = "\u251c",
      kVerticalAndRight_Right_Right = "\u251c\u2500\u2500",
      kVertical_Space_Space         = "\u2502  ";

const kNoKidsSep                    = " \u2500\u2500 ",
      kHideKidsSep                  = " ++ ",
      kShowKidsSep                  = " -- ";

function appendMrNameSpan(aP, aDescription, aUnsafeName, aIsInvalid, aNMerged)
{
  let safeName = flipBackslashes(aUnsafeName);
  if (!aIsInvalid && !aNMerged) {
    safeName += "\n";
  }
  let nameSpan = appendElementWithText(aP, "span", "mrName", safeName);
  nameSpan.title = aDescription;

  if (aIsInvalid) {
    let noteText = " [?!]";
    if (!aNMerged) {
      noteText += "\n";
    }
    let noteSpan = appendElementWithText(aP, "span", "mrNote", noteText);
    noteSpan.title =
      "Warning: this value is invalid and indicates a bug in one or more " +
      "memory reporters. ";
  }

  if (aNMerged) {
    let noteSpan = appendElementWithText(aP, "span", "mrNote",
                                         " [" + aNMerged + "]\n");
    noteSpan.title =
      "This value is the sum of " + aNMerged +
      " memory reporters that all have the same path.";
  }
}

// This is used to record the (safe) IDs of which sub-trees have been manually
// expanded (marked as true) and collapsed (marked as false).  It's used to
// replicate the collapsed/expanded state when the page is updated.  It can end
// up holding IDs of nodes that no longer exist, e.g. for compartments that
// have been closed.  This doesn't seem like a big deal, because the number is
// limited by the number of entries the user has changed from their original
// state.
let gShowSubtreesBySafeTreeId = {};

function assertClassListContains(e, className) {
  assert(e, "undefined " + className);
  assert(e.classList.contains(className), "classname isn't " + className);
}

function toggle(aEvent)
{
  // This relies on each line being a span that contains at least four spans:
  // mrValue, mrPerc, mrSep, mrName, and then zero or more mrNotes.  All
  // whitespace must be within one of these spans for this function to find the
  // right nodes.  And the span containing the children of this line must
  // immediately follow.  Assertions check this.

  // |aEvent.target| will be one of the spans.  Get the outer span.
  let outerSpan = aEvent.target.parentNode;
  assertClassListContains(outerSpan, "hasKids");

  // Toggle the '++'/'--' separator.
  let isExpansion;
  let sepSpan = outerSpan.childNodes[2];
  assertClassListContains(sepSpan, "mrSep");
  if (sepSpan.textContent === kHideKidsSep) {
    isExpansion = true;
    sepSpan.textContent = kShowKidsSep;
  } else if (sepSpan.textContent === kShowKidsSep) {
    isExpansion = false;
    sepSpan.textContent = kHideKidsSep;
  } else {
    assert(false, "bad sepSpan textContent");
  }

  // Toggle visibility of the span containing this node's children.
  let subTreeSpan = outerSpan.nextSibling;
  assertClassListContains(subTreeSpan, "kids");
  subTreeSpan.classList.toggle("hidden");

  // Record/unrecord that this sub-tree was toggled.
  let safeTreeId = outerSpan.id;
  if (gShowSubtreesBySafeTreeId[safeTreeId] !== undefined) {
    delete gShowSubtreesBySafeTreeId[safeTreeId];
  } else {
    gShowSubtreesBySafeTreeId[safeTreeId] = isExpansion;
  }
}

function expandPathToThisElement(aElement)
{
  if (aElement.classList.contains("kids")) {
    // Unhide the kids.
    aElement.classList.remove("hidden");
    expandPathToThisElement(aElement.previousSibling);  // hasKids

  } else if (aElement.classList.contains("hasKids")) {
    // Change the separator to '--'.
    let sepSpan = aElement.childNodes[2];
    assertClassListContains(sepSpan, "mrSep");
    sepSpan.textContent = kShowKidsSep;
    expandPathToThisElement(aElement.parentNode);       // kids or pre.entries

  } else {
    assertClassListContains(aElement, "entries");
  }
}

/**
 * Appends the elements for the tree, including its heading.
 *
 * @param aP
 *        The parent DOM node.
 * @param aRoot
 *        The tree root.
 * @param aProcess
 *        The process the tree corresponds to.
 * @param aPadText
 *        A string to pad the start of each entry.
 */
function appendTreeElements(aP, aRoot, aProcess, aPadText)
{
  /**
   * Appends the elements for a particular tree, without a heading.
   *
   * @param aP
   *        The parent DOM node.
   * @param aProcess
   *        The process the tree corresponds to.
   * @param aUnsafeNames
   *        An array of the names forming the path to aT.
   * @param aRoot
   *        The root of the tree this sub-tree belongs to.
   * @param aT
   *        The tree.
   * @param aTreelineText1
   *        The first part of the treeline for this entry and this entry's
   *        children.
   * @param aTreelineText2a
   *        The second part of the treeline for this entry.
   * @param aTreelineText2b
   *        The second part of the treeline for this entry's children.
   * @param aParentStringLength
   *        The length of the formatted byte count of the top node in the tree.
   */
  function appendTreeElements2(aP, aProcess, aUnsafeNames, aRoot, aT,
                               aTreelineText1, aTreelineText2a,
                               aTreelineText2b, aParentStringLength)
  {
    function appendN(aS, aC, aN)
    {
      for (let i = 0; i < aN; i++) {
        aS += aC;
      }
      return aS;
    }

    // The tree line.  Indent more if this entry is narrower than its parent.
    let valueText = aT.toString();
    let extraTreelineLength =
      Math.max(aParentStringLength - valueText.length, 0);
    if (extraTreelineLength > 0) {
      aTreelineText2a =
        appendN(aTreelineText2a, kHorizontal, extraTreelineLength);
      aTreelineText2b =
        appendN(aTreelineText2b, " ",         extraTreelineLength);
    }
    let treelineText = aTreelineText1 + aTreelineText2a;
    appendElementWithText(aP, "span", "treeline", treelineText);

    // Detect and record invalid values.
    assertInput(aRoot._units === aT._units,
                "units within a tree are inconsistent");
    let tIsInvalid = false;
    if (!(0 <= aT._amount && aT._amount <= aRoot._amount)) {
      tIsInvalid = true;
      let unsafePath = aUnsafeNames.join("/");
      gUnsafePathsWithInvalidValuesForThisProcess.push(unsafePath);
      reportAssertionFailure("Invalid value for " + flipBackslashes(unsafePath));
    }

    // For non-leaf nodes, the entire sub-tree is put within a span so it can
    // be collapsed if the node is clicked on.
    let d;
    let sep;
    let showSubtrees;
    if (aT._kids) {
      // Determine if we should show the sub-tree below this entry;  this
      // involves reinstating any previous toggling of the sub-tree.
      let unsafePath = aUnsafeNames.join("/");
      let safeTreeId = aProcess + ":" + flipBackslashes(unsafePath);
      showSubtrees = !aT._hideKids;
      if (gShowSubtreesBySafeTreeId[safeTreeId] !== undefined) {
        showSubtrees = gShowSubtreesBySafeTreeId[safeTreeId];
      }
      d = appendElement(aP, "span", "hasKids");
      d.id = safeTreeId;
      d.onclick = toggle;
      sep = showSubtrees ? kShowKidsSep : kHideKidsSep;
    } else {
      assert(!aT._hideKids, "leaf node with _hideKids set")
      sep = kNoKidsSep;
      d = aP;
    }

    // The value.
    appendElementWithText(d, "span", "mrValue" + (tIsInvalid ? " invalid" : ""),
                          valueText);

    // The percentage (omitted for single entries).
    let percText;
    if (!aT._isDegenerate) {
      // Treat 0 / 0 as 100%.
      let num = aRoot._amount === 0 ? 100 : (100 * aT._amount / aRoot._amount);
      let numText = num.toFixed(2);
      percText = numText === "100.00"
               ? " (100.0%)"
               : (0 <= num && num < 10 ? " (0" : " (") + numText + "%)";
      appendElementWithText(d, "span", "mrPerc", percText);
    }

    // The separator.
    appendElementWithText(d, "span", "mrSep", sep);

    // The entry's name.
    appendMrNameSpan(d, aT._description, aT._unsafeName,
                     tIsInvalid, aT._nMerged);

    // In non-verbose mode, invalid nodes can be hidden in collapsed sub-trees.
    // But it's good to always see them, so force this.
    if (!gVerbose && tIsInvalid) {
      expandPathToThisElement(d);
    }

    // Recurse over children.
    if (aT._kids) {
      // The 'kids' class is just used for sanity checking in toggle().
      d = appendElement(aP, "span", showSubtrees ? "kids" : "kids hidden");

      let kidTreelineText1 = aTreelineText1 + aTreelineText2b;
      for (let i = 0; i < aT._kids.length; i++) {
        let kidTreelineText2a, kidTreelineText2b;
        if (i < aT._kids.length - 1) {
          kidTreelineText2a = kVerticalAndRight_Right_Right;
          kidTreelineText2b = kVertical_Space_Space;
        } else {
          kidTreelineText2a = kUpAndRight_Right_Right;
          kidTreelineText2b = "   ";
        }
        aUnsafeNames.push(aT._kids[i]._unsafeName);
        appendTreeElements2(d, aProcess, aUnsafeNames, aRoot, aT._kids[i],
                            kidTreelineText1, kidTreelineText2a,
                            kidTreelineText2b, valueText.length);
        aUnsafeNames.pop();
      }
    }
  }

  let rootStringLength = aRoot.toString().length;
  appendTreeElements2(aP, aProcess, [aRoot._unsafeName], aRoot, aRoot,
                      aPadText, "", "", rootStringLength);
}

//---------------------------------------------------------------------------

function appendSectionHeader(aP, aText)
{
  appendElementWithText(aP, "h2", "", aText + "\n");
  return appendElement(aP, "pre", "entries");
}

//-----------------------------------------------------------------------------
// Code specific to about:compartments
//-----------------------------------------------------------------------------

function onLoadAboutCompartments()
{
  // First generate the page, then minimize memory usage to collect any dead
  // compartments, then update the page.  The first generation step may sound
  // unnecessary, but it avoids a short delay in showing content when the page
  // is loaded, which makes test_aboutcompartments.xul more reliable (see bug
  // 729018 for details).
  updateAboutCompartments();
  gMgr.minimizeMemoryUsage(
    function() { addChildObserversAndUpdate(updateAboutCompartments); });
}

/**
 * Top-level function that does the work of generating the page.
 */
function updateAboutCompartments()
{
  // First, clear the page contents.  Necessary because
  // updateAboutCompartments() might be called more than once due to the
  // "child-memory-reporter-update" observer.
  let body = clearBody();

  let compartmentsByProcess = getCompartmentsByProcess();
  let ghostWindowsByProcess = getGhostWindowsByProcess();

  function handleProcess(aProcess) {
    let section = appendElement(body, 'div', 'section');
    appendProcessAboutCompartmentsElements(section, aProcess,
                                           compartmentsByProcess[aProcess],
                                           ghostWindowsByProcess[aProcess]);
  }

  // Generate output for one process at a time.  Always start with the
  // Main process.
  handleProcess(gUnnamedProcessStr);
  for (let process in compartmentsByProcess) {
    if (process !== gUnnamedProcessStr) {
      handleProcess(process);
    }
  }

  let section = appendElement(body, 'div', 'footer');
  if (gVerbose) {
    let a = appendElementWithText(section, "a", "option", "Less verbose");
    a.href = "about:compartments";
  } else {
    let a = appendElementWithText(section, "a", "option", "More verbose");
    a.href = "about:compartments?verbose";
  }
}

//---------------------------------------------------------------------------

function Compartment(aUnsafeName, aIsSystemCompartment)
{
  this._unsafeName          = aUnsafeName;
  this._isSystemCompartment = aIsSystemCompartment;
  // this._nMerged is only defined if > 1
}

Compartment.prototype = {
  merge: function(r) {
    this._nMerged = this._nMerged ? this._nMerged + 1 : 2;
  }
};

function getCompartmentsByProcess()
{
  // Ignore anything that didn't come from the "compartments" multi-reporter.
  // (Note that some such reports can reach here as single reports if they were
  // in the child process.)

  function ignoreSingle(aUnsafePath)
  {
    return !aUnsafePath.startsWith("compartments/");
  }

  function ignoreMulti(aMRName)
  {
    return aMRName !== "compartments";
  }

  let compartmentsByProcess = {};

  function handleReport(aProcess, aUnsafePath, aKind, aUnits, aAmount,
                        aDescription)
  {
    let process = aProcess === "" ? gUnnamedProcessStr : aProcess;
    let unsafeNames = aUnsafePath.split('/');
    let isSystemCompartment;
    if (unsafeNames[0] === "compartments" && unsafeNames[1] == "system" &&
        unsafeNames.length == 3)
    {
      isSystemCompartment = true;

    } else if (unsafeNames[0] === "compartments" && unsafeNames[1] == "user" &&
        unsafeNames.length == 3)
    {
      isSystemCompartment = false;
      // These null principal compartments are user compartments according to
      // the JS engine, but they look odd being shown with content
      // compartments, so we put them in the system compartments list.
      if (unsafeNames[2].startsWith("moz-nullprincipal:{")) {
        isSystemCompartment = true;
      }

    } else {
      assertInput(false, "bad compartments path: " + aUnsafePath);
    }
    assertInput(aKind === KIND_OTHER,   "bad compartments kind");
    assertInput(aUnits === UNITS_COUNT, "bad compartments units");
    assertInput(aAmount === 1,          "bad compartments amount");
    assertInput(aDescription === "",    "bad compartments description");

    let c = new Compartment(unsafeNames[2], isSystemCompartment);

    if (!compartmentsByProcess[process]) {
      compartmentsByProcess[process] = {};
    }
    let compartments = compartmentsByProcess[process];
    let cOld = compartments[c._unsafeName];
    if (cOld) {
      // Already an entry;  must be a duplicated compartment.  This can happen
      // legitimately.  Merge them.
      cOld.merge(c);
    } else {
      compartments[c._unsafeName] = c;
    }
  }

  processMemoryReporters(ignoreSingle, ignoreMulti, handleReport);

  return compartmentsByProcess;
}

function GhostWindow(aUnsafeURL)
{
  // Call it _unsafeName rather than _unsafeURL for symmetry with the
  // Compartment object.
  this._unsafeName = aUnsafeURL;

  // this._nMerged is only defined if > 1
}

GhostWindow.prototype = {
  merge: function(r) {
    this._nMerged = this._nMerged ? this._nMerged + 1 : 2;
  }
};

function getGhostWindowsByProcess()
{
  function ignoreSingle(aUnsafePath)
  {
    return !aUnsafePath.startsWith('ghost-windows/')
  }

  function ignoreMulti(aName)
  {
    return aName !== "ghost-windows";
  }

  let ghostWindowsByProcess = {};

  function handleReport(aProcess, aUnsafePath, aKind, aUnits, aAmount,
                        aDescription)
  {
    let unsafeSplit = aUnsafePath.split('/');
    assertInput(unsafeSplit[0] === 'ghost-windows' && unsafeSplit.length === 2,
           'Unexpected path in getGhostWindowsByProcess: ' + aUnsafePath);
    assertInput(aKind === KIND_OTHER,   "bad ghost-windows kind");
    assertInput(aUnits === UNITS_COUNT, "bad ghost-windows units");
    assertInput(aAmount === 1,          "bad ghost-windows amount");
    assertInput(aDescription === "",    "bad ghost-windows description");

    let unsafeURL = unsafeSplit[1];
    let ghostWindow = new GhostWindow(unsafeURL);

    let process = aProcess === "" ? gUnnamedProcessStr : aProcess;
    if (!ghostWindowsByProcess[process]) {
      ghostWindowsByProcess[process] = {};
    }

    if (ghostWindowsByProcess[process][unsafeURL]) {
      ghostWindowsByProcess[process][unsafeURL].merge(ghostWindow);
    }
    else {
      ghostWindowsByProcess[process][unsafeURL] = ghostWindow;
    }
  }

  processMemoryReporters(ignoreSingle, ignoreMulti, handleReport);

  return ghostWindowsByProcess;
}

//---------------------------------------------------------------------------

function appendProcessAboutCompartmentsElementsHelper(aP, aEntries, aKindString)
{
  // aEntries might be null or undefined, e.g. if there are no ghost windows
  // for this process.
  aEntries = aEntries ? aEntries : {};

  appendElementWithText(aP, "h2", "", aKindString + "\n");

  let uPre = appendElement(aP, "pre", "entries");

  let lines = [];
  for (let name in aEntries) {
    let e = aEntries[name];
    let line = flipBackslashes(e._unsafeName);
    if (e._nMerged) {
      line += ' [' + e._nMerged + ']';
    }
    line += '\n';
    lines.push(line);
  }
  lines.sort();

  for (let i = 0; i < lines.length; i++) {
    appendElementWithText(uPre, "span", "", lines[i]);
  }

  appendTextNode(aP, "\n");   // gives nice spacing when we cut and paste
}

/**
 * Appends the elements for a single process.
 *
 * @param aP
 *        The parent DOM node.
 * @param aProcess
 *        The name of the process.
 * @param aCompartments
 *        Table of Compartments for this process, indexed by _unsafeName.
 * @param aGhostWindows
 *        Array of window URLs of ghost windows.
 */
function appendProcessAboutCompartmentsElements(aP, aProcess, aCompartments, aGhostWindows)
{
  appendElementWithText(aP, "h1", "", aProcess + "\n\n");

  let userCompartments = {};
  let systemCompartments = {};
  for (let name in aCompartments) {
    let c = aCompartments[name];
    if (c._isSystemCompartment) {
      systemCompartments[name] = c;
    }
    else {
      userCompartments[name] = c;
    }
  }

  appendProcessAboutCompartmentsElementsHelper(aP, userCompartments, "User Compartments");
  appendProcessAboutCompartmentsElementsHelper(aP, systemCompartments, "System Compartments");
  appendProcessAboutCompartmentsElementsHelper(aP, aGhostWindows, "Ghost Windows");
}

