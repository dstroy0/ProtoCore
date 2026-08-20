// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gateway.h
 * @brief Radio / wireless gateway bridge (PROTOCORE_ENABLE_GATEWAY) - the v5 southbound-to-
 *        northbound bridge.
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
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GATEWAY_H
#define PROTOCORE_GATEWAY_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_GATEWAY

PROTOCORE_BEGIN_DECLS

// PROTOCORE_GATEWAY_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

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

/** @brief What add_port takes: cfg. */
typedef struct
{
    const protocore_gateway_port_config *cfg;
} GatewayAddPortArgs;

/** @brief What set_uplink_cb takes: fn, ctx. */
typedef struct
{
    protocore_gateway_uplink_fn fn;
    void *ctx;
} GatewaySetUplinkCbArgs;

/** @brief What set_topic_prefix takes: prefix. */
typedef struct
{
    const char *prefix;
} GatewaySetTopicPrefixArgs;

/** @brief What uplink takes: port_id, src_addr, payload, len, rssi. */
typedef struct
{
    uint8_t port_id;
    uint16_t src_addr;
    const uint8_t *payload;
    uint16_t len;
    int16_t rssi;
} GatewayUplinkArgs;

/** @brief What downlink takes: port_id, dst_addr, payload, len. */
typedef struct
{
    uint8_t port_id;
    uint16_t dst_addr;
    const uint8_t *payload;
    uint16_t len;
} GatewayDownlinkArgs;

/** @brief What topic takes: msg, buf, buflen. */
typedef struct
{
    const protocore_gateway_msg *msg;
    char *buf;
    uint16_t buflen;
} GatewayTopicArgs;

/** @brief What get_stats takes: out. */
typedef struct
{
    protocore_gateway_stats *out;
} GatewayGetStatsArgs;

/**
 * @brief Radio / wireless gateway bridge (PROTOCORE_ENABLE_GATEWAY) - the v5 southbound-to- northbound bridge. The ...
 *
 * A caller sets the members a call takes, invokes it through ::Gateway with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Gateway.reset(work);
 *
 * @var GatewayNs::add_port_args  what add_port takes: cfg
 * @var GatewayNs::set_uplink_cb_args  what set_uplink_cb takes: fn, ctx
 * @var GatewayNs::set_topic_prefix_args  what set_topic_prefix takes: prefix
 * @var GatewayNs::uplink_args  what uplink takes: port_id, src_addr, payload, len, rssi
 * @var GatewayNs::downlink_args  what downlink takes: port_id, dst_addr, payload, len
 * @var GatewayNs::topic_args  what topic takes: msg, buf, buflen
 * @var GatewayNs::get_stats_args  what get_stats takes: out
 * @var GatewayNs::ok  true; false if cfg is null, the id is already registered, or the ...
 * @var GatewayNs::n  the string length written (excluding the NUL), or 0 if buf is too ...
 * @var GatewayNs::reset  clear all ports, the uplink sink, the topic prefix, and stats
 * @var GatewayNs::add_port  register a southbound port
 * @var GatewayNs::set_uplink_cb  install the northbound publish callback (required to publish ...
 * @var GatewayNs::set_topic_prefix  set the topic prefix used by protocore_gateway_topic() ...
 * @var GatewayNs::uplink  bridge a received southbound frame northbound: envelope it and ...
 * @var GatewayNs::downlink  bridge a northbound command southbound: transmit it on port_id's ...
 * @var GatewayNs::topic  format a northbound routing key `<prefix>/<port>/<addr>` for msg ...
 * @var GatewayNs::get_stats  copy the current gateway counters into out. The uplink rate window ...
 *
 * @c work is PROTOCORE_GATEWAY_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    GatewayAddPortArgs add_port_args;
    GatewaySetUplinkCbArgs set_uplink_cb_args;
    GatewaySetTopicPrefixArgs set_topic_prefix_args;
    GatewayUplinkArgs uplink_args;
    GatewayDownlinkArgs downlink_args;
    GatewayTopicArgs topic_args;
    GatewayGetStatsArgs get_stats_args;
    proto_bool ok;
    uint16_t n;
} GatewayVars;

/** @brief The operands and the outcome. */
extern GatewayVars GatewayV;

/** @brief The entries. */
typedef struct
{
    void (*const reset)(uint8_t *restrict work);
    void (*const add_port)(uint8_t *restrict work);
    void (*const set_uplink_cb)(uint8_t *restrict work);
    void (*const set_topic_prefix)(uint8_t *restrict work);
    void (*const uplink)(uint8_t *restrict work);
    void (*const downlink)(uint8_t *restrict work);
    void (*const topic)(uint8_t *restrict work);
    void (*const get_stats)(uint8_t *restrict work);
} GatewayNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in GatewayV or a region of the borrow at a fixed offset.
void protocore_gateway_reset(uint8_t *restrict work);
void protocore_gateway_add_port(uint8_t *restrict work);
void protocore_gateway_set_uplink_cb(uint8_t *restrict work);
void protocore_gateway_set_topic_prefix(uint8_t *restrict work);
void protocore_gateway_uplink(uint8_t *restrict work);
void protocore_gateway_downlink(uint8_t *restrict work);
void protocore_gateway_topic(uint8_t *restrict work);
void protocore_gateway_get_stats(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Gateway.reset(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const GatewayNs Gateway __attribute__((unused)) = {
    .reset = protocore_gateway_reset,
    .add_port = protocore_gateway_add_port,
    .set_uplink_cb = protocore_gateway_set_uplink_cb,
    .set_topic_prefix = protocore_gateway_set_topic_prefix,
    .uplink = protocore_gateway_uplink,
    .downlink = protocore_gateway_downlink,
    .topic = protocore_gateway_topic,
    .get_stats = protocore_gateway_get_stats,
};

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

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_GATEWAY

#endif // PROTOCORE_GATEWAY_H
