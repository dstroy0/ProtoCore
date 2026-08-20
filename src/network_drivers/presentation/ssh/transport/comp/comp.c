// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file comp.c
 * @brief RFC 4253 sec 6.2: the per-direction compression state and its reset.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SSH_ZLIB

#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/transport/comp/comp.h"

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/secure/secure.h"       // protocore_secure_wipe
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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define COMP_OFF_CTX 0u
static_assert(COMP_OFF_CTX + sizeof(SshCompCtx) <= PROTOCORE_SSH_COMP_BORROW,
              "PROTOCORE_SSH_COMP_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(COMP_OFF_CTX % _Alignof(SshCompCtx) == 0,
              "COMP_OFF_CTX is not a multiple of alignof(SshCompCtx) - COMP_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define COMP_CTX(w) ((SshCompCtx *)(void *)((w) + COMP_OFF_CTX))

// The one owned instance, private to this TU: the pointer to the bytes taken for the table.
static uint8_t *s_span;

// Not an entry: an entry takes a borrow and this is where that borrow comes from. The transport and
// the server both drive one connection's compressor, so the bytes are the module's rather than
// either caller's, and they are persistent because the streams take context over from packet to
// packet. Plaintext: session data, no key material.
uint8_t *protocore_ssh_comp_span(void)
{
    if (s_span == NULL)
    {
        s_span = protocore_plaintext_persist_span(PROTOCORE_SSH_COMP_BORROW).buf;
    }
    return s_span;
}

static void start_s2c(SshCompState *c)
{
    ZlibV.init_args.z = &c->z;
    ZlibV.init_args.win = c->work;
    ZlibV.init_args.head = c->head;
    ZlibV.init_args.prev = c->prev;
    ZlibV.init_args.ll_code = c->ll_code;
    ZlibV.init_args.ll_len = c->ll_len;
    ZlibV.init_args.d_code = c->d_code;
    ZlibV.init_args.d_len = c->d_len;
    Zlib.init(protocore_ssh_comp_span());
    c->s2c_active = PROTO_TRUE;
}

static void start_c2s(SshCompState *c)
{
    InflateV.init_args.z = &c->inf;
    InflateV.init_args.window = c->inf_window;
    Inflate.init(protocore_ssh_comp_span());
    c->c2s_active = PROTO_TRUE;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_comp_reset(uint8_t *restrict work)
{
    uint8_t i = CompV.reset_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshCompState *c = &COMP_CTX(work)->comp[i];
    c->s2c_alg = SSH_COMP_NONE;
    c->s2c_active = PROTO_FALSE;
    c->c2s_alg = SSH_COMP_NONE;
    c->c2s_active = PROTO_FALSE;
    // The deflate work buffer and the inflate window hold the session's decompressed plaintext.
    protocore_secure_wipe(c->work, sizeof(c->work));
    protocore_secure_wipe(c->inf_window, sizeof(c->inf_window));
}

void protocore_comp_set_s2c(uint8_t *restrict work)
{
    uint8_t i = CompV.set_s2c_args.i;
    SshCompAlg alg = CompV.set_s2c_args.alg;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    COMP_CTX(work)->comp[i].s2c_alg = alg;
}

void protocore_comp_set_c2s(uint8_t *restrict work)
{
    uint8_t i = CompV.set_c2s_args.i;
    SshCompAlg alg = CompV.set_c2s_args.alg;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    COMP_CTX(work)->comp[i].c2s_alg = alg;
}

// "zlib" (non-delayed) starts both directions at NEWKEYS; "zlib@openssh.com" waits for auth success.
void protocore_comp_on_newkeys(uint8_t *restrict work)
{
    uint8_t i = CompV.on_newkeys_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    // RFC 4253 sec 6.2: "The compression context is initialized after each key exchange", so a
    // re-exchange starts both streams over rather than carrying the previous LZ77 window across it.
    SshCompState *c = &COMP_CTX(work)->comp[i];
    if (c->s2c_alg == SSH_COMP_ZLIB)
    {
        start_s2c(c);
    }
    if (c->c2s_alg == SSH_COMP_ZLIB)
    {
        start_c2s(c);
    }
}

void protocore_comp_on_auth_success(uint8_t *restrict work)
{
    uint8_t i = CompV.on_auth_success_args.i;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshCompState *c = &COMP_CTX(work)->comp[i];
    if (c->s2c_alg == SSH_COMP_ZLIB_DELAYED && !c->s2c_active)
    {
        start_s2c(c);
    }
    if (c->c2s_alg == SSH_COMP_ZLIB_DELAYED && !c->c2s_active)
    {
        start_c2s(c);
    }
}

void protocore_comp_s2c_active(uint8_t *restrict work)
{
    uint8_t i = CompV.s2c_active_args.i;

    CompV.ok = i < MAX_SSH_CONNS && COMP_CTX(work)->comp[i].s2c_active;
}

void protocore_comp_s2c(uint8_t *restrict work)
{
    uint8_t i = CompV.s2c_args.i;
    const uint8_t *src = CompV.s2c_args.src;
    size_t src_len = CompV.s2c_args.src_len;
    uint8_t *dst = CompV.s2c_args.dst;
    size_t dst_cap = CompV.s2c_args.dst_cap;
    size_t *out_len = CompV.s2c_args.out_len;

    if (i >= MAX_SSH_CONNS || !COMP_CTX(work)->comp[i].s2c_active)
    {
        CompV.n = -1;
        return;
    }
    ZlibV.packet_args.z = &COMP_CTX(work)->comp[i].z;
    ZlibV.packet_args.src = src;
    ZlibV.packet_args.src_len = src_len;
    ZlibV.packet_args.dst = dst;
    ZlibV.packet_args.dst_cap = dst_cap;
    ZlibV.packet_args.out_len = out_len;
    Zlib.packet(work);
    CompV.n = ZlibV.n;
}

void protocore_comp_c2s_active(uint8_t *restrict work)
{
    uint8_t i = CompV.c2s_active_args.i;

    CompV.ok = i < MAX_SSH_CONNS && COMP_CTX(work)->comp[i].c2s_active;
}

void protocore_comp_c2s(uint8_t *restrict work)
{
    uint8_t i = CompV.c2s_args.i;
    const uint8_t *src = CompV.c2s_args.src;
    size_t src_len = CompV.c2s_args.src_len;
    uint8_t *dst = CompV.c2s_args.dst;
    size_t dst_cap = CompV.c2s_args.dst_cap;
    size_t *out_len = CompV.c2s_args.out_len;

    if (i >= MAX_SSH_CONNS || !COMP_CTX(work)->comp[i].c2s_active)
    {
        CompV.n = -1;
        return;
    }
    InflateV.packet_args.z = &COMP_CTX(work)->comp[i].inf;
    InflateV.packet_args.src = src;
    InflateV.packet_args.src_len = src_len;
    InflateV.packet_args.dst = dst;
    InflateV.packet_args.dst_cap = dst_cap;
    InflateV.packet_args.out_len = out_len;
    Inflate.packet(work);
    CompV.n = InflateV.n;
}

/** @brief The operands and the outcome. */
CompVars CompV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_ZLIB
