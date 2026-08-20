// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Redis RESP2/RESP3 codec (services/iot/redis_resp):
// Resp.encode_command (the device builds an outbound command) and Resp.parse_reply (the
// device decodes a server reply - the untrusted-input hot op). Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/redis_resp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/redis_resp/redis_resp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t redis_resp_work[16]; // the borrow an entry takes; Resp never reads it

/** @brief Encode @p argc bulk strings as one command into @p out; the octets written. */
static size_t resp_encode(char *out, size_t cap, const char *const *argv, size_t argc)
{
    Resp.out.buf = out;
    Resp.out.cap = cap;
    Resp.command.argv = argv;
    Resp.command.argv_len = NULL;
    Resp.command.argc = argc;
    Resp.encode_command(redis_resp_work);
    return Resp.n;
}

/** @brief Decode the one value at the head of @p buf into @p r; the octets it consumed, 0 on a refusal. */
static size_t resp_take(RespReply *r, const uint8_t *buf, size_t len)
{
    Resp.wire.buf = buf;
    Resp.wire.len = len;
    Resp.parse_reply(redis_resp_work);
    *r = Resp.reply;
    return Resp.ok ? Resp.n : 0;
}

void dbench_run(void)
{
    static const char *const argv[] = {"SET", "pc:sensor:temp", "21.4"};
    // A RESP2 array reply of 3 bulk strings, as a client would receive.
    static const uint8_t reply[] = "*3\r\n$2\r\nOK\r\n$5\r\nhello\r\n$3\r\n123\r\n";

    for (;;)
    {
        DBENCH_BANNER("redis_resp");
        volatile size_t sink = 0;
        static char cmd[64];
        DBENCH_OP("Resp.encode_command (3 args)", 200000, sink += resp_encode(cmd, sizeof(cmd), argv, 3));
        const size_t rlen = sizeof(reply) - 1;
        DBENCH_OP("Resp.parse_reply (walk array)", 200000, {
            size_t off = 0;
            size_t used = 0;
            RespReply r;
            while (off < rlen && (used = resp_take(&r, reply + off, rlen - off)) != 0)
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
