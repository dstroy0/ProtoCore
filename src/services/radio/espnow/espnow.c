// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file espnow.c
 * @brief ESP-NOW envelope codec + peer registry (pure) and esp_now binding (ESP32).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_ESPNOW

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "services/radio/espnow/espnow.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_HAS_VENDOR_WIFI
#include <esp_idf_version.h> // ESP_IDF_VERSION / ESP_IDF_VERSION_VAL for the recv-cb ABI guard
#include <esp_now.h>
#include <esp_wifi.h>
#endif
const uint8_t PROTOCORE_ESPNOW_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---------------------------------------------------------------------------
// Envelope codec
// ---------------------------------------------------------------------------
// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_ESPNOW_BORROW persistent bytes
} EspnowOwnCtx;
static EspnowOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_espnow_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_ESPNOW_BORROW).buf;
    }
    return s_own.span;
}

void protocore_espnow_encode(uint8_t *restrict work);
void protocore_espnow_peer_add(uint8_t *restrict work);
void protocore_espnow_peers_reset(uint8_t *restrict work);
void protocore_espnow_send(uint8_t *restrict work);

void protocore_espnow_encode(uint8_t *restrict work)
{
    (void)work;
    uint8_t type = EspnowV.encode_args.type;
    const uint8_t *payload = EspnowV.encode_args.payload;
    size_t len = EspnowV.encode_args.len;
    uint8_t *out = EspnowV.encode_args.out;
    size_t cap = EspnowV.encode_args.cap;

    if (!out || len > PROTOCORE_ESPNOW_MAX_PAYLOAD || cap < len + PROTOCORE_ESPNOW_HDR)
    {
        EspnowV.n = 0;
        return;
    }
    out[0] = PROTOCORE_ESPNOW_MAGIC;
    out[1] = type;
    out[2] = (uint8_t)len;
    if (len && payload)
    {
        mem.cpy(out + PROTOCORE_ESPNOW_HDR, payload, len);
    }
    EspnowV.n = len + PROTOCORE_ESPNOW_HDR;
}

void protocore_espnow_decode(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = EspnowV.decode_args.buf;
    size_t len = EspnowV.decode_args.len;
    uint8_t *type = EspnowV.decode_args.type;
    const uint8_t **payload = EspnowV.decode_args.payload;
    size_t *plen = EspnowV.decode_args.plen;

    if (!buf || len < PROTOCORE_ESPNOW_HDR || buf[0] != PROTOCORE_ESPNOW_MAGIC)
    {
        EspnowV.ok = PROTO_FALSE;
        return;
    }
    size_t declared = buf[2];
    if (declared + PROTOCORE_ESPNOW_HDR != len) // length must match exactly (no trailing/short)
    {
        EspnowV.ok = PROTO_FALSE;
        return;
    }
    if (type)
    {
        *type = buf[1];
    }
    if (payload)
    {
        *payload = buf + PROTOCORE_ESPNOW_HDR;
    }
    if (plen)
    {
        *plen = declared;
    }
    EspnowV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Peer registry
// ---------------------------------------------------------------------------
typedef struct
{
    uint8_t mac[6];
    proto_bool used;
} Peer;

// All ESP-NOW runtime state, owned by one instance (internal linkage): the peer registry
// (host + radio) plus the radio binding's recv callback and channel, grouped so it is one
// named owner, unreachable from any other translation unit.
typedef struct
{
    Peer peers[PROTOCORE_ESPNOW_MAX_PEERS];
#if PROTOCORE_HAS_VENDOR_WIFI
    protocore_espnow_recv_fn recv;
    uint8_t channel;
#endif
} EspnowCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define ESPNOW_OFF_CTX 0u
static_assert(ESPNOW_OFF_CTX + sizeof(EspnowCtx) <= PROTOCORE_ESPNOW_BORROW,
              "PROTOCORE_ESPNOW_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(ESPNOW_OFF_CTX % _Alignof(EspnowCtx) == 0,
              "ESPNOW_OFF_CTX is not a multiple of alignof(EspnowCtx) - ESPNOW_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define ESPNOW_CTX(w) ((EspnowCtx *)(void *)((w) + ESPNOW_OFF_CTX))

static int peer_find(const EspnowCtx *c, const uint8_t mac[6])
{
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        if (c->peers[i].used && mem.cmp(c->peers[i].mac, mac, 6) == 0)
        {
            return i;
        }
    }
    return -1;
}

void protocore_espnow_peers_reset(uint8_t *restrict work)
{
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        ESPNOW_CTX(work)->peers[i].used = PROTO_FALSE;
    }
}

