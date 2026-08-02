// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file haas_mdc.h
 * @brief Haas Machine Data Collection (MDC) Q-command codec (PC_ENABLE_HAAS_MDC) - a zero-heap codec
 *        for the documented Haas Automation MDC protocol, the `?Q` query set a Haas CNC mill / lathe
 *        control answers over RS-232 (7-E-1, XON/XOFF) or a raw TCP socket (Setting 143, default port
 *        5051). A small, fully-documented CNC read source that fans machine status into HTTP / MQTT.
 *
 * The device is the collector: it sends `?Q###` queries and the control replies with a framed,
 * comma-delimited payload. This codec builds the queries (@ref pc_haas_mdc_build_q for the numbered
 * commands and @ref pc_haas_mdc_build_var for the `?Q600 <var>` macro/system-variable read) and
 * parses the responses (@ref pc_haas_mdc_parse extracts the CSV payload framed between STX and ETB,
 * then @ref pc_haas_mdc_value / @ref pc_haas_mdc_parse_status / @ref pc_haas_mdc_parse_macro read
 * the typed forms). It also de-multiplexes the unprompted `DPRNT(...)` lines a running G-code program
 * pushes on the same link (@ref pc_haas_mdc_dprnt_line - raw text, no STX/ETB).
 *
 * Wire framing (byte-exact): a request is `?Q###` + CR (`\r`), UPPERCASE. A response payload lives
 * strictly between STX (0x02) and ETB (0x17), followed by CR LF and a `>` (0x3E) prompt byte, e.g.
 * `?Q100\r` -> `\x02SERIAL NUMBER, 1234567\x17\r\n>`. Parse defensively by scanning for the STX/ETB
 * delimiters, not fixed offsets. Q500 has two branches (`PROGRAM, Oxxxxx, <status>, PARTS, n` when
 * idle/running vs `STATUS, BUSY` mid-operation); an unsupported command returns `UNKNOWN`.
 *
 * Reference: Haas Automation service manual "Machine Data Collection" (Setting 143) + the Q-command
 * table and DPRNT how-to; framing cross-checked byte-for-byte against a production Haas serial adapter.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HAAS_MDC_H
#define PROTOCORE_HAAS_MDC_H

#include "protocore_config.h"

#if PC_ENABLE_HAAS_MDC

/** @brief Default Haas MDC TCP port (Setting 143 "Machine Data Collect"). */
#define PC_HAAS_MDC_TCP_PORT 5051
/** @brief Start-of-payload byte in an MDC response frame. */
#define PC_HAAS_MDC_STX 0x02
/** @brief End-of-payload byte in an MDC response frame. */
#define PC_HAAS_MDC_ETB 0x17
/** @brief Trailing ready/prompt byte the control emits after a response. */
#define PC_HAAS_MDC_PROMPT 0x3E
/** @brief Maximum comma-separated fields kept from a response payload (Q500 has 5). */
#define PC_HAAS_MDC_MAX_FIELDS 8

/** @brief The documented numbered Q queries (pass to @ref pc_haas_mdc_build_q). Unscoped so a value
 *  converts to the `uint16_t` command number without a cast; `?Q600` takes an argument, so it has its
 *  own builder (@ref pc_haas_mdc_build_var) rather than a member here. */
enum HaasQ : uint16_t // NOSONAR(cpp:S3642): unscoped is the documented contract - the builder accepts any Haas
                      // command number, and this header has no second enum whose values could be confused
{
    HAAS_Q_SERIAL = 100,         ///< machine serial number
    HAAS_Q_SOFTWARE = 101,       ///< control software version
    HAAS_Q_MODEL = 102,          ///< machine model number
    HAAS_Q_MODE = 104,           ///< mode (MDI / MEM / JOG / ZERORET / LIST PROG)
    HAAS_Q_TOOL_CHANGES = 200,   ///< total tool changes
    HAAS_Q_TOOL_IN_USE = 201,    ///< tool number in use
    HAAS_Q_POWERON_TIME = 300,   ///< total power-on time
    HAAS_Q_CUTTING_TIME = 301,   ///< total part-cutting (motion) time
    HAAS_Q_LAST_CYCLE = 303,     ///< last completed cycle time
    HAAS_Q_PREV_CYCLE = 304,     ///< previous cycle time
    HAAS_Q_M30_COUNTER_1 = 402,  ///< M30 parts counter #1
    HAAS_Q_M30_COUNTER_2 = 403,  ///< M30 parts counter #2
    HAAS_Q_PROGRAM_STATUS = 500, ///< active program + run status + parts counter (three-in-one)
};

/** @brief A parsed response: the comma-separated payload fields, each trimmed of surrounding spaces
 *  and pointing INTO the caller's buffer (zero-copy; the buffer must outlive the struct). */
