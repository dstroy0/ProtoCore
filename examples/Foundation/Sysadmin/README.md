# Sysadmin - a single-page admin console

**Layer:** Foundation (tutorial) · **Build flags:** none (core features only)

## What this example teaches

This serves a self-contained dark-themed admin dashboard (HTML/CSS/JS in one
`PROGMEM` string), backs it with a hardware-telemetry JSON API, and performs a
clean remote reboot. It is the pattern for "ship a small web UI from flash with
no filesystem."

**Serving a page from flash (`PROGMEM`).** The whole dashboard is a single
`R"rawhtml(...)"` raw string literal kept in flash, served in one call - no
LittleFS, no chunking:

```cpp
static const char ADMIN_HTML[] PROGMEM = R"rawhtml( <!DOCTYPE html> ... )rawhtml";
...
void handle_serve_dashboard(uint8_t slot_id, HttpReq *req) {
    server.send(slot_id, 200, "text/html", ADMIN_HTML);
}
```

**An AJAX telemetry API.** The page's JavaScript `fetch()`es `/api/sysinfo`,
which serializes live hardware stats (heap, CPU, reset reason, WiFi RSSI/SSID/
channel, IP) into a JSON object built with `snprintf` into a fixed buffer:

```cpp
snprintf(response_buf, sizeof(response_buf),
         "{\"free_heap\":%u,\"uptime_ms\":%lu,\"reset_reason\":\"%s\",\"wifi_rssi\":%d, ...}",
         ESP.getFreeHeap(), millis(), get_reset_reason_string(esp_reset_reason()), pc_net_rssi(), ...);
```

The reset reason is decoded from the ESP-IDF enum into a friendly string by
`get_reset_reason_string()` - handy for diagnosing why the device last rebooted
(panic, brownout, watchdog, etc.).

**Header-token auth.** Admin endpoints require a custom `X-Admin-Token` header
(the browser sends it from a token field), returning `401` otherwise:

```cpp
const char *token = http_get_header(req, "X-Admin-Token");
return (token && strcmp(token, ADMIN_TOKEN) == 0);
```

**A clean deferred reboot.** This is the key trick: you must not call
`ESP.restart()` inside the handler, or the HTTP response never flushes. Instead
the handler acknowledges (`200`), arms a flag, and `loop()` reboots ~1 s later
once lwIP has drained the response:

```cpp
// in the handler:
server.send(slot_id, 200, "application/json", "{\"status\":\"rebooting\"}");
pending_reboot = true; reboot_trigger_ms = millis();
// in loop():
if (pending_reboot && (millis() - reboot_trigger_ms >= 1000)) { delay(100); ESP.restart(); }
```

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/Foundation/Sysadmin/Sysadmin.ino
```

Flash, then open `http://<ip>/` in a browser. The default token is `admin123`
(prefilled). Or hit the API directly:

```sh
curl -H "X-Admin-Token: admin123" http://<ip>/api/sysinfo
curl -X POST -H "X-Admin-Token: admin123" http://<ip>/api/restart
```

## Annotated source

The complete sketch ([Sysadmin.ino](Sysadmin.ino)). The dashboard's
dark-theme CSS is elided here for brevity (it is plain styling; see the `.ino`
for the full markup); the C++ and the page's AJAX script are shown verbatim with
added comments.

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

static const char *ADMIN_TOKEN = "admin123"; // expected X-Admin-Token value

// Deferred-reboot state: set by the handler, acted on later in loop().
static bool pending_reboot = false;
static unsigned long reboot_trigger_ms = 0;

// The entire dashboard lives in flash as one raw string. Served in one send().
static const char ADMIN_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Admin Node Console</title>
    <style>
        /* ... full dark-theme CSS (variables, cards, grid, modal) in the .ino ... */
    </style>
</head>
<body>
    <!-- header, a token input, three stat cards (Memory / System / Network),
         Refresh + Restart buttons, and a confirm-reboot modal (see the .ino) -->
    <script>
        // Every API call carries the token typed into the page.
        const getHeaders = () => ({ 'X-Admin-Token': document.getElementById('token').value });

        async function loadStats() {                 // GET /api/sysinfo -> fill the cards
            try {
                const res = await fetch('/api/sysinfo', { headers: getHeaders() });
                if (res.status === 401) { alert('Unauthorized: Invalid Access Token!'); return; }
                const data = await res.json();
                document.getElementById('free-heap').innerText = Math.round(data.free_heap / 1024) + ' KB';
                /* ...the rest of the fields are filled the same way... */
            } catch (err) { console.error('Failed to load system stats', err); }
        }

        async function triggerReboot() {             // POST /api/restart
            closeRebootModal();
            try {
                const res = await fetch('/api/restart', { method: 'POST', headers: getHeaders() });
                if (res.status === 200) { alert('Reboot accepted. Reconnecting...'); setTimeout(() => location.reload(), 5000); }
                else { alert('Error: Reboot rejected by node.'); }
            } catch (err) { alert('Connection lost. Node is rebooting...'); }
        }
        window.onload = loadStats;
    </script>
