function test () {
  waitForExplicitFinish();
  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function () {
    gBrowser.selectedBrowser.removeEventListener("load", arguments.callee, true);

    let doc = gBrowser.contentDocument;
    let tooltip = document.getElementById("aHTMLTooltip");
    ok(FillInHTMLTooltip(doc.getElementById("text1"), "should get title"));
    is(tooltip.getAttribute("label"), "    This            is a title    ");

    ok(!FillInHTMLTooltip(doc.getElementById("text2"), "should not get title"));

    ok(!FillInHTMLTooltip(doc.getElementById("text3"), "should not get title"));

    ok(FillInHTMLTooltip(doc.getElementById("link1"), "should get title"));
    is(tooltip.getAttribute("label"), "      This is a title    ");
    ok(FillInHTMLTooltip(doc.getElementById("text4"), "should get title"));
    is(tooltip.getAttribute("label"), "      This is a title    ");

    ok(!FillInHTMLTooltip(doc.getElementById("link2"), "should not get title"));

    ok(FillInHTMLTooltip(doc.getElementById("link3"), "should get title"));
    ok(tooltip.getAttribute("label") != "");

    ok(FillInHTMLTooltip(doc.getElementById("link4"), "should get title"));
    is(tooltip.getAttribute("label"), "This is an xlink:title attribute");

    ok(!FillInHTMLTooltip(doc.getElementById("text5"), "should not get title"));

    gBrowser.removeCurrentTab();
    finish();
  }, true);

  content.location = 
    "http://mochi.test:8888/browser/browser/base/content/test/title_test.svg";
}

