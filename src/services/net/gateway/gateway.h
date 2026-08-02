// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gateway.h
 * @brief Radio / wireless gateway bridge (PC_ENABLE_GATEWAY) - the v5 southbound-to-
 *        northbound bridge.
 *
 * The generic gateway pattern that ties the hardware-ingest pipeline to the web stack. A
 * southbound radio (LoRa / nRF24 / CC1101 / Zigbee / Z-Wave / ... reached over SPI / I2C /
 * UART) is a **port**. When it receives a frame - the data-ready ISR reads it over DMA
 * (services/system/dma), posts it onto the FORWARD lane (services/system/preempt_queue), and a per-radio
 * codec extracts the source node address and payload - you call pc_gateway_uplink(). The
 * gateway **envelopes** the frame (source address, port, RSSI, a sequence number) and
 * **publishes it northbound** through the uplink callback, which you wire to MQTT / HTTP /
 * WebSocket / UDP. A northbound command runs the other way through pc_gateway_downlink() to the
 * port's transmit callback (the radio's SPI / UART write).
 *
 * The radio transmit and the northbound publish are **callbacks** - the seam a real radio
 * driver and a real protocol binding plug into - so the bridge is fully host- and
 * device-testable with no radio hardware (the tests / example supply capturing callbacks
 * and feed simulated frames). This is the northbound half; the DMA + FORWARD lane carry the
 * bytes, and each radio's frame format is its own codec.
 *
 * Per-port uplink rate cap (fail-closed), a routing-key helper (pc_gateway_topic() formats
 * `<prefix>/<port>/<addr>`), and static tables (zero heap): PC_GW_MAX_PORTS ports.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GATEWAY_H
#define PROTOCORE_GATEWAY_H

#include "protocore_config.h"

#if PC_ENABLE_GATEWAY

/** @brief Southbound radio / bus kind a port bridges (informational + topic hint). */
typedef enum PROTO_ENUM_PACKED
{
    PC_GW_OTHER = 0,
    PC_GW_LORA,
    PC_GW_NRF24,
    PC_GW_CC1101,
    PC_GW_THREAD,
    PC_GW_ZIGBEE,
    PC_GW_ZWAVE,
    PC_GW_ENOCEAN,
    PC_GW_SIGFOX,
    PC_GW_WISUN,
    PC_GW_NFC,
    PC_GW_BLE,
} pc_gateway_kind;

/**
 * @brief A northbound message: a southbound frame enveloped with its routing metadata.
 *        @ref payload points at the caller's bytes and is valid only for the duration of
 *        the uplink callback - copy what you publish asynchronously.
 */
typedef struct
{
    const uint8_t *payload; ///< frame payload bytes
    uint32_t seq;           ///< per-gateway uplink sequence (wraps)
    uint16_t len;           ///< payload length
    uint16_t src_addr;      ///< source node address on the radio
    int16_t rssi;           ///< received signal strength (0 if the driver has none)
    uint8_t port_id;        ///< the port the frame arrived on
    pc_gateway_kind kind;   ///< pc_gateway_kind of that port
} pc_gateway_msg;

/**
 * @brief Northbound publish: emit @p msg to MQTT / HTTP / WebSocket / UDP.
 * @return true if the northbound stack accepted it; false drops (counted).
 */
typedef proto_bool (*pc_gateway_uplink_fn)(const pc_gateway_msg *msg, void *ctx);

/**
 * @brief Southbound transmit (downlink): send @p payload to @p dst_addr on @p port_id.
 * @return true if the radio accepted the frame; false drops (counted).
 */
typedef proto_bool (*pc_gateway_tx_fn)(uint8_t port_id, uint16_t dst_addr, const uint8_t *payload, uint16_t len,
                                       void *ctx);

/** @brief Southbound port (radio / bus) configuration passed to pc_gateway_add_port(). */
typedef struct
{
    uint8_t port_id;      ///< caller-assigned id (used in topics and up/down-link calls).
    pc_gateway_kind kind; ///< pc_gateway_kind.
    pc_gateway_tx_fn tx;  ///< downlink transmit (may be null for a receive-only port).
    void *ctx;            ///< opaque, forwarded to @ref tx.
    uint16_t rate_cap;    ///< max uplink frames/second from this port (0 = unlimited).
} pc_gateway_port_config;

/** @brief Gateway counters (monotonic since the last pc_gateway_reset()). */
typedef struct
{
    uint32_t up_in;        ///< pc_gateway_uplink() calls
    uint32_t up_published; ///< frames the uplink callback accepted
    uint32_t up_dropped;   ///< uplinks dropped (rate cap / no sink / refused / bad port)
    uint32_t down_in;      ///< pc_gateway_downlink() calls
    uint32_t down_sent;    ///< downlinks the port transmit accepted
    uint32_t down_dropped; ///< downlinks dropped (bad port / no tx / refused)
} pc_gateway_stats;

/** @brief Clear all ports, the uplink sink, the topic prefix, and stats. */
void pc_gateway_reset(void);

/**
 * @brief Register a southbound port.
 * @return true; false if @p cfg is null, the id is already registered, or the table is
 *         full (PC_GW_MAX_PORTS).
 */
proto_bool pc_gateway_add_port(const pc_gateway_port_config *cfg);

/** @brief Install the northbound publish callback (required to publish anything). */
void pc_gateway_set_uplink_cb(pc_gateway_uplink_fn fn, void *ctx);

/** @brief Set the topic prefix used by pc_gateway_topic() (caller-owned string; default "gw"). */
void pc_gateway_set_topic_prefix(const char *prefix);

/**
 * @brief Bridge a received southbound frame northbound: envelope it and publish.
 *
 * Applies the port's uplink rate cap, then calls the uplink callback. Fail-closed: an
 * unknown port, no installed sink, an exceeded cap, or a callback returning false drops
 * the frame (counted), never blocks.
 * @return true if published.
 */
proto_bool pc_gateway_uplink(uint8_t port_id, uint16_t src_addr, const uint8_t *payload, uint16_t len, int16_t rssi);

/**
 * @brief Bridge a northbound command southbound: transmit it on @p port_id's radio.
 * @return true if the port's transmit callback accepted it; false drops (counted).
 */
proto_bool pc_gateway_downlink(uint8_t port_id, uint16_t dst_addr, const uint8_t *payload, uint16_t len);

/**
 * @brief Format a northbound routing key `<prefix>/<port>/<addr>` for @p msg into @p buf.
 * @return the string length written (excluding the NUL), or 0 if @p buf is too small.
 */
uint16_t pc_gateway_topic(const pc_gateway_msg *msg, char *buf, uint16_t buflen);

/** @brief Copy the current gateway counters into @p out. */
void pc_gateway_get_stats(pc_gateway_stats *out);

#if !PROTOCORE_HOT
/** @brief Host only: set the millisecond clock the rate cap uses (tests drive the window). */
void pc_gateway_test_set_now(uint32_t ms);
#endif

#endif // PC_ENABLE_GATEWAY

#endif // PROTOCORE_GATEWAY_H
