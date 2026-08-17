// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pca9685.c
 * @brief NXP PCA9685 PWM / servo driver codec - implementation. See pca9685.h.
 *
 * Prescale changes require the oscillator to be asleep, so begin() sleeps, writes PRESCALE,
 * wakes with auto-increment, then restarts and selects totem-pole outputs.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_PCA9685

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_PCA9685 needs a bus master (an I2C master). Provide one in core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/secure.h"        // the persistent end this module's state is taken from
#include "server/clock/clock.h" // protocore_delay_us: the oscillator settle in begin()
#include "server/peripherals/i2c.h"
#include "server/peripherals/pca9685/pca9685.h"

PROTOCORE_BEGIN_DECLS

static const uint32_t PCA9685_OSC_HZ = 25000000u;
static const uint8_t PCA9685_PRESCALE_MIN = 3;
static const uint8_t PCA9685_PRESCALE_MAX = 255;

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_I2C_DEVICE_BORROW persistent bytes, or null while the pool was short
} Pca9685OwnCtx;
static Pca9685OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_pca9685_span(void)
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

static void pca9685_channel_reg(uint8_t *restrict work);
static void pca9685_prescale(uint8_t *restrict work);
static void pca9685_set_pwm_bytes(uint8_t *restrict work);

static void pca9685_prescale(uint8_t *restrict work)
{
    (void)work;
    uint32_t freq_hz = Pca9685.prescale_args.freq_hz;

    if (freq_hz == 0)
    {
        Pca9685.value = PCA9685_PRESCALE_MAX;
        return;
    }
    uint32_t denom = 4096u * freq_hz;
    uint32_t pre = (PCA9685_OSC_HZ + denom / 2) / denom; // round(25e6 / (4096*freq))
    pre = pre ? pre - 1 : 0;
    if (pre < PCA9685_PRESCALE_MIN)
    {
        pre = PCA9685_PRESCALE_MIN;
    }
    if (pre > PCA9685_PRESCALE_MAX)
    {
        pre = PCA9685_PRESCALE_MAX;
    }
    Pca9685.value = (uint8_t)pre;
}

static void pca9685_channel_reg(uint8_t *restrict work)
{
    (void)work;
    uint8_t channel = Pca9685.channel_reg_args.channel;

    if (channel >= PCA9685_CHANNELS)
    {
        Pca9685.value = 0;
        return;
    }
    Pca9685.value = (uint8_t)(PCA9685_REG_LED0_ON_L + 4 * channel);
}

static void pca9685_us_to_count(uint8_t *restrict work)
{
    (void)work;
    uint32_t microseconds = Pca9685.us_to_count_args.microseconds;
    uint32_t freq_hz = Pca9685.us_to_count_args.freq_hz;

    // count = round(us * 4096 * freq / 1e6); 64-bit to avoid overflow at high pulse widths.
    uint64_t num = (uint64_t)microseconds * 4096u * freq_hz;
    uint32_t count = (uint32_t)((num + 500000u) / 1000000u);
    Pca9685.count = count > PCA9685_COUNT_MAX ? (uint16_t)PCA9685_COUNT_MAX : (uint16_t)count;
}

