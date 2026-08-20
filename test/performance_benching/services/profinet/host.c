// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the PROFINET DCP codec (Discovery and Configuration Protocol, the raw-L2
// device-discovery/naming layer): the 10-octet DCP header builder, the header parser, and the block walker
// (`[option][suboption][blockLength][value]` TLVs - the fuzz-target receive op, where a block-length lie must
// not over-read). Pure (no socket), links standalone. The device figure comes from the rig /bench op; this
// host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_PROFINET=1 test/performance_benching/services/profinet/host.c
//   src/services/fieldbus/profinet/profinet.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bp && /tmp/bp

#define PROTOCORE_ENABLE_PROFINET 1
#include "services/fieldbus/profinet/profinet.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

static uint8_t profinet_work[16]; // the borrow an entry takes; Profinet never reads it

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
    Profinet.dcp_block_args.option = PN_DCP_OPT_DEVICE;
    Profinet.dcp_block_args.suboption = PN_DCP_SUB_DEV_NAME_OF_STATION;
    Profinet.dcp_block_args.value = (const uint8_t *)name;
    Profinet.dcp_block_args.value_len = strlen(name);
    Profinet.dcp_block_args.out = blocks;
    Profinet.dcp_block_args.cap = sizeof(blocks);
    Profinet.dcp_block(profinet_work);
    size_t blen = Profinet.n;
    uint8_t hdr[16];
    Profinet.dcp_header_args.frame_id = PN_FRAMEID_DCP_IDENT_RES;
    Profinet.dcp_header_args.service_id = PN_DCP_SERVICE_IDENTIFY;
    Profinet.dcp_header_args.service_type = PN_DCP_TYPE_RESPONSE_SUCCESS;
    Profinet.dcp_header_args.xid = 0x12345678u;
    Profinet.dcp_header_args.response_delay = 0;
    Profinet.dcp_header_args.data_length = (uint16_t)blen;
    Profinet.dcp_header_args.out = hdr;
    Profinet.dcp_header_args.cap = sizeof(hdr);
    Profinet.dcp_header(profinet_work);
    size_t hlen = Profinet.n;

    hbench_header();

    // protocore_pn_dcp_header: build the DCP header (frameID / service / xid / delay / dataLength) - transmit op.
    {
        uint8_t buf[16];
        volatile size_t sink = 0;
        double ns = 0.0;
        Profinet.dcp_header_args.frame_id = PN_FRAMEID_DCP_IDENT_RES;
        Profinet.dcp_header_args.service_id = PN_DCP_SERVICE_IDENTIFY;
        Profinet.dcp_header_args.service_type = PN_DCP_TYPE_RESPONSE_SUCCESS;
        Profinet.dcp_header_args.xid = 0x12345678u;
        Profinet.dcp_header_args.response_delay = 0;
        Profinet.dcp_header_args.data_length = (uint16_t)blen;
        Profinet.dcp_header_args.out = buf;
        Profinet.dcp_header_args.cap = sizeof(buf);
        Profinet.dcp_header(profinet_work);
        HBENCH_NS(5000000, sink += Profinet.n, ns);
        hbench_row("profinet", "dcp_header (build)", ns, (double)hlen);
        (void)sink;
    }

    // protocore_pn_dcp_parse_header: validate + decode the header (receive op).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                PnDcpHeader out;
                Profinet.dcp_parse_header_args.frame = hdr;
                Profinet.dcp_parse_header_args.len = hlen;
                Profinet.dcp_parse_header_args.out = &out;
                Profinet.dcp_parse_header(profinet_work);
                if (Profinet.ok)
                {
                    sink += out.data_length + out.xid;
                }
            },
            ns);
        hbench_row("profinet", "dcp_parse_header", ns, (double)hlen);
        (void)sink;
    }

    // protocore_pn_dcp_walk: walk the option/suboption/blockLength TLVs after the header (the fuzz-target parser;
    // a block-length lie must not over-read past the block buffer).
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                size_t acc = 0;
                Profinet.dcp_walk_args.blocks = blocks;
                Profinet.dcp_walk_args.len = blen;
                Profinet.dcp_walk_args.cb = walk_cb;
                Profinet.dcp_walk_args.arg = &acc;
                Profinet.dcp_walk(profinet_work);
                if (Profinet.ok)
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
