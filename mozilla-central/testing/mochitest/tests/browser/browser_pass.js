function test() {
  ok(true, "pass ok");
  is(true, true, "pass is");
  isnot(false, true, "pass isnot");
  ise(true, true, "pass ise");
  todo(false, "pass todo");
  todo_is(false, true, "pass todo_is");
  todo_isnot(true, true, "pass todo_isnot");
  info("info message");

  var func = is;
  func(true, 1, "pass indirect is");
}
