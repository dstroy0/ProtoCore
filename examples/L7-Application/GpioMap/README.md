# GpioMap - a browser GPIO pin-mapper / diagnostics panel

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_GPIO_MAP`

## What this example teaches

This declares a compile-time table of GPIO pins (number, label, direction) and
serves them at `GET /gpio` as JSON with live levels; `POST /gpio` (body
`pin=<n>&level=0|1`) drives a pin marked as an output. A small inline page at `/`
polls the JSON and renders the pin map with toggle buttons - a zero-dependency
browser diagnostics tool. The JSON serializer and the control parser are
host-tested; only the digital read/write run on the ESP32.

**Declare the pins, then mount the endpoint.** The table is caller-owned and must
outlive the server; mark a pin `PC_GPIO_OUT` to make it drivable from the panel:

```cpp
static pc_gpio_pin gpio_pins[] = {
    {2,  "Onboard LED", pc_gpio_dir::PC_GPIO_OUT,       0},
    {0,  "BOOT button", pc_gpio_dir::PC_GPIO_IN_PULLUP, 0},
    {4,  "Relay",       pc_gpio_dir::PC_GPIO_OUT,       0},
    {34, "ADC sense",   pc_gpio_dir::PC_GPIO_IN,        0},
};

pc_gpio_map_begin(server, "/gpio", gpio_pins, gpio_count); // applies pinMode, adds GET+POST /gpio
```

`pc_gpio_map_begin()` applies `pinMode` for each entry and registers both the
JSON read route and the write route, so the only application code left is to serve
the page that drives it.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_GPIO_MAP=1" \
  --lib="." examples/L7-Application/GpioMap/GpioMap.ino
```

Flash, then open `http://<ip>/` for the live pin map, or hit the API directly:

```sh
curl http://<ip>/gpio                                 # {"pins":[...]}
curl -X POST http://<ip>/gpio --data "pin=2&level=1"  # drive an output high
```

## Annotated source

The complete sketch ([GpioMap.ino](GpioMap.ino)), reproduced verbatim with
added explanatory comments. The inline `DIAG_PAGE` HTML/JS string is elided here
for length (see the source); it just fetches `/gpio`, renders a table, and POSTs
toggles.

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_GPIO_MAP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "server/signaling/gpio_map.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// The pins to expose. Caller-owned and must outlive the server. Mark a pin
// PC_GPIO_OUT to make it drivable from the panel.
static pc_gpio_pin gpio_pins[] = {
    {2, "Onboard LED", pc_gpio_dir::PC_GPIO_OUT, 0},
    {0, "BOOT button", pc_gpio_dir::PC_GPIO_IN_PULLUP, 0},
    {4, "Relay", pc_gpio_dir::PC_GPIO_OUT, 0},
    {34, "ADC sense", pc_gpio_dir::PC_GPIO_IN, 0},
};
static const uint8_t gpio_count = sizeof(gpio_pins) / sizeof(gpio_pins[0]);

// A tiny zero-dependency diag page: fetch /gpio, render rows, toggle outputs.
static const char DIAG_PAGE[] = R"HTML(... inline HTML/JS elided; see the source ...)HTML";

void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // GET /gpio (JSON) + POST /gpio (drive an output); pinMode is applied here.
    pc_gpio_map_begin(server, "/gpio", gpio_pins, gpio_count);

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/html", DIAG_PAGE); });
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
