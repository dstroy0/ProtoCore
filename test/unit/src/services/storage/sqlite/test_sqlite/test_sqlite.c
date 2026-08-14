// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SQLite3 on-disk file-format reader (services/storage/sqlite/sqlite_format.h).
//
// The format is published as a field-offset table (sqlite.org/fileformat2.html sec 1.3 for the
// 100-byte database header, sec 1.6 for the b-tree page header, sec 2.1 for the varint and the
// record serial-type chart). test_fileformat_database_header_offsets is the load-bearing case: it
// reads every field of a database an authoritative sqlite3 build wrote and checks each against the
// offset, width and meaning that table assigns it, so a field read from the wrong offset or with the
// wrong endianness fails even though the file parses.

#include "db_multipage.h"
#include "db_overflow.h"
#include "services/storage/sqlite/sqlite_format.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// sec 1.3.1: "Every valid SQLite database file begins with the following 16 bytes (in hex):
// 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00", the UTF-8 "SQLite format 3" plus its NUL.
void test_fileformat_magic_header_string(void)
{
    static const uint8_t MAGIC[16] = {0x53, 0x51, 0x4c, 0x69, 0x74, 0x65, 0x20, 0x66,
                                      0x6f, 0x72, 0x6d, 0x61, 0x74, 0x20, 0x33, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAGIC, DB_MULTIPAGE, 16);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MAGIC, OVF_PAGE1, 16);

    SqliteDbHeader h;
    uint8_t bad[100];
    memcpy(bad, DB_MULTIPAGE, sizeof(bad));
    TEST_ASSERT_TRUE(protocore_sqlite_parse_db_header(bad, sizeof(bad), &h));
    bad[15] = 0x01; // the trailing NUL is part of the magic
    TEST_ASSERT_FALSE(protocore_sqlite_parse_db_header(bad, sizeof(bad), &h));
    memcpy(bad, DB_MULTIPAGE, sizeof(bad));
    bad[0] = 0x73; // lowercase 's'
    TEST_ASSERT_FALSE(protocore_sqlite_parse_db_header(bad, sizeof(bad), &h));

    // A buffer shorter than the 100-byte header cannot carry one.
    TEST_ASSERT_FALSE(protocore_sqlite_parse_db_header(DB_MULTIPAGE, 99, &h));
    TEST_ASSERT_FALSE(protocore_sqlite_parse_db_header(DB_MULTIPAGE, 0, &h));
}

// Big-endian field readers, so each expectation below can be stated as "the value at the offset the
// published table names", independently of what the parser did.
static uint16_t be16(const uint8_t *p, size_t off)
{
    return (uint16_t)((p[off] << 8) | p[off + 1]);
}

static uint32_t be32(const uint8_t *p, size_t off)
{
    return ((uint32_t)p[off] << 24) | ((uint32_t)p[off + 1] << 16) | ((uint32_t)p[off + 2] << 8) | (uint32_t)p[off + 3];
}

// The load-bearing case: sec 1.3's Database Header Format table, field by field, over a real file.
void test_fileformat_database_header_offsets(void)
{
    SqliteDbHeader h;
    TEST_ASSERT_TRUE(protocore_sqlite_parse_db_header(DB_MULTIPAGE, DB_MP_PAGE_SIZE, &h));

    TEST_ASSERT_EQUAL_UINT32(be16(DB_MULTIPAGE, 16), h.page_size); // offset 16, 2 bytes
    TEST_ASSERT_EQUAL_UINT8(DB_MULTIPAGE[18], h.write_version);    // offset 18, 1 byte
    TEST_ASSERT_EQUAL_UINT8(DB_MULTIPAGE[19], h.read_version);     // offset 19, 1 byte
    TEST_ASSERT_EQUAL_UINT8(DB_MULTIPAGE[20], h.reserved_per_page);
    TEST_ASSERT_EQUAL_UINT32(be32(DB_MULTIPAGE, 24), h.file_change_counter);
    TEST_ASSERT_EQUAL_UINT32(be32(DB_MULTIPAGE, 28), h.page_count);
    TEST_ASSERT_EQUAL_UINT32(be32(DB_MULTIPAGE, 32), h.freelist_first);
    TEST_ASSERT_EQUAL_UINT32(be32(DB_MULTIPAGE, 36), h.freelist_count);
    TEST_ASSERT_EQUAL_UINT32(be32(DB_MULTIPAGE, 40), h.schema_cookie);
    TEST_ASSERT_EQUAL_UINT32(be32(DB_MULTIPAGE, 44), h.schema_format);
    TEST_ASSERT_EQUAL_UINT32(be32(DB_MULTIPAGE, 56), h.text_encoding);
    TEST_ASSERT_EQUAL_UINT32(be32(DB_MULTIPAGE, 60), h.user_version);
    TEST_ASSERT_EQUAL_UINT32(be32(DB_MULTIPAGE, 68), h.application_id);
    TEST_ASSERT_EQUAL_UINT32(be32(DB_MULTIPAGE, 96), h.protocore_sqlite_version);

    // sec 1.3 also fixes what several of those values may be: the page size is what the fixture was
    // built with, the encoding value 1 means UTF-8, and the schema format is one of 1..4.
    TEST_ASSERT_EQUAL_UINT32(DB_MP_PAGE_SIZE, h.page_size);
    TEST_ASSERT_EQUAL_UINT32(1u, h.text_encoding);
    TEST_ASSERT_TRUE(h.schema_format >= 1u && h.schema_format <= 4u);
    // offsets 21/22/23 are fixed by the format at 64 / 32 / 32.
    TEST_ASSERT_EQUAL_UINT8(64, DB_MULTIPAGE[21]);
    TEST_ASSERT_EQUAL_UINT8(32, DB_MULTIPAGE[22]);
    TEST_ASSERT_EQUAL_UINT8(32, DB_MULTIPAGE[23]);
}

