/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that the style inspector works properly

let doc;
let stylePanel;
let cssHtmlTree;

XPCOMUtils.defineLazyGetter(this, "osString", function() {
  return Cc["@mozilla.org/xre/app-info;1"].getService(Ci.nsIXULRuntime).OS;
});

function createDocument()
{
  doc.body.innerHTML = '<style type="text/css"> ' +
    'span { font-variant: small-caps; color: #000000; } ' +
    '.nomatches {color: #ff0000;}</style> <div id="first" style="margin: 10em; ' +
    'font-size: 14pt; font-family: helvetica, sans-serif; color: #AAA">\n' +
    '<h1>Some header text</h1>\n' +
    '<p id="salutation" style="font-size: 12pt">hi.</p>\n' +
    '<p id="body" style="font-size: 12pt">I am a test-case. This text exists ' +
    'solely to provide some things to <span style="color: yellow">' +
    'highlight</span> and <span style="font-weight: bold">count</span> ' +
    'style list-items in the box at right. If you are reading this, ' +
    'you should go do something else instead. Maybe read a book. Or better ' +
    'yet, write some test-cases for another bit of code. ' +
    '<span style="font-style: italic">some text</span></p>\n' +
    '<p id="closing">more text</p>\n' +
    '<p>even more text</p>' +
    '</div>';
  doc.title = "Computed view context menu test";

  let span = doc.querySelector("span");
  ok(span, "captain, we have the span");

  stylePanel = new ComputedViewPanel(window);
  Services.obs.addObserver(runStyleInspectorTests, "StyleInspector-populated", false);
  stylePanel.createPanel(span);
}

function runStyleInspectorTests()
{
  Services.obs.removeObserver(runStyleInspectorTests, "StyleInspector-populated", false);

  cssHtmlTree = stylePanel.cssHtmlTree;

  let contentDocument = stylePanel.iframe.contentDocument;
  let prop = contentDocument.querySelector(".property-view");
  ok(prop, "captain, we have the property-view node");

  // We need the context menu to open in the correct place in order for
  // popupNode to be propertly set.
  EventUtils.synthesizeMouse(prop, 1, 1, { type: "contextmenu", button: 2 },
    stylePanel.iframe.contentWindow);

  checkCopyProperty()
}

function checkCopyProperty()
{
  info("Checking that cssHtmlTree.siBoundCopyDeclaration() returns the " +
       "correct clipboard value");
  let expectedPattern = "color: rgb\\(255, 255, 0\\);";

  SimpleTest.waitForClipboard(function CS_boundCopyPropCheck() {
      return checkClipboardData(expectedPattern);
    },
    cssHtmlTree.siBoundCopyDeclaration,
    checkCopyPropertyName, function() {
      failedClipboard(expectedPattern, checkCopyPropertyName);
    });
}

function checkCopyPropertyName()
{
  info("Checking that cssHtmlTree.siBoundCopyProperty() returns the " +
       "correct clipboard value");
  let expectedPattern = "color";

  SimpleTest.waitForClipboard(function CS_boundCopyPropNameCheck() {
      return checkClipboardData(expectedPattern);
    },
    cssHtmlTree.siBoundCopyProperty,
    checkCopyPropertyValue, function() {
      failedClipboard(expectedPattern, checkCopyPropertyValue);
    });
}

function checkCopyPropertyValue()
{
  info("Checking that cssHtmlTree.siBoundCopyPropertyValue() returns the " +
       "correct clipboard value");
  let expectedPattern = "rgb\\(255, 255, 0\\)";

  SimpleTest.waitForClipboard(function CS_boundCopyPropValueCheck() {
      return checkClipboardData(expectedPattern);
    },
    cssHtmlTree.siBoundCopyPropertyValue,
    checkCopySelection, function() {
      failedClipboard(expectedPattern, checkCopySelection);
    });
}

function checkCopySelection()
{
  let contentDocument = stylePanel.iframe.contentDocument;
  let contentWindow = stylePanel.iframe.contentWindow;
  let props = contentDocument.querySelectorAll(".property-view");
  ok(props, "captain, we have the property-view nodes");

  let range = document.createRange();
  range.setStart(props[0], 0);
  range.setEnd(props[3], 3);
  contentWindow.getSelection().addRange(range);

  info("Checking that cssHtmlTree.siBoundCopyPropertyValue() " +
       " returns the correct clipboard value");

  let expectedPattern = "color: rgb\\(255, 255, 0\\)[\\r\\n]+" +
                 "font-family: helvetica,sans-serif[\\r\\n]+" +
                 "font-size: 16px[\\r\\n]+" +
                 "font-variant: small-caps[\\r\\n]*";

  SimpleTest.waitForClipboard(function CS_boundCopyCheck() {
      return checkClipboardData(expectedPattern);
    },
    cssHtmlTree.siBoundCopy, closeStyleInspector, function() {
      failedClipboard(expectedPattern, closeStyleInspector);
    });
}

function checkClipboardData(aExpectedPattern)
{
  let actual = SpecialPowers.getClipboardData("text/unicode");
  let expectedRegExp = new RegExp(aExpectedPattern, "g");
  return expectedRegExp.test(actual);
}

function failedClipboard(aExpectedPattern, aCallback)
{
  // Format expected text for comparison
  let terminator = osString == "WINNT" ? "\r\n" : "\n";
  aExpectedPattern = aExpectedPattern.replace(/\[\\r\\n\][+*]/g, terminator);
  aExpectedPattern = aExpectedPattern.replace(/\\\(/g, "(");
  aExpectedPattern = aExpectedPattern.replace(/\\\)/g, ")");

  let actual = SpecialPowers.getClipboardData("text/unicode");

  // Trim the right hand side of our strings. This is because expectedPattern
  // accounts for windows sometimes adding a newline to our copied data.
  aExpectedPattern = aExpectedPattern.trimRight();
  actual = actual.trimRight();

  dump("TEST-UNEXPECTED-FAIL | Clipboard text does not match expected ... " +
    "results (escaped for accurate comparison):\n");
  info("Actual: " + escape(actual));
  info("Expected: " + escape(aExpectedPattern));
  aCallback();
}

function closeStyleInspector()
{
  stylePanel.destroy();
  finishUp();
}

function finishUp()
{
  doc = stylePanel = cssHtmlTree = null;
  gBrowser.removeCurrentTab();
  finish();
}

function test()
{
  waitForExplicitFinish();
  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function(evt) {
    gBrowser.selectedBrowser.removeEventListener(evt.type, arguments.callee, true);
    doc = content.document;
    waitForFocus(createDocument, content);
  }, true);

  content.location = "data:text/html,computed view context menu test";
}
