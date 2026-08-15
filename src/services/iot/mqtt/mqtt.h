// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mqtt.h
 * @brief The MQTT Client (PROTOCORE_ENABLE_MQTT).
 *
 * The governing document is "MQTT Version 3.1.1", an **OASIS Standard** dated 29 October 2014,
 * amended by "MQTT Version 3.1.1 Plus Errata 01" (OASIS Standard Incorporating Approved Errata 01,
 * 10 December 2015). MQTT is not an IETF protocol and carries no RFC number; every section cited
 * below is a section of that OASIS document. The same text is published as ISO/IEC 20922:2016.
 *
 * The Client and the Server exchange MQTT Control Packets over a Network Connection (sec 4.2). Each
 * packet is a fixed header (sec 2.2) carrying the Control Packet type (sec 2.2.1), four type-specific
 * flag bits (sec 2.2.2) and a Remaining Length (sec 2.2.3), then an optional variable header and
 * payload.
 *
 * Two halves behind one handle:
 *
 *  - The codec builds and reads packet octets in the caller's buffers and holds nothing, so it is
 *    unit-tested on the host.
 *  - The transport drives one Network Connection over the outbound TCP client, and `mqtts://` over
 *    the shared persistent client TLS session. No heap; one Server at a time.
 *
 * QoS 2 runs the four-packet PUBLISH / PUBREC / PUBREL / PUBCOMP exchange (sec 4.3.3). Outbound
 * QoS 1 and QoS 2 messages sit in a fixed in-flight pool and are re-delivered with DUP set
 * (sec 3.3.1.1) until acknowledged. An inbound QoS 2 Packet Identifier is held from PUBREC until
 * PUBCOMP so a repeat delivers the Application Message once (sec 4.3.3).
 *
 * The module exports one symbol, @ref Mqtt. Everything in mqtt.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MQTT_H
#define PROTOCORE_MQTT_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_MQTT

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

/** @brief The largest Remaining Length a four-octet field encodes (MQTT 3.1.1 sec 2.2.3). */
#define PROTOCORE_MQTT_REMAINING_LENGTH_MAX 268435455u

/** @brief The Protocol Level a 3.1.1 CONNECT carries (MQTT 3.1.1 sec 3.1.2.2). */
#define PROTOCORE_MQTT_PROTOCOL_LEVEL 0x04

/** @brief The SUBACK return code that reports a failed subscription (MQTT 3.1.1 sec 3.9.3). */
#define PROTOCORE_MQTT_SUBACK_FAILURE 0x80

// ---------------------------------------------------------------------------
// Typedefs
// ---------------------------------------------------------------------------

/** @brief MQTT Control Packet type, byte 1 bits 7-4 (MQTT 3.1.1 sec 2.2.1, Table 2.1). */
typedef enum PROTO_ENUM_PACKED
{
    MQTT_CONNECT = 1,      ///< Client request to connect to a Server
    MQTT_CONNACK = 2,      ///< Connect acknowledgment
    MQTT_PUBLISH = 3,      ///< Publish message
    MQTT_PUBACK = 4,       ///< Publish acknowledgment
    MQTT_PUBREC = 5,       ///< Publish received (QoS 2 publish received, part 1)
    MQTT_PUBREL = 6,       ///< Publish release (QoS 2 publish received, part 2)
    MQTT_PUBCOMP = 7,      ///< Publish complete (QoS 2 publish received, part 3)
    MQTT_SUBSCRIBE = 8,    ///< Client subscribe request
    MQTT_SUBACK = 9,       ///< Subscribe acknowledgment
    MQTT_UNSUBSCRIBE = 10, ///< Unsubscribe request
    MQTT_UNSUBACK = 11,    ///< Unsubscribe acknowledgment
    MQTT_PINGREQ = 12,     ///< PING request
    MQTT_PINGRESP = 13,    ///< PING response
    MQTT_DISCONNECT = 14,  ///< Client is disconnecting
} MqttType;

/** @brief Where an inbound PUBLISH's Topic Name and Payload are delivered (MQTT 3.1.1 sec 3.3). */
typedef void (*MqttMessageCb)(const char *topic_name, const uint8_t *payload, size_t payload_len);

/** @brief MQTT 3.1.1 sec 4.2: the Network Connection this Client opens to a Server. */
typedef struct
{
    const char *host;   ///< the Server's name; read on every connect step, so it outlives the call
    uint16_t port;      ///< its TCP port
    proto_bool use_tls; ///< the Network Connection runs over TLS (`mqtts://`)
} MqttServerArgs;

