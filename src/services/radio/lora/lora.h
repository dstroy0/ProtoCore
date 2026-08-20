// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lora.h
 * @brief LoRa radio codec + driver (PROTOCORE_ENABLE_LORA) - Semtech SX127x / RFM95-96.
 *
 * A per-radio plugin for the gateway (PROTOCORE_ENABLE_GATEWAY): the southbound-radio half of
 * a LoRa-to-web bridge. Two layers:
 *
 *   - **Codec** - the RadioHead-compatible 4-byte frame header (`to` / `from` / `id` /
 *     `flags`) that virtually every hobby / sensor LoRa deployment uses on top of the
 *     header-less LoRa PHY. protocore_lora_frame_parse() splits a received frame into that header and
 *     the payload; protocore_lora_frame_build() prepends it. Pure, no hardware.
 *   - **Driver** - the SX127x register protocol (init / send / receive / enter-RX) over a
 *     caller-supplied register-access **bus** (@ref protocore_lora_bus). The SPI transfer and the
 *     chip-select / reset GPIOs are the integration's - you implement two callbacks that
 *     read and write a chip register - so the register sequence is host-testable with a mock
 *     bus and portable across whatever SPI peripheral you wire the module to.
 *
 * Wiring to the gateway (see example LoRaGateway): poll protocore_lora_recv(); on a frame,
 * protocore_lora_frame_parse() then protocore_gateway_uplink(port, header.from, payload, len, rssi). A downlink
 * builds a frame with protocore_lora_frame_build() and protocore_lora_send()s it. The codec + register protocol
 * are verified on the host; the RF link itself needs the module.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LORA_H
#define PROTOCORE_LORA_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_LORA

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief RadioHead-compatible LoRa frame header (precedes the payload). */
typedef struct
{
    uint8_t to;    ///< destination node address (0xFF = broadcast)
    uint8_t from;  ///< source node address
    uint8_t id;    ///< sequence / message id
    uint8_t flags; ///< application flags
} protocore_lora_header;

/** @brief Read one SX127x register (@p reg is the bare 7-bit address). */
typedef uint8_t (*protocore_lora_reg_read_fn)(uint8_t reg, void *ctx);

/** @brief Write one SX127x register (@p reg is the bare 7-bit address). */
typedef void (*protocore_lora_reg_write_fn)(uint8_t reg, uint8_t val, void *ctx);

/** @brief The register-access bus a driver call uses (your SPI + chip-select behind it). */
typedef struct
{
    protocore_lora_reg_read_fn read;
    protocore_lora_reg_write_fn write;
    void *ctx;
} protocore_lora_bus;

/** @brief Radio configuration applied by protocore_lora_init(). */
typedef struct
{
    uint32_t freq_hz;    ///< carrier frequency in Hz (e.g. 868100000 / 915000000).
    uint8_t spreading;   ///< spreading factor 6..12 (SF7 default is a good start).
    uint8_t bandwidth;   ///< bandwidth code 0..9 (7 = 125 kHz - the common default).
    uint8_t coding_rate; ///< coding rate 1..4 (1 = 4/5).
    uint8_t sync_word;   ///< 0x12 private / 0x34 LoRaWAN.
    uint8_t tx_power;    ///< PA_BOOST power 2..17 dBm.
} protocore_lora_config;

/** @brief What frame_parse takes: raw, len, hdr, payload, payload_len. */
typedef struct
{
    const uint8_t *raw;
    uint16_t len;
    protocore_lora_header *hdr;
    const uint8_t **payload;
    uint16_t *payload_len;
} LoraFrameParseArgs;

/** @brief What frame_build takes: hdr, payload, len, out, cap. */
typedef struct
{
    const protocore_lora_header *hdr;
    const uint8_t *payload;
    uint16_t len;
    uint8_t *out;
    uint16_t cap;
} LoraFrameBuildArgs;

/** @brief What init takes: bus, cfg. */
typedef struct
{
    const protocore_lora_bus *bus;
    const protocore_lora_config *cfg;
} LoraInitArgs;

/** @brief What send takes: bus, frame, len. */
typedef struct
{
    const protocore_lora_bus *bus;
    const uint8_t *frame;
    uint8_t len;
} LoraSendArgs;

/** @brief What tx_done takes: bus. */
typedef struct
{
    const protocore_lora_bus *bus;
} LoraTxDoneArgs;

