/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

function log(text) {
  dump("WORKER "+text+"\n");
}

function send(message) {
  self.postMessage(message);
}

self.onmessage = function(msg) {
  self.onmessage = function(msg) {
    log("ignored message "+JSON.stringify(msg.data));
  };

  test_init();
  test_GetCurrentDirectory();
  test_OpenClose();
  test_CreateFile();
  test_ReadWrite();
  test_path();
  finish();
};

function finish() {
  send({kind: "finish"});
}

function ok(condition, description) {
  send({kind: "ok", condition: condition, description:description});
}
function is(a, b, description) {
  let outcome = a == b; // Need to decide outcome here, as not everything can be serialized
  send({kind: "is", outcome: outcome, description: description, a:""+a, b:""+b});
}
function isnot(a, b, description) {
  let outcome = a != b; // Need to decide outcome here, as not everything can be serialized
  send({kind: "isnot", outcome: outcome, description: description, a:""+a, b:""+b});
}

function test_init() {
  ok(true, "Starting test_init");
  importScripts("resource:///modules/osfile.jsm");
}

function test_OpenClose() {
  ok(true, "Starting test_OpenClose");
  is(typeof OS.Win.File.CreateFile, "function", "OS.Win.File.CreateFile is a function");
  isnot(OS.Constants.Win.GENERIC_READ, undefined, "GENERIC_READ exists");
  isnot(OS.Constants.Win.FILE_SHARE_READ, undefined, "FILE_SHARE_READ exists");
  isnot(OS.Constants.Win.FILE_ATTRIBUTE_NORMAL, undefined, "FILE_ATTRIBUTE_NORMAL exists");
  let file = OS.Win.File.CreateFile(
    "chrome\\toolkit\\components\\osfile\\tests\\mochi\\worker_test_osfile_unix.js",
    OS.Constants.Win.GENERIC_READ,
    0,
    null,
    OS.Constants.Win.OPEN_EXISTING,
    0,
    null);
  ok(true, "test_OpenClose: Passed open");
  isnot(file, OS.Constants.Win.INVALID_HANDLE_VALUE, "test_OpenClose: file opened");
  let result = OS.Win.File.CloseHandle(file);
  isnot(result, 0, "test_OpenClose: close succeeded");

  file = OS.Win.File.CreateFile(
    "\\I do not exist",
    OS.Constants.Win.GENERIC_READ,
    OS.Constants.Win.FILE_SHARE_READ,
    null,
    OS.Constants.Win.OPEN_EXISTING,
    OS.Constants.Win.FILE_ATTRIBUTE_NORMAL,
    null);
  is(file, OS.Constants.Win.INVALID_HANDLE_VALUE, "test_OpenClose: cannot open non-existing file");
  is(ctypes.winLastError, OS.Constants.Win.ERROR_FILE_NOT_FOUND, "test_OpenClose: error is ERROR_FILE_NOT_FOUND");
}

function test_CreateFile()
{
  ok(true, "Starting test_CreateFile");
  let file = OS.Win.File.CreateFile(
    "test.tmp",
    OS.Constants.Win.GENERIC_READ | OS.Constants.Win.GENERIC_WRITE,
    OS.Constants.Win.FILE_SHARE_READ | OS.Constants.FILE_SHARE_WRITE,
    null,
    OS.Constants.Win.CREATE_ALWAYS,
    OS.Constants.Win.FILE_ATTRIBUTE_NORMAL,
    null);
  isnot(file, OS.Constants.Win.INVALID_HANDLE_VALUE, "test_CreateFile: opening succeeded");
  let result = OS.Win.File.CloseHandle(file);
  isnot(result, 0, "test_CreateFile: close succeeded");
}

function test_GetCurrentDirectory()
{
  let array = new (ctypes.ArrayType(ctypes.jschar, 4096))();
  let result = OS.Win.File.GetCurrentDirectory(4096, array);
  ok(result < array.length, "test_GetCurrentDirectory: length sufficient");
  ok(result > 0, "test_GetCurrentDirectory: length != 0");
}

