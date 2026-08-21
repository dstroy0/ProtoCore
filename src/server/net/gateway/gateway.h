// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_GATEWAY_H
#define PROTOCORE_GATEWAY_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file gateway.h
 * @brief Radio / wireless gateway bridge (PROTOCORE_ENABLE_GATEWAY) - the v5 southbound-to-
northbound bridge.
 *
 * The generic gateway pattern that ties the hardware-ingest pipeline to the web stack. A
 * southbound radio (LoRa / nRF24 / CC1101 / Zigbee / Z-Wave / ... reached over SPI / I2C /
 * UART) is a **port**. When it receives a frame - the data-ready ISR reads it over DMA
 * (mmgr/dma), posts it onto the FORWARD lane (services/system/preempt_queue), and a per-radio
 * codec extracts the source node address and payload - you call protocore_gateway_uplink(). The
 * gateway **envelopes** the frame (source address, port, RSSI, a sequence number) and
 * **publishes it northbound** through the uplink callback, which you wire to MQTT / HTTP /
 * WebSocket / UDP. A northbound command runs the other way through protocore_gateway_downlink() to the
 * port's transmit callback (the radio's SPI / UART write).
 *
 * The radio transmit and the northbound publish are **callbacks** - the seam a real radio
 * driver and a real protocol binding plug into - so the bridge is fully host- and
 * device-testable with no radio hardware (the tests / example supply capturing callbacks
 * and feed simulated frames). This is the northbound half; the DMA + FORWARD lane carry the
 * bytes, and each radio's frame format is its own codec.
 *
 * Per-port uplink rate cap (fail-closed), a routing-key helper (protocore_gateway_topic() formats
 * `<prefix>/<port>/<addr>`), and static tables (zero heap): PROTOCORE_GW_MAX_PORTS ports.
 *
 * @c work is PROTOCORE_GATEWAY_BORROW bytes the CALLER took, at an address it knows. It is not held past the call, so
nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief Southbound radio / bus kind a port bridges (informational + topic hint). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_GW_OTHER = 0,
    PROTOCORE_GW_LORA,
    PROTOCORE_GW_NRF24,
    PROTOCORE_GW_CC1101,
    PROTOCORE_GW_THREAD,
    PROTOCORE_GW_ZIGBEE,
    PROTOCORE_GW_ZWAVE,
    PROTOCORE_GW_ENOCEAN,
    PROTOCORE_GW_SIGFOX,
    PROTOCORE_GW_WISUN,
    PROTOCORE_GW_NFC,
    PROTOCORE_GW_BLE,
} protocore_gateway_kind;

/**
 * @brief A northbound message: a southbound frame enveloped with its routing metadata.
 *        @ref payload points at the caller's bytes and is valid only for the duration of
 *        the uplink callback - copy what you publish asynchronously.
 */
typedef struct
{
    const uint8_t *payload;      ///< frame payload bytes
    uint32_t seq;                ///< per-gateway uplink sequence (wraps)
    uint16_t len;                ///< payload length
    uint16_t src_addr;           ///< source node address on the radio
    int16_t rssi;                ///< received signal strength (0 if the driver has none)
    uint8_t port_id;             ///< the port the frame arrived on
    protocore_gateway_kind kind; ///< protocore_gateway_kind of that port
} protocore_gateway_msg;

/**
 * @brief Northbound publish: emit @p msg to MQTT / HTTP / WebSocket / UDP.
 * @return true if the northbound stack accepted it; false drops (counted).
 */
typedef proto_bool (*protocore_gateway_uplink_fn)(const protocore_gateway_msg *msg, void *ctx);

/**
 * @brief Southbound transmit (downlink): send @p payload to @p dst_addr on @p port_id.
 * @return true if the radio accepted the frame; false drops (counted).
 */
typedef proto_bool (*protocore_gateway_tx_fn)(uint8_t port_id, uint16_t dst_addr, const uint8_t *payload, uint16_t len,
                                              void *ctx);

/** @brief Southbound port (radio / bus) configuration passed to protocore_gateway_add_port(). */
typedef struct
{
    uint8_t port_id;             ///< caller-assigned id (used in topics and up/down-link calls).
    protocore_gateway_kind kind; ///< protocore_gateway_kind.
    protocore_gateway_tx_fn tx;  ///< downlink transmit (may be null for a receive-only port).
    void *ctx;                   ///< opaque, forwarded to @ref tx.
    uint16_t rate_cap;           ///< max uplink frames/second from this port (0 = unlimited).
} protocore_gateway_port_config;

