// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file feature_en_error.h
 * @brief What the enabled features require of each other and of the sizing, as compile-time refusals.
 *
 * Reached from protocore_config.h, which is the single entry point and states the feature flags
 * every block here is gated on. Including this file on its own would read those flags before they
 * are settled.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FEATURE_EN_ERROR_H
#define PROTOCORE_FEATURE_EN_ERROR_H

#ifndef PROTOCORE_CONFIG_H
#error "include protocore_config.h instead of this file - it is the entry point that states the feature flags"
#endif

// These produce a clear #error in the compiler output rather than a cryptic linker failure
// or silent misbehavior.

#if PROTOCORE_WORKER_COUNT < 1
#error "ProtoCore: PROTOCORE_WORKER_COUNT must be >= 1"
#endif
#if PROTOCORE_WORKER_COUNT > MAX_CONNS
#error "ProtoCore: PROTOCORE_WORKER_COUNT must be <= MAX_CONNS"
#endif

#if PROTOCORE_ENABLE_PREEMPT_QUEUE && (PROTOCORE_PQ_DEPTH < 1 || PROTOCORE_PQ_ITEM_SIZE < 1)
#error "ProtoCore: PROTOCORE_PQ_DEPTH and PROTOCORE_PQ_ITEM_SIZE must be >= 1"
#endif

#if PROTOCORE_ENABLE_DMA && (PROTOCORE_DMA_CHANNELS < 1 || PROTOCORE_DMA_BUF_SIZE < 1)
#error "ProtoCore: PROTOCORE_DMA_CHANNELS and PROTOCORE_DMA_BUF_SIZE must be >= 1"
#endif

#if PROTOCORE_ENABLE_TRACE_CAPTURE && PROTOCORE_TC_MAX_WINDOW_SAMPLES < 1
#error "ProtoCore: PROTOCORE_TC_MAX_WINDOW_SAMPLES must be >= 1"
#endif

#if PROTOCORE_ENABLE_FORWARD &&                                                                                        \
    (PROTOCORE_PHY_MAX_IFACES < 1 || PROTOCORE_FWD_MAX_RULES < 1 || PROTOCORE_FWD_ACL_PATLEN < 1)
#error "ProtoCore: PROTOCORE_PHY_MAX_IFACES / PROTOCORE_FWD_MAX_RULES / PROTOCORE_FWD_ACL_PATLEN must be >= 1"
#endif

#if PROTOCORE_ENABLE_GATEWAY && (PROTOCORE_GW_MAX_PORTS < 1)
#error "ProtoCore: PROTOCORE_GW_MAX_PORTS must be >= 1"
#endif

#if PROTOCORE_ENABLE_LORA && (PROTOCORE_LORA_MAX_PAYLOAD < 1 || PROTOCORE_LORA_MAX_PAYLOAD > 251)
#error "ProtoCore: PROTOCORE_LORA_MAX_PAYLOAD must be 1..251"
#endif

#if PROTOCORE_ENABLE_NRF24 && (PROTOCORE_NRF24_PAYLOAD < 1 || PROTOCORE_NRF24_PAYLOAD > 32)
#error "ProtoCore: PROTOCORE_NRF24_PAYLOAD must be 1..32"
#endif

#if PROTOCORE_ENABLE_ENOCEAN && (PROTOCORE_ENOCEAN_MAX_DATA < 1)
#error "ProtoCore: PROTOCORE_ENOCEAN_MAX_DATA must be >= 1"
#endif

#if PROTOCORE_ENABLE_PN532 && (PROTOCORE_PN532_MAX_DATA < 1 || PROTOCORE_PN532_MAX_DATA > 254)
#error "ProtoCore: PROTOCORE_PN532_MAX_DATA must be 1..254"
#endif

#if PROTOCORE_ENABLE_SIGFOX && (PROTOCORE_SIGFOX_MAX_PAYLOAD < 1 || PROTOCORE_SIGFOX_MAX_PAYLOAD > 12)
#error "ProtoCore: PROTOCORE_SIGFOX_MAX_PAYLOAD must be 1..12"
#endif

#if PROTOCORE_ENABLE_ZWAVE && (PROTOCORE_ZWAVE_MAX_DATA < 1)
#error "ProtoCore: PROTOCORE_ZWAVE_MAX_DATA must be >= 1"
#endif

#if PROTOCORE_ENABLE_ZIGBEE && (PROTOCORE_ZIGBEE_MAX_DATA < 1)
#error "ProtoCore: PROTOCORE_ZIGBEE_MAX_DATA must be >= 1"
#endif

#if PROTOCORE_ENABLE_THREAD && (PROTOCORE_THREAD_MAX_DATA < 1)
#error "ProtoCore: PROTOCORE_THREAD_MAX_DATA must be >= 1"
#endif

// ---------------------------------------------------------------------------
// Feature flags
// ---------------------------------------------------------------------------
// Set any of these to 0 in your sketch BEFORE including this library to strip
// the feature from the build entirely (no code, no RAM, no flash cost).
//
//   #define PROTOCORE_ENABLE_WEBSOCKET 0
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
//   #define PROTOCORE_ENABLE_WEBDAV_NEEDS_FILE_SERVING PROTOCORE_ENABLE_FILE_SERVING
//   #if PROTOCORE_ENABLE_WEBDAV && !PROTOCORE_ENABLE_WEBDAV_NEEDS_FILE_SERVING
//   #error "ProtoCore: PROTOCORE_ENABLE_WEBDAV needs PROTOCORE_ENABLE_FILE_SERVING"
//   #endif
//
// so the whole graph is `grep -oE '_NEEDS_[A-Z_0-9]+' protocore_config.h` and both sides come out
// of the name. The guard tests the symbol rather than restating the condition, so what is enforced
// and what is declared are the same text.
//
// Optional integrations (these build fine on their own; the named feature is
// simply inert or reduced until you also enable the other flag):
//
//   WEBHOOK   + HTTP_CLIENT : without it, Webhook.post() leaves Webhook.i32 at -1
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

#if PROTOCORE_ENABLE_POWER_MGMT && (PROTOCORE_POWER_TEMP_COOL_C >= PROTOCORE_POWER_TEMP_HOT_C)
#error "ProtoCore: PROTOCORE_POWER_TEMP_COOL_C must be below PROTOCORE_POWER_TEMP_HOT_C (hysteresis)"
#endif

#if PROTOCORE_ENABLE_POWER_MGMT && (PROTOCORE_POWER_MHZ_MIN > PROTOCORE_POWER_MHZ_MAX)
#error "ProtoCore: PROTOCORE_POWER_MHZ_MIN must not exceed PROTOCORE_POWER_MHZ_MAX"
#endif

#if PROTOCORE_ENABLE_POWER_MGMT && (PROTOCORE_POWER_BUSY_PCT > 100)
#error "ProtoCore: PROTOCORE_POWER_BUSY_PCT must be 0..100"
#endif

#if PROTOCORE_ENABLE_HOTSWAP && (PROTOCORE_HOTSWAP_FAIL_THRESHOLD < 1 || PROTOCORE_HOTSWAP_FAIL_THRESHOLD > 255)
#error "ProtoCore: PROTOCORE_HOTSWAP_FAIL_THRESHOLD must be in [1, 255]"
#endif

#if PROTOCORE_ENABLE_FTP_SESSION && (PROTOCORE_FTP_REPLY_BUF < 128)
#error "ProtoCore: PROTOCORE_FTP_REPLY_BUF must be >= 128 (a multiline greeting needs room)"
#endif

#if PROTOCORE_ENABLE_FTP_SESSION && (PROTOCORE_FTP_CHUNK < 64)
#error "ProtoCore: PROTOCORE_FTP_CHUNK must be >= 64"
#endif

#if PROTOCORE_ENABLE_EXC_DECODER && (PROTOCORE_EXC_COREDUMP_CHUNK < 64)
#error "ProtoCore: PROTOCORE_EXC_COREDUMP_CHUNK must be >= 64"
#endif

#if PROTOCORE_ENABLE_HTTP_DELIVERY && (PROTOCORE_DELIVERY_PRECACHE_MAX < 1)
#error "ProtoCore: PROTOCORE_DELIVERY_PRECACHE_MAX must be >= 1"
#endif
#if PROTOCORE_ENABLE_HTTP_DELIVERY && (PROTOCORE_DELIVERY_MANIFEST_BUF < 64)
#error "ProtoCore: PROTOCORE_DELIVERY_MANIFEST_BUF must be >= 64"
#endif

#if PROTOCORE_ENABLE_WIFI_SNIFFER &&                                                                                   \
    ((PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS < 1) || (PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS > 14))
#error "ProtoCore: PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS must be 1..14"
#endif

#if (PROTOCORE_LOG_LEVEL < PROTOCORE_LOG_LEVEL_DEBUG) || (PROTOCORE_LOG_LEVEL > PROTOCORE_LOG_LEVEL_NONE)
#error "ProtoCore: PROTOCORE_LOG_LEVEL must be one of the PROTOCORE_LOG_LEVEL_* constants"
#endif

/**
 * @brief The HPACK dynamic table one connection tracks for its peer's encoder.
 *
 * The entry descriptors and the byte ring they index into. The connection owns these bytes; the
 * exact width is hpack.c's, and the static_assert there is what proves this covers it.
 */
#ifndef PROTOCORE_HPACK_BORROW
#define PROTOCORE_HPACK_BORROW ((size_t)PROTOCORE_HPACK_MAX_ENTRIES * 8 + PROTOCORE_HPACK_TABLE_BYTES + 32)
#endif

// The ring indexes with `% cap` (mmgr/ring.h). A power-of-two capacity makes that an AND; any other
// value emits a divide, which on a target without one is a call into a software routine per access.
#if (PROTOCORE_UDP_RX_RING & (PROTOCORE_UDP_RX_RING - 1)) != 0
#error "ProtoCore: PROTOCORE_UDP_RX_RING must be a power of two"
#endif

// A ring holds a frame header plus a payload, and keeps one slot free to tell full from empty, so a
// ring that cannot take one largest datagram would drop every receive.
#if PROTOCORE_UDP_RX_RING < (PROTOCORE_UDP_RX_BUF_SIZE + 64)
#error "ProtoCore: PROTOCORE_UDP_RX_RING must exceed PROTOCORE_UDP_RX_BUF_SIZE by at least one frame header"
#endif

/**
 * @brief Maximum concurrent SSH channels per connection (RFC 4254 multiplexing).
 *
 * Default 1 - one "session" channel per connection, byte-for-byte the original
 * single-channel behavior. Raise it to multiplex several channels (e.g. several
 * concurrent shells/exec, or - with the forwarding build flags - tunnels) over one
 * SSH connection; each channel gets its own id, window, and peer state. Fixed BSS
 * (ssh_chan[MAX_SSH_CONNS][PROTOCORE_SSH_MAX_CHANNELS]), no heap.
 */
#ifndef PROTOCORE_SSH_MAX_CHANNELS
#define PROTOCORE_SSH_MAX_CHANNELS 1
#endif
#if PROTOCORE_SSH_MAX_CHANNELS < 1
#error "ProtoCore: PROTOCORE_SSH_MAX_CHANNELS must be >= 1"
#endif

// One borrow per HTTP/2 connection from the plaintext pool's PERSISTENT end, split by offset into
// the frame payload buffer, the header block, the HPACK emit scratch and the 9-octet frame header,
// which gets 16 of its own so the three power-of-two regions keep their alignment. Proved against
// the real PROTOCORE_H2_CONN_BORROW by a static_assert in h2_conn.c.
#ifndef PROTOCORE_WORK_H2_CONN
#define PROTOCORE_WORK_H2_CONN                                                                                         \
    ((size_t)MAX_CONNS * ((size_t)PROTOCORE_H2_MAX_FRAME + 2u * (size_t)PROTOCORE_H2_HDR_BLOCK + 16u))
#endif

// One borrow per HTTP/3 connection from the same persistent end, grouped by field rather than by
// stream: every reassembly buffer, then every :path, every :authority and every :method. Grouped,
// each stride is a power of two and stream i reaches its bytes with a shift; strided by stream, the
// stride would be their sum and the reach a multiply. Proved by a static_assert in h3_conn.c.
// The context leads the borrow, as the digest context leads sha256's: it carries no key material, so
// it belongs with the stream bytes rather than in the secure arena. Proved against sizeof(H3ConnCtx)
// by a static_assert in h3_conn.c.
#ifndef PROTOCORE_H3_CONN_CTX
#define PROTOCORE_H3_CONN_CTX 672
#endif
// After the per-stream regions come four a dispatch reads one at a time and no stream owns: the
// body it hands the handler, the QPACK scratch and the encoded block a header set decodes through,
// and the buffer a response is built in. H3_OFF_BODY through H3_OFF_OUT in h3_conn.c.
#ifndef PROTOCORE_H3_CONN_BORROW
#define PROTOCORE_H3_CONN_BORROW                                                                                       \
    ((size_t)PROTOCORE_H3_CONN_CTX +                                                                                   \
     (size_t)PROTOCORE_H3_MAX_STREAMS * ((size_t)PROTOCORE_H3_STREAM_BUF + PROTOCORE_H3_PATH_LEN +                     \
                                         PROTOCORE_H3_AUTHORITY_LEN + PROTOCORE_H3_METHOD_LEN) +                       \
     2u * (size_t)PROTOCORE_H3_STREAM_BUF + (size_t)PROTOCORE_H3_QPACK_SCRATCH + (size_t)PROTOCORE_H3_QPACK_BLOCK)
#endif
#ifndef PROTOCORE_WORK_H3_CONN
#define PROTOCORE_WORK_H3_CONN ((size_t)PROTOCORE_QUIC_MAX_CONNS * PROTOCORE_H3_CONN_BORROW)
#endif

// The QUIC/HTTP3 server, whole: its control state, the connection pool and the ingest ring, all
// carved out of the one span protocore.c hands it. 96 bytes of control state, 56 per pool slot, and
// one buffered datagram plus its peer per ring entry - measured, and the static_assert in
// quic_server.c is what proves the three regions fit. 11168 bytes for the default
// PROTOCORE_QUIC_MAX_CONNS of 2 and PROTOCORE_QUIC_INGEST_RING of 8, and scales with both and with
// PROTOCORE_QUIC_MAX_DATAGRAM. Slot bookkeeping and datagrams, no key material of its own, so the
// plaintext end.
#ifndef PROTOCORE_QUIC_SERVER_BORROW
#define PROTOCORE_QUIC_SERVER_BORROW                                                                                   \
    (96u + (size_t)PROTOCORE_QUIC_MAX_CONNS * 56u +                                                                    \
     (size_t)PROTOCORE_QUIC_INGEST_RING * ((size_t)PROTOCORE_QUIC_MAX_DATAGRAM + 24u))
#endif

