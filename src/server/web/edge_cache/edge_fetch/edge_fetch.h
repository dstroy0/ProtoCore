// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_fetch.h
 * @brief CDN edge-cache tier - async origin-fetch engine (PROTOCORE_ENABLE_EDGE_CACHE).
 *
 * A non-blocking origin fetch: open + send a request over a transport seam, accumulate the response
 * across poll loops into a bounded buffer, detect completion (Content-Length / chunked / connection
 * close), then parse it with the proven http_client codec. Pumped from the server poll loop so a miss
 * or revalidation never stalls the worker; the transport seam is protocore_client on the device and a mock in
 * host tests. Zero heap; the buffer is fixed (`PROTOCORE_EDGE_FETCH_BUF`).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_EDGE_FETCH_H
#define PROTOCORE_EDGE_FETCH_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_EDGE_CACHE

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief The origin transport, bound to protocore_client on the device and a mock in host tests. */
typedef struct EdgeFetchTransport
{
    int (*open)(void *ctx, const char *host, uint16_t port, uint32_t timeout_ms); ///< cid >= 0, or < 0 on failure
    proto_bool (*connected)(void *ctx, int cid); ///< step the open along; true once it is up
    proto_bool (*send)(void *ctx, int cid, const void *data, size_t len);
    size_t (*read)(void *ctx, int cid, uint8_t *buf, size_t cap); ///< 0 = nothing available right now
    proto_bool (*closed)(void *ctx, int cid);                     ///< true once the origin closed its side
    void (*close)(void *ctx, int cid);
    void *ctx;
} EdgeFetchTransport;

/** @brief Fetch progress. */
typedef enum PROTO_ENUM_PACKED
{
    EDGE_FETCH_STATUS_PENDING,  ///< still receiving
    EDGE_FETCH_STATUS_DONE,     ///< a complete response is parsed (status / body_off / body_len valid)
    EDGE_FETCH_STATUS_OVERSIZE, ///< response exceeded the buffer - not cacheable (caller passes through / fails open)
    EDGE_FETCH_STATUS_FAILED,   ///< connect / send / timeout / closed-before-complete
} EdgeFetchStatus;

/** @brief One in-flight origin fetch (fixed-size, zero-heap). */
typedef struct
{
    EdgeFetchStatus st;
    int cid;
    uint32_t start_ms;
    uint32_t got;     ///< bytes accumulated
    uint32_t req_len; ///< request bytes parked at the head of buf until the connection is up
    proto_bool sent;  ///< the request reached the transport, so buf now takes the response
    int status;       ///< HTTP status (valid when DONE)
    size_t head_len;
    size_t body_off;
    size_t body_len;
    uint8_t buf[PROTOCORE_EDGE_FETCH_BUF];
} EdgeFetch;

/** @brief What begin takes: f, t, host, port, request, req_len, now_ms. */
typedef struct
{
    EdgeFetch *f;
    const EdgeFetchTransport *t;
    const char *host;
    uint16_t port;
    const void *request;
    size_t req_len;
    uint32_t now_ms;
} EdgeFetchBeginArgs;

/** @brief What pump takes: f, t, now_ms. */
typedef struct
{
    EdgeFetch *f;
    const EdgeFetchTransport *t;
    uint32_t now_ms;
} EdgeFetchPumpArgs;

/** @brief What end takes: f, t. */
typedef struct
{
    EdgeFetch *f;
    const EdgeFetchTransport *t;
} EdgeFetchEndArgs;

/** @brief What edge_resp_complete takes: buf, len, conn_closed, ... */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    proto_bool conn_closed;
    size_t *head_len;
} EdgeFetchEdgeRespCompleteArgs;

/**
 * @brief CDN edge-cache tier - async origin-fetch engine (PROTOCORE_ENABLE_EDGE_CACHE). A non-blocking origin fetch:
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::EdgeFetcher with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   EdgeFetcher.begin_args.f = ...;
 *   EdgeFetcher.begin_args.t = ...;
 *   EdgeFetcher.begin_args.host = ...;
 *   EdgeFetcher.begin_args.port = ...;
 *   EdgeFetcher.begin_args.request = ...;
 *   EdgeFetcher.begin_args.req_len = ...;
 *   EdgeFetcher.begin_args.now_ms = ...;
 *   EdgeFetcher.begin(work);
 *
 * @var EdgeFetchNs::begin_args  what begin takes: f, t, host, port, request, req_len, now_ms
 * @var EdgeFetchNs::pump_args  what pump takes: f, t, now_ms
 * @var EdgeFetchNs::end_args  what end takes: f, t
 * @var EdgeFetchNs::edge_resp_complete_args  what edge_resp_complete takes: buf, len, conn_closed,
 * @var EdgeFetchNs::ok  a call's true/false outcome
 * @var EdgeFetchNs::status  what a call reports
 * @var EdgeFetchNs::begin  open the origin connection and park request; begin the fetch. Sets ...
 * @var EdgeFetchNs::pump  drain available bytes and advance. On DONE the response is parsed ...
 * @var EdgeFetchNs::end  release the transport connection (idempotent)
 * @var EdgeFetchNs::edge_resp_complete  is the accumulated response complete? (headers terminated + body ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    EdgeFetchBeginArgs begin_args;
    EdgeFetchPumpArgs pump_args;
    EdgeFetchEndArgs end_args;
    EdgeFetchEdgeRespCompleteArgs edge_resp_complete_args;
    proto_bool ok;
    EdgeFetchStatus status;
} EdgeFetcherVars;

/** @brief The operands and the outcome. */
extern EdgeFetcherVars EdgeFetcherV;

/** @brief The entries. */
typedef struct
{
    void (*const begin)(uint8_t *restrict work);
    void (*const pump)(uint8_t *restrict work);
    void (*const end)(uint8_t *restrict work);
    void (*const edge_resp_complete)(uint8_t *restrict work);
} EdgeFetchNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in EdgeFetcherV or a region of the borrow at a fixed offset.
void protocore_edge_fetcher_begin(uint8_t *restrict work);
void protocore_edge_fetcher_pump(uint8_t *restrict work);
void protocore_edge_fetcher_end(uint8_t *restrict work);
void protocore_edge_fetcher_edge_resp_complete(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `EdgeFetcher.begin(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const EdgeFetchNs EdgeFetcher __attribute__((unused)) = {
    .begin = protocore_edge_fetcher_begin,
    .pump = protocore_edge_fetcher_pump,
    .end = protocore_edge_fetcher_end,
    .edge_resp_complete = protocore_edge_fetcher_edge_resp_complete,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_CACHE

#endif // PROTOCORE_EDGE_FETCH_H
