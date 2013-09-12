/* Any copyright", " is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that various editors work as expected.  Also checks
 * that the various changes are properly undoable and redoable.
 * For each step in the test, we:
 * - Run the setup for that test (if any)
 * - Check that the node we're editing is as we expect
 * - Make the change, check that the change was made as we expect
 * - Undo the change, check that the node is back in its original state
 * - Redo the change, check that the node change was made again correctly.
 *
 * This test mostly tries to verify that the editor makes changes to the
 * underlying DOM, not that the UI updates - UI updates are based on
 * underlying DOM changes, and the mutation tests should cover those cases.
 */

function test() {
  let inspector;
  let tempScope = {}
  Cu.import("resource:///modules/devtools/CssRuleView.jsm", tempScope);

  let inplaceEditor = tempScope._getInplaceEditorForSpan;

  waitForExplicitFinish();

  // Will hold the doc we're viewing
  let doc;

  // Holds the MarkupTool object we're testing.
  let markup;

  /**
   * Edit a given editableField
   */
  function editField(aField, aValue)
  {
    aField.focus();
    EventUtils.sendKey("return");
    let input = inplaceEditor(aField).input;
    input.value = aValue;
    input.blur();
  }

  function assertAttributes(aElement, aAttributes)
  {
    let attrs = Object.getOwnPropertyNames(aAttributes);
    is(aElement.attributes.length, attrs.length, "Node has the correct number of attributes");
    for (let attr of attrs) {
      is(aElement.getAttribute(attr), aAttributes[attr], "Node has the correct " + attr + " attribute.");
    }
  }

  // All the mutation types we want to test.
  let edits = [
    {
      desc: "Change an attribute",
      before: function() {
        assertAttributes(doc.querySelector("#node1"), {
          id: "node1",
          class: "node1"
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node1")).editor;
        let attr = editor.attrs["class"].querySelector(".editable");
        editField(attr, 'class="changednode1"');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node1"), {
          id: "node1",
          class: "changednode1"
        });
      }
    },

    {
      desc: "Try change an attribute to a badly formed string",
      before: function() {
        assertAttributes(doc.querySelector("#node22"), {
          id: "node22",
          class: "unchanged"
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node22")).editor;
        let attr = editor.attrs["class"].querySelector(".editable");
        editField(attr, 'class="""');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node22"), {
          id: "node22",
          class: "unchanged"
        });
      }
    },

    {
      desc: "Remove an attribute",
      before: function() {
        assertAttributes(doc.querySelector("#node4"), {
          id: "node4",
          class: "node4"
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node4")).editor;
        let attr = editor.attrs["class"].querySelector(".editable");
        editField(attr, '');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node4"), {
          id: "node4",
        });
      }
    },

    {
      desc: "Add an attribute by clicking the empty space after a node",
      before: function() {
        assertAttributes(doc.querySelector("#node14"), {
          id: "node14",
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node14")).editor;
        let attr = editor.newAttr;
        editField(attr, 'class="newclass" style="color:green"');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node14"), {
          id: "node14",
          class: "newclass",
          style: "color:green"
        });
      }
    },

    {
      desc: "Try add a badly formed attribute by clicking the empty space after a node",
      before: function() {
        assertAttributes(doc.querySelector("#node23"), {
          id: "node23",
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node23")).editor;
        let attr = editor.newAttr;
        editField(attr, 'class="newclass" style="""');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node23"), {
          id: "node23",
        });
      }
    },

    {
      desc: "Try add attributes by adding to an existing attribute's entry",
      before: function() {
        assertAttributes(doc.querySelector("#node24"), {
          id: "node24",
        });
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node24")).editor;
        let attr = editor.attrs["id"].querySelector(".editable");
        editField(attr, attr.textContent + ' class="""');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node24"), {
          id: "node24",
        });
      }
    },

    {
      desc: "Edit text",
      before: function() {
        let node = doc.querySelector('.node6').firstChild;
        is(node.nodeValue, "line6", "Text should be unchanged");
      },
      execute: function() {
        let node = doc.querySelector('.node6').firstChild;
        let editor = markup.getContainer(node).editor;
        let field = editor.elt.querySelector("pre");
        editField(field, "New text");
      },
      after: function() {
        let node = doc.querySelector('.node6').firstChild;
        is(node.nodeValue, "New text", "Text should be changed.");
      },
    },
  ];

  // Create the helper tab for parsing...
  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function onload() {
    gBrowser.selectedBrowser.removeEventListener("load", onload, true);
    doc = content.document;
    waitForFocus(setupTest, content);
  }, true);
  content.location = "http://mochi.test:8888/browser/browser/devtools/markupview/test/browser_inspector_markup_edit.html";

  function setupTest() {
    var target = TargetFactory.forTab(gBrowser.selectedTab);
    gDevTools.showToolbox(target, "inspector").then(function(toolbox) {
      inspector = toolbox.getCurrentPanel();
      runTests();
    });
  }

  function runTests() {
    inspector.selection.once("new-node", startTests);
    executeSoon(function() {
      inspector.selection.setNode(doc.body);
    });
  }

  function startTests() {
    let startNode = doc.documentElement.cloneNode();
    markup = inspector.markup;
    markup.expandAll();
    for (let step of edits) {
      info("START " + step.desc);
      if (step.setup) {
        step.setup();
      }
      step.before();
      step.execute();
      step.after();
      ok(markup.undo.canUndo(), "Should be able to undo.");
      markup.undo.undo();
      step.before();
      ok(markup.undo.canRedo(), "Should be able to redo.");
      markup.undo.redo();
      step.after();
      info("END " + step.desc);
    }
    addAttributes();
  }

  function addAttributes() {
    let test = {
      desc: "Add attributes by adding to an existing attribute's entry",
      setup: function() {
        inspector.selection.setNode(doc.querySelector("#node18"));
      },
      before: function() {
        assertAttributes(doc.querySelector("#node18"), {
          id: "node18",
        });

        is(inspector.highlighter.nodeInfo.classesBox.textContent, "",
          "No classes in the infobar before edit.");
      },
      execute: function() {
        let editor = markup.getContainer(doc.querySelector("#node18")).editor;
        let attr = editor.attrs["id"].querySelector(".editable");
        editField(attr, attr.textContent + ' class="newclass" style="color:green"');
      },
      after: function() {
        assertAttributes(doc.querySelector("#node18"), {
          id: "node18",
          class: "newclass",
          style: "color:green"
        });
        is(inspector.highlighter.nodeInfo.classesBox.textContent, ".newclass",
          "Correct classes in the infobar after edit.");
      }
    };
    testAsyncSetup(test, editTagName);
  }

  function editTagName() {
    let test =  {
      desc: "Edit the tag name",
      setup: function() {
        inspector.selection.setNode(doc.querySelector("#retag-me"));
      },
      before: function() {
        let node = doc.querySelector("#retag-me");
        let container = markup.getContainer(node);

        is(node.tagName, "DIV", "retag-me should be a div.");
        ok(container.selected, "retag-me should be selected.");
        ok(container.expanded, "retag-me should be expanded.");
        is(doc.querySelector("#retag-me-2").parentNode, node,
          "retag-me-2 should be a child of the old element.");
      },
      execute: function() {
        let node = doc.querySelector("#retag-me");
        let editor = markup.getContainer(node).editor;
        let field = editor.tag;
        editField(field, "p");
      },
      after: function() {
        let node = doc.querySelector("#retag-me");
        let container = markup.getContainer(node);
        is(node.tagName, "P", "retag-me should be a p.");
        ok(container.selected, "retag-me should be selected.");
        ok(container.expanded, "retag-me should be expanded.");
        is(doc.querySelector("#retag-me-2").parentNode, node,
          "retag-me-2 should be a child of the new element.");
      }
    };
    testAsyncSetup(test, removeElementWithDelete);
  }

  function removeElementWithDelete() {
    let test =  {
      desc: "Remove an element with the delete key",
      before: function() {
        ok(!!doc.querySelector("#node18"), "Node 18 should exist.");
      },
      execute: function() {
        inspector.selection.setNode(doc.querySelector("#node18"));
      },
      executeCont: function() {
        EventUtils.sendKey("delete");
      },
      after: function() {
        ok(!doc.querySelector("#node18"), "Node 18 should not exist.")
      }
    };
    testAsyncExecute(test, finishUp);
  }

  function testAsyncExecute(test, callback) {
    info("START " + test.desc);

    test.before();
    inspector.selection.once("new-node", function BIMET_testAsyncExecNewNode() {
      test.executeCont();
      test.after();
      undoRedo(test, callback);
    });
    executeSoon(function BIMET_setNode1() {
      test.execute();
    });
  }

  function testAsyncSetup(test, callback) {
    info("START " + test.desc);

    inspector.selection.once("new-node", function BIMET_testAsyncSetupNewNode() {
      test.before();
      test.execute();
      test.after();
      undoRedo(test, callback);
    });
    executeSoon(function BIMET_setNode2() {
      test.setup();
    });
  }

  function undoRedo(test, callback) {
    ok(markup.undo.canUndo(), "Should be able to undo.");
    markup.undo.undo();
    executeSoon(function() {
      test.before();
      ok(markup.undo.canRedo(), "Should be able to redo.");
      markup.undo.redo();
      executeSoon(function() {
        test.after();
        info("END " + test.desc);
        callback();
      });
    });
  }

  function finishUp() {
    while (markup.undo.canUndo()) {
      markup.undo.undo();
    }
    doc = inspector = null;
    gBrowser.removeCurrentTab();
    finish();
  }
}
