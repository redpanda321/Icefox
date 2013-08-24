/*
 * Copyright © 2009  Red Hat, Inc.
 * Copyright © 2009  Keith Stribley
 * Copyright © 2011  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Red Hat Author(s): Behdad Esfahbod
 * Google Author(s): Behdad Esfahbod
 */

#include "hb-private.hh"

#include "hb-icu.h"

#include "hb-unicode-private.hh"

#include <unicode/uversion.h>
#include <unicode/uchar.h>
#include <unicode/unorm.h>
#include <unicode/ustring.h>



hb_script_t
hb_icu_script_to_script (UScriptCode script)
{
  if (unlikely (script == USCRIPT_INVALID_CODE))
    return HB_SCRIPT_INVALID;

  return hb_script_from_string (uscript_getShortName (script), -1);
}

UScriptCode
hb_icu_script_from_script (hb_script_t script)
{
  if (unlikely (script == HB_SCRIPT_INVALID))
    return USCRIPT_INVALID_CODE;

  for (unsigned int i = 0; i < USCRIPT_CODE_LIMIT; i++)
    if (unlikely (hb_icu_script_to_script ((UScriptCode) i) == script))
      return (UScriptCode) i;

  return USCRIPT_UNKNOWN;
}


static unsigned int
hb_icu_unicode_combining_class (hb_unicode_funcs_t *ufuncs HB_UNUSED,
				hb_codepoint_t      unicode,
				void               *user_data HB_UNUSED)

{
  return u_getCombiningClass (unicode);
}

static unsigned int
hb_icu_unicode_eastasian_width (hb_unicode_funcs_t *ufuncs HB_UNUSED,
				hb_codepoint_t      unicode,
				void               *user_data HB_UNUSED)
{
  switch (u_getIntPropertyValue(unicode, UCHAR_EAST_ASIAN_WIDTH))
  {
  case U_EA_WIDE:
  case U_EA_FULLWIDTH:
    return 2;
  case U_EA_NEUTRAL:
  case U_EA_AMBIGUOUS:
  case U_EA_HALFWIDTH:
  case U_EA_NARROW:
    return 1;
  }
  return 1;
}

static hb_unicode_general_category_t
hb_icu_unicode_general_category (hb_unicode_funcs_t *ufuncs HB_UNUSED,
				 hb_codepoint_t      unicode,
				 void               *user_data HB_UNUSED)
{
  switch (u_getIntPropertyValue(unicode, UCHAR_GENERAL_CATEGORY))
  {
  case U_UNASSIGNED:			return HB_UNICODE_GENERAL_CATEGORY_UNASSIGNED;

  case U_UPPERCASE_LETTER:		return HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER;
  case U_LOWERCASE_LETTER:		return HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER;
  case U_TITLECASE_LETTER:		return HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER;
  case U_MODIFIER_LETTER:		return HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER;
  case U_OTHER_LETTER:			return HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER;

  case U_NON_SPACING_MARK:		return HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK;
  case U_ENCLOSING_MARK:		return HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK;
  case U_COMBINING_SPACING_MARK:	return HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK;

  case U_DECIMAL_DIGIT_NUMBER:		return HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER;
  case U_LETTER_NUMBER:			return HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER;
  case U_OTHER_NUMBER:			return HB_UNICODE_GENERAL_CATEGORY_OTHER_NUMBER;

  case U_SPACE_SEPARATOR:		return HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR;
  case U_LINE_SEPARATOR:		return HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR;
  case U_PARAGRAPH_SEPARATOR:		return HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR;

  case U_CONTROL_CHAR:			return HB_UNICODE_GENERAL_CATEGORY_CONTROL;
  case U_FORMAT_CHAR:			return HB_UNICODE_GENERAL_CATEGORY_FORMAT;
  case U_PRIVATE_USE_CHAR:		return HB_UNICODE_GENERAL_CATEGORY_PRIVATE_USE;
  case U_SURROGATE:			return HB_UNICODE_GENERAL_CATEGORY_SURROGATE;


  case U_DASH_PUNCTUATION:		return HB_UNICODE_GENERAL_CATEGORY_DASH_PUNCTUATION;
  case U_START_PUNCTUATION:		return HB_UNICODE_GENERAL_CATEGORY_OPEN_PUNCTUATION;
  case U_END_PUNCTUATION:		return HB_UNICODE_GENERAL_CATEGORY_CLOSE_PUNCTUATION;
  case U_CONNECTOR_PUNCTUATION:		return HB_UNICODE_GENERAL_CATEGORY_CONNECT_PUNCTUATION;
  case U_OTHER_PUNCTUATION:		return HB_UNICODE_GENERAL_CATEGORY_OTHER_PUNCTUATION;

  case U_MATH_SYMBOL:			return HB_UNICODE_GENERAL_CATEGORY_MATH_SYMBOL;
  case U_CURRENCY_SYMBOL:		return HB_UNICODE_GENERAL_CATEGORY_CURRENCY_SYMBOL;
  case U_MODIFIER_SYMBOL:		return HB_UNICODE_GENERAL_CATEGORY_MODIFIER_SYMBOL;
  case U_OTHER_SYMBOL:			return HB_UNICODE_GENERAL_CATEGORY_OTHER_SYMBOL;

  case U_INITIAL_PUNCTUATION:		return HB_UNICODE_GENERAL_CATEGORY_INITIAL_PUNCTUATION;
  case U_FINAL_PUNCTUATION:		return HB_UNICODE_GENERAL_CATEGORY_FINAL_PUNCTUATION;
  }

  return HB_UNICODE_GENERAL_CATEGORY_UNASSIGNED;
}

