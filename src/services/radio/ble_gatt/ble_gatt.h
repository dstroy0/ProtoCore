// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ble_gatt.h
 * @brief Bluetooth ATT protocol codec + GATT characteristic bridge (PC_ENABLE_BLE_GATT).
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

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_BLE_GATT

/** @brief ATT opcodes (subset). */
struct AttOp
{
    static constexpr uint8_t ATT_OP_ERROR_RSP = 0x01;        ///< [op][req-op][handle:2][error]
    static constexpr uint8_t ATT_OP_READ_REQ = 0x0A;         ///< [op][handle:2]
    static constexpr uint8_t ATT_OP_READ_RSP = 0x0B;         ///< [op][value...]
    static constexpr uint8_t ATT_OP_WRITE_REQ = 0x12;        ///< [op][handle:2][value...]
    static constexpr uint8_t ATT_OP_WRITE_RSP = 0x13;        ///< [op]
    static constexpr uint8_t ATT_OP_HANDLE_VALUE_NTF = 0x1B; ///< [op][handle:2][value...]
};

/** @brief GATT characteristic property bits (declaration properties byte). */
struct GattProp
{
    static constexpr uint8_t GATT_PROP_READ = 0x02;
    static constexpr uint8_t GATT_PROP_WRITE_NR = 0x04; ///< write without response.
    static constexpr uint8_t GATT_PROP_WRITE = 0x08;
    static constexpr uint8_t GATT_PROP_NOTIFY = 0x10;
    static constexpr uint8_t GATT_PROP_INDICATE = 0x20;
};

/** @brief Build a Read Request: [0x0A][handle:2 LE]. @return 3, or 0 on overflow. */
size_t att_read_req(uint16_t handle, uint8_t *out, size_t cap);

/** @brief Build a Read Response: [0x0B][value...]. @return 1+vlen, or 0 on overflow. */
size_t att_read_rsp(const uint8_t *val, size_t vlen, uint8_t *out, size_t cap);

/** @brief Build a Write Request: [0x12][handle:2 LE][value...]. @return 3+vlen, or 0 on overflow. */
size_t att_write_req(uint16_t handle, const uint8_t *val, size_t vlen, uint8_t *out, size_t cap);

/** @brief Build a Handle Value Notification: [0x1B][handle:2 LE][value...]. @return 3+vlen, or 0. */
size_t att_notify(uint16_t handle, const uint8_t *val, size_t vlen, uint8_t *out, size_t cap);

/** @brief Build an Error Response: [0x01][req-op][handle:2 LE][error]. @return 5, or 0 on overflow. */
size_t att_error_rsp(uint8_t req_op, uint16_t handle, uint8_t error, uint8_t *out, size_t cap);

/** @brief A parsed ATT PDU (value points into the input). */
struct AttPdu
{
    uint8_t opcode;
    uint16_t handle;      ///< set for opcodes that carry a handle (else 0).
    uint8_t req_op;       ///< for ERROR_RSP: the failed request opcode.
    uint8_t error;        ///< for ERROR_RSP: the error code.
    const uint8_t *value; ///< value payload (null if none).
    size_t value_len;
};

/** @brief Parse an ATT PDU into @p out. @return true if @p len >= 1 and the fixed fields fit. */
bool att_parse(const uint8_t *pdu, size_t len, AttPdu *out);

/** @brief One GATT characteristic for the northbound bridge. */
struct GattChar
{
    uint16_t handle;
    uint16_t uuid; ///< 16-bit UUID (assigned-number form).
    uint8_t props; ///< GATT_PROP_* bits.
};

/**
 * @brief Serialize a characteristic table as `[{"handle":H,"uuid":"0xXXXX","props":P},...]` for the web.
 * @return length written (excl NUL), or 0 on overflow / bad args.
 */
size_t pc_gatt_char_json(const GattChar *chars, size_t n, char *out, size_t cap);

#endif // PC_ENABLE_BLE_GATT
#endif // PROTOCORE_BLE_GATT_H