// The QUIC transport under it takes its own borrow from the same end: the bytes it owes each stream
// and the CRYPTO window per packet-number space. Proved by a static_assert in quic_conn.c.
// Only the byte buffers: the connection's context is key material and takes a secure borrow instead
// (PROTOCORE_QUIC_CONN_CTX_BORROW). Proved against the real split by a static_assert in quic_conn.c.
#ifndef PROTOCORE_QUIC_CONN_BORROW
#define PROTOCORE_QUIC_CONN_BORROW                                                                                     \
    (((size_t)PROTOCORE_QUIC_MAX_STREAMS * PROTOCORE_QUIC_STREAM_TX) + 3u * (size_t)PROTOCORE_QUIC_CRYPTO_RX)
#endif
#ifndef PROTOCORE_WORK_QUIC_CONN
#define PROTOCORE_WORK_QUIC_CONN ((size_t)PROTOCORE_QUIC_MAX_CONNS * PROTOCORE_QUIC_CONN_BORROW)
#endif

// The edge cache's state, split across both pools the way a QUIC connection is. The L1 response
// store, the route maps, the fetch slots and the per-connection scratch are cached origin bytes and
// carry nothing secret, so they take the PLAINTEXT borrow. Measured at 18168 bytes with the default
// widths and 26128 with the mesh, L2 and range arms all on, which is what this number covers; the
// static_assert refuses a build it is short of. Only the TLS half is secure, below. A literal rather
// than a formula because the terms are edge_cache.h's, which this file is included by rather than
// includes; proved against sizeof(EdgeCacheProxyCtx) by a static_assert in edge_cache_proxy.c.
#ifndef PROTOCORE_EDGE_PROXY_BORROW
#define PROTOCORE_EDGE_PROXY_BORROW 28672
#endif

// The EUROMAP 77 model state: the pointer to the caller-owned EmImm the resolvers read out of.
// Plaintext because a machine model carries no key material, and taken from the persistent end so
// the bind done before begin() outlives every Read and Browse that follows it. A literal rather than
// a formula because the term is euromap77.h's, which this file is included by rather than includes;
// proved against sizeof(EuroMap77Ctx) by a static_assert in euromap77.c.
#ifndef PROTOCORE_EUROMAP77_BORROW
#define PROTOCORE_EUROMAP77_BORROW 16
#endif

// The umati model state: the pointer to the caller-owned UmatiMachineTool the resolvers read out of,
// and the NamespaceIndex the OPC UA server gave this model's URI. Plaintext for the same reason as
// EUROMAP 77 above, and taken from the persistent end so the bind done before begin() outlives every
// Read and Browse. Proved against sizeof(UmatiCtx) by a static_assert in umati.c.
#ifndef PROTOCORE_UMATI_BORROW
#define PROTOCORE_UMATI_BORROW 16
#endif

// The robotics model state: as umati's, plus the per-axis BrowseName table the Browse hands out by
// pointer - PROTOCORE_ROBOTICS_AXES names of "Axis_" plus up to two digits. Proved against
// sizeof(RoboticsCtx) by a static_assert in robotics.c.
#ifndef PROTOCORE_ROBOTICS_BORROW
#define PROTOCORE_ROBOTICS_BORROW 128
#endif

// The SIMATIC helper operands: the block being walked with its length, position and BCC variant,
// and on the receive side the caller's link plus the byte that arrived and when. No key material,
// so the plaintext end. Proved against sizeof(SimaticCtx) by a static_assert in simatic.c.
#ifndef PROTOCORE_SIMATIC_BORROW
#define PROTOCORE_SIMATIC_BORROW 48
#endif

// The J1939 frame-builder operands: the CanFrame being filled, its PGN, priority, source and
// destination addresses and its data length - the widest set any one entry hands its private
// helper. No key material, so the plaintext end. Proved against sizeof(J1939Ctx) by a static_assert
// in j1939.c.
#ifndef PROTOCORE_J1939_BORROW
#define PROTOCORE_J1939_BORROW 16
#endif

// The Modbus data model: one bit per coil and per discrete input, one 16-bit word per holding and
// per input register, plus the write callback and the alignment between them. Scales with the four
// table sizes above. No key material, so the plaintext end. Proved against sizeof(ModbusCtx) by a
// static_assert in modbus.c.
#ifndef PROTOCORE_MODBUS_BORROW
#define PROTOCORE_MODBUS_BORROW                                                                                        \
    ((size_t)((PROTOCORE_MODBUS_COILS + 7) / 8) + (size_t)((PROTOCORE_MODBUS_DISCRETE_INPUTS + 7) / 8) +               \
     (size_t)PROTOCORE_MODBUS_HOLDING_REGS * 2u + (size_t)PROTOCORE_MODBUS_INPUT_REGS * 2u + 32u)
#endif

// The ESP-NOW peer registry: a MAC and a used flag per peer, and on the vendor arm the receive
// callback plus the channel it was bound on. No key material, so the plaintext end. Proved against
// sizeof(EspnowCtx) by a static_assert in espnow.c.
#ifndef PROTOCORE_ESPNOW_BORROW
#define PROTOCORE_ESPNOW_BORROW ((size_t)PROTOCORE_ESPNOW_MAX_PEERS * 8u + 32u)
#endif

// The promiscuous-capture state: the frame sink the radio binding calls. No key material, so the
// plaintext end. Proved against sizeof(PromiscCtx) by a static_assert in promisc.c.
#ifndef PROTOCORE_PROMISC_BORROW
#define PROTOCORE_PROMISC_BORROW 16
#endif

// The live sniff state: the packet-type tallies, the per-channel survey and the scan cursor, plus
// the running flag. No key material, so the plaintext end. Proved against sizeof(WifiSnifferCtx)
// by a static_assert in wifi_sniffer.c.
#ifndef PROTOCORE_WIFI_SNIFFER_BORROW
#define PROTOCORE_WIFI_SNIFFER_BORROW 256
#endif

// The radio keep-awake refcount: how many transfers are holding active mode. No key material, so
// the plaintext end. Proved against sizeof(struct RadioStorage) by a static_assert in radio_power.c.
#ifndef PROTOCORE_RADIO_POWER_BORROW
#define PROTOCORE_RADIO_POWER_BORROW 16
#endif

// The authoritative A records this server answers from - the owner names, their addresses and the
// count - plus the response staged for the last query. Scales with the record table. No key
// material, so the plaintext end. Proved against sizeof(struct DnsServerStorage) by a static_assert
// in dns_server.c.
#ifndef PROTOCORE_DNS_SERVER_BORROW
#define PROTOCORE_DNS_SERVER_BORROW                                                                                    \
    ((size_t)PROTOCORE_DNS_SERVER_MAX_RECORDS * ((size_t)PROTOCORE_DNS_NAME_MAX + 4u) +                                \
     (size_t)PROTOCORE_DNS_NAME_MAX + 96u)
#endif

// The forwarding plane's tables: the (src, dst) controls, the access list, the policy routes, the
// default verdict, the counters and the inspection hook. Scales with the three table caps. No key
// material, so the plaintext end. Proved against sizeof(struct ForwardStorage) by a static_assert
// in forward.c.
#ifndef PROTOCORE_FORWARD_BORROW
#define PROTOCORE_FORWARD_BORROW                                                                                       \
    ((size_t)PROTOCORE_FWD_MAX_RULES * 24u +                                                                           \
     (size_t)PROTOCORE_FWD_MAX_ACL * ((size_t)PROTOCORE_FWD_ACL_PATLEN * 2u + 24u) +                                   \
     (size_t)PROTOCORE_FWD_MAX_ROUTES * ((size_t)PROTOCORE_FWD_ACL_PATLEN * 2u + 32u) + 128u)
#endif

// The one installed log sink, the function every emitted line is handed to. A single pointer. No
// key material, so the plaintext end. Proved against sizeof(struct LogStorage) by a static_assert
// in log.c.
#ifndef PROTOCORE_LOG_BORROW
#define PROTOCORE_LOG_BORROW 16u
#endif

// The two server-wide DSCP defaults: the mark outbound TCP connections start from and the mark
// outbound UDP datagrams take. Two bytes, fixed. No key material, so the plaintext end. Proved
// against sizeof(struct DiffServStorage) by a static_assert in diffserv.c.
#ifndef PROTOCORE_DIFFSERV_BORROW
#define PROTOCORE_DIFFSERV_BORROW 8u
#endif

// The UDP sending side's one outbound control block, opened on first send. A single pointer. No key
// material, so the plaintext end. Proved against sizeof(struct UdpClientStorage) by a static_assert
// in udp/client/client.c.
#ifndef PROTOCORE_UDP_CLIENT_BORROW
#define PROTOCORE_UDP_CLIENT_BORROW 16u
#endif

// The UDP receiving side: one slot per bound port, each carrying its own receive ring, plus the
// payload stage one delivery is handed out of, the two header stages at the ends of that ring, the
// bitmap of bound slots, the reentrancy latch and the text a joined group is formatted into. Scales
// with the listener count and the ring. No key material, so the plaintext end. Proved against
// sizeof(struct UdpListenerStorage) by a static_assert in udp/server/server.c.
#ifndef PROTOCORE_UDP_LISTENER_BORROW
#define PROTOCORE_UDP_LISTENER_BORROW                                                                                  \
    ((size_t)PROTOCORE_MAX_UDP_LISTENERS * ((size_t)PROTOCORE_UDP_RX_RING + 96u) + (size_t)PROTOCORE_UDP_RX_BUF_SIZE + \
     256u)
#endif

// The TCP/lower-level seam: one marshal record per connection slot, plus the stack thread it
// captured on its first op and the TTL it stamps outbound segments with. No key material - the
// record carries a pointer to the caller's bytes, never a copy - so the plaintext end. Proved
// against sizeof(struct TcpLowerStorage) by a static_assert in tcp/lower/lower.c.
#ifndef PROTOCORE_TCP_LOWER_BORROW
#define PROTOCORE_TCP_LOWER_BORROW ((size_t)CONN_POOL_SLOTS * 96u + 64u)
#endif

// The connection pool's own state: the zeroed slot template init resets a slot from (one whole
// TcpConn, receive ring included), the nine close-reason counters, the idle deadline and the
// installed observer. No key material - the slot payloads live in conn_pool, not here - so the
// plaintext end. Proved against sizeof(struct ConnPoolStorage) by a static_assert in
// tcp/protocol/protocol.c.
#ifndef PROTOCORE_CONN_POOL_BORROW
#define PROTOCORE_CONN_POOL_BORROW ((size_t)RX_BUF_SIZE + 512u)
#endif

// The accepting side's accept-time state: the global fixed-window throttle, the per-source-address
// bucket table, the CIDR allowlist, and above one worker the per-worker event queues. Scales with
// the two table caps and the worker count. No key material - a source address is not a secret - so
// the plaintext end. Proved against sizeof(struct TcpListenerStorage) by a static_assert in
// tcp/server/server.c.
#ifndef PROTOCORE_TCP_LISTENER_BORROW
#define PROTOCORE_TCP_LISTENER_BORROW                                                                                  \
    ((size_t)PROTOCORE_PER_IP_THROTTLE_SLOTS * 32u + (size_t)PROTOCORE_IP_ALLOWLIST_SLOTS * 32u +                      \
     (size_t)PROTOCORE_WORKER_COUNT * ((size_t)EVT_QUEUE_DEPTH * 24u + 128u) + 256u)
#endif

// The dialing side: one record per outbound connection, each carrying its own receive ring, plus
// the record the private step every call runs first is pointing at. Scales with the connection
// count and that ring. No key material - a TLS client's secrets live in the TLS context, not here -
// so the plaintext end. Proved against sizeof(struct TcpClientStorage) by a static_assert in
// tcp/client/client.c.
#ifndef PROTOCORE_TCP_CLIENT_BORROW
#define PROTOCORE_TCP_CLIENT_BORROW ((size_t)PROTOCORE_CLIENT_CONNS * ((size_t)PROTOCORE_CLIENT_RX_BUF + 96u) + 64u)
#endif

// The one address a Happy Eyeballs preference step scores. The sort compares a key it holds against
// the element beside it, so the operand differs per call and rides the context rather than a
// parameter. One pointer. No key material, so the plaintext end. Proved against
// sizeof(HappyEyeballsCtx) by a static_assert in happy_eyeballs.c.
#ifndef PROTOCORE_HAPPY_EYEBALLS_BORROW
#define PROTOCORE_HAPPY_EYEBALLS_BORROW 16u
#endif

// The Layer 1 interface registry: one row per interface the application registered, each carrying
// the callback that puts octets on it and the context that callback is handed back. Scales with
// PROTOCORE_PHY_MAX_IFACES. No key material - a send callback is not a secret - so the plaintext
// end. Proved against sizeof(struct PhysicalStorage) by a static_assert in physical.c.
#ifndef PROTOCORE_PHYSICAL_BORROW
#define PROTOCORE_PHYSICAL_BORROW ((size_t)PROTOCORE_PHY_MAX_IFACES * 32u + 32u)
#endif

/**
 * @brief Worst-case bytes each module borrows from the secure pool in a single call.
 *
 * Declared here because PROTOCORE_SECURE_ARENA_SIZE below is derived from them and this is the one
 * place that can see them all - a module header cannot host its own, since every module header
 * includes this file. Each value is PROVED where the struct lives: the owning .cpp carries a
 * static_assert(sizeof(X) <= PROTOCORE_WORK_X), so a working set that grows past its declaration fails
 * the build naming itself, rather than exhausting the pool at run time.
 *
 * These are SIZES, not offsets. Nothing here couples one module to another: each is a term in a
 * sum, order is irrelevant, and adding a module shifts no one. That is the difference from the
 * crypto_work region map these replaced.
 *
 * Values are what the ESP32 toolchain reported for the real structs, rounded up.
 */

// The SHA-256 borrow, split by offset in sha256.c: the 64-byte block as it arrives, the padded last
// one, and the 32-byte state copy finalizing compresses into so the running hash survives it. 64 + 64
// + 32 = 160. The context is that module's own state and is no longer a region here. The schedule is a
// register window, not storage.
//
// One figure, both arms. The accelerator compresses a block; it does not pad, buffer a partial block,
// or hold a digest a caller can keep feeding, so the same regions are taken whether the compression
// runs on the peripheral or in software. Proved against the real layout by a static_assert in sha256.c.
#ifndef PROTOCORE_SHA256_BORROW
#define PROTOCORE_SHA256_BORROW 256
#endif

// The SHA-1 borrow, split by offset in sha1.c: the running state (uint32_t h[5], 20 bytes) then the
// two padded final blocks (128 bytes) a message whose tail reaches 56 bytes composes. 148 bytes,
// rounded up to the next 32-byte multiple. One figure, both arms: the accelerated arm digests through
// mbedtls and takes none of it. Proved against the real split by a static_assert in sha1.c.
#ifndef PROTOCORE_SHA1_BORROW
#define PROTOCORE_SHA1_BORROW 160
#endif

