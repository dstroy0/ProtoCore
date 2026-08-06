# Benchmarks: ProtoCore vs ESPAsyncWebServer

A reproducible head-to-head against **[ESP32Async/ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer)**
(the actively-maintained successor to the me-no-dev original). Two minimal server sketches (one per
library) that are **identical except for the library**, so any difference is the server, not the setup.

## TL;DR

Measured on an **ESP32-S3** (same board, core, and `-Os` for both), from an `h2load`/`curl` client:

| Workload                             | ProtoCore            | ESPAsyncWebServer 3.11.2 | Ratio    |
| ------------------------------------ | -------------------- | ------------------------ | -------- |
| Small GET, keep-alive, 1 conn        | **277 req/s**        | 110 req/s                | **2.5x** |
| Small GET, 4 concurrent conns        | **536 req/s**        | 157 req/s                | **3.4x** |
| Small GET latency under load (`-c4`) | flat (~7 ms)         | balloons (~20 ms)        | n/a      |
| Heap used serving                    | **0 (static pools)** | per-connection heap      | n/a      |

The edge comes from **HTTP keep-alive** (persistent connections, on by default here; ESPAsyncWebServer
closes after every request) and **bounded, zero-heap concurrency**. See the honest caveats below.

## Honest caveats (read these)

- **Single-request RTT is ~equal** (both ~6.5 ms, dominated by the WiFi round trip). The device compute is
  ~135 us, invisible against the radio. The win is **connection reuse**, not per-request magic.
- **Large-transfer throughput is bandwidth-bound**, so the two are **roughly equal** there (both stream in
  constant memory; ESPAsyncWebServer's `beginChunkedResponse` filler streams fine; it only OOMs on the naive
  `send(hugeString)` API, not the streaming one).
- The `2.5x`/`3.4x` numbers need keep-alive; it is **on by default** in this library. ESPAsyncWebServer has no
  HTTP keep-alive, so it reconnects every request.
- On 2.4 GHz WiFi the **device is not the bottleneck; the radio is.** These numbers describe request
  _throughput_ (reuse + concurrency), which is where the libraries actually differ.

## Setup

|                   |                                              |
| ----------------- | -------------------------------------------- |
| Board             | `esp32-s3-devkitc-1`                         |
| Core              | `espressif32@6.13.0` (arduino-esp32 2.x)     |
| Optimization      | `-Os` (release, both)                        |
| WiFi modem-sleep  | **off** on both (`WiFi.setSleep(false)`)     |
| ESPAsyncWebServer | 3.11.2 + AsyncTCP 3.4.10                     |
| Client            | `curl` + `h2load` (nghttp2), on the same LAN |

Endpoints, identical on both: `/` (tiny), `/json` (tiny), `/4k` (4 KB body), `/64k` (64 KB chunked stream).

## Run it

```sh
# WiFi creds come from the environment - never commit them.
export BENCH_WIFI_SSID='your-ssid'
export BENCH_WIFI_PASS='your-pass'

# Build + flash each server (one board at a time - see RF hygiene below).
cd protocore && pio run -t upload && cd ..     # this library
cd eaws   && pio run -t upload && cd ..     # ESPAsyncWebServer (pio pulls it from the registry)

# The board prints its IP over serial (IP=...). Measure it:
./measure.sh 192.168.1.29 pc
./measure.sh 192.168.1.29 eaws
```

`measure.sh` reports single-request RTT (curl's internal `time_connect` / `time_starttransfer`, immune to
curl process overhead) plus keep-alive and payload throughput (`h2load --h1`).

## RF hygiene (or your numbers will be garbage)

2.4 GHz is a shared, half-duplex medium. To get stable numbers:

- **Run one board at a time.** Two ESP32s hammering the same channel congest each other, measured here as a
  196 ms ping and a 10x throughput collapse until the second board was idle.
- **Keep the client's own WiFi off the board's channel** (use a wired client, or bring the client's `wlan0`
  down so it routes over Ethernet). A third talker on the channel skews everything.
- **A quiet channel** (few neighbor APs) and **cool boards** (the ESP32-S3 WiFi PA throttles when hot after
  prolonged TX) matter. USB 3.0 storage near the boards is a known 2.4 GHz noise source.
- If `h2load` reports failures at low concurrency, the board or link is unstable; reset the board and retry.

## Note on `MAX_CONNS` and concurrency

The connection pool is bounded (`MAX_CONNS`, default 8) by design: deterministic, zero-heap. Concurrency
**at or below `MAX_CONNS - 1`** runs clean (0 failures); at concurrency **equal to the pool size** a keep-alive
reconnect can find every slot briefly occupied and is refused (correct backpressure, not a crash). Small-request
throughput plateaus at the S3's request-rate ceiling (~550 req/s) by ~4 connections, **within** the default
pool, so raising `MAX_CONNS` buys more concurrent _clients_, not more aggregate req/s. Raise it with
`-DMAX_CONNS=16` (etc.) for a connection-heavy deployment; ~3.95 KB static RAM per slot.
