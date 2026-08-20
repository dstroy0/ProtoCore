// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file buffer_sizing.h
 * @brief Every capacity, buffer extent and pool borrow the enabled features are sized on.
 *
 * Reached from protocore_config.h, which is the single entry point and states the feature flags
 * every block here is gated on. Including this file on its own would read those flags before they
 * are settled.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BUFFER_SIZING_H
#define PROTOCORE_BUFFER_SIZING_H

#ifndef PROTOCORE_CONFIG_H
#error "include protocore_config.h instead of this file - it is the entry point that states the feature flags"
#endif

// ---------------------------------------------------------------------------
// Compile-time capacity constants (affect static array sizes)
// ---------------------------------------------------------------------------

/**
 * @brief Maximum simultaneous TCP connections (fixed static pool; ~3.95 KB of internal RAM per slot).
 *
 * Default 8: a keep-alive/concurrency server needs headroom above its peak concurrent client count,
 * because a connection closed by the keep-alive fairness cap (PROTOCORE_KEEPALIVE_MAX_REQUESTS) briefly
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
#ifndef PROTOCORE_TCP_NODELAY
#define PROTOCORE_TCP_NODELAY 1
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
#ifndef PROTOCORE_BASE64_SWAR
#define PROTOCORE_BASE64_SWAR 1
#endif

/** @brief Ring-buffer capacity in bytes per connection slot (feature floors enforced last, in
 *  derived_sizing.h - a value below what an enabled feature needs is raised there). */
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
 * whose request headers are not complete within PROTOCORE_REQUEST_TIMEOUT_MS is answered 408 and closed, freeing the
 * slot (the nginx client_header_timeout semantic). It is scoped to the header phase, so it never reaps a
 * legitimate slow body: a large streaming upload (PARSE_BODY) is governed by the streaming handler + idle
 * timer, not this deadline. It also does not touch WebSocket / SSE slots (long-lived by design). Lower it to
 * tighten the window on a trusted LAN; 0 turns the defense off.
 */
#ifndef PROTOCORE_REQUEST_TIMEOUT_MS
#define PROTOCORE_REQUEST_TIMEOUT_MS 10000
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
#ifndef PROTOCORE_CLOSING_TIMEOUT_MS
#define PROTOCORE_CLOSING_TIMEOUT_MS 2000
#endif

// ---------------------------------------------------------------------------
// Worker model (server task concurrency)
// ---------------------------------------------------------------------------
//
// The server pipeline (drain events -> dispatch -> send) runs in one or more
// dedicated worker tasks instead of the user's loop(). Each worker owns a
// disjoint partition of conn_pool slots (slot i -> worker i % PROTOCORE_WORKER_COUNT)
// and its own pool slot, so no two workers ever touch the same slot:
// shared-nothing, no hot-path locks, latency stays bounded (determinism
// preserved) while cores run disjoint connections in parallel.
//
// PROTOCORE_WORKER_COUNT == 1 (default) is byte-for-byte the single-pipeline model:
// one worker owns every slot, the existing single event queue. N > 1 is opt-in.
// Each pool costs its arena size once per slot, and every worker gets a slot.

/** @brief Number of server worker tasks (slots partitioned i % N). Default 1. */
#ifndef PROTOCORE_WORKER_COUNT
#define PROTOCORE_WORKER_COUNT 1
#endif

// The library's own worker slot, one past the server workers. Each independent task borrows from
// its own slot, which is what makes a borrow lock-free; a library task prefers the ghost and falls
// back to the rest. How many slots that implies is each pool's own business - see
// PROTOCORE_REG_POOL_SLOTS and PROTOCORE_SEC_POOL_SLOTS in mmgr.
#define PROTOCORE_GHOST_WORKER_SLOT (PROTOCORE_WORKER_COUNT)

/**
 * @brief Stack (bytes) for each server worker task (ESP32).
 *
 * Floor note: two heavy computations run on the worker.
 *   - RSA-2048 verification (OIDC / SSH host key / JWKS via the mbedTLS bignum
 *     modexp) uses ~7 KB (measured on a DevKitV1).
 *   - SSH modern crypto (curve25519-sha256 KEX + ssh-ed25519, software field
 *     arithmetic in radix-2^16) peaks at ~10.5 KB (measured on an ESP32-S3): the
 *     deep protocore_gf call chain plus the on-accelerator field inversion nests deeper
 *     than the RSA path.
 * So the default adapts: 12 KB when SSH is enabled (curve/ed25519 can be
 * negotiated), 8 KB otherwise. Do NOT lower it below the matching floor
 * (::PROTOCORE_WORKER_STACK_CURVE_MIN for SSH, ::PROTOCORE_WORKER_STACK_RSA_MIN for
 * OIDC) or the first handshake overflows the task stack - a build-time guard
 * (bottom of this file) enforces the floor so a lowered stack is caught at
 * compile time.
 */
// True when any feature that runs the SSH-class handshake is built. Derived, not configurable, and
// defined unconditionally: a predicate that exists only inside one #ifndef is a trap for the next
// person who tests it further down, where it would silently read 0.
#if (defined(PROTOCORE_ENABLE_SSH) && PROTOCORE_ENABLE_SSH) ||                                                         \
    (defined(PROTOCORE_ENABLE_SSH_CLIENT) && PROTOCORE_ENABLE_SSH_CLIENT) ||                                           \
    (defined(PROTOCORE_ENABLE_HTTP3) && PROTOCORE_ENABLE_HTTP3)
#define PROTOCORE_SSH_ANY 1
#else
#define PROTOCORE_SSH_ANY 0
#endif

#ifndef PROTOCORE_WORKER_TASK_STACK
// SSH (curve25519 + ssh-ed25519, server OR the reverse-SSH client) and HTTP/3 (the QUIC TLS-1.3
// handshake reuses the same protocore_ed25519 signer for CertificateVerify) all peak at ~10.5 KB on the
// worker task; the PQ/T hybrid (PROTOCORE_ENABLE_PQC_KEX) runs ML-KEM-768 on top, ~7 KB more. The default
// tracks the matching floor so a hybrid build is provisioned, not starved; the guard at the bottom of
// this file is the backstop when the stack is set by hand. (A flag set only in the config block below,
// not via -D, is still undefined here and reads as 0 - the guard then catches any shortfall.)
// sntrup761x25519-sha512 (on by default with the PQC hybrid, but a standalone -D can enable it without
// ML-KEM) is the heavy case: the reverse-SSH CLIENT runs KeyGen+Decaps whose FO re-encrypt peaks ~32 KB,
// the SERVER runs Encaps only (~22 KB). ML-KEM alone stays at 16 KB, so a build that explicitly drops
// sntrup761 keeps the lighter floor. This default block only sees -D flags (the config defaults below
// have not run yet), so sntrup761's default-tracks-PQC is assumed here and the guard at the bottom is
// the backstop when it is toggled in the config block instead of via -D.
#if (PROTOCORE_ENABLE_PQC_KEX || (defined(PROTOCORE_ENABLE_SSH_SNTRUP761) && PROTOCORE_ENABLE_SSH_SNTRUP761)) &&       \
    PROTOCORE_SSH_ANY
#if defined(PROTOCORE_ENABLE_SSH_SNTRUP761) && !PROTOCORE_ENABLE_SSH_SNTRUP761
#define PROTOCORE_WORKER_TASK_STACK 16384
#elif defined(PROTOCORE_ENABLE_SSH_CLIENT) && PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_WORKER_TASK_STACK 40960
#else
#define PROTOCORE_WORKER_TASK_STACK 32768
#endif
#elif PROTOCORE_SSH_ANY
#define PROTOCORE_WORKER_TASK_STACK 12288
#else
#define PROTOCORE_WORKER_TASK_STACK 8192
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
#ifndef PROTOCORE_WORKER_STACK_RSA_MIN
#define PROTOCORE_WORKER_STACK_RSA_MIN 8192
#endif

/**
 * @brief Minimum worker-task stack (bytes) required once SSH is compiled in.
 *
 * SSH can negotiate curve25519-sha256 + ssh-ed25519, whose software field
 * arithmetic peaks at ~10.5 KB of worker stack; 12 KB leaves ~1.8 KB of margin
 * for the rest of the handshake call chain (comparable to the RSA floor's
 * margin). Raise both this and ::PROTOCORE_WORKER_TASK_STACK together if you extend
 * the handshake, or force RSA/DH only (ssh_kex_set_prefer_rsa) on a very tight
 * build - but the server still advertises the modern suite, so a modern-only
 * client would still exercise it.
 */
#ifndef PROTOCORE_WORKER_STACK_CURVE_MIN
#define PROTOCORE_WORKER_STACK_CURVE_MIN 12288
#endif

/** @brief FreeRTOS priority for each server worker task (ESP32). */
#ifndef PROTOCORE_WORKER_TASK_PRIORITY
#define PROTOCORE_WORKER_TASK_PRIORITY 5
#endif

/**
 * @brief Core that worker 0 pins to (ESP32). Worker k pins to (PROTOCORE_WORKER_CORE
 * + k) % portNUM_PROCESSORS. Default 1 (APP_CPU), keeping Core 0 lean for the
 * WiFi/lwIP stack and offloading the user's loop().
 */
#ifndef PROTOCORE_WORKER_CORE
#define PROTOCORE_WORKER_CORE 1
#endif

/**
 * @brief Depth of each worker's deferred-callback queue.
 *
 * App code on loop() or another task submits work to a slot's owning worker via
 * Session.workers->defer() / protocore_defer_slot(); the worker runs it in its own single-thread
 * context, so an async push (ws_send / protocore_sse_send from a timer) is race-free. Each
 * worker has one queue of this depth (entries are a {fn, arg} pair, ~8 bytes).
 */
#ifndef PROTOCORE_DEFER_QUEUE_DEPTH
#define PROTOCORE_DEFER_QUEUE_DEPTH 8
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
#ifndef PROTOCORE_WORKER_POLL_TICKS
#define PROTOCORE_WORKER_POLL_TICKS 1
#endif

// ---------------------------------------------------------------------------
// Preempting work queue (PROTOCORE_ENABLE_PREEMPT_QUEUE) - v5 real-time ingest
// ---------------------------------------------------------------------------
//
// Fixed-capacity queues, each feeding one core-pinned processing task: a producer
// posts a fixed-size item (from a task or an ISR) and the scheduler preempts straight
// to the task. There are named lanes - one USER lane exposed to the app, and internal
// DMA / forwarding / device-access lanes that run at a higher priority so internal
// ingest always preempts user work. Queue storage is static (zero heap), so depth +
// item size are compile-time; a task's stack is created only when its lane starts.
// The no-lane protocore_pq_* API drives the USER lane. See preempt_queue.h.

/** @brief Capacity of the preempting queue in items (static-allocated). */
#ifndef PROTOCORE_PQ_DEPTH
#define PROTOCORE_PQ_DEPTH 16
#endif

/** @brief Bytes per preempting-queue item (the posted item must fit). */
#ifndef PROTOCORE_PQ_ITEM_SIZE
#define PROTOCORE_PQ_ITEM_SIZE 32
#endif

/** @brief Stack (bytes) for each preempting-queue processing task (ESP32). */
#ifndef PROTOCORE_PQ_STACK
#define PROTOCORE_PQ_STACK 4096
#endif

/**
 * @brief Base FreeRTOS priority for the internal preempting lanes (DMA / forwarding /
 *        device access). They run at this and just above, so internal ingest preempts
 *        the user lane; keep it above the user lane's priority and below the lwIP tcpip
 *        (18) / WiFi tasks so networking is never starved. See preempt_queue.h.
 */
#ifndef PROTOCORE_PQ_INTERNAL_PRIORITY
#define PROTOCORE_PQ_INTERNAL_PRIORITY 8
#endif

// ---------------------------------------------------------------------------
// DMA peripheral ingest / egress (PROTOCORE_ENABLE_DMA) - v5 hardware ingest
// ---------------------------------------------------------------------------
//
// Move peripheral bytes (UART / I2C / SPI) between the wire and a static buffer
// with the CPU free during the transfer; a DMA-complete event carries the bytes
// to a user callback, which typically posts a descriptor into the preempting work
// queue (PROTOCORE_ENABLE_PREEMPT_QUEUE) so the heavy processing runs off the ISR. RX
// is double-buffered (ping-pong): the completed buffer is handed up while the DMA
// engine fills the other. Storage is static (zero heap) - channel count and buffer
// size are compile-time. See mmgr/dma.h.
//
// PROTOCORE_DMA_SIMULATE routes the transfers through an in-memory ingress/egress
// simulator (feed bytes in, capture bytes out, optional TX->RX loopback) so the
// whole pipeline is exercised with no physical loopback wire - on the host test
// bench and, with the flag set, on the device itself. It is the shipped, tested
// engine; a real silicon backend plugs into protocore_dma_hw_* when PROTOCORE_DMA_SIMULATE=0.

/** @brief Number of DMA channels (static-allocated; each is one peripheral link). */
#ifndef PROTOCORE_DMA_CHANNELS
#define PROTOCORE_DMA_CHANNELS 2
#endif

/** @brief Bytes per DMA transfer buffer (RX is double-buffered at this size). */
#ifndef PROTOCORE_DMA_BUF_SIZE
#define PROTOCORE_DMA_BUF_SIZE 256
#endif

/**
 * @brief HttpRoute DMA transfers through the ingress/egress simulator (default on).
 *        Set to 0 to drive real silicon via the protocore_dma_hw_* backend hooks.
 */

// ---------------------------------------------------------------------------
// Trace capture: pre/post-trigger window assembler (PROTOCORE_ENABLE_TRACE_CAPTURE)
// ---------------------------------------------------------------------------
//
// Sits downstream of PROTOCORE_ENABLE_DMA (or any other sample source) on a high-rate
// acquisition front end: protocore_tc_feed() is called with every batch of arriving samples
// and a continuously-running pre-trigger ring always holds the most recent samples;
// protocore_tc_trigger() freezes that ring as the pre-trigger half of a window and the next
// arriving samples fill the post-trigger half, so the emitted window straddles the
// trigger instant like a benchtop oscilloscope's pretrigger/posttrigger split. One
// capture in flight at a time, fail-closed. Storage is static (zero heap) - the sum of
// the configured pretrigger + posttrigger sample counts must fit PROTOCORE_TC_MAX_WINDOW_SAMPLES.
// See server/signaling/trace_capture.h.

/** @brief Max samples a window may hold (pretrigger_samples + posttrigger_samples), static-allocated. */
#ifndef PROTOCORE_TC_MAX_WINDOW_SAMPLES
#define PROTOCORE_TC_MAX_WINDOW_SAMPLES 4096
#endif

// ---------------------------------------------------------------------------
// AD9238 SPI configuration-port codec (PROTOCORE_ENABLE_AD9238)
// ---------------------------------------------------------------------------
//
// A pure codec for the AD9238 dual ADC's low-speed SPI CONFIGURATION port (power-down,
// output format, output test patterns, offset trim) - NOT its parallel sample-data bus,
// which is out of an MCU's reach at this part's sample rates. See server/peripherals/ad9238/ad9238.h
// for the hardware-verification caveat: the per-register bit fields are transcribed from
// the datasheet, not yet confirmed against physical silicon.

// ---------------------------------------------------------------------------
// Interface forwarding plane (PROTOCORE_ENABLE_FORWARD) - v5 hardware ingest
// ---------------------------------------------------------------------------
//
// A forwarding plane over the ingest pipeline: register interfaces (Wi-Fi STA / AP,
// Ethernet, a peripheral bus, a radio), each with an egress send callback, then add
// per-pair allow / deny rules with an optional rate cap. A frame arriving on one
// interface (set Forward.src_if and Forward.frame.data / .len, then call Forward.ingress,
// typically from a DMA-complete event posted onto the FORWARD lane) is forwarded to every
// allowed destination, so the device bridges / routes between its interfaces instead of only
// terminating traffic. Forward.n reports how many next hops the frame reached. Default-deny and
// fail-closed (a full destination or an exceeded rate cap drops, never blocks). Static tables
// (zero heap). See network_drivers/network/forward/forward.h.

/** @brief Interfaces layer 1 can carry: wifi station and softAP, ethernet, a bridged bus, a radio.
 *  The registry is L1's because an interface is a physical thing; the forwarding plane reads it. */
#ifndef PROTOCORE_PHY_MAX_IFACES
#define PROTOCORE_PHY_MAX_IFACES 4
#endif

/** @brief Max forwarding rules (src -> dst allow/deny + rate cap; static-allocated). */
#ifndef PROTOCORE_FWD_MAX_RULES
#define PROTOCORE_FWD_MAX_RULES 8
#endif

/** @brief Max ingress access-control entries (byte-pattern permit/deny; static). */
#ifndef PROTOCORE_FWD_MAX_ACL
#define PROTOCORE_FWD_MAX_ACL 8
#endif

/** @brief Bytes an ACL entry can match (its pattern / mask length). */
#ifndef PROTOCORE_FWD_ACL_PATLEN
#define PROTOCORE_FWD_ACL_PATLEN 4
#endif

/** @brief Max policy routes (byte-pattern -> egress interface; static). Policy routes take
 *         precedence over the src->dst rules, so tagged traffic leaves a chosen interface. */
#ifndef PROTOCORE_FWD_MAX_ROUTES
#define PROTOCORE_FWD_MAX_ROUTES 8
#endif

/** @brief Build-time toggle for the forwarding-path inspection hook (default off, for cost +
 *         privacy). When 1, Forward.inspect.fn + Forward.set_inspector install a runtime callback
 *         that observes / filters each ingress frame before it is forwarded; when 0 the hook is
 *         compiled out entirely (no call site). Runtime toggle: register or clear (null) the
 *         inspector. */
#ifndef PROTOCORE_FWD_INSPECT
#define PROTOCORE_FWD_INSPECT 0
#endif

// ---------------------------------------------------------------------------
// Radio / wireless gateway (PROTOCORE_ENABLE_GATEWAY) - v5 southbound-to-northbound bridge
// ---------------------------------------------------------------------------
//
// The generic gateway pattern: a southbound radio (LoRa / nRF24 / Zigbee / ... reached
// over SPI / I2C / UART) is a "port"; a frame it receives (data-ready ISR -> DMA -> the
// FORWARD lane -> a per-radio codec) is handed to protocore_gateway_uplink(), which envelopes it with
// its source node address / port / RSSI and publishes it northbound through the uplink
// callback (wire it to MQTT / HTTP / WebSocket / UDP). A northbound command goes the other
// way through protocore_gateway_downlink() to the port's transmit callback. The radio TX + the
// northbound publish are callbacks (the seam a real radio driver / protocol binding plugs
// into), so the bridge is host- and device-testable with no radio. Static tables (zero
// heap). See server/net/gateway/gateway.h.

/** @brief Max southbound gateway ports (radios / buses; static-allocated). */
#ifndef PROTOCORE_GW_MAX_PORTS
#define PROTOCORE_GW_MAX_PORTS 4
#endif

/** @brief Default northbound topic prefix (overridable at runtime via protocore_gateway_set_topic_prefix). */
#ifndef PROTOCORE_GW_DEFAULT_PREFIX
#define PROTOCORE_GW_DEFAULT_PREFIX "gw"
#endif

// ---------------------------------------------------------------------------
// LoRa radio (PROTOCORE_ENABLE_LORA) - Semtech SX127x / RFM95-96 codec + driver
// ---------------------------------------------------------------------------
//
// A per-radio codec + driver that plugs into the gateway (PROTOCORE_ENABLE_GATEWAY): the
// RadioHead-compatible 4-byte frame header (to / from / id / flags) codec, and an SX127x
// register driver over a caller-supplied register-access bus (so the SPI + chip-select
// wiring is the integration's, and the register protocol is host-testable with a mock
// bus). Bridge received frames northbound with protocore_gateway_uplink(); the actual RF link needs
// the module to verify. See services/radio/lora/lora.h.

/** @brief Max LoRa payload bytes (SX127x FIFO is 256; RadioHead uses 251 + 4 header). */
#ifndef PROTOCORE_LORA_MAX_PAYLOAD
#define PROTOCORE_LORA_MAX_PAYLOAD 251
#endif

// ---------------------------------------------------------------------------
// nRF24 radio (PROTOCORE_ENABLE_NRF24) - Nordic nRF24L01+ 2.4 GHz driver
// ---------------------------------------------------------------------------
//
// A radio driver that plugs into the gateway (PROTOCORE_ENABLE_GATEWAY). The nRF24L01+ speaks
// an SPI command protocol (not plain register r/w) and needs a separate CE pin, so the
// driver runs over a caller-supplied SPI transfer + CE bus (nrf_bus). Its hardware pipe
// addressing means the "source address" of a received frame is the pipe number - no
// in-payload header, so there is no separate codec. Bridge received payloads northbound
// with protocore_gateway_uplink(port, pipe, ...); the RF link needs the module to verify.
// See services/radio/nrf24/nrf24.h.

/** @brief nRF24 fixed payload width in bytes (1..32; the chip's static payload size). */
#ifndef PROTOCORE_NRF24_PAYLOAD
#define PROTOCORE_NRF24_PAYLOAD 32
#endif

// ---------------------------------------------------------------------------
// EnOcean ESP3 (PROTOCORE_ENABLE_ENOCEAN) - energy-harvesting 868 MHz serial codec
// ---------------------------------------------------------------------------
//
// A UART telegram codec for EnOcean's ESP3 (EnOcean Serial Protocol 3), the framing used
// by USB/serial EnOcean gateways (TCM 310 / USB 300): sync 0x55, a 4-byte header (data
// length, optional length, packet type) protected by CRC8, then data + optional data
// protected by a second CRC8. protocore_esp3_parse() frames one telegram out of a byte stream and
// verifies both CRCs; protocore_esp3_build() assembles one. Pure (no UART code - you feed it the
// serial bytes), so it is fully host-testable. See services/radio/enocean/enocean.h.

/** @brief Reject an ESP3 telegram whose declared data length exceeds this (framing sanity). */
#ifndef PROTOCORE_ENOCEAN_MAX_DATA
#define PROTOCORE_ENOCEAN_MAX_DATA 512
#endif

// ---------------------------------------------------------------------------
// PN532 NFC (PROTOCORE_ENABLE_PN532) - NXP PN532 NFC/RFID controller frame codec
// ---------------------------------------------------------------------------
//
// The NXP PN532 (I2C / SPI / HSU) command-frame protocol - a tag read/write bridged to an
// HTTP / MQTT event. The chip is driven by "normal information frames" (00 00 FF | LEN |
// LCS | TFI | PData | DCS | 00) with a length checksum and a data checksum, plus a 6-byte
// ACK frame. protocore_pn532_build_frame() / protocore_pn532_parse_frame() assemble and verify those frames
// (the per-command PData is the application's), and protocore_pn532_is_ack() detects the ACK. Pure -
// you carry the frame bytes over your I2C / SPI / UART - so it is fully host-testable.
// See server/peripherals/pn532/pn532.h.

/** @brief Reject a PN532 normal frame whose declared length exceeds this (framing sanity). */
#ifndef PROTOCORE_PN532_MAX_DATA
#define PROTOCORE_PN532_MAX_DATA 254
#endif

// ---------------------------------------------------------------------------
// Sigfox (PROTOCORE_ENABLE_SIGFOX) - Wisol / Murata Sigfox modem AT-command codec
// ---------------------------------------------------------------------------
//
// Tiny low-power uplinks over the Sigfox 0G network. A Wisol / Murata Sigfox modem is
// driven by AT commands over UART: protocore_sigfox_build_uplink() formats an `AT$SF=<hex>` frame
// for a <= 12-byte payload, and protocore_sigfox_parse_response() classifies the modem's reply
// (OK / ERROR / still pending). Pure text-command codec - you carry it over your UART - so
// it is fully host-testable. See services/radio/sigfox/sigfox.h.

/** @brief Maximum Sigfox uplink payload (the network caps a message at 12 bytes). */
#ifndef PROTOCORE_SIGFOX_MAX_PAYLOAD
#define PROTOCORE_SIGFOX_MAX_PAYLOAD 12
#endif

// ---------------------------------------------------------------------------
// Z-Wave (PROTOCORE_ENABLE_ZWAVE) - Silicon Labs Z-Wave Serial API frame codec
// ---------------------------------------------------------------------------
//
// The host-side Serial API of a Silicon Labs 500 / 700-series Z-Wave controller over UART:
// a Z-Wave mesh bridged to the web. Data frames are SOF (0x01) | LEN | Type | Command |
// Data | Checksum, where the checksum is 0xFF XOR-folded over LEN..last-data; single-byte
// ACK (0x06) / NAK (0x15) / CAN (0x18) frames flow-control them. protocore_zwave_build_frame() /
// protocore_zwave_parse_frame() assemble and verify a data frame; the per-command payload is the
// application's. Pure - you carry the bytes over your UART - so it is fully host-testable.
// See services/radio/zwave/zwave.h.

