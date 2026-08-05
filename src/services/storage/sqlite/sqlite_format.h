// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_sqlite_format.h
 * @brief Reader for the SQLite3 on-disk file format (PC_ENABLE_SQLITE).
 *
 * This is **file-format access**, not the SQLite library: the documented database file structure
 * (https://www.sqlite.org/fileformat2.html) parsed by hand - the 100-byte database header, the b-tree
 * page header, the record varint, and record serial types. It is read-first (a bounded writer is a later
 * step), pure (no I/O; you hand it page bytes), and host-testable against real files produced by the
 * sqlite3 CLI. It deliberately does not pull in the SQLite amalgamation, which needs a heap and stdio and
 * does not fit the no-stdlib zero-heap model.
 *
 * A caller reads a page (::SqliteDbHeader::page_size bytes) from the backing store - e.g. via pc_wal_fs /
 * fs::FS - and walks it with these parsers: page 1 begins with the database header (100 bytes) immediately
 * followed by the root b-tree page header of the schema table.
 */

#ifndef PROTOCORE_SQLITE_FORMAT_H
#define PROTOCORE_SQLITE_FORMAT_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SQLITE

/**
 * @brief Decode a SQLite variable-length integer (1-9 bytes, big-endian; the high bit of each of the first
 * 8 bytes is a continuation flag, the 9th byte contributes all 8 bits).
 * @return the number of bytes consumed (1-9), or 0 if @p len is too short for a complete varint.
 */
size_t pc_sqlite_varint_decode(const uint8_t *buf, size_t len, uint64_t *out);

/**
 * @brief Content byte size of a record column with the given record serial type.
 *
 * 0 -> NULL (0), 1..6 -> 1/2/3/4/6/8-byte ints, 7 -> 8-byte float, 8/9 -> the constants 0/1 (0 bytes),
 * N>=12 even -> BLOB of (N-12)/2 bytes, N>=13 odd -> TEXT of (N-13)/2 bytes. Reserved 10/11 -> 0.
 */
uint64_t pc_sqlite_serial_type_size(uint64_t serial_type);

/** @brief Parsed subset of the 100-byte database header (all fields are big-endian on media). */
typedef struct
{
    uint32_t page_size;           ///< 512..65536 (the on-disk value 1 means 65536)
    uint8_t write_version;        ///< 1 = legacy (rollback journal), 2 = WAL
    uint8_t read_version;         ///< 1 = legacy, 2 = WAL
    uint8_t reserved_per_page;    ///< reserved bytes at the end of each page
    uint32_t file_change_counter; ///< bumped on each transaction
    uint32_t page_count;          ///< database size in pages (valid when it matches version-valid-for)
    uint32_t freelist_first;      ///< first freelist trunk page (0 = none)
    uint32_t freelist_count;      ///< number of freelist pages
    uint32_t schema_cookie;       ///< schema version cookie
    uint32_t schema_format;       ///< schema format number (1..4)
    uint32_t text_encoding;       ///< 1 = UTF-8, 2 = UTF-16le, 3 = UTF-16be
    uint32_t user_version;        ///< user_version pragma
    uint32_t application_id;      ///< application_id pragma
    uint32_t pc_sqlite_version;   ///< SQLITE_VERSION_NUMBER that last wrote the file
} SqliteDbHeader;

/**
 * @brief Parse and validate the 100-byte database header at @p buf (@p len must be >= 100).
 * @return false if the magic string is wrong or the page size is not a valid power of two.
 */
proto_bool pc_sqlite_parse_db_header(const uint8_t *buf, size_t len, SqliteDbHeader *out);

/** @brief B-tree page types (the first byte of a b-tree page header). */
// SQLite b-tree page types (the page-header first byte): wire/format values compared, so integer
// constants in a namespacing struct.
#define SQLITE_BTREE_INTERIOR_INDEX 2
#define SQLITE_BTREE_INTERIOR_TABLE 5
#define SQLITE_BTREE_LEAF_INDEX 10
#define SQLITE_BTREE_LEAF_TABLE 13

/** @brief Parsed b-tree page header (8 bytes for a leaf, 12 for an interior page). */
typedef struct
{
    uint8_t type;                ///< one of the SQLITE_BTREE_* values
    uint16_t first_freeblock;    ///< offset of the first freeblock (0 = none)
    uint16_t cell_count;         ///< number of cells on the page
    uint32_t cell_content_start; ///< start of the cell content area (on-disk 0 means 65536)
    uint8_t frag_free_bytes;     ///< fragmented free bytes in the cell content area
    uint32_t right_most_page;    ///< right-most child page (interior pages only; 0 on a leaf)
    uint8_t header_size;         ///< 8 (leaf) or 12 (interior) - where the cell pointer array begins
} SqliteBtreeHeader;

/**
 * @brief Parse a b-tree page header located at @p page + @p offset (@p offset is 100 for page 1, else 0).
 * @param page_len bounds the read. @return false if the page type byte is not a valid b-tree type or the
 * header runs past @p page_len.
 */
proto_bool pc_sqlite_parse_btree_header(const uint8_t *page, size_t page_len, size_t offset, SqliteBtreeHeader *out);

/**
 * @brief In-page byte offset of the @p i-th cell (0-based) from the cell pointer array. @return 0 if @p i is
 * out of range or the pointer array runs past @p page_len. @p page_offset is 100 for page 1, else 0.
 */
uint32_t pc_sqlite_cell_pointer(const uint8_t *page, size_t page_len, const SqliteBtreeHeader *bh, size_t page_offset,
                                uint16_t i);

/** @brief A parsed table-b-tree leaf cell (a table row): its rowid and where its record payload lives. */
typedef struct
{
    uint64_t rowid;
    uint32_t payload_len;    ///< total record payload length
    uint32_t local_off;      ///< in-page offset where the record payload begins
    uint32_t local_len;      ///< payload bytes stored in this page (== payload_len unless it overflows)
    proto_bool has_overflow; ///< true when payload_len > local_len (remainder is on overflow pages)
} SqliteTableLeafCell;

/**
 * @brief Parse the leaf-table cell at in-page offset @p cell_off (payload-length varint, then rowid varint).
 *
 * Computes the local payload extent using the SQLite overflow threshold (@p page_size and @p reserved size
 * the usable area). Reading the overflow-page chain is a follow-up; @c has_overflow tells you when the
 * record is only partially present in this page. @return false on a truncated/invalid cell.
 */
proto_bool pc_sqlite_parse_table_leaf_cell(const uint8_t *page, size_t page_len, uint32_t page_size, uint8_t reserved,
                                           uint32_t cell_off, SqliteTableLeafCell *out);

/** @brief Cursor over the columns of a record (row payload): the header varints and the value bytes. */
typedef struct
{
    const uint8_t *rec;
    uint32_t rec_len;
    uint32_t hdr_pos; ///< offset of the next serial-type varint within the header
    uint32_t hdr_end; ///< end of the record header (start of the value area)
    uint32_t val_pos; ///< offset of the next column value
} SqliteRecordCursor;

/** @brief Begin a record cursor over @p rec_len bytes at @p rec. @return false if the header is malformed. */
proto_bool pc_sqlite_record_begin(SqliteRecordCursor *c, const uint8_t *rec, uint32_t rec_len);

/**
 * @brief Advance to the next column. Sets @p serial_type and points @p val / @p val_len at the value bytes
 * (0-length for NULL and the integer constants 0/1). @return false when there are no more columns.
 */
proto_bool pc_sqlite_record_next(SqliteRecordCursor *c, uint64_t *serial_type, const uint8_t **val, uint32_t *val_len);

/** @brief Decode an integer column value (serial types 1-6, and 8/9 -> 0/1), sign-extended big-endian. */
int64_t pc_sqlite_column_int(uint64_t serial_type, const uint8_t *val, uint32_t val_len);

/** @brief Decode a float column value (serial type 7): an 8-byte big-endian IEEE-754 double. */
double pc_sqlite_column_float(const uint8_t *val, uint32_t val_len);

/** @brief Maximum table b-tree depth the cursor descends (SQLite trees are shallow; this is generous). */
#define SQLITE_BTREE_MAX_DEPTH 20

/**
 * @brief Fetch page number @p pgno (1-based) into @p page (@p page_size bytes). @return true on success.
 * The table cursor pulls pages through this so it works over any backing store (a RAM image, pc_wal_fs, or a
 * board driver's filesystem).
 */
typedef proto_bool (*SqlitePageReader)(void *ctx, uint32_t pgno, uint8_t *page, uint32_t page_size);

/**
 * @brief Reassemble a row's full record payload, following the overflow-page chain (fileformat2.html 1.6).
 *
 * A row that does not fit its leaf page keeps its first @c local_len bytes in the leaf, followed by a 4-byte
 * big-endian first-overflow-page number; each overflow page is a 4-byte next-page pointer (0 = last) plus up
 * to `page_size - reserved - 4` content bytes. This copies the in-page prefix, then walks the chain until
 * @c payload_len bytes are gathered.
 *
 * @param read/ctx    page source (the same reader the table cursor uses).
 * @param page_size   page size in bytes; @p reserved the per-page reserved tail (usable = page_size - reserved).
 * @param leaf_page   the leaf page the cell was parsed from (holds the local prefix + the overflow pointer).
 * @param cell        the parsed leaf cell.
 * @param out         destination for the full payload; must hold at least @c payload_len bytes.
 * @param out_cap     capacity of @p out - a payload larger than this fails closed (no overrun).
 * @param work_page   a @p page_size scratch buffer for reading overflow pages (must not alias @p leaf_page).
 * @return true when @c payload_len bytes were reassembled into @p out; false on a short buffer, a read error,
 * or a broken / looping chain (the page count is bounded, so a corrupt pointer cannot spin forever).
 */
proto_bool pc_sqlite_read_payload(SqlitePageReader read, void *ctx, uint32_t page_size, uint8_t reserved,
                                  const uint8_t *leaf_page, const SqliteTableLeafCell *cell, uint8_t *out,
                                  uint32_t out_cap, uint8_t *work_page);

/**
 * @brief A forward cursor over the rows of a table b-tree, in rowid order, across pages.
 *
 * It walks the interior/leaf table b-tree rooted at a table's `rootpage`, descending leftmost and yielding
 * each leaf cell as a row. It keeps only a bounded descent stack of page numbers (re-reading an interior
 * page when it returns to it) plus two page buffers - the current leaf and a scratch - so memory is fixed
 * regardless of table size. A row that overflows onto overflow pages yields only its in-page prefix by
 * default (and @c has_overflow flags it); provide an overflow buffer with
 * ::pc_sqlite_table_cursor_set_overflow_buf and the cursor transparently reassembles the full record for each
 * overflowing row instead.
 */
typedef struct
{
    SqlitePageReader read;
    void *ctx;
    uint32_t page_size;
    uint8_t reserved;
    uint8_t *leaf;    ///< page_size buffer holding the current leaf page (values point into it)
    uint8_t *work;    ///< page_size scratch for interior-page reads during traversal
    uint8_t *ovf_buf; ///< optional buffer to reassemble overflowing rows into (null = prefix-only)
    uint32_t ovf_cap; ///< capacity of ovf_buf in bytes
    uint32_t stack_pg[SQLITE_BTREE_MAX_DEPTH];
    uint16_t stack_idx[SQLITE_BTREE_MAX_DEPTH]; ///< next child index to descend at each interior frame
    int depth;
    SqliteBtreeHeader leaf_hdr;
    uint32_t leaf_off;
    uint32_t leaf_pgno;
    uint16_t leaf_cell;  ///< next cell index within the current leaf
    uint16_t leaf_count; ///< cells in the current leaf
} SqliteTableCursor;

/**
 * @brief Opt in to full overflow-chain reassembly. @p buf (@p cap bytes, must outlive the cursor and be at
 * least as large as the biggest row) receives the reassembled record for each overflowing row; without it
 * ::pc_sqlite_table_cursor_next yields only the in-page prefix of an overflowing row. Reuses the cursor's work
 * page as scratch, so it adds no other buffer. Call after ::pc_sqlite_table_cursor_begin.
 */
void pc_sqlite_table_cursor_set_overflow_buf(SqliteTableCursor *c, uint8_t *buf, uint32_t cap);

/**
 * @brief Begin a table cursor at @p rootpage. @p leaf_buf and @p work_buf are each @p page_size bytes and
 * must outlive the cursor. @return false if the root page cannot be read/parsed as a table b-tree.
 */
proto_bool pc_sqlite_table_cursor_begin(SqliteTableCursor *c, SqlitePageReader read, void *ctx, uint32_t page_size,
                                        uint8_t reserved, uint32_t rootpage, uint8_t *leaf_buf, uint8_t *work_buf);

/**
 * @brief Advance to the next row: sets @p rowid and starts @p row (a record cursor over the row's columns).
 * Column value pointers stay valid until the next call. @return false at the end of the table.
 */
proto_bool pc_sqlite_table_cursor_next(SqliteTableCursor *c, uint64_t *rowid, SqliteRecordCursor *row);

// ---------------------------------------------------------------------------
// Writer (bounded): build a fresh single-table SQLite database image.
//
// The inverse of the reader: it emits a byte-valid SQLite3 file that the sqlite3 CLI (or any reader) can
// open. It is deliberately BOUNDED - it lays the schema row on page 1 and the table's rows into one leaf
// b-tree page on page 2, and fails closed if a row would overflow the page or the rows do not fit (no page
// splitting, no overflow pages, no interior maintenance). That covers the on-device small config / dataset
// export case; a multi-page writer is a follow-up. Zero-heap: it writes straight into the caller's buffer.
// ---------------------------------------------------------------------------

/** @brief Encode a SQLite varint for @p v into @p out. @return bytes written (1-9), or 0 if @p cap too small. */
size_t pc_sqlite_varint_encode(uint64_t v, uint8_t *out, size_t cap);

/** @brief Column value kind for the record writer. */
typedef enum PROTO_ENUM_PACKED
{
    SQLITE_COL_NULL,
    SQLITE_COL_INT,
    SQLITE_COL_FLOAT,
    SQLITE_COL_TEXT,
    SQLITE_COL_BLOB
} SqliteColType;

/** @brief A typed column value to write (TEXT/BLOB bytes are referenced, not copied, until encode time). */
typedef struct
{
    SqliteColType type;
    int64_t i;           ///< INT value
    double f;            ///< FLOAT value
    const uint8_t *data; ///< TEXT / BLOB bytes
    uint32_t len;        ///< TEXT / BLOB byte length
} SqliteValue;

/**
 * @brief Encode @p n columns as a record (row payload): the header (a length varint + one serial-type varint
 * per column) followed by the value bytes. Integer columns use the minimal serial type (0/1 -> the 8/9
 * constants, else a 1/2/3/4/6/8-byte big-endian int); TEXT/BLOB use 13+2n / 12+2n. @return bytes written, or
 * 0 on overflow / bad input.
 */
uint32_t pc_sqlite_encode_record(const SqliteValue *cols, uint32_t n, uint8_t *out, uint32_t out_cap);

/** @brief A row for the table builder: its rowid and its column values (rowids must be ascending). */
typedef struct
{
    uint64_t rowid;
    const SqliteValue *cols;
    uint32_t ncols;
} SqliteRow;

/**
 * @brief Build a fresh two-page single-table database (page 1 = database header + the `pc_sqlite_schema` row,
 * page 2 = the table's leaf b-tree) into @p out.
 *
 * @param page_size   512..65536, a power of two.
 * @param table_name  the table's name (stored as `pc_sqlite_schema.name` and `.tbl_name`).
 * @param create_sql  the exact `CREATE TABLE ...` text stored in `pc_sqlite_schema.sql`.
 * @param rows        the table rows, rowid-ascending; @p nrows may be 0 (empty table).
 * @param out         destination image buffer; @p out_cap must be at least 2 * @p page_size bytes.
 * @return the image length (2 * @p page_size), or 0 if a row would overflow a page, the rows do not fit one
 * leaf page (the bounded writer fails closed), or on bad input.
 */
uint32_t pc_sqlite_build_table_db(uint32_t page_size, const char *table_name, const char *create_sql,
                                  const SqliteRow *rows, uint32_t nrows, uint8_t *out, uint32_t out_cap);

#endif // PC_ENABLE_SQLITE

PROTO_END_DECLS

#endif // PROTOCORE_SQLITE_FORMAT_H
