// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_sftp.cpp
 * @brief SFTP server subsystem - fs::FS binding. See ssh_sftp.h.
 *
 * Binds the pure SFTP v3 codec (services/file_transfer/sftp) to an SSH session channel: accumulates SSH_FXP_* request
 * packets from the channel byte stream, executes them against an fs::FS mount, and frames responses back with
 * pc_ssh_conn_send. A large WRITE is streamed straight to the file (never buffered whole); a READ returns a short
 * DATA (the client re-requests). A fixed handle table holds open files/dirs. Every path is checked for `..`
 * traversal (server/fs_path.h) before touching the filesystem.
 */

#include "server/ssh_sftp.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder

#if PC_ENABLE_SSH_SFTP

#include "network_drivers/presentation/ssh/connection/ssh_channel.h" // callbacks + setters
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"    // pc_ssh_conn_send / pc_ssh_conn_close_channel
#include "server/fs_path.h"
#include "services/file_transfer/sftp/sftp.h"
#include <string.h>

namespace
{
// Leave headroom below one SSH packet for the CHANNEL_DATA framing, so pc_ssh_conn_send never rejects a response.
#define PC_SFTP_RESP_CAP (SSH_PKT_BUF_SIZE - 16)
// Worst-case one READDIR NAME entry (filename + longname + attrs), used to stash an entry that did not fit.
#define PC_SFTP_ENTRY_MAX (PC_SFTP_PATH_MAX + 320)

struct SftpHandle
{
    bool used;
    bool is_dir;
    fs::File file;               ///< the open fs handle (a dir cursor for READDIR)
    char path[PC_SFTP_PATH_MAX]; ///< resolved on-disk path
    bool readdir_done;           ///< the directory has been fully listed
    bool has_pending;            ///< a READDIR entry that did not fit last time, emitted first next time
    uint16_t pend_len;
    uint8_t pend[PC_SFTP_ENTRY_MAX];
};

struct SftpSession
{
    bool active;
    uint8_t slot;
    uint32_t channel;
    uint16_t acc_len; ///< bytes accumulated toward the next request packet
    uint8_t acc[PC_SFTP_PKT_BUF];
    // streaming write: a WRITE whose data payload arrives across CHANNEL_DATA calls
    bool writing;
    int wr_handle;
    uint64_t wr_off;
    uint32_t wr_remaining;
    uint32_t wr_id;
    bool wr_err;
    SftpHandle handles[PC_SFTP_MAX_HANDLES];
};

struct SshSftpCtx
{
    fs::FS *fs = nullptr;
    const char *root = "/";
    bool registered = false;
    SftpSession sess[MAX_SSH_CONNS];
    uint8_t out[SSH_PKT_BUF_SIZE];  ///< response-build scratch (sends are synchronous, one at a time)
    uint8_t rbuf[PC_SFTP_MAX_READ]; ///< READ scratch
};
SshSftpCtx s_sftp;

// --- handle table ---------------------------------------------------------------------------------
void free_handle(SftpSession *s, int h)
{
    if (h < 0 || h >= PC_SFTP_MAX_HANDLES || !s->handles[h].used)
    {
        return;
    }
    if (s->handles[h].file)
    {
        s->handles[h].file.close();
    }
    s->handles[h].file = fs::File();
    s->handles[h].used = false;
    s->handles[h].has_pending = false;
}
void free_all_handles(SftpSession *s)
{
    for (int i = 0; i < PC_SFTP_MAX_HANDLES; i++)
    {
        free_handle(s, i);
    }
}
int alloc_handle(SftpSession *s)
{
    for (int i = 0; i < PC_SFTP_MAX_HANDLES; i++)
    {
        if (!s->handles[i].used)
        {
            return i;
        }
    }
    return -1;
}
// A handle string is our 4-byte big-endian table index. @return the valid index, or -1.
int handle_index(SftpSession *s, const uint8_t *h, uint32_t hl)
{
    if (hl != 4)
    {
        return -1;
    }
    uint32_t idx = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | (uint32_t)h[3];
    if (idx >= PC_SFTP_MAX_HANDLES || !s->handles[idx].used)
    {
        return -1;
    }
    return (int)idx;
}

// --- helpers --------------------------------------------------------------------------------------
const char *base_name(const char *name)
{
    const char *slash = strrchr(name, '/');
    return slash ? slash + 1 : name;
}

void attrs_from_file(fs::File &f, SftpAttrs *a)
{
    bool dir = f.isDirectory();
    a->flags = PC_SSH_FILEXFER_ATTR_SIZE | PC_SSH_FILEXFER_ATTR_PERMS | PC_SSH_FILEXFER_ATTR_ACMODTIME;
    a->size = dir ? 0 : (uint64_t)f.size();
    a->permissions = dir ? (PC_SFTP_S_IFDIR | 0755) : (PC_SFTP_S_IFREG | 0644);
    uint32_t mt = (uint32_t)f.getLastWrite();
    a->atime = mt;
    a->mtime = mt;
}

// Resolve a client request path to an on-disk path under the mount root. "" and "." mean the root.
int resolve(const uint8_t *path, uint32_t plen, char *out, size_t cap)
{
    char req[PC_SFTP_PATH_MAX];
    if (plen >= sizeof(req))
    {
        return -2;
    }
    memcpy(req, path, plen);
    req[plen] = '\0';
    const char *sub = (plen == 0 || (plen == 1 && req[0] == '.')) ? "/" : req;
    return fs_path_resolve(s_sftp.root, sub, out, cap);
}

void send_resp(SftpSession *s, size_t n)
{
    if (n > 0)
    {
        pc_ssh_conn_send(s->slot, s->channel, s_sftp.out, n);
    }
}
void send_status(SftpSession *s, uint32_t id, uint32_t code, const char *msg)
{
    send_resp(s, pc_sftp_build_status(id, code, msg, s_sftp.out, PC_SFTP_RESP_CAP));
}
void send_handle(SftpSession *s, uint32_t id, int hi)
{
    uint8_t hb[4] = {(uint8_t)((uint32_t)hi >> 24), (uint8_t)((uint32_t)hi >> 16), (uint8_t)((uint32_t)hi >> 8),
                     (uint8_t)hi};
    send_resp(s, pc_sftp_build_handle(id, hb, 4, s_sftp.out, PC_SFTP_RESP_CAP));
}
// Map an fs errno-free open failure to a plausible SFTP status: a write open that fails is FAILURE, a read
// open that fails is NO_SUCH_FILE (the common case a client distinguishes).
uint32_t open_fail_code(bool writing)
{
    return writing ? PC_SSH_FX_FAILURE : PC_SSH_FX_NO_SUCH_FILE;
}

// --- write streaming ------------------------------------------------------------------------------
void write_stream_bytes(SftpSession *s, const uint8_t *data, size_t n)
{
    if (n == 0)
    {
        return;
    }
    if (!s->wr_err && s->wr_handle >= 0)
    {
        fs::File &f = s->handles[s->wr_handle].file;
        if (f.write(data, n) != n)
        {
            s->wr_err = true;
        }
    }
    s->wr_off += n;
    s->wr_remaining -= (uint32_t)n;
}
void finish_write(SftpSession *s)
{
    send_status(s, s->wr_id, s->wr_err ? PC_SSH_FX_FAILURE : PC_SSH_FX_OK, s->wr_err ? "write failed" : "");
    s->writing = false;
}

// --- READDIR --------------------------------------------------------------------------------------
// Serialize one directory entry (filename + longname + attrs) into @p ent; @return its length.
size_t build_entry(fs::File &c, uint8_t *ent, size_t cap)
{
    const char *base = base_name(c.name());
    SftpAttrs a;
    attrs_from_file(c, &a);
    char ln[PC_SFTP_ENTRY_MAX];
    pc_sftp_format_longname(c.isDirectory(), a.permissions, a.size, a.mtime, base, ln, sizeof(ln));
    SftpWriter w;
    pc_sftp_wr_init(&w, ent, cap); // reserves a 4-byte length prefix we discard below
    pc_sftp_wr_string(&w, base, (uint32_t)strnlen(base, PC_SFTP_ENTRY_MAX));
    pc_sftp_wr_string(&w, ln, (uint32_t)strnlen(ln, sizeof(ln)));
    pc_sftp_wr_attrs(&w, &a);
    if (w.ovf)
    {
        return 0;
    }
    size_t el = w.off - 4;
    memmove(ent, ent + 4, el); // drop the reserved prefix so the entry bytes start at ent[0]
    return el;
}

void do_readdir(SftpSession *s, uint32_t id, SftpHandle *H)
{
    if (H->readdir_done && !H->has_pending)
    {
        send_status(s, id, PC_SSH_FX_EOF, "");
        return;
    }
    SftpWriter w;
    pc_sftp_wr_init(&w, s_sftp.out, PC_SFTP_RESP_CAP);
    pc_sftp_wr_u8(&w, PC_SSH_FXP_NAME);
    pc_sftp_wr_u32(&w, id);
    size_t count_at = pc_sftp_wr_pos(&w);
    pc_sftp_wr_u32(&w, 0); // count placeholder
    uint32_t count = 0;

    if (H->has_pending) // an entry that did not fit last call
    {
        pc_sftp_wr_bytes(&w, H->pend, H->pend_len);
        H->has_pending = false;
        count++;
    }
    while (!H->readdir_done)
    {
        fs::File c = H->file.openNextFile();
        if (!c)
        {
            H->readdir_done = true;
            break;
        }
        uint8_t ent[PC_SFTP_ENTRY_MAX];
        size_t el = build_entry(c, ent, sizeof(ent));
        c.close();
        if (el == 0)
        {
            continue; // entry could not be serialized (pathological name) - skip it
        }
        if (pc_sftp_wr_pos(&w) + el > PC_SFTP_RESP_CAP - 8) // would not fit this NAME response
        {
            if (count == 0) // first entry too big for an empty response - emit it anyway (best effort)
            {
                pc_sftp_wr_bytes(&w, ent, el);
                count++;
            }
            else // stash it for the next READDIR
            {
                memcpy(H->pend, ent, el);
                H->pend_len = (uint16_t)el;
                H->has_pending = true;
            }
            break;
        }
        pc_sftp_wr_bytes(&w, ent, el);
        count++;
    }

    if (count == 0) // nothing to return -> end of directory
    {
        send_status(s, id, PC_SSH_FX_EOF, "");
        return;
    }
    pc_sftp_wr_patch_u32(&w, count_at, count);
    size_t n = pc_sftp_wr_finish(&w);
    send_resp(s, n);
}

// --- one complete non-WRITE request ---------------------------------------------------------------
void handle_packet(SftpSession *s, const uint8_t *buf, size_t total)
{
    SftpReader r;
    pc_sftp_rd_init(&r, buf + 4, total - 4);
    uint8_t type = pc_sftp_rd_u8(&r);

    if (type == PC_SSH_FXP_INIT)
    {
        send_resp(s, pc_sftp_build_version(s_sftp.out, PC_SFTP_RESP_CAP));
        return;
    }

    uint32_t id = pc_sftp_rd_u32(&r);
    if (!r.ok)
    {
        return;
    }

    switch (type)
    {
    case PC_SSH_FXP_OPEN: {
        const uint8_t *p = nullptr;
        uint32_t pl = 0;
        if (!pc_sftp_rd_string(&r, &p, &pl))
        {
            send_status(s, id, PC_SSH_FX_BAD_MESSAGE, "");
            return;
        }
        uint32_t pflags = pc_sftp_rd_u32(&r);
        SftpAttrs a;
        pc_sftp_rd_attrs(&r, &a);
        char disk[PC_SFTP_PATH_MAX];
        if (resolve(p, pl, disk, sizeof(disk)) != 0)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        bool writing = (pflags & PC_SSH_FXF_WRITE) != 0;
        const char *mode = writing ? ((pflags & PC_SSH_FXF_APPEND) ? "a" : "w") : "r";
        fs::File f = s_sftp.fs->open(disk, mode);
        if (!f)
        {
            send_status(s, id, open_fail_code(writing), "open failed");
            return;
        }
        if (f.isDirectory())
        {
            f.close();
            send_status(s, id, PC_SSH_FX_FAILURE, "is a directory");
            return;
        }
        int hi = alloc_handle(s);
        if (hi < 0)
        {
            f.close();
            send_status(s, id, PC_SSH_FX_FAILURE, "too many open handles");
            return;
        }
        s->handles[hi].used = true;
        s->handles[hi].is_dir = false;
        s->handles[hi].file = f;
        s->handles[hi].readdir_done = false;
        s->handles[hi].has_pending = false;
        strncpy(s->handles[hi].path, disk, sizeof(s->handles[hi].path) - 1);
        s->handles[hi].path[sizeof(s->handles[hi].path) - 1] = '\0';
        send_handle(s, id, hi);
        return;
    }
    case PC_SSH_FXP_CLOSE: {
        const uint8_t *h = nullptr;
        uint32_t hl = 0;
        pc_sftp_rd_string(&r, &h, &hl);
        int hi = handle_index(s, h, hl);
        if (hi < 0)
        {
            send_status(s, id, PC_SSH_FX_FAILURE, "bad handle");
            return;
        }
        free_handle(s, hi);
        send_status(s, id, PC_SSH_FX_OK, "");
        return;
    }
    case PC_SSH_FXP_READ: {
        const uint8_t *h = nullptr;
        uint32_t hl = 0;
        pc_sftp_rd_string(&r, &h, &hl);
        uint64_t off = pc_sftp_rd_u64(&r);
        uint32_t rlen = pc_sftp_rd_u32(&r);
        int hi = handle_index(s, h, hl);
        if (!r.ok || hi < 0 || s->handles[hi].is_dir)
        {
            send_status(s, id, PC_SSH_FX_FAILURE, "bad handle");
            return;
        }
        fs::File &f = s->handles[hi].file;
        f.seek((uint32_t)off);
        uint32_t want = rlen < PC_SFTP_MAX_READ ? rlen : PC_SFTP_MAX_READ;
        size_t got = f.read(s_sftp.rbuf, want);
        if (got == 0)
        {
            send_status(s, id, PC_SSH_FX_EOF, "");
            return;
        }
        send_resp(s, pc_sftp_build_data(id, s_sftp.rbuf, (uint32_t)got, s_sftp.out, PC_SFTP_RESP_CAP));
        return;
    }
    case PC_SSH_FXP_OPENDIR: {
        const uint8_t *p = nullptr;
        uint32_t pl = 0;
        pc_sftp_rd_string(&r, &p, &pl);
        char disk[PC_SFTP_PATH_MAX];
        if (!r.ok || resolve(p, pl, disk, sizeof(disk)) != 0)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        fs::File d = s_sftp.fs->open(disk, "r");
        if (!d || !d.isDirectory())
        {
            if (d)
            {
                d.close();
            }
            send_status(s, id, PC_SSH_FX_NO_SUCH_FILE, "not a directory");
            return;
        }
        int hi = alloc_handle(s);
        if (hi < 0)
        {
            d.close();
            send_status(s, id, PC_SSH_FX_FAILURE, "too many open handles");
            return;
        }
        s->handles[hi].used = true;
        s->handles[hi].is_dir = true;
        s->handles[hi].file = d;
        s->handles[hi].readdir_done = false;
        s->handles[hi].has_pending = false;
        strncpy(s->handles[hi].path, disk, sizeof(s->handles[hi].path) - 1);
        s->handles[hi].path[sizeof(s->handles[hi].path) - 1] = '\0';
        send_handle(s, id, hi);
        return;
    }
    case PC_SSH_FXP_READDIR: {
        const uint8_t *h = nullptr;
        uint32_t hl = 0;
        pc_sftp_rd_string(&r, &h, &hl);
        int hi = handle_index(s, h, hl);
        if (hi < 0 || !s->handles[hi].is_dir)
        {
            send_status(s, id, PC_SSH_FX_FAILURE, "bad handle");
            return;
        }
        do_readdir(s, id, &s->handles[hi]);
        return;
    }
    case PC_SSH_FXP_STAT:
    case PC_SSH_FXP_LSTAT: {
        const uint8_t *p = nullptr;
        uint32_t pl = 0;
        pc_sftp_rd_string(&r, &p, &pl);
        char disk[PC_SFTP_PATH_MAX];
        if (!r.ok || resolve(p, pl, disk, sizeof(disk)) != 0)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        fs::File f = s_sftp.fs->open(disk, "r");
        if (!f)
        {
            send_status(s, id, PC_SSH_FX_NO_SUCH_FILE, "");
            return;
        }
        SftpAttrs a;
        attrs_from_file(f, &a);
        f.close();
        send_resp(s, pc_sftp_build_attrs(id, &a, s_sftp.out, PC_SFTP_RESP_CAP));
        return;
    }
    case PC_SSH_FXP_FSTAT: {
        const uint8_t *h = nullptr;
        uint32_t hl = 0;
        pc_sftp_rd_string(&r, &h, &hl);
        int hi = handle_index(s, h, hl);
        if (hi < 0)
        {
            send_status(s, id, PC_SSH_FX_FAILURE, "bad handle");
            return;
        }
        SftpAttrs a;
        attrs_from_file(s->handles[hi].file, &a);
        send_resp(s, pc_sftp_build_attrs(id, &a, s_sftp.out, PC_SFTP_RESP_CAP));
        return;
    }
    case PC_SSH_FXP_REMOVE: {
        const uint8_t *p = nullptr;
        uint32_t pl = 0;
        pc_sftp_rd_string(&r, &p, &pl);
        char disk[PC_SFTP_PATH_MAX];
        if (!r.ok || resolve(p, pl, disk, sizeof(disk)) != 0)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        send_status(s, id, s_sftp.fs->remove(disk) ? PC_SSH_FX_OK : PC_SSH_FX_FAILURE, "");
        return;
    }
    case PC_SSH_FXP_MKDIR: {
        const uint8_t *p = nullptr;
        uint32_t pl = 0;
        pc_sftp_rd_string(&r, &p, &pl);
        char disk[PC_SFTP_PATH_MAX];
        if (!r.ok || resolve(p, pl, disk, sizeof(disk)) != 0)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        send_status(s, id, s_sftp.fs->mkdir(disk) ? PC_SSH_FX_OK : PC_SSH_FX_FAILURE, "");
        return;
    }
    case PC_SSH_FXP_RMDIR: {
        const uint8_t *p = nullptr;
        uint32_t pl = 0;
        pc_sftp_rd_string(&r, &p, &pl);
        char disk[PC_SFTP_PATH_MAX];
        if (!r.ok || resolve(p, pl, disk, sizeof(disk)) != 0)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        send_status(s, id, s_sftp.fs->rmdir(disk) ? PC_SSH_FX_OK : PC_SSH_FX_FAILURE, "");
        return;
    }
    case PC_SSH_FXP_RENAME: {
        const uint8_t *op = nullptr;
        uint32_t ol = 0;
        const uint8_t *np = nullptr;
        uint32_t nl = 0;
        pc_sftp_rd_string(&r, &op, &ol);
        pc_sftp_rd_string(&r, &np, &nl);
        char od[PC_SFTP_PATH_MAX];
        char nd[PC_SFTP_PATH_MAX];
        if (!r.ok || resolve(op, ol, od, sizeof(od)) != 0 || resolve(np, nl, nd, sizeof(nd)) != 0)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        send_status(s, id, s_sftp.fs->rename(od, nd) ? PC_SSH_FX_OK : PC_SSH_FX_FAILURE, "");
        return;
    }
    case PC_SSH_FXP_REALPATH: {
        const uint8_t *p = nullptr;
        uint32_t pl = 0;
        pc_sftp_rd_string(&r, &p, &pl);
        if (!r.ok)
        {
            send_status(s, id, PC_SSH_FX_BAD_MESSAGE, "");
            return;
        }
        char req[PC_SFTP_PATH_MAX];
        if (pl >= sizeof(req))
        {
            send_status(s, id, PC_SSH_FX_FAILURE, "path too long");
            return;
        }
        memcpy(req, p, pl);
        req[pl] = '\0';
        if (strstr(req, ".."))
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "traversal");
            return;
        }
        char cpath[PC_SFTP_PATH_MAX + 1];
        if (pl == 0 || (pl == 1 && req[0] == '.'))
        {
            pc_sb sb_cpath = {cpath, sizeof(cpath), 0, true};
            pc_sb_put(&sb_cpath, "/");
            if (pc_sb_finish(&sb_cpath) == 0)
            {
                cpath[0] = '\0';
            }
        }
        else if (req[0] == '/')
        {
            pc_sb sb_cpath2 = {cpath, sizeof(cpath), 0, true};
            pc_sb_put(&sb_cpath2, req);
            if (pc_sb_finish(&sb_cpath2) == 0)
            {
                cpath[0] = '\0';
            }
        }
        else
        {
            pc_sb sb_cpath3 = {cpath, sizeof(cpath), 0, true};
            pc_sb_put(&sb_cpath3, "/");
            pc_sb_put(&sb_cpath3, req);
            if (pc_sb_finish(&sb_cpath3) == 0)
            {
                cpath[0] = '\0';
            }
        }
        SftpAttrs a;
        a.flags = PC_SSH_FILEXFER_ATTR_PERMS;
        a.permissions = PC_SFTP_S_IFDIR | 0755;
        a.size = 0;
        a.atime = a.mtime = 0;
        char ln[64];
        pc_sftp_format_longname(true, a.permissions, 0, 0, cpath, ln, sizeof(ln));
        send_resp(s, pc_sftp_build_name1(id, cpath, ln, &a, s_sftp.out, PC_SFTP_RESP_CAP));
        return;
    }
    case PC_SSH_FXP_SETSTAT:
    case PC_SSH_FXP_FSETSTAT:
        // We do not implement chmod/chown/truncate-via-setstat; accept it (OK) so `put` does not fail on the
        // trailing FSETSTAT that sets mode/mtime - the values are simply not applied.
        send_status(s, id, PC_SSH_FX_OK, "");
        return;
    default:
        send_status(s, id, PC_SSH_FX_OP_UNSUPPORTED, "unsupported");
        return;
    }
}

