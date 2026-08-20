// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the byte relay (server/net/relay): protocore_relay_step() pumps bytes
// between two ends (client <-> origin) through the per-direction carry buffers - the per-poll hot op
// of a TCP proxy. Driven here over in-memory mock ends (recv always supplies a chunk, send always
// accepts), so it measures the pure relay bookkeeping + copy cost; the real sockets are elsewhere.
//
// Build/flash:  idf.py -C test/performance_benching/relay -t upload --upload-port COM7
#include "device_bench.h"
#include "server/net/relay/relay/relay.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t relay_work[16]; // the borrow an entry takes; Relay never reads it

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

void dbench_run(void)
{
    static protocore_relay r;
    protocore_relay_end client = {mock_recv, mock_send, mock_shutdown, NULL};
    protocore_relay_end origin = {mock_recv, mock_send, mock_shutdown, NULL};

    for (;;)
    {
        DBENCH_BANNER("relay");
        volatile int sink = 0;
        RelayV.init_args.r = &r;
        RelayV.init_args.client = &client;
        RelayV.init_args.origin = &origin;
        Relay.init(relay_work);
        RelayV.step_args.r = &r;
        // The entry call stays inside DBENCH_OP so the timed loop measures the pump, not the read.
        DBENCH_OP("Relay.step (pump both dirs)", 200000, (Relay.step(relay_work), sink += (int)RelayV.status));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("relay")
