// WifiCapture - capture 802.11 frames on Wi-Fi and forward them out Ethernet.
//
// A wireless tap: put the radio in promiscuous mode (services/radio/promisc), and hand every captured
// frame to the forwarding plane (network_drivers/network/forward), which bridges it to the Ethernet interface.
// The Ethernet egress here streams each frame as a libpcap record over UDP to a wired collector
// (run `tcpdump -i any -w -` style capture, or a tiny socket that writes a .pcap Wireshark opens
// as DLT_IEEE802_11). Capture is strictly passive; a rate cap protects the wired uplink.
//
// Data path:  Wi-Fi radio --Promisc.begin--> sink --Forward.ingress--> ETH send cb --UDP--> collector
//
// Build flags (whole build), Ethernet tuned here for a LAN8720 board:
//   PROTOCORE_ENABLE_PROMISC=1 PROTOCORE_ENABLE_FORWARD=1 PROTOCORE_ENABLE_ETHERNET=1 PROTOCORE_ENABLE_UDP=1
//   ETH_PHY_TYPE=ETH_PHY_LAN8720 ETH_PHY_ADDR=1 ETH_PHY_POWER=-1
//   ETH_PHY_MDC=23 ETH_PHY_MDIO=18 ETH_CLK_MODE=ETH_CLOCK_GPIO0_IN

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/udp/udp.h"
#include "network_drivers/network/forward/forward.h"
#include "services/radio/promisc/promisc.h"
#include "shared/pcap/pcap.h"
#include "shared/ip/ip.h"


// Where to stream the captured frames on the wired side.
static const uint8_t COLLECTOR_IP[4] = {192, 168, 1, 50};
static const uint16_t COLLECTOR_PORT = 5555;
static const uint8_t CAPTURE_CHANNEL = 6;

// The collector as an address, built once in setup() - a send takes a protocore_ip, never text.
static protocore_ip collector;

// Pcap never reads its borrow: both headers are written into the caller's buffer.
static uint8_t g_pcap_work[16];

// Forwarding-plane interface ids.
enum
{
    IF_WIFI = 1,
    IF_ETH = 2
};

// Ethernet egress: wrap the frame in a libpcap record (DLT_IEEE802_11) and UDP it to the
// collector. UdpClient.sendto() routes over the default interface, which is the wired uplink.
static proto_bool eth_send(uint8_t, const uint8_t *frame, uint16_t len, void *)
{
    static uint8_t buf[PROTOCORE_PCAP_REC_HDR_LEN + 2048];
    if (len > 2048)
    {
        len = 2048;
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

// Wi-Fi is a source only - no rule forwards *to* it, so this is never called.
static proto_bool wifi_send(uint8_t, const uint8_t *, uint16_t, void *)
{
    return PROTO_FALSE;
}

// Capture sink: hand each frame to the forwarding plane. For high capture rates, post to the
// FORWARD lane of the preempting queue instead of calling ingress in the radio callback.
static void on_frame(const uint8_t *frame, uint16_t len, int8_t, uint8_t)
{
    ForwardV.src_if = IF_WIFI;
    ForwardV.frame.data = frame;
    ForwardV.frame.len = len;
    Forward.ingress(protocore_forward_span());
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

    // Wi-Fi radio for capture (promiscuous; no association - promisc sets the capture channel).
    PhysicalV.wifi.channel = 0;
    Physical.wifi_radio_init(protocore_physical_span());

    // Forwarding plane: Wi-Fi -> Ethernet, capped so a busy channel can't swamp the uplink.
    // The interfaces themselves are registered with layer 1, which the forwarding plane sends through.
    collector = protocore_ip_from_v4_octets(COLLECTOR_IP[0], COLLECTOR_IP[1], COLLECTOR_IP[2], COLLECTOR_IP[3]);
    Forward.reset(protocore_forward_span());
    Physical.iface_reset(protocore_physical_span());
    add_iface(IF_WIFI, PROTOCORE_IF_WIFI_STA, wifi_send);
    add_iface(IF_ETH, PROTOCORE_IF_ETH, eth_send);
    ForwardV.src_if = IF_WIFI;
    ForwardV.rule.dst_if = IF_ETH;
    ForwardV.rule.action = PROTOCORE_FWD_ALLOW;
    ForwardV.rule.rate_cap_per_sec = 2000; // <= 2000 frames/s to the wire
    Forward.add_rule(protocore_forward_span());

    Promisc.begin(protocore_promisc_span(), CAPTURE_CHANNEL, on_frame);
    Serial.printf("Capturing on channel %u -> forwarding to %u.%u.%u.%u:%u (PCAP over UDP)\n", CAPTURE_CHANNEL,
                  COLLECTOR_IP[0], COLLECTOR_IP[1], COLLECTOR_IP[2], COLLECTOR_IP[3], COLLECTOR_PORT);
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last > 5000)
    {
        last = millis();
        Forward.get_stats(protocore_forward_span());
        const protocore_forward_stats &s = ForwardV.stats;
        Serial.printf("captured %lu, forwarded %lu, rate-dropped %lu, send-fail %lu\n", (unsigned long)s.frames_in,
                      (unsigned long)s.forwarded, (unsigned long)s.rate_dropped, (unsigned long)s.send_fail);
    }
    delay(10);
}
