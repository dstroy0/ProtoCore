// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mqtt.h
 * @brief Zero-heap MQTT 3.1.1 publish/subscribe client (PC_ENABLE_MQTT).
 *
 * A full, persistent outbound client for IoT messaging: connect to a broker with
 * an optional Last-Will and credentials, PUBLISH and SUBSCRIBE / UNSUBSCRIBE at
 * QoS 0, 1, or 2 (complete acknowledgement flows in both directions, with bounded
 * in-flight retransmit), receive messages via a callback, and keep the session
 * alive. Split, like the other services, into a pure host-testable codec and an
 * ESP32-only transport:
 *
 *  - mqtt_build_* / mqtt_parse_* / mqtt_*_remlen are pure packet functions,
 *    unit-tested on the host (env:native_mqtt).
 *  - pc_mqtt_connect() / pc_mqtt_publish() / pc_mqtt_subscribe() / pc_mqtt_loop() resolve the
 *    broker (DNS), open a raw lwIP TCP connection (mqtts:// via client-side
 *    mbedTLS over the shared static arena), and drive the session. No heap; one
 *    broker connection at a time.
 *
 * QoS 2 uses the four-packet PUBLISH/PUBREC/PUBREL/PUBCOMP exchange; outbound
 * QoS 1/2 messages are held in a fixed in-flight pool and retransmitted (DUP) on
 * timeout until acknowledged. Inbound QoS 2 is de-duplicated by packet id.
 */

#ifndef PROTOCORE_MQTT_H
#define PROTOCORE_MQTT_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_MQTT

/** @brief MQTT control packet types (high nibble of byte 0), MQTT 3.1.1 §2.2.1. */
typedef enum PROTO_ENUM_PACKED
{
    MQTT_CONNECT = 1,
    MQTT_CONNACK = 2,
    MQTT_PUBLISH = 3,
    MQTT_PUBACK = 4,
    MQTT_PUBREC = 5,
    MQTT_PUBREL = 6,
    MQTT_PUBCOMP = 7,
    MQTT_SUBSCRIBE = 8,
    MQTT_SUBACK = 9,
    MQTT_UNSUBSCRIBE = 10,
    MQTT_UNSUBACK = 11,
    MQTT_PINGREQ = 12,
    MQTT_PINGRESP = 13,
    MQTT_DISCONNECT = 14,
} MqttType;

/** @brief CONNECT options (credentials, keep-alive, clean session, Last-Will). */
typedef struct
{
    const char *client_id;    ///< Client identifier (required; may be "" for a broker-assigned id).
    const char *user;         ///< Username, or nullptr for none.
    const char *pass;         ///< Password, or nullptr for none.
    uint16_t keepalive_s;     ///< Keep-alive seconds (0 disables).
    proto_bool clean_session; ///< Clean Session flag.
    const char *will_topic;   ///< Last-Will topic, or nullptr for no will.
    const uint8_t *will_msg;  ///< Last-Will payload (may be nullptr when will_len is 0).
    size_t will_len;          ///< Last-Will payload length.
    uint8_t will_qos;         ///< Last-Will QoS (0-2).
    proto_bool will_retain;   ///< Last-Will retain flag.
} MqttConnectOpts;

// ---------------------------------------------------------------------------
// Pure codec (host-testable; no sockets, no heap)
// ---------------------------------------------------------------------------

/**
 * @brief Encode an MQTT Remaining Length field (variable-length, 1-4 bytes).
 * @return number of bytes written to @p out (1-4), or 0 if @p len exceeds the
 *         268,435,455 maximum.
 */
size_t pc_mqtt_encode_remlen(uint8_t *out, uint32_t len);

/**
 * @brief Decode a Remaining Length field from @p buf (up to @p avail bytes).
 * @param value  receives the decoded length.
 * @param used   receives the number of bytes consumed (1-4).
 * @return true on success; false if the field is incomplete or malformed (>4 bytes).
 */
proto_bool pc_mqtt_decode_remlen(const uint8_t *buf, size_t avail, uint32_t *value, size_t *used);

/**
 * @brief Build a CONNECT packet from @p opts.
 * @return total packet length written to @p out, or 0 if it would not fit @p cap.
 */
size_t pc_mqtt_build_connect(uint8_t *out, size_t cap, const MqttConnectOpts *opts);

/**
 * @brief Build a PUBLISH packet (@p qos 0/1/2; @p packet_id used only when qos>0;
 *        set @p dup for a retransmission).
 * @return total packet length, or 0 if it would not fit @p cap.
 */
size_t pc_mqtt_build_publish(uint8_t *out, size_t cap, const char *topic, const uint8_t *payload, size_t payload_len,
                             uint8_t qos, uint16_t packet_id, proto_bool retain, proto_bool dup);

/** @brief Build a SUBSCRIBE packet for a single topic filter at @p qos. */
size_t pc_mqtt_build_subscribe(uint8_t *out, size_t cap, uint16_t packet_id, const char *topic, uint8_t qos);

/** @brief Build an UNSUBSCRIBE packet for a single topic filter. */
size_t pc_mqtt_build_unsubscribe(uint8_t *out, size_t cap, uint16_t packet_id, const char *topic);

