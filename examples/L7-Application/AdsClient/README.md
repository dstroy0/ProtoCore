# AdsClient - read a TwinCAT PLC over Beckhoff ADS

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_ADS`

## What this example teaches

ADS (Automation Device Specification) is the protocol Beckhoff's TwinCAT PC-based
control speaks - the most common way to read and write a Beckhoff PLC over the
network. It rides on AMS (Automation Message Specification) over TCP port 48898.
`services/ads` is a pure codec: `pc_ads_build_*` produce complete on-wire frames
(AMS/TCP header + 32-octet AMS header + the command payload) and `pc_ads_parse_*`
decode the replies. The sketch owns the socket (a plain `WiFiClient`) and runs a
small sequence against a real router:

```cpp
AdsRequest r = next_request();                 // target/source AMSNetId + invoke id
size_t n = pc_ads_build_read_state(c_req, sizeof(c_req), &r);
// ...write c_req, read the AMS/TCP-framed reply into c_resp...
AdsAmsHeader h;
AdsReadStateResult st;
pc_ads_parse_ams_header(c_resp, n, &h);
pc_ads_parse_read_state(h.data, h.data_len, &st); // st.pc_ads_state == 5 -> RUN
```

**Reading a symbol by name** is the two-step ADS idiom: `ReadWrite` the symbol name
to index group `0xF003` to get a 4-octet handle, then `Read` the value from index
group `0xF005` at that handle (and `Write` the handle back to `0xF006` to release it):

```cpp
pc_ads_build_read_write(c_req, sizeof(c_req), &r, ADS_IGRP_SYM_HND_BY_NAME,
                     0, 4, (const uint8_t *)"MAIN.nCounter", 13); // name -> handle
pc_ads_build_read(c_req, sizeof(c_req), &r, ADS_IGRP_SYM_VAL_BY_HANDLE, handle, 4);
```

The codec also covers `ReadDeviceInfo`, `Write`, `WriteControl`, and device
notifications (`pc_ads_build_add_notification` / `pc_ads_parse_notification` to subscribe
to a symbol and walk the pushed samples).

## Prerequisites (real TwinCAT router)

An ADS target cannot be self-hosted from this library, so this example talks to a
real PLC. Before it will connect:

1. Set `PLC_IP` and `PLC_NET_ID` (the router's AMSNetId, e.g. `5.18.30.40.1.1`) and
   `PLC_PORT` (851 for the first TwinCAT 3 PLC runtime, 801 for TwinCAT 2).
2. The device prints its own AMSNetId at boot (its WiFi IP with `.1.1` appended).
   **Add that AMSNetId as a static route on the PLC** (TwinCAT: _Router -> Edit
   Routes -> Add_, or `Tc3AmsRemoteMgr`) - the router drops connections from an
   unknown AMSNetId.
3. Set `SYMBOL` to an `INT` / `DINT` variable that exists in the PLC (`MAIN.nCounter`).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_ADS=1" \
  --lib="." examples/L7-Application/AdsClient/AdsClient.ino
```

Flash and watch Serial @ 115200:

```
IP: 192.168.1.42
This device AMSNetId: 192.168.1.42.1.1  (add as a route on the PLC)
[ads] device: CX-1234 v3.1 build 4052
[ads] state: RUN (5)
[ads] MAIN.nCounter = 1387
[ads] done
```

## Annotated source

The complete sketch is [AdsClient.ino](AdsClient.ino). The codec itself is in
[src/services/fieldbus/ads/ads.h](../../../src/services/fieldbus/ads/ads.h); the AMS/ADS wire layout,
command ids, and state flags are verified against the Beckhoff InfoSys specification.
