// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file feature_dependency_en.h
 * @brief The flags a build does not set: the ones another flag decides.
 *
 * Most features are independent and are stated directly in protocore_config.h. These are the rest -
 * a flag whose value is read off another flag, or one an enabled feature forces on because it cannot
 * work without it. Reached after every directly-stated flag is settled, so each reads final values.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FEATURE_DEPENDENCY_EN_H
#define PROTOCORE_FEATURE_DEPENDENCY_EN_H

#ifndef PROTOCORE_CONFIG_H
#error "include protocore_config.h instead of this file - it is the entry point that states the feature flags"
#endif
/**
 * @brief Streamlined NTRU Prime sntrup761x25519-sha512@openssh.com SSH KEX (default: tracks
 *        ::PROTOCORE_ENABLE_PQC_KEX).
 *
 * A second PQ/T hybrid alongside ML-KEM: sntrup761 (a lattice KEM with a conservative security
 * margin, OpenSSH's long-standing default) crossed with X25519, SHA-512 exchange hash. On by
 * default wherever the PQC hybrid is enabled so a PQC-capable peer gets both methods offered.
 * It is heavier than ML-KEM on the worker stack - the server runs Encaps (~22 KB peak) and the
 * reverse-SSH client runs KeyGen+Decaps (the FO re-encrypt peaks ~32 KB) - so a footprint-bound
 * PQC build (e.g. a classic-ESP32 that only wants ML-KEM) can set this to 0 to drop sntrup761 and
 * keep the lighter ::PROTOCORE_WORKER_STACK_PQC_MIN floor. Requires ::PROTOCORE_ENABLE_PQC_KEX.
 */
#ifndef PROTOCORE_ENABLE_SSH_SNTRUP761
#define PROTOCORE_ENABLE_SSH_SNTRUP761 PROTOCORE_ENABLE_PQC_KEX
#endif

/**
 * @brief Modbus RTU framing (serial / RS-485) over the same data model + PDU dispatch.
 *
 * Default off; implies PROTOCORE_ENABLE_MODBUS. Adds the RTU ADU codec
 * `Modbus.rtu_process_adu` - a `[slave addr][PDU][CRC16]` frame (CRC16-Modbus,
 * little-endian) around the existing host-tested PDU dispatch: a CRC mismatch or a
 * non-matching unit address is dropped silently (no reply, per the spec), and a
 * broadcast (address 0) is executed without a reply. The codec is pure and
 * host-tested; feed it from a UART/RS-485 driver (the serial transport is the
 * application's, framed by the 3.5-char inter-frame idle).
 */
#ifndef PROTOCORE_ENABLE_MODBUS_RTU
#define PROTOCORE_ENABLE_MODBUS_RTU 0
#endif
// RTU is a framing over the same PDU codec, so it needs Modbus compiled in. Declared as a hard
// dependency rather than OR-ed into a second flag the module then guards on: a derived flag is
// invisible to the build. gen_modules.py reads a module's gate off its own source and matches
// PROTOCORE_ENABLE_\w+ only, so a file wrapped in `#if PROTOCORE_NEED_MODBUS` had no gate as far as
// CMake was concerned and was compiled into every target - the derived flag defeated the gating the
// stated one is for. A build that wants RTU states Modbus too.
#define PROTOCORE_ENABLE_MODBUS_RTU_NEEDS_MODBUS PROTOCORE_ENABLE_MODBUS
#if PROTOCORE_ENABLE_MODBUS_RTU && !PROTOCORE_ENABLE_MODBUS_RTU_NEEDS_MODBUS
#error "ProtoCore: PROTOCORE_ENABLE_MODBUS_RTU needs PROTOCORE_ENABLE_MODBUS"
#endif

/**
 * @brief NMEA 2000 codec (`services/nmea2000`).
 *
 * Default off; implies PROTOCORE_ENABLE_J1939 (NMEA 2000 is J1939 at the transport layer). A
 * zero-heap codec for the marine instrumentation network over CAN: it reuses the J1939 29-bit
 * identifier codec and adds the NMEA-specific Fast Packet transport - `protocore_n2k_fastpacket_build_frame`
 * splits a 9..223-octet message across frames (a control octet of sequence + frame counter,
 * the first frame carrying the total length) and `protocore_n2k_fastpacket_feed` reassembles it;
 * `protocore_n2k_build_single` wraps a single-frame message. Pure codec, host-tested. Drive it from the
 * ESP32 TWAI peripheral or an MCP2515 over SPI to bridge an NMEA 2000 backbone onto Wi-Fi.
 */
