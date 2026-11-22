// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wifi_link.c
 * @brief Layer 1 (Physical) - the 802.11 station link under the idemIP stack. See wifi_link.h.
 *
 * The receive ring is one SPSC ring of MMgr. Each receive buffer is one loculus of the ring. The radio
 * task is the producer. It holds a ready loculus and copies the frame into its buffer. Then it puts the
 * index of the loculus on the byte stream of the ring.
 *
 * Worker 0 is the consumer. The entry rx_claim reads the next index, and gives out the region that the
 * loculus keeps out. The entry rx_release drops the loculus of the buffer that it names, in any order. The
 * ring orders each put before the read that finds it.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_IDEMIP

#include "network_drivers/physical/wifi_link/wifi_link.h"

#include "embed_atomic.h"                      // embed_atomic_u32: the association, and the dropped frames
#include "memoria_anularis/memoria_anularis.h" // the SPSC ring, its byte stream and its loculi

EMBED_BEGIN_DECLS

/** @brief The bytes of the arrival stream: one index for each buffer, and the byte that the ring keeps back. */
#define WIFI_LINK_ARRIVAL_BYTES (2u * IDEMIP_RX_DESCRIPTORS)

static_assert(IDEMIP_RX_DESCRIPTORS <= MMGR_RING_LOCULI,
              "each receive buffer is one loculus: PROTOCORE_RX_BUFFERS is MMGR_RING_LOCULI at most");
static_assert(IDEMIP_RX_DESCRIPTORS <= 256u, "the arrival stream holds each loculus index in one octet");
static_assert((IDEMIP_TX_DESCRIPTORS & (IDEMIP_TX_DESCRIPTORS - 1u)) == 0u,
              "IDEMIP_TX_DESCRIPTORS must be a power of two: tx_claim takes the buffers in turn with a mask");

ParsMemoriaeInternae(wifi_link_receive, IDEMIP_RX_DESCRIPTORS *IDEMIP_DMA_BUF_STRIDE);
ParsMemoriaeInternae(wifi_link_transmit, IDEMIP_TX_DESCRIPTORS *IDEMIP_DMA_BUF_STRIDE);
ParsMemoriaeInternae(wifi_link_arrival, WIFI_LINK_ARRIVAL_BYTES);
MemoriaAnularis(s_arrival, wifi_link_arrival);

/** @brief The loculi of the receive buffers, one bit for each one. */
#define WIFI_LINK_BUFFER_MASK                                                                                          \
    ((IDEMIP_RX_DESCRIPTORS >= EMBED_WORD_BITS) ? ~(embed_word)0u                                                      \
                                                : (embed_word)(((embed_word)1u << IDEMIP_RX_DESCRIPTORS) - 1u))

/** @brief The state of the link beside the ring: the radio task writes the two atomics, and worker 0 the rest. */
typedef struct
{
    embed_atomic_u32 dropped;  ///< the frames that found no ready loculus, no fit, or no word boundary
    embed_atomic_u32 up;       ///< 1 while the station is in its network
    uint32_t transmit_next;    ///< the transmit buffer that the next tx_claim gives out
    uint8_t *transmit_claimed; ///< the buffer of the last tx_claim, which tx_commit sends
    uint8_t mac[IDEMIP_MAC_LEN];
    embed_bool mac_read;
    embed_bool laid; ///< the ring is laid, and each loculus is ready
} WifiLinkState;

static WifiLinkState s_link;

// --- the radio task ---------------------------------------------------------

