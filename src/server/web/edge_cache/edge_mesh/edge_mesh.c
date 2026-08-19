// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_mesh.c
 * @brief CDN edge-cache tier - mesh sibling-cache wire codec + async peer-query engine. See edge_mesh.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_EDGE_MESH

#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "server/web/edge_cache/edge_cache/edge_cache.h"
#include "server/web/edge_cache/edge_cache_sd/edge_cache_sd.h"
#include "server/web/edge_cache/edge_fetch/edge_fetch.h"
#include "server/web/edge_cache/edge_mesh/edge_mesh.h"
#include "services/storage/dbm/dbm.h"

static uint8_t edge_cache_sd_work[16]; // the borrow an entry takes; EdgeCacheSd never reads it

PROTOCORE_BEGIN_DECLS

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
    if (len >= 1 && buf[0] != PROTOCORE_EDGE_MESH_MAGIC0)
    {
        return PROTO_TRUE;
    }
    if (len >= 2 && buf[1] != PROTOCORE_EDGE_MESH_MAGIC1)
    {
        return PROTO_TRUE;
    }
    if (len >= 3 && buf[2] != PROTOCORE_EDGE_MESH_VERSION)
    {
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// --- frame codec ---------------------------------------------------------------------------------

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void edge_mesh_parse_response(uint8_t *restrict work);

static void edge_mesh_build_request(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *digest = EdgeMesh.build_request_args.digest;
    const char *canon = EdgeMesh.build_request_args.canon;
    const char *req_hdrs = EdgeMesh.build_request_args.req_hdrs;
    uint8_t *out = EdgeMesh.build_request_args.out;
    size_t cap = EdgeMesh.build_request_args.cap;

    if (!digest || !canon || !out)
    {
        EdgeMesh.n = 0;
        return;
    }
    const char *hdrs = req_hdrs ? req_hdrs : "";
    size_t kl = str.len(canon, PROTOCORE_EDGE_KEY_MAX);
    size_t hl = str.len(hdrs, PROTOCORE_MESH_HDRS_MAX);
    // Both lengths are strnlen-capped to PROTOCORE_EDGE_KEY_MAX (128) and PROTOCORE_MESH_HDRS_MAX, neither of
    // which is within three orders of magnitude of the 16-bit wire limit, so neither arm can be
    // taken in any build this library is sized for. The guard is what keeps the u16 length prefixes
    // below honest if either cap is ever raised.
    if (kl > 0xFFFFu || hl > 0xFFFFu)
    {
        EdgeMesh.n = 0;
        return;
    }
    size_t need = 2 + 1 + 1 + 32 + 2 + kl + 2 + hl;
    if (need > cap)
    {
        EdgeMesh.n = 0;
        return;
    }
    size_t pos = 0;
    out[pos++] = PROTOCORE_EDGE_MESH_MAGIC0;
    out[pos++] = PROTOCORE_EDGE_MESH_MAGIC1;
    out[pos++] = PROTOCORE_EDGE_MESH_VERSION;
    out[pos++] = PROTOCORE_EDGE_MESH_OP_GET;
    mem.cpy(out + pos, digest, 32);
    pos += 32;
    put_u16(out + pos, (uint16_t)kl);
    pos += 2;
    mem.cpy(out + pos, canon, kl);
    pos += kl;
    put_u16(out + pos, (uint16_t)hl);
    pos += 2;
    mem.cpy(out + pos, hdrs, hl);
    pos += hl;
    EdgeMesh.n = pos;
}

static void edge_mesh_parse_request(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = EdgeMesh.parse_request_args.buf;
    size_t len = EdgeMesh.parse_request_args.len;
    uint8_t *digest_out = EdgeMesh.parse_request_args.digest_out;
    char *canon_out = EdgeMesh.parse_request_args.canon_out;
    size_t canon_cap = EdgeMesh.parse_request_args.canon_cap;
    char *hdrs_out = EdgeMesh.parse_request_args.hdrs_out;
    size_t hdrs_cap = EdgeMesh.parse_request_args.hdrs_cap;

    if (magic_bad(buf, len))
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_MALFORMED;
        return;
    }
    if (len < 4)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_INCOMPLETE;
        return;
    }
    if (buf[3] != PROTOCORE_EDGE_MESH_OP_GET)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_MALFORMED;
        return;
    }
    size_t pos = 4;
    if (pos + 32 > len)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_INCOMPLETE;
        return;
    }
    size_t digest_off = pos;
    pos += 32;
    if (pos + 2 > len)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_INCOMPLETE;
        return;
    }
    uint16_t kl = get_u16(buf + pos);
    pos += 2;
    if (kl >= canon_cap)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_MALFORMED;
        return; // cannot fit the destination key buffer
    }
    if (pos + kl > len)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_INCOMPLETE;
        return;
    }
    size_t key_off = pos;
    pos += kl;
    if (pos + 2 > len)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_INCOMPLETE;
        return;
    }
    uint16_t hl = get_u16(buf + pos);
    pos += 2;
    if (hl >= hdrs_cap)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_MALFORMED;
        return;
    }
    if (pos + hl > len)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_INCOMPLETE;
        return;
    }
    size_t hdrs_off = pos;
    if (digest_out)
    {
        mem.cpy(digest_out, buf + digest_off, 32);
    }
    if (canon_out)
    {
        mem.cpy(canon_out, buf + key_off, kl);
        canon_out[kl] = '\0';
    }
    if (hdrs_out)
    {
        mem.cpy(hdrs_out, buf + hdrs_off, hl);
        hdrs_out[hl] = '\0';
    }
    EdgeMesh.parse = EDGE_MESH_PARSE_HIT;
    return; // a complete, valid request
}

