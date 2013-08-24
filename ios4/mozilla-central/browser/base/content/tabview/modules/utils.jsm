/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is utils.js.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * Aza Raskin <aza@mozilla.com>
 * Ian Gilman <ian@iangilman.com>
 * Michael Yoshitaka Erlewine <mitcho@mitcho.com>
 *
 * This file incorporates work from:
 * jQuery JavaScript Library v1.4.2: http://code.jquery.com/jquery-1.4.2.js
 * This incorporated work is covered by the following copyright and
 * permission notice:
 * Copyright 2010, John Resig
 * Dual licensed under the MIT or GPL Version 2 licenses.
 * http://jquery.org/license
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

// **********
// Title: utils.js

let EXPORTED_SYMBOLS = ["Point", "Rect", "Range", "Subscribable", "Utils"];

// #########
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/Services.jsm");

// ##########
// Class: Point
// A simple point.
//
// Constructor: Point
// If a is a Point, creates a copy of it. Otherwise, expects a to be x,
// and creates a Point with it along with y. If either a or y are omitted,
// 0 is used in their place.
function Point(a, y) {
  if (Utils.isPoint(a)) {
    this.x = a.x;
    this.y = a.y;
  } else {
    this.x = (Utils.isNumber(a) ? a : 0);
    this.y = (Utils.isNumber(y) ? y : 0);
  }
};

Point.prototype = {
  // ----------
  // Function: distance
  // Returns the distance from this point to the given <Point>.
  distance: function(point) {
    var ax = this.x - point.x;
    var ay = this.y - point.y;
    return Math.sqrt((ax * ax) + (ay * ay));
  }
};

// ##########
// Class: Rect
// A simple rectangle. Note that in addition to the left and width, it also has
// a right property; changing one affects the others appropriately. Same for the
// vertical properties.
//
// Constructor: Rect
// If a is a Rect, creates a copy of it. Otherwise, expects a to be left,
// and creates a Rect with it along with top, width, and height.
function Rect(a, top, width, height) {
  // Note: perhaps 'a' should really be called 'rectOrLeft'
  if (Utils.isRect(a)) {
    this.left = a.left;
    this.top = a.top;
    this.width = a.width;
    this.height = a.height;
  } else {
    this.left = a;
    this.top = top;
    this.width = width;
    this.height = height;
  }
};

Rect.prototype = {

  get right() this.left + this.width,
  set right(value) {
    this.width = value - this.left;
  },

  get bottom() this.top + this.height,
  set bottom(value) {
    this.height = value - this.top;
  },

  // ----------
  // Variable: xRange
  // Gives you a new <Range> for the horizontal dimension.
  get xRange() new Range(this.left, this.right),

  // ----------
  // Variable: yRange
  // Gives you a new <Range> for the vertical dimension.
  get yRange() new Range(this.top, this.bottom),

  // ----------
  // Function: intersects
  // Returns true if this rectangle intersects the given <Rect>.
  intersects: function(rect) {
    return (rect.right > this.left &&
            rect.left < this.right &&
            rect.bottom > this.top &&
            rect.top < this.bottom);
  },

  // ----------
  // Function: intersection
  // Returns a new <Rect> with the intersection of this rectangle and the give <Rect>,
  // or null if they don't intersect.
  intersection: function(rect) {
    var box = new Rect(Math.max(rect.left, this.left), Math.max(rect.top, this.top), 0, 0);
    box.right = Math.min(rect.right, this.right);
    box.bottom = Math.min(rect.bottom, this.bottom);
    if (box.width > 0 && box.height > 0)
      return box;

    return null;
  },

  // ----------
  // Function: contains
  // Returns a boolean denoting if the <Rect> is contained inside
  // of the bounding rect.
  //
  // Paramaters
  //  - A <Rect>
  contains: function(rect) {
    return (rect.left > this.left &&
            rect.right < this.right &&
            rect.top > this.top &&
            rect.bottom < this.bottom);
  },

  // ----------
  // Function: center
  // Returns a new <Point> with the center location of this rectangle.
  center: function() {
    return new Point(this.left + (this.width / 2), this.top + (this.height / 2));
  },

  // ----------
  // Function: size
  // Returns a new <Point> with the dimensions of this rectangle.
  size: function() {
    return new Point(this.width, this.height);
  },

  // ----------
  // Function: position
  // Returns a new <Point> with the top left of this rectangle.
  position: function() {
    return new Point(this.left, this.top);
  },

  // ----------
  // Function: area
  // Returns the area of this rectangle.
  area: function() {
    return this.width * this.height;
  },

  // ----------
  // Function: inset
  // Makes the rect smaller (if the arguments are positive) as if a margin is added all around
  // the initial rect, with the margin widths (symmetric) being specified by the arguments.
  //
  // Paramaters
  //  - A <Point> or two arguments: x and y
  inset: function(a, b) {
    if (Utils.isPoint(a)) {
      b = a.y;
      a = a.x;
    }

    this.left += a;
    this.width -= a * 2;
    this.top += b;
    this.height -= b * 2;
  },

  // ----------
  // Function: offset
  // Moves (translates) the rect by the given vector.
  //
  // Paramaters
  //  - A <Point> or two arguments: x and y
  offset: function(a, b) {
    if (Utils.isPoint(a)) {
      this.left += a.x;
      this.top += a.y;
    } else {
      this.left += a;
      this.top += b;
    }
  },

  // ----------
  // Function: equals
  // Returns true if this rectangle is identical to the given <Rect>.
  equals: function(rect) {
    return (rect.left == this.left &&
            rect.top == this.top &&
            rect.width == this.width &&
            rect.height == this.height);
  },

  // ----------
  // Function: union
  // Returns a new <Rect> with the union of this rectangle and the given <Rect>.
  union: function(a) {
    var newLeft = Math.min(a.left, this.left);
    var newTop = Math.min(a.top, this.top);
    var newWidth = Math.max(a.right, this.right) - newLeft;
    var newHeight = Math.max(a.bottom, this.bottom) - newTop;
    var newRect = new Rect(newLeft, newTop, newWidth, newHeight);

    return newRect;
  },

  // ----------
  // Function: copy
  // Copies the values of the given <Rect> into this rectangle.
  copy: function(a) {
    this.left = a.left;
    this.top = a.top;
    this.width = a.width;
    this.height = a.height;
  },

  // ----------
  // Function: css
  // Returns an object with the dimensions of this rectangle, suitable for
  // passing into iQ's css method. You could of course just pass the rectangle
  // straight in, but this is cleaner, as it removes all the extraneous
  // properties. If you give a <Rect> to <iQClass.css> without this, it will
  // ignore the extraneous properties, but result in CSS warnings.
  css: function() {
    return {
      left: this.left,
      top: this.top,
      width: this.width,
      height: this.height
    };
  }
};

