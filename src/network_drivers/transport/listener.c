// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file listener.c
 * @brief Layer 4 (Listener) - TCP accept callback and port lifecycle.
 *
 * `listener_accept_cb` is the single lwIP accept callback registered for
 * every listener.  The listener index is embedded in the PCB user-data via
 * `pc_net_arg(listen_pcb, (void*)(uintptr_t)idx)` so this one function handles
 * all ports without a lookup table.
 *
 * The non-static per-connection callbacks (lowlevel_recv_cb, lowlevel_sent_cb,
 * lowlevel_err_cb) are defined in tcp.c and declared extern here.
 * The transport layer's enqueue() helper calls listener_enqueue(), which is
 * defined in this file - that indirection breaks the circular header dependency
 * (listener.h includes tcp.h; tcp.c includes listener.h).
 */

#include "listener.h"
#include "board_drivers/board_profiles/pc_platform.h" // the target's queues, under our names
#include "diffserv.h"                // DiffServ DSCP marking for accepted connections (compiles out when off)
#include "network_drivers/tls/tls.h" // TLS handshake begin (self-stubbing)
#if PROTOCORE_HOT
#include "network_drivers/session/worker.h" // pc_worker_wake() - nudge the owning worker task
#endif
#include "server/clock/clock.h" // pc_millis() pluggable monotonic clock (host-safe)

// Listener pool - all storage in BSS.
Listener listener_pool[MAX_LISTENERS];

// Per-connection callbacks defined in tcp.c.
extern pc_net_err lowlevel_recv_cb(void *arg, pc_pcb *tpcb, pc_pbuf *p, pc_net_err err);
extern pc_net_err lowlevel_sent_cb(void *arg, pc_pcb *tpcb, proto_u16 len);
extern void lowlevel_err_cb(void *arg, pc_net_err err);

// ---------------------------------------------------------------------------
// Accept-rate throttle (fixed window, global). State persists across accepts.
// Always compiled (unit-testable); only consulted when the feature is enabled.
// ---------------------------------------------------------------------------

// Global accept-rate-limit state, owned by one instance (internal linkage): the fixed-window
// start and the accept count in the current window. One named owner, unreachable cross-TU.
typedef struct
{
    uint32_t window_start;
    uint16_t count;
} AcceptThrottleCtx;
static AcceptThrottleCtx s_accept;

proto_bool listener_accept_allowed(uint32_t now_ms)
{
    // Unsigned subtraction wraps correctly across the millis() rollover.
    if ((uint32_t)(now_ms - s_accept.window_start) >= PC_ACCEPT_THROTTLE_WINDOW_MS)
    {
        s_accept.window_start = now_ms;
        s_accept.count = 0;
    }
    if (s_accept.count >= PC_ACCEPT_THROTTLE_MAX)
    {
        return PROTO_FALSE;
    }
    s_accept.count++;
    return PROTO_TRUE;
}

void listener_accept_throttle_reset(void)
{
    s_accept.window_start = 0;
    s_accept.count = 0;
}

// ---------------------------------------------------------------------------
// Per-IP accept-rate throttle (fixed window per source IPv4). A bounded BSS table
// of buckets - no heap. Always compiled (unit-testable); only consulted when the
// feature is enabled.
// ---------------------------------------------------------------------------

typedef struct
{
    pc_ip addr;            ///< source address (family PC_IP_NONE marks an empty bucket).
    uint32_t window_start; ///< millis() at the start of this bucket's current window.
    uint16_t count;        ///< connections counted from this address in the window.
} IpThrottleBucket;
// Per-source-IP accept-throttle state, owned by one instance (internal linkage): the bounded
// bucket table keyed by source address. One named owner, unreachable from any other TU.
typedef struct
{
    IpThrottleBucket buckets[PC_PER_IP_THROTTLE_SLOTS];
} IpThrottleCtx;
static IpThrottleCtx s_iptt;

