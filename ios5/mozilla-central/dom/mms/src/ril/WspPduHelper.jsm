/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/wap_consts.js");

let DEBUG; // set to true to see debug messages

// Special ASCII characters
const NUL = 0;
const CR = 13;
const LF = 10;
const SP = 32;
const HT = 9;
const DQUOTE = 34;
const DEL = 127;

// Special ASCII character ranges
const CTLS = 32;
const ASCIIS = 128;

/**
 * Error class for generic encoding/decoding failures.
 */
function CodeError(message) {
  this.name = "CodeError";
  this.message = message || "Invalid format";
}
CodeError.prototype = new Error();
CodeError.prototype.constructor = CodeError;

/**
 * Error class for unexpected NUL char at decoding text elements.
 *
 * @param message [optional]
 *        A short description for the error.
 */
function NullCharError(message) {
  this.name = "NullCharError";
  this.message = message || "Null character found";
}
NullCharError.prototype = new CodeError();
NullCharError.prototype.constructor = NullCharError;

/**
 * Error class for fatal encoding/decoding failures.
 *
 * This error is only raised when expected format isn't met and the parser
 * context can't do anything more to either skip it or hand over to other
 * alternative encoding/decoding steps.
 *
 * @param message [optional]
 *        A short description for the error.
 */
function FatalCodeError(message) {
  this.name = "FatalCodeError";
  this.message = message || "Decoding fails";
}
FatalCodeError.prototype = new Error();
FatalCodeError.prototype.constructor = FatalCodeError;

/**
 * Error class for undefined well known encoding.
 *
 * When a encoded header field/parameter has unknown/unsupported value, we may
 * never know how to decode the next value. For example, a parameter of
 * undefined well known encoding may be followed by a Q-value, which is
 * basically a uintvar. However, there is no way you can distiguish an Q-value
 * 0.64, encoded as 0x41, from a string begins with 'A', which is also 0x41.
 * The `skipValue` will try the latter one, which is not expected.
 *
 * @param message [optional]
 *        A short description for the error.
 */
function NotWellKnownEncodingError(message) {
  this.name = "NotWellKnownEncodingError";
  this.message = message || "Not well known encoding";
}
NotWellKnownEncodingError.prototype = new FatalCodeError();
NotWellKnownEncodingError.prototype.constructor = NotWellKnownEncodingError;

/**
 * Internal helper function to retrieve the value of a property with its name
 * specified by `name` inside the object `headers`.
 *
 * @param headers
 *        An object that contains parsed header fields.
 * @param name
 *        Header name string to be checked.
 *
 * @return Value of specified header field.
 *
 * @throws FatalCodeError if headers[name] is undefined.
 */
function ensureHeader(headers, name) {
  let value = headers[name];
  // Header field might have a null value as NoValue
  if (value === undefined) {
    throw new FatalCodeError("ensureHeader: header " + name + " not defined");
  }
  return value;
}

/**
 * Skip field value.
 *
 * The WSP field values are encoded so that the length of the field value can
 * always be determined, even if the detailed format of a specific field value
 * is not known. This makes it possible to skip over individual header fields
 * without interpreting their content. ... the first octet in all the field
 * values can be interpreted as follows:
 *
 *   0 -  30 | This octet is followed by the indicated number (0 - 30) of data
 *             octets.
 *        31 | This octet is followed by a unitvar, which indicates the number
 *             of data octets after it.
 *  32 - 127 | The value is a text string, terminated by a zero octet (NUL
 *             character).
 * 128 - 255 | It is an encoded 7-bit value; this header has no more data.
 *
 * @param data
 *        A wrapped object containing raw PDU data.
 *
 * @return Skipped value of several possible types like string, integer, or
 *         an array of octets.
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.1.2
 */
function skipValue(data) {
  let begin = data.offset;
  let value = Octet.decode(data);
  if (value <= 31) {
    if (value == 31) {
      value = UintVar.decode(data);
    }

    if (value) {
      // `value` can be larger than 30, max length of a multi-octet integer
      // here. So we must decode it as an array instead.
      value = Octet.decodeMultiple(data, data.offset + value);
    } else {
      value = null;
    }
  } else if (value <= 127) {
    data.offset = begin;
    value = NullTerminatedTexts.decode(data);
  } else {
    value &= 0x7F;
  }

  return value;
}

/**
 * Helper function for decoding multiple alternative forms.
 *
 * @param data
 *        A wrapped object containing raw PDU data.
 * @param options
 *        Extra context for decoding.
 *
 * @return Decoded value.
 */
function decodeAlternatives(data, options) {
  let begin = data.offset;
  for (let i = 2; i < arguments.length; i++) {
    try {
      return arguments[i].decode(data, options);
    } catch (e) {
      // Throw the last exception we get
      if (i == (arguments.length - 1)) {
        throw e;
      }

      data.offset = begin;
    }
  }
}

/**
 * Helper function for encoding multiple alternative forms.
 *
 * @param data
 *        A wrapped object to store encoded raw data.
 * @param value
 *        Object value of arbitrary type to be encoded.
 * @param options
 *        Extra context for encoding.
 */
function encodeAlternatives(data, value, options) {
  let begin = data.offset;
  for (let i = 3; i < arguments.length; i++) {
    try {
      arguments[i].encode(data, value, options);
      return;
    } catch (e) {
      // Throw the last exception we get
      if (i == (arguments.length - 1)) {
        throw e;
      }

      data.offset = begin;
    }
  }
}

let Octet = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @throws RangeError if no more data is available.
   */
  decode: function decode(data) {
    if (data.offset >= data.array.length) {
      throw new RangeError();
    }

    return data.array[data.offset++];
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   * @param end
   *        An ending offset indicating the end of octet array to read.
   *
   * @return A decoded array object.
   *
   * @throws RangeError if no enough data to read.
   * @throws TypeError if `data` has neither subarray() nor slice() method.
   */
  decodeMultiple: function decodeMultiple(data, end) {
    if ((end < data.offset) || (end > data.array.length)) {
      throw new RangeError();
    }
    if (end == data.offset) {
      return null;
    }

    let result;
    if (data.array.subarray) {
      result = data.array.subarray(data.offset, end);
    } else if (data.array.slice) {
      result = data.array.slice(data.offset, end);
    } else {
      throw new TypeError();
    }

    data.offset = end;
    return result;
  },

  /**
   * Internal octet decoding for specific value.
   *
   * @param data
   *        A wrapped object containing raw PDU data.
   * @param expected
   *        Expected octet value.
   *
   * @return Expected octet value.
   *
   * @throws CodeError if read octet is not equal to expected one.
   */
  decodeEqualTo: function decodeEqualTo(data, expected) {
    if (this.decode(data) != expected) {
      throw new CodeError("Octet - decodeEqualTo: doesn't match " + expected);
    }

    return expected;
  },

  /**
   * @param data
   *        A wrapped object to store encoded raw data.
   * @param octet
   *        Octet value to be encoded.
   */
  encode: function encode(data, octet) {
    if (data.offset >= data.array.length) {
      data.array.push(octet);
      data.offset++;
    } else {
      data.array[data.offset++] = octet;
    }
  },
};

