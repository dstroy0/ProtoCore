// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_sftp.h
 * @brief SFTP server subsystem - the fs::FS binding (PC_ENABLE_SSH_SFTP).
 *
 * Binds the pure SFTP v3 codec (services/file_transfer/sftp) to an SSH session channel + an Arduino fs::FS mount: when
 * a client requests the "sftp" subsystem, this serves SSH_FXP_* requests (open/read/write/opendir/readdir/
 * stat/mkdir/rmdir/remove/rename/realpath) against files under @p root, with a fixed handle table and
 * streamed reads/writes (no heap beyond the fs layer's own file handles). Call pc_ssh_sftp_begin() once
 * after pc_ssh_conn_setup(); it installs the channel subsystem + data callbacks.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_SFTP_H
#define PROTOCORE_SSH_SFTP_H

#include "protocore_config.h"

#if PC_ENABLE_SSH_SFTP

#include <FS.h>

/**
 * @brief Serve the SFTP subsystem from @p fs under @p root (a persistent string, e.g. "/" or "/gcode").
 *        Installs the channel subsystem + data callbacks. Call once, after pc_ssh_conn_setup().
 */
void pc_ssh_sftp_begin(fs::FS &fs, const char *root);

#endif // PC_ENABLE_SSH_SFTP

#endif // PROTOCORE_SSH_SFTP_H
