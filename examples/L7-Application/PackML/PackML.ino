// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file PackML.ino
 * @brief PackML / OMAC machine state model over HTTP - a packaging machine that runs the ISA-TR88.00.02
 *        state machine and surfaces its PackTags so a line controller / HMI can observe and command it
 *        (PC_ENABLE_PACKML).
 *
 * services/machine_tool/packml is a pure, fixed-BSS state engine + owned PackTags service; this sketch drives it as a
 * simulated machine and exposes it over the library's HTTP server:
 *
 *   GET /packml            -> the live Status/Admin PackTags as JSON (StateCurrent + name, UnitMode,
 *                             MachSpeedActual, ProdProcessedCount / ProdDefectiveCount, the timers)
 *   GET /packml/<command>  -> apply a control command: reset | start | stop | hold | unhold | suspend |
 *                             unsuspend | abort | clear | complete   (returns the resulting state)
 *
 * The loop auto-advances the transient "acting" states (Starting -> Execute, Aborting -> Aborted, ...)
 * after a short dwell, the way a real machine's State-Complete would, and produces a unit every second
 * while in Execute. Point a browser at http://<device-ip>/packml, then drive it with the command routes.
 *
 * Build flags (platformio.ini):  build_flags = -DPC_ENABLE_PACKML=1
 */

#define PC_ENABLE_PACKML 1

#include "protocore.h" // library entry header (also sets the src/ include root)
#include "network_drivers/physical/physical.h"
#include "services/machine_tool/packml/packml.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static const uint32_t ACTING_DWELL_MS = 800; // how long a transient state "takes" in this simulation

// GET /packml -> the live PackTags as JSON.
static void handle_status(uint8_t slot, HttpReq *req)
{
    (void)req;
    PackMlStatus st;
    pc_packml_svc_status(&st);
    char body[256];
    int n = snprintf(body, sizeof(body),
                     "{\"stateCurrent\":%u,\"state\":\"%s\",\"unitMode\":%u,\"machSpeedActual\":%.1f,"
                     "\"prodProcessedCount\":%lu,\"prodDefectiveCount\":%lu,\"stateCurrentTimeMs\":%lu,"
                     "\"accTimeSinceResetMs\":%lu}",
                     (unsigned)st.state_current, pc_packml_state_name(st.state_current), (unsigned)st.unit_mode_current,
                     (double)st.mach_speed_actual, (unsigned long)st.prod_processed, (unsigned long)st.prod_defective,
                     (unsigned long)st.state_current_ms, (unsigned long)st.acc_time_since_reset_ms);
    if (n < 0)
    {
        n = 0;
    }
    send_text(slot, 200, "application/json", (const uint8_t *)body, (size_t)n);
}

// Apply a command and answer with the resulting state name.
static void apply(uint8_t slot, PackMlCommand cmd)
{
    bool ok = pc_packml_svc_command(cmd);
    char body[96];
    int n = snprintf(body, sizeof(body), "{\"accepted\":%s,\"state\":\"%s\"}", ok ? "true" : "false",
                     pc_packml_state_name(pc_packml_svc_state()));
    send_text(slot, ok ? 200 : 409, "application/json", (const uint8_t *)body, (size_t)(n < 0 ? 0 : n));
}

static void h_reset(uint8_t s, HttpReq *r)
{
    (void)r;
    apply(s, PackMlCommand::RESET);
}
static void h_start(uint8_t s, HttpReq *r)
{
    (void)r;
    apply(s, PackMlCommand::START);
}
static void h_stop(uint8_t s, HttpReq *r)
{
    (void)r;
    apply(s, PackMlCommand::STOP);
}
static void h_hold(uint8_t s, HttpReq *r)
{
    (void)r;
    apply(s, PackMlCommand::HOLD);
}
static void h_unhold(uint8_t s, HttpReq *r)
{
    (void)r;
    apply(s, PackMlCommand::UNHOLD);
}
static void h_suspend(uint8_t s, HttpReq *r)
{
    (void)r;
    apply(s, PackMlCommand::SUSPEND);
}
static void h_unsuspend(uint8_t s, HttpReq *r)
{
    (void)r;
    apply(s, PackMlCommand::UNSUSPEND);
}
static void h_abort(uint8_t s, HttpReq *r)
{
    (void)r;
    apply(s, PackMlCommand::ABORT);
}
static void h_clear(uint8_t s, HttpReq *r)
{
    (void)r;
    apply(s, PackMlCommand::CLEAR);
}

// End the production run: Execute -> Completing.
static void h_complete(uint8_t s, HttpReq *r)
{
    (void)r;
    bool ok = pc_packml_svc_complete_run();
    char body[96];
    int n = snprintf(body, sizeof(body), "{\"accepted\":%s,\"state\":\"%s\"}", ok ? "true" : "false",
                     pc_packml_state_name(pc_packml_svc_state()));
    send_text(s, ok ? 200 : 409, "application/json", (const uint8_t *)body, (size_t)(n < 0 ? 0 : n));
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip();
    Serial.printf("PackML machine at http://%u.%u.%u.%u/packml\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_packml_svc_init(PackMlMode::PRODUCING);
    pc_packml_svc_set_speed(120.0f); // 120 units/min commanded

    on_http("/packml", HTTP_GET, handle_status);
    on_http("/packml/reset", HTTP_GET, h_reset);
    on_http("/packml/start", HTTP_GET, h_start);
    on_http("/packml/stop", HTTP_GET, h_stop);
    on_http("/packml/hold", HTTP_GET, h_hold);
    on_http("/packml/unhold", HTTP_GET, h_unhold);
    on_http("/packml/suspend", HTTP_GET, h_suspend);
    on_http("/packml/unsuspend", HTTP_GET, h_unsuspend);
    on_http("/packml/abort", HTTP_GET, h_abort);
    on_http("/packml/clear", HTTP_GET, h_clear);
    on_http("/packml/complete", HTTP_GET, h_complete);
    begin_http(80);
}

void loop()
{
    handle();

    // Simulate the machine finishing each transient action: an acting state auto-advances after a dwell.
    static uint32_t acting_since = 0;
    if (pc_packml_is_acting(pc_packml_svc_state()))
    {
        if (acting_since == 0)
        {
            acting_since = millis();
        }
        else if (millis() - acting_since >= ACTING_DWELL_MS)
        {
            pc_packml_svc_state_complete();
            acting_since = 0;
        }
    }
    else
    {
        acting_since = 0;
    }

    // Produce one unit per second while executing (1-in-20 flagged defective, for the counters).
    static uint32_t last_unit = 0;
    if (pc_packml_svc_state() == PackMlState::EXECUTE && millis() - last_unit >= 1000)
    {
        last_unit = millis();
        static uint32_t made = 0;
        pc_packml_svc_count((++made % 20) == 0);
    }
}
