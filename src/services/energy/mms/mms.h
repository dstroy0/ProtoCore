// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mms.h
 * @brief IEC 61850 MMS (Manufacturing Message Specification) PDU codec (PC_ENABLE_MMS).
 *
 * MMS (ISO 9506) is the client/server core of IEC 61850: an ACSI object model (logical devices/nodes,
 * data objects, datasets) carried as BER-encoded MMS PDUs over ISO-on-TCP (TPKT + COTP, already shipped
 * as services/fieldbus/cotp) on port 102. This builds the two most-used PDUs:
 *
 *  - **confirmed-request / Read**: `[A0 { 02 invokeID  A4 confirmedServiceRequest { A4 read { A1
 *    variableAccessSpecification { A0 listOfVariable { 30 { A0 objectName { 1A domain-specific { ... }}}}}}}]`
 *    - a request to read one named variable (a Data Object reference like "LD0/GGIO1$ST$Ind1$stVal").
 *  - **confirmed-response / Read data**: the response carrying the AccessResult data value.
 *
 * This is the MMS PDU framing + the Read request/response builders (the variable name passed as a MMS
 * ObjectName VisibleString; the response data as a caller-encoded BER blob). Pure, zero heap, no stdlib,
 * host-testable; TPKT/COTP wrapping + the socket are services/fieldbus/cotp + the transport.
 */

#ifndef PROTOCORE_MMS_H
#define PROTOCORE_MMS_H

#include "protocore_config.h"

#if PC_ENABLE_MMS

/** @brief MMS PDU tags (context-specific) + the service tags used here. */
// MMS PDU / service / BER tags: wire bytes, so integer constants in a namespacing struct.
#define MMS_PDU_CONFIRMED_REQUEST 0xA0
#define MMS_PDU_CONFIRMED_RESPONSE 0xA1
#define MMS_PDU_CONFIRMED_ERROR 0xA2
#define MMS_SERVICE_READ 0xA4  ///< confirmedServiceRequest/Response [4] read.
#define MMS_SERVICE_WRITE 0xA5 ///< [5] write.
#define MMS_TAG_INVOKE_ID 0x02 ///< Unsigned32 invokeID (INTEGER tag).

/**
 * @brief Build an MMS confirmed-request Read PDU for one named variable.
 * @param invoke_id the invoke id (echoed in the response).
 * @param item_name the MMS ObjectName (e.g. "LD0/GGIO1$ST$Ind1$stVal"), a VisibleString.
 * @return the PDU length written, or 0 on overflow / bad args.
 */
size_t pc_mms_read_request(uint32_t invoke_id, const char *item_name, uint8_t *out, size_t cap);

/**
 * @brief Build an MMS confirmed-response Read PDU carrying one pre-encoded AccessResult data value.
 * @param invoke_id the invoke id from the request.
 * @param data      a caller-encoded BER Data value (e.g. `85 01 01` for a boolean-ish, or an integer);
 *                  wrapped in the listOfAccessResult.
 * @param data_len  its length.
 * @return the PDU length written, or 0 on overflow.
 */
size_t pc_mms_read_response(uint32_t invoke_id, const uint8_t *data, size_t data_len, uint8_t *out, size_t cap);

/** @brief A parsed MMS confirmed PDU (top-level). */
typedef struct
{
    uint8_t pdu_tag;             ///< MMS_PDU_CONFIRMED_REQUEST / _RESPONSE / _ERROR.
    uint32_t invoke_id;          ///< the decoded invokeID.
    uint8_t service_tag;         ///< the confirmedService tag (MMS_SERVICE_READ, ...), or 0 if none.
    const uint8_t *service_body; ///< the service content (points into the input), or null.
    size_t service_len;
} MmsPdu;

/** @brief Parse an MMS confirmed PDU header (pdu tag + invokeID + service tag). @return true if well-formed. */
proto_bool pc_mms_parse(const uint8_t *pdu, size_t len, MmsPdu *out);

#endif // PC_ENABLE_MMS
#endif // PROTOCORE_MMS_H
