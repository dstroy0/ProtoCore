// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore.h
 * @brief Layer 7 (Application) - public HTTP routing API.
 *
 * This is the only header most application code needs to include.
 * The full OSI include chain is pulled in automatically:
 * @code
 *   protocore.h                  (L7 Application)
 *     ├── network_drivers/presentation/presentation.h (L6 Presentation)
 *     │       ├── network_drivers/presentation/http/http_parser/http_parser.h (parser types)
 *     │       └── network_drivers/transport/tcp.h      (L4 Transport)
 *     │               └── protocore_config.h   (compile-time config)
 *     └── network_drivers/session/session.h      (L5 Session - event drain)
 * @endcode
 *
 * **Feature flags** - define any of these to 0 before including to strip
 * the feature from the build entirely:
 * @code
 *   #define PC_ENABLE_WEBSOCKET    0
 *   #define PC_ENABLE_SSE          0
 *   #define PC_ENABLE_MULTIPART    0
 *   #define PC_ENABLE_FILE_SERVING 0
 *   #define PC_ENABLE_AUTH         0
 *   #include <protocore.h>
 * @endcode
 *
 * **Determinism guarantees**
 * - All buffers are statically allocated; no heap usage after begin().
 * - Every operation has O(1) or O(MAX_ROUTES) worst-case time.
 * - `handle()` is safe to call every Arduino `loop()` iteration.
 *
 * @author   Douglas Quigg (dstroy0)
 * @date     2026
 * @copyright Copyright (C) 2026 Douglas Quigg (dstroy0). AGPL-3.0-or-later.
 */

#ifndef PROTOCORE_H
#define PROTOCORE_H

#include "protocore_config.h" // the entry point: types.h for PROTO_BEGIN_DECLS, before anything uses it

// The whole library is C and every sketch that includes this header is compiled as C++, so the
// declarations below carry C linkage from here rather than from each header. System and vendor
// headers state their own; an extern "C" already inside one nests harmlessly.
PROTO_BEGIN_DECLS

// Ordered by layer, so a header only needs what is above it. A gate is the feature's own
// PC_ENABLE_, never a parent's: PC_ENABLE_<A>_NEEDS_<B> already makes a child unsettable alone.
#include "shared_primitives/log.h"
#include "shared_primitives/mime.h"

#include "crypto/cipher/aes256ctr.h"
#include "crypto/mac/hmac_sha256.h"

#include "network_drivers/application/auth/auth.h"
#include "network_drivers/physical/physical.h"

#include "network_drivers/transport/diffserv.h"
#include "network_drivers/transport/tcp.h"
#include "network_drivers/transport/udp.h"

#include "network_drivers/presentation/http/http.h"
#include "network_drivers/session/session.h"
#include "network_drivers/session/worker.h"

#include "network_drivers/presentation/codec/json/json.h"
#include "network_drivers/presentation/presentation.h"
#if PC_ENABLE_CBOR
#include "network_drivers/presentation/codec/cbor/cbor.h"
#endif
#if PC_ENABLE_MSGPACK
#include "network_drivers/presentation/codec/msgpack/msgpack.h"
#endif
#if PC_ENABLE_MULTIPART
#include "network_drivers/presentation/codec/multipart/multipart.h"
#endif
#if PC_ENABLE_WEBSOCKET
#include "network_drivers/presentation/http/websocket/websocket.h"
#endif
#if PC_ENABLE_SSE
#include "network_drivers/presentation/http/sse/sse.h"
#endif
#if PC_ENABLE_TELNET
#include "network_drivers/presentation/telnet/telnet.h"
#endif
#if PC_ENABLE_SSH
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_client.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/presentation/ssh/connection/ssh_forward.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include "network_drivers/tls/ssh_rsa.h"
#endif

// This header declares size_t, uint8_t and int64_t in its own signatures, so it names their
// headers itself rather than inheriting them from whichever include above happens to pull them in.

