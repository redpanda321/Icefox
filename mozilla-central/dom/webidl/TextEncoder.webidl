/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://encoding.spec.whatwg.org/#interface-textencoder
 *
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

[Constructor(optional DOMString label = "utf-8")]
interface TextEncoder {
  readonly attribute DOMString encoding;
  [Throws]
  Uint8Array encode(optional DOMString? input = null, optional TextEncodeOptions options);
};

dictionary TextEncodeOptions {
  boolean stream = false;
};