/**
 * @brief Build a 4-byte acknowledgement packet (PUBACK / PUBREC / PUBREL / PUBCOMP)
 *        carrying @p packet_id. (PUBREL sets the required flags 0x62.)
 */
size_t pc_mqtt_build_ack(uint8_t *out, size_t cap, MqttType type, uint16_t packet_id);

/** @brief Build a 2-byte PINGREQ. */
size_t pc_mqtt_build_pingreq(uint8_t *out, size_t cap);

/** @brief Build a 2-byte DISCONNECT. */
size_t pc_mqtt_build_disconnect(uint8_t *out, size_t cap);

/**
 * @brief Parse a fixed header at @p buf (type/flags + Remaining Length).
 * @param header_len  receives the fixed-header size (1 + remlen-field bytes).
 * @return true if a complete fixed header is present in @p avail bytes.
 */
proto_bool pc_mqtt_parse_fixed_header(const uint8_t *buf, size_t avail, uint8_t *type, uint8_t *flags,
                                      uint32_t *remaining_len, size_t *header_len);

/**
 * @brief Parse a PUBLISH variable header + payload (the @p remaining_len bytes
 *        that follow the fixed header), copying the topic into @p topic_out.
 *
 * @param flags        the fixed-header flags (low nibble); bits 1-2 carry QoS.
 * @param payload      receives a pointer into @p buf at the payload start.
 * @param packet_id    receives the packet id (QoS>0 only; 0 for QoS 0).
 * @return true on success; false if malformed or the topic overflows @p topic_cap.
 */
proto_bool pc_mqtt_parse_publish(const uint8_t *buf, uint32_t remaining_len, uint8_t flags, char *topic_out,
                                 size_t topic_cap, size_t *topic_len, const uint8_t **payload, size_t *payload_len,
                                 uint16_t *packet_id);

/**
 * @brief Read the 2-byte packet id from a PUBACK/PUBREC/PUBREL/PUBCOMP/UNSUBACK
 *        body (the @p remaining_len bytes after the fixed header).
 * @return the packet id, or 0 if malformed (a real id is never 0).
 */
uint16_t pc_mqtt_parse_ack(const uint8_t *buf, uint32_t remaining_len);

/**
 * @brief Read a CONNACK from its @p remaining_len bytes.
 * @param session_present  receives the Session Present flag (may be nullptr).
 * @return the return code (0 = Connection Accepted), or -1 if malformed.
 */
int pc_mqtt_parse_connack(const uint8_t *buf, uint32_t remaining_len, proto_bool *session_present);

/**
 * @brief Read a SUBACK from its @p remaining_len bytes.
 * @param packet_id    receives the packet id.
 * @param return_code  receives the first granted-QoS / failure (0x80) byte.
 * @return true on success.
 */
proto_bool pc_mqtt_parse_suback(const uint8_t *buf, uint32_t remaining_len, uint16_t *packet_id, uint8_t *return_code);

// ---------------------------------------------------------------------------
// Transport (ESP32 only; the calls are no-ops / false on a host build)
// ---------------------------------------------------------------------------

/** @brief Callback for an inbound PUBLISH delivered to a subscription. */
typedef void (*MqttMessageCb)(const char *topic, const uint8_t *payload, size_t len);

/** @brief Register the inbound-message callback (call before pc_mqtt_connect). */
void pc_mqtt_set_message_cb(MqttMessageCb cb);

/**
 * @brief Connect to a broker and complete the MQTT handshake (blocking).
 *
 * Resolves @p host, opens TCP (TLS when @p use_tls and PC_ENABLE_MQTT_TLS),
 * sends CONNECT (from @p opts) and waits for an accepted CONNACK.
 * @return true on an accepted connection.
 */
proto_bool pc_mqtt_connect(const char *host, uint16_t port, proto_bool use_tls, const MqttConnectOpts *opts);

/** @brief Publish @p payload to @p topic at @p qos (0/1/2). @return true if accepted. */
proto_bool pc_mqtt_publish(const char *topic, const uint8_t *payload, size_t len, uint8_t qos, proto_bool retain);

/** @brief Subscribe to @p topic at @p qos (0/1/2). @return true if the SUBSCRIBE was sent. */
proto_bool pc_mqtt_subscribe(const char *topic, uint8_t qos);

/** @brief Unsubscribe from @p topic. @return true if the UNSUBSCRIBE was sent. */
proto_bool pc_mqtt_unsubscribe(const char *topic);

/**
 * @brief Pump the connection: read inbound packets (dispatching PUBLISH to the
 *        callback and running the QoS 1/2 acknowledgement flows), retransmit
 *        unacked outbound QoS 1/2 messages, and send a keep-alive PINGREQ when
 *        due. Call once per loop(). @return false if the connection has dropped.
 */
proto_bool pc_mqtt_loop();

/** @brief True while connected to the broker. */
proto_bool pc_mqtt_connected();

/** @brief Send DISCONNECT and close the connection. */
void pc_mqtt_disconnect();

#endif // PC_ENABLE_MQTT

PROTO_END_DECLS

#endif // PROTOCORE_MQTT_H
