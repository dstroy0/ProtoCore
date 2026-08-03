// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_service.c
 * @brief mDNS / DNS-SD advertisement implementation (PC_ENABLE_MDNS).
 *
 * Uses the ESP-IDF `mdns` component directly (not the Arduino ESPmDNS wrapper)
 * so the only external dependency stays the base SDK + mbedTLS.
 */

#include "mdns_service.h"

#if PC_ENABLE_MDNS && PROTOCORE_HOT

#include "mdns.h"

proto_bool pc_mdns_begin(const char *hostname, uint16_t http_port)
{
    if (hostname == NULL || hostname[0] == '\0')
    {
        return PROTO_FALSE;
    }
    if (mdns_init() != ESP_OK)
    {
        return PROTO_FALSE;
    }
    if (mdns_hostname_set(hostname) != ESP_OK)
    {
        return PROTO_FALSE;
    }
    // Advertise an HTTP service so browsers / DNS-SD tools discover the device.
    mdns_service_add(NULL, "_http", "_tcp", http_port, NULL, 0);
    return PROTO_TRUE;
}

proto_bool pc_mdns_txt(const char *key, const char *value)
{
    if (key == NULL || value == NULL)
    {
        return PROTO_FALSE;
    }
    // Attach a TXT key/value to the _http._tcp service (Bonjour browsers show it).
    return mdns_service_txt_item_set("_http", "_tcp", key, value) == ESP_OK;
}

proto_bool pc_mdns_add_service(const char *service_type, const char *proto, uint16_t port)
{
    if (service_type == NULL || proto == NULL)
    {
        return PROTO_FALSE;
    }
    // Advertise an additional service, e.g. ("_https", "_tcp", 443).
    return mdns_service_add(NULL, service_type, proto, port, NULL, 0) == ESP_OK;
}

#else

// The header declares all three whenever PC_ENABLE_MDNS is set, so all three are defined here.
// mdns_adaptive and the mDNS examples call pc_mdns_txt and pc_mdns_add_service, which a build with
// the flag on and no responder resolves to these.

proto_bool pc_mdns_begin(const char *hostname, uint16_t http_port)
{
    (void)hostname;
    (void)http_port;
    return PROTO_FALSE; // mDNS disabled at compile time (or non-Arduino build)
}

proto_bool pc_mdns_txt(const char *key, const char *value)
{
    (void)key;
    (void)value;
    return PROTO_FALSE;
}

proto_bool pc_mdns_add_service(const char *service_type, const char *proto, uint16_t port)
{
    (void)service_type;
    (void)proto;
    (void)port;
    return PROTO_FALSE;
}

#endif // PC_ENABLE_MDNS && PROTOCORE_HOT