/**
 * TEXT = <any OCTET except CTLs, but including LWS>
 * CTL = <any US-ASCII control character (octets 0 - 31) and DEL (127)>
 * LWS = [CRLF] 1*(SP|HT)
 * CRLF = CR LF
 * CR = <US-ASCII CR, carriage return (13)>
 * LF = <US-ASCII LF, linefeed (10)>
 * SP = <US-ASCII SP, space (32)>
 * HT = <US-ASCII HT, horizontal-tab(9)>
 *
 * @see RFC 2616 clause 2.2 Basic Rules
 */
let Text = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded character.
   *
   * @throws NullCharError if a NUL character read.
   * @throws CodeError if a control character read.
   */
  decode: function decode(data) {
    let code = Octet.decode(data);
    if ((code >= CTLS) && (code != DEL)) {
      return String.fromCharCode(code);
    }

    if (code == NUL) {
      throw new NullCharError();
    }

    if (code != CR) {
      throw new CodeError("Text: invalid char code " + code);
    }

    // "A CRLF is allowed in the definition of TEXT only as part of a header
    // field continuation. It is expected that the folding LWS will be
    // replaced with a single SP before interpretation of the TEXT value."
    // ~ RFC 2616 clause 2.2

    let extra;

    // Rethrow everything as CodeError. We had already a successful read above.
    try {
      extra = Octet.decode(data);
      if (extra != LF) {
        throw new CodeError("Text: doesn't match LWS sequence");
      }

      extra = Octet.decode(data);
      if ((extra != SP) && (extra != HT)) {
        throw new CodeError("Text: doesn't match LWS sequence");
      }
    } catch (e if e instanceof CodeError) {
      throw e;
    } catch (e) {
      throw new CodeError("Text: doesn't match LWS sequence");
    }

    // Let's eat as many SP|HT as possible.
    let begin;

    // Do not throw anything here. We had already matched (SP | HT).
    try {
      do {
        begin = data.offset;
        extra = Octet.decode(data);
      } while ((extra == SP) || (extra == HT));
    } catch (e) {}

    data.offset = begin;
    return " ";
  },

  /**
   * @param data
   *        A wrapped object to store encoded raw data.
   * @param text
   *        String text of one character to be encoded.
   *
   * @throws CodeError if a control character got.
   */
  encode: function encode(data, text) {
    if (!text) {
      throw new CodeError("Text: empty string");
    }

    let code = text.charCodeAt(0);
    if ((code < CTLS) || (code == DEL) || (code > 255)) {
      throw new CodeError("Text: invalid char code " + code);
    }
    Octet.encode(data, code);
  },
};

let NullTerminatedTexts = {
  /**
   * Decode internal referenced null terminated text string.
   *
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded string.
   */
  decode: function decode(data) {
    let str = "";
    try {
      // A End-of-string is also a CTL, which should cause a error.
      while (true) {
        str += Text.decode(data);
      }
    } catch (e if e instanceof NullCharError) {
      return str;
    }
  },

  /**
   * @param data
   *        A wrapped object to store encoded raw data.
   * @param str
   *        A String to be encoded.
   */
  encode: function encode(data, str) {
    if (str) {
      for (let i = 0; i < str.length; i++) {
        Text.encode(data, str.charAt(i));
      }
    }
    Octet.encode(data, 0);
  },
};

/**
 * TOKEN = 1*<any CHAR except CTLs or separators>
 * CHAR = <any US-ASCII character (octets 0 - 127)>
 * SEPARATORS = ()<>@,;:\"/[]?={} SP HT
 *
 * @see RFC 2616 clause 2.2 Basic Rules
 */
let Token = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded character.
   *
   * @throws NullCharError if a NUL character read.
   * @throws CodeError if an invalid character read.
   */
  decode: function decode(data) {
    let code = Octet.decode(data);
    if ((code < ASCIIS) && (code >= CTLS)) {
      if ((code == HT) || (code == SP)
          || (code == 34) || (code == 40) || (code == 41) // ASCII "()
          || (code == 44) || (code == 47)                 // ASCII ,/
          || ((code >= 58) && (code <= 64))               // ASCII :;<=>?@
          || ((code >= 91) && (code <= 93))               // ASCII [\]
          || (code == 123) || (code == 125)) {            // ASCII {}
        throw new CodeError("Token: invalid char code " + code);
      }

      return String.fromCharCode(code);
    }

    if (code == NUL) {
      throw new NullCharError();
    }

    throw new CodeError("Token: invalid char code " + code);
  },

  /**
   * @param data
   *        A wrapped object to store encoded raw data.
   * @param token
   *        String text of one character to be encoded.
   *
   * @throws CodeError if an invalid character got.
   */
  encode: function encode(data, token) {
    if (!token) {
      throw new CodeError("Token: empty string");
    }

    let code = token.charCodeAt(0);
    if ((code < ASCIIS) && (code >= CTLS)) {
      if ((code == HT) || (code == SP)
          || (code == 34) || (code == 40) || (code == 41) // ASCII "()
          || (code == 44) || (code == 47)                 // ASCII ,/
          || ((code >= 58) && (code <= 64))               // ASCII :;<=>?@
          || ((code >= 91) && (code <= 93))               // ASCII [\]
          || (code == 123) || (code == 125)) {            // ASCII {}
        // Fallback to throw CodeError
      } else {
        Octet.encode(data, token.charCodeAt(0));
	return;
      }
    }

    throw new CodeError("Token: invalid char code " + code);
  },
};

/**
 * uric       = reserved | unreserved | escaped
 * reserved   = ;/?:@&=+$,
 * unreserved = alphanum | mark
 * mark       = -_.!~*'()
 * escaped    = % hex hex
 * excluded but used = #%
 *
 * Or, in decimal, they are: 33,35-59,61,63-90,95,97-122,126
 *
 * @see RFC 2396 Uniform Resource Indentifiers (URI)
 */
