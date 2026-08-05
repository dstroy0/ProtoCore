// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_dtls_handshake.c
 * @brief DTLS 1.3 handshake framing and reliability (RFC 9147 §5, §7). See pc_dtls_handshake.h.
 */

#include "network_drivers/presentation/security/dtls/dtls_handshake.h"

#if PC_ENABLE_DTLS

#include "crypto/ct_eq.h" // pc_ct_eq
#include "crypto/mac/hmac_sha256.h"

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
    if (n >= PC_DTLS_HS_REASM_MAX_RANGES)
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

size_t pc_dtls_hs_header_parse(const uint8_t *p, size_t len, DtlsHsHeader *out)
{
    if (len < PC_DTLS_HS_HDR_LEN)
    {
        return 0;
    }
    out->msg_type = p[0];
    out->length = ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    out->msg_seq = (uint16_t)(((uint16_t)p[4] << 8) | p[5]);
    out->frag_offset = ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 8) | p[8];
    out->frag_length = ((uint32_t)p[9] << 16) | ((uint32_t)p[10] << 8) | p[11];
    if (out->frag_offset + out->frag_length > out->length)
    {
        return 0; // fragment falls outside the declared message
    }
    if (PC_DTLS_HS_HDR_LEN + out->frag_length > len)
    {
        return 0; // fragment bytes truncated
    }
    out->fragment = p + PC_DTLS_HS_HDR_LEN;
    return PC_DTLS_HS_HDR_LEN + out->frag_length;
}

size_t pc_dtls_hs_frag_build(uint8_t msg_type, uint16_t msg_seq, uint32_t full_len, uint32_t frag_offset,
                             const uint8_t *frag, uint32_t frag_len, uint8_t *out, size_t out_cap)
{
    if (full_len > 0xFFFFFF || frag_offset > 0xFFFFFF || frag_len > 0xFFFFFF)
    {
        return 0; // uint24 fields
    }
    if (frag_offset + frag_len > full_len)
    {
        return 0;
    }
    size_t total = PC_DTLS_HS_HDR_LEN + frag_len;
    if (total > out_cap)
    {
        return 0;
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
        memcpy(out + PC_DTLS_HS_HDR_LEN, frag, frag_len);
    }
    return total;
}

// ---------------------------------------------------------------------------
// Message reassembly (RFC 9147 §5.4)
// ---------------------------------------------------------------------------

void pc_dtls_hs_reasm_init(DtlsHsReasm *r, uint16_t msg_seq, uint8_t *buf, size_t buf_cap)
{
    r->active = PROTO_FALSE;
    r->have_len = PROTO_FALSE;
    r->msg_type = 0;
    r->msg_seq = msg_seq;
    r->length = 0;
    r->buf = buf;
    r->buf_cap = buf_cap;
    r->range_count = 0;
}

