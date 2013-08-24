Cu.import("resource://services-sync/base_records/keys.js");
Cu.import("resource://services-sync/constants.js");
Cu.import("resource://services-sync/identity.js");
Cu.import("resource://services-sync/service.js");
Cu.import("resource://services-sync/status.js");
Cu.import("resource://services-sync/util.js");

function test_urlsAndIdentities() {
  _("Various Weave.Service properties correspond to preference settings and update other object properties upon being set.");

  try {
    _("Verify initial state");
    do_check_eq(Svc.Prefs.get("username"), undefined);
    do_check_eq(PubKeys.defaultKeyUri, undefined);
    do_check_eq(PrivKeys.defaultKeyUri, undefined);
    do_check_eq(ID.get("WeaveID").username, "");
    do_check_eq(ID.get("WeaveCryptoID").username, "");

    do_check_true(!!Weave.Service.serverURL); // actual value may change
    do_check_eq(Weave.Service.clusterURL, "");
    do_check_eq(Weave.Service.infoURL, undefined);
    do_check_eq(Weave.Service.storageURL, undefined);
    do_check_eq(Weave.Service.metaURL, undefined);

    _("The 'username' attribute is normalized to lower case, updates preferences and identities.");
    Weave.Service.username = "TarZan";
    do_check_eq(Weave.Service.username, "tarzan");
    do_check_eq(Svc.Prefs.get("username"), "tarzan");
    do_check_eq(ID.get("WeaveID").username, "tarzan");
    do_check_eq(ID.get("WeaveCryptoID").username, "tarzan");

    // Since we don't have a cluster URL yet, these will still not be defined.
    do_check_eq(Weave.Service.infoURL, undefined);
    do_check_eq(Weave.Service.storageURL, undefined);
    do_check_eq(Weave.Service.metaURL, undefined);
    do_check_eq(PubKeys.defaultKeyUri, undefined);
    do_check_eq(PrivKeys.defaultKeyUri, undefined);

    _("Tabs are stripped from the 'username' attribute as they can't be part of a URI.");
    Weave.Service.username = "jo\thn\tdoe";

    do_check_eq(Weave.Service.username, "johndoe");
    do_check_eq(Svc.Prefs.get("username"), "johndoe");
    do_check_eq(ID.get("WeaveID").username, "johndoe");
    do_check_eq(ID.get("WeaveCryptoID").username, "johndoe");

    _("The 'clusterURL' attribute updates preferences and cached URLs.");
    Weave.Service.serverURL = "http://weave.server/";
    Weave.Service.clusterURL = "http://weave.cluster/";
    do_check_eq(Svc.Prefs.get("clusterURL"), "http://weave.cluster/");

    do_check_eq(Weave.Service.infoURL,
                "http://weave.cluster/1.0/johndoe/info/collections");
    do_check_eq(Weave.Service.storageURL,
                "http://weave.cluster/1.0/johndoe/storage/");
    do_check_eq(Weave.Service.metaURL,
                "http://weave.cluster/1.0/johndoe/storage/meta/global");
    do_check_eq(PubKeys.defaultKeyUri,
                "http://weave.cluster/1.0/johndoe/storage/keys/pubkey");
    do_check_eq(PrivKeys.defaultKeyUri,
                "http://weave.cluster/1.0/johndoe/storage/keys/privkey");

    _("The 'miscURL' and 'userURL' attributes can be relative to 'serverURL' or absolute.");
    Svc.Prefs.set("miscURL", "relative/misc/");
    Svc.Prefs.set("userURL", "relative/user/");
    do_check_eq(Weave.Service.miscAPI,
                "http://weave.server/relative/misc/1.0/");
    do_check_eq(Weave.Service.userAPI,
                "http://weave.server/relative/user/1.0/");

    Svc.Prefs.set("miscURL", "http://weave.misc.services/");
    Svc.Prefs.set("userURL", "http://weave.user.services/");
    do_check_eq(Weave.Service.miscAPI, "http://weave.misc.services/1.0/");
    do_check_eq(Weave.Service.userAPI, "http://weave.user.services/1.0/");

    do_check_eq(Weave.Service.pwResetURL,
                "http://weave.server/weave-password-reset");

    _("Empty/false value for 'username' resets preference.");
    Weave.Service.username = "";
    do_check_eq(Svc.Prefs.get("username"), undefined);
    do_check_eq(ID.get("WeaveID").username, "");
    do_check_eq(ID.get("WeaveCryptoID").username, "");

    _("The 'serverURL' attributes updates/resets preferences.");
    // Identical value doesn't do anything
    Weave.Service.serverURL = Weave.Service.serverURL;
    do_check_eq(Svc.Prefs.get("clusterURL"), "http://weave.cluster/");

    Weave.Service.serverURL = "http://different.auth.node/";
    do_check_eq(Svc.Prefs.get("serverURL"), "http://different.auth.node/");
    do_check_eq(Svc.Prefs.get("clusterURL"), undefined);

  } finally {
    Svc.Prefs.resetBranch("");
  }
}