void protocore_espnow_peer_add(uint8_t *restrict work)
{
    const uint8_t *mac = EspnowV.peer_add_args.mac;

    if (!mac)
    {
        EspnowV.ok = PROTO_FALSE;
        return;
    }
    if (peer_find(ESPNOW_CTX(work), mac) >= 0)
    {
        EspnowV.ok = PROTO_TRUE; // idempotent
        return;
    }
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        if (!ESPNOW_CTX(work)->peers[i].used)
        {
            mem.cpy(ESPNOW_CTX(work)->peers[i].mac, mac, 6);
            ESPNOW_CTX(work)->peers[i].used = PROTO_TRUE;
            EspnowV.ok = PROTO_TRUE;
            return;
        }
    }
    EspnowV.ok = PROTO_FALSE; // table full
}

void protocore_espnow_peer_has(uint8_t *restrict work)
{
    const uint8_t *mac = EspnowV.peer_has_args.mac;

    EspnowV.ok = mac && peer_find(ESPNOW_CTX(work), mac) >= 0;
}

void protocore_espnow_peer_remove(uint8_t *restrict work)
{
    const uint8_t *mac = EspnowV.peer_remove_args.mac;

    int i = mac ? peer_find(ESPNOW_CTX(work), mac) : -1;
    if (i < 0)
    {
        EspnowV.ok = PROTO_FALSE;
        return;
    }
    ESPNOW_CTX(work)->peers[i].used = PROTO_FALSE;
    EspnowV.ok = PROTO_TRUE;
}

void protocore_espnow_peer_count(uint8_t *restrict work)
{
    int n = 0;
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        if (ESPNOW_CTX(work)->peers[i].used)
        {
            n++;
        }
    }
    EspnowV.n = n;
}

// ---------------------------------------------------------------------------
// ESP32 radio binding
// ---------------------------------------------------------------------------
#if PROTOCORE_HAS_VENDOR_WIFI

// The ESP-NOW receive callback signature changed in ESP-IDF 5.0 (Arduino-ESP32 3.x): the
// source MAC moved into an esp_now_recv_info_t. Match whichever the compiled core expects.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    const uint8_t *mac = info != NULL ? info->src_addr : NULL;
#else
static void on_recv(const uint8_t *mac, const uint8_t *data, int len)
{
#endif
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_espnow_span();

    if (!ESPNOW_CTX(work)->recv || len < 0 || !mac)
    {
        return;
    }
    uint8_t type;
    const uint8_t *payload;
    size_t plen;
    EspnowV.decode_args.buf = data;
    EspnowV.decode_args.len = (size_t)len;
    EspnowV.decode_args.type = &type;
    EspnowV.decode_args.payload = &payload;
    EspnowV.decode_args.plen = &plen;
    Espnow.decode(work);
    if (EspnowV.ok)
    {
        ESPNOW_CTX(work)->recv(mac, type, payload, plen);
    }
}

static proto_bool radio_add_peer(const uint8_t mac[6], uint8_t channel)
{
    esp_now_peer_info_t p;
    mem.set(&p, 0, sizeof(p));
    mem.cpy(p.peer_addr, mac, 6);
    p.channel = channel;
    p.encrypt = PROTO_FALSE;
    if (esp_now_is_peer_exist(mac))
    {
        return PROTO_TRUE;
    }
    return esp_now_add_peer(&p) == ESP_OK;
}

