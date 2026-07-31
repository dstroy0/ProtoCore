// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_scp.h
 * @brief SCP server - the fs::FS binding (PC_ENABLE_SSH_SCP).
 *
 * Binds the pure SCP/RCP codec (services/file_transfer/scp) to an SSH `exec "scp …"` channel + an Arduino fs::FS mount
 * so a client can drop a file onto the device: `scp localfile admin@device:/path`. v1 serves the SINK direction (client
 * -> device, `scp -t`); the SOURCE direction (`scp -f`, device -> client) is a follow-up - use SFTP `get` to download.
 * Streamed writes, fixed buffers, no heap beyond the fs layer's file handle. Call pc_ssh_scp_begin() once after
 * pc_ssh_conn_setup().
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_SCP_H
#define PROTOCORE_SSH_SCP_H

#include "protocore_config.h"

#if PC_ENABLE_SSH_SCP

#include <FS.h>

/**
 * @brief Serve SCP uploads from @p fs under @p root. Installs the channel exec-"scp" + data callbacks. Call
 *        once, after pc_ssh_conn_setup(). Coexists with pc_ssh_sftp_begin (they share the SSH channel layer).
 */
void pc_ssh_scp_begin(fs::FS &fs, const char *root);

#endif // PC_ENABLE_SSH_SCP

#endif // PROTOCORE_SSH_SCP_H
