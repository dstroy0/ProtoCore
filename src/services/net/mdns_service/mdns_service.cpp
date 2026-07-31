// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_service.cpp
 * @brief mDNS / DNS-SD advertisement implementation (PC_ENABLE_MDNS).
 *
 * Uses the ESP-IDF `mdns` component directly (not the Arduino ESPmDNS wrapper)
 * so the only external dependency stays the base SDK + mbedTLS.
 */

#include "mdns_service.h"

#if PC_ENABLE_MDNS && defined(ARDUINO)

#include "mdns.h"

bool pc_mdns_begin(const char *hostname, uint16_t http_port)
{
    if (!hostname || hostname[0] == '\0')
    {
        return false;
    }
    if (mdns_init() != ESP_OK)
    {
        return false;
    }
    if (mdns_hostname_set(hostname) != ESP_OK)
    {
        return false;
    }
    // Advertise an HTTP service so browsers / DNS-SD tools discover the device.
    mdns_service_add(nullptr, "_http", "_tcp", http_port, nullptr, 0);
    return true;
}

bool pc_mdns_txt(const char *key, const char *value)
{
    if (!key || !value)
    {
        return false;
    }
    // Attach a TXT key/value to the _http._tcp service (Bonjour browsers show it).
    return mdns_service_txt_item_set("_http", "_tcp", key, value) == ESP_OK;
}

bool pc_mdns_add_service(const char *service_type, const char *proto, uint16_t port)
{
    if (!service_type || !proto)
    {
        return false;
    }
    // Advertise an additional service, e.g. ("_https", "_tcp", 443).
    return mdns_service_add(nullptr, service_type, proto, port, nullptr, 0) == ESP_OK;
}

#else

bool pc_mdns_begin(const char *hostname, uint16_t http_port)
{
    (void)hostname;
    (void)http_port;
    return false; // mDNS disabled at compile time (or non-Arduino build)
}

#endif // PC_ENABLE_MDNS && ARDUINO
