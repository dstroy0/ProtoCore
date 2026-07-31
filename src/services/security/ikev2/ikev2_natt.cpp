// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ikev2_natt.cpp
 * @brief IKEv2 NAT traversal (RFC 7296 §2.23 / RFC 3948) - see ikev2_natt.h.
 */

#include "services/security/ikev2/ikev2_natt.h"

#if PC_ENABLE_IKEV2

#include "crypto/hash/sha1.h"
#include <string.h>

namespace
{
// SPIi(8) | SPIr(8) | IP(4 or 16) | Port(2) - the largest NAT-detection hash input.
#define PC_NATD_INPUT_MAX (PC_IKE_SPI_LEN + PC_IKE_SPI_LEN + 16 + 2)

// Assemble the hash input; returns its length, or 0 on a bad address length.
size_t natd_input(uint8_t *buf, const uint8_t *init_spi, const uint8_t *resp_spi, const uint8_t *ip, size_t ip_len,
                  uint16_t port)
{
    if (!init_spi || !resp_spi || !ip || (ip_len != 4 && ip_len != 16))
    {
        return 0;
    }
    size_t n = 0;
    memcpy(buf + n, init_spi, PC_IKE_SPI_LEN);
    n += PC_IKE_SPI_LEN;
    memcpy(buf + n, resp_spi, PC_IKE_SPI_LEN);
    n += PC_IKE_SPI_LEN;
    memcpy(buf + n, ip, ip_len);
    n += ip_len;
    buf[n++] = (uint8_t)(port >> 8);
    buf[n++] = (uint8_t)port;
    return n;
}
} // namespace

size_t pc_ike_natd_hash(const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                        const uint8_t *ip, size_t ip_len, uint16_t port, uint8_t out[PC_IKE_NATD_HASH_LEN])
{
    if (!out)
    {
        return 0;
    }
    uint8_t in[PC_NATD_INPUT_MAX];
    size_t n = natd_input(in, init_spi, resp_spi, ip, ip_len, port);
    if (n == 0)
    {
        return 0;
    }
    pc_sha1(in, n, out);
    return PC_IKE_NATD_HASH_LEN;
}

size_t pc_ike_natd_source_build(uint8_t *buf, size_t cap, IkePayloadType next_payload,
                                const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                                const uint8_t *src_ip, size_t ip_len, uint16_t src_port)
{
    uint8_t hash[PC_IKE_NATD_HASH_LEN];
    if (pc_ike_natd_hash(init_spi, resp_spi, src_ip, ip_len, src_port, hash) == 0)
    {
        return 0;
    }
    return pc_ike_notify_build(buf, cap, next_payload, IkeProtocol::IKE_PROTO_NONE, nullptr, 0,
                               PC_IKE_N_NAT_DETECTION_SOURCE_IP, hash, PC_IKE_NATD_HASH_LEN);
}

size_t pc_ike_natd_dest_build(uint8_t *buf, size_t cap, IkePayloadType next_payload,
                              const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                              const uint8_t *dst_ip, size_t ip_len, uint16_t dst_port)
{
    uint8_t hash[PC_IKE_NATD_HASH_LEN];
    if (pc_ike_natd_hash(init_spi, resp_spi, dst_ip, ip_len, dst_port, hash) == 0)
    {
        return 0;
    }
    return pc_ike_notify_build(buf, cap, next_payload, IkeProtocol::IKE_PROTO_NONE, nullptr, 0,
                               PC_IKE_N_NAT_DETECTION_DESTINATION_IP, hash, PC_IKE_NATD_HASH_LEN);
}

bool pc_ike_natd_match(const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                       const uint8_t *ip, size_t ip_len, uint16_t port, const uint8_t *hash)
{
    if (!hash)
    {
        return false;
    }
    uint8_t expect[PC_IKE_NATD_HASH_LEN];
    if (pc_ike_natd_hash(init_spi, resp_spi, ip, ip_len, port, expect) == 0)
    {
        return false;
    }
    return memcmp(expect, hash, PC_IKE_NATD_HASH_LEN) == 0;
}

bool pc_ike_natd_peer_behind_nat(const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                                 const uint8_t *observed_src_ip, size_t ip_len, uint16_t observed_src_port,
                                 const uint8_t *received_source_hash)
{
    return !pc_ike_natd_match(init_spi, resp_spi, observed_src_ip, ip_len, observed_src_port, received_source_hash);
}

bool pc_ike_natd_self_behind_nat(const uint8_t init_spi[PC_IKE_SPI_LEN], const uint8_t resp_spi[PC_IKE_SPI_LEN],
                                 const uint8_t *local_ip, size_t ip_len, uint16_t local_port,
                                 const uint8_t *received_dest_hash)
{
    return !pc_ike_natd_match(init_spi, resp_spi, local_ip, ip_len, local_port, received_dest_hash);
}

bool pc_natt_is_keepalive(const uint8_t *p, size_t len)
{
    return p && len == 1 && p[0] == PC_NATT_KEEPALIVE_BYTE;
}

bool pc_natt_is_ike(const uint8_t *p, size_t len)
{
    if (!p || len < PC_NATT_NON_ESP_MARKER_LEN)
    {
        return false;
    }
    // The 4-octet Non-ESP Marker is all zero; a real ESP packet starts with a nonzero SPI.
    return p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0;
}

#endif // PC_ENABLE_IKEV2
