// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the AMQP 0-9-1 frame codec (services/iot/amqp): the protocol
// header, the frame + method builders, the heartbeat builder, and the frame/method parsers - all
// pure (no heap, no sockets). Worked pattern for performance_benching/device/<service>/: a pure protocol codec
// with no hardware involved, so every call here exercises the real production code path (contrast
// with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed). The
// AMQP outbound client transport (the actual TCP socket a real broker connection rides on) is out
// of scope everywhere on this rig - only the deterministic CPU-side codec is ever benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/amqp -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/amqp/amqp.h"
#include <Arduino.h>

static void amqp_bench_task(void *)
{
    // A Connection.Start-ish method (class 10, method 10) on channel 1 - mirrors
    // test/test_amqp/test_amqp.cpp::test_build_method_bytes.
    static const uint8_t method_args[] = {0x00};
    static uint8_t hdr_buf[8];
    static uint8_t method_buf[32];
    static uint8_t heartbeat_buf[8];

    // Pre-build a method frame once so the parsers have a real, known-good frame to chew on.
    size_t method_len =
        pc_amqp_build_method(method_buf, sizeof(method_buf), 1, 10, 10, method_args, sizeof(method_args));

    for (;;)
    {
        Serial.printf("DB ==== amqp device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        AmqpFrame f;
        size_t consumed;
        uint16_t cls, meth;
        const uint8_t *args;
        size_t args_len;

        DBENCH_OP("pc_amqp_protocol_header", 200000, sink += pc_amqp_protocol_header(hdr_buf, sizeof(hdr_buf)));
        DBENCH_OP("pc_amqp_build_method", 100000,
                  sink +=
                  pc_amqp_build_method(method_buf, sizeof(method_buf), 1, 10, 10, method_args, sizeof(method_args)));
        DBENCH_OP("pc_amqp_build_heartbeat", 200000,
                  sink += pc_amqp_build_heartbeat(heartbeat_buf, sizeof(heartbeat_buf)));
        DBENCH_OP("pc_amqp_parse_frame", 100000,
                  sink += pc_amqp_parse_frame(method_buf, method_len, &f, &consumed) ? consumed : 0);
        DBENCH_OP("pc_amqp_parse_method", 100000,
                  sink += pc_amqp_parse_method(f.payload, f.payload_len, &cls, &meth, &args, &args_len) ? 1 : 0);
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: amqp device microbench");
    xTaskCreatePinnedToCore(amqp_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
