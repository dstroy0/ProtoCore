// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the IEC 61850 MMS codec (ISO 9506, the client/server core of IEC 61850 over
// ISO-on-TCP/102): the confirmed-request Read builder (BER-encoded ObjectName for a Data Object reference),
// the confirmed-response Read-data builder, and the confirmed-PDU header parser (tag + invokeID + service
// tag). Pure (no socket, no TPKT/COTP), so it links standalone. The device figure comes from the rig /bench
// op; this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_MMS=1 test/performance_benching/services/mms/host.c
//   src/services/energy/mms/mms.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bmms && /tmp/bmms

#define PROTOCORE_ENABLE_MMS 1
#include "services/energy/mms/mms.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    // A typical IEC 61850 Data Object reference (indication status value) - the item a client reads most.
    const char *item = "LD0/GGIO1$ST$Ind1$stVal";
    uint8_t req[128];
    size_t req_len = protocore_mms_read_request(0x0102, item, req, sizeof(req));

    // A pre-encoded BER AccessResult data value (boolean-ish: 85 01 01) for the response builder.
    const uint8_t data[] = {0x85, 0x01, 0x01};
    uint8_t resp[128];
    size_t resp_len = protocore_mms_read_response(0x0102, data, sizeof(data), resp, sizeof(resp));

    hbench_header();

    // protocore_mms_read_request: BER-encode the confirmed-request Read PDU for one named variable (transmit op).
    {
        uint8_t buf[128];
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000, sink += protocore_mms_read_request(0x0102, item, buf, sizeof(buf)), ns);
        hbench_row("mms", "read_request (build)", ns, (double)req_len);
        (void)sink;
    }

    // protocore_mms_read_response: BER-encode the confirmed-response carrying one AccessResult data value.
    {
        uint8_t buf[128];
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000, sink += protocore_mms_read_response(0x0102, data, sizeof(data), buf, sizeof(buf)), ns);
        hbench_row("mms", "read_response (build)", ns, (double)resp_len);
        (void)sink;
    }

    // protocore_mms_parse: validate the confirmed-PDU BER header + decode the invokeID + slice the service body.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                MmsPdu p;
                if (protocore_mms_parse(req, req_len, &p))
                {
                    sink += p.invoke_id + p.service_len;
                }
            },
            ns);
        hbench_row("mms", "parse (confirmed PDU)", ns, (double)req_len);
        (void)sink;
    }

    return 0;
}
