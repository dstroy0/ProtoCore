// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file stack.h
 * @brief Layer 3 (Network) - the idemIP stack: the cells of its units, its interfaces, and its tick.
 *
 * The TCP/IP stack is idemIP, under include/idemIP. This module holds one cell for each unit of idemIP.
 * It binds each interface to its link driver. Each call of ::StackNs::tick runs one pass of the stack.
 * Worker 0 calls the tick. The stack takes no lock, because no other context calls into it.
 *
 * Each cell comes from the plaintext cellblock. The cell counts to the owner of its unit, which
 * IDEMIP_OWNERS names. worker.h puts those owners into the owner table of the program.
 *
 * A pass drains each receive ring one frame at a time. Dispatch.input reads the frame, the reply that it
 * builds goes out on the same interface, and the descriptor goes back to the engine. Then the services and
 * the flush of idemIP run in the order that Tick enforces. Last, the engine gives back the transmit
 * descriptors that it finished.
 *
 * include/idemip_capabilities.h gives each IDEMIP_ENABLE_* value. idemIP sizes its tables from them and
 * has no default for them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_STACK_H
#define PROTOCORE_STACK_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_IDEMIP

// Only the driver table and the counts. A sketch includes this header beside protocore.h, and the whole
// of idemIP has names that ProtoCore also has. The source of this module reads the whole of idemIP.
#include "src/ethernet/phy.h" // IdemIpPhyDriver, and through it the counts of idemip_config.h

PROTOCORE_BEGIN_DECLS

/**
 * @brief What ::StackNs::start takes.
 *
 * @var StackStartArgs::isn_secret IDEMIP_TCP_ISN_SECRET_BYTES random octets, the RFC 6528 sec 3 secret
 *                                 of the initial sequence numbers. The stack copies them.
 */
typedef struct
{
    const uint8_t *isn_secret;
} StackStartArgs;

/**
 * @brief What ::StackNs::bind takes: one interface and its link driver.
 *
 * @var StackBindArgs::driver   the link driver of the interface [BORROWS]
 * @var StackBindArgs::mac      IDEMIP_MAC_LEN octets of hardware address [BORROWS]
 * @var StackBindArgs::rx_base  IDEMIP_RX_DESCRIPTORS buffers of IDEMIP_DMA_BUF_STRIDE octets, on a cache line
 * @var StackBindArgs::tx_base  IDEMIP_TX_DESCRIPTORS buffers of IDEMIP_DMA_BUF_STRIDE octets, on a cache line
 * @var StackBindArgs::phy_addr the Clause 22 management address of the PHY, 0 through 31
 * @var StackBindArgs::index    the interface, below IDEMIP_NETIF_COUNT
 */
typedef struct
{
    const IdemIpPhyDriver *driver;
    const uint8_t *mac;
    uint8_t *rx_base;
    uint8_t *tx_base;
    uint8_t phy_addr;
    uint8_t index;
} StackBindArgs;

/**
 * @brief What ::StackNs::set_addr4 takes. Each address is in host order.
 *
 * @var StackAddr4Args::address the IPv4 address of the interface
 * @var StackAddr4Args::mask    its subnet mask
 * @var StackAddr4Args::gateway its default gateway, 0 for none
 * @var StackAddr4Args::index   the interface
 */
typedef struct
{
    uint32_t address;
    uint32_t mask;
    uint32_t gateway;
    uint8_t index;
} StackAddr4Args;

/**
 * @brief What ::StackNs::tick takes.
 *
 * @var StackTickArgs::now_ms      the monotonic millisecond count of the caller
 * @var StackTickArgs::random_word a random word for this pass, which RFC 2236 sec 3 report delays draw from
 */
typedef struct
{
    uint32_t now_ms;
    uint32_t random_word;
} StackTickArgs;

/**
 * @brief The operands and the outcome.
 *
 * @var StackVars::start_args the secret that ::StackNs::start takes
 * @var StackVars::bind_args  the interface that ::StackNs::bind takes
 * @var StackVars::addr4_args the address that ::StackNs::set_addr4 takes
 * @var StackVars::tick_args  the clock and the random word that ::StackNs::tick takes
 * @var StackVars::ok         the outcome of the last call
 * @var StackVars::frames     the frames that the last tick drained
 * @var StackVars::sent       the frames that the last tick put on a transmit ring
 */
