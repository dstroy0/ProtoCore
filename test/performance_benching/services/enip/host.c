// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the EtherNet/IP (CIP encapsulation over TCP/44818) codec: the 24-octet
// encapsulation-header builder, the header parser (command/length/session validate + data slice - the
// fuzz-target receive op), and the RegisterSession builder (the session handshake). Pure (no socket), so it
// links standalone. The device figure comes from the rig /bench op; this host ns/op + MB/s is a relative
// baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_ENIP=1 test/performance_benching/services/enip/host.c
//   src/services/fieldbus/enip/enip.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/be && /tmp/be

#define PROTOCORE_ENABLE_ENIP 1
#include "services/fieldbus/enip/enip.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    // A SendRRData frame carrying an 8-octet CIP message (a typical unconnected explicit request).
    const uint8_t cip[8] = {0x54, 0x03, 0x20, 0x02, 0x24, 0x01, 0x00, 0x00};
    EipHeader h = {0};
    h.command = EIP_CMD_SEND_RR_DATA;
    h.length = sizeof(cip);
    h.session_handle = 0x01020304;
    h.status = EIP_STATUS_SUCCESS;
    h.options = 0;
    uint8_t frame[64];
    size_t frame_len = protocore_eip_build(frame, sizeof(frame), &h, cip, sizeof(cip));

    uint8_t rs[64];
    const uint8_t ctx[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t rs_len = protocore_eip_build_register_session(rs, sizeof(rs), ctx);

    hbench_header();

    // protocore_eip_build: lay down the 24-octet encapsulation header + command data (the transmit op).
    {
        uint8_t buf[64];
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000, sink += protocore_eip_build(buf, sizeof(buf), &h, cip, sizeof(cip)), ns);
        hbench_row("enip", "protocore_eip_build (encap)", ns, (double)frame_len);
        (void)sink;
    }

    // protocore_eip_parse: validate the encapsulation header (command/length/status) + slice the command data (receive
    // op; this is the parser the fuzz attack targets - a length lie must not over-read past the 24-octet header).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                EipHeader out;
                const uint8_t *data = NULL;
                size_t dlen = 0;
                if (protocore_eip_parse(frame, frame_len, &out, &data, &dlen))
                {
                    sink += dlen + out.command;
                }
            },
            ns);
        hbench_row("enip", "protocore_eip_parse (encap)", ns, (double)frame_len);
        (void)sink;
    }

    // protocore_eip_build_register_session: the session-open handshake frame (fixed 28 octets).
    {
        uint8_t buf[64];
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000, sink += protocore_eip_build_register_session(buf, sizeof(buf), ctx), ns);
        hbench_row("enip", "register_session (build)", ns, (double)rs_len);
        (void)sink;
    }

    return 0;
}