// sec 1.3.2: the page size is a power of two from 512 to 32768, and the value 1 is the magic number
// standing for 65536 (which does not fit the two-byte field).
void test_fileformat_page_size_encoding(void)
{
    uint8_t hdr[100];
    SqliteDbHeader h;
    memcpy(hdr, DB_MULTIPAGE, sizeof(hdr));

    static const uint32_t VALID[7] = {512, 1024, 2048, 4096, 8192, 16384, 32768};
    for (unsigned i = 0; i < 7; i++)
    {
        hdr[16] = (uint8_t)(VALID[i] >> 8);
        hdr[17] = (uint8_t)(VALID[i] & 0xFF);
        TEST_ASSERT_TRUE(protocore_sqlite_parse_db_header(hdr, sizeof(hdr), &h));
        TEST_ASSERT_EQUAL_UINT32(VALID[i], h.page_size);
    }

    hdr[16] = 0x00;
    hdr[17] = 0x01; // the on-disk 1
    TEST_ASSERT_TRUE(protocore_sqlite_parse_db_header(hdr, sizeof(hdr), &h));
    TEST_ASSERT_EQUAL_UINT32(65536u, h.page_size);

    static const uint16_t INVALID[4] = {0, 3, 256, 1000}; // zero, not a power of two, below 512
    for (unsigned i = 0; i < 4; i++)
    {
        hdr[16] = (uint8_t)(INVALID[i] >> 8);
        hdr[17] = (uint8_t)(INVALID[i] & 0xFF);
        TEST_ASSERT_FALSE(protocore_sqlite_parse_db_header(hdr, sizeof(hdr), &h));
    }
}

// sec 2.1: a varint is big-endian, the low seven bits of each of the first eight bytes and all eight
// bits of a ninth. Each expectation below is that definition applied by hand.
void test_fileformat_varint_decoding(void)
{
    static const struct
    {
        uint8_t enc[9];
        size_t len;
        uint64_t value;
        const char *how;
    } CASES[] = {
        {{0x00}, 1, 0ull, "0"},
        {{0x7F}, 1, 127ull, "2^7-1"},
        {{0x81, 0x00}, 2, 128ull, "(1<<7)|0"},
        {{0x81, 0x01}, 2, 129ull, "(1<<7)|1"},
        {{0xFF, 0x7F}, 2, 16383ull, "(0x7f<<7)|0x7f"},
        {{0x81, 0x80, 0x00}, 3, 16384ull, "(1<<14)"},
        {{0xFF, 0xFF, 0x7F}, 3, 2097151ull, "2^21-1"},
        {{0x81, 0x80, 0x80, 0x00}, 4, 2097152ull, "(1<<21)"},
        {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F}, 8, 0x00FFFFFFFFFFFFFFull, "2^56-1"},
        // The ninth byte contributes all eight of its bits, so eight 0x7f septets plus 0xff is 2^64-1.
        {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 9, 0xFFFFFFFFFFFFFFFFull, "2^64-1"},
        // In a nine-byte varint the first byte's septet is the most significant, sitting above the
        // seven septets and the whole ninth byte below it: 7*7 + 8 = 57 bits, so its low bit is 2^57.
        {{0x81, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00}, 9, 0x0200000000000000ull, "1<<57"},
    };
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint64_t v = 0xA5A5A5A5A5A5A5A5ull;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)CASES[i].len,
                                         (uint32_t)protocore_sqlite_varint_decode(CASES[i].enc, 9, &v), CASES[i].how);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(CASES[i].value, v, CASES[i].how);
    }

    // A buffer that ends while the continuation bit is still set holds no complete varint.
    static const uint8_t TRUNC[3] = {0x81, 0x80, 0x80};
    uint64_t v = 0;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_sqlite_varint_decode(TRUNC, 1, &v));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_sqlite_varint_decode(TRUNC, 3, &v));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_sqlite_varint_decode(TRUNC, 0, &v));
}

// sec 2.1: "A varint is between 1 and 9 bytes in length ... whichever is shorter", so a value below
// 2^(7n) takes n bytes for n up to 8 and anything at or above 2^56 takes nine. Encode then decode
// must be the identity at each of those boundaries.
void test_fileformat_varint_length_boundaries(void)
{
    static const struct
    {
        uint64_t value;
        size_t len;
    } CASES[] = {
        {0ull, 1},       {(1ull << 7) - 1, 1},       {1ull << 7, 2},  {(1ull << 14) - 1, 2},
        {1ull << 14, 3}, {(1ull << 21) - 1, 3},      {1ull << 21, 4}, {(1ull << 28) - 1, 4},
        {1ull << 28, 5}, {(1ull << 35) - 1, 5},      {1ull << 35, 6}, {(1ull << 42) - 1, 6},
        {1ull << 42, 7}, {(1ull << 49) - 1, 7},      {1ull << 49, 8}, {(1ull << 56) - 1, 8},
        {1ull << 56, 9}, {0xFFFFFFFFFFFFFFFFull, 9},
    };
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t enc[9];
        size_t n = protocore_sqlite_varint_encode(CASES[i].value, enc, sizeof(enc));
        TEST_ASSERT_EQUAL_UINT32((uint32_t)CASES[i].len, (uint32_t)n);

        uint64_t back = 0;
        TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)protocore_sqlite_varint_decode(enc, n, &back));
        TEST_ASSERT_EQUAL_UINT64(CASES[i].value, back);

        // A buffer one byte short of the encoding is refused rather than written into.
        TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_sqlite_varint_encode(CASES[i].value, enc, n - 1));
    }
}

