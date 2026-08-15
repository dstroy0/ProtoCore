// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the IEC 61850 GOOSE publisher codec (Generic Object Oriented Substation Event,
// the raw-L2 multicast IEC 61850 uses for protection trips): the BER `IECGoosePdu` builder and the full
// Ethernet-frame wrap (dst/src/0x88B8 + 8-octet GOOSE header + PDU). GOOSE is publish-only here (no parser -
// so no fuzz-attack surface; a subscriber peer would need raw-L2 multicast HW), making this a bench-only
// codec. Pure (no socket), links standalone. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_GOOSE=1 -DPROTOCORE_ENABLE_RAWL2=1 test/performance_benching/services/goose/host.c
//   src/services/energy/goose/goose.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bg && /tmp/bg

#define PROTOCORE_ENABLE_GOOSE 1
#define PROTOCORE_ENABLE_RAWL2 1
#include "services/energy/goose/goose.h"

#include "host_bench.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    // A protection-trip GOOSE control block (two boolean dataset entries in allData: 83 01 00, 83 01 01).
    const uint8_t utc[8] = {0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a};
    const uint8_t all_data[] = {0x83, 0x01, 0x00, 0x83, 0x01, 0x01};
    protocore_goose g = {0};
    g.gocb_ref = "IED1LD0/LLN0$GO$gcb01";
    g.time_allowed_to_live = 2000;
    g.dat_set = "IED1LD0/LLN0$DataSet1";
    g.go_id = "IED1_GOOSE";
    g.t = utc;
    g.st_num = 1;
    g.sq_num = 0;
    g.simulation = false;
    g.conf_rev = 1;
    g.nds_com = false;
    g.num_entries = 2;
    g.all_data = all_data;
    g.all_data_len = sizeof(all_data);

    uint8_t pdu[256];
    size_t pdu_len = protocore_goose_pdu(&g, pdu, sizeof(pdu));

    const uint8_t dst[6] = {0x01, 0x0c, 0xcd, 0x01, 0x00, 0x01}; // IEC 61850 GOOSE multicast
    const uint8_t src[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t frame[300];
    size_t frame_len = protocore_goose_frame(dst, src, 0x0001, &g, frame, sizeof(frame));

    hbench_header();

    // protocore_goose_pdu: BER-encode the IECGoosePdu (11 control fields + the allData blob) - the publish core.
    {
        uint8_t buf[256];
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000, sink += protocore_goose_pdu(&g, buf, sizeof(buf)), ns);
        hbench_row("goose", "goose_pdu (build)", ns, (double)pdu_len);
        (void)sink;
    }

    // protocore_goose_frame: wrap the PDU in the Ethernet header + 8-octet GOOSE header (the full L2 datagram).
    {
        uint8_t buf[300];
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000, sink += protocore_goose_frame(dst, src, 0x0001, &g, buf, sizeof(buf)), ns);
        hbench_row("goose", "goose_frame (build)", ns, (double)frame_len);
        (void)sink;
    }

    return 0;
}
