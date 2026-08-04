// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dnc.h
 * @brief CNC RS-232 DNC (Distributed Numerical Control) drip-feed codec (PC_ENABLE_DNC).
 *
 * DNC is the classic way a program is streamed to a machine-tool controller: a G-code
 * program (RS-274 / ISO 6983) is punched as blocks (one line each), framed with a
 * `%` rewind-stop at the start and end, and drip-fed over an RS-232 link with XON/XOFF
 * software flow control so the sender pauses when the controller's small input buffer
 * fills. This codec is the transport-agnostic framing + tape-code layer only - the same
 * bytes ride RS-232, a raw TCP socket, or a WebSocket; the app owns the wire.
 *
 * Two tape codes are supported (the historical split every DNC package still carries):
 *  - **ISO** (::DNC_CODE_ISO): ISO 7-bit / ASCII, End-of-Block = LF, program marker = `%`.
 *    Optional even parity in bit 7 (the ISO tape convention, RS-358).
 *  - **EIA** (::DNC_CODE_EIA): the EIA RS-244 punched-tape code - a distinct, odd-parity
 *    8-track encoding (parity in channel 5). End-of-Block = 0x80 (channel 8, used only for
 *    EOB), the rewind-stop is EIA End-of-Record 0x0B (the `%` equivalent), and there are no
 *    lowercase letters. The full character table is odd-parity-verified.
 *
 * Three pieces, all pure and zero-heap:
 *  1. character translation (::pc_dnc_iso_to_eia / ::pc_dnc_eia_to_iso) + ISO even-parity helper;
 *  2. XON/XOFF flow state (::DncFlow) the send pump consults before each write;
 *  3. a streaming block encoder (::pc_dnc_encode_block + the `%`/leader framing) and a
 *     byte-at-a-time block decoder (::DncDecoder) that reassembles wire bytes back into
 *     ASCII G-code lines and reports the `%` program start/end.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNC_H
#define PROTOCORE_DNC_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_DNC

/** @brief Which punched-tape character code the wire stream uses. */
typedef enum PROTO_ENUM_PACKED
{
    DNC_CODE_ISO = 0, ///< ISO 7-bit / ASCII (RS-358), EOB = LF, marker = '%'.
    DNC_CODE_EIA = 1, ///< EIA RS-244 odd-parity tape code, EOB = 0x80, marker = EOR 0x0B.
} DncCode;

/** @brief Software flow-control bytes (both tape codes; these are the raw ASCII controls). */
typedef enum PROTO_ENUM_PACKED
{
    DNC_XON = 0x11,  ///< DC1 - resume sending.
    DNC_XOFF = 0x13, ///< DC3 - pause sending.
} DncFlowByte;

/** @brief EIA RS-244 special codes (not general text characters). */
typedef enum PROTO_ENUM_PACKED
{
    DNC_EIA_EOB = 0x80, ///< EIA End-of-Block (channel 8 only; the ISO LF equivalent).
    DNC_EIA_EOR = 0x0B, ///< EIA End-of-Record (the ISO '%' rewind-stop equivalent).
    DNC_EIA_DEL = 0x7F, ///< EIA Delete / rubout (leader/trailer runout; skipped on read).
} DncEiaCode;

/**
 * @brief Translate one ISO/ASCII character to its EIA RS-244 byte.
 *
 * Covers the NC character set: digits, uppercase A-Z, space, `. - + / %` and Tab, plus the
 * `%` rewind-stop (mapped to EIA End-of-Record 0x0B). Every returned byte carries EIA odd
 * parity (channel 5).
 *
 * @return the EIA byte, or 0xFF if @p c has no EIA representation (fail-closed).
 */
uint8_t pc_dnc_iso_to_eia(char c);

/**
 * @brief Translate one EIA RS-244 byte back to its ISO/ASCII character.
 *
 * The inverse of ::pc_dnc_iso_to_eia. EIA End-of-Record (0x0B) maps back to '%'.
 *
 * @return the ASCII character, or 0 if @p b is not a known EIA code (e.g. blank/runout).
 */
char pc_dnc_eia_to_iso(uint8_t b);

/**
 * @brief Set even parity in bit 7 of a 7-bit ASCII value (the ISO tape convention).
 * @param ascii7 a value in 0x00-0x7F (bit 7 is ignored on input).
 * @return @p ascii7 with bit 7 set so the byte has an even number of 1 bits.
 */
uint8_t pc_dnc_iso_add_parity(uint8_t ascii7);

/** @brief XON/XOFF software flow-control state for the send side. */
typedef struct
{
    proto_bool paused; ///< true after XOFF (DC3), cleared by XON (DC1).
} DncFlow;

/** @brief Reset flow state to "clear to send". */
void pc_dnc_flow_init(DncFlow *f);

