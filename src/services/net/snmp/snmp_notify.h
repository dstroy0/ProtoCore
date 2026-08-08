// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_snmp_notify.h
 * @brief Outbound SNMP notifications - Trap and InformRequest (PC_ENABLE_SNMP_TRAP).
 *
 * Lets the agent push events to a manager instead of only answering polls:
 * SNMPv2c (RFC 3416) and, when PC_ENABLE_SNMP_V3 is set, SNMPv3 USM (authPriv)
 * notifications. Every notification carries the mandatory `sysUpTime.0` and
 * `snmpTrapOID.0` bindings plus any caller varbinds. Split, like the other
 * services, into a host-testable PDU builder and an ESP32 UDP send:
 *
 *  - pc_snmp_notify_build_v2c() is a pure BER function, unit-tested on the host
 *    (env:native_snmp_trap).
 *  - pc_snmp_trap_v2c() / pc_snmp_inform_v2c() (and the v3 variants) build and send to
 *    a manager over the transport-layer UDP service (port 162 by convention).
 */

#ifndef PROTOCORE_SNMP_NOTIFY_H
#define PROTOCORE_SNMP_NOTIFY_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SNMP_TRAP

/** @brief Variable-binding value types accepted in a notification. */
typedef enum PROTO_ENUM_PACKED
{
    SNMP_VB_INT = 0,       ///< INTEGER (ival)
    SNMP_VB_STRING = 1,    ///< OCTET STRING (bytes/blen)
    SNMP_VB_OID = 2,       ///< OBJECT IDENTIFIER (oid_val/oid_val_len)
    SNMP_VB_COUNTER32 = 3, ///< Counter32 (ival)
    SNMP_VB_GAUGE32 = 4,   ///< Gauge32 (ival)
    SNMP_VB_TIMETICKS = 5, ///< TimeTicks (ival)
    SNMP_VB_IPADDR = 6,    ///< IpAddress (bytes, 4 octets)
} SnmpVbType;

/** @brief One caller-supplied variable-binding (OID + typed value). */
typedef struct
{
    const uint32_t *oid;     ///< binding OID arcs
    size_t oid_len;          ///< number of arcs
    uint8_t type;            ///< SnmpVbType
    long ival;               ///< INTEGER / Counter32 / Gauge32 / TimeTicks
    const uint8_t *bytes;    ///< OCTET STRING / IpAddress bytes
    size_t blen;             ///< byte length
    const uint32_t *oid_val; ///< OID-valued binding arcs
    size_t oid_val_len;      ///< OID-valued arc count
} SnmpVarbind;

// ---------------------------------------------------------------------------
// Pure builder (host-testable; no sockets)
// ---------------------------------------------------------------------------

/**
 * @brief Build an SNMPv2c notification message (Trap or InformRequest).
 *
 * Emits `SEQUENCE { version(1), community, [pdu_tag] { request-id, 0, 0,
 * varbinds } }` where the varbinds begin with `sysUpTime.0` = @p uptime_ticks and
 * `snmpTrapOID.0` = @p trap_oid, followed by the @p n caller @p vbs.
 *
 * @param pdu_tag  SNMP_TAG_SNMP_PDU_TRAPV2 (0xA7) for a trap, 0xA6 for an InformRequest.
 * @return total message length, or 0 if it would not fit @p cap.
 */
size_t pc_snmp_notify_build_v2c(uint8_t *out, size_t cap, const char *community, uint8_t pdu_tag, uint32_t request_id,
                                const uint32_t *trap_oid, size_t trap_oid_len, uint32_t uptime_ticks,
                                const SnmpVarbind *vbs, size_t n);

typedef struct BerEnc BerEnc; // (snmp_ber.h)

/**
 * @brief Append a notification PDU (request-id, 0, 0, varbinds) under @p pdu_tag
 *        to an open encoder; the varbinds begin with sysUpTime.0 + snmpTrapOID.0.
 *
 * Used by pc_snmp_notify_build_v2c() and by the SNMPv3 notifier, which wraps this
 * PDU in a scopedPDU. @return the encoder's running length.
 */
size_t pc_snmp_notify_build_pdu(BerEnc *e, uint8_t pdu_tag, uint32_t request_id, const uint32_t *trap_oid,
                                size_t trap_oid_len, uint32_t uptime_ticks, const SnmpVarbind *vbs, size_t n);

// ---------------------------------------------------------------------------
// Transport (needs a UDP transport)
// ---------------------------------------------------------------------------

/**
 * @brief Send an SNMPv2c Trap to @p dst_ip:@p port (sysUpTime.0 is taken from millis()).
 * @return true if the datagram was queued.
 */
proto_bool pc_snmp_trap_v2c(const char *dst_ip, uint16_t port, const char *community, const uint32_t *trap_oid,
                            size_t trap_oid_len, const SnmpVarbind *vbs, size_t n);

/**
 * @brief Send an SNMPv2c InformRequest to @p dst_ip:@p port.
 *
 * Fire-and-forget at this layer (the manager's Response is not awaited or
 * retransmitted); @p request_id lets the caller match a Response if it listens.
 * @return true if the datagram was queued.
 */
proto_bool pc_snmp_inform_v2c(const char *dst_ip, uint16_t port, const char *community, uint32_t request_id,
                              const uint32_t *trap_oid, size_t trap_oid_len, const SnmpVarbind *vbs, size_t n);

#if PC_ENABLE_SNMP_V3
/**
 * @brief Send an SNMPv3 USM Trap (authPriv) using the configured engine + user.
 *
 * Builds an authenticated (and, if a privacy password is set, encrypted) v3
 * notification from the engine ID and localized keys configured via
 * pc_snmp_v3_init() / pc_snmp_v3_set_user(). @return true if the datagram was queued.
 */
proto_bool pc_snmp_trap_v3(const char *dst_ip, uint16_t port, const uint32_t *trap_oid, size_t trap_oid_len,
                           const SnmpVarbind *vbs, size_t n);

/**
 * @brief Send an SNMPv3 USM InformRequest (authPriv) - the confirmed counterpart
 *        to pc_snmp_trap_v3(), symmetric with pc_snmp_inform_v2c().
 *
 * Builds an authenticated (and, if a privacy password is set, encrypted) v3
 * InformRequest. @p request_id is echoed by the receiver in its Response; the
 * caller owns it and, for confirmed delivery, retransmits until that Response
 * arrives. @return true if the datagram was queued.
 */
proto_bool pc_snmp_inform_v3(const char *dst_ip, uint16_t port, uint32_t request_id, const uint32_t *trap_oid,
                             size_t trap_oid_len, const SnmpVarbind *vbs, size_t n);
#endif

#endif // PC_ENABLE_SNMP_TRAP

PROTO_END_DECLS

#endif // PROTOCORE_SNMP_NOTIFY_H
