// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file time_source.c
 * @brief Multi-source time fallback matrix - implementation.
 *
 * A fixed BSS table of sources queried in ascending priority. See time_source.h.
 */

#include "time_source.h"
#include "shared_primitives/http_date.h" // pc_http_date() - the shared IMF-fixdate formatter

#if PC_ENABLE_TIME_SOURCE

typedef struct
{
    const char *name;
    TimeSourceFn fn;
    uint8_t priority;
    proto_bool used;
} Src;

// All time-source state, owned by one instance (internal linkage): the priority-ordered
// source table and the last-selected source name, grouped so it is one named owner.
typedef struct
{
    Src sources[PC_TIME_SOURCE_MAX];
    const char *active;
} TimeSourceCtx;
static TimeSourceCtx s_ts;

proto_bool pc_time_source_add(const char *name, uint8_t priority, TimeSourceFn fn)
{
    if (!fn)
    {
        return PROTO_FALSE;
    }
    for (int i = 0; i < PC_TIME_SOURCE_MAX; i++)
    {
        if (!s_ts.sources[i].used)
        {
            s_ts.sources[i].name = name;
            s_ts.sources[i].fn = fn;
            s_ts.sources[i].priority = priority;
            s_ts.sources[i].used = PROTO_TRUE;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE; // table full
}

uint32_t pc_time_now(void)
{
    s_ts.active = NULL;

    // Query sources in ascending priority (lowest value first); stop at the first
    // that returns a nonzero epoch. A selection scan avoids sorting and, more
    // importantly, does not invoke lower-priority callbacks once a higher-priority
    // source has answered (reading an RTC / GPS can be costly).
    uint32_t queried = 0; // bitmask of sources already tried
    for (int pass = 0; pass < PC_TIME_SOURCE_MAX; pass++)
    {
        int sel = -1;
        for (int i = 0; i < PC_TIME_SOURCE_MAX; i++)
        {
            if (!s_ts.sources[i].used || (queried & (1u << i)))
            {
                continue;
            }
            if (sel < 0 || s_ts.sources[i].priority < s_ts.sources[sel].priority)
            {
                sel = i;
            }
        }
        if (sel < 0)
        {
            break; // no more sources
        }

        queried |= (1u << sel);
        uint32_t epoch = s_ts.sources[sel].fn();
        if (epoch != 0)
        {
            s_ts.active = s_ts.sources[sel].name;
            return epoch;
        }
    }
    return 0;
}

const char *pc_time_source_active(void)
{
    return s_ts.active;
}

void pc_time_source_reset(void)
{
    const Src blank = {0};
    for (int i = 0; i < PC_TIME_SOURCE_MAX; i++)
    {
        s_ts.sources[i] = blank;
    }
    s_ts.active = NULL;
}

#else // PC_ENABLE_TIME_SOURCE == 0 -> no-op stubs

proto_bool pc_time_source_add(const char *name, uint8_t priority, TimeSourceFn fn)
{
    (void)name;
    (void)priority;
    (void)fn;
    return PROTO_FALSE;
}
uint32_t pc_time_now(void)
{
    return 0;
}
const char *pc_time_source_active(void)
{
    return NULL;
}
void pc_time_source_reset(void)
{
}

#endif // PC_ENABLE_TIME_SOURCE

// The current best time (pc_time_now, any registered NTP / GPS / RTC / ... source) as an RFC 7231
// IMF-fixdate. Defined unconditionally: with the registry disabled pc_time_now() is 0, so this
// returns 0 (no Date). Lets the HTTP Date header draw from whatever time source is enabled.
size_t pc_time_http_date(char *out, size_t out_cap)
{
    return pc_http_date((time_t)pc_time_now(), out, out_cap);
}
