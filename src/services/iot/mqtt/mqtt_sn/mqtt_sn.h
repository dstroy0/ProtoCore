// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mqtt_sn.h
 * @brief The MQTT-SN v1.2 wire codec (PROTOCORE_ENABLE_MQTT_SN).
 *
 * The governing document is "MQTT For Sensor Networks (MQTT-SN) Protocol Specification Version 1.2",
 * by Andy Stanford-Clark and Hong Linh Truong, published by IBM on 14 November 2013. **It is not an
 * OASIS Standard and it is not an IETF protocol: it carries neither a standard number nor an RFC
 * number.** OASIS hosts the document for its MQTT Technical Committee's MQTT-SN Subcommittee, whose
 * own MQTT-SN Version 2.0 work is a separate specification. Every section, table and message name
 * cited here is from that v1.2 document.
 *
 * MQTT-SN carries publish/subscribe over a datagram link for constrained, lossy networks: numeric
 * topic ids instead of topic names, gateway discovery, and a keep-alive that supports sleeping
 * clients (sec 2, sec 3). A message is:
 * @code
 *   [Length][MsgType][Message Variable Part]
 * @endcode
 *  - Length is 1 octet when the whole message, the Length field included, is at most 255 octets;
 *    otherwise it is 3 octets: ::MQTTSN_LEN3_PREFIX then a big-endian uint16 of that same total
 *    (sec 5.2.1). The 3-octet form reaches 65535 octets.
 *  - MsgType is one octet, from Table 3 (sec 5.2.2).
 *  - TopicId, MsgId and Duration are 2 octets, most significant octet first (sec 5.3.3, sec 5.3.7,
 *    sec 5.3.11). A topic is named by a registered TopicId, a pre-defined TopicId, or a 2-character
 *    short topic name, as the Flags TopicIdType states (sec 5.3.4).
 *
 * This is the wire codec only. The gateway connection, the topic registry, and the retransmission
 * and sleep state (sec 6.13, sec 6.14) belong to the application.
 *
 * The module exports one symbol, @ref Mqttsn. Everything in mqtt_sn.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MQTT_SN_H
#define PROTOCORE_MQTT_SN_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_MQTT_SN

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

#define MQTTSN_LEN3_PREFIX 0x01 ///< a first Length octet of 0x01 signals the 3-octet form (sec 5.2.1)

// MsgType values (MQTT-SN v1.2 sec 5.2.2, Table 3). The types this codec neither builds nor parses -
// WILLTOPICUPD 0x1A, WILLTOPICRESP 0x1B, WILLMSGUPD 0x1C, WILLMSGRESP 0x1D, and the encapsulated
// message 0xFE - are named by the table but not listed here.
#define MQTTSN_ADVERTISE 0x00
#define MQTTSN_SEARCHGW 0x01
#define MQTTSN_GWINFO 0x02
#define MQTTSN_CONNECT 0x04
#define MQTTSN_CONNACK 0x05
#define MQTTSN_WILLTOPICREQ 0x06
#define MQTTSN_WILLTOPIC 0x07
#define MQTTSN_WILLMSGREQ 0x08
#define MQTTSN_WILLMSG 0x09
#define MQTTSN_REGISTER 0x0A
#define MQTTSN_REGACK 0x0B
#define MQTTSN_PUBLISH 0x0C
#define MQTTSN_PUBACK 0x0D
#define MQTTSN_PUBCOMP 0x0E
#define MQTTSN_PUBREC 0x0F
#define MQTTSN_PUBREL 0x10
#define MQTTSN_SUBSCRIBE 0x12
#define MQTTSN_SUBACK 0x13
#define MQTTSN_UNSUBSCRIBE 0x14
#define MQTTSN_UNSUBACK 0x15
#define MQTTSN_PINGREQ 0x16
#define MQTTSN_PINGRESP 0x17
#define MQTTSN_DISCONNECT 0x18

// Flags octet bit layout (MQTT-SN v1.2 sec 5.3.4, Table 4).
#define MQTTSN_FLAG_DUP 0x80              ///< DUP, bit 7
#define MQTTSN_FLAG_QOS_MASK 0x60         ///< QoS, bits 6-5
#define MQTTSN_FLAG_QOS_SHIFT 5           ///< how far QoS sits from bit 0
#define MQTTSN_FLAG_RETAIN 0x10           ///< Retain, bit 4
#define MQTTSN_FLAG_WILL 0x08             ///< Will, bit 3
#define MQTTSN_FLAG_CLEAN 0x04            ///< CleanSession, bit 2
#define MQTTSN_FLAG_TOPICIDTYPE_MASK 0x03 ///< TopicIdType, bits 1-0

