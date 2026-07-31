// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the byte relay (services/net/relay): pc_relay_step() pumps bytes
// between two ends (client <-> origin) through the per-direction carry buffers - the per-poll hot op
// of a TCP proxy. Driven here over in-memory mock ends (recv always supplies a chunk, send always
// accepts), so it measures the pure relay bookkeeping + copy cost; the real sockets are elsewhere.
//
// Build/flash:  pio run -d performance_benching/device/relay -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/relay/relay.h"
#include <Arduino.h>
#include <string.h>

// A mock end: recv fills the buffer (never EOF, so the relay keeps pumping), send is a sink.
static int mock_recv(void *, uint8_t *buf, size_t cap)
{
    size_t n = cap > 256 ? 256 : cap;
    memset(buf, 0xA5, n);
    return (int)n;
}
static int mock_send(void *, const uint8_t *, size_t len)
{
    return (int)len;
}
static void mock_shutdown(void *)
{
}

static void relay_bench_task(void *)
{
    static pc_relay r;
    pc_relay_end client{mock_recv, mock_send, mock_shutdown, nullptr};
    pc_relay_end origin{mock_recv, mock_send, mock_shutdown, nullptr};

    for (;;)
    {
        Serial.printf("DB ==== relay device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        pc_relay_init(&r, &client, &origin);
        DBENCH_OP("pc_relay_step (pump both dirs)", 200000, sink += (int)pc_relay_step(&r));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: relay device microbench");
    xTaskCreatePinnedToCore(relay_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
