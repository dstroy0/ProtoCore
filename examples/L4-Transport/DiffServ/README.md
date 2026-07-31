# DiffServ - QoS marking (RFC 2474) on outbound traffic

**Layer:** L4 Transport · **Build flags:** `PC_ENABLE_DIFFSERV`

## What this example teaches

DiffServ (RFC 2474) puts a 6-bit **DSCP** in the DS field of the IP header (the
high 6 bits of the IPv4 TOS / IPv6 Traffic-Class byte). A QoS-aware switch, router,
or Wi-Fi AP reads that class and prioritizes the packet - real-time / safety traffic
marked **Expedited Forwarding** (EF, DSCP 46) jumps ahead of best-effort, and the
Wi-Fi driver maps it into the top 802.11e WMM access categories.

With `PC_ENABLE_DIFFSERV` the transport writes the DSCP into the pcb's TOS field
as each connection is accepted / connected, so nothing is added to the send hot
path. Three levels of control, coarse to fine:

```cpp
#include "network_drivers/transport/diffserv.h"

pc_set_default_dscp(PC_DSCP_EF);       // every outbound TCP connection (accepted + client)
pc_listen_set_dscp(80, PC_DSCP_AF41);  // override for one listener's connections
pc_conn_set_dscp(slot, PC_DSCP_CS6);   // re-tag ONE live connection with any class
pc_udp_set_dscp(PC_DSCP_EF);           // outbound UDP datagrams
```

A DSCP of `0` means best-effort (no marking). Convenience code points are defined
(`PC_DSCP_EF` 46, `PC_DSCP_CS6` 48, `PC_DSCP_AF41` 34, `PC_DSCP_AF31` 26); any
`0-63` value is accepted - handy for **arbitrarily tagging traffic in network
testing**, not just standards-defined classes.

This sketch marks every connection EF by default, and re-tags the `GET /tag`
connection to CS6 to show the per-flow override.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_DIFFSERV=1" \
  --lib="." examples/L4-Transport/DiffServ/DiffServ.ino
```

Flash it, then read the DSCP straight off the wire from another machine on the LAN:

```sh
sudo tcpdump -i <iface> -v host <board-ip> and tcp port 80 &
curl http://<board-ip>/       # response packets: tos 0xb8  (EF,  DSCP 46)
curl http://<board-ip>/tag    # response packets: tos 0xc0  (CS6, DSCP 48)
```

The marking applies from the connection's first data segment: the SYN-ACK stays
best-effort because lwIP emits it before any app callback and ESP32 lwIP does not
inherit the listen-pcb TOS (HW-tested). This was verified on an ESP32-P4 off the
wire: the server default marked `:80` responses tos `0xb8` (EF), a per-listener
override marked `:8080` responses tos `0x88` (AF41), and a per-connection re-tag
marked `/tag` responses tos `0xc0` (CS6).

## Annotated source

The complete sketch ([DiffServ.ino](DiffServ.ino)):

```cpp
#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/diffserv.h"

PC server;

// GET / - response inherits the server-wide default DSCP (EF) set in setup().
void handle_root(uint8_t slot, HttpReq *req)
{
    server.send(slot, 200, "text/plain", "marked EF (DSCP 46)\n");
}

// GET /tag - re-tag THIS connection to CS6 before responding, overriding the default.
void handle_tag(uint8_t slot, HttpReq *req)
{
    pc_conn_set_dscp(slot, PC_DSCP_CS6);
    server.send(slot, 200, "text/plain", "re-tagged CS6 (DSCP 48)\n");
}

void setup()
{
    // ... bring up Wi-Fi (init_wifi_physical / wifi_ready) ...

    pc_set_default_dscp(PC_DSCP_EF); // mark every outbound connection EF
    pc_udp_set_dscp(PC_DSCP_EF);     // and outbound UDP datagrams

    server.on("/", HttpMethod::HTTP_GET, handle_root);
    server.on("/tag", HttpMethod::HTTP_GET, handle_tag);
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
