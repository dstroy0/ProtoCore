// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mpr121.h
 * @brief NXP MPR121 12-channel capacitive-touch controller codec (PROTOCORE_ENABLE_MPR121).
 *
 * The MPR121 reports a 16-bit touch-status word (registers 0x00/0x01): bits 0-11 are the twelve
 * electrodes, bit 12 is the proximity electrode, and bit 15 is the over-current flag. It also
 * exposes 10-bit filtered capacitance and 8-bit baseline per electrode. Bringing it up is a
 * fixed sequence of register writes (soft reset, the NXP filter/AFE defaults, per-electrode
 * touch/release thresholds, and the electrode-configuration register that starts it running).
 *
 * This codec is pure and host-tested: ::protocore_mpr121_touched / ::protocore_mpr121_word10 decode the reported
 * words, and ::protocore_mpr121_build_init emits the whole bring-up sequence as `(register, value)` byte
 * pairs (so the exact bytes are verifiable off-target). On an ESP32 the binding replays that
 * sequence over I2C (Wire) and reads the status; only that touches hardware.
 *
 * A cheap solder-and-bench-test breakout for touch buttons / sliders: wire it up, touch a pad,
 * watch the bit set.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MPR121_H
#define PROTOCORE_MPR121_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MPR121

PROTOCORE_BEGIN_DECLS

// PROTOCORE_MPR121_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define MPR121_ELECTRODES 12

#define MPR121_INIT_MAX 82

/** @brief What touched takes: status_lo, status_hi. */
typedef struct
{
    uint8_t status_lo;
    uint8_t status_hi;
} Mpr121TouchedArgs;
/** @brief What is_touched takes: mask, e. */
typedef struct
{
    uint16_t mask;
    uint8_t e;
} Mpr121IsTouchedArgs;
/** @brief What proximity takes: status_hi. */
typedef struct
{
    uint8_t status_hi;
} Mpr121ProximityArgs;
/** @brief What overcurrent takes: status_hi. */
typedef struct
{
    uint8_t status_hi;
} Mpr121OvercurrentArgs;
/** @brief What word10 takes: lsb, msb. */
typedef struct
{
    uint8_t lsb;
    uint8_t msb;
} Mpr121Word10Args;
/** @brief What build_init takes: buf, cap, n_electrodes, touch_thr, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t n_electrodes;
    uint8_t touch_thr;
    uint8_t release_thr;
} Mpr121BuildInitArgs;
/** @brief What begin takes: addr. */
typedef struct
{
    uint8_t addr;
} Mpr121BeginArgs;
/** @brief What read_filtered takes: e. */
typedef struct
{
    uint8_t e;
} Mpr121ReadFilteredArgs;
/**
 * @brief NXP MPR121 12-channel capacitive-touch controller codec (PROTOCORE_ENABLE_MPR121).
 *
 * A caller sets the members a call takes, invokes it through ::Mpr121 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Mpr121.touched_args.status_lo = ...;
 *   Mpr121.touched_args.status_hi = ...;
 *   Mpr121.touched(work);
 *   // Mpr121.value is what the call reports
 *
 * @var Mpr121Ns::touched_args  what touched takes: status_lo, status_hi
 * @var Mpr121Ns::is_touched_args  what is_touched takes: mask, e
 * @var Mpr121Ns::proximity_args  what proximity takes: status_hi
 * @var Mpr121Ns::overcurrent_args  what overcurrent takes: status_hi
 * @var Mpr121Ns::word10_args  what word10 takes: lsb, msb
 * @var Mpr121Ns::build_init_args  what build_init takes: buf, cap, n_electrodes, touch_thr,
 * @var Mpr121Ns::begin_args  what begin takes: addr
 * @var Mpr121Ns::read_filtered_args  what read_filtered takes: e
 * @var Mpr121Ns::ok  a call's true/false outcome
 * @var Mpr121Ns::value  the value a call reports
 * @var Mpr121Ns::n  the number of bytes written (pairs * 2), or 0 if cap is too small / ...
 * @var Mpr121Ns::touched  decode the 12-electrode touch bitmask from the two status registers ...
 * @var Mpr121Ns::is_touched  true if electrode e (0..11) is touched in a mask from ...
 * @var Mpr121Ns::proximity  true if the proximity electrode (status bit 12) is active
 * @var Mpr121Ns::overcurrent  true if the over-current flag (status bit 15) is set (wiring fault ...
 * @var Mpr121Ns::word10  combine a little-endian LSB/MSB register pair into a 10-bit value ...
 * @var Mpr121Ns::build_init  build the MPR121 bring-up sequence as consecutive `(register, ...
 * @var Mpr121Ns::begin  reset + configure the MPR121 at addr over I2C. true if it ...
 * @var Mpr121Ns::read_touched  read the current 12-electrode touch bitmask (0 if the device is ...
 * @var Mpr121Ns::read_filtered  read electrode e's 10-bit filtered capacitance value
 *
 * @c work is PROTOCORE_MPR121_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Mpr121TouchedArgs touched_args;
    Mpr121IsTouchedArgs is_touched_args;
    Mpr121ProximityArgs proximity_args;
    Mpr121OvercurrentArgs overcurrent_args;
    Mpr121Word10Args word10_args;
    Mpr121BuildInitArgs build_init_args;
    Mpr121BeginArgs begin_args;
    Mpr121ReadFilteredArgs read_filtered_args;
    proto_bool ok;
    uint16_t value;
    size_t n;
} Mpr121Vars;

/** @brief The operands and the outcome. */
extern Mpr121Vars Mpr121V;

/** @brief The entries. */
typedef struct
{
    void (*const touched)(uint8_t *restrict work);
    void (*const is_touched)(uint8_t *restrict work);
    void (*const proximity)(uint8_t *restrict work);
    void (*const overcurrent)(uint8_t *restrict work);
    void (*const word10)(uint8_t *restrict work);
    void (*const build_init)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const read_touched)(uint8_t *restrict work);
    void (*const read_filtered)(uint8_t *restrict work);
} Mpr121Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Mpr121V or a region of the borrow at a fixed offset.
void protocore_mpr121_touched(uint8_t *restrict work);
void protocore_mpr121_is_touched(uint8_t *restrict work);
void protocore_mpr121_proximity(uint8_t *restrict work);
void protocore_mpr121_overcurrent(uint8_t *restrict work);
void protocore_mpr121_word10(uint8_t *restrict work);
void protocore_mpr121_build_init(uint8_t *restrict work);
void protocore_mpr121_begin(uint8_t *restrict work);
void protocore_mpr121_read_touched(uint8_t *restrict work);
void protocore_mpr121_read_filtered(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Mpr121.touched(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Mpr121Ns Mpr121 __attribute__((unused)) = {
    .touched = protocore_mpr121_touched,
    .is_touched = protocore_mpr121_is_touched,
    .proximity = protocore_mpr121_proximity,
    .overcurrent = protocore_mpr121_overcurrent,
    .word10 = protocore_mpr121_word10,
    .build_init = protocore_mpr121_build_init,
    .begin = protocore_mpr121_begin,
    .read_touched = protocore_mpr121_read_touched,
    .read_filtered = protocore_mpr121_read_filtered,
};

/**
 * @brief The PROTOCORE_MPR121_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_mpr121_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MPR121

#endif // PROTOCORE_MPR121_H
