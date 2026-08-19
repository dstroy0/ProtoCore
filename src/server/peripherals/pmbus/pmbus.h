// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pmbus.h
 * @brief PMBus 1.3 power-management command set over SMBus.
 *
 * PMBus is SMBus with the command codes fixed and the numbers given an encoding, so a digital
 * point-of-load converter, a hot-swap controller or a power supply reports its input voltage,
 * output current, temperature and faults through one set of commands instead of a register map
 * per part.
 *
 * Three encodings carry the numbers, and all three are decoded here rather than in a driver:
 *
 *   - **LINEAR11** for most telemetry: one word holding a 5-bit signed exponent in bits 15:11 and
 *     an 11-bit signed mantissa in bits 10:0. The value is mantissa * 2^exponent.
 *   - **LINEAR16** for output voltage: the word is a 16-bit unsigned mantissa and the exponent
 *     comes from the part's VOUT_MODE register, so it is read once and applied to every reading.
 *   - **DIRECT** where a part uses it: the value is (word / 10^R - b) / m, with m, b and R coming
 *     from the part's datasheet.
 *
 * Values are returned in micro-units (microvolts, microamps, microwatts) and temperatures in
 * milli-degrees Celsius, which is what the other sensor drivers here report in. No float is
 * involved: the exponent is a shift.
 *
 * The encodings are pure and host-tested. The commands ride the SMBus shapes, so a build with no
 * bus seam refuses them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PMBUS_H
#define PROTOCORE_PMBUS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_PMBUS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define PROTOCORE_PMBUS_PAGE 0x00u

#define PROTOCORE_PMBUS_OPERATION 0x01u

#define PROTOCORE_PMBUS_ON_OFF_CONFIG 0x02u

#define PROTOCORE_PMBUS_CLEAR_FAULTS 0x03u

#define PROTOCORE_PMBUS_CAPABILITY 0x19u

#define PROTOCORE_PMBUS_VOUT_MODE 0x20u

#define PROTOCORE_PMBUS_VOUT_COMMAND 0x21u

#define PROTOCORE_PMBUS_VOUT_MAX 0x24u

#define PROTOCORE_PMBUS_VOUT_OV_FAULT_LIM 0x40u

#define PROTOCORE_PMBUS_IOUT_OC_FAULT_LIM 0x46u

#define PROTOCORE_PMBUS_OT_FAULT_LIMIT 0x4Fu

#define PROTOCORE_PMBUS_VIN_OV_FAULT_LIM 0x55u

#define PROTOCORE_PMBUS_STATUS_BYTE 0x78u

#define PROTOCORE_PMBUS_STATUS_WORD 0x79u

#define PROTOCORE_PMBUS_STATUS_VOUT 0x7Au

#define PROTOCORE_PMBUS_STATUS_IOUT 0x7Bu

#define PROTOCORE_PMBUS_STATUS_INPUT 0x7Cu

#define PROTOCORE_PMBUS_STATUS_TEMP 0x7Du

#define PROTOCORE_PMBUS_STATUS_CML 0x7Eu

#define PROTOCORE_PMBUS_READ_VIN 0x88u

#define PROTOCORE_PMBUS_READ_IIN 0x89u

#define PROTOCORE_PMBUS_READ_VOUT 0x8Bu

#define PROTOCORE_PMBUS_READ_IOUT 0x8Cu

#define PROTOCORE_PMBUS_READ_TEMP_1 0x8Du

#define PROTOCORE_PMBUS_READ_TEMP_2 0x8Eu

#define PROTOCORE_PMBUS_READ_FAN_SPEED_1 0x90u

#define PROTOCORE_PMBUS_READ_POUT 0x96u

#define PROTOCORE_PMBUS_READ_PIN 0x97u

#define PROTOCORE_PMBUS_MFR_ID 0x99u

#define PROTOCORE_PMBUS_MFR_MODEL 0x9Au

#define PROTOCORE_PMBUS_MFR_REVISION 0x9Bu

#define PROTOCORE_PMBUS_ST_NONE_ABOVE 0x01u

#define PROTOCORE_PMBUS_ST_CML 0x02u

#define PROTOCORE_PMBUS_ST_TEMPERATURE 0x04u

#define PROTOCORE_PMBUS_ST_VIN_UV 0x08u

#define PROTOCORE_PMBUS_ST_IOUT_OC 0x10u

#define PROTOCORE_PMBUS_ST_VOUT_OV 0x20u

#define PROTOCORE_PMBUS_ST_OFF 0x40u

#define PROTOCORE_PMBUS_ST_BUSY 0x80u

#define PROTOCORE_PMBUS_MODE_LINEAR 0u

#define PROTOCORE_PMBUS_MODE_VID 1u

#define PROTOCORE_PMBUS_MODE_DIRECT 2u

#define PROTOCORE_PMBUS_MODE_IEEE 3u

#define PROTOCORE_PMBUS_INVALID INT32_MIN

