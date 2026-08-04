// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lora.h
 * @brief LoRa radio codec + driver (PC_ENABLE_LORA) - Semtech SX127x / RFM95-96.
 *
 * A per-radio plugin for the gateway (PC_ENABLE_GATEWAY): the southbound-radio half of
 * a LoRa-to-web bridge. Two layers:
 *
 *   - **Codec** - the RadioHead-compatible 4-byte frame header (`to` / `from` / `id` /
 *     `flags`) that virtually every hobby / sensor LoRa deployment uses on top of the
 *     header-less LoRa PHY. pc_lora_frame_parse() splits a received frame into that header and
 *     the payload; pc_lora_frame_build() prepends it. Pure, no hardware.
 *   - **Driver** - the SX127x register protocol (init / send / receive / enter-RX) over a
 *     caller-supplied register-access **bus** (@ref pc_lora_bus). The SPI transfer and the
 *     chip-select / reset GPIOs are the integration's - you implement two callbacks that
 *     read and write a chip register - so the register sequence is host-testable with a mock
 *     bus and portable across whatever SPI peripheral you wire the module to.
 *
 * Wiring to the gateway (see example LoRaGateway): poll pc_lora_recv(); on a frame,
 * pc_lora_frame_parse() then pc_gateway_uplink(port, header.from, payload, len, rssi). A downlink
 * builds a frame with pc_lora_frame_build() and pc_lora_send()s it. The codec + register protocol
 * are verified on the host; the RF link itself needs the module.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LORA_H
#define PROTOCORE_LORA_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_LORA

// --- Codec: the RadioHead RH_RF95 4-byte header ---------------------------------------

/** @brief RadioHead-compatible LoRa frame header (precedes the payload). */
typedef struct
{
    uint8_t to;    ///< destination node address (0xFF = broadcast)
    uint8_t from;  ///< source node address
    uint8_t id;    ///< sequence / message id
    uint8_t flags; ///< application flags
} pc_lora_header;

/**
 * @brief Split a received frame into its header and payload.
 * @param[out] payload set to the first payload byte (points into @p raw).
 * @param[out] payload_len set to the payload length.
 * @return true; false if @p raw is shorter than the 4-byte header.
 */
proto_bool pc_lora_frame_parse(const uint8_t *raw, uint16_t len, pc_lora_header *hdr, const uint8_t **payload,
                               uint16_t *payload_len);

/**
 * @brief Build a frame (header + payload) into @p out.
 * @return the total frame length, or 0 if it would not fit @p cap or exceeds the payload max.
 */
uint16_t pc_lora_frame_build(const pc_lora_header *hdr, const uint8_t *payload, uint16_t len, uint8_t *out,
                             uint16_t cap);

// --- Driver: SX127x over a register-access bus ----------------------------------------

/** @brief Read one SX127x register (@p reg is the bare 7-bit address). */
typedef uint8_t (*pc_lora_reg_read_fn)(uint8_t reg, void *ctx);
/** @brief Write one SX127x register (@p reg is the bare 7-bit address). */
typedef void (*pc_lora_reg_write_fn)(uint8_t reg, uint8_t val, void *ctx);

/** @brief The register-access bus a driver call uses (your SPI + chip-select behind it). */
typedef struct
{
    pc_lora_reg_read_fn read;
    pc_lora_reg_write_fn write;
    void *ctx;
} pc_lora_bus;

/** @brief Radio configuration applied by pc_lora_init(). */
typedef struct
{
    uint32_t freq_hz;    ///< carrier frequency in Hz (e.g. 868100000 / 915000000).
    uint8_t spreading;   ///< spreading factor 6..12 (SF7 default is a good start).
    uint8_t bandwidth;   ///< bandwidth code 0..9 (7 = 125 kHz - the common default).
    uint8_t coding_rate; ///< coding rate 1..4 (1 = 4/5).
    uint8_t sync_word;   ///< 0x12 private / 0x34 LoRaWAN.
    uint8_t tx_power;    ///< PA_BOOST power 2..17 dBm.
} pc_lora_config;

/**
 * @brief Initialize the SX127x: verify the chip, switch to LoRa mode, and apply @p cfg.
 * @return true; false if the register at RegVersion is not the SX127x id (0x12) - i.e. the
 *         bus is not talking to the chip.
 */
proto_bool pc_lora_init(const pc_lora_bus *bus, const pc_lora_config *cfg);

/**
 * @brief Load @p frame into the FIFO and start a transmit (the radio returns to standby on
 *        TxDone). Poll pc_lora_tx_done() for completion.
 * @return true; false if @p len exceeds PC_LORA_MAX_PAYLOAD + 4.
 */
proto_bool pc_lora_send(const pc_lora_bus *bus, const uint8_t *frame, uint8_t len);

/** @brief True once a transmit has finished (RegIrqFlags TxDone); clears the flag. */
proto_bool pc_lora_tx_done(const pc_lora_bus *bus);

/** @brief Put the radio in continuous-receive mode (call once, then poll pc_lora_recv()). */
void pc_lora_set_rx(const pc_lora_bus *bus);

/**
 * @brief If a frame has been received, copy it into @p buf and report its RSSI.
 * @param[out] rssi set to the packet RSSI in dBm (may be null).
 * @return the frame length (>=0), or -1 if no frame is ready or the CRC failed.
 */
int pc_lora_recv(const pc_lora_bus *bus, uint8_t *buf, uint8_t cap, int16_t *rssi);

#endif // PC_ENABLE_LORA

PROTO_END_DECLS

#endif // PROTOCORE_LORA_H