// sec 2.1's "Serial Type Codes Of The Record Format" chart, entry by entry.
void test_fileformat_serial_type_content_sizes(void)
{
    TEST_ASSERT_EQUAL_UINT64(0ull, protocore_sqlite_serial_type_size(0));  // NULL
    TEST_ASSERT_EQUAL_UINT64(1ull, protocore_sqlite_serial_type_size(1));  // 8-bit int
    TEST_ASSERT_EQUAL_UINT64(2ull, protocore_sqlite_serial_type_size(2));  // 16-bit
    TEST_ASSERT_EQUAL_UINT64(3ull, protocore_sqlite_serial_type_size(3));  // 24-bit
    TEST_ASSERT_EQUAL_UINT64(4ull, protocore_sqlite_serial_type_size(4));  // 32-bit
    TEST_ASSERT_EQUAL_UINT64(6ull, protocore_sqlite_serial_type_size(5));  // 48-bit
    TEST_ASSERT_EQUAL_UINT64(8ull, protocore_sqlite_serial_type_size(6));  // 64-bit
    TEST_ASSERT_EQUAL_UINT64(8ull, protocore_sqlite_serial_type_size(7));  // IEEE 754 double
    TEST_ASSERT_EQUAL_UINT64(0ull, protocore_sqlite_serial_type_size(8));  // the constant 0
    TEST_ASSERT_EQUAL_UINT64(0ull, protocore_sqlite_serial_type_size(9));  // the constant 1
    TEST_ASSERT_EQUAL_UINT64(0ull, protocore_sqlite_serial_type_size(10)); // reserved
    TEST_ASSERT_EQUAL_UINT64(0ull, protocore_sqlite_serial_type_size(11)); // reserved

    // N >= 12 and even: a BLOB of (N-12)/2 bytes. N >= 13 and odd: TEXT of (N-13)/2.
    TEST_ASSERT_EQUAL_UINT64(0ull, protocore_sqlite_serial_type_size(12));
    TEST_ASSERT_EQUAL_UINT64(0ull, protocore_sqlite_serial_type_size(13));
    TEST_ASSERT_EQUAL_UINT64(1ull, protocore_sqlite_serial_type_size(14));
    TEST_ASSERT_EQUAL_UINT64(1ull, protocore_sqlite_serial_type_size(15));
    TEST_ASSERT_EQUAL_UINT64(494ull, protocore_sqlite_serial_type_size(1000)); // (1000-12)/2
    TEST_ASSERT_EQUAL_UINT64(494ull, protocore_sqlite_serial_type_size(1001)); // (1001-13)/2
}

// sec 1.6's B-tree Page Header Format table, read off page 1 of a real file (where the b-tree header
// starts at 100, after the database header) and off a real interior root page.
void test_fileformat_btree_page_header_offsets(void)
{
    SqliteBtreeHeader b;
    TEST_ASSERT_TRUE(protocore_sqlite_parse_btree_header(DB_MULTIPAGE, DB_MP_PAGE_SIZE, 100, &b));
    TEST_ASSERT_EQUAL_UINT8(DB_MULTIPAGE[100], b.type);
    TEST_ASSERT_EQUAL_UINT8(SQLITE_BTREE_LEAF_TABLE, b.type);
    TEST_ASSERT_EQUAL_UINT16(be16(DB_MULTIPAGE, 101), b.first_freeblock);
    TEST_ASSERT_EQUAL_UINT16(be16(DB_MULTIPAGE, 103), b.cell_count);
    TEST_ASSERT_EQUAL_UINT32(be16(DB_MULTIPAGE, 105), b.cell_content_start);
    TEST_ASSERT_EQUAL_UINT8(DB_MULTIPAGE[107], b.frag_free_bytes);
    TEST_ASSERT_EQUAL_UINT8(8, b.header_size); // a leaf header is 8 bytes
    TEST_ASSERT_EQUAL_UINT32(0, b.right_most_page);

    // The overflow fixture's table root is an interior table page: 12-byte header with the
    // right-most child pointer at offset 8.
    SqliteBtreeHeader r;
    TEST_ASSERT_TRUE(protocore_sqlite_parse_btree_header(OVF_PAGES[OVF_ROOTPAGE - 1], OVF_PAGE_SIZE, 0, &r));
    TEST_ASSERT_EQUAL_UINT8(SQLITE_BTREE_INTERIOR_TABLE, r.type);
    TEST_ASSERT_EQUAL_UINT8(12, r.header_size);
    TEST_ASSERT_EQUAL_UINT32(be32(OVF_PAGES[OVF_ROOTPAGE - 1], 8), r.right_most_page);
    TEST_ASSERT_GREATER_THAN_UINT32(0, r.right_most_page);
}

// sec 1.6: "Any other value for the b-tree page type is an error", and a header that runs past the
// page is not a header.
void test_fileformat_btree_page_type_domain(void)
{
    uint8_t page[512];
    memcpy(page, OVF_PAGES[OVF_ROOTPAGE - 1], sizeof(page));
    SqliteBtreeHeader b;
    for (unsigned t = 0; t < 256; t++)
    {
        page[0] = (uint8_t)t;
        proto_bool want = (t == SQLITE_BTREE_INTERIOR_INDEX || t == SQLITE_BTREE_INTERIOR_TABLE ||
                           t == SQLITE_BTREE_LEAF_INDEX || t == SQLITE_BTREE_LEAF_TABLE);
        TEST_ASSERT_EQUAL_INT((int)want, (int)protocore_sqlite_parse_btree_header(page, sizeof(page), 0, &b));
    }

    // An interior page needs 12 bytes of header; 11 is short.
    page[0] = SQLITE_BTREE_INTERIOR_TABLE;
    TEST_ASSERT_FALSE(protocore_sqlite_parse_btree_header(page, 11, 0, &b));
    TEST_ASSERT_TRUE(protocore_sqlite_parse_btree_header(page, 12, 0, &b));
    page[0] = SQLITE_BTREE_LEAF_TABLE;
    TEST_ASSERT_FALSE(protocore_sqlite_parse_btree_header(page, 7, 0, &b));
    TEST_ASSERT_TRUE(protocore_sqlite_parse_btree_header(page, 8, 0, &b));
}