// --- framing loop ---------------------------------------------------------------------------------
// Consume complete packets from the accumulator. A WRITE switches to streaming mode. @return false to tear the
// channel down (malformed / oversized non-WRITE packet).
bool process_acc(SftpSession *s)
{
    for (;;)
    {
        if (s->acc_len < 5)
        {
            return true; // need the length prefix + the type byte
        }
        uint32_t plen = ((uint32_t)s->acc[0] << 24) | ((uint32_t)s->acc[1] << 16) | ((uint32_t)s->acc[2] << 8) |
                        (uint32_t)s->acc[3];
        if (plen == 0)
        {
            return false; // malformed
        }
        size_t total = (size_t)plen + 4;
        uint8_t type = s->acc[4];

        if (type == PC_SSH_FXP_WRITE)
        {
            SftpReader r;
            pc_sftp_rd_init(&r, s->acc + 4, s->acc_len - 4);
            pc_sftp_rd_u8(&r); // type
            uint32_t id = pc_sftp_rd_u32(&r);
            const uint8_t *h = nullptr;
            uint32_t hl = 0;
            if (!pc_sftp_rd_string(&r, &h, &hl))
            {
                return true; // the handle has not fully arrived - wait
            }
            uint64_t off = pc_sftp_rd_u64(&r);
            uint32_t datalen = pc_sftp_rd_u32(&r);
            if (!r.ok)
            {
                return true; // the WRITE header has not fully arrived - wait
            }

            int hi = handle_index(s, h, hl);
            s->wr_id = id;
            s->wr_remaining = datalen;
            s->wr_off = off;
            s->wr_handle = hi;
            s->wr_err = (hi < 0 || s->handles[hi].is_dir);
            if (!s->wr_err)
            {
                s->handles[hi].file.seek((uint32_t)off);
            }
            s->writing = true;

            size_t hdr = 4 + r.off; // header bytes of this packet (before the data payload)
            size_t have = s->acc_len - hdr;
            size_t chunk = have < datalen ? have : datalen;
            write_stream_bytes(s, s->acc + hdr, chunk);
            size_t consumed = hdr + chunk;
            memmove(s->acc, s->acc + consumed, s->acc_len - consumed);
            s->acc_len -= (uint16_t)consumed;
            if (s->wr_remaining == 0)
            {
                finish_write(s); // all data was already in the accumulator
            }
            if (s->writing)
            {
                return true; // still streaming - the rest arrives as raw channel data
            }
            continue; // write finished within the accumulator; process the next packet
        }

        if (total > sizeof(s->acc))
        {
            return false; // a non-WRITE packet larger than the buffer -> drop the channel
        }
        if (s->acc_len < total)
        {
            return true; // wait for the rest
        }
        handle_packet(s, s->acc, total);
        memmove(s->acc, s->acc + total, s->acc_len - total);
        s->acc_len -= (uint16_t)total;
    }
}