#ifndef PROTOCORE_ENABLE_NMEA2000
#define PROTOCORE_ENABLE_NMEA2000 0
#endif
// NMEA 2000 reuses the J1939 identifier codec, so it needs J1939 compiled in.
#define PROTOCORE_ENABLE_NMEA2000_NEEDS_J1939 PROTOCORE_ENABLE_J1939
#if PROTOCORE_ENABLE_NMEA2000 && !PROTOCORE_ENABLE_NMEA2000_NEEDS_J1939
#error "ProtoCore: PROTOCORE_ENABLE_NMEA2000 needs PROTOCORE_ENABLE_J1939"
#endif

/**
 * @brief SenML (RFC 8428) measurement-pack builder (`services/senml`).
 *
 * Default off; implies PROTOCORE_ENABLE_CBOR (the SenML-CBOR form uses the CBOR writer). A
 * zero-heap SenML-JSON + SenML-CBOR encoder over the shipped JSON / CBOR codecs: the caller
 * fills a `SenmlRecord` array (base name/time, name, unit, one value, time) and
 * `Senml.json_build` / `Senml.binary_build` (any protocore_codec) emit the whole Pack. Numbers are
 * emitted as integers when integral (so timestamps keep precision), else floats. The standard
 * measurement format for CoAP / LwM2M / HTTP telemetry. Pure codec, host-tested.
 */
#ifndef PROTOCORE_ENABLE_SENML
#define PROTOCORE_ENABLE_SENML 0
#endif
// SenML's binary form is CBOR, so it needs the CBOR codec compiled in.
#define PROTOCORE_ENABLE_SENML_NEEDS_CBOR PROTOCORE_ENABLE_CBOR
#if PROTOCORE_ENABLE_SENML && !PROTOCORE_ENABLE_SENML_NEEDS_CBOR
#error "ProtoCore: PROTOCORE_ENABLE_SENML needs PROTOCORE_ENABLE_CBOR"
#endif

/**
 * @brief Sparkplug B payload + topic codec (`services/sparkplug`).
 *
 * Default off; implies PROTOCORE_ENABLE_PROTOBUF (the payload is a Protobuf message). A zero-heap
 * builder for the Eclipse Sparkplug B industrial-IoT MQTT payload (`Sparkplug.build_payload` /
 * `Sparkplug.build_metric`, over the protobuf codec) and its topic namespace
 * (`Sparkplug.build_topic`, `spBv1.0/group/type/node[/device]`). Field numbers + datatype codes
 * verified against Sparkplug 3.0.0 sec 6.4.1. Pure codec, host-tested; publish it with the MQTT client.
 */
#ifndef PROTOCORE_ENABLE_SPARKPLUG
#define PROTOCORE_ENABLE_SPARKPLUG 0
#endif
// Sparkplug B payloads are protobuf messages, so it needs the protobuf codec compiled in.
#define PROTOCORE_ENABLE_SPARKPLUG_NEEDS_PROTOBUF PROTOCORE_ENABLE_PROTOBUF
#if PROTOCORE_ENABLE_SPARKPLUG && !PROTOCORE_ENABLE_SPARKPLUG_NEEDS_PROTOBUF
#error "ProtoCore: PROTOCORE_ENABLE_SPARKPLUG needs PROTOCORE_ENABLE_PROTOBUF"
#endif

// The NTP server answers from protocore_time_now(), so with the registry off it holds no clock and drops
// every request instead of serving a wrong one. That is a bind that never answers, so it fails here.
#define PROTOCORE_ENABLE_NTP_SERVER_NEEDS_TIME_SOURCE PROTOCORE_ENABLE_TIME_SOURCE
#if PROTOCORE_ENABLE_NTP_SERVER && !PROTOCORE_ENABLE_NTP_SERVER_NEEDS_TIME_SOURCE
#error "ProtoCore: PROTOCORE_ENABLE_NTP_SERVER needs PROTOCORE_ENABLE_TIME_SOURCE"
#endif

