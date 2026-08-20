// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wisun.h
 * @brief Wi-SUN FAN border-router connector (PROTOCORE_ENABLE_WISUN).
 *
 * Wi-SUN FAN is an IPv6 / UDP / CoAP mesh, not a byte-level radio the ESP32 drives - the FAN radio is
 * terminated by a **border router / devboard** and each mesh node is reached as an ordinary IPv6 CoAP
 * endpoint. So the connector rides the existing IP stack: it keeps a table of the FAN nodes (their IPv6
 * `protocore_ip` addresses + join state) behind the border router, and builds the CoAP client requests to their
 * resources (the CoAP service ships a *server*, so the client-request builder is here). The app sends the
 * built PDU to the node's address over `protocore_udp`; the specific devboard only sets which border router you
 * point at, not this code.
 *
 * Pure: `protocore_wisun_build_coap` frames an RFC 7252 request (header + Uri-Path options + payload), the node
 * registry tracks the mesh, and `protocore_wisun_nodes_json` exposes it to the web. No heap, no stdlib,
 * host-testable.
 */

#ifndef PROTOCORE_WISUN_H
#define PROTOCORE_WISUN_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_WISUN

#include "shared/ip/ip.h" // the complete type a public struct below holds by value

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief CoAP message type + method codes (RFC 7252) used by the connector. */
#define WISUN_COAP_CON 0 ///< Confirmable.
#define WISUN_COAP_NON 1 ///< Non-confirmable.
#define WISUN_COAP_GET 1 ///< method code 0.01.
#define WISUN_COAP_PUT 3 ///< method code 0.03.

/** @brief One FAN mesh node behind the border router. */
typedef struct
{
    protocore_ip addr;  ///< the node's IPv6 address on the mesh.
    proto_bool joined;  ///< true once the node has joined the FAN.
    uint32_t last_seen; ///< tick of the last contact.
} WisunNode;

/** @brief The FAN connector state over a caller-owned node table. */
typedef struct
{
    protocore_ip border_router; ///< the border router / devboard address.
    WisunNode *nodes;
    size_t count;
    size_t cap;
} WisunFan;

/** @brief What build_coap takes: type, code, msg_id, token, tkl, ... */
typedef struct
{
    uint8_t type;           ///< WISUN_COAP_CON / WISUN_COAP_NON
    uint8_t code;           ///< method code (WISUN_COAP_GET / WISUN_COAP_PUT)
    uint16_t msg_id;        ///< the 16-bit message id (echoed in the ACK)
    const uint8_t *token;   ///< correlation token (0..8 bytes; may be null if tkl == 0)
    uint8_t tkl;            ///< token length
    const char *uri_path;   ///< resource path, e.g. "sensors/temp" (leading / optional)
    const uint8_t *payload; ///< request body (may be null if plen == 0)
    size_t plen;            ///< payload length
    uint8_t *out;
    size_t cap;
} WisunBuildCoapArgs;

/** @brief What init takes: fan, border_router, storage, cap. */
typedef struct
{
    WisunFan *fan;
    const protocore_ip *border_router;
    WisunNode *storage;
    size_t cap;
} WisunInitArgs;

/** @brief What node_register takes: fan, addr, now. */
typedef struct
{
    WisunFan *fan;
    const protocore_ip *addr;
    uint32_t now;
} WisunNodeRegisterArgs;

/** @brief What node_find takes: fan, addr, idx. */
typedef struct
{
    const WisunFan *fan;
    const protocore_ip *addr;
    size_t *idx;
} WisunNodeFindArgs;

/** @brief What joined_count takes: fan. */
typedef struct
{
    const WisunFan *fan;
} WisunJoinedCountArgs;

/** @brief What nodes_json takes: fan, out, cap. */
typedef struct
{
    const WisunFan *fan;
    char *out;
    size_t cap;
} WisunNodesJsonArgs;

/**
 * @brief Wi-SUN FAN border-router connector (PROTOCORE_ENABLE_WISUN).
 *
 * A caller sets the members a call takes, invokes it through ::Wisun with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Wisun.build_coap_args.type = ...;
 *   Wisun.build_coap_args.code = ...;
 *   Wisun.build_coap_args.msg_id = ...;
 *   Wisun.build_coap_args.token = ...;
 *   Wisun.build_coap_args.tkl = ...;
 *   Wisun.build_coap_args.uri_path = ...;
 *   Wisun.build_coap_args.payload = ...;
 *   Wisun.build_coap_args.plen = ...;
 *   Wisun.build_coap_args.out = ...;
 *   Wisun.build_coap_args.cap = ...;
 *   Wisun.build_coap(work);
 *   // Wisun.n is what the call reports
 *
 * @var WisunNs::build_coap_args  what build_coap takes: type, code, msg_id, token, tkl,
 * @var WisunNs::init_args  what init takes: fan, border_router, storage, cap
 * @var WisunNs::node_register_args  what node_register takes: fan, addr, now
 * @var WisunNs::node_find_args  what node_find takes: fan, addr, idx
 * @var WisunNs::joined_count_args  what joined_count takes: fan
 * @var WisunNs::nodes_json_args  what nodes_json takes: fan, out, cap
 * @var WisunNs::ok  a call's true/false outcome
 * @var WisunNs::n  the PDU length, or 0 on overflow / bad args (tkl > 8)
 * @var WisunNs::i32  the node index, or -1 if the table is full / bad args
 * @var WisunNs::build_coap  build a CoAP client request: header + Uri-Path options (one per `/` ...
 * @var WisunNs::init  initialize the connector over caller storage
 * @var WisunNs::node_register  register (or refresh) a node by address; sets joined + last_seen
 * @var WisunNs::node_find  find a node by address. idx (may be null) receives the index. found
 * @var WisunNs::joined_count  number of joined nodes
 * @var WisunNs::nodes_json  serialize the node table as `[{"addr":"..","joined":bool},...]` for ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    WisunBuildCoapArgs build_coap_args;
    WisunInitArgs init_args;
    WisunNodeRegisterArgs node_register_args;
    WisunNodeFindArgs node_find_args;
    WisunJoinedCountArgs joined_count_args;
    WisunNodesJsonArgs nodes_json_args;
    proto_bool ok;
    size_t n;
    int i32;
} WisunVars;

/** @brief The operands and the outcome. */
extern WisunVars WisunV;

/** @brief The entries. */
typedef struct
{
    void (*const build_coap)(uint8_t *restrict work);
    void (*const init)(uint8_t *restrict work);
    void (*const node_register)(uint8_t *restrict work);
    void (*const node_find)(uint8_t *restrict work);
    void (*const joined_count)(uint8_t *restrict work);
    void (*const nodes_json)(uint8_t *restrict work);
} WisunNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in WisunV or a region of the borrow at a fixed offset.
void protocore_wisun_build_coap(uint8_t *restrict work);
void protocore_wisun_init(uint8_t *restrict work);
void protocore_wisun_node_register(uint8_t *restrict work);
void protocore_wisun_node_find(uint8_t *restrict work);
void protocore_wisun_joined_count(uint8_t *restrict work);
void protocore_wisun_nodes_json(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Wisun.build_coap(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const WisunNs Wisun __attribute__((unused)) = {
    .build_coap = protocore_wisun_build_coap,
    .init = protocore_wisun_init,
    .node_register = protocore_wisun_node_register,
    .node_find = protocore_wisun_node_find,
    .joined_count = protocore_wisun_joined_count,
    .nodes_json = protocore_wisun_nodes_json,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WISUN

#endif // PROTOCORE_WISUN_H
