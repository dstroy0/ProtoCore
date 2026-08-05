// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/sqlite: the SQLite3 on-disk file-format parsers. The page-1 vector below is the
// first 512 bytes of a REAL database built by the sqlite3 CLI (3.46.1) with:
//   PRAGMA page_size=512; PRAGMA encoding='UTF-8';
//   CREATE TABLE t(a INTEGER, b TEXT); INSERT INTO t VALUES(42,'hello'); INSERT INTO t VALUES(7,'world');
// so the parsers are checked against bytes an authoritative implementation actually wrote.

#include "db_multipage.h"
#include "db_overflow.h"
#include "services/storage/sqlite/sqlite_format.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// First 512 bytes (page 1) of the real database file.
static const uint8_t PAGE1[512] = {
    0x53, 0x51, 0x4c, 0x69, 0x74, 0x65, 0x20, 0x66, 0x6f, 0x72, 0x6d, 0x61, 0x74, 0x20, 0x33, 0x00, //
    0x02, 0x00, 0x01, 0x01, 0x00, 0x40, 0x20, 0x20, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, //
    0x00, 0x2e, 0x7a, 0x71, 0x0d, 0x00, 0x00, 0x00, 0x01, 0x01, 0xcf, 0x00, 0x01, 0xcf, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2f, //
    0x01, 0x06, 0x17, 0x0f, 0x0f, 0x01, 0x4f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x74, 0x74, 0x02, 0x43, //
    0x52, 0x45, 0x41, 0x54, 0x45, 0x20, 0x54, 0x41, 0x42, 0x4c, 0x45, 0x20, 0x74, 0x28, 0x61, 0x20, //
    0x49, 0x4e, 0x54, 0x45, 0x47, 0x45, 0x52, 0x2c, 0x20, 0x62, 0x20, 0x54, 0x45, 0x58, 0x54, 0x29, //
};

void test_db_header_real_file(void)
{
    SqliteDbHeader h;
    TEST_ASSERT_TRUE(pc_sqlite_parse_db_header(PAGE1, sizeof(PAGE1), &h));
    TEST_ASSERT_EQUAL_UINT32(512, h.page_size);
    TEST_ASSERT_EQUAL_UINT8(1, h.write_version); // legacy rollback journal
    TEST_ASSERT_EQUAL_UINT8(1, h.read_version);
    TEST_ASSERT_EQUAL_UINT8(0, h.reserved_per_page);
    TEST_ASSERT_EQUAL_UINT32(2, h.page_count);    // matches PRAGMA page_count
    TEST_ASSERT_EQUAL_UINT32(1, h.schema_cookie); // matches PRAGMA schema_version
    TEST_ASSERT_EQUAL_UINT32(4, h.schema_format);
    TEST_ASSERT_EQUAL_UINT32(1, h.text_encoding);           // UTF-8
    TEST_ASSERT_EQUAL_UINT32(3046001, h.pc_sqlite_version); // SQLite 3.46.1
    TEST_ASSERT_EQUAL_UINT32(0, h.freelist_first);
}

void test_db_header_rejects_bad_magic(void)
{
    uint8_t bad[100];
    for (int i = 0; i < 100; i++)
    {
        bad[i] = PAGE1[i];
    }
    bad[0] = 'X'; // corrupt the magic
    SqliteDbHeader h;
    TEST_ASSERT_FALSE(pc_sqlite_parse_db_header(bad, sizeof(bad), &h));
    // Too short also fails.
    TEST_ASSERT_FALSE(pc_sqlite_parse_db_header(PAGE1, 99, &h));
}

void test_btree_header_real_page1(void)
{
    SqliteBtreeHeader b;
    // Page 1's b-tree header follows the 100-byte database header.
    TEST_ASSERT_TRUE(pc_sqlite_parse_btree_header(PAGE1, sizeof(PAGE1), 100, &b));
    TEST_ASSERT_EQUAL_UINT8(SQLITE_BTREE_LEAF_TABLE, b.type); // 13 - the pc_sqlite_schema table
    TEST_ASSERT_EQUAL_UINT16(0, b.first_freeblock);
    TEST_ASSERT_EQUAL_UINT16(1, b.cell_count); // one schema row (the CREATE TABLE)
    TEST_ASSERT_EQUAL_UINT32(463, b.cell_content_start);
    TEST_ASSERT_EQUAL_UINT8(8, b.header_size); // leaf
    TEST_ASSERT_EQUAL_UINT32(0, b.right_most_page);
}

void test_btree_header_rejects_bad_type(void)
{
    uint8_t page[16];
    for (int i = 0; i < 16; i++)
    {
        page[i] = 0;
    }
    page[0] = 99; // not a valid b-tree page type
    SqliteBtreeHeader b;
    TEST_ASSERT_FALSE(pc_sqlite_parse_btree_header(page, sizeof(page), 0, &b));
    // A valid leaf type but the header runs past the buffer.
    page[0] = SQLITE_BTREE_LEAF_TABLE;
    TEST_ASSERT_FALSE(pc_sqlite_parse_btree_header(page, 4, 0, &b));
}

void test_first_cell_varints(void)
{
    // The single cell pointer lives right after the 8-byte leaf header (offset 108), big-endian u16.
    uint32_t cell_off = ((uint32_t)PAGE1[108] << 8) | PAGE1[109];
    TEST_ASSERT_EQUAL_UINT32(463, cell_off);
    // A leaf-table cell begins with: payload-length varint, then rowid varint.
    uint64_t payload_len = 0, rowid = 0;
    size_t n = pc_sqlite_varint_decode(PAGE1 + cell_off, sizeof(PAGE1) - cell_off, &payload_len);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT64(47, payload_len); // 0x2f
    size_t n2 = pc_sqlite_varint_decode(PAGE1 + cell_off + n, sizeof(PAGE1) - cell_off - n, &rowid);
    TEST_ASSERT_EQUAL_size_t(1, n2);
    TEST_ASSERT_EQUAL_UINT64(1, rowid);
}

