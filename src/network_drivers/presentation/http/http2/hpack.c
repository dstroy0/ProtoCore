// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hpack.c
 * @brief HPACK (RFC 7541) - implementation. See hpack.h.
 *
 * The static table, the Huffman code (Appendix B), and the canonical Huffman decode tables are
 * generated verbatim from RFC 7541 (see docs / the generator in the repo history). Header names
 * and values never touch the heap; the dynamic table is a fixed byte ring with FIFO eviction.
 */

#if PROTOCORE_ENABLE_HTTP2

#include "network_drivers/presentation/codec/hpack_prim/hpack_prim.h" // shared prefix-int + Huffman
#include "network_drivers/presentation/http/http2/hpack.h"
#include "mmgr/protomem.h"
#include "mmgr/protostr.h"

/** @brief One dynamic-table entry descriptor (its bytes live in the table's byte ring). */
typedef struct
{
    uint16_t name_len; ///< header name length
    uint16_t val_len;  ///< header value length
    uint16_t ring_pos; ///< start of name||value in the byte ring
} HpackEntry;

/**
 * @brief Per-connection HPACK dynamic table (the peer encoder's state, tracked by our decoder).
 * FIFO: newest entry is dynamic index 62, oldest is evicted first. Fixed storage, no heap.
 */
typedef struct
{
    uint32_t max_size; ///< negotiated maximum size in bytes (RFC 7541 sec 4.2)
    uint32_t used;     ///< current size = sum of (name_len + val_len + 32) over entries
    uint16_t ehead;    ///< descriptor ring: index one past the newest entry
    uint16_t ecount;   ///< number of live entries
    uint16_t rtail;    ///< byte ring: start of the oldest entry's bytes
    uint16_t rused;    ///< byte ring: bytes in use
    HpackEntry ent[PROTOCORE_HPACK_MAX_ENTRIES];
    char ring[PROTOCORE_HPACK_TABLE_BYTES];
} HpackDynTable;

// The caller's borrow, split: the table at its offset. One pointer arrives and every region is
// that pointer plus a compile-time offset, so the assert below proves the span covers it before
// anything runs.
#define HPACK_OFF_CTX 0u
static_assert(HPACK_OFF_CTX + sizeof(HpackDynTable) <= PROTOCORE_HPACK_BORROW,
              "PROTOCORE_HPACK_BORROW is short of the dynamic table - raise it in protocore_config.h,"
              " which sums it into the connection that owns it");

// The region, at its offset in the caller's borrow.
#define HPACK_CTX(w) ((HpackDynTable *)(void *)((w) + HPACK_OFF_CTX))

#define HPACK_BYTES PROTOCORE_HPACK_TABLE_BYTES
#define HPACK_ENTS PROTOCORE_HPACK_MAX_ENTRIES

// Static table (1-indexed; entry 0 is a placeholder). {name, value}. Generated from RFC 7541 App A.
static const char *const STATIC[62][2] = {
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

static void ring_write(HpackDynTable *t, uint16_t pos, const char *src, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        t->ring[(pos + i) % HPACK_BYTES] = src[i];
    }
}
static void ring_read(const HpackDynTable *t, uint16_t pos, char *dst, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        dst[i] = t->ring[(pos + i) % HPACK_BYTES];
    }
}

// Descriptor for the k-th newest live entry (k = 1 is newest); null if out of range.
// k<1 is dead: both callers pass idx-61 with idx>61 already established (idx<=61 takes the static
// branch in resolve_name/emit_indexed), so k>=1 always holds here.
static const HpackEntry *dyn_entry(const HpackDynTable *t, uint32_t k)
{
    if (k < 1 || k > t->ecount)
    {
        return NULL;
    }
    uint16_t di = (uint16_t)((t->ehead + HPACK_ENTS - k) % HPACK_ENTS);
    return &t->ent[di];
}

// Unreachable: both call sites below only invoke this inside a `while (... && t->ecount > 0)` loop.
static void dyn_evict_oldest(HpackDynTable *t)
{
    if (t->ecount == 0)
    {
        return;
    }
    uint16_t oi = (uint16_t)((t->ehead + HPACK_ENTS - t->ecount) % HPACK_ENTS);
    const HpackEntry *e = &t->ent[oi];
    uint16_t bytes = (uint16_t)(e->name_len + e->val_len);
    t->rtail = (uint16_t)((t->rtail + bytes) % HPACK_BYTES);
    t->rused = (uint16_t)(t->rused - bytes);
    t->used -= (uint32_t)e->name_len + e->val_len + 32;
    t->ecount--;
}

