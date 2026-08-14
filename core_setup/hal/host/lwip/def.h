// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// lwIP byte-order helpers, standing in for silicon so the target path is the path a host test
// compiles and runs.

#pragma once

#include <stdint.h>

// Also defined by the tcp mock; C permits a repeated typedef of the same type, so either or both
// may be included.
typedef int8_t err_t;

#ifndef ERR_OK
#define ERR_OK ((err_t)0)
#endif
#define ERR_INPROGRESS ((err_t) - 5)

static inline uint32_t lwip_ntohl(uint32_t n)
{
    return ((n & 0xFFu) << 24) | ((n & 0xFF00u) << 8) | ((n >> 8) & 0xFF00u) | ((n >> 24) & 0xFFu);
}

static inline uint32_t lwip_htonl(uint32_t h)
{
    return lwip_ntohl(h);
}