// TopicIdType values, the low two bits of Flags (MQTT-SN v1.2 sec 5.3.4).
#define MQTTSN_TOPIC_NORMAL 0x00     ///< a registered numeric topic id (REGISTER, sec 6.5)
#define MQTTSN_TOPIC_PREDEFINED 0x01 ///< a pre-defined numeric topic id (sec 6.7)
#define MQTTSN_TOPIC_SHORT 0x02      ///< a 2-character short topic name (sec 6.7)

// ReturnCode values (MQTT-SN v1.2 sec 5.3.10, Table 5).
#define MQTTSN_RC_ACCEPTED 0x00
#define MQTTSN_RC_CONGESTION 0x01
#define MQTTSN_RC_INVALID_TOPIC_ID 0x02
#define MQTTSN_RC_NOT_SUPPORTED 0x03

#define MQTTSN_PROTOCOL_ID 0x01 ///< the CONNECT ProtocolId octet; all other values are reserved (sec 5.3.8)

// ---------------------------------------------------------------------------
// Typedefs
// ---------------------------------------------------------------------------

/** @brief MQTT-SN v1.2 sec 5.3.4: the six fields the Flags octet packs. */
typedef struct
{
    proto_bool dup;           ///< DUP: the message is a retransmission
    uint8_t qos;              ///< QoS: 0, 1, 2, or 3 for QoS level -1 (sec 6.8)
    proto_bool retain;        ///< Retain
    proto_bool will;          ///< Will: the client asks for Will prompting (CONNECT, sec 5.4.4)
    proto_bool clean_session; ///< CleanSession (sec 6.3)
    uint8_t topic_id_type;    ///< TopicIdType: MQTTSN_TOPIC_NORMAL, _PREDEFINED or _SHORT
    uint8_t octet;            ///< the packed Flags octet a compose writes and a parse reports
} MqttsnFlagsArgs;
/** @brief MQTT-SN v1.2 sec 5.3.11, sec 5.3.12: how a message names its topic. */
typedef struct
{
    uint16_t topic_id;      ///< TopicId; 0x0000 and 0xFFFF are reserved (sec 5.3.11)
    const char *topic_name; ///< TopicName a build writes, or where a parse found it (sec 5.3.12)
    size_t topic_name_len;  ///< its octet count as a parse reports it
} MqttsnTopicArgs;
/** @brief MQTT-SN v1.2 sec 5.3.1, sec 5.3.3, sec 5.3.7, sec 5.3.9, sec 5.3.10: the scalar fields. */
typedef struct
{
    const char *client_id;    ///< ClientId, 1 to 23 characters (sec 5.3.1)
    uint16_t duration;        ///< Duration in seconds (sec 5.3.3)
    proto_bool with_duration; ///< a DISCONNECT carries the sleep Duration (sec 5.4.21, sec 6.14)
    uint16_t msg_id;          ///< MsgId, matching a message to its acknowledgment (sec 5.3.7)
    uint8_t radius;           ///< Radius; 0x00 broadcasts to all nodes (sec 5.3.9)
    uint8_t return_code;      ///< ReturnCode (sec 5.3.10)
} MqttsnFieldArgs;
/** @brief MQTT-SN v1.2 sec 5.3.2: the Data a PUBLISH carries. */
typedef struct
{
    const uint8_t *data; ///< the application data a build writes, or where a parse found it
    size_t data_len;     ///< its octet count
} MqttsnDataArgs;
/** @brief MQTT-SN v1.2 sec 5.2: the Length and MsgType header, and the Variable Part behind it. */
typedef struct
{
    uint8_t msg_type;        ///< MsgType (sec 5.2.2)
    const uint8_t *variable; ///< the Message Variable Part, pointing into @c in (sec 5.3)
    size_t variable_len;     ///< its octet count
} MqttsnHeaderArgs;
/**
 * @brief The octets a build writes or a parse reads.
 *
 * A header parse reads a whole message from @c in; every typed parse below reads the Message
 * Variable Part the header parse pointed @c header.variable at.
 */