proto_bool listener_accept_allowed_ip(const pc_ip *ip, uint32_t now_ms)
{
    if (pc_ip_is_unspecified(ip))
    {
        return PROTO_TRUE; // untrackable source - defer to the global accept throttle
    }

    int empty = -1;
    int expired = -1;
    int lru = 0;
    for (int i = 0; i < PC_PER_IP_THROTTLE_SLOTS; i++)
    {
        IpThrottleBucket *b = &s_iptt.buckets[i];
        if (b->addr.family != PC_IP_NONE && pc_ip_equal(&b->addr, ip))
        {
            // Unsigned subtraction wraps correctly across the millis() rollover.
            if ((uint32_t)(now_ms - b->window_start) >= PC_PER_IP_THROTTLE_WINDOW_MS)
            {
                b->window_start = now_ms;
                b->count = 0;
            }
            if (b->count >= PC_PER_IP_THROTTLE_MAX)
            {
                return PROTO_FALSE;
            }
            b->count++;
            return PROTO_TRUE;
        }
        if (b->addr.family == PC_IP_NONE)
        {
            if (empty < 0)
            {
                empty = i;
            }
        }
        else
        {
            if (expired < 0 && (uint32_t)(now_ms - b->window_start) >= PC_PER_IP_THROTTLE_WINDOW_MS)
            {
                expired = i;
            }
            // Track the oldest active bucket (largest elapsed) as the eviction victim.
            if ((uint32_t)(now_ms - b->window_start) > (uint32_t)(now_ms - s_iptt.buckets[lru].window_start))
            {
                lru = i;
            }
        }
    }

    // No bucket yet for this address: claim one - empty, else expired, else evict
    // the least-recently-started active bucket.
    int slot = (empty >= 0) ? empty : (expired >= 0) ? expired : lru;
    IpThrottleBucket *b = &s_iptt.buckets[slot];
    b->addr = *ip;
    b->window_start = now_ms;
    b->count = 1;
    return PROTO_TRUE; // first connection of a fresh window is always allowed
}

void listener_per_ip_throttle_reset(void)
{
    for (int i = 0; i < PC_PER_IP_THROTTLE_SLOTS; i++)
    {
        s_iptt.buckets[i].addr.family = PC_IP_NONE;
        s_iptt.buckets[i].window_start = 0;
        s_iptt.buckets[i].count = 0;
    }
}

// ---------------------------------------------------------------------------
// Source-IP allowlist (accept-time firewall). A bounded BSS table of CIDR rules
// in host byte order. Always compiled (unit-testable); only consulted when
// PC_ENABLE_IP_ALLOWLIST is set. An empty table allows everything so enabling
// the feature before adding rules cannot lock the device out.
// ---------------------------------------------------------------------------

typedef struct
{
    pc_ip network;      ///< network address (family PC_IP_V4 / V6; PC_IP_NONE marks unused).
    uint8_t prefix_len; ///< CIDR prefix length: 0..32 for v4, 0..128 for v6.
} IpAllowRule;
// IP allowlist state, owned by one instance (internal linkage): the CIDR rule table and its
// count (empty = allow all). One named owner, unreachable from any other translation unit.
typedef struct
{
    IpAllowRule rules[PC_IP_ALLOWLIST_SLOTS];
    uint8_t count;
} IpAllowCtx;
static IpAllowCtx s_allow;

proto_bool listener_ip_allow_add(const pc_ip *network, uint8_t prefix_len)
{
    if (!network)
    {
        return PROTO_FALSE;
    }
    int bits = (network->family == PC_IP_V4) ? 32 : (network->family == PC_IP_V6 ? 128 : -1);
    if (bits < 0 || prefix_len > (uint8_t)bits)
    {
        return PROTO_FALSE; // reject a malformed family or an over-long prefix
    }
    if (s_allow.count >= PC_IP_ALLOWLIST_SLOTS)
    {
        return PROTO_FALSE;
    }
    s_allow.rules[s_allow.count].network = *network;
    s_allow.rules[s_allow.count].prefix_len = prefix_len;
    s_allow.count++;
    return PROTO_TRUE;
}