// sec 1.6: "A zero value for this integer is interpreted as 65536" for the cell content area start.
void test_fileformat_cell_content_start_zero_means_65536(void)
{
    uint8_t page[512];
    memcpy(page, OVF_PAGES[OVF_ROOTPAGE - 1], sizeof(page));
    page[5] = 0x00;
    page[6] = 0x00;
    SqliteBtreeHeader b;
    TEST_ASSERT_TRUE(protocore_sqlite_parse_btree_header(page, sizeof(page), 0, &b));
    TEST_ASSERT_EQUAL_UINT32(65536u, b.cell_content_start);
}

// sec 1.6: "The cell pointer array of a b-tree page immediately follows the b-tree page header ...
// K 2-byte integer offsets to the cell contents."
void test_fileformat_cell_pointer_array(void)
{
    SqliteBtreeHeader b;
    TEST_ASSERT_TRUE(protocore_sqlite_parse_btree_header(DB_MULTIPAGE, DB_MP_PAGE_SIZE, 100, &b));
    for (uint16_t i = 0; i < b.cell_count; i++)
    {
        uint32_t want = be16(DB_MULTIPAGE, 100 + b.header_size + 2u * i);
        TEST_ASSERT_EQUAL_UINT32(want, protocore_sqlite_cell_pointer(DB_MULTIPAGE, DB_MP_PAGE_SIZE, &b, 100, i));
    }
    // An index at or past the cell count names no cell.
    TEST_ASSERT_EQUAL_UINT32(0, protocore_sqlite_cell_pointer(DB_MULTIPAGE, DB_MP_PAGE_SIZE, &b, 100, b.cell_count));
    // A page truncated before the pointer array cannot answer either.
    TEST_ASSERT_EQUAL_UINT32(0, protocore_sqlite_cell_pointer(DB_MULTIPAGE, 100 + b.header_size + 1, &b, 100, 0));
}

// sec 2.6: page 1's b-tree is sqlite_schema, whose columns are type, name, tbl_name, rootpage, sql.
// Reading the fixture's one schema row exercises the leaf cell, the record header and every column
// decoder against a row an authoritative writer laid out.
static uint32_t schema_rootpage(const uint8_t *page1, uint32_t page_size, const char *want_name)
{
    SqliteBtreeHeader b;
    TEST_ASSERT_TRUE(protocore_sqlite_parse_btree_header(page1, page_size, 100, &b));
    for (uint16_t i = 0; i < b.cell_count; i++)
    {
        uint32_t off = protocore_sqlite_cell_pointer(page1, page_size, &b, 100, i);
        TEST_ASSERT_GREATER_THAN_UINT32(0, off);
        SqliteTableLeafCell cell;
        TEST_ASSERT_TRUE(protocore_sqlite_parse_table_leaf_cell(page1, page_size, page_size, 0, off, &cell));
        TEST_ASSERT_FALSE(cell.has_overflow);

        SqliteRecordCursor row;
        TEST_ASSERT_TRUE(protocore_sqlite_record_begin(&row, page1 + cell.local_off, cell.local_len));

        uint64_t st = 0;
        const uint8_t *val = NULL;
        uint32_t vlen = 0;
        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen)); // type
        TEST_ASSERT_EQUAL_UINT32(5, vlen);
        TEST_ASSERT_EQUAL_MEMORY("table", val, 5);
        TEST_ASSERT_EQUAL_UINT64((uint64_t)(13 + 2 * 5), st); // TEXT of 5 bytes

        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen)); // name
        if (vlen != strlen(want_name) || memcmp(val, want_name, vlen) != 0)
        {
            continue;
        }
        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen)); // tbl_name
        TEST_ASSERT_EQUAL_MEMORY(want_name, val, vlen);
        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen)); // rootpage
        int64_t root = protocore_sqlite_column_int(st, val, vlen);
        TEST_ASSERT_GREATER_THAN_INT64(1, root);                                // page 1 is the schema itself
        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen)); // sql
        TEST_ASSERT_TRUE(st >= 13 && (st % 2) == 1);
        TEST_ASSERT_EQUAL_MEMORY("CREATE TABLE ", val, 13);
        TEST_ASSERT_FALSE(protocore_sqlite_record_next(&row, &st, &val, &vlen)); // five columns only
        return (uint32_t)root;
    }
    TEST_FAIL_MESSAGE("no schema row for the table");
    return 0;
}

void test_fileformat_schema_row(void)
{
    TEST_ASSERT_EQUAL_UINT32(2u, schema_rootpage(DB_MULTIPAGE, DB_MP_PAGE_SIZE, "t"));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)OVF_ROOTPAGE, schema_rootpage(OVF_PAGE1, OVF_PAGE_SIZE, "big"));
}

