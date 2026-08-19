// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bus_capture.c
 * @brief bus_capture implementation: the pure SocketCAN framer + the listen-only controller bind.
 *
 * The controller itself is the platform's: this reaches it through the protocore_platform_can_*
 * seam, so the framing and the drain loop run the same on a host with the seam mocked.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_BUS_CAPTURE

#include "config/platform/platform.h"
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/signaling/bus_capture/bus_capture.h"
#include "shared/pcap/pcap.h"

PROTOCORE_BEGIN_DECLS

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_BUS_CAPTURE_BORROW persistent bytes
} BusCaptureOwnCtx;
static BusCaptureOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_bus_capture_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_BUS_CAPTURE_BORROW).buf;
    }
    return s_own.span;
}

static void bus_capture_can_to_socketcan(uint8_t *restrict work)
{
    (void)work;
    const CanFrame *f = BusCapture.can_to_socketcan_args.f;
    uint8_t *out = BusCapture.can_to_socketcan_args.out;
    size_t cap = BusCapture.can_to_socketcan_args.cap;

    if (!f || !out || cap < PROTOCORE_SOCKETCAN_FRAME_LEN)
    {
        BusCapture.n = 0;
        return;
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
    BusCapture.n = PROTOCORE_SOCKETCAN_FRAME_LEN;
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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define BUS_CAPTURE_OFF_CTX 0u
static_assert(BUS_CAPTURE_OFF_CTX + sizeof(BusCaptureCtx) <= PROTOCORE_BUS_CAPTURE_BORROW,
              "PROTOCORE_BUS_CAPTURE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define BUS_CAPTURE_CTX(w) ((BusCaptureCtx *)(void *)((w) + BUS_CAPTURE_OFF_CTX))

static void bus_capture_begin(uint8_t *restrict work)
{
    int tx_pin = BusCapture.begin_args.tx_pin;
    int rx_pin = BusCapture.begin_args.rx_pin;
    uint32_t bitrate = BusCapture.begin_args.bitrate;
    bus_capture_sink_fn sink = BusCapture.begin_args.sink;

    if (!sink || !protocore_platform_can_open(tx_pin, rx_pin, bitrate))
    {
        BusCapture.ok = PROTO_FALSE;
        return;
    }
    BUS_CAPTURE_CTX(work)->sink = sink;
    BUS_CAPTURE_CTX(work)->running = PROTO_TRUE;
    BusCapture.ok = PROTO_TRUE;
}

static void bus_capture_poll(uint8_t *restrict work)
{

    if (!BUS_CAPTURE_CTX(work)->running || !BUS_CAPTURE_CTX(work)->sink)
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
        BUS_CAPTURE_CTX(work)->sink(&f);
    }
}

static void bus_capture_end(uint8_t *restrict work)
{

    if (!BUS_CAPTURE_CTX(work)->running)
    {
        return;
    }
    protocore_platform_can_close();
    BUS_CAPTURE_CTX(work)->running = PROTO_FALSE;
    BUS_CAPTURE_CTX(work)->sink = NULL;
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

BusCaptureNs BusCapture = {.can_to_socketcan = bus_capture_can_to_socketcan,
                           .begin = bus_capture_begin,
                           .poll = bus_capture_poll,
                           .end = bus_capture_end};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_BUS_CAPTURE
