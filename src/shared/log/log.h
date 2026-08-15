// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file log.h
 * @brief Abstract logging whose disabled levels cost nothing at all (PROTOCORE_LOG_LEVEL).
 *
 * Instrumentation is only worth leaving in the source permanently if a build that does not want it
 * pays nothing for it - not a branch, not a call, and not a format string sitting in flash. A
 * runtime `if (level >= threshold)` fails that last part: every message is still linked in, and on
 * a device flash is the scarce resource.
 *
 * So the filter is the preprocessor. A call below PROTOCORE_LOG_LEVEL expands to a form that names its
 * arguments only inside `sizeof(...)` - an unevaluated context - which emits no code and no string
 * literal, yet still runs the compiler's printf format checking over them and marks the arguments
 * used (so a variable read only by a log does not warn). Enable the level and the same line starts
 * logging, with no source change.
 *
 * Where an emitted line goes is the caller's choice: it is handed to server/logbuf's ring when
 * PROTOCORE_ENABLE_LOGBUF is on, and to a sink callback registered with ::LogNs::set_sink (a serial port,
 * syslog, a websocket console) if there is one.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LOG_H
#define PROTOCORE_LOG_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

// Only ever pointed at from here, so the tags are enough and the engine's header stays out of
// every translation unit that includes this one.
struct protocore_field;
struct protocore_fval;

/** @brief Receives an emitted line, already formatted. @p level is a PROTOCORE_LOG_LEVEL_* value. */
typedef void (*protocore_log_sink_fn)(uint8_t level, const char *line);

/**
 * @brief Declared, never defined: only ever named inside `sizeof`, so no call is ever generated.
 *
 * It exists to mark a discarded statement's arguments as used, so a variable read only by a log
 * does not warn its way into being deleted.
 */
int protocore_log_discard_args(const struct protocore_field *spec, const struct protocore_fval *v, size_t nv);

/** @brief The discarded form: marks arguments used, emits nothing. */
#define PROTOCORE_LOG_DISCARD(spec, v, nv)                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(protocore_log_discard_args((spec), (v), (nv)));                                                   \
    } while (0)

/** @brief One line to emit: its level, and the spec that shapes it. */
typedef struct
{
    uint8_t level;                      ///< the severity the line carries
    const struct protocore_field *spec; ///< the shape of the message
    const struct protocore_fval *v;     ///< the values that fill it
    size_t nv;                          ///< how many
} LogFrameArgs;

/** @brief The sink and the calls that reach it, described only in log.c. */
struct LogInternal;

/**
 * @brief The frame logger.
 *
 * A caller sets the members a call takes and invokes it through ::Log. The installed sink is behind
 * @ref internal; the level macros below do the member-set and the call in one statement.
 *
 * @var LogNs::frame     one line to emit: its level, and the spec that shapes it
 * @var LogNs::sink      the sink an install registers; NULL clears it
 * @var LogNs::emit      build the line and route it to the ring and/or the sink
 * @var LogNs::set_sink  install (or clear) the sink emitted lines are handed to
 * @var LogNs::internal  the installed sink and the calls that reach it
 *
 * A spec, not a format string: the shape of the message is decided when the code is written, so
 * nothing here parses anything at runtime, and a build whose logs declare no float field links no
 * float formatter. A build with every level compiled out still carries the handle, so a caller
 * compiles either way and emit is the no-op.
 */
typedef struct
{
    LogFrameArgs frame;
    protocore_log_sink_fn sink;

    void (*emit)(struct LogInternal *ctx);
    void (*set_sink)(struct LogInternal *ctx);

    struct LogInternal *internal;
} LogNs;

/** @brief The one symbol this module exports. */
extern LogNs Log;

/** @brief Set the frame members and emit, as one statement. */
#define PROTOCORE_LOG_EMIT(lvl, spec_, v_, nv_)                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        Log.frame.level = (lvl);                                                                                       \
        Log.frame.spec = (spec_);                                                                                      \
        Log.frame.v = (v_);                                                                                            \
        Log.frame.nv = (nv_);                                                                                          \
        Log.emit(Log.internal);                                                                                        \
    } while (0)

#if PROTOCORE_LOG_LEVEL <= PROTOCORE_LOG_LEVEL_DEBUG
#define PROTOCORE_LOGD(spec, v, nv) PROTOCORE_LOG_EMIT(PROTOCORE_LOG_LEVEL_DEBUG, (spec), (v), (nv))
#else
#define PROTOCORE_LOGD(spec, v, nv) PROTOCORE_LOG_DISCARD((spec), (v), (nv))
#endif

#if PROTOCORE_LOG_LEVEL <= PROTOCORE_LOG_LEVEL_INFO
#define PROTOCORE_LOGI(spec, v, nv) PROTOCORE_LOG_EMIT(PROTOCORE_LOG_LEVEL_INFO, (spec), (v), (nv))
#else
#define PROTOCORE_LOGI(spec, v, nv) PROTOCORE_LOG_DISCARD((spec), (v), (nv))
#endif

#if PROTOCORE_LOG_LEVEL <= PROTOCORE_LOG_LEVEL_WARN
#define PROTOCORE_LOGW(spec, v, nv) PROTOCORE_LOG_EMIT(PROTOCORE_LOG_LEVEL_WARN, (spec), (v), (nv))
#else
#define PROTOCORE_LOGW(spec, v, nv) PROTOCORE_LOG_DISCARD((spec), (v), (nv))
#endif

#if PROTOCORE_LOG_LEVEL <= PROTOCORE_LOG_LEVEL_ERROR
#define PROTOCORE_LOGE(spec, v, nv) PROTOCORE_LOG_EMIT(PROTOCORE_LOG_LEVEL_ERROR, (spec), (v), (nv))
#else
#define PROTOCORE_LOGE(spec, v, nv) PROTOCORE_LOG_DISCARD((spec), (v), (nv))
#endif

PROTOCORE_END_DECLS

#endif // PROTOCORE_LOG_H
