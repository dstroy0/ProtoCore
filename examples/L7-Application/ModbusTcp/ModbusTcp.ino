// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ModbusTcp.ino
 * @brief Modbus TCP slave/server (Modbus Application Protocol) on TCP/502.
 *
 * Serves a small data model (coils, discrete inputs, holding + input registers)
 * over Modbus TCP. A SCADA/PLC client or a tool like `mbpoll` can read and write
 * it:
 *     mbpoll -m tcp -t 4:hex -r 1 -c 2 <ip>      # read holding regs 0..1
 *     mbpoll -m tcp -t 0 -r 1 1 <ip>             # write coil 0 = 1
 *
 * The application owns the data model: it writes input registers / discrete
 * inputs (read-only to the client) to publish sensor state, reads holding
 * registers / coils the client has written, and is notified of client writes via
 * Modbus.on_write. Modbus has no authentication or encryption - run it only on
 * a trusted control network (front it with the per-IP accept throttle).
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPROTOCORE_ENABLE_MODBUS=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_MODBUS 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/fieldbus/modbus/modbus/modbus.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

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

// Notified whenever a client writes a coil or holding register.
static void on_write(uint8_t fc, uint16_t start, uint16_t count)
{
    Serial.printf("client write: fc=0x%02X start=%u count=%u\n", fc, start, count);
}

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    Modbus.server_init(protocore_modbus_span());
    set_holding(0, 0x1234); // client-writable registers
    set_input(0, 0);        // application-published (read-only to client)
    ModbusV.on_write_args.cb = on_write;
    Modbus.on_write(protocore_modbus_span());

    listen(502, PROTO_MODBUS);
    proto_begin(NULL);
    Serial.println("Modbus TCP slave on :502");
}

void loop()
{
    handle();

    // Publish a live value into an input register the client can poll.
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        set_input(0, (uint16_t)(millis() / 1000)); // uptime seconds
    }
}