let URIC = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded character.
   *
   * @throws NullCharError if a NUL character read.
   * @throws CodeError if an invalid character read.
   */
  decode: function decode(data) {
    let code = Octet.decode(data);
    if (code == NUL) {
      throw new NullCharError();
    }

    if ((code <= CTLS) || (code >= ASCIIS) || (code == 34) || (code == 60)
        || (code == 62) || ((code >= 91) && (code <= 94)) || (code == 96)
        || ((code >= 123) && (code <= 125)) || (code == 127)) {
      throw new CodeError("URIC: invalid char code " + code);
    }

    return String.fromCharCode(code);
  },
};

/**
 * If the first character in the TEXT is in the range of 128-255, a Quote
 * character must precede it. Otherwise the Quote character must be omitted.
 * The Quote is not part of the contents.
 *
 *   Text-string = [Quote] *TEXT End-of-string
 *   Quote = <Octet 127>
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.1
 */
let TextString = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded string.
   */
  decode: function decode(data) {
    let begin = data.offset;
    let firstCode = Octet.decode(data);
    if (firstCode == 127) {
      // Quote found, check if first char code is larger-equal than 128.
      begin = data.offset;
      try {
        if (Octet.decode(data) < 128) {
          throw new CodeError("Text-string: illegal quote found.");
        }
      } catch (e if e instanceof CodeError) {
        throw e;
      } catch (e) {
        throw new CodeError("Text-string: unexpected error.");
      }
    } else if (firstCode >= 128) {
      throw new CodeError("Text-string: invalid char code " + firstCode);
    }

    data.offset = begin;
    return NullTerminatedTexts.decode(data);
  },

  /**
   * @param data
   *        A wrapped object to store encoded raw data.
   * @param str
   *        A String to be encoded.
   */
  encode: function encode(data, str) {
    if (!str) {
      Octet.encode(data, 0);
      return;
    }

    let firstCharCode = str.charCodeAt(0);
    if (firstCharCode >= 128) {
      Octet.encode(data, 127);
    }

    NullTerminatedTexts.encode(data, str);
  },
};

/**
 * Token-text = Token End-of-string
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.1
 */
let TokenText = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded string.
   */
  decode: function decode(data) {
    let str = "";
    try {
      // A End-of-string is also a CTL, which should cause a error.
      while (true) {
        str += Token.decode(data);
      }
    } catch (e if e instanceof NullCharError) {
      return str;
    }
  },

  /**
   * @param data
   *        A wrapped object to store encoded raw data.
   * @param str
   *        A String to be encoded.
   */
  encode: function encode(data, str) {
    if (str) {
      for (let i = 0; i < str.length; i++) {
        Token.encode(data, str.charAt(i));
      }
    }
    Octet.encode(data, 0);
  },
};

/**
 * The TEXT encodes an RFC2616 Quoted-string with the enclosing
 * quotation-marks <"> removed.
 *
 *   Quoted-string = <Octet 34> *TEXT End-of-string
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.1
 */
let QuotedString = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded string.
   *
   * @throws CodeError if first octet read is not 0x34.
   */
  decode: function decode(data) {
    let value = Octet.decode(data);
    if (value != 34) {
      throw new CodeError("Quoted-string: not quote " + value);
    }

    return NullTerminatedTexts.decode(data);
  },
};

/**
 * Integers in range 0-127 shall be encoded as a one octet value with the
 * most significant bit set to one (1xxx xxxx) and with the value in the
 * remaining least significant bits.
 *
 *   Short-integer = OCTET
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.1
 */
let ShortInteger = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded integer value.
   *
   * @throws CodeError if the octet read is less than 0x80.
   */
  decode: function decode(data) {
    let value = Octet.decode(data);
    if (!(value & 0x80)) {
      throw new CodeError("Short-integer: invalid value " + value);
    }

    return (value & 0x7F);
  },

  /**
   * @param data
   *        A wrapped object to store encoded raw data.
   * @param value
   *        A numeric value to be encoded.
   *
   * @throws CodeError if the octet read is larger-equal than 0x80.
   */
  encode: function encode(data, value) {
    if (value & 0x80) {
      throw new CodeError("Short-integer: invalid value " + value);
    }

    Octet.encode(data, value | 0x80);
  },
};

/**
 * The content octets shall be an unsigned integer value with the most
 * significant octet encoded first (big-endian representation). The minimum
 * number of octets must be used to encode the value.
 *
 *   Long-integer = Short-length Multi-octet-integer
 *   Short-length = <Any octet 0-30>
 *   Multi-octet-integer = 1*30 OCTET
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.1
 */
let LongInteger = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   * @param length
   *        Number of octets to read.
   *
   * @return A decoded integer value or an octets array of max 30 elements.
   */
  decodeMultiOctetInteger: function decodeMultiOctetInteger(data, length) {
    if (length < 7) {
      // Return a integer instead of an array as possible. For a multi-octet
      // integer, there are only maximum 53 bits for integer in javascript. We
      // will get an inaccurate one beyond that. We can't neither use bitwise
      // operation here, for it will be limited in 32 bits.
      let value = 0;
      while (length--) {
        value = value * 256 + Octet.decode(data);
      }
      return value;
    }

    return Octet.decodeMultiple(data, data.offset + length);
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded integer value or an octets array of max 30 elements.
   *
   * @throws CodeError if the length read is not in 1..30.
   */
  decode: function decode(data) {
    let length = Octet.decode(data);
    if ((length < 1) || (length > 30)) {
      throw new CodeError("Long-integer: invalid length " + length);
    }

    return this.decodeMultiOctetInteger(data, length);
  },
};

/**
 * @see WAP-230-WSP-20010705-a clause 8.4.2.1
 */
let UintVar = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded integer value.
   */
  decode: function decode(data) {
    let value = Octet.decode(data);
    let result = value & 0x7F;
    while (value & 0x80) {
      value = Octet.decode(data);
      result = result * 128 + (value & 0x7F);
    }

    return result;
  },
};

/**
 * This encoding is used for token values, which have no well-known binary
 * encoding, or when the assigned number of the well-known encoding is small
 * enough to fit into Short-Integer.
 *
 *   Constrained-encoding = Extension-Media | Short-integer
 *   Extension-Media = *TEXT End-of-string
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.1
 */
