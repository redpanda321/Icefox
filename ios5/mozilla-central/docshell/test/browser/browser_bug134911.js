/* The test text decoded correctly as Shift_JIS */
const rightText="\u30E6\u30CB\u30B3\u30FC\u30C9\u306F\u3001\u3059\u3079\u3066\u306E\u6587\u5B57\u306B\u56FA\u6709\u306E\u756A\u53F7\u3092\u4ED8\u4E0E\u3057\u307E\u3059";

const enteredText1="The quick brown fox jumps over the lazy dog";
const enteredText2="\u03BE\u03B5\u03C3\u03BA\u03B5\u03C0\u03AC\u03B6\u03C9\u0020\u03C4\u1F74\u03BD\u0020\u03C8\u03C5\u03C7\u03BF\u03C6\u03B8\u03CC\u03C1\u03B1\u0020\u03B2\u03B4\u03B5\u03BB\u03C5\u03B3\u03BC\u03AF\u03B1";

function test() {
  waitForExplicitFinish();

  var rootDir = getRootDirectory(gTestPath);
  gBrowser.selectedTab = gBrowser.addTab(rootDir + "test-form_sjis.html");
  gBrowser.selectedBrowser.addEventListener("load", afterOpen, true);
}

function afterOpen() {
  gBrowser.selectedBrowser.removeEventListener("load", afterOpen, true);
  gBrowser.selectedBrowser.addEventListener("load", afterChangeCharset, true);

  gBrowser.contentDocument.getElementById("testtextarea").value = enteredText1;
  gBrowser.contentDocument.getElementById("testinput").value = enteredText2;

  /* Force the page encoding to Shift_JIS */
  BrowserSetForcedCharacterSet("Shift_JIS");
}
  
function afterChangeCharset() {
  gBrowser.selectedBrowser.removeEventListener("load", afterChangeCharset, true);

  is(gBrowser.contentDocument.getElementById("testpar").innerHTML, rightText,
     "encoding successfully changed");
  is(gBrowser.contentDocument.getElementById("testtextarea").value, enteredText1,
     "text preserved in <textarea>");
  is(gBrowser.contentDocument.getElementById("testinput").value, enteredText2,
     "text preserved in <input>");

  gBrowser.removeCurrentTab();
  finish();
}
