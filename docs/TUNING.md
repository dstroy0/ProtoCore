# Performance tuning

How to size the worker model for a given workload without giving up the library's
determinism guarantees (no heap after `begin()`, fixed buffers, bounded latency).
Every knob here is compile-time; the default build is the tested-deterministic
path, so you only change these when you have a specific reason.

> Prefer a UI? The [interactive build configurator](https://dstroy0.github.io/ProtoCore/configurator.html)
> lets you tick features, tune every knob, and copy out the `build_flags` / `#define`s.
> It is generated from `src/protocore_config.h`, so it always matches the library.

## The execution model in one paragraph

The server runs in one or more dedicated FreeRTOS worker tasks, not the user's
`loop()`. Each worker owns a disjoint partition of connection slots (slot `i` ->
worker `i % PC_WORKER_COUNT`) plus its own event queue and scratch arena, so no
two workers ever touch the same state: there are no hot-path locks, which is what
keeps latency bounded (= deterministic) while cores run disjoint connections in
parallel. A worker blocks on its FreeRTOS task notification and is woken the moment
an event or a deferred callback is queued, so event latency is independent of the
idle-sweep cadence. `PC_WORKER_COUNT == 1` (the default) is byte-for-byte the
original single-pipeline behavior. The lwIP callbacks that run on the tcpip thread
(shared with WiFi on Core 0) are kept minimal: a received segment is bulk-copied
into the slot's ring with a single SPSC publish, then one event is posted.

## The memory model

Every byte the library owns is BSS: allocated at link time, zero-initialized by the
C runtime, never heap-allocated after `begin()`.

**Everything is bound in two places: at ingestion, and by mmgr.** A length is bound
where bytes enter the library - the receive path refuses a segment that will not fit
the ring rather than truncating it, and every parser downstream carries an explicit
run length instead of scanning for a terminator (which is why `strlen` is banned in
`src/`). Working memory is bound by the two pools below. Nothing between those two
points re-derives a bound, and that is what makes the footprint a number you can
compute before flashing rather than a property you measure afterwards.

Working memory is therefore not per-feature buffers but two **pools**, both the same
mechanism (`pc_arena`, `mmgr/arena.h`) instantiated twice, with one arena per slot -
one per worker, plus the ghost, which is the library's own.

| Pool                           | Holds                                                    | Reclaim                                                           |
| ------------------------------ | -------------------------------------------------------- | ----------------------------------------------------------------- |
| plaintext (`mmgr/plaintext.h`) | transient bytes that are not secret                      | `pc_plaintext_reset()` per dispatch; `pc_plaintext_release(mark)` |
| secure (`mmgr/secure.h`)       | key material: shared secrets, private scalars, schedules | same, and **the release wipes** before the position moves         |

The two differ in exactly one thing: reclaiming the secure pool zeroes the region
before it becomes available again, so a secret cannot outlive its borrow. That makes
the rule structural instead of a discipline every caller has to remember on every
return path. The regions are also disjoint, so `pc_secure_owns()` and
`pc_plaintext_owns()` are mutually exclusive by construction - a secure borrow can
never be accepted where a plaintext one is expected, with no tagging and no
per-allocation metadata.

Lifetime is not what picks the pool. Both carry long-lived and ephemeral borrows; the
only question is whether the bytes are secret.

### Borrowing

```c
size_t mark = pc_secure_mark();
uint8_t *k  = pc_secure_alloc(PC_AES128GCM_KEY_LEN, 8);
/* ... use k ... */
pc_secure_release(mark);          /* wipes, then reclaims */
```

The caller asks for RAM and gets a pointer, and **the RAM is guaranteed to be there**.
There is no fail-closed branch to write at a call site, because the size was settled
before the compiler ran. `mark` / `release` are there for lifetime and, on the secure
side, for the wipe - not for handling a failure that cannot happen.

The second argument is the alignment, and it is a **condition the core sets**, not a
favor it does. The core publishes what it provides - storage of a declared span, at a
declared alignment, for a declared lifetime - and a vendor backend meets those
conditions or it does not ship. The obligation runs that way round, and it is settled
in `board_drivers/`, the only place a vendor type is named at all: the backend
`static_assert`s that its context fits the span and satisfies the alignment, so a
vendor header that changes underneath us fails the build there, named, instead of
becoming a run-time surprise in the core.

Because they meet those conditions, **the core behaves as advertised**: the pointer is
guaranteed, the footprint is a number you computed before flashing, and no call site
has a check to write. A consumer writes `pc_secure_alloc(PC_WORK_AES128GCM, 8)` and is
done - the macro settles the size, the backend has already satisfied the requirement,
and neither is restated at the point of use.

The arena's own base is the same argument one level down. The pool aligns allocation
offsets, so the base must already satisfy the strictest alignment any caller can
request; left an ordinary struct member it would inherit only 8, so it is declared
`_Alignas(32)` where the storage lives - stated once, rather than every borrow hoping
the base was good enough.

The one thing C does not do for you: on the secure side **every** return path must
reach `pc_secure_release()`, including the early ones taken when a peer sends
something malformed. That is where the wipe happens.

### Every TU precomputes, and mmgr knows

The pool sizes are not chosen numbers. Each translation unit precomputes its span -
the worst-case bytes it borrows in a single call - and declares it as a
`PC_WORK_<MODULE>` constant in [`protocore_config.h`](../src/protocore_config.h),
the one place that can see them all, since every module header includes it.

**mmgr therefore has preknowledge of every TU's span before the build runs.** It is
not handed a request at run time and asked whether it fits; the set of spans it will
ever be asked for is an input to how it was sized. That is why the pointer is
guaranteed and there is nothing to check.

**And every address is preknown too.** A module declares a span and never an offset,
so nothing couples one module to another - but mmgr resolves that set of spans into a
layout at compile time, which makes each TU's base a constant rather than whatever a
run-time bump happened to return. Two TUs that the time domain proves are never live
together resolve to the _same_ base: that overlap is precisely why the peak-concurrent
figure is smaller than the sum, and it costs nothing, because the exclusivity was
already known.

So a borrow resolves to a known address in known storage. There is no search, no free
list, no fragmentation, and no layout decision left to make while the device is
running.

Each span is **proved where the struct lives**, by a
`static_assert(sizeof(X) <= PC_WORK_X)` in the module that owns it, so a working set
that grows past its declaration fails the build naming itself rather than exhausting a
pool at run time. The declaration and the truth cannot drift apart.

These are sizes, not offsets. Nothing couples one module to another: each is a term,
order is irrelevant, and adding a module shifts no one - the difference from the
`crypto_work` region map they replaced.

The two pools then resolve those terms differently, because their time domains differ:

- **Secure: the sum.** A strict upper bound, correct however those working sets nest
  under one another, where a nest depth is only correct while the call graph stays as
  it is. It buys certainty with a little slack.
- **Plaintext: the peak concurrent.** A worker runs one event to completion before the
  next and owns a disjoint partition of slots, so a dispatch is doing HTTP _or_
  WebSocket _or_ SSH - never two at once. The time domain is known, so the maximum is
  a stated fact rather than an estimate, and overlapping those buffers in one arena
  cuts peak RAM without weakening the guarantee.

Both are feature-gated, so a build pays only for the code it compiled.

## Knobs

| Macro                     | Default              | What it does                                                                                                                                                                                                                                                                            |
| ------------------------- | -------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `PC_WORKER_COUNT`         | 1                    | Number of worker tasks. `> 1` partitions slots across cores. Must be `<= MAX_CONNS`.                                                                                                                                                                                                    |
| `PC_WORKER_CORE`          | 1                    | Core that worker 0 pins to; worker `k` pins to `(PC_WORKER_CORE + k) % cores`.                                                                                                                                                                                                          |
| `PC_WORKER_TASK_PRIORITY` | 5                    | FreeRTOS priority of the worker task(s).                                                                                                                                                                                                                                                |
| `PC_WORKER_TASK_STACK`    | 8192                 | Per-worker task stack (bytes). A build guard requires `>= PC_WORKER_STACK_RSA_MIN` when OIDC or SSH is enabled (RSA-2048 verify needs ~7 KB).                                                                                                                                           |
| `PC_WORKER_STACK_RSA_MIN` | 8192                 | Enforced floor for `PC_WORKER_TASK_STACK` once an RSA-2048 verifier (OIDC/SSH) is compiled in. Lower it only if you marshal RSA verifies off the worker.                                                                                                                                |
| `PC_WORKER_POLL_TICKS`    | 1                    | Idle-sweep block timeout (ticks). Events wake the worker immediately regardless; this only sets how often an idle worker wakes to run the timeout sweep.                                                                                                                                |
| `EVT_QUEUE_DEPTH`         | `MAX_CONNS * 4` (32) | Per-queue event slots; tracks `MAX_CONNS` so a raised pool never trips the `>= MAX_CONNS * 4` guard. Raise it to absorb larger connection bursts.                                                                                                                                       |
| `MAX_CONNS`               | 8                    | Connection pool size. The hard ceiling on concurrent connections.                                                                                                                                                                                                                       |
| `PROTO_WORD_BITS`         | 32                   | The target's natural register width. Every narrow value is carried in it and truncated at the boundary, because arithmetic narrower than the register costs the mask that keeps the unused half correct. Must be 16, 32 or 64.                                                          |
| `PROTO_INDEX_BITS`        | 32                   | Width of `proto_idx`, which is every offset, length and capacity the library declares (never `size_t`, whose width is inherited from the pointer and so differs between a device build and the host test). Must be 16 or 32, and `<= PROTO_WORD_BITS`.                                  |
| `PROTO_SWAR_BITS`         | `PROTO_WORD_BITS`    | Width of the lane carrier the byte-parallel scans and compares work in (`shared_primitives/swar.h`). Must be 8, 16, 32 or 64 and `<= PROTO_WORD_BITS` - a wider carrier is synthesized from halves and is slower than the width it decomposes into. 8 degenerates to one lane per word. |

## Feature buffer & limit knobs

Every per-feature tuning knob (buffer sizes, table depths, message limits,
thresholds) lives in one place: [`src/protocore_config.h`](../src/protocore_config.h),
in the section **"Feature tuning knobs (grouped and gated by feature)"** at the end of
the file. You never have to open a feature header to turn one. Each is an override-able
default, so you set a new value in your `build_flags` (for example
`-D PC_OPCUA_READ_MAX=16` or `-D PC_GQL_MAX_DEPTH=8`) and the owning module picks
it up. A group is wrapped in its feature's `PC_ENABLE_*` flag, so a knob only exists
when that feature is compiled in.

What is deliberately _not_ a knob and stays next to its code: protocol- and
algorithm-fixed constants (wire opcodes, magic bytes, crypto digest/block sizes,
spec-mandated PDU/field widths, and the deflate/inflate scratch sizes a `static_assert`
pins to the table layout). Changing those breaks on-the-wire conformance, so they are
not exposed as knobs.

## Board profiles (per-variant defaults)

The sizing defaults above are not one flat set. They used to be, tuned to fit the
smallest classic-ESP32 DRAM ceiling, so a board with far more RAM or flash silently
inherited the same cramped numbers. Instead, [`src/board_drivers/board_profiles/`](../src/board_drivers/board_profiles/)
layers defaults along three independent axes, selected in [`board_profile.h`](../src/board_drivers/board_profiles/board_profile.h)
(included first thing in `protocore_config.h`):

- **chip** - one file per ESP-IDF die: `classic_defaults.h` (ESP32), `s2` / `s3` / `c2` /
  `c3` / `c5` / `c6` / `c61` / `h2` / `p4` `_defaults.h`, plus preview targets `s31` / `h4` /
  `h21` (in ESP-IDF `master` only). Auto-selected from `CONFIG_IDF_TARGET_*`; classic ESP32
  and host builds use the classic floor. Holds each die's chip-appropriate sizing and its
  per-die HW-crypto flags - `PC_HW_AES` / `_SHA` / `_RSA` / `_ECC` / `_ECDSA` / `_HMAC` /
  `_DS` - which are genuinely different across the lineup (e.g. C2/C61 have no general-purpose
  AES peripheral and no RSA/MPI; C6 has no ECDSA; H4 has no RSA/DS), so gate a HW path on the
  specific flag, never on "it's an ESP32". Values track each target's ESP-IDF `soc_caps.h`.
- **PSRAM size** - `2mbpsram.h` / `4mbpsram.h` / `8mbpsram.h` / `16mbpsram.h` / `32mbpsram.h`.
  A given chip ships with or without PSRAM, so this is its own axis. Scales the RAM-backed pools up.
- **flash size** - `2mbflash.h` / `4mbflash.h` / `8mbflash.h` / `16mbflash.h` / `32mbflash.h`.
  Likewise independent; for flash-backed sizing.

Every profile default is `#ifndef`-guarded, so precedence is _first definition wins_:

```
your -D / build_opt.h override  >  PSRAM profile  >  flash profile  >  chip profile  >  classic floor
```

Nothing here overrides a value you set yourself, and the classic ESP32 gets exactly the
historical numbers, so no existing board regresses - larger variants just stop being
capped to the smallest one. Piloted so far: the edge-cache and mesh pools
(`PC_EDGE_CACHE_SLOTS`, `PC_EDGE_BODY_MAX`, `PC_EDGE_FETCH_SLOTS`, `PC_MESH_MAX_PEERS`,
`PC_MESH_MAX_CONNS`); more sizing knobs migrate into the profiles over time.

The chip is detected automatically. PSRAM and flash size can't be read reliably from the
Arduino core, so set them for your board (they default to "none / smallest"):

```ini
; platformio.ini - an S3 with 8 MB PSRAM and 16 MB flash
build_flags = -DPC_PSRAM_MB=8 -DPC_FLASH_MB=16
```

ESP-IDF builds fill both in automatically from `CONFIG_SPIRAM_SIZE` /
`CONFIG_ESPTOOLPY_FLASHSIZE_*`. You can still pin any individual knob with a `-D` override,
which always wins over the profile.

## Measured behavior (ESP32, esp32dev, COM3)

**Event latency is decoupled from the idle-sweep cadence.** With WiFi power-save
off to isolate scheduling, `GET /health` over 15 requests:

| `PC_WORKER_POLL_TICKS` | avg     | min     | max     |
| ---------------------- | ------- | ------- | ------- |
| 1                      | 27.2 ms | 12.4 ms | 35.1 ms |
| 100                    | 28.0 ms | 12.5 ms | 42.2 ms |

Identical at a 100x longer idle sweep. The pre-notification poll would have added
up to one full sweep per request (~50 ms average at `POLL_TICKS=100`).

**Idle worker wakeups scale as `tick_rate / PC_WORKER_POLL_TICKS`.** At the
Arduino 1 kHz tick that is `1000 / POLL_TICKS` wakeups per second with no traffic
(1000/s at the default 1, 10/s at 100), each wakeup being one context switch plus
one service pass over idle slots. Raising the knob cuts that idle service-loop CPU
proportionally at no latency cost. Absolute idle CPU on a real build is dominated
by WiFi/IDF housekeeping, so the headline benefit of a high value is fewer wakeups
(CPU/power headroom), not a change to request latency.

**Parallel throughput (existing benchmark).** A CPU-bound handler (~0.2 s) under
4-way concurrency: `PC_WORKER_COUNT=1` ~5.9 req/s vs `=2` ~9.1 req/s (~1.5x).
It is not a full 2x because worker 1 shares Core 0 with WiFi/lwIP; single-request
latency is unchanged.

## Recipes

- **Default / low latency (most builds).** Leave everything at default. One worker
  on Core 1, `loop()` freed, events serviced immediately.
- **Battery / mostly idle.** Raise `PC_WORKER_POLL_TICKS` (e.g. 100 for a ~10 Hz
  idle sweep). Far fewer idle wakeups, and because events still wake the worker
  immediately there is no latency cost. Keep it well below your connection timeout
  so stale connections are still reaped promptly.
- **CPU-bound handlers / throughput.** Set `PC_WORKER_COUNT=2` to run handlers
  on both cores. Expect ~1.5x, not 2x (Core 0 also runs WiFi/lwIP). Ensure
  `MAX_CONNS >= PC_WORKER_COUNT` and that handlers touch only their own slot's
  state (the model already guarantees slot isolation).
- **Bursty connection load.** Raise `EVT_QUEUE_DEPTH` so a burst of accepts/data
  events cannot overflow a queue (an overflow is dropped, not blocked, to keep the
  tcpip thread non-blocking). Raise `MAX_CONNS` for more concurrent connections
  (BSS cost is fixed and linear).
- **Pin away from a busy core.** Set `PC_WORKER_CORE=0` only if your app keeps
  Core 1 busy; by default Core 1 is the right home (Core 0 carries WiFi/lwIP).

## Determinism notes

- Every knob above changes fixed-size BSS or scheduling, never introduces a heap
  allocation or an unbounded loop.
- `PC_WORKER_COUNT > 1` adds `PC_PLAINTEXT_ARENA_SIZE` of BSS per extra worker
  and one event queue per worker; all static.
- The internal time base stays 1000 Hz regardless of `PC_WORKER_POLL_TICKS`
  (see `services/clock.h`), so timeouts keep their tested 1 ms granularity.
