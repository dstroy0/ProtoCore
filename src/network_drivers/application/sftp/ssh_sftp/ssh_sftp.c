// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_sftp.c
 * @brief SFTP server subsystem - the SSH_FXP_* state machine. See ssh_sftp.h.
 *
 * Accumulates SSH_FXP_* request packets from the channel byte stream, runs each against the filesystem
 * accessor, and frames responses back with protocore_ssh_channel_send_data. A large WRITE is streamed straight to the
 * file (never buffered whole); a READ returns a short DATA (the client re-requests). A fixed handle table holds open
 * files and directory cursors.
 *
 * A request path goes to an operation as the bytes the client sent. This file does not join it, does not
 * resolve it, and does not check it for `..` - the accessor owns the root and the traversal guard, and a
 * server that resolved would need a root, a path buffer, and a capacity of its own.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SSH_SFTP

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "network_drivers/application/sftp/ssh_sftp/ssh_sftp.h"
#include "network_drivers/session/session.h" // sftp_sess: the session the connection carries

static uint8_t sftp_work[16]; // the borrow an entry takes; Sftp never reads it

#include "mmgr/endian/endian.h"     // the u32 <-> big-endian bytes serializers
#include "mmgr/protostr/protostr.h" // str: the bounded-run walks
#include "network_drivers/application/sftp/sftp/sftp.h"
#include "network_drivers/presentation/ssh/connection/connection.h" // callbacks + setters
#include "network_drivers/presentation/ssh/network/network.h" // protocore_ssh_channel_send_data / protocore_ssh_channel_send_close
#include "server/storage/filesystem/filesystem.h"

PROTOCORE_BEGIN_DECLS

// Leave headroom below one SSH packet for the CHANNEL_DATA framing, so protocore_ssh_channel_send_data never rejects a
// response.
#define PROTOCORE_SFTP_RESP_CAP (SSH_PKT_BUF_SIZE - 16)

// All SFTP state in one owner with internal linkage, work buffers included: a stack array is the one
// allocation the fixed-footprint accounting cannot see. Requests are served one at a time, so one of each
// buffer serves every session - none of them outlives the request that fills it.
typedef struct
{
    proto_bool registered;
    // The root this server works through, bound once in begin(). It is a handle, not a path: this
    // file cannot name where storage begins, and holding the handle is what lets SCP land on a card
    // while SFTP serves a RAM pool.
    int root;
    uint8_t out[SSH_PKT_BUF_SIZE];         ///< response-build scratch (sends are synchronous, one at a time)
    uint8_t rbuf[PROTOCORE_SFTP_MAX_READ]; ///< READ scratch
    // Two request paths, because two operations need two live at once: RENAME (source and destination) and
    // REALPATH (the client's path and its canonical form). One byte over the accepted request length, which
    // is what canonicalizing can add - REALPATH answers against "/" and a request need not carry the leading
    // separator itself.
    char req[2][PROTOCORE_FILESYSTEM_PATH_MAX + 1];
    uint8_t ent[PROTOCORE_SFTP_ENTRY_MAX];  ///< one serialized READDIR entry
    char ln[PROTOCORE_SFTP_ENTRY_MAX];      ///< its `ls -l` longname
    char nm[PROTOCORE_FILESYSTEM_PATH_MAX]; ///< the entry's own name, as the accessor reports it
    uint8_t hb[4];                          ///< a handle, big-endian, on its way into a HANDLE response
} SshSftpCtx;

// -1 until bound, not the 0 static storage would give: root 0 is a valid root, so a zeroed field
// would resolve against somebody else's storage before protocore_ssh_sftp_begin() ran.
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SSH_SFTP_OFF_CTX 0u
static_assert(SSH_SFTP_OFF_CTX + sizeof(SshSftpCtx) <= PROTOCORE_SSH_SFTP_BORROW,
              "PROTOCORE_SSH_SFTP_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(SSH_SFTP_OFF_CTX % _Alignof(SshSftpCtx) == 0,
              "SSH_SFTP_OFF_CTX is not a multiple of alignof(SshSftpCtx) - SSH_SFTP_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define SSH_SFTP_CTX(w) ((SshSftpCtx *)(void *)((w) + SSH_SFTP_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SSH_SFTP_BORROW persistent bytes
} SshSftpOwnCtx;
static SshSftpOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ssh_sftp_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_SSH_SFTP_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        SSH_SFTP_CTX(s_own.span)->root = -1;
    }
    return s_own.span;
}

// --- handle table ---------------------------------------------------------------------------------
static_assert(PROTOCORE_SFTP_MAX_HANDLES > 0 && PROTOCORE_SFTP_MAX_HANDLES <= 32,
              "the open-handle set is one 32-bit word, one bit per handle");
#define PROTOCORE_SFTP_HANDLE_BITS ((uint32_t)(((uint64_t)1 << PROTOCORE_SFTP_MAX_HANDLES) - 1u))

