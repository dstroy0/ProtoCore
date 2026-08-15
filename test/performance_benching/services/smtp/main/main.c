// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SMTP client (services/net/smtp): the full smtp_run() dialogue
// end to end (greeting / EHLO / MAIL FROM / RCPT TO / DATA / message-build+dot-stuff / QUIT), driven
// over a scripted in-memory transport (canned server replies, sink send). Pure - no lwIP, no TLS;
// this exercises the reply parser plus the message builder together.
//
// Build/flash:  idf.py -C test/performance_benching/smtp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/smtp/smtp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Scripted transport: recv() hands back canned server replies in sequence; send() is a sink.
typedef struct Script
{
    const char *const *replies;
    size_t count;
    size_t idx;
} Script;
static int scr_send(void *, const uint8_t *, size_t len)
{
    return (int)len;
}
static int scr_recv(void *ctx, uint8_t *buf, size_t cap)
{
    Script *s = (Script *)ctx;
    if (s->idx >= s->count)
    {
        return -1;
    }
    const char *r = s->replies[s->idx++];
    size_t n = strlen(r);
    if (n > cap)
    {
        n = cap;
    }
    memcpy(buf, r, n);
    return (int)n;
}

void dbench_run(void)
{
    const char *const HAPPY[] = {
        "220 mail.example.com ESMTP ready\r\n",
        "250-mail.example.com Hello [10.0.0.9]\r\n250 AUTH LOGIN\r\n",
        "250 2.1.0 Sender OK\r\n",
        "250 2.1.5 Recipient OK\r\n",
        "354 End data with <CR><LF>.<CR><LF>\r\n",
        "250 2.0.0 Ok: queued as ABC123\r\n",
        "221 2.0.0 Bye\r\n",
    };
    const SmtpConfig cfg = {"mail.example.com", 25, SMTP_PLAIN, NULL, NULL, "rig@pc", "esp32"};
    const char *body = "temperature 84C over threshold\nheap low\n";
    const SmtpMessage msg = {"ops@pc", "pc rig alert", body};

    for (;;)
    {
        DBENCH_BANNER("smtp");
        volatile int sink = 0;
        Script sc;
        sc.replies = HAPPY;
        sc.count = 7;
        DBENCH_OP("smtp_run full dialogue", 50000, {
            sc.idx = 0; // rewind the scripted server for each run
            sink += (int)smtp_run(&cfg, &msg, scr_send, scr_recv, NULL, &sc);
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("smtp")