/** @brief What vout_mode_kind takes: vout_mode. */
typedef struct
{
    uint8_t vout_mode;
} PmbusVoutModeKindArgs;

/** @brief What vout_exponent takes: vout_mode. */
typedef struct
{
    uint8_t vout_mode;
} PmbusVoutExponentArgs;

/** @brief What l11_mantissa takes: word. */
typedef struct
{
    uint16_t word;
} PmbusL11MantissaArgs;

/** @brief What l11_exponent takes: word. */
typedef struct
{
    uint16_t word;
} PmbusL11ExponentArgs;

/** @brief What linear11_micro takes: word. */
typedef struct
{
    uint16_t word;
} PmbusLinear11MicroArgs;

/** @brief What linear11_encode takes: micro. */
typedef struct
{
    int32_t micro;
} PmbusLinear11EncodeArgs;

/** @brief What linear16_micro takes: word, exponent. */
typedef struct
{
    uint16_t word;
    int8_t exponent;
} PmbusLinear16MicroArgs;

/** @brief What linear16_encode takes: micro, exponent. */
typedef struct
{
    int32_t micro;
    int8_t exponent;
} PmbusLinear16EncodeArgs;

/** @brief What direct_micro takes: word, m, b, r. */
typedef struct
{
    uint16_t word;
    int16_t m;
    int16_t b;
    int8_t r;
} PmbusDirectMicroArgs;

/** @brief What set_page takes: addr, page. */
typedef struct
{
    uint8_t addr;
    uint8_t page;
} PmbusSetPageArgs;

/** @brief What read_vout_mode takes: addr, out. */
typedef struct
{
    uint8_t addr;
    uint8_t *out;
} PmbusReadVoutModeArgs;

/** @brief What read_linear11 takes: addr, cmd, micro. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    int32_t *micro;
} PmbusReadLinear11Args;

/** @brief What read_linear16 takes: addr, cmd, exponent, micro. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    int8_t exponent;
    int32_t *micro;
} PmbusReadLinear16Args;

/** @brief What write_linear16 takes: addr, cmd, exponent, micro. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    int8_t exponent;
    int32_t micro;
} PmbusWriteLinear16Args;

/** @brief What status_byte takes: addr, out. */
typedef struct
{
    uint8_t addr;
    uint8_t *out;
} PmbusStatusByteArgs;

/** @brief What status_word takes: addr, out. */
typedef struct
{
    uint8_t addr;
    uint16_t *out;
} PmbusStatusWordArgs;

/** @brief What clear_faults takes: addr. */
typedef struct
{
    uint8_t addr;
} PmbusClearFaultsArgs;