// ##########
// Class: Range
// A physical interval, with a min and max.
//
// Constructor: Range
// Creates a Range with the given min and max
function Range(min, max) {
  if (Utils.isRange(min) && !max) { // if the one variable given is a range, copy it.
    this.min = min.min;
    this.max = min.max;
  } else {
    this.min = min || 0;
    this.max = max || 0;
  }
};

Range.prototype = {
  // Variable: extent
  // Equivalent to max-min
  get extent() {
    return (this.max - this.min);
  },

  set extent(extent) {
    this.max = extent - this.min;
  },

  // ----------
  // Function: contains
  // Whether the <Range> contains the given <Range> or value or not.
  //
  // Paramaters
  //  - a number or <Range>
  contains: function(value) {
    if (Utils.isNumber(value))
      return value >= this.min && value <= this.max;
    if (Utils.isRange(value))
      return value.min >= this.min && value.max <= this.max;
    return false;
  },

  // ----------
  // Function: proportion
  // Maps the given value to the range [0,1], so that it returns 0 if the value is <= the min,
  // returns 1 if the value >= the max, and returns an interpolated "proportion" in (min, max).
  //
  // Paramaters
  //  - a number
  //  - (bool) smooth? If true, a smooth tanh-based function will be used instead of the linear.
  proportion: function(value, smooth) {
    if (value <= this.min)
      return 0;
    if (this.max <= value)
      return 1;

    var proportion = (value - this.min) / this.extent;

    if (smooth) {
      // The ease function ".5+.5*Math.tanh(4*x-2)" is a pretty
      // little graph. It goes from near 0 at x=0 to near 1 at x=1
      // smoothly and beautifully.
      // http://www.wolframalpha.com/input/?i=.5+%2B+.5+*+tanh%28%284+*+x%29+-+2%29
      function tanh(x) {
        var e = Math.exp(x);
        return (e - 1/e) / (e + 1/e);
      }
      return .5 - .5 * tanh(2 - 4 * proportion);
    }

    return proportion;
  },

  // ----------
  // Function: scale
  // Takes the given value in [0,1] and maps it to the associated value on the Range.
  //
  // Paramaters
  //  - a number in [0,1]
  scale: function(value) {
    if (value > 1)
      value = 1;
    if (value < 0)
      value = 0;
    return this.min + this.extent * value;
  }
};

// ##########
// Class: Subscribable
// A mix-in for allowing objects to collect subscribers for custom events.
function Subscribable() {
  this.subscribers = null;
};