void test_varint_spec_vectors(void)
{
    uint64_t v = 0;
    const uint8_t a[] = {0x00};
    TEST_ASSERT_EQUAL_size_t(1, pc_sqlite_varint_decode(a, 1, &v));
    TEST_ASSERT_EQUAL_UINT64(0, v);

    const uint8_t b[] = {0x7f};
    TEST_ASSERT_EQUAL_size_t(1, pc_sqlite_varint_decode(b, 1, &v));
    TEST_ASSERT_EQUAL_UINT64(127, v);

    const uint8_t c[] = {0x81, 0x00}; // 0x80 continuation -> (1<<7)|0 = 128
    TEST_ASSERT_EQUAL_size_t(2, pc_sqlite_varint_decode(c, 2, &v));
    TEST_ASSERT_EQUAL_UINT64(128, v);

    const uint8_t d[] = {0x82, 0x2f}; // (2<<7)|0x2f = 303
    TEST_ASSERT_EQUAL_size_t(2, pc_sqlite_varint_decode(d, 2, &v));
    TEST_ASSERT_EQUAL_UINT64(303, v);

    // All nine bytes set -> the full 64-bit value.
    const uint8_t big[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    TEST_ASSERT_EQUAL_size_t(9, pc_sqlite_varint_decode(big, 9, &v));
    TEST_ASSERT_EQUAL_UINT64(0xFFFFFFFFFFFFFFFFull, v);

    // Incomplete varint (continuation bit set but no more bytes) fails closed.
    const uint8_t inc[] = {0x81};
    TEST_ASSERT_EQUAL_size_t(0, pc_sqlite_varint_decode(inc, 1, &v));
}

void test_serial_type_sizes(void)
{
    TEST_ASSERT_EQUAL_UINT64(0, pc_sqlite_serial_type_size(0)); // NULL
    TEST_ASSERT_EQUAL_UINT64(1, pc_sqlite_serial_type_size(1));
    TEST_ASSERT_EQUAL_UINT64(2, pc_sqlite_serial_type_size(2));
    TEST_ASSERT_EQUAL_UINT64(3, pc_sqlite_serial_type_size(3));
    TEST_ASSERT_EQUAL_UINT64(4, pc_sqlite_serial_type_size(4));
    TEST_ASSERT_EQUAL_UINT64(6, pc_sqlite_serial_type_size(5));
    TEST_ASSERT_EQUAL_UINT64(8, pc_sqlite_serial_type_size(6));
    TEST_ASSERT_EQUAL_UINT64(8, pc_sqlite_serial_type_size(7));  // float64
    TEST_ASSERT_EQUAL_UINT64(0, pc_sqlite_serial_type_size(8));  // int 0
    TEST_ASSERT_EQUAL_UINT64(0, pc_sqlite_serial_type_size(9));  // int 1
    TEST_ASSERT_EQUAL_UINT64(0, pc_sqlite_serial_type_size(10)); // reserved
    TEST_ASSERT_EQUAL_UINT64(0, pc_sqlite_serial_type_size(11)); // reserved
    TEST_ASSERT_EQUAL_UINT64(0, pc_sqlite_serial_type_size(12)); // BLOB len 0
    TEST_ASSERT_EQUAL_UINT64(1, pc_sqlite_serial_type_size(14)); // BLOB len 1
    TEST_ASSERT_EQUAL_UINT64(0, pc_sqlite_serial_type_size(13)); // TEXT len 0
    TEST_ASSERT_EQUAL_UINT64(1, pc_sqlite_serial_type_size(15)); // TEXT len 1  (seen in the real record)
    TEST_ASSERT_EQUAL_UINT64(5, pc_sqlite_serial_type_size(23)); // TEXT len 5  ("table", the real record)
}

// Read the one real row on page 1: the pc_sqlite_schema entry describing table t.
// Columns are (type, name, tbl_name, rootpage, sql) = ("table","t","t",2,"CREATE TABLE t(a INTEGER, b TEXT)").
void test_read_schema_row(void)
{
    SqliteBtreeHeader b;
    TEST_ASSERT_TRUE(pc_sqlite_parse_btree_header(PAGE1, sizeof(PAGE1), 100, &b));
    uint32_t cp = pc_sqlite_cell_pointer(PAGE1, sizeof(PAGE1), &b, 100, 0);
    TEST_ASSERT_EQUAL_UINT32(463, cp);

    SqliteTableLeafCell cell;
    TEST_ASSERT_TRUE(pc_sqlite_parse_table_leaf_cell(PAGE1, sizeof(PAGE1), 512, 0, cp, &cell));
    TEST_ASSERT_EQUAL_UINT64(1, cell.rowid);
    TEST_ASSERT_EQUAL_UINT32(47, cell.payload_len);
    TEST_ASSERT_FALSE(cell.has_overflow);
    TEST_ASSERT_EQUAL_UINT32(47, cell.local_len);

    SqliteRecordCursor c;
    TEST_ASSERT_TRUE(pc_sqlite_record_begin(&c, PAGE1 + cell.local_off, cell.local_len));
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;

    TEST_ASSERT_TRUE(pc_sqlite_record_next(&c, &st, &v, &vl)); // type
    TEST_ASSERT_EQUAL_UINT64(23, st);                          // text, 5 bytes
    TEST_ASSERT_EQUAL_UINT32(5, vl);
    TEST_ASSERT_EQUAL_MEMORY("table", v, 5);

    TEST_ASSERT_TRUE(pc_sqlite_record_next(&c, &st, &v, &vl)); // name
    TEST_ASSERT_EQUAL_UINT32(1, vl);
    TEST_ASSERT_EQUAL_MEMORY("t", v, 1);

    TEST_ASSERT_TRUE(pc_sqlite_record_next(&c, &st, &v, &vl)); // tbl_name
    TEST_ASSERT_EQUAL_MEMORY("t", v, 1);

    TEST_ASSERT_TRUE(pc_sqlite_record_next(&c, &st, &v, &vl)); // rootpage
    TEST_ASSERT_EQUAL_UINT64(1, st);                           // 1-byte int
    TEST_ASSERT_EQUAL_INT64(2, pc_sqlite_column_int(st, v, vl));

    TEST_ASSERT_TRUE(pc_sqlite_record_next(&c, &st, &v, &vl)); // sql
    TEST_ASSERT_EQUAL_UINT64(79, st);                          // text, 33 bytes
    TEST_ASSERT_EQUAL_UINT32(33, vl);
    TEST_ASSERT_EQUAL_MEMORY("CREATE TABLE t(a INTEGER, b TEXT)", v, 33);

    TEST_ASSERT_FALSE(pc_sqlite_record_next(&c, &st, &v, &vl)); // exactly five columns
}

void test_column_int_signextend(void)
{
    const uint8_t m1[] = {0xFF};
    TEST_ASSERT_EQUAL_INT64(-1, pc_sqlite_column_int(1, m1, 1));
    const uint8_t p127[] = {0x7F};
    TEST_ASSERT_EQUAL_INT64(127, pc_sqlite_column_int(1, p127, 1));
    const uint8_t m2[] = {0xFF, 0xFE};
    TEST_ASSERT_EQUAL_INT64(-2, pc_sqlite_column_int(2, m2, 2));
    const uint8_t big[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}; // 256 as a 64-bit int
    TEST_ASSERT_EQUAL_INT64(256, pc_sqlite_column_int(6, big, 8));
    TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(8, NULL, 0)); // constant 0
    TEST_ASSERT_EQUAL_INT64(1, pc_sqlite_column_int(9, NULL, 0)); // constant 1
}

// A payload larger than the local max for a 512-byte page must be flagged as overflowing.
void test_leaf_cell_overflow_detection(void)
{
    uint8_t page[512];
    for (int i = 0; i < 512; i++)
    {
        page[i] = 0;
    }
    // cell at offset 100: payload-length varint 478 (0x83,0x5E), rowid varint 1.
    page[100] = 0x83;
    page[101] = 0x5E;
    page[102] = 0x01;
    SqliteTableLeafCell cell;
    TEST_ASSERT_TRUE(pc_sqlite_parse_table_leaf_cell(page, sizeof(page), 512, 0, 100, &cell));
    TEST_ASSERT_EQUAL_UINT64(1, cell.rowid);
    TEST_ASSERT_EQUAL_UINT32(478, cell.payload_len);
    TEST_ASSERT_TRUE(cell.has_overflow);
    TEST_ASSERT_EQUAL_UINT32(39, cell.local_len); // min_local per the SQLite threshold formula
}

// Page reader over an in-memory database image (pages are 1-based).
typedef struct
{
    const uint8_t *data;
    uint32_t size;
} MemDb;
static proto_bool mem_read(void *ctx, uint32_t pgno, uint8_t *page, uint32_t page_size)
{
    MemDb *m = (MemDb *)ctx;
    if (pgno < 1)
    {
        return PROTO_FALSE;
    }
    uint32_t off = (pgno - 1) * page_size;
    if (off + page_size > m->size)
    {
        return PROTO_FALSE;
    }
    memcpy(page, m->data + off, page_size);
    return PROTO_TRUE;
}

// Walk a real 2-level table b-tree (interior root on page 2) and read all 40 rows in rowid order.
void test_table_cursor_multipage(void)
{
    // The table's root page (page 2) is an interior table page, so this exercises the descent stack.
    SqliteBtreeHeader bh;
    TEST_ASSERT_TRUE(
        pc_sqlite_parse_btree_header(DB_MULTIPAGE + DB_MP_PAGE_SIZE, sizeof(DB_MULTIPAGE) - DB_MP_PAGE_SIZE, 0, &bh));
    TEST_ASSERT_EQUAL_UINT8(SQLITE_BTREE_INTERIOR_TABLE, bh.type);

    MemDb db = {DB_MULTIPAGE, sizeof(DB_MULTIPAGE)};
    static uint8_t leaf[512], work[512];
    SqliteTableCursor c;
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, mem_read, &db, DB_MP_PAGE_SIZE, 0, 2, leaf, work));

    uint32_t n = 0;
    uint64_t rowid = 0;
    SqliteRecordCursor row;
    while (pc_sqlite_table_cursor_next(&c, &rowid, &row))
    {
        n++;
        TEST_ASSERT_EQUAL_UINT64(n, rowid); // rowids 1..40, in order, across pages

        uint64_t st = 0;
        const uint8_t *v = NULL;
        uint32_t vl = 0;
        TEST_ASSERT_TRUE(pc_sqlite_record_next(&row, &st, &v, &vl)); // column a (INTEGER) == rowid
        TEST_ASSERT_EQUAL_INT64((int64_t)n, pc_sqlite_column_int(st, v, vl));

        TEST_ASSERT_TRUE(pc_sqlite_record_next(&row, &st, &v, &vl)); // column b (TEXT)
        char expect[64];
        snprintf(expect, sizeof(expect), "row%04d-abcdefghijklmnopqrstuvwxyz", (int)n);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(expect), vl);
        TEST_ASSERT_EQUAL_MEMORY(expect, v, vl);

        TEST_ASSERT_FALSE(pc_sqlite_record_next(&row, &st, &v, &vl)); // exactly two columns
    }
    TEST_ASSERT_EQUAL_UINT32(DB_MP_ROWS, n); // every row visited exactly once
}

