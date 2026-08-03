// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls.cpp
 * @brief Deterministic TLS engine implementation (mbedTLS + static pool).
 *
 * ESP32/Arduino only. All mbedTLS allocations are served from a fixed BSS arena
 * (MBEDTLS_MEMORY_BUFFER_ALLOC_C) so no system heap is touched; the RNG is the
 * ESP32 hardware CSPRNG; the transport BIO reads ciphertext straight from the
 * connection's rx ring and writes via tcp_write. v2/v3 mbedTLS differences are
 * bridged with MBEDTLS_VERSION_MAJOR guards (same approach as the SSH layer).
 */

#include "network_drivers/tls/tls.h"
#include "server/clock/clock.h" // pcdelay

#if PC_ENABLE_TLS && PROTOCORE_HOT

#include "lwip/tcp.h"
#include "network_drivers/transport/tcp.h"
#include <Arduino.h> // millis() for the blocking client loop
#include <string.h>

#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform.h> // mbedtls_platform_set_calloc_free
#include <mbedtls/sha256.h>   // peer-cert pin hashing (client verification)
#include <mbedtls/ssl.h>
#include <mbedtls/version.h>
#include <mbedtls/x509_crt.h>
#if PC_ENABLE_TLS_RESUMPTION
#include <mbedtls/ssl_ticket.h> // RFC 5077 session tickets (server-side resumption)
#if !defined(MBEDTLS_SSL_TICKET_C) || !defined(MBEDTLS_SSL_SESSION_TICKETS)
#error "PC_ENABLE_TLS_RESUMPTION needs an mbedTLS build with MBEDTLS_SSL_TICKET_C + MBEDTLS_SSL_SESSION_TICKETS"
#endif
#endif

// ---------------------------------------------------------------------------
// Static memory pool - a minimal first-fit allocator over a fixed BSS arena.
//
// The precompiled Arduino mbedTLS does not ship MBEDTLS_MEMORY_BUFFER_ALLOC_C,
// so instead we install our own calloc/free via mbedtls_platform_set_calloc_free
// (MBEDTLS_PLATFORM_MEMORY). Every mbedTLS allocation (record buffers, handshake
// temporaries, cert/key) is then served from s_pool.arena - no system heap, so the
// determinism guarantee holds. Bounded by the arena: exhaustion fails the
// handshake cleanly (calloc returns NULL) rather than corrupting anything.
//
// Placement: by default the arena is internal DRAM (.bss). On a board with PSRAM,
// set PC_TLS_ARENA_IN_PSRAM=1 to move it to external RAM (frees ~PC_TLS_ARENA_SIZE
// of the ~122 KB internal dram0_0_seg budget, so several concurrent connections fit).
//
// This needs a framework built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y, which the
// STOCK arduino-esp32 core (2.x and 3.x, both PlatformIO and arduino-cli) ships OFF. With it
// off, EXT_RAM_BSS_ATTR silently expands to nothing and the arena falls back to internal DRAM
// - i.e. the PSRAM offload silently does not happen. Rather than fail silently we FAIL THE
// COMPILE and point at the rebuild recipe (tools/psram/README.md). Rebuild the core (pioarduino
// custom_sdkconfig, or esp32-arduino-lib-builder) or unset PC_TLS_ARENA_IN_PSRAM.
//
// FLASH-CACHE CAVEAT: PSRAM is on the flash cache bus, so while flash is being written (an NVS
// commit, an OTA) PSRAM is briefly unreadable - TLS code that touches the arena during that
// window faults. It is a single pool, so this is a whole-build choice: use PSRAM for a TLS
// workload that does not write flash while serving; keep the arena in internal DRAM (the
// default) if you do OTA / NVS / file-serving concurrently with live TLS. See
// docs/KNOWN_LIMITATIONS.md (TLS -> "Flash-cache / OTA caveat").
// ---------------------------------------------------------------------------
#if PC_TLS_ARENA_IN_PSRAM && PROTOCORE_HOT
#include <esp_attr.h> // pulls in sdkconfig.h -> CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#if !defined(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
#error                                                                                                                 \
    "PC_TLS_ARENA_IN_PSRAM needs a framework built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y. The stock arduino-esp32 core ships it OFF, so EXT_RAM_BSS_ATTR silently no-ops and the arena would stay in internal RAM. Rebuild the core (see tools/psram/README.md) or unset PC_TLS_ARENA_IN_PSRAM."
#endif
#if defined(EXT_RAM_BSS_ATTR)
#define PC_TLS_ARENA_ATTR EXT_RAM_BSS_ATTR // IDF v5 / arduino-esp32 3.x
#elif defined(EXT_RAM_ATTR)
#define PC_TLS_ARENA_ATTR EXT_RAM_ATTR // IDF v4 / arduino-esp32 2.x
#else
#error "PC_TLS_ARENA_IN_PSRAM: no EXT_RAM_BSS_ATTR/EXT_RAM_ATTR from the framework (unexpected)."
#endif
#else
#define PC_TLS_ARENA_ATTR
#endif
// Arena allocator state, owned by one instance (internal linkage): the static arena backing
// every mbedTLS object plus the first-fit pool cursors. One named owner, unreachable cross-TU.
#if defined(PC_TLS_HS_BENCH)
#include <esp_timer.h> // esp_timer_get_time() - microsecond wall clock for the handshake span probe
#endif
typedef struct
{
    uint8_t arena[PC_TLS_ARENA_SIZE];
    proto_bool inited = PROTO_FALSE;
    size_t used = 0;
    size_t peak = 0;
} TlsPoolCtx;
PC_TLS_ARENA_ATTR static TlsPoolCtx s_pool;

