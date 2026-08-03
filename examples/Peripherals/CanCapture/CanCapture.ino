// CanCapture - listen-only CAN capture, forwarded out Ethernet.
//
// The wired counterpart to WifiCapture. Put the ESP32's CAN (TWAI) controller in LISTEN-ONLY
// mode - it decodes every frame on the bus but never ACKs or transmits, so it is invisible to the
// other nodes - and forward each frame to the wired side through the forwarding plane. The
// Ethernet egress streams each frame as a libpcap SocketCAN record over UDP to a collector, which
// Wireshark opens as DLT_CAN_SOCKETCAN.
//
// Data path:  CAN bus --bus_capture_poll--> sink --pc_forward_ingress--> ETH send cb --UDP--> collector
//
// Wire a 3.3 V CAN transceiver (e.g. SN65HVD230) to the TX/RX GPIOs below and onto the bus.
//
// Build flags (whole build), Ethernet tuned here for a LAN8720 board:
//   PC_ENABLE_BUS_CAPTURE=1 PC_ENABLE_FORWARD=1 PC_ENABLE_ETHERNET=1
//   ETH_PHY_TYPE=ETH_PHY_LAN8720 ETH_PHY_ADDR=1 ETH_PHY_POWER=-1
//   ETH_PHY_MDC=23 ETH_PHY_MDIO=18 ETH_CLK_MODE=ETH_CLOCK_GPIO0_IN

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/udp.h"
#include "services/net/forward/forward.h"
#include "server/signaling/bus_capture.h"
#include <string.h>

static const char *COLLECTOR_IP = "192.168.1.50";
static const uint16_t COLLECTOR_PORT = 5556;
static const int CAN_TX_PIN = 5;
static const int CAN_RX_PIN = 4;
static const uint32_t CAN_BITRATE = 500000;

enum
{
    IF_CAN = 1,
    IF_ETH = 2
};

// Ethernet egress: wrap the SocketCAN frame in a libpcap record and UDP it to the collector.
static bool eth_send(uint8_t, const uint8_t *frame, uint16_t len, void *)
{
    uint8_t buf[PC_PCAP_REC_HDR_LEN + PC_SOCKETCAN_FRAME_LEN];
    if (len > PC_SOCKETCAN_FRAME_LEN)
    {
        len = PC_SOCKETCAN_FRAME_LEN;
    }
    uint32_t us = (uint32_t)micros();
    pc_pcap_record_header(buf, sizeof(buf), us / 1000000u, us % 1000000u, len, len);
    memcpy(buf + PC_PCAP_REC_HDR_LEN, frame, len);
    return pc_udp_sendto(COLLECTOR_IP, COLLECTOR_PORT, buf, PC_PCAP_REC_HDR_LEN + len);
}

// CAN is a source only - no rule forwards *to* it, so this is never called.
static bool can_send(uint8_t, const uint8_t *, uint16_t, void *)
{
    return false;
}

// Capture sink: format the decoded CAN frame as SocketCAN and hand it to the forwarding plane.
static void on_can(const CanFrame *f)
{
    uint8_t sc[PC_SOCKETCAN_FRAME_LEN];
    if (can_to_socketcan(f, sc, sizeof(sc)))
    {
        pc_forward_ingress(IF_CAN, sc, PC_SOCKETCAN_FRAME_LEN);
    }
}

void setup()
{
    Serial.begin(115200);

    // Wired uplink to the collector.
    init_eth_physical();
    Serial.print("Bringing up Ethernet");
    while (!eth_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // Ethernet is the egress here
    Serial.printf("\nEthernet IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Forwarding plane: CAN -> Ethernet.
    pc_forward_reset();
    pc_forward_add_if(IF_CAN, pc_if_kind::PC_IF_BUS, can_send, nullptr);
    pc_forward_add_if(IF_ETH, pc_if_kind::PC_IF_ETH, eth_send, nullptr);
    pc_forward_add_rule(IF_CAN, IF_ETH, pc_fwd_action::PC_FWD_ALLOW, 0); // CAN tops out ~a few k frames/s

    if (!bus_capture_begin(CAN_TX_PIN, CAN_RX_PIN, CAN_BITRATE, on_can))
    {
        Serial.println("TWAI (CAN) listen-only start failed - check pins / bit rate / transceiver");
        return;
    }
    Serial.printf("Capturing CAN @ %lu bps (listen-only) -> forwarding to %s:%u (SocketCAN PCAP over UDP)\n",
                  (unsigned long)CAN_BITRATE, COLLECTOR_IP, COLLECTOR_PORT);
}

void loop()
{
    bus_capture_poll(); // drain received CAN frames into the forwarding plane

    static uint32_t last = 0;
    if (millis() - last > 5000)
    {
        last = millis();
        pc_forward_stats s;
        pc_forward_get_stats(&s);
        Serial.printf("captured %lu, forwarded %lu, send-fail %lu\n", (unsigned long)s.frames_in,
                      (unsigned long)s.forwarded, (unsigned long)s.send_fail);
    }
}
