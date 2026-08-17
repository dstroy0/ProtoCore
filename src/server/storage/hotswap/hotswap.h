// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hotswap.h
 * @brief Safeties for removable storage that can vanish mid-write (PROTOCORE_ENABLE_HOTSWAP).
 *
 * An SD card is a connector, and a connector can be pulled - during a log append, an upload, a
 * core-dump save. The failure is nasty because it is quiet: the driver keeps handing back a mounted
 * volume, every write reports an error nobody checks, and the code carries on believing it has
 * storage. What should happen instead is that the medium is declared unusable, stale handles are
 * dropped, and callers are told to stop rather than write into nothing.
 *
 * That is what this owns. One state machine per volume:
 *
 *     ABSENT  --probe finds a card, mount succeeds-->  READY
 *     READY   --fail_threshold consecutive I/O errors-->  FAULTED  (unmount fires immediately)
 *     FAULTED --probe interval elapses, remount succeeds-->  READY
 *
 * The threshold matters: a single failed write is not proof a card left (a transient bus error, a
 * full volume), so one error does not tear down a working volume. A run of them is proof enough.
 * Any success resets the run, so intermittent noise never accumulates into a false removal.
 *
 * Callers gate on `protocore_hotswap_ready()` and report every filesystem outcome through
 * `protocore_hotswap_io()`. It is deliberately **fail-closed**: while not READY, ready() is false, so a
 * caller that honors it writes nothing rather than writing into a stale mount.
 *
 * The core is pure and takes an explicit `now`, so the whole state machine is host-testable with a
 * synthetic clock; the device binding is three app callbacks (mount / unmount / optional
 * card-detect), because how a volume is mounted is the application's business, not this owner's.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HOTSWAP_H
#define PROTOCORE_HOTSWAP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HOTSWAP

PROTOCORE_BEGIN_DECLS

// PROTOCORE_HOTSWAP_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief Where a removable volume currently stands. */
typedef enum PROTO_ENUM_PACKED
{
    STORAGE_STATE_ABSENT = 0,  ///< nothing mounted; no filesystem call is safe.
    STORAGE_STATE_READY = 1,   ///< mounted and healthy.
    STORAGE_STATE_FAULTED = 2, ///< was mounted, I/O is failing; unmounted and awaiting a remount probe.
} StorageState;

/** @brief The whole state machine. Pure: it decides, the binding acts. */
typedef struct
{
    StorageState state;         ///< current state.
    uint8_t fail_run;           ///< consecutive I/O failures seen while READY.
    uint8_t fail_threshold;     ///< failures in a row that declare the medium gone (>= 1).
    uint32_t probe_interval_ms; ///< minimum gap between remount attempts while not READY.
    uint32_t last_probe_ms;     ///< when the last probe ran.
    uint32_t mounts;            ///< successful mounts since init (a removal/insert cycle count).
    uint32_t faults;            ///< times a healthy volume was declared faulted.
} HotswapCore;

/** @brief Mount the volume. @return true on success. */
typedef proto_bool (*protocore_hotswap_mount)(void *ctx);

/** @brief Drop the mount and any handles it owns. Must tolerate being called when not mounted. */
typedef void (*protocore_hotswap_unmount)(void *ctx);

/** @brief Optional card-detect probe. nullptr means "assume present and let the mount decide". */
typedef proto_bool (*protocore_hotswap_present)(void *ctx);

/** @brief Fired on every state change, so an app can log it or light an LED. */
typedef void (*protocore_hotswap_event)(StorageState from, StorageState to, void *ctx);

/** @brief What core_init takes: c, fail_threshold, probe_interval_ms, ... */
typedef struct
{
    HotswapCore *c;
    uint8_t
        fail_threshold; ///< consecutive I/O errors that declare the medium gone; clamped to >= 1. Starting ABSENT ...
    uint32_t probe_interval_ms;
    uint32_t now;
} HotswapCoreInitArgs;

/** @brief What core_io takes: c, ok. */
typedef struct
{
    HotswapCore *c;
    proto_bool ok;
} HotswapCoreIoArgs;

/** @brief What core_due takes: c, now. */
typedef struct
{
    const HotswapCore *c;
    uint32_t now;
} HotswapCoreDueArgs;

/** @brief What core_probe takes: c, present, mounted, now. */
typedef struct
{
    HotswapCore *c;
    proto_bool present; ///< true if a medium appears to be there (card-detect, or "assume yes" without one)
    proto_bool mounted; ///< true if the mount actually succeeded. Present-but-unmountable stays ABSENT rather than ...
    uint32_t now;
} HotswapCoreProbeArgs;

/** @brief What begin takes: mount, unmount, present, ctx. */
typedef struct
{
    protocore_hotswap_mount mount;
    protocore_hotswap_unmount unmount;
    protocore_hotswap_present present;
    void *ctx;
} HotswapBeginArgs;

