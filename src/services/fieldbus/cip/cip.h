// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cip.h
 * @brief CIP (Common Industrial Protocol) message codec (PROTOCORE_ENABLE_CIP) - zero-heap
 *        request builder + response parser for the message that rides inside an EtherNet/IP
 *        Unconnected Data item (services/fieldbus/enip). Together they form a working CIP read path.
 *
 * A CIP message request is:
 * @code
 *   Service(1)  RequestPathSize(1, in 16-bit words)  RequestPath(EPATH)  ServiceData
 * @endcode
 * The EPATH addresses an object with logical segments. A logical segment byte is
 * `0x20 | logical-type | format`, where logical-type is class (0x00), instance (0x04), or
 * attribute (0x10), and format is 8-bit (0x00, then a 1-octet id) or 16-bit (0x01, then a
 * pad octet and a little-endian 2-octet id). A response is `Service|0x80  reserved(0)
 * GeneralStatus(1)  AdditionalStatusSize(1, words)  [additional status]  ServiceData`.
 *
 * Service codes + the logical-segment encoding are verified against the Wireshark CIP
 * dissector. This codec is the CIP message; wrap it with `Enip.build_send_rr_data`.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CIP_H
#define PROTOCORE_CIP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_CIP

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// Common service codes.
#define CIP_SC_GET_ATTR_ALL 0x01
#define CIP_SC_GET_ATTR_LIST 0x03
#define CIP_SC_SET_ATTR_LIST 0x04
#define CIP_SC_GET_ATTR_SINGLE 0x0E
#define CIP_SC_SET_ATTR_SINGLE 0x10
#define CIP_REPLY_FLAG 0x80 ///< OR'd into the service code in a reply

// Logical-segment EPATH encoding (segment byte = base | logical-type | format).
#define CIP_SEG_LOGICAL 0x20   ///< logical segment, segment-type bits
#define CIP_SEG_CLASS 0x00     ///< logical type: class id
#define CIP_SEG_INSTANCE 0x04  ///< logical type: instance id
#define CIP_SEG_ATTRIBUTE 0x10 ///< logical type: attribute id
#define CIP_SEG_8BIT 0x00      ///< format: an 8-bit id follows
#define CIP_SEG_16BIT 0x01     ///< format: a pad octet then a 16-bit (LE) id follows

#define CIP_STATUS_SUCCESS 0x00 ///< General Status: success

/** @brief A parsed CIP response. @ref data points INTO the source buffer. */
typedef struct
{
    uint8_t service;        ///< reply service (the 0x80 reply bit is set)
    uint8_t general_status; ///< CIP_STATUS_SUCCESS on success
    const uint8_t *data;    ///< service data (the attribute value on a read)
    size_t data_len;
} CipResponse;

