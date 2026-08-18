// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_v3.h
 * @brief SNMPv3: the message framing and the User-based Security Model (PROTOCORE_ENABLE_SNMP_V3).
 *
 * Authenticated and optionally encrypted SNMP on top of the same MIB the community framings reach.
 *
 * RFC 3412 sec 6 gives the message: `SNMPv3Message ::= SEQUENCE { msgVersion, msgGlobalData
 * HeaderData, msgSecurityParameters OCTET STRING, msgData ScopedPduData }`, where HeaderData is
 * msgID, msgMaxSize, msgFlags and msgSecurityModel, and a ScopedPDU is contextEngineID,
 * contextName and the PDU. RFC 3412 sec 6.4 defines the msgFlags bits authFlag, privFlag and
 * reportableFlag.
 *
 * RFC 3414 sec 2.4 gives msgSecurityParameters for USM: msgAuthoritativeEngineID,
 * msgAuthoritativeEngineBoots, msgAuthoritativeEngineTime, msgUserName,
 * msgAuthenticationParameters and msgPrivacyParameters. This agent is the authoritative engine,
 * so it answers discovery (RFC 3414 sec 4) with a Report PDU naming usmStatsUnknownEngineIDs, and
 * enforces the 150-second time window of RFC 3414 sec 2.2.3. Every failure is reported as the
 * matching usmStats counter of RFC 3414 sec 5.
 *
 * One authPriv user is configured:
 *  - **Authentication:** usmHMAC192SHA256AuthProtocol (RFC 7860 sec 8), HMAC-SHA-256 truncated to
 *    24 octets (RFC 7860 sec 4.1). The digest is computed over the whole message with
 *    msgAuthenticationParameters replaced by zero octets (RFC 7860 sec 4.2.1 and sec 4.2.2).
 *  - **Privacy:** usmAesCfb128Protocol (RFC 3826), CFB128-AES-128 under the IV of RFC 3826
 *    sec 3.1.2.1.
 *
 * The decrypted, authenticated inner PDU is dispatched through the shared MIB core
 * (@ref SnmpAgentNs::dispatch_pdu), so all three framings expose the same objects. The localized
 * keys are derived once in @ref SnmpV3Ns::set_user, not per message.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SNMP_V3_H
#define PROTOCORE_SNMP_V3_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SNMP_V3

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_SNMP_TRAP
#include "services/net/snmp/snmp_notify.h" // SnmpVarbind: the bindings a v3 notification carries
#endif

/** @brief msgAuthenticationParameters length: HMAC-SHA-256 truncated to 192 bits (RFC 7860 sec 4.1). */
#define SNMP_V3_AUTH_PARAM_LEN 24
/** @brief msgPrivacyParameters length: the 64-bit salt of RFC 3826 sec 3.1.2.1. */
#define SNMP_V3_PRIV_PARAM_LEN 8

/** @brief RFC 3414 sec 2.4: the authoritative engine this agent is. */
typedef struct
{
    const uint8_t *engine_id; ///< the snmpEngineID octets; NULL keeps the built-in default
    size_t engine_id_len;     ///< how many, 5 through SNMP_V3_ENGINEID_MAX
    uint32_t boots;           ///< msgAuthoritativeEngineBoots (RFC 3414 sec 2.2.2)
} SnmpV3EngineArgs;

/** @brief RFC 3414 sec 2.1: the USM user, and the passwords its localized keys come from. */
typedef struct
{
    const char *user;      ///< msgUserName
    const char *auth_pass; ///< the authentication password; NULL or empty leaves no usable user
    const char *priv_pass; ///< the privacy password; NULL or empty is authNoPriv
} SnmpV3UserArgs;

/** @brief RFC 3412 sec 6: the SNMPv3Message read, and the one written back. */
typedef struct
{
    const uint8_t *req; ///< the received message octets
    size_t req_len;     ///< how many
    uint8_t *resp;      ///< where the response message is written
    size_t resp_cap;    ///< how many octets that holds
} SnmpV3MsgArgs;

#if PROTOCORE_ENABLE_SNMP_TRAP
/** @brief RFC 3416 sec 4.2.6 and sec 4.2.7: what a v3 notification carries, and where it goes. */
typedef struct
{
    const char *dst_ip;       ///< the notification receiver's address, as text
    uint16_t port;            ///< its port, 162 by convention (RFC 3417 sec 3.2)
    uint32_t request_id;      ///< the PDU's request-id, echoed by an inform's Response-PDU
    const uint32_t *trap_oid; ///< the snmpTrapOID.0 value (RFC 3418 sec 2)
    size_t trap_oid_len;      ///< how many subidentifiers
    const SnmpVarbind *vbs;   ///< the caller bindings that follow the mandatory two
    size_t vb_count;          ///< how many
} SnmpV3NotifyArgs;
#endif

/**
 * @brief The SNMPv3 engine (RFC 3412 sec 6, RFC 3414).
 *
 * A caller sets the members a call takes, invokes it through ::SnmpV3, and reads the outcome off
 * the same handle.
 *
 * @var SnmpV3Ns::engine     the authoritative engine identity and its boot count
 * @var SnmpV3Ns::user       the USM user and its passwords
 * @var SnmpV3Ns::msg        the message read and the one written back
 * @var SnmpV3Ns::notify     what an outgoing notification carries and where it goes
 * @var SnmpV3Ns::ok         a call's true/false outcome
 * @var SnmpV3Ns::n          response octets written, 0 to send nothing
 * @var SnmpV3Ns::u32        msgAuthoritativeEngineBoots, as a read reports it
 * @var SnmpV3Ns::init       take the authoritative snmpEngineID and forget the configured user
 * @var SnmpV3Ns::set_user   take the USM user and derive its localized keys
 * @var SnmpV3Ns::set_boots  take the persisted msgAuthoritativeEngineBoots
 * @var SnmpV3Ns::get_boots  report msgAuthoritativeEngineBoots, for persisting it back
 * @var SnmpV3Ns::process    answer one SNMPv3Message: discovery, timeliness, auth, privacy, dispatch
 * @var SnmpV3Ns::trap       send an authenticated SNMPv2-Trap-PDU in a v3 message
 * @var SnmpV3Ns::inform     send an authenticated InformRequest-PDU in a v3 message
 */
typedef struct
{
    SnmpV3EngineArgs engine; ///< the authoritative engine (RFC 3414 sec 2.4)
    SnmpV3UserArgs user;     ///< the USM user (RFC 3414 sec 2.1)
    SnmpV3MsgArgs msg;       ///< the message pair (RFC 3412 sec 6)
#if PROTOCORE_ENABLE_SNMP_TRAP
    SnmpV3NotifyArgs notify; ///< what an outgoing notification carries
#endif

    proto_bool ok;
    size_t n;
    uint32_t u32;

    void (*const init)(uint8_t *restrict work);
    void (*const set_user)(uint8_t *restrict work);
    void (*const set_boots)(uint8_t *restrict work);
    void (*const get_boots)(uint8_t *restrict work);
    void (*const process)(uint8_t *restrict work);
#if PROTOCORE_ENABLE_SNMP_TRAP
    void (*const trap)(uint8_t *restrict work);
    void (*const inform)(uint8_t *restrict work);
#endif
} SnmpV3Ns;

/** @brief The one symbol this module exports. */
extern SnmpV3Ns SnmpV3;

/**
 * @brief The PROTOCORE_SNMP_V3_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_snmp_v3_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SNMP_V3

#endif // PROTOCORE_SNMP_V3_H
