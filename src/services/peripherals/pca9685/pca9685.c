// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pca9685.c
 * @brief NXP PCA9685 PWM / servo driver codec - implementation. See pca9685.h.
 *
 * Prescale changes require the oscillator to be asleep, so begin() sleeps, writes PRESCALE,
 * wakes with auto-increment, then restarts and selects totem-pole outputs.
 */

#include "services/peripherals/pca9685/pca9685.h"
#include "protocore_config.h"

#if PC_ENABLE_PCA9685

#if PC_HAS_BUS
#include "server/clock/clock.h" // pc_delay_us: the oscillator settle in begin()
#include "services/peripherals/i2c.h"
#endif
static const uint32_t PCA9685_OSC_HZ = 25000000u;
static const uint8_t PCA9685_PRESCALE_MIN = 3;
static const uint8_t PCA9685_PRESCALE_MAX = 255;

uint8_t pc_pca9685_prescale(uint32_t freq_hz)
{
    if (freq_hz == 0)
    {
        return PCA9685_PRESCALE_MAX;
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
    return (uint8_t)pre;
}

uint8_t pc_pca9685_channel_reg(uint8_t channel)
{
    if (channel >= PCA9685_CHANNELS)
    {
        return 0;
    }
    return (uint8_t)(PCA9685_REG_LED0_ON_L + 4 * channel);
}

uint16_t pc_pca9685_us_to_count(uint32_t microseconds, uint32_t freq_hz)
{
    // count = round(us * 4096 * freq / 1e6); 64-bit to avoid overflow at high pulse widths.
    uint64_t num = (uint64_t)microseconds * 4096u * freq_hz;
    uint32_t count = (uint32_t)((num + 500000u) / 1000000u);
    return count > PCA9685_COUNT_MAX ? (uint16_t)PCA9685_COUNT_MAX : (uint16_t)count;
}

size_t pc_pca9685_set_pwm_bytes(uint8_t *buf, size_t cap, uint8_t channel, uint16_t on, uint16_t off)
{
    if (!buf || cap < 5 || channel >= PCA9685_CHANNELS)
    {
        return 0;
    }
    buf[0] = pc_pca9685_channel_reg(channel);
    // Bit 4 of each _H register is the full-ON / full-OFF flag (count bit 12), so keep bits 4:0.
    buf[1] = (uint8_t)(on & 0xFF);
    buf[2] = (uint8_t)((on >> 8) & 0x1F);
    buf[3] = (uint8_t)(off & 0xFF);
    buf[4] = (uint8_t)((off >> 8) & 0x1F);
    return 5;
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

#if PC_HAS_BUS

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
static Pca9685Ctx s_pca = {.addr = PC_PCA9685_I2C_ADDR, .freq = PC_PCA9685_FREQ};

static proto_bool wr(uint8_t reg, uint8_t val)
{
    s_pca.frame[0] = reg;
    s_pca.frame[1] = val;
    return pc_i2c_write(s_pca.addr, s_pca.frame, 2);
}

proto_bool pc_pca9685_begin(uint8_t addr, uint32_t freq_hz)
{
    s_pca.addr = addr ? addr : (uint8_t)PC_PCA9685_I2C_ADDR;
    s_pca.freq = freq_hz ? freq_hz : (uint32_t)PC_PCA9685_FREQ;
    pc_i2c_begin();
    proto_bool ok = PROTO_TRUE;
    ok &= wr(PCA9685_REG_MODE1, 0x10); // SLEEP (required before changing PRESCALE)
    ok &= wr(PCA9685_REG_PRESCALE, pc_pca9685_prescale(s_pca.freq));
    ok &= wr(PCA9685_REG_MODE1, 0x20); // wake, auto-increment (AI)
    pc_delay_us(500);                  // the oscillator settles before RESTART
    ok &= wr(PCA9685_REG_MODE1, 0xA0); // AI + RESTART
    ok &= wr(PCA9685_REG_MODE2, 0x04); // OUTDRV: totem-pole outputs
    return ok;
}

proto_bool pc_pca9685_set_pwm(uint8_t channel, uint16_t on, uint16_t off)
{
    if (pc_pca9685_set_pwm_bytes(s_pca.frame, sizeof(s_pca.frame), channel, on, off) != 5)
    {
        return PROTO_FALSE;
    }
    return pc_i2c_write(s_pca.addr, s_pca.frame, 5);
}

proto_bool pc_pca9685_set_servo_us(uint8_t channel, uint32_t microseconds)
{
    return pc_pca9685_set_pwm(channel, 0, pc_pca9685_us_to_count(microseconds, s_pca.freq));
}

#else // no bus seam. The prescale / count math + encoder above are host-tested.

proto_bool pc_pca9685_begin(uint8_t addr, uint32_t freq_hz)
{
    (void)addr;
    (void)freq_hz;
    return PROTO_FALSE;
}
proto_bool pc_pca9685_set_pwm(uint8_t channel, uint16_t on, uint16_t off)
{
    (void)channel;
    (void)on;
    (void)off;
    return PROTO_FALSE;
}
proto_bool pc_pca9685_set_servo_us(uint8_t channel, uint32_t microseconds)
{
    (void)channel;
    (void)microseconds;
    return PROTO_FALSE;
}

#endif // PC_HAS_BUS

#endif // PC_ENABLE_PCA9685
