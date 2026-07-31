// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Real-network HTTP/3 interop harness (Linux / POSIX). Runs the library's quic_server as an actual
// networked HTTP/3 server over a UDP socket, so a third-party client (curl --http3, ngtcp2, quiche)
// can complete a full QUIC + TLS 1.3 + HTTP/3 exchange against the real code. It is NOT firmware and
// NOT a unit test: it drives quic_server through the same host seam the unit tests use
// (quic_server_ingest + the output sink), wired to a real socket. See tools/interop/README.md.
//
// On ESP32 the same quic_server binds UDP through pc_udp; this harness is the Linux stand-in that
// lets a real client validate the wire without flashing a board.

#include "network_drivers/presentation/http/http3/quic_server.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// Monotonic milliseconds for the server's idle clock (quic_server stays platform-agnostic).
static uint32_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int g_sock;

// quic_server hands each outbound datagram here with the peer to reply to; send it on the socket.
static void out_sink(void *, const uint8_t *dg, size_t len, const char *ip, uint16_t port)
{
    struct sockaddr_in d;
    memset(&d, 0, sizeof d);
    d.sin_family = AF_INET;
    d.sin_port = htons(port);
    inet_pton(AF_INET, ip, &d.sin_addr);
    sendto(g_sock, dg, len, 0, (struct sockaddr *)&d, sizeof d);
}

// A completed HTTP/3 request: answer 200 (HEAD would need the body suppressed, which the real
// PC path does; this bare harness always sends a body).
static void on_request(void *, uint32_t cid, uint64_t sid, const char *method, const char *path, const char *authority,
                       const uint8_t *, size_t body_len)
{
    fprintf(stderr, "[h3] %s %s authority=%s body=%zu\n", method, path, authority ? authority : "", body_len);
    const char *b = "hello from ProtoCore quic_server\n";
    quic_server_respond(cid, sid, 200, "text/plain", (const uint8_t *)b, strlen(b));
}

static void rng(uint8_t *out, size_t n)
{
    static int fd = -1;
    if (fd < 0)
    {
        fd = open("/dev/urandom", O_RDONLY);
    }
    if (read(fd, out, n) != (ssize_t)n)
    { /* best effort */
    }
}

static size_t load(const char *path, uint8_t *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        perror(path);
        exit(1);
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : 4433;
    const char *cert_path = argc > 2 ? argv[2] : "cert.der";
    const char *seed_path = argc > 3 ? argv[3] : "seed.bin";

    static uint8_t cert[4096];
    size_t clen = load(cert_path, cert, sizeof cert);
    uint8_t seed[32];
    load(seed_path, seed, sizeof seed);

    QuicServerConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.cert_der = cert;
    cfg.cert_len = clen;
    memcpy(cfg.ed25519_seed, seed, sizeof cfg.ed25519_seed);
    cfg.rng = rng;

    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(g_sock, (struct sockaddr *)&a, sizeof a) < 0)
    {
        perror("bind");
        return 1;
    }
    // A short receive timeout so the loop polls on a timer even when no datagram arrives - this is
    // what drives loss recovery (the PTO) on schedule, exactly as the ESP32 worker loop does.
    struct timeval rcv = {0, 50000}; // 50 ms
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof rcv);

    quic_server_set_out_sink(out_sink, NULL);
    if (!quic_server_begin(port, &cfg, on_request, NULL))
    {
        fprintf(stderr, "quic_server_begin failed\n");
        return 1;
    }
    fprintf(stderr, "[h3] HTTP/3 listening on UDP %u (Ed25519 cert %zuB)\n", port, clen);

    uint8_t buf[2048];
    for (;;)
    {
        struct sockaddr_in src;
        socklen_t sl = sizeof src;
        ssize_t n = recvfrom(g_sock, buf, sizeof buf, 0, (struct sockaddr *)&src, &sl);
        if (n > 0)
        {
            char ip[16];
            inet_ntop(AF_INET, &src.sin_addr, ip, sizeof ip);
            quic_server_ingest(buf, (size_t)n, ip, ntohs(src.sin_port));
        }
        quic_server_poll(monotonic_ms()); // always poll (drives PTO retransmission on schedule)
    }
}
