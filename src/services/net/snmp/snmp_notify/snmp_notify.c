// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_notify.c
 * @brief The notification originator (RFC 3416 sec 4.2.6, sec 4.2.7) - implementation.
 *        See snmp_notify.h. SNMPv3 USM notifications are bound on ::SnmpV3.
 */

#include "services/net/snmp/snmp_notify/snmp_notify.h"
#include "mmgr/protostr/protostr.h" // str.len: the community length, bounded
#include "mmgr/secure/secure.h"     // the persistent end this module's key material is taken from

static uint8_t snmp_ber_work[16]; // the borrow an entry takes; SnmpBer never reads it

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

#if PROTOCORE_ENABLE_SNMP_TRAP

#if PROTOCORE_HAS_NET_STACK
#include "network_drivers/transport/udp/client/client.h" // UdpClient: the datagram out
#include "server/clock/clock.h"                          // protocore_millis(): the library's clock seam
#include "shared/ip/ip.h"                                // Ip.parse: the receiver's address, once
#endif

// The two mandatory first bindings of every notification (RFC 3416 sec 4.2.6, RFC 3418 sec 2).
static const uint32_t OID_SYSUPTIME_0[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
static const uint32_t OID_SNMPTRAPOID_0[] = {1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0};

/**
 * @brief The originator's compile-time storage: the request-id counter and the send stage.
 *
 * All of it BSS, so a notification costs no heap and no message lands on a task stack.
 *
 * @var SnmpNotifyStorage::trap_reqid  the request-id a trap takes next (RFC 3416 sec 4.1)
 * @var SnmpNotifyStorage::tx          one built message, for the length of one send
 */
struct SnmpNotifyStorage
{
    uint32_t trap_reqid;
    uint8_t tx[PROTOCORE_SNMP_TRAP_BUF_SIZE];
};

// A trap is unacknowledged, so its request-id only has to differ from the last one; the counter
// starts at 1 and the rest of the storage starts at zero.
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SNMP_NOTIFY_OFF_CTX 0u
static_assert(SNMP_NOTIFY_OFF_CTX + sizeof(struct SnmpNotifyStorage) <= PROTOCORE_SNMP_NOTIFY_BORROW,
              "PROTOCORE_SNMP_NOTIFY_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SNMP_NOTIFY_CTX(w) ((struct SnmpNotifyStorage *)(void *)((w) + SNMP_NOTIFY_OFF_CTX))

// One caller binding: SEQUENCE { name, typed value }. Reads the binding it is given and no module
// state, so it takes that binding rather than the handle.
static void put_varbind(BerEnc *e, const SnmpVarbind *vb)
{
    SnmpBerV.enc = e;
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t t = SnmpBerV.tlv.token;
    SnmpBerV.tlv.arcs = vb->oid;
    SnmpBerV.tlv.arc_count = vb->oid_len;
    SnmpBer.put_oid(snmp_ber_work);
    switch (vb->type)
    {
    case (uint8_t)SNMP_VB_INT:
        SnmpBerV.tlv.ival = vb->ival;
        SnmpBer.put_integer(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_STRING:
        SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
        SnmpBerV.tlv.bytes = vb->bytes;
        SnmpBerV.tlv.len = vb->blen;
        SnmpBer.put_octet_string(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_OID:
        SnmpBerV.tlv.arcs = vb->oid_val;
        SnmpBerV.tlv.arc_count = vb->oid_val_len;
        SnmpBer.put_oid(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_COUNTER32:
        SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_SNMP_COUNTER32;
        SnmpBerV.tlv.uval = (uint32_t)vb->ival;
        SnmpBer.put_uint(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_GAUGE32:
        SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_SNMP_GAUGE32;
        SnmpBerV.tlv.uval = (uint32_t)vb->ival;
        SnmpBer.put_uint(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_TIMETICKS:
        SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_SNMP_TIMETICKS;
        SnmpBerV.tlv.uval = (uint32_t)vb->ival;
        SnmpBer.put_uint(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_IPADDR:
        SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_SNMP_IPADDRESS;
        SnmpBerV.tlv.bytes = vb->bytes;
        SnmpBerV.tlv.len = vb->blen;
        SnmpBer.put_octet_string(snmp_ber_work);
        break;
    default:
        e->ok = PROTO_FALSE;
        break;
    }
    SnmpBerV.enc = e;
    SnmpBerV.tlv.token = t;
    SnmpBer.seq_end(snmp_ber_work);
}

// The notification PDU: request-id, error-status 0, error-index 0, then the VarBindList with
// sysUpTime.0 and snmpTrapOID.0 first (RFC 3416 sec 4.2.6). Reads the PDU members off the handle,
// so it takes ctx, plus the encoder it appends to.
static void append_pdu(uint8_t *restrict work, BerEnc *e)
{
    SnmpBerV.enc = e;
    SnmpBerV.tlv.tag = SnmpNotifyV.pdu.pdu_tag;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t pdu = SnmpBerV.tlv.token;
    SnmpBerV.tlv.ival = (long)SnmpNotifyV.pdu.request_id;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.tlv.ival = 0; // error-status
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.tlv.ival = 0; // error-index
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t vbl = SnmpBerV.tlv.token;

    // sysUpTime.0 = TimeTicks
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t t0 = SnmpBerV.tlv.token;
    SnmpBerV.tlv.arcs = OID_SYSUPTIME_0;
    SnmpBerV.tlv.arc_count = sizeof(OID_SYSUPTIME_0) / sizeof(uint32_t);
    SnmpBer.put_oid(snmp_ber_work);
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_SNMP_TIMETICKS;
    SnmpBerV.tlv.uval = SnmpNotifyV.pdu.uptime_ticks;
    SnmpBer.put_uint(snmp_ber_work);
    SnmpBerV.tlv.token = t0;
    SnmpBer.seq_end(snmp_ber_work);

    // snmpTrapOID.0 = OBJECT IDENTIFIER
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t t1 = SnmpBerV.tlv.token;
    SnmpBerV.tlv.arcs = OID_SNMPTRAPOID_0;
    SnmpBerV.tlv.arc_count = sizeof(OID_SNMPTRAPOID_0) / sizeof(uint32_t);
    SnmpBer.put_oid(snmp_ber_work);
    SnmpBerV.tlv.arcs = SnmpNotifyV.pdu.trap_oid;
    SnmpBerV.tlv.arc_count = SnmpNotifyV.pdu.trap_oid_len;
    SnmpBer.put_oid(snmp_ber_work);
    SnmpBerV.tlv.token = t1;
    SnmpBer.seq_end(snmp_ber_work);

    for (size_t i = 0; i < SnmpNotifyV.pdu.vb_count; i++)
    {
        put_varbind(e, &SnmpNotifyV.pdu.vbs[i]);
    }

    SnmpBerV.enc = e;
    SnmpBerV.tlv.token = vbl;
    SnmpBer.seq_end(snmp_ber_work);
    SnmpBerV.tlv.token = pdu;
    SnmpBer.seq_end(snmp_ber_work);
    SnmpNotifyV.n = e->len;
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SNMP_NOTIFY_BORROW persistent bytes
} SnmpNotifyOwnCtx;
static SnmpNotifyOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_snmp_notify_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_SNMP_NOTIFY_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        SNMP_NOTIFY_CTX(s_own.span)->trap_reqid = 1;
    }
    return s_own.span;
}

void protocore_snmp_notify_build_pdu(uint8_t *restrict work)
{
    if (SnmpNotifyV.buf.enc == NULL || SnmpNotifyV.pdu.trap_oid == NULL)
    {
        SnmpNotifyV.n = 0;
        SnmpNotifyV.ok = PROTO_FALSE;
        return;
    }
    append_pdu(work, SnmpNotifyV.buf.enc);
    SnmpNotifyV.ok = SnmpNotifyV.buf.enc->ok;
}

// SEQUENCE { version 1, community, notification PDU }: the RFC 1157 sec 4 message wrapper carrying
// an SNMPv2c version field.
void protocore_snmp_notify_build_v2c(uint8_t *restrict work)
{
    SnmpNotifyV.n = 0;
    SnmpNotifyV.ok = PROTO_FALSE;
    if (!SnmpNotifyV.buf.out || !SnmpNotifyV.dst.community || !SnmpNotifyV.pdu.trap_oid)
    {
        return;
    }
    BerEnc e;
    SnmpBerV.enc = &e;
    SnmpBerV.buf.out = SnmpNotifyV.buf.out;
    SnmpBerV.buf.cap = SnmpNotifyV.buf.cap;
    SnmpBer.enc_init(snmp_ber_work);
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t msg = SnmpBerV.tlv.token;
    SnmpBerV.tlv.ival = 1; // version: SNMPv2c
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBerV.tlv.bytes = (const uint8_t *)SnmpNotifyV.dst.community;
    SnmpBerV.tlv.len = str.len(SnmpNotifyV.dst.community, SNMP_COMMUNITY_MAX + 1);
    SnmpBer.put_octet_string(snmp_ber_work);

    append_pdu(work, &e);

    SnmpBerV.enc = &e;
    SnmpBerV.tlv.token = msg;
    SnmpBer.seq_end(snmp_ber_work);
    SnmpNotifyV.n = e.ok ? e.len : 0;
    SnmpNotifyV.ok = e.ok;
}

// ---------------------------------------------------------------------------
// Sending. RFC 3417 sec 3.2 suggests notification receivers listen on UDP 162.
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_NET_STACK
// Build the message into the send stage and hand it to the datagram service.
static void send_built(uint8_t *restrict work)
{
    SnmpNotifyV.buf.out = SNMP_NOTIFY_CTX(work)->tx;
    SnmpNotifyV.buf.cap = sizeof(SNMP_NOTIFY_CTX(work)->tx);
    protocore_snmp_notify_build_v2c(work);
    const size_t n = SnmpNotifyV.n;
    if (n == 0)
    {
        SnmpNotifyV.ok = PROTO_FALSE;
        return;
    }
    protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
    IpV.args.text = SnmpNotifyV.dst.dst_ip;
    IpV.args.out = &dst;
    Ip.parse(ip_work);
    if (!IpV.ok)
    {
        SnmpNotifyV.ok = PROTO_FALSE;
        return;
    }
    UdpClientV.dst = &dst;
    UdpClientV.dst_port = SnmpNotifyV.dst.port;
    UdpClientV.data = SNMP_NOTIFY_CTX(work)->tx;
    UdpClientV.len = n;
    UdpClient.sendto(protocore_udp_client_span());
    SnmpNotifyV.ok = UdpClientV.ok;
}
#endif // PROTOCORE_HAS_NET_STACK

// SNMPv2-Trap-PDU (RFC 3416 sec 4.2.6): unacknowledged, so the request-id comes from the module's
// counter and sysUpTime.0 from the clock.
void protocore_snmp_notify_trap_v2c(uint8_t *restrict work)
{
    SnmpNotifyV.pdu.pdu_tag = (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2;
#if PROTOCORE_HAS_NET_STACK
    SnmpNotifyV.pdu.request_id = SNMP_NOTIFY_CTX(work)->trap_reqid++;
    SnmpNotifyV.pdu.uptime_ticks = (uint32_t)(Clock.ms / 10); // TimeTicks: hundredths of a second
    send_built(work);
#else
    SnmpNotifyV.n = 0;
    SnmpNotifyV.ok = PROTO_FALSE; // no transport in this build
#endif
}

// InformRequest-PDU (RFC 3416 sec 4.2.7): confirmed, so the caller owns the request-id its
// Response-PDU echoes and retransmits until that Response arrives.
void protocore_snmp_notify_inform_v2c(uint8_t *restrict work)
{
    SnmpNotifyV.pdu.pdu_tag = (uint8_t)SNMP_TAG_SNMP_PDU_INFORM;
#if PROTOCORE_HAS_NET_STACK
    SnmpNotifyV.pdu.uptime_ticks = (uint32_t)(Clock.ms / 10);
    send_built(work);
#else
    SnmpNotifyV.n = 0;
    SnmpNotifyV.ok = PROTO_FALSE; // no transport in this build
#endif
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
SnmpNotifyVars SnmpNotifyV;

#endif // PROTOCORE_ENABLE_SNMP_TRAP
