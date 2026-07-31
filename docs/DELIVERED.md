# Delivered

What ProtoCore has actually shipped, kept as a feature-level record.

This exists because [ROADMAP.md](ROADMAP.md) is for work that is **not done yet**. Mixing the two made the
roadmap unreadable: 14 of its 17 sections held no open items at all, so the plan was buried under its own
history.

Two neighboring documents cover different questions, and this one should not duplicate either:

- **[FEATURES.md](FEATURES.md)** - what the library _does_, as a reference grid. The answer to "can it do X".
- **[CHANGELOG.md](CHANGELOG.md)** - _when_ each change landed, generated per commit. The answer to "when did X change".
- **This file** - what was _undertaken and finished_, at the granularity a roadmap item was written in. The
  answer to "was X ever built, and what did it involve".

Sections below are moved verbatim from the roadmap, so the wording is as it was written while the work was
planned and then marked done.

---

## Recommended near-term order (value vs. risk)

1. **Quick wins (all done):** Cache-Control beside ETag, runtime build-flag
   endpoint (`server.diag()`), MAC-derived UUID, raw-UDP telemetry cast.
2. **Security hardening cluster:** IP allowlist + brute-force lockout + CSRF
   tokens - all shipped.
3. **Partition-map monitor** (done) + config export / restore - the latter still
   needs NVS enumeration added to the config store first.
4. **Dashboard configurator** - done: telemetry math, the SVG dashboard over SSE,
   and the WebSocket controls + Canvas chart.
5. **Architectural (deliberate):** egress-interface reporting done (the stack
   already owns failover); next Ethernet PHY, GraphQL, OPC UA.
6. **Multi-vendor portability (L, greenlit):** partition every silicon-specific
   layer by vendor so the library runs beyond ESP32 (STM32 next, then TI / RP
   and others). Own architectural track below.

## Concurrency / performance

- [x] Thread-safe transport boundary _(shipped)_ - the cross-thread connection
      fields (state, RX ring head/tail) are `pc_atomic` (acquire/release), so the
      single-producer/single-consumer ring is race-free by construction; proven
      under ThreadSanitizer in CI (env `native_tsan`).
- [x] Dedicated server task _(shipped)_ - `begin()` runs the pipeline in its own
      pinned FreeRTOS task (Core 1 by default) instead of the user's `loop()`,
      which is freed; this also removes the first-connection-after-idle stall that
      came from `loop()`-cadence coupling.
- [x] Core-partitioned parallel workers _(shipped)_ - `PC_WORKER_COUNT` (default 1) workers each own a disjoint set of connection slots (round-robin at
      accept) with their own event queue + scratch arena: shared-nothing, no
      hot-path locks, so both cores process disjoint connections in parallel with
      bounded latency (determinism preserved). N=1 is byte-for-byte the original
      single pipeline.
- [x] Thread-safe app push path _(shipped)_ - `PC::defer(slot, fn, arg)`
      runs a callback in the slot's owning worker, so application code on `loop()`
      or another task can push (SSE/WS sends) without racing the worker.
- [x] Throughput benchmark _(measured)_ - a CPU-bound handler (~0.2s) under 4-way
      concurrency: N=1 ~5.9 req/s vs N=2 ~9.1 req/s (~1.5x; not a full 2x because
      worker 1 shares Core 0 with WiFi/lwIP), single-request latency unchanged.
      Confirms real parallel scaling with determinism intact (no hot-path locks).
- [x] Pluggable monotonic clock _(shipped)_ - `services/clock.h`: all library
      timing flows through `pc_millis()` (a single 1000 Hz source). Feed your own
      clock with `pc_set_clock(fn, ticks_per_second)` and it is divided down to
      the internal 1000 Hz, so timeouts/polling keep the tested 1 ms granularity
      whatever your clock's rate; default is the platform `millis()` (host-tested).
- [x] Notification-driven worker drain _(shipped)_ - the worker blocks on its
      FreeRTOS task notification instead of free-running the `vTaskDelay` poll;
      producers (`listener_enqueue`, `pc_defer`) nudge it the moment work is
      queued, so events are serviced immediately. This decouples event latency from
      the idle-sweep cadence: `PC_WORKER_POLL_TICKS` is now purely the idle
      interval (default 1, unchanged), so raising it cuts idle wakeups (CPU/power)
      at no latency cost (HW: GET latency identical at `POLL_TICKS` 1 vs 100). Zero
      heap (notifications, not a QueueSet).
- [x] Tuning _(shipped)_ - leaner tcpip recv callback (a received segment is now
      bulk-copied into the slot ring with a single SPSC publish instead of a
      per-byte loop with a per-byte atomic store), and a worker count / core /
      poll-knob tuning guide with the measured latency and idle-wakeup data in
      [TUNING.md](TUNING.md).

### v5 milestone: user-configurable preempting task queue + hardware ingest

The next big concurrency step (the **v5 milestone**): turn the worker model into a
real-time, hardware-ingest pipeline where data lands in a queue and the scheduler
preempts immediately to process it, with the priorities exposed to the user.

- [x] \*Preempting task queue (L) _(shipped)_ - `PC_ENABLE_PREEMPT_QUEUE`:
      `services/preempt_queue`, static (zero-heap) queues feeding dedicated
      core-pinned tasks. **From a task**, `pc_pq_post()` /
      `pc_pq_post_urgent()` (`xQueueSendToBack` / `xQueueSendToFront`) with a
      wait timeout; the scheduler preempts the lower-priority producer the instant
      the item is queued. **From an ISR**, `pc_pq_post_from_isr()`
      (`xQueueSendFromISR` + `portYIELD_FROM_ISR`) switches right after posting,
      not on the next tick. Fail-closed on a full queue. **Named lanes**: one USER
      lane exposed to the app plus internal DMA / FORWARD / DEVICE lanes that run
      above it (DMA highest, below tcpip / WiFi), so internal ingest preempts user
      work without starving networking. HW-verified (~12 us ISR-to-handler latency;
      DMA + USER lanes ran continuously with zero errors under an HTTP flood);
      host-tested via the per-lane ring core (examples Foundation/PreemptQueue +
      PreemptLanes).
- [x] \*User-configurable task priorities / affinity (M) _(shipped)_. Each lane's
      task priority + core pinning are set at `pc_pq_start[_lane]()` and the queue
      depth is compile-time (`PC_PQ_DEPTH`); the no-arg API drives the USER lane so
      users own their task priorities, while internal lanes default above the user lane.
- [x] \*DMA UART / I2C / SPI transfer (L) _(shipped)_ - `PC_ENABLE_DMA`:
      `services/dma`, channels moving peripheral bytes to a static ping-pong (RX) /
      staging (TX) buffer while the CPU is free; a DMA-complete event carries the
      bytes to a callback that posts them into the preempting queue. The ingest path
      for the cheap-breakout field-bus codecs (CAN over SPI, RS-485 UART, IO-Link).
      Zero-heap, fail-closed. The ingress/egress **simulator** (`PC_DMA_SIMULATE`,
      default on) runs the whole pipeline with no physical loopback - host bench and
      on-device; a real silicon driver plugs into `pc_dma_hw_*`. Host-tested
      (`native_dma`) + HW-verified (2.2M+ frames, zero integrity errors, under HTTP
      stress). Remaining: the UHCI-UART / `spi_master`-DMA silicon backend.
- [x] \*Deeper clock-awareness (M) _(shipped)_ - `services/pc_clock` gains a
      microsecond time base beside the 1000 Hz `pc_millis()`: `pc_micros()`
      (pluggable like the ms clock, ISR-safe, for hardware-event timestamps) plus a
      `pc_latency_stat` budget helper (`pc_lat_begin` / `_end` / `_avg_us`:
      count, min/max/mean, over-budget count). Header-only, zero heap. HW-verified
      measuring the preempting queue's ISR-to-handler latency against a budget
      (avg 12 us, 0 over a 50 us budget). Host-tested in `native_clock`.
- [x] \*Interface forwarding (L), **DMA-driven** _(shipped)_ - `PC_ENABLE_FORWARD`:
      `services/forward`, a forwarding plane over the ingest pipeline. Register
      interfaces (Wi-Fi STA / AP, Ethernet, a peripheral bus / radio), each with an
      egress send callback, then add per-pair rules (`src -> dst`, allow / deny, rate
      cap). A frame arriving on one interface (`pc_forward_ingress()`, wired from a
      DMA-complete event on the FORWARD lane) is forwarded to every allowed
      destination, so the device bridges / routes instead of only terminating traffic.
      Default-deny, never reflects to the source, fail-closed (exceeded cap / refused
      send drops and is counted, never blocks); multi-destination fan-out for hub
      behavior. Zero-heap static tables. Host-tested (`native_forward`) + HW-verified
      (600k+ frames ingested over DMA and forwarded with zero loss / zero integrity
      errors under an HTTP flood; example Foundation/InterfaceForward). This is the
      generic data path the wireless gateway bridges below sit on top of.

### post-v5: RF / wireless gateway bridges

Once the hardware-ingest pipeline lands (the preempting queue + DMA peripheral
transfers above), the same codec-to-northbound pattern that bridges the wired field
buses (CAN over SPI, RS-485 / IO-Link over UART) extends to **wireless radios**: each
radio is a southbound peripheral reached over **SPI / I2C / UART**, and a gateway
function bridges its frames northbound to the existing WiFi / MQTT / HTTP / WebSocket
stack. Deterministic, zero-heap, fixed buffers; the DMA-complete / data-ready ISR posts
into the preempting queue, so wireless ingest rides the same real-time path as the wired
codecs. (ESP32 Wi-Fi / classic + BLE are already on-chip; everything below is an
external module we wire to a bus.)

The **generic gateway framework is shipped** (`PC_ENABLE_GATEWAY`, `services/gateway`):
ports, address-aware northbound enveloping + topic (`pc_gateway_uplink` / `pc_gateway_topic`),
bidirectional up/down-link, a per-port rate cap, and stats - all fail-closed and zero-heap,
HW-verified end to end over DMA + the FORWARD lane. Each radio below is now a per-module
**codec + driver** (frame parse, SPI/UART register access) plugged into that framework's
callbacks; the items stay open until verified against the real hardware.

SPI radios:

- [x] \*Thread / Matter RCP (L) - OpenThread radio co-processor (nRF52840 / EFR32 / ESP32-C6)
      over UART (spinel framing); 802.15.4 mesh bridged to IP. **Complete** (`PC_ENABLE_THREAD`,
      `services/thread`): the byte-stuffed HDLC-lite framing + CRC-16/X-25 FCS (encode/decode) that
      carries spinel, the spinel command layer (packed-uint encoding + `pc_spinel_command_build` /
      `_parse` for a `header | CMD | PROP | value` command), **and the spinel property registry +
      typed value semantics** - `SpinelProp` / `SpinelStatus` id + status tables, `pc_spinel_prop_lookup`
      / `_name` / `pc_spinel_status_name`, and a `SpinelReader` / `SpinelWriter` cursor with an accessor
      per spinel datatype (bool/u8/i8/u16/i16/u32/i32/packed-uint/EUI64/IPv6/UTF8/data/len-data). Host-tested
      (26 cases: X-25 check value, packed-int KATs, typed round trips, registry lookups) and verified
      against a real RCP two ways: **HW-verified on an ESP32-C6 running ESP-IDF `ot_rcp`** (a native
      802.15.4 radio) - the codec drove a spinel RESET and decoded **8/8** properties off the wire
      (NCP_VERSION `openthread-esp32/...; esp32c6; ...`, PROTOCOL_VERSION 4.3, INTERFACE_TYPE 3, CAPS
      including a multi-byte packed 513, the factory HWADDR EUI64 `ACEBE6FFFEC1DE00`, PHY_CHAN 11,
      PANID, LADDR); and **interop-verified against the OpenThread reference RCP** (OpenThread's own
      `ot-rcp` over a pty, 7/7) for conformance against the reference implementation. Note stock
      `ot_rcp` puts spinel on UART0's GPIO pins, unreachable on a devkit whose only host link is the
      USB-Serial-JTAG; for the C6 test its spinel fd was pointed at `/dev/usbserjtag` so the stream
      rides USB - a transport change on the peer, not a protocol change. Example ThreadGateway decodes
      named properties and bridges a real RCP.

UART radios:

### post-v5: promiscuous / monitor capture

A read-only capture mode across the same interfaces: instead of joining a network and
terminating traffic, listen to **every** frame on a channel and surface it northbound
(live over WebSocket, or batched to a PCAP / log) for diagnostics, an on-device IDS, and
field debugging. Capture is strictly passive (no injection on the capture path) and
fail-closed: a full capture queue drops frames rather than stalling the live data path.

- [x] \*Wi-Fi promiscuous / monitor mode (M) _(shipped, v4.84.0)_ - on-chip ESP32 raw 802.11
      capture (`PC_ENABLE_PROMISC`, `services/promisc`): `pc_promisc_begin(channel, sink)` over
      `esp_wifi_set_promiscuous`, a pure `wifi_frame_parse()` 802.11 header decoder (to/from-DS
      src/dst/bssid, QoS, WDS), and libpcap `DLT_IEEE802_11` framing. The sink feeds the
      forwarding plane, so captured frames bridge to another interface - **capture on Wi-Fi,
      forward to Ethernet** to a wired PCAP collector (example WifiCapture). Host-tested
      (`native_promisc`); both cores compiled. Northbound-over-WebSocket is a follow-on wiring.
- [x] \*Bus listen-only capture (M) _(CAN shipped, v4.85.0)_ - `PC_ENABLE_BUS_CAPTURE`
      (`services/bus_capture`): the TWAI controller in listen-only mode decodes every CAN frame
      without ACKing and feeds the forwarding plane, so captured frames bridge to another
      interface. `can_to_socketcan()` + libpcap `DLT_CAN_SOCKETCAN` make the stream
      Wireshark-readable (example CanCapture, host-tested `native_bus_capture`; the SocketCAN
      framing shares `shared_primitives/pcap.h` with the Wi-Fi capture). Remaining: RS-485
      receive-only decode into the same sink.

### post-v5: field-perturbation sensing

Sensors that read the environment by measuring a perturbation in an emitted or ambient
field, the same southbound-peripheral pattern pointed at sensing instead of comms: the
device reads the sensor over SPI / I2C / UART and bridges the readings northbound to the
dashboard, telemetry math, and MQTT / WebSocket. The data-ready ISR posts into the
preempting queue, so sensing shares the real-time ingest path.

## Web / API / UI

