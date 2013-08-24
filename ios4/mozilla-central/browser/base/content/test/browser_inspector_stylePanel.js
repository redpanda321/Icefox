/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Inspector Style Panel Tests.
 *
 * The Initial Developer of the Original Code is
 * The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Rob Campbell <rcampbell@mozilla.com> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

let doc;
let spans;
let testGen;

function createDocument()
{
  doc.body.innerHTML = '<div id="first" style="{ margin: 10em; ' +
    'font-size: 14pt; font-family: helvetica, sans-serif; color: #AAA}">\n' +
    '<h1>Some header text</h1>\n' +
    '<p id="salutation" style="{font-size: 12pt}">hi.</p>\n' +
    '<p id="body" style="{font-size: 12pt}">I am a test-case. This text exists ' +
    'solely to provide some things to <span style="{color: yellow}">' +
    'highlight</span> and <span style="{font-weight: bold}">count</span> ' +
    'style list-items in the box at right. If you are reading this, ' +
    'you should go do something else instead. Maybe read a book. Or better ' +
    'yet, write some test-cases for another bit of code. ' +
    '<span style="{font-style: italic}">Maybe more inspector test-cases!</span></p>\n' +
    '<p id="closing">end transmission</p>\n' +
    '</div>';
  doc.title = "Inspector Style Test";
  setupStyleTests();
}

function setupStyleTests()
{
  spans = doc.querySelectorAll("span");
  ok(spans, "captain, we have the spans");
  document.addEventListener("popupshown", runStyleTests, false);
  InspectorUI.openInspectorUI();
}

function spanGenerator()
{
  for (var i = 0; i < spans.length; ++i) {
    InspectorUI.inspectNode(spans[i]);
    yield;
  }
}

function runStyleTests(evt)
{
  if (evt.target.id != "inspector-style-panel")
    return true;
  document.removeEventListener("popupshown", runStyleTests, false);
  document.addEventListener("popupshown", performTestComparisons, false);
  InspectorUI.stopInspecting();
  testGen = spanGenerator();
  testGen.next();
}

function performTestComparisons(evt)
{
  if (evt.target.id != "highlighter-panel")
    return true;

  ok(InspectorUI.treeView.selectedNode, "selection");
  ok(InspectorUI.isStylePanelOpen, "style panel is open?");
  ok(InspectorUI.highlighter.isHighlighting, "panel is highlighting");
  ok(InspectorUI.styleBox.itemCount > 0, "styleBox has items");

  try {
    testGen.next();
  } catch(StopIteration) {
    document.removeEventListener("popupshown", performTestComparisons, false);
    finishUp();
  }
}

function finishUp() {
  InspectorUI.closeInspectorUI();
  gBrowser.removeCurrentTab();
  finish();
}

function test()
{
  waitForExplicitFinish();
  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function() {
    gBrowser.selectedBrowser.removeEventListener("load", arguments.callee, true);
    doc = content.document;
    waitForFocus(createDocument, content);
  }, true);
  
  content.location = "data:text/html,basic tests for inspector";
}