// The adaptive announcer re-applies a TXT record through the responder and counts contention
// through the promiscuous sink, so both are what it drives. Stated here so the module is one arm
// rather than a capability test around half its own entries.
#define PROTOCORE_ENABLE_MDNS_ADAPTIVE_NEEDS_MDNS PROTOCORE_ENABLE_MDNS
#if PROTOCORE_ENABLE_MDNS_ADAPTIVE && !PROTOCORE_ENABLE_MDNS_ADAPTIVE_NEEDS_MDNS
#error "ProtoCore: PROTOCORE_ENABLE_MDNS_ADAPTIVE needs PROTOCORE_ENABLE_MDNS"
#endif

#define PROTOCORE_ENABLE_MDNS_ADAPTIVE_NEEDS_PROMISC PROTOCORE_ENABLE_PROMISC
#if PROTOCORE_ENABLE_MDNS_ADAPTIVE && !PROTOCORE_ENABLE_MDNS_ADAPTIVE_NEEDS_PROMISC
#error "ProtoCore: PROTOCORE_ENABLE_MDNS_ADAPTIVE needs PROTOCORE_ENABLE_PROMISC"
#endif

#define PROTOCORE_ENABLE_FTP_SESSION_NEEDS_FTP PROTOCORE_ENABLE_FTP
#if PROTOCORE_ENABLE_FTP_SESSION && !PROTOCORE_ENABLE_FTP_SESSION_NEEDS_FTP
#error "ProtoCore: PROTOCORE_ENABLE_FTP_SESSION needs PROTOCORE_ENABLE_FTP"
#endif

/**
 * @brief Opt-in CDN edge-cache tier (PROTOCORE_ENABLE_EDGE_CACHE, requires HTTP_CACHE).
 *
 * server/web/edge_cache is the caching reverse-proxy edge that network_drivers/presentation/http/httpcache is the
 * origin-side groundwork for: a device sits in front of a remote upstream origin, fetches a response once, and serves
 * subsequent hits from a bounded local store - honoring `Cache-Control` / `Expires` / `ETag` / `Last-Modified`,
 * revalidating stale entries with conditional requests (`If-None-Match` / `If-Modified-Since` -> 304), and serving
 * `Range` / `206` straight from the cache. A two-tier store: bounded RAM (L1, hot) plus an optional dbm/WAL-backed SD
 * tier (L2, persistent, when PROTOCORE_ENABLE_DBM is set). Misses/revalidations fetch the origin asynchronously (the
 * client request is suspended and resumed from the poll loop, never stalling the worker) and always fail open. Zero
 * heap. Default off.
 */
#ifndef PROTOCORE_ENABLE_EDGE_CACHE
#define PROTOCORE_ENABLE_EDGE_CACHE 0
#endif
#define PROTOCORE_ENABLE_EDGE_CACHE_NEEDS_HTTP_CACHE PROTOCORE_ENABLE_HTTP_CACHE
#if PROTOCORE_ENABLE_EDGE_CACHE && !PROTOCORE_ENABLE_EDGE_CACHE_NEEDS_HTTP_CACHE
#error "ProtoCore: PROTOCORE_ENABLE_EDGE_CACHE needs PROTOCORE_ENABLE_HTTP_CACHE"
#endif
#define PROTOCORE_ENABLE_EDGE_CACHE_NEEDS_HTTP_CLIENT PROTOCORE_ENABLE_HTTP_CLIENT
#if PROTOCORE_ENABLE_EDGE_CACHE && !PROTOCORE_ENABLE_EDGE_CACHE_NEEDS_HTTP_CLIENT
#error "ProtoCore: PROTOCORE_ENABLE_EDGE_CACHE needs PROTOCORE_ENABLE_HTTP_CLIENT"
#endif
// Opt-in TLS upstream origins: when set, a mapped `https://` origin is fetched over the shared client-TLS
// session (protocore_tls_csess) instead of being rejected. One outbound TLS origin fetch at a time (the session is
// a singleton, shared with MQTTS/wss); the handshake blocks the worker briefly at connect (like the MQTT/WS
// clients). Needs the TLS engine + the ~48 KB arena - an S3 / PSRAM board is recommended.
/* Derived sizing for the edge cache. Macros, not constexpr: PROTOCORE_EDGE_FETCH_BUF's default is
 * computed from PROTOCORE_EDGE_MESH_RESP_MAX below and the requirement is enforced with an #error,
 * and the preprocessor can evaluate neither `constexpr` nor `sizeof`. SRCBANNED rule 18. */

