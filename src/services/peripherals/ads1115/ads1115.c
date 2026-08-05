// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ads1115.c
 * @brief TI ADS1115 16-bit ADC codec - implementation. See ads1115.h.
 */

#include "services/peripherals/ads1115/ads1115.h"
#include "protocore_config.h"
#include "server/clock/clock.h" // pcdelay

#if PC_ENABLE_ADS1115

#if PROTOCORE_HOT || PC_PLATFORM_HAS_BUS
#include "services/peripherals/i2c.h"
#include "shared_primitives/endian.h" // pc_wr16be / pc_rd16be: the registers are big-endian
#endif
// Config-register field values (per the ADS1115 datasheet).
static const uint16_t OS_SINGLE = 0x8000;   // start a single conversion
static const uint16_t MUX_SINGLE0 = 0x4000; // single-ended AIN0; AINx = this | (channel << 12)
static const uint16_t MODE_SINGLE = 0x0100; // single-shot
static const uint16_t COMP_DISABLE = 0x0003;

// PGA bits and the matching full-scale range in microvolts, indexed by gain code.
static const uint16_t PGA_BITS[6] = {0x0000, 0x0200, 0x0400, 0x0600, 0x0800, 0x0A00};
static const int32_t FSR_UV[6] = {6144000, 4096000, 2048000, 1024000, 512000, 256000};

uint16_t pc_ads1115_config_single(uint8_t channel, uint8_t gain, uint8_t dr)
{
    if (channel > 3)
    {
        channel = 0;
    }
    if (gain > ADS1115_GAIN_16)
    {
        gain = (uint8_t)PC_ADS1115_GAIN;
    }
    if (dr > ADS1115_DR_860)
    {
        dr = (uint8_t)PC_ADS1115_DR;
    }
    uint16_t cfg = OS_SINGLE;
#if PC_ADS1115_DIFFERENTIAL
    cfg |= (uint16_t)((uint16_t)channel << 12); // differential pair: MUX 0=AIN0-1, 1=AIN0-3, 2=AIN1-3, 3=AIN2-3
#else
    cfg |= (uint16_t)(MUX_SINGLE0 | ((uint16_t)channel << 12)); // single-ended AINx
#endif
    cfg |= PGA_BITS[gain];
    cfg |= MODE_SINGLE;
    cfg |= (uint16_t)((uint16_t)dr << 5); // data-rate bits [7:5]
    cfg |= COMP_DISABLE;
    return cfg;
}

int32_t pc_ads1115_raw_to_uv(int16_t raw, uint8_t gain)
{
    if (gain > ADS1115_GAIN_16)
    {
        gain = (uint8_t)PC_ADS1115_GAIN;
    }
    return (int32_t)((int64_t)raw * FSR_UV[gain] / 32768);
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT || PC_PLATFORM_HAS_BUS

// All ADS1115 I2C-binding state, owned by one instance (internal linkage): the device address and
// the bus frame, so it is one named owner, unreachable from any other translation unit. The frame
// is a member rather than a local because a transfer is composed in place, and three bytes is the
// widest this part moves, a register byte plus a 16-bit register value.
typedef struct
{
    uint8_t addr;
    uint8_t frame[3];
} Ads1115Ctx;
static Ads1115Ctx s_ads = {.addr = PC_ADS1115_I2C_ADDR, .frame = {0}};

static proto_bool wr16(uint8_t reg, uint16_t v)
{
    s_ads.frame[0] = reg;
    (void)pc_wr16be(&s_ads.frame[1], v);
    return pc_i2c_write(s_ads.addr, s_ads.frame, sizeof(s_ads.frame));
}

static proto_bool rd16(uint8_t reg, uint16_t *v)
{
    if (!pc_i2c_write_read(s_ads.addr, &reg, 1, s_ads.frame, 2))
    {
        return PROTO_FALSE;
    }
    *v = pc_rd16be(s_ads.frame);
    return PROTO_TRUE;
}

proto_bool pc_ads1115_begin(uint8_t addr)
{
    s_ads.addr = addr ? addr : (uint8_t)PC_ADS1115_I2C_ADDR;
    pc_i2c_begin();
    return PROTO_TRUE;
}

proto_bool pc_ads1115_read_raw(uint8_t channel, uint8_t gain, int16_t *raw)
{
    if (!raw)
    {
        return PROTO_FALSE;
    }
    uint8_t dr = (uint8_t)PC_ADS1115_DR;
    if (dr > ADS1115_DR_860)
    {
        dr = ADS1115_DR_128;
    }
    if (!wr16(ADS1115_REG_CONFIG, pc_ads1115_config_single(channel, gain, dr)))
    {
        return PROTO_FALSE;
    }
    // Single-shot conversion time tracks the data rate (~1000/SPS ms); wait it out plus a 1 ms margin.
    static const uint16_t pc_ads1115_sps[8] = {8, 16, 32, 64, 128, 250, 475, 860};
    pcdelay(1000u / pc_ads1115_sps[dr] + 1);
    uint16_t v = 0;
    if (!rd16(ADS1115_REG_CONVERSION, &v))
    {
        return PROTO_FALSE;
    }
    *raw = (int16_t)v;
    return PROTO_TRUE;
}

proto_bool pc_ads1115_read_uv(uint8_t channel, uint8_t gain, int32_t *microvolts)
{
    int16_t raw = 0;
    if (!pc_ads1115_read_raw(channel, gain, &raw))
    {
        return PROTO_FALSE;
    }
    if (microvolts)
    {
        *microvolts = pc_ads1115_raw_to_uv(raw, gain);
    }
    return PROTO_TRUE;
}

#else // host build: no I2C. The config encoder + conversion above are host-tested.

proto_bool pc_ads1115_begin(uint8_t addr)
{
    (void)addr;
    return PROTO_FALSE;
}
proto_bool pc_ads1115_read_raw(uint8_t channel, uint8_t gain, int16_t *raw)
{
    (void)channel;
    (void)gain;
    (void)raw;
    return PROTO_FALSE;
}
proto_bool pc_ads1115_read_uv(uint8_t channel, uint8_t gain, int32_t *microvolts)
{
    (void)channel;
    (void)gain;
    (void)microvolts;
    return PROTO_FALSE;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_ADS1115
