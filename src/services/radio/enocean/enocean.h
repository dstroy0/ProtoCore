// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_ENOCEAN_H
#define PROTOCORE_ENOCEAN_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file enocean.h
 * @brief EnOcean ESP3 serial codec (PROTOCORE_ENABLE_ENOCEAN) - energy-harvesting 868 MHz.
 *
 * A UART telegram codec for EnOcean Serial Protocol 3 (ESP3), the framing every USB /
 * serial EnOcean gateway (TCM 310 / USB 300) speaks. A telegram is:
 *
 * 0x55 | data-len (2, big-endian) | opt-len (1) | packet-type (1) | CRC8H
 * | data[data-len] | opt[opt-len] | CRC8D
 *
 * where CRC8H protects the 4 header bytes and CRC8D protects the data + optional data (both
 * CRC-8, polynomial 0x07, init 0). protocore_esp3_parse() frames one telegram out of a byte stream,
 * resynchronizing on a bad sync / CRC, and protocore_esp3_build() assembles one. This is the radio-
 * plugin codec for the gateway: an inbound RADIO_ERP1 telegram carries a sender id (its
 * source address) and payload; bridge it northbound with protocore_gateway_uplink(). Pure - you feed
 * it the UART bytes - so it is fully host-testable. See example EnOceanGateway.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_ENOCEAN_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief ESP3 sync byte that starts every telegram. */
#define ESP3_SYNC 0x55

// Common RORG (telegram-type) codes.
#define PROTOCORE_ERP_RORG_RPS 0xF6 ///< Repeated Switch communication (rocker switches): 1 payload octet
#define PROTOCORE_ERP_RORG_1BS 0xD5 ///< 1-byte communication (contacts): 1 payload octet
#define PROTOCORE_ERP_RORG_4BS 0xA5 ///< 4-byte communication (sensors): 4 payload octets
#define PROTOCORE_ERP_RORG_VLD 0xD2 ///< Variable-Length Data
#define PROTOCORE_ERP_RORG_MSC 0xD1 ///< Manufacturer-Specific Communication
#define PROTOCORE_ERP_RORG_ADT 0xA6 ///< Addressing Destination Telegram
#define PROTOCORE_ERP_RORG_UTE 0xD4 ///< Universal Teach-in

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
} protocore_esp3_type;

/** @brief A parsed ESP3 telegram (pointers alias the caller's buffer). */
typedef struct
{
    const uint8_t *data;      ///< data field
    const uint8_t *opt;       ///< optional-data field
    uint16_t data_len;        ///< data length
    uint8_t opt_len;          ///< optional-data length
    protocore_esp3_type type; ///< packet type (protocore_esp3_type)
} protocore_esp3_packet;

/** @brief A decoded ERP1 radio telegram (the payload aliases the caller's buffer). */
typedef struct
{
    uint8_t rorg;           ///< telegram type (PROTOCORE_ERP_RORG_*)
    const uint8_t *payload; ///< RORG-specific data, or nullptr if none
    uint8_t payload_len;    ///< payload octets (data length - 6)
    uint32_t sender_id;     ///< 4-octet sender id (big-endian)
    uint8_t status;         ///< status octet (repeater count + telegram-type bits)
} protocore_erp1;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    uint8_t (*esp3_crc8)(uint8_t *restrict, const uint8_t *, uint16_t);
    int (*esp3_parse)(uint8_t *restrict, const uint8_t *, uint16_t, protocore_esp3_packet *);
    uint16_t (*esp3_build)(uint8_t *restrict, protocore_esp3_type, const uint8_t *, uint16_t, const uint8_t *, uint8_t,
                           uint8_t *, uint16_t);
    proto_bool (*erp1_parse)(uint8_t *restrict, const uint8_t *, uint16_t, protocore_erp1 *);
    uint16_t (*erp1_build)(uint8_t *restrict, uint8_t *, uint16_t, uint8_t, const uint8_t *, uint8_t, uint32_t,
                           uint8_t);
} EnoceanNs;
PROTOCORE_NS_LAYOUT(EnoceanNs, esp3_crc8, esp3_parse, esp3_build, erp1_parse, erp1_build);

/**
 * @brief CRC-8 used by ESP3 (polynomial 0x07, MSB-first, init 0x00).
 * @param work PROTOCORE_ENOCEAN_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param len Len
 * @return The uint8_t.
 */
uint8_t protocore_enocean_esp3_crc8(uint8_t *restrict work, const uint8_t *buf, uint16_t len);
/**
 * @brief Frame one ESP3 telegram from the front of raw.
 * @param work PROTOCORE_ENOCEAN_BORROW bytes the caller took. Not held past the call.
 * @param raw Raw
 * @param len Len
 * @param out Out
 * @return The int.
 */
int protocore_enocean_esp3_parse(uint8_t *restrict work, const uint8_t *raw, uint16_t len, protocore_esp3_packet *out);
/**
 * @brief Assemble an ESP3 telegram into out.
 * @param work PROTOCORE_ENOCEAN_BORROW bytes the caller took. Not held past the call.
 * @param type Type
 * @param data Data
 * @param data_len Data len
 * @param opt Opt
 * @param opt_len Opt len
 * @param out Out
 * @param cap Cap
 * @return The uint16_t.
 */
uint16_t protocore_enocean_esp3_build(uint8_t *restrict work, protocore_esp3_type type, const uint8_t *data,
                                      uint16_t data_len, const uint8_t *opt, uint8_t opt_len, uint8_t *out,
                                      uint16_t cap);
/**
 * @brief Decode an ERP1 radio telegram: RORG + payload + 4-octet sender id + .
 * @param work PROTOCORE_ENOCEAN_BORROW bytes the caller took. Not held past the call.
 * @param data Data
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_enocean_erp1_parse(uint8_t *restrict work, const uint8_t *data, uint16_t len, protocore_erp1 *out);
/**
 * @brief Assemble an ERP1 radio telegram (the inverse of .
 * @param work PROTOCORE_ENOCEAN_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param rorg Rorg
 * @param payload Payload
 * @param payload_len Payload len
 * @param sender_id Sender id
 * @param status Status
 * @return The uint16_t.
 */
uint16_t protocore_enocean_erp1_build(uint8_t *restrict work, uint8_t *out, uint16_t cap, uint8_t rorg,
                                      const uint8_t *payload, uint8_t payload_len, uint32_t sender_id, uint8_t status);

/** @brief Module namespace. */
PROTOCORE_NS EnoceanNs Enocean PROTOCORE_UNUSED = {.esp3_crc8 = protocore_enocean_esp3_crc8,
                                                   .esp3_parse = protocore_enocean_esp3_parse,
                                                   .esp3_build = protocore_enocean_esp3_build,
                                                   .erp1_parse = protocore_enocean_erp1_parse,
                                                   .erp1_build = protocore_enocean_erp1_build};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENOCEAN_H
