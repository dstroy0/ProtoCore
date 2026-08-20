// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pca9685.h
 * @brief NXP PCA9685 16-channel 12-bit PWM / servo driver codec (PROTOCORE_ENABLE_PCA9685).
 *
 * The PCA9685 generates sixteen independent 12-bit PWM outputs from a 25 MHz oscillator. The
 * output frequency is set by a PRESCALE register value; each channel is four registers (a 12-bit
 * ON count and a 12-bit OFF count) at `0x06 + 4 * channel`. Driving a hobby servo is a matter of
 * turning a pulse width (in microseconds) into an OFF count at the configured frequency.
 *
 * This codec is pure and host-tested: ::protocore_pca9685_prescale computes the prescale for a frequency,
 * ::protocore_pca9685_us_to_count converts a servo pulse width to a 12-bit count, ::protocore_pca9685_channel_reg
 * gives a channel's register base, and ::protocore_pca9685_set_pwm_bytes emits the 5-byte channel write.
 * On an ESP32 the binding replays those writes over I2C (Wire); only that touches hardware.
 *
 * A cheap solder-and-bench-test breakout for driving up to 16 servos or LEDs: wire it up, sweep
 * a servo.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PCA9685_H
#define PROTOCORE_PCA9685_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_PCA9685

PROTOCORE_BEGIN_DECLS

// PROTOCORE_I2C_DEVICE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define PCA9685_CHANNELS 16 ///< PWM output channels

#define PCA9685_COUNT_MAX 4095 ///< a PWM count is 12-bit (0..4095)

#define PCA9685_FULL_ON 0x1000 ///< pass as `on` for a channel fully on (bit 12 flag)

#define PCA9685_FULL_OFF 0x1000 ///< pass as `off` for a channel fully off (bit 12 flag)

#define PCA9685_REG_MODE1 0x00

#define PCA9685_REG_MODE2 0x01

#define PCA9685_REG_LED0_ON_L 0x06 ///< channel 0 base; channel n is this + 4*n

#define PCA9685_REG_PRESCALE 0xFE

/** @brief What prescale takes: freq_hz. */
typedef struct
{
    uint32_t freq_hz;
} Pca9685PrescaleArgs;
/** @brief What channel_reg takes: channel. */
typedef struct
{
    uint8_t channel;
} Pca9685ChannelRegArgs;
/** @brief What us_to_count takes: microseconds, freq_hz. */
typedef struct
{
    uint32_t microseconds;
    uint32_t freq_hz;
} Pca9685UsToCountArgs;
/** @brief What set_pwm_bytes takes: buf, cap, channel, on, off. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t channel;
    uint16_t on;
    uint16_t off;
} Pca9685SetPwmBytesArgs;
/** @brief What begin takes: addr, freq_hz. */
typedef struct
{
    uint8_t addr;
    uint32_t freq_hz;
} Pca9685BeginArgs;
/** @brief What set_pwm takes: channel, on, off. */
typedef struct
{
    uint8_t channel;
    uint16_t on;
    uint16_t off;
} Pca9685SetPwmArgs;
/** @brief What set_servo_us takes: channel, microseconds. */
typedef struct
{
    uint8_t channel;
    uint32_t microseconds;
} Pca9685SetServoUsArgs;
/**
 * @brief NXP PCA9685 16-channel 12-bit PWM / servo driver codec (PROTOCORE_ENABLE_PCA9685).
 *
 * A caller sets the members a call takes, invokes it through ::Pca9685 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Pca9685.prescale_args.freq_hz = ...;
 *   Pca9685.prescale(work);
 *   // Pca9685.value is what the call reports
 *
 * @var Pca9685Ns::prescale_args  what prescale takes: freq_hz
 * @var Pca9685Ns::channel_reg_args  what channel_reg takes: channel
 * @var Pca9685Ns::us_to_count_args  what us_to_count takes: microseconds, freq_hz
 * @var Pca9685Ns::set_pwm_bytes_args  what set_pwm_bytes takes: buf, cap, channel, on, off
 * @var Pca9685Ns::begin_args  what begin takes: addr, freq_hz
 * @var Pca9685Ns::set_pwm_args  what set_pwm takes: channel, on, off
 * @var Pca9685Ns::set_servo_us_args  what set_servo_us takes: channel, microseconds
 * @var Pca9685Ns::ok  a call's true/false outcome
 * @var Pca9685Ns::value  the value a call reports
 * @var Pca9685Ns::count  what a call reports
 * @var Pca9685Ns::n  5, or 0 if cap < 5 or channel is out of range
 * @var Pca9685Ns::prescale  compute the PRESCALE register value for a PWM output frequency (25 ...
 * @var Pca9685Ns::channel_reg  the register base (LED_ON_L) for channel (0..15); 0 for an ...
 * @var Pca9685Ns::us_to_count  convert a servo pulse width (microseconds) at freq_hz to a 12-bit ...
 * @var Pca9685Ns::set_pwm_bytes  emit the 5-byte channel PWM write: `[LED_ON_L(channel), ON_L, ON_H, ...
 * @var Pca9685Ns::begin  reset the PCA9685 at addr and set the PWM frequency freq_hz. true ...
 * @var Pca9685Ns::set_pwm  set channel's raw 12-bit ON / OFF counts. false on I2C error / bad ...
 * @var Pca9685Ns::set_servo_us  drive a servo on channel to a microseconds pulse (uses the ...
 *
 * @c work is PROTOCORE_I2C_DEVICE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Pca9685PrescaleArgs prescale_args;
    Pca9685ChannelRegArgs channel_reg_args;
    Pca9685UsToCountArgs us_to_count_args;
    Pca9685SetPwmBytesArgs set_pwm_bytes_args;
    Pca9685BeginArgs begin_args;
    Pca9685SetPwmArgs set_pwm_args;
    Pca9685SetServoUsArgs set_servo_us_args;
    proto_bool ok;
    uint8_t value;
    uint16_t count;
    size_t n;
} Pca9685Vars;

/** @brief The operands and the outcome. */
extern Pca9685Vars Pca9685V;

/** @brief The entries. */
typedef struct
{
    void (*const prescale)(uint8_t *restrict work);
    void (*const channel_reg)(uint8_t *restrict work);
    void (*const us_to_count)(uint8_t *restrict work);
    void (*const set_pwm_bytes)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const set_pwm)(uint8_t *restrict work);
    void (*const set_servo_us)(uint8_t *restrict work);
} Pca9685Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Pca9685V or a region of the borrow at a fixed offset.
void protocore_pca9685_prescale(uint8_t *restrict work);
void protocore_pca9685_channel_reg(uint8_t *restrict work);
void protocore_pca9685_us_to_count(uint8_t *restrict work);
void protocore_pca9685_set_pwm_bytes(uint8_t *restrict work);
void protocore_pca9685_begin(uint8_t *restrict work);
void protocore_pca9685_set_pwm(uint8_t *restrict work);
void protocore_pca9685_set_servo_us(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Pca9685.prescale(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Pca9685Ns Pca9685 __attribute__((unused)) = {
    .prescale = protocore_pca9685_prescale,
    .channel_reg = protocore_pca9685_channel_reg,
    .us_to_count = protocore_pca9685_us_to_count,
    .set_pwm_bytes = protocore_pca9685_set_pwm_bytes,
    .begin = protocore_pca9685_begin,
    .set_pwm = protocore_pca9685_set_pwm,
    .set_servo_us = protocore_pca9685_set_servo_us,
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
uint8_t *protocore_pca9685_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PCA9685

#endif // PROTOCORE_PCA9685_H
