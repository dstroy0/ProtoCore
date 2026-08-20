// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SMB/NTLM codec (network_drivers/application/smb): the NTLMv2 authentication
// crypto (NT hash, NTOWFv2, the NTLMv2 response) and the NTLMSSP / SMB2 message builders. Pure
// crypto + framing (MD4 / MD5 / HMAC-MD5); the TCP socket is out of scope.
//
// Build/flash:  idf.py -C test/performance_benching/smb -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/application/smb/ntlm/ntlm.h"
#include "network_drivers/application/smb/ntlmssp/ntlmssp.h"
#include "network_drivers/application/smb/smb2/smb2.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t smb2_work[16]; // the borrow an entry takes; Smb2 never reads it

static uint8_t ntlm_work[16]; // the borrow an entry takes; Ntlm never reads it

static uint8_t ntlmssp_work[16]; // the borrow an entry takes; Ntlmssp never reads it

void dbench_run(void)
{
    static const uint8_t server_chal[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    static const uint8_t client_chal[8] = {0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
    static const uint8_t timestamp[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    static const uint8_t target_info[16] = {0x02, 0x00, 0x0C, 0x00, 'D', 0, 'O', 0, 'M', 0, 'A', 0, 'I', 0, 'N', 0};
    static const uint8_t file_id[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    uint8_t nt_hash[16], owf[16];
    NtlmV.nt_hash_args.password = "Passw0rd!";
    NtlmV.nt_hash_args.nt_hash = nt_hash;
    NtlmV.nt_hash(ntlm_work);
    NtlmV.ntowfv2_args.nt_hash = nt_hash;
    NtlmV.ntowfv2_args.user = "user";
    NtlmV.ntowfv2_args.domain = "DOMAIN";
    NtlmV.ntowfv2_args.owf = owf;
    Ntlm.ntowfv2(ntlm_work);

    for (;;)
    {
        DBENCH_BANNER("smb");
        volatile size_t sink = 0;
        uint8_t h[16];
        DBENCH_OP("protocore_ntlm_nt_hash (MD4)", 100000, {
            NtlmV.nt_hash_args.password = "Passw0rd!";
            NtlmV.nt_hash_args.nt_hash = h;
            NtlmV.nt_hash(ntlm_work);
            sink += h[0];
        });
        DBENCH_OP("protocore_ntlm_ntowfv2 (HMAC-MD5)", 100000, {
            NtlmV.ntowfv2_args.nt_hash = nt_hash;
            NtlmV.ntowfv2_args.user = "user";
            NtlmV.ntowfv2_args.domain = "DOMAIN";
            NtlmV.ntowfv2_args.owf = h;
            Ntlm.ntowfv2(ntlm_work);
            sink += h[0];
        });
        static uint8_t resp[256];
        uint8_t skey[16];
        DBENCH_OP("protocore_ntlm_v2_response", 50000,
                  sink += protocore_ntlm_v2_response(owf, server_chal, client_chal, timestamp, target_info,
                                                     sizeof(target_info), resp, sizeof(resp), skey));
        static uint8_t buf[256];
        DBENCH_OP("protocore_ntlmssp_build_negotiate", 200000,
                  sink += protocore_ntlmssp_build_negotiate(buf, sizeof(buf), 0xE2088297u));
        DBENCH_OP("protocore_smb2_build_close", 200000,
                  sink += protocore_smb2_build_close(buf, sizeof(buf), 5, 0x1122334455667788ull, 0x99, file_id));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("smb")
