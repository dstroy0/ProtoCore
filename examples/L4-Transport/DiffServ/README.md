# DiffServ - QoS marking (RFC 2474) on outbound traffic

**Layer:** L4 Transport · **Build flags:** `PROTOCORE_ENABLE_DIFFSERV`

## What this example teaches

DiffServ (RFC 2474) puts a 6-bit **DSCP** in the DS field of the IP header (the
high 6 bits of the IPv4 TOS / IPv6 Traffic-Class byte). A QoS-aware switch, router,
or Wi-Fi AP reads that class and prioritizes the packet - real-time / safety traffic
marked **Expedited Forwarding** (EF, DSCP 46) jumps ahead of best-effort, and the
Wi-Fi driver maps it into the top 802.11e WMM access categories.

With `PROTOCORE_ENABLE_DIFFSERV` the transport writes the DSCP into the pcb's TOS field
as each connection is accepted / connected, so nothing is added to the send hot
path. Two levels of control, coarse to fine:

```cpp
#include "network_drivers/transport/diffserv/diffserv.h"

DiffServV.dscp = PROTOCORE_DSCP_EF;               // every outbound TCP connection (accepted and client)
DiffServ.set_default(protocore_diffserv_span());
TcpListenerV.bind.port = 8080;                    // every connection accepted on one port
TcpListenerV.bind.dscp = PROTOCORE_DSCP_CS6;
TcpListener.set_dscp(protocore_tcp_listener_span());
DiffServ.set_udp(protocore_diffserv_span());      // outbound UDP datagrams
```

A live connection keeps the mark it starts with. RFC 9293 sec 3.9.2 SHLD-23 says that an application should not
change the DS field during a connection, so the port is the finest level.

A DSCP of `0` means best-effort (no marking). Convenience code points are defined
(`PROTOCORE_DSCP_EF` 46, `PROTOCORE_DSCP_CS6` 48, `PROTOCORE_DSCP_AF41` 34, `PROTOCORE_DSCP_AF31` 26); any
`0-63` value is accepted - handy for **arbitrarily tagging traffic in network
testing**, not just standards-defined classes.

This sketch marks every connection EF by default, and marks each connection on port
8080 CS6 to show the per-listener override.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPROTOCORE_ENABLE_DIFFSERV=1" \
  --lib="." examples/L4-Transport/DiffServ/DiffServ.ino
```

Flash it, then read the DSCP straight off the wire from another machine on the LAN:

```sh
sudo tcpdump -i <iface> -v host <board-ip> and tcp &
curl http://<board-ip>/        # response packets: tos 0xb8  (EF,  DSCP 46)
curl http://<board-ip>:8080/   # response packets: tos 0xc0  (CS6, DSCP 48)
```

The marking applies from the first data segment of a connection. The SYN-ACK stays
best-effort, because the stack sends it before any application callback, and the DS
field of the block that listens does not carry over to it.

## Annotated source

The complete sketch ([DiffServ.ino](DiffServ.ino)):

```cpp
#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/diffserv/diffserv.h"
#include "network_drivers/transport/tcp/server/server.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";
static const uint16_t MARKED_PORT = 8080; // the listener whose connections take CS6

// GET / - the response carries the mark of the port that the request came in on.
void handle_root(uint8_t slot, HttpReq *req)
{
    (void)req;
    send_text(slot, 200, "text/plain", "marked by its port: EF (46) on :80, CS6 (48) on :8080\n");
}

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    while (!protocore_physical_wifi_up())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = protocore_physical_egress_ipv4();
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Mark every outbound TCP connection Expedited Forwarding, and outbound UDP too. A DSCP of 0 would
    // mean best-effort (no marking). Set before begin_http() so listeners pick it up.
    DiffServV.dscp = PROTOCORE_DSCP_EF;
    DiffServ.set_default(protocore_diffserv_span());
    DiffServ.set_udp(protocore_diffserv_span());

    on_http("/", HTTP_GET, handle_root);
    begin_http(80, NULL);

    // A second HTTP port, whose accepted connections take CS6 in place of the default.
    listen(MARKED_PORT, PROTO_HTTP);
    TcpListenerV.bind.port = MARKED_PORT;
    TcpListenerV.bind.dscp = PROTOCORE_DSCP_CS6;
    TcpListener.set_dscp(protocore_tcp_listener_span());
    Serial.printf("DiffServ server on :80 (EF) and :%u (CS6)\n", (unsigned)MARKED_PORT);
}

void loop()
{
    handle();
}
```
