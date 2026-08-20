// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the CoAP server codec (RFC 7252): protocore_coap_server_process() takes a full
// request datagram and produces the response datagram - parse header + options, reconstruct the
// Uri-Path, dispatch against the resource table, encode a piggybacked reply. Pure (no sockets, no
// heap), so it links standalone (the UDP transport symbols it references are stubbed below). The
// device number comes from the rig /bench endpoint; this host ns/op + MB/s is a relative baseline.
// Build + run (udp.c provides the host no-op UDP stubs coap.c's transport path references):
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_COAP=1 test/performance_benching/services/coap/host.c src/services/iot/coap/coap.c
//   src/network_drivers/transport/udp.c src/network_drivers/transport/udp/udp_listener.c
//   src/network_drivers/transport/udp/udp_client.c src/network_drivers/transport/net_addr.c
//   src/server/clock/clock.c src/mmgr/protomem.c src/mmgr/protostr.c src/shared/ip/ip.c
//   -o /tmp/bc && /tmp/bc

#define PROTOCORE_ENABLE_COAP 1
#include "services/iot/coap/coap/coap.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

static void h_info(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    static const char body[] = "{\"uptime_ms\":123456,\"free_heap\":204800}";
    size_t n = sizeof(body) - 1;
    if (n > resp->payload_cap)
    {
        n = resp->payload_cap;
    }
    memcpy(resp->payload, body, n);
    resp->payload_len = n;
    resp->content_format = COAP_CF_JSON;
    resp->code = (uint8_t)COAP_RSP_CONTENT;
}

int main(void)
{
    Coap.reset(protocore_coap_span());
    CoapV.resource.path = "/info";
    CoapV.resource.methods = COAP_ALLOW_GET;
    CoapV.resource.handler = h_info;
    Coap.add_resource(protocore_coap_span());
    CoapV.resource.path = "/a/b/c";
    CoapV.resource.methods = COAP_ALLOW_GET;
    CoapV.resource.handler = h_info;
    Coap.add_resource(protocore_coap_span());

    // A CON GET /info: ver=1 type=CON tkl=4, code=0.01 GET, MID, 4-byte token, Uri-Path "info".
    const uint8_t get_info[] = {0x44, 0x01, 0x12, 0x34, 0xAA, 0xBB, 0xCC, 0xDD, 0xB4, 'i', 'n', 'f', 'o'};
    // A CON GET /a/b/c: three Uri-Path options (each delta 11, len 1).
    const uint8_t get_abc[] = {0x40, 0x01, 0x56, 0x78, 0xB1, 'a', 0x11, 'b', 0x11, 'c'};

    uint8_t resp[256];

    hbench_header();

    {
        volatile size_t sink = 0;
        double ns = 0.0;
        CoapV.msg.req = get_info;
        CoapV.msg.req_len = sizeof(get_info);
        CoapV.msg.resp = resp;
        CoapV.msg.resp_cap = sizeof(resp);
        Coap.process(protocore_coap_span());
        HBENCH_NS(1000000, sink += CoapV.n, ns);
        hbench_row("coap", "process GET /info", ns, (double)sizeof(get_info));
        (void)sink;
    }
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        CoapV.msg.req = get_abc;
        CoapV.msg.req_len = sizeof(get_abc);
        CoapV.msg.resp = resp;
        CoapV.msg.resp_cap = sizeof(resp);
        Coap.process(protocore_coap_span());
        HBENCH_NS(1000000, sink += CoapV.n, ns);
        hbench_row("coap", "process GET /a/b/c", ns, (double)sizeof(get_abc));
        (void)sink;
    }

    return 0;
}
