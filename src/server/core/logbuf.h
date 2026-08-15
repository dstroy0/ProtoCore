// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file logbuf.h
 * @brief Fixed-RAM rotating log buffer with severity traps (PROTOCORE_ENABLE_LOGBUF).
 *
 * Keeps the last PROTOCORE_LOG_LINES log lines in a fixed ring (the oldest is pruned
 * on overflow - no heap, bounded latency), each line stored as `<L> message`
 * where L is the severity letter. Dump the ring oldest-first for a `/logs`
 * endpoint, and register a trap callback that fires when a line is logged at or
 * above a severity threshold (forward criticals as an SNMP trap / webhook). Pure
 * and fully host-tested - no vendor dependency.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LOGBUF_H
#define PROTOCORE_LOGBUF_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_LOGBUF

/** @brief Severity levels (ordered low -> high). Compared (level >= threshold) and passed through the
 *  uint8_t trap-callback ABI, so integer constants in a namespacing struct - cast-free. */
#define PROTOCORE_LOG_DEBUG 0
#define PROTOCORE_LOG_INFO 1
#define PROTOCORE_LOG_WARN 2
#define PROTOCORE_LOG_ERROR 3

/** @brief Trap callback: fired for a line logged at level >= the threshold. */
typedef void (*protocore_log_trap_fn)(uint8_t level, const char *line);

/** @brief What one append carries. */
typedef struct
{
    uint8_t level;   ///< severity; the line is stored as `<L> msg`
    const char *msg; ///< the message; NULL renders empty
} LogLineArgs;

/** @brief Where a dump writes, and the line a lookup names. */
typedef struct
{
    char *out;  ///< where a dump writes the held lines, oldest-first, newline-separated
    size_t cap; ///< how much room it has
    uint16_t i; ///< the line a lookup names (0 = oldest .. count-1 = newest)
} LogReadArgs;

/** @brief What a trap fires on. */
typedef struct
{
    uint8_t threshold;        ///< fire for a line logged at this level or above; 0xFF disables
    protocore_log_trap_fn cb; ///< what fires; NULL leaves the trap off
} LogTrapArgs;

/** @brief The ring's own state and the calls that reach it, described only in logbuf.c. */
struct LogbufInternal;

/**
 * @brief The in-memory log ring.
 *
 * A caller sets the members a call takes, invokes it through ::Logbuf, and reads the outcome off the
 * same handle. The ring itself is behind @ref internal.
 *
 * @var LogbufNs::line      what one append carries
 * @var LogbufNs::read      where a dump writes, and the line a lookup names
 * @var LogbufNs::trap      what a trap fires on
 * @var LogbufNs::count     lines currently held (0 .. PROTOCORE_LOG_LINES)
 * @var LogbufNs::text      the line a lookup reports, or NULL when the index is out of range
 * @var LogbufNs::n         characters a dump wrote, or 0 when the buffer is too small
 * @var LogbufNs::reset     empty the ring and clear the line count
 * @var LogbufNs::put       append one line, truncated to fit
 * @var LogbufNs::held      how many lines are held
 * @var LogbufNs::at        one held line by index
 * @var LogbufNs::dump      every held line, oldest-first, newline-separated
 * @var LogbufNs::set_trap  install the severity trap
 * @var LogbufNs::internal  the ring and the calls that reach it
 *
 * dump fails closed: a buffer that cannot hold every line writes nothing and reports 0.
 */
typedef struct
{
    LogLineArgs line;
    LogReadArgs read;
    LogTrapArgs trap;

    uint16_t count;
    const char *text;
    int n;

    void (*reset)(struct LogbufInternal *ctx);
    void (*put)(struct LogbufInternal *ctx);
    void (*held)(struct LogbufInternal *ctx);
    void (*at)(struct LogbufInternal *ctx);
    void (*dump)(struct LogbufInternal *ctx);
    void (*set_trap)(struct LogbufInternal *ctx);

    struct LogbufInternal *internal;
} LogbufNs;

/** @brief The one symbol this module exports. */
extern LogbufNs Logbuf;

#endif // PROTOCORE_ENABLE_LOGBUF

PROTOCORE_END_DECLS

#endif // PROTOCORE_LOGBUF_H