- [x] WebSocket permessage-deflate, inbound and outbound _(shipped)_ - bounded fixed-Huffman DEFLATE compresses server-to-client data frames (RSV1), with an uncompressed fallback when the result would not shrink.
- [x] REST substrate, AJAX _(shipped)_.
- [x] Real-time **dashboard** _(shipped, phase 1)_ - `PC_ENABLE_DASHBOARD`: a compile-time `pc_widget` table served as hand-rolled SVG (gauge / value / bar / sparkline, no external JS) via the `web` pipeline, live over SSE; `pc_dashboard_set` / `_publish` (example Dashboard). **Flagship.**
- [x] Dashboard phase 2 _(shipped)_ - WebSocket controls (button / toggle / slider widgets send values to a `pc_dashboard_on_control` callback) and a Canvas chart widget for dense series.
- [x] **Telemetry math** cluster _(shipped)_ - `services/telemetry`: moving-window stats (mean / variance / stddev / min / max), a rate-of-change tracker, and a trapezoidal run-time totalizer (example Telemetry).
- [x] HTTP caching: `Cache-Control` beside ETag _(shipped)_ - `set_cache_control()` injects it into serve_file / serve_static responses.
- [x] HTTP delivery (S-M): stale-while-revalidate, service-worker cache injection, delta/offset log fetching
      _(shipped)_ - `PC_ENABLE_HTTP_DELIVERY` (`services/http_delivery`). **Stale-while-revalidate:** the
      RFC 5861 `pc_delivery_swr` freshness decision + `pc_delivery_cache_control` header, wired into serving
      by `PC::set_cache_control_swr(max_age_s, swr_s)` so every `serve_file` / `serve_static` response carries
      `public, max-age=N, stale-while-revalidate=M` built by the same core that makes the decision.
      **Service-worker cache injection:** `pc_delivery_sw_manifest` emits the versioned
      `{"version":..,"precache":[..]}`, and `pc_delivery_serve_sw()` registers `/sw.js` (a real worker shipped
      through the web-asset pipeline that precaches the shell and serves it stale-while-revalidate client-side,
      cache named after the version so a bump invalidates once) plus `/precache.json` (rebuilt per request;
      answers 500 rather than truncating past `PC_DELIVERY_MANIFEST_BUF`). **Delta/offset log fetching** is
      `server/http_range.h` (`PC_ENABLE_RANGE`), the single owner of the RFC 7233 range math already wired into
      file serving and the edge cache - the duplicate parser that had sat in this service was removed rather
      than given a second call site. Host-tested (`native_http_delivery`) and **HW-verified on an ESP32-P4
      serving from SD**: `bytes=10-19`, `bytes=-5`, and `bytes=995-` each returned byte-exact 206 payloads with
      the correct `Content-Range`, `bytes=5000-6000` returned 416 `bytes */1000`, served files carried the SWR
      header, and `/sw.js` + `/precache.json` served correctly. Example HttpDelivery.
- [x] CBOR encoder + decoder _(shipped)_ - `PC_ENABLE_CBOR`: a zero-heap RFC 8949 writer plus a cursor decoder (`pc_cbor_peek` / `pc_cbor_read_*`, no-copy strings) over caller buffers - ints / strings / bytes / arrays / maps / bool / null / float; host-tested against the spec vectors + round-trip (example Cbor).
- [x] MessagePack encoder + decoder _(shipped)_ - `PC_ENABLE_MSGPACK`: a zero-heap streaming writer over a caller buffer - shortest-form ints (fixint / 8 / 16 / 32 / 64) / str / bin / arrays / maps / bool / nil / float32; overflow tracked, fails closed - plus a cursor decoder (`pc_msgpack_peek` / `pc_msgpack_read_*`, no-copy strings, fail-closed on malformed/truncated input, ext reported as INVALID) host-tested against spec vectors + round-trip and fuzzed in the pentest harness (example MsgPack, both directions). Remaining (M-L): Protobuf / FlatBuffers zero-copy.
- [x] GraphQL bounded subset _(shipped)_ - `PC_ENABLE_GRAPHQL`: `services/graphql` parses a query into a fixed AST pool (no heap) and emits `{"data":{...}}` shaped by the selection; schema-free (sub-selection = object, leaf = one resolver call, args collected along the path), with nesting + arguments (example GraphQL). Feature-dependent schema generation remains open (M).
- [x] Browser diag tools _(shipped, GPIO mapper)_ - `PC_ENABLE_GPIO_MAP`: a compile-time pin table (number / label / direction / live level) served at GET `/gpio` as JSON, with a POST control (`pin`, `level`) that drives a mapped output; the serializer + control parser are host-tested, and the example ships a zero-dependency browser panel (example GpioMap). Remaining (M): ping / tracert panel, web logic analyzer.
- [x] **SPA micro-routing** + conditional UI streaming (M) _(shipped, HW-verified)_ -
      `PC_ENABLE_SPA_ROUTER`: `services/spa_router` `pc_spa_route()` decides, from a request path,
      whether to serve a real asset file, serve the SPA shell (`index.html`) for an extensionless
      client-side route, or pass through to the app under an API prefix - so a single-page UI's client
      routing works over static file serving. Pure decision core, host-tested (`native_spa_router`).
      **The local SCADA/HMI fallback** is `pc_spa_route_ex()` + a `pc_spa_ctx`: when the shell asset
      is missing, the client will not run scripts, or the device is degraded, a client route resolves
      to `PC_SPA_SERVE_FALLBACK` - a no-JavaScript control page. The API prefix keeps passing through
      in that mode (the fallback's own controls post to it) and asset requests are never rewritten.
      **Conditional UI streaming** is `pc_ui_fragment` + `pc_ui_stream_next()`: a fragment table with
      per-panel predicates, emitted into a buffer of any size (it resumes mid-fragment, so the page
      never has to fit RAM), with predicates evaluated as the stream reaches each fragment. Pure,
      host-tested (`native_spa_router`, 16 cases incl. identical output across chunk sizes 1..40).
      HW-verified on an **S3**: shell when healthy; fallback for `?nojs=1`, a missing shell, and a
      degraded device; and actuating via the still-live API in fallback mode flipped two panel
      predicates in opposite directions on the next render. Example SpaFallback.
- [x] WS MTU-aligned chunking / fragmentation control (M) _(shipped)_ - `PC_WS_FRAG_SIZE` (0 = off,
      default) / the runtime `ws_set_frag_size()`: an outbound data message longer than the set payload
      size is split into that-sized WebSocket frames (RFC 6455 sec 5.4: opcode+RSV1 on the first,
      CONTINUATION on the rest, FIN on the last), so each frame maps to whole TCP segments (MTU-aligned)
      and a peer with a bounded per-frame reassembly buffer can receive an arbitrarily long message.
      Compression (RFC 7692) applies to the whole message first, then the compressed bytes are split.
      Host-tested (`test_ws_outbound_fragmentation`); default 0 keeps the single-frame behavior unchanged.

## Protocols & integrations

- [x] OPC UA server + client _(shipped, SecurityPolicy None)_ - `PC_ENABLE_OPCUA`: the OPC UA Binary built-in-type codec, UA-TCP (UACP) framing, Hello/Acknowledge handshake, OpenSecureChannel, CreateSession/ActivateSession, GetEndpoints, Read/Write (Variant/DataValue), and Browse, served on PROTO_OPCUA (`listen(4840, PROTO_OPCUA)`; example OpcUa), plus an OPC UA client (example OpcUaClient). Host-tested (`native_opcua`, `native_opcua_client`) and HW-verified end-to-end against the `asyncua` reference stack via the interop harness (`opcua-client` 3/3: connect+session, browse Objects, read node). Remaining (L): a secure SecurityPolicy (Basic256Sha256 encryption/signing), subscriptions / monitored items, and Node-RED integration (M).
- [x] Modbus master codec + register scanner _(shipped)_ - `PC_ENABLE_MODBUS_MASTER`: `services/fieldbus/modbus/pc_modbus_master` builds read-request ADUs and parses responses (register values or exception) so an app can poll / auto-discover a slave's registers; pure, host-tested as a full round-trip against the slave codec, HW-verified via self-scan (example ModbusScan).
- [x] Webhooks + IFTTT _(shipped)_ - `PC_ENABLE_WEBHOOK`: `services/webhook` builds an IFTTT Maker URL + value1/2/3 JSON payload (pure, host-tested with JSON escaping) and fires it - or any JSON to any URL (Slack/Discord/your API) - via the outbound http_client; HW-verified by a self-loopback POST (example Webhook).
- [x] Email + SMS fallbacks (SMTP + gateway) (M) _(shipped, HW-verified)_ - **SMTP shipped** (`PC_ENABLE_SMTP`): `services/smtp` runs a blocking RFC 5321 send over the shared `pc_client` transport (EHLO, optional AUTH LOGIN, MAIL/RCPT/DATA with dot-stuffing, QUIT), with implicit TLS (SMTPS, port 465) when the message sets `tls` and `PC_ENABLE_TLS` is on; zero heap, fixed buffers. The dialogue engine (`smtp_run`) is split behind a send/recv seam and host-tested against a scripted mock server (`native_smtp`, 11 cases: happy path, AUTH, dot-stuffing, multi-line replies, every error). SMS needs no new code (email-to-SMS carrier gateway address). Example SmtpAlert ships a full beginner walkthrough that stands up a Postfix server. **STARTTLS is shipped** (`PC_ENABLE_SMTP_TLS`, RFC 3207): `SmtpConfig.security` became a `SmtpSecurity` enum (PLAIN/TLS/STARTTLS), the engine upgrades in band after the first EHLO and reissues EHLO per sec 4.2, and it fails closed with `SMTP_ERR_NO_STARTTLS` if the server does not advertise it - a stripped capability cannot downgrade the exchange into sending AUTH in the clear (8 new host cases in `native_smtp`, 30 total). **The live over-the-wire send is done too**: the old blocker was reaching an _external_ relay past ISP port-25 filtering, which a LAN submission server sidesteps entirely - an ESP32-S3 delivered to a live aiosmtpd on the RPi with the server logging `DELIVERED ... bytes=183 tls=True`. That run fixed three latent bugs in a TLS path that had never once been compiled: a missing `<mbedtls/ssl.h>` include, SMTP missing from the `PC_ENABLE_CLIENT_TLS` derivation (so the session API resolved to stubs that always fail), and BIO callbacks dereferencing a `ctx` that `pc_tls_client_session_begin()` never passes - which crashed on the first handshake flush.
- [x] ESP-NOW peer messaging _(shipped)_ - `PC_ENABLE_ESPNOW`: `services/espnow` adds a typed 3-byte envelope (demux by type + length check) over connectionless ESP-NOW, a bounded peer registry, and the esp_now radio binding (begin / add_peer / send / broadcast, decoded frames to a callback for bridging to WS/SSE) (example EspNow). Codec + registry host-tested; ESP-NOW<->MQTT auto-bridge remains open (M).

## Networking / connectivity

- [x] Egress-interface reporting _(shipped)_ - `pc_net_egress()` / `pc_net_egress_ip()` read the live lwIP default route so the app always knows which interface (WiFi STA / softAP / Ethernet) its traffic is leaving on; the stack owns the actual failover, so no manager or polling tick is added (example NetEgress). - RNG stays out of the pure core) and `pc_ike_initiator_on_sa_init` consumes the responder's
      reply, captures the responder SPI, and derives the SA keys, failing closed to `IKE_ST_FAILED` on a
      wrong-SPI / non-response / wrong-state message. Driven against a hand-built responder message it
      establishes the same keys the responder derives. **The initiator's IKE_AUTH send is shipped too**:
      `pc_ike_initiator_build_auth_psk` emits SK{ IDi | AUTH } - it builds the IDi payload, computes the
      §2.15 PSK AUTH over the stored RealMessage1 + Nr + prf(SK_pi, IDi'), and wraps the pair in the SK
      envelope keyed by SK_ei (the context now also stores RealMessage1 + Nr for exactly this). Decrypting
      the emitted message recovers IDi | AUTH and the AUTH matches an independent recomputation. **The
      initiator handshake is now COMPLETE**: `pc_ike_initiator_on_auth_psk` decrypts the responder's
      SK{ IDr | AUTH } with SK_er and verifies its AUTH over RealMessage2 | Ni | prf(SK_pr, IDr') in
      constant time, reaching `IKE_ST_ESTABLISHED` (the context stores RealMessage2 for this). A full
      initiator<->responder handshake driven end to end (IKE_SA_INIT + IKE_AUTH) reaches ESTABLISHED
      with mutual PSK auth, and a responder signing with the wrong key is rejected to IKE_ST_FAILED
      with mutual PSK auth, and a responder signing with the wrong key is rejected to IKE_ST_FAILED.
      **The responder IKE_SA_INIT is shipped too**: `pc_ike_responder_on_sa_init` consumes the
      initiator's request, emits the IKE_SA_INIT response, and derives keys - a real two-driver
      exchange (both the initiator and responder drivers) reaches identical SK_\* on both sides
      exchange reaches identical SK_\* on both sides. **The responder IKE_AUTH completes the state
      machine**: `pc_ike_responder_on_auth_psk` decrypts the initiator's SK{ IDi | AUTH }, verifies
      its AUTH, then builds + returns the responder's SK{ IDr | AUTH }, reaching ESTABLISHED. **A FULL
      bidirectional handshake now runs end to end** - the initiator and responder drivers exchange
      IKE_SA_INIT + IKE_AUTH through buffers, both mutually authenticate by PSK and reach ESTABLISHED
      with identical keys, and a wrong-key peer is rejected on either side (`test_ikev2` now 66). The
      IKE handshake state machine (item 2) is functionally complete for the PSK path. **The
      INFORMATIONAL exchange is shipped too** (RFC 7296 §1.4): `pc_ike_informational_build` /
      `_open` wrap Dead-Peer-Detection (an empty request), Delete, or Notify payloads in an SK{}
      message over an established SA, keyed by the sender's egress direction (SK_ei from the
      initiator, SK_er from the responder) with the INITIATOR/RESPONSE flags set independently -
      exercised end to end with a DPD keepalive both ways and a Delete(IKE) round-trip, and a
      wrong-direction key is rejected. **CREATE_CHILD_SA is shipped too** (RFC 7296 §1.3 / §2.17):
      `pc_ike_create_child_sa_build` wraps the SA | Ni | Nr | [KE] | TSi | TSr inner chain in the SK
      envelope, and `pc_ike_child_keymat` derives the Child-SA ESP keys KEYMAT = prf+(SK_d, [g^ir |]
      Ni | Nr) (with or without a PFS D-H), cross-checked against a Python reference (`test_ikev2` now
      69). Every IKEv2 exchange family (IKE_SA_INIT / IKE_AUTH / INFORMATIONAL / CREATE_CHILD_SA) is
      now implemented. **RSA certificate auth is shipped too** (RFC 7296 §2.15 / RFC 7427):
      `pc_ike_auth_verify_rsa_sha256` verifies a peer's RSA-2048 PKCS#1 v1.5 SHA-256 AUTH signature
      over the signed octets against its CERT public key (n, e) via the library's `ssh_rsa_verify` -
      the device signs with its own ECDSA-P256 key, so this covers authenticating an RSA-certificated
      peer, cross-checked against a Python-signed vector with tamper rejection (`test_ikev2` now 70).
      The IKE handshake state machine (item 2) is essentially complete: all four exchanges, PSK +
      ECDSA + RSA auth. **The IKE-SA rekey key schedule is shipped too** (RFC 7296 §2.18):
      `pc_ike_rekey_derive_keys` derives the new SA's SK_\* from SKEYSEED = prf(SK_d(old), g^ir(new)
      | Ni | Nr) then prf+ over the new SPIs - distinct from the initial schedule (the old SK_d is the
      PRF key), cross-checked against a Python reference (`test_ikev2` now 71). **NAT traversal
      detection is shipped too** (RFC 7296 §2.23 / RFC 3947, `services/security/ikev2/ikev2_natt`): the
      NAT-detection hash `SHA-1(SPIi | SPIr | IP | Port)` (`pc_ike_natd_hash`, cross-checked against a
      Python `hashlib.sha1` vector for IPv4 and IPv6), the `NAT_DETECTION_SOURCE_IP` /
      `NAT_DETECTION_DESTINATION_IP` Notify builders over the tier-1 notify codec (round-tripped through
      `pc_ike_notify_parse`), the detection logic (`pc_ike_natd_peer_behind_nat` /
      `_self_behind_nat` recompute the hash over the observed vs. local address and flag a translated
      source / destination), and the RFC 3948 UDP-4500 demux (`pc_natt_is_keepalive` for the single
      0xFF keepalive, `pc_natt_is_ike` for the 4-octet Non-ESP Marker that separates IKE from ESP) -
      `test_ikev2_natt`, 4 cases. **The Configuration payload is shipped too** (RFC 7296 §3.15,
      `pc_ike_cp_build` / `_parse` + the `IkeCfgAttrIter` attribute walker): a CFG_REQUEST /
      CFG_REPLY / CFG_SET / CFG_ACK payload carrying Configuration Attributes (INTERNAL_IP4_ADDRESS,
      INTERNAL_IP4_NETMASK, INTERNAL_IP4_DNS, INTERNAL_IP4_SUBNET, and the IPv6 forms) - the VPN
      address-assignment mechanism - with the 15-bit-type / length / value encoding and empty-value
      requests, byte-exact against a hand-computed golden and round-tripped through the attribute
      iterator, including a truncated-attribute guard (`test_ikev2` now 74). **Message fragmentation
      is shipped too** (RFC 7383): the `IKEV2_FRAGMENTATION_SUPPORTED` notify, the SKF (Encrypted
      Fragment, payload 53) envelope `pc_ike_skf_build` / `_parse` (generic header | Fragment Number |
      Total Fragments | IV | ciphertext | ICV, with the 1..Total numbering enforced), and a
      caller-buffer reassembler (`IkeFragReasm` + `pc_ike_frag_reasm_add` / `_complete` / `_assemble`)
      that stages out-of-order fragment plaintexts and concatenates them 1..Total, rejecting a
      duplicate, a disagreeing Total, an over-cap fragment count, and a pool overflow - host-tested for
      SKF build/parse round-trip, out-of-order reassembly, and every guard (`test_ikev2` now 77).
      **The anti-DoS COOKIE is shipped too** (RFC 7296 §2.6): the stateless-cookie construction
      `version | SHA-256(Ni | IPi | SPIi | secret)` (`pc_ike_cookie_compute` / `_verify` +
      `pc_ike_cookie_notify_build`) that lets a flooded responder stay stateless - it replies with
      only a COOKIE notify and recomputes + constant-time-verifies the cookie on the retry with no
      per-initiator state, so a spoofed source that cannot receive at its claimed address cannot forge
      one; KAT'd against a Python SHA-256 vector with wrong-secret / wrong-IP / wrong-SPI / tamper /
      bad-length rejection (`test_ikev2` now 80). **IKEv2 tiers 1-2 are done**: the full wire codec,
      every exchange, all three auth methods, the initial / Child-SA / rekey key schedules, NAT-T
      detection, the Configuration payload, RFC 7383 fragmentation, and the anti-DoS cookie. The only
      remaining IKEv2 work is **tier 3, the ESP datapath** (RFC 4303) - and its host-testable core
      (packet transform + anti-replay + SAD/SPD) is now shipped, leaving only the lwIP IP-hook
      (device-side, not host-unit-testable). 3. **ESP datapath** (XL, the genuinely hard part - architecturally invasive) - RFC 4303 ESP packet
      encapsulation is a **network-layer transform**, not an app service: it must hook lwIP's IP input/output
      (a custom netif or an ip4/ip6 hook) to encrypt/decrypt + (anti-replay) sequence every datagram, with
      the SAD/SPD. **The ESP PACKET TRANSFORM is shipped** (`services/esp`, gated on `PC_ENABLE_IKEV2`) -
      the pure, host-testable crypto core, separated from the device-side lwIP integration:
      `pc_esp_gcm_encapsulate` / `_decapsulate` build + verify an RFC 4303 ESP packet with AES-256-GCM
      (RFC 4106) - `SPI | Seq | IV | AES-GCM(payload | pad | PadLen | NextHeader) | ICV`, the SPI|Seq AAD,
      the salt||IV nonce, and RFC 4303 §2.4 padding - cross-checked against a Python AES-256-GCM reference
      with encapsulate/decapsulate round-trips, a golden packet, tamper (ciphertext + AAD) rejection, and
      an empty-payload case. **The anti-replay window is shipped too** (RFC 4303 §3.4.3): a zero-heap
      64-packet sliding window (`EspReplay` + `pc_esp_replay_init` / `_check`) accepts a fresh sequence
      number, rejects a duplicate inside the window (replay) or one left of it (too old), and slides on a
      new highest - host-tested for in-order / out-of-order / replay / window-slide / large-jump / seq-0
      (`native_esp`, 7 cases). **The SAD + SPD are shipped too** (RFC 4301, `services/system/esp/ipsec_db`,
      gated on `PC_ENABLE_IKEV2`) - the security-decision core the datapath consults per packet, all pure
      host-testable data structures: an ordered Security Policy Database with first-match-wins lookup over
      source / destination / protocol / port selector ranges (`pc_ipsec_spd_lookup`, PROTECT / BYPASS /
      DISCARD, default-deny on no match), an SPD selector built straight from an IKEv2-negotiated TSi/TSr
      pair (`pc_ipsec_selector_from_ts`, RFC 4301 §4.4.1), and an SPI-keyed Security Association Database
      (`pc_ipsec_sad_add` / `_find` / `_remove`) with per-SA outbound sequence allocation
      (`pc_ipsec_sad_next_seq`, pre-increment from 1, refuses to wrap) and a bound inbound anti-replay
      window - host-tested for selector edges, policy ordering, TS bridging, SPI demux, duplicate / full /
      remove, and sequence exhaustion (`native_ipsec_db`, 7 cases). The **entire host-testable ESP datapath
      is now done** (packet transform + anti-replay + SAD/SPD). _Remaining (the last piece, device-side /
      not host-unit-testable):_ the lwIP IP input/output hook that walks packets through these lookups;
      tunnel mode first (whole-packet, simplest SPD). Refs: RFC 7296 (IKEv2), 4301 (security architecture /
      SAD / SPD), 4303 (ESP), 4106 (AES-GCM in ESP), 7634 (ChaCha20-Poly1305 for IKEv2/ESP), 8247
      (algorithm requirements).