// ---------------------------------------------------------------------------
// Every service API, surfaced here
// ---------------------------------------------------------------------------
//
// protocore.h is the one header an application includes, so every service the library offers is
// reachable from it. A feature the build did not enable costs nothing: each service header carries
// its own PC_ENABLE_ guard, so a disabled one contributes no declarations and no code.
#include "mmgr/arena.h"
#include "mmgr/dma.h"
#include "mmgr/plaintext.h"
#include "mmgr/secure.h"
#include "mmgr/span.h"
#include "network_drivers/application/file_serving/file_serving.h"
#include "network_drivers/application/http_range.h"
#include "network_drivers/application/mdns_adaptive/mdns_adaptive.h"
#include "network_drivers/application/mdns_service/mdns_service.h"
#include "network_drivers/application/ntp_server/ntp_server.h"
#include "network_drivers/application/ntp_service/ntp_service.h"
#include "network_drivers/application/nts/nts.h"
#include "network_drivers/application/ptp/ptp.h"
#include "network_drivers/application/scp/scp.h"
#include "network_drivers/application/scp/ssh_scp.h"
#include "network_drivers/application/sftp/sftp.h"
#include "network_drivers/application/sftp/ssh_sftp.h"
#include "network_drivers/application/smb/ntlm.h"
#include "network_drivers/application/smb/ntlmssp.h"
#include "network_drivers/application/smb/smb2.h"
#include "network_drivers/application/smb/smb_client.h"
#include "network_drivers/application/smb/spnego.h"
#include "network_drivers/application/upload_service/upload_service.h"
#include "network_drivers/application/webdav/webdav.h"
#include "network_drivers/datalink/roaming.h"
#include "network_drivers/network/dns/resolver.h"
#include "network_drivers/network/dns/server.h"
#include "network_drivers/network/network.h"
#include "network_drivers/network/route.h"
#include "network_drivers/physical/radio_power.h"
#include "network_drivers/session/preempt_queue.h"
#include "server/clock/clock.h"
#include "server/exc_decoder.h"
#include "server/failsafe.h"
#include "server/filesystem/filesystem.h"
#include "server/filesystem/mnt.h"
#include "server/filesystem/wearlevel.h"
#include "server/logbuf.h"
#include "server/power_mgmt.h"
#include "server/signaling/bus_capture.h"
#include "server/signaling/device_id.h"
#include "server/signaling/gpio_map.h"
#include "server/signaling/hw_health.h"
#include "server/signaling/link_manager.h"
#include "server/signaling/signaling.h"
#include "server/signaling/trace_capture.h"
#include "server/sleep_sched.h"
#include "server/update/ota_rollback.h"
#include "server/update/ota_service.h"
#include "services/energy/c37118/c37118.h"
#include "services/energy/dnp3/dnp3.h"
#include "services/energy/goose/goose.h"
#include "services/energy/iccp/iccp.h"
#include "services/energy/iec60870/iec60870.h"
#include "services/energy/mms/mms.h"
#include "services/energy/openadr/openadr.h"
#include "services/energy/sep2/sep2.h"
#include "services/energy/sunspec/sunspec.h"
#include "services/fieldbus/ads/ads.h"
#include "services/fieldbus/bacnet/bacnet.h"
#include "services/fieldbus/canopen/canopen.h"
#include "services/fieldbus/cclink/cclink.h"
#include "services/fieldbus/cia402/cia402.h"
#include "services/fieldbus/cip/cip.h"
#include "services/fieldbus/cotp/cotp.h"
#include "services/fieldbus/devicenet/devicenet.h"
#include "services/fieldbus/df1/df1.h"
#include "services/fieldbus/directnet/directnet.h"
#include "services/fieldbus/enip/enip.h"
#include "services/fieldbus/fins/fins.h"
#include "services/fieldbus/hart/hart.h"
#include "services/fieldbus/hostlink/hostlink.h"
#include "services/fieldbus/interbus/interbus.h"
#include "services/fieldbus/iolink/iolink.h"
#include "services/fieldbus/j1939/j1939.h"
#include "services/fieldbus/lonworks/lonworks.h"
#include "services/fieldbus/mbplus/mbplus.h"
#include "services/fieldbus/mbus/mbus.h"
#include "services/fieldbus/melsec/melsec.h"
#include "services/fieldbus/modbus/modbus.h"
#include "services/fieldbus/modbus/modbus_master.h"
#include "services/fieldbus/opcua/opcua.h"
#include "services/fieldbus/opcua_client/opcua_client.h"
#include "services/fieldbus/powerlink/powerlink.h"
#include "services/fieldbus/profibus/profibus.h"
#include "services/fieldbus/profinet/profinet.h"
#include "services/fieldbus/rawl2/rawl2.h"
#include "services/fieldbus/s7comm/s7comm.h"
#include "services/fieldbus/sercos/sercos.h"
#include "services/fieldbus/simatic/simatic.h"
#include "services/fieldbus/snp/snp.h"
#include "services/file_transfer/ftp/ftp.h"
#include "services/file_transfer/ftp/ftp_session.h"
#include "services/file_transfer/http_delivery/http_delivery.h"
#include "services/instrumentation/gpib/gpib.h"
#include "services/instrumentation/hislip/hislip.h"
#include "services/instrumentation/scpi/scpi.h"
#include "services/instrumentation/vxi11/vxi11.h"
#include "services/iot/amqp/amqp.h"
#include "services/iot/cloudevents/cloudevents.h"
#include "services/iot/coap/coap.h"
#include "services/iot/coap/coaps.h"
#include "services/iot/coap/coaps_server.h"
#include "services/iot/dds/dds.h"
#include "services/iot/graphql/graphql.h"
#include "services/iot/grpcweb/grpcweb.h"
#include "services/iot/lwm2m/lwm2m_tlv.h"
#include "services/iot/mqtt/mqtt.h"
#include "services/iot/mqtt/mqtt_sn.h"
#include "services/iot/nats/nats.h"
#include "services/iot/protobuf/protobuf.h"
#include "services/iot/redis_resp/redis_resp.h"
#include "services/iot/senml/senml.h"
#include "services/iot/sparkplug/sparkplug.h"
#include "services/iot/statsd/statsd.h"
#include "services/iot/stomp/stomp.h"
#include "services/iot/telemetry/telemetry.h"
#include "services/iot/udp_telemetry/udp_telemetry.h"
#include "services/iot/wamp/wamp.h"
#include "services/iot/xmpp/xmpp.h"
#include "services/machine_tool/atc/atc.h"
#include "services/machine_tool/dnc/dnc.h"
#include "services/machine_tool/dnc/dnc_stream.h"
#include "services/machine_tool/euromap77/euromap77.h"
#include "services/machine_tool/fanuc_j519/fanuc_j519.h"
#include "services/machine_tool/focas/focas.h"
#include "services/machine_tool/haas_mdc/haas_mdc.h"
#include "services/machine_tool/lsv2/lsv2.h"
#include "services/machine_tool/mtconnect/mtconnect.h"
#include "services/machine_tool/packml/packml.h"
#include "services/machine_tool/robotics/robotics.h"
#include "services/machine_tool/safety_scl/safety_scl.h"
#include "services/machine_tool/umati/umati.h"
#include "services/net/flow_export/flow_export.h"
#include "services/net/forward/forward.h"
#include "services/net/gateway/gateway.h"
#include "services/net/happy_eyeballs/happy_eyeballs.h"
#include "services/net/http_client/http_client.h"
#include "services/net/iface_bridge/iface_bridge.h"
#include "services/net/iface_bridge/iface_bridge_hw.h"
#include "services/net/netadapt/netadapt.h"
#include "services/net/proxy_protocol/proxy_protocol.h"
#include "services/net/relay/relay.h"
#include "services/net/relay/relay_listener.h"
#include "services/net/smtp/smtp.h"
#include "services/net/snmp/snmp_agent.h"
#include "services/net/snmp/snmp_ber.h"
#include "services/net/snmp/snmp_crypto.h"
#include "services/net/snmp/snmp_notify.h"
#include "services/net/snmp/snmp_v3.h"
#include "services/net/sockpool/sockpool.h"
#include "services/net/southbound/sb_modbus.h"
#include "services/net/southbound/southbound.h"
#include "services/net/syslog/syslog.h"
#include "services/net/webhook/webhook.h"
#include "services/net/ws_client/ws_client.h"
#include "services/peripherals/ad9238/ad9238.h"
#include "services/peripherals/ads1115/ads1115.h"
#include "services/peripherals/dmx/dmx.h"
#include "services/peripherals/dshot/dshot.h"
#include "services/peripherals/fdc2214/fdc2214.h"
#include "services/peripherals/hmmd/hmmd.h"
#include "services/peripherals/i2c.h"
#include "services/peripherals/ina219/ina219.h"
#include "services/peripherals/ld2410/ld2410.h"
#include "services/peripherals/ldc1614/ldc1614.h"
#include "services/peripherals/mpr121/mpr121.h"
#include "services/peripherals/pca9685/pca9685.h"
#include "services/peripherals/pn532/pn532.h"
#include "services/peripherals/rcwl0516/rcwl0516.h"
#include "services/peripherals/rtc/rtc.h"
#include "services/peripherals/sdi12/sdi12.h"
#include "services/peripherals/sen0192/sen0192.h"
#include "services/peripherals/sht3x/sht3x.h"
#include "services/peripherals/spi.h"
#include "services/peripherals/vl53l0x/vl53l0x.h"
#include "services/radio/ble_gatt/ble_gatt.h"
#include "services/radio/cc1101/cc1101.h"
#include "services/radio/enocean/enocean.h"
#include "services/radio/espnow/espnow.h"
#include "services/radio/lora/lora.h"
#include "services/radio/nrf24/nrf24.h"
#include "services/radio/promisc/promisc.h"
#include "services/radio/radio_sniff/radio_sniff.h"
#include "services/radio/sigfox/sigfox.h"
#include "services/radio/thread/thread.h"
#include "services/radio/wifi_sniffer/wifi_sniffer.h"
#include "services/radio/wisun/wisun.h"
#include "services/radio/zigbee/zigbee.h"
#include "services/radio/zwave/zwave.h"
#include "services/security/audit_log/audit_log.h"
#include "services/security/auth_lockout/auth_lockout.h"
#include "services/security/csrf/csrf.h"
#include "services/security/forwarded_trust/forwarded_trust.h"
#include "services/security/guardrails/guardrails.h"
#include "services/security/ikev2/ikev2.h"
#include "services/security/ikev2/ikev2_natt.h"
#include "services/security/jwt/jwt.h"
#include "services/security/oauth2/oauth2.h"
#include "services/security/oidc/oidc.h"
#include "services/security/tls_policy/tls_policy.h"
#include "services/security/totp/totp.h"
#include "services/storage/config_io/config_io.h"
#include "services/storage/config_store/config_store.h"
#include "services/storage/dbm/dbm.h"
#include "services/storage/docstore/docstore.h"
#include "services/storage/hotswap/hotswap.h"
#include "services/storage/partition_monitor/partition_monitor.h"
#include "services/storage/psram_pool/psram_pool.h"
#include "services/storage/sqlite/sqlite_format.h"
#include "services/storage/wal/wal.h"
#include "services/storage/wal/wal_fs.h"
#include "services/storage/wal/wal_store.h"
#include "services/system/control/control.h"
#include "services/system/esp/esp.h"
#include "services/system/esp/ipsec_db.h"
#include "services/system/provisioning_service/provisioning_service.h"
#include "services/timing_position/gnss/gnss_survey.h"
#include "services/timing_position/gnss/ntrip_caster.h"
#include "services/timing_position/gnss/ntrip_caster_listener.h"
#include "services/timing_position/gnss/rtcm3.h"
#include "services/timing_position/nmea0183/nmea0183.h"
#include "services/timing_position/nmea2000/nmea2000.h"
#include "services/timing_position/time_source/time_source.h"
#include "services/timing_position/ubx/ubx.h"
#include "services/transportation/j2735/j2735.h"
#include "services/transportation/nema_ts2/nema_ts2.h"
#include "services/transportation/ntcip/ntcip.h"
#include "services/transportation/ocit/ocit.h"
#include "services/transportation/utmc/utmc.h"
#include "services/transportation/wave/wave.h"
#include "services/web/dashboard/dashboard.h"
#include "services/web/edge_cache/edge_cache.h"
#include "services/web/edge_cache/edge_cache_proxy.h"
#include "services/web/edge_cache/edge_cache_sd.h"
#include "services/web/edge_cache/edge_fetch.h"
#include "services/web/edge_cache/edge_mesh.h"
#include "services/web/httpcache/httpcache.h"
#include "services/web/spa_router/spa_router.h"
#include "services/web/web_terminal/web_terminal.h"

