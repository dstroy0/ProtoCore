# Expert - introspection, rate limiting, and profiling

**Layer:** Foundation (tutorial) · **Build flags:** none (core features only)

## What this example teaches

This is the "look under the hood" example. It reaches into the Layer-4 connection
pool to report live TCP state, adds a zero-heap token-bucket rate limiter,
profiles handler execution in microseconds, audits stack headroom, and does
content negotiation in the 404 handler. Everything here is allocation-free.

**Inspecting the L4 connection pool.** The transport layer keeps every
connection in a fixed BSS array `conn_pool[MAX_CONNS]`. Including
`tcp.h` lets the sketch read it directly to monitor TCP state, idle time,
and how many unread bytes sit in each slot's RX ring:

```cpp
#include "network_drivers/transport/tcp/tcp_conn.h" // conn_pool, TcpConn, ConnState
...
TcpConn *conn = &conn_pool[i];
switch (conn->state) { case ConnState::CONN_FREE: ...; case ConnState::CONN_ACTIVE: ...; case ConnState::CONN_CLOSING: ...; }
// unread bytes = ring fill (head minus tail, wrapping at RX_BUF_SIZE)
size_t rx_unread = (conn->rx_head >= conn->rx_tail) ? (conn->rx_head - conn->rx_tail)
                                                    : (RX_BUF_SIZE - (conn->rx_tail - conn->rx_head));
```

`loop()` prints this snapshot every 5 seconds, which is a great way to watch
keep-alive slots, idle timeouts, and backpressure in real time.

**Token-bucket rate limiting (429).** A classic rate limiter with no allocation
and no timers - just a float "bucket" refilled by elapsed time and drained one
token per request. When empty, the handler returns `429 Too Many Requests`:

```cpp
bucket_tokens += (elapsed_ms / 1000.0f) * refill_rate_per_sec; // refill by time
if (bucket_tokens > bucket_capacity) bucket_tokens = bucket_capacity;
if (bucket_tokens >= 1.0f) { bucket_tokens -= 1.0f; return true; } // allowed
return false;                                                      // rate-limited
```

> The library also ships a built-in rate limiter in the middleware pipeline; see
> [Middleware](../../L7-Application/Middleware). This hand-rolled one shows
> the mechanism.

**Microsecond profiling + stack watermark.** Each handler brackets its work with
`micros()` and logs the duration, and the diagnostics route reports the FreeRTOS
task's stack high-water mark so you can audit headroom:

```cpp
unsigned long start_us = micros();
...
UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL); // words of stack never used
...
Serial.printf("[Profile] GET %s handled in %lu us\n", req->path, micros() - start_us);
```

**Content negotiation in the 404.** The fallback handler reads the `Accept`
header and returns JSON or plaintext accordingly - the same idea as a real API
serving different representations:

