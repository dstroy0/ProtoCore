// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_notify.c
 * @brief The notification originator (RFC 3416 sec 4.2.6, sec 4.2.7) - implementation.
 *        See snmp_notify.h. SNMPv3 USM notifications are bound on ::SnmpV3.
 */

#include "services/net/snmp/snmp_notify.h"
#include "mmgr/protostr.h" // str.len: the community length, bounded

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

/**
 * @brief The originator's state and the calls that reach it - what SnmpNotifyNs points at.
 *
 * @var SnmpNotifyInternal::store  the request-id counter and the send stage
 * @var SnmpNotifyInternal::ns     the handle a caller sets a call's members on
 */
struct SnmpNotifyInternal
{
    struct SnmpNotifyStorage *store;
    SnmpNotifyNs *ns;
};

// A trap is unacknowledged, so its request-id only has to differ from the last one; the counter
// starts at 1 and the rest of the storage starts at zero.
static struct SnmpNotifyStorage s_store = {.trap_reqid = 1};

static struct SnmpNotifyInternal s_notify = {.store = &s_store, .ns = &SnmpNotify};

// One caller binding: SEQUENCE { name, typed value }. Reads the binding it is given and no module
// state, so it takes that binding rather than the handle.
static void put_varbind(BerEnc *e, const SnmpVarbind *vb)
{
    SnmpBer.enc = e;
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(SnmpBer.internal);
    const size_t t = SnmpBer.tlv.token;
    SnmpBer.tlv.arcs = vb->oid;
    SnmpBer.tlv.arc_count = vb->oid_len;
    SnmpBer.put_oid(SnmpBer.internal);
    switch (vb->type)
    {
    case (uint8_t)SNMP_VB_INT:
        SnmpBer.tlv.ival = vb->ival;
        SnmpBer.put_integer(SnmpBer.internal);
        break;
    case (uint8_t)SNMP_VB_STRING:
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
        SnmpBer.tlv.bytes = vb->bytes;
        SnmpBer.tlv.len = vb->blen;
        SnmpBer.put_octet_string(SnmpBer.internal);
        break;
    case (uint8_t)SNMP_VB_OID:
        SnmpBer.tlv.arcs = vb->oid_val;
        SnmpBer.tlv.arc_count = vb->oid_val_len;
        SnmpBer.put_oid(SnmpBer.internal);
        break;
    case (uint8_t)SNMP_VB_COUNTER32:
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_COUNTER32;
        SnmpBer.tlv.uval = (uint32_t)vb->ival;
        SnmpBer.put_uint(SnmpBer.internal);
        break;
    case (uint8_t)SNMP_VB_GAUGE32:
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_GAUGE32;
        SnmpBer.tlv.uval = (uint32_t)vb->ival;
        SnmpBer.put_uint(SnmpBer.internal);
        break;
    case (uint8_t)SNMP_VB_TIMETICKS:
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_TIMETICKS;
        SnmpBer.tlv.uval = (uint32_t)vb->ival;
        SnmpBer.put_uint(SnmpBer.internal);
        break;
    case (uint8_t)SNMP_VB_IPADDR:
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_IPADDRESS;
        SnmpBer.tlv.bytes = vb->bytes;
        SnmpBer.tlv.len = vb->blen;
        SnmpBer.put_octet_string(SnmpBer.internal);
        break;
    default:
        e->ok = PROTO_FALSE;
        break;
    }
    SnmpBer.enc = e;
    SnmpBer.tlv.token = t;
    SnmpBer.seq_end(SnmpBer.internal);
}

// The notification PDU: request-id, error-status 0, error-index 0, then the VarBindList with
// sysUpTime.0 and snmpTrapOID.0 first (RFC 3416 sec 4.2.6). Reads the PDU members off the handle,
// so it takes ctx, plus the encoder it appends to.
static void append_pdu(struct SnmpNotifyInternal *restrict ctx, BerEnc *e)
{
    SnmpBer.enc = e;
    SnmpBer.tlv.tag = ctx->ns->pdu.pdu_tag;
    SnmpBer.seq_begin(SnmpBer.internal);
    const size_t pdu = SnmpBer.tlv.token;
    SnmpBer.tlv.ival = (long)ctx->ns->pdu.request_id;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.tlv.ival = 0; // error-status
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.tlv.ival = 0; // error-index
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(SnmpBer.internal);
    const size_t vbl = SnmpBer.tlv.token;

    // sysUpTime.0 = TimeTicks
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(SnmpBer.internal);
    const size_t t0 = SnmpBer.tlv.token;
    SnmpBer.tlv.arcs = OID_SYSUPTIME_0;
    SnmpBer.tlv.arc_count = sizeof(OID_SYSUPTIME_0) / sizeof(uint32_t);
    SnmpBer.put_oid(SnmpBer.internal);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_TIMETICKS;
    SnmpBer.tlv.uval = ctx->ns->pdu.uptime_ticks;
    SnmpBer.put_uint(SnmpBer.internal);
    SnmpBer.tlv.token = t0;
    SnmpBer.seq_end(SnmpBer.internal);

    // snmpTrapOID.0 = OBJECT IDENTIFIER
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(SnmpBer.internal);
    const size_t t1 = SnmpBer.tlv.token;
    SnmpBer.tlv.arcs = OID_SNMPTRAPOID_0;
    SnmpBer.tlv.arc_count = sizeof(OID_SNMPTRAPOID_0) / sizeof(uint32_t);
    SnmpBer.put_oid(SnmpBer.internal);
    SnmpBer.tlv.arcs = ctx->ns->pdu.trap_oid;
    SnmpBer.tlv.arc_count = ctx->ns->pdu.trap_oid_len;
    SnmpBer.put_oid(SnmpBer.internal);
    SnmpBer.tlv.token = t1;
    SnmpBer.seq_end(SnmpBer.internal);

    for (size_t i = 0; i < ctx->ns->pdu.vb_count; i++)
    {
        put_varbind(e, &ctx->ns->pdu.vbs[i]);
    }

    SnmpBer.enc = e;
    SnmpBer.tlv.token = vbl;
    SnmpBer.seq_end(SnmpBer.internal);
    SnmpBer.tlv.token = pdu;
    SnmpBer.seq_end(SnmpBer.internal);
    ctx->ns->n = e->len;
}

