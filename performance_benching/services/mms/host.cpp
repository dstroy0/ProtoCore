// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the IEC 61850 MMS codec (ISO 9506, the client/server core of IEC 61850 over
// ISO-on-TCP/102): the confirmed-request Read builder (BER-encoded ObjectName for a Data Object reference),
// the confirmed-response Read-data builder, and the confirmed-PDU header parser (tag + invokeID + service
// tag). Pure (no socket, no TPKT/COTP), so it links standalone. The device figure comes from the rig /bench
// op; this host ns/op + MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_MMS=1 performance_benching/services/mms/host.cpp \
//       src/services/energy/mms/mms.cpp -o /tmp/bmms && /tmp/bmms

#define PC_ENABLE_MMS 1
#include "services/energy/mms/mms.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using clk = std::chrono::steady_clock;

template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
    {
        fn();
    }
    auto t1 = clk::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / (double)iters;
}

static void row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    printf("| %-7s | %-26s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    // A typical IEC 61850 Data Object reference (indication status value) - the item a client reads most.
    const char *item = "LD0/GGIO1$ST$Ind1$stVal";
    uint8_t req[128];
    size_t req_len = pc_mms_read_request(0x0102, item, req, sizeof(req));

    // A pre-encoded BER AccessResult data value (boolean-ish: 85 01 01) for the response builder.
    const uint8_t data[] = {0x85, 0x01, 0x01};
    uint8_t resp[128];
    size_t resp_len = pc_mms_read_response(0x0102, data, sizeof(data), resp, sizeof(resp));

    printf("| Feature | Operation                  |     ns/op |    MB/s |\n");
    printf("|---------|----------------------------|-----------|---------|\n");

    // pc_mms_read_request: BER-encode the confirmed-request Read PDU for one named variable (transmit op).
    {
        uint8_t buf[128];
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] { sink += pc_mms_read_request(0x0102, item, buf, sizeof(buf)); });
        row("mms", "read_request (build)", ns, (double)req_len);
        (void)sink;
    }

    // pc_mms_read_response: BER-encode the confirmed-response carrying one AccessResult data value.
    {
        uint8_t buf[128];
        volatile size_t sink = 0;
        double ns =
            bench_ns(5000000, [&] { sink += pc_mms_read_response(0x0102, data, sizeof(data), buf, sizeof(buf)); });
        row("mms", "read_response (build)", ns, (double)resp_len);
        (void)sink;
    }

    // pc_mms_parse: validate the confirmed-PDU BER header + decode the invokeID + slice the service body.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(10000000, [&] {
            MmsPdu p;
            if (pc_mms_parse(req, req_len, &p))
            {
                sink += p.invoke_id + p.service_len;
            }
        });
        row("mms", "parse (confirmed PDU)", ns, (double)req_len);
        (void)sink;
    }

    return 0;
}