/**
 * @brief A storage backend (server/filesystem/mnt.h), named here only as a pointer.
 *
 * Forward-declared rather than included because this API never looks inside one. A mount point takes
 * a backend and dispatches through its vtable, so what actually implements the storage - the board's
 * vendor filesystem, a RAM pool, an application's own - is unknowable from here, which is the whole
 * reason the seam is our type instead of a vendor one.
 */
typedef struct pc_mnt_backend pc_mnt_backend;

// ---------------------------------------------------------------------------
// HTTP method enumeration
// ---------------------------------------------------------------------------

// HttpMethod is the presentation layer's, declared in network_drivers/presentation/http/http.h.

// ---------------------------------------------------------------------------
// Handler and route types
// ---------------------------------------------------------------------------

/**
 * @brief Callback signature for HTTP request handlers.
 *
 * The callback receives the connection slot index and a pointer to the
 * fully-parsed request.  Call send_text() or send_empty()
 * from inside the callback to write a response.
 *
 * @param slot_id  Index into the connection pool (0 … MAX_CONNS-1).
 * @param request  Pointer to the parsed HTTP request.  Valid only during the
 *                 callback; do not cache this pointer.
 *
 * @note If the callback returns without calling send_text(), the framework will
 *       reset the slot automatically (no response is sent to the client).
 */
typedef void (*Handler)(uint8_t slot_id, HttpReq *request);

/**
 * @brief Resolver for `{{name}}` template placeholders used by send_template().
 *
 * Called with a placeholder name; returns the replacement string, or NULL
 * to substitute an empty string. The pointer must stay valid for the duration
 * of the send_template() call, and the resolver must be deterministic (it is
 * invoked twice: once to size the body, once to emit it).
 */
typedef const char *(*TemplateVar)(const char *name);

/**
 * @brief Per-request access-log callback (see on_request_log()).
 *
 * Invoked once per response with the request method/path, the HTTP status code,
 * and the response body length in bytes. The strings are valid only for the
 * duration of the call. This is a thin hook - the library does no buffering or
 * formatting; route the data to Serial, syslog, etc. as you see fit.
 */
typedef void (*RequestLogCb)(const char *method, const char *path, int status, int body_len);

/**
 * @brief Outcome of a middleware function (see @ref Middleware).
 *
 * Returning MW_NEXT passes the request to the next middleware in the chain and,
 * once the chain is exhausted, on to the matching route handler. Returning
 * MW_HALT stops the chain: the route handler is NOT invoked, so a middleware
 * that halts must have already written a response (the dispatcher treats the
 * request as fully handled).
 */
typedef enum
{
    MW_NEXT = 0, ///< Continue to the next middleware / the route handler.
    MW_HALT = 1  ///< Stop dispatch; the middleware already sent a response.
} MwResult;

/**
 * @brief Composable pre-dispatch middleware (see use()).
 *
 * Each registered middleware runs - in registration order - on every request
 * before route matching, receiving the same `(slot_id, request)` pair a handler
 * does. A middleware may inspect the request, queue response headers
 * (proto_add_response_header()), short-circuit by sending a response and
 * returning MW_HALT, or fall through with MW_NEXT. Middlewares reference the
 * application's server instance the same way handlers do (the global object), so
 * they can call send_text() / send_empty() to short-circuit.
 *
 * @param slot_id  Connection slot index (0 … MAX_CONNS-1).
 * @param request  Parsed request; valid only during the call (do not cache).
 * @return MW_NEXT to continue, MW_HALT to stop (response already sent).
 */
typedef MwResult (*Middleware)(uint8_t slot_id, HttpReq *request);

// ---------------------------------------------------------------------------
// Route type discriminator
// ---------------------------------------------------------------------------

/** @brief Discriminates between HTTP, WebSocket, and SSE route entries. */
typedef enum
{
    ROUTE_HTTP, ///< Standard HTTP request/response.
#if PC_ENABLE_WEBSOCKET
    ROUTE_WS, ///< WebSocket upgrade route.
#endif
#if PC_ENABLE_SSE
    ROUTE_SSE, ///< Server-Sent Events route.
#endif
#if PC_ENABLE_FILE_SERVING
    ROUTE_STATIC, ///< Static-file subtree mount (serve_static()).
#endif
#if PC_ENABLE_WEBDAV
    ROUTE_DAV, ///< WebDAV subtree mount (dav()).
#endif
} RouteType;

// ---------------------------------------------------------------------------
// begin() / listen() / restart() result codes
// ---------------------------------------------------------------------------

/**
 * @brief Result codes for listen(), begin(), and restart().
 *
 * Success is a positive value (PC_OK). Failures are distinct negative codes
 * so a caller can tell why startup failed.
 */
typedef enum
{
    PC_OK = 1,                 ///< Success.
    PC_ERR_NO_LISTENERS = -1,  ///< proto_begin() called before any listen() / begin(port).
    PC_ERR_LISTENER_FULL = -2, ///< listen(): listener pool (MAX_LISTENERS) is full.
    PC_ERR_LISTEN_FAILED = -3  ///< A listener failed to open (bind/listen/lwIP error).
} pc_result;

/**
 * @brief Internal route entry stored in the routing table.
 *
 * Populated by on(), on_ws(), or on_sse().
 * Application code does not interact with this struct directly.
 */
typedef struct Route
{
    char path[MAX_PATH_LEN]; ///< Null-terminated path pattern.
    RouteType type;          ///< HTTP, WS, or SSE.
    HttpMethod method;       ///< HTTP method (ROUTE_HTTP only).
    Handler callback;        ///< HTTP handler (ROUTE_HTTP only).

#if PC_ENABLE_WEBSOCKET
    /// The handler set this route serves, or PC_WS_NONE. The handlers belong to the websocket
    /// module: a route decides where a request goes, and what runs once the socket is open is not
    /// routing's business.
    uint8_t ws_id;
#endif

#if PC_ENABLE_SSE
    /// The handler this route serves, or PC_SSE_NONE. The handler belongs to the sse module: a
    /// route decides where a request goes, not what runs once a client subscribes.
    uint8_t sse_id;
#endif

#if PC_ENABLE_FILE_SERVING
    /// The mount point this route serves, or PC_MNT_NONE. The backend and subtree belong to mnt:
    /// two registrars describe a mount the same way, so the description lives with mounting.
    uint8_t mnt_id;
#endif

#if PC_ENABLE_AUTH
    /// The credential set this route needs, or PC_AUTH_NONE. The credentials themselves belong to
    /// the auth module: a route decides where a request goes, and key material in a routing entry is
    /// a copy of a secret in a place that has no reason to hold one.
    uint8_t auth_id; ///< Required password.
#endif

    proto_bool is_active;   ///< `false` for unused table slots.
    proto_bool is_wildcard; ///< `true` when path ends with `*`.
    proto_bool is_param;    ///< `true` when the path contains a `:name` segment.
    proto_bool is_regex;    ///< `true` when the path is a regex (see on_regex()).
    pc_iface iface_filter;  ///< Interface gate; PC_IFACE_ANY (0) = match any interface.
} Route;

// ---------------------------------------------------------------------------
// Chunked (streaming) response writer
// ---------------------------------------------------------------------------

struct tcp_pcb; // forward decl (full type pulled in via the transport layer)

/**
 * @brief Source callback that produces a chunked response body incrementally.
 *
 * Passed to send_chunked() and called repeatedly - possibly across
 * several server loops, as the TCP send window drains - until it returns 0. Each
 * call writes up to @p cap bytes of the next body piece into @p buf and returns
 * the count; the HTTP chunk framing (size line + CRLFs + terminator) is added by
 * the server. Track your position across calls in @p ctx. This pull/generator
 * model lets the server page an arbitrarily large body to the socket in constant
 * memory without ever blocking the worker or truncating at the send window.
 *
 * @warning @p ctx must stay valid until the body is fully sent. A body that fits
 * in a single send window finishes during the send_chunked() call, but a larger
 * one resumes on later loops, so @p ctx must NOT point at the caller's stack: use
 * static / global storage (a per-connection instance if requests can overlap), or
 * generate the body from durable state.
 *
 * @param buf  destination for the next body bytes.
 * @param cap  maximum bytes to write into @p buf on this call.
 * @param ctx  caller state pointer, passed through from send_chunked().
 * @return bytes written into @p buf (<= @p cap), or 0 to end the body.
 */