/**
 * @brief Opt-in mesh (sibling-cache) distribution for the edge cache (PROTOCORE_ENABLE_EDGE_MESH).
 *
 * Lets a fleet of edge nodes share one warm cache: on a full local miss, a node queries its configured
 * sibling peers (over a plaintext PROTO_MESH TCP link) before hitting the origin, and pulls a
 * fresh copy from whichever peer has it - so the origin is fetched once per fleet, not once per node. Pull
 * (read-through) only: no push, no invalidation protocol, no consistency window - a stale sibling copy
 * self-expires by its own TTL and the requester re-checks freshness on arrival. The transfer carries the
 * object plus its freshness/age (RFC 9111 age propagation), so a sibling-fresh object serves for its
 * remaining lifetime with zero origin contact. A serving node answers only from its local store (one hop,
 * never re-querying its own origin/peers, so the fleet cannot loop). Peers are a static list
 * (protocore_edge_cache_add_peer); auto-discovery is a follow-up. Zero heap. Default off.
 */
#ifndef PROTOCORE_ENABLE_EDGE_MESH
#define PROTOCORE_ENABLE_EDGE_MESH 0
#endif
#define PROTOCORE_ENABLE_EDGE_MESH_NEEDS_EDGE_CACHE PROTOCORE_ENABLE_EDGE_CACHE
#if PROTOCORE_ENABLE_EDGE_MESH && !PROTOCORE_ENABLE_EDGE_MESH_NEEDS_EDGE_CACHE
#error "ProtoCore: PROTOCORE_ENABLE_EDGE_MESH needs PROTOCORE_ENABLE_EDGE_CACHE"
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
#if PROTOCORE_ENABLE_OTA || PROTOCORE_ENABLE_UPLOAD || PROTOCORE_ENABLE_WEBDAV
#define PROTOCORE_ENABLE_STREAM_BODY 1
#else
#define PROTOCORE_ENABLE_STREAM_BODY 0
#endif

/**
 * @brief Internal: client-side TLS engine is compiled (HTTPS client, MQTTS, wss client, and/or a TLS edge-cache
 * origin).
 *
 * The outbound HTTP client (one-shot exchange) and the MQTT / WebSocket clients
 * and the edge cache's TLS origin fetch (persistent sessions) share the same
 * client mbedTLS code in protocore_tls - the CA/pin trust config, the BIO typedefs,
 * and the session API - gated by this.
 */
// Derived: these carry the expression their header used to hold.
#ifndef PROTOCORE_ENABLE_AES128GCM
#define PROTOCORE_ENABLE_AES128GCM                                                                                     \
    (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_SMB || PROTOCORE_ENABLE_TLS)
#endif
#ifndef PROTOCORE_ENABLE_AESCCM
#define PROTOCORE_ENABLE_AESCCM PROTOCORE_ENABLE_SMB
#endif
#ifndef PROTOCORE_ENABLE_HKDF
#define PROTOCORE_ENABLE_HKDF (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_TLS)
#endif
// The HPACK/QPACK integer and Huffman string primitives (RFC 7541 sec 5), read by the HTTP/2 header
// table and the HTTP/3 QPACK encoder.
#ifndef PROTOCORE_ENABLE_HPACK_PRIM
#define PROTOCORE_ENABLE_HPACK_PRIM (PROTOCORE_ENABLE_HTTP2 || PROTOCORE_ENABLE_HTTP3)
#endif
#ifndef PROTOCORE_ENABLE_SHA384
#define PROTOCORE_ENABLE_SHA384 (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_TLS)
#endif
#ifndef PROTOCORE_ENABLE_HMAC_SHA384
#define PROTOCORE_ENABLE_HMAC_SHA384 (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_TLS)
#endif
#ifndef PROTOCORE_ENABLE_HKDF_SHA384
#define PROTOCORE_ENABLE_HKDF_SHA384 (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_TLS)
#endif
#ifndef PROTOCORE_ENABLE_SHA3
#define PROTOCORE_ENABLE_SHA3 PROTOCORE_ENABLE_PQC_KEX
#endif
#ifndef PROTOCORE_ENABLE_MLKEM
#define PROTOCORE_ENABLE_MLKEM PROTOCORE_ENABLE_PQC_KEX
#endif
#ifndef PROTOCORE_ENABLE_SNTRUP761
#define PROTOCORE_ENABLE_SNTRUP761 PROTOCORE_ENABLE_SSH_SNTRUP761
#endif

// The file-transfer servers and file serving all reach storage through the filesystem accessor,
// which is the HAL and points them at whatever is mounted. None of them needs the mount SERVICE:
// they need the seam, and the seam fails closed when nothing is behind it. Requiring PROTOCORE_ENABLE_MNT
// would drag the RAM backend's pool into every build that moves a file, to satisfy a type.

