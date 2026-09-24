// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file UdpTelemetry.ino
 * @brief Fire-and-forget UDP telemetry cast (PROTOCORE_ENABLE_UDP_TELEMETRY).
 *
 * Builds an InfluxDB line-protocol record (`esp32 heap=...i,rssi=...i,temp=...`)
 * and casts it to a collector over UDP once a second - zero-heap, no ACK, no
 * retry. Point it at Telegraf/InfluxDB's UDP listener, or just watch the packets:
 *     nc -u -l 8094        # then run this sketch casting to your host:8094
 *
 * NOTE: enable it for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_UDP_TELEMETRY=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_UDP_TELEMETRY 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/iot/udp_telemetry/udp_telemetry.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// Where the telemetry datagrams go (your collector's UDP host + port).
static const char *COLLECTOR_IP = "192.168.1.10";
static const uint16_t COLLECTOR_PORT = 8094;

void setup()
{
    Serial.begin(115200);
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

    UdpTelemetryV.collector.addr = COLLECTOR_IP;
    UdpTelemetryV.collector.port = COLLECTOR_PORT;
    UdpTelemetry.begin(protocore_udp_telemetry_span());
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        char buf[PROTOCORE_UDP_TELEMETRY_BUF];
        uint8_t *span = protocore_udp_telemetry_span();
        UdpTelemetryV.line.buf = buf;
        UdpTelemetryV.line.cap = sizeof(buf);
        UdpTelemetryV.line.measurement = "esp32";
        UdpTelemetry.measurement(span);
        UdpTelemetryV.fields.key = "heap";
        UdpTelemetryV.fields.u64 = ESP.getFreeHeap();
        UdpTelemetry.field_uint(span);
        Physical.wifi_rssi(protocore_physical_span());
        UdpTelemetryV.fields.key = "rssi";
        UdpTelemetryV.fields.i64 = PhysicalV.i8;
        UdpTelemetry.field_int(span);
        UdpTelemetryV.fields.key = "temp";
        UdpTelemetryV.fields.f32 = temperatureRead();
        UdpTelemetryV.fields.decimals = 1;
        UdpTelemetry.field_float(span);
        UdpTelemetry.write(span); // send the built line as one datagram
        if (UdpTelemetryV.ok)
        {
            Serial.printf("cast: %s\n", buf);
        }
    }
}
