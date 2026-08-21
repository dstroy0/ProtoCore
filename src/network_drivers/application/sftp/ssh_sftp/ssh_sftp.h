// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SSH_SFTP_H
#define PROTOCORE_SSH_SFTP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file ssh_sftp.h
 * @brief SFTP v3 server subsystem - the SSH_FXP_* state machine over an SSH session channel
(PROTOCORE_ENABLE_SSH_SFTP).
 *
 * Drives the pure SFTP v3 codec (network_drivers/application/sftp) over an SSH session channel: when a
 * client requests the "sftp" subsystem, this serves SSH_FXP_* requests (open/read/write/opendir/
 * readdir/stat/mkdir/rmdir/remove/rename/realpath) with a fixed handle table and streamed
 * reads/writes.
 *
 * Storage is reached through the filesystem accessor (server/storage/filesystem.h), so this file
 * names no vendor type and holds no mount, no root, and no path buffer: a request path goes to an
 * operation as the bytes the client sent, and the accessor frames it onto the mount root and
 * rejects `..`. Mount the backend and set the root once with protocore_mnt_mount() + protocore_fs_begin().
 *
 * Call protocore_ssh_sftp_begin() once after protocore_ssh_conn_setup(); it installs the channel subsystem + data
 * callbacks.
 *
 * @c work is PROTOCORE_SSH_SFTP_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*begin)(uint8_t *restrict);
} SshSftpNs;
PROTOCORE_NS_LAYOUT(SshSftpNs, begin);

/**
 * @brief Serve the SFTP subsystem from the mounted filesystem. Installs the .
 * @param work PROTOCORE_SSH_SFTP_BORROW bytes the caller took. Not held past the call.
 */
void protocore_ssh_sftp_begin(uint8_t *restrict work);

/**
 * @brief The PROTOCORE_SSH_SFTP_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_ssh_sftp_span(void);

/** @brief Module namespace. */
PROTOCORE_NS SshSftpNs SshSftp PROTOCORE_UNUSED = {.begin = protocore_ssh_sftp_begin};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SSH_SFTP_H
