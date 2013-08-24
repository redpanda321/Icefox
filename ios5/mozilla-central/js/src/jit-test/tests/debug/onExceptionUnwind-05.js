// |jit-test| debug
// onExceptionUnwind returning undefined does not affect the thrown exception.

var g = newGlobal('new-compartment');
g.parent = this;
g.eval("new Debugger(parent).onExceptionUnwind = function () {};");

var obj = new Error("oops");
try {
    throw obj;
} catch (exc) {
    assertEq(exc, obj);
}