// The SSH key-exchange digest borrow, split by offset in ssh_kexhash.c: the region the bound hash
// runs in - SHA-512's, the wider of the two - then the octet naming which hash that is. Proved by a
// static_assert in ssh_kexhash.c.
#ifndef PROTOCORE_SSH_KEXHASH_BORROW
#define PROTOCORE_SSH_KEXHASH_BORROW (PROTOCORE_SHA512_BORROW + 8)
#endif

// A SHA-512 context works out of the same regions as SHA-256, at its own widths: the context itself,
// the 128-byte block as it arrives, the padded last one, and the 64-byte state copy finalizing
// compresses into. The schedule is a register window, not storage. One figure for both arms, for the
// reason stated above SHA-256. Proved by a static_assert in sha512.c.
#ifndef PROTOCORE_SHA512_BORROW
#define PROTOCORE_SHA512_BORROW 448
#endif

// A SHA-384 context is a SHA-512 one: same block width, same state, same regions, and only the seed
// and the digest length differ, so the figure is SHA-512's. Proved by a static_assert in sha384.c.
#ifndef PROTOCORE_SHA384_BORROW
#define PROTOCORE_SHA384_BORROW 448
#endif

// An HMAC-SHA512 context works out of two SHA-512 borrows - the inner hash it keeps across updates and
// the outer one final runs - plus the two key blocks and the inner digest between them. Proved against
// the real split by a static_assert in hmac_sha512.c.
#ifndef PROTOCORE_HMAC_SHA512_BORROW
#define PROTOCORE_HMAC_SHA512_BORROW (2 * PROTOCORE_SHA512_BORROW + 768)
#endif

// An HMAC-SHA384 context is the same split at the same widths: the block is SHA-512's 128 octets and
// only the inner digest is shorter. Proved against the real split by a static_assert in hmac_sha384.c.
#ifndef PROTOCORE_HMAC_SHA384_BORROW
#define PROTOCORE_HMAC_SHA384_BORROW (2 * PROTOCORE_SHA384_BORROW + 768)
#endif

// An HMAC-SHA256 context works out of two SHA-256 borrows - the inner hash it keeps across updates and
// the outer one final runs - plus the key blocks and digest between them. Proved against the real
// split by a static_assert in hmac_sha256.c.
#ifndef PROTOCORE_HMAC_SHA256_BORROW
#define PROTOCORE_HMAC_SHA256_BORROW (2 * PROTOCORE_SHA256_BORROW + 384)
#endif

// The accelerated modexp backend: four 256-octet big-endian buffers handed to the vendor, then the
// region the Bignum conversions that fill and drain them run in.
#ifndef PROTOCORE_WORK_BIGNUM_HW
#define PROTOCORE_WORK_BIGNUM_HW 1328
#endif
#ifndef PROTOCORE_WORK_BIGNUM_SW
#define PROTOCORE_WORK_BIGNUM_SW 1408
#endif
// AES-256-GCM keyed context. Sized per vendor for the same reason as the bignum working set above: one
// backend or the other is compiled, never both. It matters more here than it did as a transient
// borrow - a consumer now embeds two of these per connection (send and receive) for the life of the
// key, so one flat figure sized for the largest backend is RAM every target pays and only one uses.
// The static_assert in each backend is what keeps these honest against a vendor header we do not own.
#ifndef PROTOCORE_AESGCM_BORROW_HW
#define PROTOCORE_AESGCM_BORROW_HW 416 // mbedtls_gcm_context measures 392 on the S3
#endif
#ifndef PROTOCORE_AESGCM_BORROW_SW
#define PROTOCORE_AESGCM_BORROW_SW 640 // GcmWork is 608: AES-256 round keys + the 4-bit GHASH table
#endif
#ifndef PROTOCORE_AESGCM_BORROW
#if PROTOCORE_HAS_HW_AESGCM
#define PROTOCORE_AESGCM_BORROW PROTOCORE_AESGCM_BORROW_HW
#else
#define PROTOCORE_AESGCM_BORROW PROTOCORE_AESGCM_BORROW_SW
#endif
#endif
#ifndef PROTOCORE_WORK_AESCCM
#define PROTOCORE_WORK_AESCCM 448
#endif
// AES-128 single-block context (QUIC/DTLS header + sequence-number protection). Kept per key for the
// same reason as the AEAD contexts, though the win is smaller: measured on an S3, rebuilding it per
// record costs ~556 cycles plus a pool borrow and wipe. (The ECB block it protects is ~7,842 cycles on
// its own - one HW-AES operation - which is now the larger per-packet cost in QUIC and DTLS.)
#ifndef PROTOCORE_WORK_AES128_HW
#define PROTOCORE_WORK_AES128_HW 288 // mbedtls_aes_context: nr + rk + buf[68]
#endif
#ifndef PROTOCORE_WORK_AES128_SW
#define PROTOCORE_WORK_AES128_SW 176 // uint32_t rk[44]
#endif
#ifndef PROTOCORE_WORK_AES128
#if PROTOCORE_HAS_HW_AESGCM
#define PROTOCORE_WORK_AES128 PROTOCORE_WORK_AES128_HW
#else
#define PROTOCORE_WORK_AES128 PROTOCORE_WORK_AES128_SW
#endif
#endif

// AES-128-GCM keyed context, sized per vendor exactly as PROTOCORE_AESGCM_BORROW above and for the same reason:
// one backend is compiled, and a consumer holding a context per direction should not carry storage for
// the backend it did not build.
#ifndef PROTOCORE_WORK_AES128GCM_HW
#define PROTOCORE_WORK_AES128GCM_HW 416 // mbedtls_gcm_context measures 392 on the S3
#endif
#ifndef PROTOCORE_WORK_AES128GCM_SW
#define PROTOCORE_WORK_AES128GCM_SW 576 // Aes128GcmWork is 560: AES-128 round keys + the 4-bit GHASH table
#endif
#ifndef PROTOCORE_WORK_AES128GCM
#if PROTOCORE_HAS_HW_AESGCM
#define PROTOCORE_WORK_AES128GCM PROTOCORE_WORK_AES128GCM_HW
#else
#define PROTOCORE_WORK_AES128GCM PROTOCORE_WORK_AES128GCM_SW
#endif
#endif
// The chacha20-poly1305 borrow, split by offset in chachapoly.c: the per-packet working set (the
// nonce, the derived one-time Poly1305 key, the computed tag and the decrypted length word, 60
// octets), then a region for the nested ChaCha20 and one for the nested Poly1305, each driven through
// its own namespace. Proved by a static_assert in chachapoly.c.
#ifndef PROTOCORE_CHACHAPOLY_BORROW
#define PROTOCORE_CHACHAPOLY_BORROW (64 + PROTOCORE_CHACHA20_BORROW + PROTOCORE_POLY1305_BORROW)
#endif
// The ChaCha20 borrow: the 16-word input state and the 16-word round state, then one keystream block.
// 192 octets. Proved by a static_assert in chacha20.c.
#ifndef PROTOCORE_CHACHA20_BORROW
#define PROTOCORE_CHACHA20_BORROW 192
#endif
// The AES-256-CTR borrow: the expanded key, then one keystream block. The accelerator's context is the
// larger of the two arms at 304 octets. Proved by a static_assert in aes256ctr.c.
#ifndef PROTOCORE_AES256CTR_BORROW
#define PROTOCORE_AES256CTR_BORROW 384
#endif
// The Poly1305 borrow: the clamped key part, its reduction multipliers and the accumulator, five
// 26-bit limbs each, then the padded final block. 76 octets. Proved by a static_assert in poly1305.c.
#ifndef PROTOCORE_POLY1305_BORROW
#define PROTOCORE_POLY1305_BORROW 96
#endif
// The AES-128-CMAC borrow: the block context, then the prepared last block, the CBC-MAC accumulator
// and the XOR scratch. The accelerator's context is the larger of the two arms at 336 octets. Proved
// by a static_assert in aes_cmac.c.
#ifndef PROTOCORE_AES_CMAC_BORROW
#define PROTOCORE_AES_CMAC_BORROW 384
#endif
// The MD-family borrow, split by offset in md.c: the running digest state, then the regions HMAC-MD5
// needs - the key block, its two pads, the inner digest, and the state a key longer than the block is
// hashed down in. All of it is NTLM password and session-key material, so none of it may sit on the
// stack. Proved against the real split by a static_assert in md.c.
#ifndef PROTOCORE_MD_BORROW
#define PROTOCORE_MD_BORROW 448
#endif
// The Keccak borrow, split by offset in sha3.c: the sponge the streaming XOF carries across calls,
// then the sponge the one-shot digests and XOFs run in. One KeccakCtx is 25 lanes plus the rate and
// the squeeze position, 208 octets, and the split holds two of them. Proved against the real split by
// a static_assert in sha3.c.
#ifndef PROTOCORE_SHA3_BORROW
#define PROTOCORE_SHA3_BORROW 224
#endif
// The SP800-108 KDF borrow, split by offset in kdf.c: the PRF's own bytes, then K(i) and the 32-bit
// counter, 36 octets. Proved by a static_assert in kdf.c.
#ifndef PROTOCORE_KDF_BORROW
#define PROTOCORE_KDF_BORROW (PROTOCORE_HMAC_SHA256_BORROW + 64)
#endif
// The GHASH borrow: the 4-bit table built from the subkey H, 16 rows of 4 words. The table is the
// state the module's own entries carry between them - key_init builds it, update and mul read it.
// Proved by a static_assert in ghash.c.
#ifndef PROTOCORE_GHASH_BORROW
#define PROTOCORE_GHASH_BORROW 256
#endif
// The bignum borrow: the operands an entry stages for its helpers, then the value a conversion or a
// compare runs over. The modexp's own scratch is not here - that lives in the backend the build
// selects, sized by PROTOCORE_WORK_BIGNUM_HW / _SW above. Proved by a static_assert in bignum.c.
#ifndef PROTOCORE_BIGNUM_BORROW
#define PROTOCORE_BIGNUM_BORROW 304
#endif
// The AES-CCM borrow: the keyed context one record runs out of. The software arm holds the AES key
// schedule, the round count, the CBC-MAC accumulator, the formatting block, the counter block and the
// keystream, 308 octets; the accelerator's mbedtls_ccm_context is the larger arm at the figure this
// file already carried for it. Proved by a static_assert in aesccm.c.
#ifndef PROTOCORE_AESCCM_BORROW
#define PROTOCORE_AESCCM_BORROW PROTOCORE_WORK_AESCCM
#endif
// The AES-128-GCM borrow, split by offset in aes128gcm.c: the keyed AEAD context, then the
// single-block context the header protection uses. Both terms are already sized per vendor above, so
// the borrow is their sum and follows the arm the build selects. Proved by a static_assert in
// aes128gcm.c.
#ifndef PROTOCORE_AES128GCM_BORROW
#define PROTOCORE_AES128GCM_BORROW (PROTOCORE_WORK_AES128GCM + PROTOCORE_WORK_AES128)
#endif

// A TLS 1.3 record key holds one keyed AEAD context per direction, and which AEAD that is depends on
// the suite the connection negotiated: AEAD_AES_128_GCM for 0x1301, AEAD_AES_256_GCM for 0x1302. The
// slot is the wider of the two. AES-128-GCM's is not the smaller one despite the shorter key: that
// borrow carries the single-block context aes128gcm.h also exposes, which AES-256-GCM has no
// counterpart for. Proved against both by a static_assert in record.c.
#ifndef PROTOCORE_TLS_RECORD_AEAD_BORROW
#if PROTOCORE_AES128GCM_BORROW > PROTOCORE_AESGCM_BORROW
#define PROTOCORE_TLS_RECORD_AEAD_BORROW PROTOCORE_AES128GCM_BORROW
#else
#define PROTOCORE_TLS_RECORD_AEAD_BORROW PROTOCORE_AESGCM_BORROW
#endif
#endif
// The X25519 borrow: the clamped scalar, the base point, and the Montgomery ladder's running points
// and per-bit intermediates. The radix-2^16 protocore_gf arm is the larger of the two at 960 octets,
// seven 128-octet field elements over the two 32-octet scalars; the radix-2^32 fe arm is 576. Proved
// by a static_assert in curve25519.c.
#ifndef PROTOCORE_CURVE25519_BORROW
#define PROTOCORE_CURVE25519_BORROW 960
#endif
// The Ed25519 borrow, split by offset in ed25519.c: the signature working set - the S accumulator,
// the clamped hash of the seed, the two reduced scalars, the public point and the packed comparison
// value, 768 octets - then the region SHA-512 runs in. Proved by a static_assert in ed25519.c.
#ifndef PROTOCORE_ED25519_BORROW
#define PROTOCORE_ED25519_BORROW (768 + PROTOCORE_SHA512_BORROW)
#endif
// The RSA borrow, split by offset in rsa.c: the regions SHA-256, SHA-512 and the bignum conversions
// run in, then the ladder's working set - the staged multiply, the digest, the encoded block, the
// recovered block, the five 256-octet bignums and the double-width product, 2,432 octets. Proved by a
// static_assert in rsa.c.
#ifndef PROTOCORE_RSA_BORROW
#define PROTOCORE_RSA_BORROW (PROTOCORE_SHA256_BORROW + PROTOCORE_SHA512_BORROW + PROTOCORE_BIGNUM_BORROW + 2432)
#endif
// The ML-KEM-768 borrow: the region SHA-3 runs in. The module carries nothing across its entries, so
// the sponge is the whole working set. Proved by a static_assert in mlkem.c.
#ifndef PROTOCORE_MLKEM_BORROW
#define PROTOCORE_MLKEM_BORROW PROTOCORE_SHA3_BORROW
#endif
// The sntrup761 borrow: the region SHA-512 runs in. The module carries nothing across its entries.
// Proved by a static_assert in sntrup761.c.
#ifndef PROTOCORE_SNTRUP761_BORROW
#define PROTOCORE_SNTRUP761_BORROW PROTOCORE_SHA512_BORROW
#endif
// The generator's borrow, split by offset in rng.c: the draw counter and the seeded flag, the seed,
// the nonce, the ratchet's next seed, then the region ChaCha20 runs in. 88 octets over the cipher's
// own borrow. This one is taken from the pool's PERSISTENT end and lives for the program, so the seed
// survives across calls and a reseed lands in the same bytes. Proved by a static_assert in rng.c.
#ifndef PROTOCORE_RNG_BORROW
#define PROTOCORE_RNG_BORROW (88 + PROTOCORE_CHACHA20_BORROW)
#endif

