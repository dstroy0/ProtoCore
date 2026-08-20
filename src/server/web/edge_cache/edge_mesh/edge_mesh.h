// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_mesh.h
 * @brief CDN edge-cache tier - mesh (sibling-cache) wire codec + async peer-query engine
 *        (PROTOCORE_ENABLE_EDGE_MESH).
 *
 * Lets a fleet of edge nodes share one warm cache. On a full local miss a node queries its sibling peers
 * (over a plaintext ProtoConn::PROTO_MESH TCP link) with a content-addressed request and pulls a fresh copy
 * from whichever peer has it, instead of re-fetching the origin. Pull (read-through) only: no push, no
 * invalidation. The transfer carries the object plus its freshness/age, so a sibling-fresh object serves for
 * its remaining lifetime with zero origin contact (RFC 9111 age propagation).
 *
 * This file is the pure, host-testable half: the request/response frame codec, the freshness-carrying entry
 * frame (the shared ::edge_sd_serialize body plus a fixed timing trailer), and the async requester engine
 * over the same EdgeFetchTransport seam the origin fetch uses (protocore_client on device, a mock in host tests).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_EDGE_MESH

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define PROTOCORE_EDGE_MESH_MAGIC0 ('E')

#define PROTOCORE_EDGE_MESH_MAGIC1 ('M')

#define PROTOCORE_EDGE_MESH_VERSION 1

#define PROTOCORE_EDGE_MESH_OP_GET 1 ///< the only request opcode: fetch by content address

#define PROTOCORE_EDGE_MESH_REQ_MAX (2 + 1 + 1 + 32 + 2 + PROTOCORE_EDGE_KEY_MAX + 2 + PROTOCORE_MESH_HDRS_MAX)

/** @brief Tri-state parse result for the length-delimited frames (partial reads accumulate to complete). */
typedef enum PROTO_ENUM_PACKED
{
    EDGE_MESH_PARSE_MALFORMED = -1, ///< bad magic/version/opcode, or a field that cannot fit the destination
    EDGE_MESH_PARSE_INCOMPLETE = 0, ///< a valid prefix so far - need more bytes
    EDGE_MESH_PARSE_MISS = 1,       ///< a complete response with no object
    EDGE_MESH_PARSE_HIT = 2,        ///< a complete request (outputs filled) / a complete response carrying an entry
} EdgeMeshParse;

/** @brief Peer-query progress. */
typedef enum PROTO_ENUM_PACKED
{
    EDGE_MESH_STATUS_PENDING, ///< still connecting / receiving
    EDGE_MESH_STATUS_HIT,     ///< a complete entry frame arrived (entry_off / entry_len valid)
    EDGE_MESH_STATUS_MISS,    ///< the peer does not have (a fresh copy of) the object
    EDGE_MESH_STATUS_FAILED,  ///< connect / send / timeout / closed-before-complete / malformed
} EdgeMeshStatus;

/**
 * @brief One in-flight peer query (zero-heap). The response accumulates into a caller-owned @c buf (>=
 *        PROTOCORE_EDGE_MESH_RESP_MAX) supplied at begin - a fetch slot reuses its origin buffer, since the mesh and
 *        origin phases never run at once.
 */
