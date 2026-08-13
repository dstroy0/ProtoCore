// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * PROTOCORE_ENABLE_LOGBUF is on, and to a sink callback registered with protocore_log_set_sink() (Serial,
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

#if PROTOCORE_LOG_LEVEL < PROTOCORE_LOG_LEVEL_NONE

/**
 * @brief Build @p spec into a line and route it to the logbuf ring and/or the registered sink.
 *
 * A spec, not a format string: the shape of the message is decided when the code is written, so
 * nothing here parses anything at runtime, and a build whose logs declare no float field links no
 * float formatter.
 */
void protocore_log_frame(uint8_t level, const struct protocore_field *spec, const struct protocore_fval *v, size_t nv);

/** @brief Install (or clear, with NULL) the sink emitted lines are handed to. */
void protocore_log_set_sink(protocore_log_sink_fn cb);

#else

/** @brief No level is emitted, so there is nothing to sink; kept so callers still compile. */
static inline void protocore_log_set_sink(protocore_log_sink_fn cb)
{
    (void)cb;
}

#endif // PROTOCORE_LOG_LEVEL < PROTOCORE_LOG_LEVEL_NONE

#if PROTOCORE_LOG_LEVEL <= PROTOCORE_LOG_LEVEL_DEBUG
#define PROTOCORE_LOGD(spec, v, nv) protocore_log_frame(PROTOCORE_LOG_LEVEL_DEBUG, (spec), (v), (nv))
#else
#define PROTOCORE_LOGD(spec, v, nv) PROTOCORE_LOG_DISCARD((spec), (v), (nv))
#endif

#if PROTOCORE_LOG_LEVEL <= PROTOCORE_LOG_LEVEL_INFO
#define PROTOCORE_LOGI(spec, v, nv) protocore_log_frame(PROTOCORE_LOG_LEVEL_INFO, (spec), (v), (nv))
#else
#define PROTOCORE_LOGI(spec, v, nv) PROTOCORE_LOG_DISCARD((spec), (v), (nv))
#endif

#if PROTOCORE_LOG_LEVEL <= PROTOCORE_LOG_LEVEL_WARN
#define PROTOCORE_LOGW(spec, v, nv) protocore_log_frame(PROTOCORE_LOG_LEVEL_WARN, (spec), (v), (nv))
#else
#define PROTOCORE_LOGW(spec, v, nv) PROTOCORE_LOG_DISCARD((spec), (v), (nv))
#endif

#if PROTOCORE_LOG_LEVEL <= PROTOCORE_LOG_LEVEL_ERROR
#define PROTOCORE_LOGE(spec, v, nv) protocore_log_frame(PROTOCORE_LOG_LEVEL_ERROR, (spec), (v), (nv))
#else
#define PROTOCORE_LOGE(spec, v, nv) PROTOCORE_LOG_DISCARD((spec), (v), (nv))
#endif

PROTOCORE_END_DECLS

#endif // PROTOCORE_LOG_H
