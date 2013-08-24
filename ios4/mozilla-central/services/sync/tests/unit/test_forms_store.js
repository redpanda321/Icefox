_("Make sure the form store follows the Store api and correctly accesses the backend form storage");
Cu.import("resource://services-sync/engines/forms.js");
Cu.import("resource://services-sync/type_records/forms.js");

function run_test() {
  let store = new FormEngine()._store;

  _("Remove any existing entries");
  store.wipe();
  for (let id in store.getAllIDs()) {
    do_throw("Shouldn't get any ids!");
  }

  _("Add a form entry");
  store.create({
    name: "name!!",
    value: "value??"
  });

  _("Should have 1 entry now");
  let id = "";
  for (let _id in store.getAllIDs()) {
    if (id == "")
      id = _id;
    else
      do_throw("Should have only gotten one!");
  }
  do_check_true(store.itemExists(id));

  let rec = store.createRecord(id);
  _("Got record for id", id, rec);
  do_check_eq(rec.name, "name!!");
  do_check_eq(rec.value, "value??");

  _("Create a non-existant id for delete");
  do_check_true(store.createRecord("deleted!!").deleted);

  _("Try updating.. doesn't do anything yet");
  store.update({});

  _("Remove all entries");
  store.wipe();
  for (let id in store.getAllIDs()) {
    do_throw("Shouldn't get any ids!");
  }

  _("Add another entry");
  store.create({
    name: "another",
    value: "entry"
  });
  id = "";
  for (let _id in store.getAllIDs()) {
    if (id == "")
      id = _id;
    else
      do_throw("Should have only gotten one!");
  }

  _("Change the id of the new entry to something else");
  store.changeItemID(id, "newid");

  _("Make sure it's there");
  do_check_true(store.itemExists("newid"));

  _("Remove the entry");
  store.remove({
    id: "newid"
  });
  for (let id in store.getAllIDs()) {
    do_throw("Shouldn't get any ids!");
  }

  _("Removing the entry again shouldn't matter");
  store.remove({
    id: "newid"
  });
  for (let id in store.getAllIDs()) {
    do_throw("Shouldn't get any ids!");
  }
}
