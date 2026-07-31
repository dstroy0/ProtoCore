// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_sqlite_format.cpp
 * @brief SQLite3 on-disk file-format parsers (see pc_sqlite_format.h).
 */

#include "services/storage/sqlite/sqlite_format.h"

#if PC_ENABLE_SQLITE

#include <string.h>

namespace
{
uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
// The 16-byte file magic, including the trailing NUL.
const char SQLITE_MAGIC[16] = {'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f', 'o', 'r', 'm', 'a', 't', ' ', '3', '\0'};

bool is_pow2(uint32_t v)
{
    return v && (v & (v - 1)) == 0; // GCOVR_EXCL_BR_LINE  the sole caller guards page_size>=512, so v==0 never reached
}
} // namespace

size_t pc_sqlite_varint_decode(const uint8_t *buf, size_t len, uint64_t *out)
{
    uint64_t v = 0;
    for (size_t i = 0; i < 8; i++)
    {
        if (i >= len)
        {
            return 0; // incomplete varint
        }
        v = (v << 7) | (uint8_t)(buf[i] & 0x7f);
        if ((buf[i] & 0x80) == 0)
        {
            *out = v;
            return i + 1;
        }
    }
    if (len < 9)
    {
        return 0;
    }
    v = (v << 8) | buf[8]; // 9th byte carries all 8 bits
    *out = v;
    return 9;
}

uint64_t pc_sqlite_serial_type_size(uint64_t t)
{
    switch (t)
    {
    case 0:
        return 0; // NULL
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 3;
    case 4:
        return 4;
    case 5:
        return 6;
    case 6:
        return 8;
    case 7:
        return 8; // IEEE 754 float64
    case 8:
        return 0; // integer 0
    case 9:
        return 0; // integer 1
    case 10:
    case 11:
        return 0; // reserved for internal use
    default:
        // >=12 even -> BLOB of (t-12)/2; >=13 odd -> TEXT of (t-13)/2. Floor division covers both.
        return t >= 12 ? (t - 12) / 2 : 0; // GCOVR_EXCL_BR_LINE  t<12 here means serial type 10/11, SQLite-reserved and
                                           // absent from any valid record
    }
}

bool pc_sqlite_parse_db_header(const uint8_t *buf, size_t len, SqliteDbHeader *out)
{
    if (len < 100 || memcmp(buf, SQLITE_MAGIC, 16) != 0)
    {
        return false;
    }

    uint16_t raw_ps = be16(buf + 16);
    uint32_t page_size = (raw_ps == 1) ? 65536u : raw_ps;
    // A valid page size is a power of two in [512, 65536].
    // GCOVR_EXCL_BR_LINE below: page_size <= 65536 always here (raw_ps is uint16, with 1 mapping to 65536),
    // so the > 65536 arm is a dead upper bound; the < 512 and is_pow2 arms are exercised.
    if (page_size < 512 || page_size > 65536 || !is_pow2(page_size)) // GCOVR_EXCL_BR_LINE
    {
        return false;
    }

    out->page_size = page_size;
    out->write_version = buf[18];
    out->read_version = buf[19];
    out->reserved_per_page = buf[20];
    out->file_change_counter = be32(buf + 24);
    out->page_count = be32(buf + 28);
    out->freelist_first = be32(buf + 32);
    out->freelist_count = be32(buf + 36);
    out->schema_cookie = be32(buf + 40);
    out->schema_format = be32(buf + 44);
    out->text_encoding = be32(buf + 56);
    out->user_version = be32(buf + 60);
    out->application_id = be32(buf + 68);
    out->pc_sqlite_version = be32(buf + 96);
    return true;
}