typedef struct
{
    const char *field[PC_HAAS_MDC_MAX_FIELDS];
    size_t field_len[PC_HAAS_MDC_MAX_FIELDS];
    uint8_t n_fields;
} HaasMdcResp;

/** @brief The decoded Q500 (program + run status + parts) response. */
typedef struct
{
    proto_bool busy;     ///< true when the control returned `STATUS, BUSY` (no program/parts available)
    const char *program; ///< selected program (`Oxxxxx` or a name); NULL when @ref busy
    size_t program_len;
    const char *status; ///< run-status token (IDLE / FEED HOLD / ALARM / ...); `BUSY` when @ref busy
    size_t status_len;
    uint32_t parts;         ///< parts counter (valid only when @ref parts_valid)
    proto_bool parts_valid; ///< false when @ref busy or the parts field was absent / non-numeric
} HaasMdcStatus;

/**
 * @brief Build a numbered query line: `?Q<qnum>` + CR (e.g. `pc_haas_mdc_build_q(..., HAAS_Q_SERIAL)`
 *        -> `"?Q100\r"`). Pass a @ref HaasQ value or a raw command number.
 * @return characters written (excluding the NUL), or 0 on overflow / bad input.
 */
size_t pc_haas_mdc_build_q(char *buf, size_t cap, uint16_t qnum);

/**
 * @brief Build a macro/system-variable read: `?Q600 <var>` + CR (e.g. `pc_haas_mdc_build_var(..., 100)`
 *        -> `"?Q600 100\r"`). Reads any readable macro (e.g. #1-999) or system variable.
 * @return characters written (excluding the NUL), or 0 on overflow / bad input.
 */
size_t pc_haas_mdc_build_var(char *buf, size_t cap, uint32_t var);

/**
 * @brief Parse a response frame: locate the payload between STX (0x02) and ETB (0x17) - scanning, not
 *        by offset - and split it into comma-separated fields, each trimmed of surrounding spaces and
 *        pointing into @p buf. CR/LF and the `>` prompt outside the STX/ETB window are ignored.
 * @return true if a complete `STX ... ETB` frame with at least one field was found; false otherwise
 *         (no frame yet - the caller should accumulate more bytes).
 */
proto_bool pc_haas_mdc_parse(const char *buf, size_t len, HaasMdcResp *out);

/**
 * @brief Random-access a parsed field. @p p / @p l receive a pointer into the original buffer + length.
 * @return true if @p idx < n_fields.
 */
proto_bool pc_haas_mdc_field(const HaasMdcResp *r, size_t idx, const char **p, size_t *l);

/**
 * @brief The value of a simple `LABEL, value` response - field[1] (e.g. the serial for Q100, the
 *        version for Q101, the mode token for Q104).
 * @return true if the response has at least two fields.
 */
proto_bool pc_haas_mdc_value(const HaasMdcResp *r, const char **p, size_t *l);

/**
 * @brief True if the response is the control's `UNKNOWN` error (an unsupported / lowercase command).
 */
proto_bool pc_haas_mdc_is_error(const HaasMdcResp *r);

/**
 * @brief Decode a Q500 response into @ref HaasMdcStatus, handling both branches: `PROGRAM, Oxxxxx,
 *        <status>, PARTS, n` (sets program / status / parts) and `STATUS, BUSY` (sets @ref
 *        HaasMdcStatus::busy). All string members point into the parsed buffer.
 * @return true if the response is a recognizable Q500 form; false otherwise.
 */
proto_bool pc_haas_mdc_parse_status(const HaasMdcResp *r, HaasMdcStatus *out);

/**
 * @brief Decode a Q600 response `MACRO, <var>, <value>`. @p var receives the variable number; @p value
 *        / @p value_len point at the value string (trimmed - it is a fixed-width, space-padded decimal
 *        on the wire; exposed as text so the caller keeps full precision without a float parse).
 * @return true on a well-formed `MACRO, ...` response with a numeric variable field.
 */
proto_bool pc_haas_mdc_parse_macro(const HaasMdcResp *r, uint32_t *var, const char **value, size_t *value_len);

/**
 * @brief Extract an unprompted `DPRNT(...)` line pushed by a running program: a raw ASCII text line
 *        with NO STX/ETB frame, terminated by CR/LF and optionally bracketed by DC2 (POPEN, 0x12) /
 *        DC4 (PCLOS, 0x14). Strips leading prompt/newline/DC2 and trailing CR/LF/DC4, preserving any
 *        interior spaces (a DPRNT `*` arrives as a space and may be significant).
 * @return true for a non-empty pushed line; false if @p buf contains an STX (it is a framed Q
 *         response, not DPRNT) or is empty after stripping.
 */
proto_bool pc_haas_mdc_dprnt_line(const char *buf, size_t len, const char **text, size_t *text_len);

#endif // PC_ENABLE_HAAS_MDC

#endif // PROTOCORE_HAAS_MDC_H
