// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file canopen.cpp
 * @brief CANopen (CiA 301) message codec (pure, host-tested).
 */

#include "services/fieldbus/canopen/canopen.h"

#if PC_ENABLE_CANOPEN

#include <string.h>

// All CANopen default-profile identifiers are 11-bit standard frames.
static void std_frame(CanFrame *f, uint32_t id, uint8_t dlc)
{
    f->id = id & PC_CAN_STD_ID_MASK;
    f->extended = false;
    f->rtr = false;
    f->dlc = dlc;
    memset(f->data, 0, sizeof(f->data));
}

static bool valid_node(uint8_t node_id)
{
    return node_id >= 1 && node_id <= 127;
}

bool pc_canopen_build_nmt(CanFrame *out, uint8_t command, uint8_t node_id)
{
    if (!out || node_id > 127) // 0 = all nodes
    {
        return false;
    }
    std_frame(out, CANOPEN_COB_NMT, 2);
    out->data[0] = command;
    out->data[1] = node_id;
    return true;
}

bool pc_canopen_build_sync(CanFrame *out)
{
    if (!out)
    {
        return false;
    }
    std_frame(out, CANOPEN_COB_SYNC, 0);
    return true;
}

bool pc_canopen_build_time(CanFrame *out, uint32_t ms_since_midnight, uint16_t days_since_1984)
{
    if (!out)
    {
        return false;
    }
    std_frame(out, CANOPEN_COB_TIME, CANOPEN_TIME_LEN);
    uint32_t ms = ms_since_midnight & CANOPEN_TIME_MS_MASK; // 28-bit ms after midnight, top 4 bits reserved
    out->data[0] = (uint8_t)ms;                             // little-endian
    out->data[1] = (uint8_t)(ms >> 8);
    out->data[2] = (uint8_t)(ms >> 16);
    out->data[3] = (uint8_t)(ms >> 24);
    out->data[4] = (uint8_t)days_since_1984; // days since 1984-01-01, little-endian
    out->data[5] = (uint8_t)(days_since_1984 >> 8);
    return true;
}

bool pc_canopen_build_heartbeat(CanFrame *out, uint8_t node_id, uint8_t state)
{
    if (!out || !valid_node(node_id))
    {
        return false;
    }
    std_frame(out, CANOPEN_COB_HEARTBEAT + node_id, 1);
    out->data[0] = state;
    return true;
}

bool pc_canopen_build_emcy(CanFrame *out, uint8_t node_id, uint16_t error_code, uint8_t error_reg,
                           const uint8_t msef[5])
{
    if (!out || !valid_node(node_id))
    {
        return false;
    }
    std_frame(out, CANOPEN_COB_EMCY + node_id, 8);
    out->data[0] = (uint8_t)error_code; // error code, little-endian
    out->data[1] = (uint8_t)(error_code >> 8);
    out->data[2] = error_reg; // object 0x1001 error register
    if (msef)
    {
        memcpy(out->data + 3, msef, 5); // 5 manufacturer-specific error octets
    }
    return true;
}

// Map a PDO number (1..4) to its TPDO / RPDO COB-ID base.
static bool pdo_base(uint8_t pdo_num, bool transmit, uint32_t *base)
{
    if (pdo_num < 1 || pdo_num > 4)
    {
        return false;
    }
    static const uint32_t tx[4] = {CANOPEN_COB_TPDO1, CANOPEN_COB_TPDO2, CANOPEN_COB_TPDO3, CANOPEN_COB_TPDO4};
    static const uint32_t rx[4] = {CANOPEN_COB_RPDO1, CANOPEN_COB_RPDO2, CANOPEN_COB_RPDO3, CANOPEN_COB_RPDO4};
    *base = (transmit ? tx : rx)[pdo_num - 1];
    return true;
}

static bool build_pdo(CanFrame *out, uint8_t pdo_num, bool transmit, uint8_t node_id, const uint8_t *data, uint8_t len)
{
    uint32_t base;
    if (!out || !valid_node(node_id) || len > PC_CAN_MAX_DLC || (len && !data) || !pdo_base(pdo_num, transmit, &base))
    {
        return false;
    }
    std_frame(out, base + node_id, len);
    if (len)
    {
        memcpy(out->data, data, len);
    }
    return true;
}

bool pc_canopen_build_tpdo(CanFrame *out, uint8_t pdo_num, uint8_t node_id, const uint8_t *data, uint8_t len)
{
    return build_pdo(out, pdo_num, true, node_id, data, len);
}

bool pc_canopen_build_rpdo(CanFrame *out, uint8_t pdo_num, uint8_t node_id, const uint8_t *data, uint8_t len)
{
    return build_pdo(out, pdo_num, false, node_id, data, len);
}