// sec 2.1: serial types 1-6 are big-endian twos-complement integers of 1/2/3/4/6/8 bytes, and 8 and
// 9 are the constants 0 and 1 carrying no content bytes.
void test_fileformat_column_int_is_signextended_big_endian(void)
{
    static const uint8_t NEG1[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_INT64(-1, protocore_sqlite_column_int(1, NEG1, 1));
    TEST_ASSERT_EQUAL_INT64(-1, protocore_sqlite_column_int(2, NEG1, 2));
    TEST_ASSERT_EQUAL_INT64(-1, protocore_sqlite_column_int(3, NEG1, 3));
    TEST_ASSERT_EQUAL_INT64(-1, protocore_sqlite_column_int(4, NEG1, 4));
    TEST_ASSERT_EQUAL_INT64(-1, protocore_sqlite_column_int(5, NEG1, 6));
    TEST_ASSERT_EQUAL_INT64(-1, protocore_sqlite_column_int(6, NEG1, 8));

    static const uint8_t POS[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02};
    TEST_ASSERT_EQUAL_INT64(0x02, protocore_sqlite_column_int(1, POS + 7, 1));
    TEST_ASSERT_EQUAL_INT64(0x0102, protocore_sqlite_column_int(2, POS + 6, 2));
    TEST_ASSERT_EQUAL_INT64(0x0102, protocore_sqlite_column_int(3, POS + 5, 3));
    TEST_ASSERT_EQUAL_INT64(0x0102, protocore_sqlite_column_int(6, POS, 8));

    // The most negative 8-bit value, and the sign bit at each width boundary.
    static const uint8_t MIN8[1] = {0x80};
    TEST_ASSERT_EQUAL_INT64(-128, protocore_sqlite_column_int(1, MIN8, 1));
    static const uint8_t MIN16[2] = {0x80, 0x00};
    TEST_ASSERT_EQUAL_INT64(-32768, protocore_sqlite_column_int(2, MIN16, 2));
    static const uint8_t MIN24[3] = {0x80, 0x00, 0x00};
    TEST_ASSERT_EQUAL_INT64(-8388608, protocore_sqlite_column_int(3, MIN24, 3));

    TEST_ASSERT_EQUAL_INT64(0, protocore_sqlite_column_int(8, NULL, 0));
    TEST_ASSERT_EQUAL_INT64(1, protocore_sqlite_column_int(9, NULL, 0));
    TEST_ASSERT_EQUAL_INT64(0, protocore_sqlite_column_int(0, NULL, 0)); // NULL
}

// sec 2.1: serial type 7 is a big-endian IEEE 754-2008 64-bit float. The bit patterns below are the
// IEEE 754 encodings of 1.0 (sign 0, exponent 1023 = 0x3FF, zero mantissa) and -2.0 (sign 1,
// exponent 1024 = 0x400, zero mantissa).
void test_fileformat_column_float_is_big_endian_ieee754(void)
{
    static const uint8_t ONE[8] = {0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t MINUS_TWO[8] = {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t ZERO[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(protocore_sqlite_column_float(ONE, 8) == 1.0);
    TEST_ASSERT_TRUE(protocore_sqlite_column_float(MINUS_TWO, 8) == -2.0);
    TEST_ASSERT_TRUE(protocore_sqlite_column_float(ZERO, 8) == 0.0);
    // A value that is not eight bytes is not a serial-type-7 value.
    TEST_ASSERT_TRUE(protocore_sqlite_column_float(ONE, 7) == 0.0);
    TEST_ASSERT_TRUE(protocore_sqlite_column_float(ONE, 0) == 0.0);
}

// --- reading whole tables through the page reader ----------------------------------------------

typedef struct
{
    const uint8_t *image;
    uint32_t pages;
} MemDb;

static proto_bool mem_read(void *ctx, uint32_t pgno, uint8_t *page, uint32_t page_size)
{
    const MemDb *db = (const MemDb *)ctx;
    if (pgno < 1 || pgno > db->pages)
    {
        return PROTO_FALSE;
    }
    memcpy(page, db->image + (size_t)(pgno - 1) * page_size, page_size);
    return PROTO_TRUE;
}

// The multipage fixture holds 40 rows of t(a INTEGER, b TEXT) with a = 1..40 and b built from a, so
// walking the table must yield rowids 1..40 in order with the first column equal to the rowid.
void test_table_cursor_walks_every_row_in_rowid_order(void)
{
    MemDb db = {DB_MULTIPAGE, sizeof(DB_MULTIPAGE) / DB_MP_PAGE_SIZE};
    uint8_t leaf[512];
    uint8_t work[512];
    SqliteTableCursor c;
    uint32_t root = schema_rootpage(DB_MULTIPAGE, DB_MP_PAGE_SIZE, "t");
    TEST_ASSERT_TRUE(protocore_sqlite_table_cursor_begin(&c, mem_read, &db, DB_MP_PAGE_SIZE, 0, root, leaf, work));

    uint32_t seen = 0;
    uint64_t rowid = 0;
    SqliteRecordCursor row;
    uint64_t prev = 0;
    while (protocore_sqlite_table_cursor_next(&c, &rowid, &row))
    {
        seen++;
        TEST_ASSERT_GREATER_THAN_UINT64(prev, rowid); // strictly ascending rowids
        prev = rowid;

        uint64_t st = 0;
        const uint8_t *val = NULL;
        uint32_t vlen = 0;
        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen));
        TEST_ASSERT_EQUAL_INT64((int64_t)rowid, protocore_sqlite_column_int(st, val, vlen));

        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen));
        TEST_ASSERT_TRUE(st >= 13 && (st % 2) == 1); // TEXT
        TEST_ASSERT_EQUAL_UINT32((uint32_t)((st - 13) / 2), vlen);
        TEST_ASSERT_EQUAL_MEMORY("row", val, 3);
    }
    TEST_ASSERT_EQUAL_UINT32(DB_MP_ROWS, seen);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)DB_MP_ROWS, prev);
}

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

