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
 * pc_modbus_on_write(). Modbus has no authentication or encryption - run it only on
 * a trusted control network (front it with the per-IP accept throttle).
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_MODBUS=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_MODBUS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/fieldbus/modbus/modbus.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// Notified whenever a client writes a coil or holding register.
static void on_write(uint8_t fc, uint16_t start, uint16_t count)
{
    Serial.printf("client write: fc=0x%02X start=%u count=%u\n", fc, start, count);
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_modbus_server_init();
    pc_modbus_set_holding_reg(0, 0x1234); // client-writable registers
    pc_modbus_set_input_reg(0, 0);        // application-published (read-only to client)
    pc_modbus_on_write(on_write);

    listen(502, PROTO_MODBUS);
    begin();
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
        pc_modbus_set_input_reg(0, (uint16_t)(millis() / 1000)); // uptime seconds
    }
}