typedef size_t (*ChunkSource)(uint8_t *buf, size_t cap, void *ctx);

// ---------------------------------------------------------------------------
// The server API - flat, one global namespace, no class
// ---------------------------------------------------------------------------
//
// Usage:
//   void handle_api(uint8_t slot_id, HttpReq *req) { send_text(slot_id, 200, "application/json", "{}"); }
//   void setup()  { init_wifi_physical("SSID", "PW"); on("/api", HTTP_GET, handle_api); begin(80); }
//   void loop()   { handle(); }

/**
 * @brief Run the global middleware chain for a request.
 * @return true if a middleware returned MW_HALT (a response was sent and
 *         dispatch must stop); false to continue to route matching.
 */
proto_bool run_middleware(uint8_t slot_id, HttpReq *req);

/**
 * @brief Built-in fixed-window rate-limit check (see enable_rate_limit()).
 * @return true if the request was rejected with 429 (response sent, dispatch
 *         must stop); false when rate limiting is disabled or within budget.
 */
proto_bool rate_limit_check(uint8_t slot_id);

/**
 * @brief Evaluate whether a route pattern matches a request path.
 *
 * Wildcard routes end with `*`; the `*` is replaced by a prefix match.
 * Exact routes use strcmp.
 *
 * @param route       Null-terminated route pattern.
 * @param is_wildcard True if route ends with `*`.
 * @param req_path    Null-terminated path from the parsed request.
 * @return True if the route matches the request path.
 */

/// @brief Record a response for stats + the access-log hook. Reads method/path from http_pool[slot_id].
void note_response(uint8_t slot_id, int code, int body_len);

#if PC_ENABLE_KEEPALIVE
/**
 * @brief Decide whether the current response should keep the connection alive.
 *
 * Only a cleanly-parsed request (PARSE_COMPLETE) is eligible: HTTP/1.1 keeps
 * alive unless the client sent `Connection: close`; HTTP/1.0 keeps alive only
 * with `Connection: keep-alive`. On a true return the slot's request tally is
 * incremented; the PC_KEEPALIVE_MAX_REQUESTS-th request returns false so
 * the connection is closed deliberately. Always false with keep-alive off.
 */
#endif

/**
 * @brief Finish a response: flush, then close the connection (close path) or
 *        recycle the slot for the next request (keep-alive). Records the
 *        response and resets the HTTP parser either way. Addresses the
 *        connection by slot alone; the transport resolves the pcb internally.
 *
 * @param pre_flushed the caller already emitted the final bytes with Tcp.conn->send_flush()
 *        (write+tcp_output coalesced into one marshal), so skip the redundant flush here.
 */
void pc_resp_end(uint8_t slot_id, int code, int body_len, proto_bool keep, proto_bool pre_flushed);

/**
 * @brief Resolve the Connection response header and report keep-alive intent.
 *
 * One owner for the keep-alive decision: returns "Connection: keep-alive\r\n"
 * or "Connection: close\r\n" and, via @p keep_out, whether the slot is kept
 * alive. Always reports close when keep-alive is compiled out.
 */
const char *pc_resp_conn_hdr(uint8_t slot_id, proto_bool *keep_out);

/**
 * @brief Append the shared response trailer (CORS block, custom headers, the
 *        Connection header, and the terminating blank line) to a header buffer
 *        already holding the status line and per-response headers. @p hlen is
 *        the current length; returns the new total length.
 */
int proto_append_resp_trailer(char *buf, size_t cap, int hlen, uint8_t slot_id, const char *cl);

/// @brief Resume a pending chunked response: pull + frame chunks until the send window is full, finish when
/// drained.
void chunk_send_pump(uint8_t slot_id);

// serve_static_request / serve_file_internal / file_send_pump: file_serving.h

// try_serve_dav / serve_dav_request / dav_send_status / dav_stream_put_*: server/webdav_handler.h

/**
 * @brief Look up and invoke the first matching route for the given slot.
 *
 * If CORS is enabled and the method is OPTIONS, the preflight is
 * short-circuited here with a 204 response.  If no route matches, the
 * not-found handler is invoked (or a default 404 is sent).
 *
 * @param slot_id Connection slot to dispatch.
 */

/// @brief Route-selection predicate: true if route @p r is active, its path pattern matches
///        @p req, and its interface filter admits this slot's connection. Matching a param route
///        captures its path parameters into @p req as a side effect (as the inline match did).

/// @brief Dispatch a route whose path + interface already matched (WS/SSE/STATIC/HTTP + auth).
/// @return true when a response was sent (the caller stops); false to keep scanning later routes,
///         with @p path_matched / @p allow_buf updated for a possible 405.

#if PC_ENABLE_CSRF
/// @brief Built-in CSRF gate: serve the `GET /csrf` token endpoint and enforce a valid
///        `X-CSRF-Token` on every state-changing method. @return true if a response was sent.
#endif

#if PC_ENABLE_WEBSOCKET
/// @brief Complete (or reject) a ROUTE_WS handshake per RFC 6455 §4.2.1. Always responds.
#endif

#if PC_ENABLE_AUTH
/// @brief Enforce route @p r's auth (lockout gate + Digest/Basic credential check, with lockout
///        accounting). @return true if authorized; on failure the 401/429 is already sent.
#endif

#if PC_ENABLE_WEBSOCKET
/// @brief Invoke the registered WS message handler for a completed frame on @p ws.
void ws_dispatch_message(const WsConn *ws);
/// @brief Invoke the registered WS close handler for @p ws.
void ws_dispatch_close(const WsConn *ws);
#endif

/**
 * @brief Construct a PC with an empty routing table.
 *
 * All route slots are marked inactive.  CORS is disabled.  The
 * not-found handler is null (falls back to built-in 404 response).
 */

/**
 * @brief Register a port to listen on when begin() is called.
 *
 * Call this before begin() for each port you want the server to accept
 * connections on.  The @p proto argument tells the session layer which
 * protocol handler to invoke for events on this port.
 *
 * For the common single-HTTP-port case, prefer `begin(80)` which calls
 * this internally.  Use the explicit listen() + begin() form when you
 * need multiple ports (e.g., HTTP on 80 and Telnet on 23).
 *
 * @code
 * server.listen(80, ConnProto::PROTO_HTTP);
 * server.listen(23, ConnProto::PROTO_TELNET);
 * server.begin();
 * @endcode
 *
 * @param port  TCP port to open.
 * @param proto Application protocol; defaults to ConnProto::PROTO_HTTP.
 * @return the listener id (a non-negative index) on success - pass it to
 *         pc_relay_publish() / pc_ssh_forward_begin(); PC_ERR_LISTENER_FULL if the pool is
 * full.
 */
int32_t listen(uint16_t port, ConnProto proto);

/**
 * @brief Initialize all connection slots and open all registered listeners.
 *
 * Resets the HTTP parser pool, calls DeterministicAsyncTCP::pool_init(),
 * then calls listener_add() for each port registered via listen().
 * Requires at least one prior listen() call.  For the common single-port
 * case use begin(port, cfg) instead.
 *
 * @param cfg  Optional runtime configuration.  Pass NULL for defaults.
 * @return PC_OK on success; PC_ERR_NO_LISTENERS if no ports were
 *         registered; PC_ERR_LISTEN_FAILED if a listener could not open.
 */
int32_t proto_begin(const WebServerConfig *cfg);

/**
 * @brief Convenience overload: register @p port as HTTP and start listening.
 *
 * Equivalent to `listen(port); begin(cfg);`.  Preserved for backward
 * compatibility with single-port sketches.
 *
 * @param port TCP port to listen on (typically 80).
 * @param cfg  Optional runtime configuration.  Pass NULL for defaults.
 * @return PC_OK on success; a negative pc_result on failure.
 */
