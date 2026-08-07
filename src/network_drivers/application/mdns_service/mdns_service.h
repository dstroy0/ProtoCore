// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_service.h
 * @brief Optional mDNS / DNS-SD advertisement (PC_ENABLE_MDNS).
 *
 * Responds for `<hostname>.local` and advertises an `_http._tcp` service, with optional TXT records
 * and extra service types.
 *
 * Two backends, chosen by PC_HAS_VENDOR_MDNS. Where the SDK ships a responder the wrapper drives
 * that one, because it also probes and resolves name conflicts. Where it does not, the portable
 * responder answers RFC 6762 / RFC 6763 queries on 224.0.0.251:5353 over the UDP listener: A for the
 * host, PTR for the enumeration name and each service type, and SRV + TXT per instance. It advertises
 * rather than defends - no probing, no conflict resolution - so two devices given the same hostname
 * on one link both answer to it.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MDNS_SERVICE_H
#define PROTOCORE_MDNS_SERVICE_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_MDNS

/**
 * @brief Start mDNS responder and advertise an HTTP service.
 *
 * Call once after the WiFi link is up (Physical.wifi->ready()) and after begin(). The
 * device becomes reachable at `<hostname>.local` and advertises
 * `_http._tcp` on @p http_port.
 *
 * @param hostname   Host label without the `.local` suffix (e.g. "mydevice").
 * @param http_port  TCP port the HTTP server listens on (default 80).
 * @return true if the responder started; false if disabled at compile time,
 *         not on Arduino, or the mdns component failed to start.
 */
proto_bool pc_mdns_begin(const char *hostname, uint16_t http_port);

/**
 * @brief Add a TXT key/value record to the advertised `_http._tcp` service.
 *
 * Call after pc_mdns_begin(). Example: `"path"`=`"/"`, `"fw"`=`"1.2.3"`.
 *
 * @return true on success; false if mDNS is disabled or not running.
 */
proto_bool pc_mdns_txt(const char *key, const char *value);

/**
 * @brief Advertise an additional service, e.g. `("_https", "_tcp", 443)`.
 *
 * @param service_type DNS-SD service type, e.g. `"_https"`.
 * @param proto        `"_tcp"` or `"_udp"`.
 * @param port         TCP/UDP port the service listens on.
 * @return true on success; false if mDNS is disabled or not running.
 */
proto_bool pc_mdns_add_service(const char *service_type, const char *proto, uint16_t port);

#endif // PC_ENABLE_MDNS

PROTO_END_DECLS

#endif // PROTOCORE_MDNS_SERVICE_H