// Fill data[1..3] with the object index (LE) + sub-index common to every SDO frame.
static void sdo_set_object(CanFrame *f, uint16_t index, uint8_t sub)
{
    f->data[1] = (uint8_t)index;
    f->data[2] = (uint8_t)(index >> 8);
    f->data[3] = sub;
}

bool pc_canopen_build_sdo_read(CanFrame *out, uint8_t node_id, uint16_t index, uint8_t sub)
{
    if (!out || !valid_node(node_id))
    {
        return false;
    }
    std_frame(out, CANOPEN_COB_SDO_RX + node_id, 8);
    out->data[0] = (uint8_t)(CANOPEN_SDO_CCS_UPLOAD << 5); // upload initiate request (0x40)
    sdo_set_object(out, index, sub);
    return true;
}

bool pc_canopen_build_sdo_write(CanFrame *out, uint8_t node_id, uint16_t index, uint8_t sub, const uint8_t *data,
                                uint8_t len)
{
    if (!out || !valid_node(node_id) || len < 1 || len > 4 || !data)
    {
        return false;
    }
    std_frame(out, CANOPEN_COB_SDO_RX + node_id, 8);
    // download initiate, expedited (e=1), size indicated (s=1); n = unused octets in data[4..7].
    out->data[0] = (uint8_t)((CANOPEN_SDO_CCS_DOWNLOAD << 5) | (((4u - len) & 3u) << 2) | 0x03u);
    sdo_set_object(out, index, sub);
    memcpy(out->data + 4, data, len);
    return true;
}

bool pc_canopen_build_sdo_abort(CanFrame *out, uint8_t node_id, uint16_t index, uint8_t sub, uint32_t abort_code,
                                bool to_server)
{
    if (!out || !valid_node(node_id))
    {
        return false;
    }
    std_frame(out, (to_server ? CANOPEN_COB_SDO_RX : CANOPEN_COB_SDO_TX) + node_id, 8);
    out->data[0] = (uint8_t)(CANOPEN_SDO_ABORT << 5); // 0x80
    sdo_set_object(out, index, sub);
    out->data[4] = (uint8_t)abort_code; // abort code, little-endian
    out->data[5] = (uint8_t)(abort_code >> 8);
    out->data[6] = (uint8_t)(abort_code >> 16);
    out->data[7] = (uint8_t)(abort_code >> 24);
    return true;
}

bool pc_canopen_parse(const CanFrame *f, CanopenMsg *out)
{
    if (!f || !out || f->extended)
    {
        return false; // CANopen default profile is 11-bit standard frames
    }
    uint32_t id = f->id & PC_CAN_STD_ID_MASK;
    uint32_t func = id & CANOPEN_FUNC_MASK;
    uint8_t node = (uint8_t)(id & CANOPEN_NODE_MASK);
    out->type = CanopenType::CANOPEN_T_UNKNOWN;
    out->node_id = node;
    out->pdo_num = 0;

    if (id == CANOPEN_COB_NMT)
    {
        out->type = CanopenType::CANOPEN_T_NMT;
        out->node_id = 0;
        return true;
    }
    if (id == CANOPEN_COB_SYNC) // function 0x080 with node 0
    {
        out->type = CanopenType::CANOPEN_T_SYNC;
        out->node_id = 0;
        return true;
    }
    if (id == CANOPEN_COB_TIME)
    {
        out->type = CanopenType::CANOPEN_T_TIME;
        out->node_id = 0;
        return true;
    }
    if (node == 0)
    {
        return true; // a function base with node 0 we don't classify further
    }

    switch (func)
    {
    case CANOPEN_COB_EMCY:
        out->type = CanopenType::CANOPEN_T_EMCY;
        return true;
    case CANOPEN_COB_TPDO1:
        out->type = CanopenType::CANOPEN_T_TPDO;
        out->pdo_num = 1;
        return true;
    case CANOPEN_COB_RPDO1:
        out->type = CanopenType::CANOPEN_T_RPDO;
        out->pdo_num = 1;
        return true;
    case CANOPEN_COB_TPDO2:
        out->type = CanopenType::CANOPEN_T_TPDO;
        out->pdo_num = 2;
        return true;
    case CANOPEN_COB_RPDO2:
        out->type = CanopenType::CANOPEN_T_RPDO;
        out->pdo_num = 2;
        return true;
    case CANOPEN_COB_TPDO3:
        out->type = CanopenType::CANOPEN_T_TPDO;
        out->pdo_num = 3;
        return true;
    case CANOPEN_COB_RPDO3:
        out->type = CanopenType::CANOPEN_T_RPDO;
        out->pdo_num = 3;
        return true;
    case CANOPEN_COB_TPDO4:
        out->type = CanopenType::CANOPEN_T_TPDO;
        out->pdo_num = 4;
        return true;
    case CANOPEN_COB_RPDO4:
        out->type = CanopenType::CANOPEN_T_RPDO;
        out->pdo_num = 4;
        return true;
    case CANOPEN_COB_SDO_TX:
        out->type = CanopenType::CANOPEN_T_SDO_TX;
        return true;
    case CANOPEN_COB_SDO_RX:
        out->type = CanopenType::CANOPEN_T_SDO_RX;
        return true;
    case CANOPEN_COB_HEARTBEAT:
        out->type = CanopenType::CANOPEN_T_HEARTBEAT;
        return true;
    default:
        return true; // unknown function code: type stays CanopenType::CANOPEN_T_UNKNOWN
    }
}

