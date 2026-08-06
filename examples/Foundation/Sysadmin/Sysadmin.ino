// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Sysadmin.ino
 * @brief System-administration example: a dark-themed HTML/CSS dashboard, a
 *        stats API, and a clean reboot sequencer.
 *
 * This example showcases:
 *   1. Serving a single-page dashboard from a PROGMEM string.
 *   2. Responsive CSS-grid styling with dark-mode aesthetics.
 *   3. A hardware-stats endpoint (/api/sysinfo): heap, CPU, WiFi RSSI, etc.
 *   4. A clean reboot sequence: respond to the client first, then restart in
 *      loop() after a 1 s delay so lwIP buffers flush cleanly.
 *   5. Restricting admin APIs with a custom header check (X-Admin-Token).
 *
 * To run this example:
 *   - Configure SSID/PASSWORD, and upload to an ESP32.
 *   - Find the IP in the Serial Monitor and open it in a browser.
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// Simple authentication token for AJAX API queries.
static const char *ADMIN_TOKEN = "admin123";

// System control flags for asynchronous reboot scheduling.
static bool pending_reboot = false;
static unsigned long reboot_trigger_ms = 0;

// --- Embedded dashboard HTML (dark theme) ---
static const char ADMIN_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Admin Node Console</title>
    <style>
        :root {
            --bg-color: #0b0f19;
            --card-bg: #161b2a;
            --text-color: #e2e8f0;
            --text-muted: #94a3b8;
            --accent: #6366f1;
            --accent-glow: rgba(99, 102, 241, 0.4);
            --success: #10b981;
            --danger: #ef4444;
            --border: #2e354f;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
            padding: 2rem 1.5rem;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        .container { width: 100%; max-width: 900px; display: flex; flex-direction: column; gap: 2rem; }
        header {
            display: flex; justify-content: space-between; align-items: center;
            border-bottom: 1px solid var(--border); padding-bottom: 1.5rem;
        }
        .logo-area { display: flex; align-items: center; gap: 0.75rem; }
        h1 {
            font-size: 1.5rem; font-weight: 700;
            background: linear-gradient(135deg, #a5b4fc, #6366f1);
            -webkit-background-clip: text; -webkit-text-fill-color: transparent;
        }
        .status-dot {
            width: 10px; height: 10px; background-color: var(--success);
            border-radius: 50%; box-shadow: 0 0 10px var(--success); animation: pulse 2s infinite;
        }
        @keyframes pulse {
            0% { box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.7); }
            70% { box-shadow: 0 0 0 8px rgba(16, 185, 129, 0); }
            100% { box-shadow: 0 0 0 0 rgba(16, 185, 129, 0); }
        }
        .token-input-card {
            background-color: var(--card-bg); border: 1px solid var(--border);
            border-radius: 12px; padding: 1.25rem; display: flex; gap: 1rem; align-items: center;
        }
        .token-input-card label { font-weight: 600; color: var(--text-muted); white-space: nowrap; }
        .token-input-card input {
            background-color: var(--bg-color); border: 1px solid var(--border); border-radius: 6px;
            color: var(--text-color); padding: 0.5rem 0.75rem; flex-grow: 1; font-family: monospace; outline: none;
        }
        .token-input-card input:focus { border-color: var(--accent); box-shadow: 0 0 5px var(--accent-glow); }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 1.5rem; }
        .card {
            background-color: var(--card-bg); border: 1px solid var(--border); border-radius: 12px;
            padding: 1.5rem; display: flex; flex-direction: column; gap: 1rem;
            transition: transform 0.2s, border-color 0.2s;
        }
        .card:hover { transform: translateY(-2px); border-color: var(--accent); }
        .card-title {
            font-size: 0.875rem; text-transform: uppercase; letter-spacing: 0.05em;
            color: var(--text-muted); font-weight: 600;
        }
        .card-value { font-size: 1.8rem; font-weight: 700; color: #ffffff; }
        .data-list { list-style: none; display: flex; flex-direction: column; gap: 0.5rem; }
        .data-item { display: flex; justify-content: space-between; font-size: 0.9rem; }
        .data-label { color: var(--text-muted); }
        .data-value { font-weight: 500; font-family: monospace; }
        .actions { display: flex; gap: 1rem; margin-top: 1rem; }
        button {
            padding: 0.75rem 1.5rem; border: none; border-radius: 8px; cursor: pointer;
            font-weight: 600; font-size: 0.95rem; transition: background 0.2s, transform 0.1s;
            display: flex; align-items: center; justify-content: center; gap: 0.5rem;
        }
        button:active { transform: scale(0.98); }
        .btn-primary { background-color: var(--accent); color: white; }
        .btn-primary:hover { background-color: #5558e6; }
        .btn-danger { background-color: var(--danger); color: white; }
        .btn-danger:hover { background-color: #dc2626; }
        .modal {
            display: none; position: fixed; top: 0; left: 0; right: 0; bottom: 0;
            background-color: rgba(11, 15, 25, 0.85); align-items: center; justify-content: center;
            z-index: 100; backdrop-filter: blur(4px);
        }
        .modal-content {
            background-color: var(--card-bg); border: 1px solid var(--border); border-radius: 16px;
            padding: 2rem; max-width: 400px; width: 90%; text-align: center;
            display: flex; flex-direction: column; gap: 1.5rem; box-shadow: 0 10px 25px rgba(0, 0, 0, 0.5);
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <div class="logo-area">
                <div class="status-dot"></div>
                <h1>ESP32 Admin Console</h1>
            </div>
            <span style="font-family: monospace; font-size: 0.9rem;" id="ip-addr">Node Active</span>
        </header>

        <div class="token-input-card">
            <label for="token">Access Token:</label>
            <input type="password" id="token" value="admin123" placeholder="Enter X-Admin-Token">
        </div>

        <div class="grid">
            <div class="card">
                <div class="card-title">Memory Allocation</div>
                <div class="card-value" id="free-heap">-- KB</div>
                <ul class="data-list">
                    <li class="data-item"><span class="data-label">Min Free Heap</span><span class="data-value" id="min-heap">-- KB</span></li>
                    <li class="data-item"><span class="data-label">Max Alloc Block</span><span class="data-value" id="max-alloc">-- KB</span></li>
                </ul>
            </div>

            <div class="card">
                <div class="card-title">System Metrics</div>
                <ul class="data-list" style="margin-top: 0.5rem;">
                    <li class="data-item"><span class="data-label">Uptime</span><span class="data-value" id="uptime">-- s</span></li>
                    <li class="data-item"><span class="data-label">Reset Reason</span><span class="data-value" id="reset-reason">--</span></li>
                    <li class="data-item"><span class="data-label">Chip Revision</span><span class="data-value" id="chip-rev">--</span></li>
                    <li class="data-item"><span class="data-label">CPU Freq</span><span class="data-value" id="cpu-freq">-- MHz</span></li>
                </ul>
            </div>

            <div class="card">
                <div class="card-title">Network Info</div>
                <div class="card-value" id="wifi-rssi">-- dBm</div>
                <ul class="data-list">
                    <li class="data-item"><span class="data-label">SSID</span><span class="data-value" id="wifi-ssid">--</span></li>
                    <li class="data-item"><span class="data-label">Channel</span><span class="data-value" id="wifi-channel">--</span></li>
                </ul>
            </div>
        </div>

        <div class="actions">
            <button class="btn-primary" onclick="loadStats()">Refresh Stats</button>
            <button class="btn-danger" onclick="confirmReboot()">Restart Device</button>
        </div>
    </div>

    <!-- Reboot Modal -->
    <div class="modal" id="reboot-modal">
        <div class="modal-content">
            <h2>Restart ESP32?</h2>
            <p style="color: var(--text-muted);">This will temporarily disconnect all socket sessions and reboot the node firmware.</p>
            <div class="actions" style="justify-content: center;">
                <button class="btn-primary" onclick="closeRebootModal()">Cancel</button>
                <button class="btn-danger" onclick="triggerReboot()">Confirm Restart</button>
            </div>
        </div>
    </div>

    <script>
        const getHeaders = () => ({ 'X-Admin-Token': document.getElementById('token').value });

        async function loadStats() {
            try {
                const res = await fetch('/api/sysinfo', { headers: getHeaders() });
                if (res.status === 401) { alert('Unauthorized: Invalid Access Token!'); return; }
                const data = await res.json();
                document.getElementById('free-heap').innerText = Math.round(data.free_heap / 1024) + ' KB';
                document.getElementById('min-heap').innerText = Math.round(data.min_free_heap / 1024) + ' KB';
                document.getElementById('max-alloc').innerText = Math.round(data.max_alloc_heap / 1024) + ' KB';
                document.getElementById('uptime').innerText = Math.round(data.uptime_ms / 1000) + ' s';
                document.getElementById('reset-reason').innerText = data.reset_reason;
                document.getElementById('chip-rev').innerText = 'Rev ' + data.chip_revision;
                document.getElementById('cpu-freq').innerText = data.cpu_freq_mhz + ' MHz';
                document.getElementById('wifi-rssi').innerText = data.wifi_rssi + ' dBm';
                document.getElementById('wifi-ssid').innerText = data.wifi_ssid;
                document.getElementById('wifi-channel').innerText = data.wifi_channel;
                document.getElementById('ip-addr').innerText = data.ip_address;
            } catch (err) { console.error('Failed to load system stats', err); }
        }

        function confirmReboot() { document.getElementById('reboot-modal').style.display = 'flex'; }
        function closeRebootModal() { document.getElementById('reboot-modal').style.display = 'none'; }

        async function triggerReboot() {
            closeRebootModal();
            try {
                const res = await fetch('/api/restart', { method: 'POST', headers: getHeaders() });
                if (res.status === 200) {
                    alert('Reboot signal accepted. Reconnecting in 5 seconds...');
                    setTimeout(() => window.location.reload(), 5000);
                } else {
                    alert('Error: Reboot rejected by node.');
                }
            } catch (err) { alert('Connection lost. Node is rebooting...'); }
        }

        window.onload = loadStats;
    </script>
</body>
</html>
)rawhtml";

// --- Helper Functions ---

/** @brief Translate the ESP32 reset-reason enum into a friendly string. */
const char *get_reset_reason_string(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:
        return "Power On";
    case ESP_RST_EXT:
        return "External Pin Reset";
    case ESP_RST_SW:
        return "Software Reboot";
    case ESP_RST_PANIC:
        return "Software Panic / Crash";
    case ESP_RST_INT_WDT:
        return "Interrupt Watchdog";
    case ESP_RST_TASK_WDT:
        return "Task Watchdog";
    case ESP_RST_WDT:
        return "Generic Watchdog";
    case ESP_RST_DEEPSLEEP:
        return "Deep Sleep Exit";
    case ESP_RST_BROWNOUT:
        return "Brownout Event";
    case ESP_RST_SDIO:
        return "SDIO Interface Reset";
    default:
        return "Unknown";
    }
}

/** @brief Verify the admin security-token header. */
bool verify_admin_privileges(const HttpReq *req)
{
    const char *token = http_get_header(req, "X-Admin-Token");
    return (token && strcmp(token, ADMIN_TOKEN) == 0);
}

// --- HttpRoute Handlers ---

/** @brief GET /: serve the embedded single-page dashboard. */
void handle_serve_dashboard(uint8_t slot_id, HttpReq *req)
{
    send_text(slot_id, 200, "text/html", ADMIN_HTML);
}

/** @brief GET /api/sysinfo: serialize hardware telemetry to JSON (auth required). */
void handle_get_sysinfo(uint8_t slot_id, HttpReq *req)
{
    if (!verify_admin_privileges(req))
    {
        send_text(slot_id, 401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }

    char ssid[33];
    Physical.wifi->ssid(ssid, sizeof(ssid));
    uint32_t ip = Physical.link->egress_ip();
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
             (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

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
             get_reset_reason_string(esp_reset_reason()), ESP.getChipRevision(), ESP.getCpuFreqMHz(), Physical.wifi->rssi(),
             ssid, Physical.wifi->channel(), ip_str);

    send_text(slot_id, 200, "application/json", response_buf);
}

/** @brief POST /api/restart: acknowledge, then arm the reboot timer (auth required). */
void handle_post_restart(uint8_t slot_id, HttpReq *req)
{
    if (!verify_admin_privileges(req))
    {
        send_text(slot_id, 401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }

    // Acknowledge first so the response reaches the client before we reboot.
    send_text(slot_id, 200, "application/json", "{\"status\":\"rebooting\"}");

    pending_reboot = true;
    reboot_trigger_ms = millis();
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- PC SysAdmin Control Console ---");

    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Online!");
    uint32_t ip = Physical.link->egress_ip();
    Serial.printf("Access the dashboard via: http://%u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    set_cors("*");

    on_http("/", HTTP_GET, handle_serve_dashboard);
    on_http("/api/sysinfo", HTTP_GET, handle_get_sysinfo);
    on_http("/api/restart", HTTP_POST, handle_post_restart);

    int32_t result = begin_http(80, NULL);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("SysAdmin console initialized on port 80");
}

void loop()
{
    handle();

    // Perform the scheduled reboot once the ack has had time to flush.
    if (pending_reboot && (millis() - reboot_trigger_ms >= 1000))
    {
        Serial.println("Reboot sequence triggered. Rebooting now!");
        delay(100);
        ESP.restart();
    }
}
