// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coap.h
 * @brief Zero-heap CoAP server (RFC 7252): message codec + a fixed resource table.
 *
 * The server is split into a pure, host-testable core and an ESP32-only UDP
 * transport (mirroring the SNMP agent's split):
 *
 *  - pc_coap_server_process() takes a complete request datagram and produces a
 *    complete response datagram in a caller buffer - no sockets, no heap. It is
 *    unit-tested on the host (env:native_coap).
 *  - pc_coap_server_begin() binds the transport-layer UDP service on :5683
 *    (Arduino only) and feeds received datagrams through pc_coap_server_process().
 *
 * The message layer uses the piggybacked-response model: a CON request is answered
 * with a piggybacked ACK, a NON request with a NON response. Message de-duplication
 * (RFC 7252 sec 4.5) is implemented - a retransmitted CON is re-answered from a small
 * cache keyed on (source endpoint, Message-ID) WITHOUT re-running its handler, so a
 * client's retransmission cannot execute a non-idempotent request twice (see
 * PC_COAP_DEDUP_*). Separate (deferred) responses are a deliberate non-goal (this is
 * a synchronous, in-line server: a request is answered before the handler returns),
 * and there is no CON retransmission because the server never sends a Confirmable
 * message - notifications go out Non-confirmable. The /.well-known/core resource-
 * discovery listing (RFC 6690) is served. The codec understands the Uri-Path,
 * Uri-Query and Content-Format options; other options are skipped. Block-wise transfer
 * (RFC 7959, the Block1/Block2 options) is available under PC_ENABLE_COAP_BLOCK;
 * resource observation (RFC 7641) under PC_ENABLE_COAP_OBSERVE.
 *
 * The resource table is a fixed BSS array of PC_COAP_MAX_RESOURCES entries.
 * Register handlers with pc_coap_server_add_resource(); the path string is
 * referenced by pointer and must outlive the server (point it at flash/static
 * data, like the rest of the library's strings).
 */

#ifndef PROTOCORE_COAP_H
#define PROTOCORE_COAP_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_COAP

// CoAP message types (RFC 7252 §3, the 2-bit T field).
typedef enum PROTO_ENUM_PACKED
{
    COAP_TYPE_CON = 0, ///< Confirmable (answered with a piggybacked ACK).
    COAP_TYPE_NON = 1, ///< Non-confirmable (answered with a NON response).
    COAP_TYPE_ACK = 2, ///< Acknowledgement.
    COAP_TYPE_RST = 3, ///< Reset (rejects a message; sent for a malformed/empty CON).
} CoapType;

// CoAP request method codes (class 0; the on-wire Code byte's detail field).
typedef enum PROTO_ENUM_PACKED
{
    COAP_GET = 1,
    COAP_POST = 2,
    COAP_PUT = 3,
    COAP_DELETE = 4,
} CoapMethod;

// Allowed-methods bitmask for pc_coap_server_add_resource() (bit per method). A mask is OR'd
// together, so these stay plain integer constants rather than an enum, which would force a cast at
// every |. The bit position is the CoapMethod ordinal. Parenthesized because a shift binds looser
// than most operators a caller may combine it with.
#define COAP_ALLOW_GET (1u << (unsigned)COAP_GET)       ///< 0x02
#define COAP_ALLOW_POST (1u << (unsigned)COAP_POST)     ///< 0x04
#define COAP_ALLOW_PUT (1u << (unsigned)COAP_PUT)       ///< 0x08
#define COAP_ALLOW_DELETE (1u << (unsigned)COAP_DELETE) ///< 0x10

/** @brief Build a CoAP response Code byte from its class and detail (e.g. COAP_CODE(2,5) = 2.05). */
#define COAP_CODE(c, dd) ((uint8_t)(((c) << 5) | ((dd) & 0x1F)))

// Common CoAP response codes (RFC 7252 §5.9; 2.31 / 4.08 / 4.13 from RFC 7959).
typedef enum PROTO_ENUM_PACKED
{
    COAP_RSP_CREATED = COAP_CODE(2, 1),            ///< 2.01
    COAP_RSP_DELETED = COAP_CODE(2, 2),            ///< 2.02
    COAP_RSP_VALID = COAP_CODE(2, 3),              ///< 2.03
    COAP_RSP_CHANGED = COAP_CODE(2, 4),            ///< 2.04
    COAP_RSP_CONTENT = COAP_CODE(2, 5),            ///< 2.05
    COAP_RSP_CONTINUE = COAP_CODE(2, 31),          ///< 2.31 (block-wise: more Block1 blocks expected)
    COAP_RSP_BAD_REQUEST = COAP_CODE(4, 0),        ///< 4.00
    COAP_RSP_BAD_OPTION = COAP_CODE(4, 2),         ///< 4.02
    COAP_RSP_NOT_FOUND = COAP_CODE(4, 4),          ///< 4.04
    COAP_RSP_METHOD_NOT_ALLOWED = COAP_CODE(4, 5), ///< 4.05
    COAP_RSP_NOT_ACCEPTABLE = COAP_CODE(4, 6),     ///< 4.06
    COAP_RSP_REQUEST_INCOMPLETE = COAP_CODE(4, 8), ///< 4.08 (block-wise: out-of-order / lost Block1)
    COAP_RSP_REQUEST_TOO_LARGE = COAP_CODE(4, 13), ///< 4.13 (block-wise: reassembly buffer exceeded)
    COAP_RSP_INTERNAL_ERROR = COAP_CODE(5, 0),     ///< 5.00
    COAP_RSP_NOT_IMPLEMENTED = COAP_CODE(5, 1),    ///< 5.01
} CoapResponseCode;

// CoAP Content-Format identifiers (RFC 7252 §12.3). COAP_CF_NONE means "absent".
typedef enum PROTO_ENUM_PACKED
{
    COAP_CF_TEXT = 0,      ///< text/plain;charset=utf-8
    COAP_CF_LINK = 40,     ///< application/link-format
    COAP_CF_XML = 41,      ///< application/xml
    COAP_CF_OCTET = 42,    ///< application/octet-stream
    COAP_CF_JSON = 50,     ///< application/json
    COAP_CF_CBOR = 60,     ///< application/cbor
    COAP_CF_NONE = 0xFFFF, ///< no Content-Format option present / emitted
} CoapContentFormat;

/**
 * @brief A decoded CoAP request handed to a resource handler.
 *
 * All pointers reference transport- or library-owned scratch valid only for the
 * duration of the handler call; copy out anything you need to keep.
 */
typedef struct
{
    CoapMethod method;                ///< COAP_GET / COAP_POST / COAP_PUT / COAP_DELETE.
    const char *path;                 ///< reconstructed Uri-Path, e.g. "/temp" (always begins with '/').
    const char *query;                ///< reconstructed Uri-Query (segments joined by '&'), or "" if none.
    const uint8_t *payload;           ///< request payload bytes (may be nullptr if payload_len == 0).
    size_t payload_len;               ///< request payload length in bytes.
    CoapContentFormat content_format; ///< request Content-Format, or COAP_CF_NONE if absent.
} CoapRequest;

/**
 * @brief A response a resource handler fills in.
 *
 * @p code defaults to 2.05 Content; set it to another CoapResponseCode as
 * appropriate. Write the response body into @p payload (capacity @p payload_cap)
 * and set @p payload_len. Set @p content_format to describe the body, or leave it
 * COAP_CF_NONE for an empty/typeless response.
 */
typedef struct
{
    uint8_t code;                     ///< response Code byte (see CoapResponseCode); defaults to COAP_RSP_CONTENT.
    CoapContentFormat content_format; ///< COAP_CF_* describing the body, or COAP_CF_NONE.
    uint8_t *payload;                 ///< caller-provided buffer to write the response body into.
    size_t payload_cap;               ///< capacity of @p payload in bytes.
    size_t payload_len;               ///< bytes written by the handler (0 = empty body).
} CoapResponse;

/** @brief Resource handler: read @p req, fill @p resp. */
typedef void (*CoapHandler)(const CoapRequest *req, CoapResponse *resp);

// ---------------------------------------------------------------------------
// Server configuration / resource registration
// ---------------------------------------------------------------------------

/** @brief Reset the server and clear the resource table. Call before registering resources. */
void pc_coap_server_reset();

/**
 * @brief Register a resource at @p path served by @p handler.
 *
 * @param path     resource path beginning with '/' (referenced by pointer, not copied).
 * @param methods  allowed-methods bitmask (e.g. CoapMethodMask::COAP_ALLOW_GET | CoapMethodMask::COAP_ALLOW_PUT); a
 * request using a method not in the mask is answered 4.05 Method Not Allowed.
 * @param handler  invoked for an allowed method on a matching path.
 * @return false if the table is full.
 */
proto_bool pc_coap_server_add_resource(const char *path, uint8_t methods, CoapHandler handler);

// ---------------------------------------------------------------------------
// Core processing (host-testable; no sockets, no heap)
// ---------------------------------------------------------------------------

/**
 * @brief Process one CoAP request datagram and build the response datagram.
 *
 * Parses the message, reconstructs the Uri-Path/Uri-Query, dispatches against the
 * resource table, and encodes a piggybacked response (ACK for CON, NON for NON).
 * A malformed or unsupported-version CON is answered with an RST; a malformed NON
 * (or any ACK/RST received) yields no response.
 *
 * @param req      request datagram bytes.
 * @param req_len  number of bytes in @p req.
 * @param resp     destination buffer for the response datagram.
 * @param pc_resp_cap capacity of @p resp.
 * @return number of response bytes written, or 0 to send nothing.
 */
size_t pc_coap_server_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap);