// Page reader over the overflow fixture, whose pages are separate 512-byte arrays (1-based).
static proto_bool ovf_read(void *ctx, uint32_t pgno, uint8_t *page, uint32_t page_size)
{
    (void)ctx;
    if (pgno < 1 || pgno > OVF_PAGE_COUNT || page_size != OVF_PAGE_SIZE)
    {
        return PROTO_FALSE;
    }
    memcpy(page, OVF_PAGES[pgno - 1], OVF_PAGE_SIZE);
    return PROTO_TRUE;
}

// Read the TEXT column b of a row record and assert it is `len` copies of `ch`.
static void assert_text_run(SqliteRecordCursor *row, uint32_t len, char ch)
{
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;
    TEST_ASSERT_TRUE(pc_sqlite_record_next(row, &st, &v, &vl)); // column a (INTEGER id)
    TEST_ASSERT_TRUE(pc_sqlite_record_next(row, &st, &v, &vl)); // column b (TEXT data)
    TEST_ASSERT_EQUAL_UINT64(13u + 2u * len, st);               // TEXT serial type = 13 + 2*len
    TEST_ASSERT_EQUAL_UINT32(len, vl);
    for (uint32_t i = 0; i < vl; i++)
    {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)ch, v[i]);
    }
}

// Read the TEXT column b of a row record and assert it equals the exact string `s`.
static void assert_text_eq(SqliteRecordCursor *row, const char *s)
{
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;
    uint32_t len = (uint32_t)strlen(s);
    TEST_ASSERT_TRUE(pc_sqlite_record_next(row, &st, &v, &vl)); // column a (INTEGER id)
    TEST_ASSERT_TRUE(pc_sqlite_record_next(row, &st, &v, &vl)); // column b (TEXT data)
    TEST_ASSERT_EQUAL_UINT64(13u + 2u * len, st);
    TEST_ASSERT_EQUAL_UINT32(len, vl);
    TEST_ASSERT_EQUAL_MEMORY(s, v, vl);
}

// Scan the image for the first leaf-table page holding an overflowing cell; copy that leaf page into
// @p leaf_out and return the parsed cell. (The table root here is an interior page whose leaves sit on
// later pages, so we locate the leaf rather than assume the root is one.)
static proto_bool find_overflow_cell(uint8_t *leaf_out, SqliteTableLeafCell *cell_out)
{
    for (uint32_t pg = 1; pg <= OVF_PAGE_COUNT; pg++)
    {
        uint8_t page[OVF_PAGE_SIZE];
        if (!ovf_read(NULL, pg, page, OVF_PAGE_SIZE))
        {
            continue;
        }
        size_t off = (pg == 1) ? 100 : 0;
        SqliteBtreeHeader bh;
        if (!pc_sqlite_parse_btree_header(page, OVF_PAGE_SIZE, off, &bh) || bh.type != SQLITE_BTREE_LEAF_TABLE)
        {
            continue;
        }
        for (uint16_t i = 0; i < bh.cell_count; i++)
        {
            uint32_t cp = pc_sqlite_cell_pointer(page, OVF_PAGE_SIZE, &bh, off, i);
            SqliteTableLeafCell cell;
            if (cp && pc_sqlite_parse_table_leaf_cell(page, OVF_PAGE_SIZE, OVF_PAGE_SIZE, 0, cp, &cell) &&
                cell.has_overflow)
            {
                memcpy(leaf_out, page, OVF_PAGE_SIZE);
                *cell_out = cell;
                return PROTO_TRUE;
            }
        }
    }
    return PROTO_FALSE;
}

// Reassemble an overflowing row's payload directly with pc_sqlite_read_payload and verify the full TEXT.
void test_overflow_read_payload(void)
{
    static uint8_t leaf[OVF_PAGE_SIZE], work[OVF_PAGE_SIZE], payload[4096];
    SqliteTableLeafCell cell;
    TEST_ASSERT_TRUE(find_overflow_cell(leaf, &cell));
    TEST_ASSERT_TRUE(cell.has_overflow);
    TEST_ASSERT_TRUE(cell.local_len < cell.payload_len); // the record really spills onto overflow pages
    TEST_ASSERT_TRUE(
        pc_sqlite_read_payload(ovf_read, NULL, OVF_PAGE_SIZE, 0, leaf, &cell, payload, sizeof(payload), work));

    // The reassembled record decodes to (id INTEGER, data TEXT) where data is a run of one character.
    SqliteRecordCursor row;
    TEST_ASSERT_TRUE(pc_sqlite_record_begin(&row, payload, cell.payload_len));
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&row, &st, &v, &vl)); // id
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&row, &st, &v, &vl)); // data (TEXT)
    TEST_ASSERT_TRUE(vl == OVF_ROW2_LEN || vl == OVF_ROW3_LEN);
    TEST_ASSERT_EQUAL_UINT64(13u + 2u * vl, st);
    char ch = (char)v[0];
    TEST_ASSERT_TRUE(ch == 'A' || ch == 'B');
    for (uint32_t i = 0; i < vl; i++)
    {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)ch, v[i]); // every byte survived the chain intact
    }
}

// A non-overflow cell reassembles trivially: pc_sqlite_read_payload copies the in-page prefix and succeeds.
void test_read_payload_nonoverflow(void)
{
    static uint8_t leaf[OVF_PAGE_SIZE], work[OVF_PAGE_SIZE], out[OVF_PAGE_SIZE];
    memset(leaf, 0, sizeof(leaf));
    for (uint32_t i = 0; i < 50; i++)
    {
        leaf[8 + i] = (uint8_t)(i + 1); // a known local payload at offset 8
    }
    SqliteTableLeafCell cell;
    cell.rowid = 1;
    cell.payload_len = 50;
    cell.local_off = 8;
    cell.local_len = 50;
    cell.has_overflow = PROTO_FALSE;
    TEST_ASSERT_TRUE(pc_sqlite_read_payload(ovf_read, NULL, OVF_PAGE_SIZE, 0, leaf, &cell, out, sizeof(out), work));
    TEST_ASSERT_EQUAL_MEMORY(leaf + 8, out, 50);
}

// A corrupt overflow next-pointer (to a page the reader rejects) must fail closed, not crash.
void test_read_payload_bad_overflow_pointer(void)
{
    static uint8_t leaf[OVF_PAGE_SIZE], work[OVF_PAGE_SIZE], out[4096];
    memset(leaf, 0, sizeof(leaf));
    SqliteTableLeafCell cell;
    cell.rowid = 1;
    cell.payload_len = 1000; // > local -> claims an overflow chain
    cell.local_off = 8;
    cell.local_len = 100;
    cell.has_overflow = PROTO_TRUE;
    // The 4-byte first-overflow pointer sits right after the local prefix: point it at page 9999, which
    // ovf_read (only 11 pages) refuses -> the read fails and the reassembly returns false.
    uint32_t ptr = cell.local_off + cell.local_len;
    leaf[ptr] = 0x00;
    leaf[ptr + 1] = 0x00;
    leaf[ptr + 2] = 0x27;
    leaf[ptr + 3] = 0x0f; // 9999
    TEST_ASSERT_FALSE(pc_sqlite_read_payload(ovf_read, NULL, OVF_PAGE_SIZE, 0, leaf, &cell, out, sizeof(out), work));
}

// A short output buffer must fail closed, not overrun.
void test_overflow_read_payload_bounds(void)
{
    static uint8_t leaf[OVF_PAGE_SIZE], work[OVF_PAGE_SIZE];
    SqliteTableLeafCell cell;
    TEST_ASSERT_TRUE(find_overflow_cell(leaf, &cell));
    uint8_t tiny[16]; // far smaller than the >=1000-byte overflowing payload
    TEST_ASSERT_FALSE(pc_sqlite_read_payload(ovf_read, NULL, OVF_PAGE_SIZE, 0, leaf, &cell, tiny, sizeof(tiny), work));
}