bool pc_sqlite_parse_btree_header(const uint8_t *page, size_t page_len, size_t offset, SqliteBtreeHeader *out)
{
    if (offset + 8 > page_len)
    {
        return false;
    }
    const uint8_t *p = page + offset;
    uint8_t type = p[0];
    bool interior =
        (type == SqliteBtree::SQLITE_BTREE_INTERIOR_INDEX || type == SqliteBtree::SQLITE_BTREE_INTERIOR_TABLE);
    bool leaf = (type == SqliteBtree::SQLITE_BTREE_LEAF_INDEX || type == SqliteBtree::SQLITE_BTREE_LEAF_TABLE);
    if (!interior && !leaf)
    {
        return false;
    }
    uint8_t hdr = interior ? 12 : 8;
    if (offset + hdr > page_len)
    {
        return false;
    }

    out->type = type;
    out->first_freeblock = be16(p + 1);
    out->cell_count = be16(p + 3);
    uint16_t ccs = be16(p + 5);
    out->cell_content_start = (ccs == 0) ? 65536u : ccs;
    out->frag_free_bytes = p[7];
    out->right_most_page = interior ? be32(p + 8) : 0;
    out->header_size = hdr;
    return true;
}

uint32_t pc_sqlite_cell_pointer(const uint8_t *page, size_t page_len, const SqliteBtreeHeader *bh, size_t page_offset,
                                uint16_t i)
{
    if (i >= bh->cell_count)
    {
        return 0;
    }
    size_t at = page_offset + bh->header_size + (size_t)i * 2;
    if (at + 2 > page_len)
    {
        return 0;
    }
    return be16(page + at); // cell pointers are offsets from the start of the page
}

bool pc_sqlite_parse_table_leaf_cell(const uint8_t *page, size_t page_len, uint32_t page_size, uint8_t reserved,
                                     uint32_t cell_off, SqliteTableLeafCell *out)
{
    if (cell_off >= page_len)
    {
        return false;
    }
    uint64_t payload_len = 0;
    uint64_t rowid = 0;
    size_t n1 = pc_sqlite_varint_decode(page + cell_off, page_len - cell_off, &payload_len);
    if (n1 == 0)
    {
        return false;
    }
    size_t n2 = pc_sqlite_varint_decode(page + cell_off + n1, page_len - cell_off - n1, &rowid);
    if (n2 == 0)
    {
        return false;
    }

    // SQLite overflow threshold for a table b-tree leaf (fileformat2.html section 1.6).
    uint32_t usable = page_size - reserved;
    uint32_t max_local = usable - 35;
    uint32_t min_local = ((usable - 12) * 32) / 255 - 23;
    uint32_t local;
    if (payload_len <= max_local)
    {
        local = (uint32_t)payload_len;
    }
    else
    {
        uint32_t k = min_local + (uint32_t)((payload_len - min_local) % (usable - 4));
        local = (k <= max_local) ? k : min_local;
    }

    out->rowid = rowid;
    out->payload_len = (uint32_t)payload_len;
    out->local_off = cell_off + (uint32_t)(n1 + n2);
    out->local_len = local;
    out->has_overflow = local < payload_len;
    if (out->local_off + local > page_len)
    {
        return false; // the claimed local payload does not fit the page
    }
    return true;
}