Subscribable.prototype = {
  // ----------
  // Function: addSubscriber
  // The given callback will be called when the Subscribable fires the given event.
  // The refObject is used to facilitate removal if necessary.
  addSubscriber: function(refObject, eventName, callback) {
    try {
      Utils.assertThrow(refObject, "refObject");
      Utils.assertThrow(typeof callback == "function", "callback must be a function");
      Utils.assertThrow(eventName && typeof eventName == "string",
          "eventName must be a non-empty string");
    } catch(e) {
      Utils.log(e);
      return;
    }

    if (!this.subscribers)
      this.subscribers = {};

    if (!this.subscribers[eventName])
      this.subscribers[eventName] = [];

    var subs = this.subscribers[eventName];
    var existing = subs.filter(function(element) {
      return element.refObject == refObject;
    });

    if (existing.length) {
      Utils.assert(existing.length == 1, 'should only ever be one');
      existing[0].callback = callback;
    } else {
      subs.push({
        refObject: refObject,
        callback: callback
      });
    }
  },

  // ----------
  // Function: removeSubscriber
  // Removes the callback associated with refObject for the given event.
  removeSubscriber: function(refObject, eventName) {
    try {
      Utils.assertThrow(refObject, "refObject");
      Utils.assertThrow(eventName && typeof eventName == "string",
          "eventName must be a non-empty string");
    } catch(e) {
      Utils.log(e);
      return;
    }

    if (!this.subscribers || !this.subscribers[eventName])
      return;

    this.subscribers[eventName] = this.subscribers[eventName].filter(function(element) {
      return element.refObject != refObject;
    });
  },

  // ----------
  // Function: _sendToSubscribers
  // Internal routine. Used by the Subscribable to fire events.
  _sendToSubscribers: function(eventName, eventInfo) {
    try {
      Utils.assertThrow(eventName && typeof eventName == "string",
          "eventName must be a non-empty string");
    } catch(e) {
      Utils.log(e);
      return;
    }

    if (!this.subscribers || !this.subscribers[eventName])
      return;

    var subsCopy = this.subscribers[eventName].concat();
    subsCopy.forEach(function(object) {
      try {
        object.callback(this, eventInfo);
      } catch(e) {
        Utils.log(e);
      }
    }, this);
  }
};

