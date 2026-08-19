// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fdc2214.h
 * @brief TI FDC2114/2214 capacitance-to-digital field-sensing codec (PROTOCORE_ENABLE_FDC2214).
 *
 * The FDC2x14 measures the resonant frequency of an external LC tank; a capacitance shift (a finger
 * near the electrode, a liquid rising past it, a material change) moves that frequency, so watching the
 * 28-bit conversion result gives proximity / level / material sensing without contact. Each channel's
 * result is two 16-bit registers - a DATA MSB register whose top 4 bits are error flags and low 12 bits
 * are the data MSB, and a DATA LSB register - which combine into the 28-bit reading. `f_sensor =
 * data / 2^28 * f_ref`.
 *
 * This codec is pure and host-tested: ::protocore_fdc2214_data combines the register pair, ::protocore_fdc2214_error
 * pulls the flags, ::protocore_fdc2214_sensor_freq_hz scales to frequency, and ::protocore_fdc2214_build_config emits a
 * single-channel continuous-conversion bring-up as `(reg, msb, lsb)` triples. On an ESP32 the binding
 * replays that config and reads the channel over I2C (Wire); only that touches hardware. Bridge the
 * readings northbound like any sensor.
 */

#ifndef PROTOCORE_FDC2214_H
#define PROTOCORE_FDC2214_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_FDC2214

PROTOCORE_BEGIN_DECLS

// PROTOCORE_I2C_DEVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define FDC2214_REG_DATA_CH0_MSB 0x00

#define FDC2214_REG_DATA_CH0_LSB 0x01

#define FDC2214_REG_RCOUNT_CH0 0x08

#define FDC2214_REG_SETTLECOUNT_CH0 0x10

#define FDC2214_REG_CLOCK_DIVIDERS_CH0 0x14

#define FDC2214_REG_STATUS 0x18

#define FDC2214_REG_ERROR_CONFIG 0x19

#define FDC2214_REG_CONFIG 0x1A

#define FDC2214_REG_MUX_CONFIG 0x1B

#define FDC2214_REG_DRIVE_CURRENT_CH0 0x1E

#define FDC2214_REG_MANUFACTURER_ID 0x7E

#define FDC2214_REG_DEVICE_ID 0x7F

#define FDC2214_MANUFACTURER_ID 0x5449 ///< "TI".

#define FDC2214_DEVICE_ID 0x3055 ///< FDC2214 (the 12-bit FDC2114 reads 0x3054).

#define FDC2214_CONFIG_MAX 21

/** @brief What data takes: msb_reg, lsb_reg. */
typedef struct
{
    uint16_t msb_reg;
    uint16_t lsb_reg;
} Fdc2214DataArgs;

/** @brief What error takes: msb_reg. */
typedef struct
{
    uint16_t msb_reg;
} Fdc2214ErrorArgs;

/** @brief What sensor_freq_hz takes: data28, fref_hz. */
typedef struct
{
    uint32_t data28;
    uint32_t fref_hz;
} Fdc2214SensorFreqHzArgs;

/** @brief What build_config takes: buf, cap, rcount, settlecount. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t rcount;
    uint16_t settlecount;
} Fdc2214BuildConfigArgs;

/** @brief What begin takes: addr, rcount, settlecount. */
typedef struct
{
    uint8_t addr;
    uint16_t rcount;
    uint16_t settlecount;
} Fdc2214BeginArgs;

/** @brief What read_ch0 takes: out. */
typedef struct
{
    uint32_t *out;
} Fdc2214ReadCh0Args;

/**
 * @brief TI FDC2114/2214 capacitance-to-digital field-sensing codec (PROTOCORE_ENABLE_FDC2214).
 *
 * A caller sets the members a call takes, invokes it through ::Fdc2214 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Fdc2214.data_args.msb_reg = ...;
 *   Fdc2214.data_args.lsb_reg = ...;
 *   Fdc2214.data(work);
 *   // Fdc2214.value is what the call reports
 *
 * @var Fdc2214Ns::data_args  what data takes: msb_reg, lsb_reg
 * @var Fdc2214Ns::error_args  what error takes: msb_reg
 * @var Fdc2214Ns::sensor_freq_hz_args  what sensor_freq_hz takes: data28, fref_hz
 * @var Fdc2214Ns::build_config_args  what build_config takes: buf, cap, rcount, settlecount
 * @var Fdc2214Ns::begin_args  what begin takes: addr, rcount, settlecount
 * @var Fdc2214Ns::read_ch0_args  what read_ch0 takes: out
 * @var Fdc2214Ns::ok  a call's true/false outcome
 * @var Fdc2214Ns::value  the value a call reports
 * @var Fdc2214Ns::flags  what a call reports
 * @var Fdc2214Ns::hz  what a call reports
 * @var Fdc2214Ns::n  bytes written (7 * 3 = 21), or 0 if cap < FDC2214_CONFIG_MAX
 * @var Fdc2214Ns::data  combine a DATA MSB register (low 12 bits) and DATA LSB register ...
 * @var Fdc2214Ns::error  the 4 error flags from the top of a DATA MSB register (bits 15:12)
 * @var Fdc2214Ns::sensor_freq_hz  sensor frequency in Hz for a 28-bit result against a reference ...
 * @var Fdc2214Ns::build_config  emit a single-channel (CH0) continuous-conversion bring-up as ...
 * @var Fdc2214Ns::begin  verify the device id and apply the CH0 config at addr. true if ...
 * @var Fdc2214Ns::read_ch0  read channel 0's 28-bit conversion result into out. false on I2C ...
 *
 * @c work is PROTOCORE_I2C_DEVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Fdc2214DataArgs data_args;
    Fdc2214ErrorArgs error_args;
    Fdc2214SensorFreqHzArgs sensor_freq_hz_args;
    Fdc2214BuildConfigArgs build_config_args;
    Fdc2214BeginArgs begin_args;
    Fdc2214ReadCh0Args read_ch0_args;

    proto_bool ok;
    uint32_t value;
    uint8_t flags;
    uint64_t hz;
    size_t n;

    void (*const data)(uint8_t *restrict work);
    void (*const error)(uint8_t *restrict work);
    void (*const sensor_freq_hz)(uint8_t *restrict work);
    void (*const build_config)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const read_ch0)(uint8_t *restrict work);
} Fdc2214Ns;

/** @brief The one symbol this module exports. */
extern Fdc2214Ns Fdc2214;

/**
 * @brief The PROTOCORE_I2C_DEVICE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_fdc2214_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FDC2214

#endif // PROTOCORE_FDC2214_H
