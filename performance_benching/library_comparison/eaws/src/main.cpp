// ESPAsyncWebServer (ESP32Async fork) benchmark server. Matched 1:1 with ../../pc so the ONLY
// difference is the library. Same board/core/-Os, WiFi sleep OFF. Endpoints: / /json /4k /64k.
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <string.h>

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_SSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_PASSWORD"
#endif

AsyncWebServer server(80);
static char big4k[4097];

void setup()
{
    Serial.begin(115200);
    delay(300);
    memset(big4k, 'A', 4096);
    big4k[4096] = '\0';
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000)
        delay(200);
    WiFi.setSleep(false);
    Serial.print("IP=");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "text/plain", "hello"); });
    server.on("/json", HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "application/json", "{\"a\":1}"); });
    server.on("/4k", HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "text/plain", big4k); });
    server.on("/64k", HTTP_GET, [](AsyncWebServerRequest *r) {
        AsyncWebServerResponse *resp =
            r->beginChunkedResponse("text/plain", [](uint8_t *buf, size_t maxLen, size_t index) -> size_t {
                size_t remaining = 65536u - index;
                if (remaining == 0)
                    return 0;
                size_t n = remaining < maxLen ? remaining : maxLen;
                memset(buf, 'A', n);
                return n;
            });
        r->send(resp);
    });
    server.begin();
}

void loop()
{
}
