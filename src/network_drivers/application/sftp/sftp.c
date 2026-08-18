// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sftp.c
 * @brief SFTP protocol v3 wire codec - implementation. See sftp.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t time_compat_work[16]; // the borrow an entry takes; TimeCompat never reads it

#if PROTOCORE_ENABLE_SSH_SFTP

#include "mmgr/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem.h"
#include "mmgr/protostr.h"
#include "network_drivers/application/sftp/sftp.h"

#include "shared/time_compat/time_compat.h" // protocore_gmtime_r (portable reentrant UTC)

#include <time.h>

PROTOCORE_BEGIN_DECLS

// --- reader (big-endian, bounds-checked) ---------------------------------------------------------

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void sftp_rd_string(uint8_t *restrict work);
static void sftp_rd_u32(uint8_t *restrict work);
static void sftp_rd_u64(uint8_t *restrict work);
static void sftp_wr_attrs(uint8_t *restrict work);
static void sftp_wr_bytes(uint8_t *restrict work);
static void sftp_wr_finish(uint8_t *restrict work);
static void sftp_wr_init(uint8_t *restrict work);
static void sftp_wr_string(uint8_t *restrict work);
static void sftp_wr_u32(uint8_t *restrict work);
static void sftp_wr_u64(uint8_t *restrict work);
static void sftp_wr_u8(uint8_t *restrict work);

static void sftp_rd_init(uint8_t *restrict work)
{
    (void)work;
    SftpReader *r = Sftp.rd_init_args.r;
    const uint8_t *payload = Sftp.rd_init_args.payload;
    size_t len = Sftp.rd_init_args.len;

    r->p = payload;
    r->len = len;
    r->off = 0;
    r->ok = PROTO_TRUE;
}

static void sftp_rd_u8(uint8_t *restrict work)
{
    (void)work;
    SftpReader *r = Sftp.rd_u8_args.r;

    if (!r->ok || r->off + 1 > r->len)
    {
        r->ok = PROTO_FALSE;
        Sftp.value = 0;
        return;
    }
    Sftp.value = r->p[r->off++];
}

static void sftp_rd_u32(uint8_t *restrict work)
{
    (void)work;
    SftpReader *r = Sftp.rd_u32_args.r;

    if (!r->ok || r->off + 4 > r->len)
    {
        r->ok = PROTO_FALSE;
        Sftp.u32 = 0;
        return;
    }
    uint32_t v = ((uint32_t)r->p[r->off] << 24) | ((uint32_t)r->p[r->off + 1] << 16) |
                 ((uint32_t)r->p[r->off + 2] << 8) | (uint32_t)r->p[r->off + 3];
    r->off += 4;
    Sftp.u32 = v;
}

static void sftp_rd_u64(uint8_t *restrict work)
{
    (void)work;
    SftpReader *r = Sftp.rd_u64_args.r;

    if (!r->ok || r->off + 8 > r->len)
    {
        r->ok = PROTO_FALSE;
        Sftp.u64 = 0;
        return;
    }
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
    {
        v = (v << 8) | r->p[r->off + i];
    }
    r->off += 8;
    Sftp.u64 = v;
}

static void sftp_rd_string(uint8_t *restrict work)
{
    SftpReader *r = Sftp.rd_string_args.r;
    const uint8_t **out = Sftp.rd_string_args.out;
    uint32_t *out_len = Sftp.rd_string_args.out_len;

    Sftp.rd_u32_args.r = r;
    sftp_rd_u32(work);
    uint32_t n = Sftp.u32;
    if (!r->ok || r->off + n > r->len)
    {
        r->ok = PROTO_FALSE;
        Sftp.ok = PROTO_FALSE;
        return;
    }
    if (out)
    {
        *out = r->p + r->off;
    }
    if (out_len)
    {
        *out_len = n;
    }
    r->off += n;
    Sftp.ok = PROTO_TRUE;
}

