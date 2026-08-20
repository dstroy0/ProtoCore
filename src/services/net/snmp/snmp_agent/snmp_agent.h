// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_agent.h
 * @brief The command responder: community-framed messages, PDU processing, and a fixed MIB.
 *
 * The agent answers management requests against a fixed table of managed objects. Two framings
 * reach the same table:
 *
 *  - **SNMPv1**, RFC 1157 sec 4: `Message ::= SEQUENCE { version version-1(0), community OCTET
 *    STRING, data ANY }`. RFC 1157 sec 3.2.5 makes the community the whole of authentication: a
 *    message is authentic when it belongs to the community it names. RFC 1157 sec 4.1 says an
 *    entity that fails authentication notes the failure and discards the datagram, so an unknown
 *    community is answered with nothing at all.
 *  - **SNMPv2c**, RFC 1901 sec 3: the same wrapper with version(1), carrying the RFC 3416 PDUs.
 *
 * PDU processing is RFC 3416 sec 4.2: the GetRequest-PDU (sec 4.2.1), the GetNextRequest-PDU
 * (sec 4.2.2), the GetBulkRequest-PDU (sec 4.2.3) and the SetRequest-PDU (sec 4.2.5), each
 * answered by a Response-PDU (sec 4.2.4). The GetBulkRequest-PDU and the per-binding exceptions
 * noSuchObject, noSuchInstance and endOfMibView belong to v2c and v3; v1 reports through
 * error-status and error-index instead (RFC 1157 sec 4.1.1).
 *
 * The MIB is a fixed table of SNMP_MAX_MIB_ENTRIES object instances, in BSS. A value is either
 * held in the entry or fetched through a getter, as sysUpTime.0 is. String and OBJECT IDENTIFIER
 * values are referenced by pointer and must outlive the agent.
 *
 * Message processing is pure: request octets in, response octets out, no socket and no heap, so
 * it is unit-tested with no network stack under it. RFC 3417 sec 3.2 suggests command responders
 * listen on UDP port 161, which @ref SnmpAgentNs::listen binds through the transport UDP service.
 * SNMPv3 is the separate USM layer, reached through ::SnmpV3.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SNMP_AGENT_H
#define PROTOCORE_SNMP_AGENT_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SNMP

PROTOCORE_BEGIN_DECLS

#include "services/net/snmp/snmp_ber/snmp_ber.h"
/** @brief The version field of the message wrapper, as it is encoded. */
typedef enum PROTO_ENUM_PACKED
{
    SNMP_V1 = 0,  ///< version-1(0), RFC 1157 sec 4
    SNMP_V2C = 1, ///< version(1), RFC 1901 sec 3
    SNMP_V3 = 3,  ///< msgVersion, RFC 3412 sec 6
} SnmpVersion;

/** @brief error-status values (RFC 1157 sec 4.1.1 for 0 through 5, RFC 3416 sec 3 for the rest). */
typedef enum PROTO_ENUM_PACKED
{
    SNMP_ERR_NO_ERROR = 0,
    SNMP_ERR_TOO_BIG = 1,
    SNMP_ERR_NO_SUCH_NAME = 2,
    SNMP_ERR_BAD_VALUE = 3,
    SNMP_ERR_READ_ONLY = 4,
    SNMP_ERR_GEN_ERR = 5,
    SNMP_ERR_NO_ACCESS = 6,
    SNMP_ERR_WRONG_TYPE = 7,
    SNMP_ERR_NOT_WRITABLE = 17,
} SnmpErr;

/**
 * @brief The value half of a variable-binding (RFC 3416 sec 3).
 *
 * Only the field matching @ref type carries anything. String and OBJECT IDENTIFIER values are
 * referenced, not copied, so they must stay valid.
 */
