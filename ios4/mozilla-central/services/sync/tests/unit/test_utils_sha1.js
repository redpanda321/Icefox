_("Make sure sha1 digests works with various messages");
Cu.import("resource://services-sync/util.js");

function run_test() {
  let mes1 = "hello";
  let mes2 = "world";

  _("Make sure right sha1 digests are generated");
  let dig1 = Utils.sha1(mes1);
  do_check_eq(dig1, "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d");
  let dig2 = Utils.sha1(mes2);
  do_check_eq(dig2, "7c211433f02071597741e6ff5a8ea34789abbf43");
  let dig12 = Utils.sha1(mes1 + mes2);
  do_check_eq(dig12, "6adfb183a4a2c94a2f92dab5ade762a47889a5a1");
  let dig21 = Utils.sha1(mes2 + mes1);
  do_check_eq(dig21, "5715790a892990382d98858c4aa38d0617151575");

  _("Repeated sha1s shouldn't change the digest");
  do_check_eq(Utils.sha1(mes1), dig1);
  do_check_eq(Utils.sha1(mes2), dig2);
  do_check_eq(Utils.sha1(mes1 + mes2), dig12);
  do_check_eq(Utils.sha1(mes2 + mes1), dig21);

  _("Nested sha1 should work just fine");
  let nest1 = Utils.sha1(Utils.sha1(Utils.sha1(Utils.sha1(Utils.sha1(mes1)))));
  do_check_eq(nest1, "23f340d0cff31e299158b3181b6bcc7e8c7f985a");
  let nest2 = Utils.sha1(Utils.sha1(Utils.sha1(Utils.sha1(Utils.sha1(mes2)))));
  do_check_eq(nest2, "1f6453867e3fb9876ae429918a64cdb8dc5ff2d0");
}
