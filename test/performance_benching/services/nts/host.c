// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the NTS framing codecs (RFC 8915): the NTS-KE record builder/parser (the
// TLS-1.3 key-establishment exchange on :4460) and the NTP extension-field builder (per-packet). All pure
// framing - no TLS, no AEAD, no socket - so it links standalone; the crypto (AES-SIV-CMAC-256 + the TLS
// exporter) sits on top and is not part of these hot ops. The device figure comes from the rig /bench op;
// this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_NTS=1 test/performance_benching/services/nts/host.c
//   src/network_drivers/application/nts/nts.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bn && /tmp/bn

#define PROTOCORE_ENABLE_NTS 1
#include "network_drivers/application/nts/nts.h"

#include "host_bench.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint8_t nts_work[16]; // the borrow an entry takes; Nts never reads it

static void ke_count_cb(bool critical, uint16_t type, const uint8_t *body, size_t body_len, void *arg)
{
    (void)critical;
    (void)type;
    (void)body;
    (void)body_len;
    (*(size_t *)arg)++;
}

int main(void)
{
    uint8_t out[512];

    // A realistic NTS-KE server response: next-protocol NTPv4, the AES-SIV-CMAC-256 AEAD, one 32-byte
    // cookie, and the end-of-message record - the stream the client parses on every key establishment.
    uint8_t resp[512];
    size_t rl = 0;
    const uint8_t next_proto[2] = {0x00, NTS_NEXT_PROTO_NTPV4};
    const uint8_t aead[2] = {0x00, (uint8_t)NTS_AEAD_AES_SIV_CMAC_256};
    uint8_t cookie[32];
    for (int i = 0; i < 32; i++)
    {
        cookie[i] = (uint8_t)(i * 7 + 3);
    }
    NtsV.ke_record_args.critical = true;
    NtsV.ke_record_args.type = NTS_KE_NEXT_PROTOCOL;
    NtsV.ke_record_args.body = next_proto;
    NtsV.ke_record_args.body_len = 2;
    NtsV.ke_record_args.out = resp + rl;
    NtsV.ke_record_args.cap = sizeof(resp) - rl;
    Nts.ke_record(nts_work);
    rl += NtsV.n;
    NtsV.ke_record_args.critical = true;
    NtsV.ke_record_args.type = NTS_KE_AEAD_ALGORITHM;
    NtsV.ke_record_args.body = aead;
    NtsV.ke_record_args.body_len = 2;
    NtsV.ke_record_args.out = resp + rl;
    NtsV.ke_record_args.cap = sizeof(resp) - rl;
    Nts.ke_record(nts_work);
    rl += NtsV.n;
    NtsV.ke_record_args.critical = false;
    NtsV.ke_record_args.type = NTS_KE_COOKIE;
    NtsV.ke_record_args.body = cookie;
    NtsV.ke_record_args.body_len = sizeof(cookie);
    NtsV.ke_record_args.out = resp + rl;
    NtsV.ke_record_args.cap = sizeof(resp) - rl;
    Nts.ke_record(nts_work);
    rl += NtsV.n;
    NtsV.ke_record_args.critical = true;
    NtsV.ke_record_args.type = NTS_KE_END_OF_MESSAGE;
    NtsV.ke_record_args.body = NULL;
    NtsV.ke_record_args.body_len = 0;
    NtsV.ke_record_args.out = resp + rl;
    NtsV.ke_record_args.cap = sizeof(resp) - rl;
    Nts.ke_record(nts_work);
    rl += NtsV.n;

    uint8_t nonce[16];
    for (int i = 0; i < 16; i++)
    {
        nonce[i] = (uint8_t)(i * 11 + 1);
    }

    hbench_header();

    // Build the NTS-KE request (client hello: next-protocol + AEAD offer). Once per key establishment.
    {
        NtsV.ke_request_args.out = out;
        NtsV.ke_request_args.cap = sizeof(out);
        Nts.ke_request(nts_work);
        size_t req_len = NtsV.n;
        volatile size_t sink = 0;
        double ns = 0.0;
        NtsV.ke_request_args.out = out;
        NtsV.ke_request_args.cap = sizeof(out);
        Nts.ke_request(nts_work);
        HBENCH_NS(2000000, sink += NtsV.n, ns);
        hbench_row("nts", "ke_request (build)", ns, (double)req_len);
        (void)sink;
    }

    // Parse the NTS-KE server response stream (walk every record to EOM). The client receive hot op.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            2000000,
            {
                size_t n = 0;
                NtsV.ke_parse_args.buf = resp;
                NtsV.ke_parse_args.len = rl;
                NtsV.ke_parse_args.cb = ke_count_cb;
                NtsV.ke_parse_args.arg = &n;
                Nts.ke_parse(nts_work);
                if (NtsV.ok)
                {
                    sink += n;
                }
            },
            ns);
        hbench_row("nts", "ke_parse (server response)", ns, (double)rl);
        (void)sink;
    }

    // Build a Unique-Identifier EF (RFC 8915 5.3) - on every NTS-protected NTP request the client sends.
    {
        NtsV.ef_unique_id_args.nonce = nonce;
        NtsV.ef_unique_id_args.nonce_len = sizeof(nonce);
        NtsV.ef_unique_id_args.out = out;
        NtsV.ef_unique_id_args.cap = sizeof(out);
        Nts.ef_unique_id(nts_work);
        size_t ef_len = NtsV.n;
        volatile size_t sink = 0;
        double ns = 0.0;
        NtsV.ef_unique_id_args.nonce = nonce;
        NtsV.ef_unique_id_args.nonce_len = sizeof(nonce);
        NtsV.ef_unique_id_args.out = out;
        NtsV.ef_unique_id_args.cap = sizeof(out);
        Nts.ef_unique_id(nts_work);
        HBENCH_NS(5000000, sink += NtsV.n, ns);
        hbench_row("nts", "ef_unique_id (build)", ns, (double)ef_len);
        (void)sink;
    }

    // Build a Cookie EF - carried on every NTS-protected NTP request (one cookie spent per exchange).
    {
        NtsV.ef_cookie_args.cookie = cookie;
        NtsV.ef_cookie_args.cookie_len = sizeof(cookie);
        NtsV.ef_cookie_args.out = out;
        NtsV.ef_cookie_args.cap = sizeof(out);
        Nts.ef_cookie(nts_work);
        size_t ck_len = NtsV.n;
        volatile size_t sink = 0;
        double ns = 0.0;
        NtsV.ef_cookie_args.cookie = cookie;
        NtsV.ef_cookie_args.cookie_len = sizeof(cookie);
        NtsV.ef_cookie_args.out = out;
        NtsV.ef_cookie_args.cap = sizeof(out);
        Nts.ef_cookie(nts_work);
        HBENCH_NS(5000000, sink += NtsV.n, ns);
        hbench_row("nts", "ef_cookie (build)", ns, (double)ck_len);
        (void)sink;
    }

    return 0;
}
