// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mqtt_sn.h
 * @brief MQTT-SN v1.2 wire codec (PC_ENABLE_MQTT_SN) - zero-heap message builder +
 *        parser for MQTT for Sensor Networks, the UDP / non-TCP MQTT variant for
 *        constrained, lossy links (topic IDs instead of strings, gateway discovery,
 *        sleeping-client keep-alive).
 *
 * Spec: MQTT-SN v1.2 (OASIS contribution). Each message is:
 * @code
 *   [Length][MsgType][message body...]
 * @endcode
 *  - Length is 1 octet when the total message length (the Length field included) is
 *    <= 255; otherwise it is 3 octets: `0x01` then a big-endian uint16 total length.
 *  - All multi-byte integers (TopicId, MsgId, Duration) are big-endian (MSB first).
 *  - TopicId / MsgId are 2 octets; topics may be a registered numeric TopicId, a
 *    pre-defined TopicId, or a 2-character short topic name (per the Flags TopicIdType).
 *
 * Verified against the Eclipse Paho MQTT-SN reference (message-type enum + length
 * encoding) and the v1.2 spec. This is the wire codec only; the gateway connection,
 * topic registry, and retransmission/sleep state are the application's.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MQTT_SN_H
#define PROTOCORE_MQTT_SN_H

#include "protocore_config.h"

#if PC_ENABLE_MQTT_SN

#include <stddef.h>
#include <stdint.h>

#define MQTTSN_LEN3_PREFIX 0x01 ///< a first Length octet of 0x01 signals the 3-octet length form

// Message types (MQTT-SN v1.2 Table 5).
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

// Flags octet bit layout (MQTT-SN v1.2 sec 5.3.4).
#define MQTTSN_FLAG_DUP 0x80
#define MQTTSN_FLAG_QOS_MASK 0x60
#define MQTTSN_FLAG_QOS_SHIFT 5
#define MQTTSN_FLAG_RETAIN 0x10
#define MQTTSN_FLAG_WILL 0x08
#define MQTTSN_FLAG_CLEAN 0x04
#define MQTTSN_FLAG_TOPICIDTYPE_MASK 0x03

// TopicIdType values (low 2 bits of Flags).
#define MQTTSN_TOPIC_NORMAL 0x00     ///< registered numeric topic id (REGISTER)
#define MQTTSN_TOPIC_PREDEFINED 0x01 ///< pre-defined numeric topic id
#define MQTTSN_TOPIC_SHORT 0x02      ///< 2-character short topic name

// Return codes (MQTT-SN v1.2 sec 5.3.5).
#define MQTTSN_RC_ACCEPTED 0x00
#define MQTTSN_RC_CONGESTION 0x01
#define MQTTSN_RC_INVALID_TOPIC_ID 0x02
#define MQTTSN_RC_NOT_SUPPORTED 0x03

#define MQTTSN_PROTOCOL_ID 0x01 ///< CONNECT ProtocolId octet

/** @brief Compose a Flags octet. @p qos is 0..3 (3 = QoS -1); @p topic_id_type is MQTTSN_TOPIC_*. */
uint8_t pc_mqttsn_make_flags(bool dup, uint8_t qos, bool retain, bool will, bool clean, uint8_t topic_id_type);

// ---- builders (return total bytes written, or 0 on overflow / bad input) ----

/** @brief CONNECT: Flags, ProtocolId(=1), Duration, ClientId. */
size_t pc_mqttsn_build_connect(uint8_t *buf, size_t cap, uint8_t flags, uint16_t duration, const char *client_id);

/** @brief REGISTER: TopicId (0x0000 from a client), MsgId, TopicName. */
size_t pc_mqttsn_build_register(uint8_t *buf, size_t cap, uint16_t topic_id, uint16_t msg_id, const char *topic_name);

/** @brief REGACK: TopicId, MsgId, ReturnCode. */
size_t pc_mqttsn_build_regack(uint8_t *buf, size_t cap, uint16_t topic_id, uint16_t msg_id, uint8_t ret_code);

/** @brief PUBLISH: Flags, TopicId, MsgId, Data. */
size_t pc_mqttsn_build_publish(uint8_t *buf, size_t cap, uint8_t flags, uint16_t topic_id, uint16_t msg_id,
                               const uint8_t *data, size_t data_len);

/** @brief PUBACK: TopicId, MsgId, ReturnCode. */
size_t pc_mqttsn_build_puback(uint8_t *buf, size_t cap, uint16_t topic_id, uint16_t msg_id, uint8_t ret_code);

/** @brief SUBSCRIBE by topic name: Flags, MsgId, TopicName (set TopicIdType normal/short in @p flags). */
size_t pc_mqttsn_build_subscribe_name(uint8_t *buf, size_t cap, uint8_t flags, uint16_t msg_id, const char *topic_name);

/** @brief SUBSCRIBE by pre-defined topic id: Flags, MsgId, TopicId (TopicIdType predefined). */
size_t pc_mqttsn_build_subscribe_id(uint8_t *buf, size_t cap, uint8_t flags, uint16_t msg_id, uint16_t topic_id);

/** @brief PINGREQ: optional ClientId (nullptr for an empty keep-alive ping). */
size_t pc_mqttsn_build_pingreq(uint8_t *buf, size_t cap, const char *client_id);

/** @brief DISCONNECT: optional sleep Duration (pass with_duration=false for a plain disconnect). */
size_t pc_mqttsn_build_disconnect(uint8_t *buf, size_t cap, bool with_duration, uint16_t duration);

/** @brief SEARCHGW: broadcast Radius. */
size_t pc_mqttsn_build_searchgw(uint8_t *buf, size_t cap, uint8_t radius);

// ---- parsing ----

/** @brief A decoded message header: type + a slice of the body (past the MsgType octet). */
struct MqttsnHeader
{
    uint8_t msg_type;
    const uint8_t *payload; ///< message body (points INTO the source buffer)
    size_t payload_len;
};

/**
 * @brief Parse the Length + MsgType header at the head of [buf, buf+len).
 * @param consumed receives the full message length (so the caller can advance).
 * @return true on a complete, self-consistent message; false if incomplete / malformed.
 */
bool pc_mqttsn_parse_header(const uint8_t *buf, size_t len, MqttsnHeader *out, size_t *consumed);

// The typed parsers below take the @ref MqttsnHeader payload/payload_len.
bool pc_mqttsn_parse_connack(const uint8_t *payload, size_t len, uint8_t *ret_code);
bool pc_mqttsn_parse_regack(const uint8_t *payload, size_t len, uint16_t *topic_id, uint16_t *msg_id,
                            uint8_t *ret_code);
bool pc_mqttsn_parse_puback(const uint8_t *payload, size_t len, uint16_t *topic_id, uint16_t *msg_id,
                            uint8_t *ret_code);
bool pc_mqttsn_parse_suback(const uint8_t *payload, size_t len, uint8_t *flags, uint16_t *topic_id, uint16_t *msg_id,
                            uint8_t *ret_code);
bool pc_mqttsn_parse_publish(const uint8_t *payload, size_t len, uint8_t *flags, uint16_t *topic_id, uint16_t *msg_id,
                             const uint8_t **data, size_t *data_len);
bool pc_mqttsn_parse_register(const uint8_t *payload, size_t len, uint16_t *topic_id, uint16_t *msg_id,
                              const char **topic_name, size_t *topic_name_len);

#endif // PC_ENABLE_MQTT_SN

#endif // PROTOCORE_MQTT_SN_H