/** @brief Gateway counters (monotonic since the last protocore_gateway_reset()). */
typedef struct
{
    uint32_t up_in;        ///< protocore_gateway_uplink() calls
    uint32_t up_published; ///< frames the uplink callback accepted
    uint32_t up_dropped;   ///< uplinks dropped (rate cap / no sink / refused / bad port)
    uint32_t down_in;      ///< protocore_gateway_downlink() calls
    uint32_t down_sent;    ///< downlinks the port transmit accepted
    uint32_t down_dropped; ///< downlinks dropped (bad port / no tx / refused)
} protocore_gateway_stats;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*reset)(uint8_t *);
    proto_bool (*add_port)(uint8_t *, const protocore_gateway_port_config *);
    void (*set_uplink_cb)(uint8_t *, protocore_gateway_uplink_fn, void *);
    void (*set_topic_prefix)(uint8_t *, const char *);
    proto_bool (*uplink)(uint8_t *, uint8_t, uint16_t, const uint8_t *, uint16_t, int16_t);
    proto_bool (*downlink)(uint8_t *, uint8_t, uint16_t, const uint8_t *, uint16_t);
    uint16_t (*topic)(uint8_t *, const protocore_gateway_msg *, char *, uint16_t);
    void (*get_stats)(uint8_t *, protocore_gateway_stats *);
} GatewayNs;
PROTOCORE_NS_LAYOUT(GatewayNs, reset, add_port, set_uplink_cb, set_topic_prefix, uplink, downlink, topic, get_stats);

/**
 * @brief Clear all ports, the uplink sink, the topic prefix, and stats.
 * @param work PROTOCORE_GATEWAY_BORROW bytes the caller took. Not held past the call.
 */
void protocore_gateway_reset(uint8_t *work);
/**
 * @brief Register a southbound port.
 * @param work PROTOCORE_GATEWAY_BORROW bytes the caller took. Not held past the call.
 * @param cfg Cfg
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_gateway_add_port(uint8_t *work, const protocore_gateway_port_config *cfg);
/**
 * @brief Install the northbound publish callback (required to publish .
 * @param work PROTOCORE_GATEWAY_BORROW bytes the caller took. Not held past the call.
 * @param fn Fn
 * @param ctx Ctx
 */
void protocore_gateway_set_uplink_cb(uint8_t *work, protocore_gateway_uplink_fn fn, void *ctx);
/**
 * @brief Set the topic prefix used by protocore_gateway_topic() .
 * @param work PROTOCORE_GATEWAY_BORROW bytes the caller took. Not held past the call.
 * @param prefix Prefix
 */
void protocore_gateway_set_topic_prefix(uint8_t *work, const char *prefix);
/**
 * @brief Bridge a received southbound frame northbound: envelope it and .
 * @param work PROTOCORE_GATEWAY_BORROW bytes the caller took. Not held past the call.
 * @param port_id Port id
 * @param src_addr Src addr
 * @param payload Payload
 * @param len Len
 * @param rssi Rssi
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_gateway_uplink(uint8_t *work, uint8_t port_id, uint16_t src_addr, const uint8_t *payload,
                                    uint16_t len, int16_t rssi);
/**
 * @brief Bridge a northbound command southbound: transmit it on port_id's .
 * @param work PROTOCORE_GATEWAY_BORROW bytes the caller took. Not held past the call.
 * @param port_id Port id
 * @param dst_addr Dst addr
 * @param payload Payload
 * @param len Len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_gateway_downlink(uint8_t *work, uint8_t port_id, uint16_t dst_addr, const uint8_t *payload,
                                      uint16_t len);
/**
 * @brief Format a northbound routing key `<prefix>/<port>/<addr>` for msg .
 * @param work PROTOCORE_GATEWAY_BORROW bytes the caller took. Not held past the call.
 * @param msg Msg
 * @param buf Buf
 * @param buflen Buflen
 * @return The uint16_t.
 */
uint16_t protocore_gateway_topic(uint8_t *work, const protocore_gateway_msg *msg, char *buf, uint16_t buflen);
/**
 * @brief Copy the current gateway counters into out. The uplink rate window .
 * @param work PROTOCORE_GATEWAY_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 */
void protocore_gateway_get_stats(uint8_t *work, protocore_gateway_stats *out);

/**
 * @brief Northbound publish: emit @p msg to MQTT / HTTP / WebSocket / UDP.
 * @return true if the northbound stack accepted it; false drops (counted).
 */
typedef proto_bool (*protocore_gateway_uplink_fn)(const protocore_gateway_msg *msg, void *ctx);
/**
 * @brief The PROTOCORE_GATEWAY_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_gateway_span(void);

/** @brief Module namespace. */
PROTOCORE_NS GatewayNs Gateway PROTOCORE_UNUSED = {.reset = protocore_gateway_reset,
                                                   .add_port = protocore_gateway_add_port,
                                                   .set_uplink_cb = protocore_gateway_set_uplink_cb,
                                                   .set_topic_prefix = protocore_gateway_set_topic_prefix,
                                                   .uplink = protocore_gateway_uplink,
                                                   .downlink = protocore_gateway_downlink,
                                                   .topic = protocore_gateway_topic,
                                                   .get_stats = protocore_gateway_get_stats};

PROTOCORE_END_DECLS

#endif // PROTOCORE_GATEWAY_H
