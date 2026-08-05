// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_snmp_agent.h
 * @brief Zero-heap SNMP v1/v2c agent: PDU processing + a fixed MIB table.
 *
 * The agent is split into a pure, host-testable core and an ESP32-only UDP
 * transport (mirroring how the provisioning service splits its form parser from
 * its lwIP DNS responder):
 *
 *  - pc_snmp_agent_process() takes a complete request datagram and produces a
 *    complete response datagram in a caller buffer - no sockets, no heap. It is
 *    unit-tested on the host (env:native_snmp).
 *  - pc_snmp_agent_begin_udp() binds the agent on :161 via the pc_udp_* transport
 *    API (Arduino only) and feeds received datagrams through pc_snmp_agent_process().
 *
 * The MIB is a fixed BSS table of SNMP_MAX_MIB_ENTRIES objects. Register objects
 * with pc_snmp_agent_add_*; values are either static (stored in the entry) or
 * fetched through a getter callback (e.g. sysUpTime). All string/OID values are
 * referenced by pointer and must outlive the agent (point them at flash/static
 * data, exactly like the rest of the library's strings).
 *
 * Get / GetNext / GetBulk / Set are supported. GetBulk and per-varbind
 * exceptions (noSuchObject / noSuchInstance / endOfMibView) apply to v2c; v1 uses
 * the classic error-status / error-index reporting. SNMPv3 (USM) is a separate,
 * gated layer.
 */

#ifndef PROTOCORE_SNMP_AGENT_H
#define PROTOCORE_SNMP_AGENT_H

#include "protocore_config.h"
#include "services/net/snmp/snmp_ber.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SNMP

// SNMP message versions (the on-wire INTEGER value).
typedef enum PROTO_ENUM_PACKED
{
    SNMP_V1 = 0,
    SNMP_V2C = 1,
    SNMP_V3 = 3,
} SnmpVersion;

// PDU error-status values (RFC 1157 / RFC 3416).
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
 * @brief A typed SNMP value (a varbind's value, or a MIB object's value).
 *
 * Only the field matching @ref type is meaningful. String and OID values are
 * referenced by pointer and are not copied - they must remain valid.
 */
typedef struct
{
    uint8_t type;        ///< BER tag: SNMP_TAG_BER_INTEGER / SNMP_TAG_BER_OCTET_STRING / SNMP_TAG_BER_OID /
                         ///< SNMP_TAG_SNMP_TIMETICKS / SNMP_TAG_SNMP_COUNTER32 / SNMP_TAG_SNMP_GAUGE32 /
                         ///< SNMP_TAG_SNMP_IPADDRESS, or an exception tag.
    long ival;           ///< value for SNMP_TAG_BER_INTEGER
    uint32_t uval;       ///< value for SNMP_TAG_SNMP_TIMETICKS / SNMP_TAG_SNMP_COUNTER32 / SNMP_TAG_SNMP_GAUGE32 /
                         ///< SNMP_TAG_SNMP_IPADDRESS
    const char *str;     ///< bytes for SNMP_TAG_BER_OCTET_STRING (not owned)
    size_t str_len;      ///< length of @ref str
    const uint32_t *oid; ///< arcs for SNMP_TAG_BER_OID (not owned)
    size_t oid_len;      ///< number of arcs in @ref oid
} SnmpValue;

/** @brief Dynamic value getter; fill @p out and return true, or return false for noSuchInstance. */
typedef proto_bool (*SnmpGetFn)(SnmpValue *out);
/** @brief Value setter for a writable object; return true on success, false to reject (wrongType/badValue). */
typedef proto_bool (*SnmpSetFn)(const SnmpValue *in);

// ---------------------------------------------------------------------------
// Agent configuration / MIB registration
// ---------------------------------------------------------------------------

/**
 * @brief Reset the agent and set the read-only community (default "public").
 *
 * Clears the MIB table. Call before registering objects. Pass nullptr to keep
 * the default community.
 */
void pc_snmp_agent_init(const char *ro_community);

/** @brief Set the read-write community used to authorize Set requests (default: none -> Sets refused). */
void pc_snmp_agent_set_rw_community(const char *rw_community);

/**
 * @brief Populate the standard MIB-II system group (1.3.6.1.2.1.1).
 *
 * Adds sysDescr.0, sysObjectID.0, sysUpTime.0 (dynamic, hundredths of a second
 * since boot), sysContact.0, sysName.0, sysLocation.0 and sysServices.0. The
 * string arguments are referenced by pointer (not copied). @p services is the
 * sysServices bitmask (commonly 72 = application + internet layers).
 */
void pc_snmp_agent_set_system(const char *descr, const char *contact, const char *name, const char *location,
                              long services);

/** @brief Register a static OCTET STRING object. @return false if the table is full. */
proto_bool pc_snmp_agent_add_string(const uint32_t *oid, size_t oid_len, const char *value, SnmpSetFn setter);
/** @brief Register a static INTEGER object. @return false if the table is full. */
proto_bool pc_snmp_agent_add_integer(const uint32_t *oid, size_t oid_len, long value, SnmpSetFn setter);
/** @brief Register a dynamic object served by @p getter (@p type names the value's BER tag). */
proto_bool pc_snmp_agent_add_dynamic(const uint32_t *oid, size_t oid_len, uint8_t type, SnmpGetFn getter);

// ---------------------------------------------------------------------------
// Core processing (host-testable; no sockets, no heap)
// ---------------------------------------------------------------------------

/**
 * @brief Process one SNMP request datagram and build the response datagram.
 *
 * Decodes the v1/v2c message, dispatches the PDU against the MIB, and encodes a
 * Response PDU into @p resp. A request with an unknown community, a malformed
 * message, or a v3 message (when SNMPv3 is not enabled) yields no response.
 *
 * @param req      request datagram bytes.
 * @param req_len  number of bytes in @p req.
 * @param resp     destination buffer for the response datagram.
 * @param pc_resp_cap capacity of @p resp.
 * @return number of response bytes written, or 0 to send nothing.
 */
size_t pc_snmp_agent_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap);

/**
 * @brief Process one request PDU against the MIB and emit a GetResponse PDU.
 *
 * The shared dispatch core used by both the v1/v2c community framing and the v3
 * USM layer: it decodes a Get / GetNext / GetBulk / Set PDU TLV, runs it against
 * the registered MIB, and encodes a single GetResponse PDU TLV.
 *
 * @param pdu         a complete request-PDU TLV.
 * @param pdu_len     length of @p pdu.
 * @param allow_write authorize Set (true for the read-write community / a v3 user with write access).
 * @param v2c         use v2c-style per-varbind exceptions (true for v2c and v3); false = v1 error-status.
 * @param out         destination for the response PDU TLV.
 * @param out_cap     capacity of @p out.
 * @return number of response-PDU bytes written, or 0 on a malformed/unsupported PDU.
 */
size_t pc_snmp_dispatch_pdu(const uint8_t *pdu, size_t pdu_len, proto_bool allow_write, proto_bool v2c, uint8_t *out,
                            size_t out_cap);

// ---------------------------------------------------------------------------
// UDP transport (ESP32 only; no-op stub elsewhere)
// ---------------------------------------------------------------------------

/**
 * @brief Bind the agent to UDP @p port (default 161) via the transport-layer UDP service.
 *
 * Callback-driven (no per-loop servicing). Call after WiFi is up. On non-Arduino
 * builds this is a no-op so the core remains host-testable.
 */
void pc_snmp_agent_begin_udp(uint16_t port);

#endif // PC_ENABLE_SNMP

PROTO_END_DECLS

#endif // PROTOCORE_SNMP_AGENT_H