typedef struct
{
    uint8_t *out;      ///< where a build writes the whole message
    size_t cap;        ///< its room
    const uint8_t *in; ///< the octets a parse reads
    size_t avail;      ///< how many are readable there
} MqttsnBufArgs;
/**
 * @brief The MQTT-SN v1.2 wire codec.
 *
 * A caller sets the members a call takes, invokes it through ::Mqttsn, and reads the outcome off the
 * same handle.
 *
 * No slot member: every call works on the buffer the caller lends, so no call names a row.
 *
 * @var MqttsnNs::flags   the six fields the Flags octet packs (sec 5.3.4)
 * @var MqttsnNs::topic   how a message names its topic (sec 5.3.11, sec 5.3.12)
 * @var MqttsnNs::field   ClientId, Duration, MsgId, Radius and ReturnCode (sec 5.3)
 * @var MqttsnNs::data    the Data a PUBLISH carries (sec 5.3.2)
 * @var MqttsnNs::header  the Length and MsgType header a parse read (sec 5.2)
 * @var MqttsnNs::buf     the octets a build writes or a parse reads
 * @var MqttsnNs::ok      a call's true/false outcome
 * @var MqttsnNs::n
 * The whole message length: what a build wrote, or what a header parse consumed so the caller can
 * advance. 0 when a build did not fit @c cap or the message would exceed the 16-bit Length field.
 * @var MqttsnNs::make_flags
 * Pack @c flags.dup, @c flags.qos, @c flags.retain, @c flags.will, @c flags.clean_session and
 * @c flags.topic_id_type into @c flags.octet (sec 5.3.4).
 * @var MqttsnNs::build_connect
 * CONNECT: Flags, ProtocolId, Duration, ClientId (sec 5.4.4).
 * @var MqttsnNs::build_register
 * REGISTER: TopicId, MsgId, TopicName. A client codes TopicId 0x0000 (sec 5.4.10).
 * @var MqttsnNs::build_regack
 * REGACK: TopicId, MsgId, ReturnCode (sec 5.4.11).
 * @var MqttsnNs::build_publish
 * PUBLISH: Flags, TopicId, MsgId, Data (sec 5.4.12).
 * @var MqttsnNs::build_puback
 * PUBACK: TopicId, MsgId, ReturnCode (sec 5.4.13).
 * @var MqttsnNs::build_subscribe_name
 * SUBSCRIBE naming a TopicName: Flags, MsgId, TopicName. @c flags.octet states TopicIdType normal or
 * short (sec 5.4.15).
 * @var MqttsnNs::build_subscribe_id
 * SUBSCRIBE naming a pre-defined TopicId: Flags, MsgId, TopicId (sec 5.4.15).
 * @var MqttsnNs::build_pingreq
 * PINGREQ, with the optional ClientId a sleeping client includes when it wakes; a null
 * @c field.client_id builds the plain keep-alive form (sec 5.4.19, sec 6.14).
 * @var MqttsnNs::build_disconnect
 * DISCONNECT, carrying the sleep Duration when @c field.with_duration is set (sec 5.4.21, sec 6.14).
 * @var MqttsnNs::build_searchgw
 * SEARCHGW: Radius (sec 5.4.2).
 * @var MqttsnNs::parse_header
 * Read the Length and MsgType at the head of @c in into @c header, with @c n reporting the whole
 * message length so the caller can advance (sec 5.2). False on an incomplete or self-inconsistent
 * message.
 * @var MqttsnNs::parse_connack   CONNACK: ReturnCode into @c field.return_code (sec 5.4.5)
 * @var MqttsnNs::parse_regack
 * REGACK: TopicId, MsgId and ReturnCode into @c topic and @c field (sec 5.4.11).
 * @var MqttsnNs::parse_puback
 * PUBACK: the same three fields, which share REGACK's layout (sec 5.4.13).
 * @var MqttsnNs::parse_suback
 * SUBACK: Flags, TopicId, MsgId and ReturnCode; the granted QoS is in @c flags.octet (sec 5.4.16).
 * @var MqttsnNs::parse_publish
 * PUBLISH: Flags, TopicId, MsgId, and the Data slice into @c data (sec 5.4.12).
 * @var MqttsnNs::parse_register
 * REGISTER: TopicId, MsgId, and the TopicName slice into @c topic (sec 5.4.10).
 *
 * No storage member: every call works in the buffer the caller lends and holds nothing between
 * calls.
 */
