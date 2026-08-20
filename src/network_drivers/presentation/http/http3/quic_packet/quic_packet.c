// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_packet.c
 * @brief QUIC packet headers and packet-number coding - implementation. See protocore_quic_packet.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP3

#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/http/http3/quic_packet/quic_packet.h"

PROTOCORE_BEGIN_DECLS

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

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void quic_packet_pn_length(uint8_t *restrict work);

static void quic_packet_is_long_header(uint8_t *restrict work)
{
    (void)work;
    uint8_t first = QuicPacket.is_long_header_args.first;

    QuicPacket.ok = (first & 0x80) != 0;
}

static void quic_packet_parse_long_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = QuicPacket.parse_long_header_args.buf;
    size_t len = QuicPacket.parse_long_header_args.len;
    QuicLongHeader *out = QuicPacket.parse_long_header_args.out;

    if (len < 7 || !(buf[0] & 0x80)) // first byte + version(4) + dcid_len(1) + scid_len(1)
    {
        QuicPacket.ok = PROTO_FALSE;
        return;
    }
    out->first = buf[0];
    out->version = rd_be32(buf + 1);
    // RFC 9000 sec 17.2: every long-header packet carries the Fixed Bit except a Version
    // Negotiation packet, which is the only one with version 0. Without it the packet is not valid
    // in this version and is discarded.
    if (out->version != 0 && !(buf[0] & 0x40))
    {
        QuicPacket.ok = PROTO_FALSE;
        return;
    }
    out->type = (uint8_t)((buf[0] & 0x30) >> 4);
    size_t pos = 5;
    uint8_t dcl = buf[pos++];
    if (dcl > QUIC_MAX_CID_LEN || pos + dcl + 1 > len) // +1 for the SCID length byte
    {
        QuicPacket.ok = PROTO_FALSE;
        return;
    }
    mem.cpy(out->dcid, buf + pos, dcl);
    out->dcid_len = dcl;
    pos += dcl;
    uint8_t scl = buf[pos++];
    if (scl > QUIC_MAX_CID_LEN || pos + scl > len)
    {
        QuicPacket.ok = PROTO_FALSE;
        return;
    }
    mem.cpy(out->scid, buf + pos, scl);
    out->scid_len = scl;
    pos += scl;
    out->hdr_len = pos;
    QuicPacket.ok = PROTO_TRUE;
}

static void quic_packet_build_long_header(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicPacket.build_long_header_args.out;
    size_t cap = QuicPacket.build_long_header_args.cap;
    uint8_t type = QuicPacket.build_long_header_args.type;
    uint32_t version = QuicPacket.build_long_header_args.version;
    const uint8_t *dcid = QuicPacket.build_long_header_args.dcid;
    uint8_t dcid_len = QuicPacket.build_long_header_args.dcid_len;
    const uint8_t *scid = QuicPacket.build_long_header_args.scid;
    uint8_t scid_len = QuicPacket.build_long_header_args.scid_len;
    uint8_t pn_len = QuicPacket.build_long_header_args.pn_len;

    if (dcid_len > QUIC_MAX_CID_LEN || scid_len > QUIC_MAX_CID_LEN || pn_len < 1 || pn_len > 4)
    {
        QuicPacket.n = 0;
        return;
    }
    size_t need = 1 + 4 + 1 + dcid_len + 1 + scid_len;
    if (need > cap)
    {
        QuicPacket.n = 0;
        return;
    }
    // 1 Fixed(1) type(2) reserved(00) pn_len-1(2). Reserved bits are 0 before header protection.
    out[0] = (uint8_t)(0x80 | 0x40 | ((type & 0x03) << 4) | ((pn_len - 1) & 0x03));
    wr_be32(out + 1, version);
    size_t pos = 5;
    out[pos++] = dcid_len;
    mem.cpy(out + pos, dcid, dcid_len);
    pos += dcid_len;
    out[pos++] = scid_len;
    mem.cpy(out + pos, scid, scid_len);
    pos += scid_len;
    QuicPacket.n = pos;
}

static void quic_packet_parse_short_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = QuicPacket.parse_short_header_args.buf;
    size_t len = QuicPacket.parse_short_header_args.len;
    uint8_t dcid_len = QuicPacket.parse_short_header_args.dcid_len;
    QuicShortHeader *out = QuicPacket.parse_short_header_args.out;

    // RFC 9000 sec 17.3.1 states the same Fixed Bit requirement, and a short header is never a
    // Version Negotiation packet, so it holds here without exception.
    if (dcid_len > QUIC_MAX_CID_LEN || len < (size_t)1 + dcid_len || (buf[0] & 0x80) || !(buf[0] & 0x40))
    {
        QuicPacket.ok = PROTO_FALSE;
        return;
    }
    out->first = buf[0];
    out->spin = (uint8_t)((buf[0] & 0x20) ? 1 : 0);
    out->key_phase = (uint8_t)((buf[0] & 0x04) ? 1 : 0);
    out->pn_len = (uint8_t)((buf[0] & 0x03) + 1);
    mem.cpy(out->dcid, buf + 1, dcid_len);
    out->dcid_len = dcid_len;
    out->hdr_len = (size_t)1 + dcid_len;
    QuicPacket.ok = PROTO_TRUE;
}

