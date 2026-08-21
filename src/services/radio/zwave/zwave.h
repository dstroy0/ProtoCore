// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_ZWAVE_H
#define PROTOCORE_ZWAVE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file zwave.h
 * @brief Z-Wave Serial API frame codec (PROTOCORE_ENABLE_ZWAVE) - Silicon Labs controller.
 *
 * The host-side Serial API of a Silicon Labs 500 / 700-series Z-Wave controller reached
 * over UART: a Z-Wave mesh bridged to the web. The host and the controller exchange **data
 * frames**:
 *
 * SOF (0x01) | LEN | Type | Command | Data... | Checksum
 *
 * where LEN counts Type + Command + Data + Checksum, Type is 0x00 (REQ) or 0x01 (RES), and
 * the checksum is 0xFF XOR-folded over LEN through the last Data byte. Each data frame is
 * acknowledged by a single-byte **ACK (0x06)**, or rejected with **NAK (0x15)** / **CAN
 * (0x18)**.
 *
 * protocore_zwave_build_frame() assembles a data frame carrying a function command, protocore_zwave_parse_frame()
 * frames + verifies one, and protocore_zwave_is_ack() / protocore_zwave_is_nak() / protocore_zwave_is_can() /
 * protocore_zwave_build_ack() handle the flow-control bytes. The per-command payload (GetVersion,
 * SendData, AddNodeToNetwork, an ApplicationCommandHandler report, ...) is the application's.
 * Pure - you carry the bytes over your UART - so it is fully host-testable.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_ZWAVE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Z-Wave Serial API control bytes / frame markers. */
#define ZWAVE_SOF 0x01 ///< start of a data frame
#define ZWAVE_ACK 0x06 ///< frame acknowledged
#define ZWAVE_NAK 0x15 ///< frame rejected (checksum)
#define ZWAVE_CAN 0x18 ///< frame cancelled (retransmit)

/** @brief Data-frame type. */
typedef enum PROTO_ENUM_PACKED
{
    ZWAVE_REQ = 0x00, ///< request
    ZWAVE_RES = 0x01, ///< response
} protocore_zwave_type;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    uint16_t (*build_frame)(uint8_t *, protocore_zwave_type, uint8_t, const uint8_t *, uint8_t, uint8_t *, uint16_t);
    int (*parse_frame)(uint8_t *, const uint8_t *, uint16_t, uint8_t *, uint8_t *, const uint8_t **, uint8_t *);
    proto_bool (*is_ack)(uint8_t *, uint8_t);
    proto_bool (*is_nak)(uint8_t *, uint8_t);
    proto_bool (*is_can)(uint8_t *, uint8_t);
    uint16_t (*build_ack)(uint8_t *, uint8_t *, uint16_t);
} ZwaveNs;
PROTOCORE_NS_LAYOUT(ZwaveNs, build_frame, parse_frame, is_ack, is_nak, is_can, build_ack);

/**
 * @brief Assemble a data frame carrying type + cmd + data into out.
 * @param work PROTOCORE_ZWAVE_BORROW bytes the caller took. Not held past the call.
 * @param type Type
 * @param cmd Cmd
 * @param data Data
 * @param data_len Data len
 * @param out Out
 * @param cap Cap
 * @return The uint16_t.
 */
uint16_t protocore_zwave_build_frame(uint8_t *work, protocore_zwave_type type, uint8_t cmd, const uint8_t *data,
                                     uint8_t data_len, uint8_t *out, uint16_t cap);
/**
 * @brief Frame one data frame from the front of raw and verify the checksum.
 * @param work PROTOCORE_ZWAVE_BORROW bytes the caller took. Not held past the call.
 * @param raw Raw
 * @param len Len
 * @param type Type
 * @param cmd Cmd
 * @param pdata Pdata
 * @param pdata_len Pdata len
 * @return The int.
 */
int protocore_zwave_parse_frame(uint8_t *work, const uint8_t *raw, uint16_t len, uint8_t *type, uint8_t *cmd,
                                const uint8_t **pdata, uint8_t *pdata_len);
/**
 * @brief True if b is the ACK control byte.
 * @param work PROTOCORE_ZWAVE_BORROW bytes the caller took. Not held past the call.
 * @param b B
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_zwave_is_ack(uint8_t *work, uint8_t b);
/**
 * @brief True if b is the NAK control byte.
 * @param work PROTOCORE_ZWAVE_BORROW bytes the caller took. Not held past the call.
 * @param b B
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_zwave_is_nak(uint8_t *work, uint8_t b);
/**
 * @brief True if b is the CAN control byte.
 * @param work PROTOCORE_ZWAVE_BORROW bytes the caller took. Not held past the call.
 * @param b B
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_zwave_is_can(uint8_t *work, uint8_t b);
/**
 * @brief Write the single ACK byte into out. 1, or 0 if cap < 1.
 * @param work PROTOCORE_ZWAVE_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @return The uint16_t.
 */
uint16_t protocore_zwave_build_ack(uint8_t *work, uint8_t *out, uint16_t cap);

/** @brief Module namespace. */
PROTOCORE_NS ZwaveNs Zwave PROTOCORE_UNUSED = {.build_frame = protocore_zwave_build_frame,
                                               .parse_frame = protocore_zwave_parse_frame,
                                               .is_ack = protocore_zwave_is_ack,
                                               .is_nak = protocore_zwave_is_nak,
                                               .is_can = protocore_zwave_is_can,
                                               .build_ack = protocore_zwave_build_ack};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ZWAVE_H