int32_t begin_http(uint16_t port, const WebServerConfig *cfg);

#if PC_ENABLE_TLS
/**
 * @brief Load the TLS server certificate + private key (call before begin).
 *
 * Initializes the static-pool mbedTLS engine. Required before any TLS
 * listener will complete a handshake. PEM buffers must include the trailing
 * NUL in the length; DER is also accepted.
 *
 * @return true on success; false if the cert/key/pool setup failed.
 */
proto_bool tls_cert(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len);

/**
 * @brief Register a TLS (HTTPS) HTTP listener on @p port (typically 443).
 *
 * Like listen() but connections accepted here run a TLS handshake first.
 * Call tls_cert() first, then begin(). @return PC_OK or an error code.
 */
int32_t listen_tls(uint16_t port);

/**
 * @brief Convenience: load cert/key, register a TLS listener, and start.
 *
 * Equivalent to `tls_cert(...); listen_tls(port); begin(cfg);`.
 *
 * @param port     TLS port (typically 443).
 * @param cert     Server certificate (chain).
 * @param cert_len Length incl. trailing NUL for PEM.
 * @param key      Server private key.
 * @param key_len  Length incl. trailing NUL for PEM.
 * @param cfg      Optional runtime config.
 * @return PC_OK on success; a negative code, or PC_ERR_LISTEN_FAILED
 * if the TLS engine could not initialize.
 */
int32_t begin_tls(uint16_t port, const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len,
                  const WebServerConfig *cfg);

#if PC_ENABLE_MTLS
/**
 * @brief Require a verified client certificate (mTLS).
 *
 * Call after tls_cert() (or begin_tls()) and before connections arrive. Sets
 * @p ca as the trust anchor and switches the handshake to require a client
 * certificate chaining to it; a client that presents none, or an untrusted
 * one, is rejected during the handshake.
 *
 * @param ca     CA certificate (chain).
 * @param ca_len Length incl. trailing NUL for PEM.
 * @return true on success; false if the engine is not ready or the CA failed
 *         to parse.
 */
proto_bool tls_require_client_cert(const uint8_t *ca, size_t ca_len);

/**
 * @brief Copy the connecting client's verified certificate subject DN.
 *
 * Use inside a handler to identify the mTLS peer (e.g. for authorization or
 * logging). Valid only on a TLS connection whose handshake required and
 * verified a client cert.
 *
 * @param slot_id  Connection slot (the handler's id).
 * @param out      Destination buffer (always NUL-terminated on success).
 * @param out_len  Capacity of @p out.
 * @return subject length written, or <0 if there is no verified client cert.
 */
int tls_client_subject(uint8_t slot_id, char *out, size_t out_len);
#endif // PC_ENABLE_MTLS
#endif // PC_ENABLE_TLS

#if PC_ENABLE_HTTP3
/**
 * @brief Enable the HTTP/3 (QUIC) server: load its Ed25519 leaf certificate + key and choose the
 * UDP port. Call before begin(); begin() then binds the port and serves HTTP/3 through the same
 * routes as HTTP/1.1 and HTTP/2. @p cert_der is a DER X.509 leaf whose public key is the Ed25519
 * key matching @p ed25519_seed (its 32-byte private seed). @return true if stored.
 *
 * Profile: TLS_AES_128_GCM_SHA256 + X25519 + Ed25519 (a client offering none of these is refused).
 */
proto_bool pc_h3_cert(const uint8_t *cert_der, size_t cert_len, const uint8_t ed25519_seed[32], uint16_t port);

/**
 * @brief Internal: run a completed HTTP/3 request through the shared route dispatcher on the
 * reserved conn-pool slot (called by the pc_quic_server request trampoline, not by app code). The
 * response routes back to @p stream_id on @p conn_id via send_text() -> pc_quic_server_respond.
 */
void dispatch_h3_request(uint32_t conn_id, uint64_t stream_id, const char *method, const char *path,
                         const char *authority, const uint8_t *body, size_t body_len);
#endif // PC_ENABLE_HTTP3

/**
 * @brief Gracefully stop the server.
 *
 * Aborts all active connections, closes the listener, frees the event
 * queue, and resets all HTTP parser slots.  The WiFi and TCP/IP stack
 * remain active.  Call begin() or restart() to bring the server back up.
 */
void stop();

/**
 * @brief Hard-reset all connections and re-open all registered listeners.
 *
 * Equivalent to stop() followed by begin(cfg) using the ports and protocols
 * registered via listen() (or the port passed to begin(port)).  The WiFi
 * and TCP/IP stack are not touched.
 *
 * Calling restart() before any listen() / begin(port) has no effect and
 * returns -1.
 *
 * @param cfg Optional new runtime configuration.  Pass NULL to reuse
 *            the compile-time default (CONN_TIMEOUT_MS).
 */
int32_t restart(const WebServerConfig *cfg);

/**
 * @brief Register a route handler.
 *
 * Routes are matched in registration order (first match wins).
 * A trailing `*` in @p path enables prefix matching: `"/api/"` followed by `*`
 * matches `"/api/users"`, `"/api/devices"`, etc.
 *
 * @param path     URL path pattern, e.g. `"/api/status"`, or a prefix ending in a `*` wildcard.
 *                 Must be ≤ `MAX_PATH_LEN - 1` characters.
 * @param method   HTTP method this route accepts.
 * @param callback Function called when this route is matched.
 *
 * @note Registering more than MAX_ROUTES routes silently drops extras.
 */
void on_http(const char *path, HttpMethod method, Handler callback);

/**
 * @brief Register a route that only matches on a specific network interface.
 *
 * Identical to on(path, method, callback) but the route is invisible unless
 * the request arrived on @p iface (pc_iface::PC_IFACE_STA or pc_iface::PC_IFACE_AP). A
 * non-matching interface falls through to other routes / 404, so you can,
 * e.g., expose a provisioning UI only on the softAP and the app API only on
 * the station link. Requires set_ap_ip() to have been called so connections
 * can be classified.
 *
 * @param path     URL path pattern.
 * @param method   HTTP method.
 * @param callback Handler invoked on a match.
 * @param iface    pc_iface::PC_IFACE_STA or pc_iface::PC_IFACE_AP (pc_iface::PC_IFACE_ANY = no filter).
 */
void on_http_iface(const char *path, HttpMethod method, Handler callback, pc_iface iface);

/**
 * @brief Register a route whose path is a regular expression.
 *
 * The whole request path must match @p pattern (implicitly anchored). The
 * matcher is a small, bounded, allocation-free backtracker supporting:
 * `.` (any char), `*` `+` `?` quantifiers, character classes `[...]` /
 * `[^...]` with `a-z` ranges, the shorthands `\d \w \s` (and `\D \W \S`),
 * and `\` to escape a metacharacter. It is **non-capturing** and has no
 * groups `()` or alternation `|` - use `:name` path parameters (see the
 * other on() overload notes / http_get_param) when you need to capture.
 * Matching is bounded by RE_MAX_STEPS and fails closed past that budget.
 *
 * @code
 *   server.on_regex("/sensor/[0-9]+", HTTP_GET, handle_sensor);
 *   server.on_regex("/img/.+\\.png", HTTP_GET, handle_png);
 * @endcode
 *
 * @param pattern  Regex the full path must match (stored, <= MAX_PATH_LEN-1).
 * @param method   HTTP method.
 * @param callback Handler invoked on a match.
 */
void on_regex(const char *pattern, HttpMethod method, Handler callback);

/**
 * @brief Tell the server the softAP IPv4 address for STA/AP route filtering.
 *
 * Each accepted connection is tagged pc_iface::PC_IFACE_AP when its local IP equals
 * @p ap_ip, else pc_iface::PC_IFACE_STA. Call once after starting the softAP, e.g.
 * `server.set_ap_ip(pc_net_ap_ip())` (already network byte order).
 * Without it, every connection is treated as pc_iface::PC_IFACE_STA.
 *
 * @param ap_ip softAP IPv4 address in network byte order (0 to clear).
 */
void set_ap_ip(uint32_t ap_ip);

