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
//   * RFC 7233 byte ranges - already handled by the file server (PROTOCORE_ENABLE_RANGE): a client can
//     fetch just the new tail of a growing log with `Range: bytes=N-` and get a 206.
//
// Files are served from SD, so the shell is real content rather than flash strings.
//
// Build flags (whole build): PROTOCORE_ENABLE_HTTP_DELIVERY=1 PROTOCORE_ENABLE_FILE_SERVING=1 PROTOCORE_ENABLE_RANGE=1 PROTOCORE_ENABLE_MNT=1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/application/file_serving/file_serving.h"
#include "services/file_transfer/http_delivery/http_delivery.h"
#include "test/core_setup/hal/esp/esp_mnt_fs.h" // protocore_mnt_fs(): bind an Arduino FS to the storage seam
#include "shared/mime/mime.h" // PROTOCORE_MIME_TEXT_HTML
#include <SD_MMC.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-password";

// The shell the service worker precaches. Bump SHELL_VERSION whenever these change: the worker
// names its cache after it, so a new version invalidates the old shell exactly once.
static const char *const SHELL[] = {"/", "/index.html", "/app.css"};
static const char *SHELL_VERSION = "1.0.0";

static uint8_t http_delivery_work[16]; // the borrow an entry takes; HttpDelivery never reads it


static void root_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    // Registers the worker, then shows what it cached.
    send_text(slot_id, 200, PROTOCORE_MIME_TEXT_HTML,
                "<!doctype html><meta charset=utf-8><title>PC delivery</title>"
                "<h1>PC delivery</h1><p id=s>registering...</p>"
                "<script>navigator.serviceWorker.register('/sw.js').then(function(){"
                "document.getElementById('s').textContent='service worker registered';})"
                ".catch(function(e){document.getElementById('s').textContent='sw failed: '+e;});</script>");
}

void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = WIFI_SSID;
    PhysicalV.wifi.password = WIFI_PASS;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }

    if (!SD_MMC.begin())
    {
        Serial.println("SD mount failed - static files will 404");
    }
    else
    {
        FileServingV.serve_static_args.url_prefix = "/files/";
        FileServingV.serve_static_args.file_sys = protocore_mnt_fs(&SD_MMC);
        FileServingV.serve_static_args.fs_root = "/";
        FileServing.serve_static(protocore_file_serving_span()); // Range/206 comes free with PROTOCORE_ENABLE_RANGE
    }

    // Every served file carries the SWR policy: fresh for 60 s, then usable-while-revalidating for
    // another 300 s. Built by the RFC 5861 core so header and decision cannot drift apart.
    set_cache_control_swr(60, 300);

    on_http("/", HTTP_GET, root_handler);
    // Serves /sw.js + /precache.json.
    HttpDeliveryV.serve_sw_args.paths = SHELL;
    HttpDeliveryV.serve_sw_args.n = sizeof(SHELL) / sizeof(SHELL[0]);
    HttpDeliveryV.serve_sw_args.version = SHELL_VERSION;
    HttpDelivery.serve_sw(http_delivery_work);
    if (!HttpDeliveryV.ok)
    {
        Serial.println("service-worker routes failed to register");
    }

    begin_http(80, NULL);

    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32;
    Serial.printf("http://%u.%u.%u.%u/  (sw /sw.js, manifest /precache.json, files /files/...)\n",
                  (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                  (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    handle();
}
