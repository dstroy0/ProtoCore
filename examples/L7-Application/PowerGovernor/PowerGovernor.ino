// PowerGovernor - clock the SoC to what the work, the die temperature, and the supply allow.
//
// network_drivers/physical/radio_power owns the radio and server/sleep_sched decides how long to sleep. Neither
// owns the SoC itself, which is where the rest of the power budget goes. This governor answers one
// question every tick: what should the CPU clock be right now.
//
//   scaling    idle work runs at the floor - spinning a 240 MHz core to poll an idle socket is the
//              easiest power win on this part
//   thermal    a hot die clocks down, and the restore threshold is LOWER than the throttle one, so
//              a part sitting exactly at the limit does not oscillate between full speed and floor
//   brownout   a board that just browned out comes up at the floor for a settle window instead of
//              slamming back into the load that collapsed its supply and boot-looping
//   gating     a build with no BLE can hand back the Bluetooth power domain outright
//
// Precedence is deliberate: brownout beats thermal beats load. A board that cannot hold its supply
// must not be clocked up just because it is busy, and neither must a hot one.
//
// GET /power  -> {"cpu_mhz":80,"throttled":false,"recovering":false,"temp_c":48}
// GET /busy   -> burns CPU for a moment so you can watch the clock climb and settle back
//
// Build flags (whole build): PROTOCORE_ENABLE_POWER_MGMT=1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "server/clock/clock.h" // Clock.millis - the library's monotonic source
#include "server/core/power_mgmt/power_mgmt.h"
#include "shared/mime/mime.h"

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-password";

static PowerCfg g_cfg;
static PowerPlan g_plan;
static int16_t g_temp = 0;

// A crude load signal: how much of the last window the loop spent doing work rather than idling.
// A real app can feed anything it likes here - queue depth, request rate, a duty counter.
static uint32_t g_busy_until = 0;

// The library's monotonic milliseconds: Clock.millis() leaves its reading in Clock.ms.
static uint32_t now_ms(void)
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

// This boot followed a brownout (latched by the library, so it reads the same all window).
static bool brownout_boot(void)
{
    Power.brownout(protocore_power_mgmt_span());
    return PowerV.ok;
}

// The die temperature, INT16_MIN when the part has no sensor.
static int16_t die_temp_c(void)
{
    Power.die_temp(protocore_power_mgmt_span());
    return PowerV.temp_c;
}

static uint8_t sample_load_pct(void)
{
    return (now_ms() < g_busy_until) ? 100 : 0;
}

static void power_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char json[128];
    PowerV.out_args.plan = &g_plan;
    PowerV.out_args.temp_c = g_temp;
    PowerV.out_args.out = json;
    PowerV.out_args.cap = sizeof(json);
    Power.json(protocore_power_mgmt_span());
    if (PowerV.n == 0)
    {
        send_text(slot_id, 500, PROTOCORE_MIME_JSON, "{}");
        return;
    }
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, json);
}

static void busy_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    g_busy_until = now_ms() + 5000; // report "busy" for 5 s
    send_text(slot_id, 200, PROTOCORE_MIME_TEXT_PLAIN, "busy for 5s - poll /power to watch the clock\n");
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    PowerV.cfg_out = &g_cfg;
    Power.defaults(protocore_power_mgmt_span());

    // Reset reason is read once and latched, so this reads the same through the whole window.
    if (brownout_boot())
    {
        Serial.println("last reset was a BROWNOUT - coming up at the floor clock");
    }

    // A build with no BLE is holding the Bluetooth domain for nothing.
    Power.gate_bt(protocore_power_mgmt_span());
    if (PowerV.ok)
    {
        Serial.println("released the Bluetooth power domain");
    }

    Power.cpu_mhz(protocore_power_mgmt_span());
    uint16_t boot_mhz = PowerV.mhz;
    Serial.printf("boot clock: %u MHz, die %d C\n", (unsigned)boot_mhz, (int)die_temp_c());

    PhysicalV.wifi.ssid = WIFI_SSID;
    PhysicalV.wifi.password = WIFI_PASS;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }

    on_http("/power", HTTP_GET, power_handler);
    on_http("/busy", HTTP_GET, busy_handler);
    begin_http(80, NULL);

    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32;
    Serial.printf("http://%u.%u.%u.%u/power\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    handle();

    static uint32_t next = 0;
    uint32_t now = now_ms();
    if ((int32_t)(now - next) < 0)
    {
        return;
    }
    next = now + 500;

    g_temp = die_temp_c();
    // The previous plan's throttle flag goes back in: that feedback is what gives the thermal
    // decision its hysteresis. Passing false here would re-create the oscillation it exists to stop.
    bool brownout = brownout_boot();
    PowerV.plan_args.cfg = &g_cfg;
    PowerV.plan_args.load_pct = sample_load_pct();
    PowerV.plan_args.temp_c = g_temp;
    PowerV.plan_args.brownout_boot = brownout;
    PowerV.plan_args.since_boot_ms = now;
    PowerV.plan_args.was_throttled = g_plan.throttled;
    Power.decide(protocore_power_mgmt_span());
    PowerPlan p = PowerV.plan;
    PowerV.out_args.plan = &p;
    Power.apply(protocore_power_mgmt_span());
    if (PowerV.ok)
    {
        Serial.printf("clock -> %u MHz (throttled=%d recovering=%d die=%d C)\n", (unsigned)p.cpu_mhz, (int)p.throttled,
                      (int)p.recovering, (int)g_temp);
    }
    g_plan = p;
}
