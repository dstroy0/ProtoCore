# DnsServer - give the devices on your network friendly names

This makes your ESP32 a tiny **DNS server**, so other devices can say `printer.lan` instead
of remembering `192.168.1.50`. It is handy on a network with no internet (a lab bench, a
robot, an off-grid site), where the usual name lookups do not work. It is written for a
beginner.

## What is DNS, in one breath?

Computers talk to each other using **numbers** (IP addresses like `192.168.1.50`). People
prefer **names** (`printer.lan`). **DNS** is the phone book that turns a name into a number.
Normally your router or your internet provider runs it for you - but on an isolated network
there is no phone book, so nothing can find anything by name. This example gives your network
its own little phone book.

## What you will need

- An **ESP32 board** and your WiFi details.
- Another device to test from (a laptop or phone on the same network).

## Set it up

1. Open [DnsServer.ino](DnsServer.ino) and set your `SSID` and `PASSWORD`.
2. Edit the **name records** - the phone-book entries - to match your network:

    ```cpp
    pc_dns_server_add("printer.lan", 192, 168, 1, 50);   // name, then the four numbers of the IP
    pc_dns_server_add("nas.lan",     192, 168, 1, 60);
    ```

    (`esp32.lan` is added for you and points at the board itself.)

3. Upload and open the Serial Monitor at **115200**. It prints the board's IP and
   `DNS server on UDP/53`.

## Try it

You do not have to change any settings to test a single lookup - just ask this server
directly. From another computer on the network (replace `<board-ip>` with the IP from the
Serial Monitor):

```bash
nslookup printer.lan <board-ip>      # Windows / Linux / macOS
# or
dig @<board-ip> printer.lan          # Linux / macOS
```

You should get back `192.168.1.50` (or whatever you set). An unknown name returns
"NXDOMAIN", which just means "not in the phone book".

To make **every** lookup on a device go through the ESP32, set that device's DNS server to
the board's IP (in its WiFi/network settings). Then names like `esp32.lan` work everywhere on
that device. Most people only do this on a dedicated offline network.

## Troubleshooting

- **The lookup times out.** DNS uses UDP port 53. The most common cause on a home network is
  **AP/client isolation** - the router stops WiFi devices from talking to each other. Turn it
  off, or test from a device on a wired port.
- **A name comes back NXDOMAIN.** It is not in your table, or the spelling differs (matching
  is case-insensitive, but `printer.lan` and `printer.local` are different names).
- **Only A records.** This server answers IPv4 name lookups (the common case). It politely
  returns no answer for other record types (IPv6/AAAA, MX, etc.).

## Build and run (PlatformIO)

The feature lives in the library, so the flag must reach the whole build:

```bash
pio ci examples/L7-Application/DnsServer \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_DNS_SERVER=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

`pc_dns_server_add(name, a, b, c, d)` stores a `name -> IPv4` record in a small fixed table.
`pc_dns_server_begin()` binds UDP/53 through the library's transport UDP service; each query is
handed to the pure `pc_dns_server_build_response()`, which parses the question, looks the name up
(case-insensitively), and - for an A/IN query that hits - appends one answer record (using DNS
name compression: a 2-byte pointer back to the question). Misses return NXDOMAIN. Everything
is fixed-buffer and heap-free, and the wire format is unit-tested on a PC (see
`test/test_dns_server`). It is a self-contained authoritative resolver, separate from the
captive-portal DNS used during WiFi provisioning (which points every name at the setup page).
