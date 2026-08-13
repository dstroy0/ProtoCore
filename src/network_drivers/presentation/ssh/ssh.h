// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh.h
 * @brief Entry point: the connection's storage, at the offsets common.h names.
 */

#ifndef PROTOCORE_SSH_SSH_H
#define PROTOCORE_SSH_SSH_H

#include "network_drivers/presentation/ssh/common.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief The base of slot @p i's storage, or null when @p i is out of range.
 *
 * Allocated at compile time and identical for every slot, so a caller adds the offset it needs.
 */
uint8_t *ssh_conn_slot(uint8_t i);

PROTOCORE_END_DECLS

#endif // PROTOCORE_SSH_SSH_H
