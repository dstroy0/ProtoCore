// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SNMP agent (services/net/snmp): the v2c GET and GETNEXT
// request processing (BER decode, MIB dispatch, response BER encode) - the untrusted-input hot op an
// SNMP agent runs per datagram. The request datagrams are built in-buffer with the BER encoder; the
// UDP socket is out of scope.
//
// Build/flash:  idf.py -C test/performance_benching/snmp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/snmp/snmp_agent.h"
#include "services/net/snmp/snmp_ber.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Build a v2c request datagram (mirrors the SNMP agent test's builder + the host bench).
static size_t build_req(uint8_t *buf, size_t cap, uint8_t pdu, long reqid, const uint32_t *oid, size_t oidn)
{
    BerEnc e;
    protocore_ber_enc_init(&e, buf, cap);
    size_t msg = protocore_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 1;
    SnmpBer.put_integer(SnmpBer.internal); // v2c
    protocore_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)"public", 6);
    size_t pdus = protocore_ber_seq_begin(&e, pdu);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = reqid;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 0;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.enc = &e;
    SnmpBer.tlv.ival = 0;
    SnmpBer.put_integer(SnmpBer.internal);
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

void dbench_run(void)
{
    protocore_snmp_agent_init("public");
    protocore_snmp_agent_set_system("ProtoCore SNMP agent bench", "admin@pc", "esp32-pc", "lab", 72);

    static const uint32_t OID_SYSDESCR[] = {1, 3, 6, 1, 2, 1, 1, 1, 0}; // sysDescr.0
    static const uint32_t OID_SYSTEM[] = {1, 3, 6, 1, 2, 1, 1};         // system group (GetNext root)
    static uint8_t reqget[128], reqnext[128], resp[512];
    size_t nget = build_req(reqget, sizeof(reqget), (uint8_t)SNMP_TAG_SNMP_PDU_GET, 0x0102, OID_SYSDESCR, 9);
    size_t nnext = build_req(reqnext, sizeof(reqnext), (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT, 0x0103, OID_SYSTEM, 7);

    for (;;)
    {
        DBENCH_BANNER("snmp");
        volatile size_t sink = 0;
        DBENCH_OP("protocore_snmp_agent_process GET", 100000,
                  sink += protocore_snmp_agent_process(reqget, nget, resp, sizeof(resp)));
        DBENCH_OP("protocore_snmp_agent_process GETNEXT", 100000,
                  sink += protocore_snmp_agent_process(reqnext, nnext, resp, sizeof(resp)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("snmp")
