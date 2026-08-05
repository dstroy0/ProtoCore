// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ina219.c
 * @brief TI INA219 current / power monitor codec - implementation. See ina219.h.
 */

#include "services/peripherals/ina219/ina219.h"
#include "protocore_config.h"

#if PC_ENABLE_INA219

#if PROTOCORE_HOT || PC_PLATFORM_HAS_BUS
#include "services/peripherals/i2c.h"
#include "mmgr/endian.h" // pc_wr16be / pc_rd16be: the registers are big-endian
#endif
int32_t pc_ina219_bus_mv(uint16_t raw)
{
    return (int32_t)((raw >> 3) * 4); // value in bits [15:3], LSB 4 mV
}

int32_t pc_ina219_shunt_uv(int16_t raw)
{
    return (int32_t)raw * 10; // LSB 10 uV, signed
}

uint16_t pc_ina219_calibration(uint32_t current_lsb_ua, uint32_t shunt_mohm)
{
    uint32_t denom = current_lsb_ua * shunt_mohm;
    if (denom == 0)
    {
        return 0;
    }
    // 0.04096 / (lsb[A] * R[ohm]) = 40960000 / (lsb_ua * shunt_mohm).
    uint32_t cal = 40960000u / denom;
    return (uint16_t)(cal > 0xFFFF ? 0xFFFF : cal);
}

int32_t pc_ina219_current_ua(int16_t raw, uint32_t current_lsb_ua)
{
    return (int32_t)((int64_t)raw * current_lsb_ua);
}

int32_t pc_ina219_power_uw(int16_t raw, uint32_t current_lsb_ua)
{
    return (int32_t)((int64_t)raw * 20 * current_lsb_ua); // power LSB = 20 * current LSB
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT || PC_PLATFORM_HAS_BUS

// All INA219 I2C-binding state, owned by one instance (internal linkage): the device address,
// the current LSB, and the bus frame, grouped so it is one named owner, unreachable from any
// other TU. The frame is a member rather than a local because a bus transfer is composed in
// place: three bytes is the widest this part sends (a register byte plus a 16-bit value).
typedef struct
{
    uint8_t addr;
    uint32_t lsb_ua;
    uint8_t frame[3];
} Ina219Ctx;
static Ina219Ctx s_ina = {.addr = PC_INA219_I2C_ADDR, .lsb_ua = PC_INA219_CURRENT_LSB_UA};

// One transaction: the register byte then its value, big-endian.
static proto_bool wr16(uint8_t reg, uint16_t v)
{
    s_ina.frame[0] = reg;
    (void)pc_wr16be(&s_ina.frame[1], v);
    return pc_i2c_write(s_ina.addr, s_ina.frame, sizeof(s_ina.frame));
}

// Name the register, then turn the bus around without releasing it (repeated start).
static proto_bool rd16(uint8_t reg, uint16_t *v)
{
    if (!pc_i2c_write_read(s_ina.addr, &reg, 1, s_ina.frame, 2))
    {
        return PROTO_FALSE;
    }
    *v = pc_rd16be(s_ina.frame);
    return PROTO_TRUE;
}

proto_bool pc_ina219_begin(uint8_t addr, uint32_t current_lsb_ua, uint32_t shunt_mohm)
{
    s_ina.addr = addr ? addr : (uint8_t)PC_INA219_I2C_ADDR;
    s_ina.lsb_ua = current_lsb_ua ? current_lsb_ua : (uint32_t)PC_INA219_CURRENT_LSB_UA;
    pc_i2c_begin();
    proto_bool ok = PROTO_TRUE;
    ok &= wr16(INA219_REG_CALIBRATION,
               pc_ina219_calibration(s_ina.lsb_ua, shunt_mohm ? shunt_mohm : (uint32_t)PC_INA219_SHUNT_MOHM));
    ok &= wr16(INA219_REG_CONFIG, 0x399F); // 32 V range, /8 gain (320 mV), 12-bit, continuous
    return ok;
}

proto_bool pc_ina219_read_bus_mv(int32_t *millivolts)
{
    uint16_t v = 0;
    if (!rd16(INA219_REG_BUS, &v))
    {
        return PROTO_FALSE;
    }
    if (millivolts)
    {
        *millivolts = pc_ina219_bus_mv(v);
    }
    return PROTO_TRUE;
}

proto_bool pc_ina219_read_shunt_uv(int32_t *microvolts)
{
    uint16_t v = 0;
    if (!rd16(INA219_REG_SHUNT, &v))
    {
        return PROTO_FALSE;
    }
    if (microvolts)
    {
        *microvolts = pc_ina219_shunt_uv((int16_t)v);
    }
    return PROTO_TRUE;
}

proto_bool pc_ina219_read_current_ua(int32_t *microamps)
{
    uint16_t v = 0;
    if (!rd16(INA219_REG_CURRENT, &v))
    {
        return PROTO_FALSE;
    }
    if (microamps)
    {
        *microamps = pc_ina219_current_ua((int16_t)v, s_ina.lsb_ua);
    }
    return PROTO_TRUE;
}

proto_bool pc_ina219_read_power_uw(int32_t *microwatts)
{
    uint16_t v = 0;
    if (!rd16(INA219_REG_POWER, &v))
    {
        return PROTO_FALSE;
    }
    if (microwatts)
    {
        *microwatts = pc_ina219_power_uw((int16_t)v, s_ina.lsb_ua);
    }
    return PROTO_TRUE;
}

#else // host build: no I2C. The decode / calibration / scaling above are host-tested.

proto_bool pc_ina219_begin(uint8_t addr, uint32_t current_lsb_ua, uint32_t shunt_mohm)
{
    (void)addr;
    (void)current_lsb_ua;
    (void)shunt_mohm;
    return PROTO_FALSE;
}
proto_bool pc_ina219_read_bus_mv(int32_t *millivolts)
{
    (void)millivolts;
    return PROTO_FALSE;
}
proto_bool pc_ina219_read_shunt_uv(int32_t *microvolts)
{
    (void)microvolts;
    return PROTO_FALSE;
}
proto_bool pc_ina219_read_current_ua(int32_t *microamps)
{
    (void)microamps;
    return PROTO_FALSE;
}
proto_bool pc_ina219_read_power_uw(int32_t *microwatts)
{
    (void)microwatts;
    return PROTO_FALSE;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_INA219
