// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// CCOUNT microbenchmark for the bounded-run search (mmgr/protostr).
//
// str.find dispatches on the needle's length and the shapes cost differently: one byte needs no
// funnel, no lookahead word and no extent mask; two or three settle every start position in a word
// at once through a mask chain; past that the anchor+verify body walks candidate lanes out of a
// mask, which is the only shape whose cost depends on what the haystack holds. Each is timed
// against a hit in the first word and against a miss that scans the whole run, because those are
// the two ends of what a shape costs.
//
// The haystack is 32 KB of repeated request headers, long enough that the word loops rather than the
// call dominate, with a marker planted at a known head and tail offset.
#include "device_bench.h"
#include "mmgr/protostr.h"

#include <stddef.h>
#include <stdint.h>

#define HAY_LEN 32768u
#define MARK "X-Marker-Here"
#define MARK_LEN (sizeof(MARK) - 1u)
#define HEAD_AT 96u
#define TAIL_AT (HAY_LEN - 128u)

static char hay[HAY_LEN + 1u];

static void fill_hay(void)
{
    static const char unit[] = "Host: gateway.local\r\nUser-Agent: protocore/1.0\r\nAccept: application/json\r\n";
    const size_t u = sizeof(unit) - 1u;
    for (size_t i = 0; i < HAY_LEN; i++)
    {
        hay[i] = unit[i % u];
    }
    for (size_t i = 0; i < MARK_LEN; i++)
    {
        hay[HEAD_AT + i] = MARK[i];
        hay[TAIL_AT + i] = MARK[i];
    }
    hay[HAY_LEN] = '\0';
}

void dbench_run(void)
{
    fill_hay();

    for (;;)
    {
        DBENCH_BANNER("protostr");
        volatile uintptr_t sink = 0;

        // One byte: the match mask and the terminator mask are the same shape, so whichever names
        // the lower lane settles it. ':' recurs every few bytes, '~' never occurs.
        DBENCH_OP("find 1B hit head", 2000, sink += (uintptr_t)str.find(hay, HAY_LEN, ":", sizeof(":"), PROTO_FALSE));
        DBENCH_OP("find 1B miss 32K", 2000, sink += (uintptr_t)str.find(hay, HAY_LEN, "~", sizeof("~"), PROTO_FALSE));

        // Two and three bytes: one chain of shifted masks decides every start in the word, at a cost
        // per word that does not depend on what the haystack holds. "zqx" never occurs, so the miss
        // times that chain with no candidate ever reaching a verify.
        DBENCH_OP("find 2B hit head", 2000,
                  sink += (uintptr_t)str.find(hay, HAY_LEN, "\r\n", sizeof("\r\n"), PROTO_FALSE));
        DBENCH_OP("find 2B miss 32K", 2000,
                  sink += (uintptr_t)str.find(hay, HAY_LEN, "zq", sizeof("zq"), PROTO_FALSE));
        DBENCH_OP("find 3B miss 32K", 2000,
                  sink += (uintptr_t)str.find(hay, HAY_LEN, "zqx", sizeof("zqx"), PROTO_FALSE));

        // Past three: the anchor mask, then a candidate walked out of it per set lane.
        DBENCH_OP("find 13B hit head", 2000, sink += (uintptr_t)str.find(hay, HAY_LEN, MARK, sizeof(MARK), PROTO_FALSE));
        DBENCH_OP("find 13B miss 32K", 2000,
                  sink += (uintptr_t)str.find(hay, HAY_LEN, "X-Marker-Herz", sizeof("X-Marker-Herz"), PROTO_FALSE));

        // Case folding runs the same walk on a folded syndrome.
        DBENCH_OP("find 13B hit head ci", 2000,
                  sink += (uintptr_t)str.find(hay, HAY_LEN, "x-marker-here", sizeof("x-marker-here"), PROTO_TRUE));
        DBENCH_OP("find 13B miss 32K ci", 2000,
                  sink += (uintptr_t)str.find(hay, HAY_LEN, "x-marker-herz", sizeof("x-marker-herz"), PROTO_TRUE));

        // What walking the same 32 KB costs with no matching to do, as the floor the scans sit above.
        DBENCH_OP("len 32K", 2000, sink += str.len(hay, HAY_LEN + 1u));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("protostr")