#define PROTOCORE_ENABLE_TLS_RESUMPTION_NEEDS_TLS PROTOCORE_ENABLE_TLS
#if PROTOCORE_ENABLE_TLS_RESUMPTION && !PROTOCORE_ENABLE_TLS_RESUMPTION_NEEDS_TLS
#error "ProtoCore: PROTOCORE_ENABLE_TLS_RESUMPTION needs PROTOCORE_ENABLE_TLS"
#endif

#define PROTOCORE_ENABLE_METRICS_NEEDS_STATS PROTOCORE_ENABLE_STATS
#if PROTOCORE_ENABLE_METRICS && !PROTOCORE_ENABLE_METRICS_NEEDS_STATS
#error "ProtoCore: PROTOCORE_ENABLE_METRICS needs PROTOCORE_ENABLE_STATS"
#endif

#define PROTOCORE_ENABLE_SNMP_TRAP_NEEDS_SNMP PROTOCORE_ENABLE_SNMP
#if PROTOCORE_ENABLE_SNMP_TRAP && !PROTOCORE_ENABLE_SNMP_TRAP_NEEDS_SNMP
#error "ProtoCore: PROTOCORE_ENABLE_SNMP_TRAP needs PROTOCORE_ENABLE_SNMP"
#endif

#define PROTOCORE_ENABLE_PMBUS_NEEDS_SMBUS PROTOCORE_ENABLE_SMBUS
#if PROTOCORE_ENABLE_PMBUS && !PROTOCORE_ENABLE_PMBUS_NEEDS_SMBUS
#error "ProtoCore: PROTOCORE_ENABLE_PMBUS needs PROTOCORE_ENABLE_SMBUS"
#endif

#define PROTOCORE_ENABLE_COAP_OBSERVE_NEEDS_COAP PROTOCORE_ENABLE_COAP
#if PROTOCORE_ENABLE_COAP_OBSERVE && !PROTOCORE_ENABLE_COAP_OBSERVE_NEEDS_COAP
#error "ProtoCore: PROTOCORE_ENABLE_COAP_OBSERVE needs PROTOCORE_ENABLE_COAP"
#endif

#define PROTOCORE_ENABLE_WS_DEFLATE_NEEDS_WEBSOCKET PROTOCORE_ENABLE_WEBSOCKET
#if PROTOCORE_ENABLE_WS_DEFLATE && !PROTOCORE_ENABLE_WS_DEFLATE_NEEDS_WEBSOCKET
#error "ProtoCore: PROTOCORE_ENABLE_WS_DEFLATE needs PROTOCORE_ENABLE_WEBSOCKET"
#endif

#define PROTOCORE_ENABLE_CIA402_NEEDS_CANOPEN PROTOCORE_ENABLE_CANOPEN
#if PROTOCORE_ENABLE_CIA402 && !PROTOCORE_ENABLE_CIA402_NEEDS_CANOPEN
#error "ProtoCore: PROTOCORE_ENABLE_CIA402 needs PROTOCORE_ENABLE_CANOPEN"
#endif

#define PROTOCORE_ENABLE_SSH_ZLIB_NEEDS_SSH PROTOCORE_ENABLE_SSH
#if PROTOCORE_ENABLE_SSH_ZLIB && !PROTOCORE_ENABLE_SSH_ZLIB_NEEDS_SSH
#error "ProtoCore: PROTOCORE_ENABLE_SSH_ZLIB needs PROTOCORE_ENABLE_SSH"
#endif

#define PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE_NEEDS_SSH PROTOCORE_ENABLE_SSH
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE && !PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE_NEEDS_SSH
#error "ProtoCore: PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE needs PROTOCORE_ENABLE_SSH"
#endif
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE && !PROTOCORE_SSH_ALLOW_PASSWORD
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE is password-backed - it verifies the response through the password callback, so PROTOCORE_SSH_ALLOW_PASSWORD must stay 1 (or drop keyboard-interactive for publickey-only)"
#endif