#if PC_ENABLE_AUTH
/**
 * @brief Register a route handler protected by HTTP authentication.
 *
 * If the request does not include valid credentials, the library sends
 * `401 Unauthorized` with the appropriate `WWW-Authenticate` challenge
 * (`Basic`, or `Digest` with SHA-256 + `qop=auth` per RFC 7616) and the
 * callback is not invoked.
 *
 * @param path     URL path pattern.
 * @param method   HTTP method.
 * @param callback Handler invoked only on successful authentication.
 * @param realm    WWW-Authenticate realm displayed by the browser.
 * @param user     Required username.
 * @param pass     Required password.
 * @param digest   When true, use Digest authentication instead of Basic.
 */
void on_http_auth(const char *path, HttpMethod method, Handler callback, const char *realm, const char *user,
                  const char *pass, proto_bool digest);
#endif // PC_ENABLE_AUTH

// serve_file / serve_static: file_serving.h

#if PC_ENABLE_WEBDAV
/**
 * @brief Mount a filesystem subtree as a WebDAV share (RFC 4918).
 *
 * Registers a wildcard route so every request under @p url_prefix is handled
 * as WebDAV against @p fs_root on the mounted volume. The supported methods are
 * OPTIONS, PROPFIND (Depth 0/1), GET, HEAD, PUT, DELETE, MKCOL, COPY, MOVE,
 * and advisory LOCK/UNLOCK; a client such as rclone, cadaver, curl, or a
 * mounted network drive can browse and edit files. The request path beyond
 * the prefix is appended to @p fs_root (paths containing `..` are rejected).
 *
 * Limits (see PC_ENABLE_WEBDAV): PROPFIND builds a 207 into a
 * PC_WEBDAV_BUF_SIZE buffer and lists at most PC_WEBDAV_MAX_ENTRIES
 * children; PUT buffers the body (bounded by BODY_BUF_SIZE); COPY handles
 * files (not collections); locks are advisory (issued, not enforced);
 * PROPPATCH is unsupported. Combine with per-route auth and HTTPS before
 * exposing a writable share.
 *
 * @code
 * server.dav("/dav", NULL, "/dav");   // dav://<ip>/dav -> /dav on the board's own storage
 * @endcode
 *
 * @param url_prefix URL prefix to mount (with or without a trailing `*`).
 * @param file_sys   Backend to serve from; NULL uses whatever is mounted (the board's).
 * @param fs_root    Subtree on that backend (persistent string).
 */
void dav(const char *url_prefix, const pc_mnt_backend *file_sys, const char *fs_root);
#endif // PC_ENABLE_WEBDAV

/**
 * @brief Register a fallback handler for unmatched requests.
 *
 * Called instead of sending a built-in 404 when no route matches.
 * The callback may call send_text() to return a custom error page.
 *
 * @param callback Handler to invoke on a 404 condition.
 */
void on_not_found(Handler callback);

/**
 * @brief Install a per-request access-log callback (one hook, no buffering).
 *
 * @p cb is invoked once per response with the method, path, status code, and
 * response body length. Pass NULL to remove. See @ref RequestLogCb.
 */
void on_request_log(RequestLogCb cb);

/**
 * @brief Register a middleware to run before every request is dispatched.
 *
 * Middlewares run in registration order (see @ref Middleware) ahead of route
 * matching, after the built-in rate-limit check. Up to MAX_MIDDLEWARE may be
 * registered; further calls are ignored. Use this to add cross-cutting
 * behavior - request logging, custom auth, header injection, feature gating -
 * composed independently of individual routes.
 *
 * @code
 *   static MwResult log_mw(uint8_t slot, HttpReq *req) {
 *       Serial.printf("%s %s\n", req->method, req->path);
 *       return MW_NEXT;                  // fall through to the handler
 *   }
 *   server.use(log_mw);
 * @endcode
 *
 * @param mw Middleware function pointer (must not be NULL).
 */
void use(Middleware mw);

/**
 * @brief Enable a built-in fixed-window request rate limiter.
 *
 * Counts all incoming requests in a sliding fixed window; once more than
 * @p max_requests arrive within @p window_ms the server answers further
 * requests in that window with `429 Too Many Requests` (plus a `Retry-After`
 * header) instead of dispatching them. The check runs before the middleware
 * chain and route matching, so it bounds work under flood. State is a few
 * per-server counters (no heap, no per-IP table) - a global throttle suited
 * to a small device behind a trusted LAN. For connection-level flood defense
 * see also `PC_ENABLE_ACCEPT_THROTTLE`.
 *
 * @param max_requests Requests allowed per window. Pass 0 to disable.
 * @param window_ms    Window length in milliseconds (must be > 0).
 */
void enable_rate_limit(uint16_t max_requests, uint32_t window_ms);

#if PC_ENABLE_STATS
/**
 * @brief Send a JSON runtime-stats snapshot and close the connection.
 *
 * Body: uptime_ms, total requests, 2xx/4xx/5xx counts, active connection-pool
 * slots, and (on ESP32) free heap. Wire it to a route:
 * @code
 *   server.on("/stats", HTTP_GET, [](uint8_t id, HttpReq *) { server.stats(id); });
 * @endcode
 *
 * @param slot_id Connection slot to respond on.
 */
void stats(uint8_t slot_id);
#endif

#if PC_ENABLE_METRICS
/**
 * @brief Respond with runtime metrics in Prometheus text exposition format.
 *
 * Convenience for a `/metrics` route: emits the stats counters as Prometheus
 * gauges/counters (Content-Type `text/plain; version=0.0.4`) so a Prometheus
 * server can scrape the device.
 * @code
 *   server.on("/metrics", HTTP_GET, [](uint8_t id, HttpReq *) { server.metrics(id); });
 * @endcode
 *
 * @param slot_id Connection slot to respond on.
 */
void metrics(uint8_t slot_id);
#endif

/**
 * @brief Enable CORS by pre-building the Access-Control headers.
 *
 * Once called, every response produced by send_text() and send_empty()
 * includes the CORS headers.  OPTIONS requests are intercepted and
 * answered with 204 automatically (preflight short-circuit).
 *
 * @param origin `Access-Control-Allow-Origin` value, e.g. `"*"` or
 *               `"https://example.com"`.  Pass `""` to disable CORS.
 */
void set_cors(const char *origin);

/**
 * @brief Set the `Cache-Control` header emitted for static files.
 *
 * Applies to serve_file() / serve_static() responses (beside the ETag), so
 * browsers can cache assets and revalidate cheaply with `If-None-Match`.
 * Examples: `"no-cache"` (cache but always revalidate), `"max-age=3600"`,
 * `"public, max-age=31536000, immutable"`. Pass `""` / `NULL` to disable.
 *
 * @param value `Cache-Control` directive, or empty/null to emit no header.
 */
void set_cache_control(const char *value);

#if PC_ENABLE_HTTP_DELIVERY
/**
 * @brief Set `Cache-Control` to an RFC 5861 stale-while-revalidate policy.
 *
 * Emits `public, max-age=N, stale-while-revalidate=M` on served files. Within `max_age_s` a
 * client uses its copy outright; for `swr_s` beyond that it may serve the stale copy *and*
 * refresh in the background - so a sleepy or busy device never blocks the page, and the visitor
 * after it gets the fresh copy. Pass `swr_s` 0 for a plain `max-age`.
 *
 * @return true if the directive was built and applied.
 */
proto_bool set_cache_control_swr(uint32_t max_age_s, uint32_t swr_s);
#endif

/**
 * @brief Drive the server - call every Arduino `loop()` iteration.
 *
 * On ESP32 `begin()` spawns the server worker task(s) (see PC_WORKER_COUNT),
 * which run the pipeline on their own core; `handle()` is then a no-op and your
 * `loop()` is free for application code. On host builds (and if no worker task
 * is running) `handle()` drives one service iteration inline, so existing
 * sketches and the native tests keep working unchanged.
 *
 * One service iteration (see service_once()):
 * 1. Calls `DeterministicAsyncTCP::check_timeouts()` to kill stale
 *    connections.
 * 2. Drains the event queue (connections, data, disconnects, errors).
 * 3. Scans all connection slots for `PARSE_COMPLETE` requests and
 *    dispatches them to the matching route handler.
 * 4. Auto-sends 400 for any slot stuck in `PARSE_ERROR`.
 * 5. Auto-sends 413 for any slot stuck in `PARSE_ENTITY_TOO_LARGE`.
 * 6. Auto-sends 414 for any slot stuck in `PARSE_URI_TOO_LONG`.
 *
 * Threading note: with the worker task running, route/WS/SSE handlers execute
 * in the worker task. Do server I/O from handlers; pushing from `loop()` (e.g.
 * SSE broadcast on a timer) runs concurrently with the worker and is made
 * thread-safe in a later phase.
 */
