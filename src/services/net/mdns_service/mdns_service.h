// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_service.h
 * @brief Optional mDNS / DNS-SD advertisement (PC_ENABLE_MDNS).
 *
 * Thin wrapper over the ESP-IDF `mdns` component so a headless device is reachable
 * at `<hostname>.local` and advertises an `_http._tcp` service for browsers and
 * Bonjour / DNS-SD tools (with optional TXT records and extra service types).
 * Compiled to a no-op stub when PC_ENABLE_MDNS is 0 or on non-Arduino builds,
 * so it costs nothing unless enabled.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MDNS_SERVICE_H
#define PROTOCORE_MDNS_SERVICE_H

#include "protocore_config.h"
#include <stdint.h>

/**
 * @brief Start mDNS responder and advertise an HTTP service.
 *
 * Call once after the WiFi link is up (wifi_ready()) and after begin(). The
 * device becomes reachable at `<hostname>.local` and advertises
 * `_http._tcp` on @p http_port.
 *
 * @param hostname   Host label without the `.local` suffix (e.g. "mydevice").
 * @param http_port  TCP port the HTTP server listens on (default 80).
 * @return true if the responder started; false if disabled at compile time,
 *         not on Arduino, or the mdns component failed to start.
 */
bool pc_mdns_begin(const char *hostname, uint16_t http_port = 80);

/**
 * @brief Add a TXT key/value record to the advertised `_http._tcp` service.
 *
 * Bonjour / DNS-SD browsers display these (e.g. `"path"`=`"/"`, `"fw"`=`"1.2.3"`).
 * Call after pc_mdns_begin().
 *
 * @return true on success; false if mDNS is disabled or not running.
 */
bool pc_mdns_txt(const char *key, const char *value);

/**
 * @brief Advertise an additional service, e.g. `("_https", "_tcp", 443)`.
 *
 * @param service_type DNS-SD service type, e.g. `"_https"`.
 * @param proto        `"_tcp"` or `"_udp"`.
 * @param port         TCP/UDP port the service listens on.
 * @return true on success; false if mDNS is disabled or not running.
 */
bool pc_mdns_add_service(const char *service_type, const char *proto, uint16_t port);

#endif // PROTOCORE_MDNS_SERVICE_H
