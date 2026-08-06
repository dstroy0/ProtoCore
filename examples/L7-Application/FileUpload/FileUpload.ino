// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file FileUpload.ino
 * @brief Streaming file upload: POST a body straight into a LittleFS file.
 *
 * The request body is streamed into a file in FILE_CHUNK_SIZE pieces via the
 * parser's streaming-body hook (the same mechanism OTA uses), so an upload never
 * has to fit in RAM. A GET route serves the stored file back so you can verify.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl --data-binary @somefile.bin http://<ip>/upload     # 200 OK <n> bytes
 *   curl http://<ip>/file > roundtrip.bin                   # read it back
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_UPLOAD=1 -DMAX_CONNS=4
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.) The upload
 * sink shares the parser streaming hook with OTA - enable one or the other, not both.
 *
 * Enabling upload raises RX_BUF_SIZE to the streaming floor of 8192 (a full TCP
 * receive window) automatically - a smaller ring stalls a large upload into the
 * idle-timeout reset (HW-measured: 2048 resets a 64 KB upload; 8192 round-trips
 * 256 KB byte-exact). That ring is real DRAM per connection, so MAX_CONNS is
 * dialed to 4 to fit the classic ESP32; a PSRAM/large-SRAM board can raise it.
 */

#define PC_ENABLE_UPLOAD 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/application/upload_service/upload_service.h"
#include "board_drivers/hal/esp/esp_mnt_fs.h" // pc_mnt_fs(): bind an Arduino FS to the storage seam
#include "server/filesystem/mnt.h"            // pc_mnt_mount()
#include <LittleFS.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *DEST = "/uploaded.bin";


void setup()
{
    Serial.begin(115200);

    if (!LittleFS.begin(true)) // format on first use
    {
        Serial.println("LittleFS mount failed");
    }

    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Mount LittleFS through the storage seam; the upload service writes to whatever is mounted.
    pc_mnt_mount(pc_mnt_fs(&LittleFS));

    // POST /upload -> stream the body into DEST on the mounted store.
    pc_upload_begin("/upload", DEST);

    // GET /file -> serve the stored file back.
    on_http("/file", HTTP_GET,
              [](uint8_t id, HttpReq *) { serve_file(id, LittleFS, DEST, "application/octet-stream"); });

    int32_t result = begin_http(80, NULL);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
    }
    else
    {
        Serial.println("Upload server on :80 (curl --data-binary @file http://<ip>/upload)");
    }
}

void loop()
{
    handle();
}