let ConstrainedEncoding = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decode integer value or string.
   */
  decode: function decode(data) {
    return decodeAlternatives(data, null, NullTerminatedTexts, ShortInteger);
  },
};

/**
 * Value-length = Short-length | (Length-quote Length)
 * Short-length = <Any octet 0-30>
 * Length-quote = <Octet 31>
 * Length = Uintvar-integer
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.2
 */
let ValueLength = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded integer value.
   *
   * @throws CodeError if the first octet read is larger than 31.
   */
  decode: function decode(data) {
    let value = Octet.decode(data);
    if (value <= 30) {
      return value;
    }

    if (value == 31) {
      return UintVar.decode(data);
    }

    throw new CodeError("Value-length: invalid value " + value);
  },
};

/**
 * No-value = <Octet 0>
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.3
 */
let NoValue = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Always returns null.
   */
  decode: function decode(data) {
    Octet.decodeEqualTo(data, 0);
    return null;
  },
};

/**
 * Text-value = No-value | Token-text | Quoted-string
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.3
 */
let TextValue = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded string or null for No-value.
   */
  decode: function decode(data) {
    return decodeAlternatives(data, null, NoValue, TokenText, QuotedString);
  },
};

/**
 * Integer-Value = Short-integer | Long-integer
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.3
 */
let IntegerValue = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded integer value or array of octets.
   */
  decode: function decode(data) {
    return decodeAlternatives(data, null, ShortInteger, LongInteger);
  },
};

/**
 * The encoding of dates shall be done in number of seconds from
 * 1970-01-01, 00:00:00 GMT.
 *
 *   Date-value = Long-integer
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.3
 */
let DateValue = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A Date object.
   */
  decode: function decode(data) {
    let numOrArray = LongInteger.decode(data);
    let seconds;
    if (typeof numOrArray == "number") {
      seconds = numOrArray;
    } else {
      seconds = 0;
      for (let i = 0; i < numOrArray.length; i++) {
        seconds = seconds * 256 + numOrArray[i];
      }
    }

    return new Date(seconds * 1000);
  },
};

/**
 * Delta-seconds-value = Integer-value
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.3
 */
let DeltaSecondsValue = IntegerValue;

/**
 * Quality factor 0 and quality factors with one or two decimal digits are
 * encoded into 1-100; three digits ones into 101-1099.
 *
 *   Q-value = 1*2 OCTET
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.3
 */
let QValue = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded integer value of 1..1099.
   *
   * @throws CodeError if decoded UintVar is not in range 1..1099.
   */
  decode: function decode(data) {
    let value = UintVar.decode(data);
    if (value > 0) {
      if (value <= 100) {
        return (value - 1) / 100.0;
      }
      if (value <= 1099) {
        return (value - 100) / 1000.0;
      }
    }

    throw new CodeError("Q-value: invalid value " + value);
  },
};

/**
 * The three most significant bits of the Short-integer value are interpreted
 * to encode a major version number in the range 1-7, and the four least
 * significant bits contain a minor version number in the range 0-14. If
 * there is only a major version number, this is encoded by placing the value
 * 15 in the four least significant bits.
 *
 *   Version-value = Short-integer | Text-string
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.3
 */
let VersionValue = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Binary encoded version number.
   */
  decode: function decode(data) {
    let begin = data.offset;
    let value;
    try {
      value = ShortInteger.decode(data);
      if ((value >= 0x10) && (value < 0x80)) {
        return value;
      }

      throw new CodeError("Version-value: invalid value " + value);
    } catch (e) {}

    data.offset = begin;

    let str = TextString.decode(data);
    if (!str.match(/^[1-7](\.1?\d)?$/)) {
      throw new CodeError("Version-value: invalid value " + str);
    }

    let major = str.charCodeAt(0) - 0x30;
    let minor = 0x0F;
    if (str.length > 1) {
      minor = str.charCodeAt(2) - 0x30;
      if (str.length > 3) {
        minor = 10 + (str.charCodeAt(3) - 0x30);
        if (minor > 14) {
          throw new CodeError("Version-value: invalid minor " + minor);
        }
      }
    }

    return major << 4 | minor;
  },
};

/**
 * URI value should be encoded per [RFC2616], but service user may use a
 * different format.
 *
 *   Uri-value = Text-string
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.3
 * @see RFC 2616 clause 2.2 Basic Rules
 */
let UriValue = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded uri string.
   */
  decode: function decode(data) {
    let str = "";
    try {
      // A End-of-string is also a CTL, which should cause a error.
      while (true) {
        str += URIC.decode(data);
      }
    } catch (e if e instanceof NullCharError) {
      return str;
    }
  },
};

/**
 * Parameter = Typed-parameter | Untyped-parameter
 *
 * For Typed-parameters, the actual expected type of the value is implied by
 * the well-known parameter. In addition to the expected type, there may be no
 * value. If the value cannot be encoded using expected type, it shall be
 * encoded as text.
 *
 *   Typed-parameter = Well-known-parameter-token Typed-value
 *   Well-known-parameter-token = Integer-value
 *   Typed-value = Compact-value | Text-value
 *   Compact-value = Integer-value | Date-value | Delta-seconds-value | Q-value
 *                   | Version-value | Uri-value
 *
 * For Untyped-parameters, the type of the value is unknown, but is shall be
 * encoded as an integer, if that is possible.
 *
 *   Untyped-parameter = Token-text Untyped-value
 *   Untyped-value = Integer-value | Text-value
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.4
 */
