// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file server.c
 * @brief Layer 4 (Transport) - the passive OPEN: accept callback and port lifecycle.
 *
 * `listener_accept_cb` is the single stack accept callback registered for
 * every listener.  The listener index is embedded in the PCB user-data via
 * `protocore_net_arg(listen_pcb, (void*)(uintptr_t)idx)` so this one function handles
 * all ports without a lookup table.
 *
 * The non-static per-connection callbacks (lowlevel_recv_cb, lowlevel_sent_cb,
 * lowlevel_err_cb) are defined in protocol/protocol.c and declared extern here.
 * The protocol engine's protocore_conn_enqueue() helper calls listener_enqueue(), which is
 * defined in this file - that indirection breaks the circular header dependency
 * (server.h reaches tcp.h; protocol.c includes server.h).
 */

#include "server.h"
#include "../../diffserv/diffserv.h"   // DiffServ DSCP marking for accepted connections (compiles out when off)
#include "../../net_addr/net_addr.h"   // protocore_net_addr_to_ip(): the stack's address as a protocore_ip
#include "../common.h"                 // TcpConn, conn_pool: the slots an accept claims
#include "../lower/lower.h"            // TcpLower.apply_ttl: the TTL a new pcb is stamped with
#include "../protocol/protocol.h"      // ConnPool: the slots an accept claims
#include "../tcp.h"                    // the aggregate the halves hang off
#include "config/platform/platform.h"  // the stack's queues, under our names
#include "mmgr/plaintext/plaintext.h"  // the persistent end this module's state is taken from
#include "network_drivers/tls/tls.h"   // TLS handshake begin (self-stubbing)
#include "server/clock/clock.h"        // protocore_millis() pluggable monotonic clock
#include "server/core/worker/worker.h" // Workers.wake() - nudge the owning worker task

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

// Listener pool - all storage in BSS.
Listener listener_pool[MAX_LISTENERS];

// Per-connection callbacks defined in tcp.c.
extern protocore_net_err lowlevel_recv_cb(void *arg, protocore_pcb *tpcb, protocore_pbuf *p, protocore_net_err err);
extern protocore_net_err lowlevel_sent_cb(void *arg, protocore_pcb *tpcb, proto_u16 len);
extern void lowlevel_err_cb(void *arg, protocore_net_err err);

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_TCP_LISTENER_BORROW persistent bytes
} TcpListenerOwnCtx;
static TcpListenerOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_tcp_listener_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_TCP_LISTENER_BORROW).buf;
    }
    return s_own.span;
}

// Both teardowns are called by the add that replaces an active row, above their definitions.
void protocore_tcp_listener_stop(uint8_t *restrict work);
void protocore_tcp_listener_stop_dynamic(uint8_t *restrict work);

// ---------------------------------------------------------------------------
// The accepting side's state: every bounded table, and the handle that reaches it
// ---------------------------------------------------------------------------

// Global accept-rate-limit state, owned by one instance (internal linkage): the fixed-window
// start and the accept count in the current window. One named owner, unreachable cross-TU.
typedef struct
{
    uint32_t window_start;
    uint16_t count;
} AcceptThrottleCtx;

// One bucket of the per-source-IP throttle, keyed by the address it counts.
typedef struct
{
    protocore_ip addr;     ///< source address (family PROTOCORE_IP_NONE marks an empty bucket).
    uint32_t window_start; ///< millis() at the start of this bucket's current window.
    uint16_t count;        ///< connections counted from this address in the window.
} IpThrottleBucket;

// Per-source-IP accept-throttle state, owned by one instance (internal linkage): the bounded
// bucket table keyed by source address. One named owner, unreachable from any other TU.
typedef struct
{
    IpThrottleBucket buckets[PROTOCORE_PER_IP_THROTTLE_SLOTS];
} IpThrottleCtx;

// One allowlist rule: a network and the prefix length that bounds it (RFC 4632).
typedef struct
{
    protocore_ip network; ///< network address (family PROTOCORE_IP_V4 / V6; PROTOCORE_IP_NONE marks unused).
    uint8_t prefix_len;   ///< CIDR prefix length: 0..32 for v4, 0..128 for v6.
} IpAllowRule;

// IP allowlist state, owned by one instance (internal linkage): the CIDR rule table and its
// count (empty = allow all). One named owner, unreachable from any other translation unit.
typedef struct
{
    IpAllowRule rules[PROTOCORE_IP_ALLOWLIST_SLOTS];
    uint8_t count;
} IpAllowCtx;

