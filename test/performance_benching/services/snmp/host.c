// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the SNMP v1/v2c agent codec: protocore_snmp_agent_process() takes a complete
// request datagram and produces the response datagram - BER-decode the message + PDU, walk the MIB,
// BER-encode the reply. Pure (no sockets, no heap), so it links standalone (the UDP transport symbols
// it references come from udp.c's host no-op stubs). The device number comes from the rig /bench
// endpoint; this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_SNMP=1 test/performance_benching/services/snmp/host.c
//   src/services/net/snmp/snmp_agent.c src/services/net/snmp/snmp_ber.c
//   src/network_drivers/transport/udp.c src/network_drivers/transport/udp/udp_listener.c
//   src/network_drivers/transport/udp/udp_client.c src/network_drivers/transport/net_addr.c
//   src/mmgr/protomem.c src/mmgr/protostr.c src/shared/ip/ip.c -o /tmp/bs && /tmp/bs

#define PROTOCORE_ENABLE_SNMP 1
#include "services/net/snmp/snmp_agent/snmp_agent.h"
#include "services/net/snmp/snmp_ber/snmp_ber.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

static uint8_t snmp_ber_work[16]; // the borrow an entry takes; SnmpBer never reads it

// Build a v2c request datagram (mirrors the SNMP agent test's builder).
static size_t build_req(uint8_t *buf, size_t cap, uint8_t pdu, long reqid, long f2, long f3, const uint32_t *oid,
                        size_t oidn)
{
    BerEnc e;
    protocore_ber_enc_init(&e, buf, cap);
    size_t msg = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = 1;
    SnmpBer.put_integer(snmp_ber_work); // v2c
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)"public", 6);
    size_t pdus = protocore_ber_seq_begin(&e, pdu);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = reqid;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = f2;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.ival = f3;
    SnmpBer.put_integer(snmp_ber_work);
    size_t vbl = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    size_t vb = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    protocore_ber_put_oid(&e, oid, oidn);
    protocore_ber_put_null(&e);
    protocore_ber_seq_end(&e, vb);
    protocore_ber_seq_end(&e, vbl);
    protocore_ber_seq_end(&e, pdus);
    protocore_ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

int main(void)
{
    protocore_snmp_agent_init("public");
    protocore_snmp_agent_set_system("ProtoCore SNMP agent bench", "admin@example.com", "esp32-pc", "lab bench", 72);

    static const uint32_t OID_SYSDESCR[] = {1, 3, 6, 1, 2, 1, 1, 1, 0}; // sysDescr.0 (OCTET STRING)
    static const uint32_t OID_SYSTEM[] = {1, 3, 6, 1, 2, 1, 1};         // system group (GetNext walk root)

    uint8_t reqget[128], reqnext[128], resp[512];
    size_t nget = build_req(reqget, sizeof(reqget), (uint8_t)SNMP_TAG_SNMP_PDU_GET, 0x0102, 0, 0, OID_SYSDESCR, 9);
    size_t nnext = build_req(reqnext, sizeof(reqnext), (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT, 0x0103, 0, 0, OID_SYSTEM, 7);

    hbench_header();

    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(500000, sink += protocore_snmp_agent_process(reqget, nget, resp, sizeof(resp)), ns);
        hbench_row("snmp", "process GET sysDescr.0", ns, (double)nget);
        (void)sink;
    }
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(500000, sink += protocore_snmp_agent_process(reqnext, nnext, resp, sizeof(resp)), ns);
        hbench_row("snmp", "process GETNEXT (walk)", ns, (double)nnext);
        (void)sink;
    }

    return 0;
}