// Drive the table cursor with an overflow buffer: every row (incl. the overflowing ones) fully reassembled.
void test_overflow_cursor(void)
{
    static uint8_t leaf[OVF_PAGE_SIZE], work[OVF_PAGE_SIZE], ovf[4096];
    SqliteTableCursor c;
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, ovf_read, NULL, OVF_PAGE_SIZE, 0, OVF_ROOTPAGE, leaf, work));
    pc_sqlite_table_cursor_set_overflow_buf(&c, ovf, sizeof(ovf));

    uint64_t rowid = 0;
    SqliteRecordCursor row;
    uint32_t n = 0;
    while (pc_sqlite_table_cursor_next(&c, &rowid, &row))
    {
        n++;
        TEST_ASSERT_EQUAL_UINT64(n, rowid);
        if (n == 1)
        {
            assert_text_eq(&row, OVF_ROW1);
        }
        else if (n == 2)
        {
            assert_text_run(&row, OVF_ROW2_LEN, 'A');
        }
        else
        {
            assert_text_run(&row, OVF_ROW3_LEN, 'B');
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3, n);
}

// ---- Writer (bounded single-table builder) ----

void test_varint_encode_roundtrip(void)
{
    const uint64_t vals[] = {0,
                             1,
                             127,
                             128,
                             16383,
                             16384,
                             2097151,
                             2097152,
                             0xFFFFFFFFull,
                             0x7FFFFFFFFFull,
                             0x1FFFFFFFFFFFFull,
                             0xFFFFFFFFFFFFFFull,
                             0xFFFFFFFFFFFFFFFFull};
    for (unsigned k = 0; k < sizeof(vals) / sizeof(vals[0]); k++)
    {
        uint8_t buf[9];
        size_t n = pc_sqlite_varint_encode(vals[k], buf, sizeof(buf));
        TEST_ASSERT_TRUE(n >= 1 && n <= 9);
        uint64_t back = 0;
        size_t m = pc_sqlite_varint_decode(buf, n, &back);
        TEST_ASSERT_EQUAL_size_t(n, m); // encode and decode agree on the length
        TEST_ASSERT_EQUAL_UINT64(vals[k], back);
    }
    // A capacity that is one short must fail closed.
    uint8_t one[1];
    TEST_ASSERT_EQUAL_size_t(0, pc_sqlite_varint_encode(128, one, 1));
}

void test_encode_record_roundtrip(void)
{
    // A row of (INT, TEXT, FLOAT, NULL, INT=0) round-trips through the record reader.
    SqliteValue cols[5];
    cols[0] = (SqliteValue){SQLITE_COL_INT, -12345, 0, NULL, 0};
    cols[1] = (SqliteValue){SQLITE_COL_TEXT, 0, 0, (const uint8_t *)"hello", 5};
    cols[2] = (SqliteValue){SQLITE_COL_FLOAT, 0, 3.14159, NULL, 0};
    cols[3] = (SqliteValue){SQLITE_COL_NULL, 0, 0, NULL, 0};
    cols[4] = (SqliteValue){SQLITE_COL_INT, 0, 0, NULL, 0}; // the constant 0 (serial type 8, 0 bytes)

    uint8_t rec[128];
    uint32_t rl = pc_sqlite_encode_record(cols, 5, rec, sizeof(rec));
    TEST_ASSERT_TRUE(rl > 0);

    SqliteRecordCursor rc;
    TEST_ASSERT_TRUE(pc_sqlite_record_begin(&rc, rec, rl));
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;

    TEST_ASSERT_TRUE(pc_sqlite_record_next(&rc, &st, &v, &vl)); // INT -12345
    TEST_ASSERT_EQUAL_INT64(-12345, pc_sqlite_column_int(st, v, vl));
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&rc, &st, &v, &vl)); // TEXT "hello"
    TEST_ASSERT_EQUAL_UINT64(13u + 2u * 5u, st);
    TEST_ASSERT_EQUAL_UINT32(5, vl);
    TEST_ASSERT_EQUAL_MEMORY("hello", v, 5);
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&rc, &st, &v, &vl)); // FLOAT 3.14159
    TEST_ASSERT_EQUAL_UINT64(7, st);
    // Unity's double asserts are disabled in this build; compare the IEEE-754 bit pattern instead (the
    // decoder reads back the exact 8 bytes we wrote, so it is bit-exact).
    double got = pc_sqlite_column_float(v, vl), want = 3.14159;
    uint64_t got_bits = 0, want_bits = 0;
    memcpy(&got_bits, &got, 8);
    memcpy(&want_bits, &want, 8);
    TEST_ASSERT_EQUAL_HEX64(want_bits, got_bits);
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&rc, &st, &v, &vl)); // NULL
    TEST_ASSERT_EQUAL_UINT64(0, st);
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&rc, &st, &v, &vl)); // INT 0 (constant)
    TEST_ASSERT_EQUAL_UINT64(8, st);
    TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(st, v, vl));
    TEST_ASSERT_FALSE(pc_sqlite_record_next(&rc, &st, &v, &vl)); // exactly five columns
}

void test_build_table_db_roundtrip(void)
{
    // Build a real 2-page DB, then read it back with our own reader.
    struct RowSpec
    {
        long a;
        const char *b;
    } spec[] = {{100, "alpha"}, {-5, "beta"}, {999999, "gamma"}};
    SqliteValue rowvals[3][2];
    SqliteRow rows[3];
    for (int r = 0; r < 3; r++)
    {
        rowvals[r][0] = (SqliteValue){SQLITE_COL_INT, spec[r].a, 0, NULL, 0};
        rowvals[r][1] = (SqliteValue){SQLITE_COL_TEXT, 0, 0, (const uint8_t *)spec[r].b, (uint32_t)strlen(spec[r].b)};
        rows[r] = (SqliteRow){(uint64_t)(r + 1), rowvals[r], 2};
    }

    static uint8_t img[1024]; // 2 * 512
    uint32_t len = pc_sqlite_build_table_db(512, "t", "CREATE TABLE t(a INTEGER, b TEXT)", rows, 3, img, sizeof(img));
    TEST_ASSERT_EQUAL_UINT32(1024, len);

    // The database header parses and reports what we wrote.
    SqliteDbHeader h;
    TEST_ASSERT_TRUE(pc_sqlite_parse_db_header(img, len, &h));
    TEST_ASSERT_EQUAL_UINT32(512, h.page_size);
    TEST_ASSERT_EQUAL_UINT32(2, h.page_count);
    TEST_ASSERT_EQUAL_UINT32(1, h.text_encoding);

    // The pc_sqlite_schema row on page 1: type='table', name='t', rootpage=2, sql=the CREATE.
    SqliteBtreeHeader p1;
    TEST_ASSERT_TRUE(pc_sqlite_parse_btree_header(img, 512, 100, &p1));
    TEST_ASSERT_EQUAL_UINT16(1, p1.cell_count);
    uint32_t mcp = pc_sqlite_cell_pointer(img, 512, &p1, 100, 0);
    SqliteTableLeafCell mcell;
    TEST_ASSERT_TRUE(pc_sqlite_parse_table_leaf_cell(img, 512, 512, 0, mcp, &mcell));
    SqliteRecordCursor mrec;
    TEST_ASSERT_TRUE(pc_sqlite_record_begin(&mrec, img + mcell.local_off, mcell.local_len));
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&mrec, &st, &v, &vl)); // type
    TEST_ASSERT_EQUAL_MEMORY("table", v, 5);
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&mrec, &st, &v, &vl)); // name
    TEST_ASSERT_EQUAL_MEMORY("t", v, 1);
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&mrec, &st, &v, &vl)); // tbl_name
    TEST_ASSERT_EQUAL_MEMORY("t", v, 1);
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&mrec, &st, &v, &vl)); // rootpage
    TEST_ASSERT_EQUAL_INT64(2, pc_sqlite_column_int(st, v, vl));

    // The table rows on page 2, read via the cursor.
    MemDb db = {img, len};
    static uint8_t leaf[512], work[512];
    SqliteTableCursor c;
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, mem_read, &db, 512, 0, 2, leaf, work));
    uint64_t rowid = 0;
    SqliteRecordCursor row;
    int n = 0;
    while (pc_sqlite_table_cursor_next(&c, &rowid, &row))
    {
        TEST_ASSERT_EQUAL_UINT64((uint64_t)(n + 1), rowid);
        TEST_ASSERT_TRUE(pc_sqlite_record_next(&row, &st, &v, &vl)); // a INTEGER
        TEST_ASSERT_EQUAL_INT64(spec[n].a, pc_sqlite_column_int(st, v, vl));
        TEST_ASSERT_TRUE(pc_sqlite_record_next(&row, &st, &v, &vl)); // b TEXT
        TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(spec[n].b), vl);
        TEST_ASSERT_EQUAL_MEMORY(spec[n].b, v, vl);
        n++;
    }
    TEST_ASSERT_EQUAL_INT(3, n);
}

