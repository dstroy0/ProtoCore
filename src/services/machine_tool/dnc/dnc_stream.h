// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dnc_stream.h
 * @brief DNC drip-feed engine (PC_ENABLE_DNC) - stream a whole G-code program over a transport,
 *        pacing on reverse-channel XON/XOFF.
 *
 * The dnc codec (dnc.h) is pure framing; this drives the exchange: it emits the leader, the `%`
 * program-start marker, every source line as a block, the `%` end marker, and the trailer, honoring
 * software flow control - when the controller sends XOFF (DC3) it pauses and waits for XON (DC1)
 * before the next write. Like the SMB / SMTP engines it works against a send/recv seam, so it is
 * transport-agnostic (the same engine drip-feeds over a raw TCP socket for "Ethernet DNC" or over a
 * UART for classic RS-232 DNC) and host-testable with a scripted mock controller.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNC_STREAM_H
#define PROTOCORE_DNC_STREAM_H

#include "protocore_config.h"

#if PC_ENABLE_DNC

#include "dnc.h"

/** @brief Result of a drip-feed. 0 is success; each failure is a distinct code. */
typedef enum PROTO_ENUM_PACKED
{
    DNC_STREAM_OK = 0,
    DNC_STREAM_ERR_ARG = -1,    ///< a required argument was null
    DNC_STREAM_ERR_IO = -2,     ///< a send/recv failed, or XOFF never cleared (see PC_DNC_XOFF_MAX_POLLS)
    DNC_STREAM_ERR_ENCODE = -3, ///< a source line had no representation in the tape code, or overran a block
} DncStreamResult;

/**
 * @brief Transport seam: the engine moves bytes only through these, so it runs over TCP, a UART, or
 *        a test mock.
 * @return send: bytes written (must equal @p len), else < 0.
 * @return recv: reverse-channel bytes read (>= 0, 0 if none available now), or < 0 on error / close.
 *         While paused by XOFF the engine polls @c recv for XON, so a real transport should pace it
 *         (block briefly when idle) rather than spin.
 */
using DncSendFn = int (*)(void *ctx, const uint8_t *data, size_t len);
using DncRecvFn = int (*)(void *ctx, uint8_t *buf, size_t cap);

/**
 * @brief Drip-feed @p program (plain ASCII G-code, lines separated by LF; a trailing CR is stripped)
 *        as a framed DNC stream over @p send / @p recv, pausing on XOFF.
 *
 * Each line must fit PC_DNC_LINE_MAX; a longer or untranslatable line fails closed with
 * ::DNC_STREAM_ERR_ENCODE (nothing partial is left mid-block). An empty @p program still frames the
 * `%` start/end markers.
 *
 * @return ::DNC_STREAM_OK, or a ::DncStreamResult error.
 */
DncStreamResult dnc_stream(const DncCfg *cfg, const char *program, size_t prog_len, DncSendFn send, DncRecvFn recv,
                           void *ctx);

#endif // PC_ENABLE_DNC

#endif // PROTOCORE_DNC_STREAM_H
