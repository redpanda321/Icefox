Cu.import("resource://services-sync/base_records/crypto.js");
Cu.import("resource://services-sync/base_records/keys.js");
Cu.import("resource://services-sync/engines/clients.js");
Cu.import("resource://services-sync/util.js");

function run_test() {
  let baseUri = "http://fakebase/";
  let pubUri = baseUri + "pubkey";
  let privUri = baseUri + "privkey";
  let cryptoUri = baseUri + "crypto";

  _("Setting up fake pub/priv keypair and symkey for encrypt/decrypt");
  PubKeys.defaultKeyUri = baseUri + "pubkey";
  let {pubkey, privkey} = PubKeys.createKeypair(passphrase, pubUri, privUri);
  PubKeys.set(pubUri, pubkey);
  PrivKeys.set(privUri, privkey);
  let cryptoMeta = new CryptoMeta(cryptoUri);
  cryptoMeta.addUnwrappedKey(pubkey, Svc.Crypto.generateRandomKey());
  CryptoMetas.set(cryptoUri, cryptoMeta);

  _("Test that serializing client records results in uploadable ascii");
  Clients.__defineGetter__("cryptoMetaURL", function() cryptoUri);
  Clients.localID = "ascii";
  Clients.localName = "wéävê";

  _("Make sure we have the expected record");
  let record = Clients._createRecord("ascii");
  do_check_eq(record.id, "ascii");
  do_check_eq(record.name, "wéävê");

  record.encrypt(passphrase)
  let serialized = JSON.stringify(record);
  let checkCount = 0;
  _("Checking for all ASCII:", serialized);
  Array.forEach(serialized, function(ch) {
    let code = ch.charCodeAt(0);
    _("Checking asciiness of '", ch, "'=", code);
    do_check_true(code < 128);
    checkCount++;
  });

  _("Processed", checkCount, "characters out of", serialized.length);
  do_check_eq(checkCount, serialized.length);

  _("Making sure the record still looks like it did before");
  record.decrypt(passphrase)
  do_check_eq(record.id, "ascii");
  do_check_eq(record.name, "wéävê");

  _("Sanity check that creating the record also gives the same");
  record = Clients._createRecord("ascii");
  do_check_eq(record.id, "ascii");
  do_check_eq(record.name, "wéävê");
}