/** @brief Reject a Z-Wave frame whose declared length exceeds this data cap (sanity). */
#ifndef PROTOCORE_ZWAVE_MAX_DATA
#define PROTOCORE_ZWAVE_MAX_DATA 64
#endif

// ---------------------------------------------------------------------------
// Zigbee (PROTOCORE_ENABLE_ZIGBEE) - Silicon Labs EZSP / ASH serial framing codec
// ---------------------------------------------------------------------------
//
// The ASH (Asynchronous Serial Host) data-link layer that carries EZSP frames to a Silicon
// Labs EmberZNet NCP over UART - a Zigbee network bridged to the web. ASH delimits frames
// with a Flag byte (0x7E), byte-stuffs the reserved control bytes, and protects each frame
// with a CRC-16/CCITT. protocore_ash_frame_encode() wraps a control byte + payload into a stuffed,
// CRC'd frame; protocore_ash_frame_decode() unstuffs + verifies one. The EZSP command payload the
// frame carries (version, stack status, an incoming APS message, ...) is the application's.
// protocore_ash_frame_decode() removes the stuffing and verifies the CRC. Pure - you carry the bytes
// over your UART - so it is fully host-testable. See services/radio/zigbee/zigbee.h.

/** @brief Max ASH payload bytes (an EZSP frame; the ASH data field caps near 128). */
#ifndef PROTOCORE_ZIGBEE_MAX_DATA
#define PROTOCORE_ZIGBEE_MAX_DATA 128
#endif

// ---------------------------------------------------------------------------
// Thread (PROTOCORE_ENABLE_THREAD) - OpenThread spinel over HDLC-lite framing codec
// ---------------------------------------------------------------------------
//
// The HDLC-lite framing that carries spinel frames to an OpenThread radio co-processor
// (RCP: an nRF52840 / EFR32) over UART - an 802.15.4 / Thread mesh bridged to IP / the web.
// Each spinel frame is wrapped by HDLC-lite: an FCS (CRC-16/X-25) is appended, the reserved
// bytes are byte-stuffed, and a Flag byte (0x7E) terminates it. protocore_spinel_frame_encode() /
// protocore_spinel_frame_decode() do the framing + FCS; the spinel command inside (a property
// get/set/insert, a stream frame) is the application's. Pure - you carry the bytes over your
// UART - so it is fully host-testable. See services/radio/thread/thread.h.

/** @brief Max spinel payload bytes carried in one HDLC-lite frame. */
#ifndef PROTOCORE_THREAD_MAX_DATA
#define PROTOCORE_THREAD_MAX_DATA 256
#endif

// ---------------------------------------------------------------------------
// Wired Ethernet PHY (PROTOCORE_ENABLE_ETHERNET) - run the server over an RMII PHY
// ---------------------------------------------------------------------------
//
// Bring up a wired Ethernet link (an RMII PHY: LAN8720 / TLK110 / RTL8201 / DP83848) so the
// server runs over Ethernet instead of (or alongside) Wi-Fi. Physical.eth_init is a thin
// wrapper over the Arduino ETH library; the PHY pins / type / clock come from the standard
// ETH_PHY_* build flags for your board (see example Ethernet). The egress reporting
// (Physical.egress -> Physical.if_kind == PROTOCORE_IF_ETH) and the per-route interface classifier
// already handle a wired route, so once the link has an IP the server accepts on it with no other
// change. Default off (zero cost / the ETH library is not linked). ESP32-only.

// W5500 SPI Ethernet (arduino-esp32 3.x only). Set PROTOCORE_ETH_W5500=1 to select the SPI PHY over the RMII
// default; the pins below are the ESP32-S3-DevKitC wiring (HSPI / SPI3). The 2.x ETH library has no W5500,
// so Physical.eth_init falls back to the RMII ETH.begin() when the core is older.
#ifndef PROTOCORE_ETH_W5500
#define PROTOCORE_ETH_W5500 0
#endif
#ifndef PROTOCORE_ETH_W5500_CS
#define PROTOCORE_ETH_W5500_CS 7 ///< chip select
#endif
#ifndef PROTOCORE_ETH_W5500_RST
#define PROTOCORE_ETH_W5500_RST 6 ///< reset
#endif
#ifndef PROTOCORE_ETH_W5500_INT
#define PROTOCORE_ETH_W5500_INT 5 ///< interrupt
#endif
#ifndef PROTOCORE_ETH_W5500_SCK
#define PROTOCORE_ETH_W5500_SCK 12 ///< HSPI clock (S3-DevKitC default)
#endif
#ifndef PROTOCORE_ETH_W5500_MISO
#define PROTOCORE_ETH_W5500_MISO 13 ///< HSPI MISO (S3-DevKitC default)
#endif
#ifndef PROTOCORE_ETH_W5500_MOSI
#define PROTOCORE_ETH_W5500_MOSI 11 ///< HSPI MOSI (S3-DevKitC default)
#endif
// W5500 SPI clock in MHz. The W5500 datasheet allows up to 33.3 MHz; 20 is the arduino-esp32 default and
// a safe value for breadboard jumper wiring. Higher clocks raise throughput (the link is SPI-bound, not
// PHY-bound) but need clean, short wiring - marginal signal integrity at high MHz corrupts frames.
#ifndef PROTOCORE_ETH_W5500_SPI_MHZ
#define PROTOCORE_ETH_W5500_SPI_MHZ 20 ///< W5500 SPI clock (MHz); raise for throughput on clean wiring
#endif

// Feature / service / codec tuning knobs are consolidated at the END of this file,
// under "Feature tuning knobs (grouped and gated by feature)" - placed there so every
// PROTOCORE_ENABLE_* flag is already resolved and each group can gate on its own feature.

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
 * qop, nc, cnonce) is far longer than MAX_VAL_LEN, so when PROTOCORE_ENABLE_AUTH
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
#ifndef PROTOCORE_DIGEST_NONCE_LIFETIME_MS
#define PROTOCORE_DIGEST_NONCE_LIFETIME_MS (5u * 60u * 1000u)
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
 * would leave the arena with no worst case - `Ws.frame.len` is a `uint16_t`, and neither
 * WS_FRAME_SIZE (which bounds the *inbound* reassembled message) nor PROTOCORE_WS_FRAG_SIZE (off by
 * default, and a runtime setter besides) constrains it. This is that bound, and it is what makes
 * PROTOCORE_PLAINTEXT_WORK_WS_SEND a compile-time constant.
 *
 * A larger message is still sent, uncompressed, as the per-message RSV1 flag makes legal - the same
 * outcome as before this was declared, except chosen rather than reached by an allocation failure.
 * Raising it costs arena: the term grows by roughly 1.125x the increase.
 */
#ifndef PROTOCORE_WS_DEFLATE_MAX
#define PROTOCORE_WS_DEFLATE_MAX WS_FRAME_SIZE
#endif

// ---------------------------------------------------------------------------
// Server-Sent Events sizing constants
// ---------------------------------------------------------------------------

/**
 * @brief Maximum simultaneous SSE connections.
 *
 * Each connection occupies one TCP slot from MAX_CONNS and one entry in
 * protocore_sse_pool[].  MAX_WS_CONNS + MAX_SSE_CONNS must not exceed MAX_CONNS.
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

/**
 * @brief WebSocket outbound fragmentation size (RFC 6455 sec 5.4), in payload bytes. 0 = off.
 *
 * When >0, an outbound data message (text/binary) longer than this many payload bytes is split into
 * that-sized WebSocket frames - the first carrying the opcode (and the RFC 7692 RSV1 bit if the message
 * is compressed), the rest CONTINUATION, the last with FIN - instead of one large frame. Sizing it near
 * the TCP MSS (e.g. 1400) keeps each frame within whole segments (MTU-aligned) and lets a peer with a
 * bounded per-frame reassembly buffer receive an arbitrarily long message. The runtime override is
 * Ws.set_frag_size. Compression applies to the whole message first, then the compressed bytes are
 * split. Default 0 (one frame per message, unchanged).
 */
#ifndef PROTOCORE_WS_FRAG_SIZE
#define PROTOCORE_WS_FRAG_SIZE 0
#endif

/** @brief Buffer (BSS) for a WebDAV 207 Multi-Status response, in bytes (see PROTOCORE_ENABLE_WEBDAV). */
#ifndef PROTOCORE_WEBDAV_BUF_SIZE
#define PROTOCORE_WEBDAV_BUF_SIZE 2048
#endif

/** @brief Maximum children listed in a WebDAV Depth-1 PROPFIND (bounds the response). */
#ifndef PROTOCORE_WEBDAV_MAX_ENTRIES
#define PROTOCORE_WEBDAV_MAX_ENTRIES 32
#endif

/**
 * @brief Deepest tree a WebDAV DELETE / COPY walks before refusing (see PROTOCORE_ENABLE_WEBDAV).
 *
 * The recursive walkers carry one path and one child name per level, so this is what turns their
 * working storage into a fixed number instead of one that grows with the tree being walked. It was
 * a bare 8 in the walk's own test, which bounded the recursion but sized nothing, because the paths
 * were stack arrays the footprint could not see.
 */
#ifndef PROTOCORE_DAV_MAX_DEPTH
#define PROTOCORE_DAV_MAX_DEPTH 8
#endif

/** @brief Maximum properties echoed in a WebDAV PROPPATCH 207 response (bounds the response). */
#ifndef PROTOCORE_WEBDAV_MAX_PROPS
#define PROTOCORE_WEBDAV_MAX_PROPS 16
#endif

/**
 * @brief HTTP method-token buffer size (bytes, including the NUL).
 *
 * Sized for the longest method the server must recognize: 8 normally (OPTIONS),
 * grown to fit the WebDAV methods (PROPPATCH is 9 chars) when WebDAV is enabled.
 */
#ifndef PROTOCORE_METHOD_BUF_SIZE
#if PROTOCORE_ENABLE_WEBDAV
#define PROTOCORE_METHOD_BUF_SIZE 12
#else
#define PROTOCORE_METHOD_BUF_SIZE 8
#endif
#endif

/** @brief Max header lines parsed per STOMP frame (extras beyond this are ignored). */
#ifndef PROTOCORE_STOMP_MAX_HEADERS
#define PROTOCORE_STOMP_MAX_HEADERS 16
#endif

/** @brief 3964R block-body buffer size (built/received bytes: DLE-stuffed payload + DLE ETX + BCC). */
#ifndef PROTOCORE_SIMATIC_BLOCK_MAX
#define PROTOCORE_SIMATIC_BLOCK_MAX 256
#endif

/** @brief 3964R QVZ (Quittungsverzugszeit): handshake acknowledge-delay timeout, ms.
 *         Siemens AcknDelayTime, default 16#07D0. */
#ifndef PROTOCORE_SIMATIC_QVZ_MS
#define PROTOCORE_SIMATIC_QVZ_MS 2000
#endif

/** @brief 3964R ZVZ (Zeichenverzugszeit): inter-character timeout while receiving a block, ms.
 *         Siemens CharacterDelayTime, default 16#00DC. */
#ifndef PROTOCORE_SIMATIC_ZVZ_MS
#define PROTOCORE_SIMATIC_ZVZ_MS 220
#endif

/** @brief Max serialized size of one Sparkplug B metric submessage (stack temp, bytes). */
#ifndef PROTOCORE_SPB_METRIC_MAX
#define PROTOCORE_SPB_METRIC_MAX 256
#endif

/** @brief Number of Modbus coils (FC 1/5/15), single-bit R/W (BSS, bit-packed). */
#ifndef PROTOCORE_MODBUS_COILS
#define PROTOCORE_MODBUS_COILS 64
#endif

/** @brief Number of Modbus discrete inputs (FC 2), single-bit read-only (BSS, bit-packed). */
#ifndef PROTOCORE_MODBUS_DISCRETE_INPUTS
#define PROTOCORE_MODBUS_DISCRETE_INPUTS 64
#endif

/** @brief Number of Modbus holding registers (FC 3/6/16), 16-bit R/W (BSS). */
#ifndef PROTOCORE_MODBUS_HOLDING_REGS
#define PROTOCORE_MODBUS_HOLDING_REGS 64
#endif

/** @brief Number of Modbus input registers (FC 4), 16-bit read-only (BSS). */
#ifndef PROTOCORE_MODBUS_INPUT_REGS
#define PROTOCORE_MODBUS_INPUT_REGS 64
#endif

/** @brief Maximum simultaneous TLS connections (each holds mbedTLS record buffers). */
#ifndef MAX_TLS_CONNS
#define MAX_TLS_CONNS 1
#endif

/**
 * @brief 1 when the portable TLS 1.3 compiles: TLS is on and the vendor has no stack of its own.
 *
 * DERIVED from PROTOCORE_ENABLE_TLS and PROTOCORE_HAS_VENDOR_TLS (protocore_platform.h),
 * never set by hand. It selects the record layer and connection driver in network_drivers/tls, and widens the guards on
 * the TLS 1.3 pieces the QUIC and DTLS handshakes already share.
 */
#if PROTOCORE_ENABLE_TLS && !PROTOCORE_HAS_VENDOR_TLS
#define PROTOCORE_TLS_SOFTWARE 1
#else
#define PROTOCORE_TLS_SOFTWARE 0
#endif

/** @brief Session-ticket lifetime / key-rotation period in seconds (see PROTOCORE_ENABLE_TLS_RESUMPTION). */
#ifndef PROTOCORE_TLS_TICKET_LIFETIME_S
#define PROTOCORE_TLS_TICKET_LIFETIME_S 86400
#endif

/** @brief Maximum length of a verified mTLS peer subject DN string (incl. NUL). */
#ifndef PROTOCORE_MTLS_SUBJECT_MAX
#define PROTOCORE_MTLS_SUBJECT_MAX 128
#endif

/** @brief Maximum extra variable-bindings (beyond sysUpTime/snmpTrapOID) in one notification. */
#ifndef PROTOCORE_SNMP_TRAP_MAX_VARBINDS
#define PROTOCORE_SNMP_TRAP_MAX_VARBINDS 8
#endif

/** @brief Static datagram buffer for an outbound SNMP notification, bytes. */
#ifndef PROTOCORE_SNMP_TRAP_BUF_SIZE
#define PROTOCORE_SNMP_TRAP_BUF_SIZE 1024
#endif

/** @brief Maximum sub-identifiers (arcs) in an SNMP object identifier. */
#ifndef SNMP_MAX_OID_LEN
#define SNMP_MAX_OID_LEN 32
#endif

/**
 * @brief Maximum registered MIB objects (the agent's fixed OID table).
 *
 * Each entry holds its OID, a value descriptor, and optional get/set callbacks
 * (see src/services/net/snmp/snmp_agent.h). The table lives in BSS; entries are
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

/** @brief Default read-only community (overridable at runtime via SnmpAgent.community.ro + SnmpAgent.init).
 *  Deployments SHOULD change this from the RFC-1157 well-known "public" for anything but a closed
 *  network. */
#ifndef PROTOCORE_SNMP_DEFAULT_RO_COMMUNITY
#define PROTOCORE_SNMP_DEFAULT_RO_COMMUNITY "public"
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
// CoAP server sizing constants  (PROTOCORE_ENABLE_COAP must be 1)
// ---------------------------------------------------------------------------

/** @brief Maximum simultaneous CoAP observers (one slot per observed resource per client). */
#ifndef PROTOCORE_COAP_MAX_OBSERVERS
#define PROTOCORE_COAP_MAX_OBSERVERS 4
#endif

/**
 * @brief CoAP message de-duplication cache size (RFC 7252 sec 4.5). A Confirmable request the server has
 *        already answered is recognized by its (source endpoint, Message-ID) and re-answered with the
 *        cached response WITHOUT re-running the handler - so a client's CON retransmission cannot execute
 *        a non-idempotent request (POST/PUT/DELETE) twice. Set to 0 to compile the dedup cache out.
 */
#ifndef PROTOCORE_COAP_DEDUP_ENTRIES
#define PROTOCORE_COAP_DEDUP_ENTRIES 4
#endif

/** @brief Largest cached response the dedup cache retains per entry; a bigger response is not cached (a
 *  retransmission re-processes it, fine for the idempotent GET whose block-wise reply exceeds this). */
#ifndef PROTOCORE_COAP_DEDUP_RESP_MAX
#define PROTOCORE_COAP_DEDUP_RESP_MAX 256
#endif

/** @brief How long (ms) a dedup entry stays fresh - RFC 7252 EXCHANGE_LIFETIME (~247 s) by default, past
 *  which a repeat Message-ID is treated as a new exchange. */
#ifndef PROTOCORE_COAP_DEDUP_LIFETIME_MS
#define PROTOCORE_COAP_DEDUP_LIFETIME_MS 247000u
#endif

/** @brief Largest block-size exponent (SZX) the server will use: block size = 2^(SZX+4) bytes, SZX 0..6 (16..1024). */
#ifndef PROTOCORE_COAP_BLOCK_SZX_MAX
#define PROTOCORE_COAP_BLOCK_SZX_MAX 6
#endif

/**
 * @brief Reassembly buffer for a block-wise (Block1) request upload, in bytes.
 *
 * One buffer of this size lives in BSS only when PROTOCORE_ENABLE_COAP_BLOCK is set.
 * It bounds the largest payload a chunked POST/PUT can deliver to a handler.
 */
#ifndef PROTOCORE_COAP_BLOCK1_MAX
#define PROTOCORE_COAP_BLOCK1_MAX 1024
#endif

/**
 * @brief Maximum registered CoAP resources (the server's fixed routing table).
 *
 * Each entry holds a path pointer, an allowed-methods bitmask, and a handler.
 * The table lives in BSS and is scanned linearly (small table).
 */
#ifndef PROTOCORE_COAP_MAX_RESOURCES
#define PROTOCORE_COAP_MAX_RESOURCES 8
#endif

/** @brief Maximum reconstructed Uri-Path length, including separators and the leading '/'. */
#ifndef PROTOCORE_COAP_MAX_PATH
#define PROTOCORE_COAP_MAX_PATH 64
#endif

/** @brief Maximum reconstructed Uri-Query length (segments joined by '&'). */
#ifndef PROTOCORE_COAP_MAX_QUERY
#define PROTOCORE_COAP_MAX_QUERY 64
#endif

/**
 * @brief Maximum CoAP request/response payload in bytes.
 *
 * Sizes the static scratch a handler writes its response body into and bounds
 * the request payload handed to it. One buffer of this size lives in BSS.
 */
#ifndef PROTOCORE_COAP_MAX_PAYLOAD
#define PROTOCORE_COAP_MAX_PAYLOAD 256
#endif

/**
 * @brief Static response-datagram buffer for the CoAP UDP server.
 *
 * One buffer of this size lives in BSS (the request is transport-owned). Must
 * hold a 4-byte header + token (<=8) + the Content-Format option + a 0xFF marker
 * + PROTOCORE_COAP_MAX_PAYLOAD bytes. When block-wise transfer is enabled it must
 * also hold one full block (2^(PROTOCORE_COAP_BLOCK_SZX_MAX+4) bytes) + option
 * overhead, so the default grows accordingly.
 */
#ifndef PROTOCORE_COAP_MSG_BUF_SIZE
#if PROTOCORE_ENABLE_COAP_BLOCK
#define PROTOCORE_COAP_MSG_BUF_SIZE 1152
#else
#define PROTOCORE_COAP_MSG_BUF_SIZE 512
#endif
#endif

/** @brief Default UDP port the CoAP observe transport notifies from (IANA well-known 5683). */
#ifndef PROTOCORE_COAP_OBSERVE_PORT
#define PROTOCORE_COAP_OBSERVE_PORT 5683
#endif

/**
 * @brief Bytes of the static BSS arena mbedTLS allocates from (PROTOCORE_ENABLE_TLS).
 *
 * All mbedTLS allocations (per-connection record buffers, handshake temporaries,
 * cert/key parsing) are served from this fixed arena via a custom allocator
 * installed with mbedtls_platform_set_calloc_free() - never the system heap. Must
 * cover the worst-case handshake peak for MAX_TLS_CONNS; if undersized the
 * handshake fails cleanly (no corruption). Measured peak for ONE ECDSA P-256
 * connection on Arduino-esp32 (16 KB IN + 16 KB OUT records) is ~41.5 KB, so the
 * default leaves a small margin. An RSA cert/larger chain needs more; query the
 * live peak via protocore_tls_arena_peak(). NOTE: a second concurrent TLS connection
 * roughly doubles the record-buffer cost (~32 KB more), which overflows the
 * static DRAM budget - keep MAX_TLS_CONNS at 1 unless you shrink the IDF record
 * sizes (CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN, needs an ESP-IDF build).
 */
#ifndef PROTOCORE_TLS_ARENA_SIZE
// The arena is SHARED across all TLS connections, so it must cover the peak for MAX_TLS_CONNS: ~48 KB
// for the first handshake (ECDSA P-256, 16 KB IN + 16 KB OUT records + temporaries) plus ~32 KB of
// record buffers per additional concurrent connection. Auto-derive so a profile that raises
// MAX_TLS_CONNS (a PSRAM board, via PROTOCORE_TLS_ARENA_IN_PSRAM) grows the arena to match instead of
// silently starving the second handshake. MAX_TLS_CONNS == 1 keeps the historical 49152.
#define PROTOCORE_TLS_ARENA_SIZE (49152 + (MAX_TLS_CONNS - 1) * 32768)
#endif

/**
 * @brief Place the TLS arena in external PSRAM instead of internal DRAM (ESP32).
 *
 * The internal static-DRAM ceiling (`dram0_0_seg`) is only ~122 KB, so a single
 * ~48 KB arena already uses a large slice and a second concurrent connection
 * (MAX_TLS_CONNS > 1) overflows it. On a board with PSRAM, set this to 1 to move
 * the arena to external RAM via `EXT_RAM_BSS_ATTR` / `EXT_RAM_ATTR`, freeing the
 * whole `PROTOCORE_TLS_ARENA_SIZE` back to internal DRAM so many connections fit.
 * Requires `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` (and PSRAM enabled) in the
 * ESP-IDF/PlatformIO config; without it the attribute is a no-op and the arena
 * stays in DRAM (safe fallback). No effect on the native host build.
 */
#ifndef PROTOCORE_TLS_ARENA_IN_PSRAM
#define PROTOCORE_TLS_ARENA_IN_PSRAM 0
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
#ifndef PROTOCORE_TLS_MAX_FRAG_LEN
#define PROTOCORE_TLS_MAX_FRAG_LEN 0
#endif

/**
 * @brief Lead the ECDHE curve/group preference with secp256r1 (P-256) instead of x25519.
 *
 * PER-VARIANT by default, because the ECC silicon differs wildly between dies. On a chip with a
 * hardware NIST-ECC accelerator (`PROTOCORE_HW_ECC` = 1: ESP32-P4/C5/C6/C61/H2/H4/... where mbedTLS routes
 * P-256 through the HW via `ecc_alt`), P-256 is dramatically faster than x25519, which stays software
 * (measured on an ESP32-P4: P-256 ECDHE ~10 ms vs x25519 ~132 ms, and the full TLS handshake ~29 ms
 * vs ~160 ms - a 5.5x win). On a chip WITHOUT ECC HW (`PROTOCORE_HW_ECC` = 0: ESP32-S3/S2/classic), both
 * curves are software and near-identical in the full handshake, so x25519 (the security-preferred
 * modern default) leads and this stays 0 (the S3 order is unchanged).
 *
 * So the default tracks `PROTOCORE_HW_ECC` - the profile's assertion that the die has NIST-ECC HW - and is
 * overridable: force `-DPROTOCORE_TLS_ECDHE_PREFER_P256=0` to mandate x25519-first even on an ECC-HW chip
 * (a deployment policy choice), or `=1` to prefer P-256 on a chip whose profile has not flagged HW ECC.
 * This only reorders PREFERENCE; every curve stays enabled, so a peer that offers just one still
 * connects. Applied to both the server and outbound-client configs (tls.cpp `tls_apply_curve_pref`).
 */
#ifndef PROTOCORE_TLS_ECDHE_PREFER_P256
#define PROTOCORE_TLS_ECDHE_PREFER_P256 PROTOCORE_HW_ECC
#endif

/**
 * @brief Acknowledge that a MAX_TLS_CONNS > 1 build has been sized to fit.
 *
 * The whole TLS arena is static `.bss` and the internal `dram0_0_seg` ceiling is only
 * ~122 KB, so a second concurrent connection's arena overflows it on a stock build.
 * A validation guard (bottom of this file) therefore rejects MAX_TLS_CONNS > 1 unless
 * you have taken one of the paths in docs/KNOWN_LIMITATIONS.md - move the arena to
 * PSRAM (`PROTOCORE_TLS_ARENA_IN_PSRAM`, which satisfies the guard on its own), shrink the
 * mbedTLS records in a custom ESP-IDF build, or reclaim internal DRAM - and then set
 * this to 1 to confirm the build was sized deliberately.
 */
