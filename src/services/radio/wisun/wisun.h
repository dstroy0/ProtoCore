// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_WISUN_H
#define PROTOCORE_WISUN_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths
#include "shared/ip/ip.h"     // the complete type a public struct below holds by value

PROTOCORE_BEGIN_DECLS

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
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */

// PROTOCORE_WISUN_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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

#include "shared/ip/ip.h" // protocore_ip: the type a parameter points at

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*build_coap)(uint8_t *, uint8_t, uint8_t, uint16_t, const uint8_t *, uint8_t, const char *, const uint8_t *,
                         size_t, uint8_t *, size_t);
    void (*init)(uint8_t *, WisunFan *, const protocore_ip *, WisunNode *, size_t);
    int (*node_register)(uint8_t *, WisunFan *, const protocore_ip *, uint32_t);
    proto_bool (*node_find)(uint8_t *, const WisunFan *, const protocore_ip *, size_t *);
    size_t (*joined_count)(uint8_t *, const WisunFan *);
    size_t (*nodes_json)(uint8_t *, const WisunFan *, char *, size_t);
} WisunNs;
PROTOCORE_NS_LAYOUT(WisunNs, build_coap, init, node_register, node_find, joined_count, nodes_json);

/**
 * @brief Build a CoAP client request: header + Uri-Path options (one per `/` .
 * @param work PROTOCORE_WISUN_BORROW bytes the caller took. Not held past the call.
 * @param type WISUN_COAP_CON / WISUN_COAP_NON
 * @param code method code (WISUN_COAP_GET / WISUN_COAP_PUT)
 * @param msg_id the 16-bit message id (echoed in the ACK)
 * @param token correlation token (0..8 bytes; may be null if tkl == 0)
 * @param tkl token length
 * @param uri_path resource path, e.g. "sensors/temp" (leading / optional)
 * @param payload request body (may be null if plen == 0)
 * @param plen payload length
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_wisun_build_coap(uint8_t *work, uint8_t type, uint8_t code, uint16_t msg_id, const uint8_t *token,
                                  uint8_t tkl, const char *uri_path, const uint8_t *payload, size_t plen, uint8_t *out,
                                  size_t cap);
/**
 * @brief Initialize the connector over caller storage.
 * @param work PROTOCORE_WISUN_BORROW bytes the caller took. Not held past the call.
 * @param fan Fan
 * @param border_router Border router
 * @param storage Storage
 * @param cap Cap
 */
void protocore_wisun_init(uint8_t *work, WisunFan *fan, const protocore_ip *border_router, WisunNode *storage,
                          size_t cap);
/**
 * @brief Register (or refresh) a node by address; sets joined + last_seen.
 * @param work PROTOCORE_WISUN_BORROW bytes the caller took. Not held past the call.
 * @param fan Fan
 * @param addr Addr
 * @param now Now
 * @return The int.
 */
int protocore_wisun_node_register(uint8_t *work, WisunFan *fan, const protocore_ip *addr, uint32_t now);
/**
 * @brief Find a node by address. idx (may be null) receives the index. found.
 * @param work PROTOCORE_WISUN_BORROW bytes the caller took. Not held past the call.
 * @param fan Fan
 * @param addr Addr
 * @param idx Idx
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_wisun_node_find(uint8_t *work, const WisunFan *fan, const protocore_ip *addr, size_t *idx);
/**
 * @brief Number of joined nodes.
 * @param work PROTOCORE_WISUN_BORROW bytes the caller took. Not held past the call.
 * @param fan Fan
 * @return The size_t.
 */
size_t protocore_wisun_joined_count(uint8_t *work, const WisunFan *fan);
/**
 * @brief Serialize the node table as `[{"addr":"..","joined":bool},...]` for .
 * @param work PROTOCORE_WISUN_BORROW bytes the caller took. Not held past the call.
 * @param fan Fan
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_wisun_nodes_json(uint8_t *work, const WisunFan *fan, char *out, size_t cap);

/** @brief Module namespace. */
PROTOCORE_NS WisunNs Wisun PROTOCORE_UNUSED = {.build_coap = protocore_wisun_build_coap,
                                               .init = protocore_wisun_init,
                                               .node_register = protocore_wisun_node_register,
                                               .node_find = protocore_wisun_node_find,
                                               .joined_count = protocore_wisun_joined_count,
                                               .nodes_json = protocore_wisun_nodes_json};

PROTOCORE_END_DECLS

#endif // PROTOCORE_WISUN_H