static void quic_packet_build_version_negotiation(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicPacket.build_version_negotiation_args.out;
    size_t cap = QuicPacket.build_version_negotiation_args.cap;
    const uint8_t *dcid = QuicPacket.build_version_negotiation_args.dcid;
    uint8_t dcid_len = QuicPacket.build_version_negotiation_args.dcid_len;
    const uint8_t *scid = QuicPacket.build_version_negotiation_args.scid;
    uint8_t scid_len = QuicPacket.build_version_negotiation_args.scid_len;
    const uint32_t *versions = QuicPacket.build_version_negotiation_args.versions;
    size_t nversions = QuicPacket.build_version_negotiation_args.nversions;

    if (dcid_len > QUIC_MAX_CID_LEN || scid_len > QUIC_MAX_CID_LEN)
    {
        QuicPacket.n = 0;
        return;
    }
    size_t need = 1 + 4 + 1 + dcid_len + 1 + scid_len + nversions * 4;
    if (need > cap)
    {
        QuicPacket.n = 0;
        return;
    }
    out[0] = 0x80 | 0x40; // header form + the recommended set Fixed-Bit position (sec 17.2.1)
    wr_be32(out + 1, 0);  // Version = 0 marks a Version Negotiation packet
    size_t pos = 5;
    out[pos++] = dcid_len;
    mem.cpy(out + pos, dcid, dcid_len);
    pos += dcid_len;
    out[pos++] = scid_len;
    mem.cpy(out + pos, scid, scid_len);
    pos += scid_len;
    for (size_t i = 0; i < nversions; i++)
    {
        wr_be32(out + pos, versions[i]);
        pos += 4;
    }
    QuicPacket.n = pos;
}

static void quic_packet_pn_length(uint8_t *restrict work)
{
    (void)work;
    uint64_t full_pn = QuicPacket.pn_length_args.full_pn;
    int64_t largest_acked = QuicPacket.pn_length_args.largest_acked;

    // num_unacked = full_pn + 1 when nothing acked, else full_pn - largest_acked (RFC 9000 A.2).
    uint64_t num_unacked = (largest_acked < 0) ? (full_pn + 1) : (full_pn - (uint64_t)largest_acked);
    // Smallest k in 1..4 with 2^(8k) >= 2 * num_unacked (i.e. min_bits = log2(n)+1 rounded up to bytes).
    for (uint8_t k = 1; k < 4; k++)
    {
        if (((uint64_t)1 << (8 * k)) >= (num_unacked << 1))
        {
            QuicPacket.u8 = k;
            return;
        }
    }
    QuicPacket.u8 = 4;
}

static void quic_packet_pn_encode(uint8_t *restrict work)
{
    uint8_t *out = QuicPacket.pn_encode_args.out;
    size_t cap = QuicPacket.pn_encode_args.cap;
    uint64_t full_pn = QuicPacket.pn_encode_args.full_pn;
    int64_t largest_acked = QuicPacket.pn_encode_args.largest_acked;

    QuicPacket.pn_length_args.full_pn = full_pn;
    QuicPacket.pn_length_args.largest_acked = largest_acked;
    quic_packet_pn_length(work);
    uint8_t n = QuicPacket.u8;
    if (n > cap)
    {
        QuicPacket.n = 0;
        return;
    }
    for (uint8_t i = 0; i < n; i++)
    {
        out[i] = (uint8_t)(full_pn >> (8 * (n - 1 - i))); // truncate to the n least-significant bytes, big-endian
    }
    QuicPacket.n = n;
}

static void quic_packet_pn_decode(uint8_t *restrict work)
{
    (void)work;
    uint64_t largest_pn = QuicPacket.pn_decode_args.largest_pn;
    uint64_t truncated_pn = QuicPacket.pn_decode_args.truncated_pn;
    uint8_t pn_nbits = QuicPacket.pn_decode_args.pn_nbits;

    uint64_t expected = largest_pn + 1;
    uint64_t pn_win = (uint64_t)1 << pn_nbits;
    uint64_t pn_hwin = pn_win / 2;
    uint64_t pn_mask = pn_win - 1;
    uint64_t candidate = (expected & ~pn_mask) | (truncated_pn & pn_mask);
    // candidate <= expected - pn_hwin, guarded against underflow and the 2^62 ceiling.
    if (candidate + pn_hwin <= expected && candidate < (((uint64_t)1 << 62) - pn_win))
    {
        QuicPacket.u64 = candidate + pn_win;
        return;
    }
    if (candidate > expected + pn_hwin && candidate >= pn_win)
    {
        QuicPacket.u64 = candidate - pn_win;
        return;
    }
    QuicPacket.u64 = candidate;
}

QuicPacketNs QuicPacket = {
    .is_long_header = quic_packet_is_long_header,
    .parse_long_header = quic_packet_parse_long_header,
    .build_long_header = quic_packet_build_long_header,
    .parse_short_header = quic_packet_parse_short_header,
    .build_version_negotiation = quic_packet_build_version_negotiation,
    .pn_length = quic_packet_pn_length,
    .pn_encode = quic_packet_pn_encode,
    .pn_decode = quic_packet_pn_decode,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3
