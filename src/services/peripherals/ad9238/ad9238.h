// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ad9238.h
 * @brief SPI configuration-port codec for the AD9238 (and the shared ADI high-speed-ADC SPI
 *        map it belongs to) - PC_ENABLE_AD9238.
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
 *    this era (AD9238 and its close siblings) share, per the AD9238 Rev. D datasheet's SPI
 *    Register Map. Pure codec (builds/parses byte sequences); the SPI clocking is the app's -
 *    same contract as every other codec in this library (see services/instrumentation/scpi,
 * services/instrumentation/gpib).
 *
 * **Confidence note.** The instruction-word framing and the transfer-register mechanism are the
 * standardized shape used across this ADI ADC generation and are implemented directly from that
 * convention. The specific per-register bit-field constants below (@ref Ad9238Reg and the test-
 * pattern / output-mode enums) transcribe the AD9238 Rev. D datasheet's register table as best
 * documented; **confirm every address and bit position against your part's datasheet revision
 * before writing to real silicon** - per this project's hardware-verification policy
 * (docs/KNOWN_LIMITATIONS.md), nothing here has been validated against a physical AD9238 yet.
 *
 * Reference: Analog Devices AD9238 Data Sheet Rev. D, "Serial Port Interface (SPI)" section.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AD9238_H
#define PROTOCORE_AD9238_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_AD9238

/** @brief SPI register addresses (13-bit address field). Verify against your datasheet revision. */
typedef enum PROTO_ENUM_PACKED
{
    AD9238_REG_CHIP_PORT_CONFIG = 0x00, ///< SDIO direction, LSB/MSB-first, soft reset (mirrored bits)
    AD9238_REG_CHIP_ID = 0x01,          ///< read-only device id
    AD9238_REG_CHIP_GRADE = 0x02,       ///< read-only speed-grade id
    AD9238_REG_CHANNEL_INDEX = 0x08,    ///< bit0 = channel A, bit1 = channel B (selects which
                                        ///< channel the following per-channel registers target)
    AD9238_REG_POWER_DOWN = 0x09,       ///< per-channel: bit0 power-down, bit1 standby
    AD9238_REG_OUTPUT_MODE = 0x0A,      ///< per-channel: data format + output invert (see Ad9238OutputFormat)
    AD9238_REG_OUTPUT_PHASE = 0x0D,     ///< per-channel: output clock phase adjust
    AD9238_REG_OUTPUT_DELAY = 0x0F,     ///< per-channel: output data valid delay
    AD9238_REG_VREF = 0x10,             ///< per-channel: internal reference full-scale select
    AD9238_REG_ANALOG_INPUT = 0x14,     ///< per-channel: analog input duty-cycle stabilizer enable
    AD9238_REG_TEST_IO = 0x15,          ///< per-channel: output test pattern select (see Ad9238TestPattern)
    AD9238_REG_OFFSET_ADJUST = 0x18,    ///< per-channel: digital offset trim
    AD9238_REG_DEVICE_UPDATE = 0xFF,    ///< bit0 = SW transfer: latch all shadowed writes into effect
} Ad9238Reg;

/** @brief AD9238_REG_TEST_IO[2:0] - the output test pattern (0x15, bits 2:0). */
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

/** @brief AD9238_REG_OUTPUT_MODE[1:0] - the output data format (0x0A, bits 1:0). */
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

/**
 * @brief Build the 16-bit SPI instruction word (MSB first on the wire: high byte then low byte).
 * @param read      true for a read transaction, false for a write.
 * @param reg_addr  13-bit register address (@ref Ad9238Reg or a raw value).
 * @param nbytes    number of data bytes to follow (1-4; encoded as W1:W0 = nbytes-1, so 4 means
 *                  "streaming" per the datasheet's byte-count field - pass 1 unless you know you
 *                  want streaming mode).
 * @param out2      receives the 2-byte instruction word.
 * @return true; false only if @p out2 is null or @p nbytes is 0 or > 4.
 */
proto_bool pc_ad9238_build_instruction(proto_bool read, uint16_t reg_addr, uint8_t nbytes, uint8_t out2[2]);

/**
 * @brief Build a complete single-register write transaction (instruction word + one data byte)
 *        ready to clock out over SPI.
 * @return 3 (bytes written to @p out), or 0 if @p out is null / @p cap < 3.
 */
size_t pc_ad9238_build_write(uint16_t reg_addr, uint8_t value, uint8_t *out, size_t cap);

/**
 * @brief Build a single-register read instruction (the 2-byte header; the caller then clocks one
 *        further byte to receive the register value, since this is a half-duplex codec with no
 *        transport of its own).
 * @return 2 (bytes written to @p out), or 0 if @p out is null / @p cap < 2.
 */
size_t pc_ad9238_build_read(uint16_t reg_addr, uint8_t *out, size_t cap);

/**
 * @brief Build the "device update" transfer transaction (write 0x01 to AD9238_REG_DEVICE_UPDATE):
 *        every shadowed register write above needs this to take effect.
 * @return 3, or 0 if @p out is null / @p cap < 3.
 */
size_t pc_ad9238_build_transfer(uint8_t *out, size_t cap);

#endif // PC_ENABLE_AD9238

PROTO_END_DECLS

#endif // PROTOCORE_AD9238_H