void test_encode_record_int_widths(void)
{
    // Every integer serial type: the value round-trips and the encoder picks the minimal type.
    struct
    {
        int64_t v;
        uint64_t st;
    } cases[] = {
        {0, 8},
        {1, 9},
        {-1, 1},
        {127, 1},
        {-128, 1},
        {128, 2},
        {-129, 2},
        {32767, 2},
        {40000, 3},
        {-40000, 3},
        {8388607, 3},
        {10000000, 4},
        {-10000000, 4},
        {2147483647, 4},
        {3000000000LL, 5},
        {-3000000000LL, 5},
        {140737488355327LL, 5},
        {200000000000000LL, 6},
        {-9000000000000000000LL, 6},
    };
    for (unsigned k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
    {
        SqliteValue col = {SQLITE_COL_INT, cases[k].v, 0, NULL, 0};
        uint8_t rec[32];
        uint32_t rl = pc_sqlite_encode_record(&col, 1, rec, sizeof(rec));
        TEST_ASSERT_TRUE(rl > 0);
        SqliteRecordCursor rc;
        TEST_ASSERT_TRUE(pc_sqlite_record_begin(&rc, rec, rl));
        uint64_t st = 0;
        const uint8_t *v = NULL;
        uint32_t vl = 0;
        TEST_ASSERT_TRUE(pc_sqlite_record_next(&rc, &st, &v, &vl));
        TEST_ASSERT_EQUAL_UINT64(cases[k].st, st); // minimal serial type
        TEST_ASSERT_EQUAL_INT64(cases[k].v, pc_sqlite_column_int(st, v, vl));
        TEST_ASSERT_FALSE(pc_sqlite_record_next(&rc, &st, &v, &vl)); // single column
    }
}

void test_encode_record_blob(void)
{
    // A BLOB column (serial type 12 + 2n) round-trips its raw bytes, including embedded NULs.
    const uint8_t blob[] = {0x00, 0xff, 0x10, 0x00, 0x7f, 0x80};
    SqliteValue col = {SQLITE_COL_BLOB, 0, 0, blob, sizeof(blob)};
    uint8_t rec[32];
    uint32_t rl = pc_sqlite_encode_record(&col, 1, rec, sizeof(rec));
    TEST_ASSERT_TRUE(rl > 0);
    SqliteRecordCursor rc;
    TEST_ASSERT_TRUE(pc_sqlite_record_begin(&rc, rec, rl));
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&rc, &st, &v, &vl));
    TEST_ASSERT_EQUAL_UINT64(12u + 2u * sizeof(blob), st); // BLOB serial type
    TEST_ASSERT_EQUAL_UINT32(sizeof(blob), vl);
    TEST_ASSERT_EQUAL_MEMORY(blob, v, sizeof(blob));
}

void test_build_table_db_page_overflow_fails_closed(void)
{
    // Many rows that each fit but collectively exceed one leaf page must fail closed (distinct from the
    // single-oversized-row path): the cells + pointer array do not fit page 2.
    static SqliteValue rowvals[60][1];
    static SqliteRow rows[60];
    for (int r = 0; r < 60; r++)
    {
        rowvals[r][0] = (SqliteValue){SQLITE_COL_INT, 1000000 + r, 0, NULL, 0}; // ~4-byte int + framing each
        rows[r] = (SqliteRow){(uint64_t)(r + 1), rowvals[r], 1};
    }
    static uint8_t img[1024];
    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", rows, 60, img, sizeof(img)));
}

void test_build_table_db_fails_closed(void)
{
    // A single row larger than one leaf page can hold must fail closed (bounded writer, no overflow pages).
    static uint8_t big[600];
    memset(big, 'X', sizeof(big));
    SqliteValue col = {SQLITE_COL_TEXT, 0, 0, big, sizeof(big)};
    SqliteRow row = {1, &col, 1};
    static uint8_t img[1024];
    TEST_ASSERT_EQUAL_UINT32(0,
                             pc_sqlite_build_table_db(512, "t", "CREATE TABLE t(b TEXT)", &row, 1, img, sizeof(img)));
    // Too small an output buffer also fails closed.
    SqliteValue small = {SQLITE_COL_INT, 1, 0, NULL, 0};
    SqliteRow r2 = {1, &small, 1};
    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", &r2, 1, img, 512));
}

// ---- Malformed-input rejects: varints, headers, cells, records ----

void test_varint_decode_truncated_nine_byte(void)
{
    // Eight continuation bytes with no ninth byte: the 9-byte form is incomplete.
    const uint8_t eight[8] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    uint64_t v = 0;
    TEST_ASSERT_EQUAL_size_t(0, pc_sqlite_varint_decode(eight, sizeof(eight), &v));
    // With the ninth byte present the same prefix decodes (the 9th byte contributes all 8 bits).
    const uint8_t nine[9] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F};
    TEST_ASSERT_EQUAL_size_t(9, pc_sqlite_varint_decode(nine, sizeof(nine), &v));
    TEST_ASSERT_EQUAL_UINT64(0x7F, v);
}

void test_db_header_page_size_rejects(void)
{
    uint8_t hdr[100];
    memcpy(hdr, PAGE1, sizeof(hdr));
    SqliteDbHeader h;

    hdr[16] = 0x01; // 256 - below the 512 minimum
    hdr[17] = 0x00;
    TEST_ASSERT_FALSE(pc_sqlite_parse_db_header(hdr, sizeof(hdr), &h));

    hdr[16] = 0x02; // 513 - in range but not a power of two
    hdr[17] = 0x01;
    TEST_ASSERT_FALSE(pc_sqlite_parse_db_header(hdr, sizeof(hdr), &h));

    hdr[16] = 0x00; // the on-disk value 1 means 65536
    hdr[17] = 0x01;
    TEST_ASSERT_TRUE(pc_sqlite_parse_db_header(hdr, sizeof(hdr), &h));
    TEST_ASSERT_EQUAL_UINT32(65536, h.page_size);
}

void test_btree_header_index_pages_and_truncation(void)
{
    uint8_t page[16];
    SqliteBtreeHeader b;

    // An interior INDEX page is a valid b-tree page and carries the 12-byte header.
    memset(page, 0, sizeof(page));
    page[0] = SQLITE_BTREE_INTERIOR_INDEX;
    page[11] = 0x07; // right-most child page 7
    TEST_ASSERT_TRUE(pc_sqlite_parse_btree_header(page, sizeof(page), 0, &b));
    TEST_ASSERT_EQUAL_UINT8(12, b.header_size);
    TEST_ASSERT_EQUAL_UINT32(7, b.right_most_page);

    // A leaf INDEX page carries the 8-byte header and no right-most pointer.
    memset(page, 0, sizeof(page));
    page[0] = SQLITE_BTREE_LEAF_INDEX;
    TEST_ASSERT_TRUE(pc_sqlite_parse_btree_header(page, sizeof(page), 0, &b));
    TEST_ASSERT_EQUAL_UINT8(8, b.header_size);
    TEST_ASSERT_EQUAL_UINT32(0, b.right_most_page);
    // The on-disk cell-content-start 0 means 65536.
    TEST_ASSERT_EQUAL_UINT32(65536, b.cell_content_start);

    // An interior page whose 12-byte header does not fit, though the 8-byte check passes.
    page[0] = SQLITE_BTREE_INTERIOR_TABLE;
    TEST_ASSERT_FALSE(pc_sqlite_parse_btree_header(page, 10, 0, &b));
}

void test_cell_pointer_rejects(void)
{
    uint8_t page[32];
    memset(page, 0, sizeof(page));
    SqliteBtreeHeader b;
    b.type = SQLITE_BTREE_LEAF_TABLE;
    b.first_freeblock = 0;
    b.cell_count = 2;
    b.cell_content_start = 32;
    b.frag_free_bytes = 0;
    b.right_most_page = 0;
    b.header_size = 8;

    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_cell_pointer(page, sizeof(page), &b, 0, 2)); // index past the cell count

    b.cell_count = 100; // the pointer array itself runs past the buffer
    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_cell_pointer(page, sizeof(page), &b, 0, 99));
}

