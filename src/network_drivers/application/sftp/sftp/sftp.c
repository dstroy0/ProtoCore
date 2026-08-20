// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sftp.c
 * @brief SFTP protocol v3 wire codec - implementation. See sftp.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SSH_SFTP

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "network_drivers/application/sftp/sftp/sftp.h"

#include "shared/time_compat/time_compat.h" // protocore_gmtime_r (portable reentrant UTC)

#include <time.h>

PROTOCORE_BEGIN_DECLS

// --- reader (big-endian, bounds-checked) ---------------------------------------------------------

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_sftp_rd_string(uint8_t *restrict work);
void protocore_sftp_rd_u32(uint8_t *restrict work);
void protocore_sftp_rd_u64(uint8_t *restrict work);
void protocore_sftp_wr_attrs(uint8_t *restrict work);
void protocore_sftp_wr_bytes(uint8_t *restrict work);
void protocore_sftp_wr_finish(uint8_t *restrict work);
void protocore_sftp_wr_init(uint8_t *restrict work);
void protocore_sftp_wr_string(uint8_t *restrict work);
void protocore_sftp_wr_u32(uint8_t *restrict work);
void protocore_sftp_wr_u64(uint8_t *restrict work);
void protocore_sftp_wr_u8(uint8_t *restrict work);

void protocore_sftp_rd_init(uint8_t *restrict work)
{
    (void)work;
    SftpReader *r = SftpV.rd_init_args.r;
    const uint8_t *payload = SftpV.rd_init_args.payload;
    size_t len = SftpV.rd_init_args.len;

    r->p = payload;
    r->len = len;
    r->off = 0;
    r->ok = PROTO_TRUE;
}

void protocore_sftp_rd_u8(uint8_t *restrict work)
{
    (void)work;
    SftpReader *r = SftpV.rd_u8_args.r;

    if (!r->ok || r->off + 1 > r->len)
    {
        r->ok = PROTO_FALSE;
        SftpV.value = 0;
        return;
    }
    SftpV.value = r->p[r->off++];
}

void protocore_sftp_rd_u32(uint8_t *restrict work)
{
    (void)work;
    SftpReader *r = SftpV.rd_u32_args.r;

    if (!r->ok || r->off + 4 > r->len)
    {
        r->ok = PROTO_FALSE;
        SftpV.u32 = 0;
        return;
    }
    uint32_t v = ((uint32_t)r->p[r->off] << 24) | ((uint32_t)r->p[r->off + 1] << 16) |
                 ((uint32_t)r->p[r->off + 2] << 8) | (uint32_t)r->p[r->off + 3];
    r->off += 4;
    SftpV.u32 = v;
}

void protocore_sftp_rd_u64(uint8_t *restrict work)
{
    (void)work;
    SftpReader *r = SftpV.rd_u64_args.r;

    if (!r->ok || r->off + 8 > r->len)
    {
        r->ok = PROTO_FALSE;
        SftpV.u64 = 0;
        return;
    }
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
    {
        v = (v << 8) | r->p[r->off + i];
    }
    r->off += 8;
    SftpV.u64 = v;
}

void protocore_sftp_rd_string(uint8_t *restrict work)
{
    SftpReader *r = SftpV.rd_string_args.r;
    const uint8_t **out = SftpV.rd_string_args.out;
    uint32_t *out_len = SftpV.rd_string_args.out_len;

    SftpV.rd_u32_args.r = r;
    protocore_sftp_rd_u32(work);
    uint32_t n = SftpV.u32;
    if (!r->ok || r->off + n > r->len)
    {
        r->ok = PROTO_FALSE;
        SftpV.ok = PROTO_FALSE;
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
    SftpV.ok = PROTO_TRUE;
}

void protocore_sftp_rd_attrs(uint8_t *restrict work)
{
    SftpReader *r = SftpV.rd_attrs_args.r;
    SftpAttrs *a = SftpV.rd_attrs_args.a;

    SftpV.rd_u32_args.r = r;
    protocore_sftp_rd_u32(work);
    a->flags = SftpV.u32;
    a->size = 0;
    a->permissions = 0;
    a->atime = 0;
    a->mtime = 0;
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_SIZE)
    {
        SftpV.rd_u64_args.r = r;
        protocore_sftp_rd_u64(work);
        a->size = SftpV.u64;
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_UIDGID)
    {
        SftpV.rd_u32_args.r = r;
        protocore_sftp_rd_u32(work); // uid (ignored)
        SftpV.rd_u32_args.r = r;
        protocore_sftp_rd_u32(work); // gid (ignored)
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_PERMS)
    {
        SftpV.rd_u32_args.r = r;
        protocore_sftp_rd_u32(work);
        a->permissions = SftpV.u32;
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME)
    {
        SftpV.rd_u32_args.r = r;
        protocore_sftp_rd_u32(work);
        a->atime = SftpV.u32;
        SftpV.rd_u32_args.r = r;
        protocore_sftp_rd_u32(work);
        a->mtime = SftpV.u32;
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_EXTENDED)
    {
        SftpV.rd_u32_args.r = r;
        protocore_sftp_rd_u32(work);
        uint32_t ec = SftpV.u32;
        for (uint32_t i = 0; i < ec && r->ok; i++)
        {
            SftpV.rd_string_args.r = r;
            SftpV.rd_string_args.out = NULL;
            SftpV.rd_string_args.out_len = NULL;
            protocore_sftp_rd_string(work); // extended type
            SftpV.rd_string_args.r = r;
            SftpV.rd_string_args.out = NULL;
            SftpV.rd_string_args.out_len = NULL;
            protocore_sftp_rd_string(work); // extended data
        }
    }
    SftpV.ok = r->ok;
}

