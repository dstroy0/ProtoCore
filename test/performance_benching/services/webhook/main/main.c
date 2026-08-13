// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the webhook client (services/net/webhook): the IFTTT Maker URL
// builder and the JSON payload builder. Pure string logic; the HTTPS POST (protocore_webhook_post /
// protocore_ifttt_trigger) is out of scope.
//
// Build/flash:  idf.py -C test/performance_benching/webhook -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/webhook/webhook.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("webhook");
        volatile int sink = 0;
        static char out[256];
        DBENCH_OP("protocore_ifttt_url", 200000,
                  sink += protocore_ifttt_url("temp_alert", "bXlrZXktMTIzNDU", out, sizeof(out)));
        DBENCH_OP("protocore_ifttt_payload", 200000,
                  sink += protocore_ifttt_payload("84.0", "threshold", "rig-1", out, sizeof(out)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("webhook")
