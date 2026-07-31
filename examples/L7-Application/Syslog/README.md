# Syslog - remote logging to a syslog server (RFC 5424)

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_SYSLOG`

## What this example teaches

This ships device log lines to a central syslog collector as RFC 5424 UDP
datagrams - a zero-heap structured-logging sink for a fleet of devices. It logs at
boot, once a second as a heartbeat, and once per HTTP request through the
access-log hook.

**Initialize the sink, then log by severity.**

```cpp
pc_syslog_init(SYSLOG_SERVER, SYSLOG_PORT, "esp32-pc", "pc", SyslogFacility::SYSLOG_FAC_LOCAL0);
pc_syslog_log(SyslogSeverity::SYSLOG_NOTICE, "device booted");
```

`pc_syslog_init(host, port, hostname, app_name, facility)` configures the UDP
destination and the fields stamped into every message; `pc_syslog_log(severity, msg)`
emits one datagram (severities `SYSLOG_ERR` / `SYSLOG_WARNING` / `SYSLOG_INFO` /
`SYSLOG_NOTICE`, ...).

**Per-request access logging via a hook.** `on_request_log()` registers a callback
the server invokes for every response, so you can map HTTP status onto syslog
severity:

```cpp
static void access_log(const char *method, const char *path, int status, int len) {
    char line[96];
    snprintf(line, sizeof(line), "%s %s -> %d (%d bytes)", method, path, status, len);
    pc_syslog_log(status >= 500 ? SyslogSeverity::SYSLOG_ERR : status >= 400 ? SyslogSeverity::SYSLOG_WARNING : SyslogSeverity::SYSLOG_INFO, line);
}
server.on_request_log(access_log);
```

Point `SYSLOG_SERVER` at your collector. Port 514 is the IANA syslog port; use an
unprivileged port like 5140 with an ad-hoc UDP listener for testing.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SYSLOG=1" \
  --lib="." examples/L7-Application/Syslog/Syslog.ino
```

A quick UDP listener on port 5140 (set `SYSLOG_PORT` to 5140 to match):

```sh
python -c "import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('0.0.0.0',5140));[print(s.recvfrom(2048)[0].decode()) for _ in iter(int,1)]"
```

## Annotated source

The complete sketch ([Syslog.ino](Syslog.ino)), reproduced verbatim with
added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_SYSLOG 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/syslog/syslog.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *SYSLOG_SERVER = "192.168.1.10"; // your syslog collector
static const uint16_t SYSLOG_PORT = 514;           // 514 = IANA syslog; use 5140 for an unprivileged listener

PC server;

// Per-request access log -> syslog (status maps to severity).
static void access_log(const char *method, const char *path, int status, int len)
{
    char line[96];
    snprintf(line, sizeof(line), "%s %s -> %d (%d bytes)", method, path, status, len);
    pc_syslog_log(status >= 500 ? SyslogSeverity::SYSLOG_ERR : status >= 400 ? SyslogSeverity::SYSLOG_WARNING : SyslogSeverity::SYSLOG_INFO, line);
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_syslog_init(SYSLOG_SERVER, SYSLOG_PORT, "esp32-pc", "pc", SyslogFacility::SYSLOG_FAC_LOCAL0);
    pc_syslog_log(SyslogSeverity::SYSLOG_NOTICE, "device booted");

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "ok"); });
    server.on_request_log(access_log); // every response is logged to syslog

    int32_t result = server.begin(80);
    if (result < 0)
        Serial.printf("begin() failed (error %d)\n", result);
    else
        Serial.println("HTTP server on :80; logging to syslog");
}

void loop()
{
    server.handle();

    // Heartbeat once a second.
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        char hb[48];
        snprintf(hb, sizeof(hb), "heartbeat uptime=%lus heap=%u", (unsigned long)(millis() / 1000),
                 (unsigned)ESP.getFreeHeap());
        pc_syslog_log(SyslogSeverity::SYSLOG_INFO, hb);
    }
}
```
