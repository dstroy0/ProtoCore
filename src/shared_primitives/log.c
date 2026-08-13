// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file log.c
 * @brief The emitted half of the PROTOCORE_LOG* macros (see log.h).
 *
 * Nothing here is compiled when PROTOCORE_LOG_LEVEL is PROTOCORE_NONE - not even the sink pointer - so a build that
 * logs nothing links no logging code and spends no BSS on it.
 */

#include "shared_primitives/log.h"

#include "mmgr/protoframe.h" // frame.build: the line is a spec, not a format string

#if PROTOCORE_LOG_LEVEL < PROTOCORE_LOG_LEVEL_NONE

#include <stdarg.h>

#if PROTOCORE_ENABLE_LOGBUF
#include "server/logbuf.h"
#endif

/** @brief Owned state: just the sink the formatted line is handed to. */
typedef struct
{
    protocore_log_sink_fn sink;
} LogCtx;
static LogCtx s_log = {NULL};

void protocore_log_set_sink(protocore_log_sink_fn cb)
{
    s_log.sink = cb;
}

void protocore_log_frame(uint8_t level, const struct protocore_field *spec, const struct protocore_fval *v, size_t nv)
{
    if (!spec)
    {
        return;
    }

    // One line's worth of stack, matching what the ring can store. A spec whose worst case exceeds
    // it is a spec that was written wrong, not a runtime condition to absorb: every log frame
    // states its literals' lengths and bounds its string fields, so whether a message fits is
    // settled when the frame is declared. Nothing here decides it from the data.
    char line[PROTOCORE_LOG_LINE_LEN];
    (void)frame.build(line, sizeof(line), spec, v, nv);

#if PROTOCORE_ENABLE_LOGBUF
    protocore_log(level, line);
#endif
    if (s_log.sink)
    {
        s_log.sink(level, line);
    }
}

#endif // PROTOCORE_LOG_LEVEL < PROTOCORE_LOG_LEVEL_NONE
