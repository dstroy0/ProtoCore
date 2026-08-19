// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file enocean.c
 * @brief EnOcean ESP3 serial codec - implementation.
 *
 * ESP3 telegram: 0x55 | data-len(2) | opt-len(1) | type(1) | CRC8H | data | opt | CRC8D.
 * CRC-8 is polynomial 0x07, MSB-first, init 0x00 (the ESP3 u8CRC8Table generator).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t crc_work[16]; // the borrow an entry takes; Crc never reads it

#if PROTOCORE_ENABLE_ENOCEAN

#include "mmgr/protomem/protomem.h"
#include "services/radio/enocean/enocean.h"

#include "shared/crc/crc.h" // PROTOCORE_CRC8_SMBUS

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void enocean_esp3_crc8(uint8_t *restrict work);

static void enocean_esp3_crc8(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Enocean.esp3_crc8_args.buf;
    uint16_t len = Enocean.esp3_crc8_args.len;

    // The ESP3 CRC-8 (the u8CRC8Table generator) is the cataloge's CRC-8/SMBUS: poly 0x07, MSB-first, init 0,
    // no final XOR.
    Crc.args.params = &PROTOCORE_CRC8_SMBUS;
    Crc.args.data = buf;
    Crc.args.len = len;
    Crc.compute(crc_work);
    Enocean.value = (uint8_t)Crc.value;
}

static void enocean_esp3_parse(uint8_t *restrict work)
{
    const uint8_t *raw = Enocean.esp3_parse_args.raw;
    uint16_t len = Enocean.esp3_parse_args.len;
    protocore_esp3_packet *out = Enocean.esp3_parse_args.out;

    if (!raw || len < 1)
    {
        Enocean.n = 0;
        return;
    }
    if (raw[0] != ESP3_SYNC)
    {
        Enocean.n = -1; // not a telegram start
        return;
    }
    if (len < 6)
    {
        Enocean.n = 0; // need sync + 4-byte header + CRC8H
        return;
    }
    uint16_t data_len = (uint16_t)((raw[1] << 8) | raw[2]);
    uint8_t opt_len = raw[3];
    uint8_t type = raw[4];
    if (data_len > PROTOCORE_ENOCEAN_MAX_DATA)
    {
        Enocean.n = -1; // implausible length -> resynchronize
        return;
    }
    Enocean.esp3_crc8_args.buf = &raw[1];
    Enocean.esp3_crc8_args.len = 4;
    enocean_esp3_crc8(work);
    if (Enocean.value != raw[5])
    {
        Enocean.n = -1; // header CRC mismatch
        return;
    }
    uint32_t total = 6u + data_len + opt_len + 1u;
    if (len < total)
    {
        Enocean.n = 0; // wait for the rest of the telegram
        return;
    }
    Enocean.esp3_crc8_args.buf = &raw[6];
    Enocean.esp3_crc8_args.len = (uint16_t)(data_len + opt_len);
    enocean_esp3_crc8(work);
    if (Enocean.value != raw[6 + data_len + opt_len])
    {
        Enocean.n = -1; // data CRC mismatch
        return;
    }
    if (out)
    {
        out->type = (protocore_esp3_type)type;
        out->data = &raw[6];
        out->data_len = data_len;
        out->opt = &raw[6 + data_len];
        out->opt_len = opt_len;
    }
    Enocean.n = (int)total;
}

static void enocean_esp3_build(uint8_t *restrict work)
{
    protocore_esp3_type type = Enocean.esp3_build_args.type;
    const uint8_t *data = Enocean.esp3_build_args.data;
    uint16_t data_len = Enocean.esp3_build_args.data_len;
    const uint8_t *opt = Enocean.esp3_build_args.opt;
    uint8_t opt_len = Enocean.esp3_build_args.opt_len;
    uint8_t *out = Enocean.esp3_build_args.out;
    uint16_t cap = Enocean.esp3_build_args.cap;

    if (!out || data_len > PROTOCORE_ENOCEAN_MAX_DATA)
    {
        Enocean.u16 = 0;
        return;
    }
    uint32_t total = 6u + data_len + opt_len + 1u;
    if (total > cap)
    {
        Enocean.u16 = 0;
        return;
    }
    out[0] = ESP3_SYNC;
    out[1] = (uint8_t)(data_len >> 8);
    out[2] = (uint8_t)(data_len & 0xFF);
    out[3] = opt_len;
    out[4] = (uint8_t)type;
    Enocean.esp3_crc8_args.buf = &out[1];
    Enocean.esp3_crc8_args.len = 4;
    enocean_esp3_crc8(work);
    out[5] = Enocean.value;
    for (uint16_t i = 0; i < data_len; i++)
    {
        out[6 + i] = data[i];
    }
    for (uint8_t i = 0; i < opt_len; i++)
    {
        out[6 + data_len + i] = opt[i];
    }
    Enocean.esp3_crc8_args.buf = &out[6];
    Enocean.esp3_crc8_args.len = (uint16_t)(data_len + opt_len);
    enocean_esp3_crc8(work);
    out[6 + data_len + opt_len] = Enocean.value;
    Enocean.u16 = (uint16_t)total;
}

static void enocean_erp1_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = Enocean.erp1_parse_args.data;
    uint16_t len = Enocean.erp1_parse_args.len;
    protocore_erp1 *out = Enocean.erp1_parse_args.out;

    if (!data || !out || len < 6) // RORG(1) + sender id(4) + status(1)
    {
        Enocean.ok = PROTO_FALSE;
        return;
    }
    out->rorg = data[0];
    out->payload = (len > 6) ? data + 1 : NULL;
    out->payload_len = (uint8_t)(len - 6);
    const uint8_t *id = data + len - 5; // the 4-octet sender id precedes the status octet
    out->sender_id =
        ((uint32_t)id[0] << 24) | ((uint32_t)id[1] << 16) | ((uint32_t)id[2] << 8) | (uint32_t)id[3]; // big-endian
    out->status = data[len - 1];
    Enocean.ok = PROTO_TRUE;
}

static void enocean_erp1_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Enocean.erp1_build_args.out;
    uint16_t cap = Enocean.erp1_build_args.cap;
    uint8_t rorg = Enocean.erp1_build_args.rorg;
    const uint8_t *payload = Enocean.erp1_build_args.payload;
    uint8_t payload_len = Enocean.erp1_build_args.payload_len;
    uint32_t sender_id = Enocean.erp1_build_args.sender_id;
    uint8_t status = Enocean.erp1_build_args.status;

    if (!out || (payload_len && !payload))
    {
        Enocean.u16 = 0;
        return;
    }
    uint16_t total = (uint16_t)(1 + payload_len + 4 + 1); // RORG + payload + sender id + status
    if (total > cap)
    {
        Enocean.u16 = 0;
        return;
    }
    uint16_t p = 0;
    out[p++] = rorg;
    if (payload_len)
    {
        mem.cpy(out + p, payload, payload_len);
        p = (uint16_t)(p + payload_len);
    }
    out[p++] = (uint8_t)(sender_id >> 24); // 4-octet sender id, big-endian
    out[p++] = (uint8_t)(sender_id >> 16);
    out[p++] = (uint8_t)(sender_id >> 8);
    out[p++] = (uint8_t)sender_id;
    out[p++] = status;
    Enocean.u16 = p;
}

EnoceanNs Enocean = {.esp3_crc8 = enocean_esp3_crc8,
                     .esp3_parse = enocean_esp3_parse,
                     .esp3_build = enocean_esp3_build,
                     .erp1_parse = enocean_erp1_parse,
                     .erp1_build = enocean_erp1_build};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ENOCEAN
