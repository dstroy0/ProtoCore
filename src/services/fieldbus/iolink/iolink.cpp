// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iolink.cpp
 * @brief IO-Link (SDCI) data-link message codec (pure, host-tested).
 */

#include "services/fieldbus/iolink/iolink.h"

#if PC_ENABLE_IOLINK

uint8_t pc_iol_mc(bool read, uint8_t channel, uint8_t address)
{
    return (uint8_t)((read ? IOL_MC_READ : 0u) | ((channel & 0x03u) << 5) | (address & 0x1Fu));
}

bool pc_iol_mc_is_read(uint8_t mc)
{
    return (mc & IOL_MC_READ) != 0;
}

uint8_t pc_iol_mc_channel(uint8_t mc)
{
    return (uint8_t)((mc >> 5) & 0x03u);
}

uint8_t pc_iol_mc_address(uint8_t mc)
{
    return (uint8_t)(mc & 0x1Fu);
}

uint8_t pc_iol_ckt(uint8_t mseq_type, uint8_t checksum6)
{
    return (uint8_t)(((mseq_type & 0x03u) << 6) | (checksum6 & IOL_CHECK_SUM_MASK));
}

uint8_t pc_iol_cks(bool event, bool pd_invalid, uint8_t checksum6)
{
    return (uint8_t)((event ? IOL_CKS_EVENT : 0u) | (pd_invalid ? IOL_CKS_PD_INVALID : 0u) |
                     (checksum6 & IOL_CHECK_SUM_MASK));
}

// Compress the 8-bit XOR result to 6 bits per IO-Link spec v1.1.4 Annex A.1.6 equation (A.1).
static uint8_t compress6(uint8_t b)
{
    uint8_t b0 = b & 1u, b1 = (b >> 1) & 1u, b2 = (b >> 2) & 1u, b3 = (b >> 3) & 1u;
    uint8_t b4 = (b >> 4) & 1u, b5 = (b >> 5) & 1u, b6 = (b >> 6) & 1u, b7 = (b >> 7) & 1u;
    uint8_t d5 = (uint8_t)(b7 ^ b5 ^ b3 ^ b1);
    uint8_t d4 = (uint8_t)(b6 ^ b4 ^ b2 ^ b0);
    uint8_t d3 = (uint8_t)(b7 ^ b6);
    uint8_t d2 = (uint8_t)(b5 ^ b4);
    uint8_t d1 = (uint8_t)(b3 ^ b2);
    uint8_t d0 = (uint8_t)(b1 ^ b0);
    return (uint8_t)((d5 << 5) | (d4 << 4) | (d3 << 3) | (d2 << 2) | (d1 << 1) | d0);
}

uint8_t pc_iol_checksum6(const uint8_t *msg, size_t len)
{
    uint8_t x = IOL_CHECKSUM_SEED; // seed XORed with the first octet, then every octet
    for (size_t i = 0; i < len; i++)
    {
        x ^= msg[i];
    }
    return compress6(x);
}

uint8_t pc_iol_finalize(uint8_t *msg, size_t len, size_t check_idx)
{
    if (!msg || check_idx >= len)
    {
        return 0;
    }
    msg[check_idx] = (uint8_t)(msg[check_idx] & IOL_CHECK_HIGH_MASK); // zero the checksum field
    uint8_t c6 = pc_iol_checksum6(msg, len);
    msg[check_idx] = (uint8_t)(msg[check_idx] | (c6 & IOL_CHECK_SUM_MASK));
    return msg[check_idx];
}

bool pc_iol_verify(const uint8_t *msg, size_t len, size_t check_idx)
{
    if (!msg || check_idx >= len)
    {
        return false;
    }
    uint8_t x = IOL_CHECKSUM_SEED;
    for (size_t i = 0; i < len; i++)
    {
        x ^= (i == check_idx) ? (uint8_t)(msg[i] & IOL_CHECK_HIGH_MASK) : msg[i];
    }
    return compress6(x) == (uint8_t)(msg[check_idx] & IOL_CHECK_SUM_MASK);
}

#endif // PC_ENABLE_IOLINK
