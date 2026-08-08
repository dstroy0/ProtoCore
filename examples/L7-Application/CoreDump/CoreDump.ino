// CoreDump - recover a crash after the reboot that erased the evidence.
//
// A panic prints a Guru Meditation dump to a console nobody is watching, then reboots. ESP-IDF also
// writes a core dump to a flash partition, and that survives. So on the next boot this device can:
//
//   1. notice a dump is waiting        pc_exc_coredump_present()
//   2. say what crashed                pc_exc_coredump_summary()  -> the same ExcInfo/JSON the
//                                      live /exception panel already renders
//   3. put it somewhere durable        pc_exc_coredump_save(SD, "/crash.bin")  (a filesystem)
//                                      pc_ftp_store(...)                       (off the device)
//   4. clear the partition             pc_exc_coredump_erase()    so the next crash has room
//
// Step 3 shows both offload transports. They share one seam: pc_exc_coredump_read() pulls the
// image in chunks, so the FTP uploader streams straight out of flash and neither owner knows about
// the other. A dump that only ever reaches the device's own SD card is still lost with the device;
// FTP is what gets it to a machine you can run esp-coredump on.
//
// The saved file is the RAW core-dump image (ESP-IDF flash format: a 24-byte header, then an
// ET_CORE ELF at offset 24, then a checksum) - so pass it to the host tool as raw, not elf:
//     esp-coredump info_corefile -c crash.bin -t raw build/firmware.elf
// (`dd bs=1 skip=24` would carve out a plain ELF if you want one.)
//
// On Xtensa (ESP32/S2/S3) the summary carries a real backtrace. On RISC-V (C3/C6/H2/P4) it carries a
// stack dump instead - unwinding needs DWARF off-device, which is exactly why step 3 matters there.
//
// GET /exception  -> the decoded crash as JSON (or {} when there is nothing stored)
// GET /crash      -> triggers a null dereference, so you can watch the whole cycle
//
// Requires a `coredump` partition (the default Arduino partition tables have one).
// Build flags (whole build): PC_ENABLE_EXC_DECODER=1 PC_ENABLE_FTP=1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/file_transfer/ftp/ftp_session.h"
#include "server/exc_decoder.h"
#include "shared_primitives/log.h"
#include "shared_primitives/mime.h"
#include <SD_MMC.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-password";
static const char *DUMP_PATH = "/crash.bin"; // raw image, not a bare ELF - see the note above

// Where to ship the dump. Leave the host empty to skip the FTP offload and keep the SD copy only.
static const char *FTP_HOST = "";
static const uint16_t FTP_PORT = 0; // 0 = the default 21
static const char *FTP_USER = "pc";
static const char *FTP_PASS = "pc";

static char g_last_json[512] = "{}"; // the recovered crash, rendered once at boot
static bool g_saved = false;
static bool g_uploaded = false;

// Send the library's own log lines to the console. Build with -DPC_LOG_LEVEL=PC_LOG_LEVEL_DEBUG
// to watch the FTP conversation step by step; at the default level these lines are not in the
// binary at all, so leaving the sink registered costs nothing.
static void serial_log_sink(uint8_t level, const char *line)
{
    static const char letters[] = "DIWE";
    Serial.printf("[%c] %s\n", level < sizeof(letters) - 1 ? letters[level] : '?', line);
}

// Pull the image straight out of the core-dump partition for the uploader. This is the whole
// coupling between the two: no temp file, no buffer holding the dump.
static size_t coredump_source(void *ctx, size_t offset, uint8_t *buf, size_t cap)
{
    (void)ctx;
    return pc_exc_coredump_read(offset, buf, cap) ? cap : 0;
}

static void exception_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, PC_MIME_JSON, g_last_json);
}

// Deliberately crash so the cycle can be observed end to end.
static void crash_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, PC_MIME_TEXT_PLAIN, "crashing now - reconnect after the reboot\n");
    delay(200); // let the response flush before the panic
    volatile int *p = (int *)0;
    *p = 42;
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    pc_log_set_sink(serial_log_sink);

    bool sd = SD_MMC.begin();
    Serial.printf("SD: %s\n", sd ? "mounted" : "FAILED");

    // Recover anything the previous boot left behind, before doing anything else. Decoding and the
    // SD copy need no network, so they happen first - a dump survives even if the radio never joins.
    ExcCoreDump img;
    bool have_dump = pc_exc_coredump_present(&img);
    if (have_dump)
    {
        Serial.printf("core dump present: %u bytes @ 0x%08X\n", (unsigned)img.size, (unsigned)img.addr);

        ExcInfo info;
        if (pc_exc_coredump_summary(&info))
        {
            pc_exc_json(&info, g_last_json, sizeof(g_last_json));
            Serial.printf("crash: %s\n", g_last_json);
        }

        if (sd)
        {
            g_saved = pc_exc_coredump_save(SD_MMC, DUMP_PATH);
            Serial.printf("SD copy: %s\n", g_saved ? DUMP_PATH : "FAILED");
        }
    }
    else
    {
        Serial.println("no core dump stored (clean boot)");
    }

    Physical.wifi->init(WIFI_SSID, WIFI_PASS);
    while (!Physical.wifi->ready())
    {
        delay(250);
    }

    // Now that there is a network, get the dump off the device entirely.
    if (have_dump && FTP_HOST[0] != '\0')
    {
        FtpTarget target = {FTP_HOST, FTP_PORT, FTP_USER, FTP_PASS};
        g_uploaded = pc_ftp_store(&target, DUMP_PATH, img.size, coredump_source, nullptr);
        Serial.printf("FTP offload: %s\n", g_uploaded ? "ok" : "FAILED");
    }

    // Erase only once the image is somewhere else. If every configured offload failed, the dump
    // stays in flash so the next boot can try again rather than losing the crash for good.
    if (have_dump && (g_saved || g_uploaded))
    {
        pc_exc_coredump_erase();
        Serial.println("core-dump partition erased");
    }
    else if (have_dump)
    {
        Serial.println("no offload succeeded - leaving the dump in flash for the next attempt");
    }

    on_http("/exception", HTTP_GET, exception_handler);
    on_http("/crash", HTTP_GET, crash_handler);
    if (sd)
    {
        serve_static("/files/", SD_MMC, "/"); // download the saved dump
    }
    begin_http(80, NULL);

    uint32_t ip = Physical.link->egress_ip();
    Serial.printf("http://%u.%u.%u.%u/exception  (sd=%s ftp=%s)\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF), g_saved ? "yes" : "no",
                  g_uploaded ? "yes" : "no");
}

void loop()
{
    handle();
}