bool pc_sqlite_read_payload(SqlitePageReader read, void *ctx, uint32_t page_size, uint8_t reserved,
                            const uint8_t *leaf_page, const SqliteTableLeafCell *cell, uint8_t *out, uint32_t out_cap,
                            uint8_t *work_page)
{
    if (cell->payload_len > out_cap)
    {
        return false; // fail closed rather than overrun the caller buffer
    }
    if ((size_t)cell->local_off + cell->local_len > page_size)
    {
        return false;
    }

    memcpy(out, leaf_page + cell->local_off, cell->local_len);
    uint32_t got = cell->local_len;
    if (!cell->has_overflow)
    {
        return got == cell->payload_len; // wholly in-page: nothing to follow
    }

    // The 4-byte first-overflow-page number sits immediately after the local prefix.
    size_t ptr_off = (size_t)cell->local_off + cell->local_len;
    if (ptr_off + 4 > page_size)
    {
        return false;
    }
    uint32_t next = be32(leaf_page + ptr_off);

    uint32_t usable = page_size - reserved;
    uint32_t content = usable - 4; // per overflow page: 4-byte next pointer + content
    if (content == 0)
    {
        return false;
    }
    // A chain cannot legitimately be longer than this; the bound turns a corrupt/looping
    // next-pointer into a clean failure instead of an unbounded read.
    uint32_t max_pages = cell->payload_len / content + 2;

    for (uint32_t pages = 0; next != 0 && got < cell->payload_len; pages++)
    {
        // Unreachable belt-and-suspenders: `got` grows by `content` every iteration, so it reaches
        // payload_len in fewer than max_pages steps - kept only as a guard against future logic changes.
        if (pages >= max_pages) // GCOVR_EXCL_BR_LINE  broken / looping chain (provably not hit; see above)
        {
            return false; // GCOVR_EXCL_LINE  broken / looping chain (provably not hit; see above)
        }
        if (!read(ctx, next, work_page, page_size))
        {
            return false;
        }
        uint32_t nnext = be32(work_page);
        uint32_t chunk = cell->payload_len - got;
        if (chunk > content)
        {
            chunk = content;
        }
        // Also unreachable: got < payload_len <= out_cap and chunk <= payload_len - got, so got + chunk
        // never exceeds out_cap - the entry check at the top already bounded the write.
        if (got + chunk > out_cap) // GCOVR_EXCL_BR_LINE  bounded by the entry check (see above)
        {
            return false; // GCOVR_EXCL_LINE
        }
        memcpy(out + got, work_page + 4, chunk);
        got += chunk;
        next = nnext;
    }
    return got == cell->payload_len;
}

bool pc_sqlite_record_begin(SqliteRecordCursor *c, const uint8_t *rec, uint32_t rec_len)
{
    uint64_t hdr_len = 0;
    size_t n = pc_sqlite_varint_decode(rec, rec_len, &hdr_len);
    if (n == 0 || hdr_len > rec_len || hdr_len < n)
    {
        return false;
    }
    c->rec = rec;
    c->rec_len = rec_len;
    c->hdr_pos = (uint32_t)n;
    c->hdr_end = (uint32_t)hdr_len;
    c->val_pos = (uint32_t)hdr_len;
    return true;
}

bool pc_sqlite_record_next(SqliteRecordCursor *c, uint64_t *serial_type, const uint8_t **val, uint32_t *val_len)
{
    if (c->hdr_pos >= c->hdr_end)
    {
        return false;
    }
    uint64_t st = 0;
    size_t n = pc_sqlite_varint_decode(c->rec + c->hdr_pos, c->hdr_end - c->hdr_pos, &st);
    if (n == 0)
    {
        return false;
    }
    uint32_t sz = (uint32_t)pc_sqlite_serial_type_size(st);
    if (c->val_pos + sz > c->rec_len)
    {
        return false; // value runs past the record (a truncated / overflowing row)
    }
    *serial_type = st;
    *val = c->rec + c->val_pos;
    *val_len = sz;
    c->hdr_pos += (uint32_t)n;
    c->val_pos += sz;
    return true;
}

int64_t pc_sqlite_column_int(uint64_t serial_type, const uint8_t *val, uint32_t val_len)
{
    if (serial_type == 8)
    {
        return 0;
    }
    if (serial_type == 9)
    {
        return 1;
    }
    if (serial_type < 1 || serial_type > 6)
    {
        return 0;
    }
    size_t nbytes = (size_t)pc_sqlite_serial_type_size(serial_type);
    if (val_len < nbytes)
    {
        return 0;
    }
    uint64_t u = 0;
    for (size_t i = 0; i < nbytes; i++)
    {
        u = (u << 8) | val[i];
    }
    size_t bits = nbytes * 8;
    if (bits < 64 && (u & (1ULL << (bits - 1))))
    {
        u |= ~((1ULL << bits) - 1); // sign-extend from the stored width
    }
    return (int64_t)u;
}

double pc_sqlite_column_float(const uint8_t *val, uint32_t val_len)
{
    if (val_len < 8)
    {
        return 0.0;
    }
    uint64_t u = 0;
    for (int i = 0; i < 8; i++)
    {
        u = (u << 8) | val[i];
    }
    double d = 0.0;
    memcpy(&d, &u, 8); // u holds the big-endian-read IEEE-754 bit pattern as a native u64
    return d;
}

