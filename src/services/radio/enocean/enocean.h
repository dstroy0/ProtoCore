// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file enocean.h
 * @brief EnOcean ESP3 serial codec (PC_ENABLE_ENOCEAN) - energy-harvesting 868 MHz.
 *
 * A UART telegram codec for EnOcean Serial Protocol 3 (ESP3), the framing every USB /
 * serial EnOcean gateway (TCM 310 / USB 300) speaks. A telegram is:
 *
 *   0x55 | data-len (2, big-endian) | opt-len (1) | packet-type (1) | CRC8H
 *        | data[data-len] | opt[opt-len] | CRC8D
 *
 * where CRC8H protects the 4 header bytes and CRC8D protects the data + optional data (both
 * CRC-8, polynomial 0x07, init 0). pc_esp3_parse() frames one telegram out of a byte stream,
 * resynchronizing on a bad sync / CRC, and pc_esp3_build() assembles one. This is the radio-
 * plugin codec for the gateway: an inbound RADIO_ERP1 telegram carries a sender id (its
 * source address) and payload; bridge it northbound with pc_gateway_uplink(). Pure - you feed
 * it the UART bytes - so it is fully host-testable. See example EnOceanGateway.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ENOCEAN_H
#define PROTOCORE_ENOCEAN_H

#include "protocore_config.h"

#if PC_ENABLE_ENOCEAN

/** @brief ESP3 sync byte that starts every telegram. */
#define ESP3_SYNC 0x55

/** @brief ESP3 packet types (the common ones). */
typedef enum PROTO_ENUM_PACKED
{
    ESP3_RADIO_ERP1 = 0x01,
    ESP3_RESPONSE = 0x02,
    ESP3_RADIO_SUB_TEL = 0x03,
    ESP3_EVENT = 0x04,
    ESP3_COMMON_COMMAND = 0x05,
    ESP3_SMART_ACK = 0x06,
    ESP3_REMOTE_MAN = 0x07,
    ESP3_RADIO_ERP2 = 0x0A,
} pc_esp3_type;

/** @brief A parsed ESP3 telegram (pointers alias the caller's buffer). */
typedef struct
{
    const uint8_t *data; ///< data field
    const uint8_t *opt;  ///< optional-data field
    uint16_t data_len;   ///< data length
    uint8_t opt_len;     ///< optional-data length
    pc_esp3_type type;   ///< packet type (pc_esp3_type)
} pc_esp3_packet;

/** @brief CRC-8 used by ESP3 (polynomial 0x07, MSB-first, init 0x00). */
uint8_t pc_esp3_crc8(const uint8_t *buf, uint16_t len);

/**
 * @brief Frame one ESP3 telegram from the front of @p raw.
 * @return the telegram length consumed (> 0, fields in @p out) if a valid telegram is at
 *         @p raw[0]; 0 if more bytes are needed to complete it; -1 if @p raw[0] is not a
 *         valid telegram start (wrong sync, header CRC, data CRC, or an over-length data
 *         field) - the caller should drop one byte and retry to resynchronize.
 */
int pc_esp3_parse(const uint8_t *raw, uint16_t len, pc_esp3_packet *out);

/**
 * @brief Assemble an ESP3 telegram into @p out.
 * @return the total telegram length, or 0 if it would not fit @p cap or @p data_len exceeds
 *         PC_ENOCEAN_MAX_DATA.
 */
uint16_t pc_esp3_build(pc_esp3_type type, const uint8_t *data, uint16_t data_len, const uint8_t *opt, uint8_t opt_len,
                       uint8_t *out, uint16_t cap);

// --- ERP1 radio telegram (the data field of a RADIO_ERP1 ESP3 packet) ---
//
// An ERP1 telegram is: a RORG (telegram-type) octet, a RORG-specific payload, the 4-octet sender id
// (big-endian), and a status octet. The payload length is fixed per RORG (RPS / 1BS carry 1 octet, 4BS
// carries 4) or variable (VLD); it is always (data length - 6), the 6 being RORG + sender id + status.

// Common RORG (telegram-type) codes.
#define PC_ERP_RORG_RPS 0xF6 ///< Repeated Switch communication (rocker switches): 1 payload octet
#define PC_ERP_RORG_1BS 0xD5 ///< 1-byte communication (contacts): 1 payload octet
#define PC_ERP_RORG_4BS 0xA5 ///< 4-byte communication (sensors): 4 payload octets
#define PC_ERP_RORG_VLD 0xD2 ///< Variable-Length Data
#define PC_ERP_RORG_MSC 0xD1 ///< Manufacturer-Specific Communication
#define PC_ERP_RORG_ADT 0xA6 ///< Addressing Destination Telegram
#define PC_ERP_RORG_UTE 0xD4 ///< Universal Teach-in

/** @brief A decoded ERP1 radio telegram (the payload aliases the caller's buffer). */
typedef struct
{
    uint8_t rorg;           ///< telegram type (PC_ERP_RORG_*)
    const uint8_t *payload; ///< RORG-specific data, or nullptr if none
    uint8_t payload_len;    ///< payload octets (data length - 6)
    uint32_t sender_id;     ///< 4-octet sender id (big-endian)
    uint8_t status;         ///< status octet (repeater count + telegram-type bits)
} pc_erp1;

/**
 * @brief Decode an ERP1 radio telegram: RORG + payload + 4-octet sender id + status octet.
 * @return true iff @p len is at least 6 octets (RORG + sender id + status); false otherwise.
 */
proto_bool pc_erp1_parse(const uint8_t *data, uint16_t len, pc_erp1 *out);

/**
 * @brief Assemble an ERP1 radio telegram (the inverse of pc_erp1_parse): @p rorg + @p payload + the 4-octet
 *        @p sender_id (big-endian) + @p status. The result is the data field of a RADIO_ERP1 ESP3 packet -
 *        wrap it with pc_esp3_build to transmit, so a device can send a switch / actuator command.
 * @return the telegram length (1 + @p payload_len + 5), or 0 on a null buffer, a null payload with a nonzero
 *         length, or an overflow.
 */
uint16_t pc_erp1_build(uint8_t *out, uint16_t cap, uint8_t rorg, const uint8_t *payload, uint8_t payload_len,
                       uint32_t sender_id, uint8_t status);

#endif // PC_ENABLE_ENOCEAN

#endif // PROTOCORE_ENOCEAN_H
