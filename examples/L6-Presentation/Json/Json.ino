// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Json.ino
 * @brief Zero-heap JSON: build responses with the Json writer, read requests with Json.get_*.
 *
 * The Json writer (protocore_json_writer) formats into a fixed stack buffer (no heap);
 * Json.get_str/int/bool read top-level members of a JSON request body in place. See json.h.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl http://<ip>/api/info
 *   curl -X POST http://<ip>/api/echo -H "Content-Type: application/json" \
 *        -d '{"name":"ada","age":36,"admin":true}'
 */


#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// The borrow every Json entry takes. The codec carries nothing between calls and never reads
// it, so a small static buffer serves; operands go in JsonV and the outcome comes back there.
static uint8_t json_work[16];

// GET /api/info - build a JSON object with the Json writer (no heap).
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
    JsonV.kv_str_args = (JsonKvStrArgs){&w, "lib", "ProtoCore"};
    Json.kv_str(json_work);
    JsonV.kv_uint_args = (JsonKvUintArgs){&w, "uptime_ms", (unsigned long)millis()};
    Json.kv_uint(json_work);
    JsonV.kv_uint_args = (JsonKvUintArgs){&w, "free_heap", (unsigned long)ESP.getFreeHeap()};
    Json.kv_uint(json_work);
    JsonV.key_args = (JsonKeyArgs){&w, "features"};
    Json.key(json_work);
    JsonV.begin_array_args.w = &w;
    Json.begin_array(json_work);
    JsonV.put_str_args = (JsonPutStrArgs){&w, "json"};
    Json.put_str(json_work);
    JsonV.put_str_args = (JsonPutStrArgs){&w, "chunked"};
    Json.put_str(json_work);
    JsonV.put_str_args = (JsonPutStrArgs){&w, "middleware"};
    Json.put_str(json_work);
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
    proto_bool admin = PROTO_FALSE;
    JsonV.get_str_args = (JsonGetStrArgs){(const char *)req->body, "name", name, sizeof(name)};
    Json.get_str(json_work);
    bool have_name = JsonV.ok;
    JsonV.get_int_args = (JsonGetIntArgs){(const char *)req->body, "age", &age};
    Json.get_int(json_work);
    JsonV.get_bool_args = (JsonGetBoolArgs){(const char *)req->body, "admin", &admin};
    Json.get_bool(json_work);

    char buf[128];
    protocore_json_writer w;
    JsonV.init_args.w = &w;
    JsonV.init_args.buf = buf;
    JsonV.init_args.cap = sizeof(buf);
    Json.init(json_work);
    JsonV.begin_object_args.w = &w;
    Json.begin_object(json_work);
    JsonV.kv_str_args = (JsonKvStrArgs){&w, "name", have_name ? name : ""};
    Json.kv_str(json_work);
    JsonV.kv_int_args = (JsonKvIntArgs){&w, "age", age};
    Json.kv_int(json_work);
    JsonV.kv_bool_args = (JsonKvBoolArgs){&w, "admin", admin};
    Json.kv_bool(json_work);
    JsonV.end_object_args.w = &w;
    Json.end_object(json_work);
    send_text(slot_id, 200, "application/json", protocore_json_c_str(&w));
}

void setup()
{
    Serial.begin(115200);

    // Every Physical entry reads its operands from PhysicalV and writes its outcome back there.
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
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
