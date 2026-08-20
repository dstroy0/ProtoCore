// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ldc1614.h
 * @brief TI LDC1614 inductance-to-digital field-sensing codec (PROTOCORE_ENABLE_LDC1614).
 *
 * The LDC1614 measures the resonant frequency of an external LC tank driven by a coil; a nearby
 * conductor changes the coil's effective inductance (eddy currents), moving that frequency - so the
 * 28-bit conversion result tracks metal proximity, displacement, and EM-field perturbation without
 * contact. It shares TI's FDC/LDC register architecture: each channel's result is a DATA MSB register
 * (top 4 bits error flags, low 12 bits data MSB) plus a DATA LSB register, combining into 28 bits, with
 * `f_sensor = data / 2^28 * f_ref` and `L = 1 / (C * (2*pi*f)^2)` derived by the app from the tank C.
 *
 * This codec is pure and host-tested: ::protocore_ldc1614_data combines the register pair, ::protocore_ldc1614_error
 * pulls the flags, ::protocore_ldc1614_sensor_freq_hz scales to frequency, and ::protocore_ldc1614_build_config emits a
 * single-channel bring-up. On an ESP32 the binding replays that config and reads the channel over I2C;
 * only that touches hardware. Bridge the readings northbound like any sensor.
 */

#ifndef PROTOCORE_LDC1614_H
#define PROTOCORE_LDC1614_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_LDC1614

PROTOCORE_BEGIN_DECLS

// PROTOCORE_I2C_DEVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define LDC1614_REG_DATA_CH0_MSB 0x00

#define LDC1614_REG_DATA_CH0_LSB 0x01

#define LDC1614_REG_RCOUNT_CH0 0x08

#define LDC1614_REG_SETTLECOUNT_CH0 0x10

#define LDC1614_REG_CLOCK_DIVIDERS_CH0 0x14

#define LDC1614_REG_STATUS 0x18

#define LDC1614_REG_ERROR_CONFIG 0x19

#define LDC1614_REG_CONFIG 0x1A

#define LDC1614_REG_MUX_CONFIG 0x1B

#define LDC1614_REG_DRIVE_CURRENT_CH0 0x1E

#define LDC1614_REG_MANUFACTURER_ID 0x7E

#define LDC1614_REG_DEVICE_ID 0x7F

#define LDC1614_MANUFACTURER_ID 0x5449 ///< "TI".

#define LDC1614_DEVICE_ID 0x3055 ///< LDC1614 / LDC1612.

#define LDC1614_CONFIG_MAX 21

/** @brief What data takes: msb_reg, lsb_reg. */
typedef struct
{
    uint16_t msb_reg;
    uint16_t lsb_reg;
} Ldc1614DataArgs;
/** @brief What error takes: msb_reg. */
typedef struct
{
    uint16_t msb_reg;
} Ldc1614ErrorArgs;
/** @brief What sensor_freq_hz takes: data28, fref_hz. */
typedef struct
{
    uint32_t data28;
    uint32_t fref_hz;
} Ldc1614SensorFreqHzArgs;
/** @brief What build_config takes: buf, cap, rcount, settlecount. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t rcount;
    uint16_t settlecount;
} Ldc1614BuildConfigArgs;
/** @brief What begin takes: addr, rcount, settlecount. */
typedef struct
{
    uint8_t addr;
    uint16_t rcount;
    uint16_t settlecount;
} Ldc1614BeginArgs;
/** @brief What read_ch0 takes: out. */
typedef struct
{
    uint32_t *out;
} Ldc1614ReadCh0Args;
/**
 * @brief TI LDC1614 inductance-to-digital field-sensing codec (PROTOCORE_ENABLE_LDC1614).
 *
 * A caller sets the members a call takes, invokes it through ::Ldc1614 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ldc1614.data_args.msb_reg = ...;
 *   Ldc1614.data_args.lsb_reg = ...;
 *   Ldc1614.data(work);
 *   // Ldc1614.value is what the call reports
 *
 * @var Ldc1614Ns::data_args  what data takes: msb_reg, lsb_reg
 * @var Ldc1614Ns::error_args  what error takes: msb_reg
 * @var Ldc1614Ns::sensor_freq_hz_args  what sensor_freq_hz takes: data28, fref_hz
 * @var Ldc1614Ns::build_config_args  what build_config takes: buf, cap, rcount, settlecount
 * @var Ldc1614Ns::begin_args  what begin takes: addr, rcount, settlecount
 * @var Ldc1614Ns::read_ch0_args  what read_ch0 takes: out
 * @var Ldc1614Ns::ok  a call's true/false outcome
 * @var Ldc1614Ns::value  the value a call reports
 * @var Ldc1614Ns::flags  what a call reports
 * @var Ldc1614Ns::hz  what a call reports
 * @var Ldc1614Ns::n  bytes written (7 * 3 = 21), or 0 if cap < LDC1614_CONFIG_MAX
 * @var Ldc1614Ns::data  combine a DATA MSB register (low 12 bits) and DATA LSB register ...
 * @var Ldc1614Ns::error  the 4 error flags from the top of a DATA MSB register (bits 15:12)
 * @var Ldc1614Ns::sensor_freq_hz  sensor frequency in Hz for a 28-bit result against a reference ...
 * @var Ldc1614Ns::build_config  emit a single-channel (CH0) continuous-conversion bring-up as ...
 * @var Ldc1614Ns::begin  verify the device id and apply the CH0 config at addr. true if ...
 * @var Ldc1614Ns::read_ch0  read channel 0's 28-bit conversion result into out. false on I2C ...
 *
 * @c work is PROTOCORE_I2C_DEVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Ldc1614DataArgs data_args;
    Ldc1614ErrorArgs error_args;
    Ldc1614SensorFreqHzArgs sensor_freq_hz_args;
    Ldc1614BuildConfigArgs build_config_args;
    Ldc1614BeginArgs begin_args;
    Ldc1614ReadCh0Args read_ch0_args;
    proto_bool ok;
    uint32_t value;
    uint8_t flags;
    uint64_t hz;
    size_t n;
} Ldc1614Vars;

/** @brief The operands and the outcome. */
extern Ldc1614Vars Ldc1614V;

/** @brief The entries. */
typedef struct
{
    void (*const data)(uint8_t *restrict work);
    void (*const error)(uint8_t *restrict work);
    void (*const sensor_freq_hz)(uint8_t *restrict work);
    void (*const build_config)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const read_ch0)(uint8_t *restrict work);
} Ldc1614Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Ldc1614V or a region of the borrow at a fixed offset.
void protocore_ldc1614_data(uint8_t *restrict work);
void protocore_ldc1614_error(uint8_t *restrict work);
void protocore_ldc1614_sensor_freq_hz(uint8_t *restrict work);
void protocore_ldc1614_build_config(uint8_t *restrict work);
void protocore_ldc1614_begin(uint8_t *restrict work);
void protocore_ldc1614_read_ch0(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ldc1614.data(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Ldc1614Ns Ldc1614 __attribute__((unused)) = {
    .data = protocore_ldc1614_data,
    .error = protocore_ldc1614_error,
    .sensor_freq_hz = protocore_ldc1614_sensor_freq_hz,
    .build_config = protocore_ldc1614_build_config,
    .begin = protocore_ldc1614_begin,
    .read_ch0 = protocore_ldc1614_read_ch0,
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
uint8_t *protocore_ldc1614_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LDC1614

#endif // PROTOCORE_LDC1614_H
