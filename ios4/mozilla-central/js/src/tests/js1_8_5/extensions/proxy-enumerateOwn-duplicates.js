// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/licenses/publicdomain/

//-----------------------------------------------------------------------------
var BUGNUMBER = 580200;
var summary =
  'Assertion failure enumerating own properties of proxy returning ' +
  'duplicated own property name';
print(BUGNUMBER + ": " + summary);

/**************
 * BEGIN TEST *
 **************/

var x = Proxy.create({ enumerateOwn: function() { return ["0","0"]; } }, [1,2]);
var ax = Object.getOwnPropertyNames(x);
assertEq(ax.length, 1, "array: " + ax);
assertEq(ax[0], "0");

var p = Proxy.create({ enumerateOwn: function() { return ["1","1"]; } }, null);
var ap = Object.getOwnPropertyNames(p);
assertEq(ap.length, 1, "array: " + ap);
assertEq(ap[0], "1");


/******************************************************************************/

if (typeof reportCompare === "function")
  reportCompare(true, true);

print("All tests passed!");
