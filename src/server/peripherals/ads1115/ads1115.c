// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ads1115.c
 * @brief TI ADS1115 16-bit ADC codec - implementation. See ads1115.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_ADS1115

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_ADS1115 needs a bus master (an I2C master). Provide one in test/core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/endian/endian.h" // endian.wr16be / endian.rd16be: the registers are big-endian
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/clock/clock.h" // pcdelay
#include "server/peripherals/ads1115/ads1115.h"
#include "server/peripherals/i2c.h"

PROTOCORE_BEGIN_DECLS

// Config-register field values (per the ADS1115 datasheet).
static const uint16_t OS_SINGLE = 0x8000;   // start a single conversion
static const uint16_t MUX_SINGLE0 = 0x4000; // single-ended AIN0; AINx = this | (channel << 12)
static const uint16_t MODE_SINGLE = 0x0100; // single-shot
static const uint16_t COMP_DISABLE = 0x0003;

// PGA bits and the matching full-scale range in microvolts, indexed by gain code.
static const uint16_t PGA_BITS[6] = {0x0000, 0x0200, 0x0400, 0x0600, 0x0800, 0x0A00};
static const int32_t FSR_UV[6] = {6144000, 4096000, 2048000, 1024000, 512000, 256000};

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_I2C_DEVICE_BORROW persistent bytes
} Ads1115OwnCtx;
static Ads1115OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ads1115_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_I2C_DEVICE_BORROW).buf;
    }
    return s_own.span;
}

static void ads1115_config_single(uint8_t *restrict work);
static void ads1115_raw_to_uv(uint8_t *restrict work);
static void ads1115_read_raw(uint8_t *restrict work);

static void ads1115_config_single(uint8_t *restrict work)
{
    (void)work;
    uint8_t channel = Ads1115.config_single_args.channel;
    uint8_t gain = Ads1115.config_single_args.gain;
    uint8_t dr = Ads1115.config_single_args.dr;

    if (channel > 3)
    {
        channel = 0;
    }
    if (gain > ADS1115_GAIN_16)
    {
        gain = (uint8_t)PROTOCORE_ADS1115_GAIN;
    }
    if (dr > ADS1115_DR_860)
    {
        dr = (uint8_t)PROTOCORE_ADS1115_DR;
    }
    uint16_t cfg = OS_SINGLE;
#if PROTOCORE_ADS1115_DIFFERENTIAL
    cfg |= (uint16_t)((uint16_t)channel << 12); // differential pair: MUX 0=AIN0-1, 1=AIN0-3, 2=AIN1-3, 3=AIN2-3
#else
    cfg |= (uint16_t)(MUX_SINGLE0 | ((uint16_t)channel << 12)); // single-ended AINx
#endif
    cfg |= PGA_BITS[gain];
    cfg |= MODE_SINGLE;
    cfg |= (uint16_t)((uint16_t)dr << 5); // data-rate bits [7:5]
    cfg |= COMP_DISABLE;
    Ads1115.word = cfg;
}

