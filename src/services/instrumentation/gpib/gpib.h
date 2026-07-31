// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpib.h
 * @brief GPIB-over-LAN (Prologix-style) controller command codec (PC_ENABLE_GPIB) - a zero-heap
 *        codec for the Prologix-compatible `++` command set that drives a bench of legacy IEEE-488
 *        (GPIB) instruments through a Prologix GPIB-Ethernet / GPIB-USB adapter (raw socket on TCP
 *        1234). The bridge into pre-LAN test gear that will never speak SCPI-over-TCP directly.
 *
 * The device is the host: it sends `++` commands (a line starting with an unescaped `++`) to
 * configure/control the adapter, and data lines (anything else) that the adapter forwards over GPIB
 * to the addressed instrument. This codec builds the commands (@ref pc_gpib_command and the typed
 * @ref pc_gpib_addr / @ref pc_gpib_read / @ref pc_gpib_spoll / @ref pc_gpib_eos helpers), builds
 * an escaped data line (@ref pc_gpib_build_data - a leading ESC before a CR / LF / ESC / `+` byte in
 * the payload, then an unescaped newline terminator), classifies a line (@ref pc_gpib_is_command),
 * and parses the responses (@ref pc_gpib_parse_decimal for the serial-poll status byte / SRQ /
 * address, @ref pc_gpib_parse_version). Pure codec, host-tested; the socket / serial link is the
 * application's.
 *
 * Reference: Prologix GPIB-ETHERNET / GPIB-USB Controller manuals (prologix.biz).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GPIB_H
#define PROTOCORE_GPIB_H

#include "protocore_config.h"

#if PC_ENABLE_GPIB

#include <stddef.h>
#include <stdint.h>

/** @brief The Prologix GPIB-Ethernet raw-socket TCP port. */
#define PC_GPIB_PORT 1234
/** @brief The Prologix NetFinder UDP discovery port. */
#define PC_GPIB_DISCOVERY_PORT 3040

/** @brief GPIB terminator appended to data sent to the instrument (`++eos`). */
enum class GpibEos : uint8_t
{
    CRLF = 0,    ///< CR + LF
    CR = 1,      ///< CR
    LF = 2,      ///< LF
    PC_NONE = 3, ///< none (use for binary payloads)
};

/** @brief Read-termination mode (`++read`). */
enum class GpibRead : uint8_t
{
    UNTIL_TIMEOUT, ///< `++read` - read until the inter-character timeout only (NOT until EOI)
    UNTIL_EOI,     ///< `++read eoi` - read until EOI or timeout
    UNTIL_CHAR,    ///< `++read <char>` - read until the given character or timeout
};

/**
 * @brief Build a generic `++` command line: `"++"` + @p cmd + `'\n'` (e.g. `pc_gpib_command(...,
 *        "mode 1")` -> `"++mode 1\n"`, `"eoi 1"`, `"clr"`, `"ver"`, `"read_tmo_ms 500"`).
 * @return characters written (excluding the NUL), or 0 on overflow / bad input.
 */
size_t pc_gpib_command(char *buf, size_t cap, const char *cmd);

/**
 * @brief Build `++addr <pad>[ <sad>]` - set the instrument GPIB address.
 * @param pad primary address (0-30).
 * @param sad optional secondary address (96-126); pass < 0 for none.
 * @return characters written (excluding NUL), or 0 on overflow / bad @p pad.
 */
size_t pc_gpib_addr(char *buf, size_t cap, uint8_t pad, int sad);

/**
 * @brief Build a `++read` command in one of its three forms.
 * @param ch the read-until character (decimal), used only when @p mode is @ref GpibRead::UNTIL_CHAR.
 * @return characters written (excluding NUL), or 0 on overflow.
 */
size_t pc_gpib_read(char *buf, size_t cap, GpibRead mode, uint8_t ch);

/**
 * @brief Build `++spoll` (serial poll). With @p pad < 0, polls the currently-addressed instrument;
 *        otherwise `++spoll <pad>[ <sad>]`. The response is the status byte as a decimal string.
 * @return characters written (excluding NUL), or 0 on overflow.
 */
size_t pc_gpib_spoll(char *buf, size_t cap, int pad, int sad);

/** @brief Build `++eos <n>` - the GPIB terminator appended to instrument data. */
size_t pc_gpib_eos(char *buf, size_t cap, GpibEos eos);

/**
 * @brief Build an escaped data line to send to the addressed instrument: each CR (13) / LF (10) /
 *        ESC (27) / `+` (43) byte in @p src is preceded by an ESC (27), then an unescaped `'\n'`
 *        line terminator is appended. (Data received FROM instruments is never escaped.)
 * @return total bytes written (NOT NUL-terminated - the payload may be binary), or 0 on overflow.
 */
size_t pc_gpib_build_data(uint8_t *buf, size_t cap, const uint8_t *src, size_t len);

/** @brief True if @p line is a controller command (starts with an unescaped `++`), else it is data. */
bool pc_gpib_is_command(const char *line, size_t len);

/**
 * @brief Parse a decimal integer response (trims surrounding spaces / CR / LF) - the `++spoll`
 *        status byte, the `++srq` 0/1, or a `++addr` primary address.
 * @return true on a clean decimal; false otherwise.
 */
bool pc_gpib_parse_decimal(const char *s, size_t len, uint32_t *out);

/**
 * @brief Parse a `++addr` query response: a primary address, optionally a space + secondary.
 * @param pad receives the primary address.
 * @param sad receives the secondary address, or -1 if none was present.
 * @return true on a clean response; false otherwise.
 */
bool pc_gpib_parse_addr(const char *s, size_t len, uint8_t *pad, int *sad);

/**
 * @brief Parse a `++ver` response: locate the version token after `"version "`. @p ver points INTO
 *        @p s (trailing CR/LF trimmed from @p ver_len).
 * @return true if a version token was found; false otherwise.
 */
bool pc_gpib_parse_version(const char *s, size_t len, const char **ver, size_t *ver_len);

#endif // PC_ENABLE_GPIB

#endif // PROTOCORE_GPIB_H
