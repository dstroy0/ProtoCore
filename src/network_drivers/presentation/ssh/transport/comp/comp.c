// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file comp.c
 * @brief RFC 4253 sec 6.2: the per-direction compression state and its reset.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t inflate_work[16]; // the borrow an entry takes; Inflate never reads it

static uint8_t zlib_work[16]; // the borrow an entry takes; Zlib never reads it

#if PROTOCORE_ENABLE_SSH_ZLIB

#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/transport/comp/comp.h"

#include "mmgr/secure/secure.h" // protocore_secure_wipe
#include "network_drivers/presentation/ssh/transport/inflate/inflate.h"
#include "network_drivers/presentation/ssh/transport/zlib/zlib.h"

PROTOCORE_BEGIN_DECLS

// The per-connection compressor holds a window-sized work buffer + a window-sized hash chain (tens
// of KB).
#define PROTOCORE_SSH_COMP_ATTR

// Per-connection compression state (large buffers -> PSRAM; the flags are trivial).
typedef struct
{
    SshDeflate z;                           ///< streaming compressor (bound to the buffers below).
    uint8_t work[SSH_ZLIB_WORK_SIZE];       ///< history + input work buffer.
    uint16_t head[SSH_ZLIB_HASH_SIZE];      ///< hash bucket heads.
    uint16_t prev[SSH_ZLIB_WORK_SIZE];      ///< hash chain (absolute-position indexed).
    uint16_t ll_code[288];                  ///< fixed lit/length Huffman codes.
    uint8_t ll_len[288];                    ///< their bit lengths.
    uint16_t d_code[30];                    ///< fixed distance Huffman codes.
    uint8_t d_len[30];                      ///< their bit lengths.
    SshInflate inf;                         ///< streaming decompressor (bound to inf_window below).
    uint8_t inf_window[SSH_INFLATE_WINDOW]; ///< 32 KB context-takeover window for c2s inflate.
    SshCompAlg s2c_alg;                     ///< negotiated server-to-client algorithm.
    proto_bool s2c_active;                  ///< true once the s2c (deflate) stream has started.
    SshCompAlg c2s_alg;                     ///< negotiated client-to-server algorithm.
    proto_bool c2s_active;                  ///< true once the c2s (inflate) stream has started.
} SshCompState;

// All SSH compression state, owned by one instance (internal linkage): the per-connection
// deflate stream table. One named owner, unreachable from any other translation unit.
typedef struct
{
    SshCompState comp[MAX_SSH_CONNS];
} SshCompCtx;
static PROTOCORE_SSH_COMP_ATTR SshCompCtx s_ssh_comp;

static void start_s2c(SshCompState *c)
{
    Zlib.init_args.z = &c->z;
    Zlib.init_args.win = c->work;
    Zlib.init_args.head = c->head;
    Zlib.init_args.prev = c->prev;
    Zlib.init_args.ll_code = c->ll_code;
    Zlib.init_args.ll_len = c->ll_len;
    Zlib.init_args.d_code = c->d_code;
    Zlib.init_args.d_len = c->d_len;
    Zlib.init(zlib_work);
    c->s2c_active = PROTO_TRUE;
}