void test_leaf_cell_parse_rejects(void)
{
    static uint8_t page[512];
    SqliteTableLeafCell cell;

    memset(page, 0, sizeof(page));
    // The cell offset is outside the page.
    TEST_ASSERT_FALSE(pc_sqlite_parse_table_leaf_cell(page, sizeof(page), 512, 0, 512, &cell));

    // A truncated payload-length varint at the very last byte of the page.
    page[511] = 0x81;
    TEST_ASSERT_FALSE(pc_sqlite_parse_table_leaf_cell(page, sizeof(page), 512, 0, 511, &cell));

    // A complete payload-length varint followed by a truncated rowid varint.
    page[510] = 0x01;
    TEST_ASSERT_FALSE(pc_sqlite_parse_table_leaf_cell(page, sizeof(page), 512, 0, 510, &cell));

    // A local payload within the overflow threshold that still runs past the supplied page length.
    memset(page, 0, sizeof(page));
    page[100] = 0x82; // payload length 300
    page[101] = 0x2c;
    page[102] = 0x01; // rowid 1
    TEST_ASSERT_FALSE(pc_sqlite_parse_table_leaf_cell(page, 200, 512, 0, 100, &cell));
}

void test_record_begin_rejects(void)
{
    SqliteRecordCursor c;
    const uint8_t rec[3] = {0x80, 0x01, 0x00};             // header-size varint 1, encoded in 2 bytes
    TEST_ASSERT_FALSE(pc_sqlite_record_begin(&c, rec, 0)); // no header varint at all
    const uint8_t big[1] = {0x50};                         // header size 80 in a 1-byte record
    TEST_ASSERT_FALSE(pc_sqlite_record_begin(&c, big, sizeof(big)));
    TEST_ASSERT_FALSE(pc_sqlite_record_begin(&c, rec, sizeof(rec))); // header shorter than its own varint
}

void test_record_next_rejects(void)
{
    SqliteRecordCursor c;
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;

    // A truncated serial-type varint inside the record header.
    const uint8_t trunc[2] = {0x02, 0x81};
    TEST_ASSERT_TRUE(pc_sqlite_record_begin(&c, trunc, sizeof(trunc)));
    TEST_ASSERT_FALSE(pc_sqlite_record_next(&c, &st, &v, &vl));

    // A serial type whose value bytes run past the end of the record.
    const uint8_t past[2] = {0x02, 0x0f}; // header size 2, TEXT of 1 byte, no value bytes present
    TEST_ASSERT_TRUE(pc_sqlite_record_begin(&c, past, sizeof(past)));
    TEST_ASSERT_FALSE(pc_sqlite_record_next(&c, &st, &v, &vl));
}

void test_column_decoder_rejects(void)
{
    const uint8_t bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(0, bytes, 8)); // NULL is not an integer type
    TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(7, bytes, 8)); // float is not an integer type
    TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(4, bytes, 2)); // fewer value bytes than the type needs

    // A float column with fewer than 8 value bytes reads as 0.0 rather than over-reading.
    double d = pc_sqlite_column_float(bytes, 4);
    uint64_t bits = 0;
    memcpy(&bits, &d, 8);
    TEST_ASSERT_EQUAL_HEX64(0, bits);
}

// ---- Overflow-chain edges, driven by a synthetic 64-byte-page image ----

typedef struct
{
    uint8_t pages[2][64];
    uint32_t count;
} OvfSynth;

static proto_bool ovf_synth_read(void *ctx, uint32_t pgno, uint8_t *page, uint32_t page_size)
{
    OvfSynth *s = (OvfSynth *)ctx;
    if (pgno < 1 || pgno > s->count || page_size != 64)
    {
        return PROTO_FALSE;
    }
    memcpy(page, s->pages[pgno - 1], 64);
    return PROTO_TRUE;
}

void test_read_payload_chain_edges(void)
{
    OvfSynth s;
    memset(&s, 0, sizeof(s));
    s.count = 2;
    // Page 2 is the single overflow page: a next pointer plus 60 content bytes.
    s.pages[1][3] = 2; // next -> page 2 again (a non-zero pointer past the last needed byte)
    for (int i = 0; i < 60; i++)
    {
        s.pages[1][4 + i] = (uint8_t)i;
    }

    uint8_t leaf[64];
    memset(leaf, 0xAA, sizeof(leaf));
    // The 4-byte first-overflow page number sits at local_off + local_len = 10.
    leaf[10] = 0;
    leaf[11] = 0;
    leaf[12] = 0;
    leaf[13] = 2;

    static uint8_t out[256], work[64];
    SqliteTableLeafCell cell;
    cell.rowid = 1;
    cell.payload_len = 70;
    cell.local_off = 0;
    cell.local_len = 10;
    cell.has_overflow = PROTO_TRUE;

    // The payload completes exactly at the end of the first overflow page even though that page's
    // next pointer is non-zero: the byte count, not the pointer, ends the walk.
    TEST_ASSERT_TRUE(pc_sqlite_read_payload(ovf_synth_read, &s, 64, 0, leaf, &cell, out, sizeof(out), work));
    TEST_ASSERT_EQUAL_MEMORY(leaf, out, 10);
    for (int i = 0; i < 60; i++)
    {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, out[10 + i]);
    }

    // A chain that ends before the payload is complete fails closed.
    s.pages[1][3] = 0; // next = 0 after one page
    cell.payload_len = 200;
    TEST_ASSERT_FALSE(pc_sqlite_read_payload(ovf_synth_read, &s, 64, 0, leaf, &cell, out, sizeof(out), work));

    // The local prefix runs past the end of the page.
    cell.payload_len = 100;
    cell.local_off = 60;
    cell.local_len = 8;
    TEST_ASSERT_FALSE(pc_sqlite_read_payload(ovf_synth_read, &s, 64, 0, leaf, &cell, out, sizeof(out), work));

    // The local prefix ends exactly at the page end, so the 4-byte overflow pointer does not fit.
    cell.local_off = 60;
    cell.local_len = 4;
    TEST_ASSERT_FALSE(pc_sqlite_read_payload(ovf_synth_read, &s, 64, 0, leaf, &cell, out, sizeof(out), work));

    // A degenerate page size leaves no content room on an overflow page (usable - 4 == 0).
    cell.payload_len = 8;
    cell.local_off = 0;
    cell.local_len = 0;
    TEST_ASSERT_FALSE(pc_sqlite_read_payload(ovf_synth_read, &s, 4, 0, leaf, &cell, out, sizeof(out), work));
}

// ---- Synthetic b-tree pages: the cursor's corrupt-page and traversal edges ----

#define SYNTH_PAGE_SIZE 512

typedef struct
{
    uint8_t pages[6][SYNTH_PAGE_SIZE];
    uint32_t count;
    int reads;
    int fail_after;    // once `reads` passes this every read fails (-1 = never)
    int corrupt_after; // once `reads` passes this the page comes back with a bad type byte (-1 = never)
} SynthDb;

static proto_bool synth_read(void *ctx, uint32_t pgno, uint8_t *page, uint32_t page_size)
{
    SynthDb *s = (SynthDb *)ctx;
    s->reads++;
    if (pgno < 1 || pgno > s->count || page_size != SYNTH_PAGE_SIZE)
    {
        return PROTO_FALSE;
    }
    if (s->fail_after >= 0 && s->reads > s->fail_after)
    {
        return PROTO_FALSE;
    }
    memcpy(page, s->pages[pgno - 1], SYNTH_PAGE_SIZE);
    if (s->corrupt_after >= 0 && s->reads > s->corrupt_after)
    {
        page[0] = 0x63; // not a b-tree page type
    }
    return PROTO_TRUE;
}

static void synth_init(SynthDb *s, uint32_t count)
{
    memset(s, 0, sizeof(*s));
    s->count = count;
    s->fail_after = -1;
    s->corrupt_after = -1;
}

// A leaf-table page holding one row (a single NULL column) whose cell sits at `cell_at`.
static void synth_leaf(uint8_t *page, uint16_t cell_at, uint8_t rowid)
{
    memset(page, 0, SYNTH_PAGE_SIZE);
    page[0] = SQLITE_BTREE_LEAF_TABLE;
    page[4] = 1; // cell count
    page[8] = (uint8_t)(cell_at >> 8);
    page[9] = (uint8_t)cell_at;
    page[cell_at + 0] = 0x02;  // payload length 2
    page[cell_at + 1] = rowid; // rowid varint (< 128)
    page[cell_at + 2] = 0x02;  // record header size 2
    page[cell_at + 3] = 0x00;  // one NULL column
}

