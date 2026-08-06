// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_config.h
 * @brief User-facing configuration for ProtoCore.
 *
 * **Compile-time sizing constants**
 * These govern static array dimensions and must be set before the first
 * library header is included.  Define any of them in your sketch or in a
 * build flag before including this file to override the defaults:
 * @code
 *   // platformio.ini
 *   build_flags = -DMAX_CONNS=8 -DBODY_BUF_SIZE=512
 * @endcode
 *
 * **Runtime parameters - flash or RAM, your choice**
 * `WebServerConfig` holds values that can be changed without a rebuild.
 * On ESP32, `PROGMEM` is a no-op (const data lands in DROM automatically).
 * On AVR it places data in flash and requires `pgm_read_*` accessors - this
 * library targets ESP32 only, so both forms read identically via pointer:
 * @code
 *   // Flash (PROGMEM, no RAM cost at runtime):
 *   const WebServerConfig my_cfg PROGMEM = { .conn_timeout_ms = 10000 };
 *
 *   // RAM (can be changed at runtime):
 *   WebServerConfig my_cfg = { .conn_timeout_ms = 10000 };
 *
 *   begin_http(80, &my_cfg);
 * @endcode
 * Pass `NULL` to use the built-in default (`CONN_TIMEOUT_MS`, 5000 ms idle
 * timeout).
 */

#ifndef PROTOCORE_CONFIG_H
#define PROTOCORE_CONFIG_H

// Per-variant default sizing (chip / PSRAM / flash profiles). Included before the sizing
// defaults below so a board profile can raise them for a larger target; your -D / build_opt.h
// overrides still win (every profile default is #ifndef-guarded).
#include "board_drivers/board_profiles/board_profile.h"

// ---------------------------------------------------------------------------
// Platform widths
// ---------------------------------------------------------------------------
// The three numbers every primitive type and every lane mask is derived from
// (shared_primitives/types.h, mmgr/swar.h). They are `#define`s rather than typedefs
// so they participate in preprocessor arithmetic, can be tested by `#if`, and can be overridden
// from build_opt.h or -D like every other knob. Each is checked below, so a bad value stops the
// build here, naming itself, instead of at the first expression that assumed it.

/**
 * @brief The target's natural register width, in bits.
 *
 * What a value is carried in while it is being worked on. Arithmetic narrower than the register is
 * not cheaper on any part in the target list - it costs the mask or sign-extend that keeps the
 * unused half correct - so the library states the register once and narrows only at a boundary.
 */
// The die states its register width in board_drivers/board_profiles/ (PC_HW_WORD_BITS, floored at
// 32 for every part in the target list); this only names it. It is NOT read off the toolchain: the
// host toolchain is 64-bit, so inferring would give the host build 8-byte lane math, an 8-byte move
// ladder and 64-bit index arithmetic - a shape no target executes, measured on a machine that does
// not ship. A -D override still wins, which is how a 64-bit port or a width experiment is done.
#ifndef PROTO_WORD_BITS
#define PROTO_WORD_BITS PC_HW_WORD_BITS
#endif

/**
 * @brief Bits in every offset, length and capacity the library declares (pc_idx).
 *
 * Replaces `size_t`, whose width is inherited from the target's pointer and therefore differs
 * between a device build and the host test that proves it - the same source emitting different
 * index arithmetic in the two places it has to agree. 32 addresses far more than any pool reserved
 * here; a target whose every buffer is under 64 KB may set 16.
 */
#ifndef PROTO_INDEX_BITS
#define PROTO_INDEX_BITS 32
#endif

/**
 * @brief Bits in the lane carrier the byte-parallel scans and compares work in.
 *
 * Defaults to the register width, which is what makes the lane algebra worth doing: one word test
 * answers for PROTO_SWAR_BITS/8 bytes at once. Set it lower only to model a narrower machine - a
 * carrier WIDER than the register is synthesized from halves and is measurably slower than the
 * width it decomposes into. 8 is legal and degenerates to one lane per word, which is the honest
 * setting for a part with no wider register.
 */
#ifndef PROTO_SWAR_BITS
#define PROTO_SWAR_BITS PROTO_WORD_BITS
#endif

#if PROTO_WORD_BITS != 16 && PROTO_WORD_BITS != 32 && PROTO_WORD_BITS != 64
#error "PROTO_WORD_BITS must be 16, 32 or 64"
#endif
#if PROTO_INDEX_BITS != 16 && PROTO_INDEX_BITS != 32
#error "PROTO_INDEX_BITS must be 16 or 32"
#endif
#if PROTO_SWAR_BITS != 8 && PROTO_SWAR_BITS != 16 && PROTO_SWAR_BITS != 32 && PROTO_SWAR_BITS != 64
#error "PROTO_SWAR_BITS must be 8, 16, 32 or 64"
#endif
#if PROTO_INDEX_BITS > PROTO_WORD_BITS
#error "PROTO_INDEX_BITS exceeds PROTO_WORD_BITS: an index must fit the register it is carried in"
#endif
#if PROTO_SWAR_BITS > PROTO_WORD_BITS
#error "PROTO_SWAR_BITS exceeds PROTO_WORD_BITS: a lane carrier wider than the register is synthesized \
from halves and is slower than the width it decomposes into"
#endif

/**
 * @brief Linkage for a leaf primitive whose body is cheaper than the call that reaches it.
 *
 * The framework's size-optimized level declines to inline `static inline`, and a lane primitive that
 * does not inline cannot fold: the extent and the needle a call site states as literals are passed
 * at runtime instead of folding away. This is stated here because this header is every file's first
 * include, so the linkage is settled before any body that uses it is parsed. A per-file pragma
 * cannot reach a header-only library at all, since `#pragma GCC optimize` binds only to functions
 * parsed after it and the bodies arrive with the include.
 *
 * board_drivers/board_profiles/pc_platform.h states the same definition and is reached above, so
 * this is the fallback for a translation unit that arrives without it.
 *
 * Leaves only. On a composite, forcing the inline trades one call for a copy of the whole body at
 * every site, which is a size decision the compiler is better placed to make.
 */
#ifndef PC_INLINE
#if defined(__GNUC__)
#define PC_INLINE static inline __attribute__((always_inline))
#else
#define PC_INLINE static inline
#endif
#endif

// The widths are settled above, so the types built from them come next. This header is the single
// entry point: a file includes it and has both the knobs and the primitive types, which is why
// types.h refuses to be included on its own.
#include "shared_primitives/types.h"

// ---------------------------------------------------------------------------
// Compile-time capacity constants (affect static array sizes)
// ---------------------------------------------------------------------------

/**
 * @brief Maximum simultaneous TCP connections (fixed static pool; ~3.95 KB of internal RAM per slot).
 *
 * Default 8: a keep-alive/concurrency server needs headroom above its peak concurrent client count,
 * because a connection closed by the keep-alive fairness cap (PC_KEEPALIVE_MAX_REQUESTS) briefly
 * holds its slot in CONN_CLOSING while it drains, and a reconnecting client needs a free slot mean-
 * while - if concurrency equals the pool size there is none, and the overflow connection is refused
 * (correct backpressure, but it caps clean throughput at concurrency == MAX_CONNS - 1). Set lower
 * (e.g. -DMAX_CONNS=4, ~16 KB less RAM) on a RAM-constrained target, or higher (16/32) for a
 * connection-heavy HTTP server; the event queue tracks it automatically (EVT_QUEUE_DEPTH below).
 */
#ifndef MAX_CONNS
#define MAX_CONNS 8
#endif

/**
 * @brief Disable Nagle's algorithm (set TCP_NODELAY) on every accepted connection.
 *
 * A request/response server is latency-first: the response is buffered whole (`tcp_write`) and pushed with a
 * single `tcp_output`, so Nagle only ever delays the final sub-MSS segment of a multi-segment response (or a
 * streamed chunk) - it waits for the peer's ACK of the prior segment, costing a ~40-200 ms delayed-ACK stall
 * for no bandwidth benefit here. Disabling it lets that tail go out immediately. Set to 0 only if the device
 * mainly streams bulk data and you prefer Nagle's segment coalescing over per-response latency.
 */
#ifndef PC_TCP_NODELAY
#define PC_TCP_NODELAY 1
#endif

/**
 * @brief Enable DiffServ QoS marking (RFC 2474) on outbound traffic. Default off.
 *
 * When set, the transport can stamp the 6-bit DSCP into the DS field (the high 6 bits of the IPv4 TOS /
 * IPv6 Traffic-Class byte) of every outbound TCP connection and UDP datagram, so a QoS-aware network - and
 * the Wi-Fi driver's 802.11e WMM access-category mapping - can prioritize safety / real-time packets (e.g.
 * the Expedited-Forwarding class, DSCP 46) over best-effort. `network_drivers/transport/diffserv.h` exposes a
 * server-wide default (`pc_set_default_dscp`), a UDP default (`pc_udp_set_dscp`), a per-listener override
 * (`pc_listen_set_dscp`), and a per-connection setter (`pc_conn_set_dscp`) so an individual flow can carry
 * any DSCP - useful both for real QoS and for arbitrarily tagging traffic in network testing. The DSCP is
 * applied on tcpip_thread (accept / connect / udp create), so no extra marshalling is added to the hot path.
 * Default off (zero cost: the marking code and the DSCP state are compiled out).
 */
#ifndef PC_ENABLE_DIFFSERV
#define PC_ENABLE_DIFFSERV 0
#endif

/**
 * @brief Use the SWAR base64 decoder (classify 4 characters per 32-bit word). Default on.
 *
 * base64 decode is the one base64 path that touches a secret (the Basic-auth credential, RFC 7617, and the
 * JWT / JWS segments, RFC 7515), so it must be **constant-time** - the character -> value mapping evaluated
 * with branchless arithmetic masks, no data-dependent branch or table. Two constant-time implementations are
 * available: a scalar one that classifies a character at a time, and this SWAR one that packs 4 characters
 * into a word and classifies all four lanes in parallel with guard-bit range masks (every base64 character
 * is < 0x80, so borrows never cross lanes). Both are byte-identical (`test_base64` runs against each) and
 * both are constant-time; measured on the ESP32-S3, SWAR is ~1.9x faster than the scalar path and 5.36x
 * faster than mbedTLS (882 vs 1639 vs 4728 cyc on a credential), at 0.00-cycle input-dependent variance.
 * SWAR is the default; set to 0 for the smaller, simpler scalar decoder if code size matters more than the
 * ~1.9x once-per-request decode win. Portable (any 32-bit target); encode is unaffected (always software).
 */
#ifndef PC_BASE64_SWAR
#define PC_BASE64_SWAR 1
#endif

/** @brief Ring-buffer capacity in bytes per connection slot (feature floors enforced last, in
 *  board_drivers/board_profiles/derived_sizing.h - a value below what an enabled feature needs is raised there). */
#ifndef RX_BUF_SIZE
#define RX_BUF_SIZE 1024
#endif

/**
 * @brief Compile-time default for connection idle timeout in milliseconds.
 *
 * The actual runtime value is stored in `WebServerConfig::conn_timeout_ms`,
 * loaded by `Tcp.conn->init()` and read back with
 * `Tcp.conn->timeout_ms()`.
 */
#ifndef CONN_TIMEOUT_MS
#define CONN_TIMEOUT_MS 5000
#endif

/**
 * @brief Request-header read deadline in milliseconds (slow-loris defense). Default 10 s; 0 disables.
 *
 * The idle timeout (CONN_TIMEOUT_MS) refreshes on every accepted byte, so a slow-loris that trickles one
 * header byte just under the idle window holds a connection slot forever and, with a few connections, denies
 * the whole fixed pool to legitimate clients (a connection-slot DoS - verified on HW). This is an ABSOLUTE
 * deadline from the first byte of a request to the end of its HEADERS that a trickle cannot reset: a connection
 * whose request headers are not complete within PC_REQUEST_TIMEOUT_MS is answered 408 and closed, freeing the
 * slot (the nginx client_header_timeout semantic). It is scoped to the header phase, so it never reaps a
 * legitimate slow body: a large streaming upload (PARSE_BODY) is governed by the streaming handler + idle
 * timer, not this deadline. It also does not touch WebSocket / SSE slots (long-lived by design). Lower it to
 * tighten the window on a trusted LAN; 0 turns the defense off.
 */
#ifndef PC_REQUEST_TIMEOUT_MS
#define PC_REQUEST_TIMEOUT_MS 10000
#endif

/**
 * @brief Upper bound (ms) a slot may dwell in CONN_CLOSING after a graceful close
 *        before the idle sweep force-aborts it.
 *
 * On a graceful (local) close the slot stays in CONN_CLOSING - keeping its PCB and
 * callbacks - until the peer ACKs the response (then it frees itself in the sent
 * callback). If the peer never ACKs (dead/black-holed), this bound lets the
 * timeout sweep reclaim the slot so the fixed pool cannot leak.
 */
#ifndef PC_CLOSING_TIMEOUT_MS
#define PC_CLOSING_TIMEOUT_MS 2000
#endif

// ---------------------------------------------------------------------------
// Worker model (server task concurrency)
// ---------------------------------------------------------------------------
//
// The server pipeline (drain events -> dispatch -> send) runs in one or more
// dedicated worker tasks instead of the user's loop(). Each worker owns a
// disjoint partition of conn_pool slots (slot i -> worker i % PC_WORKER_COUNT)
// and its own pool slot, so no two workers ever touch the same slot:
// shared-nothing, no hot-path locks, latency stays bounded (determinism
// preserved) while cores run disjoint connections in parallel.
//
// PC_WORKER_COUNT == 1 (default) is byte-for-byte the single-pipeline model:
// one worker owns every slot, the existing single event queue. N > 1 is opt-in.
// Each pool costs its arena size once per slot, and every worker gets a slot.

/** @brief Number of server worker tasks (slots partitioned i % N). Default 1. */
#ifndef PC_WORKER_COUNT
#define PC_WORKER_COUNT 1
#endif

// The library's own worker slot, one past the server workers. Each independent task borrows from
// its own slot, which is what makes a borrow lock-free; a library task prefers the ghost and falls
// back to the rest. How many slots that implies is each pool's own business - see
// PC_REG_POOL_SLOTS and PC_SEC_POOL_SLOTS in mmgr.
#define PC_GHOST_WORKER_SLOT (PC_WORKER_COUNT)

/**
 * @brief Stack (bytes) for each server worker task (ESP32).
 *
 * Floor note: two heavy computations run on the worker.
 *   - RSA-2048 verification (OIDC / SSH host key / JWKS via the mbedTLS bignum
 *     modexp) uses ~7 KB (measured on a DevKitV1).
 *   - SSH modern crypto (curve25519-sha256 KEX + ssh-ed25519, software field
 *     arithmetic in radix-2^16) peaks at ~10.5 KB (measured on an ESP32-S3): the
 *     deep pc_gf call chain plus the on-accelerator field inversion nests deeper
 *     than the RSA path.
 * So the default adapts: 12 KB when SSH is enabled (curve/ed25519 can be
 * negotiated), 8 KB otherwise. Do NOT lower it below the matching floor
 * (::PC_WORKER_STACK_CURVE_MIN for SSH, ::PC_WORKER_STACK_RSA_MIN for
 * OIDC) or the first handshake overflows the task stack - a build-time guard
 * (bottom of this file) enforces the floor so a lowered stack is caught at
 * compile time.
 */
// True when any feature that runs the SSH-class handshake is built. Derived, not configurable, and
// defined unconditionally: a predicate that exists only inside one #ifndef is a trap for the next
// person who tests it further down, where it would silently read 0.
#if (defined(PC_ENABLE_SSH) && PC_ENABLE_SSH) || (defined(PC_ENABLE_SSH_CLIENT) && PC_ENABLE_SSH_CLIENT) ||            \
    (defined(PC_ENABLE_HTTP3) && PC_ENABLE_HTTP3)
#define PC_SSH_ANY 1
#else
#define PC_SSH_ANY 0
#endif

#ifndef PC_WORKER_TASK_STACK
// SSH (curve25519 + ssh-ed25519, server OR the reverse-SSH client) and HTTP/3 (the QUIC TLS-1.3
// handshake reuses the same pc_ed25519 signer for CertificateVerify) all peak at ~10.5 KB on the
// worker task; the PQ/T hybrid (PC_ENABLE_PQC_KEX) runs ML-KEM-768 on top, ~7 KB more. The default
// tracks the matching floor so a hybrid build is provisioned, not starved; the guard at the bottom of
// this file is the backstop when the stack is set by hand. (A flag set only in the config block below,
// not via -D, is still undefined here and reads as 0 - the guard then catches any shortfall.)
// sntrup761x25519-sha512 (on by default with the PQC hybrid, but a standalone -D can enable it without
// ML-KEM) is the heavy case: the reverse-SSH CLIENT runs KeyGen+Decaps whose FO re-encrypt peaks ~32 KB,
// the SERVER runs Encaps only (~22 KB). ML-KEM alone stays at 16 KB, so a build that explicitly drops
// sntrup761 keeps the lighter floor. This default block only sees -D flags (the config defaults below
// have not run yet), so sntrup761's default-tracks-PQC is assumed here and the guard at the bottom is
// the backstop when it is toggled in the config block instead of via -D.
#if (PC_ENABLE_PQC_KEX || (defined(PC_ENABLE_SSH_SNTRUP761) && PC_ENABLE_SSH_SNTRUP761)) && PC_SSH_ANY
#if defined(PC_ENABLE_SSH_SNTRUP761) && !PC_ENABLE_SSH_SNTRUP761
#define PC_WORKER_TASK_STACK 16384
#elif defined(PC_ENABLE_SSH_CLIENT) && PC_ENABLE_SSH_CLIENT
#define PC_WORKER_TASK_STACK 40960
#else
#define PC_WORKER_TASK_STACK 32768
#endif
#elif PC_SSH_ANY
#define PC_WORKER_TASK_STACK 12288
#else
#define PC_WORKER_TASK_STACK 8192
#endif
#endif

/**
 * @brief Minimum worker-task stack (bytes) required once an RSA-2048 verifier is
 *        compiled in (OIDC / SSH).
 *
 * The mbedTLS bignum modexp alone consumes ~7 KB; 8 KB leaves room for the rest
 * of the request call chain. Overridable only for an advanced build that marshals
 * every RSA verify onto a dedicated larger-stack task (then the worker itself never
 * runs one) - otherwise leave it at the default.
 */
#ifndef PC_WORKER_STACK_RSA_MIN
#define PC_WORKER_STACK_RSA_MIN 8192
#endif

/**
 * @brief Minimum worker-task stack (bytes) required once SSH is compiled in.
 *
 * SSH can negotiate curve25519-sha256 + ssh-ed25519, whose software field
 * arithmetic peaks at ~10.5 KB of worker stack; 12 KB leaves ~1.8 KB of margin
 * for the rest of the handshake call chain (comparable to the RSA floor's
 * margin). Raise both this and ::PC_WORKER_TASK_STACK together if you extend
 * the handshake, or force RSA/DH only (ssh_kex_set_prefer_rsa) on a very tight
 * build - but the server still advertises the modern suite, so a modern-only
 * client would still exercise it.
 */
#ifndef PC_WORKER_STACK_CURVE_MIN
#define PC_WORKER_STACK_CURVE_MIN 12288
#endif

/** @brief FreeRTOS priority for each server worker task (ESP32). */
#ifndef PC_WORKER_TASK_PRIORITY
#define PC_WORKER_TASK_PRIORITY 5
#endif

/**
 * @brief Core that worker 0 pins to (ESP32). Worker k pins to (PC_WORKER_CORE
 * + k) % portNUM_PROCESSORS. Default 1 (APP_CPU), keeping Core 0 lean for the
 * WiFi/lwIP stack and offloading the user's loop().
 */
#ifndef PC_WORKER_CORE
#define PC_WORKER_CORE 1
#endif

/**
 * @brief Depth of each worker's deferred-callback queue.
 *
 * App code on loop() or another task submits work to a slot's owning worker via
 * Session.workers->defer() / pc_defer_slot(); the worker runs it in its own single-thread
 * context, so an async push (ws_send / pc_sse_send from a timer) is race-free. Each
 * worker has one queue of this depth (entries are a {fn, arg} pair, ~8 bytes).
 */
#ifndef PC_DEFER_QUEUE_DEPTH
#define PC_DEFER_QUEUE_DEPTH 8
#endif

/**
 * @brief Idle-sweep timeout, in FreeRTOS ticks, that a worker blocks between
 *        service iterations when no events are pending.
 *
 * The worker no longer free-runs a poll: it blocks on a task notification and a
 * producer (a new connection event or a deferred submission) wakes it the moment
 * work arrives, so event latency is independent of this value. The block still
 * times out after this many ticks so the idle timeout sweep (check_timeouts) keeps
 * reaping stale connections when nothing is in flight.
 *
 * Default 1 (1 tick at the Arduino 1 kHz FreeRTOS config) preserves the original
 * idle cadence byte-for-byte. Because events now wake the worker immediately,
 * raising it lowers idle wakeups (CPU/power on a battery device) WITHOUT the
 * latency penalty the old poll-based knob carried - e.g. 100 -> a ~10 Hz idle
 * sweep, still far below any connection timeout. The internal time base stays
 * 1000 Hz regardless (see server/clock/clock.h).
 */
#ifndef PC_WORKER_POLL_TICKS
#define PC_WORKER_POLL_TICKS 1
#endif

#if PC_WORKER_COUNT < 1
#error "ProtoCore: PC_WORKER_COUNT must be >= 1"
#endif
#if PC_WORKER_COUNT > MAX_CONNS
#error "ProtoCore: PC_WORKER_COUNT must be <= MAX_CONNS"
#endif

// ---------------------------------------------------------------------------
// Preempting work queue (PC_ENABLE_PREEMPT_QUEUE) - v5 real-time ingest
// ---------------------------------------------------------------------------
//
// Fixed-capacity queues, each feeding one core-pinned processing task: a producer
// posts a fixed-size item (from a task or an ISR) and the scheduler preempts straight
// to the task. There are named lanes - one USER lane exposed to the app, and internal
// DMA / forwarding / device-access lanes that run at a higher priority so internal
// ingest always preempts user work. Queue storage is static (zero heap), so depth +
// item size are compile-time; a task's stack is created only when its lane starts.
// The no-lane pc_pq_* API drives the USER lane. See preempt_queue.h.

/** @brief Enable the preempting work queue primitive (default off). */
#ifndef PC_ENABLE_PREEMPT_QUEUE
#define PC_ENABLE_PREEMPT_QUEUE 0
#endif

/** @brief Capacity of the preempting queue in items (static-allocated). */
#ifndef PC_PQ_DEPTH
#define PC_PQ_DEPTH 16
#endif

/** @brief Bytes per preempting-queue item (the posted item must fit). */
#ifndef PC_PQ_ITEM_SIZE
#define PC_PQ_ITEM_SIZE 32
#endif

/** @brief Stack (bytes) for each preempting-queue processing task (ESP32). */
#ifndef PC_PQ_STACK
#define PC_PQ_STACK 4096
#endif

/**
 * @brief Base FreeRTOS priority for the internal preempting lanes (DMA / forwarding /
 *        device access). They run at this and just above, so internal ingest preempts
 *        the user lane; keep it above the user lane's priority and below the lwIP tcpip
 *        (18) / WiFi tasks so networking is never starved. See preempt_queue.h.
 */
#ifndef PC_PQ_INTERNAL_PRIORITY
#define PC_PQ_INTERNAL_PRIORITY 8
#endif

#if PC_ENABLE_PREEMPT_QUEUE && (PC_PQ_DEPTH < 1 || PC_PQ_ITEM_SIZE < 1)
#error "ProtoCore: PC_PQ_DEPTH and PC_PQ_ITEM_SIZE must be >= 1"
#endif

// ---------------------------------------------------------------------------
// DMA peripheral ingest / egress (PC_ENABLE_DMA) - v5 hardware ingest
// ---------------------------------------------------------------------------
//
// Move peripheral bytes (UART / I2C / SPI) between the wire and a static buffer
// with the CPU free during the transfer; a DMA-complete event carries the bytes
// to a user callback, which typically posts a descriptor into the preempting work
// queue (PC_ENABLE_PREEMPT_QUEUE) so the heavy processing runs off the ISR. RX
// is double-buffered (ping-pong): the completed buffer is handed up while the DMA
// engine fills the other. Storage is static (zero heap) - channel count and buffer
// size are compile-time. See mmgr/dma.h.
//
// PC_DMA_SIMULATE routes the transfers through an in-memory ingress/egress
// simulator (feed bytes in, capture bytes out, optional TX->RX loopback) so the
// whole pipeline is exercised with no physical loopback wire - on the host test
// bench and, with the flag set, on the device itself. It is the shipped, tested
// engine; a real silicon backend plugs into pc_dma_hw_* when PC_DMA_SIMULATE=0.

/** @brief Enable the DMA peripheral ingest / egress primitive (default off). */
#ifndef PC_ENABLE_DMA
#define PC_ENABLE_DMA 0
#endif

/** @brief Number of DMA channels (static-allocated; each is one peripheral link). */
#ifndef PC_DMA_CHANNELS
#define PC_DMA_CHANNELS 2
#endif

/** @brief Bytes per DMA transfer buffer (RX is double-buffered at this size). */
#ifndef PC_DMA_BUF_SIZE
#define PC_DMA_BUF_SIZE 256
#endif

/**
 * @brief HttpRoute DMA transfers through the ingress/egress simulator (default on).
 *        Set to 0 to drive real silicon via the pc_dma_hw_* backend hooks.
 */

#if PC_ENABLE_DMA && (PC_DMA_CHANNELS < 1 || PC_DMA_BUF_SIZE < 1)
#error "ProtoCore: PC_DMA_CHANNELS and PC_DMA_BUF_SIZE must be >= 1"
#endif

// ---------------------------------------------------------------------------
// Trace capture: pre/post-trigger window assembler (PC_ENABLE_TRACE_CAPTURE)
// ---------------------------------------------------------------------------
//
// Sits downstream of PC_ENABLE_DMA (or any other sample source) on a high-rate
// acquisition front end: pc_tc_feed() is called with every batch of arriving samples
// and a continuously-running pre-trigger ring always holds the most recent samples;
// pc_tc_trigger() freezes that ring as the pre-trigger half of a window and the next
// arriving samples fill the post-trigger half, so the emitted window straddles the
// trigger instant like a benchtop oscilloscope's pretrigger/posttrigger split. One
// capture in flight at a time, fail-closed. Storage is static (zero heap) - the sum of
// the configured pretrigger + posttrigger sample counts must fit PC_TC_MAX_WINDOW_SAMPLES.
// See server/signaling/trace_capture.h.

/** @brief Enable the pre/post-trigger window assembler (default off). */
#ifndef PC_ENABLE_TRACE_CAPTURE
#define PC_ENABLE_TRACE_CAPTURE 0
#endif

/** @brief Max samples a window may hold (pretrigger_samples + posttrigger_samples), static-allocated. */
#ifndef PC_TC_MAX_WINDOW_SAMPLES
#define PC_TC_MAX_WINDOW_SAMPLES 4096
#endif

#if PC_ENABLE_TRACE_CAPTURE && PC_TC_MAX_WINDOW_SAMPLES < 1
#error "ProtoCore: PC_TC_MAX_WINDOW_SAMPLES must be >= 1"
#endif

// ---------------------------------------------------------------------------
// AD9238 SPI configuration-port codec (PC_ENABLE_AD9238)
// ---------------------------------------------------------------------------
//
// A pure codec for the AD9238 dual ADC's low-speed SPI CONFIGURATION port (power-down,
// output format, output test patterns, offset trim) - NOT its parallel sample-data bus,
// which is out of an MCU's reach at this part's sample rates. See services/peripherals/ad9238/ad9238.h
// for the hardware-verification caveat: the per-register bit fields are transcribed from
// the datasheet, not yet confirmed against physical silicon.

/** @brief Enable the AD9238 SPI configuration-port codec (default off). */
#ifndef PC_ENABLE_AD9238
#define PC_ENABLE_AD9238 0
#endif

// ---------------------------------------------------------------------------
// Interface forwarding plane (PC_ENABLE_FORWARD) - v5 hardware ingest
// ---------------------------------------------------------------------------
//
// A forwarding plane over the ingest pipeline: register interfaces (Wi-Fi STA / AP,
// Ethernet, a peripheral bus, a radio), each with an egress send callback, then add
// per-pair allow / deny rules with an optional rate cap. A frame arriving on one
// interface (pc_forward_ingress(), typically from a DMA-complete event posted onto the
// FORWARD lane) is forwarded to every allowed destination, so the device bridges / routes
// between its interfaces instead of only terminating traffic. Default-deny and fail-closed
// (a full destination or an exceeded rate cap drops, never blocks). Static tables (zero
// heap). See network_drivers/network/forward/forward.h.

/** @brief Enable the interface forwarding plane (default off). */
#ifndef PC_ENABLE_FORWARD
#define PC_ENABLE_FORWARD 0
#endif

/** @brief Interfaces layer 1 can carry: wifi station and softAP, ethernet, a bridged bus, a radio.
 *  The registry is L1's because an interface is a physical thing; the forwarding plane reads it. */
#ifndef PC_PHY_MAX_IFACES
#define PC_PHY_MAX_IFACES 4
#endif

/** @brief Max forwarding rules (src -> dst allow/deny + rate cap; static-allocated). */
#ifndef PC_FWD_MAX_RULES
#define PC_FWD_MAX_RULES 8
#endif

/** @brief Max ingress access-control entries (byte-pattern permit/deny; static). */
#ifndef PC_FWD_MAX_ACL
#define PC_FWD_MAX_ACL 8
#endif

/** @brief Bytes an ACL entry can match (its pattern / mask length). */
#ifndef PC_FWD_ACL_PATLEN
#define PC_FWD_ACL_PATLEN 4
#endif

/** @brief Max policy routes (byte-pattern -> egress interface; static). Policy routes take
 *         precedence over the src->dst rules, so tagged traffic leaves a chosen interface. */
#ifndef PC_FWD_MAX_ROUTES
#define PC_FWD_MAX_ROUTES 8
#endif

/** @brief Build-time toggle for the forwarding-path inspection hook (default off, for cost +
 *         privacy). When 1, pc_forward_set_inspector() installs a runtime callback that observes
 *         / filters each ingress frame before it is forwarded; when 0 the hook is compiled out
 *         entirely (no call site). Runtime toggle: register or clear (null) the inspector. */
#ifndef PC_FWD_INSPECT
#define PC_FWD_INSPECT 0
#endif

#if PC_ENABLE_FORWARD && (PC_PHY_MAX_IFACES < 1 || PC_FWD_MAX_RULES < 1 || PC_FWD_ACL_PATLEN < 1)
#error "ProtoCore: PC_PHY_MAX_IFACES / PC_FWD_MAX_RULES / PC_FWD_ACL_PATLEN must be >= 1"
#endif

// ---------------------------------------------------------------------------
// Radio / wireless gateway (PC_ENABLE_GATEWAY) - v5 southbound-to-northbound bridge
// ---------------------------------------------------------------------------
//
// The generic gateway pattern: a southbound radio (LoRa / nRF24 / Zigbee / ... reached
// over SPI / I2C / UART) is a "port"; a frame it receives (data-ready ISR -> DMA -> the
// FORWARD lane -> a per-radio codec) is handed to pc_gateway_uplink(), which envelopes it with
// its source node address / port / RSSI and publishes it northbound through the uplink
// callback (wire it to MQTT / HTTP / WebSocket / UDP). A northbound command goes the other
// way through pc_gateway_downlink() to the port's transmit callback. The radio TX + the
// northbound publish are callbacks (the seam a real radio driver / protocol binding plugs
// into), so the bridge is host- and device-testable with no radio. Static tables (zero
// heap). See services/net/gateway/gateway.h.

/** @brief Enable the radio / wireless gateway bridge (default off). */
#ifndef PC_ENABLE_GATEWAY
#define PC_ENABLE_GATEWAY 0
#endif

/** @brief Max southbound gateway ports (radios / buses; static-allocated). */
#ifndef PC_GW_MAX_PORTS
#define PC_GW_MAX_PORTS 4
#endif

/** @brief Default northbound topic prefix (overridable at runtime via pc_gateway_set_topic_prefix). */
#ifndef PC_GW_DEFAULT_PREFIX
#define PC_GW_DEFAULT_PREFIX "gw"
#endif

#if PC_ENABLE_GATEWAY && (PC_GW_MAX_PORTS < 1)
#error "ProtoCore: PC_GW_MAX_PORTS must be >= 1"
#endif

// ---------------------------------------------------------------------------
// LoRa radio (PC_ENABLE_LORA) - Semtech SX127x / RFM95-96 codec + driver
// ---------------------------------------------------------------------------
//
// A per-radio codec + driver that plugs into the gateway (PC_ENABLE_GATEWAY): the
// RadioHead-compatible 4-byte frame header (to / from / id / flags) codec, and an SX127x
// register driver over a caller-supplied register-access bus (so the SPI + chip-select
// wiring is the integration's, and the register protocol is host-testable with a mock
// bus). Bridge received frames northbound with pc_gateway_uplink(); the actual RF link needs
// the module to verify. See services/radio/lora/lora.h.

/** @brief Enable the LoRa (SX127x) radio codec + driver (default off). */
#ifndef PC_ENABLE_LORA
#define PC_ENABLE_LORA 0
#endif

/** @brief Max LoRa payload bytes (SX127x FIFO is 256; RadioHead uses 251 + 4 header). */
#ifndef PC_LORA_MAX_PAYLOAD
#define PC_LORA_MAX_PAYLOAD 251
#endif

#if PC_ENABLE_LORA && (PC_LORA_MAX_PAYLOAD < 1 || PC_LORA_MAX_PAYLOAD > 251)
#error "ProtoCore: PC_LORA_MAX_PAYLOAD must be 1..251"
#endif

// ---------------------------------------------------------------------------
// nRF24 radio (PC_ENABLE_NRF24) - Nordic nRF24L01+ 2.4 GHz driver
// ---------------------------------------------------------------------------
//
// A radio driver that plugs into the gateway (PC_ENABLE_GATEWAY). The nRF24L01+ speaks
// an SPI command protocol (not plain register r/w) and needs a separate CE pin, so the
// driver runs over a caller-supplied SPI transfer + CE bus (nrf_bus). Its hardware pipe
// addressing means the "source address" of a received frame is the pipe number - no
// in-payload header, so there is no separate codec. Bridge received payloads northbound
// with pc_gateway_uplink(port, pipe, ...); the RF link needs the module to verify.
// See services/radio/nrf24/nrf24.h.

/** @brief Enable the nRF24L01+ radio driver (default off). */
#ifndef PC_ENABLE_NRF24
#define PC_ENABLE_NRF24 0
#endif

/** @brief nRF24 fixed payload width in bytes (1..32; the chip's static payload size). */
#ifndef PC_NRF24_PAYLOAD
#define PC_NRF24_PAYLOAD 32
#endif

#if PC_ENABLE_NRF24 && (PC_NRF24_PAYLOAD < 1 || PC_NRF24_PAYLOAD > 32)
#error "ProtoCore: PC_NRF24_PAYLOAD must be 1..32"
#endif

// ---------------------------------------------------------------------------
// EnOcean ESP3 (PC_ENABLE_ENOCEAN) - energy-harvesting 868 MHz serial codec
// ---------------------------------------------------------------------------
//
// A UART telegram codec for EnOcean's ESP3 (EnOcean Serial Protocol 3), the framing used
// by USB/serial EnOcean gateways (TCM 310 / USB 300): sync 0x55, a 4-byte header (data
// length, optional length, packet type) protected by CRC8, then data + optional data
// protected by a second CRC8. pc_esp3_parse() frames one telegram out of a byte stream and
// verifies both CRCs; pc_esp3_build() assembles one. Pure (no UART code - you feed it the
// serial bytes), so it is fully host-testable. See services/radio/enocean/enocean.h.

/** @brief Enable the EnOcean ESP3 serial codec (default off). */
#ifndef PC_ENABLE_ENOCEAN
#define PC_ENABLE_ENOCEAN 0
#endif

/** @brief Reject an ESP3 telegram whose declared data length exceeds this (framing sanity). */
#ifndef PC_ENOCEAN_MAX_DATA
#define PC_ENOCEAN_MAX_DATA 512
#endif

#if PC_ENABLE_ENOCEAN && (PC_ENOCEAN_MAX_DATA < 1)
#error "ProtoCore: PC_ENOCEAN_MAX_DATA must be >= 1"
#endif

// ---------------------------------------------------------------------------
// PN532 NFC (PC_ENABLE_PN532) - NXP PN532 NFC/RFID controller frame codec
// ---------------------------------------------------------------------------
//
// The NXP PN532 (I2C / SPI / HSU) command-frame protocol - a tag read/write bridged to an
// HTTP / MQTT event. The chip is driven by "normal information frames" (00 00 FF | LEN |
// LCS | TFI | PData | DCS | 00) with a length checksum and a data checksum, plus a 6-byte
// ACK frame. pc_pn532_build_frame() / pc_pn532_parse_frame() assemble and verify those frames
// (the per-command PData is the application's), and pc_pn532_is_ack() detects the ACK. Pure -
// you carry the frame bytes over your I2C / SPI / UART - so it is fully host-testable.
// See services/peripherals/pn532/pn532.h.

/** @brief Enable the PN532 NFC frame codec (default off). */
#ifndef PC_ENABLE_PN532
#define PC_ENABLE_PN532 0
#endif

/** @brief Reject a PN532 normal frame whose declared length exceeds this (framing sanity). */
#ifndef PC_PN532_MAX_DATA
#define PC_PN532_MAX_DATA 254
#endif

#if PC_ENABLE_PN532 && (PC_PN532_MAX_DATA < 1 || PC_PN532_MAX_DATA > 254)
#error "ProtoCore: PC_PN532_MAX_DATA must be 1..254"
#endif

// ---------------------------------------------------------------------------
// Sigfox (PC_ENABLE_SIGFOX) - Wisol / Murata Sigfox modem AT-command codec
// ---------------------------------------------------------------------------
//
// Tiny low-power uplinks over the Sigfox 0G network. A Wisol / Murata Sigfox modem is
// driven by AT commands over UART: pc_sigfox_build_uplink() formats an `AT$SF=<hex>` frame
// for a <= 12-byte payload, and pc_sigfox_parse_response() classifies the modem's reply
// (OK / ERROR / still pending). Pure text-command codec - you carry it over your UART - so
// it is fully host-testable. See services/radio/sigfox/sigfox.h.

/** @brief Enable the Sigfox AT-command codec (default off). */
#ifndef PC_ENABLE_SIGFOX
#define PC_ENABLE_SIGFOX 0
#endif

/** @brief Maximum Sigfox uplink payload (the network caps a message at 12 bytes). */
#ifndef PC_SIGFOX_MAX_PAYLOAD
#define PC_SIGFOX_MAX_PAYLOAD 12
#endif

#if PC_ENABLE_SIGFOX && (PC_SIGFOX_MAX_PAYLOAD < 1 || PC_SIGFOX_MAX_PAYLOAD > 12)
#error "ProtoCore: PC_SIGFOX_MAX_PAYLOAD must be 1..12"
#endif

// ---------------------------------------------------------------------------
// Z-Wave (PC_ENABLE_ZWAVE) - Silicon Labs Z-Wave Serial API frame codec
// ---------------------------------------------------------------------------
//
// The host-side Serial API of a Silicon Labs 500 / 700-series Z-Wave controller over UART:
// a Z-Wave mesh bridged to the web. Data frames are SOF (0x01) | LEN | Type | Command |
// Data | Checksum, where the checksum is 0xFF XOR-folded over LEN..last-data; single-byte
// ACK (0x06) / NAK (0x15) / CAN (0x18) frames flow-control them. pc_zwave_build_frame() /
// pc_zwave_parse_frame() assemble and verify a data frame; the per-command payload is the
// application's. Pure - you carry the bytes over your UART - so it is fully host-testable.
// See services/radio/zwave/zwave.h.

/** @brief Enable the Z-Wave Serial API frame codec (default off). */
#ifndef PC_ENABLE_ZWAVE
#define PC_ENABLE_ZWAVE 0
#endif

/** @brief Reject a Z-Wave frame whose declared length exceeds this data cap (sanity). */
#ifndef PC_ZWAVE_MAX_DATA
#define PC_ZWAVE_MAX_DATA 64
#endif

#if PC_ENABLE_ZWAVE && (PC_ZWAVE_MAX_DATA < 1)
#error "ProtoCore: PC_ZWAVE_MAX_DATA must be >= 1"
#endif

// ---------------------------------------------------------------------------
// Zigbee (PC_ENABLE_ZIGBEE) - Silicon Labs EZSP / ASH serial framing codec
// ---------------------------------------------------------------------------
//
// The ASH (Asynchronous Serial Host) data-link layer that carries EZSP frames to a Silicon
// Labs EmberZNet NCP over UART - a Zigbee network bridged to the web. ASH delimits frames
// with a Flag byte (0x7E), byte-stuffs the reserved control bytes, and protects each frame
// with a CRC-16/CCITT. pc_ash_frame_encode() wraps a control byte + payload into a stuffed,
// CRC'd frame; pc_ash_frame_decode() unstuffs + verifies one. The EZSP command payload the
// frame carries (version, stack status, an incoming APS message, ...) is the application's.
// pc_ash_frame_decode() removes the stuffing and verifies the CRC. Pure - you carry the bytes
// over your UART - so it is fully host-testable. See services/radio/zigbee/zigbee.h.

/** @brief Enable the Zigbee EZSP / ASH framing codec (default off). */
#ifndef PC_ENABLE_ZIGBEE
#define PC_ENABLE_ZIGBEE 0
#endif

/** @brief Max ASH payload bytes (an EZSP frame; the ASH data field caps near 128). */
#ifndef PC_ZIGBEE_MAX_DATA
#define PC_ZIGBEE_MAX_DATA 128
#endif

#if PC_ENABLE_ZIGBEE && (PC_ZIGBEE_MAX_DATA < 1)
#error "ProtoCore: PC_ZIGBEE_MAX_DATA must be >= 1"
#endif

// ---------------------------------------------------------------------------
// Thread (PC_ENABLE_THREAD) - OpenThread spinel over HDLC-lite framing codec
// ---------------------------------------------------------------------------
//
// The HDLC-lite framing that carries spinel frames to an OpenThread radio co-processor
// (RCP: an nRF52840 / EFR32) over UART - an 802.15.4 / Thread mesh bridged to IP / the web.
// Each spinel frame is wrapped by HDLC-lite: an FCS (CRC-16/X-25) is appended, the reserved
// bytes are byte-stuffed, and a Flag byte (0x7E) terminates it. pc_spinel_frame_encode() /
// pc_spinel_frame_decode() do the framing + FCS; the spinel command inside (a property
// get/set/insert, a stream frame) is the application's. Pure - you carry the bytes over your
// UART - so it is fully host-testable. See services/radio/thread/thread.h.

/** @brief Enable the Thread spinel / HDLC-lite framing codec (default off). */
#ifndef PC_ENABLE_THREAD
#define PC_ENABLE_THREAD 0
#endif

/** @brief Max spinel payload bytes carried in one HDLC-lite frame. */
#ifndef PC_THREAD_MAX_DATA
#define PC_THREAD_MAX_DATA 256
#endif

#if PC_ENABLE_THREAD && (PC_THREAD_MAX_DATA < 1)
#error "ProtoCore: PC_THREAD_MAX_DATA must be >= 1"
#endif

// ---------------------------------------------------------------------------
// Wired Ethernet PHY (PC_ENABLE_ETHERNET) - run the server over an RMII PHY
// ---------------------------------------------------------------------------
//
// Bring up a wired Ethernet link (an RMII PHY: LAN8720 / TLK110 / RTL8201 / DP83848) so the
// server runs over Ethernet instead of (or alongside) Wi-Fi. Physical.eth->init() is a thin
// wrapper over the Arduino ETH library; the PHY pins / type / clock come from the standard
// ETH_PHY_* build flags for your board (see example Ethernet). The egress reporting
// (Physical.link->egress -> PC_IF_ETH) and the per-route interface classifier already handle a
// wired route, so once the link has an IP the server accepts on it with no other change.
// Default off (zero cost / the ETH library is not linked). ESP32-only.

/** @brief Enable wired Ethernet bring-up (init_eth_physical / eth_ready). Default off. */
#ifndef PC_ENABLE_ETHERNET
#define PC_ENABLE_ETHERNET 0
#endif

// W5500 SPI Ethernet (arduino-esp32 3.x only). Set PC_ETH_W5500=1 to select the SPI PHY over the RMII
// default; the pins below are the ESP32-S3-DevKitC wiring (HSPI / SPI3). The 2.x ETH library has no W5500,
// so Physical.eth->init() falls back to the RMII ETH.begin() when the core is older.
#ifndef PC_ETH_W5500
#define PC_ETH_W5500 0
#endif
#ifndef PC_ETH_W5500_CS
#define PC_ETH_W5500_CS 7 ///< chip select
#endif
#ifndef PC_ETH_W5500_RST
#define PC_ETH_W5500_RST 6 ///< reset
#endif
#ifndef PC_ETH_W5500_INT
#define PC_ETH_W5500_INT 5 ///< interrupt
#endif
#ifndef PC_ETH_W5500_SCK
#define PC_ETH_W5500_SCK 12 ///< HSPI clock (S3-DevKitC default)
#endif
#ifndef PC_ETH_W5500_MISO
#define PC_ETH_W5500_MISO 13 ///< HSPI MISO (S3-DevKitC default)
#endif
#ifndef PC_ETH_W5500_MOSI
#define PC_ETH_W5500_MOSI 11 ///< HSPI MOSI (S3-DevKitC default)
#endif
// W5500 SPI clock in MHz. The W5500 datasheet allows up to 33.3 MHz; 20 is the arduino-esp32 default and
// a safe value for breadboard jumper wiring. Higher clocks raise throughput (the link is SPI-bound, not
// PHY-bound) but need clean, short wiring - marginal signal integrity at high MHz corrupts frames.
#ifndef PC_ETH_W5500_SPI_MHZ
#define PC_ETH_W5500_SPI_MHZ 20 ///< W5500 SPI clock (MHz); raise for throughput on clean wiring
#endif

/**
 * @brief Enable IPv6 on the network interface (dual-stack). Default off.
 *
 * When set, Physical.ip6->init() turns on IPv6 for the Wi-Fi netif (SLAAC link-local plus any
 * router-advertised global address). The TCP and UDP listeners already bind IPADDR_TYPE_ANY, so
 * the server accepts IPv6 connections the moment the interface has a v6 address; the pc_ip core
 * (shared_primitives/ip.h) parses / formats / classifies both families. Requires an
 * lwIP built with LWIP_IPV6=1 (the stock Arduino-ESP32 core ships it).
 */
#ifndef PC_ENABLE_IPV6
#define PC_ENABLE_IPV6 0
#endif

/**
 * @brief Wi-Fi promiscuous (monitor) capture (PC_ENABLE_PROMISC). Default off.
 *
 * Passive 802.11 sniffing: pc_promisc_begin() puts the radio in promiscuous mode on a channel and
 * delivers every frame to a sink (services/radio/promisc). Wire that sink into the forwarding plane
 * (PC_ENABLE_FORWARD) to bridge captured Wi-Fi frames to another interface - e.g. stream them
 * to a wired collector over Ethernet. Ships a pure 802.11 header parser and libpcap framing
 * (DLT_IEEE802_11) so a forwarded frame is a valid PCAP a wired Wireshark / tcpdump can read.
 */
#ifndef PC_ENABLE_PROMISC
#define PC_ENABLE_PROMISC 0
#endif

/**
 * @brief Wired field-bus listen-only capture (PC_ENABLE_BUS_CAPTURE). Default off.
 *
 * The wired counterpart to promiscuous Wi-Fi capture: bus_capture_begin() installs the CAN (TWAI)
 * controller in listen-only mode - it decodes every frame on the bus but never ACKs or transmits,
 * so it stays invisible - and delivers each CanFrame to a sink (server/signaling/bus_capture). Wire the
 * sink into the forwarding plane (PC_ENABLE_FORWARD) to bridge captured CAN frames to another
 * interface. can_to_socketcan() formats a frame as a Linux SocketCAN frame so, with the libpcap
 * DLT_CAN_SOCKETCAN link type, the stream is a capture Wireshark reads.
 */
#ifndef PC_ENABLE_BUS_CAPTURE
#define PC_ENABLE_BUS_CAPTURE 0
#endif

// Feature / service / codec tuning knobs are consolidated at the END of this file,
// under "Feature tuning knobs (grouped and gated by feature)" - placed there so every
// PC_ENABLE_* flag is already resolved and each group can gate on its own feature.

/** @brief Maximum HTTP headers stored per request. */
#ifndef MAX_HEADERS
#define MAX_HEADERS 8
#endif

/** @brief Maximum URL path length (including leading `/`). */
#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 64
#endif

/**
 * @brief Maximum header field-name length (e.g. `"Content-Type"`).
 *
 * Must accommodate the longest header name the app needs to read by key.
 * Standard names reach 30+ chars (`Sec-WebSocket-Extensions` = 24,
 * `Access-Control-Request-Headers` = 30), so the default leaves margin; an
 * over-long key is truncated (not rejected) by the parser.
 */
#ifndef MAX_KEY_LEN
#define MAX_KEY_LEN 32
#endif

/** @brief Maximum header field-value length. */
#ifndef MAX_VAL_LEN
#define MAX_VAL_LEN 48
#endif

/** @brief Maximum raw query-string length (everything after `?`). */
#ifndef MAX_QUERY_LEN
#define MAX_QUERY_LEN 128
#endif

/** @brief Maximum number of parsed query-string parameters. */
#ifndef MAX_QUERY_PARAMS
#define MAX_QUERY_PARAMS 8
#endif

/** @brief Maximum number of `:name` path parameters captured per route match. */
#ifndef MAX_PATH_PARAMS
#define MAX_PATH_PARAMS 4
#endif

/**
 * @brief Capacity for the full `Authorization` header value (Digest auth).
 *
 * A Digest `Authorization` header (username, realm, nonce, uri, response,
 * qop, nc, cnonce) is far longer than MAX_VAL_LEN, so when PC_ENABLE_AUTH
 * is set the parser captures it whole into a dedicated per-request buffer.
 */
#ifndef DIGEST_AUTH_HDR_MAX
#define DIGEST_AUTH_HDR_MAX 384
#endif

/**
 * @brief Lifetime of a Digest `nonce`, in milliseconds (default 5 minutes).
 *
 * The server mints a stateless, keyed, timestamped nonce (RFC 7616 3.3) rather
 * than a fixed one: each challenge carries the issue time plus a MAC over the
 * server secret, so no per-nonce table is needed. A client `Authorization` whose
 * nonce is older than this window is treated as @c stale - the credentials are
 * re-checked and, if correct, the server reissues a fresh challenge with
 * `stale=true` so the client retries transparently (no re-prompt). This bounds
 * how long a captured Digest response can be replayed without any server-side
 * state, which the shared-nothing worker model could not hold safely.
 */
#ifndef PC_DIGEST_NONCE_LIFETIME_MS
#define PC_DIGEST_NONCE_LIFETIME_MS (5u * 60u * 1000u)
#endif

/** @brief Maximum query-parameter key length. */
#ifndef QUERY_KEY_LEN
#define QUERY_KEY_LEN 24
#endif

/** @brief Maximum query-parameter value length. */
#ifndef QUERY_VAL_LEN
#define QUERY_VAL_LEN 48
#endif

/**
 * @brief Maximum request body bytes stored in `HttpReq::body`.
 *
 * Bodies larger than this trigger a 413 Payload Too Large response -
 * the parser detects the overflow via `Content-Length` before any body
 * bytes arrive, so no data is read or stored for oversized requests.
 */
#ifndef BODY_BUF_SIZE
#define BODY_BUF_SIZE 256
#endif

/** @brief Maximum simultaneously registered routes. */
#ifndef MAX_ROUTES
#define MAX_ROUTES 16
#endif

/**
 * @brief Maximum globally-registered middleware functions.
 *
 * The middleware chain is a fixed array of function pointers run in
 * registration order before a request reaches its route handler (see
 * use()). Costs MAX_MIDDLEWARE pointers of BSS; an empty chain
 * adds no per-request work.
 */
#ifndef MAX_MIDDLEWARE
#define MAX_MIDDLEWARE 4
#endif

/**
 * @brief Per-chunk staging buffer for send_chunked()'s ChunkSource (max bytes a
 *        source produces per call, hence the largest single chunk on the wire).
 *
 * Allocated on the worker stack only while a chunk is being framed - no persistent
 * RAM cost. The pump asks the source for at most this many bytes (or fewer when the
 * send window is smaller), so it bounds the chunk size, not the total body.
 *
 * Sized to one TCP segment (~MSS): the pump frames + sends each chunk in a single
 * tcpip_thread round-trip (~23 us on-device), so a bigger chunk = fewer round-trips per
 * byte. 1440 keeps the framed chunk within one segment; raise it (up to the send window)
 * to cut the round-trip count further on a fast transport (e.g. Ethernet), at more stack.
 */
#ifndef CHUNK_BUF_SIZE
#define CHUNK_BUF_SIZE 1440
#endif

/**
 * @brief Maximum object/array nesting depth for the JsonWriter (see json.h).
 *
 * Bounds the writer's per-level comma-tracking stack (one bool per level);
 * begin_object()/begin_array() beyond this fail the writer instead of
 * overflowing. No heap; ~JSON_MAX_DEPTH bytes of stack inside the writer object.
 */
#ifndef JSON_MAX_DEPTH
#define JSON_MAX_DEPTH 8
#endif

/**
 * @brief Step budget for the regex route matcher (see on_regex()).
 *
 * The matcher is a bounded backtracker: it counts match steps and fails closed
 * (no match) once this budget is exhausted, so a pathological pattern can never
 * backtrack unboundedly. Keeps regex routing deterministic. Routing patterns hit
 * only a handful of steps; the default leaves wide margin.
 */
#ifndef RE_MAX_STEPS
#define RE_MAX_STEPS 2000
#endif

// ---------------------------------------------------------------------------
// WebSocket sizing constants
// ---------------------------------------------------------------------------

/**
 * @brief Maximum simultaneous WebSocket connections.
 *
 * Each connection occupies one TCP slot from MAX_CONNS and one entry in
 * ws_pool[].  MAX_WS_CONNS + MAX_SSE_CONNS must not exceed MAX_CONNS.
 */
#ifndef MAX_WS_CONNS
#define MAX_WS_CONNS 2
#endif

/**
 * @brief Maximum WebSocket frame payload in bytes.
 *
 * Frames larger than this are rejected with Close code 1009 (Message Too Big).
 * Fragmented messages are not supported; each message must fit in one frame.
 */
#ifndef WS_FRAME_SIZE
#define WS_FRAME_SIZE 512
#endif

/**
 * @brief Largest outbound payload permessage-deflate will compress, in bytes.
 *
 * The compressor borrows `len + len/8 + 16` from the scratch arena, so an unbounded outbound length
 * would leave the arena with no worst case - `ws_send_frame()` takes a `uint16_t`, and neither
 * WS_FRAME_SIZE (which bounds the *inbound* reassembled message) nor PC_WS_FRAG_SIZE (off by
 * default, and a runtime setter besides) constrains it. This is that bound, and it is what makes
 * PC_PLAINTEXT_WORK_WS_SEND a compile-time constant.
 *
 * A larger message is still sent, uncompressed, as the per-message RSV1 flag makes legal - the same
 * outcome as before this was declared, except chosen rather than reached by an allocation failure.
 * Raising it costs arena: the term grows by roughly 1.125x the increase.
 */
#ifndef PC_WS_DEFLATE_MAX
#define PC_WS_DEFLATE_MAX WS_FRAME_SIZE
#endif

// ---------------------------------------------------------------------------
// Server-Sent Events sizing constants
// ---------------------------------------------------------------------------

/**
 * @brief Maximum simultaneous SSE connections.
 *
 * Each connection occupies one TCP slot from MAX_CONNS and one entry in
 * pc_sse_pool[].  MAX_WS_CONNS + MAX_SSE_CONNS must not exceed MAX_CONNS.
 */
#ifndef MAX_SSE_CONNS
#define MAX_SSE_CONNS 2
#endif

/**
 * @brief Output buffer size in bytes for a single SSE event.
 *
 * An event larger than this is silently truncated.  The buffer holds the
 * formatted `data: ...\n\n` line before it is handed to tcp_write().
 */
#ifndef SSE_BUF_SIZE
#define SSE_BUF_SIZE 256
#endif

// ---------------------------------------------------------------------------
// Static file serving sizing constants
// ---------------------------------------------------------------------------

/**
 * @brief Bytes read from the filesystem and passed to tcp_write() per loop().
 *
 * Each read+send is one tcpip_thread round-trip (~23 us on-device), so a larger chunk =
 * fewer round-trips per byte (better throughput on a fast transport), at more peak stack.
 * Must be <= RX_BUF_SIZE to avoid stalling the TCP send window; 1024 tracks the default
 * RX_BUF_SIZE. Lower it (e.g. -DFILE_CHUNK_SIZE=512) on a stack-constrained target.
 */
#ifndef FILE_CHUNK_SIZE
#define FILE_CHUNK_SIZE 1024
#endif

// ---------------------------------------------------------------------------
// Basic Auth sizing constants
// ---------------------------------------------------------------------------

/**
 * @brief Maximum username or password length for HTTP Basic Authentication.
 *
 * Both username and password must fit in this many bytes including the
 * null terminator.  Longer credentials are silently rejected with 401.
 */
#ifndef MAX_AUTH_LEN
#define MAX_AUTH_LEN 32
#endif

// ---------------------------------------------------------------------------
// MultipartBody form-data sizing constants
// ---------------------------------------------------------------------------

/**
 * @brief Maximum simultaneously parsed multipart parts per request.
 *
 * Parts beyond this limit are silently ignored.  A typical upload form
 * has 1-4 fields; increase this for forms with more.
 */
#ifndef MAX_MULTIPART_PARTS
#define MAX_MULTIPART_PARTS 4
#endif

/**
 * @brief Maximum MIME boundary length (RFC 2046 allows up to 70 characters).
 */
#ifndef MAX_BOUNDARY_LEN
#define MAX_BOUNDARY_LEN 72
#endif

// ---------------------------------------------------------------------------
// Event queue depth
// ---------------------------------------------------------------------------

/**
 * @brief Depth of the FreeRTOS event queue shared between lwIP callbacks and
 *        the main-loop task.
 *
 * Each slot holds one TcpEvt (8 bytes).  The queue is the only heap
 * allocation the library makes at begin() time:
 *
 *   heap = sizeof(StaticQueue_t) + EVT_QUEUE_DEPTH * sizeof(TcpEvt)
 *
 * Must be large enough to absorb a burst of MAX_CONNS * 4 events without
 * blocking the lwIP thread, so it tracks MAX_CONNS automatically (a raised
 * MAX_CONNS never trips the EVT_QUEUE_DEPTH >= MAX_CONNS * 4 guard below).
 */
#ifndef EVT_QUEUE_DEPTH
#define EVT_QUEUE_DEPTH (MAX_CONNS * 4)
#endif

// ---------------------------------------------------------------------------
// Internal response buffer sizing constants
// ---------------------------------------------------------------------------

/**
 * @brief Stack buffer for HTTP response header lines in send() / send_empty() /
 *        send_unauth() / serve_file().
 *
 * Must be large enough to hold the status line, Content-Type, Content-Length,
 * Connection, and any CORS headers.  The CORS block alone can reach
 * CORS_HDR_BUF_SIZE bytes, so this value should be at least
 * CORS_HDR_BUF_SIZE + 96.
 */
#ifndef RESP_HDR_BUF_SIZE
#define RESP_HDR_BUF_SIZE 768
#endif

/**
 * @brief Per-connection buffer for app-supplied custom response headers and
 *        cookies.
 *
 * Filled by proto_add_response_header() / set_cookie() and injected into send() /
 * send_empty() / redirect() the same way the CORS block is. RESP_HDR_BUF_SIZE
 * must be large enough to hold the status line plus the CORS block plus this
 * block (see the assert below).
 */
#ifndef EXTRA_HDR_BUF_SIZE
#define EXTRA_HDR_BUF_SIZE 256
#endif

/**
 * @brief Stack buffer for the HTTP 101 Switching Protocols response sent during
 *        the WebSocket handshake.
 *
 * Must hold: status line + Upgrade + Connection + Sec-WebSocket-Accept (28
 * base64 chars) + CRLF pairs.  Minimum is ~120 bytes; default leaves margin.
 */
#ifndef WS_HDR_BUF_SIZE
#define WS_HDR_BUF_SIZE 256
#endif

/**
 * @brief Size of the pre-built CORS header block stored in PC.
 *
 * Built once by set_cors() and injected into every response.  Must hold
 * Access-Control-Allow-Origin, Access-Control-Allow-Methods, and
 * Access-Control-Allow-Headers lines for the configured origin.
 */
#ifndef CORS_HDR_BUF_SIZE
#define CORS_HDR_BUF_SIZE 192
#endif

/**
 * @brief Size of the optional Cache-Control header line stored in PC.
 *
 * Built once by set_cache_control() and injected into static-file responses
 * (serve_file / serve_static) beside the ETag. Holds "Cache-Control: <value>\r\n".
 */
#ifndef CACHE_CONTROL_BUF_SIZE
#define CACHE_CONTROL_BUF_SIZE 64
#endif

// ---------------------------------------------------------------------------
// Feature flags
// ---------------------------------------------------------------------------
// Set any of these to 0 in your sketch BEFORE including this library to strip
// the feature from the build entirely (no code, no RAM, no flash cost).
//
//   #define PC_ENABLE_WEBSOCKET 0
//   #include <protocore.h>
//
// ---------------------------------------------------------------------------
// BUILD-FLAG DEPENDENCY TREE
// ---------------------------------------------------------------------------
// Most features are independent. A few build on another feature and cannot
// compile without it; those HARD dependencies are enforced near the bottom of
// this file with a clear #error, so an illegal combination fails fast at
// compile time instead of producing a cryptic linker error. Enable a child
// only together with its parent(s).
//
// Hard dependencies (child needs parent) are DECLARED, not described here. Each one is a symbol:
//
//   #define PC_ENABLE_WEBDAV_NEEDS_FILE_SERVING PC_ENABLE_FILE_SERVING
//   #if PC_ENABLE_WEBDAV && !PC_ENABLE_WEBDAV_NEEDS_FILE_SERVING
//   #error "ProtoCore: PC_ENABLE_WEBDAV needs PC_ENABLE_FILE_SERVING"
//   #endif
//
// so the whole graph is `grep -oE '_NEEDS_[A-Z_0-9]+' protocore_config.h` and both sides come out
// of the name. The guard tests the symbol rather than restating the condition, so what is enforced
// and what is declared are the same text.
//
// Optional integrations (these build fine on their own; the named feature is
// simply inert or reduced until you also enable the other flag):
//
//   WEBHOOK   + HTTP_CLIENT : without it, pc_webhook_post() returns -1
//   OAUTH2    + HTTP_CLIENT : the token-endpoint POST helpers compile only with it
//   DASHBOARD + WEBSOCKET   : adds live control widgets; the SSE value stream works alone
//
// Auto-derived (do NOT set these yourself; the library computes them):
//
//   STREAM_BODY         = OTA || UPLOAD
//   CLIENT_TLS          = HTTP_CLIENT_TLS || MQTT_TLS || WS_CLIENT_TLS
//   CAPTURE_AUTH_HEADER = AUTH || JWT || OIDC
//
// The same tree appears in README.md and examples/Foundation/Configuration.
// ---------------------------------------------------------------------------

/** @brief WebSocket support (RFC 6455 framing + SHA-1/base64 handshake). */
#ifndef PC_ENABLE_WEBSOCKET
#define PC_ENABLE_WEBSOCKET 0
#endif

/**
 * @brief WebSocket permessage-deflate (RFC 7692) - bidirectional compression.
 *
 * When set (and PC_ENABLE_WEBSOCKET is on), the server negotiates the
 * `permessage-deflate` extension and both decompresses inbound compressed (RSV1)
 * messages via a bounded INFLATE (network_drivers/presentation/inflate.*) and
 * compresses outbound data frames via a bounded DEFLATE
 * (network_drivers/presentation/deflate.*); both borrow their table scratch from
 * the shared per-dispatch arena. The extension is negotiated with
 * `{client,server}_no_context_takeover` so every message (de)compresses
 * independently - no window is carried between messages. An outbound frame that
 * would not shrink is sent uncompressed (the per-message RSV1 flag permits this).
 * Default off.
 */
#ifndef PC_ENABLE_WS_DEFLATE
#define PC_ENABLE_WS_DEFLATE 0
#endif

/**
 * @brief WebSocket outbound fragmentation size (RFC 6455 sec 5.4), in payload bytes. 0 = off.
 *
 * When >0, an outbound data message (text/binary) longer than this many payload bytes is split into
 * that-sized WebSocket frames - the first carrying the opcode (and the RFC 7692 RSV1 bit if the message
 * is compressed), the rest CONTINUATION, the last with FIN - instead of one large frame. Sizing it near
 * the TCP MSS (e.g. 1400) keeps each frame within whole segments (MTU-aligned) and lets a peer with a
 * bounded per-frame reassembly buffer receive an arbitrarily long message. The runtime override is
 * ws_set_frag_size(). Compression applies to the whole message first, then the compressed bytes are
 * split. Default 0 (one frame per message, unchanged).
 */
#ifndef PC_WS_FRAG_SIZE
#define PC_WS_FRAG_SIZE 0
#endif

/** @brief Server-Sent Events push support. */
#ifndef PC_ENABLE_SSE
#define PC_ENABLE_SSE 0
#endif

/** @brief multipart/form-data body parser. */
#ifndef PC_ENABLE_MULTIPART
#define PC_ENABLE_MULTIPART 0
#endif

/**
 * @brief Zero-heap CBOR (RFC 8949) encoder for compact binary payloads.
 *
 * Default off. When set, network_drivers/presentation/codec/cbor/cbor.h provides a writer
 * that serializes ints, strings, byte strings, arrays, maps, booleans, null, and
 * float32 into a caller-provided buffer - a compact binary alternative to the JSON
 * writer for telemetry. Pure, no heap, host-tested against the RFC 8949 vectors.
 */
#ifndef PC_ENABLE_CBOR
#define PC_ENABLE_CBOR 0
#endif

/**
 * @brief Zero-heap MessagePack encoder and decoder for compact binary payloads.
 *
 * Default off. When set, network_drivers/presentation/codec/msgpack/msgpack.h provides a
 * writer that serializes ints, strings, byte strings, arrays, maps, booleans, nil,
 * and float32 into a caller-provided buffer, plus a cursor decoder (MsgPack.peek /
 * MsgPack.get_*, no-copy strings) over a caller buffer - the MessagePack-format
 * sibling of the CBOR / JSON readers and writers. Pure, no heap, host-tested
 * against the spec encodings and round-trip.
 */
#ifndef PC_ENABLE_MSGPACK
#define PC_ENABLE_MSGPACK 0
#endif

/** @brief Static file serving via Arduino FS (LittleFS, SPIFFS, SD). */
#ifndef PC_ENABLE_FILE_SERVING
#define PC_ENABLE_FILE_SERVING 0
#endif

/**
 * @brief WebDAV server (RFC 4918, class 1 + advisory locks) over the file system.
 *
 * Default off. When set (requires PC_ENABLE_FILE_SERVING), dav() mounts an FS
 * subtree that answers the WebDAV methods - OPTIONS, PROPFIND (Depth 0/1),
 * PROPPATCH, GET, HEAD, PUT, DELETE, MKCOL, COPY, MOVE, and advisory LOCK/UNLOCK -
 * so a client (rclone, cadaver, curl, or a mounted network drive) can browse and
 * edit files. PROPFIND returns a 207 Multi-Status document built into a fixed
 * buffer (PC_WEBDAV_BUF_SIZE); a Depth-1 listing is capped at
 * PC_WEBDAV_MAX_ENTRIES children. PROPPATCH returns a 207 with each requested
 * property refused 403 Forbidden (the live properties are read-only, no dead-
 * property store) - this keeps Windows Explorer / macOS Finder, which PROPPATCH a
 * timestamp right after a PUT, from erroring on a 405. PUT streams the request
 * body straight to the file (via the shared streaming-body sink), so an upload is
 * not bounded by BODY_BUF_SIZE. Locks are advisory (a synthetic token is issued
 * but not enforced). See docs/SECURITY.md before exposing it.
 */
#ifndef PC_ENABLE_WEBDAV
#define PC_ENABLE_WEBDAV 0
#endif

/** @brief Buffer (BSS) for a WebDAV 207 Multi-Status response, in bytes (see PC_ENABLE_WEBDAV). */
#ifndef PC_WEBDAV_BUF_SIZE
#define PC_WEBDAV_BUF_SIZE 2048
#endif

/** @brief Maximum children listed in a WebDAV Depth-1 PROPFIND (bounds the response). */
#ifndef PC_WEBDAV_MAX_ENTRIES
#define PC_WEBDAV_MAX_ENTRIES 32
#endif

/**
 * @brief Deepest tree a WebDAV DELETE / COPY walks before refusing (see PC_ENABLE_WEBDAV).
 *
 * The recursive walkers carry one path and one child name per level, so this is what turns their
 * working storage into a fixed number instead of one that grows with the tree being walked. It was
 * a bare 8 in the walk's own test, which bounded the recursion but sized nothing, because the paths
 * were stack arrays the footprint could not see.
 */
#ifndef PC_DAV_MAX_DEPTH
#define PC_DAV_MAX_DEPTH 8
#endif

/** @brief Maximum properties echoed in a WebDAV PROPPATCH 207 response (bounds the response). */
#ifndef PC_WEBDAV_MAX_PROPS
#define PC_WEBDAV_MAX_PROPS 16
#endif

/**
 * @brief HTTP method-token buffer size (bytes, including the NUL).
 *
 * Sized for the longest method the server must recognize: 8 normally (OPTIONS),
 * grown to fit the WebDAV methods (PROPPATCH is 9 chars) when WebDAV is enabled.
 */
#ifndef PC_METHOD_BUF_SIZE
#if PC_ENABLE_WEBDAV
#define PC_METHOD_BUF_SIZE 12
#else
#define PC_METHOD_BUF_SIZE 8
#endif
#endif

/** @brief HTTP Basic Authentication per-route. */
#ifndef PC_ENABLE_AUTH
#define PC_ENABLE_AUTH 0
#endif

/** @brief Telnet server support (RFC 854 / IAC option negotiation). */
#ifndef PC_ENABLE_TELNET
#define PC_ENABLE_TELNET 0
#endif

/** @brief SSH server support (RFC 4253/4252/4254). */
#ifndef PC_ENABLE_SSH
#define PC_ENABLE_SSH 0
#endif

/**
 * @brief Outbound SSH client + reverse tunnel (RFC 4254 §7.1 tcpip-forward, the `ssh -R` seam).
 *
 * The device dials OUT to a relay as the SSH *client* and asks it to forward a port back, so a device
 * behind NAT stays reachable. Reuses the transport crypto (curve25519 / ecdh-p256 / dh-group14 KEX;
 * ssh-ed25519 / rsa / ecdsa host-key verify; chacha / aes-gcm / aes-ctr) and the role-aware packet
 * layer. Needs the outbound TCP client transport. Default off.
 */
#ifndef PC_ENABLE_SSH_CLIENT
#define PC_ENABLE_SSH_CLIENT 0
#endif

/**
 * @brief Post-quantum hybrid key exchange: ML-KEM-768 + X25519 (FIPS 203 / RFC 9370 combiner).
 *
 * Adds the mlkem768x25519-sha256 SSH KEX method (draft-ietf-sshm-mlkem-hybrid-kex) and the
 * X25519MLKEM768 TLS 1.3 group (IANA 0x11ec) for HTTP/3, so a PQC-capable peer (OpenSSH 9.x+ and
 * current browsers, which now DEFAULT to hybrid) negotiates a quantum-resistant handshake instead of
 * down-negotiating to classical X25519. The device is always the responder, so only ML-KEM Encaps
 * ships (no KeyGen/Decaps, so none of the constant-time FO-comparison surface). The NTT core is
 * software with Montgomery reduction over q=3329 (the MPI accelerator is for RSA/DH-sized operands,
 * not 12-bit coefficients). Requires PC_ENABLE_SSH and/or PC_ENABLE_HTTP3.
 */
#ifndef PC_ENABLE_PQC_KEX
#define PC_ENABLE_PQC_KEX 0
#endif

/**
 * @brief Streamlined NTRU Prime sntrup761x25519-sha512@openssh.com SSH KEX (default: tracks
 *        ::PC_ENABLE_PQC_KEX).
 *
 * A second PQ/T hybrid alongside ML-KEM: sntrup761 (a lattice KEM with a conservative security
 * margin, OpenSSH's long-standing default) crossed with X25519, SHA-512 exchange hash. On by
 * default wherever the PQC hybrid is enabled so a PQC-capable peer gets both methods offered.
 * It is heavier than ML-KEM on the worker stack - the server runs Encaps (~22 KB peak) and the
 * reverse-SSH client runs KeyGen+Decaps (the FO re-encrypt peaks ~32 KB) - so a footprint-bound
 * PQC build (e.g. a classic-ESP32 that only wants ML-KEM) can set this to 0 to drop sntrup761 and
 * keep the lighter ::PC_WORKER_STACK_PQC_MIN floor. Requires ::PC_ENABLE_PQC_KEX.
 */
#ifndef PC_ENABLE_SSH_SNTRUP761
#define PC_ENABLE_SSH_SNTRUP761 PC_ENABLE_PQC_KEX
#endif

/**
 * @brief Modbus TCP slave/server (Modbus Application Protocol v1.1b3) on TCP/502.
 *
 * Default off. When set, listen(502, PROTO_MODBUS) serves a fixed data model
 * (coils, discrete inputs, holding + input registers, all in BSS) over Modbus
 * TCP: Read/Write Coils (FC 1/5/15), Read Discrete Inputs (FC 2), Read/Write
 * Holding Registers (FC 3/6/16), and Read Input Registers (FC 4). The codec
 * (MBAP framing + PDU dispatch) is pure and host-tested; the TCP transport is
 * ESP32-only. The application reads/writes the model with the accessor functions
 * and is notified of client writes via pc_modbus_on_write(). Modbus has no
 * authentication or encryption - run it only on a trusted control network.
 */
#ifndef PC_ENABLE_MODBUS
#define PC_ENABLE_MODBUS 0
#endif

/**
 * @brief Modbus RTU framing (serial / RS-485) over the same data model + PDU dispatch.
 *
 * Default off; implies PC_ENABLE_MODBUS. Adds the RTU ADU codec
 * `pc_modbus_rtu_process_adu()` - a `[slave addr][PDU][CRC16]` frame (CRC16-Modbus,
 * little-endian) around the existing host-tested PDU dispatch: a CRC mismatch or a
 * non-matching unit address is dropped silently (no reply, per the spec), and a
 * broadcast (address 0) is executed without a reply. The codec is pure and
 * host-tested; feed it from a UART/RS-485 driver (the serial transport is the
 * application's, framed by the 3.5-char inter-frame idle).
 */
#ifndef PC_ENABLE_MODBUS_RTU
#define PC_ENABLE_MODBUS_RTU 0
#endif
// RTU is a framing over the same PDU codec, so it needs Modbus compiled in. The requirement is
// derived rather than written back over PC_ENABLE_MODBUS: a user's flag is an input and stays one,
// so -DPC_ENABLE_MODBUS=0 keeps meaning what it says. Code guards on PC_NEED_MODBUS.
#define PC_NEED_MODBUS (PC_ENABLE_MODBUS || PC_ENABLE_MODBUS_RTU)

/**
 * @brief CloudEvents v1.0 (CNCF) event envelope (structured JSON + binary headers).
 *
 * Default off. Adds `services/cloudevents`: `pc_cloudevents_build_json()` emits a
 * structured `application/cloudevents+json` envelope (required `id`/`source`/`type`
 * + optional `subject`/`datacontenttype`/`data`) via the JSON writer, and
 * `pc_cloudevents_from_headers()` reads an inbound binary-mode event's `ce-*` headers.
 * Makes the device's events interoperable with serverless / event-mesh consumers.
 */
#ifndef PC_ENABLE_CLOUDEVENTS
#define PC_ENABLE_CLOUDEVENTS 0
#endif

/**
 * @brief Redis RESP2/RESP3 wire codec (`services/iot/redis_resp`).
 *
 * Default off. A zero-heap command encoder (a command becomes a RESP array of bulk strings) and a
 * streaming reply decoder that reads one value at a time, so an arbitrarily nested reply is walked
 * with only the caller's loop state and never a tree allocation. Covers RESP2 and the RESP3
 * additions (null / boolean / double / big number / bulk error / verbatim / map / set / push).
 * Pure codec - you hand it byte buffers; the connection is the application's. Host-tested against
 * the spec vectors and a real redis-server.
 */
#ifndef PC_ENABLE_REDIS
#define PC_ENABLE_REDIS 0
#endif

/**
 * @brief STOMP 1.2 frame codec (`services/stomp`).
 *
 * Default off. A zero-heap frame builder (`pc_stomp_build_frame`, command + escaped
 * headers + NUL-terminated body) + a non-mutating parser (`pc_stomp_parse_frame`,
 * command / header slices / body, honoring `content-length`) so the device can talk
 * to a STOMP broker (ActiveMQ / RabbitMQ / Artemis) over the shipped outbound client
 * transport, or STOMP-over-WebSocket via the WS client. Pure codec, host-tested.
 */
#ifndef PC_ENABLE_STOMP
#define PC_ENABLE_STOMP 0
#endif

/** @brief Max header lines parsed per STOMP frame (extras beyond this are ignored). */
#ifndef PC_STOMP_MAX_HEADERS
#define PC_STOMP_MAX_HEADERS 16
#endif

/**
 * @brief MQTT-SN v1.2 wire codec (`services/iot/mqtt/mqtt_sn`).
 *
 * Default off. A zero-heap message builder + parser for MQTT for Sensor Networks - the
 * UDP / non-TCP MQTT variant for constrained, lossy links (numeric topic IDs instead of
 * strings, gateway discovery, sleeping-client keep-alive). Builds CONNECT / REGISTER /
 * PUBLISH / SUBSCRIBE / PINGREQ / DISCONNECT / SEARCHGW and parses CONNACK / REGACK /
 * PUBACK / SUBACK / PUBLISH / REGISTER, including the 1- and 3-octet Length forms. Pure
 * codec, host-tested; the datagram send (Udp.client->sendto) and topic registry are the app's.
 */
#ifndef PC_ENABLE_MQTT_SN
#define PC_ENABLE_MQTT_SN 0
#endif

/**
 * @brief Flow-record export codec (`services/flow_export`).
 *
 * Default off. A zero-heap exporter-side codec for on-device flow accounting: NetFlow v5
 * (fixed 24-octet header + 48-octet records), NetFlow v9 (RFC 3954), and IPFIX (RFC 7011),
 * the latter two via a small cursor that emits a Template then matching Data records and
 * patches the message length (IPFIX) or record count (v9) on finish. Pure codec,
 * host-tested; the flow cache (5-tuple + counters) and the UDP send (Udp.client->sendto) are
 * the application's. Pairs with the telemetry / observability services.
 */
#ifndef PC_ENABLE_FLOW_EXPORT
#define PC_ENABLE_FLOW_EXPORT 0
#endif

/**
 * @brief Protocol Buffers wire codec (`services/protobuf`).
 *
 * Default off. A zero-heap streaming Protobuf encoder + cursor reader over caller buffers
 * (the same shape as the CBOR / MessagePack codecs): varint / ZigZag / fixed32 / fixed64 /
 * length-delimited fields, with embedded messages built into a sub-buffer and added via
 * `pc_pb_bytes`. Pure codec, host-tested against the spec vectors. This is the standalone
 * Protobuf deliverable; gRPC (framed Protobuf over HTTP/2) is gated on the HTTP/2 item.
 */
#ifndef PC_ENABLE_PROTOBUF
#define PC_ENABLE_PROTOBUF 0
#endif

/**
 * @brief WAMP messaging codec (`services/wamp`).
 *
 * Default off. A zero-heap codec for the Web Application Messaging Protocol (unified RPC +
 * PubSub over WebSocket): builders for HELLO / SUBSCRIBE / PUBLISH / CALL / REGISTER /
 * YIELD / GOODBYE (JSON arrays emitted via the shared JsonWriter) and a positional parser
 * that pulls the message type, ids, and URIs out of an inbound array. Rides the shipped
 * WebSocket layer; the session / subscription / registration tables are the application's.
 * Pure codec, host-tested. Builds on the always-on JSON writer.
 */
#ifndef PC_ENABLE_WAMP
#define PC_ENABLE_WAMP 0
#endif

/**
 * @brief SunSpec Modbus device-information-model codec (`services/sunspec`).
 *
 * Default off. A zero-heap codec for the SunSpec Alliance register maps layered on the
 * holding-register model: a model-chain walker (verify the `SunS` marker, then iterate each
 * model's id / length / body) + typed point readers (u16 / i16 / u32 / i32 / string) and a
 * map writer (marker, model headers + points, end model). Makes a solar inverter / meter /
 * battery interoperable. Pure codec, host-tested; pairs with the Modbus service.
 */
#ifndef PC_ENABLE_SUNSPEC
#define PC_ENABLE_SUNSPEC 0
#endif

/**
 * @brief IEEE C37.118.2 synchrophasor frame codec (`services/c37118`).
 *
 * Default off. A zero-heap builder + CRC-validating parser for the PMU / PDC wide-area
 * measurement wire protocol: `pc_c37118_build_frame` / `pc_c37118_build_command` emit a
 * `SYNC FRAMESIZE IDCODE SOC FRACSEC DATA CHK` frame (CHK = CRC-CCITT) and
 * `pc_c37118_parse_frame` validates the CRC and reports the frame type / ids / timestamp /
 * payload slice. Frames any payload and fully handles the fixed Command frame. Pure codec,
 * host-tested.
 */
#ifndef PC_ENABLE_C37118
#define PC_ENABLE_C37118 0
#endif

/**
 * @brief DNP3 (IEEE 1815) data-link frame codec (`services/dnp3`).
 *
 * Default off. A zero-heap builder + CRC-validating parser for the SCADA / utility
 * outstation data-link layer: `pc_dnp3_build_frame` emits the `0x0564 LEN CTRL DEST SRC CRC`
 * header block + the CRC'd 16-octet user-data blocks, and `pc_dnp3_parse_frame` validates the
 * header and every block CRC (CRC-16/DNP) and de-blocks the user data. Pure codec,
 * host-tested; the transport-function reassembly and the application layer are layered on
 * the de-blocked user data.
 */
#ifndef PC_ENABLE_DNP3
#define PC_ENABLE_DNP3 0
#endif

/**
 * @brief CANopen (CiA 301) message codec (`services/canopen`).
 *
 * Default off. A zero-heap builder + parser for the CANopen messaging set over classic CAN
 * frames (`shared_primitives/can.h`): NMT node control, SYNC, TIME, heartbeat / boot-up,
 * EMCY, PDO process data, and expedited SDO read / write / abort. The 11-bit COB-ID is a
 * 4-bit function code plus a 7-bit node id; builders compute it, parsers classify it back.
 * The object dictionary is the application's; SDO is expedited only (segmented / block not
 * yet covered). Pure codec, host-tested. Drive it from the ESP32 TWAI peripheral or an
 * MCP2515 over SPI to bridge a CANopen bus onto Wi-Fi.
 */
#ifndef PC_ENABLE_CANOPEN
#define PC_ENABLE_CANOPEN 0
#endif

/**
 * @brief CiA 402 / IEC 61800-7-201 drive + motion profile (`services/cia402`).
 *
 * Default off. Requires CANOPEN. The standardized servo / stepper drive profile over CANopen:
 * `pc_cia402_state` decodes the power state machine from the Statusword (the CiA 402 mask/value
 * table), `pc_cia402_controlword` / `pc_cia402_enable_sequence` produce the Controlword commands that
 * walk an axis to Operation Enabled, and the `pc_cia402_sdo_set_*` / `pc_cia402_pack_command` helpers
 * write Controlword / Modes of Operation / target position-velocity-torque via the shipped
 * CANopen SDO / PDO codec. State masks + command values + object indices verified against
 * IEC 61800-7-201. Pure profile, host-tested. Turns the CAN stack into a motion master; close
 * the loop with a `services/control` PID.
 */
#ifndef PC_ENABLE_CIA402
#define PC_ENABLE_CIA402 0
#endif

/**
 * @brief Closed-loop control law (`services/control`).
 *
 * Default off. A zero-heap, FPU-accelerated PID controller (single-precision float, FMA-folded,
 * IRAM-placeable with PC_CONTROL_IRAM=1) with derivative-on-measurement, an optional
 * derivative low-pass, output clamping, and anti-windup by back-calculation plus a hard integral
 * clamp, and a feed-forward term - plus inline control-law primitives (clamp / deadband / slew /
 * low-pass). `pid_update` runs one loop; `pid_update_n` runs a batch of axes off one tick. Pair
 * it with a plant it can command (a `services/cia402` drive, a dshot ESC, a heater) and tune the
 * gains offline with `tools/pid_tune.py`. Pure math, host-tested.
 */
#ifndef PC_ENABLE_CONTROL
#define PC_ENABLE_CONTROL 0
#endif

/**
 * @brief SAE J1939 message codec (`services/j1939`).
 *
 * Default off. A zero-heap codec for the heavy-duty-vehicle / agriculture / marine / genset
 * CAN higher-layer protocol over 29-bit extended frames (`shared_primitives/can.h`):
 * `pc_j1939_encode_id` / `pc_j1939_decode_id` pack and unpack the priority / PGN / SA / DA
 * identifier (PDU1 peer + PDU2 broadcast), `pc_j1939_build_message` emits single frames,
 * `pc_j1939_build_request` / `pc_j1939_build_address_claim` (+ `pc_j1939_build_name`) handle the
 * Request PGN and Address Claimed messages, and the Transport Protocol (BAM announce +
 * TP.DT packets) reassembles multi-packet messages up to `PC_J1939_TP_MAX` octets. Pure
 * codec, host-tested. Drive it from the ESP32 TWAI peripheral or an MCP2515 over SPI.
 */
#ifndef PC_ENABLE_J1939
#define PC_ENABLE_J1939 0
#endif

/**
 * @brief DeviceNet link-adaptation codec (`services/devicenet`).
 *
 * Default off. The CAN-specific layer of "CIP over CAN": the 11-bit DeviceNet identifier as a
 * Message Group (1..4) + Message ID + MAC ID (`pc_devicenet_encode_id` / `pc_devicenet_decode_id`),
 * the explicit-message header octet, single-frame explicit messages, and the fragmentation
 * protocol with a reassembler (`pc_devicenet_frag_feed`) for bodies longer than one CAN frame.
 * The CIP application layer (services / EPATH / data) is the same one EtherNet/IP uses, so
 * build the body with the existing `pc_cip_*` functions (`PC_ENABLE_CIP`). Pure codec,
 * host-tested. Drive it from the ESP32 TWAI peripheral or an MCP2515 over SPI.
 */
#ifndef PC_ENABLE_DEVICENET
#define PC_ENABLE_DEVICENET 0
#endif

/**
 * @brief NMEA 2000 codec (`services/nmea2000`).
 *
 * Default off; implies PC_ENABLE_J1939 (NMEA 2000 is J1939 at the transport layer). A
 * zero-heap codec for the marine instrumentation network over CAN: it reuses the J1939 29-bit
 * identifier codec and adds the NMEA-specific Fast Packet transport - `pc_n2k_fastpacket_build_frame`
 * splits a 9..223-octet message across frames (a control octet of sequence + frame counter,
 * the first frame carrying the total length) and `pc_n2k_fastpacket_feed` reassembles it;
 * `pc_n2k_build_single` wraps a single-frame message. Pure codec, host-tested. Drive it from the
 * ESP32 TWAI peripheral or an MCP2515 over SPI to bridge an NMEA 2000 backbone onto Wi-Fi.
 */
#ifndef PC_ENABLE_NMEA2000
#define PC_ENABLE_NMEA2000 0
#endif
// NMEA 2000 reuses the J1939 identifier codec, so it needs J1939 compiled in.
#define PC_NEED_J1939 (PC_ENABLE_J1939 || PC_ENABLE_NMEA2000)

/**
 * @brief Wired M-Bus (Meter-Bus, EN 13757) frame codec (`services/mbus`).
 *
 * Default off. A zero-heap builder + parser for the M-Bus link-layer frames used by utility
 * meters (water / gas / heat / electricity): the single-character ACK, the short frame
 * (`10 C A CS 16`), and the long / control frame (`68 L L 68 C A CI ... CS 16`, 8-bit sum
 * checksum), plus `pc_mbus_record_next` which walks the EN 13757-3 variable-data records
 * (DIF / VIF, skipping DIFE / VIFE extension chains and decoding the data length). Pure codec,
 * host-tested. Talk to the powered two-wire bus over a UART through an M-Bus level converter
 * (e.g. a TSS721-based master) and bridge meter readings onto Wi-Fi.
 */
#ifndef PC_ENABLE_MBUS
#define PC_ENABLE_MBUS 0
#endif

/**
 * @brief IEC 60870-5-101 / -104 telecontrol (SCADA) codec (`services/iec60870`).
 *
 * Default off. The utility-SCADA protocol in both transports: the -104 APCI over TCP
 * (`68 LEN` + 4 control octets in I / S / U formats via `pc_iec104_build_i/_s/_u` + `pc_iec104_parse`),
 * the shared ASDU header + 3-octet Information Object Address (`pc_iec_asdu_build_header` /
 * `pc_iec_asdu_parse_header`, `pc_iec_put_ioa` / `pc_iec_get_ioa`), and the -101 FT1.2 serial link
 * frames (fixed + variable, 8-bit sum checksum, via `pc_iec101_build_fixed` / `_variable` +
 * `pc_iec101_parse`). Named type-id / cause-of-transmission constants are provided; the
 * per-type information elements are the application's. Pure codec, host-tested. Run -104 over
 * the shipped TCP stack or -101 over a UART/RS-485 transceiver to bridge an RTU onto Wi-Fi.
 */
#ifndef PC_ENABLE_IEC60870
#define PC_ENABLE_IEC60870 0
#endif

/**
 * @brief SDI-12 sensor-bus codec (`services/sdi12`).
 *
 * Default off. A zero-heap command / response codec for the 1200-baud single-wire ASCII bus
 * used by environmental / agricultural sensors: builders for the standard commands
 * (`pc_sdi12_build_measure` / `_concurrent` / `_data` / `_identify` / `_change_address` /
 * `_query_address`), a parser for the measurement response (`pc_sdi12_parse_measure`: seconds
 * until ready + value count), a data-value splitter (`pc_sdi12_parse_values`), and the SDI-12
 * CRC (`pc_sdi12_crc16` / `pc_sdi12_crc_encode` / `pc_sdi12_check_crc`) for the CRC-protected `aMC!` /
 * `aCC!` variants. Pure codec, host-tested. Drive the single 1200-baud line over a UART (with
 * a small level / direction circuit) and bridge sensor readings onto Wi-Fi.
 */
#ifndef PC_ENABLE_SDI12
#define PC_ENABLE_SDI12 0
#endif

/**
 * @brief DMX512 + RDM (ANSI E1.20) lighting codec (`services/dmx`).
 *
 * Default off. A zero-heap codec for stage / architectural lighting over RS-485: `pc_dmx_build` /
 * `pc_dmx_get_channel` assemble and read the positional DMX512 slot packet (a start code + up to
 * 512 channels), and the RDM (Remote Device Management) functions build / parse the addressed
 * management packet that shares the wire - `pc_rdm_build` / `pc_rdm_parse` with 48-bit source /
 * destination UIDs (`pc_rdm_uid`), a command class + parameter id, and the 16-bit additive
 * checksum (`pc_rdm_checksum`). Pure codec, host-tested. Drive a `MAX485`-class transceiver on a
 * UART (250 kbit/s, 8N2; the break is the application's) and bridge a lighting rig onto Wi-Fi.
 */
#ifndef PC_ENABLE_DMX
#define PC_ENABLE_DMX 0
#endif

/**
 * @brief NMEA 0183 sentence codec (`services/nmea0183`).
 *
 * Default off. A zero-heap codec for the marine / GPS ASCII protocol (`$GPGGA,...*47`):
 * `pc_nmea0183_build` emits a sentence (adding the `$`, XOR checksum, and CR/LF), `pc_nmea0183_parse`
 * validates the `*HH` checksum and splits the comma-separated fields (deriving talker id +
 * sentence type from the address field), and `pc_nmea0183_field_float` / `_int` decode field
 * values. Sentence framing + checksum verified against the NMEA 0183 standard (the canonical
 * GGA example); pure and host-tested. GPS / marine receivers are cheap UART breakouts, so this
 * is a plain HardwareSerial link (4800 / 9600 baud); bridge position / wind / depth onto Wi-Fi.
 */
#ifndef PC_ENABLE_NMEA0183
#define PC_ENABLE_NMEA0183 0
#endif

/**
 * @brief Wi-Fi roaming decision layer (`services/roaming`).
 *
 * The pure policy that decides whether and where to roam to a better access point, given the current
 * link RSSI, a candidate neighbor list (from an 802.11k neighbor report or a scan), and an optional
 * 802.11v BSS-Transition-Management hint from the network. `Roam.decide` fuses those into a
 * roam/stay decision with a target BSSID + channel and a reason (a disassociation-imminent BTM, a
 * network-suggested BTM, or a weak link with a clearly stronger candidate past a hysteresis margin).
 * Pure, stateless, host-tested; the actual scan / neighbor-report request and the 802.11r fast
 * transition that executes the decision live in the Wi-Fi supplicant.
 */
#ifndef PC_ENABLE_ROAMING
#define PC_ENABLE_ROAMING 0
#endif

/**
 * @brief u-blox UBX binary GNSS protocol codec (`services/ubx`).
 *
 * The binary companion to NMEA 0183 that u-blox receivers speak on the same UART. `pc_ubx_build`
 * frames a message (sync chars B5 62, class/id, little-endian length, payload, 8-bit Fletcher
 * checksum), `pc_ubx_build_poll` emits a zero-length poll request, `pc_ubx_parse` validates one
 * frame, and `pc_ubx_stream_feed` demultiplexes UBX frames out of a mixed NMEA+UBX byte stream
 * (handing every non-UBX byte back for an NMEA line assembler). Pure and host-tested; pairs with
 * services/timing_position/nmea0183 for a full GNSS link (send config/poll as UBX, read fixes as either).
 */
#ifndef PC_ENABLE_UBX
#define PC_ENABLE_UBX 0
#endif

/**
 * @brief PTP / IEEE 1588-2008 (PTPv2) message codec + slave clock math (`services/ptp`).
 *
 * The Precision Time Protocol disciplines LAN clocks to sub-microsecond accuracy. `pc_ptp_build_*`
 * / `pc_ptp_parse_*` frame and decode the PTPv2 wire format (34-octet common header, 10-octet
 * 48-bit-seconds+32-bit-ns timestamp, and the Sync / Delay_Req / Follow_Up / Delay_Resp / Announce
 * messages, all big-endian), and `pc_ptp_compute` derives an ordinary-clock slave's offset-from-
 * master and mean-path-delay from the four transfer timestamps. Pure and host-tested; the UDP
 * transport (event port 319, general port 320) and local timestamping are the application's. Example
 * Ptp is an ordinary-clock slave.
 */
#ifndef PC_ENABLE_PTP
#define PC_ENABLE_PTP 0
#endif

/**
 * @brief IO-Link (SDCI, IEC 61131-9) data-link message codec (`services/iolink`).
 *
 * Default off. The point-to-point smart-sensor link's data-link message layer: the M-sequence
 * Control octet (`pc_iol_mc` + decoders), the checksum / type octet of a master message
 * (`pc_iol_ckt`), the checksum / status octet of a device reply (`pc_iol_cks`), and the SDCI message
 * checksum (`pc_iol_checksum6` / `pc_iol_finalize` / `pc_iol_verify`) implemented straight from IO-Link
 * spec v1.1.4 Annex A.1.6 (the 0x52 seed + the 8-to-6-bit compression of equation A.1). Lay
 * the M-sequence / ISDU octets out per your device profile, then finalize / verify with this
 * codec. Pure codec, host-tested. The wire is a UART through an IO-Link transceiver
 * (e.g. MAX14819 / L6360); bridge sensor data onto Wi-Fi.
 */
#ifndef PC_ENABLE_IOLINK
#define PC_ENABLE_IOLINK 0
#endif

/**
 * @brief gRPC-Web message framing (`services/grpcweb`).
 *
 * Default off. A zero-heap length-prefixed frame builder + parser for gRPC-Web, the
 * HTTP/1.1-reachable subset of gRPC (gRPC proper needs HTTP/2). `pc_grpcweb_frame_message`
 * wraps a Protobuf message in the 5-octet `[flags][len BE32]` prefix, `pc_grpcweb_frame_trailer`
 * emits the 0x80 trailers frame (`grpc-status` / `grpc-message`), and `pc_grpcweb_parse` reads
 * one frame back. Wraps the Protobuf codec (`PC_ENABLE_PROTOBUF`) over the shipped
 * HTTP/1.1 server/client. Pure codec, host-tested.
 */
#ifndef PC_ENABLE_GRPC_WEB
#define PC_ENABLE_GRPC_WEB 0
#endif

/**
 * @brief OMA LwM2M TLV codec (`services/lwm2m`).
 *
 * Default off. A zero-heap writer + cursor reader for the LwM2M `application/vnd.oma.lwm2m+tlv`
 * resource encoding (Type / Identifier / Length / Value, 8-/16-bit ids, 0-/8-/16-/24-bit
 * lengths), carried over the shipped CoAP service for device management. Value helpers for
 * shortest-form integers, booleans, strings, and floats. Pure codec, host-tested.
 */
#ifndef PC_ENABLE_LWM2M
#define PC_ENABLE_LWM2M 0
#endif

/**
 * @brief Omron FINS frame codec (`services/fins`).
 *
 * Default off. A zero-heap command/response builder + parser for the Factory Interface
 * Network Service (FINS/UDP): `pc_fins_build_command` / `pc_fins_build_memory_area_read` emit the
 * 10-octet routing header + command code + parameters, and `pc_fins_parse_command` /
 * `pc_fins_parse_response` read them back (the response end code MRES/SRES included). Talks to
 * an Omron PLC over the shipped UDP transport (Udp.client->sendto). Pure codec, host-tested.
 */
#ifndef PC_ENABLE_FINS
#define PC_ENABLE_FINS 0
#endif

/**
 * @brief Omron Host Link (C-mode) frame codec (`services/hostlink`).
 *
 * Default off. A zero-heap ASCII command/response codec for the Omron serial host-link
 * protocol (the RS-232/485 sibling of FINS): `pc_hostlink_build` emits `@UU` + header code +
 * text + FCS + `*`CR, and `pc_hostlink_parse` FCS-validates and splits a frame
 * (`pc_hostlink_end_code` reads a response's end code). FCS is the 8-bit XOR from `@` through
 * the text. Pure codec, host-tested; the serial transport is the application's.
 */
#ifndef PC_ENABLE_HOSTLINK
#define PC_ENABLE_HOSTLINK 0
#endif

/**
 * @brief SCPI / IEEE 488.2 instrument-control codec (`services/scpi`).
 *
 * Default off. A zero-heap codec for the text command language nearly every modern bench
 * instrument speaks (DMMs, scopes, power supplies, function generators, SMUs, loads) over a raw
 * TCP socket on port 5025: a command builder (`pc_scpi_build` joins a `:`-hierarchy header with
 * comma-separated parameters + terminator), the IEEE 488.2 common commands (`*IDN?` / `*RST` /
 * `*CLS` / `*ESR?` / `*STB?` / ...), response parsers (numeric NR1/NR2/NR3, boolean, quoted
 * string, and definite/indefinite arbitrary block `#<n><len><data>` for waveform captures), the
 * status-byte / event-status-register / error-queue model, and a SCPI short/long-form header
 * matcher for dispatching incoming commands. Makes the device a bench-instrument controller or a
 * wireless bridge that fans instrument telemetry into HTTP/MQTT. Pure codec, host-tested; the TCP
 * transport is the application's.
 */
#ifndef PC_ENABLE_SCPI
#define PC_ENABLE_SCPI 0
#endif
/** @brief SCPI error/event queue depth (entries). The SCPI status model requires a queue; when it
 *  overflows the tail entry is replaced with -350 "Queue overflow" per the standard. */
#ifndef PC_SCPI_ERR_QUEUE
#define PC_SCPI_ERR_QUEUE 8
#endif

/**
 * @brief HiSLIP (High-Speed LAN Instrument Protocol) message codec (`services/hislip`).
 *
 * Default off. A zero-heap codec for the IVI Foundation's modern LXI instrument transport (IVI-6.1)
 * on TCP port 4880 - the successor to VXI-11 that carries SCPI at higher throughput over two TCP
 * channels (synchronous + asynchronous). `pc_hislip_build_header` / `pc_hislip_parse_header`
 * frame the fixed 16-byte header ("HS" prologue + message type + control code + 32-bit parameter +
 * 64-bit payload length), with helpers for the Initialize / AsyncInitialize handshake and the
 * Data / DataEND message carrying a SCPI payload + its message id. Pairs with `PC_ENABLE_SCPI`
 * (the payload). Pure codec, host-tested; the two TCP connections are the application's.
 */
#ifndef PC_ENABLE_HISLIP
#define PC_ENABLE_HISLIP 0
#endif

/**
 * @brief VXI-11 (TCP/IP Instrument Protocol) codec (`services/vxi11`).
 *
 * Default off. A zero-heap codec for the legacy LXI instrument transport that predates HiSLIP:
 * VXI-11 over ONC RPC (Sun RPC) with XDR encoding. Provides the reusable XDR / ONC-RPC primitives
 * (record-marking header, CALL / REPLY message framing, AUTH_NONE, length-prefixed opaque/string,
 * 4-byte alignment) plus the DEVICE_CORE procedures - create_link / device_write / device_read /
 * device_readstb / destroy_link (program 0x0607AF v1) - and the portmapper GETPORT call that maps
 * the program to its TCP port. Still the fallback transport for a large installed base of LAN
 * instruments. Pairs with `PC_ENABLE_SCPI` (the payload). Pure codec, host-tested; the TCP
 * connection is the application's.
 */
#ifndef PC_ENABLE_VXI11
#define PC_ENABLE_VXI11 0
#endif

/**
 * @brief GPIB-over-LAN (Prologix-style) controller command codec (`services/gpib`).
 *
 * Default off. A zero-heap codec for the Prologix-compatible `++` command set that drives a
 * legacy IEEE-488 (GPIB) bench of instruments through a Prologix GPIB-Ethernet / GPIB-USB adapter
 * (raw socket on TCP 1234): builders for the control commands (`++addr` / `++mode` / `++read` /
 * `++eoi` / `++eos` / `++spoll` / `++clr` / `++trg` / `++ver` / ...), the data-line escaping (a
 * leading ESC before a CR / LF / ESC / `+` byte in instrument data), and response parsers (the
 * address / serial-poll status byte / version string). Makes the device a bridge into pre-LAN test
 * gear that will never speak SCPI-over-TCP directly. Pure codec, host-tested; the socket / serial
 * link to the adapter is the application's.
 */
#ifndef PC_ENABLE_GPIB
#define PC_ENABLE_GPIB 0
#endif

/**
 * @brief Haas Machine Data Collection (MDC) Q-command codec (`services/haas_mdc`).
 *
 * Default off. A zero-heap codec for the documented Haas Automation MDC protocol - the `?Q` command
 * set a Haas CNC mill / lathe control answers over RS-232 or its Ethernet port: builders for the
 * numbered queries (`?Q100` machine serial number, `?Q101` control software version, `?Q102` model,
 * `?Q104` mode, `?Q300` power-on time, `?Q500` active program + run status + parts counter, and
 * `?Q600 <var>` macro / system-variable read) and a parser for the `>`-wrapped, comma-delimited
 * responses (and the unprompted `DPRNT(...)` lines a running G-code program emits). Makes the device
 * a fixed-BSS CNC data collector that fans machine status into HTTP / MQTT. Pure codec, host-tested;
 * the serial / TCP link to the control is the application's.
 */
#ifndef PC_ENABLE_HAAS_MDC
#define PC_ENABLE_HAAS_MDC 0
#endif

/**
 * @brief PackML / OMAC packaging-machine state model (`services/packml`).
 *
 * The ISA-TR88.00.02 PackML state machine (17 states + the Reset/Start/Stop/Hold/.../Abort/Clear commands)
 * plus the Command/Status/Admin PackTags (current state + unit mode, machine speed, production counters) as
 * a fixed-BSS state engine + owned service. Pure, host-tested; the OPC UA / tag transport is the
 * application's.
 */
#ifndef PC_ENABLE_PACKML
#define PC_ENABLE_PACKML 0
#endif

/**
 * @brief Heidenhain LSV/2 telegram codec (`services/lsv2`).
 *
 * Default off. A zero-heap codec for the LSV/2 protocol Heidenhain TNC controls speak for DNC + data
 * access over serial or Ethernet (LSV/2-over-TCP, port 19000): the telegram framer (a 4-byte
 * big-endian payload-length prefix, a 4-character command / response mnemonic, then the payload) plus
 * typed builders for login / logout (`A_LG` / `A_LO`), the null-terminated-filename file commands
 * (`R_FL` load, `C_FL` send, `C_FD` delete, `C_DC` change dir, `C_DM` / `C_DD` make / delete dir), and
 * the run-info request (`R_RI` with a 2-byte run-info selector: execution state, selected program,
 * override, program state), and readers for the response mnemonics (`T_OK` / `T_FD` and the `T_ER` /
 * `T_BD` two-byte error-class + error-code). A CNC-native southbound source for the common European
 * control alongside `services/focas` / `services/haas_mdc`. Pure codec, host-tested; the serial / TCP
 * link to the control is the application's.
 */
#ifndef PC_ENABLE_LSV2
#define PC_ENABLE_LSV2 0
#endif

/**
 * @brief IKEv2 (RFC 7296) message + payload codec (`services/ikev2`).
 *
 * Default off. A zero-heap builder / parser for the Internet Key Exchange v2 wire format that
 * negotiates IPsec security associations over UDP 500 / 4500 (NAT-T) - tier 1 (the pure framing) of an
 * IKEv2/IPsec stack: the 28-octet IKE header and the generic payload chain (SA -> proposals ->
 * transforms with the key-length attribute, KE, Ni/Nr nonce, IDi/IDr, CERT/CERTREQ, AUTH, N notify, D
 * delete, TSi/TSr traffic selectors, and the SK encrypted-payload envelope). Build / parse into caller
 * buffers only - no sockets and no crypto; the Diffie-Hellman math, the SKEYSEED / SK_* key derivation,
 * the SK AEAD, and the IKE_SA_INIT -> IKE_AUTH state machine are later tiers that reuse the crypto the
 * library already ships. Field layouts verified against RFC 7296 + IANA and cross-checked byte-for-byte
 * against scapy's IKEv2 codec. Pure codec, host-tested; the UDP transport is the application's.
 */
#ifndef PC_ENABLE_IKEV2
#define PC_ENABLE_IKEV2 0
#endif

/**
 * @brief SenML (RFC 8428) measurement-pack builder (`services/senml`).
 *
 * Default off; implies PC_ENABLE_CBOR (the SenML-CBOR form uses the CBOR writer). A
 * zero-heap SenML-JSON + SenML-CBOR encoder over the shipped JSON / CBOR codecs: the caller
 * fills a `SenmlRecord` array (base name/time, name, unit, one value, time) and
 * `pc_senml_json_build` / `pc_senml_build` (any pc_codec) emit the whole pack. Numbers are emitted as
 * integers when integral (so timestamps keep precision), else floats. The standard
 * measurement format for CoAP / LwM2M / HTTP telemetry. Pure codec, host-tested.
 */
#ifndef PC_ENABLE_SENML
#define PC_ENABLE_SENML 0
#endif
// SenML's binary form is CBOR, so it needs the CBOR codec compiled in.
#define PC_NEED_CBOR (PC_ENABLE_CBOR || PC_ENABLE_SENML)

/**
 * @brief Allen-Bradley DF1 full-duplex frame codec (`services/df1`).
 *
 * Default off. A zero-heap framing + DLE byte-stuffing + BCC/CRC codec for the Rockwell
 * serial PLC data-link layer (pub. 1770-6.5.16): `pc_df1_build_frame` wraps application data in
 * `DLE STX ... DLE ETX` with a doubled-DLE escape and a BCC (2's complement of the data sum)
 * or CRC-16 (over the data + ETX, low byte first), and `pc_df1_parse_frame` validates the check
 * and un-stuffs the data. Pure codec, host-tested; the application header is the app's.
 */
#ifndef PC_ENABLE_DF1
#define PC_ENABLE_DF1 0
#endif

/**
 * @brief Siemens SIMATIC serial point-to-point: 3964R link + RK512 telegrams (`services/simatic`).
 *
 * Default off. The pre-Ethernet Siemens PtP link (CP 341/441/524/525): the 3964R byte-oriented,
 * half-duplex link protocol (STX/DLE handshake, DLE-doubling, XOR BCC on the "R" variant, priority
 * arbitration on an STX collision, QVZ/ZVZ timeouts + retry) plus the RK512 computer-link telegrams
 * (SEND / FETCH addressing a DB / flag / I-O area, big-endian words). A pure framing/telegram codec +
 * one owned link-state-machine context; the RS-232 / RS-485 UART is the application's. Host-tested
 * against an independent python 3964R+RK512 reference peer.
 */
#ifndef PC_ENABLE_SIMATIC
#define PC_ENABLE_SIMATIC 0
#endif

/** @brief 3964R block-body buffer size (built/received bytes: DLE-stuffed payload + DLE ETX + BCC). */
#ifndef PC_SIMATIC_BLOCK_MAX
#define PC_SIMATIC_BLOCK_MAX 256
#endif

/** @brief 3964R QVZ (Quittungsverzugszeit): handshake acknowledge-delay timeout, ms. */
#ifndef PC_SIMATIC_QVZ_MS
#define PC_SIMATIC_QVZ_MS 2000
#endif

/** @brief 3964R ZVZ (Zeichenverzugszeit): inter-character timeout while receiving a block, ms. */
#ifndef PC_SIMATIC_ZVZ_MS
#define PC_SIMATIC_ZVZ_MS 200
#endif

/**
 * @brief TPKT (RFC 1006) + COTP (X.224 class 0) frame codec (`services/cotp`).
 *
 * Default off. A zero-heap "ISO transport on TCP" framing codec - the reusable foundation
 * under S7comm and IEC 61850 MMS. `pc_tpkt_build` / `pc_tpkt_parse` handle the 4-octet TPKT
 * envelope; `pc_cotp_build_dt` wraps user data in a Data TPDU, `pc_cotp_build_cr` builds a
 * Connection Request (with the TPDU-size parameter + caller TSAP params), and `pc_cotp_parse`
 * reports the TPDU type and the DT data / CR-CC refs. Pure codec, host-tested.
 */
#ifndef PC_ENABLE_COTP
#define PC_ENABLE_COTP 0
#endif

/**
 * @brief Siemens S7comm PDU codec (`services/s7comm`).
 *
 * Default off. A zero-heap builder + parser for the S7-300/400 communication PDUs carried
 * inside a COTP Data TPDU (PC_ENABLE_COTP) over ISO-on-TCP (port 102): `pc_s7_build_setup`
 * (Setup Communication), `pc_s7_build_read_request` (Read Var, S7-ANY items over DB/I/Q/M),
 * `pc_s7_parse_header`, and `pc_s7_read_next_item` (the response data items, honoring the
 * length-in-bits transport sizes + even-item padding). Constants verified against the
 * Wireshark S7comm dissector. Pure codec, host-tested; wrap the PDU with COTP + TPKT.
 */
#ifndef PC_ENABLE_S7COMM
#define PC_ENABLE_S7COMM 0
#endif

/**
 * @brief Mitsubishi MELSEC MC protocol (binary 3E) codec (`services/melsec`).
 *
 * Default off. A zero-heap batch-read request builder + response parser for MELSEC PLCs over
 * TCP/UDP: `pc_melsec_build_read` emits the binary 3E batch-read (word) frame (little-endian
 * fields, subheader 0x5000, command 0x0401, the device code + 24-bit head device + point
 * count), and `pc_melsec_parse_response` validates the 0xD000 response and reports the end code
 * + the read data. Frame layout + device codes verified against a third-party MC impl. Pure
 * codec, host-tested. Completes the major-vendor PLC read set (FINS / Host Link / DF1 / S7).
 */
#ifndef PC_ENABLE_MELSEC
#define PC_ENABLE_MELSEC 0
#endif

/**
 * @brief Beckhoff ADS / AMS protocol codec (`services/ads`).
 *
 * Default off. A zero-heap builder + parser for the TwinCAT PC-based-control protocol over TCP
 * 48898: `pc_ads_build_*` emit complete AMS/TCP + AMS-header frames (little-endian, target-before-
 * source addressing, cmd id + state flags + cbData + invoke id) for ReadDeviceInfo / Read /
 * Write / ReadWrite / ReadState / WriteControl / Add+DeleteNotification, and `pc_ads_parse_*` decode
 * the responses (including the DeviceNotification stamp/sample stream). ReadWrite drives symbol-
 * by-name access (name -> handle via index group 0xF003, value via 0xF005). AMS header layout +
 * command ids verified against the Beckhoff InfoSys spec. Pure codec, host-tested; the caller
 * owns the TCP socket and the AMS route on the target router.
 */
#ifndef PC_ENABLE_ADS
#define PC_ENABLE_ADS 0
#endif

/**
 * @brief FANUC FOCAS Ethernet protocol codec (`services/focas`).
 *
 * Default off. A zero-heap builder + parser for the FANUC CNC data protocol over TCP 8193:
 * `pc_focas_build_*` emit the complete on-wire frames (a 10-octet big-endian envelope + payload) for
 * the open/close handshake and the documented read functions (SysInfo, alarm status, CNC
 * parameters, macro variables, position/axis data, actual feed / spindle), and `pc_focas_parse_*`
 * decode the responses (echoed selector + status + data), including the ODBSYS SysInfo layout and
 * the FANUC 8-octet `data / base^exp` value encoding. Frame layout, selector encoding, and value
 * decoding reverse-engineered by and cross-checked against diohpix/pyfanuc. Pure codec, host-
 * tested; the caller owns the TCP socket and drives the open -> command -> close sequence.
 */
#ifndef PC_ENABLE_FOCAS
#define PC_ENABLE_FOCAS 0
#endif

/**
 * @brief FANUC Stream Motion (option J519) UDP codec (`services/fanuc_j519`).
 *
 * Default off. The robot counterpart to `PC_ENABLE_FOCAS` (which speaks to FANUC CNCs): Stream
 * Motion is the real-time external motion interface on R-30iB / R-30iA robot controllers, where an
 * external controller streams joint or Cartesian setpoints over UDP 60015 at the controller's
 * interpolation rate (typically 125 / 250 Hz) and the robot answers each command with its measured
 * Cartesian pose, joint pose, and per-axis motor current. Zero-heap and symmetric: `pc_j519_build_*`
 * emit the Start / Motion / Stop / Request packets and `pc_j519_parse_*` decode the Robot Status /
 * Ack replies, plus the mirrored pair so the device can stand in as a robot simulator for bench work.
 * Every field is LITTLE-endian (unlike FOCAS) and floats are IEEE-754 binary32. The packet type word
 * is reused across directions (0 = Start or Robot Status, 3 = Request or Ack), so the direction is in
 * the function name and each parser requires its packet's exact length. Field offsets, sizes, type
 * codes, I/O-type and threshold enumerations, and the status bits were taken from the public Wireshark
 * dissector fanuc-stream-motion/packet-fanuc-stream-motion-j519 - no FANUC source or header is used.
 * Pure codec, host-tested; the caller owns the UDP socket and the real-time cadence.
 */
#ifndef PC_ENABLE_FANUC_J519
#define PC_ENABLE_FANUC_J519 0
#endif

/**
 * @brief BACnet/IP BVLC + NPDU codec (`services/bacnet`).
 *
 * Default off. A zero-heap framing codec for the ASHRAE 135 building-automation network
 * layer over UDP (47808): `pc_bvlc_build` / `pc_bvlc_parse` handle the BVLC envelope (type 0x81,
 * function, length), and `pc_npdu_build` / `pc_npdu_parse` handle the NPDU (version + NPCI control
 * + optional DNET/DADR destination addressing + hop count) and slice the APDU. The APDU
 * (application-layer services / object model) layers on top. Pure codec, host-tested.
 */
#ifndef PC_ENABLE_BACNET
#define PC_ENABLE_BACNET 0
#endif

/**
 * @brief EtherNet/IP encapsulation codec (`services/enip`).
 *
 * Default off. A zero-heap builder + parser for the ODVA EtherNet/IP encapsulation layer
 * (TCP/UDP 44818) that carries CIP: `pc_eip_build` / `pc_eip_parse` handle the 24-octet header
 * (little-endian command / length / session handle / status / sender context / options),
 * `pc_eip_build_register_session` opens a session, and `pc_eip_build_send_rr_data` /
 * `pc_eip_parse_send_rr_data` wrap + unwrap a CIP message as an unconnected message (Common
 * Packet Format: Null Address + Unconnected Data items). Commands + CPF item types verified
 * against the Wireshark ENIP dissector. Pure codec, host-tested; the CIP message is the app's.
 */
#ifndef PC_ENABLE_ENIP
#define PC_ENABLE_ENIP 0
#endif

/**
 * @brief AMQP 0-9-1 frame codec (`services/amqp`).
 *
 * Default off. A zero-heap frame builder + parser for the RabbitMQ wire protocol so a device
 * can be an AMQP client: `pc_amqp_protocol_header` (the `"AMQP" 0 0 9 1` preamble),
 * `pc_amqp_build_frame` / `pc_amqp_parse_frame` (type + channel + size + payload + the 0xCE
 * frame-end), `pc_amqp_build_method` / `pc_amqp_parse_method` (a METHOD frame's class-id /
 * method-id / arguments), and `pc_amqp_build_heartbeat`. Pure codec, host-tested; the method
 * arguments and the connection are the application's. Rides the outbound client transport.
 */
#ifndef PC_ENABLE_AMQP
#define PC_ENABLE_AMQP 0
#endif

/**
 * @brief CIP (Common Industrial Protocol) message codec (`services/cip`).
 *
 * Default off. A zero-heap CIP request builder + response parser for the message that rides
 * inside an EtherNet/IP Unconnected Data item (PC_ENABLE_ENIP): `pc_cip_build_epath` (the
 * class/instance/attribute logical-segment EPATH), `pc_cip_build_request` /
 * `pc_cip_build_get_attr_single`, and `pc_cip_parse_response` (service / general status / data).
 * Service codes + the logical-segment encoding verified against the Wireshark CIP dissector.
 * Pure codec, host-tested; wrap the request with `pc_eip_build_send_rr_data` for a working read.
 */
#ifndef PC_ENABLE_CIP
#define PC_ENABLE_CIP 0
#endif

/**
 * @brief NATS client protocol codec (`services/nats`).
 *
 * Default off. A zero-heap builder + parser for the text-based NATS pub/sub protocol so a
 * device can be a NATS client: `pc_nats_build_connect` / `pc_nats_build_pub` / `pc_nats_build_sub` /
 * `pc_nats_build_unsub` / `pc_nats_build_ping` / `pc_nats_build_pong`, and `pc_nats_parse` which decodes
 * an inbound MSG / INFO / PING / PONG / +OK / -ERR (MSG yields subject / sid / reply-to /
 * payload). Line-oriented (CRLF), space-delimited; only PUB and MSG carry a payload. Pure
 * codec, host-tested; rides the outbound client transport.
 */
#ifndef PC_ENABLE_NATS
#define PC_ENABLE_NATS 0
#endif

/**
 * @brief HAProxy PROXY protocol codec (`services/proxy_protocol`).
 *
 * Default off. A zero-heap parser + builder for the PROXY protocol header a load balancer /
 * reverse proxy prepends, so the server recovers the real client IPv4 behind one.
 * `proxy_parse` detects + decodes a v1 (text `PROXY TCP4 ...`) or v2 (binary signature +
 * ver_cmd / fam / address block) header and reports the bytes to skip; `proxy_v1_build` /
 * `proxy_v2_build` emit a TCP/IPv4 header. Pure codec, host-tested; the application feeds it
 * the first bytes of an accepted connection.
 */
#ifndef PC_ENABLE_PROXY_PROTOCOL
#define PC_ENABLE_PROXY_PROTOCOL 0
#endif

/**
 * @brief Sparkplug B payload + topic codec (`services/sparkplug`).
 *
 * Default off; implies PC_ENABLE_PROTOBUF (the payload is a Protobuf message). A zero-heap
 * builder for the Eclipse Sparkplug B industrial-IoT MQTT payload (`pc_spb_build_payload` /
 * `pc_spb_build_metric`, over the protobuf codec) and its topic namespace (`pc_spb_build_topic`,
 * `spBv1.0/group/type/node[/device]`). Field numbers + datatype codes verified against the
 * Eclipse Tahu sparkplug_b.proto. Pure codec, host-tested; publish it with the MQTT client.
 */
#ifndef PC_ENABLE_SPARKPLUG
#define PC_ENABLE_SPARKPLUG 0
#endif
// Sparkplug B payloads are protobuf messages, so it needs the protobuf codec compiled in.
#define PC_NEED_PROTOBUF (PC_ENABLE_PROTOBUF || PC_ENABLE_SPARKPLUG)

/** @brief Max serialized size of one Sparkplug B metric submessage (stack temp, bytes). */
#ifndef PC_SPB_METRIC_MAX
#define PC_SPB_METRIC_MAX 256
#endif

/**
 * @brief Opt-in Modbus master codec + register scanner (PC_ENABLE_MODBUS_MASTER).
 *
 * Default off. services/fieldbus/modbus/pc_modbus_master builds Modbus TCP read-request ADUs
 * and parses the responses (register values or exception), so an app can poll /
 * auto-discover a slave's registers. Pure and host-tested as a full round-trip
 * against the slave codec (pc_modbus_process_adu); the actual send is the app's TCP.
 */
#ifndef PC_ENABLE_MODBUS_MASTER
#define PC_ENABLE_MODBUS_MASTER 0
#endif

/** @brief Number of Modbus coils (FC 1/5/15), single-bit R/W (BSS, bit-packed). */
#ifndef PC_MODBUS_COILS
#define PC_MODBUS_COILS 64
#endif

/** @brief Number of Modbus discrete inputs (FC 2), single-bit read-only (BSS, bit-packed). */
#ifndef PC_MODBUS_DISCRETE_INPUTS
#define PC_MODBUS_DISCRETE_INPUTS 64
#endif

/** @brief Number of Modbus holding registers (FC 3/6/16), 16-bit R/W (BSS). */
#ifndef PC_MODBUS_HOLDING_REGS
#define PC_MODBUS_HOLDING_REGS 64
#endif

/** @brief Number of Modbus input registers (FC 4), 16-bit read-only (BSS). */
#ifndef PC_MODBUS_INPUT_REGS
#define PC_MODBUS_INPUT_REGS 64
#endif

/**
 * @brief TLS (HTTPS/WSS) via mbedTLS with a static memory pool (ESP32-only).
 *
 * When set, the server can accept TLS connections using mbedTLS configured with
 * MBEDTLS_MEMORY_BUFFER_ALLOC_C over a fixed BSS arena (PC_TLS_ARENA_SIZE) -
 * no system heap, so the determinism guarantee is preserved. The TLS engine is
 * compiled only on Arduino/ESP32 (mbedTLS is not part of the native build).
 * Default off.
 */
#ifndef PC_ENABLE_TLS
#define PC_ENABLE_TLS 0
#endif

/** @brief Maximum simultaneous TLS connections (each holds mbedTLS record buffers). */
#ifndef MAX_TLS_CONNS
#define MAX_TLS_CONNS 1
#endif

/**
 * @brief TLS session resumption via RFC 5077 session tickets (requires PC_ENABLE_TLS).
 *
 * Default off. When set, the TLS 1.2 server issues encrypted session tickets and
 * accepts them on reconnect, so a returning client completes an abbreviated
 * handshake (no certificate or full key exchange) - much faster and far less CPU
 * than the ~RSA/ECDHE full handshake. Resumption is stateless: the session state
 * lives in the client's ticket, sealed with a server-held key, so there is no
 * growing per-session cache (the determinism / zero-heap-growth guarantee holds;
 * only a small fixed ticket key and a little arena headroom are added). The ticket
 * key rotates automatically on the PC_TLS_TICKET_LIFETIME_S schedule. Needs the
 * mbedTLS build to provide MBEDTLS_SSL_TICKET_C (stock arduino-esp32 does).
 */
#ifndef PC_ENABLE_TLS_RESUMPTION
#define PC_ENABLE_TLS_RESUMPTION 0
#endif

/** @brief Session-ticket lifetime / key-rotation period in seconds (see PC_ENABLE_TLS_RESUMPTION). */
#ifndef PC_TLS_TICKET_LIFETIME_S
#define PC_TLS_TICKET_LIFETIME_S 86400
#endif

/**
 * @brief Mutual TLS - require and verify a client certificate (mTLS).
 *
 * Default off. When set (requires PC_ENABLE_TLS), the server can be given a
 * trust-anchor CA via tls_require_client_cert(): the TLS handshake
 * then demands a client certificate chaining to that CA
 * (MBEDTLS_SSL_VERIFY_REQUIRED) and aborts the connection if the client presents
 * none or an untrusted one. The verified peer's subject DN is available to
 * handlers via tls_client_subject(). Strong transport-level client
 * authentication with no passwords.
 */
#ifndef PC_ENABLE_MTLS
#define PC_ENABLE_MTLS 0
#endif

/** @brief Maximum length of a verified mTLS peer subject DN string (incl. NUL). */
#ifndef PC_MTLS_SUBJECT_MAX
#define PC_MTLS_SUBJECT_MAX 128
#endif

/**
 * @brief SNMP agent (v1/v2c, + v3 USM when PC_ENABLE_SNMP_V3) over lwIP UDP.
 *
 * Zero-heap ASN.1 BER codec + a fixed MIB table on UDP/161. Default off. The BER
 * codec itself is gated by this flag and is otherwise unit-tested standalone
 * (env:native_snmp).
 */
#ifndef PC_ENABLE_SNMP
#define PC_ENABLE_SNMP 0
#endif

/** @brief Add SNMPv3 USM (auth via HMAC-SHA, privacy via AES-128-CFB). Default off. */
#ifndef PC_ENABLE_SNMP_V3
#define PC_ENABLE_SNMP_V3 0
#endif

/**
 * @brief Outbound SNMP notifications - traps and informs (requires PC_ENABLE_SNMP).
 *
 * Default off. When set, src/services/net/snmp/pc_snmp_notify.h sends SNMPv2c (and, with
 * PC_ENABLE_SNMP_V3, SNMPv3 USM) Trap / InformRequest PDUs to a manager over
 * UDP - so the agent can push alerts instead of only answering polls. Reuses the
 * BER codec and the transport-layer UDP service; the PDU builder is host-testable.
 */
#ifndef PC_ENABLE_SNMP_TRAP
#define PC_ENABLE_SNMP_TRAP 0
#endif

/** @brief Maximum extra variable-bindings (beyond sysUpTime/snmpTrapOID) in one notification. */
#ifndef PC_SNMP_TRAP_MAX_VARBINDS
#define PC_SNMP_TRAP_MAX_VARBINDS 8
#endif

/** @brief Static datagram buffer for an outbound SNMP notification, bytes. */
#ifndef PC_SNMP_TRAP_BUF_SIZE
#define PC_SNMP_TRAP_BUF_SIZE 1024
#endif

/** @brief Maximum sub-identifiers (arcs) in an SNMP object identifier. */
#ifndef SNMP_MAX_OID_LEN
#define SNMP_MAX_OID_LEN 32
#endif

/**
 * @brief Maximum registered MIB objects (the agent's fixed OID table).
 *
 * Each entry holds its OID, a value descriptor, and optional get/set callbacks
 * (see src/services/net/snmp/pc_snmp_agent.h). The table lives in BSS; entries are
 * scanned linearly (small table) and need not be registered in OID order.
 */
#ifndef SNMP_MAX_MIB_ENTRIES
#define SNMP_MAX_MIB_ENTRIES 16
#endif

/**
 * @brief Maximum variable bindings the agent will emit in one response.
 *
 * Bounds GetBulk expansion (max-repetitions is clamped so the total response
 * varbind count never exceeds this) and the per-request decode scratch.
 */
#ifndef SNMP_MAX_VARBINDS
#define SNMP_MAX_VARBINDS 16
#endif

/**
 * @brief Static request/response datagram buffers for the SNMP UDP agent.
 *
 * Two buffers of this size live in BSS (one in, one out) - no heap. 484 is the
 * RFC 1157 minimum maximum message size; the default holds a one-frame UDP
 * payload so GetBulk walks fit without IP fragmentation.
 */
#ifndef SNMP_MSG_BUF_SIZE
#define SNMP_MSG_BUF_SIZE 1472
#endif

/** @brief Maximum SNMP community-string length (including null terminator). */
#ifndef SNMP_COMMUNITY_MAX
#define SNMP_COMMUNITY_MAX 32
#endif

/** @brief Default read-only community (overridable at runtime via pc_snmp_agent_init). Deployments
 *  SHOULD change this from the RFC-1157 well-known "public" for anything but a closed network. */
#ifndef PC_SNMP_DEFAULT_RO_COMMUNITY
#define PC_SNMP_DEFAULT_RO_COMMUNITY "public"
#endif

/** @brief Maximum SNMPv3 USM user-name length (including null terminator). */
#ifndef SNMP_V3_USER_MAX
#define SNMP_V3_USER_MAX 32
#endif

/** @brief Maximum SNMPv3 authoritative engine-ID length in bytes (RFC 3411 allows 5..32). */
#ifndef SNMP_V3_ENGINEID_MAX
#define SNMP_V3_ENGINEID_MAX 32
#endif

// ---------------------------------------------------------------------------
// CoAP server sizing constants  (PC_ENABLE_COAP must be 1)
// ---------------------------------------------------------------------------

/**
 * @brief CoAP server (RFC 7252) over UDP/5683.
 *
 * A zero-heap Constrained Application Protocol endpoint: a fixed resource table
 * dispatched against the request's Uri-Path, with a pure host-testable message
 * codec (parse/build) and an ESP32 UDP binding via the transport-layer UDP
 * service. Default off; the codec is otherwise unit-tested standalone
 * (env:native_coap).
 */
#ifndef PC_ENABLE_COAP
#define PC_ENABLE_COAP 0
#endif

/**
 * @brief CoAP resource observation - RFC 7641 (requires PC_ENABLE_COAP).
 *
 * Default off. When set, a client GET with the Observe option registers as an
 * observer of a resource; the application calls pc_coap_notify(path) to push the
 * resource's current representation to every observer (a CoAP notification from
 * the server port with an increasing Observe sequence). Observers are dropped on
 * a deregister GET, a client RST, or send failure.
 */
#ifndef PC_ENABLE_COAP_OBSERVE
#define PC_ENABLE_COAP_OBSERVE 0
#endif

/** @brief Maximum simultaneous CoAP observers (one slot per observed resource per client). */
#ifndef PC_COAP_MAX_OBSERVERS
#define PC_COAP_MAX_OBSERVERS 4
#endif

/**
 * @brief CoAP message de-duplication cache size (RFC 7252 sec 4.5). A Confirmable request the server has
 *        already answered is recognized by its (source endpoint, Message-ID) and re-answered with the
 *        cached response WITHOUT re-running the handler - so a client's CON retransmission cannot execute
 *        a non-idempotent request (POST/PUT/DELETE) twice. Set to 0 to compile the dedup cache out.
 */
#ifndef PC_COAP_DEDUP_ENTRIES
#define PC_COAP_DEDUP_ENTRIES 4
#endif

/** @brief Largest cached response the dedup cache retains per entry; a bigger response is not cached (a
 *  retransmission re-processes it, fine for the idempotent GET whose block-wise reply exceeds this). */
#ifndef PC_COAP_DEDUP_RESP_MAX
#define PC_COAP_DEDUP_RESP_MAX 256
#endif

/** @brief How long (ms) a dedup entry stays fresh - RFC 7252 EXCHANGE_LIFETIME (~247 s) by default, past
 *  which a repeat Message-ID is treated as a new exchange. */
#ifndef PC_COAP_DEDUP_LIFETIME_MS
#define PC_COAP_DEDUP_LIFETIME_MS 247000u
#endif

/**
 * @brief CoAP block-wise transfer - RFC 7959 (requires PC_ENABLE_COAP).
 *
 * Default off. When set, the server understands the Block2 (descriptive,
 * responses) and Block1 (control, request uploads) options:
 *  - Block2: a representation larger than one block, or any GET that carries a
 *    Block2 option, is served one block at a time. A constrained client requests
 *    a small block size (SZX) and pages through with ascending block numbers; the
 *    server re-renders the (idempotent) resource and slices out the asked-for
 *    block, setting the More bit until the last.
 *  - Block1: a POST/PUT payload larger than one block is reassembled into a
 *    single BSS buffer. Each non-final block is acknowledged 2.31 Continue; the
 *    final block dispatches the handler with the whole reassembled payload.
 *
 * One block-wise transfer is reassembled at a time (deterministic, single
 * buffer); an out-of-order or oversized block yields 4.08 / 4.13. Size1/Size2
 * options and the /.well-known/core listing are out of scope.
 */
#ifndef PC_ENABLE_COAP_BLOCK
#define PC_ENABLE_COAP_BLOCK 0
#endif

/** @brief Largest block-size exponent (SZX) the server will use: block size = 2^(SZX+4) bytes, SZX 0..6 (16..1024). */
#ifndef PC_COAP_BLOCK_SZX_MAX
#define PC_COAP_BLOCK_SZX_MAX 6
#endif

/**
 * @brief Reassembly buffer for a block-wise (Block1) request upload, in bytes.
 *
 * One buffer of this size lives in BSS only when PC_ENABLE_COAP_BLOCK is set.
 * It bounds the largest payload a chunked POST/PUT can deliver to a handler.
 */
#ifndef PC_COAP_BLOCK1_MAX
#define PC_COAP_BLOCK1_MAX 1024
#endif

/**
 * @brief Maximum registered CoAP resources (the server's fixed routing table).
 *
 * Each entry holds a path pointer, an allowed-methods bitmask, and a handler.
 * The table lives in BSS and is scanned linearly (small table).
 */
#ifndef PC_COAP_MAX_RESOURCES
#define PC_COAP_MAX_RESOURCES 8
#endif

/** @brief Maximum reconstructed Uri-Path length, including separators and the leading '/'. */
#ifndef PC_COAP_MAX_PATH
#define PC_COAP_MAX_PATH 64
#endif

/** @brief Maximum reconstructed Uri-Query length (segments joined by '&'). */
#ifndef PC_COAP_MAX_QUERY
#define PC_COAP_MAX_QUERY 64
#endif

/**
 * @brief Maximum CoAP request/response payload in bytes.
 *
 * Sizes the static scratch a handler writes its response body into and bounds
 * the request payload handed to it. One buffer of this size lives in BSS.
 */
#ifndef PC_COAP_MAX_PAYLOAD
#define PC_COAP_MAX_PAYLOAD 256
#endif

/**
 * @brief Static response-datagram buffer for the CoAP UDP server.
 *
 * One buffer of this size lives in BSS (the request is transport-owned). Must
 * hold a 4-byte header + token (<=8) + the Content-Format option + a 0xFF marker
 * + PC_COAP_MAX_PAYLOAD bytes. When block-wise transfer is enabled it must
 * also hold one full block (2^(PC_COAP_BLOCK_SZX_MAX+4) bytes) + option
 * overhead, so the default grows accordingly.
 */
#ifndef PC_COAP_MSG_BUF_SIZE
#if PC_ENABLE_COAP_BLOCK
#define PC_COAP_MSG_BUF_SIZE 1152
#else
#define PC_COAP_MSG_BUF_SIZE 512
#endif
#endif

/** @brief Default UDP port the CoAP observe transport notifies from (IANA well-known 5683). */
#ifndef PC_COAP_OBSERVE_PORT
#define PC_COAP_OBSERVE_PORT 5683
#endif

/**
 * @brief Bytes of the static BSS arena mbedTLS allocates from (PC_ENABLE_TLS).
 *
 * All mbedTLS allocations (per-connection record buffers, handshake temporaries,
 * cert/key parsing) are served from this fixed arena via a custom allocator
 * installed with mbedtls_platform_set_calloc_free() - never the system heap. Must
 * cover the worst-case handshake peak for MAX_TLS_CONNS; if undersized the
 * handshake fails cleanly (no corruption). Measured peak for ONE ECDSA P-256
 * connection on Arduino-esp32 (16 KB IN + 16 KB OUT records) is ~41.5 KB, so the
 * default leaves a small margin. An RSA cert/larger chain needs more; query the
 * live peak via pc_tls_arena_peak(). NOTE: a second concurrent TLS connection
 * roughly doubles the record-buffer cost (~32 KB more), which overflows the
 * static DRAM budget - keep MAX_TLS_CONNS at 1 unless you shrink the IDF record
 * sizes (CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN, needs an ESP-IDF build).
 */
#ifndef PC_TLS_ARENA_SIZE
// The arena is SHARED across all TLS connections, so it must cover the peak for MAX_TLS_CONNS: ~48 KB
// for the first handshake (ECDSA P-256, 16 KB IN + 16 KB OUT records + temporaries) plus ~32 KB of
// record buffers per additional concurrent connection. Auto-derive so a profile that raises
// MAX_TLS_CONNS (a PSRAM board, via PC_TLS_ARENA_IN_PSRAM) grows the arena to match instead of
// silently starving the second handshake. MAX_TLS_CONNS == 1 keeps the historical 49152.
#define PC_TLS_ARENA_SIZE (49152 + (MAX_TLS_CONNS - 1) * 32768)
#endif

/**
 * @brief Place the TLS arena in external PSRAM instead of internal DRAM (ESP32).
 *
 * The internal static-DRAM ceiling (`dram0_0_seg`) is only ~122 KB, so a single
 * ~48 KB arena already uses a large slice and a second concurrent connection
 * (MAX_TLS_CONNS > 1) overflows it. On a board with PSRAM, set this to 1 to move
 * the arena to external RAM via `EXT_RAM_BSS_ATTR` / `EXT_RAM_ATTR`, freeing the
 * whole `PC_TLS_ARENA_SIZE` back to internal DRAM so many connections fit.
 * Requires `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` (and PSRAM enabled) in the
 * ESP-IDF/PlatformIO config; without it the attribute is a no-op and the arena
 * stays in DRAM (safe fallback). No effect on the native host build.
 */
#ifndef PC_TLS_ARENA_IN_PSRAM
#define PC_TLS_ARENA_IN_PSRAM 0
#endif

/**
 * @brief Cap TLS records via the Maximum Fragment Length extension (RFC 6066).
 *
 * 0 (default) leaves the 16 KB TLS record ceiling. Set to 512, 1024, 2048, or 4096
 * to negotiate a smaller maximum record. On a mbedTLS build with variable-length
 * record buffers this shrinks the per-connection arena footprint (so more concurrent
 * connections fit); on a fixed-buffer build it still bounds the on-wire record size
 * (bandwidth / latency on a constrained link) and honors a client's MFL request.
 * Applied to both the server and the outbound client config. Needs an mbedTLS build
 * with `MBEDTLS_SSL_MAX_FRAGMENT_LENGTH` (else it is a no-op).
 */
#ifndef PC_TLS_MAX_FRAG_LEN
#define PC_TLS_MAX_FRAG_LEN 0
#endif

/**
 * @brief Lead the ECDHE curve/group preference with secp256r1 (P-256) instead of x25519.
 *
 * PER-VARIANT by default, because the ECC silicon differs wildly between dies. On a chip with a
 * hardware NIST-ECC accelerator (`PC_HW_ECC` = 1: ESP32-P4/C5/C6/C61/H2/H4/... where mbedTLS routes
 * P-256 through the HW via `ecc_alt`), P-256 is dramatically faster than x25519, which stays software
 * (measured on an ESP32-P4: P-256 ECDHE ~10 ms vs x25519 ~132 ms, and the full TLS handshake ~29 ms
 * vs ~160 ms - a 5.5x win). On a chip WITHOUT ECC HW (`PC_HW_ECC` = 0: ESP32-S3/S2/classic), both
 * curves are software and near-identical in the full handshake, so x25519 (the security-preferred
 * modern default) leads and this stays 0 (the S3 order is unchanged).
 *
 * So the default tracks `PC_HW_ECC` - the profile's assertion that the die has NIST-ECC HW - and is
 * overridable: force `-DPC_TLS_ECDHE_PREFER_P256=0` to mandate x25519-first even on an ECC-HW chip
 * (a deployment policy choice), or `=1` to prefer P-256 on a chip whose profile has not flagged HW ECC.
 * This only reorders PREFERENCE; every curve stays enabled, so a peer that offers just one still
 * connects. Applied to both the server and outbound-client configs (tls.cpp `tls_apply_curve_pref`).
 */
#ifndef PC_TLS_ECDHE_PREFER_P256
#define PC_TLS_ECDHE_PREFER_P256 PC_HW_ECC
#endif

/**
 * @brief Acknowledge that a MAX_TLS_CONNS > 1 build has been sized to fit.
 *
 * The whole TLS arena is static `.bss` and the internal `dram0_0_seg` ceiling is only
 * ~122 KB, so a second concurrent connection's arena overflows it on a stock build.
 * A validation guard (bottom of this file) therefore rejects MAX_TLS_CONNS > 1 unless
 * you have taken one of the paths in docs/KNOWN_LIMITATIONS.md - move the arena to
 * PSRAM (`PC_TLS_ARENA_IN_PSRAM`, which satisfies the guard on its own), shrink the
 * mbedTLS records in a custom ESP-IDF build, or reclaim internal DRAM - and then set
 * this to 1 to confirm the build was sized deliberately.
 */
#ifndef PC_TLS_ACK_MULTI_CONN_DRAM
#define PC_TLS_ACK_MULTI_CONN_DRAM 0
#endif

// ---------------------------------------------------------------------------
// Optional network services (ESP32-only thin wrappers; each default-off so it
// costs no code/RAM/flash unless explicitly enabled).
// ---------------------------------------------------------------------------

/** @brief mDNS / DNS-SD advertisement (`name.local` + `_http._tcp`) via ESPmDNS. */
#ifndef PC_ENABLE_MDNS
#define PC_ENABLE_MDNS 0
#endif

/** @brief SNTP wall-clock time sync via the ESP-IDF SNTP client. */
#ifndef PC_ENABLE_NTP
#define PC_ENABLE_NTP 0
#endif

/**
 * @brief NTP/SNTP time server (RFC 5905 / RFC 4330 server mode) on UDP/123 (services/pc_ntp_server).
 *
 * Turns the device into a local time source: it answers client NTP requests from its own
 * clock (`pc_time_now()` + the `pc_millis()` sub-second fraction), so an offline or
 * air-gapped LAN can keep its devices in sync without reaching the public NTP pool. The
 * 48-byte response codec is pure and host-tested; the wire binding is the transport UDP
 * service. Get the device's own time first (e.g. PC_ENABLE_NTP upstream, an RTC, or GPS
 * via a time source) - when it has none, the server stays silent rather than serve bad time.
 */
#ifndef PC_ENABLE_NTP_SERVER
#define PC_ENABLE_NTP_SERVER 0
#endif

/** @brief Stratum the NTP server advertises (distance from a reference clock; 1-15). */
#ifndef PC_NTP_SERVER_STRATUM
#define PC_NTP_SERVER_STRATUM 3
#endif

/**
 * @brief Authoritative DNS server (services/net/dns_server) on UDP/53.
 *
 * Default off. Resolves a small fixed table of `name -> IPv4` A records you register with
 * pc_dns_server_add(), so devices on an offline / air-gapped LAN can use names instead of raw
 * IPs (a companion to the NTP server for offline infrastructure). Answers A/IN queries from
 * the table, returns NXDOMAIN for unknown names, and ignores other query types. The response
 * builder is pure and host-tested; the wire binding is the transport UDP service. This is a
 * general resolver, distinct from the provisioning captive-portal DNS (which answers every
 * query with the softAP IP) - do not enable both (they both bind :53).
 */
#ifndef PC_ENABLE_DNS_SERVER
#define PC_ENABLE_DNS_SERVER 0
#endif

/** @brief Max A records in the DNS server's fixed table. */
#ifndef PC_DNS_SERVER_MAX_RECORDS
#define PC_DNS_SERVER_MAX_RECORDS 8
#endif

/** @brief TTL (seconds) the DNS server puts on its answers. */
#ifndef PC_DNS_SERVER_TTL
#define PC_DNS_SERVER_TTL 60
#endif

/** @brief Max length of a queried/stored DNS name (bytes, incl NUL). */
#ifndef PC_DNS_NAME_MAX
#define PC_DNS_NAME_MAX 128
#endif

/**
 * @brief Auto-inject a `Date` response header (RFC 7231 7.1.1.2) when a wall-clock
 *        time is available.
 *
 * Default off: a clock-less device must not emit a wrong `Date`, and most embedded
 * responses do not need one, so it stays off the hot path. When set, every dynamic
 * response carries `Date: <IMF-fixdate>` - but only once a real time exists; before a
 * source has valid time it is silently omitted (still correct for a clock-less boot).
 *
 * The time is taken from the multi-source registry (any enabled NTP / GPS / RTC / ...
 * by priority) when PC_ENABLE_TIME_SOURCE is set - register your sources with
 * pc_time_source_add() (pc_rtc_time_source, pc_ntp_time_source, ...). Otherwise it comes
 * straight from NTP (pc_ntp_http_date). Needs at least one such time source to emit.
 */
#ifndef PC_HTTP_EMIT_DATE
#define PC_HTTP_EMIT_DATE 0
#endif

/**
 * @brief Multi-source time fallback (NTP / RTC / GPS / ... by priority).
 *
 * When set, src/services/timing_position/time_source/time_source.h provides a small registry of
 * user-defined time sources, each a callback returning Unix epoch seconds (0 when
 * that source has no valid time). pc_time_now() queries them in priority order
 * (lowest value first) and returns the first valid result, so the device falls
 * back automatically when its preferred clock is unavailable. Pure and zero-heap
 * (a fixed source table); host-testable. Default off.
 */
#ifndef PC_ENABLE_TIME_SOURCE
#define PC_ENABLE_TIME_SOURCE 0
#endif

/** @brief Maximum registered time sources (PC_ENABLE_TIME_SOURCE). */
#ifndef PC_TIME_SOURCE_MAX
#define PC_TIME_SOURCE_MAX 4
#endif

/**
 * @brief Shared I2C bus pins for the sensor / peripheral drivers (RTC, SHT3x, MPR121, ADS1115,
 * INA219, PCA9685). All of them share one bus via pc_i2c_begin() (services/peripherals/i2c.h), so
 * this is the single place to move it. The default -1 uses the platform's default pins (GPIO 21
 * SDA / 22 SCL on the classic ESP32). Set both to free GPIOs when those pins are taken - most
 * importantly a **wired-Ethernet PHY**: the LAN8720 RMII uses GPIO 21 (TX_EN) and GPIO 22
 * (TXD1) on the classic ESP32 (WROOM/WROVER) and the ESP32-P4 (which have the RMII EMAC), so
 * with that Ethernet on, move the I2C bus off them (e.g. 32 / 33). The ESP32-S3/C3 have no RMII
 * MAC and use an SPI Ethernet (W5500) instead - relocate the bus off whatever SPI pins that
 * uses. UART peripherals (LD2410) take their RX/TX pins at pc_ld2410_begin(), so remap those too.
 */
#ifndef PC_I2C_SDA_PIN
#define PC_I2C_SDA_PIN -1
#endif
#ifndef PC_I2C_SCL_PIN
#define PC_I2C_SCL_PIN -1
#endif

/**
 * @brief Shared SPI bus pins for the peripheral drivers, the same way the I2C pins above are
 * shared. The default -1 uses the platform's default pins for its VSPI/HSPI host. Set them when
 * those pins are taken, most often by a W5500 SPI Ethernet on a part with no RMII MAC (the
 * ESP32-S3 / C3), which drives this same bus.
 */
#ifndef PC_SPI_MOSI_PIN
#define PC_SPI_MOSI_PIN -1
#endif
#ifndef PC_SPI_MISO_PIN
#define PC_SPI_MISO_PIN -1
#endif
#ifndef PC_SPI_SCLK_PIN
#define PC_SPI_SCLK_PIN -1
#endif

/**
 * @brief I2C real-time-clock driver (DS1307 / DS3231) - a battery-backed time source.
 *
 * Default off. services/peripherals/rtc reads and sets a DS1307/DS3231 RTC over I2C (Wire), so the device
 * keeps accurate wall-clock time across reboots and power loss with no network - the ideal
 * fallback below GPS and above upstream NTP in a time-source chain (feeds `pc_time_now()`
 * and the NTP server). The BCD<->epoch conversion (7 time registers, 12/24-hour, leap years,
 * range validation) is pure and host-tested; only the register read/write touches I2C.
 */
#ifndef PC_ENABLE_RTC
#define PC_ENABLE_RTC 0
#endif

/** @brief I2C address of the RTC (DS1307/DS3231 are fixed at 0x68). */
#ifndef PC_RTC_I2C_ADDR
#define PC_RTC_I2C_ADDR 0x68
#endif

/**
 * @brief HLK-LD2410 24 GHz mmWave presence / motion radar (UART).
 *
 * Default off. services/peripherals/ld2410 syncs to the LD2410's framed serial output (256000 baud) and
 * decodes the target report - presence state (none / moving / stationary / both), the moving
 * and stationary target distance (cm) and energy (0-100), and, in engineering mode, the
 * per-gate energies - plus encodes the config commands (enter / exit config, enable / disable
 * engineering mode, restart). The frame sync + decode is pure and host-tested; only the UART
 * read/write touches hardware. A cheap solder-and-test breakout: wave a hand, watch presence.
 */
#ifndef PC_ENABLE_LD2410
#define PC_ENABLE_LD2410 0
#endif

/**
 * @brief RCWL-0516 microwave Doppler presence sensor + the shared one-GPIO presence facade
 *        (`services/rcwl0516`).
 *
 * Default off. The RCWL-0516 has no data protocol - just one 3.3 V OUT pin that latches HIGH on a
 * moving reflector - so the whole problem is timing, not bytes. `PresenceCore` is a debounced,
 * hold-extended view of one active-high presence pin: a level must hold for
 * `PC_RCWL0516_DEBOUNCE_MS` before it is believed (the OUT pin is comparator-driven and chatters
 * around the threshold), and presence then persists for `PC_RCWL0516_HOLD_MS` past the last
 * believed-HIGH sample, so the module's ~2 s retrigger gaps read as one continuous occupied span
 * instead of a flapping boolean. The core is pure and takes an explicit `now` like
 * `PC_ENABLE_HOTSWAP`, so it is host-tested against a synthetic clock with no GPIO, and every
 * elapsed-time test is an unsigned difference - wrap-safe across a `millis()` rollover. It is
 * deliberately sensor-agnostic: the RCWL-0516 is only its first user (via `pc_rcwl0516_core_init`),
 * and an HMMD OUT pin, a PIR, or an HB100 reuse the same core with their own two constants.
 */
#ifndef PC_ENABLE_RCWL0516
#define PC_ENABLE_RCWL0516 0
#endif

/**
 * @brief Waveshare HMMD 24 GHz mmWave micro-motion radar codec (`services/hmmd`).
 *
 * Default off. The HMMD (Waveshare's FMCW micro-motion module on the S3KM1110 / SXKMxxx0 radar SoC)
 * is a close relative of the LD2410 and shares its framing exactly: a report frame
 * `F4 F3 F2 F1 | len(2) | ... | F8 F7 F6 F5` and a command frame
 * `FD FC FB FA | len(2) | word(2) | [value] | 04 03 02 01`, everything little-endian. Where the
 * LD2410 splits moving vs stationary across 9 gates, the HMMD reports one detection flag, one
 * distance, and the per-gate energy of 16 gates - it is a micro-motion detector, so a still person
 * breathing is the case it exists to catch. `pc_hmmd_parse_report` decodes a report and
 * `HmmdStream` reassembles frames byte-by-byte with resync on noise (fixed buffer, no heap),
 * mirroring `Ld2410Stream`; the command encoders cover open/close command mode plus the firmware
 * version (0x0000), serial number (0x0011), parameter config (0x0008) and register (0x0002) reads,
 * and `pc_hmmd_parse_ack` decodes the replies. The module's bare GPIO OUT pin carries no protocol -
 * feed it to `PresenceCore` from `PC_ENABLE_RCWL0516` for debounced, hold-extended presence
 * (an application-level wiring choice; this service does not depend on that one). Framing, payload
 * layout, and command encoding come from the public 2Grey/s3km1110 reference; no vendor SDK is used.
 */
#ifndef PC_ENABLE_HMMD
#define PC_ENABLE_HMMD 0
#endif

/** @brief HMMD UART baud rate (the module's factory default is 115200). */
#ifndef PC_HMMD_BAUD
#define PC_HMMD_BAUD 115200
#endif

/** @brief UART unit the HMMD is wired to. Unit 2 is the one free of the console on most boards. */
#ifndef PC_HMMD_UART
#define PC_HMMD_UART 2
#endif

/**
 * @brief IEC 61784-3 black-channel Safety Communication Layer primitives (`services/safety_scl`).
 *
 * Default off. The functional-safety profiles (PROFIsafe / IEC 61784-3-3, CIP Safety / -3-2, FSoE /
 * -3-12, IO-Link Safety) all treat the underlying fieldbus as an untrusted "black channel" and layer
 * the same three end-to-end checks on top: a CRC signature over the safety payload, a monitoring /
 * consecutive counter, and a receive watchdog. This lands the counter state machine, the watchdog,
 * and the fail-safe state machine that combines them, so each profile's codec composes these rather
 * than reimplementing them. It deliberately does **not** compute the CRC: every profile defines its
 * own polynomial, width, seed and input ordering, those constants live in paid standards, and a
 * guessed CRC in a safety layer would look authoritative while silently failing to detect the
 * corruption it exists to catch - so the caller passes its profile's verdict in as `signature_ok`
 * and this module owns the consequence, which is profile-independent. Fail-safe latches: once any
 * check fails the connection stays fail-safe until an explicit `pc_scl_reset`, because a safety
 * layer that silently reheals lets an intermittent fault present as a working link. Pure, with an
 * explicit `now` like `PC_ENABLE_HOTSWAP`, host-tested against a synthetic clock, and wrap-safe
 * across a `millis()` rollover. No heap, no stdlib.
 */
#ifndef PC_ENABLE_SAFETY_SCL
#define PC_ENABLE_SAFETY_SCL 0
#endif

/** @brief LD2410 UART baud rate (the module's fixed factory default is 256000). */
#ifndef PC_LD2410_BAUD
#define PC_LD2410_BAUD 256000
#endif

/** @brief UART unit the LD2410 is wired to. Unit 2 is the one free of the console on most boards. */
#ifndef PC_LD2410_UART
#define PC_LD2410_UART 2
#endif

/**
 * @brief DFRobot SEN0192 10.525 GHz microwave Doppler motion sensor (single digital OUT line).
 *
 * Default off. services/peripherals/sen0192 tracks the module's OUT line as a debounced presence signal: it asserts
 * presence on an active sample and holds it for PC_SEN0192_HOLD_MS after the last active sample, so
 * brief gaps between Doppler returns don't make presence flap. The presence state machine is pure and
 * host-tested; only the GPIO read touches hardware. Unlike a PIR it senses motion through thin non-metal
 * enclosures and is unaffected by ambient light / temperature.
 */
#ifndef PC_ENABLE_SEN0192
#define PC_ENABLE_SEN0192 0
#endif

/** @brief GPIO the SEN0192 OUT line is wired to. */
#ifndef PC_SEN0192_PIN
#define PC_SEN0192_PIN 4
#endif

/** @brief Presence is held this many ms after the last active (motion) sample before it clears. */
#ifndef PC_SEN0192_HOLD_MS
#define PC_SEN0192_HOLD_MS 2000
#endif

/** @brief SEN0192 OUT polarity: 1 = the OUT line reads HIGH on motion, 0 = active-LOW. */
#ifndef PC_SEN0192_ACTIVE_HIGH
#define PC_SEN0192_ACTIVE_HIGH 1
#endif

/**
 * @brief NXP MPR121 12-channel capacitive-touch controller (I2C).
 *
 * Default off. services/peripherals/mpr121 decodes the touch-status word (12 electrode bits + proximity +
 * over-current) and the 10-bit filtered / baseline per-electrode data, and builds the register
 * init sequence (soft reset, the NXP filter/AFE defaults, per-electrode touch/release
 * thresholds, and the electrode-configuration start). The decode + init-sequence builder are
 * pure and host-tested; only the register read/write touches I2C. A cheap solder-and-test
 * breakout for touch buttons / sliders: wire it up, touch a pad, watch the bit set.
 */
#ifndef PC_ENABLE_MPR121
#define PC_ENABLE_MPR121 0
#endif

/** @brief I2C address of the MPR121 (0x5A default; 0x5B/0x5C/0x5D via the ADDR pin). */
#ifndef PC_MPR121_I2C_ADDR
#define PC_MPR121_I2C_ADDR 0x5A
#endif

/** @brief MPR121 per-electrode touch threshold (delta counts from baseline; NXP AN3944 suggests ~4..12).
 *         Higher = less sensitive. Keep the release threshold below it for hysteresis. */
#ifndef PC_MPR121_TOUCH_THRESHOLD
#define PC_MPR121_TOUCH_THRESHOLD 12
#endif

/** @brief MPR121 per-electrode release threshold (delta counts; should be below the touch threshold). */
#ifndef PC_MPR121_RELEASE_THRESHOLD
#define PC_MPR121_RELEASE_THRESHOLD 6
#endif

/**
 * @brief Sensirion SHT3x temperature / humidity sensor (I2C).
 *
 * Default off. services/peripherals/sht3x issues the single-shot measurement command, checks the CRC-8 on
 * each returned word (polynomial 0x31, init 0xFF - the Sensirion check value 0xBEEF -> 0x92),
 * and converts the raw 16-bit ticks to temperature and relative humidity in integer milli-units
 * (no float printf needed). The CRC + conversion are pure and host-tested; only the command
 * write / data read touches I2C. A cheap solder-and-test breakout (GY-SHT31 etc.) for
 * environmental telemetry: read it, bridge it onto the network.
 */
#ifndef PC_ENABLE_SHT3X
#define PC_ENABLE_SHT3X 0
#endif

/** @brief I2C address of the SHT3x (0x44 with ADDR low; 0x45 with ADDR high). */
#ifndef PC_SHT3X_I2C_ADDR
#define PC_SHT3X_I2C_ADDR 0x44
#endif

/**
 * @brief NXP PCA9685 16-channel 12-bit PWM / servo driver (I2C).
 *
 * Default off. services/peripherals/pca9685 computes the PRESCALE value for a PWM frequency from the 25 MHz
 * oscillator, the per-channel register address, the 12-bit ON/OFF pulse counts, and a servo
 * pulse-width (microseconds) -> count conversion; it also emits the 5-byte channel PWM write.
 * The prescale / count math + the register encoder are pure and host-tested; only the register
 * writes touch I2C. A cheap solder-and-test breakout for driving up to 16 servos or LEDs.
 */
#ifndef PC_ENABLE_PCA9685
#define PC_ENABLE_PCA9685 0
#endif

/** @brief I2C address of the PCA9685 (0x40 default; the six address pins select 0x40..0x7F). */
#ifndef PC_PCA9685_I2C_ADDR
#define PC_PCA9685_I2C_ADDR 0x40
#endif

/** @brief Default PWM output frequency in Hz (50 Hz suits hobby servos). */
#ifndef PC_PCA9685_FREQ
#define PC_PCA9685_FREQ 50
#endif

/**
 * @brief TI ADS1115 16-bit ADC (I2C) - a precise external analog input.
 *
 * Default off. services/peripherals/ads1115 builds the 16-bit config register (OS start, single-ended
 * channel MUX, programmable gain, single-shot mode, data rate, comparator disabled) for a
 * single-shot reading, and converts the signed 16-bit result to microvolts for the selected
 * gain's full-scale range. The config encoder + conversion are pure and host-tested; only the
 * register write / conversion read touches I2C. A cheap solder-and-test breakout for reading
 * batteries, potentiometers, and analog sensors with far more resolution than the ESP32 ADC.
 */
#ifndef PC_ENABLE_ADS1115
#define PC_ENABLE_ADS1115 0
#endif

/** @brief I2C address of the ADS1115 (0x48 with ADDR to GND; 0x49/0x4A/0x4B for VDD/SDA/SCL). */
#ifndef PC_ADS1115_I2C_ADDR
#define PC_ADS1115_I2C_ADDR 0x48
#endif

/** @brief Default ADS1115 PGA gain code (ADS1115_GAIN_*): 0=+/-6.144V, 1=+/-4.096V, 2=+/-2.048V (default),
 *         3=+/-1.024V, 4=+/-0.512V, 5=+/-0.256V. Also the fallback when a read passes an invalid gain. */
#ifndef PC_ADS1115_GAIN
#define PC_ADS1115_GAIN 2 // ADS1115_GAIN_2 (+/- 2.048 V)
#endif

/** @brief Default ADS1115 data-rate code (ADS1115_DR_*): 0=8, 1=16, 2=32, 3=64, 4=128 (default), 5=250,
 *         6=475, 7=860 SPS. The single-shot read waits the matching conversion time. */
#ifndef PC_ADS1115_DR
#define PC_ADS1115_DR 4 // ADS1115_DR_128 (128 SPS)
#endif

/** @brief ADS1115 input mode: 0 = single-ended (AINx vs GND), 1 = differential. In differential mode the
 *         channel selects the pair: 0=AIN0-AIN1, 1=AIN0-AIN3, 2=AIN1-AIN3, 3=AIN2-AIN3. */
#ifndef PC_ADS1115_DIFFERENTIAL
#define PC_ADS1115_DIFFERENTIAL 0
#endif

/**
 * @brief SMBus 3.1 transaction shapes over the shared I2C bus.
 *
 * Default off. services/peripherals/smbus adds the named transaction forms SMBus defines on top of
 * I2C: quick command, send / receive byte, write / read byte and word, block write and read, and
 * the two process calls. A part that speaks SMBus (a battery gauge, a fan controller, a power
 * sequencer) answers this fixed set rather than a register layout its datasheet invents.
 *
 * The Packet Error Code is a CRC-8 over every byte of the transaction, the address bytes and their
 * R/W bits included. It comes from the shared CRC engine (PC_CRC8_SMBUS) and is off until
 * pc_smbus_set_pec() turns it on; a part that does not implement PEC NACKs the extra byte. The PEC
 * computation is pure and host-tested; only the transfers touch I2C.
 */
#ifndef PC_ENABLE_SMBUS
#define PC_ENABLE_SMBUS 0
#endif

/**
 * @brief PMBus 1.3 power-management command set over SMBus.
 *
 * Default off. services/peripherals/pmbus adds the standard command codes and the two numeric
 * encodings PMBus reports in: LINEAR11 for most telemetry (a 5-bit signed exponent and an 11-bit
 * signed mantissa in one word) and LINEAR16 for output voltage (a 16-bit unsigned mantissa scaled
 * by an exponent the part reports separately). Reading a digital point-of-load converter's input
 * voltage, output current, temperature and fault status goes through these. Needs PC_ENABLE_SMBUS.
 *
 * The encodings are pure and host-tested; the commands ride the SMBus shapes.
 */
#ifndef PC_ENABLE_PMBUS
#define PC_ENABLE_PMBUS 0
#endif

/**
 * @brief TI INA219 high-side current / power monitor (I2C).
 *
 * Default off. services/peripherals/ina219 decodes the bus-voltage register (LSB 4 mV) and the shunt-voltage
 * register (LSB 10 uV), computes the calibration register from the shunt resistance and the
 * chosen current LSB, and scales the raw current / power registers to microamps / microwatts.
 * The decode + calibration + scaling math are pure and host-tested; only the register read /
 * write touches I2C. A cheap solder-and-test breakout for measuring how much current and power a
 * circuit actually draws.
 */
#ifndef PC_ENABLE_INA219
#define PC_ENABLE_INA219 0
#endif

/** @brief I2C address of the INA219 (0x40 default; the A0/A1 pins select 0x40..0x4F). */
#ifndef PC_INA219_I2C_ADDR
#define PC_INA219_I2C_ADDR 0x40
#endif

/** @brief Default INA219 current LSB in microamps per bit (calibration input). The fallback when
 *         pc_ina219_begin() is passed 0. 100 uA/bit with a 100 mohm shunt -> a 2 A full-scale range. */
#ifndef PC_INA219_CURRENT_LSB_UA
#define PC_INA219_CURRENT_LSB_UA 100
#endif

/** @brief Default INA219 shunt resistance in milliohms (calibration input). The fallback when
 *         pc_ina219_begin() is passed 0. 100 mohm is the common breakout value. */
#ifndef PC_INA219_SHUNT_MOHM
#define PC_INA219_SHUNT_MOHM 100
#endif

/**
 * @brief Typed NVS configuration store (WiFi creds, IP config, ... as blobs).
 *
 * When set, src/services/storage/config_store/config_store.h provides a typed key/value
 * API (string / u32 / blob) that routes core settings into the ESP32's native
 * NVS partition (via `Preferences`) instead of a JSON file on the filesystem -
 * which survives FS corruption and is the corruption-resistant home for
 * credentials. On host builds it is backed by a fixed in-memory table so the
 * typed contract is unit-testable. Default off.
 */
#ifndef PC_ENABLE_CONFIG_STORE
#define PC_ENABLE_CONFIG_STORE 0
#endif

/** @brief Max key/value entries in the host (test) config backend. */
#ifndef PC_CONFIG_MAX_ENTRIES
#define PC_CONFIG_MAX_ENTRIES 16
#endif

/** @brief Max key length incl. null (NVS caps keys at 15 chars). */
#ifndef PC_CONFIG_KEY_MAX
#define PC_CONFIG_KEY_MAX 16
#endif

/** @brief Max value bytes per entry in the host (test) config backend. */
#ifndef PC_CONFIG_VAL_MAX
#define PC_CONFIG_VAL_MAX 64
#endif

/**
 * @brief Stable device UUID derived from the chip MAC (RFC 4122 v5).
 *
 * When set, src/server/signaling/device_id.h derives a deterministic v5 UUID
 * from a MAC (via the library's SHA-1) - a storage-free, stable identity for
 * mDNS hostnames, MQTT client IDs, etc. The MAC->UUID core is host-testable;
 * pc_device_uuid() reads the ESP32 factory MAC. Default off.
 */
#ifndef PC_ENABLE_DEVICE_ID
#define PC_ENABLE_DEVICE_ID 0
#endif

/**
 * @brief Telemetry math helpers (moving-window stats, rate-of-change, totalizer).
 *
 * Default off. When set, src/services/iot/telemetry/telemetry.h provides zero-heap
 * pure-computation helpers over caller-supplied storage: a moving-window stats
 * accumulator (mean / variance / stddev / min / max), a derivative / rate-of-
 * change tracker, and a trapezoidal run-time totalizer. No ESP32 dependency, so
 * the whole cluster is host-testable; it feeds dashboards, alert triggers, and
 * odometer-style counters.
 */
#ifndef PC_ENABLE_TELEMETRY
#define PC_ENABLE_TELEMETRY 0
#endif

/**
 * @brief Real-time SVG dashboard (PC_ENABLE_DASHBOARD; requires PC_ENABLE_SSE).
 *
 * Default off. Serves a self-contained, hand-rolled SVG dashboard page whose
 * widgets are declared in a fixed compile-time pc_widget table (zero-heap,
 * deterministic). The page fetches the widget layout as JSON and subscribes to an
 * SSE stream of live values; pc_dashboard_set() + pc_dashboard_publish()
 * push the current readings. The widget-table -> JSON serializers are
 * host-testable; WebSocket controls are a follow-up.
 */
#ifndef PC_ENABLE_DASHBOARD
#define PC_ENABLE_DASHBOARD 0
#endif

/**
 * @brief Embed the theme stylesheet library as runtime-selectable blobs (default off).
 *
 * Off by default: build-time theme injection (`<!--#theme NAME-->`) costs nothing extra, but
 * embedding the whole library for runtime switching links every theme's CSS into flash (~1 KB each).
 * When set, application/binary_asset_blobs.{h,cpp} exposes `pc_theme_css(name)` + the registry
 * `PC_THEME_BLOBS`, so a route (e.g. `/themes/<name>.css`) or a picker can switch themes live.
 * Regenerate with `web_assets/wizard/gen_theme_blobs.py` after adding a theme.
 */
#ifndef PC_ENABLE_THEMES
#define PC_ENABLE_THEMES 0
#endif

/**
 * @brief Include the trademark-named themes in the embedded set (default on / open-source).
 *
 * A few themes are named after a company or product (Darcula, Windows XP, Discord, Spotify, ...). The
 * palette is just colors, but a commercial product should not ship the branded name, so a commercial
 * build sets this to 0 to drop those blobs from the registry (the list is `RESTRICTED` in
 * `web_assets/wizard/gen_themes.py`). The open-source (AGPL) build keeps them.
 */
#ifndef PC_THEMES_INCLUDE_TRADEMARKED
#define PC_THEMES_INCLUDE_TRADEMARKED 1
#endif

/** @brief Maximum widgets in the dashboard table (BSS value array). */
#ifndef PC_DASHBOARD_MAX_WIDGETS
#define PC_DASHBOARD_MAX_WIDGETS 16
#endif

/** @brief Stack buffer for the dashboard layout / values JSON (bytes). */
#ifndef PC_DASHBOARD_JSON_BUF
#define PC_DASHBOARD_JSON_BUF 1024
#endif

/**
 * @brief Opt-in flash partition-map monitor endpoint (PC_ENABLE_PARTITION_MONITOR).
 *
 * Default off. When set, services/storage/partition_monitor reports the device's flash
 * partition table (label, kind, type / subtype, offset, size, and which app slot
 * is running) as JSON, for diagnostics and OTA dashboards. The partition walk uses
 * esp_partition / esp_ota_ops; the JSON serializer and the kind classifier are
 * pure and host-testable.
 */
#ifndef PC_ENABLE_PARTITION_MONITOR
#define PC_ENABLE_PARTITION_MONITOR 0
#endif

/** @brief Maximum partitions the monitor reports (BSS table). */
#ifndef PC_PARTITION_MAX
#define PC_PARTITION_MAX 16
#endif

/** @brief Stack buffer for the partition-map JSON (bytes). */
#ifndef PC_PARTITION_JSON_BUF
#define PC_PARTITION_JSON_BUF 1024
#endif

/**
 * @brief Opt-in browser GPIO pin-mapper / diagnostics endpoint (PC_ENABLE_GPIO_MAP).
 *
 * Default off. When set, server/signaling/gpio_map serves a compile-time table of GPIO
 * pins (number, label, direction, live level) as JSON for a browser diag panel,
 * and accepts a control POST (`pin`, `level`) to drive an output. The live read /
 * write uses the Arduino digital API on ESP32; the JSON serializer and the control
 * parser are pure and host-testable.
 */
#ifndef PC_ENABLE_GPIO_MAP
#define PC_ENABLE_GPIO_MAP 0
#endif

/** @brief Maximum GPIO pins the mapper reports (BSS table). */
#ifndef PC_GPIO_MAX
#define PC_GPIO_MAX 40
#endif

/** @brief Stack buffer for the GPIO-map JSON (bytes). */
#ifndef PC_GPIO_JSON_BUF
#define PC_GPIO_JSON_BUF 1024
#endif

/**
 * @brief Opt-in fire-and-forget UDP telemetry cast (PC_ENABLE_UDP_TELEMETRY).
 *
 * Default off. When set, services/iot/udp_telemetry casts metric lines (InfluxDB line
 * protocol: `measurement field=val,field2=val2`) to a configured collector over
 * UDP via Udp.client->sendto - zero-heap, fire-and-forget (no ACK, no retry), ideal
 * for shipping device metrics to Telegraf/InfluxDB/a log sink. The line builder is
 * pure and host-tested; only the send touches the network.
 */
#ifndef PC_ENABLE_UDP_TELEMETRY
#define PC_ENABLE_UDP_TELEMETRY 0
#endif

/** @brief Stack buffer for one telemetry line (bytes). */
#ifndef PC_UDP_TELEMETRY_BUF
#define PC_UDP_TELEMETRY_BUF 256
#endif

/**
 * @brief Opt-in StatsD metrics client (PC_ENABLE_STATSD).
 *
 * Default off. When set, services/iot/statsd pushes metrics in the StatsD wire format
 * (`name:value|type`, e.g. `api.hits:1|c`) over UDP to a StatsD-speaking collector -
 * Graphite/StatsD, Telegraf, Datadog, InfluxDB, etc. Counters, gauges (absolute + delta),
 * timings, and sets, with optional sample-rate (`|@0.1`) and DogStatsD tags (`|#env:prod`).
 * This is the push counterpart to the pull-based Prometheus `/metrics`. The line formatter
 * is pure and host-tested; only the send (Udp.client->sendto) touches the network. Zero heap.
 */
#ifndef PC_ENABLE_STATSD
#define PC_ENABLE_STATSD 0
#endif

/** @brief Default StatsD collector UDP port (StatsD/Graphite standard). */
#ifndef PC_STATSD_PORT
#define PC_STATSD_PORT 8125
#endif

/** @brief Stack buffer for one StatsD line (bytes; caps metric name + value + tags). */
#ifndef PC_STATSD_LINE_MAX
#define PC_STATSD_LINE_MAX 256
#endif

/**
 * @brief Opt-in runtime heap/stack guardrails (PC_ENABLE_GUARDRAILS).
 *
 * Default off. When set, services/security/guardrails samples free heap, the heap low-water
 * mark, the largest free block (fragmentation), and the calling task's remaining
 * stack, and fires a callback when any crosses its threshold - a proactive
 * fail-safe hook beyond the passive numbers in /metrics. The threshold evaluator
 * and the JSON serializer are pure and host-tested; the sample reads esp_* / the
 * FreeRTOS stack high-water on ESP32.
 */
#ifndef PC_ENABLE_GUARDRAILS
#define PC_ENABLE_GUARDRAILS 0
#endif

/** @brief Free-heap floor (bytes); below this trips the heap guardrail. */
#ifndef PC_GUARDRAIL_HEAP_MIN
#define PC_GUARDRAIL_HEAP_MIN 8192
#endif

/** @brief Largest-free-block floor (bytes); below this trips the fragmentation guardrail. */
#ifndef PC_GUARDRAIL_FRAG_MIN_BLOCK
#define PC_GUARDRAIL_FRAG_MIN_BLOCK 4096
#endif

/** @brief Task remaining-stack floor (bytes); below this trips the stack guardrail. */
#ifndef PC_GUARDRAIL_STACK_MIN
#define PC_GUARDRAIL_STACK_MIN 512
#endif

/**
 * @brief Opt-in software watchdog: deadlock detection + fail-safe safe-state (PC_ENABLE_FAILSAFE).
 *
 * When set, server/failsafe provides a fixed registry of "lifelines" (a task / worker / control loop
 * that must check in within its deadline). pc_failsafe_check() detects one that stopped feeding (a
 * hang / deadlock) and fires a breach callback once per episode so the app can enter a known-safe
 * state. App-defined and per-lifeline, on top of the hardware task watchdog. Pure core, zero heap.
 * Default off.
 */
#ifndef PC_ENABLE_FAILSAFE
#define PC_ENABLE_FAILSAFE 0
#endif

/** @brief Max monitored lifelines in the fail-safe registry (static, zero-heap). */
#ifndef PC_FAILSAFE_MAX_LIFELINES
#define PC_FAILSAFE_MAX_LIFELINES 8
#endif

/**
 * @brief Opt-in dynamic sleep-cycle scheduler (PC_ENABLE_SLEEP_SCHED).
 *
 * When set, server/sleep_sched provides pc_sleep_next(): from the time since the last activity it
 * returns how long a low-power device should sleep (0 = stay awake), ramping the window from a floor up
 * to a ceiling the longer the idle streak runs. Pure decision core (the app applies the window via
 * light / modem / deep sleep). Complements services/radio_power. Default off.
 */
#ifndef PC_ENABLE_SLEEP_SCHED
#define PC_ENABLE_SLEEP_SCHED 0
#endif

/**
 * @brief Opt-in flash wear-leveling slot selector (PC_ENABLE_WEARLEVEL).
 *
 * When set, server/filesystem/wearlevel provides pc_wearlevel_pick(): given per-slot write counts it returns
 * the least-worn slot to write next, so repeated flash/NVS writes spread evenly and the region ages
 * together instead of burning out one block. Pure core (the app owns the slots + persisted counts).
 * Default off.
 */
#ifndef PC_ENABLE_WEARLEVEL
#define PC_ENABLE_WEARLEVEL 0
#endif

/**
 * @brief Opt-in SoC power governor (PC_ENABLE_POWER_MGMT).
 *
 * network_drivers/physical/radio_power owns the radio and server/sleep_sched decides how long to sleep; neither
 * owns the SoC. When set, server/power_mgmt decides the CPU clock from load, die temperature and
 * the reset reason: idle work runs at the floor instead of spinning a 240 MHz core, a hot die clocks
 * down (with a lower restore threshold, so a part sitting at the limit does not oscillate), and a
 * board that just browned out comes back up at the floor for a settle window rather than slamming
 * into the load that collapsed its supply. It can also release the Bluetooth power domain on a build
 * that never uses BT. Pure decision core, host-tested; the binding only reads sensors and applies.
 * Default off.
 */
#ifndef PC_ENABLE_POWER_MGMT
#define PC_ENABLE_POWER_MGMT 0
#endif

/** @brief CPU clock (MHz) when there is work to do. */
#ifndef PC_POWER_MHZ_MAX
#define PC_POWER_MHZ_MAX 240
#endif

/** @brief CPU clock (MHz) when idle, thermally throttled, or recovering from a brownout. */
#ifndef PC_POWER_MHZ_MIN
#define PC_POWER_MHZ_MIN 80
#endif

/** @brief Load percentage at/above which the ceiling clock is used. */
#ifndef PC_POWER_BUSY_PCT
#define PC_POWER_BUSY_PCT 40
#endif

/** @brief Die temperature (C) at/above which the clock is throttled. */
#ifndef PC_POWER_TEMP_HOT_C
#define PC_POWER_TEMP_HOT_C 80
#endif

/**
 * @brief Die temperature (C) at/below which the throttle is released.
 *
 * Deliberately below PC_POWER_TEMP_HOT_C: with a single threshold a part sitting exactly at the
 * limit would flap between ceiling and floor every tick, which is worse than either state.
 *
 * The gap has to be wider than the temperature swing the clock change *itself* causes, or the
 * governor oscillates no matter how correct the hysteresis is. Measured on an ESP32-S3: dropping
 * 240 -> 80 MHz cools the die about 2 C within one 500 ms tick, and going back up reheats it by the
 * same amount. A band narrower than that swing is self-sustaining - the throttle's own effect
 * carries the die back across the release threshold. The 10 C default clears it with room to spare.
 */
#ifndef PC_POWER_TEMP_COOL_C
#define PC_POWER_TEMP_COOL_C 70
#endif

/** @brief How long (ms) to hold the floor clock after a brownout reset before ramping back up. */
#ifndef PC_POWER_RECOVER_MS
#define PC_POWER_RECOVER_MS 10000
#endif

#if PC_ENABLE_POWER_MGMT && (PC_POWER_TEMP_COOL_C >= PC_POWER_TEMP_HOT_C)
#error "ProtoCore: PC_POWER_TEMP_COOL_C must be below PC_POWER_TEMP_HOT_C (hysteresis)"
#endif

#if PC_ENABLE_POWER_MGMT && (PC_POWER_MHZ_MIN > PC_POWER_MHZ_MAX)
#error "ProtoCore: PC_POWER_MHZ_MIN must not exceed PC_POWER_MHZ_MAX"
#endif

#if PC_ENABLE_POWER_MGMT && (PC_POWER_BUSY_PCT > 100)
#error "ProtoCore: PC_POWER_BUSY_PCT must be 0..100"
#endif

/**
 * @brief Opt-in removable-storage hot-swap safeties (PC_ENABLE_HOTSWAP).
 *
 * An SD card is a connector, and it can be pulled mid-write. The failure is quiet: the driver still
 * reports a mounted volume, writes fail into a void, and code carries on believing it has storage.
 * When set, services/storage/hotswap runs a small state machine per volume (ABSENT / READY / FAULTED): a run
 * of consecutive I/O errors declares the medium gone and unmounts it immediately, `pc_hotswap_ready()`
 * becomes the fail-closed gate callers check before any filesystem call, and a periodic probe
 * remounts when a card comes back. The core is pure and takes an explicit `now`, so the whole state
 * machine is host-tested; mounting is three app callbacks, since how a volume mounts is the
 * application's business. Default off.
 */
#ifndef PC_ENABLE_HOTSWAP
#define PC_ENABLE_HOTSWAP 0
#endif

/**
 * @brief Consecutive I/O failures that declare a removable volume gone.
 *
 * Not 1: a single failed write is not proof a card left (a transient bus error, a full volume), and
 * tearing down a working mount over one error would be its own bug. Any success resets the run.
 */
#ifndef PC_HOTSWAP_FAIL_THRESHOLD
#define PC_HOTSWAP_FAIL_THRESHOLD 3
#endif

/** @brief Minimum gap between remount attempts while a volume is absent or faulted (ms). */
#ifndef PC_HOTSWAP_PROBE_MS
#define PC_HOTSWAP_PROBE_MS 2000
#endif

#if PC_ENABLE_HOTSWAP && (PC_HOTSWAP_FAIL_THRESHOLD < 1 || PC_HOTSWAP_FAIL_THRESHOLD > 255)
#error "ProtoCore: PC_HOTSWAP_FAIL_THRESHOLD must be in [1, 255]"
#endif

/**
 * @brief Opt-in network adaptation decisions (PC_ENABLE_NETADAPT).
 *
 * When set, services/net/netadapt provides two pure decisions: pc_netadapt_window() sizes the TCP
 * receive window from the free heap (bigger when RAM is plentiful, shrinking when tight), and
 * pc_netadapt_dhcp_fallback() decides when to give up on DHCP and use a static IP. The app applies
 * the results (lwIP window / netif config). Default off.
 */
#ifndef PC_ENABLE_NETADAPT
#define PC_ENABLE_NETADAPT 0
#endif

/**
 * @brief Opt-in DShot ESC throttle protocol codec (PC_ENABLE_DSHOT).
 *
 * When set, services/peripherals/dshot provides pc_dshot_encode() / _decode(): the 16-bit DShot frame
 * (11-bit throttle/command + telemetry bit + 4-bit CRC), the bidirectional/extended inverted-CRC
 * variant, and the per-rate bit timing for an RMT driver. Pure codec (the app clocks it out via RMT).
 * Default off.
 */
#ifndef PC_ENABLE_DSHOT
#define PC_ENABLE_DSHOT 0
#endif

/**
 * @brief Opt-in HART / HART-IP process-instrument protocol codec (PC_ENABLE_HART).
 *
 * When set, services/fieldbus/hart provides the HART command-frame codec (build/parse with the longitudinal XOR
 * checksum, short + long addressing) and the 8-octet HART-IP message header, so a device speaks HART
 * over UDP/TCP 5094 (front-end-free) or, with a HART FSK modem, over the 4-20 mA loop. Pure, host-tested.
 * Default off.
 */
#ifndef PC_ENABLE_HART
#define PC_ENABLE_HART 0
#endif

/**
 * @brief Opt-in Network Time Security (NTS, RFC 8915) wire codec (PC_ENABLE_NTS).
 *
 * When set, network_drivers/application/nts provides the NTS-KE record codec (build/parse the TLV records - next
 * protocol, AEAD, cookies, server/port) and the NTS NTP extension-field framing (Unique Identifier, Cookie,
 * Authenticator). Pure framing (the AES-SIV-CMAC-256 AEAD + TLS-exporter key derivation are the crypto
 * integration on top). Default off.
 */
#ifndef PC_ENABLE_NTS
#define PC_ENABLE_NTS 0
#endif

/**
 * @brief Opt-in DDS / RTPS wire-protocol codec (PC_ENABLE_DDS).
 *
 * When set, services/iot/dds provides the RTPS (DDSI-RTPS) message + submessage framing: the 20-octet
 * header (magic / version / vendor / guidPrefix) and the typed submessages (INFO_TS, DATA, HEARTBEAT,
 * ACKNACK, ...) with the endianness flag, built by pc_rtps_header / _submessage and walked by
 * pc_rtps_parse. Pure framing (CDR payloads + SPDP/SEDP discovery layer on top). Default off.
 */
#ifndef PC_ENABLE_DDS
#define PC_ENABLE_DDS 0
#endif

/**
 * @brief Opt-in XMPP (RFC 6120) stanza codec (PC_ENABLE_XMPP).
 *
 * When set, services/iot/xmpp builds correctly XML-escaped `<stream:stream>` / `<message>` / `<presence>` /
 * `<iq>` stanzas into a caller buffer and reads the stanza element name + an attribute value out of a
 * received stanza, so a device is an IoT XMPP client. Pure text framing (TLS/SASL ride the client TLS
 * path; the IoT XEPs layer inside `<iq>`). Default off.
 */
#ifndef PC_ENABLE_XMPP
#define PC_ENABLE_XMPP 0
#endif

/**
 * @brief Opt-in raw Layer-2 Ethernet frame codec (PC_ENABLE_RAWL2).
 *
 * When set, services/fieldbus/rawl2 builds/parses Ethernet II + 802.1Q VLAN frames (no FCS - the MAC appends it;
 * pc_eth_fcs is provided for the cases that need it), so the app can inject/receive arbitrary L2
 * frames through the vendor L2 transmit path - the basis for the raw-L2 industrial protocols
 * (PROFINET DCP, GOOSE, POWERLINK). Pure codec, host-tested. Default off.
 */
#ifndef PC_ENABLE_RAWL2
#define PC_ENABLE_RAWL2 0
#endif

/**
 * @brief Opt-in single-page-app micro-routing decision (PC_ENABLE_SPA_ROUTER).
 *
 * When set, services/web/spa_router provides pc_spa_route(): given a request path it returns whether to
 * serve a real asset file, serve the SPA shell (index.html) for a client-side route, or pass through to
 * the app's handlers under an API prefix - so a single-page UI's client routing works. Pure decision
 * core (the caller wires the result into serve_static / the router). Default off.
 */
#ifndef PC_ENABLE_SPA_ROUTER
#define PC_ENABLE_SPA_ROUTER 0
#endif

/**
 * @brief Opt-in IEC 61850 GOOSE publisher codec (PC_ENABLE_GOOSE).
 *
 * When set, services/energy/goose builds the BER-encoded IECGoosePdu (gocbRef / timeAllowedToLive / datSet /
 * goID / t / stNum / sqNum / simulation / confRev / ndsCom / numDatSetEntries / allData) and wraps it in
 * the 8-octet GOOSE header + Ethernet frame (ethertype 0x88B8) for the fast raw-L2 substation-event
 * publish. Pure codec (allData is a caller-encoded BER blob; the raw-L2 transmit is the device step).
 * Default off.
 */
#ifndef PC_ENABLE_GOOSE
#define PC_ENABLE_GOOSE 0
#endif

/**
 * @brief Opt-in MTConnect agent response codec (PC_ENABLE_MTCONNECT).
 *
 * When set, services/machine_tool/mtconnect builds the MTConnectStreams (current/sample) and MTConnectError XML
 * response documents (ANSI/MTC1.4) into a caller buffer - header with instanceId + nextSequence, then
 * per-DataItem Samples/Events/Condition observations - so the web server is an MTConnect agent over the
 * existing HTTP stack. Pure text framing (values XML-escaped). Default off.
 */
#ifndef PC_ENABLE_MTCONNECT
#define PC_ENABLE_MTCONNECT 0
#endif

/**
 * @brief MTConnect rolling sample buffer sizing (PC_ENABLE_MTCONNECT).
 *
 * The agent retains the most recent ::PC_MTC_SAMPLE_BUFFER observations in a fixed ring so a
 * subscriber can replay them with the `sample` from/count long-poll cursor (MTC1.4 §6.7): a request
 * asks for observations starting at a sequence number, and the response header reports firstSequence /
 * lastSequence / nextSequence so the client knows what it received and where to resume. Each retained
 * observation stores its type / dataItemId / timestamp / value in fixed char fields; when the ring is
 * full the oldest is evicted and firstSequence advances. Zero-heap, compile-time sized; the buffer costs
 * ~PC_MTC_SAMPLE_BUFFER * (48 + the four string caps) bytes only where a pc_mtc_sample_buffer is used.
 */
#ifndef PC_MTC_SAMPLE_BUFFER
#define PC_MTC_SAMPLE_BUFFER 32 // observations retained for `sample` replay
#endif
#ifndef PC_MTC_STR_MAX
#define PC_MTC_STR_MAX 24 // max stored type / dataItemId length (excl NUL)
#endif
#ifndef PC_MTC_TS_MAX
#define PC_MTC_TS_MAX 32 // max stored ISO-8601 timestamp length (excl NUL)
#endif
#ifndef PC_MTC_VAL_MAX
#define PC_MTC_VAL_MAX 32 // max stored observation value length (excl NUL)
#endif

/**
 * @brief Opt-in write-ahead store for atomic buffer-to-flash storage (PC_ENABLE_WAL).
 *
 * services/storage/wal is a power-loss-safe write-ahead log over any fs::FS backend (SD card, LittleFS): records
 * are CRC32-framed, a checkpoint is atomic via an A/B superblock, and a recovery scan on mount replays
 * past the last checkpoint and stops at the first bad CRC (the torn tail), bounding the loss window. Sized
 * from the measured SD envelope (docs/FEATURE_PERFORMANCE.md): append sequentially in ~32 KiB pages,
 * checkpoint every ~128-256 KiB (never scatter small durable writes). The substrate for on-device data
 * stores (dbm / sqlite / nosql). Zero heap. Default off.
 */
#ifndef PC_ENABLE_WAL
#define PC_ENABLE_WAL 0
#endif
#ifndef PC_WAL_PAGE_SIZE
#define PC_WAL_PAGE_SIZE 32768 // sequential write unit (the measured durable-throughput knee)
#endif
#ifndef PC_WAL_MAX_RECORD
#define PC_WAL_MAX_RECORD 4096 // largest single record payload
#endif

/**
 * @brief Opt-in dbm: a log-structured hash key-value store on the WAL (PC_ENABLE_DBM, requires WAL).
 *
 * services/storage/dbm is a Bitcask-style key-value store: each put/delete appends one WAL record (so writes are
 * the WAL's fast sequential appends, not slow durable random writes), and an in-RAM open-addressed hash
 * index (fixed BSS, no heap) maps each live key to where its value sits in the log. Mount rebuilds the
 * index by scanning the WAL. Keys are bounded by PC_DBM_KEY_MAX, values by PC_DBM_VAL_MAX, and the
 * index holds up to PC_DBM_SLOTS live keys. Default off.
 */
#ifndef PC_ENABLE_DBM
#define PC_ENABLE_DBM 0
#endif
#ifndef PC_DBM_SLOTS
#define PC_DBM_SLOTS 256 // max live keys (in-RAM index capacity; open-addressed, keep load < ~0.7)
#endif
#ifndef PC_DBM_KEY_MAX
#define PC_DBM_KEY_MAX 32 // largest key in bytes
#endif
#ifndef PC_DBM_VAL_MAX
#define PC_DBM_VAL_MAX 256 // largest value in bytes
#endif

/**
 * @brief Opt-in local JSON document store on the WAL (PC_ENABLE_DOCSTORE, requires DBM + WAL).
 *
 * services/storage/docstore is a small NoSQL document store: JSON documents addressed by an id, kept durably on
 * the write-ahead log. It is a thin layer over dbm (id = key, JSON body = value) and adds the document
 * capability - top-level field queries (find documents whose JSON field equals a value) via the zero-heap
 * JSON reader. Ids are bounded by PC_DBM_KEY_MAX, bodies by PC_DBM_VAL_MAX. Default off.
 */
#ifndef PC_ENABLE_DOCSTORE
#define PC_ENABLE_DOCSTORE 0
#endif
#ifndef PC_DOCSTORE_FIELD_MAX
#define PC_DOCSTORE_FIELD_MAX 128 // largest string field value a find can compare
#endif

/**
 * @brief Opt-in SQLite3 on-disk file-format reader (PC_ENABLE_SQLITE).
 *
 * services/storage/sqlite parses the documented SQLite database file structure by hand - the 100-byte database
 * header, the b-tree page header, the record varint, and record serial types - so a device can read a
 * SQLite file (from pc_wal_fs / fs::FS) without the SQLite amalgamation (which needs a heap + stdio and does
 * not fit the no-stdlib zero-heap model). Read-first (a bounded writer is a later step); pure, host-tested
 * against real files from the sqlite3 CLI. Default off.
 */
#ifndef PC_ENABLE_SQLITE
#define PC_ENABLE_SQLITE 0
#endif

/**
 * @brief Opt-in CNC RS-232 DNC drip-feed codec (PC_ENABLE_DNC).
 *
 * services/machine_tool/dnc is the transport-agnostic framing + tape-code layer that streams a G-code program
 * (RS-274 / ISO 6983) to a machine-tool controller over RS-232 or a socket: block framing with a `%`
 * rewind-stop, ISO 7-bit (ASCII, optional even parity) or EIA RS-244 (odd-parity punched-tape code)
 * character translation, a streaming block encoder + reassembling decoder, and XON/XOFF software
 * flow-control state. Pure codec (you own the UART / socket); host-tested. Default off.
 */
#ifndef PC_ENABLE_DNC
#define PC_ENABLE_DNC 0
#endif

/**
 * @brief Largest G-code block (one line) the DNC decoder reassembles (PC_ENABLE_DNC).
 *
 * A block longer than this overflows the decoder's fixed line buffer and is dropped whole
 * (::DNC_EV_OVERFLOW) rather than truncated. Sized for a normal G-code line; raise it only for
 * unusually long blocks (many parameters). Zero heap - this is the static per-decoder buffer.
 */
#ifndef PC_DNC_LINE_MAX
#define PC_DNC_LINE_MAX 128
#endif

/**
 * @brief Default leader/trailer runout length for the DNC encoder (PC_ENABLE_DNC).
 *
 * The number of NUL runout bytes ::pc_dnc_encode_leader emits before the program (and can emit after
 * it). The reader skips them until the first `%`. Traditional tape leaders were a few inches of
 * blank feed; 32 bytes is a serial-link equivalent. Overridable per call via DncCfg::leader_len.
 */
#ifndef PC_DNC_LEADER_LEN
#define PC_DNC_LEADER_LEN 32
#endif

/**
 * @brief Safety cap on how many times the DNC stream engine polls the reverse channel while paused
 *        by an XOFF, before giving up with an I/O error (PC_ENABLE_DNC).
 *
 * `dnc_stream` pauses on XOFF and polls `recv` for the XON that resumes it; a well-behaved transport
 * paces `recv` (blocks briefly when idle) so this cap is only a backstop against a `recv` that spins
 * returning no data forever. Raise it if a slow controller legitimately holds XOFF for a long time.
 */
#ifndef PC_DNC_XOFF_MAX_POLLS
#define PC_DNC_XOFF_MAX_POLLS 200000
#endif

/**
 * @brief Opt-in TCP relay / DNAT port forwarding (PC_ENABLE_RELAY).
 *
 * services/net/relay is a bidirectional byte pump that publishes an internal `host:port` through the
 * server: an inbound connection is relayed to an origin (an outbound pc_client connection), moving
 * bytes both ways with backpressure and independent half-close, so the device fronts a service that
 * lives behind it. The engine is a pure step function over two send/recv seams (host-testable); the
 * app owns the two sockets. Default off.
 */
#ifndef PC_ENABLE_RELAY
#define PC_ENABLE_RELAY 0
#endif

/**
 * @brief User-defined address:port -> hardware-bus bridge (services/net/iface_bridge).
 *
 * A configurable "device server": the app registers rules mapping a listen `x.x.x.x:nnnn` (TCP/UDP) to a
 * UART, SPI chip-select, or I2C address. A network client talking to the port is bridged to that bus -
 * raw stream passthrough for UART, or framed write-then-read transactions (uint16 write_len || uint16
 * read_len || write_bytes) for SPI/I2C. The rule table + transaction frame codec are a pure, zero-heap,
 * host-tested core; the bus I/O (Serial/SPI/Wire) and the PROTO_BRIDGE listener are the ESP32 step.
 * Default off.
 */
#ifndef PC_ENABLE_IFACE_BRIDGE
#define PC_ENABLE_IFACE_BRIDGE 0
#endif

/** @brief Max concurrent address:port -> bus rules (services/net/iface_bridge). */
#ifndef PC_BRIDGE_MAX_RULES
#define PC_BRIDGE_MAX_RULES 8
#endif

/**
 * @brief Max write / read payload (bytes) per TRANSACTION frame (services/net/iface_bridge).
 *
 * Bounds the per-transaction stack scratch used to clock an SPI/I2C write-then-read, and rejects a frame
 * whose write_len or read_len exceeds it. Device-server transactions are small register accesses, so the
 * default is modest; a frame over the cap closes the connection (protocol error). Keep it comfortably
 * under the transport RX ring so a whole frame can buffer before it is parsed.
 */
#ifndef PC_BRIDGE_TXN_MAX
#define PC_BRIDGE_TXN_MAX 256
#endif

/** @brief STREAM (UART) pipe chunk size (bytes) for services/net/iface_bridge - one socket<->UART hop. */
#ifndef PC_BRIDGE_STREAM_CHUNK
#define PC_BRIDGE_STREAM_CHUNK 256
#endif

/** @brief Chunks a STREAM target moves per poll before yielding, bounding the UART drain loop. */
#ifndef PC_BRIDGE_MAX_DRAIN
#define PC_BRIDGE_MAX_DRAIN 8
#endif

/** @brief UART TRANSACTION read window (ms): how long a write-then-read waits for the read_len reply. */
#ifndef PC_BRIDGE_UART_TXN_MS
#define PC_BRIDGE_UART_TXN_MS 50
#endif

/**
 * @brief GNSS RTK base station + NTRIP caster (services/timing_position/gnss).
 *
 * Turns the device into a differential-GNSS correction source: it surveys in a fixed antenna position and
 * serves RTCM 3.x corrections to rovers over the network as an NTRIP caster, so a rover applies them for
 * RTK / DGPS accuracy. The RTCM3 frame codec (0xD3 preamble, 10-bit length, CRC-24Q, message-type parse,
 * MSB-first bit I/O, station-reference 1005/1006 encode/decode) is a pure, zero-heap, host-tested core;
 * the caster server (rover connections + sourcetable) and the receiver bring-up (UBX / NMEA over UART) are
 * the ESP32 step. Generating RTCM3 *observation* (MSM) messages needs a receiver that outputs raw
 * measurements (u-blox RXM-RAWX: F9P / M8T class); a raw-less module (NEO-6/7, GT-U7) can still serve the
 * surveyed reference point + sourcetable. Default off.
 */
#ifndef PC_ENABLE_NTRIP_CASTER
#define PC_ENABLE_NTRIP_CASTER 0
#endif

/** @brief Max concurrent rover connections a caster serves corrections to (services/timing_position/gnss). */
#ifndef PC_NTRIP_MAX_ROVERS
#define PC_NTRIP_MAX_ROVERS 4
#endif

// The base surveys in from the receiver's GGA fixes, so the NTRIP caster needs the NMEA 0183 codec.
#define PC_NEED_NMEA0183 (PC_ENABLE_NMEA0183 || PC_ENABLE_NTRIP_CASTER)

/** @brief Max length (incl. NUL) of an NTRIP mountpoint name the caster serves. */
#ifndef PC_NTRIP_MOUNT_MAX
#define PC_NTRIP_MOUNT_MAX 32
#endif

/** @brief Max NTRIP client request size (bytes) the caster buffers while reading the request headers. */
#ifndef PC_NTRIP_REQ_MAX
#define PC_NTRIP_REQ_MAX 512
#endif

/** @brief Max distinct mountpoints a single caster serves (each = one RTCM stream). */
#ifndef PC_NTRIP_MAX_MOUNTS
#define PC_NTRIP_MAX_MOUNTS 2
#endif

/**
 * @brief Per-direction relay buffer size (bytes) for services/net/relay (PC_ENABLE_RELAY).
 *
 * Each active relay holds two buffers of this size (one per direction) for bytes read from one peer
 * but not yet accepted by the other (backpressure carry). Larger buffers raise throughput per step
 * (fewer cross-thread Tcp.conn->send marshals per KB) at the cost of RAM per concurrent relay
 * (2 * PC_RELAY_BUF * PC_RELAY_MAX_CONNS bytes).
 */
#ifndef PC_RELAY_BUF
#define PC_RELAY_BUF 2048
#endif

/**
 * @brief Max pc_relay_step passes per poll for the relay listener (PC_ENABLE_RELAY).
 *
 * One poll drains up to this many PC_RELAY_BUF chunks per direction, so a single event forwards the
 * whole buffered origin RX ring (PC_CLIENT_RX_BUF) instead of one chunk - the difference between a
 * ~0.4 Mbps and a multi-Mbps port-forward. Bounded so one busy bridge cannot starve the others.
 */
#ifndef PC_RELAY_DRAIN_MAX
#define PC_RELAY_DRAIN_MAX 8
#endif

/**
 * @brief Max published relay ports (bind table size) for the relay listener (PC_ENABLE_RELAY).
 *
 * Each pc_relay_publish() call binds one listener port to one origin `host:port`. This caps how
 * many distinct ports the device can front at once.
 */
#ifndef PC_RELAY_MAX_PUBLISH
#define PC_RELAY_MAX_PUBLISH 4
#endif

/**
 * @brief Max concurrent relayed connections (bridge table size) for the relay listener
 *        (PC_ENABLE_RELAY). Each holds a pc_relay (two PC_RELAY_BUF buffers) + an origin slot.
 */
#ifndef PC_RELAY_MAX_CONNS
#define PC_RELAY_MAX_CONNS 4
#endif

/** @brief Max origin hostname length (bytes, incl. NUL) stored per published relay port. */
#ifndef PC_RELAY_HOST_MAX
#define PC_RELAY_HOST_MAX 64
#endif

/** @brief Blocking connect timeout (ms) when the relay listener dials the origin on a new inbound. */
#ifndef PC_RELAY_CONNECT_MS
#define PC_RELAY_CONNECT_MS 5000
#endif

/**
 * @brief Opt-in FTP client wire codec (PC_ENABLE_FTP).
 *
 * services/file_transfer/ftp is the pure protocol layer of an FTP client (RFC 959 + RFC 2428 EPSV/EPRT):
 * `pc_ftp_build_command` / `pc_ftp_build_port` / `pc_ftp_build_eprt` build control-channel commands,
 * `pc_ftp_parse_reply` detects a complete single- or multi-line 3-digit reply, and
 * `pc_ftp_parse_pasv` / `pc_ftp_parse_epsv` decode the data-channel address the server returns. So a
 * device can push/pull files - e.g. drip a `.nc` program to a CNC controller's FTP store. Pure
 * codec (you own the control + data sockets); host-tested. Default off.
 */
#ifndef PC_ENABLE_FTP
#define PC_ENABLE_FTP 0
#endif

/**
 * @brief Suggested FTP control-command buffer size (PC_ENABLE_FTP).
 *
 * A convenience cap for callers sizing the buffer they hand `pc_ftp_build_command`; the builders
 * are all length-checked against the caller's `cap`, so this is only a sensible default. Large
 * enough for a RETR / STOR with a long path.
 */
#ifndef PC_FTP_CMD_MAX
#define PC_FTP_CMD_MAX 256
#endif

/**
 * @brief Opt-in FTP client session driver (PC_ENABLE_FTP_SESSION, requires PC_ENABLE_FTP).
 *
 * services/file_transfer/ftp is deliberately pure - it owns no sockets. This is the other half: services/ftp_session
 * drives a real control connection through login -> TYPE I -> passive mode -> STOR -> QUIT over the
 * outbound client transport, opens the data connection the server names, and streams a payload pulled
 * from a caller-supplied source (so the bytes can come from a file, a log, or the core-dump partition
 * without the driver knowing). Separate from the codec gate because it drags in the TCP client and the
 * DNS resolver, which a build that only wanted the pure codec should not pay for. Default off.
 */
#ifndef PC_ENABLE_FTP_SESSION
#define PC_ENABLE_FTP_SESSION 0
#endif

/**
 * @brief Control-reply accumulator for the FTP session driver (PC_ENABLE_FTP_SESSION).
 *
 * services/ftp_session buffers a whole control reply here before parsing it. Multiline greetings
 * and FEAT listings are the large cases; a reply that will not fit is treated as malformed rather
 * than waited on forever.
 */
#ifndef PC_FTP_REPLY_BUF
#define PC_FTP_REPLY_BUF 512
#endif

/** @brief Bytes staged per data-channel write when the session driver streams a payload. */
#ifndef PC_FTP_CHUNK
#define PC_FTP_CHUNK 512
#endif

/** @brief Per-step timeout for the FTP session driver: connect, and each control reply. */
#ifndef PC_FTP_TIMEOUT_MS
#define PC_FTP_TIMEOUT_MS 8000
#endif

#define PC_ENABLE_FTP_SESSION_NEEDS_FTP PC_ENABLE_FTP
#if PC_ENABLE_FTP_SESSION && !PC_ENABLE_FTP_SESSION_NEEDS_FTP
#error "ProtoCore: PC_ENABLE_FTP_SESSION needs PC_ENABLE_FTP"
#endif

#if PC_ENABLE_FTP_SESSION && (PC_FTP_REPLY_BUF < 128)
#error "ProtoCore: PC_FTP_REPLY_BUF must be >= 128 (a multiline greeting needs room)"
#endif

#if PC_ENABLE_FTP_SESSION && (PC_FTP_CHUNK < 64)
#error "ProtoCore: PC_FTP_CHUNK must be >= 64"
#endif

/**
 * @brief Opt-in HTTP Cache-Control directive helpers (PC_ENABLE_HTTP_CACHE).
 *
 * services/web/httpcache is the origin-side of edge caching (RFC 9111 + RFC 8246 + RFC 5861): a
 * structured `Cache-Control` builder (`cache_control_build` + first-class presets like
 * `cache_immutable_asset` / `cache_shared`) so app routes emit correct, edge-cacheable responses
 * (hand the value to set_cache_control()), a tolerant directive parser
 * (`cache_control_parse`), and the RFC 9111 freshness-lifetime calculation. Pure text, host-tested.
 * Groundwork for the CDN roadmap; the caching tier itself is a separate piece. Default off.
 */
#ifndef PC_ENABLE_HTTP_CACHE
#define PC_ENABLE_HTTP_CACHE 0
#endif

/**
 * @brief Opt-in CDN edge-cache tier (PC_ENABLE_EDGE_CACHE, requires HTTP_CACHE).
 *
 * services/web/edge_cache is the caching reverse-proxy edge that services/web/httpcache is the origin-side
 * groundwork for: a device sits in front of a remote upstream origin, fetches a response once, and
 * serves subsequent hits from a bounded local store - honoring `Cache-Control` / `Expires` / `ETag` /
 * `Last-Modified`, revalidating stale entries with conditional requests (`If-None-Match` /
 * `If-Modified-Since` -> 304), and serving `Range` / `206` straight from the cache. A two-tier store:
 * bounded RAM (L1, hot) plus an optional dbm/WAL-backed SD tier (L2, persistent, when PC_ENABLE_DBM
 * is set). Misses/revalidations fetch the origin asynchronously (the client request is suspended and
 * resumed from the poll loop, never stalling the worker) and always fail open. Zero heap. Default off.
 */
#ifndef PC_ENABLE_EDGE_CACHE
#define PC_ENABLE_EDGE_CACHE 0
#endif
#define PC_ENABLE_EDGE_CACHE_NEEDS_HTTP_CACHE PC_ENABLE_HTTP_CACHE
#if PC_ENABLE_EDGE_CACHE && !PC_ENABLE_EDGE_CACHE_NEEDS_HTTP_CACHE
#error "ProtoCore: PC_ENABLE_EDGE_CACHE needs PC_ENABLE_HTTP_CACHE"
#endif
#define PC_ENABLE_EDGE_CACHE_NEEDS_HTTP_CLIENT PC_ENABLE_HTTP_CLIENT
#if PC_ENABLE_EDGE_CACHE && !PC_ENABLE_EDGE_CACHE_NEEDS_HTTP_CLIENT
#error "ProtoCore: PC_ENABLE_EDGE_CACHE needs PC_ENABLE_HTTP_CLIENT"
#endif
// Opt-in TLS upstream origins: when set, a mapped `https://` origin is fetched over the shared client-TLS
// session (pc_tls_csess) instead of being rejected. One outbound TLS origin fetch at a time (the session is
// a singleton, shared with MQTTS/wss); the handshake blocks the worker briefly at connect (like the MQTT/WS
// clients). Needs the TLS engine + the ~48 KB arena - an S3 / PSRAM board is recommended.
#ifndef PC_ENABLE_EDGE_ORIGIN_TLS
#define PC_ENABLE_EDGE_ORIGIN_TLS 0
#endif
#define PC_ENABLE_EDGE_ORIGIN_TLS_NEEDS_EDGE_CACHE PC_ENABLE_EDGE_CACHE
#if PC_ENABLE_EDGE_ORIGIN_TLS && !PC_ENABLE_EDGE_ORIGIN_TLS_NEEDS_EDGE_CACHE
#error "ProtoCore: PC_ENABLE_EDGE_ORIGIN_TLS needs PC_ENABLE_EDGE_CACHE"
#endif
#define PC_ENABLE_EDGE_ORIGIN_TLS_NEEDS_TLS PC_ENABLE_TLS
#if PC_ENABLE_EDGE_ORIGIN_TLS && !PC_ENABLE_EDGE_ORIGIN_TLS_NEEDS_TLS
#error "ProtoCore: PC_ENABLE_EDGE_ORIGIN_TLS needs PC_ENABLE_TLS"
#endif
/* Derived sizing for the edge cache. Macros, not constexpr: PC_EDGE_FETCH_BUF's default is
 * computed from PC_EDGE_MESH_RESP_MAX below and the requirement is enforced with an #error,
 * and the preprocessor can evaluate neither `constexpr` nor `sizeof`. SRCBANNED rule 18. */

/** @brief Worst-case serialized L2 entry (edge_sd_serialize). */
#define PC_EDGE_SD_VALUE_MAX                                                                                           \
    (1 /*version*/ + 2 /*status*/ + 2 /*body_len*/ + 7u * 2u /*str lengths*/ + PC_EDGE_KEY_MAX + PC_EDGE_CTYPE_MAX +   \
     PC_EDGE_ETAG_MAX + PC_EDGE_LASTMOD_MAX + PC_EDGE_CENC_MAX + PC_EDGE_VARY_MAX + PC_EDGE_VARY_MAX +                 \
     PC_EDGE_BODY_MAX)

/** @brief Fixed timing trailer prepended to a mesh entry frame (age propagation). */
#define PC_EDGE_MESH_TRAILER (8 /*date*/ + 8 /*expires*/ + 4 /*lifetime_s*/ + 4 /*age_hdr*/ + 4 /*age*/)

/** @brief Worst-case mesh entry frame (trailer + a full serialized entry). */
#define PC_EDGE_MESH_ENTRY_MAX (PC_EDGE_MESH_TRAILER + PC_EDGE_SD_VALUE_MAX)

/** @brief Worst-case mesh response frame (header + entry on a HIT). */
#define PC_EDGE_MESH_RESP_MAX (2 + 1 + 1 + 2 + PC_EDGE_MESH_ENTRY_MAX)

/** @brief Worst-case mesh request frame (bounded request-header snapshot for Vary matching). */
#define PC_EDGE_MESH_REQ_MAX (2 + 1 + 1 + 32 + 2 + PC_EDGE_KEY_MAX + 2 + PC_MESH_HDRS_MAX)

/* A fetch slot reuses its origin buffer for the mesh query (the two phases never overlap),
 * so with the mesh on the buffer must also hold a mesh response. Deriving the floor here is
 * what makes PC_ENABLE_EDGE_MESH work out of the box instead of failing to compile. */
#if PC_ENABLE_EDGE_MESH
#define PC_EDGE_FETCH_BUF_MIN ((PC_EDGE_MESH_RESP_MAX) > 2560 ? (PC_EDGE_MESH_RESP_MAX) : 2560)
#else
#define PC_EDGE_FETCH_BUF_MIN 2560
#endif

// PC_EDGE_CACHE_SLOTS and PC_EDGE_BODY_MAX come from board_drivers/board_profiles/ (classic floor, raised
// per chip/PSRAM by board_profile.h above); override with -D as usual.
#ifndef PC_EDGE_KEY_MAX
#define PC_EDGE_KEY_MAX 128 // largest canonical cache key (method\nhost\npath[\nquery])
#endif
#ifndef PC_EDGE_VARY_MAX
#define PC_EDGE_VARY_MAX 64 // stored Vary field-name list / captured request values (each)
#endif

/** @brief Stored Content-Type to replay. */
#ifndef PC_EDGE_CTYPE_MAX
#define PC_EDGE_CTYPE_MAX 64
#endif

/** @brief Stored validator (ETag, quotes included). */
#ifndef PC_EDGE_ETAG_MAX
#define PC_EDGE_ETAG_MAX 64
#endif

/** @brief Stored Last-Modified (RFC 1123 date). */
#ifndef PC_EDGE_LASTMOD_MAX
#define PC_EDGE_LASTMOD_MAX 40
#endif

/** @brief Stored Content-Encoding to replay (e.g. gzip). */
#ifndef PC_EDGE_CENC_MAX
#define PC_EDGE_CENC_MAX 32
#endif
#ifndef PC_EDGE_MAP_MAX
#define PC_EDGE_MAP_MAX 4 // path-prefix -> origin route mappings
#endif
#ifndef PC_EDGE_ORIGIN_URL_MAX
#define PC_EDGE_ORIGIN_URL_MAX 128 // largest origin base URL in a route mapping
#endif
// PC_EDGE_FETCH_SLOTS comes from board_drivers/board_profiles/ (classic floor, raised per chip/PSRAM).
#ifndef PC_EDGE_FETCH_BUF
#define PC_EDGE_FETCH_BUF PC_EDGE_FETCH_BUF_MIN // per-fetch origin-response accumulation buffer
#endif
#ifndef PC_EDGE_FETCH_TIMEOUT_MS
#define PC_EDGE_FETCH_TIMEOUT_MS 8000 // origin fetch deadline before fail-open
#endif
#ifndef PC_EDGE_DEFAULT_TTL_S
#define PC_EDGE_DEFAULT_TTL_S 60 // fallback freshness when no directive and no wall clock
#endif
// L2 (SD) tier: when PC_ENABLE_DBM is also set, the edge cache spills evicted entries to a dbm store
// (edge_cache_sd) so the cached set survives a reboot. Each entry serializes to ~ its body plus ~470 B of
// response metadata; for a full-body spill, PC_DBM_VAL_MAX must be >= that size (>= PC_EDGE_BODY_MAX
// + ~470). Entries that do not fit simply stay L1-only, so a small PC_DBM_VAL_MAX is safe but persists
// less. The L2 key is the 32-byte cache-key digest, so PC_DBM_KEY_MAX must be >= 32 (its default).

/**
 * @brief Opt-in mesh (sibling-cache) distribution for the edge cache (PC_ENABLE_EDGE_MESH).
 *
 * Lets a fleet of edge nodes share one warm cache: on a full local miss, a node queries its configured
 * sibling peers (over a plaintext PROTO_MESH TCP link) before hitting the origin, and pulls a
 * fresh copy from whichever peer has it - so the origin is fetched once per fleet, not once per node. Pull
 * (read-through) only: no push, no invalidation protocol, no consistency window - a stale sibling copy
 * self-expires by its own TTL and the requester re-checks freshness on arrival. The transfer carries the
 * object plus its freshness/age (RFC 9111 age propagation), so a sibling-fresh object serves for its
 * remaining lifetime with zero origin contact. A serving node answers only from its local store (one hop,
 * never re-querying its own origin/peers, so the fleet cannot loop). Peers are a static list
 * (pc_edge_cache_add_peer); auto-discovery is a follow-up. Zero heap. Default off.
 */
#ifndef PC_ENABLE_EDGE_MESH
#define PC_ENABLE_EDGE_MESH 0
#endif
#define PC_ENABLE_EDGE_MESH_NEEDS_EDGE_CACHE PC_ENABLE_EDGE_CACHE
#if PC_ENABLE_EDGE_MESH && !PC_ENABLE_EDGE_MESH_NEEDS_EDGE_CACHE
#error "ProtoCore: PC_ENABLE_EDGE_MESH needs PC_ENABLE_EDGE_CACHE"
#endif
// PC_MESH_MAX_PEERS and PC_MESH_MAX_CONNS come from board_drivers/board_profiles/ (classic floor, raised
// per chip/PSRAM).
#ifndef PC_MESH_QUERY_MS
#define PC_MESH_QUERY_MS 300 // per-peer query deadline before moving on (miss) / to the origin
#endif
#ifndef PC_MESH_HOST_MAX
#define PC_MESH_HOST_MAX 64 // largest sibling peer host string
#endif
#ifndef PC_MESH_HDRS_MAX
#define PC_MESH_HDRS_MAX                                                                                               \
    384 // request-header snapshot carried to a peer so it can match Vary variants
        // (headers past the cap are dropped -> at worst a safe mesh miss, never wrong content)
#endif

/**
 * @brief Opt-in SMB2 client (PC_ENABLE_SMB).
 *
 * network_drivers/application/smb is an SMB2 client (MS-SMB2) so a device can read/write files on a Windows share -
 * e.g. a CNC controller's program store. The full read/write-a-file path: the Direct-TCP transport
 * frame + SMB2 sync header, NEGOTIATE, the two-round NTLMv2 SESSION_SETUP (NTLM digests MD4/MD5/
 * HMAC-MD5, the NTLMv2 response, the NTLMSSP messages, SPNEGO wrapping), TREE_CONNECT, CREATE, READ,
 * WRITE, and CLOSE. smb_client ties the codecs into the exchange over a send/recv seam (host-tested
 * with a scripted mock server); you own the TCP socket (pc_client). All little-endian. Default off.
 */
#ifndef PC_ENABLE_SMB
#define PC_ENABLE_SMB 0
#endif

/**
 * @brief SMB2 client work-buffer size (bytes) for smb_client's request/response framing.
 *
 * Two buffers of this size live on the stack during a call, plus a few half-size scratch buffers for
 * the NTLM auth tokens, so the engine needs roughly 4x this in stack. 1024 covers the NEGOTIATE ->
 * SESSION_SETUP -> TREE_CONNECT -> CREATE handshake; raise it if a server's SPNEGO/target-info token
 * or your share path is unusually large.
 */
#ifndef PC_SMB_BUF
#define PC_SMB_BUF 1024
#endif

/**
 * @brief Opt-in SAE J2735 V2X codec (PC_ENABLE_J2735).
 *
 * When set, services/transportation/j2735 provides the ASN.1 UPER (Unaligned Packed Encoding Rules) bit-level primitive
 * codec (constrained INTEGER / BOOLEAN / bit fields) and, on top of it, the J2735 BSMcore safety-message
 * block (msgCnt / id / secMark / lat / long / elev / speed / heading) encode + decode, for connected-
 * vehicle messaging. Pure codec (the DSRC / C-V2X radio is an external module). Default off.
 */
#ifndef PC_ENABLE_J2735
#define PC_ENABLE_J2735 0
#endif

/**
 * @brief Opt-in NEMA TS 2 traffic-cabinet SDLC frame codec (PC_ENABLE_NEMA_TS2).
 *
 * When set, services/transportation/nema_ts2 builds/validates the TS 2 SDLC bus frames ([address][control][frame-type]
 * [data][CRC-16/X-25]) that link a traffic-signal controller to the MMU, BIUs, and detector racks. Pure
 * codec (the synchronous serial PHY + BIU timing are hardware-gated). Default off.
 */
#ifndef PC_ENABLE_NEMA_TS2
#define PC_ENABLE_NEMA_TS2 0
#endif

/**
 * @brief Opt-in GE Fanuc SNP (Series Ninety Protocol) serial frame codec (PC_ENABLE_SNP).
 *
 * When set, services/fieldbus/snp builds/validates the SNP master-slave serial frame ([control][length][data]
 * [arithmetic-sum BCC]) for reading/writing registers on a GE Fanuc Series 90 (90-30/90-70) PLC over
 * RS-485. Pure codec (the UART transport + SNP-X session are the device step). Default off.
 */
#ifndef PC_ENABLE_SNP
#define PC_ENABLE_SNP 0
#endif

/**
 * @brief Opt-in AutomationDirect / Koyo DirectNET serial frame codec (PC_ENABLE_DIRECTNET).
 *
 * When set, services/fieldbus/directnet builds/validates the DirectNET master-slave serial frames - the header
 * (SOH + slave/type/address/blocks ASCII-hex + ETB + LRC) and the data frame (STX + data + ETX + LRC) -
 * for V-memory read/write on an AutomationDirect DirectLOGIC PLC. Pure codec (the UART transport +
 * ACK/NAK handshake are the device step). Default off.
 */
#ifndef PC_ENABLE_DIRECTNET
#define PC_ENABLE_DIRECTNET 0
#endif

/**
 * @brief Opt-in IEEE 2030.5 (Smart Energy Profile 2.0) resource codec (PC_ENABLE_SEP2).
 *
 * When set, services/energy/sep2 builds the core 2030.5 XML resource documents (DeviceCapability, EndDevice,
 * DERControl) in the urn:ieee:std:2030.5:ns namespace, so the web server is a 2030.5 smart-grid
 * server/client over the existing HTTP stack (DER dispatch / curtailment). Pure text framing. Default off.
 */
#ifndef PC_ENABLE_SEP2
#define PC_ENABLE_SEP2 0
#endif

/**
 * @brief Opt-in PROFINET DCP (Discovery and Configuration Protocol) frame codec (PC_ENABLE_PROFINET).
 *
 * When set, services/fieldbus/profinet builds/parses the DCP frames (10-octet header + option/suboption blocks)
 * PROFINET uses to discover and name IO-Devices over raw L2 (ethertype 0x8892) - Identify request/
 * response and Set (assign NameOfStation / IP). Pure codec (the raw-L2 transmit via services/fieldbus/rawl2 +
 * the vendor Ethernet driver is the device step). Default off.
 */
#ifndef PC_ENABLE_PROFINET
#define PC_ENABLE_PROFINET 0
#endif

/**
 * @brief Opt-in NTCIP transportation-device object identifiers (PC_ENABLE_NTCIP).
 *
 * When set, services/transportation/ntcip provides the NTCIP (National Transportation Communications for ITS Protocol)
 * object OID definitions for the common device classes - NTCIP 1202 (actuated signal controller: phases,
 * timing, live states) and 1203 (dynamic message sign) - plus an OID builder, so an app exposes them via
 * the shipped SNMP agent (services/net/snmp). Pure OID data. Default off.
 */
#ifndef PC_ENABLE_NTCIP
#define PC_ENABLE_NTCIP 0
#endif

/**
 * @brief Opt-in OpenADR 3.0 (Automated Demand Response) JSON codec (PC_ENABLE_OPENADR).
 *
 * When set, services/energy/openadr builds the OpenADR 3.0 event (a demand-response signal: programID +
 * eventName + interval payload points) and report (a VEN reading back to the VTN) JSON objects into a
 * caller buffer, over the existing HTTP client/server + OAuth2. Pure JSON framing. Default off.
 */
#ifndef PC_ENABLE_OPENADR
#define PC_ENABLE_OPENADR 0
#endif

/**
 * @brief Opt-in IEC 61850 MMS PDU codec (PC_ENABLE_MMS).
 *
 * When set, services/energy/mms builds/parses the MMS (ISO 9506) confirmed-request/response Read PDUs
 * (BER-encoded, the ACSI client/server core of IEC 61850) - pc_mms_read_request builds a Read of a
 * named Data Object, pc_mms_read_response the data reply. Carried over ISO-on-TCP (TPKT + COTP via
 * the shipped services/fieldbus/cotp) on port 102. Pure BER codec. Default off.
 */
#ifndef PC_ENABLE_MMS
#define PC_ENABLE_MMS 0
#endif

/**
 * @brief Opt-in CC-Link (CLPA) cyclic fieldbus frame codec (PC_ENABLE_CCLINK).
 *
 * When set, services/fieldbus/cclink builds/validates the CC-Link cyclic frame ([station][command][RX/RY bit
 * data][RWr/RWw word data][sum checksum]) a Mitsubishi CC-Link master exchanges with remote stations
 * over RS-485, plus bit/word process-image accessors. Pure codec (the RS-485 timing + CC-Link IE Field
 * PHY are hardware-gated). Default off.
 */
#ifndef PC_ENABLE_CCLINK
#define PC_ENABLE_CCLINK 0
#endif

/**
 * @brief Opt-in Ethernet POWERLINK (EPSG) basic frame codec (PC_ENABLE_POWERLINK).
 *
 * When set, services/fieldbus/powerlink builds/parses the EPL basic frames ([messageType][dest][source][payload])
 * of the isochronous managed-node cycle - SoC (start of cycle), PReq (poll request), PRes (poll
 * response with process data), SoA (start of async) - over raw L2 (ethertype 0x88AB, on the shipped
 * services/fieldbus/rawl2). Pure codec (the raw-L2 transmit + isochronous timing are the device step). Default off.
 */
#ifndef PC_ENABLE_POWERLINK
#define PC_ENABLE_POWERLINK 0
#endif

/**
 * @brief Opt-in SERCOS III motion-bus telegram codec (PC_ENABLE_SERCOS).
 *
 * When set, services/fieldbus/sercos builds/parses the SERCOS III MDT/AT telegrams (type + phase + cycle + cyclic
 * device data) the real-time drive/motion bus exchanges over raw L2 (ethertype 0x88CD, on the shipped
 * services/fieldbus/rawl2), plus the IDN (IDentification Number) encode/decode for drive-parameter addressing.
 * Pure codec (the isochronous timing + ring topology are hardware-gated). Default off.
 */
#ifndef PC_ENABLE_SERCOS
#define PC_ENABLE_SERCOS 0
#endif

/**
 * @brief Opt-in PROFIBUS-DP FDL telegram codec (PC_ENABLE_PROFIBUS).
 *
 * When set, services/fieldbus/profibus builds/validates the PROFIBUS-DP FDL telegrams - SD1 (no-data: SD1 DA SA
 * FC FCS ED) and SD2 (variable-data: SD2 LE LEr SD2 DA SA FC data FCS ED, arithmetic-sum FCS) - a
 * Siemens DP master exchanges with slaves over RS-485 (the DP-V0 cyclic I/O exchange). Pure codec (the
 * RS-485 timing + DP state machine are the device step). Default off.
 */
#ifndef PC_ENABLE_PROFIBUS
#define PC_ENABLE_PROFIBUS 0
#endif

/**
 * @brief Opt-in LonWorks / LON-IP (ISO/IEC 14908) network-variable codec (PC_ENABLE_LONWORKS).
 *
 * When set, services/fieldbus/lonworks builds/parses the LonTalk network-variable PDU ([msg-code][14-bit
 * selector][value]) that a building-automation device exchanges - over LON/IP (14908-4) UDP, so no
 * Neuron chip is needed - plus the common SNVT scalar encodings (SNVT_temp, SNVT_switch). Pure codec
 * (the UDP transport is the shipped UDP layer). Default off.
 */
#ifndef PC_ENABLE_LONWORKS
#define PC_ENABLE_LONWORKS 0
#endif

/**
 * @brief Opt-in Modbus Plus HDLC token-bus frame codec (PC_ENABLE_MBPLUS).
 *
 * When set, services/fieldbus/mbplus builds/validates the Modbus Plus HDLC frame (7E addr ctrl payload CRC-16/X-25
 * 7E) that Schneider's token-passing peer bus exchanges, plus the token-rotation helper (next station in
 * the logical ring). Reuses the shipped Modbus PDU model for the data. Pure codec (the 1 Mbit/s bus is
 * hardware-gated). Default off.
 */
#ifndef PC_ENABLE_MBPLUS
#define PC_ENABLE_MBPLUS 0
#endif

/**
 * @brief Opt-in INTERBUS summation-frame fieldbus codec (PC_ENABLE_INTERBUS).
 *
 * When set, services/fieldbus/interbus assembles/disassembles the INTERBUS summation frame (loopback word +
 * per-device 16-bit process-image slices + CRC-16/CCITT FCS) of the Phoenix Contact ring fieldbus,
 * where every device is a shift-register slice of one circulating frame. Pure codec (the physical ring
 * clocking is hardware-gated). Default off.
 */
#ifndef PC_ENABLE_INTERBUS
#define PC_ENABLE_INTERBUS 0
#endif

/**
 * @brief Opt-in ICCP / TASE.2 (IEC 60870-6) inter-control-center telemetry codec (PC_ENABLE_ICCP).
 *
 * When set, services/energy/iccp builds the TASE.2 Data_Value BER structures - StateQ (a discrete state +
 * quality) and RealQ (a scaled real + quality), each with an optional timestamp - the indication points
 * a control center transfers as MMS Reads (on the shipped services/energy/mms + services/fieldbus/cotp). Pure BER
 * codec. Default off.
 */
#ifndef PC_ENABLE_ICCP
#define PC_ENABLE_ICCP 0
#endif

/**
 * @brief Opt-in IEEE 1609 WAVE (WSMP + 1609.2 envelope) codec (PC_ENABLE_WAVE).
 *
 * When set, services/transportation/wave builds/parses the IEEE 1609 vehicular-radio framing that carries J2735: the
 * 1609.3 WSMP header (version + P-encoded PSID + length + payload) and the 1609.2 secured-message
 * envelope header (version + content type). Pairs with services/j2735. Pure codec (the DSRC / C-V2X
 * radio is an external module). Default off.
 */
#ifndef PC_ENABLE_WAVE
#define PC_ENABLE_WAVE 0
#endif

/**
 * @brief Opt-in UTMC (Urban Traffic Management and Control) common-database codec (PC_ENABLE_UTMC).
 *
 * When set, services/transportation/utmc builds/parses the UTMC common-database HTTP+XML messages - a UTMCRequest for
 * an object id and a UTMCResponse carrying the object value + a data-quality flag + a timestamp - the UK
 * modular framework for sharing traffic data across municipal systems, over the existing HTTP server.
 * Pure text framing. Default off.
 */
#ifndef PC_ENABLE_UTMC
#define PC_ENABLE_UTMC 0
#endif

/**
 * @brief Opt-in OCIT-Outstations message codec (PC_ENABLE_OCIT).
 *
 * When set, services/transportation/ocit builds/parses the OCIT (DE/AT/CH road-traffic-control) object messages
 * ([msg-type][object-type][instance][data-type][value]) between central traffic computers and field
 * controllers / detectors, with typed values (bool / byte / u16 / u32 / octets). Pure codec (the OCIT
 * transport is the shipped transport). Default off.
 */
#ifndef PC_ENABLE_OCIT
#define PC_ENABLE_OCIT 0
#endif

/**
 * @brief Opt-in ATC (Advanced Traffic Controller) field-I/O interop snapshot (PC_ENABLE_ATC).
 *
 * When set, services/machine_tool/atc exposes this device's field-I/O (a fixed table of named input/output points it
 * already gathers via the NTCIP / NEMA-TS2 / gpio services) to an ATC Linux engine over the existing
 * HTTP surface: pc_atc_snapshot_json serializes the FIO map as JSON, and pc_atc_set_output drives
 * an output point from an ATC command. Pure interop codec (ATC is a platform spec, not a wire protocol).
 * Default off.
 */
#ifndef PC_ENABLE_ATC
#define PC_ENABLE_ATC 0
#endif

/**
 * @brief Opt-in southbound protocol-driver framework (PC_ENABLE_SOUTHBOUND).
 *
 * The uniform seam every field-device driver plugs into so the app polls/drives any southbound device
 * (a Modbus slave, a BACnet controller, a raw sensor over SPI/I2C/UART) through one facade: register a
 * SouthboundDriver (a read/write/read_block/write_block vtable + its transport ctx), then address points
 * by driver name via pc_southbound_read / _write / _read_block / _write_block. The block calls are the
 * atomic multi-point (register-matrix) path. Bounded registry (PC_SOUTHBOUND_MAX_DRIVERS, default 8),
 * no heap; Modbus master is the one such driver today. Default off.
 */
#ifndef PC_ENABLE_SOUTHBOUND
#define PC_ENABLE_SOUTHBOUND 0
#endif

/**
 * @brief Opt-in ESP32 panic / exception decoder for a live diagnostics panel (PC_ENABLE_EXC_DECODER).
 *
 * When set, server/exc_decoder parses a captured Guru Meditation panic dump (the cause, the register
 * PC + EXCVADDR, and the backtrace PC:SP frames) into a structured ExcInfo and serializes it as JSON for
 * a "/exception" panel; the browser or a build server resolves the PCs to file:line against the firmware
 * ELF (addr2line lives off-device). Pure, no heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_EXC_DECODER
#define PC_ENABLE_EXC_DECODER 0
#endif

/**
 * @brief Chunk the core-dump image is streamed out of flash in (PC_EXC_COREDUMP_CHUNK).
 *
 * pc_exc_coredump_save() copies the partition to a file this many bytes at a time from a stack
 * buffer, so a dump of any size costs no heap and never has to fit RAM.
 */
#ifndef PC_EXC_COREDUMP_CHUNK
#define PC_EXC_COREDUMP_CHUNK 512
#endif

#if PC_ENABLE_EXC_DECODER && (PC_EXC_COREDUMP_CHUNK < 64)
#error "ProtoCore: PC_EXC_COREDUMP_CHUNK must be >= 64"
#endif

/**
 * @brief Opt-in HTTP delivery optimizations (PC_ENABLE_HTTP_DELIVERY).
 *
 * Three pure cores for cheaper HTTP serving, each a real web standard: RFC 5861 stale-while-revalidate
 * (pc_delivery_swr decision + pc_delivery_cache_control header), RFC 7233 byte-range delta/offset
 * fetch (pc_delivery_range parse of X-Y / X- / -N + pc_delivery_content_range for a 206), and a
 * versioned service-worker precache manifest (pc_delivery_sw_manifest). No heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_HTTP_DELIVERY
#define PC_ENABLE_HTTP_DELIVERY 0
#endif

/**
 * @brief Most asset paths a service-worker precache manifest may list (PC_DELIVERY_PRECACHE_MAX).
 */
#ifndef PC_DELIVERY_PRECACHE_MAX
#define PC_DELIVERY_PRECACHE_MAX 16
#endif

/**
 * @brief Buffer the precache manifest JSON is built into (PC_DELIVERY_MANIFEST_BUF).
 *
 * Must hold `{"version":"..","precache":[..]}` for PC_DELIVERY_PRECACHE_MAX paths; the manifest
 * route answers 500 rather than truncating if it does not fit.
 */
#ifndef PC_DELIVERY_MANIFEST_BUF
#define PC_DELIVERY_MANIFEST_BUF 512
#endif

#if PC_ENABLE_HTTP_DELIVERY && (PC_DELIVERY_PRECACHE_MAX < 1)
#error "ProtoCore: PC_DELIVERY_PRECACHE_MAX must be >= 1"
#endif
#if PC_ENABLE_HTTP_DELIVERY && (PC_DELIVERY_MANIFEST_BUF < 64)
#error "ProtoCore: PC_DELIVERY_MANIFEST_BUF must be >= 64"
#endif

/**
 * @brief Opt-in hardware-health diagnostics (PC_ENABLE_HW_HEALTH).
 *
 * Four pure decision cores fed with samples the app reads from the hardware: a power-rail voltage-drop
 * logger (pc_hwhealth_rail_sample tracks worst droop + sag/brownout counts), a SPI-bus CRC audit with
 * hysteretic clock backoff (pc_hwhealth_spi_result halves/doubles the clock on fail/ok streaks), a
 * GPIO short-circuit test (pc_hwhealth_gpio_short: driven vs readback), and a capacitor-leakage diag
 * (pc_hwhealth_cap_leak: measured vs expected RC decay). No heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_HW_HEALTH
#define PC_ENABLE_HW_HEALTH 0
#endif

/**
 * @brief Opt-in adaptive mDNS beacon scheduling (PC_ENABLE_MDNS_ADAPTIVE).
 *
 * Pure scheduling decisions on top of the shipped mDNS service: pc_mdns_beacon_adapt backs the
 * announce interval off toward a ceiling under RF contention and recovers it when the air is quiet,
 * pc_mdns_refresh_interval gives the TTL/2 continuous-refresher cadence, pc_mdns_beacon_due says
 * when an announce is due, and pc_mdns_beacon_presleep_due says whether to announce before a sleep
 * window that would otherwise let the record lapse. Wrap-safe time math, no heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_MDNS_ADAPTIVE
#define PC_ENABLE_MDNS_ADAPTIVE 0
#endif

/**
 * @brief Opt-in dynamic socket recycling: an LRU connection-slot pool (PC_ENABLE_SOCKPOOL).
 *
 * The transport-pool half of the adaptive-networking work: services/net/sockpool keeps a fixed table of
 * connection slots and, when saturated, recycles the least-recently-used slot for a new peer
 * (pc_sockpool_acquire returns the evicted id so the transport closes it), plus touch / release /
 * find. The app owns the real sockets; this owns which slot a connection lives in and which to reclaim
 * under pressure. No heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_SOCKPOOL
#define PC_ENABLE_SOCKPOOL 0
#endif

/**
 * @brief Opt-in buffer placement policy (DRAM vs PSRAM) + SPI DMA ping-pong manager (PC_ENABLE_PSRAM_POOL).
 *
 * Pure buffer-management decisions for a PSRAM-equipped ESP32: pc_psram_place picks DRAM vs PSRAM for
 * a buffer by size, DMA requirement, and free-heap headroom (large/cold to PSRAM, small/hot + DMA to
 * DRAM, always leaving an internal-DRAM reserve), and pc_pingpong_* keeps the classic SPI DMA
 * double-buffer bookkeeping (CPU fills one buffer while DMA drains the other; swap flips their roles).
 * The actual heap_caps_calloc is the app's. No heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_PSRAM_POOL
#define PC_ENABLE_PSRAM_POOL 0
#endif

/**
 * @brief Opt-in dual-stack Happy Eyeballs destination selection (PC_ENABLE_HAPPY_EYEBALLS).
 *
 * The client-side IPv6/IPv4 fallback decision on top of the shipped pc_ip: pc_he_pref scores a
 * destination (RFC 6724 scope + family), pc_he_order sorts a candidate list and interleaves the
 * address families (RFC 8305) so successive connection attempts alternate v6/v4, and
 * pc_he_attempt_due gates the next attempt by the Connection Attempt Delay. Fast IPv6 when it works,
 * quick fallback to IPv4 when it does not. Needs PC_ENABLE_IPV6 to matter. No heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_HAPPY_EYEBALLS
#define PC_ENABLE_HAPPY_EYEBALLS 0
#endif

/**
 * @brief Opt-in 802.11 sniffer / traffic analyzer (PC_ENABLE_WIFI_SNIFFER).
 *
 * The decode + decision layer for a promiscuous-mode WiFi sniffer: pc_wifi_parse decodes an 802.11
 * MAC header (frame-control type/subtype + flags and the addresses whose roles depend on ToDS/FromDS),
 * pc_wifi_stats_* tallies frames by type for a traffic panel, and pc_wifi_should_roam decides when
 * a candidate AP is enough stronger (RSSI hysteresis) to justify channel-agility roaming. On top of
 * that, pc_wifi_scan_* is the channel-hop dwell schedule and pc_wifi_survey_* is the per-channel
 * RSSI/traffic survey that supplies the roam candidate. With PC_ENABLE_PROMISC also set, the live
 * binding (pc_wifi_sniffer_begin / _tick / _end) drives the promiscuous-capture owner
 * (services/radio/promisc) so a channel-hopping sniff runs on hardware. No heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_WIFI_SNIFFER
#define PC_ENABLE_WIFI_SNIFFER 0
#endif

/**
 * @brief Channels tracked by the WiFi sniffer's per-channel survey (PC_WIFI_SNIFFER_MAX_CHANNELS).
 *
 * 14 covers the full 2.4 GHz channel plan (1-14); lower it to the channels actually swept to shrink
 * the survey table (each entry is 11 bytes).
 */
#ifndef PC_WIFI_SNIFFER_MAX_CHANNELS
#define PC_WIFI_SNIFFER_MAX_CHANNELS 14
#endif

#if PC_ENABLE_WIFI_SNIFFER && ((PC_WIFI_SNIFFER_MAX_CHANNELS < 1) || (PC_WIFI_SNIFFER_MAX_CHANNELS > 14))
#error "ProtoCore: PC_WIFI_SNIFFER_MAX_CHANNELS must be 1..14"
#endif

/**
 * @brief Opt-in multi-interface egress selection / failover policy (PC_ENABLE_LINK_MANAGER).
 *
 * The policy that drives which interface carries traffic once a device has more than one (a wired
 * Ethernet PHY alongside WiFi STA / softAP): server/signaling/link_manager keeps a small table of interfaces
 * (kind + priority + up/down) and deterministically selects the best link that is up, escalating to a
 * higher-priority interface when it comes up and failing over when it drops, reporting only real
 * transitions so the app reconfigures the netif once. The PHY bring-up stays the app's. No
 * heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_LINK_MANAGER
#define PC_ENABLE_LINK_MANAGER 0
#endif

/**
 * @brief Opt-in CC1101 sub-GHz radio driver (PC_ENABLE_CC1101).
 *
 * A gateway radio plugin (PC_ENABLE_GATEWAY) for the TI CC1101 300-928 MHz transceiver over SPI:
 * services/radio/cc1101 drives the chip's SPI header protocol (config registers, command strobes, status
 * registers, TX/RX FIFO) - reset + apply a SmartRF register table + set channel + verify VERSION
 * (pc_cc1101_init), send a variable-length packet (pc_cc1101_send), poll TX-done, enter RX, and read a packet
 * with appended RSSI/LQI (pc_cc1101_recv), plus the RSSI-to-dBm decode. The huge modem config is a
 * caller-supplied register table. Host-tested against a mock; the RF link needs the module. Default off.
 */
#ifndef PC_ENABLE_CC1101
#define PC_ENABLE_CC1101 0
#endif

/**
 * @brief Opt-in FDC2114/2214 capacitance-to-digital field sensor (PC_ENABLE_FDC2214).
 *
 * A field-perturbation sensing peripheral: services/peripherals/fdc2214 decodes the FDC2x14's 28-bit conversion
 * result (a capacitance shift moves the LC-tank frequency, giving contactless proximity / liquid-level /
 * material sensing) - pc_fdc2214_data combines the register pair, pc_fdc2214_error pulls the flags,
 * pc_fdc2214_sensor_freq_hz scales to frequency, and pc_fdc2214_build_config emits a single-channel
 * bring-up; the ESP32 binding replays it and reads the channel over I2C. Pure codec host-tested. Default off.
 */
#ifndef PC_ENABLE_FDC2214
#define PC_ENABLE_FDC2214 0
#endif

/**
 * @brief Opt-in LDC1614 inductance-to-digital field sensor (PC_ENABLE_LDC1614).
 *
 * A field-perturbation sensing peripheral: services/peripherals/ldc1614 decodes the LDC1614's 28-bit conversion
 * result (a nearby conductor changes the coil inductance via eddy currents, giving contactless metal
 * proximity / displacement / EM-field sensing) - pc_ldc1614_data combines the register pair, pc_ldc1614_error
 * pulls the flags, pc_ldc1614_sensor_freq_hz scales to frequency, and pc_ldc1614_build_config emits a
 * single-channel bring-up; the ESP32 binding replays it and reads the channel over I2C. Pure codec
 * host-tested. Default off.
 */
#ifndef PC_ENABLE_LDC1614
#define PC_ENABLE_LDC1614 0
#endif

/**
 * @brief Opt-in VL53L0X optical time-of-flight ranging sensor (PC_ENABLE_VL53L0X).
 *
 * A field-perturbation sensing peripheral for contactless distance / gesture: services/peripherals/vl53l0x decodes
 * the ST VL53L0X ranging registers - pc_vl53l0x_range_mm combines the range byte pair, pc_vl53l0x_data_ready
 * decodes the interrupt-status byte, and pc_vl53l0x_range_valid checks the device range-status field; the
 * ESP32 binding verifies the model id, starts continuous ranging, and reads the distance over I2C.
 * Default-settings ranging (ST's tuning blob is not applied). Pure codec host-tested. Default off.
 */
#ifndef PC_ENABLE_VL53L0X
#define PC_ENABLE_VL53L0X 0
#endif

/**
 * @brief Opt-in receive-only radio channel sniffer to pcap (PC_ENABLE_RADIO_SNIFF).
 *
 * Feeds frames pulled off the air by the RF gateway drivers (CC1101 / LoRa / 802.15.4) in receive-only
 * mode into the capture pipeline: services/radio/radio_sniff wraps each 802.15.4 MAC frame in the Wireshark
 * TAP pseudo-header (carrying per-frame RSSI + channel) and a pcap record so the forwarded stream is a
 * valid .pcap. pc_radiosniff_global writes the DLT-TAP global header and pc_radiosniff_tap_record
 * writes one record. Pure framing (no heap/stdlib); the radio drivers own the receive. Default off.
 */
#ifndef PC_ENABLE_RADIO_SNIFF
#define PC_ENABLE_RADIO_SNIFF 0
#endif

/**
 * @brief Opt-in Bluetooth ATT protocol codec + GATT characteristic bridge (PC_ENABLE_BLE_GATT).
 *
 * The wire protocol under GATT for bridging the on-chip BLE radio to the web: services/radio/ble_gatt builds
 * and parses the common ATT PDUs (read / write / notify / error, Bluetooth Core Vol 3 Part F) and
 * serializes a GATT characteristic table as JSON for the web stack (att_read_req / att_write_req /
 * att_notify / att_error_rsp / att_parse / pc_gatt_char_json). The BLE stack owns the radio; this owns the
 * ATT bytes + the northbound JSON. Pure, no heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_BLE_GATT
#define PC_ENABLE_BLE_GATT 0
#endif

/**
 * @brief Opt-in TLS version negotiation + pinned cipher-suite policy (PC_ENABLE_TLS_POLICY).
 *
 * A policy layer on top of the mbedTLS-backed transport TLS (which already runs the 1.2 / 1.3 record +
 * handshake): services/security/tls_policy pins the version to an audited [min,max] and makes the negotiated
 * version observable (pc_tls_negotiate_version / pc_tls_version_name), and pins the cipher suites
 * to an audited allowlist selected by server preference (pc_tls_select_cipher), with an AEAD-only
 * classifier (pc_tls_is_aead) for a hardened profile. Pure, host-tested; the app feeds the results to
 * the mbedTLS config. Default off.
 */
#ifndef PC_ENABLE_TLS_POLICY
#define PC_ENABLE_TLS_POLICY 0
#endif

/**
 * @brief Opt-in Wi-SUN FAN border-router connector (PC_ENABLE_WISUN).
 *
 * Wi-SUN FAN is an IPv6/UDP/CoAP mesh terminated by a border router, so the connector rides the existing
 * IP stack rather than driving a radio: services/radio/wisun keeps a table of FAN nodes (their pc_ip addresses +
 * join state) behind the border router and builds the CoAP client requests to their resources
 * (pc_wisun_build_coap frames an RFC 7252 header + Uri-Path options + payload; the CoAP service ships only a
 * server). pc_wisun_nodes_json exposes the mesh to the web. The app sends the built PDU over pc_udp; the
 * chosen devboard only sets which border router you point at. Pure, no heap/stdlib. Default off.
 */
#ifndef PC_ENABLE_WISUN
#define PC_ENABLE_WISUN 0
#endif

/**
 * @brief Opt-in fixed-RAM rotating log buffer with severity traps (PC_ENABLE_LOGBUF).
 *
 * Default off. When set, server/logbuf keeps the last PC_LOG_LINES log lines
 * in a fixed ring (oldest pruned on overflow - no heap, bounded), dumps them
 * oldest-first for a `/logs` endpoint, and fires a trap callback when a line is
 * logged at/above a severity threshold (forward criticals as an SNMP trap /
 * webhook). The ring + trap logic is pure and host-tested.
 */
#ifndef PC_ENABLE_LOGBUF
#define PC_ENABLE_LOGBUF 0
#endif

/** @brief Number of log lines retained in the ring. */
#ifndef PC_LOG_LINES
#define PC_LOG_LINES 32
#endif

/** @brief Maximum length of one stored log line (bytes, including null). */
#ifndef PC_LOG_LINE_LEN
#define PC_LOG_LINE_LEN 96
#endif

/**
 * @brief Compile-time severity floor for the PC_LOG* macros (shared_primitives/log.h).
 *
 * The values are ordered low -> high and match pc_log_level's, so a level is usable both in the
 * preprocessor (which cannot see a constexpr) and as the runtime argument. PC_NONE sits above ERROR
 * so that the default discards everything.
 */
#define PC_LOG_LEVEL_DEBUG 0
#define PC_LOG_LEVEL_INFO 1
#define PC_LOG_LEVEL_WARN 2
#define PC_LOG_LEVEL_ERROR 3
#define PC_LOG_LEVEL_NONE 4

/**
 * @brief Lowest severity the PC_LOG* macros emit code for.
 *
 * A call below this floor is discarded by the preprocessor: no call, no formatting, and no format
 * string left in flash, because the discarded form only names its arguments inside `sizeof` - an
 * unevaluated context that still type-checks them. So instrumentation can be left in the source
 * permanently and costs exactly nothing in a build that does not want it, which is the point.
 *
 * Defaults to PC_NONE: opt in per build (e.g. -DPC_LOG_LEVEL=PC_LOG_LEVEL_WARN).
 */
#ifndef PC_LOG_LEVEL
#define PC_LOG_LEVEL PC_LOG_LEVEL_NONE
#endif

#if (PC_LOG_LEVEL < PC_LOG_LEVEL_DEBUG) || (PC_LOG_LEVEL > PC_LOG_LEVEL_NONE)
#error "ProtoCore: PC_LOG_LEVEL must be one of the PC_LOG_LEVEL_* constants"
#endif

/**
 * @brief Opt-in schema-driven config export / restore (PC_ENABLE_CONFIG_IO).
 *
 * Default off. Requires PC_ENABLE_CONFIG_STORE. The app declares a fixed schema
 * (key + type); services/storage/config_io serializes the current values to a portable
 * `key=value` text blob (backup / migrate) and parses one back into the store
 * (restore / bulk template). Schema-driven rather than enumerating NVS, so it
 * stays deterministic and zero-heap; the serialize / parse is host-tested.
 */
#ifndef PC_ENABLE_CONFIG_IO
#define PC_ENABLE_CONFIG_IO 0
#endif

/** @brief Authenticated OTA firmware update (streaming POST to the ESP32 Update API). */
#ifndef PC_ENABLE_OTA
#define PC_ENABLE_OTA 0
#endif

/**
 * @brief Opt-in OTA rollback protection / soft-brick safeguard (PC_ENABLE_OTA_ROLLBACK).
 *
 * Default off. After an OTA update the new image boots in PENDING_VERIFY; this
 * service confirms it (esp_ota_mark_app_valid) once a self-test passes, or rolls
 * back to the previous image if the self-test fails or the confirm window elapses
 * without success - so a bad update self-heals instead of soft-bricking. The
 * decision logic is pure and host-tested; the commit / rollback use esp_ota_ops.
 * Requires the bootloader's app-rollback support (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
 */
#ifndef PC_ENABLE_OTA_ROLLBACK
#define PC_ENABLE_OTA_ROLLBACK 0
#endif

/** @brief Confirm window (ms): a pending image not confirmed within this rolls back. */
#ifndef PC_OTA_CONFIRM_WINDOW_MS
#define PC_OTA_CONFIRM_WINDOW_MS 30000
#endif

/**
 * @brief Opt-in TOTP two-factor auth (RFC 6238) (PC_ENABLE_TOTP).
 *
 * Default off. services/security/totp computes and verifies time-based one-time passwords
 * (HMAC-SHA1 over the existing SHA-1, Google Authenticator compatible) and decodes
 * base32 shared secrets, for a second factor on top of password / JWT auth. Pure
 * and host-tested against the RFC 6238 vectors; the verifier checks a +/- step
 * window for clock skew.
 */
#ifndef PC_ENABLE_TOTP
#define PC_ENABLE_TOTP 0
#endif

/**
 * @brief Opt-in outbound webhooks / IFTTT (PC_ENABLE_WEBHOOK).
 *
 * Default off. Needs PC_ENABLE_HTTP_CLIENT to actually send: the API always
 * compiles, but without the HTTP client pc_webhook_post() returns -1.
 * services/net/webhook builds an IFTTT Maker URL and a value1/value2/value3 JSON
 * payload (pure, host-tested) and fires them - or any JSON to any URL - via the
 * outbound http_client (POST). Use it to
 * push an event from the device to IFTTT, a Slack/Discord hook, or your own API.
 */
#ifndef PC_ENABLE_WEBHOOK
#define PC_ENABLE_WEBHOOK 0
#endif

/**
 * @brief Opt-in radio power controls (PC_ENABLE_RADIO_POWER).
 *
 * Default off. network_drivers/physical/radio_power applies the WiFi modem-sleep mode and an
 * optional max-TX-power cap in one call - trade throughput/latency for lower average
 * power on a battery device. The mode names are host-tested; the apply needs a vendor
 * radio backend.
 */
#ifndef PC_ENABLE_RADIO_POWER
#define PC_ENABLE_RADIO_POWER 0
#endif

/** @brief WiFi modem-sleep mode: 0 = none (max perf), 1 = min modem, 2 = max modem. */
#ifndef PC_RADIO_WIFI_PS
#define PC_RADIO_WIFI_PS 0
#endif

/** @brief Max TX power cap in dBm (2..20); 0 = leave the platform default. */
#ifndef PC_RADIO_MAX_TX_DBM
#define PC_RADIO_MAX_TX_DBM 0
#endif

/**
 * @brief Opt-in DNS resolver with answer verification (PC_ENABLE_DNS_RESOLVER).
 *
 * Default off. network_drivers/network/dns_resolver resolves a hostname to an IPv4 address (lwIP
 * dns_gethostbyname, marshalled to tcpip_thread like the http_client) and can
 * reject suspicious answers - 0.0.0.0, broadcast, loopback, multicast - which are
 * spoofing / DNS-rebinding indicators for a remote host. The address classifier /
 * verifier is pure and host-tested; the resolve is ESP32-only (blocking, so call
 * it off the request hot path).
 */
#ifndef PC_ENABLE_DNS_RESOLVER
#define PC_ENABLE_DNS_RESOLVER 0
#endif

/** @brief DNS resolve timeout in milliseconds. */
#ifndef PC_DNS_TIMEOUT_MS
#define PC_DNS_TIMEOUT_MS 5000
#endif

/**
 * @brief Tamper-evident audit log (PC_ENABLE_AUDIT_LOG).
 *
 * Default off. services/security/audit_log keeps an append-only, hash-chained security
 * log: each record carries SHA-256(prev_hash || fields), so altering, deleting,
 * or reordering any retained record breaks the chain (pc_audit_verify()
 * detects it). Storage is a fixed RAM ring of PC_AUDIT_LOG_ENTRIES records
 * (no heap); when it wraps, a moving anchor keeps the retained window verifiable.
 * Install a sink (pc_audit_set_sink) to forward every record at creation time
 * to a durable / remote store - SD-card file, syslog or HTTP log service, serial
 * console - preserving the same chain off-device. Pure and host-tested.
 */
#ifndef PC_ENABLE_AUDIT_LOG
#define PC_ENABLE_AUDIT_LOG 0
#endif

// Ring depth and per-record message length are tunable in audit_log.h
// (PC_AUDIT_LOG_ENTRIES, PC_AUDIT_MSG_LEN); define them before include to
// override. The RAM cost is roughly PC_AUDIT_LOG_ENTRIES * (PC_AUDIT_MSG_LEN
// + 41) bytes.

/**
 * @brief OpenID Connect ID-token verification, RS256 (PC_ENABLE_OIDC).
 *
 * Default off. services/security/oidc verifies an OIDC ID token (JWT) as a relying party:
 * requires alg RS256, selects the issuer key by kid from a JWKS, verifies the
 * RSASSA-PKCS1-v1.5 SHA-256 signature (real RSA modexp via ssh_rsa, mbedTLS-
 * accelerated on ESP32), and checks iss / aud / exp / nbf, extracting sub / email.
 * Pure and host-tested; the caller fetches + caches the JWKS over HTTPS (off the
 * request hot path) and passes the JSON in. Builds on the SSH RSA primitive, not
 * the HS256 JWT module (services/security/jwt), so the two are independent.
 */
#ifndef PC_ENABLE_OIDC
#define PC_ENABLE_OIDC 0
#endif

/** @brief Max accepted OIDC ID-token length (also sizes the Authorization buffer). */
#ifndef PC_OIDC_MAX_LEN
#define PC_OIDC_MAX_LEN 1600
#endif

/**
 * @brief Mounted storage (PC_ENABLE_MNT).
 *
 * Default off. server/filesystem/mnt says *what is mounted*: a pluggable backend
 * vtable (open/read/write/close/seek, exists/size/remove/rename, mkdir/rmdir/stat,
 * opendir/readdir) that a feature reaches through the filesystem accessor
 * (server/filesystem/filesystem.h), so it can target storage without knowing the medium. A
 * built-in zero-heap RAM backend (fixed BSS pool - deterministic, host-identical)
 * ships for scratch / tests, which is what lets the file-transfer servers run under
 * a native test; an Arduino-FS backend (board layer) wraps a real fs::FS (LittleFS
 * / SD / SPIFFS) for persistence. Mount one at startup; the API fails closed
 * otherwise. Pool dimensions are tunable in this config (PC_MNT_RAM_FILES,
 * _RAM_FILE_SIZE, _MAX_OPEN, _NAME_MAX).
 */
#ifndef PC_ENABLE_MNT
#define PC_ENABLE_MNT 0
#endif

/**
 * @brief GraphQL query subset (PC_ENABLE_GRAPHQL).
 *
 * Default off. services/iot/graphql parses a GraphQL query into a fixed AST node pool
 * (no heap) and emits a `{"data":{...}}` response shaped exactly by the requested
 * selection. Schema-free: a field with a sub-selection is an object (the engine
 * recurses), a leaf field calls your single resolver, and arguments collected
 * along the path are handed to it. Supports nested selections, field arguments,
 * and the anonymous / `query` forms; mutations, subscriptions, fragments, and
 * variables are out of scope. Pure and host-tested; bounds are compile-time
 * (PC_GQL_* in this config). Serve it from a POST /graphql route.
 */
#ifndef PC_ENABLE_GRAPHQL
#define PC_ENABLE_GRAPHQL 0
#endif

/**
 * @brief ESP-NOW peer messaging (PC_ENABLE_ESPNOW).
 *
 * Default off. services/radio/espnow wraps ESP-NOW connectionless peer-to-peer radio
 * messaging in a 3-byte typed envelope (magic + type + length) so a receiver can
 * demux by message type and reject a truncated frame, plus a bounded peer
 * registry (PC_ESPNOW_MAX_PEERS, no heap). The envelope codec + registry are
 * pure and host-tested; the radio path (begin / add_peer / send / broadcast over
 * esp_now, decoded frames to a callback) is ESP32-only and can bridge to
 * WebSocket/SSE. No stdlib.
 */
#ifndef PC_ENABLE_ESPNOW
#define PC_ENABLE_ESPNOW 0
#endif

/**
 * @brief OAuth2 token-endpoint client (PC_ENABLE_OAUTH2).
 *
 * Default off. services/security/oauth2 obtains tokens - the counterpart to the OIDC
 * ID-token verifier. It builds the percent-encoded form body for the
 * authorization_code and refresh_token grants (RFC 6749), supporting a
 * confidential client (client_secret) or a public client with PKCE
 * (code_verifier, RFC 7636), and parses the JSON token response (reusing the
 * zero-heap JSON reader). The build + parse core is pure and host-tested; the POST
 * to the token endpoint uses the HTTP(S) client (needs PC_ENABLE_HTTP_CLIENT).
 * No heap, no stdlib.
 */
#ifndef PC_ENABLE_OAUTH2
#define PC_ENABLE_OAUTH2 0
#endif

/**
 * @brief OPC UA Binary server (PC_ENABLE_OPCUA).
 *
 * Default off. services/fieldbus/opcua provides an OPC UA (IEC 62541) Binary server: the
 * little-endian built-in-type codec (incl. NodeId / ExtensionObject / DateTime /
 * Variant / DataValue / ReferenceDescription), UA-TCP (UACP) message framing, the
 * Hello/Acknowledge handshake, the SecureChannel (OpenSecureChannel, SecurityPolicy
 * None), the Session (CreateSession + ActivateSession), GetEndpoints, the Read, Write
 * and Browse services (registered resolvers map a NodeId to a value / accept a written
 * value / list child references), plus CloseSession + CloseSecureChannel and a
 * ServiceFault for unsupported services, served on TCP via PROTO_OPCUA
 * (`listen(4840, PROTO_OPCUA)`). The MSG framing is spec-faithful (incl.
 * SecureChannelId), so standard clients interoperate (verified with python asyncua:
 * connect + browse + read + write/read-back). All pure and host-tested. No heap, no stdlib.
 */
#ifndef PC_ENABLE_OPCUA
#define PC_ENABLE_OPCUA 0
#endif

/**
 * @brief OPC UA Binary client (PC_ENABLE_OPCUA_CLIENT).
 *
 * Default off. Requires PC_ENABLE_OPCUA (shares the codec). services/pc_opcua_client
 * provides the client side of the OPC UA Binary protocol: request builders (Hello,
 * OpenSecureChannel, CreateSession, ActivateSession, Read, Browse, CloseSession,
 * CloseSecureChannel) and response parsers, reusing the opcua.h codec. It is
 * transport-agnostic - the app supplies the outbound socket (e.g. an Arduino
 * WiFiClient) and feeds bytes through these pure builders/parsers. No heap, no stdlib.
 */
#ifndef PC_ENABLE_OPCUA_CLIENT
#define PC_ENABLE_OPCUA_CLIENT 0
#endif

/**
 * @brief umati - OPC UA for Machine Tools information model (PC_ENABLE_UMATI).
 *
 * Default off. Requires PC_ENABLE_OPCUA (builds on the OPC UA Binary server). services/machine_tool/umati
 * exposes the umati / OPC UA for Machine Tools companion model (OPC 40501-1, namespace
 * `http://opcfoundation.org/UA/MachineTool/`): a fixed MachineTool node hierarchy -
 * Identification, Monitoring (MachineTool / Channel / Spindle / Axis_X..Z), Production, and
 * Notification - served through the OPC UA Browse + Read resolvers out of a caller-owned
 * UmatiMachineTool struct you refresh each loop. Faithful BrowseNames per OPC 40501-1; a monitoring
 * (read-only) model any umati / OPC UA client browses and reads by BrowseName. No heap, no stdlib.
 */
#ifndef PC_ENABLE_UMATI
#define PC_ENABLE_UMATI 0
#endif

/** @brief NamespaceIndex the umati MachineTool nodes live at (default 1). */
#ifndef PC_UMATI_NS
#define PC_UMATI_NS 1
#endif

/**
 * @brief OPC UA for Robotics information model (PC_ENABLE_ROBOTICS).
 *
 * Default off. Requires PC_ENABLE_OPCUA (builds on the OPC UA Binary server). services/machine_tool/robotics
 * exposes the OPC UA for Robotics companion model (OPC 40010-1, namespace
 * `http://opcfoundation.org/UA/Robotics/`): a fixed MotionDeviceSystem node hierarchy -
 * MotionDevices (MotionDevice / ParameterSet / PC_ROBOTICS_AXES parametric Axes), Controllers
 * (Controller / Software), and SafetyStates (SafetyState / ParameterSet) - served through the OPC UA
 * Browse + Read resolvers out of a caller-owned RoboticsMotionDeviceSystem struct you refresh each loop.
 * Faithful BrowseNames per OPC 40010-1; a monitoring (read-only) model any OPC UA client browses and
 * reads by BrowseName. No heap, no stdlib. Same pattern as PC_ENABLE_UMATI.
 */
#ifndef PC_ENABLE_ROBOTICS
#define PC_ENABLE_ROBOTICS 0
#endif

/** @brief NamespaceIndex the robotics MotionDeviceSystem nodes live at (default 1). */
#ifndef PC_ROBOTICS_NS
#define PC_ROBOTICS_NS 1
#endif

/** @brief Number of Axes the robotics MotionDevice exposes (default 6; must fit PC_OPCUA_REF_MAX). */
#ifndef PC_ROBOTICS_AXES
#define PC_ROBOTICS_AXES 6
#endif

/**
 * @brief EUROMAP 77 (OPC 40077) - OPC UA for injection moulding machines (IMM <-> MES) (PC_ENABLE_EUROMAP77).
 *
 * Default off. Requires PC_ENABLE_OPCUA (builds on the OPC UA Binary server). services/machine_tool/euromap77
 * exposes the EUROMAP 77 IMM_MES_Interface companion model (OPC 40077, namespace
 * `http://www.euromap.org/euromap77/`, enums from EUROMAP 83 / OPC 40083): a fixed node hierarchy -
 * MachineInformation, MachineStatus, and Jobs (ActiveJob + ActiveJobValues with the UInt64 production
 * counters) - served through the OPC UA Browse + Read resolvers out of a caller-owned EmImm struct you
 * refresh each loop. Faithful BrowseNames + a read-only monitoring model any OPC UA client browses and
 * reads by BrowseName. No heap, no stdlib. Same pattern as PC_ENABLE_UMATI / PC_ENABLE_ROBOTICS.
 */
#ifndef PC_ENABLE_EUROMAP77
#define PC_ENABLE_EUROMAP77 0
#endif

/** @brief NamespaceIndex the EUROMAP 77 IMM_MES_Interface nodes live at (default 1). */
#ifndef PC_EM77_NS
#define PC_EM77_NS 1
#endif

/**
 * @brief Streaming file upload: POST a body straight to a file on the filesystem.
 *
 * Default off. When set, src/network_drivers/application/upload_service/upload_service.h registers a POST route
 * that streams the request body directly into an Arduino FS file (LittleFS /
 * SPIFFS / SD) - the upload never has to fit in RAM. Reuses the same parser
 * streaming-body hook as OTA.
 *
 * For reliable streamed uploads the RX ring must hold at least one full TCP
 * receive window (RX_BUF_SIZE >= TCP_WND, ~5.7 KB by default): the transport
 * reopens the window only as the consumer drains the ring (ack-on-consume), so a
 * ring smaller than the window lets the peer overrun it and the transfer
 * deadlocks - you cannot advertise a window larger than your buffer. When a
 * streaming feature is enabled and RX_BUF_SIZE was left at its default, it is
 * automatically upsized below; an explicit RX_BUF_SIZE is honored as-is (set it
 * >= TCP_WND yourself). The 1024 default suits ordinary requests, not uploads.
 */
#ifndef PC_ENABLE_UPLOAD
#define PC_ENABLE_UPLOAD 0
#endif

/**
 * @brief Internal: the parser's streaming-body machinery (OTA, file upload, WebDAV PUT).
 *
 * Each streams the request body to a sink instead of buffering it into body[]; the
 * parser support is shared and compiled when any of these features is enabled. The
 * sink is a single global hook, so only one streaming consumer is active per build
 * (the last to register wins) - do not combine OTA / upload / WebDAV streaming in
 * the same firmware.
 */
#if PC_ENABLE_OTA || PC_ENABLE_UPLOAD || PC_ENABLE_WEBDAV
#define PC_ENABLE_STREAM_BODY 1
#else
#define PC_ENABLE_STREAM_BODY 0
#endif

// The RX-ring feature floors (streaming needs a full TCP window, SSH/TLS a full first flight) are
// resolved by board_drivers/board_profiles/derived_sizing.h, included at the end of this file once every feature
// flag is known - that is the sizing layer's job, not this file's.

/** @brief First-boot WiFi provisioning: softAP + captive-portal credentials form. */
#ifndef PC_ENABLE_PROVISIONING
#define PC_ENABLE_PROVISIONING 0
#endif

/**
 * @brief Syslog client (RFC 5424 over UDP).
 *
 * Default off. When set, the device can ship log lines to a remote syslog server
 * (e.g. rsyslog / journald / a SIEM) as RFC 5424 UDP datagrams via the
 * transport-layer UDP service - a zero-heap structured-logging sink for fleets
 * of constrained devices. See src/services/net/syslog/syslog.h.
 */
#ifndef PC_ENABLE_SYSLOG
#define PC_ENABLE_SYSLOG 0
#endif

/** @brief Maximum formatted syslog datagram length in bytes (RFC 5424 line). */
#ifndef PC_SYSLOG_MSG_MAX
#define PC_SYSLOG_MSG_MAX 256
#endif

/** @brief Maximum syslog HOSTNAME / APP-NAME field length (including NUL). */
#ifndef PC_SYSLOG_FIELD_MAX
#define PC_SYSLOG_FIELD_MAX 32
#endif

/** @brief Default syslog collector UDP port (RFC 5426 well-known 514; overridable at runtime
 *  via pc_syslog_init and here for a non-standard collector). */
#ifndef PC_SYSLOG_DEFAULT_PORT
#define PC_SYSLOG_DEFAULT_PORT 514
#endif

/**
 * @brief JWT bearer-token authentication (HS256).
 *
 * Default off. When set, src/services/security/jwt/jwt.h verifies `Authorization: Bearer
 * <jwt>` tokens signed with HMAC-SHA-256 (reusing the SSH crypto layer) and can
 * read integer claims (e.g. `exp`) so a handler/middleware can gate routes on a
 * stateless token. Signature verification is constant-time.
 */
#ifndef PC_ENABLE_JWT
#define PC_ENABLE_JWT 0
#endif

/** @brief Maximum accepted JWT length in bytes (header.payload.signature). */
#ifndef PC_JWT_MAX_LEN
#define PC_JWT_MAX_LEN 512
#endif

/**
 * @brief Outbound HTTP(S) client (raw lwIP, optional client-side mbedTLS).
 *
 * Default off. When set, src/services/net/http_client/http_client.h can issue a
 * blocking GET/POST to a remote server: it resolves the host (DNS), opens a raw
 * lwIP TCP connection (https:// goes through client-side mbedTLS over the same
 * static arena as the server TLS), sends the request, and returns the status +
 * body in caller buffers. For webhooks, telemetry push, REST calls from the
 * device. The request builder + response parser are host-testable; the transport
 * is ESP32-only.
 */
#ifndef PC_ENABLE_HTTP_CLIENT
#define PC_ENABLE_HTTP_CLIENT 0
#endif

/** @brief HTTPS client support inside the HTTP client (needs PC_ENABLE_TLS). */
#ifndef PC_ENABLE_HTTP_CLIENT_TLS
#define PC_ENABLE_HTTP_CLIENT_TLS 0
#endif

/** @brief Receive buffer (and max response size) for the outbound HTTP client, bytes. */
#ifndef PC_HTTP_CLIENT_BUF_SIZE
#define PC_HTTP_CLIENT_BUF_SIZE 2048
#endif

/**
 * @brief Ciphertext receive-ring size for the https:// client, bytes.
 *
 * The lwIP recv callback feeds TLS wire bytes into this draining ring while the
 * TLS engine pulls and decrypts them, so it holds only the in-flight (not yet
 * decrypted) ciphertext: a multi-KB handshake flight fits without loss thanks to
 * the refuse-and-redeliver backpressure. Must exceed one TCP segment (TCP_MSS,
 * ~1460) or a full segment could never fit. Only used when
 * PC_ENABLE_HTTP_CLIENT_TLS is set.
 */
#ifndef PC_HTTP_CLIENT_CT_BUF_SIZE
#define PC_HTTP_CLIENT_CT_BUF_SIZE 4096
#endif

/** @brief Outbound HTTP client connect/response timeout in milliseconds. */
#ifndef PC_HTTP_CLIENT_TIMEOUT_MS
#define PC_HTTP_CLIENT_TIMEOUT_MS 8000
#endif

/**
 * @brief Outbound SMTP client (RFC 5321) for device email alerts (services/net/smtp).
 *
 * A blocking one-shot `smtp_send()`: EHLO, optional AUTH LOGIN, MAIL FROM / RCPT TO /
 * DATA over the shared client transport (`pc_client`), with implicit TLS (SMTPS, e.g.
 * :465) when the message config sets `tls` and PC_ENABLE_TLS is on. Zero heap; the
 * dialogue engine (`smtp_run`) takes a send/recv seam so it is host-tested without lwIP.
 * "SMS fallback" rides on top - most carriers accept an email-to-SMS gateway address.
 */
#ifndef PC_ENABLE_SMTP
#define PC_ENABLE_SMTP 0
#endif

/**
 * @brief Secure SMTP: run the mail client over client-side TLS (needs PC_ENABLE_TLS).
 *
 * Covers both SMTP_TLS (implicit, port 465) and SMTP_STARTTLS (the
 * in-band upgrade on the submission port, 587). Separate from PC_ENABLE_SMTP because the plain
 * codec needs neither the TLS stack nor the client-session singleton.
 */
#ifndef PC_ENABLE_SMTP_TLS
#define PC_ENABLE_SMTP_TLS 0
#endif

#define PC_ENABLE_SMTP_TLS_NEEDS_SMTP PC_ENABLE_SMTP
#if PC_ENABLE_SMTP_TLS && !PC_ENABLE_SMTP_TLS_NEEDS_SMTP
#error "ProtoCore: PC_ENABLE_SMTP_TLS needs PC_ENABLE_SMTP"
#endif

/** @brief Max length of one SMTP command / address line (bytes, incl. CRLF). */
#ifndef PC_SMTP_LINE_MAX
#define PC_SMTP_LINE_MAX 256
#endif

/** @brief Max size of the assembled DATA payload (headers + dot-stuffed body), bytes. */
#ifndef PC_SMTP_MSG_MAX
#define PC_SMTP_MSG_MAX 2048
#endif

/** @brief Max size of one (possibly multi-line) server reply held while parsing, bytes. */
#ifndef PC_SMTP_REPLY_MAX
#define PC_SMTP_REPLY_MAX 512
#endif

/** @brief SMTP connect / per-reply timeout in milliseconds. */
#ifndef PC_SMTP_TIMEOUT_MS
#define PC_SMTP_TIMEOUT_MS 10000
#endif

/** @brief Ciphertext receive-ring size for SMTPS, bytes (only used when the message is TLS). */
#ifndef PC_SMTP_CT_BUF_SIZE
#define PC_SMTP_CT_BUF_SIZE 4096
#endif

/**
 * @brief MQTT 3.1.1 publish/subscribe client (raw lwIP, optional MQTTS over TLS).
 *
 * Default off. When set, src/services/iot/mqtt/mqtt.h provides a persistent outbound
 * client: connect to a broker, PUBLISH (QoS 0/1/2) and SUBSCRIBE to topics, receive
 * incoming messages via a callback, with keep-alive pings - the dominant IoT
 * messaging pattern, for telemetry push and remote command. The packet codec is
 * host-testable; the transport (DNS + raw lwIP TCP, MQTTS via client-side mbedTLS)
 * is ESP32-only. Full QoS 0/1/2 (outbound DUP retransmit, inbound QoS-2
 * de-duplication by packet id) and Last-Will are supported.
 */
#ifndef PC_ENABLE_MQTT
#define PC_ENABLE_MQTT 0
#endif

/** @brief MQTTS: run the MQTT client over client-side TLS (needs PC_ENABLE_TLS). */
#ifndef PC_ENABLE_MQTT_TLS
#define PC_ENABLE_MQTT_TLS 0
#endif

/**
 * @brief MQTT packet buffer size in bytes (bounds one outgoing/incoming packet).
 *
 * Two buffers of this size live in BSS (one tx, one rx). Must hold the largest
 * CONNECT/PUBLISH the client sends and the largest incoming PUBLISH it accepts
 * (topic + payload + a few header bytes); larger incoming packets are dropped.
 */
#ifndef PC_MQTT_BUF_SIZE
#define PC_MQTT_BUF_SIZE 1024
#endif

/** @brief Default MQTT keep-alive interval in seconds (PINGREQ cadence / CONNECT field). */
#ifndef PC_MQTT_KEEPALIVE_S
#define PC_MQTT_KEEPALIVE_S 30
#endif

/** @brief Ciphertext receive-ring size for MQTTS (draining ring; must exceed one TCP_MSS). */
#ifndef PC_MQTT_CT_BUF_SIZE
#define PC_MQTT_CT_BUF_SIZE 4096
#endif

/** @brief Maximum inbound MQTT topic length (including NUL) delivered to the callback. */
#ifndef PC_MQTT_MAX_TOPIC
#define PC_MQTT_MAX_TOPIC 128
#endif

/**
 * @brief Outbound QoS 1/2 in-flight slots (unacknowledged messages held for DUP retransmit).
 *
 * Each slot stores its serialized packet (up to PC_MQTT_INFLIGHT_BUF bytes) until
 * the broker acknowledges it; a publish is refused when all slots are busy. The pool
 * costs PC_MQTT_MAX_INFLIGHT * (PC_MQTT_INFLIGHT_BUF + a few bytes) of BSS.
 */
#ifndef PC_MQTT_MAX_INFLIGHT
#define PC_MQTT_MAX_INFLIGHT 4
#endif

/** @brief Stored-packet size per in-flight QoS 1/2 slot (caps a retransmittable PUBLISH). */
#ifndef PC_MQTT_INFLIGHT_BUF
#define PC_MQTT_INFLIGHT_BUF 256
#endif

/** @brief Retransmit timeout (ms) for an unacknowledged in-flight QoS 1/2 message. */
#ifndef PC_MQTT_RETRANSMIT_MS
#define PC_MQTT_RETRANSMIT_MS 5000
#endif

/** @brief Inbound QoS 2 packet-id de-duplication ring depth (PUBREC-acknowledged, awaiting PUBREL). */
#ifndef PC_MQTT_RX_QOS2_SLOTS
#define PC_MQTT_RX_QOS2_SLOTS 8
#endif

/**
 * @brief Outbound WebSocket client (RFC 6455 over raw lwIP, optional wss:// TLS).
 *
 * Default off. When set, src/services/net/ws_client/ws_client.h connects to a remote
 * WebSocket endpoint (ws://, or wss:// over client-side mbedTLS), performs the
 * RFC 6455 client handshake (Sec-WebSocket-Key/Accept), and sends masked text /
 * binary frames + receives server frames via a callback - for streaming to cloud
 * dashboards or bidirectional control. The frame/handshake codec is host-testable.
 */
#ifndef PC_ENABLE_WS_CLIENT
#define PC_ENABLE_WS_CLIENT 0
#endif

/** @brief wss://: run the WebSocket client over client-side TLS (needs PC_ENABLE_TLS). */
#ifndef PC_ENABLE_WS_CLIENT_TLS
#define PC_ENABLE_WS_CLIENT_TLS 0
#endif

/** @brief WebSocket client send/receive buffer size in bytes (bounds one frame). */
#ifndef PC_WS_CLIENT_BUF_SIZE
#define PC_WS_CLIENT_BUF_SIZE 1024
#endif

/** @brief Ciphertext receive-ring size for wss:// (draining ring; must exceed one TCP_MSS). */
#ifndef PC_WS_CLIENT_CT_BUF_SIZE
#define PC_WS_CLIENT_CT_BUF_SIZE 4096
#endif

/**
 * @brief Internal: client-side TLS engine is compiled (HTTPS client, MQTTS, wss client, and/or a TLS edge-cache
 * origin).
 *
 * The outbound HTTP client (one-shot exchange) and the MQTT / WebSocket clients
 * and the edge cache's TLS origin fetch (persistent sessions) share the same
 * client mbedTLS code in pc_tls - the CA/pin trust config, the BIO typedefs,
 * and the session API - gated by this.
 */
#if PC_ENABLE_HTTP_CLIENT_TLS || PC_ENABLE_MQTT_TLS || PC_ENABLE_WS_CLIENT_TLS || PC_ENABLE_EDGE_ORIGIN_TLS ||         \
    PC_ENABLE_SMTP_TLS
#define PC_ENABLE_CLIENT_TLS 1
#else
#define PC_ENABLE_CLIENT_TLS 0
#endif

// The outbound clients (pc_client) resolve hostnames through the shared DNS
// resolver (pc_dns_resolver_resolve), so enabling any client implies the resolver - one
// owner of the gethostbyname-marshal pattern instead of a private copy per client.
// PC_NEED_CLIENT marks when the client transport is actually used; the
// pc_client translation unit compiles its body only then (a server-only Arduino
// build that does not enable a client must not reference the resolver symbols).
// Every feature that drives the outbound client transport must pull it in: the direct callers
// (http_client / mqtt / ws_client / relay / smtp / ssh port-forward) and the seam-based engines
// whose shipped example binds the seam to pc_client (smb / dnc). Miss one and its Tcp.client->open
// resolves to the !NEED stub that returns -1, so the feature silently never connects on device.
#if PC_ENABLE_HTTP_CLIENT || PC_ENABLE_MQTT || PC_ENABLE_WS_CLIENT || PC_ENABLE_RELAY || PC_ENABLE_SMTP ||             \
    PC_SSH_PORT_FORWARD || PC_ENABLE_SMB || PC_ENABLE_DNC || PC_ENABLE_FTP_SESSION || PC_ENABLE_SSH_CLIENT
#define PC_NEED_CLIENT 1
#endif
#ifndef PC_NEED_CLIENT
#define PC_NEED_CLIENT 0
#endif

// The client dials by name, so anything that needs the client needs the resolver.
#define PC_NEED_DNS_RESOLVER (PC_ENABLE_DNS_RESOLVER || PC_NEED_CLIENT)

// PC_NEED_UDP marks when the datagram transport is built. The listener and client hold rings that
// only move when someone drains them, and Session.tick() is that someone, so the tick references the
// Udp table only where a feature put it in the image. Every feature that binds a UDP port or sends a
// datagram lists itself here. Miss one and its rings fill and stop, with nothing on the wire.
#if PC_ENABLE_COAP || PC_ENABLE_DTLS || PC_ENABLE_STATSD || PC_ENABLE_UDP_TELEMETRY || PC_ENABLE_SNMP ||               \
    PC_ENABLE_SNMP_TRAP || PC_ENABLE_SNMP_V3 || PC_ENABLE_SYSLOG || PC_ENABLE_FLOW_EXPORT || PC_ENABLE_PROVISIONING || \
    PC_ENABLE_NTP_SERVER || PC_ENABLE_DNS_SERVER || PC_ENABLE_HTTP3
#define PC_NEED_UDP 1
#endif
#ifndef PC_NEED_UDP
#define PC_NEED_UDP 0
#endif

// ---------------------------------------------------------------------------
// Full Authorization-header capture (internal)
// ---------------------------------------------------------------------------
// Digest auth and JWT bearer tokens both carry an Authorization value far longer
// than MAX_VAL_LEN, so the parser captures the whole header into a dedicated
// per-request buffer (HttpReq::authorization) when either feature is enabled.

/** @brief True when the parser must capture the full Authorization header value. */
#if PC_ENABLE_AUTH || PC_ENABLE_JWT || PC_ENABLE_OIDC
#define PC_CAPTURE_AUTH_HEADER 1
#else
#define PC_CAPTURE_AUTH_HEADER 0
#endif

/**
 * @brief Capacity of HttpReq::authorization (full Authorization header value).
 *
 * Sized to the largest enabled consumer: a Digest header (DIGEST_AUTH_HDR_MAX), a
 * `Bearer <jwt>` HS256 token (PC_JWT_MAX_LEN), or a `Bearer <id_token>` OIDC
 * RS256 token (PC_OIDC_MAX_LEN), each plus the scheme.
 */
#if PC_ENABLE_OIDC
#define PC_AUTH_HDR_CAP_OIDC (PC_OIDC_MAX_LEN + 16)
#else
#define PC_AUTH_HDR_CAP_OIDC 0
#endif
#if PC_ENABLE_JWT
#define PC_AUTH_HDR_CAP_JWT (PC_JWT_MAX_LEN + 16)
#else
#define PC_AUTH_HDR_CAP_JWT 0
#endif
#define PC_AUTH_HDR_CAP_M1 (PC_AUTH_HDR_CAP_JWT > DIGEST_AUTH_HDR_MAX ? PC_AUTH_HDR_CAP_JWT : DIGEST_AUTH_HDR_MAX)
#define PC_AUTH_HDR_CAP (PC_AUTH_HDR_CAP_OIDC > PC_AUTH_HDR_CAP_M1 ? PC_AUTH_HDR_CAP_OIDC : PC_AUTH_HDR_CAP_M1)

/** @brief Runtime stats endpoint (uptime, request/error counts, pool usage, heap). */
#ifndef PC_ENABLE_STATS
#define PC_ENABLE_STATS 0
#endif

/**
 * @brief Transport-layer observability: connection event hook + counters.
 *
 * Default off (zero cost when unset - the notify points compile to nothing).
 * When set, the transport (L4) fires an application callback on every connection
 * state transition - Tcp.conn->on_event(slot, old_state, new_state, reason) - and
 * maintains lock-free counters (accepts, closes by reason, idle timeouts, RX
 * backpressure events, dropped deferred events, and a live CONN_CLOSING gauge)
 * readable via Tcp.conn->counters_get(). This is the only state-transition trace the
 * L4/L5 core exposes; pair it with PC_ENABLE_STATS for request-level metrics.
 */
#ifndef PC_ENABLE_OBSERVABILITY
#define PC_ENABLE_OBSERVABILITY 0
#endif

/**
 * @brief Prometheus `/metrics` endpoint (text exposition format 0.0.4).
 *
 * Default off (requires PC_ENABLE_STATS for the underlying counters). When
 * set, metrics() emits the runtime stats as Prometheus metrics
 * (`pc_uptime_seconds`, `pc_http_requests_total`,
 * `pc_http_responses_total{class=...}`, `pc_active_connections`,
 * `pc_free_heap_bytes`, ...) so a Prometheus server can scrape the device.
 */
#ifndef PC_ENABLE_METRICS
#define PC_ENABLE_METRICS 0
#endif

/**
 * @brief Browser "web serial" terminal over WebSocket (src/services/web/web_terminal).
 *
 * Serves a self-contained terminal page and a WebSocket endpoint: device output
 * is broadcast to all connected browsers, browser input is delivered to a
 * command callback. Requires PC_ENABLE_WEBSOCKET. Default off.
 */
#ifndef PC_ENABLE_WEB_TERMINAL
#define PC_ENABLE_WEB_TERMINAL 0
#endif

/**
 * @brief Stack scratch for pc_web_terminal_println() line building.
 *
 * One formatted terminal line must fit in this many bytes (longer is truncated).
 * Allocated on the stack only during the call - no persistent RAM cost.
 */
#ifndef TERM_TX_BUF_SIZE
#define TERM_TX_BUF_SIZE 256
#endif

/**
 * @brief Conditional GET (ETag + Last-Modified) for served files.
 *
 * When set, serve_file()/serve_static() emit a strong `ETag` (from file size +
 * mtime) and a `Last-Modified` date, and answer a conditional request with
 * `304 Not Modified` when either the client's `If-None-Match` matches the ETag or
 * - per RFC 9110, only if no `If-None-Match` is present - its `If-Modified-Since`
 * is not older than the file. Saves bandwidth on repeat fetches of static assets.
 * (If-Modified-Since needs a real wall clock for the file mtime; with no clock the
 * date validator is skipped and the ETag validator still works.)
 */
#ifndef PC_ENABLE_ETAG
#define PC_ENABLE_ETAG 0
#endif

/**
 * @brief Expose a diagnostic JSON endpoint via diag().
 *
 * Disabled by default - enabling it exposes compile-time configuration
 * (buffer sizes, feature flags) which could aid an attacker.  Only
 * enable in development or behind an authenticated route.
 *
 * When enabled, serve it from any route handler:
 * @code
 *   static void handle_diag(uint8_t id, HttpReq *req)
 *   {
 *       (void)req;
 *       diag(id);
 *   }
 *
 *   on_http("/diag", HTTP_GET, handle_diag);
 * @endcode
 */
#ifndef PC_ENABLE_DIAG
#define PC_ENABLE_DIAG 0
#endif

/**
 * @brief HTTP/1.1 persistent connections (keep-alive).
 *
 * Default off (every response carries `Connection: close` and the connection is
 * closed after one request - the long-standing behavior). When set to 1, a
 * cleanly-parsed request is answered with `Connection: keep-alive` and the slot
 * is recycled for the next request on the same socket: HTTP/1.1 keeps the
 * connection open unless the client sends `Connection: close`; HTTP/1.0 closes
 * unless the client sends `Connection: keep-alive`. Error responses (400/413/414
 * and any non-PARSE_COMPLETE path) always close, since the next request boundary
 * is unknown. Idle keep-alive connections are still reclaimed by the existing
 * conn_timeout sweep, and each connection serves at most
 * PC_KEEPALIVE_MAX_REQUESTS requests before a deliberate close.
 */
#ifndef PC_ENABLE_KEEPALIVE
#define PC_ENABLE_KEEPALIVE 0
#endif

/**
 * @brief Maximum requests served on one keep-alive connection before it is closed.
 *
 * A fairness bound so a single client cannot hold a connection slot
 * indefinitely with a steady request stream. After this many responses the
 * server emits `Connection: close` and drops the link; the client simply
 * reconnects. Only meaningful when PC_ENABLE_KEEPALIVE is set.
 */
#ifndef PC_KEEPALIVE_MAX_REQUESTS
#define PC_KEEPALIVE_MAX_REQUESTS 100
#endif

/**
 * @brief HTTP/2 (RFC 9113) over the version-agnostic request/response core.
 *
 * Default off. When set, the server negotiates HTTP/2 via TLS ALPN ("h2") and speaks the binary
 * framing + HPACK header compression (RFC 7541) on top of the same routes/handlers as HTTP/1.1
 * (the response serializer is version-neutral). The HPACK codec and the frame layer are pure and
 * host-tested; the connection/stream state machine plugs in as a ProtoHandler.
 */
#ifndef PC_ENABLE_HTTP2
#define PC_ENABLE_HTTP2 0
#endif

/**
 * @brief Per-connection HPACK dynamic-table size in bytes (our decoder; advertised to the peer
 * as SETTINGS_HEADER_TABLE_SIZE). RFC 7541's default is 4096; lower it to save per-connection
 * RAM (each active HTTP/2 connection holds one table).
 */
#ifndef PC_HPACK_TABLE_BYTES
#define PC_HPACK_TABLE_BYTES 4096
#endif

/** @brief Max HPACK dynamic-table entries (>= PC_HPACK_TABLE_BYTES / 32, the min entry size). */
#ifndef PC_HPACK_MAX_ENTRIES
#define PC_HPACK_MAX_ENTRIES 128
#endif

/**
 * @brief Largest HTTP/2 frame we accept, in bytes (advertised as SETTINGS_MAX_FRAME_SIZE). RFC
 * 9113 requires accepting at least 16384; a whole frame is buffered for reassembly, so this
 * (plus the HPACK table) sets the per-HTTP/2-connection RAM. Range: [16384, 16777215].
 */
#ifndef PC_H2_MAX_FRAME
#define PC_H2_MAX_FRAME 16384
#endif

/** @brief Max concurrent HTTP/2 streams per connection (advertised as MAX_CONCURRENT_STREAMS). */
#ifndef PC_H2_MAX_STREAMS
#define PC_H2_MAX_STREAMS 8
#endif

/**
 * @brief Header-block reassembly buffer for HTTP/2 requests that span HEADERS + CONTINUATION
 * frames (a single END_HEADERS frame decodes in place and needs no copy). Caps the compressed
 * request-header size; a larger block is rejected (RFC 9113 sec 6.10).
 */
#ifndef PC_H2_HDR_BLOCK
#define PC_H2_HDR_BLOCK 4096
#endif

/**
 * @brief Place the HTTP/2 connection-engine pool in external PSRAM (ESP32).
 *
 * Each HTTP/2 connection needs a ~28 KB engine, so the pool (MAX_CONNS of them) does not fit the
 * ~122 KB internal DRAM alongside a TLS server - HTTP/2 therefore requires PSRAM. Set this to 1
 * on a PSRAM board (S3 / P4 / WROVER) to move the pool to external RAM via `EXT_RAM_BSS_ATTR`.
 * Like PC_TLS_ARENA_IN_PSRAM it needs a framework built with
 * `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` (the stock arduino-esp32 core ships it off; see
 * tools/psram/README.md). A compile-time guard rejects PC_ENABLE_HTTP2 without this on ARDUINO.
 */
#ifndef PC_H2_POOL_IN_PSRAM
#define PC_H2_POOL_IN_PSRAM 0
#endif

/**
 * @brief HTTP/3 (RFC 9114) over QUIC (RFC 9000) - implemented, host-tested end-to-end (HW verification pending).
 *
 * Default off. HTTP/3 runs over QUIC (a reliable transport over UDP) with QPACK (RFC 9204)
 * header compression and its own binary framing. The full stack is in place and exercised by a
 * host end-to-end test - the QUIC variable-length integer (RFC 9000 sec 16), packet protection +
 * framing, the TLS 1.3-in-QUIC handshake, the transport connection engine, and the HTTP/3 + QPACK
 * codecs. On-device (ESP32) HW verification and an example are still pending. Like HTTP/2 this is a
 * PSRAM-class feature.
 */
#ifndef PC_ENABLE_HTTP3
#define PC_ENABLE_HTTP3 0
#endif

/**
 * @brief DTLS 1.3 datagram security (RFC 9147) - the record layer.
 *
 * DTLS 1.3 secures datagram (UDP) transports - CoAP-over-DTLS and other constrained-device
 * telemetry - reusing the hand-rolled TLS 1.3 handshake crypto that already backs HTTP/3. This
 * flag gates the DTLS 1.3 **record layer** (pc_dtls_record): the DTLSCiphertext unified header,
 * per-record AEAD protection (AEAD_AES_128_GCM), the RFC 9147 sequence-number encryption,
 * sequence-number reconstruction, and the anti-replay window; the **handshake framing and
 * reliability** layer (pc_dtls_handshake, RFC 9147 §5 + §7): the 12-byte handshake header,
 * overlap-tolerant message reassembly, the ACK message, and the stateless HelloRetryRequest
 * cookie; and the **server handshake state machine** (pc_dtls_conn, RFC 9147 §5-6): the
 * one-round-trip full handshake (TLS_AES_128_GCM_SHA256 / X25519 / Ed25519), epoch 0->2->3
 * transitions, reusing the TLS 1.3 messages + key schedule (pc_tls13_msg / pc_tls13_kdf). The
 * HelloRetryRequest cookie round-trip and ACK/timeout retransmission, plus a CoAPs front-end, are
 * the following phases. Enabling this also compiles the shared pc_hkdf / aes128gcm / pc_tls13_*
 * primitives (otherwise gated behind HTTP/3). Default off.
 */
#ifndef PC_ENABLE_DTLS
#define PC_ENABLE_DTLS 0
#endif

/**
 * @brief TLS Raw Public Keys (RFC 7250) - present a bare public key instead of an X.509 certificate.
 *
 * When a client offers the @c server_certificate_type extension (IANA 20) with RawPublicKey(2), the
 * server answers with that certificate type in EncryptedExtensions and sends a Certificate message
 * whose entry is a DER @c SubjectPublicKeyInfo (the 44-byte Ed25519 SPKI) rather than an X.509 chain -
 * the same Ed25519 key still signs CertificateVerify, so there is no security downgrade, only a
 * smaller handshake with no cert parsing. A cert-less credential is the natural fit for provisioned,
 * key-pinned ESP32 fleets and the RFC 7252 sec 9 CoAP-over-DTLS RawPublicKey profile. This is
 * server-side only (the server presents an RPK); the handshake never requests a client certificate.
 * Additive: a client that does not offer the extension still gets the X.509 certificate. Wired into
 * the DTLS 1.3 handshake; the shared TLS 1.3 codec also carries it for a future HTTP/3 use. Default off.
 */
#ifndef PC_ENABLE_TLS_RPK
#define PC_ENABLE_TLS_RPK 0
#endif

// Internal request-dispatch slots appended to the connection pool for non-TCP transports.
// HTTP/3 runs over QUIC/UDP and has no accept-time TCP slot, but it reuses the same request
// pipeline (match_and_execute + send), which is indexed by a connection-pool slot. One reserved
// slot at index MAX_CONNS lets an HTTP/3 request run through that pipeline. The TCP accept path only
// ever scans [0, MAX_CONNS), and this slot is driven synchronously by the HTTP/3 poll on the worker
// thread, so there is no accept race. CONN_POOL_SLOTS sizes conn_pool / http_pool / the per-slot
// response-header buffer; every TCP loop still bounds itself with MAX_CONNS.
#if PC_ENABLE_HTTP3
#define PC_INTERNAL_SLOTS 1
#define PC_H3_DISPATCH_SLOT MAX_CONNS ///< reserved conn-pool slot an HTTP/3 request dispatches through
#else
#define PC_INTERNAL_SLOTS 0
#endif
#define CONN_POOL_SLOTS (MAX_CONNS + PC_INTERNAL_SLOTS)

/** @brief UDP port the HTTP/3 (QUIC) server binds by default (used by pc_h3_cert). */
#ifndef PC_HTTP3_PORT
#define PC_HTTP3_PORT 443
#endif

/**
 * @brief Maximum bytes of one QUIC/TLS handshake CRYPTO flight (RFC 9001).
 *
 * The server's second flight - EncryptedExtensions + Certificate + CertificateVerify + Finished -
 * is assembled whole before it is fragmented into CRYPTO frames across Handshake packets. The
 * Certificate (a DER X.509 chain) dominates the size, so this bounds the certificate the server can
 * present. The default fits a single Ed25519 leaf certificate comfortably; raise it for a chain.
 */
#ifndef PC_H3_CRYPTO_BUF
#define PC_H3_CRYPTO_BUF 2048
#endif

/**
 * @brief Maximum concurrent request streams per HTTP/3 connection.
 *
 * Bounds the per-connection QUIC stream table (client-initiated bidirectional request streams plus
 * the handful of unidirectional control / QPACK streams). Each slot is small; 8 matches the HTTP/2
 * default (PC_H2_MAX_STREAMS).
 */
#ifndef PC_H3_MAX_STREAMS
#define PC_H3_MAX_STREAMS 8
#endif

/**
 * @brief HTTP Range requests / 206 Partial Content (requires PC_ENABLE_FILE_SERVING or
 *        PC_ENABLE_EDGE_CACHE).
 *
 * Default off. When set, serve_file() / serve_static() and the CDN edge cache honor a single-range
 * `Range: bytes=...` request header: they answer `206 Partial Content` with a `Content-Range` header
 * and stream only the requested bytes (file serving seeks the file; the edge cache windows the cached
 * body), advertise `Accept-Ranges: bytes` on full responses, and answer an unsatisfiable range with
 * `416 Range Not Satisfiable`. This enables resumable downloads and media seeking. Multi-range
 * (multipart/byteranges) requests are not supported - the server falls back to a full 200 response,
 * which is RFC 7233 §3.1 compliant. The parser is shared (network_drivers/application/http_range.h).
 */
#ifndef PC_ENABLE_RANGE
#define PC_ENABLE_RANGE 0
#endif

/**
 * @brief Enforce the RFC 7230 §5.4 Host-header requirement (default on).
 *
 * When 1, an HTTP/1.1 request that lacks a Host header - or carries more than
 * one - is rejected with 400 Bad Request. When 0, the Host header is not
 * required (useful for constrained clients or test harnesses that feed bare
 * request lines). The multiple-Host rule and Content-Length validation are
 * always active regardless of this flag.
 */
#ifndef PC_ENFORCE_HOST_HEADER
#define PC_ENFORCE_HOST_HEADER 1
#endif

/**
 * @brief Allow SSH password authentication (default on).
 *
 * Set to 0 to harden the SSH server to publickey-only authentication
 * (RFC 4252 §7): the "password" method is then refused outright and is not
 * advertised in the USERAUTH_FAILURE method list. Publickey auth is always
 * available regardless of this flag.
 */
#ifndef PC_SSH_ALLOW_PASSWORD
#define PC_SSH_ALLOW_PASSWORD 1
#endif

/**
 * @brief SSH keyboard-interactive authentication (RFC 4256), default off.
 *
 * Adds the "keyboard-interactive" method alongside password/publickey. On selection the server sends
 * one SSH_MSG_USERAUTH_INFO_REQUEST with a single non-echoed "Password: " prompt and verifies the
 * client's SSH_MSG_USERAUTH_INFO_RESPONSE through the same ::pc_ssh_auth_set_password_cb callback -
 * so it is the challenge-response face of password auth (the common OpenSSH `-o
 * PreferredAuthentications=keyboard-interactive` / PAM-password case), not a second credential store.
 * Requires PC_ENABLE_SSH and, because it is password-backed, PC_SSH_ALLOW_PASSWORD.
 */
#ifndef PC_ENABLE_SSH_KEYBOARD_INTERACTIVE
#define PC_ENABLE_SSH_KEYBOARD_INTERACTIVE 0
#endif

/**
 * @brief Maximum failed SSH authentication attempts per connection.
 *
 * RFC 4252 §4 permits the server to disconnect after a small bounded number of
 * failed USERAUTH_REQUESTs. After this many SSH_MSG_USERAUTH_FAILURE responses
 * on one connection the server sends SSH_MSG_DISCONNECT and drops the link.
 * (The publickey "would-be-accepted" probe and a SUCCESS do not count.)
 */
#ifndef SSH_MAX_AUTH_ATTEMPTS
#define SSH_MAX_AUTH_ATTEMPTS 6
#endif

/**
 * @brief Where the SSH RSA host private key is stored: the NVS namespace and the item in it.
 *
 * The server reads a DER-encoded PKCS#1/PKCS#8 blob from here at startup. Provisioning writes it
 * once per device (docs/SSH.md). Both names are within the 15-character NVS limit.
 */
#ifndef PC_SSH_HOST_KEY_NS
#define PC_SSH_HOST_KEY_NS "ssh_host_key"
#endif
#ifndef PC_SSH_HOST_KEY_ITEM
#define PC_SSH_HOST_KEY_ITEM "priv_der"
#endif

// ---------------------------------------------------------------------------
// Listener pool
// ---------------------------------------------------------------------------

/** @brief Maximum number of simultaneously active listener ports. */
#ifndef MAX_LISTENERS
#define MAX_LISTENERS 3
#endif

/**
 * @brief Maximum simultaneously bound UDP ports (transport-layer UDP service).
 *
 * Sizes the fixed pool in udp.cpp. One slot per bound port, e.g. SNMP
 * (:161) and the captive-portal DNS responder (:53). Costs only a few pointers
 * of BSS each.
 */
#ifndef PC_MAX_UDP_LISTENERS
#define PC_MAX_UDP_LISTENERS 2
#endif

/**
 * @brief Largest UDP datagram a bound port accepts, in bytes.
 *
 * Bounds one datagram, both directions: a longer inbound datagram is truncated to this at the
 * receive trampoline, and a longer send is refused. Sizes the per-slot staging buffer the drain
 * hands the handler. Must hold the largest datagram any UDP service expects (SNMP messages are the
 * largest user).
 */
#ifndef PC_UDP_RX_BUF_SIZE
#define PC_UDP_RX_BUF_SIZE 1472
#endif

/**
 * @brief Per-slot UDP receive ring, in bytes.
 *
 * Backs one bound port's receive ring. The stack's trampoline frames each datagram into it and the
 * drain reads them out, so this is how many bytes of datagram, plus a header each, may wait between
 * two poll() calls. A datagram that does not fit the free space is dropped.
 */
#ifndef PC_UDP_RX_RING
#define PC_UDP_RX_RING 2048
#endif

/**
 * @brief UDP send ring, in bytes: per slot on the listener side, one on the client side.
 *
 * Holds framed datagrams between the call that queues one and the poll() that moves it to the wire,
 * so a burst is written at memory speed and drains at wire speed. A send that does not fit the free
 * space is refused rather than blocking.
 */
#ifndef PC_UDP_TX_RING
#define PC_UDP_TX_RING 4096
#endif

// The rings index with `% cap` (mmgr/ring.h). A power-of-two capacity makes that an AND; any other
// value emits a divide, which on a target without one is a call into a software routine per access.
#if (PC_UDP_RX_RING & (PC_UDP_RX_RING - 1)) != 0
#error "ProtoCore: PC_UDP_RX_RING must be a power of two"
#endif
#if (PC_UDP_TX_RING & (PC_UDP_TX_RING - 1)) != 0
#error "ProtoCore: PC_UDP_TX_RING must be a power of two"
#endif

// A ring holds a frame header plus a payload, and keeps one slot free to tell full from empty, so a
// ring that cannot take one largest datagram would refuse every send and drop every receive.
#if PC_UDP_RX_RING < (PC_UDP_RX_BUF_SIZE + 64)
#error "ProtoCore: PC_UDP_RX_RING must exceed PC_UDP_RX_BUF_SIZE by at least one frame header"
#endif
#if PC_UDP_TX_RING < (PC_UDP_RX_BUF_SIZE + 64)
#error "ProtoCore: PC_UDP_TX_RING must exceed PC_UDP_RX_BUF_SIZE by at least one frame header"
#endif

/**
 * @brief Opt-in global accept-rate throttle (connection-flood defense).
 *
 * Default off (zero cost / no behavior change). When set to 1 the accept
 * callback rejects new connections once more than PC_ACCEPT_THROTTLE_MAX
 * have been accepted within a PC_ACCEPT_THROTTLE_WINDOW_MS fixed window
 * (global across all listeners, two static counters - no per-IP table). This
 * bounds connection churn (e.g. reconnect brute-force) on top of the bounded
 * connection pool and the per-connection auth limits. mitigate finer-grained /
 * per-IP attacks at the network layer.
 */
#ifndef PC_ENABLE_ACCEPT_THROTTLE
#define PC_ENABLE_ACCEPT_THROTTLE 0
#endif

/** @brief Max accepted connections per throttle window (see PC_ENABLE_ACCEPT_THROTTLE). */
#ifndef PC_ACCEPT_THROTTLE_MAX
#define PC_ACCEPT_THROTTLE_MAX 20
#endif

/** @brief Throttle window length in milliseconds (see PC_ENABLE_ACCEPT_THROTTLE). */
#ifndef PC_ACCEPT_THROTTLE_WINDOW_MS
#define PC_ACCEPT_THROTTLE_WINDOW_MS 1000
#endif

/**
 * @brief Opt-in per-IP accept-rate throttle (connection-flood defense, keyed by source IPv4).
 *
 * Default off (zero cost / no behavior change). Complements the global accept
 * throttle: the accept callback rejects a new connection once one source IPv4
 * address has opened more than PC_PER_IP_THROTTLE_MAX connections within a
 * PC_PER_IP_THROTTLE_WINDOW_MS fixed window. A fixed BSS table of
 * PC_PER_IP_THROTTLE_SLOTS buckets tracks the most-recently-seen source
 * addresses; when a new address arrives and the table is full, an expired or
 * least-recently-started bucket is reused, so memory stays bounded (no heap).
 *
 * This bounds reconnect/brute-force churn from a single host (the gap left by the
 * global throttle, which cannot tell one noisy client from many). It is
 * best-effort: an attacker spreading across many source addresses can still churn
 * the bounded connection pool, so combine it with the global throttle and
 * network-layer filtering.
 */
#ifndef PC_ENABLE_PER_IP_THROTTLE
#define PC_ENABLE_PER_IP_THROTTLE 0
#endif

/** @brief Number of source IPv4 addresses tracked by the per-IP throttle (BSS bucket table). */
#ifndef PC_PER_IP_THROTTLE_SLOTS
#define PC_PER_IP_THROTTLE_SLOTS 16
#endif

/** @brief Max accepted connections per window from one source IP (see PC_ENABLE_PER_IP_THROTTLE). */
#ifndef PC_PER_IP_THROTTLE_MAX
#define PC_PER_IP_THROTTLE_MAX 10
#endif

/** @brief Per-IP throttle window length in milliseconds (see PC_ENABLE_PER_IP_THROTTLE). */
#ifndef PC_PER_IP_THROTTLE_WINDOW_MS
#define PC_PER_IP_THROTTLE_WINDOW_MS 10000
#endif

// ---------------------------------------------------------------------------
// Source-IP allowlist  (accept-time firewall; PC_ENABLE_IP_ALLOWLIST)
// ---------------------------------------------------------------------------

/**
 * @brief Opt-in source-IP allowlist (accept-time firewall, IPv4 and IPv6).
 *
 * Default off (zero cost / no behavior change). When set, the accept callback
 * drops any connection whose source address is not contained in a configured
 * CIDR rule (add rules with Tcp.listener->ip_allow_add_cidr("192.168.1.0/24") /
 * "2001:db8::/32"). Matching is a full-address prefix compare per family, so a v4
 * peer never matches a v6 rule and vice versa. An empty allowlist allows
 * everything, so enabling the feature before adding rules never locks the device
 * out. Rules live in a fixed BSS table of PC_IP_ALLOWLIST_SLOTS entries (no heap).
 *
 * This is a coarse first-line filter - a spoofed source address can still pass
 * it - so combine it with the accept throttles and network-layer filtering.
 */
#ifndef PC_ENABLE_IP_ALLOWLIST
#define PC_ENABLE_IP_ALLOWLIST 0
#endif

/** @brief Number of CIDR rules the source-IP allowlist can hold (BSS table). */
#ifndef PC_IP_ALLOWLIST_SLOTS
#define PC_IP_ALLOWLIST_SLOTS 8
#endif

// ---------------------------------------------------------------------------
// Trusted reverse-proxy forwarded-client resolution  (PC_ENABLE_FORWARDED_TRUST)
// ---------------------------------------------------------------------------

/**
 * @brief Believe a `Forwarded` / `X-Forwarded-For` client address only from a trusted upstream.
 *
 * Default off. A forwarded header is client-spoofable, so it is honored only when the connection's
 * real TCP peer matches a configured trusted-proxy CIDR (register one with
 * `pc_forwarded_trust_add_cidr("10.0.0.0/8")`). When set, the per-IP auth lockout keys on the
 * recovered original client address behind such a proxy instead of the proxy's shared TCP address, so
 * one abusive client cannot lock out every client behind the proxy, while a direct (untrusted) peer's
 * spoofed header is ignored. The accept-time throttle and the IP allowlist deliberately stay on the
 * real TCP source. Requires PC_ENABLE_AUTH_LOCKOUT.
 */
#ifndef PC_ENABLE_FORWARDED_TRUST
#define PC_ENABLE_FORWARDED_TRUST 0
#endif

/** @brief Number of trusted-upstream CIDR rules the forwarded-client resolver holds (BSS table). */
#ifndef PC_TRUSTED_PROXY_MAX
#define PC_TRUSTED_PROXY_MAX 2
#endif

// ---------------------------------------------------------------------------
// Brute-force auth lockout  (per-source-IP; PC_ENABLE_AUTH_LOCKOUT)
// ---------------------------------------------------------------------------

/**
 * @brief Opt-in per-IP brute-force lockout for HTTP auth (requires PC_ENABLE_AUTH).
 *
 * Default off (zero cost / no behavior change). When set, the auth gate counts
 * consecutive failed authentications per source address (IPv4 or IPv6, keyed on
 * the full address) in a fixed BSS table; after
 * PC_AUTH_LOCKOUT_THRESHOLD failures the address is locked out for
 * PC_AUTH_LOCKOUT_BASE_MS, doubling on each further failure up to
 * PC_AUTH_LOCKOUT_MAX_MS. A locked address gets 429 (Retry-After) with no
 * credential check; a successful auth clears it. Bounded memory (no heap); the
 * table evicts idle, then least-recently-used, addresses when full.
 */
#ifndef PC_ENABLE_AUTH_LOCKOUT
#define PC_ENABLE_AUTH_LOCKOUT 0
#endif

/** @brief Number of source IPs the auth lockout tracks (BSS bucket table). */
#ifndef PC_AUTH_LOCKOUT_SLOTS
#define PC_AUTH_LOCKOUT_SLOTS 16
#endif

/** @brief Consecutive failed auths from one IP before it is locked out. */
#ifndef PC_AUTH_LOCKOUT_THRESHOLD
#define PC_AUTH_LOCKOUT_THRESHOLD 5
#endif

/** @brief First lockout duration in ms; doubles on each further failure. */
#ifndef PC_AUTH_LOCKOUT_BASE_MS
#define PC_AUTH_LOCKOUT_BASE_MS 1000
#endif

/** @brief Maximum lockout duration in ms (the exponential backoff cap). */
#ifndef PC_AUTH_LOCKOUT_MAX_MS
#define PC_AUTH_LOCKOUT_MAX_MS 300000
#endif

// ---------------------------------------------------------------------------
// CSRF protection  (PC_ENABLE_CSRF)
// ---------------------------------------------------------------------------

/**
 * @brief Opt-in CSRF protection for state-changing HTTP requests.
 *
 * Default off (zero cost / no behavior change). When set, every POST / PUT /
 * PATCH / DELETE must carry a valid `X-CSRF-Token` header (a stateless,
 * HMAC-signed token); requests without one get 403 Forbidden. GET / HEAD /
 * OPTIONS are exempt (they are not state-changing). Clients fetch a token from
 * the built-in `GET /csrf` endpoint, which also sets it as the `csrf` cookie.
 * No server-side session storage - the token self-validates against an HMAC
 * secret seeded from the hardware RNG at begin(); it is independent of
 * PC_ENABLE_AUTH.
 */
#ifndef PC_ENABLE_CSRF
#define PC_ENABLE_CSRF 0
#endif

// ---------------------------------------------------------------------------
// Telnet sizing constants  (PC_ENABLE_TELNET must be 1)
// ---------------------------------------------------------------------------

/** @brief Maximum simultaneous Telnet connections. */
#ifndef MAX_TELNET_CONNS
#define MAX_TELNET_CONNS 2
#endif

/** @brief Stack buffer for one Telnet I/O chunk. */
#ifndef TELNET_BUF_SIZE
#define TELNET_BUF_SIZE 256
#endif

// ---------------------------------------------------------------------------
// SSH sizing constants  (PC_ENABLE_SSH must be 1)
// ---------------------------------------------------------------------------

/** @brief Maximum simultaneous SSH connections. */
#ifndef MAX_SSH_CONNS
#define MAX_SSH_CONNS 1
#endif

/**
 * @brief Maximum concurrent SSH channels per connection (RFC 4254 multiplexing).
 *
 * Default 1 - one "session" channel per connection, byte-for-byte the original
 * single-channel behavior. Raise it to multiplex several channels (e.g. several
 * concurrent shells/exec, or - with the forwarding build flags - tunnels) over one
 * SSH connection; each channel gets its own id, window, and peer state. Fixed BSS
 * (ssh_chan[MAX_SSH_CONNS][PC_SSH_MAX_CHANNELS]), no heap.
 */
#ifndef PC_SSH_MAX_CHANNELS
#define PC_SSH_MAX_CHANNELS 1
#endif
#if PC_SSH_MAX_CHANNELS < 1
#error "ProtoCore: PC_SSH_MAX_CHANNELS must be >= 1"
#endif

/**
 * @brief SSH TCP port forwarding (`direct-tcpip`, i.e. `ssh -L`). Default off.
 *
 * When set, the SSH server can open an outbound TCP connection to a client-named
 * host:port and bridge bytes between that socket and the SSH channel - the
 * `ssh_forward` owner does the I/O via the outbound client transport (pc_client),
 * so it needs `PC_CLIENT_CONNS >= PC_SSH_FWD_MAX` and a channel pool
 * (`PC_SSH_MAX_CHANNELS > 1`) to be useful. Forwarding is still opt-in at
 * runtime: nothing is forwarded until the application calls `pc_ssh_forward_begin()`.
 * Off = the channel codec refuses every `direct-tcpip` open (no open relay).
 */
#ifndef PC_SSH_PORT_FORWARD
#define PC_SSH_PORT_FORWARD 0
#endif

/** @brief Maximum concurrent forwarded TCP connections (must be <= PC_CLIENT_CONNS). */
#ifndef PC_SSH_FWD_MAX
#define PC_SSH_FWD_MAX 2
#endif

/** @brief Maximum forward target hostname length including null terminator. */
#ifndef PC_SSH_FWD_HOST_MAX
#define PC_SSH_FWD_HOST_MAX 64
#endif

/** @brief Blocking connect timeout (ms) when opening a forward target. */
#ifndef PC_SSH_FWD_CONNECT_MS
#define PC_SSH_FWD_CONNECT_MS 3000
#endif

/** @brief Max bytes moved per forward channel per poll, target -> client (<= SSH_PKT_BUF_SIZE). */
#ifndef PC_SSH_FWD_CHUNK
#define PC_SSH_FWD_CHUNK 1024
#endif

/**
 * @brief Maximum concurrent remote-forward listeners (`ssh -R` / `tcpip-forward`).
 *
 * Each accepted client that requests remote forwarding can bind up to this many
 * ports on the device; each binding consumes one `listener_pool[]` slot, so
 * `MAX_LISTENERS` must have that much headroom above the app's own listeners.
 * Remote forwarding shares `PC_SSH_PORT_FORWARD` (compiled in) and is inert
 * until `pc_ssh_forward_begin()`.
 */
#ifndef PC_SSH_RFWD_MAX
#define PC_SSH_RFWD_MAX 1
#endif

/**
 * @brief Maximum concurrent bridged connections across all remote forwards.
 *
 * Each connection accepted on a forwarded port occupies one transport `conn_pool`
 * slot plus one SSH channel (so it needs `PC_SSH_MAX_CHANNELS` headroom) and one
 * entry here while it is bridged back to the client.
 */
#ifndef PC_SSH_RFWD_BRIDGE_MAX
#define PC_SSH_RFWD_BRIDGE_MAX 2
#endif

/** @brief Packet assembly buffer per SSH connection (bytes). */
#ifndef SSH_PKT_BUF_SIZE
#define SSH_PKT_BUF_SIZE 2048
#endif

/**
 * @brief SFTP server subsystem over SSH (SSH_FXP_* v3, draft-ietf-secsh-filexfer-02). Default off.
 *
 * When set, an SSH client's `subsystem` request for "sftp" opens an SFTP session over the channel and the
 * device serves files from the mounted filesystem: open/read/write/opendir/readdir/stat/mkdir/rmdir/remove/
 * rename/realpath, with a fixed handle table and streamed reads/writes (zero heap). Mount a backend and set
 * the root once (pc_mnt_mount + pc_fs_begin), then start the server with pc_ssh_sftp_begin(). The
 * standards-track southbound path for secure file / G-code push over the one authenticated SSH port.
 * Requires PC_ENABLE_SSH (the channel) + PC_ENABLE_MNT (the storage).
 */
#ifndef PC_ENABLE_SSH_SFTP
#define PC_ENABLE_SSH_SFTP 0
#endif

/**
 * @brief SCP server over SSH (the legacy RCP protocol via `exec "scp -t/-f"`). Default off.
 *
 * When set, `scp file board:/path` (sink) and `scp board:/path file` (source) transfer a file to/from the
 * mounted filesystem. Storage is reached through the same accessor SFTP uses, so the mount root and the
 * path-traversal guard are shared rather than reimplemented; start it with pc_ssh_scp_begin(). v1 carries
 * its error/ack bytes inline on the channel (no CHANNEL_EXTENDED_DATA). Requires SSH + MNT.
 */
#ifndef PC_ENABLE_SSH_SCP
#define PC_ENABLE_SSH_SCP 0
#endif

/** @brief Max concurrent open SFTP handles (files + dirs) per SSH connection. */
#ifndef PC_SFTP_MAX_HANDLES
#define PC_SFTP_MAX_HANDLES 4
#endif

/** @brief SFTP packet-assembly buffer per SFTP channel (bytes); bounds one non-streamed request/response. */
#ifndef PC_SFTP_PKT_BUF
#define PC_SFTP_PKT_BUF 2048
#endif

/**
 * @brief Largest PC_SSH_FXP_DATA payload returned for one READ (a short read - the client re-requests). Kept
 *        within one SSH packet (SSH_PKT_BUF_SIZE minus framing), so bump SSH_PKT_BUF_SIZE too for throughput.
 */
#ifndef PC_SFTP_MAX_READ
#define PC_SFTP_MAX_READ 1024
#endif

/** @brief Largest absolute path the SFTP/SCP server resolves (mount root + request path). */
#ifndef PC_FILESYSTEM_PATH_MAX
#define PC_FILESYSTEM_PATH_MAX 256
#endif

/**
 * @brief SSH server-to-client compression (`zlib@openssh.com` / `zlib`, RFC 4253 sec 6.2). Default off.
 *
 * When set, the server advertises `zlib@openssh.com` (delayed, OpenSSH's default) and `zlib` for the
 * SERVER->CLIENT direction and, once active, compresses every outbound packet payload with a
 * context-takeover DEFLATE stream (a persistent sliding window carried across packets, sync-flushed
 * per packet - RFC 1951 / RFC 1950). Client->server stays `none`: SSH negotiates each direction
 * independently, and the inbound direction (keystrokes / uploads to the device) is tiny and, because
 * OpenSSH compresses outbound with Z_PARTIAL_FLUSH, would need a far larger resumable inflate engine
 * for little gain. `ssh -o Compression=yes` still gets real compression on the high-volume direction.
 *
 * PSRAM-class: each connection holds a compressor (~window + hash tables, tens of KB). A compile-time
 * guard rejects this on ARDUINO without PC_SSH_ZLIB_IN_PSRAM. Requires PC_ENABLE_SSH.
 */
#ifndef PC_ENABLE_SSH_ZLIB
#define PC_ENABLE_SSH_ZLIB 0
#endif

/**
 * @brief Place the per-connection SSH compression state in external PSRAM (ESP32).
 *
 * Like PC_H2_POOL_IN_PSRAM / PC_TLS_ARENA_IN_PSRAM: moves the compressor pool
 * (MAX_SSH_CONNS of them) to external RAM via `EXT_RAM_BSS_ATTR`. Needs a framework built with
 * `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` (tools/psram/README.md).
 */
#ifndef PC_SSH_ZLIB_IN_PSRAM
#define PC_SSH_ZLIB_IN_PSRAM 0
#endif

/**
 * @brief Acknowledge placing the SSH compressor in internal DRAM (no PSRAM).
 *
 * The per-connection compressor is ~48 KB. With MAX_SSH_CONNS=1 and no TLS server it fits internal
 * DRAM on a roomy chip (S3 / P4). Rather than force PSRAM, this mirrors PC_TLS_ACK_MULTI_CONN_DRAM:
 * set it to 1 to consciously accept the internal-DRAM cost when PC_SSH_ZLIB_IN_PSRAM is off. The
 * build otherwise fails fast on ARDUINO with guidance (below) instead of a raw linker overflow.
 */
#ifndef PC_SSH_ZLIB_ACK_DRAM
#define PC_SSH_ZLIB_ACK_DRAM 0
#endif

/**
 * @brief SSH s2c DEFLATE sliding-window size in bytes (max back-reference distance). Power of two,
 * 256..32768. Larger = better ratio + more per-connection RAM (the compressor holds a window-sized
 * work buffer + a window-sized hash chain). The client always allocates a 32 KB inflate window, so
 * any value here interoperates; 8 KB is a good ratio/RAM balance for terminal + command output.
 */
#ifndef PC_SSH_ZLIB_WINDOW
#define PC_SSH_ZLIB_WINDOW 8192
#endif

/**
 * @brief Largest uncompressed payload the s2c compressor accepts in one call (bytes). Outbound SSH
 * payloads are bounded by SSH_PKT_BUF_SIZE; this sizes the compressor's history+input work buffer.
 */
#ifndef PC_SSH_ZLIB_MAX_IN
#define PC_SSH_ZLIB_MAX_IN 2048
#endif

/** @brief Maximum SSH username length including null terminator. */
#ifndef SSH_MAX_USERNAME_LEN
#define SSH_MAX_USERNAME_LEN 32
#endif

/** @brief Maximum SSH password length including null terminator. */
#ifndef SSH_MAX_PASSWORD_LEN
#define SSH_MAX_PASSWORD_LEN 64
#endif

/**
 * @brief Size in bytes of the shared per-dispatch scratch arena.
 *
 * Codec / protocol handlers borrow transient working memory from this single BSS
 * arena (see mmgr/plaintext.h) instead of each feature owning a
 * dedicated buffer. The session layer empties it before every event dispatch, so
 * it only needs to hold the *peak concurrent* scratch of any one dispatch, not
 * the sum across features. Tune from the pc_plaintext_high_water() reading on a real
 * workload; an over-budget borrow fails closed (pc_plaintext_alloc returns NULL).
 */
#ifndef PC_PLAINTEXT_ARENA_SIZE
#define PC_PLAINTEXT_ARENA_SIZE 8192
#endif

/**
 * @brief Compile the library's internal debug checks (default 0 = off).
 *
 * Deliberately NOT keyed on NDEBUG. Whether NDEBUG is defined is a property of whichever toolchain
 * happens to build the library - the Arduino ESP32 core does not define it - so keying on it means
 * nobody actually chose. This is the switch we control.
 *
 * What it enables today: the pools' owner tripwire, which records the first execution context to
 * touch each slot and asserts every later borrow matches. That catches a borrow crossing tasks - the
 * one way the lock-free single-accessor invariant can break - and turns a silent cross-core race into
 * an immediate failure.
 *
 * Measured cost on an ESP32-S3 at 240 MHz: ~52 cycles per pool entry point, and a
 * mark + alloc + release touches three of them, so roughly 156 cycles on every borrow. Worth paying
 * while chasing a memory bug; not worth shipping.
 */
#ifndef PC_DEBUG_CHECKS
#define PC_DEBUG_CHECKS 0
#endif

/**
 * @brief Bytes a worker's generator draws before it redraws its seed from the platform.
 *
 * The generator's own pace, not a caller's: nothing in crypto/rng/rng.h lets a consumer ask for a
 * reseed, because a consumer that could ask would set the rate, and the module that asked most often
 * would set it for everyone. A draw is answered from the keystream and the seed is redrawn once this
 * budget is spent.
 *
 * Lower spends more platform entropy for a shorter window per seed; higher does the reverse. The
 * forward ratchet already makes an earlier draw unrecoverable from the current state, so this bounds
 * the other direction - how long one platform draw is relied on - rather than backward secrecy.
 */
#ifndef PC_RAND_RESEED_BYTES
#define PC_RAND_RESEED_BYTES (1u << 20)
#endif

/**
 * @brief Worst-case bytes each module borrows from the secure pool in a single call.
 *
 * Declared here because PC_SECURE_ARENA_SIZE below is derived from them and this is the one
 * place that can see them all - a module header cannot host its own, since every module header
 * includes this file. Each value is PROVED where the struct lives: the owning .cpp carries a
 * static_assert(sizeof(X) <= PC_WORK_X), so a working set that grows past its declaration fails
 * the build naming itself, rather than exhausting the pool at run time.
 *
 * These are SIZES, not offsets. Nothing here couples one module to another: each is a term in a
 * sum, order is irrelevant, and adding a module shifts no one. That is the difference from the
 * crypto_work region map these replaced.
 *
 * Values are what the ESP32 toolchain reported for the real structs, rounded up.
 */
#ifndef PC_WORK_BIGNUM_HW
#define PC_WORK_BIGNUM_HW 1024
#endif
#ifndef PC_WORK_BIGNUM_SW
#define PC_WORK_BIGNUM_SW 1408
#endif
// AES-256-GCM keyed context. Sized per vendor for the same reason as the bignum working set above: one
// backend or the other is compiled, never both. It matters more here than it did as a transient
// borrow - a consumer now embeds two of these per connection (send and receive) for the life of the
// key, so one flat figure sized for the largest backend is RAM every target pays and only one uses.
// The static_assert in each backend is what keeps these honest against a vendor header we do not own.
#ifndef PC_WORK_AESGCM_HW
#define PC_WORK_AESGCM_HW 416 // mbedtls_gcm_context measures 392 on the S3
#endif
#ifndef PC_WORK_AESGCM_SW
#define PC_WORK_AESGCM_SW 640 // GcmWork is 608: AES-256 round keys + the 4-bit GHASH table
#endif
#ifndef PC_WORK_AESGCM
#if PC_HAS_HW_AESGCM
#define PC_WORK_AESGCM PC_WORK_AESGCM_HW
#else
#define PC_WORK_AESGCM PC_WORK_AESGCM_SW
#endif
#endif
#ifndef PC_WORK_AESCCM
#define PC_WORK_AESCCM 448
#endif
// AES-128 single-block context (QUIC/DTLS header + sequence-number protection). Kept per key for the
// same reason as the AEAD contexts, though the win is smaller: measured on an S3, rebuilding it per
// record costs ~556 cycles plus a pool borrow and wipe. (The ECB block it protects is ~7,842 cycles on
// its own - one HW-AES operation - which is now the larger per-packet cost in QUIC and DTLS.)
#ifndef PC_WORK_AES128_HW
#define PC_WORK_AES128_HW 288 // mbedtls_aes_context: nr + rk + buf[68]
#endif
#ifndef PC_WORK_AES128_SW
#define PC_WORK_AES128_SW 176 // uint32_t rk[44]
#endif
#ifndef PC_WORK_AES128
#if PC_HAS_HW_AESGCM
#define PC_WORK_AES128 PC_WORK_AES128_HW
#else
#define PC_WORK_AES128 PC_WORK_AES128_SW
#endif
#endif

// AES-128-GCM keyed context, sized per vendor exactly as PC_WORK_AESGCM above and for the same reason:
// one backend is compiled, and a consumer holding a context per direction should not carry storage for
// the backend it did not build.
#ifndef PC_WORK_AES128GCM_HW
#define PC_WORK_AES128GCM_HW 416 // mbedtls_gcm_context measures 392 on the S3
#endif
#ifndef PC_WORK_AES128GCM_SW
#define PC_WORK_AES128GCM_SW 576 // Aes128GcmWork is 560: AES-128 round keys + the 4-bit GHASH table
#endif
#ifndef PC_WORK_AES128GCM
#if PC_HAS_HW_AESGCM
#define PC_WORK_AES128GCM PC_WORK_AES128GCM_HW
#else
#define PC_WORK_AES128GCM PC_WORK_AES128GCM_SW
#endif
#endif
#ifndef PC_WORK_CHACHAPOLY
#define PC_WORK_CHACHAPOLY 64
#endif
#ifndef PC_WORK_CHACHA20
#define PC_WORK_CHACHA20 192
#endif
#ifndef PC_WORK_AES256CTR
#define PC_WORK_AES256CTR 384
#endif
#ifndef PC_WORK_HMAC_SHA256
#define PC_WORK_HMAC_SHA256 288 // 272 on the SSH example, 276 with DTLS+PQC (flags change the SHA ctx); headroom to 288
#endif
#ifndef PC_WORK_POLY1305
#define PC_WORK_POLY1305 80
#endif
#ifndef PC_WORK_MD
#define PC_WORK_MD 96
#endif

// The two tables a module holds for the life of the program rather than for the life of a call.
// They take the persistent end of the arena, so they are stated here for the same reason every
// working set is: the pool is sized off what the build declares, and an undeclared borrow is one
// the pool has no room for.
#ifndef PC_WORK_ROUTE_TABLE
#define PC_WORK_ROUTE_TABLE (MAX_ROUTES * 104 + 16) // HttpRoute is 88 with every gated id compiled
#endif
#ifndef PC_WORK_AUTH_TABLE
#define PC_WORK_AUTH_TABLE (MAX_ROUTES * (3 * MAX_AUTH_LEN + 8) + 32) // AuthCred is 3*MAX_AUTH_LEN + 1
#endif
#ifndef PC_WORK_RNG
#define PC_WORK_RNG 72 // the generator: seed(32) + nonce(8) + ratchet scratch(32), for the program's life
#endif

/**
 * @brief Size in bytes of the per-slot SECURE pool (see mmgr/secure.h), DERIVED.
 *
 * Not a chosen number. Every borrow from this pool is a working set some module declares - see the
 * PC_WORK_* constants, each proved against its struct's sizeof by a static_assert in the module that
 * owns it. The floor is the sum of the ones a build actually compiles.
 *
 * A sum, not a deepest-nest figure. The sum is a strict upper bound - correct however those working
 * sets nest under one another - whereas a nest depth is only correct while the call graph stays as it
 * is. This value must not be wrong, so it buys certainty with a little slack.
 *
 * A module whose working set grows past its declaration fails the build, naming itself, instead of
 * exhausting the pool at run time. Override PC_SECURE_ARENA_SIZE to pin a size regardless.
 */
#ifndef PC_SECURE_ARENA_SIZE

// The modexp dominates: one backend or the other is compiled, never both.
#if PC_HAS_HW_BIGNUM
#define PC_SECURE_WORK_BIGNUM PC_WORK_BIGNUM_HW
#else
#define PC_SECURE_WORK_BIGNUM PC_WORK_BIGNUM_SW
#endif

// Feature-gated terms: a build pays only for the code it compiled.
#if PC_ENABLE_SSH || PC_ENABLE_SSH_CLIENT || PC_ENABLE_TLS || PC_ENABLE_HTTP3 || PC_ENABLE_DTLS
#define PC_SECURE_WORK_AEAD (PC_WORK_AESGCM + PC_WORK_CHACHAPOLY + PC_WORK_CHACHA20 + PC_WORK_POLY1305)
#define PC_SECURE_WORK_MAC PC_WORK_HMAC_SHA256
#else
#define PC_SECURE_WORK_AEAD 0
#define PC_SECURE_WORK_MAC 0
#endif

#if PC_ENABLE_SMB
#define PC_SECURE_WORK_SMB (PC_WORK_AESCCM + PC_WORK_AES128GCM + PC_WORK_MD)
#else
#define PC_SECURE_WORK_SMB 0
#endif

#if PC_ENABLE_SSH || PC_ENABLE_SSH_CLIENT
#define PC_SECURE_WORK_SSHCIPHER PC_WORK_AES256CTR
#else
#define PC_SECURE_WORK_SSHCIPHER 0
#endif

#if PC_ENABLE_AUTH
#define PC_SECURE_WORK_AUTH PC_WORK_AUTH_TABLE
#else
#define PC_SECURE_WORK_AUTH 0
#endif

#define PC_SECURE_ARENA_SIZE                                                                                           \
    (PC_SECURE_WORK_BIGNUM + PC_SECURE_WORK_AEAD + PC_SECURE_WORK_MAC + PC_SECURE_WORK_SMB +                           \
     PC_SECURE_WORK_SSHCIPHER + PC_WORK_ROUTE_TABLE + PC_SECURE_WORK_AUTH + PC_WORK_RNG +                              \
     256) // + 256: alignment round-up across the individual borrows
#endif

// ---------------------------------------------------------------------------
// Static RAM (BSS) usage table
// ---------------------------------------------------------------------------
//
// All library memory is in BSS - allocated at link time, zero-initialized by
// the C runtime, never heap-allocated after begin().  The table below shows
// the contribution of every feature at its default constant values.
//
// Sizes are for ESP32 (32-bit pointers, int = 4 B).  Where a size depends on
// a macro the formula is given so you can compute the impact of any change.
//
// ┌──────────────────────────────┬──────────────────────────────────────────────────────────────┬──────────┐
// │ Symbol / pool                │ Size formula                                                 │ Default  │
// ├──────────────────────────────┼──────────────────────────────────────────────────────────────┼──────────┤
// │ TRANSPORT LAYER (always on)  │                                                              │          │
// │  conn_pool[MAX_CONNS]        │ MAX_CONNS × (RX_BUF_SIZE + 22)                              │  4 168 B │
// │  listener_pool[MAX_LISTENERS]│ MAX_LISTENERS × (StaticQueue_t≈48 + EVT_QUEUE_DEPTH×12 + 18)│    654 B │
// │  conn_timeout_ms             │ 4 B                                                          │      4 B │
// │  TRANSPORT SUBTOTAL          │                                                              │  4 826 B │
// ├──────────────────────────────┼──────────────────────────────────────────────────────────────┼──────────┤
// │ HTTP PRESENTATION (always on)│                                                              │          │
// │  http_pool[MAX_CONNS]        │ MAX_CONNS × (MAX_PATH_LEN + MAX_QUERY_LEN                   │          │
// │                              │   + MAX_HEADERS×(MAX_KEY_LEN+MAX_VAL_LEN)                   │          │
// │                              │   + MAX_QUERY_PARAMS×(QUERY_KEY_LEN+QUERY_VAL_LEN)          │          │
// │                              │   + BODY_BUF_SIZE + 50)                                     │  6 668 B │
// │  HTTP SUBTOTAL               │                                                              │  6 668 B │
// ├──────────────────────────────┼──────────────────────────────────────────────────────────────┼──────────┤
// │ WEBSOCKET (PC_ENABLE_WEBSOCKET=1)                                                        │          │
// │  ws_pool[MAX_WS_CONNS]       │ MAX_WS_CONNS × (WS_FRAME_SIZE + 29)                         │  1 082 B │
// ├──────────────────────────────┼──────────────────────────────────────────────────────────────┼──────────┤
// │ SSE (PC_ENABLE_SSE=1)     │                                                              │          │
// │  pc_sse_pool[MAX_SSE_CONNS]     │ MAX_SSE_CONNS × (MAX_PATH_LEN + 3)                          │    134 B │
// ├──────────────────────────────┼──────────────────────────────────────────────────────────────┼──────────┤
// │ SSH (PC_ENABLE_SSH=1)     │                                                              │          │
// │  ssh_pool[MAX_SSH_CONNS]     │ MAX_SSH_CONNS × (SSH_PKT_BUF_SIZE + 22)                     │  2 070 B │
// │  ssh_keys[MAX_SSH_CONNS]     │ MAX_SSH_CONNS × sizeof(SshKeyMat)                            │   1187 B │
// │   └─ SshKeyMat (all builds)  │   2×aes_key[32] + 2×aes_iv[16] + 2×mac_key[64]               │          │
// │                              │   + 2×chacha_key[64] + 3 flags = 355 B, plus the two keyed   │          │
// │                              │   GCM contexts 2×PC_WORK_AESGCM (832 B on a vendor AEAD).    │          │
// │                              │   The contexts buy ~9,200 cycles per packet - a FIXED cost   │          │
// │                              │   that dominates small interactive traffic (see aesgcm.h).   │          │
// │                              │   CTR still rebuilds its schedule in scratch per packet.     │          │
// │  ssh_dh[MAX_SSH_CONNS]       │ MAX_SSH_CONNS × (3×pc_bignum[256] + H[32] + 1)              │    801 B │
// │  crypto_work[]               │ PC_CRYPTO_WORK_SIZE (scratch, wiped after each use)         │  2 144 B │
// │  SSH SUBTOTAL                │                                                              │  5 370 B │
// ├──────────────────────────────┼──────────────────────────────────────────────────────────────┼──────────┤
// │ GRAND TOTAL (all features)   │                                                              │ ≈18 KB   │
// └──────────────────────────────┴──────────────────────────────────────────────────────────────┴──────────┘
//
// ESP32 has 320 KB of SRAM; the library uses ~5–18 KB depending on features.
// Stack usage is separate; the largest frame is during SSH DH key exchange
// (~256 B for the pc_bignum private scalar on the call stack before it is
// zeroed by ssh_dh_finish()).
//
// SSH KEY MATERIAL IS NOT IN THE TABLE ABOVE intentionally:
//   - The RSA host private key is NEVER stored in any static array.  It is
//     loaded from NVS into a local stack frame at sign time, used once, then
//     explicitly zeroed (volatile memset) before the function returns.
//   - AES session keys and HMAC keys live in ssh_keys[] (above), which is a
//     separate BSS symbol from ssh_pool[].  Physical separation means a
//     buffer overflow in the packet receive path (ssh_pool[].pkt_buf) cannot
//     reach the key material without crossing a distinct linker symbol - a
//     significant barrier against heap/BSS spray attacks.
//   - The DH ephemeral private scalar y lives in ssh_dh[].y and is zeroed
//     immediately after the shared secret K is derived.
//   - crypto_work[] is zeroed via pc_secure_wipe() after every use so that
//     bignum intermediates (including partial products that contain key
//     material) do not persist in memory.

// ---------------------------------------------------------------------------
// Runtime configuration struct
// ---------------------------------------------------------------------------

/**
 * @brief Runtime-tunable server parameters.
 *
 * Can be declared as `const PROGMEM` (flash) or as a mutable variable (RAM).
 * Pass a pointer to proto_begin() / begin_http() / begin_tls(), which hand it
 * to Tcp.conn->init().
 */
typedef struct WebServerConfig
{
    /** Milliseconds of inactivity before a connection is force-closed. */
    proto_u32 conn_timeout_ms;
} WebServerConfig;

// ---------------------------------------------------------------------------
// Protocol identifier
// ---------------------------------------------------------------------------

/**
 * @brief Application protocol spoken on a listener port or connection slot.
 *
 * Stored in both Listener::proto and TcpConn::proto.  The session layer uses
 * this to route events to the correct protocol handler without branching on
 * port numbers.
 *
 * All values are always present regardless of feature flags - the enum is
 * part of the listener API.  Feature flags gate the implementation, not the
 * identifier.
 */
typedef enum PROTO_ENUM_PACKED
{
    PROTO_NONE = 0,         ///< Unassigned slot.
    PROTO_HTTP = 1,         ///< HTTP/1.1 with optional WS and SSE upgrades.
    PROTO_TELNET = 2,       ///< Telnet (RFC 854).
    PROTO_SSH = 3,          ///< SSH (RFC 4253/4252/4254).
    PROTO_MODBUS = 4,       ///< Modbus TCP slave (Modbus Application Protocol).
    PROTO_OPCUA = 5,        ///< OPC UA Binary (UA-TCP) server.
    PROTO_SSH_RFWD = 6,     ///< SSH remote-forward listener (ssh -R): accepts bridge to a forwarded-tcpip channel.
    PROTO_RELAY = 7,        ///< TCP relay / DNAT (PC_ENABLE_RELAY): bridge to an origin pc_client connection.
    PROTO_BRIDGE = 8,       ///< address:port -> hardware bus (PC_ENABLE_IFACE_BRIDGE): UART/SPI/I2C device server.
    PROTO_NTRIP_CASTER = 9, ///< NTRIP caster (PC_ENABLE_NTRIP_CASTER): serves RTCM3 corrections to rovers.
    PROTO_MESH = 10, ///< Edge-cache sibling link (PC_ENABLE_EDGE_MESH): answers a peer's content-addressed query.
} ConnProto;

/**
 * @brief What an interface is, and the filter that selects one.
 *
 * One vocabulary for both jobs. A registered interface carries its kind (layer 1 keeps the
 * registry); a route or a connection carries the same value as a filter, where PC_IF_ANY means
 * "no filter". The wifi/eth values are what a connection is stamped with at accept time by
 * comparing its local IP to the softAP IP; a bus or radio interface is registered by the
 * application and forwarded to like any other.
 */
typedef enum PROTO_ENUM_PACKED
{
    PC_IF_ANY = 0,      ///< unspecified kind, and the filter that matches any interface
    PC_IF_WIFI_STA = 1, ///< station interface (joined to an AP / your LAN)
    PC_IF_WIFI_AP = 2,  ///< softAP interface (clients joined to the device)
    PC_IF_ETH = 3,      ///< wired Ethernet PHY
    PC_IF_BUS = 4,      ///< a bus bridged onto the network (uart, spi, can)
    PC_IF_RADIO = 5,    ///< a non-wifi radio
} pc_if_kind;

// Both of these are struct members (TcpConn::proto, TcpConn::iface, Listener::proto, and a route's
// interface gate), so their width is per-slot BSS rather than a detail of the type. Pinned here
// because the pool sizes in this file are what the footprint is computed from.
static_assert(sizeof(ConnProto) == 1, "ConnProto must stay one byte: it is a per-slot struct member");
static_assert(sizeof(pc_if_kind) == 1, "pc_if_kind must stay one byte: it is a per-slot struct member");

// ---------------------------------------------------------------------------
// Compile-time sanity checks
// ---------------------------------------------------------------------------
// These produce a clear #error message in the compiler output rather than a
// cryptic linker failure or silent misbehavior.

#if EVT_QUEUE_DEPTH < MAX_CONNS * 4
#error "ProtoCore: EVT_QUEUE_DEPTH must be >= MAX_CONNS * 4 to absorb event bursts without blocking lwIP"
#endif

#if MAX_CONNS < 1
#error "ProtoCore: MAX_CONNS must be >= 1"
#endif

#if MAX_CONNS > 255
#error "ProtoCore: MAX_CONNS must be <= 255 (slot IDs are uint8_t)"
#endif

#if PC_ENABLE_WEBSOCKET && PC_ENABLE_SSE
#if MAX_WS_CONNS + MAX_SSE_CONNS > MAX_CONNS
#error "ProtoCore: MAX_WS_CONNS + MAX_SSE_CONNS must not exceed MAX_CONNS"
#endif
#elif PC_ENABLE_WEBSOCKET
#if MAX_WS_CONNS > MAX_CONNS
#error "ProtoCore: MAX_WS_CONNS must not exceed MAX_CONNS"
#endif
#elif PC_ENABLE_SSE
#if MAX_SSE_CONNS > MAX_CONNS
#error "ProtoCore: MAX_SSE_CONNS must not exceed MAX_CONNS"
#endif
#endif

#if BODY_BUF_SIZE < 1
#error "ProtoCore: BODY_BUF_SIZE must be >= 1"
#endif

#if BODY_BUF_SIZE > RX_BUF_SIZE
#error "ProtoCore: BODY_BUF_SIZE must not exceed RX_BUF_SIZE (parser reads from the ring buffer)"
#endif

#if PC_ENABLE_FILE_SERVING && FILE_CHUNK_SIZE > RX_BUF_SIZE
#error "ProtoCore: FILE_CHUNK_SIZE must not exceed RX_BUF_SIZE"
#endif

#if MAX_KEY_LEN < 4
#error "ProtoCore: MAX_KEY_LEN must be >= 4 (minimum valid HTTP header name length)"
#endif

#if MAX_VAL_LEN < 1
#error "ProtoCore: MAX_VAL_LEN must be >= 1"
#endif

#if MAX_PATH_LEN < 2
#error "ProtoCore: MAX_PATH_LEN must be >= 2 (minimum: \"/\")"
#endif

#if MAX_ROUTES < 1
#error "ProtoCore: MAX_ROUTES must be >= 1"
#endif

#if MAX_MIDDLEWARE < 1
#error "ProtoCore: MAX_MIDDLEWARE must be >= 1"
#endif

#if CHUNK_BUF_SIZE < 16
#error "ProtoCore: CHUNK_BUF_SIZE must be >= 16"
#endif

#if JSON_MAX_DEPTH < 1
#error "ProtoCore: JSON_MAX_DEPTH must be >= 1"
#endif

#if RE_MAX_STEPS < 64
#error "ProtoCore: RE_MAX_STEPS must be >= 64"
#endif

// RSA-2048 verification (OIDC / SSH host key / JWKS) runs on a worker task and consumes
// ~7 KB of stack via the mbedTLS bignum modexp. Enforce the documented floor so a lowered
// worker stack is caught at build time instead of overflowing on the first verify.
#if PC_ENABLE_OIDC && !PC_ENABLE_SSH && (PC_WORKER_TASK_STACK < PC_WORKER_STACK_RSA_MIN)
#error                                                                                                                 \
    "ProtoCore: PC_WORKER_TASK_STACK is below PC_WORKER_STACK_RSA_MIN; RSA-2048 verification (OIDC) needs ~7 KB of worker stack - raise PC_WORKER_TASK_STACK (>= 8192) or marshal RSA verifies onto a dedicated larger-stack task"
#endif

// SSH additionally can negotiate curve25519-sha256 + ssh-ed25519, whose software field
// arithmetic peaks at ~10.5 KB of worker stack (deeper than the RSA path). Enforce the
// higher floor so a lowered stack is caught at build time instead of tripping the task
// stack canary on the first modern-crypto handshake.
#if (PC_ENABLE_SSH || PC_ENABLE_SSH_CLIENT || PC_ENABLE_HTTP3) && (PC_WORKER_TASK_STACK < PC_WORKER_STACK_CURVE_MIN)
#error                                                                                                                 \
    "ProtoCore: PC_WORKER_TASK_STACK is below PC_WORKER_STACK_CURVE_MIN; SSH (server or reverse-SSH client) and HTTP/3 (QUIC TLS-1.3) curve25519/ed25519 need ~10.5 KB of worker stack - raise PC_WORKER_TASK_STACK (>= 12288) or marshal the handshake onto a dedicated larger-stack task"
#endif

// The PQ/T hybrid KEX (PC_ENABLE_PQC_KEX) runs ML-KEM-768 Encaps in the handshake path, whose NTT
// + sampling peak at ~7 KB of worker stack on top of the classical curve/ed25519 work. Enforce a
// higher floor so it is caught at build time rather than overflowing on the first hybrid handshake.
#ifndef PC_WORKER_STACK_PQC_MIN
#define PC_WORKER_STACK_PQC_MIN 16384
#endif
#if PC_ENABLE_PQC_KEX && !PC_ENABLE_SSH_SNTRUP761 && (PC_ENABLE_SSH || PC_ENABLE_SSH_CLIENT || PC_ENABLE_HTTP3) &&     \
    (PC_WORKER_TASK_STACK < PC_WORKER_STACK_PQC_MIN)
#error                                                                                                                 \
    "ProtoCore: PC_WORKER_TASK_STACK is below PC_WORKER_STACK_PQC_MIN; the ML-KEM-768 hybrid KEX (PC_ENABLE_PQC_KEX) needs ~7 KB more worker stack - raise PC_WORKER_TASK_STACK (>= 16384) or marshal the handshake onto a dedicated larger-stack task"
#endif

// sntrup761x25519-sha512 (PC_ENABLE_SSH_SNTRUP761, on by default with the hybrid) is heavier than
// ML-KEM: the server runs Encaps (~22 KB), the reverse-SSH client runs KeyGen+Decaps whose FO
// re-encrypt peaks ~32 KB. Enforce the matching floor so it is caught at build time.
#ifndef PC_WORKER_STACK_SNTRUP_MIN
#if defined(PC_ENABLE_SSH_CLIENT) && PC_ENABLE_SSH_CLIENT
#define PC_WORKER_STACK_SNTRUP_MIN 40960
#else
#define PC_WORKER_STACK_SNTRUP_MIN 32768
#endif
#endif
// sntrup761x25519-sha512 is an SSH key exchange; only an SSH server / reverse-SSH client runs its heavy
// KeyGen/Encaps/Decaps on the worker stack. HTTP/3's PQC is the lighter ML-KEM hybrid, so an HTTP/3-only
// build (PC_ENABLE_SSH_SNTRUP761 defaults on with PQC but is dormant without SSH) must not trip this.
#if PC_ENABLE_SSH_SNTRUP761 && (PC_ENABLE_SSH || PC_ENABLE_SSH_CLIENT) &&                                              \
    (PC_WORKER_TASK_STACK < PC_WORKER_STACK_SNTRUP_MIN)
#error                                                                                                                 \
    "ProtoCore: PC_WORKER_TASK_STACK is below PC_WORKER_STACK_SNTRUP_MIN; sntrup761x25519-sha512 needs ~32 KB worker stack for the server Encaps and ~40 KB for the reverse-SSH client KeyGen+Decaps - raise PC_WORKER_TASK_STACK (>= 32768 server / 40960 client), set PC_ENABLE_SSH_SNTRUP761 0 to keep ML-KEM only, or marshal the handshake onto a dedicated larger-stack task"
#endif

#if PC_ENABLE_TLS
#if MAX_TLS_CONNS < 1 || MAX_TLS_CONNS > MAX_CONNS
#error "ProtoCore: MAX_TLS_CONNS must be between 1 and MAX_CONNS"
#endif
#if PC_TLS_ARENA_SIZE < 8192
#error "ProtoCore: PC_TLS_ARENA_SIZE is far too small for a TLS handshake"
#endif
// Concurrent TLS guard: the whole arena is static .bss and the ESP32 internal
// dram0_0_seg ceiling is only ~122 KB (ROM-reserved at both ends), so a 2nd
// connection's arena overflows the link. Reject MAX_TLS_CONNS > 1 with a clear
// message unless the arena is offloaded to PSRAM or the build was consciously sized -
// far friendlier than the raw "region `dram0_0_seg' overflowed" linker error.
#if PROTOCORE_HOT && (MAX_TLS_CONNS > 1) && !PC_TLS_ARENA_IN_PSRAM && !PC_TLS_ACK_MULTI_CONN_DRAM
#error                                                                                                                 \
    "ProtoCore: MAX_TLS_CONNS > 1 - the static TLS arena will not fit the ~122 KB internal dram0_0_seg. Pick a path (docs/KNOWN_LIMITATIONS.md): set PC_TLS_ARENA_IN_PSRAM=1 on a PSRAM board, OR shrink records via a custom ESP-IDF build (CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN + PC_TLS_MAX_FRAG_LEN), OR reclaim internal DRAM; then set PC_TLS_ACK_MULTI_CONN_DRAM=1 to confirm."
#endif
#endif

// HTTP/2's per-connection engine pool (~MAX_CONNS x 28 KB) cannot fit internal DRAM alongside
// TLS, so it must live in PSRAM. Fail fast with guidance instead of the raw linker overflow.
#if PC_ENABLE_HTTP2 && PROTOCORE_HOT && !PC_H2_POOL_IN_PSRAM
#error                                                                                                                 \
    "ProtoCore: PC_ENABLE_HTTP2 needs PSRAM - the HTTP/2 engine pool (~MAX_CONNS x 28 KB) overflows the ~122 KB internal dram0_0_seg alongside TLS. Set PC_H2_POOL_IN_PSRAM=1 on a PSRAM board (S3 / P4 / WROVER) built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y (tools/psram/README.md)."
#endif

#if PC_ENABLE_SNMP
#if SNMP_MAX_OID_LEN < 4
#error "ProtoCore: SNMP_MAX_OID_LEN must be >= 4"
#endif
#if SNMP_MAX_MIB_ENTRIES < 1
#error "ProtoCore: SNMP_MAX_MIB_ENTRIES must be >= 1"
#endif
#if SNMP_MAX_VARBINDS < 1
#error "ProtoCore: SNMP_MAX_VARBINDS must be >= 1"
#endif
#if SNMP_MSG_BUF_SIZE < 484
#error "ProtoCore: SNMP_MSG_BUF_SIZE must be >= 484 (RFC 1157 minimum)"
#endif
#endif

#if PC_ENABLE_COAP
#if PC_COAP_MAX_RESOURCES < 1
#error "ProtoCore: PC_COAP_MAX_RESOURCES must be >= 1"
#endif
#if PC_COAP_MAX_PATH < 2
#error "ProtoCore: PC_COAP_MAX_PATH must be >= 2 (minimum: \"/\")"
#endif
#if PC_COAP_MAX_PAYLOAD < 1
#error "ProtoCore: PC_COAP_MAX_PAYLOAD must be >= 1"
#endif
#if PC_COAP_MSG_BUF_SIZE < (PC_COAP_MAX_PAYLOAD + 16)
#error                                                                                                                 \
    "ProtoCore: PC_COAP_MSG_BUF_SIZE must be >= PC_COAP_MAX_PAYLOAD + 16 (header + token + Content-Format option + payload marker)"
#endif
#endif

#if PC_ENABLE_AUTH && MAX_AUTH_LEN < 2
#error "ProtoCore: MAX_AUTH_LEN must be >= 2 when PC_ENABLE_AUTH is set"
#endif

#if PC_ENABLE_PER_IP_THROTTLE
#if PC_PER_IP_THROTTLE_SLOTS < 1
#error "ProtoCore: PC_PER_IP_THROTTLE_SLOTS must be >= 1 when PC_ENABLE_PER_IP_THROTTLE is set"
#endif
#if PC_PER_IP_THROTTLE_MAX < 1
#error "ProtoCore: PC_PER_IP_THROTTLE_MAX must be >= 1 when PC_ENABLE_PER_IP_THROTTLE is set"
#endif
#endif

#if PC_ENABLE_IP_ALLOWLIST && PC_IP_ALLOWLIST_SLOTS < 1
#error "ProtoCore: PC_IP_ALLOWLIST_SLOTS must be >= 1 when PC_ENABLE_IP_ALLOWLIST is set"
#endif

#if PC_ENABLE_AUTH_LOCKOUT
#if !PC_ENABLE_AUTH
#error "ProtoCore: PC_ENABLE_AUTH_LOCKOUT requires PC_ENABLE_AUTH"
#endif
#if PC_AUTH_LOCKOUT_SLOTS < 1
#error "ProtoCore: PC_AUTH_LOCKOUT_SLOTS must be >= 1 when PC_ENABLE_AUTH_LOCKOUT is set"
#endif
#if PC_AUTH_LOCKOUT_THRESHOLD < 1
#error "ProtoCore: PC_AUTH_LOCKOUT_THRESHOLD must be >= 1 when PC_ENABLE_AUTH_LOCKOUT is set"
#endif
#if PC_AUTH_LOCKOUT_BASE_MS < 1 || PC_AUTH_LOCKOUT_MAX_MS < PC_AUTH_LOCKOUT_BASE_MS
#error "ProtoCore: need 1 <= PC_AUTH_LOCKOUT_BASE_MS <= PC_AUTH_LOCKOUT_MAX_MS"
#endif
// The backoff doubles a uint32 capped at MAX_MS, so MAX_MS must leave headroom for
// one more shift (cap <= 0x80000000 => cap<<1 fits in uint32 without overflow).
#if PC_AUTH_LOCKOUT_MAX_MS > 0x80000000
#error "ProtoCore: PC_AUTH_LOCKOUT_MAX_MS must be <= 0x80000000 (2147483648)"
#endif
#endif

#if PC_ENABLE_FORWARDED_TRUST
#if !PC_ENABLE_AUTH_LOCKOUT
#error "ProtoCore: PC_ENABLE_FORWARDED_TRUST requires PC_ENABLE_AUTH_LOCKOUT"
#endif
#if PC_TRUSTED_PROXY_MAX < 1
#error "ProtoCore: PC_TRUSTED_PROXY_MAX must be >= 1 when PC_ENABLE_FORWARDED_TRUST is set"
#endif
#endif

#if PC_ENABLE_WEBDAV
#if !PC_ENABLE_FILE_SERVING
#error "ProtoCore: PC_ENABLE_WEBDAV requires PC_ENABLE_FILE_SERVING"
#endif
#if PC_WEBDAV_BUF_SIZE < 256
#error "ProtoCore: PC_WEBDAV_BUF_SIZE must be >= 256"
#endif
#if PC_METHOD_BUF_SIZE < 10
#error "ProtoCore: PC_METHOD_BUF_SIZE must be >= 10 when PC_ENABLE_WEBDAV is set (PROPPATCH)"
#endif
#endif

#if PC_NEED_MODBUS
#if PC_MODBUS_COILS < 1 || PC_MODBUS_DISCRETE_INPUTS < 1 || PC_MODBUS_HOLDING_REGS < 1 || PC_MODBUS_INPUT_REGS < 1
#error "ProtoCore: each PC_MODBUS_* table size must be >= 1 when PC_ENABLE_MODBUS is set"
#endif
#endif

#if PC_ENABLE_KEEPALIVE && PC_KEEPALIVE_MAX_REQUESTS < 1
#error "ProtoCore: PC_KEEPALIVE_MAX_REQUESTS must be >= 1 when PC_ENABLE_KEEPALIVE is set"
#endif

#if PC_ENABLE_RANGE && !PC_ENABLE_FILE_SERVING && !PC_ENABLE_EDGE_CACHE
#error "ProtoCore: PC_ENABLE_RANGE requires PC_ENABLE_FILE_SERVING or PC_ENABLE_EDGE_CACHE"
#endif

// The file-transfer servers and file serving all reach storage through the filesystem accessor,
// which is the HAL and points them at whatever is mounted. None of them needs the mount SERVICE:
// they need the seam, and the seam fails closed when nothing is behind it. Requiring PC_ENABLE_MNT
// would drag the RAM backend's pool into every build that moves a file, to satisfy a type.
#define PC_ENABLE_MTLS_NEEDS_TLS PC_ENABLE_TLS
#if PC_ENABLE_MTLS && !PC_ENABLE_MTLS_NEEDS_TLS
#error "ProtoCore: PC_ENABLE_MTLS needs PC_ENABLE_TLS"
#endif

#define PC_ENABLE_TLS_RESUMPTION_NEEDS_TLS PC_ENABLE_TLS
#if PC_ENABLE_TLS_RESUMPTION && !PC_ENABLE_TLS_RESUMPTION_NEEDS_TLS
#error "ProtoCore: PC_ENABLE_TLS_RESUMPTION needs PC_ENABLE_TLS"
#endif

#define PC_ENABLE_METRICS_NEEDS_STATS PC_ENABLE_STATS
#if PC_ENABLE_METRICS && !PC_ENABLE_METRICS_NEEDS_STATS
#error "ProtoCore: PC_ENABLE_METRICS needs PC_ENABLE_STATS"
#endif

#define PC_ENABLE_HTTP_CLIENT_TLS_NEEDS_TLS PC_ENABLE_TLS
#if PC_ENABLE_HTTP_CLIENT_TLS && !PC_ENABLE_HTTP_CLIENT_TLS_NEEDS_TLS
#error "ProtoCore: PC_ENABLE_HTTP_CLIENT_TLS needs PC_ENABLE_TLS"
#endif

#define PC_ENABLE_MQTT_TLS_NEEDS_TLS PC_ENABLE_TLS
#if PC_ENABLE_MQTT_TLS && !PC_ENABLE_MQTT_TLS_NEEDS_TLS
#error "ProtoCore: PC_ENABLE_MQTT_TLS needs PC_ENABLE_TLS"
#endif

#define PC_ENABLE_WS_CLIENT_TLS_NEEDS_TLS PC_ENABLE_TLS
#if PC_ENABLE_WS_CLIENT_TLS && !PC_ENABLE_WS_CLIENT_TLS_NEEDS_TLS
#error "ProtoCore: PC_ENABLE_WS_CLIENT_TLS needs PC_ENABLE_TLS"
#endif

#define PC_ENABLE_SNMP_TRAP_NEEDS_SNMP PC_ENABLE_SNMP
#if PC_ENABLE_SNMP_TRAP && !PC_ENABLE_SNMP_TRAP_NEEDS_SNMP
#error "ProtoCore: PC_ENABLE_SNMP_TRAP needs PC_ENABLE_SNMP"
#endif

#define PC_ENABLE_PMBUS_NEEDS_SMBUS PC_ENABLE_SMBUS
#if PC_ENABLE_PMBUS && !PC_ENABLE_PMBUS_NEEDS_SMBUS
#error "ProtoCore: PC_ENABLE_PMBUS needs PC_ENABLE_SMBUS"
#endif

#define PC_ENABLE_COAP_OBSERVE_NEEDS_COAP PC_ENABLE_COAP
#if PC_ENABLE_COAP_OBSERVE && !PC_ENABLE_COAP_OBSERVE_NEEDS_COAP
#error "ProtoCore: PC_ENABLE_COAP_OBSERVE needs PC_ENABLE_COAP"
#endif

#if PC_ENABLE_TLS_RPK && !(PC_ENABLE_DTLS || PC_ENABLE_HTTP3)
#error "ProtoCore: PC_ENABLE_TLS_RPK requires PC_ENABLE_DTLS or PC_ENABLE_HTTP3"
#endif

#if PC_ENABLE_COAP_BLOCK
#if !PC_ENABLE_COAP
#error "ProtoCore: PC_ENABLE_COAP_BLOCK requires PC_ENABLE_COAP"
#endif
#if PC_COAP_BLOCK_SZX_MAX > 6
#error "ProtoCore: PC_COAP_BLOCK_SZX_MAX must be <= 6 (block size 2^(SZX+4); SZX 7 is reserved)"
#endif
#if PC_COAP_MSG_BUF_SIZE < ((1 << (PC_COAP_BLOCK_SZX_MAX + 4)) + 16)
#error "ProtoCore: PC_COAP_MSG_BUF_SIZE must hold one full block (2^(PC_COAP_BLOCK_SZX_MAX+4)) + 16 header/option bytes"
#endif
#if PC_COAP_BLOCK1_MAX < (1 << (PC_COAP_BLOCK_SZX_MAX + 4))
#error "ProtoCore: PC_COAP_BLOCK1_MAX must be >= one block (2^(PC_COAP_BLOCK_SZX_MAX+4))"
#endif
#endif

// --- feature dependency guards (centralized; see the BUILD-FLAG DEPENDENCY TREE
//     near the top of this file). A child feature requires its parent(s). ---

#define PC_ENABLE_WS_DEFLATE_NEEDS_WEBSOCKET PC_ENABLE_WEBSOCKET
#if PC_ENABLE_WS_DEFLATE && !PC_ENABLE_WS_DEFLATE_NEEDS_WEBSOCKET
#error "ProtoCore: PC_ENABLE_WS_DEFLATE needs PC_ENABLE_WEBSOCKET"
#endif

#define PC_ENABLE_CIA402_NEEDS_CANOPEN PC_ENABLE_CANOPEN
#if PC_ENABLE_CIA402 && !PC_ENABLE_CIA402_NEEDS_CANOPEN
#error "ProtoCore: PC_ENABLE_CIA402 needs PC_ENABLE_CANOPEN"
#endif

#define PC_ENABLE_SSH_ZLIB_NEEDS_SSH PC_ENABLE_SSH
#if PC_ENABLE_SSH_ZLIB && !PC_ENABLE_SSH_ZLIB_NEEDS_SSH
#error "ProtoCore: PC_ENABLE_SSH_ZLIB needs PC_ENABLE_SSH"
#endif

#define PC_ENABLE_SSH_KEYBOARD_INTERACTIVE_NEEDS_SSH PC_ENABLE_SSH
#if PC_ENABLE_SSH_KEYBOARD_INTERACTIVE && !PC_ENABLE_SSH_KEYBOARD_INTERACTIVE_NEEDS_SSH
#error "ProtoCore: PC_ENABLE_SSH_KEYBOARD_INTERACTIVE needs PC_ENABLE_SSH"
#endif
#if PC_ENABLE_SSH_KEYBOARD_INTERACTIVE && !PC_SSH_ALLOW_PASSWORD
#error                                                                                                                 \
    "ProtoCore: PC_ENABLE_SSH_KEYBOARD_INTERACTIVE is password-backed - it verifies the response through the password callback, so PC_SSH_ALLOW_PASSWORD must stay 1 (or drop keyboard-interactive for publickey-only)"
#endif

// SFTP and SCP need the channel layer to carry them and the mount to store into (PC_ENABLE_MNT is
// required above). They do NOT need FILE_SERVING: that dependency was the fs::FS seam, and the seam
// now lives with the vendor code in board_drivers/, behind the mount backend.
#define PC_ENABLE_SSH_SFTP_NEEDS_SSH PC_ENABLE_SSH
#if PC_ENABLE_SSH_SFTP && !PC_ENABLE_SSH_SFTP_NEEDS_SSH
#error "ProtoCore: PC_ENABLE_SSH_SFTP needs PC_ENABLE_SSH"
#endif
#define PC_ENABLE_SSH_SCP_NEEDS_SSH PC_ENABLE_SSH
#if PC_ENABLE_SSH_SCP && !PC_ENABLE_SSH_SCP_NEEDS_SSH
#error "ProtoCore: PC_ENABLE_SSH_SCP needs PC_ENABLE_SSH"
#endif

#if PC_ENABLE_SSH_ZLIB
// Window must be a power of two in [256, 32768] (32 KB is zlib's max; the client's inflate window).
#if (PC_SSH_ZLIB_WINDOW & (PC_SSH_ZLIB_WINDOW - 1)) != 0 || PC_SSH_ZLIB_WINDOW < 256 || PC_SSH_ZLIB_WINDOW > 32768
#error "ProtoCore: PC_SSH_ZLIB_WINDOW must be a power of two in [256, 32768]"
#endif
// Positions index a uint16 hash chain, so window + max-input must stay under 65535.
#if (PC_SSH_ZLIB_WINDOW + PC_SSH_ZLIB_MAX_IN) > 65534
#error "ProtoCore: PC_SSH_ZLIB_WINDOW + PC_SSH_ZLIB_MAX_IN must be <= 65534"
#endif
// The compressor must accept a full packet payload, else a max-size outbound packet would fail to
// compress and drop, desyncing the stateful stream.
#if PC_SSH_ZLIB_MAX_IN < SSH_PKT_BUF_SIZE
#error "ProtoCore: PC_SSH_ZLIB_MAX_IN must be >= SSH_PKT_BUF_SIZE"
#endif
// The per-connection compression pool is ~80 KB: the s2c deflate work buffer + hash chain (~48 KB)
// plus the c2s 32 KB context-takeover inflate window. On ARDUINO pick a path: offload it to PSRAM
// (PC_SSH_ZLIB_IN_PSRAM, a PSRAM board built with the BSS-in-PSRAM core), or acknowledge the
// internal-DRAM cost (PC_SSH_ZLIB_ACK_DRAM, fine for MAX_SSH_CONNS=1 without TLS on a roomy S3 / P4).
// Fail fast with guidance instead of a raw linker overflow.
#if PROTOCORE_HOT && !PC_SSH_ZLIB_IN_PSRAM && !PC_SSH_ZLIB_ACK_DRAM
#error                                                                                                                 \
    "ProtoCore: PC_ENABLE_SSH_ZLIB - the per-connection compression pool is ~80 KB (s2c deflate ~48 KB + the c2s 32 KB inflate window). Set PC_SSH_ZLIB_IN_PSRAM=1 on a PSRAM board (S3 / P4 / WROVER, core built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y, tools/psram/README.md), OR set PC_SSH_ZLIB_ACK_DRAM=1 to accept the internal-DRAM cost (fits MAX_SSH_CONNS=1 without TLS on a roomy chip)."
#endif
#endif

#define PC_ENABLE_WEB_TERMINAL_NEEDS_WEBSOCKET PC_ENABLE_WEBSOCKET
#if PC_ENABLE_WEB_TERMINAL && !PC_ENABLE_WEB_TERMINAL_NEEDS_WEBSOCKET
#error "ProtoCore: PC_ENABLE_WEB_TERMINAL needs PC_ENABLE_WEBSOCKET"
#endif

#define PC_ENABLE_DASHBOARD_NEEDS_SSE PC_ENABLE_SSE
#if PC_ENABLE_DASHBOARD && !PC_ENABLE_DASHBOARD_NEEDS_SSE
#error "ProtoCore: PC_ENABLE_DASHBOARD needs PC_ENABLE_SSE"
#endif

#define PC_ENABLE_SNMP_V3_NEEDS_SNMP PC_ENABLE_SNMP
#if PC_ENABLE_SNMP_V3 && !PC_ENABLE_SNMP_V3_NEEDS_SNMP
#error "ProtoCore: PC_ENABLE_SNMP_V3 needs PC_ENABLE_SNMP"
#endif

#define PC_ENABLE_OPCUA_CLIENT_NEEDS_OPCUA PC_ENABLE_OPCUA
#if PC_ENABLE_OPCUA_CLIENT && !PC_ENABLE_OPCUA_CLIENT_NEEDS_OPCUA
#error "ProtoCore: PC_ENABLE_OPCUA_CLIENT needs PC_ENABLE_OPCUA"
#endif

#define PC_ENABLE_UMATI_NEEDS_OPCUA PC_ENABLE_OPCUA
#if PC_ENABLE_UMATI && !PC_ENABLE_UMATI_NEEDS_OPCUA
#error "ProtoCore: PC_ENABLE_UMATI needs PC_ENABLE_OPCUA"
#endif

#define PC_ENABLE_ROBOTICS_NEEDS_OPCUA PC_ENABLE_OPCUA
#if PC_ENABLE_ROBOTICS && !PC_ENABLE_ROBOTICS_NEEDS_OPCUA
#error "ProtoCore: PC_ENABLE_ROBOTICS needs PC_ENABLE_OPCUA"
#endif

#define PC_ENABLE_EUROMAP77_NEEDS_OPCUA PC_ENABLE_OPCUA
#if PC_ENABLE_EUROMAP77 && !PC_ENABLE_EUROMAP77_NEEDS_OPCUA
#error "ProtoCore: PC_ENABLE_EUROMAP77 needs PC_ENABLE_OPCUA"
#endif

#define PC_ENABLE_CONFIG_IO_NEEDS_CONFIG_STORE PC_ENABLE_CONFIG_STORE
#if PC_ENABLE_CONFIG_IO && !PC_ENABLE_CONFIG_IO_NEEDS_CONFIG_STORE
#error "ProtoCore: PC_ENABLE_CONFIG_IO needs PC_ENABLE_CONFIG_STORE"
#endif

#define PC_ENABLE_HTTP_CLIENT_TLS_NEEDS_HTTP_CLIENT PC_ENABLE_HTTP_CLIENT
#if PC_ENABLE_HTTP_CLIENT_TLS && !PC_ENABLE_HTTP_CLIENT_TLS_NEEDS_HTTP_CLIENT
#error "ProtoCore: PC_ENABLE_HTTP_CLIENT_TLS needs PC_ENABLE_HTTP_CLIENT"
#endif

#define PC_ENABLE_MQTT_TLS_NEEDS_MQTT PC_ENABLE_MQTT
#if PC_ENABLE_MQTT_TLS && !PC_ENABLE_MQTT_TLS_NEEDS_MQTT
#error "ProtoCore: PC_ENABLE_MQTT_TLS needs PC_ENABLE_MQTT"
#endif

#define PC_ENABLE_WS_CLIENT_TLS_NEEDS_WS_CLIENT PC_ENABLE_WS_CLIENT
#if PC_ENABLE_WS_CLIENT_TLS && !PC_ENABLE_WS_CLIENT_TLS_NEEDS_WS_CLIENT
#error "ProtoCore: PC_ENABLE_WS_CLIENT_TLS needs PC_ENABLE_WS_CLIENT"
#endif

#if PC_ENABLE_WEBSOCKET && WS_FRAME_SIZE < 2
#error "ProtoCore: WS_FRAME_SIZE must be >= 2 when PC_ENABLE_WEBSOCKET is set"
#endif

#if PC_ENABLE_SSE && SSE_BUF_SIZE < 8
#error "ProtoCore: SSE_BUF_SIZE must be >= 8 when PC_ENABLE_SSE is set"
#endif

#if PC_ENABLE_MULTIPART && MAX_MULTIPART_PARTS < 1
#error "ProtoCore: MAX_MULTIPART_PARTS must be >= 1 when PC_ENABLE_MULTIPART is set"
#endif

#if RESP_HDR_BUF_SIZE < 128
#error "ProtoCore: RESP_HDR_BUF_SIZE must be >= 128 (status line + headers + CORS block)"
#endif

#if PC_ENABLE_WEBSOCKET && WS_HDR_BUF_SIZE < 128
#error "ProtoCore: WS_HDR_BUF_SIZE must be >= 128 when PC_ENABLE_WEBSOCKET is set"
#endif

#if CORS_HDR_BUF_SIZE < 64
#error "ProtoCore: CORS_HDR_BUF_SIZE must be >= 64"
#endif

#if RESP_HDR_BUF_SIZE < (CORS_HDR_BUF_SIZE + EXTRA_HDR_BUF_SIZE + 96)
#error                                                                                                                 \
    "ProtoCore: RESP_HDR_BUF_SIZE must be >= CORS_HDR_BUF_SIZE + EXTRA_HDR_BUF_SIZE + 96 (status line + CORS block + custom-header block are injected into response headers)"
#endif

// ===========================================================================
// Feature tuning knobs (grouped and gated by feature)
// ===========================================================================
//
// One place to turn every tunable knob - buffer sizes, table depths, limits, thresholds -
// so you never have to open a feature header. Each is an override-able default (set it in
// your build flags to change it); the owning module includes this header and uses the
// value. An optional feature's group is wrapped in that feature's PC_ENABLE_* flag
// (resolved above), so a knob only exists when its feature is built.
//
// What is NOT here: protocol- and algorithm-fixed constants (wire opcodes, magic bytes,
// crypto digest / block sizes, spec-mandated PDU / field widths, the deflate/inflate
// scratch sizes a static_assert pins to the table layout). Those are not knobs - changing
// them breaks conformance - so they stay in their feature file next to the code they bind.

// -- Core: protocol dispatch + shared outbound transport (always built) --
/** @brief Size of the protocol-handler dispatch table; must exceed the largest ConnProto id. */
#ifndef PROTO_MAX_HANDLERS
#define PROTO_MAX_HANDLERS 11
#endif
// proto_register / proto_get index this table by ConnProto id, so it must be wide enough for every id.
static_assert((unsigned)PROTO_MESH < PROTO_MAX_HANDLERS, "PROTO_MAX_HANDLERS must exceed the largest ConnProto id");
/** @brief Reverse-SSH tunnel: max concurrent forwarded-tcpip channels bridged at once. A relay that
 * forwards to a web UI opens one channel per inbound TCP connection, so this bounds concurrency. */
#if PC_ENABLE_SSH_CLIENT
#ifndef PC_SSH_CLIENT_MAX_CHANNELS
#define PC_SSH_CLIENT_MAX_CHANNELS 4
#endif
#if PC_SSH_CLIENT_MAX_CHANNELS < 1
#error "ProtoCore: PC_SSH_CLIENT_MAX_CHANNELS must be >= 1"
#endif
#endif

/** @brief Number of simultaneous outbound client connections (BSS pool size). */
#ifndef PC_CLIENT_CONNS
#if PC_ENABLE_SSH_CLIENT
// The reverse-SSH tunnel holds the relay connection plus one local bridge per forwarded channel, so
// the pool must cover 1 + PC_SSH_CLIENT_MAX_CHANNELS or channels past the pool fail to bridge.
#define PC_CLIENT_CONNS (1 + PC_SSH_CLIENT_MAX_CHANNELS)
#else
#define PC_CLIENT_CONNS 2
#endif
#endif

// The FTP session driver holds a control connection and a data connection at the same time; with a
// smaller pool the data open would fail after login and every transfer would abort mid-session.
#if PC_ENABLE_FTP_SESSION && (PC_CLIENT_CONNS < 2)
#error "ProtoCore: PC_ENABLE_FTP_SESSION needs PC_CLIENT_CONNS >= 2 (control + data)"
#endif

// The reverse-SSH tunnel needs the relay connection plus one local bridge per forwarded channel.
#if PC_ENABLE_SSH_CLIENT && (PC_CLIENT_CONNS < 1 + PC_SSH_CLIENT_MAX_CHANNELS)
#error                                                                                                                 \
    "ProtoCore: PC_ENABLE_SSH_CLIENT needs PC_CLIENT_CONNS >= 1 + PC_SSH_CLIENT_MAX_CHANNELS (relay + one local bridge per channel)"
#endif

/**
 * @brief Per-connection wire receive ring size (bytes).
 *
 * Holds plaintext (plain) or ciphertext (TLS). The transport ACKs on consume
 * (Tcp.client->read reopens the window), so for a large inbound transfer to never
 * stall the ring must hold a full TCP receive window: keep PC_CLIENT_RX_BUF >=
 * TCP_WND (~5.7 KB). The 8192 default clears that and a multi-KB TLS handshake
 * flight; a ring below TCP_WND can deadlock a sustained download (the peer would be
 * allowed to send more than the ring holds). Must exceed one TCP segment (TCP_MSS).
 */
#ifndef PC_CLIENT_RX_BUF
#define PC_CLIENT_RX_BUF 8192
#endif

// -- SSH (network_drivers/presentation/ssh; the codec compiles when the SSH sources are
//    built, so its knobs are always defined) --
/** @brief Initial receive window the SSH server advertises (RFC 4254 §5.1). */
#ifndef SSH_CHAN_WINDOW
#define SSH_CHAN_WINDOW 32768u
#endif
/**
 * @brief Maximum SSH channel data payload the server advertises it can receive per message.
 *
 * This is what a peer may put in one SSH_MSG_CHANNEL_DATA, so it MUST fit one inbound SSH packet: the
 * transport rejects any packet larger than SSH_PKT_BUF_SIZE, so advertising more than that (minus the
 * channel-data + packet framing + MAC + padding) makes a peer that sends a bigger message - e.g. an SFTP
 * WRITE - trip the packet-too-large check and drop the connection. Derived from SSH_PKT_BUF_SIZE so it scales
 * when that buffer is raised (e.g. for higher SFTP throughput). Interactive shells never approach it.
 */
#ifndef SSH_CHAN_MAX_PACKET
#define SSH_CHAN_MAX_PACKET (SSH_PKT_BUF_SIZE - 64u)
#endif
/**
 * @brief Re-key when either packet sequence number reaches this value.
 *
 * RFC 4253 §9 recommends re-keying after ~1 GB or one hour. A packet-count proxy is used
 * here; the default is well below SSH_SEQ_CLOSE_THRESHOLD so a re-key always happens before
 * the sequence number can wrap.
 */
#ifndef SSH_REKEY_PACKET_THRESHOLD
#define SSH_REKEY_PACKET_THRESHOLD 0x40000000u
#endif
/**
 * @brief Elapsed-time re-key trigger in milliseconds (RFC 4253 §9: "after each hour"). Default 1 hour.
 *
 * A server-initiated re-key fires when either this much time or SSH_REKEY_PACKET_THRESHOLD packets have
 * passed since the last KEX, whichever comes first. Set to 0 to disable the time trigger (packet-count
 * only). Measured with the pluggable clock (pc_millis()).
 */
#ifndef SSH_REKEY_TIME_MS
#define SSH_REKEY_TIME_MS 3600000u
#endif
/** @brief Max stored user name (RFC 4252 imposes no limit; we cap for BSS). */
#ifndef SSH_AUTH_USER_MAX
#define SSH_AUTH_USER_MAX 32
#endif
/** @brief Max stored password length. */
#ifndef SSH_AUTH_PASS_MAX
#define SSH_AUTH_PASS_MAX 64
#endif
/**
 * @brief Max stored size of the CLIENT KEXINIT payload (I_C, for the exchange hash).
 *
 * A modern OpenSSH client's KEXINIT (post-quantum KEX names + cert host-key algs + EtM
 * MACs + ext-info-c) runs well past 1 KB, so this must be large enough to hold it - a
 * smaller bound silently rejects real clients at key exchange. The packet layer already
 * caps any single packet at SSH_PKT_BUF_SIZE.
 */
#ifndef SSH_KEXINIT_MAX
#define SSH_KEXINIT_MAX 2048
#endif

#if PC_ENABLE_AUDIT_LOG
// -- Audit log (services/security/audit_log) --
#ifndef PC_AUDIT_LOG_ENTRIES
#define PC_AUDIT_LOG_ENTRIES 32 ///< RAM ring depth (records retained for query/verify).
#endif
#ifndef PC_AUDIT_MSG_LEN
#define PC_AUDIT_MSG_LEN 48 ///< Max message bytes per record (truncated to fit).
#endif
#endif // PC_ENABLE_AUDIT_LOG

#if PC_ENABLE_DEVICENET
// -- DeviceNet (services/fieldbus/devicenet) --
#ifndef PC_DEVICENET_MSG_MAX
#define PC_DEVICENET_MSG_MAX 256 ///< max reassembled fragmented message
#endif
#endif // PC_ENABLE_DEVICENET

#if PC_ENABLE_ESPNOW
// -- ESP-NOW (services/radio/espnow) --
#ifndef PC_ESPNOW_MAX_PEERS
#define PC_ESPNOW_MAX_PEERS 8 ///< Bounded peer registry size.
#endif
#endif // PC_ENABLE_ESPNOW

#if PC_ENABLE_GRAPHQL
// -- GraphQL (services/iot/graphql) --
#ifndef PC_GQL_MAX_NODES
#define PC_GQL_MAX_NODES 48 ///< Max fields across the whole query.
#endif
#ifndef PC_GQL_MAX_ARGS
#define PC_GQL_MAX_ARGS 24 ///< Max arguments across the whole query.
#endif
#ifndef PC_GQL_MAX_DEPTH
#define PC_GQL_MAX_DEPTH 6 ///< Max selection-set nesting depth.
#endif
#ifndef PC_GQL_NAME_MAX
#define PC_GQL_NAME_MAX 32 ///< Max field / argument name length.
#endif
#ifndef PC_GQL_PATH_MAX
#define PC_GQL_PATH_MAX 96 ///< Max dotted path length passed to the resolver.
#endif
#ifndef PC_GQL_STRBUF
#define PC_GQL_STRBUF 256 ///< Pool for decoded string-argument bytes.
#endif
#endif // PC_ENABLE_GRAPHQL

#if PC_NEED_J1939
// -- J1939 (services/j1939; also built when NMEA 2000 is enabled) --
#ifndef PC_J1939_TP_MAX
#define PC_J1939_TP_MAX 256 ///< max reassembled TP message (spec allows up to 1785); sized down for RAM
#endif
#endif // PC_NEED_J1939

#if PC_NEED_NMEA0183
// -- NMEA 0183 (services/timing_position/nmea0183) --
#ifndef PC_NMEA0183_MAX_FIELDS
#define PC_NMEA0183_MAX_FIELDS 26 ///< max comma-separated fields (incl. the address field)
#endif
#endif // PC_NEED_NMEA0183

#if PC_ENABLE_UBX
// -- UBX (services/timing_position/ubx) --
#ifndef PC_UBX_MAX_PAYLOAD
#define PC_UBX_MAX_PAYLOAD 256 ///< max UBX payload the stream demux buffers (NAV-PVT is 92; longer frames are skipped)
#endif
#endif // PC_ENABLE_UBX

#if PC_ENABLE_NMEA2000
// -- NMEA 2000 (services/timing_position/nmea2000) --
#ifndef PC_N2K_FP_MAX
#define PC_N2K_FP_MAX 223 ///< Fast Packet max payload (6 in frame 0 + 31 x 7)
#endif
#endif // PC_ENABLE_NMEA2000

#if PC_ENABLE_OAUTH2
// -- OAuth2 (services/security/oauth2) --
#ifndef PC_OAUTH2_TOKEN_LEN
#define PC_OAUTH2_TOKEN_LEN 768 ///< access_token / id_token buffer (JWTs are large).
#endif
#ifndef PC_OAUTH2_RT_LEN
#define PC_OAUTH2_RT_LEN 256 ///< refresh_token buffer.
#endif
#ifndef PC_OAUTH2_BODY_BUF
#define PC_OAUTH2_BODY_BUF 1024 ///< token-request body buffer.
#endif
#ifndef PC_OAUTH2_RESP_BUF
#define PC_OAUTH2_RESP_BUF 2048 ///< token-endpoint response buffer.
#endif
#endif // PC_ENABLE_OAUTH2

#if PC_ENABLE_OIDC
// -- OIDC (services/security/oidc) --
// PC_OIDC_MAX_LEN is declared unconditionally with PC_ENABLE_OIDC above, because PC_AUTH_HDR_CAP
// sizes the Authorization buffer from it and that runs before this block.
#ifndef PC_OIDC_SUB_LEN
#define PC_OIDC_SUB_LEN 64 ///< Captured `sub` claim buffer.
#endif
#ifndef PC_OIDC_EMAIL_LEN
#define PC_OIDC_EMAIL_LEN 96 ///< Captured `email` claim buffer.
#endif
#ifndef PC_OIDC_KID_LEN
#define PC_OIDC_KID_LEN 80 ///< Max `kid` length.
#endif
#ifndef PC_OIDC_JWKS_MAX
#define PC_OIDC_JWKS_MAX 16384 ///< Max JWKS document scanned; exceeds any real multi-key set, bounds the parse.
#endif
#endif // PC_ENABLE_OIDC

#if PC_ENABLE_OPCUA
// -- OPC UA (services/fieldbus/opcua) --
#ifndef PC_OPCUA_BUF
#define PC_OPCUA_BUF 8192 ///< Server's advertised buffer / max-message size for the handshake.
#endif
#ifndef PC_OPCUA_READ_MAX
#define PC_OPCUA_READ_MAX 8 ///< max NodesToRead handled per ReadRequest.
#endif
#ifndef PC_OPCUA_BROWSE_MAX
#define PC_OPCUA_BROWSE_MAX 4 ///< max NodesToBrowse handled per BrowseRequest.
#endif
#ifndef PC_OPCUA_REF_MAX
#define PC_OPCUA_REF_MAX 8 ///< max references returned per browsed node.
#endif
#ifndef PC_OPCUA_WRITE_MAX
#define PC_OPCUA_WRITE_MAX 8 ///< max NodesToWrite handled per WriteRequest.
#endif

// An Axes Browse returns one reference per axis, so the axis count must fit a single Browse response.
#if PC_ENABLE_ROBOTICS && (PC_ROBOTICS_AXES > PC_OPCUA_REF_MAX)
#error "ProtoCore: PC_ROBOTICS_AXES must be <= PC_OPCUA_REF_MAX (an Axes Browse returns one reference per axis)"
#endif
// Advertised server identity (endpoint descriptions), overridable per deployment; the app may
// also set these at runtime via pc_opcua_set_endpoint_url() / the OpcUaServerInfo it passes.
#ifndef PC_OPCUA_DEFAULT_ENDPOINT
#define PC_OPCUA_DEFAULT_ENDPOINT "opc.tcp://localhost:4840" ///< default endpoint URL.
#endif
#ifndef PC_OPCUA_DEFAULT_APP_URI
#define PC_OPCUA_DEFAULT_APP_URI "urn:det:opcua:server" ///< default ApplicationUri.
#endif
#ifndef PC_OPCUA_DEFAULT_APP_NAME
#define PC_OPCUA_DEFAULT_APP_NAME "pc_opcua_server" ///< default ApplicationName.
#endif
#endif // PC_ENABLE_OPCUA

#if PC_ENABLE_PROVISIONING
// -- Wi-Fi provisioning credential store (services/system/provisioning_service) --
// The NVS namespace and its keys, overridable per deployment (e.g. to avoid an NVS-namespace
// collision with the application's own store).
#ifndef PC_PROV_NVS_NAMESPACE
#define PC_PROV_NVS_NAMESPACE "wifi_prov" ///< NVS namespace holding the saved credentials.
#endif
#ifndef PC_PROV_KEY_SSID
#define PC_PROV_KEY_SSID "ssid" ///< NVS key + HTML form field for the SSID.
#endif
#ifndef PC_PROV_KEY_PSK
#define PC_PROV_KEY_PSK "psk" ///< NVS key + HTML form field for the pre-shared key.
#endif
#endif // PC_ENABLE_PROVISIONING

#if PC_ENABLE_MNT
// -- Mounted storage (services/storage/mnt) --
#ifndef PC_MNT_RAM_FILES
#define PC_MNT_RAM_FILES 4 ///< RAM backend: number of files (a directory occupies one).
#endif
#ifndef PC_MNT_RAM_FILE_SIZE
#define PC_MNT_RAM_FILE_SIZE 1024 ///< RAM backend: max bytes per file.
#endif
#ifndef PC_MNT_MAX_OPEN
#define PC_MNT_MAX_OPEN 4 ///< Concurrent open handles, files and directory cursors together.
#endif
#ifndef PC_MNT_NAME_MAX
#define PC_MNT_NAME_MAX 48 ///< Max path length (RAM backend). Not a bound on any caller's buffer.
#endif
#endif // PC_ENABLE_MNT

// Final sizing pass: raise buffers to the floors the enabled features require (every PC_ENABLE_*
// flag is resolved by this point). Kept in the board-profile layer, not inline above.
#include "board_drivers/board_profiles/derived_sizing.h" // PC_ALLOW_LATE_INCLUDE: ordered - derives sizes from the PC_ENABLE_* flags resolved above

#endif
