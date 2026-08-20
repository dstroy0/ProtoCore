// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ad9238.h
 * @brief SPI configuration-port codec for the AD9238 (and the shared ADI high-speed-ADC SPI
 *        map it belongs to) - PROTOCORE_ENABLE_AD9238.
 *
 * The AD9238 (12-bit, 20/40/65 MSPS dual ADC) has TWO interfaces that must not be confused:
 *  - The **sample data path**: a parallel CMOS/LVDS bus (12 data lines + DCO/output clock per
 *    channel) run far beyond what a microcontroller can bit-bang at 20-65 MSPS. That path is
 *    NOT this file - it is out of scope for direct MCU capture; see reverse_engineering/README.md
 *    for the FPGA/CPLD-buffered burst-drain architecture this project actually uses.
 *  - The **SPI configuration port** (SCLK / SDIO / CSB, 3-wire, MSB first): a low-speed,
 *    low-throughput control channel for power-down, output data format, output test patterns,
 *    and offset trim - register writes only, never the sample stream. This file is that codec:
 *    it builds the 16-bit instruction word (R/W + 2-bit byte-count + 13-bit address) and the
 *    shadow-register "device update" transfer that the whole ADI high-speed-ADC generation of
 *    this era (AD9238 and its close siblings) share, per AN-877's SPI register map. Pure codec
 *    (builds/parses byte sequences); the SPI clocking is the app's - same contract as every other
 *    codec in this library (see services/instrumentation/scpi, services/instrumentation/gpib).
 *
 * **Confidence note.** The instruction-word framing, the transfer-register mechanism and every
 * register address in @ref Ad9238Reg are transcribed from AN-877 Rev. A, which defines the map
 * this whole ADI ADC generation shares. AN-877 says which registers exist and where; it does not
 * say which of them a given part implements, so **confirm every address and bit position against
 * your part's datasheet revision before writing to real silicon** - per this project's
 * hardware-verification policy (docs/KNOWN_LIMITATIONS.md), nothing here has been validated
 * against a physical AD9238 yet.
 *
 * Reference: Analog Devices AN-877 Rev. A, "Interfacing to High Speed ADCs via SPI", the FORMAT
 * and CHIP PROGRAMMING sections. Cached at docs/learn/datasheets/an877.pdf.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AD9238_H
#define PROTOCORE_AD9238_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_AD9238

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/**
 * @brief SPI register addresses (13-bit address field), from AN-877 "CHIP PROGRAMMING".
 *
 * Every address below is the one AN-877 Rev. A names for that register, section by section:
 * Configuration Register (0x000), Chip ID (0x001), Chip Grade (0x002), Device Indexing (0x004 and
 * 0x005), Modes (0x008), Output Test Modes (0x00D), Analog Input (0x00F), Offset Adjust (0x010),
 * Output Mode (0x014), Clock Divider Phase (0x016), Output Delay Adjust (0x017), Reference Adjust
 * (0x018) and the Transfer Register (0x0FF). A device may not implement all of them - check the
 * part's own datasheet for which are present and what the fields mean.
 */
typedef enum PROTO_ENUM_PACKED
{
    AD9238_REG_CHIP_PORT_CONFIG = 0x000, ///< SDIO active, LSB first, soft reset (mirrored nibbles)
    AD9238_REG_CHIP_ID = 0x001,          ///< read-only device id
    AD9238_REG_CHIP_GRADE = 0x002,       ///< read-only speed-grade id
    AD9238_REG_CHANNEL_INDEX = 0x005,    ///< device indexing, ADC0..ADC3 (0x004 indexes ADC4..ADC7)
    AD9238_REG_POWER_DOWN = 0x008,       ///< modes: bits 2:0 the internal power-down mode
    AD9238_REG_TEST_IO = 0x00D,          ///< output test modes: bits 3:0 select the pattern (see Ad9238TestPattern)
    AD9238_REG_ANALOG_INPUT = 0x00F,     ///< analog input: low-pass corner, disconnect, single-ended
    AD9238_REG_OFFSET_ADJUST = 0x010,    ///< digital offset trim, twos complement about midscale
    AD9238_REG_OUTPUT_MODE = 0x014,      ///< output logic type, invert, and bits 1:0 the data format
    AD9238_REG_OUTPUT_PHASE = 0x016,     ///< clock divider phase: which phase latches the data
    AD9238_REG_OUTPUT_DELAY = 0x017,     ///< fine delay on the output latch
    AD9238_REG_VREF = 0x018,             ///< reference adjust: bits 7:6 select VREF, bits 5:0 trim it
    AD9238_REG_DEVICE_UPDATE = 0x0FF,    ///< transfer: bit0 latches every shadowed write into effect
} Ad9238Reg;

/** @brief AD9238_REG_TEST_IO[3:0] - the output test pattern (AN-877 Table 8, register 0x00D). */
typedef enum PROTO_ENUM_PACKED
{
    AD9238_TEST_OFF = 0x00, ///< normal operation
    AD9238_TEST_MIDSCALE_SHORT = 0x01,
    AD9238_TEST_POS_FULLSCALE = 0x02,
    AD9238_TEST_NEG_FULLSCALE = 0x03,
    AD9238_TEST_CHECKERBOARD = 0x04, ///< alternating 0xAAA/0x555 - the pipeline's self-test pattern
    AD9238_TEST_PN23 = 0x05,
    AD9238_TEST_PN9 = 0x06,
    AD9238_TEST_ONE_ZERO_TOGGLE = 0x07,
} Ad9238TestPattern;

