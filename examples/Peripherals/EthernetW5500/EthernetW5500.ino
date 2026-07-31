// EthernetW5500 - run the server over a W5500 SPI Ethernet module (ESP32-S3).
//
// The RMII path (example Ethernet) needs an ESP32 with an on-chip Ethernet MAC. The S3 has
// no RMII MAC, so a wired link there uses an SPI Ethernet controller - the WIZnet W5500 - over
// the HSPI bus. With PC_ETH_W5500=1, init_eth_physical() calls the arduino-esp32 3.x ETH SPI
// API (ETH.begin(ETH_PHY_W5500, ...)); once the link has a DHCP IP the server accepts on it with
// no other change (the egress reporting classifies the wired route as pc_iface::PC_IFACE_ETH).
//
// W5500 SPI Ethernet is arduino-esp32 3.x only (the 2.x ETH library has no W5500). Build this
// with the arduino-cli / IDF-5.x core.
//
// Wiring (ESP32-S3-DevKitC, HSPI) - the PC_ETH_W5500_* build flags in build_opt.h:
//   CS = GPIO7  RST = GPIO6  INT = GPIO5  SCLK = GPIO12  MOSI = GPIO11  MISO = GPIO13  (VCC 3V3, GND)

#include "protocore.h"
#include "network_drivers/physical/physical.h"

PC server;

static unsigned long request_count = 0;

void handle_root(uint8_t slot_id, HttpReq *)
{
    request_count++;
    server.send(slot_id, 200, "text/plain", "Served over W5500 SPI Ethernet.");
}

void handle_status(uint8_t slot_id, HttpReq *)
{
    request_count++;
    char body[128];
    snprintf(body, sizeof(body), "{\"link\":\"w5500\",\"count\":%lu,\"uptime_ms\":%lu,\"free_heap\":%u}", request_count,
             millis(), ESP.getFreeHeap());
    server.send(slot_id, 200, "application/json", body);
}

void setup()
{
    Serial.begin(115200);

    // init_eth_physical() installs the W5500 driver (ETH.begin with the PC_ETH_W5500_* pins). It
    // returns false if the MAC never answered on SPI - check the return before polling for a link, or a
    // never-installed driver reboot-loops the poll below.
    if (!init_eth_physical())
    {
        Serial.println("W5500 init failed: the MAC did not answer on SPI. Check 3V3 power and the SPI "
                       "wiring (CS/SCK/MISO/MOSI + RST/INT). Not polling for a link.");
        return;
    }
    Serial.print("Bringing up W5500 Ethernet");
    unsigned long t0 = millis();
    while (!eth_ready() && millis() - t0 < 15000)
    {
        delay(250);
        Serial.print('.');
    }
    if (!eth_ready())
    {
        Serial.println("\nW5500 link did not come up (check wiring / DHCP)");
        return;
    }
    uint32_t ip = pc_net_egress_ip(); // Ethernet is the egress here
    Serial.printf("\nETH_IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/", HttpMethod::HTTP_GET, handle_root);
    server.on("/api/status", HttpMethod::HTTP_GET, handle_status);

    int32_t result = server.begin(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on :80 over W5500 Ethernet");
}

void loop()
{
    server.handle();
}