/**
 * @brief MQTT 3.1.1 sec 3.1: the CONNECT variable header and payload, less the Will.
 *
 * A null @c user_name clears the User Name flag (sec 3.1.2.8) and a null @c password clears the
 * Password flag (sec 3.1.2.9), so neither field is written into the payload.
 */
typedef struct
{
    const char *client_id;    ///< Client Identifier (sec 3.1.3.1); "" asks the Server to assign one
    const char *user_name;    ///< User Name (sec 3.1.3.4), or null for none
    const char *password;     ///< Password (sec 3.1.3.5), or null for none
    uint16_t keep_alive;      ///< Keep Alive seconds (sec 3.1.2.10); 0 turns the mechanism off
    proto_bool clean_session; ///< Clean Session (sec 3.1.2.4)
} MqttSessionArgs;

/**
 * @brief MQTT 3.1.1 sec 3.1.2.5 - sec 3.1.2.7, sec 3.1.3.2, sec 3.1.3.3: the Will a CONNECT carries.
 *
 * A null @c topic clears the Will Flag, and with it Will QoS and Will Retain (sec 3.1.2.5).
 */
typedef struct
{
    const char *topic;      ///< Will Topic (sec 3.1.3.2), or null for no Will
    const uint8_t *message; ///< Will Message (sec 3.1.3.3); may be null when @c message_len is 0
    size_t message_len;     ///< its octet count
    uint8_t qos;            ///< Will QoS, 0 to 2 (sec 3.1.2.6)
    proto_bool retain;      ///< Will Retain (sec 3.1.2.7)
} MqttWillArgs;

/**
 * @brief MQTT 3.1.1 sec 3.3: a PUBLISH's Topic Name, Payload and fixed-header flags.
 *
 * @c topic_name is what a build writes; @c topic_out is where a parse copies the Topic Name it read,
 * NUL terminated, with @c topic_len reporting its octet count.
 */
typedef struct
{
    const char *topic_name; ///< Topic Name a build writes (sec 3.3.2.1); no wildcards (MQTT-3.3.2-2)
    char *topic_out;        ///< where a parse copies the Topic Name it read
    size_t topic_cap;       ///< its room, the NUL included
    size_t topic_len;       ///< the Topic Name octets a parse copied
    const uint8_t *payload; ///< Payload a build writes, or where a parse found it inside @c in (sec 3.3.3)
    size_t payload_len;     ///< its octet count
    uint8_t qos;            ///< QoS level, 0 to 2 (sec 3.3.1.2)
    proto_bool retain;      ///< RETAIN (sec 3.3.1.3)
    proto_bool dup;         ///< DUP, set on a re-delivery (sec 3.3.1.1)
} MqttPublishArgs;

/** @brief MQTT 3.1.1 sec 3.8.3, sec 3.10.3: the Topic Filter a SUBSCRIBE or UNSUBSCRIBE names. */
typedef struct
{
    const char *topic_filter; ///< Topic Filter (sec 4.7); wildcards are allowed here
    uint8_t qos;              ///< Requested QoS, 0 to 2 (sec 3.8.3.1)
} MqttFilterArgs;

/** @brief MQTT 3.1.1 sec 2.2, sec 2.3.1: the fixed header a build stamps or a parse reads. */
typedef struct
{
    MqttType type;             ///< MQTT Control Packet type (sec 2.2.1)
    uint8_t flags;             ///< the type-specific flags, byte 1 bits 3-0 (sec 2.2.2)
    uint32_t remaining_length; ///< Remaining Length (sec 2.2.3)
    uint16_t packet_id;        ///< Packet Identifier (sec 2.3.1); never 0 on the wire
} MqttPacketArgs;

/**
 * @brief The octets a codec call writes or reads.
 *
 * A build assembles the variable header and payload in @c body, because the fixed header's Remaining
 * Length is not known until that part is finished, then composes the whole Control Packet into
 * @c out. The codec declares no storage, so the caller lends both.
 */
typedef struct
{
    uint8_t *out;      ///< where a build writes the whole Control Packet
    size_t cap;        ///< its room
    uint8_t *body;     ///< scratch the variable header and payload assemble in
    size_t body_cap;   ///< its room
    const uint8_t *in; ///< the octets a parse reads
    size_t avail;      ///< how many are readable there
} MqttBufArgs;

/** @brief Where the Client hands an inbound Application Message on (MQTT 3.1.1 sec 3.3). */
typedef struct
{
    MqttMessageCb on_message; ///< the Topic Name and Payload sink; null delivers nowhere
} MqttDeliveryArgs;

/** @brief The Client's own state and the calls that reach it, described only in mqtt.c. */
struct MqttInternal;

