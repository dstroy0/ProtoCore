// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file exc_coredump.c
 * @brief Read the stored crash image and offload it (see exc_decoder.h).
 *
 * The panic text a device prints dies with the reboot; the crash image the part writes to flash does
 * not. This reads that image back on the next boot - as a summary for the live panel, and as raw
 * bytes for a durable copy - then erases it so the next crash gets the space.
 *
 * Where the image lives, and how a summary is laid out, are the platform's answer: this reaches both
 * through the protocore_platform_crashdump_* seam, so the module runs the same on a host with the
 * seam mocked. The decoding/serialization of a panic stays pure in exc_decoder.c.
 */

#include "core_setup/board_profiles/protocore_platform.h"
#include "mmgr/protomem.h"
#include "mmgr/protostr.h"
#include "server/core/exc_decoder.h"

#if PROTOCORE_ENABLE_EXC_DECODER && PROTOCORE_HAS_VENDOR_COREDUMP

void protocore_exc_cd_present(struct ExcDecoderInternal *restrict ctx)
{
    ExcCoreDump *out = ctx->ns->dump.img;

    // The seam reports what is stored only once it verifies, so a torn write from a crash mid-dump
    // is reported as absent rather than handed on as a valid image.
    uint32_t size = protocore_platform_crashdump_size();
    ctx->ns->ok = PROTO_FALSE;
    if (size == 0)
    {
        return;
    }
    if (out)
    {
        out->addr = 0; // the image's location is the platform's, and does not cross the seam
        out->size = size;
    }
    ctx->ns->ok = PROTO_TRUE;
}

void protocore_exc_cd_summary(struct ExcDecoderInternal *restrict ctx)
{
    ExcInfo *out = ctx->ns->parse_args.info;

    ctx->ns->ok = PROTO_FALSE;
    if (!out)
    {
        return;
    }
    protocore_crash_summary s;
    mem.set(&s, 0, sizeof(s));
    if (!protocore_platform_crashdump_summary(&s))
    {
        return;
    }

    mem.set(out, 0, sizeof(*out));
    out->core = -1; // the summary does not name the core; the raw image does
    out->pc = s.pc;
    // The crashing task name is the most useful short label the summary carries, so it fills the
    // slot the console decoder puts the panic cause in.
    str.copy(out->cause, s.task, sizeof(out->cause));

    out->excvaddr = s.fault_addr;
    out->has_excvaddr = s.has_fault_addr ? PROTO_TRUE : PROTO_FALSE;

    // A part that stores a stack dump rather than a walkable backtrace reports no frames, and none
    // are invented here: unwinding one needs debug information that lives off the device.
    size_t depth = s.frame_count;
    if (depth > PROTOCORE_EXC_MAX_FRAMES)
    {
        depth = PROTOCORE_EXC_MAX_FRAMES;
    }
    for (size_t i = 0; i < depth; i++)
    {
        out->frames[i].pc = s.frame_pc[i];
        out->frames[i].sp = 0; // not part of the summary
    }
    out->frame_count = depth;
    ctx->ns->ok = PROTO_TRUE;
}

void protocore_exc_cd_read(struct ExcDecoderInternal *restrict ctx)
{
    void *buf = ctx->ns->dump.buf;
    const size_t len = ctx->ns->dump.len;

    ctx->ns->ok = PROTO_FALSE;
    if (!buf)
    {
        return;
    }
    if (len == 0)
    {
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    // The seam refuses a range that runs past the image rather than returning whatever follows it.
    ctx->ns->ok = protocore_platform_crashdump_read((uint32_t)ctx->ns->dump.offset, (uint8_t *)buf, (uint32_t)len)
                      ? PROTO_TRUE
                      : PROTO_FALSE;
}

void protocore_exc_cd_save(struct ExcDecoderInternal *restrict ctx)
{
    const protocore_mnt_backend *file_sys = ctx->ns->dump.file_sys;
    const char *path = ctx->ns->dump.path;

    ctx->ns->ok = PROTO_FALSE;
    if (!file_sys || !path || path[0] == '\0')
    {
        return;
    }

    uint32_t size = protocore_platform_crashdump_size();
    if (size == 0)
    {
        return;
    }

    int fh = file_sys->open(path, PROTOCORE_MNT_WRITE);
    if (fh < 0)
    {
        return;
    }

    // Streamed in fixed chunks: a dump can be tens of KB and must never need to fit RAM at once.
    uint8_t buf[PROTOCORE_EXC_COREDUMP_CHUNK];
    uint32_t off = 0;
    proto_bool ok = PROTO_TRUE;
    while (off < size)
    {
        uint32_t n = (size - off < sizeof(buf)) ? size - off : (uint32_t)sizeof(buf);
        if (!protocore_platform_crashdump_read(off, buf, n) || file_sys->write(fh, buf, n) != (int)n)
        {
            ok = PROTO_FALSE;
            break;
        }
        off += n;
    }
    file_sys->close(fh);
    if (!ok)
    {
        (void)file_sys->remove(path); // never leave a half-written dump that looks complete
    }
    ctx->ns->ok = ok;
}

void protocore_exc_cd_erase(struct ExcDecoderInternal *restrict ctx)
{
    ctx->ns->ok = protocore_platform_crashdump_erase() ? PROTO_TRUE : PROTO_FALSE;
}

#endif // PROTOCORE_ENABLE_EXC_DECODER && PROTOCORE_HAS_VENDOR_COREDUMP
