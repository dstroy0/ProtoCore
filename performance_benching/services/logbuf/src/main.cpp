// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the rotating log ring (services/system/logbuf): a fixed-RAM ring of
// the last PC_LOG_LINES lines with a severity trap, all pure (no heap, no ESP32 dependency). Four
// deterministic operations are benched:
//   - pc_log()      : the append hot path (snprintf of `<L> msg` into the next ring slot),
//   - pc_log()+trap : the same append with a severity trap armed and firing (trap-dispatch cost),
//   - pc_log_at()   : indexed oldest-first retrieval (head+i modulo ring size),
//   - pc_log_dump()  : dumping the whole ring newline-separated into a caller buffer (bulk memcpy).
// The trap callback is a tiny no-op here (it only bumps a volatile sink) - it stands in for the real
// SNMP-trap / webhook forwarder the production caller would install, so no network I/O is ever done;
// like every performance_benching/device/ sketch this rig touches no peripherals or transport. Nothing in logbuf is
// out of scope: the whole service is pure, so every call below runs the real production code path.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/logbuf -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/system/logbuf/logbuf.h"
#include <Arduino.h>

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

// Fill the ring to capacity with realistic, spec-conformant log lines (overflow past PC_LOG_LINES so
// rotation runs and count settles at the ring size). Untimed setup for the read-side benches below.
static void fill_ring(void)
{
    pc_logbuf_reset();
    char msg[64];
    for (int i = 0; i < PC_LOG_LINES + 8; i++)
    {
        snprintf(msg, sizeof(msg), "client 192.168.1.%d auth failed (attempt %d)", 40 + (i & 7), i);
        pc_log(pc_log_level::PC_LOG_INFO, msg);
    }
}

static void logbuf_bench_task(void *)
{
    static char dumpbuf[PC_LOG_LINES * PC_LOG_LINE_LEN];
    static const char *kMsg = "client 192.168.1.42 auth failed (attempt 7)";

    for (;;)
    {
        Serial.printf("DB ==== logbuf device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());

        // Read-side benches first, on a freshly filled full ring (append below mutates it).
        fill_ring();
        pc_log_set_trap(0xFF, nullptr); // trap disabled for the read-side ops
        int dumped = pc_log_dump(dumpbuf, sizeof(dumpbuf));

        volatile size_t sink = 0;
        volatile uintptr_t psink = 0;

        // Dump the whole ring (oldest-first, newline-joined) into a caller buffer - bulk memcpy path.
        DBENCH_BULK("pc_log_dump full ring", 20000, (size_t)(dumped > 0 ? dumped : 1),
                    sink += (size_t)pc_log_dump(dumpbuf, sizeof(dumpbuf)));
        // Indexed oldest-first retrieval (head+i modulo ring size).
        DBENCH_OP("pc_log_at fetch", 200000, psink += (uintptr_t)pc_log_at(7));

        // Append hot path with the trap disarmed (snprintf of `<L> msg` into the next slot).
        DBENCH_OP("pc_log append (no trap)", 50000, pc_log(pc_log_level::PC_LOG_INFO, kMsg));
        // Same append with a WARN trap armed and an ERROR line firing it every iteration.
        pc_log_set_trap(pc_log_level::PC_LOG_WARN, logbuf_trap_noop);
        DBENCH_OP("pc_log append (trap fires)", 50000, pc_log(pc_log_level::PC_LOG_ERROR, kMsg));

        (void)sink;
        (void)psink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: logbuf device microbench");
    xTaskCreatePinnedToCore(logbuf_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
