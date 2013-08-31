/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://encoding.spec.whatwg.org/#interface-textdecoder
 *
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

[Constructor(optional DOMString label = "utf-8", optional TextDecoderOptions options)]
interface TextDecoder {
  readonly attribute DOMString encoding;
  [Throws]
  DOMString decode(optional ArrayBufferView? input = null, optional TextDecodeOptions options);
};

dictionary TextDecoderOptions {
  boolean fatal = false;
};

dictionary TextDecodeOptions {
  boolean stream = false;
};

