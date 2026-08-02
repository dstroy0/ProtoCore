// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_packet.cpp
 * @brief QUIC packet headers and packet-number coding - implementation. See pc_quic_packet.h.
 */

#include "network_drivers/presentation/http/http3/quic_packet.h"

#if PC_ENABLE_HTTP3

#include <string.h>

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

proto_bool pc_quic_is_long_header(uint8_t first)
{
    return (first & 0x80) != 0;
}

proto_bool pc_quic_parse_long_header(const uint8_t *buf, size_t len, QuicLongHeader *out)
{
    if (len < 7 || !(buf[0] & 0x80)) // first byte + version(4) + dcid_len(1) + scid_len(1)
    {
        return PROTO_FALSE;
    }
    out->first = buf[0];
    out->version = rd_be32(buf + 1);
    out->type = (uint8_t)((buf[0] & 0x30) >> 4);
    size_t pos = 5;
    uint8_t dcl = buf[pos++];
    if (dcl > QUIC_MAX_CID_LEN || pos + dcl + 1 > len) // +1 for the SCID length byte
    {
        return PROTO_FALSE;
    }
    memcpy(out->dcid, buf + pos, dcl);
    out->dcid_len = dcl;
    pos += dcl;
    uint8_t scl = buf[pos++];
    if (scl > QUIC_MAX_CID_LEN || pos + scl > len)
    {
        return PROTO_FALSE;
    }
    memcpy(out->scid, buf + pos, scl);
    out->scid_len = scl;
    pos += scl;
    out->hdr_len = pos;
    return PROTO_TRUE;
}

size_t pc_quic_build_long_header(uint8_t *out, size_t cap, uint8_t type, uint32_t version, const uint8_t *dcid,
                                 uint8_t dcid_len, const uint8_t *scid, uint8_t scid_len, uint8_t pn_len)
{
    if (dcid_len > QUIC_MAX_CID_LEN || scid_len > QUIC_MAX_CID_LEN || pn_len < 1 || pn_len > 4)
    {
        return 0;
    }
    size_t need = 1 + 4 + 1 + dcid_len + 1 + scid_len;
    if (need > cap)
    {
        return 0;
    }
    // 1 Fixed(1) type(2) reserved(00) pn_len-1(2). Reserved bits are 0 before header protection.
    out[0] = (uint8_t)(0x80 | 0x40 | ((type & 0x03) << 4) | ((pn_len - 1) & 0x03));
    wr_be32(out + 1, version);
    size_t pos = 5;
    out[pos++] = dcid_len;
    memcpy(out + pos, dcid, dcid_len);
    pos += dcid_len;
    out[pos++] = scid_len;
    memcpy(out + pos, scid, scid_len);
    pos += scid_len;
    return pos;
}

proto_bool pc_quic_parse_short_header(const uint8_t *buf, size_t len, uint8_t dcid_len, QuicShortHeader *out)
{
    if (dcid_len > QUIC_MAX_CID_LEN || len < (size_t)1 + dcid_len || (buf[0] & 0x80))
    {
        return PROTO_FALSE;
    }
    out->first = buf[0];
    out->spin = (uint8_t)((buf[0] & 0x20) ? 1 : 0);
    out->key_phase = (uint8_t)((buf[0] & 0x04) ? 1 : 0);
    out->pn_len = (uint8_t)((buf[0] & 0x03) + 1);
    memcpy(out->dcid, buf + 1, dcid_len);
    out->dcid_len = dcid_len;
    out->hdr_len = (size_t)1 + dcid_len;
    return PROTO_TRUE;
}

size_t pc_quic_build_version_negotiation(uint8_t *out, size_t cap, const uint8_t *dcid, uint8_t dcid_len,
                                         const uint8_t *scid, uint8_t scid_len, const uint32_t *versions,
                                         size_t nversions)
{
    if (dcid_len > QUIC_MAX_CID_LEN || scid_len > QUIC_MAX_CID_LEN)
    {
        return 0;
    }
    size_t need = 1 + 4 + 1 + dcid_len + 1 + scid_len + nversions * 4;
    if (need > cap)
    {
        return 0;
    }
    out[0] = 0x80 | 0x40; // header form + the recommended set Fixed-Bit position (sec 17.2.1)
    wr_be32(out + 1, 0);  // Version = 0 marks a Version Negotiation packet
    size_t pos = 5;
    out[pos++] = dcid_len;
    memcpy(out + pos, dcid, dcid_len);
    pos += dcid_len;
    out[pos++] = scid_len;
    memcpy(out + pos, scid, scid_len);
    pos += scid_len;
    for (size_t i = 0; i < nversions; i++)
    {
        wr_be32(out + pos, versions[i]);
        pos += 4;
    }
    return pos;
}

uint8_t pc_quic_pn_length(uint64_t full_pn, int64_t largest_acked)
{
    // num_unacked = full_pn + 1 when nothing acked, else full_pn - largest_acked (RFC 9000 A.2).
    uint64_t num_unacked = (largest_acked < 0) ? (full_pn + 1) : (full_pn - (uint64_t)largest_acked);
    // Smallest k in 1..4 with 2^(8k) >= 2 * num_unacked (i.e. min_bits = log2(n)+1 rounded up to bytes).
    for (uint8_t k = 1; k < 4; k++)
    {
        if (((uint64_t)1 << (8 * k)) >= (num_unacked << 1))
        {
            return k;
        }
    }
    return 4;
}

size_t pc_quic_pn_encode(uint8_t *out, size_t cap, uint64_t full_pn, int64_t largest_acked)
{
    uint8_t n = pc_quic_pn_length(full_pn, largest_acked);
    if (n > cap)
    {
        return 0;
    }
    for (uint8_t i = 0; i < n; i++)
    {
        out[i] = (uint8_t)(full_pn >> (8 * (n - 1 - i))); // truncate to the n least-significant bytes, big-endian
    }
    return n;
}

uint64_t pc_quic_pn_decode(uint64_t largest_pn, uint64_t truncated_pn, uint8_t pn_nbits)
{
    uint64_t expected = largest_pn + 1;
    uint64_t pn_win = (uint64_t)1 << pn_nbits;
    uint64_t pn_hwin = pn_win / 2;
    uint64_t pn_mask = pn_win - 1;
    uint64_t candidate = (expected & ~pn_mask) | (truncated_pn & pn_mask);
    // candidate <= expected - pn_hwin, guarded against underflow and the 2^62 ceiling.
    if (candidate + pn_hwin <= expected && candidate < (((uint64_t)1 << 62) - pn_win))
    {
        return candidate + pn_win;
    }
    if (candidate > expected + pn_hwin && candidate >= pn_win)
    {
        return candidate - pn_win;
    }
    return candidate;
}

#endif // PC_ENABLE_HTTP3
