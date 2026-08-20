// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_scp.h
 * @brief SCP server - the rcp SINK state machine over an SSH exec channel (PROTOCORE_ENABLE_SSH_SCP).
 *
 * Drives the SCP/RCP codec (network_drivers/session/scp) over an SSH `exec "scp …"` channel so a
 * client can drop a file onto the device: `scp localfile admin@device:/path`. v1 serves the SINK
 * direction (client -> device, `scp -t`); the SOURCE direction (`scp -f`, device -> client) is a
 * follow-up - use SFTP `get` to download.
 *
 * Storage is reached through the filesystem accessor (server/storage/filesystem.h), so this file
 * names no vendor type and holds no mount, no root, and no path buffer. Mount the backend and set
 * the root once with protocore_mnt_mount() + protocore_fs_begin(); every server over that storage then answers
 * from the same root by construction rather than by each being told the same string.
 *
 * Streamed writes, fixed buffers, no heap. Call protocore_ssh_scp_begin() once after protocore_ssh_conn_setup().
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_SCP_H
#define PROTOCORE_SSH_SCP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH_SCP

PROTOCORE_BEGIN_DECLS

// PROTOCORE_SSH_SCP_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/**
 * @brief SCP server - the rcp SINK state machine over an SSH exec channel (PROTOCORE_ENABLE_SSH_SCP).
 *
 * A caller sets the members a call takes, invokes it through ::SshScp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   SshScp.begin(work);
 *
 * @var SshScpNs::ok  a call's true/false outcome
 * @var SshScpNs::begin  serve SCP uploads onto the mounted filesystem. Installs the channel ...
 *
 * @c work is PROTOCORE_SSH_SCP_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{

    proto_bool ok;

    void (*const begin)(uint8_t *restrict work);
} SshScpNs;

/** @brief The one symbol this module exports. */
extern SshScpNs SshScp;

/**
 * @brief The PROTOCORE_SSH_SCP_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_ssh_scp_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_SCP

#endif // PROTOCORE_SSH_SCP_H