#ifndef PROTOCORE_TLS_ACK_MULTI_CONN_DRAM
#define PROTOCORE_TLS_ACK_MULTI_CONN_DRAM 0
#endif

// ---------------------------------------------------------------------------
// Optional network services (ESP32-only thin wrappers; each default-off so it
// costs no code/RAM/flash unless explicitly enabled).
// ---------------------------------------------------------------------------

/** @brief Services the responder advertises at once, `_http._tcp` included. */
#ifndef PROTOCORE_MDNS_MAX_SERVICES
#define PROTOCORE_MDNS_MAX_SERVICES 4
#endif

/** @brief Bytes of packed `key=value` TXT strings, each with its own length byte ahead of it. */
#ifndef PROTOCORE_MDNS_TXT_MAX
#define PROTOCORE_MDNS_TXT_MAX 128
#endif

/** @brief Longest host label, service type or proto label the responder holds, NUL included. */
#ifndef PROTOCORE_MDNS_LABEL_MAX
#define PROTOCORE_MDNS_LABEL_MAX 32
#endif

/**
 * @brief Response datagram the responder composes.
 *
 * One answer set is an A plus, per service, two PTRs, an SRV and a TXT, so this bounds how many
 * services fit one packet rather than how many may be registered.
 */
#ifndef PROTOCORE_MDNS_TX_MAX
#define PROTOCORE_MDNS_TX_MAX 512
#endif

/**
 * @brief Local UDP port the portable SNTP client asks from.
 *
 * Not 123: a device running PROTOCORE_ENABLE_NTP_SERVER already holds that port, and the client has to
 * bind one of its own to hear the reply come back.
 */
#ifndef PROTOCORE_NTP_CLIENT_PORT
#define PROTOCORE_NTP_CLIENT_PORT 1123
#endif

/** @brief Stratum the NTP server advertises (distance from a reference clock; 1-15). */
#ifndef PROTOCORE_NTP_SERVER_STRATUM
#define PROTOCORE_NTP_SERVER_STRATUM 3
#endif

/** @brief Max A records in the DNS server's fixed table. */
#ifndef PROTOCORE_DNS_SERVER_MAX_RECORDS
#define PROTOCORE_DNS_SERVER_MAX_RECORDS 8
#endif

/** @brief TTL (seconds) the DNS server puts on its answers. */
#ifndef PROTOCORE_DNS_SERVER_TTL
#define PROTOCORE_DNS_SERVER_TTL 60
#endif

/** @brief Max length of a queried/stored DNS name (bytes, incl NUL). */
#ifndef PROTOCORE_DNS_NAME_MAX
#define PROTOCORE_DNS_NAME_MAX 128
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
 * by priority) when PROTOCORE_ENABLE_TIME_SOURCE is set - register your sources with
 * protocore_time_source_add() (protocore_rtc_time_source, protocore_ntp_time_source, ...). Otherwise it comes
 * straight from NTP (protocore_ntp_http_date). Needs at least one such time source to emit.
 */
#ifndef PROTOCORE_HTTP_EMIT_DATE
#define PROTOCORE_HTTP_EMIT_DATE 0
#endif

/** @brief Maximum registered time sources (PROTOCORE_ENABLE_TIME_SOURCE). */
#ifndef PROTOCORE_TIME_SOURCE_MAX
#define PROTOCORE_TIME_SOURCE_MAX 4
#endif

/**
 * @brief Shared I2C bus pins for the sensor / peripheral drivers (RTC, SHT3x, MPR121, ADS1115,
 * INA219, PCA9685). All of them share one bus via protocore_i2c_begin() (server/peripherals/i2c.h), so
 * this is the single place to move it. The default -1 uses the platform's default pins (GPIO 21
 * SDA / 22 SCL on the classic ESP32). Set both to free GPIOs when those pins are taken - most
 * importantly a **wired-Ethernet PHY**: the LAN8720 RMII uses GPIO 21 (TX_EN) and GPIO 22
 * (TXD1) on the classic ESP32 (WROOM/WROVER) and the ESP32-P4 (which have the RMII EMAC), so
 * with that Ethernet on, move the I2C bus off them (e.g. 32 / 33). The ESP32-S3/C3 have no RMII
 * MAC and use an SPI Ethernet (W5500) instead - relocate the bus off whatever SPI pins that
 * uses. UART peripherals (LD2410) take their RX/TX pins at protocore_ld2410_begin(), so remap those too.
 */
#ifndef PROTOCORE_I2C_SDA_PIN
#define PROTOCORE_I2C_SDA_PIN -1
#endif
#ifndef PROTOCORE_I2C_SCL_PIN
#define PROTOCORE_I2C_SCL_PIN -1
#endif

/**
 * @brief Shared SPI bus pins for the peripheral drivers, the same way the I2C pins above are
 * shared. The default -1 uses the platform's default pins for its VSPI/HSPI host. Set them when
 * those pins are taken, most often by a W5500 SPI Ethernet on a part with no RMII MAC (the
 * ESP32-S3 / C3), which drives this same bus.
 */
#ifndef PROTOCORE_SPI_MOSI_PIN
#define PROTOCORE_SPI_MOSI_PIN -1
#endif
#ifndef PROTOCORE_SPI_MISO_PIN
#define PROTOCORE_SPI_MISO_PIN -1
#endif
#ifndef PROTOCORE_SPI_SCLK_PIN
#define PROTOCORE_SPI_SCLK_PIN -1
#endif

/** @brief I2C address of the RTC (DS1307/DS3231 are fixed at 0x68). */
#ifndef PROTOCORE_RTC_I2C_ADDR
#define PROTOCORE_RTC_I2C_ADDR 0x68
#endif

/** @brief HMMD UART baud rate (the module's factory default is 115200). */
#ifndef PROTOCORE_HMMD_BAUD
#define PROTOCORE_HMMD_BAUD 115200
#endif

/** @brief UART unit the HMMD is wired to. Unit 2 is the one free of the console on most boards. */
#ifndef PROTOCORE_HMMD_UART
#define PROTOCORE_HMMD_UART 2
#endif

/** @brief LD2410 UART baud rate (the module's fixed factory default is 256000). */
#ifndef PROTOCORE_LD2410_BAUD
#define PROTOCORE_LD2410_BAUD 256000
#endif

/** @brief UART unit the LD2410 is wired to. Unit 2 is the one free of the console on most boards. */
#ifndef PROTOCORE_LD2410_UART
#define PROTOCORE_LD2410_UART 2
#endif

/** @brief GPIO the SEN0192 OUT line is wired to. */
#ifndef PROTOCORE_SEN0192_PIN
#define PROTOCORE_SEN0192_PIN 4
#endif

/** @brief Presence is held this many ms after the last active (motion) sample before it clears. */
#ifndef PROTOCORE_SEN0192_HOLD_MS
#define PROTOCORE_SEN0192_HOLD_MS 2000
#endif

/** @brief SEN0192 OUT polarity: 1 = the OUT line reads HIGH on motion, 0 = active-LOW. */
#ifndef PROTOCORE_SEN0192_ACTIVE_HIGH
#define PROTOCORE_SEN0192_ACTIVE_HIGH 1
#endif

/** @brief I2C address of the MPR121 (0x5A default; 0x5B/0x5C/0x5D via the ADDR pin). */
#ifndef PROTOCORE_MPR121_I2C_ADDR
#define PROTOCORE_MPR121_I2C_ADDR 0x5A
#endif

/** @brief MPR121 per-electrode touch threshold (delta counts from baseline; NXP AN3944 suggests ~4..12).
 *         Higher = less sensitive. Keep the release threshold below it for hysteresis. */
#ifndef PROTOCORE_MPR121_TOUCH_THRESHOLD
#define PROTOCORE_MPR121_TOUCH_THRESHOLD 12
#endif

/** @brief MPR121 per-electrode release threshold (delta counts; should be below the touch threshold). */
#ifndef PROTOCORE_MPR121_RELEASE_THRESHOLD
#define PROTOCORE_MPR121_RELEASE_THRESHOLD 6
#endif

/** @brief I2C address of the SHT3x (0x44 with ADDR low; 0x45 with ADDR high). */
#ifndef PROTOCORE_SHT3X_I2C_ADDR
#define PROTOCORE_SHT3X_I2C_ADDR 0x44
#endif

/** @brief I2C address of the PCA9685 (0x40 default; the six address pins select 0x40..0x7F). */
#ifndef PROTOCORE_PCA9685_I2C_ADDR
#define PROTOCORE_PCA9685_I2C_ADDR 0x40
#endif

/** @brief Default PWM output frequency in Hz (50 Hz suits hobby servos). */
#ifndef PROTOCORE_PCA9685_FREQ
#define PROTOCORE_PCA9685_FREQ 50
#endif

/** @brief I2C address of the ADS1115 (0x48 with ADDR to GND; 0x49/0x4A/0x4B for VDD/SDA/SCL). */
#ifndef PROTOCORE_ADS1115_I2C_ADDR
#define PROTOCORE_ADS1115_I2C_ADDR 0x48
#endif

/** @brief Default ADS1115 PGA gain code (ADS1115_GAIN_*): 0=+/-6.144V, 1=+/-4.096V, 2=+/-2.048V (default),
 *         3=+/-1.024V, 4=+/-0.512V, 5=+/-0.256V. Also the fallback when a read passes an invalid gain. */
#ifndef PROTOCORE_ADS1115_GAIN
#define PROTOCORE_ADS1115_GAIN 2 // ADS1115_GAIN_2 (+/- 2.048 V)
#endif

/** @brief Default ADS1115 data-rate code (ADS1115_DR_*): 0=8, 1=16, 2=32, 3=64, 4=128 (default), 5=250,
 *         6=475, 7=860 SPS. The single-shot read waits the matching conversion time. */
#ifndef PROTOCORE_ADS1115_DR
#define PROTOCORE_ADS1115_DR 4 // ADS1115_DR_128 (128 SPS)
#endif

/** @brief ADS1115 input mode: 0 = single-ended (AINx vs GND), 1 = differential. In differential mode the
 *         channel selects the pair: 0=AIN0-AIN1, 1=AIN0-AIN3, 2=AIN1-AIN3, 3=AIN2-AIN3. */
#ifndef PROTOCORE_ADS1115_DIFFERENTIAL
#define PROTOCORE_ADS1115_DIFFERENTIAL 0
#endif

/** @brief I2C address of the INA219 (0x40 default; the A0/A1 pins select 0x40..0x4F). */
#ifndef PROTOCORE_INA219_I2C_ADDR
#define PROTOCORE_INA219_I2C_ADDR 0x40
#endif

/** @brief Default INA219 current LSB in microamps per bit (calibration input). The fallback when
 *         Ina219.begin is passed 0. 100 uA/bit with a 100 mohm shunt -> a 2 A full-scale range. */
#ifndef PROTOCORE_INA219_CURRENT_LSB_UA
#define PROTOCORE_INA219_CURRENT_LSB_UA 100
#endif

/** @brief Default INA219 shunt resistance in milliohms (calibration input). The fallback when
 *         Ina219.begin is passed 0. 100 mohm is the common breakout value. */
#ifndef PROTOCORE_INA219_SHUNT_MOHM
#define PROTOCORE_INA219_SHUNT_MOHM 100
#endif

/** @brief Max key/value entries in the host (test) config backend. */
#ifndef PROTOCORE_CONFIG_MAX_ENTRIES
#define PROTOCORE_CONFIG_MAX_ENTRIES 16
#endif

/** @brief Max key length incl. null (NVS caps keys at 15 chars). */
#ifndef PROTOCORE_CONFIG_KEY_MAX
#define PROTOCORE_CONFIG_KEY_MAX 16
#endif

/**
 * @brief Max value bytes per entry in the host (test) config backend.
 *
 * Holds the largest blob the seam carries, which is the SSH host key's PKCS#8 DER
 * (SSH_RSA_KEY_DER_MAX, 1700). A power of two keeps the row stride a shift.
 */
#ifndef PROTOCORE_CONFIG_VAL_MAX
#define PROTOCORE_CONFIG_VAL_MAX 2048
#endif

/**
 * @brief Include the trademark-named themes in the embedded set (default on / open-source).
 *
 * A few themes are named after a company or product (Darcula, Windows XP, Discord, Spotify, ...). The
 * palette is just colors, but a commercial product should not ship the branded name, so a commercial
 * build sets this to 0 to drop those blobs from the registry (the list is `RESTRICTED` in
 * `src/web_assets/wizard/gen_themes.py`). The open-source (AGPL) build keeps them.
 */
#ifndef PROTOCORE_THEMES_INCLUDE_TRADEMARKED
#define PROTOCORE_THEMES_INCLUDE_TRADEMARKED 1
#endif

/** @brief Maximum widgets in the dashboard table (BSS value array). */
#ifndef PROTOCORE_DASHBOARD_MAX_WIDGETS
#define PROTOCORE_DASHBOARD_MAX_WIDGETS 16
#endif

/** @brief Stack buffer for the dashboard layout / values JSON (bytes). */
#ifndef PROTOCORE_DASHBOARD_JSON_BUF
#define PROTOCORE_DASHBOARD_JSON_BUF 1024
#endif

/** @brief Maximum partitions the monitor reports (BSS table). */
#ifndef PROTOCORE_PARTITION_MAX
#define PROTOCORE_PARTITION_MAX 16
#endif

/** @brief Stack buffer for the partition-map JSON (bytes). */
#ifndef PROTOCORE_PARTITION_JSON_BUF
#define PROTOCORE_PARTITION_JSON_BUF 1024
#endif

/** @brief Maximum GPIO pins the mapper reports (BSS table). */
#ifndef PROTOCORE_GPIO_MAX
#define PROTOCORE_GPIO_MAX 40
#endif

/** @brief Stack buffer for the GPIO-map JSON (bytes). */
#ifndef PROTOCORE_GPIO_JSON_BUF
#define PROTOCORE_GPIO_JSON_BUF 1024
#endif

/** @brief Stack buffer for one telemetry line (bytes). */
#ifndef PROTOCORE_UDP_TELEMETRY_BUF
#define PROTOCORE_UDP_TELEMETRY_BUF 256
#endif

/** @brief Default StatsD collector UDP port (StatsD/Graphite standard). */
#ifndef PROTOCORE_STATSD_PORT
#define PROTOCORE_STATSD_PORT 8125
#endif

/** @brief Stack buffer for one StatsD line (bytes; caps metric name + value + tags). */
#ifndef PROTOCORE_STATSD_LINE_MAX
#define PROTOCORE_STATSD_LINE_MAX 256
#endif

/** @brief Free-heap floor (bytes); below this trips the heap guardrail. */
#ifndef PROTOCORE_GUARDRAIL_HEAP_MIN
#define PROTOCORE_GUARDRAIL_HEAP_MIN 8192
#endif

/** @brief Largest-free-block floor (bytes); below this trips the fragmentation guardrail. */
#ifndef PROTOCORE_GUARDRAIL_FRAG_MIN_BLOCK
#define PROTOCORE_GUARDRAIL_FRAG_MIN_BLOCK 4096
#endif

/** @brief Task remaining-stack floor (bytes); below this trips the stack guardrail. */
#ifndef PROTOCORE_GUARDRAIL_STACK_MIN
#define PROTOCORE_GUARDRAIL_STACK_MIN 512
#endif

/** @brief Max monitored lifelines in the fail-safe registry (static, zero-heap). */
#ifndef PROTOCORE_FAILSAFE_MAX_LIFELINES
#define PROTOCORE_FAILSAFE_MAX_LIFELINES 8
#endif

/** @brief CPU clock (MHz) when there is work to do. */
#ifndef PROTOCORE_POWER_MHZ_MAX
#define PROTOCORE_POWER_MHZ_MAX 240
#endif

/** @brief CPU clock (MHz) when idle, thermally throttled, or recovering from a brownout. */
#ifndef PROTOCORE_POWER_MHZ_MIN
#define PROTOCORE_POWER_MHZ_MIN 80
#endif

/** @brief Load percentage at/above which the ceiling clock is used. */
#ifndef PROTOCORE_POWER_BUSY_PCT
#define PROTOCORE_POWER_BUSY_PCT 40
#endif

/** @brief Die temperature (C) at/above which the clock is throttled. */
#ifndef PROTOCORE_POWER_TEMP_HOT_C
#define PROTOCORE_POWER_TEMP_HOT_C 80
#endif

/**
 * @brief Die temperature (C) at/below which the throttle is released.
 *
 * Deliberately below PROTOCORE_POWER_TEMP_HOT_C: with a single threshold a part sitting exactly at the
 * limit would flap between ceiling and floor every tick, which is worse than either state.
 *
 * The gap has to be wider than the temperature swing the clock change *itself* causes, or the
 * governor oscillates no matter how correct the hysteresis is. Measured on an ESP32-S3: dropping
 * 240 -> 80 MHz cools the die about 2 C within one 500 ms tick, and going back up reheats it by the
 * same amount. A band narrower than that swing is self-sustaining - the throttle's own effect
 * carries the die back across the release threshold. The 10 C default clears it with room to spare.
 */
#ifndef PROTOCORE_POWER_TEMP_COOL_C
#define PROTOCORE_POWER_TEMP_COOL_C 70
#endif

/** @brief How long (ms) to hold the floor clock after a brownout reset before ramping back up. */
#ifndef PROTOCORE_POWER_RECOVER_MS
#define PROTOCORE_POWER_RECOVER_MS 10000
#endif

/**
 * @brief Consecutive I/O failures that declare a removable volume gone.
 *
 * Not 1: a single failed write is not proof a card left (a transient bus error, a full volume), and
 * tearing down a working mount over one error would be its own bug. Any success resets the run.
 */
#ifndef PROTOCORE_HOTSWAP_FAIL_THRESHOLD
#define PROTOCORE_HOTSWAP_FAIL_THRESHOLD 3
#endif

/** @brief Minimum gap between remount attempts while a volume is absent or faulted (ms). */
#ifndef PROTOCORE_HOTSWAP_PROBE_MS
#define PROTOCORE_HOTSWAP_PROBE_MS 2000
#endif

/**
 * @brief MTConnect rolling sample buffer sizing (PROTOCORE_ENABLE_MTCONNECT).
 *
 * The agent retains the most recent ::PROTOCORE_MTC_SAMPLE_BUFFER observations in a fixed ring so a
 * subscriber can replay them with the `sample` from/count long-poll cursor (MTC1.4 §6.7): a request
 * asks for observations starting at a sequence number, and the response header reports firstSequence /
 * lastSequence / nextSequence so the client knows what it received and where to resume. Each retained
 * observation stores its type / dataItemId / timestamp / value in fixed char fields; when the ring is
 * full the oldest is evicted and firstSequence advances. Zero-heap, compile-time sized; the buffer costs
 * ~PROTOCORE_MTC_SAMPLE_BUFFER * (48 + the four string caps) bytes only where a protocore_mtc_sample_buffer is used.
 */
#ifndef PROTOCORE_MTC_SAMPLE_BUFFER
#define PROTOCORE_MTC_SAMPLE_BUFFER 32 // observations retained for `sample` replay
#endif

#ifndef PROTOCORE_MTC_STR_MAX
#define PROTOCORE_MTC_STR_MAX 24 // max stored type / dataItemId length (excl NUL)
#endif
#ifndef PROTOCORE_MTC_TS_MAX
#define PROTOCORE_MTC_TS_MAX 32 // max stored ISO-8601 timestamp length (excl NUL)
#endif
#ifndef PROTOCORE_MTC_VAL_MAX
#define PROTOCORE_MTC_VAL_MAX 32 // max stored observation value length (excl NUL)
#endif

/**
 * @brief The bytes an MTConnect document runs out of: the running context and the observation ring.
 *
 * One retained observation is its four strings, its sequence and its category, and the slack covers
 * the padding the compiler puts between them plus the context in front of the ring. The exact layout
 * is mtconnect.c's, and that translation unit includes both and proves this covers it - the same
 * arrangement PROTOCORE_SSH_SLOT_BYTES has with the SSH offsets.
 */
#ifndef PROTOCORE_MTCONNECT_BORROW
#define PROTOCORE_MTCONNECT_BORROW                                                                                     \
    ((size_t)PROTOCORE_MTC_SAMPLE_BUFFER *                                                                             \
         (2u * (PROTOCORE_MTC_STR_MAX + 1u) + (PROTOCORE_MTC_TS_MAX + 1u) + (PROTOCORE_MTC_VAL_MAX + 1u) + 16u) +      \
     128u)
#endif

// The agent is a gated module, so a build without it reserves nothing for the ring.
#if PROTOCORE_ENABLE_MTCONNECT
#define PROTOCORE_PLAINTEXT_WORK_MTCONNECT PROTOCORE_MTCONNECT_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_MTCONNECT 0
#endif

/**
 * @brief Largest G-code block (one line) the DNC decoder reassembles (PROTOCORE_ENABLE_DNC).
 *
 * A block longer than this overflows the decoder's fixed line buffer and is dropped whole
 * (::DNC_EV_OVERFLOW) rather than truncated. Sized for a normal G-code line; raise it only for
 * unusually long blocks (many parameters). Zero heap - this is the static per-decoder buffer.
 */
#ifndef PROTOCORE_DNC_LINE_MAX
#define PROTOCORE_DNC_LINE_MAX 128
#endif

/**
 * @brief Default leader/trailer runout length for the DNC encoder (PROTOCORE_ENABLE_DNC).
 *
 * The number of NUL runout bytes ::protocore_dnc_encode_leader emits before the program (and can emit after
 * it). The reader skips them until the first `%`. Traditional tape leaders were a few inches of
 * blank feed; 32 bytes is a serial-link equivalent. Overridable per call via DncCfg::leader_len.
 */
#ifndef PROTOCORE_DNC_LEADER_LEN
#define PROTOCORE_DNC_LEADER_LEN 32
#endif

/**
 * @brief Safety cap on how many times the DNC stream engine polls the reverse channel while paused
 *        by an XOFF, before giving up with an I/O error (PROTOCORE_ENABLE_DNC).
 *
 * `dnc_stream` pauses on XOFF and polls `recv` for the XON that resumes it; a well-behaved transport
 * paces `recv` (blocks briefly when idle) so this cap is only a backstop against a `recv` that spins
 * returning no data forever. Raise it if a slow controller legitimately holds XOFF for a long time.
 */
#ifndef PROTOCORE_DNC_XOFF_MAX_POLLS
#define PROTOCORE_DNC_XOFF_MAX_POLLS 200000
#endif

/** @brief Max concurrent address:port -> bus rules (server/net/iface_bridge). */
#ifndef PROTOCORE_BRIDGE_MAX_RULES
#define PROTOCORE_BRIDGE_MAX_RULES 8
#endif

/**
 * @brief Max write / read payload (bytes) per TRANSACTION frame (server/net/iface_bridge).
 *
 * Bounds the per-transaction stack scratch used to clock an SPI/I2C write-then-read, and rejects a frame
 * whose write_len or read_len exceeds it. Device-server transactions are small register accesses, so the
 * default is modest; a frame over the cap closes the connection (protocol error). Keep it comfortably
 * under the transport RX ring so a whole frame can buffer before it is parsed.
 */
#ifndef PROTOCORE_BRIDGE_TXN_MAX
#define PROTOCORE_BRIDGE_TXN_MAX 256
#endif

/** @brief STREAM (UART) pipe chunk size (bytes) for server/net/iface_bridge - one socket<->UART hop. */
#ifndef PROTOCORE_BRIDGE_STREAM_CHUNK
#define PROTOCORE_BRIDGE_STREAM_CHUNK 256
#endif

/** @brief Chunks a STREAM target moves per poll before yielding, bounding the UART drain loop. */
#ifndef PROTOCORE_BRIDGE_MAX_DRAIN
#define PROTOCORE_BRIDGE_MAX_DRAIN 8
#endif

/** @brief UART TRANSACTION read window (ms): how long a write-then-read waits for the read_len reply. */
#ifndef PROTOCORE_BRIDGE_UART_TXN_MS
#define PROTOCORE_BRIDGE_UART_TXN_MS 50
#endif

/** @brief Max concurrent rover connections a caster serves corrections to (services/timing_position/gnss). */
#ifndef PROTOCORE_NTRIP_MAX_ROVERS
#define PROTOCORE_NTRIP_MAX_ROVERS 4
#endif

