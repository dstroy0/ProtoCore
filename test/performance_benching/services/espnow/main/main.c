// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the ESP-NOW envelope codec + peer registry
// (services/radio/espnow): protocore_espnow_encode/decode (typed [magic][type][len] + payload framing) and
// the bounded peer registry (protocore_espnow_peer_add/has), all pure - no heap, no radio. Per
// test_matrix.json, the envelope codec + peer registry are host-tested here while the esp_now
// radio binding is ESP32-only; this rig benches only the shared pure core. Deliberately out of
// scope: protocore_espnow_begin/add_peer/send/broadcast, which call esp_now_init()/esp_now_add_peer()/
// esp_now_send() against real WiFi/radio state this rig does not have configured - exercising
// them here would be real radio I/O, not a deterministic CPU-side microbenchmark.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/espnow -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/radio/espnow/espnow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    Espnow.peers_reset(protocore_espnow_span());

    // Small envelope, mirrors test_espnow.cpp's roundtrip fixture.
    static const uint8_t payload_small[] = {1, 2, 3, 4, 5};
    static uint8_t frame_small[PROTOCORE_ESPNOW_HDR + sizeof(payload_small)];
    EspnowV.encode_args.type = 42;
    EspnowV.encode_args.payload = payload_small;
    EspnowV.encode_args.len = sizeof(payload_small);
    EspnowV.encode_args.out = frame_small;
    EspnowV.encode_args.cap = sizeof(frame_small);
    Espnow.encode(protocore_espnow_span());
    size_t n_small = EspnowV.n;

    // Max-size envelope (radio MTU minus header) for the bulk/throughput numbers.
    static uint8_t payload_max[PROTOCORE_ESPNOW_MAX_PAYLOAD];
    for (size_t i = 0; i < sizeof(payload_max); i++)
    {
        payload_max[i] = (uint8_t)i;
    }
    static uint8_t frame_max[PROTOCORE_ESPNOW_HDR + PROTOCORE_ESPNOW_MAX_PAYLOAD];
    EspnowV.encode_args.type = 7;
    EspnowV.encode_args.payload = payload_max;
    EspnowV.encode_args.len = sizeof(payload_max);
    EspnowV.encode_args.out = frame_max;
    EspnowV.encode_args.cap = sizeof(frame_max);
    Espnow.encode(protocore_espnow_span());
    size_t n_max = EspnowV.n;

    // One registered peer to exercise the registry's lookup/add-idempotent paths.
    static const uint8_t mac_a[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0xAA};
    EspnowV.peer_add_args.mac = mac_a;
    Espnow.peer_add(protocore_espnow_span());

    static uint8_t g_type_out = 0;
    static const uint8_t *g_payload_out = NULL;
    static size_t g_plen_out = 0;

    for (;;)
    {
        DBENCH_BANNER("espnow");
        volatile size_t sink = 0;
        volatile bool bsink = false;

        DBENCH_OP("protocore_espnow_encode", 100000,
                  sink +=
                  protocore_espnow_encode(42, payload_small, sizeof(payload_small), frame_small, sizeof(frame_small)));
        DBENCH_OP("protocore_espnow_decode", 100000,
                  bsink = protocore_espnow_decode(frame_small, n_small, &g_type_out, &g_payload_out, &g_plen_out));
        DBENCH_BULK("protocore_espnow_encode_max", 20000, PROTOCORE_ESPNOW_MAX_PAYLOAD,
                    sink += protocore_espnow_encode(7, payload_max, sizeof(payload_max), frame_max, sizeof(frame_max)));
        DBENCH_BULK("protocore_espnow_decode_max", 20000, PROTOCORE_ESPNOW_MAX_PAYLOAD,
                    bsink = protocore_espnow_decode(frame_max, n_max, &g_type_out, &g_payload_out, &g_plen_out));
        DBENCH_OP("protocore_espnow_peer_has", 100000, bsink = protocore_espnow_peer_has(mac_a));
        DBENCH_OP("protocore_espnow_peer_add", 100000, bsink = protocore_espnow_peer_add(mac_a)); // idempotent re-add

        (void)sink;
        (void)bsink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("espnow")
