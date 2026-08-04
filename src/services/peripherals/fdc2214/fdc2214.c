// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fdc2214.c
 * @brief FDC2114/2214 capacitance-to-digital codec + ESP32 binding (see fdc2214.h).
 */

#include "services/peripherals/fdc2214/fdc2214.h"
#include "protocore_config.h"

#if PC_ENABLE_FDC2214

#if PROTOCORE_HOT
#include "services/peripherals/i2c.h"
#include "shared_primitives/endian.h" // pc_wr16be / pc_rd16be: the registers are big-endian
#endif
uint32_t pc_fdc2214_data(uint16_t msb_reg, uint16_t lsb_reg)
{
    return ((uint32_t)(msb_reg & 0x0FFF) << 16) | lsb_reg;
}

uint8_t pc_fdc2214_error(uint16_t msb_reg)
{
    return (uint8_t)((msb_reg >> 12) & 0x0F);
}

uint64_t pc_fdc2214_sensor_freq_hz(uint32_t data28, uint32_t fref_hz)
{
    return ((uint64_t)data28 * fref_hz) >> 28;
}

size_t pc_fdc2214_build_config(uint8_t *buf, size_t cap, uint16_t rcount, uint16_t settlecount)
{
    if (!buf || cap < FDC2214_CONFIG_MAX)
    {
        return 0;
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
    return o;
}

#if PROTOCORE_HOT

// All FDC2214 I2C-binding state, owned by one instance (internal linkage): the device address, the
// register frame, and the bring-up sequence buffer, so it is one named owner, unreachable from any
// other translation unit. Both buffers are members rather than locals because a transfer is
// composed in place: a write is a register byte and a 16-bit value, and the bring-up sequence is
// the (register, msb, lsb) triples pc_fdc2214_build_config lays down.
typedef struct
{
    uint8_t addr;
    uint8_t frame[3];
    uint8_t config[FDC2214_CONFIG_MAX];
} Fdc2214Ctx;
static Fdc2214Ctx s_fdc = {.addr = 0x2A, .frame = {0}, .config = {0}};

static proto_bool read16(uint8_t reg, uint16_t *out)
{
    if (!pc_i2c_write_read(s_fdc.addr, &reg, 1, s_fdc.frame, 2))
    {
        return PROTO_FALSE;
    }
    *out = pc_rd16be(s_fdc.frame);
    return PROTO_TRUE;
}

static proto_bool write16(uint8_t reg, uint16_t val)
{
    s_fdc.frame[0] = reg;
    (void)pc_wr16be(&s_fdc.frame[1], val);
    return pc_i2c_write(s_fdc.addr, s_fdc.frame, sizeof(s_fdc.frame));
}

proto_bool pc_fdc2214_begin(uint8_t addr, uint16_t rcount, uint16_t settlecount)
{
    pc_i2c_begin();
    s_fdc.addr = addr;
    uint16_t id = 0;
    if (!read16(FDC2214_REG_DEVICE_ID, &id))
    {
        return PROTO_FALSE;
    }
    if (id != FDC2214_DEVICE_ID && id != 0x3054) // 0x3054 = FDC2114 (12-bit sibling)
    {
        return PROTO_FALSE;
    }
    size_t n = pc_fdc2214_build_config(s_fdc.config, sizeof(s_fdc.config), rcount, settlecount);
    for (size_t i = 0; i + 3 <= n; i += 3)
    {
        if (!write16(s_fdc.config[i], pc_rd16be(&s_fdc.config[i + 1])))
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

proto_bool pc_fdc2214_read_ch0(uint32_t *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    uint16_t msb = 0;
    uint16_t lsb = 0;
    if (!read16(FDC2214_REG_DATA_CH0_MSB, &msb) || !read16(FDC2214_REG_DATA_CH0_LSB, &lsb))
    {
        return PROTO_FALSE;
    }
    *out = pc_fdc2214_data(msb, lsb);
    return PROTO_TRUE;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_FDC2214
