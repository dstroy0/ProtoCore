// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_dtls_handshake.c
 * @brief DTLS 1.3 handshake framing and reliability (RFC 9147 §5, §7). See protocore_dtls_handshake.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DTLS

#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/security/dtls/dtls_handshake/dtls_handshake.h"

#include "crypto/ct_eq.h" // protocore_ct_eq
#include "crypto/mac/hmac_sha256/hmac_sha256.h"

PROTOCORE_BEGIN_DECLS

static void put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
    {
        p[i] = (uint8_t)(v >> (8 * (7 - i)));
    }
}

static uint64_t get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
    {
        v = (v << 8) | p[i];
    }
    return v;
}

// Merge the received byte range [lo, hi) into the reassembler's sorted, disjoint interval list,
// absorbing any overlapping or adjacent intervals. Returns -1 if there is no room to insert a new
// disjoint interval (the fragmentation is too scattered for the bounded list).
static int reasm_merge(DtlsHsReasm *r, uint32_t lo, uint32_t hi)
{
    uint8_t n = r->range_count;
    uint8_t i = 0;
    while (i < n)
    {
        // Disjoint (a gap on either side): keep and advance. Touching (hi == lo) merges so the
        // completion check can collapse contiguous fragments into a single [0, length) interval.
        if (r->range_hi[i] < lo || r->range_lo[i] > hi)
        {
            i++;
            continue;
        }
        if (r->range_lo[i] < lo)
        {
            lo = r->range_lo[i];
        }
        if (r->range_hi[i] > hi)
        {
            hi = r->range_hi[i];
        }
        for (uint8_t j = i; j + 1 < n; j++) // remove interval i; re-check the one shifted into its slot
        {
            r->range_lo[j] = r->range_lo[j + 1];
            r->range_hi[j] = r->range_hi[j + 1];
        }
        n--;
    }
    if (n >= PROTOCORE_DTLS_HS_REASM_MAX_RANGES)
    {
        return -1;
    }
    uint8_t k = n; // insert [lo, hi) keeping the list sorted by lo
    while (k > 0 && r->range_lo[k - 1] > lo)
    {
        r->range_lo[k] = r->range_lo[k - 1];
        r->range_hi[k] = r->range_hi[k - 1];
        k--;
    }
    r->range_lo[k] = lo;
    r->range_hi[k] = hi;
    r->range_count = (uint8_t)(n + 1);
    return 0;
}

// ---------------------------------------------------------------------------
// Handshake message header (RFC 9147 §5.2)
// ---------------------------------------------------------------------------

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_dtls_handshake_header_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *p = DtlsHandshakeV.header_parse_args.p;
    size_t len = DtlsHandshakeV.header_parse_args.len;
    DtlsHsHeader *out = DtlsHandshakeV.header_parse_args.out;

    if (len < PROTOCORE_DTLS_HS_HDR_LEN)
    {
        DtlsHandshakeV.n = 0;
        return;
    }
    out->msg_type = p[0];
    out->length = ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    out->msg_seq = (uint16_t)(((uint16_t)p[4] << 8) | p[5]);
    out->frag_offset = ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 8) | p[8];
    out->frag_length = ((uint32_t)p[9] << 16) | ((uint32_t)p[10] << 8) | p[11];
    if (out->frag_offset + out->frag_length > out->length)
    {
        DtlsHandshakeV.n = 0; // fragment falls outside the declared message
        return;
    }
    if (PROTOCORE_DTLS_HS_HDR_LEN + out->frag_length > len)
    {
        DtlsHandshakeV.n = 0; // fragment bytes truncated
        return;
    }
    out->fragment = p + PROTOCORE_DTLS_HS_HDR_LEN;
    DtlsHandshakeV.n = PROTOCORE_DTLS_HS_HDR_LEN + out->frag_length;
}

