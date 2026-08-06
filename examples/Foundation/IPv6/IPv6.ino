// IPv6 - serve over IPv6 (dual-stack), alongside IPv4.
//
// The TCP and UDP listeners already bind IPADDR_TYPE_ANY, so the moment the interface has an
// IPv6 address the server answers over v6 with no extra work. PC_ENABLE_IPV6 turns IPv6 on
// for the Wi-Fi netif (init_ipv6_physical() -> SLAAC: a link-local address, plus a global one
// if the network advertises a prefix). The pc_ip address core
// (shared_primitives/ip.h) parses, formats (RFC 5952 canonical), and classifies both
// families - used here to print and report the acquired address.
//
// Build flag (whole build, not just this sketch):
//   PC_ENABLE_IPV6=1

#include "protocore.h"
#include "shared_primitives/ip.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static const char *scope_name(pc_ip_scope s)
{
    switch (s)
    {
    case pc_ip_scope::PC_IP_SCOPE_LOOPBACK:
        return "loopback";
    case pc_ip_scope::PC_IP_SCOPE_LINK_LOCAL:
        return "link-local";
    case pc_ip_scope::PC_IP_SCOPE_PRIVATE:
        return "unique-local";
    case pc_ip_scope::PC_IP_SCOPE_MULTICAST:
        return "multicast";
    case pc_ip_scope::PC_IP_SCOPE_GLOBAL:
        return "global";
    default:
        return "unspecified";
    }
}

void handle_root(uint8_t slot_id, HttpReq *)
{
    pc_ip v6;
    char buf[160];
    if (net_global_ipv6(&v6))
    {
        char addr[PC_IP_STR_MAX];
        pc_ip_format(&v6, addr, sizeof(addr));
        snprintf(buf, sizeof(buf), "Served over IPv6. My global address is [%s] (%s).", addr,
                 scope_name(pc_ip_classify(&v6)));
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

    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    init_ipv6_physical(); // enable IPv6 (SLAAC) on the Wi-Fi netif

    uint32_t ip = pc_net_egress_ip();
    Serial.printf("IPv4: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
    Serial.print("Waiting for a global IPv6 address");
    for (int i = 0; i < 40 && !pc_ipv6_ready(); i++)
    {
        delay(250);
        Serial.print('.');
    }

    pc_ip v6;
    if (net_global_ipv6(&v6))
    {
        char addr[PC_IP_STR_MAX];
        pc_ip_format(&v6, addr, sizeof(addr));
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