void handle();

/**
 * @brief Run exactly one service iteration for worker @p worker_id (the body
 *        driven by that worker's task, or by handle() when no task is running).
 *
 * Services only the connection slots owned by @p worker_id, so multiple workers
 * run disjoint slot sets in parallel. At PC_WORKER_COUNT=1 worker 0 owns
 * every slot. Public so the worker task can invoke it; application code should
 * call handle() rather than this directly.
 */
void service_once(int worker_id);

/**
 * @brief The instance-bound HTTP poll pump for one slot (the HTTP ProtoHandler's on_poll).
 *
 * Installed into the HTTP handler at begin() via http_proto_set_poll() so the worker dispatch
 * loop pumps HTTP through the same uniform ProtoHandler seam as every other protocol - there is no
 * HTTP special case in the loop. Runs the file/chunk send pumps, the WebSocket + SSE drains, the
 * keep-alive re-parse, and dispatches a completed request into this server's routes. Public only so
 * the poll trampoline can reach it (like service_once); application code never calls it directly.
 * @param slot_id Connection slot to pump.
 */
void http_poll_slot(uint8_t slot_id);

/**
 * @brief Run @p fn(@p arg) on the worker that owns connection @p slot.
 *
 * The thread-safe way to push to a connection from outside a handler - e.g. an
 * SSE broadcast or a ws_send from loop() or a sensor task. Calling the send API
 * directly from another task would race the worker that owns the slot; instead
 * wrap the send in @p fn and defer it, and it runs single-threaded in the
 * owning worker's context. @p arg must stay valid until the callback runs. On
 * host builds (no worker task) it runs inline immediately.
 *
 * @return false if the slot is invalid or the worker's defer queue is full.
 */
proto_bool defer(uint8_t slot, pc_deferred_fn fn, void *arg);

/**
 * @brief Send an HTTP response with a body and close the connection.
 *
 * Writes status line, Content-Type, Content-Length, optional CORS
 * headers, and the payload; then calls tcp_close (tcp_abort on failure).
 * Always calls http_reset() at the end to free the parser slot.
 *
 * @param slot_id      Connection slot index returned by the router.
 * @param code         HTTP status code (200, 404, 500, …).
 * @param content_type MIME type string, e.g. `"application/json"`.
 * @param payload      Null-terminated response body.
 *
 * @note If the underlying PCB has already been freed (e.g. by a
 *       concurrent timeout), this function is a no-op that just
 *       resets the slot.
 */
void send_text(uint8_t slot_id, int code, const char *content_type, const char *payload);

/**
 * @brief Send an HTTP response with an explicit-length (possibly binary) body.
 *
 * Same as send_text() above but the body length is given, so the body may contain NUL
 * bytes (protobuf, gRPC-web frames, octet-stream, images). @p body_len is bounded
 * by the single-write limit (65535); larger bodies need the chunked/file path.
 *
 * @param slot_id      Connection slot index returned by the router.
 * @param code         HTTP status code.
 * @param content_type MIME type string, e.g. `"application/grpc-web+proto"`.
 * @param body         Response body (may contain NULs); not required to be terminated.
 * @param body_len     Number of body octets.
 */
void send_bin(uint8_t slot_id, int code, const char *content_type, const uint8_t *body, size_t body_len);

/**
 * @brief Send a headers-only HTTP response and close the connection.
 *
 * Equivalent to send_text() with an empty body and Content-Length: 0.
 * Useful for 204 No Content, 304 Not Modified, HEAD responses, and
 * CORS preflight replies.
 *
 * @param slot_id Connection slot index.
 * @param code    HTTP status code.
 */
void send_empty(uint8_t slot_id, int code);

/**
 * @brief Send an HTTP redirect (Location header, empty body) and close.
 *
 * Convenience for the common `/`→`/index.html` or canonical-host case.
 *
 * @param slot_id  Connection slot index.
 * @param code     Redirect status: 301, 302, 303, 307, or 308. Any other
 *                 value is treated as 302 Found.
 * @param location Value for the `Location` response header.
 */
void redirect(uint8_t slot_id, int code, const char *location);

/**
 * @brief Send a response body with `{{name}}` placeholders substituted.
 *
 * Streams @p tmpl to the client, replacing each `{{name}}` token with the
 * string returned by @p resolver (NULL → empty). The body is never
 * buffered whole: it is walked twice - once to compute Content-Length, once
 * to write - so memory use is constant regardless of body size. A `{{` with
 * no matching `}}` (or a name longer than 32 chars) is emitted literally.
 *
 * @param slot_id      Connection slot index.
 * @param code         HTTP status code.
 * @param content_type Response Content-Type.
 * @param tmpl         Null-terminated template text.
 * @param resolver     Placeholder resolver (see TemplateVar), or NULL.
 */
void send_template(uint8_t slot_id, int code, const char *content_type, const char *tmpl, TemplateVar resolver);

/**
 * @brief Stream a response body of unknown length via chunked transfer.
 *
 * Writes the status line and headers (including `Transfer-Encoding: chunked`,
 * plus any CORS / queued custom headers), then pulls the body from @p source
 * one piece at a time, adding the chunk framing and the terminating chunk. The
 * body is never buffered whole and the send paces with the TCP window - paging
 * across server loops as it drains - so output size is unbounded in constant
 * memory and a body larger than the send buffer is never truncated. This is the
 * complement to send_text(), which needs the full payload up front. A HEAD request
 * sends the headers only (@p source is not called).
 *
 * @param slot_id      Connection slot index.
 * @param code         HTTP status code.
 * @param content_type Response Content-Type.
 * @param source       Generator that produces the body (must not be NULL).
 * @param ctx          Opaque state handed to @p source; see @ref ChunkSource
 *                     for the lifetime requirement (must outlive the response).
 */
void send_chunked(uint8_t slot_id, int code, const char *content_type, ChunkSource source, void *ctx);

/**
 * @brief Queue a custom response header for the next send on this slot.
 *
 * Call from inside a handler before send_text() / send_empty() / redirect().
 * The header is appended to a fixed per-slot buffer (EXTRA_HDR_BUF_SIZE)
 * and emitted verbatim as `Name: value\r\n`. Headers that would overflow
 * the buffer are dropped whole (never truncated mid-line). The buffer is
 * cleared automatically at the start of each request.
 *
 * @param slot_id Connection slot index.
 * @param name    Header field name (no `:` or CRLF).
 * @param value   Header field value (no CRLF).
 */
void proto_add_response_header(uint8_t slot_id, const char *name, const char *value);

/**
 * @brief Queue a `Set-Cookie` response header for the next send on this slot.
 *
 * Emits `Set-Cookie: name=value\r\n`, or `Set-Cookie: name=value; attrs\r\n`
 * when @p attrs is non-null (e.g. `"Path=/; HttpOnly; Max-Age=3600"`).
 * Shares the per-slot buffer with proto_add_response_header().
 *
 * @param slot_id Connection slot index.
 * @param name    Cookie name.
 * @param value   Cookie value.
 * @param attrs   Optional `;`-separated attribute string, or NULL.
 */
void set_cookie(uint8_t slot_id, const char *name, const char *value, const char *attrs);

/**
 * @brief Discard any headers/cookies queued for this slot.
 *
 * @param slot_id Connection slot index.
 */
void clear_response_headers(uint8_t slot_id);

/**
 * @brief Guess a `Content-Type` from a path's file extension.
 *
 * Small static extension→type table covering the common web asset types
 * (html, css, js, json, svg, png, jpg, gif, ico, txt, wasm, woff2, …).
 * Case-insensitive on the extension. Falls back to
 * `"application/octet-stream"` when the extension is unknown or absent.
 *
 * @param path  File path or name (e.g. "/css/site.css").
 * @return Static content-type string (never null).
 */
