// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ad9238.c
 * @brief AD9238 SPI configuration-port codec - implementation.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_AD9238

#include "server/peripherals/ad9238/ad9238.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_ad9238_build_instruction(uint8_t *restrict work)
{
    (void)work;
    proto_bool read = Ad9238V.build_instruction_args.read;
    uint16_t reg_addr = Ad9238V.build_instruction_args.reg_addr;
    uint8_t nbytes = Ad9238V.build_instruction_args.nbytes;
    uint8_t *out2 = Ad9238V.build_instruction_args.out2;

    if (!out2 || nbytes == 0 || nbytes > 4 || reg_addr > 0x1FFF)
    {
        Ad9238V.ok = PROTO_FALSE;
        return;
    }
    uint16_t w1w0 = (uint16_t)(nbytes - 1) & 0x3;
    uint16_t word = (uint16_t)((read ? 0x8000u : 0x0000u) | (w1w0 << 13) | (reg_addr & 0x1FFFu));
    out2[0] = (uint8_t)(word >> 8);
    out2[1] = (uint8_t)(word & 0xFF);
    Ad9238V.ok = PROTO_TRUE;
}

void protocore_ad9238_build_write(uint8_t *restrict work)
{
    (void)work;
    uint16_t reg_addr = Ad9238V.build_write_args.reg_addr;
    uint8_t value = Ad9238V.build_write_args.value;
    uint8_t *out = Ad9238V.build_write_args.out;
    size_t cap = Ad9238V.build_write_args.cap;

    if (!out || cap < 3)
    {
        Ad9238V.n = 0;
        return;
    }
    uint8_t hdr[2];
    Ad9238V.build_instruction_args.read = PROTO_FALSE;
    Ad9238V.build_instruction_args.reg_addr = reg_addr;
    Ad9238V.build_instruction_args.nbytes = 1;
    Ad9238V.build_instruction_args.out2 = hdr;
    protocore_ad9238_build_instruction(work);
    if (!Ad9238V.ok)
    {
        Ad9238V.n = 0;
        return;
    }
    out[0] = hdr[0];
    out[1] = hdr[1];
    out[2] = value;
    Ad9238V.n = 3;
}

void protocore_ad9238_build_read(uint8_t *restrict work)
{
    (void)work;
    uint16_t reg_addr = Ad9238V.build_read_args.reg_addr;
    uint8_t *out = Ad9238V.build_read_args.out;
    size_t cap = Ad9238V.build_read_args.cap;

    if (!out || cap < 2)
    {
        Ad9238V.n = 0;
        return;
    }
    uint8_t hdr[2];
    Ad9238V.build_instruction_args.read = PROTO_TRUE;
    Ad9238V.build_instruction_args.reg_addr = reg_addr;
    Ad9238V.build_instruction_args.nbytes = 1;
    Ad9238V.build_instruction_args.out2 = hdr;
    protocore_ad9238_build_instruction(work);
    if (!Ad9238V.ok)
    {
        Ad9238V.n = 0;
        return;
    }
    out[0] = hdr[0];
    out[1] = hdr[1];
    Ad9238V.n = 2;
}

void protocore_ad9238_build_transfer(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Ad9238V.build_transfer_args.out;
    size_t cap = Ad9238V.build_transfer_args.cap;

    Ad9238V.build_write_args.reg_addr = (uint16_t)AD9238_REG_DEVICE_UPDATE;
    Ad9238V.build_write_args.value = 0x01;
    Ad9238V.build_write_args.out = out;
    Ad9238V.build_write_args.cap = cap;
    protocore_ad9238_build_write(work);
}

/** @brief The operands and the outcome. */
Ad9238Vars Ad9238V;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AD9238
