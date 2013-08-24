function getPostDataString(aIS) {
  if (!aIS)
    return null;

  var sis = Cc["@mozilla.org/scriptableinputstream;1"].
            createInstance(Ci.nsIScriptableInputStream);
  sis.init(aIS);
  var dataLines = sis.read(aIS.available()).split("\n");

  // only want the last line
  return dataLines[dataLines.length-1];
}

function keywordResult(aURL, aPostData) {
  this.url = aURL;
  this.postData = aPostData;
}

function keyWordData() {}
keyWordData.prototype = {
  init: function(aKeyWord, aURL, aPostData, aSearchWord) {
    this.keyword = aKeyWord;
    this.uri = makeURI(aURL);
    this.postData = aPostData;
    this.searchWord = aSearchWord;

    this.method = (this.postData ? "POST" : "GET");
  }
}

function bmKeywordData(aKeyWord, aURL, aPostData, aSearchWord) {
  this.init(aKeyWord, aURL, aPostData, aSearchWord);
}
bmKeywordData.prototype = new keyWordData();

function searchKeywordData(aKeyWord, aURL, aPostData, aSearchWord) {
  this.init(aKeyWord, aURL, aPostData, aSearchWord);
}
searchKeywordData.prototype = new keyWordData();

var testData = [
  [new bmKeywordData("bmget", "http://bmget/search=%s", null, "foo"),
   new keywordResult("http://bmget/search=foo", null)],

  [new bmKeywordData("bmpost", "http://bmpost/", "search=%s", "foo2"),
   new keywordResult("http://bmpost/", "search=foo2")],

  [new bmKeywordData("bmpostget", "http://bmpostget/search1=%s", "search2=%s", "foo3"),
   new keywordResult("http://bmpostget/search1=foo3", "search2=foo3")],

  [new bmKeywordData("bmget-nosearch", "http://bmget-nosearch/", null, ""),
   new keywordResult("http://bmget-nosearch/", null)],

  [new searchKeywordData("searchget", "http://searchget/?search={searchTerms}", null, "foo4"),
   new keywordResult("http://searchget/?search=foo4", null)],

  [new searchKeywordData("searchpost", "http://searchpost/", "search={searchTerms}", "foo5"),
   new keywordResult("http://searchpost/", "search=foo5")],

  [new searchKeywordData("searchpostget", "http://searchpostget/?search1={searchTerms}", "search2={searchTerms}", "foo6"),
   new keywordResult("http://searchpostget/?search1=foo6", "search2=foo6")],

  // Bookmark keywords that don't take parameters should not be activated if a
  // parameter is passed (bug 420328).
  [new bmKeywordData("bmget-noparam", "http://bmget-noparam/", null, "foo7"),
   new keywordResult(null, null)],
  [new bmKeywordData("bmpost-noparam", "http://bmpost-noparam/", "not_a=param", "foo8"),
   new keywordResult(null, null)],

  // Test escaping (%s = escaped, %S = raw)
  // UTF-8 default
  [new bmKeywordData("bmget-escaping", "http://bmget/?esc=%s&raw=%S", null, "fo�"),
   new keywordResult("http://bmget/?esc=fo%C3%A9&raw=fo�", null)],
  // Explicitly-defined ISO-8859-1
  [new bmKeywordData("bmget-escaping2", "http://bmget/?esc=%s&raw=%S&mozcharset=ISO-8859-1", null, "fo�"),
   new keywordResult("http://bmget/?esc=fo%E9&raw=fo�", null)],
];

function test() {
  setupKeywords();

  for each (var item in testData) {
    var [data, result] = item;

    var postData = {};
    var query = data.keyword;
    if (data.searchWord)
      query += " " + data.searchWord;
    var url = getShortcutOrURI(query, postData);

    // null result.url means we should expect the same query we sent in
    var expected = result.url || query;
    is(url, expected, "got correct URL for " + data.keyword);
    is(getPostDataString(postData.value), result.postData, "got correct postData for " + data.keyword);
  }

  cleanupKeywords();
}

var gBMFolder = null;
var gAddedEngines = [];
function setupKeywords() {
  gBMFolder = Application.bookmarks.menu.addFolder("keyword-test");
  for each (var item in testData) {
    var data = item[0];
    if (data instanceof bmKeywordData) {
      var bm = gBMFolder.addBookmark(data.keyword, data.uri);
      bm.keyword = data.keyword;
      if (data.postData)
        bm.annotations.set("bookmarkProperties/POSTData", data.postData, Ci.nsIAnnotationService.EXPIRE_SESSION);
    }

    if (data instanceof searchKeywordData) {
      Services.search.addEngineWithDetails(data.keyword, "", data.keyword, "", data.method, data.uri.spec);
      var addedEngine = Services.search.getEngineByName(data.keyword);
      if (data.postData) {
        var [paramName, paramValue] = data.postData.split("=");
        addedEngine.addParam(paramName, paramValue, null);
      }

      gAddedEngines.push(addedEngine);
    }
  }
}

function cleanupKeywords() {
  gBMFolder.remove();
  gAddedEngines.map(Services.search.removeEngine);
}
