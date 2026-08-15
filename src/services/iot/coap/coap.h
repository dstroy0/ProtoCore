// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coap.h
 * @brief The CoAP server (RFC 7252): the message codec, a fixed resource table, and the UDP binding.
 *
 * RFC 7252 sec 3 gives the message: a 4-byte header carrying Version (Ver), Type (T), Token Length
 * (TKL), Code and Message ID, then the Token of TKL bytes, then zero or more Options in TLV form,
 * then the Payload behind the one-byte Payload Marker 0xFF.
 *
 * A request is answered piggybacked (RFC 7252 sec 5.2.1): a Confirmable request takes an
 * Acknowledgement carrying the response, a Non-confirmable request takes a Non-confirmable response
 * (sec 5.2.3). Separate responses (sec 5.2.2) are not produced - a request is answered before its
 * handler returns - and the server never sends a Confirmable message, so nothing is retransmitted.
 *
 * Message deduplication (RFC 7252 sec 4.5) is kept: a Confirmable message repeated within
 * @ref PROTOCORE_COAP_DEDUP_LIFETIME_MS is recognized by its Message ID and source endpoint and
 * re-answered from a cache, so the request is processed only once.
 *
 * Resource discovery is served at "/.well-known/core" in the CoRE Link Format (RFC 6690 sec 4,
 * sec 2). The codec reads the Uri-Path (11), Content-Format (12) and Uri-Query (15) options
 * (RFC 7252 sec 5.10); an unrecognized option of class critical answers 4.02 Bad Option and an
 * elective one is ignored (sec 5.4.1). Block-wise transfer with the Block2 (23) and Block1 (27)
 * options (RFC 7959 sec 2.1) is compiled in by PROTOCORE_ENABLE_COAP_BLOCK; resource observation
 * with the Observe (6) option (RFC 7641 sec 2) by PROTOCORE_ENABLE_COAP_OBSERVE.
 *
 * The resource table is a fixed array of PROTOCORE_COAP_MAX_RESOURCES rows in storage. A path is
 * referenced by pointer and must outlive the server.
 *
 * The module exports one symbol, @ref Coap. Everything in coap.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_COAP_H
#define PROTOCORE_COAP_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_COAP

PROTOCORE_BEGIN_DECLS

/** @brief RFC 7252 sec 3 Code: a 3-bit class and a 5-bit detail, written "c.dd". */
#define COAP_CODE(c, dd) ((uint8_t)(((c) << 5) | ((dd) & 0x1F)))

// One bit per Method Code (RFC 7252 sec 12.1.1: GET 0.01, POST 0.02, PUT 0.03, DELETE 0.04), the bit
// position being the code. OR'd into the mask a resource registers, so they stay integer constants.
#define COAP_ALLOW_GET (1u << 1)    ///< 0x02, Method Code 0.01
#define COAP_ALLOW_POST (1u << 2)   ///< 0x04, Method Code 0.02
#define COAP_ALLOW_PUT (1u << 3)    ///< 0x08, Method Code 0.03
#define COAP_ALLOW_DELETE (1u << 4) ///< 0x10, Method Code 0.04

/** @brief RFC 7252 sec 3 Type (T), the 2-bit field. */
typedef enum PROTO_ENUM_PACKED
{
    COAP_TYPE_CON = 0, ///< Confirmable
    COAP_TYPE_NON = 1, ///< Non-confirmable
    COAP_TYPE_ACK = 2, ///< Acknowledgement
    COAP_TYPE_RST = 3, ///< Reset
} CoapType;

/** @brief RFC 7252 sec 12.1.1 Method Codes, the detail of a class-0 Code byte. */
typedef enum PROTO_ENUM_PACKED
{
    COAP_GET = 1,    ///< 0.01 GET (RFC 7252 sec 5.8.1)
    COAP_POST = 2,   ///< 0.02 POST (sec 5.8.2)
    COAP_PUT = 3,    ///< 0.03 PUT (sec 5.8.3)
    COAP_DELETE = 4, ///< 0.04 DELETE (sec 5.8.4)
} CoapMethod;

