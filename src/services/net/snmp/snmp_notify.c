// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_notify.c
 * @brief The notification originator (RFC 3416 sec 4.2.6, sec 4.2.7) - implementation.
 *        See snmp_notify.h. SNMPv3 USM notifications are bound on ::SnmpV3.
 */

#include "services/net/snmp/snmp_notify.h"
#include "mmgr/protostr.h" // str.len: the community length, bounded
#include "mmgr/secure.h"   // the persistent end this module's key material is taken from

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
    SnmpBer.enc = e;
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t t = SnmpBer.tlv.token;
    SnmpBer.tlv.arcs = vb->oid;
    SnmpBer.tlv.arc_count = vb->oid_len;
    SnmpBer.put_oid(snmp_ber_work);
    switch (vb->type)
    {
    case (uint8_t)SNMP_VB_INT:
        SnmpBer.tlv.ival = vb->ival;
        SnmpBer.put_integer(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_STRING:
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
        SnmpBer.tlv.bytes = vb->bytes;
        SnmpBer.tlv.len = vb->blen;
        SnmpBer.put_octet_string(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_OID:
        SnmpBer.tlv.arcs = vb->oid_val;
        SnmpBer.tlv.arc_count = vb->oid_val_len;
        SnmpBer.put_oid(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_COUNTER32:
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_COUNTER32;
        SnmpBer.tlv.uval = (uint32_t)vb->ival;
        SnmpBer.put_uint(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_GAUGE32:
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_GAUGE32;
        SnmpBer.tlv.uval = (uint32_t)vb->ival;
        SnmpBer.put_uint(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_TIMETICKS:
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_TIMETICKS;
        SnmpBer.tlv.uval = (uint32_t)vb->ival;
        SnmpBer.put_uint(snmp_ber_work);
        break;
    case (uint8_t)SNMP_VB_IPADDR:
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_IPADDRESS;
        SnmpBer.tlv.bytes = vb->bytes;
        SnmpBer.tlv.len = vb->blen;
        SnmpBer.put_octet_string(snmp_ber_work);
        break;
    default:
        e->ok = PROTO_FALSE;
        break;
    }
    SnmpBer.enc = e;
    SnmpBer.tlv.token = t;
    SnmpBer.seq_end(snmp_ber_work);
}

// The notification PDU: request-id, error-status 0, error-index 0, then the VarBindList with
// sysUpTime.0 and snmpTrapOID.0 first (RFC 3416 sec 4.2.6). Reads the PDU members off the handle,
// so it takes ctx, plus the encoder it appends to.
static void append_pdu(uint8_t *restrict work, BerEnc *e)
{
    SnmpBer.enc = e;
    SnmpBer.tlv.tag = SnmpNotify.pdu.pdu_tag;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t pdu = SnmpBer.tlv.token;
    SnmpBer.tlv.ival = (long)SnmpNotify.pdu.request_id;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.ival = 0; // error-status
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.ival = 0; // error-index
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t vbl = SnmpBer.tlv.token;

    // sysUpTime.0 = TimeTicks
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t t0 = SnmpBer.tlv.token;
    SnmpBer.tlv.arcs = OID_SYSUPTIME_0;
    SnmpBer.tlv.arc_count = sizeof(OID_SYSUPTIME_0) / sizeof(uint32_t);
    SnmpBer.put_oid(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_TIMETICKS;
    SnmpBer.tlv.uval = SnmpNotify.pdu.uptime_ticks;
    SnmpBer.put_uint(snmp_ber_work);
    SnmpBer.tlv.token = t0;
    SnmpBer.seq_end(snmp_ber_work);

    // snmpTrapOID.0 = OBJECT IDENTIFIER
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t t1 = SnmpBer.tlv.token;
    SnmpBer.tlv.arcs = OID_SNMPTRAPOID_0;
    SnmpBer.tlv.arc_count = sizeof(OID_SNMPTRAPOID_0) / sizeof(uint32_t);
    SnmpBer.put_oid(snmp_ber_work);
    SnmpBer.tlv.arcs = SnmpNotify.pdu.trap_oid;
    SnmpBer.tlv.arc_count = SnmpNotify.pdu.trap_oid_len;
    SnmpBer.put_oid(snmp_ber_work);
    SnmpBer.tlv.token = t1;
    SnmpBer.seq_end(snmp_ber_work);

    for (size_t i = 0; i < SnmpNotify.pdu.vb_count; i++)
    {
        put_varbind(e, &SnmpNotify.pdu.vbs[i]);
    }

    SnmpBer.enc = e;
    SnmpBer.tlv.token = vbl;
    SnmpBer.seq_end(snmp_ber_work);
    SnmpBer.tlv.token = pdu;
    SnmpBer.seq_end(snmp_ber_work);
    SnmpNotify.n = e->len;
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SNMP_NOTIFY_BORROW persistent bytes, or null while the pool was short
} SnmpNotifyOwnCtx;
static SnmpNotifyOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_snmp_notify_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_SNMP_NOTIFY_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
            // A borrow arrives zeroed, and these do not start at zero.
            SNMP_NOTIFY_CTX(s_own.span)->trap_reqid = 1;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void build_pdu(uint8_t *restrict work)
{
    if (SnmpNotify.buf.enc == NULL || SnmpNotify.pdu.trap_oid == NULL)
    {
        SnmpNotify.n = 0;
        SnmpNotify.ok = PROTO_FALSE;
        return;
    }
    append_pdu(work, SnmpNotify.buf.enc);
    SnmpNotify.ok = SnmpNotify.buf.enc->ok;
}

// SEQUENCE { version 1, community, notification PDU }: the RFC 1157 sec 4 message wrapper carrying
// an SNMPv2c version field.
static void build_v2c(uint8_t *restrict work)
{
    SnmpNotify.n = 0;
    SnmpNotify.ok = PROTO_FALSE;
    if (!SnmpNotify.buf.out || !SnmpNotify.dst.community || !SnmpNotify.pdu.trap_oid)
    {
        return;
    }
    BerEnc e;
    SnmpBer.enc = &e;
    SnmpBer.buf.out = SnmpNotify.buf.out;
    SnmpBer.buf.cap = SnmpNotify.buf.cap;
    SnmpBer.enc_init(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t msg = SnmpBer.tlv.token;
    SnmpBer.tlv.ival = 1; // version: SNMPv2c
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = (const uint8_t *)SnmpNotify.dst.community;
    SnmpBer.tlv.len = str.len(SnmpNotify.dst.community, SNMP_COMMUNITY_MAX + 1);
    SnmpBer.put_octet_string(snmp_ber_work);

    append_pdu(work, &e);

    SnmpBer.enc = &e;
    SnmpBer.tlv.token = msg;
    SnmpBer.seq_end(snmp_ber_work);
    SnmpNotify.n = e.ok ? e.len : 0;
    SnmpNotify.ok = e.ok;
}

// ---------------------------------------------------------------------------
// Sending. RFC 3417 sec 3.2 suggests notification receivers listen on UDP 162.
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_NET_STACK
// Build the message into the send stage and hand it to the datagram service.
static void send_built(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    SnmpNotify.buf.out = SNMP_NOTIFY_CTX(work)->tx;
    SnmpNotify.buf.cap = sizeof(SNMP_NOTIFY_CTX(work)->tx);
    build_v2c(work);
    const size_t n = SnmpNotify.n;
    if (n == 0)
    {
        SnmpNotify.ok = PROTO_FALSE;
        return;
    }
    protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
    Ip.args.text = SnmpNotify.dst.dst_ip;
    Ip.args.out = &dst;
    Ip.parse(ip_work);
    if (!Ip.ok)
    {
        SnmpNotify.ok = PROTO_FALSE;
        return;
    }
    UdpClient.dst = &dst;
    UdpClient.dst_port = SnmpNotify.dst.port;
    UdpClient.data = SNMP_NOTIFY_CTX(work)->tx;
    UdpClient.len = n;
    UdpClient.sendto(protocore_udp_client_span());
    SnmpNotify.ok = UdpClient.ok;
}
#endif // PROTOCORE_HAS_NET_STACK

// SNMPv2-Trap-PDU (RFC 3416 sec 4.2.6): unacknowledged, so the request-id comes from the module's
// counter and sysUpTime.0 from the clock.
static void trap_v2c(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    SnmpNotify.pdu.pdu_tag = (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2;
#if PROTOCORE_HAS_NET_STACK
    SnmpNotify.pdu.request_id = SNMP_NOTIFY_CTX(work)->trap_reqid++;
    SnmpNotify.pdu.uptime_ticks = (uint32_t)(Clock.ms / 10); // TimeTicks: hundredths of a second
    send_built(work);
#else
    SnmpNotify.n = 0;
    SnmpNotify.ok = PROTO_FALSE; // no transport in this build
#endif
}

// InformRequest-PDU (RFC 3416 sec 4.2.7): confirmed, so the caller owns the request-id its
// Response-PDU echoes and retransmits until that Response arrives.
static void inform_v2c(uint8_t *restrict work)
{
    SnmpNotify.pdu.pdu_tag = (uint8_t)SNMP_TAG_SNMP_PDU_INFORM;
#if PROTOCORE_HAS_NET_STACK
    SnmpNotify.pdu.uptime_ticks = (uint32_t)(Clock.ms / 10);
    send_built(work);
#else
    SnmpNotify.n = 0;
    SnmpNotify.ok = PROTO_FALSE; // no transport in this build
#endif
}

// Designated, so a member's position in the struct does not decide what it binds to.
SnmpNotifyNs SnmpNotify = {
    .build_pdu = build_pdu, .build_v2c = build_v2c, .trap_v2c = trap_v2c, .inform_v2c = inform_v2c};

#endif // PROTOCORE_ENABLE_SNMP_TRAP
