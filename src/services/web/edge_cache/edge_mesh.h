// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_mesh.h
 * @brief CDN edge-cache tier - mesh (sibling-cache) wire codec + async peer-query engine
 *        (PC_ENABLE_EDGE_MESH).
 *
 * Lets a fleet of edge nodes share one warm cache. On a full local miss a node queries its sibling peers
 * (over a plaintext ConnProto::PROTO_MESH TCP link) with a content-addressed request and pulls a fresh copy
 * from whichever peer has it, instead of re-fetching the origin. Pull (read-through) only: no push, no
 * invalidation. The transfer carries the object plus its freshness/age, so a sibling-fresh object serves for
 * its remaining lifetime with zero origin contact (RFC 9111 age propagation).
 *
 * This file is the pure, host-testable half: the request/response frame codec, the freshness-carrying entry
 * frame (the shared ::edge_sd_serialize body plus a fixed timing trailer), and the async requester engine
 * over the same EdgeFetchTransport seam the origin fetch uses (pc_client on device, a mock in host tests).
 * The server glue (the peer table, the pre-origin query phase, and the PROTO_MESH serving listener) lives in
 * edge_cache_proxy. Zero heap; fixed buffers.
 *
 * Wire format (little-endian, versioned; magic 'E','M'):
 *   request : 'E' 'M' | ver=1 | op=GET(1) | digest[32] | u16 key_len + key | u16 hdrs_len + req_hdrs
 *   response: 'E' 'M' | ver=1 | status (MISS=0 / HIT=1) | [ u16 entry_len + entry_frame ]   (entry on HIT)
 *   entry   : timing trailer (i64 date | i64 expires | u32 lifetime_s | u32 age_hdr | u32 current_age)
 *             followed by an ::edge_sd_serialize body (content: key/status/ct/validators/encoding/Vary/body)
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_EDGE_MESH_H
#define PROTOCORE_EDGE_MESH_H

#include "protocore_config.h"

#if PC_ENABLE_EDGE_MESH

#include "services/web/edge_cache/edge_cache.h"    // EdgeEntry
#include "services/web/edge_cache/edge_cache_sd.h" // PC_EDGE_SD_VALUE_MAX + the shared entry serializer
#include "services/web/edge_cache/edge_fetch.h"    // EdgeFetchTransport (reused transport seam)
#include <stddef.h>
#include <stdint.h>

#define PC_EDGE_MESH_MAGIC0 ('E')
#define PC_EDGE_MESH_MAGIC1 ('M')
#define PC_EDGE_MESH_VERSION 1
#define PC_EDGE_MESH_OP_GET 1 ///< the only request opcode: fetch by content address

/** @brief The fixed timing trailer prepended to an entry frame (age propagation). */
/** @brief Worst-case entry frame (trailer + a full ::edge_sd_serialize body). */
/** @brief Worst-case request frame (the third field is a bounded request-header snapshot for Vary matching). */
#define PC_EDGE_MESH_REQ_MAX (2 + 1 + 1 + 32 + 2 + PC_EDGE_KEY_MAX + 2 + PC_MESH_HDRS_MAX)
/** @brief Worst-case response frame (header + entry on a HIT). */

/** @brief Tri-state parse result for the length-delimited frames (partial reads accumulate to complete). */
enum class EdgeMeshParse : int8_t
{
    MALFORMED = -1, ///< bad magic/version/opcode, or a field that cannot fit the destination
    INCOMPLETE = 0, ///< a valid prefix so far - need more bytes
    MISS = 1,       ///< a complete response with no object
    HIT = 2,        ///< a complete request (outputs filled) / a complete response carrying an entry
};

// --- frame codec (pure) --------------------------------------------------------------------------

/**
 * @brief Build a GET request for @p digest / @p canon into @p out.
 * @param req_hdrs the requester's header snapshot (name RS value US ...) so the peer can match Vary variants;
 *        pass "" for none. @return the frame length, or 0 if it would not fit @p cap.
 */
size_t edge_mesh_build_request(const uint8_t digest[32], const char *canon, const char *req_hdrs, uint8_t *out,
                               size_t cap);

/**
 * @brief Parse an accumulated request buffer.
 * @return EdgeMeshParse::HIT with @p digest_out / @p canon_out / @p hdrs_out filled when a whole valid GET is
 *         present, INCOMPLETE if a valid prefix needs more bytes, or MALFORMED. (No MISS for a request.)
 */