typedef struct
{
    uint8_t type;        ///< the value's tag: an ASN.1 simple type, an SMIv2 application-wide type
                         ///< (RFC 2578 sec 7.1), or a VarBind exception marker
    long ival;           ///< the INTEGER value
    uint32_t uval;       ///< the TimeTicks, Counter32, Gauge32 or IpAddress value
    const char *str;     ///< the OCTET STRING octets, not owned
    size_t str_len;      ///< how many
    const uint32_t *oid; ///< the OBJECT IDENTIFIER subidentifiers, not owned
    size_t oid_len;      ///< how many
} SnmpValue;
/** @brief Read a dynamic object's value: fill @p out and return true, or false for noSuchInstance. */
typedef proto_bool (*SnmpGetFn)(SnmpValue *out);
/** @brief Write a read-write object (RFC 2578 sec 7.3): true on success, false to reject the value. */
typedef proto_bool (*SnmpSetFn)(const SnmpValue *in);
/** @brief RFC 1157 sec 3.2.5: the communities a message is authenticated against. */
typedef struct
{
    const char *ro; ///< the community that authorizes reads; NULL keeps the built-in default
    const char *rw; ///< the community that authorizes a SetRequest-PDU; NULL or empty refuses every write
} SnmpCommunityArgs;
/** @brief RFC 2578 sec 7: one managed object instance and how its value is reached. */
typedef struct
{
    const uint32_t *oid; ///< the instance name, as subidentifiers
    size_t oid_len;      ///< how many, at least 2
    uint8_t type;        ///< a dynamic object's value tag (RFC 2578 sec 7.1)
    const char *text;    ///< the OCTET STRING value a static registration takes, referenced not copied
    long ival;           ///< the INTEGER value a static registration takes
    SnmpGetFn getter;    ///< what a dynamic object's value is read through
    SnmpSetFn setter;    ///< what a write reaches; NULL leaves the object read-only (RFC 2578 sec 7.3)
} SnmpObjectArgs;
/** @brief RFC 3418 sec 2: the system group under 1.3.6.1.2.1.1. */
typedef struct
{
    const char *descr;    ///< sysDescr.0
    const char *contact;  ///< sysContact.0
    const char *name;     ///< sysName.0
    const char *location; ///< sysLocation.0
    long services;        ///< sysServices.0, the layer bitmask
} SnmpSystemArgs;
/** @brief RFC 3417 sec 3.1: the serialized message read, and the one written back. */
typedef struct
{
    const uint8_t *req; ///< the received message octets
    size_t req_len;     ///< how many
    uint8_t *resp;      ///< where the response message is written
    size_t resp_cap;    ///< how many octets that holds
} SnmpMsgArgs;
/** @brief RFC 3416 sec 4.2: the request PDU dispatched, and the Response-PDU written. */
typedef struct
{
    const uint8_t *req;     ///< one complete request-PDU TLV
    size_t req_len;         ///< its length in octets
    uint8_t *out;           ///< where the Response-PDU TLV is written
    size_t out_cap;         ///< how many octets that holds
    proto_bool allow_write; ///< a SetRequest-PDU is authorized (RFC 3416 sec 4.2.5)
    proto_bool v2c;         ///< report per-binding exceptions rather than v1 error-status
} SnmpPduArgs;
/**
 * @brief The command responder: the MIB, the PDU processing, and the message framing.
 *
 * A caller sets the members a call takes, invokes it through ::SnmpAgent, and reads the outcome
 * off the same handle.
 *
 * @var SnmpAgentNs::port           the UDP port a listen binds, 161 by convention (RFC 3417 sec 3.2)
 * @var SnmpAgentNs::community      the communities a message is authenticated against
 * @var SnmpAgentNs::object         the managed object a registration binds
 * @var SnmpAgentNs::system         the system group values (RFC 3418 sec 2)
 * @var SnmpAgentNs::msg            the message read and the one written back
 * @var SnmpAgentNs::pdu            the PDU dispatched and the Response-PDU written
 * @var SnmpAgentNs::ok             a registration's true/false outcome: the table had room
 * @var SnmpAgentNs::n              octets written, 0 to send nothing
 * @var SnmpAgentNs::init           empty the MIB and take the read-only community
 * @var SnmpAgentNs::set_rw_community  take the community that authorizes a SetRequest-PDU
 * @var SnmpAgentNs::set_system     register the system group (RFC 3418 sec 2)
 * @var SnmpAgentNs::add_string     register an object whose value is an OCTET STRING
 * @var SnmpAgentNs::add_integer    register an object whose value is an INTEGER
 * @var SnmpAgentNs::add_dynamic    register an object whose value is read through a getter
 * @var SnmpAgentNs::dispatch_pdu   run one request PDU against the MIB and write a Response-PDU
 * @var SnmpAgentNs::process        decode one message, dispatch it, and frame the response message
 * @var SnmpAgentNs::listen         answer requests arriving on @c port
 */
typedef struct
{
    uint16_t port;               ///< the UDP port a listen binds
    SnmpCommunityArgs community; ///< what authenticates a message (RFC 1157 sec 3.2.5)
    SnmpObjectArgs object;       ///< what a registration binds (RFC 2578 sec 7)
    SnmpSystemArgs system;       ///< the system group values (RFC 3418 sec 2)
    SnmpMsgArgs msg;             ///< the message pair (RFC 3417 sec 3.1)
    SnmpPduArgs pdu;             ///< the PDU pair (RFC 3416 sec 4.2)
    proto_bool ok;
    size_t n;
} SnmpAgentVars;

/** @brief The operands and the outcome. */
extern SnmpAgentVars SnmpAgentV;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const set_rw_community)(uint8_t *restrict work);
    void (*const set_system)(uint8_t *restrict work);
    void (*const add_string)(uint8_t *restrict work);
    void (*const add_integer)(uint8_t *restrict work);
    void (*const add_dynamic)(uint8_t *restrict work);
    void (*const dispatch_pdu)(uint8_t *restrict work);
    void (*const process)(uint8_t *restrict work);
    void (*const listen)(uint8_t *restrict work);
} SnmpAgentNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SnmpAgentV or a region of the borrow at a fixed offset.
void protocore_snmp_agent_init(uint8_t *restrict work);
void protocore_snmp_agent_set_rw_community(uint8_t *restrict work);
void protocore_snmp_agent_set_system(uint8_t *restrict work);
void protocore_snmp_agent_add_string(uint8_t *restrict work);
void protocore_snmp_agent_add_integer(uint8_t *restrict work);
void protocore_snmp_agent_add_dynamic(uint8_t *restrict work);
void protocore_snmp_agent_dispatch_pdu(uint8_t *restrict work);
void protocore_snmp_agent_process(uint8_t *restrict work);
void protocore_snmp_agent_listen(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `SnmpAgent.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SnmpAgentNs SnmpAgent __attribute__((unused)) = {
    .init = protocore_snmp_agent_init,
    .set_rw_community = protocore_snmp_agent_set_rw_community,
    .set_system = protocore_snmp_agent_set_system,
    .add_string = protocore_snmp_agent_add_string,
    .add_integer = protocore_snmp_agent_add_integer,
    .add_dynamic = protocore_snmp_agent_add_dynamic,
    .dispatch_pdu = protocore_snmp_agent_dispatch_pdu,
    .process = protocore_snmp_agent_process,
    .listen = protocore_snmp_agent_listen,
};

/**
 * @brief The PROTOCORE_SNMP_AGENT_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_snmp_agent_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SNMP

#endif // PROTOCORE_SNMP_AGENT_H
