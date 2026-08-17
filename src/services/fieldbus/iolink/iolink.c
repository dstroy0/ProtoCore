// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iolink.c
 * @brief IO-Link (SDCI) data-link message codec (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_IOLINK

#include "services/fieldbus/iolink/iolink.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void iolink_checksum6(uint8_t *restrict work);

static void iolink_mc(uint8_t *restrict work)
{
    (void)work;
    proto_bool read = Iolink.mc_args.read;
    uint8_t channel = Iolink.mc_args.channel;
    uint8_t address = Iolink.mc_args.address;

    Iolink.value = (uint8_t)((read ? IOL_MC_READ : 0u) | ((channel & 0x03u) << 5) | (address & 0x1Fu));
}

static void iolink_mc_is_read(uint8_t *restrict work)
{
    (void)work;
    uint8_t mc = Iolink.mc_is_read_args.mc;

    Iolink.ok = (mc & IOL_MC_READ) != 0;
}

static void iolink_mc_channel(uint8_t *restrict work)
{
    (void)work;
    uint8_t mc = Iolink.mc_channel_args.mc;

    Iolink.value = (uint8_t)((mc >> 5) & 0x03u);
}

static void iolink_mc_address(uint8_t *restrict work)
{
    (void)work;
    uint8_t mc = Iolink.mc_address_args.mc;

    Iolink.value = (uint8_t)(mc & 0x1Fu);
}

static void iolink_ckt(uint8_t *restrict work)
{
    (void)work;
    uint8_t mseq_type = Iolink.ckt_args.mseq_type;
    uint8_t checksum6 = Iolink.ckt_args.checksum6;

    Iolink.value = (uint8_t)(((mseq_type & 0x03u) << 6) | (checksum6 & IOL_CHECK_SUM_MASK));
}

static void iolink_cks(uint8_t *restrict work)
{
    (void)work;
    proto_bool event = Iolink.cks_args.event;
    proto_bool pd_invalid = Iolink.cks_args.pd_invalid;
    uint8_t checksum6 = Iolink.cks_args.checksum6;

    Iolink.value = (uint8_t)((event ? IOL_CKS_EVENT : 0u) | (pd_invalid ? IOL_CKS_PD_INVALID : 0u) |
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

static void iolink_checksum6(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *msg = Iolink.checksum6_args.msg;
    size_t len = Iolink.checksum6_args.len;

    uint8_t x = IOL_CHECKSUM_SEED; // seed XORed with the first octet, then every octet
    for (size_t i = 0; i < len; i++)
    {
        x ^= msg[i];
    }
    Iolink.value = compress6(x);
}

static void iolink_finalize(uint8_t *restrict work)
{
    uint8_t *msg = Iolink.finalize_args.msg;
    size_t len = Iolink.finalize_args.len;
    size_t check_idx = Iolink.finalize_args.check_idx;

    if (!msg || check_idx >= len)
    {
        Iolink.value = 0;
        return;
    }
    msg[check_idx] = (uint8_t)(msg[check_idx] & IOL_CHECK_HIGH_MASK); // zero the checksum field
    Iolink.checksum6_args.msg = msg;
    Iolink.checksum6_args.len = len;
    iolink_checksum6(work);
    uint8_t c6 = Iolink.value;
    msg[check_idx] = (uint8_t)(msg[check_idx] | (c6 & IOL_CHECK_SUM_MASK));
    Iolink.value = msg[check_idx];
}

static void iolink_verify(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *msg = Iolink.verify_args.msg;
    size_t len = Iolink.verify_args.len;
    size_t check_idx = Iolink.verify_args.check_idx;

    if (!msg || check_idx >= len)
    {
        Iolink.ok = PROTO_FALSE;
        return;
    }
    uint8_t x = IOL_CHECKSUM_SEED;
    for (size_t i = 0; i < len; i++)
    {
        x ^= (i == check_idx) ? (uint8_t)(msg[i] & IOL_CHECK_HIGH_MASK) : msg[i];
    }
    Iolink.ok = compress6(x) == (uint8_t)(msg[check_idx] & IOL_CHECK_SUM_MASK);
}

IolinkNs Iolink = {.mc = iolink_mc,
                   .mc_is_read = iolink_mc_is_read,
                   .mc_channel = iolink_mc_channel,
                   .mc_address = iolink_mc_address,
                   .ckt = iolink_ckt,
                   .cks = iolink_cks,
                   .checksum6 = iolink_checksum6,
                   .finalize = iolink_finalize,
                   .verify = iolink_verify};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IOLINK