// SSH frames every outbound packet in the secure pool: the payload it carries is the session's own
// plaintext until the cipher runs over it. One transient payload plus one wire, live together while
// protocore_ssh_conn_send or an open_forwarded builds a message and frames it.
//
// A connection's own bytes are not here. Every one of them - the two key epochs, the DH ephemeral,
// the handshake constants, the wire, the packet MAC's and the handshake crypto's working bytes, and
// the receive buffers - is a named offset in the connection's compile-time storage (ssh_conn.h).
//
// The wire bound is derived in ssh_packet.h from this same SSH_PKT_BUF_SIZE, so the figure is
// stated here in units of it and proved against the real SSH_WIRE_CAP by a static_assert in
// ssh_conn.c. Three units cover the wire's framing overhead over a full payload, compression's
// expansion bound included, and the fourth is the payload.
#ifndef PROTOCORE_WORK_SSH_CONN
#define PROTOCORE_WORK_SSH_CONN (4u * (size_t)SSH_PKT_BUF_SIZE)
#endif

// The software TLS 1.3 handshake driver takes one borrow per connection from the secure pool's
// PERSISTENT end and splits it by offset: TX at 0, where a message is built to send and where a
// received record is opened; RX at PROTOCORE_TLS_CONN_MSG_CAP, which the worker fills with one record; and
// the terms after it - the key share, the ECDHE secret, the transcript hash in hand, the Finished
// MAC, and Transcript-Hash(CH..server Finished). Every offset is a multiple of 32, so the borrow
// stays aligned end to end.
#ifndef PROTOCORE_TLS_CONN_MSG_CAP
#define PROTOCORE_TLS_CONN_MSG_CAP 1024
#endif
#ifndef PROTOCORE_TLS_CONN_REC_CAP
#define PROTOCORE_TLS_CONN_REC_CAP 1024
#endif
// RFC 8446 sec 7.1 keys the schedule off the negotiated cipher suite's hash, so a term is 32 octets
// under a SHA-256 suite and 48 under a SHA-384 one. The layout is stated at the wider of the two and
// a connection reads back the length its suite bound (Tls13KsNs::len).
#ifndef PROTOCORE_TLS13_SECRET_MAX
#define PROTOCORE_TLS13_SECRET_MAX 48
#endif
// The key share, the ECDHE secret, the transcript hash in hand, the Finished MAC, and
// Transcript-Hash(CH..server Finished). The peer's public key is NOT one of these: it can be an
// RSA modulus, so it has its own region (PROTOCORE_TLS_CONN_PEERKEY_CAP) rather than a 32-byte term.
#ifndef PROTOCORE_TLS_CONN_TERMS
#define PROTOCORE_TLS_CONN_TERMS 5
#endif
#ifndef PROTOCORE_TLS_CONN_TERMS_CAP
#define PROTOCORE_TLS_CONN_TERMS_CAP ((size_t)PROTOCORE_TLS_CONN_TERMS * PROTOCORE_TLS13_SECRET_MAX)
#endif
// The transcript's working bytes, the parsed ClientHello, the key schedule, and the SHA-512 an
// Ed25519 signature runs through, which the driver reaches by pointer. Stated in bytes here and
// proved against their real sizes by a static_assert in handshake.c:
// 448 (TLS13_TRANSCRIPT_BORROW) + sizeof(Tls13ClientHello) + 2802 (TLS13_KS_BORROW) + 448
// (SHA512_BORROW), which is 3842 with the PQC arm off and 3850 with it on. Rounded up to a multiple
// of 32 so the offsets after it stay aligned.
#ifndef PROTOCORE_TLS_CONN_STATE_CAP
#define PROTOCORE_TLS_CONN_STATE_CAP 3872
#endif
// The peer's subjectPublicKey, kept from the Certificate that carried it to the CertificateVerify
// checked under it: the message buffer the certificate arrived in is reused by the next handshake
// message, so a view over it would dangle. An RSA-2048 RSAPublicKey SEQUENCE is a little over 256
// octets, a P-256 point 65 and an Ed25519 key 32, so this covers the widest and the header naming
// its algorithm and length.
#ifndef PROTOCORE_TLS_CONN_PEERKEY_CAP
#define PROTOCORE_TLS_CONN_PEERKEY_CAP 320
#endif
// The terms of one TLS 1.3 key schedule: early, handshake and master secrets; the four traffic
// secrets; the empty hash, the derived salt, the finished key, the zero IKM, and the Finished
// verify_data. The connection that runs the schedule owns the storage, so the extent is stated
// here and spent there: PROTOCORE_TLS13_KS_CAP.
#ifndef PROTOCORE_TLS13_KS_TERMS
#define PROTOCORE_TLS13_KS_TERMS 12
#endif
// The schedule's terms, then the bytes its HKDF works out of. One borrow, split by offset in
// key_schedule.h, taken by whichever connection runs the handshake. The HKDF figure is the SHA-384
// one because it is the larger of the two and a connection binds either.
#ifndef PROTOCORE_TLS13_KS_BORROW
#define PROTOCORE_TLS13_KS_BORROW                                                                                      \
    ((size_t)PROTOCORE_TLS13_KS_TERMS * PROTOCORE_TLS13_SECRET_MAX + PROTOCORE_HKDF_SHA384_BORROW)
#endif
// The bytes one running Transcript-Hash works out of, at the wider of the two suite hashes so the
// region does not depend on what the connection binds. Taken by the connection that keeps the
// transcript and passed to Tls13Ks.transcript_*.
#ifndef PROTOCORE_TLS13_TRANSCRIPT_BORROW
#if PROTOCORE_SHA384_BORROW > PROTOCORE_SHA256_BORROW
#define PROTOCORE_TLS13_TRANSCRIPT_BORROW PROTOCORE_SHA384_BORROW
#else
#define PROTOCORE_TLS13_TRANSCRIPT_BORROW PROTOCORE_SHA256_BORROW
#endif
#endif
#ifndef PROTOCORE_WORK_TLS_CONN
#define PROTOCORE_WORK_TLS_CONN                                                                                        \
    ((size_t)MAX_TLS_CONNS *                                                                                           \
     ((size_t)PROTOCORE_TLS_CONN_MSG_CAP + (size_t)PROTOCORE_TLS_CONN_REC_CAP + (size_t)PROTOCORE_TLS_CONN_TERMS_CAP + \
      (size_t)PROTOCORE_TLS_CONN_STATE_CAP + (size_t)PROTOCORE_TLS_CONN_PEERKEY_CAP))
#endif

/**
 * @brief Size in bytes of the per-slot SECURE pool (see mmgr/secure.h), DERIVED.
 *
 * Not a chosen number. Every borrow from this pool is a working set some module declares - see the
 * PROTOCORE_WORK_* constants, each proved against its struct's sizeof by a static_assert in the module that
 * owns it. The floor is the sum of the ones a build actually compiles.
 *
 * A sum, not a deepest-nest figure. The sum is a strict upper bound - correct however those working
 * sets nest under one another - whereas a nest depth is only correct while the call graph stays as it
 * is. This value must not be wrong, so it buys certainty with a little slack.
 *
 * A module whose working set grows past its declaration fails the build, naming itself, instead of
 * exhausting the pool at run time. Override PROTOCORE_SECURE_ARENA_SIZE to pin a size regardless.
 */
// Every module's borrow is declared here, above the arena guard: a static_assert in the
// module names it in every build, and pinning PROTOCORE_SECURE_ARENA_SIZE overrides only
// the sum below, never a declaration.
// The SSE module's bytes: one subscribe handler per route and the buffer a write frames its record
// in. Taken from the persistent end so it lasts the life of the program. Proved against the real
// split by a static_assert in sse.c.
#ifndef PROTOCORE_SSE_BORROW
#define PROTOCORE_SSE_BORROW (MAX_ROUTES * 8 + SSE_BUF_SIZE + 32)
#endif

// The WebSocket module's bytes: one handler set per route, the scratch a slot's bytes are staged in
// for the frame walk, and the outbound fragmentation size. Taken from the persistent end so it lasts
// the life of the program. Proved against the real split by a static_assert in websocket.c.
#ifndef PROTOCORE_WS_BORROW
#define PROTOCORE_WS_BORROW (MAX_ROUTES * 24 + RX_BUF_SIZE + 32) // WsRoute is three handlers
#endif

// The CSRF issuer's bytes: its HMAC secret and nonce counter, then the region the nested
// HMAC-SHA256 runs out of. Secure because the secret is key material, and taken from the persistent
// end so it lasts the life of the program. Proved against the real split by a static_assert in csrf.c.
#ifndef PROTOCORE_CSRF_BORROW
#define PROTOCORE_CSRF_BORROW (64 + PROTOCORE_HMAC_SHA256_BORROW)
#endif

// The authentication lockout table: one bucket per recently-seen source address. Secure because a
// bucket names who is being throttled, and taken from the persistent end so the table lasts the life
// of the program. Proved against sizeof(LockoutCtx) by a static_assert in auth_lockout.c.
#ifndef PROTOCORE_AUTH_LOCKOUT_BORROW
#define PROTOCORE_AUTH_LOCKOUT_BORROW ((size_t)PROTOCORE_AUTH_LOCKOUT_SLOTS * 48u)
#endif

// The trusted-proxy table a forwarded header is honored against. Secure because it names which peers
// may rewrite a client address, and taken from the persistent end so it lasts the life of the
// program. Proved against the real table by a static_assert in forwarded_trust.c.
#ifndef PROTOCORE_FORWARDED_TRUST_BORROW
#define PROTOCORE_FORWARDED_TRUST_BORROW ((size_t)PROTOCORE_TRUSTED_PROXY_MAX * 32u)
#endif

// The audit ring: the retained records, their cursors, the chain anchor and the sink. Secure
// because a record names who did what, and taken from the persistent end so the chain survives the
// requests it spans. Proved against sizeof(AuditCtx) by a static_assert in audit_log.c.
#ifndef PROTOCORE_AUDIT_LOG_BORROW
#define PROTOCORE_AUDIT_LOG_BORROW                                                                                     \
    ((size_t)PROTOCORE_AUDIT_LOG_ENTRIES * (PROTOCORE_AUDIT_MSG_LEN + PROTOCORE_AUDIT_HASH_LEN + 64u) + 128u)
#endif

// The web terminal's command callback, its WebSocket path and which slots are terminal browsers. Taken from the
// persistent end so it outlives the connections it tracks. Proved by a static_assert in web_terminal.c.
#ifndef PROTOCORE_WEB_TERMINAL_BORROW
#define PROTOCORE_WEB_TERMINAL_BORROW (MAX_PATH_LEN + MAX_WS_CONNS + 32u)
#endif

// The config store's open namespace name. Secure because a namespace names where settings live, and taken from the
// persistent end so it outlives the request that opened it. Proved by a static_assert in config_store.c.
#ifndef PROTOCORE_CONFIG_STORE_BORROW
#define PROTOCORE_CONFIG_STORE_BORROW (PROTOCORE_CONFIG_KEY_MAX + 32u)
#endif

// The relay listener's table: the published front-port binds, and one live bridge per relayed
// connection - each carrying the relay engine's two PROTOCORE_RELAY_BUF carry buffers. Taken from
// the persistent end so a bridge outlives the poll ticks it spans. Proved against
// sizeof(RelayListenerCtx) by a static_assert in relay_listener.c.
#ifndef PROTOCORE_RELAY_LISTENER_BORROW
#define PROTOCORE_RELAY_LISTENER_BORROW                                                                                \
    ((size_t)PROTOCORE_RELAY_MAX_PUBLISH * (PROTOCORE_RELAY_HOST_MAX + 16u) +                                          \
     (size_t)PROTOCORE_RELAY_MAX_CONNS * (2u * PROTOCORE_RELAY_BUF + 128u) + 64u)
#endif

// The southbound gateway's table: one entry per published radio port with its rate-limit window,
// plus the northbound sink, the topic prefix and the counters. Taken from the persistent end so a
// published port outlives the frames it carries. Proved against sizeof(GatewayCtx) by a
// static_assert in gateway.c.
#ifndef PROTOCORE_GATEWAY_BORROW
#define PROTOCORE_GATEWAY_BORROW ((size_t)PROTOCORE_GW_MAX_PORTS * 32u + 128u)
#endif

// The interface bridge's rule table: one address:port -> bus mapping per rule, each carrying a
// bind address and the bus target it forwards to. Taken from the persistent end so a published
// rule outlives the connections it serves. Proved against sizeof(BridgeCtx) by a static_assert in
// iface_bridge.c.
#ifndef PROTOCORE_IFACE_BRIDGE_BORROW
#define PROTOCORE_IFACE_BRIDGE_BORROW ((size_t)PROTOCORE_BRIDGE_MAX_RULES * 64u + 64u)
#endif

// The bridge glue's state: which listener each rule is published on, whether the handler and the
// shared SPI bus are up, and the one chunk a STREAM target moves per pump. Taken from the
// persistent end so a published bind outlives the connections it serves. Proved against
// sizeof(BridgeGlueCtx) by a static_assert in iface_bridge_hw.c.
#ifndef PROTOCORE_IFACE_BRIDGE_HW_BORROW
#define PROTOCORE_IFACE_BRIDGE_HW_BORROW                                                                               \
    ((size_t)PROTOCORE_BRIDGE_MAX_RULES * 16u + PROTOCORE_BRIDGE_STREAM_CHUNK + 64u)
#endif

// The edge cache's TLS half: the transport binding, the client cid of the in-flight https fetch, and
// the two session flags. Secure rather than plaintext because these name the client-TLS session the
// fetch runs over; the 18 KB of cached origin bytes beside them is the plaintext borrow
// (PROTOCORE_EDGE_PROXY_BORROW). Proved against sizeof(EdgeProxyTlsCtx) by a static_assert in
// edge_cache_proxy.c.
#ifndef PROTOCORE_EDGE_PROXY_TLS_BORROW
#define PROTOCORE_EDGE_PROXY_TLS_BORROW 128
#endif

// The WebDAV handler's state: the accessor root it resolves every path against, the 207 Multi-Status
// build buffer (PROTOCORE_WEBDAV_BUF_SIZE), one directory entry's name for the Depth-1 listing
// (PROTOCORE_FILESYSTEM_PATH_MAX), one streaming-PUT destination per connection slot, and the
// server-global lock table (PROTOCORE_DAV_LOCK_MAX entries of path + token). Taken from the
// persistent end so a lock outlives the request that took it. A literal rather than a formula
// because the lock terms are webdav.h's, which this file is included by rather than includes;
// proved against sizeof(DavCtx) by a static_assert in webdav_handler.c.
#ifndef PROTOCORE_WEBDAV_BORROW
#define PROTOCORE_WEBDAV_BORROW 4096
#endif

