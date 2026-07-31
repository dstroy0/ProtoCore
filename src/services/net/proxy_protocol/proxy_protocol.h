// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file proxy_protocol.h
 * @brief HAProxy PROXY protocol codec (PC_ENABLE_PROXY_PROTOCOL) - zero-heap parser +
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

#include "protocore_config.h"

#if PC_ENABLE_PROXY_PROTOCOL

#include <stddef.h>
#include <stdint.h>

#define PROXY_V2_SIG_LEN 12         ///< v2 signature length
#define PROXY_V2_VER_CMD_PROXY 0x21 ///< version 2 | PROXY command
#define PROXY_V2_VER_CMD_LOCAL 0x20 ///< version 2 | LOCAL command
#define PROXY_V2_FAM_TCP4 0x11      ///< AF_INET | STREAM (TCP over IPv4)

/** @brief The decoded proxied connection endpoints (IPv4, host byte order). */
struct ProxyInfo
{
    uint8_t version;   ///< 1 or 2
    bool has_addr;     ///< true when TCP/IPv4 addresses were decoded
    uint32_t src_addr; ///< real client IPv4 (host order)
    uint32_t dst_addr; ///< proxied destination IPv4
    uint16_t src_port;
    uint16_t dst_port;
};

/**
 * @brief Detect + parse a PROXY header (v1 or v2) at the head of [buf, buf+len).
 * @param consumed receives the header length so the caller can skip it before the stream.
 * @return true if a complete v1/v2 header was parsed; false if absent or not fully buffered.
 */
bool proxy_parse(const uint8_t *buf, size_t len, ProxyInfo *out, size_t *consumed);

/** @brief Build a v1 (text) TCP4 header. Returns bytes written (excluding NUL), or 0. */
size_t proxy_v1_build(char *buf, size_t cap, uint32_t src_addr, uint32_t dst_addr, uint16_t src_port,
                      uint16_t dst_port);

/** @brief Build a v2 (binary) TCP/IPv4 PROXY header. Returns 28, or 0 on overflow. */
size_t proxy_v2_build(uint8_t *buf, size_t cap, uint32_t src_addr, uint32_t dst_addr, uint16_t src_port,
                      uint16_t dst_port);

#endif // PC_ENABLE_PROXY_PROTOCOL

#endif // PROTOCORE_PROXY_PROTOCOL_H
