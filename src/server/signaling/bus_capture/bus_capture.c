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

void protocore_bus_capture_can_to_socketcan(uint8_t *restrict work)
{
    (void)work;
    const CanFrame *f = BusCaptureV.can_to_socketcan_args.f;
    uint8_t *out = BusCaptureV.can_to_socketcan_args.out;
    size_t cap = BusCaptureV.can_to_socketcan_args.cap;

    if (!f || !out || cap < PROTOCORE_SOCKETCAN_FRAME_LEN)
    {
        BusCaptureV.n = 0;
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
    BusCaptureV.n = PROTOCORE_SOCKETCAN_FRAME_LEN;
}

// --- Controller binding ------------------------------------------------------------------
// --- the borrow, carved above the capability gate ---------------------------
//
// Above it, because the borrow is the MODULE's and not the seam's: same size, same offset, same
// alignment, whether or not a CAN controller exists behind it. The carving used to sit inside
// `#if PROTOCORE_HAS_VENDOR_CAN` while the borrow is handed out unconditionally, so with the
// capability off the module owned a borrow and had nothing to read it with.

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

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    BUS_CAPTURE_OFF_CTX % _Alignof(BusCaptureCtx) == 0,
    "BUS_CAPTURE_OFF_CTX is not a multiple of alignof(BusCaptureCtx) - BUS_CAPTURE_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define BUS_CAPTURE_CTX(w) ((BusCaptureCtx *)(void *)((w) + BUS_CAPTURE_OFF_CTX))

#if PROTOCORE_HAS_VENDOR_CAN

// All bus-capture bind state, owned by one instance (internal linkage): the frame sink and
// the running flag, grouped so it is one named owner, unreachable from any other TU.

void protocore_bus_capture_begin(uint8_t *restrict work)
{
    int tx_pin = BusCaptureV.begin_args.tx_pin;
    int rx_pin = BusCaptureV.begin_args.rx_pin;
    uint32_t bitrate = BusCaptureV.begin_args.bitrate;
    bus_capture_sink_fn sink = BusCaptureV.begin_args.sink;

    if (!sink || !protocore_platform_can_open(tx_pin, rx_pin, bitrate))
    {
        BusCaptureV.ok = PROTO_FALSE;
        return;
    }
    BUS_CAPTURE_CTX(work)->sink = sink;
    BUS_CAPTURE_CTX(work)->running = PROTO_TRUE;
    BusCaptureV.ok = PROTO_TRUE;
}

void protocore_bus_capture_poll(uint8_t *restrict work)
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

void protocore_bus_capture_end(uint8_t *restrict work)
{

    if (!BUS_CAPTURE_CTX(work)->running)
    {
        return;
    }
    protocore_platform_can_close();
    BUS_CAPTURE_CTX(work)->running = PROTO_FALSE;
    BUS_CAPTURE_CTX(work)->sink = NULL;
}

#else // no controller seam to open: the three entries answer, and capture never starts

// The SAME THREE ENTRIES the header declares, not a second API beside them. This arm used to define
// protocore_bus_capture_begin(int, int, uint32_t, bus_capture_sink_fn) - the pre-namespace flat
// shape - under the name bus_capture.h declares as an entry taking the borrow. Not a stale
// duplicate sitting quietly next to the table: the same symbol with two signatures, so the arm
// failed on `conflicting types` before reaching anything else. Nothing reported it, because no env
// states PROTOCORE_HAS_VENDOR_CAN=0 and this half has never been compiled.

void protocore_bus_capture_begin(uint8_t *restrict work)
{
    // The context is still cleared, so a later poll or end reads a stopped capture rather than
    // whatever the borrow held before.
    BUS_CAPTURE_CTX(work)->sink = NULL;
    BUS_CAPTURE_CTX(work)->running = PROTO_FALSE;
    BusCaptureV.ok = PROTO_FALSE; // no controller was opened
}

void protocore_bus_capture_poll(uint8_t *restrict work)
{
    (void)work; // no controller, so nothing to drain
}

void protocore_bus_capture_end(uint8_t *restrict work)
{
    (void)work; // no controller, so nothing to stop
}

#endif // PROTOCORE_HAS_VENDOR_CAN

/** @brief The operands and the outcome. */
BusCaptureVars BusCaptureV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_BUS_CAPTURE
