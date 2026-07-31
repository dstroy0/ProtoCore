// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file espnow.cpp
 * @brief ESP-NOW envelope codec + peer registry (pure) and esp_now binding (ESP32).
 */

#include "services/radio/espnow/espnow.h"

#if PC_ENABLE_ESPNOW

#include <string.h>

#if defined(ARDUINO)
#include <esp_idf_version.h> // ESP_IDF_VERSION / ESP_IDF_VERSION_VAL for the recv-cb ABI guard
#include <esp_now.h>
#include <esp_wifi.h>
#endif
const uint8_t PC_ESPNOW_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---------------------------------------------------------------------------
// Envelope codec
// ---------------------------------------------------------------------------
size_t pc_espnow_encode(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t cap)
{
    if (!out || len > PC_ESPNOW_MAX_PAYLOAD || cap < len + PC_ESPNOW_HDR)
    {
        return 0;
    }
    out[0] = PC_ESPNOW_MAGIC;
    out[1] = type;
    out[2] = (uint8_t)len;
    if (len && payload)
    {
        memcpy(out + PC_ESPNOW_HDR, payload, len);
    }
    return len + PC_ESPNOW_HDR;
}

bool pc_espnow_decode(const uint8_t *buf, size_t len, uint8_t *type, const uint8_t **payload, size_t *plen)
{
    if (!buf || len < PC_ESPNOW_HDR || buf[0] != PC_ESPNOW_MAGIC)
    {
        return false;
    }
    size_t declared = buf[2];
    if (declared + PC_ESPNOW_HDR != len) // length must match exactly (no trailing/short)
    {
        return false;
    }
    if (type)
    {
        *type = buf[1];
    }
    if (payload)
    {
        *payload = buf + PC_ESPNOW_HDR;
    }
    if (plen)
    {
        *plen = declared;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Peer registry
// ---------------------------------------------------------------------------
namespace
{
struct Peer
{
    uint8_t mac[6];
    bool used;
};

// All ESP-NOW runtime state, owned by one instance (internal linkage): the peer registry
// (host + radio) plus the radio binding's recv callback and channel, grouped so it is one
// named owner, unreachable from any other translation unit.
struct EspnowCtx
{
    Peer peers[PC_ESPNOW_MAX_PEERS];
#ifdef ARDUINO
    pc_espnow_recv_fn recv = nullptr;
    uint8_t channel = 0;
#endif
};
EspnowCtx s_espnow;

int peer_find(const EspnowCtx &c, const uint8_t mac[6])
{
    for (int i = 0; i < PC_ESPNOW_MAX_PEERS; i++)
    {
        if (c.peers[i].used && memcmp(c.peers[i].mac, mac, 6) == 0)
        {
            return i;
        }
    }
    return -1;
}
} // namespace

void pc_espnow_peers_reset(void)
{
    for (int i = 0; i < PC_ESPNOW_MAX_PEERS; i++)
    {
        s_espnow.peers[i].used = false;
    }
}

bool pc_espnow_peer_add(const uint8_t mac[6])
{
    if (!mac)
    {
        return false;
    }
    if (peer_find(s_espnow, mac) >= 0)
    {
        return true; // idempotent
    }
    for (int i = 0; i < PC_ESPNOW_MAX_PEERS; i++)
    {
        if (!s_espnow.peers[i].used)
        {
            memcpy(s_espnow.peers[i].mac, mac, 6);
            s_espnow.peers[i].used = true;
            return true;
        }
    }
    return false; // table full
}

bool pc_espnow_peer_has(const uint8_t mac[6])
{
    return mac && peer_find(s_espnow, mac) >= 0;
}

bool pc_espnow_peer_remove(const uint8_t mac[6])
{
    int i = mac ? peer_find(s_espnow, mac) : -1;
    if (i < 0)
    {
        return false;
    }
    s_espnow.peers[i].used = false;
    return true;
}

int pc_espnow_peer_count(void)
{
    int n = 0;
    for (int i = 0; i < PC_ESPNOW_MAX_PEERS; i++)
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
#ifdef ARDUINO

namespace
{
// The ESP-NOW receive callback signature changed in ESP-IDF 5.0 (Arduino-ESP32 3.x): the
// source MAC moved into an esp_now_recv_info_t. Match whichever the compiled core expects.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    const uint8_t *mac = info ? info->src_addr : nullptr;
#else
void on_recv(const uint8_t *mac, const uint8_t *data, int len)
{
#endif
    if (!s_espnow.recv || len < 0 || !mac)
    {
        return;
    }
    uint8_t type;
    const uint8_t *payload;
    size_t plen;
    if (pc_espnow_decode(data, (size_t)len, &type, &payload, &plen))
    {
        s_espnow.recv(mac, type, payload, plen);
    }
}

bool radio_add_peer(const uint8_t mac[6], uint8_t channel)
{
    esp_now_peer_info_t p;
    memset(&p, 0, sizeof(p));
    memcpy(p.peer_addr, mac, 6);
    p.channel = channel;
    p.encrypt = false;
    if (esp_now_is_peer_exist(mac))
    {
        return true;
    }
    return esp_now_add_peer(&p) == ESP_OK;
}
} // namespace

bool pc_espnow_begin(uint8_t channel, pc_espnow_recv_fn cb)
{
    s_espnow.channel = channel;
    s_espnow.recv = cb;
    if (esp_now_init() != ESP_OK)
    {
        return false;
    }
    esp_now_register_recv_cb(on_recv);
    pc_espnow_peers_reset();
    return radio_add_peer(PC_ESPNOW_BROADCAST, channel); // broadcast is always a peer
}

bool pc_espnow_add_peer(const uint8_t mac[6])
{
    if (!pc_espnow_peer_add(mac))
    {
        return false;
    }
    return radio_add_peer(mac, s_espnow.channel);
}

bool pc_espnow_send(const uint8_t mac[6], uint8_t type, const uint8_t *payload, size_t len)
{
    uint8_t frame[PC_ESPNOW_HDR + PC_ESPNOW_MAX_PAYLOAD];
    size_t n = pc_espnow_encode(type, payload, len, frame, sizeof(frame));
    if (n == 0)
    {
        return false;
    }
    return esp_now_send(mac, frame, n) == ESP_OK;
}

bool pc_espnow_broadcast(uint8_t type, const uint8_t *payload, size_t len)
{
    return pc_espnow_send(PC_ESPNOW_BROADCAST, type, payload, len);
}

#else // host build - no radio

bool pc_espnow_begin(uint8_t, pc_espnow_recv_fn)
{
    return false;
}
bool pc_espnow_add_peer(const uint8_t mac[6])
{
    return pc_espnow_peer_add(mac);
}
bool pc_espnow_send(const uint8_t *, uint8_t, const uint8_t *, size_t)
{
    return false;
}
bool pc_espnow_broadcast(uint8_t, const uint8_t *, size_t)
{
    return false;
}

#endif // ARDUINO

#endif // PC_ENABLE_ESPNOW
