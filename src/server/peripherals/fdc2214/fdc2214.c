// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fdc2214.c
 * @brief FDC2114/2214 capacitance-to-digital codec + ESP32 binding (see fdc2214.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_FDC2214

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_FDC2214 needs a bus master (an I2C master). Provide one in test/core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/endian/endian.h" // endian.wr16be / endian.rd16be: the registers are big-endian
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/peripherals/fdc2214/fdc2214.h"
#include "server/peripherals/i2c.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_I2C_DEVICE_BORROW persistent bytes, or null while the pool was short
} Fdc2214OwnCtx;
static Fdc2214OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_fdc2214_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_I2C_DEVICE_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void fdc2214_build_config(uint8_t *restrict work);
static void fdc2214_data(uint8_t *restrict work);

static void fdc2214_data(uint8_t *restrict work)
{
    (void)work;
    uint16_t msb_reg = Fdc2214.data_args.msb_reg;
    uint16_t lsb_reg = Fdc2214.data_args.lsb_reg;

    Fdc2214.value = ((uint32_t)(msb_reg & 0x0FFF) << 16) | lsb_reg;
}

static void fdc2214_error(uint8_t *restrict work)
{
    (void)work;
    uint16_t msb_reg = Fdc2214.error_args.msb_reg;

    Fdc2214.flags = (uint8_t)((msb_reg >> 12) & 0x0F);
}

static void fdc2214_sensor_freq_hz(uint8_t *restrict work)
{
    (void)work;
    uint32_t data28 = Fdc2214.sensor_freq_hz_args.data28;
    uint32_t fref_hz = Fdc2214.sensor_freq_hz_args.fref_hz;

    Fdc2214.hz = ((uint64_t)data28 * fref_hz) >> 28;
}

static void fdc2214_build_config(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Fdc2214.build_config_args.buf;
    size_t cap = Fdc2214.build_config_args.cap;
    uint16_t rcount = Fdc2214.build_config_args.rcount;
    uint16_t settlecount = Fdc2214.build_config_args.settlecount;

    if (!buf || cap < FDC2214_CONFIG_MAX)
    {
        Fdc2214.n = 0;
        return;
    }
    // (register, value) writes; CONFIG is written last because it starts the conversion.
    const uint16_t seq[][2] = {
        {FDC2214_REG_RCOUNT_CH0, rcount},
        {FDC2214_REG_SETTLECOUNT_CH0, settlecount},
        {FDC2214_REG_CLOCK_DIVIDERS_CH0, 0x1001}, // FIN_SEL=1, FREF_DIVIDER=1
        {FDC2214_REG_DRIVE_CURRENT_CH0, 0x8C40},  // mid drive current
        {FDC2214_REG_ERROR_CONFIG, 0x0000},       // no error reporting on INTB
        {FDC2214_REG_MUX_CONFIG, 0x020D},         // single channel CH0, 10 MHz deglitch
        {FDC2214_REG_CONFIG, 0x1E01},             // active CH0, internal ref, full current, start
    };
    size_t o = 0;
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
    {
        buf[o++] = (uint8_t)seq[i][0];
        buf[o++] = (uint8_t)(seq[i][1] >> 8);
        buf[o++] = (uint8_t)seq[i][1];
    }
    Fdc2214.n = o;
}

