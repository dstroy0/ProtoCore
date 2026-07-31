// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file InterfaceBridge.ino
 * @brief Turn the ESP32 into a network<->hardware-bus device server (PC_ENABLE_IFACE_BRIDGE).
 *
 * Map a listen `x.x.x.x:nnnn` to a UART, an SPI chip-select, or an I2C address, so a network client
 * that connects to the port is transparently bridged onto that bus. Two payload models:
 *
 *   - STREAM (UART): raw bidirectional passthrough - a classic serial-device server / ser2net. Bytes
 *     the client sends go out the UART; bytes the UART receives come back to the client. No framing.
 *   - TRANSACTION (SPI / I2C, also usable for UART): the client sends framed write-then-read requests
 *         uint16 write_len (big-endian) || uint16 read_len (big-endian) || write_bytes[write_len]
 *     and gets back the read_len bytes clocked off the bus. This is what master-initiated buses need.
 *
 * Wiring mirrors the relay: `server.listen(port, ConnProto::PROTO_BRIDGE)` opens the port, then
 * `pc_iface_bridge_publish()` binds it to a target and brings the bus up. The server poll loop does the rest.
 *
 * Edit the lines marked "CHANGE ME", flash, open Serial @ 115200, then from another machine:
 *   - UART stream:  `nc <board-ip> 2323`  (type; it goes out UART1, replies come back)
 *   - SPI/I2C txn:  send a 4-byte header + write bytes; read read_len bytes back. Part 2 of the
 *     README shows a one-line Python client.
 *
 * SECURITY: a published port is a direct pipe to the bus, with no authentication at this layer. Only
 * expose it on a trusted interface / behind an upstream ACL.
 *
 * NOTE (PlatformIO): the bridge is compiled into the *library*, so the flag must reach the whole
 * build: `build_flags = -DPC_ENABLE_IFACE_BRIDGE=1`. In the Arduino IDE it is set in build_opt.h.
 */

#define PC_ENABLE_IFACE_BRIDGE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/iface_bridge/iface_bridge_hw.h" // pc_iface_bridge_publish

// --- CHANGE ME: your WiFi ---
static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// --- CHANGE ME: the two ports the board serves ---
static const uint16_t UART_PORT = 2323; // raw stream <-> UART1
static const uint16_t SPI_PORT = 2324;  // write-then-read transactions <-> an SPI device

PC server;

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

    // (1) UART1 as a raw serial-device server (ser2net). unit=1 -> Serial1 @ 115200 baud.
    //     {bus, mode, unit, addr_cs, rate, spi_mode, bit_order}
    BridgeTarget uart = {BridgeBus::uart, BridgeMode::stream, 1, 0, 115200, 0, 0};
    int32_t lu = server.listen(UART_PORT, ConnProto::PROTO_BRIDGE);
    if (lu < 0 || !pc_iface_bridge_publish((uint8_t)lu, UART_PORT, BridgeProto::tcp, &uart))
    {
        Serial.println("UART bridge publish failed");
    }

    // (2) An SPI device on chip-select GPIO 5, mode 0, MSB-first, 1 MHz, as write-then-read transactions.
    BridgeTarget spi = {BridgeBus::spi, BridgeMode::transaction, 0, 5 /*CS gpio*/, 1000000, 0, 0};
    int32_t ls = server.listen(SPI_PORT, ConnProto::PROTO_BRIDGE);
    if (ls < 0 || !pc_iface_bridge_publish((uint8_t)ls, SPI_PORT, BridgeProto::tcp, &spi))
    {
        Serial.println("SPI bridge publish failed");
    }

    server.begin();
    Serial.printf("UART stream : %u.%u.%u.%u:%u  <->  Serial1\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF), UART_PORT);
    Serial.printf("SPI txn     : %u.%u.%u.%u:%u  <->  SPI CS=GPIO5\n", (unsigned)(ip & 0xFF),
                  (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF), SPI_PORT);
}

void loop()
{
    server.handle(); // the server poll loop pumps both bridges
}
