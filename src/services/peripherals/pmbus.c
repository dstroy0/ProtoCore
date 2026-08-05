// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pmbus.c
 * @brief PMBus 1.3 command set - implementation. See pmbus.h.
 *
 * The exponent in every PMBus encoding is a power of two, so each decode is a shift in one
 * direction or the other and never a divide. The intermediate is 64-bit because a mantissa scaled
 * to micro-units and then shifted up leaves the 32-bit range a positive exponent can reach.
 */

#include "services/peripherals/pmbus.h"
#include "protocore_config.h"

#if PC_ENABLE_PMBUS

#include "services/peripherals/smbus.h"

// Micro-units per unit: every value this returns is scaled by it.
#define PC_PMBUS_MICRO 1000000

// A decoded value is refused past this, which is what an int32 of micro-units holds.
#define PC_PMBUS_MAX_MICRO 2147483647LL
#define PC_PMBUS_MIN_MICRO (-2147483647LL - 1)

uint8_t pc_pmbus_vout_mode_kind(uint8_t vout_mode)
{
    return (uint8_t)((vout_mode >> 5) & 0x07u);
}

int8_t pc_pmbus_vout_exponent(uint8_t vout_mode)
{
    uint8_t e = (uint8_t)(vout_mode & 0x1Fu);
    // Sign-extend from 5 bits: bit 4 set means the value is negative.
    return (int8_t)((e & 0x10u) != 0 ? (int8_t)(e | 0xE0u) : (int8_t)e);
}

int16_t pc_pmbus_l11_mantissa(uint16_t word)
{
    uint16_t y = (uint16_t)(word & 0x07FFu);
    // Sign-extend from 11 bits: bit 10 set means the value is negative.
    return (int16_t)((y & 0x0400u) != 0 ? (int16_t)(y | 0xF800u) : (int16_t)y);
}

int8_t pc_pmbus_l11_exponent(uint16_t word)
{
    uint8_t n = (uint8_t)((word >> 11) & 0x1Fu);
    return (int8_t)((n & 0x10u) != 0 ? (int8_t)(n | 0xE0u) : (int8_t)n);
}

// Scale @p mantissa by 2^@p exponent into micro-units, refusing anything that leaves an int32.
static int32_t scale_micro(int64_t mantissa, int8_t exponent)
{
    int64_t v = mantissa * PC_PMBUS_MICRO;
    if (exponent < 0)
    {
        int32_t sh = -(int32_t)exponent;
        if (sh >= 63)
        {
            return 0;
        }
        v >>= sh;
    }
    else if (exponent > 0)
    {
        if (exponent >= 32)
        {
            return PC_PMBUS_INVALID;
        }
        // Refuse rather than wrap: the shift would carry the value out of the returned range.
        int64_t limit = PC_PMBUS_MAX_MICRO >> exponent;
        if (v > limit || v < (PC_PMBUS_MIN_MICRO >> exponent))
        {
            return PC_PMBUS_INVALID;
        }
        v <<= exponent;
    }
    if (v > PC_PMBUS_MAX_MICRO || v < PC_PMBUS_MIN_MICRO)
    {
        return PC_PMBUS_INVALID;
    }
    return (int32_t)v;
}

int32_t pc_pmbus_linear11_micro(uint16_t word)
{
    return scale_micro((int64_t)pc_pmbus_l11_mantissa(word), pc_pmbus_l11_exponent(word));
}

// The mantissa @p micro takes at exponent @p n: micro / (10^6 * 2^n), the shift going whichever
// way the sign of @p n calls for.
static int64_t l11_mantissa_at(int32_t micro, int32_t n)
{
    if (n >= 0)
    {
        return (int64_t)micro / ((int64_t)PC_PMBUS_MICRO << n);
    }
    return ((int64_t)micro << (-n)) / PC_PMBUS_MICRO;
}

uint16_t pc_pmbus_linear11_encode(int32_t micro)
{
    if (micro == 0)
    {
        return 0;
    }
    // Take the exponent as low as it goes while the mantissa still fits its 11 signed bits, which
    // is what keeps the most significant bits of the value.
    int32_t n = 15;
    while (n > -16)
    {
        int64_t next = l11_mantissa_at(micro, n - 1);
        if (next > 1023 || next < -1024)
        {
            break;
        }
        n--;
    }
    int64_t y = l11_mantissa_at(micro, n);
    if (y > 1023)
    {
        y = 1023;
    }
    if (y < -1024)
    {
        y = -1024;
    }
    return (uint16_t)((((uint16_t)((uint8_t)n & 0x1Fu)) << 11) | ((uint16_t)y & 0x07FFu));
}

int32_t pc_pmbus_linear16_micro(uint16_t word, int8_t exponent)
{
    return scale_micro((int64_t)word, exponent);
}

uint16_t pc_pmbus_linear16_encode(int32_t micro, int8_t exponent)
{
    if (micro <= 0)
    {
        return 0;
    }
    int64_t v = (int64_t)micro;
    // Undo the exponent the part applies, then take the value out of micro-units.
    if (exponent < 0)
    {
        int32_t sh = -(int32_t)exponent;
        if (sh >= 40)
        {
            return 0;
        }
        v <<= sh;
    }
    else if (exponent > 0)
    {
        v >>= exponent;
    }
    int64_t y = v / PC_PMBUS_MICRO;
    return y > 0xFFFF ? 0xFFFFu : (uint16_t)y;
}

