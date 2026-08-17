// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cotp.c
 * @brief TPKT + COTP (X.224 class 0) frame builder + parser (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_COTP

#include "mmgr/protomem.h"
#include "services/fieldbus/cotp/cotp.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void cotp_tpkt_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Cotp.tpkt_build_args.buf;
    size_t cap = Cotp.tpkt_build_args.cap;
    const uint8_t *payload = Cotp.tpkt_build_args.payload;
    size_t payload_len = Cotp.tpkt_build_args.payload_len;

    if (!buf || (payload_len && !payload))
    {
        Cotp.n = 0;
        return;
    }
    size_t total = TPKT_HEADER_SIZE + payload_len;
    if (total > 0xFFFF || total > cap)
    {
        Cotp.n = 0;
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
    Cotp.n = total;
}

static void cotp_tpkt_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Cotp.tpkt_parse_args.buf;
    size_t len = Cotp.tpkt_parse_args.len;
    const uint8_t **payload = Cotp.tpkt_parse_args.payload;
    size_t *payload_len = Cotp.tpkt_parse_args.payload_len;
    size_t *consumed = Cotp.tpkt_parse_args.consumed;

    if (!buf || len < TPKT_HEADER_SIZE)
    {
        Cotp.ok = PROTO_FALSE;
        return;
    }
    if (buf[0] != TPKT_VERSION)
    {
        Cotp.ok = PROTO_FALSE;
        return;
    }
    size_t total = ((size_t)buf[2] << 8) | buf[3];
    if (total < TPKT_HEADER_SIZE || total > len)
    {
        Cotp.ok = PROTO_FALSE;
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
    Cotp.ok = PROTO_TRUE;
}

static void cotp_build_dt(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Cotp.build_dt_args.buf;
    size_t cap = Cotp.build_dt_args.cap;
    const uint8_t *data = Cotp.build_dt_args.data;
    size_t data_len = Cotp.build_dt_args.data_len;
    proto_bool eot = Cotp.build_dt_args.eot;

    if (!buf || (data_len && !data))
    {
        Cotp.n = 0;
        return;
    }
    size_t total = COTP_DT_HEADER_LEN + data_len;
    if (total > cap)
    {
        Cotp.n = 0;
        return;
    }
    buf[0] = COTP_DT_HEADER_LEN - 1;        // LI = octets after LI (code + nr/eot)
    buf[1] = COTP_DT;                       // Data TPDU
    buf[2] = (uint8_t)(eot ? COTP_EOT : 0); // EOT flag | TPDU-NR (0 for class 0)
    if (data_len)
    {
        mem.cpy(buf + COTP_DT_HEADER_LEN, data, data_len);
    }
    Cotp.n = total;
}

static void cotp_build_cr(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Cotp.build_cr_args.buf;
    size_t cap = Cotp.build_cr_args.cap;
    uint16_t src_ref = Cotp.build_cr_args.src_ref;
    uint8_t tpdu_size_code = Cotp.build_cr_args.tpdu_size_code;
    const uint8_t *extra_params = Cotp.build_cr_args.extra_params;
    size_t extra_len = Cotp.build_cr_args.extra_len;

    if (!buf || (extra_len && !extra_params))
    {
        Cotp.n = 0;
        return;
    }
    // after the LI octet the header is the one-octet code, the two-octet destination and source references,
    // the one-octet class, the three-octet TPDU-size parameter, then any extra parameters
    size_t after_li = 1 + 2 + 2 + 1 + 3 + extra_len;
    size_t total = 1 + after_li; // + the LI octet itself
    if (after_li > 0xFF || total > cap)
    {
        Cotp.n = 0;
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
    Cotp.n = p;
}

static void cotp_build_cc(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Cotp.build_cc_args.buf;
    size_t cap = Cotp.build_cc_args.cap;
    uint16_t dst_ref = Cotp.build_cc_args.dst_ref;
    uint16_t src_ref = Cotp.build_cc_args.src_ref;
    uint8_t tpdu_size_code = Cotp.build_cc_args.tpdu_size_code;
    const uint8_t *extra_params = Cotp.build_cc_args.extra_params;
    size_t extra_len = Cotp.build_cc_args.extra_len;

    if (!buf || (extra_len && !extra_params))
    {
        Cotp.n = 0;
        return;
    }
    // after the LI octet the header is the one-octet code, the two-octet destination and source references,
    // the one-octet class, the three-octet TPDU-size parameter, then any extra parameters
    size_t after_li = 1 + 2 + 2 + 1 + 3 + extra_len;
    size_t total = 1 + after_li; // + the LI octet itself
    if (after_li > 0xFF || total > cap)
    {
        Cotp.n = 0;
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
    Cotp.n = p;
}

static void cotp_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Cotp.parse_args.buf;
    size_t len = Cotp.parse_args.len;
    CotpHeader *out = Cotp.parse_args.out;

    if (!buf || !out || len < 2)
    {
        Cotp.ok = PROTO_FALSE;
        return;
    }
    uint8_t li = buf[0];
    size_t header = (size_t)li + 1; // LI counts the octets after itself
    if (header > len || li < 1)
    {
        Cotp.ok = PROTO_FALSE;
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
            Cotp.ok = PROTO_FALSE;
            return;
        }
        out->eot = (buf[2] & COTP_EOT) != 0;
        out->data = buf + header;
        out->data_len = len - header;
        Cotp.ok = PROTO_TRUE;
        return;
    }
    if (out->code == COTP_CR || out->code == COTP_CC)
    {
        if (li < 6) // code + dst-ref(2) + src-ref(2) + class(1)
        {
            Cotp.ok = PROTO_FALSE;
            return;
        }
        out->dst_ref = (uint16_t)((buf[2] << 8) | buf[3]);
        out->src_ref = (uint16_t)((buf[4] << 8) | buf[5]);
        Cotp.ok = PROTO_TRUE;
        return;
    }
    // Other TPDU types (DR/DC/ER/...): the type code is reported; no body extracted.
    Cotp.ok = PROTO_TRUE;
}

CotpNs Cotp = {.tpkt_build = cotp_tpkt_build,
               .tpkt_parse = cotp_tpkt_parse,
               .build_dt = cotp_build_dt,
               .build_cr = cotp_build_cr,
               .build_cc = cotp_build_cc,
               .parse = cotp_parse};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_COTP
