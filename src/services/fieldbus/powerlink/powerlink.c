// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file powerlink.c
 * @brief Ethernet POWERLINK basic frame codec (see powerlink.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_POWERLINK

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/powerlink/powerlink.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void powerlink_build(uint8_t *restrict work);

static void powerlink_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t msg_type = Powerlink.build_args.msg_type;
    uint8_t dest = Powerlink.build_args.dest;
    uint8_t source = Powerlink.build_args.source;
    const uint8_t *payload = Powerlink.build_args.payload;
    size_t payload_len = Powerlink.build_args.payload_len;
    uint8_t *out = Powerlink.build_args.out;
    size_t cap = Powerlink.build_args.cap;

    if (!out || (payload_len && !payload))
    {
        Powerlink.n = 0;
        return;
    }
    size_t n = 3 + payload_len;
    if (n > cap)
    {
        Powerlink.n = 0;
        return;
    }
    out[0] = msg_type;
    out[1] = dest;
    out[2] = source;
    if (payload_len)
    {
        mem.cpy(out + 3, payload, payload_len);
    }
    Powerlink.n = n;
}

static void powerlink_soc(uint8_t *restrict work)
{
    uint8_t source = Powerlink.soc_args.source;
    uint8_t *out = Powerlink.soc_args.out;
    size_t cap = Powerlink.soc_args.cap;

    Powerlink.build_args.msg_type = EPL_MSG_SOC;
    Powerlink.build_args.dest = EPL_NODE_BROADCAST;
    Powerlink.build_args.source = source;
    Powerlink.build_args.payload = NULL;
    Powerlink.build_args.payload_len = 0;
    Powerlink.build_args.out = out;
    Powerlink.build_args.cap = cap;
    powerlink_build(work);
}

static void powerlink_preq(uint8_t *restrict work)
{
    uint8_t dest_cn = Powerlink.preq_args.dest_cn;
    uint8_t source = Powerlink.preq_args.source;
    const uint8_t *pdo = Powerlink.preq_args.pdo;
    size_t pdo_len = Powerlink.preq_args.pdo_len;
    uint8_t *out = Powerlink.preq_args.out;
    size_t cap = Powerlink.preq_args.cap;

    Powerlink.build_args.msg_type = EPL_MSG_PREQ;
    Powerlink.build_args.dest = dest_cn;
    Powerlink.build_args.source = source;
    Powerlink.build_args.payload = pdo;
    Powerlink.build_args.payload_len = pdo_len;
    Powerlink.build_args.out = out;
    Powerlink.build_args.cap = cap;
    powerlink_build(work);
}

static void powerlink_pres(uint8_t *restrict work)
{
    uint8_t source_cn = Powerlink.pres_args.source_cn;
    const uint8_t *pdo = Powerlink.pres_args.pdo;
    size_t pdo_len = Powerlink.pres_args.pdo_len;
    uint8_t *out = Powerlink.pres_args.out;
    size_t cap = Powerlink.pres_args.cap;

    Powerlink.build_args.msg_type = EPL_MSG_PRES;
    Powerlink.build_args.dest = EPL_NODE_BROADCAST;
    Powerlink.build_args.source = source_cn;
    Powerlink.build_args.payload = pdo;
    Powerlink.build_args.payload_len = pdo_len;
    Powerlink.build_args.out = out;
    Powerlink.build_args.cap = cap;
    powerlink_build(work);
}

static void powerlink_soa(uint8_t *restrict work)
{
    uint8_t source = Powerlink.soa_args.source;
    const uint8_t *payload = Powerlink.soa_args.payload;
    size_t payload_len = Powerlink.soa_args.payload_len;
    uint8_t *out = Powerlink.soa_args.out;
    size_t cap = Powerlink.soa_args.cap;

    Powerlink.build_args.msg_type = EPL_MSG_SOA;
    Powerlink.build_args.dest = EPL_NODE_BROADCAST;
    Powerlink.build_args.source = source;
    Powerlink.build_args.payload = payload;
    Powerlink.build_args.payload_len = payload_len;
    Powerlink.build_args.out = out;
    Powerlink.build_args.cap = cap;
    powerlink_build(work);
}

static void powerlink_asnd(uint8_t *restrict work)
{
    uint8_t dest = Powerlink.asnd_args.dest;
    uint8_t source = Powerlink.asnd_args.source;
    const uint8_t *payload = Powerlink.asnd_args.payload;
    size_t payload_len = Powerlink.asnd_args.payload_len;
    uint8_t *out = Powerlink.asnd_args.out;
    size_t cap = Powerlink.asnd_args.cap;

    Powerlink.build_args.msg_type = EPL_MSG_ASND;
    Powerlink.build_args.dest = dest;
    Powerlink.build_args.source = source;
    Powerlink.build_args.payload = payload;
    Powerlink.build_args.payload_len = payload_len;
    Powerlink.build_args.out = out;
    Powerlink.build_args.cap = cap;
    powerlink_build(work);
}

static void powerlink_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *frame = Powerlink.parse_args.frame;
    size_t len = Powerlink.parse_args.len;
    EplFrame *out = Powerlink.parse_args.out;

    if (!frame || !out || len < 3)
    {
        Powerlink.ok = PROTO_FALSE;
        return;
    }
    uint8_t mt = frame[0];
    if (mt != EPL_MSG_SOC && mt != EPL_MSG_PREQ && mt != EPL_MSG_PRES && mt != EPL_MSG_SOA && mt != EPL_MSG_ASND)
    {
        Powerlink.ok = PROTO_FALSE;
        return;
    }
    out->msg_type = mt;
    out->dest = frame[1];
    out->source = frame[2];
    out->payload = (len > 3) ? (frame + 3) : NULL;
    out->payload_len = len - 3;
    Powerlink.ok = PROTO_TRUE;
}

PowerlinkNs Powerlink = {.build = powerlink_build,
                         .soc = powerlink_soc,
                         .preq = powerlink_preq,
                         .pres = powerlink_pres,
                         .soa = powerlink_soa,
                         .asnd = powerlink_asnd,
                         .parse = powerlink_parse};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_POWERLINK
