/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

let tempScope = {};
Cu.import("resource://gre/modules/NetUtil.jsm", tempScope);
Cu.import("resource://gre/modules/FileUtils.jsm", tempScope);
let NetUtil = tempScope.NetUtil;
let FileUtils = tempScope.FileUtils;

// Reference to the Scratchpad object.
let gScratchpad;

// Reference to the temporary nsIFiles.
let gFile;

// Temporary file name.
let gFileName = "testFileForBug751744.tmp"


// Content for the temporary file.
let gFileContent = "/* this file is already saved */\n" +
                   "function foo() { alert('bar') }";
let gLength = gFileContent.length;

// Reference to the menu entry.
let menu;

function startTest()
{
  gScratchpad = gScratchpadWindow.Scratchpad;
  menu = gScratchpadWindow.document.getElementById("sp-menu-revert");
  createAndLoadTemporaryFile();
}

function testAfterSaved() {
  // Check if the revert menu is disabled as the file is at saved state.
  ok(menu.hasAttribute("disabled"), "The revert menu entry is disabled.");

  // chancging the text in the file
  gScratchpad.setText("\nfoo();", gLength, gLength);
  // Checking the text got changed
  is(gScratchpad.getText(), gFileContent + "\nfoo();",
     "The text changed the first time.");

  // Revert menu now should be enabled.
  ok(!menu.hasAttribute("disabled"),
     "The revert menu entry is enabled after changing text first time");

  // reverting back to last saved state.
  gScratchpad.revertFile(testAfterRevert);
}

function testAfterRevert() {
  // Check if the file's text got reverted
  is(gScratchpad.getText(), gFileContent,
     "The text reverted back to original text.");
  // The revert menu should be disabled again.
  ok(menu.hasAttribute("disabled"),
     "The revert menu entry is disabled after reverting.");

  // chancging the text in the file again
  gScratchpad.setText("\nalert(foo.toSource());", gLength, gLength);
  // Saving the file.
  gScratchpad.saveFile(testAfterSecondSave);
}

function testAfterSecondSave() {
  // revert menu entry should be disabled.
  ok(menu.hasAttribute("disabled"),
     "The revert menu entry is disabled after saving.");

  // changing the text.
  gScratchpad.setText("\nfoo();", gLength + 23, gLength + 23);

  // revert menu entry should get enabled yet again.
  ok(!menu.hasAttribute("disabled"),
     "The revert menu entry is enabled after changing text third time");

  // reverting back to last saved state.
  gScratchpad.revertFile(testAfterSecondRevert);
}

function testAfterSecondRevert() {
  // Check if the file's text got reverted
  is(gScratchpad.getText(), gFileContent + "\nalert(foo.toSource());",
     "The text reverted back to the changed saved text.");
  // The revert menu should be disabled again.
  ok(menu.hasAttribute("disabled"),
     "Revert menu entry is disabled after reverting to changed saved state.");
  gFile.remove(false);
  gFile = null;
  gScratchpad = null;
}

function createAndLoadTemporaryFile()
{
  // Create a temporary file.
  gFile = FileUtils.getFile("TmpD", [gFileName]);
  gFile.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0666);

  // Write the temporary file.
  let fout = Cc["@mozilla.org/network/file-output-stream;1"].
             createInstance(Ci.nsIFileOutputStream);
  fout.init(gFile.QueryInterface(Ci.nsILocalFile), 0x02 | 0x08 | 0x20,
            0644, fout.DEFER_OPEN);

  let converter = Cc["@mozilla.org/intl/scriptableunicodeconverter"].
                  createInstance(Ci.nsIScriptableUnicodeConverter);
  converter.charset = "UTF-8";
  let fileContentStream = converter.convertToInputStream(gFileContent);

  NetUtil.asyncCopy(fileContentStream, fout, tempFileSaved);
}

function tempFileSaved(aStatus)
{
  ok(Components.isSuccessCode(aStatus),
     "the temporary file was saved successfully");

  // Import the file into Scratchpad.
  gScratchpad.setFilename(gFile.path);
  gScratchpad.importFromFile(gFile.QueryInterface(Ci.nsILocalFile),  true,
                             testAfterSaved);
}

function test()
{
  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function onLoad() {
    gBrowser.selectedBrowser.removeEventListener("load", onLoad, true);
    openScratchpad(startTest);
  }, true);

  content.location = "data:text/html,<p>test reverting to last saved state of" +
                     " a file </p>";
}