static void edge_mesh_serialize_entry(uint8_t *restrict work)
{
    (void)work;
    const EdgeEntry *e = EdgeMesh.serialize_entry_args.e;
    long current_age = EdgeMesh.serialize_entry_args.current_age;
    uint8_t *out = EdgeMesh.serialize_entry_args.out;
    size_t cap = EdgeMesh.serialize_entry_args.cap;

    if (!e || !out || cap < PROTOCORE_EDGE_MESH_TRAILER)
    {
        EdgeMesh.n = 0;
        return;
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
    EdgeCacheSd.serialize_args.e = e;
    EdgeCacheSd.serialize_args.out = out + PROTOCORE_EDGE_MESH_TRAILER;
    EdgeCacheSd.serialize_args.cap = cap - PROTOCORE_EDGE_MESH_TRAILER;
    EdgeCacheSd.serialize(edge_cache_sd_work);
    size_t n = EdgeCacheSd.n;
    if (n == 0)
    {
        EdgeMesh.n = 0;
        return;
    }
    EdgeMesh.n = PROTOCORE_EDGE_MESH_TRAILER + n;
}

static void edge_mesh_deserialize_entry(uint8_t *restrict work)
{
    (void)work;
    uint8_t *entry_buf = EdgeMesh.deserialize_entry_args.entry_buf;
    const uint8_t *buf = EdgeMesh.deserialize_entry_args.buf;
    size_t len = EdgeMesh.deserialize_entry_args.len;
    EdgeEntry *e = EdgeMesh.deserialize_entry_args.e;
    uint32_t now_ms = EdgeMesh.deserialize_entry_args.now_ms;

    if (!buf || !e || len < PROTOCORE_EDGE_MESH_TRAILER)
    {
        EdgeMesh.ok = PROTO_FALSE;
        return;
    }
    int64_t date = get_i64(buf + 0);
    int64_t expires = get_i64(buf + 8);
    uint32_t lifetime = get_u32(buf + 16);
    uint32_t age_hdr = get_u32(buf + 20);
    uint32_t current_age = get_u32(buf + 24);
    EdgeCacheSd.deserialize_args.entry_buf = entry_buf;
    EdgeCacheSd.deserialize_args.buf = buf + PROTOCORE_EDGE_MESH_TRAILER;
    EdgeCacheSd.deserialize_args.len = len - PROTOCORE_EDGE_MESH_TRAILER;
    EdgeCacheSd.deserialize_args.e = e;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    if (!EdgeCacheSd.ok)
    {
        EdgeMesh.ok = PROTO_FALSE;
        return;
    }
    e->date_epoch = date;
    e->expires_epoch = expires;
    e->lifetime_s = (long)lifetime;
    e->age_hdr = (int32_t)age_hdr;
    e->initial_age = (long)current_age; // the sender's age at transfer -> receiver keeps propagating it
    e->insert_ms = now_ms;
    e->last_used_ms = now_ms;
    EdgeMesh.ok = PROTO_TRUE;
}

static void edge_mesh_build_response(uint8_t *restrict work)
{
    (void)work;
    proto_bool hit = EdgeMesh.build_response_args.hit;
    const uint8_t *entry = EdgeMesh.build_response_args.entry;
    size_t entry_len = EdgeMesh.build_response_args.entry_len;
    uint8_t *out = EdgeMesh.build_response_args.out;
    size_t cap = EdgeMesh.build_response_args.cap;

    if (!out || cap < 4)
    {
        EdgeMesh.n = 0;
        return;
    }
    size_t pos = 0;
    out[pos++] = PROTOCORE_EDGE_MESH_MAGIC0;
    out[pos++] = PROTOCORE_EDGE_MESH_MAGIC1;
    out[pos++] = PROTOCORE_EDGE_MESH_VERSION;
    out[pos++] = hit ? 1 : 0;
    if (hit)
    {
        if (!entry || entry_len == 0 || entry_len > 0xFFFFu || pos + 2 + entry_len > cap)
        {
            EdgeMesh.n = 0;
            return;
        }
        put_u16(out + pos, (uint16_t)entry_len);
        pos += 2;
        mem.cpy(out + pos, entry, entry_len);
        pos += entry_len;
    }
    EdgeMesh.n = pos;
}

static void edge_mesh_parse_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = EdgeMesh.parse_response_args.buf;
    size_t len = EdgeMesh.parse_response_args.len;
    size_t *entry_off = EdgeMesh.parse_response_args.entry_off;
    size_t *entry_len = EdgeMesh.parse_response_args.entry_len;

    if (magic_bad(buf, len))
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_MALFORMED;
        return;
    }
    if (len < 4)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_INCOMPLETE;
        return;
    }
    uint8_t status = buf[3];
    if (status == 0)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_MISS;
        return;
    }
    if (status != 1)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_MALFORMED;
        return;
    }
    if (len < 6)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_INCOMPLETE;
        return;
    }
    uint16_t el = get_u16(buf + 4);
    if (el == 0)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_MALFORMED;
        return;
    }
    if (len < (size_t)6 + el)
    {
        EdgeMesh.parse = EDGE_MESH_PARSE_INCOMPLETE;
        return;
    }
    if (entry_off)
    {
        *entry_off = 6;
    }
    if (entry_len)
    {
        *entry_len = el;
    }
    EdgeMesh.parse = EDGE_MESH_PARSE_HIT;
}