typedef struct
{
    EdgeMeshStatus st;
    int cid;
    uint32_t start_ms;
    size_t got;       ///< response bytes accumulated
    size_t entry_off; ///< offset of the entry frame within buf (valid on HIT)
    size_t entry_len; ///< length of the entry frame (valid on HIT)
    uint8_t *buf;     ///< caller-owned accumulation buffer
    size_t cap;       ///< its capacity (must be >= PROTOCORE_EDGE_MESH_RESP_MAX)
} EdgeMeshFetch;
/** @brief EdgeEntry, as the caller already knows it. */
struct EdgeEntry;
/** @brief EdgeFetchTransport, as the caller already knows it. */
struct EdgeFetchTransport;
/** @brief What build_request takes: digest, canon, req_hdrs, out, cap. */
typedef struct
{
    const uint8_t *digest; ///< 32 bytes.
    const char *canon;
    const char
        *req_hdrs; ///< the requester's header snapshot (name RS value US ...) so the peer can match Vary variants; ...
    uint8_t *out;
    size_t cap;
} EdgeMeshBuildRequestArgs;
/** @brief What parse_request takes: buf, len, digest_out, canon_out, ... */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    uint8_t *digest_out; ///< 32 bytes.
    char *canon_out;
    size_t canon_cap;
    char *hdrs_out;
    size_t hdrs_cap;
} EdgeMeshParseRequestArgs;
/** @brief What serialize_entry takes: e, current_age, out, cap. */
typedef struct
{
    const struct EdgeEntry *e;
    long current_age; ///< the sender's corrected age for e at send time (RFC 9111 sec 4.2.3), clamped >= 0
    uint8_t *out;
    size_t cap;
} EdgeMeshSerializeEntryArgs;
/** @brief What deserialize_entry takes: entry_buf, buf, len, e, now_ms. */
typedef struct
{
    uint8_t *entry_buf;
    const uint8_t *buf;
    size_t len;
    struct EdgeEntry *e;
    uint32_t now_ms;
} EdgeMeshDeserializeEntryArgs;
/** @brief What build_response takes: hit, entry, entry_len, out, cap. */
typedef struct
{
    proto_bool hit;
    const uint8_t *entry;
    size_t entry_len;
    uint8_t *out;
    size_t cap;
} EdgeMeshBuildResponseArgs;
/** @brief What parse_response takes: buf, len, entry_off, entry_len. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    size_t *entry_off;
    size_t *entry_len;
} EdgeMeshParseResponseArgs;
/** @brief What fetch_begin takes: m, t, host, port, request, req_len, ... */
typedef struct
{
    EdgeMeshFetch *m;
    const struct EdgeFetchTransport *t;
    const char *host;
    uint16_t port;
    const uint8_t *request;
    size_t req_len;
    uint8_t *buf;
    size_t cap;
    uint32_t now_ms;
} EdgeMeshFetchBeginArgs;
/** @brief What fetch_pump takes: m, t, now_ms. */
typedef struct
{
    EdgeMeshFetch *m;
    const struct EdgeFetchTransport *t;
    uint32_t now_ms;
} EdgeMeshFetchPumpArgs;
/** @brief What fetch_end takes: m, t. */
typedef struct
{
    EdgeMeshFetch *m;
    const struct EdgeFetchTransport *t;
} EdgeMeshFetchEndArgs;
/**
 * @brief CDN edge-cache tier - mesh (sibling-cache) wire codec + async peer-query engine (PROTOCORE_ENABLE_EDGE_MESH).
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::EdgeMesh with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   EdgeMesh.build_request_args.digest = ...;
 *   EdgeMesh.build_request_args.canon = ...;
 *   EdgeMesh.build_request_args.req_hdrs = ...;
 *   EdgeMesh.build_request_args.out = ...;
 *   EdgeMesh.build_request_args.cap = ...;
 *   EdgeMesh.build_request(work);
 *   // EdgeMesh.n is what the call reports
 *
 * @var EdgeMeshNs::build_request_args  what build_request takes: digest, canon, req_hdrs, out, cap
 * @var EdgeMeshNs::parse_request_args  what parse_request takes: buf, len, digest_out, canon_out,
 * @var EdgeMeshNs::serialize_entry_args  what serialize_entry takes: e, current_age, out, cap
 * @var EdgeMeshNs::deserialize_entry_args  what deserialize_entry takes: entry_buf, buf, len, e, now_ms
 * @var EdgeMeshNs::build_response_args  what build_response takes: hit, entry, entry_len, out, cap
 * @var EdgeMeshNs::parse_response_args  what parse_response takes: buf, len, entry_off, entry_len
 * @var EdgeMeshNs::fetch_begin_args  what fetch_begin takes: m, t, host, port, request, req_len,
 * @var EdgeMeshNs::fetch_pump_args  what fetch_pump takes: m, t, now_ms
 * @var EdgeMeshNs::fetch_end_args  what fetch_end takes: m, t
 * @var EdgeMeshNs::ok  a call's true/false outcome
 * @var EdgeMeshNs::n  the entry-frame length, or 0 if it would not fit cap
 * @var EdgeMeshNs::parse  EDGE_MESH_PARSE_HIT with digest_out / canon_out / hdrs_out filled ...
 * @var EdgeMeshNs::status  what a call reports
 * @var EdgeMeshNs::build_request  build a GET request for digest / canon into out
 * @var EdgeMeshNs::parse_request  parse an accumulated request buffer
 * @var EdgeMeshNs::serialize_entry  serialize e (content via the shared ::edge_sd_serialize) plus a ...
 * @var EdgeMeshNs::deserialize_entry  rehydrate e from an entry frame: content via ::edge_sd_deserialize, ...
 * @var EdgeMeshNs::build_response  build a response (hit -> carry entry / entry_len; else a MISS). ...
 * @var EdgeMeshNs::parse_response  parse an accumulated response buffer
 * @var EdgeMeshNs::fetch_begin  dial host:port, send request, begin receiving into buf (cap >= ...
 * @var EdgeMeshNs::fetch_pump  drain available bytes and advance; honors PROTOCORE_MESH_QUERY_MS. ...
 * @var EdgeMeshNs::fetch_end  release the peer connection (idempotent)
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    EdgeMeshBuildRequestArgs build_request_args;
    EdgeMeshParseRequestArgs parse_request_args;
    EdgeMeshSerializeEntryArgs serialize_entry_args;
    EdgeMeshDeserializeEntryArgs deserialize_entry_args;
    EdgeMeshBuildResponseArgs build_response_args;
    EdgeMeshParseResponseArgs parse_response_args;
    EdgeMeshFetchBeginArgs fetch_begin_args;
    EdgeMeshFetchPumpArgs fetch_pump_args;
    EdgeMeshFetchEndArgs fetch_end_args;
    proto_bool ok;
    size_t n;
    EdgeMeshParse parse;
    EdgeMeshStatus status;
} EdgeMeshVars;

/** @brief The operands and the outcome. */
extern EdgeMeshVars EdgeMeshV;

