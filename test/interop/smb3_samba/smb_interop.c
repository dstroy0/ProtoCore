// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SMB 3.1.1 interop driver: run the real PC smb_client engine (network_drivers/application/smb) over a live TCP socket
// against a real Samba/Windows server, proving the on-the-wire session - dialect 3.1.1, the
// preauth-integrity chain, the SP800-108-derived signing key, AES-128-CMAC message signing, and the
// NTLMSSP MIC - interoperates with a third-party implementation, not just the host mock. This is a
// MANUAL interop test (it needs a running server); see README.md for how to stand one up and build/run.
//
// Config via environment (all optional): SMB_HOST (127.0.0.1), SMB_PORT (445), SMB_USER, SMB_PASS,
// SMB_DOMAIN (""), SMB_SHARE (\\host\share UNC), SMB_PATH (file name). Exit 0 on a byte-exact round trip.

#include "network_drivers/application/smb/smb2.h"
#include "network_drivers/application/smb/smb_client.h"
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

static int sock_send(void *ctx, const uint8_t *data, size_t len)
{
    int fd = *(int *)ctx;
    size_t off = 0;
    while (off < len)
    {
        ssize_t w = send(fd, data + off, len - off, 0);
        if (w <= 0)
        {
            return -1;
        }
        off += (size_t)w;
    }
    return (int)len;
}
static int sock_recv(void *ctx, uint8_t *buf, size_t cap)
{
    int fd = *(int *)ctx;
    return (int)recv(fd, buf, cap, 0);
}
static const char *env_or(const char *k, const char *dflt)
{
    const char *v = getenv(k);
    return (v && *v) ? v : dflt;
}
static const char *algo_name(Smb2SignAlgo a)
{
    return a == SMB2_SIGN_ALGO_AES_CMAC ? "AES-CMAC" : "HMAC-SHA256";
}

int main()
{
    const char *ip = env_or("SMB_HOST", "127.0.0.1");
    int port = atoi(env_or("SMB_PORT", "445"));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &sa.sin_addr);
    if (connect(fd, (sockaddr *)&sa, sizeof(sa)) != 0)
    {
        printf("CONNECT FAILED to %s:%d\n", ip, port);
        return 2;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    SmbConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.user = env_or("SMB_USER", "pcuser");
    cfg.pass = env_or("SMB_PASS", "SmbPass123!");
    cfg.domain = env_or("SMB_DOMAIN", "");
    cfg.workstation = "PCCLIENT";
    cfg.share = env_or("SMB_SHARE", "\\\\127.0.0.1\\pcshare");
    cfg.path = env_or("SMB_PATH", "interop.txt");
    cfg.desired_access = SMB2_FILE_GENERIC_READ | SMB2_FILE_GENERIC_WRITE;
    cfg.disposition = SMB2_FILE_OVERWRITE_IF;

    SmbHandle h;
    memset(&h, 0, sizeof(h));
    SmbResult r = smb_open(&cfg, &h, sock_send, sock_recv, &fd);
    printf("smb_open -> %d\n", (int)r);
    if (r != SMB_OK)
    {
        close(fd);
        return 3;
    }
    printf("  signing_active=%d algo=%s\n", h.signing_active, algo_name(h.signing_algo));

    const char *payload = "ProtoCore SMB 3.1.1 CMAC interop payload - 0123456789";
    size_t plen = strlen(payload);
    size_t wrote = 0;
    r = smb_write(&h, 0, (const uint8_t *)payload, plen, &wrote, sock_send, sock_recv, &fd);
    printf("smb_write -> %d (%zu bytes)\n", (int)r, wrote);

    // Read back on the same open handle (a second smb_open would re-NEGOTIATE, which SMB2 forbids on an
    // existing connection). The handle was opened GENERIC_READ|GENERIC_WRITE, so a read at 0 works.
    uint8_t buf[256];
    size_t got = 0;
    r = smb_read(&h, 0, buf, sizeof(buf), &got, sock_send, sock_recv, &fd);
    bool match = (got == plen) && (memcmp(buf, payload, plen) == 0);
    printf("smb_read -> %d (%zu bytes) match=%s\n", (int)r, got, match ? "YES" : "NO");

    smb_close(&h, sock_send, sock_recv, &fd);
    close(fd);
    return match ? 0 : 4;
}
