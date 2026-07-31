// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fins.cpp
 * @brief Omron FINS command/response builder + parser (pure, host-tested).
 */

#include "services/fieldbus/fins/fins.h"

#if PC_ENABLE_FINS

#include <string.h>

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

size_t pc_fins_build_command(uint8_t *buf, size_t cap, const FinsHeader *h, uint8_t mrc, uint8_t src,
                             const uint8_t *params, size_t params_len)
{
    if (!buf || !h || (params_len && !params))
    {
        return 0;
    }
    size_t total = FINS_HEADER_SIZE + 2 + params_len;
    if (total > cap)
    {
        return 0;
    }
    size_t p = write_header(buf, h);
    buf[p++] = mrc;
    buf[p++] = src;
    if (params_len)
    {
        memcpy(buf + p, params, params_len);
        p += params_len;
    }
    return p;
}

size_t pc_fins_build_memory_area_read(uint8_t *buf, size_t cap, const FinsHeader *h, uint8_t area, uint16_t address,
                                      uint8_t bit, uint16_t count)
{
    uint8_t params[6];
    params[0] = area;
    params[1] = (uint8_t)(address >> 8);
    params[2] = (uint8_t)(address & 0xFF);
    params[3] = bit;
    params[4] = (uint8_t)(count >> 8);
    params[5] = (uint8_t)(count & 0xFF);
    return pc_fins_build_command(buf, cap, h, FINS_MRC_MEMORY_AREA, FINS_SRC_MEMORY_AREA_READ, params, sizeof(params));
}

size_t pc_fins_build_memory_area_write(uint8_t *buf, size_t cap, const FinsHeader *h, uint8_t area, uint16_t address,
                                       uint8_t bit, uint16_t count, const uint8_t *data, size_t data_len)
{
    if (data_len && !data)
    {
        return 0;
    }
    uint8_t prefix[6];
    prefix[0] = area;
    prefix[1] = (uint8_t)(address >> 8);
    prefix[2] = (uint8_t)(address & 0xFF);
    prefix[3] = bit;
    prefix[4] = (uint8_t)(count >> 8);
    prefix[5] = (uint8_t)(count & 0xFF);
    // The command builder lays down header + MRC + SRC + the 6-octet prefix; the write data follows it.
    size_t n =
        pc_fins_build_command(buf, cap, h, FINS_MRC_MEMORY_AREA, FINS_SRC_MEMORY_AREA_WRITE, prefix, sizeof(prefix));
    if (!n)
    {
        return 0; // header + prefix did not fit
    }
    if (data_len)
    {
        if (n + data_len > cap)
        {
            return 0; // the write data does not fit
        }
        memcpy(buf + n, data, data_len);
    }
    return n + data_len;
}

size_t pc_fins_build_run(uint8_t *buf, size_t cap, const FinsHeader *h, FinsRunMode mode)
{
    uint8_t params[3];
    params[0] = 0xFF;          // program number 0xFFFF (all programs)
    params[1] = 0xFF;          //
    params[2] = (uint8_t)mode; // wire byte: 0x02 MONITOR / 0x04 RUN
    return pc_fins_build_command(buf, cap, h, FINS_MRC_OPERATING_MODE, FINS_SRC_RUN, params, sizeof(params));
}

size_t pc_fins_build_stop(uint8_t *buf, size_t cap, const FinsHeader *h)
{
    return pc_fins_build_command(buf, cap, h, FINS_MRC_OPERATING_MODE, FINS_SRC_STOP, nullptr, 0);
}

bool pc_fins_parse_command(const uint8_t *buf, size_t len, FinsCommand *out)
{
    if (!buf || !out || len < FINS_HEADER_SIZE + 2)
    {
        return false;
    }
    read_header(buf, &out->header);
    out->mrc = buf[FINS_HEADER_SIZE];
    out->src = buf[FINS_HEADER_SIZE + 1];
    out->params = buf + FINS_HEADER_SIZE + 2;
    out->params_len = len - (FINS_HEADER_SIZE + 2);
    return true;
}

bool pc_fins_parse_response(const uint8_t *buf, size_t len, FinsResponse *out)
{
    if (!buf || !out || len < FINS_HEADER_SIZE + 4) // header + MRC + SRC + MRES + SRES
    {
        return false;
    }
    read_header(buf, &out->header);
    out->mrc = buf[FINS_HEADER_SIZE];
    out->src = buf[FINS_HEADER_SIZE + 1];
    out->mres = buf[FINS_HEADER_SIZE + 2];
    out->sres = buf[FINS_HEADER_SIZE + 3];
    out->data = buf + FINS_HEADER_SIZE + 4;
    out->data_len = len - (FINS_HEADER_SIZE + 4);
    return true;
}

#endif // PC_ENABLE_FINS
