// Minimal ESP-IDF + arduino-esp32 sketch: a one-route web server on the ProtoCore
// library, built with the ESP-IDF CMake toolchain (idf.py) instead of Arduino/PlatformIO.
//
// Arduino autostart is enabled (CONFIG_AUTOSTART_ARDUINO=y in sdkconfig.defaults), so the arduino-esp32
// component calls setup() once and loop() forever - the same shape as an .ino sketch. Set your Wi-Fi
// credentials below and flash with `idf.py flash monitor`.
#include "network_drivers/physical/physical.h" // init_wifi_physical / wifi_ready
#include "protocore.h"
#include <Arduino.h>

static const char *WIFI_SSID = "YOUR_SSID";
static const char *WIFI_PASS = "YOUR_PASSWORD";

PC server;

static void handle_root(uint8_t slot, HttpReq *)
{
    server.send(slot, 200, "text/plain", "Hello from ESP-IDF + ProtoCore\n");
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Physical.wifi->init(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi connecting");
    uint32_t t0 = millis();
    while (!Physical.wifi->ready() && millis() - t0 < 20000)
    {
        delay(250);
        Serial.print('.');
    }
    if (!Physical.wifi->ready())
    {
        Serial.println(" no WiFi");
        return;
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/", HTTP_GET, handle_root);
    server.begin(80);
    Serial.println("server ready on :80");
}

void loop()
{
    server.handle();
}
