// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the SMTP client (RFC 5321): the full smtp_run() dialogue end to end -
// greeting/EHLO/MAIL FROM/RCPT TO/DATA/message-build (CRLF-normalize + dot-stuff)/QUIT - driven over a
// scripted in-memory transport (canned server replies, sink send), so it is pure (no lwIP, no TLS). This
// exercises the reply parser (reply_complete, the untrusted-input hot op) + the message builder together.
// The device number comes from the rig /bench smtp_run op; this host ns/op is a RELATIVE baseline. Build:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_SMTP=1 performance_benching/services/smtp/host.cpp
//   \
//       src/services/net/smtp/smtp.cpp src/network_drivers/presentation/codec/base64/base64.cpp -o /tmp/bs && /tmp/bs

#define PC_ENABLE_SMTP 1
#include "services/net/smtp/smtp.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using clk = std::chrono::steady_clock;

template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
    {
        fn();
    }
    auto t1 = clk::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / (double)iters;
}

static void row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    printf("| %-12s | %-24s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

// Scripted transport: recv() hands back canned server replies in sequence; send() is a sink.
struct Script
{
    const char *const *replies;
    size_t count;
    size_t idx;
};
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

int main()
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
    SmtpConfig cfg = {"mail.example.com", 25, SmtpSecurity::SMTP_PLAIN, nullptr, nullptr, "rig@example.com", "esp32"};
    const char *body = "temperature 84C over threshold\nheap low\n";
    SmtpMessage msg = {"ops@example.com", "pc rig alert", body};

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // the full client dialogue: reply parse x7 + EHLO/MAIL/RCPT command build + message build/dot-stuff.
    {
        volatile int sink = 0;
        double ns = bench_ns(1000000, [&] {
            Script sc = {HAPPY, 7, 0};
            sink += (int)smtp_run(&cfg, &msg, scr_send, scr_recv, nullptr, &sc);
        });
        row("smtp", "smtp_run full dialogue", ns, (double)strlen(body));
        (void)sink;
    }

    return 0;
}
