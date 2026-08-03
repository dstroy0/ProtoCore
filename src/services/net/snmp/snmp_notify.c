// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_snmp_notify.cpp
 * @brief Outbound SNMP Trap / Inform PDU builder (host-testable) + the UDP send
 *        (ESP32 only). SNMPv3 USM notifications live in pc_snmp_v3.cpp.
 */

#include "services/net/snmp/snmp_notify.h"

#if PC_ENABLE_SNMP_TRAP

#include "services/net/snmp/snmp_ber.h"
#include <string.h>

// The two mandatory bindings of any v2c/v3 notification (RFC 3416 4.2.6).
#if PROTOCORE_HOT
#include "network_drivers/transport/udp.h"
#include "server/clock/clock.h" // pc_millis() - the library's clock seam (ban 5: never bare millis)
#endif
static const uint32_t OID_SYSUPTIME_0[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
static const uint32_t OID_SNMPTRAPOID_0[] = {1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0};

// Encode one caller varbind: SEQUENCE { OID, typed-value }.
static void put_varbind(BerEnc *e, const SnmpVarbind *vb)
{
    size_t t = pc_ber_seq_begin(e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_oid(e, vb->oid, vb->oid_len);
    switch (vb->type)
    {
    case (uint8_t)SNMP_VB_INT:
        pc_ber_put_integer(e, vb->ival);
        break;
    case (uint8_t)SNMP_VB_STRING:
        pc_ber_put_octet_string(e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, vb->bytes, vb->blen);
        break;
    case (uint8_t)SNMP_VB_OID:
        pc_ber_put_oid(e, vb->oid_val, vb->oid_val_len);
        break;
    case (uint8_t)SNMP_VB_COUNTER32:
        pc_ber_put_uint(e, (uint8_t)SNMP_TAG_SNMP_COUNTER32, (uint32_t)vb->ival);
        break;
    case (uint8_t)SNMP_VB_GAUGE32:
        pc_ber_put_uint(e, (uint8_t)SNMP_TAG_SNMP_GAUGE32, (uint32_t)vb->ival);
        break;
    case (uint8_t)SNMP_VB_TIMETICKS:
        pc_ber_put_uint(e, (uint8_t)SNMP_TAG_SNMP_TIMETICKS, (uint32_t)vb->ival);
        break;
    case (uint8_t)SNMP_VB_IPADDR:
        pc_ber_put_octet_string(e, (uint8_t)SNMP_TAG_SNMP_IPADDRESS, vb->bytes, vb->blen);
        break;
    default:
        e->ok = PROTO_FALSE;
        break;
    }
    pc_ber_seq_end(e, t);
}

// Build the notification PDU (request-id, 0, 0, varbinds) under @p pdu_tag. The
// varbinds begin with sysUpTime.0 + snmpTrapOID.0. Shared with the v3 builder
// (which wraps this PDU in a scopedPDU); see pc_snmp_notify_build_pdu() in the header.
size_t pc_snmp_notify_build_pdu(BerEnc *e, uint8_t pdu_tag, uint32_t request_id, const uint32_t *trap_oid,
                                size_t trap_oid_len, uint32_t uptime_ticks, const SnmpVarbind *vbs, size_t n)
{
    size_t pdu = pc_ber_seq_begin(e, pdu_tag);
    pc_ber_put_integer(e, (long)request_id); // request-id
    pc_ber_put_integer(e, 0);                // error-status
    pc_ber_put_integer(e, 0);                // error-index
    size_t vbl = pc_ber_seq_begin(e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    // sysUpTime.0 = TimeTicks
    {
        size_t t = pc_ber_seq_begin(e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
        pc_ber_put_oid(e, OID_SYSUPTIME_0, sizeof(OID_SYSUPTIME_0) / sizeof(uint32_t));
        pc_ber_put_uint(e, (uint8_t)SNMP_TAG_SNMP_TIMETICKS, uptime_ticks);
        pc_ber_seq_end(e, t);
    }
    // snmpTrapOID.0 = OID
    {
        size_t t = pc_ber_seq_begin(e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
        pc_ber_put_oid(e, OID_SNMPTRAPOID_0, sizeof(OID_SNMPTRAPOID_0) / sizeof(uint32_t));
        pc_ber_put_oid(e, trap_oid, trap_oid_len);
        pc_ber_seq_end(e, t);
    }
    for (size_t i = 0; i < n; i++)
    {
        put_varbind(e, &vbs[i]);
    }
    pc_ber_seq_end(e, vbl);
    pc_ber_seq_end(e, pdu);
    return e->len;
}

size_t pc_snmp_notify_build_v2c(uint8_t *out, size_t cap, const char *community, uint8_t pdu_tag, uint32_t request_id,
                                const uint32_t *trap_oid, size_t trap_oid_len, uint32_t uptime_ticks,
                                const SnmpVarbind *vbs, size_t n)
{
    if (!out || !community || !trap_oid)
    {
        return 0;
    }
    BerEnc e;
    pc_ber_enc_init(&e, out, cap);
    size_t msg = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, 1); // version: SNMPv2c
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)community,
                            strnlen(community, SNMP_COMMUNITY_MAX + 1));
    pc_snmp_notify_build_pdu(&e, pdu_tag, request_id, trap_oid, trap_oid_len, uptime_ticks, vbs, n);
    pc_ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

// ---------------------------------------------------------------------------
// Transport (ESP32 only)
// ---------------------------------------------------------------------------
#if PROTOCORE_HOT

// All SNMP-notify transport state, owned by one instance (internal linkage): the trap
// request-id counter, so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    uint32_t trap_reqid = 1;
} SnmpNotifyCtx;
static SnmpNotifyCtx s_notify;

proto_bool pc_snmp_trap_v2c(const char *dst_ip, uint16_t port, const char *community, const uint32_t *trap_oid,
                            size_t trap_oid_len, const SnmpVarbind *vbs, size_t n)
{
    uint8_t buf[PC_SNMP_TRAP_BUF_SIZE];
    uint32_t up = (uint32_t)(pc_millis() / 10); // TimeTicks = hundredths of a second
    size_t len = pc_snmp_notify_build_v2c(buf, sizeof(buf), community, (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2,
                                          s_notify.trap_reqid++, trap_oid, trap_oid_len, up, vbs, n);
    return len && pc_udp_sendto(dst_ip, port, buf, len);
}

proto_bool pc_snmp_inform_v2c(const char *dst_ip, uint16_t port, const char *community, uint32_t request_id,
                              const uint32_t *trap_oid, size_t trap_oid_len, const SnmpVarbind *vbs, size_t n)
{
    uint8_t buf[PC_SNMP_TRAP_BUF_SIZE];
    uint32_t up = (uint32_t)(pc_millis() / 10);
    size_t len = pc_snmp_notify_build_v2c(buf, sizeof(buf), community, 0xA6 /* InformRequest */, request_id, trap_oid,
                                          trap_oid_len, up, vbs, n);
    return len && pc_udp_sendto(dst_ip, port, buf, len);
}

#else // host build: transport is a stub

proto_bool pc_snmp_trap_v2c(const char *, uint16_t, const char *, const uint32_t *, size_t, const SnmpVarbind *, size_t)
{
    return PROTO_FALSE;
}
proto_bool pc_snmp_inform_v2c(const char *, uint16_t, const char *, uint32_t, const uint32_t *, size_t,
                              const SnmpVarbind *, size_t)
{
    return PROTO_FALSE;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_SNMP_TRAP
