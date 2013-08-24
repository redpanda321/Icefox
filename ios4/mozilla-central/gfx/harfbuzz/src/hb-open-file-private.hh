/*
 * Copyright (C) 2007,2008,2009  Red Hat, Inc.
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
 */

#ifndef HB_OPEN_FILE_PRIVATE_HH
#define HB_OPEN_FILE_PRIVATE_HH

#include "hb-open-type-private.hh"


/*
 *
 * The OpenType Font File
 *
 */


/*
 * Organization of an OpenType Font
 */

struct OpenTypeFontFile;
struct OffsetTable;
struct TTCHeader;


typedef struct TableDirectory
{
  inline bool sanitize (hb_sanitize_context_t *c) {
    TRACE_SANITIZE ();
    return c->check_struct (this);
  }

  Tag		tag;		/* 4-byte identifier. */
  CheckSum	checkSum;	/* CheckSum for this table. */
  ULONG		offset;		/* Offset from beginning of TrueType font
				 * file. */
  ULONG		length;		/* Length of this table. */
  public:
  DEFINE_SIZE_STATIC (16);
} OpenTypeTable;

typedef struct OffsetTable
{
  friend struct OpenTypeFontFile;

  inline unsigned int get_table_count (void) const
  { return numTables; }
  inline const TableDirectory& get_table (unsigned int i) const
  {
    if (unlikely (i >= numTables)) return Null(TableDirectory);
    return tableDir[i];
  }
  inline bool find_table_index (hb_tag_t tag, unsigned int *table_index) const
  {
    Tag t;
    t.set (tag);
    /* TODO: bsearch (need to sort in sanitize) */
    unsigned int count = numTables;
    for (unsigned int i = 0; i < count; i++)
    {
      if (t == tableDir[i].tag)
      {
        if (table_index) *table_index = i;
        return true;
      }
    }
    if (table_index) *table_index = Index::NOT_FOUND_INDEX;
    return false;
  }
  inline const TableDirectory& get_table_by_tag (hb_tag_t tag) const
  {
    unsigned int table_index;
    find_table_index (tag, &table_index);
    return get_table (table_index);
  }

  public:
  inline bool sanitize (hb_sanitize_context_t *c) {
    TRACE_SANITIZE ();
    return c->check_struct (this)
	&& c->check_array (tableDir, TableDirectory::static_size, numTables);
  }

  private:
  Tag		sfnt_version;	/* '\0\001\0\00' if TrueType / 'OTTO' if CFF */
  USHORT	numTables;	/* Number of tables. */
  USHORT	searchRange;	/* (Maximum power of 2 <= numTables) x 16 */
  USHORT	entrySelector;	/* Log2(maximum power of 2 <= numTables). */
  USHORT	rangeShift;	/* NumTables x 16-searchRange. */
  TableDirectory tableDir[VAR];	/* TableDirectory entries. numTables items */
  public:
  DEFINE_SIZE_ARRAY (12, tableDir);
} OpenTypeFontFace;


/*
 * TrueType Collections
 */

struct TTCHeaderVersion1
{
  friend struct TTCHeader;

  inline unsigned int get_face_count (void) const { return table.len; }
  inline const OpenTypeFontFace& get_face (unsigned int i) const { return this+table[i]; }

  inline bool sanitize (hb_sanitize_context_t *c) {
    TRACE_SANITIZE ();
    return table.sanitize (c, this);
  }

  private:
  Tag		ttcTag;		/* TrueType Collection ID string: 'ttcf' */
  FixedVersion	version;	/* Version of the TTC Header (1.0),
				 * 0x00010000 */
  LongOffsetLongArrayOf<OffsetTable>
		table;		/* Array of offsets to the OffsetTable for each font
				 * from the beginning of the file */
  public:
  DEFINE_SIZE_ARRAY (12, table);
};

struct TTCHeader
{
  friend struct OpenTypeFontFile;

  private:

  inline unsigned int get_face_count (void) const
  {
    switch (u.header.version) {
    case 2: /* version 2 is compatible with version 1 */
    case 1: return u.version1.get_face_count ();
    default:return 0;
    }
  }
  inline const OpenTypeFontFace& get_face (unsigned int i) const
  {
    switch (u.header.version) {
    case 2: /* version 2 is compatible with version 1 */
    case 1: return u.version1.get_face (i);
    default:return Null(OpenTypeFontFace);
    }
  }

  inline bool sanitize (hb_sanitize_context_t *c) {
    TRACE_SANITIZE ();
    if (unlikely (!u.header.version.sanitize (c))) return false;
    switch (u.header.version) {
    case 2: /* version 2 is compatible with version 1 */
    case 1: return u.version1.sanitize (c);
    default:return true;
    }
  }

  private:
  union {
  struct {
  Tag		ttcTag;		/* TrueType Collection ID string: 'ttcf' */
  FixedVersion	version;	/* Version of the TTC Header (1.0 or 2.0),
				 * 0x00010000 or 0x00020000 */
  }			header;
  TTCHeaderVersion1	version1;
  } u;
};


/*
 * OpenType Font File
 */

struct OpenTypeFontFile
{
  static const hb_tag_t CFFTag		= HB_TAG ('O','T','T','O'); /* OpenType with Postscript outlines */
  static const hb_tag_t TrueTypeTag	= HB_TAG ( 0 , 1 , 0 , 0 ); /* OpenType with TrueType outlines */
  static const hb_tag_t TTCTag		= HB_TAG ('t','t','c','f'); /* TrueType Collection */
  static const hb_tag_t TrueTag		= HB_TAG ('t','r','u','e'); /* Obsolete Apple TrueType */
  static const hb_tag_t Typ1Tag		= HB_TAG ('t','y','p','1'); /* Obsolete Apple Type1 font in SFNT container */

  inline hb_tag_t get_tag (void) const { return u.tag; }

  inline unsigned int get_face_count (void) const
  {
    switch (u.tag) {
    case CFFTag:	/* All the non-collection tags */
    case TrueTag:
    case Typ1Tag:
    case TrueTypeTag:	return 1;
    case TTCTag:	return u.ttcHeader.get_face_count ();
    default:		return 0;
    }
  }
  inline const OpenTypeFontFace& get_face (unsigned int i) const
  {
    switch (u.tag) {
    /* Note: for non-collection SFNT data we ignore index.  This is because
     * Apple dfont container is a container of SFNT's.  So each SFNT is a
     * non-TTC, but the index is more than zero. */
    case CFFTag:	/* All the non-collection tags */
    case TrueTag:
    case Typ1Tag:
    case TrueTypeTag:	return u.fontFace;
    case TTCTag:	return u.ttcHeader.get_face (i);
    default:		return Null(OpenTypeFontFace);
    }
  }

  inline bool sanitize (hb_sanitize_context_t *c) {
    TRACE_SANITIZE ();
    if (unlikely (!u.tag.sanitize (c))) return false;
    switch (u.tag) {
    case CFFTag:	/* All the non-collection tags */
    case TrueTag:
    case Typ1Tag:
    case TrueTypeTag:	return u.fontFace.sanitize (c);
    case TTCTag:	return u.ttcHeader.sanitize (c);
    default:		return true;
    }
  }

  private:
  union {
  Tag			tag;		/* 4-byte identifier. */
  OpenTypeFontFace	fontFace;
  TTCHeader		ttcHeader;
  } u;
  public:
  DEFINE_SIZE_UNION (4, tag);
};


#endif /* HB_OPEN_FILE_PRIVATE_HH */