static void start_c2s(SshCompState *c)
{
    Inflate.init_args.z = &c->inf;
    Inflate.init_args.window = c->inf_window;
    Inflate.init(inflate_work);
    c->c2s_active = PROTO_TRUE;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void comp_reset(uint8_t *restrict work)
{
    uint8_t i = Comp.reset_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshCompState *c = &s_ssh_comp.comp[i];
    c->s2c_alg = SSH_COMP_NONE;
    c->s2c_active = PROTO_FALSE;
    c->c2s_alg = SSH_COMP_NONE;
    c->c2s_active = PROTO_FALSE;
    // The deflate work buffer and the inflate window hold the session's decompressed plaintext.
    protocore_secure_wipe(c->work, sizeof(c->work));
    protocore_secure_wipe(c->inf_window, sizeof(c->inf_window));
}

static void comp_set_s2c(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = Comp.set_s2c_args.i;
    SshCompAlg alg = Comp.set_s2c_args.alg;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    s_ssh_comp.comp[i].s2c_alg = alg;
}

static void comp_set_c2s(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = Comp.set_c2s_args.i;
    SshCompAlg alg = Comp.set_c2s_args.alg;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    s_ssh_comp.comp[i].c2s_alg = alg;
}

// "zlib" (non-delayed) starts both directions at NEWKEYS; "zlib@openssh.com" waits for auth success.
static void comp_on_newkeys(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = Comp.on_newkeys_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    // RFC 4253 sec 6.2: "The compression context is initialized after each key exchange", so a
    // re-exchange starts both streams over rather than carrying the previous LZ77 window across it.
    SshCompState *c = &s_ssh_comp.comp[i];
    if (c->s2c_alg == SSH_COMP_ZLIB)
    {
        start_s2c(c);
    }
    if (c->c2s_alg == SSH_COMP_ZLIB)
    {
        start_c2s(c);
    }
}

static void comp_on_auth_success(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = Comp.on_auth_success_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshCompState *c = &s_ssh_comp.comp[i];
    if (c->s2c_alg == SSH_COMP_ZLIB_DELAYED && !c->s2c_active)
    {
        start_s2c(c);
    }
    if (c->c2s_alg == SSH_COMP_ZLIB_DELAYED && !c->c2s_active)
    {
        start_c2s(c);
    }
}

static void comp_s2c_active(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = Comp.s2c_active_args.i;

    Comp.ok = i < MAX_SSH_CONNS && s_ssh_comp.comp[i].s2c_active;
}

static void comp_s2c(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = Comp.s2c_args.i;
    const uint8_t *src = Comp.s2c_args.src;
    size_t src_len = Comp.s2c_args.src_len;
    uint8_t *dst = Comp.s2c_args.dst;
    size_t dst_cap = Comp.s2c_args.dst_cap;
    size_t *out_len = Comp.s2c_args.out_len;

    if (i >= MAX_SSH_CONNS || !s_ssh_comp.comp[i].s2c_active)
    {
        Comp.n = -1;
        return;
    }
    Zlib.packet_args.z = &s_ssh_comp.comp[i].z;
    Zlib.packet_args.src = src;
    Zlib.packet_args.src_len = src_len;
    Zlib.packet_args.dst = dst;
    Zlib.packet_args.dst_cap = dst_cap;
    Zlib.packet_args.out_len = out_len;
    Zlib.packet(zlib_work);
    Comp.n = Zlib.n;
}

static void comp_c2s_active(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = Comp.c2s_active_args.i;

    Comp.ok = i < MAX_SSH_CONNS && s_ssh_comp.comp[i].c2s_active;
}

static void comp_c2s(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = Comp.c2s_args.i;
    const uint8_t *src = Comp.c2s_args.src;
    size_t src_len = Comp.c2s_args.src_len;
    uint8_t *dst = Comp.c2s_args.dst;
    size_t dst_cap = Comp.c2s_args.dst_cap;
    size_t *out_len = Comp.c2s_args.out_len;

    if (i >= MAX_SSH_CONNS || !s_ssh_comp.comp[i].c2s_active)
    {
        Comp.n = -1;
        return;
    }
    Inflate.packet_args.z = &s_ssh_comp.comp[i].inf;
    Inflate.packet_args.src = src;
    Inflate.packet_args.src_len = src_len;
    Inflate.packet_args.dst = dst;
    Inflate.packet_args.dst_cap = dst_cap;
    Inflate.packet_args.out_len = out_len;
    Inflate.packet(inflate_work);
    Comp.n = Inflate.n;
}

CompNs Comp = {
    .reset = comp_reset,
    .set_s2c = comp_set_s2c,
    .on_newkeys = comp_on_newkeys,
    .on_auth_success = comp_on_auth_success,
    .s2c_active = comp_s2c_active,
    .s2c = comp_s2c,
    .set_c2s = comp_set_c2s,
    .c2s_active = comp_c2s_active,
    .c2s = comp_c2s,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_ZLIB