/**
 * @brief Like pc_coap_server_process(), but include an Observe option (RFC 7641) in
 *        a successful (2.xx) response carrying the notification sequence
 *        @p observe_seq (a value < 0 omits it). Used by the Observe transport.
 */
size_t pc_coap_server_process_ex(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap,
                                 int32_t observe_seq);

#if PC_COAP_DEDUP_ENTRIES > 0
/**
 * @brief Message de-duplication lookup (RFC 7252 sec 4.5). If a Confirmable request from @p src_ip :
 *        @p src_port with Message-ID @p mid was answered within PC_COAP_DEDUP_LIFETIME_MS, report its
 *        cached response so the transport can resend it without re-running the handler.
 * @return true and (on non-null out params) the cached response bytes + length; false on a miss.
 */
proto_bool pc_coap_dedup_lookup(const char *src_ip, uint16_t src_port, uint16_t mid, const uint8_t **out,
                                size_t *out_len);

/**
 * @brief Record the response sent for a Confirmable (@p src_ip : @p src_port, @p mid) exchange so a later
 *        retransmission is deduplicated. A response longer than PC_COAP_DEDUP_RESP_MAX is not cached.
 */
void pc_coap_dedup_store(const char *src_ip, uint16_t src_port, uint16_t mid, const uint8_t *resp, size_t len);
#endif

// ---------------------------------------------------------------------------
// UDP transport (binds via the transport-layer UDP service; no-op on host)
// ---------------------------------------------------------------------------

/**
 * @brief Bind the server to UDP @p port (5683 is the RFC 7252 default) via the transport-layer
 *        UDP service.
 *
 * Callback-driven (no per-loop servicing). Call after WiFi is up. On non-Arduino
 * builds pc_udp_listen() is a stub, so the core remains host-testable.
 */
void pc_coap_server_begin(uint16_t port);

#if PC_ENABLE_COAP_OBSERVE
/**
 * @brief Push a notification to every observer of @p path (RFC 7641).
 *
 * Re-renders the resource (invokes its GET handler) and sends the current
 * representation as a CoAP notification - from the bound server port, carrying
 * each observer's token and an increasing Observe sequence. Call this whenever the
 * resource's state changes. A send failure drops that observer. No-op on a host
 * build. A client registers by sending a GET with the Observe option (0); it
 * deregisters with Observe (1), a Reset, or by going away.
 */
void pc_coap_notify(const char *path);
#endif

#endif // PC_ENABLE_COAP

PROTO_END_DECLS

#endif // PROTOCORE_COAP_H