static proto_bool handle_open(const SftpSession *s, int h)
{
    return h >= 0 && h < PROTOCORE_SFTP_MAX_HANDLES && (s->open_mask & (1u << h)) != 0;
}
static void free_handle(SftpSession *s, int h)
{
    if (!handle_open(s, h))
    {
        return;
    }
    Fs.io.handle = s->handles[h].fh; // the bit is what says fh is real, so it needs no second marker
    Fs.close(protocore_filesystem_span());
    s->open_mask &= ~(1u << h);
    s->handles[h].has_pending = PROTO_FALSE;
}
static void free_all_handles(SftpSession *s)
{
    // Walks the set bits, not the table: a session with one file open closes one file.
    while (s->open_mask != 0)
    {
        free_handle(s, __builtin_ctz(s->open_mask));
    }
}
static int alloc_handle(SftpSession *s)
{
    uint32_t free_bits = ~s->open_mask & PROTOCORE_SFTP_HANDLE_BITS;
    return (free_bits == 0) ? -1 : (int)__builtin_ctz(free_bits);
}
// A handle string is our 4-byte big-endian table index. @return the valid index, or -1.
static int handle_index(SftpSession *s, const uint8_t *h, uint32_t hl)
{
    if (hl != 4)
    {
        return -1;
    }
    uint32_t idx = endian.rd32be(h);
    return handle_open(s, (int)idx) ? (int)idx : -1;
}

// --- helpers --------------------------------------------------------------------------------------
// Take a wire path (length-prefixed, not NUL-terminated) into request slot @p slot as a C string. "" and "."
// both name the mount root. Nothing is resolved here; the string that comes back is the client's own bytes.
static const char *req_path(uint8_t *restrict work, int slot, const uint8_t *p, uint32_t plen)
{
    if (p == NULL || plen >= PROTOCORE_FILESYSTEM_PATH_MAX)
    {
        return NULL;
    }
    mem.cpy(SSH_SFTP_CTX(work)->req[slot], p, plen);
    SSH_SFTP_CTX(work)->req[slot][plen] = '\0';
    if (plen == 0 || (plen == 1 && SSH_SFTP_CTX(work)->req[slot][0] == '.'))
    {
        SSH_SFTP_CTX(work)->req[slot][0] = '/';
        SSH_SFTP_CTX(work)->req[slot][1] = '\0';
    }
    return SSH_SFTP_CTX(work)->req[slot];
}

static void attrs_from_stat(const protocore_mnt_stat *st, SftpAttrs *a)
{
    a->flags =
        PROTOCORE_SSH_FILEXFER_ATTR_SIZE | PROTOCORE_SSH_FILEXFER_ATTR_PERMS | PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME;
    a->size = st->is_dir ? 0 : st->size;
    a->permissions = st->is_dir ? (PROTOCORE_SFTP_S_IFDIR | 0755) : (PROTOCORE_SFTP_S_IFREG | 0644);
    a->atime = st->mtime;
    a->mtime = st->mtime;
}

// A refused send is a peer that is gone, and a gone peer sends nothing back: end the session
// rather than writing further responses into a channel nobody reads.
static void send_resp(uint8_t *restrict work, SftpSession *s, size_t n)
{
    if (n == 0)
    {
        return;
    }
    SshConnectionV.chan.slot = s->slot;
    SshConnectionV.chan.channel = s->channel;
    SshConnectionV.chan.data = SSH_SFTP_CTX(work)->out;
    SshConnectionV.chan.len = n;
    SshConnection.channel_send_data(protocore_ssh_connection_span());
    if (SshConnectionV.i32 < 0)
    {
        SshConnectionV.chan.slot = s->slot;
        SshConnectionV.chan.channel = s->channel;
        SshConnection.channel_send_close(protocore_ssh_connection_span());
        s->active = PROTO_FALSE;
    }
}
static void send_status(uint8_t *restrict work, SftpSession *s, uint32_t id, uint32_t code, const char *msg)
{
    SftpV.build_status_args.id = id;
    SftpV.build_status_args.code = code;
    SftpV.build_status_args.msg = msg;
    SftpV.build_status_args.out = SSH_SFTP_CTX(work)->out;
    SftpV.build_status_args.cap = PROTOCORE_SFTP_RESP_CAP;
    Sftp.build_status(sftp_work);
    send_resp(work, s, SftpV.n);
}
static void send_handle(uint8_t *restrict work, SftpSession *s, uint32_t id, int hi)
{
    // The serializer returns the width it wrote, which is the length the HANDLE string carries.
    size_t n = endian.wr32be(SSH_SFTP_CTX(work)->hb, (uint32_t)hi);
    SftpV.build_handle_args.id = id;
    SftpV.build_handle_args.handle = SSH_SFTP_CTX(work)->hb;
    SftpV.build_handle_args.hlen = n;
    SftpV.build_handle_args.out = SSH_SFTP_CTX(work)->out;
    SftpV.build_handle_args.cap = PROTOCORE_SFTP_RESP_CAP;
    Sftp.build_handle(sftp_work);
    send_resp(work, s, SftpV.n);
}
// --- write streaming ------------------------------------------------------------------------------
static void write_stream_bytes(SftpSession *s, const uint8_t *data, size_t n)
{
    if (n == 0)
    {
        return;
    }
    if (!s->wr_err && s->wr_handle >= 0)
    {
        Fs.io.handle = s->handles[s->wr_handle].fh;
        Fs.io.wbuf = data;
        Fs.io.n = n;
        Fs.write(protocore_filesystem_span());
        if (Fs.i32 != (int)n)
        {
            s->wr_err = PROTO_TRUE;
        }
    }
    s->wr_off += n;
    s->wr_remaining -= (uint32_t)n;
}
static void finish_write(uint8_t *restrict work, SftpSession *s)
{
    send_status(work, s, s->wr_id, s->wr_err ? PROTOCORE_SSH_FX_FAILURE : PROTOCORE_SSH_FX_OK,
                s->wr_err ? "write failed" : "");
    s->writing = PROTO_FALSE;
}

