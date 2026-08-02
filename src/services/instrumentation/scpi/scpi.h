// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file scpi.h
 * @brief SCPI / IEEE 488.2 instrument-control codec (PC_ENABLE_SCPI) - a zero-heap codec for the
 *        text command language nearly every modern bench instrument speaks (DMMs, oscilloscopes,
 *        power supplies, function/arbitrary generators, SMUs, spectrum/network analyzers, loads)
 *        over a raw TCP socket on port 5025 (also USBTMC / VXI-11 / HiSLIP / serial).
 *
 * The codec is symmetric - it serves both roles:
 *  - **Controller** (device drives an instrument): build command lines with a `:`-hierarchy header
 *    and comma-separated parameters (@ref pc_scpi_build), and parse the instrument's replies -
 *    numeric NR1/NR2/NR3 (@ref pc_scpi_parse_number), boolean (@ref pc_scpi_parse_bool), quoted
 *    string (@ref pc_scpi_parse_string), and the IEEE 488.2 arbitrary block `#<n><len><data>` used
 *    for waveform captures (@ref pc_scpi_parse_block).
 *  - **Instrument** (device answers a controller): the IEEE 488.2 status model - the Status Byte,
 *    Standard Event Status Register + its enable mask, the Service Request Enable mask, and the SCPI
 *    error/event queue (@ref ScpiStatus) - plus a SCPI short/long-form header matcher
 *    (@ref pc_scpi_match) to dispatch an incoming command against a pattern like `"SYSTem:ERRor?"`.
 *
 * Common commands (@ref pc_scpi_common) return the IEEE 488.2 mnemonics (`*IDN?`, `*RST`, `*CLS`,
 * `*ESR?`, `*STB?`, ...). Pure codec, host-tested; the TCP/USB/serial transport is the app's.
 *
 * References:
 *  - IEEE Std 488.2-1992 (Codes, Formats, Protocols and Common Commands).
 *  - SCPI Consortium, "Standard Commands for Programmable Instruments", Vol. 1 Syntax & Style (1999).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SCPI_H
#define PROTOCORE_SCPI_H

#include "protocore_config.h"

#if PC_ENABLE_SCPI

/** @brief The default raw-socket ("SCPI-RAW") TCP port instruments listen on. */
#define PC_SCPI_PORT 5025

// ── Standard Event Status Register (ESR) bits - read/cleared by *ESR? ──────────────────────────
#define SCPI_ESR_OPC 0x01 ///< bit 0: Operation Complete
#define SCPI_ESR_RQC 0x02 ///< bit 1: Request Control
#define SCPI_ESR_QYE 0x04 ///< bit 2: Query Error
#define SCPI_ESR_DDE 0x08 ///< bit 3: Device-Dependent Error
#define SCPI_ESR_EXE 0x10 ///< bit 4: Execution Error
#define SCPI_ESR_CME 0x20 ///< bit 5: Command Error
#define SCPI_ESR_URQ 0x40 ///< bit 6: User Request
#define SCPI_ESR_PON 0x80 ///< bit 7: Power On

// ── Status Byte Register (STB) bits - read by *STB? / serial poll (SCPI model) ─────────────────
#define SCPI_STB_EAV 0x04 ///< bit 2: Error/event queue not empty
#define SCPI_STB_QSB 0x08 ///< bit 3: QUEStionable status summary
#define SCPI_STB_MAV 0x10 ///< bit 4: Message Available (output queue not empty)
#define SCPI_STB_ESB 0x20 ///< bit 5: Standard Event status Summary (ESR & ESE)
#define SCPI_STB_MSS 0x40 ///< bit 6: Master Summary Status (RQS in a serial poll)
#define SCPI_STB_OSB 0x80 ///< bit 7: OPERation status summary

/** @brief The mandatory IEEE 488.2 common commands (`pc_scpi_common` renders each). */
typedef enum PROTO_ENUM_PACKED
{
    SCPI_CLS,   ///< "*CLS"  clear status
    SCPI_ESE,   ///< "*ESE"  set event status enable
    SCPI_ESE_Q, ///< "*ESE?" query event status enable
    SCPI_ESR_Q, ///< "*ESR?" query + clear event status register
    SCPI_IDN_Q, ///< "*IDN?" identification query
    SCPI_OPC,   ///< "*OPC"  set operation-complete bit when done
    SCPI_OPC_Q, ///< "*OPC?" query operation complete (returns 1)
    SCPI_RST,   ///< "*RST"  reset
    SCPI_SRE,   ///< "*SRE"  set service request enable
    SCPI_SRE_Q, ///< "*SRE?" query service request enable
    SCPI_STB_Q, ///< "*STB?" query status byte
    SCPI_TST_Q, ///< "*TST?" self-test query
    SCPI_WAI,   ///< "*WAI"  wait to continue
} ScpiCommon;

/** @brief Render an IEEE 488.2 common command mnemonic (a static string, never null). */
const char *pc_scpi_common(ScpiCommon c);

/**
 * @brief Build a SCPI command line: `header` + ' ' + comma-joined `args` + '\n'.
 *
 * With @p argc 0 the line is just `header` + '\n' (e.g. `"*RST\n"`, `"MEAS:VOLT:DC?\n"`).
 * @param header a full `:`-hierarchy header or a common command, e.g. `"SOURce:VOLTage"`, `"*IDN?"`.
 * @param args   already-formatted parameter strings (see @ref pc_scpi_fmt_real to format a number).
 * @return characters written (excluding the NUL terminator), or 0 on overflow / bad input.
 * @note  The line is NUL-terminated so callers may treat @p buf as a C-string.
 */