int32_t pc_pmbus_direct_micro(uint16_t word, int16_t m, int16_t b, int8_t r)
{
    if (m == 0)
    {
        return PC_PMBUS_INVALID;
    }
    // X = (Y * 10^-R - b) / m, carried in micro-units so the divide keeps its precision.
    int64_t v = (int64_t)(int16_t)word * PC_PMBUS_MICRO;
    int32_t k = r;
    while (k > 0)
    {
        v /= 10;
        k--;
    }
    while (k < 0)
    {
        v *= 10;
        k++;
    }
    v -= (int64_t)b * PC_PMBUS_MICRO;
    v /= m;
    if (v > PC_PMBUS_MAX_MICRO || v < PC_PMBUS_MIN_MICRO)
    {
        return PC_PMBUS_INVALID;
    }
    return (int32_t)v;
}

#if PROTOCORE_HOT || PC_PLATFORM_HAS_BUS

proto_bool pc_pmbus_begin(void)
{
    return pc_smbus_begin();
}

proto_bool pc_pmbus_set_page(uint8_t addr, uint8_t page)
{
    return pc_smbus_write_byte(addr, PC_PMBUS_PAGE, page);
}

proto_bool pc_pmbus_read_vout_mode(uint8_t addr, uint8_t *out)
{
    return pc_smbus_read_byte(addr, PC_PMBUS_VOUT_MODE, out);
}

proto_bool pc_pmbus_read_linear11(uint8_t addr, uint8_t cmd, int32_t *micro)
{
    if (micro == NULL)
    {
        return PROTO_FALSE;
    }
    uint16_t w = 0;
    if (!pc_smbus_read_word(addr, cmd, &w))
    {
        return PROTO_FALSE;
    }
    *micro = pc_pmbus_linear11_micro(w);
    return *micro != PC_PMBUS_INVALID;
}

proto_bool pc_pmbus_read_linear16(uint8_t addr, uint8_t cmd, int8_t exponent, int32_t *micro)
{
    if (micro == NULL)
    {
        return PROTO_FALSE;
    }
    uint16_t w = 0;
    if (!pc_smbus_read_word(addr, cmd, &w))
    {
        return PROTO_FALSE;
    }
    *micro = pc_pmbus_linear16_micro(w, exponent);
    return *micro != PC_PMBUS_INVALID;
}

proto_bool pc_pmbus_write_linear16(uint8_t addr, uint8_t cmd, int8_t exponent, int32_t micro)
{
    return pc_smbus_write_word(addr, cmd, pc_pmbus_linear16_encode(micro, exponent));
}

proto_bool pc_pmbus_status_byte(uint8_t addr, uint8_t *out)
{
    return pc_smbus_read_byte(addr, PC_PMBUS_STATUS_BYTE, out);
}

proto_bool pc_pmbus_status_word(uint8_t addr, uint16_t *out)
{
    return pc_smbus_read_word(addr, PC_PMBUS_STATUS_WORD, out);
}

proto_bool pc_pmbus_clear_faults(uint8_t addr)
{
    return pc_smbus_send_byte(addr, PC_PMBUS_CLEAR_FAULTS);
}

proto_bool pc_pmbus_read_mfr_string(uint8_t addr, uint8_t cmd, uint8_t *out, size_t cap, size_t *len)
{
    return pc_smbus_read_block(addr, cmd, out, cap, len);
}

#else // host build: no bus. The encodings above are host-tested.

proto_bool pc_pmbus_begin(void)
{
    return PROTO_FALSE;
}

proto_bool pc_pmbus_set_page(uint8_t addr, uint8_t page)
{
    (void)addr;
    (void)page;
    return PROTO_FALSE;
}

proto_bool pc_pmbus_read_vout_mode(uint8_t addr, uint8_t *out)
{
    (void)addr;
    (void)out;
    return PROTO_FALSE;
}

proto_bool pc_pmbus_read_linear11(uint8_t addr, uint8_t cmd, int32_t *micro)
{
    (void)addr;
    (void)cmd;
    (void)micro;
    return PROTO_FALSE;
}

proto_bool pc_pmbus_read_linear16(uint8_t addr, uint8_t cmd, int8_t exponent, int32_t *micro)
{
    (void)addr;
    (void)cmd;
    (void)exponent;
    (void)micro;
    return PROTO_FALSE;
}

proto_bool pc_pmbus_write_linear16(uint8_t addr, uint8_t cmd, int8_t exponent, int32_t micro)
{
    (void)addr;
    (void)cmd;
    (void)exponent;
    (void)micro;
    return PROTO_FALSE;
}

proto_bool pc_pmbus_status_byte(uint8_t addr, uint8_t *out)
{
    (void)addr;
    (void)out;
    return PROTO_FALSE;
}

proto_bool pc_pmbus_status_word(uint8_t addr, uint16_t *out)
{
    (void)addr;
    (void)out;
    return PROTO_FALSE;
}

proto_bool pc_pmbus_clear_faults(uint8_t addr)
{
    (void)addr;
    return PROTO_FALSE;
}

proto_bool pc_pmbus_read_mfr_string(uint8_t addr, uint8_t cmd, uint8_t *out, size_t cap, size_t *len)
{
    (void)addr;
    (void)cmd;
    (void)out;
    (void)cap;
    (void)len;
    return PROTO_FALSE;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_PMBUS
