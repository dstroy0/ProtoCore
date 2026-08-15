// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SenML codec (services/iot/senml): building a SenML-JSON pack
// and a SenML-CBOR pack from a record array. Pure; no transport.
//
// Build/flash:  idf.py -C test/performance_benching/senml -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/presentation/codec/cbor/cbor.h"
#include "services/iot/senml/senml.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Encode @p n Records as application/senml+json into @p out; the octets written. */
static size_t senml_json(char *out, size_t cap, const SenmlRecord *recs, size_t n)
{
    Senml.pack.records = recs;
    Senml.pack.count = n;
    Senml.json.buf = out;
    Senml.json.cap = cap;
    Senml.json_build(Senml.internal);
    return Senml.n;
}

/** @brief Encode @p n Records through the CBOR codec into @p out; the octets written. */
static size_t senml_cbor(uint8_t *out, size_t cap, const SenmlRecord *recs, size_t n)
{
    Senml.pack.records = recs;
    Senml.pack.count = n;
    Senml.binary.codec = &Cbor;
    Senml.binary.buf = out;
    Senml.binary.cap = cap;
    Senml.binary_build(Senml.internal);
    return Senml.n;
}

void dbench_run(void)
{
    static const SenmlRecord recs[3] = {
        {"urn:dev:ow:10e2073a01080063:", true, 1720700000.0, "temp", "Cel", SENML_VALUE_NUMBER, 21.4, NULL, false, false, 0},
        {NULL, false, 0, "humidity", "%RH", SENML_VALUE_NUMBER, 48.0, NULL, false, true, 10.0},
        {NULL, false, 0, "status", NULL, SENML_VALUE_STRING, 0, "ok", false, false, 0},
    };

    for (;;)
    {
        DBENCH_BANNER("senml");
        volatile size_t sink = 0;
        static char jbuf[512];
        static uint8_t cbuf[512];
        DBENCH_OP("Senml.json_build (3 recs)", 200000, sink += senml_json(jbuf, sizeof(jbuf), recs, 3));
        DBENCH_OP("Senml.binary_build CBOR (3 recs)", 200000, sink += senml_cbor(cbuf, sizeof(cbuf), recs, 3));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("senml")