let Parameter = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded object containing `name` and `value` properties or null
   *         if something wrong. The `name` property must be a string, but the
   *         `value` property can be many different types depending on `name`.
   *
   * @throws CodeError if decoded IntegerValue is an array.
   * @throws NotWellKnownEncodingError if decoded well-known parameter number
   *         is not registered or supported.
   */
  decodeTypedParameter: function decodeTypedParameter(data) {
    let numOrArray = IntegerValue.decode(data);
    // `decodeIntegerValue` can return a array, which doesn't apply here.
    if (typeof numOrArray != "number") {
      throw new CodeError("Typed-parameter: invalid integer type");
    }

    let number = numOrArray;
    let param = WSP_WELL_KNOWN_PARAMS[number];
    if (!param) {
      throw new NotWellKnownEncodingError(
        "Typed-parameter: not well known parameter " + number);
    }

    let begin = data.offset, value;
    try {
      // Althought Text-string is not included in BNF of Compact-value, but
      // some service provider might still pass a less-strict text form and
      // cause a unexpected CodeError raised. For example, the `start`
      // parameter expects its value of Text-value, but service provider might
      // gives "<smil>", which contains illegal characters "<" and ">".
      value = decodeAlternatives(data, null,
                                 param.coder, TextValue, TextString);
    } catch (e) {
      data.offset = begin;

      // Skip current parameter.
      value = skipValue(data);
      debug("Skip malformed typed parameter: "
            + JSON.stringify({name: param.name, value: value}));

      return null;
    }

    return {
      name: param.name,
      value: value,
    };
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded object containing `name` and `value` properties or null
   *         if something wrong. The `name` property must be a string, but the
   *         `value` property can be many different types depending on `name`.
   */
  decodeUntypedParameter: function decodeUntypedParameter(data) {
    let name = TokenText.decode(data);

    let begin = data.offset, value;
    try {
      value = decodeAlternatives(data, null, IntegerValue, TextValue);
    } catch (e) {
      data.offset = begin;

      // Skip current parameter.
      value = skipValue(data);
      debug("Skip malformed untyped parameter: "
            + JSON.stringify({name: name, value: value}));

      return null;
    }

    return {
      name: name.toLowerCase(),
      value: value,
    };
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded object containing `name` and `value` properties or null
   *         if something wrong. The `name` property must be a string, but the
   *         `value` property can be many different types depending on `name`.
   */
  decode: function decode(data) {
    let begin = data.offset;
    try {
      return this.decodeTypedParameter(data);
    } catch (e) {
      data.offset = begin;
      return this.decodeUntypedParameter(data);
    }
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   * @param end
   *        Ending offset of following parameters.
   *
   * @return An array of decoded objects.
   */
  decodeMultiple: function decodeMultiple(data, end) {
    let params = null, param;

    while (data.offset < end) {
      try {
        param = this.decode(data);
      } catch (e) {
        break;
      }
      if (param) {
        if (!params) {
          params = {};
        }
        params[param.name] = param.value;
      }
    }

    return params;
  },
};

/**
 * Header = Message-header | Shift-sequence
 * Message-header = Well-known-header | Application-header
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.6
 */
let Header = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded object containing `name` and `value` properties or null
   *         in case of a failed parsing. The `name` property must be a string,
   *         but the `value` property can be many different types depending on
   *         `name`.
   */
  decodeMessageHeader: function decodeMessageHeader(data) {
    return decodeAlternatives(data, null, WellKnownHeader, ApplicationHeader);
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded object containing `name` and `value` properties or null
   *         in case of a failed parsing. The `name` property must be a string,
   *         but the `value` property can be many different types depending on
   *         `name`.
   */
  decode: function decode(data) {
    // TODO: support header code page shift-sequence
    return this.decodeMessageHeader(data);
  },
};

/**
 * Well-known-header = Well-known-field-name Wap-value
 * Well-known-field-name = Short-integer
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.6
 */
let WellKnownHeader = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded object containing `name` and `value` properties or null
   *         in case of a failed parsing. The `name` property must be a string,
   *         but the `value` property can be many different types depending on
   *         `name`.
   *
   * @throws NotWellKnownEncodingError if decoded well-known header field
   *         number is not registered or supported.
   */
  decode: function decode(data) {
    let index = ShortInteger.decode(data);

    let entry = WSP_HEADER_FIELDS[index];
    if (!entry) {
      throw new NotWellKnownEncodingError(
        "Well-known-header: not well known header " + index);
    }

    let begin = data.offset, value;
    try {
      value = decodeAlternatives(data, null, entry.coder, TextValue);
    } catch (e) {
      data.offset = begin;

      value = skipValue(data);
      debug("Skip malformed well known header(" + index + "): "
            + JSON.stringify({name: entry.name, value: value}));

      return null;
    }

    return {
      name: entry.name,
      value: value,
    };
  },
};

/**
 * Application-header = Token-text Application-specific-value
 * Application-specific-value = Text-string
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.6
 */
let ApplicationHeader = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded object containing `name` and `value` properties or null
   *         in case of a failed parsing. The `name` property must be a string,
   *         but the `value` property can be many different types depending on
   *         `name`.
   */
  decode: function decode(data) {
    let name = TokenText.decode(data);

    let begin = data.offset, value;
    try {
      value = TextString.decode(data);
    } catch (e) {
      data.offset = begin;

      value = skipValue(data);
      debug("Skip malformed application header: "
            + JSON.stringify({name: name, value: value}));

      return null;
    }

    return {
      name: name.toLowerCase(),
      value: value,
    };
  },

  /**
   * @param data
   *        A wrapped object to store encoded raw data.
   * @param header
   *        An object containing two attributes: a string-typed `name` and a
   *        `value` of arbitrary type.
   *
   * @throws CodeError if got an empty header name.
   */
  encode: function encode(data, header) {
    if (!header.name) {
      throw new CodeError("Application-header: empty header name");
    }

    TokenText.encode(data, header.name);
    TextString.encode(data, header.value);
  },
};

/**
 * Field-name = Token-text | Well-known-field-name
 * Well-known-field-name = Short-integer
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.6
 */
let FieldName = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A field name string.
   *
   * @throws NotWellKnownEncodingError if decoded well-known header field
   *         number is not registered or supported.
   */
  decode: function decode(data) {
    let begin = data.offset;
    try {
      return TokenText.decode(data).toLowerCase();
    } catch (e) {}

    data.offset = begin;

    let number = ShortInteger.decode(data);
    let entry = WSP_HEADER_FIELDS[number];
    if (!entry) {
      throw new NotWellKnownEncodingError(
        "Field-name: not well known encoding " + number);
    }

    return entry.name;
  },
};

/**
 * Accept-charset-value = Constrained-charset | Accept-charset-general-form
 * Constrained-charset = Any-charset | Constrained-encoding
 * Any-charset = <Octet 128>
 * Accept-charset-general-form = Value-length (Well-known-charset | Token-text) [Q-value]
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.8
 */