namespace
{
// The page byte offset of an interior page's b-tree header (100 only for page 1, the schema root).
size_t page_hdr_off(uint32_t pgno)
{
    return pgno == 1 ? 100 : 0;
}

// Child page pointer to descend for child index `i` of an interior table page: the left-child of cell i,
// or the right-most pointer when i == cell_count. 0 on a bad pointer.
uint32_t interior_child(const uint8_t *page, size_t page_len, const SqliteBtreeHeader *h, size_t off, uint16_t i)
{
    if (i >= h->cell_count)
    {
        return h->right_most_page;
    }
    uint32_t cp = pc_sqlite_cell_pointer(page, page_len, h, off, i);
    if (cp == 0 || (size_t)cp + 4 > page_len)
    {
        return 0;
    }
    return be32(page + cp); // an interior-table cell starts with the u32 left-child page number
}

// Descend leftmost from `pgno`, pushing interior frames, until a leaf table page is loaded into c->leaf.
bool cursor_descend(SqliteTableCursor *c, uint32_t pgno)
{
    for (;;)
    {
        if (!c->read(c->ctx, pgno, c->work, c->page_size))
        {
            return false;
        }
        size_t off = page_hdr_off(pgno);
        SqliteBtreeHeader h;
        if (!pc_sqlite_parse_btree_header(c->work, c->page_size, off, &h))
        {
            return false;
        }
        if (h.type == SqliteBtree::SQLITE_BTREE_LEAF_TABLE)
        {
            memcpy(c->leaf, c->work, c->page_size);
            c->leaf_hdr = h;
            c->leaf_off = (uint32_t)off;
            c->leaf_pgno = pgno;
            c->leaf_count = h.cell_count;
            c->leaf_cell = 0;
            return true;
        }
        if (h.type != SqliteBtree::SQLITE_BTREE_INTERIOR_TABLE)
        {
            return false; // an index b-tree or garbage - not a table scan
        }
        if (c->depth >= SQLITE_BTREE_MAX_DEPTH)
        {
            return false;
        }
        uint32_t child = interior_child(c->work, c->page_size, &h, off, 0);
        if (child == 0)
        {
            return false;
        }
        c->stack_pg[c->depth] = pgno;
        c->stack_idx[c->depth] = 1; // child 0 taken; next is 1
        c->depth++;
        pgno = child;
    }
}
} // namespace

bool pc_sqlite_table_cursor_begin(SqliteTableCursor *c, SqlitePageReader read, void *ctx, uint32_t page_size,
                                  uint8_t reserved, uint32_t rootpage, uint8_t *leaf_buf, uint8_t *work_buf)
{
    c->read = read;
    c->ctx = ctx;
    c->page_size = page_size;
    c->reserved = reserved;
    c->leaf = leaf_buf;
    c->work = work_buf;
    c->ovf_buf = nullptr;
    c->ovf_cap = 0;
    c->depth = 0;
    c->leaf_count = 0;
    c->leaf_cell = 0;
    return cursor_descend(c, rootpage);
}

void pc_sqlite_table_cursor_set_overflow_buf(SqliteTableCursor *c, uint8_t *buf, uint32_t cap)
{
    c->ovf_buf = buf;
    c->ovf_cap = cap;
}

// Begin reading a row record from one leaf cell: straight from the leaf page, or (for an overflowing
// cell) reassembled into the caller's overflow buffer first. Extracted to keep the cursor loop flat.
static bool pc_sqlite_cursor_begin_row(SqliteTableCursor *c, const SqliteTableLeafCell *cell, SqliteRecordCursor *row)
{
    if (!cell->has_overflow || !c->ovf_buf)
    {
        return pc_sqlite_record_begin(row, c->leaf + cell->local_off, cell->local_len);
    }
    // c->work is free here (we are at a leaf, not descending), so it serves as the scratch page.
    if (!pc_sqlite_read_payload(c->read, c->ctx, c->page_size, c->reserved, c->leaf, cell, c->ovf_buf, c->ovf_cap,
                                c->work))
    {
        return false;
    }
    return pc_sqlite_record_begin(row, c->ovf_buf, cell->payload_len);
}

