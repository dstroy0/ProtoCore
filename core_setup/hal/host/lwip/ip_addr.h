// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The lwIP address type and its dotted-quad parser, standing in for silicon so the target path is
// the path a host test compiles and runs.

#pragma once

#include "lwip/def.h"
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t addr; ///< network byte order, as lwIP stores it
} ip4_addr_t;

typedef ip4_addr_t ip_addr_t;

static inline ip4_addr_t *ip_2_ip4(const ip_addr_t *a)
{
    return (ip4_addr_t *)a;
}

static inline uint32_t ip4_addr_get_u32(const ip4_addr_t *a)
{
    return a->addr;
}

/** @brief Parse a dotted quad into @p out. Returns 1 on success, 0 otherwise, as lwIP's does. */
static inline int ipaddr_aton(const char *s, ip_addr_t *out)
{
    if (s == NULL || out == NULL)
    {
        return 0;
    }
    uint32_t oct[4] = {0, 0, 0, 0};
    int n = 0;
    int digits = 0;
    for (const char *p = s;; p++)
    {
        if (*p >= '0' && *p <= '9')
        {
            oct[n] = oct[n] * 10u + (uint32_t)(*p - '0');
            if (oct[n] > 255u)
            {
                return 0;
            }
            digits++;
            continue;
        }
        if (digits == 0)
        {
            return 0; // an empty octet
        }
        digits = 0;
        if (*p == '.')
        {
            n++;
            if (n > 3)
            {
                return 0;
            }
            continue;
        }
        if (*p == '\0')
        {
            break;
        }
        return 0; // any other byte: not a literal
    }
    if (n != 3)
    {
        return 0;
    }
    out->addr = lwip_htonl((oct[0] << 24) | (oct[1] << 16) | (oct[2] << 8) | oct[3]);
    return 1;
}
