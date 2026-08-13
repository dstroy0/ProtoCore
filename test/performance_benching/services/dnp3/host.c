// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the DNP3 (IEEE 1815) data-link frame codec: the CRC-16/DNP inner op (run
// once per 16-octet block), the zero-heap frame builder (header block + CRC'd data blocks), and the
// CRC-validating de-blocking parser. Pure (no socket), so it links standalone. The device figure comes
// from the rig /bench op; this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_DNP3=1 test/performance_benching/services/dnp3/host.c
//   src/services/energy/dnp3/dnp3.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bd && /tmp/bd

#define PROTOCORE_ENABLE_DNP3 1
#include "services/energy/dnp3/dnp3.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    // 32 octets of application data -> a header block + two CRC'd data blocks (16 + 16).
    uint8_t user[32];
    for (int i = 0; i < 32; i++)
    {
        user[i] = (uint8_t)(i * 5 + 1);
    }
    uint8_t block[DNP3_BLOCK_LEN];
    memcpy(block, user, DNP3_BLOCK_LEN);

    uint8_t frame[512];
    size_t frame_len = protocore_dnp3_build_frame(frame, sizeof(frame), 0x44, 0x0001, 0x0002, user, sizeof(user));

    hbench_header();

    // CRC-16/DNP over one 16-octet block - the per-block inner op (a frame CRCs the header + every block).
    {
        volatile uint16_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(10000000, sink ^= protocore_dnp3_crc(block, sizeof(block)), ns);
        hbench_row("dnp3", "crc16 (16-octet block)", ns, (double)DNP3_BLOCK_LEN);
        (void)sink;
    }

    // Build a full data-link frame (header block + CRC'd data blocks) from 32 octets of user data.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(2000000, sink += protocore_dnp3_build_frame(frame, sizeof(frame), 0x44, 1, 2, user, 32), ns);
        hbench_row("dnp3", "build_frame (32B user)", ns, (double)frame_len);
        (void)sink;
    }

    // Parse + CRC-validate the frame, de-blocking the user data (every block CRC checked) - the receive op.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            2000000,
            {
                Dnp3Frame f;
                uint8_t out[256];
                size_t ul = 0;
                if (protocore_dnp3_parse_frame(frame, frame_len, &f, out, sizeof(out), &ul))
                {
                    sink += ul;
                }
            },
            ns);
        hbench_row("dnp3", "parse_frame (validate CRCs)", ns, (double)frame_len);
        (void)sink;
    }

    return 0;
}