// sec 1.6: a row too large for its leaf page keeps a prefix there and continues on a chain of
// overflow pages, each a 4-byte next-page pointer plus content. With a reassembly buffer the cursor
// must hand back the whole value; without one it flags the row and yields only the prefix.
void test_overflow_chain_reassembly(void)
{
    uint8_t leaf[OVF_PAGE_SIZE];
    uint8_t work[OVF_PAGE_SIZE];
    uint8_t big[OVF_ROW3_LEN + 64];
    SqliteTableCursor c;
    TEST_ASSERT_TRUE(
        protocore_sqlite_table_cursor_begin(&c, ovf_read, NULL, OVF_PAGE_SIZE, 0, OVF_ROOTPAGE, leaf, work));
    protocore_sqlite_table_cursor_set_overflow_buf(&c, big, sizeof(big));

    static const struct
    {
        uint32_t len;
        char fill;
    } WANT[3] = {{5, 0}, {OVF_ROW2_LEN, 'A'}, {OVF_ROW3_LEN, 'B'}};

    uint64_t rowid = 0;
    SqliteRecordCursor row;
    unsigned i = 0;
    while (protocore_sqlite_table_cursor_next(&c, &rowid, &row))
    {
        TEST_ASSERT_LESS_THAN_UINT32(3, i);
        TEST_ASSERT_EQUAL_UINT64((uint64_t)(i + 1), rowid);

        uint64_t st = 0;
        const uint8_t *val = NULL;
        uint32_t vlen = 0;
        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen)); // id
        TEST_ASSERT_EQUAL_INT64((int64_t)rowid, protocore_sqlite_column_int(st, val, vlen));

        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen)); // data
        TEST_ASSERT_EQUAL_UINT64((uint64_t)(13 + 2 * WANT[i].len), st);
        TEST_ASSERT_EQUAL_UINT32(WANT[i].len, vlen);
        if (WANT[i].fill)
        {
            for (uint32_t k = 0; k < vlen; k++)
            {
                TEST_ASSERT_EQUAL_CHAR(WANT[i].fill, (char)val[k]);
            }
        }
        else
        {
            TEST_ASSERT_EQUAL_MEMORY(OVF_ROW1, val, vlen);
        }
        i++;
    }
    TEST_ASSERT_EQUAL_UINT32(3, i);
}

// Without a reassembly buffer an overflowing row still parses, but only its in-page prefix is
// present, so the value is shorter than the serial type announces.
void test_overflow_without_a_buffer_yields_the_prefix(void)
{
    uint8_t leaf[OVF_PAGE_SIZE];
    uint8_t work[OVF_PAGE_SIZE];
    SqliteTableCursor c;
    TEST_ASSERT_TRUE(
        protocore_sqlite_table_cursor_begin(&c, ovf_read, NULL, OVF_PAGE_SIZE, 0, OVF_ROOTPAGE, leaf, work));

    uint64_t rowid = 0;
    SqliteRecordCursor row;
    proto_bool saw_truncated = PROTO_FALSE;
    while (protocore_sqlite_table_cursor_next(&c, &rowid, &row))
    {
        uint64_t st = 0;
        const uint8_t *val = NULL;
        uint32_t vlen = 0;
        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen));
        if (!protocore_sqlite_record_next(&row, &st, &val, &vlen))
        {
            saw_truncated = PROTO_TRUE; // the text column did not fit the prefix at all
            continue;
        }
        if (st >= 13 && (st % 2) == 1 && vlen < (uint32_t)((st - 13) / 2))
        {
            saw_truncated = PROTO_TRUE;
        }
    }
    TEST_ASSERT_TRUE(saw_truncated);
}

// A page reader that fails leaves the cursor unopened rather than reading uninitialized memory.
void test_table_cursor_refuses_an_unreadable_root(void)
{
    MemDb db = {DB_MULTIPAGE, sizeof(DB_MULTIPAGE) / DB_MP_PAGE_SIZE};
    uint8_t leaf[512];
    uint8_t work[512];
    SqliteTableCursor c;
    TEST_ASSERT_FALSE(protocore_sqlite_table_cursor_begin(&c, mem_read, &db, DB_MP_PAGE_SIZE, 0, 999, leaf, work));
}

// sec 1.2: page 1 is always a table b-tree, the root of sqlite_schema, and its b-tree header sits
// after the 100-byte database header. Rooting the cursor there walks the schema table itself.
void test_table_cursor_walks_the_schema_table_on_page_one(void)
{
    MemDb db = {DB_MULTIPAGE, sizeof(DB_MULTIPAGE) / DB_MP_PAGE_SIZE};
    uint8_t leaf[512];
    uint8_t work[512];
    SqliteTableCursor c;
    TEST_ASSERT_TRUE(protocore_sqlite_table_cursor_begin(&c, mem_read, &db, DB_MP_PAGE_SIZE, 0, 1, leaf, work));

    uint64_t rowid = 0;
    SqliteRecordCursor row;
    unsigned rows = 0;
    while (protocore_sqlite_table_cursor_next(&c, &rowid, &row))
    {
        uint64_t st = 0;
        const uint8_t *val = NULL;
        uint32_t vlen = 0;
        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen));
        TEST_ASSERT_EQUAL_MEMORY("table", val, 5); // the fixture's only schema object
        rows++;
    }
    TEST_ASSERT_EQUAL_UINT32(1, rows);
}

// --- the bounded writer -------------------------------------------------------------------------