/** @brief RFC 7252 sec 5.9 Response Codes, plus the three RFC 7959 sec 2.9 adds. */
typedef enum PROTO_ENUM_PACKED
{
    COAP_RSP_CREATED = COAP_CODE(2, 1),                   ///< 2.01 Created
    COAP_RSP_DELETED = COAP_CODE(2, 2),                   ///< 2.02 Deleted
    COAP_RSP_VALID = COAP_CODE(2, 3),                     ///< 2.03 Valid
    COAP_RSP_CHANGED = COAP_CODE(2, 4),                   ///< 2.04 Changed
    COAP_RSP_CONTENT = COAP_CODE(2, 5),                   ///< 2.05 Content
    COAP_RSP_CONTINUE = COAP_CODE(2, 31),                 ///< 2.31 Continue (RFC 7959 sec 2.9.1)
    COAP_RSP_BAD_REQUEST = COAP_CODE(4, 0),               ///< 4.00 Bad Request
    COAP_RSP_BAD_OPTION = COAP_CODE(4, 2),                ///< 4.02 Bad Option
    COAP_RSP_NOT_FOUND = COAP_CODE(4, 4),                 ///< 4.04 Not Found
    COAP_RSP_METHOD_NOT_ALLOWED = COAP_CODE(4, 5),        ///< 4.05 Method Not Allowed
    COAP_RSP_NOT_ACCEPTABLE = COAP_CODE(4, 6),            ///< 4.06 Not Acceptable
    COAP_RSP_REQUEST_ENTITY_INCOMPLETE = COAP_CODE(4, 8), ///< 4.08 Request Entity Incomplete (RFC 7959 sec 2.9.2)
    COAP_RSP_REQUEST_ENTITY_TOO_LARGE = COAP_CODE(4, 13), ///< 4.13 Request Entity Too Large (RFC 7959 sec 2.9.3)
    COAP_RSP_INTERNAL_SERVER_ERROR = COAP_CODE(5, 0),     ///< 5.00 Internal Server Error
    COAP_RSP_NOT_IMPLEMENTED = COAP_CODE(5, 1),           ///< 5.01 Not Implemented
} CoapResponseCode;

/** @brief CoAP Content-Formats (RFC 7252 sec 12.3 Table 9; 60 from the IANA sub-registry, RFC 8949). */
typedef enum PROTO_ENUM_PACKED
{
    COAP_CF_TEXT = 0,      ///< text/plain;charset=utf-8
    COAP_CF_LINK = 40,     ///< application/link-format (RFC 6690)
    COAP_CF_XML = 41,      ///< application/xml
    COAP_CF_OCTET = 42,    ///< application/octet-stream
    COAP_CF_JSON = 50,     ///< application/json
    COAP_CF_CBOR = 60,     ///< application/cbor (RFC 8949)
    COAP_CF_NONE = 0xFFFF, ///< no Content-Format option present or emitted
} CoapContentFormat;

/**
 * @brief A decoded request handed to a resource handler.
 *
 * Every pointer references scratch that lives for the handler call. Copy out what outlives it.
 */
typedef struct
{
    CoapMethod method;                ///< the Method Code the request carries
    const char *path;                 ///< the Uri-Path segments rejoined, leading '/' included
    const char *query;                ///< the Uri-Query segments rejoined by '&', or "" when absent
    const uint8_t *payload;           ///< the request payload, NULL when payload_len is 0
    size_t payload_len;               ///< its length in bytes
    CoapContentFormat content_format; ///< the request's Content-Format, or COAP_CF_NONE
} CoapRequest;

/**
 * @brief The response a resource handler fills in.
 *
 * @c code starts at 2.05 Content. The body goes into @c payload within @c payload_cap, with
 * @c payload_len set to what was written and @c content_format naming its format.
 */
typedef struct
{
    uint8_t code;                     ///< the Response Code byte, a ::CoapResponseCode
    CoapContentFormat content_format; ///< what the body is, or COAP_CF_NONE
    uint8_t *payload;                 ///< where the handler writes the body
    size_t payload_cap;               ///< how much room that has
    size_t payload_len;               ///< how much it wrote
} CoapResponse;

/** @brief Resource handler: read @p req, fill @p resp. */
typedef void (*CoapHandler)(const CoapRequest *req, CoapResponse *resp);

/** @brief One row of the resource table: a path, the methods it answers, and what answers them. */
typedef struct
{
    const char *path;    ///< the Uri-Path it is reached at, referenced by pointer and not copied
    uint8_t methods;     ///< the Method Codes it answers, as COAP_ALLOW_* bits
    CoapHandler handler; ///< what an allowed method on that path dispatches to
} CoapResourceArgs;

