// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file h3_server.c
 * @brief The HTTP/3 request bridge. See h3_server.h.
 */

#if PROTOCORE_ENABLE_HTTP3

#include "mmgr/protostr.h"                                   // str: the bounded-run walks
#include "mmgr/rawmemcpy.h"                                  // raw.read: each field moves into the slot
#include "network_drivers/presentation/http/http.h"          // Http.match_and_execute
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the reserved dispatch slot
#include "network_drivers/presentation/http/http3/h3_server.h"
#include "network_drivers/session/session.h" // the per-connection tables this reads

/**
 * @brief The bridge's state and the calls that reach it - what H3ServerNs points at.
 *
 * The reserved dispatch slot and the per-slot HTTP state are the layers' own; this holds only the
 * handle a call is served through.
 *
 * @var H3ServerInternal::ns  the handle a caller sets a call's members on
 */
struct H3ServerInternal
{
    H3ServerNs *ns;
};

static struct H3ServerInternal s_h3 = {.ns = &H3Server};

// Randomness for the QUIC ephemeral X25519 key, the ServerHello random, and our connection IDs:
// four bytes per platform draw, the last draw truncated to what is left.
static void rng(struct H3ServerInternal *restrict ctx)
{
    uint8_t *out = ctx->ns->rng_args.out;
    const size_t len = ctx->ns->rng_args.len;
    size_t i = 0;
    while (i < len)
    {
        uint32_t r = protocore_platform_rand_u32();
        size_t n = 4;
        if (len - i < n)
        {
            n = len - i;
        }
        raw.read(out + i, &r, n);
        i += n;
    }
}

// Response sink for the HTTP/3 dispatch slot: route (code, content_type, body) onto the QUIC stream
// the request arrived on (ids stashed on the slot by dispatch_h3_request). Installed as conn->protocore_resp_sink
// so send_text()/send_empty() stay protocol-agnostic.
static proto_bool protocore_h3_resp_sink(uint8_t slot, int code, const char *content_type, const char *body, size_t len)
{
    return protocore_quic_server_respond(http_h3_conn_id[slot], http_h3_stream[slot], code, content_type,
                                         (const uint8_t *)body, len);
}

static void request(struct H3ServerInternal *restrict ctx)
{
    const uint32_t conn_id = ctx->ns->stream.conn_id;
    const uint64_t stream_id = ctx->ns->stream.stream_id;
    const char *method = ctx->ns->req.method;
    const char *path = ctx->ns->req.path;
    const char *authority = ctx->ns->req.authority;
    const uint8_t *body = ctx->ns->req.body;
    const size_t body_len = ctx->ns->req.body_len;
    const uint8_t slot = PROTOCORE_H3_DISPATCH_SLOT;
    HttpReq *r = &http_pool[slot];
    http_parser_reset(&http_pool[slot]);

    // Map the semantic request fields into the shared HttpReq (as protocore_h2_server does per stream).
    size_t mn = str.len(method, sizeof(r->method));
    if (mn >= sizeof(r->method))
    {
        mn = sizeof(r->method) - 1;
    }
    raw.read(r->method, method, mn);
    r->method[mn] = 0;

    // Bounded by everything the request could occupy here, path and query together, rather than by
    // the path field alone: a '?' past the path cap still names a query this slot has room for, and
    // capping the search at the path would drop it while keeping the truncated path.
    const char *q = str.find(path, sizeof(r->path) + sizeof(r->query), "?", sizeof("?"), PROTO_FALSE);
    size_t plen = (q != NULL) ? (size_t)(q - path) : str.len(path, sizeof(r->path));
    if (plen >= sizeof(r->path))
    {
        plen = sizeof(r->path) - 1;
    }
    raw.read(r->path, path, plen);
    r->path[plen] = 0;
    r->path_idx = str.len(r->path, sizeof(r->path));
    if (q != NULL)
    {
        size_t ql = str.len(q + 1, sizeof(r->query));
        if (ql >= sizeof(r->query))
        {
            ql = sizeof(r->query) - 1;
        }
        raw.read(r->query, q + 1, ql);
        r->query[ql] = 0;
        r->query_idx = str.len(r->query, sizeof(r->query));
    }

    // :authority maps to Host, the way the h2 bridge does.
    if (authority && authority[0] && r->header_count < MAX_HEADERS)
    {
        Header *h = &r->headers[r->header_count];
        r->header_count++;
        raw.read(h->key, "host", 5);
        size_t vl = str.len(authority, sizeof(h->val));
        if (vl >= sizeof(h->val))
        {
            vl = sizeof(h->val) - 1;
        }
        raw.read(h->val, authority, vl);
        h->val[vl] = 0;
    }

    if (body && body_len)
    {
        size_t n = body_len > BODY_BUF_SIZE ? BODY_BUF_SIZE : body_len;
        raw.read(r->body, body, n);
        r->body_len = n;
        r->body[r->body_len] = 0;
        r->body_bytes_read = body_len;
        r->content_length = body_len;
    }
    r->parse_state = PARSE_COMPLETE;

    // Mark the reserved slot as HTTP/3 and install the response sink so send_text() / send_empty() route the
    // response back onto this stream (no TCP pcb here - the sink owns the QUIC framing).
    http_h3[slot] = 1;
    http_h3_conn_id[slot] = conn_id;
    http_h3_stream[slot] = stream_id;
    http_resp_sink[slot] = protocore_h3_resp_sink;
    ConnPool.slot = slot;
    ConnPool.st = CONN_ACTIVE;
    ConnPool.set_state(ConnPool.internal); // reserved slot: no bitmask bit (slot >= MAX_CONNS)

    Http.slot = slot;
    Http.match_and_execute(
        Http.internal); // -> handler -> send_text() -> protocore_resp_sink -> protocore_quic_server_respond()

    // Release the dispatch slot for the next request (a no-response handler simply leaves the stream open).
    http_h3[slot] = 0;
    http_resp_sink[slot] = NULL;
    ConnPool.slot = slot;
    ConnPool.st = CONN_FREE;
    ConnPool.set_state(ConnPool.internal); // reserved slot: no bitmask bit (slot >= MAX_CONNS)
    http_parser_reset(&http_pool[slot]);
}

// The QUIC server's seam dictates these two shapes, so they carry their arguments onto the handle.
void protocore_h3_server_rng(uint8_t *out, size_t len)
{
    H3Server.rng_args.out = out;
    H3Server.rng_args.len = len;
    rng(&s_h3);
}

void protocore_h3_server_request(void *app, uint32_t conn_id, uint64_t stream_id, const char *method, const char *path,
                                 const char *authority, const uint8_t *body, size_t body_len)
{
    (void)app; // the route table and the slot pools are global owners; nothing is carried here
    H3Server.stream.conn_id = conn_id;
    H3Server.stream.stream_id = stream_id;
    H3Server.req.method = method;
    H3Server.req.path = path;
    H3Server.req.authority = authority;
    H3Server.req.body = body;
    H3Server.req.body_len = body_len;
    request(&s_h3);
}

// Designated, so a member's position in the struct does not decide what it binds to.
H3ServerNs H3Server = {.request = request, .rng = rng, .internal = &s_h3};

#endif // PROTOCORE_ENABLE_HTTP3