// The provisioning service's state: the softAP address the captive-portal DNS answers with, stamped
// by begin() from the interface the radio actually came up on. Taken from the persistent end so it
// outlives the DNS callbacks that read it. Proved against sizeof(ProvCtx) by a static_assert in
// provisioning_service.c.
#ifndef PROTOCORE_PROVISIONING_BORROW
#define PROTOCORE_PROVISIONING_BORROW 32
#endif

// The hot-swap binding: the state machine (state, failure run and threshold, probe interval and
// stamp, mount and fault counts) plus the four callbacks the application registered and the context
// it passes back. Taken from the persistent end so a volume's state survives the polls that watch
// it. A literal rather than a formula because the terms are hotswap.h's, which this file is
// included by rather than includes; proved against sizeof(HotswapCtx) by a static_assert in
// hotswap.c.
#ifndef PROTOCORE_HOTSWAP_BORROW
#define PROTOCORE_HOTSWAP_BORROW 128
#endif

// The SMBus transaction state: whether the Packet Error Code is on, and the frame a transaction is
// composed in - two address bytes, a command, a count, a 32-octet block and the PEC, which is the
// longest sequence any shape puts on the wire. Taken from the persistent end so the PEC setting
// outlives the transactions that use it. A literal rather than a formula because the terms are
// smbus.h's, which this file is included by rather than includes; proved against sizeof(SmbusCtx)
// by a static_assert in smbus.c.
#ifndef PROTOCORE_SMBUS_BORROW
#define PROTOCORE_SMBUS_BORROW 64
#endif

// The MPR121's I2C binding: the device address, the two-octet register frame, and the bring-up
// sequence (MPR121_INIT_MAX register/value pairs, written one pair at a time). Taken from the
// persistent end so the address set at begin() outlives the reads that use it. A literal rather
// than a formula because the terms are mpr121.h's, which this file is included by rather than
// includes; proved against sizeof(Mpr121Ctx) by a static_assert in mpr121.c.
#ifndef PROTOCORE_MPR121_BORROW
#define PROTOCORE_MPR121_BORROW 128
#endif

// The HMMD radar's UART binding: the frame reassembler (a 45-octet frame plus its cursor), the last
// decoded report (detect, distance and 16 gate energies), and the 64-octet chunk a poll reads into.
// Taken from the persistent end so a frame split across polls reassembles. A literal rather than a
// formula because the terms are hmmd.h's, which this file is included by rather than includes;
// proved against sizeof(HmmdCtx) by a static_assert in hmmd.c.
#ifndef PROTOCORE_HMMD_BORROW
#define PROTOCORE_HMMD_BORROW 256
#endif

// The LD2410 radar's UART binding: the frame reassembler (a 72-octet frame plus its cursor), the
// last decoded report with its 9 gate energies, the 64-octet receive chunk and the 16-octet command
// frame. Taken from the persistent end so a frame split across polls reassembles. A literal rather
// than a formula because the terms are ld2410's own; proved against sizeof(Ld2410Ctx) by a
// static_assert in ld2410.c.
#ifndef PROTOCORE_LD2410_BORROW
#define PROTOCORE_LD2410_BORROW 320
#endif

// The dashboard's state: the widget table the application registered, the current value per widget,
// the inbound-control callback, and the two route paths begin() composes - the SSE stream and the
// control socket, one MAX_PATH_LEN each. Taken from the persistent end so the layout outlives the
// requests that render it. Proved against sizeof(DashboardCtx) by a static_assert in dashboard.c.
#ifndef PROTOCORE_DASHBOARD_BORROW
#define PROTOCORE_DASHBOARD_BORROW ((size_t)PROTOCORE_DASHBOARD_MAX_WIDGETS * 4u + 2u * MAX_PATH_LEN + 64u)
#endif

// An I2C peripheral binding: the device address and the widest bus frame the part moves. Taken
// from the persistent end so the address set at begin() outlives the transfers that use it. One
// size covers them all - the widest frame here is a register byte plus a 16-bit value - and each
// module's own static_assert proves its context against it.
#ifndef PROTOCORE_I2C_DEVICE_BORROW
#define PROTOCORE_I2C_DEVICE_BORROW 32
#endif

// The bus capture's binding: the frame sink a capture delivers to, and whether one is running.
// Taken from the persistent end so the binding outlives the poll ticks it spans. Proved against
// sizeof(BusCaptureCtx) by a static_assert in bus_capture.c.
#ifndef PROTOCORE_BUS_CAPTURE_BORROW
#define PROTOCORE_BUS_CAPTURE_BORROW 32u
#endif

// A QUIC connection's context: the packet-number spaces, the stream table, and the TLS handshake
// with its traffic secrets. Secure rather than plaintext because of that last term - the bytes the
// connection owes its streams are the plaintext borrow beside it (PROTOCORE_QUIC_CONN_BORROW), and
// only these carry key material. One per connection, taken from the persistent end so it lasts the
// connection. Measured at 13904 bytes: the QuicTls inside it carries ks_store (TLS13_KS_BORROW) and
// two transcript contexts (TLS13_TRANSCRIPT_BORROW each), so it tracks both. Proved against
// sizeof(QuicConnCtx) by a static_assert in quic_conn.c.
#ifndef PROTOCORE_QUIC_CONN_CTX_BORROW
#define PROTOCORE_QUIC_CONN_CTX_BORROW 13952
#endif

#ifndef PROTOCORE_SECURE_ARENA_SIZE

// The modexp dominates: one backend or the other is compiled, never both. The software backend also
// walks the SSH host key in the pool and holds its private exponent there; the accelerated one hands
// the key to mbedtls instead.
#if PROTOCORE_HAS_HW_BIGNUM
#define PROTOCORE_SECURE_WORK_BIGNUM PROTOCORE_WORK_BIGNUM_HW
#else
#define PROTOCORE_SECURE_WORK_BIGNUM (PROTOCORE_WORK_BIGNUM_SW + PROTOCORE_WORK_SSH_HOST_KEY)
#endif

// Feature-gated terms: a build pays only for the code it compiled.
#if PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT || PROTOCORE_ENABLE_TLS || PROTOCORE_ENABLE_HTTP3 ||           \
    PROTOCORE_ENABLE_DTLS
#define PROTOCORE_SECURE_WORK_AEAD                                                                                     \
    (PROTOCORE_AESGCM_BORROW + PROTOCORE_CHACHAPOLY_BORROW + PROTOCORE_CHACHA20_BORROW + PROTOCORE_POLY1305_BORROW)
#else
#define PROTOCORE_SECURE_WORK_AEAD 0
#endif

// The SMB client's one sequential dialogue: the send and receive framing buffers, the NTLMv2
// response, the AUTHENTICATE blob, the second SESSION_SETUP body, the UTF-16 staging and the
// CHALLENGE target-info, plus the bytes this connection's crypto calls work out of. Measured at
// 6272 bytes for the default PROTOCORE_SMB_BUF of 1024, and scales with it. It holds the NTLM
// response and a keyed MAC context, so the secure end.
#ifndef PROTOCORE_SMB_CLIENT_BORROW
#define PROTOCORE_SMB_CLIENT_BORROW                                                                                    \
    (2u * PROTOCORE_SMB_BUF + 5u * (PROTOCORE_SMB_BUF / 2) + PROTOCORE_CRYPTO_BORROW_MAX)
#endif

#if PROTOCORE_ENABLE_SMB
#define PROTOCORE_SECURE_WORK_SMBCLIENT PROTOCORE_SMB_CLIENT_BORROW
#else
#define PROTOCORE_SECURE_WORK_SMBCLIENT 0
#endif

#if PROTOCORE_ENABLE_SMB
#define PROTOCORE_SECURE_WORK_SMB                                                                                      \
    (PROTOCORE_WORK_AESCCM + PROTOCORE_WORK_AES128GCM + PROTOCORE_MD_BORROW + PROTOCORE_KDF_BORROW)
#else
#define PROTOCORE_SECURE_WORK_SMB 0
#endif

#if PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_SECURE_WORK_SSHCIPHER PROTOCORE_AES256CTR_BORROW
#define PROTOCORE_SECURE_WORK_SSHCONN PROTOCORE_WORK_SSH_CONN
#else
#define PROTOCORE_SECURE_WORK_SSHCIPHER 0
#define PROTOCORE_SECURE_WORK_SSHCONN 0
#endif

// The dialling role's session: the negotiated methods, the relay connection, the KEX private scalar,
// and the hybrid decapsulation key when one is built. Measured at 368 bytes classical, 2136 with
// sntrup761 and 2768 with ML-KEM-768, which share one union so the larger arm is the size. Carries
// the KEX private and the hybrid secret key, so the secure end.
#ifndef PROTOCORE_SSH_CLIENT_BORROW
#if PROTOCORE_ENABLE_PQC_KEX
#define PROTOCORE_SSH_CLIENT_BORROW 2816u
#elif PROTOCORE_ENABLE_SSH_SNTRUP761
#define PROTOCORE_SSH_CLIENT_BORROW 2176u
#else
#define PROTOCORE_SSH_CLIENT_BORROW 384u
#endif
#endif

#if PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_SECURE_WORK_SSHCLIENT PROTOCORE_SSH_CLIENT_BORROW
#else
#define PROTOCORE_SECURE_WORK_SSHCLIENT 0
#endif

// Every SSH connection's span, which ssh.c hands out one slot at a time. This is the largest single
// borrow in the tree - 177472 bytes at the default sizing - and it is the bytes that used to sit in
// ssh.c's BSS, so the arena grows by exactly what BSS shed. Slot memory holds the session keys, so
// the secure end.
#if PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_SECURE_WORK_SSHSLOTS PROTOCORE_SSH_BORROW
#else
#define PROTOCORE_SECURE_WORK_SSHSLOTS 0
#endif

// The SSH RSA host key: the borrowed span holding the private exponent, and whether it has been
// parsed. Measured at 40 bytes; the key bytes themselves are a separate PROTOCORE_RSA_KEY_BYTES
// borrow this points at. A private exponent, so the secure end.
#ifndef PROTOCORE_SSH_RSA_BORROW
#define PROTOCORE_SSH_RSA_BORROW 64u
#endif

#if PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_SECURE_WORK_SSHRSA PROTOCORE_SSH_RSA_BORROW
#else
#define PROTOCORE_SECURE_WORK_SSHRSA 0
#endif

// The SSH channel layer's remote-forward bindings (RFC 4254 sec 7.1), the application hooks it
// calls back into, and the local-forward admission policy. Measured at 176 bytes with every SSH
// capability on, and scales with PROTOCORE_SSH_RFWD_MAX. Carries the bound addresses a forward
// names, so the secure end alongside the rest of the SSH state.
#ifndef PROTOCORE_SSH_CONNECTION_BORROW
#define PROTOCORE_SSH_CONNECTION_BORROW ((size_t)PROTOCORE_SSH_RFWD_MAX * (PROTOCORE_SSH_FWD_HOST_MAX + 16u) + 128u)
#endif

#if PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_SECURE_WORK_SSHCONNECTION PROTOCORE_SSH_CONNECTION_BORROW
#else
#define PROTOCORE_SECURE_WORK_SSHCONNECTION 0
#endif

// The SSH auth layer's per-slot state: failure counts, the sec 4 timeout stamps, the user and
// service each slot's state belongs to, the deferred password change, the armed keyboard-interactive
// exchange, and the three application verifiers. Measured at 112 bytes, 144 with
// PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE, and scales with MAX_SSH_CONNS. Carries user names and
// a password change in flight, so the secure end.
#ifndef PROTOCORE_SSH_AUTH_BORROW
#define PROTOCORE_SSH_AUTH_BORROW ((size_t)MAX_SSH_CONNS * 160u + 32u)
#endif

#if PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_SECURE_WORK_SSHAUTH PROTOCORE_SSH_AUTH_BORROW
#else
#define PROTOCORE_SECURE_WORK_SSHAUTH 0
#endif

// The SSH transport machine's host signing keys - the ed25519 seed and public half, the P-256
// scalar and point, whether each is loaded - and the runtime KEX preference. Measured at 164 bytes.
// Host private keys, so the secure end.
#ifndef PROTOCORE_SSH_TRANSPORT_BORROW
#define PROTOCORE_SSH_TRANSPORT_BORROW 192u
#endif

#if PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_SECURE_WORK_SSHTRANSPORT PROTOCORE_SSH_TRANSPORT_BORROW
#else
#define PROTOCORE_SECURE_WORK_SSHTRANSPORT 0
#endif

#if PROTOCORE_ENABLE_AUTH
#define PROTOCORE_SECURE_WORK_AUTH PROTOCORE_HTTP_AUTH_BORROW
#else
#define PROTOCORE_SECURE_WORK_AUTH 0
#endif

#if PROTOCORE_ENABLE_SSE
#define PROTOCORE_SECURE_WORK_SSE PROTOCORE_SSE_BORROW
#else
#define PROTOCORE_SECURE_WORK_SSE 0
#endif

// The HTTP/2 engine's bytes: one connection record per transport slot and the per-slot
// header-block bits. Taken from the persistent end so it lasts the life of the program. Proved
// against the real split by a static_assert in h2_server.c.
// One H2Conn: the frame and header-block cursors, the HPACK decoder table, the peer settings and
// the stream table. The exact width is h2_conn.h's, which protocore_config.h cannot see, so the
// static_assert in h2_server.c is what proves this covers it.
#ifndef PROTOCORE_H2_CONN_RECORD
#define PROTOCORE_H2_CONN_RECORD 32768
#endif

// HTTP/2 runs over TLS, so a connection's bytes are secure. The slot table holds the pointers;
// the connections hold the bytes.
#if PROTOCORE_ENABLE_HTTP2 && PROTOCORE_ENABLE_TLS
#define PROTOCORE_SECURE_WORK_H2_SERVER                                                                                \
    (PROTOCORE_H2_SERVER_BORROW +                                                                                      \
     (size_t)MAX_CONNS * (PROTOCORE_H2_CONN_RECORD + PROTOCORE_H2_MAX_FRAME + 2 * PROTOCORE_H2_HDR_BLOCK + 16))
#else
#define PROTOCORE_SECURE_WORK_H2_SERVER 0
#endif

#if PROTOCORE_ENABLE_WEBSOCKET
#define PROTOCORE_SECURE_WORK_WS PROTOCORE_WS_BORROW
#else
#define PROTOCORE_SECURE_WORK_WS 0
#endif

#if PROTOCORE_ENABLE_CSRF
#define PROTOCORE_SECURE_WORK_CSRF PROTOCORE_CSRF_BORROW
#else
#define PROTOCORE_SECURE_WORK_CSRF 0
#endif

#if PROTOCORE_ENABLE_AUTH_LOCKOUT
#define PROTOCORE_SECURE_WORK_LOCKOUT PROTOCORE_AUTH_LOCKOUT_BORROW
#else
#define PROTOCORE_SECURE_WORK_LOCKOUT 0
#endif