static void dyn_set_max(HpackDynTable *t, uint32_t new_max)
{
    if (new_max > HPACK_BYTES)
    {
        new_max = HPACK_BYTES; // never exceed our advertised storage
    }
    t->max_size = new_max;
    // ecount>0 is dead here: ecount==0 implies used==0 (dyn_insert/dyn_evict_oldest keep that
    // invariant), so used>max_size can't be true while ecount==0.
    while (t->used > t->max_size && t->ecount > 0)
    {
        dyn_evict_oldest(t);
    }
}

static void dyn_insert(HpackDynTable *t, const char *name, size_t nlen, const char *val, size_t vlen)
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
    // t->max_size is already guaranteed above, and PROTOCORE_HPACK_TABLE_BYTES / 32 == PROTOCORE_HPACK_MAX_ENTRIES
    // exactly, so the byte-budget half always trips at or before the entry-count half could, and
    // ecount can't be 0 whenever either half of the || is true.
    while ((t->used + entry_size > t->max_size || t->ecount >= HPACK_ENTS) && t->ecount > 0)
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
static proto_bool resolve_name(const HpackDynTable *t, uint32_t idx, char *out, size_t cap, size_t *out_len)
{
    if (idx >= 1 && idx <= 61)
    {
        size_t nl = str.len(STATIC[idx][0], cap + 1);
        if (nl > cap)
        {
            return PROTO_FALSE;
        }
        mem.cpy(out, STATIC[idx][0], nl);
        *out_len = nl;
        return PROTO_TRUE;
    }
    const HpackEntry *e = dyn_entry(t, idx - 61);
    if (!e || e->name_len > cap)
    {
        return PROTO_FALSE;
    }
    ring_read(t, e->ring_pos, out, e->name_len);
    *out_len = e->name_len;
    return PROTO_TRUE;
}

// Emit a fully-indexed field (idx resolves to name+value); copies both into scratch.
// idx>=1 is always true here: the only caller (protocore_hpack_decode's 6.1 Indexed Header Field case)
// already rejects idx==0 before calling emit_indexed.
static proto_bool emit_indexed(HpackDynTable *t, uint32_t idx, char *scratch, size_t cap, HpackEmitFn emit, void *ctx)
{
    size_t nl;
    size_t vl;
    if (idx >= 1 && idx <= 61)
    {
        nl = str.len(STATIC[idx][0], cap + 1);
        vl = str.len(STATIC[idx][1], cap + 1);
        if (nl + vl > cap)
        {
            return PROTO_FALSE;
        }
        mem.cpy(scratch, STATIC[idx][0], nl);
        mem.cpy(scratch + nl, STATIC[idx][1], vl);
    }
    else
    {
        const HpackEntry *e = dyn_entry(t, idx - 61);
        if (!e)
        {
            return PROTO_FALSE;
        }
        nl = e->name_len;
        vl = e->val_len;
        if (nl + vl > cap)
        {
            return PROTO_FALSE;
        }
        ring_read(t, e->ring_pos, scratch, nl);
        ring_read(t, (uint16_t)((e->ring_pos + nl) % HPACK_BYTES), scratch + nl, vl);
    }
    return emit(ctx, scratch, nl, scratch + nl, vl);
}

// Decode a literal representation (name via index or inline, value inline; optional indexing).
static proto_bool decode_literal(HpackDynTable *t, const uint8_t *block, size_t len, size_t *pos, uint8_t prefix_bits,
                                 proto_bool do_index, char *scratch, size_t cap, HpackEmitFn emit, void *ctx)
{
    size_t c = 0;
    uint32_t name_idx = 0;
    if (!HpackPrim.decode_int(block + *pos, len - *pos, prefix_bits, &c, &name_idx))
    {
        return PROTO_FALSE;
    }
    *pos += c;
    size_t name_len = 0;
    if (name_idx == 0)
    {
        if (!HpackPrim.decode_str(block, len, pos, scratch, cap, &name_len))
        {
            return PROTO_FALSE;
        }
    }
    else if (!resolve_name(t, name_idx, scratch, cap, &name_len))
    {
        return PROTO_FALSE;
    }
    size_t val_len = 0;
    if (!HpackPrim.decode_str(block, len, pos, scratch + name_len, cap - name_len, &val_len))
    {
        return PROTO_FALSE;
    }
    if (do_index)
    {
        dyn_insert(t, scratch, name_len, scratch + name_len, val_len);
    }
    return emit(ctx, scratch, name_len, scratch + name_len, val_len);
}

// --- Public API ------------------------------------------------------------------------------

static void hpack_dyn_init_run(HpackDynTable *t, uint32_t max_bytes)
{
    mem.set(t, 0, sizeof(*t));
    t->max_size = max_bytes ? max_bytes : (uint32_t)HPACK_BYTES;
    if (t->max_size > HPACK_BYTES)
    {
        t->max_size = HPACK_BYTES;
    }
}