// sec 2.1: a record is a header (its own size as a varint, then one serial-type varint per column)
// followed by the value bytes. Encoding then reading back must round-trip, and each column must
// carry the serial type the chart assigns its value.
void test_encode_record_round_trip(void)
{
    static const uint8_t BLOB[3] = {0xDE, 0xAD, 0xBE};
    SqliteValue cols[6];
    cols[0].type = SQLITE_COL_NULL;
    cols[1].type = SQLITE_COL_INT;
    cols[1].i = -1;
    cols[2].type = SQLITE_COL_INT;
    cols[2].i = 1;
    cols[3].type = SQLITE_COL_FLOAT;
    cols[3].f = -2.0;
    cols[4].type = SQLITE_COL_TEXT;
    cols[4].data = (const uint8_t *)"hello";
    cols[4].len = 5;
    cols[5].type = SQLITE_COL_BLOB;
    cols[5].data = BLOB;
    cols[5].len = sizeof(BLOB);

    uint8_t rec[64];
    uint32_t n = protocore_sqlite_encode_record(cols, 6, rec, sizeof(rec));
    TEST_ASSERT_GREATER_THAN_UINT32(0, n);

    SqliteRecordCursor c;
    TEST_ASSERT_TRUE(protocore_sqlite_record_begin(&c, rec, n));
    uint64_t st = 0;
    const uint8_t *val = NULL;
    uint32_t vlen = 0;

    TEST_ASSERT_TRUE(protocore_sqlite_record_next(&c, &st, &val, &vlen));
    TEST_ASSERT_EQUAL_UINT64(0ull, st); // NULL
    TEST_ASSERT_EQUAL_UINT32(0, vlen);

    TEST_ASSERT_TRUE(protocore_sqlite_record_next(&c, &st, &val, &vlen));
    TEST_ASSERT_EQUAL_UINT64(1ull, st); // -1 fits an 8-bit twos-complement int
    TEST_ASSERT_EQUAL_INT64(-1, protocore_sqlite_column_int(st, val, vlen));

    TEST_ASSERT_TRUE(protocore_sqlite_record_next(&c, &st, &val, &vlen));
    TEST_ASSERT_EQUAL_UINT64(9ull, st); // the constant 1, zero content bytes
    TEST_ASSERT_EQUAL_UINT32(0, vlen);
    TEST_ASSERT_EQUAL_INT64(1, protocore_sqlite_column_int(st, val, vlen));

    TEST_ASSERT_TRUE(protocore_sqlite_record_next(&c, &st, &val, &vlen));
    TEST_ASSERT_EQUAL_UINT64(7ull, st); // IEEE 754 double
    TEST_ASSERT_EQUAL_UINT32(8, vlen);
    TEST_ASSERT_TRUE(protocore_sqlite_column_float(val, vlen) == -2.0);
    // -2.0 is sign 1, exponent 1024, zero mantissa: 0xC000000000000000, stored big-endian.
    static const uint8_t MINUS_TWO_BE[8] = {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MINUS_TWO_BE, val, 8);

    TEST_ASSERT_TRUE(protocore_sqlite_record_next(&c, &st, &val, &vlen));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(13 + 2 * 5), st);
    TEST_ASSERT_EQUAL_MEMORY("hello", val, 5);

    TEST_ASSERT_TRUE(protocore_sqlite_record_next(&c, &st, &val, &vlen));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(12 + 2 * 3), st);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BLOB, val, 3);

    TEST_ASSERT_FALSE(protocore_sqlite_record_next(&c, &st, &val, &vlen));
}

// The chart's integer widths: the smallest serial type that holds the value, at each boundary of the
// 8/16/24/32/48/64-bit twos-complement ranges.
void test_encode_record_picks_the_narrowest_integer_type(void)
{
    static const struct
    {
        int64_t v;
        uint64_t st;
    } CASES[] = {
        {0, 8},
        {1, 9},
        {2, 1},
        {-1, 1},
        {127, 1},
        {-128, 1},
        {128, 2},
        {-129, 2},
        {32767, 2},
        {-32768, 2},
        {32768, 3},
        {8388607, 3},
        {-8388608, 3},
        {8388608, 4},
        {2147483647, 4},
        {-2147483648LL, 4},
        {2147483648LL, 5},
        {140737488355327LL, 5},
        {140737488355328LL, 6},
        {-9223372036854775807LL - 1, 6},
    };
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        SqliteValue col;
        col.type = SQLITE_COL_INT;
        col.i = CASES[i].v;
        uint8_t rec[32];
        uint32_t n = protocore_sqlite_encode_record(&col, 1, rec, sizeof(rec));
        TEST_ASSERT_GREATER_THAN_UINT32(0, n);

        SqliteRecordCursor c;
        TEST_ASSERT_TRUE(protocore_sqlite_record_begin(&c, rec, n));
        uint64_t st = 0;
        const uint8_t *val = NULL;
        uint32_t vlen = 0;
        TEST_ASSERT_TRUE(protocore_sqlite_record_next(&c, &st, &val, &vlen));
        TEST_ASSERT_EQUAL_UINT64(CASES[i].st, st);
        TEST_ASSERT_EQUAL_UINT64(protocore_sqlite_serial_type_size(st), (uint64_t)vlen);
        TEST_ASSERT_EQUAL_INT64(CASES[i].v, protocore_sqlite_column_int(st, val, vlen));
    }
}

