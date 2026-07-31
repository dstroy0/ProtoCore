// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OMA LwM2M TLV codec (services/iot/lwm2m): the zero-heap
// writer (raw + int/bool/string/float value helpers, 8-/16-bit ids, inline/8-/16-/24-bit lengths),
// the cursor reader (pc_lwm2m_tlv_read), and integer value decoding (pc_lwm2m_tlv_value_int).
// This is a pure `application/vnd.oma.lwm2m+tlv` byte codec, so - like performance_benching/device/modbus - every
// call here exercises the real production code path. Deliberately out of scope: the CoAP/DTLS
// transport that would actually carry these TLVs over the wire; this rig has no network attached
// and the codec itself never touches a socket, heap, or peripheral.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/lwm2m -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/lwm2m/lwm2m_tlv.h"
#include <Arduino.h>

// Build a small, realistic LwM2M Device-object (Object 3) instance payload: a resource integer
// (shortest-octet encode), a UTF-8 manufacturer string, a boolean, and an 8-octet IEEE-754 float -
// exercising every value-helper path of the writer. Returns bytes emitted (0 on overflow).
static size_t lwm2m_encode_instance(uint8_t *buf, size_t cap)
{
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, cap);
    pc_lwm2m_tlv_write_int(&w, 0, 12345);                     // Resource /0: int (2-octet)
    pc_lwm2m_tlv_write_string(&w, 1, "Open Mobile Alliance"); // Resource /1: UTF-8 string
    pc_lwm2m_tlv_write_bool(&w, 2, true);                     // Resource /2: bool
    pc_lwm2m_tlv_write_float(&w, 3, 3.14159265358979);        // Resource /3: 8-octet float
    return pc_lwm2m_tlv_finish(&w);
}

// A single shortest-octet resource-integer write (init + one write_int), the hottest encode path.
static bool lwm2m_write_one_int(uint8_t *buf, size_t cap)
{
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, cap);
    return pc_lwm2m_tlv_write_int(&w, 3, -2000000000LL); // forces the 4-octet branch
}

// Walk the whole payload with the cursor reader, decoding every TLV at the buffer head. Returns the
// count decoded (used only to keep the loop from being optimized away).
static size_t lwm2m_decode_all(const uint8_t *buf, size_t len)
{
    size_t pos = 0, count = 0;
    Lwm2mTlv tlv;
    while (pc_lwm2m_tlv_read(buf, len, &pos, &tlv))
        count++;
    return count;
}

static void lwm2m_bench_task(void *)
{
    static uint8_t buf[256];
    // Pre-encode the instance once so the reader/decoder benches have a real, spec-conformant payload.
    size_t encoded_len = lwm2m_encode_instance(buf, sizeof(buf));

    // A 4-octet big-endian two's-complement integer TLV value (0x12345678) for the value-decode bench.
    static const uint8_t int_val[4] = {0x12, 0x34, 0x56, 0x78};

    for (;;)
    {
        Serial.printf("DB ==== lwm2m device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile int64_t isink = 0;
        int64_t ival = 0;

        DBENCH_OP("pc_lwm2m_encode_instance", 50000, sink += lwm2m_encode_instance(buf, sizeof(buf)));
        DBENCH_OP("pc_lwm2m_tlv_write_int", 200000, sink += lwm2m_write_one_int(buf, sizeof(buf)));
        DBENCH_BULK("pc_lwm2m_tlv_read (walk)", 50000, encoded_len, sink += lwm2m_decode_all(buf, encoded_len));
        DBENCH_OP("pc_lwm2m_tlv_value_int", 200000, isink += pc_lwm2m_tlv_value_int(int_val, sizeof(int_val), &ival));

        (void)sink;
        (void)isink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: lwm2m device microbench");
    xTaskCreatePinnedToCore(lwm2m_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
