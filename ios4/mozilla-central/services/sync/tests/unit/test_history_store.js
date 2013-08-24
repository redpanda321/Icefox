Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://services-sync/engines/history.js");
Cu.import("resource://services-sync/type_records/history.js");
Cu.import("resource://services-sync/ext/Sync.js");
Cu.import("resource://services-sync/util.js");

const TIMESTAMP1 = 1281077113313976;
const TIMESTAMP2 = 1281088209595212;
const TIMESTAMP3 = 1281199249129950;

function queryPlaces(uri, options) {
  let query = Svc.History.getNewQuery();
  query.uri = uri;
  let res = Svc.History.executeQuery(query, options);
  res.root.containerOpen = true;

  let results = [];
  for (let i = 0; i < res.root.childCount; i++)
    results.push(res.root.getChild(i));
  return results;
}

function queryHistoryVisits(uri) {
  let options = Svc.History.getNewQueryOptions();
  options.queryType = Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY;
  options.resultType = Ci.nsINavHistoryQueryOptions.RESULTS_AS_VISIT;
  options.sortingMode = Ci.nsINavHistoryQueryOptions.SORT_BY_DATE_ASCENDING;
  return queryPlaces(uri, options);
}

function waitForTitleChanged(test) {
  let [exec, cb] = Sync.withCb(function (callback) {
    Svc.History.addObserver({
      onBeginUpdateBatch: function onBeginUpdateBatch() {},
      onEndUpdateBatch: function onEndUpdateBatch() {},
      onPageChanged: function onPageChanged() {},
      onTitleChanged: function onTitleChanged() {
        Svc.History.removeObserver(this);
        callback();
      },
      onVisit: function onVisit() {},
      onDeleteVisits: function onDeleteVisits() {},
      onPageExpired: function onPageExpired() {},
      onBeforeDeleteURI: function onBeforeDeleteURI() {},
      onDeleteURI: function onDeleteURI() {},
      onClearHistory: function onClearHistory() {},
      QueryInterface: XPCOMUtils.generateQI([
        Ci.nsINavHistoryObserver,
        Ci.nsINavHistoryObserver_MOZILLA_1_9_1_ADDITIONS,
        Ci.nsISupportsWeakReference
      ])
    }, true);
    test();
  });
  exec(cb);
}

function run_test() {
  _("Verify that we've got an empty store to work with.");
  let store = new HistoryEngine()._store;
  do_check_eq([id for (id in store.getAllIDs())].length, 0);

  try {
    _("Let's create an entry in the database.");
    let fxuri = Utils.makeURI("http://getfirefox.com/");
    Svc.History.addPageWithDetails(fxuri, "Get Firefox!", TIMESTAMP1);

    _("Verify that the entry exists.");
    let ids = [id for (id in store.getAllIDs())];
    do_check_eq(ids.length, 1);
    let fxguid = ids[0];
    do_check_true(store.itemExists(fxguid));

    _("If we query a non-existent record, it's marked as deleted.");
    let record = store.createRecord("non-existent");
    do_check_true(record.deleted);

    _("Verify createRecord() returns a complete record.");
    record = store.createRecord(fxguid);
    do_check_eq(record.histUri, fxuri.spec);
    do_check_eq(record.title, "Get Firefox!");
    do_check_eq(record.visits.length, 1);
    do_check_eq(record.visits[0].date, TIMESTAMP1);
    do_check_eq(record.visits[0].type, Ci.nsINavHistoryService.TRANSITION_LINK);

    _("Let's modify the record and have the store update the database.");
    let secondvisit = {date: TIMESTAMP2,
                       type: Ci.nsINavHistoryService.TRANSITION_TYPED};
    waitForTitleChanged(function() {
      store.update({histUri: record.histUri,
                    title: "Hol Dir Firefox!",
                    visits: [record.visits[0], secondvisit]});
    });
    let queryres = queryHistoryVisits(fxuri);
    do_check_eq(queryres.length, 2);
    do_check_eq(queryres[0].time, TIMESTAMP1);
    do_check_eq(queryres[0].title, "Hol Dir Firefox!");
    do_check_eq(queryres[1].time, TIMESTAMP2);
    do_check_eq(queryres[1].title, "Hol Dir Firefox!");

    _("Create a brand new record through the store.");
    let tbguid = Utils.makeGUID();
    let tburi = Utils.makeURI("http://getthunderbird.com");
    waitForTitleChanged(function() {
      store.create({id: tbguid,
                    histUri: tburi.spec,
                    title: "The bird is the word!",
                    visits: [{date: TIMESTAMP3,
                              type: Ci.nsINavHistoryService.TRANSITION_TYPED}]});
    });
    do_check_eq([id for (id in store.getAllIDs())].length, 2);
    queryres = queryHistoryVisits(tburi);
    do_check_eq(queryres.length, 1);
    do_check_eq(queryres[0].time, TIMESTAMP3);
    do_check_eq(queryres[0].title, "The bird is the word!");

    _("Remove a record from the store.");
    store.remove({id: fxguid});
    do_check_false(store.itemExists(fxguid));
    queryres = queryHistoryVisits(fxuri);
    do_check_eq(queryres.length, 0);

    _("Make sure wipe works.");
    store.wipe();
    do_check_eq([id for (id in store.getAllIDs())].length, 0);
    queryres = queryHistoryVisits(fxuri);
    do_check_eq(queryres.length, 0);
    queryres = queryHistoryVisits(tburi);
    do_check_eq(queryres.length, 0);

  } finally {
    _("Clean up.");
    Svc.History.removeAllPages();
  }
}
