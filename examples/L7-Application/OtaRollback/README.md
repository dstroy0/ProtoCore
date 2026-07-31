# OtaRollback - OTA rollback protection / soft-brick safeguard

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_OTA_ROLLBACK`

## What this example teaches

A bad OTA image that boots but cannot do its job will soft-brick a remote device.
The ESP32 bootloader can mark a freshly flashed image `PENDING_VERIFY` and roll
back to the previous one unless the new firmware confirms itself. This wraps that
mechanism: each loop it runs a self-test (here WiFi up + healthy heap) and ticks the
rollback service - a passing self-test commits the image, a failing one (or no
confirm within `PC_OTA_CONFIRM_WINDOW_MS`) rolls back. So a bad update self-heals
instead of bricking. It is the safety net for the OTA upload in
[OTA](../OTA).

**Define a self-test, then tick until committed:**

```cpp
static bool self_test() { return wifi_ready() && ESP.getFreeHeap() > 20000; }

void loop() {
    static bool done = false;
    if (!done) {
        pc_ota_action a = pc_ota_rollback_tick(self_test());
        if (a == pc_ota_action::PC_OTA_COMMIT) { Serial.println("[ota] image committed"); done = true; }
    }
    server.handle();
}
```

`pc_ota_rollback_tick(ok)` is a no-op once the image is committed or on a
normally-booted image, so it is safe to call every loop. `pc_ota_img_state()`
reports the current image state for the `/ota-state` endpoint.

**Requirement.** Actual rollback needs the bootloader's app-rollback support
(`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`) enabled in the build.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_OTA_ROLLBACK=1" \
  --lib="." examples/L7-Application/OtaRollback/OtaRollback.ino
```

```sh
curl http://<ip>/ota-state   # {"img_state":...} - the running image's verify state
```

## Annotated source

The complete sketch ([OtaRollback.ino](OtaRollback.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_OTA_ROLLBACK 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/system/ota_rollback/ota_rollback.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// The health check that decides commit vs rollback. Put your real checks here.
static bool self_test()
{
    return wifi_ready() && ESP.getFreeHeap() > 20000;
}

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
        delay(250);
    Serial.print("IP: ");
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/ota-state", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char b[48];
        snprintf(b, sizeof(b), "{\"img_state\":%u}", pc_ota_img_state());
        server.send(id, 200, "application/json", b);
    });
    server.begin(80);
}

void loop()
{
    // Confirm (or roll back) a freshly-updated image. A no-op once committed or on
    // a normally-booted image.
    static bool done = false;
    if (!done)
    {
        pc_ota_action a = pc_ota_rollback_tick(self_test());
        if (a == pc_ota_action::PC_OTA_COMMIT)
        {
            Serial.println("[ota] image committed");
            done = true;
        }
    }
    server.handle();
}
```
