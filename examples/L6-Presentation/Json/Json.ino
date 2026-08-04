// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Json.ino
 * @brief Zero-heap JSON: build responses with JsonWriter, read requests with json_get_*.
 *
 * JsonWriter formats into a fixed stack buffer (no heap); json_get_str/int/bool
 * read top-level members of a JSON request body in place. See json.h.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl http://<ip>/api/info
 *   curl -X POST http://<ip>/api/echo -H "Content-Type: application/json" \
 *        -d '{"name":"ada","age":36,"admin":true}'
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// GET /api/info - build a JSON object with JsonWriter (no heap).
void handle_info(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char buf[160];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object();
    w.kv_str("lib", "ProtoCore");
    w.kv_uint("uptime_ms", (unsigned long)millis());
    w.kv_uint("free_heap", (unsigned long)ESP.getFreeHeap());
    w.key("features");
    w.begin_array();
    w.str("json");
    w.str("chunked");
    w.str("middleware");
    w.end_array();
    w.end_object();

    if (w.ok())
    {
        send_text(slot_id, 200, "application/json", w.c_str());
    }
    else
    {
        send_text(slot_id, 500, "text/plain", "json buffer overflow");
    }
}

// POST /api/echo - read top-level fields from a JSON body and reflect them.
void handle_echo(uint8_t slot_id, HttpReq *req)
{
    char name[32];
    long age = 0;
    bool admin = false;
    bool have_name = json_get_str((const char *)req->body, "name", name, sizeof(name));
    json_get_int((const char *)req->body, "age", &age);
    json_get_bool((const char *)req->body, "admin", &admin);

    char buf[128];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object();
    w.kv_str("name", have_name ? name : "");
    w.kv_int("age", age);
    w.kv_bool("admin", admin);
    w.end_object();
    send_text(slot_id, 200, "application/json", w.c_str());
}

void setup()
{
    Serial.begin(115200);

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

    on_http("/api/info", HTTP_GET, handle_info);
    on_http("/api/echo", HTTP_POST, handle_echo);

    int32_t result = begin_http(80, NULL);
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