bool pc_sqlite_table_cursor_next(SqliteTableCursor *c, uint64_t *rowid, SqliteRecordCursor *row)
{
    for (;;)
    {
        if (c->leaf_cell < c->leaf_count)
        {
            uint32_t cp = pc_sqlite_cell_pointer(c->leaf, c->page_size, &c->leaf_hdr, c->leaf_off, c->leaf_cell);
            c->leaf_cell++;
            if (cp == 0)
            {
                continue;
            }
            SqliteTableLeafCell cell;
            if (!pc_sqlite_parse_table_leaf_cell(c->leaf, c->page_size, c->page_size, c->reserved, cp, &cell))
            {
                continue;
            }
            if (!pc_sqlite_cursor_begin_row(c, &cell, row))
            {
                continue;
            }
            *rowid = cell.rowid;
            return true;
        }
        // Current leaf is exhausted: advance the descent stack to the next subtree.
        if (c->depth == 0)
        {
            return false; // whole table consumed
        }
        int top = c->depth - 1;
        if (!c->read(c->ctx, c->stack_pg[top], c->work, c->page_size))
        {
            return false;
        }
        size_t off = page_hdr_off(c->stack_pg[top]);
        SqliteBtreeHeader h;
        if (!pc_sqlite_parse_btree_header(c->work, c->page_size, off, &h))
        {
            return false;
        }
        if (c->stack_idx[top] <= h.cell_count)
        {
            uint32_t child = interior_child(c->work, c->page_size, &h, off, c->stack_idx[top]);
            c->stack_idx[top]++;
            if (child == 0 || !cursor_descend(c, child))
            {
                return false;
            }
        }
        else
        {
            c->depth--; // this interior frame's children are all visited; pop to its parent
        }
    }
}

// ---------------------------------------------------------------------------
// Writer (bounded): build a fresh single-table database image.
// ---------------------------------------------------------------------------

