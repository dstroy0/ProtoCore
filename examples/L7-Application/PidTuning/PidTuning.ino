// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file PidTuning.ino
 * @brief PID control loop + the offline-tuning workflow (PC_ENABLE_CONTROL).
 *
 * Shows the whole services/system/control tuning loop end to end:
 *   1. run a PID (`pid_update`) at a fixed rate against a plant,
 *   2. record each step (setpoint / measurement / output) into a buffer,
 *   3. serve the run as a CSV log at GET /log.csv, and
 *   4. feed that log to tools/pid_tune.py, which identifies the plant, simulates
 *      candidate Kp/Ki/Kd, and suggests better gains - then you edit KP/KI/KD
 *      below and re-flash.
 *
 * So it runs on ANY ESP32 with no extra hardware, the "plant" here is a simulated
 * first-order process. To tune a REAL loop, replace plant_read()/plant_write()
 * with your sensor read + actuator write (an ADC + an LEDC PWM, a dshot ESC, a
 * services/fieldbus/cia402 drive's actual velocity + target, ...); everything else is the same.
 *
 *   curl http://<ip>/log.csv > run.csv
 *   python tools/pid_tune.py run.csv --autotune --png tune.png
 *
 * Build flag (platformio.ini):  build_flags = -DPC_ENABLE_CONTROL=1
 */

#define PC_ENABLE_CONTROL 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/system/control/control.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// --- the gains you tune (edit these from pid_tune.py's suggestion, then re-flash) ---
static const float KP = 1.5f;
static const float KI = 4.0f;
static const float KD = 0.05f;
static const float OUT_MIN = 0.0f;
static const float OUT_MAX = 50.0f;
static const uint32_t PERIOD_MS = 20; // 50 Hz control rate

static Pid pid;

// Recorded run (BSS): the first LOG_N control steps after boot / GET /reset.
#define LOG_N 400
static float g_t[LOG_N];
static float g_sp[LOG_N];
static float g_meas[LOG_N];
static float g_out[LOG_N];
static float g_dt[LOG_N];
static int g_count = 0;

// --- the plant. Replace these two with your real sensor + actuator to tune live hardware. ---
static float sim_y = 0.0f;
static float plant_read()
{
    return sim_y; // e.g. analogRead(PIN) scaled to engineering units
}
static void plant_write(float u, float dt)
{
    const float K = 2.0f, tau = 0.5f;    // first-order lag: y' = (K*u - y)/tau
    sim_y += (K * u - sim_y) / tau * dt; // e.g. ledcWrite(CH, (int)u)
}

static float setpoint_now(uint32_t ms)
{
    return ((ms / 2000) & 1) ? 10.0f : 0.0f; // a 0.25 Hz square wave -> a step to tune against
}

// --- CSV log source for GET /log.csv (self-describing: a `# pid ...` metadata line the tuner
//     reads for the baseline gains + limits, then the CONTROL_LOG_HEADER columns, then rows). ---
struct LogCtx
{
    int i;
};
static size_t log_source(uint8_t *buf, size_t cap, void *vctx)
{
    LogCtx *c = (LogCtx *)vctx;
    int s = c->i++;
    if (s == 0)
    {
        return (size_t)snprintf((char *)buf, cap, "# pid kp=%.4f ki=%.4f kd=%.4f out_min=%.1f out_max=%.1f dt=%.4f\n",
                                KP, KI, KD, OUT_MIN, OUT_MAX, PERIOD_MS / 1000.0f);
    }
    if (s == 1)
    {
        return (size_t)snprintf((char *)buf, cap, "%s\n", CONTROL_LOG_HEADER);
    }
    int idx = s - 2;
    if (idx >= g_count)
    {
        return 0; // done
    }
    return (size_t)snprintf((char *)buf, cap, "%.4f,%.4f,%.4f,%.4f,%.4f\n", g_t[idx], g_sp[idx], g_meas[idx],
                            g_out[idx], g_dt[idx]);
}

static LogCtx s_log_ctx; // must outlive the chunked response

void handle_log(uint8_t slot, HttpReq *)
{
    s_log_ctx.i = 0;
    send_chunked(slot, 200, "text/csv", log_source, &s_log_ctx);
}

void handle_reset(uint8_t slot, HttpReq *)
{
    g_count = 0;
    pid_reset(&pid);
    sim_y = 0.0f;
    send_text(slot, 200, "text/plain", "recording restarted\n");
}

void handle_root(uint8_t slot, HttpReq *)
{
    char page[512];
    snprintf(page, sizeof(page),
             "PID tuning demo\n\n"
             "gains: Kp=%.4f Ki=%.4f Kd=%.4f  (rate %lu Hz)\n"
             "state: setpoint=%.3f measurement=%.3f output=%.3f  samples=%d/%d\n\n"
             "tune it:\n"
             "  curl http://<ip>/log.csv > run.csv\n"
             "  python tools/pid_tune.py run.csv --autotune --png tune.png\n"
             "then edit KP/KI/KD, re-flash, and GET /reset to record a fresh run.\n",
             KP, KI, KD, 1000UL / PERIOD_MS, g_count ? g_sp[g_count - 1] : 0.0f, g_count ? g_meas[g_count - 1] : 0.0f,
             g_count ? g_out[g_count - 1] : 0.0f, g_count, LOG_N);
    send_text(slot, 200, "text/plain", page);
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

    pid_init(&pid, KP, KI, KD);
    pid_set_output_limits(&pid, OUT_MIN, OUT_MAX);
    pid_set_rate(&pid, PERIOD_MS * 0.001f); // fixed rate -> pid_update_fixed() skips the /dt divide

    on_http("/", HttpMethod::HTTP_GET, handle_root);
    on_http("/log.csv", HttpMethod::HTTP_GET, handle_log);
    on_http("/reset", HttpMethod::HTTP_GET, handle_reset);
    begin_http(80);
    Serial.println("GET /log.csv to capture the run for tools/pid_tune.py");
}

void loop()
{
    handle();

    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < PERIOD_MS)
    {
        return;
    }
    float dt = PERIOD_MS * 0.001f; // fixed rate; * 0.001f, not / 1000.0f (a float divide is a __divsf3 call)
    last = now;

    float sp = setpoint_now(now);
    float meas = plant_read();
    float u = pid_update_fixed(&pid, sp, meas); // no per-tick /dt divide (rate cached in setup)
    plant_write(u, dt);

    if (g_count < LOG_N)
    {
        int i = g_count++;
        g_t[i] = now / 1000.0f;
        g_sp[i] = sp;
        g_meas[i] = meas;
        g_out[i] = u;
        g_dt[i] = dt;
    }
}
