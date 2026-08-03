// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ldc1614.c
 * @brief LDC1614 inductance-to-digital codec + ESP32 binding (see ldc1614.h).
 */

#include "services/peripherals/ldc1614/ldc1614.h"
#include "protocore_config.h"

#if PC_ENABLE_LDC1614

#if PROTOCORE_HOT
#include "services/peripherals/i2c.h"
#include <Wire.h>
#endif
uint32_t pc_ldc1614_data(uint16_t msb_reg, uint16_t lsb_reg)
{
    return ((uint32_t)(msb_reg & 0x0FFF) << 16) | lsb_reg;
}

uint8_t pc_ldc1614_error(uint16_t msb_reg)
{
    return (uint8_t)((msb_reg >> 12) & 0x0F);
}

uint64_t pc_ldc1614_sensor_freq_hz(uint32_t data28, uint32_t fref_hz)
{
    return ((uint64_t)data28 * fref_hz) >> 28;
}

size_t pc_ldc1614_build_config(uint8_t *buf, size_t cap, uint16_t rcount, uint16_t settlecount)
{
    if (!buf || cap < LDC1614_CONFIG_MAX)
    {
        return 0;
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
    return o;
}

#if PROTOCORE_HOT

// All LDC1614 I2C-binding state, owned by one instance (internal linkage): the device address,
// so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    uint8_t addr = 0x2A;
} Ldc1614Ctx;
static Ldc1614Ctx s_ldc;

static proto_bool read16(uint8_t reg, uint16_t *out)
{
    Wire.beginTransmission(s_ldc.addr);
    Wire.write(reg);
    if (Wire.endTransmission(PROTO_FALSE) != 0)
    {
        return PROTO_FALSE;
    }
    if (Wire.requestFrom((int)s_ldc.addr, 2) != 2)
    {
        return PROTO_FALSE;
    }
    uint16_t hi = Wire.read();
    uint16_t lo = Wire.read();
    *out = (uint16_t)((hi << 8) | lo);
    return PROTO_TRUE;
}

static proto_bool write16(uint8_t reg, uint16_t val)
{
    Wire.beginTransmission(s_ldc.addr);
    Wire.write(reg);
    Wire.write((uint8_t)(val >> 8));
    Wire.write((uint8_t)val);
    return Wire.endTransmission() == 0;
}

proto_bool pc_ldc1614_begin(uint8_t addr, uint16_t rcount, uint16_t settlecount)
{
    pc_i2c_begin();
    s_ldc.addr = addr;
    uint16_t id = 0;
    if (!read16(LDC1614_REG_DEVICE_ID, &id))
    {
        return PROTO_FALSE;
    }
    if (id != LDC1614_DEVICE_ID)
    {
        return PROTO_FALSE;
    }
    uint8_t seq[LDC1614_CONFIG_MAX];
    size_t n = pc_ldc1614_build_config(seq, sizeof(seq), rcount, settlecount);
    for (size_t i = 0; i + 3 <= n; i += 3)
    {
        if (!write16(seq[i], (uint16_t)((seq[i + 1] << 8) | seq[i + 2])))
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

proto_bool pc_ldc1614_read_ch0(uint32_t *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    uint16_t msb = 0;
    uint16_t lsb = 0;
    if (!read16(LDC1614_REG_DATA_CH0_MSB, &msb) || !read16(LDC1614_REG_DATA_CH0_LSB, &lsb))
    {
        return PROTO_FALSE;
    }
    *out = pc_ldc1614_data(msb, lsb);
    return PROTO_TRUE;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_LDC1614