EdgeMeshParse edge_mesh_parse_request(const uint8_t *buf, size_t len, uint8_t digest_out[32], char *canon_out,
                                      size_t canon_cap, char *hdrs_out, size_t hdrs_cap);

/**
 * @brief Serialize @p e (content via the shared ::edge_sd_serialize) plus a timing trailer into @p out.
 * @param current_age the sender's corrected age for @p e at send time (RFC 9111 sec 4.2.3), clamped >= 0.
 * @return the entry-frame length, or 0 if it would not fit @p cap.
 */
size_t edge_mesh_serialize_entry(const EdgeEntry *e, long current_age, uint8_t *out, size_t cap);

/**
 * @brief Rehydrate @p e from an entry frame: content via ::edge_sd_deserialize, then the freshness fields.
 *
 * @p now_ms becomes the entry's insert time and the sender's transferred age becomes its initial age, so the
 * receiver's ::edge_current_age keeps growing from where the sender left off (age propagation). The caller
 * owns @p e's `used`/LRU linkage (typically an ::edge_store_alloc slot) and must re-check freshness + that the
 * restored key matches the request (guards a wrong / colliding object). @return false on a short/corrupt frame.
 */
bool edge_mesh_deserialize_entry(const uint8_t *buf, size_t len, EdgeEntry *e, uint32_t now_ms);

/** @brief Build a response (@p hit -> carry @p entry / @p entry_len; else a MISS). @return length or 0. */
size_t edge_mesh_build_response(bool hit, const uint8_t *entry, size_t entry_len, uint8_t *out, size_t cap);

/**
 * @brief Parse an accumulated response buffer.
 * @return HIT (with @p entry_off / @p entry_len pointing into @p buf), MISS, INCOMPLETE, or MALFORMED.
 */
EdgeMeshParse edge_mesh_parse_response(const uint8_t *buf, size_t len, size_t *entry_off, size_t *entry_len);

// --- async requester engine (over the EdgeFetchTransport seam) ------------------------------------

/** @brief Peer-query progress. */
enum class EdgeMeshStatus : uint8_t
{
    PENDING, ///< still connecting / receiving
    HIT,     ///< a complete entry frame arrived (entry_off / entry_len valid)
    MISS,    ///< the peer does not have (a fresh copy of) the object
    FAILED,  ///< connect / send / timeout / closed-before-complete / malformed
};

/**
 * @brief One in-flight peer query (zero-heap). The response accumulates into a caller-owned @c buf (>=
 *        PC_EDGE_MESH_RESP_MAX) supplied at begin - a fetch slot reuses its origin buffer, since the mesh and
 *        origin phases never run at once.
 */
struct EdgeMeshFetch
{
    EdgeMeshStatus st;
    int cid;
    uint32_t start_ms;
    size_t got;       ///< response bytes accumulated
    size_t entry_off; ///< offset of the entry frame within buf (valid on HIT)
    size_t entry_len; ///< length of the entry frame (valid on HIT)
    uint8_t *buf;     ///< caller-owned accumulation buffer
    size_t cap;       ///< its capacity (must be >= PC_EDGE_MESH_RESP_MAX)
};

/**
 * @brief Dial @p host:@p port, send @p request, begin receiving into @p buf (@p cap >= PC_EDGE_MESH_RESP_MAX).
 *        Sets st to PENDING, or FAILED on error.
 */
void edge_mesh_fetch_begin(EdgeMeshFetch *m, const EdgeFetchTransport *t, const char *host, uint16_t port,
                           const uint8_t *request, size_t req_len, uint8_t *buf, size_t cap, uint32_t now_ms);

/** @brief Drain available bytes and advance; honors PC_MESH_QUERY_MS. @return the current status. */
EdgeMeshStatus edge_mesh_fetch_pump(EdgeMeshFetch *m, const EdgeFetchTransport *t, uint32_t now_ms);

/** @brief Release the peer connection (idempotent). */
void edge_mesh_fetch_end(EdgeMeshFetch *m, const EdgeFetchTransport *t);

#endif // PC_ENABLE_EDGE_MESH

#endif // PROTOCORE_EDGE_MESH_H
