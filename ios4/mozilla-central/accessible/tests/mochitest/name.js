function testName(aAccOrElmOrID, aName, aMsg)
{
  var msg = aMsg ? aMsg : "";

  var acc = getAccessible(aAccOrElmOrID);
  if (!acc)
    return;

  var txtID = prettyName(aAccOrElmOrID);
  try {
    is(acc.name, aName, msg + "Wrong name of the accessible for " + txtID);
  } catch (e) {
    ok(false, msg + "Can't get name of the accessible for " + txtID);
  }
  return acc;
}

////////////////////////////////////////////////////////////////////////////////
// Name tests described by "namerules.xml" file.

var gNameRulesFileURL =
  "chrome://mochikit/content/a11y/accessible/namerules.xml";

var gRuleDoc = null;

/**
 * Start name tests. Run through markup elements and test names for test
 * element (see namerules.xml for details).
 */
function testNames()
{
  var request = new XMLHttpRequest();
  request.open("get", gNameRulesFileURL, false);
  request.send();

  gRuleDoc = request.responseXML;

  var markupElms = evaluateXPath(gRuleDoc, "//rules/rulesample/markup");
  gTestIterator.iterateMarkups(markupElms);
}

////////////////////////////////////////////////////////////////////////////////
// Private section.

/**
 * Helper class to interate through name tests.
 */
var gTestIterator =
{
  iterateMarkups: function gTestIterator_iterateMarkups(aMarkupElms)
  {
    this.markupElms = aMarkupElms;

    this.iterateNext();
  },

  iterateRules: function gTestIterator_iterateRules(aElm, aContainer, aRuleElms)
  {
    this.ruleElms = aRuleElms;
    this.elm = aElm;
    this.container = aContainer;

    this.iterateNext();
  },

  iterateNext: function gTestIterator_iterateNext()
  {
    if (this.markupIdx == -1) {
      this.markupIdx++;
      testNamesForMarkup(this.markupElms[this.markupIdx]);
      return;
    }

    this.ruleIdx++;
    if (this.ruleIdx == this.ruleElms.length) {
      this.markupIdx++;
      if (this.markupIdx == this.markupElms.length) {
        SimpleTest.finish();
        return;
      }

      document.body.removeChild(this.container);

      this.ruleIdx = -1;
      testNamesForMarkup(this.markupElms[this.markupIdx]);
      return;
    }

    testNameForRule(this.elm, this.ruleElms[this.ruleIdx]);
  },

  markupElms: null,
  markupIdx: -1,
  ruleElms: null,
  ruleIdx: -1,
  elm: null,
  container: null
};

/**
 * Process every 'markup' element and test names for it. Used by testNames
 * function.
 */
function testNamesForMarkup(aMarkupElm)
{
  var div = document.createElement("div");
  div.setAttribute("id", "test");

  var child = aMarkupElm.firstChild;
  while (child) {
    var newChild = document.importNode(child, true);
    div.appendChild(newChild);
    child = child.nextSibling;
  }

  waitForEvent(EVENT_REORDER, document, testNamesForMarkupRules,
                null, aMarkupElm, div);

  document.body.appendChild(div);
}

function testNamesForMarkupRules(aMarkupElm, aContainer)
{
  ensureAccessibleTree(aContainer);

  var serializer = new XMLSerializer();

  var expr = "//html/body/div[@id='test']/" + aMarkupElm.getAttribute("ref");
  var elms = evaluateXPath(document, expr, htmlDocResolver);

  var ruleId = aMarkupElm.getAttribute("ruleset");
  var ruleElms = getRuleElmsByRulesetId(ruleId);

  gTestIterator.iterateRules(elms[0], aContainer, ruleElms);
}

/**
 * Test name for current rule and current 'markup' element. Used by
 * testNamesForMarkup function.
 */
function testNameForRule(aElm, aRuleElm)
{
  if (aRuleElm.hasAttribute("attr"))
    testNameForAttrRule(aElm, aRuleElm);
  else if (aRuleElm.hasAttribute("elm") && aRuleElm.hasAttribute("elmattr"))
    testNameForElmRule(aElm, aRuleElm);
  else if (aRuleElm.getAttribute("fromsubtree") == "true")
    testNameForSubtreeRule(aElm, aRuleElm);
}