/**
 * @brief The MQTT Client (OASIS MQTT Version 3.1.1).
 *
 * A caller sets the members a call takes, invokes it through ::Mqtt, and reads the outcome off the
 * same handle. The session, its in-flight window and its buffers are behind @ref internal.
 *
 * No slot member: this Client holds one Network Connection at a time (sec 4.2), so no call names a
 * row.
 *
 * @var MqttNs::server    the Network Connection this Client opens (sec 4.2)
 * @var MqttNs::session   the CONNECT variable header and payload (sec 3.1)
 * @var MqttNs::will      the Will a CONNECT carries (sec 3.1.2.5, sec 3.1.3.2, sec 3.1.3.3)
 * @var MqttNs::message   a PUBLISH's Topic Name, Payload and flags (sec 3.3)
 * @var MqttNs::filter    the Topic Filter a SUBSCRIBE or UNSUBSCRIBE names (sec 4.7)
 * @var MqttNs::packet    the fixed header a build stamps or a parse reads (sec 2.2, sec 2.3.1)
 * @var MqttNs::buf       the octets a codec call writes or reads
 * @var MqttNs::delivery  where an inbound Application Message is handed on (sec 3.3)
 * @var MqttNs::ok        a call's true/false outcome
 * @var MqttNs::n
 * An octet count: the Control Packet a build wrote, the fixed header a parse read, or the Remaining
 * Length field an encode or decode covered. 0 when a build did not fit @c cap or @c body_cap.
 * @var MqttNs::i32       the Connect Return code a CONNACK carried (sec 3.2.2.3), or -1 if malformed
 * @var MqttNs::u8        the first return code a SUBACK carried (sec 3.9.3)
 * @var MqttNs::session_present  the Session Present flag a CONNACK carried (sec 3.2.2.2)
 * @var MqttNs::encode_remaining_length
 * Write @c packet.remaining_length into @c out as a Remaining Length field of 1 to 4 octets
 * (sec 2.2.3). @c n reports the octets written, 0 above ::PROTOCORE_MQTT_REMAINING_LENGTH_MAX or
 * when the field would not fit @c cap.
 * @var MqttNs::decode_remaining_length
 * Read a Remaining Length field from @c in into @c packet.remaining_length, @c n reporting the octets
 * it consumed. False when the field is incomplete or runs past four octets (sec 2.2.3).
 * @var MqttNs::build_connect
 * Build a CONNECT from @c session and @c will: Protocol Name "MQTT", Protocol Level 4, Connect Flags,
 * Keep Alive, then the payload's Client Identifier, Will Topic, Will Message, User Name and Password
 * (sec 3.1).
 * @var MqttNs::build_publish
 * Build a PUBLISH from @c message, its Packet Identifier taken from @c packet.packet_id when
 * @c message.qos is above 0 (sec 3.3). A Topic Name holding `+` or `#` is refused (MQTT-3.3.2-2).
 * @var MqttNs::build_subscribe
 * Build a SUBSCRIBE carrying @c packet.packet_id and one Topic Filter at @c filter.qos, with the
 * fixed-header flags the spec reserves as 0,0,1,0 (sec 3.8.1, sec 3.8.3).
 * @var MqttNs::build_unsubscribe
 * Build an UNSUBSCRIBE carrying @c packet.packet_id and one Topic Filter, with the fixed-header flags
 * the spec reserves as 0,0,1,0 (sec 3.10.1, sec 3.10.3).
 * @var MqttNs::build_ack
 * Build the four-octet PUBACK, PUBREC, PUBREL or PUBCOMP named by @c packet.type carrying
 * @c packet.packet_id (sec 3.4 - sec 3.7). PUBREL takes the reserved flags 0,0,1,0 (sec 3.6.1).
 * @var MqttNs::build_pingreq     build the two-octet PINGREQ (sec 3.12)
 * @var MqttNs::build_disconnect  build the two-octet DISCONNECT (sec 3.14)
 * @var MqttNs::parse_fixed_header
 * Read the fixed header at @c in into @c packet.type, @c packet.flags and @c packet.remaining_length,
 * @c n reporting its size (sec 2.2). False until @c avail holds the whole header.
 * @var MqttNs::parse_publish
 * Read the @c packet.remaining_length octets after a PUBLISH fixed header: copy the Topic Name into
 * @c message.topic_out, point @c message.payload at the Payload, and take the Packet Identifier into
 * @c packet.packet_id when @c packet.flags carries a QoS above 0 (sec 3.3). False on a malformed
 * Topic Name (sec 1.5.3) or on both QoS bits set (MQTT-3.3.1-4).
 * @var MqttNs::parse_ack
 * Read the Packet Identifier from a PUBACK, PUBREC, PUBREL, PUBCOMP or UNSUBACK body into
 * @c packet.packet_id (sec 2.3.1). 0 reports a malformed body, since no real identifier is 0.
 * @var MqttNs::parse_connack
 * Read a CONNACK body into @c session_present (sec 3.2.2.2) and @c i32, the Connect Return code
 * (sec 3.2.2.3). @c i32 is -1 when the body is malformed.
 * @var MqttNs::parse_suback
 * Read a SUBACK body into @c packet.packet_id and @c u8, the first return code of the payload list
 * (sec 3.9.2, sec 3.9.3). ::PROTOCORE_MQTT_SUBACK_FAILURE there is a refused subscription.
 * @var MqttNs::on_message  record @c delivery.on_message; call it before @ref MqttNs::connect
 * @var MqttNs::connect
 * Open the Network Connection to @c server and frame the CONNECT built from @c session and @c will.
 * Returns straight away: @ref MqttNs::loop steps the transport, then the handshake, then the CONNACK,
 * and gives the whole thing up past ::PROTOCORE_MQTT_CONNECT_MS. @c session and @c will are read only
 * during this call.
 * @var MqttNs::publish
 * Send @c message as a PUBLISH (sec 3.3). QoS 0 goes out and is forgotten; QoS 1 and QoS 2 take an
 * in-flight slot and are re-delivered with DUP until acknowledged (sec 4.3.2, sec 4.3.3).
 * @var MqttNs::subscribe    send a SUBSCRIBE for @c filter (sec 3.8)
 * @var MqttNs::unsubscribe  send an UNSUBSCRIBE for @c filter.topic_filter (sec 3.10)
 * @var MqttNs::loop
 * Pump the Network Connection: read inbound Control Packets, deliver PUBLISH to
 * @c delivery.on_message and run the QoS 1 and QoS 2 acknowledgement flows, re-deliver unacknowledged
 * in-flight messages, and send PINGREQ when Keep Alive is due (sec 3.1.2.10). Call once per loop().
 * False once the Network Connection is gone.
 * @var MqttNs::connected  true while the Server has accepted the CONNECT
 * @var MqttNs::disconnect
 * Send DISCONNECT and close the Network Connection (sec 3.14).
 * @var MqttNs::internal   the Client's state and the calls that reach it
 */
