// SpaFallback - a single-page UI that still works when the single-page UI does not.
//
// A device UI that is only a JavaScript bundle has a failure mode nobody plans for: the shell asset
// is missing after a half-finished upload, the filesystem got wiped, the browser will not run
// scripts, or the device came up degraded. On a machine you can actuate, "the page did not load" is
// not an acceptable state - an operator still has to see what is happening and be able to stop it.
//
// services/web/spa_router routes for that:
//
//   /api/...        -> PASSTHROUGH   the handlers, always - including in fallback mode
//   /app.js         -> SERVE_FILE    a real asset (a missing one is a real 404, not a rewrite)
//   /dashboard      -> SERVE_SHELL   a client route, when the SPA can actually serve it
//   /dashboard      -> SERVE_FALLBACK  ... when it cannot
//
// The fallback page is assembled from fragments with predicates and streamed in fixed-size chunks,
// so only the panels that currently apply are emitted and the page never has to fit in RAM at once.
//
// GET /             -> shell, or the fallback page
// GET /dashboard    -> same (a client-side route)
// GET /?nojs=1      -> force the fallback (what a non-scripting client would get)
// GET /api/state    -> passes through, in either mode
// GET /degrade      -> toggle the degraded flag and watch every route flip to the fallback
// GET /shell        -> toggle whether the shell asset is "present"
//
// Build flags (whole build): PC_ENABLE_SPA_ROUTER=1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/web/spa_router/spa_router.h"
#include "shared_primitives/mime.h"

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-password";


// Device state the UI reflects. A real build would read these from the machine.
struct HmiState
{
    bool alarm;
    bool degraded;
    bool shell_present;
};
static HmiState g_hmi = {false, false, true};

// --- the conditional fragments the fallback page is built from ------------

static bool when_alarm(void *ctx)
{
    return ((HmiState *)ctx)->alarm;
}
static bool when_ok(void *ctx)
{
    return !((HmiState *)ctx)->alarm;
}
static bool when_degraded(void *ctx)
{
    return ((HmiState *)ctx)->degraded;
}

// Plain HTML and a plain form: no JavaScript, no asset files, nothing to fail to load.
static const pc_ui_fragment HMI_FRAGMENTS[] = {
    {"head", "<!doctype html><html><head><title>HMI</title></head><body><h1>Device HMI</h1>", nullptr},
    {"degraded", "<p><b>DEGRADED MODE</b> - the full UI is unavailable.</p>", when_degraded},
    {"alarm", "<p style=\"color:red\"><b>ALARM ACTIVE</b></p>", when_alarm},
    {"ok", "<p>Status: normal</p>", when_ok},
    {"controls", "<form method=\"POST\" action=\"/api/stop\"><button>STOP</button></form>", nullptr},
    {"foot", "<p><a href=\"/api/state\">raw state</a></p></body></html>", nullptr},
};
static const size_t HMI_FRAGMENT_COUNT = sizeof(HMI_FRAGMENTS) / sizeof(HMI_FRAGMENTS[0]);

// Stream the fallback page out in small chunks, proving the page never has to fit in one buffer.
static void send_fallback(uint8_t slot_id)
{
    char page[512];
    size_t total = 0;
    pc_ui_stream s;
    pc_ui_stream_begin(&s, HMI_FRAGMENTS, HMI_FRAGMENT_COUNT, &g_hmi);
    // A deliberately small chunk: the cursor resumes mid-fragment, so this produces the same bytes
    // any larger buffer would.
    for (;;)
    {
        size_t n = pc_ui_stream_next(&s, page + total, 48 < sizeof(page) - total ? 48 : sizeof(page) - total);
        if (n == 0)
        {
            break;
        }
        total += n;
        if (total >= sizeof(page) - 1)
        {
            break;
        }
    }
    page[total] = '\0';
    send_text(slot_id, 200, PC_MIME_TEXT_HTML, page);
}

// Would this client actually run the SPA? There is no reliable header for it, so this is the app's
// call - here an explicit ?nojs=1 for testing. A real build might also weigh the Accept header.
static bool client_will_script(HttpReq *req)
{
    for (uint8_t i = 0; i < req->query_count; i++)
    {
        if (strcmp(req->query_params[i].key, "nojs") == 0)
        {
            return false;
        }
    }
    return true;
}

static void ui_handler(uint8_t slot_id, HttpReq *req)
{
    pc_spa_ctx ctx;
    ctx.api_prefix = "/api/";
    ctx.shell_available = g_hmi.shell_present;
    ctx.client_scripting = client_will_script(req);
    ctx.degraded = g_hmi.degraded;

    switch (pc_spa_route_ex(req->path, &ctx))
    {
    case pc_spa_action::PC_SPA_SERVE_SHELL:
        // A real build hands this to serve_static(index.html); inline here so the example needs no
        // filesystem to demonstrate the decision.
        send_text(slot_id, 200, PC_MIME_TEXT_HTML,
                    "<!doctype html><html><body><div id=app></div>"
                    "<script src=/app.js></script></body></html>");
        break;
    case pc_spa_action::PC_SPA_SERVE_FALLBACK:
        send_fallback(slot_id);
        break;
    case pc_spa_action::PC_SPA_SERVE_FILE:
        send_text(slot_id, 404, PC_MIME_TEXT_PLAIN, "no such asset\n");
        break;
    case pc_spa_action::PC_SPA_PASSTHROUGH:
    default:
        send_text(slot_id, 500, PC_MIME_TEXT_PLAIN, "routed here by mistake\n");
        break;
    }
}

static void state_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char json[128];
    snprintf(json, sizeof(json), "{\"alarm\":%s,\"degraded\":%s,\"shell\":%s}", g_hmi.alarm ? "true" : "false",
             g_hmi.degraded ? "true" : "false", g_hmi.shell_present ? "true" : "false");
    send_text(slot_id, 200, PC_MIME_JSON, json);
}

static void stop_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    g_hmi.alarm = !g_hmi.alarm; // stand-in for a real actuation
    send_text(slot_id, 200, PC_MIME_TEXT_PLAIN, g_hmi.alarm ? "alarm set\n" : "alarm cleared\n");
}

static void degrade_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    g_hmi.degraded = !g_hmi.degraded;
    send_text(slot_id, 200, PC_MIME_TEXT_PLAIN, g_hmi.degraded ? "degraded\n" : "normal\n");
}

static void shell_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    g_hmi.shell_present = !g_hmi.shell_present;
    send_text(slot_id, 200, PC_MIME_TEXT_PLAIN, g_hmi.shell_present ? "shell present\n" : "shell missing\n");
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    Physical.wifi->init(WIFI_SSID, WIFI_PASS);
    while (!Physical.wifi->ready())
    {
        delay(250);
    }

    on_http("/", HTTP_GET, ui_handler);
    on_http("/dashboard", HTTP_GET, ui_handler);
    on_http("/api/state", HTTP_GET, state_handler);
    on_http("/api/stop", HTTP_GET, stop_handler);
    on_http("/degrade", HTTP_GET, degrade_handler);
    on_http("/shell", HTTP_GET, shell_handler);
    begin_http(80, NULL);

    uint32_t ip = Physical.link->egress_ip();
    Serial.printf("http://%u.%u.%u.%u/\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    handle();
}
