// WifiCapture - capture 802.11 frames on Wi-Fi and forward them out Ethernet.
//
// A wireless tap: put the radio in promiscuous mode (services/radio/promisc), and hand every captured
// frame to the forwarding plane (network_drivers/network/forward), which bridges it to the Ethernet interface.
// The Ethernet egress here streams each frame as a libpcap record over UDP to a wired collector
// (run `tcpdump -i any -w -` style capture, or a tiny socket that writes a .pcap Wireshark opens
// as DLT_IEEE802_11). Capture is strictly passive; a rate cap protects the wired uplink.
//
// Data path:  Wi-Fi radio --pc_promisc_begin--> sink --pc_forward_ingress--> ETH send cb --UDP--> collector
//
// Build flags (whole build), Ethernet tuned here for a LAN8720 board:
//   PC_ENABLE_PROMISC=1 PC_ENABLE_FORWARD=1 PC_ENABLE_ETHERNET=1
//   ETH_PHY_TYPE=ETH_PHY_LAN8720 ETH_PHY_ADDR=1 ETH_PHY_POWER=-1
//   ETH_PHY_MDC=23 ETH_PHY_MDIO=18 ETH_CLK_MODE=ETH_CLOCK_GPIO0_IN

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/udp.h"
#include "network_drivers/network/forward/forward.h"
#include "services/radio/promisc/promisc.h"


// Where to stream the captured frames on the wired side.
static const char *COLLECTOR_IP = "192.168.1.50";
static const uint16_t COLLECTOR_PORT = 5555;
static const uint8_t CAPTURE_CHANNEL = 6;

// Forwarding-plane interface ids.
enum
{
    IF_WIFI = 1,
    IF_ETH = 2
};

// Ethernet egress: wrap the frame in a libpcap record (DLT_IEEE802_11) and UDP it to the
// collector. Udp.client->sendto() routes over the default interface, which is the wired uplink.
static bool eth_send(uint8_t, const uint8_t *frame, uint16_t len, void *)
{
    static uint8_t buf[PC_PCAP_REC_HDR_LEN + 2048];
    if (len > 2048)
    {
        len = 2048;
    }
    uint32_t us = (uint32_t)micros();
    pc_pcap_record_header(buf, sizeof(buf), us / 1000000u, us % 1000000u, len, len);
    memcpy(buf + PC_PCAP_REC_HDR_LEN, frame, len);
    return Udp.client->sendto(COLLECTOR_IP, COLLECTOR_PORT, buf, PC_PCAP_REC_HDR_LEN + len);
}

// Wi-Fi is a source only - no rule forwards *to* it, so this is never called.
static bool wifi_send(uint8_t, const uint8_t *, uint16_t, void *)
{
    return false;
}

// Capture sink: hand each frame to the forwarding plane. For high capture rates, post to the
// FORWARD lane of the preempting queue instead of calling ingress in the radio callback.
static void on_frame(const uint8_t *frame, uint16_t len, int8_t, uint8_t)
{
    pc_forward_ingress(IF_WIFI, frame, len);
}

void setup()
{
    Serial.begin(115200);

    // Wired uplink to the collector.
    Physical.eth->init();
    Serial.print("Bringing up Ethernet");
    while (!Physical.eth->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // Ethernet is the egress here
    Serial.printf("\nEthernet IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Wi-Fi radio for capture (promiscuous; no association - promisc sets the capture channel).
    Physical.wifi->init_radio(0);

    // Forwarding plane: Wi-Fi -> Ethernet, capped so a busy channel can't swamp the uplink.
    pc_forward_reset();
    pc_forward_add_if(IF_WIFI, pc_if_kind::PC_IF_WIFI_STA, wifi_send, nullptr);
    pc_forward_add_if(IF_ETH, pc_if_kind::PC_IF_ETH, eth_send, nullptr);
    pc_forward_add_rule(IF_WIFI, IF_ETH, pc_fwd_action::PC_FWD_ALLOW, 2000); // <= 2000 frames/s to the wire

    pc_promisc_begin(CAPTURE_CHANNEL, on_frame);
    Serial.printf("Capturing on channel %u -> forwarding to %s:%u (PCAP over UDP)\n", CAPTURE_CHANNEL, COLLECTOR_IP,
                  COLLECTOR_PORT);
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last > 5000)
    {
        last = millis();
        pc_forward_stats s;
        pc_forward_get_stats(&s);
        Serial.printf("captured %lu, forwarded %lu, rate-dropped %lu, send-fail %lu\n", (unsigned long)s.frames_in,
                      (unsigned long)s.forwarded, (unsigned long)s.rate_dropped, (unsigned long)s.send_fail);
    }
    delay(10);
}