- [x] WiFi (M): sniffer / traffic analyzer / RF diag, channel-agility roaming _(shipped)_ -
      `PC_ENABLE_WIFI_SNIFFER` (`services/wifi_sniffer`): `pc_wifi_parse` decodes an 802.11 MAC header
      (frame-control type/subtype + flags and the addresses whose roles depend on the ToDS/FromDS bits),
      `pc_wifi_stats_*` tallies frames by type for a traffic panel, `pc_wifi_scan_*` is the channel-hop
      dwell schedule (wrap-safe, sweep-counting), `pc_wifi_survey_*` keeps the per-channel RSSI/traffic
      survey and returns the strongest _other_ channel, and `pc_wifi_should_roam` is the RSSI-hysteresis
      roaming decision on that candidate. With `PC_ENABLE_PROMISC` the live binding
      (`pc_wifi_sniffer_begin` / `_tick` / `_end`) drives the promiscuous-capture owner
      (`services/promisc`) instead of installing a second radio callback; stats/survey are lock-free so
      capture is never stalled by a reader. Pure parts host-tested (`native_wifi_sniffer`, 15 cases) and
      **HW-verified on an ESP32-S3** against live 2.4 GHz traffic: 490 frames (369 mgmt / 121 data) over
      10 channel sweeps, nine channels surveyed with real BSSIDs, and the roam decision correctly choosing
      a -31 dBm channel over the -66 dBm one. Example WifiSniffer.
- [x] DNS resolver + answer verification _(shipped)_ - `PC_ENABLE_DNS_RESOLVER`: `services/dns_resolver` resolves a hostname to IPv4 (lwIP dns_gethostbyname marshalled to tcpip_thread, dotted-quad fast path) and verifies the answer - rejecting 0.0.0.0 / broadcast / loopback / multicast as spoof / DNS-rebinding indicators; classifier + verifier host-tested, HW-verified against live DNS (example DnsResolver). Remaining (M): captive-portal DNS-spoof mitigation, captive-portal auto-teardown timer.
- [x] mDNS TXT / `_https._tcp` / extra services _(shipped)_ - `pc_mdns_txt` / `pc_mdns_add_service`.
- [x] mDNS adaptive / auto-sleep beacons + a continuous refresher for crowded RF (M) _(shipped, HW-verified)_ -
      `PC_ENABLE_MDNS_ADAPTIVE` (`services/mdns_adaptive`): `pc_mdns_beacon_adapt` backs the announce
      interval off toward a ceiling under RF contention and recovers it toward the nominal cadence when the
      air is quiet, `pc_mdns_refresh_interval` gives the TTL/2 continuous-refresher cadence,
      `pc_mdns_beacon_due` says when an announce is due, and `pc_mdns_beacon_presleep_due` is the
      auto-sleep beacon (announce before a sleep window that would let the record lapse). Wrap-safe time math,
      pure, host-tested (`native_mdns_adaptive`). **Shipped: the device binding** (`pc_mdns_adaptive_begin/_tick/_end`, needs `PC_ENABLE_MDNS` + `PC_ENABLE_PROMISC`). The live
      contention counter is **promiscuous capture pinned to the station's own channel** - a radio-layer
      callback that never touches the responder's sockets, so unlike a second bind on UDP 5353 it does not
      turn the responder announce-only. (The 5353 approach was tried and reverted: bound after the ESP-IDF
      responder the join fails, bound before it the responder browses but every SRV/TXT/A query times out,
      because lwIP hands a datagram to the first matching PCB and `SO_REUSE_RXTOALL` does not rescue a raw
      PCB sharing a port with a socket-layer one. The `pc_udp_listen_multicast` primitive built for it was
      kept - it is correct for any group nothing else owns.) `pc_mdns_contention_sample` turns the capture's
      running frame total into a per-window value (host-tested incl. counter/clock wrap + saturation); the
      transmit is `mdns_service_txt_item_set()` re-applied at its current value (re-announces on every PCB
      with no goodbye, a refresh not an evict). The backoff ceiling is capped at ~7/8 of the TTL so the
      record can never lapse - a bug the HW run caught, since a ceiling past the TTL made the device
      undiscoverable. Host-tested (`native_mdns_adaptive`, 14 cases). HW-verified on an **S3** against
      avahi: A+SRV+TXT kept resolving while capture ran (the exact case the 5353 approach broke), real
      ambient frames drove the backoff, and it capped below the TTL. Example MdnsAdaptive.
- [x] Raw-UDP telemetry cast _(shipped)_ - `PC_ENABLE_UDP_TELEMETRY`: `services/udp_telemetry` builds InfluxDB line-protocol records (`measurement field=val,...`, host-tested) and casts them to a collector over UDP via `pc_udp_sendto`, zero-heap fire-and-forget (example UdpTelemetry).
- [x] **Static-IP fallback + TCP window auto-scaling by free RAM (M)** _(shipped)_ -
      `PC_ENABLE_NETADAPT`: `services/netadapt` `pc_netadapt_window()` sizes the TCP receive window
      from the free heap (a reserve left untouched, then a quarter of the spare, clamped to [min, max]) so
      a RAM-rich device gets throughput and a tight one stays alive; `pc_netadapt_dhcp_fallback()`
      decides when to give up on DHCP and use a static IP (elapsed timeout or retry budget). Pure cores,
      host-tested (`native_netadapt`); the app applies the results (lwIP window / netif config). Dynamic
      socket recycling (the transport-pool concern) shipped separately as `PC_ENABLE_SOCKPOOL`
      (`services/sockpool`): a fixed LRU connection-slot pool that, when saturated, recycles the
      least-recently-used slot for a new peer and reports the evicted id so the transport closes it (acquire /
      touch / release / find), pure + host-tested (`native_sockpool`). _Remaining:_ none (cores complete).

## Power & radio management

- [x] Radio power _(shipped)_ - `PC_ENABLE_RADIO_POWER`: `services/radio_power` applies a WiFi modem-sleep mode (`PC_RADIO_WIFI_PS` none/min/max) + an optional max-TX cap (`PC_RADIO_MAX_TX_DBM`) in one call (esp_wifi_set_ps / set_max_tx_power), trading throughput for lower average power; mode names host-tested, apply/readback HW-verified (example RadioPower). Remaining: BT-coexistence preference (only relevant on a BT-enabled build).
- [x] Dynamic network sleep modes / **sleep-cycle scheduler (M)** _(shipped, HW-verified)_ -
      `PC_ENABLE_SLEEP_SCHED`: `services/sleep_sched` `pc_sleep_next()` decides, from the idle time,
      how long a low-power device should sleep (0 = awake), ramping the window from a floor up to a
      ceiling (doubling every `ramp_ms`) the longer the idle streak runs. Pure wrap-safe decision core,
      fully host-tested (`native_sleep_sched`); the app applies the window via light / modem / deep sleep.
      Complements `services/radio_power`. **The SoC side** is `PC_ENABLE_POWER_MGMT`
      (`services/power_mgmt`): `pc_power_plan()` sets the CPU clock each tick with brownout beating
      thermal beating load - idle work drops to the floor, a hot die throttles (the caller feeds the
      previous throttle flag back, which supplies the hysteresis), a board that just browned out holds
      the floor for a settle window instead of re-collapsing its rail, and `pc_power_gate_bt()` hands
      back the Bluetooth power domain on a build with no BLE. Pure core, host-tested
      (`native_power_mgmt`, 19 cases). HW-verified on an **S3** with its real die sensor: BT domain
      released, an idle server clocked itself 240 -> 80 MHz, load took it back to 240 with the die
      rising 36 -> 38 C, and on a reachable threshold band the throttle engaged and released at exactly
      the configured points. The HW run produced a tuning rule now documented at the flag: the
      hysteresis band must exceed the temperature swing the clock change itself causes (~2 C for
      240 -> 80 MHz within one tick), or the throttle self-sustains however correct the comparison is.
      Example PowerGovernor. _Follow-up:_ forcing a real brownout needs a bench supply that can sag the
      rail on demand; the detection is live, the policy it feeds is host-tested.

## Security & auth