function test_syncID() {
  _("Weave.Service.syncID is auto-generated, corresponds to preference.");
  new FakeGUIDService();

  try {
    // Ensure pristine environment
    do_check_eq(Svc.Prefs.get("client.syncID"), undefined);

    // Performing the first get on the attribute will generate a new GUID.
    do_check_eq(Weave.Service.syncID, "fake-guid-0");
    do_check_eq(Svc.Prefs.get("client.syncID"), "fake-guid-0");

    Svc.Prefs.set("client.syncID", Utils.makeGUID());
    do_check_eq(Svc.Prefs.get("client.syncID"), "fake-guid-1");
    do_check_eq(Weave.Service.syncID, "fake-guid-1");
  } finally {
    Svc.Prefs.resetBranch("");
    new FakeGUIDService();
  }
}


function test_prefAttributes() {
  _("Test various attributes corresponding to preferences.");

  const TIMESTAMP1 = 1275493471649;
  const TIMESTAMP2 = 1275493741122;
  const INTERVAL = 42 * 60 * 1000;   // 42 minutes
  const THRESHOLD = 3142;
  const SCORE = 2718;
  const NUMCLIENTS = 42;

  try {
    _("The 'nextSync' and 'nextHeartbeat' attributes store a millisecond timestamp to the nearest second.");
    do_check_eq(Weave.Service.nextSync, 0);
    do_check_eq(Weave.Service.nextHeartbeat, 0);
    Weave.Service.nextSync = TIMESTAMP1;
    Weave.Service.nextHeartbeat = TIMESTAMP2;
    do_check_eq(Weave.Service.nextSync, Math.floor(TIMESTAMP1/1000)*1000);
    do_check_eq(Weave.Service.nextHeartbeat, Math.floor(TIMESTAMP2/1000)*1000);

    _("'syncInterval' has a non-zero default value.");
    do_check_eq(Svc.Prefs.get('syncInterval'), undefined);
    do_check_true(Weave.Service.syncInterval > 0);

    _("'syncInterval' corresponds to a preference setting.");
    Weave.Service.syncInterval = INTERVAL;
    do_check_eq(Weave.Service.syncInterval, INTERVAL);
    do_check_eq(Svc.Prefs.get('syncInterval'), INTERVAL);

    _("'syncInterval' ignored preference setting after partial sync..");
    Status.partial = true;
    do_check_eq(Weave.Service.syncInterval, PARTIAL_DATA_SYNC);

    _("'syncThreshold' corresponds to preference, has non-zero default.");
    do_check_eq(Svc.Prefs.get('syncThreshold'), undefined);
    do_check_true(Weave.Service.syncThreshold > 0);
    Weave.Service.syncThreshold = THRESHOLD;
    do_check_eq(Weave.Service.syncThreshold, THRESHOLD);
    do_check_eq(Svc.Prefs.get('syncThreshold'), THRESHOLD);

    _("'globalScore' corresponds to preference, defaults to zero.");
    do_check_eq(Svc.Prefs.get('globalScore'), undefined);
    do_check_eq(Weave.Service.globalScore, 0);
    Weave.Service.globalScore = SCORE;
    do_check_eq(Weave.Service.globalScore, SCORE);
    do_check_eq(Svc.Prefs.get('globalScore'), SCORE);

    _("'numClients' corresponds to preference, defaults to zero.");
    do_check_eq(Svc.Prefs.get('numClients'), undefined);
    do_check_eq(Weave.Service.numClients, 0);
    Weave.Service.numClients = NUMCLIENTS;
    do_check_eq(Weave.Service.numClients, NUMCLIENTS);
    do_check_eq(Svc.Prefs.get('numClients'), NUMCLIENTS);

  } finally {
    Svc.Prefs.resetBranch("");
  }
}


function test_locked() {
  _("The 'locked' attribute can be toggled with lock() and unlock()");

  // Defaults to false
  do_check_eq(Weave.Service.locked, false);

  do_check_eq(Weave.Service.lock(), true);
  do_check_eq(Weave.Service.locked, true);

  // Locking again will return false
  do_check_eq(Weave.Service.lock(), false);

  Weave.Service.unlock();
  do_check_eq(Weave.Service.locked, false);
}

function run_test() {
  test_urlsAndIdentities();
  test_syncID();
  test_prefAttributes();
  test_locked();
}
