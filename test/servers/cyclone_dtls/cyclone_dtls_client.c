// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// CycloneSSL DTLS 1.3 interop client (host Linux). Test tooling only, NOT part of the library; it
// builds against Oryx CycloneSSL (GPLv2), fetched at run time by run_interop.sh - never vendored here.
//
// Completes a real DTLS 1.3 handshake (TLS_AES_128_GCM_SHA256 / X25519 / Ed25519) against the
// library's pc_dtls_conn UDP test server, then does one application-data round trip: sends
// "hello cyclone!" and prints the echo. Exit 0 on a byte-exact round trip, non-zero otherwise.
//
//   usage: cyclone_dtls_client <host> <port> [--rpk]
//
// --rpk registers a RawPublicKey verify callback, which makes CycloneSSL advertise the
// server_certificate_type = RawPublicKey extension (RFC 7250) in the ClientHello.

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "core/crypto.h"
#include "hash/sha256.h"
#include "rng/hmac_drbg.h"
#include "tls.h"

// The connected UDP socket. connect(2) fixes the peer so plain send()/recv() work.
static int g_sock = -1;

// --- CycloneSSL socket callbacks (datagram backed) ---------------------------------------------

static error_t socket_send_cb(TlsSocketHandle handle, const void *data, size_t length, size_t *written, uint_t flags)
{
    int fd = (int)(intptr_t)handle;
    (void)flags;
    ssize_t n = send(fd, data, length, 0);
    if (n < 0)
    {
        *written = 0;
        return ERROR_WRITE_FAILED;
    }
    *written = (size_t)n;
    return NO_ERROR;
}

static error_t socket_recv_cb(TlsSocketHandle handle, void *data, size_t size, size_t *received, uint_t flags)
{
    int fd = (int)(intptr_t)handle;
    (void)flags;
    ssize_t n = recv(fd, data, size, 0);
    if (n < 0)
    {
        *received = 0;
        // SO_RCVTIMEO fired: report a timeout so the DTLS layer can retransmit its flight.
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return ERROR_TIMEOUT;
        }
        return ERROR_READ_FAILED;
    }
    *received = (size_t)n;
    return NO_ERROR;
}

// Accept any server certificate: this is an interop test, not a trust decision. The DTLS
// CertificateVerify signature is still checked by CycloneSSL, so the peer is still authenticated.
static error_t cert_verify_cb(TlsContext *context, const X509CertInfo *certInfo, uint_t pathLen, void *param)
{
    (void)context;
    (void)certInfo;
    (void)pathLen;
    (void)param;
    fprintf(stderr, "[client] server presented an X.509 certificate (verify bypassed)\n");
    return NO_ERROR;
}

// Accept any raw public key (RFC 7250). Registering this also switches on the ServerCertType ext.
// This callback fires only when the peer actually sent a RawPublicKey (SubjectPublicKeyInfo).
static error_t rpk_verify_cb(TlsContext *context, const uint8_t *rawPublicKey, size_t rawPublicKeyLen)
{
    (void)context;
    (void)rawPublicKey;
    fprintf(stderr, "[client] server presented a RawPublicKey (RFC 7250), %zu bytes SPKI\n", rawPublicKeyLen);
    return NO_ERROR;
}

