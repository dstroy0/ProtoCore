// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The lwIP DNS surface dns_resolver.c resolves through, standing in for silicon so the target path
// is the path a host test compiles and runs.
//
// A resolve answers from g_mock_dns_answer: set it to a host-order IPv4 for success, leave it 0 for
// the "no answer" case. The callback fires inline, so the resolve loop sees its done flag on the
// first poll and never spins on the deadline.

#pragma once

#include "lwip/def.h"
#include "lwip/ip_addr.h"
#include <stddef.h>
#include <stdint.h>

// The injected answer: host-order IPv4, 0 meaning the name does not resolve. Weak, so the test and
// the resolver reach the same word - see the note on linkage in pc_net_host.h.
__attribute__((weak)) uint32_t g_mock_dns_answer;

typedef void (*dns_found_callback)(const char *name, const ip_addr_t *addr, void *arg);

static inline err_t dns_gethostbyname(const char *name, ip_addr_t *addr, dns_found_callback found, void *arg)
{
    uint32_t answer = g_mock_dns_answer;
    if (answer == 0u)
    {
        found(name, NULL, arg); // no answer, reported the way lwIP reports one
        return ERR_INPROGRESS;
    }
    ip_addr_t a;
    a.addr = lwip_htonl(answer);
    if (addr != NULL)
    {
        *addr = a;
    }
    found(name, &a, arg);
    return ERR_INPROGRESS; // the callback carried the result, as the async path does
}
