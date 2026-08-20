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

void protocore_powerlink_build(uint8_t *restrict work);

void protocore_powerlink_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t msg_type = PowerlinkV.build_args.msg_type;
    uint8_t dest = PowerlinkV.build_args.dest;
    uint8_t source = PowerlinkV.build_args.source;
    const uint8_t *payload = PowerlinkV.build_args.payload;
    size_t payload_len = PowerlinkV.build_args.payload_len;
    uint8_t *out = PowerlinkV.build_args.out;
    size_t cap = PowerlinkV.build_args.cap;

    if (!out || (payload_len && !payload))
    {
        PowerlinkV.n = 0;
        return;
    }
    size_t n = 3 + payload_len;
    if (n > cap)
    {
        PowerlinkV.n = 0;
        return;
    }
    out[0] = msg_type;
    out[1] = dest;
    out[2] = source;
    if (payload_len)
    {
        mem.cpy(out + 3, payload, payload_len);
    }
    PowerlinkV.n = n;
}

void protocore_powerlink_soc(uint8_t *restrict work)
{
    uint8_t source = PowerlinkV.soc_args.source;
    uint8_t *out = PowerlinkV.soc_args.out;
    size_t cap = PowerlinkV.soc_args.cap;

    PowerlinkV.build_args.msg_type = EPL_MSG_SOC;
    PowerlinkV.build_args.dest = EPL_NODE_BROADCAST;
    PowerlinkV.build_args.source = source;
    PowerlinkV.build_args.payload = NULL;
    PowerlinkV.build_args.payload_len = 0;
    PowerlinkV.build_args.out = out;
    PowerlinkV.build_args.cap = cap;
    protocore_powerlink_build(work);
}

void protocore_powerlink_preq(uint8_t *restrict work)
{
    uint8_t dest_cn = PowerlinkV.preq_args.dest_cn;
    uint8_t source = PowerlinkV.preq_args.source;
    const uint8_t *pdo = PowerlinkV.preq_args.pdo;
    size_t pdo_len = PowerlinkV.preq_args.pdo_len;
    uint8_t *out = PowerlinkV.preq_args.out;
    size_t cap = PowerlinkV.preq_args.cap;

    PowerlinkV.build_args.msg_type = EPL_MSG_PREQ;
    PowerlinkV.build_args.dest = dest_cn;
    PowerlinkV.build_args.source = source;
    PowerlinkV.build_args.payload = pdo;
    PowerlinkV.build_args.payload_len = pdo_len;
    PowerlinkV.build_args.out = out;
    PowerlinkV.build_args.cap = cap;
    protocore_powerlink_build(work);
}

void protocore_powerlink_pres(uint8_t *restrict work)
{
    uint8_t source_cn = PowerlinkV.pres_args.source_cn;
    const uint8_t *pdo = PowerlinkV.pres_args.pdo;
    size_t pdo_len = PowerlinkV.pres_args.pdo_len;
    uint8_t *out = PowerlinkV.pres_args.out;
    size_t cap = PowerlinkV.pres_args.cap;

    PowerlinkV.build_args.msg_type = EPL_MSG_PRES;
    PowerlinkV.build_args.dest = EPL_NODE_BROADCAST;
    PowerlinkV.build_args.source = source_cn;
    PowerlinkV.build_args.payload = pdo;
    PowerlinkV.build_args.payload_len = pdo_len;
    PowerlinkV.build_args.out = out;
    PowerlinkV.build_args.cap = cap;
    protocore_powerlink_build(work);
}

void protocore_powerlink_soa(uint8_t *restrict work)
{
    uint8_t source = PowerlinkV.soa_args.source;
    const uint8_t *payload = PowerlinkV.soa_args.payload;
    size_t payload_len = PowerlinkV.soa_args.payload_len;
    uint8_t *out = PowerlinkV.soa_args.out;
    size_t cap = PowerlinkV.soa_args.cap;

    PowerlinkV.build_args.msg_type = EPL_MSG_SOA;
    PowerlinkV.build_args.dest = EPL_NODE_BROADCAST;
    PowerlinkV.build_args.source = source;
    PowerlinkV.build_args.payload = payload;
    PowerlinkV.build_args.payload_len = payload_len;
    PowerlinkV.build_args.out = out;
    PowerlinkV.build_args.cap = cap;
    protocore_powerlink_build(work);
}

void protocore_powerlink_asnd(uint8_t *restrict work)
{
    uint8_t dest = PowerlinkV.asnd_args.dest;
    uint8_t source = PowerlinkV.asnd_args.source;
    const uint8_t *payload = PowerlinkV.asnd_args.payload;
    size_t payload_len = PowerlinkV.asnd_args.payload_len;
    uint8_t *out = PowerlinkV.asnd_args.out;
    size_t cap = PowerlinkV.asnd_args.cap;

    PowerlinkV.build_args.msg_type = EPL_MSG_ASND;
    PowerlinkV.build_args.dest = dest;
    PowerlinkV.build_args.source = source;
    PowerlinkV.build_args.payload = payload;
    PowerlinkV.build_args.payload_len = payload_len;
    PowerlinkV.build_args.out = out;
    PowerlinkV.build_args.cap = cap;
    protocore_powerlink_build(work);
}

void protocore_powerlink_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *frame = PowerlinkV.parse_args.frame;
    size_t len = PowerlinkV.parse_args.len;
    EplFrame *out = PowerlinkV.parse_args.out;

    if (!frame || !out || len < 3)
    {
        PowerlinkV.ok = PROTO_FALSE;
        return;
    }
    uint8_t mt = frame[0];
    if (mt != EPL_MSG_SOC && mt != EPL_MSG_PREQ && mt != EPL_MSG_PRES && mt != EPL_MSG_SOA && mt != EPL_MSG_ASND)
    {
        PowerlinkV.ok = PROTO_FALSE;
        return;
    }
    out->msg_type = mt;
    out->dest = frame[1];
    out->source = frame[2];
    out->payload = (len > 3) ? (frame + 3) : NULL;
    out->payload_len = len - 3;
    PowerlinkV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
PowerlinkVars PowerlinkV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_POWERLINK