/** @brief What build_epath takes: buf, cap, class_id, instance_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t class_id;
    uint16_t instance_id;
    uint16_t attribute_id;
    proto_bool with_attribute; ///< include the attribute segment
} CipBuildEpathArgs;

/** @brief What build_request takes: buf, cap, service, epath, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t service;
    const uint8_t *epath;
    size_t epath_len;
    const uint8_t *data;
    size_t data_len;
} CipBuildRequestArgs;

/** @brief What build_get_attr_single takes: buf, cap, class_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t class_id;
    uint16_t instance_id;
    uint16_t attribute_id;
} CipBuildGetAttrSingleArgs;

/** @brief What build_get_attr_all takes: buf, cap, class_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t class_id;
    uint16_t instance_id;
} CipBuildGetAttrAllArgs;

/** @brief What build_set_attr_single takes: buf, cap, class_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t class_id;
    uint16_t instance_id;
    uint16_t attribute_id;
    const uint8_t *value;
    size_t value_len;
} CipBuildSetAttrSingleArgs;

/** @brief What parse_response takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    CipResponse *out;
} CipParseResponseArgs;

/**
 * @brief CIP (Common Industrial Protocol) message codec (PROTOCORE_ENABLE_CIP) - zero-heap request builder + response
 * parser for the message that rides inside an EtherNet/IP Unconnected Data item (services/fieldbus/enip).
 *
 * A caller sets the members a call takes, invokes it through ::Cip with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Cip.build_epath_args.buf = ...;
 *   Cip.build_epath_args.cap = ...;
 *   Cip.build_epath_args.class_id = ...;
 *   Cip.build_epath_args.instance_id = ...;
 *   Cip.build_epath_args.attribute_id = ...;
 *   Cip.build_epath_args.with_attribute = ...;
 *   Cip.build_epath(work);
 *   // Cip.n is what the call reports
 *
 * @var CipNs::build_epath_args  what build_epath takes: buf, cap, class_id, instance_id,
 * @var CipNs::build_request_args  what build_request takes: buf, cap, service, epath,
 * @var CipNs::build_get_attr_single_args  what build_get_attr_single takes: buf, cap, class_id,
 * @var CipNs::build_get_attr_all_args  what build_get_attr_all takes: buf, cap, class_id,
 * @var CipNs::build_set_attr_single_args  what build_set_attr_single takes: buf, cap, class_id,
 * @var CipNs::parse_response_args  what parse_response takes: buf, len, out
 * @var CipNs::ok  a call's true/false outcome
 * @var CipNs::n  EPATH length in octets (always even / word-aligned), or 0 on ...
 * @var CipNs::build_epath  build a class/instance[/attribute] EPATH (logical segments) into buf
 * @var CipNs::build_request  build a CIP request: service + path size (words) + EPATH + service ...
 * @var CipNs::build_get_attr_single  build a Get_Attribute_Single request for class/instance/attribute
 * @var CipNs::build_get_attr_all  build a Get_Attributes_All request for class/instance: service 0x01 ...
 * @var CipNs::build_set_attr_single  build a Set_Attribute_Single request for class/instance/attribute ...
 * @var CipNs::parse_response  parse a CIP response (service + status + additional status + data)
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    CipBuildEpathArgs build_epath_args;
    CipBuildRequestArgs build_request_args;
    CipBuildGetAttrSingleArgs build_get_attr_single_args;
    CipBuildGetAttrAllArgs build_get_attr_all_args;
    CipBuildSetAttrSingleArgs build_set_attr_single_args;
    CipParseResponseArgs parse_response_args;
    proto_bool ok;
    size_t n;
} CipVars;

/** @brief The operands and the outcome. */
extern CipVars CipV;

/** @brief The entries. */
typedef struct
{
    void (*const build_epath)(uint8_t *restrict work);
    void (*const build_request)(uint8_t *restrict work);
    void (*const build_get_attr_single)(uint8_t *restrict work);
    void (*const build_get_attr_all)(uint8_t *restrict work);
    void (*const build_set_attr_single)(uint8_t *restrict work);
    void (*const parse_response)(uint8_t *restrict work);
} CipNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in CipV or a region of the borrow at a fixed offset.
void protocore_cip_build_epath(uint8_t *restrict work);
void protocore_cip_build_request(uint8_t *restrict work);
void protocore_cip_build_get_attr_single(uint8_t *restrict work);
void protocore_cip_build_get_attr_all(uint8_t *restrict work);
void protocore_cip_build_set_attr_single(uint8_t *restrict work);
void protocore_cip_parse_response(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Cip.build_epath(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const CipNs Cip __attribute__((unused)) = {
    .build_epath = protocore_cip_build_epath,
    .build_request = protocore_cip_build_request,
    .build_get_attr_single = protocore_cip_build_get_attr_single,
    .build_get_attr_all = protocore_cip_build_get_attr_all,
    .build_set_attr_single = protocore_cip_build_set_attr_single,
    .parse_response = protocore_cip_parse_response,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CIP

#endif // PROTOCORE_CIP_H
