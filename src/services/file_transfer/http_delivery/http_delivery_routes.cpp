// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_delivery_routes.cpp
 * @brief Serves the service worker + its precache manifest (see http_delivery.h).
 *
 * Separated from the host-testable core (http_delivery.cpp) so the manifest serializer unit-tests
 * without pulling in the server, matching dashboard.cpp / dashboard_routes.cpp.
 */

#include "services/file_transfer/http_delivery/http_delivery.h"

#if PC_ENABLE_HTTP_DELIVERY && defined(ARDUINO)

#include "network_drivers/application/web_assets.h" // PC_SERVICE_WORKER
#include "protocore.h"
#include "shared_primitives/mime.h"

// All service-worker route state, owned by one instance (internal linkage): the server handle plus
// the borrowed precache list the manifest is rebuilt from on each request. The route handlers are
// fixed-signature callbacks, so they reach this single owner directly.
struct DeliveryRoutesCtx
{
    const char *const *paths = nullptr;
    size_t n = 0;
    const char *version = nullptr;
};
static DeliveryRoutesCtx s_delr;

static void sw_script_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    // No instance test: a handler only runs because this service registered the route.
    send_text(slot_id, 200, PC_MIME_JAVASCRIPT, PC_SERVICE_WORKER);
}

static void sw_manifest_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char buf[PC_DELIVERY_MANIFEST_BUF];
    // Rebuilt per request rather than cached: it is small, and the version/list can be changed at
    // runtime without a stale copy surviving.
    if (pc_delivery_sw_manifest(s_delr.paths, s_delr.n, s_delr.version, buf, sizeof(buf)) == 0)
    {
        send_text(slot_id, 500, PC_MIME_JSON, "{\"error\":\"manifest too large\"}");
        return;
    }
    send_text(slot_id, 200, PC_MIME_JSON, buf);
}

bool pc_delivery_serve_sw(const char *const *paths, size_t n, const char *version)
{
    if (!paths || n == 0 || n > PC_DELIVERY_PRECACHE_MAX || !version)
    {
        return false;
    }
    s_delr.paths = paths;
    s_delr.n = n;
    s_delr.version = version;
    // The worker's scope is the path it is served from, so it must sit at the root to control the
    // whole origin - "/sw.js", not "/assets/sw.js".
    on_http("/sw.js", HttpMethod::HTTP_GET, sw_script_handler);
    on_http("/precache.json", HttpMethod::HTTP_GET, sw_manifest_handler);
    return true;
}

#endif // PC_ENABLE_HTTP_DELIVERY && ARDUINO
