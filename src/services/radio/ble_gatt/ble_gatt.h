// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ble_gatt.h
 * @brief Bluetooth ATT protocol codec + GATT characteristic bridge (PROTOCORE_ENABLE_BLE_GATT).
 *
 * The ESP32's BLE radio is on-chip, but bridging GATT to the web still needs the wire protocol under
 * GATT - the **Attribute Protocol** (ATT, Bluetooth Core Vol 3 Part F): the read / write / notify /
 * error PDUs a central and peripheral exchange, each a 1-byte opcode followed by a little-endian
 * attribute handle and value. This is that codec (build + parse the common ATT PDUs) plus a small
 * characteristic table serializer that exposes discovered / offered GATT characteristics as JSON for the
 * web stack.
 *
 * Pure, zero heap, no stdlib, host-testable. The BLE stack (NimBLE / Bluedroid) owns the radio; this owns
 * the ATT bytes and the northbound JSON.
 */

#ifndef PROTOCORE_BLE_GATT_H
#define PROTOCORE_BLE_GATT_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_BLE_GATT

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief ATT opcodes (subset). */
#define ATT_OP_ERROR_RSP 0x01        ///< [op][req-op][handle:2][error]
#define ATT_OP_READ_REQ 0x0A         ///< [op][handle:2]
#define ATT_OP_READ_RSP 0x0B         ///< [op][value...]
#define ATT_OP_WRITE_REQ 0x12        ///< [op][handle:2][value...]
#define ATT_OP_WRITE_RSP 0x13        ///< [op]
#define ATT_OP_HANDLE_VALUE_NTF 0x1B ///< [op][handle:2][value...]

/** @brief GATT characteristic property bits (declaration properties byte). */
#define GATT_PROP_READ 0x02
#define GATT_PROP_WRITE_NR 0x04 ///< write without response.
#define GATT_PROP_WRITE 0x08
#define GATT_PROP_NOTIFY 0x10
#define GATT_PROP_INDICATE 0x20

