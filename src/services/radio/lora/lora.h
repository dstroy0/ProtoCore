// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_LORA_H
#define PROTOCORE_LORA_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file lora.h
 * @brief LoRa radio codec + driver (PROTOCORE_ENABLE_LORA) - Semtech SX127x / RFM95-96.
 *
 * A per-radio plugin for the gateway (PROTOCORE_ENABLE_GATEWAY): the southbound-radio half of
 * a LoRa-to-web bridge. Two layers:
 *
 * - **Codec** - the RadioHead-compatible 4-byte frame header (`to` / `from` / `id` /
 * `flags`) that virtually every hobby / sensor LoRa deployment uses on top of the
 * header-less LoRa PHY. protocore_lora_frame_parse() splits a received frame into that header and
 * the payload; protocore_lora_frame_build() prepends it. Pure, no hardware.
 * - **Driver** - the SX127x register protocol (init / send / receive / enter-RX) over a
 * caller-supplied register-access **bus** (@ref protocore_lora_bus). The SPI transfer and the
 * chip-select / reset GPIOs are the integration's - you implement two callbacks that
 * read and write a chip register - so the register sequence is host-testable with a mock
 * bus and portable across whatever SPI peripheral you wire the module to.
 *
 * Wiring to the gateway (see example LoRaGateway): poll protocore_lora_recv(); on a frame,
 * protocore_lora_frame_parse() then protocore_gateway_uplink(port, header.from, payload, len, rssi). A downlink
 * builds a frame with protocore_lora_frame_build() and protocore_lora_send()s it. The codec + register protocol
 * are verified on the host; the RF link itself needs the module.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_LORA_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*frame_parse)(uint8_t *, const uint8_t *, uint16_t, protocore_lora_header *, const uint8_t **,
                              uint16_t *);
    uint16_t (*frame_build)(uint8_t *, const protocore_lora_header *, const uint8_t *, uint16_t, uint8_t *, uint16_t);
    proto_bool (*init)(uint8_t *, const protocore_lora_bus *, const protocore_lora_config *);
    proto_bool (*send)(uint8_t *, const protocore_lora_bus *, const uint8_t *, uint8_t);
    proto_bool (*tx_done)(uint8_t *, const protocore_lora_bus *);
    void (*set_rx)(uint8_t *, const protocore_lora_bus *);
    int (*recv)(uint8_t *, const protocore_lora_bus *, uint8_t *, uint8_t, int16_t *);
} LoraNs;
PROTOCORE_NS_LAYOUT(LoraNs, frame_parse, frame_build, init, send, tx_done, set_rx, recv);

/**
 * @brief Split a received frame into its header and payload.
 * @param work PROTOCORE_LORA_BORROW bytes the caller took. Not held past the call.
 * @param raw Raw
 * @param len Len
 * @param hdr Hdr
 * @param payload Payload
 * @param payload_len Payload len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_lora_frame_parse(uint8_t *work, const uint8_t *raw, uint16_t len, protocore_lora_header *hdr,
                                      const uint8_t **payload, uint16_t *payload_len);
/**
 * @brief Build a frame (header + payload) into out.
 * @param work PROTOCORE_LORA_BORROW bytes the caller took. Not held past the call.
 * @param hdr Hdr
 * @param payload Payload
 * @param len Len
 * @param out Out
 * @param cap Cap
 * @return The uint16_t.
 */
uint16_t protocore_lora_frame_build(uint8_t *work, const protocore_lora_header *hdr, const uint8_t *payload,
                                    uint16_t len, uint8_t *out, uint16_t cap);
/**
 * @brief Initialize the SX127x: verify the chip, switch to LoRa mode, and .
 * @param work PROTOCORE_LORA_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @param cfg Cfg
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_lora_init(uint8_t *work, const protocore_lora_bus *bus, const protocore_lora_config *cfg);
/**
 * @brief Load frame into the FIFO and start a transmit (the radio returns to .
 * @param work PROTOCORE_LORA_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @param frame Frame
 * @param len Len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_lora_send(uint8_t *work, const protocore_lora_bus *bus, const uint8_t *frame, uint8_t len);
/**
 * @brief True once a transmit has finished (RegIrqFlags TxDone); clears the .
 * @param work PROTOCORE_LORA_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_lora_tx_done(uint8_t *work, const protocore_lora_bus *bus);
/**
 * @brief Put the radio in continuous-receive mode (call once, then poll .
 * @param work PROTOCORE_LORA_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 */
void protocore_lora_set_rx(uint8_t *work, const protocore_lora_bus *bus);
/**
 * @brief If a frame has been received, copy it into buf and report its RSSI.
 * @param work PROTOCORE_LORA_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @param buf Buf
 * @param cap Cap
 * @param rssi Rssi
 * @return The int.
 */
int protocore_lora_recv(uint8_t *work, const protocore_lora_bus *bus, uint8_t *buf, uint8_t cap, int16_t *rssi);

/** @brief Read one SX127x register (@p reg is the bare 7-bit address). */
typedef uint8_t (*protocore_lora_reg_read_fn)(uint8_t reg, void *ctx);
/** @brief Write one SX127x register (@p reg is the bare 7-bit address). */
typedef void (*protocore_lora_reg_write_fn)(uint8_t reg, uint8_t val, void *ctx);

/** @brief Module namespace. */
PROTOCORE_NS LoraNs Lora PROTOCORE_UNUSED = {.frame_parse = protocore_lora_frame_parse,
                                             .frame_build = protocore_lora_frame_build,
                                             .init = protocore_lora_init,
                                             .send = protocore_lora_send,
                                             .tx_done = protocore_lora_tx_done,
                                             .set_rx = protocore_lora_set_rx,
                                             .recv = protocore_lora_recv};

PROTOCORE_END_DECLS

#endif // PROTOCORE_LORA_H
