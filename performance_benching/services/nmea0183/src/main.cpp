// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the NMEA 0183 sentence codec (services/timing_position/nmea0183): the marine
// / GPS ASCII protocol (`$GPGGA,123519,4807.038,N,...*47<CR><LF>`). Benched are the four pure hot
// paths - the XOR checksum over the sentence body, pc_nmea0183_build() (adds `$`, `*HH`, CRLF),
// pc_nmea0183_parse() (leading-`$` guard, `*HH` XOR validation, comma field split, talker/type
// derivation), and the pc_strtof / pc_strtol field-value helpers. Like performance_benching/device/modbus this is
// a pure codec with no hardware involved: on a real rig the receiver is a plain HardwareSerial UART,
// which is the application's - no UART / socket / radio I/O is ever performed here, so every call
// exercises the real production code path over the canonical GGA known-answer vector (checksum 0x47)
// copied verbatim from test/test_nmea0183/test_nmea0183.cpp.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/nmea0183 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/timing_position/nmea0183/nmea0183.h"
#include <Arduino.h>
#include <string.h>

// The standard GGA example; its documented checksum is 0x47 (test/test_nmea0183/test_nmea0183.cpp).
static const char GGA[] = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
static const char GGA_BODY[] = "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";

static void nmea0183_bench_task(void *)
{
    const size_t body_len = strlen(GGA_BODY);
    const size_t gga_len = strlen(GGA);
    static char build_buf[96];

    // Parse once up front so the field-value helpers have a real, spec-conformant message to walk.
    Nmea0183 m;
    pc_nmea0183_parse(GGA, gga_len, &m);

    for (;;)
    {
        Serial.printf("DB ==== nmea0183 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint8_t sink8 = 0;
        volatile size_t sinksz = 0;
        volatile bool sinkb = false;
        volatile float sinkf = 0.0f;
        volatile long sinkl = 0;

        // XOR checksum over the 60-byte sentence body: bulk op, so ns/byte and MB/s are reported.
        DBENCH_BULK("pc_nmea0183_checksum", 200000, body_len, sink8 += pc_nmea0183_checksum(GGA_BODY, body_len));
        // Build `$<body>*HH\r\n` (checksum + framing) from the GGA body.
        DBENCH_OP("pc_nmea0183_build", 100000, sinksz += pc_nmea0183_build(build_buf, sizeof(build_buf), GGA_BODY));
        // Full parse: guard + checksum validation + comma field split + talker/type derivation.
        DBENCH_OP("pc_nmea0183_parse GGA", 50000, sinkb ^= pc_nmea0183_parse(GGA, gga_len, &m));
        // Field-value helpers: decode field 2 ("4807.038") as float, field 7 ("08") as int.
        {
            float f = 0.0f;
            DBENCH_OP("pc_nmea0183_field_float", 100000, sinkb ^= pc_nmea0183_field_float(&m, 2, &f); sinkf += f);
        }
        {
            long v = 0;
            DBENCH_OP("pc_nmea0183_field_int", 100000, sinkb ^= pc_nmea0183_field_int(&m, 7, &v); sinkl += v);
        }

        (void)sink8;
        (void)sinksz;
        (void)sinkb;
        (void)sinkf;
        (void)sinkl;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: nmea0183 device microbench");
    xTaskCreatePinnedToCore(nmea0183_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
