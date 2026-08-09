// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the PROFINET DCP codec (Discovery and Configuration Protocol, the raw-L2
// device-discovery/naming layer): the 10-octet DCP header builder, the header parser, and the block walker
// (`[option][suboption][blockLength][value]` TLVs - the fuzz-target receive op, where a block-length lie must
// not over-read). Pure (no socket), links standalone. The device figure comes from the rig /bench op; this
// host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPC_ENABLE_PROFINET=1 test/performance_benching/services/profinet/host.c
//   src/services/fieldbus/profinet/profinet.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bp && /tmp/bp

#define PC_ENABLE_PROFINET 1
#include "services/fieldbus/profinet/profinet.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

static void walk_cb(uint8_t option, uint8_t suboption, const uint8_t *value, size_t value_len, void *arg)
{
    (void)option;
    (void)suboption;
    (void)value;
    *(size_t *)arg += value_len;
}

int main(void)
{
    // A DCP Identify response naming the station "et200sp" (a NameOfStation block).
    const char *name = "et200sp";
    uint8_t blocks[64];
    size_t blen = pc_pn_dcp_block(PN_DCP_OPT_DEVICE, PN_DCP_SUB_DEV_NAME_OF_STATION, (const uint8_t *)name,
                                  strlen(name), blocks, sizeof(blocks));
    uint8_t hdr[16];
    size_t hlen = pc_pn_dcp_header(PN_FRAMEID_DCP_IDENT_RES, PN_DCP_SERVICE_IDENTIFY, PN_DCP_TYPE_RESPONSE_SUCCESS,
                                   0x12345678u, (uint16_t)blen, hdr, sizeof(hdr));

    hbench_header();

    // pc_pn_dcp_header: build the 10-octet DCP header (frameID / service / xid / dataLength) - transmit op.
    {
        uint8_t buf[16];
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000,
                  sink += pc_pn_dcp_header(PN_FRAMEID_DCP_IDENT_RES, PN_DCP_SERVICE_IDENTIFY,
                                           PN_DCP_TYPE_RESPONSE_SUCCESS, 0x12345678u, (uint16_t)blen, buf, sizeof(buf)),
                  ns);
        hbench_row("profinet", "dcp_header (build)", ns, (double)hlen);
        (void)sink;
    }

    // pc_pn_dcp_parse_header: validate + decode the 10-octet header (receive op).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                PnDcpHeader out;
                if (pc_pn_dcp_parse_header(hdr, hlen, &out))
                {
                    sink += out.data_length + out.xid;
                }
            },
            ns);
        hbench_row("profinet", "dcp_parse_header", ns, (double)hlen);
        (void)sink;
    }

    // pc_pn_dcp_walk: walk the option/suboption/blockLength TLVs after the header (the fuzz-target parser;
    // a block-length lie must not over-read past the block buffer).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                size_t acc = 0;
                if (pc_pn_dcp_walk(blocks, blen, walk_cb, &acc))
                {
                    sink += acc;
                }
            },
            ns);
        hbench_row("profinet", "dcp_walk (blocks)", ns, (double)blen);
        (void)sink;
    }

    return 0;
}