// The base surveys in from the receiver's GGA fixes, so the NTRIP caster needs the NMEA 0183 codec.
#define PROTOCORE_ENABLE_NTRIP_CASTER_NEEDS_NMEA0183 PROTOCORE_ENABLE_NMEA0183
#if PROTOCORE_ENABLE_NTRIP_CASTER && !PROTOCORE_ENABLE_NTRIP_CASTER_NEEDS_NMEA0183
#error "ProtoCore: PROTOCORE_ENABLE_NTRIP_CASTER needs PROTOCORE_ENABLE_NMEA0183"
#endif

/** @brief Max length (incl. NUL) of an NTRIP mountpoint name the caster serves. */
#ifndef PROTOCORE_NTRIP_MOUNT_MAX
#define PROTOCORE_NTRIP_MOUNT_MAX 32
#endif

/** @brief Max NTRIP client request size (bytes) the caster buffers while reading the request headers. */
#ifndef PROTOCORE_NTRIP_REQ_MAX
#define PROTOCORE_NTRIP_REQ_MAX 512
#endif

/** @brief Max distinct mountpoints a single caster serves (each = one RTCM stream). */
#ifndef PROTOCORE_NTRIP_MAX_MOUNTS
#define PROTOCORE_NTRIP_MAX_MOUNTS 2
#endif

/**
 * @brief Per-direction relay buffer size (bytes) for server/net/relay (PROTOCORE_ENABLE_RELAY).
 *
 * Each active relay holds two buffers of this size (one per direction) for bytes read from one peer
 * but not yet accepted by the other (backpressure carry). Larger buffers raise throughput per step
 * (fewer cross-thread Tcp.conn->send marshals per KB) at the cost of RAM per concurrent relay
 * (2 * PROTOCORE_RELAY_BUF * PROTOCORE_RELAY_MAX_CONNS bytes).
 */
#ifndef PROTOCORE_RELAY_BUF
#define PROTOCORE_RELAY_BUF 2048
#endif

/**
 * @brief Max protocore_relay_step passes per poll for the relay listener (PROTOCORE_ENABLE_RELAY).
 *
 * One poll drains up to this many PROTOCORE_RELAY_BUF chunks per direction, so a single event forwards the
 * whole buffered origin RX ring (PROTOCORE_CLIENT_RX_BUF) instead of one chunk - the difference between a
 * ~0.4 Mbps and a multi-Mbps port-forward. Bounded so one busy bridge cannot starve the others.
 */
#ifndef PROTOCORE_RELAY_DRAIN_MAX
#define PROTOCORE_RELAY_DRAIN_MAX 8
#endif

/**
 * @brief Max published relay ports (bind table size) for the relay listener (PROTOCORE_ENABLE_RELAY).
 *
 * Each protocore_relay_publish() call binds one listener port to one origin `host:port`. This caps how
 * many distinct ports the device can front at once.
 */
#ifndef PROTOCORE_RELAY_MAX_PUBLISH
#define PROTOCORE_RELAY_MAX_PUBLISH 4
#endif

/**
 * @brief Max concurrent relayed connections (bridge table size) for the relay listener
 *        (PROTOCORE_ENABLE_RELAY). Each holds a protocore_relay (two PROTOCORE_RELAY_BUF buffers) + an origin slot.
 */
#ifndef PROTOCORE_RELAY_MAX_CONNS
#define PROTOCORE_RELAY_MAX_CONNS 4
#endif

/** @brief Max origin hostname length (bytes, incl. NUL) stored per published relay port. */
#ifndef PROTOCORE_RELAY_HOST_MAX
#define PROTOCORE_RELAY_HOST_MAX 64
#endif

/** @brief Blocking connect timeout (ms) when the relay listener dials the origin on a new inbound. */
#ifndef PROTOCORE_RELAY_CONNECT_MS
#define PROTOCORE_RELAY_CONNECT_MS 5000
#endif

/**
 * @brief Suggested FTP control-command buffer size (PROTOCORE_ENABLE_FTP).
 *
 * A convenience cap for callers sizing the buffer they hand `protocore_ftp_build_command`; the builders
 * are all length-checked against the caller's `cap`, so this is only a sensible default. Large
 * enough for a RETR / STOR with a long path.
 */
#ifndef PROTOCORE_FTP_CMD_MAX
#define PROTOCORE_FTP_CMD_MAX 256
#endif

/**
 * @brief Control-reply accumulator for the FTP session driver (PROTOCORE_ENABLE_FTP_SESSION).
 *
 * services/ftp_session buffers a whole control reply here before parsing it. Multiline greetings
 * and FEAT listings are the large cases; a reply that will not fit is treated as malformed rather
 * than waited on forever.
 */
#ifndef PROTOCORE_FTP_REPLY_BUF
#define PROTOCORE_FTP_REPLY_BUF 512
#endif

/** @brief Bytes staged per data-channel write when the session driver streams a payload. */
#ifndef PROTOCORE_FTP_CHUNK
#define PROTOCORE_FTP_CHUNK 512
#endif

/** @brief Per-step timeout for the FTP session driver: connect, and each control reply. */
#ifndef PROTOCORE_FTP_TIMEOUT_MS
#define PROTOCORE_FTP_TIMEOUT_MS 8000
#endif

/** @brief Worst-case serialized L2 entry (edge_sd_serialize). */
#define PROTOCORE_EDGE_SD_VALUE_MAX                                                                                    \
    (1 /*version*/ + 2 /*status*/ + 2 /*body_len*/ + 7u * 2u /*str lengths*/ + PROTOCORE_EDGE_KEY_MAX +                \
     PROTOCORE_EDGE_CTYPE_MAX + PROTOCORE_EDGE_ETAG_MAX + PROTOCORE_EDGE_LASTMOD_MAX + PROTOCORE_EDGE_CENC_MAX +       \
     PROTOCORE_EDGE_VARY_MAX + PROTOCORE_EDGE_VARY_MAX + PROTOCORE_EDGE_BODY_MAX)

/** @brief Fixed timing trailer prepended to a mesh entry frame (age propagation). */
#define PROTOCORE_EDGE_MESH_TRAILER (8 /*date*/ + 8 /*expires*/ + 4 /*lifetime_s*/ + 4 /*age_hdr*/ + 4 /*age*/)

/** @brief Worst-case mesh entry frame (trailer + a full serialized entry). */
#define PROTOCORE_EDGE_MESH_ENTRY_MAX (PROTOCORE_EDGE_MESH_TRAILER + PROTOCORE_EDGE_SD_VALUE_MAX)

/** @brief Worst-case mesh response frame (header + entry on a HIT). */
#define PROTOCORE_EDGE_MESH_RESP_MAX (2 + 1 + 1 + 2 + PROTOCORE_EDGE_MESH_ENTRY_MAX)

/** @brief Worst-case mesh request frame (bounded request-header snapshot for Vary matching). */
#define PROTOCORE_EDGE_MESH_REQ_MAX (2 + 1 + 1 + 32 + 2 + PROTOCORE_EDGE_KEY_MAX + 2 + PROTOCORE_MESH_HDRS_MAX)

/* A fetch slot reuses its origin buffer for the mesh query (the two phases never overlap),
 * so with the mesh on the buffer must also hold a mesh response. Deriving the floor here is
 * what makes PROTOCORE_ENABLE_EDGE_MESH work out of the box instead of failing to compile. */
#if PROTOCORE_ENABLE_EDGE_MESH
#define PROTOCORE_EDGE_FETCH_BUF_MIN ((PROTOCORE_EDGE_MESH_RESP_MAX) > 2560 ? (PROTOCORE_EDGE_MESH_RESP_MAX) : 2560)
#else
#define PROTOCORE_EDGE_FETCH_BUF_MIN 2560
#endif

// PROTOCORE_EDGE_CACHE_SLOTS and PROTOCORE_EDGE_BODY_MAX come from vendor/board_profiles/ (classic floor, raised
// per chip/PSRAM by board_profile.h above); override with -D as usual.
#ifndef PROTOCORE_EDGE_KEY_MAX
#define PROTOCORE_EDGE_KEY_MAX 128 // largest canonical cache key (method\nhost\npath[\nquery])
#endif
#ifndef PROTOCORE_EDGE_VARY_MAX
#define PROTOCORE_EDGE_VARY_MAX 64 // stored Vary field-name list / captured request values (each)
#endif

/** @brief Stored Content-Type to replay. */
#ifndef PROTOCORE_EDGE_CTYPE_MAX
#define PROTOCORE_EDGE_CTYPE_MAX 64
#endif

/** @brief Stored validator (ETag, quotes included). */
#ifndef PROTOCORE_EDGE_ETAG_MAX
#define PROTOCORE_EDGE_ETAG_MAX 64
#endif

/** @brief Stored Last-Modified (RFC 1123 date). */
#ifndef PROTOCORE_EDGE_LASTMOD_MAX
#define PROTOCORE_EDGE_LASTMOD_MAX 40
#endif

/** @brief Stored Content-Encoding to replay (e.g. gzip). */
#ifndef PROTOCORE_EDGE_CENC_MAX
#define PROTOCORE_EDGE_CENC_MAX 32
#endif
#ifndef PROTOCORE_EDGE_MAP_MAX
#define PROTOCORE_EDGE_MAP_MAX 4 // path-prefix -> origin route mappings
#endif
#ifndef PROTOCORE_EDGE_ORIGIN_URL_MAX
#define PROTOCORE_EDGE_ORIGIN_URL_MAX 128 // largest origin base URL in a route mapping
#endif
// PROTOCORE_EDGE_FETCH_SLOTS comes from vendor/board_profiles/ (classic floor, raised per chip/PSRAM).
#ifndef PROTOCORE_EDGE_FETCH_BUF
#define PROTOCORE_EDGE_FETCH_BUF PROTOCORE_EDGE_FETCH_BUF_MIN // per-fetch origin-response accumulation buffer
#endif
#ifndef PROTOCORE_EDGE_FETCH_TIMEOUT_MS
#define PROTOCORE_EDGE_FETCH_TIMEOUT_MS 8000 // origin fetch deadline before fail-open
#endif
#ifndef PROTOCORE_EDGE_DEFAULT_TTL_S
#define PROTOCORE_EDGE_DEFAULT_TTL_S 60 // fallback freshness when no directive and no wall clock
#endif
// L2 (SD) tier: when PROTOCORE_ENABLE_DBM is also set, the edge cache spills evicted entries to a dbm store
// (edge_cache_sd) so the cached set survives a reboot. Each entry serializes to ~ its body plus ~470 B of
// response metadata; for a full-body spill, PROTOCORE_DBM_VAL_MAX must be >= that size (>= PROTOCORE_EDGE_BODY_MAX
// + ~470). Entries that do not fit simply stay L1-only, so a small PROTOCORE_DBM_VAL_MAX is safe but persists
// less. The L2 key is the 32-byte cache-key digest, so PROTOCORE_DBM_KEY_MAX must be >= 32 (its default).

/**
 * @brief SMB2 client work-buffer size (bytes) for smb_client's request/response framing.
 *
 * Two buffers of this size live on the stack during a call, plus a few half-size scratch buffers for
 * the NTLM auth tokens, so the engine needs roughly 4x this in stack. 1024 covers the NEGOTIATE ->
 * SESSION_SETUP -> TREE_CONNECT -> CREATE handshake; raise it if a server's SPNEGO/target-info token
 * or your share path is unusually large.
 */
#ifndef PROTOCORE_SMB_BUF
#define PROTOCORE_SMB_BUF 1024
#endif

/**
 * @brief Chunk the core-dump image is streamed out of flash in (PROTOCORE_EXC_COREDUMP_CHUNK).
 *
 * protocore_exc_coredump_save() copies the partition to a file this many bytes at a time from a stack
 * buffer, so a dump of any size costs no heap and never has to fit RAM.
 */
#ifndef PROTOCORE_EXC_COREDUMP_CHUNK
#define PROTOCORE_EXC_COREDUMP_CHUNK 512
#endif

/**
 * @brief Most asset paths a service-worker precache manifest may list (PROTOCORE_DELIVERY_PRECACHE_MAX).
 */
#ifndef PROTOCORE_DELIVERY_PRECACHE_MAX
#define PROTOCORE_DELIVERY_PRECACHE_MAX 16
#endif

/**
 * @brief Buffer the precache manifest JSON is built into (PROTOCORE_DELIVERY_MANIFEST_BUF).
 *
 * Must hold `{"version":"..","precache":[..]}` for PROTOCORE_DELIVERY_PRECACHE_MAX paths; the manifest
 * route answers 500 rather than truncating if it does not fit.
 */
#ifndef PROTOCORE_DELIVERY_MANIFEST_BUF
#define PROTOCORE_DELIVERY_MANIFEST_BUF 512
#endif

/**
 * @brief Channels tracked by the WiFi sniffer's per-channel survey (PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS).
 *
 * 14 covers the full 2.4 GHz channel plan (1-14); lower it to the channels actually swept to shrink
 * the survey table (each entry is 11 bytes).
 */
#ifndef PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS
#define PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS 14
#endif

/** @brief I2C address of the FDC2214, set by the ADDR pin: 0x2A when it is low, 0x2B when it is high. */
#ifndef PROTOCORE_FDC2214_I2C_ADDR
#define PROTOCORE_FDC2214_I2C_ADDR 0x2A
#endif

/** @brief I2C address of the LDC1614, set by the ADDR pin: 0x2A when it is low, 0x2B when it is high. */
#ifndef PROTOCORE_LDC1614_I2C_ADDR
#define PROTOCORE_LDC1614_I2C_ADDR 0x2A
#endif

/** @brief I2C address of the VL53L0X. DS11555 gives the device address as 0x52, which is the 8-bit
 *         form with the R/W bit in it; on a 7-bit API that is 0x29. */
#ifndef PROTOCORE_VL53L0X_I2C_ADDR
#define PROTOCORE_VL53L0X_I2C_ADDR 0x29
#endif

/** @brief Number of log lines retained in the ring. */
#ifndef PROTOCORE_LOG_LINES
#define PROTOCORE_LOG_LINES 32
#endif

/** @brief Maximum length of one stored log line (bytes, including null). */
#ifndef PROTOCORE_LOG_LINE_LEN
#define PROTOCORE_LOG_LINE_LEN 96
#endif

/**
 * @brief Compile-time severity floor for the PROTOCORE_LOG* macros (shared/log/log.h).
 *
 * The values are ordered low -> high and match protocore_log_level's, so a level is usable both in the
 * preprocessor (which cannot see a constexpr) and as the runtime argument. PROTOCORE_NONE sits above ERROR
 * so that the default discards everything.
 */
#define PROTOCORE_LOG_LEVEL_DEBUG 0
#define PROTOCORE_LOG_LEVEL_INFO 1
#define PROTOCORE_LOG_LEVEL_WARN 2
#define PROTOCORE_LOG_LEVEL_ERROR 3
#define PROTOCORE_LOG_LEVEL_NONE 4

/**
 * @brief Lowest severity the PROTOCORE_LOG* macros emit code for.
 *
 * A call below this floor is discarded by the preprocessor: no call, no formatting, and no format
 * string left in flash, because the discarded form only names its arguments inside `sizeof` - an
 * unevaluated context that still type-checks them. So instrumentation can be left in the source
 * permanently and costs exactly nothing in a build that does not want it, which is the point.
 *
 * Defaults to PROTOCORE_NONE: opt in per build (e.g. -DPROTOCORE_LOG_LEVEL=PROTOCORE_LOG_LEVEL_WARN).
 */
#ifndef PROTOCORE_LOG_LEVEL
#define PROTOCORE_LOG_LEVEL PROTOCORE_LOG_LEVEL_NONE
#endif

/** @brief Confirm window (ms): a pending image not confirmed within this rolls back. */
#ifndef PROTOCORE_OTA_CONFIRM_WINDOW_MS
#define PROTOCORE_OTA_CONFIRM_WINDOW_MS 30000
#endif

/** @brief WiFi modem-sleep mode: 0 = none (max perf), 1 = min modem, 2 = max modem. */
#ifndef PROTOCORE_RADIO_WIFI_PS
#define PROTOCORE_RADIO_WIFI_PS 0
#endif

/** @brief Max TX power cap in dBm (2..20); 0 = leave the platform default. */
#ifndef PROTOCORE_RADIO_MAX_TX_DBM
#define PROTOCORE_RADIO_MAX_TX_DBM 0
#endif

/** @brief DNS resolve timeout in milliseconds. */
/**
 * @brief Nameserver the portable resolver asks when nothing has told it otherwise.
 *
 * The vendor backend takes its servers from the stack (DHCP), so this is the portable one's only
 * starting point. A device that learns a server from DHCP or provisioning should hand it over with
 * Resolver.server.ip + Resolver.set_server rather than query this one.
 */
#ifndef PROTOCORE_DNS_SERVER
#define PROTOCORE_DNS_SERVER "9.9.9.9"
#endif

/** @brief Local UDP port the portable resolver asks from and hears the answer on. */
#ifndef PROTOCORE_DNS_CLIENT_PORT
#define PROTOCORE_DNS_CLIENT_PORT 1153
#endif

#ifndef PROTOCORE_DNS_TIMEOUT_MS
#define PROTOCORE_DNS_TIMEOUT_MS 5000
#endif

// Ring depth and per-record message length are tunable in audit_log.h
// (PROTOCORE_AUDIT_LOG_ENTRIES, PROTOCORE_AUDIT_MSG_LEN); define them before include to
// override. The RAM cost is roughly PROTOCORE_AUDIT_LOG_ENTRIES * (PROTOCORE_AUDIT_MSG_LEN
// + 41) bytes.

/** @brief Max accepted OIDC ID-token length (also sizes the Authorization buffer). */
#ifndef PROTOCORE_OIDC_MAX_LEN
#define PROTOCORE_OIDC_MAX_LEN 1600
#endif

/** @brief NamespaceIndex the umati MachineTool nodes live at (default 1). */
#ifndef PROTOCORE_UMATI_NS
#define PROTOCORE_UMATI_NS 1
#endif

/** @brief NamespaceIndex the robotics MotionDeviceSystem nodes live at (default 1). */
#ifndef PROTOCORE_ROBOTICS_NS
#define PROTOCORE_ROBOTICS_NS 1
#endif

/** @brief Number of Axes the robotics MotionDevice exposes (default 6; must fit PROTOCORE_OPCUA_REF_MAX). */
#ifndef PROTOCORE_ROBOTICS_AXES
#define PROTOCORE_ROBOTICS_AXES 6
#endif

/** @brief NamespaceIndex the EUROMAP 77 IMM_MES_Interface nodes live at (default 1). */
#ifndef PROTOCORE_EM77_NS
#define PROTOCORE_EM77_NS 1
#endif

// The RX-ring feature floors (streaming needs a full TCP window, SSH/TLS a full first flight) are
// resolved by derived_sizing.h, included at the end of this file once every feature
// flag is known - that is the sizing layer's job, not this file's.

/** @brief Maximum formatted syslog datagram length in bytes (RFC 5424 line). */
#ifndef PROTOCORE_SYSLOG_MSG_MAX
#define PROTOCORE_SYSLOG_MSG_MAX 256
#endif

/** @brief Maximum syslog HOSTNAME / APP-NAME field length (including NUL). */
#ifndef PROTOCORE_SYSLOG_FIELD_MAX
#define PROTOCORE_SYSLOG_FIELD_MAX 32
#endif

/** @brief Default syslog collector UDP port (RFC 5426 well-known 514; overridable at runtime
 *  via Syslog.collector.port + Syslog.init and here for a non-standard collector). */
#ifndef PROTOCORE_SYSLOG_DEFAULT_PORT
#define PROTOCORE_SYSLOG_DEFAULT_PORT 514
#endif

/** @brief Maximum accepted JWT length in bytes (header.payload.signature). */
#ifndef PROTOCORE_JWT_MAX_LEN
#define PROTOCORE_JWT_MAX_LEN 512
#endif

/** @brief Receive buffer (and max response size) for the outbound HTTP client, bytes. */
#ifndef PROTOCORE_HTTP_CLIENT_BUF_SIZE
#define PROTOCORE_HTTP_CLIENT_BUF_SIZE 2048
#endif

/**
 * @brief Ciphertext receive-ring size for the https:// client, bytes.
 *
 * The lwIP recv callback feeds TLS wire bytes into this draining ring while the
 * TLS engine pulls and decrypts them, so it holds only the in-flight (not yet
 * decrypted) ciphertext: a multi-KB handshake flight fits without loss thanks to
 * the refuse-and-redeliver backpressure. Must exceed one TCP segment (TCP_MSS,
 * ~1460) or a full segment could never fit. Only used when
 * PROTOCORE_ENABLE_HTTP_CLIENT_TLS is set.
 */
#ifndef PROTOCORE_HTTP_CLIENT_CT_BUF_SIZE
#define PROTOCORE_HTTP_CLIENT_CT_BUF_SIZE 4096
#endif

/** @brief Outbound HTTP client connect/response timeout in milliseconds. */
#ifndef PROTOCORE_HTTP_CLIENT_TIMEOUT_MS
#define PROTOCORE_HTTP_CLIENT_TIMEOUT_MS 8000
#endif

/** @brief Max length of one SMTP command / address line (bytes, incl. CRLF). */
#ifndef PROTOCORE_SMTP_LINE_MAX
#define PROTOCORE_SMTP_LINE_MAX 256
#endif

/** @brief Max size of the assembled DATA payload (headers + dot-stuffed body), bytes. */
#ifndef PROTOCORE_SMTP_MSG_MAX
#define PROTOCORE_SMTP_MSG_MAX 2048
#endif

/** @brief Max size of one (possibly multi-line) server reply held while parsing, bytes. */
#ifndef PROTOCORE_SMTP_REPLY_MAX
#define PROTOCORE_SMTP_REPLY_MAX 512
#endif

/** @brief SMTP connect / per-reply timeout in milliseconds. */
#ifndef PROTOCORE_SMTP_TIMEOUT_MS
#define PROTOCORE_SMTP_TIMEOUT_MS 10000
#endif

/** @brief Ciphertext receive-ring size for SMTPS, bytes (only used when the message is TLS). */
#ifndef PROTOCORE_SMTP_CT_BUF_SIZE
#define PROTOCORE_SMTP_CT_BUF_SIZE 4096
#endif

/**
 * @brief MQTT packet buffer size in bytes (bounds one outgoing/incoming packet).
 *
 * The client borrows twice this from the secure pool's persistent end and splits it: one half is
 * the payload the codec assembles into and the receive reassembly, the other is the wire. Must hold
 * the largest CONNECT/PUBLISH the client sends and the largest incoming PUBLISH it accepts
 * (topic + payload + a few header bytes); larger incoming packets are dropped.
 */
#ifndef PROTOCORE_MQTT_BUF_SIZE
#define PROTOCORE_MQTT_BUF_SIZE 1024
#endif

/**
 * @brief What the whole MQTT connect is given, in milliseconds.
 *
 * Covers the transport coming up, the TLS handshake for mqtts, and the CONNACK: Mqtt.connect
 * returns immediately and Mqtt.loop gives the Network Connection up once this passes. One budget
 * rather than one per stage, because a Server slow in any of them is slow to the caller either way.
 */
#ifndef PROTOCORE_MQTT_CONNECT_MS
#define PROTOCORE_MQTT_CONNECT_MS 8000
#endif

/** @brief Default MQTT keep-alive interval in seconds (PINGREQ cadence / CONNECT field). */
#ifndef PROTOCORE_MQTT_KEEPALIVE_S
#define PROTOCORE_MQTT_KEEPALIVE_S 30
#endif

/** @brief Ciphertext receive-ring size for MQTTS (draining ring; must exceed one TCP_MSS). */
#ifndef PROTOCORE_MQTT_CT_BUF_SIZE
#define PROTOCORE_MQTT_CT_BUF_SIZE 4096
#endif

/** @brief Maximum inbound MQTT topic length (including NUL) delivered to the callback. */
#ifndef PROTOCORE_MQTT_MAX_TOPIC
#define PROTOCORE_MQTT_MAX_TOPIC 128
#endif

/**
 * @brief Outbound QoS 1/2 in-flight slots (unacknowledged exchanges awaiting their acknowledgement).
 *
 * A slot records the packet identifier, how far the exchange has got and when it was last sent - not
 * the packet, which stays in the client's wire buffer where a retransmit marks DUP and rewinds the
 * worker to the start of it. A publish is refused when all slots are busy. Each slot costs a
 * handful of bytes.
 */
#ifndef PROTOCORE_MQTT_MAX_INFLIGHT
#define PROTOCORE_MQTT_MAX_INFLIGHT 4
#endif

/** @brief Retransmit timeout (ms) for an unacknowledged in-flight QoS 1/2 message. */
#ifndef PROTOCORE_MQTT_RETRANSMIT_MS
#define PROTOCORE_MQTT_RETRANSMIT_MS 5000
#endif

