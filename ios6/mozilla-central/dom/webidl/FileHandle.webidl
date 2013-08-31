/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

interface DOMRequest;
interface LockedFile;

enum FileMode { "readonly", "readwrite" };

interface FileHandle : EventTarget {
  readonly attribute DOMString name;
  readonly attribute DOMString type;

  [Throws]
  LockedFile open(optional FileMode mode = "readonly");

  [Throws]
  DOMRequest getFile();

  [SetterThrows]
  attribute EventHandler onabort;
  [SetterThrows]
  attribute EventHandler onerror;
};
