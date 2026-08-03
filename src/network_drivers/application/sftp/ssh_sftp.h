// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_sftp.h
 * @brief SFTP v3 server subsystem - the SSH_FXP_* state machine over an SSH session channel
 *        (PC_ENABLE_SSH_SFTP).
 *
 * Drives the pure SFTP v3 codec (network_drivers/application/sftp) over an SSH session channel: when a
 * client requests the "sftp" subsystem, this serves SSH_FXP_* requests (open/read/write/opendir/
 * readdir/stat/mkdir/rmdir/remove/rename/realpath) with a fixed handle table and streamed
 * reads/writes.
 *
 * Storage is reached through the filesystem accessor (server/filesystem/filesystem.h), so this file
 * names no vendor type and holds no mount, no root, and no path buffer: a request path goes to an
 * operation as the bytes the client sent, and the accessor frames it onto the mount root and
 * rejects `..`. Mount the backend and set the root once with pc_mnt_mount() + pc_fs_begin().
 *
 * Call pc_ssh_sftp_begin() once after pc_ssh_conn_setup(); it installs the channel subsystem + data
 * callbacks.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_SFTP_H
#define PROTOCORE_SSH_SFTP_H

#include "protocore_config.h"

#if PC_ENABLE_SSH_SFTP

/**
 * @brief Serve the SFTP subsystem from the mounted filesystem. Installs the channel subsystem +
 *        data callbacks. Call once, after pc_ssh_conn_setup() and pc_fs_begin().
 */
void pc_ssh_sftp_begin(void);

#endif // PC_ENABLE_SSH_SFTP

#endif // PROTOCORE_SSH_SFTP_H
