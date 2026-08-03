// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file scp.h
 * @brief SCP (RCP) protocol wire codec - the pure, host-testable half of the SCP-over-SSH server
 *        (PC_ENABLE_SSH_SCP).
 *
 * SCP transfers a file over an SSH `exec "scp …"` channel using the old rcp line protocol: the source side
 * sends a control line `C<mode> <size> <name>\n`, the peer acks with a 0 byte, then the file bytes flow,
 * ended by a 0 byte and another ack. This file parses/builds the command line and the control line and knows
 * the ack bytes - no filesystem, no SSH, no Arduino, zero heap. The fs::FS sink/source state machine + the
 * channel glue live in network_drivers/application/scp/ssh_scp.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SCP_H
#define PROTOCORE_SCP_H

#include "protocore_config.h"

#if PC_ENABLE_SSH_SCP

// rcp acknowledgement bytes (sent between records).
#define PC_SCP_ACK_OK 0    ///< proceed
#define PC_SCP_ACK_WARN 1  ///< warning: followed by a message + '\n'
#define PC_SCP_ACK_ERROR 2 ///< fatal error: followed by a message + '\n'

/** @brief The role of an `scp` invocation, parsed from the exec command. */
typedef enum PROTO_ENUM_PACKED
{
    SCP_MODE_INVALID = 0,
    SCP_MODE_SINK,  ///< `scp -t <path>`: the client sends a file TO the device (device receives)
    SCP_MODE_SOURCE ///< `scp -f <path>`: the client fetches a file FROM the device (device sends)
} ScpMode;

/**
 * @brief Parse an exec command `scp [-v] [-r] [-p] [-d] -t|-f <path>` into its role + target path.
 * @param cmd not NUL-terminated (@p cmd_len bytes). @return the mode; @p path_out gets the (NUL-terminated)
 *         target, SCP_MODE_INVALID on a command we do not support.
 */
ScpMode pc_scp_parse_cmd(const char *cmd, size_t cmd_len, char *path_out, size_t path_cap);

/**
 * @brief Parse a control line `C<mode> <size> <name>` (a trailing '\n' optional) into its fields.
 * @return true on a well-formed `C` line; @p mode_out is the octal permission bits, @p size_out the byte
 *         count, @p name_out the (NUL-terminated) filename. Only file records (`C`) are handled (not `D`/`E`).
 */
proto_bool pc_scp_parse_cline(const char *line, size_t len, uint32_t *mode_out, uint64_t *size_out, char *name_out,
                              size_t name_cap);

/**
 * @brief Build a control line `C<mode> <size> <name>\n` for a source transfer. @return the length written, or
 *        0 if it would not fit @p cap.
 */
size_t pc_scp_build_cline(uint32_t mode, uint64_t size, const char *name, char *out, size_t cap);

#endif // PC_ENABLE_SSH_SCP

#endif // PROTOCORE_SCP_H
