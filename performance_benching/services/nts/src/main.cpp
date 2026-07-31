// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Network Time Security wire codec (services/timing_position/nts, RFC 8915):
// building the standard NTS-KE client request (Next-Protocol + AEAD + End-of-Message TLV records),
// building a single NTS-KE record, walking an NTS-KE response stream, and framing the NTS NTP
// extension fields (Unique Identifier / Cookie, with RFC 7822 4-byte padding). All of this is pure
// wire framing - zero heap, no stdlib, no sockets - so every call here exercises the real production
// code path (like performance_benching/device/modbus, and unlike a peripheral driver). Deliberately out of scope: the
// AES-SIV-CMAC-256 AEAD (RFC 5297) and the TLS-exporter key derivation that sit on top of this framing
// are crypto integration, not part of this codec, so they are not benched here. The NTS-KE parse
// callback (pc_nts_ke_cb) is satisfied by a tiny local no-op sink - it is a required function pointer,
// not a hardware/transport dependency.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/nts -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/timing_position/nts/nts.h"
#include <Arduino.h>

// No-op sink satisfying the pc_nts_ke_cb function-pointer arg of pc_nts_ke_parse; counts records so the
// parse cannot be optimized away. Not a hardware/transport stub - the parser has no such dependency.
static void nts_ke_sink(bool critical, uint16_t type, const uint8_t *body, size_t body_len, void *arg)
{
    (void)critical;
    (void)type;
    (void)body;
    (void)body_len;
    if (arg)
        (*(volatile uint32_t *)arg)++;
}

static void nts_bench_task(void *)
{
    // 32-byte "random" for a Unique Identifier EF (RFC 8915 sec 5.3 requires >= 32 bytes); the exact
    // byte fill matches test/test_nts/test_nts.cpp (0xAB) so this is known-good, spec-conformant data.
    static uint8_t uid[32];
    memset(uid, 0xAB, sizeof(uid));
    // A representative NTS cookie blob (server-opaque; length is what drives the padding math).
    static uint8_t cookie[64];
    memset(cookie, 0x5A, sizeof(cookie));
    // A 2-byte AEAD-algorithm body, as carried by one critical NTS-KE record.
    static const uint8_t aead_body[2] = {0x00, 0x0F}; // AES-SIV-CMAC-256 = 15

    // Pre-build the standard 16-byte NTS-KE request once; the parse bench walks this known-good stream.
    static uint8_t req[32];
    size_t req_len = pc_nts_ke_request(req, sizeof(req));

    static uint8_t out[128];

    for (;;)
    {
        Serial.printf("DB ==== nts device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile uint32_t rec_count = 0;

        DBENCH_OP("pc_nts_ke_request", 100000, sink += pc_nts_ke_request(out, sizeof(out)));
        DBENCH_OP("pc_nts_ke_record aead", 100000,
                  sink +=
                  pc_nts_ke_record(true, Nts::NTS_KE_AEAD_ALGORITHM, aead_body, sizeof(aead_body), out, sizeof(out)));
        DBENCH_OP("pc_nts_ke_parse request", 100000,
                  sink += pc_nts_ke_parse(req, req_len, nts_ke_sink, (void *)&rec_count));
        DBENCH_OP("pc_nts_ef_unique_id 32B", 100000, sink += pc_nts_ef_unique_id(uid, sizeof(uid), out, sizeof(out)));
        DBENCH_OP("pc_nts_ef_cookie 64B", 100000, sink += pc_nts_ef_cookie(cookie, sizeof(cookie), out, sizeof(out)));

        (void)sink;
        (void)rec_count;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: nts device microbench");
    xTaskCreatePinnedToCore(nts_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