- [x] Source-IP allowlist / firewall in the accept callback _(shipped)_ - `listener_ip_allow_add_cidr("192.168.1.0/24")` / `listener_ip_allowed` (IPv4 + IPv6 CIDR rules matched on the full address, `PC_ENABLE_IP_ALLOWLIST`; example IpAllowlist).
- [x] Brute-force per-IP exponential lockout _(shipped)_ - `PC_ENABLE_AUTH_LOCKOUT`; `auth_lockout_*` table (keyed on the full IPv4/IPv6 address) issues 429 + Retry-After on the HTTP auth gate (example AuthLockout).
- [x] CSRF token verification _(shipped)_ - `PC_ENABLE_CSRF`; global enforcement on POST/PUT/PATCH/DELETE via a stateless HMAC-signed `X-CSRF-Token` (built-in `GET /csrf` issues it; example Csrf).
- [x] Granular API-token scoping _(shipped)_ - `pc_jwt_claim_str()` reads string claims (sub / role / scope) and `pc_jwt_scope_allows()` matches a space-separated OAuth2 scope claim, so a handler can authorize per role/scope on the verified JWT (example JWTAuth).
- [x] MFA - TOTP two-factor _(shipped)_ - `PC_ENABLE_TOTP`: `services/totp` computes / verifies RFC 6238 time-based one-time passwords (HMAC-SHA1 on the software SHA-1, Authenticator-compatible) and decodes base32 secrets, for a second factor on top of password / JWT; host-tested against the RFC vectors, HW-verified (example Totp). An external-API verifier can also be called from a handler via the http_client.
- [x] OIDC ID-token verification + OAuth2 token exchange _(shipped)_ - RS256 relying-party verifier (`services/oidc`: JWKS key selection by kid, real RSA-2048 signature check, iss/aud/exp/nbf, claim extraction) plus the OAuth2 token-endpoint client (`services/oauth2`: authorization_code + refresh_token grants, confidential-client or PKCE, JSON token parsing; example OAuth2). SAML remains open (heavy XML/canonicalization - poor fit) (L).
- [x] Secure boot + flash encryption _(shipped, docs)_ - [docs/SECURE_BOOT.md](SECURE_BOOT.md): a hardening guide for ESP32 Secure Boot v2 + Flash Encryption (and NVS encryption) mapped to the secrets this library holds (SSH host key, TLS key, SNMPv3/JWT secrets, config blobs, audit-log sink). Encrypted config handshake during onboarding remains open (M).
- [x] MAC-derived UUID _(shipped)_ - `pc_uuid_from_mac` / `pc_device_uuid` (RFC 4122 v5; example DeviceUuid).
- [x] **Modern SSH ECC: Curve25519 key exchange + Ed25519 keys** (L) _(shipped v4.94.0)_ - the SSH server now negotiates a crypto-agnostic KEX: `curve25519-sha256` + `ssh-ed25519` host key + ed25519 client auth, alongside the `diffie-hellman-group14-sha256` + `rsa-sha2-256` set, with the preference runtime-selectable (`ssh_kex_set_prefer_rsa`, default RSA). The curve backend is a bespoke constant-time X25519 / Ed25519 that fits the zero-heap / no-stdlib idiom (no micro-ecc / donna / wolfSSL dependency), with the ESP32 MPI accelerator driving field inversion on-device and a software fallback retained. RSA stays as a fallback; ed25519 host-key provisioning sits alongside the RSA DER path; crypto KATs cover both suites and the OpenSSH interop test passes with zero forced algorithms. HW-verified on an ESP32-S3 (both suites; worker stack raised to 12288 when SSH is enabled, curve peaks ~10.5 KB).

### Crypto parity vs CycloneSSL / CycloneSSH (ESP32-relevant gaps)

Comparison against Oryx Embedded's **CycloneSSL** (TLS/DTLS) and **CycloneSSH** - the closest portable,
zero-external-dependency C stacks to ours. Only gaps that matter on an ESP32 are tracked (their
Camellia / SEED / ARIA / Serpent / Twofish / RC4 / Blowfish / DSS ciphers are deliberately skipped).
**Benchmark caveat:** Oryx publishes their numbers at **-O3**, so any apples-to-apples throughput /
handshake comparison must build our crypto at `-O3` too (our default examples build `-Os`); measure the
same core on the same S3 at `-O3` before claiming a delta. Our current surface: **TLS** = mbedTLS 1.2/1.3
(ECDHE curve-pref, AES-GCM, ChaCha20-Poly1305, tickets/resumption); **SSH** = curve25519-sha256 +
dh-group14, ssh-ed25519 + ecdsa-sha2-nistp256 + rsa-sha2-512/256 host keys, chacha20-poly1305@openssh.com +
aes256-gcm + aes256-ctr, hmac-sha2-256/512 (+ETM), zlib@openssh.com s2c, password + publickey auth.

- [x] **Post-quantum hybrid key exchange - SSH** (SHIPPED v6.6.0, `PC_ENABLE_PQC_KEX`) - the **headline
      gap is closed for SSH**. Shipped a constant-time, zero-heap **ML-KEM-768** core (FIPS 203, the novel
      primitive: software NTT over q=3329 + a Keccak/SHA-3/SHAKE sponge) in `network_drivers/presentation/pqc`,
      Encaps-only (the device is always the responder). The SSH transport advertises **mlkem768x25519-sha256**
      (draft-ietf-sshm-mlkem-hybrid-kex) first, runs the existing X25519 alongside ML-KEM-Encaps, and combines
      `K = SHA256(K_PQ || K_CL)` (RFC 9370). Byte-exact vs the FIPS 203 reference (kyber-py) + a decisive
      end-to-end host test (independent ML-KEM Decaps client, string-K exchange hash / KDF, ed25519 signature).
      Peak ~7 KB worker stack (`PC_WORKER_STACK_PQC_MIN`). _Pending:_ interop vs real OpenSSH 9.9+ on the S3 rig.
- [x] **Post-quantum hybrid key exchange - HTTP/3 / TLS 1.3** (SHIPPED v6.7.0) - wired the same ML-KEM-768
      core into the QUIC hand-rolled TLS 1.3 as the **X25519MLKEM768** group (IANA 0x11ec): added the group to
      `pc_tls13_msg` supported_groups + key_share (client share = ek(1184) || X25519(32); server share =
      ciphertext(1088) || X25519(32) - ML-KEM first per draft-ietf-tls-ecdhe-mlkem), concatenated the two secrets
      (ML-KEM || X25519, 64 B) into the `pc_tls13_kdf` handshake secret, and enlarged the QUIC ServerHello flight
      buffer for the ~1.1 KB key_share. A PQC-capable browser negotiates it; others fall back to X25519. Verified
      by a full hybrid handshake host test (independent ML-KEM Decaps client + both Finished MACs). The classical
      HTTPS path stays on mbedTLS (whose hybrid support tracks its version). _Pending:_ curl/browser HW interop.
- [x] **PQC hybrid follow-ons** (S) _(shipped)_ - **sntrup761x25519-sha512@openssh.com** for SSH parity with
      OpenSSH's other default (server + reverse-SSH client, HW-verified on the P4 vs OpenSSH 10), and a QUIC
      **HelloRetryRequest** path (`pc_quic_tls`): a client that lists X25519MLKEM768 but sends only a classical
      key_share is answered with an HRR selecting the hybrid group (RFC 8446 §4.1.4, message_hash transcript
      restart §4.4.1) instead of being downgraded to X25519. Host-tested (`native_quic_tls_pqc`
      test_hybrid_hrr_roundtrip: the retry completes and the server Finished verifies over the HRR transcript).
- [x] **SSH cipher / host-key breadth** _(shipped)_ - closed the easy deltas vs CycloneSSH:
    - [x] **`aes256-gcm@openssh.com`** _(shipped)_ - AES-256-GCM AEAD (RFC 5647): length-in-clear as AAD,
          per-packet invocation counter, no separate MAC. Self-contained `ssh/crypto/ssh_aesgcm` (AES block HW
          via mbedTLS on ESP32, GHASH in software; AES-256, so not the AES-128 `http3/pc_quic_aead` core). Seal/open
          KAT vs the NIST/McGrew Test Case 16 vector + a packet-layer round-trip; negotiated second after chacha.
    - [x] **`rsa-sha2-512`** host-key signatures _(shipped)_ - RFC 8332 SHA-512 PKCS#1 v1.5 over the exchange
          hash, hash-parameterized `ssh_rsa_sign`/`_verify` (SHA-512 DigestInfo). The one `ssh-rsa` key now
          advertises both rsa-sha2-512 (preferred) and rsa-sha2-256 in `server_host_key_algorithms` and
          `server-sig-algs`; client-auth picks the verify hash from the signature name. SHA-512 KAT byte-exact
          vs openssl + hash-binding, e2e KEXDH reply, and a genuine client-auth signature all tested.
    - [x] **`ecdsa-sha2-nistp256`** host key + client auth _(shipped)_ - ECDSA over NIST P-256 (RFC 5656,
          SHA-256). New `ssh/crypto/ssh_ecdsa`: ESP32 mbedTLS (HW, side-channel-hardened); native is a
          self-contained software P-256 (field/Jacobian point math) with RFC 6979 deterministic signing, so
          the sign path is byte-exact against the RFC 6979 A.2.5 vectors. Installed via `pc_ssh_hostkey_ecdsa_set()`;
          negotiated in `server_host_key_algorithms` + `server-sig-algs`; the KEXDH reply carries the P-256 blob
          and an `mpint(r)||mpint(s)` signature; client-auth verifies. KAT + e2e KEXDH + genuine client-auth
          all tested. _(aes128-gcm skipped: 256 is the ESP32-relevant one.)_
    - [x] **`ecdh-sha2-nistp256`** key exchange _(shipped)_ - completes RFC 5656 (its §4 ECDH alongside the §3
          ECDSA host key). The shared secret is the X coordinate of `d_S * Q_C`, hashed as an mpint with
          `Q_C`/`Q_S` as 65-byte point strings; the peer point is on-curve checked. Reuses the one P-256 curve
          in `ssh/crypto/ssh_ecdsa` (ESP32 `mbedtls_ecdh_compute_shared`, HW; native software P-256), added
          `ssh_ecdsa_p256_ecdh()`. Negotiated in `kex_algorithms`. Byte-exact vs the RFC 5903 §8.1 shared-secret
          KAT + a negotiation test + an e2e KEXDH that verifies the host signature over the reconstructed hash.
- [x] **SFTP + SCP subsystem over SSH** (XL, high value - ties to G-code deployment) _(done, HW-verified)_ -
      Cyclone has both SFTP and SCP client+server; we now have the **server** side. `PC_ENABLE_SSH_SFTP`
      (services/file_transfer/sftp + server/ssh_sftp) - an SFTP v3 server subsystem (`SSH_FXP_*` over a session channel) with
      a fixed-BSS handle table, streamed reads/writes, no heap, serving an `fs::FS` mount via
      `pc_ssh_sftp_begin(fs, root)`: OPEN/READ/WRITE/OPENDIR/READDIR/STAT/MKDIR/RMDIR/REMOVE/RENAME/REALPATH,
      the `..` traversal guard (server/fs_path). `PC_ENABLE_SSH_SCP` (services/file_transfer/scp + server/ssh_scp) - the
      rcp SINK direction (`scp file device:/path`) via `exec "scp -t"`. The standards-track southbound path for
      **secure machine-agent G-code push** (drop NC programs on the device, or drip them to a controller via
      `services/dnc`) over the one authenticated SSH port. Pure codecs host-tested (native_ssh_sftp, native_scp);
      HW-verified vs the OpenSSH `sftp`/`scp` clients on an ESP32-S3 + SD card (60 KB byte-exact). The HW run
      caught + fixed a latent SSH transport bug (a TCP read carrying several pipelined packets - a large SFTP
      write - overflowed the single-packet receive buffer; now drained incrementally). _Follow-ups:_ the SCP
      SOURCE (download) direction, recursive SCP, and a graceful `SSH_MSG_DISCONNECT` on teardown.
- [x] **TLS Raw Public Keys (RFC 7250)** (M) _(server-side, DTLS 1.3; wolfSSL-verified)_ - Cyclone supports
      RPK; a cert-less TLS credential (bare SubjectPublicKeyInfo, no X.509 chain) is a natural fit for
      constrained, provisioned ESP32 fleets where a pinned key beats a full PKI - smaller handshakes, no cert
      parsing. **Shipped (`PC_ENABLE_TLS_RPK`):** the hand-rolled TLS 1.3 codec (`pc_tls13_msg`) parses the
      `server_certificate_type` extension (RFC 7250, IANA 20), the DTLS 1.3 server negotiates RawPublicKey and
      presents its Ed25519 `SubjectPublicKeyInfo` (44-byte DER, RFC 8410) in place of the X.509 chain - the same
      key still signs CertificateVerify, so no downgrade. Additive + server-side (no client-cert request). Pure
      codec + end-to-end host tests (`native_dtls_tls13`, `native_dtls_conn`), and interop-verified against the
      wolfSSL DTLS 1.3 client in RawPublicKey-only mode (`--rpk`). _Follow-up:_ wire it into the HTTP/3 path (the
      shared codec already carries it) once an H3 RPK peer is available.
- [x] **DTLS 1.3** (L) _(full server stack + CoAP-over-DTLS front-end shipped)_ - Cyclone does DTLS 1.0/1.2/1.3;
      we have QUIC (HTTP/3) but no DTLS service. DTLS 1.3 secures CoAP-over-UDP and other datagram telemetry
      without a TCP+TLS session - the missing secure UDP transport for the IoT-device families. Layers on the
      existing TLS 1.3 record/handshake crypto. **Shipped (`PC_ENABLE_DTLS`, network_drivers/presentation/security/dtls):**
      the RFC 9147 §4 record layer (pc_dtls_record) - DTLSPlaintext + DTLSCiphertext (unified header,
      AEAD_AES_128_GCM, §4.2.3 sequence-number encryption via an AES-ECB mask + §4.2.2 reconstruction, the
      TLS 1.3 nonce, §4.5.1 anti-replay window); the §5 + §7 handshake framing / reliability (pc_dtls_handshake) -
      the 12-byte handshake header, overlap-tolerant reassembly, the ACK message, and the stateless
      HelloRetryRequest cookie; and the **§5-6 server handshake state machine** (pc_dtls_conn) - the one-round-trip
      full handshake (TLS_AES_128_GCM_SHA256 / X25519 / Ed25519), epoch 0→2→3 transitions, reusing the TLS 1.3
      messages + key schedule. Byte-exact host KATs (`native_dtls`, `native_dtls_hs`, `native_dtls_tls13`) plus
      an end-to-end handshake proof (`native_dtls_conn`: a from-scratch peer completes it, both sides install
      matching app keys) and **wolfSSL DTLS 1.3 interop** (direct + HelloRetryRequest group renegotiation).
      **HelloRetryRequest** group renegotiation (§5.1, address-bound cookie) and **ACK/timeout retransmission**
      (§5.8, fresh-seq flight re-send on exponential backoff) are shipped, and the **CoAP-over-DTLS front-end**
      is complete - the `pc_coaps_process` bridge plus `coaps_server`, a per-peer DtlsConn pool binding UDP 5684
      (`coaps://`), driven by one `pc_coaps_server_poll()` (`native_coaps` / `native_coaps_server`; example
      CoapSecure). **Connection IDs** (§9 / RFC 9146) are shipped too - the connection_id extension is
      negotiated in the handshake, every DTLSCiphertext carries the peer's CID, and `coaps_server` routes by
      CID so a session survives a client address change / NAT rebinding (`native_dtls*` + wolfSSL `--cid`
      interop). _Remaining:_ full HW-rig interop.
