// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ldc1614.c
 * @brief LDC1614 inductance-to-digital codec + ESP32 binding (see ldc1614.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_LDC1614

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_LDC1614 needs a bus master (an I2C master). Provide one in core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/endian.h" // endian.wr16be / endian.rd16be: the registers are big-endian
#include "mmgr/secure.h" // the persistent end this module's state is taken from
#include "server/peripherals/i2c.h"
#include "server/peripherals/ldc1614/ldc1614.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_I2C_DEVICE_BORROW persistent bytes, or null while the pool was short
} Ldc1614OwnCtx;
static Ldc1614OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ldc1614_span(void)
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

static void ldc1614_build_config(uint8_t *restrict work);
static void ldc1614_data(uint8_t *restrict work);

static void ldc1614_data(uint8_t *restrict work)
{
    (void)work;
    uint16_t msb_reg = Ldc1614.data_args.msb_reg;
    uint16_t lsb_reg = Ldc1614.data_args.lsb_reg;

    Ldc1614.value = ((uint32_t)(msb_reg & 0x0FFF) << 16) | lsb_reg;
}

static void ldc1614_error(uint8_t *restrict work)
{
    (void)work;
    uint16_t msb_reg = Ldc1614.error_args.msb_reg;

    Ldc1614.flags = (uint8_t)((msb_reg >> 12) & 0x0F);
}

static void ldc1614_sensor_freq_hz(uint8_t *restrict work)
{
    (void)work;
    uint32_t data28 = Ldc1614.sensor_freq_hz_args.data28;
    uint32_t fref_hz = Ldc1614.sensor_freq_hz_args.fref_hz;

    Ldc1614.hz = ((uint64_t)data28 * fref_hz) >> 28;
}

static void ldc1614_build_config(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Ldc1614.build_config_args.buf;
    size_t cap = Ldc1614.build_config_args.cap;
    uint16_t rcount = Ldc1614.build_config_args.rcount;
    uint16_t settlecount = Ldc1614.build_config_args.settlecount;

    if (!buf || cap < LDC1614_CONFIG_MAX)
    {
        Ldc1614.n = 0;
        return;
    }
    const uint16_t seq[][2] = {
        {LDC1614_REG_RCOUNT_CH0, rcount},
        {LDC1614_REG_SETTLECOUNT_CH0, settlecount},
        {LDC1614_REG_CLOCK_DIVIDERS_CH0, 0x1001}, // FIN_SEL=1, FREF_DIVIDER=1
        {LDC1614_REG_DRIVE_CURRENT_CH0, 0x9000},  // sensor drive current
        {LDC1614_REG_ERROR_CONFIG, 0x0000},       // no error reporting on INTB
        {LDC1614_REG_MUX_CONFIG, 0x020D},         // single channel CH0, 10 MHz deglitch
        {LDC1614_REG_CONFIG, 0x1601},             // active CH0, internal ref, full current, start
    };
    size_t o = 0;
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
    {
        buf[o++] = (uint8_t)seq[i][0];
        buf[o++] = (uint8_t)(seq[i][1] >> 8);
        buf[o++] = (uint8_t)seq[i][1];
    }
    Ldc1614.n = o;
}

