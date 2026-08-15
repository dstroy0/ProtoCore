// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file espnow.c
 * @brief ESP-NOW envelope codec + peer registry (pure) and esp_now binding (ESP32).
 */

#include "services/radio/espnow/espnow.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_ESPNOW

#if PROTOCORE_HAS_VENDOR_WIFI
#include <esp_idf_version.h> // ESP_IDF_VERSION / ESP_IDF_VERSION_VAL for the recv-cb ABI guard
#include <esp_now.h>
#include <esp_wifi.h>
#endif
const uint8_t PROTOCORE_ESPNOW_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---------------------------------------------------------------------------
// Envelope codec
// ---------------------------------------------------------------------------
size_t protocore_espnow_encode(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t cap)
{
    if (!out || len > PROTOCORE_ESPNOW_MAX_PAYLOAD || cap < len + PROTOCORE_ESPNOW_HDR)
    {
        return 0;
    }
    out[0] = PROTOCORE_ESPNOW_MAGIC;
    out[1] = type;
    out[2] = (uint8_t)len;
    if (len && payload)
    {
        mem.cpy(out + PROTOCORE_ESPNOW_HDR, payload, len);
    }
    return len + PROTOCORE_ESPNOW_HDR;
}

proto_bool protocore_espnow_decode(const uint8_t *buf, size_t len, uint8_t *type, const uint8_t **payload, size_t *plen)
{
    if (!buf || len < PROTOCORE_ESPNOW_HDR || buf[0] != PROTOCORE_ESPNOW_MAGIC)
    {
        return PROTO_FALSE;
    }
    size_t declared = buf[2];
    if (declared + PROTOCORE_ESPNOW_HDR != len) // length must match exactly (no trailing/short)
    {
        return PROTO_FALSE;
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
    return PROTO_TRUE;
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
static EspnowCtx s_espnow;

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

void protocore_espnow_peers_reset(void)
{
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        s_espnow.peers[i].used = PROTO_FALSE;
    }
}

proto_bool protocore_espnow_peer_add(const uint8_t mac[6])
{
    if (!mac)
    {
        return PROTO_FALSE;
    }
    if (peer_find(&s_espnow, mac) >= 0)
    {
        return PROTO_TRUE; // idempotent
    }
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        if (!s_espnow.peers[i].used)
        {
            mem.cpy(s_espnow.peers[i].mac, mac, 6);
            s_espnow.peers[i].used = PROTO_TRUE;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE; // table full
}

proto_bool protocore_espnow_peer_has(const uint8_t mac[6])
{
    return mac && peer_find(&s_espnow, mac) >= 0;
}

proto_bool protocore_espnow_peer_remove(const uint8_t mac[6])
{
    int i = mac ? peer_find(&s_espnow, mac) : -1;
    if (i < 0)
    {
        return PROTO_FALSE;
    }
    s_espnow.peers[i].used = PROTO_FALSE;
    return PROTO_TRUE;
}

int protocore_espnow_peer_count(void)
{
    int n = 0;
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        if (s_espnow.peers[i].used)
        {
            n++;
        }
    }
    return n;
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
    if (!s_espnow.recv || len < 0 || !mac)
    {
        return;
    }
    uint8_t type;
    const uint8_t *payload;
    size_t plen;
    if (protocore_espnow_decode(data, (size_t)len, &type, &payload, &plen))
    {
        s_espnow.recv(mac, type, payload, plen);
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

proto_bool protocore_espnow_begin(uint8_t channel, protocore_espnow_recv_fn cb)
{
    s_espnow.channel = channel;
    s_espnow.recv = cb;
    if (esp_now_init() != ESP_OK)
    {
        return PROTO_FALSE;
    }
    esp_now_register_recv_cb(on_recv);
    protocore_espnow_peers_reset();
    return radio_add_peer(PROTOCORE_ESPNOW_BROADCAST, channel); // broadcast is always a peer
}

proto_bool protocore_espnow_add_peer(const uint8_t mac[6])
{
    if (!protocore_espnow_peer_add(mac))
    {
        return PROTO_FALSE;
    }
    return radio_add_peer(mac, s_espnow.channel);
}

proto_bool protocore_espnow_send(const uint8_t mac[6], uint8_t type, const uint8_t *payload, size_t len)
{
    uint8_t frame[PROTOCORE_ESPNOW_HDR + PROTOCORE_ESPNOW_MAX_PAYLOAD];
    size_t n = protocore_espnow_encode(type, payload, len, frame, sizeof(frame));
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    return esp_now_send(mac, frame, n) == ESP_OK;
}

proto_bool protocore_espnow_broadcast(uint8_t type, const uint8_t *payload, size_t len)
{
    return protocore_espnow_send(PROTOCORE_ESPNOW_BROADCAST, type, payload, len);
}

#else // host build - no radio

proto_bool protocore_espnow_begin(uint8_t channel, protocore_espnow_recv_fn cb)
{
    (void)channel;
    (void)cb;
    return PROTO_FALSE;
}
proto_bool protocore_espnow_add_peer(const uint8_t mac[6])
{
    return protocore_espnow_peer_add(mac);
}
proto_bool protocore_espnow_send(const uint8_t *mac, uint8_t type, const uint8_t *payload, size_t len)
{
    (void)mac;
    (void)type;
    (void)payload;
    (void)len;
    return PROTO_FALSE;
}
proto_bool protocore_espnow_broadcast(uint8_t type, const uint8_t *payload, size_t len)
{
    (void)type;
    (void)payload;
    (void)len;
    return PROTO_FALSE;
}

#endif // PROTOCORE_HAS_VENDOR_WIFI

#endif // PROTOCORE_ENABLE_ESPNOW