embed_bool protocore_wifi_link_frame_receive(const WifiLinkFrameArgs *args)
{
    // Explicit cast reads the address as an integer, where the test for word alignment is arithmetic
    const embed_bool aligned = ((uintptr_t)args->frame & (uintptr_t)(sizeof(embed_word) - 1u)) == 0u;
    if (!s_link.laid || !aligned || args->len == 0u || args->len > IDEMIP_DMA_FRAME_MAX)
    {
        (void)EMBED_CALL(embed_atomic_u32_table.fetch_add, EmbedAtomicU32FetchAddCfg, .word = &s_link.dropped,
                         .value = 1u);
        return EMBED_FALSE;
    }
    const embed_word ready =
        EMBED_CALL(anularis.loculus_ready, AnularisCfg, .ring = &s_arrival) & WIFI_LINK_BUFFER_MASK;
    const embed_iword next = EMBED_CALL(anularis.loculus_next, AnularisCfg, .mask = ready);
    if (next < 0)
    {
        (void)EMBED_CALL(embed_atomic_u32_table.fetch_add, EmbedAtomicU32FetchAddCfg, .word = &s_link.dropped,
                         .value = 1u);
        return EMBED_FALSE;
    }
    // Explicit cast takes the loculus index, which the test holds at 0 or above, and the mask below
    // IDEMIP_RX_DESCRIPTORS
    const size_t loculus = (size_t)next;
    uint8_t *const buffer = mmgr_pars_storage_wifi_link_receive + (loculus * IDEMIP_DMA_BUF_STRIDE);
    if (!EMBED_CALL(anularis.loculus_hold, AnularisCfg, .ring = &s_arrival, .index = loculus, .src = buffer,
                    .bytes = args->len))
    {
        (void)EMBED_CALL(embed_atomic_u32_table.fetch_add, EmbedAtomicU32FetchAddCfg, .word = &s_link.dropped,
                         .value = 1u);
        return EMBED_FALSE;
    }
    EMBED_CALL(memor.cpy, MemoriaCfg, .dst = buffer, .src = args->frame, .bytes = args->len);
    // Explicit cast narrows the index, which the assert above holds below 256
    const uint8_t index = (uint8_t)loculus;
    // The put publishes the index after the copy, and the ring orders it before the read that finds it.
    (void)EMBED_CALL(anularis.put, AnularisCfg, .ring = &s_arrival, .src = &index, .bytes = 1u);
    return EMBED_TRUE;
}

void protocore_wifi_link_state_set(const WifiLinkStateArgs *args)
{
    EMBED_CALL(embed_atomic_u32_table.store, EmbedAtomicU32StoreCfg, .word = &s_link.up, .value = args->up ? 1u : 0u);
}

// --- the driver, in worker 0 ------------------------------------------------

static IdemIpPhyLink wifi_link_link(void)
{
    // IEEE 802.11 has no Clause 22 line rate. Nothing in idemIP reads the speed, and the link reports none.
    const IdemIpPhyLink link = {
        .speed = IDEMIP_PHY_SPEED_NONE,
        .full_duplex = EMBED_FALSE,
        .up = (EMBED_CALL(embed_atomic_u32_table.load, EmbedAtomicU32LoadCfg, .word = &s_link.up) != 0u) ? EMBED_TRUE
                                                                                                         : EMBED_FALSE,
    };
    return link;
}

static const uint8_t *wifi_link_mac(void)
{
    if (!s_link.mac_read)
    {
        s_link.mac_read = protocore_wifi_radio_mac_read(s_link.mac);
    }
    return s_link.mac;
}

static size_t wifi_link_rx_claim(const uint8_t **frame)
{
    uint8_t index = 0u;
    if (!s_link.laid || !EMBED_CALL(anularis.read_byte, AnularisCfg, .ring = &s_arrival, .dst = &index))
    {
        return 0u;
    }
    const mmgr_ring_span *const region =
        EMBED_CALL(anularis.loculus_keepout, AnularisCfg, .ring = &s_arrival, .index = index);
    *frame = region->buf;
    return region->bytes;
}

static void wifi_link_rx_release(const uint8_t *frame)
{
    // Explicit cast takes the offset of the buffer, which Dma holds inside the pool that bind gave it
    const size_t loculus = (size_t)(frame - mmgr_pars_storage_wifi_link_receive) / IDEMIP_DMA_BUF_STRIDE;
    EMBED_CALL(anularis.loculus_drop, AnularisCfg, .ring = &s_arrival, .index = loculus);
}

// The radio copies a frame before its send returns. Each transmit buffer is thus free again at the
// reap of the next tick, and the claims take the buffers in turn.
static uint8_t *wifi_link_tx_claim(size_t len)
{
    if (len > IDEMIP_DMA_FRAME_MAX)
    {
        return NULL;
    }
    uint8_t *const buffer =
        mmgr_pars_storage_wifi_link_transmit + ((size_t)s_link.transmit_next * IDEMIP_DMA_BUF_STRIDE);
    s_link.transmit_next = (s_link.transmit_next + 1u) & (IDEMIP_TX_DESCRIPTORS - 1u);
    s_link.transmit_claimed = buffer;
    return buffer;
}

