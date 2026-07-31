// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the GNSS RTK base / NTRIP caster pure core (services/timing_position/gnss):
//   - rtcm3: the RTCM 3.x transport frame (0xD3 preamble, 10-bit length, CRC-24Q) and the 1005/1006
//     Stationary Antenna Reference Point codec - build (bit-pack + CRC), parse (CRC-verify + bit-unpack),
//     the CRC-24Q kernel over a full frame, and preamble sync over a byte stream.
//   - gnss_survey: the WGS84 geodetic <-> ECEF transforms (the closed-form forward and the iterative
//     inverse) and the survey-in accumulator step - the transcendental-heavy geodesy math a base runs
//     per fix while surveying in its own antenna position.
// All of this is pure, zero-heap, no-I/O math and bit-shuffling, so every call exercises the real
// production path. Deliberately out of scope: the NTRIP caster/listener transport glue
// (ntrip_caster_listener.*) - it drives real TCP sockets this rig has no peer for - and the NMEA0183
// GGA folding helper (built only under PC_ENABLE_NMEA0183, which this bench does not enable).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/gnss -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/timing_position/gnss/gnss_survey.h"
#include "services/timing_position/gnss/rtcm3.h"
#include <Arduino.h>

// pyrtcm-confirmed reference frames + the ECEF antenna reference point behind them (copied verbatim
// from test/test_rtcm3/test_rtcm3.cpp - already known-good, spec-conformant, CRC-valid).
static const uint16_t SID = 2003;
static const int64_t EX = 11149789412LL;
static const int64_t EY = -48500020000LL;
static const int64_t EZ = 39752890000LL;
static const uint16_t AH = 12500; // antenna height (0.1 mm) for 1006

static const uint8_t FRAME_1005[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02, 0x98, 0x94, 0x48, 0xE4, 0x34,
                                     0xB5, 0x2C, 0x6C, 0xE0, 0x09, 0x41, 0x74, 0xF6, 0x90, 0x1E, 0xA5, 0x47};

static void gnss_bench_task(void *)
{
    static uint8_t frame[RTCM3_MAX_FRAME];

    for (;;)
    {
        Serial.printf("DB ==== gnss device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile bool bsink = false;
        volatile double dsink = 0.0;

        // --- RTCM3 framing + 1005/1006 station-reference codec ---
        // Build a full 1005 frame: pack the ARP fields MSB-first, then CRC-24Q the header+payload.
        DBENCH_OP("rtcm3_build_1005", 20000, sink += pc_rtcm3_build_1005(frame, sizeof(frame), SID, EX, EY, EZ));
        // Build a full 1006 frame (adds the 16-bit antenna-height field).
        DBENCH_OP("rtcm3_build_1006", 20000, sink += pc_rtcm3_build_1006(frame, sizeof(frame), SID, EX, EY, EZ, AH));
        // Parse a framed 1005: length probe + CRC-24Q verify + read the 12-bit message number.
        {
            Rtcm3Frame f;
            DBENCH_OP("rtcm3_frame_parse", 20000, sink += pc_rtcm3_frame_parse(FRAME_1005, sizeof(FRAME_1005), &f));
        }
        // Decode the 1005 payload into the station ARP struct (MSB-first bit unpack of every DF field).
        {
            const uint8_t *payload = FRAME_1005 + RTCM3_HDR_LEN;
            Rtcm3StationArp arp;
            DBENCH_OP("rtcm3_parse_1005", 20000,
                      bsink ^= pc_rtcm3_parse_1005(payload, sizeof(FRAME_1005) - RTCM3_HDR_LEN - RTCM3_CRC_LEN, &arp));
        }
        // CRC-24Q kernel over the full frame body (the RTCM3 / Qualcomm CRC).
        DBENCH_BULK("rtcm3_crc24q", 20000, sizeof(FRAME_1005), sink += pc_rtcm3_crc24q(FRAME_1005, sizeof(FRAME_1005)));

        // --- GNSS survey-in geodesy math ---
        // WGS84 geodetic -> ECEF (closed form; sin/cos/sqrt per call).
        {
            GnssGeodetic g = {40.44338, -79.94238, 312.5};
            GnssEcef e = {0, 0, 0};
            DBENCH_OP("gnss_geodetic_to_ecef", 20000, pc_gnss_geodetic_to_ecef(&g, &e); dsink += e.x);
        }
        // ECEF -> WGS84 geodetic (6-pass iterative inverse; the more expensive direction).
        {
            GnssEcef e = {849490.0, -4813977.0, 4114789.0};
            GnssGeodetic g = {0, 0, 0};
            DBENCH_OP("gnss_ecef_to_geodetic", 10000, pc_gnss_ecef_to_geodetic(&e, &g); dsink += g.lat_deg);
        }
        // One survey-in accumulation step (running mean + 3-D spread), the per-fix hot path.
        {
            GnssSurvey s;
            pc_gnss_survey_reset(&s);
            GnssEcef e = {849490.0, -4813977.0, 4114789.0};
            DBENCH_OP("gnss_survey_add_ecef", 50000, pc_gnss_survey_add_ecef(&s, &e));
        }

        (void)sink;
        (void)bsink;
        (void)dsink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: gnss device microbench");
    xTaskCreatePinnedToCore(gnss_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
