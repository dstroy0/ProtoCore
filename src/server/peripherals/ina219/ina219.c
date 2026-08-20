// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ina219.c
 * @brief TI INA219 current / power monitor codec - implementation. See ina219.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_INA219

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_INA219 needs a bus master (an I2C master). Provide one in test/core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/endian/endian.h" // endian.wr16be / endian.rd16be: the registers are big-endian
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/peripherals/i2c.h"
#include "server/peripherals/ina219/ina219.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_I2C_DEVICE_BORROW persistent bytes
} Ina219OwnCtx;
static Ina219OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ina219_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_I2C_DEVICE_BORROW).buf;
    }
    return s_own.span;
}

void protocore_ina219_bus_mv(uint8_t *restrict work);
void protocore_ina219_calibration(uint8_t *restrict work);
void protocore_ina219_current_ua(uint8_t *restrict work);
void protocore_ina219_power_uw(uint8_t *restrict work);
void protocore_ina219_shunt_uv(uint8_t *restrict work);

void protocore_ina219_bus_mv(uint8_t *restrict work)
{
    (void)work;
    uint16_t raw = Ina219V.bus_mv_args.raw;

    Ina219V.value = (int32_t)((raw >> 3) * 4); // value in bits [15:3], LSB 4 mV
}

void protocore_ina219_shunt_uv(uint8_t *restrict work)
{
    (void)work;
    int16_t raw = Ina219V.shunt_uv_args.raw;

    Ina219V.value = (int32_t)raw * 10; // LSB 10 uV, signed
}

void protocore_ina219_calibration(uint8_t *restrict work)
{
    (void)work;
    uint32_t current_lsb_ua = Ina219V.calibration_args.current_lsb_ua;
    uint32_t shunt_mohm = Ina219V.calibration_args.shunt_mohm;

    uint32_t denom = current_lsb_ua * shunt_mohm;
    if (denom == 0)
    {
        Ina219V.cal = 0;
        return;
    }
    // 0.04096 / (lsb[A] * R[ohm]) = 40960000 / (lsb_ua * shunt_mohm).
    uint32_t cal = 40960000u / denom;
    Ina219V.cal = (uint16_t)(cal > 0xFFFF ? 0xFFFF : cal);
}

void protocore_ina219_current_ua(uint8_t *restrict work)
{
    (void)work;
    int16_t raw = Ina219V.current_ua_args.raw;
    uint32_t current_lsb_ua = Ina219V.current_ua_args.current_lsb_ua;

    Ina219V.value = (int32_t)((int64_t)raw * current_lsb_ua);
}