/** @brief What set_rx takes: bus. */
typedef struct
{
    const protocore_lora_bus *bus;
} LoraSetRxArgs;

/** @brief What recv takes: bus, buf, cap, rssi. */
typedef struct
{
    const protocore_lora_bus *bus;
    uint8_t *buf;
    uint8_t cap;
    int16_t *rssi;
} LoraRecvArgs;

/**
 * @brief LoRa radio codec + driver (PROTOCORE_ENABLE_LORA) - Semtech SX127x / RFM95-96.
 *
 * A caller sets the members a call takes, invokes it through ::Lora with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Lora.frame_parse_args.raw = ...;
 *   Lora.frame_parse_args.len = ...;
 *   Lora.frame_parse_args.hdr = ...;
 *   Lora.frame_parse_args.payload = ...;
 *   Lora.frame_parse_args.payload_len = ...;
 *   Lora.frame_parse(work);
 *   // Lora.ok is what the call reports
 *
 * @var LoraNs::frame_parse_args  what frame_parse takes: raw, len, hdr, payload, payload_len
 * @var LoraNs::frame_build_args  what frame_build takes: hdr, payload, len, out, cap
 * @var LoraNs::init_args  what init takes: bus, cfg
 * @var LoraNs::send_args  what send takes: bus, frame, len
 * @var LoraNs::tx_done_args  what tx_done takes: bus
 * @var LoraNs::set_rx_args  what set_rx takes: bus
 * @var LoraNs::recv_args  what recv takes: bus, buf, cap, rssi
 * @var LoraNs::ok  true; false if raw is shorter than the 4-byte header
 * @var LoraNs::value  the total frame length, or 0 if it would not fit cap or exceeds the ...
 * @var LoraNs::n  the frame length (>=0), or -1 if no frame is ready or the CRC failed
 * @var LoraNs::frame_parse  split a received frame into its header and payload
 * @var LoraNs::frame_build  build a frame (header + payload) into out
 * @var LoraNs::init  initialize the SX127x: verify the chip, switch to LoRa mode, and ...
 * @var LoraNs::send  load frame into the FIFO and start a transmit (the radio returns to ...
 * @var LoraNs::tx_done  true once a transmit has finished (RegIrqFlags TxDone); clears the ...
 * @var LoraNs::set_rx  put the radio in continuous-receive mode (call once, then poll ...
 * @var LoraNs::recv  if a frame has been received, copy it into buf and report its RSSI
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    LoraFrameParseArgs frame_parse_args;
    LoraFrameBuildArgs frame_build_args;
    LoraInitArgs init_args;
    LoraSendArgs send_args;
    LoraTxDoneArgs tx_done_args;
    LoraSetRxArgs set_rx_args;
    LoraRecvArgs recv_args;
    proto_bool ok;
    uint16_t value;
    int n;
} LoraVars;

/** @brief The operands and the outcome. */
extern LoraVars LoraV;

/** @brief The entries. */
typedef struct
{
    void (*const frame_parse)(uint8_t *restrict work);
    void (*const frame_build)(uint8_t *restrict work);
    void (*const init)(uint8_t *restrict work);
    void (*const send)(uint8_t *restrict work);
    void (*const tx_done)(uint8_t *restrict work);
    void (*const set_rx)(uint8_t *restrict work);
    void (*const recv)(uint8_t *restrict work);
} LoraNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in LoraV or a region of the borrow at a fixed offset.
void protocore_lora_frame_parse(uint8_t *restrict work);
void protocore_lora_frame_build(uint8_t *restrict work);
void protocore_lora_init(uint8_t *restrict work);
void protocore_lora_send(uint8_t *restrict work);
void protocore_lora_tx_done(uint8_t *restrict work);
void protocore_lora_set_rx(uint8_t *restrict work);
void protocore_lora_recv(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Lora.frame_parse(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const LoraNs Lora __attribute__((unused)) = {
    .frame_parse = protocore_lora_frame_parse,
    .frame_build = protocore_lora_frame_build,
    .init = protocore_lora_init,
    .send = protocore_lora_send,
    .tx_done = protocore_lora_tx_done,
    .set_rx = protocore_lora_set_rx,
    .recv = protocore_lora_recv,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LORA

#endif // PROTOCORE_LORA_H