// --- READDIR --------------------------------------------------------------------------------------
// Serialize one directory entry (filename + longname + attrs) into the entry buffer; @return its length.
static size_t build_entry(uint8_t *restrict work, const protocore_mnt_stat *st, const char *name, size_t name_len)
{
    SftpAttrs a = {0};
    attrs_from_stat(st, &a);
    // The formatter returns what it wrote, so the longname is not rescanned to find out how long the
    // call that just built it made it.
    SftpV.format_longname_args.is_dir = st->is_dir;
    SftpV.format_longname_args.perms = a.permissions;
    SftpV.format_longname_args.size = a.size;
    SftpV.format_longname_args.mtime = a.mtime;
    SftpV.format_longname_args.name = name;
    SftpV.format_longname_args.out = SSH_SFTP_CTX(work)->ln;
    SftpV.format_longname_args.cap = sizeof(SSH_SFTP_CTX(work)->ln);
    Sftp.format_longname(sftp_work);
    size_t ln_len = SftpV.n;
    SftpWriter w;
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = SSH_SFTP_CTX(work)->ent;
    SftpV.wr_init_args.cap = sizeof(SSH_SFTP_CTX(work)->ent);
    Sftp.wr_init(sftp_work); // reserves a 4-byte length prefix we discard below
    SftpV.wr_string_args.w = &w;
    SftpV.wr_string_args.s = name;
    SftpV.wr_string_args.n = (uint32_t)name_len;
    Sftp.wr_string(sftp_work);
    SftpV.wr_string_args.w = &w;
    SftpV.wr_string_args.s = SSH_SFTP_CTX(work)->ln;
    SftpV.wr_string_args.n = (uint32_t)ln_len;
    Sftp.wr_string(sftp_work);
    SftpV.wr_attrs_args.w = &w;
    SftpV.wr_attrs_args.a = &a;
    Sftp.wr_attrs(sftp_work);
    if (w.ovf)
    {
        return 0;
    }
    size_t el = w.off - 4;
    mem.move(SSH_SFTP_CTX(work)->ent, SSH_SFTP_CTX(work)->ent + 4,
             el); // drop the reserved prefix so the entry bytes start at ent[0]
    return el;
}

static void do_readdir(uint8_t *restrict work, SftpSession *s, uint32_t id, SftpHandle *H)
{
    if (H->readdir_done && !H->has_pending)
    {
        send_status(work, s, id, PROTOCORE_SSH_FX_EOF, "");
        return;
    }
    SftpWriter w;
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = SSH_SFTP_CTX(work)->out;
    SftpV.wr_init_args.cap = PROTOCORE_SFTP_RESP_CAP;
    Sftp.wr_init(sftp_work);
    SftpV.wr_u8_args.w = &w;
    SftpV.wr_u8_args.v = PROTOCORE_SSH_FXP_NAME;
    Sftp.wr_u8(sftp_work);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = id;
    Sftp.wr_u32(sftp_work);
    SftpV.wr_pos_args.w = &w;
    Sftp.wr_pos(sftp_work);
    size_t count_at = SftpV.n;
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = 0;
    Sftp.wr_u32(sftp_work); // count placeholder
    uint32_t count = 0;

    if (H->has_pending) // an entry that did not fit last call
    {
        SftpV.wr_bytes_args.w = &w;
        SftpV.wr_bytes_args.b = H->pend;
        SftpV.wr_bytes_args.n = H->pend_len;
        Sftp.wr_bytes(sftp_work);
        H->has_pending = PROTO_FALSE;
        count++;
    }
    while (!H->readdir_done)
    {
        protocore_mnt_stat st = {0};
        Fs.io.handle = H->fh;
        Fs.io.stat = &st;
        Fs.io.name_out = SSH_SFTP_CTX(work)->nm;
        Fs.io.name_cap = sizeof(SSH_SFTP_CTX(work)->nm);
        Fs.readdir(protocore_filesystem_span());
        if (!Fs.ok)
        {
            H->readdir_done = PROTO_TRUE;
            break;
        }
        size_t el = build_entry(work, &st, SSH_SFTP_CTX(work)->nm,
                                str.len(SSH_SFTP_CTX(work)->nm, sizeof(SSH_SFTP_CTX(work)->nm)));
        if (el == 0)
        {
            continue; // entry could not be serialized (pathological name) - skip it
        }
        SftpV.wr_pos_args.w = &w;
        Sftp.wr_pos(sftp_work);
        if (SftpV.n + el > PROTOCORE_SFTP_RESP_CAP - 8) // would not fit this NAME response
        {
            if (count == 0) // first entry too big for an empty response - emit it anyway (best effort)
            {
                SftpV.wr_bytes_args.w = &w;
                SftpV.wr_bytes_args.b = SSH_SFTP_CTX(work)->ent;
                SftpV.wr_bytes_args.n = el;
                Sftp.wr_bytes(sftp_work);
                count++;
            }
            else // stash it for the next READDIR
            {
                mem.cpy(H->pend, SSH_SFTP_CTX(work)->ent, el);
                H->pend_len = (uint16_t)el;
                H->has_pending = PROTO_TRUE;
            }
            break;
        }
        SftpV.wr_bytes_args.w = &w;
        SftpV.wr_bytes_args.b = SSH_SFTP_CTX(work)->ent;
        SftpV.wr_bytes_args.n = el;
        Sftp.wr_bytes(sftp_work);
        count++;
    }

    if (count == 0) // nothing to return -> end of directory
    {
        send_status(work, s, id, PROTOCORE_SSH_FX_EOF, "");
        return;
    }
    SftpV.wr_patch_u32_args.w = &w;
    SftpV.wr_patch_u32_args.at = count_at;
    SftpV.wr_patch_u32_args.v = count;
    Sftp.wr_patch_u32(sftp_work);
    SftpV.wr_finish_args.w = &w;
    Sftp.wr_finish(sftp_work);
    size_t n = SftpV.n;
    send_resp(work, s, n);
}

