// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_delivery_routes.c
 * @brief Serves the service worker + its precache manifest (see http_delivery.h).
 *
 * Separated from the host-testable core (http_delivery.cpp) so the manifest serializer unit-tests
 * without pulling in the server, matching dashboard.cpp / dashboard_routes.cpp.
 */

#include "services/file_transfer/http_delivery/http_delivery.h"

static uint8_t http_delivery_work[16]; // the borrow an entry takes; HttpDelivery never reads it

#if PROTOCORE_ENABLE_HTTP_DELIVERY

#include "network_drivers/application/web_assets.h" // PROTOCORE_SERVICE_WORKER
#include "protocore.h"
#include "shared/mime/mime.h"

// All service-worker route state, owned by one instance (internal linkage): the server handle plus
// the borrowed precache list the manifest is rebuilt from on each request. The route handlers are
// fixed-signature callbacks, so they reach this single owner directly.
typedef struct
{
    const char *const *paths;
    size_t n;
    const char *version;
} DeliveryRoutesCtx;
static DeliveryRoutesCtx s_delr;

static void sw_script_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    // No instance test: a handler only runs because this service registered the route.
    send_text(slot_id, 200, PROTOCORE_MIME_JAVASCRIPT, PROTOCORE_SERVICE_WORKER);
}

static void sw_manifest_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char buf[PROTOCORE_DELIVERY_MANIFEST_BUF];
    // Rebuilt per request rather than cached: it is small, and the version/list can be changed at
    // runtime without a stale copy surviving.
    HttpDelivery.sw_manifest_args.paths = s_delr.paths;
    HttpDelivery.sw_manifest_args.n = s_delr.n;
    HttpDelivery.sw_manifest_args.version = s_delr.version;
    HttpDelivery.sw_manifest_args.out = buf;
    HttpDelivery.sw_manifest_args.cap = sizeof(buf);
    HttpDelivery.sw_manifest(http_delivery_work);
    if (HttpDelivery.n == 0)
    {
        send_text(slot_id, 500, PROTOCORE_MIME_JSON, "{\"error\":\"manifest too large\"}");
        return;
    }
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, buf);
}

void http_delivery_serve_sw(uint8_t *restrict work)
{
    (void)work;
    const char *const *paths = HttpDelivery.serve_sw_args.paths;
    const size_t n = HttpDelivery.serve_sw_args.n;
    const char *version = HttpDelivery.serve_sw_args.version;

    HttpDelivery.ok = PROTO_FALSE;
    if (!paths || n == 0 || n > PROTOCORE_DELIVERY_PRECACHE_MAX || !version)
    {
        return;
    }
    s_delr.paths = paths;
    s_delr.n = n;
    s_delr.version = version;
    // The worker's scope is the path it is served from, so it must sit at the root to control the
    // whole origin - "/sw.js", not "/assets/sw.js".
    on_http("/sw.js", HTTP_GET, sw_script_handler);
    on_http("/precache.json", HTTP_GET, sw_manifest_handler);
    HttpDelivery.ok = PROTO_TRUE;
}

#endif // PROTOCORE_ENABLE_HTTP_DELIVERY