/** @brief RFC 7252 sec 3: one request datagram in, one response datagram out. */
typedef struct
{
    const uint8_t *req; ///< the request datagram's octets
    size_t req_len;     ///< how many
    uint8_t *resp;      ///< where the response datagram is built
    size_t resp_cap;    ///< how much room that has
} CoapMessageArgs;

/** @brief RFC 7641: the Observe option a response carries, and the resource a notification renders. */
typedef struct
{
    int32_t seq;      ///< the sequence number a 2.xx notification carries (sec 4.4); below 0 omits the option
    const char *path; ///< the resource a notification re-renders (sec 4.2)
} CoapObserveArgs;

/** @brief RFC 7252 sec 4.5: the exchange a deduplication entry is keyed by, and what it caches. */
typedef struct
{
    const char *src_ip;  ///< the source endpoint's address, as text
    uint16_t src_port;   ///< its port
    uint16_t mid;        ///< the Message ID that endpoint sent
    const uint8_t *resp; ///< the response a store caches for it
    size_t resp_len;     ///< how many octets that is
} CoapExchangeArgs;

/** @brief The UDP endpoint the server receives on (RFC 7252 sec 12.6: port 5683, service "coap"). */
typedef struct
{
    uint16_t port; ///< the port a begin binds
} CoapBindArgs;

/** @brief The server's own state and the calls that reach it, described only in coap.c. */
struct CoapInternal;

/**
 * @brief The CoAP server.
 *
 * A caller sets the members a call takes, invokes it through ::Coap, and reads the outcome off the
 * same handle.
 *
 * No slot member: one server owns one resource table, and each call names its own subject inside its
 * own argument group, so no member is common to all of them.
 *
 * @var CoapNs::resource  the row an add registers
 * @var CoapNs::msg       the request datagram a process reads and the response it writes
 * @var CoapNs::observe   the Observe sequence a response carries and the resource a notify renders
 * @var CoapNs::exchange  the endpoint and Message ID a deduplication entry is keyed by
 * @var CoapNs::bind      the UDP port a begin binds
 * @var CoapNs::ok        a call's true/false outcome
 * @var CoapNs::n         the octets a call produced: the response datagram's length, or a cached response's
 * @var CoapNs::bytes     the cached response a deduplication lookup reports, NULL on a miss
 * @var CoapNs::reset          empty the resource table and every cache
 * @var CoapNs::add_resource   register @c resource, reporting false when the table is full
 * @var CoapNs::process        answer one request datagram, emitting no Observe option
 * @var CoapNs::process_observe  the same, carrying @c observe.seq in a 2.xx response (RFC 7641 sec 4.2)
 * @var CoapNs::dedup_lookup   report the response already sent for @c exchange (RFC 7252 sec 4.5)
 * @var CoapNs::dedup_store    cache the response sent for @c exchange so a repeat is answered from it
 * @var CoapNs::begin          bind @c bind.port and route its datagrams into the server
 * @var CoapNs::notify         send the current representation of @c observe.path to every observer
 * @var CoapNs::internal       the server's state and the calls that reach it
 */
typedef struct
{
    CoapResourceArgs resource; ///< what registering a resource takes
    CoapMessageArgs msg;       ///< what answering one datagram takes
    CoapObserveArgs observe;   ///< what the Observe option carries
    CoapExchangeArgs exchange; ///< what a deduplication entry is keyed by
    CoapBindArgs bind;         ///< what binding the receive port takes

    proto_bool ok;
    size_t n;
    const uint8_t *bytes;

    void (*reset)(struct CoapInternal *ctx);
    void (*add_resource)(struct CoapInternal *ctx);
    void (*process)(struct CoapInternal *ctx);
    void (*process_observe)(struct CoapInternal *ctx);
#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
    void (*dedup_lookup)(struct CoapInternal *ctx);
    void (*dedup_store)(struct CoapInternal *ctx);
#endif
    void (*begin)(struct CoapInternal *ctx);
#if PROTOCORE_ENABLE_COAP_OBSERVE
    void (*notify)(struct CoapInternal *ctx);
#endif

    struct CoapInternal *internal;
} CoapNs;

/** @brief The one symbol this module exports. */
extern CoapNs Coap;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_COAP

#endif // PROTOCORE_COAP_H
