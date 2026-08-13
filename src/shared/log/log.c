// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file log.c
 * @brief The emitted half of the PROTOCORE_LOG* macros (see log.h).
 *
 * Nothing here is compiled when PROTOCORE_LOG_LEVEL is PROTOCORE_NONE - not even the sink pointer - so a build that
 * logs nothing links no logging code and spends no BSS on it.
 */

#include "shared/log/log.h"

#include "mmgr/protoframe.h" // frame.build: the line is a spec, not a format string

#if PROTOCORE_LOG_LEVEL < PROTOCORE_LOG_LEVEL_NONE

#include <stdarg.h>

#if PROTOCORE_ENABLE_LOGBUF
#include "server/core/logbuf.h"
#endif

/**
 * @brief The logger's compile-time storage: just the sink a formatted line is handed to.
 */
struct LogStorage
{
    protocore_log_sink_fn sink;
};

/**
 * @brief The sink and the calls that reach it - what LogNs points at.
 *
 * @var LogInternal::store  the installed sink
 * @var LogInternal::ns     the handle a caller sets a call's members on
 */
struct LogInternal
{
    struct LogStorage *store;
    LogNs *ns;
};

static struct LogStorage s_store = {NULL};

static struct LogInternal s_log = {.store = &s_store, .ns = &Log};

static void log_set_sink(struct LogInternal *restrict ctx)
{
    ctx->store->sink = ctx->ns->sink;
}

static void log_emit(struct LogInternal *restrict ctx)
{
    const uint8_t level = ctx->ns->frame.level;
    const struct protocore_field *spec = ctx->ns->frame.spec;

    if (!spec)
    {
        return;
    }

    // One line's worth of stack, matching what the ring can store. A spec whose worst case exceeds
    // it is a spec that was written wrong, not a runtime condition to absorb: every log frame
    // states its literals' lengths and bounds its string fields, so whether a message fits is
    // settled when the frame is declared. Nothing here decides it from the data.
    char line[PROTOCORE_LOG_LINE_LEN];
    (void)frame.build(line, sizeof(line), spec, ctx->ns->frame.v, ctx->ns->frame.nv);

#if PROTOCORE_ENABLE_LOGBUF
    Logbuf.line.level = level;
    Logbuf.line.msg = line;
    Logbuf.put(Logbuf.internal);
#endif
    if (ctx->store->sink)
    {
        ctx->store->sink(level, line);
    }
}

// Designated, so a member's position in the struct does not decide what it binds to.
LogNs Log = {.emit = log_emit, .set_sink = log_set_sink, .internal = &s_log};

#else // every level compiled out: the handle stays so a caller still compiles

/** @brief Nothing to sink and nothing to build, so the handle carries no storage. */
struct LogInternal
{
    LogNs *ns;
};

static struct LogInternal s_log = {.ns = &Log};

static void log_emit(struct LogInternal *restrict ctx)
{
    (void)ctx;
}

static void log_set_sink(struct LogInternal *restrict ctx)
{
    (void)ctx;
}

LogNs Log = {.emit = log_emit, .set_sink = log_set_sink, .internal = &s_log};

#endif // PROTOCORE_LOG_LEVEL < PROTOCORE_LOG_LEVEL_NONE
