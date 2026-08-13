// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IEC 61850 MMS PDU codec (services/energy/mms): building the two
// most-used confirmed PDUs and parsing them back. protocore_mms_read_request() BER-encodes a
// confirmed-request/Read for one named ObjectName (a Data Object reference VisibleString);
// protocore_mms_read_response() BER-encodes the matching confirmed-response carrying a caller-encoded
// AccessResult Data value; protocore_mms_parse() walks the outer PDU header (tag + invokeID + service
// tag). All three are pure, zero-heap, no-stdlib TLV work over caller buffers - no sockets. Like
// performance_benching/device/modbus, this is a pure protocol codec, so every call exercises the real production
// path; the TPKT/COTP framing and the port-102 socket (services/fieldbus/cotp + transport) are deliberately
// out of scope here - only the deterministic PDU codec is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/mms -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/energy/mms/mms.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // A realistic Data Object reference (IEC 61850 ACSI object name), straight from test_mms.cpp.
    static const char item[] = "LD0/GGIO1$ST$Ind1$stVal";
    // A caller-encoded AccessResult Data value: [3] BOOLEAN true -> 83 01 FF (from test_mms.cpp).
    static const uint8_t data[] = {0x83, 0x01, 0xFF};
    static uint8_t out[256];

    // Pre-build a confirmed-request Read PDU once (outside timing) so the parse bench has a known-good,
    // spec-conformant buffer to walk on every iteration.
    static uint8_t reqbuf[256];
    size_t reqlen = protocore_mms_read_request(42, item, reqbuf, sizeof(reqbuf));

    for (;;)
    {
        DBENCH_BANNER("mms");
        volatile size_t sink = 0;
        // Encode: confirmed-request / Read for one named variable.
        DBENCH_OP("protocore_mms_read_request", 50000, sink += protocore_mms_read_request(42, item, out, sizeof(out)));
        // Encode: confirmed-response / Read carrying one AccessResult data value.
        DBENCH_OP("protocore_mms_read_response", 50000,
                  sink += protocore_mms_read_response(42, data, sizeof(data), out, sizeof(out)));
        // Decode: parse the outer PDU header (tag + invokeID + service tag) of the pre-built request.
        MmsPdu p;
        DBENCH_OP("protocore_mms_parse", 100000, sink += protocore_mms_parse(reqbuf, reqlen, &p) ? 1u : 0u);
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("mms")
