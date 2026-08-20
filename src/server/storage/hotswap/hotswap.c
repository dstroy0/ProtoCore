// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hotswap.c
 * @brief Removable-storage state machine + its binding (see hotswap.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HOTSWAP

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "mmgr/secure/secure.h"     // the persistent end this module's state is taken from
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

void protocore_hotswap_core_init(uint8_t *restrict work);
void protocore_hotswap_core_io(uint8_t *restrict work);
void protocore_hotswap_core_probe(uint8_t *restrict work);
void protocore_hotswap_poll_at(uint8_t *restrict work);
void protocore_hotswap_state_name(uint8_t *restrict work);

void protocore_hotswap_core_init(uint8_t *restrict work)
{
    (void)work;
    HotswapCore *c = HotswapV.core_init_args.c;
    uint8_t fail_threshold = HotswapV.core_init_args.fail_threshold;
    uint32_t probe_interval_ms = HotswapV.core_init_args.probe_interval_ms;
    uint32_t now = HotswapV.core_init_args.now;

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

void protocore_hotswap_core_io(uint8_t *restrict work)
{
    (void)work;
    HotswapCore *c = HotswapV.core_io_args.c;
    proto_bool ok = HotswapV.core_io_args.ok;

    if (!c || c->state != STORAGE_STATE_READY)
    {
        HotswapV.ok = PROTO_FALSE;
        return; // not mounted: the caller should not have been touching it
    }
    if (ok)
    {
        c->fail_run = 0; // any success proves the medium is still there
        HotswapV.ok = PROTO_FALSE;
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
        HotswapV.ok = PROTO_FALSE;
        return; // one bad write is not a removal
    }
    c->state = STORAGE_STATE_FAULTED;
    c->faults++;
    HotswapV.ok = PROTO_TRUE;
}

void protocore_hotswap_core_due(uint8_t *restrict work)
{
    (void)work;
    const HotswapCore *c = HotswapV.core_due_args.c;
    uint32_t now = HotswapV.core_due_args.now;

    if (!c || c->state == STORAGE_STATE_READY)
    {
        HotswapV.ok = PROTO_FALSE;
        return;
    }
    // Unsigned delta, so this is correct across a millis() rollover.
    HotswapV.ok = (now - c->last_probe_ms) >= c->probe_interval_ms;
}

void protocore_hotswap_core_probe(uint8_t *restrict work)
{
    (void)work;
    HotswapCore *c = HotswapV.core_probe_args.c;
    proto_bool present = HotswapV.core_probe_args.present;
    proto_bool mounted = HotswapV.core_probe_args.mounted;
    uint32_t now = HotswapV.core_probe_args.now;

    if (!c)
    {
        HotswapV.ok = PROTO_FALSE;
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
    HotswapV.ok = c->state != was;
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

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(HOTSWAP_OFF_CTX % _Alignof(HotswapCtx) == 0,
              "HOTSWAP_OFF_CTX is not a multiple of alignof(HotswapCtx) - HOTSWAP_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

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

void protocore_hotswap_begin(uint8_t *restrict work)
{
    protocore_hotswap_mount mount = HotswapV.begin_args.mount;
    protocore_hotswap_unmount unmount = HotswapV.begin_args.unmount;
    protocore_hotswap_present present = HotswapV.begin_args.present;
    void *ctx = HotswapV.begin_args.ctx;

    HOTSWAP_CTX(work)->mount = mount;
    HOTSWAP_CTX(work)->unmount = unmount;
    HOTSWAP_CTX(work)->present = present;
    HOTSWAP_CTX(work)->ctx = ctx;
    HOTSWAP_CTX(work)->begun = PROTO_TRUE;
    HotswapV.core_init_args.c = &HOTSWAP_CTX(work)->core;
    HotswapV.core_init_args.fail_threshold = PROTOCORE_HOTSWAP_FAIL_THRESHOLD;
    HotswapV.core_init_args.probe_interval_ms = PROTOCORE_HOTSWAP_PROBE_MS;
    HotswapV.core_init_args.now = Clock.ms;
    protocore_hotswap_core_init(work);
}

void protocore_hotswap_set_event_cb(uint8_t *restrict work)
{
    protocore_hotswap_event cb = HotswapV.set_event_cb_args.cb;

    HOTSWAP_CTX(work)->event = cb;
}

void protocore_hotswap_poll_at(uint8_t *restrict work)
{
    uint32_t now = HotswapV.poll_at_args.now;

    // The two tests stay separate: staged above the flag, the due check would run on a core that
    // begin() has not initialized yet.
    if (!HOTSWAP_CTX(work)->begun)
    {
        return;
    }
    HotswapV.core_due_args.c = &HOTSWAP_CTX(work)->core;
    HotswapV.core_due_args.now = now;
    protocore_hotswap_core_due(work);
    if (!HotswapV.ok)
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
    HotswapV.core_probe_args.c = &HOTSWAP_CTX(work)->core;
    HotswapV.core_probe_args.present = present;
    HotswapV.core_probe_args.mounted = mounted;
    HotswapV.core_probe_args.now = now;
    protocore_hotswap_core_probe(work);
    if (HotswapV.ok)
    {
        hs_notify(work, was, HOTSWAP_CTX(work)->core.state);
    }
}

void protocore_hotswap_poll(uint8_t *restrict work)
{

    HotswapV.poll_at_args.now = Clock.ms;
    protocore_hotswap_poll_at(work);
}

void protocore_hotswap_ready(uint8_t *restrict work)
{

    HotswapV.ok = HOTSWAP_CTX(work)->core.state == STORAGE_STATE_READY;
}

void protocore_hotswap_io(uint8_t *restrict work)
{
    proto_bool ok = HotswapV.io_args.ok;

    StorageState was = HOTSWAP_CTX(work)->core.state;
    HotswapV.core_io_args.c = &HOTSWAP_CTX(work)->core;
    HotswapV.core_io_args.ok = ok;
    protocore_hotswap_core_io(work);
    if (!HotswapV.ok)
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

void protocore_hotswap_state(uint8_t *restrict work)
{

    HotswapV.value = HOTSWAP_CTX(work)->core.state;
}

void protocore_hotswap_state_name(uint8_t *restrict work)
{
    (void)work;
    StorageState s = HotswapV.state_name_args.s;

    switch (s)
    {
    case STORAGE_STATE_READY:
        HotswapV.text = "ready";
        return;
    case STORAGE_STATE_FAULTED:
        HotswapV.text = "faulted";
        return;
    case STORAGE_STATE_ABSENT:
    default:
        HotswapV.text = "absent";
        return;
    }
}

void protocore_hotswap_json(uint8_t *restrict work)
{
    char *out = HotswapV.json_args.out;
    size_t cap = HotswapV.json_args.cap;

    if (!out || cap == 0)
    {
        HotswapV.n = 0;
        return;
    }
    protocore_sb sb_out = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb_out, "{\"storage\":\"");
    HotswapV.state_name_args.s = HOTSWAP_CTX(work)->core.state;
    protocore_hotswap_state_name(work);
    Sb.put(&sb_out, HotswapV.text);
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
        HotswapV.n = 0;
        return; // fail closed rather than emit a truncated object
    }
    HotswapV.n = (size_t)n;
}

/** @brief The operands and the outcome. */
HotswapVars HotswapV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HOTSWAP