// ##########
// Class: Utils
// Singelton with common utility functions.
let Utils = {
  // ___ Logging

  // ----------
  // Function: log
  // Prints the given arguments to the JavaScript error console as a message.
  // Pass as many arguments as you want, it'll print them all.
  log: function() {
    var text = this.expandArgumentsForLog(arguments);
    Services.console.logStringMessage(text);
  },

  // ----------
  // Function: error
  // Prints the given arguments to the JavaScript error console as an error.
  // Pass as many arguments as you want, it'll print them all.
  error: function() {
    var text = this.expandArgumentsForLog(arguments);
    Cu.reportError("tabview error: " + text);
  },

  // ----------
  // Function: trace
  // Prints the given arguments to the JavaScript error console as a message,
  // along with a full stack trace.
  // Pass as many arguments as you want, it'll print them all.
  trace: function() {
    var text = this.expandArgumentsForLog(arguments);
    // cut off the first two lines of the stack trace, because they're just this function.
    let stack = Error().stack.replace(/^.*?\n.*?\n/, "");
    // if the caller was assert, cut out the line for the assert function as well.
    if (this.trace.caller.name == 'Utils_assert')
      stack = stack.replace(/^.*?\n/, "");
    this.log('trace: ' + text + '\n' + stack);
  },

  // ----------
  // Function: assert
  // Prints a stack trace along with label (as a console message) if condition is false.
  assert: function Utils_assert(condition, label) {
    if (!condition) {
      let text;
      if (typeof label != 'string')
        text = 'badly formed assert';
      else
        text = "tabview assert: " + label;

      this.trace(text);
    }
  },

  // ----------
  // Function: assertThrow
  // Throws label as an exception if condition is false.
  assertThrow: function(condition, label) {
    if (!condition) {
      let text;
      if (typeof label != 'string')
        text = 'badly formed assert';
      else
        text = "tabview assert: " + label;

      // cut off the first two lines of the stack trace, because they're just this function.
      text += Error().stack.replace(/^.*?\n.*?\n/, "");

      throw text;
    }
  },

  // ----------
  // Function: expandObject
  // Prints the given object to a string, including all of its properties.
  expandObject: function(obj) {
    var s = obj + ' = {';
    for (let prop in obj) {
      let value;
      try {
        value = obj[prop];
      } catch(e) {
        value = '[!!error retrieving property]';
      }

      s += prop + ': ';
      if (typeof value == 'string')
        s += '\'' + value + '\'';
      else if (typeof value == 'function')
        s += 'function';
      else
        s += value;

      s += ', ';
    }
    return s + '}';
  },

  // ----------
  // Function: expandArgumentsForLog
  // Expands all of the given args (an array) into a single string.
  expandArgumentsForLog: function(args) {
    var that = this;
    return Array.map(args, function(arg) {
      return typeof arg == 'object' ? that.expandObject(arg) : arg;
    }).join('; ');
  },

  // ___ Misc

  // ----------
  // Function: isRightClick
  // Given a DOM mouse event, returns true if it was for the right mouse button.
  isRightClick: function(event) {
    return event.button == 2;
  },

  // ----------
  // Function: isDOMElement
  // Returns true if the given object is a DOM element.
  isDOMElement: function(object) {
    return object instanceof Ci.nsIDOMElement;
  },

  // ----------
  // Function: isNumber
  // Returns true if the argument is a valid number.
  isNumber: function(n) {
    return typeof n == 'number' && !isNaN(n);
  },

  // ----------
  // Function: isRect
  // Returns true if the given object (r) looks like a <Rect>.
  isRect: function(r) {
    return (r &&
            this.isNumber(r.left) &&
            this.isNumber(r.top) &&
            this.isNumber(r.width) &&
            this.isNumber(r.height));
  },

  // ----------
  // Function: isRange
  // Returns true if the given object (r) looks like a <Range>.
  isRange: function(r) {
    return (r &&
            this.isNumber(r.min) &&
            this.isNumber(r.max));
  },

  // ----------
  // Function: isPoint
  // Returns true if the given object (p) looks like a <Point>.
  isPoint: function(p) {
    return (p && this.isNumber(p.x) && this.isNumber(p.y));
  },

  // ----------
  // Function: isPlainObject
  // Check to see if an object is a plain object (created using "{}" or "new Object").
  isPlainObject: function(obj) {
    // Must be an Object.
    // Make sure that DOM nodes and window objects don't pass through, as well
    if (!obj || Object.prototype.toString.call(obj) !== "[object Object]" ||
       obj.nodeType || obj.setInterval) {
      return false;
    }

    // Not own constructor property must be Object
    const hasOwnProperty = Object.prototype.hasOwnProperty;

    if (obj.constructor &&
       !hasOwnProperty.call(obj, "constructor") &&
       !hasOwnProperty.call(obj.constructor.prototype, "isPrototypeOf")) {
      return false;
    }

    // Own properties are enumerated firstly, so to speed up,
    // if last one is own, then all properties are own.

    var key;
    for (key in obj) {}

    return key === undefined || hasOwnProperty.call(obj, key);
  },

  // ----------
  // Function: isEmptyObject
  // Returns true if the given object has no members.
  isEmptyObject: function(obj) {
    for (let name in obj)
      return false;
    return true;
  },

  // ----------
  // Function: copy
  // Returns a copy of the argument. Note that this is a shallow copy; if the argument
  // has properties that are themselves objects, those properties will be copied by reference.
  copy: function(value) {
    if (value && typeof value == 'object') {
      if (Array.isArray(value))
        return this.extend([], value);
      return this.extend({}, value);
    }
    return value;
  },

  // ----------
  // Function: merge
  // Merge two array-like objects into the first and return it.
  merge: function(first, second) {
    Array.forEach(second, function(el) Array.push(first, el));
    return first;
  },

  // ----------
  // Function: extend
  // Pass several objects in and it will combine them all into the first object and return it.
  extend: function() {

    // copy reference to target object
    let target = arguments[0] || {};
    // Deep copy is not supported
    if (typeof target === "boolean") {
      this.assert(false, "The first argument of extend cannot be a boolean." +
          "Deep copy is not supported.");
      return target;
    }

    // Back when this was in iQ + iQ.fn, so you could extend iQ objects with it.
    // This is no longer supported.
    let length = arguments.length;
    if (length === 1) {
      this.assert(false, "Extending the iQ prototype using extend is not supported.");
      return target;
    }

    // Handle case when target is a string or something
    if (typeof target != "object" && typeof target != "function") {
      target = {};
    }

    for (let i = 1; i < length; i++) {
      // Only deal with non-null/undefined values
      let options = arguments[i];
      if (options != null) {
        // Extend the base object
        for (let name in options) {
          let copy = options[name];

          // Prevent never-ending loop
          if (target === copy)
            continue;

          if (copy !== undefined)
            target[name] = copy;
        }
      }
    }

    // Return the modified object
    return target;
  }
};