// --- writer --------------------------------------------------------------------------------------

void protocore_sftp_wr_init(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = SftpV.wr_init_args.w;
    uint8_t *out = SftpV.wr_init_args.out;
    size_t cap = SftpV.wr_init_args.cap;

    w->p = out;
    w->cap = cap;
    w->off = 4; // reserve the length prefix
    w->ovf = (cap < 4);
}

void protocore_sftp_wr_u8(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = SftpV.wr_u8_args.w;
    uint8_t v = SftpV.wr_u8_args.v;

    if (w->ovf || w->off + 1 > w->cap)
    {
        w->ovf = PROTO_TRUE;
        return;
    }
    w->p[w->off++] = v;
}

void protocore_sftp_wr_u32(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = SftpV.wr_u32_args.w;
    uint32_t v = SftpV.wr_u32_args.v;

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

void protocore_sftp_wr_u64(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = SftpV.wr_u64_args.w;
    uint64_t v = SftpV.wr_u64_args.v;

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

void protocore_sftp_wr_bytes(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = SftpV.wr_bytes_args.w;
    const void *b = SftpV.wr_bytes_args.b;
    size_t n = SftpV.wr_bytes_args.n;

    if (w->ovf || w->off + n > w->cap)
    {
        w->ovf = PROTO_TRUE;
        return;
    }
    mem.cpy(w->p + w->off, b, n);
    w->off += n;
}

void protocore_sftp_wr_string(uint8_t *restrict work)
{
    SftpWriter *w = SftpV.wr_string_args.w;
    const void *s = SftpV.wr_string_args.s;
    uint32_t n = SftpV.wr_string_args.n;

    SftpV.wr_u32_args.w = w;
    SftpV.wr_u32_args.v = n;
    protocore_sftp_wr_u32(work);
    SftpV.wr_bytes_args.w = w;
    SftpV.wr_bytes_args.b = s;
    SftpV.wr_bytes_args.n = n;
    protocore_sftp_wr_bytes(work);
}

void protocore_sftp_wr_attrs(uint8_t *restrict work)
{
    SftpWriter *w = SftpV.wr_attrs_args.w;
    const SftpAttrs *a = SftpV.wr_attrs_args.a;

    SftpV.wr_u32_args.w = w;
    SftpV.wr_u32_args.v = a->flags;
    protocore_sftp_wr_u32(work);
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_SIZE)
    {
        SftpV.wr_u64_args.w = w;
        SftpV.wr_u64_args.v = a->size;
        protocore_sftp_wr_u64(work);
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_UIDGID)
    {
        SftpV.wr_u32_args.w = w;
        SftpV.wr_u32_args.v = 0;
        protocore_sftp_wr_u32(work);
        SftpV.wr_u32_args.w = w;
        SftpV.wr_u32_args.v = 0;
        protocore_sftp_wr_u32(work);
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_PERMS)
    {
        SftpV.wr_u32_args.w = w;
        SftpV.wr_u32_args.v = a->permissions;
        protocore_sftp_wr_u32(work);
    }
    if (a->flags & PROTOCORE_SSH_FILEXFER_ATTR_ACMODTIME)
    {
        SftpV.wr_u32_args.w = w;
        SftpV.wr_u32_args.v = a->atime;
        protocore_sftp_wr_u32(work);
        SftpV.wr_u32_args.w = w;
        SftpV.wr_u32_args.v = a->mtime;
        protocore_sftp_wr_u32(work);
    }
}

void protocore_sftp_wr_finish(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = SftpV.wr_finish_args.w;

    if (w->ovf)
    {
        SftpV.n = 0;
        return;
    }
    uint32_t plen = (uint32_t)(w->off - 4);
    w->p[0] = (uint8_t)(plen >> 24);
    w->p[1] = (uint8_t)(plen >> 16);
    w->p[2] = (uint8_t)(plen >> 8);
    w->p[3] = (uint8_t)plen;
    SftpV.n = w->off;
}

void protocore_sftp_wr_pos(uint8_t *restrict work)
{
    (void)work;
    const SftpWriter *w = SftpV.wr_pos_args.w;

    SftpV.n = w->off;
}

void protocore_sftp_wr_patch_u32(uint8_t *restrict work)
{
    (void)work;
    SftpWriter *w = SftpV.wr_patch_u32_args.w;
    size_t at = SftpV.wr_patch_u32_args.at;
    uint32_t v = SftpV.wr_patch_u32_args.v;

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

void protocore_sftp_frame_len(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = SftpV.frame_len_args.buf;
    size_t have = SftpV.frame_len_args.have;
    size_t max = SftpV.frame_len_args.max;

    if (have < 4)
    {
        SftpV.n = 0; // need at least the length prefix
        return;
    }
    uint32_t plen = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    size_t total = (size_t)plen + 4;
    if (plen == 0 || total > max)
    {
        SftpV.n = (size_t)-1; // malformed (0-length) or larger than the caller can hold -> drop
        return;
    }
    SftpV.n = total;
}

// --- response builders ---------------------------------------------------------------------------

void protocore_sftp_build_version(uint8_t *restrict work)
{
    uint8_t *out = SftpV.build_version_args.out;
    size_t cap = SftpV.build_version_args.cap;

    SftpWriter w;
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = out;
    SftpV.wr_init_args.cap = cap;
    protocore_sftp_wr_init(work);
    SftpV.wr_u8_args.w = &w;
    SftpV.wr_u8_args.v = PROTOCORE_SSH_FXP_VERSION;
    protocore_sftp_wr_u8(work);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = PROTOCORE_SFTP_VERSION;
    protocore_sftp_wr_u32(work);
    SftpV.wr_finish_args.w = &w;
    protocore_sftp_wr_finish(work);
}

void protocore_sftp_build_status(uint8_t *restrict work)
{
    uint32_t id = SftpV.build_status_args.id;
    uint32_t code = SftpV.build_status_args.code;
    const char *msg = SftpV.build_status_args.msg;
    uint8_t *out = SftpV.build_status_args.out;
    size_t cap = SftpV.build_status_args.cap;

    SftpWriter w;
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = out;
    SftpV.wr_init_args.cap = cap;
    protocore_sftp_wr_init(work);
    SftpV.wr_u8_args.w = &w;
    SftpV.wr_u8_args.v = PROTOCORE_SSH_FXP_STATUS;
    protocore_sftp_wr_u8(work);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = id;
    protocore_sftp_wr_u32(work);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = code;
    protocore_sftp_wr_u32(work);
    size_t ml = msg ? str.len(msg, cap) : 0;
    SftpV.wr_string_args.w = &w;
    SftpV.wr_string_args.s = msg ? msg : "";
    SftpV.wr_string_args.n = (uint32_t)ml;
    protocore_sftp_wr_string(work);
    SftpV.wr_string_args.w = &w;
    SftpV.wr_string_args.s = "";
    SftpV.wr_string_args.n = 0;
    protocore_sftp_wr_string(work); // language tag
    SftpV.wr_finish_args.w = &w;
    protocore_sftp_wr_finish(work);
}

void protocore_sftp_build_handle(uint8_t *restrict work)
{
    uint32_t id = SftpV.build_handle_args.id;
    const void *handle = SftpV.build_handle_args.handle;
    uint32_t hlen = SftpV.build_handle_args.hlen;
    uint8_t *out = SftpV.build_handle_args.out;
    size_t cap = SftpV.build_handle_args.cap;

    SftpWriter w;
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = out;
    SftpV.wr_init_args.cap = cap;
    protocore_sftp_wr_init(work);
    SftpV.wr_u8_args.w = &w;
    SftpV.wr_u8_args.v = PROTOCORE_SSH_FXP_HANDLE;
    protocore_sftp_wr_u8(work);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = id;
    protocore_sftp_wr_u32(work);
    SftpV.wr_string_args.w = &w;
    SftpV.wr_string_args.s = handle;
    SftpV.wr_string_args.n = hlen;
    protocore_sftp_wr_string(work);
    SftpV.wr_finish_args.w = &w;
    protocore_sftp_wr_finish(work);
}

void protocore_sftp_build_attrs(uint8_t *restrict work)
{
    uint32_t id = SftpV.build_attrs_args.id;
    const SftpAttrs *a = SftpV.build_attrs_args.a;
    uint8_t *out = SftpV.build_attrs_args.out;
    size_t cap = SftpV.build_attrs_args.cap;

    SftpWriter w;
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = out;
    SftpV.wr_init_args.cap = cap;
    protocore_sftp_wr_init(work);
    SftpV.wr_u8_args.w = &w;
    SftpV.wr_u8_args.v = PROTOCORE_SSH_FXP_ATTRS;
    protocore_sftp_wr_u8(work);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = id;
    protocore_sftp_wr_u32(work);
    SftpV.wr_attrs_args.w = &w;
    SftpV.wr_attrs_args.a = a;
    protocore_sftp_wr_attrs(work);
    SftpV.wr_finish_args.w = &w;
    protocore_sftp_wr_finish(work);
}

void protocore_sftp_build_data(uint8_t *restrict work)
{
    uint32_t id = SftpV.build_data_args.id;
    const void *data = SftpV.build_data_args.data;
    uint32_t dlen = SftpV.build_data_args.dlen;
    uint8_t *out = SftpV.build_data_args.out;
    size_t cap = SftpV.build_data_args.cap;

    SftpWriter w;
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = out;
    SftpV.wr_init_args.cap = cap;
    protocore_sftp_wr_init(work);
    SftpV.wr_u8_args.w = &w;
    SftpV.wr_u8_args.v = PROTOCORE_SSH_FXP_DATA;
    protocore_sftp_wr_u8(work);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = id;
    protocore_sftp_wr_u32(work);
    SftpV.wr_string_args.w = &w;
    SftpV.wr_string_args.s = data;
    SftpV.wr_string_args.n = dlen;
    protocore_sftp_wr_string(work);
    SftpV.wr_finish_args.w = &w;
    protocore_sftp_wr_finish(work);
}

void protocore_sftp_build_name1(uint8_t *restrict work)
{
    uint32_t id = SftpV.build_name1_args.id;
    const char *name = SftpV.build_name1_args.name;
    const char *longname = SftpV.build_name1_args.longname;
    const SftpAttrs *a = SftpV.build_name1_args.a;
    uint8_t *out = SftpV.build_name1_args.out;
    size_t cap = SftpV.build_name1_args.cap;

    SftpWriter w;
    SftpV.wr_init_args.w = &w;
    SftpV.wr_init_args.out = out;
    SftpV.wr_init_args.cap = cap;
    protocore_sftp_wr_init(work);
    SftpV.wr_u8_args.w = &w;
    SftpV.wr_u8_args.v = PROTOCORE_SSH_FXP_NAME;
    protocore_sftp_wr_u8(work);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = id;
    protocore_sftp_wr_u32(work);
    SftpV.wr_u32_args.w = &w;
    SftpV.wr_u32_args.v = 1;
    protocore_sftp_wr_u32(work); // one entry
    SftpV.wr_string_args.w = &w;
    SftpV.wr_string_args.s = name;
    SftpV.wr_string_args.n = (uint32_t)str.len(name, cap);
    protocore_sftp_wr_string(work);
    SftpV.wr_string_args.w = &w;
    SftpV.wr_string_args.s = longname;
    SftpV.wr_string_args.n = (uint32_t)str.len(longname, cap);
    protocore_sftp_wr_string(work);
    SftpV.wr_attrs_args.w = &w;
    SftpV.wr_attrs_args.a = a;
    protocore_sftp_wr_attrs(work);
    SftpV.wr_finish_args.w = &w;
    protocore_sftp_wr_finish(work);
}

void protocore_sftp_format_longname(uint8_t *restrict work)
{
    (void)work;
    proto_bool is_dir = SftpV.format_longname_args.is_dir;
    uint32_t perms = SftpV.format_longname_args.perms;
    uint64_t size = SftpV.format_longname_args.size;
    uint32_t mtime = SftpV.format_longname_args.mtime;
    const char *name = SftpV.format_longname_args.name;
    char *out = SftpV.format_longname_args.out;
    size_t cap = SftpV.format_longname_args.cap;

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
    TimeCompatV.args.epoch = (time_t)mtime; // mtime==0 -> epoch, a harmless placeholder date
    TimeCompatV.args.out = &tmv;
    TimeCompat.gmtime(work);
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
    SftpV.n = Sb.finish(&sb_out);
}

/** @brief The operands and the outcome. */
SftpVars SftpV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_SFTP
