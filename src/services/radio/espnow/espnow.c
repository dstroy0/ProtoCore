// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file espnow.c
 * @brief ESP-NOW envelope codec + peer registry (pure) and esp_now binding (ESP32).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_ESPNOW

#include "mmgr/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem.h"
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
    uint8_t *span; ///< PROTOCORE_ESPNOW_BORROW persistent bytes, or null while the pool was short
} EspnowOwnCtx;
static EspnowOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_espnow_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_ESPNOW_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void espnow_encode(uint8_t *restrict work);
static void espnow_peer_add(uint8_t *restrict work);
static void espnow_peers_reset(uint8_t *restrict work);
static void espnow_send(uint8_t *restrict work);

static void espnow_encode(uint8_t *restrict work)
{
    (void)work;
    uint8_t type = Espnow.encode_args.type;
    const uint8_t *payload = Espnow.encode_args.payload;
    size_t len = Espnow.encode_args.len;
    uint8_t *out = Espnow.encode_args.out;
    size_t cap = Espnow.encode_args.cap;

    if (!out || len > PROTOCORE_ESPNOW_MAX_PAYLOAD || cap < len + PROTOCORE_ESPNOW_HDR)
    {
        Espnow.n = 0;
        return;
    }
    out[0] = PROTOCORE_ESPNOW_MAGIC;
    out[1] = type;
    out[2] = (uint8_t)len;
    if (len && payload)
    {
        mem.cpy(out + PROTOCORE_ESPNOW_HDR, payload, len);
    }
    Espnow.n = len + PROTOCORE_ESPNOW_HDR;
}

static void espnow_decode(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Espnow.decode_args.buf;
    size_t len = Espnow.decode_args.len;
    uint8_t *type = Espnow.decode_args.type;
    const uint8_t **payload = Espnow.decode_args.payload;
    size_t *plen = Espnow.decode_args.plen;

    if (!buf || len < PROTOCORE_ESPNOW_HDR || buf[0] != PROTOCORE_ESPNOW_MAGIC)
    {
        Espnow.ok = PROTO_FALSE;
        return;
    }
    size_t declared = buf[2];
    if (declared + PROTOCORE_ESPNOW_HDR != len) // length must match exactly (no trailing/short)
    {
        Espnow.ok = PROTO_FALSE;
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
    Espnow.ok = PROTO_TRUE;
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

static void espnow_peers_reset(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }

    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        ESPNOW_CTX(work)->peers[i].used = PROTO_FALSE;
    }
}

static void espnow_peer_add(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const uint8_t *mac = Espnow.peer_add_args.mac;

    if (!mac)
    {
        Espnow.ok = PROTO_FALSE;
        return;
    }
    if (peer_find(ESPNOW_CTX(work), mac) >= 0)
    {
        Espnow.ok = PROTO_TRUE; // idempotent
        return;
    }
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        if (!ESPNOW_CTX(work)->peers[i].used)
        {
            mem.cpy(ESPNOW_CTX(work)->peers[i].mac, mac, 6);
            ESPNOW_CTX(work)->peers[i].used = PROTO_TRUE;
            Espnow.ok = PROTO_TRUE;
            return;
        }
    }
    Espnow.ok = PROTO_FALSE; // table full
}

static void espnow_peer_has(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const uint8_t *mac = Espnow.peer_has_args.mac;

    Espnow.ok = mac && peer_find(ESPNOW_CTX(work), mac) >= 0;
}

static void espnow_peer_remove(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const uint8_t *mac = Espnow.peer_remove_args.mac;

    int i = mac ? peer_find(ESPNOW_CTX(work), mac) : -1;
    if (i < 0)
    {
        Espnow.ok = PROTO_FALSE;
        return;
    }
    ESPNOW_CTX(work)->peers[i].used = PROTO_FALSE;
    Espnow.ok = PROTO_TRUE;
}

static void espnow_peer_count(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }

    int n = 0;
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        if (ESPNOW_CTX(work)->peers[i].used)
        {
            n++;
        }
    }
    Espnow.n = n;
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
    if (work == NULL)
    {
        return;
    }

    if (!ESPNOW_CTX(work)->recv || len < 0 || !mac)
    {
        return;
    }
    uint8_t type;
    const uint8_t *payload;
    size_t plen;
    Espnow.decode_args.buf = data;
    Espnow.decode_args.len = (size_t)len;
    Espnow.decode_args.type = &type;
    Espnow.decode_args.payload = &payload;
    Espnow.decode_args.plen = &plen;
    Espnow.decode(work);
    if (Espnow.ok)
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