static void build_pdu(struct SnmpNotifyInternal *restrict ctx)
{
    if (ctx->ns->buf.enc == NULL || ctx->ns->pdu.trap_oid == NULL)
    {
        ctx->ns->n = 0;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    append_pdu(ctx, ctx->ns->buf.enc);
    ctx->ns->ok = ctx->ns->buf.enc->ok;
}

// SEQUENCE { version 1, community, notification PDU }: the RFC 1157 sec 4 message wrapper carrying
// an SNMPv2c version field.
static void build_v2c(struct SnmpNotifyInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.out || !ctx->ns->dst.community || !ctx->ns->pdu.trap_oid)
    {
        return;
    }
    BerEnc e;
    SnmpBer.enc = &e;
    SnmpBer.buf.out = ctx->ns->buf.out;
    SnmpBer.buf.cap = ctx->ns->buf.cap;
    SnmpBer.enc_init(SnmpBer.internal);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(SnmpBer.internal);
    const size_t msg = SnmpBer.tlv.token;
    SnmpBer.tlv.ival = 1; // version: SNMPv2c
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = (const uint8_t *)ctx->ns->dst.community;
    SnmpBer.tlv.len = str.len(ctx->ns->dst.community, SNMP_COMMUNITY_MAX + 1);
    SnmpBer.put_octet_string(SnmpBer.internal);

    append_pdu(ctx, &e);

    SnmpBer.enc = &e;
    SnmpBer.tlv.token = msg;
    SnmpBer.seq_end(SnmpBer.internal);
    ctx->ns->n = e.ok ? e.len : 0;
    ctx->ns->ok = e.ok;
}

// ---------------------------------------------------------------------------
// Sending. RFC 3417 sec 3.2 suggests notification receivers listen on UDP 162.
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_NET_STACK
// Build the message into the send stage and hand it to the datagram service.
static void send_built(struct SnmpNotifyInternal *restrict ctx)
{
    ctx->ns->buf.out = ctx->store->tx;
    ctx->ns->buf.cap = sizeof(ctx->store->tx);
    build_v2c(ctx);
    const size_t n = ctx->ns->n;
    if (n == 0)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
    Ip.args.text = ctx->ns->dst.dst_ip;
    Ip.args.out = &dst;
    Ip.parse(Ip.internal);
    if (!Ip.ok)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    UdpClient.dst = &dst;
    UdpClient.dst_port = ctx->ns->dst.port;
    UdpClient.data = ctx->store->tx;
    UdpClient.len = n;
    UdpClient.sendto(UdpClient.internal);
    ctx->ns->ok = UdpClient.ok;
}
#endif // PROTOCORE_HAS_NET_STACK

// SNMPv2-Trap-PDU (RFC 3416 sec 4.2.6): unacknowledged, so the request-id comes from the module's
// counter and sysUpTime.0 from the clock.
static void trap_v2c(struct SnmpNotifyInternal *restrict ctx)
{
    ctx->ns->pdu.pdu_tag = (uint8_t)SNMP_TAG_SNMP_PDU_TRAPV2;
#if PROTOCORE_HAS_NET_STACK
    ctx->ns->pdu.request_id = ctx->store->trap_reqid++;
    ctx->ns->pdu.uptime_ticks = (uint32_t)(protocore_millis() / 10); // TimeTicks: hundredths of a second
    send_built(ctx);
#else
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE; // no transport in this build
#endif
}

// InformRequest-PDU (RFC 3416 sec 4.2.7): confirmed, so the caller owns the request-id its
// Response-PDU echoes and retransmits until that Response arrives.
static void inform_v2c(struct SnmpNotifyInternal *restrict ctx)
{
    ctx->ns->pdu.pdu_tag = (uint8_t)SNMP_TAG_SNMP_PDU_INFORM;
#if PROTOCORE_HAS_NET_STACK
    ctx->ns->pdu.uptime_ticks = (uint32_t)(protocore_millis() / 10);
    send_built(ctx);
#else
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE; // no transport in this build
#endif
}

// Designated, so a member's position in the struct does not decide what it binds to.
SnmpNotifyNs SnmpNotify = {.build_pdu = build_pdu,
                           .build_v2c = build_v2c,
                           .trap_v2c = trap_v2c,
                           .inform_v2c = inform_v2c,
                           .internal = &s_notify};

#endif // PROTOCORE_ENABLE_SNMP_TRAP
