// ProtoCore benchmark server. Minimal, matched 1:1 with the ESPAsyncWebServer
// sketch in ../../eaws so the ONLY difference is the library. Same board/core/-Os, WiFi sleep OFF.
// Endpoints: / (tiny), /json (tiny), /4k (single-window body), /64k (chunked stream).
#include "network_drivers/physical/physical.h"
#include "protocore.h"
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_SSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_PASSWORD"
#endif

PC server;
static uint8_t big4k[4096];
static uint32_t g_sent64;

static void h_root(uint8_t id, HttpReq *)
{
    server.send(id, 200, "text/plain", "hello");
}
static void h_json(uint8_t id, HttpReq *)
{
    server.send(id, 200, "application/json", "{\"a\":1}");
}
static void h_4k(uint8_t id, HttpReq *)
{
    server.send(id, 200, "text/plain", big4k, sizeof(big4k));
}
static size_t chunk64k(uint8_t *buf, size_t cap, void *)
{
    uint32_t remaining = 65536u - g_sent64;
    if (remaining == 0)
    {
        return 0;
    }
    size_t n = remaining < cap ? remaining : cap;
    memset(buf, 'A', n);
    g_sent64 += (uint32_t)n;
    return n;
}
static void h_64k(uint8_t id, HttpReq *)
{
    g_sent64 = 0;
    server.send_chunked(id, 200, "text/plain", chunk64k, nullptr);
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    memset(big4k, 'A', sizeof(big4k));
    init_wifi_physical(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (!wifi_ready() && millis() - t0 < 30000)
    {
        delay(200);
    }
    WiFi.setSleep(false); // WiFi modem-sleep adds latency - off on both benchmark sketches
    Serial.print("IP=");
    Serial.println(WiFi.localIP());

    server.on("/", HttpMethod::HTTP_GET, h_root);
    server.on("/json", HttpMethod::HTTP_GET, h_json);
    server.on("/4k", HttpMethod::HTTP_GET, h_4k);
    server.on("/64k", HttpMethod::HTTP_GET, h_64k);
    server.begin(80); // keep-alive + MAX_CONNS=8 are the library defaults
}

void loop()
{
    server.handle();
}
