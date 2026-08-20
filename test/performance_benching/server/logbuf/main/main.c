// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the rotating log ring (server/logbuf): a fixed-RAM ring of
// the last PROTOCORE_LOG_LINES lines with a severity trap, all pure (no heap, no ESP32 dependency). Four
// deterministic operations are benched:
//   - Logbuf.put      : the append hot path (snprintf of `<L> msg` into the next ring slot),
//   - Logbuf.put+trap : the same append with a severity trap armed and firing (trap-dispatch cost),
//   - Logbuf.at       : indexed oldest-first retrieval (head+i modulo ring size),
//   - Logbuf.dump     : dumping the whole ring newline-separated into a caller buffer (bulk memcpy).
// The trap callback is a tiny no-op here (it only bumps a volatile sink) - it stands in for the real
// SNMP-trap / webhook forwarder the production caller would install, so no network I/O is ever done;
// like every performance_benching/device/ sketch this rig touches no peripherals or transport. Nothing in logbuf is
// out of scope: the whole service is pure, so every call below runs the real production code path.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/logbuf -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/core/logbuf/logbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Stands in for the production severity-trap sink (an SNMP trap / webhook forwarder). Kept a pure
// no-op - it only bumps a volatile counter so the compiler cannot elide the trap dispatch and no
// network I/O is ever performed on this peripheral-less rig.
static volatile uint32_t g_trap_sink = 0;
static void logbuf_trap_noop(uint8_t level, const char *line)
{
    (void)level;
    (void)line;
    g_trap_sink++;
}

/** @brief Store `<level> msg` in the next ring slot. */
static void log_put(uint8_t level, const char *msg)
{
    LogbufV.line.level = level;
    LogbufV.line.msg = msg;
    Logbuf.put(protocore_logbuf_span());
}

/** @brief The line at oldest-first index @p i. */
static const char *log_at(uint16_t i)
{
    LogbufV.read.i = i;
    Logbuf.at(protocore_logbuf_span());
    return LogbufV.text;
}

/** @brief Write every held line into @p out, oldest-first, newline-separated; the characters written. */
static int log_dump(char *out, size_t cap)
{
    LogbufV.read.out = out;
    LogbufV.read.cap = cap;
    Logbuf.dump(protocore_logbuf_span());
    return LogbufV.n;
}

/** @brief Fire @p cb for a line logged at @p threshold or above; 0xFF disables. */
static void log_set_trap(uint8_t threshold, protocore_log_trap_fn cb)
{
    LogbufV.trap.threshold = threshold;
    LogbufV.trap.cb = cb;
    Logbuf.set_trap(protocore_logbuf_span());
}

// Fill the ring to capacity with realistic, spec-conformant log lines (overflow past PROTOCORE_LOG_LINES so
// rotation runs and count settles at the ring size). Untimed setup for the read-side benches below.
static void fill_ring(void)
{
    Logbuf.reset(protocore_logbuf_span());
    char msg[64];
    for (int i = 0; i < PROTOCORE_LOG_LINES + 8; i++)
    {
        snprintf(msg, sizeof(msg), "client 192.168.1.%d auth failed (attempt %d)", 40 + (i & 7), i);
        log_put(PROTOCORE_LOG_INFO, msg);
    }
}

void dbench_run(void)
{
    static char dumpbuf[PROTOCORE_LOG_LINES * PROTOCORE_LOG_LINE_LEN];
    static const char *kMsg = "client 192.168.1.42 auth failed (attempt 7)";

    for (;;)
    {
        DBENCH_BANNER("logbuf");

        // Read-side benches first, on a freshly filled full ring (append below mutates it).
        fill_ring();
        log_set_trap(0xFF, NULL); // trap disabled for the read-side ops
        int dumped = log_dump(dumpbuf, sizeof(dumpbuf));

        volatile size_t sink = 0;
        volatile uintptr_t psink = 0;

        // Dump the whole ring (oldest-first, newline-joined) into a caller buffer - bulk memcpy path.
        DBENCH_BULK("Logbuf.dump full ring", 20000, (size_t)(dumped > 0 ? dumped : 1),
                    sink += (size_t)log_dump(dumpbuf, sizeof(dumpbuf)));
        // Indexed oldest-first retrieval (head+i modulo ring size).
        DBENCH_OP("Logbuf.at fetch", 200000, psink += (uintptr_t)log_at(7));

        // Append hot path with the trap disarmed (snprintf of `<L> msg` into the next slot).
        DBENCH_OP("Logbuf.put append (no trap)", 50000, log_put(PROTOCORE_LOG_INFO, kMsg));
        // Same append with a WARN trap armed and an ERROR line firing it every iteration.
        log_set_trap(PROTOCORE_LOG_WARN, logbuf_trap_noop);
        DBENCH_OP("Logbuf.put append (trap fires)", 50000, log_put(PROTOCORE_LOG_ERROR, kMsg));

        (void)sink;
        (void)psink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("logbuf")
