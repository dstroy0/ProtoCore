// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lsv2.h
 * @brief Heidenhain LSV/2 telegram codec (PC_ENABLE_LSV2) - a zero-heap codec for the LSV/2 protocol
 *        Heidenhain TNC controls (iTNC 530, TNC 320/620/640, ...) speak for DNC and data access over a
 *        serial link or, as implemented here, LSV/2-over-TCP (default port 19000). A CNC-native
 *        southbound source for the common European control, alongside `services/focas` (Fanuc) and
 *        `services/haas_mdc` (Haas).
 *
 * Wire framing (byte-exact, both directions): a telegram is a 4-byte big-endian payload-length prefix,
 * then a 4-character ASCII command / response mnemonic, then `payload-length` payload bytes. The length
 * counts ONLY the payload - the mnemonic is not included - so a telegram with no payload is exactly 8
 * bytes on the wire (e.g. a bare `T_OK` acknowledgement is `00 00 00 00 'T' '_' 'O' 'K'`). The command
 * mnemonics group into login / logout (`A_LG` / `A_LO`), file system (`C_*` write, `R_*` read), and
 * run / status inspection (`R_RI` / `R_ST` / `R_VR`); the control answers with a response mnemonic
 * (`T_OK` done, `T_FD` file-data finished, `S_*` a data reply, or `T_ER` / `T_BD` a two-byte
 * error-class + error-code).
 *
 * This codec owns the framing and the request builders: @ref pc_lsv2_build frames an arbitrary
 * mnemonic + payload, @ref pc_lsv2_parse slices one complete telegram off a byte stream (reporting how
 * many bytes it consumed so a caller can re-frame the rest), and the typed helpers build the common
 * requests (@ref pc_lsv2_build_login, @ref pc_lsv2_build_filename, @ref pc_lsv2_build_run_info) and
 * read the response (@ref pc_lsv2_is_ok, @ref pc_lsv2_error). The TCP / serial link to the control is
 * the application's.
 *
 * Reference: the LSV/2 telegram framing + the command / response mnemonic set, cross-checked
 * byte-for-byte against the `pyLSV2` reference implementation (drunsinn/pyLSV2, the widely-used open
 * Python LSV/2 client) - the `!L` big-endian length prefix, the 4-character mnemonic, and the `!H`
 * big-endian run-info selector.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LSV2_H
#define PROTOCORE_LSV2_H

#include "protocore_config.h"

#if PC_ENABLE_LSV2

/** @brief Default LSV/2-over-TCP port a Heidenhain control listens on. */
#define PC_LSV2_TCP_PORT 19000
/** @brief Every LSV/2 command / response mnemonic is exactly four ASCII characters. */
#define PC_LSV2_MNEMONIC_LEN 4
/** @brief Fixed telegram header: a 4-byte length prefix + the 4-byte mnemonic (payload follows). */
#define PC_LSV2_HEADER_LEN 8

// ── command mnemonics (CP -> control) ──────────────────────────────────────────────────────────
#define PC_LSV2_CMD_LOGIN "A_LG"       ///< gain access to a privilege group (payload: login[+password])
#define PC_LSV2_CMD_LOGOUT "A_LO"      ///< drop a privilege group (payload: optional login)
#define PC_LSV2_CMD_FILE_LOAD "R_FL"   ///< read a file from the control (payload: filename)
#define PC_LSV2_CMD_FILE_SEND "C_FL"   ///< send a file to the control (payload: filename)
#define PC_LSV2_CMD_FILE_DELETE "C_FD" ///< delete a file (payload: filename)
#define PC_LSV2_CMD_FILE_INFO "R_FI"   ///< read info about a file (payload: filename)
#define PC_LSV2_CMD_DIR_CHANGE "C_DC"  ///< change the working directory (payload: path)
#define PC_LSV2_CMD_DIR_MAKE "C_DM"    ///< create a directory (payload: path)
#define PC_LSV2_CMD_DIR_DELETE "C_DD"  ///< delete a directory (payload: path)
#define PC_LSV2_CMD_DIR_READ "R_DR"    ///< read directory contents
#define PC_LSV2_CMD_DIR_INFO "R_DI"    ///< read info about the selected directory
#define PC_LSV2_CMD_RUN_INFO "R_RI"    ///< read run info (payload: 2-byte big-endian selector)
#define PC_LSV2_CMD_STATUS "R_ST"      ///< request remote status
#define PC_LSV2_CMD_VERSION "R_VR"     ///< read control version / identity info
#define PC_LSV2_CMD_PARAM "R_PR"       ///< read a parameter from the control

// ── response mnemonics (control -> CP) ─────────────────────────────────────────────────────────
#define PC_LSV2_RSP_OK "T_OK"       ///< last transaction completed
#define PC_LSV2_RSP_ERROR "T_ER"    ///< transaction error (payload: error-class, error-code)
#define PC_LSV2_RSP_FIN "T_FD"      ///< all file data sent, transfer finished
#define PC_LSV2_RSP_XFER_ERR "T_BD" ///< file-transfer error (payload: error-class, error-code)
#define PC_LSV2_RSP_LONG "M_CC"     ///< a long-running operation completed
#define PC_LSV2_RSP_RUN_INFO "S_RI" ///< run-info data reply
#define PC_LSV2_RSP_STATUS "S_ST"   ///< status data reply
#define PC_LSV2_RSP_VERSION "S_VR"  ///< version data reply
#define PC_LSV2_RSP_FILE "S_FL"     ///< file-content data reply
#define PC_LSV2_RSP_DIR "S_DR"      ///< directory-content data reply

