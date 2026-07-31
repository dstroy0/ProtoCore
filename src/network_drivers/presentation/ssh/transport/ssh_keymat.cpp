// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_keymat.cpp
 * @brief BSS definitions for SSH session key and DH state pools.
 *
 * Declared extern in ssh_keymat.h; defined here as separate linker symbols
 * so that a linear overflow from ssh_pkt[] (in ssh_packet.cpp) cannot reach
 * ssh_keys[] or ssh_dh[] in a single stride.  See ssh_keymat.h for the full
 * security rationale.
 */

#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"

SshKeyMat ssh_keys[MAX_SSH_CONNS];
SshDhState ssh_dh[MAX_SSH_CONNS];
