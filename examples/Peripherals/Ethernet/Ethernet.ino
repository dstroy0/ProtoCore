// Ethernet - run the server over a wired Ethernet PHY instead of Wi-Fi.
//
// Some deployments want a wired uplink (PoE cameras, panel-mount controllers, noisy RF
// environments). With PC_ENABLE_ETHERNET the physical layer gains init_eth_physical()
// alongside init_wifi_physical() - a thin wrapper over the Arduino ETH library for an RMII
// PHY (LAN8720 / TLK110 / RTL8201 / DP83848). Once the link has an IP the server accepts on
// it with no other change: the egress reporting already classifies a wired route as
// pc_iface::PC_IFACE_ETH, so per-route interface filters and everything else just work.
//
// The PHY pins / type / clock come from the standard ETH_PHY_* build flags (below) - set
// them for your board. Needs an ESP32 with an Ethernet PHY to run.
//
// Build flags (whole build), tuned here for a LAN8720 board:
//   PC_ENABLE_ETHERNET=1
//   ETH_PHY_TYPE=ETH_PHY_LAN8720 ETH_PHY_ADDR=1 ETH_PHY_POWER=-1
//   ETH_PHY_MDC=23 ETH_PHY_MDIO=18 ETH_CLK_MODE=ETH_CLOCK_GPIO0_IN

#include "protocore.h"
#include "network_drivers/physical/physical.h"

PC server;

static unsigned long request_count = 0;

void handle_root(uint8_t slot_id, HttpReq *)
{
    request_count++;
    server.send(slot_id, 200, "text/plain", "Served over wired Ethernet.");
}

void handle_status(uint8_t slot_id, HttpReq *)
{
    request_count++;
    char body[128];
    snprintf(body, sizeof(body), "{\"link\":\"ethernet\",\"count\":%lu,\"uptime_ms\":%lu,\"free_heap\":%u}",
             request_count, millis(), ESP.getFreeHeap());
    server.send(slot_id, 200, "application/json", body);
}

void setup()
{
    Serial.begin(115200);

    // init_eth_physical() brings up the RMII PHY (ETH.begin with the ETH_PHY_* flags). Check the return
    // before polling for a link - a PHY that never installed would spin this loop forever.
    if (!init_eth_physical())
    {
        Serial.println("Ethernet init failed: the PHY did not come up. Check the ETH_PHY_* pin / clock / "
                       "power flags for your board. Not polling for a link.");
        return;
    }
    Serial.print("Bringing up Ethernet");
    unsigned long t0 = millis();
    while (!eth_ready() && millis() - t0 < 15000)
    {
        delay(250);
        Serial.print('.');
    }
    if (!eth_ready())
    {
        Serial.println("\nEthernet link did not come up (cable / DHCP?)");
        return;
    }
    uint32_t ip = pc_net_egress_ip(); // Ethernet is the egress here
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/", HttpMethod::HTTP_GET, handle_root);
    server.on("/api/status", HttpMethod::HTTP_GET, handle_status);

    int32_t result = server.begin(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on :80 over Ethernet");
}

void loop()
{
    server.handle();
}