/** @brief A parsed ATT PDU (value points into the input). */
typedef struct
{
    uint8_t opcode;
    uint16_t handle;      ///< set for opcodes that carry a handle (else 0).
    uint8_t req_op;       ///< for ERROR_RSP: the failed request opcode.
    uint8_t error;        ///< for ERROR_RSP: the error code.
    const uint8_t *value; ///< value payload (null if none).
    size_t value_len;
} AttPdu;
/** @brief One GATT characteristic for the northbound bridge. */
typedef struct
{
    uint16_t handle;
    uint16_t uuid; ///< 16-bit UUID (assigned-number form).
    uint8_t props; ///< GATT_PROP_* bits.
} GattChar;
/** @brief What att_read_req takes: handle, out, cap. */
typedef struct
{
    uint16_t handle;
    uint8_t *out;
    size_t cap;
} BleGattAttReadReqArgs;
/** @brief What att_read_rsp takes: val, vlen, out, cap. */
typedef struct
{
    const uint8_t *val;
    size_t vlen;
    uint8_t *out;
    size_t cap;
} BleGattAttReadRspArgs;
/** @brief What att_write_req takes: handle, val, vlen, out, cap. */
typedef struct
{
    uint16_t handle;
    const uint8_t *val;
    size_t vlen;
    uint8_t *out;
    size_t cap;
} BleGattAttWriteReqArgs;
/** @brief What att_notify takes: handle, val, vlen, out, cap. */
typedef struct
{
    uint16_t handle;
    const uint8_t *val;
    size_t vlen;
    uint8_t *out;
    size_t cap;
} BleGattAttNotifyArgs;
/** @brief What att_error_rsp takes: req_op, handle, error, out, cap. */
typedef struct
{
    uint8_t req_op;
    uint16_t handle;
    uint8_t error;
    uint8_t *out;
    size_t cap;
} BleGattAttErrorRspArgs;
/** @brief What att_parse takes: pdu, len, out. */
typedef struct
{
    const uint8_t *pdu;
    size_t len;
    AttPdu *out;
} BleGattAttParseArgs;
/** @brief What char_json takes: chars, n, out, cap. */
typedef struct
{
    const GattChar *chars;
    size_t n;
    char *out;
    size_t cap;
} BleGattCharJsonArgs;
/**
 * @brief Bluetooth ATT protocol codec + GATT characteristic bridge (PROTOCORE_ENABLE_BLE_GATT).
 *
 * A caller sets the members a call takes, invokes it through ::BleGatt with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   BleGatt.att_read_req_args.handle = ...;
 *   BleGatt.att_read_req_args.out = ...;
 *   BleGatt.att_read_req_args.cap = ...;
 *   BleGatt.att_read_req(work);
 *   // BleGatt.n is what the call reports
 *
 * @var BleGattNs::att_read_req_args  what att_read_req takes: handle, out, cap
 * @var BleGattNs::att_read_rsp_args  what att_read_rsp takes: val, vlen, out, cap
 * @var BleGattNs::att_write_req_args  what att_write_req takes: handle, val, vlen, out, cap
 * @var BleGattNs::att_notify_args  what att_notify takes: handle, val, vlen, out, cap
 * @var BleGattNs::att_error_rsp_args  what att_error_rsp takes: req_op, handle, error, out, cap
 * @var BleGattNs::att_parse_args  what att_parse takes: pdu, len, out
 * @var BleGattNs::char_json_args  what char_json takes: chars, n, out, cap
 * @var BleGattNs::ok  a call's true/false outcome
 * @var BleGattNs::n  length written (excl NUL), or 0 on overflow / bad args
 * @var BleGattNs::att_read_req  build a Read Request: [0x0A][handle:2 LE]. 3, or 0 on overflow
 * @var BleGattNs::att_read_rsp  build a Read Response: [0x0B][value...]. 1+vlen, or 0 on overflow
 * @var BleGattNs::att_write_req  build a Write Request: [0x12][handle:2 LE][value...]. 3+vlen, or 0 ...
 * @var BleGattNs::att_notify  build a Handle Value Notification: [0x1B][handle:2 LE][value...]. ...
 * @var BleGattNs::att_error_rsp  build an Error Response: [0x01][req-op][handle:2 LE][error]. 5, or ...
 * @var BleGattNs::att_parse  parse an ATT PDU into out. true if len >= 1 and the fixed fields fit
 * @var BleGattNs::char_json  serialize a characteristic table as ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    BleGattAttReadReqArgs att_read_req_args;
    BleGattAttReadRspArgs att_read_rsp_args;
    BleGattAttWriteReqArgs att_write_req_args;
    BleGattAttNotifyArgs att_notify_args;
    BleGattAttErrorRspArgs att_error_rsp_args;
    BleGattAttParseArgs att_parse_args;
    BleGattCharJsonArgs char_json_args;
    proto_bool ok;
    size_t n;
} BleGattVars;

/** @brief The operands and the outcome. */
extern BleGattVars BleGattV;

/** @brief The entries. */
typedef struct
{
    void (*const att_read_req)(uint8_t *restrict work);
    void (*const att_read_rsp)(uint8_t *restrict work);
    void (*const att_write_req)(uint8_t *restrict work);
    void (*const att_notify)(uint8_t *restrict work);
    void (*const att_error_rsp)(uint8_t *restrict work);
    void (*const att_parse)(uint8_t *restrict work);
    void (*const char_json)(uint8_t *restrict work);
} BleGattNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in BleGattV or a region of the borrow at a fixed offset.
void protocore_ble_gatt_att_read_req(uint8_t *restrict work);
void protocore_ble_gatt_att_read_rsp(uint8_t *restrict work);
void protocore_ble_gatt_att_write_req(uint8_t *restrict work);
void protocore_ble_gatt_att_notify(uint8_t *restrict work);
void protocore_ble_gatt_att_error_rsp(uint8_t *restrict work);
void protocore_ble_gatt_att_parse(uint8_t *restrict work);
void protocore_ble_gatt_char_json(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `BleGatt.att_read_req(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const BleGattNs BleGatt __attribute__((unused)) = {
    .att_read_req = protocore_ble_gatt_att_read_req,
    .att_read_rsp = protocore_ble_gatt_att_read_rsp,
    .att_write_req = protocore_ble_gatt_att_write_req,
    .att_notify = protocore_ble_gatt_att_notify,
    .att_error_rsp = protocore_ble_gatt_att_error_rsp,
    .att_parse = protocore_ble_gatt_att_parse,
    .char_json = protocore_ble_gatt_char_json,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_BLE_GATT

#endif // PROTOCORE_BLE_GATT_H
