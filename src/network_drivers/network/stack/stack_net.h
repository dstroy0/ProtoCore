// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file stack_net.h
 * @brief The stack surface of the platform, over idemIP: the protocore_net_* names that the transport calls.
 *
 * With PROTOCORE_ENABLE_IDEMIP, the platform header takes these declarations in place of those of lwIP. The
 * names, the types and the callbacks are those of the raw API of lwIP, so that the transport above runs as it
 * does over lwIP. The stack module of the network layer answers each call: stack.c runs the units of idemIP,
 * stack_tcp.c holds each TCP connection, and stack_net.c maps the raw API onto both.
 *
 * Each call runs under the lock of the stack. protocore_net_call_marshal takes the lock and runs its function
 * in the thread of the caller. A callback runs in the pass of the stack, which holds the lock already.
 *
 * The platform header reaches this one before protocore_config.h defines anything. It thus reads embedded_types
 * alone.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_STACK_NET_H
#define PROTOCORE_STACK_NET_H

#include <stddef.h>
#include <stdint.h>

#include "embed_compiler_directives.h" // EMBED_BEGIN_DECLS
#include "embed_types.h"               // embed_bool and the widths

EMBED_BEGIN_DECLS

// --- results -------------------------------------------------------------------------

/** @brief The result of a call of the stack, as lwIP numbers it. */
typedef int8_t protocore_net_err;
#define PROTOCORE_NET_OK 0
#define PROTOCORE_NET_ERR_MEM (-1)
#define PROTOCORE_NET_ERR_BUF (-2)
#define PROTOCORE_NET_ERR_VAL (-6)
#define PROTOCORE_NET_ERR_USE (-8)
#define PROTOCORE_NET_ERR_CONN (-11)
#define PROTOCORE_NET_ERR_ABRT (-13)
#define PROTOCORE_NET_ERR_RST (-14)
#define PROTOCORE_NET_ERR_CLSD (-15)
#define PROTOCORE_NET_ERR_ARG (-16)
#define PROTOCORE_NET_ERR_INPROGRESS (-5)

// --- addresses -----------------------------------------------------------------------

#define PROTOCORE_NET_TYPE_ANY 0
#define PROTOCORE_NET_TYPE_V4 4
#define PROTOCORE_NET_TYPE_V6 6
#define PROTOCORE_NET_HAS_IGMP 1
#define PROTOCORE_NET_HAS_IPV6 0

/**
 * @brief One address of the stack.
 *
 * @var protocore_net_ip::type  PROTOCORE_NET_TYPE_V4, or PROTOCORE_NET_TYPE_ANY for the any address
 * @var protocore_net_ip::bytes the address in network order, IPv4 in the first four octets
 */
typedef struct
{
    uint8_t type;
    uint8_t bytes[16];
} protocore_net_ip;

/** @brief The any address, which a bind takes for each local address. */
extern const protocore_net_ip protocore_stack_net_any;
#define PROTOCORE_NET_ADDR_ANY (&protocore_stack_net_any)
#define PROTOCORE_NET_ADDR_ANY4 (&protocore_stack_net_any)
#define PROTOCORE_NET_ADDR_ANY4_P (&protocore_stack_net_any)

#define protocore_net_ip_is_v4(a) ((a) != NULL && (a)->type == PROTOCORE_NET_TYPE_V4)
#define protocore_net_ip_is_v6(a) ((a) != NULL && (a)->type == PROTOCORE_NET_TYPE_V6)
#define protocore_net_ip_as_v4(a) (a)
#define protocore_net_ip_as_v6(a) (a)
#define protocore_net_ip6_bytes(a) ((const uint8_t *)(a)->bytes)
#define protocore_net_ip6_wbytes(a) ((uint8_t *)(a)->bytes)
#define protocore_net_ip6_mark(a) ((a)->type = PROTOCORE_NET_TYPE_V6)

