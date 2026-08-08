// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Mnt.ino
 * @brief Mounted storage over a real filesystem (PC_ENABLE_MNT).
 *
 * The same pc_fs_* API drives a RAM pool in tests and a real filesystem on the
 * device. Here it is mounted on LittleFS, so writes persist across reboots:
 *
 *   GET /save?name=greeting&data=hello   -> stores /greeting on flash
 *   GET /load?name=greeting              -> returns its contents
 *   GET /size?name=greeting              -> byte count (-1 if absent)
 *   GET /rm?name=greeting                -> deletes it
 *
 * Note what the handlers do NOT do: they never build a path. pc_fs_begin() binds
 * the root once and hands back a handle; every call below passes that handle, an
 * empty dir, and the client's name straight through - the accessor joins the three
 * and refuses any `..` before storage is touched. A query of `name=../../secret`
 * is rejected without this sketch containing a single line about it.
 *
 * To run entirely in RAM instead (no flash, deterministic), mount the built-in
 * backend: `pc_mnt_mount(pc_mnt_ram());` - every endpoint below is unchanged.
 * That is the whole point: features target one API, the application chooses the
 * medium.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_MNT=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_MNT 1

#include "protocore.h"
#include "core_setup/hal/esp/esp_mnt_fs.h"
#include "network_drivers/physical/physical.h"
#include "server/filesystem/filesystem.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// The root pc_fs_begin() binds, at file scope because the handlers below are captureless lambdas.
static int s_root = -1;


void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
    {
        delay(250);
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    LittleFS.begin(true); // format on first use
    pc_mnt_mount(pc_mnt_fs(&LittleFS));
    s_root = pc_fs_begin("/"); // every name below is resolved against this root

    on_http("/save", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = http_get_query(req, "name");
        const char *data = http_get_query(req, "data");
        if (!name || !*name || !data)
        {
            send_text(id, 400, "application/json", "{\"error\":\"name+data\"}");
            return;
        }
        bool ok = pc_fs_write_file(s_root, "", name, data, strlen(data));
        send_text(id, ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    on_http("/load", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = http_get_query(req, "name");
        if (!name || !*name)
        {
            send_text(id, 400, "text/plain", "name?");
            return;
        }
        char buf[512];
        long n = pc_fs_read_file(s_root, "", name, buf, sizeof(buf) - 1);
        if (n < 0)
        {
            send_text(id, 404, "text/plain", "not found");
            return;
        }
        buf[n] = '\0';
        send_text(id, 200, "text/plain", buf);
    });

    on_http("/size", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = http_get_query(req, "name");
        long n = (name && *name) ? pc_fs_size(s_root, "", name) : -1;
        char b[24];
        snprintf(b, sizeof(b), "%ld", n);
        send_text(id, 200, "text/plain", b);
    });

    on_http("/rm", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = http_get_query(req, "name");
        bool ok = (name && *name) && pc_fs_remove(s_root, "", name);
        send_text(id, ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    begin_http(80, NULL);
}

void loop()
{
    handle();
}
