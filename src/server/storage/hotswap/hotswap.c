// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hotswap.c
 * @brief Removable-storage state machine + its binding (see hotswap.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HOTSWAP

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "mmgr/secure/secure.h"   // the persistent end this module's state is taken from
#include "server/storage/hotswap/hotswap.h"

#include "server/clock/clock.h" // protocore_millis

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Pure core
// ---------------------------------------------------------------------------

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_HOTSWAP_BORROW persistent bytes
} HotswapOwnCtx;
static HotswapOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_hotswap_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_HOTSWAP_BORROW).buf;
    }
    return s_own.span;
}

static void hotswap_core_init(uint8_t *restrict work);
static void hotswap_core_io(uint8_t *restrict work);
static void hotswap_core_probe(uint8_t *restrict work);
static void hotswap_poll_at(uint8_t *restrict work);
static void hotswap_state_name(uint8_t *restrict work);

static void hotswap_core_init(uint8_t *restrict work)
{
    (void)work;
    HotswapCore *c = Hotswap.core_init_args.c;
    uint8_t fail_threshold = Hotswap.core_init_args.fail_threshold;
    uint32_t probe_interval_ms = Hotswap.core_init_args.probe_interval_ms;
    uint32_t now = Hotswap.core_init_args.now;

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

static void hotswap_core_io(uint8_t *restrict work)
{
    (void)work;
    HotswapCore *c = Hotswap.core_io_args.c;
    proto_bool ok = Hotswap.core_io_args.ok;

    if (!c || c->state != STORAGE_STATE_READY)
    {
        Hotswap.ok = PROTO_FALSE;
        return; // not mounted: the caller should not have been touching it
    }
    if (ok)
    {
        c->fail_run = 0; // any success proves the medium is still there
        Hotswap.ok = PROTO_FALSE;
        return;
    }
    if (c->fail_run < 0xFF)
    {
        c->fail_run++;
    }
    // Zero is "not initialized yet", which behaves as 1 - the same clamp core_init applies, stated
    // here as well so a core that arrives zeroed does not fault the volume before any failure has
    // been seen.
    const uint8_t threshold = c->fail_threshold ? c->fail_threshold : 1u;
    if (c->fail_run < threshold)
    {
        Hotswap.ok = PROTO_FALSE;
        return; // one bad write is not a removal
    }
    c->state = STORAGE_STATE_FAULTED;
    c->faults++;
    Hotswap.ok = PROTO_TRUE;
}

static void hotswap_core_due(uint8_t *restrict work)
{
    (void)work;
    const HotswapCore *c = Hotswap.core_due_args.c;
    uint32_t now = Hotswap.core_due_args.now;

    if (!c || c->state == STORAGE_STATE_READY)
    {
        Hotswap.ok = PROTO_FALSE;
        return;
    }
    // Unsigned delta, so this is correct across a millis() rollover.
    Hotswap.ok = (now - c->last_probe_ms) >= c->probe_interval_ms;
}

static void hotswap_core_probe(uint8_t *restrict work)
{
    (void)work;
    HotswapCore *c = Hotswap.core_probe_args.c;
    proto_bool present = Hotswap.core_probe_args.present;
    proto_bool mounted = Hotswap.core_probe_args.mounted;
    uint32_t now = Hotswap.core_probe_args.now;

    if (!c)
    {
        Hotswap.ok = PROTO_FALSE;
        return;
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
    Hotswap.ok = c->state != was;
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

/** @brief Owned state: the core plus the app's callbacks. */
typedef struct
{
    HotswapCore core;
    protocore_hotswap_mount mount;
    protocore_hotswap_unmount unmount;
    protocore_hotswap_present present;
    protocore_hotswap_event event;
    void *ctx;
    proto_bool begun;
} HotswapCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define HOTSWAP_OFF_CTX 0u
static_assert(HOTSWAP_OFF_CTX + sizeof(HotswapCtx) <= PROTOCORE_HOTSWAP_BORROW,
              "PROTOCORE_HOTSWAP_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define HOTSWAP_CTX(w) ((HotswapCtx *)(void *)((w) + HOTSWAP_OFF_CTX))

static void hs_notify(uint8_t *restrict work, StorageState from, StorageState to)
{
    // `from != to` has no false branch to reach: both call sites (poll_at, io) only invoke hs_notify
    // after protocore_hotswap_core_probe / protocore_hotswap_core_io reported an actual state change.
    if (HOTSWAP_CTX(work)->event && from != to)
    {
        HOTSWAP_CTX(work)->event(from, to, HOTSWAP_CTX(work)->ctx);
    }
}

static void hotswap_begin(uint8_t *restrict work)
{
    protocore_hotswap_mount mount = Hotswap.begin_args.mount;
    protocore_hotswap_unmount unmount = Hotswap.begin_args.unmount;
    protocore_hotswap_present present = Hotswap.begin_args.present;
    void *ctx = Hotswap.begin_args.ctx;

    HOTSWAP_CTX(work)->mount = mount;
    HOTSWAP_CTX(work)->unmount = unmount;
    HOTSWAP_CTX(work)->present = present;
    HOTSWAP_CTX(work)->ctx = ctx;
    HOTSWAP_CTX(work)->begun = PROTO_TRUE;
    Hotswap.core_init_args.c = &HOTSWAP_CTX(work)->core;
    Hotswap.core_init_args.fail_threshold = PROTOCORE_HOTSWAP_FAIL_THRESHOLD;
    Hotswap.core_init_args.probe_interval_ms = PROTOCORE_HOTSWAP_PROBE_MS;
    Hotswap.core_init_args.now = Clock.ms;
    hotswap_core_init(work);
}

static void hotswap_set_event_cb(uint8_t *restrict work)
{
    protocore_hotswap_event cb = Hotswap.set_event_cb_args.cb;

    HOTSWAP_CTX(work)->event = cb;
}

static void hotswap_poll_at(uint8_t *restrict work)
{
    uint32_t now = Hotswap.poll_at_args.now;

    // The two tests stay separate: staged above the flag, the due check would run on a core that
    // begin() has not initialized yet.
    if (!HOTSWAP_CTX(work)->begun)
    {
        return;
    }
    Hotswap.core_due_args.c = &HOTSWAP_CTX(work)->core;
    Hotswap.core_due_args.now = now;
    hotswap_core_due(work);
    if (!Hotswap.ok)
    {
        return;
    }

    // A volume that faulted is still mounted as far as the driver knows. Drop it before retrying,
    // so the remount starts from a clean state instead of reusing handles to a card that left.
    if (HOTSWAP_CTX(work)->core.state == STORAGE_STATE_FAULTED && HOTSWAP_CTX(work)->unmount)
    {
        HOTSWAP_CTX(work)->unmount(HOTSWAP_CTX(work)->ctx);
    }

    proto_bool present = HOTSWAP_CTX(work)->present ? HOTSWAP_CTX(work)->present(HOTSWAP_CTX(work)->ctx) : PROTO_TRUE;
    proto_bool mounted = PROTO_FALSE;
    if (present && HOTSWAP_CTX(work)->mount)
    {
        mounted = HOTSWAP_CTX(work)->mount(HOTSWAP_CTX(work)->ctx);
    }

    StorageState was = HOTSWAP_CTX(work)->core.state;
    Hotswap.core_probe_args.c = &HOTSWAP_CTX(work)->core;
    Hotswap.core_probe_args.present = present;
    Hotswap.core_probe_args.mounted = mounted;
    Hotswap.core_probe_args.now = now;
    hotswap_core_probe(work);
    if (Hotswap.ok)
    {
        hs_notify(work, was, HOTSWAP_CTX(work)->core.state);
    }
}

static void hotswap_poll(uint8_t *restrict work)
{

    Hotswap.poll_at_args.now = Clock.ms;
    hotswap_poll_at(work);
}

static void hotswap_ready(uint8_t *restrict work)
{

    Hotswap.ok = HOTSWAP_CTX(work)->core.state == STORAGE_STATE_READY;
}

static void hotswap_io(uint8_t *restrict work)
{
    proto_bool ok = Hotswap.io_args.ok;

    StorageState was = HOTSWAP_CTX(work)->core.state;
    Hotswap.core_io_args.c = &HOTSWAP_CTX(work)->core;
    Hotswap.core_io_args.ok = ok;
    hotswap_core_io(work);
    if (!Hotswap.ok)
    {
        return;
    }
    // Just faulted: drop the mount now rather than at the next poll, so nothing else can write
    // through a handle to a card that is no longer there.
    if (HOTSWAP_CTX(work)->unmount)
    {
        HOTSWAP_CTX(work)->unmount(HOTSWAP_CTX(work)->ctx);
    }
    hs_notify(work, was, HOTSWAP_CTX(work)->core.state);
}

static void hotswap_state(uint8_t *restrict work)
{

    Hotswap.value = HOTSWAP_CTX(work)->core.state;
}

static void hotswap_state_name(uint8_t *restrict work)
{
    (void)work;
    StorageState s = Hotswap.state_name_args.s;

    switch (s)
    {
    case STORAGE_STATE_READY:
        Hotswap.text = "ready";
        return;
    case STORAGE_STATE_FAULTED:
        Hotswap.text = "faulted";
        return;
    case STORAGE_STATE_ABSENT:
    default:
        Hotswap.text = "absent";
        return;
    }
}

static void hotswap_json(uint8_t *restrict work)
{
    char *out = Hotswap.json_args.out;
    size_t cap = Hotswap.json_args.cap;

    if (!out || cap == 0)
    {
        Hotswap.n = 0;
        return;
    }
    protocore_sb sb_out = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb_out, "{\"storage\":\"");
    Hotswap.state_name_args.s = HOTSWAP_CTX(work)->core.state;
    hotswap_state_name(work);
    Sb.put(&sb_out, Hotswap.text);
    Sb.put(&sb_out, "\",\"mounts\":");
    Sb.u32(&sb_out, (uint32_t)((unsigned)HOTSWAP_CTX(work)->core.mounts));
    Sb.put(&sb_out, ",\"faults\":");
    Sb.u32(&sb_out, (uint32_t)((unsigned)HOTSWAP_CTX(work)->core.faults));
    Sb.put(&sb_out, "}");
    int n = (int)Sb.finish(&sb_out);
    // `n < 0` has no true branch to reach: the format is a fixed literal with no encoding-dependent
    // conversion and cap == 0 was rejected above, so snprintf can only ever report truncation here.
    if (!sb_out.ok)
    {
        out[0] = '\0';
        Hotswap.n = 0;
        return; // fail closed rather than emit a truncated object
    }
    Hotswap.n = (size_t)n;
}

HotswapNs Hotswap = {.core_init = hotswap_core_init,
                     .core_io = hotswap_core_io,
                     .core_due = hotswap_core_due,
                     .core_probe = hotswap_core_probe,
                     .begin = hotswap_begin,
                     .set_event_cb = hotswap_set_event_cb,
                     .poll = hotswap_poll,
                     .poll_at = hotswap_poll_at,
                     .ready = hotswap_ready,
                     .io = hotswap_io,
                     .state = hotswap_state,
                     .state_name = hotswap_state_name,
                     .json = hotswap_json};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HOTSWAP