/** @brief The IPv4 address of @p a as the word that lwIP keeps: the four octets in network order in memory. */
uint32_t protocore_net_ip4_u32(const protocore_net_ip *a);
/** @brief Set @p a to the IPv4 address b0.b1.b2.b3. */
void protocore_net_ip4_set(protocore_net_ip *a, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3);
/** @brief 1 when @p a is an IPv4 host group, 224.0.0.0/4. */
int protocore_net_ip4_is_multicast(const protocore_net_ip *a);
/** @brief Parse the dotted quad @p s into @p out. 1 on success, and 0 otherwise. */
int protocore_net_ip_parse(const char *s, protocore_net_ip *out);
/** @brief The dotted quad of @p a in @p buf, of @p cap octets. */
char *protocore_net_ip_print(const protocore_net_ip *a, char *buf, int cap);

// --- packet buffers --------------------------------------------------------------------

#define PROTOCORE_NET_PBUF_TRANSPORT 0
#define PROTOCORE_NET_PBUF_RAM 0

/**
 * @brief One packet buffer, a chain of one link.
 *
 * @var protocore_pbuf::next    the next link, NULL here
 * @var protocore_pbuf::payload the octets
 * @var protocore_pbuf::tot_len the octets of the chain
 * @var protocore_pbuf::len     the octets of this link
 */
typedef struct protocore_pbuf
{
    struct protocore_pbuf *next;
    void *payload;
    uint16_t tot_len;
    uint16_t len;
} protocore_pbuf;

/** @brief A packet buffer of @p len octets from the pool, or NULL when the pool is spent. */
protocore_pbuf *protocore_net_pbuf_alloc(int layer, uint16_t len, int type);
/** @brief Give a packet buffer back to the pool. */
uint8_t protocore_net_pbuf_free(const protocore_pbuf *p);
/** @brief Copy at most @p len octets of @p p, from @p off, to @p dst. The octets copied. */
uint16_t protocore_net_pbuf_copy(const protocore_pbuf *p, void *dst, uint16_t len, uint16_t off);

// --- TCP -------------------------------------------------------------------------------

#define PROTOCORE_NET_WRITE_COPY 0x01
#define PROTOCORE_NET_OPT_REUSEADDR 0x04

typedef struct protocore_pcb protocore_pcb;
typedef struct protocore_udp_pcb protocore_udp_pcb;
typedef protocore_net_err (*protocore_net_recv_fn)(void *, protocore_pcb *, protocore_pbuf *, protocore_net_err);
typedef protocore_net_err (*protocore_net_sent_fn)(void *, protocore_pcb *, uint16_t);
typedef protocore_net_err (*protocore_net_accept_fn)(void *, protocore_pcb *, protocore_net_err);
typedef protocore_net_err (*protocore_net_connect_fn)(void *, protocore_pcb *, protocore_net_err);
typedef void (*protocore_net_err_fn)(void *, protocore_net_err);
typedef void (*protocore_net_udp_recv_fn)(void *, protocore_udp_pcb *, protocore_pbuf *, const protocore_net_ip *,
                                          uint16_t);

/**
 * @brief An active open that waits for the ARP row of its next hop.
 *
 * @var StackNetConnectWait::deadline_ms the millisecond that the wait ends at
 * @var StackNetConnectWait::arp_sent_ms the millisecond that the last ARP request went out
 * @var StackNetConnectWait::pending     the wait runs
 */
typedef struct
{
    uint32_t deadline_ms;
    uint32_t arp_sent_ms;
    embed_bool pending;
} StackNetConnectWait;