/** @brief AD9238_REG_OUTPUT_MODE[1:0] - the output data format (AN-877 Table 11, register 0x014). */
typedef enum PROTO_ENUM_PACKED
{
    AD9238_FORMAT_OFFSET_BINARY = 0x00,
    AD9238_FORMAT_TWOS_COMPLEMENT = 0x01,
    AD9238_FORMAT_GRAY_CODE = 0x02,
} Ad9238OutputFormat;

/** @brief Which channel a per-channel register write targets (AD9238_REG_CHANNEL_INDEX bits). */
typedef enum PROTO_ENUM_PACKED
{
    AD9238_CHAN_A = 0x01,
    AD9238_CHAN_B = 0x02,
    AD9238_CHAN_BOTH = 0x03,
} Ad9238Channel;

/** @brief What build_instruction takes: read, reg_addr, nbytes, out2. */
typedef struct
{
    proto_bool read;   ///< true for a read transaction, false for a write
    uint16_t reg_addr; ///< 13-bit register address (Ad9238Reg or a raw value)
    uint8_t nbytes;    ///< number of data bytes to follow (1-4; encoded as W1:W0 = nbytes-1, so 4 means "streaming" ...
    uint8_t *out2;     ///< receives the 2-byte instruction word
} Ad9238BuildInstructionArgs;
/** @brief What build_write takes: reg_addr, value, out, cap. */
typedef struct
{
    uint16_t reg_addr;
    uint8_t value;
    uint8_t *out;
    size_t cap;
} Ad9238BuildWriteArgs;
/** @brief What build_read takes: reg_addr, out, cap. */
typedef struct
{
    uint16_t reg_addr;
    uint8_t *out;
    size_t cap;
} Ad9238BuildReadArgs;
/** @brief What build_transfer takes: out, cap. */
typedef struct
{
    uint8_t *out;
    size_t cap;
} Ad9238BuildTransferArgs;
/**
 * @brief SPI configuration-port codec for the AD9238 (and the shared ADI high-speed-ADC SPI map it belongs to) - ...
 *
 * A caller sets the members a call takes, invokes it through ::Ad9238 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ad9238.build_instruction_args.read = ...;
 *   Ad9238.build_instruction_args.reg_addr = ...;
 *   Ad9238.build_instruction_args.nbytes = ...;
 *   Ad9238.build_instruction_args.out2 = ...;
 *   Ad9238.build_instruction(work);
 *   // Ad9238.ok is what the call reports
 *
 * @var Ad9238Ns::build_instruction_args  what build_instruction takes: read, reg_addr, nbytes, out2
 * @var Ad9238Ns::build_write_args  what build_write takes: reg_addr, value, out, cap
 * @var Ad9238Ns::build_read_args  what build_read takes: reg_addr, out, cap
 * @var Ad9238Ns::build_transfer_args  what build_transfer takes: out, cap
 * @var Ad9238Ns::ok  true; false only if out2 is null or nbytes is 0 or > 4
 * @var Ad9238Ns::n  3 (bytes written to out), or 0 if out is null / cap < 3
 * @var Ad9238Ns::build_instruction  build the 16-bit SPI instruction word (MSB first on the wire: high ...
 * @var Ad9238Ns::build_write  build a complete single-register write transaction (instruction ...
 * @var Ad9238Ns::build_read  build a single-register read instruction (the 2-byte header; the ...
 * @var Ad9238Ns::build_transfer  build the "device update" transfer transaction (write 0x01 to ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Ad9238BuildInstructionArgs build_instruction_args;
    Ad9238BuildWriteArgs build_write_args;
    Ad9238BuildReadArgs build_read_args;
    Ad9238BuildTransferArgs build_transfer_args;
    proto_bool ok;
    size_t n;
} Ad9238Vars;

/** @brief The operands and the outcome. */
extern Ad9238Vars Ad9238V;

/** @brief The entries. */
typedef struct
{
    void (*const build_instruction)(uint8_t *restrict work);
    void (*const build_write)(uint8_t *restrict work);
    void (*const build_read)(uint8_t *restrict work);
    void (*const build_transfer)(uint8_t *restrict work);
} Ad9238Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Ad9238V or a region of the borrow at a fixed offset.
void protocore_ad9238_build_instruction(uint8_t *restrict work);
void protocore_ad9238_build_write(uint8_t *restrict work);
void protocore_ad9238_build_read(uint8_t *restrict work);
void protocore_ad9238_build_transfer(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ad9238.build_instruction(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Ad9238Ns Ad9238 __attribute__((unused)) = {
    .build_instruction = protocore_ad9238_build_instruction,
    .build_write = protocore_ad9238_build_write,
    .build_read = protocore_ad9238_build_read,
    .build_transfer = protocore_ad9238_build_transfer,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AD9238

#endif // PROTOCORE_AD9238_H