// An interior-table page whose i-th cell carries left-child page children[i].
static void synth_interior(uint8_t *page, uint16_t ncells, const uint32_t *children, uint32_t right_most)
{
    memset(page, 0, SYNTH_PAGE_SIZE);
    page[0] = SQLITE_BTREE_INTERIOR_TABLE;
    page[3] = (uint8_t)(ncells >> 8);
    page[4] = (uint8_t)ncells;
    page[8] = (uint8_t)(right_most >> 24);
    page[9] = (uint8_t)(right_most >> 16);
    page[10] = (uint8_t)(right_most >> 8);
    page[11] = (uint8_t)right_most;
    for (uint16_t i = 0; i < ncells; i++)
    {
        uint16_t at = (uint16_t)(200 + i * 8); // cell bodies, clear of the pointer array
        page[12 + i * 2] = (uint8_t)(at >> 8);
        page[13 + i * 2] = (uint8_t)at;
        page[at + 0] = (uint8_t)(children[i] >> 24);
        page[at + 1] = (uint8_t)(children[i] >> 16);
        page[at + 2] = (uint8_t)(children[i] >> 8);
        page[at + 3] = (uint8_t)children[i];
        page[at + 4] = 0x01; // the cell's key varint (unused by a table scan)
    }
}

// A page source that always hands back an interior-table page pointing at the next page number, so
// the descent never reaches a leaf and must stop at the depth cap.
static proto_bool endless_interior_read(void *ctx, uint32_t pgno, uint8_t *page, uint32_t page_size)
{
    (void)ctx;
    (void)page_size;
    const uint32_t child = pgno + 1;
    synth_interior(page, 1, &child, pgno + 2);
    return PROTO_TRUE;
}

void test_cursor_descend_rejects(void)
{
    static uint8_t leaf[SYNTH_PAGE_SIZE], work[SYNTH_PAGE_SIZE];
    SqliteTableCursor c;
    SynthDb s;
    const uint32_t kids[1] = {3};

    // The root page cannot be read at all.
    synth_init(&s, 2);
    synth_leaf(s.pages[1], 400, 1);
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 99, leaf, work));

    // The root page is not a b-tree page at all.
    synth_init(&s, 2);
    s.pages[1][0] = 0x63;
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 2, leaf, work));

    // A valid b-tree page, but an index b-tree - not a table scan.
    synth_init(&s, 2);
    s.pages[1][0] = SQLITE_BTREE_LEAF_INDEX;
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 2, leaf, work));

    // An interior page with no cells and a zero right-most pointer has no child to descend to.
    synth_init(&s, 2);
    synth_interior(s.pages[1], 0, NULL, 0);
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 2, leaf, work));

    // An interior page whose first cell pointer is 0 (a corrupt pointer array).
    synth_init(&s, 2);
    synth_interior(s.pages[1], 1, kids, 4);
    s.pages[1][12] = 0;
    s.pages[1][13] = 0;
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 2, leaf, work));

    // A cell pointer so close to the page end that the cell's child page number is truncated.
    synth_init(&s, 2);
    synth_interior(s.pages[1], 1, kids, 4);
    s.pages[1][12] = (uint8_t)((SYNTH_PAGE_SIZE - 2) >> 8);
    s.pages[1][13] = (uint8_t)(SYNTH_PAGE_SIZE - 2);
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 2, leaf, work));
}

void test_cursor_depth_cap(void)
{
    // An endless interior chain stops at SQLITE_BTREE_MAX_DEPTH instead of overrunning the stack.
    static uint8_t leaf[SYNTH_PAGE_SIZE], work[SYNTH_PAGE_SIZE];
    SqliteTableCursor c;
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_begin(&c, endless_interior_read, NULL, SYNTH_PAGE_SIZE, 0, 2, leaf, work));
    TEST_ASSERT_EQUAL_INT(SQLITE_BTREE_MAX_DEPTH, c.depth);
}

void test_cursor_next_skips_bad_cells(void)
{
    static uint8_t leaf[SYNTH_PAGE_SIZE], work[SYNTH_PAGE_SIZE];
    SynthDb s;
    synth_init(&s, 2);
    synth_leaf(s.pages[1], 400, 7);
    s.pages[1][4] = 3; // three cell pointers
    s.pages[1][8] = 0; // cell 0: a null pointer, skipped
    s.pages[1][9] = 0;
    s.pages[1][10] = 0x02; // cell 1: 600, outside the page, rejected
    s.pages[1][11] = 0x58;
    s.pages[1][12] = 0x01; // cell 2: 400, the real row
    s.pages[1][13] = 0x90;

    SqliteTableCursor c;
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 2, leaf, work));
    uint64_t rowid = 0;
    SqliteRecordCursor row;
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_next(&c, &rowid, &row));
    TEST_ASSERT_EQUAL_UINT64(7, rowid);
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_next(&c, &rowid, &row));
}

void test_cursor_parent_frame_rejects(void)
{
    static uint8_t leaf[SYNTH_PAGE_SIZE], work[SYNTH_PAGE_SIZE];
    SqliteTableCursor c;
    SynthDb s;
    uint64_t rowid = 0;
    SqliteRecordCursor row;
    const uint32_t kids[2] = {3, 4};

    // Re-reading the parent interior page fails once the first leaf is exhausted.
    synth_init(&s, 4);
    synth_interior(s.pages[1], 1, kids, 4);
    synth_leaf(s.pages[2], 400, 1);
    synth_leaf(s.pages[3], 400, 2);
    s.fail_after = 2; // the root read and the first leaf read succeed, the parent re-read does not
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 2, leaf, work));
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_next(&c, &rowid, &row));
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_next(&c, &rowid, &row));

    // The parent page comes back corrupt on the re-read.
    synth_init(&s, 4);
    synth_interior(s.pages[1], 1, kids, 4);
    synth_leaf(s.pages[2], 400, 1);
    synth_leaf(s.pages[3], 400, 2);
    s.corrupt_after = 2;
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 2, leaf, work));
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_next(&c, &rowid, &row));
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_next(&c, &rowid, &row));

    // The parent's next child pointer is 0.
    const uint32_t zero_kid[2] = {3, 0};
    synth_init(&s, 4);
    synth_interior(s.pages[1], 2, zero_kid, 4);
    synth_leaf(s.pages[2], 400, 1);
    synth_leaf(s.pages[3], 400, 2);
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 2, leaf, work));
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_next(&c, &rowid, &row));
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_next(&c, &rowid, &row));

    // The parent's next child page cannot be descended into (the reader rejects it).
    const uint32_t missing_kid[2] = {3, 99};
    synth_init(&s, 4);
    synth_interior(s.pages[1], 2, missing_kid, 4);
    synth_leaf(s.pages[2], 400, 1);
    synth_leaf(s.pages[3], 400, 2);
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, synth_read, &s, SYNTH_PAGE_SIZE, 0, 2, leaf, work));
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_next(&c, &rowid, &row));
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_next(&c, &rowid, &row));
}

void test_table_cursor_page1_schema_scan(void)
{
    // Scanning the schema table roots the cursor at page 1, whose b-tree header sits after the
    // 100-byte database header.
    MemDb db = {PAGE1, sizeof(PAGE1)};
    static uint8_t leaf[512], work[512];
    SqliteTableCursor c;
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, mem_read, &db, 512, 0, 1, leaf, work));
    TEST_ASSERT_EQUAL_UINT32(100, c.leaf_off);

    uint64_t rowid = 0;
    SqliteRecordCursor row;
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_next(&c, &rowid, &row));
    TEST_ASSERT_EQUAL_UINT64(1, rowid);
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&row, &st, &v, &vl));
    TEST_ASSERT_EQUAL_UINT32(5, vl);
    TEST_ASSERT_EQUAL_MEMORY("table", v, 5);
    TEST_ASSERT_FALSE(pc_sqlite_table_cursor_next(&c, &rowid, &row)); // exactly one schema row
}

void test_overflow_cursor_without_buffer(void)
{
    // With no overflow buffer the cursor still yields every row, just the in-page prefix of the
    // overflowing ones.
    static uint8_t leaf[OVF_PAGE_SIZE], work[OVF_PAGE_SIZE];
    SqliteTableCursor c;
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, ovf_read, NULL, OVF_PAGE_SIZE, 0, OVF_ROOTPAGE, leaf, work));
    uint64_t rowid = 0;
    SqliteRecordCursor row;
    uint32_t n = 0;
    while (pc_sqlite_table_cursor_next(&c, &rowid, &row))
    {
        n++;
        TEST_ASSERT_EQUAL_UINT64(n, rowid);
    }
    TEST_ASSERT_EQUAL_UINT32(3, n);
}

