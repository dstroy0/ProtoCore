// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file GpioMap.ino
 * @brief Browser GPIO pin-mapper / diagnostics panel (PC_ENABLE_GPIO_MAP).
 *
 * Declares a compile-time table of GPIO pins (number, label, direction) and serves
 * them at GET /gpio as JSON with live levels; POST /gpio (body `pin=<n>&level=0|1`)
 * drives a pin marked as an output. A small inline page at "/" polls the JSON and
 * renders the pin map, with toggle buttons for the outputs - a zero-dependency
 * browser diag tool. The serializer + control parser are host-tested; the digital
 * read / write run on the ESP32.
 *
 * NOTE: enable it for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPC_ENABLE_GPIO_MAP=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_GPIO_MAP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/system/gpio_map/gpio_map.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// The pins to expose. Caller-owned and must outlive the server. Mark a pin
// pc_gpio_dir::PC_GPIO_OUT to make it drivable from the panel.
static pc_gpio_pin gpio_pins[] = {
    {2, "Onboard LED", pc_gpio_dir::PC_GPIO_OUT, 0},
    {0, "BOOT button", pc_gpio_dir::PC_GPIO_IN_PULLUP, 0},
    {4, "Relay", pc_gpio_dir::PC_GPIO_OUT, 0},
    {34, "ADC sense", pc_gpio_dir::PC_GPIO_IN, 0},
};
static const uint8_t gpio_count = sizeof(gpio_pins) / sizeof(gpio_pins[0]);

// A tiny zero-dependency diag page: fetch /gpio, render rows, toggle outputs.
static const char DIAG_PAGE[] = R"HTML(<!doctype html><meta name=viewport content="width=device-width">
<title>GPIO map</title><style>body{font:14px system-ui;margin:2rem}
table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:.3rem .6rem}
button{cursor:pointer}</style><h1>GPIO map</h1><table id=t>
<tr><th>Pin<th>Label<th>Dir<th>Level<th></tr></table>
<script>
async function load(){
 let r=await fetch('/gpio'),j=await r.json(),t=document.getElementById('t');
 t.innerHTML='<tr><th>Pin<th>Label<th>Dir<th>Level<th></tr>';
 for(const p of j.pins){
  let tr=t.insertRow(),btn=p.dir=='out'
   ?`<button onclick="set(${p.pin},${p.level?0:1})">set ${p.level?0:1}</button>`:'';
  tr.innerHTML=`<td>${p.pin}<td>${p.label}<td>${p.dir}<td>${p.level}<td>${btn}`;
 }
}
async function set(pin,level){
 await fetch('/gpio',{method:'POST',body:`pin=${pin}&level=${level}`});load();
}
load();setInterval(load,2000);
</script>)HTML";

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
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // GET /gpio (JSON) + POST /gpio (drive an output); pinMode is applied here.
    pc_gpio_map_begin("/gpio", gpio_pins, gpio_count);

    on_http("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { send_text(id, 200, "text/html", DIAG_PAGE); });
    begin_http(80);
}

void loop()
{
    handle();
}
