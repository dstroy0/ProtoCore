// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file proxy_protocol.h
 * @brief HAProxy PROXY protocol codec (PROTOCORE_ENABLE_PROXY_PROTOCOL) - zero-heap parser +
 *        builder for the v1 (text) and v2 (binary) headers a load balancer / proxy prepends,
 *        so the server can recover the real client IPv4 when it sits behind one.
 *
 * The header is sent once, before the proxied stream:
 *  - v1 (text): `PROXY TCP4 <src-ip> <dst-ip> <src-port> <dst-port>\r\n` (space-separated,
 *    CRLF-terminated; also `PROXY TCP6 ...` and `PROXY UNKNOWN\r\n`).
 *  - v2 (binary): a 12-octet signature, then ver_cmd (high nibble version 2, low nibble
 *    command - 0x1 PROXY / 0x0 LOCAL), fam (high nibble address family - 0x1 AF_INET, low
 *    nibble transport - 0x1 STREAM), a 2-octet big-endian address-block length, then the
 *    address block (for TCP/IPv4: src(4) dst(4) src-port(2) dst-port(2), network order).
 *
 * This codec handles TCP/IPv4 (the library's address family); IPv6 / UNIX / LOCAL headers
 * parse to their length but yield no addresses. Format per the HAProxy PROXY protocol spec.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PROXY_PROTOCOL_H
#define PROTOCORE_PROXY_PROTOCOL_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_PROXY_PROTOCOL

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define PROXY_V2_SIG_LEN 12         ///< v2 signature length
#define PROXY_V2_VER_CMD_PROXY 0x21 ///< version 2 | PROXY command
#define PROXY_V2_VER_CMD_LOCAL 0x20 ///< version 2 | LOCAL command
#define PROXY_V2_FAM_TCP4 0x11      ///< AF_INET | STREAM (TCP over IPv4)

/** @brief The decoded proxied connection endpoints (IPv4, host byte order). */
typedef struct
{
    uint8_t version;     ///< 1 or 2
    proto_bool has_addr; ///< true when TCP/IPv4 addresses were decoded
    uint32_t src_addr;   ///< real client IPv4 (host order)
    uint32_t dst_addr;   ///< proxied destination IPv4
    uint16_t src_port;
    uint16_t dst_port;
} ProxyInfo;
/** @brief What parse takes: buf, len, out, consumed. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    ProxyInfo *out;
    size_t *consumed; ///< receives the header length so the caller can skip it before the stream
} ProxyProtocolParseArgs;
/** @brief What v1_build takes: buf, cap, src_addr, dst_addr, ... */
typedef struct
{
    char *buf;
    size_t cap;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint16_t src_port;
    uint16_t dst_port;
} ProxyProtocolV1BuildArgs;
/** @brief What v2_build takes: buf, cap, src_addr, dst_addr, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint16_t src_port;
    uint16_t dst_port;
} ProxyProtocolV2BuildArgs;
/**
 * @brief HAProxy PROXY protocol codec (PROTOCORE_ENABLE_PROXY_PROTOCOL) - zero-heap parser + builder for the v1 (text)
 * and v2 (binary) headers a load balancer / proxy prepends, so the server can recover the real client IPv4 when it sits
 * behind one.
 *
 * A caller sets the members a call takes, invokes it through ::ProxyProtocol with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   ProxyProtocol.parse_args.buf = ...;
 *   ProxyProtocol.parse_args.len = ...;
 *   ProxyProtocol.parse_args.out = ...;
 *   ProxyProtocol.parse_args.consumed = ...;
 *   ProxyProtocol.parse(work);
 *   // ProxyProtocol.ok is what the call reports
 *
 * @var ProxyProtocolNs::parse_args  what parse takes: buf, len, out, consumed
 * @var ProxyProtocolNs::v1_build_args  what v1_build takes: buf, cap, src_addr, dst_addr,
 * @var ProxyProtocolNs::v2_build_args  what v2_build takes: buf, cap, src_addr, dst_addr,
 * @var ProxyProtocolNs::ok  true if a complete v1/v2 header was parsed; false if absent or not ...
 * @var ProxyProtocolNs::n  the count a call reports
 * @var ProxyProtocolNs::parse  detect + parse a PROXY header (v1 or v2) at the head of [buf, ...
 * @var ProxyProtocolNs::v1_build  build a v1 (text) TCP4 header. Returns bytes written (excluding ...
 * @var ProxyProtocolNs::v2_build  build a v2 (binary) TCP/IPv4 PROXY header. Returns 28, or 0 on ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ProxyProtocolParseArgs parse_args;
    ProxyProtocolV1BuildArgs v1_build_args;
    ProxyProtocolV2BuildArgs v2_build_args;
    proto_bool ok;
    size_t n;
} ProxyProtocolVars;

/** @brief The operands and the outcome. */
extern ProxyProtocolVars ProxyProtocolV;

/** @brief The entries. */
typedef struct
{
    void (*const parse)(uint8_t *restrict work);
    void (*const v1_build)(uint8_t *restrict work);
    void (*const v2_build)(uint8_t *restrict work);
} ProxyProtocolNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in ProxyProtocolV or a region of the borrow at a fixed offset.
void protocore_proxy_protocol_parse(uint8_t *restrict work);
void protocore_proxy_protocol_v1_build(uint8_t *restrict work);
void protocore_proxy_protocol_v2_build(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `ProxyProtocol.parse(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const ProxyProtocolNs ProxyProtocol __attribute__((unused)) = {
    .parse = protocore_proxy_protocol_parse,
    .v1_build = protocore_proxy_protocol_v1_build,
    .v2_build = protocore_proxy_protocol_v2_build,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PROXY_PROTOCOL

#endif // PROTOCORE_PROXY_PROTOCOL_H