/** @brief Inbound QoS 2 packet-id de-duplication ring depth (PUBREC-acknowledged, awaiting PUBREL). */
#ifndef PROTOCORE_MQTT_RX_QOS2_SLOTS
#define PROTOCORE_MQTT_RX_QOS2_SLOTS 8
#endif

/** @brief WebSocket client send/receive buffer size in bytes (bounds one frame). */
#ifndef PROTOCORE_WS_CLIENT_BUF_SIZE
#define PROTOCORE_WS_CLIENT_BUF_SIZE 1024
#endif

/** @brief Ciphertext receive-ring size for wss:// (draining ring; must exceed one TCP_MSS). */
#ifndef PROTOCORE_WS_CLIENT_CT_BUF_SIZE
#define PROTOCORE_WS_CLIENT_CT_BUF_SIZE 4096
#endif

// Everything that dials out goes through the one TCP client rather than carrying a private
// copy of the connect-and-drain pattern, so each of these needs it compiled in. This was an
// OR-list that set PROTOCORE_NEED_CLIENT, and a feature missing from the list got a stub
// whose open() returns -1 - a build that compiled, linked, and never connected.

#define PROTOCORE_ENABLE_HTTP_CLIENT_NEEDS_TCP_CLIENT PROTOCORE_ENABLE_TCP_CLIENT
#if PROTOCORE_ENABLE_HTTP_CLIENT && !PROTOCORE_ENABLE_HTTP_CLIENT_NEEDS_TCP_CLIENT
#error "ProtoCore: PROTOCORE_ENABLE_HTTP_CLIENT needs PROTOCORE_ENABLE_TCP_CLIENT"
#endif

#define PROTOCORE_ENABLE_MQTT_NEEDS_TCP_CLIENT PROTOCORE_ENABLE_TCP_CLIENT
#if PROTOCORE_ENABLE_MQTT && !PROTOCORE_ENABLE_MQTT_NEEDS_TCP_CLIENT
#error "ProtoCore: PROTOCORE_ENABLE_MQTT needs PROTOCORE_ENABLE_TCP_CLIENT"
#endif

#define PROTOCORE_ENABLE_WS_CLIENT_NEEDS_TCP_CLIENT PROTOCORE_ENABLE_TCP_CLIENT
#if PROTOCORE_ENABLE_WS_CLIENT && !PROTOCORE_ENABLE_WS_CLIENT_NEEDS_TCP_CLIENT
#error "ProtoCore: PROTOCORE_ENABLE_WS_CLIENT needs PROTOCORE_ENABLE_TCP_CLIENT"
#endif

#define PROTOCORE_ENABLE_RELAY_NEEDS_TCP_CLIENT PROTOCORE_ENABLE_TCP_CLIENT
#if PROTOCORE_ENABLE_RELAY && !PROTOCORE_ENABLE_RELAY_NEEDS_TCP_CLIENT
#error "ProtoCore: PROTOCORE_ENABLE_RELAY needs PROTOCORE_ENABLE_TCP_CLIENT"
#endif

#define PROTOCORE_ENABLE_SMTP_NEEDS_TCP_CLIENT PROTOCORE_ENABLE_TCP_CLIENT
#if PROTOCORE_ENABLE_SMTP && !PROTOCORE_ENABLE_SMTP_NEEDS_TCP_CLIENT
#error "ProtoCore: PROTOCORE_ENABLE_SMTP needs PROTOCORE_ENABLE_TCP_CLIENT"
#endif

#define PROTOCORE_ENABLE_SMB_NEEDS_TCP_CLIENT PROTOCORE_ENABLE_TCP_CLIENT
#if PROTOCORE_ENABLE_SMB && !PROTOCORE_ENABLE_SMB_NEEDS_TCP_CLIENT
#error "ProtoCore: PROTOCORE_ENABLE_SMB needs PROTOCORE_ENABLE_TCP_CLIENT"
#endif

#define PROTOCORE_ENABLE_DNC_NEEDS_TCP_CLIENT PROTOCORE_ENABLE_TCP_CLIENT
#if PROTOCORE_ENABLE_DNC && !PROTOCORE_ENABLE_DNC_NEEDS_TCP_CLIENT
#error "ProtoCore: PROTOCORE_ENABLE_DNC needs PROTOCORE_ENABLE_TCP_CLIENT"
#endif

#define PROTOCORE_ENABLE_FTP_SESSION_NEEDS_TCP_CLIENT PROTOCORE_ENABLE_TCP_CLIENT
#if PROTOCORE_ENABLE_FTP_SESSION && !PROTOCORE_ENABLE_FTP_SESSION_NEEDS_TCP_CLIENT
#error "ProtoCore: PROTOCORE_ENABLE_FTP_SESSION needs PROTOCORE_ENABLE_TCP_CLIENT"
#endif

#define PROTOCORE_ENABLE_SSH_CLIENT_NEEDS_TCP_CLIENT PROTOCORE_ENABLE_TCP_CLIENT
#if PROTOCORE_ENABLE_SSH_CLIENT && !PROTOCORE_ENABLE_SSH_CLIENT_NEEDS_TCP_CLIENT
#error "ProtoCore: PROTOCORE_ENABLE_SSH_CLIENT needs PROTOCORE_ENABLE_TCP_CLIENT"
#endif

// The client dials by name, so it needs the resolver: one owner of the hostname marshal
// instead of a private copy per client.

// SSH port forwarding dials the forwarded destination, so it is on the same list. Spelled without
// the ENABLE_ infix, which is why it is stated here rather than generated with the rest.
#define PROTOCORE_SSH_PORT_FORWARD_NEEDS_TCP_CLIENT PROTOCORE_ENABLE_TCP_CLIENT
#if PROTOCORE_SSH_PORT_FORWARD && !PROTOCORE_SSH_PORT_FORWARD_NEEDS_TCP_CLIENT
#error "ProtoCore: PROTOCORE_SSH_PORT_FORWARD needs PROTOCORE_ENABLE_TCP_CLIENT"
#endif

#define PROTOCORE_ENABLE_TCP_CLIENT_NEEDS_DNS_RESOLVER PROTOCORE_ENABLE_DNS_RESOLVER
#if PROTOCORE_ENABLE_TCP_CLIENT && !PROTOCORE_ENABLE_TCP_CLIENT_NEEDS_DNS_RESOLVER
#error "ProtoCore: PROTOCORE_ENABLE_TCP_CLIENT needs PROTOCORE_ENABLE_DNS_RESOLVER"
#endif

// The datagram transport's rings only move when Session.tick() drains them, so a feature that
// binds a UDP port or sends a datagram needs the transport in the image. This was an OR-list
// that set PROTOCORE_NEED_UDP, and a feature missing from it filled its rings and stopped,
// with nothing on the wire.

#define PROTOCORE_ENABLE_COAP_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_COAP && !PROTOCORE_ENABLE_COAP_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_COAP needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_DTLS_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_DTLS && !PROTOCORE_ENABLE_DTLS_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_DTLS needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_STATSD_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_STATSD && !PROTOCORE_ENABLE_STATSD_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_STATSD needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_UDP_TELEMETRY_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_UDP_TELEMETRY && !PROTOCORE_ENABLE_UDP_TELEMETRY_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_UDP_TELEMETRY needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_SNMP_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_SNMP && !PROTOCORE_ENABLE_SNMP_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_SNMP needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_SNMP_TRAP_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_SNMP_TRAP && !PROTOCORE_ENABLE_SNMP_TRAP_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_SNMP_TRAP needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_SNMP_V3_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_SNMP_V3 && !PROTOCORE_ENABLE_SNMP_V3_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_SNMP_V3 needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_SYSLOG_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_SYSLOG && !PROTOCORE_ENABLE_SYSLOG_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_SYSLOG needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_FLOW_EXPORT_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_FLOW_EXPORT && !PROTOCORE_ENABLE_FLOW_EXPORT_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_FLOW_EXPORT needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_PROVISIONING_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_PROVISIONING && !PROTOCORE_ENABLE_PROVISIONING_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_PROVISIONING needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_NTP_SERVER_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_NTP_SERVER && !PROTOCORE_ENABLE_NTP_SERVER_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_NTP_SERVER needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_DNS_SERVER_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_DNS_SERVER && !PROTOCORE_ENABLE_DNS_SERVER_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_DNS_SERVER needs PROTOCORE_ENABLE_UDP"
#endif

#define PROTOCORE_ENABLE_HTTP3_NEEDS_UDP PROTOCORE_ENABLE_UDP
#if PROTOCORE_ENABLE_HTTP3 && !PROTOCORE_ENABLE_HTTP3_NEEDS_UDP
#error "ProtoCore: PROTOCORE_ENABLE_HTTP3 needs PROTOCORE_ENABLE_UDP"
#endif

// ---------------------------------------------------------------------------
// Full Authorization-header capture (internal)
// ---------------------------------------------------------------------------
// Digest auth and JWT bearer tokens both carry an Authorization value far longer
// than MAX_VAL_LEN, so the parser captures the whole header into a dedicated
// per-request buffer (HttpReq::authorization) when either feature is enabled.

/** @brief True when the parser must capture the full Authorization header value. */
#if PROTOCORE_ENABLE_AUTH || PROTOCORE_ENABLE_JWT || PROTOCORE_ENABLE_OIDC
#define PROTOCORE_CAPTURE_AUTH_HEADER 1
#else
#define PROTOCORE_CAPTURE_AUTH_HEADER 0
#endif

/**
 * @brief Capacity of HttpReq::authorization (full Authorization header value).
 *
 * Sized to the largest enabled consumer: a Digest header (DIGEST_AUTH_HDR_MAX), a
 * `Bearer <jwt>` HS256 token (PROTOCORE_JWT_MAX_LEN), or a `Bearer <id_token>` OIDC
 * RS256 token (PROTOCORE_OIDC_MAX_LEN), each plus the scheme.
 */
#if PROTOCORE_ENABLE_OIDC
#define PROTOCORE_AUTH_HDR_CAP_OIDC (PROTOCORE_OIDC_MAX_LEN + 16)
#else
#define PROTOCORE_AUTH_HDR_CAP_OIDC 0
#endif
#if PROTOCORE_ENABLE_JWT
#define PROTOCORE_AUTH_HDR_CAP_JWT (PROTOCORE_JWT_MAX_LEN + 16)
#else
#define PROTOCORE_AUTH_HDR_CAP_JWT 0
#endif
#define PROTOCORE_AUTH_HDR_CAP_M1                                                                                      \
    (PROTOCORE_AUTH_HDR_CAP_JWT > DIGEST_AUTH_HDR_MAX ? PROTOCORE_AUTH_HDR_CAP_JWT : DIGEST_AUTH_HDR_MAX)
#define PROTOCORE_AUTH_HDR_CAP                                                                                         \
    (PROTOCORE_AUTH_HDR_CAP_OIDC > PROTOCORE_AUTH_HDR_CAP_M1 ? PROTOCORE_AUTH_HDR_CAP_OIDC : PROTOCORE_AUTH_HDR_CAP_M1)

/**
 * @brief Stack scratch for protocore_web_terminal_println() line building.
 *
 * One formatted terminal line must fit in this many bytes (longer is truncated).
 * Allocated on the stack only during the call - no persistent RAM cost.
 */
#ifndef TERM_TX_BUF_SIZE
#define TERM_TX_BUF_SIZE 256
#endif

/**
 * @brief Maximum requests served on one keep-alive connection before it is closed.
 *
 * A fairness bound so a single client cannot hold a connection slot
 * indefinitely with a steady request stream. After this many responses the
 * server emits `Connection: close` and drops the link; the client simply
 * reconnects. Only meaningful when PROTOCORE_ENABLE_KEEPALIVE is set.
 */
#ifndef PROTOCORE_KEEPALIVE_MAX_REQUESTS
#define PROTOCORE_KEEPALIVE_MAX_REQUESTS 100
#endif

/**
 * @brief Per-connection HPACK dynamic-table size in bytes (our decoder; advertised to the peer
 * as SETTINGS_HEADER_TABLE_SIZE). RFC 7541's default is 4096; lower it to save per-connection
 * RAM (each active HTTP/2 connection holds one table).
 */
#ifndef PROTOCORE_HPACK_TABLE_BYTES
#define PROTOCORE_HPACK_TABLE_BYTES 4096
#endif

/** @brief Max HPACK dynamic-table entries (>= PROTOCORE_HPACK_TABLE_BYTES / 32, the min entry size). */
#ifndef PROTOCORE_HPACK_MAX_ENTRIES
#define PROTOCORE_HPACK_MAX_ENTRIES 128
#endif

/**
 * @brief Largest HTTP/2 frame we accept, in bytes (advertised as SETTINGS_MAX_FRAME_SIZE). RFC
 * 9113 requires accepting at least 16384; a whole frame is buffered for reassembly, so this
 * (plus the HPACK table) sets the per-HTTP/2-connection RAM. Range: [16384, 16777215].
 */
#ifndef PROTOCORE_H2_MAX_FRAME
#define PROTOCORE_H2_MAX_FRAME 16384
#endif

/** @brief Max concurrent HTTP/2 streams per connection (advertised as MAX_CONCURRENT_STREAMS). */
#ifndef PROTOCORE_H2_MAX_STREAMS
#define PROTOCORE_H2_MAX_STREAMS 8
#endif

/**
 * @brief Header-block reassembly buffer for HTTP/2 requests that span HEADERS + CONTINUATION
 * frames (a single END_HEADERS frame decodes in place and needs no copy). Caps the compressed
 * request-header size; a larger block is rejected (RFC 9113 sec 6.10).
 */
#ifndef PROTOCORE_H2_HDR_BLOCK
#define PROTOCORE_H2_HDR_BLOCK 4096
#endif

/**
 * @brief CONTINUATION frames one header block may span (RFC 9113 sec 6.10).
 *
 * PROTOCORE_H2_HDR_BLOCK bounds the bytes a block may carry, but an empty CONTINUATION adds no bytes, so
 * a peer can send them without end and never reach that bound. This bounds the frame count as
 * well, which is what makes the block terminate.
 */
#ifndef PROTOCORE_H2_MAX_CONTINUATION
#define PROTOCORE_H2_MAX_CONTINUATION 8
#endif

/**
 * @brief Largest datagram a DTLS handshake flight will put on the wire, before a connection
 * overrides it (RFC 9147 sec 4.3).
 *
 * A handshake message longer than this is split across fragments that each fit one datagram. The
 * default is the IPv6 minimum MTU less the worst-case IPv6 and UDP headers, which no path is
 * allowed to be smaller than; a connection that knows its own path sets DtlsServerConfig::pmtu.
 */
#ifndef PROTOCORE_DTLS_PMTU_DEFAULT
#define PROTOCORE_DTLS_PMTU_DEFAULT 1232
#endif

/**
 * @brief Place the HTTP/2 connection-engine pool in external PSRAM (ESP32).
 *
 * Each HTTP/2 connection needs a ~28 KB engine, so the pool (MAX_CONNS of them) does not fit the
 * ~122 KB internal DRAM alongside a TLS server - HTTP/2 therefore requires PSRAM. Set this to 1
 * on a PSRAM board (S3 / P4 / WROVER) to move the pool to external RAM via `EXT_RAM_BSS_ATTR`.
 * Like PROTOCORE_TLS_ARENA_IN_PSRAM it needs a framework built with
 * `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` (the stock arduino-esp32 core ships it off; see
 * tools/psram/README.md). A compile-time guard rejects PROTOCORE_ENABLE_HTTP2 without this on ARDUINO.
 */
#ifndef PROTOCORE_H2_POOL_IN_PSRAM
#define PROTOCORE_H2_POOL_IN_PSRAM 0
#endif

// Internal request-dispatch slots appended to the connection pool for non-TCP transports.
// HTTP/3 runs over QUIC/UDP and has no accept-time TCP slot, but it reuses the same request
// pipeline (match_and_execute + send), which is indexed by a connection-pool slot. One reserved
// slot at index MAX_CONNS lets an HTTP/3 request run through that pipeline. The TCP accept path only
// ever scans [0, MAX_CONNS), and this slot is driven synchronously by the HTTP/3 poll on the worker
// thread, so there is no accept race. CONN_POOL_SLOTS sizes conn_pool / http_pool / the per-slot
// response-header buffer; every TCP loop still bounds itself with MAX_CONNS.
#if PROTOCORE_ENABLE_HTTP3
#define PROTOCORE_INTERNAL_SLOTS 1
#define PROTOCORE_H3_DISPATCH_SLOT MAX_CONNS ///< reserved conn-pool slot an HTTP/3 request dispatches through
#else
#define PROTOCORE_INTERNAL_SLOTS 0
#endif
#define CONN_POOL_SLOTS (MAX_CONNS + PROTOCORE_INTERNAL_SLOTS)

/** @brief UDP port the HTTP/3 (QUIC) server binds by default (used by protocore_h3_cert). */
#ifndef PROTOCORE_HTTP3_PORT
#define PROTOCORE_HTTP3_PORT 443
#endif

/**
 * @brief Maximum bytes of one QUIC/TLS handshake CRYPTO flight (RFC 9001).
 *
 * The server's second flight - EncryptedExtensions + Certificate + CertificateVerify + Finished -
 * is assembled whole before it is fragmented into CRYPTO frames across Handshake packets. The
 * Certificate (a DER X.509 chain) dominates the size, so this bounds the certificate the server can
 * present. The default fits a single Ed25519 leaf certificate comfortably; raise it for a chain.
 */
#ifndef PROTOCORE_H3_CRYPTO_BUF
#define PROTOCORE_H3_CRYPTO_BUF 2048
#endif

/**
 * @brief Maximum concurrent request streams per HTTP/3 connection.
 *
 * Bounds the per-connection QUIC stream table (client-initiated bidirectional request streams plus
 * the handful of unidirectional control / QPACK streams). Each slot is small; 8 matches the HTTP/2
 * default (PROTOCORE_H2_MAX_STREAMS).
 */
#ifndef PROTOCORE_H3_MAX_STREAMS
#define PROTOCORE_H3_MAX_STREAMS 8
#endif

/** @brief Simultaneous HTTP/3 connections. Each is a QuicConn plus an H3Conn. */
#ifndef PROTOCORE_QUIC_MAX_CONNS
#define PROTOCORE_QUIC_MAX_CONNS 2
#endif

/** @brief Datagrams buffered from the lwIP thread until the server poll drains them. */
#ifndef PROTOCORE_QUIC_INGEST_RING
#define PROTOCORE_QUIC_INGEST_RING 8
#endif

/** @brief Largest UDP payload the QUIC transport sends or accepts (conservative, under a 1500 MTU). */
#ifndef PROTOCORE_QUIC_MAX_DATAGRAM
#define PROTOCORE_QUIC_MAX_DATAGRAM 1350
#endif

// What one HTTP/3 request stream holds: the frames it reassembles and the three pseudo-headers it
// captures out of them. Every one is a power of two, so a stream reaches its own bytes with a shift.
#ifndef PROTOCORE_H3_STREAM_BUF
#define PROTOCORE_H3_STREAM_BUF 2048 ///< per-request-stream reassembly buffer (HEADERS + DATA)
#endif
#ifndef PROTOCORE_H3_PATH_LEN
#define PROTOCORE_H3_PATH_LEN 256 ///< captured :path length cap
#endif
#ifndef PROTOCORE_H3_AUTHORITY_LEN
#define PROTOCORE_H3_AUTHORITY_LEN 128 ///< captured :authority length cap
#endif
#ifndef PROTOCORE_H3_METHOD_LEN
#define PROTOCORE_H3_METHOD_LEN 16 ///< captured :method length cap
#endif
// What QPACK decodes a field section through, and what a response field section is encoded into.
// Both are taken from the plaintext pool's transient end while one call runs.
#ifndef PROTOCORE_H3_QPACK_SCRATCH
#define PROTOCORE_H3_QPACK_SCRATCH 512
#endif
#ifndef PROTOCORE_H3_QPACK_BLOCK
#define PROTOCORE_H3_QPACK_BLOCK 256
#endif
// What the QUIC transport under HTTP/3 holds per connection: the bytes owed to each stream, and the
// in-order CRYPTO window per packet-number space. Both are powers of two, so a stream and a space
// reach their own bytes with a shift.
#ifndef PROTOCORE_QUIC_STREAM_TX
#define PROTOCORE_QUIC_STREAM_TX 2048 ///< per-stream outbound buffer (drained into STREAM frames)
#endif
#ifndef PROTOCORE_QUIC_CRYPTO_RX
#define PROTOCORE_QUIC_CRYPTO_RX 2048 ///< per-level inbound CRYPTO reassembly window (ClientHello, Finished)
#endif
#ifndef PROTOCORE_QUIC_MAX_STREAMS
#define PROTOCORE_QUIC_MAX_STREAMS PROTOCORE_H3_MAX_STREAMS ///< tracked streams (request + control/QPACK)
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
#ifndef PROTOCORE_ENFORCE_HOST_HEADER
#define PROTOCORE_ENFORCE_HOST_HEADER 1
#endif

/**
 * @brief Allow SSH password authentication (default on).
 *
 * Set to 0 to harden the SSH server to publickey-only authentication
 * (RFC 4252 §7): the "password" method is then refused outright and is not
 * advertised in the USERAUTH_FAILURE method list. Publickey auth is always
 * available regardless of this flag.
 */
#ifndef PROTOCORE_SSH_ALLOW_PASSWORD
#define PROTOCORE_SSH_ALLOW_PASSWORD 1
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

// Minimum spacing between password-change attempts (RFC 4252 sec 8). A change runs the caller's
// storage write, so a request inside this window is answered as a failure (busy) rather than run.
#ifndef PROTOCORE_SSH_PW_CHANGE_COOLDOWN_MS
#define PROTOCORE_SSH_PW_CHANGE_COOLDOWN_MS 60000u
#endif

/**
 * @brief Where the SSH RSA host private key is stored: the NVS namespace and the item in it.
 *
 * The server reads a DER-encoded PKCS#1/PKCS#8 blob from here at startup. Provisioning writes it
 * once per device (docs/SSH.md). Both names are within the 15-character NVS limit.
 */
#ifndef PROTOCORE_SSH_HOST_KEY_NS
#define PROTOCORE_SSH_HOST_KEY_NS "ssh_host_key"
#endif
#ifndef PROTOCORE_SSH_HOST_KEY_ITEM
#define PROTOCORE_SSH_HOST_KEY_ITEM "priv_der"
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
#ifndef PROTOCORE_MAX_UDP_LISTENERS
#define PROTOCORE_MAX_UDP_LISTENERS 2
#endif

/**
 * @brief Largest UDP datagram a bound port accepts, in bytes.
 *
 * Bounds one datagram, both directions: a longer inbound datagram is truncated to this at the
 * receive trampoline, and a longer send is refused. Sizes the per-slot staging buffer the drain
 * hands the handler. Must hold the largest datagram any UDP service expects (SNMP messages are the
 * largest user).
 */
#ifndef PROTOCORE_UDP_RX_BUF_SIZE
#define PROTOCORE_UDP_RX_BUF_SIZE 1472
#endif

/**
 * @brief Per-slot UDP receive ring, in bytes.
 *
 * Backs one bound port's receive ring. The stack's trampoline frames each datagram into it and the
 * drain reads them out, so this is how many bytes of datagram, plus a header each, may wait between
 * two poll() calls. A datagram that does not fit the free space is dropped.
 */
#ifndef PROTOCORE_UDP_RX_RING
#define PROTOCORE_UDP_RX_RING 2048
#endif

/** @brief Max accepted connections per throttle window (see PROTOCORE_ENABLE_ACCEPT_THROTTLE). */
#ifndef PROTOCORE_ACCEPT_THROTTLE_MAX
#define PROTOCORE_ACCEPT_THROTTLE_MAX 20
#endif

/** @brief Throttle window length in milliseconds (see PROTOCORE_ENABLE_ACCEPT_THROTTLE). */
#ifndef PROTOCORE_ACCEPT_THROTTLE_WINDOW_MS
#define PROTOCORE_ACCEPT_THROTTLE_WINDOW_MS 1000
#endif

/** @brief Number of source IPv4 addresses tracked by the per-IP throttle (BSS bucket table). */
#ifndef PROTOCORE_PER_IP_THROTTLE_SLOTS
#define PROTOCORE_PER_IP_THROTTLE_SLOTS 16
#endif

/** @brief Max accepted connections per window from one source IP (see PROTOCORE_ENABLE_PER_IP_THROTTLE). */
#ifndef PROTOCORE_PER_IP_THROTTLE_MAX
#define PROTOCORE_PER_IP_THROTTLE_MAX 10
#endif