static void ads1115_raw_to_uv(uint8_t *restrict work)
{
    (void)work;
    int16_t raw = Ads1115.raw_to_uv_args.raw;
    uint8_t gain = Ads1115.raw_to_uv_args.gain;

    if (gain > ADS1115_GAIN_16)
    {
        gain = (uint8_t)PROTOCORE_ADS1115_GAIN;
    }
    Ads1115.uv = (int32_t)((int64_t)raw * FSR_UV[gain] / 32768);
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

// All ADS1115 I2C-binding state, owned by one instance (internal linkage): the device address and
// the bus frame, so it is one named owner, unreachable from any other translation unit. The frame
// is a member rather than a local because a transfer is composed in place, and three bytes is the
// widest this part moves, a register byte plus a 16-bit register value.
typedef struct
{
    uint8_t addr;
    uint8_t frame[3];
} Ads1115Ctx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define ADS1115_OFF_CTX 0u
static_assert(ADS1115_OFF_CTX + sizeof(Ads1115Ctx) <= PROTOCORE_I2C_DEVICE_BORROW,
              "PROTOCORE_I2C_DEVICE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(ADS1115_OFF_CTX % _Alignof(Ads1115Ctx) == 0,
              "ADS1115_OFF_CTX is not a multiple of alignof(Ads1115Ctx) - ADS1115_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define ADS1115_CTX(w) ((Ads1115Ctx *)(void *)((w) + ADS1115_OFF_CTX))

// Zero is "no address set yet", which is the default address - stated here rather than on the
// declaration so the context carries no initializer and can live in a borrow that arrives zeroed.
// begin() applies the same default to the address it is handed.
static uint8_t dev_addr(uint8_t *restrict work)
{
    return ADS1115_CTX(work)->addr ? ADS1115_CTX(work)->addr : (uint8_t)PROTOCORE_ADS1115_I2C_ADDR;
}

static proto_bool wr16(uint8_t *restrict work, uint8_t reg, uint16_t v)
{
    ADS1115_CTX(work)->frame[0] = reg;
    (void)endian.wr16be(&ADS1115_CTX(work)->frame[1], v);
    return protocore_i2c_write(dev_addr(work), ADS1115_CTX(work)->frame, sizeof(ADS1115_CTX(work)->frame));
}

static proto_bool rd16(uint8_t *restrict work, uint8_t reg, uint16_t *v)
{
    if (!protocore_i2c_write_read(dev_addr(work), &reg, 1, ADS1115_CTX(work)->frame, 2))
    {
        return PROTO_FALSE;
    }
    *v = endian.rd16be(ADS1115_CTX(work)->frame);
    return PROTO_TRUE;
}

static void ads1115_begin(uint8_t *restrict work)
{
    uint8_t addr = Ads1115.begin_args.addr;

    ADS1115_CTX(work)->addr = addr ? addr : (uint8_t)PROTOCORE_ADS1115_I2C_ADDR;
    protocore_i2c_begin();
    Ads1115.ok = PROTO_TRUE;
}

static void ads1115_read_raw(uint8_t *restrict work)
{
    (void)work;
    uint8_t channel = Ads1115.read_raw_args.channel;
    uint8_t gain = Ads1115.read_raw_args.gain;
    int16_t *raw = Ads1115.read_raw_args.raw;

    if (!raw)
    {
        Ads1115.ok = PROTO_FALSE;
        return;
    }
    uint8_t dr = (uint8_t)PROTOCORE_ADS1115_DR;
    if (dr > ADS1115_DR_860)
    {
        dr = ADS1115_DR_128;
    }
    Ads1115.config_single_args.channel = channel;
    Ads1115.config_single_args.gain = gain;
    Ads1115.config_single_args.dr = dr;
    ads1115_config_single(work);
    if (!wr16(work, ADS1115_REG_CONFIG, Ads1115.word))
    {
        Ads1115.ok = PROTO_FALSE;
        return;
    }
    // Single-shot conversion time tracks the data rate (~1000/SPS ms); wait it out plus a 1 ms margin.
    static const uint16_t protocore_ads1115_sps[8] = {8, 16, 32, 64, 128, 250, 475, 860};
    pcdelay(1000u / protocore_ads1115_sps[dr] + 1);
    uint16_t v = 0;
    if (!rd16(work, ADS1115_REG_CONVERSION, &v))
    {
        Ads1115.ok = PROTO_FALSE;
        return;
    }
    *raw = (int16_t)v;
    Ads1115.ok = PROTO_TRUE;
}

static void ads1115_read_uv(uint8_t *restrict work)
{
    (void)work;
    uint8_t channel = Ads1115.read_uv_args.channel;
    uint8_t gain = Ads1115.read_uv_args.gain;
    int32_t *microvolts = Ads1115.read_uv_args.microvolts;

    int16_t raw = 0;
    Ads1115.read_raw_args.channel = channel;
    Ads1115.read_raw_args.gain = gain;
    Ads1115.read_raw_args.raw = &raw;
    ads1115_read_raw(work);
    if (!Ads1115.ok)
    {
        Ads1115.ok = PROTO_FALSE;
        return;
    }
    if (microvolts)
    {
        Ads1115.raw_to_uv_args.raw = raw;
        Ads1115.raw_to_uv_args.gain = gain;
        ads1115_raw_to_uv(work);
        *microvolts = Ads1115.uv;
    }
    Ads1115.ok = PROTO_TRUE;
}

Ads1115Ns Ads1115 = {.config_single = ads1115_config_single,
                     .raw_to_uv = ads1115_raw_to_uv,
                     .begin = ads1115_begin,
                     .read_raw = ads1115_read_raw,
                     .read_uv = ads1115_read_uv};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ADS1115
