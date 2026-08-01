// HotSwapStorage - survive an SD card being pulled mid-write.
//
// A card is a connector, so it can leave while you are writing to it. The failure is quiet: the
// driver still reports a mounted volume, every write fails into nothing, and code that does not
// check carries on believing it has storage. Logs vanish, uploads truncate, and nothing says why.
//
// services/storage/hotswap makes that loud and recoverable:
//
//   ABSENT  --probe finds a card, mount ok-->  READY
//   READY   --N consecutive I/O errors------>  FAULTED   (unmounts immediately)
//   FAULTED --probe interval, remount ok---->  READY
//
// The rule for callers is two lines: gate on pc_hotswap_ready() before touching the filesystem,
// and report the outcome of every call with pc_hotswap_io(). That is what lets a run of failures
// mean "the card left" instead of scrolling past unnoticed.
//
// Why a *run* of errors and not one: a single failed write is not proof of removal (a transient bus
// error, a full volume), and tearing down a working mount over one error is its own bug. Any
// success resets the run, so noise never accumulates into a false removal.
//
// GET /storage  -> {"storage":"ready","mounts":1,"faults":0}
// GET /write    -> appends a line, reporting the outcome to the state machine
// GET /yank     -> unmounts underneath the app, so you can watch the fault + auto-recovery without
//                  physically pulling the card (the writes that follow really do fail)
//
// Build flags (whole build): PC_ENABLE_HOTSWAP=1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/storage/hotswap/hotswap.h"
#include "shared_primitives/mime.h"
#include <SD_MMC.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-password";
static const char *LOG_PATH = "/hotswap.log";

static uint32_t g_writes = 0;

// --- the three things the app owns: how to mount, unmount, and detect ------

static bool sd_mount(void *ctx)
{
    (void)ctx;
    return SD_MMC.begin();
}

static void sd_unmount(void *ctx)
{
    (void)ctx;
    SD_MMC.end(); // tolerates being called when already unmounted
}

// No card-detect pin wired here, so let the mount attempt be the detector. A board that has one
// should return its GPIO state instead - it is cheaper than a failed mount.
static pc_hotswap_present sd_present = nullptr;

static void on_state_change(StorageState from, StorageState to, void *ctx)
{
    (void)ctx;
    Serial.printf("storage: %s -> %s\n", pc_hotswap_state_name(from), pc_hotswap_state_name(to));
}

// --- routes ---------------------------------------------------------------

static void storage_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char json[96];
    if (pc_hotswap_json(json, sizeof(json)) == 0)
    {
        send_text(slot_id, 500, PC_MIME_JSON, "{}");
        return;
    }
    send_text(slot_id, 200, PC_MIME_JSON, json);
}

static void write_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    // The gate. Without it this write would go into a stale mount and be silently lost.
    if (!pc_hotswap_ready())
    {
        send_text(slot_id, 503, PC_MIME_TEXT_PLAIN, "storage not ready\n");
        return;
    }

    bool ok = false;
    fs::File f = SD_MMC.open(LOG_PATH, FILE_APPEND);
    if (f)
    {
        char line[64];
        int n = snprintf(line, sizeof(line), "write %u\n", (unsigned)++g_writes);
        ok = (n > 0) && (f.write((const uint8_t *)line, (size_t)n) == (size_t)n);
        f.close();
    }

    // Report it either way: successes are what keep a healthy volume from drifting toward a fault.
    pc_hotswap_io(ok);
    send_text(slot_id, ok ? 200 : 500, PC_MIME_TEXT_PLAIN, ok ? "ok\n" : "write failed\n");
}

// Pull the rug out from under the app without touching the hardware. Every write after this really
// does fail, so the fault path runs for real rather than being simulated.
static void yank_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    SD_MMC.end();
    send_text(slot_id, 200, PC_MIME_TEXT_PLAIN, "unmounted - now hit /write a few times\n");
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    pc_hotswap_set_event_cb(on_state_change);
    pc_hotswap_begin(sd_mount, sd_unmount, sd_present, nullptr);
    pc_hotswap_poll(); // first poll mounts a card that is already in the slot
    Serial.printf("storage at boot: %s\n", pc_hotswap_state_name(pc_hotswap_state()));

    init_wifi_physical(WIFI_SSID, WIFI_PASS);
    while (!wifi_ready())
    {
        delay(250);
    }

    on_http("/storage", HttpMethod::HTTP_GET, storage_handler);
    on_http("/write", HttpMethod::HTTP_GET, write_handler);
    on_http("/yank", HttpMethod::HTTP_GET, yank_handler);
    begin_http(80);

    uint32_t ip = pc_net_egress_ip();
    Serial.printf("http://%u.%u.%u.%u/storage\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    handle();
    pc_hotswap_poll(); // rate-limited internally, so this is cheap to call every pass
}