/** @brief What set_event_cb takes: cb. */
typedef struct
{
    protocore_hotswap_event cb;
} HotswapSetEventCbArgs;

/** @brief What poll_at takes: now. */
typedef struct
{
    uint32_t now;
} HotswapPollAtArgs;

/** @brief What io takes: ok. */
typedef struct
{
    proto_bool ok;
} HotswapIoArgs;

/** @brief What state_name takes: s. */
typedef struct
{
    StorageState s;
} HotswapStateNameArgs;

/** @brief What json takes: out, cap. */
typedef struct
{
    char *out;
    size_t cap;
} HotswapJsonArgs;

/**
 * @brief Safeties for removable storage that can vanish mid-write (PROTOCORE_ENABLE_HOTSWAP).
 *
 * A caller sets the members a call takes, invokes it through ::Hotswap with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Hotswap.core_init_args.c = ...;
 *   Hotswap.core_init_args.fail_threshold = ...;
 *   Hotswap.core_init_args.probe_interval_ms = ...;
 *   Hotswap.core_init_args.now = ...;
 *   Hotswap.core_init(work);
 *
 * @var HotswapNs::core_init_args  what core_init takes: c, fail_threshold, probe_interval_ms,
 * @var HotswapNs::core_io_args  what core_io takes: c, ok
 * @var HotswapNs::core_due_args  what core_due takes: c, now
 * @var HotswapNs::core_probe_args  what core_probe takes: c, present, mounted, now
 * @var HotswapNs::begin_args  what begin takes: mount, unmount, present, ctx
 * @var HotswapNs::set_event_cb_args  what set_event_cb takes: cb
 * @var HotswapNs::poll_at_args  what poll_at takes: now
 * @var HotswapNs::io_args  what io takes: ok
 * @var HotswapNs::state_name_args  what state_name takes: s
 * @var HotswapNs::json_args  what json takes: out, cap
 * @var HotswapNs::ok  true if the state changed (so the binding knows to unmount + notify)
 * @var HotswapNs::value  the value a call reports
 * @var HotswapNs::text  the string a call reports
 * @var HotswapNs::n  length written (excl NUL), or 0 on overflow
 * @var HotswapNs::core_init  initialize to ABSENT at now
 * @var HotswapNs::core_io  report one filesystem outcome. A success while READY clears the ...
 * @var HotswapNs::core_due  is a (re)mount probe due at now? Only while not READY, and only ...
 * @var HotswapNs::core_probe  report what a probe found
 * @var HotswapNs::begin  install the callbacks and reset to ABSENT. A first poll will ...
 * @var HotswapNs::set_event_cb  install (or clear, with nullptr) the state-change callback
 * @var HotswapNs::poll  run the state machine: probe when due, unmount on a fresh fault. ...
 * @var HotswapNs::poll_at  poll_at
 * @var HotswapNs::ready  is it safe to touch the filesystem right now? The gate every caller ...
 * @var HotswapNs::io  report a filesystem outcome; unmounts and notifies if this is the ...
 * @var HotswapNs::state  current state
 * @var HotswapNs::state_name  short name for s ("absent" / "ready" / "faulted"), for logs and JSON
 * @var HotswapNs::json  serialize as `{"storage":"ready","mounts":N,"faults":N}` for a ...
 *
 * @c work is PROTOCORE_HOTSWAP_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    HotswapCoreInitArgs core_init_args;
    HotswapCoreIoArgs core_io_args;
    HotswapCoreDueArgs core_due_args;
    HotswapCoreProbeArgs core_probe_args;
    HotswapBeginArgs begin_args;
    HotswapSetEventCbArgs set_event_cb_args;
    HotswapPollAtArgs poll_at_args;
    HotswapIoArgs io_args;
    HotswapStateNameArgs state_name_args;
    HotswapJsonArgs json_args;

    proto_bool ok;
    StorageState value;
    const char *text;
    size_t n;

    void (*const core_init)(uint8_t *restrict work);
    void (*const core_io)(uint8_t *restrict work);
    void (*const core_due)(uint8_t *restrict work);
    void (*const core_probe)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const set_event_cb)(uint8_t *restrict work);
    void (*const poll)(uint8_t *restrict work);
    void (*const poll_at)(uint8_t *restrict work);
    void (*const ready)(uint8_t *restrict work);
    void (*const io)(uint8_t *restrict work);
    void (*const state)(uint8_t *restrict work);
    void (*const state_name)(uint8_t *restrict work);
    void (*const json)(uint8_t *restrict work);
} HotswapNs;

/** @brief The one symbol this module exports. */
extern HotswapNs Hotswap;

/**
 * @brief The PROTOCORE_HOTSWAP_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_hotswap_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HOTSWAP

#endif // PROTOCORE_HOTSWAP_H