static void sftp_rd_attrs(uint8_t *restrict work)
{
    SftpReader *r = Sftp.rd_attrs_args.r;
    SftpAttrs *a = Sftp.rd_attrs_args.a;

    Sftp.rd_u32_args.r = r;
    sftp_rd_u32(work);
    a->flags = Sftp.u32;
    a->size = 0;
    a->permissions = 0;
    a->atime = 0;
    a->mtime = 0;
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_SIZE)
    {
        Sftp.rd_u64_args.r = r;
        sftp_rd_u64(work);
        a->size = Sftp.u64;
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_UIDGID)
    {
        Sftp.rd_u32_args.r = r;
        sftp_rd_u32(work); // uid (ignored)
        Sftp.rd_u32_args.r = r;
        sftp_rd_u32(work); // gid (ignored)
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_PERMS)
    {
        Sftp.rd_u32_args.r = r;
        sftp_rd_u32(work);
        a->permissions = Sftp.u32;
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME)
    {
        Sftp.rd_u32_args.r = r;
        sftp_rd_u32(work);
        a->atime = Sftp.u32;
        Sftp.rd_u32_args.r = r;
        sftp_rd_u32(work);
        a->mtime = Sftp.u32;
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_EXTENDED)
    {
        Sftp.rd_u32_args.r = r;
        sftp_rd_u32(work);
        uint32_t ec = Sftp.u32;
        for (uint32_t i = 0; i < ec && r->ok; i++)
        {
            Sftp.rd_string_args.r = r;
            Sftp.rd_string_args.out = NULL;
            Sftp.rd_string_args.out_len = NULL;
            sftp_rd_string(work); // extended type
            Sftp.rd_string_args.r = r;
            Sftp.rd_string_args.out = NULL;
            Sftp.rd_string_args.out_len = NULL;
            sftp_rd_string(work); // extended data
        }
    }
    Sftp.ok = r->ok;
}

// --- writer --------------------------------------------------------------------------------------

static void sftp_wr_init(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = Sftp.wr_init_args.w;
    uint8_t *out = Sftp.wr_init_args.out;
    size_t cap = Sftp.wr_init_args.cap;

    w->p = out;
    w->cap = cap;
    w->off = 4; // reserve the length prefix
    w->ovf = (cap < 4);
}

static void sftp_wr_u8(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = Sftp.wr_u8_args.w;
    uint8_t v = Sftp.wr_u8_args.v;

    if (w->ovf || w->off + 1 > w->cap)
    {
        w->ovf = PROTO_TRUE;
        return;
    }
    w->p[w->off++] = v;
}

static void sftp_wr_u32(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = Sftp.wr_u32_args.w;
    uint32_t v = Sftp.wr_u32_args.v;

    if (w->ovf || w->off + 4 > w->cap)
    {
        w->ovf = PROTO_TRUE;
        return;
    }
    w->p[w->off++] = (uint8_t)(v >> 24);
    w->p[w->off++] = (uint8_t)(v >> 16);
    w->p[w->off++] = (uint8_t)(v >> 8);
    w->p[w->off++] = (uint8_t)v;
}

static void sftp_wr_u64(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = Sftp.wr_u64_args.w;
    uint64_t v = Sftp.wr_u64_args.v;

    if (w->ovf || w->off + 8 > w->cap)
    {
        w->ovf = PROTO_TRUE;
        return;
    }
    for (int i = 7; i >= 0; i--)
    {
        w->p[w->off++] = (uint8_t)(v >> (8 * i));
    }
}

static void sftp_wr_bytes(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = Sftp.wr_bytes_args.w;
    const void *b = Sftp.wr_bytes_args.b;
    size_t n = Sftp.wr_bytes_args.n;

    if (w->ovf || w->off + n > w->cap)
    {
        w->ovf = PROTO_TRUE;
        return;
    }
    mem.cpy(w->p + w->off, b, n);
    w->off += n;
}

static void sftp_wr_string(uint8_t *restrict work)
{
    SftpWriter *w = Sftp.wr_string_args.w;
    const void *s = Sftp.wr_string_args.s;
    uint32_t n = Sftp.wr_string_args.n;

    Sftp.wr_u32_args.w = w;
    Sftp.wr_u32_args.v = n;
    sftp_wr_u32(work);
    Sftp.wr_bytes_args.w = w;
    Sftp.wr_bytes_args.b = s;
    Sftp.wr_bytes_args.n = n;
    sftp_wr_bytes(work);
}

static void sftp_wr_attrs(uint8_t *restrict work)
{
    SftpWriter *w = Sftp.wr_attrs_args.w;
    const SftpAttrs *a = Sftp.wr_attrs_args.a;

    Sftp.wr_u32_args.w = w;
    Sftp.wr_u32_args.v = a->flags;
    sftp_wr_u32(work);
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_SIZE)
    {
        Sftp.wr_u64_args.w = w;
        Sftp.wr_u64_args.v = a->size;
        sftp_wr_u64(work);
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_UIDGID)
    {
        Sftp.wr_u32_args.w = w;
        Sftp.wr_u32_args.v = 0;
        sftp_wr_u32(work);
        Sftp.wr_u32_args.w = w;
        Sftp.wr_u32_args.v = 0;
        sftp_wr_u32(work);
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_PERMS)
    {
        Sftp.wr_u32_args.w = w;
        Sftp.wr_u32_args.v = a->permissions;
        sftp_wr_u32(work);
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME)
    {
        Sftp.wr_u32_args.w = w;
        Sftp.wr_u32_args.v = a->atime;
        sftp_wr_u32(work);
        Sftp.wr_u32_args.w = w;
        Sftp.wr_u32_args.v = a->mtime;
        sftp_wr_u32(work);
    }
}