void protocore_dtls_handshake_frag_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t msg_type = DtlsHandshakeV.frag_build_args.msg_type;
    uint16_t msg_seq = DtlsHandshakeV.frag_build_args.msg_seq;
    uint32_t full_len = DtlsHandshakeV.frag_build_args.full_len;
    uint32_t frag_offset = DtlsHandshakeV.frag_build_args.frag_offset;
    const uint8_t *frag = DtlsHandshakeV.frag_build_args.frag;
    uint32_t frag_len = DtlsHandshakeV.frag_build_args.frag_len;
    uint8_t *out = DtlsHandshakeV.frag_build_args.out;
    size_t out_cap = DtlsHandshakeV.frag_build_args.out_cap;

    if (full_len > 0xFFFFFF || frag_offset > 0xFFFFFF || frag_len > 0xFFFFFF)
    {
        DtlsHandshakeV.n = 0; // uint24 fields
        return;
    }
    if (frag_offset + frag_len > full_len)
    {
        DtlsHandshakeV.n = 0;
        return;
    }
    size_t total = PROTOCORE_DTLS_HS_HDR_LEN + frag_len;
    if (total > out_cap)
    {
        DtlsHandshakeV.n = 0;
        return;
    }
    out[0] = msg_type;
    out[1] = (uint8_t)(full_len >> 16);
    out[2] = (uint8_t)(full_len >> 8);
    out[3] = (uint8_t)full_len;
    out[4] = (uint8_t)(msg_seq >> 8);
    out[5] = (uint8_t)msg_seq;
    out[6] = (uint8_t)(frag_offset >> 16);
    out[7] = (uint8_t)(frag_offset >> 8);
    out[8] = (uint8_t)frag_offset;
    out[9] = (uint8_t)(frag_len >> 16);
    out[10] = (uint8_t)(frag_len >> 8);
    out[11] = (uint8_t)frag_len;
    if (frag_len)
    {
        mem.cpy(out + PROTOCORE_DTLS_HS_HDR_LEN, frag, frag_len);
    }
    DtlsHandshakeV.n = total;
}

// ---------------------------------------------------------------------------
// Message reassembly (RFC 9147 §5.4)
// ---------------------------------------------------------------------------

void protocore_dtls_handshake_reasm_init(uint8_t *restrict work)
{
    (void)work;
    DtlsHsReasm *r = DtlsHandshakeV.reasm_init_args.r;
    uint16_t msg_seq = DtlsHandshakeV.reasm_init_args.msg_seq;
    uint8_t *buf = DtlsHandshakeV.reasm_init_args.buf;
    size_t buf_cap = DtlsHandshakeV.reasm_init_args.buf_cap;

    r->active = PROTO_FALSE;
    r->have_len = PROTO_FALSE;
    r->msg_type = 0;
    r->msg_seq = msg_seq;
    r->length = 0;
    r->buf = buf;
    r->buf_cap = buf_cap;
    r->range_count = 0;
}

void protocore_dtls_handshake_reasm_add(uint8_t *restrict work)
{
    (void)work;
    DtlsHsReasm *r = DtlsHandshakeV.reasm_add_args.r;
    const DtlsHsHeader *frag = DtlsHandshakeV.reasm_add_args.frag;

    if (frag->msg_seq != r->msg_seq)
    {
        DtlsHandshakeV.n = 0; // a different message; the state machine decides what to do with it
        return;
    }
    if (!r->have_len)
    {
        if (frag->length > r->buf_cap)
        {
            DtlsHandshakeV.n = -1; // message will not fit the reassembly buffer
            return;
        }
        r->length = frag->length;
        r->msg_type = frag->msg_type;
        r->have_len = PROTO_TRUE;
        r->active = PROTO_TRUE;
    }
    else if (frag->length != r->length)
    {
        DtlsHandshakeV.n = -1; // fragments of one message must agree on its total length
        return;
    }
    uint32_t lo = frag->frag_offset;
    uint32_t hi = frag->frag_offset + frag->frag_length;
    if (hi > r->length)
    {
        DtlsHandshakeV.n = -1;
        return;
    }
    if (r->length == 0)
    {
        DtlsHandshakeV.n = 1; // empty body: complete as soon as the header is seen
        return;
    }
    if (frag->frag_length == 0)
    {
        DtlsHandshakeV.n = 0; // empty fragment of a non-empty message contributes nothing
        return;
    }
    // sec 5.5: "Senders MUST NOT change handshake message bytes upon retransmission. Receivers MAY
    // check that retransmitted bytes are identical and SHOULD abort the handshake with an
    // illegal_parameter alert if the value of a byte changes." Every byte this fragment shares with
    // one already held is compared before the copy overwrites it.
    for (uint8_t i = 0; i < r->range_count; i++)
    {
        uint32_t ov_lo = r->range_lo[i];
        if (lo > ov_lo)
        {
            ov_lo = lo;
        }
        uint32_t ov_hi = r->range_hi[i];
        if (hi < ov_hi)
        {
            ov_hi = hi;
        }
        if (ov_lo < ov_hi && mem.cmp(r->buf + ov_lo, frag->fragment + (ov_lo - lo), ov_hi - ov_lo) != 0)
        {
            DtlsHandshakeV.n = -1;
            return;
        }
    }
    mem.cpy(r->buf + lo, frag->fragment, frag->frag_length);
    if (reasm_merge(r, lo, hi) < 0)
    {
        DtlsHandshakeV.n = -1;
        return;
    }
    if (r->range_count == 1 && r->range_lo[0] == 0 && r->range_hi[0] >= r->length)
    {
        DtlsHandshakeV.n = 1;
        return;
    }
    DtlsHandshakeV.n = 0;
}

