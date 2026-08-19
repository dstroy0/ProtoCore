// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pmbus.c
 * @brief PMBus 1.3 command set - implementation. See pmbus.h.
 *
 * The exponent in every PMBus encoding is a power of two, so each decode is a shift in one
 * direction or the other and never a divide. The intermediate is 64-bit because a mantissa scaled
 * to micro-units and then shifted up leaves the 32-bit range a positive exponent can reach.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_PMBUS

#include "server/peripherals/pmbus/pmbus.h"

#include "server/peripherals/smbus/smbus.h"

PROTOCORE_BEGIN_DECLS

// Micro-units per unit: every value this returns is scaled by it.
#define PROTOCORE_PMBUS_MICRO 1000000

// A decoded value is refused past this, which is what an int32 of micro-units holds.
#define PROTOCORE_PMBUS_MAX_MICRO 2147483647LL
#define PROTOCORE_PMBUS_MIN_MICRO (-2147483647LL - 1)

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void pmbus_l11_exponent(uint8_t *restrict work);
static void pmbus_l11_mantissa(uint8_t *restrict work);
static void pmbus_linear11_micro(uint8_t *restrict work);
static void pmbus_linear16_encode(uint8_t *restrict work);
static void pmbus_linear16_micro(uint8_t *restrict work);

static void pmbus_vout_mode_kind(uint8_t *restrict work)
{
    (void)work;
    uint8_t vout_mode = Pmbus.vout_mode_kind_args.vout_mode;

    Pmbus.kind = (uint8_t)((vout_mode >> 5) & 0x07u);
}

static void pmbus_vout_exponent(uint8_t *restrict work)
{
    (void)work;
    uint8_t vout_mode = Pmbus.vout_exponent_args.vout_mode;

    uint8_t e = (uint8_t)(vout_mode & 0x1Fu);
    // Sign-extend from 5 bits: bit 4 set means the value is negative.
    Pmbus.exp = (int8_t)((e & 0x10u) != 0 ? (int8_t)(e | 0xE0u) : (int8_t)e);
}

static void pmbus_l11_mantissa(uint8_t *restrict work)
{
    (void)work;
    uint16_t word = Pmbus.l11_mantissa_args.word;

    uint16_t y = (uint16_t)(word & 0x07FFu);
    // Sign-extend from 11 bits: bit 10 set means the value is negative.
    Pmbus.mantissa = (int16_t)((y & 0x0400u) != 0 ? (int16_t)(y | 0xF800u) : (int16_t)y);
}

static void pmbus_l11_exponent(uint8_t *restrict work)
{
    (void)work;
    uint16_t word = Pmbus.l11_exponent_args.word;

    uint8_t n = (uint8_t)((word >> 11) & 0x1Fu);
    Pmbus.exp = (int8_t)((n & 0x10u) != 0 ? (int8_t)(n | 0xE0u) : (int8_t)n);
}

// Scale @p mantissa by 2^@p exponent into micro-units, refusing anything that leaves an int32.
static int32_t scale_micro(int64_t mantissa, int8_t exponent)
{
    int64_t v = mantissa * PROTOCORE_PMBUS_MICRO;
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
            return PROTOCORE_PMBUS_INVALID;
        }
        // Refuse rather than wrap: the shift would carry the value out of the returned range.
        int64_t limit = PROTOCORE_PMBUS_MAX_MICRO >> exponent;
        if (v > limit || v < (PROTOCORE_PMBUS_MIN_MICRO >> exponent))
        {
            return PROTOCORE_PMBUS_INVALID;
        }
        v <<= exponent;
    }
    if (v > PROTOCORE_PMBUS_MAX_MICRO || v < PROTOCORE_PMBUS_MIN_MICRO)
    {
        return PROTOCORE_PMBUS_INVALID;
    }
    return (int32_t)v;
}

static void pmbus_linear11_micro(uint8_t *restrict work)
{
    (void)work;
    uint16_t word = Pmbus.linear11_micro_args.word;

    // Each half is captured before the other runs: both report through the one namespace, so
    // reading them in a single expression would read the second twice.
    Pmbus.l11_mantissa_args.word = word;
    pmbus_l11_mantissa(work);
    const int16_t m = Pmbus.mantissa;
    Pmbus.l11_exponent_args.word = word;
    pmbus_l11_exponent(work);
    const int8_t e = Pmbus.exp;
    Pmbus.micro = scale_micro((int64_t)m, e);
}

