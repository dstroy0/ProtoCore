// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Mnt.ino
 * @brief Mounted storage over a real filesystem (PROTOCORE_ENABLE_MNT).
 *
 * The same Fs API drives a RAM pool in tests and a real filesystem on the
 * device. Here it is mounted on LittleFS, so writes persist across reboots:
 *
 *   GET /save?name=greeting&data=hello   -> stores /greeting on flash
 *   GET /load?name=greeting              -> returns its contents
 *   GET /size?name=greeting              -> byte count (-1 if absent)
 *   GET /rm?name=greeting                -> deletes it
 *
 * Note what the handlers do NOT do: they never build a path. Fs.begin binds
 * the root once and hands back a handle; every call below passes that handle, an
 * empty dir, and the client's name straight through - the accessor joins the three
 * and refuses any `..` before storage is touched. A query of `name=../../secret`
 * is rejected without this sketch containing a single line about it.
 *
 * To run entirely in RAM instead (no flash, deterministic), mount the built-in
 * backend: `MntV.args.backend = MntRam.backend(work); Mnt.mount(work);` - every endpoint below is unchanged.
 * That is the whole point: features target one API, the application chooses the
 * medium.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_MNT=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_MNT 1

#include "protocore.h"
#include "test/core_setup/hal/esp/esp_mnt_fs.h"
#include "network_drivers/physical/physical/physical.h"
#include "server/storage/filesystem/filesystem.h"
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// The root Fs.begin binds, at file scope because the handlers below are captureless lambdas.
static int s_root = -1;

static uint8_t mnt_work[16]; // the borrow a Mnt entry takes; Mnt never reads it

// Point Fs at one client-named entry under the bound root (empty dir: the accessor joins them).
static void fs_name(const char *name)
{
    Fs.path.root = s_root;
    Fs.path.dir = "";
    Fs.path.name = name;
}

static const char *query(HttpReq *req, const char *key)
{
    return HttpParser.get_query(protocore_http_parser_span(), req, key);
}

void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    LittleFS.begin(true); // format on first use
    MntV.args.backend = protocore_mnt_fs(&LittleFS);
    Mnt.mount(mnt_work);
    Fs.mount = "/";
    Fs.begin(protocore_filesystem_span());
    s_root = Fs.i32; // every name below is resolved against this root

    on_http("/save", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = query(req, "name");
        const char *data = query(req, "data");
        if (!name || !*name || !data)
        {
            send_text(id, 400, "application/json", "{\"error\":\"name+data\"}");
            return;
        }
        fs_name(name);
        Fs.io.wbuf = data;
        Fs.io.n = strlen(data);
        Fs.write_file(protocore_filesystem_span());
        bool ok = Fs.ok;
        send_text(id, ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    on_http("/load", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = query(req, "name");
        if (!name || !*name)
        {
            send_text(id, 400, "text/plain", "name?");
            return;
        }
        char buf[512];
        fs_name(name);
        Fs.io.buf = buf;
        Fs.io.n = sizeof(buf) - 1;
        Fs.read_file(protocore_filesystem_span());
        long n = Fs.len;
        if (n < 0)
        {
            send_text(id, 404, "text/plain", "not found");
            return;
        }
        buf[n] = '\0';
        send_text(id, 200, "text/plain", buf);
    });

    on_http("/size", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = query(req, "name");
        long n = -1;
        if (name && *name)
        {
            fs_name(name);
            Fs.size(protocore_filesystem_span());
            n = Fs.len;
        }
        char b[24];
        snprintf(b, sizeof(b), "%ld", n);
        send_text(id, 200, "text/plain", b);
    });

    on_http("/rm", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *name = query(req, "name");
        bool ok = false;
        if (name && *name)
        {
            fs_name(name);
            Fs.remove(protocore_filesystem_span());
            ok = Fs.ok;
        }
        send_text(id, ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    begin_http(80, NULL);
}

void loop()
{
    handle();
}
