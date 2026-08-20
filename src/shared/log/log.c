// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file log.c
 * @brief The emitted half of the PROTOCORE_LOG* macros (see log.h).
 *
 * Nothing here is compiled when PROTOCORE_LOG_LEVEL is PROTOCORE_NONE - not even the sink pointer - so a build that
 * logs nothing links no logging code and spends no BSS on it.
 */

#include "shared/log/log.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

#include "mmgr/protoframe/protoframe.h" // frame.build: the line is a spec, not a format string

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_LOG_BORROW persistent bytes
} LogOwnCtx;
static LogOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_log_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_LOG_BORROW).buf;
    }
    return s_own.span;
}

#if PROTOCORE_LOG_LEVEL < PROTOCORE_LOG_LEVEL_NONE

#include <stdarg.h>

#if PROTOCORE_ENABLE_LOGBUF
#include "server/core/logbuf/logbuf.h"
#endif

/**
 * @brief The logger's compile-time storage: just the sink a formatted line is handed to.
 */
struct LogStorage
{
    protocore_log_sink_fn sink;
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define LOG_OFF_CTX 0u
static_assert(LOG_OFF_CTX + sizeof(struct LogStorage) <= PROTOCORE_LOG_BORROW,
              "PROTOCORE_LOG_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define LOG_CTX(w) ((struct LogStorage *)(void *)((w) + LOG_OFF_CTX))

void protocore_log_set_sink(uint8_t *restrict work)
{
    LOG_CTX(work)->sink = LogV.sink;
}

void protocore_log_emit(uint8_t *restrict work)
{
    const uint8_t level = LogV.frame.level;
    const struct protocore_field *spec = LogV.frame.spec;

    if (!spec)
    {
        return;
    }

    // One line's worth of stack, matching what the ring can store. A spec whose worst case exceeds
    // it is a spec that was written wrong, not a runtime condition to absorb: every log frame
    // states its literals' lengths and bounds its string fields, so whether a message fits is
    // settled when the frame is declared. Nothing here decides it from the data.
    char line[PROTOCORE_LOG_LINE_LEN];
    (void)frame.build(line, sizeof(line), spec, LogV.frame.v, LogV.frame.nv);

#if PROTOCORE_ENABLE_LOGBUF
    LogbufV.line.level = level;
    LogbufV.line.msg = line;
    Logbuf.put(protocore_logbuf_span());
#endif
    if (LOG_CTX(work)->sink)
    {
        LOG_CTX(work)->sink(level, line);
    }
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
LogVars LogV;

#else // every level compiled out: the handle stays so a caller still compiles

void protocore_log_emit(uint8_t *restrict work)
{
    (void)work;
}

void protocore_log_set_sink(uint8_t *restrict work)
{
    (void)work;
}

LogNs Log = {.emit = protocore_log_emit, .set_sink = protocore_log_set_sink};

#endif // PROTOCORE_LOG_LEVEL < PROTOCORE_LOG_LEVEL_NONE
