// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mbus.h
 * @brief Wired M-Bus (Meter-Bus, EN 13757-2/-3) frame codec (PC_ENABLE_MBUS).
 *
 * A pure, zero-heap builder + parser for the M-Bus link-layer frames used by utility meters
 * (water / gas / heat / electricity), plus a walker for the EN 13757-3 variable-data records
 * (DIF / VIF). M-Bus has three frame formats:
 * @code
 *   single char : E5                                                  (ACK)
 *   short frame : 10 C A CS 16
 *   long frame  : 68 L L 68 C A CI [user data] CS 16   (L = 3 + data; CS = sum(C..data) mod 256)
 * @endcode
 * The control frame is just a long frame with no user data (L = 3). The checksum is the
 * 8-bit sum of every octet from C through the end of the user data.
 *
 * The wired bus is a powered two-wire pair: the ESP32 talks to it over a UART through an
 * M-Bus level converter (e.g. a TSS721-based master module). This codec is the framing +
 * record layer, including decoding a record's raw value (integer / BCD / real) into a number and its VIF
 * into a physical unit + decimal exponent; the UART transport is the application's. Bridge meters onto
 * Wi-Fi by polling REQ_UD2 and publishing the decoded records.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MBUS_H
#define PROTOCORE_MBUS_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_MBUS

#define MBUS_START_SHORT 0x10u ///< short-frame start octet
#define MBUS_START_LONG 0x68u  ///< long / control-frame start octet
#define MBUS_STOP 0x16u        ///< frame stop octet
#define MBUS_ACK 0xE5u         ///< single-character acknowledge

// Common control-field (C) values.
#define MBUS_C_SND_NKE 0x40u ///< initialize slave (link reset)
#define MBUS_C_REQ_UD2 0x5Bu ///< request class-2 user data (FCB=0); 0x7B with FCB=1
#define MBUS_C_REQ_UD1 0x5Au ///< request class-1 user data
#define MBUS_C_SND_UD 0x53u  ///< send user data to slave (FCB=0); 0x73 with FCB=1
#define MBUS_C_RSP_UD 0x08u  ///< response with user data (+ ACD/DFC bits)

// Common control-information (CI) values.
#define MBUS_CI_DATA_SEND 0x51u    ///< data send (master -> slave)
#define MBUS_CI_SELECT 0x52u       ///< selection of slaves
#define MBUS_CI_RSP_VARIABLE 0x72u ///< variable data response, long header (LSB first)
#define MBUS_CI_RSP_FIXED 0x73u    ///< fixed data response

#define MBUS_MAX_DATA 252u ///< max user-data octets (L is one octet; 255 - 3)

/** @brief M-Bus frame kinds. */
typedef enum PROTO_ENUM_PACKED
{
    MBUS_FRAME_NONE = 0,
    MBUS_FRAME_ACK,   ///< single 0xE5
    MBUS_FRAME_SHORT, ///< 10 C A CS 16
    MBUS_FRAME_LONG,  ///< 68 L L 68 C A CI ... CS 16 (control frame = long with no data)
} MbusFrameType;

/** @brief A parsed M-Bus frame (data points into the caller's buffer). */
typedef struct
{
    MbusFrameType type;
    uint8_t c;           ///< control field (short / long)
    uint8_t a;           ///< address field (short / long)
    uint8_t ci;          ///< control-information field (long only)
    const uint8_t *data; ///< user data (long only), or nullptr
    uint8_t data_len;    ///< user-data length (long only)
} MbusFrame;

