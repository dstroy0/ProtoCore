// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file canopen.c
 * @brief CANopen (CiA 301) message codec (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CANOPEN

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/canopen/canopen.h"
#include "shared/can/can.h"

PROTOCORE_BEGIN_DECLS

// All CANopen default-profile identifiers are 11-bit standard frames.
static void std_frame(CanFrame *f, uint32_t id, uint8_t dlc)
{
    f->id = id & PROTOCORE_CAN_STD_ID_MASK;
    f->extended = PROTO_FALSE;
    f->rtr = PROTO_FALSE;
    f->dlc = dlc;
    mem.set(f->data, 0, sizeof(f->data));
}

static proto_bool valid_node(uint8_t node_id)
{
    return node_id >= 1 && node_id <= 127;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_canopen_build_nmt(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_nmt_args.out;
    uint8_t command = CanopenV.build_nmt_args.command;
    uint8_t node_id = CanopenV.build_nmt_args.node_id;

    if (!out || node_id > 127) // 0 = all nodes
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, CANOPEN_COB_NMT, 2);
    out->data[0] = command;
    out->data[1] = node_id;
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_build_sync(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_sync_args.out;

    if (!out)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, CANOPEN_COB_SYNC, 0);
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_build_time(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_time_args.out;
    uint32_t ms_since_midnight = CanopenV.build_time_args.ms_since_midnight;
    uint16_t days_since_1984 = CanopenV.build_time_args.days_since_1984;

    if (!out)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, CANOPEN_COB_TIME, CANOPEN_TIME_LEN);
    uint32_t ms = ms_since_midnight & CANOPEN_TIME_MS_MASK; // 28-bit ms after midnight, top 4 bits reserved
    out->data[0] = (uint8_t)ms;                             // little-endian
    out->data[1] = (uint8_t)(ms >> 8);
    out->data[2] = (uint8_t)(ms >> 16);
    out->data[3] = (uint8_t)(ms >> 24);
    out->data[4] = (uint8_t)days_since_1984; // days since 1984-01-01, little-endian
    out->data[5] = (uint8_t)(days_since_1984 >> 8);
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_build_heartbeat(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_heartbeat_args.out;
    uint8_t node_id = CanopenV.build_heartbeat_args.node_id;
    uint8_t state = CanopenV.build_heartbeat_args.state;

    if (!out || !valid_node(node_id))
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, CANOPEN_COB_HEARTBEAT + node_id, 1);
    out->data[0] = state;
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_build_emcy(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_emcy_args.out;
    uint8_t node_id = CanopenV.build_emcy_args.node_id;
    uint16_t error_code = CanopenV.build_emcy_args.error_code;
    uint8_t error_reg = CanopenV.build_emcy_args.error_reg;
    const uint8_t *msef = CanopenV.build_emcy_args.msef;

    if (!out || !valid_node(node_id))
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, CANOPEN_COB_EMCY + node_id, 8);
    out->data[0] = (uint8_t)error_code; // error code, little-endian
    out->data[1] = (uint8_t)(error_code >> 8);
    out->data[2] = error_reg; // object 0x1001 error register
    if (msef)
    {
        mem.cpy(out->data + 3, msef, 5); // 5 manufacturer-specific error octets
    }
    CanopenV.ok = PROTO_TRUE;
}

// Map a PDO number (1..4) to its TPDO / RPDO COB-ID base.
static proto_bool pdo_base(uint8_t pdo_num, proto_bool transmit, uint32_t *base)
{
    if (pdo_num < 1 || pdo_num > 4)
    {
        return PROTO_FALSE;
    }
    static const uint32_t tx[4] = {CANOPEN_COB_TPDO1, CANOPEN_COB_TPDO2, CANOPEN_COB_TPDO3, CANOPEN_COB_TPDO4};
    static const uint32_t rx[4] = {CANOPEN_COB_RPDO1, CANOPEN_COB_RPDO2, CANOPEN_COB_RPDO3, CANOPEN_COB_RPDO4};
    *base = (transmit ? tx : rx)[pdo_num - 1];
    return PROTO_TRUE;
}

static proto_bool build_pdo(CanFrame *out, uint8_t pdo_num, proto_bool transmit, uint8_t node_id, const uint8_t *data,
                            uint8_t len)
{
    uint32_t base;
    if (!out || !valid_node(node_id) || len > PROTOCORE_CAN_MAX_DLC || (len && !data) ||
        !pdo_base(pdo_num, transmit, &base))
    {
        return PROTO_FALSE;
    }
    std_frame(out, base + node_id, len);
    if (len)
    {
        mem.cpy(out->data, data, len);
    }
    return PROTO_TRUE;
}

void protocore_canopen_build_tpdo(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_tpdo_args.out;
    uint8_t pdo_num = CanopenV.build_tpdo_args.pdo_num;
    uint8_t node_id = CanopenV.build_tpdo_args.node_id;
    const uint8_t *data = CanopenV.build_tpdo_args.data;
    uint8_t len = CanopenV.build_tpdo_args.len;

    CanopenV.ok = build_pdo(out, pdo_num, PROTO_TRUE, node_id, data, len);
}

void protocore_canopen_build_rpdo(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_rpdo_args.out;
    uint8_t pdo_num = CanopenV.build_rpdo_args.pdo_num;
    uint8_t node_id = CanopenV.build_rpdo_args.node_id;
    const uint8_t *data = CanopenV.build_rpdo_args.data;
    uint8_t len = CanopenV.build_rpdo_args.len;

    CanopenV.ok = build_pdo(out, pdo_num, PROTO_FALSE, node_id, data, len);
}

// Fill data[1..3] with the object index (LE) + sub-index common to every SDO frame.
static void sdo_set_object(CanFrame *f, uint16_t index, uint8_t sub)
{
    f->data[1] = (uint8_t)index;
    f->data[2] = (uint8_t)(index >> 8);
    f->data[3] = sub;
}

void protocore_canopen_build_sdo_read(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_sdo_read_args.out;
    uint8_t node_id = CanopenV.build_sdo_read_args.node_id;
    uint16_t index = CanopenV.build_sdo_read_args.index;
    uint8_t sub = CanopenV.build_sdo_read_args.sub;

    if (!out || !valid_node(node_id))
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, CANOPEN_COB_SDO_RX + node_id, 8);
    out->data[0] = (uint8_t)(CANOPEN_SDO_CCS_UPLOAD << 5); // upload initiate request (0x40)
    sdo_set_object(out, index, sub);
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_build_sdo_write(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_sdo_write_args.out;
    uint8_t node_id = CanopenV.build_sdo_write_args.node_id;
    uint16_t index = CanopenV.build_sdo_write_args.index;
    uint8_t sub = CanopenV.build_sdo_write_args.sub;
    const uint8_t *data = CanopenV.build_sdo_write_args.data;
    uint8_t len = CanopenV.build_sdo_write_args.len;

    if (!out || !valid_node(node_id) || len < 1 || len > 4 || !data)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, CANOPEN_COB_SDO_RX + node_id, 8);
    // download initiate, expedited (e=1), size indicated (s=1); n = unused octets in data[4..7].
    out->data[0] = (uint8_t)((CANOPEN_SDO_CCS_DOWNLOAD << 5) | (((4u - len) & 3u) << 2) | 0x03u);
    sdo_set_object(out, index, sub);
    mem.cpy(out->data + 4, data, len);
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_build_sdo_abort(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_sdo_abort_args.out;
    uint8_t node_id = CanopenV.build_sdo_abort_args.node_id;
    uint16_t index = CanopenV.build_sdo_abort_args.index;
    uint8_t sub = CanopenV.build_sdo_abort_args.sub;
    uint32_t abort_code = CanopenV.build_sdo_abort_args.abort_code;
    proto_bool to_server = CanopenV.build_sdo_abort_args.to_server;

    if (!out || !valid_node(node_id))
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, (to_server ? CANOPEN_COB_SDO_RX : CANOPEN_COB_SDO_TX) + node_id, 8);
    out->data[0] = (uint8_t)(CANOPEN_SDO_ABORT << 5); // 0x80
    sdo_set_object(out, index, sub);
    out->data[4] = (uint8_t)abort_code; // abort code, little-endian
    out->data[5] = (uint8_t)(abort_code >> 8);
    out->data[6] = (uint8_t)(abort_code >> 16);
    out->data[7] = (uint8_t)(abort_code >> 24);
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_parse(uint8_t *restrict work)
{
    (void)work;
    const CanFrame *f = CanopenV.parse_args.f;
    CanopenMsg *out = CanopenV.parse_args.out;

    if (!f || !out || f->extended)
    {
        CanopenV.ok = PROTO_FALSE; // CANopen default profile is 11-bit standard frames
        return;
    }
    uint32_t id = f->id & PROTOCORE_CAN_STD_ID_MASK;
    uint32_t func = id & CANOPEN_FUNC_MASK;
    uint8_t node = (uint8_t)(id & CANOPEN_NODE_MASK);
    out->type = CANOPEN_T_UNKNOWN;
    out->node_id = node;
    out->pdo_num = 0;

    if (id == CANOPEN_COB_NMT)
    {
        out->type = CANOPEN_T_NMT;
        out->node_id = 0;
        CanopenV.ok = PROTO_TRUE;
        return;
    }
    if (id == CANOPEN_COB_SYNC) // function 0x080 with node 0
    {
        out->type = CANOPEN_T_SYNC;
        out->node_id = 0;
        CanopenV.ok = PROTO_TRUE;
        return;
    }
    if (id == CANOPEN_COB_TIME)
    {
        out->type = CANOPEN_T_TIME;
        out->node_id = 0;
        CanopenV.ok = PROTO_TRUE;
        return;
    }
    if (node == 0)
    {
        CanopenV.ok = PROTO_TRUE; // a function base with node 0 we don't classify further
        return;
    }

    switch (func)
    {
    case CANOPEN_COB_EMCY:
        out->type = CANOPEN_T_EMCY;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_TPDO1:
        out->type = CANOPEN_T_TPDO;
        out->pdo_num = 1;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_RPDO1:
        out->type = CANOPEN_T_RPDO;
        out->pdo_num = 1;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_TPDO2:
        out->type = CANOPEN_T_TPDO;
        out->pdo_num = 2;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_RPDO2:
        out->type = CANOPEN_T_RPDO;
        out->pdo_num = 2;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_TPDO3:
        out->type = CANOPEN_T_TPDO;
        out->pdo_num = 3;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_RPDO3:
        out->type = CANOPEN_T_RPDO;
        out->pdo_num = 3;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_TPDO4:
        out->type = CANOPEN_T_TPDO;
        out->pdo_num = 4;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_RPDO4:
        out->type = CANOPEN_T_RPDO;
        out->pdo_num = 4;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_SDO_TX:
        out->type = CANOPEN_T_SDO_TX;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_SDO_RX:
        out->type = CANOPEN_T_SDO_RX;
        CanopenV.ok = PROTO_TRUE;
        return;
    case CANOPEN_COB_HEARTBEAT:
        out->type = CANOPEN_T_HEARTBEAT;
        CanopenV.ok = PROTO_TRUE;
        return;
    default:
        CanopenV.ok = PROTO_TRUE; // unknown function code: type stays CANOPEN_T_UNKNOWN
        return;
    }
}

void protocore_canopen_parse_emcy(uint8_t *restrict work)
{
    (void)work;
    const CanFrame *f = CanopenV.parse_emcy_args.f;
    uint8_t *node_id = CanopenV.parse_emcy_args.node_id;
    uint16_t *error_code = CanopenV.parse_emcy_args.error_code;
    uint8_t *error_reg = CanopenV.parse_emcy_args.error_reg;
    uint8_t *msef = CanopenV.parse_emcy_args.msef;

    if (!f || f->extended || f->dlc < 8)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    uint32_t id = f->id & PROTOCORE_CAN_STD_ID_MASK;
    uint8_t node = (uint8_t)(id & CANOPEN_NODE_MASK);
    if ((id & CANOPEN_FUNC_MASK) != CANOPEN_COB_EMCY || node == 0)
    {
        CanopenV.ok = PROTO_FALSE; // 0x080 with node 0 is SYNC, not EMCY
        return;
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
        mem.cpy(msef, f->data + 3, 5);
    }
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_parse_heartbeat(uint8_t *restrict work)
{
    (void)work;
    const CanFrame *f = CanopenV.parse_heartbeat_args.f;
    uint8_t *node_id = CanopenV.parse_heartbeat_args.node_id;
    uint8_t *state = CanopenV.parse_heartbeat_args.state;

    if (!f || f->extended || f->dlc < 1)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    uint32_t id = f->id & PROTOCORE_CAN_STD_ID_MASK;
    uint8_t node = (uint8_t)(id & CANOPEN_NODE_MASK);
    if ((id & CANOPEN_FUNC_MASK) != CANOPEN_COB_HEARTBEAT || node == 0)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    if (node_id)
    {
        *node_id = node;
    }
    if (state)
    {
        *state = (uint8_t)(f->data[0] & 0x7Fu); // bit 7 is the boot toggle in some stacks
    }
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_parse_time(uint8_t *restrict work)
{
    (void)work;
    const CanFrame *f = CanopenV.parse_time_args.f;
    CanopenTime *out = CanopenV.parse_time_args.out;

    if (!f || !out || f->extended || f->dlc < CANOPEN_TIME_LEN)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    if ((f->id & PROTOCORE_CAN_STD_ID_MASK) != CANOPEN_COB_TIME)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    uint32_t ms = (uint32_t)f->data[0] | ((uint32_t)f->data[1] << 8) | ((uint32_t)f->data[2] << 16) |
                  ((uint32_t)f->data[3] << 24);
    out->ms_since_midnight = ms & CANOPEN_TIME_MS_MASK; // discard the reserved top 4 bits
    out->days_since_1984 = (uint16_t)(f->data[4] | (f->data[5] << 8));
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_parse_sdo_response(uint8_t *restrict work)
{
    (void)work;
    const CanFrame *f = CanopenV.parse_sdo_response_args.f;
    CanopenSdoResponse *out = CanopenV.parse_sdo_response_args.out;

    if (!f || !out || f->extended || f->dlc < 8)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    uint32_t id = f->id & PROTOCORE_CAN_STD_ID_MASK;
    if ((id & CANOPEN_FUNC_MASK) != CANOPEN_COB_SDO_TX || (id & CANOPEN_NODE_MASK) == 0)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }

    uint8_t cmd = f->data[0];
    uint8_t scs = (uint8_t)(cmd >> 5);
    out->index = (uint16_t)(f->data[1] | (f->data[2] << 8));
    out->sub = f->data[3];
    out->is_abort = PROTO_FALSE;
    out->abort_code = 0;
    out->is_upload = PROTO_FALSE;
    out->expedited = PROTO_FALSE;
    out->len = 0;
    mem.set(out->data, 0, sizeof(out->data));

    if (scs == CANOPEN_SDO_ABORT)
    {
        out->is_abort = PROTO_TRUE;
        out->abort_code = (uint32_t)f->data[4] | ((uint32_t)f->data[5] << 8) | ((uint32_t)f->data[6] << 16) |
                          ((uint32_t)f->data[7] << 24);
        CanopenV.ok = PROTO_TRUE;
        return;
    }
    if (scs == CANOPEN_SDO_SCS_UPLOAD) // upload initiate response
    {
        out->is_upload = PROTO_TRUE;
        proto_bool e = (cmd & 0x02u) != 0; // expedited
        proto_bool s = (cmd & 0x01u) != 0; // size indicated
        if (e)
        {
            out->expedited = PROTO_TRUE;
            out->len = s ? (uint8_t)(4u - ((cmd >> 2) & 0x03u)) : 4u;
            mem.cpy(out->data, f->data + 4, out->len);
        }
        CanopenV.ok = PROTO_TRUE; // a non-expedited (segmented) response is reported with len 0
        return;
    }
    if (scs == CANOPEN_SDO_SCS_DOWNLOAD) // download initiate response (write acknowledged)
    {
        CanopenV.ok = PROTO_TRUE;
        return;
    }
    CanopenV.ok = PROTO_FALSE; // not a recognized server command specifier
}

// --- segmented SDO (CiA 301 §7.2.4.3) ---

void protocore_canopen_build_sdo_download_init(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_sdo_download_init_args.out;
    uint8_t node_id = CanopenV.build_sdo_download_init_args.node_id;
    uint16_t index = CanopenV.build_sdo_download_init_args.index;
    uint8_t sub = CanopenV.build_sdo_download_init_args.sub;
    uint32_t total_size = CanopenV.build_sdo_download_init_args.total_size;

    if (!out || !valid_node(node_id))
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, CANOPEN_COB_SDO_RX + node_id, 8);
    // download initiate, segmented (e=0), size indicated (s=1) -> command 0x21; size in data[4..7] LE.
    out->data[0] = (uint8_t)((CANOPEN_SDO_CCS_DOWNLOAD << 5) | 0x01u);
    sdo_set_object(out, index, sub);
    out->data[4] = (uint8_t)total_size;
    out->data[5] = (uint8_t)(total_size >> 8);
    out->data[6] = (uint8_t)(total_size >> 16);
    out->data[7] = (uint8_t)(total_size >> 24);
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_build_sdo_download_segment(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_sdo_download_segment_args.out;
    uint8_t node_id = CanopenV.build_sdo_download_segment_args.node_id;
    proto_bool toggle = CanopenV.build_sdo_download_segment_args.toggle;
    const uint8_t *data = CanopenV.build_sdo_download_segment_args.data;
    uint8_t len = CanopenV.build_sdo_download_segment_args.len;
    proto_bool last = CanopenV.build_sdo_download_segment_args.last;

    if (!out || !valid_node(node_id) || !data || len < 1 || len > CANOPEN_SDO_SEG_DATA)
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, CANOPEN_COB_SDO_RX + node_id, 8);
    // segment: ccs=0, t=toggle (bit 4), n=unused octets (bits 1..3), c=last (bit 0).
    uint8_t n = (uint8_t)(CANOPEN_SDO_SEG_DATA - len);
    out->data[0] = (uint8_t)((toggle ? 0x10u : 0u) | ((n & 0x07u) << 1) | (last ? 0x01u : 0u));
    mem.cpy(out->data + 1, data, len);
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_build_sdo_upload_segment_req(uint8_t *restrict work)
{
    (void)work;
    CanFrame *out = CanopenV.build_sdo_upload_segment_req_args.out;
    uint8_t node_id = CanopenV.build_sdo_upload_segment_req_args.node_id;
    proto_bool toggle = CanopenV.build_sdo_upload_segment_req_args.toggle;

    if (!out || !valid_node(node_id))
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    std_frame(out, CANOPEN_COB_SDO_RX + node_id, 8);
    // upload segment request: ccs=3, t=toggle (bit 4).
    out->data[0] = (uint8_t)((3u << 5) | (toggle ? 0x10u : 0u));
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_parse_sdo_segment(uint8_t *restrict work)
{
    (void)work;
    const CanFrame *f = CanopenV.parse_sdo_segment_args.f;
    proto_bool *toggle = CanopenV.parse_sdo_segment_args.toggle;
    uint8_t *data = CanopenV.parse_sdo_segment_args.data;
    uint8_t *len = CanopenV.parse_sdo_segment_args.len;
    proto_bool *last = CanopenV.parse_sdo_segment_args.last;

    if (!f || f->dlc < 8 || (f->data[0] & 0xE0u) != 0u) // segment form: command specifier (high 3 bits) is 0
    {
        CanopenV.ok = PROTO_FALSE;
        return;
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
        mem.cpy(data, f->data + 1, seg_len);
    }
    CanopenV.ok = PROTO_TRUE;
}

void protocore_canopen_sdo_reasm_init(uint8_t *restrict work)
{
    (void)work;
    CanopenSdoReasm *r = CanopenV.sdo_reasm_init_args.r;
    uint8_t *buf = CanopenV.sdo_reasm_init_args.buf;
    size_t cap = CanopenV.sdo_reasm_init_args.cap;

    if (!r)
    {
        return;
    }
    r->buf = buf;
    r->cap = cap;
    r->len = 0;
    r->expect_toggle = PROTO_FALSE; // the first segment carries toggle 0
    r->done = PROTO_FALSE;
}

void protocore_canopen_sdo_reasm_feed(uint8_t *restrict work)
{
    (void)work;
    CanopenSdoReasm *r = CanopenV.sdo_reasm_feed_args.r;
    const uint8_t *data = CanopenV.sdo_reasm_feed_args.data;
    uint8_t len = CanopenV.sdo_reasm_feed_args.len;
    proto_bool toggle = CanopenV.sdo_reasm_feed_args.toggle;
    proto_bool last = CanopenV.sdo_reasm_feed_args.last;

    if (!r || !r->buf || r->done || (len && !data))
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    if (toggle != r->expect_toggle) // toggles must alternate 0,1,0,1
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    if (len > r->cap - r->len) // would overflow the buffer
    {
        CanopenV.ok = PROTO_FALSE;
        return;
    }
    if (len)
    {
        mem.cpy(r->buf + r->len, data, len);
    }
    r->len += len;
    r->expect_toggle = !r->expect_toggle;
    if (last)
    {
        r->done = PROTO_TRUE;
    }
    CanopenV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
CanopenVars CanopenV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CANOPEN
