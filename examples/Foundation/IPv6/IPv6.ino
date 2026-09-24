// IPv6 - serve over IPv6 (dual-stack), alongside IPv4.
//
// The TCP and UDP listeners already bind IPADDR_TYPE_ANY, so the moment the interface has an
// IPv6 address the server answers over v6 with no extra work. PROTOCORE_ENABLE_IPV6 turns IPv6 on
// for the Wi-Fi netif (Physical.ip6_init() -> SLAAC: a link-local address, plus a global one
// if the network advertises a prefix). The protocore_ip address core
// (shared/ip/ip.h) parses, formats (RFC 5952 canonical), and classifies both
// families - used here to print and report the acquired address.
//
// Build flag (whole build, not just this sketch):
//   PROTOCORE_ENABLE_IPV6=1

#include "protocore.h"
#include "shared/ip/ip.h"
#include "network_drivers/physical/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static const char *scope_name(protocore_ip_scope s)
{
    switch (s)
    {
    case protocore_ip_scope::PROTOCORE_IP_SCOPE_LOOPBACK:
        return "loopback";
    case protocore_ip_scope::PROTOCORE_IP_SCOPE_LINK_LOCAL:
        return "link-local";
    case protocore_ip_scope::PROTOCORE_IP_SCOPE_PRIVATE:
        return "unique-local";
    case protocore_ip_scope::PROTOCORE_IP_SCOPE_MULTICAST:
        return "multicast";
    case protocore_ip_scope::PROTOCORE_IP_SCOPE_GLOBAL:
        return "global";
    default:
        return "unspecified";
    }
}

// The borrow an Ip entry takes. Ip reads its operands from IpV and never touches it.
static uint8_t ip_work[16];

void handle_root(uint8_t slot_id, HttpReq *)
{
    protocore_ip v6;
    char buf[160];
    PhysicalV.read.ip6 = &v6;
    Physical.ip6_global(protocore_physical_span());
    if (PhysicalV.ok)
    {
        char addr[PROTOCORE_IP_STR_MAX];
        IpV.args.ip = &v6;
        IpV.args.buf = addr;
        IpV.args.cap = sizeof(addr);
        Ip.format(ip_work);
        Ip.classify(ip_work);
        snprintf(buf, sizeof(buf), "Served over IPv6. My global address is [%s] (%s).", addr,
                 scope_name(IpV.scope));
    }
    else
    {
        snprintf(buf, sizeof(buf), "Served over IPv4 (no global IPv6 address yet).");
    }
    send_text(slot_id, 200, "text/plain", buf);
}

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.ip6_init(protocore_physical_span()); // enable IPv6 (SLAAC) on the Wi-Fi netif

    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32;
    Serial.printf("IPv4: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
    Serial.print("Waiting for a global IPv6 address");
    Physical.ip6_ready(protocore_physical_span());
    for (int i = 0; i < 40 && !PhysicalV.ok; i++)
    {
        delay(250);
        Serial.print('.');
        Physical.ip6_ready(protocore_physical_span());
    }

    protocore_ip v6;
    PhysicalV.read.ip6 = &v6;
    Physical.ip6_global(protocore_physical_span());
    if (PhysicalV.ok)
    {
        char addr[PROTOCORE_IP_STR_MAX];
        IpV.args.ip = &v6;
        IpV.args.buf = addr;
        IpV.args.cap = sizeof(addr);
        Ip.format(ip_work);
        Serial.printf("\nIPv6: %s\n", addr);
        Serial.printf("Try: curl -g 'http://[%s]/'\n", addr);
    }
    else
    {
        Serial.println("\nNo global IPv6 yet (the network may not advertise a prefix); link-local still works.");
    }

    on_http("/", HTTP_GET, handle_root);

    int32_t result = begin_http(80, NULL);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on :80 (dual-stack IPv4 + IPv6)");
}

void loop()
{
    handle();
}
