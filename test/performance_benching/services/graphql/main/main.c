// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the GraphQL query subset (services/iot/graphql):
// GraphQL.execute parses a query document into a fixed AST node pool (no heap) and walks the
// selection set, calling a single leaf resolver and emitting a `{"data":{...}}` JSON response that
// mirrors the requested shape. Every operation here is pure - a parse + execute over a query string
// into a caller buffer - so each call exercises the real production code path (like performance_benching/device/
// modbus, and unlike performance_benching/device/ads1115 where a bus transaction would be stubbed). The only thing
// this bench supplies is the app-side leaf resolver, which the API requires and which is itself a
// tiny pure function (the same demo schema the host test uses); there is no hardware, transport, or
// heap to stub out.
//
// Benched: a flat selection, a nested object, an argument-along-the-path selection, a string
// argument (interning + JSON escaping), the fail-closed parse-error emit, and a bulk parse+execute
// over a representative multi-field query (reported as MB/s of query text). Deliberately out of
// scope: any real transport/socket - this codec only touches the query string and the out buffer.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/graphql -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/graphql/graphql.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Demo schema resolver (mirrors test/test_graphql's): a handful of flat scalar fields, a nested
// `device` object, and argument-driven `sensor`/`greet` leaves. Pure - no I/O of any kind.
static bool gql_resolver(const char *path, const struct protocore_gql_args *args, protocore_gql_value *out)
{
    if (!strcmp(path, "name"))
    {
        out->type = PROTOCORE_GQL_STR;
        out->s = "esp32";
        return true;
    }
    if (!strcmp(path, "uptime"))
    {
        out->type = PROTOCORE_GQL_INT;
        out->i = 12345;
        return true;
    }
    if (!strcmp(path, "temp"))
    {
        out->type = PROTOCORE_GQL_FLOAT;
        out->f = 21.5;
        return true;
    }
    if (!strcmp(path, "online"))
    {
        out->type = PROTOCORE_GQL_BOOL;
        out->b = true;
        return true;
    }
    if (!strcmp(path, "device.name"))
    {
        out->type = PROTOCORE_GQL_STR;
        out->s = "dev1";
        return true;
    }
    if (!strcmp(path, "device.uptime"))
    {
        out->type = PROTOCORE_GQL_INT;
        out->i = 99;
        return true;
    }
    if (!strcmp(path, "sensor.value"))
    {
        GraphQLV.argument.values = args;
        GraphQLV.argument.name = "id";
        GraphQL.arg_int(protocore_graphql_span());
        out->type = PROTOCORE_GQL_INT;
        out->i = GraphQLV.ok ? GraphQLV.i64 * 10 : -1;
        return true;
    }
    if (!strcmp(path, "greet"))
    {
        GraphQLV.argument.values = args;
        GraphQLV.argument.name = "name";
        GraphQL.arg_str(protocore_graphql_span());
        const char *who = GraphQLV.ok ? GraphQLV.text : "?";
        static char b[64];
        snprintf(b, sizeof(b), "hi %s", who);
        out->type = PROTOCORE_GQL_STR;
        out->s = b;
        return true;
    }
    return false; // -> JSON null
}

/** @brief Execute @p doc through gql_resolver into @p out; the outcome code. */
static protocore_gql_result gql_execute(const char *doc, size_t len, char *out, size_t cap)
{
    GraphQLV.request.document = doc;
    GraphQLV.request.len = len;
    GraphQLV.request.resolver = gql_resolver;
    GraphQLV.response.out = out;
    GraphQLV.response.cap = cap;
    GraphQL.execute(protocore_graphql_span());
    return GraphQLV.result;
}

void dbench_run(void)
{
    // Query documents lifted straight from test/test_graphql (known-good, spec-conformant).
    static const char q_flat[] = "{ name uptime }";
    static const char q_nested[] = "{ device { name uptime } }";
    static const char q_args[] = "{ sensor(id: 7) { value } }";
    static const char q_strarg[] = "{ greet(name: \"a\\\"b\") }";
    static const char q_err[] = "{ name "; // unterminated -> fail-closed error document
    // A representative multi-field query for the throughput (MB/s) measurement.
    static const char q_bulk[] = "{ name uptime temp online device { name uptime } sensor(id: 7) { value } }";

    static char resp[512];

    for (;;)
    {
        DBENCH_BANNER("graphql");
        volatile int32_t sink = 0;
        DBENCH_OP("GraphQL.execute flat", 20000,
                  sink += (int32_t)gql_execute(q_flat, sizeof(q_flat) - 1, resp, sizeof(resp)));
        DBENCH_OP("GraphQL.execute nested", 20000,
                  sink += (int32_t)gql_execute(q_nested, sizeof(q_nested) - 1, resp, sizeof(resp)));
        DBENCH_OP("GraphQL.execute args", 20000,
                  sink += (int32_t)gql_execute(q_args, sizeof(q_args) - 1, resp, sizeof(resp)));
        DBENCH_OP("GraphQL.execute strarg", 20000,
                  sink += (int32_t)gql_execute(q_strarg, sizeof(q_strarg) - 1, resp, sizeof(resp)));
        DBENCH_OP("GraphQL.execute parse_err", 20000,
                  sink += (int32_t)gql_execute(q_err, sizeof(q_err) - 1, resp, sizeof(resp)));
        DBENCH_BULK("GraphQL.execute bulk", 20000, sizeof(q_bulk) - 1,
                    sink += (int32_t)gql_execute(q_bulk, sizeof(q_bulk) - 1, resp, sizeof(resp)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("graphql")