const char *mime_type(const char *path);

#if PC_ENABLE_DIAG
/**
 * @brief Send the diagnostic JSON and close the connection.
 *
 * Responds with 200 application/json containing the compile-time feature
 * flags and all capacity constants.  Only available when
 * PC_ENABLE_DIAG is set to 1 - disable before deploying to production.
 *
 * @param slot_id Connection slot index.
 */
void diag(uint8_t slot_id);
#endif

#if PC_ENABLE_WEBSOCKET
// -----------------------------------------------------------------------
// WebSocket API
// -----------------------------------------------------------------------

/**
 * @brief Register a WebSocket upgrade route.
 *
 * When a GET request arrives for @p path with `Upgrade: websocket`, the
 * library performs the RFC 6455 handshake automatically and fires
 * @p on_connect.  Subsequent frames fire @p on_message.  Closing the
 * connection fires @p on_close.
 *
 * Ping frames are answered with Pong automatically; no handler needed.
 *
 * @param path        URL path the client connects to, e.g. `"/ws"`.
 * @param on_connect  Fired once when the handshake completes.  May be NULL.
 * @param on_message  Fired for each text or binary frame.  Must not be NULL.
 * @param on_close    Fired when the connection closes.  May be NULL.
 */
void on_ws(const char *path, WsConnectHandler on_connect, WsMessageHandler on_message, WsCloseHandler on_close);

/**
 * @brief Send a text frame to a WebSocket client.
 *
 * @param ws_id    Index into ws_pool[] (from the WsConnectHandler or WsMessageHandler).
 * @param text     Null-terminated UTF-8 string to send.
 */
void ws_send_text(uint8_t ws_id, const char *text);

/**
 * @brief Send a binary frame to a WebSocket client.
 *
 * @param ws_id    Index into ws_pool[].
 * @param data     Payload bytes.
 * @param len      Payload length in bytes; must be <= WS_FRAME_SIZE.
 */
void ws_send_binary(uint8_t ws_id, const uint8_t *data, uint16_t len);

/**
 * @brief Initiate a graceful WebSocket close.
 *
 * Sends a Close frame with WS_CLOSE_NORMAL and marks the slot WS_CLOSED.
 * The on_close handler fires on the next handle() call.
 *
 * @param ws_id  Index into ws_pool[].
 */
void ws_disconnect(uint8_t ws_id);
#endif // PC_ENABLE_WEBSOCKET

#if PC_ENABLE_SSE
// -----------------------------------------------------------------------
// Server-Sent Events API
// -----------------------------------------------------------------------

/**
 * @brief Register a Server-Sent Events endpoint.
 *
 * When a GET request arrives for @p path, the library sends the SSE
 * headers and keeps the connection open.  @p on_connect fires so the
 * handler can push an initial event with pc_sse_send().
 *
 * @param path        URL path, e.g. `"/events"`.
 * @param on_connect  Fired when a client subscribes.  May be NULL.
 */
void on_sse(const char *path, SseConnectHandler on_connect);

/**
 * @brief Push an event to one SSE client.
 *
 * Formats and sends `event: ...\ndata: ...\nid: ...\n\n` to the client
 * on @p pc_sse_id.  Any field may be NULL to omit it from the output.
 * The data field is required; passing NULL sends nothing.
 *
 * @param pc_sse_id  Index into pc_sse_pool[].
 * @param data    Event data string (required).
 * @param event   Optional event name (sets the `event:` field).
 * @param id      Optional event ID (sets the `id:` field).
 */
void pc_sse_send(uint8_t pc_sse_id, const char *data, const char *event, const char *id);

/**
 * @brief Push an event to all connected SSE clients on a given path.
 *
 * Iterates pc_sse_pool[] and calls pc_sse_send() for every active client
 * whose path matches @p path.
 *
 * @param path   SSE endpoint path, e.g. `"/events"`.
 * @param data   Event data string.
 * @param event  Optional event name.
 * @param id     Optional event ID.
 */
void pc_sse_broadcast(const char *path, const char *data, const char *event, const char *id);
#endif // PC_ENABLE_SSE

// ---------------------------------------------------------------------------
// Cross-file server API
// ---------------------------------------------------------------------------
//
// Each function is defined by the file that owns the state behind it. Where state is involved the
// declaration is a reader, never the storage, so the owner stays the only writer.

/** @brief Reason phrase for an HTTP status code (e.g. 404 -> "Not Found"). */

/**
 * @brief The fixed reply sent when a response's own headers will not fit RESP_HDR_BUF_SIZE.
 *
 * A header block cut short has no terminating CRLF, so the peer keeps reading the body as headers
 * and the connection desynchronizes - which is why truncating is not an option and this always-
 * fitting reply goes out instead. Connection: close, because the request is not recoverable.
 */
extern const char PC_RESP_HDR_OVERFLOW[];

/** @brief Length of PC_RESP_HDR_OVERFLOW, taken with sizeof where the array bound is still visible. */
extern const size_t PC_RESP_HDR_OVERFLOW_LEN;

/** @brief Initialize the common fields (path, flags) of a route-table entry from its pattern. */
void fill_route_base(Route *r, const char *path);

/** @brief Format @p t as an RFC 1123 GMT date into @p out (cap bytes); @p out is emptied for t <= 0. */
// http_rfc1123: file_serving.h

/** @brief True if the request in slot @p slot_id used the HEAD method (send headers, no body). */

/** @brief Whole-path regex match (anchored both ends; bounded by RE_MAX_STEPS, fails closed). */
proto_bool regex_match(const char *pattern, const char *path);

// An outbound transfer owns its slot: the poll skips the rest of the pipeline until the body is
// out. Each kind is owned by the file that runs it, so neither continuation struct is declared
// anywhere - the poll asks each owner whether it holds the slot rather than reading its state.

/** @brief True while a chunked response is paging out on @p slot (owner: server/response.cpp). */
proto_bool pc_resp_holds_slot(uint8_t slot);

/** @brief Drop this file's per-run configuration and any in-flight chunked send. See
 *         pc_server_reset(), which is what callers use. */
void pc_resp_reset(void);

/** @brief Drop the middleware chain and the rate-limit window. See pc_server_reset(), which is
 *         what callers use. */
void pc_middleware_reset(void);

/**
 * @brief Return the server to its start-of-run state: routes, dispatch hooks, registered listeners,
 *        response configuration, and the middleware chain.
 *
 * Firmware has no use for this - a device configures itself once and runs - but a test case
 * registers its own routes and handlers, and without a way back to the start each case would
 * inherit every route the previous ones left behind. This is what `server = PC()` meant when there
 * was an instance to reassign.
 *
 * It does NOT touch the connection pool or the parser pool: those belong to transport and the
 * presentation layer, which reset per connection.
 */
void pc_server_reset(void);

// The header blocks every response carries, owned by server/response.cpp.

/** @brief True after a non-empty set_cors(). */
proto_bool pc_resp_cors_enabled(void);
/** @brief The pre-built CORS header block, or "" when CORS is off. */
const char *pc_resp_cors_header(void);
/** @brief The pre-built Cache-Control line, or "" when unset. */
const char *pc_resp_cache_control(void);
/** @brief This slot's queued custom headers / cookies (writable: the queue is appended in place). */
char *pc_resp_extra_hdr(uint8_t slot);

// pc_file_holds_slot: file_serving.h

#if PC_ENABLE_WEBSOCKET
/** @brief Perform the RFC 6455 101 handshake and hand the slot to the WS frame parser. */
proto_bool ws_do_upgrade(uint8_t slot_id, HttpReq *req, WsConnectHandler on_connect);

/** @brief Reject an unsupported Sec-WebSocket-Version with a 426 (RFC 6455 4.2.1) and close. */
void ws_send_version_required(uint8_t slot_id);
#endif

#if PC_ENABLE_SSE
/** @brief Send the SSE 200 headers and promote the slot to server-sent-events mode. */
proto_bool pc_sse_do_upgrade(uint8_t slot_id, HttpReq *req, SseConnectHandler on_connect);
#endif

PROTO_END_DECLS

#endif