int pc_dtls_hs_reasm_add(DtlsHsReasm *r, const DtlsHsHeader *frag)
{
    if (frag->msg_seq != r->msg_seq)
    {
        return 0; // a different message; the state machine decides what to do with it
    }
    if (!r->have_len)
    {
        if (frag->length > r->buf_cap)
        {
            return -1; // message will not fit the reassembly buffer
        }
        r->length = frag->length;
        r->msg_type = frag->msg_type;
        r->have_len = PROTO_TRUE;
        r->active = PROTO_TRUE;
    }
    else if (frag->length != r->length)
    {
        return -1; // fragments of one message must agree on its total length
    }
    uint32_t lo = frag->frag_offset;
    uint32_t hi = frag->frag_offset + frag->frag_length;
    if (hi > r->length)
    {
        return -1;
    }
    if (r->length == 0)
    {
        return 1; // empty body: complete as soon as the header is seen
    }
    if (frag->frag_length == 0)
    {
        return 0; // empty fragment of a non-empty message contributes nothing
    }
    memcpy(r->buf + lo, frag->fragment, frag->frag_length);
    if (reasm_merge(r, lo, hi) < 0)
    {
        return -1;
    }
    if (r->range_count == 1 && r->range_lo[0] == 0 && r->range_hi[0] >= r->length)
    {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// ACK message (RFC 9147 §7)
// ---------------------------------------------------------------------------

size_t pc_dtls_ack_build(const DtlsRecordNumber *nums, size_t count, uint8_t *out, size_t out_cap)
{
    size_t list_len = count * 16;
    if (list_len > 0xFFFF)
    {
        return 0;
    }
    size_t total = 2 + list_len;
    if (total > out_cap)
    {
        return 0;
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
    return total;
}

proto_bool pc_dtls_ack_parse(const uint8_t *body, size_t len, DtlsRecordNumber *out, size_t out_cap, size_t *out_count)
{
    if (len < 2)
    {
        return PROTO_FALSE;
    }
    size_t list_len = ((size_t)body[0] << 8) | body[1];
    if (list_len % 16 != 0 || 2 + list_len != len)
    {
        return PROTO_FALSE;
    }
    size_t n = list_len / 16;
    if (n > out_cap)
    {
        return PROTO_FALSE;
    }
    size_t o = 2;
    for (size_t i = 0; i < n; i++)
    {
        out[i].epoch = get_u64(body + o);
        out[i].seq = get_u64(body + o + 8);
        o += 16;
    }
    *out_count = n;
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// HelloRetryRequest cookie (RFC 9147 §5.1)
// ---------------------------------------------------------------------------

size_t pc_dtls_cookie_make(const uint8_t pc_hmac_key[32], uint64_t timestamp, const uint8_t *payload,
                           size_t payload_len, const uint8_t *client_addr, size_t addr_len, uint8_t *out,
                           size_t out_cap)
{
    if (payload_len > 0xFFFF)
    {
        return 0;
    }
    size_t body = 1 + 8 + 2 + payload_len; // version || timestamp || payload_len || payload
    size_t total = body + PC_HMAC_SHA256_LEN;
    if (total > out_cap || total > PC_DTLS_COOKIE_MAX)
    {
        return 0;
    }
    out[0] = 1; // cookie format version
    put_u64(out + 1, timestamp);
    out[9] = (uint8_t)(payload_len >> 8);
    out[10] = (uint8_t)payload_len;
    if (payload_len)
    {
        memcpy(out + 11, payload, payload_len);
    }
    // MAC covers version || timestamp || client_addr || payload_len || payload: the address is
    // authenticated (so a cookie cannot be replayed from another peer) without being stored.
    pc_hmac_sha256_ctx h;
    pc_hmac_sha256_init(&h, pc_hmac_key, 32);
    pc_hmac_sha256_update(&h, out, 9);
    pc_hmac_sha256_update(&h, client_addr, addr_len);
    pc_hmac_sha256_update(&h, out + 9, 2 + payload_len);
    pc_hmac_sha256_final(&h, out + body);
    return total;
}

proto_bool pc_dtls_cookie_verify(const uint8_t pc_hmac_key[32], uint64_t now, uint64_t max_age,
                                 const uint8_t *client_addr, size_t addr_len, const uint8_t *cookie, size_t cookie_len,
                                 uint8_t *payload_out, size_t payload_cap, size_t *payload_len_out)
{
    if (cookie_len < 1 + 8 + 2 + PC_HMAC_SHA256_LEN || cookie[0] != 1)
    {
        return PROTO_FALSE;
    }
    size_t payload_len = ((size_t)cookie[9] << 8) | cookie[10];
    size_t body = 1 + 8 + 2 + payload_len;
    if (body + PC_HMAC_SHA256_LEN != cookie_len) // exact-length: bounds payload before it is read
    {
        return PROTO_FALSE;
    }
    if (payload_len > payload_cap)
    {
        return PROTO_FALSE;
    }
    uint8_t mac[PC_HMAC_SHA256_LEN];
    pc_hmac_sha256_ctx h;
    pc_hmac_sha256_init(&h, pc_hmac_key, 32);
    pc_hmac_sha256_update(&h, cookie, 9);
    pc_hmac_sha256_update(&h, client_addr, addr_len);
    pc_hmac_sha256_update(&h, cookie + 9, 2 + payload_len);
    pc_hmac_sha256_final(&h, mac);
    if (!pc_ct_eq(mac, cookie + body, PC_HMAC_SHA256_LEN))
    {
        return PROTO_FALSE;
    }
    if (max_age != 0)
    {
        uint64_t ts = get_u64(cookie + 1);
        if (ts > now || now - ts > max_age)
        {
            return PROTO_FALSE; // future-dated or stale
        }
    }
    if (payload_len)
    {
        memcpy(payload_out, cookie + 11, payload_len);
    }
    *payload_len_out = payload_len;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_DTLS
