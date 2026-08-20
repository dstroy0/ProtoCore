// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file logbuf.c
 * @brief Fixed-RAM rotating log ring + severity trap - implementation (pure).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_LOGBUF

#include "mmgr/plaintext/plaintext.h"   // the persistent end this module's state is taken from
#include "mmgr/protoframe/protoframe.h" // the one frame engine
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "server/core/logbuf/logbuf.h"

PROTOCORE_BEGIN_DECLS

// A log line is its severity letter, a space, then the message.
static const protocore_field LOG_LINE[] = {PROTOCORE_CH, {PROTOCORE_FK_LIT, 0, 1, " "}, PROTOCORE_STR, PROTOCORE_END};

/**
 * @brief The ring's compile-time storage: the lines, their severities, and the cursors over them.
 */
struct LogbufStorage
{
    char lines[PROTOCORE_LOG_LINES][PROTOCORE_LOG_LINE_LEN]; // ring storage (BSS)
    uint8_t level[PROTOCORE_LOG_LINES];                      // per-line severity
    uint16_t head;                                           // index of the oldest line
    uint16_t count;                                          // lines currently held
    uint8_t trap_threshold;                                  // set with `trap` by the set_trap call; 0xFF disables
    protocore_log_trap_fn trap;                              // NULL until set; the null check gates trap_threshold
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define LOGBUF_OFF_CTX 0u
static_assert(LOGBUF_OFF_CTX + sizeof(struct LogbufStorage) <= PROTOCORE_LOGBUF_BORROW,
              "PROTOCORE_LOGBUF_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define LOGBUF_CTX(w) ((struct LogbufStorage *)(void *)((w) + LOGBUF_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_LOGBUF_BORROW persistent bytes
} LogbufOwnCtx;
static LogbufOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_logbuf_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_LOGBUF_BORROW).buf;
    }
    return s_own.span;
}

static char level_letter(uint8_t level)
{
    switch (level)
    {
    case PROTOCORE_LOG_ERROR:
        return 'E';
    case PROTOCORE_LOG_WARN:
        return 'W';
    case PROTOCORE_LOG_INFO:
        return 'I';
    default:
        return 'D';
    }
}

void protocore_logbuf_reset(uint8_t *restrict work)
{
    LOGBUF_CTX(work)->head = 0;
    LOGBUF_CTX(work)->count = 0;
}

void protocore_logbuf_put(uint8_t *restrict work)
{
    const uint8_t level = LogbufV.line.level;
    uint16_t slot;

    if (LOGBUF_CTX(work)->count < PROTOCORE_LOG_LINES)
    {
        slot = (uint16_t)((LOGBUF_CTX(work)->head + LOGBUF_CTX(work)->count) % PROTOCORE_LOG_LINES);
        LOGBUF_CTX(work)->count++;
    }
    else // full: overwrite the oldest and advance head
    {
        slot = LOGBUF_CTX(work)->head;
        LOGBUF_CTX(work)->head = (uint16_t)((LOGBUF_CTX(work)->head + 1) % PROTOCORE_LOG_LINES);
    }
    // A NULL msg renders empty and an over-long line empties the slot, both from the frame contract.
    frame.build(LOGBUF_CTX(work)->lines[slot], PROTOCORE_LOG_LINE_LEN, LOG_LINE,
                (const protocore_fval[]){PROTOCORE_VCH(level_letter(level)), PROTOCORE_VSTR(LogbufV.line.msg)}, 2);
    LOGBUF_CTX(work)->level[slot] = level;

    if (LOGBUF_CTX(work)->trap && level >= LOGBUF_CTX(work)->trap_threshold)
    {
        LOGBUF_CTX(work)->trap(level, LOGBUF_CTX(work)->lines[slot]);
    }
}

void protocore_logbuf_held(uint8_t *restrict work)
{
    LogbufV.count = LOGBUF_CTX(work)->count;
}

void protocore_logbuf_at(uint8_t *restrict work)
{
    const uint16_t i = LogbufV.read.i;

    LogbufV.text = NULL;
    if (i >= LOGBUF_CTX(work)->count)
    {
        return;
    }
    LogbufV.text = LOGBUF_CTX(work)->lines[(LOGBUF_CTX(work)->head + i) % PROTOCORE_LOG_LINES];
}

void protocore_logbuf_dump(uint8_t *restrict work)
{
    char *out = LogbufV.read.out;
    const size_t cap = LogbufV.read.cap;

    LogbufV.n = 0;
    if (!out || cap == 0)
    {
        return;
    }
    out[0] = '\0';
    size_t pos = 0;
    for (uint16_t i = 0; i < LOGBUF_CTX(work)->count; i++)
    {
        const char *line = LOGBUF_CTX(work)->lines[(LOGBUF_CTX(work)->head + i) % PROTOCORE_LOG_LINES];
        size_t n = str.len(line, cap);
        size_t need = n + (i + 1 < LOGBUF_CTX(work)->count ? 1 : 0); // +1 for the '\n' separator
        if (pos + need >= cap)                                       // keep room for the null terminator
        {
            out[0] = '\0';
            return;
        }
        mem.cpy(out + pos, line, n);
        pos += n;
        if (i + 1 < LOGBUF_CTX(work)->count)
        {
            out[pos++] = '\n';
        }
    }
    out[pos] = '\0';
    LogbufV.n = (int)pos;
}

void protocore_logbuf_set_trap(uint8_t *restrict work)
{
    LOGBUF_CTX(work)->trap_threshold = LogbufV.trap.threshold;
    LOGBUF_CTX(work)->trap = LogbufV.trap.cb;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
LogbufVars LogbufV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LOGBUF
