// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SCP_H
#define PROTOCORE_SCP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file scp.h
 * @brief SCP (RCP) protocol wire codec - the pure, host-testable half of the SCP-over-SSH server
(PROTOCORE_ENABLE_SSH_SCP).
 *
 * SCP transfers a file over an SSH `exec "scp …"` channel using the old rcp line protocol: the source side
 * sends a control line `C<mode> <size> <name>\n`, the peer acks with a 0 byte, then the file bytes flow,
 * ended by a 0 byte and another ack. This file parses/builds the command line and the control line and knows
 * the ack bytes - no filesystem, no SSH, no Arduino, zero heap. The fs::FS sink/source state machine + the
 * channel glue live in network_drivers/session/scp/ssh_scp.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_SCP_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    ScpMode (*parse_cmd)(uint8_t *, const char *, size_t, char *, size_t);
    proto_bool (*parse_cline)(uint8_t *, const char *, size_t, uint32_t *, uint64_t *, char *, size_t);
    size_t (*build_cline)(uint8_t *, uint32_t, uint64_t, const char *, char *, size_t);
} ScpNs;
PROTOCORE_NS_LAYOUT(ScpNs, parse_cmd, parse_cline, build_cline);

/**
 * @brief Parse an exec command `scp [-v] [-r] [-p] [-d] -t|-f <path>` into .
 * @param work PROTOCORE_SCP_BORROW bytes the caller took. Not held past the call.
 * @param cmd not NUL-terminated (cmd_len bytes). the mode; path_out gets the (NUL-terminated) target,
 * @param cmd_len Cmd len
 * @param path_out Path out
 * @param path_cap Path cap
 * @return The ScpMode.
 */
ScpMode protocore_scp_parse_cmd(uint8_t *work, const char *cmd, size_t cmd_len, char *path_out, size_t path_cap);
/**
 * @brief Parse a control line `C<mode> <size> <name>` (a trailing '\n' .
 * @param work PROTOCORE_SCP_BORROW bytes the caller took. Not held past the call.
 * @param line Line
 * @param len Len
 * @param mode_out Mode out
 * @param size_out Size out
 * @param name_out Name out
 * @param name_cap Name cap
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_scp_parse_cline(uint8_t *work, const char *line, size_t len, uint32_t *mode_out,
                                     uint64_t *size_out, char *name_out, size_t name_cap);
/**
 * @brief Build a control line `C<mode> <size> <name>\n` for a source .
 * @param work PROTOCORE_SCP_BORROW bytes the caller took. Not held past the call.
 * @param mode Mode
 * @param size Size
 * @param name Name
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_scp_build_cline(uint8_t *work, uint32_t mode, uint64_t size, const char *name, char *out, size_t cap);

/** @brief Module namespace. */
PROTOCORE_NS ScpNs Scp PROTOCORE_UNUSED = {.parse_cmd = protocore_scp_parse_cmd,
                                           .parse_cline = protocore_scp_parse_cline,
                                           .build_cline = protocore_scp_build_cline};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SCP_H