// Remember which request path a handle was opened with, so FSTAT can answer without a second kind of
// handle-to-path map. The accessor resolves it again; it is the client's bytes, not a resolved path.
static void keep_req(SftpHandle *H, const char *req)
{
    size_t n = str.len(req, sizeof(H->req));
    if (n >= sizeof(H->req))
    {
        n = sizeof(H->req) - 1;
    }
    mem.cpy(H->req, req, n);
    H->req[n] = '\0';
}

// --- one complete non-WRITE request ---------------------------------------------------------------
static void handle_packet(uint8_t *restrict work, SftpSession *s, const uint8_t *buf, size_t total)
{
    SftpReader r;
    SftpV.rd_init_args.r = &r;
    SftpV.rd_init_args.payload = buf + 4;
    SftpV.rd_init_args.len = total - 4;
    Sftp.rd_init(sftp_work);
    SftpV.rd_u8_args.r = &r;
    Sftp.rd_u8(sftp_work);
    uint8_t type = SftpV.value;

    if (type == PROTOCORE_SSH_FXP_INIT)
    {
        SftpV.build_version_args.out = SSH_SFTP_CTX(work)->out;
        SftpV.build_version_args.cap = PROTOCORE_SFTP_RESP_CAP;
        Sftp.build_version(sftp_work);
        send_resp(work, s, SftpV.n);
        return;
    }

    SftpV.rd_u32_args.r = &r;
    Sftp.rd_u32(sftp_work);
    uint32_t id = SftpV.u32;
    if (!r.ok)
    {
        return;
    }

    switch (type)
    {
    case PROTOCORE_SSH_FXP_OPEN: {
        const uint8_t *p = NULL;
        uint32_t pl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &p;
        SftpV.rd_string_args.out_len = &pl;
        Sftp.rd_string(sftp_work);
        if (!SftpV.ok)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_BAD_MESSAGE, "");
            return;
        }
        SftpV.rd_u32_args.r = &r;
        Sftp.rd_u32(sftp_work);
        uint32_t pflags = SftpV.u32;
        SftpAttrs a = {0};
        SftpV.rd_attrs_args.r = &r;
        SftpV.rd_attrs_args.a = &a;
        Sftp.rd_attrs(sftp_work);
        (void)SftpV.ok; // read to advance the reader past the attrs; the open flags decide the mode
        const char *req = req_path(work, 0, p, pl);
        if (req == NULL)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        proto_bool writing = (pflags & PROTOCORE_SSH_FXF_WRITE) != 0;
        protocore_mnt_mode mode = !writing                              ? PROTOCORE_MNT_READ
                                  : (pflags & PROTOCORE_SSH_FXF_APPEND) ? PROTOCORE_MNT_APPEND
                                                                        : PROTOCORE_MNT_WRITE;
        int hi = alloc_handle(s);
        if (hi < 0)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_FAILURE, "too many open handles");
            return;
        }
        Fs.path.root = SSH_SFTP_CTX(work)->root;
        Fs.path.dir = req;
        Fs.path.name = "";
        Fs.io.mode = mode;
        Fs.open(protocore_filesystem_span());
        int fh = Fs.i32;
        if (fh < 0)
        {
            // A backend refuses to open a directory as a file, so the distinction the client sees is
            // recovered from the record rather than from a second open.
            protocore_mnt_stat st = {0};
            Fs.path.root = SSH_SFTP_CTX(work)->root;
            Fs.path.dir = req;
            Fs.path.name = "";
            Fs.io.stat = &st;
            Fs.stat(protocore_filesystem_span());
            proto_bool is_dir = Fs.ok && st.is_dir;
            // NO_SUCH_FILE only for a plain read that found nothing - that is the one case a client
            // acts on differently. A directory, or any failed write, is FAILURE.
            send_status(work, s, id, (is_dir || writing) ? PROTOCORE_SSH_FX_FAILURE : PROTOCORE_SSH_FX_NO_SUCH_FILE,
                        is_dir ? "is a directory" : "open failed");
            return;
        }
        s->open_mask |= (1u << hi);
        s->handles[hi].is_dir = PROTO_FALSE;
        s->handles[hi].fh = fh;
        s->handles[hi].readdir_done = PROTO_FALSE;
        s->handles[hi].has_pending = PROTO_FALSE;
        keep_req(&s->handles[hi], req);
        send_handle(work, s, id, hi);
        return;
    }
    case PROTOCORE_SSH_FXP_CLOSE: {
        const uint8_t *h = NULL;
        uint32_t hl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &h;
        SftpV.rd_string_args.out_len = &hl;
        Sftp.rd_string(sftp_work);
        if (!SftpV.ok)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_BAD_MESSAGE, "");
            return;
        }
        int hi = handle_index(s, h, hl);
        if (hi < 0)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_FAILURE, "bad handle");
            return;
        }
        free_handle(s, hi);
        send_status(work, s, id, PROTOCORE_SSH_FX_OK, "");
        return;
    }
    case PROTOCORE_SSH_FXP_READ: {
        const uint8_t *h = NULL;
        uint32_t hl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &h;
        SftpV.rd_string_args.out_len = &hl;
        Sftp.rd_string(sftp_work);
        if (!SftpV.ok)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_BAD_MESSAGE, "");
            return;
        }
        SftpV.rd_u64_args.r = &r;
        Sftp.rd_u64(sftp_work);
        uint64_t off = SftpV.u64;
        SftpV.rd_u32_args.r = &r;
        Sftp.rd_u32(sftp_work);
        uint32_t rlen = SftpV.u32;
        int hi = handle_index(s, h, hl);
        if (!r.ok || hi < 0 || s->handles[hi].is_dir)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_FAILURE, "bad handle");
            return;
        }
        Fs.io.handle = s->handles[hi].fh;
        Fs.io.off = off;
        Fs.seek(protocore_filesystem_span());
        if (!Fs.ok)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_FAILURE, "seek");
            return;
        }
        uint32_t want = rlen < PROTOCORE_SFTP_MAX_READ ? rlen : PROTOCORE_SFTP_MAX_READ;
        Fs.io.handle = s->handles[hi].fh;
        Fs.io.buf = SSH_SFTP_CTX(work)->rbuf;
        Fs.io.n = want;
        Fs.read(protocore_filesystem_span());
        int got = Fs.i32;
        if (got <= 0)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_EOF, "");
            return;
        }
        SftpV.build_data_args.id = id;
        SftpV.build_data_args.data = SSH_SFTP_CTX(work)->rbuf;
        SftpV.build_data_args.dlen = (uint32_t)got;
        SftpV.build_data_args.out = SSH_SFTP_CTX(work)->out;
        SftpV.build_data_args.cap = PROTOCORE_SFTP_RESP_CAP;
        Sftp.build_data(sftp_work);
        send_resp(work, s, SftpV.n);
        return;
    }
    case PROTOCORE_SSH_FXP_OPENDIR: {
        const uint8_t *p = NULL;
        uint32_t pl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &p;
        SftpV.rd_string_args.out_len = &pl;
        Sftp.rd_string(sftp_work);
        const char *req = r.ok ? req_path(work, 0, p, pl) : NULL;
        if (req == NULL)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        int hi = alloc_handle(s);
        if (hi < 0)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_FAILURE, "too many open handles");
            return;
        }
        Fs.path.root = SSH_SFTP_CTX(work)->root;
        Fs.path.dir = req;
        Fs.path.name = "";
        Fs.opendir(protocore_filesystem_span());
        int fh = Fs.i32;
        if (fh < 0)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_NO_SUCH_FILE, "not a directory");
            return;
        }
        s->open_mask |= (1u << hi);
        s->handles[hi].is_dir = PROTO_TRUE;
        s->handles[hi].fh = fh;
        s->handles[hi].readdir_done = PROTO_FALSE;
        s->handles[hi].has_pending = PROTO_FALSE;
        keep_req(&s->handles[hi], req);
        send_handle(work, s, id, hi);
        return;
    }
    case PROTOCORE_SSH_FXP_READDIR: {
        const uint8_t *h = NULL;
        uint32_t hl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &h;
        SftpV.rd_string_args.out_len = &hl;
        Sftp.rd_string(sftp_work);
        if (!SftpV.ok)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_BAD_MESSAGE, "");
            return;
        }
        int hi = handle_index(s, h, hl);
        if (hi < 0 || !s->handles[hi].is_dir)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_FAILURE, "bad handle");
            return;
        }
        do_readdir(work, s, id, &s->handles[hi]);
        return;
    }
    case PROTOCORE_SSH_FXP_STAT:
    case PROTOCORE_SSH_FXP_LSTAT: {
        const uint8_t *p = NULL;
        uint32_t pl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &p;
        SftpV.rd_string_args.out_len = &pl;
        Sftp.rd_string(sftp_work);
        const char *req = r.ok ? req_path(work, 0, p, pl) : NULL;
        if (req == NULL)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        protocore_mnt_stat st = {0};
        Fs.path.root = SSH_SFTP_CTX(work)->root;
        Fs.path.dir = req;
        Fs.path.name = "";
        Fs.io.stat = &st;
        Fs.stat(protocore_filesystem_span());
        if (!Fs.ok)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_NO_SUCH_FILE, "");
            return;
        }
        SftpAttrs a = {0};
        attrs_from_stat(&st, &a);
        SftpV.build_attrs_args.id = id;
        SftpV.build_attrs_args.a = &a;
        SftpV.build_attrs_args.out = SSH_SFTP_CTX(work)->out;
        SftpV.build_attrs_args.cap = PROTOCORE_SFTP_RESP_CAP;
        Sftp.build_attrs(sftp_work);
        send_resp(work, s, SftpV.n);
        return;
    }
    case PROTOCORE_SSH_FXP_FSTAT: {
        const uint8_t *h = NULL;
        uint32_t hl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &h;
        SftpV.rd_string_args.out_len = &hl;
        Sftp.rd_string(sftp_work);
        if (!SftpV.ok)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_BAD_MESSAGE, "");
            return;
        }
        int hi = handle_index(s, h, hl);
        protocore_mnt_stat st = {0};
        proto_bool stat_ok = PROTO_FALSE;
        if (hi >= 0)
        {
            Fs.path.root = SSH_SFTP_CTX(work)->root;
            Fs.path.dir = s->handles[hi].req;
            Fs.path.name = "";
            Fs.io.stat = &st;
            Fs.stat(protocore_filesystem_span());
            stat_ok = Fs.ok;
        }
        if (!stat_ok)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_FAILURE, "bad handle");
            return;
        }
        SftpAttrs a = {0};
        attrs_from_stat(&st, &a);
        SftpV.build_attrs_args.id = id;
        SftpV.build_attrs_args.a = &a;
        SftpV.build_attrs_args.out = SSH_SFTP_CTX(work)->out;
        SftpV.build_attrs_args.cap = PROTOCORE_SFTP_RESP_CAP;
        Sftp.build_attrs(sftp_work);
        send_resp(work, s, SftpV.n);
        return;
    }
    case PROTOCORE_SSH_FXP_REMOVE: {
        const uint8_t *p = NULL;
        uint32_t pl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &p;
        SftpV.rd_string_args.out_len = &pl;
        Sftp.rd_string(sftp_work);
        const char *req = r.ok ? req_path(work, 0, p, pl) : NULL;
        if (req == NULL)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        Fs.path.root = SSH_SFTP_CTX(work)->root;
        Fs.path.dir = req;
        Fs.path.name = "";
        Fs.remove(protocore_filesystem_span());
        send_status(work, s, id, Fs.ok ? PROTOCORE_SSH_FX_OK : PROTOCORE_SSH_FX_FAILURE, "");
        return;
    }
    case PROTOCORE_SSH_FXP_MKDIR: {
        const uint8_t *p = NULL;
        uint32_t pl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &p;
        SftpV.rd_string_args.out_len = &pl;
        Sftp.rd_string(sftp_work);
        const char *req = r.ok ? req_path(work, 0, p, pl) : NULL;
        if (req == NULL)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        Fs.path.root = SSH_SFTP_CTX(work)->root;
        Fs.path.dir = req;
        Fs.path.name = "";
        Fs.mkdir(protocore_filesystem_span());
        send_status(work, s, id, Fs.ok ? PROTOCORE_SSH_FX_OK : PROTOCORE_SSH_FX_FAILURE, "");
        return;
    }
    case PROTOCORE_SSH_FXP_RMDIR: {
        const uint8_t *p = NULL;
        uint32_t pl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &p;
        SftpV.rd_string_args.out_len = &pl;
        Sftp.rd_string(sftp_work);
        const char *req = r.ok ? req_path(work, 0, p, pl) : NULL;
        if (req == NULL)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        Fs.path.root = SSH_SFTP_CTX(work)->root;
        Fs.path.dir = req;
        Fs.path.name = "";
        Fs.rmdir(protocore_filesystem_span());
        send_status(work, s, id, Fs.ok ? PROTOCORE_SSH_FX_OK : PROTOCORE_SSH_FX_FAILURE, "");
        return;
    }
    case PROTOCORE_SSH_FXP_RENAME: {
        const uint8_t *op = NULL;
        uint32_t ol = 0;
        const uint8_t *np = NULL;
        uint32_t nl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &op;
        SftpV.rd_string_args.out_len = &ol;
        Sftp.rd_string(sftp_work);
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &np;
        SftpV.rd_string_args.out_len = &nl;
        Sftp.rd_string(sftp_work);
        const char *from = r.ok ? req_path(work, 0, op, ol) : NULL;
        const char *to = (from != NULL) ? req_path(work, 1, np, nl) : NULL;
        if (to == NULL)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_PERMISSION_DENIED, "bad path");
            return;
        }
        Fs.path.root = SSH_SFTP_CTX(work)->root;
        Fs.path.dir = from;
        Fs.path.name = "";
        Fs.dest.dir = to;
        Fs.dest.name = "";
        Fs.rename(protocore_filesystem_span());
        send_status(work, s, id, Fs.ok ? PROTOCORE_SSH_FX_OK : PROTOCORE_SSH_FX_FAILURE, "");
        return;
    }
    case PROTOCORE_SSH_FXP_REALPATH: {
        const uint8_t *p = NULL;
        uint32_t pl = 0;
        SftpV.rd_string_args.r = &r;
        SftpV.rd_string_args.out = &p;
        SftpV.rd_string_args.out_len = &pl;
        Sftp.rd_string(sftp_work);
        if (!r.ok)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_BAD_MESSAGE, "");
            return;
        }
        const char *req = req_path(work, 0, p, pl);
        if (req == NULL)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_FAILURE, "path too long");
            return;
        }
        // REALPATH answers in the client's namespace, so it canonicalizes against "/" rather than the
        // mount root. This is the accessor's own canonicalizer, called with the root the answer is in.
        int rc = protocore_fs_resolve("/", req, "", SSH_SFTP_CTX(work)->req[1], sizeof(SSH_SFTP_CTX(work)->req[1]));
        if (rc == -1)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_PERMISSION_DENIED, "traversal");
            return;
        }
        if (rc != 0)
        {
            send_status(work, s, id, PROTOCORE_SSH_FX_FAILURE, "path too long");
            return;
        }
        SftpAttrs a = {0};
        a.flags = PROTOCORE_SSH_FILEXFER_ATTR_PERMS;
        a.permissions = PROTOCORE_SFTP_S_IFDIR | 0755;
        a.size = 0;
        a.atime = a.mtime = 0;
        SftpV.format_longname_args.is_dir = PROTO_TRUE;
        SftpV.format_longname_args.perms = a.permissions;
        SftpV.format_longname_args.size = 0;
        SftpV.format_longname_args.mtime = 0;
        SftpV.format_longname_args.name = SSH_SFTP_CTX(work)->req[1];
        SftpV.format_longname_args.out = SSH_SFTP_CTX(work)->ln;
        SftpV.format_longname_args.cap = sizeof(SSH_SFTP_CTX(work)->ln);
        Sftp.format_longname(sftp_work);
        SftpV.build_name1_args.id = id;
        SftpV.build_name1_args.name = SSH_SFTP_CTX(work)->req[1];
        SftpV.build_name1_args.longname = SSH_SFTP_CTX(work)->ln;
        SftpV.build_name1_args.a = &a;
        SftpV.build_name1_args.out = SSH_SFTP_CTX(work)->out;
        SftpV.build_name1_args.cap = PROTOCORE_SFTP_RESP_CAP;
        Sftp.build_name1(sftp_work);
        send_resp(work, s, SftpV.n);
        return;
    }
    case PROTOCORE_SSH_FXP_SETSTAT:
    case PROTOCORE_SSH_FXP_FSETSTAT:
        // We do not implement chmod/chown/truncate-via-setstat; accept it (OK) so `put` does not fail on the
        // trailing FSETSTAT that sets mode/mtime - the values are simply not applied.
        send_status(work, s, id, PROTOCORE_SSH_FX_OK, "");
        return;
    default:
        send_status(work, s, id, PROTOCORE_SSH_FX_OP_UNSUPPORTED, "unsupported");
        return;
    }
}

