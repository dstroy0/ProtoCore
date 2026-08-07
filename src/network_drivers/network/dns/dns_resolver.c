// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file resolver.c
 * @brief IPv4 classifier / verifier (pure) + lwIP DNS resolve (ESP32).
 *
 * The resolve marshals dns_gethostbyname into tcpip_thread and polls a done flag
 * with a deadline - the same cross-thread pattern the http_client uses.
 */

#include "network_drivers/network/dns/dns_resolver.h"

#if PC_NEED_DNS_RESOLVER

#include "lwip/def.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/priv/tcpip_priv.h"
#include "server/clock/clock.h" // pc_millis() - the single pluggable monotonic source
static pc_ip_class classify(uint32_t ip)
{
    if (ip == 0u)
    {
        return PC_IP_UNSPECIFIED;
    }
    if (ip == 0xFFFFFFFFu)
    {
        return PC_IP_BROADCAST;
    }
    uint8_t a = (uint8_t)((ip >> 24) & 0xFF);
    uint8_t b = (uint8_t)((ip >> 16) & 0xFF);
    if (a == 127)
    {
        return PC_IP_LOOPBACK;
    }
    if (a == 10)
    {
        return PC_IP_PRIVATE;
    }
    if (a == 172 && b >= 16 && b <= 31)
    {
        return PC_IP_PRIVATE;
    }
    if (a == 192 && b == 168)
    {
        return PC_IP_PRIVATE;
    }
    if (a == 169 && b == 254)
    {
        return PC_IP_LINKLOCAL;
    }
    if (a >= 224 && a <= 239)
    {
        return PC_IP_MULTICAST;
    }
    return PC_IP_PUBLIC;
}

static proto_bool verify(uint32_t ip)
{
    switch (classify(ip))
    {
    case PC_IP_UNSPECIFIED: // 0.0.0.0 - blocked / no answer
    case PC_IP_BROADCAST:   // 255.255.255.255 - never a host
    case PC_IP_LOOPBACK:    // 127.x - DNS-rebinding to localhost
    case PC_IP_MULTICAST:   // 224-239 - never an A-record host
        return PROTO_FALSE;
    default:
        return PROTO_TRUE; // private / link-local / public are plausible
    }
}

// All DNS-resolve binding state, owned by one instance (internal linkage): the resolved
// address plus the done/ok flags the lwIP callback sets, grouped so it is one named owner,
// unreachable cross-TU. The flags are volatile: the callback runs on tcpip_thread while the
// resolve loop polls them.
typedef struct
{
    ip_addr_t addr;
    volatile proto_bool done;
    volatile proto_bool ok;
} DnsResolverCtx;
static DnsResolverCtx s_dr;

typedef struct
{
    struct tcpip_api_call_data base;
    const char *host;
} DnsCall;

static void dns_cb(const char *name, const ip_addr_t *addr, void *arg)
{
    (void)name;
    (void)arg;
    if (addr)
    {
        s_dr.addr = *addr;
        s_dr.ok = PROTO_TRUE;
    }
    s_dr.done = PROTO_TRUE;
}

static err_t do_dns(struct tcpip_api_call_data *c)
{
    const char *host = ((DnsCall *)c)->host;
    err_t e = dns_gethostbyname(host, &s_dr.addr, dns_cb, NULL);
    if (e == ERR_OK) // already cached
    {
        s_dr.ok = PROTO_TRUE;
        s_dr.done = PROTO_TRUE;
    }
    else if (e != ERR_INPROGRESS) // hard failure
    {
        s_dr.done = PROTO_TRUE;
    }
    return ERR_OK;
}

static uint32_t to_host_order(const ip_addr_t *a)
{
    return lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(a)));
}

static proto_bool resolve(const char *host, uint32_t *out_ip)
{
    if (!host || !out_ip)
    {
        return PROTO_FALSE;
    }

    ip_addr_t literal;
    if (ipaddr_aton(host, &literal)) // dotted-quad fast path, no DNS
    {
        *out_ip = to_host_order(&literal);
        return PROTO_TRUE;
    }

    s_dr.done = PROTO_FALSE;
    s_dr.ok = PROTO_FALSE;
    DnsCall k;
    memset(&k, 0, sizeof(k));
    k.host = host;
    tcpip_api_call(do_dns, &k.base); // resolve in the lwIP thread

    uint32_t deadline = pc_millis() + PC_DNS_TIMEOUT_MS;
    while (!s_dr.done && (int32_t)(deadline - pc_millis()) > 0)
    {
        pcdelay(5);
    }

    if (!s_dr.ok)
    {
        return PROTO_FALSE;
    }
    *out_ip = to_host_order(&s_dr.addr);
    return PROTO_TRUE;
}

static proto_bool resolve_verified(const char *host, uint32_t *out_ip)
{
    uint32_t ip = 0;
    if (!resolve(host, &ip))
    {
        return PROTO_FALSE;
    }
    if (!verify(ip))
    {
        return PROTO_FALSE;
    }
    if (out_ip)
    {
        *out_ip = ip;
    }
    return PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
const ResolverNs Resolver = {
    .classify = classify, .verify = verify, .resolve = resolve, .resolve_verified = resolve_verified};

#endif // PC_NEED_DNS_RESOLVER