function test_ReadWrite()
{
  ok(true, "Starting test_ReadWrite");
  let output_name = "osfile_copy.tmp";
  // Copy file
  let input = OS.Win.File.CreateFile(
    "chrome\\toolkit\\components\\osfile\\tests\\mochi\\worker_test_osfile_unix.js",
     OS.Constants.Win.GENERIC_READ,
     0,
     null,
     OS.Constants.Win.OPEN_EXISTING,
     0,
     null);
  isnot(input, OS.Constants.Win.INVALID_HANDLE_VALUE, "test_ReadWrite: input file opened");
  let output = OS.Win.File.CreateFile(
     "osfile_copy.tmp",
     OS.Constants.Win.GENERIC_READ | OS.Constants.Win.GENERIC_WRITE,
     0,
     null,
     OS.Constants.Win.CREATE_ALWAYS,
     OS.Constants.Win.FILE_ATTRIBUTE_NORMAL,
     null);
  isnot(output, OS.Constants.Win.INVALID_HANDLE_VALUE, "test_ReadWrite: output file opened");
  let array = new (ctypes.ArrayType(ctypes.char, 4096))();
  let bytes_read = new ctypes.int32_t(-1);
  let bytes_read_ptr = bytes_read.address();
  log("We have a pointer for bytes read: "+bytes_read_ptr);
  let bytes_written = new ctypes.int32_t(-1);
  let bytes_written_ptr = bytes_written.address();
  log("We have a pointer for bytes written: "+bytes_written_ptr);
  log("test_ReadWrite: buffer and pointers ready");
  let result;
  while (true) {
    log("test_ReadWrite: reading");
    result = OS.Win.File.ReadFile(input, array, 4096, bytes_read_ptr, null);
    isnot (result, 0, "test_ReadWrite: read success");
    let write_from = 0;
    let bytes_left = bytes_read;
    log("test_ReadWrite: read chunk complete " + bytes_left.value);
    if (bytes_left.value == 0) {
      break;
    }
    while (bytes_left.value > 0) {
      log("test_ReadWrite: writing "+bytes_left.value);
      let ptr = array.addressOfElement(write_from);
      // Note: |WriteFile| launches an exception in case of error
      result = OS.Win.File.WriteFile(output, array, bytes_left, bytes_written_ptr, null);
      isnot (result, 0, "test_ReadWrite: write success");
      write_from += bytes_written;
      bytes_left -= bytes_written;
    }
  }
  ok(true, "test_ReadWrite: copy complete");

  // Compare files
  result = OS.Win.File.SetFilePointer(input, 0, null, OS.Constants.Win.FILE_BEGIN);
  isnot (result, OS.Constants.Win.INVALID_SET_FILE_POINTER, "test_ReadWrite: input reset");

  result = OS.Win.File.SetFilePointer(output, 0, null, OS.Constants.Win.FILE_BEGIN);
  isnot (result, OS.Constants.Win.INVALID_SET_FILE_POINTER, "test_ReadWrite: output reset");

  let array2 = new (ctypes.ArrayType(ctypes.char, 4096))();
  let bytes_read2 = new ctypes.int32_t(-1);
  let bytes_read2_ptr = bytes_read2.address();
  let pos = 0;
  while (true) {
    result = OS.Win.File.ReadFile(input, array, 4096, bytes_read_ptr, null);
    isnot(result, 0, "test_ReadWrite: input read succeeded");

    result = OS.Win.File.ReadFile(output, array2, 4096, bytes_read2_ptr, null);
    isnot(result, 0, "test_ReadWrite: output read succeeded");

    is(bytes_read.value > 0, bytes_read2.value > 0,
       "Both files contain data or neither does " + bytes_read.value + ", " + bytes_read2.value);
    if (bytes_read.value == 0) {
      break;
    }
    let bytes;
    if (bytes_read.value != bytes_read2.value) {
      // This would be surprising, but theoretically possible with a
      // remote file system, I believe.
      bytes = Math.min(bytes_read.value, bytes_read2.value);
      pos += bytes;
      result = OS.Win.File.SetFilePointer(input,  pos, null, OS.Constants.Win.FILE_BEGIN);
      isnot(result, 0, "test_ReadWrite: input seek succeeded");

      result = OS.Win.File.SetFilePointer(output, pos, null, OS.Constants.Win.FILE_BEGIN);
      isnot(result, 0, "test_ReadWrite: output seek succeeded");

    } else {
      bytes = bytes_read.value;
      pos += bytes;
    }
    for (let i = 0; i < bytes; ++i) {
      if (array[i] != array2[i]) {
        ok(false, "Files do not match at position " + i
           + " ("+array[i] + "/"+array2[i] + ")");
      }
    }
  }
  ok(true, "test_ReadWrite test complete");
  result = OS.Win.File.CloseHandle(input);
  isnot(result, 0, "test_ReadWrite: inpout close succeeded");
  result = OS.Win.File.CloseHandle(output);
  isnot(result, 0, "test_ReadWrite: outpout close succeeded");
  result = OS.Win.File.DeleteFile(output_name);
  isnot(result, 0, "test_ReadWrite: output remove succeeded");
  ok(true, "test_ReadWrite cleanup complete");
}

