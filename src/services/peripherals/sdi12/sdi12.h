// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sdi12.h
 * @brief SDI-12 sensor-bus command / response codec (PC_ENABLE_SDI12).
 *
 * SDI-12 is the 1200-baud single-wire ASCII bus used by environmental / agricultural sensors
 * (soil moisture, water level, weather). A recorder addresses a sensor by a single character
 * (0-9, A-Z, a-z) and sends `<addr><command>!`; the sensor replies `<addr>...<CR><LF>`. This
 * codec builds the standard commands, parses the measurement response (`atttn`: seconds until
 * ready + value count), splits the data values, and does the SDI-12 CRC (the `aMC!` / `aCC!`
 * CRC-protected variants).
 *
 * The wire is a single 1200-baud 7E1 line with a 5 V break / marking convention; on an ESP32
 * it needs a small level / direction circuit, and the UART transport is the application's.
 * Pure codec, host-tested. Bridge a sensor string onto Wi-Fi by polling `aM!` / `aD0!` and
 * publishing the values.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SDI12_H
#define PROTOCORE_SDI12_H

#include "protocore_config.h"

#if PC_ENABLE_SDI12

#include <stddef.h>
#include <stdint.h>

#define SDI12_CRC_POLY 0xA001u ///< CRC-16 polynomial (reflected 0x8005), init 0x0000
#define SDI12_CRC_CHARS 3      ///< the CRC is appended as 3 printable ASCII octets

// --- command builders: write a NUL-terminated command into @p buf, return its length or 0 ---

/** @brief Generic `<addr><body>!` command (body is the command letters, e.g. "M" or "D0"). */
size_t pc_sdi12_build(char *buf, size_t cap, char addr, const char *body);

/** @brief Acknowledge-active command `a!`. */
size_t pc_sdi12_build_ack(char *buf, size_t cap, char addr);

/** @brief Send-identification command `aI!`. */
size_t pc_sdi12_build_identify(char *buf, size_t cap, char addr);

/** @brief Start-measurement command `aM!` (or `aMC!` when @p with_crc). */
size_t pc_sdi12_build_measure(char *buf, size_t cap, char addr, bool with_crc);

/** @brief Concurrent-measurement command `aC!` (or `aCC!` when @p with_crc). */
size_t pc_sdi12_build_concurrent(char *buf, size_t cap, char addr, bool with_crc);

/**
 * @brief Additional-measurement command `aM<n>!` (or `aMC<n>!` when @p with_crc), @p m_index 1..9 - the
 *        secondary measurement sets a multi-parameter sensor exposes beyond the primary `aM!`. @return 0 for
 *        an @p m_index outside 1..9.
 */
size_t pc_sdi12_build_measure_additional(char *buf, size_t cap, char addr, uint8_t m_index, bool with_crc);

/**
 * @brief Additional-concurrent command `aC<n>!` (or `aCC<n>!` when @p with_crc), @p c_index 1..9 - the
 *        concurrent-mode counterpart of the additional measurement. @return 0 for a @p c_index outside 1..9.
 */
size_t pc_sdi12_build_concurrent_additional(char *buf, size_t cap, char addr, uint8_t c_index, bool with_crc);

/**
 * @brief Continuous-measurement command `aR<n>!` (or `aRC<n>!` when @p with_crc), @p r_index 0..9. Unlike
 *        `aM!`, a continuous-measurement sensor returns its values immediately (no service-request delay).
 */
size_t pc_sdi12_build_continuous(char *buf, size_t cap, char addr, uint8_t r_index, bool with_crc);

/** @brief Start-verification command `aV!`; the response uses the same `atttn` timing form as `aM!`. */
size_t pc_sdi12_build_verify(char *buf, size_t cap, char addr);

/** @brief Send-data command `aD<n>!` (@p d_index 0..9). */
size_t pc_sdi12_build_data(char *buf, size_t cap, char addr, uint8_t d_index);

/** @brief Change-address command `aA<b>!` (@p new_addr is the new sensor address). */
size_t pc_sdi12_build_change_address(char *buf, size_t cap, char addr, char new_addr);

/** @brief Address-query command `?!` (asks the single connected sensor for its address). */
size_t pc_sdi12_build_query_address(char *buf, size_t cap);

// --- response parsing ---

/**
 * @brief Parse a measurement response `atttn<CR><LF>`: @p ready_sec = seconds until the data is
 * ready, @p num_values = how many values will be available. Works for both the 1-digit (`aM!`)
 * and 2-digit (`aC!`) value-count forms. @p addr (optional) receives the echoed address.
 */
bool pc_sdi12_parse_measure(const char *resp, size_t len, char *addr, uint16_t *ready_sec, uint8_t *num_values);

/**
 * @brief Split a data response `a<+/-value...><CR><LF>` into floats. Skips the leading address,
 * decodes each sign-prefixed number, and stops at CR/LF or the buffer end. Returns the count
 * via @p n (capped at @p max).
 */
bool pc_sdi12_parse_values(const char *resp, size_t len, float *out, size_t max, size_t *n);

/** @brief A decoded identify (aI!) response. Each field is fixed-width per the SDI-12 spec and
 *  NUL-terminated (the spec pads short values with spaces, which are left in place). */
struct Sdi12Identity
{
    char addr;              ///< sensor address
    char sdi_version[3];    ///< SDI-12 version "ll" (e.g. "14" = version 1.4) + NUL
    char vendor[9];         ///< 8-character vendor identification + NUL
    char model[7];          ///< 6-character sensor model number + NUL
    char sensor_version[4]; ///< 3-character sensor version + NUL
};

/**
 * @brief Parse an identify (aI!) response: address + 2-char SDI-12 version + 8-char vendor + 6-char model +
 *        3-char sensor version (any optional field and the CRC / CR-LF that follow are ignored).
 * @return true iff @p len covers the 20 fixed octets; false otherwise.
 */
bool pc_sdi12_parse_identify(const char *resp, size_t len, Sdi12Identity *out);

// --- CRC (RFC-free SDI-12 16-bit CRC) ---

/** @brief Compute the SDI-12 CRC-16 over @p data. */
uint16_t pc_sdi12_crc16(const uint8_t *data, size_t len);

/** @brief Encode a CRC into its 3 printable ASCII octets (out[0..2]). */
void pc_sdi12_crc_encode(uint16_t crc, char out[SDI12_CRC_CHARS]);

/**
 * @brief Verify a CRC-protected response: the 3 octets before the trailing `<CR><LF>` must be
 * the CRC of everything before them. @p len should include the data and the 3 CRC octets (the
 * `<CR><LF>` may or may not be included).
 */
bool pc_sdi12_check_crc(const char *resp, size_t len);

#endif // PC_ENABLE_SDI12
#endif // PROTOCORE_SDI12_H
