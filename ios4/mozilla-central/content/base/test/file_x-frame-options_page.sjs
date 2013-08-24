// SJS file for X-Frame-Options mochitests
function handleRequest(request, response)
{
  var query = {};
  request.queryString.split('&').forEach(function (val) {
    var [name, value] = val.split('=');
    query[name] = unescape(value);
  });

  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader("Content-Type", "text/html", false);

  // X-Frame-Options header value
  if (query['xfo'] == "deny") {
    for (var i = 0 ; i < 0x7fffff ; i++) { }
    response.setHeader("X-Frame-Options", "DENY", false);
  }
  else if (query['xfo'] == "sameorigin") {
    response.setHeader("X-Frame-Options", "SAMEORIGIN", false);
  }

  // from the test harness we'll be checking for the presence of this element
  // to test if the page loaded
  response.write("<h1 id=\"test\">" + query["testid"] + "</h1>");
}
