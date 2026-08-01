// HttpDelivery - serve an app shell that loads instantly and refreshes in the background.
//
// A constrained device is a slow origin: it may be busy, throttled, or asleep. Three standards make
// that acceptable to a browser, and this wires all three:
//
//   * RFC 5861 stale-while-revalidate - set_cache_control_swr(max_age, swr) tells the client it may
//     keep using its copy for max_age, then serve the stale copy for another swr seconds *while* it
//     refreshes in the background. The page never blocks on this device.
//   * A service worker - /sw.js precaches the shell listed in /precache.json and serves it
//     stale-while-revalidate client-side, so a repeat visit paints with the device untouched (and
//     still works while it is offline or asleep).
//   * RFC 7233 byte ranges - already handled by the file server (PC_ENABLE_RANGE): a client can
//     fetch just the new tail of a growing log with `Range: bytes=N-` and get a 206.
//
// Files are served from SD, so the shell is real content rather than flash strings.
//
// Build flags (whole build): PC_ENABLE_HTTP_DELIVERY=1 PC_ENABLE_FILE_SERVING=1 PC_ENABLE_RANGE=1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/file_transfer/http_delivery/http_delivery.h"
#include "shared_primitives/mime.h" // PC_MIME_TEXT_HTML
#include <SD_MMC.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-password";

// The shell the service worker precaches. Bump SHELL_VERSION whenever these change: the worker
// names its cache after it, so a new version invalidates the old shell exactly once.
static const char *const SHELL[] = {"/", "/index.html", "/app.css"};
static const char *SHELL_VERSION = "1.0.0";


static void root_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    // Registers the worker, then shows what it cached.
    send_text(slot_id, 200, PC_MIME_TEXT_HTML,
                "<!doctype html><meta charset=utf-8><title>PC delivery</title>"
                "<h1>PC delivery</h1><p id=s>registering...</p>"
                "<script>navigator.serviceWorker.register('/sw.js').then(function(){"
                "document.getElementById('s').textContent='service worker registered';})"
                ".catch(function(e){document.getElementById('s').textContent='sw failed: '+e;});</script>");
}

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(WIFI_SSID, WIFI_PASS);
    while (!wifi_ready())
    {
        delay(250);
    }

    if (!SD_MMC.begin())
    {
        Serial.println("SD mount failed - static files will 404");
    }
    else
    {
        serve_static("/files/", SD_MMC, "/"); // Range/206 comes free with PC_ENABLE_RANGE
    }

    // Every served file carries the SWR policy: fresh for 60 s, then usable-while-revalidating for
    // another 300 s. Built by the RFC 5861 core so header and decision cannot drift apart.
    set_cache_control_swr(60, 300);

    on_http("/", HttpMethod::HTTP_GET, root_handler);
    // Serves /sw.js + /precache.json.
    if (!pc_delivery_serve_sw(server, SHELL, sizeof(SHELL) / sizeof(SHELL[0]), SHELL_VERSION))
    {
        Serial.println("service-worker routes failed to register");
    }

    begin_http(80);

    uint32_t ip = pc_net_egress_ip();
    Serial.printf("http://%u.%u.%u.%u/  (sw /sw.js, manifest /precache.json, files /files/...)\n",
                  (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                  (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    handle();
}