#define TLS_ALIGN 8u
typedef struct
{
    size_t size;  // payload bytes
    uint8_t used; // 0 = free, 1 = in use
} PoolBlk;
static const size_t POOL_HDR = (sizeof(PoolBlk) + (TLS_ALIGN - 1)) & ~(size_t)(TLS_ALIGN - 1);

static void pool_init()
{
    PoolBlk *b = (PoolBlk *)s_pool.arena;
    b->size = sizeof(s_pool.arena) - POOL_HDR;
    b->used = 0;
    s_pool.used = 0;
    s_pool.peak = 0;
    s_pool.inited = PROTO_TRUE;
}

static void pool_coalesce()
{
    uint8_t *p = s_pool.arena;
    uint8_t *end = s_pool.arena + sizeof(s_pool.arena);
    while (p < end)
    {
        PoolBlk *b = (PoolBlk *)p;
        uint8_t *next = p + POOL_HDR + b->size;
        if (!b->used && next < end)
        {
            PoolBlk *nb = (PoolBlk *)next;
            if (!nb->used)
            {
                b->size += POOL_HDR + nb->size; // merge
                continue;                       // try merging further
            }
        }
        p = next;
    }
}

static void *pool_calloc(size_t n, size_t size)
{
    if (!s_pool.inited)
    {
        pool_init();
    }
    if (n != 0 && size > (size_t)-1 / n)
    {
        return NULL; // overflow
    }
    size_t want = n * size;
    want = (want + (TLS_ALIGN - 1)) & ~(size_t)(TLS_ALIGN - 1);
    if (want == 0)
    {
        want = TLS_ALIGN;
    }

    uint8_t *p = s_pool.arena;
    uint8_t *end = s_pool.arena + sizeof(s_pool.arena);
    while (p < end)
    {
        PoolBlk *b = (PoolBlk *)p;
        if (!b->used && b->size >= want)
        {
            // Split if the remainder can hold another header + a min payload.
            if (b->size >= want + POOL_HDR + TLS_ALIGN)
            {
                PoolBlk *nb = (PoolBlk *)(p + POOL_HDR + want);
                nb->size = b->size - want - POOL_HDR;
                nb->used = 0;
                b->size = want;
            }
            b->used = 1;
            s_pool.used += b->size;
            if (s_pool.used > s_pool.peak)
            {
                s_pool.peak = s_pool.used;
            }
            void *payload = p + POOL_HDR;
            memset(payload, 0, b->size);
            return payload;
        }
        p += POOL_HDR + b->size;
    }
    return NULL; // arena exhausted
}

static void pool_free(void *ptr)
{
    if (!ptr)
    {
        return;
    }
    PoolBlk *b = (PoolBlk *)((uint8_t *)ptr - POOL_HDR);
    if (b->used)
    {
        b->used = 0;
        if (s_pool.used >= b->size)
        {
            s_pool.used -= b->size;
        }
    }
    pool_coalesce();
}

// The server-config "ready" flag, owned separately from the multi-hundred-byte mbedTLS
// server state below. pc_tls_ready() (and the client-side guards) read this flag every
// call; grouping it into TlsServerCtx would anchor the whole server config/cert/key/ticket
// via that always-live reference, keeping ~600 bytes linked even in a client-only firmware
// that never runs pc_tls_configure(). Kept apart, --gc-sections drops s_srv when server
// setup is unused.
typedef struct
{
    proto_bool ready = PROTO_FALSE;
} TlsServerReadyCtx;
static TlsServerReadyCtx s_srv_ready;

// TLS server config, owned by one instance (internal linkage): the mbedTLS config/cert/key,
// the optional mTLS client-cert trust anchor, and the resumption ticket key. Referenced only
// by the server-setup path, so a client-only build garbage-collects it. One named owner.
typedef struct
{
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
#if PC_ENABLE_MTLS
    mbedtls_x509_crt ca; // client-cert trust anchor (mTLS)
#endif
#if PC_ENABLE_TLS_RESUMPTION
    mbedtls_ssl_ticket_context ticket_ctx; // server-held key for sealing session tickets
#endif
} TlsServerCtx;
static TlsServerCtx s_srv;

typedef struct
{
    mbedtls_ssl_context ssl;
    struct tcp_pcb *pcb; // captured at begin: the senders null conn->pcb mid-write
    uint8_t slot;
    proto_bool active;
    proto_bool established;
#ifdef PC_TLS_HS_BENCH
    long long hs_cpu_us;   // accumulated device CPU time INSIDE mbedtls_ssl_handshake across pumps
    long long hs_wall0_us; // esp_timer at the first handshake pump (wall clock incl. network waits)
    proto_bool hs_started; // timing began for this handshake
#endif
} TlsConn;
// TLS connection pool, owned by one instance (internal linkage): the per-slot mbedTLS session
// contexts. One named owner, unreachable from any other translation unit.
typedef struct
{
    TlsConn conns[MAX_TLS_CONNS];
} TlsConnsCtx;
static TlsConnsCtx s_conns;

