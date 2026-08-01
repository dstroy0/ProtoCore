// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_sftp.cpp
 * @brief SFTP server subsystem - the SSH_FXP_* state machine. See ssh_sftp.h.
 *
 * Accumulates SSH_FXP_* request packets from the channel byte stream, runs each against the filesystem
 * accessor, and frames responses back with pc_ssh_conn_send. A large WRITE is streamed straight to the file
 * (never buffered whole); a READ returns a short DATA (the client re-requests). A fixed handle table holds
 * open files and directory cursors.
 *
 * A request path goes to an operation as the bytes the client sent. This file does not join it, does not
 * resolve it, and does not check it for `..` - the accessor owns the root and the traversal guard, and a
 * server that resolved would need a root, a path buffer, and a capacity of its own.
 */

#include "server/ssh_sftp.h"

#if PC_ENABLE_SSH_SFTP

#include "network_drivers/presentation/ssh/connection/ssh_channel.h" // callbacks + setters
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"    // pc_ssh_conn_send / pc_ssh_conn_close_channel
#include "server/filesystem/filesystem.h"
#include "services/file_transfer/sftp/sftp.h"
#include "shared_primitives/endian.h" // the u32 <-> big-endian bytes serializers
#include "shared_primitives/swar.h"   // the bounded word-at-a-time length scan
#include <string.h>

