// Minimal ESP-IDF + arduino-esp32 sketch: a one-route web server on the ProtoCore
// library, built with the ESP-IDF CMake toolchain (idf.py) instead of Arduino/PlatformIO.
//
// Arduino autostart is enabled (CONFIG_AUTOSTART_ARDUINO=y in sdkconfig.defaults), so the arduino-esp32
// component calls setup() once and loop() forever - the same shape as an .ino sketch. Set your Wi-Fi
// credentials below and flash with `idf.py flash monitor`.
#include "network_drivers/physical/physical/physical.h" // Physical / PhysicalV
#include "protocore.h"
#include <Arduino.h>

static const char *WIFI_SSID = "YOUR_SSID";
static const char *WIFI_PASS = "YOUR_PASSWORD";

static void handle_root(uint8_t slot, HttpReq *)
{
    send_text(slot, 200, "text/plain", "Hello from ESP-IDF + ProtoCore\n");
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    // Every Physical entry reads its operands from PhysicalV and writes its outcome back there.
    PhysicalV.wifi.ssid = WIFI_SSID;
    PhysicalV.wifi.password = WIFI_PASS;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("WiFi connecting");
    uint32_t t0 = millis();
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok && millis() - t0 < 20000;
         Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    if (!PhysicalV.ok)
    {
        Serial.println(" no WiFi");
        return;
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/", HTTP_GET, handle_root);
    begin_http(80, NULL);
    Serial.println("server ready on :80");
}

void loop()
{
    handle();
}
