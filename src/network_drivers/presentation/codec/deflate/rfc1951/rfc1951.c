// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rfc1951.c
 * @brief The one definition of the RFC 1951 sec 3.2.5 tables, and the namespace over them.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DEFLATE_RFC1951

#include "network_drivers/presentation/codec/deflate/rfc1951/rfc1951.h"

// Length code base values and extra bits (RFC 1951 sec 3.2.5), codes 257..285.
static const short len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// Distance code base values and extra bits, codes 0..29.
static const short dist_base[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// The one instance the header hands out.
static const Rfc1951Ns instance = {len_base, len_extra, dist_base, dist_extra};

const Rfc1951Ns *protocore_rfc1951(void)
{
    return &instance;
}

#endif // PROTOCORE_ENABLE_DEFLATE_RFC1951
