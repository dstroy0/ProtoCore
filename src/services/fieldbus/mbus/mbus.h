// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mbus.h
 * @brief Wired M-Bus (Meter-Bus, EN 13757-2/-3) frame codec (PROTOCORE_ENABLE_MBUS).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MBUS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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

/** @brief What build_ack takes: buf, cap. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
} MbusBuildAckArgs;

/** @brief What build_short takes: buf, cap, c, a. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t c;
    uint8_t a;
} MbusBuildShortArgs;

/** @brief What build_long takes: buf, cap, c, a, ci, data, data_len. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t c;
    uint8_t a;
    uint8_t ci;
    const uint8_t *data;
    uint8_t data_len;
} MbusBuildLongArgs;

/** @brief What build_snd_nke takes: buf, cap, a. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t a;
} MbusBuildSndNkeArgs;

/** @brief What build_req_ud2 takes: buf, cap, a, fcb. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t a;
    proto_bool fcb;
} MbusBuildReqUd2Args;

/** @brief What build_req_ud1 takes: buf, cap, a, fcb. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t a;
    proto_bool fcb;
} MbusBuildReqUd1Args;

/** @brief What parse takes: buf, len, out, consumed. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    MbusFrame *out;
    size_t *consumed;
} MbusParseArgs;

/** @brief What dif_data_len takes: coding. */
typedef struct
{
    uint8_t coding;
} MbusDifDataLenArgs;

/** @brief What record_next takes: body, len, pos, out. */
typedef struct
{
    const uint8_t *body;
    size_t len;
    size_t *pos;
    MbusRecord *out;
} MbusRecordNextArgs;

/** @brief What record_value_int takes: r, out. */
typedef struct
{
    const MbusRecord *r;
    int64_t *out;
} MbusRecordValueIntArgs;

/** @brief What record_value_real takes: r, out. */
typedef struct
{
    const MbusRecord *r;
    float *out;
} MbusRecordValueRealArgs;

/** @brief What vif_decode takes: vif, unit, exp10. */
typedef struct
{
    uint8_t vif;
    MbusUnit *unit;
    int8_t *exp10;
} MbusVifDecodeArgs;

/** @brief What parse_var_header takes: body, len, out. */
typedef struct
{
    const uint8_t *body;
    size_t len;
    MbusVarHeader *out;
} MbusParseVarHeaderArgs;

/**
 * @brief Wired M-Bus (Meter-Bus, EN 13757-2/-3) frame codec (PROTOCORE_ENABLE_MBUS).
 *
 * A caller sets the members a call takes, invokes it through ::Mbus with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Mbus.build_ack_args.buf = ...;
 *   Mbus.build_ack_args.cap = ...;
 *   Mbus.build_ack(work);
 *   // Mbus.n is what the call reports
 *
 * @var MbusNs::build_ack_args  what build_ack takes: buf, cap
 * @var MbusNs::build_short_args  what build_short takes: buf, cap, c, a
 * @var MbusNs::build_long_args  what build_long takes: buf, cap, c, a, ci, data, data_len
 * @var MbusNs::build_snd_nke_args  what build_snd_nke takes: buf, cap, a
 * @var MbusNs::build_req_ud2_args  what build_req_ud2 takes: buf, cap, a, fcb
 * @var MbusNs::build_req_ud1_args  what build_req_ud1 takes: buf, cap, a, fcb
 * @var MbusNs::parse_args  what parse takes: buf, len, out, consumed
 * @var MbusNs::dif_data_len_args  what dif_data_len takes: coding
 * @var MbusNs::record_next_args  what record_next takes: body, len, pos, out
 * @var MbusNs::record_value_int_args  what record_value_int takes: r, out
 * @var MbusNs::record_value_real_args  what record_value_real takes: r, out
 * @var MbusNs::vif_decode_args  what vif_decode takes: vif, unit, exp10
 * @var MbusNs::parse_var_header_args  what parse_var_header takes: body, len, out
 * @var MbusNs::ok  true for a decoded measurement VIF; false (unit UNKNOWN) for one ...
 * @var MbusNs::n  the count a call reports
 * @var MbusNs::value  the value a call reports
 * @var MbusNs::build_ack  single-character acknowledge (0xE5)
 * @var MbusNs::build_short  short frame: 10 C A CS 16
 * @var MbusNs::build_long  long frame: 68 L L 68 C A CI [data] CS 16. data_len 0 builds a ...
 * @var MbusNs::build_snd_nke  convenience: a SND_NKE (link reset) short frame to address a
 * @var MbusNs::build_req_ud2  convenience: a REQ_UD2 short frame to address a (fcb toggles the ...
 * @var MbusNs::build_req_ud1  convenience: a REQ_UD1 (class-1 / alarm data request) short frame ...
 * @var MbusNs::parse  parse one M-Bus frame from buf. Validates the start/stop octets, ...
 * @var MbusNs::dif_data_len  map a DIF low-nibble coding to its fixed data length (0 for ...
 * @var MbusNs::record_next  walk one data record at *pos within a long-frame body (the octets ...
 * @var MbusNs::record_value_int  decode a record's value as a signed 64-bit integer (the integer and ...
 * @var MbusNs::record_value_real  decode a record's value as an IEEE-754 float (only the REAL32 DIF ...
 * @var MbusNs::vif_decode  decode a VIF octet into its unit and the base-10 exponent applied ...
 * @var MbusNs::parse_var_header  decode the 12-octet variable-data-structure fixed header (the ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    MbusBuildAckArgs build_ack_args;
    MbusBuildShortArgs build_short_args;
    MbusBuildLongArgs build_long_args;
    MbusBuildSndNkeArgs build_snd_nke_args;
    MbusBuildReqUd2Args build_req_ud2_args;
    MbusBuildReqUd1Args build_req_ud1_args;
    MbusParseArgs parse_args;
    MbusDifDataLenArgs dif_data_len_args;
    MbusRecordNextArgs record_next_args;
    MbusRecordValueIntArgs record_value_int_args;
    MbusRecordValueRealArgs record_value_real_args;
    MbusVifDecodeArgs vif_decode_args;
    MbusParseVarHeaderArgs parse_var_header_args;
    proto_bool ok;
    size_t n;
    uint8_t value;
} MbusVars;

/** @brief The operands and the outcome. */
extern MbusVars MbusV;

