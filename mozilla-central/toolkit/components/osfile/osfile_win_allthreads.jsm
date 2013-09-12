/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module defines the thread-agnostic components of the Win version
 * of OS.File. It depends on the thread-agnostic cross-platform components
 * of OS.File.
 *
 * It serves the following purposes:
 * - open libc;
 * - define OS.Shared.Win.Error;
 * - define a few constants and types that need to be defined on all platforms.
 *
 * This module can be:
 * - opened from the main thread as a jsm module;
 * - opened from a chrome worker through importScripts.
 */

if (typeof Components != "undefined") {
  // Module is opened as a jsm module
  this.EXPORTED_SYMBOLS = ["OS"];
  Components.utils.import("resource://gre/modules/ctypes.jsm");
  Components.utils.import("resource://gre/modules/osfile/osfile_shared_allthreads.jsm", this);
}

(function(exports) {
  "use strict";
  if (!exports.OS || !exports.OS.Shared) {
    throw new Error("osfile_win_allthreads.jsm must be loaded after osfile_shared_allthreads.jsm");
  }
  if (exports.OS.Shared.Win) {
    // Avoid double inclusion
    return;
  }
  exports.OS.Shared.Win = {};

  let LOG = OS.Shared.LOG.bind(OS.Shared, "Win", "allthreads");

  // Open libc
  let libc = ctypes.open("kernel32.dll");
  if (!libc) {
    throw new Error("Could not open kernel32.dll");
  }
  exports.OS.Shared.Win.libc = libc;

  // Define declareFFI
  let declareFFI = OS.Shared.declareFFI.bind(null, libc);
  exports.OS.Shared.Win.declareFFI = declareFFI;

  // Define Error
  let FormatMessage = libc.declare("FormatMessageW", ctypes.winapi_abi,
    /*return*/ ctypes.uint32_t,
    /*flags*/  ctypes.uint32_t,
    /*source*/ ctypes.voidptr_t,
    /*msgid*/  ctypes.uint32_t,
    /*langid*/ ctypes.uint32_t,
    /*buf*/    ctypes.jschar.ptr,
    /*size*/   ctypes.uint32_t,
    /*Arguments*/ctypes.voidptr_t
  );

  /**
   * A File-related error.
   *
   * To obtain a human-readable error message, use method |toString|.
   * To determine the cause of the error, use the various |becauseX|
   * getters. To determine the operation that failed, use field
   * |operation|.
   *
   * Additionally, this implementation offers a field
   * |winLastError|, which holds the OS-specific error
   * constant. If you need this level of detail, you may match the value
   * of this field against the error constants of |OS.Constants.Win|.
   *
   * @param {string=} operation The operation that failed. If unspecified,
   * the name of the calling function is taken to be the operation that
   * failed.
   * @param {number=} lastError The OS-specific constant detailing the
   * reason of the error. If unspecified, this is fetched from the system
   * status.
   *
   * @constructor
   * @extends {OS.Shared.Error}
   */
  let OSError = function OSError(operation, lastError) {
    operation = operation || "unknown operation";
    exports.OS.Shared.Error.call(this, operation);
    this.winLastError = lastError || ctypes.winLastError;
  };
  OSError.prototype = new exports.OS.Shared.Error();
  OSError.prototype.toString = function toString() {
    let buf = new (ctypes.ArrayType(ctypes.jschar, 1024))();
    let result = FormatMessage(
      exports.OS.Constants.Win.FORMAT_MESSAGE_FROM_SYSTEM |
      exports.OS.Constants.Win.FORMAT_MESSAGE_IGNORE_INSERTS,
      null,
      /* The error number */ this.winLastError,
      /* Default language */ 0,
      /* Output buffer*/     buf,
      /* Minimum size of buffer */ 1024,
      /* Format args*/       null
    );
    if (!result) {
      buf = "additional error " +
        ctypes.winLastError +
        " while fetching system error message";
    }
    return "Win error " + this.winLastError + " during operation "
      + this.operation + " (" + buf.readString() + ")";
  };

  /**
   * |true| if the error was raised because a file or directory
   * already exists, |false| otherwise.
   */
  Object.defineProperty(OSError.prototype, "becauseExists", {
    get: function becauseExists() {
      return this.winLastError == exports.OS.Constants.Win.ERROR_FILE_EXISTS ||
        this.winLastError == exports.OS.Constants.Win.ERROR_ALREADY_EXISTS;
    }
  });
  /**
   * |true| if the error was raised because a file or directory
   * does not exist, |false| otherwise.
   */
  Object.defineProperty(OSError.prototype, "becauseNoSuchFile", {
    get: function becauseNoSuchFile() {
      return this.winLastError == exports.OS.Constants.Win.ERROR_FILE_NOT_FOUND;
    }
  });
  /**
   * |true| if the error was raised because a directory is not empty
   * does not exist, |false| otherwise.
   */
  Object.defineProperty(OSError.prototype, "becauseNotEmpty", {
    get: function becauseNotEmpty() {
      return this.winLastError == OS.Constants.Win.ERROR_DIR_NOT_EMPTY;
    }
  });
  /**
   * |true| if the error was raised because a file or directory
   * is closed, |false| otherwise.
   */
  Object.defineProperty(OSError.prototype, "becauseClosed", {
    get: function becauseClosed() {
      return this.winLastError == exports.OS.Constants.Win.INVALID_HANDLE_VALUE;
    }
  });

  /**
   * Serialize an instance of OSError to something that can be
   * transmitted across threads (not necessarily a string).
   */
  OSError.toMsg = function toMsg(error) {
    return {
      operation: error.operation,
     winLastError: error.winLastError
    };
  };

  /**
   * Deserialize a message back to an instance of OSError
   */
  OSError.fromMsg = function fromMsg(msg) {
    return new OSError(msg.operation, msg.winLastError);
  };

  exports.OS.Shared.Win.Error = OSError;

  // Special constants that need to be defined on all platforms

  Object.defineProperty(exports.OS.Shared, "POS_START", { value: exports.OS.Constants.Win.FILE_BEGIN });
  Object.defineProperty(exports.OS.Shared, "POS_CURRENT", { value: exports.OS.Constants.Win.FILE_CURRENT });
  Object.defineProperty(exports.OS.Shared, "POS_END", { value: exports.OS.Constants.Win.FILE_END });

  // Special types that need to be defined for communication
  // between threads
  let Types = exports.OS.Shared.Type;

  /**
   * Native paths
   *
   * Under Windows, expressed as wide strings
   */
  Types.path = Types.wstring.withName("[in] path");
  Types.out_path = Types.out_wstring.withName("[out] path");

  // Special constructors that need to be defined on all threads
  OSError.closed = function closed(operation) {
    return new OSError(operation, exports.OS.Constants.Win.INVALID_HANDLE_VALUE);
  };

  OSError.exists = function exists(operation) {
    return new OSError(operation, exports.OS.Constants.Win.ERROR_FILE_EXISTS);
  };
})(this);
