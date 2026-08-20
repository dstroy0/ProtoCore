// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ina219.h
 * @brief TI INA219 high-side current / power monitor codec (PROTOCORE_ENABLE_INA219).
 *
 * The INA219 measures the voltage across a shunt resistor (LSB 10 uV) and the bus voltage (LSB
 * 4 mV, in the upper 13 bits of its register), and - once a calibration value derived from the
 * shunt resistance and a chosen current LSB is programmed - reports current and power directly.
 * From those you get how much current and power a circuit draws.
 *
 * This codec is pure and host-tested: ::protocore_ina219_bus_mv / ::protocore_ina219_shunt_uv decode the voltage
 * registers, ::protocore_ina219_calibration computes the calibration register, and ::protocore_ina219_current_ua /
 * ::protocore_ina219_power_uw scale the raw current / power registers by the current LSB. On an ESP32 the
 * binding programs the calibration + config and reads the registers over I2C (Wire); only that
 * touches hardware.
 *
 * A cheap solder-and-bench-test breakout: put it in series with a load and watch the current.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_INA219_H
#define PROTOCORE_INA219_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_INA219

PROTOCORE_BEGIN_DECLS

// PROTOCORE_I2C_DEVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define INA219_REG_CONFIG 0x00 ///< configuration

#define INA219_REG_SHUNT 0x01 ///< shunt voltage

#define INA219_REG_BUS 0x02 ///< bus voltage

#define INA219_REG_POWER 0x03 ///< power

#define INA219_REG_CURRENT 0x04 ///< current

#define INA219_REG_CALIBRATION 0x05 ///< calibration

/** @brief What bus_mv takes: raw. */
typedef struct
{
    uint16_t raw;
} Ina219BusMvArgs;
/** @brief What shunt_uv takes: raw. */
typedef struct
{
    int16_t raw;
} Ina219ShuntUvArgs;
/** @brief What calibration takes: current_lsb_ua, shunt_mohm. */
typedef struct
{
    uint32_t current_lsb_ua;
    uint32_t shunt_mohm;
} Ina219CalibrationArgs;
/** @brief What current_ua takes: raw, current_lsb_ua. */
typedef struct
{
    int16_t raw;
    uint32_t current_lsb_ua;
} Ina219CurrentUaArgs;
/** @brief What power_uw takes: raw, current_lsb_ua. */
typedef struct
{
    int16_t raw;
    uint32_t current_lsb_ua;
} Ina219PowerUwArgs;
/** @brief What begin takes: addr, current_lsb_ua, shunt_mohm. */
typedef struct
{
    uint8_t addr;
    uint32_t current_lsb_ua;
    uint32_t shunt_mohm;
} Ina219BeginArgs;
/** @brief What read_bus_mv takes: millivolts. */
typedef struct
{
    int32_t *millivolts;
} Ina219ReadBusMvArgs;
/** @brief What read_shunt_uv takes: microvolts. */
typedef struct
{
    int32_t *microvolts;
} Ina219ReadShuntUvArgs;
/** @brief What read_current_ua takes: microamps. */
typedef struct
{
    int32_t *microamps;
} Ina219ReadCurrentUaArgs;
/** @brief What read_power_uw takes: microwatts. */
typedef struct
{
    int32_t *microwatts;
} Ina219ReadPowerUwArgs;
/**
 * @brief TI INA219 high-side current / power monitor codec (PROTOCORE_ENABLE_INA219).
 *
 * A caller sets the members a call takes, invokes it through ::Ina219 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ina219.bus_mv_args.raw = ...;
 *   Ina219.bus_mv(work);
 *   // Ina219.value is what the call reports
 *
 * @var Ina219Ns::bus_mv_args  what bus_mv takes: raw
 * @var Ina219Ns::shunt_uv_args  what shunt_uv takes: raw
 * @var Ina219Ns::calibration_args  what calibration takes: current_lsb_ua, shunt_mohm
 * @var Ina219Ns::current_ua_args  what current_ua takes: raw, current_lsb_ua
 * @var Ina219Ns::power_uw_args  what power_uw takes: raw, current_lsb_ua
 * @var Ina219Ns::begin_args  what begin takes: addr, current_lsb_ua, shunt_mohm
 * @var Ina219Ns::read_bus_mv_args  what read_bus_mv takes: millivolts
 * @var Ina219Ns::read_shunt_uv_args  what read_shunt_uv takes: microvolts
 * @var Ina219Ns::read_current_ua_args  what read_current_ua takes: microamps
 * @var Ina219Ns::read_power_uw_args  what read_power_uw takes: microwatts
 * @var Ina219Ns::ok  a call's true/false outcome
 * @var Ina219Ns::value  the value a call reports
 * @var Ina219Ns::cal  what a call reports
 * @var Ina219Ns::bus_mv  decode the bus-voltage register to millivolts (value is bits ...
 * @var Ina219Ns::shunt_uv  decode the shunt-voltage register to microvolts (signed, LSB 10 uV)
 * @var Ina219Ns::calibration  compute the calibration register from the current LSB (microamps ...
 * @var Ina219Ns::current_ua  scale the raw current register to microamps (raw * current_lsb_ua)
 * @var Ina219Ns::power_uw  scale the raw power register to microwatts (power LSB is 20 * ...
 * @var Ina219Ns::begin  program the INA219 at addr: write the calibration for ...
 * @var Ina219Ns::read_bus_mv  read the bus voltage into millivolts. false on I2C error
 * @var Ina219Ns::read_shunt_uv  read the shunt voltage into microvolts. false on I2C error
 * @var Ina219Ns::read_current_ua  read the current into microamps (needs the calibration set by ...
 * @var Ina219Ns::read_power_uw  read the power into microwatts (needs the calibration set by ...
 *
 * @c work is PROTOCORE_I2C_DEVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Ina219BusMvArgs bus_mv_args;
    Ina219ShuntUvArgs shunt_uv_args;
    Ina219CalibrationArgs calibration_args;
    Ina219CurrentUaArgs current_ua_args;
    Ina219PowerUwArgs power_uw_args;
    Ina219BeginArgs begin_args;
    Ina219ReadBusMvArgs read_bus_mv_args;
    Ina219ReadShuntUvArgs read_shunt_uv_args;
    Ina219ReadCurrentUaArgs read_current_ua_args;
    Ina219ReadPowerUwArgs read_power_uw_args;
    proto_bool ok;
    int32_t value;
    uint16_t cal;
} Ina219Vars;

/** @brief The operands and the outcome. */
extern Ina219Vars Ina219V;