#ifdef PC_TLS_HS_BENCH
// The one owned handshake-bench instance (struct declared in tls.h): the last completed handshake's
// device-CPU time (summed across the pumped mbedtls_ssl_handshake calls) and wall time. The rig prints it.
TlsHsBenchCtx pc_tls_hs_bench = {0, 0, 0};
#endif

static TlsConn *find(uint8_t slot)
{
    for (uint8_t i = 0; i < MAX_TLS_CONNS; i++)
    {
        if (s_conns.conns[i].active && s_conns.conns[i].slot == slot)
        {
            return &s_conns.conns[i];
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// RNG + transport BIO
// ---------------------------------------------------------------------------
static int tls_rng(void *ctx, unsigned char *out, size_t len)
{
    (void)ctx;
    pc_platform_rand_fill(out, len); // ESP32 hardware CSPRNG
    return 0;
}

// Server BIO recv (a pc_tls_bio_recv_fn): pull ciphertext from the connection's
// rx ring (filled by the lwIP recv callback). No bytes -> WANT_READ so mbedTLS
// yields to the loop. Reads only the ring, so it is safe from either context.
static int server_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    TlsConn *e = (TlsConn *)ctx;
    size_t n = pc_conn_read(e->slot, buf, len); // ciphertext from the rx ring
    if (n == 0)
    {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return (int)n;
}

// Server BIO send (a pc_tls_bio_send_fn): emit ciphertext through the transport's
// context-safe raw write (pc_conn_raw_send), so the handshake - pumped from the
// main loop - never does an unsynchronized tcp_write racing the lwIP thread, while
// app-data writes (already in the lwIP thread) still go out directly. Uses the pcb
// captured at begin() because the response senders null conn->pcb before writing.
static int server_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    TlsConn *e = (TlsConn *)ctx;
    if (!e->pcb)
    {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    size_t avail = tcp_sndbuf(e->pcb);
    if (avail == 0)
    {
        return MBEDTLS_ERR_SSL_WANT_WRITE; // backpressure; retry next pump
    }
    size_t to = len;
    if (to > avail)
    {
        to = avail;
    }
    if (to > 0xFFFF)
    {
        to = 0xFFFF;
    }

    if (pc_conn_raw_send(e->pcb, buf, (proto_u16)to))
    {
        return (int)to;
    }
    return MBEDTLS_ERR_SSL_WANT_WRITE; // send buffer full -> mbedTLS retries
}

// Apply the configured TLS Maximum Fragment Length (RFC 6066) to @p conf: records are
// capped at PC_TLS_MAX_FRAG_LEN. With a variable-buffer-length mbedTLS build this
// also shrinks per-connection arena use; otherwise it bounds the on-wire record size
// (bandwidth / latency on a constrained link) and honors a client's MFL request. A
// no-op when the knob is 0 or the mbedTLS build lacks MBEDTLS_SSL_MAX_FRAGMENT_LENGTH.
static void tls_apply_max_frag_len(mbedtls_ssl_config *conf)
{
#if PC_TLS_MAX_FRAG_LEN && defined(MBEDTLS_SSL_MAX_FRAGMENT_LENGTH)
#if PC_TLS_MAX_FRAG_LEN <= 512
    mbedtls_ssl_conf_max_frag_len(conf, MBEDTLS_SSL_MAX_FRAG_LEN_512);
#elif PC_TLS_MAX_FRAG_LEN <= 1024
    mbedtls_ssl_conf_max_frag_len(conf, MBEDTLS_SSL_MAX_FRAG_LEN_1024);
#elif PC_TLS_MAX_FRAG_LEN <= 2048
    mbedtls_ssl_conf_max_frag_len(conf, MBEDTLS_SSL_MAX_FRAG_LEN_2048);
#else
    mbedtls_ssl_conf_max_frag_len(conf, MBEDTLS_SSL_MAX_FRAG_LEN_4096);
#endif
#else
    (void)conf;
#endif
}

// Pin the ECDHE curve/group preference (RFC 8446 supported_groups / RFC 8422 for TLS 1.2).
// This is PERFORMANCE-CRITICAL: mbedTLS, given no explicit preference, negotiates the FIRST curve in
// its own default list that the client also offers, and on the esp-idf mbedTLS build that is secp521r1
// - the MOST expensive curve. The ECDHE variable-base scalar multiply is the dominant handshake op, and
// a P-521 one runs ~2.4x a P-256/x25519 one in software. Measured end-to-end on an ESP32-S3 (full TLS 1.2
// handshake, ECDHE-ECDSA-AES256-GCM): default(secp521r1) ~1000 ms -> a cheap 128-bit curve ~487 ms (2.05x).
//
// The ORDER of the two cheap curves is PER-VARIANT, because the ECC silicon differs wildly between dies
// (PC_TLS_ECDHE_PREFER_P256, which defaults to PC_HW_ECC):
//   - HW NIST-ECC (P4/C5/C6/...): secp256r1 leads - mbedTLS routes P-256 through the HW accelerator, so it
//     is far faster than x25519 (which stays software). Measured on an ESP32-P4: P-256 ECDHE ~10 ms vs
//     x25519 ~132 ms, full handshake ~29 ms vs ~160 ms (5.5x).
//   - no ECC HW (S3/S2/classic): x25519 leads - both curves are software and near-identical in the full
//     handshake, so the security-preferred modern default wins (the S3 order is unchanged).
// secp384r1/secp521r1 stay last (interop only). Every curve stays available - this only reorders PREFERENCE,
// so a client that supports just one still connects. Applied to the server and outbound-client configs.
static void tls_apply_curve_pref(mbedtls_ssl_config *conf)
{
#if MBEDTLS_VERSION_MAJOR >= 3
    static const uint16_t kGroupPref[] = {
#if PC_TLS_ECDHE_PREFER_P256 && defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
        MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1, // HW-accelerated NIST curve leads (PC_HW_ECC dies)
#endif
#if defined(MBEDTLS_ECP_DP_CURVE25519_ENABLED)
        MBEDTLS_SSL_IANA_TLS_GROUP_X25519,
#endif
#if !PC_TLS_ECDHE_PREFER_P256 && defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
        MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1, // software curves: x25519 (modern default) leads, P-256 second
#endif
#if defined(MBEDTLS_ECP_DP_SECP384R1_ENABLED)
        MBEDTLS_SSL_IANA_TLS_GROUP_SECP384R1,
#endif
#if defined(MBEDTLS_ECP_DP_SECP521R1_ENABLED)
        MBEDTLS_SSL_IANA_TLS_GROUP_SECP521R1,
#endif
        0,
    };
    mbedtls_ssl_conf_groups(conf, kGroupPref);
#else
    static const mbedtls_ecp_group_id kCurvePref[] = {
#if PC_TLS_ECDHE_PREFER_P256 && defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
        MBEDTLS_ECP_DP_SECP256R1, // HW-accelerated NIST curve leads (PC_HW_ECC dies)
#endif
#if defined(MBEDTLS_ECP_DP_CURVE25519_ENABLED)
        MBEDTLS_ECP_DP_CURVE25519,
#endif
#if !PC_TLS_ECDHE_PREFER_P256 && defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
        MBEDTLS_ECP_DP_SECP256R1, // software curves: x25519 (modern default) leads, P-256 second
#endif
#if defined(MBEDTLS_ECP_DP_SECP384R1_ENABLED)
        MBEDTLS_ECP_DP_SECP384R1,
#endif
#if defined(MBEDTLS_ECP_DP_SECP521R1_ENABLED)
        MBEDTLS_ECP_DP_SECP521R1,
#endif
        MBEDTLS_ECP_DP_NONE,
    };
    mbedtls_ssl_conf_curves(conf, kCurvePref);
#endif
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
proto_bool pc_tls_global_init(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len)
{
    if (s_srv_ready.ready)
    {
        return PROTO_TRUE;
    }
    if (!cert || !key)
    {
        return PROTO_FALSE;
    }

    // Route ALL mbedTLS allocations through our static arena before any mbedTLS
    // object is initialized.
    pool_init();
    mbedtls_platform_set_calloc_free(pool_calloc, pool_free);

    mbedtls_x509_crt_init(&s_srv.cert);
    mbedtls_pk_init(&s_srv.key);
    mbedtls_ssl_config_init(&s_srv.conf);

    if (mbedtls_x509_crt_parse(&s_srv.cert, cert, cert_len) != 0)
    {
        return PROTO_FALSE;
    }

#if MBEDTLS_VERSION_MAJOR >= 3
    if (mbedtls_pk_parse_key(&s_srv.key, key, key_len, NULL, 0, tls_rng, NULL) != 0)
    {
        return PROTO_FALSE;
    }
#else
    if (mbedtls_pk_parse_key(&s_srv.key, key, key_len, NULL, 0) != 0)
    {
        return PROTO_FALSE;
    }
#endif

    if (mbedtls_ssl_config_defaults(&s_srv.conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
    {
        return PROTO_FALSE;
    }

    mbedtls_ssl_conf_rng(&s_srv.conf, tls_rng, NULL);
    tls_apply_max_frag_len(&s_srv.conf); // RFC 6066 record cap (PC_TLS_MAX_FRAG_LEN)
    tls_apply_curve_pref(&s_srv.conf);   // prefer cheap curves (no ECC HW on the S3) - see helper
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_ssl_conf_min_tls_version(&s_srv.conf, MBEDTLS_SSL_VERSION_TLS1_2);
#else
    mbedtls_ssl_conf_min_version(&s_srv.conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
#endif

    if (mbedtls_ssl_conf_own_cert(&s_srv.conf, &s_srv.cert, &s_srv.key) != 0)
    {
        return PROTO_FALSE;
    }

#if PC_ENABLE_HTTP2
    // Offer HTTP/2 over TLS via ALPN (RFC 7301), falling back to HTTP/1.1. The list must outlive
    // the config, so it is static; pc_tls_alpn() reports the negotiated choice post-handshake.
    static const char *s_alpn[] = {"h2", "http/1.1", NULL}; // mbedTLS keeps this pointer
    if (mbedtls_ssl_conf_alpn_protocols(&s_srv.conf, s_alpn) != 0)
    {
        return PROTO_FALSE;
    }
#endif

#if PC_ENABLE_TLS_RESUMPTION
    // RFC 5077 session tickets: a returning client resumes with an abbreviated
    // handshake. Stateless (the session lives in the client's sealed ticket), so
    // no per-session cache grows in the arena. The ticket key rotates on the
    // configured lifetime. mbedtls_ssl_ticket_write/parse are the default codec.
    mbedtls_ssl_ticket_init(&s_srv.ticket_ctx);
    if (mbedtls_ssl_ticket_setup(&s_srv.ticket_ctx, tls_rng, NULL, MBEDTLS_CIPHER_AES_256_GCM,
                                 PC_TLS_TICKET_LIFETIME_S) != 0)
    {
        return PROTO_FALSE;
    }
    mbedtls_ssl_conf_session_tickets_cb(&s_srv.conf, mbedtls_ssl_ticket_write, mbedtls_ssl_ticket_parse,
                                        &s_srv.ticket_ctx);
#endif

    for (uint8_t i = 0; i < MAX_TLS_CONNS; i++)
    {
        s_conns.conns[i].active = PROTO_FALSE;
    }

    s_srv_ready.ready = PROTO_TRUE;
    return PROTO_TRUE;
}

proto_bool pc_tls_ready()
{
    return s_srv_ready.ready;
}

const char *pc_tls_alpn(uint8_t slot)
{
    TlsConn *c = find(slot);
    return c ? mbedtls_ssl_get_alpn_protocol(&c->ssl) : NULL;
}

proto_bool pc_tls_conn_begin(uint8_t slot)
{
    if (!s_srv_ready.ready)
    {
        return PROTO_FALSE;
    }
    TlsConn *e = NULL;
    for (uint8_t i = 0; i < MAX_TLS_CONNS; i++)
    {
        if (!s_conns.conns[i].active)
        {
            e = &s_conns.conns[i];
            break;
        }
    }
    if (!e)
    {
        return PROTO_FALSE; // TLS connection pool full
    }

    e->slot = slot;
    e->pcb = conn_pool[slot].pcb;
    e->active = PROTO_TRUE;
    e->established = PROTO_FALSE;
    mbedtls_ssl_init(&e->ssl);
    if (mbedtls_ssl_setup(&e->ssl, &s_srv.conf) != 0)
    {
        mbedtls_ssl_free(&e->ssl);
        e->active = PROTO_FALSE;
        return PROTO_FALSE;
    }
    mbedtls_ssl_set_bio(&e->ssl, e, server_bio_send, server_bio_recv, NULL);
    return PROTO_TRUE;
}

int pc_tls_handshake(uint8_t slot)
{
    TlsConn *e = find(slot);
    if (!e)
    {
        return -1;
    }
#ifdef PC_TLS_HS_BENCH
    if (!e->hs_started)
    {
        e->hs_started = PROTO_TRUE;
        e->hs_cpu_us = 0;
        e->hs_wall0_us = esp_timer_get_time();
        pc_tls_hs_bench.n_pumps = 0;
    }
    long long hs_t0 = esp_timer_get_time();
#endif
    int ret = mbedtls_ssl_handshake(&e->ssl);
#ifdef PC_TLS_HS_BENCH
    long long hs_d = esp_timer_get_time() - hs_t0; // device CPU in this pump; network waits are between pumps
    e->hs_cpu_us += hs_d;
    if (hs_d > 2000 && pc_tls_hs_bench.n_pumps < 8) // record the crypto-heavy flights, skip the idle pumps
    {
        pc_tls_hs_bench.pumps[pc_tls_hs_bench.n_pumps++] = hs_d;
    }
#endif
    if (ret == 0)
    {
        e->established = PROTO_TRUE;
#ifdef PC_TLS_HS_BENCH
        pc_tls_hs_bench.last_cpu_us = e->hs_cpu_us;
        pc_tls_hs_bench.last_wall_us = esp_timer_get_time() - e->hs_wall0_us;
        pc_tls_hs_bench.count++;
        e->hs_started = PROTO_FALSE;
#endif
        return 1;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        return 0;
    }
    return -1;
}

proto_bool pc_tls_established(uint8_t slot)
{
    TlsConn *e = find(slot);
    return e && e->established;
}

int pc_tls_read(uint8_t slot, uint8_t *buf, size_t len)
{
    TlsConn *e = find(slot);
    if (!e)
    {
        return -1;
    }
    int ret = mbedtls_ssl_read(&e->ssl, buf, len);
    if (ret > 0)
    {
        return ret;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        return 0; // no plaintext available yet
    }
    return -1; // close_notify, peer close, or fatal
}

int pc_tls_write(uint8_t slot, const void *data, size_t len)
{
    TlsConn *e = find(slot);
    if (!e)
    {
        return -1;
    }
    const unsigned char *p = (const unsigned char *)data;
    size_t sent = 0;
    uint16_t guard = 0; // bound retries so a stuck send buffer cannot spin forever
    while (sent < len)
    {
        int ret = mbedtls_ssl_write(&e->ssl, p + sent, len - sent);
        if (ret > 0)
        {
            sent += (size_t)ret;
            guard = 0;
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            if (++guard > 64)
            {
                break; // give up this flush; backpressure
            }
            continue;
        }
        return -1;
    }
    return (int)sent;
}

void pc_tls_conn_end(uint8_t slot)
{
    TlsConn *e = find(slot);
    if (!e)
    {
        return;
    }
    mbedtls_ssl_close_notify(&e->ssl);
    mbedtls_ssl_free(&e->ssl);
    e->active = PROTO_FALSE;
    e->established = PROTO_FALSE;
    e->pcb = NULL;
}

void pc_tls_conn_free(uint8_t slot)
{
    TlsConn *e = find(slot);
    if (!e)
    {
        return;
    }
    mbedtls_ssl_free(&e->ssl); // abrupt teardown: no close_notify (peer is gone)
    e->active = PROTO_FALSE;
    e->established = PROTO_FALSE;
    e->pcb = NULL;
}

size_t pc_tls_arena_peak()
{
    return s_pool.peak;
}

#if PC_ENABLE_MTLS
proto_bool pc_tls_set_client_ca(const uint8_t *ca, size_t ca_len)
{
    if (!s_srv_ready.ready || !ca)
    {
        return PROTO_FALSE;
    }
    mbedtls_x509_crt_init(&s_srv.ca);
    if (mbedtls_x509_crt_parse(&s_srv.ca, ca, ca_len) != 0)
    {
        return PROTO_FALSE;
    }
    // Trust anchor for the client chain + demand a (valid) client cert: an absent
    // or untrusted client certificate now fails the handshake.
    mbedtls_ssl_conf_ca_chain(&s_srv.conf, &s_srv.ca, NULL);
    mbedtls_ssl_conf_authmode(&s_srv.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    return PROTO_TRUE;
}

int pc_tls_peer_subject(uint8_t slot, char *out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return -1;
    }
    out[0] = '\0';
    TlsConn *e = find(slot);
    if (!e || !e->established)
    {
        return -1;
    }
    const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&e->ssl);
    if (!peer)
    {
        return -1;
    }
    int n = mbedtls_x509_dn_gets(out, out_len, &peer->subject);
    return n; // bytes written (excl. NUL), or <0 on error
}
#endif // PC_ENABLE_MTLS

#if PC_ENABLE_CLIENT_TLS
// Optional client-side server authentication (default off = encrypt-only):
//  - a CA trust anchor -> mbedTLS verifies the chain + hostname during handshake;
//  - a 32-byte SHA-256 cert pin -> the peer's certificate DER is hashed and
//    constant-time compared after the handshake.
// Either, both, or neither may be set; both must pass when both are set. Shared by
// the one-shot HTTP client (pc_tls_client_run) and the persistent session (csess).
// Client-side server-authentication config, owned by one instance (internal linkage): the CA
// trust anchor (+set flag) and the SHA-256 cert pin (+set flag). Shared by the one-shot HTTP
// client and the persistent session. One named owner, unreachable from any other TU.
typedef struct
{
    mbedtls_x509_crt ca;
    proto_bool ca_set = PROTO_FALSE;
    uint8_t pin[32];
    proto_bool pin_set = PROTO_FALSE;
} TlsClientAuthCtx;
static TlsClientAuthCtx s_cli;

// Route mbedTLS allocations through the static arena (the client may run before
// any server-side TLS init has installed the allocator).
static void client_arena_ensure()
{
    if (!s_pool.inited)
    {
        pool_init();
        mbedtls_platform_set_calloc_free(pool_calloc, pool_free);
    }
}

void pc_tls_client_set_ca(const uint8_t *ca, size_t ca_len)
{
    client_arena_ensure();
    if (s_cli.ca_set)
    {
        mbedtls_x509_crt_free(&s_cli.ca);
    }
    s_cli.ca_set = PROTO_FALSE;
    if (!ca || ca_len == 0)
    {
        return;
    }
    mbedtls_x509_crt_init(&s_cli.ca);
    s_cli.ca_set = (mbedtls_x509_crt_parse(&s_cli.ca, ca, ca_len) == 0);
    if (!s_cli.ca_set)
    {
        mbedtls_x509_crt_free(&s_cli.ca);
    }
}

void pc_tls_client_set_pin(const uint8_t sha256[32])
{
    if (!sha256)
    {
        s_cli.pin_set = PROTO_FALSE;
        return;
    }
    memcpy(s_cli.pin, sha256, 32);
    s_cli.pin_set = PROTO_TRUE;
}

void pc_tls_client_clear_verify()
{
    if (s_cli.ca_set)
    {
        mbedtls_x509_crt_free(&s_cli.ca);
    }
    s_cli.ca_set = PROTO_FALSE;
    s_cli.pin_set = PROTO_FALSE;
}

// Constant-time 32-byte compare (no early-out on the first differing byte).
static proto_bool ct_eq32(const uint8_t *a, const uint8_t *b)
{
    uint8_t d = 0;
    for (int i = 0; i < 32; i++)
    {
        d |= (uint8_t)(a[i] ^ b[i]);
    }
    return d == 0;
}

// Apply the shared client config: RNG, server-auth mode (CA verify or
// encrypt-only), TLS >= 1.2, and the CA chain when one is installed.
static int client_conf_apply(mbedtls_ssl_config *conf)
{
    if (mbedtls_ssl_config_defaults(conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
    {
        return -1;
    }
    mbedtls_ssl_conf_rng(conf, tls_rng, NULL);
    tls_apply_max_frag_len(conf); // RFC 6066 record cap (PC_TLS_MAX_FRAG_LEN)
    tls_apply_curve_pref(conf);   // offer cheap curves first (no ECC HW on the S3) - see helper
    if (s_cli.ca_set)
    {
        mbedtls_ssl_conf_ca_chain(conf, &s_cli.ca, NULL);
        mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    }
    else
    {
        mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE); // encrypt-only (no trust store)
    }
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_ssl_conf_min_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_2);
#else
    mbedtls_ssl_conf_min_version(conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
#endif
#if PC_ENABLE_TLS_RESUMPTION
    // Accept a server-issued session ticket (RFC 5077) so the client can resume an
    // abbreviated handshake on reconnect (see the csess save/restore below).
    mbedtls_ssl_conf_session_tickets(conf, MBEDTLS_SSL_SESSION_TICKETS_ENABLED);
#endif
    return 0;
}

// Post-handshake certificate pin check: true if no pin is set, or the peer's
// certificate DER hashes to the installed pin (constant-time compared).
static proto_bool client_pin_ok(mbedtls_ssl_context *ssl)
{
    if (!s_cli.pin_set)
    {
        return PROTO_TRUE;
    }
    const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(ssl);
    if (!peer)
    {
        return PROTO_FALSE;
    }
    uint8_t hash[32];
#if MBEDTLS_VERSION_MAJOR >= 3
    int hret = mbedtls_sha256(peer->raw.p, peer->raw.len, hash, 0);
#else
    int hret = mbedtls_sha256_ret(peer->raw.p, peer->raw.len, hash, 0);
#endif
    return (hret == 0) && ct_eq32(hash, s_cli.pin);
}

#if PC_ENABLE_HTTP_CLIENT_TLS
// Blocking client-side TLS exchange over caller-supplied BIO callbacks. Used by
// the outbound HTTP client for https://. The transport (raw lwIP tcp_write + the
// receive ring) lives in http_client.cpp; here we own only the mbedTLS client
// session, served from the same static arena as the server side.
//
// Server authentication is OFF by default (the device has no trust store): the
// transport is encrypted but the peer is unauthenticated unless a CA
// (pc_tls_client_set_ca) and/or a cert pin (pc_tls_client_set_pin) is installed.
int pc_tls_client_run(const char *host, const uint8_t *req, size_t reqlen, uint8_t *out, size_t out_cap,
                      size_t *out_len, pc_tls_bio_send_fn send_fn, pc_tls_bio_recv_fn recv_fn, uint32_t deadline_ms)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!req || !out || out_cap == 0 || !send_fn || !recv_fn)
    {
        return -1;
    }

    client_arena_ensure();

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);

    int rc = -1;
    int ret;
    do
    {
        if (client_conf_apply(&conf) != 0)
        {
            break;
        }
        if (mbedtls_ssl_setup(&ssl, &conf) != 0)
        {
            break;
        }
        if (host)
        {
            mbedtls_ssl_set_hostname(&ssl, host); // SNI (ignored if unsupported)
        }
        mbedtls_ssl_set_bio(&ssl, NULL, send_fn, recv_fn, NULL);

        // Handshake (BIO callbacks yield WANT_READ/WANT_WRITE while data is pending).
        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0)
        {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                break;
            }
            if ((int32_t)(deadline_ms - millis()) <= 0)
            {
                ret = -1;
                break;
            }
            pcdelay(5);
        }
        if (ret != 0)
        {
#ifdef PC_HTTP_CLIENT_DEBUG
            printf("[tls] handshake ret=-0x%04x arena_peak=%u\n", (unsigned)(-ret), (unsigned)pc_tls_arena_peak());
#endif
            break;
        }

        // Certificate pinning (mismatch or no peer cert aborts).
        if (!client_pin_ok(&ssl))
        {
#ifdef PC_HTTP_CLIENT_DEBUG
            printf("[tls] cert pin mismatch\n");
#endif
            ret = -1;
            break;
        }

        // Send the (plaintext) request; mbedTLS encrypts and pushes via send_fn.
        size_t sent = 0;
        while (sent < reqlen)
        {
            ret = mbedtls_ssl_write(&ssl, req + sent, reqlen - sent);
            if (ret > 0)
            {
                sent += (size_t)ret;
                continue;
            }
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                break;
            }
            if ((int32_t)(deadline_ms - millis()) <= 0)
            {
                break;
            }
            pcdelay(5);
        }
        if (sent < reqlen)
        {
            break;
        }

        // Read the decrypted response into out until the peer closes / buffer fills.
        size_t total = 0;
        while (total < out_cap)
        {
            ret = mbedtls_ssl_read(&ssl, out + total, out_cap - total);
            if (ret > 0)
            {
                total += (size_t)ret;
                continue;
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                if ((int32_t)(deadline_ms - millis()) <= 0)
                {
                    break;
                }
                pcdelay(5);
                continue;
            }
            break; // 0 / PEER_CLOSE_NOTIFY / fatal -> response complete
        }
        if (out_len)
        {
            *out_len = total;
        }
        rc = (total > 0) ? 0 : -1;
    } while (0);

    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    return rc;
}
#endif // PC_ENABLE_HTTP_CLIENT_TLS

// --- Persistent client session (csess): one long-lived outbound TLS connection
// (e.g. MQTTS). Handshake once, then read/write application data over the
// caller's BIO until pc_tls_client_session_end(). Honors the CA/pin trust config above. ---
// Persistent client session (csess) state, owned by one instance (internal linkage): the
// long-lived outbound TLS ssl/config + active flag, and (with resumption) the saved session
// holding the server's ticket. One named owner, unreachable from any other translation unit.
typedef struct
{
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    proto_bool active = PROTO_FALSE;
#if PC_ENABLE_TLS_RESUMPTION
    mbedtls_ssl_session saved;
    proto_bool saved_valid = PROTO_FALSE;
#endif
} TlsCsessCtx;
static TlsCsessCtx s_csess;

#if PC_ENABLE_TLS_RESUMPTION
// Session saved from the last successful csess handshake (holds the server's
// ticket). Presented on the next begin() for an abbreviated handshake. Lives in
// the static arena like every other mbedTLS object - no heap growth.

void pc_tls_client_session_forget_session()
{
    if (s_csess.saved_valid)
    {
        mbedtls_ssl_session_free(&s_csess.saved);
    }
    s_csess.saved_valid = PROTO_FALSE;
}
#else
void pc_tls_client_session_forget_session()
{
}
#endif

proto_bool pc_tls_client_session_begin(const char *host, pc_tls_bio_send_fn send_fn, pc_tls_bio_recv_fn recv_fn)
{
    if (!send_fn || !recv_fn)
    {
        return PROTO_FALSE;
    }
    if (s_csess.active)
    {
        pc_tls_client_session_end();
    }
    client_arena_ensure();
    mbedtls_ssl_init(&s_csess.ssl);
    mbedtls_ssl_config_init(&s_csess.conf);
    if (client_conf_apply(&s_csess.conf) != 0 || mbedtls_ssl_setup(&s_csess.ssl, &s_csess.conf) != 0)
    {
        mbedtls_ssl_free(&s_csess.ssl);
        mbedtls_ssl_config_free(&s_csess.conf);
        return PROTO_FALSE;
    }
    if (host)
    {
        mbedtls_ssl_set_hostname(&s_csess.ssl, host);
    }
#if PC_ENABLE_TLS_RESUMPTION
    // Present the saved session (server ticket) so this handshake resumes if the
    // server still honors it; a full handshake transparently replaces it below.
    if (s_csess.saved_valid)
    {
        mbedtls_ssl_set_session(&s_csess.ssl, &s_csess.saved);
    }
#endif
    mbedtls_ssl_set_bio(&s_csess.ssl, NULL, send_fn, recv_fn, NULL);
    s_csess.active = PROTO_TRUE;
    return PROTO_TRUE;
}

proto_bool pc_tls_client_session_active()
{
    return s_csess.active;
}

int pc_tls_client_session_handshake()
{
    if (!s_csess.active)
    {
        return -1;
    }
    int ret = mbedtls_ssl_handshake(&s_csess.ssl);
    if (ret == 0)
    {
        if (!client_pin_ok(&s_csess.ssl)) // verify the pin once established
        {
            return -1;
        }
#if PC_ENABLE_TLS_RESUMPTION
        // Capture the established session (incl. any new ticket) for next time.
        pc_tls_client_session_forget_session();
        mbedtls_ssl_session_init(&s_csess.saved);
        s_csess.saved_valid = (mbedtls_ssl_get_session(&s_csess.ssl, &s_csess.saved) == 0);
#endif
        return 1;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        return 0;
    }
    return -1;
}

int pc_tls_client_session_read(uint8_t *buf, size_t len)
{
    if (!s_csess.active)
    {
        return -1;
    }
    int ret = mbedtls_ssl_read(&s_csess.ssl, buf, len);
    if (ret > 0)
    {
        return ret;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        return 0; // no plaintext available yet
    }
    return -1; // close_notify / peer close / fatal
}

int pc_tls_client_session_write(const uint8_t *data, size_t len)
{
    if (!s_csess.active)
    {
        return -1;
    }
    size_t sent = 0;
    uint16_t guard = 0;
    while (sent < len)
    {
        int ret = mbedtls_ssl_write(&s_csess.ssl, data + sent, len - sent);
        if (ret > 0)
        {
            sent += (size_t)ret;
            guard = 0;
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            if (++guard > 64)
            {
                break; // send buffer stuck; report a short write
            }
            pcdelay(2);
            continue;
        }
        return -1;
    }
    return (int)sent;
}

void pc_tls_client_session_end()
{
    if (!s_csess.active)
    {
        return;
    }
    mbedtls_ssl_close_notify(&s_csess.ssl);
    mbedtls_ssl_free(&s_csess.ssl);
    mbedtls_ssl_config_free(&s_csess.conf);
    s_csess.active = PROTO_FALSE;
}

#endif // PC_ENABLE_CLIENT_TLS

#endif // PC_ENABLE_TLS && PROTOCORE_HOT
