// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hotswap.c
 * @brief Removable-storage state machine + its binding (see hotswap.h).
 */

#include "services/storage/hotswap/hotswap.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_HOTSWAP

#include "services/system/clock.h" // pc_millis
#include <stdio.h>

// ---------------------------------------------------------------------------
// Pure core
// ---------------------------------------------------------------------------

void pc_hotswap_core_init(HotswapCore *c, uint8_t fail_threshold, uint32_t probe_interval_ms, uint32_t now)
{
    if (!c)
    {
        return;
    }
    c->state = STORAGE_STATE_ABSENT;
    c->fail_run = 0;
    // A zero threshold would fault the volume before any failure had been seen.
    c->fail_threshold = fail_threshold ? fail_threshold : 1;
    c->probe_interval_ms = probe_interval_ms;
    // Back-date the first probe so a mount is attempted immediately rather than one interval late.
    c->last_probe_ms = now - probe_interval_ms;
    c->mounts = 0;
    c->faults = 0;
}

proto_bool pc_hotswap_core_io(HotswapCore *c, proto_bool ok)
{
    if (!c || c->state != STORAGE_STATE_READY)
    {
        return PROTO_FALSE; // not mounted: the caller should not have been touching it
    }
    if (ok)
    {
        c->fail_run = 0; // any success proves the medium is still there
        return PROTO_FALSE;
    }
    if (c->fail_run < 0xFF)
    {
        c->fail_run++;
    }
    if (c->fail_run < c->fail_threshold)
    {
        return PROTO_FALSE; // one bad write is not a removal
    }
    c->state = STORAGE_STATE_FAULTED;
    c->faults++;
    return PROTO_TRUE;
}

proto_bool pc_hotswap_core_due(const HotswapCore *c, uint32_t now)
{
    if (!c || c->state == STORAGE_STATE_READY)
    {
        return PROTO_FALSE;
    }
    // Unsigned delta, so this is correct across a millis() rollover.
    return (now - c->last_probe_ms) >= c->probe_interval_ms;
}

proto_bool pc_hotswap_core_probe(HotswapCore *c, proto_bool present, proto_bool mounted, uint32_t now)
{
    if (!c)
    {
        return PROTO_FALSE;
    }
    c->last_probe_ms = now;
    StorageState was = c->state;
    if (present && mounted)
    {
        c->state = STORAGE_STATE_READY;
        c->fail_run = 0;
        if (was != STORAGE_STATE_READY)
        {
            c->mounts++;
        }
    }
    else
    {
        // Present but unmountable is not storage, so it reads the same as absent.
        c->state = STORAGE_STATE_ABSENT;
        c->fail_run = 0;
    }
    return c->state != was;
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

/** @brief Owned state: the core plus the app's callbacks. */
typedef struct
{
    HotswapCore core;
    pc_hotswap_mount mount;
    pc_hotswap_unmount unmount;
    pc_hotswap_present present;
    pc_hotswap_event event;
    void *ctx;
    proto_bool begun;
} HotswapCtx;
static HotswapCtx s_hs = {{STORAGE_STATE_ABSENT, 0, 1, 0, 0, 0, 0}, NULL, NULL, NULL, NULL, NULL, PROTO_FALSE};

static void hs_notify(StorageState from, StorageState to)
{
    // `from != to` has no false branch to reach: both call sites (poll_at, io) only invoke hs_notify
    // after pc_hotswap_core_probe / pc_hotswap_core_io reported an actual state change.
    if (s_hs.event && from != to) // GCOVR_EXCL_LINE - see above
    {
        s_hs.event(from, to, s_hs.ctx);
    }
}

void pc_hotswap_begin(pc_hotswap_mount mount, pc_hotswap_unmount unmount, pc_hotswap_present present, void *ctx)
{
    s_hs.mount = mount;
    s_hs.unmount = unmount;
    s_hs.present = present;
    s_hs.ctx = ctx;
    s_hs.begun = PROTO_TRUE;
    pc_hotswap_core_init(&s_hs.core, PC_HOTSWAP_FAIL_THRESHOLD, PC_HOTSWAP_PROBE_MS, pc_millis());
}

void pc_hotswap_set_event_cb(pc_hotswap_event cb)
{
    s_hs.event = cb;
}

void pc_hotswap_poll_at(uint32_t now)
{
    if (!s_hs.begun || !pc_hotswap_core_due(&s_hs.core, now))
    {
        return;
    }

    // A volume that faulted is still mounted as far as the driver knows. Drop it before retrying,
    // so the remount starts from a clean state instead of reusing handles to a card that left.
    if (s_hs.core.state == STORAGE_STATE_FAULTED && s_hs.unmount)
    {
        s_hs.unmount(s_hs.ctx);
    }

    proto_bool present = s_hs.present ? s_hs.present(s_hs.ctx) : PROTO_TRUE;
    proto_bool mounted = PROTO_FALSE;
    if (present && s_hs.mount)
    {
        mounted = s_hs.mount(s_hs.ctx);
    }

    StorageState was = s_hs.core.state;
    if (pc_hotswap_core_probe(&s_hs.core, present, mounted, now))
    {
        hs_notify(was, s_hs.core.state);
    }
}

void pc_hotswap_poll(void)
{
    pc_hotswap_poll_at(pc_millis());
}

proto_bool pc_hotswap_ready(void)
{
    return s_hs.core.state == STORAGE_STATE_READY;
}

void pc_hotswap_io(proto_bool ok)
{
    StorageState was = s_hs.core.state;
    if (!pc_hotswap_core_io(&s_hs.core, ok))
    {
        return;
    }
    // Just faulted: drop the mount now rather than at the next poll, so nothing else can write
    // through a handle to a card that is no longer there.
    if (s_hs.unmount)
    {
        s_hs.unmount(s_hs.ctx);
    }
    hs_notify(was, s_hs.core.state);
}

StorageState pc_hotswap_state(void)
{
    return s_hs.core.state;
}

const char *pc_hotswap_state_name(StorageState s)
{
    switch (s)
    {
    case STORAGE_STATE_READY:
        return "ready";
    case STORAGE_STATE_FAULTED:
        return "faulted";
    case STORAGE_STATE_ABSENT:
    default:
        return "absent";
    }
}

size_t pc_hotswap_json(char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    pc_sb sb_out = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_out, "{\"storage\":\"");
    pc_sb_put(&sb_out, pc_hotswap_state_name(s_hs.core.state));
    pc_sb_put(&sb_out, "\",\"mounts\":");
    pc_sb_u32(&sb_out, (uint32_t)((unsigned)s_hs.core.mounts));
    pc_sb_put(&sb_out, ",\"faults\":");
    pc_sb_u32(&sb_out, (uint32_t)((unsigned)s_hs.core.faults));
    pc_sb_put(&sb_out, "}");
    int n = (int)pc_sb_finish(&sb_out);
    // `n < 0` has no true branch to reach: the format is a fixed literal with no encoding-dependent
    // conversion and cap == 0 was rejected above, so snprintf can only ever report truncation here.
    if (!sb_out.ok) // GCOVR_EXCL_LINE - see above
    {
        out[0] = '\0';
        return 0; // fail closed rather than emit a truncated object
    }
    return (size_t)n;
}

#endif // PC_ENABLE_HOTSWAP