/** @brief The entries. */
typedef struct
{
    void (*const bus_mv)(uint8_t *restrict work);
    void (*const shunt_uv)(uint8_t *restrict work);
    void (*const calibration)(uint8_t *restrict work);
    void (*const current_ua)(uint8_t *restrict work);
    void (*const power_uw)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const read_bus_mv)(uint8_t *restrict work);
    void (*const read_shunt_uv)(uint8_t *restrict work);
    void (*const read_current_ua)(uint8_t *restrict work);
    void (*const read_power_uw)(uint8_t *restrict work);
} Ina219Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Ina219V or a region of the borrow at a fixed offset.
void protocore_ina219_bus_mv(uint8_t *restrict work);
void protocore_ina219_shunt_uv(uint8_t *restrict work);
void protocore_ina219_calibration(uint8_t *restrict work);
void protocore_ina219_current_ua(uint8_t *restrict work);
void protocore_ina219_power_uw(uint8_t *restrict work);
void protocore_ina219_begin(uint8_t *restrict work);
void protocore_ina219_read_bus_mv(uint8_t *restrict work);
void protocore_ina219_read_shunt_uv(uint8_t *restrict work);
void protocore_ina219_read_current_ua(uint8_t *restrict work);
void protocore_ina219_read_power_uw(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ina219.bus_mv(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Ina219Ns Ina219 __attribute__((unused)) = {
    .bus_mv = protocore_ina219_bus_mv,
    .shunt_uv = protocore_ina219_shunt_uv,
    .calibration = protocore_ina219_calibration,
    .current_ua = protocore_ina219_current_ua,
    .power_uw = protocore_ina219_power_uw,
    .begin = protocore_ina219_begin,
    .read_bus_mv = protocore_ina219_read_bus_mv,
    .read_shunt_uv = protocore_ina219_read_shunt_uv,
    .read_current_ua = protocore_ina219_read_current_ua,
    .read_power_uw = protocore_ina219_read_power_uw,
};

/**
 * @brief The PROTOCORE_I2C_DEVICE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_ina219_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_INA219

#endif // PROTOCORE_INA219_H
