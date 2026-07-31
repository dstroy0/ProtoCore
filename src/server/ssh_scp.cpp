// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_scp.cpp
 * @brief SCP server - fs::FS binding (SINK direction). See ssh_scp.h.
 *
 * Drives the rcp SINK protocol over an SSH `exec "scp -t <path>"` channel: send a ready ack, read the client's
 * `C<mode> <size> <name>` control line, ack it, stream <size> data bytes straight to an fs::FS file, read the
 * trailing end-of-record byte, and send the final ack. One file per transfer (no -r); the SOURCE direction
 * (`scp -f`) replies "use sftp get". Every path is checked for `..` traversal before opening the file.
 */

#include "server/ssh_scp.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder

#if PC_ENABLE_SSH_SCP

#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "server/fs_path.h"
#include "services/file_transfer/scp/scp.h"
#include <string.h>

namespace
{
enum class ScpSt : uint8_t
{
    PC_NONE,
    WAIT_CLINE, ///< reading the C<mode> <size> <name> control line
    RECV,       ///< streaming file data to disk
    WAIT_END    ///< the file's bytes are in; awaiting the end-of-record byte
};

struct ScpConn
{
    bool active;
    uint8_t slot;
    uint32_t channel;
    ScpSt st;
    char dest[PC_SFTP_PATH_MAX]; ///< the -t target (a file, or a dir if it ends with '/')
    bool dest_is_dir;
    fs::File file;
    uint64_t remaining; ///< data bytes still to receive
    bool err;
    uint16_t cl_len; ///< control-line accumulator length
    char cl[PC_SFTP_PATH_MAX + 64];
};

struct SshScpCtx
{
    fs::FS *fs = nullptr;
    const char *root = "/";
    bool registered = false;
    ScpConn conns[MAX_SSH_CONNS];
};
SshScpCtx s_scp;

void ack(ScpConn *c, uint8_t byte)
{
    pc_ssh_conn_send(c->slot, c->channel, &byte, 1);
}
void err_ack(ScpConn *c, const char *msg)
{
    uint8_t buf[96];
    buf[0] = PC_SCP_ACK_ERROR;
    size_t ml = strnlen(msg, sizeof(buf) - 3);
    memcpy(buf + 1, msg, ml);
    buf[1 + ml] = '\n';
    pc_ssh_conn_send(c->slot, c->channel, buf, 2 + ml);
}
void pc_scp_end(ScpConn *c)
{
    if (c->file)
    {
        c->file.close();
    }
    c->file = fs::File();
    c->active = false;
    pc_ssh_conn_close_channel(c->slot, c->channel);
}

void pc_scp_on_open(uint8_t slot, uint32_t channel, const char *cmd, size_t cmd_len)
{
    if (slot >= MAX_SSH_CONNS)
    {
        return;
    }
    ScpConn *c = &s_scp.conns[slot];
    if (c->file)
    {
        c->file.close();
    }
    c->file = fs::File();
    c->active = true;
    c->slot = slot;
    c->channel = channel;
    c->err = false;
    c->cl_len = 0;

    char path[PC_SFTP_PATH_MAX];
    ScpMode mode = pc_scp_parse_cmd(cmd, cmd_len, path, sizeof(path));
    if (mode == ScpMode::SINK)
    {
        size_t pl = strnlen(path, sizeof(path));
        c->dest_is_dir = (pl > 0 && path[pl - 1] == '/');
        strncpy(c->dest, path, sizeof(c->dest) - 1);
        c->dest[sizeof(c->dest) - 1] = '\0';
        c->st = ScpSt::WAIT_CLINE;
        ack(c, PC_SCP_ACK_OK); // ready for the control line
    }
    else if (mode == ScpMode::SOURCE)
    {
        err_ack(c, "scp download not supported; use sftp get");
        pc_scp_end(c);
    }
    else
    {
        err_ack(c, "unsupported scp command");
        pc_scp_end(c);
    }
}

// Resolve the on-disk destination for a received file named @p name.
bool pc_scp_resolve_dest(ScpConn *c, const char *name, char *out, size_t cap)
{
    char sub[PC_SFTP_PATH_MAX + 96];
    if (c->dest_is_dir)
    {
        pc_sb sb_sub = {sub, sizeof(sub), 0, true};
        pc_sb_put(&sb_sub, c->dest);
        pc_sb_put(&sb_sub, name);
        if (pc_sb_finish(&sb_sub) == 0)
        {
            sub[0] = '\0'; // c->dest ends with '/'
        }
    }
    else
    {
        pc_sb sb_sub2 = {sub, sizeof(sub), 0, true};
        pc_sb_put(&sb_sub2, c->dest);
        if (pc_sb_finish(&sb_sub2) == 0)
        {
            sub[0] = '\0';
        }
    }
    return fs_path_resolve(s_scp.root, sub, out, cap) == 0;
}

void pc_scp_on_data(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len)
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
        if (c->st == ScpSt::WAIT_CLINE)
        {
            bool complete = false;
            while (len > 0)
            {
                char ch = (char)data[0];
                data++;
                len--;
                if (ch == '\n')
                {
                    complete = true;
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
            char name[PC_SFTP_PATH_MAX];
            if (!pc_scp_parse_cline(c->cl, c->cl_len, &mode, &size, name, sizeof(name)))
            {
                err_ack(c, "unsupported scp record"); // e.g. a D/E directory record (no -r support)
                pc_scp_end(c);
                return;
            }
            char disk[PC_SFTP_PATH_MAX];
            if (!pc_scp_resolve_dest(c, name, disk, sizeof(disk)))
            {
                err_ack(c, "bad path");
                pc_scp_end(c);
                return;
            }
            c->file = s_scp.fs->open(disk, "w");
            if (!c->file)
            {
                err_ack(c, "cannot create file");
                pc_scp_end(c);
                return;
            }
            c->remaining = size;
            c->st = (size == 0) ? ScpSt::WAIT_END : ScpSt::RECV;
            c->cl_len = 0;
            ack(c, PC_SCP_ACK_OK); // proceed with the data
            continue;
        }
        if (c->st == ScpSt::RECV)
        {
            size_t take = (len < c->remaining) ? len : (size_t)c->remaining;
            if (c->file.write(data, take) != take)
            {
                c->err = true;
            }
            data += take;
            len -= take;
            c->remaining -= take;
            if (c->remaining == 0)
            {
                c->st = ScpSt::WAIT_END;
            }
            continue;
        }
        if (c->st == ScpSt::WAIT_END)
        {
            data++; // consume the end-of-record byte (0)
            len--;
            if (c->file)
            {
                c->file.close();
            }
            c->file = fs::File();
            if (c->err)
            {
                err_ack(c, "write error");
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
} // namespace

void pc_ssh_scp_begin(fs::FS &fs, const char *root)
{
    s_scp.fs = &fs;
    s_scp.root = (root && root[0]) ? root : "/";
    for (int i = 0; i < MAX_SSH_CONNS; i++)
    {
        s_scp.conns[i].active = false;
        if (s_scp.conns[i].file)
        {
            s_scp.conns[i].file.close();
        }
        s_scp.conns[i].file = fs::File();
    }
    if (!s_scp.registered)
    {
        pc_ssh_channel_set_scp_open_cb(pc_scp_on_open);
        pc_ssh_channel_set_scp_data_cb(pc_scp_on_data);
        s_scp.registered = true;
    }
}

#endif // PC_ENABLE_SSH_SCP