size_t pc_scpi_build(char *buf, size_t cap, const char *header, const char *const *args, size_t argc);

/**
 * @brief Format a real number as a SCPI parameter (NR2 fixed, or NR3 scientific for large/small
 *        magnitudes), trailing zeros trimmed. Handy for @ref pc_scpi_build args.
 * @return characters written (excluding NUL), or 0 on overflow.
 */
size_t pc_scpi_fmt_real(char *buf, size_t cap, double v);

/**
 * @brief Parse a SCPI numeric response (NR1 integer / NR2 fixed / NR3 scientific), hand-rolled
 *        (no stdlib). Recognizes the special values `9.9E37` (INFinity), `-9.9E37`, `9.91E37` (NaN).
 * @return true on a fully-consumed numeric field; false on malformed input.
 */
proto_bool pc_scpi_parse_number(const char *s, size_t len, double *out);

/**
 * @brief Parse a SCPI boolean response: `1`/`0` or `ON`/`OFF` (case-insensitive).
 * @return true on a recognized boolean; false otherwise.
 */
proto_bool pc_scpi_parse_bool(const char *s, size_t len, proto_bool *out);

/**
 * @brief Parse a SCPI string response: strip the surrounding single or double quotes and collapse a
 *        doubled quote (`""` or `''`) to one. Copies into @p out (NUL-terminated).
 * @return the unquoted length (excluding NUL), or 0 on a missing/mismatched quote or overflow.
 */
size_t pc_scpi_parse_string(const char *s, size_t len, char *out, size_t cap);

/**
 * @brief Parse an IEEE 488.2 arbitrary block: definite `#<n><n-length-digits><data>` or indefinite
 *        `#0<data>` (runs to the trailing newline). On success @p data points INTO @p buf.
 * @param data      receives a pointer to the first data byte.
 * @param data_len  receives the data byte count.
 * @param consumed  receives the total bytes the block occupied (header + data [+ the indefinite NL]).
 * @return true on a complete block; false if truncated or malformed.
 */
proto_bool pc_scpi_parse_block(const uint8_t *buf, size_t len, const uint8_t **data, size_t *data_len,
                               size_t *consumed);

// ── IEEE 488.2 / SCPI status model (instrument side) ───────────────────────────────────────────

/** @brief One error/event queue entry. @ref msg points at a static or app-owned string. */
typedef struct
{
    int16_t number;  ///< SCPI error number (negative = standard, positive = device-specific, 0 = no error)
    const char *msg; ///< the description text (without the surrounding quotes)
} ScpiError;

/** @brief The IEEE 488.2 status registers + the SCPI error/event queue (one owned block). */
typedef struct
{
    uint8_t esr;                        ///< Standard Event Status Register (latched events)
    uint8_t ese;                        ///< Standard Event Status Enable mask (ESR -> ESB)
    uint8_t sre;                        ///< Service Request Enable mask (STB -> MSS)
    uint8_t summary;                    ///< app-set STB summary bits (QSB/MAV/OSB); merged into *STB?
    ScpiError queue[PC_SCPI_ERR_QUEUE]; ///< the error/event queue (FIFO ring)
    uint8_t head;                       ///< ring read index
    uint8_t count;                      ///< entries currently queued
} ScpiStatus;

/** @brief Clear every register + the queue (power-on state). */
void pc_scpi_status_init(ScpiStatus *s);

/** @brief Latch one or more ESR event bits (e.g. @ref SCPI_ESR_OPC). */
void pc_scpi_event(ScpiStatus *s, uint8_t esr_bits);

/**
 * @brief Enqueue an error/event and latch its ESR class bit (CME/EXE/DDE/QYE from the -1xx/-2xx/
 *        -3xx/-4xx number range). On overflow the tail entry becomes -350 "Queue overflow".
 * @param msg  the description; pass nullptr to use the standard text for a standard number.
 */
void pc_scpi_push_error(ScpiStatus *s, int16_t number, const char *msg);

/**
 * @brief Pop the head error (the `SYSTem:ERRor?` action). When the queue is empty @p out becomes
 *        `0,"No error"`.
 * @return true if an error was dequeued; false if the queue was empty (out = the no-error entry).
 */
proto_bool pc_scpi_pop_error(ScpiStatus *s, ScpiError *out);

/** @brief Compute the Status Byte: EAV (queue), ESB (esr & ese), the app summary bits, and MSS. */
uint8_t pc_scpi_stb(const ScpiStatus *s);

/** @brief The `*CLS` action: clear the ESR and empty the error/event queue (enables untouched). */
void pc_scpi_cls(ScpiStatus *s);

/** @brief The standard SCPI message for a standard (negative) error number, or "" if unknown. */
const char *pc_scpi_std_error(int16_t number);

/**
 * @brief Match an incoming command header against a SCPI short/long-form pattern.
 *
 * Case-insensitive. Each `:`-separated node in @p pattern has an uppercase run (the required short
 * form) followed by an optional lowercase tail (the long form); the input node must equal EITHER the
 * short form OR the whole long form (no other truncation). An optional numeric suffix is compared
 * with an omitted suffix defaulting to 1 (`OUTP` == `OUTPut1`). A trailing `?` in the pattern must
 * be matched by a `?` in the input. A `*`-prefixed pattern (common command) is matched whole,
 * case-insensitively. A leading `:` on the input (absolute path) is ignored.
 *
 * @return true if @p input (its header, up to the first space) matches @p pattern.
 */
proto_bool pc_scpi_match(const char *input, size_t input_len, const char *pattern);

#endif // PC_ENABLE_SCPI

#endif // PROTOCORE_SCPI_H