let AcceptCharsetValue = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A object with a property `charset` of string "*".
   */
  decodeAnyCharset: function decodeAnyCharset(data) {
    Octet.decodeEqualTo(data, 128);
    return {charset: "*"};
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A object with a string property `charset` and a optional integer
   *         property `q`.
   *
   * @throws NotWellKnownEncodingError if decoded well-known charset number is
   *         not registered or supported.
   */
  decodeConstrainedCharset: function decodeConstrainedCharset(data) {
    let begin = data.offset;
    try {
      return this.decodeAnyCharset(data);
    } catch (e) {}

    data.offset = begin;

    let numOrStr = ConstrainedEncoding.decode(data);
    if (typeof numOrStr == "string") {
      return {charset: numOrStr};
    }

    let charset = numOrStr;
    let entry = WSP_WELL_KNOWN_CHARSETS[charset];
    if (!entry) {
      throw new NotWellKnownEncodingError(
        "Constrained-charset: not well known charset: " + charset);
    }

    return {charset: entry.name};
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A object with a string property `charset` and a optional integer
   *         property `q`.
   */
  decodeAcceptCharsetGeneralForm: function decodeAcceptCharsetGeneralForm(data) {
    let length = ValueLength.decode(data);

    let begin = data.offset;
    let end = begin + length;

    let result;
    try {
      result = WellKnownCharset.decode(data);
    } catch (e) {
      data.offset = begin;

      result = {charset: TokenText.decode(data)};
      if (data.offset < end) {
        result.q = QValue.decode(data);
      }
    }

    if (data.offset != end) {
      data.offset = end;
    }

    return result;
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A object with a string property `charset` and a optional integer
   *         property `q`.
   */
  decode: function decode(data) {
    let begin = data.offset;
    try {
      return this.decodeConstrainedCharset(data);
    } catch (e) {
      data.offset = begin;
      return this.decodeAcceptCharsetGeneralForm(data);
    }
  },
};

/**
 * Well-known-charset = Any-charset | Integer-value
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.8
 */
let WellKnownCharset = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A object with a string property `charset`.
   *
   * @throws CodeError if decoded charset number is an array.
   * @throws NotWellKnownEncodingError if decoded well-known charset number
   *         is not registered or supported.
   */
  decode: function decode(data) {
    let begin = data.offset;

    try {
      return AcceptCharsetValue.decodeAnyCharset(data);
    } catch (e) {}

    data.offset = begin;

    // `IntegerValue.decode` can return a array, which doesn't apply here.
    let numOrArray = IntegerValue.decode(data);
    if (typeof numOrArray != "number") {
      throw new CodeError("Well-known-charset: invalid integer type");
    }

    let charset = numOrArray;
    let entry = WSP_WELL_KNOWN_CHARSETS[charset];
    if (!entry) {
      throw new NotWellKnownEncodingError(
        "Well-known-charset: not well known charset " + charset);
    }

    return {charset: entry.name};
  },
};

/**
 * The short form of the Content-type-value MUST only be used when the
 * well-known media is in the range of 0-127 or a text string. In all other
 * cases the general form MUST be used.
 *
 *   Content-type-value = Constrained-media | Content-general-form
 *   Constrained-media = Constrained-encoding
 *   Content-general-form = Value-length Media-type
 *   Media-type = Media *(Parameter)
 *   Media = Well-known-media | Extension-Media
 *   Well-known-media = Integer-value
 *   Extension-Media = *TEXT End-of-string
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.24
 */
let ContentTypeValue = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded object containing `media` and `params` properties or
   *         null in case of a failed parsing. The `media` property must be a
   *         string, and the `params` property is always null.
   *
   * @throws NotWellKnownEncodingError if decoded well-known content type number
   *         is not registered or supported.
   */
  decodeConstrainedMedia: function decodeConstrainedMedia(data) {
    let numOrStr = ConstrainedEncoding.decode(data);
    if (typeof numOrStr == "string") {
      return {
        media: numOrStr.toLowerCase(),
        params: null,
      };
    }

    let number = numOrStr;
    let entry = WSP_WELL_KNOWN_CONTENT_TYPES[number];
    if (!entry) {
      throw new NotWellKnownEncodingError(
        "Constrained-media: not well known media " + number);
    }

    return {
      media: entry.type,
      params: null,
    };
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decode string.
   *
   * @throws CodeError if decoded content type number is an array.
   * @throws NotWellKnownEncodingError if decoded well-known content type
   *         number is not registered or supported.
   */
  decodeMedia: function decodeMedia(data) {
    let begin = data.offset, number;
    try {
      number = IntegerValue.decode(data);
    } catch (e) {
      data.offset = begin;
      return NullTerminatedTexts.decode(data).toLowerCase();
    }

    // `decodeIntegerValue` can return a array, which doesn't apply here.
    if (typeof number != "number") {
      throw new CodeError("Media: invalid integer type");
    }

    let entry = WSP_WELL_KNOWN_CONTENT_TYPES[number];
    if (!entry) {
      throw new NotWellKnownEncodingError("Media: not well known media " + number);
    }

    return entry.type;
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   * @param end
   *        Ending offset of the Media-type value.
   *
   * @return A decoded object containing `media` and `params` properties or
   *         null in case of a failed parsing. The `media` property must be a
   *         string, and the `params` property is a hash map from a string to
   *         an value of unspecified type.
   */
  decodeMediaType: function decodeMediaType(data, end) {
    let media = this.decodeMedia(data);
    let params = Parameter.decodeMultiple(data, end);

    return {
      media: media,
      params: params,
    };
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded object containing `media` and `params` properties or
   *         null in case of a failed parsing. The `media` property must be a
   *         string, and the `params` property is null or a hash map from a
   *         string to an value of unspecified type.
   */
  decodeContentGeneralForm: function decodeContentGeneralForm(data) {
    let length = ValueLength.decode(data);
    let end = data.offset + length;

    let value = this.decodeMediaType(data, end);

    if (data.offset != end) {
      data.offset = end;
    }

    return value;
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return A decoded object containing `media` and `params` properties or
   *         null in case of a failed parsing. The `media` property must be a
   *         string, and the `params` property is null or a hash map from a
   *         string to an value of unspecified type.
   */
  decode: function decode(data) {
    let begin = data.offset;

    try {
      return this.decodeConstrainedMedia(data);
    } catch (e) {
      data.offset = begin;
      return this.decodeContentGeneralForm(data);
    }
  },
};

/**
 * Application-id-value = Uri-value | App-assigned-code
 * App-assigned-code = Integer-value
 *
 * @see WAP-230-WSP-20010705-a clause 8.4.2.54
 */
let ApplicationIdValue = {
  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return Decoded string value.
   *
   * @throws CodeError if decoded application id number is an array.
   * @throws NotWellKnownEncodingError if decoded well-known application id
   *         number is not registered or supported.
   */
  decode: function decode(data) {
    let begin = data.offset;
    try {
      return UriValue.decode(data);
    } catch (e) {}

    data.offset = begin;

    // `decodeIntegerValue` can return a array, which doesn't apply here.
    let numOrArray = IntegerValue.decode(data);
    if (typeof numOrArray != "number") {
      throw new CodeError("Application-id-value: invalid integer type");
    }

    let id = numOrArray;
    let entry = OMNA_PUSH_APPLICATION_IDS[id];
    if (!entry) {
      throw new NotWellKnownEncodingError(
        "Application-id-value: not well known id: " + id);
    }

    return entry.urn;
  },
};

let PduHelper = {
  /**
   * Parse multiple header fields with end mark.
   *
   * @param data
   *        A wrapped object containing raw PDU data.
   * @param end
   *        An ending offset indicating the end of headers.
   * @param headers [optional]
   *        An optional object to store parsed header fields. Created
   *        automatically if undefined.
   *
   * @return A object containing decoded header fields as its attributes.
   */
  parseHeaders: function parseHeaders(data, end, headers) {
    if (!headers) {
      headers = {};
    }

    let header;
    while (data.offset < end) {
      try {
        header = Header.decode(data);
      } catch (e) {
        break;
      }
      if (header) {
        headers[header.name] = header.value;
      }
    }

    if (data.offset != end) {
      debug("Parser expects ending in " + end + ", but in " + data.offset);
      // Explicitly seek to end in case of skipped header fields.
      data.offset = end;
    }

    return headers;
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   * @param msg
   *        Message object to be populated with decoded header fields.
   *
   * @see WAP-230-WSP-20010705-a clause 8.2.4
   */
  parsePushHeaders: function parsePushHeaders(data, msg) {
    if (!msg.headers) {
      msg.headers = {};
    }

    let headersLen = UintVar.decode(data);
    let headersEnd = data.offset + headersLen;

    let contentType = ContentTypeValue.decode(data);
    msg.headers["content-type"] = contentType;

    msg.headers = this.parseHeaders(data, headersEnd, msg.headers);
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   *
   * @return An array of objects representing multipart entries or null in case
   *         of errors found.
   *
   * @see WAP-230-WSP-20010705-a section 8.5
   */
  parseMultiPart: function parseMultiPart(data) {
    let nEntries = UintVar.decode(data);
    if (!nEntries) {
      return null;
    }

    let parts = new Array(nEntries);
    for (let i = 0; i < nEntries; i++) {
      // Length of the ContentType and Headers fields combined.
      let headersLen = UintVar.decode(data);
      // Length of the Data field
      let contentLen = UintVar.decode(data);

      let headersEnd = data.offset + headersLen;
      let contentEnd = headersEnd + contentLen;

      try {
        let headers = {};

        let contentType = ContentTypeValue.decode(data);
        headers["content-type"] = contentType;
        headers["content-length"] = contentLen;

        headers = this.parseHeaders(data, headersEnd, headers);

        let content = Octet.decodeMultiple(data, contentEnd);

        parts[i] = {
          index: i,
          headers: headers,
          content: content,
        };
      } catch (e) {
        debug("Failed to parse multipart entry, message: " + e.message);
        // Placeholder to keep original index of following entries.
        parts[i] = null;
      }

      if (data.offset != contentEnd) {
        // Seek to entry boundary for next entry.
        data.offset = contentEnd;
      }
    }

    return parts;
  },

  /**
   * @param data
   *        A wrapped object containing raw PDU data.
   * @param isSessionless
   *        Whether or not the PDU contains a session less WSP PDU.
   * @param msg [optional]
   *        Optional pre-defined PDU object.
   *
   * @return Parsed WSP PDU object or null in case of errors found.
   */
  parse: function parse(data, isSessionless, msg) {
    if (!msg) {
      msg = {
        type: null,
      };
    }

    try {
      if (isSessionless) {
        // "The `transactionId` is used to associate requests with replies in
        // the connectionless session service." ~ WAP-230-WSP-20010705-a 8.2.1
        msg.transactionId = Octet.decode(data);
      }

      msg.type = Octet.decode(data);
      switch (msg.type) {
        case WSP_PDU_TYPE_PUSH:
          this.parsePushHeaders(data, msg);
          break;
      }
    } catch (e) {
      debug("Parse error. Message: " + e.message);
      msg = null;
    }

    return msg;
  },
};

// WSP Header Field Name Assignments
// Note: Items commented out are either deprecated or not implemented.
//       Deprecated items should only be supported for backward compatibility
//       purpose.
// @see WAP-230-WSP-20010705-a Appendix A. Assigned Numbers.
const WSP_HEADER_FIELDS = (function () {
  let names = {};
  function add(name, number, coder) {
    let entry = {
      name: name,
      number: number,
      coder: coder,
    };
    names[name] = names[number] = entry;
  }

  //add("accept",               0x00);
  //add("accept-charset",       0x01); Deprecated
  //add("accept-encoding",      0x02); Deprecated
  //add("accept-language",      0x03);
  //add("accept-ranges",        0x04);
  add("age",                    0x05, DeltaSecondsValue);
  //add("allow",                0x06);
  //add("authorization",        0x07);
  //add("cache-control",        0x08); Deprecated
  //add("connection",           0x09);
  //add("content-base",         0x0A); Deprecated
  //add("content-encoding",     0x0B);
  //add("content-language",     0x0C);
  add("content-length",         0x0D, IntegerValue);
  add("content-location",       0x0E, UriValue);
  //add("content-md5",          0x0F);
  //add("content-range",        0x10); Deprecated
  add("content-type",           0x11, ContentTypeValue);
  add("date",                   0x12, DateValue);
  add("etag",                   0x13, TextString);
  add("expires",                0x14, DateValue);
  add("from",                   0x15, TextString);
  add("host",                   0x16, TextString);
  add("if-modified-since",      0x17, DateValue);
  add("if-match",               0x18, TextString);
  add("if-none-match",          0x19, TextString);
  //add("if-range",             0x1A);
  add("if-unmodified-since",    0x1B, DateValue);
  add("location",               0x1C, UriValue);
  add("last-modified",          0x1D, DateValue);
  add("max-forwards",           0x1E, IntegerValue);
  //add("pragma",               0x1F);
  //add("proxy-authenticate",   0x20);
  //add("proxy-authentication", 0x21);
  //add("public",               0x22);
  //add("range",                0x23);
  add("referer",                0x24, UriValue);
  //add("retry-after",          0x25);
  add("server",                 0x26, TextString);
  //add("transfer-encoding",    0x27);
  add("upgrade",                0x28, TextString);
  add("user-agent",             0x29, TextString);
  //add("vary",                 0x2A);
  add("via",                    0x2B, TextString);
  //add("warning",              0x2C);
  //add("www-authenticate",     0x2D);
  //add("content-disposition",  0x2E); Deprecated
  add("x-wap-application-id",   0x2F, ApplicationIdValue);
  add("x-wap-content-uri",      0x30, UriValue);
  add("x-wap-initiator-uri",    0x31, UriValue);
  //add("accept-application",   0x32);
  add("bearer-indication",      0x33, IntegerValue);
  add("push-flag",              0x34, ShortInteger);
  add("profile",                0x35, UriValue);
  //add("profile-diff",         0x36);
  //add("profile-warning",      0x37); Deprecated
  //add("expect",               0x38);
  //add("te",                   0x39);
  //add("trailer",              0x3A);
  add("accept-charset",         0x3B, AcceptCharsetValue);
  //add("accept-encoding",      0x3C);
  //add("cache-control",        0x3D); Deprecated
  //add("content-range",        0x3E);
  add("x-wap-tod",              0x3F, DateValue);
  add("content-id",             0x40, QuotedString);
  //add("set-cookie",           0x41);
  //add("cookie",               0x42);
  //add("encoding-version",     0x43);
  //add("profile-warning",      0x44);
  //add("content-disposition",  0x45);
  //add("x-wap-security",       0x46);
  //add("cache-control",        0x47);

  return names;
})();

// WSP Content Type Assignments
// @see http://www.wapforum.org/wina
const WSP_WELL_KNOWN_CONTENT_TYPES = (function () {
  let types = {};

  function add(type, number) {
    let entry = {
      type: type,
      number: number,
    };
    types[type] = types[number] = entry;
  }

  // Well Known Values
  add("application/vnd.wap.multipart.related", 0x33);
  add("application/vnd.wap.mms-message", 0x3E);

  return types;
})();

// WSP Well-Known Parameter Assignments
// Note: Items commented out are either deprecated or not implemented.
//       Deprecated items should not be used.
// @see WAP-230-WSP-20010705-a Appendix A. Assigned Numbers.
const WSP_WELL_KNOWN_PARAMS = (function () {
  let params = {};

  function add(name, number, coder) {
    let entry = {
      name: name,
      number: number,
      coder: coder,
    };
    params[name] = params[number] = entry;
  }

  add("q",                 0x00, QValue);
  add("charset",           0x01, WellKnownCharset);
  add("level",             0x02, VersionValue);
  add("type",              0x03, IntegerValue);
  add("name",              0x05, TextValue); // Deprecated, but used in some carriers, eg. Hinet.
  //add("filename",        0x06); Deprecated
  add("differences",       0x07, FieldName);
  add("padding",           0x08, ShortInteger);
  add("type",              0x09, ConstrainedEncoding);
  add("start",             0x0A, TextValue); // Deprecated, but used in some carriers, eg. T-Mobile.
  //add("start-info",      0x0B); Deprecated
  //add("comment",         0x0C); Deprecated
  //add("domain",          0x0D); Deprecated
  add("max-age",           0x0E, DeltaSecondsValue);
  //add("path",            0x0F); Deprecated
  add("secure",            0x10, NoValue);
  add("sec",               0x11, ShortInteger);
  add("mac",               0x12, TextValue);
  add("creation-date",     0x13, DateValue);
  add("modification-date", 0x14, DateValue);
  add("read-date",         0x15, DateValue);
  add("size",              0x16, IntegerValue);
  add("name",              0x17, TextValue);
  add("filename",          0x18, TextValue);
  add("start",             0x19, TextValue);
  add("start-info",        0x1A, TextValue);
  add("comment",           0x1B, TextValue);
  add("domain",            0x1C, TextValue);
  add("path",              0x1D, TextValue);

  return params;
})();

// WSP Character Set Assignments
// @see WAP-230-WSP-20010705-a Appendix A. Assigned Numbers.
// @see http://www.iana.org/assignments/character-sets
const WSP_WELL_KNOWN_CHARSETS = (function () {
  let charsets = {};

  function add(name, number, converter) {
    let entry = {
      name: name,
      number: number,
      converter: converter,
    };

    charsets[name] = charsets[number] = entry;
  }

  add("ansi_x3.4-1968",     3, null);
  add("iso_8859-1:1987",    4, "ISO-8859-1");
  add("utf-8",            106, "UTF-8");
  add("windows-1252",    2252, "windows-1252");

  return charsets;
})();

// OMNA PUSH Application ID
// @see http://www.openmobilealliance.org/tech/omna/omna-push-app-id.aspx
const OMNA_PUSH_APPLICATION_IDS = (function () {
  let ids = {};

  function add(urn, number) {
    let entry = {
      urn: urn,
      number: number,
    };

    ids[urn] = ids[number] = entry;
  }

  add("x-wap-application:mms.ua", 0x04);

  return ids;
})();

let debug;
if (DEBUG) {
  debug = function (s) {
    dump("-@- WspPduHelper: " + s + "\n");
  };
} else {
  debug = function (s) {};
}

const EXPORTED_SYMBOLS = ALL_CONST_SYMBOLS.concat([
  // Constant values
  "WSP_HEADER_FIELDS",
  "WSP_WELL_KNOWN_CONTENT_TYPES",
  "WSP_WELL_KNOWN_PARAMS",
  "WSP_WELL_KNOWN_CHARSETS",
  "OMNA_PUSH_APPLICATION_IDS",

  // Error classes
  "CodeError",
  "FatalCodeError",
  "NotWellKnownEncodingError",

  // Utility functions
  "ensureHeader",
  "skipValue",
  "decodeAlternatives",
  "encodeAlternatives",

  // Decoders
  "Octet",
  "Text",
  "NullTerminatedTexts",
  "Token",
  "URIC",
  "TextString",
  "TokenText",
  "QuotedString",
  "ShortInteger",
  "LongInteger",
  "UintVar",
  "ConstrainedEncoding",
  "ValueLength",
  "NoValue",
  "TextValue",
  "IntegerValue",
  "DateValue",
  "DeltaSecondsValue",
  "QValue",
  "VersionValue",
  "UriValue",
  "Parameter",
  "Header",
  "WellKnownHeader",
  "ApplicationHeader",
  "FieldName",
  "AcceptCharsetValue",
  "WellKnownCharset",
  "ContentTypeValue",
  "ApplicationIdValue",

  // Parser
  "PduHelper",
]);