/** @brief The entries. */
typedef struct
{
    void (*const build_ack)(uint8_t *restrict work);
    void (*const build_short)(uint8_t *restrict work);
    void (*const build_long)(uint8_t *restrict work);
    void (*const build_snd_nke)(uint8_t *restrict work);
    void (*const build_req_ud2)(uint8_t *restrict work);
    void (*const build_req_ud1)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const dif_data_len)(uint8_t *restrict work);
    void (*const record_next)(uint8_t *restrict work);
    void (*const record_value_int)(uint8_t *restrict work);
    void (*const record_value_real)(uint8_t *restrict work);
    void (*const vif_decode)(uint8_t *restrict work);
    void (*const parse_var_header)(uint8_t *restrict work);
} MbusNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in MbusV or a region of the borrow at a fixed offset.
void protocore_mbus_build_ack(uint8_t *restrict work);
void protocore_mbus_build_short(uint8_t *restrict work);
void protocore_mbus_build_long(uint8_t *restrict work);
void protocore_mbus_build_snd_nke(uint8_t *restrict work);
void protocore_mbus_build_req_ud2(uint8_t *restrict work);
void protocore_mbus_build_req_ud1(uint8_t *restrict work);
void protocore_mbus_parse(uint8_t *restrict work);
void protocore_mbus_dif_data_len(uint8_t *restrict work);
void protocore_mbus_record_next(uint8_t *restrict work);
void protocore_mbus_record_value_int(uint8_t *restrict work);
void protocore_mbus_record_value_real(uint8_t *restrict work);
void protocore_mbus_vif_decode(uint8_t *restrict work);
void protocore_mbus_parse_var_header(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Mbus.build_ack(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const MbusNs Mbus __attribute__((unused)) = {
    .build_ack = protocore_mbus_build_ack,
    .build_short = protocore_mbus_build_short,
    .build_long = protocore_mbus_build_long,
    .build_snd_nke = protocore_mbus_build_snd_nke,
    .build_req_ud2 = protocore_mbus_build_req_ud2,
    .build_req_ud1 = protocore_mbus_build_req_ud1,
    .parse = protocore_mbus_parse,
    .dif_data_len = protocore_mbus_dif_data_len,
    .record_next = protocore_mbus_record_next,
    .record_value_int = protocore_mbus_record_value_int,
    .record_value_real = protocore_mbus_record_value_real,
    .vif_decode = protocore_mbus_vif_decode,
    .parse_var_header = protocore_mbus_parse_var_header,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MBUS

#endif // PROTOCORE_MBUS_H
