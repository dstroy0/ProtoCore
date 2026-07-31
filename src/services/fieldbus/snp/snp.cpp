// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snp.cpp
 * @brief GE Fanuc SNP serial frame codec (see snp.h).
 */

#include "services/fieldbus/snp/snp.h"

#if PC_ENABLE_SNP

#include <string.h>

uint8_t pc_snp_bcc(const uint8_t *bytes, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum;
}

size_t pc_snp_build(uint8_t control, const uint8_t *data, size_t data_len, uint8_t *out, size_t cap)
{
    if (!out || (data_len && !data) || data_len > 255)
    {
        return 0;
    }
    size_t n = 2 + data_len + 1; // control + length + data + BCC
    if (n > cap)
    {
        return 0;
    }
    out[0] = control;
    out[1] = (uint8_t)data_len;
    if (data_len)
    {
        memcpy(out + 2, data, data_len);
    }
    out[2 + data_len] = pc_snp_bcc(out, 2 + data_len); // BCC over control..last data
    return n;
}

bool pc_snp_parse(const uint8_t *frame, size_t len, SnpFrame *out)
{
    if (!frame || !out || len < 3) // control + length + BCC
    {
        return false;
    }
    uint8_t data_len = frame[1];
    size_t expect = 2 + (size_t)data_len + 1;
    if (len < expect)
    {
        return false;
    }
    if (pc_snp_bcc(frame, 2 + data_len) != frame[2 + data_len])
    {
        return false;
    }
    out->control = frame[0];
    out->data = data_len ? (frame + 2) : nullptr;
    out->data_len = data_len;
    return true;
}

#endif // PC_ENABLE_SNP