```cpp
const char *accept_header = http_get_header(req, "Accept");
bool wants_json = (accept_header && strstr(accept_header, "application/json") != nullptr);
```

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/Foundation/Expert/Expert.ino
```

Flash, open the serial monitor (115200) to watch the pool snapshots, then:

```sh
curl http://<ip>/api/diagnostics            # telemetry; hammer it to trigger 429
curl http://<ip>/api/compute
curl -H "Accept: application/json" http://<ip>/nope   # JSON 404
curl http://<ip>/nope                                  # plaintext 404
```

## Annotated source

The complete sketch ([Expert.ino](Expert.ino)), reproduced verbatim with
added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/tcp/tcp_conn.h" // access conn_pool and ConnState

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

static unsigned long total_routed_requests = 0;
static unsigned long total_rate_limited = 0;

// --- Token-bucket rate limiter state (all floats/longs in BSS, no heap) ---
static float bucket_tokens = 5.0f; // current tokens (starts full)
static const float bucket_capacity = 5.0f;
static const float refill_rate_per_sec = 2.0f;
static unsigned long last_refill_time_ms = 0;

/**
 * Refill the bucket by elapsed time, then try to spend one token.
 * @return true if allowed, false if the bucket is empty (rate-limited).
 */
bool acquire_rate_limit_token()
{
    unsigned long now = millis();
    unsigned long elapsed_ms = now - last_refill_time_ms;
    last_refill_time_ms = now;

    bucket_tokens += (elapsed_ms / 1000.0f) * refill_rate_per_sec; // time-based refill
    if (bucket_tokens > bucket_capacity)
        bucket_tokens = bucket_capacity;                           // clamp to capacity

    if (bucket_tokens >= 1.0f)
    {
        bucket_tokens -= 1.0f;                                     // spend one
        return true;
    }
    return false;
}

// --- Connection-pool monitoring (reads the L4 transport state directly) ---

/** Log a snapshot of every connection slot to Serial. */
void print_connection_pool_stats()
{
    Serial.println("\n--- Connection Pool Snapshot ---");
    for (int i = 0; i < MAX_CONNS; i++)
    {
        TcpConn *conn = &conn_pool[i];
        const char *state_str = "UNKNOWN";
        switch (conn->state)
        {
        case ConnState::CONN_FREE:    state_str = "FREE";    break;
        case ConnState::CONN_ACTIVE:  state_str = "ACTIVE";  break;
        case ConnState::CONN_CLOSING: state_str = "CLOSING"; break;
        }

        // Unread bytes = the SPSC RX ring fill level (head produced by tcpip,
        // tail consumed by the worker), wrapping at RX_BUF_SIZE.
        size_t rx_unread = 0;
        if (conn->state == ConnState::CONN_ACTIVE)
            rx_unread = (conn->rx_head >= conn->rx_tail) ? (conn->rx_head - conn->rx_tail)
                                                         : (RX_BUF_SIZE - (conn->rx_tail - conn->rx_head));

        Serial.printf("Slot [%d]: State=%-7s | UnreadBytes=%4zu | LastActivity=%6lu ms ago | PCB=%p\n", i, state_str,
                      rx_unread, (conn->state == ConnState::CONN_ACTIVE) ? (millis() - conn->last_activity_ms) : 0, conn->pcb);
    }
    Serial.println("---------------------------------");
}

// --- HttpRoute handlers with microsecond profiling ---

/** GET /api/diagnostics - telemetry; rate-limited; reports stack headroom. */
void handle_diagnostics(uint8_t slot_id, HttpReq *req)
{
    unsigned long start_us = micros();      // profile start
    total_routed_requests++;

    if (!acquire_rate_limit_token())
    {
        total_rate_limited++;
        server.send(slot_id, 429, "application/json", "{\"error\":\"Too Many Requests. Rate limit exceeded.\"}");
        return;
    }

    // Words of this task's stack that have NEVER been used = remaining headroom.
    UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL);

    char response_buf[384];
    snprintf(response_buf, sizeof(response_buf),
             "{"
             "\"uptime_ms\":%lu,"
             "\"routed_requests\":%lu,"
             "\"rate_limited_count\":%lu,"
             "\"free_heap_bytes\":%u,"
             "\"task_stack_headroom_words\":%u,"
             "\"bucket_tokens_left\":%.2f"
             "}",
             millis(), total_routed_requests, total_rate_limited, ESP.getFreeHeap(), (unsigned int)stack_high_water,
             bucket_tokens);

    unsigned long duration_us = micros() - start_us;
    server.send(slot_id, 200, "application/json", response_buf);
    Serial.printf("[Profile] GET %s handled in %lu us\n", req->path, duration_us);
}

/** GET /api/compute - a heavier compute-bound route, also profiled. */
void handle_compute(uint8_t slot_id, HttpReq *req)
{
    unsigned long start_us = micros();
    total_routed_requests++;

    if (!acquire_rate_limit_token())
    {
        total_rate_limited++;
        server.send(slot_id, 429, "application/json", "{\"error\":\"Too Many Requests\"}");
        return;
    }

    volatile uint32_t val = 12345;          // volatile so the loop is not optimized away
    for (int i = 0; i < 500; i++)
        val = (val ^ 37821) * 31;

    char response_buf[64];
    snprintf(response_buf, sizeof(response_buf), "{\"result\":%u}", val);
    server.send(slot_id, 200, "application/json", response_buf);

    unsigned long duration_us = micros() - start_us;
    Serial.printf("[Profile] GET %s (heavy compute) handled in %lu us\n", req->path, duration_us);
}

/** 404 fallback - returns JSON or plaintext depending on the Accept header. */
void handle_expert_not_found(uint8_t slot_id, HttpReq *req)
{
    unsigned long start_us = micros();
    total_routed_requests++;

    const char *accept_header = http_get_header(req, "Accept");
    bool wants_json = (accept_header && strstr(accept_header, "application/json") != nullptr);

    char error_buf[256];
    if (wants_json)
    {
        snprintf(error_buf, sizeof(error_buf), "{\"error\":\"not_found\",\"requested_path\":\"%s\",\"uptime\":%lu}",
                 req->path, millis());
        server.send(slot_id, 404, "application/json", error_buf);
    }
    else
    {
        snprintf(error_buf, sizeof(error_buf), "--- Error 404 ---\nPath: %s\nUptime: %lu ms\nESP32 node", req->path,
                 millis());
        server.send(slot_id, 404, "text/plain", error_buf);
    }

    unsigned long duration_us = micros() - start_us;
    Serial.printf("[Profile] Fallback 404 matched in %lu us (JSON: %d)\n", duration_us, wants_json);
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- PC Expert Performance Example ---");

    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi online!");
    Serial.print("Local IP: ");
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    last_refill_time_ms = millis();         // start the rate-limiter clock

    server.on("/api/diagnostics", HttpMethod::HTTP_GET, handle_diagnostics);
    server.on("/api/compute", HttpMethod::HTTP_GET, handle_compute);
    server.on_not_found(handle_expert_not_found);

    int32_t result = server.begin(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Telemetry server started on port 80");
}

void loop()
{
    server.handle();

    // Print the live pool diagnostics every 5 seconds.
    static unsigned long last_snapshot = 0;
    if (millis() - last_snapshot >= 5000)
    {
        last_snapshot = millis();
        print_connection_pool_stats();
    }
}
```