/** @brief Per-IP throttle window length in milliseconds (see PROTOCORE_ENABLE_PER_IP_THROTTLE). */
#ifndef PROTOCORE_PER_IP_THROTTLE_WINDOW_MS
#define PROTOCORE_PER_IP_THROTTLE_WINDOW_MS 10000
#endif

// ---------------------------------------------------------------------------
// Source-IP allowlist  (accept-time firewall; PROTOCORE_ENABLE_IP_ALLOWLIST)
// ---------------------------------------------------------------------------

/** @brief Number of CIDR rules the source-IP allowlist can hold (BSS table). */
#ifndef PROTOCORE_IP_ALLOWLIST_SLOTS
#define PROTOCORE_IP_ALLOWLIST_SLOTS 8
#endif

// ---------------------------------------------------------------------------
// Trusted reverse-proxy forwarded-client resolution  (PROTOCORE_ENABLE_FORWARDED_TRUST)
// ---------------------------------------------------------------------------

/** @brief Number of trusted-upstream CIDR rules the forwarded-client resolver holds (BSS table). */
#ifndef PROTOCORE_TRUSTED_PROXY_MAX
#define PROTOCORE_TRUSTED_PROXY_MAX 2
#endif

// ---------------------------------------------------------------------------
// Brute-force auth lockout  (per-source-IP; PROTOCORE_ENABLE_AUTH_LOCKOUT)
// ---------------------------------------------------------------------------

/** @brief Number of source IPs the auth lockout tracks (BSS bucket table). */
#ifndef PROTOCORE_AUTH_LOCKOUT_SLOTS
#define PROTOCORE_AUTH_LOCKOUT_SLOTS 16
#endif

/** @brief Consecutive failed auths from one IP before it is locked out. */
#ifndef PROTOCORE_AUTH_LOCKOUT_THRESHOLD
#define PROTOCORE_AUTH_LOCKOUT_THRESHOLD 5
#endif

/** @brief First lockout duration in ms; doubles on each further failure. */
#ifndef PROTOCORE_AUTH_LOCKOUT_BASE_MS
#define PROTOCORE_AUTH_LOCKOUT_BASE_MS 1000
#endif

/** @brief Maximum lockout duration in ms (the exponential backoff cap). */
#ifndef PROTOCORE_AUTH_LOCKOUT_MAX_MS
#define PROTOCORE_AUTH_LOCKOUT_MAX_MS 300000
#endif

// ---------------------------------------------------------------------------
// CSRF protection  (PROTOCORE_ENABLE_CSRF)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Telnet sizing constants  (PROTOCORE_ENABLE_TELNET must be 1)
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
// SSH sizing constants  (PROTOCORE_ENABLE_SSH must be 1)
// ---------------------------------------------------------------------------

/** @brief Maximum simultaneous SSH connections. */
#ifndef MAX_SSH_CONNS
#define MAX_SSH_CONNS 1
#endif

/**
 * @brief One connection's whole span: the wire, the session, the exchange, the packet and the rx
 *        regions end to end, which ssh.c hands out one slot at a time.
 *
 * Stated here as a number because the offsets that sum to it are built in
 * network_drivers/presentation/ssh/common.h, which this file cannot see. common.h is the translation
 * unit that includes both, so it is where this is proved against the real SSH_SLOT_BORROW - the same
 * arrangement PROTOCORE_SSH_CPUB_MAX has with the PQC key sizes.
 */
#ifndef PROTOCORE_SSH_SLOT_BYTES
#define PROTOCORE_SSH_SLOT_BYTES 183616u
#endif

/** @brief Every slot's span together: the bytes ssh.c takes from the secure pool. */
#ifndef PROTOCORE_SSH_BORROW
#define PROTOCORE_SSH_BORROW ((size_t)MAX_SSH_CONNS * PROTOCORE_SSH_SLOT_BYTES)
#endif

/**
 * @brief SSH TCP port forwarding (`direct-tcpip`, i.e. `ssh -L`). Default off.
 *
 * When set, the SSH server can open an outbound TCP connection to a client-named
 * host:port and bridge bytes between that socket and the SSH channel - the
 * `ssh_forward` owner does the I/O via the outbound client transport (protocore_client),
 * so it needs `PROTOCORE_CLIENT_CONNS >= PROTOCORE_SSH_FWD_MAX` and a channel pool
 * (`PROTOCORE_SSH_MAX_CHANNELS > 1`) to be useful. Forwarding is still opt-in at
 * runtime: nothing is forwarded until the application calls `protocore_ssh_forward_begin()`.
 * Off = the channel codec refuses every `direct-tcpip` open (no open relay).
 */
#ifndef PROTOCORE_SSH_PORT_FORWARD
#define PROTOCORE_SSH_PORT_FORWARD 0
#endif

/** @brief Maximum concurrent forwarded TCP connections (must be <= PROTOCORE_CLIENT_CONNS). */
#ifndef PROTOCORE_SSH_FWD_MAX
#define PROTOCORE_SSH_FWD_MAX 2
#endif

/** @brief Maximum forward target hostname length including null terminator. */
#ifndef PROTOCORE_SSH_FWD_HOST_MAX
#define PROTOCORE_SSH_FWD_HOST_MAX 64
#endif

/** @brief Blocking connect timeout (ms) when opening a forward target. */
#ifndef PROTOCORE_SSH_FWD_CONNECT_MS
#define PROTOCORE_SSH_FWD_CONNECT_MS 3000
#endif

/** @brief Max bytes moved per forward channel per poll, target -> client (<= SSH_PKT_BUF_SIZE). */
#ifndef PROTOCORE_SSH_FWD_CHUNK
#define PROTOCORE_SSH_FWD_CHUNK 1024
#endif

/**
 * @brief Maximum concurrent remote-forward listeners (`ssh -R` / `tcpip-forward`).
 *
 * Each accepted client that requests remote forwarding can bind up to this many
 * ports on the device; each binding consumes one `listener_pool[]` slot, so
 * `MAX_LISTENERS` must have that much headroom above the app's own listeners.
 * Remote forwarding shares `PROTOCORE_SSH_PORT_FORWARD` (compiled in) and is inert
 * until `protocore_ssh_forward_begin()`.
 */
#ifndef PROTOCORE_SSH_RFWD_MAX
#define PROTOCORE_SSH_RFWD_MAX 1
#endif

/**
 * @brief Maximum concurrent bridged connections across all remote forwards.
 *
 * Each connection accepted on a forwarded port occupies one transport `conn_pool`
 * slot plus one SSH channel (so it needs `PROTOCORE_SSH_MAX_CHANNELS` headroom) and one
 * entry here while it is bridged back to the client.
 */
#ifndef PROTOCORE_SSH_RFWD_BRIDGE_MAX
#define PROTOCORE_SSH_RFWD_BRIDGE_MAX 2
#endif

/**
 * @brief Packet assembly buffer per SSH connection (bytes).
 *
 * RFC 4253 sec 6.1: every implementation MUST process an uncompressed payload of 32768 bytes and a
 * total packet of 35000. A smaller value rejects a conforming peer's legal packet.
 */
#ifndef SSH_PKT_BUF_SIZE
#define SSH_PKT_BUF_SIZE 2048
#endif

/** @brief Max concurrent open SFTP handles (files + dirs) per SSH connection. */
#ifndef PROTOCORE_SFTP_MAX_HANDLES
#define PROTOCORE_SFTP_MAX_HANDLES 4
#endif

/** @brief SFTP packet-assembly buffer per SFTP channel (bytes); bounds one non-streamed request/response. */
#ifndef PROTOCORE_SFTP_PKT_BUF
#define PROTOCORE_SFTP_PKT_BUF 2048
#endif

/**
 * @brief Largest PROTOCORE_SSH_FXP_DATA payload returned for one READ (a short read - the client re-requests). Kept
 *        within one SSH packet (SSH_PKT_BUF_SIZE minus framing), so bump SSH_PKT_BUF_SIZE too for throughput.
 */
#ifndef PROTOCORE_SFTP_MAX_READ
#define PROTOCORE_SFTP_MAX_READ 1024
#endif

/** @brief Largest absolute path the SFTP/SCP server resolves (mount root + request path). */
#ifndef PROTOCORE_FILESYSTEM_PATH_MAX
#define PROTOCORE_FILESYSTEM_PATH_MAX 256
#endif

/**
 * @brief Largest serialized SSH_FXP_NAME entry one READDIR emits: filename, `ls -l` longname and
 *        attributes. Sizes the per-handle stash an entry that did not fit is held in, which is a
 *        field of SftpHandle in session.h, so it is stated here rather than in the server's own .c.
 */
#ifndef PROTOCORE_SFTP_ENTRY_MAX
#define PROTOCORE_SFTP_ENTRY_MAX (PROTOCORE_FILESYSTEM_PATH_MAX + 320)
#endif

/**
 * @brief Place the per-connection SSH compression state in external PSRAM (ESP32).
 *
 * Like PROTOCORE_H2_POOL_IN_PSRAM / PROTOCORE_TLS_ARENA_IN_PSRAM: moves the compressor pool
 * (MAX_SSH_CONNS of them) to external RAM via `EXT_RAM_BSS_ATTR`. Needs a framework built with
 * `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` (tools/psram/README.md).
 */
#ifndef PROTOCORE_SSH_ZLIB_IN_PSRAM
#define PROTOCORE_SSH_ZLIB_IN_PSRAM 0
#endif

/**
 * @brief Acknowledge placing the SSH compressor in internal DRAM (no PSRAM).
 *
 * The per-connection compressor is ~48 KB. With MAX_SSH_CONNS=1 and no TLS server it fits internal
 * DRAM on a roomy chip (S3 / P4). Rather than force PSRAM, this mirrors PROTOCORE_TLS_ACK_MULTI_CONN_DRAM:
 * set it to 1 to consciously accept the internal-DRAM cost when PROTOCORE_SSH_ZLIB_IN_PSRAM is off. The
 * build otherwise fails fast on ARDUINO with guidance (below) instead of a raw linker overflow.
 */
#ifndef PROTOCORE_SSH_ZLIB_ACK_DRAM
#define PROTOCORE_SSH_ZLIB_ACK_DRAM 0
#endif

/**
 * @brief SSH s2c DEFLATE sliding-window size in bytes (max back-reference distance). Power of two,
 * 256..32768. Larger = better ratio + more per-connection RAM (the compressor holds a window-sized
 * work buffer + a window-sized hash chain). The client always allocates a 32 KB inflate window, so
 * any value here interoperates; 8 KB is a good ratio/RAM balance for terminal + command output.
 */
#ifndef PROTOCORE_SSH_ZLIB_WINDOW
#define PROTOCORE_SSH_ZLIB_WINDOW 8192
#endif

/**
 * @brief Largest uncompressed payload the s2c compressor accepts in one call (bytes). Outbound SSH
 * payloads are bounded by SSH_PKT_BUF_SIZE; this sizes the compressor's history+input work buffer.
 */
#ifndef PROTOCORE_SSH_ZLIB_MAX_IN
#define PROTOCORE_SSH_ZLIB_MAX_IN 2048
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
 * the sum across features. Tune from the protocore_plaintext_high_water() reading on a real
 * workload; an over-budget borrow fails closed (protocore_plaintext_alloc returns NULL).
 */
// The deepest nest is the SSH receive path with compression on: ssh_recv_ctr_emac holds
// SSH_PKT_BUF_SIZE + 64 across ssh_dispatch_payload, which holds SSH_PKT_BUF_SIZE across
// protocore_ssh_server_dispatch, which holds a SSH_PKT_BUF_SIZE reply across the switch, under which
// protocore_ssh_auth_handle_pubkey holds 2,552 - 8,760 bytes live together.
// The transient end: what a request, a response body and a codec work out of while a call runs.
#ifndef PROTOCORE_PLAINTEXT_SCRATCH
#define PROTOCORE_PLAINTEXT_SCRATCH 10240
#endif

#if PROTOCORE_ENABLE_HTTP3
#define PROTOCORE_PLAINTEXT_WORK_H3CONN                                                                                \
    (PROTOCORE_WORK_H3_CONN + PROTOCORE_WORK_QUIC_CONN + PROTOCORE_QUIC_SERVER_BORROW)
#else
#define PROTOCORE_PLAINTEXT_WORK_H3CONN 0
#endif

#if PROTOCORE_ENABLE_EDGE_CACHE
#define PROTOCORE_PLAINTEXT_WORK_EDGEPROXY PROTOCORE_EDGE_PROXY_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_EDGEPROXY 0
#endif

#if PROTOCORE_ENABLE_EUROMAP77
#define PROTOCORE_PLAINTEXT_WORK_EUROMAP77 PROTOCORE_EUROMAP77_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_EUROMAP77 0
#endif

#if PROTOCORE_ENABLE_UMATI
#define PROTOCORE_PLAINTEXT_WORK_UMATI PROTOCORE_UMATI_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_UMATI 0
#endif

#if PROTOCORE_ENABLE_ROBOTICS
#define PROTOCORE_PLAINTEXT_WORK_ROBOTICS PROTOCORE_ROBOTICS_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_ROBOTICS 0
#endif

#if PROTOCORE_ENABLE_SIMATIC
#define PROTOCORE_PLAINTEXT_WORK_SIMATIC PROTOCORE_SIMATIC_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_SIMATIC 0
#endif

#if PROTOCORE_ENABLE_J1939
#define PROTOCORE_PLAINTEXT_WORK_J1939 PROTOCORE_J1939_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_J1939 0
#endif

#if PROTOCORE_ENABLE_MODBUS
#define PROTOCORE_PLAINTEXT_WORK_MODBUS PROTOCORE_MODBUS_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_MODBUS 0
#endif

#if PROTOCORE_ENABLE_ESPNOW
#define PROTOCORE_PLAINTEXT_WORK_ESPNOW PROTOCORE_ESPNOW_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_ESPNOW 0
#endif

#if PROTOCORE_ENABLE_PROMISC
#define PROTOCORE_PLAINTEXT_WORK_PROMISC PROTOCORE_PROMISC_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_PROMISC 0
#endif

#if PROTOCORE_ENABLE_WIFI_SNIFFER
#define PROTOCORE_PLAINTEXT_WORK_WIFISNIFF PROTOCORE_WIFI_SNIFFER_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_WIFISNIFF 0
#endif

#if PROTOCORE_ENABLE_RADIO_POWER
#define PROTOCORE_PLAINTEXT_WORK_RADIOPOWER PROTOCORE_RADIO_POWER_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_RADIOPOWER 0
#endif

#if PROTOCORE_ENABLE_DNS_SERVER
#define PROTOCORE_PLAINTEXT_WORK_DNSSERVER PROTOCORE_DNS_SERVER_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_DNSSERVER 0
#endif

#if PROTOCORE_ENABLE_FORWARD
#define PROTOCORE_PLAINTEXT_WORK_FORWARD PROTOCORE_FORWARD_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_FORWARD 0
#endif

// The session layer's per-protocol handler table, one pointer per registered ProtoHandler. Measured
// at 96 bytes for the default PROTO_MAX_HANDLERS of 12, and scales with it. ProtoRegistryNs reads
// the same table through the same borrow: the registry holds nothing of its own. No key material,
// so the plaintext end.
#ifndef PROTOCORE_SESSION_BORROW
#define PROTOCORE_SESSION_BORROW ((size_t)PROTO_MAX_HANDLERS * 8u + 32u)
#endif

#define PROTOCORE_PLAINTEXT_WORK_SESSION PROTOCORE_SESSION_BORROW

// The signalling layer's link state and the counters around it. Measured at 28 bytes. No key
// material, so the plaintext end.
#ifndef PROTOCORE_SIGNALING_BORROW
#define PROTOCORE_SIGNALING_BORROW 64u
#endif

#define PROTOCORE_PLAINTEXT_WORK_SIGNALING PROTOCORE_SIGNALING_BORROW

// The trace ring one capture fills. Measured at 176 bytes. No key material, so the plaintext end.
#ifndef PROTOCORE_TRACE_CAPTURE_BORROW
#define PROTOCORE_TRACE_CAPTURE_BORROW 256u
#endif

#if PROTOCORE_ENABLE_TRACE_CAPTURE
#define PROTOCORE_PLAINTEXT_WORK_TRACECAPTURE PROTOCORE_TRACE_CAPTURE_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_TRACECAPTURE 0
#endif

// The power manager's current mode and the two flags around it. Measured at 3 bytes. No key material, so the plaintext
// end.
#ifndef PROTOCORE_POWER_MGMT_BORROW
#define PROTOCORE_POWER_MGMT_BORROW 8u
#endif

#if PROTOCORE_ENABLE_POWER_MGMT
#define PROTOCORE_PLAINTEXT_WORK_POWERMGMT PROTOCORE_POWER_MGMT_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_POWERMGMT 0
#endif

// The guardrail counters one pass trips against. Measured at 8 bytes. No key material, so the plaintext end.
#ifndef PROTOCORE_GUARDRAILS_BORROW
#define PROTOCORE_GUARDRAILS_BORROW 16u
#endif

#if PROTOCORE_ENABLE_GUARDRAILS
#define PROTOCORE_PLAINTEXT_WORK_GUARDRAILS PROTOCORE_GUARDRAILS_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_GUARDRAILS 0
#endif

// The failsafe's armed state and what it reverts to. Measured at 208 bytes. No key material, so the plaintext end.
#ifndef PROTOCORE_FAILSAFE_BORROW
#define PROTOCORE_FAILSAFE_BORROW 256u
#endif

#if PROTOCORE_ENABLE_FAILSAFE
#define PROTOCORE_PLAINTEXT_WORK_FAILSAFE PROTOCORE_FAILSAFE_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_FAILSAFE 0
#endif

// Every worker's task handle, its deferred-callback queue and that queue's storage, plus the pump
// each runs and the flag that stops them. Scales with both maxima: measured at 160 bytes for one
// worker and 1136 for eight, at the default queue depth of 8. No key material, so the plaintext end.
#ifndef PROTOCORE_WORKER_BORROW
#define PROTOCORE_WORKER_BORROW                                                                                        \
    ((size_t)PROTOCORE_WORKER_COUNT * ((size_t)PROTOCORE_DEFER_QUEUE_DEPTH * 16u + 64u) + 64u)
#endif

#if PROTOCORE_ENABLE_PREEMPT_QUEUE
#define PROTOCORE_PLAINTEXT_WORK_WORKER PROTOCORE_WORKER_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_WORKER 0
#endif

// The preemption queue's entries and its head and tail. Measured at 408 bytes. No key material, so the plaintext end.
#ifndef PROTOCORE_PREEMPT_QUEUE_BORROW
#define PROTOCORE_PREEMPT_QUEUE_BORROW 512u
#endif

#if PROTOCORE_ENABLE_PREEMPT_QUEUE
#define PROTOCORE_PLAINTEXT_WORK_PREEMPTQUEUE PROTOCORE_PREEMPT_QUEUE_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_PREEMPTQUEUE 0
#endif

// The log ring: PROTOCORE_LOG_LINES lines of PROTOCORE_LOG_LINE_LEN, their severities, and the
// trap. Measured at 3120 bytes. No key material, so the plaintext end.
#ifndef PROTOCORE_LOGBUF_BORROW
#define PROTOCORE_LOGBUF_BORROW 3584u
#endif

#if PROTOCORE_ENABLE_LOGBUF
#define PROTOCORE_PLAINTEXT_WORK_LOGBUF PROTOCORE_LOGBUF_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_LOGBUF 0
#endif

// The flow exporter's cursor into the datagram it is filling. Measured at 40 bytes. Flow records
// carry no key material, so the plaintext end.
#ifndef PROTOCORE_FLOW_EXPORT_BORROW
#define PROTOCORE_FLOW_EXPORT_BORROW 64u
#endif

#if PROTOCORE_ENABLE_FLOW_EXPORT
#define PROTOCORE_PLAINTEXT_WORK_FLOWEXPORT PROTOCORE_FLOW_EXPORT_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_FLOWEXPORT 0
#endif

// The syslog sender's collector address, facility and the one line it formats. Measured at 342
// bytes. No key material, so the plaintext end.
#ifndef PROTOCORE_SYSLOG_BORROW
#define PROTOCORE_SYSLOG_BORROW 512u
#endif

#if PROTOCORE_ENABLE_SYSLOG
#define PROTOCORE_PLAINTEXT_WORK_SYSLOG PROTOCORE_SYSLOG_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_SYSLOG 0
#endif

// The trap sender's request id and the PDU it builds. Measured at 1028 bytes. A v3 trap is signed
// and may be encrypted with the USM keys, so the secure end.
#ifndef PROTOCORE_SNMP_NOTIFY_BORROW
#define PROTOCORE_SNMP_NOTIFY_BORROW 1536u
#endif

#if PROTOCORE_ENABLE_SNMP_TRAP
#define PROTOCORE_SECURE_WORK_SNMPNOTIFY PROTOCORE_SNMP_NOTIFY_BORROW
#else
#define PROTOCORE_SECURE_WORK_SNMPNOTIFY 0
#endif

// The HTTP client's receive buffer, target and built request. Measured at 3060 bytes. A request
// line carries Authorization headers and a response its bodies, so the secure end.
#ifndef PROTOCORE_HTTP_CLIENT_BORROW
#define PROTOCORE_HTTP_CLIENT_BORROW 3584u
#endif

#if PROTOCORE_ENABLE_HTTP_CLIENT
#define PROTOCORE_SECURE_WORK_HTTPCLIENT PROTOCORE_HTTP_CLIENT_BORROW
#else
#define PROTOCORE_SECURE_WORK_HTTPCLIENT 0
#endif

// The SMTP session's command and reply lines, with the base64 AUTH client response (RFC 4954 sec
// 4). Measured at 3104 bytes. That response is a credential, so the secure end.
#ifndef PROTOCORE_SMTP_BORROW
#define PROTOCORE_SMTP_BORROW 3584u
#endif

#if PROTOCORE_ENABLE_SMTP
#define PROTOCORE_SECURE_WORK_SMTP PROTOCORE_SMTP_BORROW
#else
#define PROTOCORE_SECURE_WORK_SMTP 0
#endif

// The WebSocket client's three rings: received octets, the assembled packet and what is queued to
// send. Measured at 4144 bytes. Over wss those rings hold the cleartext, so the secure end.
#ifndef PROTOCORE_WS_CLIENT_BORROW
#define PROTOCORE_WS_CLIENT_BORROW 4608u
#endif

#if PROTOCORE_ENABLE_WS_CLIENT
#define PROTOCORE_SECURE_WORK_WSCLIENT PROTOCORE_WS_CLIENT_BORROW
#else
#define PROTOCORE_SECURE_WORK_WSCLIENT 0
#endif

// The USM engine: its engine id and boots, the user, and the auth and privacy keys derived for it
// (RFC 3414). Measured at 8680 bytes. Key material, so the secure end.
#ifndef PROTOCORE_SNMP_V3_BORROW
#define PROTOCORE_SNMP_V3_BORROW 9216u
#endif

#if PROTOCORE_ENABLE_SNMP_V3
#define PROTOCORE_SECURE_WORK_SNMPV3 PROTOCORE_SNMP_V3_BORROW
#else
#define PROTOCORE_SECURE_WORK_SNMPV3 0
#endif

// The agent's MIB table, its read and write community strings, and the varbinds one request walks.
// Measured at 12512 bytes. A community string is a credential, so the secure end.
#ifndef PROTOCORE_SNMP_AGENT_BORROW
#define PROTOCORE_SNMP_AGENT_BORROW 13312u
#endif

#if PROTOCORE_ENABLE_SNMP
#define PROTOCORE_SECURE_WORK_SNMPAGENT PROTOCORE_SNMP_AGENT_BORROW
#else
#define PROTOCORE_SECURE_WORK_SNMPAGENT 0
#endif

// The OAuth 2.0 request body and the token-endpoint reply. Measured at 3072 bytes. The body carries
// the RFC 6749 sec 2.3.1 client password and the reply the issued tokens, so the secure end. Only
// the transport calls own it, and those need PROTOCORE_ENABLE_HTTP_CLIENT.
#ifndef PROTOCORE_OAUTH2_BORROW
#define PROTOCORE_OAUTH2_BORROW 3072u
#endif

#if PROTOCORE_ENABLE_OAUTH2 && PROTOCORE_ENABLE_HTTP_CLIENT
#define PROTOCORE_SECURE_WORK_OAUTH2 PROTOCORE_OAUTH2_BORROW
#else
#define PROTOCORE_SECURE_WORK_OAUTH2 0
#endif

// The filesystem's bound roots and the two buffers a path is resolved into. Measured at 2684
// bytes. Paths and mount names, so the plaintext end.
#ifndef PROTOCORE_FILESYSTEM_BORROW
#define PROTOCORE_FILESYSTEM_BORROW 3072u
#endif

