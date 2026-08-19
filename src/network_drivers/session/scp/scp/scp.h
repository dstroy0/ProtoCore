// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file scp.h
 * @brief SCP (RCP) protocol wire codec - the pure, host-testable half of the SCP-over-SSH server
 *        (PROTOCORE_ENABLE_SSH_SCP).
 *
 * SCP transfers a file over an SSH `exec "scp …"` channel using the old rcp line protocol: the source side
 * sends a control line `C<mode> <size> <name>\n`, the peer acks with a 0 byte, then the file bytes flow,
 * ended by a 0 byte and another ack. This file parses/builds the command line and the control line and knows
 * the ack bytes - no filesystem, no SSH, no Arduino, zero heap. The fs::FS sink/source state machine + the
 * channel glue live in network_drivers/session/scp/ssh_scp.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SCP_H
#define PROTOCORE_SCP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH_SCP

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// rcp acknowledgement bytes (sent between records).
#define PROTOCORE_SCP_ACK_OK 0    ///< proceed
#define PROTOCORE_SCP_ACK_WARN 1  ///< warning: followed by a message + '\n'
#define PROTOCORE_SCP_ACK_ERROR 2 ///< fatal error: followed by a message + '\n'

/** @brief The role of an `scp` invocation, parsed from the exec command. */
typedef enum PROTO_ENUM_PACKED
{
    SCP_MODE_INVALID = 0,
    SCP_MODE_SINK,  ///< `scp -t <path>`: the client sends a file TO the device (device receives)
    SCP_MODE_SOURCE ///< `scp -f <path>`: the client fetches a file FROM the device (device sends)
} ScpMode;

/** @brief What parse_cmd takes: cmd, cmd_len, path_out, path_cap. */
typedef struct
{
    const char *cmd; ///< not NUL-terminated (cmd_len bytes). the mode; path_out gets the (NUL-terminated) target, ...
    size_t cmd_len;
    char *path_out;
    size_t path_cap;
} ScpParseCmdArgs;

/** @brief What parse_cline takes: line, len, mode_out, size_out, ... */
typedef struct
{
    const char *line;
    size_t len;
    uint32_t *mode_out;
    uint64_t *size_out;
    char *name_out;
    size_t name_cap;
} ScpParseClineArgs;

/** @brief What build_cline takes: mode, size, name, out, cap. */
typedef struct
{
    uint32_t mode;
    uint64_t size;
    const char *name;
    char *out;
    size_t cap;
} ScpBuildClineArgs;

/**
 * @brief SCP (RCP) protocol wire codec - the pure, host-testable half of the SCP-over-SSH server
 * (PROTOCORE_ENABLE_SSH_SCP).
 *
 * A caller sets the members a call takes, invokes it through ::Scp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Scp.parse_cmd_args.cmd = ...;
 *   Scp.parse_cmd_args.cmd_len = ...;
 *   Scp.parse_cmd_args.path_out = ...;
 *   Scp.parse_cmd_args.path_cap = ...;
 *   Scp.parse_cmd(work);
 *   // Scp.value is what the call reports
 *
 * @var ScpNs::parse_cmd_args  what parse_cmd takes: cmd, cmd_len, path_out, path_cap
 * @var ScpNs::parse_cline_args  what parse_cline takes: line, len, mode_out, size_out,
 * @var ScpNs::build_cline_args  what build_cline takes: mode, size, name, out, cap
 * @var ScpNs::ok  true on a well-formed `C` line; mode_out is the octal permission ...
 * @var ScpNs::value  the value a call reports
 * @var ScpNs::n  the count a call reports
 * @var ScpNs::parse_cmd  parse an exec command `scp [-v] [-r] [-p] [-d] -t|-f <path>` into ...
 * @var ScpNs::parse_cline  parse a control line `C<mode> <size> <name>` (a trailing '\n' ...
 * @var ScpNs::build_cline  build a control line `C<mode> <size> <name>\n` for a source ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ScpParseCmdArgs parse_cmd_args;
    ScpParseClineArgs parse_cline_args;
    ScpBuildClineArgs build_cline_args;

    proto_bool ok;
    ScpMode value;
    size_t n;

    void (*const parse_cmd)(uint8_t *restrict work);
    void (*const parse_cline)(uint8_t *restrict work);
    void (*const build_cline)(uint8_t *restrict work);
} ScpNs;

/** @brief The one symbol this module exports. */
extern ScpNs Scp;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_SCP

#endif // PROTOCORE_SCP_H
