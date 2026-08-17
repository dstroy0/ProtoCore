// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file devicenet.c
 * @brief DeviceNet link-adaptation codec (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DEVICENET

#include "mmgr/protomem.h"
#include "services/fieldbus/devicenet/devicenet.h"
#include "shared/can/can.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void devicenet_encode_id(uint8_t *restrict work);
static void devicenet_frag_octet(uint8_t *restrict work);
static void devicenet_frag_reset(uint8_t *restrict work);
static void devicenet_msg_header(uint8_t *restrict work);

static void devicenet_encode_id(uint8_t *restrict work)
{
    (void)work;
    uint32_t *id = Devicenet.encode_id_args.id;
    DeviceNetGroup group = Devicenet.encode_id_args.group;
    uint8_t msg_id = Devicenet.encode_id_args.msg_id;
    uint8_t mac_id = Devicenet.encode_id_args.mac_id;

    if (!id || mac_id > DEVICENET_MAC_MASK)
    {
        Devicenet.ok = PROTO_FALSE;
        return;
    }
    switch (group)
    {
    case DEVICENET_GROUP_1:
        if (msg_id > 0x0Fu)
        {
            Devicenet.ok = PROTO_FALSE;
            return;
        }
        *id = DEVICENET_G1_BASE | ((uint32_t)msg_id << 6) | mac_id;
        Devicenet.ok = PROTO_TRUE;
        return;
    case DEVICENET_GROUP_2:
        if (msg_id > 0x07u)
        {
            Devicenet.ok = PROTO_FALSE;
            return;
        }
        *id = DEVICENET_G2_BASE | ((uint32_t)mac_id << 3) | msg_id;
        Devicenet.ok = PROTO_TRUE;
        return;
    case DEVICENET_GROUP_3:
        if (msg_id > 0x06u) // msg id 7 puts the identifier at 0x7C0+, which is Group 4's range
        {
            Devicenet.ok = PROTO_FALSE;
            return;
        }
        *id = DEVICENET_G3_BASE | ((uint32_t)msg_id << 6) | mac_id;
        Devicenet.ok = PROTO_TRUE;
        return;
    case DEVICENET_GROUP_4:
        if (msg_id > 0x2Fu) // Group 4 has no MAC id; message ids 0x00..0x2F
        {
            Devicenet.ok = PROTO_FALSE;
            return;
        }
        *id = DEVICENET_G4_BASE | msg_id;
        Devicenet.ok = PROTO_TRUE;
        return;
    default:
        Devicenet.ok = PROTO_FALSE;
        return;
    }
}

static void devicenet_decode_id(uint8_t *restrict work)
{
    (void)work;
    uint32_t can_id = Devicenet.decode_id_args.can_id;
    DeviceNetId *out = Devicenet.decode_id_args.out;

    if (!out)
    {
        Devicenet.ok = PROTO_FALSE;
        return;
    }
    uint32_t id = can_id & PROTOCORE_CAN_STD_ID_MASK;
    if (id < DEVICENET_G2_BASE) // Group 1: 0 MsgID(4) MAC(6)
    {
        out->group = DEVICENET_GROUP_1;
        out->msg_id = (uint8_t)((id >> 6) & 0x0Fu);
        out->mac_id = (uint8_t)(id & DEVICENET_MAC_MASK);
        Devicenet.ok = PROTO_TRUE;
        return;
    }
    if (id < DEVICENET_G3_BASE) // Group 2: 10 MAC(6) MsgID(3)
    {
        out->group = DEVICENET_GROUP_2;
        out->mac_id = (uint8_t)((id >> 3) & DEVICENET_MAC_MASK);
        out->msg_id = (uint8_t)(id & 0x07u);
        Devicenet.ok = PROTO_TRUE;
        return;
    }
    if (id < DEVICENET_G4_BASE) // Group 3: 11 MsgID(3) MAC(6)
    {
        out->group = DEVICENET_GROUP_3;
        out->msg_id = (uint8_t)((id >> 6) & 0x07u);
        out->mac_id = (uint8_t)(id & DEVICENET_MAC_MASK);
        Devicenet.ok = PROTO_TRUE;
        return;
    }
    if (id <= 0x7EFu) // Group 4: 11111 MsgID(6)
    {
        out->group = DEVICENET_GROUP_4;
        out->msg_id = (uint8_t)(id & 0x3Fu);
        out->mac_id = 0;
        Devicenet.ok = PROTO_TRUE;
        return;
    }
    Devicenet.ok = PROTO_FALSE; // 0x7F0..0x7FF are invalid identifiers
}

static void devicenet_msg_header(uint8_t *restrict work)
{
    (void)work;
    proto_bool frag = Devicenet.msg_header_args.frag;
    proto_bool xid = Devicenet.msg_header_args.xid;
    uint8_t mac_id = Devicenet.msg_header_args.mac_id;

    Devicenet.value =
        (uint8_t)((frag ? DEVICENET_HDR_FRAG : 0u) | (xid ? DEVICENET_HDR_XID : 0u) | (mac_id & DEVICENET_MAC_MASK));
}

static void devicenet_frag_octet(uint8_t *restrict work)
{
    (void)work;
    uint8_t type = Devicenet.frag_octet_args.type;
    uint8_t count = Devicenet.frag_octet_args.count;

    Devicenet.value = (uint8_t)((type & DEVICENET_FRAG_TYPE_MASK) | (count & DEVICENET_FRAG_COUNT_MASK));
}

static void devicenet_build_explicit(uint8_t *restrict work)
{
    CanFrame *out = Devicenet.build_explicit_args.out;
    DeviceNetGroup group = Devicenet.build_explicit_args.group;
    uint8_t msg_id = Devicenet.build_explicit_args.msg_id;
    uint8_t mac_id = Devicenet.build_explicit_args.mac_id;
    const uint8_t *body = Devicenet.build_explicit_args.body;
    uint8_t body_len = Devicenet.build_explicit_args.body_len;

    if (!out || body_len > 7 || (body_len && !body)) // 1 header octet + up to 7 body octets
    {
        Devicenet.ok = PROTO_FALSE;
        return;
    }
    uint32_t id;
    Devicenet.encode_id_args.id = &id;
    Devicenet.encode_id_args.group = group;
    Devicenet.encode_id_args.msg_id = msg_id;
    Devicenet.encode_id_args.mac_id = mac_id;
    devicenet_encode_id(work);
    if (!Devicenet.ok)
    {
        Devicenet.ok = PROTO_FALSE;
        return;
    }
    out->id = id;
    out->extended = PROTO_FALSE;
    out->rtr = PROTO_FALSE;
    out->dlc = (uint8_t)(1 + body_len);
    mem.set(out->data, 0, sizeof(out->data));
    Devicenet.msg_header_args.frag = PROTO_FALSE;
    Devicenet.msg_header_args.xid = PROTO_FALSE;
    Devicenet.msg_header_args.mac_id = mac_id;
    devicenet_msg_header(work);
    out->data[0] = Devicenet.value; // not fragmented
    if (body_len)
    {
        mem.cpy(out->data + 1, body, body_len);
    }
    Devicenet.ok = PROTO_TRUE;
}

static void devicenet_build_fragment(uint8_t *restrict work)
{
    CanFrame *out = Devicenet.build_fragment_args.out;
    DeviceNetGroup group = Devicenet.build_fragment_args.group;
    uint8_t msg_id = Devicenet.build_fragment_args.msg_id;
    uint8_t mac_id = Devicenet.build_fragment_args.mac_id;
    proto_bool xid = Devicenet.build_fragment_args.xid;
    uint8_t frag_type = Devicenet.build_fragment_args.frag_type;
    uint8_t frag_count = Devicenet.build_fragment_args.frag_count;
    const uint8_t *data = Devicenet.build_fragment_args.data;
    uint8_t data_len = Devicenet.build_fragment_args.data_len;

    // 1 header octet + 1 fragmentation octet + up to 6 data octets fill the 8-octet CAN frame.
    if (!out || data_len > 6 || (data_len && !data) || (frag_type & (uint8_t)~DEVICENET_FRAG_TYPE_MASK) ||
        (frag_count & (uint8_t)~DEVICENET_FRAG_COUNT_MASK))
    {
        Devicenet.ok = PROTO_FALSE;
        return;
    }
    uint32_t id;
    Devicenet.encode_id_args.id = &id;
    Devicenet.encode_id_args.group = group;
    Devicenet.encode_id_args.msg_id = msg_id;
    Devicenet.encode_id_args.mac_id = mac_id;
    devicenet_encode_id(work);
    if (!Devicenet.ok)
    {
        Devicenet.ok = PROTO_FALSE;
        return;
    }
    out->id = id;
    out->extended = PROTO_FALSE;
    out->rtr = PROTO_FALSE;
    out->dlc = (uint8_t)(2 + data_len);
    mem.set(out->data, 0, sizeof(out->data));
    Devicenet.msg_header_args.frag = PROTO_TRUE;
    Devicenet.msg_header_args.xid = xid;
    Devicenet.msg_header_args.mac_id = mac_id;
    devicenet_msg_header(work);
    out->data[0] = Devicenet.value; // FRAG set
    Devicenet.frag_octet_args.type = frag_type;
    Devicenet.frag_octet_args.count = frag_count;
    devicenet_frag_octet(work);
    out->data[1] = Devicenet.value;
    if (data_len)
    {
        mem.cpy(out->data + 2, data, data_len);
    }
    Devicenet.ok = PROTO_TRUE;
}

static void devicenet_frag_reset(uint8_t *restrict work)
{
    (void)work;
    DeviceNetFragRx *rx = Devicenet.frag_reset_args.rx;

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

static void devicenet_frag_feed(uint8_t *restrict work)
{
    DeviceNetFragRx *rx = Devicenet.frag_feed_args.rx;
    const uint8_t *body = Devicenet.frag_feed_args.body;
    uint8_t body_len = Devicenet.frag_feed_args.body_len;

    if (!rx || !body || body_len < 1)
    {
        Devicenet.frag = DEVICENET_FRAG_IGNORED;
        return;
    }

    if (!(body[0] & DEVICENET_HDR_FRAG)) // a complete, non-fragmented message in one frame
    {
        Devicenet.frag_reset_args.rx = rx;
        devicenet_frag_reset(work);
        if (body_len > 1 && !frag_append(rx, body + 1, (uint8_t)(body_len - 1)))
        {
            Devicenet.frag = DEVICENET_FRAG_ERR;
            return;
        }
        Devicenet.frag = DEVICENET_FRAG_COMPLETE;
        return;
    }
    if (body_len < 2)
    {
        Devicenet.frag = DEVICENET_FRAG_ERR; // FRAG set but no fragmentation octet
        return;
    }
    uint8_t type = body[1] & DEVICENET_FRAG_TYPE_MASK;
    uint8_t count = body[1] & DEVICENET_FRAG_COUNT_MASK;
    const uint8_t *data = body + 2;
    uint8_t data_len = (uint8_t)(body_len - 2);

    switch (type)
    {
    case DEVICENET_FRAG_FIRST:
        Devicenet.frag_reset_args.rx = rx;
        devicenet_frag_reset(work);
        rx->active = PROTO_TRUE;
        rx->next_count = (uint8_t)((count + 1u) & DEVICENET_FRAG_COUNT_MASK);
        if (data_len && !frag_append(rx, data, data_len))
        {
            Devicenet.frag = DEVICENET_FRAG_ERR;
            return;
        }
        Devicenet.frag = DEVICENET_FRAG_STARTED;
        return;
    case DEVICENET_FRAG_MIDDLE:
        if (!rx->active || count != rx->next_count)
        {
            Devicenet.frag_reset_args.rx = rx;
            devicenet_frag_reset(work);
            Devicenet.frag = DEVICENET_FRAG_ERR;
            return;
        }
        if (data_len && !frag_append(rx, data, data_len))
        {
            Devicenet.frag_reset_args.rx = rx;
            devicenet_frag_reset(work);
            Devicenet.frag = DEVICENET_FRAG_ERR;
            return;
        }
        rx->next_count = (uint8_t)((count + 1u) & DEVICENET_FRAG_COUNT_MASK);
        Devicenet.frag = DEVICENET_FRAG_PROGRESS;
        return;
    case DEVICENET_FRAG_LAST:
        if (!rx->active || count != rx->next_count)
        {
            Devicenet.frag_reset_args.rx = rx;
            devicenet_frag_reset(work);
            Devicenet.frag = DEVICENET_FRAG_ERR;
            return;
        }
        if (data_len && !frag_append(rx, data, data_len))
        {
            Devicenet.frag_reset_args.rx = rx;
            devicenet_frag_reset(work);
            Devicenet.frag = DEVICENET_FRAG_ERR;
            return;
        }
        rx->active = PROTO_FALSE;
        Devicenet.frag = DEVICENET_FRAG_COMPLETE;
        return;
    default: // DEVICENET_FRAG_ACK is flow control, not data
        Devicenet.frag = DEVICENET_FRAG_IGNORED;
        return;
    }
}

DevicenetNs Devicenet = {.encode_id = devicenet_encode_id,
                         .decode_id = devicenet_decode_id,
                         .msg_header = devicenet_msg_header,
                         .frag_octet = devicenet_frag_octet,
                         .build_explicit = devicenet_build_explicit,
                         .build_fragment = devicenet_build_fragment,
                         .frag_reset = devicenet_frag_reset,
                         .frag_feed = devicenet_frag_feed};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DEVICENET