#if PROTOCORE_WORKER_COUNT > 1
// Per-worker event queues: each worker drains only its own queue, so connection
// slots partition across workers with no shared-queue contention. Static BSS, no
// heap. Created once (idempotent) before the first accept can fire.
// Per-worker event-queue state, owned by one instance (internal linkage): the static queue
// control blocks, their storage, and the queue handles. One named owner, unreachable cross-TU.
typedef struct
{
    protocore_platform_queue_ctrl wq_struct[PROTOCORE_WORKER_COUNT];
    uint8_t wq_storage[PROTOCORE_WORKER_COUNT][EVT_QUEUE_DEPTH * sizeof(TcpEvt)];
    protocore_platform_queue wq[PROTOCORE_WORKER_COUNT];
} ListenerQueueCtx;
#endif // PROTOCORE_WORKER_COUNT > 1

/**
 * @brief The accepting side's compile-time storage: every bounded table it keeps.
 *
 * All of it BSS, so a listener costs no heap and nothing lands on a task stack.
 */
struct TcpListenerStorage
{
    AcceptThrottleCtx accept; ///< the global fixed-window throttle
    IpThrottleCtx iptt;       ///< the per-source-address bucket table
    IpAllowCtx allow;         ///< the CIDR rule table (RFC 4632)
#if PROTOCORE_WORKER_COUNT > 1
    ListenerQueueCtx lq; ///< the per-worker event queues
#endif
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define TCP_LISTENER_OFF_CTX 0u
static_assert(TCP_LISTENER_OFF_CTX + sizeof(struct TcpListenerStorage) <= PROTOCORE_TCP_LISTENER_BORROW,
              "PROTOCORE_TCP_LISTENER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define TCP_LISTENER_CTX(w) ((struct TcpListenerStorage *)(void *)((w) + TCP_LISTENER_OFF_CTX))

// ---------------------------------------------------------------------------
// Accept-rate throttle (fixed window, global). State persists across accepts.
// Always compiled (unit-testable); only consulted when the feature is enabled.
// ---------------------------------------------------------------------------

void protocore_tcp_listener_accept_allowed(uint8_t *restrict work)
{
    // Unsigned subtraction wraps correctly across the millis() rollover.
    if ((uint32_t)(TcpListenerV.gate.now_ms - TCP_LISTENER_CTX(work)->accept.window_start) >=
        PROTOCORE_ACCEPT_THROTTLE_WINDOW_MS)
    {
        TCP_LISTENER_CTX(work)->accept.window_start = TcpListenerV.gate.now_ms;
        TCP_LISTENER_CTX(work)->accept.count = 0;
    }
    if (TCP_LISTENER_CTX(work)->accept.count >= PROTOCORE_ACCEPT_THROTTLE_MAX)
    {
        TcpListenerV.ok = PROTO_FALSE;
        return;
    }
    TCP_LISTENER_CTX(work)->accept.count++;
    TcpListenerV.ok = PROTO_TRUE;
}

void protocore_tcp_listener_accept_throttle_reset(uint8_t *restrict work)
{
    TCP_LISTENER_CTX(work)->accept.window_start = 0;
    TCP_LISTENER_CTX(work)->accept.count = 0;
}

// ---------------------------------------------------------------------------
// Per-IP accept-rate throttle (fixed window per source IPv4). A bounded BSS table
// of buckets - no heap. Always compiled (unit-testable); only consulted when the
// feature is enabled.
// ---------------------------------------------------------------------------

void protocore_tcp_listener_accept_allowed_ip(uint8_t *restrict work)
{
    IpV.args.ip = TcpListenerV.gate.addr;
    Ip.is_unspecified(ip_work);
    if (IpV.ok)
    {
        TcpListenerV.ok = PROTO_TRUE; // untrackable source - defer to the global accept throttle
        return;
    }

    int empty = -1;
    int expired = -1;
    int lru = 0;
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_SLOTS; i++)
    {
        IpThrottleBucket *b = &TCP_LISTENER_CTX(work)->iptt.buckets[i];
        IpV.args.ip = &b->addr;
        IpV.args.b = TcpListenerV.gate.addr;
        Ip.equal(ip_work);
        if (b->addr.family != PROTOCORE_IP_NONE && IpV.ok)
        {
            // Unsigned subtraction wraps correctly across the millis() rollover.
            if ((uint32_t)(TcpListenerV.gate.now_ms - b->window_start) >= PROTOCORE_PER_IP_THROTTLE_WINDOW_MS)
            {
                b->window_start = TcpListenerV.gate.now_ms;
                b->count = 0;
            }
            if (b->count >= PROTOCORE_PER_IP_THROTTLE_MAX)
            {
                TcpListenerV.ok = PROTO_FALSE;
                return;
            }
            b->count++;
            TcpListenerV.ok = PROTO_TRUE;
            return;
        }
        if (b->addr.family == PROTOCORE_IP_NONE)
        {
            if (empty < 0)
            {
                empty = i;
            }
        }
        else
        {
            if (expired < 0 &&
                (uint32_t)(TcpListenerV.gate.now_ms - b->window_start) >= PROTOCORE_PER_IP_THROTTLE_WINDOW_MS)
            {
                expired = i;
            }
            // Track the oldest active bucket (largest elapsed) as the eviction victim.
            if ((uint32_t)(TcpListenerV.gate.now_ms - b->window_start) >
                (uint32_t)(TcpListenerV.gate.now_ms - TCP_LISTENER_CTX(work)->iptt.buckets[lru].window_start))
            {
                lru = i;
            }
        }
    }

    // No bucket yet for this address: claim one - empty, else expired, else evict
    // the least-recently-started active bucket.
    int slot = (empty >= 0) ? empty : (expired >= 0) ? expired : lru;
    IpThrottleBucket *b = &TCP_LISTENER_CTX(work)->iptt.buckets[slot];
    b->addr = *TcpListenerV.gate.addr;
    b->window_start = TcpListenerV.gate.now_ms;
    b->count = 1;
    TcpListenerV.ok = PROTO_TRUE; // first connection of a fresh window is always allowed
}

void protocore_tcp_listener_per_ip_throttle_reset(uint8_t *restrict work)
{
    for (int i = 0; i < PROTOCORE_PER_IP_THROTTLE_SLOTS; i++)
    {
        TCP_LISTENER_CTX(work)->iptt.buckets[i].addr.family = PROTOCORE_IP_NONE;
        TCP_LISTENER_CTX(work)->iptt.buckets[i].window_start = 0;
        TCP_LISTENER_CTX(work)->iptt.buckets[i].count = 0;
    }
}

// ---------------------------------------------------------------------------
// Source-IP allowlist (accept-time firewall). A bounded BSS table of CIDR rules
// in host byte order. Always compiled (unit-testable); only consulted when
// PROTOCORE_ENABLE_IP_ALLOWLIST is set. An empty table allows everything so enabling
// the feature before adding rules cannot lock the device out.
// ---------------------------------------------------------------------------

void protocore_tcp_listener_ip_allow_add(uint8_t *restrict work)
{
    TcpListenerV.ok = PROTO_FALSE;
    if (!TcpListenerV.gate.addr)
    {
        return;
    }
    int bits = (TcpListenerV.gate.addr->family == PROTOCORE_IP_V4)
                   ? 32
                   : (TcpListenerV.gate.addr->family == PROTOCORE_IP_V6 ? 128 : -1);
    if (bits < 0 || TcpListenerV.gate.prefix_len > (uint8_t)bits)
    {
        return; // reject a malformed family or an over-long prefix
    }
    if (TCP_LISTENER_CTX(work)->allow.count >= PROTOCORE_IP_ALLOWLIST_SLOTS)
    {
        return;
    }
    TCP_LISTENER_CTX(work)->allow.rules[TCP_LISTENER_CTX(work)->allow.count].network = *TcpListenerV.gate.addr;
    TCP_LISTENER_CTX(work)->allow.rules[TCP_LISTENER_CTX(work)->allow.count].prefix_len = TcpListenerV.gate.prefix_len;
    TCP_LISTENER_CTX(work)->allow.count++;
    TcpListenerV.ok = PROTO_TRUE;
}

void protocore_tcp_listener_ip_allow_add_cidr(uint8_t *restrict work)
{
    TcpListenerV.ok = PROTO_FALSE;
    if (!TcpListenerV.gate.cidr)
    {
        return;
    }

    // Split "address/prefix" at the slash. The address half is copied into a bounded
    // buffer (a CIDR string is never longer than an address plus "/128") for the parser.
    char addr[PROTOCORE_IP_STR_MAX];
    const char *slash = NULL;
    size_t n = 0;
    for (const char *p = TcpListenerV.gate.cidr; *p; p++)
    {
        if (*p == '/')
        {
            slash = p;
            break;
        }
        if (n + 1 >= sizeof(addr))
        {
            return; // address text too long to be valid
        }
        addr[n++] = *p;
    }
    addr[n] = '\0';

    protocore_ip net;
    net.family = PROTOCORE_IP_NONE;
    IpV.args.text = addr;
    IpV.args.out = &net;
    Ip.parse(ip_work);
    if (!IpV.ok)
    {
        return;
    }

    uint8_t width = (net.family == PROTOCORE_IP_V4) ? 32 : 128;
    uint8_t prefix = width; // bare address -> host route
    if (slash)
    {
        // Parse the decimal prefix by hand (no stdlib in src/); reject empty or non-digit.
        uint32_t v = 0;
        const char *p = slash + 1;
        if (!*p)
        {
            return;
        }
        for (; *p; p++)
        {
            if (*p < '0' || *p > '9')
            {
                return;
            }
            v = v * 10 + (uint32_t)(*p - '0');
            if (v > width)
            {
                return; // out of range for the family
            }
        }
        prefix = (uint8_t)v;
    }

    TcpListenerV.gate.addr = &net;
    TcpListenerV.gate.prefix_len = prefix;
    protocore_tcp_listener_ip_allow_add(work);
}

void protocore_tcp_listener_ip_allowed(uint8_t *restrict work)
{
    if (TCP_LISTENER_CTX(work)->allow.count == 0)
    {
        TcpListenerV.ok = PROTO_TRUE; // no rules configured -> allow all (fail-open by design)
        return;
    }
    for (uint8_t i = 0; i < TCP_LISTENER_CTX(work)->allow.count; i++)
    {
        // protocore_ip_prefix_match requires the same family, so a v4 peer never matches a v6 rule.
        IpV.args.ip = TcpListenerV.gate.addr;
        IpV.args.b = &TCP_LISTENER_CTX(work)->allow.rules[i].network;
        IpV.args.prefix_len = TCP_LISTENER_CTX(work)->allow.rules[i].prefix_len;
        Ip.prefix_match(ip_work);
        if (IpV.ok)
        {
            TcpListenerV.ok = PROTO_TRUE;
            return;
        }
    }
    TcpListenerV.ok = PROTO_FALSE;
}

void protocore_tcp_listener_ip_allowlist_reset(uint8_t *restrict work)
{
    for (int i = 0; i < PROTOCORE_IP_ALLOWLIST_SLOTS; i++)
    {
        TCP_LISTENER_CTX(work)->allow.rules[i].network.family = PROTOCORE_IP_NONE;
    }
    TCP_LISTENER_CTX(work)->allow.count = 0;
}

#if PROTOCORE_WORKER_COUNT > 1

void protocore_tcp_listener_worker_queues_init(uint8_t *restrict work)
{
    for (int i = 0; i < PROTOCORE_WORKER_COUNT; i++)
    {
        if (!TCP_LISTENER_CTX(work)->lq.wq[i])
        {
            TCP_LISTENER_CTX(work)->lq.wq[i] = protocore_platform_queue_create(
                EVT_QUEUE_DEPTH, sizeof(TcpEvt), TCP_LISTENER_CTX(work)->lq.wq_storage[i],
                &TCP_LISTENER_CTX(work)->lq.wq_struct[i]);
        }
    }
}

void protocore_tcp_listener_worker_queue(uint8_t *restrict work)
{
    if (TcpListenerV.q.worker_id < 0 || TcpListenerV.q.worker_id >= PROTOCORE_WORKER_COUNT)
    {
        TcpListenerV.queue = NULL;
        return;
    }
    TcpListenerV.queue = TCP_LISTENER_CTX(work)->lq.wq[TcpListenerV.q.worker_id];
}
#else
// The queue listener ns->idx drains, NULL when the row is inactive or out of range. One worker owns
// every slot here, so the listener's own queue is the only path an event takes.
void protocore_tcp_listener_listener_queue(uint8_t *restrict work)
{
    (void)work;
    TcpListenerV.queue = NULL;
    if (TcpListenerV.idx >= MAX_LISTENERS)
    {
        return;
    }
    Listener *lst = &listener_pool[TcpListenerV.idx];
    if (!lst->active)
    {
        return;
    }
    TcpListenerV.queue = lst->queue;
}
#endif // PROTOCORE_WORKER_COUNT > 1

void protocore_tcp_listener_enqueue(uint8_t *restrict work)
{
    TcpListenerV.ok = PROTO_FALSE;
    if (TcpListenerV.q.evt == NULL || TcpListenerV.q.evt->slot_id >= CONN_POOL_SLOTS)
    {
        return;
    }
#if PROTOCORE_WORKER_COUNT > 1
    // HttpRoute by the slot's owner so the owning worker is the sole consumer.
    uint8_t owner = conn_pool[TcpListenerV.q.evt->slot_id].owner;
    if (owner >= PROTOCORE_WORKER_COUNT || !TCP_LISTENER_CTX(work)->lq.wq[owner])
    {
        return;
    }
    if (protocore_platform_queue_send(TCP_LISTENER_CTX(work)->lq.wq[owner], TcpListenerV.q.evt, 0) !=
        PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    WorkersV.worker_id = owner; // nudge the owning worker so it services this now
    Workers.wake(protocore_worker_span());
#else
    if (TcpListenerV.idx >= MAX_LISTENERS)
    {
        return;
    }
    Listener *lst = &listener_pool[TcpListenerV.idx];
    if (!lst->active || !lst->queue)
    {
        return;
    }
    if (protocore_platform_queue_send(lst->queue, TcpListenerV.q.evt, 0) != PROTOCORE_PLATFORM_OK)
    {
        return;
    }
    WorkersV.worker_id = 0; // single worker owns every slot - nudge it now
    Workers.wake(protocore_worker_span());
#endif
    TcpListenerV.ok = PROTO_TRUE;
}

/**
 * @brief Accept callback from the lower-level module - single handler for all listener ports.
 *
 * @p arg carries the listener index cast to a pointer via
 * `protocore_net_arg(listen_pcb, (void*)(uintptr_t)idx)`.  Finds a free TcpConn slot,
 * sets its protocol, wires the per-connection callbacks, and posts EVT_CONNECT
 * to the owning listener's queue.  Rejects the connection with PROTOCORE_NET_ERR_ABRT when
 * the pool is full - PROTOCORE_NET_ERR_ABRT tells the stack the control block is already gone from our side.
 *
 * Non-static (like tcp.c's lowlevel_*_cb) so the host unit tests can call it
 * directly with a fabricated newpcb: on native there is no real accept event
 * to drive it through protocore_net_on_accept(), whose mock does not store or invoke the
 * registered callback at all.
 */
protocore_net_err listener_accept_cb(void *arg, protocore_pcb *newpcb, protocore_net_err err)
{
    if (err != PROTOCORE_NET_OK || newpcb == NULL)
    {
        return PROTOCORE_NET_ERR_VAL;
    }

    uint8_t idx = (uint8_t)(uintptr_t)arg;
    if (idx >= MAX_LISTENERS)
    {
        return PROTOCORE_NET_ERR_VAL;
    }
    Listener *lst = &listener_pool[idx];

#if PROTOCORE_ENABLE_ACCEPT_THROTTLE
    // Connection-flood defense: drop accepts beyond the per-window budget before
    // claiming a pool slot or doing any per-connection work.
    TcpListenerV.gate.now_ms = Clock.ms;
    protocore_tcp_listener_accept_allowed(protocore_tcp_listener_span());
    if (!TcpListenerV.ok)
    {
        protocore_net_abort(newpcb);
        return PROTOCORE_NET_ERR_ABRT;
    }
#endif

#if PROTOCORE_ENABLE_PER_IP_THROTTLE || PROTOCORE_ENABLE_IP_ALLOWLIST
    // Resolve the peer's family-tagged source address once for the accept-time abuse gates
    // below - the full IPv4 or IPv6 address, never a lossy hash.
    protocore_ip remote;
    remote.family = PROTOCORE_IP_NONE;
    protocore_net_addr_to_ip(&newpcb->remote_ip, &remote);
#endif

#if PROTOCORE_ENABLE_PER_IP_THROTTLE
    // Per-source-IP flood defense: drop accepts beyond one address's per-window
    // budget (the global throttle cannot tell one noisy client from many). Keyed on
    // the full address, so an IPv6 peer cannot spray a /64 past a per-address cap.
    //
    // The reject branch below is unreachable THROUGH THIS CALL SITE on native: `remote`
    // is hardcoded to PROTOCORE_IP_NONE just above (no real control block to read an
    // address from), and listener_accept_allowed_ip()'s very first check
    // (protocore_ip_is_unspecified) always allows an unspecified address through, deferring to
    // the global throttle - see test_accept_gate.cpp's test_per_ip_unspecified_defers.
    // The function's own reject path IS fully host-tested directly with a synthetic
    // protocore_ip (test_per_ip_independent_budgets et al.); only ITS USE HERE, gated behind a
    // peer address this host build can never resolve, cannot be driven to the false case.
    TcpListenerV.gate.addr = &remote;
    TcpListenerV.gate.now_ms = Clock.ms;
    protocore_tcp_listener_accept_allowed_ip(protocore_tcp_listener_span());
    if (!TcpListenerV.ok)
    {
        protocore_net_abort(newpcb);
        return PROTOCORE_NET_ERR_ABRT;
    }
#endif

#if PROTOCORE_ENABLE_IP_ALLOWLIST
    // Source-IP firewall: drop connections from addresses outside the configured
    // allowlist (an empty allowlist allows all, so this is a no-op until rules are
    // added). CIDR prefix match on the full v4/v6 address.
    TcpListenerV.gate.addr = &remote;
    protocore_tcp_listener_ip_allowed(protocore_tcp_listener_span());
    if (!TcpListenerV.ok)
    {
        protocore_net_abort(newpcb);
        return PROTOCORE_NET_ERR_ABRT;
    }
#endif

    // First free slot as one ctz on the live-slot bitmask (was a MAX_CONNS scan). Runs in stack context, and
    // accepts are serialized here, so the slot found is claimed by the protocore_conn_set_state() below before any
    // other accept runs.
    ConnPool.alloc_free(protocore_conn_pool_span());
    int32_t free_slot = ConnPoolV.i32;
    if (free_slot < 0)
    {
        protocore_net_abort(newpcb);
        return PROTOCORE_NET_ERR_ABRT;
    }

    TcpConn *slot = &conn_pool[free_slot];
#if PROTOCORE_WORKER_COUNT > 1
    // Round-robin the new connection across workers. Runs only in stack context,
    // so the counter needs no lock. Set BEFORE the state release store so a worker
    // that observes CONN_ACTIVE also sees the owner, and so the EVT_CONNECT below
    // routes to the owner's queue.
    static uint8_t s_next_owner = 0;
    slot->owner = s_next_owner;
    s_next_owner = (uint8_t)((s_next_owner + 1) % PROTOCORE_WORKER_COUNT);
#else
    slot->owner = 0;
#endif
    ConnPoolV.slot = (uint8_t)free_slot;
    ConnPoolV.st = CONN_ACTIVE;
    ConnPool.set_state(protocore_conn_pool_span()); // reserves the slot in the bitmask
    slot->pcb = newpcb;
    slot->last_activity_ms = Clock.ms;
    slot->rx_head = 0;
    slot->rx_tail = 0;
    slot->rx_acked = 0; // window-ack cursor starts level with an empty ring
    slot->listener_id = idx;
    slot->proto = lst->proto;

    // Tag the ingress interface for per-route STA/AP filtering: the connection's local IP against
    // the configured softAP IP.
    {
        uint32_t lip = protocore_net_ip4_u32(protocore_net_ip_as_v4(&newpcb->local_ip));
        slot->iface = PROTOCORE_IF_WIFI_STA;
        if (protocore_ap_ip != 0 && lip == protocore_ap_ip)
        {
            slot->iface = PROTOCORE_IF_WIFI_AP;
        }
    }

    protocore_net_arg(newpcb, slot);

#if PROTOCORE_TCP_NODELAY
    // Latency-first: disable Nagle so the final sub-MSS segment of a response (or a streamed chunk) is not held
    // waiting for the peer's ACK of the prior segment (a ~40-200 ms delayed-ACK stall). Runs in stack context
    // (accept callback), so touching the pcb here is safe. See PROTOCORE_TCP_NODELAY.
    protocore_net_nagle_disable(newpcb);
#endif

    // RFC 9293 sec 3.9.2 MUST-49: the TTL segments go out with is configurable, and it is stamped
    // here - before the connection carries anything - so every segment of it leaves with the same
    // one. Same context as protocore_net_nagle_disable above, so touching the pcb is race-free.
    TcpLowerV.pcb = newpcb;
    TcpLower.apply_ttl(protocore_tcp_lower_span());

#if PROTOCORE_ENABLE_DIFFSERV
    // DiffServ (RFC 2474): stamp this connection's DS field so a QoS-aware network - and the Wi-Fi WMM
    // mapping - prioritizes it. The per-listener DSCP wins over the server-wide default; 0 means best-effort
    // (leave the stack's default of 0). Set here and not again: RFC 9293 sec 3.9.2 SHLD-23 says an
    // application should not change the Diffserv field during a connection, so a port's code point is
    // the granularity, applied to what it accepts.
    {
        uint8_t dscp = (lst->dscp != PROTOCORE_DSCP_UNSET) ? lst->dscp : protocore_diffserv_default_dscp();
        if (dscp)
        {
            newpcb->tos = protocore_dscp_to_tos(dscp);
        }
    }
#endif

#if PROTOCORE_ENABLE_TLS
    // TLS listeners begin a handshake immediately; the session loop pumps it.
    slot->tls = lst->tls ? 1 : 0;
    if (lst->tls)
    {
        protocore_tls_conn_begin(free_slot);
    }
#else
    slot->tls = 0;
#endif
    protocore_net_on_recv(newpcb, lowlevel_recv_cb);
    protocore_net_on_sent(newpcb, lowlevel_sent_cb);
    protocore_net_on_err(newpcb, lowlevel_err_cb);

    PROTOCORE_OBS_TRANSITION((uint8_t)free_slot, CONN_FREE, CONN_ACTIVE, PROTOCORE_CONN_R_ACCEPT);

    TcpEvt evt = {EVT_CONNECT, (uint8_t)free_slot, 0};
    TcpListenerV.idx = idx;
    TcpListenerV.q.evt = &evt;
    protocore_tcp_listener_enqueue(protocore_tcp_listener_span());
    if (!TcpListenerV.ok)
    {
        PROTOCORE_OBS_NOTICE((uint8_t)free_slot, CONN_ACTIVE, PROTOCORE_CONN_R_DEFER_DROP);
    }

    return PROTOCORE_NET_OK;
}

static protocore_net_err listener_pcb_marshal(uint8_t idx, uint16_t port, proto_bool create);

void protocore_tcp_listener_add(uint8_t *restrict work)
{
    if (TcpListenerV.idx >= MAX_LISTENERS)
    {
        TcpListenerV.i32 = -1;
        return;
    }

    protocore_tcp_listener_stop(work); // clean up if already active

#if PROTOCORE_WORKER_COUNT > 1
    protocore_tcp_listener_worker_queues_init(work); // create the per-worker event queues once (idempotent)
#endif

    Listener *lst = &listener_pool[TcpListenerV.idx];
    lst->port = TcpListenerV.bind.port;
    lst->proto = TcpListenerV.bind.proto;
    lst->tls = TcpListenerV.bind.tls;
    // no per-listener override until TcpListener.set_dscp; accept() uses the server-wide default
    lst->dscp = PROTOCORE_DSCP_UNSET;

#if PROTOCORE_WORKER_COUNT == 1
    lst->queue =
        protocore_platform_queue_create(EVT_QUEUE_DEPTH, sizeof(TcpEvt), lst->_queue_storage, &lst->_queue_struct);
    if (!lst->queue)
    {
        TcpListenerV.i32 = -1;
        return;
    }
#endif

    // Create the listening control block in stack context. A raw new/bind/listen from the app or
    // worker task that calls begin() trips the stack's core-locking assert, so marshal it -
    // the same path the dynamic listener uses. Fields the accept callback reads (proto, queue) are
    // set above, before the pcb can accept.
    if (listener_pcb_marshal(TcpListenerV.idx, TcpListenerV.bind.port, PROTO_TRUE) != PROTOCORE_NET_OK)
    {
#if PROTOCORE_WORKER_COUNT == 1
        protocore_platform_queue_delete(lst->queue);
        lst->queue = NULL;
#endif
        TcpListenerV.i32 = -1;
        return;
    }
    lst->active = PROTO_TRUE;

    TcpListenerV.i32 = 1;
}

void protocore_tcp_listener_stop(uint8_t *restrict work)
{
    (void)work;
    if (TcpListenerV.idx >= MAX_LISTENERS)
    {
        return;
    }
    Listener *lst = &listener_pool[TcpListenerV.idx];
    if (!lst->active)
    {
        return;
    }
    lst->active = PROTO_FALSE;
    (void)listener_pcb_marshal(TcpListenerV.idx, 0, PROTO_FALSE); // close the listen pcb in stack context
#if PROTOCORE_WORKER_COUNT == 1
    if (lst->queue)
    {
        protocore_platform_queue_delete(lst->queue);
        lst->queue = NULL;
    }
#endif
}

void protocore_tcp_listener_stop_all(uint8_t *restrict work)
{
    for (uint8_t i = 0; i < MAX_LISTENERS; i++)
    {
        TcpListenerV.idx = i;
        protocore_tcp_listener_stop(work);
    }
}

// ---------------------------------------------------------------------------
// Marshaled listener create / close. A raw new/bind/listen/close must run in stack context: a
// call from any other task trips the core-locking assert, and without that locking it races the
// stack. Both listener_add/stop (from begin()) and the dynamic listeners (SSH `ssh -R`
// remote-forward, opened from a worker task) route through here via protocore_net_call_marshal().
// ---------------------------------------------------------------------------

typedef struct
{
    protocore_net_call base;
    uint8_t idx;
    uint16_t port;
    proto_bool create; // true = new+bind+listen+accept, false = close the listen pcb
    protocore_net_err result;
} protocore_listener_call;

// Runs in stack context. Creates or closes the listening PCB for listener_pool[idx].
static protocore_net_err listener_pcb_do(protocore_net_call *c)
{
    protocore_listener_call *k = (protocore_listener_call *)c;
    Listener *lst = &listener_pool[k->idx];
    k->result = PROTOCORE_NET_OK;
    if (k->create)
    {
        protocore_pcb *pcb = protocore_net_new(PROTOCORE_NET_TYPE_ANY);
        if (!pcb)
        {
            k->result = PROTOCORE_NET_ERR_MEM;
            return PROTOCORE_NET_OK;
        }
        if (protocore_net_bind(pcb, PROTOCORE_NET_ADDR_ANY, k->port) != PROTOCORE_NET_OK)
        {
            protocore_net_abort(pcb);
            k->result = PROTOCORE_NET_ERR_USE; // port already bound
            return PROTOCORE_NET_OK;
        }
        protocore_pcb *lp = protocore_net_listen(pcb, MAX_CONNS);
        if (!lp)
        {
            protocore_net_abort(pcb); // tcp_listen did not consume pcb on failure
            k->result = PROTOCORE_NET_ERR_MEM;
            return PROTOCORE_NET_OK;
        }
        protocore_net_arg(lp, (void *)(uintptr_t)k->idx);
        protocore_net_on_accept(lp, listener_accept_cb);
        lst->listen_pcb = lp;
    }
    else if (lst->listen_pcb)
    {
        protocore_net_close(lst->listen_pcb);
        lst->listen_pcb = NULL;
    }
    return PROTOCORE_NET_OK;
}

static protocore_net_err listener_pcb_marshal(uint8_t idx, uint16_t port, proto_bool create)
{
    protocore_listener_call k = {0};
    k.idx = idx;
    k.port = port;
    k.create = create;
    protocore_net_call_marshal(listener_pcb_do, &k.base);
    return k.result;
}

// Install the code point every connection accepted on a port takes. The store is unconditional:
// PROTOCORE_ENABLE_DIFFSERV decides whether the accept callback stamps it, not whether a caller can
// name it.
void protocore_tcp_listener_set_dscp(uint8_t *restrict work)
{
    (void)work;
    for (uint8_t i = 0; i < MAX_LISTENERS; i++)
    {
        Listener *lst = &listener_pool[i];
        if (lst->active && lst->port == TcpListenerV.bind.port)
        {
            // Preserve the UNSET sentinel; mask any real code point to 6 bits. Applied (via the accept
            // callback's newpcb->tos) to connections accepted after this call - existing connections keep the
            // DSCP they were stamped with. The handshake (SYN-ACK) stays best-effort: the stack emits it
            // before any app callback and does not inherit the listening block's DS field, so marking
            // begins at the connection's first data segment.
            lst->dscp = (TcpListenerV.bind.dscp == PROTOCORE_DSCP_UNSET) ? PROTOCORE_DSCP_UNSET
                                                                         : (uint8_t)(TcpListenerV.bind.dscp & 0x3F);
            TcpListenerV.ok = PROTO_TRUE;
            return;
        }
    }
    TcpListenerV.ok = PROTO_FALSE;
}

void protocore_tcp_listener_add_dynamic(uint8_t *restrict work)
{
    if (TcpListenerV.idx >= MAX_LISTENERS)
    {
        TcpListenerV.i32 = -1;
        return;
    }
    protocore_tcp_listener_stop_dynamic(work); // clean up if this slot was already active

#if PROTOCORE_WORKER_COUNT > 1
    protocore_tcp_listener_worker_queues_init(work); // idempotent (queue creation is task-safe)
#endif

    Listener *lst = &listener_pool[TcpListenerV.idx];
    lst->port = TcpListenerV.bind.port;
    lst->proto = TcpListenerV.bind.proto;
    lst->tls = PROTO_FALSE;           // forwarded ports are plaintext bridges
    lst->dscp = PROTOCORE_DSCP_UNSET; // dynamic (forwarded) listeners inherit the server-wide default DSCP

#if PROTOCORE_WORKER_COUNT == 1
    lst->queue =
        protocore_platform_queue_create(EVT_QUEUE_DEPTH, sizeof(TcpEvt), lst->_queue_storage, &lst->_queue_struct);
    if (!lst->queue)
    {
        TcpListenerV.i32 = -1;
        return;
    }
#endif

    // Create the listening PCB in stack context. Fields the accept callback reads
    // (proto, queue) are set above, before the pcb can accept anything.
    if (listener_pcb_marshal(TcpListenerV.idx, TcpListenerV.bind.port, PROTO_TRUE) != PROTOCORE_NET_OK)
    {
#if PROTOCORE_WORKER_COUNT == 1
        protocore_platform_queue_delete(lst->queue);
        lst->queue = NULL;
#endif
        TcpListenerV.i32 = -1;
        return;
    }

    lst->active = PROTO_TRUE;
    TcpListenerV.i32 = 1;
}

void protocore_tcp_listener_stop_dynamic(uint8_t *restrict work)
{
    (void)work;
    if (TcpListenerV.idx >= MAX_LISTENERS)
    {
        return;
    }
    Listener *lst = &listener_pool[TcpListenerV.idx];
    if (!lst->active)
    {
        return;
    }
    lst->active = PROTO_FALSE;
    (void)listener_pcb_marshal(TcpListenerV.idx, 0, PROTO_FALSE); // close the listen pcb in stack context
#if PROTOCORE_WORKER_COUNT == 1
    if (lst->queue)
    {
        protocore_platform_queue_delete(lst->queue);
        lst->queue = NULL;
    }
#endif
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
TcpListenerVars TcpListenerV;
