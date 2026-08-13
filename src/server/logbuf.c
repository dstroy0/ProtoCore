// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file logbuf.c
 * @brief Fixed-RAM rotating log ring + severity trap - implementation (pure).
 */

#include "server/logbuf.h"
#include "mmgr/protoframe.h" // the one frame engine
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_LOGBUF

#include <stdio.h>

// A log line is its severity letter, a space, then the message.
static const protocore_field LOG_LINE[] = {PROTOCORE_CH, {PROTOCORE_FK_LIT, 0, 1, " "}, PROTOCORE_STR, PROTOCORE_END};

// All log-ring state, owned by one instance (internal linkage): the line/severity ring, its
// head/count cursors, and the severity trap, grouped so it is one named owner, unreachable
// from any other translation unit.
typedef struct
{
    char lines[PROTOCORE_LOG_LINES][PROTOCORE_LOG_LINE_LEN]; // ring storage (BSS)
    uint8_t level[PROTOCORE_LOG_LINES];                      // per-line severity
    uint16_t head;                                           // index of the oldest line
    uint16_t count;                                          // lines currently held
    uint8_t trap_threshold;     // set with `trap` by protocore_log_set_trap(); 0xFF disables
    protocore_log_trap_fn trap; // NULL until set; the null check gates trap_threshold
} LogbufCtx;
static LogbufCtx s_log;

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

void protocore_logbuf_reset(void)
{
    s_log.head = 0;
    s_log.count = 0;
}

void protocore_log(uint8_t level, const char *msg)
{
    uint16_t slot;
    if (s_log.count < PROTOCORE_LOG_LINES)
    {
        slot = (uint16_t)((s_log.head + s_log.count) % PROTOCORE_LOG_LINES);
        s_log.count++;
    }
    else // full: overwrite the oldest and advance head
    {
        slot = s_log.head;
        s_log.head = (uint16_t)((s_log.head + 1) % PROTOCORE_LOG_LINES);
    }
    // A NULL msg renders empty and an over-long line empties the slot, both from the frame contract.
    frame.build(s_log.lines[slot], PROTOCORE_LOG_LINE_LEN, LOG_LINE,
                (const protocore_fval[]){PROTOCORE_VCH(level_letter(level)), PROTOCORE_VSTR(msg)}, 2);
    s_log.level[slot] = level;

    if (s_log.trap && level >= s_log.trap_threshold)
    {
        s_log.trap(level, s_log.lines[slot]);
    }
}

uint16_t protocore_log_count(void)
{
    return s_log.count;
}

const char *protocore_log_at(uint16_t i)
{
    if (i >= s_log.count)
    {
        return NULL;
    }
    return s_log.lines[(s_log.head + i) % PROTOCORE_LOG_LINES];
}

int protocore_log_dump(char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    out[0] = '\0';
    size_t pos = 0;
    for (uint16_t i = 0; i < s_log.count; i++)
    {
        const char *line = s_log.lines[(s_log.head + i) % PROTOCORE_LOG_LINES];
        size_t n = strnlen(line, cap);
        size_t need = n + (i + 1 < s_log.count ? 1 : 0); // +1 for the '\n' separator
        if (pos + need >= cap)                           // keep room for the null terminator
        {
            out[0] = '\0';
            return 0;
        }
        mem.cpy(out + pos, line, n);
        pos += n;
        if (i + 1 < s_log.count)
        {
            out[pos++] = '\n';
        }
    }
    out[pos] = '\0';
    return (int)pos;
}

void protocore_log_set_trap(uint8_t threshold, protocore_log_trap_fn cb)
{
    s_log.trap_threshold = threshold;
    s_log.trap = cb;
}

#endif // PROTOCORE_ENABLE_LOGBUF
