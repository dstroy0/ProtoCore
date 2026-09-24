// CanCapture - listen-only CAN capture, forwarded out Ethernet.
//
// The wired counterpart to WifiCapture. Put the ESP32's CAN (TWAI) controller in LISTEN-ONLY
// mode - it decodes every frame on the bus but never ACKs or transmits, so it is invisible to the
// other nodes - and forward each frame to the wired side through the forwarding plane. The
// Ethernet egress streams each frame as a libpcap SocketCAN record over UDP to a collector, which
// Wireshark opens as DLT_CAN_SOCKETCAN.
//
// Data path:  CAN bus --BusCapture.poll--> sink --Forward.ingress--> ETH send cb --UDP--> collector
//
// Wire a 3.3 V CAN transceiver (e.g. SN65HVD230) to the TX/RX GPIOs below and onto the bus.
//
// Build flags (whole build), Ethernet tuned here for a LAN8720 board:
//   PROTOCORE_ENABLE_BUS_CAPTURE=1 PROTOCORE_ENABLE_FORWARD=1 PROTOCORE_ENABLE_ETHERNET=1 PROTOCORE_ENABLE_UDP=1
//   ETH_PHY_TYPE=ETH_PHY_LAN8720 ETH_PHY_ADDR=1 ETH_PHY_POWER=-1
//   ETH_PHY_MDC=23 ETH_PHY_MDIO=18 ETH_CLK_MODE=ETH_CLOCK_GPIO0_IN

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/udp/udp.h"
#include "network_drivers/network/forward/forward.h"
#include "server/signaling/bus_capture/bus_capture.h"
#include "shared/pcap/pcap.h"
#include "shared/ip/ip.h"


static const uint8_t COLLECTOR_IP[4] = {192, 168, 1, 50};
static const uint16_t COLLECTOR_PORT = 5556;
static const int CAN_TX_PIN = 5;
static const int CAN_RX_PIN = 4;
static const uint32_t CAN_BITRATE = 500000;

// The collector as an address, built once in setup() - a send takes a protocore_ip, never text.
static protocore_ip collector;

// Pcap never reads its borrow: both headers are written into the caller's buffer.
static uint8_t g_pcap_work[16];

enum
{
    IF_CAN = 1,
    IF_ETH = 2
};

// Ethernet egress: wrap the SocketCAN frame in a libpcap record and UDP it to the collector.
static proto_bool eth_send(uint8_t, const uint8_t *frame, uint16_t len, void *)
{
    uint8_t buf[PROTOCORE_PCAP_REC_HDR_LEN + PROTOCORE_SOCKETCAN_FRAME_LEN];
    if (len > PROTOCORE_SOCKETCAN_FRAME_LEN)
    {
        len = PROTOCORE_SOCKETCAN_FRAME_LEN;
    }
    uint32_t us = (uint32_t)micros();
    PcapV.args.out = buf;
    PcapV.args.cap = sizeof(buf);
    PcapV.rec.ts_sec = us / 1000000u;
    PcapV.rec.ts_usec = us % 1000000u;
    PcapV.rec.caplen = len;
    PcapV.rec.origlen = len;
    Pcap.record_header(g_pcap_work);
    memcpy(buf + PROTOCORE_PCAP_REC_HDR_LEN, frame, len);
    UdpClientV.dst = &collector;
    UdpClientV.dst_port = COLLECTOR_PORT;
    UdpClientV.data = buf;
    UdpClientV.len = PROTOCORE_PCAP_REC_HDR_LEN + len;
    UdpClient.sendto(protocore_udp_client_span());
    return UdpClientV.ok;
}

// CAN is a source only - no rule forwards *to* it, so this is never called.
static proto_bool can_send(uint8_t, const uint8_t *, uint16_t, void *)
{
    return PROTO_FALSE;
}

// Capture sink: format the decoded CAN frame as SocketCAN and hand it to the forwarding plane.
static void on_can(const CanFrame *f)
{
    uint8_t sc[PROTOCORE_SOCKETCAN_FRAME_LEN];
    BusCaptureV.can_to_socketcan_args.f = f;
    BusCaptureV.can_to_socketcan_args.out = sc;
    BusCaptureV.can_to_socketcan_args.cap = sizeof(sc);
    BusCapture.can_to_socketcan(protocore_bus_capture_span());
    if (BusCaptureV.n)
    {
        ForwardV.src_if = IF_CAN;
        ForwardV.frame.data = sc;
        ForwardV.frame.len = PROTOCORE_SOCKETCAN_FRAME_LEN;
        Forward.ingress(protocore_forward_span());
    }
}

// Register interface @p id of @p kind with layer 1, sending through @p send.
static void add_iface(uint8_t id, protocore_if_kind kind, protocore_if_send_fn send)
{
    PhysicalV.iface.id = id;
    PhysicalV.iface.kind = kind;
    PhysicalV.iface.send = send;
    PhysicalV.iface.ctx = nullptr;
    Physical.iface_add(protocore_physical_span());
}

void setup()
{
    Serial.begin(115200);

    // Wired uplink to the collector.
    Physical.eth_init(protocore_physical_span());
    Serial.print("Bringing up Ethernet");
    for (Physical.eth_ready(protocore_physical_span()); !PhysicalV.ok; Physical.eth_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // Ethernet is the egress here
    Serial.printf("\nEthernet IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Forwarding plane: CAN -> Ethernet.
    // The interfaces themselves are registered with layer 1, which the forwarding plane sends through.
    collector = protocore_ip_from_v4_octets(COLLECTOR_IP[0], COLLECTOR_IP[1], COLLECTOR_IP[2], COLLECTOR_IP[3]);
    Forward.reset(protocore_forward_span());
    Physical.iface_reset(protocore_physical_span());
    add_iface(IF_CAN, PROTOCORE_IF_BUS, can_send);
    add_iface(IF_ETH, PROTOCORE_IF_ETH, eth_send);
    ForwardV.src_if = IF_CAN;
    ForwardV.rule.dst_if = IF_ETH;
    ForwardV.rule.action = PROTOCORE_FWD_ALLOW;
    ForwardV.rule.rate_cap_per_sec = 0; // CAN tops out ~a few k frames/s
    Forward.add_rule(protocore_forward_span());

    BusCaptureV.begin_args.tx_pin = CAN_TX_PIN;
    BusCaptureV.begin_args.rx_pin = CAN_RX_PIN;
    BusCaptureV.begin_args.bitrate = CAN_BITRATE;
    BusCaptureV.begin_args.sink = on_can;
    BusCapture.begin(protocore_bus_capture_span());
    if (!BusCaptureV.ok)
    {
        Serial.println("TWAI (CAN) listen-only start failed - check pins / bit rate / transceiver");
        return;
    }
    Serial.printf("Capturing CAN @ %lu bps (listen-only) -> forwarding to %u.%u.%u.%u:%u (SocketCAN PCAP over UDP)\n",
                  (unsigned long)CAN_BITRATE, COLLECTOR_IP[0], COLLECTOR_IP[1], COLLECTOR_IP[2], COLLECTOR_IP[3],
                  COLLECTOR_PORT);
}

void loop()
{
    BusCapture.poll(protocore_bus_capture_span()); // drain received CAN frames into the forwarding plane

    static uint32_t last = 0;
    if (millis() - last > 5000)
    {
        last = millis();
        Forward.get_stats(protocore_forward_span());
        const protocore_forward_stats &s = ForwardV.stats;
        Serial.printf("captured %lu, forwarded %lu, send-fail %lu\n", (unsigned long)s.frames_in,
                      (unsigned long)s.forwarded, (unsigned long)s.send_fail);
    }
}
