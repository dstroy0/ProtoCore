// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cotp.c
 * @brief TPKT + COTP (X.224 class 0) frame builder + parser (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_COTP

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/cotp/cotp.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_cotp_tpkt_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = CotpV.tpkt_build_args.buf;
    size_t cap = CotpV.tpkt_build_args.cap;
    const uint8_t *payload = CotpV.tpkt_build_args.payload;
    size_t payload_len = CotpV.tpkt_build_args.payload_len;

    if (!buf || (payload_len && !payload))
    {
        CotpV.n = 0;
        return;
    }
    size_t total = TPKT_HEADER_SIZE + payload_len;
    if (total > 0xFFFF || total > cap)
    {
        CotpV.n = 0;
        return;
    }
    buf[0] = TPKT_VERSION;
    buf[1] = 0x00;                  // reserved
    buf[2] = (uint8_t)(total >> 8); // length, big-endian, whole packet
    buf[3] = (uint8_t)(total & 0xFF);
    if (payload_len)
    {
        mem.cpy(buf + TPKT_HEADER_SIZE, payload, payload_len);
    }
    CotpV.n = total;
}

void protocore_cotp_tpkt_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = CotpV.tpkt_parse_args.buf;
    size_t len = CotpV.tpkt_parse_args.len;
    const uint8_t **payload = CotpV.tpkt_parse_args.payload;
    size_t *payload_len = CotpV.tpkt_parse_args.payload_len;
    size_t *consumed = CotpV.tpkt_parse_args.consumed;

    if (!buf || len < TPKT_HEADER_SIZE)
    {
        CotpV.ok = PROTO_FALSE;
        return;
    }
    if (buf[0] != TPKT_VERSION)
    {
        CotpV.ok = PROTO_FALSE;
        return;
    }
    size_t total = ((size_t)buf[2] << 8) | buf[3];
    if (total < TPKT_HEADER_SIZE || total > len)
    {
        CotpV.ok = PROTO_FALSE;
        return; // invalid / not fully buffered
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
    CotpV.ok = PROTO_TRUE;
}

void protocore_cotp_build_dt(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = CotpV.build_dt_args.buf;
    size_t cap = CotpV.build_dt_args.cap;
    const uint8_t *data = CotpV.build_dt_args.data;
    size_t data_len = CotpV.build_dt_args.data_len;
    proto_bool eot = CotpV.build_dt_args.eot;

    if (!buf || (data_len && !data))
    {
        CotpV.n = 0;
        return;
    }
    size_t total = COTP_DT_HEADER_LEN + data_len;
    if (total > cap)
    {
        CotpV.n = 0;
        return;
    }
    buf[0] = COTP_DT_HEADER_LEN - 1;        // LI = octets after LI (code + nr/eot)
    buf[1] = COTP_DT;                       // Data TPDU
    buf[2] = (uint8_t)(eot ? COTP_EOT : 0); // EOT flag | TPDU-NR (0 for class 0)
    if (data_len)
    {
        mem.cpy(buf + COTP_DT_HEADER_LEN, data, data_len);
    }
    CotpV.n = total;
}

void protocore_cotp_build_cr(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = CotpV.build_cr_args.buf;
    size_t cap = CotpV.build_cr_args.cap;
    uint16_t src_ref = CotpV.build_cr_args.src_ref;
    uint8_t tpdu_size_code = CotpV.build_cr_args.tpdu_size_code;
    const uint8_t *extra_params = CotpV.build_cr_args.extra_params;
    size_t extra_len = CotpV.build_cr_args.extra_len;

    if (!buf || (extra_len && !extra_params))
    {
        CotpV.n = 0;
        return;
    }
    // after the LI octet the header is the one-octet code, the two-octet destination and source references,
    // the one-octet class, the three-octet TPDU-size parameter, then any extra parameters
    size_t after_li = 1 + 2 + 2 + 1 + 3 + extra_len;
    size_t total = 1 + after_li; // + the LI octet itself
    if (after_li > 0xFF || total > cap)
    {
        CotpV.n = 0;
        return;
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
        mem.cpy(buf + p, extra_params, extra_len);
        p += extra_len;
    }
    CotpV.n = p;
}

void protocore_cotp_build_cc(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = CotpV.build_cc_args.buf;
    size_t cap = CotpV.build_cc_args.cap;
    uint16_t dst_ref = CotpV.build_cc_args.dst_ref;
    uint16_t src_ref = CotpV.build_cc_args.src_ref;
    uint8_t tpdu_size_code = CotpV.build_cc_args.tpdu_size_code;
    const uint8_t *extra_params = CotpV.build_cc_args.extra_params;
    size_t extra_len = CotpV.build_cc_args.extra_len;

    if (!buf || (extra_len && !extra_params))
    {
        CotpV.n = 0;
        return;
    }
    // after the LI octet the header is the one-octet code, the two-octet destination and source references,
    // the one-octet class, the three-octet TPDU-size parameter, then any extra parameters
    size_t after_li = 1 + 2 + 2 + 1 + 3 + extra_len;
    size_t total = 1 + after_li; // + the LI octet itself
    if (after_li > 0xFF || total > cap)
    {
        CotpV.n = 0;
        return;
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
        mem.cpy(buf + p, extra_params, extra_len);
        p += extra_len;
    }
    CotpV.n = p;
}

void protocore_cotp_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = CotpV.parse_args.buf;
    size_t len = CotpV.parse_args.len;
    CotpHeader *out = CotpV.parse_args.out;

    if (!buf || !out || len < 2)
    {
        CotpV.ok = PROTO_FALSE;
        return;
    }
    uint8_t li = buf[0];
    size_t header = (size_t)li + 1; // LI counts the octets after itself
    if (header > len || li < 1)
    {
        CotpV.ok = PROTO_FALSE;
        return;
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
            CotpV.ok = PROTO_FALSE;
            return;
        }
        out->eot = (buf[2] & COTP_EOT) != 0;
        out->data = buf + header;
        out->data_len = len - header;
        CotpV.ok = PROTO_TRUE;
        return;
    }
    if (out->code == COTP_CR || out->code == COTP_CC)
    {
        if (li < 6) // code + dst-ref(2) + src-ref(2) + class(1)
        {
            CotpV.ok = PROTO_FALSE;
            return;
        }
        out->dst_ref = (uint16_t)((buf[2] << 8) | buf[3]);
        out->src_ref = (uint16_t)((buf[4] << 8) | buf[5]);
        CotpV.ok = PROTO_TRUE;
        return;
    }
    // Other TPDU types (DR/DC/ER/...): the type code is reported; no body extracted.
    CotpV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
CotpVars CotpV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_COTP