/** @brief What read_mfr_string takes: addr, cmd, out, cap, len. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    uint8_t *out; ///< caller-owned, cap bytes; not terminated, the length comes back in len
    size_t cap;
    size_t *len;
} PmbusReadMfrStringArgs;

/**
 * @brief PMBus 1.3 power-management command set over SMBus. PMBus is SMBus with the command codes fixed and the ...
 *
 * A caller sets the members a call takes, invokes it through ::Pmbus with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Pmbus.vout_mode_kind_args.vout_mode = ...;
 *   Pmbus.vout_mode_kind(work);
 *   // Pmbus.kind is what the call reports
 *
 * @var PmbusNs::vout_mode_kind_args  what vout_mode_kind takes: vout_mode
 * @var PmbusNs::vout_exponent_args  what vout_exponent takes: vout_mode
 * @var PmbusNs::l11_mantissa_args  what l11_mantissa takes: word
 * @var PmbusNs::l11_exponent_args  what l11_exponent takes: word
 * @var PmbusNs::linear11_micro_args  what linear11_micro takes: word
 * @var PmbusNs::linear11_encode_args  what linear11_encode takes: micro
 * @var PmbusNs::linear16_micro_args  what linear16_micro takes: word, exponent
 * @var PmbusNs::linear16_encode_args  what linear16_encode takes: micro, exponent
 * @var PmbusNs::direct_micro_args  what direct_micro takes: word, m, b, r
 * @var PmbusNs::set_page_args  what set_page takes: addr, page
 * @var PmbusNs::read_vout_mode_args  what read_vout_mode takes: addr, out
 * @var PmbusNs::read_linear11_args  what read_linear11 takes: addr, cmd, micro
 * @var PmbusNs::read_linear16_args  what read_linear16 takes: addr, cmd, exponent, micro
 * @var PmbusNs::write_linear16_args  what write_linear16 takes: addr, cmd, exponent, micro
 * @var PmbusNs::status_byte_args  what status_byte takes: addr, out
 * @var PmbusNs::status_word_args  what status_word takes: addr, out
 * @var PmbusNs::clear_faults_args  what clear_faults takes: addr
 * @var PmbusNs::read_mfr_string_args  what read_mfr_string takes: addr, cmd, out, cap, len
 * @var PmbusNs::ok  a call's true/false outcome
 * @var PmbusNs::kind  what a call reports
 * @var PmbusNs::exp  what a call reports
 * @var PmbusNs::mantissa  what a call reports
 * @var PmbusNs::micro  the value, or ::PROTOCORE_PMBUS_INVALID if it does not fit in an ...
 * @var PmbusNs::word  what a call reports
 * @var PmbusNs::vout_mode_kind  the encoding VOUT_MODE names, from its bits 7:5
 * @var PmbusNs::vout_exponent  the exponent VOUT_MODE carries in its bits 4:0, sign-extended from ...
 * @var PmbusNs::l11_mantissa  the 11-bit signed mantissa of a LINEAR11 word, sign-extended
 * @var PmbusNs::l11_exponent  the 5-bit signed exponent of a LINEAR11 word, sign-extended
 * @var PmbusNs::linear11_micro  decode a LINEAR11 word to micro-units: mantissa * 2^exponent, ...
 * @var PmbusNs::linear11_encode  encode micro micro-units as a LINEAR11 word, picking the exponent ...
 * @var PmbusNs::linear16_micro  decode a LINEAR16 word to micro-units: an unsigned mantissa scaled ...
 * @var PmbusNs::linear16_encode  encode micro micro-units as a LINEAR16 word at exponent
 * @var PmbusNs::direct_micro  decode a DIRECT-format word to micro-units: (word / 10^r - b) / m, ...
 * @var PmbusNs::begin  bring up the bus for PMBus traffic
 * @var PmbusNs::set_page  select page page on addr; a multi-rail part answers per page
 * @var PmbusNs::read_vout_mode  read VOUT_MODE, whose exponent every LINEAR16 reading on this part ...
 * @var PmbusNs::read_linear11  read a LINEAR11 telemetry command (READ_VIN, READ_IOUT, READ_PIN, ...
 * @var PmbusNs::read_linear16  read a LINEAR16 command (READ_VOUT, VOUT_COMMAND) in microvolts at ...
 * @var PmbusNs::write_linear16  write micro microvolts to a LINEAR16 command at exponent
 * @var PmbusNs::status_byte  read STATUS_BYTE
 * @var PmbusNs::status_word  read STATUS_WORD, whose low half is STATUS_BYTE
 * @var PmbusNs::clear_faults  clear every latched fault on addr
 * @var PmbusNs::read_mfr_string  read one of the block-encoded manufacturer strings (MFR_ID, ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    PmbusVoutModeKindArgs vout_mode_kind_args;
    PmbusVoutExponentArgs vout_exponent_args;
    PmbusL11MantissaArgs l11_mantissa_args;
    PmbusL11ExponentArgs l11_exponent_args;
    PmbusLinear11MicroArgs linear11_micro_args;
    PmbusLinear11EncodeArgs linear11_encode_args;
    PmbusLinear16MicroArgs linear16_micro_args;
    PmbusLinear16EncodeArgs linear16_encode_args;
    PmbusDirectMicroArgs direct_micro_args;
    PmbusSetPageArgs set_page_args;
    PmbusReadVoutModeArgs read_vout_mode_args;
    PmbusReadLinear11Args read_linear11_args;
    PmbusReadLinear16Args read_linear16_args;
    PmbusWriteLinear16Args write_linear16_args;
    PmbusStatusByteArgs status_byte_args;
    PmbusStatusWordArgs status_word_args;
    PmbusClearFaultsArgs clear_faults_args;
    PmbusReadMfrStringArgs read_mfr_string_args;

    proto_bool ok;
    uint8_t kind;
    int8_t exp;
    int16_t mantissa;
    int32_t micro;
    uint16_t word;

    void (*const vout_mode_kind)(uint8_t *restrict work);
    void (*const vout_exponent)(uint8_t *restrict work);
    void (*const l11_mantissa)(uint8_t *restrict work);
    void (*const l11_exponent)(uint8_t *restrict work);
    void (*const linear11_micro)(uint8_t *restrict work);
    void (*const linear11_encode)(uint8_t *restrict work);
    void (*const linear16_micro)(uint8_t *restrict work);
    void (*const linear16_encode)(uint8_t *restrict work);
    void (*const direct_micro)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const set_page)(uint8_t *restrict work);
    void (*const read_vout_mode)(uint8_t *restrict work);
    void (*const read_linear11)(uint8_t *restrict work);
    void (*const read_linear16)(uint8_t *restrict work);
    void (*const write_linear16)(uint8_t *restrict work);
    void (*const status_byte)(uint8_t *restrict work);
    void (*const status_word)(uint8_t *restrict work);
    void (*const clear_faults)(uint8_t *restrict work);
    void (*const read_mfr_string)(uint8_t *restrict work);
} PmbusNs;

/** @brief The one symbol this module exports. */
extern PmbusNs Pmbus;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PMBUS

#endif // PROTOCORE_PMBUS_H
