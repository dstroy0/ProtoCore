// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wisun.h
 * @brief Wi-SUN FAN border-router connector (PC_ENABLE_WISUN).
 *
 * Wi-SUN FAN is an IPv6 / UDP / CoAP mesh, not a byte-level radio the ESP32 drives - the FAN radio is
 * terminated by a **border router / devboard** and each mesh node is reached as an ordinary IPv6 CoAP
 * endpoint. So the connector rides the existing IP stack: it keeps a table of the FAN nodes (their IPv6
 * `pc_ip` addresses + join state) behind the border router, and builds the CoAP client requests to their
 * resources (the CoAP service ships a *server*, so the client-request builder is here). The app sends the
 * built PDU to the node's address over `pc_udp`; the specific devboard only sets which border router you
 * point at, not this code.
 *
 * Pure: `pc_wisun_build_coap` frames an RFC 7252 request (header + Uri-Path options + payload), the node
 * registry tracks the mesh, and `pc_wisun_nodes_json` exposes it to the web. No heap, no stdlib,
 * host-testable.
 */

#ifndef PROTOCORE_WISUN_H
#define PROTOCORE_WISUN_H

#include "network_drivers/network/ip.h"
#include "protocore_config.h"

#if PC_ENABLE_WISUN

/** @brief CoAP message type + method codes (RFC 7252) used by the connector. */
#define WISUN_COAP_CON 0 ///< Confirmable.
#define WISUN_COAP_NON 1 ///< Non-confirmable.
#define WISUN_COAP_GET 1 ///< method code 0.01.
#define WISUN_COAP_PUT 3 ///< method code 0.03.

/**
 * @brief Build a CoAP client request: header + Uri-Path options (one per `/` segment) + optional payload.
 * @param type    WISUN_COAP_CON / WISUN_COAP_NON.
 * @param code    method code (WISUN_COAP_GET / WISUN_COAP_PUT).
 * @param msg_id  the 16-bit message id (echoed in the ACK).
 * @param token   correlation token (0..8 bytes; may be null if @p tkl == 0).
 * @param tkl     token length.
 * @param uri_path resource path, e.g. "sensors/temp" (leading / optional).
 * @param payload request body (may be null if @p plen == 0).
 * @param plen    payload length.
 * @return the PDU length, or 0 on overflow / bad args (tkl > 8).
 */
size_t pc_wisun_build_coap(uint8_t type, uint8_t code, uint16_t msg_id, const uint8_t *token, uint8_t tkl,
                           const char *uri_path, const uint8_t *payload, size_t plen, uint8_t *out, size_t cap);

/** @brief One FAN mesh node behind the border router. */
typedef struct
{
    pc_ip addr;         ///< the node's IPv6 address on the mesh.
    proto_bool joined;  ///< true once the node has joined the FAN.
    uint32_t last_seen; ///< tick of the last contact.
} WisunNode;

/** @brief The FAN connector state over a caller-owned node table. */
typedef struct
{
    pc_ip border_router; ///< the border router / devboard address.
    WisunNode *nodes;
    size_t count;
    size_t cap;
} WisunFan;

/** @brief Initialize the connector over caller storage. */
void pc_wisun_init(WisunFan *fan, const pc_ip *border_router, WisunNode *storage, size_t cap);

/**
 * @brief Register (or refresh) a node by address; sets joined + last_seen.
 * @return the node index, or -1 if the table is full / bad args.
 */
int pc_wisun_node_register(WisunFan *fan, const pc_ip *addr, uint32_t now);

/** @brief Find a node by address. @p idx (may be null) receives the index. @return found. */
proto_bool pc_wisun_node_find(const WisunFan *fan, const pc_ip *addr, size_t *idx);

/** @brief Number of joined nodes. */
size_t pc_wisun_joined_count(const WisunFan *fan);

/**
 * @brief Serialize the node table as `[{"addr":"..","joined":bool},...]` for the web.
 * @return length written (excl NUL), or 0 on overflow / bad args.
 */
size_t pc_wisun_nodes_json(const WisunFan *fan, char *out, size_t cap);

#endif // PC_ENABLE_WISUN
#endif // PROTOCORE_WISUN_H