- [x] **SSH keyboard-interactive auth (RFC 4256)** (S) _(shipped, HW-verified on the P4)_ - the
      challenge-response face of password auth (SSH_MSG_USERAUTH_INFO_REQUEST/RESPONSE), enabling
      server-driven MFA prompts / password-change flows over SSH (`PC_ENABLE_SSH_KEYBOARD_INTERACTIVE`).

## Storage & config

- [x] Typed NVS config store _(shipped)_.
- [x] Partition-map status monitor endpoint _(shipped)_ - `PC_ENABLE_PARTITION_MONITOR`: `pc_partition_monitor_begin()` serves the flash partition table (label, kind, type/subtype, offset, size, running app slot) as JSON via `esp_partition` / `esp_ota_ops`; kind classifier + serializer host-tested (example PartitionMonitor).
- [x] Config export / restore _(shipped, schema-driven)_ - `PC_ENABLE_CONFIG_IO`: `services/config_io` serializes a declared schema of fields to a portable `key=value` text blob and parses one back into the NVS config store - backup / migrate / bulk-provision, deterministic and zero-heap (host-tested round-trip; example ConfigExport). Remaining (M): full enumeration-based export (needs NVS key iteration), ZTP multi-stage provisioning.
- [x] Unified VFS wrapper _(shipped)_ - `PC_ENABLE_VFS`: `services/vfs` gives one file API (open/read/write/close, exists/size/remove/rename, whole-file helpers) over a pluggable backend - a zero-heap RAM pool (deterministic, host-tested) or a real `fs::FS` (LittleFS / SD / SPIFFS) on ESP32 - so features target storage without knowing the medium (example Vfs).
- [x] **Wear leveling** + log offload (server/SD) (M) _(shipped, HW-verified)_ - `PC_ENABLE_WEARLEVEL`:
      `services/wearlevel` `pc_wearlevel_pick()` returns the least-worn slot (from per-slot write
      counts the app persists) so repeated flash/NVS writes spread evenly and the region ages together
      instead of burning out one block, plus a `pc_wearlevel_spread()` imbalance metric. Pure core,
      host-tested (`native_wearlevel`; a 4000-write pick+mark loop levels every slot exactly). Log
      offload rides the existing `services/logbuf` pluggable sink (SD / syslog / HTTP).
      **Hot-swap storage safeties** are `PC_ENABLE_HOTSWAP` (`services/hotswap`): a card is a connector,
      and when it leaves the failure is quiet - the driver still reports a mounted volume and every write
      fails into nothing. One state machine per volume (ABSENT / READY / FAULTED) makes that loud and
      recoverable: `pc_hotswap_ready()` is the fail-closed gate before any filesystem call,
      `PC_HOTSWAP_FAIL_THRESHOLD` consecutive I/O errors declare the medium gone and unmount it at once
      (one error does not, and any success resets the run), and a probe every `PC_HOTSWAP_PROBE_MS`
      remounts a card that returns. Pure core with an explicit `now`, host-tested (`native_hotswap`, 20
      cases incl. counter saturation and rollover-safe probe pacing). HW-verified on a **P4** with a real
      SD card: it faulted on the third consecutive failure (not the first), refused the next write with
      `storage not ready` rather than attempting it, and remounted itself (`mounts` 1 -> 2). Example
      HotSwapStorage. _Follow-up (opt-in, deliberately not folded in):_ the gate is currently the
      application's to call. The library's own SD writers - the edge-cache L2 tier, upload_service,
      file_serving, ssh_sftp/scp - could consume it directly so a card pull is survived without any
      app code, but that changes the behavior of shipped, HW-verified features, so it belongs to a
      separate decision rather than riding along here.
- [x] OTA rollback protection + soft-brick safeguard _(shipped)_ - `PC_ENABLE_OTA_ROLLBACK`: `services/ota_rollback` commits a freshly-updated image once a self-test passes, or rolls back to the previous image if the self-test fails or the confirm window elapses, so a bad update self-heals; decision logic pure + host-tested, commit/rollback via esp_ota_ops, HW-verified (example OtaRollback). Remaining: modular partition swapping (M).

## Observability, diagnostics & reliability

- [x] Immutable audit logs _(shipped)_ - tamper-evident SHA-256 hash chain in a fixed RAM ring (moving anchor keeps the retained window verifiable), with a pluggable sink for store-and-forward to SD / syslog / an HTTP log service. (FDA 21 CFR Part 11 attestation/e-sign workflow still open.)
- [x] Rotating log buffer + severity traps _(shipped)_ - `PC_ENABLE_LOGBUF`: `services/logbuf` keeps the last `PC_LOG_LINES` lines in a fixed RAM ring (oldest pruned, no heap), dumps them oldest-first for a `/logs` endpoint, and fires a trap callback on lines at/above a severity threshold (forward critical lines as an SNMP trap / webhook); pure + host-tested (example LogBuffer).
- [x] Core dump to SD/FTP + live exception-decoder panel (M) _(shipped, HW-verified)_ -
      `PC_ENABLE_EXC_DECODER` (`services/exc_decoder`): `pc_exc_parse` extracts a captured Guru
      Meditation panic dump (cause, register PC + EXCVADDR, and the `Backtrace: pc:sp ...` frames, tolerating
      missing fields + a trailing `|<-CORRUPTED`) into a structured ExcInfo, and `pc_exc_json` serializes
      it for a live `/exception` panel; the browser / build server resolves the PCs to file:line against the
      firmware ELF (addr2line lives off-device). **Core-dump-partition recovery** closes the reboot gap:
      `pc_exc_coredump_present` / `_summary` (architecture-honest - a real backtrace on Xtensa, trap
      cause/value and no invented frames on RISC-V) / `_save(fs, path)` / `_erase`, with
      `pc_exc_coredump_read(offset, buf, len)` as the transport-agnostic seam. **SD/FTP offload** rides that
      seam: `PC_ENABLE_FTP_SESSION` (`services/ftp_session`) drives a real control + data connection pair
      through the RFC 959 STOR sequence and pulls the payload through a caller-supplied source, so the dump
      streams straight out of flash. **Zero-overhead abstract logging** is `PC_LOG_LEVEL` +
      `PC_LOGD/I/W/E` (`shared_primitives/log.h`): levels below the floor are discarded by the preprocessor
      into an unevaluated `sizeof`, so they emit no code and no flash string while still being format-checked
    - measured byte-identical to a function with no logging at all. Host-tested (`native_exc_decoder`,
      `native_log`, `native_ftp`). HW-verified on a **P4** (RISC-V, SD offload, erase proven by the `{}`
      between crashes) and an **S3** (Xtensa 10-frame backtrace symbolizing to the true crash path; 21540-byte
      dump uploaded to a live FTP server and then parsed by Espressif's own `esp-coredump -t raw`, which
      reported `StoreProhibitedCause` / `excvaddr 0x0` / `pc <crash_handler+33>` / task `pc_worker`).
      Example CoreDump. The HW run caught a real bug: the new session driver was missing from
      `PC_NEED_DET_CLIENT`, so `pc_client_open` resolved to the stub that returns -1 and the offload
      silently never connected.
- [x] Runtime heap/stack guardrails _(shipped)_ - `PC_ENABLE_GUARDRAILS`: `services/guardrails` samples free heap, the heap low-water mark, the largest free block (fragmentation), and a task's remaining stack, and fires a breach callback when any crosses its `PC_GUARDRAIL_*` floor - a proactive fail-safe hook on top of the passive /metrics numbers; evaluator + JSON host-tested, served at `/health` (example Guardrails).
- [x] Fail-safe safe-state + deadlock-detection WDT + watchdog-protected coroutine lifelines (M)
      _(shipped)_ - `PC_ENABLE_FAILSAFE`: `services/failsafe`, a software watchdog. Register a
      "lifeline" (a task / worker / control loop) that must check in (`pc_failsafe_feed`) within its
      deadline; `pc_failsafe_check()` detects one that wedged (hang / deadlock) via a wrap-safe time
      delta and fires a breach callback **once per stuck episode** so the app drives a known-safe state.
      App-defined and per-lifeline, on top of the hardware task WDT; the pure core takes an explicit
      `now` so it is fully host-tested (`native_failsafe`), plus a `/health`-style JSON serializer.
      Zero heap, no stdlib.

## Build / tooling

- [x] Runtime build-flag reporter _(shipped)_ - `server.diag()` / `PC_ENABLE_DIAG` serves a build-info JSON (example Diagnostics); the feature enumeration could be extended.
- [x] **Pentesting / adversarial suite** _(shipped)_ - a separately-runnable harness (env `native_pentest` + a nightly `Pentest` CI job, _not_ part of the per-commit unit-test run) that fuzzes the untrusted-input parsers (HTTP request line/headers/body, Modbus ADU, base32) with malformed, oversized, partial slowloris-style, binary/protocol-confusion, and deterministically-random input, asserting the device's safety invariants: fixed footprint (no buffer index past its bound), fail-closed (defined error states only), and liveness (no hang/over-read). Plus a documented on-device stress playbook (slowloris / floods / brute-force vs the throttle / lockout / allowlist defenses). Full guide: [PENTEST.md](PENTEST.md). Extend it to the remaining codecs (CBOR / SNMP / CoAP / WS / multipart) as you go.

## Protocol & transport versions

The big-ticket transport/protocol upgrades, sequenced last. Each is large (L) and
gated on the internal-piping straightening (a clean transport read/write API) so a
new framing/record layer slots in behind one owner instead of threading through
every layer. The current HTTP/1.1 core already tracks the modern HTTP specs
(RFC 9110 semantics, RFC 9112 messaging - which obsolete RFC 7230/7231).

- [x] **HTTP/2** (L, RFC 9113) _(shipped, PSRAM-gated)_ - HPACK header compression
      (RFC 7541), stream multiplexing + flow control, and `h2` ALPN over the TLS
      layer above; streams mapped onto the deterministic per-connection model
      without per-stream heap.
- [x] **HTTP/3** (L, RFC 9114) - QUIC transport (UDP) + HTTP/3 with QPACK
      (RFC 9204); the largest item, **DONE and the trigger for v5.0.0**. The whole stack
      is built, host-tested end-to-end, HW-validated, and **proven against a real
      third-party client**: the codecs (QUIC varint RFC 9000 sec 16, packet /
      packet-number coding sec 17, frame codec sec 19, HTTP/3 framing RFC 9114 sec 7,
      QPACK RFC 9204); the QUIC packet crypto (RFC 9001 - HKDF-Expand-Label key
      schedule, AEAD_AES_128_GCM, header protection, Retry tag); a hand-rolled TLS 1.3
      handshake (RFC 8446, TLS_AES_128_GCM_SHA256 + X25519 + Ed25519, pinned to the
      RFC 8448 traces - mbedTLS has no QUIC-TLS API); the transport-parameters codec
      (sec 18); the stateful QUIC v1 connection engine (per-level AEAD, CRYPTO / STREAM
      reassembly, ACKs, coalescing, HANDSHAKE_DONE); the HTTP/3 application engine
      (control + QPACK streams, SETTINGS, request -> response); the UDP-facing
      `pc_quic_server` pool (DCID routing, ingest ring); and the PC bridge that
      serves HTTP/3 through the same routes as HTTP/1.1 and HTTP/2 (`server.pc_h3_cert(...)` + `begin()`). **curl 8.14.1 (OpenSSL 3.5.6 QUIC + nghttp3) completes the handshake
      and serves GET / POST at ~14 ms/request** (see `tools/interop/`); the ESP32-S3
      mbedTLS hardware-crypto path is byte-identical to the software path. Remaining as
      v5.x hardening (not correctness blockers): PTO loss recovery, CONNECTION_CLOSE on
      idle / error, and the PSRAM build guard for a device deployment.

### Supporting HTTP specs (smaller, fold in alongside the above)

- [x] **Cookies** (M, RFC 6265) _(shipped)_ - emission via `set_cookie()` (name/value
      plus a freeform attributes string for `Secure` / `HttpOnly` / `SameSite` /
      `Max-Age` / `Expires` / `Path` / `Domain`), and inbound reading via
      `http_get_cookie(req, name, out, cap)` - parses the request `Cookie` header
      (case-sensitive names, DQUOTE-stripped values, `=` preserved in values),
      mirroring `http_get_header()`. Host-tested (`test_http_parser`, 4 cookie cases);
      pairs with the session / CSRF / auth features.
- [x] **HTTP caching** (RFC 9111) _(shipped)_ - conditional GET on served files via
      `PC_ENABLE_ETAG`: a strong `ETag` + `Last-Modified`, and `304 Not Modified`
      for a matching `If-None-Match` or (when no `If-None-Match`) an `If-Modified-Since`
      not older than the file; plus `Cache-Control` via `set_cache_control()`. Remaining
      (optional): response-side freshness heuristics / `Age`, request `Cache-Control`.
- [x] **Forwarded header** (S, RFC 7239) - _parser + trusted-upstream lockout wiring shipped._
      `http_forwarded_client()` recovers the original client address + scheme from `Forwarded`
      (RFC 7239) or the de-facto `X-Forwarded-For` / `X-Forwarded-Proto` (leftmost = the
      client). Both IPv4 (optional `:port`) and IPv6 (bracketed `for="[2001:db8::1]:port"` or a
      bare `X-Forwarded-For` literal) are recovered and canonicalized (RFC 5952); the candidate
      is validated with `pc_ip_parse`, so `unknown` / obfuscated `_id` tokens are rejected.
      Host-tested (`test_http_parser`, 5 cases). `PC_ENABLE_FORWARDED_TRUST`
      (`services/forwarded_trust`) auto-wires the recovered IP into the per-IP auth lockout, but
      only when the real TCP peer matches a configured trusted-upstream CIDR
      (`pc_forwarded_trust_add_cidr`) - the header is client-spoofable, so a direct/untrusted
      peer's header is ignored and a malformed / obfuscated / unspecified token falls back to the
      TCP peer. The accept-time throttle and IP allowlist deliberately stay on the proxy's real TCP
      source. Pure + host-tested (`native_forwarded_trust`, 10 cases) and HW-verified on an
      ESP32-S3 (a forwarded client is locked out independently of the TCP source and of other
      forwarded clients); example ForwardedTrust.

## Maintenance

- [x] **API directional symmetry pass** _(audited)_ - swept the paired ingress/egress
      surfaces (codec encode/decode, transport read/send, server/client, HTTP
      request/response, the protocol-service parse/build pairs) against the rule that a
      value at a position on ingress sits at the same position on egress. Result: the
      surfaces are symmetric, with one real fix applied - the egress transport API
      (`pc_conn_send` / `pc_conn_sndbuf` / `pc_conn_flush`, plus the private
      `pc_resp_end`) no longer threads a `tcp_pcb *`; it resolves the slot's pcb
      internally, exactly as the RX read path and the client surface already do, so a
      caller can no longer pass a pcb that disagrees with the slot. The remaining LOW
      findings (CBOR `text` vs MessagePack `str`; builder-returns-length vs
      parser-returns-bool) are spec-faithful, consistent house style - left as is.
