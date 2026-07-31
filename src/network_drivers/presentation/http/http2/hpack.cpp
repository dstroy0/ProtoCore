// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hpack.cpp
 * @brief HPACK (RFC 7541) - implementation. See hpack.h.
 *
 * The static table, the Huffman code (Appendix B), and the canonical Huffman decode tables are
 * generated verbatim from RFC 7541 (see docs / the generator in the repo history). Header names
 * and values never touch the heap; the dynamic table is a fixed byte ring with FIFO eviction.
 */

#include "network_drivers/presentation/http/http2/hpack.h"

#if PC_ENABLE_HTTP2

#include "network_drivers/presentation/codec/hpack_prim/hpack_prim.h" // shared prefix-int + Huffman
#include <string.h>

namespace
{
#define HPACK_BYTES PC_HPACK_TABLE_BYTES
#define HPACK_ENTS PC_HPACK_MAX_ENTRIES

// Static table (1-indexed; entry 0 is a placeholder). {name, value}. Generated from RFC 7541 App A.
const char *const STATIC[62][2] = {
    {"", ""},
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""},
};

// --- Dynamic table byte-ring helpers ---------------------------------------------------------

void ring_write(HpackDynTable *t, uint16_t pos, const char *src, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        t->ring[(pos + i) % HPACK_BYTES] = src[i];
    }
}
void ring_read(const HpackDynTable *t, uint16_t pos, char *dst, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        dst[i] = t->ring[(pos + i) % HPACK_BYTES];
    }
}

// Descriptor for the k-th newest live entry (k = 1 is newest); null if out of range.
// k<1 is dead: both callers pass idx-61 with idx>61 already established (idx<=61 takes the static
// branch in resolve_name/emit_indexed), so k>=1 always holds here.
const HpackEntry *dyn_entry(const HpackDynTable *t, uint32_t k)
{
    if (k < 1 || k > t->ecount) // GCOVR_EXCL_BR_LINE
    {
        return nullptr;
    }
    uint16_t di = (uint16_t)((t->ehead + HPACK_ENTS - k) % HPACK_ENTS);
    return &t->ent[di];
}

// Unreachable: both call sites below only invoke this inside a `while (... && t->ecount > 0)` loop.
void dyn_evict_oldest(HpackDynTable *t)
{
    if (t->ecount == 0) // GCOVR_EXCL_LINE
    {
        return; // GCOVR_EXCL_LINE
    }
    uint16_t oi = (uint16_t)((t->ehead + HPACK_ENTS - t->ecount) % HPACK_ENTS);
    const HpackEntry *e = &t->ent[oi];
    uint16_t bytes = (uint16_t)(e->name_len + e->val_len);
    t->rtail = (uint16_t)((t->rtail + bytes) % HPACK_BYTES);
    t->rused = (uint16_t)(t->rused - bytes);
    t->used -= (uint32_t)e->name_len + e->val_len + 32;
    t->ecount--;
}

void dyn_set_max(HpackDynTable *t, uint32_t new_max)
{
    if (new_max > HPACK_BYTES)
    {
        new_max = HPACK_BYTES; // never exceed our advertised storage
    }
    t->max_size = new_max;
    // ecount>0 is dead here: ecount==0 implies used==0 (dyn_insert/dyn_evict_oldest keep that
    // invariant), so used>max_size can't be true while ecount==0.
    while (t->used > t->max_size && t->ecount > 0) // GCOVR_EXCL_BR_LINE
    {
        dyn_evict_oldest(t);
    }
}

