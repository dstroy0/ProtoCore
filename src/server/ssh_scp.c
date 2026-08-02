// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_scp.c
 * @brief SCP server - the rcp SINK state machine. See ssh_scp.h.
 *
 * Drives the rcp SINK protocol over an SSH `exec "scp -t <path>"` channel: send a ready ack, read the client's
 * `C<mode> <size> <name>` control line, ack it, stream <size> data bytes straight to the file, read the trailing
 * end-of-record byte, and send the final ack. One file per transfer (no -r); the SOURCE direction (`scp -f`)
 * replies "use sftp get".
 *
 * The destination reaches storage as the two pieces it already is - the `-t` target and, when that target is a
 * directory, the control line's filename - so the accessor frames the whole path once. Resolving it here would
 * put a root, a path buffer, its capacity, and a copy of the `..` guard into a protocol server.
 */

#include "server/ssh_scp.h"

#if PC_ENABLE_SSH_SCP

#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "server/filesystem/filesystem.h"
#include "services/file_transfer/scp/scp.h"
#include "shared_primitives/runops.h" // the bounded word-at-a-time length scan
#include <string.h>

typedef enum PROTO_ENUM_PACKED
{
    PC_NONE,
    WAIT_CLINE, ///< reading the C<mode> <size> <name> control line
    RECV,       ///< streaming file data to disk
    WAIT_END    ///< the file's bytes are in; awaiting the end-of-record byte
} ScpSt;

typedef struct
{
    proto_bool active;
    uint8_t slot;
    uint32_t channel;
    ScpSt st;
    char dest[PC_FILESYSTEM_PATH_MAX]; ///< the -t target (a file, or a dir if it ends with '/')
    proto_bool dest_is_dir;
    int fh;             ///< open file handle, or -1
    uint64_t remaining; ///< data bytes still to receive
    proto_bool err;
    uint16_t cl_len; ///< control-line accumulator length
    char cl[PC_FILESYSTEM_PATH_MAX + 64];
} ScpConn;

// All SCP state in one owner with internal linkage, the work buffer included: a stack array is the
// one allocation the fixed-footprint accounting cannot see, and the buffer does not outlive the
// callback that fills it, so one serves every slot.
typedef struct
{
    proto_bool registered;
    // The root this server works through, bound once in begin(). A handle, not a path: this file
    // cannot name where storage begins.
    int root;
    ScpConn conns[MAX_SSH_CONNS];
    char leaf[PC_FILESYSTEM_PATH_MAX]; ///< one control line's filename, live only until the open
} SshScpCtx;

// -1 until bound, not the 0 static storage would give: root 0 is a valid root, so a zeroed field
// would resolve against somebody else's storage before pc_ssh_scp_begin() ran.
static SshScpCtx s_scp = {.root = -1};

// An error ack is a status byte, a message, and a terminator, and all three are fixed - so each one
// is a single rodata string, sent as it stands. Staging one in a buffer would copy a constant and
// then scan the copy for a length the compiler already had.
//
// The status byte is spelled as its own literal so the concatenation boundary is explicit: written
// as one string, "\x02c" would be read as the single byte 0x2C, because a hex escape consumes every
// hex digit that follows it and 'c' is one.
static const char SCP_ERR_NO_SOURCE[] = "\x02"
                                        "scp download not supported; use sftp get\n";
static const char SCP_ERR_BAD_CMD[] = "\x02"
                                      "unsupported scp command\n";
static const char SCP_ERR_BAD_RECORD[] = "\x02"
                                         "unsupported scp record\n";
static const char SCP_ERR_CREATE[] = "\x02"
                                     "cannot create file\n";
static const char SCP_ERR_WRITE[] = "\x02"
                                    "write error\n";

static void ack(ScpConn *c, uint8_t byte)
{
    pc_ssh_conn_send(c->slot, c->channel, &byte, 1);
}
/** @brief Send one complete error record. @p len is `sizeof(record) - 1`, resolved at compile time. */
static void err_ack(ScpConn *c, const char *rec, size_t len)
{
    pc_ssh_conn_send(c->slot, c->channel, (const uint8_t *)(rec), len);
}
static void close_file(ScpConn *c)
{
    if (c->fh >= 0)
    {
        pc_fs_close(c->fh);
        c->fh = -1;
    }
}
static void pc_scp_end(ScpConn *c)
{
    close_file(c);
    c->active = PROTO_FALSE;
    pc_ssh_conn_close_channel(c->slot, c->channel);
}