bool pc_canopen_parse_emcy(const CanFrame *f, uint8_t *node_id, uint16_t *error_code, uint8_t *error_reg,
                           uint8_t msef[5])
{
    if (!f || f->extended || f->dlc < 8)
    {
        return false;
    }
    uint32_t id = f->id & PC_CAN_STD_ID_MASK;
    uint8_t node = (uint8_t)(id & CANOPEN_NODE_MASK);
    if ((id & CANOPEN_FUNC_MASK) != CANOPEN_COB_EMCY || node == 0)
    {
        return false; // 0x080 with node 0 is SYNC, not EMCY
    }
    if (node_id)
    {
        *node_id = node;
    }
    if (error_code)
    {
        *error_code = (uint16_t)(f->data[0] | (f->data[1] << 8));
    }
    if (error_reg)
    {
        *error_reg = f->data[2];
    }
    if (msef)
    {
        memcpy(msef, f->data + 3, 5);
    }
    return true;
}

bool pc_canopen_parse_heartbeat(const CanFrame *f, uint8_t *node_id, uint8_t *state)
{
    if (!f || f->extended || f->dlc < 1)
    {
        return false;
    }
    uint32_t id = f->id & PC_CAN_STD_ID_MASK;
    uint8_t node = (uint8_t)(id & CANOPEN_NODE_MASK);
    if ((id & CANOPEN_FUNC_MASK) != CANOPEN_COB_HEARTBEAT || node == 0)
    {
        return false;
    }
    if (node_id)
    {
        *node_id = node;
    }
    if (state)
    {
        *state = (uint8_t)(f->data[0] & 0x7Fu); // bit 7 is the boot toggle in some stacks
    }
    return true;
}

bool pc_canopen_parse_time(const CanFrame *f, CanopenTime *out)
{
    if (!f || !out || f->extended || f->dlc < CANOPEN_TIME_LEN)
    {
        return false;
    }
    if ((f->id & PC_CAN_STD_ID_MASK) != CANOPEN_COB_TIME)
    {
        return false;
    }
    uint32_t ms = (uint32_t)f->data[0] | ((uint32_t)f->data[1] << 8) | ((uint32_t)f->data[2] << 16) |
                  ((uint32_t)f->data[3] << 24);
    out->ms_since_midnight = ms & CANOPEN_TIME_MS_MASK; // discard the reserved top 4 bits
    out->days_since_1984 = (uint16_t)(f->data[4] | (f->data[5] << 8));
    return true;
}

bool pc_canopen_parse_sdo_response(const CanFrame *f, CanopenSdoResponse *out)
{
    if (!f || !out || f->extended || f->dlc < 8)
    {
        return false;
    }
    uint32_t id = f->id & PC_CAN_STD_ID_MASK;
    if ((id & CANOPEN_FUNC_MASK) != CANOPEN_COB_SDO_TX || (id & CANOPEN_NODE_MASK) == 0)
    {
        return false;
    }

    uint8_t cmd = f->data[0];
    uint8_t scs = (uint8_t)(cmd >> 5);
    out->index = (uint16_t)(f->data[1] | (f->data[2] << 8));
    out->sub = f->data[3];
    out->is_abort = false;
    out->abort_code = 0;
    out->is_upload = false;
    out->expedited = false;
    out->len = 0;
    memset(out->data, 0, sizeof(out->data));

    if (scs == CANOPEN_SDO_ABORT)
    {
        out->is_abort = true;
        out->abort_code = (uint32_t)f->data[4] | ((uint32_t)f->data[5] << 8) | ((uint32_t)f->data[6] << 16) |
                          ((uint32_t)f->data[7] << 24);
        return true;
    }
    if (scs == CANOPEN_SDO_SCS_UPLOAD) // upload initiate response
    {
        out->is_upload = true;
        bool e = (cmd & 0x02u) != 0; // expedited
        bool s = (cmd & 0x01u) != 0; // size indicated
        if (e)
        {
            out->expedited = true;
            out->len = s ? (uint8_t)(4u - ((cmd >> 2) & 0x03u)) : 4u;
            memcpy(out->data, f->data + 4, out->len);
        }
        return true; // a non-expedited (segmented) response is reported with len 0
    }
    if (scs == CANOPEN_SDO_SCS_DOWNLOAD) // download initiate response (write acknowledged)
    {
        return true;
    }
    return false; // not a recognized server command specifier
}