// ── login privilege groups (payload for A_LG) ──────────────────────────────────────────────────
#define PC_LSV2_LOGIN_INSPECT "INSPECT"   ///< read machine state / parameters
#define PC_LSV2_LOGIN_FILE "FILE"         ///< file-system access
#define PC_LSV2_LOGIN_DNC "DNC"           ///< DNC functions
#define PC_LSV2_LOGIN_MONITOR "MONITOR"   ///< screen / keypad monitoring
#define PC_LSV2_LOGIN_DIAG "DIAGNOSTICS"  ///< diagnostics
#define PC_LSV2_LOGIN_PLCDEBUG "PLCDEBUG" ///< PLC debug

/** @brief Run-info selectors for @ref pc_lsv2_build_run_info (the 2-byte `R_RI` argument). */
enum Lsv2RunInfo : uint16_t // NOSONAR(cpp:S3642): unscoped so a selector converts to the 2-byte R_RI
                            // argument without a cast; no other enum in this header shares its value space
{
    LSV2_RI_EXEC_STATE = 23,   ///< program execution state (running / stopped / ...)
    LSV2_RI_SELECTED_PGM = 24, ///< currently selected program
    LSV2_RI_OVERRIDE = 25,     ///< feed / rapid / spindle override values
    LSV2_RI_PGM_STATE = 26,    ///< program status
};

/** @brief One parsed telegram: the 4-char mnemonic (NOT null-terminated) plus the payload slice, which
 *  points INTO the caller's buffer (zero-copy; the buffer must outlive the struct). */
typedef struct
{
    char mnemonic[PC_LSV2_MNEMONIC_LEN];
    const uint8_t *payload;
    size_t payload_len;
} Lsv2Telegram;

/**
 * @brief Frame a telegram: a 4-byte big-endian @p payload_len, the 4-character @p mnemonic, then the
 *        payload. @p mnemonic must point at (at least) four characters; exactly four are copied.
 * @return total bytes written (`PC_LSV2_HEADER_LEN + payload_len`), or 0 on overflow / bad input.
 */
size_t pc_lsv2_build(uint8_t *buf, size_t cap, const char *mnemonic, const uint8_t *payload, size_t payload_len);

/**
 * @brief Slice one complete telegram off @p buf: read the big-endian length prefix, and if the whole
 *        telegram (header + payload) is present, fill @p out and report the byte count via @p consumed
 *        (so the caller can advance past it and re-parse the remainder).
 * @return true if a complete telegram was found; false if fewer than `8 + payload_len` bytes are
 *         available yet (the caller should accumulate more).
 */
proto_bool pc_lsv2_parse(const uint8_t *buf, size_t len, Lsv2Telegram *out, size_t *consumed);

/**
 * @brief True if the parsed telegram's mnemonic equals the four characters at @p mnemonic4.
 */
proto_bool pc_lsv2_is(const Lsv2Telegram *t, const char *mnemonic4);

/**
 * @brief Build a login telegram (`A_LG`): payload is the NUL-terminated privilege group (one of the
 *        `PC_LSV2_LOGIN_*` strings), optionally followed by a NUL-terminated @p password.
 * @param password pass nullptr for a group that needs no password.
 * @return total bytes written, or 0 on overflow / bad input.
 */
size_t pc_lsv2_build_login(uint8_t *buf, size_t cap, const char *login, const char *password);

/**
 * @brief Build a logout telegram (`A_LO`): payload is the NUL-terminated privilege group to drop, or
 *        empty when @p login is nullptr (log out of everything).
 * @return total bytes written, or 0 on overflow / bad input.
 */
size_t pc_lsv2_build_logout(uint8_t *buf, size_t cap, const char *login);

/**
 * @brief Build a filename-argument command: the 4-char @p mnemonic (`R_FL` / `C_FL` / `C_FD` / `C_DC` /
 *        `C_DM` / `C_DD` / `R_FI`) followed by the NUL-terminated @p filename as the payload.
 * @return total bytes written, or 0 on overflow / bad input.
 */
size_t pc_lsv2_build_filename(uint8_t *buf, size_t cap, const char *mnemonic, const char *filename);

/**
 * @brief Build a run-info request (`R_RI`) with @p info_code (an @ref Lsv2RunInfo selector) as a 2-byte
 *        big-endian payload.
 * @return total bytes written (always `PC_LSV2_HEADER_LEN + 2` on success), or 0 on overflow.
 */
size_t pc_lsv2_build_run_info(uint8_t *buf, size_t cap, uint16_t info_code);

/**
 * @brief True if the response is `T_OK` (transaction completed).
 */
proto_bool pc_lsv2_is_ok(const Lsv2Telegram *t);

/**
 * @brief True if the response is an error mnemonic (`T_ER` transaction error or `T_BD` transfer error).
 */
proto_bool pc_lsv2_is_error(const Lsv2Telegram *t);

/**
 * @brief Decode an error response's two-byte payload into @p err_class and @p err_code.
 * @return true on an error mnemonic (`T_ER` / `T_BD`) carrying exactly two payload bytes.
 */
proto_bool pc_lsv2_error(const Lsv2Telegram *t, uint8_t *err_class, uint8_t *err_code);

#endif // PC_ENABLE_LSV2

#endif // PROTOCORE_LSV2_H