typedef struct
{
    MqttsnFlagsArgs flags;   ///< the Flags octet's six fields
    MqttsnTopicArgs topic;   ///< how a message names its topic
    MqttsnFieldArgs field;   ///< the scalar fields of the Message Variable Part
    MqttsnDataArgs data;     ///< the Data a PUBLISH carries
    MqttsnHeaderArgs header; ///< the Length and MsgType header a parse read
    MqttsnBufArgs buf;       ///< the octets a call moves
    proto_bool ok;
    size_t n;
} MqttsnVars;

/** @brief The operands and the outcome. */
extern MqttsnVars MqttsnV;

/** @brief The entries. */
typedef struct
{
    void (*const make_flags)(uint8_t *restrict work);
    void (*const build_connect)(uint8_t *restrict work);
    void (*const build_register)(uint8_t *restrict work);
    void (*const build_regack)(uint8_t *restrict work);
    void (*const build_publish)(uint8_t *restrict work);
    void (*const build_puback)(uint8_t *restrict work);
    void (*const build_subscribe_name)(uint8_t *restrict work);
    void (*const build_subscribe_id)(uint8_t *restrict work);
    void (*const build_pingreq)(uint8_t *restrict work);
    void (*const build_disconnect)(uint8_t *restrict work);
    void (*const build_searchgw)(uint8_t *restrict work);
    void (*const parse_header)(uint8_t *restrict work);
    void (*const parse_connack)(uint8_t *restrict work);
    void (*const parse_regack)(uint8_t *restrict work);
    void (*const parse_puback)(uint8_t *restrict work);
    void (*const parse_suback)(uint8_t *restrict work);
    void (*const parse_publish)(uint8_t *restrict work);
    void (*const parse_register)(uint8_t *restrict work);
} MqttsnNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in MqttsnV or a region of the borrow at a fixed offset.
void protocore_mqtt_sn_make_flags(uint8_t *restrict work);
void protocore_mqtt_sn_build_connect(uint8_t *restrict work);
void protocore_mqtt_sn_build_register(uint8_t *restrict work);
void protocore_mqtt_sn_build_regack(uint8_t *restrict work);
void protocore_mqtt_sn_build_publish(uint8_t *restrict work);
void protocore_mqtt_sn_build_puback(uint8_t *restrict work);
void protocore_mqtt_sn_build_subscribe_name(uint8_t *restrict work);
void protocore_mqtt_sn_build_subscribe_id(uint8_t *restrict work);
void protocore_mqtt_sn_build_pingreq(uint8_t *restrict work);
void protocore_mqtt_sn_build_disconnect(uint8_t *restrict work);
void protocore_mqtt_sn_build_searchgw(uint8_t *restrict work);
void protocore_mqtt_sn_parse_header(uint8_t *restrict work);
void protocore_mqtt_sn_parse_connack(uint8_t *restrict work);
void protocore_mqtt_sn_parse_regack(uint8_t *restrict work);
void protocore_mqtt_sn_parse_puback(uint8_t *restrict work);
void protocore_mqtt_sn_parse_suback(uint8_t *restrict work);
void protocore_mqtt_sn_parse_publish(uint8_t *restrict work);
void protocore_mqtt_sn_parse_register(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Mqttsn.make_flags(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const MqttsnNs Mqttsn __attribute__((unused)) = {
    .make_flags = protocore_mqtt_sn_make_flags,
    .build_connect = protocore_mqtt_sn_build_connect,
    .build_register = protocore_mqtt_sn_build_register,
    .build_regack = protocore_mqtt_sn_build_regack,
    .build_publish = protocore_mqtt_sn_build_publish,
    .build_puback = protocore_mqtt_sn_build_puback,
    .build_subscribe_name = protocore_mqtt_sn_build_subscribe_name,
    .build_subscribe_id = protocore_mqtt_sn_build_subscribe_id,
    .build_pingreq = protocore_mqtt_sn_build_pingreq,
    .build_disconnect = protocore_mqtt_sn_build_disconnect,
    .build_searchgw = protocore_mqtt_sn_build_searchgw,
    .parse_header = protocore_mqtt_sn_parse_header,
    .parse_connack = protocore_mqtt_sn_parse_connack,
    .parse_regack = protocore_mqtt_sn_parse_regack,
    .parse_puback = protocore_mqtt_sn_parse_puback,
    .parse_suback = protocore_mqtt_sn_parse_suback,
    .parse_publish = protocore_mqtt_sn_parse_publish,
    .parse_register = protocore_mqtt_sn_parse_register,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MQTT_SN

#endif // PROTOCORE_MQTT_SN_H
