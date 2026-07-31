// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file failsafe.cpp
 * @brief Software watchdog / deadlock detection + safe-state (see failsafe.h).
 */

#include "services/system/failsafe/failsafe.h"

#if PC_ENABLE_FAILSAFE

#include "services/system/clock.h" // pc_millis() - the pluggable monotonic clock

namespace
{
// All failsafe state, owned by one instance (internal linkage via the anon namespace):
// grouped for auditability, unreachable from any other translation unit.
struct FailsafeCtx
{
    pc_lifeline lines[PC_FAILSAFE_MAX_LIFELINES];
    pc_failsafe_cb cb = nullptr;
    void *cb_arg = nullptr;
};
FailsafeCtx s_fs;

// Minimal unsigned -> decimal, no stdlib; returns chars written.
size_t u32_dec(uint32_t v, char *out)
{
    char tmp[10];
    size_t n = 0;
    do
    {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (size_t i = 0; i < n; i++)
    {
        out[i] = tmp[n - 1 - i];
    }
    return n;
}
} // namespace

void pc_failsafe_reset(void)
{
    for (int i = 0; i < PC_FAILSAFE_MAX_LIFELINES; i++)
    {
        s_fs.lines[i] = pc_lifeline{};
    }
    s_fs.cb = nullptr;
    s_fs.cb_arg = nullptr;
}

int pc_failsafe_register_at(const char *name, uint32_t deadline_ms, uint32_t now)
{
    for (int i = 0; i < PC_FAILSAFE_MAX_LIFELINES; i++)
    {
        if (!s_fs.lines[i].armed)
        {
            s_fs.lines[i].name = name;
            s_fs.lines[i].deadline_ms = deadline_ms;
            s_fs.lines[i].last_feed_ms = now; // starts fed, so it is not instantly overdue
            s_fs.lines[i].armed = true;
            s_fs.lines[i].breached = false;
            return i;
        }
    }
    return -1;
}

int pc_failsafe_register(const char *name, uint32_t deadline_ms)
{
    return pc_failsafe_register_at(name, deadline_ms, pc_millis());
}

bool pc_failsafe_feed_at(int id, uint32_t now)
{
    if (id < 0 || id >= PC_FAILSAFE_MAX_LIFELINES || !s_fs.lines[id].armed)
    {
        return false;
    }
    s_fs.lines[id].last_feed_ms = now;
    s_fs.lines[id].breached = false; // a fresh check-in clears the breach so it can fire again next time
    return true;
}

bool pc_failsafe_feed(int id)
{
    return pc_failsafe_feed_at(id, pc_millis());
}

void pc_failsafe_on_breach(pc_failsafe_cb cb, void *arg)
{
    s_fs.cb = cb;
    s_fs.cb_arg = arg;
}

uint32_t pc_failsafe_check_at(uint32_t now)
{
    uint32_t mask = 0;
    for (int i = 0; i < PC_FAILSAFE_MAX_LIFELINES; i++)
    {
        pc_lifeline &l = s_fs.lines[i];
        if (!l.armed)
        {
            continue;
        }
        if (!pc_lifeline_overdue(now, l.last_feed_ms, l.deadline_ms))
        {
            continue;
        }
        mask |= (1u << i);
        if (l.breached) // fire once per stuck episode
        {
            continue;
        }
        l.breached = true;
        if (s_fs.cb)
        {
            s_fs.cb(i, l.name, s_fs.cb_arg);
        }
    }
    return mask;
}

uint32_t pc_failsafe_check(void)
{
    return pc_failsafe_check_at(pc_millis());
}

// append a literal into out[*n], bounded by cap (leaving room for the NUL); truncates safely on overflow.
static void fs_put(char *out, size_t cap, size_t *n, const char *s)
{
    while (*s && *n + 1 < cap)
    {
        out[(*n)++] = *s++;
    }
}
// append @p v as decimal into out[*n], same bound.
static void fs_put_u32(char *out, size_t cap, size_t *n, uint32_t v)
{
    char b[10];
    size_t k = u32_dec(v, b);
    for (size_t i = 0; i < k && *n + 1 < cap; i++)
    {
        out[(*n)++] = b[i];
    }
}

int pc_failsafe_json_at(uint32_t now, char *out, size_t cap)
{
    // {"lifelines":[{"name":"...","overdue":false,"age_ms":N,"deadline_ms":N},...]}
    if (!out || cap == 0)
    {
        return 0;
    }
    size_t n = 0;
    fs_put(out, cap, &n, "{\"lifelines\":[");
    bool first = true;
    for (int i = 0; i < PC_FAILSAFE_MAX_LIFELINES; i++)
    {
        const pc_lifeline &l = s_fs.lines[i];
        if (!l.armed)
        {
            continue;
        }
        if (!first)
        {
            fs_put(out, cap, &n, ",");
        }
        first = false;
        fs_put(out, cap, &n, "{\"name\":\"");
        fs_put(out, cap, &n, l.name ? l.name : "");
        fs_put(out, cap, &n, "\",\"overdue\":");
        fs_put(out, cap, &n, pc_lifeline_overdue(now, l.last_feed_ms, l.deadline_ms) ? "true" : "false");
        fs_put(out, cap, &n, ",\"age_ms\":");
        fs_put_u32(out, cap, &n, now - l.last_feed_ms);
        fs_put(out, cap, &n, ",\"deadline_ms\":");
        fs_put_u32(out, cap, &n, l.deadline_ms);
        fs_put(out, cap, &n, "}");
    }
    fs_put(out, cap, &n, "]}");
    // The n >= cap arm is unreachable: fs_put/fs_put_u32 only ever advance n while n + 1 < cap, so n
    // can never reach cap by the time we get here (cap > 0 was already established above).
    out[n < cap ? n : cap - 1] = '\0'; // GCOVR_EXCL_BR_LINE
    return (int)n;
}

#endif // PC_ENABLE_FAILSAFE