- [x] **Refresh build footprints** (S) _(automated)_ - the per-feature / per-example flash + RAM
      footprint tables now track the current build automatically: the **ESP32 Build** workflow
      (`.github/workflows/esp32-build.yml`) rebuilds every example on each push to main, merges the
      totals into `docs/footprints.json`, regenerates `docs/FOOTPRINTS.md` and the per-feature ranges
      (`ci_tooling/generate/example_footprints.py` + `feature_budget.py`), and commits them
      (`docs: update ESP32 build footprints [skip ci]`). So the documented numbers stay current after a
      batch of changes with no manual step - the opt-in codec services added since track through their
      flags, and the auto-commit lands right after each release bump.
- [x] **Audit the library against its standards** (L) _(audit performed + tracked)_ - every standard in
      [STANDARDS.md](STANDARDS.md) was reviewed against its key MUST / SHOULD requirements, with the
      verdict and the backing evidence recorded per standard in **[AUDIT.md](AUDIT.md)**. The audit
      leverages the strongest available evidence: the major protocols (HTTP, WebSocket, CoAP, MQTT,
      Modbus, SNMP, OPC UA, SSH) are each verified against a **real reference implementation** in the
      interop harness, and the crypto primitives are pinned to FIPS/RFC test vectors. No open conformance
      defects were found in this pass (the HTTP request-smuggling and CoAP-Observe-clock fixes from earlier
      reviews are regression-tested); the remaining `🔷` entries are documented scope boundaries tracked
      here, not violations. The audit is re-run per subsystem change - the verdict + evidence columns are
      kept current.
- [x] **Shared CRC engine** (S) - SHIPPED. `shared_primitives/crc.h`: the standard Rocksoft / Williams
      model (width / poly / init / refin / refout / xorout), so any published CRC is six numbers rather
      than a new loop. Header-only and pure like `endian.h` - no feature flag, nothing emitted when
      unused. `begin` / `update` / `final` are split so a frame that is not contiguous in memory (a
      header struct then a payload buffer) can be checksummed without copying it together first.
      Bitwise rather than table-driven on purpose: a 256-entry table _per polynomial_ costs more flash
      than these frames justify, and every caller here checksums tens to hundreds of octets, not
      megabytes (`services/wal` is the one exception - see the migration item below). Presets ship for
      CRC-8/SMBUS, CRC-8/MAXIM-DOW, CRC-8/NRSC-5, CRC-16/ARC, CRC-16/MODBUS, CRC-16/IBM-3740,
      CRC-16/XMODEM, CRC-16/KERMIT, CRC-16/X-25, CRC-16/DNP, CRC-24/OPENPGP, CRC-32/ISO-HDLC and
      CRC-32/BZIP2. Two independent lines of evidence, both asserted in `test_crc` (10 cases, in
      `native_primitives`): every preset reproduces its **published cataloge check value** (the
      CRC of `"123456789"`), and the engine is **diffed against the in-tree hand-rolled loops** across
      every length 0..64, so a preset meant to retire one of them is proven byte-identical to the
      interop-tested code it replaces rather than merely plausible.
- [x] **Migrate the hand-rolled CRCs onto the shared engine** (M) - _13 of 16 done; the remaining
      three are deliberate exclusions, see below._ The consolidation the CRC engine was written to
      enable. Sixteen services each carried their own CRC loop - the same duplication `endian.h` was
      created to end. Thirteen mapped onto a shipped preset and are now migrated, one commit each so
      a regression bisects to a single codec: `c37118`, `df1`, `dnp3`, `enocean`, `interbus`,
      `mbplus`, `modbus`, `nema_ts2`, `rawl2`, `sdi12`, `sht3x`, `thread`, `zigbee`. Net -42 lines,
      and every service's own suite stayed green (150 cases). The substitutions are proven rather
      than assumed: `test_crc` diffs the shared engine against each of those exact loops over every
      length 0..64, so "byte-identical" is asserted in CI, not argued in a commit message.
      Two of them also stopped needing a scratch buffer - `zigbee` (control byte + payload) and `df1`
      (data + a trailing ETX) CRC two non-adjacent regions, which is what the engine's
      begin/update/final split exists for. Note their running value is the engine's internal
      register, held unreflected until `final()` for a reflected CRC, so it is not interchangeable
      with the old right-shift intermediate - those call sites were converted whole rather than one
      call at a time.
      The remaining three are **not** pending work; each is excluded for a reason:
    - `wal` is **table-driven** (256-entry CRC-32 lookup) because it checksums bulk log records, not
      frames - the one place the flash-vs-throughput trade goes the other way. Migrate only if
      measurement says the bitwise cost is acceptable; otherwise it stays as it is.
    - `gnss/rtcm3` uses **CRC-24Q** (poly `0x864CFB`, init `0`) - same polynomial as CRC-24/OPENPGP but
      a different seed, so no shipped preset covers it. Adding one needs its check value confirmed
      against a published source first rather than computed with this engine and asserted against
      itself, which would prove nothing.
    - `dshot` is **not a CRC at all** - a 4-bit XOR fold of the three nibbles - so it has nothing to
      migrate onto. (An earlier revision of this roadmap also listed `hw_health` as hand-rolling a CRC;
      it does not - it only consumes a `crc_ok` verdict.)

## Low-level networking (raw Layer 2)

The ESP32 _does_ expose raw Layer-2 access on both interfaces, and the hard-real-time
cyclic timing is reachable too - so the earlier "PHY can't do raw L2 / can't hit the
timing" caveats are corrected here. **Timing model:** build the device as a
_single feature + the base web server_, and run the protocol's cyclic loop as a
hardware-timer / RMT ISR feeding a **high-priority preempting task** (a top-priority,
core-pinned FreeRTOS task that preempts the web server) off a small interrupt queue.
The web server runs underneath at lower priority; the cyclic deadline owns the CPU
when it fires. With the whole device budgeted to one cyclic loop, the sub-millisecond
isochronous deadlines are achievable - the determinism guarantees (fixed buffers, no
heap, bounded work) are exactly what makes a hard real-time loop tractable here.

