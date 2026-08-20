// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// DTLS 1.3 real-peer interop harness: a tiny UDP server that wraps the library's transport-neutral
// protocore_dtls_conn state machine, so it can be driven by a reference DTLS 1.3 client (wolfSSL). It is a
// Linux test driver, NOT part of the library. See README.md for the wolfSSL build + run steps.
//
//   usage: protocore_dtls_interop_server <udp-port> <cert.der> <ed25519-seed-32.bin>
//
// It completes the DTLS 1.3 handshake against the peer, then decrypts inbound application records
// (epoch 3) and echoes them back, printing "INTEROP OK" once a full round trip has happened.

#include "network_drivers/presentation/security/dtls/dtls_conn/dtls_conn.h"
#include "network_drivers/presentation/security/dtls/dtls_record/dtls_record.h"
#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static uint8_t dtls_server_work[16]; // the borrow an entry takes; DtlsServer never reads it

static size_t read_file(const char *path, uint8_t *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static void rand_bytes(uint8_t *p, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(p, 1, n, f) != n)
    {
        exit(2);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s <port> <cert.der> <seed32.bin>\n", argv[0]);
        return 2;
    }
    uint16_t port = (uint16_t)atoi(argv[1]);
    static uint8_t cert[4096];
    size_t cert_len = read_file(argv[2], cert, sizeof cert);
    uint8_t seed[32];
    read_file(argv[3], seed, 32);
    uint8_t eph[32], srand[32]; // fresh per connection, from the CSPRNG
    rand_bytes(eph, 32);
    rand_bytes(srand, 32);

    static const uint8_t cookie_key[32] = {0}; // fixed HRR cookie secret is fine for a one-shot test peer
    DtlsServerConfig cfg;
    cfg.cert_der = cert;
    cfg.cert_len = cert_len;
    cfg.ed25519_seed = seed;
    cfg.ephemeral_priv = eph;
    cfg.server_random = srand;
    cfg.cookie_key = cookie_key;
    DtlsConn conn;
    proto_bool inited =
        PROTO_FALSE; // the HRR cookie binds the peer address, so init once the first datagram reveals it

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); // don't wedge on a just-exited prior run
    sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(port);
    if (bind(fd, (sockaddr *)&a, sizeof a) < 0)
    {
        perror("bind");
        return 2;
    }
    fprintf(stderr, "listening udp/%u cert_len=%zu\n", port, cert_len);

    uint8_t dgram[8192], out[8192];
    int pre_flights =
        0; // server flights sent before establishment; >= 2 means a HelloRetryRequest preceded ServerHello
    sockaddr_in peer;
    socklen_t plen;
    for (;;)
    {
        plen = sizeof peer;
        ssize_t n = recvfrom(fd, dgram, sizeof dgram, 0, (sockaddr *)&peer, &plen);
        if (n <= 0)
        {
            continue;
        }
        if (!inited)
        {
            // Bind the HRR cookie to this peer's IPv4 address + port.
            uint8_t paddr[6];
            memcpy(paddr, &peer.sin_addr.s_addr, 4);
            memcpy(paddr + 4, &peer.sin_port, 2);
            DtlsServerV.init_args.c = &conn;
            DtlsServerV.init_args.cfg = &cfg;
            DtlsServerV.init_args.peer_addr = paddr;
            DtlsServerV.init_args.peer_addr_len = sizeof paddr;
            DtlsServer.init(dtls_server_work);
            inited = PROTO_TRUE;
        }
        DtlsServerV.established_args.c = &conn;
        DtlsServer.established(dtls_server_work);
        if (!DtlsServerV.ok)
        {
            DtlsServerV.process_args.c = &conn;
            DtlsServerV.process_args.dgram = dgram;
            DtlsServerV.process_args.len = (size_t)n;
            DtlsServerV.process_args.out = out;
            DtlsServerV.process_args.out_cap = sizeof out;
            DtlsServer.process(dtls_server_work);
            int r = DtlsServerV.n;
            if (r < 0)
            {
                DtlsServerV.alert_args.c = &conn;
                DtlsServerV.alert(dtls_server_work);
                fprintf(stderr, "HANDSHAKE FAIL alert=%u\n", DtlsServerV.value);
                return 1;
            }
            if (r > 0)
            {
                sendto(fd, out, (size_t)r, 0, (sockaddr *)&peer, plen);
            }
            DtlsServerV.established_args.c = &conn;
            DtlsServer.established(dtls_server_work);
            if (DtlsServerV.ok)
            {
                uint8_t cid[PROTOCORE_DTLS_CID_MAX];
                DtlsServerV.local_cid_args.c = &conn;
                DtlsServerV.local_cid_args.out = cid;
                DtlsServerV.local_cid(dtls_server_work);
                size_t cidlen = DtlsServerV.n;
                fprintf(stderr, "HANDSHAKE OK%s%s", pre_flights >= 2 ? " (via HelloRetryRequest)" : "",
                        cidlen ? " (with connection ID, " : "\n");
                if (cidlen)
                {
                    fprintf(stderr, "%zu bytes)\n", cidlen);
                }
            }
            else if (r > 0)
            {
                pre_flights++;
            }
        }
        else
        {
            // epoch-3 application data: decrypt and echo back, exercising the same seal/open helpers a
            // CoAP-over-DTLS front-end uses (DtlsServer.open_app / DtlsServer.seal_app - a shared epoch-3
            // send sequence, so the echo never collides with the handshake-completion ACK).
            uint8_t inner[8192];
            size_t plen_in = 0;
            DtlsServerV.open_app_args.c = &conn;
            DtlsServerV.open_app_args.rec = dgram;
            DtlsServerV.open_app_args.rec_len = (size_t)n;
            DtlsServerV.open_app_args.out = inner;
            DtlsServerV.open_app_args.out_cap = sizeof inner;
            DtlsServerV.open_app_args.out_len = &plen_in;
            DtlsServer.open_app(dtls_server_work);
            if (DtlsServerV.ok)
            {
                fprintf(stderr, "APPDATA RX %zu bytes: %.*s\n", plen_in, (int)plen_in, inner);
                uint8_t rec[8192];
                DtlsServerV.seal_app_args.c = &conn;
                DtlsServerV.seal_app_args.data = inner;
                DtlsServerV.seal_app_args.len = plen_in;
                DtlsServerV.seal_app_args.out = rec;
                DtlsServerV.seal_app_args.out_cap = sizeof rec;
                DtlsServer.seal_app(dtls_server_work);
                size_t rn = DtlsServerV.n;
                if (rn)
                {
                    sendto(fd, rec, rn, 0, (sockaddr *)&peer, plen);
                }
                fprintf(stderr, "APPDATA echoed %zu bytes; INTEROP OK\n", plen_in);
                fflush(stderr);
            }
        }
    }
}