// ---------------------------------------------------------------------------
// ACK message (RFC 9147 §7)
// ---------------------------------------------------------------------------

void protocore_dtls_handshake_ack_build(uint8_t *restrict work)
{
    (void)work;
    const DtlsRecordNumber *nums = DtlsHandshakeV.ack_build_args.nums;
    size_t count = DtlsHandshakeV.ack_build_args.count;
    uint8_t *out = DtlsHandshakeV.ack_build_args.out;
    size_t out_cap = DtlsHandshakeV.ack_build_args.out_cap;

    size_t list_len = count * 16;
    if (list_len > 0xFFFF)
    {
        DtlsHandshakeV.n = 0;
        return;
    }
    size_t total = 2 + list_len;
    if (total > out_cap)
    {
        DtlsHandshakeV.n = 0;
        return;
    }
    out[0] = (uint8_t)(list_len >> 8);
    out[1] = (uint8_t)list_len;
    size_t o = 2;
    for (size_t i = 0; i < count; i++)
    {
        put_u64(out + o, nums[i].epoch);
        put_u64(out + o + 8, nums[i].seq);
        o += 16;
    }
    DtlsHandshakeV.n = total;
}

void protocore_dtls_handshake_ack_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *body = DtlsHandshakeV.ack_parse_args.body;
    size_t len = DtlsHandshakeV.ack_parse_args.len;
    DtlsRecordNumber *out = DtlsHandshakeV.ack_parse_args.out;
    size_t out_cap = DtlsHandshakeV.ack_parse_args.out_cap;
    size_t *out_count = DtlsHandshakeV.ack_parse_args.out_count;

    if (len < 2)
    {
        DtlsHandshakeV.ok = PROTO_FALSE;
        return;
    }
    size_t list_len = ((size_t)body[0] << 8) | body[1];
    if (list_len % 16 != 0 || 2 + list_len != len)
    {
        DtlsHandshakeV.ok = PROTO_FALSE;
        return;
    }
    size_t n = list_len / 16;
    if (n > out_cap)
    {
        DtlsHandshakeV.ok = PROTO_FALSE;
        return;
    }
    size_t o = 2;
    for (size_t i = 0; i < n; i++)
    {
        out[i].epoch = get_u64(body + o);
        out[i].seq = get_u64(body + o + 8);
        o += 16;
    }
    *out_count = n;
    DtlsHandshakeV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// HelloRetryRequest cookie (RFC 9147 §5.1)
// ---------------------------------------------------------------------------

void protocore_dtls_handshake_cookie_make(uint8_t *restrict work)
{
    (void)work;
    uint8_t *mac_work = DtlsHandshakeV.cookie_make_args.mac_work;
    const uint8_t *protocore_hmac_key = DtlsHandshakeV.cookie_make_args.protocore_hmac_key;
    uint64_t timestamp = DtlsHandshakeV.cookie_make_args.timestamp;
    const uint8_t *payload = DtlsHandshakeV.cookie_make_args.payload;
    size_t payload_len = DtlsHandshakeV.cookie_make_args.payload_len;
    const uint8_t *client_addr = DtlsHandshakeV.cookie_make_args.client_addr;
    size_t addr_len = DtlsHandshakeV.cookie_make_args.addr_len;
    uint8_t *out = DtlsHandshakeV.cookie_make_args.out;
    size_t out_cap = DtlsHandshakeV.cookie_make_args.out_cap;

    if (payload_len > 0xFFFF)
    {
        DtlsHandshakeV.n = 0;
        return;
    }
    size_t body = 1 + 8 + 2 + payload_len; // version || timestamp || payload_len || payload
    size_t total = body + PROTOCORE_HMAC_SHA256_LEN;
    if (total > out_cap || total > PROTOCORE_DTLS_COOKIE_MAX)
    {
        DtlsHandshakeV.n = 0;
        return;
    }
    out[0] = 1; // cookie format version
    put_u64(out + 1, timestamp);
    out[9] = (uint8_t)(payload_len >> 8);
    out[10] = (uint8_t)payload_len;
    if (payload_len)
    {
        mem.cpy(out + 11, payload, payload_len);
    }
    // MAC covers version || timestamp || client_addr || payload_len || payload: the address is
    // authenticated (so a cookie cannot be replayed from another peer) without being stored.
    HmacSha256.key_args.key = protocore_hmac_key;
    HmacSha256.key_args.key_len = 32;
    HmacSha256.init(mac_work);
    HmacSha256.update_args.data = out;
    HmacSha256.update_args.len = 9;
    HmacSha256.update(mac_work);
    HmacSha256.update_args.data = client_addr;
    HmacSha256.update_args.len = addr_len;
    HmacSha256.update(mac_work);
    HmacSha256.update_args.data = out + 9;
    HmacSha256.update_args.len = 2 + payload_len;
    HmacSha256.update(mac_work);
    HmacSha256.final_args.out = out + body;
    HmacSha256.final(mac_work);
    DtlsHandshakeV.n = total;
}

