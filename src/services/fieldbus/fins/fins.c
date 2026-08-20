// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fins.c
 * @brief Omron FINS command/response builder + parser (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_FINS

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/fins/fins.h"

PROTOCORE_BEGIN_DECLS

static size_t write_header(uint8_t *buf, const FinsHeader *h)
{
    buf[0] = h->icf;
    buf[1] = h->rsv;
    buf[2] = h->gct;
    buf[3] = h->dna;
    buf[4] = h->da1;
    buf[5] = h->da2;
    buf[6] = h->sna;
    buf[7] = h->sa1;
    buf[8] = h->sa2;
    buf[9] = h->sid;
    return FINS_HEADER_SIZE;
}

static void read_header(const uint8_t *buf, FinsHeader *h)
{
    h->icf = buf[0];
    h->rsv = buf[1];
    h->gct = buf[2];
    h->dna = buf[3];
    h->da1 = buf[4];
    h->da2 = buf[5];
    h->sna = buf[6];
    h->sa1 = buf[7];
    h->sa2 = buf[8];
    h->sid = buf[9];
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_fins_build_command(uint8_t *restrict work);

void protocore_fins_build_command(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = FinsV.build_command_args.buf;
    size_t cap = FinsV.build_command_args.cap;
    const FinsHeader *h = FinsV.build_command_args.h;
    uint8_t mrc = FinsV.build_command_args.mrc;
    uint8_t src = FinsV.build_command_args.src;
    const uint8_t *params = FinsV.build_command_args.params;
    size_t params_len = FinsV.build_command_args.params_len;

    if (!buf || !h || (params_len && !params))
    {
        FinsV.n = 0;
        return;
    }
    size_t total = FINS_HEADER_SIZE + 2 + params_len;
    if (total > cap)
    {
        FinsV.n = 0;
        return;
    }
    size_t p = write_header(buf, h);
    buf[p++] = mrc;
    buf[p++] = src;
    if (params_len)
    {
        mem.cpy(buf + p, params, params_len);
        p += params_len;
    }
    FinsV.n = p;
}

void protocore_fins_build_memory_area_read(uint8_t *restrict work)
{
    uint8_t *buf = FinsV.build_memory_area_read_args.buf;
    size_t cap = FinsV.build_memory_area_read_args.cap;
    const FinsHeader *h = FinsV.build_memory_area_read_args.h;
    uint8_t area = FinsV.build_memory_area_read_args.area;
    uint16_t address = FinsV.build_memory_area_read_args.address;
    uint8_t bit = FinsV.build_memory_area_read_args.bit;
    uint16_t count = FinsV.build_memory_area_read_args.count;

    uint8_t params[6];
    params[0] = area;
    params[1] = (uint8_t)(address >> 8);
    params[2] = (uint8_t)(address & 0xFF);
    params[3] = bit;
    params[4] = (uint8_t)(count >> 8);
    params[5] = (uint8_t)(count & 0xFF);
    FinsV.build_command_args.buf = buf;
    FinsV.build_command_args.cap = cap;
    FinsV.build_command_args.h = h;
    FinsV.build_command_args.mrc = FINS_MRC_MEMORY_AREA;
    FinsV.build_command_args.src = FINS_SRC_MEMORY_AREA_READ;
    FinsV.build_command_args.params = params;
    FinsV.build_command_args.params_len = sizeof(params);
    protocore_fins_build_command(work);
}

void protocore_fins_build_memory_area_write(uint8_t *restrict work)
{
    uint8_t *buf = FinsV.build_memory_area_write_args.buf;
    size_t cap = FinsV.build_memory_area_write_args.cap;
    const FinsHeader *h = FinsV.build_memory_area_write_args.h;
    uint8_t area = FinsV.build_memory_area_write_args.area;
    uint16_t address = FinsV.build_memory_area_write_args.address;
    uint8_t bit = FinsV.build_memory_area_write_args.bit;
    uint16_t count = FinsV.build_memory_area_write_args.count;
    const uint8_t *data = FinsV.build_memory_area_write_args.data;
    size_t data_len = FinsV.build_memory_area_write_args.data_len;

    if (data_len && !data)
    {
        FinsV.n = 0;
        return;
    }
    uint8_t prefix[6];
    prefix[0] = area;
    prefix[1] = (uint8_t)(address >> 8);
    prefix[2] = (uint8_t)(address & 0xFF);
    prefix[3] = bit;
    prefix[4] = (uint8_t)(count >> 8);
    prefix[5] = (uint8_t)(count & 0xFF);
    // The command builder lays down header + MRC + SRC + the 6-octet prefix; the write data follows it.
    FinsV.build_command_args.buf = buf;
    FinsV.build_command_args.cap = cap;
    FinsV.build_command_args.h = h;
    FinsV.build_command_args.mrc = FINS_MRC_MEMORY_AREA;
    FinsV.build_command_args.src = FINS_SRC_MEMORY_AREA_WRITE;
    FinsV.build_command_args.params = prefix;
    FinsV.build_command_args.params_len = sizeof(prefix);
    protocore_fins_build_command(work);
    size_t n = FinsV.n;
    if (!n)
    {
        FinsV.n = 0; // header + prefix did not fit
        return;
    }
    if (data_len)
    {
        if (n + data_len > cap)
        {
            FinsV.n = 0; // the write data does not fit
            return;
        }
        mem.cpy(buf + n, data, data_len);
    }
    FinsV.n = n + data_len;
}

void protocore_fins_build_run(uint8_t *restrict work)
{
    uint8_t *buf = FinsV.build_run_args.buf;
    size_t cap = FinsV.build_run_args.cap;
    const FinsHeader *h = FinsV.build_run_args.h;
    FinsRunMode mode = FinsV.build_run_args.mode;

    uint8_t params[3];
    params[0] = 0xFF;          // program number 0xFFFF (all programs)
    params[1] = 0xFF;          //
    params[2] = (uint8_t)mode; // wire byte: 0x02 MONITOR / 0x04 RUN
    FinsV.build_command_args.buf = buf;
    FinsV.build_command_args.cap = cap;
    FinsV.build_command_args.h = h;
    FinsV.build_command_args.mrc = FINS_MRC_OPERATING_MODE;
    FinsV.build_command_args.src = FINS_SRC_RUN;
    FinsV.build_command_args.params = params;
    FinsV.build_command_args.params_len = sizeof(params);
    protocore_fins_build_command(work);
}

void protocore_fins_build_stop(uint8_t *restrict work)
{
    uint8_t *buf = FinsV.build_stop_args.buf;
    size_t cap = FinsV.build_stop_args.cap;
    const FinsHeader *h = FinsV.build_stop_args.h;

    FinsV.build_command_args.buf = buf;
    FinsV.build_command_args.cap = cap;
    FinsV.build_command_args.h = h;
    FinsV.build_command_args.mrc = FINS_MRC_OPERATING_MODE;
    FinsV.build_command_args.src = FINS_SRC_STOP;
    FinsV.build_command_args.params = NULL;
    FinsV.build_command_args.params_len = 0;
    protocore_fins_build_command(work);
}

void protocore_fins_parse_command(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = FinsV.parse_command_args.buf;
    size_t len = FinsV.parse_command_args.len;
    FinsCommand *out = FinsV.parse_command_args.out;

    if (!buf || !out || len < FINS_HEADER_SIZE + 2)
    {
        FinsV.ok = PROTO_FALSE;
        return;
    }
    read_header(buf, &out->header);
    out->mrc = buf[FINS_HEADER_SIZE];
    out->src = buf[FINS_HEADER_SIZE + 1];
    out->params = buf + FINS_HEADER_SIZE + 2;
    out->params_len = len - (FINS_HEADER_SIZE + 2);
    FinsV.ok = PROTO_TRUE;
}

void protocore_fins_parse_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = FinsV.parse_response_args.buf;
    size_t len = FinsV.parse_response_args.len;
    FinsResponse *out = FinsV.parse_response_args.out;

    if (!buf || !out || len < FINS_HEADER_SIZE + 4) // header + MRC + SRC + MRES + SRES
    {
        FinsV.ok = PROTO_FALSE;
        return;
    }
    read_header(buf, &out->header);
    out->mrc = buf[FINS_HEADER_SIZE];
    out->src = buf[FINS_HEADER_SIZE + 1];
    out->mres = buf[FINS_HEADER_SIZE + 2];
    out->sres = buf[FINS_HEADER_SIZE + 3];
    out->data = buf + FINS_HEADER_SIZE + 4;
    out->data_len = len - (FINS_HEADER_SIZE + 4);
    FinsV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
FinsVars FinsV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FINS
