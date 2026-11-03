// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Json.ino
 * @brief Zero-heap JSON: build responses with the Json writer, read requests with Json.get_*.
 *
 * The Json writer formats into a fixed stack buffer (no heap); Json.get_str/int/bool
 * read top-level members of a JSON request body in place. See json.h.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl http://<ip>/api/info
 *   curl -X POST http://<ip>/api/echo -H "Content-Type: application/json" \
 *        -d '{"name":"ada","age":36,"admin":true}'
 */

#define PROTOCORE_ENABLE_JSON 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"

static uint8_t json_work[16]; // the borrow an entry takes; Json never reads it

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// GET /api/info - build a JSON object with the Json writer into a fixed buffer (no heap).
void handle_info(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char buf[160];
    protocore_json_writer w;
    JsonV.init_args.w = &w;
    JsonV.init_args.buf = buf;
    JsonV.init_args.cap = sizeof(buf);
    Json.init(json_work);
    JsonV.begin_object_args.w = &w;
    Json.begin_object(json_work);
    JsonV.kv_str_args.w = &w;
    JsonV.kv_str_args.k = "lib";
    JsonV.kv_str_args.v = "ProtoCore";
    Json.kv_str(json_work);
    JsonV.kv_uint_args.w = &w;
    JsonV.kv_uint_args.k = "uptime_ms";
    JsonV.kv_uint_args.v = (unsigned long)millis();
    Json.kv_uint(json_work);
    JsonV.kv_uint_args.k = "free_heap";
    JsonV.kv_uint_args.v = (unsigned long)ESP.getFreeHeap();
    Json.kv_uint(json_work);
    JsonV.key_args.w = &w;
    JsonV.key_args.k = "features";
    Json.key(json_work);
    JsonV.begin_array_args.w = &w;
    Json.begin_array(json_work);
    static const char *const FEATURES[] = {"json", "chunked", "middleware"};
    JsonV.put_str_args.w = &w;
    for (const char *feature : FEATURES)
    {
        JsonV.put_str_args.v = feature;
        Json.put_str(json_work);
    }
    JsonV.end_array_args.w = &w;
    Json.end_array(json_work);
    JsonV.end_object_args.w = &w;
    Json.end_object(json_work);

    if (protocore_json_ok(&w))
    {
        send_text(slot_id, 200, "application/json", protocore_json_c_str(&w));
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
    proto_bool admin = false;
    JsonV.get_str_args.json = (const char *)req->body;
    JsonV.get_str_args.key = "name";
    JsonV.get_str_args.out = name;
    JsonV.get_str_args.out_cap = sizeof(name);
    Json.get_str(json_work);
    const bool have_name = JsonV.ok;
    JsonV.get_int_args.json = (const char *)req->body;
    JsonV.get_int_args.key = "age";
    JsonV.get_int_args.out = &age;
    Json.get_int(json_work);
    JsonV.get_bool_args.json = (const char *)req->body;
    JsonV.get_bool_args.key = "admin";
    JsonV.get_bool_args.out = &admin;
    Json.get_bool(json_work);

    char buf[128];
    protocore_json_writer w;
    JsonV.init_args.w = &w;
    JsonV.init_args.buf = buf;
    JsonV.init_args.cap = sizeof(buf);
    Json.init(json_work);
    JsonV.begin_object_args.w = &w;
    Json.begin_object(json_work);
    JsonV.kv_str_args.w = &w;
    JsonV.kv_str_args.k = "name";
    JsonV.kv_str_args.v = have_name ? name : "";
    Json.kv_str(json_work);
    JsonV.kv_int_args.w = &w;
    JsonV.kv_int_args.k = "age";
    JsonV.kv_int_args.v = age;
    Json.kv_int(json_work);
    JsonV.kv_bool_args.w = &w;
    JsonV.kv_bool_args.k = "admin";
    JsonV.kv_bool_args.v = admin;
    Json.kv_bool(json_work);
    JsonV.end_object_args.w = &w;
    Json.end_object(json_work);
    send_text(slot_id, 200, "application/json", protocore_json_c_str(&w));
}

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    while (!protocore_physical_wifi_up())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = protocore_physical_egress_ipv4(); // library egress IP (network byte order), no Arduino WiFi
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
