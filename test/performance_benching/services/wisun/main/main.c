// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Wi-SUN FAN codec (services/radio/wisun): the RFC 7252 CoAP
// request builder (the per-request hot op over the mesh) and the pure node-registry bookkeeping
// (register / find). Pure; no radio.
//
// Build/flash:  idf.py -C test/performance_benching/wisun -t upload --upload-port COM7
#include "device_bench.h"
#include "services/radio/wisun/wisun.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t wisun_work[16]; // the borrow an entry takes; Wisun never reads it

void dbench_run(void)
{
    static const uint8_t v6[16] = {0xfd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    protocore_ip br = protocore_ip_from_v6_bytes(v6);
    static WisunNode storage[8];
    static WisunFan fan;

    for (;;)
    {
        DBENCH_BANNER("wisun");
        volatile size_t sink = 0;
        static const uint8_t body[4] = {0x01, 0x02, 0x03, 0x04};
        static uint8_t out[128];
        DBENCH_OP("protocore_wisun_build_coap (CON PUT)", 200000,
                  sink += protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_PUT, 0x1234, NULL, 0, "led", body,
                                                     sizeof(body), out, sizeof(out)));
        Wisun.init_args.fan = &fan;
        Wisun.init_args.border_router = &br;
        Wisun.init_args.storage = storage;
        Wisun.init_args.cap = 8;
        Wisun.init(wisun_work);
        uint8_t na[16] = {0xfd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
        DBENCH_OP("protocore_wisun_node_register", 100000, {
            na[15] = (uint8_t)(sink & 0x7F) + 2;
            protocore_ip addr = protocore_ip_from_v6_bytes(na);
            Wisun.node_register_args.fan = &fan;
            Wisun.node_register_args.addr = &addr;
            Wisun.node_register_args.now = 0;
            Wisun.node_register(wisun_work);
            sink += (size_t)(Wisun.i32 >= 0 ? 1 : 0);
        });
        protocore_ip find = protocore_ip_from_v6_bytes(na);
        size_t idx;
        DBENCH_OP("protocore_wisun_node_find", 200000, sink += protocore_wisun_node_find(&fan, &find, &idx));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("wisun")