typedef struct
{
    MqttServerArgs server;     ///< the Network Connection this Client opens
    MqttSessionArgs session;   ///< what a CONNECT states about the session
    MqttWillArgs will;         ///< the Will a CONNECT carries
    MqttPublishArgs message;   ///< a PUBLISH's Topic Name, Payload and flags
    MqttFilterArgs filter;     ///< the Topic Filter a SUBSCRIBE or UNSUBSCRIBE names
    MqttPacketArgs packet;     ///< the fixed header a build stamps or a parse reads
    MqttBufArgs buf;           ///< the octets a codec call moves
    MqttDeliveryArgs delivery; ///< where an inbound Application Message is handed on

    proto_bool ok;
    size_t n;
    int32_t i32;
    uint8_t u8;
    proto_bool session_present;

    void (*encode_remaining_length)(struct MqttInternal *ctx);
    void (*decode_remaining_length)(struct MqttInternal *ctx);
    void (*build_connect)(struct MqttInternal *ctx);
    void (*build_publish)(struct MqttInternal *ctx);
    void (*build_subscribe)(struct MqttInternal *ctx);
    void (*build_unsubscribe)(struct MqttInternal *ctx);
    void (*build_ack)(struct MqttInternal *ctx);
    void (*build_pingreq)(struct MqttInternal *ctx);
    void (*build_disconnect)(struct MqttInternal *ctx);
    void (*parse_fixed_header)(struct MqttInternal *ctx);
    void (*parse_publish)(struct MqttInternal *ctx);
    void (*parse_ack)(struct MqttInternal *ctx);
    void (*parse_connack)(struct MqttInternal *ctx);
    void (*parse_suback)(struct MqttInternal *ctx);
    void (*on_message)(struct MqttInternal *ctx);
    void (*connect)(struct MqttInternal *ctx);
    void (*publish)(struct MqttInternal *ctx);
    void (*subscribe)(struct MqttInternal *ctx);
    void (*unsubscribe)(struct MqttInternal *ctx);
    void (*loop)(struct MqttInternal *ctx);
    void (*connected)(struct MqttInternal *ctx);
    void (*disconnect)(struct MqttInternal *ctx);

    struct MqttInternal *internal;
} MqttNs;

/** @brief The one symbol this module exports. */
extern MqttNs Mqtt;

#endif // PROTOCORE_ENABLE_MQTT

PROTOCORE_END_DECLS

#endif // PROTOCORE_MQTT_H
