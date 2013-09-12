if (this.Components) {
  throw new Error("This worker can only be loaded from a worker thread");
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


// Worker thread for osfile asynchronous front-end

(function(exports) {
  "use strict";

  // A simple flag used to control debugging messages.
  // FIXME: Once this library has been battle-tested, this flag will
  // either be removed or replaced with a pref.
  const DEBUG = false;

   try {
     importScripts("resource://gre/modules/osfile.jsm");

     let LOG = exports.OS.Shared.LOG.bind(exports.OS.Shared.LOG, "Agent");

     /**
      * Communications with the controller.
      *
      * Accepts messages:
      * {fun:function_name, args:array_of_arguments_or_null, id:id}
      *
      * Sends messages:
      * {ok: result, id:id} / {fail: serialized_form_of_OS.File.Error, id:id}
      */
     self.onmessage = function onmessage(msg) {
       let data = msg.data;
       if (DEBUG) {
         LOG("Received message", JSON.stringify(data));
       }
       let id = data.id;
       let result;
       let exn;
       try {
         let method = data.fun;
         if (DEBUG) {
           LOG("Calling method", method);
         }
         result = Agent[method].apply(Agent, data.args);
         if (DEBUG) {
           LOG("Method", method, "succeeded");
         }
       } catch (ex) {
         exn = ex;
         if (DEBUG) {
           LOG("Error while calling agent method", exn, exn.stack);
         }
       }
       // Now, post a reply, possibly as an uncaught error.
       // We post this message from outside the |try ... catch| block
       // to avoid capturing errors that take place during |postMessage| and
       // built-in serialization.
       if (!exn) {
         if (DEBUG) {
           LOG("Sending positive reply", JSON.stringify(result), "id is", id);
         }
         self.postMessage({ok: result, id:id});
       } else if (exn == StopIteration) {
         // StopIteration cannot be serialized automatically
         if (DEBUG) {
           LOG("Sending back StopIteration");
         }
         self.postMessage({StopIteration: true, id: id});
       } else if (exn instanceof exports.OS.File.Error) {
         if (DEBUG) {
           LOG("Sending back OS.File error", exn, "id is", id);
         }
         // Instances of OS.File.Error know how to serialize themselves
         // (deserialization ensures that we end up with OS-specific
         // instances of |OS.File.Error|)
         self.postMessage({fail: exports.OS.File.Error.toMsg(exn), id:id});
       } else {
         if (DEBUG) {
           LOG("Sending back regular error", exn, exn.stack, "id is", id);
         }
         // Other exceptions do not, and should be propagated through DOM's
         // built-in mechanism for uncaught errors, although this mechanism
         // may lose interesting information.
         throw exn;
       }
     };

     /**
      * A data structure used to track opened resources
      */
     let ResourceTracker = function ResourceTracker() {
       // A number used to generate ids
       this._idgen = 0;
       // A map from id to resource
       this._map = new Map();
     };
     ResourceTracker.prototype = {
       /**
        * Get a resource from its unique identifier.
        */
       get: function(id) {
         let result = this._map.get(id);
         if (result == null) {
           return result;
         }
         return result.resource;
       },
       /**
        * Remove a resource from its unique identifier.
        */
       remove: function(id) {
         if (!this._map.has(id)) {
           throw new Error("Cannot find resource id " + id);
         }
         this._map.delete(id);
       },
       /**
        * Add a resource, return a new unique identifier
        *
        * @param {*} resource A resource.
        * @param {*=} info Optional information. For debugging purposes.
        *
        * @return {*} A unique identifier. For the moment, this is a number,
        * but this might not remain the case forever.
        */
       add: function(resource, info) {
         let id = this._idgen++;
         this._map.set(id, {resource:resource, info:info});
         return id;
       }
     };

     /**
      * A map of unique identifiers to opened files.
      */
     let OpenedFiles = new ResourceTracker();

     /**
      * Execute a function in the context of a given file.
      *
      * @param {*} id A unique identifier, as used by |OpenFiles|.
      * @param {Function} f A function to call.
      * @return The return value of |f()|
      *
      * This function attempts to get the file matching |id|. If
      * the file exists, it executes |f| within the |this| set
      * to the corresponding file. Otherwise, it throws an error.
      */
     let withFile = function withFile(id, f) {
       let file = OpenedFiles.get(id);
       if (file == null) {
         throw new Error("Could not find File");
       }
       return f.call(file);
     };

     let OpenedDirectoryIterators = new ResourceTracker();
     let withDir = function withDir(fd, f) {
       let file = OpenedDirectoryIterators.get(fd);
       if (file == null) {
         throw new Error("Could not find Directory");
       }
       if (!(file instanceof File.DirectoryIterator)) {
         throw new Error("file is not a directory iterator " + file.__proto__.toSource());
       }
       return f.call(file);
     };

     let Type = exports.OS.Shared.Type;

     let File = exports.OS.File;

     /**
      * The agent.
      *
      * It is in charge of performing method-specific deserialization
      * of messages, calling the function/method of OS.File and serializing
      * back the results.
      */
     let Agent = {
       // Functions of OS.File
       stat: function stat(path) {
         return exports.OS.File.Info.toMsg(
           exports.OS.File.stat(Type.path.fromMsg(path)));
       },
       getCurrentDirectory: function getCurrentDirectory() {
         return exports.OS.Shared.Type.path.toMsg(File.getCurrentDirectory());
       },
       setCurrentDirectory: function setCurrentDirectory(path) {
         File.setCurrentDirectory(exports.OS.Shared.Type.path.fromMsg(path));
       },
       copy: function copy(sourcePath, destPath, options) {
         return File.copy(Type.path.fromMsg(sourcePath),
           Type.path.fromMsg(destPath), options);
       },
       move: function move(sourcePath, destPath, options) {
         return File.move(Type.path.fromMsg(sourcePath),
           Type.path.fromMsg(destPath), options);
       },
       makeDir: function makeDir(path, options) {
         return File.makeDir(Type.path.fromMsg(path), options);
       },
       removeEmptyDir: function removeEmptyDir(path, options) {
         return File.removeEmptyDir(Type.path.fromMsg(path), options);
       },
       remove: function remove(path) {
         return File.remove(Type.path.fromMsg(path));
       },
       open: function open(path, mode, options) {
         let file = File.open(Type.path.fromMsg(path), mode, options);
         return OpenedFiles.add(file);
       },
       read: function read(path, bytes) {
         return File.read(Type.path.fromMsg(path), bytes);
       },
       exists: function exists(path) {
         return File.exists(Type.path.fromMsg(path));
       },
       writeAtomic: function writeAtomic(path, buffer, options) {
         if (options.tmpPath) {
           options.tmpPath = Type.path.fromMsg(options.tmpPath);
         }
         return File.writeAtomic(Type.path.fromMsg(path),
                                 Type.voidptr_t.fromMsg(buffer),
                                 options
                                );
       },
       new_DirectoryIterator: function new_DirectoryIterator(path, options) {
         let iterator = new File.DirectoryIterator(Type.path.fromMsg(path), options);
         return OpenedDirectoryIterators.add(iterator);
       },
       // Methods of OS.File
       File_prototype_close: function close(fd) {
         return withFile(fd,
           function do_close() {
             try {
               return this.close();
             } finally {
               OpenedFiles.remove(fd);
             }
         });
       },
       File_prototype_stat: function stat(fd) {
         return withFile(fd,
           function do_stat() {
             return exports.OS.File.Info.toMsg(this.stat());
           });
       },
       File_prototype_readTo: function readTo(fd, buffer, options) {
         return withFile(fd,
           function do_readTo() {
             return this.readTo(exports.OS.Shared.Type.voidptr_t.fromMsg(buffer),
             options);
           });
       },
       File_prototype_write: function write(fd, buffer, options) {
         return withFile(fd,
           function do_write() {
             return this.write(exports.OS.Shared.Type.voidptr_t.fromMsg(buffer),
             options);
           });
       },
       File_prototype_setPosition: function setPosition(fd, pos, whence) {
         return withFile(fd,
           function do_setPosition() {
             return this.setPosition(pos, whence);
           });
       },
       File_prototype_getPosition: function getPosition(fd) {
         return withFile(fd,
           function do_getPosition() {
             return this.getPosition();
           });
       },
       // Methods of OS.File.DirectoryIterator
       DirectoryIterator_prototype_next: function next(dir) {
         return withDir(dir,
           function do_next() {
             try {
               return File.DirectoryIterator.Entry.toMsg(this.next());
             } catch (x) {
               if (x == StopIteration) {
                 OpenedDirectoryIterators.remove(dir);
               }
               throw x;
             }
           });
       },
       DirectoryIterator_prototype_nextBatch: function nextBatch(dir, size) {
         return withDir(dir,
           function do_nextBatch() {
             let result;
             try {
               result = this.nextBatch(size);
             } catch (x) {
               OpenedDirectoryIterators.remove(dir);
               throw x;
             }
             return result.map(File.DirectoryIterator.Entry.toMsg);
           });
       },
       DirectoryIterator_prototype_close: function close(dir) {
         return withDir(dir,
           function do_close() {
             this.close();
             OpenedDirectoryIterators.remove(dir);
           });
       }
     };
  } catch(ex) {
    dump("WORKER ERROR DURING SETUP " + ex + "\n");
    dump("WORKER ERROR DETAIL " + ex.stack + "\n");
  }
})(this);