// SFTP and SCP need the channel layer to carry them and the mount to store into (PROTOCORE_ENABLE_MNT is
// required above). They do NOT need FILE_SERVING: that dependency was the fs::FS seam, and the seam
// now lives with the vendor code in test/core_setup/, behind the mount backend.
#define PROTOCORE_ENABLE_SSH_SFTP_NEEDS_SSH PROTOCORE_ENABLE_SSH
#if PROTOCORE_ENABLE_SSH_SFTP && !PROTOCORE_ENABLE_SSH_SFTP_NEEDS_SSH
#error "ProtoCore: PROTOCORE_ENABLE_SSH_SFTP needs PROTOCORE_ENABLE_SSH"
#endif
#define PROTOCORE_ENABLE_SSH_SCP_NEEDS_SSH PROTOCORE_ENABLE_SSH
#if PROTOCORE_ENABLE_SSH_SCP && !PROTOCORE_ENABLE_SSH_SCP_NEEDS_SSH
#error "ProtoCore: PROTOCORE_ENABLE_SSH_SCP needs PROTOCORE_ENABLE_SSH"
#endif

#define PROTOCORE_ENABLE_WEB_TERMINAL_NEEDS_WEBSOCKET PROTOCORE_ENABLE_WEBSOCKET
#if PROTOCORE_ENABLE_WEB_TERMINAL && !PROTOCORE_ENABLE_WEB_TERMINAL_NEEDS_WEBSOCKET
#error "ProtoCore: PROTOCORE_ENABLE_WEB_TERMINAL needs PROTOCORE_ENABLE_WEBSOCKET"
#endif

#define PROTOCORE_ENABLE_DASHBOARD_NEEDS_SSE PROTOCORE_ENABLE_SSE
#if PROTOCORE_ENABLE_DASHBOARD && !PROTOCORE_ENABLE_DASHBOARD_NEEDS_SSE
#error "ProtoCore: PROTOCORE_ENABLE_DASHBOARD needs PROTOCORE_ENABLE_SSE"
#endif

#define PROTOCORE_ENABLE_SNMP_V3_NEEDS_SNMP PROTOCORE_ENABLE_SNMP
#if PROTOCORE_ENABLE_SNMP_V3 && !PROTOCORE_ENABLE_SNMP_V3_NEEDS_SNMP
#error "ProtoCore: PROTOCORE_ENABLE_SNMP_V3 needs PROTOCORE_ENABLE_SNMP"
#endif

#define PROTOCORE_ENABLE_OPCUA_CLIENT_NEEDS_OPCUA PROTOCORE_ENABLE_OPCUA
#if PROTOCORE_ENABLE_OPCUA_CLIENT && !PROTOCORE_ENABLE_OPCUA_CLIENT_NEEDS_OPCUA
#error "ProtoCore: PROTOCORE_ENABLE_OPCUA_CLIENT needs PROTOCORE_ENABLE_OPCUA"
#endif

#define PROTOCORE_ENABLE_UMATI_NEEDS_OPCUA PROTOCORE_ENABLE_OPCUA
#if PROTOCORE_ENABLE_UMATI && !PROTOCORE_ENABLE_UMATI_NEEDS_OPCUA
#error "ProtoCore: PROTOCORE_ENABLE_UMATI needs PROTOCORE_ENABLE_OPCUA"
#endif

#define PROTOCORE_ENABLE_ROBOTICS_NEEDS_OPCUA PROTOCORE_ENABLE_OPCUA
#if PROTOCORE_ENABLE_ROBOTICS && !PROTOCORE_ENABLE_ROBOTICS_NEEDS_OPCUA
#error "ProtoCore: PROTOCORE_ENABLE_ROBOTICS needs PROTOCORE_ENABLE_OPCUA"
#endif

#define PROTOCORE_ENABLE_EUROMAP77_NEEDS_OPCUA PROTOCORE_ENABLE_OPCUA
#if PROTOCORE_ENABLE_EUROMAP77 && !PROTOCORE_ENABLE_EUROMAP77_NEEDS_OPCUA
#error "ProtoCore: PROTOCORE_ENABLE_EUROMAP77 needs PROTOCORE_ENABLE_OPCUA"
#endif

#define PROTOCORE_ENABLE_CONFIG_IO_NEEDS_CONFIG_STORE PROTOCORE_ENABLE_CONFIG_STORE
#if PROTOCORE_ENABLE_CONFIG_IO && !PROTOCORE_ENABLE_CONFIG_IO_NEEDS_CONFIG_STORE
#error "ProtoCore: PROTOCORE_ENABLE_CONFIG_IO needs PROTOCORE_ENABLE_CONFIG_STORE"
#endif

#endif // PROTOCORE_FEATURE_DEPENDENCY_EN_H