// The writer's own output must satisfy the published format: the magic string, the page size at
// offset 16, a schema row on page 1 and a leaf-table b-tree on page 2 carrying the rows.
void test_build_table_db_writes_a_readable_file(void)
{
    static const uint8_t B[4] = {1, 2, 3, 4};
    SqliteValue r0[3];
    r0[0].type = SQLITE_COL_INT;
    r0[0].i = 42;
    r0[1].type = SQLITE_COL_TEXT;
    r0[1].data = (const uint8_t *)"hello";
    r0[1].len = 5;
    r0[2].type = SQLITE_COL_BLOB;
    r0[2].data = B;
    r0[2].len = sizeof(B);

    SqliteValue r1[3];
    r1[0].type = SQLITE_COL_INT;
    r1[0].i = -7;
    r1[1].type = SQLITE_COL_TEXT;
    r1[1].data = (const uint8_t *)"world";
    r1[1].len = 5;
    r1[2].type = SQLITE_COL_NULL;

    SqliteRow rows[2] = {{1, r0, 3}, {2, r1, 3}};
    static uint8_t image[2 * 1024];
    uint32_t n = protocore_sqlite_build_table_db(1024, "t", "CREATE TABLE t(a,b,c)", rows, 2, image, sizeof(image));
    TEST_ASSERT_EQUAL_UINT32(2u * 1024u, n);

    SqliteDbHeader h;
    TEST_ASSERT_TRUE(protocore_sqlite_parse_db_header(image, n, &h));
    TEST_ASSERT_EQUAL_UINT32(1024u, h.page_size);
    TEST_ASSERT_EQUAL_UINT32(be16(image, 16), h.page_size);
    TEST_ASSERT_EQUAL_UINT32(2u, h.page_count);
    TEST_ASSERT_EQUAL_UINT32(1u, h.text_encoding); // UTF-8

    TEST_ASSERT_EQUAL_UINT32(2u, schema_rootpage(image, 1024, "t"));

    MemDb db = {image, 2};
    uint8_t leaf[1024];
    uint8_t work[1024];
    SqliteTableCursor c;
    TEST_ASSERT_TRUE(protocore_sqlite_table_cursor_begin(&c, mem_read, &db, 1024, 0, 2, leaf, work));

    uint64_t rowid = 0;
    SqliteRecordCursor row;
    TEST_ASSERT_TRUE(protocore_sqlite_table_cursor_next(&c, &rowid, &row));
    TEST_ASSERT_EQUAL_UINT64(1ull, rowid);
    uint64_t st = 0;
    const uint8_t *val = NULL;
    uint32_t vlen = 0;
    TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen));
    TEST_ASSERT_EQUAL_INT64(42, protocore_sqlite_column_int(st, val, vlen));
    TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen));
    TEST_ASSERT_EQUAL_MEMORY("hello", val, 5);
    TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(12 + 2 * 4), st);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(B, val, 4);

    TEST_ASSERT_TRUE(protocore_sqlite_table_cursor_next(&c, &rowid, &row));
    TEST_ASSERT_EQUAL_UINT64(2ull, rowid);
    TEST_ASSERT_TRUE(protocore_sqlite_record_next(&row, &st, &val, &vlen));
    TEST_ASSERT_EQUAL_INT64(-7, protocore_sqlite_column_int(st, val, vlen));
    TEST_ASSERT_FALSE(protocore_sqlite_table_cursor_next(&c, &rowid, &row));
}

// The writer is bounded by contract: an output buffer smaller than two pages, a page size that is
// not a power of two in range, and rows that will not fit one leaf page all fail closed.
void test_build_table_db_fails_closed(void)
{
    SqliteValue col;
    col.type = SQLITE_COL_INT;
    col.i = 1;
    SqliteRow row = {1, &col, 1};
    static uint8_t image[4096];

    TEST_ASSERT_EQUAL_UINT32(0, protocore_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", &row, 1, image, 1023));
    TEST_ASSERT_EQUAL_UINT32(
        0, protocore_sqlite_build_table_db(500, "t", "CREATE TABLE t(a)", &row, 1, image, sizeof(image)));
    TEST_ASSERT_EQUAL_UINT32(
        0, protocore_sqlite_build_table_db(512, NULL, "CREATE TABLE t(a)", &row, 1, image, sizeof(image)));
    TEST_ASSERT_EQUAL_UINT32(0, protocore_sqlite_build_table_db(512, "t", NULL, &row, 1, image, sizeof(image)));

    // One 512-byte leaf page cannot hold a row far larger than itself.
    static uint8_t huge[4000];
    memset(huge, 'x', sizeof(huge));
    SqliteValue big;
    big.type = SQLITE_COL_TEXT;
    big.data = huge;
    big.len = sizeof(huge);
    SqliteRow bigrow = {1, &big, 1};
    TEST_ASSERT_EQUAL_UINT32(
        0, protocore_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", &bigrow, 1, image, sizeof(image)));

    // An empty table is legal.
    TEST_ASSERT_EQUAL_UINT32(
        1024u, protocore_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", NULL, 0, image, sizeof(image)));
}

// A record whose header size varint points past the record, or whose columns run past its end, is
// refused rather than read out of bounds.
void test_record_cursor_refuses_a_malformed_record(void)
{
    SqliteRecordCursor c;
    static const uint8_t HDR_TOO_BIG[3] = {0x20, 0x01, 0x02}; // header claims 32 bytes of 3
    TEST_ASSERT_FALSE(protocore_sqlite_record_begin(&c, HDR_TOO_BIG, sizeof(HDR_TOO_BIG)));
    static const uint8_t EMPTY[1] = {0x00}; // a header size of 0 is not even its own varint
    TEST_ASSERT_FALSE(protocore_sqlite_record_begin(&c, EMPTY, sizeof(EMPTY)));
    TEST_ASSERT_FALSE(protocore_sqlite_record_begin(&c, HDR_TOO_BIG, 0));

    // Header size 2 (itself plus one serial type), a TEXT column of 5 bytes, but no value bytes.
    static const uint8_t SHORT_VALUE[2] = {0x02, 0x17};
    TEST_ASSERT_TRUE(protocore_sqlite_record_begin(&c, SHORT_VALUE, sizeof(SHORT_VALUE)));
    uint64_t st = 0;
    const uint8_t *val = NULL;
    uint32_t vlen = 0;
    TEST_ASSERT_FALSE(protocore_sqlite_record_next(&c, &st, &val, &vlen));
}

// A leaf cell whose varints or payload run past the page is refused.
void test_leaf_cell_refuses_a_truncated_cell(void)
{
    SqliteTableLeafCell cell;
    uint8_t page[512];
    memset(page, 0, sizeof(page));

    // A payload length far larger than the page, at the very end of it.
    page[510] = 0xFF;
    page[511] = 0x7F;
    TEST_ASSERT_FALSE(protocore_sqlite_parse_table_leaf_cell(page, sizeof(page), 512, 0, 510, &cell));
    // A cell offset at or past the page has no cell.
    TEST_ASSERT_FALSE(protocore_sqlite_parse_table_leaf_cell(page, sizeof(page), 512, 0, 512, &cell));
    TEST_ASSERT_FALSE(protocore_sqlite_parse_table_leaf_cell(page, sizeof(page), 512, 0, 4096, &cell));
}