typedef struct
{
    StackStartArgs start_args;
    StackBindArgs bind_args;
    StackAddr4Args addr4_args;
    StackTickArgs tick_args;
    proto_bool ok;
    uint16_t frames;
    uint16_t sent;
} StackVars;

/** @brief The operands and the outcome. */
extern StackVars StackV;

/**
 * @brief The entries.
 *
 * @var StackNs::start     clear each unit, seed the initial sequence numbers and bind the shared units
 * @var StackNs::bind      bind one interface to its link driver and its frame buffers
 * @var StackNs::set_addr4 give an interface its IPv4 address
 * @var StackNs::tick      run one pass of the stack: drain, services, flush
 */
typedef struct
{
    void (*const start)(uint8_t *work);
    void (*const bind)(uint8_t *work);
    void (*const set_addr4)(uint8_t *work);
    void (*const tick)(uint8_t *work);
} StackNs;

// The functions that the table binds. The .c defines each one, and each one takes one parameter.
void protocore_stack_start(uint8_t *work);
void protocore_stack_bind(uint8_t *work);
void protocore_stack_set_addr4(uint8_t *work);
void protocore_stack_tick(uint8_t *work);

/**
 * @brief The cells of the unit borrows of idemIP, the region that each unit runs in.
 *
 * @param[in] work The borrow of this module [BORROWS].
 * @note A caller above the network layer reads a unit through this, as the transport reads TcpPcb.
 */
typedef struct
{
    uint8_t *netif;
    uint8_t *loopif;
    uint8_t *arp;
    uint8_t *ip4_route;
    uint8_t *ip4_forward;
    uint8_t *ip6_forward;
    uint8_t *ip4_reass;
    uint8_t *ip4_frag;
    uint8_t *igmp;
    uint8_t *ip6_reass;
    uint8_t *ip6_frag;
    uint8_t *mld6;
    uint8_t *icmp_in;
    uint8_t *icmp6_in;
    uint8_t *raw_pcb;
    uint8_t *udp_pcb;
    uint8_t *udplite;
    uint8_t *tcp_pcb;
    uint8_t *tcp_in;
    uint8_t *tcp_out;
    uint8_t *tcp_isn;
    uint8_t *ip4_addr;
    uint8_t *ip6_addr;
    uint8_t *ip6_select;
    uint8_t *pmtu4;
    uint8_t *dns;
    uint8_t *timeouts;
    uint8_t *tick;
    uint8_t *dispatch;
    uint8_t *stats;
    uint8_t *vlan;
    uint8_t *ethip6;
    uint8_t *phy[IDEMIP_NETIF_COUNT];
    uint8_t *dma[IDEMIP_NETIF_COUNT];
    uint8_t *nd6[IDEMIP_NETIF_COUNT];
    uint8_t *acd[IDEMIP_NETIF_COUNT];
    uint8_t *autoip[IDEMIP_NETIF_COUNT];
    uint8_t *pmtu6[IDEMIP_NETIF_COUNT];
    uint8_t *dhcp4[IDEMIP_NETIF_COUNT];
    uint8_t *dhcp6[IDEMIP_NETIF_COUNT];
    uint8_t *dad[IDEMIP_NETIF_COUNT];
    uint8_t *slaac[IDEMIP_NETIF_COUNT];
    uint8_t *rdnss[IDEMIP_NETIF_COUNT];
} StackUnits;

/** @brief The unit borrows of the stack in @p work. */
const StackUnits *protocore_stack_units(const uint8_t *work);

// The table is `static const` with its initializer here, and not `extern` against a definition in
// the .c. Each translation unit can see the initializer. `Stack.tick(work)` thus resolves to a named
// function and becomes a direct call.
static const StackNs Stack __attribute__((unused)) = {
    .start = protocore_stack_start,
    .bind = protocore_stack_bind,
    .set_addr4 = protocore_stack_set_addr4,
    .tick = protocore_stack_tick,
};

/** @brief The borrow of this module: its context, taken once from the persistent tier. */
uint8_t *protocore_stack_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IDEMIP

#endif // PROTOCORE_STACK_H
