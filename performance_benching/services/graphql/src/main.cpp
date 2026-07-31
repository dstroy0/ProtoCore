// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the GraphQL query subset (services/iot/graphql):
// pc_graphql_execute() parses a query document into a fixed AST node pool (no heap) and walks the
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
//   pio run -d performance_benching/device/graphql -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/graphql/graphql.h"
#include <Arduino.h>
#include <string.h>

// Demo schema resolver (mirrors test/test_graphql's): a handful of flat scalar fields, a nested
// `device` object, and argument-driven `sensor`/`greet` leaves. Pure - no I/O of any kind.
static bool gql_resolver(const char *path, const pc_gql_args *args, pc_gql_value *out)
{
    if (!strcmp(path, "name"))
    {
        out->type = pc_gql_type::PC_GQL_STR;
        out->s = "esp32";
        return true;
    }
    if (!strcmp(path, "uptime"))
    {
        out->type = pc_gql_type::PC_GQL_INT;
        out->i = 12345;
        return true;
    }
    if (!strcmp(path, "temp"))
    {
        out->type = pc_gql_type::PC_GQL_FLOAT;
        out->f = 21.5;
        return true;
    }
    if (!strcmp(path, "online"))
    {
        out->type = pc_gql_type::PC_GQL_BOOL;
        out->b = true;
        return true;
    }
    if (!strcmp(path, "device.name"))
    {
        out->type = pc_gql_type::PC_GQL_STR;
        out->s = "dev1";
        return true;
    }
    if (!strcmp(path, "device.uptime"))
    {
        out->type = pc_gql_type::PC_GQL_INT;
        out->i = 99;
        return true;
    }
    if (!strcmp(path, "sensor.value"))
    {
        long long id = 0;
        out->type = pc_gql_type::PC_GQL_INT;
        out->i = pc_gql_arg_int(args, "id", &id) ? id * 10 : -1;
        return true;
    }
    if (!strcmp(path, "greet"))
    {
        const char *who = "?";
        pc_gql_arg_str(args, "name", &who);
        static char b[64];
        snprintf(b, sizeof(b), "hi %s", who);
        out->type = pc_gql_type::PC_GQL_STR;
        out->s = b;
        return true;
    }
    return false; // -> JSON null
}

static void graphql_bench_task(void *)
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
        Serial.printf("DB ==== graphql device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile int32_t sink = 0;
        DBENCH_OP("pc_graphql_execute flat", 20000,
                  sink += (int32_t)pc_graphql_execute(q_flat, sizeof(q_flat) - 1, gql_resolver, resp, sizeof(resp)));
        DBENCH_OP("pc_graphql_execute nested", 20000,
                  sink +=
                  (int32_t)pc_graphql_execute(q_nested, sizeof(q_nested) - 1, gql_resolver, resp, sizeof(resp)));
        DBENCH_OP("pc_graphql_execute args", 20000,
                  sink += (int32_t)pc_graphql_execute(q_args, sizeof(q_args) - 1, gql_resolver, resp, sizeof(resp)));
        DBENCH_OP("pc_graphql_execute strarg", 20000,
                  sink +=
                  (int32_t)pc_graphql_execute(q_strarg, sizeof(q_strarg) - 1, gql_resolver, resp, sizeof(resp)));
        DBENCH_OP("pc_graphql_execute parse_err", 20000,
                  sink += (int32_t)pc_graphql_execute(q_err, sizeof(q_err) - 1, gql_resolver, resp, sizeof(resp)));
        DBENCH_BULK("pc_graphql_execute bulk", 20000, sizeof(q_bulk) - 1,
                    sink += (int32_t)pc_graphql_execute(q_bulk, sizeof(q_bulk) - 1, gql_resolver, resp, sizeof(resp)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: graphql device microbench");
    xTaskCreatePinnedToCore(graphql_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
