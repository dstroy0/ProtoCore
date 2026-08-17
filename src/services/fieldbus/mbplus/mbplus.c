// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mbplus.c
 * @brief Modbus Plus HDLC token-bus frame codec (see mbplus.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MBPLUS

#include "mmgr/protomem.h"
#include "services/fieldbus/mbplus/mbplus.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC16_X25

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void mbplus_crc(uint8_t *restrict work);

static void mbplus_crc(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bytes = Mbplus.crc_args.bytes;
    size_t len = Mbplus.crc_args.len;

    // CRC-16/X-25: poly 0x1021 reflected, init 0xFFFF, xorout 0xFFFF - cataloged as CRC-16/IBM-SDLC.
    Crc.args.params = &PROTOCORE_CRC16_X25;
    Crc.args.data = bytes;
    Crc.args.len = len;
    Crc.compute(Crc.internal);
    Mbplus.value = (uint16_t)Crc.value;
}

static void mbplus_build(uint8_t *restrict work)
{
    uint8_t address = Mbplus.build_args.address;
    uint8_t control = Mbplus.build_args.control;
    const uint8_t *payload = Mbplus.build_args.payload;
    size_t payload_len = Mbplus.build_args.payload_len;
    uint8_t *out = Mbplus.build_args.out;
    size_t cap = Mbplus.build_args.cap;

    if (!out || (payload_len && !payload) || address < 1 || address > MBPLUS_MAX_STATION)
    {
        Mbplus.n = 0;
        return;
    }
    size_t n = 1 + 1 + 1 + payload_len + 2 + 1; // 7E addr ctrl payload CRClo CRChi 7E
    if (n > cap)
    {
        Mbplus.n = 0;
        return;
    }
    size_t i = 0;
    out[i++] = MBPLUS_FLAG;
    size_t body = i;
    out[i++] = address;
    out[i++] = control;
    if (payload_len)
    {
        mem.cpy(out + i, payload, payload_len);
        i += payload_len;
    }
    Mbplus.crc_args.bytes = out + body;
    Mbplus.crc_args.len = (i - body);
    mbplus_crc(work);
    uint16_t crc = Mbplus.value; // over addr..last payload
    out[i++] = (uint8_t)crc;     // CRC low byte first
    out[i++] = (uint8_t)(crc >> 8);
    out[i++] = MBPLUS_FLAG;
    Mbplus.n = i;
}

static void mbplus_parse(uint8_t *restrict work)
{
    const uint8_t *frame = Mbplus.parse_args.frame;
    size_t len = Mbplus.parse_args.len;
    MbPlusFrame *out = Mbplus.parse_args.out;

    // Min: 7E addr ctrl CRClo CRChi 7E = 6 bytes.
    if (!frame || !out || len < 6)
    {
        Mbplus.ok = PROTO_FALSE;
        return;
    }
    if (frame[0] != MBPLUS_FLAG || frame[len - 1] != MBPLUS_FLAG)
    {
        Mbplus.ok = PROTO_FALSE;
        return;
    }
    // Body is frame[1 .. len-2), the CRC is the last 2 body bytes.
    size_t body_end = len - 1;     // index of the trailing flag
    size_t crc_pos = body_end - 2; // low byte of the CRC
    size_t covered = crc_pos - 1;  // number of bytes (addr..payload) the CRC covers
    Mbplus.crc_args.bytes = frame + 1;
    Mbplus.crc_args.len = covered;
    mbplus_crc(work);
    uint16_t want = Mbplus.value;
    uint16_t got = (uint16_t)(frame[crc_pos] | (frame[crc_pos + 1] << 8));
    if (want != got)
    {
        Mbplus.ok = PROTO_FALSE;
        return;
    }
    out->address = frame[1];
    out->control = frame[2];
    out->payload = (covered > 2) ? (frame + 3) : NULL;
    out->payload_len = covered - 2; // minus addr + ctrl
    Mbplus.ok = PROTO_TRUE;
}

static void mbplus_next_token(uint8_t *restrict work)
{
    (void)work;
    uint8_t current = Mbplus.next_token_args.current;
    uint8_t max_station = Mbplus.next_token_args.max_station;

    if (max_station < 1)
    {
        Mbplus.value = 1;
        return;
    }
    Mbplus.value = (current >= max_station) ? 1 : (uint8_t)(current + 1);
}

MbplusNs Mbplus = {.crc = mbplus_crc, .build = mbplus_build, .parse = mbplus_parse, .next_token = mbplus_next_token};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MBPLUS