static void sftp_wr_finish(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = Sftp.wr_finish_args.w;

    if (w->ovf)
    {
        Sftp.n = 0;
        return;
    }
    uint32_t plen = (uint32_t)(w->off - 4);
    w->p[0] = (uint8_t)(plen >> 24);
    w->p[1] = (uint8_t)(plen >> 16);
    w->p[2] = (uint8_t)(plen >> 8);
    w->p[3] = (uint8_t)plen;
    Sftp.n = w->off;
}

static void sftp_wr_pos(uint8_t *restrict work)
{
    (void)work;
    const SftpWriter *w = Sftp.wr_pos_args.w;

    Sftp.n = w->off;
}

static void sftp_wr_patch_u32(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = Sftp.wr_patch_u32_args.w;
    size_t at = Sftp.wr_patch_u32_args.at;
    uint32_t v = Sftp.wr_patch_u32_args.v;

    if (at + 4 > w->cap)
    {
        return;
    }
    w->p[at] = (uint8_t)(v >> 24);
    w->p[at + 1] = (uint8_t)(v >> 16);
    w->p[at + 2] = (uint8_t)(v >> 8);
    w->p[at + 3] = (uint8_t)v;
}

// --- framing -------------------------------------------------------------------------------------

static void sftp_frame_len(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Sftp.frame_len_args.buf;
    size_t have = Sftp.frame_len_args.have;
    size_t max = Sftp.frame_len_args.max;

    if (have < 4)
    {
        Sftp.n = 0; // need at least the length prefix
        return;
    }
    uint32_t plen = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    size_t total = (size_t)plen + 4;
    if (plen == 0 || total > max)
    {
        Sftp.n = (size_t)-1; // malformed (0-length) or larger than the caller can hold -> drop
        return;
    }
    Sftp.n = total;
}

// --- response builders ---------------------------------------------------------------------------

static void sftp_build_version(uint8_t *restrict work)
{
    uint8_t *out = Sftp.build_version_args.out;
    size_t cap = Sftp.build_version_args.cap;

    SftpWriter w;
    Sftp.wr_init_args.w = &w;
    Sftp.wr_init_args.out = out;
    Sftp.wr_init_args.cap = cap;
    sftp_wr_init(work);
    Sftp.wr_u8_args.w = &w;
    Sftp.wr_u8_args.v = PROTOCORE_SSH_FXP_VERSION;
    sftp_wr_u8(work);
    Sftp.wr_u32_args.w = &w;
    Sftp.wr_u32_args.v = PROTOCORE_SFTP_VERSION;
    sftp_wr_u32(work);
    Sftp.wr_finish_args.w = &w;
    sftp_wr_finish(work);
}

static void sftp_build_status(uint8_t *restrict work)
{
    uint32_t id = Sftp.build_status_args.id;
    uint32_t code = Sftp.build_status_args.code;
    const char *msg = Sftp.build_status_args.msg;
    uint8_t *out = Sftp.build_status_args.out;
    size_t cap = Sftp.build_status_args.cap;

    SftpWriter w;
    Sftp.wr_init_args.w = &w;
    Sftp.wr_init_args.out = out;
    Sftp.wr_init_args.cap = cap;
    sftp_wr_init(work);
    Sftp.wr_u8_args.w = &w;
    Sftp.wr_u8_args.v = PROTOCORE_SSH_FXP_STATUS;
    sftp_wr_u8(work);
    Sftp.wr_u32_args.w = &w;
    Sftp.wr_u32_args.v = id;
    sftp_wr_u32(work);
    Sftp.wr_u32_args.w = &w;
    Sftp.wr_u32_args.v = code;
    sftp_wr_u32(work);
    size_t ml = msg ? str.len(msg, cap) : 0;
    Sftp.wr_string_args.w = &w;
    Sftp.wr_string_args.s = msg ? msg : "";
    Sftp.wr_string_args.n = (uint32_t)ml;
    sftp_wr_string(work);
    Sftp.wr_string_args.w = &w;
    Sftp.wr_string_args.s = "";
    Sftp.wr_string_args.n = 0;
    sftp_wr_string(work); // language tag
    Sftp.wr_finish_args.w = &w;
    sftp_wr_finish(work);
}

