// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_fetch.h
 * @brief CDN edge-cache tier - async origin-fetch engine (PC_ENABLE_EDGE_CACHE).
 *
 * A non-blocking origin fetch: open + send a request over a transport seam, accumulate the response
 * across poll loops into a bounded buffer, detect completion (Content-Length / chunked / connection
 * close), then parse it with the proven http_client codec. Pumped from the server poll loop so a miss
 * or revalidation never stalls the worker; the transport seam is pc_client on the device and a mock in
 * host tests. Zero heap; the buffer is fixed (`PC_EDGE_FETCH_BUF`).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_EDGE_FETCH_H
#define PROTOCORE_EDGE_FETCH_H

#include "protocore_config.h"

#if PC_ENABLE_EDGE_CACHE

/** @brief The origin transport, bound to pc_client on the device and a mock in host tests. */
typedef struct
{
    int (*open)(void *ctx, const char *host, uint16_t port, uint32_t timeout_ms); ///< cid >= 0, or < 0 on failure
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
    uint32_t got; ///< bytes accumulated
    int status;   ///< HTTP status (valid when DONE)
    size_t head_len;
    size_t body_off;
    size_t body_len;
    uint8_t buf[PC_EDGE_FETCH_BUF];
} EdgeFetch;

/** @brief Open + send @p request; begin receiving. Sets @p st to PENDING, or FAILED on open/send error. */
void edge_fetch_begin(EdgeFetch *f, const EdgeFetchTransport *t, const char *host, uint16_t port, const void *request,
                      size_t req_len, uint32_t now_ms);

/**
 * @brief Drain available bytes and advance. On DONE the response is parsed (chunked bodies decoded in
 *        place); honors `PC_EDGE_FETCH_TIMEOUT_MS`. @return the current status.
 */
EdgeFetchStatus edge_fetch_pump(EdgeFetch *f, const EdgeFetchTransport *t, uint32_t now_ms);

/** @brief Release the transport connection (idempotent). */
void edge_fetch_end(EdgeFetch *f, const EdgeFetchTransport *t);

/**
 * @brief Is the accumulated response complete? (headers terminated + body per Content-Length / chunked
 *        terminator / connection close). Sets @p head_len to the header-block length (0 if not yet whole).
 *        Pure - host-testable without a transport.
 */
proto_bool edge_resp_complete(const uint8_t *buf, size_t len, proto_bool conn_closed, size_t *head_len);

#endif // PC_ENABLE_EDGE_CACHE

#endif // PROTOCORE_EDGE_FETCH_H
