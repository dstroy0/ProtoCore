// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the TLS policy helpers (server/security/tls_policy): the version
// negotiation, the cipher-suite selection (server-pinned order over the client's offered list), and
// the AEAD classifier. Pure decision logic; no handshake.
//
// Build/flash:  idf.py -C test/performance_benching/tls_policy -t upload --upload-port COM7
#include "device_bench.h"
#include "server/security/tls_policy/tls_policy.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // TLS 1.3 + 1.2 AEAD suites (client offered, server pinned).
    static const uint16_t offered[] = {0x1301, 0x1302, 0xC02F, 0xC030, 0x009C};
    static const uint16_t pinned[] = {0x1302, 0x1301, 0xC030};

    for (;;)
    {
        DBENCH_BANNER("tls_policy");
        volatile uint32_t sink = 0;
        DBENCH_OP("protocore_tls_negotiate_version", 200000,
                  sink += protocore_tls_negotiate_version(0x0304, 0x0303, 0x0304));
        DBENCH_OP("protocore_tls_select_cipher", 200000, sink += protocore_tls_select_cipher(offered, 5, pinned, 3));
        DBENCH_OP("protocore_tls_is_aead", 200000, sink += protocore_tls_is_aead(0x1301));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("tls_policy")
