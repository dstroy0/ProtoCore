// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_BLE_GATT_H
#define PROTOCORE_BLE_GATT_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */

// PROTOCORE_BLE_GATT_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*att_read_req)(uint8_t *, uint16_t, uint8_t *, size_t);
    size_t (*att_read_rsp)(uint8_t *, const uint8_t *, size_t, uint8_t *, size_t);
    size_t (*att_write_req)(uint8_t *, uint16_t, const uint8_t *, size_t, uint8_t *, size_t);
    size_t (*att_notify)(uint8_t *, uint16_t, const uint8_t *, size_t, uint8_t *, size_t);
    size_t (*att_error_rsp)(uint8_t *, uint8_t, uint16_t, uint8_t, uint8_t *, size_t);
    proto_bool (*att_parse)(uint8_t *, const uint8_t *, size_t, AttPdu *);
    size_t (*char_json)(uint8_t *, const GattChar *, size_t, char *, size_t);
} BleGattNs;
PROTOCORE_NS_LAYOUT(BleGattNs, att_read_req, att_read_rsp, att_write_req, att_notify, att_error_rsp, att_parse,
                    char_json);

/**
 * @brief Build a Read Request: [0x0A][handle:2 LE]. 3, or 0 on overflow.
 * @param work PROTOCORE_BLE_GATT_BORROW bytes the caller took. Not held past the call.
 * @param handle Handle
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_ble_gatt_att_read_req(uint8_t *work, uint16_t handle, uint8_t *out, size_t cap);
/**
 * @brief Build a Read Response: [0x0B][value...]. 1+vlen, or 0 on overflow.
 * @param work PROTOCORE_BLE_GATT_BORROW bytes the caller took. Not held past the call.
 * @param val Val
 * @param vlen Vlen
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_ble_gatt_att_read_rsp(uint8_t *work, const uint8_t *val, size_t vlen, uint8_t *out, size_t cap);
/**
 * @brief Build a Write Request: [0x12][handle:2 LE][value...]. 3+vlen, or 0 .
 * @param work PROTOCORE_BLE_GATT_BORROW bytes the caller took. Not held past the call.
 * @param handle Handle
 * @param val Val
 * @param vlen Vlen
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_ble_gatt_att_write_req(uint8_t *work, uint16_t handle, const uint8_t *val, size_t vlen, uint8_t *out,
                                        size_t cap);
/**
 * @brief Build a Handle Value Notification: [0x1B][handle:2 LE][value...]. .
 * @param work PROTOCORE_BLE_GATT_BORROW bytes the caller took. Not held past the call.
 * @param handle Handle
 * @param val Val
 * @param vlen Vlen
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_ble_gatt_att_notify(uint8_t *work, uint16_t handle, const uint8_t *val, size_t vlen, uint8_t *out,
                                     size_t cap);
/**
 * @brief Build an Error Response: [0x01][req-op][handle:2 LE][error]. 5, or .
 * @param work PROTOCORE_BLE_GATT_BORROW bytes the caller took. Not held past the call.
 * @param req_op Req op
 * @param handle Handle
 * @param error Error
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_ble_gatt_att_error_rsp(uint8_t *work, uint8_t req_op, uint16_t handle, uint8_t error, uint8_t *out,
                                        size_t cap);
/**
 * @brief Parse an ATT PDU into out. true if len >= 1 and the fixed fields fit.
 * @param work PROTOCORE_BLE_GATT_BORROW bytes the caller took. Not held past the call.
 * @param pdu Pdu
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ble_gatt_att_parse(uint8_t *work, const uint8_t *pdu, size_t len, AttPdu *out);
/**
 * @brief Serialize a characteristic table as .
 * @param work PROTOCORE_BLE_GATT_BORROW bytes the caller took. Not held past the call.
 * @param chars Chars
 * @param n N
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_ble_gatt_char_json(uint8_t *work, const GattChar *chars, size_t n, char *out, size_t cap);

/** @brief Module namespace. */
PROTOCORE_NS BleGattNs BleGatt PROTOCORE_UNUSED = {.att_read_req = protocore_ble_gatt_att_read_req,
                                                   .att_read_rsp = protocore_ble_gatt_att_read_rsp,
                                                   .att_write_req = protocore_ble_gatt_att_write_req,
                                                   .att_notify = protocore_ble_gatt_att_notify,
                                                   .att_error_rsp = protocore_ble_gatt_att_error_rsp,
                                                   .att_parse = protocore_ble_gatt_att_parse,
                                                   .char_json = protocore_ble_gatt_char_json};

PROTOCORE_END_DECLS

#endif // PROTOCORE_BLE_GATT_H
