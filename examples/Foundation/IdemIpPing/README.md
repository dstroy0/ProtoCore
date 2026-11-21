# IdemIpPing - one Wi-Fi station on the idemIP stack

**Layer:** Foundation (tutorial) · **Build flags:** `-DPROTOCORE_ENABLE_IDEMIP=1` (in `build_opt.h`)

## What this example teaches

This sketch runs the idemIP stack on one Wi-Fi station. The station joins one network and takes one
static IPv4 address. It then answers ARP requests and pings at that address. The sketch uses no
esp_netif and no lwIP. The radio gives each frame to the link, and the stack reads each frame from
the link.

Set four values at the top of the sketch: `WIFI_SSID`, `WIFI_PASSWORD`, `STATION_ADDRESS` and
`STATION_MASK`. Flash the sketch, then ping `STATION_ADDRESS` from a host on the same network.

## The order in `setup()`

1. `protocore_wifi_link_start` starts the radio as a station. The radio gives its hardware address
   only after it starts.
2. `Stack.start` takes the cell of each unit of idemIP and seeds the initial sequence numbers.
3. `Stack.bind` binds the link as interface 0, with its driver, its address and its two buffer arrays.
4. `Stack.set_addr4` gives interface 0 its IPv4 address and its mask.

`loop()` calls `Stack.tick` with the clock and a random word. Each call drains the receive ring of
the link, sends each reply, and runs the services of idemIP. Once each second, the serial monitor
shows the state of the link and the frames that the ticks drained, sent and dropped.

## Memory

The link has `PROTOCORE_RX_BUFFERS` receive buffers, 32 by default, each one frame of 1536 bytes.
The count is in `vendor/board_profiles/stack_profile.h`.