static void sftp_build_handle(uint8_t *restrict work)
{
    uint32_t id = Sftp.build_handle_args.id;
    const void *handle = Sftp.build_handle_args.handle;
    uint32_t hlen = Sftp.build_handle_args.hlen;
    uint8_t *out = Sftp.build_handle_args.out;
    size_t cap = Sftp.build_handle_args.cap;

    SftpWriter w;
    Sftp.wr_init_args.w = &w;
    Sftp.wr_init_args.out = out;
    Sftp.wr_init_args.cap = cap;
    sftp_wr_init(work);
    Sftp.wr_u8_args.w = &w;
    Sftp.wr_u8_args.v = PROTOCORE_SSH_FXP_HANDLE;
    sftp_wr_u8(work);
    Sftp.wr_u32_args.w = &w;
    Sftp.wr_u32_args.v = id;
    sftp_wr_u32(work);
    Sftp.wr_string_args.w = &w;
    Sftp.wr_string_args.s = handle;
    Sftp.wr_string_args.n = hlen;
    sftp_wr_string(work);
    Sftp.wr_finish_args.w = &w;
    sftp_wr_finish(work);
}

static void sftp_build_attrs(uint8_t *restrict work)
{
    uint32_t id = Sftp.build_attrs_args.id;
    const SftpAttrs *a = Sftp.build_attrs_args.a;
    uint8_t *out = Sftp.build_attrs_args.out;
    size_t cap = Sftp.build_attrs_args.cap;

    SftpWriter w;
    Sftp.wr_init_args.w = &w;
    Sftp.wr_init_args.out = out;
    Sftp.wr_init_args.cap = cap;
    sftp_wr_init(work);
    Sftp.wr_u8_args.w = &w;
    Sftp.wr_u8_args.v = PROTOCORE_SSH_FXP_ATTRS;
    sftp_wr_u8(work);
    Sftp.wr_u32_args.w = &w;
    Sftp.wr_u32_args.v = id;
    sftp_wr_u32(work);
    Sftp.wr_attrs_args.w = &w;
    Sftp.wr_attrs_args.a = a;
    sftp_wr_attrs(work);
    Sftp.wr_finish_args.w = &w;
    sftp_wr_finish(work);
}

static void sftp_build_data(uint8_t *restrict work)
{
    uint32_t id = Sftp.build_data_args.id;
    const void *data = Sftp.build_data_args.data;
    uint32_t dlen = Sftp.build_data_args.dlen;
    uint8_t *out = Sftp.build_data_args.out;
    size_t cap = Sftp.build_data_args.cap;

    SftpWriter w;
    Sftp.wr_init_args.w = &w;
    Sftp.wr_init_args.out = out;
    Sftp.wr_init_args.cap = cap;
    sftp_wr_init(work);
    Sftp.wr_u8_args.w = &w;
    Sftp.wr_u8_args.v = PROTOCORE_SSH_FXP_DATA;
    sftp_wr_u8(work);
    Sftp.wr_u32_args.w = &w;
    Sftp.wr_u32_args.v = id;
    sftp_wr_u32(work);
    Sftp.wr_string_args.w = &w;
    Sftp.wr_string_args.s = data;
    Sftp.wr_string_args.n = dlen;
    sftp_wr_string(work);
    Sftp.wr_finish_args.w = &w;
    sftp_wr_finish(work);
}

static void sftp_build_name1(uint8_t *restrict work)
{
    uint32_t id = Sftp.build_name1_args.id;
    const char *name = Sftp.build_name1_args.name;
    const char *longname = Sftp.build_name1_args.longname;
    const SftpAttrs *a = Sftp.build_name1_args.a;
    uint8_t *out = Sftp.build_name1_args.out;
    size_t cap = Sftp.build_name1_args.cap;

    SftpWriter w;
    Sftp.wr_init_args.w = &w;
    Sftp.wr_init_args.out = out;
    Sftp.wr_init_args.cap = cap;
    sftp_wr_init(work);
    Sftp.wr_u8_args.w = &w;
    Sftp.wr_u8_args.v = PROTOCORE_SSH_FXP_NAME;
    sftp_wr_u8(work);
    Sftp.wr_u32_args.w = &w;
    Sftp.wr_u32_args.v = id;
    sftp_wr_u32(work);
    Sftp.wr_u32_args.w = &w;
    Sftp.wr_u32_args.v = 1;
    sftp_wr_u32(work); // one entry
    Sftp.wr_string_args.w = &w;
    Sftp.wr_string_args.s = name;
    Sftp.wr_string_args.n = (uint32_t)str.len(name, cap);
    sftp_wr_string(work);
    Sftp.wr_string_args.w = &w;
    Sftp.wr_string_args.s = longname;
    Sftp.wr_string_args.n = (uint32_t)str.len(longname, cap);
    sftp_wr_string(work);
    Sftp.wr_attrs_args.w = &w;
    Sftp.wr_attrs_args.a = a;
    sftp_wr_attrs(work);
    Sftp.wr_finish_args.w = &w;
    sftp_wr_finish(work);
}

