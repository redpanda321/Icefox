/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const TESTCASE_URI_HTML = TEST_BASE + "simple.html";
const TESTCASE_URI_CSS = TEST_BASE + "simple.css";

const Cc = Components.classes;
const Ci = Components.interfaces;

let tempScope = {};
Components.utils.import("resource://gre/modules/FileUtils.jsm", tempScope);
Components.utils.import("resource://gre/modules/NetUtil.jsm", tempScope);
let FileUtils = tempScope.FileUtils;
let NetUtil = tempScope.NetUtil;


function test()
{
  waitForExplicitFinish();

  copy(TESTCASE_URI_HTML, "simple.html", function(htmlFile) {
    copy(TESTCASE_URI_CSS, "simple.css", function(cssFile) {

      addTabAndLaunchStyleEditorChromeWhenLoaded(function (aChrome) {
        aChrome.addChromeListener({
          onEditorAdded: function (aChrome, aEditor) {
            if (aEditor.styleSheetIndex != 0) {
              return; // we want to test against the first stylesheet
            }

            if (aEditor.sourceEditor) {
              run(aEditor); // already attached to input element
            } else {
              aEditor.addActionListener({
                onAttach: run
              });
            }
          }
        });
      });

      let uri = Services.io.newFileURI(htmlFile);
      let filePath = uri.resolve("");

      content.location = filePath;
    });
  });
}

function run(aEditor)
{
  aEditor.saveToFile(null, function (aFile) {
    ok(aFile, "file should get saved directly when using a file:// URI");

    gChromeWindow.close();
    finish();
  });
}

function copy(aSrcChromeURL, aDestFileName, aCallback)
{
  let destFile = FileUtils.getFile("ProfD", [aDestFileName]);
  write(read(aSrcChromeURL), destFile, aCallback);
}

function read(aSrcChromeURL)
{
  let scriptableStream = Cc["@mozilla.org/scriptableinputstream;1"]
    .getService(Ci.nsIScriptableInputStream);

  let channel = Services.io.newChannel(aSrcChromeURL, null, null);
  let input = channel.open();
  scriptableStream.init(input);

  let data = scriptableStream.read(input.available());
  scriptableStream.close();
  input.close();

  return data;
}

function write(aData, aFile, aCallback)
{
  let converter = Cc["@mozilla.org/intl/scriptableunicodeconverter"]
    .createInstance(Ci.nsIScriptableUnicodeConverter);

  converter.charset = "UTF-8";

  let istream = converter.convertToInputStream(aData);
  let ostream = FileUtils.openSafeFileOutputStream(aFile);

  NetUtil.asyncCopy(istream, ostream, function(status) {
    if (!Components.isSuccessCode(status)) {
      info("Coudln't write to " + aFile.path);
      return;
    }

    aCallback(aFile);
  });
}