void dyn_insert(HpackDynTable *t, const char *name, size_t nlen, const char *val, size_t vlen)
{
    uint32_t entry_size = (uint32_t)nlen + (uint32_t)vlen + 32;
    if (entry_size > t->max_size)
    { // RFC 7541 sec 4.4: clears the table, entry not added
        t->ecount = 0;
        t->used = 0;
        t->rused = 0;
        t->rtail = 0;
        t->ehead = 0;
        return;
    }
    // Both the entry-count half of the || and the trailing ecount>0 check are dead: entry_size <=
    // t->max_size is already guaranteed above, and PC_HPACK_TABLE_BYTES / 32 == PC_HPACK_MAX_ENTRIES
    // exactly, so the byte-budget half always trips at or before the entry-count half could, and
    // ecount can't be 0 whenever either half of the || is true.
    while ((t->used + entry_size > t->max_size || t->ecount >= HPACK_ENTS) && t->ecount > 0) // GCOVR_EXCL_BR_LINE
    {
        dyn_evict_oldest(t);
    }
    uint16_t rpos = (uint16_t)((t->rtail + t->rused) % HPACK_BYTES);
    HpackEntry *e = &t->ent[t->ehead];
    e->name_len = (uint16_t)nlen;
    e->val_len = (uint16_t)vlen;
    e->ring_pos = rpos;
    ring_write(t, rpos, name, nlen);
    ring_write(t, (uint16_t)((rpos + nlen) % HPACK_BYTES), val, vlen);
    t->rused = (uint16_t)(t->rused + nlen + vlen);
    t->ehead = (uint16_t)((t->ehead + 1) % HPACK_ENTS);
    t->ecount++;
    t->used += entry_size;
}

// Copy an indexed header's name (idx>=1) into out; false if idx invalid / too big.
// idx>=1 is always true here: the only caller (decode_literal) routes name_idx==0 to the inline-name
// path and never calls resolve_name with it.
bool resolve_name(const HpackDynTable *t, uint32_t idx, char *out, size_t cap, size_t *out_len)
{
    if (idx >= 1 && idx <= 61) // GCOVR_EXCL_BR_LINE
    {
        size_t nl = strnlen(STATIC[idx][0], cap + 1);
        if (nl > cap)
        {
            return false;
        }
        memcpy(out, STATIC[idx][0], nl);
        *out_len = nl;
        return true;
    }
    const HpackEntry *e = dyn_entry(t, idx - 61);
    if (!e || e->name_len > cap)
    {
        return false;
    }
    ring_read(t, e->ring_pos, out, e->name_len);
    *out_len = e->name_len;
    return true;
}

// Emit a fully-indexed field (idx resolves to name+value); copies both into scratch.
// idx>=1 is always true here: the only caller (pc_hpack_decode's 6.1 Indexed Header Field case)
// already rejects idx==0 before calling emit_indexed.
bool emit_indexed(HpackDynTable *t, uint32_t idx, char *scratch, size_t cap, HpackEmitFn emit, void *ctx)
{
    size_t nl;
    size_t vl;
    if (idx >= 1 && idx <= 61) // GCOVR_EXCL_BR_LINE
    {
        nl = strnlen(STATIC[idx][0], cap + 1);
        vl = strnlen(STATIC[idx][1], cap + 1);
        if (nl + vl > cap)
        {
            return false;
        }
        memcpy(scratch, STATIC[idx][0], nl);
        memcpy(scratch + nl, STATIC[idx][1], vl);
    }
    else
    {
        const HpackEntry *e = dyn_entry(t, idx - 61);
        if (!e)
        {
            return false;
        }
        nl = e->name_len;
        vl = e->val_len;
        if (nl + vl > cap)
        {
            return false;
        }
        ring_read(t, e->ring_pos, scratch, nl);
        ring_read(t, (uint16_t)((e->ring_pos + nl) % HPACK_BYTES), scratch + nl, vl);
    }
    return emit(ctx, scratch, nl, scratch + nl, vl);
}