</body>
</html>
)rawhtml";

// --- Helper Functions ---

/** Translate the ESP32 reset-reason enum into a human-friendly string. */
const char *get_reset_reason_string(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:   return "Power On";
    case ESP_RST_EXT:       return "External Pin Reset";
    case ESP_RST_SW:        return "Software Reboot";
    case ESP_RST_PANIC:     return "Software Panic / Crash";
    case ESP_RST_INT_WDT:   return "Interrupt Watchdog";
    case ESP_RST_TASK_WDT:  return "Task Watchdog";
    case ESP_RST_WDT:       return "Generic Watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep Sleep Exit";
    case ESP_RST_BROWNOUT:  return "Brownout Event";
    case ESP_RST_SDIO:      return "SDIO Interface Reset";
    default:                return "Unknown";
    }
}

/** Verify the admin token header (X-Admin-Token). */
bool verify_admin_privileges(const HttpReq *req)
{
    const char *token = http_get_header(req, "X-Admin-Token");
    return (token && strcmp(token, ADMIN_TOKEN) == 0);
}

// --- Route Handlers ---

/** GET / - serve the embedded single-page dashboard straight from flash. */
void handle_serve_dashboard(uint8_t slot_id, HttpReq *req)
{
    server.send(slot_id, 200, "text/html", ADMIN_HTML);
}

/** GET /api/sysinfo - hardware telemetry as JSON (token required). */
void handle_get_sysinfo(uint8_t slot_id, HttpReq *req)
{
    if (!verify_admin_privileges(req))
    {
        server.send(slot_id, 401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }

    char response_buf[384];
    snprintf(response_buf, sizeof(response_buf),
             "{"
             "\"free_heap\":%u,"
             "\"min_free_heap\":%u,"
             "\"max_alloc_heap\":%u,"
             "\"uptime_ms\":%lu,"
             "\"reset_reason\":\"%s\","
             "\"chip_revision\":%d,"
             "\"cpu_freq_mhz\":%d,"
             "\"wifi_rssi\":%d,"
             "\"wifi_ssid\":\"%s\","
             "\"wifi_channel\":%d,"
             "\"ip_address\":\"%s\""
             "}",
             ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), millis(),
             get_reset_reason_string(esp_reset_reason()), ESP.getChipRevision(), ESP.getCpuFreqMHz(), pc_net_rssi(),
             ssid, pc_net_channel(), ip_str);

    server.send(slot_id, 200, "application/json", response_buf);
}

/** POST /api/restart - acknowledge, then arm the deferred reboot (token required). */
void handle_post_restart(uint8_t slot_id, HttpReq *req)
{
    if (!verify_admin_privileges(req))
    {
        server.send(slot_id, 401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }

    // Respond FIRST so the client gets the ack before the socket dies.
    server.send(slot_id, 200, "application/json", "{\"status\":\"rebooting\"}");

    pending_reboot = true;          // actual restart happens in loop()
    reboot_trigger_ms = millis();
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- PC SysAdmin Control Console ---");

    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Online!");
    Serial.print("Access the dashboard via: http://");
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.set_cors("*");

    server.on("/", HttpMethod::HTTP_GET, handle_serve_dashboard);
    server.on("/api/sysinfo", HttpMethod::HTTP_GET, handle_get_sysinfo);
    server.on("/api/restart", HttpMethod::HTTP_POST, handle_post_restart);

    int32_t result = server.begin(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("SysAdmin console initialized on port 80");
}

void loop()
{
    server.handle();

    // Reboot ~1s after the ack so lwIP can flush the response cleanly.
    if (pending_reboot && (millis() - reboot_trigger_ms >= 1000))
    {
        Serial.println("Reboot sequence triggered. Rebooting now!");
        delay(100);
        ESP.restart();
    }
}
```
