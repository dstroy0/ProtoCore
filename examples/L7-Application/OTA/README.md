# OTA - authenticated over-the-air firmware update

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_OTA`

## What this example teaches

This registers a streaming `POST /update` endpoint: a firmware image POSTed with
valid HTTP Basic credentials is fed straight into the ESP32 `Update` API through
the parser's streaming-body hook, so the image never has to fit in RAM, then the
device reboots into the new firmware.

**One call wires authenticated streaming OTA:**

```cpp
pc_ota_begin(server, "/update", "admin", "s3cret"); // POST body -> Update, then reboot
```

The body is streamed chunk by chunk into flash as it arrives; only requests with
the configured Basic credentials are accepted. Because it uses the same
streaming-body hook as file upload, **enable OTA or upload, not both** (see
[FileUpload](../FileUpload)). Build your `firmware.bin` with `pio run`
(it lands at `.pio/build/<env>/firmware.bin`).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_OTA=1 -DMAX_CONNS=4" \
  --lib="." examples/L7-Application/OTA/OTA.ino
```

```sh
curl -u admin:s3cret --data-binary @firmware.bin http://<ip>/update
```

## Annotated source

The complete sketch ([OTA.ino](OTA.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_OTA 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/ota_service.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void handle_root(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.send(slot_id, 200, "text/plain", "OTA demo - POST a firmware image to /update");
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

    server.on("/", HttpMethod::HTTP_GET, handle_root);
    if (server.begin(80) < 0)
    {
        Serial.println("begin() failed");
        return;
    }

    // Authenticated streaming OTA at POST /update (Basic admin:s3cret).
    pc_ota_begin(server, "/update", "admin", "s3cret");

    Serial.println("Server up; OTA at POST /update");
}

void loop()
{
    server.handle();
}
```
