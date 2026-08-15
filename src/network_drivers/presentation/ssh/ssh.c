// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh.c
 * @brief Every byte the connections use, one span per slot.
 */

#include "network_drivers/presentation/ssh/ssh.h"

// The connections' storage, owned by one instance (internal linkage). Reached only through
// ssh_conn_slot(), at the offsets common.h names.
typedef struct
{
    uint8_t mem[MAX_SSH_CONNS][SSH_SLOT_BORROW];
} SshMemCtx;
static SshMemCtx s_mem;

uint8_t *ssh_conn_slot(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return NULL;
    }
    return s_mem.mem[i];
}