#if PROTOCORE_ENABLE_FORWARDED_TRUST
#define PROTOCORE_SECURE_WORK_FWDTRUST PROTOCORE_FORWARDED_TRUST_BORROW
#else
#define PROTOCORE_SECURE_WORK_FWDTRUST 0
#endif

#if PROTOCORE_ENABLE_AUDIT_LOG
#define PROTOCORE_SECURE_WORK_AUDIT PROTOCORE_AUDIT_LOG_BORROW
#else
#define PROTOCORE_SECURE_WORK_AUDIT 0
#endif

#if PROTOCORE_ENABLE_WEB_TERMINAL
#define PROTOCORE_SECURE_WORK_WEBTERM PROTOCORE_WEB_TERMINAL_BORROW
#else
#define PROTOCORE_SECURE_WORK_WEBTERM 0
#endif

#if PROTOCORE_ENABLE_CONFIG_STORE
#define PROTOCORE_SECURE_WORK_CFGSTORE PROTOCORE_CONFIG_STORE_BORROW
#else
#define PROTOCORE_SECURE_WORK_CFGSTORE 0
#endif

#if PROTOCORE_ENABLE_RELAY
#define PROTOCORE_SECURE_WORK_RELAYLISTEN PROTOCORE_RELAY_LISTENER_BORROW
#else
#define PROTOCORE_SECURE_WORK_RELAYLISTEN 0
#endif

#if PROTOCORE_ENABLE_GATEWAY
#define PROTOCORE_SECURE_WORK_GATEWAY PROTOCORE_GATEWAY_BORROW
#else
#define PROTOCORE_SECURE_WORK_GATEWAY 0
#endif

#if PROTOCORE_ENABLE_IFACE_BRIDGE
#define PROTOCORE_SECURE_WORK_IFACEBRIDGE PROTOCORE_IFACE_BRIDGE_BORROW
#else
#define PROTOCORE_SECURE_WORK_IFACEBRIDGE 0
#endif

#if PROTOCORE_ENABLE_IFACE_BRIDGE
#define PROTOCORE_SECURE_WORK_IFACEBRIDGEHW PROTOCORE_IFACE_BRIDGE_HW_BORROW
#else
#define PROTOCORE_SECURE_WORK_IFACEBRIDGEHW 0
#endif

#if PROTOCORE_ENABLE_EDGE_CACHE && PROTOCORE_ENABLE_EDGE_ORIGIN_TLS
#define PROTOCORE_SECURE_WORK_EDGEPROXYTLS PROTOCORE_EDGE_PROXY_TLS_BORROW
#else
#define PROTOCORE_SECURE_WORK_EDGEPROXYTLS 0
#endif

#if PROTOCORE_ENABLE_WEBDAV
#define PROTOCORE_SECURE_WORK_WEBDAV PROTOCORE_WEBDAV_BORROW
#else
#define PROTOCORE_SECURE_WORK_WEBDAV 0
#endif

#if PROTOCORE_ENABLE_PROVISIONING
#define PROTOCORE_SECURE_WORK_PROVISIONING PROTOCORE_PROVISIONING_BORROW
#else
#define PROTOCORE_SECURE_WORK_PROVISIONING 0
#endif

#if PROTOCORE_ENABLE_HOTSWAP
#define PROTOCORE_SECURE_WORK_HOTSWAP PROTOCORE_HOTSWAP_BORROW
#else
#define PROTOCORE_SECURE_WORK_HOTSWAP 0
#endif

#if PROTOCORE_ENABLE_SMBUS
#define PROTOCORE_SECURE_WORK_SMBUS PROTOCORE_SMBUS_BORROW
#else
#define PROTOCORE_SECURE_WORK_SMBUS 0
#endif

#if PROTOCORE_ENABLE_MPR121
#define PROTOCORE_SECURE_WORK_MPR121 PROTOCORE_MPR121_BORROW
#else
#define PROTOCORE_SECURE_WORK_MPR121 0
#endif

#if PROTOCORE_ENABLE_HMMD
#define PROTOCORE_SECURE_WORK_HMMD PROTOCORE_HMMD_BORROW
#else
#define PROTOCORE_SECURE_WORK_HMMD 0
#endif

#if PROTOCORE_ENABLE_LD2410
#define PROTOCORE_SECURE_WORK_LD2410 PROTOCORE_LD2410_BORROW
#else
#define PROTOCORE_SECURE_WORK_LD2410 0
#endif

#if PROTOCORE_ENABLE_DASHBOARD
#define PROTOCORE_SECURE_WORK_DASHBOARD PROTOCORE_DASHBOARD_BORROW
#else
#define PROTOCORE_SECURE_WORK_DASHBOARD 0
#endif

#if PROTOCORE_ENABLE_ADS1115
#define PROTOCORE_SECURE_WORK_ADS1115 PROTOCORE_I2C_DEVICE_BORROW
#else
#define PROTOCORE_SECURE_WORK_ADS1115 0
#endif

#if PROTOCORE_ENABLE_INA219
#define PROTOCORE_SECURE_WORK_INA219 PROTOCORE_I2C_DEVICE_BORROW
#else
#define PROTOCORE_SECURE_WORK_INA219 0
#endif

#if PROTOCORE_ENABLE_PCA9685
#define PROTOCORE_SECURE_WORK_PCA9685 PROTOCORE_I2C_DEVICE_BORROW
#else
#define PROTOCORE_SECURE_WORK_PCA9685 0
#endif

#if PROTOCORE_ENABLE_FDC2214
#define PROTOCORE_SECURE_WORK_FDC2214 PROTOCORE_I2C_DEVICE_BORROW
#else
#define PROTOCORE_SECURE_WORK_FDC2214 0
#endif

#if PROTOCORE_ENABLE_LDC1614
#define PROTOCORE_SECURE_WORK_LDC1614 PROTOCORE_I2C_DEVICE_BORROW
#else
#define PROTOCORE_SECURE_WORK_LDC1614 0
#endif

#if PROTOCORE_ENABLE_VL53L0X
#define PROTOCORE_SECURE_WORK_VL53L0X PROTOCORE_I2C_DEVICE_BORROW
#else
#define PROTOCORE_SECURE_WORK_VL53L0X 0
#endif

#if PROTOCORE_ENABLE_RTC
#define PROTOCORE_SECURE_WORK_RTC PROTOCORE_I2C_DEVICE_BORROW
#else
#define PROTOCORE_SECURE_WORK_RTC 0
#endif

#if PROTOCORE_ENABLE_SHT3X
#define PROTOCORE_SECURE_WORK_SHT3X PROTOCORE_I2C_DEVICE_BORROW
#else
#define PROTOCORE_SECURE_WORK_SHT3X 0
#endif

#if PROTOCORE_ENABLE_BUS_CAPTURE
#define PROTOCORE_SECURE_WORK_BUSCAPTURE PROTOCORE_BUS_CAPTURE_BORROW
#else
#define PROTOCORE_SECURE_WORK_BUSCAPTURE 0
#endif

#if PROTOCORE_TLS_SOFTWARE
#define PROTOCORE_SECURE_WORK_TLSCONN PROTOCORE_WORK_TLS_CONN
#else
#define PROTOCORE_SECURE_WORK_TLSCONN 0
#endif

// The HTTP/3 connection above it carries no key material, so its context sits in the plaintext
// borrow with the stream bytes and takes nothing from here.
#if PROTOCORE_ENABLE_HTTP3
#define PROTOCORE_SECURE_WORK_QUICCONN ((size_t)PROTOCORE_QUIC_MAX_CONNS * PROTOCORE_QUIC_CONN_CTX_BORROW)
#else
#define PROTOCORE_SECURE_WORK_QUICCONN 0
#endif

#if PROTOCORE_ENABLE_SHA1
#define PROTOCORE_SECURE_WORK_SHA1 PROTOCORE_SHA1_BORROW
#else
#define PROTOCORE_SECURE_WORK_SHA1 0
#endif

#if PROTOCORE_ENABLE_SHA3
#define PROTOCORE_SECURE_WORK_SHA3 PROTOCORE_SHA3_BORROW
#else
#define PROTOCORE_SECURE_WORK_SHA3 0
#endif

#if PROTOCORE_ENABLE_SHA256
#define PROTOCORE_SECURE_WORK_SHA256 PROTOCORE_SHA256_BORROW
#else
#define PROTOCORE_SECURE_WORK_SHA256 0
#endif

#if PROTOCORE_ENABLE_CURVE25519
#define PROTOCORE_SECURE_WORK_CURVE25519 PROTOCORE_CURVE25519_BORROW
#else
#define PROTOCORE_SECURE_WORK_CURVE25519 0
#endif

#if PROTOCORE_ENABLE_ED25519
#define PROTOCORE_SECURE_WORK_ED25519 PROTOCORE_ED25519_BORROW
#else
#define PROTOCORE_SECURE_WORK_ED25519 0
#endif

#if PROTOCORE_ENABLE_ECDSA
#define PROTOCORE_SECURE_WORK_ECDSA PROTOCORE_ECDSA_BORROW
#else
#define PROTOCORE_SECURE_WORK_ECDSA 0
#endif

#if PROTOCORE_ENABLE_RSA
#define PROTOCORE_SECURE_WORK_RSA PROTOCORE_RSA_BORROW
#else
#define PROTOCORE_SECURE_WORK_RSA 0
#endif

#if PROTOCORE_ENABLE_MLKEM
#define PROTOCORE_SECURE_WORK_MLKEM PROTOCORE_MLKEM_BORROW
#else
#define PROTOCORE_SECURE_WORK_MLKEM 0
#endif

#if PROTOCORE_ENABLE_SNTRUP761
#define PROTOCORE_SECURE_WORK_SNTRUP761 PROTOCORE_SNTRUP761_BORROW
#else
#define PROTOCORE_SECURE_WORK_SNTRUP761 0
#endif

// The record AEAD the QUIC, DTLS and TLS paths key per direction, one borrow carrying both the AEAD
// context and the header-protection block context. The SMB term below carries its own.
#if PROTOCORE_ENABLE_AES128GCM
#define PROTOCORE_SECURE_WORK_AES128GCM PROTOCORE_AES128GCM_BORROW
#else
#define PROTOCORE_SECURE_WORK_AES128GCM 0
#endif

// The generator takes its borrow from the pool's PERSISTENT end, once, for the program's life.
#if PROTOCORE_ENABLE_RNG
#define PROTOCORE_SECURE_WORK_RNG PROTOCORE_RNG_BORROW
#else
#define PROTOCORE_SECURE_WORK_RNG 0
#endif

// The resolver's own state: the span a record's owner name is walked into, the query in flight and
// the nameserver being asked, the query ID (RFC 1035 sec 4.1.1), the deadline it waits to, and the
// busy flag a second host waits on. The vendor arm holds the stack's reported address instead of
// the query. The spans it hands out are already taken from the secure end and its release wipes, so
// the handles to them are kept beside their bytes. Proved against sizeof(struct ResolverStorage) by
// a static_assert in dns_resolver.c.
#ifndef PROTOCORE_DNS_RESOLVER_BORROW
#define PROTOCORE_DNS_RESOLVER_BORROW 192u
#endif

#if PROTOCORE_NEED_DNS_RESOLVER
#define PROTOCORE_SECURE_WORK_DNSRESOLVER PROTOCORE_DNS_RESOLVER_BORROW
#else
#define PROTOCORE_SECURE_WORK_DNSRESOLVER 0
#endif

// What one pump of a handshake owes the wire: the server's whole flight fits in one build, and the
// seam sends it in one raw write. Sized by the largest flight this engine produces.
#ifndef PROTOCORE_TLS_SEAM_OUT_CAP
#define PROTOCORE_TLS_SEAM_OUT_CAP 2048
#endif

// The slot-indexed TLS surface: one TlsConn per connection slot, the per-connection configuration
// carrying its own ephemeral key and Hello random, the pcb each writes through, the credential this
// end presents, and the flight buffer above. A TlsConn holds its four traffic key generations
// inline and the credential is a signing seed, so these are key material and take the end whose
// release wipes. Proved against sizeof(struct TlsStorage) by a static_assert in tls.c.
// Per slot: sizeof(TlsConn) 3312 + sizeof(TlsConnConfig) 64 + the pcb 8 + the ephemeral key and
// Hello random 64, measured on the host at the default widths; 3584 rounds that up.
#ifndef PROTOCORE_TLS_BORROW
#define PROTOCORE_TLS_BORROW ((size_t)MAX_CONNS * 3584u + (size_t)PROTOCORE_TLS_SEAM_OUT_CAP + 128u)
#endif

#if PROTOCORE_TLS_SOFTWARE
#define PROTOCORE_SECURE_WORK_TLSSEAM PROTOCORE_TLS_BORROW
#else
#define PROTOCORE_SECURE_WORK_TLSSEAM 0
#endif

// One certificate signature check: the RSA modulus and exponent left-padded into the fields the
// verifier takes, then the region the algorithm itself works in. RSA sizes the second - a
// verification runs over the modulus - and the ECDSA and Ed25519 paths use a fraction of it.
// Proved against sizeof(X509VerifyCtx) + PROTOCORE_RSA_BORROW by a static_assert in x509_verify.c.
#ifndef PROTOCORE_X509_VERIFY_BORROW
#define PROTOCORE_X509_VERIFY_BORROW ((size_t)PROTOCORE_RSA_KEY_BYTES + 8u + PROTOCORE_RSA_BORROW)
#endif

#if PROTOCORE_ENABLE_X509
#define PROTOCORE_SECURE_WORK_X509VERIFY PROTOCORE_X509_VERIFY_BORROW
#else
#define PROTOCORE_SECURE_WORK_X509VERIFY 0
#endif