// --- segmented SDO (CiA 301 §7.2.4.3) ---

bool pc_canopen_build_sdo_download_init(CanFrame *out, uint8_t node_id, uint16_t index, uint8_t sub,
                                        uint32_t total_size)
{
    if (!out || !valid_node(node_id))
    {
        return false;
    }
    std_frame(out, CANOPEN_COB_SDO_RX + node_id, 8);
    // download initiate, segmented (e=0), size indicated (s=1) -> command 0x21; size in data[4..7] LE.
    out->data[0] = (uint8_t)((CANOPEN_SDO_CCS_DOWNLOAD << 5) | 0x01u);
    sdo_set_object(out, index, sub);
    out->data[4] = (uint8_t)total_size;
    out->data[5] = (uint8_t)(total_size >> 8);
    out->data[6] = (uint8_t)(total_size >> 16);
    out->data[7] = (uint8_t)(total_size >> 24);
    return true;
}

bool pc_canopen_build_sdo_download_segment(CanFrame *out, uint8_t node_id, bool toggle, const uint8_t *data,
                                           uint8_t len, bool last)
{
    if (!out || !valid_node(node_id) || !data || len < 1 || len > CANOPEN_SDO_SEG_DATA)
    {
        return false;
    }
    std_frame(out, CANOPEN_COB_SDO_RX + node_id, 8);
    // segment: ccs=0, t=toggle (bit 4), n=unused octets (bits 1..3), c=last (bit 0).
    uint8_t n = (uint8_t)(CANOPEN_SDO_SEG_DATA - len);
    out->data[0] = (uint8_t)((toggle ? 0x10u : 0u) | ((n & 0x07u) << 1) | (last ? 0x01u : 0u));
    memcpy(out->data + 1, data, len);
    return true;
}

bool pc_canopen_build_sdo_upload_segment_req(CanFrame *out, uint8_t node_id, bool toggle)
{
    if (!out || !valid_node(node_id))
    {
        return false;
    }
    std_frame(out, CANOPEN_COB_SDO_RX + node_id, 8);
    // upload segment request: ccs=3, t=toggle (bit 4).
    out->data[0] = (uint8_t)((3u << 5) | (toggle ? 0x10u : 0u));
    return true;
}

bool pc_canopen_parse_sdo_segment(const CanFrame *f, bool *toggle, uint8_t *data, uint8_t *len, bool *last)
{
    if (!f || f->dlc < 8 || (f->data[0] & 0xE0u) != 0u) // segment form: command specifier (high 3 bits) is 0
    {
        return false;
    }
    uint8_t n = (uint8_t)((f->data[0] >> 1) & 0x07u);
    uint8_t seg_len = (uint8_t)(CANOPEN_SDO_SEG_DATA - n);
    if (toggle)
    {
        *toggle = (f->data[0] & 0x10u) != 0u;
    }
    if (last)
    {
        *last = (f->data[0] & 0x01u) != 0u;
    }
    if (len)
    {
        *len = seg_len;
    }
    if (data)
    {
        memcpy(data, f->data + 1, seg_len);
    }
    return true;
}

void pc_canopen_sdo_reasm_init(CanopenSdoReasm *r, uint8_t *buf, size_t cap)
{
    if (!r)
    {
        return;
    }
    r->buf = buf;
    r->cap = cap;
    r->len = 0;
    r->expect_toggle = false; // the first segment carries toggle 0
    r->done = false;
}

bool pc_canopen_sdo_reasm_feed(CanopenSdoReasm *r, const uint8_t *data, uint8_t len, bool toggle, bool last)
{
    if (!r || !r->buf || r->done || (len && !data))
    {
        return false;
    }
    if (toggle != r->expect_toggle) // toggles must alternate 0,1,0,1
    {
        return false;
    }
    if (len > r->cap - r->len) // would overflow the buffer
    {
        return false;
    }
    if (len)
    {
        memcpy(r->buf + r->len, data, len);
    }
    r->len += len;
    r->expect_toggle = !r->expect_toggle;
    if (last)
    {
        r->done = true;
    }
    return true;
}

#endif // PC_ENABLE_CANOPEN
