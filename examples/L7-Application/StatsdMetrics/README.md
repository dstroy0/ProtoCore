# StatsdMetrics - send live numbers from your ESP32 to a metrics dashboard

This example makes your ESP32 **report measurements** - like "free memory is 210 KB" or "I
just handled a request" - to a metrics collector, so you can watch graphs of your device
over time. It is written for a beginner; no prior monitoring experience needed.

## What is a "metric", and what is StatsD?

A **metric** is just a named number you record over and over: a temperature, a count of
requests, how long something took. Collect them over time and you can draw a graph and spot
trends ("memory is slowly leaking", "traffic spikes at noon").

**StatsD** is a tiny, wildly popular format for shipping those numbers. Each metric is one
short line of text sent over the network:

```
esp32.heap.free:210000|g      <- a gauge: "the value right now is 210000"
esp32.loops:1|c               <- a counter: "add 1 to a running total"
esp32.loop.work_ms:3|ms       <- a timing: "that took 3 milliseconds"
```

The `|g`, `|c`, `|ms` on the end says what kind of number it is. That is the whole protocol.
Loads of tools understand it: **Telegraf**, **Graphite/StatsD**, **Datadog**, and more.

There are two ways to get metrics off a device: the device can wait to be **asked** (that is
the Prometheus `/metrics` example, #21), or the device can **push** them out itself. StatsD
is the push way - great when your device is behind a home router and nothing on the internet
can reach in to ask it.

## What you will need

- An **ESP32 board** + your WiFi details.
- A **collector** on your network that listens for StatsD. Easiest options:
    - **Just watch the raw lines** (no install): on any computer run `nc -u -l 8125` - it
      prints every StatsD line the ESP32 sends. Perfect for a first look.
    - **A real dashboard:** [Telegraf](https://docs.influxdata.com/telegraf/) with an
      `[[inputs.statsd]]` block feeding InfluxDB + Grafana, or the classic StatsD + Graphite.

## Set it up

1. Find your collector computer's IP address and make sure it is on the same network.
2. Open [StatsdMetrics.ino](StatsdMetrics.ino) and set `SSID`, `PASSWORD`, and
   `STATSD_HOST` (the collector's IP). Leave `STATSD_PORT` at **8125** (the StatsD standard).
3. Upload and open the Serial Monitor at **115200**. You will see `pushed metrics` every 10
   seconds.
4. On the collector, watch them arrive (`nc -u -l 8125` shows lines like
   `esp32.heap.free:210000|g|#device:esp32-demo`).

The `|#device:esp32-demo` on the end is a **tag** - an optional label (here set once in
`pc_statsd_begin`) that lets your dashboard group metrics by device. Plain StatsD collectors
ignore it; Datadog/Telegraf use it.

## Put your own numbers in

The API is one call per metric - drop these wherever something interesting happens:

```cpp
pc_statsd_count("orders.placed", 1);          // a counter (how often)
pc_statsd_gauge("tank.level_pct", level);     // a gauge (the current value)
pc_statsd_gauge_delta("connections", +1);     // nudge a gauge up or down
pc_statsd_timing("sensor.read_ms", elapsed);  // how long something took
pc_statsd_set("unique.users", clientId);      // count distinct values
pc_statsd_count_sampled("chatty.event", 1, 0.1f); // only sending 1-in-10? tell the server so
```

## Troubleshooting

- **Nothing arrives at the collector.** StatsD is fire-and-forget UDP - the ESP32 never
  knows if it landed. Double-check `STATSD_HOST` and that the collector is listening on UDP 8125. Also, many home routers **isolate wireless clients** from each other; if the
  collector is on WiFi too, put it on a wired port or disable that isolation.
- **Numbers look wrong in the graph.** A gauge (`|g`) is the value _right now_; a counter
  (`|c`) is a _running total_ the collector sums per interval - pick the right type for what
  you mean.

## Build and run (PlatformIO)

The feature lives in the library, so the flag must reach the whole build:

```bash
pio ci examples/L7-Application/StatsdMetrics \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_STATSD=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

`pc_statsd_begin(host, port, tags)` stores the target and optional global tags in fixed BSS.
Each `pc_statsd_*` call renders the value by hand (no `printf` float/64-bit formatting, which
needs extra support on some targets), builds the `name:value|type[|@rate][|#tags]` line with
the pure `pc_statsd_format()` builder, and sends it with the transport UDP service
(`Udp.client->sendto`). Zero heap; the line format is unit-tested on a PC against the StatsD spec
(see `test/test_statsd`).