#define PROTOCORE_SECURE_ARENA_SIZE                                                                                    \
    (PROTOCORE_SECURE_WORK_BIGNUM + PROTOCORE_SECURE_WORK_AEAD + PROTOCORE_SECURE_WORK_SMB +                           \
     PROTOCORE_SECURE_WORK_SSHCIPHER + PROTOCORE_SECURE_WORK_SSHCONN + PROTOCORE_SECURE_WORK_TLSCONN +                 \
     PROTOCORE_SECURE_WORK_SHA1 + PROTOCORE_SECURE_WORK_SHA3 + PROTOCORE_SECURE_WORK_SHA256 +                          \
     PROTOCORE_SECURE_WORK_CURVE25519 + PROTOCORE_SECURE_WORK_ED25519 + PROTOCORE_SECURE_WORK_ECDSA +                  \
     PROTOCORE_SECURE_WORK_RSA + PROTOCORE_SECURE_WORK_MLKEM + PROTOCORE_SECURE_WORK_SNTRUP761 +                       \
     PROTOCORE_SECURE_WORK_AES128GCM + PROTOCORE_SECURE_WORK_ROUTETABLE + PROTOCORE_SECURE_WORK_AUTH +                 \
     PROTOCORE_SECURE_WORK_RNG + PROTOCORE_SECURE_WORK_QUICCONN + PROTOCORE_SECURE_WORK_WS +                           \
     PROTOCORE_SECURE_WORK_SSE + PROTOCORE_SECURE_WORK_H2_SERVER + PROTOCORE_SECURE_WORK_CSRF +                        \
     PROTOCORE_SECURE_WORK_LOCKOUT + PROTOCORE_SECURE_WORK_FWDTRUST + PROTOCORE_SECURE_WORK_AUDIT +                    \
     PROTOCORE_SECURE_WORK_WEBTERM + PROTOCORE_SECURE_WORK_CFGSTORE + PROTOCORE_SECURE_WORK_RELAYLISTEN +              \
     PROTOCORE_SECURE_WORK_GATEWAY + PROTOCORE_SECURE_WORK_IFACEBRIDGE + PROTOCORE_SECURE_WORK_IFACEBRIDGEHW +         \
     PROTOCORE_SECURE_WORK_BUSCAPTURE + PROTOCORE_SECURE_WORK_HMMD + PROTOCORE_SECURE_WORK_LD2410 +                    \
     PROTOCORE_SECURE_WORK_DASHBOARD + PROTOCORE_SECURE_WORK_ADS1115 + PROTOCORE_SECURE_WORK_RTC +                     \
     PROTOCORE_SECURE_WORK_SHT3X + PROTOCORE_SECURE_WORK_INA219 + PROTOCORE_SECURE_WORK_PCA9685 +                      \
     PROTOCORE_SECURE_WORK_MPR121 + PROTOCORE_SECURE_WORK_FDC2214 + PROTOCORE_SECURE_WORK_LDC1614 +                    \
     PROTOCORE_SECURE_WORK_VL53L0X + PROTOCORE_SECURE_WORK_SMBUS + PROTOCORE_SECURE_WORK_HOTSWAP +                     \
     PROTOCORE_SECURE_WORK_PROVISIONING + PROTOCORE_SECURE_WORK_WEBDAV + PROTOCORE_SECURE_WORK_EDGEPROXYTLS +          \
     PROTOCORE_SECURE_WORK_DNSRESOLVER + PROTOCORE_SECURE_WORK_TLSSEAM + PROTOCORE_SECURE_WORK_X509VERIFY +            \
     PROTOCORE_SECURE_WORK_COAPSSERVER + PROTOCORE_SECURE_WORK_MQTT + PROTOCORE_SECURE_WORK_SNMPNOTIFY +               \
     PROTOCORE_SECURE_WORK_HTTPCLIENT + PROTOCORE_SECURE_WORK_SMTP + PROTOCORE_SECURE_WORK_WSCLIENT +                  \
     PROTOCORE_SECURE_WORK_SNMPV3 + PROTOCORE_SECURE_WORK_SNMPAGENT + PROTOCORE_SECURE_WORK_OAUTH2 +                   \
     PROTOCORE_SECURE_WORK_SSHCLIENT + PROTOCORE_SECURE_WORK_SSHTRANSPORT + PROTOCORE_SECURE_WORK_SSHAUTH +            \
     PROTOCORE_SECURE_WORK_SSHCONNECTION + PROTOCORE_SECURE_WORK_SSHRSA + PROTOCORE_SECURE_WORK_SSHSLOTS +             \
     PROTOCORE_SECURE_WORK_SMBCLIENT + 256) // + 256: alignment round-up across the individual borrows
#endif

// Both of these are struct members (TcpConn::proto, TcpConn::iface, Listener::proto, and a route's
// interface gate), so their width is per-slot BSS rather than a detail of the type. Pinned here
// because the pool sizes in this file are what the footprint is computed from.
static_assert(sizeof(ProtoConn) == 1, "ProtoConn must stay one byte: it is a per-slot struct member");
static_assert(sizeof(protocore_if_kind) == 1, "protocore_if_kind must stay one byte: it is a per-slot struct member");

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

#if PROTOCORE_ENABLE_WEBSOCKET && PROTOCORE_ENABLE_SSE
#if MAX_WS_CONNS + MAX_SSE_CONNS > MAX_CONNS
#error "ProtoCore: MAX_WS_CONNS + MAX_SSE_CONNS must not exceed MAX_CONNS"
#endif
#elif PROTOCORE_ENABLE_WEBSOCKET
#if MAX_WS_CONNS > MAX_CONNS
#error "ProtoCore: MAX_WS_CONNS must not exceed MAX_CONNS"
#endif
#elif PROTOCORE_ENABLE_SSE
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

#if PROTOCORE_ENABLE_FILE_SERVING && FILE_CHUNK_SIZE > RX_BUF_SIZE
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
#if PROTOCORE_ENABLE_OIDC && !PROTOCORE_ENABLE_SSH && (PROTOCORE_WORKER_TASK_STACK < PROTOCORE_WORKER_STACK_RSA_MIN)
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_WORKER_TASK_STACK is below PROTOCORE_WORKER_STACK_RSA_MIN; RSA-2048 verification (OIDC) needs ~7 KB of worker stack - raise PROTOCORE_WORKER_TASK_STACK (>= 8192) or marshal RSA verifies onto a dedicated larger-stack task"
#endif

// SSH additionally can negotiate curve25519-sha256 + ssh-ed25519, whose software field
// arithmetic peaks at ~10.5 KB of worker stack (deeper than the RSA path). Enforce the
// higher floor so a lowered stack is caught at build time instead of tripping the task
// stack canary on the first modern-crypto handshake.
#if (PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT || PROTOCORE_ENABLE_HTTP3) &&                                 \
    (PROTOCORE_WORKER_TASK_STACK < PROTOCORE_WORKER_STACK_CURVE_MIN)
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_WORKER_TASK_STACK is below PROTOCORE_WORKER_STACK_CURVE_MIN; SSH (server or reverse-SSH client) and HTTP/3 (QUIC TLS-1.3) curve25519/ed25519 need ~10.5 KB of worker stack - raise PROTOCORE_WORKER_TASK_STACK (>= 12288) or marshal the handshake onto a dedicated larger-stack task"
#endif

// The PQ/T hybrid KEX (PROTOCORE_ENABLE_PQC_KEX) runs ML-KEM-768 Encaps in the handshake path, whose NTT
// + sampling peak at ~7 KB of worker stack on top of the classical curve/ed25519 work. Enforce a
// higher floor so it is caught at build time rather than overflowing on the first hybrid handshake.
#ifndef PROTOCORE_WORKER_STACK_PQC_MIN
#define PROTOCORE_WORKER_STACK_PQC_MIN 16384
#endif
#if PROTOCORE_ENABLE_PQC_KEX && !PROTOCORE_ENABLE_SSH_SNTRUP761 &&                                                     \
    (PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT || PROTOCORE_ENABLE_HTTP3) &&                                 \
    (PROTOCORE_WORKER_TASK_STACK < PROTOCORE_WORKER_STACK_PQC_MIN)
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_WORKER_TASK_STACK is below PROTOCORE_WORKER_STACK_PQC_MIN; the ML-KEM-768 hybrid KEX (PROTOCORE_ENABLE_PQC_KEX) needs ~7 KB more worker stack - raise PROTOCORE_WORKER_TASK_STACK (>= 16384) or marshal the handshake onto a dedicated larger-stack task"
#endif

// sntrup761x25519-sha512 (PROTOCORE_ENABLE_SSH_SNTRUP761, on by default with the hybrid) is heavier than
// ML-KEM: the server runs Encaps (~22 KB), the reverse-SSH client runs KeyGen+Decaps whose FO
// re-encrypt peaks ~32 KB. Enforce the matching floor so it is caught at build time.
#ifndef PROTOCORE_WORKER_STACK_SNTRUP_MIN
#if defined(PROTOCORE_ENABLE_SSH_CLIENT) && PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_WORKER_STACK_SNTRUP_MIN 40960
#else
#define PROTOCORE_WORKER_STACK_SNTRUP_MIN 32768
#endif
#endif
// sntrup761x25519-sha512 is an SSH key exchange; only an SSH server / reverse-SSH client runs its heavy
// KeyGen/Encaps/Decaps on the worker stack. HTTP/3's PQC is the lighter ML-KEM hybrid, so an HTTP/3-only
// build (PROTOCORE_ENABLE_SSH_SNTRUP761 defaults on with PQC but is dormant without SSH) must not trip this.
#if PROTOCORE_ENABLE_SSH_SNTRUP761 && (PROTOCORE_ENABLE_SSH || PROTOCORE_ENABLE_SSH_CLIENT) &&                         \
    (PROTOCORE_WORKER_TASK_STACK < PROTOCORE_WORKER_STACK_SNTRUP_MIN)
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_WORKER_TASK_STACK is below PROTOCORE_WORKER_STACK_SNTRUP_MIN; sntrup761x25519-sha512 needs ~32 KB worker stack for the server Encaps and ~40 KB for the reverse-SSH client KeyGen+Decaps - raise PROTOCORE_WORKER_TASK_STACK (>= 32768 server / 40960 client), set PROTOCORE_ENABLE_SSH_SNTRUP761 0 to keep ML-KEM only, or marshal the handshake onto a dedicated larger-stack task"
#endif

#if PROTOCORE_ENABLE_TLS
#if MAX_TLS_CONNS < 1 || MAX_TLS_CONNS > MAX_CONNS
#error "ProtoCore: MAX_TLS_CONNS must be between 1 and MAX_CONNS"
#endif
#if PROTOCORE_TLS_ARENA_SIZE < 8192
#error "ProtoCore: PROTOCORE_TLS_ARENA_SIZE is far too small for a TLS handshake"
#endif
// Concurrent TLS guard: the whole arena is static .bss and the ESP32 internal
// dram0_0_seg ceiling is only ~122 KB (ROM-reserved at both ends), so a 2nd
// connection's arena overflows the link. Reject MAX_TLS_CONNS > 1 with a clear
// message unless the arena is offloaded to PSRAM or the build was consciously sized -
// far friendlier than the raw "region `dram0_0_seg' overflowed" linker error.
#if PROTOCORE_HAS_BOUNDED_DRAM && (MAX_TLS_CONNS > 1) && !PROTOCORE_TLS_ARENA_IN_PSRAM &&                              \
    !PROTOCORE_TLS_ACK_MULTI_CONN_DRAM
#error                                                                                                                 \
    "ProtoCore: MAX_TLS_CONNS > 1 - the static TLS arena will not fit the ~122 KB internal dram0_0_seg. Pick a path (docs/KNOWN_LIMITATIONS.md): set PROTOCORE_TLS_ARENA_IN_PSRAM=1 on a PSRAM board, OR shrink records via a custom ESP-IDF build (CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN + PROTOCORE_TLS_MAX_FRAG_LEN), OR reclaim internal DRAM; then set PROTOCORE_TLS_ACK_MULTI_CONN_DRAM=1 to confirm."
#endif
#endif

// HTTP/2's per-connection engine pool (~MAX_CONNS x 28 KB) cannot fit internal DRAM alongside
// TLS, so it must live in PSRAM. Fail fast with guidance instead of the raw linker overflow.
#if PROTOCORE_ENABLE_HTTP2 && PROTOCORE_HAS_BOUNDED_DRAM && !PROTOCORE_H2_POOL_IN_PSRAM
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_HTTP2 needs PSRAM - the HTTP/2 engine pool (~MAX_CONNS x 28 KB) overflows the ~122 KB internal dram0_0_seg alongside TLS. Set PROTOCORE_H2_POOL_IN_PSRAM=1 on a PSRAM board (S3 / P4 / WROVER) built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y (tools/psram/README.md)."
#endif

#if PROTOCORE_ENABLE_SNMP
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

#if PROTOCORE_ENABLE_COAP
#if PROTOCORE_COAP_MAX_RESOURCES < 1
#error "ProtoCore: PROTOCORE_COAP_MAX_RESOURCES must be >= 1"
#endif
#if PROTOCORE_COAP_MAX_PATH < 2
#error "ProtoCore: PROTOCORE_COAP_MAX_PATH must be >= 2 (minimum: \"/\")"
#endif
#if PROTOCORE_COAP_MAX_PAYLOAD < 1
#error "ProtoCore: PROTOCORE_COAP_MAX_PAYLOAD must be >= 1"
#endif
#if PROTOCORE_COAP_MSG_BUF_SIZE < (PROTOCORE_COAP_MAX_PAYLOAD + 16)
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_COAP_MSG_BUF_SIZE must be >= PROTOCORE_COAP_MAX_PAYLOAD + 16 (header + token + Content-Format option + payload marker)"
#endif
#endif

#if PROTOCORE_ENABLE_AUTH && MAX_AUTH_LEN < 2
#error "ProtoCore: MAX_AUTH_LEN must be >= 2 when PROTOCORE_ENABLE_AUTH is set"
#endif

#if PROTOCORE_ENABLE_PER_IP_THROTTLE
#if PROTOCORE_PER_IP_THROTTLE_SLOTS < 1
#error "ProtoCore: PROTOCORE_PER_IP_THROTTLE_SLOTS must be >= 1 when PROTOCORE_ENABLE_PER_IP_THROTTLE is set"
#endif
#if PROTOCORE_PER_IP_THROTTLE_MAX < 1
#error "ProtoCore: PROTOCORE_PER_IP_THROTTLE_MAX must be >= 1 when PROTOCORE_ENABLE_PER_IP_THROTTLE is set"
#endif
#endif

#if PROTOCORE_ENABLE_IP_ALLOWLIST && PROTOCORE_IP_ALLOWLIST_SLOTS < 1
#error "ProtoCore: PROTOCORE_IP_ALLOWLIST_SLOTS must be >= 1 when PROTOCORE_ENABLE_IP_ALLOWLIST is set"
#endif

#if PROTOCORE_ENABLE_AUTH_LOCKOUT
#if !PROTOCORE_ENABLE_AUTH
#error "ProtoCore: PROTOCORE_ENABLE_AUTH_LOCKOUT requires PROTOCORE_ENABLE_AUTH"
#endif
#if PROTOCORE_AUTH_LOCKOUT_SLOTS < 1
#error "ProtoCore: PROTOCORE_AUTH_LOCKOUT_SLOTS must be >= 1 when PROTOCORE_ENABLE_AUTH_LOCKOUT is set"
#endif
#if PROTOCORE_AUTH_LOCKOUT_THRESHOLD < 1
#error "ProtoCore: PROTOCORE_AUTH_LOCKOUT_THRESHOLD must be >= 1 when PROTOCORE_ENABLE_AUTH_LOCKOUT is set"
#endif
#if PROTOCORE_AUTH_LOCKOUT_BASE_MS < 1 || PROTOCORE_AUTH_LOCKOUT_MAX_MS < PROTOCORE_AUTH_LOCKOUT_BASE_MS
#error "ProtoCore: need 1 <= PROTOCORE_AUTH_LOCKOUT_BASE_MS <= PROTOCORE_AUTH_LOCKOUT_MAX_MS"
#endif
// The backoff doubles a uint32 capped at MAX_MS, so MAX_MS must leave headroom for
// one more shift (cap <= 0x80000000 => cap<<1 fits in uint32 without overflow).
#if PROTOCORE_AUTH_LOCKOUT_MAX_MS > 0x80000000
#error "ProtoCore: PROTOCORE_AUTH_LOCKOUT_MAX_MS must be <= 0x80000000 (2147483648)"
#endif
#endif