**Front-end assumption (applies to every "hardware-gated" item below):** the user can
attach an external adapter on **SPI / UART / I2C** to supply any missing PHY, radio,
or line transceiver - so the deliverable is always the protocol + a small adapter
driver, never "impossible." Canonical examples: an **EtherCAT Slave Controller**
(e.g. LAN9252) over SPI, a **HART FSK modem** over UART, a **DSRC / C-V2X radio**
module over SPI/UART, an **IO-Link master IC** over SPI, **RS-485 / CAN / SDLC /
MBP / LON** transceivers, etc. **Analog I/O** is native: a **4-20 mA** current loop
(or 0-10 V) is read straight off the ESP32 **ADC** over a known-range sense resistor
(scale = known full-scale range), and driven out via the DAC / PWM - so analog
instrument variables (incl. HART's 4-20 mA primary value) need no special front end.

    - **Wi-Fi (802.11):** `esp_wifi_80211_tx()` injects arbitrary management / data
      frames (custom beacons, proprietary MAC headers) - bypasses the Wi-Fi state
      machine. Also the basis for the pentest/observability angle (beacon/mgmt crafting).
    - **Wired (802.3):** `esp_eth_transmit()` + `esp_eth_update_input_path()` detach the
      Ethernet MAC from lwIP so the app reads/writes raw Ethernet frames at L2 directly -
      this is what makes PROFINET-RT / EtherCAT / GOOSE / POWERLINK / SERCOS framing
      reachable - the cyclic deadline is met by the high-priority preempting-ISR/task
      model above (single-feature build), not blocked by the platform.
    - **L2 transparent bridge:** capture raw L2 on Wi-Fi and map onto the wire via the
      Ethernet MAC (and back) so the device acts as an unmanaged Wi-Fi-to-wired L2 switch.
      Fixed BSS frame buffers, no heap; one build flag, off by default (raw injection is
      powerful - keep it opt-in).

## Industrial / standards protocols - delivered

### Test & measurement / lab instrumentation

Casting the interop net over bench and rack test equipment: a wireless ESP32 becomes a lab-instrument
bridge/controller. SCPI is near-universal across modern instruments and is the highest-value first step;
the LXI transports (VXI-11 / HiSLIP) and a GPIB gateway extend reach to older and higher-throughput gear.

- [x] **SCPI / IEEE 488.2** (S, universal instrument control - **high value**) - SHIPPED
      (`services/scpi`, `PC_ENABLE_SCPI`, example `L7-Application/Scpi`). Standard Commands for Programmable Instruments,
      the text command language nearly every modern bench instrument speaks (oscilloscopes, DMMs, power
      supplies, function/arbitrary generators, SMUs, spectrum/network analyzers, electronic loads) over
      a raw TCP socket on **port 5025** (also USBTMC / serial). A zero-heap codec: a SCPI command
      builder (`pc_scpi_build` - headers, `:` hierarchy, parameter formatting), the 13 mandatory IEEE
      488.2 common commands (`pc_scpi_common`: `*IDN?` / `*RST` / `*OPC?` / `*CLS` / `*ESR?` /
      `*STB?` / ...), response parsers (numeric NR1/NR2/NR3, boolean, string, definite + indefinite
      arbitrary block `#<n><len><data>` for waveform captures), the status-byte / event-status-register + enable-mask / error-queue model (`ScpiStatus`), and a SCPI short/long-form + numeric-suffix
      header matcher (`pc_scpi_match`). Makes the device a bench-instrument controller or a wireless
      bridge that fans instrument telemetry into HTTP/MQTT. Register bits, error-number classes, and the
      exact standard error strings verified against SCPI-1999 + IEEE 488.2-1992; host-tested
      (`native_scpi`). _Remaining (separate roadmap items):_ the HiSLIP / VXI-11 / GPIB-gateway
      transports below.
- [x] **HiSLIP** (M, modern LXI transport, IVI Foundation) - SHIPPED (`services/hislip`,
      `PC_ENABLE_HISLIP`, example `L7-Application/HiSlip`). High-Speed LAN Instrument Protocol on
      **port 4880**, the current LXI transport that replaced VXI-11: a binary message protocol with
      separate synchronous/asynchronous channels, message-type framing (the full `HislipMsg` enum
      0-38 incl. the 2.0 TLS/SASL types), message-id sequencing (initial `0xFFFFFF00`, increment-by-2),
      and much higher throughput than VXI-11. Carries SCPI as its payload; the codec is the fixed
      16-byte header build/parse + the Initialize / AsyncInitialize two-channel handshake + Data /
      DataEND framing. Header layout, message-type codes, and handshake vectors verified against
      IVI-6.1 (cross-checked with the Wireshark dissector, MSL-equipment, PyHiSLIP); host-tested
      (`native_hislip`, byte-exact vectors). _Remaining (separate roadmap items):_ VXI-11 + the
      GPIB-gateway below.
- [x] **LXI / VXI-11** (M, ONC RPC instrument channel) - SHIPPED (`services/vxi11`,
      `PC_ENABLE_VXI11`, example `L7-Application/Vxi11`). The older LAN eXtensions for Instrumentation
      transport: VXI-11 over ONC/Sun RPC (portmapper **port 111** -> the dynamic instrument channel via
      `pc_vxi11_build_getport`), the `create_link` / `device_write` / `device_read` / `device_readstb`
      / `destroy_link` calls with response parsers, and the **reusable ONC RPC / XDR codec**
      (record-marking + CALL/REPLY framing with AUTH_NONE over big-endian 4-byte-aligned XDR). Verified
      against the VXI-11 spec + RFC 5531/4506/1833 (cross-checked with python-vxi11 + the Wireshark
      dissector); host-tested (`native_vxi11`, byte-exact create_link vector). _Remaining:_ the LXI
      mDNS/VXI-11 discovery beacon (low-pri) and the GPIB gateway below.
- [x] **GPIB-over-LAN gateway** (S, Prologix-style) - SHIPPED (`services/gpib`, `PC_ENABLE_GPIB`,
      example `L7-Application/Gpib`). The Prologix `++`-command GPIB-Ethernet gateway control set
      (`++addr`, `++read`, `++eoi`, `++eos`, `++mode`, `++spoll`, ... via `pc_gpib_command` + typed
      helpers), the data-line escaping (leading ESC before CR/LF/ESC/`+`, byte-exact against the
      manual), the command-vs-data classifier, and the response parsers, so the device drives a bench
      of legacy IEEE-488 (GPIB) instruments through a common Prologix-compatible adapter - the bridge
      into pre-LAN test gear that will never speak SCPI-over-TCP directly. Verified against the
      Prologix GPIB-Ethernet / GPIB-USB manuals (cross-checked with AR488); host-tested
      (`native_gpib`). **This completes the instrument-control cluster** (SCPI + HiSLIP + VXI-11 +
      GPIB).

### Intelligent Transportation Systems (ITS)

### IoT device management

### Messaging & RPC

- [x] **STOMP** (M, messaging) _(shipped)_ - `PC_ENABLE_STOMP` (`services\stomp`):
      a zero-heap STOMP 1.2 frame codec - `pc_stomp_build_frame()` writes a frame (command +
      escaped `key:value` headers + blank line + NUL-terminated body) and
      `pc_stomp_parse_frame()` is a non-mutating cursor that reports the command, header
      key/value slices, and body (honoring `content-length`, tolerating `\r\n` endings,
      skipping broker heart-beats), with `pc_stomp_header()` lookup and `pc_stomp_unescape()` for
      header escapes (`\r` `\n` `\c` `\\`). Drives `CONNECT` / `SEND` / `SUBSCRIBE` /
      `MESSAGE` / `ACK` ... against ActiveMQ / RabbitMQ / Artemis over the shipped outbound
      client transport (or STOMP-over-WebSocket via the WS client); pure, host-tested. The
      connection / subscription state is the application's.
- [x] **NATS** (M, messaging) _(shipped)_ - `PC_ENABLE_NATS` (`services\nats`): a
      zero-heap codec for the text-based NATS pub/sub protocol - builders for
      `CONNECT` / `PUB` (with optional reply-to) / `SUB` (with optional queue group) /
      `UNSUB` / `PING` / `PONG`, and `pc_nats_parse()` which decodes an inbound
      `MSG` / `INFO` / `PING` / `PONG` / `+OK` / `-ERR` (MSG yields subject / sid / reply-to /
      payload). Line-oriented (CRLF), space-delimited, payload only on PUB/MSG; pure,
      host-tested. Rides the outbound client transport; the connection / subscription state is
      the application's.
- [x] **Sparkplug B** (M, industrial IoT) _(shipped)_ - `PC_ENABLE_SPARKPLUG`
      (`services\sparkplug`, implies Protobuf): a zero-heap builder for the Eclipse Sparkplug B
      MQTT payload + topic - `pc_spb_build_topic()` (`spBv1.0/group/type/node[/device]`),
      `pc_spb_build_metric()` (a Tahu Metric: name / alias / timestamp / datatype + an int / long
      / float / double / boolean / string value), and `pc_spb_build_payload()` (timestamp +
      metrics + seq) over the Protobuf codec. Field numbers + datatype codes verified against
      the Eclipse Tahu sparkplug_b.proto; pure, host-tested. Publish it with the MQTT client.
- [x] **WAMP** (M, web messaging) _(shipped)_ - `PC_ENABLE_WAMP` (`services\wamp`): a
      zero-heap codec for the Web Application Messaging Protocol (unified RPC + PubSub over
      WebSocket, subprotocol `wamp.2.json`). Builders for HELLO / SUBSCRIBE / UNSUBSCRIBE /
      PUBLISH / CALL / REGISTER / YIELD / GOODBYE (JSON arrays emitted via the shared
      `JsonWriter`; Options/Details default to `{}`, Arguments / ArgumentsKw passed as JSON
      literals) and a nesting-aware positional parser (`pc_wamp_get_type` / `pc_wamp_get_uint` /
      `pc_wamp_get_uri` / `pc_wamp_element`) that pulls the message type, ids, and URIs out of an
      inbound WELCOME / SUBSCRIBED / EVENT / RESULT / INVOCATION / ERROR. Message codes
      verified against the WAMP spec; pure, host-tested. It rides the shipped WebSocket
      layer; the session / subscription / registration tables are the application's. The
      caller can still serialize payloads with the MessagePack / CBOR codecs.
- [x] **CloudEvents** (S-M, CNCF spec) _(shipped)_ - `PC_ENABLE_CLOUDEVENTS`,
      `services/cloudevents`: `pc_cloudevents_build_json()` emits a structured
      `application/cloudevents+json` envelope (required `id` / `source` / `type` +
      `specversion` 1.0, optional `subject` / `datacontenttype` / `data` - `data` either a
      verbatim JSON value or an escaped string) over the existing JSON writer, and
      `pc_cloudevents_from_headers()` reads an inbound binary-mode event's `ce-*` headers.
      Host-tested (`test_cloudevents`, 7 cases). The HTTP body / `ce-*` header bindings
      are emitted with the normal send / `add_response_header` paths; an app reuses the
      same envelope over MQTT / WebHook. Fixed BSS, no heap, one build flag.
- [x] **MQTT-SN** (M, sensor networks) _(shipped)_ - `PC_ENABLE_MQTT_SN`
      (`services\mqtt\mqtt_sn`): a zero-heap MQTT-SN v1.2 wire codec for the UDP / non-TCP
      MQTT variant on constrained, lossy links (numeric topic IDs instead of strings,
      gateway discovery, sleeping-client keep-alive). Builders for CONNECT / REGISTER /
      PUBLISH / SUBSCRIBE (by name or pre-defined id) / PINGREQ / DISCONNECT / SEARCHGW and
      a `pc_mqttsn_parse_header()` (both the 1- and 3-octet Length forms, big-endian fields) +
      typed parsers for CONNACK / REGACK / PUBACK / SUBACK / PUBLISH / REGISTER, with a
      `pc_mqttsn_make_flags()` helper (DUP / QoS / retain / will / clean / TopicIdType). Wire
      bytes verified against the spec + the Eclipse Paho reference; pure, host-tested. The
      datagram send (`pc_udp_sendto`), topic-ID registry, and sleep/retransmit state are
      the application's. Pairs with the existing MQTT client.

### Network telemetry

- [x] **NetFlow / IPFIX** (M, flow export) _(shipped)_ - `PC_ENABLE_FLOW_EXPORT`
      (`services\flow_export`): a zero-heap exporter-side codec for on-device flow
      accounting. **NetFlow v5** is fixed (`flow_v5_write_header` + `flow_v5_write_record`,
      24- and 48-octet layouts); **NetFlow v9** (RFC 3954) and **IPFIX** (RFC 7011) share a
      small cursor (`FlowWriter`) - `flow_ipfix_begin` / `flow_v9_begin`, then
      `flow_export_template()` (a Template Set/FlowSet), `flow_export_data_begin/record/end`
      (matching Data Set), and `flow_export_finish()` which patches the IPFIX message length
      or the v9 record count (and pads each v9 FlowSet to a 4-octet boundary). Field offsets
      verified against RFC 7011 / RFC 3954 / the published v5 layout; pure, host-tested. The
      flow cache (5-tuple + counters) and the UDP send (`pc_udp_sendto`) are the app's.

### Building automation

### Databases & time-series

- [x] **InfluxDB Line Protocol** (S, time-series ingest) _(shipped)_ - the
      `pc_line` builder (`services/udp_telemetry`, `PC_ENABLE_UDP_TELEMETRY`) emits
      the full `measurement,tag=v field=v timestamp` form: `pc_line_add_tag()` (escaped
      tag set, must precede fields) + `pc_line_set_timestamp()` on top of the existing
      int/uint/float fields. Host-tested (`test_udp_telemetry`: tags+timestamp ordering,
      special-char escaping, tag-after-field fail-closed). Cast over UDP
      (`pc_udp_telemetry_cast`) or POST the same line to InfluxDB `/write` with the
      shipped HTTP client. Pairs with the telemetry-math service. One build flag, no heap.

### Motor / actuator control (ESC protocols)

For drone / robotics use - drive ESCs and ingest their telemetry straight from the
web server, using the ESP32's hardware timing peripherals (RMT / MCPWM) so the
microsecond-precise pulse trains run without locking the CPU. Each behind a build
flag; fixed BSS, no heap.

### Network Time Security (NTS, RFC 8915)

Authenticated time - the secure successor to plain NTP (the current
`PC_ENABLE_NTP` client trusts whatever the network hands it, so a MITM can shift
the device's clock and break TLS validity windows, JWT/OIDC `exp`, TOTP, and log
timestamps). NTS adds cryptographic authentication on top of NTPv4 with no shared
secret. Builds on the existing NTP client + the TLS stack + AEAD primitives; fixed
BSS, no heap, behind a build flag.

> The plain (unauthenticated) NTP **server** already ships (`PC_ENABLE_NTP_SERVER`,
> RFC 5905 server mode on UDP/123 - see the feature reference; example NtpServer pairs
> it with a GPS stratum-1 source + upstream-NTP fallback). NTS below hardens the time the
> device _consumes_; a future NTS server mode could extend the server the same way.

### Documentation: Sphinx over Doxygen (do last)

> The final roadmap item - tackle only after everything else on the TODO and the
> roadmap above is checked off.

The docs today are Doxygen + a custom CSS theme. The goal is to add **Sphinx** on top and
then apply **"squirty"** styling over it for a polished, modern docs site.

- [x] **Sphinx + Breathe bridge** (L) _(shipped)_ - the Sphinx project lives in `docs/sphinx`: `docs/Doxyfile`
      now emits XML (`GENERATE_XML=YES` -> `docs/sphinx/xml`), **Breathe** renders it into the API reference
      (the `PC` public surface), and **MyST** pulls the hand-written Markdown guides
      (`ARCHITECTURE.md`, the `docs/learn` primers) into the same site via `include` stubs (one source of
      truth). Built + verified on the RPi (doxygen + a `~/.sphinxdocs` venv from `docs/sphinx/requirements.txt`,
      then `sphinx-build`), and kept buildable by the **Docs (Sphinx)** CI workflow
      (`.github/workflows/docs-sphinx.yml`, build + artifact). The full per-symbol `doxygenindex` is not
      inlined (the Sphinx C++ domain does not parse every macro-heavy declaration); that complete reference
      stays in the Doxygen HTML.
- [x] **Apply "squirty" over the Sphinx theme** (M) _(shipped)_ - "squirty" is the project brand: the
      **Squirty the Injection Squid** mascot (`docs/squirty.svg`) plus the **Retro TTY Green Screen** palette
      (phosphor `#2bb35a` on a near-black `#080c08` CRT, from `docs/custom.css`). Layered over **Furo** via
      `docs/sphinx/conf.py` (light/dark brand css-variables) + `docs/sphinx/_static/squirty.css` (the mascot
      logo, phosphor glow, a faint scanline wash, monospace headings, terminal-styled Breathe signatures),
      light/dark aware.

### One folder per implemented service

> Convention: a service that has both a `.h` and a `.cpp` lives in its own `src/services/<name>/`
> directory; only header-only services (a `.h` with no `.cpp`) are allowed to sit at the
> `src/services/` root. Today nine root-level services break this.

- [x] **Move each `.h`+`.cpp` service at the `src/services/` root into its own folder** (M) _(shipped)_ - cloudevents,
      mdns_service, pc_ntp_service, ota_service, provisioning_service, redis_resp, stomp, upload_service, and
      web_terminal currently live as loose `src/services/<name>.{h,cpp}` pairs. Move each into
      `src/services/<name>/<name>.{h,cpp}`, update the `#include` paths, the `build_src_filter` globs in
      `platformio.ini`, the compile-DB / dep-graph tooling, and any docs references. Header-only services
      (e.g. `clock.h`, `i2c.h`) stay at the root. Pure relocation - all builds + tests stay green.

### Perf: unroll small pool scans to bitflag + bitmask compares

> Hot-path validity/membership checks that loop over a small fixed pool (conn_pool, ws_pool, pc_sse_pool,
> the SSH per-conn tables) should be O(1) bitflag+mask ops or fully-unrolled branchless compares, not
> `for` scans. The stale-pcb guard in `pc_tcp_do` (`pcb_still_bound`) is the motivating case: SEND/OUTPUT
> already reduce to the O(1) `k->pcb == conn_pool[k->slot].pcb` slot compare; only RAWSEND (TLS BIO, slot
> unknown) still scans.

- [x] **Replace the accept free-slot scan with a live-slot bitmask** (M) - DONE. `ConnPoolCtx::free_mask` (a
      `std::atomic<uint32_t>`, bit i = slot i is CONN_FREE) is kept in lock-step with every `conn_pool[i].state`
      write through one choke point, `pc_conn_set_state()` (the owner pattern - the ~13 scattered state writes
      now route through it), so the accept path's free-slot lookup is one `__builtin_ctz` (`pc_conn_alloc_free`)
      instead of a `MAX_CONNS` linear scan. Atomic because CONN_FREE is written from both the tcpip callbacks and
      the worker (`check_timeouts`). Measured on the ESP32-S3, worst case (only the last slot free): the real
      allocator is **86 -> 30 cyc (2.87x)** - the `pc_atomic` state reads make the scan 8 atomic loads. Verified
      host-side (`test_freeslot_bitmask_alloc`: claim / free / full / lowest-first / CLOSING-reserved) and by a
      connection-churn stress on an ESP32-P4 that behaves identically to the pre-bitmask scan (the pool fully
      recovers after overload - no mask desync/leak).
- [x] **SWAR / bit-sliced constant-time base64 decode** (M) - DONE (`PC_BASE64_SWAR`, default on). The mbedTLS
      decode delegation was replaced with a portable **branchless constant-time** codec
      (`src/network_drivers/presentation/codec/base64/base64.cpp`) offering **two selectable implementations**, both
      constant-time (every alphabet range is an arithmetic mask, no branch and no data-indexed table): a scalar
      one-char-at-a-time path, and the default **SWAR** path that packs 4 characters into a word and classifies
      all four lanes in parallel with guard-bit range masks (every base64 char is < 0x80, so borrows never cross
      lanes). Measured on the ESP32-S3, decoding a credential: **SWAR 882 cyc, scalar 1639 cyc, mbedTLS 4728 cyc** - SWAR is **1.9x faster than the scalar path and 5.36x faster than mbedTLS** (~8.4x on 1 KiB), and both are
      **timing-invariant** (0.00-cycle spread across alphabet-spanning inputs). This also closed a timing gap on
      the `base64url` (JWT) path - previously a plain branchy software decoder - and dropped the mbedTLS base64
      dependency. Verified with RFC 4648 vectors both directions + a 10,000-input differential vs Python `base64`,
      run against **both** implementations (`test_base64` in env:native = SWAR default and env:native_base64_scalar).
- [x] **Hand-rolled SSE record framer** (S) - DONE. `pc_sse_format()` now builds each `event:`/`id:`/`data:`
      record with a branchless `memcpy` framer (fixed field prefixes + `strlen`/`memcpy` of each value + the
      terminators) instead of three `snprintf("%s")` calls, avoiding the expensive Xtensa `vsnprintf` path.
      Measured on the ESP32-S3 in a same-input A/B: **2902 -> 789 cyc (3.68x)** for a fully-addressed record;
      ~8x on the host (180 -> 22 ns). Output is byte-identical - the existing `test_sse_format` asserts the
      exact WHATWG event-stream bytes, and it stayed green. A real win for a high-rate broadcast fan-out.

### Radar presence sensor drivers (mmWave + Doppler)

> Two more presence radars for the EM / radar presence + motion section above (LD2410 is the shipped
> reference codec): a framed-UART mmWave module and a bare-GPIO Doppler module - opposite ends of the
> presence-sensing cost/capability range. Both bridge northbound to the web stack like the other sensor
> drivers; a device reads them over UART / GPIO and publishes presence.

- [x] **Waveshare HMMD 24 GHz mmWave presence sensor** (M) - human micro-motion detection (moving /
      standing / stationary body plus a target distance gate) built on the **S3KM1110** 24 GHz FMCW radar
      SoC behind a **PY32F003** MCU, exposed over a 3.3V UART (default **115200** baud) plus a GPIO presence
      pin ([waveshare.com/wiki/HMMD_mmWave_Sensor](https://www.waveshare.com/wiki/HMMD_mmWave_Sensor)). Add
      `PC_ENABLE_HMMD` / `services/hmmd`: a pure, host-tested **little-endian** frame codec - the default
      ON/OFF + distance-gate report parse and a byte-by-byte resyncing stream reassembler (mirror the
      `Ld2410Stream` reassembler), plus the command encoders (read firmware version `0x0000`, serial number
      `0x0011`, register `0x0002`, parameter config `0x0008`). Bridge presence / distance northbound exactly
      like LD2410. Test = captured-frame vectors both directions (`native_hmmd`); the sensor's GPIO OUT pin
      shares the debounced presence bridge below.
      SHIPPED - `services/hmmd` (`PC_ENABLE_HMMD`). The framing is LD2410's exactly, so the reassembler
      mirrors `Ld2410Stream` (including the widen-before-compare guard against a wrapping length field);
      the report is a fixed 35-octet payload - detect(1) + distance(2) + 16 gates x 2 - which makes a
      45-octet frame, agreeing exactly with the reference library's own `kMaxFrameLength` and so
      cross-checking the layout. Command encoders cover open/close command mode plus the firmware
      (0x0000), serial (0x0011), config (0x0008) and register (0x0002) reads over a public
      `pc_hmmd_cmd_build`, and `pc_hmmd_parse_ack` decodes replies. Framing, payload layout and
      command encoding come from the public `2Grey/s3km1110` reference (the Waveshare wiki refuses
      automated fetches); no vendor SDK is used. Host-tested (`native_hmmd`, 11 cases). Two deliberate
      limits: the register **selector payload** is passed through verbatim rather than modelled, because
      the reference does not specify the register map and inventing one would be a guess; and the ACK
      payload is handed back whole rather than split into a status word, since the reference establishes
      only that the ACK echoes the request's command octet. The GPIO OUT pin reuses `PresenceCore` from
      `PC_ENABLE_RCWL0516` at the application level, so this service takes no dependency on that one.
      Caveat: vectors are constructed from the documented layout, **not** captured off a physical module,
      and nothing here has been HW-verified.
- [x] **RCWL-0516 microwave Doppler presence sensor** (S) - the low-cost ~3.18 GHz Doppler motion module
      (RCWL-9196 controller + MMBR941M RF amp): no data protocol at all - a single 3.3V **OUT** pin that
      latches HIGH for ~2 s on any moving reflector and returns LOW when idle. This is the analog-Doppler end
      of the presence path already noted in the EM / radar section. Add `PC_ENABLE_RCWL0516` /
      `services/rcwl0516`: a tiny debounced edge-detect + hold-time state machine over one GPIO (pure,
      host-testable by injecting pin levels against a fake clock via `pc_millis`), surfaced to the web
      stack as a boolean presence event - the same one-GPIO presence facade the HMMD OUT pin and other
      bare-output detectors (PIR, HB100) can reuse.
      SHIPPED - `services/rcwl0516` (`PC_ENABLE_RCWL0516`). `PresenceCore` is the facade, written
      sensor-agnostic from the start so the HMMD entry above can reuse it: debounce
      (`PC_RCWL0516_DEBOUNCE_MS`, default 50) rejects comparator chatter, and a hold
      (`PC_RCWL0516_HOLD_MS`, default 2000, matching the module's retrigger window) bridges the gaps
      between retriggers into one continuous occupied span. `pc_presence_take_event` consumes a
      changed-flag once per transition, so callers publish an event per edge rather than a level per
      poll. Pure, explicit `now` (the `services/hotswap` pattern), starts fail-safe absent, and every
      elapsed test is an unsigned difference so it is wrap-safe across a `millis()` rollover.
      Host-tested (`native_rcwl0516`, 10 cases - incl. chatter rejection, retrigger-gap bridging, the
      rollover, and the degenerate zero-debounce / zero-hold configs). Not HW-verified against a
      physical module; the timing constants come from the module's documented ~2 s retrigger window.
- [x] **HLK-LD2410B (Bluetooth variant of the shipped LD2410)** (S) - the LD2410**B** is HiLink's
      BLE-equipped build of the same 24 GHz FMCW presence radar, speaking the **same `FD FC FB FA` framed
      UART protocol** the shipped `services/ld2410` codec already builds and parses
      ([datasheet](https://en.ai-thinker.com/Uploads/file/20231016/20231016032622_13559.pdf)). So this is a
      **verification + thin extension** of the existing driver, not a new codec: confirm the LD2410B report /
      ACK frames decode byte-for-byte with `pc_ld2410_parse_report` + the `Ld2410Stream` reassembler (add
      captured-frame vectors to `native_ld2410`), document it as a supported part, and add the few
      LD2410B-only config commands (Bluetooth enable / permission, MAC query) to the `FD FC FB FA` command
      encoders. The BLE control channel itself is out of scope for the wired driver - the UART report / config
      path is the shared surface.
      SHIPPED - `services/ld2410` now covers the B as a supported part. Added the LD2410B-only config
      commands over the wired UART: Bluetooth on/off (`pc_ld2410_cmd_bluetooth`, word 0x00A4), MAC query
      (`pc_ld2410_cmd_get_mac`, 0x00A5), and set Bluetooth password (`pc_ld2410_cmd_set_bt_password`,
      0x00A9). Also added the command-ACK decoder the driver never had - `pc_ld2410_parse_ack` /
      `pc_ld2410_ack_ok` / `pc_ld2410_ack_mac` - since a query is useless without it (ACKs echo the
      request word with 0x0100 set, then a 2-octet status). "Obtain Bluetooth access" (0x00A8) is
      deliberately NOT implemented: the protocol document states it answers only over Bluetooth and not the
      serial port, so it belongs to the out-of-scope BLE channel. Host-tested in `native_ld2410` (+3 cases,
      11 total) with frames taken byte-for-byte from the worked examples in LD2410B Serial communication
      protocol V1.07. Caveat: those vectors are from the protocol document, **not** a capture off physical
      LD2410B hardware, and none of this has been HW-verified against a real B module - the report path is
      unchanged and shared with the already-verified LD2410, but the B-only commands are untested on metal.
      (Note: the doc's prose for get-MAC says "1 byte fixed type + 3 bytes MAC", which contradicts its own
      worked example and length field - both of which say 6 MAC octets. The example is what we implement.)

## Industrial / standards protocols - delivered (individual items)

- [x] **umati / OPC UA for Machine Tools** (M, OPC 40501-1) _(MachineTool model shipped)_ -
      `PC_ENABLE_UMATI` (`services/umati`, requires `PC_ENABLE_OPCUA`): the umati companion-spec
      MachineTool information model - Identification, Monitoring (MachineTool / Channel / Spindle /
      Axis_X..Z), Production, and Notification - served through the OPC UA Browse + Read resolvers out of a
      caller-owned `UmatiMachineTool` struct, with faithful BrowseNames per OPC 40501-1 (namespace
      `http://opcfoundation.org/UA/MachineTool/`). Host-tested (`native_umati`, 11/11) and ESP32-compiled
      (example Umati); a read-only monitoring model any umati / OPC UA client browses and reads by
      BrowseName. _Remaining:_ companion-spec TypeDefinitions + the MachineTool namespace in the server
      NamespaceArray (needs array-Variant support), multiple channels/spindles, and Equipment (tool magazine).
- [x] **Fanuc FOCAS** (L, CNC data collection) - SHIPPED (`services/focas`, `PC_ENABLE_FOCAS`). The
      FANUC Open CNC API's Ethernet wire protocol (TCP 8193) as a zero-heap client codec - no `fwlib32`
      (the licensed FANUC binary, part # A02B-0207-K732/K737, which cannot run on an ESP32) is used or
      required. `pc_focas_build_*` emit the on-wire frames (a 10-octet big-endian magic/version/type/length
      envelope + payload) for the open/close handshake and the documented read functions (SysInfo, alarm
      status, CNC parameters, macro variables, position/axis data, actual feed / spindle), and
      `pc_focas_parse_*` decode the responses (echoed selector + FOCAS return code + data), including ODBSYS
      and the FANUC 8-octet `data / base^exp` value encoding. Frame layout, selector set, and value
      decoding reverse-engineered vs `diohpix/pyfanuc`; host-tested (`native_focas`, 11/11) and
      **byte-cross-checked against an independent Python reference** (`test/servers/peers/pc_focas_peer.py`,
      which also serves a mock FANUC CNC for on-device interop). HW verification against a real FANUC
      control pending a rig. Fixed BSS, no heap. Refs: pyfanuc, node-red-contrib-fanuc-focas.
- [x] **Beckhoff ADS / AMS** (L, TwinCAT) - SHIPPED (`services/ads`, `PC_ENABLE_ADS`). The
      Automation Device Specification over TCP 48898: the AMS/TCP + AMS header (target-before-source
      AMSNetId + port, command id, state flags, cbData, invoke id) plus the ADS commands (ReadDeviceInfo,
      Read, Write, ReadWrite, ReadState, WriteControl, Add/Delete/DeviceNotification) that read + write PLC
      symbols by index group/offset or by handle (name -> handle via 0xF003, value via 0xF005). Header
      layout + command ids verified against the Beckhoff InfoSys spec; payloads cross-checked with
      Beckhoff's ADS library, `pyads`, and Apache PLC4X. Host-tested (`native_ads`), example AdsClient.
      Fixed BSS, no heap.
- [x] **Heidenhain LSV/2** (M, CNC DNC) - SHIPPED (`services/lsv2`, `PC_ENABLE_LSV2`, host-tested
      `native_lsv2`). The LSV/2 protocol Heidenhain TNC controls use for DNC + data access, over serial or
      LSV/2-over-TCP (port 19000): the telegram framer (a 4-byte big-endian payload-length prefix + a
      4-character command / response mnemonic + payload, the length counting the payload only), the typed
      request builders (login / logout `A_LG` / `A_LO` with the privilege groups, the null-terminated-filename
      file commands `R_FL` / `C_FL` / `C_FD` / `C_DC` / `C_DM` / `C_DD`, and the `R_RI` run-info request with a
      2-byte selector), and the response readers (`T_OK`, the `T_ER` / `T_BD` two-byte error-class + error-code,
      and the `S_*` data replies). Framing + mnemonics + payload layouts cross-checked byte-for-byte against the
      `pyLSV2` reference. A CNC-native southbound source for the common European control alongside `services/focas` + `services/haas_mdc`. Fixed BSS, no heap; the serial / TCP link is the application's.
- [x] **EUROMAP 77 / 83** (M, OPC UA for plastics) - the injection-molding / rubber-machine OPC UA
      companion specs (EUROMAP, euromap.org NodeSet), the plastics analogue of **umati**. Layers on the
      OPC UA server exactly like `services/umati` (the machine/mould/job model through the Browse + Read
      resolvers out of a caller struct) - reuses the umati pattern, low risk, high value for molding shops.
      SHIPPED - services/machine_tool/euromap77 (PC_ENABLE_EUROMAP77): the IMM_MES_Interface hierarchy (MachineInformation,
      MachineStatus, Jobs / ActiveJob + ActiveJobValues with the UInt64 production counters) served through the
      OPC UA Browse + Read resolvers out of a caller-owned EmImm, faithful BrowseNames per the EUROMAP 77
      NodeSet + enums from EUROMAP 83. Added Int64/UInt64 to the OPC UA Variant codec for the 64-bit counters.
      Pure resolver, host-tested (native_euromap77 + the opcua Variant round-trip), example Euromap77.
- [x] **CiA 402 drive profile** (M, IEC 61800-7-201) - SHIPPED (`services/cia402`, `PC_ENABLE_CIA402`,
      host-tested `native_cia402`). The standardized servo/stepper drive + motion profile over the
      existing **CANopen** (`services/canopen`) / EtherCAT-CoE: the state machine (Controlword /
      Statusword decode with the mask/value table), Modes of Operation (Profile Position / Velocity /
      Torque + Cyclic Sync), the enable-sequencer bring-up loop, the target/actual
      position-velocity-torque SDO setters/getters, and cyclic PDO pack/unpack. Turns the CAN stack
      into a motion master; pairs with the dshot / ESC actuator set.
- [x] **PackML / OMAC** (M, ISA-88 / OMAC PackTags) - the packaging-machine state model: the PackML state
      machine (Idle/Execute/Held/Suspended/Aborted/...) plus the Admin/Status/Command PackTags, usually
      surfaced over OPC UA - the state engine + tag model as a fixed-BSS service.
      SHIPPED - services/machine_tool/packml (PC_ENABLE_PACKML): the pure ISA-TR88.00.02 (ISBN 978-1-64331-224-8)
      17-state transition engine (pc_packml_command / _state_complete / _execute_complete + command
      validity, StateCurrent wire numbers) and the owned PackTags service (state advance, Prod counters,
      unit-mode-change rules, MachSpeed, StateCurrentTime / AccTimeSinceReset timers). Pure codec,
      host-tested (native_packml, 16 cases) + HW-verified on an ESP32-P4 (full state graph + fault branch + illegal-command 409s driven live over HTTP), example PackML.
- [x] **Haas Machine Data Collection** (S, serial Q commands) - the documented Haas MDC serial protocol
      (the `Q` command set: Q100 machine serial, Q500 program+status, Q600 macro-variable read) that Haas
      CNC controls answer over RS-232 / the network port. A small, fully-documented CNC read source.
      SHIPPED - services/machine_tool/haas_mdc (PC_ENABLE_HAAS_MDC): the `?Q` query builders + the `>`-wrapped /
      DPRNT response parser. Pure codec, host-tested (native_haas_mdc), example HaasMdc.
- [x] **OPC UA Robotics (OPC 40010)** - the VDMA Robotics OPC UA companion spec (the MotionDevice model:
      axes, controller, safety state), another umati-pattern model on the OPC UA server; pairs with the
      Siemens **Run MyRobot** item and **umati** for robot cells.
      SHIPPED - services/machine_tool/robotics (PC_ENABLE_ROBOTICS): the MotionDeviceSystem hierarchy (MotionDevices /
      MotionDevice / ParameterSet / PC_ROBOTICS_AXES parametric Axes, Controllers / Controller / Software,
      SafetyStates / SafetyState) served through the OPC UA Browse + Read resolvers out of a caller-owned
      RoboticsMotionDeviceSystem, faithful BrowseNames per OPC 40010-1. Pure resolver, host-tested
      (native_robotics), example Robotics; the twin of umati.
- [x] **SunSpec Modbus** (M, DER device models) _(shipped)_ - `PC_ENABLE_SUNSPEC`
      (`services\sunspec`): a zero-heap codec for the SunSpec Alliance register maps layered
      on the holding-register model. A model-chain walker (`pc_sunspec_check_marker` /
      `pc_sunspec_begin` / `pc_sunspec_next_model` - verify the `SunS` 0x53756E53 marker, then
      iterate each model's id / length / body to the 0xFFFF end model) + typed point readers
      (`pc_sunspec_u16` / `_i16` / `_u32` / `_i32` / `_string`) and a map writer
      (`pc_sunspec_write_marker` / `_model_header` / point writers / `_write_end_model`), so a
      solar inverter / meter / battery is interoperable. Marker + header format verified
      against the SunSpec spec; pure, host-tested. Pairs with the shipped Modbus
      ([modbus](../src/services/fieldbus/modbus/)).
- [x] **DiffServ QoS marking (RFC 2474)** (S) - DONE (`PC_ENABLE_DIFFSERV`). The transport stamps the 6-bit
      DSCP into the IP DS field (the high 6 bits of the TOS byte) of outbound traffic so a QoS-aware network -
      and the Wi-Fi WMM access-category mapping - prioritizes the Expedited-Forwarding class (EF, DSCP 46) and
      the like over best-effort. Three levels of control in `network_drivers/transport/diffserv.h`:
      `pc_set_default_dscp()` (server-wide, all outbound TCP), `pc_listen_set_dscp(port, dscp)` (per-listener),
      and `pc_conn_set_dscp(slot, dscp)` (per-connection - any 0-63, for real per-flow QoS or arbitrary tagging
      in network testing), plus `pc_udp_set_dscp()` for datagrams. The DSCP is written to the pcb TOS on
      tcpip_thread as the connection is accepted / connected, so nothing is added to the send hot path (marking
      applies from the first data segment - the SYN-ACK stays best-effort: it is emitted before any app callback
      and ESP32 lwIP does not inherit the listen-pcb TOS, HW-tested by stamping the listen pcb and confirming the
      SYN-ACK is still tos 0x0). Host-tested (`native_diffserv`) and HW-verified on an ESP32-P4 off the wire with
      tcpdump: server default EF on `:80` (tos 0xb8), per-listener AF41 on `:8080` (tos 0x88), per-connection
      re-tag to CS6 (tos 0xc0), and UDP datagrams sent marked CS6 (same `pcb->tos` path as the verified TCP).
      Example DiffServ.
