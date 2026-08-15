// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the webhook client (services/net/webhook): the IFTTT Maker URL
// builder and the JSON payload builder. Pure string logic; the HTTPS POST (Webhook.post /
// Webhook.ifttt_trigger) is out of scope.
//
// Build/flash:  idf.py -C test/performance_benching/webhook -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/webhook/webhook.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Build the Maker target URI into @p out; the octets written. */
static int ifttt_url(char *out, size_t cap)
{
    Webhook.build.out = out;
    Webhook.build.cap = cap;
    Webhook.ifttt.event = "temp_alert";
    Webhook.ifttt.key = "bXlrZXktMTIzNDU";
    Webhook.ifttt_url(Webhook.internal);
    return Webhook.n;
}

/** @brief Build the value1/value2/value3 object into @p out; the octets written. */
static int ifttt_payload(char *out, size_t cap)
{
    Webhook.build.out = out;
    Webhook.build.cap = cap;
    Webhook.ifttt.value1 = "84.0";
    Webhook.ifttt.value2 = "threshold";
    Webhook.ifttt.value3 = "rig-1";
    Webhook.ifttt_payload(Webhook.internal);
    return Webhook.n;
}

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("webhook");
        volatile int sink = 0;
        static char out[256];
        DBENCH_OP("Webhook.ifttt_url", 200000, sink += ifttt_url(out, sizeof(out)));
        DBENCH_OP("Webhook.ifttt_payload", 200000, sink += ifttt_payload(out, sizeof(out)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("webhook")