namespace
{
// Leave headroom below one SSH packet for the CHANNEL_DATA framing, so pc_ssh_conn_send never rejects a response.
#define PC_SFTP_RESP_CAP (SSH_PKT_BUF_SIZE - 16)
// Worst-case one READDIR NAME entry (filename + longname + attrs), used to stash an entry that did not fit.
#define PC_SFTP_ENTRY_MAX (PC_FILESYSTEM_PATH_MAX + 320)

struct SftpHandle
{
    bool is_dir;
    int fh;                           ///< the accessor's handle (a dir cursor when is_dir)
    char req[PC_FILESYSTEM_PATH_MAX]; ///< the request path this was opened with; FSTAT stats it
    bool readdir_done;                ///< the directory has been fully listed
    bool has_pending;                 ///< a READDIR entry that did not fit last time, emitted first next time
    uint16_t pend_len;
    uint8_t pend[PC_SFTP_ENTRY_MAX];
};

struct SftpSession
{
    bool active;
    uint8_t slot;
    uint32_t channel;
    // Which handles are open, one bit each. In-use state is a single bit, so the whole table's
    // answer fits in a register: allocation is one bit scan instead of a walk, releasing them all
    // touches only the ones actually open, and resetting the table is a store rather than a loop.
    uint32_t open_mask;
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

// All SFTP state in one owned instance (internal linkage), work buffers included: a stack array is the one
// allocation the fixed-footprint accounting cannot see. Requests are served one at a time, so one of each
// buffer serves every session - none of them outlives the request that fills it.
struct SshSftpCtx
{
    bool registered = false;
    // The root this server works through, bound once in begin(). It is a handle, not a path: this
    // file cannot name where storage begins, and holding the handle is what lets SCP land on a card
    // while SFTP serves a RAM pool. -1 until bound, which fails every operation closed rather than
    // resolving against root 0 - somebody else's storage.
    int root = -1;
    SftpSession sess[MAX_SSH_CONNS];
    uint8_t out[SSH_PKT_BUF_SIZE];  ///< response-build scratch (sends are synchronous, one at a time)
    uint8_t rbuf[PC_SFTP_MAX_READ]; ///< READ scratch
    // Two request paths, because two operations need two live at once: RENAME (source and destination) and
    // REALPATH (the client's path and its canonical form). One byte over the accepted request length, which
    // is what canonicalizing can add - REALPATH answers against "/" and a request need not carry the leading
    // separator itself.
    char req[2][PC_FILESYSTEM_PATH_MAX + 1];
    uint8_t ent[PC_SFTP_ENTRY_MAX];  ///< one serialized READDIR entry
    char ln[PC_SFTP_ENTRY_MAX];      ///< its `ls -l` longname
    char nm[PC_FILESYSTEM_PATH_MAX]; ///< the entry's own name, as the accessor reports it
    uint8_t hb[4];                   ///< a handle, big-endian, on its way into a HANDLE response
};
SshSftpCtx s_sftp;

// --- handle table ---------------------------------------------------------------------------------
static_assert(PC_SFTP_MAX_HANDLES > 0 && PC_SFTP_MAX_HANDLES <= 32,
              "the open-handle set is one 32-bit word, one bit per handle");
#define PC_SFTP_HANDLE_BITS ((uint32_t)(((uint64_t)1 << PC_SFTP_MAX_HANDLES) - 1u))

bool handle_open(const SftpSession *s, int h)
{
    return h >= 0 && h < PC_SFTP_MAX_HANDLES && (s->open_mask & (1u << h)) != 0;
}
void free_handle(SftpSession *s, int h)
{
    if (!handle_open(s, h))
    {
        return;
    }
    pc_fs_close(s->handles[h].fh); // the bit is what says fh is real, so it needs no second marker
    s->open_mask &= ~(1u << h);
    s->handles[h].has_pending = false;
}
void free_all_handles(SftpSession *s)
{
    // Walks the set bits, not the table: a session with one file open closes one file.
    while (s->open_mask != 0)
    {
        free_handle(s, __builtin_ctz(s->open_mask));
    }
}
int alloc_handle(SftpSession *s)
{
    uint32_t free_bits = ~s->open_mask & PC_SFTP_HANDLE_BITS;
    return (free_bits == 0) ? -1 : (int)__builtin_ctz(free_bits);
}
// A handle string is our 4-byte big-endian table index. @return the valid index, or -1.
int handle_index(SftpSession *s, const uint8_t *h, uint32_t hl)
{
    if (hl != 4)
    {
        return -1;
    }
    uint32_t idx = pc_rd32be(h);
    return handle_open(s, (int)idx) ? (int)idx : -1;
}

// --- helpers --------------------------------------------------------------------------------------
// Take a wire path (length-prefixed, not NUL-terminated) into request slot @p slot as a C string. "" and "."
// both name the mount root. Nothing is resolved here; the string that comes back is the client's own bytes.
const char *req_path(int slot, const uint8_t *p, uint32_t plen)
{
    if (p == nullptr || plen >= PC_FILESYSTEM_PATH_MAX)
    {
        return nullptr;
    }
    memcpy(s_sftp.req[slot], p, plen);
    s_sftp.req[slot][plen] = '\0';
    if (plen == 0 || (plen == 1 && s_sftp.req[slot][0] == '.'))
    {
        s_sftp.req[slot][0] = '/';
        s_sftp.req[slot][1] = '\0';
    }
    return s_sftp.req[slot];
}

void attrs_from_stat(const pc_mnt_stat *st, SftpAttrs *a)
{
    a->flags = PC_SSH_FILEXFER_ATTR_SIZE | PC_SSH_FILEXFER_ATTR_PERMS | PC_SSH_FILEXFER_ATTR_ACMODTIME;
    a->size = st->is_dir ? 0 : st->size;
    a->permissions = st->is_dir ? (PC_SFTP_S_IFDIR | 0755) : (PC_SFTP_S_IFREG | 0644);
    a->atime = st->mtime;
    a->mtime = st->mtime;
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
    // The serializer returns the width it wrote, which is the length the HANDLE string carries.
    size_t n = pc_wr32be(s_sftp.hb, (uint32_t)hi);
    send_resp(s, pc_sftp_build_handle(id, s_sftp.hb, n, s_sftp.out, PC_SFTP_RESP_CAP));
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
        if (pc_fs_write(s->handles[s->wr_handle].fh, data, n) != (int)n)
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
// Serialize one directory entry (filename + longname + attrs) into the entry buffer; @return its length.
size_t build_entry(const pc_mnt_stat *st, const char *name, size_t name_len)
{
    SftpAttrs a;
    attrs_from_stat(st, &a);
    // The formatter returns what it wrote, so the longname is not rescanned to find out how long the
    // call that just built it made it.
    size_t ln_len =
        pc_sftp_format_longname(st->is_dir, a.permissions, a.size, a.mtime, name, s_sftp.ln, sizeof(s_sftp.ln));
    SftpWriter w;
    pc_sftp_wr_init(&w, s_sftp.ent, sizeof(s_sftp.ent)); // reserves a 4-byte length prefix we discard below
    pc_sftp_wr_string(&w, name, (uint32_t)name_len);
    pc_sftp_wr_string(&w, s_sftp.ln, (uint32_t)ln_len);
    pc_sftp_wr_attrs(&w, &a);
    if (w.ovf)
    {
        return 0;
    }
    size_t el = w.off - 4;
    memmove(s_sftp.ent, s_sftp.ent + 4, el); // drop the reserved prefix so the entry bytes start at ent[0]
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
        pc_mnt_stat st;
        if (!pc_fs_readdir(H->fh, &st, s_sftp.nm, sizeof(s_sftp.nm)))
        {
            H->readdir_done = true;
            break;
        }
        size_t el = build_entry(&st, s_sftp.nm, pc_swar_scan_nul(s_sftp.nm, sizeof(s_sftp.nm)));
        if (el == 0)
        {
            continue; // entry could not be serialized (pathological name) - skip it
        }
        if (pc_sftp_wr_pos(&w) + el > PC_SFTP_RESP_CAP - 8) // would not fit this NAME response
        {
            if (count == 0) // first entry too big for an empty response - emit it anyway (best effort)
            {
                pc_sftp_wr_bytes(&w, s_sftp.ent, el);
                count++;
            }
            else // stash it for the next READDIR
            {
                memcpy(H->pend, s_sftp.ent, el);
                H->pend_len = (uint16_t)el;
                H->has_pending = true;
            }
            break;
        }
        pc_sftp_wr_bytes(&w, s_sftp.ent, el);
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

// Remember which request path a handle was opened with, so FSTAT can answer without a second kind of
// handle-to-path map. The accessor resolves it again; it is the client's bytes, not a resolved path.
void keep_req(SftpHandle *H, const char *req)
{
    size_t n = pc_swar_scan_nul(req, sizeof(H->req));
    if (n >= sizeof(H->req))
    {
        n = sizeof(H->req) - 1;
    }
    memcpy(H->req, req, n);
    H->req[n] = '\0';
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
        const char *req = req_path(0, p, pl);
        if (req == nullptr)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        bool writing = (pflags & PC_SSH_FXF_WRITE) != 0;
        pc_mnt_mode mode = !writing                       ? pc_mnt_mode::PC_MNT_READ
                           : (pflags & PC_SSH_FXF_APPEND) ? pc_mnt_mode::PC_MNT_APPEND
                                                          : pc_mnt_mode::PC_MNT_WRITE;
        int hi = alloc_handle(s);
        if (hi < 0)
        {
            send_status(s, id, PC_SSH_FX_FAILURE, "too many open handles");
            return;
        }
        int fh = pc_fs_open(s_sftp.root, req, "", mode);
        if (fh < 0)
        {
            // A backend refuses to open a directory as a file, so the distinction the client sees is
            // recovered from the record rather than from a second open.
            pc_mnt_stat st;
            bool is_dir = pc_fs_stat(s_sftp.root, req, "", &st) && st.is_dir;
            // NO_SUCH_FILE only for a plain read that found nothing - that is the one case a client
            // acts on differently. A directory, or any failed write, is FAILURE.
            send_status(s, id, (is_dir || writing) ? PC_SSH_FX_FAILURE : PC_SSH_FX_NO_SUCH_FILE,
                        is_dir ? "is a directory" : "open failed");
            return;
        }
        s->open_mask |= (1u << hi);
        s->handles[hi].is_dir = false;
        s->handles[hi].fh = fh;
        s->handles[hi].readdir_done = false;
        s->handles[hi].has_pending = false;
        keep_req(&s->handles[hi], req);
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
        pc_fs_seek(s->handles[hi].fh, off);
        uint32_t want = rlen < PC_SFTP_MAX_READ ? rlen : PC_SFTP_MAX_READ;
        int got = pc_fs_read(s->handles[hi].fh, s_sftp.rbuf, want);
        if (got <= 0)
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
        const char *req = r.ok ? req_path(0, p, pl) : nullptr;
        if (req == nullptr)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        int hi = alloc_handle(s);
        if (hi < 0)
        {
            send_status(s, id, PC_SSH_FX_FAILURE, "too many open handles");
            return;
        }
        int fh = pc_fs_opendir(s_sftp.root, req, "");
        if (fh < 0)
        {
            send_status(s, id, PC_SSH_FX_NO_SUCH_FILE, "not a directory");
            return;
        }
        s->open_mask |= (1u << hi);
        s->handles[hi].is_dir = true;
        s->handles[hi].fh = fh;
        s->handles[hi].readdir_done = false;
        s->handles[hi].has_pending = false;
        keep_req(&s->handles[hi], req);
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
        const char *req = r.ok ? req_path(0, p, pl) : nullptr;
        if (req == nullptr)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        pc_mnt_stat st;
        if (!pc_fs_stat(s_sftp.root, req, "", &st))
        {
            send_status(s, id, PC_SSH_FX_NO_SUCH_FILE, "");
            return;
        }
        SftpAttrs a;
        attrs_from_stat(&st, &a);
        send_resp(s, pc_sftp_build_attrs(id, &a, s_sftp.out, PC_SFTP_RESP_CAP));
        return;
    }
    case PC_SSH_FXP_FSTAT: {
        const uint8_t *h = nullptr;
        uint32_t hl = 0;
        pc_sftp_rd_string(&r, &h, &hl);
        int hi = handle_index(s, h, hl);
        pc_mnt_stat st;
        if (hi < 0 || !pc_fs_stat(s_sftp.root, s->handles[hi].req, "", &st))
        {
            send_status(s, id, PC_SSH_FX_FAILURE, "bad handle");
            return;
        }
        SftpAttrs a;
        attrs_from_stat(&st, &a);
        send_resp(s, pc_sftp_build_attrs(id, &a, s_sftp.out, PC_SFTP_RESP_CAP));
        return;
    }
    case PC_SSH_FXP_REMOVE: {
        const uint8_t *p = nullptr;
        uint32_t pl = 0;
        pc_sftp_rd_string(&r, &p, &pl);
        const char *req = r.ok ? req_path(0, p, pl) : nullptr;
        if (req == nullptr)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        send_status(s, id, pc_fs_remove(s_sftp.root, req, "") ? PC_SSH_FX_OK : PC_SSH_FX_FAILURE, "");
        return;
    }
    case PC_SSH_FXP_MKDIR: {
        const uint8_t *p = nullptr;
        uint32_t pl = 0;
        pc_sftp_rd_string(&r, &p, &pl);
        const char *req = r.ok ? req_path(0, p, pl) : nullptr;
        if (req == nullptr)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        send_status(s, id, pc_fs_mkdir(s_sftp.root, req, "") ? PC_SSH_FX_OK : PC_SSH_FX_FAILURE, "");
        return;
    }
    case PC_SSH_FXP_RMDIR: {
        const uint8_t *p = nullptr;
        uint32_t pl = 0;
        pc_sftp_rd_string(&r, &p, &pl);
        const char *req = r.ok ? req_path(0, p, pl) : nullptr;
        if (req == nullptr)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        send_status(s, id, pc_fs_rmdir(s_sftp.root, req, "") ? PC_SSH_FX_OK : PC_SSH_FX_FAILURE, "");
        return;
    }
    case PC_SSH_FXP_RENAME: {
        const uint8_t *op = nullptr;
        uint32_t ol = 0;
        const uint8_t *np = nullptr;
        uint32_t nl = 0;
        pc_sftp_rd_string(&r, &op, &ol);
        pc_sftp_rd_string(&r, &np, &nl);
        const char *from = r.ok ? req_path(0, op, ol) : nullptr;
        const char *to = (from != nullptr) ? req_path(1, np, nl) : nullptr;
        if (to == nullptr)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        send_status(s, id, pc_fs_rename(s_sftp.root, from, "", to, "") ? PC_SSH_FX_OK : PC_SSH_FX_FAILURE, "");
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
        const char *req = req_path(0, p, pl);
        if (req == nullptr)
        {
            send_status(s, id, PC_SSH_FX_FAILURE, "path too long");
            return;
        }
        // REALPATH answers in the client's namespace, so it canonicalizes against "/" rather than the
        // mount root. This is the accessor's own canonicalizer, called with the root the answer is in.
        int rc = pc_fs_resolve("/", req, "", s_sftp.req[1], sizeof(s_sftp.req[1]));
        if (rc == -1)
        {
            send_status(s, id, PC_SSH_FX_PERMISSION_DENIED, "traversal");
            return;
        }
        if (rc != 0)
        {
            send_status(s, id, PC_SSH_FX_FAILURE, "path too long");
            return;
        }
        SftpAttrs a;
        a.flags = PC_SSH_FILEXFER_ATTR_PERMS;
        a.permissions = PC_SFTP_S_IFDIR | 0755;
        a.size = 0;
        a.atime = a.mtime = 0;
        pc_sftp_format_longname(true, a.permissions, 0, 0, s_sftp.req[1], s_sftp.ln, sizeof(s_sftp.ln));
        send_resp(s, pc_sftp_build_name1(id, s_sftp.req[1], s_sftp.ln, &a, s_sftp.out, PC_SFTP_RESP_CAP));
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
        uint32_t plen = pc_rd32be(s->acc);
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
                pc_fs_seek(s->handles[hi].fh, off);
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
void pc_ssh_sftp_begin(void)
{
    // Bind the root this server answers from. The name is what the accessor maps; two servers naming
    // the same one share it and cost one entry, and naming different ones is how they end up over
    // different storage without either being able to tell.
    s_sftp.root = pc_fs_begin("mnt/sftp");

    for (int i = 0; i < MAX_SSH_CONNS; i++)
    {
        s_sftp.sess[i].active = false;
        // Close whatever a previous run left open, then the table is empty. BSS starts the mask at
        // zero, so a first call closes nothing - it never mistakes handle 0 for an open file.
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
