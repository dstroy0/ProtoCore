// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file vl53l0x.c
 * @brief VL53L0X time-of-flight ranging codec + ESP32 binding (see vl53l0x.h).
 */

#include "services/peripherals/vl53l0x/vl53l0x.h"
#include "protocore_config.h"

#if PC_ENABLE_VL53L0X

#if PROTOCORE_HOT || PC_PLATFORM_HAS_BUS
#include "services/peripherals/i2c.h"
#endif
uint16_t pc_vl53l0x_range_mm(uint8_t hi, uint8_t lo)
{
    return (uint16_t)((hi << 8) | lo);
}

proto_bool pc_vl53l0x_data_ready(uint8_t interrupt_status)
{
    return (interrupt_status & 0x07) != 0;
}

uint8_t pc_vl53l0x_range_status(uint8_t range_status_reg)
{
    return (uint8_t)((range_status_reg >> 3) & 0x0F);
}

proto_bool pc_vl53l0x_range_valid(uint8_t range_status_reg)
{
    return pc_vl53l0x_range_status(range_status_reg) == VL53L0X_RANGE_VALID;
}

#if PROTOCORE_HOT || PC_PLATFORM_HAS_BUS

// All VL53L0X I2C-binding state, owned by one instance (internal linkage): the device address, the
// register-pair frame, and the result block, so it is one named owner, unreachable from any other
// translation unit. Both buffers are members rather than locals because a transfer is composed in
// place: a write is one register byte and its value, and a range read is the twelve result
// registers from RESULT_RANGE_STATUS onward.
typedef struct
{
    uint8_t addr;
    uint8_t frame[2];
    uint8_t result[12];
} Vl53l0xCtx;
static Vl53l0xCtx s_vl = {.addr = 0x29, .frame = {0}, .result = {0}};

static proto_bool w8(uint8_t reg, uint8_t val)
{
    s_vl.frame[0] = reg;
    s_vl.frame[1] = val;
    return pc_i2c_write(s_vl.addr, s_vl.frame, sizeof(s_vl.frame));
}

static proto_bool r8(uint8_t reg, uint8_t *val)
{
    return pc_i2c_write_read(s_vl.addr, &reg, 1, val, 1);
}

static proto_bool rn(uint8_t reg, uint8_t *buf, uint8_t n)
{
    return pc_i2c_write_read(s_vl.addr, &reg, 1, buf, n);
}

proto_bool pc_vl53l0x_begin(uint8_t addr)
{
    pc_i2c_begin();
    s_vl.addr = addr;
    uint8_t id = 0;
    if (!r8(VL53L0X_REG_IDENTIFICATION_MODEL_ID, &id) || id != VL53L0X_MODEL_ID)
    {
        return PROTO_FALSE;
    }
    return w8(VL53L0X_REG_SYSRANGE_START, 0x02); // continuous back-to-back ranging
}

proto_bool pc_vl53l0x_read_mm(uint16_t *mm)
{
    if (!mm)
    {
        return PROTO_FALSE;
    }
    uint8_t irq = 0;
    if (!r8(VL53L0X_REG_RESULT_INTERRUPT_STATUS, &irq) || !pc_vl53l0x_data_ready(irq))
    {
        return PROTO_FALSE;
    }
    if (!rn(VL53L0X_REG_RESULT_RANGE_STATUS, s_vl.result, (uint8_t)sizeof(s_vl.result)))
    {
        return PROTO_FALSE;
    }
    proto_bool valid = pc_vl53l0x_range_valid(s_vl.result[0]);
    *mm = pc_vl53l0x_range_mm(s_vl.result[10], s_vl.result[11]); // distance at RESULT_RANGE_STATUS + 10/11
    (void)w8(VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
    return valid;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_VL53L0X
