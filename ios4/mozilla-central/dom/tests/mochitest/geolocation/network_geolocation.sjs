function parseQueryString(str)
{
  if (str == "")
    return {};

  var paramArray = str.split("&");
  var regex = /^([^=]+)=(.*)$/;
  var params = {};
  for (var i = 0, sz = paramArray.length; i < sz; i++)
  {
    var match = regex.exec(paramArray[i]);
    if (!match)
      throw "Bad parameter in queryString!  '" + paramArray[i] + "'";
    params[decodeURIComponent(match[1])] = decodeURIComponent(match[2]);
  }

  return params;
}

function getPosition()
{  
    // this isnt' the w3c data structure, it is the network location provider structure.

  var address = {
      street_number: "street_number",
      street: "street",
      premises: "premises",
      city: "city",
      county: "county",
      region: "region",
      country: "country",
      country_code: "country_code",
      postal_code: "postal_code",
  };


  var coords = {
    latitude: 37.41857,
    longitude: -122.08769,

    altitude: 42,
    accuracy: 42,
    altitude_accuracy: 42,
  };
  
  var geoposition = {
    location: coords,
  };

  return JSON.stringify(geoposition);
}

function handleRequest(request, response)
{
  var params = parseQueryString(request.queryString);

  if (params.action == "stop-responding") {
      return;
  }

  var position = getPosition();

  if (params.action == "respond-garbage") {
     // better way?
    var chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXTZabcdefghiklmnopqrstuvwxyz";
    position = "";
    var len = Math.floor(Math.random() * 5000);

    for (var i=0; i< len; i++) {
        var c = Math.floor(Math.random() * chars.length);
        position += chars.substring(c, c+1);
    }
  }

  var response;
  response.setStatusLine("1.0", 200, "OK");
  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader("Content-Type", "aplication/x-javascript", false);
  response.write(position);
}

