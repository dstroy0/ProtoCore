// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the SNMP v1/v2c agent codec: pc_snmp_agent_process() takes a complete
// request datagram and produces the response datagram - BER-decode the message + PDU, walk the MIB,
// BER-encode the reply. Pure (no sockets, no heap), so it links standalone (the UDP transport symbols
// it references come from udp.c's host no-op stubs). The device number comes from the rig /bench
// endpoint; this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPC_ENABLE_SNMP=1 test/performance_benching/services/snmp/host.c
//   src/services/net/snmp/snmp_agent.c src/services/net/snmp/snmp_ber.c
//   src/network_drivers/transport/udp.c src/network_drivers/transport/udp/udp_listener.c
//   src/network_drivers/transport/udp/udp_client.c src/network_drivers/transport/net_addr.c
//   src/mmgr/protomem.c src/mmgr/protostr.c src/shared_primitives/ip.c -o /tmp/bs && /tmp/bs

#define PC_ENABLE_SNMP 1
#include "services/net/snmp/snmp_agent.h"
#include "services/net/snmp/snmp_ber.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

// Build a v2c request datagram (mirrors the SNMP agent test's builder).
static size_t build_req(uint8_t *buf, size_t cap, uint8_t pdu, long reqid, long f2, long f3, const uint32_t *oid,
                        size_t oidn)
{
    BerEnc e;
    pc_ber_enc_init(&e, buf, cap);
    size_t msg = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, 1); // v2c
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)"public", 6);
    size_t pdus = pc_ber_seq_begin(&e, pdu);
    pc_ber_put_integer(&e, reqid);
    pc_ber_put_integer(&e, f2);
    pc_ber_put_integer(&e, f3);
    size_t vbl = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    size_t vb = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_oid(&e, oid, oidn);
    pc_ber_put_null(&e);
    pc_ber_seq_end(&e, vb);
    pc_ber_seq_end(&e, vbl);
    pc_ber_seq_end(&e, pdus);
    pc_ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

int main(void)
{
    pc_snmp_agent_init("public");
    pc_snmp_agent_set_system("ProtoCore SNMP agent bench", "admin@example.com", "esp32-pc", "lab bench", 72);

    static const uint32_t OID_SYSDESCR[] = {1, 3, 6, 1, 2, 1, 1, 1, 0}; // sysDescr.0 (OCTET STRING)
    static const uint32_t OID_SYSTEM[] = {1, 3, 6, 1, 2, 1, 1};         // system group (GetNext walk root)

    uint8_t reqget[128], reqnext[128], resp[512];
    size_t nget = build_req(reqget, sizeof(reqget), (uint8_t)SNMP_TAG_SNMP_PDU_GET, 0x0102, 0, 0, OID_SYSDESCR, 9);
    size_t nnext = build_req(reqnext, sizeof(reqnext), (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT, 0x0103, 0, 0, OID_SYSTEM, 7);

    hbench_header();

    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(500000, sink += pc_snmp_agent_process(reqget, nget, resp, sizeof(resp)), ns);
        hbench_row("snmp", "process GET sysDescr.0", ns, (double)nget);
        (void)sink;
    }
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(500000, sink += pc_snmp_agent_process(reqnext, nnext, resp, sizeof(resp)), ns);
        hbench_row("snmp", "process GETNEXT (walk)", ns, (double)nnext);
        (void)sink;
    }

    return 0;
}