// DIF data-field coding (low nibble of the DIF). The decoded fixed lengths are in octets.
typedef enum PROTO_ENUM_PACKED
{
    MBUS_DIF_NONE = 0x0,     ///< no data
    MBUS_DIF_INT8 = 0x1,     ///< 8-bit integer
    MBUS_DIF_INT16 = 0x2,    ///< 16-bit integer
    MBUS_DIF_INT24 = 0x3,    ///< 24-bit integer
    MBUS_DIF_INT32 = 0x4,    ///< 32-bit integer
    MBUS_DIF_REAL32 = 0x5,   ///< 32-bit IEEE-754 real
    MBUS_DIF_INT48 = 0x6,    ///< 48-bit integer
    MBUS_DIF_INT64 = 0x7,    ///< 64-bit integer
    MBUS_DIF_READOUT = 0x8,  ///< selection for readout (no data)
    MBUS_DIF_BCD2 = 0x9,     ///< 2-digit BCD (1 octet)
    MBUS_DIF_BCD4 = 0xA,     ///< 4-digit BCD (2 octets)
    MBUS_DIF_BCD6 = 0xB,     ///< 6-digit BCD (3 octets)
    MBUS_DIF_BCD8 = 0xC,     ///< 8-digit BCD (4 octets)
    MBUS_DIF_VARIABLE = 0xD, ///< variable length (LVAR octet precedes the data)
    MBUS_DIF_BCD12 = 0xE,    ///< 12-digit BCD (6 octets)
    MBUS_DIF_SPECIAL = 0xF,  ///< special functions (no data)
} MbusDifCoding;

/** @brief One decoded EN 13757-3 data record. */
typedef struct
{
    uint8_t dif;         ///< first data-information octet
    uint8_t coding;      ///< DIF low nibble (see MbusDifCoding)
    uint8_t vif;         ///< first value-information octet (0 if none)
    const uint8_t *data; ///< value octets (points into the caller buffer)
    uint8_t data_len;    ///< value length in octets
} MbusRecord;

// --- builders: write into @p buf (cap), return frame length or 0 on overflow ---

/** @brief Single-character acknowledge (0xE5). */
size_t pc_mbus_build_ack(uint8_t *buf, size_t cap);

/** @brief Short frame: 10 C A CS 16. */
size_t pc_mbus_build_short(uint8_t *buf, size_t cap, uint8_t c, uint8_t a);

/** @brief Long frame: 68 L L 68 C A CI [data] CS 16. @p data_len 0 builds a control frame. */
size_t pc_mbus_build_long(uint8_t *buf, size_t cap, uint8_t c, uint8_t a, uint8_t ci, const uint8_t *data,
                          uint8_t data_len);

/** @brief Convenience: a SND_NKE (link reset) short frame to address @p a. */
size_t pc_mbus_build_snd_nke(uint8_t *buf, size_t cap, uint8_t a);

/** @brief Convenience: a REQ_UD2 short frame to address @p a (@p fcb toggles the FCB bit). */
size_t pc_mbus_build_req_ud2(uint8_t *buf, size_t cap, uint8_t a, proto_bool fcb);

/** @brief Convenience: a REQ_UD1 (class-1 / alarm data request) short frame to address @p a (@p fcb toggles
 *  the FCB bit). Where REQ_UD2 fetches routine class-2 data, REQ_UD1 fetches class-1 (alarm) data. */
size_t pc_mbus_build_req_ud1(uint8_t *buf, size_t cap, uint8_t a, proto_bool fcb);

// --- parser ---

/**
 * @brief Parse one M-Bus frame from @p buf. Validates the start/stop octets, the doubled
 * length, and the checksum. On success fills @p out and sets @p consumed to the frame length.
 */
proto_bool pc_mbus_parse(const uint8_t *buf, size_t len, MbusFrame *out, size_t *consumed);

// --- variable-data records (DIF / VIF) ---

/** @brief Map a DIF low-nibble coding to its fixed data length (0 for variable / none). */
uint8_t pc_mbus_dif_data_len(uint8_t coding);

/**
 * @brief Walk one data record at @p *pos within a long-frame body (the octets after CI).
 * Skips DIFE / VIFE extension chains, decodes the data length (incl. the LVAR variable form),
 * and advances @p *pos past the record. Returns false at the end of data or on overflow.
 */
proto_bool pc_mbus_record_next(const uint8_t *body, size_t len, size_t *pos, MbusRecord *out);

// --- record value + unit decoding ---

/**
 * @brief Decode a record's value as a signed 64-bit integer (the integer and BCD DIF codings).
 *
 * Integer codings are little-endian and sign-extended; BCD codings are little-endian octets of two
 * digits, with a 0xF most-significant nibble marking a negative value. @return false for a real / variable
 * / no-data coding, or an invalid BCD nibble.
 */
proto_bool pc_mbus_record_value_int(const MbusRecord *r, int64_t *out);

