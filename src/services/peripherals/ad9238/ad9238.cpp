// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ad9238.cpp
 * @brief AD9238 SPI configuration-port codec - implementation.
 */

#include "services/peripherals/ad9238/ad9238.h"

#if PC_ENABLE_AD9238

bool pc_ad9238_build_instruction(bool read, uint16_t reg_addr, uint8_t nbytes, uint8_t out2[2])
{
    if (!out2 || nbytes == 0 || nbytes > 4 || reg_addr > 0x1FFF)
    {
        return false;
    }
    uint16_t w1w0 = (uint16_t)(nbytes - 1) & 0x3;
    uint16_t word = (uint16_t)((read ? 0x8000u : 0x0000u) | (w1w0 << 13) | (reg_addr & 0x1FFFu));
    out2[0] = (uint8_t)(word >> 8);
    out2[1] = (uint8_t)(word & 0xFF);
    return true;
}

size_t pc_ad9238_build_write(uint16_t reg_addr, uint8_t value, uint8_t *out, size_t cap)
{
    if (!out || cap < 3)
    {
        return 0;
    }
    uint8_t hdr[2];
    if (!pc_ad9238_build_instruction(false, reg_addr, 1, hdr))
    {
        return 0;
    }
    out[0] = hdr[0];
    out[1] = hdr[1];
    out[2] = value;
    return 3;
}

size_t pc_ad9238_build_read(uint16_t reg_addr, uint8_t *out, size_t cap)
{
    if (!out || cap < 2)
    {
        return 0;
    }
    uint8_t hdr[2];
    if (!pc_ad9238_build_instruction(true, reg_addr, 1, hdr))
    {
        return 0;
    }
    out[0] = hdr[0];
    out[1] = hdr[1];
    return 2;
}

size_t pc_ad9238_build_transfer(uint8_t *out, size_t cap)
{
    return pc_ad9238_build_write((uint16_t)Ad9238Reg::AD9238_REG_DEVICE_UPDATE, 0x01, out, cap);
}

#endif // PC_ENABLE_AD9238