#define PROTOCORE_PLAINTEXT_WORK_FILESYSTEM PROTOCORE_FILESYSTEM_BORROW

// The southbound driver table: each driver's name, its point range and the callbacks that reach
// it. Measured at 72 bytes. Field device addresses, so the plaintext end.
#ifndef PROTOCORE_SOUTHBOUND_BORROW
#define PROTOCORE_SOUTHBOUND_BORROW 128u
#endif

#if PROTOCORE_ENABLE_SOUTHBOUND
#define PROTOCORE_PLAINTEXT_WORK_SOUTHBOUND PROTOCORE_SOUTHBOUND_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_SOUTHBOUND 0
#endif

// The SCP server's bound root, its registration flag and one control line's filename. Upload
// paths, so the plaintext end.
#ifndef PROTOCORE_SSH_SCP_BORROW
#define PROTOCORE_SSH_SCP_BORROW ((size_t)PROTOCORE_FILESYSTEM_PATH_MAX + 16u)
#endif

#if PROTOCORE_ENABLE_SSH_SCP
#define PROTOCORE_PLAINTEXT_WORK_SSHSCP PROTOCORE_SSH_SCP_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_SSHSCP 0
#endif

/**
 * @brief One SSH connection's zlib@openssh.com compressor: both streams and their windows.
 *
 * Stated here as a number because it sums a deflate window, its hash chain, the fixed Huffman
 * tables and a 32 KB inflate context-takeover window, none of which this file can see - they are
 * built in ssh/transport/comp/comp.c, which is the translation unit that includes both and is where
 * this is proved against the real SshCompCtx. Same arrangement as PROTOCORE_SSH_SLOT_BYTES.
 */
#ifndef PROTOCORE_SSH_COMP_SLOT_BYTES
#define PROTOCORE_SSH_COMP_SLOT_BYTES 81000u
#endif

/** @brief Every connection's compressor together: the bytes comp.c takes from the plaintext pool. */
#ifndef PROTOCORE_SSH_COMP_BORROW
#define PROTOCORE_SSH_COMP_BORROW ((size_t)MAX_SSH_CONNS * PROTOCORE_SSH_COMP_SLOT_BYTES)
#endif

// Compression is a negotiated extra, so the term is the borrow only where the streams are built.
#if PROTOCORE_ENABLE_SSH_ZLIB
#define PROTOCORE_PLAINTEXT_WORK_SSHCOMP PROTOCORE_SSH_COMP_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_SSHCOMP 0
#endif

// The SFTP server's bound root and registration flag, its one response-build buffer, the READ
// scratch, two request paths, one serialized READDIR entry with its longname and the entry's own
// name, and a handle on its way into a HANDLE response. Measured at 5008 bytes for the default
// SSH_PKT_BUF_SIZE, PROTOCORE_SFTP_MAX_READ and PROTOCORE_FILESYSTEM_PATH_MAX, and scales with all
// three. File bytes and paths, no key material, so the plaintext end.
#ifndef PROTOCORE_SSH_SFTP_BORROW
#define PROTOCORE_SSH_SFTP_BORROW                                                                                      \
    ((size_t)SSH_PKT_BUF_SIZE + PROTOCORE_SFTP_MAX_READ + 5u * PROTOCORE_FILESYSTEM_PATH_MAX + 656u)
#endif

#if PROTOCORE_ENABLE_SSH_SFTP
#define PROTOCORE_PLAINTEXT_WORK_SSHSFTP PROTOCORE_SSH_SFTP_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_SSHSFTP 0
#endif

// The static file server's bound accessor root. Measured at 4 bytes; the per-slot transfer state
// this pages out of is session's, not here. A handle, no key material, so the plaintext end.
#ifndef PROTOCORE_FILE_SERVING_BORROW
#define PROTOCORE_FILE_SERVING_BORROW 16u
#endif

#if PROTOCORE_ENABLE_FILE_SERVING
#define PROTOCORE_PLAINTEXT_WORK_FILESERVING PROTOCORE_FILE_SERVING_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_FILESERVING 0
#endif

// The HTTP request parser's streaming-body hooks: the three callbacks an application installs to
// take a body as it arrives. Measured at 24 bytes. Function pointers, no key material, so the
// plaintext end. The per-slot request table is http_pool[], which is not a borrow.
#ifndef PROTOCORE_HTTP_PARSER_BORROW
#define PROTOCORE_HTTP_PARSER_BORROW 32u
#endif

// The parser took a gate when every module did, so the term follows it. It defaults on, so this
// changes no build that exists - it is the arm a build that turns it off would otherwise pay for.
#if PROTOCORE_ENABLE_HTTP_PARSER
#define PROTOCORE_PLAINTEXT_WORK_HTTPPARSER PROTOCORE_HTTP_PARSER_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_HTTPPARSER 0
#endif

// The adaptive mDNS announcer's live state: its config, the beacon interval, the contention window,
// the running frame total the promiscuous sink bumps, and the channel capture is pinned to.
// Measured at 80 bytes. Beacon timing, no key material, so the plaintext end.
#ifndef PROTOCORE_MDNS_ADAPTIVE_BORROW
#define PROTOCORE_MDNS_ADAPTIVE_BORROW 96u
#endif

#if PROTOCORE_ENABLE_MDNS_ADAPTIVE
#define PROTOCORE_PLAINTEXT_WORK_MDNSADAPTIVE PROTOCORE_MDNS_ADAPTIVE_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_MDNSADAPTIVE 0
#endif

// The upload service's in-flight transfer: the sink the streamed body is handed to and the byte
// count the last one carried. Measured at 32 bytes. Upload bookkeeping, no key material, so the
// plaintext end.
#ifndef PROTOCORE_UPLOAD_SERVICE_BORROW
#define PROTOCORE_UPLOAD_SERVICE_BORROW 48u
#endif

#if PROTOCORE_ENABLE_UPLOAD
#define PROTOCORE_PLAINTEXT_WORK_UPLOADSERVICE PROTOCORE_UPLOAD_SERVICE_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_UPLOADSERVICE 0
#endif

// The SNTP client's session: the epoch the last accepted reply carried, the millisecond it arrived
// so the monotonic clock can carry it between syncs, the cookie that reply had to echo, and the
// request span held in flight. Measured at 48 bytes. A wall clock and an anti-spoof cookie, no key
// material, so the plaintext end.
#ifndef PROTOCORE_NTP_SERVICE_BORROW
#define PROTOCORE_NTP_SERVICE_BORROW 64u
#endif

#if PROTOCORE_ENABLE_NTP
#define PROTOCORE_PLAINTEXT_WORK_NTPSERVICE PROTOCORE_NTP_SERVICE_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_NTPSERVICE 0
#endif

// The NTP server's advertised stratum and reference id. Measured at 8 bytes. Clock metadata, no
// key material, so the plaintext end.
#ifndef PROTOCORE_NTP_SERVER_BORROW
#define PROTOCORE_NTP_SERVER_BORROW 16u
#endif

#if PROTOCORE_ENABLE_NTP_SERVER
#define PROTOCORE_PLAINTEXT_WORK_NTPSERVER PROTOCORE_NTP_SERVER_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_NTPSERVER 0
#endif

// The mDNS responder's advertised host name, the services and TXT pairs it answers with, and the
// UDP binding it answers on. Measured at 304 bytes. Service names, no key material, so the
// plaintext end.
#ifndef PROTOCORE_MDNS_SERVICE_BORROW
#define PROTOCORE_MDNS_SERVICE_BORROW 320u
#endif

#if PROTOCORE_ENABLE_MDNS
#define PROTOCORE_PLAINTEXT_WORK_MDNSSERVICE PROTOCORE_MDNS_SERVICE_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_MDNSSERVICE 0
#endif

// The Telnet console's per-slot NVT table, the read scratch a slot's bytes are staged in for the
// IAC walk, the command callback and the row a call is bound to. Measured at 1568 bytes for the
// default MAX_TELNET_CONNS and RX_BUF_SIZE, and scales with both. Console lines, no key material,
// so the plaintext end.
#ifndef PROTOCORE_TELNET_BORROW
#define PROTOCORE_TELNET_BORROW ((size_t)MAX_TELNET_CONNS * (TELNET_BUF_SIZE + 16u) + (size_t)RX_BUF_SIZE + 32u)
#endif

#if PROTOCORE_ENABLE_TELNET
#define PROTOCORE_PLAINTEXT_WORK_TELNET PROTOCORE_TELNET_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_TELNET 0
#endif

// The HTTP connection glue's read scratch, where a slot's available bytes are staged for the
// parser, plus the per-slot pump the application installs. Measured at 1032 bytes for the default
// RX_BUF_SIZE of 1024, and scales with it. Request bytes, not key material, so the plaintext end.
#ifndef PROTOCORE_HTTP_CONN_BORROW
#define PROTOCORE_HTTP_CONN_BORROW ((size_t)RX_BUF_SIZE + 32u)
#endif

#define PROTOCORE_PLAINTEXT_WORK_HTTPCONN PROTOCORE_HTTP_CONN_BORROW

// The HTTP surface's registered handlers: the not-found handler, and the edge-cache fetch pump when
// that capability is built. Measured at 8 bytes, 16 with PROTOCORE_ENABLE_EDGE_CACHE. Function
// pointers, no key material, so the plaintext end.
#ifndef PROTOCORE_HTTP_BORROW
#define PROTOCORE_HTTP_BORROW 32u
#endif

#define PROTOCORE_PLAINTEXT_WORK_HTTP PROTOCORE_HTTP_BORROW

// The SSH network layer's slot-to-stream map: which socket each SSH slot uses, which pool that
// handle indexes, the socket each channel bridges, and the one-time init flag. Measured at 20 bytes
// for the default MAX_SSH_CONNS of 1, 24 with PROTOCORE_SSH_MAX_CHANNELS raised to 4, and scales
// with both. Slot numbers and socket handles, no key material, so the plaintext end.
#ifndef PROTOCORE_SSH_NETWORK_BORROW
#define PROTOCORE_SSH_NETWORK_BORROW ((size_t)MAX_SSH_CONNS * (4u + 4u * PROTOCORE_SSH_MAX_CHANNELS) + 16u)
#endif

#if PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_PLAINTEXT_WORK_SSHNETWORK PROTOCORE_SSH_NETWORK_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_SSHNETWORK 0
#endif

// The SSH listening role's per-slot teardown flag, one octet per connection. Measured at 1 byte for
// the default MAX_SSH_CONNS of 1, and scales with it. No key material, so the plaintext end.
#ifndef PROTOCORE_SSH_SERVER_BORROW
#define PROTOCORE_SSH_SERVER_BORROW ((size_t)MAX_SSH_CONNS + 8u)
#endif

#if PROTOCORE_ENABLE_SSH
#define PROTOCORE_PLAINTEXT_WORK_SSHSERVER PROTOCORE_SSH_SERVER_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_SSHSERVER 0
#endif

// The RCWL-0516's debounce and hold state and the GPIO pin it samples. Measured at 28 bytes. A pin
// level, so the plaintext end.
#ifndef PROTOCORE_RCWL0516_BORROW
#define PROTOCORE_RCWL0516_BORROW 32u
#endif

#if PROTOCORE_ENABLE_RCWL0516
#define PROTOCORE_PLAINTEXT_WORK_RCWL0516 PROTOCORE_RCWL0516_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_RCWL0516 0
#endif

// The SEN0192's motion state: the debounced level, its event count and when it last went active.
// Measured at 24 bytes. A pin level and a counter, so the plaintext end.
#ifndef PROTOCORE_SEN0192_BORROW
#define PROTOCORE_SEN0192_BORROW 32u
#endif

#if PROTOCORE_ENABLE_SEN0192
#define PROTOCORE_PLAINTEXT_WORK_SEN0192 PROTOCORE_SEN0192_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_SEN0192 0
#endif

// The FTP client session: its two socket handles, the step it is on and the reply buffer it reads
// control lines into. Measured at 1320 bytes. A path and a reply line, so the plaintext end.
#ifndef PROTOCORE_FTP_SESSION_BORROW
#define PROTOCORE_FTP_SESSION_BORROW 1536u
#endif

#if PROTOCORE_ENABLE_FTP_SESSION
#define PROTOCORE_PLAINTEXT_WORK_FTPSESSION PROTOCORE_FTP_SESSION_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_FTPSESSION 0
#endif

// The StatsD client's collector address, its tag string, and the one line it formats at a time.
// Measured at 398 bytes. No key material, so the plaintext end.
#ifndef PROTOCORE_STATSD_BORROW
#define PROTOCORE_STATSD_BORROW 512u
#endif

#if PROTOCORE_ENABLE_STATSD
#define PROTOCORE_PLAINTEXT_WORK_STATSD PROTOCORE_STATSD_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_STATSD 0
#endif

// One parsed GraphQL document and the execution walking it. Measured at 4520 bytes. No key
// material, so the plaintext end.
#ifndef PROTOCORE_GRAPHQL_BORROW
#define PROTOCORE_GRAPHQL_BORROW 5120u
#endif

#if PROTOCORE_ENABLE_GRAPHQL
#define PROTOCORE_PLAINTEXT_WORK_GRAPHQL PROTOCORE_GRAPHQL_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_GRAPHQL 0
#endif

// The LwM2M TLV codec's two cursors, one per direction. Measured at 64 bytes. No key material,
// so the plaintext end.
#ifndef PROTOCORE_LWM2M_TLV_BORROW
#define PROTOCORE_LWM2M_TLV_BORROW 128u
#endif

#if PROTOCORE_ENABLE_LWM2M
#define PROTOCORE_PLAINTEXT_WORK_LWM2MTLV PROTOCORE_LWM2M_TLV_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_LWM2MTLV 0
#endif

// The CoAP server's resource table, the path and query it splits out, and its message buffers.
// Measured at 3032 bytes. No key material - DTLS keys live in coaps_server - so the plaintext end.
#ifndef PROTOCORE_COAP_BORROW
#define PROTOCORE_COAP_BORROW 3584u
#endif

#if PROTOCORE_ENABLE_COAP
#define PROTOCORE_PLAINTEXT_WORK_COAP PROTOCORE_COAP_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_COAP 0
#endif

// The DTLS CoAP server's connection pool and ingest ring, with its Ed25519 seed and cookie key.
// Measured at 33664 bytes: each slot carries a whole DtlsConn, whose ks_store is
// PROTOCORE_TLS13_KS_BORROW, so the pool tracks the key schedule's width. Key material, so the
// secure end.
#ifndef PROTOCORE_COAPS_SERVER_BORROW
#define PROTOCORE_COAPS_SERVER_BORROW 34816u
#endif

#if PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP
#define PROTOCORE_SECURE_WORK_COAPSSERVER PROTOCORE_COAPS_SERVER_BORROW
#else
#define PROTOCORE_SECURE_WORK_COAPSSERVER 0
#endif

// The MQTT session: its transport slot, keep-alive timers, inflight table and topic. Measured at
// 304 bytes. Beside the wire buffers this module already takes from the secure end, and on the
// same end because a retained topic and packet ids describe secured traffic.
#ifndef PROTOCORE_MQTT_BORROW
#define PROTOCORE_MQTT_BORROW 512u
#endif

#if PROTOCORE_ENABLE_MQTT && PROTOCORE_HAS_NET_STACK
#define PROTOCORE_SECURE_WORK_MQTT PROTOCORE_MQTT_BORROW
#else
#define PROTOCORE_SECURE_WORK_MQTT 0
#endif

// The protobuf codec's writer and reader rows, one pair per slot. Measured at 224 bytes. No key
// material, so the plaintext end.
#ifndef PROTOCORE_PROTOBUF_BORROW
#define PROTOCORE_PROTOBUF_BORROW 512u
#endif

#if PROTOCORE_ENABLE_PROTOBUF
#define PROTOCORE_PLAINTEXT_WORK_PROTOBUF PROTOCORE_PROTOBUF_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_PROTOBUF 0
#endif

// The one Sparkplug metric being encoded or decoded. Measured at 256 bytes. No key material, so
// the plaintext end.
#ifndef PROTOCORE_SPARKPLUG_BORROW
#define PROTOCORE_SPARKPLUG_BORROW 512u
#endif

#if PROTOCORE_ENABLE_SPARKPLUG
#define PROTOCORE_PLAINTEXT_WORK_SPARKPLUG PROTOCORE_SPARKPLUG_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_SPARKPLUG 0
#endif

// The UDP telemetry sender's collector address and the line it is building. Measured at 56 bytes.
// No key material, so the plaintext end.
#ifndef PROTOCORE_UDP_TELEMETRY_BORROW
#define PROTOCORE_UDP_TELEMETRY_BORROW 128u
#endif

#if PROTOCORE_ENABLE_UDP_TELEMETRY
#define PROTOCORE_PLAINTEXT_WORK_UDPTELEMETRY PROTOCORE_UDP_TELEMETRY_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_UDPTELEMETRY 0
#endif

#define PROTOCORE_PLAINTEXT_WORK_LOG PROTOCORE_LOG_BORROW

#if PROTOCORE_ENABLE_DIFFSERV
#define PROTOCORE_PLAINTEXT_WORK_DIFFSERV PROTOCORE_DIFFSERV_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_DIFFSERV 0
#endif

#define PROTOCORE_PLAINTEXT_WORK_UDPCLIENT PROTOCORE_UDP_CLIENT_BORROW

#define PROTOCORE_PLAINTEXT_WORK_UDPLISTENER PROTOCORE_UDP_LISTENER_BORROW

#define PROTOCORE_PLAINTEXT_WORK_TCPLOWER PROTOCORE_TCP_LOWER_BORROW

#define PROTOCORE_PLAINTEXT_WORK_CONNPOOL PROTOCORE_CONN_POOL_BORROW

#define PROTOCORE_PLAINTEXT_WORK_TCPLISTENER PROTOCORE_TCP_LISTENER_BORROW

#define PROTOCORE_PLAINTEXT_WORK_TCPCLIENT PROTOCORE_TCP_CLIENT_BORROW

#if PROTOCORE_ENABLE_HAPPY_EYEBALLS
#define PROTOCORE_PLAINTEXT_WORK_HAPPYEYEBALLS PROTOCORE_HAPPY_EYEBALLS_BORROW
#else
#define PROTOCORE_PLAINTEXT_WORK_HAPPYEYEBALLS 0
#endif

#define PROTOCORE_PLAINTEXT_WORK_PHYSICAL PROTOCORE_PHYSICAL_BORROW

#ifndef PROTOCORE_PLAINTEXT_ARENA_SIZE
#define PROTOCORE_PLAINTEXT_ARENA_SIZE                                                                                 \
    (PROTOCORE_PLAINTEXT_SCRATCH + PROTOCORE_PLAINTEXT_WORK_H3CONN + PROTOCORE_PLAINTEXT_WORK_EDGEPROXY +              \
     PROTOCORE_PLAINTEXT_WORK_EUROMAP77 + PROTOCORE_PLAINTEXT_WORK_UMATI + PROTOCORE_PLAINTEXT_WORK_ROBOTICS +         \
     PROTOCORE_PLAINTEXT_WORK_J1939 + PROTOCORE_PLAINTEXT_WORK_SIMATIC + PROTOCORE_PLAINTEXT_WORK_MODBUS +             \
     PROTOCORE_PLAINTEXT_WORK_FTPSESSION + PROTOCORE_PLAINTEXT_WORK_ESPNOW + PROTOCORE_PLAINTEXT_WORK_PROMISC +        \
     PROTOCORE_PLAINTEXT_WORK_WIFISNIFF + PROTOCORE_PLAINTEXT_WORK_RADIOPOWER + PROTOCORE_PLAINTEXT_WORK_DNSSERVER +   \
     PROTOCORE_PLAINTEXT_WORK_FORWARD + PROTOCORE_PLAINTEXT_WORK_DIFFSERV + PROTOCORE_PLAINTEXT_WORK_UDPCLIENT +       \
     PROTOCORE_PLAINTEXT_WORK_UDPLISTENER + PROTOCORE_PLAINTEXT_WORK_TCPLOWER + PROTOCORE_PLAINTEXT_WORK_CONNPOOL +    \
     PROTOCORE_PLAINTEXT_WORK_TCPLISTENER + PROTOCORE_PLAINTEXT_WORK_TCPCLIENT +                                       \
     PROTOCORE_PLAINTEXT_WORK_HAPPYEYEBALLS + PROTOCORE_PLAINTEXT_WORK_PHYSICAL + PROTOCORE_PLAINTEXT_WORK_LOG +       \
     PROTOCORE_PLAINTEXT_WORK_STATSD + PROTOCORE_PLAINTEXT_WORK_FILESYSTEM + PROTOCORE_PLAINTEXT_WORK_SOUTHBOUND +     \
     PROTOCORE_PLAINTEXT_WORK_SSHSCP + PROTOCORE_PLAINTEXT_WORK_RCWL0516 + PROTOCORE_PLAINTEXT_WORK_SEN0192 +          \
     PROTOCORE_PLAINTEXT_WORK_GRAPHQL + PROTOCORE_PLAINTEXT_WORK_LWM2MTLV + PROTOCORE_PLAINTEXT_WORK_COAP +            \
     PROTOCORE_PLAINTEXT_WORK_PROTOBUF + PROTOCORE_PLAINTEXT_WORK_SPARKPLUG + PROTOCORE_PLAINTEXT_WORK_UDPTELEMETRY +  \
     PROTOCORE_PLAINTEXT_WORK_FLOWEXPORT + PROTOCORE_PLAINTEXT_WORK_SYSLOG + PROTOCORE_PLAINTEXT_WORK_POWERMGMT +      \
     PROTOCORE_PLAINTEXT_WORK_GUARDRAILS + PROTOCORE_PLAINTEXT_WORK_FAILSAFE + PROTOCORE_PLAINTEXT_WORK_WORKER +       \
     PROTOCORE_PLAINTEXT_WORK_PREEMPTQUEUE + PROTOCORE_PLAINTEXT_WORK_LOGBUF + PROTOCORE_PLAINTEXT_WORK_SESSION +      \
     PROTOCORE_PLAINTEXT_WORK_SIGNALING + PROTOCORE_PLAINTEXT_WORK_TRACECAPTURE + PROTOCORE_PLAINTEXT_WORK_SSHSERVER + \
     PROTOCORE_PLAINTEXT_WORK_HTTP + PROTOCORE_PLAINTEXT_WORK_SSHNETWORK + PROTOCORE_PLAINTEXT_WORK_HTTPCONN +         \
     PROTOCORE_PLAINTEXT_WORK_TELNET + PROTOCORE_PLAINTEXT_WORK_MDNSSERVICE + PROTOCORE_PLAINTEXT_WORK_NTPSERVER +     \
     PROTOCORE_PLAINTEXT_WORK_NTPSERVICE + PROTOCORE_PLAINTEXT_WORK_UPLOADSERVICE +                                    \
     PROTOCORE_PLAINTEXT_WORK_MDNSADAPTIVE + PROTOCORE_PLAINTEXT_WORK_HTTPPARSER + PROTOCORE_PLAINTEXT_WORK_SSHSFTP +  \
     PROTOCORE_PLAINTEXT_WORK_SSHCOMP + PROTOCORE_PLAINTEXT_WORK_MTCONNECT + PROTOCORE_PLAINTEXT_WORK_FILESERVING +    \
     256)
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
#ifndef PROTOCORE_DEBUG_CHECKS
#define PROTOCORE_DEBUG_CHECKS 0
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
#ifndef PROTOCORE_RAND_RESEED_BYTES
#define PROTOCORE_RAND_RESEED_BYTES (1u << 20)
#endif

// ---------------------------------------------------------------------------
// One gate per crypto primitive
// ---------------------------------------------------------------------------
// Each module under crypto/ is wrapped in exactly one of these, so a build turns a primitive off by
// name rather than by knowing which consumer drags it in. Stated here rather than in the module's own
// header because the six derived below read the feature flags, which are all resolved above.
//
// The six that a header used to gate on a consumer keep that consumer's expression verbatim; the rest
// were compiled unconditionally before they had a gate, so they stand at 1. Every one is
// #ifndef-guarded, so -D on the command line wins.

// One IKE SA's crypto: the cookie hash, the prf+ chain, the AUTH MAC and the ECDSA / RSA signature,
// which run in sequence. The signature is the largest.
#ifndef PROTOCORE_IKE_BORROW
#define PROTOCORE_IKE_BORROW PROTOCORE_CRYPTO_BORROW_MAX
#endif

// The largest working set any single crypto operation takes. An owner that runs several in sequence
// out of one region sizes it by this rather than restating the comparison.
#ifndef PROTOCORE_CRYPTO_BORROW_MAX
#define PROTOCORE_CRYPTO_BORROW_MAX PROTOCORE_HMAC_SHA512_BORROW
#endif