static void pc_scp_on_open(uint8_t slot, uint32_t channel, const char *cmd, size_t cmd_len)
{
    if (slot >= MAX_SSH_CONNS)
    {
        return;
    }
    ScpConn *c = &s_scp.conns[slot];
    close_file(c);
    c->active = PROTO_TRUE;
    c->slot = slot;
    c->channel = channel;
    c->err = PROTO_FALSE;
    c->cl_len = 0;

    // Parsed straight into the field that keeps it. Parsing into scratch and copying would move a
    // whole path twice and then rescan the copy for a length the parse already walked past.
    ScpMode mode = pc_scp_parse_cmd(cmd, cmd_len, c->dest, sizeof(c->dest));
    if (mode == SINK)
    {
        // A '/' terminator is what makes the target a directory, and it is also the separator the
        // accessor's join relies on, so the flag and the string agree without either being rebuilt.
        size_t pl = proto_scan_nul(c->dest, sizeof(c->dest));
        c->dest_is_dir = (pl > 0 && c->dest[pl - 1] == '/');
        c->st = WAIT_CLINE;
        ack(c, PC_SCP_ACK_OK); // ready for the control line
    }
    else if (mode == SOURCE)
    {
        err_ack(c, SCP_ERR_NO_SOURCE, sizeof(SCP_ERR_NO_SOURCE) - 1);
        pc_scp_end(c);
    }
    else
    {
        err_ack(c, SCP_ERR_BAD_CMD, sizeof(SCP_ERR_BAD_CMD) - 1);
        pc_scp_end(c);
    }
}

static void pc_scp_on_data(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len)
{
    if (slot >= MAX_SSH_CONNS)
    {
        return;
    }
    ScpConn *c = &s_scp.conns[slot];
    if (!c->active || c->channel != channel)
    {
        return;
    }

    while (len > 0)
    {
        if (c->st == WAIT_CLINE)
        {
            proto_bool complete = PROTO_FALSE;
            while (len > 0)
            {
                char ch = (char)data[0];
                data++;
                len--;
                if (ch == '\n')
                {
                    complete = PROTO_TRUE;
                    break;
                }
                if (c->cl_len < sizeof(c->cl) - 1)
                {
                    c->cl[c->cl_len++] = ch;
                }
            }
            if (!complete)
            {
                return; // the whole control line has not arrived yet
            }
            c->cl[c->cl_len] = '\0';

            uint32_t mode = 0;
            uint64_t size = 0;
            if (!pc_scp_parse_cline(c->cl, c->cl_len, &mode, &size, s_scp.leaf, sizeof(s_scp.leaf)))
            {
                // e.g. a D/E directory record (no -r support)
                err_ack(c, SCP_ERR_BAD_RECORD, sizeof(SCP_ERR_BAD_RECORD) - 1);
                pc_scp_end(c);
                return;
            }
            // A directory target takes the control line's filename; a file target is the whole
            // destination on its own. Either way the accessor gets the pieces and frames once.
            c->fh = pc_fs_open(s_scp.root, c->dest, c->dest_is_dir ? s_scp.leaf : "", PC_MNT_WRITE);
            if (c->fh < 0)
            {
                err_ack(c, SCP_ERR_CREATE, sizeof(SCP_ERR_CREATE) - 1);
                pc_scp_end(c);
                return;
            }
            c->remaining = size;
            c->st = (size == 0) ? WAIT_END : RECV;
            c->cl_len = 0;
            ack(c, PC_SCP_ACK_OK); // proceed with the data
            continue;
        }
        if (c->st == RECV)
        {
            size_t take = (len < c->remaining) ? len : (size_t)c->remaining;
            if (pc_fs_write(c->fh, data, take) != (int)take)
            {
                c->err = PROTO_TRUE;
            }
            data += take;
            len -= take;
            c->remaining -= take;
            if (c->remaining == 0)
            {
                c->st = WAIT_END;
            }
            continue;
        }
        if (c->st == WAIT_END)
        {
            data++; // consume the end-of-record byte (0)
            len--;
            close_file(c);
            if (c->err)
            {
                err_ack(c, SCP_ERR_WRITE, sizeof(SCP_ERR_WRITE) - 1);
            }
            else
            {
                ack(c, PC_SCP_ACK_OK);
            }
            pc_scp_end(c);
            return;
        }
        return; // PC_NONE / unexpected
    }
}

void pc_ssh_scp_begin(void)
{
    // Bind the root this server answers from. Naming a different one than SFTP is how the two end up
    // over different storage; naming the same one shares it and costs one entry.
    s_scp.root = pc_fs_begin("mnt/scp");

    for (int i = 0; i < MAX_SSH_CONNS; i++)
    {
        s_scp.conns[i].active = PROTO_FALSE;
        // BSS zeroes this table, and 0 is a valid handle, so the free marker is set rather than
        // assumed - closing handle 0 here would take a file another server had open.
        s_scp.conns[i].fh = -1;
    }
    if (!s_scp.registered)
    {
        pc_ssh_channel_set_scp_open_cb(pc_scp_on_open);
        pc_ssh_channel_set_scp_data_cb(pc_scp_on_data);
        s_scp.registered = PROTO_TRUE;
    }
}

#endif // PC_ENABLE_SSH_SCP
