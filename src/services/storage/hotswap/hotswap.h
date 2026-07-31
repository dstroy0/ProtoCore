// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hotswap.h
 * @brief Safeties for removable storage that can vanish mid-write (PC_ENABLE_HOTSWAP).
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
 * Callers gate on `pc_hotswap_ready()` and report every filesystem outcome through
 * `pc_hotswap_io()`. It is deliberately **fail-closed**: while not READY, ready() is false, so a
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

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_HOTSWAP

/** @brief Where a removable volume currently stands. */
enum class StorageState : uint8_t
{
    ABSENT = 0,  ///< nothing mounted; no filesystem call is safe.
    READY = 1,   ///< mounted and healthy.
    FAULTED = 2, ///< was mounted, I/O is failing; unmounted and awaiting a remount probe.
};

// ---------------------------------------------------------------------------
// Host-testable core
// ---------------------------------------------------------------------------

/** @brief The whole state machine. Pure: it decides, the binding acts. */
struct HotswapCore
{
    StorageState state;         ///< current state.
    uint8_t fail_run;           ///< consecutive I/O failures seen while READY.
    uint8_t fail_threshold;     ///< failures in a row that declare the medium gone (>= 1).
    uint32_t probe_interval_ms; ///< minimum gap between remount attempts while not READY.
    uint32_t last_probe_ms;     ///< when the last probe ran.
    uint32_t mounts;            ///< successful mounts since init (a removal/insert cycle count).
    uint32_t faults;            ///< times a healthy volume was declared faulted.
};

/**
 * @brief Initialize to ABSENT at @p now.
 * @param fail_threshold consecutive I/O errors that declare the medium gone; clamped to >= 1.
 *
 * Starting ABSENT rather than READY is the safe default: nothing may touch the volume until a probe
 * has actually mounted it.
 */
void pc_hotswap_core_init(HotswapCore *c, uint8_t fail_threshold, uint32_t probe_interval_ms, uint32_t now);

/**
 * @brief Report one filesystem outcome.
 *
 * A success while READY clears the failure run. A failure extends it, and on reaching the threshold
 * the volume becomes FAULTED. Outcomes reported while not READY are ignored - a caller honoring
 * ready() should not have been touching the volume, and a failure there is already accounted for.
 *
 * @return true if the state changed (so the binding knows to unmount + notify).
 */
bool pc_hotswap_core_io(HotswapCore *c, bool ok);

/**
 * @brief Is a (re)mount probe due at @p now?
 *
 * Only while not READY, and only once per probe_interval_ms - so a missing card costs one cheap
 * check every interval instead of a mount storm. Wrap-safe across a millis() rollover.
 */
bool pc_hotswap_core_due(const HotswapCore *c, uint32_t now);

/**
 * @brief Report what a probe found.
 * @param present true if a medium appears to be there (card-detect, or "assume yes" without one).
 * @param mounted true if the mount actually succeeded.
 *
 * Present-but-unmountable stays ABSENT rather than READY: a card that will not mount is not storage.
 * @return true if the state changed.
 */
bool pc_hotswap_core_probe(HotswapCore *c, bool present, bool mounted, uint32_t now);

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

/** @brief Mount the volume. @return true on success. */
typedef bool (*pc_hotswap_mount)(void *ctx);
/** @brief Drop the mount and any handles it owns. Must tolerate being called when not mounted. */
typedef void (*pc_hotswap_unmount)(void *ctx);
/** @brief Optional card-detect probe. nullptr means "assume present and let the mount decide". */
typedef bool (*pc_hotswap_present)(void *ctx);
/** @brief Fired on every state change, so an app can log it or light an LED. */
typedef void (*pc_hotswap_event)(StorageState from, StorageState to, void *ctx);

/** @brief Install the callbacks and reset to ABSENT. A first poll will attempt the mount. */
void pc_hotswap_begin(pc_hotswap_mount mount, pc_hotswap_unmount unmount, pc_hotswap_present present, void *ctx);

/** @brief Install (or clear, with nullptr) the state-change callback. */
void pc_hotswap_set_event_cb(pc_hotswap_event cb);

/** @brief Run the state machine: probe when due, unmount on a fresh fault. Cheap; call each loop. */
void pc_hotswap_poll(void);
void pc_hotswap_poll_at(uint32_t now);

/**
 * @brief Is it safe to touch the filesystem right now?
 *
 * The gate every caller checks first. False whenever the volume is ABSENT or FAULTED.
 */
bool pc_hotswap_ready(void);

/** @brief Report a filesystem outcome; unmounts and notifies if this is the failure that faults it. */
void pc_hotswap_io(bool ok);

/** @brief Current state. */
StorageState pc_hotswap_state(void);

/** @brief Short name for @p s ("absent" / "ready" / "faulted"), for logs and JSON. */
const char *pc_hotswap_state_name(StorageState s);

/**
 * @brief Serialize as `{"storage":"ready","mounts":N,"faults":N}` for a /health panel.
 * @return length written (excl NUL), or 0 on overflow.
 */
size_t pc_hotswap_json(char *out, size_t cap);

#endif // PC_ENABLE_HOTSWAP
#endif // PROTOCORE_HOTSWAP_H
