// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file log.c
 * @brief The emitted half of the PC_LOG* macros (see log.h).
 *
 * Nothing here is compiled when PC_LOG_LEVEL is PC_NONE - not even the sink pointer - so a build that
 * logs nothing links no logging code and spends no BSS on it.
 */

#include "shared_primitives/log.h"

#if PC_LOG_LEVEL < PC_LOG_LEVEL_NONE

#include <stdarg.h>

#if PC_ENABLE_LOGBUF
#include "services/system/logbuf/logbuf.h"
#endif

/** @brief Owned state: just the sink the formatted line is handed to. */
typedef struct
{
    pc_log_sink_fn sink;
} LogCtx;
static LogCtx s_log = {NULL};

void pc_log_set_sink(pc_log_sink_fn cb)
{
    s_log.sink = cb;
}

void pc_log_frame(uint8_t level, const pc_field *spec, ...)
{
    if (!spec)
    {
        return;
    }

    // One line's worth of stack, matching what the ring can store. A spec whose worst case exceeds
    // it is a spec that was written wrong, not a runtime condition to absorb: every log frame
    // states its literals' lengths and bounds its string fields, so whether a message fits is
    // settled when the frame is declared. Nothing here decides it from the data.
    char line[PC_LOG_LINE_LEN];
    va_list ap;
    va_start(ap, spec);
    (void)pc_frame_vbuild(line, sizeof(line), spec, ap);
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