proto_bool listener_ip_allow_add_cidr(const char *cidr)
{
    if (!cidr)
    {
        return PROTO_FALSE;
    }

    // Split "address/prefix" at the slash. The address half is copied into a bounded
    // buffer (a CIDR string is never longer than an address plus "/128") for the parser.
    char addr[PC_IP_STR_MAX];
    const char *slash = NULL;
    size_t n = 0;
    for (const char *p = cidr; *p; p++)
    {
        if (*p == '/')
        {
            slash = p;
            break;
        }
        if (n + 1 >= sizeof(addr))
        {
            return PROTO_FALSE; // address text too long to be valid
        }
        addr[n++] = *p;
    }
    addr[n] = '\0';

    pc_ip net;
    net.family = PC_IP_NONE;
    if (!pc_ip_parse(addr, &net))
    {
        return PROTO_FALSE;
    }

    uint8_t width = (net.family == PC_IP_V4) ? 32 : 128;
    uint8_t prefix = width; // bare address -> host route
    if (slash)
    {
        // Parse the decimal prefix by hand (no stdlib in src/); reject empty or non-digit.
        uint32_t v = 0;
        const char *p = slash + 1;
        if (!*p)
        {
            return PROTO_FALSE;
        }
        for (; *p; p++)
        {
            if (*p < '0' || *p > '9')
            {
                return PROTO_FALSE;
            }
            v = v * 10 + (uint32_t)(*p - '0');
            if (v > width)
            {
                return PROTO_FALSE; // out of range for the family
            }
        }
        prefix = (uint8_t)v;
    }

    return listener_ip_allow_add(&net, prefix);
}

