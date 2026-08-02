// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cotp.c
 * @brief TPKT + COTP (X.224 class 0) frame builder + parser (pure, host-tested).
 */

#include "services/fieldbus/cotp/cotp.h"

#if PC_ENABLE_COTP

#include <string.h>

size_t pc_tpkt_build(uint8_t *buf, size_t cap, const uint8_t *payload, size_t payload_len)
{
    if (!buf || (payload_len && !payload))
    {
        return 0;
    }
    size_t total = TPKT_HEADER_SIZE + payload_len;
    if (total > 0xFFFF || total > cap)
    {
        return 0;
    }
    buf[0] = TPKT_VERSION;
    buf[1] = 0x00;                  // reserved
    buf[2] = (uint8_t)(total >> 8); // length, big-endian, whole packet
    buf[3] = (uint8_t)(total & 0xFF);
    if (payload_len)
    {
        memcpy(buf + TPKT_HEADER_SIZE, payload, payload_len);
    }
    return total;
}

proto_bool pc_tpkt_parse(const uint8_t *buf, size_t len, const uint8_t **payload, size_t *payload_len, size_t *consumed)
{
    if (!buf || len < TPKT_HEADER_SIZE)
    {
        return PROTO_FALSE;
    }
    if (buf[0] != TPKT_VERSION)
    {
        return PROTO_FALSE;
    }
    size_t total = ((size_t)buf[2] << 8) | buf[3];
    if (total < TPKT_HEADER_SIZE || total > len)
    {
        return PROTO_FALSE; // invalid / not fully buffered
    }
    if (payload)
    {
        *payload = buf + TPKT_HEADER_SIZE;
    }
    if (payload_len)
    {
        *payload_len = total - TPKT_HEADER_SIZE;
    }
    if (consumed)
    {
        *consumed = total;
    }
    return PROTO_TRUE;
}

size_t pc_cotp_build_dt(uint8_t *buf, size_t cap, const uint8_t *data, size_t data_len, proto_bool eot)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    size_t total = COTP_DT_HEADER_LEN + data_len;
    if (total > cap)
    {
        return 0;
    }
    buf[0] = COTP_DT_HEADER_LEN - 1;        // LI = octets after LI (code + nr/eot)
    buf[1] = COTP_DT;                       // Data TPDU
    buf[2] = (uint8_t)(eot ? COTP_EOT : 0); // EOT flag | TPDU-NR (0 for class 0)
    if (data_len)
    {
        memcpy(buf + COTP_DT_HEADER_LEN, data, data_len);
    }
    return total;
}

size_t pc_cotp_build_cr(uint8_t *buf, size_t cap, uint16_t src_ref, uint8_t tpdu_size_code, const uint8_t *extra_params,
                        size_t extra_len)
{
    if (!buf || (extra_len && !extra_params))
    {
        return 0;
    }
    // after the LI octet the header is the one-octet code, the two-octet destination and source references,
    // the one-octet class, the three-octet TPDU-size parameter, then any extra parameters
    size_t after_li = 1 + 2 + 2 + 1 + 3 + extra_len;
    size_t total = 1 + after_li; // + the LI octet itself
    if (after_li > 0xFF || total > cap)
    {
        return 0;
    }
    size_t p = 0;
    buf[p++] = (uint8_t)after_li; // LI
    buf[p++] = COTP_CR;
    buf[p++] = 0x00; // dst-ref = 0 (unknown on a request)
    buf[p++] = 0x00;
    buf[p++] = (uint8_t)(src_ref >> 8);
    buf[p++] = (uint8_t)(src_ref & 0xFF);
    buf[p++] = 0x00; // class 0, no options
    buf[p++] = COTP_PARAM_TPDU_SIZE;
    buf[p++] = 0x01; // parameter length
    buf[p++] = tpdu_size_code;
    if (extra_len)
    {
        memcpy(buf + p, extra_params, extra_len);
        p += extra_len;
    }
    return p;
}

size_t pc_cotp_build_cc(uint8_t *buf, size_t cap, uint16_t dst_ref, uint16_t src_ref, uint8_t tpdu_size_code,
                        const uint8_t *extra_params, size_t extra_len)
{
    if (!buf || (extra_len && !extra_params))
    {
        return 0;
    }
    // after the LI octet the header is the one-octet code, the two-octet destination and source references,
    // the one-octet class, the three-octet TPDU-size parameter, then any extra parameters
    size_t after_li = 1 + 2 + 2 + 1 + 3 + extra_len;
    size_t total = 1 + after_li; // + the LI octet itself
    if (after_li > 0xFF || total > cap)
    {
        return 0;
    }
    size_t p = 0;
    buf[p++] = (uint8_t)after_li; // LI
    buf[p++] = COTP_CC;
    buf[p++] = (uint8_t)(dst_ref >> 8); // dst-ref = the peer's src-ref, echoed
    buf[p++] = (uint8_t)(dst_ref & 0xFF);
    buf[p++] = (uint8_t)(src_ref >> 8);
    buf[p++] = (uint8_t)(src_ref & 0xFF);
    buf[p++] = 0x00; // class 0, no options
    buf[p++] = COTP_PARAM_TPDU_SIZE;
    buf[p++] = 0x01; // parameter length
    buf[p++] = tpdu_size_code;
    if (extra_len)
    {
        memcpy(buf + p, extra_params, extra_len);
        p += extra_len;
    }
    return p;
}

proto_bool pc_cotp_parse(const uint8_t *buf, size_t len, CotpHeader *out)
{
    if (!buf || !out || len < 2)
    {
        return PROTO_FALSE;
    }
    uint8_t li = buf[0];
    size_t header = (size_t)li + 1; // LI counts the octets after itself
    if (header > len || li < 1)
    {
        return PROTO_FALSE;
    }

    out->code = (uint8_t)(buf[1] & 0xF0); // type is the high nibble
    out->dst_ref = 0;
    out->src_ref = 0;
    out->eot = PROTO_FALSE;
    out->data = NULL;
    out->data_len = 0;

    if (out->code == COTP_DT)
    {
        if (li < 2) // need the TPDU-NR/EOT octet
        {
            return PROTO_FALSE;
        }
        out->eot = (buf[2] & COTP_EOT) != 0;
        out->data = buf + header;
        out->data_len = len - header;
        return PROTO_TRUE;
    }
    if (out->code == COTP_CR || out->code == COTP_CC)
    {
        if (li < 6) // code + dst-ref(2) + src-ref(2) + class(1)
        {
            return PROTO_FALSE;
        }
        out->dst_ref = (uint16_t)((buf[2] << 8) | buf[3]);
        out->src_ref = (uint16_t)((buf[4] << 8) | buf[5]);
        return PROTO_TRUE;
    }
    // Other TPDU types (DR/DC/ER/...): the type code is reported; no body extracted.
    return PROTO_TRUE;
}

#endif // PC_ENABLE_COTP