/**
 * @brief One TCP control block of the stack.
 *
 * @var protocore_pcb::tos          RFC 791 Type of Service of each segment
 * @var protocore_pcb::ttl          RFC 791 Time to Live of each segment
 * @var protocore_pcb::state        nonzero while the control block is open
 * @var protocore_pcb::local_port   the local port
 * @var protocore_pcb::remote_port  the port of the peer
 * @var protocore_pcb::local_ip     the local address
 * @var protocore_pcb::remote_ip    the address of the peer
 * @var protocore_pcb::so_options   the options that protocore_net_opt_set set
 * @var protocore_pcb::snd_queuelen the segments of the connection that the peer did not acknowledge yet
 * @var protocore_pcb::flags        the flags of lwIP, which this stack does not read
 * @var protocore_pcb::arg          the argument of each callback
 * @var protocore_pcb::on_recv      the callback of text, and of the FIN with a NULL buffer
 * @var protocore_pcb::on_sent      the callback of octets that the peer acknowledged
 * @var protocore_pcb::on_accept    the callback of a connection that a listener took
 * @var protocore_pcb::on_connect   the callback of an active open that completed
 * @var protocore_pcb::on_err       the callback of a connection that a reset or a timeout ended
 * @var protocore_pcb::connect_wait the active open that waits for the ARP row of its next hop
 * @var protocore_pcb::entry        the entry of the TCP of the stack, PROTOCORE_STACK_NET_NONE for none
 * @var protocore_pcb::listen       the control block listens
 * @var protocore_pcb::in_use       the control block is taken
 */
struct protocore_pcb
{
    uint8_t tos;
    uint8_t ttl;
    uint8_t state;
    uint16_t local_port;
    uint16_t remote_port;
    protocore_net_ip local_ip;
    protocore_net_ip remote_ip;
    uint32_t so_options;
    uint16_t snd_queuelen;
    uint8_t flags;
    void *arg;
    protocore_net_recv_fn on_recv;
    protocore_net_sent_fn on_sent;
    protocore_net_accept_fn on_accept;
    protocore_net_connect_fn on_connect;
    protocore_net_err_fn on_err;
    StackNetConnectWait connect_wait;
    uint8_t entry;
    embed_bool listen;
    embed_bool in_use;
};

/** @brief The entry index that names none. */
#define PROTOCORE_STACK_NET_NONE 0xFFu

protocore_pcb *protocore_net_new(int type);
protocore_net_err protocore_net_bind(protocore_pcb *p, const protocore_net_ip *a, uint16_t port);
protocore_pcb *protocore_net_listen(protocore_pcb *p, uint8_t backlog);
protocore_net_err protocore_net_connect(protocore_pcb *p, const protocore_net_ip *a, uint16_t port,
                                        protocore_net_connect_fn connected);
protocore_net_err protocore_net_close(protocore_pcb *p);
void protocore_net_abort(protocore_pcb *p);
void protocore_net_arg(protocore_pcb *p, void *arg);
void protocore_net_on_recv(protocore_pcb *p, protocore_net_recv_fn fn);
void protocore_net_on_sent(protocore_pcb *p, protocore_net_sent_fn fn);
void protocore_net_on_accept(protocore_pcb *p, protocore_net_accept_fn fn);
void protocore_net_on_err(protocore_pcb *p, protocore_net_err_fn fn);
protocore_net_err protocore_net_write(protocore_pcb *p, const void *data, uint16_t len, uint8_t flags);
protocore_net_err protocore_net_output(const protocore_pcb *p);
void protocore_net_recved(const protocore_pcb *p, uint16_t len);
uint16_t protocore_net_sndbuf(const protocore_pcb *p);
void protocore_net_nagle_disable(const protocore_pcb *p);
void protocore_net_rcv_wnd_update(const protocore_pcb *p, uint16_t len);
void protocore_net_opt_set(void *p, uint32_t opt);

// --- UDP and IGMP ----------------------------------------------------------------------

