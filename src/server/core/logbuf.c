// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file logbuf.c
 * @brief Fixed-RAM rotating log ring + severity trap - implementation (pure).
 */

#include "server/core/logbuf.h"
#include "mmgr/protoframe.h" // the one frame engine
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_LOGBUF

#include <stdio.h>

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
    uint8_t trap_threshold;     // set with `trap` by the set_trap call; 0xFF disables
    protocore_log_trap_fn trap; // NULL until set; the null check gates trap_threshold
};

/**
 * @brief The ring and the calls that reach it - what LogbufNs points at.
 *
 * @var LogbufInternal::store  the lines, their severities, and the cursors over them
 * @var LogbufInternal::ns     the handle a caller sets a call's members on
 */
struct LogbufInternal
{
    struct LogbufStorage *store;
    LogbufNs *ns;
};

static struct LogbufStorage s_store;

static struct LogbufInternal s_log = {.store = &s_store, .ns = &Logbuf};

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

static void logbuf_reset(struct LogbufInternal *restrict ctx)
{
    ctx->store->head = 0;
    ctx->store->count = 0;
}

static void logbuf_put(struct LogbufInternal *restrict ctx)
{
    const uint8_t level = ctx->ns->line.level;
    uint16_t slot;

    if (ctx->store->count < PROTOCORE_LOG_LINES)
    {
        slot = (uint16_t)((ctx->store->head + ctx->store->count) % PROTOCORE_LOG_LINES);
        ctx->store->count++;
    }
    else // full: overwrite the oldest and advance head
    {
        slot = ctx->store->head;
        ctx->store->head = (uint16_t)((ctx->store->head + 1) % PROTOCORE_LOG_LINES);
    }
    // A NULL msg renders empty and an over-long line empties the slot, both from the frame contract.
    frame.build(ctx->store->lines[slot], PROTOCORE_LOG_LINE_LEN, LOG_LINE,
                (const protocore_fval[]){PROTOCORE_VCH(level_letter(level)), PROTOCORE_VSTR(ctx->ns->line.msg)}, 2);
    ctx->store->level[slot] = level;

    if (ctx->store->trap && level >= ctx->store->trap_threshold)
    {
        ctx->store->trap(level, ctx->store->lines[slot]);
    }
}

static void logbuf_held(struct LogbufInternal *restrict ctx)
{
    ctx->ns->count = ctx->store->count;
}

static void logbuf_at(struct LogbufInternal *restrict ctx)
{
    const uint16_t i = ctx->ns->read.i;

    ctx->ns->text = NULL;
    if (i >= ctx->store->count)
    {
        return;
    }
    ctx->ns->text = ctx->store->lines[(ctx->store->head + i) % PROTOCORE_LOG_LINES];
}

static void logbuf_dump(struct LogbufInternal *restrict ctx)
{
    char *out = ctx->ns->read.out;
    const size_t cap = ctx->ns->read.cap;

    ctx->ns->n = 0;
    if (!out || cap == 0)
    {
        return;
    }
    out[0] = '\0';
    size_t pos = 0;
    for (uint16_t i = 0; i < ctx->store->count; i++)
    {
        const char *line = ctx->store->lines[(ctx->store->head + i) % PROTOCORE_LOG_LINES];
        size_t n = strnlen(line, cap);
        size_t need = n + (i + 1 < ctx->store->count ? 1 : 0); // +1 for the '\n' separator
        if (pos + need >= cap)                                 // keep room for the null terminator
        {
            out[0] = '\0';
            return;
        }
        mem.cpy(out + pos, line, n);
        pos += n;
        if (i + 1 < ctx->store->count)
        {
            out[pos++] = '\n';
        }
    }
    out[pos] = '\0';
    ctx->ns->n = (int)pos;
}

static void logbuf_set_trap(struct LogbufInternal *restrict ctx)
{
    ctx->store->trap_threshold = ctx->ns->trap.threshold;
    ctx->store->trap = ctx->ns->trap.cb;
}

// Designated, so a member's position in the struct does not decide what it binds to.
LogbufNs Logbuf = {.reset = logbuf_reset,
                   .put = logbuf_put,
                   .held = logbuf_held,
                   .at = logbuf_at,
                   .dump = logbuf_dump,
                   .set_trap = logbuf_set_trap,
                   .internal = &s_log};

#endif // PROTOCORE_ENABLE_LOGBUF
