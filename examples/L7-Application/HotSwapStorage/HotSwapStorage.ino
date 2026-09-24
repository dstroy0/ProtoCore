// HotSwapStorage - survive an SD card being pulled mid-write.
//
// A card is a connector, so it can leave while you are writing to it. The failure is quiet: the
// driver still reports a mounted volume, every write fails into nothing, and code that does not
// check carries on believing it has storage. Logs vanish, uploads truncate, and nothing says why.
//
// server/storage/hotswap makes that loud and recoverable:
//
//   ABSENT  --probe finds a card, mount ok-->  READY
//   READY   --N consecutive I/O errors------>  FAULTED   (unmounts immediately)
//   FAULTED --probe interval, remount ok---->  READY
//
// The rule for callers is two lines: gate on protocore_hotswap_ready() before touching the filesystem,
// and report the outcome of every call with protocore_hotswap_io(). That is what lets a run of failures
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
// Build flags (whole build): PROTOCORE_ENABLE_HOTSWAP=1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "server/storage/hotswap/hotswap.h"
#include "shared/mime/mime.h"
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
static protocore_hotswap_present sd_present = nullptr;

// Every Hotswap entry reads its operands from HotswapV and writes its outcome back there.
static const char *state_name(StorageState s)
{
    HotswapV.state_name_args.s = s;
    Hotswap.state_name(protocore_hotswap_span());
    return HotswapV.text;
}

static void on_state_change(StorageState from, StorageState to, void *ctx)
{
    (void)ctx;
    const char *from_name = state_name(from);
    Serial.printf("storage: %s -> %s\n", from_name, state_name(to));
}

// --- routes ---------------------------------------------------------------

static void storage_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char json[96];
    HotswapV.json_args.out = json;
    HotswapV.json_args.cap = sizeof(json);
    Hotswap.json(protocore_hotswap_span());
    if (HotswapV.n == 0)
    {
        send_text(slot_id, 500, PROTOCORE_MIME_JSON, "{}");
        return;
    }
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, json);
}

static void write_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    // The gate. Without it this write would go into a stale mount and be silently lost.
    Hotswap.ready(protocore_hotswap_span());
    if (!HotswapV.ok)
    {
        send_text(slot_id, 503, PROTOCORE_MIME_TEXT_PLAIN, "storage not ready\n");
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
    HotswapV.io_args.ok = ok;
    Hotswap.io(protocore_hotswap_span());
    send_text(slot_id, ok ? 200 : 500, PROTOCORE_MIME_TEXT_PLAIN, ok ? "ok\n" : "write failed\n");
}

// Pull the rug out from under the app without touching the hardware. Every write after this really
// does fail, so the fault path runs for real rather than being simulated.
static void yank_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    SD_MMC.end();
    send_text(slot_id, 200, PROTOCORE_MIME_TEXT_PLAIN, "unmounted - now hit /write a few times\n");
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    HotswapV.set_event_cb_args.cb = on_state_change;
    Hotswap.set_event_cb(protocore_hotswap_span());
    HotswapV.begin_args.mount = sd_mount;
    HotswapV.begin_args.unmount = sd_unmount;
    HotswapV.begin_args.present = sd_present;
    HotswapV.begin_args.ctx = nullptr;
    Hotswap.begin(protocore_hotswap_span());
    Hotswap.poll(protocore_hotswap_span()); // first poll mounts a card that is already in the slot
    Hotswap.state(protocore_hotswap_span());
    Serial.printf("storage at boot: %s\n", state_name(HotswapV.value));

    PhysicalV.wifi.ssid = WIFI_SSID;
    PhysicalV.wifi.password = WIFI_PASS;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }

    on_http("/storage", HTTP_GET, storage_handler);
    on_http("/write", HTTP_GET, write_handler);
    on_http("/yank", HTTP_GET, yank_handler);
    begin_http(80, NULL);

    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32;
    Serial.printf("http://%u.%u.%u.%u/storage\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    handle();
    Hotswap.poll(protocore_hotswap_span()); // rate-limited internally, so this is cheap to call every pass
}
