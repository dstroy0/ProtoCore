# IPv6 - serve over IPv6 (dual-stack)

**Layer:** Foundation · **Build flags:** `PC_ENABLE_IPV6`

## What this example teaches

The server is **already dual-stack**: the TCP and UDP listeners bind `IPADDR_TYPE_ANY`, so the
moment the interface has an IPv6 address it answers over v6 as well as v4. All you add is
turning IPv6 on for the network interface:

```cpp
Physical.wifi->init(SSID, PASSWORD);
while (!Physical.wifi->ready()) delay(250);
Physical.ip6->init();            // enable IPv6 (SLAAC) on the Wi-Fi netif
while (!Physical.ip6->ready()) delay(250); // waits for a global (routable) v6 address
```

`PC_ENABLE_IPV6` gates the bring-up. `Physical.ip6->init()` enables IPv6 on the netif
(SLAAC gives a `fe80::` link-local address, plus a global one if a router advertises a prefix).
`Physical.ip6->global_addr()` reads the acquired global address straight from lwIP into a `pc_ip`.

## The pc_ip address core

`network_drivers/network/ip.h` is one family-tagged type for both v4 and v6, with:

- **`pc_ip_parse()`** - RFC 4291 text (dotted-quad; v6 with `::` zero-compression and the
  embedded-v4 `::ffff:a.b.c.d` tail).
- **`pc_ip_format()`** - the RFC 5952 canonical form (lower-case, no leading zeros, longest
  zero run compressed, v4-mapped shown dotted).
- **`pc_ip_classify()`** - loopback / link-local / private-ULA / multicast / global.

It is pure and host-tested (`pio test -e native_ip`), so the address handling is verified
off-device; the netif bring-up is ESP32-only.

## Try it

Flash, open Serial @ 115200 for the addresses, then (note the brackets and `curl -g`):

```sh
curl -g "http://[2001:db8::abcd]/"     # your printed global address
```

## Build-flag note

The flag must reach the whole library build:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_IPV6=1" \
  --lib="." examples/Foundation/IPv6/IPv6.ino
```

Requires an lwIP built with `LWIP_IPV6=1` (the stock Arduino-ESP32 core ships it). In the
Arduino IDE the `build_opt.h` beside this sketch sets the flag for you.