// All LDC1614 I2C-binding state, owned by one instance (internal linkage): the device address, the
// register frame, and the bring-up sequence buffer, so it is one named owner, unreachable from any
// other translation unit. Both buffers are members rather than locals because a transfer is
// composed in place: a write is a register byte and a 16-bit value, and the bring-up sequence is
// the (register, msb, lsb) triples protocore_ldc1614_build_config lays down.
typedef struct
{
    uint8_t addr;
    uint8_t frame[3];
    uint8_t config[LDC1614_CONFIG_MAX];
} Ldc1614Ctx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define LDC1614_OFF_CTX 0u
static_assert(LDC1614_OFF_CTX + sizeof(Ldc1614Ctx) <= PROTOCORE_I2C_DEVICE_BORROW,
              "PROTOCORE_I2C_DEVICE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define LDC1614_CTX(w) ((Ldc1614Ctx *)(void *)((w) + LDC1614_OFF_CTX))

// Zero is "no address set yet", which is the address the ADDR pin selects when it is low - stated
// here rather than on the declaration so the context carries no initializer and can live in a
// borrow that arrives zeroed. begin() applies the same default to the address it is handed.
static uint8_t dev_addr(uint8_t *restrict work)
{
    return LDC1614_CTX(work)->addr ? LDC1614_CTX(work)->addr : (uint8_t)PROTOCORE_LDC1614_I2C_ADDR;
}

static proto_bool read16(uint8_t *restrict work, uint8_t reg, uint16_t *out)
{
    if (!protocore_i2c_write_read(dev_addr(work), &reg, 1, LDC1614_CTX(work)->frame, 2))
    {
        return PROTO_FALSE;
    }
    *out = endian.rd16be(LDC1614_CTX(work)->frame);
    return PROTO_TRUE;
}

static proto_bool write16(uint8_t *restrict work, uint8_t reg, uint16_t val)
{
    LDC1614_CTX(work)->frame[0] = reg;
    (void)endian.wr16be(&LDC1614_CTX(work)->frame[1], val);
    return protocore_i2c_write(dev_addr(work), LDC1614_CTX(work)->frame, sizeof(LDC1614_CTX(work)->frame));
}

static void ldc1614_begin(uint8_t *restrict work)
{
    uint8_t addr = Ldc1614.begin_args.addr;
    uint16_t rcount = Ldc1614.begin_args.rcount;
    uint16_t settlecount = Ldc1614.begin_args.settlecount;

    protocore_i2c_begin();
    LDC1614_CTX(work)->addr = addr ? addr : (uint8_t)PROTOCORE_LDC1614_I2C_ADDR;
    uint16_t id = 0;
    if (!read16(work, LDC1614_REG_DEVICE_ID, &id))
    {
        Ldc1614.ok = PROTO_FALSE;
        return;
    }
    if (id != LDC1614_DEVICE_ID)
    {
        Ldc1614.ok = PROTO_FALSE;
        return;
    }
    Ldc1614.build_config_args.buf = LDC1614_CTX(work)->config;
    Ldc1614.build_config_args.cap = sizeof(LDC1614_CTX(work)->config);
    Ldc1614.build_config_args.rcount = rcount;
    Ldc1614.build_config_args.settlecount = settlecount;
    ldc1614_build_config(work);
    size_t n = Ldc1614.n;
    for (size_t i = 0; i + 3 <= n; i += 3)
    {
        if (!write16(work, LDC1614_CTX(work)->config[i], endian.rd16be(&LDC1614_CTX(work)->config[i + 1])))
        {
            Ldc1614.ok = PROTO_FALSE;
            return;
        }
    }
    Ldc1614.ok = PROTO_TRUE;
}

static void ldc1614_read_ch0(uint8_t *restrict work)
{
    uint32_t *out = Ldc1614.read_ch0_args.out;

    if (!out)
    {
        Ldc1614.ok = PROTO_FALSE;
        return;
    }
    uint16_t msb = 0;
    uint16_t lsb = 0;
    if (!read16(work, LDC1614_REG_DATA_CH0_MSB, &msb) || !read16(work, LDC1614_REG_DATA_CH0_LSB, &lsb))
    {
        Ldc1614.ok = PROTO_FALSE;
        return;
    }
    Ldc1614.data_args.msb_reg = msb;
    Ldc1614.data_args.lsb_reg = lsb;
    ldc1614_data(work);
    *out = Ldc1614.value;
    Ldc1614.ok = PROTO_TRUE;
}

Ldc1614Ns Ldc1614 = {.data = ldc1614_data,
                     .error = ldc1614_error,
                     .sensor_freq_hz = ldc1614_sensor_freq_hz,
                     .build_config = ldc1614_build_config,
                     .begin = ldc1614_begin,
                     .read_ch0 = ldc1614_read_ch0};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LDC1614
