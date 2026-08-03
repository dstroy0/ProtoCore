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
// Build flags (whole build): PC_ENABLE_POWER_MGMT=1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "server/clock/clock.h" // pc_millis - the library's monotonic source
#include "services/system/power_mgmt/power_mgmt.h"
#include "shared_primitives/mime.h"

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-password";

static PowerCfg g_cfg;
static PowerPlan g_plan;
static int16_t g_temp = 0;

// A crude load signal: how much of the last window the loop spent doing work rather than idling.
// A real app can feed anything it likes here - queue depth, request rate, a duty counter.
static uint32_t g_busy_until = 0;

static uint8_t sample_load_pct(void)
{
    return (pc_millis() < g_busy_until) ? 100 : 0;
}

static void power_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char json[128];
    if (pc_power_json(&g_plan, g_temp, json, sizeof(json)) == 0)
    {
        send_text(slot_id, 500, PC_MIME_JSON, "{}");
        return;
    }
    send_text(slot_id, 200, PC_MIME_JSON, json);
}

static void busy_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    g_busy_until = pc_millis() + 5000; // report "busy" for 5 s
    send_text(slot_id, 200, PC_MIME_TEXT_PLAIN, "busy for 5s - poll /power to watch the clock\n");
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    pc_power_cfg_defaults(&g_cfg);

    // Reset reason is read once and latched, so this reads the same through the whole window.
    if (pc_power_brownout_boot())
    {
        Serial.println("last reset was a BROWNOUT - coming up at the floor clock");
    }

    // A build with no BLE is holding the Bluetooth domain for nothing.
    if (pc_power_gate_bt())
    {
        Serial.println("released the Bluetooth power domain");
    }

    Serial.printf("boot clock: %u MHz, die %d C\n", (unsigned)pc_power_cpu_mhz(), (int)pc_power_temp_c());

    init_wifi_physical(WIFI_SSID, WIFI_PASS);
    while (!wifi_ready())
    {
        delay(250);
    }

    on_http("/power", HttpMethod::HTTP_GET, power_handler);
    on_http("/busy", HttpMethod::HTTP_GET, busy_handler);
    begin_http(80);

    uint32_t ip = pc_net_egress_ip();
    Serial.printf("http://%u.%u.%u.%u/power\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    handle();

    static uint32_t next = 0;
    uint32_t now = pc_millis();
    if ((int32_t)(now - next) < 0)
    {
        return;
    }
    next = now + 500;

    g_temp = pc_power_temp_c();
    // The previous plan's throttle flag goes back in: that feedback is what gives the thermal
    // decision its hysteresis. Passing false here would re-create the oscillation it exists to stop.
    PowerPlan p = pc_power_plan(&g_cfg, sample_load_pct(), g_temp, pc_power_brownout_boot(), now, g_plan.throttled);
    if (pc_power_apply(&p))
    {
        Serial.printf("clock -> %u MHz (throttled=%d recovering=%d die=%d C)\n", (unsigned)p.cpu_mhz, (int)p.throttled,
                      (int)p.recovering, (int)g_temp);
    }
    g_plan = p;
}