namespace
{
void wr_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

// Number of bytes a varint of value v occupies (mirror of pc_sqlite_varint_encode's length).
size_t varint_len(uint64_t v)
{
    if (v <= 0x7fULL)
    {
        return 1;
    }
    if (v <= 0x3fffULL)
    {
        return 2;
    }
    if (v <= 0x1fffffULL)
    {
        return 3;
    }
    if (v <= 0xfffffffULL)
    {
        return 4;
    }
    if (v <= 0x7ffffffffULL)
    {
        return 5;
    }
    if (v <= 0x3ffffffffffULL)
    {
        return 6;
    }
    if (v <= 0x1ffffffffffffULL)
    {
        return 7;
    }
    if (v <= 0xffffffffffffffULL)
    {
        return 8;
    }
    return 9;
}

// The serial type + value byte length for a column value. Integers get the minimal encoding.
void value_serial(const SqliteValue *v, uint64_t *st, uint32_t *vlen)
{
    switch (v->type)
    {
    case SqliteColType::SQLITE_COL_NULL:
        *st = 0;
        *vlen = 0;
        return;
    case SqliteColType::SQLITE_COL_FLOAT:
        *st = 7;
        *vlen = 8;
        return;
    case SqliteColType::SQLITE_COL_TEXT:
        *st = 13 + (uint64_t)2 * v->len;
        *vlen = v->len;
        return;
    case SqliteColType::SQLITE_COL_BLOB:
        *st = 12 + (uint64_t)2 * v->len;
        *vlen = v->len;
        return;
    case SqliteColType::SQLITE_COL_INT:
    default:
        break;
    }
    int64_t x = v->i;
    if (x == 0)
    {
        *st = 8;
        *vlen = 0;
    }
    else if (x == 1)
    {
        *st = 9;
        *vlen = 0;
    }
    else if (x >= -128 && x <= 127)
    {
        *st = 1;
        *vlen = 1;
    }
    else if (x >= -32768 && x <= 32767)
    {
        *st = 2;
        *vlen = 2;
    }
    else if (x >= -8388608 && x <= 8388607)
    {
        *st = 3;
        *vlen = 3;
    }
    else if (x >= -2147483648LL && x <= 2147483647LL)
    {
        *st = 4;
        *vlen = 4;
    }
    else if (x >= -140737488355328LL && x <= 140737488355327LL)
    {
        *st = 5;
        *vlen = 6;
    }
    else
    {
        *st = 6;
        *vlen = 8;
    }
}

// Write a column's value bytes (big-endian for ints/floats) into out; returns the count.
uint32_t write_value(const SqliteValue *v, uint64_t st, uint32_t vlen, uint8_t *out)
{
    if (v->type == SqliteColType::SQLITE_COL_TEXT || v->type == SqliteColType::SQLITE_COL_BLOB)
    {
        if (vlen)
        {
            memcpy(out, v->data, vlen);
        }
        return vlen;
    }
    if (v->type == SqliteColType::SQLITE_COL_FLOAT)
    {
        uint64_t u = 0;
        memcpy(&u, &v->f, 8); // native bit pattern -> emit big-endian
        for (int i = 7; i >= 0; i--)
        {
            *out++ = (uint8_t)(u >> (i * 8));
        }
        return 8;
    }
    if (st >= 1 && st <= 6) // signed big-endian integer of `vlen` bytes
    {
        uint64_t u = (uint64_t)v->i;
        for (int i = (int)vlen - 1; i >= 0; i--)
        {
            out[(int)vlen - 1 - i] = (uint8_t)(u >> (i * 8));
        }
        return vlen;
    }
    return 0; // NULL / 0 / 1 constants carry no bytes
}

// Total encoded length of a record (header + values), or 0 on invalid input.
uint32_t record_len(const SqliteValue *cols, uint32_t n)
{
    uint32_t st_len = 0;
    uint32_t val_len = 0;
    for (uint32_t c = 0; c < n; c++)
    {
        uint64_t st = 0;
        uint32_t vl = 0;
        value_serial(&cols[c], &st, &vl);
        st_len += (uint32_t)varint_len(st);
        val_len += vl;
    }
    // header_size counts its own length varint, which can grow the total - resolve the fixed point.
    size_t hs_varlen = 1;
    for (;;)
    {
        uint64_t hs = hs_varlen + st_len;
        if (varint_len(hs) == hs_varlen)
        {
            break;
        }
        hs_varlen = varint_len(hs);
    }
    return (uint32_t)hs_varlen + st_len + val_len;
}

// Write a leaf-table page (b-tree header at hdr_off, then the rows as cells) into `page`. hdr_off is 100 for
// page 1, else 0. Fails closed if a row would overflow the page or the cells + pointer array do not fit.
bool write_leaf_page(uint8_t *page, uint32_t page_size, uint32_t hdr_off, const SqliteRow *rows, uint32_t nrows)
{
    const uint32_t usable = page_size; // reserved = 0 for images we build
    const uint32_t max_local = usable - 35;

    // Measure each cell up front so we can lay them out and fail closed before writing.
    uint32_t total = 0;
    for (uint32_t r = 0; r < nrows; r++)
    {
        uint32_t rl = record_len(rows[r].cols, rows[r].ncols);
        // record_len() >= 2 whenever ncols != 0 (st_len >= ncols >= 1, plus the >=1 header varint), so
        // rl == 0 implies ncols == 0; the guard is defense against future drift, not a reachable state.
        if (rl == 0 && rows[r].ncols != 0) // GCOVR_EXCL_BR_LINE  unreachable: rl==0 implies ncols==0 (see above)
        {
            return false; // GCOVR_EXCL_LINE  unreachable (see above)
        }
        if (rl > max_local)
        {
            return false; // would need an overflow page - out of scope for the bounded writer
        }
        total += (uint32_t)varint_len(rl) + (uint32_t)varint_len(rows[r].rowid) + rl;
    }
    uint32_t header_end = hdr_off + 8 + 2 * nrows; // 8-byte leaf header + 2-byte cell pointer each
    if ((uint64_t)header_end + total > page_size)
    {
        return false; // does not fit one leaf page
    }

    uint32_t content_start = page_size - total;

    // b-tree leaf-table header.
    uint8_t *h = page + hdr_off;
    h[0] = (uint8_t)SqliteBtree::SQLITE_BTREE_LEAF_TABLE;
    wr_be16(h + 1, 0);               // first freeblock
    wr_be16(h + 3, (uint16_t)nrows); // cell count
    wr_be16(h + 5, (uint16_t)(content_start == 65536 ? 0 : content_start));
    h[7] = 0; // fragmented free bytes

    // Pack cells from the top of the content area downward; pointer array (rowid order) follows the header.
    uint32_t off = page_size;
    for (uint32_t r = 0; r < nrows; r++)
    {
        uint32_t rl = record_len(rows[r].cols, rows[r].ncols);
        uint32_t cell_len = (uint32_t)varint_len(rl) + (uint32_t)varint_len(rows[r].rowid) + rl;
        off -= cell_len;
        uint8_t *cp = page + off;
        size_t k = pc_sqlite_varint_encode(rl, cp, cell_len);
        k += pc_sqlite_varint_encode(rows[r].rowid, cp + k, cell_len - k);
        uint32_t w = pc_sqlite_encode_record(rows[r].cols, rows[r].ncols, cp + k, cell_len - (uint32_t)k);
        // Unreachable: cp+k was given exactly `rl` bytes of capacity (cell_len - k, where cell_len was
        // sized from this same `rl`). Inside pc_sqlite_encode_record(), the header-size varint, the
        // per-column serial-type varints, and the per-column value bytes are each derived from the
        // identical deterministic value_serial() calls (same cols/n, no mutation in between) that
        // record_len() used to compute `rl`, so their lengths sum to exactly `rl` - the same budget the
        // caller handed it. No internal capacity check can ever trip short for these args, so
        // pos == rl == w always. Kept only as a guard against future logic drift between the two.
        if (w != rl) // GCOVR_EXCL_BR_LINE  internal invariant: capacity budget is exact (see above)
        {
            return false; // GCOVR_EXCL_LINE  internal invariant: capacity budget is exact (see above)
        }
        wr_be16(page + hdr_off + 8 + 2 * r, (uint16_t)off); // cell pointer for row r
    }
    return true;
}
} // namespace