static proto_bool hpack_decode_run(HpackDynTable *t, const uint8_t *block, size_t len, char *scratch,
                                   size_t scratch_cap, HpackEmitFn emit, void *ctx)
{
    size_t pos = 0;
    while (pos < len)
    {
        uint8_t b = block[pos];
        if (b & 0x80)
        { // 6.1 Indexed Header Field
            size_t c = 0;
            uint32_t idx = 0;
            if (!HpackPrim.decode_int(block + pos, len - pos, 7, &c, &idx) || idx == 0)
            {
                return PROTO_FALSE;
            }
            pos += c;
            if (!emit_indexed(t, idx, scratch, scratch_cap, emit, ctx))
            {
                return PROTO_FALSE;
            }
        }
        else if (b & 0x40)
        { // 6.2.1 Literal with incremental indexing (name prefix 6)
            if (!decode_literal(t, block, len, &pos, 6, PROTO_TRUE, scratch, scratch_cap, emit, ctx))
            {
                return PROTO_FALSE;
            }
        }
        else if ((b & 0xE0) == 0x20)
        { // 6.3 Dynamic table size update (prefix 5)
            size_t c = 0;
            uint32_t nm = 0;
            if (!HpackPrim.decode_int(block + pos, len - pos, 5, &c, &nm))
            {
                return PROTO_FALSE;
            }
            pos += c;
            // RFC 7541 sec 6.3: a size update above the limit the enclosing protocol set is a
            // decoding error, and RFC 9113 sec 4.3 makes a decoding error a connection error. We
            // advertise no SETTINGS_HEADER_TABLE_SIZE, so the limit is the RFC 9113 sec 6.5.2 default
            // that HPACK_BYTES holds. Clamping would take a peer's illegal update as if it were legal.
            if (nm > HPACK_BYTES)
            {
                return PROTO_FALSE;
            }
            dyn_set_max(t, nm);
        }
        else
        { // 6.2.2 without / 6.2.3 never indexed (name prefix 4, no table insert)
            if (!decode_literal(t, block, len, &pos, 4, PROTO_FALSE, scratch, scratch_cap, emit, ctx))
            {
                return PROTO_FALSE;
            }
        }
    }
    return PROTO_TRUE;
}

static size_t hpack_encode_header_run(uint8_t *out, size_t cap, const char *name, size_t name_len, const char *value,
                                      size_t value_len)
{
    int name_idx = 0;
    int full_idx = 0;
    for (int i = 1; i <= 61; i++)
    {
        if (str.len(STATIC[i][0], name_len + 1) == name_len && mem.cmp(STATIC[i][0], name, name_len) == 0)
        {
            if (!name_idx)
            {
                name_idx = i;
            }
            if (str.len(STATIC[i][1], value_len + 1) == value_len && mem.cmp(STATIC[i][1], value, value_len) == 0)
            {
                full_idx = i;
                break;
            }
        }
    }
    if (full_idx)
    {
        return HpackPrim.encode_int(out, cap, 7, 0x80, (uint32_t)full_idx);
    }
    // Literal without indexing (top nibble 0000), name prefix 4.
    size_t o = HpackPrim.encode_int(out, cap, 4, 0x00, (uint32_t)name_idx);
    if (!o)
    {
        return 0;
    }
    if (name_idx == 0)
    {
        size_t ns = HpackPrim.encode_str(out + o, cap - o, name, name_len);
        if (!ns)
        {
            return 0;
        }
        o += ns;
    }
    size_t vs = HpackPrim.encode_str(out + o, cap - o, value, value_len);
    if (!vs)
    {
        return 0;
    }
    return o + vs;
}

// --- the entries ---

static void hpack_dyn_init(uint8_t *restrict work)
{
    hpack_dyn_init_run(HPACK_CTX(work), Hpack.init_args.max_bytes);
}

static void hpack_decode(uint8_t *restrict work)
{
    Hpack.ok =
        hpack_decode_run(HPACK_CTX(work), Hpack.decode_args.block, Hpack.decode_args.len, Hpack.decode_args.scratch,
                         Hpack.decode_args.scratch_cap, Hpack.decode_args.emit, Hpack.decode_args.ctx);
}

static void hpack_encode_header(uint8_t *restrict work)
{
    (void)work;
    Hpack.n = hpack_encode_header_run(Hpack.encode_args.out, Hpack.encode_args.cap, Hpack.encode_args.name,
                                      Hpack.encode_args.name_len, Hpack.encode_args.value, Hpack.encode_args.value_len);
}

// Designated, so a member's position in the struct does not decide what it binds to.
HpackNs Hpack = {.dyn_init = hpack_dyn_init, .decode = hpack_decode, .encode_header = hpack_encode_header};

#endif // PROTOCORE_ENABLE_HTTP2
