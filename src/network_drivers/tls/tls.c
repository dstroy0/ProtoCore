// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls.c
 * @brief The per-slot TLS connections, and the slot-indexed surface over them. See tls.h.
 *
 * handshake/ drives one ::TlsConn and record/ frames for it; neither knows what a connection slot
 * is. This file is the table that joins the two, so a caller that has a slot index reaches the
 * connection standing on it.
 */

#include "network_drivers/tls/tls.h"

#if PROTOCORE_TLS_SOFTWARE

// One connection per slot, indexed by it. BSS, so a slot costs no allocator.
static TlsConn s_conns[MAX_CONNS];

TlsConn *protocore_tls_conn_at(uint8_t slot)
{
    return (slot < MAX_CONNS) ? &s_conns[slot] : NULL;
}

const char *protocore_tls_alpn(uint8_t slot)
{
    const TlsConn *c = protocore_tls_conn_at(slot);
    return c ? c->alpn : NULL;
}

#endif // PROTOCORE_TLS_SOFTWARE
