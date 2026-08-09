// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the Redis RESP2/RESP3 codec: pc_resp_encode_command (the device builds an
// outbound command) and pc_resp_parse (the device decodes a server reply - the untrusted-input hot op).
// Both are pure (no sockets, no heap), so they link standalone. The device number comes from the rig
// /bench endpoint; this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPC_ENABLE_REDIS=1 test/performance_benching/services/redis_resp/host.c
//   src/services/iot/redis_resp/redis_resp.c -o /tmp/br && /tmp/br

#define PC_ENABLE_REDIS 1
#include "services/iot/redis_resp/redis_resp.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    const char *args[] = {"SET", "pc:sensor:temp", "21.4"};
    char cmd[128];
    size_t clen = pc_resp_encode_command(cmd, sizeof(cmd), args, NULL, 3);

    // A representative RESP2 array reply (what a HGETALL / MGET returns).
    const uint8_t reply[] = "*3\r\n$5\r\nhello\r\n:12345\r\n$-1\r\n";
    const size_t rlen = sizeof(reply) - 1;

    hbench_header();

    // encode a SET command.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(2000000, sink += pc_resp_encode_command(cmd, sizeof(cmd), args, NULL, 3), ns);
        hbench_row("redis-resp", "encode SET command", ns, (double)clen);
        (void)sink;
    }
    // parse a reply: walk every value in the array (header + 3 children), as a client would.
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            2000000,
            {
                size_t off = 0;
                size_t used = 0;
                RespReply r;
                while (off < rlen && pc_resp_parse(reply + off, rlen - off, &r, &used) && used)
                {
                    off += used;
                    sink += (int)r.type;
                }
            },
            ns);
        hbench_row("redis-resp", "parse array reply", ns, (double)rlen);
        (void)sink;
    }

    return 0;
}
