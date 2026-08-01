// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Configuration.ino
 * @brief Reference for every user-configurable flag and constant, plus runnable
 *        routes that echo the active configuration so you can confirm your
 *        overrides took effect.
 *
 * All #defines must appear BEFORE the library include (or be supplied as
 * -D build flags in platformio.ini).  Any that are omitted get their documented
 * defaults.  Illegal combinations (e.g. pool sizes that exceed MAX_CONNS)
 * produce a #error in the compiler output rather than a silent misbehavior.
 *
 * ============================================================
 * FEATURE FLAGS  (set to 0 to strip from the build entirely)
 * ============================================================
 *   PC_ENABLE_WEBSOCKET    default 1  RFC 6455 framing + SHA-1/base64 handshake
 *   PC_ENABLE_SSE          default 1  Server-Sent Events push
 *   PC_ENABLE_MULTIPART    default 1  multipart/form-data body parser
 *   PC_ENABLE_FILE_SERVING default 1  static files via Arduino FS
 *   PC_ENABLE_AUTH         default 1  HTTP Basic Authentication per-route
 *   PC_ENABLE_DIAG         default 0  /diag JSON of the compile-time config
 *
 * ============================================================
 * FEATURE DEPENDENCIES  (a child needs its parent; illegal combos #error)
 * ============================================================
 *   FILE_SERVING -> WEBDAV, RANGE
 *   TLS          -> MTLS, TLS_RESUMPTION, HTTP_CLIENT_TLS (+HTTP_CLIENT),
 *                   MQTT_TLS (+MQTT), WS_CLIENT_TLS (+WS_CLIENT)
 *   WEBSOCKET    -> WS_DEFLATE, WEB_TERMINAL
 *   SSE          -> DASHBOARD
 *   STATS        -> METRICS
 *   AUTH         -> AUTH_LOCKOUT
 *   SNMP         -> SnmpVersion::SNMP_V3, SNMP_TRAP
 *   COAP         -> COAP_OBSERVE, COAP_BLOCK
 *   OPCUA        -> OPCUA_CLIENT
 *   CONFIG_STORE -> CONFIG_IO
 *   Optional: WEBHOOK/OAUTH2 use HTTP_CLIENT to send; DASHBOARD uses WEBSOCKET
 *   for live controls. Full tree: README.md and src/protocore_config.h.
 *
 * ============================================================
 * CAPACITY CONSTANTS  (affect static array sizes → RAM / flash)
 * ============================================================
 *   MAX_CONNS         default 4    simultaneous TCP connections (1–255)
 *   RX_BUF_SIZE       default 1024 ring-buffer bytes per slot (>= BODY_BUF_SIZE)
 *   CONN_TIMEOUT_MS   default 5000 idle timeout (compile-time default)
 *   MAX_HEADERS       default 8    headers stored per request
 *   MAX_PATH_LEN      default 64   URL path bytes incl. leading '/' (>= 2)
 *   MAX_KEY_LEN       default 24   header field-name bytes (>= 4)
 *   MAX_VAL_LEN       default 48   header field-value bytes
 *   MAX_QUERY_LEN     default 128  raw query-string bytes
 *   MAX_QUERY_PARAMS  default 8    parsed key=value pairs
 *   QUERY_KEY_LEN     default 24   query-parameter key bytes
 *   QUERY_VAL_LEN     default 48   query-parameter value bytes
 *   BODY_BUF_SIZE     default 256  request body bytes (<= RX_BUF_SIZE)
 *   MAX_ROUTES        default 16   registered routes (>= 1)
 *
 * WebSocket:  MAX_WS_CONNS (2), WS_FRAME_SIZE (512)
 * SSE:        MAX_SSE_CONNS (2), SSE_BUF_SIZE (256)
 * Files:      FILE_CHUNK_SIZE (512, <= RX_BUF_SIZE)
 * Auth:       MAX_AUTH_LEN (32, >= 2)
 * Multipart:  MAX_MULTIPART_PARTS (4), MAX_BOUNDARY_LEN (72)
 * Constraint: MAX_WS_CONNS + MAX_SSE_CONNS <= MAX_CONNS
 *
 * ============================================================
 * RUNTIME CONFIGURATION  (passed to begin())
 * ============================================================
 *   WebServerConfig::conn_timeout_ms: ms of inactivity before force-close.
 *     Flash (no RAM cost):  const WebServerConfig cfg PROGMEM = {10000};
 *     RAM (runtime-tunable): WebServerConfig cfg = {10000};
 *     Pass nullptr (or omit) to use the built-in default of 5000 ms.
 *
 * Example curl commands:
 *   curl http://<ip>/config
 *   curl -X POST http://<ip>/echo -d "hello"
 *   curl "http://<ip>/search?q=esp32&sort=date"
 */

// -------------------------------------------------------------------
// A low-footprint REST-only profile: no WS, SSE, multipart, or file IO.
// These values roughly halve RAM use versus the defaults; good for sensor
// nodes sharing the heap with other subsystems.
// -------------------------------------------------------------------

#define PC_ENABLE_WEBSOCKET 0
#define PC_ENABLE_SSE 0
#define PC_ENABLE_MULTIPART 0
#define PC_ENABLE_FILE_SERVING 0
#define PC_ENABLE_AUTH 0

// Diagnostic endpoint: exposes the compile-time config; disable in production.
#define PC_ENABLE_DIAG 1

// Tightened capacity to match a small REST API.
#define MAX_CONNS 2
#define EVT_QUEUE_DEPTH 8 // must be >= MAX_CONNS * 4
#define RX_BUF_SIZE 512
#define BODY_BUF_SIZE 128
#define MAX_ROUTES 8
#define MAX_HEADERS 6
#define MAX_PATH_LEN 48
#define MAX_KEY_LEN 16
#define MAX_VAL_LEN 32
#define MAX_QUERY_LEN 64
#define MAX_QUERY_PARAMS 4
#define QUERY_KEY_LEN 16
#define QUERY_VAL_LEN 24
#define CONN_TIMEOUT_MS 3000

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// GET /config
// Returns every active sizing constant as JSON so you can verify your build
// flags / #defines took effect without a debugger.
void handle_config(uint8_t slot_id, HttpReq *req)
{
    char body[384];
    snprintf(body, sizeof(body),
             "{"
             "\"MAX_CONNS\":%u,"
             "\"RX_BUF_SIZE\":%u,"
             "\"CONN_TIMEOUT_MS\":%u,"
             "\"MAX_HEADERS\":%u,"
             "\"MAX_PATH_LEN\":%u,"
             "\"MAX_KEY_LEN\":%u,"
             "\"MAX_VAL_LEN\":%u,"
             "\"MAX_QUERY_LEN\":%u,"
             "\"MAX_QUERY_PARAMS\":%u,"
             "\"QUERY_KEY_LEN\":%u,"
             "\"QUERY_VAL_LEN\":%u,"
             "\"BODY_BUF_SIZE\":%u,"
             "\"MAX_ROUTES\":%u"
             "}",
             (unsigned)MAX_CONNS, (unsigned)RX_BUF_SIZE, (unsigned)CONN_TIMEOUT_MS, (unsigned)MAX_HEADERS,
             (unsigned)MAX_PATH_LEN, (unsigned)MAX_KEY_LEN, (unsigned)MAX_VAL_LEN, (unsigned)MAX_QUERY_LEN,
             (unsigned)MAX_QUERY_PARAMS, (unsigned)QUERY_KEY_LEN, (unsigned)QUERY_VAL_LEN, (unsigned)BODY_BUF_SIZE,
             (unsigned)MAX_ROUTES);

    send_text(slot_id, 200, "application/json", body);
}

// POST /echo
// Echoes the request body back.  With BODY_BUF_SIZE=128, bodies longer than
// 128 bytes trigger an automatic 413 before this handler is called.
void handle_echo(uint8_t slot_id, HttpReq *req)
{
    send_text(slot_id, 200, "text/plain", (const char *)req->body);
}

// GET /search
// Echoes the parsed query parameters.  With MAX_QUERY_PARAMS=4, a fifth
// parameter is parsed but silently dropped: this shows exactly what was kept.
void handle_search(uint8_t slot_id, HttpReq *req)
{
    char body[256] = "params: ";
    for (uint8_t i = 0; i < req->query_count; i++)
    {
        if (i > 0)
        {
            strncat(body, ", ", sizeof(body) - strlen(body) - 1);
        }
        strncat(body, req->query_params[i].key, sizeof(body) - strlen(body) - 1);
        strncat(body, "=", sizeof(body) - strlen(body) - 1);
        strncat(body, req->query_params[i].val, sizeof(body) - strlen(body) - 1);
    }
    send_text(slot_id, 200, "text/plain", body);
}

void setup()
{
    Serial.begin(115200);

    // Print the active config so you can confirm overrides without curl.
    Serial.println("\n--- Active PC config ---");
    Serial.printf("  MAX_CONNS        = %u\n", (unsigned)MAX_CONNS);
    Serial.printf("  RX_BUF_SIZE      = %u\n", (unsigned)RX_BUF_SIZE);
    Serial.printf("  CONN_TIMEOUT_MS  = %u\n", (unsigned)CONN_TIMEOUT_MS);
    Serial.printf("  BODY_BUF_SIZE    = %u\n", (unsigned)BODY_BUF_SIZE);
    Serial.printf("  MAX_QUERY_PARAMS = %u\n", (unsigned)MAX_QUERY_PARAMS);
    Serial.println("----------------------------------");

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/config", HttpMethod::HTTP_GET, handle_config);
    on_http("/echo", HttpMethod::HTTP_POST, handle_echo);
    on_http("/search", HttpMethod::HTTP_GET, handle_search);

    // Diagnostic route (PC_ENABLE_DIAG=1): remove or protect in production.
    on_http("/diag", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { diag(id); });

    // Pass a runtime config to override the idle timeout without a rebuild.
    WebServerConfig cfg = {.conn_timeout_ms = CONN_TIMEOUT_MS};
    int32_t result = begin_http(80, &cfg);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on port 80");
}

void loop()
{
    handle();
}
