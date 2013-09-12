/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

{
  if (typeof Components != "undefined") {
    this.EXPORTED_SYMBOLS = ["OS"];
  }
  (function(exports) {
     "use strict";
     /*
      * This block defines |OS.Shared.Type|. However, |OS| can exist already
      * (in particular, if this code is executed in a worker thread, it is
      * defined).
      */
     if (!exports.OS) {
       exports.OS = {};
     }
     if (!exports.OS.Shared) {
       exports.OS.Shared = {};
     }
     if (exports.OS.Shared.Type) {
       return; // Avoid double-initialization
     }

     // Import components after having initialized |exports.OS|, to ensure
     // that everybody uses the same definition of |OS|.
     if (typeof Components != "undefined") {
       Components.utils.import("resource://gre/modules/ctypes.jsm");
       Components.classes["@mozilla.org/net/osfileconstantsservice;1"].
         getService(Components.interfaces.nsIOSFileConstantsService).init();
     }

     // Define a lazy getter for a property
     let defineLazyGetter = function defineLazyGetter(object, name, getter) {
       Object.defineProperty(object, name, {
         configurable: true,
         get: function lazy() {
           delete this[name];
           let value = getter.call(this);
           Object.defineProperty(object, name, {
             value: value
           });
           return value;
         }
       });
     };
     exports.OS.Shared.defineLazyGetter = defineLazyGetter;

     /**
      * A variable controlling whether we should printout logs.
      */
     exports.OS.Shared.DEBUG = false;
     let LOG;
     if (typeof console != "undefined" && console.log) {
       LOG = console.log.bind(console, "OS");
     } else {
       LOG = function() {
         let text = "OS";
         for (let i = 0; i < arguments.length; ++i) {
           text += (" " + arguments[i]);
         }
         dump(text + "\n");
       };
     }
     exports.OS.Shared.LOG = LOG;

     /**
      * An OS error.
      *
      * This class is provided mostly for type-matching. If you need more
      * details about an error, you should use the platform-specific error
      * codes provided by subclasses of |OS.Shared.Error|.
      *
      * @param {string} operation The operation that failed.
      *
      * @constructor
      */
     function OSError(operation) {
       Error.call(this);
       this.operation = operation;
     }
     exports.OS.Shared.Error = OSError;

     /**
      * Abstraction above js-ctypes types.
      *
      * Use values of this type to register FFI functions. In addition to the
      * usual features of js-ctypes, values of this type perform the necessary
      * transformations to ensure that C errors are handled nicely, to connect
      * resources with their finalizer, etc.
      *
      * @param {string} name The name of the type. Must be unique.
      * @param {CType} implementation The js-ctypes implementation of the type.
      *
      * @constructor
      */
     function Type(name, implementation) {
       if (!(typeof name == "string")) {
         throw new TypeError("Type expects as first argument a name, got: "
                             + name);
       }
       if (!(implementation instanceof ctypes.CType)) {
         throw new TypeError("Type expects as second argument a ctypes.CType"+
                             ", got: " + implementation);
       }
       Object.defineProperty(this, "name", { value: name });
       Object.defineProperty(this, "implementation", { value: implementation });
     }
     Type.prototype = {
       /**
        * Serialize a value of |this| |Type| into a format that can
        * be transmitted as a message (not necessarily a string).
        *
        * In the default implementation, the method returns the
        * value unchanged.
        */
       toMsg: function default_toMsg(value) {
         return value;
       },
       /**
        * Deserialize a message to a value of |this| |Type|.
        *
        * In the default implementation, the method returns the
        * message unchanged.
        */
       fromMsg: function default_fromMsg(msg) {
         return msg;
       },
       /**
        * Import a value from C.
        *
        * In this default implementation, return the value
        * unchanged.
        */
       importFromC: function default_importFromC(value) {
         return value;
       },

       /**
        * A pointer/array used to pass data to the foreign function.
        */
       get in_ptr() {
         delete this.in_ptr;
         let ptr_t = new PtrType(
           "[in] " + this.name + "*",
           this.implementation.ptr,
           this);
         Object.defineProperty(this, "in_ptr",
           {
             get: function() {
               return ptr_t;
             }
           });
         return ptr_t;
       },

       /**
        * A pointer/array used to receive data from the foreign function.
        */
       get out_ptr() {
         delete this.out_ptr;
         let ptr_t = new PtrType(
           "[out] " + this.name + "*",
           this.implementation.ptr,
           this);
         Object.defineProperty(this, "out_ptr",
           {
             get: function() {
               return ptr_t;
             }
           });
         return ptr_t;
       },

       /**
        * A pointer/array used to both pass data to the foreign function
        * and receive data from the foreign function.
        *
        * Whenever possible, prefer using |in_ptr| or |out_ptr|, which
        * are generally faster.
        */
       get inout_ptr() {
         delete this.inout_ptr;
         let ptr_t = new PtrType(
           "[inout] " + this.name + "*",
           this.implementation.ptr,
           this);
         Object.defineProperty(this, "inout_ptr",
           {
             get: function() {
               return ptr_t;
             }
           });
         return ptr_t;
       },

       /**
        * Attach a finalizer to a type.
        */
       releaseWith: function releaseWith(finalizer) {
         let parent = this;
         let type = this.withName("[auto " + this.name + ", " + finalizer + "] ");
         type.importFromC = function importFromC(value, operation) {
           return ctypes.CDataFinalizer(
             parent.importFromC(value, operation),
             finalizer);
         };
         return type;
       },

       /**
        * Return an alias to a type with a different name.
        */
       withName: function withName(name) {
         return Object.create(this, {name: {value: name}});
       },

       /**
        * Cast a C value to |this| type.
        *
        * Throw an error if the value cannot be casted.
        */
       cast: function cast(value) {
         return ctypes.cast(value, this.implementation);
       },

       /**
        * Return the number of bytes in a value of |this| type.
        *
        * This may not be defined, e.g. for |void_t|, array types
        * without length, etc.
        */
       get size() {
         return this.implementation.size;
       }
     };

     /**
      * Utility function used to determine whether an object is a typed array
      */
     let isTypedArray = function isTypedArray(obj) {
       return typeof obj == "object"
         && "byteOffset" in obj;
     };
     exports.OS.Shared.isTypedArray = isTypedArray;

     /**
      * A |Type| of pointers.
      *
      * @param {string} name The name of this type.
      * @param {CType} implementation The type of this pointer.
      * @param {Type} targetType The target type.
      */
     function PtrType(name, implementation, targetType) {
       Type.call(this, name, implementation);
       if (targetType == null || !targetType instanceof Type) {
         throw new TypeError("targetType must be an instance of Type");
       }
       /**
        * The type of values targeted by this pointer type.
        */
       Object.defineProperty(this, "targetType", {
         value: targetType
       });
     }
     PtrType.prototype = Object.create(Type.prototype);

     /**
      * Convert a value to a pointer.
      *
      * Protocol:
      * - |null| returns |null|
      * - a string returns |{string: value}|
      * - a typed array returns |{ptr: address_of_buffer}|
      * - a C array returns |{ptr: address_of_buffer}|
      * everything else raises an error
      */
     PtrType.prototype.toMsg = function ptr_toMsg(value) {
       if (value == null) {
         return null;
       }
       if (typeof value == "string") {
         return { string: value };
       }
       let normalized;
       if (isTypedArray(value)) { // Typed array
         normalized = Types.uint8_t.in_ptr.implementation(value.buffer);
         if (value.byteOffset != 0) {
           normalized = exports.OS.Shared.offsetBy(normalized, value.byteOffset);
         }
       } else if ("addressOfElement" in value) { // C array
         normalized = value.addressOfElement(0);
       } else if ("isNull" in value) { // C pointer
         normalized = value;
       } else {
         throw new TypeError("Value " + value +
           " cannot be converted to a pointer");
       }
       let cast = Types.uintptr_t.cast(normalized);
       return {ptr: cast.value.toString()};
     };

     /**
      * Convert a message back to a pointer.
      */
     PtrType.prototype.fromMsg = function ptr_fromMsg(msg) {
       if (msg == null) {
         return null;
       }
       if ("string" in msg) {
         return msg.string;
       }
       if ("ptr" in msg) {
         let address = ctypes.uintptr_t(msg.ptr);
         return this.cast(address);
       }
       throw new TypeError("Message " + msg.toSource() +
         " does not represent a pointer");
     };

     exports.OS.Shared.Type = Type;
     let Types = Type;

     /*
      * Some values are large integers on 64 bit platforms. Unfortunately,
      * in practice, 64 bit integers cannot be manipulated in JS. We
      * therefore project them to regular numbers whenever possible.
      */

     let projectLargeInt = function projectLargeInt(x) {
       return parseInt(x.toString(), 10);
     };
     let projectLargeUInt = function projectLargeUInt(x) {
       return parseInt(x.toString(), 10);
     };
     let projectValue = function projectValue(x) {
       if (!(x instanceof ctypes.CData)) {
         return x;
       }
       if (!("value" in x)) { // Sanity check
         throw new TypeError("Number " + x.toSource() + " has no field |value|");
       }
       return x.value;
     };

     function projector(type, signed) {
       if (exports.OS.Shared.DEBUG) {
         LOG("Determining best projection for", type,
             "(size: ", type.size, ")", signed?"signed":"unsigned");
       }
       if (type instanceof Type) {
         type = type.implementation;
       }
       if (!type.size) {
         throw new TypeError("Argument is not a proper C type");
       }
       // Determine if type is projected to Int64/Uint64
       if (type.size == 8           // Usual case
           // The following cases have special treatment in js-ctypes
           // Regardless of their size, the value getter returns
           // a Int64/Uint64
           || type == ctypes.size_t // Special cases
           || type == ctypes.ssize_t
           || type == ctypes.intptr_t
           || type == ctypes.uintptr_t
           || type == ctypes.off_t){
          if (signed) {
	    if (exports.OS.Shared.DEBUG) {
             LOG("Projected as a large signed integer");
	    }
            return projectLargeInt;
          } else {
	    if (exports.OS.Shared.DEBUG) {
             LOG("Projected as a large unsigned integer");
	    }
            return projectLargeUInt;
          }
       }
       if (exports.OS.Shared.DEBUG) {
         LOG("Projected as a regular number");
       }
       return projectValue;
     };
     exports.OS.Shared.projectValue = projectValue;



     /**
      * Get the appropriate type for an unsigned int of the given size.
      *
      * This function is useful to define types such as |mode_t| whose
      * actual width depends on the OS/platform.
      *
      * @param {number} size The number of bytes requested.
      */
     Types.uintn_t = function uintn_t(size) {
       switch (size) {
       case 1: return Types.uint8_t;
       case 2: return Types.uint16_t;
       case 4: return Types.uint32_t;
       case 8: return Types.uint64_t;
       default:
         throw new Error("Cannot represent unsigned integers of " + size + " bytes");
       }
     };

     /**
      * Get the appropriate type for an signed int of the given size.
      *
      * This function is useful to define types such as |mode_t| whose
      * actual width depends on the OS/platform.
      *
      * @param {number} size The number of bytes requested.
      */
     Types.intn_t = function intn_t(size) {
       switch (size) {
       case 1: return Types.int8_t;
       case 2: return Types.int16_t;
       case 4: return Types.int32_t;
       case 8: return Types.int64_t;
       default:
         throw new Error("Cannot represent integers of " + size + " bytes");
       }
     };

     /**
      * Actual implementation of common C types.
      */

     /**
      * The void value.
      */
     Types.void_t =
       new Type("void",
                ctypes.void_t);

     /**
      * Shortcut for |void*|.
      */
     Types.voidptr_t =
       new PtrType("void*",
                   ctypes.voidptr_t,
                   Types.void_t);

     // void* is a special case as we can cast any pointer to/from it
     // so we have to shortcut |in_ptr|/|out_ptr|/|inout_ptr| and
     // ensure that js-ctypes' casting mechanism is invoked directly
     ["in_ptr", "out_ptr", "inout_ptr"].forEach(function(key) {
       Object.defineProperty(Types.void_t, key,
       {
         value: Types.voidptr_t
       });
     });

     /**
      * A Type of integers.
      *
      * @param {string} name The name of this type.
      * @param {CType} implementation The underlying js-ctypes implementation.
      * @param {bool} signed |true| if this is a type of signed integers,
      * |false| otherwise.
      *
      * @constructor
      */
     function IntType(name, implementation, signed) {
       Type.call(this, name, implementation);
       this.importFromC = projector(implementation, signed);
       this.project = this.importFromC;
     };
     IntType.prototype = Object.create(Type.prototype);
     IntType.prototype.toMsg = function toMsg(value) {
       if (typeof value == "number") {
         return value;
       }
       return this.project(value);
     };

     /**
      * A C char (one byte)
      */
     Types.char =
       new Type("char",
                ctypes.char);

     /**
      * A C wide char (two bytes)
      */
     Types.jschar =
       new Type("jschar",
                ctypes.jschar);

      /**
       * Base string types.
       */
     Types.cstring = Types.char.in_ptr.withName("[in] C string");
     Types.wstring = Types.jschar.in_ptr.withName("[in] wide string");
     Types.out_cstring = Types.char.out_ptr.withName("[out] C string");
     Types.out_wstring = Types.jschar.out_ptr.withName("[out] wide string");

     /**
      * A C integer (8-bits).
      */
     Types.int8_t =
       new IntType("int8_t", ctypes.int8_t, true);

     Types.uint8_t =
       new IntType("uint8_t", ctypes.uint8_t, false);

     /**
      * A C integer (16-bits).
      *
      * Also known as WORD under Windows.
      */
     Types.int16_t =
       new IntType("int16_t", ctypes.int16_t, true);

     Types.uint16_t =
       new IntType("uint16_t", ctypes.uint16_t, false);

     /**
      * A C integer (32-bits).
      *
      * Also known as DWORD under Windows.
      */
     Types.int32_t =
       new IntType("int32_t", ctypes.int32_t, true);

     Types.uint32_t =
       new IntType("uint32_t", ctypes.uint32_t, false);

     /**
      * A C integer (64-bits).
      */
     Types.int64_t =
       new IntType("int64_t", ctypes.int64_t, true);

     Types.uint64_t =
       new IntType("uint64_t", ctypes.uint64_t, false);

      /**
      * A C integer
      *
      * Size depends on the platform.
      */
     Types.int = Types.intn_t(ctypes.int.size).
       withName("int");

     Types.unsigned_int = Types.intn_t(ctypes.unsigned_int.size).
       withName("unsigned int");

     /**
      * A C long integer.
      *
      * Size depends on the platform.
      */
     Types.long =
       Types.intn_t(ctypes.long.size).withName("long");

     Types.unsigned_long =
       Types.intn_t(ctypes.unsigned_long.size).withName("unsigned long");

     /**
      * An unsigned integer with the same size as a pointer.
      *
      * Used to cast a pointer to an integer, whenever necessary.
      */
     Types.uintptr_t =
       Types.uintn_t(ctypes.uintptr_t.size).withName("uintptr_t");

     /**
      * A boolean.
      * Implemented as a C integer.
      */
     Types.bool = Types.int.withName("bool");
     Types.bool.importFromC = function projectBool(x) {
       return !!(x.value);
     };

     /**
      * A user identifier.
      *
      * Implemented as a C integer.
      */
     Types.uid_t =
       Types.int.withName("uid_t");

     /**
      * A group identifier.
      *
      * Implemented as a C integer.
      */
     Types.gid_t =
       Types.int.withName("gid_t");

     /**
      * An offset (positive or negative).
      *
      * Implemented as a C integer.
      */
     Types.off_t =
       new IntType("off_t", ctypes.off_t, true);

     /**
      * A size (positive).
      *
      * Implemented as a C size_t.
      */
     Types.size_t =
       new IntType("size_t", ctypes.size_t, false);

     /**
      * An offset (positive or negative).
      * Implemented as a C integer.
      */
     Types.ssize_t =
       new IntType("ssize_t", ctypes.ssize_t, true);

     /**
      * Encoding/decoding strings
      */
     Types.uencoder =
       new Type("uencoder", ctypes.StructType("uencoder"));
     Types.udecoder =
       new Type("udecoder", ctypes.StructType("udecoder"));

     /**
      * Utility class, used to build a |struct| type
      * from a set of field names, types and offsets.
      *
      * @param {string} name The name of the |struct| type.
      * @param {number} size The total size of the |struct| type in bytes.
      */
     function HollowStructure(name, size) {
       if (!name) {
         throw new TypeError("HollowStructure expects a name");
       }
       if (!size || size < 0) {
         throw new TypeError("HollowStructure expects a (positive) size");
       }

       // A mapping from offsets in the struct to name/type pairs
       // (or nothing if no field starts at that offset).
       this.offset_to_field_info = [];

       // The name of the struct
       this.name = name;

       // The size of the struct, in bytes
       this.size = size;

       // The number of paddings inserted so far.
       // Used to give distinct names to padding fields.
       this._paddings = 0;
     }
     HollowStructure.prototype = {
       /**
        * Add a field at a given offset.
        *
        * @param {number} offset The offset at which to insert the field.
        * @param {string} name The name of the field.
        * @param {CType|Type} type The type of the field.
        */
       add_field_at: function add_field_at(offset, name, type) {
         if (offset == null) {
           throw new TypeError("add_field_at requires a non-null offset");
         }
         if (!name) {
           throw new TypeError("add_field_at requires a non-null name");
         }
         if (!type) {
           throw new TypeError("add_field_at requires a non-null type");
         }
         if (type instanceof Type) {
           type = type.implementation;
         }
         if (this.offset_to_field_info[offset]) {
           throw new Error("HollowStructure " + this.name +
                           " already has a field at offset " + offset);
         }
         if (offset + type.size > this.size) {
           throw new Error("HollowStructure " + this.name +
                           " cannot place a value of type " + type +
                           " at offset " + offset +
                           " without exceeding its size of " + this.size);
         }
         let field = {name: name, type:type};
         this.offset_to_field_info[offset] = field;
       },

       /**
        * Create a pseudo-field that will only serve as padding.
        *
        * @param {number} size The number of bytes in the field.
        * @return {Object} An association field-name => field-type,
        * as expected by |ctypes.StructType|.
        */
       _makePaddingField: function makePaddingField(size) {
         let field = ({});
         field["padding_" + this._paddings] =
           ctypes.ArrayType(ctypes.uint8_t, size);
         this._paddings++;
         return field;
       },

       /**
        * Convert this |HollowStructure| into a |Type|.
        */
       getType: function getType() {
         // Contents of the structure, in the format expected
         // by ctypes.StructType.
         let struct = [];

         let i = 0;
         while (i < this.size) {
           let currentField = this.offset_to_field_info[i];
           if (!currentField) {
             // No field was specified at this offset, we need to
             // introduce some padding.

             // Firstly, determine how many bytes of padding
             let padding_length = 1;
             while (i + padding_length < this.size
                 && !this.offset_to_field_info[i + padding_length]) {
               ++padding_length;
             }

             // Then add the padding
             struct.push(this._makePaddingField(padding_length));

             // And proceed
             i += padding_length;
           } else {
             // We have a field at this offset.

             // Firstly, ensure that we do not have two overlapping fields
             for (let j = 1; j < currentField.type.size; ++j) {
               let candidateField = this.offset_to_field_info[i + j];
               if (candidateField) {
                 throw new Error("Fields " + currentField.name +
                   " and " + candidateField.name +
                   " overlap at position " + (i + j));
               }
             }

             // Then add the field
             let field = ({});
             field[currentField.name] = currentField.type;
             struct.push(field);

             // And proceed
             i += currentField.type.size;
           }
         }
         let result = new Type(this.name, ctypes.StructType(this.name, struct));
         if (result.implementation.size != this.size) {
           throw new Error("Wrong size for type " + this.name +
               ": expected " + this.size +
               ", found " + result.implementation.size +
               " (" + result.implementation.toSource() + ")");
         }
         return result;
       }
     };
     exports.OS.Shared.HollowStructure = HollowStructure;

     /**
      * Declare a function through js-ctypes
      *
      * @param {ctypes.library} lib The ctypes library holding the function.
      * @param {string} symbol The name of the function, as defined in the
      * library.
      * @param {ctypes.abi} abi The abi to use, or |null| for default.
      * @param {Type} returnType The type of values returned by the function.
      * @param {...Type} argTypes The type of arguments to the function.
      *
      * @return null if the function could not be defined (generally because
      * it does not exist), or a JavaScript wrapper performing the call to C
      * and any type conversion required.
      */// Note: Future versions will use a different implementation of this
        // function on the main thread, osfile worker thread and regular worker
        // thread
     let declareFFI = function declareFFI(lib, symbol, abi,
                                          returnType /*, argTypes ...*/) {
       if (exports.OS.Shared.DEBUG) {
         LOG("Attempting to declare FFI ", symbol);
       }
       // We guard agressively, to avoid any late surprise
       if (typeof symbol != "string") {
         throw new TypeError("declareFFI expects as first argument a string");
       }
       abi = abi || ctypes.default_abi;
       if (Object.prototype.toString.call(abi) != "[object CABI]") {
         // Note: This is the only known manner of checking whether an object
         // is an abi.
         throw new TypeError("declareFFI expects as second argument an abi or null");
       }
       if (!returnType.importFromC) {
         throw new TypeError("declareFFI expects as third argument an instance of Type");
       }
       let signature = [symbol, abi];
       let argtypes  = [];
       for (let i = 3; i < arguments.length; ++i) {
         let current = arguments[i];
         if (!current) {
           throw new TypeError("Missing type for argument " + ( i - 3 ) +
                               " of symbol " + symbol);
         }
         if (!current.implementation) {
           throw new TypeError("Missing implementation for argument " + (i - 3)
                               + " of symbol " + symbol
                               + " ( " + current.name + " )" );
         }
         signature.push(current.implementation);
       }
       try {
         let fun = lib.declare.apply(lib, signature);
         let result = function ffi(/*arguments*/) {
           let result = fun.apply(fun, arguments);
           return returnType.importFromC(result, symbol);
         };
         if (exports.OS.Shared.DEBUG) {
           result.fun = fun; // Also return the raw FFI function.
         }
	 if (exports.OS.Shared.DEBUG) {
          LOG("Function", symbol, "declared");
	 }
         return result;
       } catch (x) {
         // Note: Not being able to declare a function is normal.
         // Some functions are OS (or OS version)-specific.
	 if (exports.OS.Shared.DEBUG) {
          LOG("Could not declare function " + symbol, x);
	 }
         return null;
       }
     };
     exports.OS.Shared.declareFFI = declareFFI;

     // A bogus array type used to perform pointer arithmetics
     let gOffsetByType;

     /**
      * Advance a pointer by a number of items.
      *
      * This method implements adding an integer to a pointer in C.
      *
      * Example:
      *   // ptr is a uint16_t*,
      *   offsetBy(ptr, 3)
      *  // returns a uint16_t* with the address ptr + 3 * 2 bytes
      *
      * @param {C pointer} pointer The start pointer.
      * @param {number} length The number of items to advance. Must not be
      * negative.
      *
      * @return {C pointer} |pointer| advanced by |length| items
      */
     exports.OS.Shared.offsetBy =
       function offsetBy(pointer, length) {
         if (length === undefined || length < 0) {
           throw new TypeError("offsetBy expects a positive number");
         }
        if (!("isNull" in pointer)) {
           throw new TypeError("offsetBy expects a pointer");
         }
         if (length == 0) {
           return pointer;
         }
         let type = pointer.constructor;
         let size = type.targetType.size;
         if (size == 0 || size == null) {
           throw new TypeError("offsetBy cannot be applied to a pointer without size");
         }
         let bytes = length * size;
         if (!gOffsetByType || gOffsetByType.size <= bytes) {
           gOffsetByType = ctypes.uint8_t.array(bytes * 2);
         }
         let addr = ctypes.cast(pointer, gOffsetByType.ptr).
           contents.addressOfElement(bytes);
         return ctypes.cast(addr, type);
     };

// Encodings

   })(this);
}
