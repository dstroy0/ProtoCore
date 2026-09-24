// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// Industrial edge gateway on ProtoCore, built with the ESP-IDF CMake toolchain
// (idf.py) instead of Arduino/PlatformIO. One device exposes three industrial-facing services at once:
//
//   - Web dashboard   HTTP/80        a status page (uptime, heap, live Modbus register)
//   - Modbus TCP slave TCP/502       a small coil/register model a SCADA/PLC client reads + writes
//   - SNMP agent       UDP/161       MIB-II system group + a private free-heap gauge (v1/v2c)
//
// This is the shape of a real industrial edge device: a fieldbus face for the control network, an
// SNMP face for the NMS, and a small web face for humans. Every buffer is statically sized (no heap),
// so determinism holds with all three running.
//
// Feature flags: PROTOCORE_ENABLE_MODBUS, PROTOCORE_ENABLE_SNMP and PROTOCORE_ENABLE_UDP are turned on for the whole
// build by the project's top-level CMakeLists (add_compile_definitions, before project()) - the ESP-IDF way to set a
// PROTOCORE_ENABLE_* flag, since the guards live in the separately-compiled library sources. Do NOT #define them here
// too; the CMake definition already reaches every translation unit.
//
// Arduino autostart is enabled (CONFIG_AUTOSTART_ARDUINO=y in sdkconfig.defaults), so setup() runs once
// and loop() forever, the same shape as an .ino sketch. Set your Wi-Fi credentials below, then flash
// with `idf.py -p <PORT> flash monitor`.
//
// Modbus and SNMP have no authentication or encryption - run them only on a trusted control network.
#include "network_drivers/physical/physical/physical.h" // Physical / PhysicalV
#include "protocore.h"
#include "services/fieldbus/modbus/modbus/modbus.h"
#include "services/net/snmp/snmp_agent/snmp_agent.h"
#include <Arduino.h>

static const char *WIFI_SSID = "YOUR_SSID";
static const char *WIFI_PASS = "YOUR_PASSWORD";

// Modbus data-model addresses (shared with the dashboard + SNMP so the three faces show one state).
static constexpr uint16_t MB_UPTIME_INPUT_REG = 0;  // application-published: uptime seconds (read-only)
static constexpr uint16_t MB_SETPOINT_HOLD_REG = 0; // client-writable: a setpoint the PLC pokes

// Private enterprise SNMP subtree 1.3.6.1.4.1.49374 - a read-only free-heap gauge.
static const uint32_t OID_FREE_HEAP[] = {1, 3, 6, 1, 4, 1, 49374, 10, 0};

// SNMP dynamic read: report the current free heap as a Gauge32.
static bool get_free_heap(SnmpValue *out)
{
    out->type = (uint8_t)SNMP_TAG_SNMP_GAUGE32;
    out->uval = (uint32_t)ESP.getFreeHeap();
    return true;
}

static void set_holding(uint16_t addr, uint16_t value)
{
    ModbusV.set_holding_reg_args.addr = addr;
    ModbusV.set_holding_reg_args.value = value;
    Modbus.set_holding_reg(protocore_modbus_span());
}

static void set_input(uint16_t addr, uint16_t value)
{
    ModbusV.set_input_reg_args.addr = addr;
    ModbusV.set_input_reg_args.value = value;
    Modbus.set_input_reg(protocore_modbus_span());
}

static uint16_t get_holding(uint16_t addr)
{
    ModbusV.get_holding_reg_args.addr = addr;
    Modbus.get_holding_reg(protocore_modbus_span());
    return ModbusV.value;
}

// Notified whenever a Modbus client writes a coil or holding register.
static void on_modbus_write(uint8_t fc, uint16_t start, uint16_t count)
{
    Serial.printf("modbus client write: fc=0x%02X start=%u count=%u\n", fc, start, count);
}

// Web dashboard: one status page that reflects the same state the fieldbus + NMS see.
static void handle_root(uint8_t slot, HttpReq *)
{
    char body[512];
    int n = snprintf(body, sizeof(body),
                     "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                     "<title>PC Industrial Gateway</title>"
                     "<h1>Industrial edge gateway</h1><ul>"
                     "<li>uptime: %lu s</li>"
                     "<li>free heap: %u bytes</li>"
                     "<li>Modbus setpoint (holding reg %u): %u</li>"
                     "</ul><p>Modbus TCP on :502 &middot; SNMP on UDP:161 &middot; HTTP on :80</p>",
                     (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(), (unsigned)MB_SETPOINT_HOLD_REG,
                     (unsigned)get_holding(MB_SETPOINT_HOLD_REG));
    if (n < 0 || (size_t)n >= sizeof(body))
    {
        return send_text(slot, 500, "text/plain", "render error");
    }
    send_text(slot, 200, "text/html", body);
}

void setup()
{
    Serial.begin(115200);
    delay(200);

    // Every Physical entry reads its operands from PhysicalV and writes its outcome back there.
    PhysicalV.wifi.ssid = WIFI_SSID;
    PhysicalV.wifi.password = WIFI_PASS;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("WiFi connecting");
    uint32_t t0 = millis();
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok && millis() - t0 < 20000;
         Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    if (!PhysicalV.ok)
    {
        Serial.println(" no WiFi");
        return;
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // --- Fieldbus: Modbus TCP slave on :502 ---
    Modbus.server_init(protocore_modbus_span());
    set_holding(MB_SETPOINT_HOLD_REG, 0); // client-writable setpoint
    set_input(MB_UPTIME_INPUT_REG, 0);    // application-published uptime (read-only)
    ModbusV.on_write_args.cb = on_modbus_write;
    Modbus.on_write(protocore_modbus_span());
    listen(502, PROTO_MODBUS);

    // --- Management: SNMP v1/v2c agent on UDP:161 ---
    SnmpAgentV.community.ro = "public";
    SnmpAgent.init(protocore_snmp_agent_span());
    SnmpAgentV.system.descr = "ProtoCore industrial gateway";
    SnmpAgentV.system.contact = "admin@example.com";
    SnmpAgentV.system.name = "esp32-pc-gw";
    SnmpAgentV.system.location = "plant floor";
    SnmpAgentV.system.services = 72;
    SnmpAgent.set_system(protocore_snmp_agent_span());
    SnmpAgentV.object.oid = OID_FREE_HEAP;
    SnmpAgentV.object.oid_len = 9;
    SnmpAgentV.object.type = (uint8_t)SNMP_TAG_SNMP_GAUGE32;
    SnmpAgentV.object.getter = get_free_heap;
    SnmpAgent.add_dynamic(protocore_snmp_agent_span());
    SnmpAgentV.port = 161;
    SnmpAgent.listen(protocore_snmp_agent_span());

    // --- Web dashboard + start the TCP server (HTTP/80 + the Modbus listener) ---
    on_http("/", HTTP_GET, handle_root);
    int32_t rc = begin_http(80, NULL);
    if (rc < 0)
    {
        Serial.printf("begin_http() failed (error %ld)\n", (long)rc);
        return;
    }
    Serial.println("gateway ready: HTTP :80, Modbus TCP :502, SNMP UDP :161");
}

void loop()
{
    handle(); // drives HTTP + Modbus; SNMP is serviced by lwIP UDP callbacks

    // Publish a live value the PLC can poll and the dashboard/SNMP reflect: uptime seconds.
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        set_input(MB_UPTIME_INPUT_REG, (uint16_t)(millis() / 1000));
    }
}