/** @brief Decode a record's value as an IEEE-754 float (only the REAL32 DIF coding). @return false otherwise. */
proto_bool pc_mbus_record_value_real(const MbusRecord *r, float *out);

/** @brief Physical unit a VIF decodes to (the common EN 13757-3 measurement ranges). */
typedef enum PROTO_ENUM_PACKED
{
    MBUS_UNIT_UNKNOWN = 0,
    MBUS_UNIT_WH,       ///< energy, watt-hours
    MBUS_UNIT_J,        ///< energy, joules
    MBUS_UNIT_M3,       ///< volume, cubic metres
    MBUS_UNIT_KG,       ///< mass, kilograms
    MBUS_UNIT_W,        ///< power, watts
    MBUS_UNIT_J_PER_H,  ///< power, joules per hour
    MBUS_UNIT_M3_PER_H, ///< volume flow, cubic metres per hour
    MBUS_UNIT_CELSIUS,  ///< temperature, degrees Celsius
    MBUS_UNIT_K,        ///< temperature difference, kelvin
    MBUS_UNIT_BAR,      ///< pressure, bar
} MbusUnit;

/**
 * @brief Decode a VIF octet into its unit and the base-10 exponent applied to the raw value.
 *
 * Covers the common EN 13757-3 main-table measurement ranges (energy, volume, mass, power, volume flow,
 * temperature, pressure). The physical value is (raw value) * 10^(@p exp10) in @p unit.
 * @return true for a decoded measurement VIF; false (unit UNKNOWN) for one outside those ranges.
 */
proto_bool pc_mbus_vif_decode(uint8_t vif, MbusUnit *unit, int8_t *exp10);

// --- variable-data-structure fixed header (EN 13757-3): the 12 octets before the data records ---
//
// A CI = MBUS_CI_RSP_VARIABLE (0x72) long-frame body opens with a fixed header identifying the meter -
// its secondary-address serial, manufacturer, version, and medium - then the access / status / signature,
// and only then the DIF/VIF data records that pc_mbus_record_next walks.

#define MBUS_VAR_HEADER_LEN 12u ///< octets of the fixed header preceding the records in a CI=0x72 response

// Common medium / device-type codes (EN 13757-3 §6.4).
#define MBUS_MEDIUM_OTHER 0x00u
#define MBUS_MEDIUM_OIL 0x01u
#define MBUS_MEDIUM_ELECTRICITY 0x02u
#define MBUS_MEDIUM_GAS 0x03u
#define MBUS_MEDIUM_HEAT_OUTLET 0x04u
#define MBUS_MEDIUM_STEAM 0x05u
#define MBUS_MEDIUM_WARM_WATER 0x06u
#define MBUS_MEDIUM_WATER 0x07u
#define MBUS_MEDIUM_HEAT_COST 0x08u
#define MBUS_MEDIUM_HEAT_INLET 0x0Cu
#define MBUS_MEDIUM_HEAT_COOLING 0x0Du
#define MBUS_MEDIUM_COLD_WATER 0x16u

/** @brief The decoded EN 13757-3 variable-data-structure fixed header. */
typedef struct
{
    uint32_t id;               ///< identification number (secondary-address serial), decoded from the 4-octet BCD
    char manufacturer[4];      ///< 3-letter manufacturer code + NUL (decoded from the 2-octet field)
    uint16_t manufacturer_raw; ///< the raw 2-octet manufacturer value (FLAG code)
    uint8_t version;           ///< device generation / version
    uint8_t medium;            ///< medium / device type (MBUS_MEDIUM_*)
    uint8_t access_no;         ///< access number (increments each readout)
    uint8_t status;            ///< status octet (error / alarm bits)
    uint16_t signature;        ///< signature word (usually 0)
} MbusVarHeader;

/**
 * @brief Decode the 12-octet variable-data-structure fixed header (the header that precedes the data records
 *        in a CI = MBUS_CI_RSP_VARIABLE (0x72) long-frame body) into @p out.
 * @return true iff @p len is at least 12 octets and the identification number is valid BCD; false otherwise.
 */
proto_bool pc_mbus_parse_var_header(const uint8_t *body, size_t len, MbusVarHeader *out);

#endif // PC_ENABLE_MBUS

PROTO_END_DECLS

#endif // PROTOCORE_MBUS_H