/** @brief The entries. */
typedef struct
{
    void (*const build_request)(uint8_t *restrict work);
    void (*const parse_request)(uint8_t *restrict work);
    void (*const serialize_entry)(uint8_t *restrict work);
    void (*const deserialize_entry)(uint8_t *restrict work);
    void (*const build_response)(uint8_t *restrict work);
    void (*const parse_response)(uint8_t *restrict work);
    void (*const fetch_begin)(uint8_t *restrict work);
    void (*const fetch_pump)(uint8_t *restrict work);
    void (*const fetch_end)(uint8_t *restrict work);
} EdgeMeshNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in EdgeMeshV or a region of the borrow at a fixed offset.
void protocore_edge_mesh_build_request(uint8_t *restrict work);
void protocore_edge_mesh_parse_request(uint8_t *restrict work);
void protocore_edge_mesh_serialize_entry(uint8_t *restrict work);
void protocore_edge_mesh_deserialize_entry(uint8_t *restrict work);
void protocore_edge_mesh_build_response(uint8_t *restrict work);
void protocore_edge_mesh_parse_response(uint8_t *restrict work);
void protocore_edge_mesh_fetch_begin(uint8_t *restrict work);
void protocore_edge_mesh_fetch_pump(uint8_t *restrict work);
void protocore_edge_mesh_fetch_end(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `EdgeMesh.build_request(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const EdgeMeshNs EdgeMesh __attribute__((unused)) = {
    .build_request = protocore_edge_mesh_build_request,
    .parse_request = protocore_edge_mesh_parse_request,
    .serialize_entry = protocore_edge_mesh_serialize_entry,
    .deserialize_entry = protocore_edge_mesh_deserialize_entry,
    .build_response = protocore_edge_mesh_build_response,
    .parse_response = protocore_edge_mesh_parse_response,
    .fetch_begin = protocore_edge_mesh_fetch_begin,
    .fetch_pump = protocore_edge_mesh_fetch_pump,
    .fetch_end = protocore_edge_mesh_fetch_end,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_MESH

#endif // PROTOCORE_EDGE_MESH_H
