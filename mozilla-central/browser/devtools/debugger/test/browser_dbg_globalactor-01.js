/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Check extension-added global actor API.
 */

var gClient = null;

function test()
{
  DebuggerServer.addActors("chrome://mochitests/content/browser/browser/devtools/debugger/test/testactors.js");

  let transport = DebuggerServer.connectPipe();
  gClient = new DebuggerClient(transport);
  gClient.connect(function(aType, aTraits) {
    is(aType, "browser", "Root actor should identify itself as a browser.");
    gClient.listTabs(function(aResponse) {
      let globalActor = aResponse.testGlobalActor1;
      ok(globalActor, "Found the test tab actor.")
      ok(globalActor.indexOf("testone") >= 0,
         "testTabActor's actorPrefix should be used.");
      gClient.request({ to: globalActor, type: "ping" }, function(aResponse) {
        is(aResponse.pong, "pong", "Actor should respond to requests.");
        // Send another ping to see if the same actor is used.
        gClient.request({ to: globalActor, type: "ping" }, function(aResponse) {
          is(aResponse.pong, "pong", "Actor should respond to requests.");

          // Make sure that lazily-created actors are created only once.
          let connections = Object.keys(DebuggerServer._connections);
          is(connections.length, 1, "Only one connection is established.");
          let connPrefix = connections[0];
          ok(DebuggerServer._connections[connPrefix],
             connPrefix + " is the only connection.");
          // First we look for the pool of global actors.
          let extraPools = DebuggerServer._connections[connPrefix]._extraPools;
          let globalPool;
          for (let pool of extraPools) {
            if (Object.keys(pool._actors).some(function(elem) {
              // Tab actors are in the global pool.
              let re = new RegExp(connPrefix + "tab", "g");
              return elem.match(re) !== null;
            })) {
              globalPool = pool;
              break;
            }
          }
          // Then we look if the global pool contains only one test actor.
          let actorPrefix = connPrefix + "testone";
          let actors = Object.keys(globalPool._actors).join();
          info("Global actors: " + actors);
          isnot(actors.indexOf(actorPrefix), -1, "The test actor exists in the pool.");
          is(actors.indexOf(actorPrefix), actors.lastIndexOf(actorPrefix),
             "Only one actor exists in the pool.");

          finish_test();
        });
      });
    });
  });
}

function finish_test()
{
  gClient.close(function() {
    finish();
  });
}
