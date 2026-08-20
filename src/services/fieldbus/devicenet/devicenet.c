// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file devicenet.c
 * @brief DeviceNet link-adaptation codec (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DEVICENET

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/devicenet/devicenet.h"
#include "shared/can/can.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_devicenet_encode_id(uint8_t *restrict work);
void protocore_devicenet_frag_octet(uint8_t *restrict work);
void protocore_devicenet_frag_reset(uint8_t *restrict work);
void protocore_devicenet_msg_header(uint8_t *restrict work);

void protocore_devicenet_encode_id(uint8_t *restrict work)
{
    (void)work;
    uint32_t *id = DevicenetV.encode_id_args.id;
    DeviceNetGroup group = DevicenetV.encode_id_args.group;
    uint8_t msg_id = DevicenetV.encode_id_args.msg_id;
    uint8_t mac_id = DevicenetV.encode_id_args.mac_id;

    if (!id || mac_id > DEVICENET_MAC_MASK)
    {
        DevicenetV.ok = PROTO_FALSE;
        return;
    }
    switch (group)
    {
    case DEVICENET_GROUP_1:
        if (msg_id > 0x0Fu)
        {
            DevicenetV.ok = PROTO_FALSE;
            return;
        }
        *id = DEVICENET_G1_BASE | ((uint32_t)msg_id << 6) | mac_id;
        DevicenetV.ok = PROTO_TRUE;
        return;
    case DEVICENET_GROUP_2:
        if (msg_id > 0x07u)
        {
            DevicenetV.ok = PROTO_FALSE;
            return;
        }
        *id = DEVICENET_G2_BASE | ((uint32_t)mac_id << 3) | msg_id;
        DevicenetV.ok = PROTO_TRUE;
        return;
    case DEVICENET_GROUP_3:
        if (msg_id > 0x06u) // msg id 7 puts the identifier at 0x7C0+, which is Group 4's range
        {
            DevicenetV.ok = PROTO_FALSE;
            return;
        }
        *id = DEVICENET_G3_BASE | ((uint32_t)msg_id << 6) | mac_id;
        DevicenetV.ok = PROTO_TRUE;
        return;
    case DEVICENET_GROUP_4:
        if (msg_id > 0x2Fu) // Group 4 has no MAC id; message ids 0x00..0x2F
        {
            DevicenetV.ok = PROTO_FALSE;
            return;
        }
        *id = DEVICENET_G4_BASE | msg_id;
        DevicenetV.ok = PROTO_TRUE;
        return;
    default:
        DevicenetV.ok = PROTO_FALSE;
        return;
    }
}

void protocore_devicenet_decode_id(uint8_t *restrict work)
{
    (void)work;
    uint32_t can_id = DevicenetV.decode_id_args.can_id;
    DeviceNetId *out = DevicenetV.decode_id_args.out;

    if (!out)
    {
        DevicenetV.ok = PROTO_FALSE;
        return;
    }
    uint32_t id = can_id & PROTOCORE_CAN_STD_ID_MASK;
    if (id < DEVICENET_G2_BASE) // Group 1: 0 MsgID(4) MAC(6)
    {
        out->group = DEVICENET_GROUP_1;
        out->msg_id = (uint8_t)((id >> 6) & 0x0Fu);
        out->mac_id = (uint8_t)(id & DEVICENET_MAC_MASK);
        DevicenetV.ok = PROTO_TRUE;
        return;
    }
    if (id < DEVICENET_G3_BASE) // Group 2: 10 MAC(6) MsgID(3)
    {
        out->group = DEVICENET_GROUP_2;
        out->mac_id = (uint8_t)((id >> 3) & DEVICENET_MAC_MASK);
        out->msg_id = (uint8_t)(id & 0x07u);
        DevicenetV.ok = PROTO_TRUE;
        return;
    }
    if (id < DEVICENET_G4_BASE) // Group 3: 11 MsgID(3) MAC(6)
    {
        out->group = DEVICENET_GROUP_3;
        out->msg_id = (uint8_t)((id >> 6) & 0x07u);
        out->mac_id = (uint8_t)(id & DEVICENET_MAC_MASK);
        DevicenetV.ok = PROTO_TRUE;
        return;
    }
    if (id <= 0x7EFu) // Group 4: 11111 MsgID(6)
    {
        out->group = DEVICENET_GROUP_4;
        out->msg_id = (uint8_t)(id & 0x3Fu);
        out->mac_id = 0;
        DevicenetV.ok = PROTO_TRUE;
        return;
    }
    DevicenetV.ok = PROTO_FALSE; // 0x7F0..0x7FF are invalid identifiers
}

void protocore_devicenet_msg_header(uint8_t *restrict work)
{
    (void)work;
    proto_bool frag = DevicenetV.msg_header_args.frag;
    proto_bool xid = DevicenetV.msg_header_args.xid;
    uint8_t mac_id = DevicenetV.msg_header_args.mac_id;

    DevicenetV.value =
        (uint8_t)((frag ? DEVICENET_HDR_FRAG : 0u) | (xid ? DEVICENET_HDR_XID : 0u) | (mac_id & DEVICENET_MAC_MASK));
}

void protocore_devicenet_frag_octet(uint8_t *restrict work)
{
    (void)work;
    uint8_t type = DevicenetV.frag_octet_args.type;
    uint8_t count = DevicenetV.frag_octet_args.count;

    DevicenetV.value = (uint8_t)((type & DEVICENET_FRAG_TYPE_MASK) | (count & DEVICENET_FRAG_COUNT_MASK));
}

void protocore_devicenet_build_explicit(uint8_t *restrict work)
{
    CanFrame *out = DevicenetV.build_explicit_args.out;
    DeviceNetGroup group = DevicenetV.build_explicit_args.group;
    uint8_t msg_id = DevicenetV.build_explicit_args.msg_id;
    uint8_t mac_id = DevicenetV.build_explicit_args.mac_id;
    const uint8_t *body = DevicenetV.build_explicit_args.body;
    uint8_t body_len = DevicenetV.build_explicit_args.body_len;

    if (!out || body_len > 7 || (body_len && !body)) // 1 header octet + up to 7 body octets
    {
        DevicenetV.ok = PROTO_FALSE;
        return;
    }
    uint32_t id;
    DevicenetV.encode_id_args.id = &id;
    DevicenetV.encode_id_args.group = group;
    DevicenetV.encode_id_args.msg_id = msg_id;
    DevicenetV.encode_id_args.mac_id = mac_id;
    protocore_devicenet_encode_id(work);
    if (!DevicenetV.ok)
    {
        DevicenetV.ok = PROTO_FALSE;
        return;
    }
    out->id = id;
    out->extended = PROTO_FALSE;
    out->rtr = PROTO_FALSE;
    out->dlc = (uint8_t)(1 + body_len);
    mem.set(out->data, 0, sizeof(out->data));
    DevicenetV.msg_header_args.frag = PROTO_FALSE;
    DevicenetV.msg_header_args.xid = PROTO_FALSE;
    DevicenetV.msg_header_args.mac_id = mac_id;
    protocore_devicenet_msg_header(work);
    out->data[0] = DevicenetV.value; // not fragmented
    if (body_len)
    {
        mem.cpy(out->data + 1, body, body_len);
    }
    DevicenetV.ok = PROTO_TRUE;
}

void protocore_devicenet_build_fragment(uint8_t *restrict work)
{
    CanFrame *out = DevicenetV.build_fragment_args.out;
    DeviceNetGroup group = DevicenetV.build_fragment_args.group;
    uint8_t msg_id = DevicenetV.build_fragment_args.msg_id;
    uint8_t mac_id = DevicenetV.build_fragment_args.mac_id;
    proto_bool xid = DevicenetV.build_fragment_args.xid;
    uint8_t frag_type = DevicenetV.build_fragment_args.frag_type;
    uint8_t frag_count = DevicenetV.build_fragment_args.frag_count;
    const uint8_t *data = DevicenetV.build_fragment_args.data;
    uint8_t data_len = DevicenetV.build_fragment_args.data_len;

    // 1 header octet + 1 fragmentation octet + up to 6 data octets fill the 8-octet CAN frame.
    if (!out || data_len > 6 || (data_len && !data) || (frag_type & (uint8_t)~DEVICENET_FRAG_TYPE_MASK) ||
        (frag_count & (uint8_t)~DEVICENET_FRAG_COUNT_MASK))
    {
        DevicenetV.ok = PROTO_FALSE;
        return;
    }
    uint32_t id;
    DevicenetV.encode_id_args.id = &id;
    DevicenetV.encode_id_args.group = group;
    DevicenetV.encode_id_args.msg_id = msg_id;
    DevicenetV.encode_id_args.mac_id = mac_id;
    protocore_devicenet_encode_id(work);
    if (!DevicenetV.ok)
    {
        DevicenetV.ok = PROTO_FALSE;
        return;
    }
    out->id = id;
    out->extended = PROTO_FALSE;
    out->rtr = PROTO_FALSE;
    out->dlc = (uint8_t)(2 + data_len);
    mem.set(out->data, 0, sizeof(out->data));
    DevicenetV.msg_header_args.frag = PROTO_TRUE;
    DevicenetV.msg_header_args.xid = xid;
    DevicenetV.msg_header_args.mac_id = mac_id;
    protocore_devicenet_msg_header(work);
    out->data[0] = DevicenetV.value; // FRAG set
    DevicenetV.frag_octet_args.type = frag_type;
    DevicenetV.frag_octet_args.count = frag_count;
    protocore_devicenet_frag_octet(work);
    out->data[1] = DevicenetV.value;
    if (data_len)
    {
        mem.cpy(out->data + 2, data, data_len);
    }
    DevicenetV.ok = PROTO_TRUE;
}

void protocore_devicenet_frag_reset(uint8_t *restrict work)
{
    (void)work;
    DeviceNetFragRx *rx = DevicenetV.frag_reset_args.rx;

    if (rx)
    {
        mem.set(rx, 0, sizeof(*rx));
    }
}

// The two "cannot overflow" exclusions below (the non-fragmented and FIRST appends) hold only because a
// single append into a freshly reset buffer fits PROTOCORE_DEVICENET_MSG_MAX. body_len is a uint8_t, so the
// largest such append is body_len - 1 == 254 octets; anything smaller than that turns those excluded
// error returns into live code. PROTOCORE_DEVICENET_MSG_MAX is a plain #ifndef in protocore_config.h and can be
// overridden, so pin the invariant here rather than trusting the default.
static_assert(PROTOCORE_DEVICENET_MSG_MAX >= 254,
              "PROTOCORE_DEVICENET_MSG_MAX must be >= 254 (the largest single-frame append, body_len - 1 with "
              "body_len at its uint8_t maximum) or frag_append can overflow on the first frame");

// Append @p n octets to the reassembly buffer; false if it would overflow.
static proto_bool frag_append(DeviceNetFragRx *rx, const uint8_t *p, uint8_t n)
{
    if ((uint32_t)rx->len + n > PROTOCORE_DEVICENET_MSG_MAX)
    {
        return PROTO_FALSE;
    }
    mem.cpy(rx->buf + rx->len, p, n);
    rx->len = (uint16_t)(rx->len + n);
    return PROTO_TRUE;
}

void protocore_devicenet_frag_feed(uint8_t *restrict work)
{
    DeviceNetFragRx *rx = DevicenetV.frag_feed_args.rx;
    const uint8_t *body = DevicenetV.frag_feed_args.body;
    uint8_t body_len = DevicenetV.frag_feed_args.body_len;

    if (!rx || !body || body_len < 1)
    {
        DevicenetV.frag = DEVICENET_FRAG_IGNORED;
        return;
    }

    if (!(body[0] & DEVICENET_HDR_FRAG)) // a complete, non-fragmented message in one frame
    {
        DevicenetV.frag_reset_args.rx = rx;
        protocore_devicenet_frag_reset(work);
        if (body_len > 1 && !frag_append(rx, body + 1, (uint8_t)(body_len - 1)))
        {
            DevicenetV.frag = DEVICENET_FRAG_ERR;
            return;
        }
        DevicenetV.frag = DEVICENET_FRAG_COMPLETE;
        return;
    }
    if (body_len < 2)
    {
        DevicenetV.frag = DEVICENET_FRAG_ERR; // FRAG set but no fragmentation octet
        return;
    }
    uint8_t type = body[1] & DEVICENET_FRAG_TYPE_MASK;
    uint8_t count = body[1] & DEVICENET_FRAG_COUNT_MASK;
    const uint8_t *data = body + 2;
    uint8_t data_len = (uint8_t)(body_len - 2);

    switch (type)
    {
    case DEVICENET_FRAG_FIRST:
        DevicenetV.frag_reset_args.rx = rx;
        protocore_devicenet_frag_reset(work);
        rx->active = PROTO_TRUE;
        rx->next_count = (uint8_t)((count + 1u) & DEVICENET_FRAG_COUNT_MASK);
        if (data_len && !frag_append(rx, data, data_len))
        {
            DevicenetV.frag = DEVICENET_FRAG_ERR;
            return;
        }
        DevicenetV.frag = DEVICENET_FRAG_STARTED;
        return;
    case DEVICENET_FRAG_MIDDLE:
        if (!rx->active || count != rx->next_count)
        {
            DevicenetV.frag_reset_args.rx = rx;
            protocore_devicenet_frag_reset(work);
            DevicenetV.frag = DEVICENET_FRAG_ERR;
            return;
        }
        if (data_len && !frag_append(rx, data, data_len))
        {
            DevicenetV.frag_reset_args.rx = rx;
            protocore_devicenet_frag_reset(work);
            DevicenetV.frag = DEVICENET_FRAG_ERR;
            return;
        }
        rx->next_count = (uint8_t)((count + 1u) & DEVICENET_FRAG_COUNT_MASK);
        DevicenetV.frag = DEVICENET_FRAG_PROGRESS;
        return;
    case DEVICENET_FRAG_LAST:
        if (!rx->active || count != rx->next_count)
        {
            DevicenetV.frag_reset_args.rx = rx;
            protocore_devicenet_frag_reset(work);
            DevicenetV.frag = DEVICENET_FRAG_ERR;
            return;
        }
        if (data_len && !frag_append(rx, data, data_len))
        {
            DevicenetV.frag_reset_args.rx = rx;
            protocore_devicenet_frag_reset(work);
            DevicenetV.frag = DEVICENET_FRAG_ERR;
            return;
        }
        rx->active = PROTO_FALSE;
        DevicenetV.frag = DEVICENET_FRAG_COMPLETE;
        return;
    default: // DEVICENET_FRAG_ACK is flow control, not data
        DevicenetV.frag = DEVICENET_FRAG_IGNORED;
        return;
    }
}

/** @brief The operands and the outcome. */
DevicenetVars DevicenetV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DEVICENET