// QUIC packet keys: the HKDF's bytes, then the packet key and header-protection key it expands into
// before each becomes a keyed context.
#ifndef PROTOCORE_QUIC_KEYS_BORROW
#define PROTOCORE_QUIC_KEYS_BORROW (PROTOCORE_HKDF_BORROW + 32)
#endif

// ECDSA hashes the message with SHA-256, then the software path draws its nonce from an RFC 6979
// HMAC-DRBG. The two run in sequence; the sum is the bound either way.
#ifndef PROTOCORE_ECDSA_BORROW
#define PROTOCORE_ECDSA_BORROW (PROTOCORE_SHA256_BORROW + PROTOCORE_HMAC_SHA256_BORROW)
#endif

// The same at SHA-384's width: one HMAC-SHA384, a 48-byte T(i) block, and the same 514-byte HkdfLabel
// region, whose cap is the RFC 8446 sec 7.1 field widths and not the hash.
#ifndef PROTOCORE_HKDF_SHA384_BORROW
#define PROTOCORE_HKDF_SHA384_BORROW (PROTOCORE_HMAC_SHA384_BORROW + 48 + 514)
#endif

// HKDF drives one HMAC-SHA256 and holds the T(i) block and the HkdfLabel it builds.
#ifndef PROTOCORE_HKDF_BORROW
#define PROTOCORE_HKDF_BORROW (PROTOCORE_HMAC_SHA256_BORROW + 32 + 514)
#endif

// The RFC 4253 sec 7.2 KDF runs one exchange-hash digest and accumulates the K1 || K2 chain behind
// it. The chain is bounded by SSH_KDF_MAX (128).
#ifndef PROTOCORE_SSH_KDF_BORROW
#define PROTOCORE_SSH_KDF_BORROW (PROTOCORE_SSH_KEXHASH_BORROW + 128)
#endif

// The two tables a module holds for the life of the program rather than for the life of a call.
// They take the persistent end of the arena, so they are stated here for the same reason every
// working set is: the pool is sized off what the build declares, and an undeclared borrow is one
// the pool has no room for.
#ifndef PROTOCORE_WORK_ROUTE_TABLE
#define PROTOCORE_WORK_ROUTE_TABLE (MAX_ROUTES * 104 + 16) // HttpRoute is 88 with every gated id compiled
#endif

/** @brief The route table's borrow: every entry plus the count. Proved in http_route.c. */
#ifndef PROTOCORE_HTTP_ROUTE_BORROW
#define PROTOCORE_HTTP_ROUTE_BORROW PROTOCORE_WORK_ROUTE_TABLE
#endif

// The routes are a gated module and it defaults off, so a build without it reserved the whole table
// for something it never compiled. The term is the borrow only where the table is built.
#if PROTOCORE_ENABLE_HTTP_ROUTE
#define PROTOCORE_SECURE_WORK_ROUTETABLE PROTOCORE_HTTP_ROUTE_BORROW
#else
#define PROTOCORE_SECURE_WORK_ROUTETABLE 0
#endif
/**
 * @brief The HTTP auth borrow: the credential table, then the SHA-256 bytes behind it.
 *
 * Both regions of one span. The table lasts the life of the program and the hash scratch does not,
 * but the worst case over an entry's whole chain is taken once and never exceeded, so the digest
 * nonce and the Basic check run out of the same borrow the table sits in. Proved in http/auth.c.
 */
#ifndef PROTOCORE_HTTP_AUTH_BORROW
#define PROTOCORE_HTTP_AUTH_BORROW ((size_t)PROTOCORE_WORK_AUTH_TABLE + PROTOCORE_SHA256_BORROW)
#endif

#ifndef PROTOCORE_WORK_AUTH_TABLE
#define PROTOCORE_WORK_AUTH_TABLE (MAX_ROUTES * (3 * MAX_AUTH_LEN + 8) + 32) // AuthCred is 3*MAX_AUTH_LEN + 1
#endif
// The SSH host key on the software RSA backend: the private exponent from the persistent end for the
// program's life, plus the PKCS#8 DER borrowed while protocore_ssh_rsa_load_pubkey walks it.
#ifndef PROTOCORE_WORK_SSH_HOST_KEY
#define PROTOCORE_WORK_SSH_HOST_KEY (256 + 1700 + 16) // PROTOCORE_RSA_KEY_BYTES + SSH_RSA_KEY_DER_MAX + alignment
#endif

#ifndef PROTOCORE_H2_SERVER_BORROW
#define PROTOCORE_H2_SERVER_BORROW ((size_t)MAX_CONNS * 16 + 64) // one pointer + one mask per slot
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
// │ WEBSOCKET (PROTOCORE_ENABLE_WEBSOCKET=1)                                                        │          │
// │  ws_pool[MAX_WS_CONNS]       │ MAX_WS_CONNS × (WS_FRAME_SIZE + 29)                         │  1 082 B │
// ├──────────────────────────────┼──────────────────────────────────────────────────────────────┼──────────┤
// │ SSE (PROTOCORE_ENABLE_SSE=1)     │                                                              │          │
// │  protocore_sse_pool[MAX_SSE_CONNS]     │ MAX_SSE_CONNS × (MAX_PATH_LEN + 3)                          │    134 B │
// ├──────────────────────────────┼──────────────────────────────────────────────────────────────┼──────────┤
// │ SSH (PROTOCORE_ENABLE_SSH=1)     │                                                              │          │
// │  ssh_pool[MAX_SSH_CONNS]     │ MAX_SSH_CONNS × (SSH_PKT_BUF_SIZE + 22)                     │  2 070 B │
// │  ssh_keys[MAX_SSH_CONNS]     │ MAX_SSH_CONNS × sizeof(SshKeyMat)                            │   1187 B │
// │   └─ SshKeyMat (all builds)  │   2×aes_key[32] + 2×aes_iv[16] + 2×mac_key[64]               │          │
// │                              │   + 2×chacha_key[64] + 3 flags = 355 B, plus the two keyed   │          │
// │                              │   GCM contexts 2×PROTOCORE_AESGCM_BORROW (832 B on a vendor AEAD).    │          │
// │                              │   The contexts buy ~9,200 cycles per packet - a FIXED cost   │          │
// │                              │   that dominates small interactive traffic (see aesgcm.h).   │          │
// │                              │   CTR still rebuilds its schedule in scratch per packet.     │          │
// │  ssh_dh[MAX_SSH_CONNS]       │ MAX_SSH_CONNS × (3×protocore_bignum[256] + H[32] + 1)              │    801 B │
// │  crypto_work[]               │ PROTOCORE_CRYPTO_WORK_SIZE (scratch, wiped after each use)         │  2 144 B │
// │  SSH SUBTOTAL                │                                                              │  5 370 B │
// ├──────────────────────────────┼──────────────────────────────────────────────────────────────┼──────────┤
// │ GRAND TOTAL (all features)   │                                                              │ ≈18 KB   │
// └──────────────────────────────┴──────────────────────────────────────────────────────────────┴──────────┘
//
// ESP32 has 320 KB of SRAM; the library uses ~5–18 KB depending on features.
// Stack usage is separate; the largest frame is during SSH DH key exchange
// (~256 B for the protocore_bignum private scalar on the call stack before it is
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
//   - crypto_work[] is zeroed via protocore_secure_wipe() after every use so that
//     bignum intermediates (including partial products that contain key
//     material) do not persist in memory.

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
    PROTO_NONE = 0,     ///< Unassigned slot.
    PROTO_HTTP = 1,     ///< HTTP/1.1 with optional WS and SSE upgrades.
    PROTO_TELNET = 2,   ///< Telnet (RFC 854).
    PROTO_SSH = 3,      ///< SSH (RFC 4253/4252/4254).
    PROTO_MODBUS = 4,   ///< Modbus TCP slave (Modbus Application Protocol).
    PROTO_OPCUA = 5,    ///< OPC UA Binary (UA-TCP) server.
    PROTO_SSH_RFWD = 6, ///< SSH remote-forward listener (ssh -R): accepts bridge to a forwarded-tcpip channel.
    PROTO_RELAY = 7,    ///< TCP relay / DNAT (PROTOCORE_ENABLE_RELAY): bridge to an origin protocore_client connection.
    PROTO_BRIDGE = 8,   ///< address:port -> hardware bus (PROTOCORE_ENABLE_IFACE_BRIDGE): UART/SPI/I2C device server.
    PROTO_NTRIP_CASTER = 9, ///< NTRIP caster (PROTOCORE_ENABLE_NTRIP_CASTER): serves RTCM3 corrections to rovers.
    PROTO_MESH =
        10,         ///< Edge-cache sibling link (PROTOCORE_ENABLE_EDGE_MESH): answers a peer's content-addressed query.
    PROTO_UDP = 11, ///< A bound datagram port. The slot carries the peer per entry, not per slot.
} ProtoConn;

/**
 * @brief What an interface is, and the filter that selects one.
 *
 * One vocabulary for both jobs. A registered interface carries its kind (layer 1 keeps the
 * registry); a route or a connection carries the same value as a filter, where PROTOCORE_IF_ANY means
 * "no filter". The wifi/eth values are what a connection is stamped with at accept time by
 * comparing its local IP to the softAP IP; a bus or radio interface is registered by the
 * application and forwarded to like any other.
 */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_IF_ANY = 0,      ///< unspecified kind, and the filter that matches any interface
    PROTOCORE_IF_WIFI_STA = 1, ///< station interface (joined to an AP / your LAN)
    PROTOCORE_IF_WIFI_AP = 2,  ///< softAP interface (clients joined to the device)
    PROTOCORE_IF_ETH = 3,      ///< wired Ethernet PHY
    PROTOCORE_IF_BUS = 4,      ///< a bus bridged onto the network (uart, spi, can)
    PROTOCORE_IF_RADIO = 5,    ///< a non-wifi radio
} protocore_if_kind;

// --- feature dependency guards (centralized; see the BUILD-FLAG DEPENDENCY TREE
//     near the top of this file). A child feature requires its parent(s). ---

/** @brief Number of simultaneous outbound client connections (BSS pool size). */
#ifndef PROTOCORE_CLIENT_CONNS
#if PROTOCORE_ENABLE_SSH_CLIENT
// The reverse-SSH tunnel holds the relay connection plus one local bridge per forwarded channel, so
// the pool must cover 1 + PROTOCORE_SSH_CLIENT_MAX_CHANNELS or channels past the pool fail to bridge.
#define PROTOCORE_CLIENT_CONNS (1 + PROTOCORE_SSH_CLIENT_MAX_CHANNELS)
#else
#define PROTOCORE_CLIENT_CONNS 2
#endif
#endif

/**
 * @brief Per-connection wire receive ring size (bytes).
 *
 * Holds plaintext (plain) or ciphertext (TLS). The transport ACKs on consume
 * (TcpClient.read reopens the window), so for a large inbound transfer to never
 * stall the ring must hold a full TCP receive window: keep PROTOCORE_CLIENT_RX_BUF >=
 * TCP_WND (~5.7 KB). The 8192 default clears that and a multi-KB TLS handshake
 * flight; a ring below TCP_WND can deadlock a sustained download (the peer would be
 * allowed to send more than the ring holds). Must exceed one TCP segment (TCP_MSS).
 */
#ifndef PROTOCORE_CLIENT_RX_BUF
#define PROTOCORE_CLIENT_RX_BUF 8192
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
 * Two bounds govern one key and the tighter one binds: RFC 4253 sec 9 gives a gigabyte of
 * transmitted data, RFC 4344 sec 3.2 gives 2^32 blocks, which at 16 bytes a block is 64 GiB. So the
 * gigabyte is what to divide by a packet. A wire packet is the payload buffer plus the compressor's
 * worst case (an eighth) plus framing and the largest MAC tag, which is under twice the buffer, so
 * twice the buffer is the packet size to divide by. Both are powers of two and so is the quotient:
 * the sequence-number check is a compare against a shift, never a divide. 2^30 / (2 * BUF) is
 * written as 2^29 / BUF. Far below SSH_SEQ_CLOSE_THRESHOLD, so a re-key always precedes the wrap
 * that would repeat the CTR keystream.
 */
#ifndef SSH_REKEY_PACKET_THRESHOLD
#define SSH_REKEY_PACKET_THRESHOLD (0x20000000u / SSH_PKT_BUF_SIZE)
#endif
/**
 * @brief Elapsed-time re-key trigger in milliseconds (RFC 4253 §9: "after each hour"). Default 1 hour.
 *
 * A server-initiated re-key fires when either this much time or SSH_REKEY_PACKET_THRESHOLD packets have
 * passed since the last KEX, whichever comes first. Set to 0 to disable the time trigger (packet-count
 * only). Measured with the pluggable clock (protocore_millis()).
 */
#ifndef SSH_REKEY_TIME_MS
#define SSH_REKEY_TIME_MS 3600000u
#endif
/**
 * @brief How long a connection may stay unauthenticated before it is disconnected, milliseconds.
 *
 * RFC 4252 sec 4: "The server SHOULD have a timeout for authentication and disconnect if the
 * authentication has not been accepted within the timeout period. The RECOMMENDED timeout period is
 * 10 minutes." Set to 0 to disable. Measured with the pluggable clock (protocore_millis()).
 */
#ifndef SSH_AUTH_TIMEOUT_MS
#define SSH_AUTH_TIMEOUT_MS 600000u
#endif
/** @brief Max stored user name (RFC 4252 imposes no limit; we cap for BSS). */
#ifndef SSH_AUTH_USER_MAX
#define SSH_AUTH_USER_MAX 32
#endif
/**
 * @brief Max stored TERM value from a pty-req (RFC 4254 sec 6.2).
 *
 * TERM is an environment variable's value, so the RFC sets no bound; a longer one is truncated
 * rather than refused. "vt100", "xterm", "xterm-256color" and "screen-256color" all fit.
 */
#ifndef PROTOCORE_SSH_PTY_TERM_MAX
#define PROTOCORE_SSH_PTY_TERM_MAX 24
#endif
/** @brief Max stored password length. */
#ifndef SSH_AUTH_PASS_MAX
#define SSH_AUTH_PASS_MAX 64
#endif
/** @brief Max stored public-key algorithm name ("rsa-sha2-512", "ecdsa-sha2-nistp256", RFC 4253 sec 6.6). */
#ifndef SSH_AUTH_ALGO_MAX
#define SSH_AUTH_ALGO_MAX 20
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

#if PROTOCORE_ENABLE_AUDIT_LOG
// -- Audit log (server/security/audit_log) --
#ifndef PROTOCORE_AUDIT_LOG_ENTRIES
#define PROTOCORE_AUDIT_LOG_ENTRIES 32 ///< RAM ring depth (records retained for query/verify).
#endif
#ifndef PROTOCORE_AUDIT_MSG_LEN
#define PROTOCORE_AUDIT_MSG_LEN 48 ///< Max message bytes per record (truncated to fit).
#endif
/**
 * @brief Octets of each record's chain hash: the SHA-256 digest width.
 *
 * Not a knob. Each record hashes SHA-256(prev_hash || seq || ts || category || msg_len || msg), so
 * the width is the digest's and any other value breaks the chain a verify walks.
 */
#define PROTOCORE_AUDIT_HASH_LEN 32
#endif // PROTOCORE_ENABLE_AUDIT_LOG

#if PROTOCORE_ENABLE_DEVICENET
// -- DeviceNet (services/fieldbus/devicenet) --
#ifndef PROTOCORE_DEVICENET_MSG_MAX
#define PROTOCORE_DEVICENET_MSG_MAX 256 ///< max reassembled fragmented message
#endif
#endif // PROTOCORE_ENABLE_DEVICENET

#if PROTOCORE_ENABLE_ESPNOW
// -- ESP-NOW (services/radio/espnow) --
#ifndef PROTOCORE_ESPNOW_MAX_PEERS
#define PROTOCORE_ESPNOW_MAX_PEERS 8 ///< Bounded peer registry size.
#endif
#endif // PROTOCORE_ENABLE_ESPNOW

#if PROTOCORE_ENABLE_GRAPHQL
// -- GraphQL (services/iot/graphql) --
#ifndef PROTOCORE_GQL_MAX_NODES
#define PROTOCORE_GQL_MAX_NODES 48 ///< Max fields across the whole query.
#endif
#ifndef PROTOCORE_GQL_MAX_ARGS
#define PROTOCORE_GQL_MAX_ARGS 24 ///< Max arguments across the whole query.
#endif
#ifndef PROTOCORE_GQL_MAX_DEPTH
#define PROTOCORE_GQL_MAX_DEPTH 6 ///< Max selection-set nesting depth.
#endif
#ifndef PROTOCORE_GQL_NAME_MAX
#define PROTOCORE_GQL_NAME_MAX 32 ///< Max field / argument name length.
#endif
#ifndef PROTOCORE_GQL_PATH_MAX
#define PROTOCORE_GQL_PATH_MAX 96 ///< Max dotted path length passed to the resolver.
#endif
#ifndef PROTOCORE_GQL_STRBUF
#define PROTOCORE_GQL_STRBUF 256 ///< Pool for decoded string-argument bytes.
#endif
#endif // PROTOCORE_ENABLE_GRAPHQL

#if PROTOCORE_ENABLE_J1939
// -- J1939 (services/j1939; also built when NMEA 2000 is enabled) --
#ifndef PROTOCORE_J1939_TP_MAX
#define PROTOCORE_J1939_TP_MAX 256 ///< max reassembled TP message (spec allows up to 1785); sized down for RAM
#endif
#endif // PROTOCORE_ENABLE_J1939

#if PROTOCORE_ENABLE_NMEA0183
// -- NMEA 0183 (services/timing_position/nmea0183) --
#ifndef PROTOCORE_NMEA0183_MAX_FIELDS
#define PROTOCORE_NMEA0183_MAX_FIELDS 26 ///< max comma-separated fields (incl. the address field)
#endif
#endif // PROTOCORE_ENABLE_NMEA0183

#if PROTOCORE_ENABLE_UBX
// -- UBX (services/timing_position/ubx) --
#ifndef PROTOCORE_UBX_MAX_PAYLOAD
#define PROTOCORE_UBX_MAX_PAYLOAD                                                                                      \
    256 ///< max UBX payload the stream demux buffers (NAV-PVT is 92; longer frames are skipped)
#endif
#endif // PROTOCORE_ENABLE_UBX

#if PROTOCORE_ENABLE_NMEA2000
// -- NMEA 2000 (services/timing_position/nmea2000) --
#ifndef PROTOCORE_N2K_FP_MAX
#define PROTOCORE_N2K_FP_MAX 223 ///< Fast Packet max payload (6 in frame 0 + 31 x 7)
#endif
#endif // PROTOCORE_ENABLE_NMEA2000

#if PROTOCORE_ENABLE_OAUTH2
// -- OAuth2 (services/security/oauth2) --
#ifndef PROTOCORE_OAUTH2_TOKEN_LEN
#define PROTOCORE_OAUTH2_TOKEN_LEN 768 ///< access_token / id_token buffer (JWTs are large).
#endif
#ifndef PROTOCORE_OAUTH2_RT_LEN
#define PROTOCORE_OAUTH2_RT_LEN 256 ///< refresh_token buffer.
#endif
#ifndef PROTOCORE_OAUTH2_BODY_BUF
#define PROTOCORE_OAUTH2_BODY_BUF 1024 ///< token-request body buffer.
#endif
#ifndef PROTOCORE_OAUTH2_RESP_BUF
#define PROTOCORE_OAUTH2_RESP_BUF 2048 ///< token-endpoint response buffer.
#endif
#endif // PROTOCORE_ENABLE_OAUTH2

#if PROTOCORE_ENABLE_OIDC
// -- OIDC (services/security/oidc) --
// PROTOCORE_OIDC_MAX_LEN is declared unconditionally with PROTOCORE_ENABLE_OIDC above, because PROTOCORE_AUTH_HDR_CAP
// sizes the Authorization buffer from it and that runs before this block.
#ifndef PROTOCORE_OIDC_SUB_LEN
#define PROTOCORE_OIDC_SUB_LEN 64 ///< Captured `sub` claim buffer.
#endif
#ifndef PROTOCORE_OIDC_EMAIL_LEN
#define PROTOCORE_OIDC_EMAIL_LEN 96 ///< Captured `email` claim buffer.
#endif
#ifndef PROTOCORE_OIDC_KID_LEN
#define PROTOCORE_OIDC_KID_LEN 80 ///< Max `kid` length.
#endif
#ifndef PROTOCORE_OIDC_JWKS_MAX
#define PROTOCORE_OIDC_JWKS_MAX 16384 ///< Max JWKS document scanned; exceeds any real multi-key set, bounds the parse.
#endif
#endif // PROTOCORE_ENABLE_OIDC

#if PROTOCORE_ENABLE_PROVISIONING
// -- Wi-Fi provisioning credential store (server/core/provisioning_service) --
// The NVS namespace and its keys, overridable per deployment (e.g. to avoid an NVS-namespace
// collision with the application's own store).
#ifndef PROTOCORE_PROV_NVS_NAMESPACE
#define PROTOCORE_PROV_NVS_NAMESPACE "wifi_prov" ///< NVS namespace holding the saved credentials.
#endif
#ifndef PROTOCORE_PROV_KEY_SSID
#define PROTOCORE_PROV_KEY_SSID "ssid" ///< NVS key + HTML form field for the SSID.
#endif
#ifndef PROTOCORE_PROV_KEY_PSK
#define PROTOCORE_PROV_KEY_PSK "psk" ///< NVS key + HTML form field for the pre-shared key.
#endif
#endif // PROTOCORE_ENABLE_PROVISIONING

#if PROTOCORE_ENABLE_MNT
// -- Mounted storage (services/storage/mnt) --
#ifndef PROTOCORE_MNT_RAM_FILES
#define PROTOCORE_MNT_RAM_FILES 4 ///< RAM backend: number of files (a directory occupies one).
#endif
#ifndef PROTOCORE_MNT_RAM_FILE_SIZE
#define PROTOCORE_MNT_RAM_FILE_SIZE 1024 ///< RAM backend: max bytes per file.
#endif
#ifndef PROTOCORE_MNT_MAX_OPEN
#define PROTOCORE_MNT_MAX_OPEN 4 ///< Concurrent open handles, files and directory cursors together.
#endif
#ifndef PROTOCORE_MNT_NAME_MAX
#define PROTOCORE_MNT_NAME_MAX 48 ///< Max path length (RAM backend). Not a bound on any caller's buffer.
#endif
#endif // PROTOCORE_ENABLE_MNT

/** @brief SCPI error/event queue depth (entries). The SCPI status model requires a queue; when it
 *  overflows the tail entry is replaced with -350 "Queue overflow" per the standard. */
#ifndef PROTOCORE_SCPI_ERR_QUEUE
#define PROTOCORE_SCPI_ERR_QUEUE 8
#endif

#ifndef PROTOCORE_WAL_PAGE_SIZE
#define PROTOCORE_WAL_PAGE_SIZE 32768 // sequential write unit (the measured durable-throughput knee)
#endif

#ifndef PROTOCORE_WAL_MAX_RECORD
#define PROTOCORE_WAL_MAX_RECORD 4096 // largest single record payload
#endif

#ifndef PROTOCORE_DBM_SLOTS
#define PROTOCORE_DBM_SLOTS 256 // max live keys (in-RAM index capacity; open-addressed, keep load < ~0.7)
#endif

#ifndef PROTOCORE_DBM_KEY_MAX
#define PROTOCORE_DBM_KEY_MAX 32 // largest key in bytes
#endif

#ifndef PROTOCORE_DBM_VAL_MAX
#define PROTOCORE_DBM_VAL_MAX 256 // largest value in bytes
#endif

#ifndef PROTOCORE_DOCSTORE_FIELD_MAX
#define PROTOCORE_DOCSTORE_FIELD_MAX 128 // largest string field value a find can compare
#endif

// PROTOCORE_MESH_MAX_PEERS and PROTOCORE_MESH_MAX_CONNS come from vendor/board_profiles/ (classic floor, raised
// per chip/PSRAM).
#ifndef PROTOCORE_MESH_QUERY_MS
#define PROTOCORE_MESH_QUERY_MS 300 // per-peer query deadline before moving on (miss) / to the origin
#endif

#ifndef PROTOCORE_MESH_HOST_MAX
#define PROTOCORE_MESH_HOST_MAX 64 // largest sibling peer host string
#endif

#ifndef PROTOCORE_MESH_HDRS_MAX
#define PROTOCORE_MESH_HDRS_MAX                                                                                        \
    384 // request-header snapshot carried to a peer so it can match Vary variants
        // (headers past the cap are dropped -> at worst a safe mesh miss, never wrong content)
#endif

#endif // PROTOCORE_BUFFER_SIZING_H
