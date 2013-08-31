/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

add_test(function test_clearDB() {
  NetworkStatsService._db.clear(function onDBCleared(error, result) {
    do_check_eq(result, null);
    run_next_test();
  });
});


add_test(function test_networkStatsAvailable_ok() {
  NetworkStatsService.networkStatsAvailable(function (success, msg) {
    do_check_eq(success, true);
    run_next_test();
  }, true, Ci.nsINetworkInterface.NETWORK_TYPE_WIFI, 1234, 4321, new Date());
});

add_test(function test_networkStatsAvailable_failure() {
  NetworkStatsService.networkStatsAvailable(function (success, msg) {
    do_check_eq(success, false);
    run_next_test();
  }, false, Ci.nsINetworkInterface.NETWORK_TYPE_WIFI, 1234, 4321, new Date());
});

add_test(function test_update_invalidConnection() {
  NetworkStatsService.update(-1, function (success, msg) {
    do_check_eq(success, false);
    do_check_eq(msg, "Invalid network type -1");
    run_next_test();
  });
});

add_test(function test_update() {
  NetworkStatsService.update(Ci.nsINetworkInterface.NETWORK_TYPE_WIFI, function (success, msg) {
    do_check_eq(success, true);
    run_next_test();
  });
});

add_test(function test_updateQueueIndex() {
  NetworkStatsService.updateQueue = [{type: 0, callbacks: null},
                                     {type: 1, callbacks: null},
                                     {type: 2, callbacks: null},
                                     {type: 3, callbacks: null},
                                     {type: 4, callbacks: null}];
  var index = NetworkStatsService.updateQueueIndex(3);
  do_check_eq(index, 3);
  index = NetworkStatsService.updateQueueIndex(10);
  do_check_eq(index, -1);

  NetworkStatsService.updateQueue = [];
  run_next_test();
});

add_test(function test_updateAllStats() {
  NetworkStatsService.updateAllStats(function(success, msg) {
    do_check_eq(success, true);
    run_next_test();
  });
});

add_test(function test_updateStats_ok() {
  NetworkStatsService.updateStats(Ci.nsINetworkInterface.NETWORK_TYPE_WIFI, function(success, msg){
    do_check_eq(success, true);
    run_next_test();
  });
});

add_test(function test_updateStats_failure() {
  NetworkStatsService.updateStats(-1, function(success, msg){
    do_check_eq(success, false);
    run_next_test();
  });
});

add_test(function test_queue() {
  NetworkStatsService.updateStats(Ci.nsINetworkInterface.NETWORK_TYPE_WIFI);
  NetworkStatsService.updateStats(Ci.nsINetworkInterface.NETWORK_TYPE_MOBILE);
  do_check_eq(NetworkStatsService.updateQueue.length, 2);
  do_check_eq(NetworkStatsService.updateQueue[0].callbacks.length, 1);

  NetworkStatsService.updateStats(Ci.nsINetworkInterface.NETWORK_TYPE_WIFI, function(success, msg){
    do_check_eq(NetworkStatsService.updateQueue.length, 1);
  });

  NetworkStatsService.updateStats(Ci.nsINetworkInterface.NETWORK_TYPE_MOBILE, function(success, msg){
    do_check_eq(NetworkStatsService.updateQueue.length, 0);
    run_next_test();
  });

  do_check_eq(NetworkStatsService.updateQueue.length, 2);
  do_check_eq(NetworkStatsService.updateQueue[0].callbacks.length, 2);
  do_check_eq(NetworkStatsService.updateQueue[0].callbacks[0], null);
  do_check_neq(NetworkStatsService.updateQueue[0].callbacks[1], null);
});

function run_test() {
  do_get_profile();

  Cu.import("resource://gre/modules/NetworkStatsService.jsm");
  run_next_test();
}