// --- channel callbacks ----------------------------------------------------------------------------
void pc_sftp_on_open(uint8_t slot, uint32_t channel)
{
    if (slot >= MAX_SSH_CONNS)
    {
        return;
    }
    SftpSession *s = &s_sftp.sess[slot];
    free_all_handles(s); // clean any handles lingering from a prior session on this slot
    s->active = true;
    s->slot = slot;
    s->channel = channel;
    s->acc_len = 0;
    s->writing = false;
    s->wr_remaining = 0;
    s->wr_handle = -1;
    // The SFTP VERSION reply is sent when the client's INIT packet arrives.
}

void pc_sftp_on_data(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len)
{
    if (slot >= MAX_SSH_CONNS)
    {
        return;
    }
    SftpSession *s = &s_sftp.sess[slot];
    if (!s->active || s->channel != channel)
    {
        return;
    }
    while (len > 0)
    {
        if (s->writing)
        {
            size_t take = len < s->wr_remaining ? len : s->wr_remaining;
            write_stream_bytes(s, data, take);
            data += take;
            len -= take;
            if (s->wr_remaining == 0)
            {
                finish_write(s);
            }
            continue;
        }
        size_t space = sizeof(s->acc) - s->acc_len;
        if (space == 0)
        {
            pc_ssh_conn_close_channel(slot, s->channel); // a non-WRITE packet too big to buffer
            s->active = false;
            return;
        }
        size_t take = len < space ? len : space;
        memcpy(s->acc + s->acc_len, data, take);
        s->acc_len += (uint16_t)take;
        data += take;
        len -= take;
        if (!process_acc(s))
        {
            pc_ssh_conn_close_channel(slot, s->channel);
            s->active = false;
            return;
        }
    }
}
} // namespace

// --- public API -----------------------------------------------------------------------------------
void pc_ssh_sftp_begin(fs::FS &fs, const char *root)
{
    s_sftp.fs = &fs;
    s_sftp.root = (root && root[0]) ? root : "/";
    for (int i = 0; i < MAX_SSH_CONNS; i++)
    {
        s_sftp.sess[i].active = false;
        free_all_handles(&s_sftp.sess[i]);
    }
    if (!s_sftp.registered)
    {
        pc_ssh_channel_set_sftp_open_cb(pc_sftp_on_open);
        pc_ssh_channel_set_sftp_data_cb(pc_sftp_on_data);
        s_sftp.registered = true;
    }
}

#endif // PC_ENABLE_SSH_SFTP
