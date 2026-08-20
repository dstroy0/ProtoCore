// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file enocean.h
 * @brief EnOcean ESP3 serial codec (PROTOCORE_ENABLE_ENOCEAN) - energy-harvesting 868 MHz.
 *
 * A UART telegram codec for EnOcean Serial Protocol 3 (ESP3), the framing every USB /
 * serial EnOcean gateway (TCM 310 / USB 300) speaks. A telegram is:
 *
 *   0x55 | data-len (2, big-endian) | opt-len (1) | packet-type (1) | CRC8H
 *        | data[data-len] | opt[opt-len] | CRC8D
 *
 * where CRC8H protects the 4 header bytes and CRC8D protects the data + optional data (both
 * CRC-8, polynomial 0x07, init 0). protocore_esp3_parse() frames one telegram out of a byte stream,
 * resynchronizing on a bad sync / CRC, and protocore_esp3_build() assembles one. This is the radio-
 * plugin codec for the gateway: an inbound RADIO_ERP1 telegram carries a sender id (its
 * source address) and payload; bridge it northbound with protocore_gateway_uplink(). Pure - you feed
 * it the UART bytes - so it is fully host-testable. See example EnOceanGateway.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ENOCEAN_H
#define PROTOCORE_ENOCEAN_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_ENOCEAN

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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

/** @brief What esp3_crc8 takes: buf, len. */
typedef struct
{
    const uint8_t *buf;
    uint16_t len;
} EnoceanEsp3Crc8Args;

/** @brief What esp3_parse takes: raw, len, out. */
typedef struct
{
    const uint8_t *raw;
    uint16_t len;
    protocore_esp3_packet *out;
} EnoceanEsp3ParseArgs;

/** @brief What esp3_build takes: type, data, data_len, opt, opt_len, ... */
typedef struct
{
    protocore_esp3_type type;
    const uint8_t *data;
    uint16_t data_len;
    const uint8_t *opt;
    uint8_t opt_len;
    uint8_t *out;
    uint16_t cap;
} EnoceanEsp3BuildArgs;

/** @brief What erp1_parse takes: data, len, out. */
typedef struct
{
    const uint8_t *data;
    uint16_t len;
    protocore_erp1 *out;
} EnoceanErp1ParseArgs;

/** @brief What erp1_build takes: out, cap, rorg, payload, ... */
typedef struct
{
    uint8_t *out;
    uint16_t cap;
    uint8_t rorg;
    const uint8_t *payload;
    uint8_t payload_len;
    uint32_t sender_id;
    uint8_t status;
} EnoceanErp1BuildArgs;

/**
 * @brief EnOcean ESP3 serial codec (PROTOCORE_ENABLE_ENOCEAN) - energy-harvesting 868 MHz.
 *
 * A caller sets the members a call takes, invokes it through ::Enocean with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Enocean.esp3_crc8_args.buf = ...;
 *   Enocean.esp3_crc8_args.len = ...;
 *   Enocean.esp3_crc8(work);
 *   // Enocean.value is what the call reports
 *
 * @var EnoceanNs::esp3_crc8_args  what esp3_crc8 takes: buf, len
 * @var EnoceanNs::esp3_parse_args  what esp3_parse takes: raw, len, out
 * @var EnoceanNs::esp3_build_args  what esp3_build takes: type, data, data_len, opt, opt_len,
 * @var EnoceanNs::erp1_parse_args  what erp1_parse takes: data, len, out
 * @var EnoceanNs::erp1_build_args  what erp1_build takes: out, cap, rorg, payload,
 * @var EnoceanNs::ok  true iff len is at least 6 octets (RORG + sender id + status); ...
 * @var EnoceanNs::value  the value a call reports
 * @var EnoceanNs::n  the telegram length consumed (> 0, fields in out) if a valid ...
 * @var EnoceanNs::u16  the total telegram length, or 0 if it would not fit cap or data_len ...
 * @var EnoceanNs::esp3_crc8  CRC-8 used by ESP3 (polynomial 0x07, MSB-first, init 0x00)
 * @var EnoceanNs::esp3_parse  frame one ESP3 telegram from the front of raw
 * @var EnoceanNs::esp3_build  assemble an ESP3 telegram into out
 * @var EnoceanNs::erp1_parse  decode an ERP1 radio telegram: RORG + payload + 4-octet sender id + ...
 * @var EnoceanNs::erp1_build  assemble an ERP1 radio telegram (the inverse of ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    EnoceanEsp3Crc8Args esp3_crc8_args;
    EnoceanEsp3ParseArgs esp3_parse_args;
    EnoceanEsp3BuildArgs esp3_build_args;
    EnoceanErp1ParseArgs erp1_parse_args;
    EnoceanErp1BuildArgs erp1_build_args;
    proto_bool ok;
    uint8_t value;
    int n;
    uint16_t u16;
} EnoceanVars;

/** @brief The operands and the outcome. */
extern EnoceanVars EnoceanV;

/** @brief The entries. */
typedef struct
{
    void (*const esp3_crc8)(uint8_t *restrict work);
    void (*const esp3_parse)(uint8_t *restrict work);
    void (*const esp3_build)(uint8_t *restrict work);
    void (*const erp1_parse)(uint8_t *restrict work);
    void (*const erp1_build)(uint8_t *restrict work);
} EnoceanNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in EnoceanV or a region of the borrow at a fixed offset.
void protocore_enocean_esp3_crc8(uint8_t *restrict work);
void protocore_enocean_esp3_parse(uint8_t *restrict work);
void protocore_enocean_esp3_build(uint8_t *restrict work);
void protocore_enocean_erp1_parse(uint8_t *restrict work);
void protocore_enocean_erp1_build(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Enocean.esp3_crc8(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const EnoceanNs Enocean __attribute__((unused)) = {
    .esp3_crc8 = protocore_enocean_esp3_crc8,
    .esp3_parse = protocore_enocean_esp3_parse,
    .esp3_build = protocore_enocean_esp3_build,
    .erp1_parse = protocore_enocean_erp1_parse,
    .erp1_build = protocore_enocean_erp1_build,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ENOCEAN

#endif // PROTOCORE_ENOCEAN_H
