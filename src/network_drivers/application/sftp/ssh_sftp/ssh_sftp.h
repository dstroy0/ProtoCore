// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_sftp.h
 * @brief SFTP v3 server subsystem - the SSH_FXP_* state machine over an SSH session channel
 *        (PROTOCORE_ENABLE_SSH_SFTP).
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
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_SFTP_H
#define PROTOCORE_SSH_SFTP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH_SFTP

PROTOCORE_BEGIN_DECLS

// PROTOCORE_SSH_SFTP_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/**
 * @brief SFTP v3 server subsystem - the SSH_FXP_* state machine over an SSH session channel
 * (PROTOCORE_ENABLE_SSH_SFTP).
 *
 * A caller sets the members a call takes, invokes it through ::SshSftp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   SshSftp.begin(work);
 *
 * @var SshSftpNs::ok  a call's true/false outcome
 * @var SshSftpNs::begin  serve the SFTP subsystem from the mounted filesystem. Installs the ...
 *
 * @c work is PROTOCORE_SSH_SFTP_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    proto_bool ok;
} SshSftpVars;

/** @brief The operands and the outcome. */
extern SshSftpVars SshSftpV;

/** @brief The entries. */
typedef struct
{
    void (*const begin)(uint8_t *restrict work);
} SshSftpNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SshSftpV or a region of the borrow at a fixed offset.
void protocore_ssh_sftp_begin(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `SshSftp.begin(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SshSftpNs SshSftp __attribute__((unused)) = {
    .begin = protocore_ssh_sftp_begin,
};

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

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_SFTP

#endif // PROTOCORE_SSH_SFTP_H
