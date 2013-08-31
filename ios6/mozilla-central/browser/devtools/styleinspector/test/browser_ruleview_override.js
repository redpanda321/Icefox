/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

let tempScope = {}
Cu.import("resource:///modules/devtools/CssRuleView.jsm", tempScope);
let CssRuleView = tempScope.CssRuleView;
let _ElementStyle = tempScope._ElementStyle;
let _editableField = tempScope._editableField;

let doc;

function simpleOverride()
{
  let style = '' +
    '#testid {' +
    '  background-color: blue;' +
    '} ' +
    '.testclass {' +
    '  background-color: green;' +
    '}';

  let styleNode = addStyle(doc, style);
  doc.body.innerHTML = '<div id="testid" class="testclass">Styled Node</div>';

  let elementStyle = new _ElementStyle(doc.getElementById("testid"));

  let idRule = elementStyle.rules[1];
  let idProp = idRule.textProps[0];
  is(idProp.name, "background-color", "First ID prop should be background-color");
  ok(!idProp.overridden, "ID prop should not be overridden.");

  let classRule = elementStyle.rules[2];
  let classProp = classRule.textProps[0];
  is(classProp.name, "background-color", "First class prop should be background-color");
  ok(classProp.overridden, "Class property should be overridden.");

  // Override background-color by changing the element style.
  let elementRule = elementStyle.rules[0];
  elementRule.createProperty("background-color", "purple", "");
  let elementProp = elementRule.textProps[0];
  is(classProp.name, "background-color", "First element prop should now be background-color");

  ok(!elementProp.overridden, "Element style property should not be overridden");
  ok(idProp.overridden, "ID property should be overridden");
  ok(classProp.overridden, "Class property should be overridden");

  styleNode.parentNode.removeChild(styleNode);

  partialOverride();
}

function partialOverride()
{
  let style = '' +
    // Margin shorthand property...
    '.testclass {' +
    '  margin: 2px;' +
    '}' +
    // ... will be partially overridden.
    '#testid {' +
    '  margin-left: 1px;' +
    '}';

  let styleNode = addStyle(doc, style);
  doc.body.innerHTML = '<div id="testid" class="testclass">Styled Node</div>';

  let elementStyle = new _ElementStyle(doc.getElementById("testid"));

  let classRule = elementStyle.rules[2];
  let classProp = classRule.textProps[0];
  ok(!classProp.overridden, "Class prop shouldn't be overridden, some props are still being used.");
  for each (let computed in classProp.computed) {
    if (computed.name.indexOf("margin-left") == 0) {
      ok(computed.overridden, "margin-left props should be overridden.");
    } else {
      ok(!computed.overridden, "Non-margin-left props should not be overridden.");
    }
  }

  styleNode.parentNode.removeChild(styleNode);

  importantOverride();
}

function importantOverride()
{
  let style = '' +
    // Margin shorthand property...
    '.testclass {' +
    '  background-color: green !important;' +
    '}' +
    // ... will be partially overridden.
    '#testid {' +
    '  background-color: blue;' +
    '}';
  let styleNode = addStyle(doc, style);
  doc.body.innerHTML = '<div id="testid" class="testclass">Styled Node</div>';

  let elementStyle = new _ElementStyle(doc.getElementById("testid"));

  let idRule = elementStyle.rules[1];
  let idProp = idRule.textProps[0];
  ok(idProp.overridden, "Not-important rule should be overridden.");

  let classRule = elementStyle.rules[2];
  let classProp = classRule.textProps[0];
  ok(!classProp.overridden, "Important rule should not be overridden.");

  styleNode.parentNode.removeChild(styleNode);

  let elementRule = elementStyle.rules[0];
  let elementProp = elementRule.createProperty("background-color", "purple", "important");
  ok(classProp.overridden, "New important prop should override class property.");
  ok(!elementProp.overridden, "New important prop should not be overriden.");

  disableOverride();
}

function disableOverride()
{
  let style = '' +
    '#testid {' +
    '  background-color: blue;' +
    '}' +
    '.testclass {' +
    '  background-color: green;' +
    '}';
  let styleNode = addStyle(doc, style);
  doc.body.innerHTML = '<div id="testid" class="testclass">Styled Node</div>';

  let elementStyle = new _ElementStyle(doc.getElementById("testid"));

  let idRule = elementStyle.rules[1];
  let idProp = idRule.textProps[0];
  idProp.setEnabled(false);

  let classRule = elementStyle.rules[2];
  let classProp = classRule.textProps[0];
  ok(!classProp.overridden, "Class prop should not be overridden after id prop was disabled.");

  styleNode.parentNode.removeChild(styleNode);

  finishTest();
}

function finishTest()
{
  doc = null;
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
    waitForFocus(simpleOverride, content);
  }, true);

  content.location = "data:text/html,basic style inspector tests";
}
