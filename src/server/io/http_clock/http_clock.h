// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_clock.h
 * @brief Which clock the HTTP `Date` header reads, and the IMF-fixdate it renders.
 *
 * One decision in one place: a build with the multi-source time registry takes the epoch from
 * protocore_time_now(), a build with only the NTP client takes it from that, and a build with
 * neither has no wall clock and emits no `Date` at all. ::HttpDate does the rendering; this module
 * says what instant to render and holds the result until the caller has copied it.
 *
 * It exists because that decision used to live in two escape hatches - `protocore_time_http_date`
 * in time_source and `protocore_ntp_http_date` in ntp_service - each defined OUTSIDE its own
 * module's enable gate so response.c could call it either way. That is what kept both modules in
 * every binary: gen_modules.py reads a module's gate off its own source, and a file with code
 * outside its gate has no gate to read, so CMake adds it to every target and its symbols are
 * reachable from translation units that never asked for a clock. With the sometimes-used part
 * isolated here, behind one consumer, both clocks are gated for real.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HTTP_CLOCK_H
#define PROTOCORE_HTTP_CLOCK_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP_CLOCK

PROTOCORE_BEGIN_DECLS

/**
 * @brief The operands and the outcome.
 *
 * No args record: the instant is whatever the build's clock reads now, and the destination is a
 * region of the borrow rather than a buffer the caller states.
 *
 * @var HttpClockVars::n    characters written, or 0 when no source has valid time yet
 * @var HttpClockVars::imf  the rendered date, inside the borrow the call was handed
 */
typedef struct
{
    uint8_t n;
    const char *imf;
} HttpClockVars;

extern HttpClockVars HttpClockV;

/**
 * @brief The `Date` header's instant, rendered.
 *
 * @var HttpClockNs::n    characters written, or 0 when no source has valid time yet
 * @var HttpClockNs::imf  the rendered date, inside the borrow the call was handed
 * @var HttpClockNs::date  render the current instant from whichever clock this build compiled in.
 *                         Writes nothing and reports 0 before any source has valid time, which is
 *                         the correct answer for a clock-less boot: RFC 7231 sec 7.1.1.2 has a
 *                         `Date` omitted rather than guessed.
 */
typedef struct
{
    uint8_t n;
    const char *imf;
    void (*const date)(uint8_t *restrict work);
} HttpClockNs;

void protocore_http_clock_date(uint8_t *restrict work);

/**
 * @brief The bytes this module runs out of, for a caller that holds no borrow of its own.
 *
 * response.c is the one consumer and is built on file-scope state, so no borrow is in scope
 * anywhere along the response path. It forwards this span and the entry carves its region out of
 * it at ::PROTOCORE_HTTP_CLOCK_BORROW's fixed offset.
 */
uint8_t *protocore_http_clock_span(void);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `HttpClock.date(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HttpClockNs HttpClock __attribute__((unused)) = {
    .date = protocore_http_clock_date,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_CLOCK

#endif // PROTOCORE_HTTP_CLOCK_H
