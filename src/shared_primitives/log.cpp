// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file log.cpp
 * @brief The emitted half of the PC_LOG* macros (see log.h).
 *
 * Nothing here is compiled when PC_LOG_LEVEL is PC_NONE - not even the sink pointer - so a build that
 * logs nothing links no logging code and spends no BSS on it.
 */

#include "shared_primitives/log.h"

#if PC_LOG_LEVEL < PC_LOG_LEVEL_NONE

#include <stdarg.h>
#include <stdio.h>

#if PC_ENABLE_LOGBUF
#include "services/system/logbuf/logbuf.h"
#endif

/** @brief Owned state: just the sink the formatted line is handed to. */
struct LogCtx
{
    pc_log_sink_fn sink;
};
static LogCtx s_log = {nullptr};

void pc_log_set_sink(pc_log_sink_fn cb)
{
    s_log.sink = cb;
}

void pc_log_printf(uint8_t level, const char *fmt, ...)
{
    if (!fmt)
    {
        return;
    }

    // One line's worth of stack, matching what the ring can store - a message longer than a stored
    // line would be truncated there anyway, so it is truncated once, here.
    char line[PC_LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

#if PC_ENABLE_LOGBUF
    pc_log(level, line);
#endif
    if (s_log.sink)
    {
        s_log.sink(level, line);
    }
}

#endif // PC_LOG_LEVEL < PC_LOG_LEVEL_NONE
