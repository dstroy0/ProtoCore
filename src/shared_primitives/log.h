// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file log.h
 * @brief Abstract logging whose disabled levels cost nothing at all (PC_LOG_LEVEL).
 *
 * Instrumentation is only worth leaving in the source permanently if a build that does not want it
 * pays nothing for it - not a branch, not a call, and not a format string sitting in flash. A
 * runtime `if (level >= threshold)` fails that last part: every message is still linked in, and on
 * a device flash is the scarce resource.
 *
 * So the filter is the preprocessor. A call below PC_LOG_LEVEL expands to a form that names its
 * arguments only inside `sizeof(...)` - an unevaluated context - which emits no code and no string
 * literal, yet still runs the compiler's printf format checking over them and marks the arguments
 * used (so a variable read only by a log does not warn). Enable the level and the same line starts
 * logging, with no source change.
 *
 * Where an emitted line goes is the caller's choice: it is handed to services/system/logbuf's ring when
 * PC_ENABLE_LOGBUF is on, and to a sink callback registered with pc_log_set_sink() (Serial,
 * syslog, a websocket console) if there is one.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LOG_H
#define PROTOCORE_LOG_H

#include "mmgr/frame.h"
#include "protocore_config.h"

/** @brief Receives an emitted line, already formatted. @p level is a PC_LOG_LEVEL_* value. */
typedef void (*pc_log_sink_fn)(uint8_t level, const char *line);

/**
 * @brief Declared, never defined: only ever named inside `sizeof`, so no call is ever generated.
 *
 * It exists to mark a discarded statement's arguments as used, so a variable read only by a log
 * does not warn its way into being deleted.
 */
int pc_log_discard_args(const pc_field *spec, ...);

/** @brief The discarded form: marks arguments used, emits nothing. */
#define PC_LOG_DISCARD(...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(pc_log_discard_args(__VA_ARGS__));                                                                \
    } while (0)

#if PC_LOG_LEVEL < PC_LOG_LEVEL_NONE

/**
 * @brief Build @p spec into a line and route it to the logbuf ring and/or the registered sink.
 *
 * A spec, not a format string: the shape of the message is decided when the code is written, so
 * nothing here parses anything at runtime, and a build whose logs declare no float field links no
 * float formatter.
 */
void pc_log_frame(uint8_t level, const pc_field *spec, ...);

/** @brief Install (or clear, with NULL) the sink emitted lines are handed to. */
void pc_log_set_sink(pc_log_sink_fn cb);

#else

/** @brief No level is emitted, so there is nothing to sink; kept so callers still compile. */
static inline void pc_log_set_sink(pc_log_sink_fn cb)
{
    (void)cb;
}

#endif // PC_LOG_LEVEL < PC_LOG_LEVEL_NONE

#if PC_LOG_LEVEL <= PC_LOG_LEVEL_DEBUG
#define PC_LOGD(...) pc_log_frame(PC_LOG_LEVEL_DEBUG, __VA_ARGS__)
#else
#define PC_LOGD(...) PC_LOG_DISCARD(__VA_ARGS__)
#endif

#if PC_LOG_LEVEL <= PC_LOG_LEVEL_INFO
#define PC_LOGI(...) pc_log_frame(PC_LOG_LEVEL_INFO, __VA_ARGS__)
#else
#define PC_LOGI(...) PC_LOG_DISCARD(__VA_ARGS__)
#endif

#if PC_LOG_LEVEL <= PC_LOG_LEVEL_WARN
#define PC_LOGW(...) pc_log_frame(PC_LOG_LEVEL_WARN, __VA_ARGS__)
#else
#define PC_LOGW(...) PC_LOG_DISCARD(__VA_ARGS__)
#endif

#if PC_LOG_LEVEL <= PC_LOG_LEVEL_ERROR
#define PC_LOGE(...) pc_log_frame(PC_LOG_LEVEL_ERROR, __VA_ARGS__)
#else
#define PC_LOGE(...) PC_LOG_DISCARD(__VA_ARGS__)
#endif

#endif // PROTOCORE_LOG_H