/**
 * @brief Feed one received byte to the flow-control state machine.
 * @return true if @p rx was a flow-control byte (XON/XOFF) and was consumed; false otherwise
 *         (the byte is ordinary inbound data the caller still owns).
 */
proto_bool pc_dnc_flow_feed(DncFlow *f, uint8_t rx);

/** @brief Whether the send pump may transmit (i.e. not paused by an XOFF). */
static inline proto_bool pc_dnc_flow_can_send(const DncFlow *f)
{
    return !f->paused;
}

/** @brief Encoder configuration - the tape code and its framing options. */
typedef struct
{
    DncCode code;           ///< ISO or EIA.
    proto_bool even_parity; ///< ISO only: emit even parity in bit 7 (ignored for EIA, which is always odd).
    proto_bool crlf;        ///< ISO only: emit CR before the LF End-of-Block (some controllers want CR LF).
    uint16_t leader_len;    ///< leader/trailer runout length in bytes (::pc_dnc_encode_leader / _trailer).
} DncCfg;

/**
 * @brief Frame one G-code source line as a block (its characters + an End-of-Block).
 *
 * The source is plain ASCII with no terminator. Each character is translated to the
 * configured tape code (ISO passes 7-bit through, adding even parity if requested; EIA maps
 * via ::pc_dnc_iso_to_eia), then the End-of-Block is appended (ISO: optional CR then LF; EIA: 0x80).
 *
 * @return bytes written to @p out, or 0 on overflow or a character with no EIA
 *         representation (fail-closed - nothing partial is emitted as a complete block).
 */
size_t pc_dnc_encode_block(const DncCfg *cfg, const char *line, size_t line_len, uint8_t *out, size_t out_cap);

/**
 * @brief Emit the `%` program-start (or -end) marker followed by an End-of-Block.
 *
 * ISO writes '%' (with parity if configured); EIA writes End-of-Record (0x0B). Both then
 * write the End-of-Block. Start and end are byte-identical; call it at both ends of the program.
 *
 * @return bytes written, or 0 on overflow.
 */
size_t pc_dnc_encode_marker(const DncCfg *cfg, uint8_t *out, size_t out_cap);

/**
 * @brief Emit @ref DncCfg::leader_len runout bytes (NUL - skipped by the reader until `%`).
 * @return bytes written (== leader_len), or 0 if @p out_cap is too small.
 */
size_t pc_dnc_encode_leader(const DncCfg *cfg, uint8_t *out, size_t out_cap);

/** @brief What ::pc_dnc_decode_feed produced for the byte just fed. */
typedef enum PROTO_ENUM_PACKED
{
    DNC_EV_NONE = 0,   ///< byte absorbed (mid-block, runout, or flow/ignored); nothing to report.
    DNC_EV_LINE,       ///< a complete non-empty block is ready in DncDecoder::line (NUL-terminated).
    DNC_EV_PROG_START, ///< the first `%` / EOR was seen (program start).
    DNC_EV_PROG_END,   ///< a later `%` / EOR was seen (program end).
    DNC_EV_OVERFLOW,   ///< the current block exceeded PC_DNC_LINE_MAX; it was dropped.
} DncEvent;

/** @brief Streaming block reassembler: wire bytes in, ASCII G-code lines out. */
typedef struct
{
    DncCode code;                   ///< the tape code being decoded.
    char line[PC_DNC_LINE_MAX + 1]; ///< the current block, NUL-terminated when DNC_EV_LINE fires.
    uint16_t len;                   ///< bytes accumulated in @ref line so far (the line length on DNC_EV_LINE).
    proto_bool overflow;            ///< the current block overran; drop until the next End-of-Block.
    proto_bool in_program;          ///< a program-start `%` has been seen (so the next `%` is the end).
    proto_bool line_ready;          ///< internal: the previous feed delivered a line; reset on the next feed.
} DncDecoder;

/** @brief Reset a decoder for a given tape code. */
void pc_dnc_decode_init(DncDecoder *d, DncCode code);

/**
 * @brief Feed one wire byte to the block reassembler.
 *
 * Strips parity (ISO) / translates (EIA), skips runout (NUL / DEL / CR), accumulates a
 * block until its End-of-Block, and reports the `%` program markers. XON/XOFF are not filtered
 * here - flow control rides the reverse channel (see ::pc_dnc_flow_feed); in the forward program
 * stream 0x13 is the EIA data character '3', not DC3. On ::DNC_EV_LINE the
 * completed line is in @ref DncDecoder::line (NUL-terminated) and @ref DncDecoder::len is its
 * length; both are reset on the next call.
 *
 * @return the event for this byte (see ::DncEvent).
 */
DncEvent pc_dnc_decode_feed(DncDecoder *d, uint8_t wire);

#endif // PC_ENABLE_DNC

PROTO_END_DECLS

#endif // PROTOCORE_DNC_H