void protocore_dtls_handshake_cookie_verify(uint8_t *restrict work)
{
    (void)work;
    uint8_t *mac_work = DtlsHandshakeV.cookie_verify_args.mac_work;
    const uint8_t *protocore_hmac_key = DtlsHandshakeV.cookie_verify_args.protocore_hmac_key;
    uint64_t now = DtlsHandshakeV.cookie_verify_args.now;
    uint64_t max_age = DtlsHandshakeV.cookie_verify_args.max_age;
    const uint8_t *client_addr = DtlsHandshakeV.cookie_verify_args.client_addr;
    size_t addr_len = DtlsHandshakeV.cookie_verify_args.addr_len;
    const uint8_t *cookie = DtlsHandshakeV.cookie_verify_args.cookie;
    size_t cookie_len = DtlsHandshakeV.cookie_verify_args.cookie_len;
    uint8_t *payload_out = DtlsHandshakeV.cookie_verify_args.payload_out;
    size_t payload_cap = DtlsHandshakeV.cookie_verify_args.payload_cap;
    size_t *payload_len_out = DtlsHandshakeV.cookie_verify_args.payload_len_out;

    if (cookie_len < 1 + 8 + 2 + PROTOCORE_HMAC_SHA256_LEN || cookie[0] != 1)
    {
        DtlsHandshakeV.ok = PROTO_FALSE;
        return;
    }
    size_t payload_len = ((size_t)cookie[9] << 8) | cookie[10];
    size_t body = 1 + 8 + 2 + payload_len;
    if (body + PROTOCORE_HMAC_SHA256_LEN != cookie_len) // exact-length: bounds payload before it is read
    {
        DtlsHandshakeV.ok = PROTO_FALSE;
        return;
    }
    if (payload_len > payload_cap)
    {
        DtlsHandshakeV.ok = PROTO_FALSE;
        return;
    }
    uint8_t mac[PROTOCORE_HMAC_SHA256_LEN];
    HmacSha256.key_args.key = protocore_hmac_key;
    HmacSha256.key_args.key_len = 32;
    HmacSha256.init(mac_work);
    HmacSha256.update_args.data = cookie;
    HmacSha256.update_args.len = 9;
    HmacSha256.update(mac_work);
    HmacSha256.update_args.data = client_addr;
    HmacSha256.update_args.len = addr_len;
    HmacSha256.update(mac_work);
    HmacSha256.update_args.data = cookie + 9;
    HmacSha256.update_args.len = 2 + payload_len;
    HmacSha256.update(mac_work);
    HmacSha256.final_args.out = mac;
    HmacSha256.final(mac_work);
    if (!protocore_ct_eq(mac, cookie + body, PROTOCORE_HMAC_SHA256_LEN))
    {
        DtlsHandshakeV.ok = PROTO_FALSE;
        return;
    }
    if (max_age != 0)
    {
        uint64_t ts = get_u64(cookie + 1);
        if (ts > now || now - ts > max_age)
        {
            DtlsHandshakeV.ok = PROTO_FALSE; // future-dated or stale
            return;
        }
    }
    if (payload_len)
    {
        mem.cpy(payload_out, cookie + 11, payload_len);
    }
    *payload_len_out = payload_len;
    DtlsHandshakeV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
DtlsHandshakeVars DtlsHandshakeV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DTLS
