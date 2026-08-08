// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_mesh.c
 * @brief CDN edge-cache tier - mesh sibling-cache wire codec + async peer-query engine. See edge_mesh.h.
 */

#include "services/web/edge_cache/edge_mesh.h"

#if PC_ENABLE_EDGE_MESH

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}
static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put_i64(uint8_t *p, int64_t v)
{
    uint64_t u = (uint64_t)v;
    for (int i = 0; i < 8; i++)
    {
        p[i] = (uint8_t)(u >> (8 * i));
    }
}
static int64_t get_i64(const uint8_t *p)
{
    uint64_t u = 0;
    for (int i = 0; i < 8; i++)
    {
        u |= (uint64_t)p[i] << (8 * i);
    }
    return (int64_t)u;
}

// True (and stops) once buf[0..min(len,4)] diverges from the fixed request/response magic+version prefix.
static proto_bool magic_bad(const uint8_t *buf, size_t len)
{
    if (len >= 1 && buf[0] != PC_EDGE_MESH_MAGIC0)
    {
        return PROTO_TRUE;
    }
    if (len >= 2 && buf[1] != PC_EDGE_MESH_MAGIC1)
    {
        return PROTO_TRUE;
    }
    if (len >= 3 && buf[2] != PC_EDGE_MESH_VERSION)
    {
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// --- frame codec ---------------------------------------------------------------------------------

size_t edge_mesh_build_request(const uint8_t digest[32], const char *canon, const char *req_hdrs, uint8_t *out,
                               size_t cap)
{
    if (!digest || !canon || !out)
    {
        return 0;
    }
    const char *hdrs = req_hdrs ? req_hdrs : "";
    size_t kl = strnlen(canon, PC_EDGE_KEY_MAX);
    size_t hl = strnlen(hdrs, PC_MESH_HDRS_MAX);
    // Both lengths are strnlen-capped to PC_EDGE_KEY_MAX (128) and PC_MESH_HDRS_MAX, neither of
    // which is within three orders of magnitude of the 16-bit wire limit, so neither arm can be
    // taken in any build this library is sized for. The guard is what keeps the u16 length prefixes
    // below honest if either cap is ever raised.
    if (kl > 0xFFFFu || hl > 0xFFFFu)
    {
        return 0;
    }
    size_t need = 2 + 1 + 1 + 32 + 2 + kl + 2 + hl;
    if (need > cap)
    {
        return 0;
    }
    size_t pos = 0;
    out[pos++] = PC_EDGE_MESH_MAGIC0;
    out[pos++] = PC_EDGE_MESH_MAGIC1;
    out[pos++] = PC_EDGE_MESH_VERSION;
    out[pos++] = PC_EDGE_MESH_OP_GET;
    memcpy(out + pos, digest, 32);
    pos += 32;
    put_u16(out + pos, (uint16_t)kl);
    pos += 2;
    memcpy(out + pos, canon, kl);
    pos += kl;
    put_u16(out + pos, (uint16_t)hl);
    pos += 2;
    memcpy(out + pos, hdrs, hl);
    pos += hl;
    return pos;
}

EdgeMeshParse edge_mesh_parse_request(const uint8_t *buf, size_t len, uint8_t digest_out[32], char *canon_out,
                                      size_t canon_cap, char *hdrs_out, size_t hdrs_cap)
{
    if (magic_bad(buf, len))
    {
        return EDGE_MESH_PARSE_MALFORMED;
    }
    if (len < 4)
    {
        return EDGE_MESH_PARSE_INCOMPLETE;
    }
    if (buf[3] != PC_EDGE_MESH_OP_GET)
    {
        return EDGE_MESH_PARSE_MALFORMED;
    }
    size_t pos = 4;
    if (pos + 32 > len)
    {
        return EDGE_MESH_PARSE_INCOMPLETE;
    }
    size_t digest_off = pos;
    pos += 32;
    if (pos + 2 > len)
    {
        return EDGE_MESH_PARSE_INCOMPLETE;
    }
    uint16_t kl = get_u16(buf + pos);
    pos += 2;
    if (kl >= canon_cap)
    {
        return EDGE_MESH_PARSE_MALFORMED; // cannot fit the destination key buffer
    }
    if (pos + kl > len)
    {
        return EDGE_MESH_PARSE_INCOMPLETE;
    }
    size_t key_off = pos;
    pos += kl;
    if (pos + 2 > len)
    {
        return EDGE_MESH_PARSE_INCOMPLETE;
    }
    uint16_t hl = get_u16(buf + pos);
    pos += 2;
    if (hl >= hdrs_cap)
    {
        return EDGE_MESH_PARSE_MALFORMED;
    }
    if (pos + hl > len)
    {
        return EDGE_MESH_PARSE_INCOMPLETE;
    }
    size_t hdrs_off = pos;
    if (digest_out)
    {
        memcpy(digest_out, buf + digest_off, 32);
    }
    if (canon_out)
    {
        memcpy(canon_out, buf + key_off, kl);
        canon_out[kl] = '\0';
    }
    if (hdrs_out)
    {
        memcpy(hdrs_out, buf + hdrs_off, hl);
        hdrs_out[hl] = '\0';
    }
    return EDGE_MESH_PARSE_HIT; // a complete, valid request
}

size_t edge_mesh_serialize_entry(const EdgeEntry *e, long current_age, uint8_t *out, size_t cap)
{
    if (!e || !out || cap < PC_EDGE_MESH_TRAILER)
    {
        return 0;
    }
    if (current_age < 0)
    {
        current_age = 0;
    }
    put_i64(out + 0, e->date_epoch);
    put_i64(out + 8, e->expires_epoch);
    put_u32(out + 16, (uint32_t)(e->lifetime_s < 0 ? 0 : e->lifetime_s));
    put_u32(out + 20, (uint32_t)(e->age_hdr < 0 ? 0 : e->age_hdr));
    put_u32(out + 24, (uint32_t)current_age);
    size_t n = edge_sd_serialize(e, out + PC_EDGE_MESH_TRAILER, cap - PC_EDGE_MESH_TRAILER);
    if (n == 0)
    {
        return 0;
    }
    return PC_EDGE_MESH_TRAILER + n;
}

proto_bool edge_mesh_deserialize_entry(const uint8_t *buf, size_t len, EdgeEntry *e, uint32_t now_ms)
{
    if (!buf || !e || len < PC_EDGE_MESH_TRAILER)
    {
        return PROTO_FALSE;
    }
    int64_t date = get_i64(buf + 0);
    int64_t expires = get_i64(buf + 8);
    uint32_t lifetime = get_u32(buf + 16);
    uint32_t age_hdr = get_u32(buf + 20);
    uint32_t current_age = get_u32(buf + 24);
    if (!edge_sd_deserialize(buf + PC_EDGE_MESH_TRAILER, len - PC_EDGE_MESH_TRAILER, e))
    {
        return PROTO_FALSE;
    }
    e->date_epoch = date;
    e->expires_epoch = expires;
    e->lifetime_s = (long)lifetime;
    e->age_hdr = (int32_t)age_hdr;
    e->initial_age = (long)current_age; // the sender's age at transfer -> receiver keeps propagating it
    e->insert_ms = now_ms;
    e->last_used_ms = now_ms;
    return PROTO_TRUE;
}

size_t edge_mesh_build_response(proto_bool hit, const uint8_t *entry, size_t entry_len, uint8_t *out, size_t cap)
{
    if (!out || cap < 4)
    {
        return 0;
    }
    size_t pos = 0;
    out[pos++] = PC_EDGE_MESH_MAGIC0;
    out[pos++] = PC_EDGE_MESH_MAGIC1;
    out[pos++] = PC_EDGE_MESH_VERSION;
    out[pos++] = hit ? 1 : 0;
    if (hit)
    {
        if (!entry || entry_len == 0 || entry_len > 0xFFFFu || pos + 2 + entry_len > cap)
        {
            return 0;
        }
        put_u16(out + pos, (uint16_t)entry_len);
        pos += 2;
        memcpy(out + pos, entry, entry_len);
        pos += entry_len;
    }
    return pos;
}

EdgeMeshParse edge_mesh_parse_response(const uint8_t *buf, size_t len, size_t *entry_off, size_t *entry_len)
{
    if (magic_bad(buf, len))
    {
        return EDGE_MESH_PARSE_MALFORMED;
    }
    if (len < 4)
    {
        return EDGE_MESH_PARSE_INCOMPLETE;
    }
    uint8_t status = buf[3];
    if (status == 0)
    {
        return EDGE_MESH_PARSE_MISS;
    }
    if (status != 1)
    {
        return EDGE_MESH_PARSE_MALFORMED;
    }
    if (len < 6)
    {
        return EDGE_MESH_PARSE_INCOMPLETE;
    }
    uint16_t el = get_u16(buf + 4);
    if (el == 0)
    {
        return EDGE_MESH_PARSE_MALFORMED;
    }
    if (len < (size_t)6 + el)
    {
        return EDGE_MESH_PARSE_INCOMPLETE;
    }
    if (entry_off)
    {
        *entry_off = 6;
    }
    if (entry_len)
    {
        *entry_len = el;
    }
    return EDGE_MESH_PARSE_HIT;
}

// --- async requester engine ----------------------------------------------------------------------

void edge_mesh_fetch_begin(EdgeMeshFetch *m, const EdgeFetchTransport *t, const char *host, uint16_t port,
                           const uint8_t *request, size_t req_len, uint8_t *buf, size_t cap, uint32_t now_ms)
{
    m->st = EDGE_MESH_STATUS_PENDING;
    m->cid = -1;
    m->start_ms = now_ms;
    m->got = 0;
    m->entry_off = 0;
    m->entry_len = 0;
    m->buf = buf;
    m->cap = cap;
    if (!t || !host || !request || req_len == 0 || !buf || cap < PC_EDGE_MESH_RESP_MAX)
    {
        m->st = EDGE_MESH_STATUS_FAILED;
        return;
    }
    int cid = t->open(t->ctx, host, port, PC_MESH_QUERY_MS); // blocking connect (LAN sibling), bounded by the timeout
    if (cid < 0)
    {
        m->st = EDGE_MESH_STATUS_FAILED;
        return;
    }
    m->cid = cid;
    if (!t->send(t->ctx, cid, request, req_len))
    {
        t->close(t->ctx, cid);
        m->cid = -1;
        m->st = EDGE_MESH_STATUS_FAILED;
    }
}

EdgeMeshStatus edge_mesh_fetch_pump(EdgeMeshFetch *m, const EdgeFetchTransport *t, uint32_t now_ms)
{
    if (m->st != EDGE_MESH_STATUS_PENDING)
    {
        return m->st;
    }
    if (!t || m->cid < 0)
    {
        m->st = EDGE_MESH_STATUS_FAILED;
        return m->st;
    }
    if (now_ms - m->start_ms > PC_MESH_QUERY_MS)
    {
        m->st = EDGE_MESH_STATUS_FAILED; // query deadline
        return m->st;
    }

    if (m->got < m->cap)
    {
        m->got += t->read(t->ctx, m->cid, m->buf + m->got, m->cap - m->got);
    }

    size_t eoff = 0;
    size_t elen = 0;
    EdgeMeshParse p = edge_mesh_parse_response(m->buf, m->got, &eoff, &elen);
    if (p == EDGE_MESH_PARSE_HIT)
    {
        m->entry_off = eoff;
        m->entry_len = elen;
        m->st = EDGE_MESH_STATUS_HIT;
    }
    else if (p == EDGE_MESH_PARSE_MISS)
    {
        m->st = EDGE_MESH_STATUS_MISS;
    }
    else if (p == EDGE_MESH_PARSE_MALFORMED)
    {
        m->st = EDGE_MESH_STATUS_FAILED;
    }
    else if (m->got >= m->cap || t->closed(t->ctx, m->cid))
    {
        m->st = EDGE_MESH_STATUS_FAILED; // buffer full still short, or peer closed before a complete frame
    }
    return m->st;
}

void edge_mesh_fetch_end(EdgeMeshFetch *m, const EdgeFetchTransport *t)
{
    if (m->cid >= 0 && t)
    {
        t->close(t->ctx, m->cid);
    }
    m->cid = -1;
}

#endif // PC_ENABLE_EDGE_MESH