size_t pc_sqlite_varint_encode(uint64_t v, uint8_t *out, size_t cap)
{
    size_t n = varint_len(v);
    if (n > cap)
    {
        return 0;
    }
    if (n == 9)
    {
        // Bytes 0-7 carry bits 63..8 (7 bits each, high bit = continuation); byte 8 carries the low 8 bits.
        for (int i = 0; i < 8; i++)
        {
            out[i] = (uint8_t)(0x80 | (uint8_t)((v >> (57 - 7 * i)) & 0x7f));
        }
        out[8] = (uint8_t)v;
        return 9;
    }
    // The low 7 bits of each of the n bytes; high bit set on all but the last.
    for (size_t i = 0; i < n; i++)
    {
        uint8_t bits = (uint8_t)((v >> (7 * (n - 1 - i))) & 0x7f);
        out[i] = (i + 1 < n) ? (uint8_t)(bits | 0x80) : bits;
    }
    return n;
}

uint32_t pc_sqlite_encode_record(const SqliteValue *cols, uint32_t n, uint8_t *out, uint32_t out_cap)
{
    uint32_t total = record_len(cols, n);
    // record_len() >= 2 whenever n != 0 (see pc_sqlite_encode_page), so total == 0 implies n == 0.
    if (total == 0 && n != 0) // GCOVR_EXCL_BR_LINE  unreachable: total==0 implies n==0 (see above)
    {
        return 0; // GCOVR_EXCL_LINE  unreachable (see above)
    }
    if (total > out_cap)
    {
        return 0;
    }

    // Compute the header size (length varint + serial-type varints), same fixed point as record_len.
    uint32_t st_len = 0;
    for (uint32_t c = 0; c < n; c++)
    {
        uint64_t st = 0;
        uint32_t vl = 0;
        value_serial(&cols[c], &st, &vl);
        st_len += (uint32_t)varint_len(st);
    }
    size_t hs_varlen = 1;
    for (;;)
    {
        uint64_t hs = hs_varlen + st_len;
        if (varint_len(hs) == hs_varlen)
        {
            break;
        }
        hs_varlen = varint_len(hs);
    }
    uint32_t header_size = (uint32_t)hs_varlen + st_len;

    uint32_t pos = (uint32_t)pc_sqlite_varint_encode(header_size, out, out_cap);
    // Serial-type varints (the header body).
    for (uint32_t c = 0; c < n; c++)
    {
        uint64_t st = 0;
        uint32_t vl = 0;
        value_serial(&cols[c], &st, &vl);
        pos += (uint32_t)pc_sqlite_varint_encode(st, out + pos, out_cap - pos);
    }
    // Value bytes, in column order.
    for (uint32_t c = 0; c < n; c++)
    {
        uint64_t st = 0;
        uint32_t vl = 0;
        value_serial(&cols[c], &st, &vl);
        pos += write_value(&cols[c], st, vl, out + pos);
    }
    return pos;
}

