// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the SMTP client (RFC 5321): the full smtp_run() dialogue end to end -
// greeting/EHLO/MAIL FROM/RCPT TO/DATA/message-build (CRLF-normalize + dot-stuff)/QUIT - driven over a
// scripted in-memory transport (canned server replies, sink send), so it is pure (no lwIP, no TLS). This
// exercises the reply parser (reply_complete, the untrusted-input hot op) + the message builder together.
// The device number comes from the rig /bench smtp_run op; this host ns/op is a relative baseline. Build:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_SMTP=1 test/performance_benching/services/smtp/host.c
//   src/services/net/smtp/smtp.c src/network_drivers/presentation/codec/base64/base64.c
//   src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bs && /tmp/bs

#define PROTOCORE_ENABLE_SMTP 1
#include "services/net/smtp/smtp.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

// Scripted transport: recv() hands back canned server replies in sequence; send() is a sink.
struct Script
{
    const char *const *replies;
    size_t count;
    size_t idx;
};

static int scr_send(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    (void)data;
    return (int)len;
}

static int scr_recv(void *ctx, uint8_t *buf, size_t cap)
{
    struct Script *s = (struct Script *)ctx;
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

int main(void)
{
    // The well-formed happy-path dialogue (greeting -> multiline EHLO -> MAIL FROM -> RCPT TO -> DATA ->
    // body-ack -> QUIT). read_reply() consumes one per call, in this order.
    static const char *const HAPPY[] = {
        "220 mail.example.com ESMTP ready\r\n",
        "250-mail.example.com Hello [10.0.0.9]\r\n250 AUTH LOGIN\r\n",
        "250 2.1.0 Sender OK\r\n",
        "250 2.1.5 Recipient OK\r\n",
        "354 End data with <CR><LF>.<CR><LF>\r\n",
        "250 2.0.0 Ok: queued as ABC123\r\n",
        "221 2.0.0 Bye\r\n",
    };
    SmtpConfig cfg = {"mail.example.com", 25, SMTP_PLAIN, NULL, NULL, "rig@example.com", "esp32"};
    const char *body = "temperature 84C over threshold\nheap low\n";
    SmtpMessage msg = {"ops@example.com", "pc rig alert", body};

    hbench_header();

    // the full client dialogue: reply parse x7 + EHLO/MAIL/RCPT command build + message build/dot-stuff.
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            1000000,
            {
                struct Script sc;
                sc.replies = HAPPY;
                sc.count = 7;
                sc.idx = 0;
                sink += (int)smtp_run(&cfg, &msg, scr_send, scr_recv, NULL, &sc);
            },
            ns);
        hbench_row("smtp", "smtp_run full dialogue", ns, (double)strlen(body));
        (void)sink;
    }

    return 0;
}
