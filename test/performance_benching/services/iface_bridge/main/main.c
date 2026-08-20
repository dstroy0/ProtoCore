// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the interface-bridge pure core (server/net/iface_bridge): the
// user-defined address:port -> hardware-bus rule table (register with bind-address parse + dedup, and
// the per-accepted-connection dispatch lookup) and the write-then-read transaction frame codec
// (big-endian write_len/read_len header || write payload). All four ops are pure - no heap, no sockets.
// The actual UART/SPI/I2C bus I/O and the PROTO_BRIDGE listener live in iface_bridge_hw.* and are
// deliberately OUT OF SCOPE on this rig (no bus peripherals attached), exactly like performance_benching/device/ads1115
// benches only the ADS1115 codec and never the I2C transaction. Only iface_bridge.h is included, so the
// Library Dependency Finder never compiles the hardware half - nothing to stub.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/iface_bridge -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/net/iface_bridge/iface_bridge/iface_bridge.h"
#include "shared/ip/ip.h"

#include <stdint.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // Spec-conformant sample data copied straight out of test/test_iface_bridge (already known-good):
    // a 3-byte write payload and the complete request frame it builds into (write_len=3, read_len=5).
    static const uint8_t wr[3] = {0xAA, 0xBB, 0xCC};
    static const uint8_t frame[7] = {0x00, 0x03, 0x00, 0x05, 0xAA, 0xBB, 0xCC};
    static uint8_t out[64];

    // A UART stream target, mirroring uart_target() in the host test.
    BridgeTarget uart;
    memset(&uart, 0, sizeof(uart));
    uart.bus = BRIDGE_BUS_UART;
    uart.mode = BRIDGE_MODE_STREAM;
    uart.unit = 1;
    uart.rate = 115200;

    for (;;)
    {
        DBENCH_BANNER("iface_bridge");
        volatile size_t sink = 0;

        // 1) Register path: build the rule, validate/parse the bind address (protocore_ip_parse), scan for a
        //    port+proto duplicate, and insert. First iteration inserts; the rest exercise the identical
        //    parse+dedup-scan cost (the dominant work the config-time register path does).
        IfaceBridgeV.clear(protocore_iface_bridge_span());
        // The entry call stays inside DBENCH_OP so the timed loop measures the parse and the
        // dedup scan, not the read that follows them.
        IfaceBridgeV.map_args.ip = "192.168.1.50";
        IfaceBridgeV.map_args.port = 4001;
        IfaceBridgeV.map_args.proto = BRIDGE_PROTO_TCP;
        IfaceBridgeV.map_args.target = &uart;
        DBENCH_OP("IfaceBridge.map", 50000,
                  (IfaceBridgeV.map(protocore_iface_bridge_span()), sink += IfaceBridgeV.ok ? 1u : 0u));

        // 2) Listener dispatch: find the rule bound to a port+proto - run on every accepted connection.
        IfaceBridgeV.find_args.port = 4001;
        IfaceBridgeV.find_args.proto = BRIDGE_PROTO_TCP;
        DBENCH_OP("IfaceBridge.find", 200000,
                  (IfaceBridgeV.find(protocore_iface_bridge_span()), sink += (uintptr_t)IfaceBridgeV.rule));

        // 3) Transaction frame build (write-then-read request), MB/s over the whole emitted frame.
        IfaceBridgeV.txn_build_args.out = out;
        IfaceBridgeV.txn_build_args.cap = sizeof(out);
        IfaceBridgeV.txn_build_args.write_data = wr;
        IfaceBridgeV.txn_build_args.write_len = (uint16_t)sizeof(wr);
        IfaceBridgeV.txn_build_args.read_len = 5;
        DBENCH_BULK("protocore_iface_bridge_txn_build", 100000, (size_t)PROTOCORE_BRIDGE_TXN_HDR + sizeof(wr),
                    (IfaceBridge.txn_build(protocore_iface_bridge_span()), sink += IfaceBridgeV.n));

        // 4) Transaction frame parse: the per-request codec on the hot transaction-listener path
        //    (header decode + bounds check, returns a pointer into the buffer - no copy).
        {
            uint16_t wl = 0;
            uint16_t rl = 0;
            const uint8_t *wd = NULL;
            IfaceBridgeV.txn_parse_args.buf = frame;
            IfaceBridgeV.txn_parse_args.len = sizeof(frame);
            IfaceBridgeV.txn_parse_args.write_len = &wl;
            IfaceBridgeV.txn_parse_args.read_len = &rl;
            IfaceBridgeV.txn_parse_args.write_data = &wd;
            DBENCH_OP("IfaceBridge.txn_parse", 200000,
                      (IfaceBridge.txn_parse(protocore_iface_bridge_span()), sink += IfaceBridgeV.n));
        }

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("iface_bridge")