uint32_t pc_sqlite_build_table_db(uint32_t page_size, const char *table_name, const char *create_sql,
                                  const SqliteRow *rows, uint32_t nrows, uint8_t *out, uint32_t out_cap)
{
    if (page_size < 512 || page_size > 65536 || !is_pow2(page_size))
    {
        return 0;
    }
    if ((uint64_t)page_size * 2 > out_cap || !table_name || !create_sql)
    {
        return 0;
    }

    memset(out, 0, (size_t)page_size * 2);

    // --- Page 1: the 100-byte database header ---
    memcpy(out, SQLITE_MAGIC, 16);
    wr_be16(out + 16, (uint16_t)(page_size == 65536 ? 1 : page_size));
    out[18] = 1;                // write version (legacy)
    out[19] = 1;                // read version (legacy)
    out[20] = 0;                // reserved bytes per page
    out[21] = 64;               // max embedded payload fraction
    out[22] = 32;               // min embedded payload fraction
    out[23] = 32;               // leaf payload fraction
    wr_be32(out + 24, 1);       // file change counter
    wr_be32(out + 28, 2);       // page count
    wr_be32(out + 40, 1);       // schema cookie
    wr_be32(out + 44, 4);       // schema format number
    wr_be32(out + 56, 1);       // text encoding = UTF-8
    wr_be32(out + 92, 1);       // version-valid-for (== file change counter)
    wr_be32(out + 96, 3046001); // SQLITE_VERSION_NUMBER that wrote the file

    // --- Page 1: the pc_sqlite_schema row for our table (type,name,tbl_name,rootpage,sql) ---
    uint32_t name_len = (uint32_t)strnlen(table_name, out_cap);
    uint32_t sql_len = (uint32_t)strnlen(create_sql, out_cap);
    SqliteValue master[5];
    master[0] = {SqliteColType::SQLITE_COL_TEXT, 0, 0, (const uint8_t *)"table", 5};
    master[1] = {SqliteColType::SQLITE_COL_TEXT, 0, 0, (const uint8_t *)table_name, name_len};
    master[2] = {SqliteColType::SQLITE_COL_TEXT, 0, 0, (const uint8_t *)table_name, name_len};
    master[3] = {SqliteColType::SQLITE_COL_INT, 2, 0, nullptr, 0}; // rootpage = 2
    master[4] = {SqliteColType::SQLITE_COL_TEXT, 0, 0, (const uint8_t *)create_sql, sql_len};
    SqliteRow master_row = {1, master, 5};
    if (!write_leaf_page(out, page_size, 100, &master_row, 1))
    {
        return 0;
    }

    // --- Page 2: the table's leaf b-tree with the caller's rows ---
    if (!write_leaf_page(out + page_size, page_size, 0, rows, nrows))
    {
        return 0;
    }

    return page_size * 2;
}

#endif // PC_ENABLE_SQLITE
