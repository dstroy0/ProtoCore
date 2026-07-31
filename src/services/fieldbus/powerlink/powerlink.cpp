// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file powerlink.cpp
 * @brief Ethernet POWERLINK basic frame codec (see powerlink.h).
 */

#include "services/fieldbus/powerlink/powerlink.h"

#if PC_ENABLE_POWERLINK

#include <string.h>

size_t pc_epl_build(uint8_t msg_type, uint8_t dest, uint8_t source, const uint8_t *payload, size_t payload_len,
                    uint8_t *out, size_t cap)
{
    if (!out || (payload_len && !payload))
    {
        return 0;
    }
    size_t n = 3 + payload_len;
    if (n > cap)
    {
        return 0;
    }
    out[0] = msg_type;
    out[1] = dest;
    out[2] = source;
    if (payload_len)
    {
        memcpy(out + 3, payload, payload_len);
    }
    return n;
}

size_t pc_epl_soc(uint8_t source, uint8_t *out, size_t cap)
{
    return pc_epl_build(Epl::EPL_MSG_SOC, Epl::EPL_NODE_BROADCAST, source, nullptr, 0, out, cap);
}

size_t pc_epl_preq(uint8_t dest_cn, uint8_t source, const uint8_t *pdo, size_t pdo_len, uint8_t *out, size_t cap)
{
    return pc_epl_build(Epl::EPL_MSG_PREQ, dest_cn, source, pdo, pdo_len, out, cap);
}

size_t pc_epl_pres(uint8_t source_cn, const uint8_t *pdo, size_t pdo_len, uint8_t *out, size_t cap)
{
    return pc_epl_build(Epl::EPL_MSG_PRES, Epl::EPL_NODE_BROADCAST, source_cn, pdo, pdo_len, out, cap);
}

size_t pc_epl_soa(uint8_t source, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap)
{
    return pc_epl_build(Epl::EPL_MSG_SOA, Epl::EPL_NODE_BROADCAST, source, payload, payload_len, out, cap);
}

size_t pc_epl_asnd(uint8_t dest, uint8_t source, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap)
{
    return pc_epl_build(Epl::EPL_MSG_ASND, dest, source, payload, payload_len, out, cap);
}

bool pc_epl_parse(const uint8_t *frame, size_t len, EplFrame *out)
{
    if (!frame || !out || len < 3)
    {
        return false;
    }
    uint8_t mt = frame[0];
    if (mt != Epl::EPL_MSG_SOC && mt != Epl::EPL_MSG_PREQ && mt != Epl::EPL_MSG_PRES && mt != Epl::EPL_MSG_SOA &&
        mt != Epl::EPL_MSG_ASND)
    {
        return false;
    }
    out->msg_type = mt;
    out->dest = frame[1];
    out->source = frame[2];
    out->payload = (len > 3) ? (frame + 3) : nullptr;
    out->payload_len = len - 3;
    return true;
}

#endif // PC_ENABLE_POWERLINK