function test_path()
{
  ok(true, "test_path: starting");
  is(OS.Win.Path.basename("a\\b"), "b", "basename of a\\b");
  is(OS.Win.Path.basename("a\\b\\"), "", "basename of a\\b\\");
  is(OS.Win.Path.basename("abc"), "abc", "basename of abc");
  is(OS.Win.Path.dirname("a\\b"), "a", "dirname of a\\b");
  is(OS.Win.Path.dirname("a\\b\\"), "a\\b", "dirname of a\\b\\");
  is(OS.Win.Path.dirname("a\\\\\\\\b"), "a", "dirname of a\\\\\\b");
  is(OS.Win.Path.dirname("abc"), ".", "dirname of abc");
  is(OS.Win.Path.normalize("\\a\\b\\c"), "\\a\\b\\c", "normalize \\a\\b\\c");
  is(OS.Win.Path.normalize("\\a\\b\\\\\\\\c"), "\\a\\b\\c", "normalize \\a\\b\\\\\\\\c");
  is(OS.Win.Path.normalize("\\a\\b\\c\\\\\\"), "\\a\\b\\c", "normalize \\a\\b\\c\\\\\\");
  is(OS.Win.Path.normalize("\\a\\b\\c\\..\\..\\..\\d\\e\\f"), "\\d\\e\\f", "normalize \\a\\b\\c\\..\\..\\..\\d\\e\\f");
  is(OS.Win.Path.normalize("a\\b\\c\\..\\..\\..\\d\\e\\f"), "d\\e\\f", "normalize a\\b\\c\\..\\..\\..\\d\\e\\f");
  let error = false;
  try {
    OS.Win.Path.normalize("\\a\\b\\c\\..\\..\\..\\..\\d\\e\\f");
  } catch (x) {
    error = true;
  }
  ok(error, "cannot normalize \\a\\b\\c\\..\\..\\..\\..\\d\\e\\f");
  is(OS.Win.Path.join("\\tmp", "foo", "bar"), "\\tmp\\foo\\bar", "join \\tmp,foo,bar");
  is(OS.Win.Path.join("\\tmp", "\\foo", "bar"), "\\foo\\bar", "join \\tmp,\\foo,bar");

  is(OS.Win.Path.basename("c:a\\b"), "b", "basename of c:a\\b");
  is(OS.Win.Path.basename("c:a\\b\\"), "", "basename of c:a\\b\\");
  is(OS.Win.Path.basename("c:abc"), "abc", "basename of c:abc");
  is(OS.Win.Path.dirname("c:a\\b"), "c:a", "dirname of c:a\\b");
  is(OS.Win.Path.dirname("c:a\\b\\"), "c:a\\b", "dirname of c:a\\b\\");
  is(OS.Win.Path.dirname("c:a\\\\\\\\b"), "c:a", "dirname of c:a\\\\\\b");
  is(OS.Win.Path.dirname("c:abc"), "c:", "dirname of c:abc");
  let options = {
    winNoDrive: true
  };
  is(OS.Win.Path.dirname("c:a\\b", options), "a", "dirname of c:a\\b");
  is(OS.Win.Path.dirname("c:a\\b\\", options), "a\\b", "dirname of c:a\\b\\");
  is(OS.Win.Path.dirname("c:a\\\\\\\\b", options), "a", "dirname of c:a\\\\\\b");
  is(OS.Win.Path.dirname("c:abc", options), ".", "dirname of c:abc");

  is(OS.Win.Path.normalize("c:\\a\\b\\c"), "c:\\a\\b\\c", "normalize c:\\a\\b\\c");
  is(OS.Win.Path.normalize("c:\\a\\b\\\\\\\\c"), "c:\\a\\b\\c", "normalize c:\\a\\b\\\\\\\\c");
  is(OS.Win.Path.normalize("c:\\\\\\\\a\\b\\c"), "c:\\a\\b\\c", "normalize c:\\\\\\a\\b\\c");
  is(OS.Win.Path.normalize("c:\\a\\b\\c\\\\\\"), "c:\\a\\b\\c", "normalize c:\\a\\b\\c\\\\\\");
  is(OS.Win.Path.normalize("c:\\a\\b\\c\\..\\..\\..\\d\\e\\f"), "c:\\d\\e\\f", "normalize c:\\a\\b\\c\\..\\..\\..\\d\\e\\f");
  is(OS.Win.Path.normalize("c:a\\b\\c\\..\\..\\..\\d\\e\\f"), "c:d\\e\\f", "normalize c:a\\b\\c\\..\\..\\..\\d\\e\\f");
  error = false;
  try {
    OS.Win.Path.normalize("c:\\a\\b\\c\\..\\..\\..\\..\\d\\e\\f");
  } catch (x) {
    error = true;
  }
  ok(error, "cannot normalize c:\\a\\b\\c\\..\\..\\..\\..\\d\\e\\f");
  is(OS.Win.Path.join("c:\\tmp", "foo", "bar"), "c:\\tmp\\foo\\bar", "join c:\\tmp,foo,bar");
  is(OS.Win.Path.join("c:\\tmp", "\\foo", "bar"), "c:\\foo\\bar", "join c:\\tmp,\\foo,bar");
  is(OS.Win.Path.join("c:\\tmp", "c:\\foo", "bar"), "c:\\foo\\bar", "join c:\\tmp,c:\\foo,bar");
  is(OS.Win.Path.join("c:\\tmp", "c:foo", "bar"), "c:foo\\bar", "join c:\\tmp,c:foo,bar");

  ok(true, "test_path: complete");
}
