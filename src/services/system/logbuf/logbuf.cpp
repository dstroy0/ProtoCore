// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file logbuf.cpp
 * @brief Fixed-RAM rotating log ring + severity trap - implementation (pure).
 */

#include "services/system/logbuf/logbuf.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder

#if PC_ENABLE_LOGBUF

#include <stdio.h>
#include <string.h>

namespace
{
// All log-ring state, owned by one instance (internal linkage): the line/severity ring, its
// head/count cursors, and the severity trap, grouped so it is one named owner, unreachable
// from any other translation unit.
struct LogbufCtx
{
    char lines[PC_LOG_LINES][PC_LOG_LINE_LEN]; // ring storage (BSS)
    uint8_t level[PC_LOG_LINES];               // per-line severity
    uint16_t head = 0;                         // index of the oldest line
    uint16_t count = 0;                        // lines currently held
    uint8_t trap_threshold = 0xFF;             // 0xFF = trap disabled
    pc_log_trap_fn trap = nullptr;
};
LogbufCtx s_log;

char level_letter(uint8_t level)
{
    switch (level)
    {
    case pc_log_level::PC_LOG_ERROR:
        return 'E';
    case pc_log_level::PC_LOG_WARN:
        return 'W';
    case pc_log_level::PC_LOG_INFO:
        return 'I';
    default:
        return 'D';
    }
}
} // namespace

void pc_logbuf_reset(void)
{
    s_log.head = 0;
    s_log.count = 0;
}

void pc_log(uint8_t level, const char *msg)
{
    uint16_t slot;
    if (s_log.count < PC_LOG_LINES)
    {
        slot = (uint16_t)((s_log.head + s_log.count) % PC_LOG_LINES);
        s_log.count++;
    }
    else // full: overwrite the oldest and advance head
    {
        slot = s_log.head;
        s_log.head = (uint16_t)((s_log.head + 1) % PC_LOG_LINES);
    }
    pc_sb sb67 = {s_log.lines[slot], PC_LOG_LINE_LEN, 0, true};
    pc_sb_ch(&sb67, (char)(level_letter(level)));
    pc_sb_put(&sb67, " ");
    pc_sb_put(&sb67, msg ? msg : "");
    if (pc_sb_finish(&sb67) == 0)
    {
        sb67.p[0] = '\0';
    }
    s_log.level[slot] = level;

    if (s_log.trap && level >= s_log.trap_threshold)
    {
        s_log.trap(level, s_log.lines[slot]);
    }
}

uint16_t pc_log_count(void)
{
    return s_log.count;
}

const char *pc_log_at(uint16_t i)
{
    if (i >= s_log.count)
    {
        return nullptr;
    }
    return s_log.lines[(s_log.head + i) % PC_LOG_LINES];
}

int pc_log_dump(char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    out[0] = '\0';
    size_t pos = 0;
    for (uint16_t i = 0; i < s_log.count; i++)
    {
        const char *line = s_log.lines[(s_log.head + i) % PC_LOG_LINES];
        size_t n = strnlen(line, cap);
        size_t need = n + (i + 1 < s_log.count ? 1 : 0); // +1 for the '\n' separator
        if (pos + need >= cap)                           // keep room for the null terminator
        {
            out[0] = '\0';
            return 0;
        }
        memcpy(out + pos, line, n);
        pos += n;
        if (i + 1 < s_log.count)
        {
            out[pos++] = '\n';
        }
    }
    out[pos] = '\0';
    return (int)pos;
}

void pc_log_set_trap(uint8_t threshold, pc_log_trap_fn cb)
{
    s_log.trap_threshold = threshold;
    s_log.trap = cb;
}

#endif // PC_ENABLE_LOGBUF