// All FDC2214 I2C-binding state, owned by one instance (internal linkage): the device address, the
// register frame, and the bring-up sequence buffer, so it is one named owner, unreachable from any
// other translation unit. Both buffers are members rather than locals because a transfer is
// composed in place: a write is a register byte and a 16-bit value, and the bring-up sequence is
// the (register, msb, lsb) triples protocore_fdc2214_build_config lays down.
typedef struct
{
    uint8_t addr;
    uint8_t frame[3];
    uint8_t config[FDC2214_CONFIG_MAX];
} Fdc2214Ctx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define FDC2214_OFF_CTX 0u
static_assert(FDC2214_OFF_CTX + sizeof(Fdc2214Ctx) <= PROTOCORE_I2C_DEVICE_BORROW,
              "PROTOCORE_I2C_DEVICE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define FDC2214_CTX(w) ((Fdc2214Ctx *)(void *)((w) + FDC2214_OFF_CTX))

// Zero is "no address set yet", which is the address the ADDR pin selects when it is low - stated
// here rather than on the declaration so the context carries no initializer and can live in a
// borrow that arrives zeroed. begin() applies the same default to the address it is handed.
static uint8_t dev_addr(uint8_t *restrict work)
{
    return FDC2214_CTX(work)->addr ? FDC2214_CTX(work)->addr : (uint8_t)PROTOCORE_FDC2214_I2C_ADDR;
}

static proto_bool read16(uint8_t *restrict work, uint8_t reg, uint16_t *out)
{
    if (!protocore_i2c_write_read(dev_addr(work), &reg, 1, FDC2214_CTX(work)->frame, 2))
    {
        return PROTO_FALSE;
    }
    *out = endian.rd16be(FDC2214_CTX(work)->frame);
    return PROTO_TRUE;
}

static proto_bool write16(uint8_t *restrict work, uint8_t reg, uint16_t val)
{
    FDC2214_CTX(work)->frame[0] = reg;
    (void)endian.wr16be(&FDC2214_CTX(work)->frame[1], val);
    return protocore_i2c_write(dev_addr(work), FDC2214_CTX(work)->frame, sizeof(FDC2214_CTX(work)->frame));
}

static void fdc2214_begin(uint8_t *restrict work)
{
    uint8_t addr = Fdc2214.begin_args.addr;
    uint16_t rcount = Fdc2214.begin_args.rcount;
    uint16_t settlecount = Fdc2214.begin_args.settlecount;

    protocore_i2c_begin();
    FDC2214_CTX(work)->addr = addr ? addr : (uint8_t)PROTOCORE_FDC2214_I2C_ADDR;
    uint16_t id = 0;
    if (!read16(work, FDC2214_REG_DEVICE_ID, &id))
    {
        Fdc2214.ok = PROTO_FALSE;
        return;
    }
    if (id != FDC2214_DEVICE_ID && id != 0x3054) // 0x3054 = FDC2114 (12-bit sibling)
    {
        Fdc2214.ok = PROTO_FALSE;
        return;
    }
    Fdc2214.build_config_args.buf = FDC2214_CTX(work)->config;
    Fdc2214.build_config_args.cap = sizeof(FDC2214_CTX(work)->config);
    Fdc2214.build_config_args.rcount = rcount;
    Fdc2214.build_config_args.settlecount = settlecount;
    fdc2214_build_config(work);
    size_t n = Fdc2214.n;
    for (size_t i = 0; i + 3 <= n; i += 3)
    {
        if (!write16(work, FDC2214_CTX(work)->config[i], endian.rd16be(&FDC2214_CTX(work)->config[i + 1])))
        {
            Fdc2214.ok = PROTO_FALSE;
            return;
        }
    }
    Fdc2214.ok = PROTO_TRUE;
}

static void fdc2214_read_ch0(uint8_t *restrict work)
{
    uint32_t *out = Fdc2214.read_ch0_args.out;

    if (!out)
    {
        Fdc2214.ok = PROTO_FALSE;
        return;
    }
    uint16_t msb = 0;
    uint16_t lsb = 0;
    if (!read16(work, FDC2214_REG_DATA_CH0_MSB, &msb) || !read16(work, FDC2214_REG_DATA_CH0_LSB, &lsb))
    {
        Fdc2214.ok = PROTO_FALSE;
        return;
    }
    Fdc2214.data_args.msb_reg = msb;
    Fdc2214.data_args.lsb_reg = lsb;
    fdc2214_data(work);
    *out = Fdc2214.value;
    Fdc2214.ok = PROTO_TRUE;
}

Fdc2214Ns Fdc2214 = {.data = fdc2214_data,
                     .error = fdc2214_error,
                     .sensor_freq_hz = fdc2214_sensor_freq_hz,
                     .build_config = fdc2214_build_config,
                     .begin = fdc2214_begin,
                     .read_ch0 = fdc2214_read_ch0};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FDC2214