static void pca9685_set_pwm_bytes(uint8_t *restrict work)
{
    uint8_t *buf = Pca9685.set_pwm_bytes_args.buf;
    size_t cap = Pca9685.set_pwm_bytes_args.cap;
    uint8_t channel = Pca9685.set_pwm_bytes_args.channel;
    uint16_t on = Pca9685.set_pwm_bytes_args.on;
    uint16_t off = Pca9685.set_pwm_bytes_args.off;

    if (!buf || cap < 5 || channel >= PCA9685_CHANNELS)
    {
        Pca9685.n = 0;
        return;
    }
    Pca9685.channel_reg_args.channel = channel;
    pca9685_channel_reg(work);
    buf[0] = Pca9685.value;
    // Bit 4 of each _H register is the full-ON / full-OFF flag (count bit 12), so keep bits 4:0.
    buf[1] = (uint8_t)(on & 0xFF);
    buf[2] = (uint8_t)((on >> 8) & 0x1F);
    buf[3] = (uint8_t)(off & 0xFF);
    buf[4] = (uint8_t)((off >> 8) & 0x1F);
    Pca9685.n = 5;
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

// All PCA9685 I2C-binding state, owned by one instance (internal linkage): the device address,
// the configured PWM frequency, and the bus frame, grouped so it is one named owner, unreachable
// cross-TU. The frame is a member rather than a local because a transfer is composed in place:
// five bytes is the widest this part takes, a register byte plus a channel's on and off counts.
typedef struct
{
    uint8_t addr;
    uint32_t freq;
    uint8_t frame[5];
} Pca9685Ctx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define PCA9685_OFF_CTX 0u
static_assert(PCA9685_OFF_CTX + sizeof(Pca9685Ctx) <= PROTOCORE_I2C_DEVICE_BORROW,
              "PROTOCORE_I2C_DEVICE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define PCA9685_CTX(w) ((Pca9685Ctx *)(void *)((w) + PCA9685_OFF_CTX))

// Zero is "not set yet", which is the configured default - stated here rather than on the
// declaration so the context carries no initializer and can live in a borrow that arrives zeroed.
// begin() applies the same defaults to what it is handed.
static uint8_t dev_addr(uint8_t *restrict work)
{
    return PCA9685_CTX(work)->addr ? PCA9685_CTX(work)->addr : (uint8_t)PROTOCORE_PCA9685_I2C_ADDR;
}

// The PWM frequency the prescale was computed from, which a pulse width in microseconds is
// counted against.
static uint32_t dev_freq(uint8_t *restrict work)
{
    return PCA9685_CTX(work)->freq ? PCA9685_CTX(work)->freq : (uint32_t)PROTOCORE_PCA9685_FREQ;
}

static proto_bool wr(uint8_t *restrict work, uint8_t reg, uint8_t val)
{
    PCA9685_CTX(work)->frame[0] = reg;
    PCA9685_CTX(work)->frame[1] = val;
    return protocore_i2c_write(dev_addr(work), PCA9685_CTX(work)->frame, 2);
}

static void pca9685_begin(uint8_t *restrict work)
{
    uint8_t addr = Pca9685.begin_args.addr;
    uint32_t freq_hz = Pca9685.begin_args.freq_hz;

    PCA9685_CTX(work)->addr = addr ? addr : (uint8_t)PROTOCORE_PCA9685_I2C_ADDR;
    PCA9685_CTX(work)->freq = freq_hz ? freq_hz : (uint32_t)PROTOCORE_PCA9685_FREQ;
    protocore_i2c_begin();
    proto_bool ok = PROTO_TRUE;
    ok &= wr(work, PCA9685_REG_MODE1, 0x10); // SLEEP (required before changing PRESCALE)
    Pca9685.prescale_args.freq_hz = dev_freq(work);
    pca9685_prescale(work);
    ok &= wr(work, PCA9685_REG_PRESCALE, Pca9685.value);
    ok &= wr(work, PCA9685_REG_MODE1, 0x20); // wake, auto-increment (AI)
    protocore_delay_us(500);                 // the oscillator settles before RESTART
    ok &= wr(work, PCA9685_REG_MODE1, 0xA0); // AI + RESTART
    ok &= wr(work, PCA9685_REG_MODE2, 0x04); // OUTDRV: totem-pole outputs
    Pca9685.ok = ok;
}

static void pca9685_set_pwm(uint8_t *restrict work)
{
    uint8_t channel = Pca9685.set_pwm_args.channel;
    uint16_t on = Pca9685.set_pwm_args.on;
    uint16_t off = Pca9685.set_pwm_args.off;

    Pca9685.set_pwm_bytes_args.buf = PCA9685_CTX(work)->frame;
    Pca9685.set_pwm_bytes_args.cap = sizeof(PCA9685_CTX(work)->frame);
    Pca9685.set_pwm_bytes_args.channel = channel;
    Pca9685.set_pwm_bytes_args.on = on;
    Pca9685.set_pwm_bytes_args.off = off;
    pca9685_set_pwm_bytes(work);
    if (Pca9685.n != 5)
    {
        Pca9685.ok = PROTO_FALSE;
        return;
    }
    Pca9685.ok = protocore_i2c_write(dev_addr(work), PCA9685_CTX(work)->frame, 5);
}

static void pca9685_set_servo_us(uint8_t *restrict work)
{
    uint8_t channel = Pca9685.set_servo_us_args.channel;
    uint32_t microseconds = Pca9685.set_servo_us_args.microseconds;

    // The count is captured before the write runs: both report through the one namespace, so
    // nesting the two calls would hand set_pwm whatever the second one had already overwritten.
    Pca9685.us_to_count_args.microseconds = microseconds;
    Pca9685.us_to_count_args.freq_hz = dev_freq(work);
    pca9685_us_to_count(work);
    Pca9685.set_pwm_args.channel = channel;
    Pca9685.set_pwm_args.on = 0;
    Pca9685.set_pwm_args.off = Pca9685.count;
    pca9685_set_pwm(work); // reports through Pca9685.ok itself
}

Pca9685Ns Pca9685 = {.prescale = pca9685_prescale,
                     .channel_reg = pca9685_channel_reg,
                     .us_to_count = pca9685_us_to_count,
                     .set_pwm_bytes = pca9685_set_pwm_bytes,
                     .begin = pca9685_begin,
                     .set_pwm = pca9685_set_pwm,
                     .set_servo_us = pca9685_set_servo_us};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PCA9685
