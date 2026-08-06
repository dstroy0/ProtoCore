// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file h3_server.c
 * @brief The HTTP/3 request bridge. See h3_server.h.
 */

#include "network_drivers/presentation/http/http3/h3_server.h"

#if PC_ENABLE_HTTP3

#include "board_drivers/board_profiles/pc_platform.h" // pc_platform_rand_u32: the device TRNG
#include "mmgr/rawmemcpy.h"                           // proto_raw_read: each field moves into the slot
#include "network_drivers/presentation/http/http.h"   // Http.match_and_execute
#include "network_drivers/transport/tcp.h"            // TcpConn, conn_pool: the reserved dispatch slot
#include "protocore.h"                                // http_pool, PC_H3_DISPATCH_SLOT, http_reset
#include "shared_primitives/runops.h"                 // every scan and search on the mapped fields

// Randomness for the QUIC ephemeral X25519 key, the ServerHello random, and our connection IDs: the
// hardware TRNG on device; a deterministic PRNG on host (test builds carry no security context and
// have no esp_random).
void pc_h3_server_rng(uint8_t *out, size_t len)
{
#if PROTOCORE_HOT
    size_t i = 0;
    while (i < len)
    {
        uint32_t r = pc_platform_rand_u32();
        size_t n = (len - i) < 4 ? (len - i) : 4;
        proto_raw_read(out + i, &r, n);
        i += n;
    }
#else
    static uint32_t s = 0x9e3779b9u;
    for (size_t i = 0; i < len; i++)
    {
        s = s * 1664525u + 1013904223u;
        out[i] = (uint8_t)(s >> 24);
    }
#endif
}

// Response sink for the HTTP/3 dispatch slot: route (code, content_type, body) onto the QUIC stream
// the request arrived on (ids stashed on the slot by dispatch_h3_request). Installed as conn->pc_resp_sink
// so send_text()/send_empty() stay protocol-agnostic.
static proto_bool pc_h3_resp_sink(uint8_t slot, int code, const char *content_type, const char *body, size_t len)
{
    TcpConn *c = &conn_pool[slot];
    return pc_quic_server_respond(c->pc_h3_conn_id, c->pc_h3_stream, code, content_type, (const uint8_t *)body, len);
}

void pc_h3_server_request(void *app, uint32_t conn_id, uint64_t stream_id, const char *method, const char *path,
                          const char *authority, const uint8_t *body, size_t body_len)
{
    (void)app; // the route table and the slot pools are global owners; nothing is carried here
    const uint8_t slot = PC_H3_DISPATCH_SLOT;
    HttpReq *r = &http_pool[slot];
    http_reset(slot);

    // Map the semantic request fields into the shared HttpReq (as pc_h2_server does per stream).
    size_t mn = proto_scan_nul(method, sizeof(r->method));
    if (mn >= sizeof(r->method))
    {
        mn = sizeof(r->method) - 1;
    }
    proto_raw_read(r->method, method, mn);
    r->method[mn] = 0;

    // Bounded by everything the request could occupy here, path and query together, rather than by
    // the path field alone: a '?' past the path cap still names a query this slot has room for, and
    // capping the search at the path would drop it while keeping the truncated path.
    const char *q = proto_find(path, sizeof(r->path) + sizeof(r->query), "?", sizeof("?"));
    size_t plen = (q != NULL) ? (size_t)(q - path) : proto_scan_nul(path, sizeof(r->path));
    if (plen >= sizeof(r->path))
    {
        plen = sizeof(r->path) - 1;
    }
    proto_raw_read(r->path, path, plen);
    r->path[plen] = 0;
    r->path_idx = proto_scan_nul(r->path, sizeof(r->path));
    if (q != NULL)
    {
        size_t ql = proto_scan_nul(q + 1, sizeof(r->query));
        if (ql >= sizeof(r->query))
        {
            ql = sizeof(r->query) - 1;
        }
        proto_raw_read(r->query, q + 1, ql);
        r->query[ql] = 0;
        r->query_idx = proto_scan_nul(r->query, sizeof(r->query));
    }

    // :authority maps to Host, the way the h2 bridge does.
    if (authority && authority[0] && r->header_count < MAX_HEADERS)
    {
        Header *h = &r->headers[r->header_count];
        r->header_count++;
        proto_raw_read(h->key, "host", 5);
        size_t vl = proto_scan_nul(authority, sizeof(h->val));
        if (vl >= sizeof(h->val))
        {
            vl = sizeof(h->val) - 1;
        }
        proto_raw_read(h->val, authority, vl);
        h->val[vl] = 0;
    }

    if (body && body_len)
    {
        size_t n = body_len > BODY_BUF_SIZE ? BODY_BUF_SIZE : body_len;
        proto_raw_read(r->body, body, n);
        r->body_len = n;
        r->body[r->body_len] = 0;
        r->body_bytes_read = body_len;
        r->content_length = body_len;
    }
    r->parse_state = PARSE_COMPLETE;

    // Mark the reserved slot as HTTP/3 and install the response sink so send_text() / send_empty() route the
    // response back onto this stream (no TCP pcb here - the sink owns the QUIC framing).
    TcpConn *c = &conn_pool[slot];
    c->h3 = 1;
    c->pc_h3_conn_id = conn_id;
    c->pc_h3_stream = stream_id;
    c->pc_resp_sink = pc_h3_resp_sink;
    c->iface = PC_IF_WIFI_STA;
    Tcp.conn->set_state(slot, CONN_ACTIVE); // reserved slot: no bitmask bit (slot >= MAX_CONNS)
    c->pcb = NULL;

    Http.match_and_execute(slot); // -> handler -> send_text() -> pc_resp_sink -> pc_quic_server_respond()

    // Release the dispatch slot for the next request (a no-response handler simply leaves the stream open).
    c->h3 = 0;
    c->pc_resp_sink = NULL;
    Tcp.conn->set_state(slot, CONN_FREE); // reserved slot: no bitmask bit (slot >= MAX_CONNS)
    http_reset(slot);
}

#endif // PC_ENABLE_HTTP3