static void seed_prng(HmacDrbgContext *drbg)
{
    uint8_t seed[48];
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(seed, 1, sizeof seed, f) != sizeof seed)
    {
        fprintf(stderr, "cannot read /dev/urandom\n");
        _exit(2);
    }
    fclose(f);
    if (hmacDrbgInit(drbg, SHA256_HASH_ALGO) != NO_ERROR || hmacDrbgSeed(drbg, seed, sizeof seed) != NO_ERROR)
    {
        fprintf(stderr, "PRNG seed failed\n");
        _exit(2);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <host> <port> [--rpk]\n", argv[0]);
        return 2;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    int want_rpk = (argc > 3 && strcmp(argv[3], "--rpk") == 0);

    // --- connected UDP socket ---
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0)
    {
        perror("socket");
        return 2;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1)
    {
        fprintf(stderr, "bad host %s\n", host);
        return 2;
    }
    if (connect(g_sock, (struct sockaddr *)&sa, sizeof sa) < 0)
    {
        perror("connect");
        return 2;
    }
    // Bounded blocking recv so ERROR_TIMEOUT drives DTLS retransmission (DTLS_INIT_TIMEOUT=1000ms).
    struct timeval tv = {0, 200 * 1000};
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    // --- PRNG (required by CycloneSSL) ---
    HmacDrbgContext drbg;
    seed_prng(&drbg);

    // --- TLS context ---
    TlsContext *tls = tlsInit();
    if (tls == NULL)
    {
        fprintf(stderr, "tlsInit failed\n");
        return 2;
    }

    error_t e;
    e = tlsSetSocketCallbacks(tls, socket_send_cb, socket_recv_cb, (TlsSocketHandle)(intptr_t)g_sock);
    if (e)
    {
        fprintf(stderr, "tlsSetSocketCallbacks: %d\n", e);
        return 2;
    }

    e = tlsSetTransportProtocol(tls, TLS_TRANSPORT_PROTOCOL_DATAGRAM);
    if (e)
    {
        fprintf(stderr, "tlsSetTransportProtocol: %d\n", e);
        return 2;
    }

    // Pin DTLS 1.3 (TLS 1.3 codepoint; DATAGRAM transport makes it DTLS on the wire).
    e = tlsSetVersion(tls, TLS_VERSION_1_3, TLS_VERSION_1_3);
    if (e)
    {
        fprintf(stderr, "tlsSetVersion: %d\n", e);
        return 2;
    }

    e = tlsSetPrng(tls, HMAC_DRBG_PRNG_ALGO, &drbg);
    if (e)
    {
        fprintf(stderr, "tlsSetPrng: %d\n", e);
        return 2;
    }

    // Overall handshake/read deadline so a dead peer does not hang forever.
    tlsSetTimeout(tls, 20000);

    // Disable chain verification (accept the throwaway self-signed Ed25519 cert).
    e = tlsSetCertificateVerifyCallback(tls, cert_verify_cb, NULL);
    if (e)
    {
        fprintf(stderr, "tlsSetCertificateVerifyCallback: %d\n", e);
        return 2;
    }

    if (want_rpk)
    {
        e = tlsSetRpkVerifyCallback(tls, rpk_verify_cb);
        if (e)
        {
            fprintf(stderr, "tlsSetRpkVerifyCallback: %d\n", e);
            return 2;
        }
        fprintf(stderr, "[client] requesting RawPublicKey server certificate (RFC 7250)\n");
    }

    // --- handshake ---
    e = tlsConnect(tls);
    if (e)
    {
        fprintf(stderr, "tlsConnect FAILED: error=%d\n", e);
        return 1;
    }
    fprintf(stderr, "[client] DTLS 1.3 handshake OK\n");
#if (TLS_RAW_PUBLIC_KEY_SUPPORT == ENABLED)
    fprintf(stderr, "[client] negotiated peer cert format = %s\n",
            tls->peerCertFormat == TLS_CERT_FORMAT_RAW_PUBLIC_KEY ? "RawPublicKey (RFC 7250)" : "X.509");
#endif

    // --- application-data round trip ---
    static const char msg[] = "hello cyclone!\n";
    size_t written = 0;
    e = tlsWrite(tls, msg, strlen(msg), &written, 0);
    if (e)
    {
        fprintf(stderr, "tlsWrite FAILED: error=%d\n", e);
        return 1;
    }

    char buf[256];
    size_t received = 0;
    e = tlsRead(tls, buf, sizeof buf - 1, &received, 0);
    if (e)
    {
        fprintf(stderr, "tlsRead FAILED: error=%d\n", e);
        return 1;
    }
    buf[received] = '\0';
    printf("ECHO(%zu): %s", received, buf);
    fflush(stdout);

    int ok = (received == strlen(msg) && memcmp(buf, msg, received) == 0);

    tlsShutdown(tls);
    tlsFree(tls);
    close(g_sock);

    if (!ok)
    {
        fprintf(stderr, "[client] echo MISMATCH\n");
        return 1;
    }
    fprintf(stderr, "[client] byte-exact echo OK\n");
    return 0;
}
