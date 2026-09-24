// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Expert.ino
 * @brief Expert example: connection-pool inspection, request profiling,
 *        rate-limiting, and memory-safe stack templating.
 *
 * Demonstrates:
 *   1. Low-level query of the Layer 4 connection pool (every slot of MAX_CONNS, through
 *      ConnPool) to monitor TCP states, control blocks, and ring-buffer fill levels.
 *   2. Token-bucket rate limiting: zero-allocation, time-based threshold checks
 *      returning "429 Too Many Requests".
 *   3. Execution profiling: measuring route handler times in microseconds.
 *   4. Dynamic stack-allocated response templating: JSON vs. plaintext error
 *      pages in the 404 handler based on the Accept header.
 *   5. Real-time stack watermarking (uxTaskGetStackHighWaterMark) to audit headroom.
 *
 * To run this example:
 *   - Configure SSID/PASSWORD, and upload to an ESP32.
 *   - Trigger high-frequency requests to see the rate limiter (429) in action.
 *   - Watch the connection-pool diagnostics printed on Serial every 5 s.
 */

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/tcp/tcp.h" // ConnPool: per-slot state, ring fill, control block

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static unsigned long total_routed_requests = 0;
static unsigned long total_rate_limited = 0;

// --- Token-bucket rate limiter state ---
static float bucket_tokens = 5.0f; // current tokens (starts full)
static const float bucket_capacity = 5.0f;
static const float refill_rate_per_sec = 2.0f;
static unsigned long last_refill_time_ms = 0;

/**
 * @brief Zero-heap token-bucket rate limiter.
 * Refills based on elapsed time and consumes one token per request.
 * @return true if allowed, false if rate-limited (bucket empty).
 */
bool acquire_rate_limit_token()
{
    unsigned long now = millis();
    unsigned long elapsed_ms = now - last_refill_time_ms;
    last_refill_time_ms = now;

    bucket_tokens += (elapsed_ms / 1000.0f) * refill_rate_per_sec;
    if (bucket_tokens > bucket_capacity)
    {
        bucket_tokens = bucket_capacity;
    }

    if (bucket_tokens >= 1.0f)
    {
        bucket_tokens -= 1.0f;
        return true;
    }
    return false;
}

// --- Connection-pool monitoring ---

/** @brief Log current TCP connection-pool statistics to Serial. */
void print_connection_pool_stats()
{
    // The pool's slot layout is the transport's own; a sketch reads a slot through ConnPool,
    // which takes the slot in ConnPoolV.slot and reports back into ConnPoolV.
    Serial.println("\n--- Connection Pool Snapshot ---");
    for (int i = 0; i < MAX_CONNS; i++)
    {
        ConnPoolV.slot = (uint8_t)i;
        ConnPool.active(protocore_conn_pool_span());
        bool active = ConnPoolV.ok != 0;
        ConnPool.pcb_of(protocore_conn_pool_span());
        protocore_pcb *pcb = ConnPoolV.pcb;

        // ACTIVE holds a live control block; a slot with a control block that is no longer
        // active is draining its close; no control block at all is a free slot.
        const char *state_str = active ? "ACTIVE" : (pcb != NULL ? "CLOSING" : "FREE");

        size_t rx_unread = 0;
        if (active)
        {
            ConnPool.available(protocore_conn_pool_span());
            rx_unread = ConnPoolV.n;
        }

        Serial.printf("Slot [%d]: State=%-7s | UnreadBytes=%4zu | PCB=%p\n", i, state_str, rx_unread, (void *)pcb);
    }
    Serial.println("---------------------------------");
}

// --- HttpRoute handlers with microsecond profiling ---

/**
 * @brief GET /api/diagnostics
 * Returns detailed telemetry; the profiler measures how fast the handler runs.
 */
void handle_diagnostics(uint8_t slot_id, HttpReq *req)
{
    unsigned long start_us = micros();
    total_routed_requests++;

    if (!acquire_rate_limit_token())
    {
        total_rate_limited++;
        send_text(slot_id, 429, "application/json", "{\"error\":\"Too Many Requests. Rate limit exceeded.\"}");
        return;
    }

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
    send_text(slot_id, 200, "application/json", response_buf);
    Serial.printf("[Profile] GET %s handled in %lu us\n", req->path, duration_us);
}

/**
 * @brief GET /api/compute
 * A heavier compute-bound route with timing checks.
 */
void handle_compute(uint8_t slot_id, HttpReq *req)
{
    unsigned long start_us = micros();
    total_routed_requests++;

    if (!acquire_rate_limit_token())
    {
        total_rate_limited++;
        send_text(slot_id, 429, "application/json", "{\"error\":\"Too Many Requests\"}");
        return;
    }

    volatile uint32_t val = 12345;
    for (int i = 0; i < 500; i++)
    {
        val = (val ^ 37821) * 31;
    }

    char response_buf[64];
    snprintf(response_buf, sizeof(response_buf), "{\"result\":%u}", val);
    send_text(slot_id, 200, "application/json", response_buf);

    unsigned long duration_us = micros() - start_us;
    Serial.printf("[Profile] GET %s (heavy compute) handled in %lu us\n", req->path, duration_us);
}

/**
 * @brief Dynamic-template 404 handler.
 * Chooses JSON or plaintext based on the Accept header.
 */
void handle_expert_not_found(uint8_t slot_id, HttpReq *req)
{
    unsigned long start_us = micros();
    total_routed_requests++;

    const char *accept_header = HttpParser.get_header(protocore_http_parser_span(), req, "Accept");
    bool wants_json = (accept_header && strstr(accept_header, "application/json") != nullptr);

    char error_buf[256];
    if (wants_json)
    {
        snprintf(error_buf, sizeof(error_buf), "{\"error\":\"not_found\",\"requested_path\":\"%s\",\"uptime\":%lu}",
                 req->path, millis());
        send_text(slot_id, 404, "application/json", error_buf);
    }
    else
    {
        snprintf(error_buf, sizeof(error_buf), "--- Error 404 ---\nPath: %s\nUptime: %lu ms\nESP32 node", req->path,
                 millis());
        send_text(slot_id, 404, "text/plain", error_buf);
    }

    unsigned long duration_us = micros() - start_us;
    Serial.printf("[Profile] Fallback 404 matched in %lu us (JSON: %d)\n", duration_us, wants_json);
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- PC Expert Performance Example ---");

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi online!");
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32;
    Serial.printf("Local IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    last_refill_time_ms = millis();

    on_http("/api/diagnostics", HTTP_GET, handle_diagnostics);
    on_http("/api/compute", HTTP_GET, handle_compute);
    on_not_found(handle_expert_not_found);

    int32_t result = begin_http(80, NULL);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Telemetry server started on port 80");
}

void loop()
{
    handle();

    // Display active pool diagnostics every 5 seconds.
    static unsigned long last_snapshot = 0;
    if (millis() - last_snapshot >= 5000)
    {
        last_snapshot = millis();
        print_connection_pool_stats();
    }
}