void test_overflow_cursor_short_buffer_skips_row(void)
{
    // An overflow buffer too small for a row makes the reassembly fail, and that row is skipped
    // rather than half-returned.
    static uint8_t leaf[OVF_PAGE_SIZE], work[OVF_PAGE_SIZE], tiny[64];
    SqliteTableCursor c;
    TEST_ASSERT_TRUE(pc_sqlite_table_cursor_begin(&c, ovf_read, NULL, OVF_PAGE_SIZE, 0, OVF_ROOTPAGE, leaf, work));
    pc_sqlite_table_cursor_set_overflow_buf(&c, tiny, sizeof(tiny));
    uint64_t rowid = 0;
    SqliteRecordCursor row;
    uint32_t n = 0;
    while (pc_sqlite_table_cursor_next(&c, &rowid, &row))
    {
        n++;
        TEST_ASSERT_EQUAL_UINT64(1, rowid); // only the non-overflowing first row survives
    }
    TEST_ASSERT_EQUAL_UINT32(1, n);
}

// ---- Writer edges ----

void test_encode_record_empty_text_and_out_cap(void)
{
    // Zero-length TEXT and BLOB columns contribute a serial type but no value bytes.
    SqliteValue cols[2];
    cols[0] = (SqliteValue){SQLITE_COL_TEXT, 0, 0, (const uint8_t *)"", 0};
    cols[1] = (SqliteValue){SQLITE_COL_BLOB, 0, 0, (const uint8_t *)"", 0};
    uint8_t rec[16];
    uint32_t rl = pc_sqlite_encode_record(cols, 2, rec, sizeof(rec));
    TEST_ASSERT_EQUAL_UINT32(3, rl); // header-size varint + two serial-type varints

    SqliteRecordCursor c;
    TEST_ASSERT_TRUE(pc_sqlite_record_begin(&c, rec, rl));
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&c, &st, &v, &vl));
    TEST_ASSERT_EQUAL_UINT64(13, st); // TEXT of length 0
    TEST_ASSERT_EQUAL_UINT32(0, vl);
    TEST_ASSERT_TRUE(pc_sqlite_record_next(&c, &st, &v, &vl));
    TEST_ASSERT_EQUAL_UINT64(12, st); // BLOB of length 0
    TEST_ASSERT_EQUAL_UINT32(0, vl);

    // Too small an output buffer fails closed.
    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_encode_record(cols, 2, rec, 2));
}

void test_encode_record_multibyte_header_size(void)
{
    // 127 columns push the record header past 127 bytes, so the header-size varint itself grows to
    // two bytes - the fixed point the encoder has to resolve.
    static SqliteValue cols[127];
    for (int i = 0; i < 127; i++)
    {
        cols[i] = (SqliteValue){SQLITE_COL_NULL, 0, 0, NULL, 0};
    }
    static uint8_t rec[256];
    uint32_t rl = pc_sqlite_encode_record(cols, 127, rec, sizeof(rec));
    TEST_ASSERT_EQUAL_UINT32(129, rl); // 2-byte header size + 127 serial types, no value bytes

    SqliteRecordCursor c;
    TEST_ASSERT_TRUE(pc_sqlite_record_begin(&c, rec, rl));
    uint64_t st = 0;
    const uint8_t *v = NULL;
    uint32_t vl = 0;
    int n = 0;
    while (pc_sqlite_record_next(&c, &st, &v, &vl))
    {
        TEST_ASSERT_EQUAL_UINT64(0, st);
        n++;
    }
    TEST_ASSERT_EQUAL_INT(127, n);
}

void test_build_table_db_input_rejects(void)
{
    static uint8_t img[2048];
    SqliteValue col = {SQLITE_COL_INT, 1, 0, NULL, 0};
    SqliteRow row = {1, &col, 1};

    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(256, "t", "CREATE TABLE t(a)", &row, 1, img, sizeof(img)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(131072, "t", "CREATE TABLE t(a)", &row, 1, img, sizeof(img)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(1000, "t", "CREATE TABLE t(a)", &row, 1, img, sizeof(img)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, NULL, "CREATE TABLE t(a)", &row, 1, img, sizeof(img)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", NULL, &row, 1, img, sizeof(img)));

    // A CREATE text so long that the schema row does not fit page 1 fails closed too.
    static char sql[421];
    memset(sql, 'x', sizeof(sql) - 1);
    sql[sizeof(sql) - 1] = 0;
    TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", sql, &row, 1, img, sizeof(img)));
}

void test_build_table_db_64k_empty_table(void)
{
    // The largest legal page size: the on-disk page-size field stores 1, and an empty page-2 leaf
    // puts the cell content start at 65536, which is stored as 0.
    static uint8_t img[131072];
    uint32_t len = pc_sqlite_build_table_db(65536, "t", "CREATE TABLE t(a)", NULL, 0, img, sizeof(img));
    TEST_ASSERT_EQUAL_UINT32(131072, len);
    TEST_ASSERT_EQUAL_HEX8(0x00, img[16]);
    TEST_ASSERT_EQUAL_HEX8(0x01, img[17]); // page-size field = 1

    SqliteDbHeader h;
    TEST_ASSERT_TRUE(pc_sqlite_parse_db_header(img, len, &h));
    TEST_ASSERT_EQUAL_UINT32(65536, h.page_size);

    SqliteBtreeHeader b;
    TEST_ASSERT_TRUE(pc_sqlite_parse_btree_header(img + 65536, 65536, 0, &b));
    TEST_ASSERT_EQUAL_UINT16(0, b.cell_count);
    TEST_ASSERT_EQUAL_UINT32(65536, b.cell_content_start); // written as 0, read back as 65536
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_db_header_real_file);
    RUN_TEST(test_db_header_rejects_bad_magic);
    RUN_TEST(test_btree_header_real_page1);
    RUN_TEST(test_btree_header_rejects_bad_type);
    RUN_TEST(test_first_cell_varints);
    RUN_TEST(test_varint_spec_vectors);
    RUN_TEST(test_serial_type_sizes);
    RUN_TEST(test_read_schema_row);
    RUN_TEST(test_column_int_signextend);
    RUN_TEST(test_leaf_cell_overflow_detection);
    RUN_TEST(test_table_cursor_multipage);
    RUN_TEST(test_overflow_read_payload);
    RUN_TEST(test_read_payload_nonoverflow);
    RUN_TEST(test_read_payload_bad_overflow_pointer);
    RUN_TEST(test_overflow_read_payload_bounds);
    RUN_TEST(test_overflow_cursor);
    RUN_TEST(test_varint_encode_roundtrip);
    RUN_TEST(test_encode_record_roundtrip);
    RUN_TEST(test_build_table_db_roundtrip);
    RUN_TEST(test_encode_record_int_widths);
    RUN_TEST(test_encode_record_blob);
    RUN_TEST(test_build_table_db_page_overflow_fails_closed);
    RUN_TEST(test_build_table_db_fails_closed);
    RUN_TEST(test_varint_decode_truncated_nine_byte);
    RUN_TEST(test_db_header_page_size_rejects);
    RUN_TEST(test_btree_header_index_pages_and_truncation);
    RUN_TEST(test_cell_pointer_rejects);
    RUN_TEST(test_leaf_cell_parse_rejects);
    RUN_TEST(test_record_begin_rejects);
    RUN_TEST(test_record_next_rejects);
    RUN_TEST(test_column_decoder_rejects);
    RUN_TEST(test_read_payload_chain_edges);
    RUN_TEST(test_cursor_descend_rejects);
    RUN_TEST(test_cursor_depth_cap);
    RUN_TEST(test_cursor_next_skips_bad_cells);
    RUN_TEST(test_cursor_parent_frame_rejects);
    RUN_TEST(test_table_cursor_page1_schema_scan);
    RUN_TEST(test_overflow_cursor_without_buffer);
    RUN_TEST(test_overflow_cursor_short_buffer_skips_row);
    RUN_TEST(test_encode_record_empty_text_and_out_cap);
    RUN_TEST(test_encode_record_multibyte_header_size);
    RUN_TEST(test_build_table_db_input_rejects);
    RUN_TEST(test_build_table_db_64k_empty_table);
    return UNITY_END();
}