// --- framing loop ---------------------------------------------------------------------------------
// Consume complete packets from the accumulator. A WRITE switches to streaming mode. @return false to tear the
// channel down (malformed / oversized non-WRITE packet).
static proto_bool process_acc(uint8_t *restrict work, SftpSession *s)
{
    for (;;)
    {
        if (s->acc_len < 5)
        {
            return PROTO_TRUE; // need the length prefix + the type byte
        }
        uint32_t plen = endian.rd32be(s->acc);
        if (plen == 0)
        {
            return PROTO_FALSE; // malformed
        }
        size_t total = (size_t)plen + 4;
        uint8_t type = s->acc[4];

        if (type == PROTOCORE_SSH_FXP_WRITE)
        {
            SftpReader r;
            SftpV.rd_init_args.r = &r;
            SftpV.rd_init_args.payload = s->acc + 4;
            SftpV.rd_init_args.len = s->acc_len - 4;
            Sftp.rd_init(sftp_work);
            SftpV.rd_u8_args.r = &r;
            Sftp.rd_u8(sftp_work); // type
            SftpV.rd_u32_args.r = &r;
            Sftp.rd_u32(sftp_work);
            uint32_t id = SftpV.u32;
            const uint8_t *h = NULL;
            uint32_t hl = 0;
            SftpV.rd_string_args.r = &r;
            SftpV.rd_string_args.out = &h;
            SftpV.rd_string_args.out_len = &hl;
            Sftp.rd_string(sftp_work);
            if (!SftpV.ok)
            {
                return PROTO_TRUE; // the handle has not fully arrived - wait
            }
            SftpV.rd_u64_args.r = &r;
            Sftp.rd_u64(sftp_work);
            uint64_t off = SftpV.u64;
            SftpV.rd_u32_args.r = &r;
            Sftp.rd_u32(sftp_work);
            uint32_t datalen = SftpV.u32;
            if (!r.ok)
            {
                return PROTO_TRUE; // the WRITE header has not fully arrived - wait
            }

            int hi = handle_index(s, h, hl);
            s->wr_id = id;
            s->wr_remaining = datalen;
            s->wr_off = off;
            s->wr_handle = hi;
            s->wr_err = (hi < 0 || s->handles[hi].is_dir);
            if (!s->wr_err)
            {
                Fs.io.handle = s->handles[hi].fh;
                Fs.io.off = off;
                Fs.seek(protocore_filesystem_span());
            }
            s->writing = PROTO_TRUE;

            size_t hdr = 4 + r.off; // header bytes of this packet (before the data payload)
            size_t have = s->acc_len - hdr;
            size_t chunk = have < datalen ? have : datalen;
            write_stream_bytes(s, s->acc + hdr, chunk);
            size_t consumed = hdr + chunk;
            mem.move(s->acc, s->acc + consumed, s->acc_len - consumed);
            s->acc_len -= (uint16_t)consumed;
            if (s->wr_remaining == 0)
            {
                finish_write(work, s); // all data was already in the accumulator
            }
            if (s->writing)
            {
                return PROTO_TRUE; // still streaming - the rest arrives as raw channel data
            }
            continue; // write finished within the accumulator; process the next packet
        }

        if (total > sizeof(s->acc))
        {
            return PROTO_FALSE; // a non-WRITE packet larger than the buffer -> drop the channel
        }
        if (s->acc_len < total)
        {
            return PROTO_TRUE; // wait for the rest
        }
        handle_packet(work, s, s->acc, total);
        mem.move(s->acc, s->acc + total, s->acc_len - total);
        s->acc_len -= (uint16_t)total;
    }
}