// The mantissa @p micro takes at exponent @p n: micro / (10^6 * 2^n), the shift going whichever
// way the sign of @p n calls for.
static int64_t l11_mantissa_at(int32_t micro, int32_t n)
{
    if (n >= 0)
    {
        return (int64_t)micro / ((int64_t)PROTOCORE_PMBUS_MICRO << n);
    }
    return ((int64_t)micro << (-n)) / PROTOCORE_PMBUS_MICRO;
}

static void pmbus_linear11_encode(uint8_t *restrict work)
{
    (void)work;
    int32_t micro = Pmbus.linear11_encode_args.micro;

    if (micro == 0)
    {
        Pmbus.word = 0;
        return;
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
    Pmbus.word = (uint16_t)((((uint16_t)((uint8_t)n & 0x1Fu)) << 11) | ((uint16_t)y & 0x07FFu));
}

static void pmbus_linear16_micro(uint8_t *restrict work)
{
    (void)work;
    uint16_t word = Pmbus.linear16_micro_args.word;
    int8_t exponent = Pmbus.linear16_micro_args.exponent;

    Pmbus.micro = scale_micro((int64_t)word, exponent);
}

static void pmbus_linear16_encode(uint8_t *restrict work)
{
    (void)work;
    int32_t micro = Pmbus.linear16_encode_args.micro;
    int8_t exponent = Pmbus.linear16_encode_args.exponent;

    if (micro <= 0)
    {
        Pmbus.word = 0;
        return;
    }
    int64_t v = (int64_t)micro;
    // Undo the exponent the part applies, then take the value out of micro-units.
    if (exponent < 0)
    {
        int32_t sh = -(int32_t)exponent;
        if (sh >= 40)
        {
            Pmbus.word = 0;
            return;
        }
        v <<= sh;
    }
    else if (exponent > 0)
    {
        v >>= exponent;
    }
    int64_t y = v / PROTOCORE_PMBUS_MICRO;
    Pmbus.word = y > 0xFFFF ? 0xFFFFu : (uint16_t)y;
}

static void pmbus_direct_micro(uint8_t *restrict work)
{
    (void)work;
    uint16_t word = Pmbus.direct_micro_args.word;
    int16_t m = Pmbus.direct_micro_args.m;
    int16_t b = Pmbus.direct_micro_args.b;
    int8_t r = Pmbus.direct_micro_args.r;

    if (m == 0)
    {
        Pmbus.micro = PROTOCORE_PMBUS_INVALID;
        return;
    }
    // X = (Y * 10^-R - b) / m, carried in micro-units so the divide keeps its precision.
    int64_t v = (int64_t)(int16_t)word * PROTOCORE_PMBUS_MICRO;
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
    v -= (int64_t)b * PROTOCORE_PMBUS_MICRO;
    v /= m;
    if (v > PROTOCORE_PMBUS_MAX_MICRO || v < PROTOCORE_PMBUS_MIN_MICRO)
    {
        Pmbus.micro = PROTOCORE_PMBUS_INVALID;
        return;
    }
    Pmbus.micro = (int32_t)v;
}

#if PROTOCORE_HAS_BUS

static void pmbus_begin(uint8_t *restrict work)
{
    (void)work;

    Smbus.begin(protocore_smbus_span());
    Pmbus.ok = Smbus.ok;
}

static void pmbus_set_page(uint8_t *restrict work)
{
    (void)work;
    uint8_t addr = Pmbus.set_page_args.addr;
    uint8_t page = Pmbus.set_page_args.page;

    Smbus.write_byte_args.addr = addr;
    Smbus.write_byte_args.cmd = PROTOCORE_PMBUS_PAGE;
    Smbus.write_byte_args.value = page;
    Smbus.write_byte(protocore_smbus_span());
    Pmbus.ok = Smbus.ok;
}

static void pmbus_read_vout_mode(uint8_t *restrict work)
{
    (void)work;
    uint8_t addr = Pmbus.read_vout_mode_args.addr;
    uint8_t *out = Pmbus.read_vout_mode_args.out;

    Smbus.read_byte_args.addr = addr;
    Smbus.read_byte_args.cmd = PROTOCORE_PMBUS_VOUT_MODE;
    Smbus.read_byte_args.out = out;
    Smbus.read_byte(protocore_smbus_span());
    Pmbus.ok = Smbus.ok;
}

static void pmbus_read_linear11(uint8_t *restrict work)
{
    (void)work;
    uint8_t addr = Pmbus.read_linear11_args.addr;
    uint8_t cmd = Pmbus.read_linear11_args.cmd;
    int32_t *micro = Pmbus.read_linear11_args.micro;

    if (micro == NULL)
    {
        Pmbus.ok = PROTO_FALSE;
        return;
    }
    uint16_t w = 0;
    Smbus.read_word_args.addr = addr;
    Smbus.read_word_args.cmd = cmd;
    Smbus.read_word_args.out = &w;
    Smbus.read_word(protocore_smbus_span());
    if (!Smbus.ok)
    {
        Pmbus.ok = PROTO_FALSE;
        return;
    }
    Pmbus.linear11_micro_args.word = w;
    pmbus_linear11_micro(work);
    *micro = Pmbus.micro;
    Pmbus.ok = *micro != PROTOCORE_PMBUS_INVALID;
}

static void pmbus_read_linear16(uint8_t *restrict work)
{
    (void)work;
    uint8_t addr = Pmbus.read_linear16_args.addr;
    uint8_t cmd = Pmbus.read_linear16_args.cmd;
    int8_t exponent = Pmbus.read_linear16_args.exponent;
    int32_t *micro = Pmbus.read_linear16_args.micro;

    if (micro == NULL)
    {
        Pmbus.ok = PROTO_FALSE;
        return;
    }
    uint16_t w = 0;
    Smbus.read_word_args.addr = addr;
    Smbus.read_word_args.cmd = cmd;
    Smbus.read_word_args.out = &w;
    Smbus.read_word(protocore_smbus_span());
    if (!Smbus.ok)
    {
        Pmbus.ok = PROTO_FALSE;
        return;
    }
    Pmbus.linear16_micro_args.word = w;
    Pmbus.linear16_micro_args.exponent = exponent;
    pmbus_linear16_micro(work);
    *micro = Pmbus.micro;
    Pmbus.ok = *micro != PROTOCORE_PMBUS_INVALID;
}

static void pmbus_write_linear16(uint8_t *restrict work)
{
    (void)work;
    uint8_t addr = Pmbus.write_linear16_args.addr;
    uint8_t cmd = Pmbus.write_linear16_args.cmd;
    int8_t exponent = Pmbus.write_linear16_args.exponent;
    int32_t micro = Pmbus.write_linear16_args.micro;

    Pmbus.linear16_encode_args.micro = micro;
    Pmbus.linear16_encode_args.exponent = exponent;
    pmbus_linear16_encode(work);
    Smbus.write_word_args.addr = addr;
    Smbus.write_word_args.cmd = cmd;
    Smbus.write_word_args.value = Pmbus.word;
    Smbus.write_word(protocore_smbus_span());
    Pmbus.ok = Smbus.ok;
}

static void pmbus_status_byte(uint8_t *restrict work)
{
    (void)work;
    uint8_t addr = Pmbus.status_byte_args.addr;
    uint8_t *out = Pmbus.status_byte_args.out;

    Smbus.read_byte_args.addr = addr;
    Smbus.read_byte_args.cmd = PROTOCORE_PMBUS_STATUS_BYTE;
    Smbus.read_byte_args.out = out;
    Smbus.read_byte(protocore_smbus_span());
    Pmbus.ok = Smbus.ok;
}

static void pmbus_status_word(uint8_t *restrict work)
{
    (void)work;
    uint8_t addr = Pmbus.status_word_args.addr;
    uint16_t *out = Pmbus.status_word_args.out;

    Smbus.read_word_args.addr = addr;
    Smbus.read_word_args.cmd = PROTOCORE_PMBUS_STATUS_WORD;
    Smbus.read_word_args.out = out;
    Smbus.read_word(protocore_smbus_span());
    Pmbus.ok = Smbus.ok;
}

static void pmbus_clear_faults(uint8_t *restrict work)
{
    (void)work;
    uint8_t addr = Pmbus.clear_faults_args.addr;

    Smbus.send_byte_args.addr = addr;
    Smbus.send_byte_args.value = PROTOCORE_PMBUS_CLEAR_FAULTS;
    Smbus.send_byte(protocore_smbus_span());
    Pmbus.ok = Smbus.ok;
}

static void pmbus_read_mfr_string(uint8_t *restrict work)
{
    (void)work;
    uint8_t addr = Pmbus.read_mfr_string_args.addr;
    uint8_t cmd = Pmbus.read_mfr_string_args.cmd;
    uint8_t *out = Pmbus.read_mfr_string_args.out;
    size_t cap = Pmbus.read_mfr_string_args.cap;
    size_t *len = Pmbus.read_mfr_string_args.len;

    Smbus.read_block_args.addr = addr;
    Smbus.read_block_args.cmd = cmd;
    Smbus.read_block_args.out = out;
    Smbus.read_block_args.cap = cap;
    Smbus.read_block_args.len = len;
    Smbus.read_block(protocore_smbus_span());
    Pmbus.ok = Smbus.ok;
}

#else // no bus seam. The encodings above are host-tested.

proto_bool protocore_pmbus_begin(void)
{
    return PROTO_FALSE;
}

proto_bool protocore_pmbus_set_page(uint8_t addr, uint8_t page)
{
    (void)addr;
    (void)page;
    return PROTO_FALSE;
}

proto_bool protocore_pmbus_read_vout_mode(uint8_t addr, uint8_t *out)
{
    (void)addr;
    (void)out;
    return PROTO_FALSE;
}

proto_bool protocore_pmbus_read_linear11(uint8_t addr, uint8_t cmd, int32_t *micro)
{
    (void)addr;
    (void)cmd;
    (void)micro;
    return PROTO_FALSE;
}

proto_bool protocore_pmbus_read_linear16(uint8_t addr, uint8_t cmd, int8_t exponent, int32_t *micro)
{
    (void)addr;
    (void)cmd;
    (void)exponent;
    (void)micro;
    return PROTO_FALSE;
}

proto_bool protocore_pmbus_write_linear16(uint8_t addr, uint8_t cmd, int8_t exponent, int32_t micro)
{
    (void)addr;
    (void)cmd;
    (void)exponent;
    (void)micro;
    return PROTO_FALSE;
}

proto_bool protocore_pmbus_status_byte(uint8_t addr, uint8_t *out)
{
    (void)addr;
    (void)out;
    return PROTO_FALSE;
}

proto_bool protocore_pmbus_status_word(uint8_t addr, uint16_t *out)
{
    (void)addr;
    (void)out;
    return PROTO_FALSE;
}

proto_bool protocore_pmbus_clear_faults(uint8_t addr)
{
    (void)addr;
    return PROTO_FALSE;
}

proto_bool protocore_pmbus_read_mfr_string(uint8_t addr, uint8_t cmd, uint8_t *out, size_t cap, size_t *len)
{
    (void)addr;
    (void)cmd;
    (void)out;
    (void)cap;
    (void)len;
    return PROTO_FALSE;
}

#endif // PROTOCORE_HAS_BUS

PmbusNs Pmbus = {.vout_mode_kind = pmbus_vout_mode_kind,
                 .vout_exponent = pmbus_vout_exponent,
                 .l11_mantissa = pmbus_l11_mantissa,
                 .l11_exponent = pmbus_l11_exponent,
                 .linear11_micro = pmbus_linear11_micro,
                 .linear11_encode = pmbus_linear11_encode,
                 .linear16_micro = pmbus_linear16_micro,
                 .linear16_encode = pmbus_linear16_encode,
                 .direct_micro = pmbus_direct_micro,
                 .begin = pmbus_begin,
                 .set_page = pmbus_set_page,
                 .read_vout_mode = pmbus_read_vout_mode,
                 .read_linear11 = pmbus_read_linear11,
                 .read_linear16 = pmbus_read_linear16,
                 .write_linear16 = pmbus_write_linear16,
                 .status_byte = pmbus_status_byte,
                 .status_word = pmbus_status_word,
                 .clear_faults = pmbus_clear_faults,
                 .read_mfr_string = pmbus_read_mfr_string};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PMBUS
