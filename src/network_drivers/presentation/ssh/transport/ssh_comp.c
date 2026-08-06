// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_comp.c
 * @brief SSH server-to-client compression owner - per-connection state + activation.
 */

#include "network_drivers/presentation/ssh/transport/ssh_comp.h"

#if PC_ENABLE_SSH_ZLIB

#include "network_drivers/presentation/ssh/transport/ssh_inflate.h"
#include "network_drivers/presentation/ssh/transport/ssh_zlib.h"

// The per-connection compressor holds a window-sized work buffer + a window-sized hash chain (tens
// of KB); the pool does not fit internal DRAM alongside the SSH crypto stack, so it lives in PSRAM
// (PC_SSH_ZLIB_IN_PSRAM). Same mechanism/caveat as the TLS arena and HTTP/2 pool: it needs a
// framework built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y (the stock arduino-esp32 core
// ships it OFF, so EXT_RAM_BSS_ATTR would silently no-op); see tools/psram/README.md.
#if PC_SSH_ZLIB_IN_PSRAM && PROTOCORE_HOT
#include <esp_attr.h> // pulls in sdkconfig.h -> CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#if !defined(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
#error                                                                                                                 \
    "PC_SSH_ZLIB_IN_PSRAM needs a framework built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y. The stock arduino-esp32 core ships it OFF, so EXT_RAM_BSS_ATTR silently no-ops and the compressor pool would overflow internal DRAM. Rebuild the core (tools/psram/README.md) or unset PC_SSH_ZLIB_IN_PSRAM."
#endif
#if defined(EXT_RAM_BSS_ATTR)
#define PC_SSH_COMP_ATTR EXT_RAM_BSS_ATTR // IDF v5 / arduino-esp32 3.x
#elif defined(EXT_RAM_ATTR)
#define PC_SSH_COMP_ATTR EXT_RAM_ATTR // IDF v4 / arduino-esp32 2.x
#else
#define PC_SSH_COMP_ATTR
#endif
#else
#define PC_SSH_COMP_ATTR
#endif

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
static PC_SSH_COMP_ATTR SshCompCtx s_ssh_comp;

static void start_s2c(SshCompState *c)
{
    SshDeflater.init(&c->z, c->work, c->head, c->prev, c->ll_code, c->ll_len, c->d_code, c->d_len);
    c->s2c_active = PROTO_TRUE;
}

static void start_c2s(SshCompState *c)
{
    SshInflater.init(&c->inf, c->inf_window);
    c->c2s_active = PROTO_TRUE;
}

static void ssh_comp_reset(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshCompState *c = &s_ssh_comp.comp[i];
    c->s2c_alg = SSH_COMP_NONE;
    c->s2c_active = PROTO_FALSE;
    c->c2s_alg = SSH_COMP_NONE;
    c->c2s_active = PROTO_FALSE;
}

static void ssh_comp_set_s2c(uint8_t i, SshCompAlg alg)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    s_ssh_comp.comp[i].s2c_alg = alg;
}

static void ssh_comp_set_c2s(uint8_t i, SshCompAlg alg)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    s_ssh_comp.comp[i].c2s_alg = alg;
}

// "zlib" (non-delayed) starts both directions at NEWKEYS; "zlib@openssh.com" waits for auth success.
static void ssh_comp_on_newkeys(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshCompState *c = &s_ssh_comp.comp[i];
    if (c->s2c_alg == SSH_COMP_ZLIB && !c->s2c_active)
    {
        start_s2c(c);
    }
    if (c->c2s_alg == SSH_COMP_ZLIB && !c->c2s_active)
    {
        start_c2s(c);
    }
}

static void ssh_comp_on_auth_success(uint8_t i)
{
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

static proto_bool ssh_comp_s2c_active(uint8_t i)
{
    return i < MAX_SSH_CONNS && s_ssh_comp.comp[i].s2c_active;
}

static int ssh_comp_s2c(uint8_t i, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len)
{
    if (i >= MAX_SSH_CONNS || !s_ssh_comp.comp[i].s2c_active)
    {
        return -1;
    }
    return SshDeflater.packet(&s_ssh_comp.comp[i].z, src, src_len, dst, dst_cap, out_len);
}

static proto_bool ssh_comp_c2s_active(uint8_t i)
{
    return i < MAX_SSH_CONNS && s_ssh_comp.comp[i].c2s_active;
}

static int ssh_comp_c2s(uint8_t i, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap, size_t *out_len)
{
    if (i >= MAX_SSH_CONNS || !s_ssh_comp.comp[i].c2s_active)
    {
        return -1;
    }
    return SshInflater.packet(&s_ssh_comp.comp[i].inf, src, src_len, dst, dst_cap, out_len);
}

const SshCompNs SshComp = {ssh_comp_reset,      ssh_comp_set_s2c, ssh_comp_on_newkeys, ssh_comp_on_auth_success,
                           ssh_comp_s2c_active, ssh_comp_s2c,     ssh_comp_set_c2s,    ssh_comp_c2s_active,
                           ssh_comp_c2s,        &SshDeflater,     &SshInflater};

#endif // PC_ENABLE_SSH_ZLIB