// Decode a literal representation (name via index or inline, value inline; optional indexing).
bool decode_literal(HpackDynTable *t, const uint8_t *block, size_t len, size_t *pos, uint8_t prefix_bits, bool do_index,
                    char *scratch, size_t cap, HpackEmitFn emit, void *ctx)
{
    size_t c = 0;
    uint32_t name_idx = 0;
    if (!pc_hpack_decode_int(block + *pos, len - *pos, prefix_bits, &c, &name_idx))
    {
        return false;
    }
    *pos += c;
    size_t name_len = 0;
    if (name_idx == 0)
    {
        if (!pc_hpack_decode_str(block, len, pos, scratch, cap, &name_len))
        {
            return false;
        }
    }
    else if (!resolve_name(t, name_idx, scratch, cap, &name_len))
    {
        return false;
    }
    size_t val_len = 0;
    if (!pc_hpack_decode_str(block, len, pos, scratch + name_len, cap - name_len, &val_len))
    {
        return false;
    }
    if (do_index)
    {
        dyn_insert(t, scratch, name_len, scratch + name_len, val_len);
    }
    return emit(ctx, scratch, name_len, scratch + name_len, val_len);
}

} // namespace

// --- Public API ------------------------------------------------------------------------------

void pc_hpack_dyn_init(HpackDynTable *t, uint32_t max_bytes)
{
    memset(t, 0, sizeof(*t));
    t->max_size = max_bytes ? max_bytes : (uint32_t)HPACK_BYTES;
    if (t->max_size > HPACK_BYTES)
    {
        t->max_size = HPACK_BYTES;
    }
}

bool pc_hpack_decode(HpackDynTable *t, const uint8_t *block, size_t len, char *scratch, size_t scratch_cap,
                     HpackEmitFn emit, void *ctx)
{
    size_t pos = 0;
    while (pos < len)
    {
        uint8_t b = block[pos];
        if (b & 0x80)
        { // 6.1 Indexed Header Field
            size_t c = 0;
            uint32_t idx = 0;
            if (!pc_hpack_decode_int(block + pos, len - pos, 7, &c, &idx) || idx == 0)
            {
                return false;
            }
            pos += c;
            if (!emit_indexed(t, idx, scratch, scratch_cap, emit, ctx))
            {
                return false;
            }
        }
        else if (b & 0x40)
        { // 6.2.1 Literal with incremental indexing (name prefix 6)
            if (!decode_literal(t, block, len, &pos, 6, true, scratch, scratch_cap, emit, ctx))
            {
                return false;
            }
        }
        else if ((b & 0xE0) == 0x20)
        { // 6.3 Dynamic table size update (prefix 5)
            size_t c = 0;
            uint32_t nm = 0;
            if (!pc_hpack_decode_int(block + pos, len - pos, 5, &c, &nm))
            {
                return false;
            }
            pos += c;
            dyn_set_max(t, nm);
        }
        else
        { // 6.2.2 without / 6.2.3 never indexed (name prefix 4, no table insert)
            if (!decode_literal(t, block, len, &pos, 4, false, scratch, scratch_cap, emit, ctx))
            {
                return false;
            }
        }
    }
    return true;
}

size_t pc_hpack_encode_header(uint8_t *out, size_t cap, const char *name, size_t name_len, const char *value,
                              size_t value_len)
{
    int name_idx = 0;
    int full_idx = 0;
    for (int i = 1; i <= 61; i++)
    {
        if (strnlen(STATIC[i][0], name_len + 1) == name_len && memcmp(STATIC[i][0], name, name_len) == 0)
        {
            if (!name_idx)
            {
                name_idx = i;
            }
            if (strnlen(STATIC[i][1], value_len + 1) == value_len && memcmp(STATIC[i][1], value, value_len) == 0)
            {
                full_idx = i;
                break;
            }
        }
    }
    if (full_idx)
    {
        return pc_hpack_encode_int(out, cap, 7, 0x80, (uint32_t)full_idx);
    }
    // Literal without indexing (top nibble 0000), name prefix 4.
    size_t o = pc_hpack_encode_int(out, cap, 4, 0x00, (uint32_t)name_idx);
    if (!o)
    {
        return 0;
    }
    if (name_idx == 0)
    {
        size_t ns = pc_hpack_encode_str(out + o, cap - o, name, name_len);
        if (!ns)
        {
            return 0;
        }
        o += ns;
    }
    size_t vs = pc_hpack_encode_str(out + o, cap - o, value, value_len);
    if (!vs)
    {
        return 0;
    }
    return o + vs;
}

#endif // PC_ENABLE_HTTP2
