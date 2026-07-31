// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Vfs.ino
 * @brief Unified VFS over a real filesystem (PC_ENABLE_VFS).
 *
 * The same pc_vfs_* API drives a RAM pool in tests and a real filesystem on
 * the device. Here it is mounted on LittleFS, so writes persist across reboots:
 *
 *   GET /save?name=greeting&data=hello   -> stores /greeting on flash
 *   GET /load?name=greeting              -> returns its contents
 *   GET /size?name=greeting              -> byte count (-1 if absent)
 *   GET /rm?name=greeting                -> deletes it
 *
 * To run entirely in RAM instead (no flash, deterministic), mount the built-in
 * backend: `pc_vfs_mount(pc_vfs_ram());` - every endpoint below is
 * unchanged. That is the whole point: features target one API, the application
 * chooses the medium.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_VFS=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_VFS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/storage/vfs/vfs.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Resolve a query "name" to a leading-slash path in a small static buffer.
static const char *path_of(HttpReq *req, char *out, size_t cap)
{
    const char *name = http_get_query(req, "name");
    if (!name || !*name)
    {
        return nullptr;
    }
    snprintf(out, cap, "/%s", name);
    return out;
}

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    LittleFS.begin(true); // format on first use
    pc_vfs_mount(pc_vfs_fs(&LittleFS));

    server.on("/save", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        char path[PC_VFS_NAME_MAX];
        const char *data = http_get_query(req, "data");
        if (!path_of(req, path, sizeof(path)) || !data)
        {
            server.send(id, 400, "application/json", "{\"error\":\"name+data\"}");
            return;
        }
        bool ok = pc_vfs_write_file(path, data, strlen(data));
        server.send(id, ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    server.on("/load", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        char path[PC_VFS_NAME_MAX];
        if (!path_of(req, path, sizeof(path)))
        {
            server.send(id, 400, "text/plain", "name?");
            return;
        }
        char buf[512];
        long n = pc_vfs_read_file(path, buf, sizeof(buf) - 1);
        if (n < 0)
        {
            server.send(id, 404, "text/plain", "not found");
            return;
        }
        buf[n] = '\0';
        server.send(id, 200, "text/plain", buf);
    });

    server.on("/size", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        char path[PC_VFS_NAME_MAX];
        long n = path_of(req, path, sizeof(path)) ? pc_vfs_size(path) : -1;
        char b[24];
        snprintf(b, sizeof(b), "%ld", n);
        server.send(id, 200, "text/plain", b);
    });

    server.on("/rm", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        char path[PC_VFS_NAME_MAX];
        bool ok = path_of(req, path, sizeof(path)) && pc_vfs_remove(path);
        server.send(id, ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    server.begin(80);
}

void loop()
{
    server.handle();
}
