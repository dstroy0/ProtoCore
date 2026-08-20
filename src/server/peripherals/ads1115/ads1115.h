// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ads1115.h
 * @brief TI ADS1115 16-bit ADC codec (PROTOCORE_ENABLE_ADS1115).
 *
 * The ADS1115 is a 4-channel 16-bit analog-to-digital converter on the I2C bus with a
 * programmable-gain amplifier - far more resolution and range control than the ESP32's own ADC.
 * A reading is a 16-bit config-register write (start, channel, gain, mode, data rate) followed
 * by a 16-bit read of the conversion register; the signed result scales to a voltage by the
 * selected gain's full-scale range.
 *
 * This codec is pure and host-tested: ::protocore_ads1115_config_single builds the config word for a
 * single-shot single-ended reading, and ::protocore_ads1115_raw_to_uv converts the signed sample to
 * microvolts. On an ESP32 the binding writes the config, waits for the conversion, and reads it
 * back over I2C (Wire); only that touches hardware.
 *
 * A cheap solder-and-bench-test breakout: measure a battery, a potentiometer, or an analog
 * sensor and bridge the reading onto the network.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ADS1115_H
#define PROTOCORE_ADS1115_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_ADS1115

PROTOCORE_BEGIN_DECLS

// PROTOCORE_I2C_DEVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define ADS1115_REG_CONVERSION 0x00 ///< conversion result register

#define ADS1115_REG_CONFIG 0x01 ///< configuration register

#define ADS1115_GAIN_TWOTHIRDS 0 ///< +/- 6.144 V

#define ADS1115_GAIN_1 1 ///< +/- 4.096 V

#define ADS1115_GAIN_2 2 ///< +/- 2.048 V (default)

#define ADS1115_GAIN_4 3 ///< +/- 1.024 V

#define ADS1115_GAIN_8 4 ///< +/- 0.512 V

#define ADS1115_GAIN_16 5 ///< +/- 0.256 V

#define ADS1115_DR_8 0 ///< 8 SPS

#define ADS1115_DR_16 1 ///< 16 SPS

#define ADS1115_DR_32 2 ///< 32 SPS

#define ADS1115_DR_64 3 ///< 64 SPS

#define ADS1115_DR_128 4 ///< 128 SPS (default)

#define ADS1115_DR_250 5 ///< 250 SPS

#define ADS1115_DR_475 6 ///< 475 SPS

#define ADS1115_DR_860 7 ///< 860 SPS

/** @brief What config_single takes: channel, gain, dr. */
typedef struct
{
    uint8_t channel;
    uint8_t gain;
    uint8_t dr;
} Ads1115ConfigSingleArgs;
/** @brief What raw_to_uv takes: raw, gain. */
typedef struct
{
    int16_t raw;
    uint8_t gain;
} Ads1115RawToUvArgs;
/** @brief What begin takes: addr. */
typedef struct
{
    uint8_t addr;
} Ads1115BeginArgs;
/** @brief What read_raw takes: channel, gain, raw. */
typedef struct
{
    uint8_t channel;
    uint8_t gain;
    int16_t *raw;
} Ads1115ReadRawArgs;
/** @brief What read_uv takes: channel, gain, microvolts. */
typedef struct
{
    uint8_t channel;
    uint8_t gain;
    int32_t *microvolts;
} Ads1115ReadUvArgs;
/**
 * @brief TI ADS1115 16-bit ADC codec (PROTOCORE_ENABLE_ADS1115). The ADS1115 is a 4-channel 16-bit analog-to-digital
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::Ads1115 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ads1115.config_single_args.channel = ...;
 *   Ads1115.config_single_args.gain = ...;
 *   Ads1115.config_single_args.dr = ...;
 *   Ads1115.config_single(work);
 *   // Ads1115.word is what the call reports
 *
 * @var Ads1115Ns::config_single_args  what config_single takes: channel, gain, dr
 * @var Ads1115Ns::raw_to_uv_args  what raw_to_uv takes: raw, gain
 * @var Ads1115Ns::begin_args  what begin takes: addr
 * @var Ads1115Ns::read_raw_args  what read_raw takes: channel, gain, raw
 * @var Ads1115Ns::read_uv_args  what read_uv takes: channel, gain, microvolts
 * @var Ads1115Ns::ok  a call's true/false outcome
 * @var Ads1115Ns::word  what a call reports
 * @var Ads1115Ns::uv  what a call reports
 * @var Ads1115Ns::config_single  build the 16-bit config word for a single-shot, single-ended ...
 * @var Ads1115Ns::raw_to_uv  convert a signed 16-bit sample to microvolts for gain's full-scale ...
 * @var Ads1115Ns::begin  initialize the I2C bus for the ADS1115 at addr. true on ESP32
 * @var Ads1115Ns::read_raw  single-shot read of channel (0..3) at gain into raw. false on error
 * @var Ads1115Ns::read_uv  single-shot read of channel at gain, converted to microvolts in ...
 *
 * @c work is PROTOCORE_I2C_DEVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Ads1115ConfigSingleArgs config_single_args;
    Ads1115RawToUvArgs raw_to_uv_args;
    Ads1115BeginArgs begin_args;
    Ads1115ReadRawArgs read_raw_args;
    Ads1115ReadUvArgs read_uv_args;
    proto_bool ok;
    uint16_t word;
    int32_t uv;
} Ads1115Vars;

/** @brief The operands and the outcome. */
extern Ads1115Vars Ads1115V;

/** @brief The entries. */
typedef struct
{
    void (*const config_single)(uint8_t *restrict work);
    void (*const raw_to_uv)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const read_raw)(uint8_t *restrict work);
    void (*const read_uv)(uint8_t *restrict work);
} Ads1115Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Ads1115V or a region of the borrow at a fixed offset.
void protocore_ads1115_config_single(uint8_t *restrict work);
void protocore_ads1115_raw_to_uv(uint8_t *restrict work);
void protocore_ads1115_begin(uint8_t *restrict work);
void protocore_ads1115_read_raw(uint8_t *restrict work);
void protocore_ads1115_read_uv(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ads1115.config_single(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Ads1115Ns Ads1115 __attribute__((unused)) = {
    .config_single = protocore_ads1115_config_single,
    .raw_to_uv = protocore_ads1115_raw_to_uv,
    .begin = protocore_ads1115_begin,
    .read_raw = protocore_ads1115_read_raw,
    .read_uv = protocore_ads1115_read_uv,
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
uint8_t *protocore_ads1115_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ADS1115

#endif // PROTOCORE_ADS1115_H