static void espnow_begin(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    nel = Espnow.begin_args.channel;
    protocore_espnow_recv_fn cb = Espnow.begin_args.cb;

    ESPNOW_CTX(work)->channel = channel;
    ESPNOW_CTX(work)->recv = cb;
    if (esp_now_init() != ESP_OK)
    {
        Espnow.ok = PROTO_FALSE;
        return;
    }
    esp_now_register_recv_cb(on_recv);
    espnow_peers_reset(work);
    Espnow.ok = radio_add_peer(PROTOCORE_ESPNOW_BROADCAST, channel); // broadcast is always a peer
}

static void espnow_add_peer(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const uint8_t *mac = Espnow.add_peer_args.mac;

    Espnow.peer_add_args.mac = mac;
    espnow_peer_add(work);
    if (!Espnow.ok)
    {
        Espnow.ok = PROTO_FALSE;
        return;
    }
    Espnow.ok = radio_add_peer(mac, ESPNOW_CTX(work)->channel);
}

static void espnow_send(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const uint8_t *mac = Espnow.send_args.mac;
    uint8_t type = Espnow.send_args.type;
    const uint8_t *payload = Espnow.send_args.payload;
    size_t len = Espnow.send_args.len;

    uint8_t frame[PROTOCORE_ESPNOW_HDR + PROTOCORE_ESPNOW_MAX_PAYLOAD];
    Espnow.encode_args.type = type;
    Espnow.encode_args.payload = payload;
    Espnow.encode_args.len = len;
    Espnow.encode_args.out = frame;
    Espnow.encode_args.cap = sizeof(frame);
    espnow_encode(work);
    size_t n = Espnow.n;
    if (n == 0)
    {
        Espnow.ok = PROTO_FALSE;
        return;
    }
    Espnow.ok = esp_now_send(mac, frame, n) == ESP_OK;
}

static void espnow_broadcast(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t type = Espnow.broadcast_args.type;
    const uint8_t *payload = Espnow.broadcast_args.payload;
    size_t len = Espnow.broadcast_args.len;

    Espnow.send_args.mac = PROTOCORE_ESPNOW_BROADCAST;
    Espnow.send_args.type = type;
    Espnow.send_args.payload = payload;
    Espnow.send_args.len = len;
    espnow_send(work);
}

#else // host build - no radio

static void espnow_begin(uint8_t *restrict work)
{
    (void)work;
    uint8_t channel = Espnow.begin_args.channel;
    protocore_espnow_recv_fn cb = Espnow.begin_args.cb;

    (void)channel;
    (void)cb;
    Espnow.ok = PROTO_FALSE;
}
static void espnow_add_peer(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    const uint8_t *mac = Espnow.add_peer_args.mac;

    Espnow.peer_add_args.mac = mac;
    espnow_peer_add(work);
}
static void espnow_send(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *mac = Espnow.send_args.mac;
    uint8_t type = Espnow.send_args.type;
    const uint8_t *payload = Espnow.send_args.payload;
    size_t len = Espnow.send_args.len;

    (void)mac;
    (void)type;
    (void)payload;
    (void)len;
    Espnow.ok = PROTO_FALSE;
}
static void espnow_broadcast(uint8_t *restrict work)
{
    (void)work;
    uint8_t type = Espnow.broadcast_args.type;
    const uint8_t *payload = Espnow.broadcast_args.payload;
    size_t len = Espnow.broadcast_args.len;

    (void)type;
    (void)payload;
    (void)len;
    Espnow.ok = PROTO_FALSE;
}

#endif // PROTOCORE_HAS_VENDOR_WIFI

EspnowNs Espnow = {
    .encode = espnow_encode,
    .decode = espnow_decode,
    .peers_reset = espnow_peers_reset,
    .peer_add = espnow_peer_add,
    .peer_has = espnow_peer_has,
    .peer_remove = espnow_peer_remove,
    .peer_count = espnow_peer_count,
    .begin = espnow_begin,
    .add_peer = espnow_add_peer,
    .send = espnow_send,
    .broadcast = espnow_broadcast,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ESPNOW
