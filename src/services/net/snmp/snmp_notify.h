// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_notify.h
 * @brief Notifications: the SNMPv2-Trap-PDU and the InformRequest-PDU (PROTOCORE_ENABLE_SNMP_TRAP).
 *
 * The notification originator side of the agent: an event is pushed to a notification receiver
 * instead of waiting to be polled.
 *
 * RFC 3416 sec 4.2.6 defines the SNMPv2-Trap-PDU and sec 4.2.7 the InformRequest-PDU. Both carry
 * the same two mandatory first variable-bindings: sysUpTime.0 and snmpTrapOID.0 (RFC 3418 sec 2),
 * followed by whatever bindings the caller adds. The two differ in confirmation, not in shape: a
 * trap is unacknowledged, while an InformRequest-PDU is answered by a Response-PDU that echoes its
 * request-id, so its sender owns the retransmission.
 *
 * RFC 3417 sec 3.2 suggests notification receivers listen on UDP port 162.
 *
 * The build calls are pure: message octets in a caller buffer, no socket and no clock, so they are
 * unit-tested with no network stack under them. The send calls add the address parse and the
 * datagram. SNMPv3 USM notifications are the v3 layer's, reached through ::SnmpV3.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SNMP_NOTIFY_H
#define PROTOCORE_SNMP_NOTIFY_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SNMP_TRAP

PROTOCORE_BEGIN_DECLS

#include "services/net/snmp/snmp_ber.h" // BerEnc: the open encoder a PDU append writes into
/** @brief Which SMIv2 type a caller variable-binding carries (RFC 2578 sec 7.1). */
typedef enum PROTO_ENUM_PACKED
{
    SNMP_VB_INT = 0,       ///< INTEGER, in ival
    SNMP_VB_STRING = 1,    ///< OCTET STRING, in bytes/blen
    SNMP_VB_OID = 2,       ///< OBJECT IDENTIFIER, in oid_val/oid_val_len
    SNMP_VB_COUNTER32 = 3, ///< Counter32, in ival (RFC 2578 sec 7.1.6)
    SNMP_VB_GAUGE32 = 4,   ///< Gauge32, in ival (RFC 2578 sec 7.1.7)
    SNMP_VB_TIMETICKS = 5, ///< TimeTicks, in ival (RFC 2578 sec 7.1.8)
    SNMP_VB_IPADDR = 6,    ///< IpAddress, 4 octets in bytes (RFC 2578 sec 7.1.5)
} SnmpVbType;

/** @brief One variable-binding of the VarBindList: a name and its typed value (RFC 3416 sec 3). */
typedef struct
{
    const uint32_t *oid;     ///< the binding's name, as subidentifiers
    size_t oid_len;          ///< how many
    uint8_t type;            ///< which ::SnmpVbType the value is
    long ival;               ///< INTEGER, Counter32, Gauge32 or TimeTicks value
    const uint8_t *bytes;    ///< OCTET STRING or IpAddress octets
    size_t blen;             ///< how many
    const uint32_t *oid_val; ///< an OBJECT IDENTIFIER value's subidentifiers
    size_t oid_val_len;      ///< how many
} SnmpVarbind;

/** @brief RFC 3416 sec 4.2.6 and sec 4.2.7: what a notification PDU carries. */
typedef struct
{
    uint8_t pdu_tag;          ///< ::SNMP_TAG_SNMP_PDU_TRAPV2 or ::SNMP_TAG_SNMP_PDU_INFORM
    uint32_t request_id;      ///< the PDU's request-id, echoed by an inform's Response-PDU
    const uint32_t *trap_oid; ///< the snmpTrapOID.0 value (RFC 3418 sec 2)
    size_t trap_oid_len;      ///< how many subidentifiers
    uint32_t uptime_ticks;    ///< the sysUpTime.0 value, TimeTicks (RFC 2578 sec 7.1.8)
    const SnmpVarbind *vbs;   ///< the caller bindings that follow the mandatory two
    size_t vb_count;          ///< how many
} SnmpNotifyPduArgs;

/** @brief RFC 3417 sec 3.2: the notification receiver a send addresses. */
typedef struct
{
    const char *dst_ip;    ///< its address, as text
    uint16_t port;         ///< its port, 162 by convention
    const char *community; ///< the community the message carries (RFC 1157 sec 3.2.5)
} SnmpNotifyDstArgs;

/** @brief Where a notification is built: an open encoder, or a bare buffer. */
typedef struct
{
    BerEnc *enc;  ///< the open encoder a PDU append writes into
    uint8_t *out; ///< where a complete message is built
    size_t cap;   ///< how many octets that holds
} SnmpNotifyBufArgs;

/**
 * @brief The notification originator (RFC 3416 sec 4.2.6, sec 4.2.7).
 *
 * A caller sets the members a call takes, invokes it through ::SnmpNotify, and reads the outcome
 * off the same handle.
 *
 * @var SnmpNotifyNs::pdu           what the notification PDU carries
 * @var SnmpNotifyNs::dst           the notification receiver a send addresses
 * @var SnmpNotifyNs::buf           where the message is built
 * @var SnmpNotifyNs::ok            a send's true/false outcome: the stack took the datagram
 * @var SnmpNotifyNs::n             octets a build wrote, 0 when the buffer could not hold them
 * @var SnmpNotifyNs::build_pdu     append the notification PDU to @c buf.enc, mandatory bindings first
 * @var SnmpNotifyNs::build_v2c     build a complete SNMPv2c notification message into @c buf.out
 * @var SnmpNotifyNs::trap_v2c      build and send an SNMPv2-Trap-PDU, sysUpTime.0 from the clock
 * @var SnmpNotifyNs::inform_v2c    build and send an InformRequest-PDU under the caller's request-id
 */
typedef struct
{
    SnmpNotifyPduArgs pdu; ///< what the notification PDU carries
    SnmpNotifyDstArgs dst; ///< where a send goes
    SnmpNotifyBufArgs buf; ///< where the message is built

    proto_bool ok;
    size_t n;

    void (*const build_pdu)(uint8_t *restrict work);
    void (*const build_v2c)(uint8_t *restrict work);
    void (*const trap_v2c)(uint8_t *restrict work);
    void (*const inform_v2c)(uint8_t *restrict work);
} SnmpNotifyNs;

/** @brief The one symbol this module exports. */
extern SnmpNotifyNs SnmpNotify;

/**
 * @brief The PROTOCORE_SNMP_NOTIFY_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_snmp_notify_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SNMP_TRAP

#endif // PROTOCORE_SNMP_NOTIFY_H