/**
 * @brief One UDP control block of the stack.
 *
 * @var protocore_udp_pcb::tos        RFC 791 Type of Service of each datagram
 * @var protocore_udp_pcb::local_port the bound port
 * @var protocore_udp_pcb::arg        the argument of the callback
 * @var protocore_udp_pcb::on_recv    the callback of each datagram
 * @var protocore_udp_pcb::so_options the options that protocore_net_opt_set set
 * @var protocore_udp_pcb::entry      the entry of the UdpPcb unit of idemIP
 * @var protocore_udp_pcb::opened     the entry is open
 * @var protocore_udp_pcb::in_use     the control block is taken
 */
struct protocore_udp_pcb
{
    uint8_t tos;
    uint16_t local_port;
    void *arg;
    protocore_net_udp_recv_fn on_recv;
    uint32_t so_options;
    uint16_t entry;
    embed_bool opened;
    embed_bool in_use;
};

protocore_udp_pcb *protocore_net_udp_new(void);
protocore_net_err protocore_net_udp_bind(protocore_udp_pcb *p, const protocore_net_ip *a, uint16_t port);
void protocore_net_udp_recv(protocore_udp_pcb *p, protocore_net_udp_recv_fn fn, void *arg);
protocore_net_err protocore_net_udp_sendto(const protocore_udp_pcb *p, const protocore_pbuf *b,
                                           const protocore_net_ip *a, uint16_t port);
void protocore_net_udp_remove(protocore_udp_pcb *p);
protocore_net_err protocore_net_igmp_join(const protocore_net_ip *nif, const protocore_net_ip *grp);
protocore_net_err protocore_net_igmp_leave(const protocore_net_ip *nif, const protocore_net_ip *grp);

// --- the context of the stack ------------------------------------------------------------

/** @brief The record that a call of the stack carries, as the transport embeds it. */
typedef struct protocore_net_call
{
    int reserved;
} protocore_net_call;

/** @brief Run @p fn on @p c under the lock of the stack, and return what it returns. */
protocore_net_err protocore_net_call_marshal(protocore_net_err (*fn)(protocore_net_call *), protocore_net_call *c);

// --- names -----------------------------------------------------------------------------------

/** @brief The callback of a name that the resolver of the stack found, with a NULL address for none. */
typedef void (*protocore_net_dns_found_fn)(const char *, const protocore_net_ip *, void *);

/**
 * @brief Resolve @p host to @p addr, as dns_gethostbyname of lwIP does.
 *
 * A dotted quad completes in the call, with PROTOCORE_NET_OK. The stack has no resolver of its own, so each
 * other name gets PROTOCORE_NET_ERR_VAL.
 */
protocore_net_err protocore_net_dns_resolve(const char *host, protocore_net_ip *addr, protocore_net_dns_found_fn found,
                                            void *arg);

// --- the pass ----------------------------------------------------------------------------------

/**
 * @brief One pass of the stack, under its lock: the units of idemIP, then each event of the TCP as a callback.
 *
 * @param now_ms      the monotonic millisecond count of the caller
 * @param random_word a random word for this pass
 */
void protocore_stack_net_pass(uint32_t now_ms, uint32_t random_word);

/**
 * @brief Start the task of the stack, which runs protocore_stack_net_pass once each tick of the platform.
 *
 * The task has the place of the tcpip thread of lwIP. Call it once, after Stack.start, Stack.bind and
 * Stack.set_addr4. A second call does nothing.
 *
 * @return 1 when the task runs, and 0 when the platform refused it
 */
int protocore_stack_net_start(void);

/**
 * @brief One UDP datagram that Dispatch delivered to a UdpPcb entry, to the callback of its control block.
 *
 * @param frame       the received frame
 * @param entry       the UdpPcb entry that the datagram matched
 * @param ip_off      the octets from the frame to the IPv4 header
 * @param payload_off the octets from the frame to the UDP header
 * @param payload_len the octets of the UDP header and its data
 */
void protocore_stack_net_udp_input(const uint8_t *frame, uint16_t entry, size_t ip_off, size_t payload_off,
                                   size_t payload_len);

EMBED_END_DECLS

#endif // PROTOCORE_STACK_NET_H
