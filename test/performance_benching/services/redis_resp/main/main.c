// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Redis RESP2/RESP3 codec (services/iot/redis_resp):
// protocore_resp_encode_command() (the device builds an outbound command) and protocore_resp_parse() (the
// device decodes a server reply - the untrusted-input hot op). Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/redis_resp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/redis_resp/redis_resp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static const char *argv[] = {"SET", "pc:sensor:temp", "21.4"};
    // A RESP2 array reply of 3 bulk strings, as a client would receive.
    static const uint8_t reply[] = "*3\r\n$2\r\nOK\r\n$5\r\nhello\r\n$3\r\n123\r\n";

    for (;;)
    {
        DBENCH_BANNER("redis_resp");
        volatile size_t sink = 0;
        static char cmd[64];
        DBENCH_OP("protocore_resp_encode_command (3 args)", 200000,
                  sink += protocore_resp_encode_command(cmd, sizeof(cmd), argv, NULL, 3));
        const size_t rlen = sizeof(reply) - 1;
        DBENCH_OP("protocore_resp_parse (walk array)", 200000, {
            size_t off = 0;
            size_t used = 0;
            RespReply r;
            while (off < rlen && protocore_resp_parse(reply + off, rlen - off, &r, &used) && used)
            {
                off += used;
                sink += (size_t)r.type;
            }
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("redis_resp")