function testNameForAttrRule(aElm, aRule)
{
  var name = "";

  var attr = aRule.getAttribute("attr");
  var attrValue = aElm.getAttribute(attr);

  var type = aRule.getAttribute("type");
  if (type == "string") {
    name = attrValue;

  } else if (type == "ref") {
    var ids = attrValue.split(/\s+/);
    for (var idx = 0; idx < ids.length; idx++) {
      var labelElm = getNode(ids[idx]);
      if (name != "")
        name += " ";

      name += labelElm.getAttribute("a11yname");
    }
  }

  var msg = "Attribute '" + attr + "' test. ";
  testName(aElm, name, msg);
  aElm.removeAttribute(attr);

  gTestIterator.iterateNext();
}

function testNameForElmRule(aElm, aRule)
{  
  var elm = aRule.getAttribute("elm");
  var elmattr = aRule.getAttribute("elmattr");

  var filter = {
    acceptNode: function filter_acceptNode(aNode)
    {
      if (aNode.localName == this.mLocalName &&
          aNode.getAttribute(this.mAttrName) == this.mAttrValue)
        return NodeFilter.FILTER_ACCEPT;

      return NodeFilter.FILTER_SKIP;
    },

    mLocalName: elm,
    mAttrName: elmattr,
    mAttrValue: aElm.getAttribute("id")
  };

  var treeWalker = document.createTreeWalker(document.body,
                                             NodeFilter.SHOW_ELEMENT,
                                             filter, false);
  var labelElm = treeWalker.nextNode();
  var msg = "Element '" + elm + "' test.";
  testName(aElm, labelElm.getAttribute("a11yname"), msg);

  var parentNode = labelElm.parentNode;
  waitForEvent(EVENT_REORDER, parentNode,
               gTestIterator.iterateNext, gTestIterator);

  parentNode.removeChild(labelElm);
}

function testNameForSubtreeRule(aElm, aRule)
{
  var msg = "From subtree test.";
  testName(aElm, aElm.getAttribute("a11yname"), msg);

  waitForEvent(EVENT_REORDER, aElm, gTestIterator.iterateNext, gTestIterator);

  while (aElm.firstChild)
    aElm.removeChild(aElm.firstChild);
}

/**
 * Return array of 'rule' elements. Used in conjunction with
 * getRuleElmsFromRulesetElm() function.
 */
function getRuleElmsByRulesetId(aRulesetId)
{
  var expr = "//rules/ruledfn/ruleset[@id='" + aRulesetId + "']";
  var rulesetElm = evaluateXPath(gRuleDoc, expr);
  return getRuleElmsFromRulesetElm(rulesetElm[0]);
}

function getRuleElmsFromRulesetElm(aRulesetElm)
{
  var rulesetId = aRulesetElm.getAttribute("ref");
  if (rulesetId)
    return getRuleElmsByRulesetId(rulesetId);

  var ruleElms = [];

  var child = aRulesetElm.firstChild;
  while (child) {
    if (child.localName == "ruleset")
      ruleElms = ruleElms.concat(getRuleElmsFromRulesetElm(child));
    if (child.localName == "rule")
      ruleElms.push(child);

    child = child.nextSibling;
  }

  return ruleElms;
}

/**
 * Helper method to evaluate xpath expression.
 */
function evaluateXPath(aNode, aExpr, aResolver)
{
  var xpe = new XPathEvaluator();

  var resolver = aResolver;
  if (!resolver) {
    var node = aNode.ownerDocument == null ?
      aNode.documentElement : aNode.ownerDocument.documentElement;
    resolver = xpe.createNSResolver(node);
  }

  var result = xpe.evaluate(aExpr, aNode, resolver, 0, null);
  var found = [];
  var res;
  while (res = result.iterateNext())
    found.push(res);

  return found;
}

function htmlDocResolver(aPrefix) {
  var ns = {
    'html' : 'http://www.w3.org/1999/xhtml'
  };
  return ns[aPrefix] || null;
}