static void sftp_format_longname(uint8_t *restrict work)
{
    (void)work;
    proto_bool is_dir = Sftp.format_longname_args.is_dir;
    uint32_t perms = Sftp.format_longname_args.perms;
    uint64_t size = Sftp.format_longname_args.size;
    uint32_t mtime = Sftp.format_longname_args.mtime;
    const char *name = Sftp.format_longname_args.name;
    char *out = Sftp.format_longname_args.out;
    size_t cap = Sftp.format_longname_args.cap;

    static const char *kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char mode[11];
    mode[0] = is_dir ? 'd' : '-';
    static const char rwx[9] = {'r', 'w', 'x', 'r', 'w', 'x', 'r', 'w', 'x'};
    for (int i = 0; i < 9; i++)
    {
        mode[1 + i] = (perms & (1u << (8 - i))) ? rwx[i] : '-';
    }
    mode[10] = '\0';

    struct tm tmv;
    mem.set(&tmv, 0, sizeof(tmv));
    TimeCompat.args.epoch = (time_t)mtime; // mtime==0 -> epoch, a harmless placeholder date
    TimeCompat.args.out = &tmv;
    TimeCompat.gmtime(time_compat_work);
    // mtime is a uint32_t, so t is always inside the range every gmtime implementation accepts and the
    // conversion yields tm_mon in [0,11] by definition; tmv is zeroed above, so even a failed conversion
    // leaves month 0. The range test is purely a bounds guard on the kMonths[] index below.
    int mon = (tmv.tm_mon >= 0 && tmv.tm_mon < 12) ? tmv.tm_mon : 0;

    // Clipping appenders here, not the fail-closed ones. `longname` is the
    // draft-ietf-secsh-filexfer display field ("an expanded format for the file name, similar to
    // what is returned by ls -l"), so a short rendering is correct where a refused one would leave
    // a directory listing with an empty column. The wire framing is the separate length-prefixed
    // string written by protocore_sftp_wr_string, and that is never clipped. The date is appended in
    // place rather than staged: the two column widths are what `ls -l` alignment means, and
    // Sb.u64_clip states them directly.
    protocore_sb sb_out = {out, cap, 0, PROTO_TRUE};
    Sb.put_clip(&sb_out, mode);
    Sb.put_clip(&sb_out, " 1 0 0 ");
    Sb.u64_clip(&sb_out, (uint64_t)size, 0);
    Sb.put_clip(&sb_out, " ");
    Sb.put_clip(&sb_out, kMonths[mon]);
    Sb.put_clip(&sb_out, " ");
    Sb.u64_clip(&sb_out, (uint64_t)tmv.tm_mday, 2);
    Sb.put_clip(&sb_out, " ");
    Sb.u64_clip(&sb_out, (uint64_t)(tmv.tm_year + 1900), 5);
    Sb.put_clip(&sb_out, " ");
    Sb.put_clip(&sb_out, name);
    Sftp.n = Sb.finish(&sb_out);
}

SftpNs Sftp = {
    .rd_init = sftp_rd_init,
    .rd_u8 = sftp_rd_u8,
    .rd_u32 = sftp_rd_u32,
    .rd_u64 = sftp_rd_u64,
    .rd_string = sftp_rd_string,
    .rd_attrs = sftp_rd_attrs,
    .wr_init = sftp_wr_init,
    .wr_u8 = sftp_wr_u8,
    .wr_u32 = sftp_wr_u32,
    .wr_u64 = sftp_wr_u64,
    .wr_bytes = sftp_wr_bytes,
    .wr_string = sftp_wr_string,
    .wr_attrs = sftp_wr_attrs,
    .wr_finish = sftp_wr_finish,
    .wr_pos = sftp_wr_pos,
    .wr_patch_u32 = sftp_wr_patch_u32,
    .frame_len = sftp_frame_len,
    .build_version = sftp_build_version,
    .build_status = sftp_build_status,
    .build_handle = sftp_build_handle,
    .build_attrs = sftp_build_attrs,
    .build_data = sftp_build_data,
    .build_name1 = sftp_build_name1,
    .format_longname = sftp_format_longname,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_SFTP
