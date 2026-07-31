// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the interface-bridge pure core (services/net/iface_bridge): the
// user-defined address:port -> hardware-bus rule table (register with bind-address parse + dedup, and
// the per-accepted-connection dispatch lookup) and the write-then-read transaction frame codec
// (big-endian write_len/read_len header || write payload). All four ops are pure - no heap, no sockets.
// The actual UART/SPI/I2C bus I/O and the PROTO_BRIDGE listener live in iface_bridge_hw.* and are
// deliberately OUT OF SCOPE on this rig (no bus peripherals attached), exactly like performance_benching/device/ads1115
// benches only the ADS1115 codec and never the I2C transaction. Only iface_bridge.h is included, so the
// Library Dependency Finder never compiles the hardware half - nothing to stub.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/iface_bridge -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/net/iface_bridge/iface_bridge.h"
#include <Arduino.h>
#include <stdint.h>
#include <string.h>

static void iface_bridge_bench_task(void *)
{
    // Spec-conformant sample data copied straight out of test/test_iface_bridge (already known-good):
    // a 3-byte write payload and the complete request frame it builds into (write_len=3, read_len=5).
    static const uint8_t wr[3] = {0xAA, 0xBB, 0xCC};
    static const uint8_t frame[7] = {0x00, 0x03, 0x00, 0x05, 0xAA, 0xBB, 0xCC};
    static uint8_t out[64];

    // A UART stream target, mirroring uart_target() in the host test.
    BridgeTarget uart;
    memset(&uart, 0, sizeof(uart));
    uart.bus = BridgeBus::uart;
    uart.mode = BridgeMode::stream;
    uart.unit = 1;
    uart.rate = 115200;

    for (;;)
    {
        Serial.printf("DB ==== iface_bridge device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        // 1) Register path: build the rule, validate/parse the bind address (pc_ip_parse), scan for a
        //    port+proto duplicate, and insert. First iteration inserts; the rest exercise the identical
        //    parse+dedup-scan cost (the dominant work the config-time register path does).
        pc_iface_bridge_clear();
        DBENCH_OP("pc_iface_bridge_map", 50000,
                  sink += pc_iface_bridge_map("192.168.1.50", 4001, BridgeProto::tcp, &uart) ? 1u : 0u);

        // 2) Listener dispatch: find the rule bound to a port+proto - run on every accepted connection.
        DBENCH_OP("pc_iface_bridge_find", 200000, sink += (uintptr_t)pc_iface_bridge_find(4001, BridgeProto::tcp));

        // 3) Transaction frame build (write-then-read request), MB/s over the whole emitted frame.
        DBENCH_BULK("pc_iface_bridge_txn_build", 100000, (size_t)PC_BRIDGE_TXN_HDR + sizeof(wr),
                    sink += pc_iface_bridge_txn_build(out, sizeof(out), wr, (uint16_t)sizeof(wr), 5));

        // 4) Transaction frame parse: the per-request codec on the hot transaction-listener path
        //    (header decode + bounds check, returns a pointer into the buffer - no copy).
        {
            uint16_t wl = 0;
            uint16_t rl = 0;
            const uint8_t *wd = nullptr;
            DBENCH_OP("pc_iface_bridge_txn_parse", 200000,
                      sink += pc_iface_bridge_txn_parse(frame, sizeof(frame), &wl, &rl, &wd));
        }

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: iface_bridge device microbench");
    xTaskCreatePinnedToCore(iface_bridge_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