#if PROTOCORE_ENABLE_FORWARDED_TRUST
#if !PROTOCORE_ENABLE_AUTH_LOCKOUT
#error "ProtoCore: PROTOCORE_ENABLE_FORWARDED_TRUST requires PROTOCORE_ENABLE_AUTH_LOCKOUT"
#endif
#if PROTOCORE_TRUSTED_PROXY_MAX < 1
#error "ProtoCore: PROTOCORE_TRUSTED_PROXY_MAX must be >= 1 when PROTOCORE_ENABLE_FORWARDED_TRUST is set"
#endif
#endif

#if PROTOCORE_ENABLE_WEBDAV
#if !PROTOCORE_ENABLE_FILE_SERVING
#error "ProtoCore: PROTOCORE_ENABLE_WEBDAV requires PROTOCORE_ENABLE_FILE_SERVING"
#endif
#if PROTOCORE_WEBDAV_BUF_SIZE < 256
#error "ProtoCore: PROTOCORE_WEBDAV_BUF_SIZE must be >= 256"
#endif
#if PROTOCORE_METHOD_BUF_SIZE < 10
#error "ProtoCore: PROTOCORE_METHOD_BUF_SIZE must be >= 10 when PROTOCORE_ENABLE_WEBDAV is set (PROPPATCH)"
#endif
#endif

#if PROTOCORE_NEED_MODBUS
#if PROTOCORE_MODBUS_COILS < 1 || PROTOCORE_MODBUS_DISCRETE_INPUTS < 1 || PROTOCORE_MODBUS_HOLDING_REGS < 1 ||         \
    PROTOCORE_MODBUS_INPUT_REGS < 1
#error "ProtoCore: each PROTOCORE_MODBUS_* table size must be >= 1 when PROTOCORE_ENABLE_MODBUS is set"
#endif
#endif

#if PROTOCORE_ENABLE_KEEPALIVE && PROTOCORE_KEEPALIVE_MAX_REQUESTS < 1
#error "ProtoCore: PROTOCORE_KEEPALIVE_MAX_REQUESTS must be >= 1 when PROTOCORE_ENABLE_KEEPALIVE is set"
#endif

#if PROTOCORE_ENABLE_RANGE && !PROTOCORE_ENABLE_FILE_SERVING && !PROTOCORE_ENABLE_EDGE_CACHE
#error "ProtoCore: PROTOCORE_ENABLE_RANGE requires PROTOCORE_ENABLE_FILE_SERVING or PROTOCORE_ENABLE_EDGE_CACHE"
#endif

// The portable TLS arm authenticates by raw public key and asserts on PROTOCORE_ENABLE_TLS_RPK, so it is
// the third thing that carries the extension, alongside DTLS and HTTP/3.
#if PROTOCORE_ENABLE_TLS_RPK && !(PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_HTTP3 || PROTOCORE_TLS_SOFTWARE)
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_TLS_RPK requires PROTOCORE_ENABLE_DTLS, PROTOCORE_ENABLE_HTTP3 or the portable TLS arm (PROTOCORE_ENABLE_TLS without a vendor stack)"
#endif

#if PROTOCORE_ENABLE_COAP_BLOCK
#if !PROTOCORE_ENABLE_COAP
#error "ProtoCore: PROTOCORE_ENABLE_COAP_BLOCK requires PROTOCORE_ENABLE_COAP"
#endif
#if PROTOCORE_COAP_BLOCK_SZX_MAX > 6
#error "ProtoCore: PROTOCORE_COAP_BLOCK_SZX_MAX must be <= 6 (block size 2^(SZX+4); SZX 7 is reserved)"
#endif
#if PROTOCORE_COAP_MSG_BUF_SIZE < ((1 << (PROTOCORE_COAP_BLOCK_SZX_MAX + 4)) + 16)
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_COAP_MSG_BUF_SIZE must hold one full block (2^(PROTOCORE_COAP_BLOCK_SZX_MAX+4)) + 16 header/option bytes"
#endif
#if PROTOCORE_COAP_BLOCK1_MAX < (1 << (PROTOCORE_COAP_BLOCK_SZX_MAX + 4))
#error "ProtoCore: PROTOCORE_COAP_BLOCK1_MAX must be >= one block (2^(PROTOCORE_COAP_BLOCK_SZX_MAX+4))"
#endif
#endif

#if PROTOCORE_ENABLE_SSH_ZLIB
// Window must be a power of two in [256, 32768] (32 KB is zlib's max; the client's inflate window).
#if (PROTOCORE_SSH_ZLIB_WINDOW & (PROTOCORE_SSH_ZLIB_WINDOW - 1)) != 0 || PROTOCORE_SSH_ZLIB_WINDOW < 256 ||           \
    PROTOCORE_SSH_ZLIB_WINDOW > 32768
#error "ProtoCore: PROTOCORE_SSH_ZLIB_WINDOW must be a power of two in [256, 32768]"
#endif
// Positions index a uint16 hash chain, so window + max-input must stay under 65535.
#if (PROTOCORE_SSH_ZLIB_WINDOW + PROTOCORE_SSH_ZLIB_MAX_IN) > 65534
#error "ProtoCore: PROTOCORE_SSH_ZLIB_WINDOW + PROTOCORE_SSH_ZLIB_MAX_IN must be <= 65534"
#endif
// The compressor must accept a full packet payload, else a max-size outbound packet would fail to
// compress and drop, desyncing the stateful stream.
#if PROTOCORE_SSH_ZLIB_MAX_IN < SSH_PKT_BUF_SIZE
#error "ProtoCore: PROTOCORE_SSH_ZLIB_MAX_IN must be >= SSH_PKT_BUF_SIZE"
#endif
// The per-connection compression pool is ~80 KB: the s2c deflate work buffer + hash chain (~48 KB)
// plus the c2s 32 KB context-takeover inflate window. On ARDUINO pick a path: offload it to PSRAM
// (PROTOCORE_SSH_ZLIB_IN_PSRAM, a PSRAM board built with the BSS-in-PSRAM core), or acknowledge the
// internal-DRAM cost (PROTOCORE_SSH_ZLIB_ACK_DRAM, fine for MAX_SSH_CONNS=1 without TLS on a roomy S3 / P4).
// Fail fast with guidance instead of a raw linker overflow.
#if PROTOCORE_HAS_BOUNDED_DRAM && !PROTOCORE_SSH_ZLIB_IN_PSRAM && !PROTOCORE_SSH_ZLIB_ACK_DRAM
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_SSH_ZLIB - the per-connection compression pool is ~80 KB (s2c deflate ~48 KB + the c2s 32 KB inflate window). Set PROTOCORE_SSH_ZLIB_IN_PSRAM=1 on a PSRAM board (S3 / P4 / WROVER, core built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y, tools/psram/README.md), OR set PROTOCORE_SSH_ZLIB_ACK_DRAM=1 to accept the internal-DRAM cost (fits MAX_SSH_CONNS=1 without TLS on a roomy chip)."
#endif
#endif

#if PROTOCORE_ENABLE_WEBSOCKET && WS_FRAME_SIZE < 2
#error "ProtoCore: WS_FRAME_SIZE must be >= 2 when PROTOCORE_ENABLE_WEBSOCKET is set"
#endif

#if PROTOCORE_ENABLE_SSE && SSE_BUF_SIZE < 8
#error "ProtoCore: SSE_BUF_SIZE must be >= 8 when PROTOCORE_ENABLE_SSE is set"
#endif

#if PROTOCORE_ENABLE_MULTIPART && MAX_MULTIPART_PARTS < 1
#error "ProtoCore: MAX_MULTIPART_PARTS must be >= 1 when PROTOCORE_ENABLE_MULTIPART is set"
#endif

#if RESP_HDR_BUF_SIZE < 128
#error "ProtoCore: RESP_HDR_BUF_SIZE must be >= 128 (status line + headers + CORS block)"
#endif

#if PROTOCORE_ENABLE_WEBSOCKET && WS_HDR_BUF_SIZE < 128
#error "ProtoCore: WS_HDR_BUF_SIZE must be >= 128 when PROTOCORE_ENABLE_WEBSOCKET is set"
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
// value. An optional feature's group is wrapped in that feature's PROTOCORE_ENABLE_* flag
// (resolved above), so a knob only exists when its feature is built.
//
// What is NOT here: protocol- and algorithm-fixed constants (wire opcodes, magic bytes,
// crypto digest / block sizes, spec-mandated PDU / field widths, the deflate/inflate
// scratch sizes a static_assert pins to the table layout). Those are not knobs - changing
// them breaks conformance - so they stay in their feature file next to the code they bind.

// -- Core: protocol dispatch + shared outbound transport (always built) --
/** @brief Size of the protocol-handler dispatch table; must exceed the largest ProtoConn id. */
#ifndef PROTO_MAX_HANDLERS
#define PROTO_MAX_HANDLERS 12
#endif
// proto_register / proto_get index this table by ProtoConn id, so it must be wide enough for every id.
static_assert((unsigned)PROTO_UDP < PROTO_MAX_HANDLERS, "PROTO_MAX_HANDLERS must exceed the largest ProtoConn id");
/** @brief Reverse-SSH tunnel: max concurrent forwarded-tcpip channels bridged at once. A relay that
 * forwards to a web UI opens one channel per inbound TCP connection, so this bounds concurrency. */
#if PROTOCORE_ENABLE_SSH_CLIENT
#ifndef PROTOCORE_SSH_CLIENT_MAX_CHANNELS
#define PROTOCORE_SSH_CLIENT_MAX_CHANNELS 4
#endif
#if PROTOCORE_SSH_CLIENT_MAX_CHANNELS < 1
#error "ProtoCore: PROTOCORE_SSH_CLIENT_MAX_CHANNELS must be >= 1"
#endif
#endif

// The FTP session driver holds a control connection and a data connection at the same time; with a
// smaller pool the data open would fail after login and every transfer would abort mid-session.
#if PROTOCORE_ENABLE_FTP_SESSION && (PROTOCORE_CLIENT_CONNS < 2)
#error "ProtoCore: PROTOCORE_ENABLE_FTP_SESSION needs PROTOCORE_CLIENT_CONNS >= 2 (control + data)"
#endif

// The reverse-SSH tunnel needs the relay connection plus one local bridge per forwarded channel.
#if PROTOCORE_ENABLE_SSH_CLIENT && (PROTOCORE_CLIENT_CONNS < 1 + PROTOCORE_SSH_CLIENT_MAX_CHANNELS)
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_SSH_CLIENT needs PROTOCORE_CLIENT_CONNS >= 1 + PROTOCORE_SSH_CLIENT_MAX_CHANNELS (relay + one local bridge per channel)"
#endif

/**
 * @brief Max stored size of the exchange value the client sends (RFC 4253 sec 8 'e', RFC 8731 Q_C,
 *        or a hybrid C_INIT), kept for the exchange hash.
 *
 * Worst case for the build: an ML-KEM-768 ek (1184) or sntrup761 pk (1158) followed by the 32-byte
 * X25519 public, else the 256-byte dh-group14 'e'. ssh_transport.c static_asserts this against
 * MLKEM768_EK_BYTES and PROTOCORE_SNTRUP761_PK_BYTES, which only that translation unit can see.
 */
#ifndef PROTOCORE_SSH_CPUB_MAX
#if PROTOCORE_ENABLE_PQC_KEX
#define PROTOCORE_SSH_CPUB_MAX 1216
#elif PROTOCORE_ENABLE_SSH_SNTRUP761
#define PROTOCORE_SSH_CPUB_MAX 1190
#else
#define PROTOCORE_SSH_CPUB_MAX 256
#endif
#endif

#if PROTOCORE_ENABLE_OPCUA
// -- OPC UA (services/fieldbus/opcua) --
#ifndef PROTOCORE_OPCUA_BUF
#define PROTOCORE_OPCUA_BUF 8192 ///< Server's advertised buffer / max-message size for the handshake.
#endif
#ifndef PROTOCORE_OPCUA_READ_MAX
#define PROTOCORE_OPCUA_READ_MAX 8 ///< max NodesToRead handled per ReadRequest.
#endif
#ifndef PROTOCORE_OPCUA_BROWSE_MAX
#define PROTOCORE_OPCUA_BROWSE_MAX 4 ///< max NodesToBrowse handled per BrowseRequest.
#endif
#ifndef PROTOCORE_OPCUA_REF_MAX
#define PROTOCORE_OPCUA_REF_MAX 8 ///< max references returned per browsed node.
#endif
#ifndef PROTOCORE_OPCUA_WRITE_MAX
#define PROTOCORE_OPCUA_WRITE_MAX 8 ///< max NodesToWrite handled per WriteRequest.
#endif

// An Axes Browse returns one reference per axis, so the axis count must fit a single Browse response.
#if PROTOCORE_ENABLE_ROBOTICS && (PROTOCORE_ROBOTICS_AXES > PROTOCORE_OPCUA_REF_MAX)
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ROBOTICS_AXES must be <= PROTOCORE_OPCUA_REF_MAX (an Axes Browse returns one reference per axis)"
#endif
// Advertised server identity (endpoint descriptions), overridable per deployment; the app may
// also set these at runtime via protocore_opcua_set_endpoint_url() / the OpcUaServerInfo it passes.
#ifndef PROTOCORE_OPCUA_DEFAULT_ENDPOINT
#define PROTOCORE_OPCUA_DEFAULT_ENDPOINT "opc.tcp://localhost:4840" ///< default endpoint URL.
#endif
#ifndef PROTOCORE_OPCUA_DEFAULT_APP_URI
#define PROTOCORE_OPCUA_DEFAULT_APP_URI "urn:det:opcua:server" ///< default ApplicationUri.
#endif
#ifndef PROTOCORE_OPCUA_DEFAULT_APP_NAME
#define PROTOCORE_OPCUA_DEFAULT_APP_NAME "protocore_opcua_server" ///< default ApplicationName.
#endif
#endif // PROTOCORE_ENABLE_OPCUA

#endif // PROTOCORE_FEATURE_EN_ERROR_H