void protocore_espnow_begin(uint8_t *restrict work)
{
    nel = EspnowV.begin_args.channel;
    protocore_espnow_recv_fn cb = EspnowV.begin_args.cb;

    ESPNOW_CTX(work)->channel = channel;
    ESPNOW_CTX(work)->recv = cb;
    if (esp_now_init() != ESP_OK)
    {
        EspnowV.ok = PROTO_FALSE;
        return;
    }
    esp_now_register_recv_cb(on_recv);
    protocore_espnow_peers_reset(work);
    EspnowV.ok = radio_add_peer(PROTOCORE_ESPNOW_BROADCAST, channel); // broadcast is always a peer
}

void protocore_espnow_add_peer(uint8_t *restrict work)
{
    const uint8_t *mac = EspnowV.add_peer_args.mac;

    EspnowV.peer_add_args.mac = mac;
    protocore_espnow_peer_add(work);
    if (!EspnowV.ok)
    {
        EspnowV.ok = PROTO_FALSE;
        return;
    }
    EspnowV.ok = radio_add_peer(mac, ESPNOW_CTX(work)->channel);
}

void protocore_espnow_send(uint8_t *restrict work)
{
    const uint8_t *mac = EspnowV.send_args.mac;
    uint8_t type = EspnowV.send_args.type;
    const uint8_t *payload = EspnowV.send_args.payload;
    size_t len = EspnowV.send_args.len;

    uint8_t frame[PROTOCORE_ESPNOW_HDR + PROTOCORE_ESPNOW_MAX_PAYLOAD];
    EspnowV.encode_args.type = type;
    EspnowV.encode_args.payload = payload;
    EspnowV.encode_args.len = len;
    EspnowV.encode_args.out = frame;
    EspnowV.encode_args.cap = sizeof(frame);
    protocore_espnow_encode(work);
    size_t n = EspnowV.n;
    if (n == 0)
    {
        EspnowV.ok = PROTO_FALSE;
        return;
    }
    EspnowV.ok = esp_now_send(mac, frame, n) == ESP_OK;
}

void protocore_espnow_broadcast(uint8_t *restrict work)
{
    uint8_t type = EspnowV.broadcast_args.type;
    const uint8_t *payload = EspnowV.broadcast_args.payload;
    size_t len = EspnowV.broadcast_args.len;

    EspnowV.send_args.mac = PROTOCORE_ESPNOW_BROADCAST;
    EspnowV.send_args.type = type;
    EspnowV.send_args.payload = payload;
    EspnowV.send_args.len = len;
    protocore_espnow_send(work);
}

#else // host build - no radio

void protocore_espnow_begin(uint8_t *restrict work)
{
    (void)work;
    uint8_t channel = EspnowV.begin_args.channel;
    protocore_espnow_recv_fn cb = EspnowV.begin_args.cb;

    (void)channel;
    (void)cb;
    EspnowV.ok = PROTO_FALSE;
}
void protocore_espnow_add_peer(uint8_t *restrict work)
{
    const uint8_t *mac = EspnowV.add_peer_args.mac;

    EspnowV.peer_add_args.mac = mac;
    protocore_espnow_peer_add(work);
}
void protocore_espnow_send(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *mac = EspnowV.send_args.mac;
    uint8_t type = EspnowV.send_args.type;
    const uint8_t *payload = EspnowV.send_args.payload;
    size_t len = EspnowV.send_args.len;

    (void)mac;
    (void)type;
    (void)payload;
    (void)len;
    EspnowV.ok = PROTO_FALSE;
}
void protocore_espnow_broadcast(uint8_t *restrict work)
{
    (void)work;
    uint8_t type = EspnowV.broadcast_args.type;
    const uint8_t *payload = EspnowV.broadcast_args.payload;
    size_t len = EspnowV.broadcast_args.len;

    (void)type;
    (void)payload;
    (void)len;
    EspnowV.ok = PROTO_FALSE;
}

#endif // PROTOCORE_HAS_VENDOR_WIFI

/** @brief The operands and the outcome. */
EspnowVars EspnowV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ESPNOW
