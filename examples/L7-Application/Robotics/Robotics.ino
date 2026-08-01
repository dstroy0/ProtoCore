// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Robotics.ino
 * @brief OPC UA for Robotics (OPC 40010-1) MotionDeviceSystem model (PC_ENABLE_ROBOTICS).
 *
 * Turns the board into an OPC UA for Robotics server: it exposes the standard MotionDeviceSystem
 * information model (MotionDevices - MotionDevice / ParameterSet / Axes, Controllers - Controller /
 * Software, SafetyStates - SafetyState) over OPC UA on TCP/4840, served straight out of a
 * RoboticsMotionDeviceSystem struct you refresh in loop(). Any OPC UA client (UaExpert, python asyncua,
 * open62541) browses the MotionDeviceSystem and reads live values by their standard BrowseNames - the
 * same shape across robot vendors.
 *
 *   pc_robotics_install(&mds)             -> registers the OPC UA Browse + Read resolvers
 *   listen(4840, PROTO_OPCUA)       -> the OPC UA / robotics endpoint
 *
 * Builds on example OpcUa (the OPC UA Binary server); robotics is the MotionDevice model on top - the
 * twin of example Umati (machine tools). The HTTP server on :80 runs alongside on the same event loop.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_OPCUA=1 -DPC_ENABLE_ROBOTICS=1
 * (Arduino IDE: already set for you in the build_opt.h beside this sketch.)
 */

#define PC_ENABLE_OPCUA 1
#define PC_ENABLE_ROBOTICS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/machine_tool/robotics/robotics.h"
#include <math.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// The MotionDeviceSystem the robotics server exposes. Own it statically and refresh its live fields each
// loop from your real robot I/O; the resolvers read straight out of it (no copy).
static RoboticsMotionDeviceSystem mds;

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

    // --- MotionDevice identity (static): OPC 40010-1 MotionDeviceType ---
    mds.name = "PC-Robot";
    mds.device.manufacturer = "Acme Robotics";
    mds.device.model = "AR-6";
    mds.device.product_code = "AR6-STD";
    mds.device.serial_number = "SN-R-0007";
    mds.device.category = RoboticsMotionDeviceCategory::ROBOTICS_CAT_ARTICULATED_ROBOT;
    mds.device.axis_count = 6; // a 6-axis articulated arm
    for (uint32_t k = 0; k < mds.device.axis_count; k++)
    {
        mds.device.axes[k].motion_profile = RoboticsMotionProfile::ROBOTICS_PROFILE_ROTARY;
    }

    // --- Controller + Software identity ---
    mds.controller.manufacturer = "Acme Controls";
    mds.controller.model = "CTRL-9";
    mds.controller.product_code = "C9-STD";
    mds.controller.serial_number = "SN-C-0009";
    mds.controller.sw_manufacturer = "Acme Software";
    mds.controller.sw_model = "RobOS";
    mds.controller.sw_revision = "1.0.0";

    // --- Initial safety state ---
    mds.safety.operational_mode = RoboticsOperationalMode::ROBOTICS_MODE_AUTOMATIC;
    mds.safety.emergency_stop = false;
    mds.safety.protective_stop = false;

    pc_robotics_install(&mds); // bind + register the OPC UA Browse/Read resolvers
    on_http("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        send_text(id, 200, "text/plain", "OPC UA for Robotics MotionDeviceSystem on :4840");
    });
    listen(4840, ConnProto::PROTO_OPCUA); // OPC UA / robotics endpoint - before begin()
    begin_http(80);
    Serial.println("OPC UA for Robotics: opc.tcp://<ip>:4840  - browse MotionDeviceSystem, read live axis values");
}

void loop()
{
    handle();

    // Simulate a moving robot: refresh the live axis values a couple of times a second.
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last >= 500)
    {
        last = now;
        double t = now / 1000.0;
        mds.device.on_path = true;
        mds.device.in_control = true;
        mds.device.speed_override = 100.0;
        for (uint32_t k = 0; k < mds.device.axis_count; k++)
        {
            double phase = t + (double)k * 0.5;
            mds.device.axes[k].actual_position = 90.0 * sin(phase);
            mds.device.axes[k].actual_speed = 90.0 * cos(phase);
            mds.device.axes[k].actual_acceleration = -90.0 * sin(phase);
        }
    }
}
