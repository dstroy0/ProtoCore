// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bus_capture.c
 * @brief bus_capture implementation: the pure SocketCAN framer + the listen-only controller bind.
 *
 * The controller itself is the platform's: this reaches it through the protocore_platform_can_*
 * seam, so the framing and the drain loop run the same on a host with the seam mocked.
 */

#include "server/signaling/bus_capture.h"
#include "core_setup/board_profiles/protocore_platform.h"

#if PROTOCORE_ENABLE_BUS_CAPTURE

size_t can_to_socketcan(const CanFrame *f, uint8_t *out, size_t cap)
{
    if (!f || !out || cap < PROTOCORE_SOCKETCAN_FRAME_LEN)
    {
        return 0;
    }

    uint32_t id = f->id & (f->extended ? PROTOCORE_CAN_EXT_ID_MASK : PROTOCORE_CAN_STD_ID_MASK);
    if (f->extended)
    {
        id |= PROTOCORE_CAN_EFF_FLAG;
    }
    if (f->rtr)
    {
        id |= PROTOCORE_CAN_RTR_FLAG;
    }

    out[0] = (uint8_t)(id >> 24); // can_id, big-endian
    out[1] = (uint8_t)(id >> 16);
    out[2] = (uint8_t)(id >> 8);
    out[3] = (uint8_t)id;

    uint8_t dlc = f->dlc > PROTOCORE_CAN_MAX_DLC ? PROTOCORE_CAN_MAX_DLC : f->dlc;
    out[4] = dlc; // length
    out[5] = 0;   // __pad
    out[6] = 0;   // __res0
    out[7] = 0;   // len8_dlc / __res1
    for (int i = 0; i < PROTOCORE_CAN_MAX_DLC; i++)
    {
        out[8 + i] = (i < dlc && !f->rtr) ? f->data[i] : 0;
    }
    return PROTOCORE_SOCKETCAN_FRAME_LEN;
}

// --- Controller binding ------------------------------------------------------------------
#if PROTOCORE_HAS_VENDOR_CAN

// All bus-capture bind state, owned by one instance (internal linkage): the frame sink and
// the running flag, grouped so it is one named owner, unreachable from any other TU.
typedef struct
{
    bus_capture_sink_fn sink;
    proto_bool running;
} BusCaptureCtx;
static BusCaptureCtx s_bus;

proto_bool bus_capture_begin(int tx_pin, int rx_pin, uint32_t bitrate, bus_capture_sink_fn sink)
{
    if (!sink || !protocore_platform_can_open(tx_pin, rx_pin, bitrate))
    {
        return PROTO_FALSE;
    }
    s_bus.sink = sink;
    s_bus.running = PROTO_TRUE;
    return PROTO_TRUE;
}

void bus_capture_poll(void)
{
    if (!s_bus.running || !s_bus.sink)
    {
        return;
    }
    protocore_can_frame m;
    while (protocore_platform_can_recv(&m)) // drains what is queued without blocking
    {
        CanFrame f;
        f.id = m.id;
        f.extended = m.ext;
        f.rtr = m.rtr;
        f.dlc = m.len > PROTOCORE_CAN_MAX_DLC ? PROTOCORE_CAN_MAX_DLC : m.len;
        for (int i = 0; i < PROTOCORE_CAN_MAX_DLC; i++)
        {
            f.data[i] = (i < f.dlc) ? m.data[i] : 0;
        }
        s_bus.sink(&f);
    }
}

void bus_capture_end(void)
{
    if (!s_bus.running)
    {
        return;
    }
    protocore_platform_can_close();
    s_bus.running = PROTO_FALSE;
    s_bus.sink = NULL;
}

#else // no controller seam to open

proto_bool bus_capture_begin(int tx_pin, int rx_pin, uint32_t bitrate, bus_capture_sink_fn sink)
{
    (void)tx_pin;
    (void)rx_pin;
    (void)bitrate;
    (void)sink;
    return PROTO_FALSE;
}
void bus_capture_poll(void)
{
    // no controller, so nothing to drain
}
void bus_capture_end(void)
{
    // no controller, so nothing to stop
}

#endif // PROTOCORE_HAS_VENDOR_CAN

#endif // PROTOCORE_ENABLE_BUS_CAPTURE