static hb_codepoint_t
hb_icu_unicode_mirroring (hb_unicode_funcs_t *ufuncs HB_UNUSED,
			  hb_codepoint_t      unicode,
			  void               *user_data HB_UNUSED)
{
  return u_charMirror(unicode);
}

static hb_script_t
hb_icu_unicode_script (hb_unicode_funcs_t *ufuncs HB_UNUSED,
		       hb_codepoint_t      unicode,
		       void               *user_data HB_UNUSED)
{
  UErrorCode status = U_ZERO_ERROR;
  UScriptCode scriptCode = uscript_getScript(unicode, &status);

  if (unlikely (U_FAILURE (status)))
    return HB_SCRIPT_UNKNOWN;

  return hb_icu_script_to_script (scriptCode);
}

static hb_bool_t
hb_icu_unicode_compose (hb_unicode_funcs_t *ufuncs HB_UNUSED,
			hb_codepoint_t      a,
			hb_codepoint_t      b,
			hb_codepoint_t     *ab,
			void               *user_data HB_UNUSED)
{
  if (!a || !b)
    return FALSE;

  UChar utf16[4], normalized[5];
  int len;
  hb_bool_t ret, err;
  UErrorCode icu_err;

  len = 0;
  err = FALSE;
  U16_APPEND (utf16, len, ARRAY_LENGTH (utf16), a, err);
  if (err) return FALSE;
  U16_APPEND (utf16, len, ARRAY_LENGTH (utf16), b, err);
  if (err) return FALSE;

  icu_err = U_ZERO_ERROR;
  len = unorm_normalize (utf16, len, UNORM_NFC, 0, normalized, ARRAY_LENGTH (normalized), &icu_err);
  if (U_FAILURE (icu_err))
    return FALSE;
  if (u_countChar32 (normalized, len) == 1) {
    U16_GET_UNSAFE (normalized, 0, *ab);
    ret = TRUE;
  } else {
    ret = FALSE;
  }

  return ret;
}

static hb_bool_t
hb_icu_unicode_decompose (hb_unicode_funcs_t *ufuncs HB_UNUSED,
			  hb_codepoint_t      ab,
			  hb_codepoint_t     *a,
			  hb_codepoint_t     *b,
			  void               *user_data HB_UNUSED)
{
  UChar utf16[2], normalized[20];
  int len;
  hb_bool_t ret, err;
  UErrorCode icu_err;

  /* This function is a monster! Maybe it wasn't a good idea adding a
   * pairwise decompose API... */
  /* Watchout for the dragons.  Err, watchout for macros changing len. */

  len = 0;
  err = FALSE;
  U16_APPEND (utf16, len, ARRAY_LENGTH (utf16), ab, err);
  if (err) return FALSE;

  icu_err = U_ZERO_ERROR;
  len = unorm_normalize (utf16, len, UNORM_NFD, 0, normalized, ARRAY_LENGTH (normalized), &icu_err);
  if (U_FAILURE (icu_err))
    return FALSE;

  len = u_countChar32 (normalized, len);

  if (len == 1) {
    U16_GET_UNSAFE (normalized, 0, *a);
    *b = 0;
    ret = *a != ab;
  } else if (len == 2) {
    len =0;
    U16_NEXT_UNSAFE (normalized, len, *a);
    U16_NEXT_UNSAFE (normalized, len, *b);

    /* Here's the ugly part: if ab decomposes to a single character and
     * that character decomposes again, we have to detect that and undo
     * the second part :-(. */
    UChar recomposed[20];
    icu_err = U_ZERO_ERROR;
    unorm_normalize (normalized, len, UNORM_NFC, 0, recomposed, ARRAY_LENGTH (recomposed), &icu_err);
    if (U_FAILURE (icu_err))
      return FALSE;
    hb_codepoint_t c;
    U16_GET_UNSAFE (recomposed, 0, c);
    if (c != *a && c != ab) {
      *a = c;
      *b = 0;
    }
    ret = TRUE;
  } else {
    /* If decomposed to more than two characters, take the last one,
     * and recompose the rest to get the first component. */
    U16_PREV_UNSAFE (normalized, len, *b);
    UChar recomposed[20];
    icu_err = U_ZERO_ERROR;
    len = unorm_normalize (normalized, len, UNORM_NFC, 0, recomposed, ARRAY_LENGTH (recomposed), &icu_err);
    if (U_FAILURE (icu_err))
      return FALSE;
    /* We expect that recomposed has exactly one character now. */
    U16_GET_UNSAFE (recomposed, 0, *a);
    ret = TRUE;
  }

  return ret;
}

extern HB_INTERNAL hb_unicode_funcs_t _hb_unicode_funcs_icu;
hb_unicode_funcs_t _hb_icu_unicode_funcs = {
  HB_OBJECT_HEADER_STATIC,

  NULL, /* parent */
  TRUE, /* immutable */
  {
#define HB_UNICODE_FUNC_IMPLEMENT(name) hb_icu_unicode_##name,
    HB_UNICODE_FUNCS_IMPLEMENT_CALLBACKS
#undef HB_UNICODE_FUNC_IMPLEMENT
  }
};

hb_unicode_funcs_t *
hb_icu_get_unicode_funcs (void)
{
  return &_hb_icu_unicode_funcs;
}