// --- async requester engine ----------------------------------------------------------------------

static void edge_mesh_fetch_begin(uint8_t *restrict work)
{
    (void)work;
    EdgeMeshFetch *m = EdgeMesh.fetch_begin_args.m;
    const EdgeFetchTransport *t = EdgeMesh.fetch_begin_args.t;
    const char *host = EdgeMesh.fetch_begin_args.host;
    uint16_t port = EdgeMesh.fetch_begin_args.port;
    const uint8_t *request = EdgeMesh.fetch_begin_args.request;
    size_t req_len = EdgeMesh.fetch_begin_args.req_len;
    uint8_t *buf = EdgeMesh.fetch_begin_args.buf;
    size_t cap = EdgeMesh.fetch_begin_args.cap;
    uint32_t now_ms = EdgeMesh.fetch_begin_args.now_ms;

    m->st = EDGE_MESH_STATUS_PENDING;
    m->cid = -1;
    m->start_ms = now_ms;
    m->got = 0;
    m->entry_off = 0;
    m->entry_len = 0;
    m->buf = buf;
    m->cap = cap;
    if (!t || !host || !request || req_len == 0 || !buf || cap < PROTOCORE_EDGE_MESH_RESP_MAX)
    {
        m->st = EDGE_MESH_STATUS_FAILED;
        return;
    }
    int cid =
        t->open(t->ctx, host, port, PROTOCORE_MESH_QUERY_MS); // blocking connect (LAN sibling), bounded by the timeout
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

static void edge_mesh_fetch_pump(uint8_t *restrict work)
{
    (void)work;
    EdgeMeshFetch *m = EdgeMesh.fetch_pump_args.m;
    const EdgeFetchTransport *t = EdgeMesh.fetch_pump_args.t;
    uint32_t now_ms = EdgeMesh.fetch_pump_args.now_ms;

    if (m->st != EDGE_MESH_STATUS_PENDING)
    {
        EdgeMesh.status = m->st;
        return;
    }
    if (!t || m->cid < 0)
    {
        m->st = EDGE_MESH_STATUS_FAILED;
        EdgeMesh.status = m->st;
        return;
    }
    if (now_ms - m->start_ms > PROTOCORE_MESH_QUERY_MS)
    {
        m->st = EDGE_MESH_STATUS_FAILED; // query deadline
        EdgeMesh.status = m->st;
        return;
    }

    if (m->got < m->cap)
    {
        m->got += t->read(t->ctx, m->cid, m->buf + m->got, m->cap - m->got);
    }

    size_t eoff = 0;
    size_t elen = 0;
    EdgeMesh.parse_response_args.buf = m->buf;
    EdgeMesh.parse_response_args.len = m->got;
    EdgeMesh.parse_response_args.entry_off = &eoff;
    EdgeMesh.parse_response_args.entry_len = &elen;
    edge_mesh_parse_response(work);
    EdgeMeshParse p = EdgeMesh.parse;
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
    EdgeMesh.status = m->st;
}

static void edge_mesh_fetch_end(uint8_t *restrict work)
{
    (void)work;
    EdgeMeshFetch *m = EdgeMesh.fetch_end_args.m;
    const EdgeFetchTransport *t = EdgeMesh.fetch_end_args.t;

    if (m->cid >= 0 && t)
    {
        t->close(t->ctx, m->cid);
    }
    m->cid = -1;
}

EdgeMeshNs EdgeMesh = {.build_request = edge_mesh_build_request,
                       .parse_request = edge_mesh_parse_request,
                       .serialize_entry = edge_mesh_serialize_entry,
                       .deserialize_entry = edge_mesh_deserialize_entry,
                       .build_response = edge_mesh_build_response,
                       .parse_response = edge_mesh_parse_response,
                       .fetch_begin = edge_mesh_fetch_begin,
                       .fetch_pump = edge_mesh_fetch_pump,
                       .fetch_end = edge_mesh_fetch_end};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_MESH