static embed_bool wifi_link_tx_commit(size_t len)
{
    const WifiLinkFrameArgs frame = {.frame = s_link.transmit_claimed, .len = len};
    return (s_link.transmit_claimed != NULL && protocore_wifi_radio_frame_send(&frame)) ? EMBED_TRUE : EMBED_FALSE;
}

// The buffers are in internal RAM, which the radio reads and writes through the processor. No cache
// line holds a stale copy. Both entries thus do nothing.
static void wifi_link_cache_invalidate(const void *region, size_t len)
{
    (void)region;
    (void)len;
}

static void wifi_link_cache_clean(const void *region, size_t len)
{
    (void)region;
    (void)len;
}

// An 802.11 station has no IEEE 802.3 Clause 22 management interface. Each access thus refuses. The read has
// the signature of IdemIpPhyDriver::mdio_read, and a driver with a management interface writes out.
static embed_bool wifi_link_mdio_read(uint8_t phy_addr, uint8_t reg, uint16_t *out) // NOSONAR(c:S995)
{
    (void)phy_addr;
    (void)reg;
    (void)out;
    return EMBED_FALSE;
}

static embed_bool wifi_link_mdio_write(uint8_t phy_addr, uint8_t reg, uint16_t val)
{
    (void)phy_addr;
    (void)reg;
    (void)val;
    return EMBED_FALSE;
}

static const IdemIpPhyDriver s_driver = {
    .link = wifi_link_link,
    .mac = wifi_link_mac,
    .rx_claim = wifi_link_rx_claim,
    .rx_release = wifi_link_rx_release,
    .tx_claim = wifi_link_tx_claim,
    .tx_commit = wifi_link_tx_commit,
    .cache_invalidate = wifi_link_cache_invalidate,
    .cache_clean = wifi_link_cache_clean,
    .mdio_read = wifi_link_mdio_read,
    .mdio_write = wifi_link_mdio_write,
};

// --- the entries ------------------------------------------------------------

// Lays the ring before the radio starts. The first frame thus finds every loculus ready.
static embed_bool wifi_link_lay(void)
{
    if (!s_link.laid)
    {
        s_link.laid =
            EMBED_CALL(anularis.init, AnularisCfg, .ring = &s_arrival, .buf = mmgr_pars_storage_wifi_link_arrival,
                       .capacity = WIFI_LINK_ARRIVAL_BYTES, .segment_count = 1u);
    }
    return s_link.laid;
}

embed_bool protocore_wifi_link_start(const WifiLinkStartArgs *args)
{
    return wifi_link_lay() && protocore_wifi_radio_start(args);
}

const IdemIpPhyDriver *protocore_wifi_link_driver(void)
{
    (void)wifi_link_lay();
    return &s_driver;
}

uint8_t *protocore_wifi_link_rx_base(void)
{
    return mmgr_pars_storage_wifi_link_receive;
}

uint8_t *protocore_wifi_link_tx_base(void)
{
    return mmgr_pars_storage_wifi_link_transmit;
}

uint32_t protocore_wifi_link_dropped(void)
{
    return EMBED_CALL(embed_atomic_u32_table.load, EmbedAtomicU32LoadCfg, .word = &s_link.dropped);
}

// --- the radio seam: each weak default refuses ------------------------------

EMBED_WEAK embed_bool protocore_wifi_radio_start(const WifiLinkStartArgs *args)
{
    (void)args;
    return EMBED_FALSE;
}

EMBED_WEAK embed_bool protocore_wifi_radio_frame_send(const WifiLinkFrameArgs *args)
{
    (void)args;
    return EMBED_FALSE;
}

EMBED_WEAK embed_bool protocore_wifi_radio_mac_read(uint8_t *mac) // NOSONAR(c:S995): a port writes mac
{
    (void)mac;
    return EMBED_FALSE;
}

EMBED_END_DECLS

#endif // PROTOCORE_ENABLE_IDEMIP