proto_bool listener_ip_allowed(const pc_ip *ip)
{
    if (s_allow.count == 0)
    {
        return PROTO_TRUE; // no rules configured -> allow all (fail-open by design)
    }
    for (uint8_t i = 0; i < s_allow.count; i++)
    {
        // pc_ip_prefix_match requires the same family, so a v4 peer never matches a v6 rule.
        if (pc_ip_prefix_match(ip, &s_allow.rules[i].network, s_allow.rules[i].prefix_len))
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

void listener_ip_allowlist_reset(void)
{
    for (int i = 0; i < PC_IP_ALLOWLIST_SLOTS; i++)
    {
        s_allow.rules[i].network.family = PC_IP_NONE;
    }
    s_allow.count = 0;
}

#if PC_WORKER_COUNT > 1
// Per-worker event queues: each worker drains only its own queue, so connection
// slots partition across workers with no shared-queue contention. Static BSS, no
// heap. Created once (idempotent) before the first accept can fire.
// Per-worker event-queue state, owned by one instance (internal linkage): the static queue
// control blocks, their storage, and the queue handles. One named owner, unreachable cross-TU.
typedef struct
{
    pc_platform_queue_ctrl wq_struct[PC_WORKER_COUNT];
    uint8_t wq_storage[PC_WORKER_COUNT][EVT_QUEUE_DEPTH * sizeof(TcpEvt)];
    pc_platform_queue wq[PC_WORKER_COUNT];
} ListenerQueueCtx;
static ListenerQueueCtx s_lq;

void listener_worker_queues_init(void)
{
    for (int i = 0; i < PC_WORKER_COUNT; i++)
    {
        if (!s_lq.wq[i])
        {
            s_lq.wq[i] =
                pc_platform_queue_create(EVT_QUEUE_DEPTH, sizeof(TcpEvt), s_lq.wq_storage[i], &s_lq.wq_struct[i]);
        }
    }
}

pc_platform_queue listener_worker_queue(int worker_id)
{
    if (worker_id < 0 || worker_id >= PC_WORKER_COUNT)
    {
        return NULL;
    }
    return s_lq.wq[worker_id];
}
#endif // PC_WORKER_COUNT > 1

proto_bool listener_enqueue(uint8_t listener_id, const TcpEvt *evt)
{
#if PC_WORKER_COUNT > 1
    // Route by the slot's owner so the owning worker is the sole consumer.
    (void)listener_id;
    uint8_t owner = conn_pool[evt->slot_id].owner;
    if (owner >= PC_WORKER_COUNT || !s_lq.wq[owner])
    {
        return PROTO_FALSE;
    }
    if (pc_platform_queue_send(s_lq.wq[owner], evt, 0) != PC_PLATFORM_OK)
    {
        return PROTO_FALSE;
    }
#if PROTOCORE_HOT
    pc_worker_wake(owner); // nudge the owning worker so it services this now
#endif
#else
    if (listener_id >= MAX_LISTENERS)
    {
        return PROTO_FALSE;
    }
    Listener *lst = &listener_pool[listener_id];
    if (!lst->active || !lst->queue)
    {
        return PROTO_FALSE;
    }
    if (pc_platform_queue_send(lst->queue, evt, 0) != PC_PLATFORM_OK)
    {
        return PROTO_FALSE;
    }
#if PROTOCORE_HOT
    pc_worker_wake(0); // single worker owns every slot - nudge it now
#endif
#endif
    return PROTO_TRUE;
}

/**
 * @brief lwIP accept callback - single handler for all listener ports.
 *
 * @p arg carries the listener index cast to a pointer via
 * `pc_net_arg(listen_pcb, (void*)(uintptr_t)idx)`.  Finds a free TcpConn slot,
 * sets its protocol, wires the per-connection callbacks, and posts EVT_CONNECT
 * to the owning listener's queue.  Rejects the connection with PC_NET_ERR_ABRT when
 * the pool is full - PC_NET_ERR_ABRT tells lwIP the PCB is already gone from our side.
 *
 * Non-static (like tcp.c's lowlevel_*_cb) so the host unit tests can call it
 * directly with a fabricated newpcb: on native there is no real lwIP accept event
 * to drive it through pc_net_on_accept(), whose mock (test/mocks/lwip/tcp.h) does not
 * store or invoke the registered callback at all.
 */
pc_net_err listener_accept_cb(void *arg, pc_pcb *newpcb, pc_net_err err)
{
    if (err != PC_NET_OK || newpcb == NULL)
    {
        return PC_NET_ERR_VAL;
    }

    uint8_t idx = (uint8_t)(uintptr_t)arg;
    if (idx >= MAX_LISTENERS)
    {
        return PC_NET_ERR_VAL;
    }
    Listener *lst = &listener_pool[idx];

#if PC_ENABLE_ACCEPT_THROTTLE
    // Connection-flood defense: drop accepts beyond the per-window budget before
    // claiming a pool slot or doing any per-connection work.
    if (!listener_accept_allowed(pc_millis()))
    {
        pc_net_abort(newpcb);
        return PC_NET_ERR_ABRT;
    }
#endif

#if PC_ENABLE_PER_IP_THROTTLE || PC_ENABLE_IP_ALLOWLIST
    // Resolve the peer's family-tagged source address once for the accept-time abuse
    // gates below - the full IPv4 or IPv6 address, never a lossy hash. On native
    // there is no real lwIP pcb, so it stays unspecified and the gates pass it
    // through; the host unit tests drive those gates directly with synthetic pc_ip.
    pc_ip remote;
    remote.family = PC_IP_NONE;
#if PROTOCORE_HOT
    pc_lwip_to_ip(&newpcb->remote_ip, &remote);
#endif
#endif

#if PC_ENABLE_PER_IP_THROTTLE
    // Per-source-IP flood defense: drop accepts beyond one address's per-window
    // budget (the global throttle cannot tell one noisy client from many). Keyed on
    // the full address, so an IPv6 peer cannot spray a /64 past a per-address cap.
    //
    // The reject branch below is unreachable THROUGH THIS CALL SITE on native: `remote`
    // is hardcoded to PC_IP_NONE just above (no real lwIP pcb to read an
    // address from), and listener_accept_allowed_ip()'s very first check
    // (pc_ip_is_unspecified) always allows an unspecified address through, deferring to
    // the global throttle - see test_accept_gate.cpp's test_per_ip_unspecified_defers.
    // The function's own reject path IS fully host-tested directly with a synthetic
    // pc_ip (test_per_ip_independent_budgets et al.); only ITS USE HERE, gated behind a
    // peer address this host build can never resolve, cannot be driven to the false case.
    if (!listener_accept_allowed_ip(&remote, pc_millis())) // GCOVR_EXCL_BR_LINE - see above
    {
        // GCOVR_EXCL_START - unreachable: see the comment above this `if`
        pc_net_abort(newpcb);
        return PC_NET_ERR_ABRT;
        // GCOVR_EXCL_STOP
    }
#endif

#if PC_ENABLE_IP_ALLOWLIST
    // Source-IP firewall: drop connections from addresses outside the configured
    // allowlist (an empty allowlist allows all, so this is a no-op until rules are
    // added). CIDR prefix match on the full v4/v6 address.
    if (!listener_ip_allowed(&remote))
    {
        pc_net_abort(newpcb);
        return PC_NET_ERR_ABRT;
    }
#endif

    // First free slot as one ctz on the live-slot bitmask (was a MAX_CONNS scan). Runs in tcpip_thread, and
    // accepts are serialized here, so the slot found is claimed by the pc_conn_set_state() below before any
    // other accept runs.
    int32_t free_slot = pc_conn_alloc_free();
    if (free_slot < 0)
    {
        pc_net_abort(newpcb);
        return PC_NET_ERR_ABRT;
    }

    TcpConn *slot = &conn_pool[free_slot];
#if PC_WORKER_COUNT > 1
    // Round-robin the new connection across workers. Runs only in tcpip_thread,
    // so the counter needs no lock. Set BEFORE the state release store so a worker
    // that observes CONN_ACTIVE also sees the owner, and so the EVT_CONNECT below
    // routes to the owner's queue.
    static uint8_t s_next_owner = 0;
    slot->owner = s_next_owner;
    s_next_owner = (uint8_t)((s_next_owner + 1) % PC_WORKER_COUNT);
#else
    slot->owner = 0;
#endif
    pc_conn_set_state((uint8_t)free_slot, CONN_ACTIVE); // reserves the slot in the bitmask
    slot->pcb = newpcb;
    slot->last_activity_ms = pc_millis();
    slot->req_start_ms = 0; // no request yet; armed on the first RX byte (request-completion deadline)
    slot->rx_head = 0;
    slot->rx_tail = 0;
    slot->rx_acked = 0; // window-ack cursor starts level with an empty ring
    slot->listener_id = idx;
    slot->proto = lst->proto;

    // Tag the ingress interface for per-route STA/AP filtering. On ESP32 compare
    // the connection's local IP to the configured softAP IP; on native (no real
    // pcb IP) leave it unclassified for tests to set directly.
#if PROTOCORE_HOT
    {
        uint32_t lip = pc_net_ip4_u32(pc_net_ip_as_v4(&newpcb->local_ip));
        slot->iface = (pc_ap_ip != 0 && lip == pc_ap_ip) ? PC_IFACE_AP : PC_IFACE_STA;
    }
#else
    slot->iface = PC_IFACE_ANY;
#endif

    pc_net_arg(newpcb, slot);

#if PC_TCP_NODELAY
    // Latency-first: disable Nagle so the final sub-MSS segment of a response (or a streamed chunk) is not held
    // waiting for the peer's ACK of the prior segment (a ~40-200 ms delayed-ACK stall). Runs in tcpip_thread
    // (accept callback), so touching the pcb here is safe. See PC_TCP_NODELAY.
    pc_net_nagle_disable(newpcb);
#endif

#if PC_ENABLE_DIFFSERV
    // DiffServ (RFC 2474): stamp this connection's DS field so a QoS-aware network - and the Wi-Fi WMM
    // mapping - prioritizes it. The per-listener DSCP wins over the server-wide default; 0 means best-effort
    // (leave the lwIP default of 0). Safe here: the accept callback runs in tcpip_thread, so touching the
    // pcb is race-free (same context as pc_net_nagle_disable above).
    {
        uint8_t dscp = (lst->dscp != PC_DSCP_UNSET) ? lst->dscp : pc_diffserv_default_dscp();
        if (dscp)
        {
            newpcb->tos = pc_dscp_to_tos(dscp);
        }
    }
#endif

#if PC_ENABLE_TLS
    // TLS listeners begin a handshake immediately; the session loop pumps it.
    slot->tls = lst->tls ? 1 : 0;
    if (lst->tls)
    {
        pc_tls_conn_begin(free_slot);
    }
#else
    slot->tls = 0;
#endif
    pc_net_on_recv(newpcb, lowlevel_recv_cb);
    pc_net_on_sent(newpcb, lowlevel_sent_cb);
    pc_net_on_err(newpcb, lowlevel_err_cb);

    PC_OBS_TRANSITION((uint8_t)free_slot, CONN_FREE, CONN_ACTIVE, PC_CONN_R_ACCEPT);

    TcpEvt evt = {EVT_CONNECT, (uint8_t)free_slot, 0};
    if (!listener_enqueue(idx, &evt))
    {
        PC_OBS_NOTICE((uint8_t)free_slot, CONN_ACTIVE, PC_CONN_R_DEFER_DROP);
    }

    return PC_NET_OK;
}

#if PROTOCORE_HOT
static pc_net_err listener_lwip_marshal(uint8_t idx, uint16_t port, proto_bool create);
#endif

int32_t listener_add(uint8_t idx, uint16_t port, ConnProto proto, proto_bool tls)
{
    if (idx >= MAX_LISTENERS)
    {
        return -1;
    }

    listener_stop(idx); // clean up if already active

#if PC_WORKER_COUNT > 1
    listener_worker_queues_init(); // create the per-worker event queues once (idempotent)
#endif

    Listener *lst = &listener_pool[idx];
    lst->port = port;
    lst->proto = proto;
    lst->tls = tls;
#if PC_ENABLE_DIFFSERV
    lst->dscp = PC_DSCP_UNSET; // no per-listener override until pc_listen_set_dscp(); accept() uses the default
#endif

    lst->queue = pc_platform_queue_create(EVT_QUEUE_DEPTH, sizeof(TcpEvt), lst->_queue_storage, &lst->_queue_struct);
    if (!lst->queue)
    {
        return -1;
    }

#if PROTOCORE_HOT
    // Create the listening PCB in tcpip_thread. With lwIP core-locking (arduino-esp32
    // 3.x / IDF 5.x) a raw tcp_new/bind/listen from the app or worker task that calls
    // begin() asserts ("Required to lock TCPIP core functionality"), so marshal it -
    // the same path the dynamic listener uses. Fields the accept callback reads (proto,
    // queue) are set above, before the pcb can accept.
    if (listener_lwip_marshal(idx, port, PROTO_TRUE) != PC_NET_OK)
    {
        pc_platform_queue_delete(lst->queue);
        lst->queue = NULL;
        return -1;
    }
#else
    pc_pcb *pcb = pc_net_new(PC_NET_TYPE_ANY);
    if (!pcb)
    {
        return -1;
    }

    pc_net_err bind_err = pc_net_bind(pcb, PC_NET_ADDR_ANY, port);
    if (bind_err != PC_NET_OK)
    {
        pc_net_abort(pcb);
        return -1;
    }

    lst->listen_pcb = pc_net_listen(pcb, MAX_CONNS);
    if (!lst->listen_pcb)
    {
        pc_net_abort(pcb);
        return -1;
    }

    pc_net_arg(lst->listen_pcb, (void *)(uintptr_t)idx);
    pc_net_on_accept(lst->listen_pcb, listener_accept_cb);
#endif
    lst->active = PROTO_TRUE;

    return 1;
}

void listener_stop(uint8_t idx)
{
    if (idx >= MAX_LISTENERS)
    {
        return;
    }
    Listener *lst = &listener_pool[idx];
    if (!lst->active)
    {
        return;
    }
    lst->active = PROTO_FALSE;
#if PROTOCORE_HOT
    listener_lwip_marshal(idx, 0, PROTO_FALSE); // close the listen pcb in tcpip_thread
#else
    lst->listen_pcb = NULL; // host build: no real pcb to close (matches listener_stop_dynamic)
#endif
    if (lst->queue)
    {
        pc_platform_queue_delete(lst->queue);
        lst->queue = NULL;
    }
}

void listener_stop_all(void)
{
    for (uint8_t i = 0; i < MAX_LISTENERS; i++)
    {
        listener_stop(i);
    }
}

// ---------------------------------------------------------------------------
// tcpip_thread-marshaled listener create / close. Raw lwIP tcp_new/bind/listen/close
// must run in tcpip_thread: with lwIP core-locking (arduino-esp32 3.x / IDF 5.x) a
// call from any other task asserts, and without it a call off tcpip_thread races the
// stack. Both listener_add/stop (from begin()) and the dynamic listeners (SSH `ssh -R`
// remote-forward, opened from a worker task) route through here via pc_net_call_marshal().
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT
typedef struct
{
    pc_net_call base;
    uint8_t idx;
    uint16_t port;
    proto_bool create; // true = new+bind+listen+accept, false = close the listen pcb
    pc_net_err result;
} pc_listener_call;

// Runs in tcpip_thread. Creates or closes the listening PCB for listener_pool[idx].
static pc_net_err listener_lwip_do(pc_net_call *c)
{
    pc_listener_call *k = (pc_listener_call *)c;
    Listener *lst = &listener_pool[k->idx];
    k->result = PC_NET_OK;
    if (k->create)
    {
        pc_pcb *pcb = pc_net_new(PC_NET_TYPE_ANY);
        if (!pcb)
        {
            k->result = PC_NET_ERR_MEM;
            return PC_NET_OK;
        }
        if (pc_net_bind(pcb, PC_NET_ADDR_ANY, k->port) != PC_NET_OK)
        {
            pc_net_abort(pcb);
            k->result = PC_NET_ERR_USE; // port already bound
            return PC_NET_OK;
        }
        pc_pcb *lp = pc_net_listen(pcb, MAX_CONNS);
        if (!lp)
        {
            pc_net_abort(pcb); // tcp_listen did not consume pcb on failure
            k->result = PC_NET_ERR_MEM;
            return PC_NET_OK;
        }
        pc_net_arg(lp, (void *)(uintptr_t)k->idx);
        pc_net_on_accept(lp, listener_accept_cb);
        lst->listen_pcb = lp;
    }
    else if (lst->listen_pcb)
    {
        pc_net_close(lst->listen_pcb);
        lst->listen_pcb = NULL;
    }
    return PC_NET_OK;
}

static pc_net_err listener_lwip_marshal(uint8_t idx, uint16_t port, proto_bool create)
{
    pc_listener_call k = {0};
    k.idx = idx;
    k.port = port;
    k.create = create;
    pc_net_call_marshal(listener_lwip_do, &k.base);
    return k.result;
}
#endif // PROTOCORE_HOT

#if PC_ENABLE_DIFFSERV
proto_bool pc_listen_set_dscp(uint16_t port, uint8_t dscp)
{
    for (uint8_t i = 0; i < MAX_LISTENERS; i++)
    {
        Listener *lst = &listener_pool[i];
        if (lst->active && lst->port == port)
        {
            // Preserve the UNSET sentinel; mask any real code point to 6 bits. Applied (via the accept
            // callback's newpcb->tos) to connections accepted after this call - existing connections keep the
            // DSCP they were stamped with. The handshake (SYN-ACK) stays best-effort: it is emitted by lwIP
            // before any app callback and ESP32 lwIP does not inherit the listen-pcb TOS (HW-tested), so
            // marking begins at the connection's first data segment.
            lst->dscp = (dscp == PC_DSCP_UNSET) ? PC_DSCP_UNSET : (uint8_t)(dscp & 0x3F);
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}
#endif // PC_ENABLE_DIFFSERV

int32_t listener_add_dynamic(uint8_t idx, uint16_t port, ConnProto proto)
{
    if (idx >= MAX_LISTENERS)
    {
        return -1;
    }
    listener_stop_dynamic(idx); // clean up if this slot was already active

#if PC_WORKER_COUNT > 1
    listener_worker_queues_init(); // idempotent (queue creation is task-safe)
#endif

    Listener *lst = &listener_pool[idx];
    lst->port = port;
    lst->proto = proto;
    lst->tls = PROTO_FALSE; // forwarded ports are plaintext bridges
#if PC_ENABLE_DIFFSERV
    lst->dscp = PC_DSCP_UNSET; // dynamic (forwarded) listeners inherit the server-wide default DSCP
#endif

    lst->queue = pc_platform_queue_create(EVT_QUEUE_DEPTH, sizeof(TcpEvt), lst->_queue_storage, &lst->_queue_struct);
    if (!lst->queue)
    {
        return -1;
    }

#if PROTOCORE_HOT
    // Create the listening PCB in tcpip_thread. Fields the accept callback reads
    // (proto, queue) are set above, before the pcb can accept anything.
    if (listener_lwip_marshal(idx, port, PROTO_TRUE) != PC_NET_OK)
    {
        pc_platform_queue_delete(lst->queue);
        lst->queue = NULL;
        return -1;
    }
#else
    lst->listen_pcb = NULL; // native host: no lwIP, exercised via the accept-gate unit paths
#endif

    lst->active = PROTO_TRUE;
    return 1;
}

void listener_stop_dynamic(uint8_t idx)
{
    if (idx >= MAX_LISTENERS)
    {
        return;
    }
    Listener *lst = &listener_pool[idx];
    if (!lst->active)
    {
        return;
    }
    lst->active = PROTO_FALSE;
#if PROTOCORE_HOT
    listener_lwip_marshal(idx, 0, PROTO_FALSE); // close the listen pcb in tcpip_thread
#else
    lst->listen_pcb = NULL;
#endif
    if (lst->queue)
    {
        pc_platform_queue_delete(lst->queue);
        lst->queue = NULL;
    }
}
