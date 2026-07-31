// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dns_resolver.cpp
 * @brief IPv4 classifier / verifier (pure) + lwIP DNS resolve (ESP32).
 *
 * The resolve marshals dns_gethostbyname into tcpip_thread and polls a done flag
 * with a deadline - the same cross-thread pattern the http_client uses.
 */

#include "services/net/dns_resolver/dns_resolver.h"

#if PC_ENABLE_DNS_RESOLVER

#if defined(ARDUINO)
#include "lwip/def.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/priv/tcpip_priv.h"
#include "services/system/clock.h" // pc_millis() - the single pluggable monotonic source
#include <Arduino.h>
#endif
pc_ip_class pc_dns_resolver_classify(uint32_t ip)
{
    if (ip == 0u)
    {
        return pc_ip_class::PC_IP_UNSPECIFIED;
    }
    if (ip == 0xFFFFFFFFu)
    {
        return pc_ip_class::PC_IP_BROADCAST;
    }
    uint8_t a = (uint8_t)((ip >> 24) & 0xFF);
    uint8_t b = (uint8_t)((ip >> 16) & 0xFF);
    if (a == 127)
    {
        return pc_ip_class::PC_IP_LOOPBACK;
    }
    if (a == 10)
    {
        return pc_ip_class::PC_IP_PRIVATE;
    }
    if (a == 172 && b >= 16 && b <= 31)
    {
        return pc_ip_class::PC_IP_PRIVATE;
    }
    if (a == 192 && b == 168)
    {
        return pc_ip_class::PC_IP_PRIVATE;
    }
    if (a == 169 && b == 254)
    {
        return pc_ip_class::PC_IP_LINKLOCAL;
    }
    if (a >= 224 && a <= 239)
    {
        return pc_ip_class::PC_IP_MULTICAST;
    }
    return pc_ip_class::PC_IP_PUBLIC;
}

bool pc_dns_resolver_verify(uint32_t ip)
{
    switch (pc_dns_resolver_classify(ip))
    {
    case pc_ip_class::PC_IP_UNSPECIFIED: // 0.0.0.0 - blocked / no answer
    case pc_ip_class::PC_IP_BROADCAST:   // 255.255.255.255 - never a host
    case pc_ip_class::PC_IP_LOOPBACK:    // 127.x - DNS-rebinding to localhost
    case pc_ip_class::PC_IP_MULTICAST:   // 224-239 - never an A-record host
        return false;
    default:
        return true; // private / link-local / public are plausible
    }
}

#ifdef ARDUINO

namespace
{
// All DNS-resolve binding state, owned by one instance (internal linkage): the resolved
// address plus the done/ok flags the lwIP callback sets, grouped so it is one named owner,
// unreachable cross-TU. The flags are volatile: the callback runs on tcpip_thread while the
// resolve loop polls them.
struct DnsResolverCtx
{
    ip_addr_t addr;
    volatile bool done = false;
    volatile bool ok = false;
};
DnsResolverCtx s_dr;

struct DnsCall
{
    struct tcpip_api_call_data base;
    const char *host;
};

void dns_cb(const char *name, const ip_addr_t *addr, void *arg)
{
    (void)name;
    (void)arg;
    if (addr)
    {
        s_dr.addr = *addr;
        s_dr.ok = true;
    }
    s_dr.done = true;
}

err_t do_dns(struct tcpip_api_call_data *c)
{
    const char *host = ((DnsCall *)c)->host;
    err_t e = dns_gethostbyname(host, &s_dr.addr, dns_cb, nullptr);
    if (e == ERR_OK) // already cached
    {
        s_dr.ok = true;
        s_dr.done = true;
    }
    else if (e != ERR_INPROGRESS) // hard failure
    {
        s_dr.done = true;
    }
    return ERR_OK;
}

uint32_t to_host_order(const ip_addr_t *a)
{
    return lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(a)));
}
} // namespace

bool pc_dns_resolver_resolve(const char *host, uint32_t *out_ip)
{
    if (!host || !out_ip)
    {
        return false;
    }

    ip_addr_t literal;
    if (ipaddr_aton(host, &literal)) // dotted-quad fast path, no DNS
    {
        *out_ip = to_host_order(&literal);
        return true;
    }

    s_dr.done = false;
    s_dr.ok = false;
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
        return false;
    }
    *out_ip = to_host_order(&s_dr.addr);
    return true;
}

#else // host build - no real resolver; a host test can inject a synthetic answer

namespace
{
// The injected synthetic answer, owned by one instance (internal linkage): a host test sets
// it via pc_dns_resolver_test_set_resolve() and pc_dns_resolver_resolve() returns it, grouped so it is one
// named owner rather than loose file-scope mutables.
struct DnsTestCtx
{
    bool ok = false;
    uint32_t ip = 0;
};
DnsTestCtx s_dns_test;
} // namespace

void pc_dns_resolver_test_set_resolve(bool ok, uint32_t ip)
{
    s_dns_test.ok = ok;
    s_dns_test.ip = ip;
}
bool pc_dns_resolver_resolve(const char *, uint32_t *out_ip)
{
    if (!s_dns_test.ok)
    {
        return false;
    }
    if (out_ip)
    {
        *out_ip = s_dns_test.ip;
    }
    return true;
}

#endif // ARDUINO

bool pc_dns_resolver_resolve_verified(const char *host, uint32_t *out_ip)
{
    uint32_t ip = 0;
    if (!pc_dns_resolver_resolve(host, &ip))
    {
        return false;
    }
    if (!pc_dns_resolver_verify(ip))
    {
        return false;
    }
    if (out_ip)
    {
        *out_ip = ip;
    }
    return true;
}

#endif // PC_ENABLE_DNS_RESOLVER