void protocore_ina219_power_uw(uint8_t *restrict work)
{
    (void)work;
    int16_t raw = Ina219V.power_uw_args.raw;
    uint32_t current_lsb_ua = Ina219V.power_uw_args.current_lsb_ua;

    Ina219V.value = (int32_t)((int64_t)raw * 20 * current_lsb_ua); // power LSB = 20 * current LSB
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define INA219_OFF_CTX 0u
static_assert(INA219_OFF_CTX + sizeof(Ina219Ctx) <= PROTOCORE_I2C_DEVICE_BORROW,
              "PROTOCORE_I2C_DEVICE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(INA219_OFF_CTX % _Alignof(Ina219Ctx) == 0,
              "INA219_OFF_CTX is not a multiple of alignof(Ina219Ctx) - INA219_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define INA219_CTX(w) ((Ina219Ctx *)(void *)((w) + INA219_OFF_CTX))

// Zero is "not set yet", which is the configured default - stated here rather than on the
// declaration so the context carries no initializer and can live in a borrow that arrives zeroed.
// begin() applies the same defaults to what it is handed.
static uint8_t dev_addr(uint8_t *restrict work)
{
    return INA219_CTX(work)->addr ? INA219_CTX(work)->addr : (uint8_t)PROTOCORE_INA219_I2C_ADDR;
}

// The current LSB in microamps, which every current and power reading is scaled by.
static uint32_t dev_lsb_ua(uint8_t *restrict work)
{
    return INA219_CTX(work)->lsb_ua ? INA219_CTX(work)->lsb_ua : (uint32_t)PROTOCORE_INA219_CURRENT_LSB_UA;
}

// One transaction: the register byte then its value, big-endian.
static proto_bool wr16(uint8_t *restrict work, uint8_t reg, uint16_t v)
{
    INA219_CTX(work)->frame[0] = reg;
    (void)endian.wr16be(&INA219_CTX(work)->frame[1], v);
    return protocore_i2c_write(dev_addr(work), INA219_CTX(work)->frame, sizeof(INA219_CTX(work)->frame));
}

// Name the register, then turn the bus around without releasing it (repeated start).
static proto_bool rd16(uint8_t *restrict work, uint8_t reg, uint16_t *v)
{
    if (!protocore_i2c_write_read(dev_addr(work), &reg, 1, INA219_CTX(work)->frame, 2))
    {
        return PROTO_FALSE;
    }
    *v = endian.rd16be(INA219_CTX(work)->frame);
    return PROTO_TRUE;
}

void protocore_ina219_begin(uint8_t *restrict work)
{
    uint8_t addr = Ina219V.begin_args.addr;
    uint32_t current_lsb_ua = Ina219V.begin_args.current_lsb_ua;
    uint32_t shunt_mohm = Ina219V.begin_args.shunt_mohm;

    INA219_CTX(work)->addr = addr ? addr : (uint8_t)PROTOCORE_INA219_I2C_ADDR;
    INA219_CTX(work)->lsb_ua = current_lsb_ua ? current_lsb_ua : (uint32_t)PROTOCORE_INA219_CURRENT_LSB_UA;
    protocore_i2c_begin();
    proto_bool ok = PROTO_TRUE;
    Ina219V.calibration_args.current_lsb_ua = dev_lsb_ua(work);
    Ina219V.calibration_args.shunt_mohm = shunt_mohm ? shunt_mohm : (uint32_t)PROTOCORE_INA219_SHUNT_MOHM;
    protocore_ina219_calibration(work);
    ok &= wr16(work, INA219_REG_CALIBRATION, Ina219V.cal);
    ok &= wr16(work, INA219_REG_CONFIG, 0x399F); // 32 V range, /8 gain (320 mV), 12-bit, continuous
    Ina219V.ok = ok;
}

void protocore_ina219_read_bus_mv(uint8_t *restrict work)
{
    int32_t *millivolts = Ina219V.read_bus_mv_args.millivolts;

    uint16_t v = 0;
    if (!rd16(work, INA219_REG_BUS, &v))
    {
        Ina219V.ok = PROTO_FALSE;
        return;
    }
    if (millivolts)
    {
        Ina219V.bus_mv_args.raw = v;
        protocore_ina219_bus_mv(work);
        *millivolts = Ina219V.value;
    }
    Ina219V.ok = PROTO_TRUE;
}

void protocore_ina219_read_shunt_uv(uint8_t *restrict work)
{
    int32_t *microvolts = Ina219V.read_shunt_uv_args.microvolts;

    uint16_t v = 0;
    if (!rd16(work, INA219_REG_SHUNT, &v))
    {
        Ina219V.ok = PROTO_FALSE;
        return;
    }
    if (microvolts)
    {
        Ina219V.shunt_uv_args.raw = (int16_t)v;
        protocore_ina219_shunt_uv(work);
        *microvolts = Ina219V.value;
    }
    Ina219V.ok = PROTO_TRUE;
}

void protocore_ina219_read_current_ua(uint8_t *restrict work)
{
    int32_t *microamps = Ina219V.read_current_ua_args.microamps;

    uint16_t v = 0;
    if (!rd16(work, INA219_REG_CURRENT, &v))
    {
        Ina219V.ok = PROTO_FALSE;
        return;
    }
    if (microamps)
    {
        Ina219V.current_ua_args.raw = (int16_t)v;
        Ina219V.current_ua_args.current_lsb_ua = dev_lsb_ua(work);
        protocore_ina219_current_ua(work);
        *microamps = Ina219V.value;
    }
    Ina219V.ok = PROTO_TRUE;
}

void protocore_ina219_read_power_uw(uint8_t *restrict work)
{
    int32_t *microwatts = Ina219V.read_power_uw_args.microwatts;

    uint16_t v = 0;
    if (!rd16(work, INA219_REG_POWER, &v))
    {
        Ina219V.ok = PROTO_FALSE;
        return;
    }
    if (microwatts)
    {
        Ina219V.power_uw_args.raw = (int16_t)v;
        Ina219V.power_uw_args.current_lsb_ua = dev_lsb_ua(work);
        protocore_ina219_power_uw(work);
        *microwatts = Ina219V.value;
    }
    Ina219V.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
Ina219Vars Ina219V;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_INA219
