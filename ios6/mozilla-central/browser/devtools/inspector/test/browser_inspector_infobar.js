/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

function test()
{
  waitForExplicitFinish();
  ignoreAllUncaughtExceptions();

  let doc;
  let nodes;
  let cursor;
  let inspector;

  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function onload() {
    gBrowser.selectedBrowser.removeEventListener("load", onload, true);
    doc = content.document;
    waitForFocus(setupInfobarTest, content);
  }, true);

  let style = "body{width:100%;height: 100%} div {position: absolute;height: 100px;width: 500px}#bottom {bottom: 0px}#vertical {height: 100%}#farbottom{bottom: -200px}";
  let html = "<style>" + style + "</style><div id=vertical></div><div id=top class='class1 class2'></div><div id=bottom></div><div id=farbottom></div>"

  content.location = "data:text/html," + encodeURIComponent(html);

  function setupInfobarTest()
  {
    nodes = [
      {node: doc.querySelector("#top"), position: "bottom", tag: "DIV", id: "#top", classes: ".class1.class2"},
      {node: doc.querySelector("#vertical"), position: "overlap", tag: "DIV", id: "#vertical", classes: ""},
      {node: doc.querySelector("#bottom"), position: "top", tag: "DIV", id: "#bottom", classes: ""},
      {node: doc.querySelector("body"), position: "overlap", tag: "BODY", id: "", classes: ""},
      {node: doc.querySelector("#farbottom"), position: "top", tag: "DIV", id: "#farbottom", classes: ""},
    ]

    for (let i = 0; i < nodes.length; i++) {
      ok(nodes[i].node, "node " + i + " found");
    }

    openInspector(runTests);
  }

  function runTests(aInspector)
  {
    inspector = aInspector;
    cursor = 0;
    executeSoon(function() {
      inspector.selection.setNode(nodes[0].node, "");
      nodeSelected();
    });
  }

  function nodeSelected()
  {
    executeSoon(function() {
      performTest();
      cursor++;
      if (cursor >= nodes.length) {
        finishUp();
      } else {
        let node = nodes[cursor].node;
        inspector.selection.setNode(node, "");
        nodeSelected();
      }
    });
  }

  function performTest()
  {
    let browser = gBrowser.selectedBrowser;
    let stack = browser.parentNode;

    let container = stack.querySelector(".highlighter-nodeinfobar-container");
    is(container.getAttribute("position"), nodes[cursor].position, "node " + cursor + ": position matches.");

    let tagNameLabel = stack.querySelector(".highlighter-nodeinfobar-tagname");
    is(tagNameLabel.textContent, nodes[cursor].tag, "node " + cursor  + ": tagName matches.");

    let idLabel = stack.querySelector(".highlighter-nodeinfobar-id");
    is(idLabel.textContent, nodes[cursor].id, "node " + cursor  + ": id matches.");

    let classesBox = stack.querySelector(".highlighter-nodeinfobar-classes");
    is(classesBox.textContent, nodes[cursor].classes, "node " + cursor  + ": classes match.");
  }

  function finishUp() {
    doc = nodes = null;
    gBrowser.removeCurrentTab();
    finish();
  }
}

