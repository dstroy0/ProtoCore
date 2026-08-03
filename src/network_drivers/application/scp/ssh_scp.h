// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_scp.h
 * @brief SCP server - the rcp SINK state machine over an SSH exec channel (PC_ENABLE_SSH_SCP).
 *
 * Drives the SCP/RCP codec (network_drivers/application/scp) over an SSH `exec "scp …"` channel so a
 * client can drop a file onto the device: `scp localfile admin@device:/path`. v1 serves the SINK
 * direction (client -> device, `scp -t`); the SOURCE direction (`scp -f`, device -> client) is a
 * follow-up - use SFTP `get` to download.
 *
 * Storage is reached through the filesystem accessor (server/filesystem/filesystem.h), so this file
 * names no vendor type and holds no mount, no root, and no path buffer. Mount the backend and set
 * the root once with pc_mnt_mount() + pc_fs_begin(); every server over that storage then answers
 * from the same root by construction rather than by each being told the same string.
 *
 * Streamed writes, fixed buffers, no heap. Call pc_ssh_scp_begin() once after pc_ssh_conn_setup().
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_SCP_H
#define PROTOCORE_SSH_SCP_H

#include "protocore_config.h"

#if PC_ENABLE_SSH_SCP

/**
 * @brief Serve SCP uploads onto the mounted filesystem. Installs the channel exec-"scp" + data
 *        callbacks. Call once, after pc_ssh_conn_setup() and pc_fs_begin(). Coexists with
 *        pc_ssh_sftp_begin (they share the SSH channel layer).
 */
void pc_ssh_scp_begin(void);

#endif // PC_ENABLE_SSH_SCP

#endif // PROTOCORE_SSH_SCP_H