// --- channel callbacks ----------------------------------------------------------------------------
static void protocore_sftp_on_open(uint8_t slot, uint32_t channel)
{
    if (slot >= MAX_SSH_CONNS)
    {
        return;
    }
    SftpSession *s = &sftp_sess[slot];
    free_all_handles(s); // clean any handles lingering from a prior session on this slot
    s->active = PROTO_TRUE;
    s->slot = slot;
    s->channel = channel;
    s->acc_len = 0;
    s->writing = PROTO_FALSE;
    s->wr_remaining = 0;
    s->wr_handle = -1;
    // The SFTP VERSION reply is sent when the client's INIT packet arrives.
}

static void protocore_sftp_on_data(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_ssh_sftp_span();

    if (slot >= MAX_SSH_CONNS)
    {
        return;
    }
    SftpSession *s = &sftp_sess[slot];
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
                finish_write(work, s);
            }
            continue;
        }
        size_t space = sizeof(s->acc) - s->acc_len;
        if (space == 0)
        {
            SshConnectionV.chan.slot = slot;
            SshConnectionV.chan.channel = s->channel; // a non-WRITE packet too big to buffer
            SshConnection.channel_send_close(protocore_ssh_connection_span());
            s->active = PROTO_FALSE;
            return;
        }
        size_t take = len < space ? len : space;
        mem.cpy(s->acc + s->acc_len, data, take);
        s->acc_len += (uint16_t)take;
        data += take;
        len -= take;
        if (!process_acc(work, s))
        {
            SshConnectionV.chan.slot = slot;
            SshConnectionV.chan.channel = s->channel;
            SshConnection.channel_send_close(protocore_ssh_connection_span());
            s->active = PROTO_FALSE;
            return;
        }
    }
}

// --- public API -----------------------------------------------------------------------------------
void protocore_ssh_sftp_begin(uint8_t *restrict work)
{
    // Bind the root this server answers from. The name is what the accessor maps; two servers naming
    // the same one share it and cost one entry, and naming different ones is how they end up over
    // different storage without either being able to tell.
    Fs.mount = "mnt/sftp";
    Fs.begin(protocore_filesystem_span());
    SSH_SFTP_CTX(work)->root = Fs.i32;

    for (int i = 0; i < MAX_SSH_CONNS; i++)
    {
        sftp_sess[i].active = PROTO_FALSE;
        // Close whatever a previous run left open, then the table is empty. BSS starts the mask at
        // zero, so a first call closes nothing - it never mistakes handle 0 for an open file.
        free_all_handles(&sftp_sess[i]);
    }
    if (!SSH_SFTP_CTX(work)->registered)
    {
        SshConnectionV.sftp_open_cb = protocore_sftp_on_open;
        SshConnection.set_sftp_open_cb(protocore_ssh_connection_span());
        SshConnectionV.sftp_data_cb = protocore_sftp_on_data;
        SshConnection.set_sftp_data_cb(protocore_ssh_connection_span());
        SSH_SFTP_CTX(work)->registered = PROTO_TRUE;
    }
}

/** @brief The operands and the outcome. */
SshSftpVars SshSftpV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_SFTP
